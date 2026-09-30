#include "crypto/Bech32.h"

#include <QVector>

namespace bech32 {
namespace {

constexpr char kCharset[] = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";

int charsetIndex(QChar c) {
    const char latin = c.toLatin1();
    for (int i = 0; i < 32; ++i) {
        if (kCharset[i] == latin)
            return i;
    }
    return -1;
}

quint32 polymod(const QVector<quint8>& values) {
    static constexpr quint32 kGen[] = {
        0x3b6a57b2, 0x26508e6d, 0x1ea119fa, 0x3d4233dd, 0x2a1462b3};
    quint32 chk = 1;
    for (const quint8 v : values) {
        const quint32 b = chk >> 25;
        chk = ((chk & 0x1ffffff) << 5) ^ v;
        for (int i = 0; i < 5; ++i) {
            if ((b >> i) & 1)
                chk ^= kGen[i];
        }
    }
    return chk;
}

QVector<quint8> hrpExpand(const QString& hrp) {
    QVector<quint8> out;
    out.reserve(hrp.size() * 2 + 1);
    for (const QChar c : hrp)
        out.append(static_cast<quint8>(c.unicode() >> 5));
    out.append(0);
    for (const QChar c : hrp)
        out.append(static_cast<quint8>(c.unicode() & 31));
    return out;
}

QVector<quint8> createChecksum(const QString& hrp, const QVector<quint8>& data) {
    QVector<quint8> values = hrpExpand(hrp);
    values += data;
    values.append({0, 0, 0, 0, 0, 0});
    const quint32 pm = polymod(values) ^ 1;
    QVector<quint8> checksum;
    checksum.reserve(6);
    for (int i = 0; i < 6; ++i)
        checksum.append(static_cast<quint8>((pm >> (5 * (5 - i))) & 31));
    return checksum;
}

std::optional<QVector<quint8>> convertBits(const QVector<quint8>& data, int fromBits, int toBits, bool pad) {
    int acc = 0;
    int bits = 0;
    QVector<quint8> out;
    const int maxv = (1 << toBits) - 1;
    const int maxAcc = (1 << (fromBits + toBits - 1)) - 1;
    for (const quint8 value : data) {
        if (value >> fromBits)
            return std::nullopt;
        acc = ((acc << fromBits) | value) & maxAcc;
        bits += fromBits;
        while (bits >= toBits) {
            bits -= toBits;
            out.append(static_cast<quint8>((acc >> bits) & maxv));
        }
    }
    if (pad) {
        if (bits)
            out.append(static_cast<quint8>((acc << (toBits - bits)) & maxv));
    } else if (bits >= fromBits || ((acc << (toBits - bits)) & maxv)) {
        return std::nullopt;
    }
    return out;
}

QVector<quint8> bytesToValues(const QByteArray& data) {
    QVector<quint8> out;
    out.reserve(data.size());
    for (unsigned char c : data)
        out.append(c);
    return out;
}

QByteArray valuesToBytes(const QVector<quint8>& data) {
    QByteArray out;
    out.resize(data.size());
    for (int i = 0; i < data.size(); ++i)
        out[i] = static_cast<char>(data.at(i));
    return out;
}

}

std::optional<QString> encode(const QString& hrp, const QByteArray& data) {
    if (hrp.isEmpty() || hrp.size() > 83)
        return std::nullopt;
    for (const QChar c : hrp) {
        const ushort u = c.unicode();
        if (u < 33 || u > 126)
            return std::nullopt;
    }
    const auto converted = convertBits(bytesToValues(data), 8, 5, true);
    if (!converted)
        return std::nullopt;
    const QString lower = hrp.toLower();
    const QVector<quint8> checksum = createChecksum(lower, *converted);
    QString out = lower + QLatin1Char('1');
    for (const quint8 v : *converted)
        out.append(QLatin1Char(kCharset[v]));
    for (const quint8 v : checksum)
        out.append(QLatin1Char(kCharset[v]));
    return out;
}

std::optional<std::pair<QString, QByteArray>> decode(const QString& text) {
    if (text.size() < 8 || text.size() > 90)
        return std::nullopt;
    bool hasLower = false;
    bool hasUpper = false;
    for (const QChar c : text) {
        if (c.isLower())
            hasLower = true;
        if (c.isUpper())
            hasUpper = true;
        const ushort u = c.unicode();
        if (u < 33 || u > 126)
            return std::nullopt;
    }
    if (hasLower && hasUpper)
        return std::nullopt;
    const QString lower = text.toLower();
    const int sep = lower.lastIndexOf(QLatin1Char('1'));
    if (sep < 1 || sep + 7 > lower.size())
        return std::nullopt;
    const QString hrp = lower.left(sep);
    QVector<quint8> data;
    data.reserve(lower.size() - sep - 1);
    for (int i = sep + 1; i < lower.size(); ++i) {
        const int idx = charsetIndex(lower.at(i));
        if (idx < 0)
            return std::nullopt;
        data.append(static_cast<quint8>(idx));
    }
    QVector<quint8> values = hrpExpand(hrp);
    values += data;
    if (polymod(values) != 1)
        return std::nullopt;
    data.resize(data.size() - 6);
    const auto converted = convertBits(data, 5, 8, false);
    if (!converted)
        return std::nullopt;
    return std::make_pair(hrp, valuesToBytes(*converted));
}

}
