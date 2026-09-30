#pragma once

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

    Q_INVOKABLE void generateIdentity();
    Q_INVOKABLE void importSecret(const QString& text);
    Q_INVOKABLE void publishNote(const QString& text);
    Q_INVOKABLE void publishProfile(const QString& name, const QString& about);
    Q_INVOKABLE void beginReply(const QString& eventId, const QString& pubkey, const QString& name);
    Q_INVOKABLE void cancelReply();
    Q_INVOKABLE void toggleFollow(const QString& pubkey);
    Q_INVOKABLE void setRelaysFromText(const QString& text);
    Q_INVOKABLE void copyNpub();

signals:
    void identityChanged();
    void statusChanged();
    void feedModeChanged();
    void relaysChanged();
    void replyChanged();
    void followsChanged();

private:
    struct Profile {
        QString name;
        QString about;
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
    void connectDefault();
    void subscribeAll();
    void subscribeFeed();
    void subscribeProfiles();
    void noteAuthor(const QString& pubkey);
    void handleEvent(const QString& relay, const QString& subId, const QJsonObject& object);
    void handleProfile(const NostrEvent& event);
    void handleContacts(const NostrEvent& event);
    void publishContacts();
    bool publishEvent(NostrEvent event);
    QString nameFor(const QString& pubkey) const;
    void setStatus(const QString& text);
    static QStringList defaultRelays();

    RelayPool m_pool;
    TimelineModel m_timeline;
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
    QSet<QString> m_seenAuthors;
    QSet<QString> m_pendingProfiles;
    qint64 m_contactsAt = -1;
    int m_feedMode = 0;
    std::optional<Reply> m_reply;
};
