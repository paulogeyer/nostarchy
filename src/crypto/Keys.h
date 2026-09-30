#pragma once

#include <QByteArray>
#include <QString>

#include <optional>

class Keys {
public:
    static std::optional<Keys> generate();
    static std::optional<Keys> fromSecret(const QByteArray& secret);
    static std::optional<Keys> fromSecretHex(const QString& hex);
    static std::optional<Keys> fromBech32(const QString& text, QString* error = nullptr);

    QByteArray secret() const;
    QByteArray pubkey() const;
    QString secretHex() const;
    QString pubkeyHex() const;
    QString nsec() const;
    QString npub() const;

    QByteArray sign(const QByteArray& msg32) const;
    QByteArray sign(const QByteArray& msg32, const QByteArray& aux32) const;

    static bool verify(const QByteArray& pubkey32, const QByteArray& msg32, const QByteArray& sig64);
    static QString npubFromHex(const QString& pubkeyHex);

private:
    Keys(QByteArray secret, QByteArray pubkey);

    QByteArray m_secret;
    QByteArray m_pubkey;
};
