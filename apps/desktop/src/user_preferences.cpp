#include "user_preferences.hpp"

#include "local_json_store.hpp"

#include <QDir>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>

#include <utility>

namespace kearne::ui {
namespace {

constexpr qsizetype maximumPreferenceBytes = 64 * 1024;

QString defaultPath() {
  return QDir(QStandardPaths::writableLocation(
                  QStandardPaths::AppConfigLocation))
      .filePath(QStringLiteral("preferences.json"));
}

} // namespace

UserPreferences::UserPreferences(QString path)
    : path_(path.isEmpty() ? defaultPath() : std::move(path)),
      values_{
          {QStringLiteral("theme"), QStringLiteral("system")},
          {QStringLiteral("default-length-unit"), QStringLiteral("mm")},
          {QStringLiteral("interface-density"), QStringLiteral("compact")},
          {QStringLiteral("navigation-profile"), QStringLiteral("solidworks")},
          {QStringLiteral("zoom-direction"), QStringLiteral("standard")}} {
  load();
}

QVariant UserPreferences::value(const QString &preferenceId) const {
  return values_.value(preferenceId);
}

QString UserPreferences::lastError() const { return lastError_; }
QString UserPreferences::path() const { return path_; }

bool UserPreferences::validate(const QString &preferenceId,
                               const QVariant &value, QString &error) const {
  if (value.metaType().id() != QMetaType::QString) {
    error = preferenceId + QStringLiteral(" must be a string");
    return false;
  }
  const QString text = value.toString();
  if (preferenceId == QStringLiteral("theme")) {
    static const QRegularExpression validTheme(
        QStringLiteral("^(system|[a-z0-9][a-z0-9.-]{0,63})$"));
    if (validTheme.match(text).hasMatch())
      return true;
    error = QStringLiteral("theme has an invalid ID");
    return false;
  }
  if (preferenceId == QStringLiteral("default-length-unit")) {
    if (QStringList{QStringLiteral("mm"), QStringLiteral("cm"),
                    QStringLiteral("m"), QStringLiteral("in")}
            .contains(text))
      return true;
    error = QStringLiteral("default-length-unit is unsupported");
    return false;
  }
  if (preferenceId == QStringLiteral("interface-density")) {
    if (QStringList{QStringLiteral("compact"), QStringLiteral("comfortable")}
            .contains(text))
      return true;
    error = QStringLiteral("interface-density is unsupported");
    return false;
  }
  if (preferenceId == QStringLiteral("navigation-profile")) {
    if (QStringList{QStringLiteral("fusion"), QStringLiteral("solidworks"),
                    QStringLiteral("onshape")}
            .contains(text))
      return true;
    error = QStringLiteral("navigation-profile is unsupported");
    return false;
  }
  if (preferenceId == QStringLiteral("zoom-direction")) {
    if (QStringList{QStringLiteral("standard"), QStringLiteral("reversed")}
            .contains(text))
      return true;
    error = QStringLiteral("zoom-direction is unsupported");
    return false;
  }
  error = QStringLiteral("unknown user preference: ") + preferenceId;
  return false;
}

void UserPreferences::load() {
  const auto document =
      local_json::read(path_, maximumPreferenceBytes, lastError_);
  if (!document)
    return;
  const QJsonObject root = *document;
  for (auto field = root.constBegin(); field != root.constEnd(); ++field) {
    if (field.key() != QStringLiteral("schema") &&
        field.key() != QStringLiteral("values")) {
      lastError_ = QStringLiteral("User preferences contain an unknown field");
      return;
    }
  }
  if (root.value(QStringLiteral("schema")).toString() !=
          QStringLiteral("kearne.user-preferences/v1") ||
      !root.value(QStringLiteral("values")).isObject()) {
    lastError_ = QStringLiteral("User preference schema is unsupported");
    return;
  }
  QVariantMap loaded = values_;
  const QJsonObject values = root.value(QStringLiteral("values")).toObject();
  for (auto field = values.constBegin(); field != values.constEnd(); ++field) {
    QString error;
    const QVariant candidate = field.value().toVariant();
    if (!validate(field.key(), candidate, error)) {
      lastError_ = error;
      return;
    }
    loaded.insert(field.key(), candidate);
  }
  values_ = std::move(loaded);
}

bool UserPreferences::setValue(const QString &preferenceId,
                               const QVariant &value) {
  QString error;
  if (!validate(preferenceId, value, error)) {
    lastError_ = error;
    return false;
  }
  if (values_.value(preferenceId) == value && lastError_.isEmpty())
    return true;
  const QVariant previous = values_.value(preferenceId);
  values_.insert(preferenceId, value);
  if (save()) {
    lastError_.clear();
    return true;
  }
  values_.insert(preferenceId, previous);
  return false;
}

bool UserPreferences::save() {
  const QJsonObject root{
      {QStringLiteral("schema"), QStringLiteral("kearne.user-preferences/v1")},
      {QStringLiteral("values"), QJsonObject::fromVariantMap(values_)},
  };
  return local_json::write(path_, root, lastError_);
}

} // namespace kearne::ui
