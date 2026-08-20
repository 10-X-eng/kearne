#include "local_json_store.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>

namespace kearne::ui::local_json {

std::optional<QJsonObject> read(const QString &path, qsizetype maximumBytes,
                                QString &error) {
  error.clear();
  QFile file(path);
  if (!file.exists())
    return std::nullopt;
  if (!file.open(QIODevice::ReadOnly)) {
    error = QStringLiteral("Cannot read local state");
    return std::nullopt;
  }
  if (file.size() < 1 || file.size() > maximumBytes) {
    error = QStringLiteral("Local state exceeds the size limit");
    return std::nullopt;
  }
  QJsonParseError parseError;
  const QJsonDocument document =
      QJsonDocument::fromJson(file.readAll(), &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    error = QStringLiteral("Local state is not valid JSON");
    return std::nullopt;
  }
  return document.object();
}

bool write(const QString &path, const QJsonObject &document, QString &error) {
  QDir directory;
  const QString parent = QFileInfo(path).absolutePath();
  if (!directory.mkpath(parent)) {
    error = QStringLiteral("Cannot create the local state directory");
    return false;
  }
  QFile::setPermissions(parent, QFileDevice::ReadOwner |
                                    QFileDevice::WriteOwner |
                                    QFileDevice::ExeOwner);
  const QByteArray bytes =
      QJsonDocument(document).toJson(QJsonDocument::Indented);
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() ||
      !file.commit()) {
    error = QStringLiteral("Cannot write local state atomically");
    return false;
  }
  QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
  error.clear();
  return true;
}

} // namespace kearne::ui::local_json
