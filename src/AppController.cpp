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
#include <QCryptographicHash>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>
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

bool newerReplaceable(qint64 createdAt, const QString& id, qint64 oldAt, const QString& oldId) {
    if (oldAt < 0)
        return true;
    if (createdAt != oldAt)
        return createdAt > oldAt;
    return id < oldId;
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
    connect(&m_pool, &RelayPool::eoseReceived, this, [this](const QString&, const QString& subId) {
        if (subId != QStringLiteral("profiles"))
            return;
        m_profilesInFlight = false;
        if (!m_pendingProfiles.isEmpty() && !m_profileTimer.isActive())
            m_profileTimer.start();
    });
    connect(&m_pool, &RelayPool::okReceived, this, [this](const QString&, const QString& eventId, bool accepted, const QString&) {
        if (!accepted || eventId != m_pendingContactId)
            return;
        m_contactsAt = m_pendingContactAt;
        m_contactsId = m_pendingContactId;
        persist();
        scheduleSave();
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
        m_inbox.refreshMentions();
        m_hashtagNotes.refreshMentions();
        m_profileNotes.refreshMentions();
    });
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(400);
    connect(&m_saveTimer, &QTimer::timeout, this, &AppController::saveCache);
    m_clock.setInterval(30000);
    m_replies.setParent(this);
    m_inbox.setParent(this);
    m_hashtagNotes.setParent(this);
    m_profileNotes.setParent(this);
    const auto formatter = [this](const QString& content, const QVector<QStringList>& tags) {
        return renderNote(content, tags);
    };
    m_timeline.setFormatter(formatter);
    m_replies.setFormatter(formatter);
    m_inbox.setFormatter(formatter);
    m_hashtagNotes.setFormatter(formatter);
    m_profileNotes.setFormatter(formatter);
    const auto images = [this](const QString& url) { return m_images.lookup(url); };
    m_timeline.setImageLookup(images);
    m_replies.setImageLookup(images);
    m_inbox.setImageLookup(images);
    m_hashtagNotes.setImageLookup(images);
    m_profileNotes.setImageLookup(images);
    const auto pictures = [this](const QString& pubkey) { return pictureFor(pubkey); };
    m_timeline.setPictureLookup(pictures);
    m_replies.setPictureLookup(pictures);
    m_inbox.setPictureLookup(pictures);
    m_hashtagNotes.setPictureLookup(pictures);
    m_profileNotes.setPictureLookup(pictures);
    m_people.setParent(this);
    m_people.setNameLookup([this](const QString& pubkey) { return nameFor(pubkey); });
    m_people.setImageLookup(images);
    connect(&m_images, &ImageCache::ready, this, [this](const QString& url) {
        m_timeline.refreshPicture(url);
        m_replies.refreshPicture(url);
        m_inbox.refreshPicture(url);
        m_hashtagNotes.refreshPicture(url);
        m_profileNotes.refreshPicture(url);
        if (m_keys && pictureFor(m_keys->pubkeyHex()) == url)
            emit identityChanged();
        if (pictureFor(m_openPubkey) == url)
            emit noteChanged();
        if (pictureFor(m_profilePubkey) == url)
            emit profileChanged();
        ++m_avatarEpoch;
        emit avatarEpochChanged();
    });
    connect(&m_clock, &QTimer::timeout, &m_timeline, &TimelineModel::refreshTimes);
    connect(&m_clock, &QTimer::timeout, &m_replies, &TimelineModel::refreshTimes);
    connect(&m_clock, &QTimer::timeout, &m_inbox, &TimelineModel::refreshTimes);
    connect(&m_clock, &QTimer::timeout, &m_hashtagNotes, &TimelineModel::refreshTimes);
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

int AppController::avatarEpoch() const { return m_avatarEpoch; }

TimelineModel* AppController::inbox() { return &m_inbox; }
TimelineModel* AppController::hashtagNotes() { return &m_hashtagNotes; }
bool AppController::viewingHashtag() const { return m_viewingHashtag; }
QString AppController::hashtag() const { return m_hashtag; }
bool AppController::quoting() const { return m_quote.has_value(); }
QString AppController::quoteLabel() const { return m_quote ? m_quote->name : QString(); }
QString AppController::profileNip05() const { return m_profiles.value(m_profilePubkey).nip05; }
bool AppController::profileNip05Ok() const { return m_profiles.value(m_profilePubkey).nip05Ok; }
int AppController::reactionEpoch() const { return m_reactionEpoch; }
int AppController::reactionCount(const QString& eventId) const { return m_reactionCounts.value(eventId); }
bool AppController::isMuted(const QString& pubkey) const { return m_muted.contains(pubkey.toLower()); }

QString AppController::avatarUrl(const QString& pubkey) {
    const QString remote = pictureFor(pubkey);
    const QString local = m_images.lookup(remote);
    if (local.isEmpty())
        m_images.prefetch(remote);
    return local;
}
QString AppController::profileNpub() const { return Keys::npubFromHex(m_profilePubkey); }
int AppController::profileFollowingCount() const { return m_profileFollowing.size(); }
int AppController::profileFollowerCount() const { return m_profileFollowers.size(); }
bool AppController::viewingPeople() const { return m_viewingPeople; }
QString AppController::peopleTitle() const { return m_peopleMode == 1 ? QStringLiteral("Followers") : QStringLiteral("Following"); }
PeopleModel* AppController::people() { return &m_people; }
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
    m_feedMode = mode == 1 ? 1 : mode == 2 ? 2 : 0;
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
    if (m_quote) {
        event.tags.append({QStringLiteral("q"), m_quote->id, QString(), m_quote->pubkey});
        event.tags.append({QStringLiteral("e"), m_quote->id, QString(), QStringLiteral("mention")});
        event.tags.append({QStringLiteral("p"), m_quote->pubkey});
    }
    if (!publishEvent(event))
        return;
    cancelReply();
    cancelQuote();
}

void AppController::deleteNote(const QString& eventId) {
    if (!m_keys || eventId.size() != 64)
        return;
    QString author;
    if (m_noteCache.contains(eventId))
        author = m_noteCache.value(eventId).pubkey;
    else if (m_openId == eventId)
        author = m_openPubkey;
    if (author != m_keys->pubkeyHex()) {
        setStatus(QStringLiteral("can only delete your own notes"));
        return;
    }
    NostrEvent event;
    event.kind = 5;
    event.createdAt = QDateTime::currentSecsSinceEpoch();
    event.tags.append({QStringLiteral("e"), eventId});
    if (!publishEvent(event))
        return;
    forgetNote(eventId);
    setStatus(QStringLiteral("deletion requested"));
}

void AppController::react(const QString& eventId, const QString& pubkey) {
    if (!m_keys || eventId.size() != 64 || m_reacted.contains(eventId))
        return;
    NostrEvent event;
    event.kind = 7;
    event.createdAt = QDateTime::currentSecsSinceEpoch();
    event.content = QStringLiteral("+");
    event.tags.append({QStringLiteral("e"), eventId});
    if (pubkey.size() == 64)
        event.tags.append({QStringLiteral("p"), pubkey.toLower()});
    if (!publishEvent(event))
        return;
    m_reacted.insert(eventId);
    ++m_reactionCounts[eventId];
    ++m_reactionEpoch;
    emit reactionEpochChanged();
}

void AppController::repost(const QString& eventId, const QString& pubkey) {
    if (!m_keys || eventId.size() != 64)
        return;
    NostrEvent event;
    event.kind = 6;
    event.createdAt = QDateTime::currentSecsSinceEpoch();
    event.tags.append({QStringLiteral("e"), eventId});
    if (pubkey.size() == 64)
        event.tags.append({QStringLiteral("p"), pubkey.toLower()});
    if (m_noteCache.contains(eventId))
        event.content = QString::fromUtf8(m_noteCache.value(eventId).toWire());
    if (!publishEvent(event))
        return;
    setStatus(QStringLiteral("reposted"));
}

void AppController::beginQuote(const QString& eventId, const QString& pubkey, const QString& name) {
    m_quote = Reply{eventId, pubkey.toLower(), name};
    emit quoteChanged();
}

void AppController::cancelQuote() {
    if (!m_quote)
        return;
    m_quote.reset();
    emit quoteChanged();
}

void AppController::copyNote(const QString& eventId) {
    const auto raw = fromHex(eventId);
    if (!raw || raw->size() != 32)
        return;
    const auto text = bech32::encode(QStringLiteral("note"), *raw);
    if (!text)
        return;
    QGuiApplication::clipboard()->setText(*text);
    setStatus(QStringLiteral("note id copied"));
}

void AppController::mutePubkey(const QString& pubkey) {
    const QString hex = pubkey.toLower();
    if (hex.size() != 64 || m_muted.contains(hex))
        return;
    m_muted.insert(hex);
    m_timeline.removeAuthor(hex);
    m_inbox.removeAuthor(hex);
    m_hashtagNotes.removeAuthor(hex);
    publishMuteList();
    persist();
    setStatus(QStringLiteral("muted"));
}

void AppController::unmutePubkey(const QString& pubkey) {
    if (!m_muted.remove(pubkey.toLower()))
        return;
    publishMuteList();
    persist();
    refillTimeline();
    setStatus(QStringLiteral("unmuted"));
}

void AppController::openHashtag(const QString& tag) {
    QString clean = tag.trimmed().toLower();
    if (clean.startsWith(QLatin1Char('#')))
        clean = clean.mid(1);
    if (clean.isEmpty())
        return;
    m_hashtag = clean;
    m_viewingHashtag = true;
    m_hashtagNotes.clear();
    emit hashtagChanged();
    QJsonObject filter;
    filter.insert(QStringLiteral("kinds"), QJsonArray{1});
    filter.insert(QStringLiteral("#t"), QJsonArray{clean});
    filter.insert(QStringLiteral("limit"), 40);
    m_pool.subscribe(QStringLiteral("hashtag"), QJsonArray{filter});
}

void AppController::closeHashtag() {
    if (!m_viewingHashtag)
        return;
    m_viewingHashtag = false;
    m_hashtag.clear();
    m_hashtagNotes.clear();
    m_pool.unsubscribe(QStringLiteral("hashtag"));
    emit hashtagChanged();
}

void AppController::uploadImage(const QString& path) {
    if (!m_keys)
        return;
    const QString local = QUrl(path).isLocalFile() ? QUrl(path).toLocalFile() : path;
    QFile file(local);
    if (!file.open(QIODevice::ReadOnly)) {
        setStatus(QStringLiteral("could not read image"));
        return;
    }
    const QByteArray bytes = file.readAll();
    if (bytes.size() < 32 || bytes.size() > 5 * 1024 * 1024) {
        setStatus(QStringLiteral("image must be under 5 MB"));
        return;
    }
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    NostrEvent auth;
    auth.kind = 24242;
    auth.createdAt = QDateTime::currentSecsSinceEpoch();
    auth.tags.append({QStringLiteral("t"), QStringLiteral("upload")});
    auth.tags.append({QStringLiteral("x"), hash});
    auth.tags.append({QStringLiteral("expiration"), QString::number(auth.createdAt + 60)});
    if (!auth.sign(*m_keys)) {
        setStatus(QStringLiteral("could not sign upload"));
        return;
    }
    QNetworkRequest request{QUrl(QStringLiteral("https://blossom.primal.net/upload"))};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QMimeDatabase().mimeTypeForFile(local).name());
    request.setRawHeader("Authorization", "Nostr " + auth.toWire().toBase64());
    QNetworkReply* reply = m_net.put(request, bytes);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const QByteArray body = reply->isOpen() ? reply->readAll() : QByteArray();
        const bool ok = reply->error() == QNetworkReply::NoError;
        reply->deleteLater();
        const QString url = QJsonDocument::fromJson(body).object().value(QStringLiteral("url")).toString();
        if (!ok || url.isEmpty()) {
            setStatus(QStringLiteral("image upload failed"));
            return;
        }
        emit imageUploaded(url);
        setStatus(QStringLiteral("image added"));
    });
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
    const QString picture = m_profiles.value(m_keys->pubkeyHex()).picture;
    if (!picture.isEmpty())
        meta.insert(QStringLiteral("picture"), picture);
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
    if (raw.contains(QLatin1Char('@')) && !raw.contains(QLatin1Char(' '))) {
        resolveNip05(raw, true);
        return;
    }
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
    if (m_noteCache.contains(id)) {
        const NostrEvent& cached = m_noteCache.value(id);
        if (m_openContent.isEmpty())
            m_openContent = cached.content;
        if (m_openPubkey.isEmpty())
            m_openPubkey = cached.pubkey;
        if (m_openName.isEmpty())
            m_openName = nameFor(cached.pubkey);
        m_openTags = cached.tags;
    } else {
        m_openTags = m_noteTags.value(id);
    }
    for (const QStringList& tag : m_openTags) {
        if (tag.size() >= 2 && tag.at(0) == QStringLiteral("q"))
            requestQuote(tag.at(1).toLower());
    }
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
    QJsonObject reactions;
    reactions.insert(QStringLiteral("kinds"), QJsonArray{7});
    reactions.insert(QStringLiteral("#e"), QJsonArray{eventId});
    reactions.insert(QStringLiteral("limit"), 80);
    m_pool.subscribe(QStringLiteral("reactions"), QJsonArray{reactions});
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
    m_pool.unsubscribe(QStringLiteral("reactions"));
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
    m_viewingPeople = false;
    m_peopleMode = 0;
    emit peopleChanged();
    applySocial(pubkey);
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
    QJsonObject following;
    following.insert(QStringLiteral("kinds"), QJsonArray{3});
    following.insert(QStringLiteral("authors"), QJsonArray{pubkey});
    following.insert(QStringLiteral("limit"), 1);
    QJsonObject followers;
    followers.insert(QStringLiteral("kinds"), QJsonArray{3});
    followers.insert(QStringLiteral("#p"), QJsonArray{pubkey});
    followers.insert(QStringLiteral("limit"), 200);
    m_pool.subscribe(QStringLiteral("graph"), QJsonArray{following, followers});
}

void AppController::closeProfile() {
    if (!m_viewingProfile)
        return;
    m_viewingProfile = false;
    m_viewingPeople = false;
    m_profileNotes.clear();
    m_pool.unsubscribe(QStringLiteral("person"));
    m_pool.unsubscribe(QStringLiteral("graph"));
    emit profileChanged();
    emit peopleChanged();
}

void AppController::showFollowing() {
    m_peopleMode = 0;
    m_viewingPeople = true;
    m_people.setPubkeys(m_profileFollowing);
    for (const QString& pubkey : m_profileFollowing)
        noteAuthor(pubkey);
    emit peopleChanged();
}

void AppController::showFollowers() {
    m_peopleMode = 1;
    m_viewingPeople = true;
    m_people.setPubkeys(m_profileFollowers);
    for (const QString& pubkey : m_profileFollowers)
        noteAuthor(pubkey);
    emit peopleChanged();
}

void AppController::closePeople() {
    if (!m_viewingPeople)
        return;
    m_viewingPeople = false;
    emit peopleChanged();
}

void AppController::applySocial(const QString& pubkey) {
    const Social social = m_social.value(pubkey);
    m_profileFollowing = social.following;
    m_profileFollowers = social.followers;
    emit profileChanged();
}

void AppController::rememberSocial(const NostrEvent& event) {
    QStringList following;
    for (const QStringList& tag : event.tags) {
        if (tag.size() >= 2 && tag.at(0) == QStringLiteral("p") && tag.at(1).size() == 64)
            following.append(tag.at(1).toLower());
    }
    Social& own = m_social[event.pubkey];
    if (event.createdAt >= own.followingAt) {
        own.following = following;
        own.followingAt = event.createdAt;
    }
    if (!m_profilePubkey.isEmpty() && event.pubkey != m_profilePubkey && following.contains(m_profilePubkey)) {
        Social& viewed = m_social[m_profilePubkey];
        if (!viewed.followers.contains(event.pubkey) && viewed.followers.size() < 300) {
            viewed.followers.append(event.pubkey);
            viewed.followersAt = event.createdAt;
        }
    }
    if (event.pubkey == m_profilePubkey)
        applySocial(event.pubkey);
    else if (!m_profilePubkey.isEmpty())
        applySocial(m_profilePubkey);
    if (m_viewingPeople)
        m_people.setPubkeys(m_peopleMode == 1 ? m_profileFollowers : m_profileFollowing);
    scheduleSave();
}

void AppController::openLink(const QString& link) {
    QString raw = link.trimmed();
    if (raw.startsWith(QStringLiteral("nostr:"), Qt::CaseInsensitive))
        raw = raw.mid(6);
    if (raw.startsWith(QStringLiteral("tag:"))) {
        openHashtag(raw.mid(4));
        return;
    }
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
    m_writeRelays.clear();
    m_pool.clearOutbox();
    const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QFile::remove(cacheDir + QStringLiteral("/feed.json"));
    QFile::remove(cacheDir + QStringLiteral("/following.json"));
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
    settings.setValue(QStringLiteral("muted"), QStringList(m_muted.cbegin(), m_muted.cend()));
    settings.setValue(QStringLiteral("follows"), QStringList(m_follows.cbegin(), m_follows.cend()));
    settings.setValue(QStringLiteral("contactsAt"), m_contactsAt);
}

void AppController::load() {
    QSettings settings;
    m_displayName = settings.value(QStringLiteral("identity/name")).toString();
    m_about = settings.value(QStringLiteral("identity/about")).toString();
    const int savedMode = settings.value(QStringLiteral("feedMode"), 0).toInt();
    m_feedMode = savedMode == 1 ? 1 : savedMode == 2 ? 2 : 0;
    for (const QString& pubkey : settings.value(QStringLiteral("muted")).toStringList()) {
        if (pubkey.size() == 64)
            m_muted.insert(pubkey.toLower());
    }
    const QStringList savedFollows = settings.value(QStringLiteral("follows")).toStringList();
    for (const QString& pubkey : savedFollows) {
        if (pubkey.size() == 64)
            m_follows.insert(pubkey.toLower());
    }
    m_contactsAt = settings.value(QStringLiteral("contactsAt"), -1).toLongLong();
    const QString secret = settings.value(QStringLiteral("identity/secret")).toString();
    if (!secret.isEmpty()) {
        if (auto keys = Keys::fromSecretHex(secret))
            m_keys = std::move(*keys);
    }
    const bool fastFollows = m_feedMode == 1 && loadFollowingCache();
    if (!fastFollows)
        loadCache();
    if (m_feedMode == 1 && m_follows.isEmpty())
        m_feedMode = 0;
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
    if (fastFollows)
        QTimer::singleShot(0, this, [this] {
            loadCache();
            refillTimeline();
        });
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
    QJsonObject mutes;
    mutes.insert(QStringLiteral("kinds"), QJsonArray{10000});
    mutes.insert(QStringLiteral("authors"), QJsonArray{m_keys->pubkeyHex()});
    mutes.insert(QStringLiteral("limit"), 1);
    m_pool.subscribe(QStringLiteral("mutes"), QJsonArray{mutes});
    QJsonObject mentions;
    mentions.insert(QStringLiteral("kinds"), QJsonArray{1});
    mentions.insert(QStringLiteral("#p"), QJsonArray{m_keys->pubkeyHex()});
    mentions.insert(QStringLiteral("limit"), 40);
    m_pool.subscribe(QStringLiteral("mentions"), QJsonArray{mentions});
    subscribeFeed();
    noteAuthor(m_keys->pubkeyHex());
}

void AppController::subscribeFeed() {
    if (!m_keys)
        return;
    QJsonObject filter;
    filter.insert(QStringLiteral("kinds"), QJsonArray{1});
    filter.insert(QStringLiteral("limit"), m_feedMode == 1 && !m_follows.isEmpty() ? 50 : 30);
    if (m_feedMode == 2) {
        m_pool.unsubscribe(QStringLiteral("feed"));
        m_pool.unsubscribe(QStringLiteral("following"));
        QJsonObject mentions;
        mentions.insert(QStringLiteral("kinds"), QJsonArray{1});
        mentions.insert(QStringLiteral("#p"), QJsonArray{m_keys->pubkeyHex()});
        mentions.insert(QStringLiteral("limit"), 40);
        m_pool.subscribe(QStringLiteral("mentions"), QJsonArray{mentions});
        return;
    }
    if (m_feedMode == 1 && m_follows.isEmpty()) {
        m_pool.unsubscribe(QStringLiteral("feed"));
        m_pool.unsubscribe(QStringLiteral("following"));
        return;
    }
    if (m_feedMode == 1) {
        QJsonArray filters;
        QStringList people(m_follows.cbegin(), m_follows.cend());
        people.prepend(m_keys->pubkeyHex());
        for (const QString& pubkey : people) {
            QJsonObject one;
            one.insert(QStringLiteral("kinds"), QJsonArray{1});
            one.insert(QStringLiteral("authors"), QJsonArray{pubkey});
            one.insert(QStringLiteral("limit"), 20);
            filters.append(one);
        }
        m_pool.subscribe(QStringLiteral("following"), filters);
        m_pool.subscribe(QStringLiteral("feed"), filters);
        subscribeRelayLists();
        return;
    }
    m_pool.unsubscribe(QStringLiteral("following"));
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
    if (filters.isEmpty() || m_profilesInFlight)
        return;
    for (const QJsonValue& filter : filters) {
        const QJsonArray authors = filter.toObject().value(QStringLiteral("authors")).toArray();
        for (const QJsonValue& author : authors) {
            const QString pubkey = author.toString();
            m_pendingProfiles.remove(pubkey);
            m_profileAsked.insert(pubkey);
        }
    }
    m_profilesInFlight = true;
    m_pool.subscribe(QStringLiteral("profiles"), filters);
}

void AppController::noteAuthor(const QString& pubkey) {
    if (pubkey.size() != 64 || m_profiles.value(pubkey).createdAt > 0 || m_profileAsked.contains(pubkey))
        return;
    if (m_pendingProfiles.size() >= 200 && !m_pendingProfiles.contains(pubkey))
        return;
    m_pendingProfiles.insert(pubkey);
    if (m_profilesInFlight || m_profileTimer.isActive())
        return;
    m_profileTimer.start();
}

void AppController::handleEvent(const QString&, const QString& subId, const QJsonObject& object) {
    auto event = NostrEvent::fromJson(object);
    if (!event)
        return;
    if (m_deletedIds.contains(event->id))
        return;
    if (m_seenEventIds.contains(event->id))
        return;
    if (m_seenEventIds.size() > 4000)
        m_seenEventIds.clear();
    m_seenEventIds.insert(event->id);
    if (!event->verify())
        return;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (event->kind == 1 && (event->createdAt > now + 120 || event->createdAt < now - 60LL * 60 * 24 * 365 * 5))
        return;
    if (event->kind == 0) {
        handleProfile(*event);
        return;
    }
    if (event->kind == 10002) {
        if (!m_follows.contains(event->pubkey))
            return;
        if (!newerReplaceable(event->createdAt, event->id, m_relayListAt.value(event->pubkey, -1), m_relayListId.value(event->pubkey)))
            return;
        QStringList writes;
        for (const QStringList& tag : event->tags) {
            if (tag.size() < 2 || tag.at(0) != QStringLiteral("r"))
                continue;
            if (tag.size() >= 3 && tag.at(2) == QStringLiteral("read"))
                continue;
            writes.append(tag.at(1));
        }
        m_relayListAt.insert(event->pubkey, event->createdAt);
        m_relayListId.insert(event->pubkey, event->id);
        m_writeRelays.insert(event->pubkey, writes);
        rebuildOutbox();
        return;
    }
    if (event->kind == 7) {
        for (const QStringList& tag : event->tags) {
            if (tag.size() < 2 || tag.at(0) != QStringLiteral("e") || tag.at(1).size() != 64)
                continue;
            const QString id = tag.at(1).toLower();
            ++m_reactionCounts[id];
            if (m_keys && event->pubkey == m_keys->pubkeyHex())
                m_reacted.insert(id);
        }
        ++m_reactionEpoch;
        emit reactionEpochChanged();
        return;
    }
    if (event->kind == 6) {
        NostrEvent shown = *event;
        auto inner = NostrEvent::fromJson(QJsonDocument::fromJson(event->content.toUtf8()).object());
        if (inner && !inner->content.isEmpty())
            shown.content = inner->content;
        else
            shown.content = QStringLiteral("Reposted a note");
        showNote(shown);
        return;
    }
    if (event->kind == 10000) {
        if (!m_keys || event->pubkey != m_keys->pubkeyHex())
            return;
        if (!newerReplaceable(event->createdAt, event->id, m_muteAt, m_muteId))
            return;
        m_muted.clear();
        m_mutedWords.clear();
        for (const QStringList& tag : event->tags) {
            if (tag.size() < 2)
                continue;
            if (tag.at(0) == QStringLiteral("p") && tag.at(1).size() == 64)
                m_muted.insert(tag.at(1).toLower());
            else if (tag.at(0) == QStringLiteral("word"))
                m_mutedWords.append(tag.at(1).toLower());
        }
        m_muteAt = event->createdAt;
        m_muteId = event->id;
        refillTimeline();
        persist();
        return;
    }
    if (event->kind == 5) {
        for (const QStringList& tag : event->tags) {
            if (tag.size() < 2 || tag.at(0) != QStringLiteral("e") || tag.at(1).size() != 64)
                continue;
            const QString id = tag.at(1).toLower();
            const QString author = m_noteCache.contains(id) ? m_noteCache.value(id).pubkey : m_deletionAuthors.value(id);
            if (!author.isEmpty() && author != event->pubkey)
                continue;
            if (author.isEmpty())
                m_deletionAuthors.insert(id, event->pubkey);
            else
                forgetNote(id);
        }
        scheduleSave();
        return;
    }
    if (event->kind == 3) {
        if (m_keys && event->pubkey == m_keys->pubkeyHex())
            handleContacts(*event);
        rememberSocial(*event);
        return;
    }
    if (event->kind != 1)
        return;
    if (m_deletionAuthors.value(event->id) == event->pubkey) {
        forgetNote(event->id);
        return;
    }
    rememberNote(*event);
    m_noteTags.insert(event->id, event->tags);
    if (m_quoteAsked.remove(event->id)) {
        m_pool.unsubscribe(QStringLiteral("quote-") + event->id.left(12));
        if (!m_mentionTimer.isActive())
            m_mentionTimer.start();
        for (const QStringList& tag : m_openTags) {
            if (tag.size() >= 2 && tag.at(0) == QStringLiteral("q") && tag.at(1).toLower() == event->id)
                emit noteChanged();
        }
    }
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
    showNote(*event);
}

void AppController::handleProfile(const NostrEvent& event) {
    Profile& profile = m_profiles[event.pubkey];
    if (profile.createdAt > 0 && !newerReplaceable(event.createdAt, event.id, profile.createdAt, profile.id))
        return;
    if (m_profiles.size() > 400 && !m_profiles.contains(event.pubkey))
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
    profile.nip05 = meta.value(QStringLiteral("nip05")).toString().trimmed();
    profile.nip05Ok = false;
    if (!profile.nip05.isEmpty())
        verifyNip05(event.pubkey, profile.nip05);
    if (!profile.picture.isEmpty())
        m_images.prefetch(profile.picture);
    profile.id = event.id;
    profile.createdAt = event.createdAt;
    m_pendingProfiles.remove(event.pubkey);
    m_timeline.setAuthor(event.pubkey, profile.name, profile.picture);
    m_replies.setAuthor(event.pubkey, profile.name, profile.picture);
    m_profileNotes.setAuthor(event.pubkey, profile.name, profile.picture);
    m_people.setAuthor(event.pubkey, profile.name, profile.picture);
    ++m_avatarEpoch;
    emit avatarEpochChanged();
    if (!m_mentionTimer.isActive())
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
    if (m_pendingContactAt >= 0 && event.createdAt < m_pendingContactAt)
        return;
    if (!newerReplaceable(event.createdAt, event.id, m_contactsAt, m_contactsId))
        return;
    QSet<QString> follows;
    for (const QStringList& tag : event.tags) {
        if (tag.size() >= 2 && tag.at(0) == QStringLiteral("p") && tag.at(1).size() == 64)
            follows.insert(tag.at(1).toLower());
    }
    m_contactsAt = event.createdAt;
    m_contactsId = event.id;
    m_lastContactTags = event.tags;
    if (follows == m_follows)
        return;
    m_follows = follows;
    m_writeRelays.clear();
    rebuildOutbox();
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
    QHash<QString, QStringList> previous;
    for (const QStringList& tag : m_lastContactTags) {
        if (tag.size() >= 2 && tag.at(0) == QStringLiteral("p"))
            previous.insert(tag.at(1).toLower(), tag);
    }
    for (const QString& pubkey : m_follows) {
        if (previous.contains(pubkey))
            event.tags.append(previous.value(pubkey));
        else
            event.tags.append({QStringLiteral("p"), pubkey, QString()});
    }
    if (!event.sign(*m_keys)) {
        setStatus(QStringLiteral("could not sign contact list"));
        return;
    }
    m_pendingContactAt = event.createdAt;
    m_pendingContactId = event.id;
    const int sent = m_pool.publish(event.toWire());
    setStatus(sent ? QStringLiteral("follow list sent to %1 relays").arg(sent)
                   : QStringLiteral("follow list not sent — no relay connected"));
    persist();
    scheduleSave();
}

bool AppController::hidden(const NostrEvent& event) const {
    if (m_muted.contains(event.pubkey))
        return true;
    const QString lower = event.content.toLower();
    for (const QString& word : m_mutedWords) {
        if (!word.isEmpty() && lower.contains(word))
            return true;
    }
    for (const QStringList& tag : event.tags) {
        if (tag.size() >= 2 && tag.at(0) == QStringLiteral("t") && m_mutedWords.contains(tag.at(1).toLower()))
            return true;
    }
    return false;
}

void AppController::showNote(const NostrEvent& event) {
    if (hidden(event))
        return;
    const QString name = nameFor(event.pubkey);
    const QString picture = pictureFor(event.pubkey);
    if (m_viewingProfile && event.pubkey == m_profilePubkey)
        m_profileNotes.upsert(event, name, picture);
    for (const QStringList& tag : event.tags) {
        if (tag.size() >= 2 && tag.at(0) == QStringLiteral("q"))
            requestQuote(tag.at(1).toLower());
    }
    if (m_viewingHashtag) {
        for (const QStringList& tag : event.tags) {
            if (tag.size() >= 2 && tag.at(0) == QStringLiteral("t") && tag.at(1).toLower() == m_hashtag)
                m_hashtagNotes.upsert(event, name, picture);
        }
    }
    bool mentionsMe = false;
    if (m_keys) {
        for (const QStringList& tag : event.tags) {
            if (tag.size() >= 2 && tag.at(0) == QStringLiteral("p") && tag.at(1).toLower() == m_keys->pubkeyHex())
                mentionsMe = true;
        }
    }
    if (mentionsMe && (!m_keys || event.pubkey != m_keys->pubkeyHex()))
        m_inbox.upsert(event, name, picture);
    if (m_feedMode == 2)
        return;
    if (m_feedMode == 1 && !m_follows.isEmpty() && m_keys && event.pubkey != m_keys->pubkeyHex() && !m_follows.contains(event.pubkey))
        return;
    m_timeline.upsert(event, name, picture);
}

void AppController::publishMuteList() {
    if (!m_keys)
        return;
    NostrEvent event;
    event.kind = 10000;
    event.createdAt = QDateTime::currentSecsSinceEpoch();
    for (const QString& pubkey : m_muted)
        event.tags.append({QStringLiteral("p"), pubkey});
    for (const QString& word : m_mutedWords)
        event.tags.append({QStringLiteral("word"), word});
    publishEvent(event);
}

void AppController::verifyNip05(const QString& pubkey, const QString& nip05) {
    const int at = nip05.indexOf(QLatin1Char('@'));
    if (at <= 0 || at + 1 >= nip05.size())
        return;
    const QString name = nip05.left(at).toLower();
    const QString domain = nip05.mid(at + 1).toLower();
    QUrl url(QStringLiteral("https://") + domain + QStringLiteral("/.well-known/nostr.json"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("name"), name);
    url.setQuery(query);
    QNetworkRequest request{url};
    request.setTransferTimeout(8000);
    QNetworkReply* reply = m_net.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, pubkey, nip05, name] {
        const QByteArray body = reply->isOpen() ? reply->readAll() : QByteArray();
        const bool ok = reply->error() == QNetworkReply::NoError;
        reply->deleteLater();
        if (!m_profiles.contains(pubkey))
            return;
        const QString listed = QJsonDocument::fromJson(body).object().value(QStringLiteral("names")).toObject().value(name).toString().toLower();
        m_profiles[pubkey].nip05Ok = ok && listed == pubkey;
        m_profiles[pubkey].nip05 = nip05;
        if (pubkey == m_profilePubkey)
            emit profileChanged();
    });
}

void AppController::resolveNip05(const QString& nip05, bool follow) {
    const int at = nip05.indexOf(QLatin1Char('@'));
    if (at <= 0)
        return;
    const QString name = nip05.left(at).toLower();
    const QString domain = nip05.mid(at + 1).toLower();
    QUrl url(QStringLiteral("https://") + domain + QStringLiteral("/.well-known/nostr.json"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("name"), name);
    url.setQuery(query);
    QNetworkReply* reply = m_net.get(QNetworkRequest{url});
    connect(reply, &QNetworkReply::finished, this, [this, reply, name, follow] {
        const QByteArray body = reply->isOpen() ? reply->readAll() : QByteArray();
        reply->deleteLater();
        const QString pubkey = QJsonDocument::fromJson(body).object().value(QStringLiteral("names")).toObject().value(name).toString().toLower();
        if (pubkey.size() != 64) {
            setStatus(QStringLiteral("nip05 not found"));
            return;
        }
        if (follow)
            toggleFollow(pubkey);
        else
            openProfile(pubkey);
    });
}

void AppController::requestQuote(const QString& eventId) {
    if (eventId.size() != 64 || m_noteCache.contains(eventId) || m_quoteAsked.contains(eventId))
        return;
    m_quoteAsked.insert(eventId);
    QJsonObject filter;
    filter.insert(QStringLiteral("ids"), QJsonArray{eventId});
    filter.insert(QStringLiteral("limit"), 1);
    m_pool.subscribe(QStringLiteral("quote-") + eventId.left(12), QJsonArray{filter});
}

void AppController::forgetNote(const QString& eventId) {
    if (eventId.size() != 64)
        return;
    m_deletedIds.insert(eventId);
    m_deletionAuthors.remove(eventId);
    m_noteCache.remove(eventId);
    m_noteTags.remove(eventId);
    m_timeline.remove(eventId);
    m_profileNotes.remove(eventId);
    m_replies.remove(eventId);
    if (m_openId == eventId)
        closeNote();
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
        if (m_viewingProfile && event.pubkey == m_profilePubkey)
            m_profileNotes.upsert(event, name, picture);
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
    const QRegularExpression hashTag(QStringLiteral("#([A-Za-z0-9_]{1,64})"));
    auto hashes = hashTag.globalMatch(content);
    while (hashes.hasNext()) {
        const auto match = hashes.next();
        const QString tag = match.captured(1).toLower();
        hits.append({static_cast<int>(match.capturedStart()), static_cast<int>(match.capturedEnd()),
            QStringLiteral("<a href=\"tag:%1\">#%2</a>").arg(tag, htmlEscape(match.captured(1)))});
    }
    const QRegularExpression imageUrl(QStringLiteral("https://\\S+\\.(?:jpg|jpeg|png|gif|webp)"));
    auto images = imageUrl.globalMatch(content);
    while (images.hasNext()) {
        const auto match = images.next();
        const QString url = match.captured(0);
        hits.append({static_cast<int>(match.capturedStart()), static_cast<int>(match.capturedEnd()),
            QStringLiteral("<br><img src=\"%1\">").arg(htmlEscape(url))});
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
    for (const QStringList& tag : tags) {
        if (tag.size() < 2 || tag.at(0) != QStringLiteral("q") || tag.at(1).size() != 64)
            continue;
        const QString id = tag.at(1).toLower();
        const auto quoted = m_noteCache.constFind(id);
        if (quoted == m_noteCache.cend()) {
            out += QStringLiteral("<br><a href=\"e:%1\">quoted note</a>").arg(id);
            continue;
        }
        QString who = nameFor(quoted->pubkey);
        if (who.isEmpty())
            who = quoted->pubkey.left(8);
        QString text = quoted->content;
        if (text.size() > 800)
            text = text.left(800) + QStringLiteral("…");
        out += QStringLiteral("<br><font color=\"#8a8a8a\">%1</font><br>%2")
                   .arg(htmlEscape(who), htmlEscape(text));
        const QRegularExpression quotedImage(QStringLiteral("https://\\S+\\.(?:jpg|jpeg|png|gif|webp)"));
        auto quotedImages = quotedImage.globalMatch(quoted->content);
        while (quotedImages.hasNext())
            out += QStringLiteral("<br><img src=\"%1\">").arg(htmlEscape(quotedImages.next().captured(0)));
        out += QStringLiteral("<br><a href=\"e:%1\">open quote</a>").arg(id);
    }
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
    if (!m_saveTimer.isActive())
        m_saveTimer.start();
}

void AppController::rebuildOutbox() {
    m_pool.clearOutbox();
    QSet<QString> urls;
    for (const QString& pubkey : m_follows) {
        for (const QString& url : m_writeRelays.value(pubkey))
            urls.insert(url);
    }
    for (const QString& url : urls)
        m_pool.addOutboxRelay(url);
}

void AppController::rememberNote(const NostrEvent& event) {
    if (m_noteCache.contains(event.id))
        return;
    m_noteCache.insert(event.id, event);
    trimNoteCache();
    scheduleSave();
}

void AppController::trimNoteCache() {
    const int cap = 200;
    const QString mine = pubkey();
    auto pinned = [this, &mine](const NostrEvent& event) {
        return event.pubkey == mine || m_follows.contains(event.pubkey);
    };
    while (m_noteCache.size() > cap) {
        QString drop;
        qint64 oldest = std::numeric_limits<qint64>::max();
        for (auto it = m_noteCache.cbegin(); it != m_noteCache.cend(); ++it) {
            if (pinned(*it))
                continue;
            if (it->createdAt < oldest) {
                oldest = it->createdAt;
                drop = it.key();
            }
        }
        if (drop.isEmpty()) {
            for (auto it = m_noteCache.cbegin(); it != m_noteCache.cend(); ++it) {
                if (it->createdAt < oldest) {
                    oldest = it->createdAt;
                    drop = it.key();
                }
            }
        }
        if (drop.isEmpty())
            break;
        m_noteCache.remove(drop);
        m_noteTags.remove(drop);
    }
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
        if (hidden(event))
            continue;
        for (const QStringList& tag : event.tags) {
            if (tag.size() >= 2 && tag.at(0) == QStringLiteral("q"))
                requestQuote(tag.at(1).toLower());
        }
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

bool AppController::loadFollowingCache() {
    const QString path = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/following.json");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    const QString owner = root.value(QStringLiteral("owner")).toString();
    if (m_keys && !owner.isEmpty() && owner != m_keys->pubkeyHex())
        return false;
    for (const QJsonValue& item : root.value(QStringLiteral("deleted")).toArray()) {
        const QString id = item.toString().toLower();
        if (id.size() == 64)
            m_deletedIds.insert(id);
    }
    const QJsonObject profiles = root.value(QStringLiteral("profiles")).toObject();
    for (auto it = profiles.begin(); it != profiles.end(); ++it) {
        if (it.key().size() != 64)
            continue;
        const QJsonObject item = it.value().toObject();
        Profile profile;
        profile.name = item.value(QStringLiteral("name")).toString();
        profile.picture = item.value(QStringLiteral("picture")).toString();
        profile.createdAt = item.value(QStringLiteral("created_at")).toInteger();
        if (profile.createdAt > m_profiles.value(it.key()).createdAt)
            m_profiles.insert(it.key(), profile);
    }
    int loaded = 0;
    for (const QJsonValue& item : root.value(QStringLiteral("notes")).toArray()) {
        auto event = NostrEvent::fromJson(item.toObject());
        if (!event || event->kind != 1 || event->id.size() != 64)
            continue;
        if (m_deletedIds.contains(event->id))
            continue;
        m_noteCache.insert(event->id, *event);
        ++loaded;
    }
    return loaded > 0;
}

void AppController::saveFollowingCache() {
    const QString mine = pubkey();
    QList<NostrEvent> notes;
    for (const NostrEvent& event : m_noteCache) {
        if (event.pubkey == mine || m_follows.contains(event.pubkey))
            notes.append(event);
    }
    std::sort(notes.begin(), notes.end(), [](const NostrEvent& a, const NostrEvent& b) {
        return a.createdAt > b.createdAt;
    });
    if (notes.size() > 80)
        notes.resize(80);
    QJsonArray noteArray;
    QJsonObject profiles;
    for (const NostrEvent& event : notes) {
        noteArray.append(QJsonDocument::fromJson(event.toWire()).object());
        if (profiles.contains(event.pubkey))
            continue;
        const Profile profile = m_profiles.value(event.pubkey);
        if (profile.createdAt <= 0 && profile.name.isEmpty() && profile.picture.isEmpty())
            continue;
        QJsonObject item;
        item.insert(QStringLiteral("name"), profile.name);
        item.insert(QStringLiteral("picture"), profile.picture);
        item.insert(QStringLiteral("created_at"), profile.createdAt);
        profiles.insert(event.pubkey, item);
    }
    QJsonObject root;
    if (m_keys)
        root.insert(QStringLiteral("owner"), m_keys->pubkeyHex());
    root.insert(QStringLiteral("notes"), noteArray);
    root.insert(QStringLiteral("profiles"), profiles);
    QJsonArray deleted;
    for (const QString& id : m_deletedIds)
        deleted.append(id);
    root.insert(QStringLiteral("deleted"), deleted);
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(dir);
    QSaveFile file(dir + QStringLiteral("/following.json"));
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    file.commit();
}

void AppController::loadCache() {
    const QString path = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/feed.json");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    const QString owner = root.value(QStringLiteral("owner")).toString();
    if (m_keys && !owner.isEmpty() && owner != m_keys->pubkeyHex())
        return;
    for (const QJsonValue& item : root.value(QStringLiteral("deleted")).toArray()) {
        const QString id = item.toString().toLower();
        if (id.size() == 64)
            forgetNote(id);
    }
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
    const QJsonObject social = root.value(QStringLiteral("social")).toObject();
    for (auto it = social.begin(); it != social.end(); ++it) {
        if (it.key().size() != 64)
            continue;
        const QJsonObject item = it.value().toObject();
        Social entry;
        const QJsonArray following = item.value(QStringLiteral("following")).toArray();
        const QJsonArray followers = item.value(QStringLiteral("followers")).toArray();
        const int followCap = qMin(following.size(), 80);
        const int followerCap = qMin(followers.size(), 40);
        for (int i = 0; i < followCap; ++i) {
            const QString pubkey = following.at(i).toString().toLower();
            if (pubkey.size() == 64)
                entry.following.append(pubkey);
        }
        for (int i = 0; i < followerCap; ++i) {
            const QString pubkey = followers.at(i).toString().toLower();
            if (pubkey.size() == 64)
                entry.followers.append(pubkey);
        }
        entry.followingAt = item.value(QStringLiteral("followingAt")).toInteger(-1);
        entry.followersAt = item.value(QStringLiteral("followersAt")).toInteger(0);
        m_social.insert(it.key(), entry);
    }
    for (const QJsonValue& item : root.value(QStringLiteral("notes")).toArray()) {
        auto event = NostrEvent::fromJson(item.toObject());
        if (event && event->kind == 1 && event->id.size() == 64 && !m_deletedIds.contains(event->id))
            m_noteCache.insert(event->id, *event);
    }
}

void AppController::saveCache() {
    QList<NostrEvent> all = m_noteCache.values();
    std::sort(all.begin(), all.end(), [](const NostrEvent& a, const NostrEvent& b) {
        return a.createdAt > b.createdAt;
    });
    const QString mine = pubkey();
    QList<NostrEvent> notes;
    QList<NostrEvent> other;
    for (const NostrEvent& event : all) {
        if (event.pubkey == mine || m_follows.contains(event.pubkey)) {
            if (notes.size() < 100)
                notes.append(event);
        } else if (other.size() < 40) {
            other.append(event);
        }
    }
    notes += other;
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
    QJsonObject social;
    for (auto it = m_social.cbegin(); it != m_social.cend(); ++it) {
        if (it->following.isEmpty() && it->followers.isEmpty())
            continue;
        QJsonObject item;
        QJsonArray following;
        QJsonArray followers;
        const int followCap = qMin(it->following.size(), 80);
        const int followerCap = qMin(it->followers.size(), 40);
        for (int i = 0; i < followCap; ++i)
            following.append(it->following.at(i));
        for (int i = 0; i < followerCap; ++i)
            followers.append(it->followers.at(i));
        item.insert(QStringLiteral("following"), following);
        item.insert(QStringLiteral("followers"), followers);
        item.insert(QStringLiteral("followingAt"), it->followingAt);
        item.insert(QStringLiteral("followersAt"), it->followersAt);
        social.insert(it.key(), item);
    }
    QJsonObject root;
    if (m_keys)
        root.insert(QStringLiteral("owner"), m_keys->pubkeyHex());
    root.insert(QStringLiteral("profiles"), profiles);
    root.insert(QStringLiteral("notes"), noteArray);
    root.insert(QStringLiteral("follows"), follows);
    root.insert(QStringLiteral("social"), social);
    root.insert(QStringLiteral("contactsAt"), m_contactsAt);
    QJsonArray deleted;
    for (const QString& id : m_deletedIds)
        deleted.append(id);
    root.insert(QStringLiteral("deleted"), deleted);
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(dir);
    QSaveFile file(dir + QStringLiteral("/feed.json"));
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    file.commit();
    saveFollowingCache();
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
