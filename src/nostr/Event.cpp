#include "nostr/Event.h"

#include "util/Bytes.h"

#include <QCryptographicHash>
#include <QJsonArray>

namespace {

QByteArray jsonEscape(const QString& text) {
    const QByteArray utf8 = text.toUtf8();
    QByteArray out;
    out.reserve(utf8.size());
    for (qsizetype i = 0; i < utf8.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(utf8.at(i));
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out += static_cast<char>(c); break;
        }
    }
    return out;
}

QByteArray quote(const QString& text) {
    QByteArray out;
    out += '"';
    out += jsonEscape(text);
    out += '"';
    return out;
}

bool isHex64(const QString& text) {
    if (text.size() != 64)
        return false;
    for (const QChar c : text) {
        const bool digit = c.isDigit();
        const bool alpha = c >= QLatin1Char('a') && c <= QLatin1Char('f');
        if (!digit && !alpha)
            return false;
    }
    return true;
}

}

QByteArray NostrEvent::canonical() const {
    QByteArray out;
    out += "[0,";
    out += quote(pubkey);
    out += ',';
    out += QByteArray::number(createdAt);
    out += ',';
    out += QByteArray::number(kind);
    out += ",[";
    for (int i = 0; i < tags.size(); ++i) {
        if (i)
            out += ',';
        out += '[';
        const QStringList& tag = tags.at(i);
        for (int j = 0; j < tag.size(); ++j) {
            if (j)
                out += ',';
            out += quote(tag.at(j));
        }
        out += ']';
    }
    out += "],";
    out += quote(content);
    out += ']';
    return out;
}

QByteArray NostrEvent::toWire() const {
    QByteArray out;
    out += "{\"id\":";
    out += quote(id);
    out += ",\"pubkey\":";
    out += quote(pubkey);
    out += ",\"created_at\":";
    out += QByteArray::number(createdAt);
    out += ",\"kind\":";
    out += QByteArray::number(kind);
    out += ",\"tags\":[";
    for (int i = 0; i < tags.size(); ++i) {
        if (i)
            out += ',';
        out += '[';
        const QStringList& tag = tags.at(i);
        for (int j = 0; j < tag.size(); ++j) {
            if (j)
                out += ',';
            out += quote(tag.at(j));
        }
        out += ']';
    }
    out += "],\"content\":";
    out += quote(content);
    out += ",\"sig\":";
    out += quote(sig);
    out += '}';
    return out;
}

bool NostrEvent::computeId() {
    const QByteArray hash = QCryptographicHash::hash(canonical(), QCryptographicHash::Sha256);
    id = toHex(hash);
    return id.size() == 64;
}

bool NostrEvent::sign(const Keys& keys) {
    pubkey = keys.pubkeyHex();
    if (!computeId())
        return false;
    const auto hash = fromHex(id);
    if (!hash)
        return false;
    const QByteArray signature = keys.sign(*hash);
    if (signature.size() != 64)
        return false;
    sig = toHex(signature);
    return verify();
}

bool NostrEvent::verify(QString* reason) const {
    if (!isHex64(id) || !isHex64(pubkey) || sig.size() != 128) {
        if (reason)
            *reason = QStringLiteral("malformed event");
        return false;
    }
    NostrEvent copy = *this;
    if (!copy.computeId() || copy.id != id) {
        if (reason)
            *reason = QStringLiteral("id mismatch");
        return false;
    }
    const auto pub = fromHex(pubkey);
    const auto hash = fromHex(id);
    const auto signature = fromHex(sig);
    if (!pub || !hash || !signature || !Keys::verify(*pub, *hash, *signature)) {
        if (reason)
            *reason = QStringLiteral("bad signature");
        return false;
    }
    return true;
}

std::optional<NostrEvent> NostrEvent::fromJson(const QJsonObject& object) {
    NostrEvent event;
    event.id = object.value(QStringLiteral("id")).toString().toLower();
    event.pubkey = object.value(QStringLiteral("pubkey")).toString().toLower();
    const QJsonValue created = object.value(QStringLiteral("created_at"));
    event.createdAt = created.isString() ? created.toString().toLongLong() : created.toInteger();
    event.kind = object.value(QStringLiteral("kind")).toInt(-1);
    event.content = object.value(QStringLiteral("content")).toString();
    event.sig = object.value(QStringLiteral("sig")).toString().toLower();
    const QJsonArray tags = object.value(QStringLiteral("tags")).toArray();
    for (const QJsonValue& tagValue : tags) {
        if (!tagValue.isArray())
            continue;
        QStringList tag;
        for (const QJsonValue& item : tagValue.toArray()) {
            if (!item.isString())
                return std::nullopt;
            tag.append(item.toString());
        }
        if (!tag.isEmpty())
            event.tags.append(tag);
    }
    if (event.kind < 0)
        return std::nullopt;
    return event;
}
