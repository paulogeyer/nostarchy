#include "net/WebSocketClient.h"

#include "util/Bytes.h"

#include <QCryptographicHash>

WebSocketClient::WebSocketClient(QObject* parent)
    : QObject(parent) {
    m_connectTimer.setSingleShot(true);
    connect(&m_connectTimer, &QTimer::timeout, this, [this] {
        fail(QStringLiteral("connection timed out"));
    });
    m_pingTimer.setInterval(25000);
    connect(&m_pingTimer, &QTimer::timeout, this, [this] {
        if (m_state != State::Open)
            return;
        if (m_lastInbound.isValid() && m_lastInbound.elapsed() > 50000) {
            fail(QStringLiteral("relay went quiet"));
            return;
        }
        sendFrame(0x9, QByteArray("n"));
    });
}

void WebSocketClient::open(const QUrl& url) {
    close();
    m_userClose = false;
    m_failed = false;
    m_url = url;
    m_buffer.clear();
    m_message.clear();
    m_messageOpcode = -1;
    resetParser();
    m_state = State::Connecting;
    m_tls = url.scheme() == QStringLiteral("wss");

    if (!m_socket) {
        m_socket = new QSslSocket(this);
        connect(m_socket, &QSslSocket::encrypted, this, &WebSocketClient::startHandshake);
        connect(m_socket, &QSslSocket::connected, this, [this] {
            if (!m_tls)
                startHandshake();
        });
        connect(m_socket, &QSslSocket::readyRead, this, &WebSocketClient::onReadyRead);
        connect(m_socket, &QAbstractSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
            onSocketError();
        });
        connect(m_socket, &QSslSocket::sslErrors, this, [this](const QList<QSslError>& errors) {
            QStringList parts;
            for (const QSslError& error : errors)
                parts.append(error.errorString());
            fail(parts.join(QStringLiteral("; ")));
        });
        connect(m_socket, &QSslSocket::disconnected, this, [this] {
            m_connectTimer.stop();
            m_pingTimer.stop();
            const bool notify = m_state != State::Idle && !m_userClose;
            m_state = State::Idle;
            if (notify)
                emit disconnected();
        });
    }

    const QString host = url.host();
    const int port = url.port(m_tls ? 443 : 80);
    if (m_tls)
        m_socket->connectToHostEncrypted(host, static_cast<quint16>(port));
    else
        m_socket->connectToHost(host, static_cast<quint16>(port));
    m_connectTimer.start(15000);
}

void WebSocketClient::close() {
    m_userClose = true;
    m_connectTimer.stop();
    m_pingTimer.stop();
    if (!m_socket)
        return;
    if (m_state == State::Open)
        sendFrame(0x8, QByteArray());
    m_state = State::Idle;
    m_socket->abort();
}

void WebSocketClient::sendText(const QString& text) {
    if (m_state != State::Open)
        return;
    sendFrame(0x1, text.toUtf8());
}

bool WebSocketClient::isOpen() const { return m_state == State::Open; }
bool WebSocketClient::isConnecting() const {
    return m_state == State::Connecting || m_state == State::Handshake;
}

void WebSocketClient::startHandshake() {
    if (m_state != State::Connecting || !m_socket)
        return;
    m_state = State::Handshake;
    m_acceptKey = randomBytes(16).toBase64();
    const QString host = m_url.host();
    const int port = m_url.port(m_tls ? 443 : 80);
    QString hostHeader = host;
    if (!((m_tls && port == 443) || (!m_tls && port == 80)))
        hostHeader += QLatin1Char(':') + QString::number(port);
    QString path = m_url.path(QUrl::FullyEncoded);
    if (path.isEmpty())
        path = QStringLiteral("/");
    if (m_url.hasQuery())
        path += QLatin1Char('?') + m_url.query(QUrl::FullyEncoded);

    QByteArray request;
    request += "GET " + path.toUtf8() + " HTTP/1.1\r\n";
    request += "Host: " + hostHeader.toUtf8() + "\r\n";
    request += "Upgrade: websocket\r\n";
    request += "Connection: Upgrade\r\n";
    request += "Sec-WebSocket-Key: " + m_acceptKey + "\r\n";
    request += "Sec-WebSocket-Version: 13\r\n";
    request += "User-Agent: nostarchy/0.1\r\n";
    request += "\r\n";
    m_socket->write(request);
}

void WebSocketClient::onReadyRead() {
    if (!m_socket)
        return;
    m_buffer += m_socket->readAll();
    if (m_state == State::Handshake) {
        const int end = m_buffer.indexOf("\r\n\r\n");
        if (end < 0) {
            if (m_buffer.size() > 65536)
                fail(QStringLiteral("handshake too large"));
            return;
        }
        const QByteArray header = m_buffer.left(end);
        m_buffer.remove(0, end + 4);
        const QList<QByteArray> lines = header.split('\n');
        if (lines.isEmpty() || !lines.first().contains(" 101 ")) {
            const QByteArray statusLine = lines.value(0).trimmed();
            const int space = statusLine.indexOf(' ');
            const int code = space >= 0 ? statusLine.mid(space + 1, 3).toInt() : 0;
            QString location;
            for (int i = 1; i < lines.size(); ++i) {
                const QByteArray line = lines.at(i).trimmed();
                const int colon = line.indexOf(':');
                if (colon < 0)
                    continue;
                if (QString::fromLatin1(line.left(colon)).trimmed().toLower() == QStringLiteral("location"))
                    location = QString::fromLatin1(line.mid(colon + 1)).trimmed();
            }
            if (code >= 300 && code < 400 && !location.isEmpty() && m_redirects < 1) {
                QUrl next(location);
                if (next.isRelative())
                    next = m_url.resolved(next);
                const QString scheme = next.scheme().toLower();
                if (scheme == QStringLiteral("https"))
                    next.setScheme(QStringLiteral("wss"));
                else if (scheme == QStringLiteral("http"))
                    next.setScheme(QStringLiteral("ws"));
                const QString relayScheme = next.scheme().toLower();
                if ((relayScheme == QStringLiteral("wss") || relayScheme == QStringLiteral("ws")) && next.isValid()) {
                    ++m_redirects;
                    emit redirected(next);
                    m_userClose = true;
                    m_connectTimer.stop();
                    m_pingTimer.stop();
                    m_state = State::Idle;
                    m_failed = false;
                    if (m_socket)
                        m_socket->abort();
                    m_userClose = false;
                    return;
                }
            }
            const QString status = QString::fromLatin1(statusLine);
            if ((code >= 300 && code < 400) || (code >= 400 && code < 500))
                emit rejected(status.isEmpty() ? QStringLiteral("websocket upgrade failed") : status);
            fail(status.isEmpty() ? QStringLiteral("websocket upgrade failed") : status);
            return;
        }
        m_redirects = 0;
        QString accept;
        for (int i = 1; i < lines.size(); ++i) {
            const QByteArray line = lines.at(i).trimmed();
            const int colon = line.indexOf(':');
            if (colon < 0)
                continue;
            const QString name = QString::fromLatin1(line.left(colon)).trimmed().toLower();
            if (name == QStringLiteral("sec-websocket-accept"))
                accept = QString::fromLatin1(line.mid(colon + 1)).trimmed();
        }
        const QByteArray expected = QCryptographicHash::hash(
            m_acceptKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11", QCryptographicHash::Sha1).toBase64();
        if (accept.toLatin1() != expected) {
            fail(QStringLiteral("bad websocket accept"));
            return;
        }
        m_state = State::Open;
        m_connectTimer.stop();
        m_lastInbound.start();
        m_pingTimer.start();
        emit connected();
    }
    if (m_state == State::Open) {
        while (tryParseFrame()) {
        }
    }
}

void WebSocketClient::onSocketError() {
    if (!m_socket || m_userClose || m_failed)
        return;
    fail(m_socket->errorString());
}

void WebSocketClient::fail(const QString& message) {
    if (m_failed)
        return;
    m_failed = true;
    m_connectTimer.stop();
    m_pingTimer.stop();
    emit errorOccurred(message);
    if (m_socket && m_socket->state() != QAbstractSocket::UnconnectedState)
        m_socket->abort();
    else if (m_state != State::Idle) {
        m_state = State::Idle;
        if (!m_userClose)
            emit disconnected();
    }
}

void WebSocketClient::sendFrame(quint8 opcode, const QByteArray& payload) {
    if (!m_socket)
        return;
    QByteArray frame;
    frame.append(static_cast<char>(0x80 | opcode));
    const int len = payload.size();
    if (len <= 125) {
        frame.append(static_cast<char>(0x80 | len));
    } else if (len <= 65535) {
        frame.append(static_cast<char>(0x80 | 126));
        frame.append(static_cast<char>((len >> 8) & 0xff));
        frame.append(static_cast<char>(len & 0xff));
    } else {
        frame.append(static_cast<char>(0x80 | 127));
        const quint64 n = static_cast<quint64>(len);
        for (int i = 7; i >= 0; --i)
            frame.append(static_cast<char>((n >> (8 * i)) & 0xff));
    }
    const QByteArray mask = randomBytes(4);
    frame.append(mask);
    QByteArray masked = payload;
    for (int i = 0; i < masked.size(); ++i)
        masked[i] = static_cast<char>(static_cast<unsigned char>(masked[i]) ^ static_cast<unsigned char>(mask.at(i % 4)));
    frame.append(masked);
    m_socket->write(frame);
}

bool WebSocketClient::tryParseFrame() {
    if (m_phase == Phase::Header) {
        if (m_buffer.size() < 2)
            return false;
        const auto b0 = static_cast<quint8>(m_buffer.at(0));
        const auto b1 = static_cast<quint8>(m_buffer.at(1));
        if (b0 & 0x70) {
            fail(QStringLiteral("unsupported websocket extension"));
            return false;
        }
        m_fin = b0 & 0x80;
        m_opcode = b0 & 0x0f;
        m_masked = b1 & 0x80;
        quint64 len = b1 & 0x7f;
        int header = 2 + (m_masked ? 4 : 0);
        if (len == 126)
            header += 2;
        else if (len == 127)
            header += 8;
        if (m_buffer.size() < header)
            return false;
        int offset = 2;
        if ((b1 & 0x7f) == 126) {
            len = (static_cast<quint8>(m_buffer.at(2)) << 8) | static_cast<quint8>(m_buffer.at(3));
            offset = 4;
        } else if ((b1 & 0x7f) == 127) {
            len = 0;
            for (int i = 0; i < 8; ++i)
                len = (len << 8) | static_cast<quint8>(m_buffer.at(2 + i));
            offset = 10;
        }
        if (len > 2 * 1024 * 1024) {
            fail(QStringLiteral("frame too large"));
            return false;
        }
        if (m_masked) {
            m_mask = m_buffer.mid(offset, 4);
            offset += 4;
        } else {
            m_mask.clear();
        }
        m_buffer.remove(0, offset);
        m_need = len;
        m_payload.clear();
        m_phase = Phase::Payload;
    }
    if (m_buffer.size() < static_cast<qsizetype>(m_need))
        return false;
    m_payload = m_buffer.left(static_cast<qsizetype>(m_need));
    m_buffer.remove(0, static_cast<qsizetype>(m_need));
    if (m_masked) {
        for (int i = 0; i < m_payload.size(); ++i) {
            m_payload[i] = static_cast<char>(static_cast<unsigned char>(m_payload.at(i))
                ^ static_cast<unsigned char>(m_mask.at(i % 4)));
        }
    }
    const bool fin = m_fin;
    const int opcode = m_opcode;
    const QByteArray payload = m_payload;
    resetParser();
    handleFrame(fin, opcode, payload);
    return m_state == State::Open;
}

void WebSocketClient::handleFrame(bool fin, int opcode, const QByteArray& payload) {
    m_lastInbound.start();
    if (opcode == 0x9) {
        sendFrame(0xA, payload);
        return;
    }
    if (opcode == 0xA)
        return;
    if (opcode == 0x8) {
        m_state = State::Idle;
        sendFrame(0x8, QByteArray());
        if (m_socket)
            m_socket->disconnectFromHost();
        return;
    }
    if (m_message.size() + payload.size() > 2 * 1024 * 1024) {
        fail(QStringLiteral("message too large"));
        return;
    }
    if (opcode == 0x1 || opcode == 0x2) {
        m_messageOpcode = opcode;
        m_message = payload;
    } else if (opcode == 0x0) {
        m_message += payload;
    } else {
        fail(QStringLiteral("bad websocket opcode"));
        return;
    }
    if (!fin)
        return;
    if (m_messageOpcode == 0x1)
        emit textMessageReceived(QString::fromUtf8(m_message));
    m_message.clear();
    m_messageOpcode = -1;
}

void WebSocketClient::resetParser() {
    m_phase = Phase::Header;
    m_need = 0;
    m_payload.clear();
    m_mask.clear();
    m_fin = false;
    m_masked = false;
    m_opcode = 0;
}
