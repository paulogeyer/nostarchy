#pragma once

#include "crypto/Keys.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

struct NostrEvent {
    QString id;
    QString pubkey;
    qint64 createdAt = 0;
    int kind = 1;
    QVector<QStringList> tags;
    QString content;
    QString sig;

    QByteArray canonical() const;
    QByteArray toWire() const;
    bool computeId();
    bool sign(const Keys& keys);
    bool verify(QString* reason = nullptr) const;

    static std::optional<NostrEvent> fromJson(const QJsonObject& object);
};
