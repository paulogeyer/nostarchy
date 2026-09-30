#pragma once

#include <QByteArray>
#include <QObject>
#include <QSslSocket>
#include <QTimer>
#include <QUrl>
#include <QElapsedTimer>

class WebSocketClient : public QObject {
    Q_OBJECT
public:
    explicit WebSocketClient(QObject* parent = nullptr);

    void open(const QUrl& url);
    void close();
    void sendText(const QString& text);
    bool isOpen() const;
    bool isConnecting() const;

signals:
    void connected();
    void disconnected();
    void textMessageReceived(const QString& message);
    void errorOccurred(const QString& message);
    void redirected(const QUrl& url);
    void rejected(const QString& message);

private:
    void startHandshake();
    void onReadyRead();
    void onSocketError();
    void fail(const QString& message);
    void sendFrame(quint8 opcode, const QByteArray& payload);
    bool tryParseFrame();
    void handleFrame(bool fin, int opcode, const QByteArray& payload);
    void resetParser();

    QSslSocket* m_socket = nullptr;
    QTimer m_connectTimer;
    QTimer m_pingTimer;
    QElapsedTimer m_lastInbound;
    QUrl m_url;
    QByteArray m_acceptKey;
    QByteArray m_buffer;
    QByteArray m_message;
    QByteArray m_mask;
    QByteArray m_payload;
    bool m_tls = true;
    bool m_userClose = false;
    int m_redirects = 0;
    bool m_failed = false;
    bool m_fin = false;
    bool m_masked = false;
    int m_opcode = 0;
    int m_messageOpcode = -1;
    quint64 m_need = 0;
    enum class State { Idle, Connecting, Handshake, Open };
    State m_state = State::Idle;
    enum class Phase { Header, Payload };
    Phase m_phase = Phase::Header;
};
