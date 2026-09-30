#pragma once

#include <QByteArray>
#include <QString>

#include <optional>
#include <utility>

namespace bech32 {

std::optional<QString> encode(const QString& hrp, const QByteArray& data);
std::optional<std::pair<QString, QByteArray>> decode(const QString& text);

}
