#include "AppController.h"

#include <QDateTime>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QClipboard>
#include <QSettings>
#include <QtLogging>

namespace {

QString hostOf(const QString& url) {
    const QUrl parsed(url);
    return parsed.host().isEmpty() ? url : parsed.host();
}

}

AppController::AppController(QObject* parent)
    : QObject(parent) {
    connect(&m_pool, &RelayPool::eventReceived, this, &AppController::handleEvent);
    connect(&m_pool, &RelayPool::okReceived, this, [this](const QString& relay, const QString&, bool accepted, const QString& message) {
        if (!accepted)
            setStatus(hostOf(relay) + QStringLiteral(": ") + message);
        else
            setStatus(QStringLiteral("accepted by ") + hostOf(relay));
    });
    connect(&m_pool, &RelayPool::noticeReceived, this, [this](const QString& relay, const QString& message) {
        setStatus(hostOf(relay) + QStringLiteral(": ") + message);
    });
    connect(&m_pool, &RelayPool::relayStateChanged, this, [this](const QString& url, const QString& state, const QString& detail) {
        if (!url.isEmpty()) {
            m_relayState.insert(url, state);
            m_relayDetail.insert(url, detail);
        }
        emit relaysChanged();
    });
    m_profileTimer.setSingleShot(true);
    m_profileTimer.setInterval(250);
    connect(&m_profileTimer, &QTimer::timeout, this, &AppController::subscribeProfiles);
    m_clock.setInterval(30000);
    connect(&m_clock, &QTimer::timeout, &m_timeline, &TimelineModel::refreshTimes);
    m_clock.start();
    load();
}

bool AppController::hasIdentity() const { return m_keys.has_value(); }
QString AppController::npub() const { return m_keys ? m_keys->npub() : QString(); }
QString AppController::pubkey() const { return m_keys ? m_keys->pubkeyHex() : QString(); }
QString AppController::nsec() const { return m_keys ? m_keys->nsec() : QString(); }
QString AppController::displayName() const { return m_displayName; }
QString AppController::about() const { return m_about; }
QString AppController::statusText() const { return m_status; }
int AppController::feedMode() const { return m_feedMode; }
int AppController::openRelays() const { return m_pool.openCount(); }
int AppController::relayCount() const { return m_pool.relays().size(); }
TimelineModel* AppController::timeline() { return &m_timeline; }
bool AppController::replying() const { return m_reply.has_value(); }
int AppController::followCount() const { return m_follows.size(); }

QString AppController::replyLabel() const {
    if (!m_reply)
        return {};
    return m_reply->name.isEmpty() ? m_reply->pubkey.left(8) : m_reply->name;
}

QVariantList AppController::relays() const {
    QVariantList list;
    for (const QString& url : m_pool.relays()) {
        QVariantMap item;
        item.insert(QStringLiteral("url"), url);
        item.insert(QStringLiteral("host"), hostOf(url));
        item.insert(QStringLiteral("state"), m_relayState.value(url, QStringLiteral("connecting")));
        item.insert(QStringLiteral("detail"), m_relayDetail.value(url));
        list.append(item);
    }
    return list;
}

QString AppController::relayText() const {
    return m_pool.relays().join(QLatin1Char('\n'));
}

void AppController::setFeedMode(int mode) {
    if (mode == m_feedMode)
        return;
    m_feedMode = mode == 1 ? 1 : 0;
    m_timeline.clear();
    subscribeFeed();
    persist();
    emit feedModeChanged();
}

void AppController::generateIdentity() {
    auto keys = Keys::generate();
    if (!keys) {
        setStatus(QStringLiteral("could not generate a key"));
        return;
    }
    m_displayName.clear();
    m_about.clear();
    m_follows.clear();
    m_contactsAt = -1;
    m_profiles.clear();
    adopt(std::move(*keys));
    setStatus(QStringLiteral("new identity created"));
}

void AppController::importSecret(const QString& text) {
    QString error;
    auto keys = Keys::fromBech32(text, &error);
    if (!keys) {
        setStatus(error.isEmpty() ? QStringLiteral("invalid secret") : error);
        return;
    }
    m_displayName.clear();
    m_about.clear();
    m_follows.clear();
    m_contactsAt = -1;
    m_profiles.clear();
    adopt(std::move(*keys));
    setStatus(QStringLiteral("identity imported"));
}

void AppController::publishNote(const QString& text) {
    const QString content = text.trimmed();
    if (!m_keys || content.isEmpty())
        return;
    NostrEvent event;
    event.kind = 1;
    event.content = content;
    event.createdAt = QDateTime::currentSecsSinceEpoch();
    event.tags.append({QStringLiteral("client"), QStringLiteral("nostarchy")});
    if (m_reply) {
        event.tags.append({QStringLiteral("e"), m_reply->id, QString(), QStringLiteral("root")});
        event.tags.append({QStringLiteral("e"), m_reply->id, QString(), QStringLiteral("reply")});
        event.tags.append({QStringLiteral("p"), m_reply->pubkey});
    }
    if (!publishEvent(event))
        return;
    cancelReply();
}

void AppController::publishProfile(const QString& name, const QString& about) {
    if (!m_keys)
        return;
    m_displayName = name.trimmed();
    m_about = about.trimmed();
    QJsonObject meta;
    meta.insert(QStringLiteral("name"), m_displayName);
    if (!m_about.isEmpty())
        meta.insert(QStringLiteral("about"), m_about);
    NostrEvent event;
    event.kind = 0;
    event.createdAt = QDateTime::currentSecsSinceEpoch();
    event.content = QString::fromUtf8(QJsonDocument(meta).toJson(QJsonDocument::Compact));
    if (!publishEvent(event))
        return;
    emit identityChanged();
    persist();
}

void AppController::beginReply(const QString& eventId, const QString& pubkey, const QString& name) {
    m_reply = Reply{eventId, pubkey, name};
    emit replyChanged();
}

void AppController::cancelReply() {
    if (!m_reply)
        return;
    m_reply.reset();
    emit replyChanged();
}

void AppController::toggleFollow(const QString& pubkey) {
    if (!m_keys || pubkey.isEmpty() || pubkey == m_keys->pubkeyHex())
        return;
    if (m_follows.contains(pubkey))
        m_follows.remove(pubkey);
    else
        m_follows.insert(pubkey);
    m_timeline.setFollows(m_follows);
    emit followsChanged();
    publishContacts();
    if (m_feedMode == 1)
        subscribeFeed();
}

void AppController::setRelaysFromText(const QString& text) {
    m_pool.setRelays(text.split(QLatin1Char('\n'), Qt::SkipEmptyParts));
    subscribeAll();
    persist();
    emit relaysChanged();
}

void AppController::copyNpub() {
    if (!m_keys)
        return;
    QGuiApplication::clipboard()->setText(m_keys->npub());
    setStatus(QStringLiteral("npub copied"));
}

void AppController::adopt(Keys keys) {
    m_keys = std::move(keys);
    m_seenAuthors.clear();
    m_pendingProfiles.clear();
    m_timeline.clear();
    cancelReply();
    persist();
    emit identityChanged();
    emit followsChanged();
    connectDefault();
    subscribeAll();
}

void AppController::persist() {
    QSettings settings;
    if (m_keys)
        settings.setValue(QStringLiteral("identity/secret"), m_keys->secretHex());
    else
        settings.remove(QStringLiteral("identity/secret"));
    settings.setValue(QStringLiteral("identity/name"), m_displayName);
    settings.setValue(QStringLiteral("identity/about"), m_about);
    settings.setValue(QStringLiteral("relays"), m_pool.relays());
    settings.setValue(QStringLiteral("feedMode"), m_feedMode);
}

void AppController::load() {
    QSettings settings;
    m_displayName = settings.value(QStringLiteral("identity/name")).toString();
    m_about = settings.value(QStringLiteral("identity/about")).toString();
    m_feedMode = settings.value(QStringLiteral("feedMode"), 0).toInt() == 1 ? 1 : 0;
    const QString secret = settings.value(QStringLiteral("identity/secret")).toString();
    if (!secret.isEmpty()) {
        if (auto keys = Keys::fromSecretHex(secret))
            m_keys = std::move(*keys);
    }
    QStringList relays = settings.value(QStringLiteral("relays")).toStringList();
    if (relays.isEmpty())
        relays = defaultRelays();
    if (m_keys) {
        m_pool.setRelays(relays);
        subscribeAll();
    } else {
        m_pool.setRelays(relays);
    }
}

void AppController::connectDefault() {
    if (m_pool.relays().isEmpty())
        m_pool.setRelays(defaultRelays());
}

void AppController::subscribeAll() {
    if (!m_keys)
        return;
    QJsonObject contacts;
    contacts.insert(QStringLiteral("kinds"), QJsonArray{3});
    contacts.insert(QStringLiteral("authors"), QJsonArray{m_keys->pubkeyHex()});
    contacts.insert(QStringLiteral("limit"), 1);
    m_pool.subscribe(QStringLiteral("contacts"), QJsonArray{contacts});
    subscribeFeed();
    noteAuthor(m_keys->pubkeyHex());
}

void AppController::subscribeFeed() {
    if (!m_keys)
        return;
    QJsonObject filter;
    filter.insert(QStringLiteral("kinds"), QJsonArray{1});
    filter.insert(QStringLiteral("limit"), m_feedMode == 1 ? 50 : 30);
    if (m_feedMode == 1) {
        QJsonArray authors;
        authors.append(m_keys->pubkeyHex());
        for (const QString& pubkey : m_follows)
            authors.append(pubkey);
        filter.insert(QStringLiteral("authors"), authors);
    }
    m_pool.subscribe(QStringLiteral("feed"), QJsonArray{filter});
}

void AppController::subscribeProfiles() {
    if (m_pendingProfiles.isEmpty())
        return;
    QJsonArray filters;
    QJsonArray authors;
    for (const QString& pubkey : m_pendingProfiles) {
        authors.append(pubkey);
        if (authors.size() == 50) {
            QJsonObject filter;
            filter.insert(QStringLiteral("kinds"), QJsonArray{0});
            filter.insert(QStringLiteral("authors"), authors);
            filters.append(filter);
            authors = QJsonArray();
        }
    }
    if (!authors.isEmpty()) {
        QJsonObject filter;
        filter.insert(QStringLiteral("kinds"), QJsonArray{0});
        filter.insert(QStringLiteral("authors"), authors);
        filters.append(filter);
    }
    m_pendingProfiles.clear();
    m_pool.subscribe(QStringLiteral("profiles"), filters);
}

void AppController::noteAuthor(const QString& pubkey) {
    if (pubkey.size() != 64 || m_seenAuthors.contains(pubkey))
        return;
    m_seenAuthors.insert(pubkey);
    m_pendingProfiles.insert(pubkey);
    m_profileTimer.start();
}

void AppController::handleEvent(const QString&, const QString&, const QJsonObject& object) {
    auto event = NostrEvent::fromJson(object);
    if (!event)
        return;
    QString reason;
    if (!event->verify(&reason)) {
        qInfo().noquote() << "dropped event" << event->id.left(8) << reason;
        return;
    }
    if (event->kind == 0) {
        handleProfile(*event);
        return;
    }
    if (event->kind == 3 && m_keys && event->pubkey == m_keys->pubkeyHex()) {
        handleContacts(*event);
        return;
    }
    if (event->kind != 1)
        return;
    if (m_feedMode == 1 && m_keys && event->pubkey != m_keys->pubkeyHex() && !m_follows.contains(event->pubkey))
        return;
    m_timeline.upsert(*event, nameFor(event->pubkey));
    noteAuthor(event->pubkey);
}

void AppController::handleProfile(const NostrEvent& event) {
    Profile& profile = m_profiles[event.pubkey];
    if (profile.createdAt > event.createdAt)
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(event.content.toUtf8());
    const QJsonObject meta = doc.object();
    profile.name = meta.value(QStringLiteral("name")).toString();
    profile.about = meta.value(QStringLiteral("about")).toString();
    profile.createdAt = event.createdAt;
    m_timeline.setName(event.pubkey, profile.name);
    if (m_keys && event.pubkey == m_keys->pubkeyHex()) {
        m_displayName = profile.name;
        m_about = profile.about;
        emit identityChanged();
        persist();
    }
}

void AppController::handleContacts(const NostrEvent& event) {
    if (event.createdAt < m_contactsAt)
        return;
    m_contactsAt = event.createdAt;
    QSet<QString> follows;
    for (const QStringList& tag : event.tags) {
        if (tag.size() >= 2 && tag.at(0) == QStringLiteral("p") && tag.at(1).size() == 64)
            follows.insert(tag.at(1).toLower());
    }
    m_follows = follows;
    m_timeline.setFollows(m_follows);
    emit followsChanged();
    for (const QString& pubkey : m_follows)
        noteAuthor(pubkey);
    if (m_feedMode == 1)
        subscribeFeed();
}

void AppController::publishContacts() {
    if (!m_keys)
        return;
    NostrEvent event;
    event.kind = 3;
    event.createdAt = QDateTime::currentSecsSinceEpoch();
    event.content = QString();
    for (const QString& pubkey : m_follows)
        event.tags.append({QStringLiteral("p"), pubkey, QString()});
    if (!event.sign(*m_keys)) {
        setStatus(QStringLiteral("could not sign contact list"));
        return;
    }
    m_contactsAt = event.createdAt;
    const int sent = m_pool.publish(event.toWire());
    setStatus(sent ? QStringLiteral("follow list sent to %1 relays").arg(sent)
                   : QStringLiteral("follow list not sent — no relay connected"));
}

bool AppController::publishEvent(NostrEvent event) {
    if (!m_keys || !event.sign(*m_keys)) {
        setStatus(QStringLiteral("could not sign event"));
        return false;
    }
    if (event.kind == 1)
        m_timeline.upsert(event, nameFor(event.pubkey));
    const int sent = m_pool.publish(event.toWire());
    setStatus(sent ? QStringLiteral("sent to %1 relays").arg(sent)
                   : QStringLiteral("not sent — no relay connected"));
    return true;
}

QString AppController::nameFor(const QString& pubkey) const {
    return m_profiles.value(pubkey).name;
}

void AppController::setStatus(const QString& text) {
    if (m_status == text)
        return;
    m_status = text;
    emit statusChanged();
}

QStringList AppController::defaultRelays() {
    return {
        QStringLiteral("wss://relay.damus.io"),
        QStringLiteral("wss://nos.lol"),
        QStringLiteral("wss://relay.nostr.band"),
        QStringLiteral("wss://relay.primal.net"),
    };
}
