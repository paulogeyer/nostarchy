#include "AppController.h"

#include "crypto/Bech32.h"
#include "util/Bytes.h"

#include <algorithm>
#include <limits>

#include <QDateTime>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QClipboard>
#include <QRegularExpression>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <QtLogging>

namespace {

QString hostOf(const QString& url) {
    const QUrl parsed(url);
    return parsed.host().isEmpty() ? url : parsed.host();
}

QString htmlEscape(QString text) {
    text.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    text.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    text.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    text.replace(QLatin1Char('"'), QStringLiteral("&quot;"));
    text.replace(QLatin1Char('\n'), QStringLiteral("<br>"));
    return text;
}

QString safePicture(QString url) {
    url = url.trimmed();
    if (url.size() >= 2 && url.front() == QLatin1Char('"') && url.back() == QLatin1Char('"'))
        url = url.mid(1, url.size() - 2).trimmed();
    const QUrl parsed(url);
    const QString scheme = parsed.scheme().toLower();
    if (!parsed.isValid() || parsed.host().isEmpty())
        return {};
    if (scheme != QStringLiteral("https") && scheme != QStringLiteral("http"))
        return {};
    return url;
}

std::optional<QByteArray> tlv(const QByteArray& data, quint8 type) {
    int i = 0;
    while (i + 2 <= data.size()) {
        const auto entryType = static_cast<quint8>(data.at(i));
        const auto length = static_cast<quint8>(data.at(i + 1));
        if (i + 2 + length > data.size())
            return std::nullopt;
        if (entryType == type)
            return data.mid(i + 2, length);
        i += 2 + length;
    }
    return std::nullopt;
}

bool repliesTo(const NostrEvent& event, const QString& id) {
    bool tagged = false;
    for (const QStringList& tag : event.tags) {
        if (tag.size() < 2 || tag.at(0) != QStringLiteral("e") || tag.at(1).toLower() != id)
            continue;
        const QString marker = tag.size() >= 4 ? tag.at(3) : QString();
        if (marker == QStringLiteral("mention"))
            continue;
        tagged = true;
    }
    return tagged;
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
    m_mentionTimer.setSingleShot(true);
    m_mentionTimer.setInterval(300);
    connect(&m_mentionTimer, &QTimer::timeout, this, [this] {
        m_timeline.refreshMentions();
        m_replies.refreshMentions();
        m_profileNotes.refreshMentions();
    });
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(400);
    connect(&m_saveTimer, &QTimer::timeout, this, &AppController::saveCache);
    m_clock.setInterval(30000);
    m_replies.setParent(this);
    m_profileNotes.setParent(this);
    const auto formatter = [this](const QString& content, const QVector<QStringList>& tags) {
        return renderNote(content, tags);
    };
    m_timeline.setFormatter(formatter);
    m_replies.setFormatter(formatter);
    m_profileNotes.setFormatter(formatter);
    const auto images = [this](const QString& url) { return m_images.lookup(url); };
    m_timeline.setImageLookup(images);
    m_replies.setImageLookup(images);
    m_profileNotes.setImageLookup(images);
    connect(&m_images, &ImageCache::ready, this, [this](const QString& url) {
        m_timeline.refreshPicture(url);
        m_replies.refreshPicture(url);
        m_profileNotes.refreshPicture(url);
        emit noteChanged();
        emit profileChanged();
        emit identityChanged();
    });
    connect(&m_clock, &QTimer::timeout, &m_timeline, &TimelineModel::refreshTimes);
    connect(&m_clock, &QTimer::timeout, &m_replies, &TimelineModel::refreshTimes);
    connect(&m_clock, &QTimer::timeout, &m_profileNotes, &TimelineModel::refreshTimes);
    m_clock.start();
    m_pool.addMetadataRelay(QStringLiteral("wss://purplepag.es/"));
    m_pool.setAuthSigner([this](const QString& relay, const QString& challenge) {
        if (!m_keys)
            return QByteArray();
        NostrEvent event;
        event.kind = 22242;
        event.createdAt = QDateTime::currentSecsSinceEpoch();
        event.tags = {{QStringLiteral("relay"), relay}, {QStringLiteral("challenge"), challenge}};
        if (!event.sign(*m_keys))
            return QByteArray();
        return event.toWire();
    });
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
bool AppController::viewing() const { return !m_openId.isEmpty(); }
QString AppController::openNoteId() const { return m_openId; }
QString AppController::openNotePubkey() const { return m_openPubkey; }
QString AppController::openNoteName() const { return m_openName.isEmpty() ? m_openPubkey.left(8) : m_openName; }
QString AppController::openNoteContent() const { return m_openContent; }
QString AppController::openNoteTime() const { return m_openTime; }
QString AppController::openNotePicture() const { return m_images.lookup(pictureFor(m_openPubkey)); }
QString AppController::openNoteRich() const { return renderNote(m_openContent, m_openTags); }
QString AppController::selfPicture() const { return m_keys ? m_images.lookup(pictureFor(m_keys->pubkeyHex())) : QString(); }
bool AppController::viewingProfile() const { return m_viewingProfile; }
QString AppController::profilePubkey() const { return m_profilePubkey; }
QString AppController::profileName() const {
    const QString name = m_profiles.value(m_profilePubkey).name;
    if (!name.isEmpty())
        return name;
    const QString npub = Keys::npubFromHex(m_profilePubkey);
    return npub.isEmpty() ? m_profilePubkey.left(8) : npub.left(12);
}
QString AppController::profileAbout() const { return renderNote(m_profiles.value(m_profilePubkey).about, {}); }
QString AppController::profilePicture() const { return m_images.lookup(pictureFor(m_profilePubkey)); }
QString AppController::profileNpub() const { return Keys::npubFromHex(m_profilePubkey); }
TimelineModel* AppController::replies() { return &m_replies; }
TimelineModel* AppController::profileNotes() { return &m_profileNotes; }

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
    refillTimeline();
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

bool AppController::isFollowed(const QString& pubkey) const {
    return m_follows.contains(pubkey);
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

void AppController::followNpub(const QString& text) {
    if (!m_keys) {
        setStatus(QStringLiteral("create an identity first"));
        return;
    }
    QString raw = text.trimmed();
    if (raw.startsWith(QStringLiteral("nostr:"), Qt::CaseInsensitive))
        raw = raw.mid(6);
    QString pubkey;
    if (raw.startsWith(QStringLiteral("npub1"), Qt::CaseInsensitive)) {
        const auto decoded = bech32::decode(raw);
        if (!decoded || decoded->first != QStringLiteral("npub") || decoded->second.size() != 32) {
            setStatus(QStringLiteral("invalid npub"));
            return;
        }
        pubkey = toHex(decoded->second);
    } else {
        const auto bytes = fromHex(raw);
        if (!bytes || bytes->size() != 32) {
            setStatus(QStringLiteral("paste an npub"));
            return;
        }
        pubkey = toHex(*bytes);
    }
    if (pubkey == m_keys->pubkeyHex()) {
        setStatus(QStringLiteral("that is your own key"));
        return;
    }
    const bool added = !m_follows.contains(pubkey);
    m_follows.insert(pubkey);
    m_timeline.setFollows(m_follows);
    noteAuthor(pubkey);
    emit followsChanged();
    if (added)
        publishContacts();
    else
        setStatus(QStringLiteral("already following"));
    if (m_feedMode != 1)
        setFeedMode(1);
    else
        subscribeFeed();
}

void AppController::openNote(const QString& eventId, const QString& pubkey, const QString& name, const QString& content, const QString& timeText) {
    const QString id = eventId.trimmed().toLower();
    if (id.size() != 64 || !m_keys)
        return;
    if (m_viewingProfile)
        closeProfile();
    m_openId = id;
    m_openPubkey = pubkey.toLower();
    m_openName = name;
    m_openContent = content;
    m_openTime = timeText;
    m_openTags = m_noteTags.value(id);
    m_replies.clear();
    emit noteChanged();
    noteAuthor(m_openPubkey);
    QJsonObject replies;
    replies.insert(QStringLiteral("kinds"), QJsonArray{1});
    replies.insert(QStringLiteral("#e"), QJsonArray{id});
    replies.insert(QStringLiteral("limit"), 100);
    QJsonObject self;
    self.insert(QStringLiteral("ids"), QJsonArray{id});
    m_pool.subscribe(QStringLiteral("thread"), QJsonArray{replies, self});
}

void AppController::closeNote() {
    if (m_openId.isEmpty())
        return;
    m_openId.clear();
    m_openPubkey.clear();
    m_openName.clear();
    m_openContent.clear();
    m_openTime.clear();
    m_openTags.clear();
    m_replies.clear();
    m_pool.unsubscribe(QStringLiteral("thread"));
    emit noteChanged();
}

void AppController::openProfile(const QString& pubkeyOrNpub) {
    const QString pubkey = personPubkey(pubkeyOrNpub);
    if (pubkey.size() != 64) {
        setStatus(QStringLiteral("invalid profile"));
        return;
    }
    m_profilePubkey = pubkey;
    m_viewingProfile = true;
    m_profileNotes.clear();
    for (const NostrEvent& cached : m_noteCache) {
        if (cached.pubkey == pubkey)
            m_profileNotes.upsert(cached, nameFor(pubkey), pictureFor(pubkey));
    }
    emit profileChanged();
    noteAuthor(pubkey);
    QJsonObject meta;
    meta.insert(QStringLiteral("kinds"), QJsonArray{0});
    meta.insert(QStringLiteral("authors"), QJsonArray{pubkey});
    meta.insert(QStringLiteral("limit"), 1);
    QJsonObject notes;
    notes.insert(QStringLiteral("kinds"), QJsonArray{1});
    notes.insert(QStringLiteral("authors"), QJsonArray{pubkey});
    notes.insert(QStringLiteral("limit"), 40);
    m_pool.subscribe(QStringLiteral("person"), QJsonArray{meta, notes});
}

void AppController::closeProfile() {
    if (!m_viewingProfile)
        return;
    m_viewingProfile = false;
    m_profileNotes.clear();
    m_pool.unsubscribe(QStringLiteral("person"));
    emit profileChanged();
}

void AppController::openLink(const QString& link) {
    QString raw = link.trimmed();
    if (raw.startsWith(QStringLiteral("nostr:"), Qt::CaseInsensitive))
        raw = raw.mid(6);
    if (raw.startsWith(QStringLiteral("p:"))) {
        openProfile(raw.mid(2));
        return;
    }
    if (raw.startsWith(QStringLiteral("e:"))) {
        openNote(raw.mid(2), QString(), QString(), QString(), QString());
        return;
    }
    const auto decoded = bech32::decode(raw);
    if (!decoded)
        return;
    if (decoded->first == QStringLiteral("npub") || decoded->first == QStringLiteral("nprofile")) {
        openProfile(raw);
        return;
    }
    if (decoded->first == QStringLiteral("note") && decoded->second.size() == 32) {
        openNote(toHex(decoded->second), QString(), QString(), QString(), QString());
        return;
    }
    if (decoded->first == QStringLiteral("nevent")) {
        const auto id = tlv(decoded->second, 0);
        if (id && id->size() == 32)
            openNote(toHex(*id), QString(), QString(), QString(), QString());
    }
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
    m_pendingProfiles.clear();
    m_timeline.clear();
    m_noteCache.clear();
    closeProfile();
    closeNote();
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
    settings.setValue(QStringLiteral("follows"), QStringList(m_follows.cbegin(), m_follows.cend()));
    settings.setValue(QStringLiteral("contactsAt"), m_contactsAt);
}

void AppController::load() {
    QSettings settings;
    m_displayName = settings.value(QStringLiteral("identity/name")).toString();
    m_about = settings.value(QStringLiteral("identity/about")).toString();
    m_feedMode = settings.value(QStringLiteral("feedMode"), 0).toInt() == 1 ? 1 : 0;
    const QStringList savedFollows = settings.value(QStringLiteral("follows")).toStringList();
    for (const QString& pubkey : savedFollows) {
        if (pubkey.size() == 64)
            m_follows.insert(pubkey.toLower());
    }
    m_contactsAt = settings.value(QStringLiteral("contactsAt"), -1).toLongLong();
    loadCache();
    if (m_feedMode == 1 && m_follows.isEmpty())
        m_feedMode = 0;
    const QString secret = settings.value(QStringLiteral("identity/secret")).toString();
    if (!secret.isEmpty()) {
        if (auto keys = Keys::fromSecretHex(secret))
            m_keys = std::move(*keys);
    }
    QStringList relays = settings.value(QStringLiteral("relays")).toStringList();
    if (relays.isEmpty())
        relays = defaultRelays();
    refillTimeline();
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
    filter.insert(QStringLiteral("limit"), m_feedMode == 1 && !m_follows.isEmpty() ? 50 : 30);
    if (m_feedMode == 1 && !m_follows.isEmpty()) {
        QJsonArray authors;
        authors.append(m_keys->pubkeyHex());
        for (const QString& pubkey : m_follows)
            authors.append(pubkey);
        filter.insert(QStringLiteral("authors"), authors);
        m_pool.subscribe(QStringLiteral("following"), QJsonArray{filter});
        subscribeRelayLists();
    } else {
        m_pool.unsubscribe(QStringLiteral("following"));
    }
    m_pool.subscribe(QStringLiteral("feed"), QJsonArray{filter});
}

void AppController::subscribeRelayLists() {
    if (m_follows.isEmpty())
        return;
    QJsonArray authors;
    for (const QString& pubkey : m_follows)
        authors.append(pubkey);
    QJsonObject filter;
    filter.insert(QStringLiteral("kinds"), QJsonArray{10002});
    filter.insert(QStringLiteral("authors"), authors);
    m_pool.subscribe(QStringLiteral("relaylists"), QJsonArray{filter});
}

void AppController::subscribeProfiles() {
    QJsonArray filters;
    QJsonArray authors;
    for (const QString& pubkey : m_pendingProfiles) {
        if (m_profiles.value(pubkey).createdAt > 0)
            continue;
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
    if (filters.isEmpty())
        return;
    m_pool.subscribe(QStringLiteral("profiles"), filters);
}

void AppController::noteAuthor(const QString& pubkey) {
    if (pubkey.size() != 64 || m_profiles.value(pubkey).createdAt > 0)
        return;
    const bool added = !m_pendingProfiles.contains(pubkey);
    m_pendingProfiles.insert(pubkey);
    if (added)
        m_profileTimer.start();
}

void AppController::handleEvent(const QString&, const QString& subId, const QJsonObject& object) {
    auto event = NostrEvent::fromJson(object);
    if (!event)
        return;
    if (!event->verify())
        return;
    if (event->kind == 0) {
        handleProfile(*event);
        return;
    }
    if (event->kind == 10002) {
        if (!m_follows.contains(event->pubkey))
            return;
        for (const QStringList& tag : event->tags) {
            if (tag.size() < 2 || tag.at(0) != QStringLiteral("r"))
                continue;
            if (tag.size() >= 3 && tag.at(2) == QStringLiteral("read"))
                continue;
            m_pool.addOutboxRelay(tag.at(1));
        }
        return;
    }
    if (event->kind == 3 && m_keys && event->pubkey == m_keys->pubkeyHex()) {
        handleContacts(*event);
        return;
    }
    if (event->kind != 1)
        return;
    rememberNote(*event);
    m_noteTags.insert(event->id, event->tags);
    indexPeople(*event);
    if (event->id == m_openId) {
        m_openContent = event->content;
        m_openPubkey = event->pubkey;
        m_openTags = event->tags;
        const QString known = nameFor(event->pubkey);
        if (!known.isEmpty())
            m_openName = known;
        if (m_openTime.isEmpty())
            m_openTime = QDateTime::fromSecsSinceEpoch(event->createdAt).toString(QStringLiteral("MMM d"));
        emit noteChanged();
    }
    const QString name = nameFor(event->pubkey);
    const QString picture = pictureFor(event->pubkey);
    if (!m_openId.isEmpty() && repliesTo(*event, m_openId))
        m_replies.upsert(*event, name, picture);
    if (subId == QStringLiteral("thread"))
        return;
    if (subId == QStringLiteral("person")) {
        if (event->pubkey == m_profilePubkey)
            m_profileNotes.upsert(*event, name, picture);
        return;
    }
    if (m_feedMode == 1 && !m_follows.isEmpty() && m_keys && event->pubkey != m_keys->pubkeyHex() && !m_follows.contains(event->pubkey))
        return;
    m_timeline.upsert(*event, name, picture);
}

void AppController::handleProfile(const NostrEvent& event) {
    Profile& profile = m_profiles[event.pubkey];
    if (profile.createdAt > event.createdAt)
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(event.content.toUtf8());
    const QJsonObject meta = doc.object();
    QString name = meta.value(QStringLiteral("name")).toString().trimmed();
    if (name.isEmpty())
        name = meta.value(QStringLiteral("display_name")).toString().trimmed();
    if (name.isEmpty())
        name = meta.value(QStringLiteral("displayName")).toString().trimmed();
    profile.name = name;
    profile.about = meta.value(QStringLiteral("about")).toString();
    profile.picture = safePicture(meta.value(QStringLiteral("picture")).toString());
    if (!profile.picture.isEmpty())
        m_images.prefetch(profile.picture);
    profile.createdAt = event.createdAt;
    m_pendingProfiles.remove(event.pubkey);
    m_timeline.setAuthor(event.pubkey, profile.name, profile.picture);
    m_replies.setAuthor(event.pubkey, profile.name, profile.picture);
    m_profileNotes.setAuthor(event.pubkey, profile.name, profile.picture);
    m_mentionTimer.start();
    if (event.pubkey == m_openPubkey && !profile.name.isEmpty())
        m_openName = profile.name;
    if (event.pubkey == m_openPubkey || event.pubkey == m_profilePubkey)
        emit noteChanged();
    if (event.pubkey == m_profilePubkey)
        emit profileChanged();
    if (m_keys && event.pubkey == m_keys->pubkeyHex()) {
        m_displayName = profile.name;
        m_about = profile.about;
        emit identityChanged();
        persist();
    }
    scheduleSave();
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
    if (m_feedMode == 1) {
        refillTimeline();
        subscribeFeed();
    }
    persist();
    scheduleSave();
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
    persist();
    scheduleSave();
}

bool AppController::publishEvent(NostrEvent event) {
    if (!m_keys || !event.sign(*m_keys)) {
        setStatus(QStringLiteral("could not sign event"));
        return false;
    }
    if (event.kind == 1) {
        rememberNote(event);
        m_noteTags.insert(event.id, event.tags);
        const QString name = nameFor(event.pubkey);
        const QString picture = pictureFor(event.pubkey);
        m_timeline.upsert(event, name, picture);
        if (!m_openId.isEmpty() && repliesTo(event, m_openId))
            m_replies.upsert(event, name, picture);
    }
    const int sent = m_pool.publish(event.toWire());
    setStatus(sent ? QStringLiteral("sent to %1 relays").arg(sent)
                   : QStringLiteral("not sent — no relay connected"));
    return true;
}

QString AppController::nameFor(const QString& pubkey) const {
    return m_profiles.value(pubkey).name;
}

QString AppController::pictureFor(const QString& pubkey) const {
    return m_profiles.value(pubkey).picture;
}

QString AppController::personPubkey(const QString& text) const {
    QString raw = text.trimmed();
    if (raw.startsWith(QStringLiteral("nostr:"), Qt::CaseInsensitive))
        raw = raw.mid(6);
    if (raw.startsWith(QStringLiteral("npub1"), Qt::CaseInsensitive) || raw.startsWith(QStringLiteral("nprofile1"), Qt::CaseInsensitive)) {
        const auto decoded = bech32::decode(raw);
        if (!decoded)
            return {};
        if (decoded->first == QStringLiteral("npub") && decoded->second.size() == 32)
            return toHex(decoded->second);
        if (decoded->first == QStringLiteral("nprofile")) {
            const auto pub = tlv(decoded->second, 0);
            if (pub && pub->size() == 32)
                return toHex(*pub);
        }
        return {};
    }
    const auto bytes = fromHex(raw);
    if (!bytes || bytes->size() != 32)
        return {};
    return toHex(*bytes);
}

QString AppController::renderNote(const QString& content, const QVector<QStringList>& tags) const {
    struct Hit {
        int start = 0;
        int end = 0;
        QString html;
    };
    QList<Hit> hits;
    const QRegularExpression entity(QStringLiteral("(?:nostr:)?((?:npub|nprofile|note|nevent)1[qpzry9x8gf2tvdw0s3jn54khce6mua7l]{10,})"));
    auto entities = entity.globalMatch(content);
    while (entities.hasNext()) {
        const auto match = entities.next();
        const QString token = match.captured(1);
        const auto decoded = bech32::decode(token);
        if (!decoded)
            continue;
        QString label;
        if (decoded->first == QStringLiteral("npub") || decoded->first == QStringLiteral("nprofile")) {
            const QString pubkey = personPubkey(token);
            if (pubkey.isEmpty())
                continue;
            label = nameFor(pubkey);
            if (label.isEmpty()) {
                const QString npub = decoded->first == QStringLiteral("npub") ? token : Keys::npubFromHex(pubkey);
                label = npub.left(8) + QStringLiteral("…") + npub.right(4);
            }
        } else if (decoded->first == QStringLiteral("note") || decoded->first == QStringLiteral("nevent")) {
            label = QStringLiteral("note");
        } else {
            continue;
        }
        hits.append({static_cast<int>(match.capturedStart()), static_cast<int>(match.capturedEnd()),
            QStringLiteral("<a href=\"nostr:%1\">%2</a>").arg(token, htmlEscape(label))});
    }
    const QRegularExpression tagRef(QStringLiteral("#\\[(\\d+)\\]"));
    auto refs = tagRef.globalMatch(content);
    while (refs.hasNext()) {
        const auto match = refs.next();
        bool ok = false;
        const int index = match.captured(1).toInt(&ok);
        if (!ok || index < 0 || index >= tags.size() || tags.at(index).size() < 2)
            continue;
        const QStringList& tag = tags.at(index);
        QString href;
        QString label;
        if (tag.at(0) == QStringLiteral("p") && tag.at(1).size() == 64) {
            const QString pubkey = tag.at(1).toLower();
            label = nameFor(pubkey);
            if (label.isEmpty()) {
                const QString npub = Keys::npubFromHex(pubkey);
                label = npub.left(8) + QStringLiteral("…") + npub.right(4);
            }
            href = QStringLiteral("p:") + pubkey;
        } else if (tag.at(0) == QStringLiteral("e") && tag.at(1).size() == 64) {
            label = QStringLiteral("note");
            href = QStringLiteral("e:") + tag.at(1).toLower();
        } else {
            continue;
        }
        hits.append({static_cast<int>(match.capturedStart()), static_cast<int>(match.capturedEnd()),
            QStringLiteral("<a href=\"%1\">%2</a>").arg(href, htmlEscape(label))});
    }
    std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.start < b.start; });
    QString out;
    int cursor = 0;
    for (const Hit& hit : hits) {
        if (hit.start < cursor)
            continue;
        out += htmlEscape(content.mid(cursor, hit.start - cursor));
        out += hit.html;
        cursor = hit.end;
    }
    out += htmlEscape(content.mid(cursor));
    return out;
}

void AppController::indexPeople(const NostrEvent& event) {
    noteAuthor(event.pubkey);
    for (const QStringList& tag : event.tags) {
        if (tag.size() >= 2 && tag.at(0) == QStringLiteral("p") && tag.at(1).size() == 64)
            noteAuthor(tag.at(1).toLower());
    }
    const QRegularExpression entity(QStringLiteral("(?:nostr:)?((?:npub|nprofile)1[qpzry9x8gf2tvdw0s3jn54khce6mua7l]{10,})"));
    auto matches = entity.globalMatch(event.content);
    while (matches.hasNext()) {
        const QString pubkey = personPubkey(matches.next().captured(1));
        if (!pubkey.isEmpty())
            noteAuthor(pubkey);
    }
}

void AppController::scheduleSave() {
    m_saveTimer.start();
}

void AppController::rememberNote(const NostrEvent& event) {
    m_noteCache.insert(event.id, event);
    if (m_noteCache.size() > 200) {
        QString oldestId;
        qint64 oldest = std::numeric_limits<qint64>::max();
        for (auto it = m_noteCache.cbegin(); it != m_noteCache.cend(); ++it) {
            if (it->createdAt < oldest) {
                oldest = it->createdAt;
                oldestId = it.key();
            }
        }
        m_noteCache.remove(oldestId);
    }
    scheduleSave();
}

void AppController::refillTimeline() {
    m_timeline.clear();
    QList<NostrEvent> notes = m_noteCache.values();
    std::sort(notes.begin(), notes.end(), [](const NostrEvent& a, const NostrEvent& b) {
        if (a.createdAt != b.createdAt)
            return a.createdAt > b.createdAt;
        return a.id < b.id;
    });
    const QString mine = pubkey();
    const bool filterFollows = m_feedMode == 1 && !m_follows.isEmpty();
    int shown = 0;
    for (const NostrEvent& event : notes) {
        if (filterFollows && event.pubkey != mine && !m_follows.contains(event.pubkey))
            continue;
        const QString picture = pictureFor(event.pubkey);
        if (!picture.isEmpty())
            m_images.prefetch(picture);
        m_timeline.upsert(event, nameFor(event.pubkey), picture);
        if (++shown >= 80)
            break;
    }
}

void AppController::loadCache() {
    const QString path = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/feed.json");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    const QJsonObject profiles = root.value(QStringLiteral("profiles")).toObject();
    for (auto it = profiles.begin(); it != profiles.end(); ++it) {
        if (it.key().size() != 64)
            continue;
        const QJsonObject item = it.value().toObject();
        Profile profile;
        profile.name = item.value(QStringLiteral("name")).toString();
        profile.about = item.value(QStringLiteral("about")).toString();
        profile.picture = item.value(QStringLiteral("picture")).toString();
        profile.createdAt = item.value(QStringLiteral("created_at")).toInteger();
        if (profile.createdAt > m_profiles.value(it.key()).createdAt)
            m_profiles.insert(it.key(), profile);
        if (!profile.picture.isEmpty())
            m_images.prefetch(profile.picture);
    }
    const qint64 contactsAt = root.value(QStringLiteral("contactsAt")).toInteger(-1);
    if (contactsAt > m_contactsAt) {
        m_contactsAt = contactsAt;
        m_follows.clear();
        for (const QJsonValue& item : root.value(QStringLiteral("follows")).toArray()) {
            const QString pubkey = item.toString().toLower();
            if (pubkey.size() == 64)
                m_follows.insert(pubkey);
        }
    }
    for (const QJsonValue& item : root.value(QStringLiteral("notes")).toArray()) {
        auto event = NostrEvent::fromJson(item.toObject());
        if (event && event->kind == 1 && event->id.size() == 64)
            m_noteCache.insert(event->id, *event);
    }
}

void AppController::saveCache() {
    QList<NostrEvent> notes = m_noteCache.values();
    std::sort(notes.begin(), notes.end(), [](const NostrEvent& a, const NostrEvent& b) {
        return a.createdAt > b.createdAt;
    });
    if (notes.size() > 120)
        notes.resize(120);
    QJsonArray noteArray;
    for (const NostrEvent& event : notes)
        noteArray.append(QJsonDocument::fromJson(event.toWire()).object());
    QJsonObject profiles;
    for (auto it = m_profiles.cbegin(); it != m_profiles.cend(); ++it) {
        if (it->createdAt <= 0)
            continue;
        QJsonObject item;
        item.insert(QStringLiteral("name"), it->name);
        item.insert(QStringLiteral("about"), it->about);
        item.insert(QStringLiteral("picture"), it->picture);
        item.insert(QStringLiteral("created_at"), it->createdAt);
        profiles.insert(it.key(), item);
    }
    QJsonArray follows;
    for (const QString& pubkey : m_follows)
        follows.append(pubkey);
    QJsonObject root;
    root.insert(QStringLiteral("profiles"), profiles);
    root.insert(QStringLiteral("notes"), noteArray);
    root.insert(QStringLiteral("follows"), follows);
    root.insert(QStringLiteral("contactsAt"), m_contactsAt);
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(dir);
    QSaveFile file(dir + QStringLiteral("/feed.json"));
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    file.commit();
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
