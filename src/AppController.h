#pragma once

#include "ImageCache.h"
#include "TimelineModel.h"
#include "nostr/RelayPool.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QVariantList>

#include <optional>

class AppController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool hasIdentity READ hasIdentity NOTIFY identityChanged)
    Q_PROPERTY(QString npub READ npub NOTIFY identityChanged)
    Q_PROPERTY(QString pubkey READ pubkey NOTIFY identityChanged)
    Q_PROPERTY(QString nsec READ nsec NOTIFY identityChanged)
    Q_PROPERTY(QString displayName READ displayName NOTIFY identityChanged)
    Q_PROPERTY(QString about READ about NOTIFY identityChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(int feedMode READ feedMode WRITE setFeedMode NOTIFY feedModeChanged)
    Q_PROPERTY(int openRelays READ openRelays NOTIFY relaysChanged)
    Q_PROPERTY(int relayCount READ relayCount NOTIFY relaysChanged)
    Q_PROPERTY(QVariantList relays READ relays NOTIFY relaysChanged)
    Q_PROPERTY(QString relayText READ relayText NOTIFY relaysChanged)
    Q_PROPERTY(TimelineModel* timeline READ timeline CONSTANT)
    Q_PROPERTY(bool replying READ replying NOTIFY replyChanged)
    Q_PROPERTY(QString replyLabel READ replyLabel NOTIFY replyChanged)
    Q_PROPERTY(int followCount READ followCount NOTIFY followsChanged)
    Q_PROPERTY(QString selfPicture READ selfPicture NOTIFY identityChanged)
    Q_PROPERTY(bool viewing READ viewing NOTIFY noteChanged)
    Q_PROPERTY(QString openNotePicture READ openNotePicture NOTIFY noteChanged)
    Q_PROPERTY(QString openNoteRich READ openNoteRich NOTIFY noteChanged)
    Q_PROPERTY(bool viewingProfile READ viewingProfile NOTIFY profileChanged)
    Q_PROPERTY(QString profilePubkey READ profilePubkey NOTIFY profileChanged)
    Q_PROPERTY(QString profileName READ profileName NOTIFY profileChanged)
    Q_PROPERTY(QString profileAbout READ profileAbout NOTIFY profileChanged)
    Q_PROPERTY(QString profilePicture READ profilePicture NOTIFY profileChanged)
    Q_PROPERTY(QString profileNpub READ profileNpub NOTIFY profileChanged)
    Q_PROPERTY(TimelineModel* profileNotes READ profileNotes CONSTANT)
    Q_PROPERTY(QString openNoteId READ openNoteId NOTIFY noteChanged)
    Q_PROPERTY(QString openNotePubkey READ openNotePubkey NOTIFY noteChanged)
    Q_PROPERTY(QString openNoteName READ openNoteName NOTIFY noteChanged)
    Q_PROPERTY(QString openNoteContent READ openNoteContent NOTIFY noteChanged)
    Q_PROPERTY(QString openNoteTime READ openNoteTime NOTIFY noteChanged)
    Q_PROPERTY(TimelineModel* replies READ replies CONSTANT)

public:
    explicit AppController(QObject* parent = nullptr);

    bool hasIdentity() const;
    QString npub() const;
    QString pubkey() const;
    QString nsec() const;
    QString displayName() const;
    QString about() const;
    QString statusText() const;
    int feedMode() const;
    void setFeedMode(int mode);
    int openRelays() const;
    int relayCount() const;
    QVariantList relays() const;
    QString relayText() const;
    TimelineModel* timeline();
    bool replying() const;
    QString replyLabel() const;
    int followCount() const;
    bool viewing() const;
    QString openNoteId() const;
    QString openNotePubkey() const;
    QString openNoteName() const;
    QString openNoteContent() const;
    QString openNoteTime() const;
    QString openNotePicture() const;
    QString openNoteRich() const;
    QString selfPicture() const;
    bool viewingProfile() const;
    QString profilePubkey() const;
    QString profileName() const;
    QString profileAbout() const;
    QString profilePicture() const;
    QString profileNpub() const;
    TimelineModel* replies();
    TimelineModel* profileNotes();

    Q_INVOKABLE void generateIdentity();
    Q_INVOKABLE void importSecret(const QString& text);
    Q_INVOKABLE void publishNote(const QString& text);
    Q_INVOKABLE void publishProfile(const QString& name, const QString& about);
    Q_INVOKABLE void beginReply(const QString& eventId, const QString& pubkey, const QString& name);
    Q_INVOKABLE void cancelReply();
    Q_INVOKABLE void toggleFollow(const QString& pubkey);
    Q_INVOKABLE bool isFollowed(const QString& pubkey) const;
    Q_INVOKABLE void followNpub(const QString& text);
    Q_INVOKABLE void openNote(const QString& eventId, const QString& pubkey, const QString& name, const QString& content, const QString& timeText);
    Q_INVOKABLE void closeNote();
    Q_INVOKABLE void openProfile(const QString& pubkeyOrNpub);
    Q_INVOKABLE void closeProfile();
    Q_INVOKABLE void openLink(const QString& link);
    Q_INVOKABLE void setRelaysFromText(const QString& text);
    Q_INVOKABLE void copyNpub();

signals:
    void identityChanged();
    void statusChanged();
    void feedModeChanged();
    void relaysChanged();
    void replyChanged();
    void followsChanged();
    void noteChanged();
    void profileChanged();

private:
    struct Profile {
        QString name;
        QString about;
        QString picture;
        qint64 createdAt = 0;
    };
    struct Reply {
        QString id;
        QString pubkey;
        QString name;
    };

    void adopt(Keys keys);
    void persist();
    void load();
    void loadCache();
    void saveCache();
    void scheduleSave();
    void rememberNote(const NostrEvent& event);
    void refillTimeline();
    void connectDefault();
    void subscribeAll();
    void subscribeFeed();
    void subscribeRelayLists();
    void subscribeProfiles();
    void noteAuthor(const QString& pubkey);
    void indexPeople(const NostrEvent& event);
    QString renderNote(const QString& content, const QVector<QStringList>& tags) const;
    QString personPubkey(const QString& text) const;
    QString pictureFor(const QString& pubkey) const;
    void handleEvent(const QString& relay, const QString& subId, const QJsonObject& object);
    void handleProfile(const NostrEvent& event);
    void handleContacts(const NostrEvent& event);
    void publishContacts();
    bool publishEvent(NostrEvent event);
    QString nameFor(const QString& pubkey) const;
    void setStatus(const QString& text);
    static QStringList defaultRelays();

    RelayPool m_pool;
    ImageCache m_images;
    TimelineModel m_timeline;
    TimelineModel m_replies{nullptr, false};
    TimelineModel m_profileNotes;
    QTimer m_profileTimer;
    QTimer m_clock;
    std::optional<Keys> m_keys;
    QString m_displayName;
    QString m_about;
    QString m_status;
    QHash<QString, Profile> m_profiles;
    QHash<QString, QString> m_relayState;
    QHash<QString, QString> m_relayDetail;
    QSet<QString> m_follows;
    QSet<QString> m_pendingProfiles;
    QTimer m_mentionTimer;
    QTimer m_saveTimer;
    QHash<QString, NostrEvent> m_noteCache;
    qint64 m_contactsAt = -1;
    int m_feedMode = 0;
    std::optional<Reply> m_reply;
    QString m_openId;
    QString m_openPubkey;
    QString m_openName;
    QString m_openContent;
    QString m_openTime;
    QVector<QStringList> m_openTags;
    QHash<QString, QVector<QStringList>> m_noteTags;
    bool m_viewingProfile = false;
    QString m_profilePubkey;
};
