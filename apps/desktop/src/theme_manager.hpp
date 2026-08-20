#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <cstdint>
#include <vector>

namespace kearne::ui {

struct ThemeSummary {
  QString id;
  QString name;
};

class ThemeManager : public QObject {
  Q_OBJECT
  QML_NAMED_ELEMENT(ThemeManager)
  QML_UNCREATABLE("Available through App.themes")
  Q_PROPERTY(qulonglong revision READ revision NOTIFY projectionChanged)
  Q_PROPERTY(QString selectionId READ selectionId NOTIFY projectionChanged)
  Q_PROPERTY(QString activeThemeId READ activeThemeId NOTIFY projectionChanged)
  Q_PROPERTY(
      QString activeThemeName READ activeThemeName NOTIFY projectionChanged)
  Q_PROPERTY(QString appearance READ appearance NOTIFY projectionChanged)
  Q_PROPERTY(QVariantMap tokens READ tokens NOTIFY projectionChanged)
  Q_PROPERTY(QVariantList options READ qmlOptions NOTIFY projectionChanged)
  Q_PROPERTY(QString lastError READ lastError NOTIFY projectionChanged)

public:
  explicit ThemeManager(QObject *parent = nullptr);
  ~ThemeManager() override;

  [[nodiscard]] qulonglong revision() const;
  [[nodiscard]] QString selectionId() const;
  [[nodiscard]] QString activeThemeId() const;
  [[nodiscard]] QString activeThemeName() const;
  [[nodiscard]] QString appearance() const;
  [[nodiscard]] QVariantMap tokens() const;
  [[nodiscard]] QVariantList qmlOptions() const;
  [[nodiscard]] QString lastError() const;
  [[nodiscard]] std::vector<ThemeSummary> summaries() const;
  [[nodiscard]] QVariantList tokenSchema() const;
  [[nodiscard]] bool validateThemeDocument(const QByteArray &document,
                                           QString *error = nullptr) const;

  Q_INVOKABLE bool selectTheme(const QString &themeId);
  Q_INVOKABLE bool importTheme(const QUrl &source);
  Q_INVOKABLE bool importThemePath(const QString &sourcePath);
  Q_INVOKABLE void clearError();

signals:
  void projectionChanged();
  void selectionChanged(const QString &themeId);

private:
  struct ThemeDefinition;

  [[nodiscard]] ThemeDefinition parseTheme(const QByteArray &document,
                                           const QString &source,
                                           bool builtIn) const;
  void loadBuiltIn(const QString &resourcePath);
  void loadUserThemes();
  void applySelection(bool selectionChanged);
  void setError(const QString &message);

  std::vector<ThemeDefinition> themes_;
  QVariantMap activeTokens_;
  QString selectionId_;
  QString activeThemeId_;
  QString activeThemeName_;
  QString appearance_;
  QString lastError_;
  std::uint64_t revision_ = 0;
};

} // namespace kearne::ui
