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
    if (url.hasQuery() || url.host().contains(QStringLiteral("nwc"), Qt::CaseInsensitive))
        return {};
    const QString path = url.path().toLower();
    if (path.contains(QStringLiteral("inbox")))
        return {};
    for (const QString& part : url.path().split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        if (part.size() >= 20)
            return {};
    }
    return url.toString(QUrl::FullyEncoded);
}

QString logRelay(const QString& url) {
    const QUrl parsed(url);
    if (parsed.path().size() > 1 || parsed.hasQuery())
        return parsed.scheme() + QStringLiteral("://") + parsed.host() + QStringLiteral("/");
    return url;
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
        m_stable.setSingleShot(true);
        connect(&m_stable, &QTimer::timeout, this, [this] { m_attempt = 0; });
        connect(&m_socket, &WebSocketClient::connected, this, [this] {
            m_open = true;
            m_detail.clear();
            m_stable.start(25000);
            qInfo().noquote() << "relay open" << logRelay(m_url);
            emit m_pool->relayStateChanged(m_url, QStringLiteral("open"), QString());
            QTimer::singleShot(0, this, [this] {
                if (m_open && !m_authPending)
                    m_pool->sendSubscriptions(this);
            });
        });
        connect(&m_socket, &WebSocketClient::disconnected, this, [this] {
            m_open = false;
            m_authPending = false;
            m_authId.clear();
            m_stable.stop();
            if (!m_wanted)
                return;
            emit m_pool->relayStateChanged(m_url, QStringLiteral("closed"), m_detail);
            scheduleRetry();
        });
        connect(&m_socket, &WebSocketClient::errorOccurred, this, [this](const QString& message) {
            m_detail = message;
            qInfo().noquote() << "relay" << logRelay(m_url) << message;
            emit m_pool->relayStateChanged(m_url, m_open ? QStringLiteral("open") : QStringLiteral("error"), message);
        });
        connect(&m_socket, &WebSocketClient::redirected, this, [this](const QUrl& url) {
            const QString next = normalizeRelay(url.toString());
            if (next.isEmpty() || next == m_url) {
                m_wanted = false;
                m_retry.stop();
                qInfo().noquote() << "relay dropped" << logRelay(m_url);
                return;
            }
            m_pool->retarget(this, next);
            QTimer::singleShot(0, this, [this] { ensureOpen(); });
        });
        connect(&m_socket, &WebSocketClient::rejected, this, [this](const QString&) {
            m_wanted = false;
            m_retry.stop();
            qInfo().noquote() << "relay dropped" << logRelay(m_url);
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
    void setUrl(const QString& url) { m_url = url; }

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
            if (event.isEmpty())
                return;
            const QJsonObject signedEvent = QJsonDocument::fromJson(event).object();
            m_authId = signedEvent.value(QStringLiteral("id")).toString();
            m_authPending = true;
            m_open = true;
            send("[\"AUTH\"," + event + "]");
        } else if (type == QStringLiteral("OK") && arr.size() >= 4) {
            if (!m_authId.isEmpty() && arr.at(1).toString() == m_authId) {
                m_authPending = false;
                m_authId.clear();
                if (arr.at(2).toBool())
                    m_pool->sendSubscriptions(this);
            }
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
    QTimer m_stable;
    QString m_detail;
    QString m_authId;
    int m_attempt = 0;
    bool m_open = false;
    bool m_wanted = true;
    bool m_authPending = false;
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
        || id == QStringLiteral("relaylists") || id == QStringLiteral("graph") || id == QStringLiteral("mutes");
}

void RelayPool::retarget(Session* session, const QString& raw) {
    const QString url = normalizeRelay(raw);
    if (!session || url.isEmpty() || url == session->url())
        return;
    if (m_sessions.contains(url)) {
        session->stop();
        return;
    }
    const QString old = session->url();
    m_sessions.remove(old);
    session->setUrl(url);
    m_sessions.insert(url, session);
    if (m_outbox.remove(old))
        m_outbox.insert(url);
    if (m_metadata.remove(old))
        m_metadata.insert(url);
    const int at = m_order.indexOf(old);
    if (at >= 0)
        m_order[at] = url;
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

void RelayPool::clearOutbox() {
    const QStringList urls = m_outbox.values();
    m_outbox.clear();
    for (const QString& url : urls) {
        if (m_order.contains(url) || m_metadata.contains(url))
            continue;
        Session* session = m_sessions.take(url);
        if (!session)
            continue;
        session->stop();
        session->deleteLater();
    }
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
        if (m_outbox.contains(url) && id != QStringLiteral("following") && id != QStringLiteral("thread") && id != QStringLiteral("person") && id != QStringLiteral("graph"))
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
    for (auto it = m_sessions.cbegin(); it != m_sessions.cend(); ++it) {
        if (m_metadata.contains(it.key()) || m_outbox.contains(it.key()))
            continue;
        if (!it.value()->open())
            continue;
        it.value()->send(message);
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
        if (outbox && it.key() != QStringLiteral("following") && it.key() != QStringLiteral("thread") && it.key() != QStringLiteral("person") && it.key() != QStringLiteral("graph"))
            continue;
        session->send(it.value());
    }
}
