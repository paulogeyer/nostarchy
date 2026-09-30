#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QStringList>

#include <functional>

class RelayPool : public QObject {
    Q_OBJECT
public:
    explicit RelayPool(QObject* parent = nullptr);

    void setRelays(const QStringList& urls);
    void addMetadataRelay(const QString& url);
    void addOutboxRelay(const QString& url);
    void setAuthSigner(const std::function<QByteArray(const QString& relay, const QString& challenge)>& signer);
    QByteArray authEvent(const QString& relay, const QString& challenge) const;
    QStringList relays() const;
    void subscribe(const QString& id, const QJsonArray& filters);
    void unsubscribe(const QString& id);
    int publish(const QByteArray& wireEvent);
    int openCount() const;

signals:
    void eventReceived(const QString& relay, const QString& subId, const QJsonObject& event);
    void okReceived(const QString& relay, const QString& eventId, bool accepted, const QString& message);
    void noticeReceived(const QString& relay, const QString& message);
    void eoseReceived(const QString& relay, const QString& subId);
    void relayStateChanged(const QString& url, const QString& state, const QString& detail);

private:
    class Session;
    void sendSubscriptions(Session* session);

    static bool isMetadataSub(const QString& id);
    QHash<QString, Session*> m_sessions;
    QHash<QString, QByteArray> m_subs;
    QSet<QString> m_metadata;
    QSet<QString> m_outbox;
    QStringList m_order;
    std::function<QByteArray(const QString&, const QString&)> m_signer;
};
