#pragma once

#include <QByteArray>
#include <QRandomGenerator>
#include <QString>
#include <QVector>

#include <optional>

inline QByteArray randomBytes(int n) {
    if (n <= 0)
        return {};
    QVector<quint32> words((n + 3) / 4);
    QRandomGenerator::system()->fillRange(words.data(), words.size());
    return QByteArray(reinterpret_cast<const char*>(words.constData()), n);
}

inline QString toHex(const QByteArray& bytes) {
    return QString::fromLatin1(bytes.toHex());
}

inline std::optional<QByteArray> fromHex(QString hex) {
    hex = hex.trimmed().toLower();
    if (hex.startsWith(QStringLiteral("0x")))
        hex = hex.mid(2);
    if (hex.size() % 2 != 0)
        return std::nullopt;
    for (const QChar c : hex) {
        const bool digit = c.isDigit();
        const bool alpha = c >= QLatin1Char('a') && c <= QLatin1Char('f');
        if (!digit && !alpha)
            return std::nullopt;
    }
    const QByteArray out = QByteArray::fromHex(hex.toLatin1());
    if (out.size() != hex.size() / 2)
        return std::nullopt;
    return out;
}
