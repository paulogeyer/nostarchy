#include "crypto/Keys.h"

#include "crypto/Bech32.h"
#include "util/Bytes.h"

#include <secp256k1.h>
#include <secp256k1_extrakeys.h>
#include <secp256k1_schnorrsig.h>

namespace {

secp256k1_context* context() {
    static secp256k1_context* ctx = [] {
        secp256k1_context* created = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
        const QByteArray seed = randomBytes(32);
        if (!secp256k1_context_randomize(created, reinterpret_cast<const unsigned char*>(seed.constData())))
            return created;
        return created;
    }();
    return ctx;
}

}

std::optional<Keys> Keys::generate() {
    for (int i = 0; i < 32; ++i) {
        const QByteArray secret = randomBytes(32);
        if (auto keys = fromSecret(secret))
            return keys;
    }
    return std::nullopt;
}

std::optional<Keys> Keys::fromSecret(const QByteArray& secret) {
    if (secret.size() != 32)
        return std::nullopt;
    const auto* sec = reinterpret_cast<const unsigned char*>(secret.constData());
    if (!secp256k1_ec_seckey_verify(context(), sec))
        return std::nullopt;
    secp256k1_keypair keypair;
    if (!secp256k1_keypair_create(context(), &keypair, sec))
        return std::nullopt;
    secp256k1_xonly_pubkey xonly;
    if (!secp256k1_keypair_xonly_pub(context(), &xonly, nullptr, &keypair))
        return std::nullopt;
    unsigned char pub[32];
    if (!secp256k1_xonly_pubkey_serialize(context(), pub, &xonly))
        return std::nullopt;
    return Keys(secret, QByteArray(reinterpret_cast<const char*>(pub), 32));
}

std::optional<Keys> Keys::fromSecretHex(const QString& hex) {
    const auto bytes = fromHex(hex);
    if (!bytes)
        return std::nullopt;
    return fromSecret(*bytes);
}

std::optional<Keys> Keys::fromBech32(const QString& text, QString* error) {
    const QString trimmed = text.trimmed();
    if (trimmed.startsWith(QStringLiteral("npub"), Qt::CaseInsensitive)) {
        if (error)
            *error = QStringLiteral("that is a public key, not a secret");
        return std::nullopt;
    }
    if (trimmed.size() == 64) {
        auto keys = fromSecretHex(trimmed);
        if (!keys && error)
            *error = QStringLiteral("invalid secret key");
        return keys;
    }
    const auto decoded = bech32::decode(trimmed);
    if (!decoded || decoded->first != QStringLiteral("nsec") || decoded->second.size() != 32) {
        if (error)
            *error = QStringLiteral("invalid nsec");
        return std::nullopt;
    }
    auto keys = fromSecret(decoded->second);
    if (!keys && error)
        *error = QStringLiteral("invalid secret key");
    return keys;
}

Keys::Keys(QByteArray secret, QByteArray pubkey)
    : m_secret(std::move(secret))
    , m_pubkey(std::move(pubkey)) {}

QByteArray Keys::secret() const { return m_secret; }
QByteArray Keys::pubkey() const { return m_pubkey; }
QString Keys::secretHex() const { return toHex(m_secret); }
QString Keys::pubkeyHex() const { return toHex(m_pubkey); }

QString Keys::nsec() const {
    return bech32::encode(QStringLiteral("nsec"), m_secret).value_or(QString());
}

QString Keys::npub() const {
    return bech32::encode(QStringLiteral("npub"), m_pubkey).value_or(QString());
}

QString Keys::npubFromHex(const QString& pubkeyHex) {
    const auto bytes = fromHex(pubkeyHex);
    if (!bytes || bytes->size() != 32)
        return {};
    return bech32::encode(QStringLiteral("npub"), *bytes).value_or(QString());
}

QByteArray Keys::sign(const QByteArray& msg32) const {
    return sign(msg32, randomBytes(32));
}

QByteArray Keys::sign(const QByteArray& msg32, const QByteArray& aux32) const {
    if (msg32.size() != 32 || aux32.size() != 32)
        return {};
    secp256k1_keypair keypair;
    if (!secp256k1_keypair_create(context(), &keypair,
            reinterpret_cast<const unsigned char*>(m_secret.constData())))
        return {};
    unsigned char sig[64];
    if (!secp256k1_schnorrsig_sign32(context(), sig,
            reinterpret_cast<const unsigned char*>(msg32.constData()), &keypair,
            reinterpret_cast<const unsigned char*>(aux32.constData())))
        return {};
    return QByteArray(reinterpret_cast<const char*>(sig), 64);
}

bool Keys::verify(const QByteArray& pubkey32, const QByteArray& msg32, const QByteArray& sig64) {
    if (pubkey32.size() != 32 || msg32.size() != 32 || sig64.size() != 64)
        return false;
    secp256k1_xonly_pubkey xonly;
    if (!secp256k1_xonly_pubkey_parse(context(), &xonly,
            reinterpret_cast<const unsigned char*>(pubkey32.constData())))
        return false;
    return secp256k1_schnorrsig_verify(context(),
               reinterpret_cast<const unsigned char*>(sig64.constData()),
               reinterpret_cast<const unsigned char*>(msg32.constData()), 32, &xonly)
        == 1;
}
