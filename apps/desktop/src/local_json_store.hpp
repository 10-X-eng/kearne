#pragma once

#include <QJsonObject>
#include <QString>

#include <optional>

namespace kearne::ui::local_json {

[[nodiscard]] std::optional<QJsonObject>
read(const QString &path, qsizetype maximumBytes, QString &error);
[[nodiscard]] bool write(const QString &path, const QJsonObject &document,
                         QString &error);

} // namespace kearne::ui::local_json
