#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QStringList>

class RelayPool : public QObject {
    Q_OBJECT
public:
    explicit RelayPool(QObject* parent = nullptr);

    void setRelays(const QStringList& urls);
    QStringList relays() const;
    void subscribe(const QString& id, const QJsonArray& filters);
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

    QHash<QString, Session*> m_sessions;
    QHash<QString, QByteArray> m_subs;
    QStringList m_order;
};
