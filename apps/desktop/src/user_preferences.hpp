#pragma once

#include <QString>
#include <QVariant>
#include <QVariantMap>

namespace kearne::ui {

class UserPreferences final {
public:
  explicit UserPreferences(QString path = {});

  [[nodiscard]] QVariant value(const QString &preferenceId) const;
  [[nodiscard]] QString lastError() const;
  [[nodiscard]] QString path() const;
  [[nodiscard]] bool setValue(const QString &preferenceId,
                              const QVariant &value);

private:
  [[nodiscard]] bool validate(const QString &preferenceId,
                              const QVariant &value, QString &error) const;
  void load();
  [[nodiscard]] bool save();

  QString path_;
  QVariantMap values_;
  QString lastError_;
};

} // namespace kearne::ui
