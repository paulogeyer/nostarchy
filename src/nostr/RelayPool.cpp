#include "nostr/RelayPool.h"

#include "net/WebSocketClient.h"

#include <QJsonDocument>
#include <QTimer>
#include <QUrl>
#include <QtLogging>

namespace {

QString normalizeRelay(QString raw) {
    raw = raw.trimmed();
    if (raw.isEmpty() || raw.startsWith(QLatin1Char('#')))
        return {};
    if (!raw.contains(QStringLiteral("://")))
        raw = QStringLiteral("wss://") + raw;
    QUrl url(raw);
    if (!url.isValid() || url.host().isEmpty())
        return {};
    const QString scheme = url.scheme().toLower();
    if (scheme != QStringLiteral("wss") && scheme != QStringLiteral("ws"))
        return {};
    url.setScheme(scheme);
    url.setHost(url.host().toLower());
    if (url.path().isEmpty())
        url.setPath(QStringLiteral("/"));
    url.setFragment({});
    return url.toString(QUrl::FullyEncoded);
}

}

class RelayPool::Session : public QObject {
public:
    Session(const QString& url, RelayPool* pool)
        : QObject(pool)
        , m_url(url)
        , m_pool(pool) {
        m_retry.setSingleShot(true);
        connect(&m_retry, &QTimer::timeout, this, [this] { ensureOpen(); });
        connect(&m_socket, &WebSocketClient::connected, this, [this] {
            m_open = true;
            m_attempt = 0;
            m_detail.clear();
            emit m_pool->relayStateChanged(m_url, QStringLiteral("open"), QString());
            m_pool->sendSubscriptions(this);
        });
        connect(&m_socket, &WebSocketClient::disconnected, this, [this] {
            m_open = false;
            if (!m_wanted)
                return;
            emit m_pool->relayStateChanged(m_url, QStringLiteral("closed"), m_detail);
            scheduleRetry();
        });
        connect(&m_socket, &WebSocketClient::errorOccurred, this, [this](const QString& message) {
            m_detail = message;
            qInfo().noquote() << "relay" << m_url << message;
            emit m_pool->relayStateChanged(m_url, m_open ? QStringLiteral("open") : QStringLiteral("error"), message);
        });
        connect(&m_socket, &WebSocketClient::textMessageReceived, this, [this](const QString& text) {
            handle(text);
        });
        ensureOpen();
    }

    void stop() {
        m_wanted = false;
        m_retry.stop();
        m_socket.close();
    }

    void send(const QByteArray& bytes) {
        if (m_open)
            m_socket.sendText(QString::fromUtf8(bytes));
    }

    bool open() const { return m_open; }
    QString url() const { return m_url; }

private:
    void ensureOpen() {
        if (!m_wanted || m_socket.isOpen() || m_socket.isConnecting())
            return;
        emit m_pool->relayStateChanged(m_url, QStringLiteral("connecting"), m_detail);
        m_socket.open(QUrl(m_url));
    }

    void scheduleRetry() {
        if (!m_wanted || m_retry.isActive())
            return;
        const int delay = qMin(30000, 1000 * (1 << qMin(m_attempt, 5)));
        ++m_attempt;
        m_retry.start(delay);
    }

    void handle(const QString& text) {
        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &error);
        if (!doc.isArray())
            return;
        const QJsonArray arr = doc.array();
        if (arr.isEmpty())
            return;
        const QString type = arr.at(0).toString();
        if (type == QStringLiteral("EVENT") && arr.size() >= 3) {
            emit m_pool->eventReceived(m_url, arr.at(1).toString(), arr.at(2).toObject());
        } else if (type == QStringLiteral("AUTH") && arr.size() >= 2) {
            const QByteArray event = m_pool->authEvent(m_url, arr.at(1).toString());
            if (!event.isEmpty())
                send("[\"AUTH\"," + event + "]");
        } else if (type == QStringLiteral("OK") && arr.size() >= 4) {
            emit m_pool->okReceived(m_url, arr.at(1).toString(), arr.at(2).toBool(), arr.at(3).toString());
        } else if (type == QStringLiteral("EOSE") && arr.size() >= 2) {
            m_detail = QStringLiteral("live");
            emit m_pool->eoseReceived(m_url, arr.at(1).toString());
            emit m_pool->relayStateChanged(m_url, QStringLiteral("open"), m_detail);
        } else if (type == QStringLiteral("NOTICE") && arr.size() >= 2) {
            const QString message = arr.at(1).toString();
            m_detail = message;
            emit m_pool->noticeReceived(m_url, message);
            emit m_pool->relayStateChanged(m_url, QStringLiteral("open"), message);
        } else if (type == QStringLiteral("CLOSED") && arr.size() >= 3) {
            const QString message = arr.at(2).toString();
            m_detail = message;
            emit m_pool->noticeReceived(m_url, message);
            emit m_pool->relayStateChanged(m_url, QStringLiteral("open"), message);
        }
    }

    QString m_url;
    RelayPool* m_pool;
    WebSocketClient m_socket;
    QTimer m_retry;
    QString m_detail;
    int m_attempt = 0;
    bool m_open = false;
    bool m_wanted = true;
};

RelayPool::RelayPool(QObject* parent)
    : QObject(parent) {}

void RelayPool::setRelays(const QStringList& urls) {
    QStringList next;
    for (const QString& raw : urls) {
        const QString url = normalizeRelay(raw);
        if (!url.isEmpty() && !next.contains(url))
            next.append(url);
    }
    for (auto it = m_sessions.begin(); it != m_sessions.end();) {
        if (!next.contains(it.key()) && !m_metadata.contains(it.key()) && !m_outbox.contains(it.key())) {
            it.value()->stop();
            it.value()->deleteLater();
            it = m_sessions.erase(it);
        } else {
            ++it;
        }
    }
    for (const QString& url : next) {
        if (!m_sessions.contains(url))
            m_sessions.insert(url, new Session(url, this));
    }
    m_order = next;
    emit relayStateChanged(QString(), QStringLiteral("list"), QString());
}

QStringList RelayPool::relays() const { return m_order; }

void RelayPool::addMetadataRelay(const QString& raw) {
    const QString url = normalizeRelay(raw);
    if (url.isEmpty() || (!m_metadata.contains(url) && m_metadata.size() >= 16))
        return;
    m_metadata.insert(url);
    if (!m_sessions.contains(url))
        m_sessions.insert(url, new Session(url, this));
    else if (Session* session = m_sessions.value(url); session->open())
        sendSubscriptions(session);
}

bool RelayPool::isMetadataSub(const QString& id) {
    return id == QStringLiteral("profiles") || id == QStringLiteral("person") || id == QStringLiteral("thread")
        || id == QStringLiteral("relaylists");
}

void RelayPool::addOutboxRelay(const QString& raw) {
    const QString url = normalizeRelay(raw);
    if (url.isEmpty() || m_order.contains(url))
        return;
    if (!m_outbox.contains(url) && m_outbox.size() >= 8)
        return;
    m_outbox.insert(url);
    if (!m_sessions.contains(url))
        m_sessions.insert(url, new Session(url, this));
    else if (Session* session = m_sessions.value(url); session->open())
        sendSubscriptions(session);
}

void RelayPool::setAuthSigner(const std::function<QByteArray(const QString&, const QString&)>& signer) {
    m_signer = signer;
}

QByteArray RelayPool::authEvent(const QString& relay, const QString& challenge) const {
    return m_signer ? m_signer(relay, challenge) : QByteArray();
}

void RelayPool::subscribe(const QString& id, const QJsonArray& filters) {
    QJsonArray message;
    message.append(QStringLiteral("REQ"));
    message.append(id);
    for (const QJsonValue& filter : filters)
        message.append(filter);
    const QByteArray bytes = QJsonDocument(message).toJson(QJsonDocument::Compact);
    m_subs.insert(id, bytes);
    for (Session* session : m_sessions) {
        const QString url = session->url();
        if (m_metadata.contains(url) && !isMetadataSub(id))
            continue;
        if (m_outbox.contains(url) && id != QStringLiteral("following"))
            continue;
        session->send(bytes);
    }
}

void RelayPool::unsubscribe(const QString& id) {
    if (!m_subs.contains(id))
        return;
    m_subs.remove(id);
    const QByteArray bytes = QJsonDocument(QJsonArray{QStringLiteral("CLOSE"), id}).toJson(QJsonDocument::Compact);
    for (Session* session : m_sessions)
        session->send(bytes);
}

int RelayPool::publish(const QByteArray& wireEvent) {
    const QByteArray message = "[\"EVENT\"," + wireEvent + "]";
    int sent = 0;
    for (Session* session : m_sessions) {
        if (!session->open())
            continue;
        session->send(message);
        ++sent;
    }
    return sent;
}

int RelayPool::openCount() const {
    int n = 0;
    for (auto it = m_sessions.cbegin(); it != m_sessions.cend(); ++it) {
        if (m_metadata.contains(it.key()) || m_outbox.contains(it.key()))
            continue;
        if (it.value()->open())
            ++n;
    }
    return n;
}

void RelayPool::sendSubscriptions(Session* session) {
    const QString url = session->url();
    const bool metadata = m_metadata.contains(url);
    const bool outbox = m_outbox.contains(url);
    for (auto it = m_subs.cbegin(); it != m_subs.cend(); ++it) {
        if (metadata && !isMetadataSub(it.key()))
            continue;
        if (outbox && it.key() != QStringLiteral("following"))
            continue;
        session->send(it.value());
    }
}
