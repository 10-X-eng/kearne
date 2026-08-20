#include "theme_manager.hpp"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStyleHints>

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace kearne::ui {
namespace {

constexpr qsizetype maximumThemeBytes = 256 * 1024;

enum class TokenKind { Color, Integer, Real, String };

struct TokenSpec {
  const char *name;
  TokenKind kind;
  double minimum = 0.0;
  double maximum = 0.0;
};

constexpr std::array tokenSpecs{
    TokenSpec{"surface", TokenKind::Color},
    TokenSpec{"surfaceRaised", TokenKind::Color},
    TokenSpec{"surfaceMuted", TokenKind::Color},
    TokenSpec{"canvas", TokenKind::Color},
    TokenSpec{"canvasGridMinor", TokenKind::Color},
    TokenSpec{"canvasGridMajor", TokenKind::Color},
    TokenSpec{"axisX", TokenKind::Color},
    TokenSpec{"axisY", TokenKind::Color},
    TokenSpec{"axisZ", TokenKind::Color},
    TokenSpec{"border", TokenKind::Color},
    TokenSpec{"borderStrong", TokenKind::Color},
    TokenSpec{"text", TokenKind::Color},
    TokenSpec{"textMuted", TokenKind::Color},
    TokenSpec{"textFaint", TokenKind::Color},
    TokenSpec{"accent", TokenKind::Color},
    TokenSpec{"accentHover", TokenKind::Color},
    TokenSpec{"accentSoft", TokenKind::Color},
    TokenSpec{"focus", TokenKind::Color},
    TokenSpec{"selection", TokenKind::Color},
    TokenSpec{"success", TokenKind::Color},
    TokenSpec{"warning", TokenKind::Color},
    TokenSpec{"error", TokenKind::Color},
    TokenSpec{"stale", TokenKind::Color},
    TokenSpec{"agent", TokenKind::Color},
    TokenSpec{"transparent", TokenKind::Color},
    TokenSpec{"space1", TokenKind::Integer, 0, 64},
    TokenSpec{"space2", TokenKind::Integer, 0, 64},
    TokenSpec{"space3", TokenKind::Integer, 0, 64},
    TokenSpec{"space4", TokenKind::Integer, 0, 64},
    TokenSpec{"space6", TokenKind::Integer, 0, 96},
    TokenSpec{"radiusSmall", TokenKind::Integer, 0, 64},
    TokenSpec{"radius", TokenKind::Integer, 0, 64},
    TokenSpec{"badgeRadius", TokenKind::Integer, 0, 64},
    TokenSpec{"separatorWidth", TokenKind::Integer, 1, 4},
    TokenSpec{"focusRingWidth", TokenKind::Integer, 1, 6},
    TokenSpec{"iconSizeSmall", TokenKind::Integer, 8, 64},
    TokenSpec{"iconSize", TokenKind::Integer, 8, 64},
    TokenSpec{"iconSizeLarge", TokenKind::Integer, 8, 96},
    TokenSpec{"iconStrokeWidth", TokenKind::Real, 0.5, 6.0},
    TokenSpec{"gridLineWidthMinor", TokenKind::Real, 0.1, 4.0},
    TokenSpec{"gridLineWidthMajor", TokenKind::Real, 0.1, 6.0},
    TokenSpec{"axisLineWidth", TokenKind::Real, 0.1, 8.0},
    TokenSpec{"disabledOpacity", TokenKind::Real, 0.2, 1.0},
    TokenSpec{"controlHeightCompact", TokenKind::Integer, 20, 80},
    TokenSpec{"controlHeight", TokenKind::Integer, 20, 96},
    TokenSpec{"viewportControlHeight", TokenKind::Integer, 20, 96},
    TokenSpec{"projectBarHeight", TokenKind::Integer, 28, 128},
    TokenSpec{"workspaceBarHeight", TokenKind::Integer, 24, 96},
    TokenSpec{"commandBarHeight", TokenKind::Integer, 32, 160},
    TokenSpec{"statusBarHeight", TokenKind::Integer, 20, 72},
    TokenSpec{"leftPanelWidth", TokenKind::Integer, 160, 720},
    TokenSpec{"rightPanelWidth", TokenKind::Integer, 180, 720},
    TokenSpec{"switchWidth", TokenKind::Integer, 24, 96},
    TokenSpec{"switchHeight", TokenKind::Integer, 16, 64},
    TokenSpec{"switchKnobSize", TokenKind::Integer, 10, 56},
    TokenSpec{"fontUiFamily", TokenKind::String},
    TokenSpec{"fontDataFamily", TokenKind::String},
    TokenSpec{"fontSmall", TokenKind::Integer, 8, 36},
    TokenSpec{"fontBody", TokenKind::Integer, 8, 48},
    TokenSpec{"fontTitle", TokenKind::Integer, 8, 56},
    TokenSpec{"fontDisplay", TokenKind::Integer, 10, 72},
    TokenSpec{"fontHeading", TokenKind::Integer, 12, 96},
    TokenSpec{"fontWeightNormal", TokenKind::Integer, 100, 900},
    TokenSpec{"fontWeightStrong", TokenKind::Integer, 100, 900},
    TokenSpec{"letterSpacing", TokenKind::Real, 0.0, 8.0},
};

const TokenSpec *findTokenSpec(const QString &name) {
  const auto found = std::find_if(
      tokenSpecs.cbegin(), tokenSpecs.cend(), [&name](const TokenSpec &spec) {
        return name == QString::fromLatin1(spec.name);
      });
  return found == tokenSpecs.cend() ? nullptr : &*found;
}

QString scalar(const YAML::Node &node, const QString &field) {
  if (!node || !node.IsScalar())
    throw std::runtime_error(
        (field + QStringLiteral(" must be a scalar")).toStdString());
  return QString::fromStdString(node.Scalar());
}

void validateMapKeys(const YAML::Node &node, const QString &field,
                     const QStringList &allowed = {}) {
  if (!node || !node.IsMap())
    throw std::runtime_error(
        (field + QStringLiteral(" must be a map")).toStdString());
  QStringList seen;
  for (const auto &entry : node) {
    const QString key = scalar(entry.first, field + QStringLiteral(" key"));
    if (seen.contains(key))
      throw std::runtime_error(
          (field + QStringLiteral(" contains duplicate key ") + key)
              .toStdString());
    if (!allowed.isEmpty() && !allowed.contains(key))
      throw std::runtime_error(
          (field + QStringLiteral(" contains unknown key ") + key)
              .toStdString());
    seen.push_back(key);
  }
}

QVariant parseToken(const TokenSpec &spec, const YAML::Node &node) {
  const QString name = QString::fromLatin1(spec.name);
  const QString text = scalar(node, QStringLiteral("token ") + name);
  if (spec.kind == TokenKind::Color) {
    const QColor color(text);
    if (!color.isValid())
      throw std::runtime_error(
          (name + QStringLiteral(" is not a valid color")).toStdString());
    if (name == QStringLiteral("transparent")) {
      if (color.alpha() != 0)
        throw std::runtime_error("transparent must have zero alpha");
    } else if (color.alpha() != 255) {
      throw std::runtime_error(
          (name + QStringLiteral(" must be opaque")).toStdString());
    }
    return color;
  }
  if (spec.kind == TokenKind::String) {
    if (text.size() > 128)
      throw std::runtime_error(
          (name + QStringLiteral(" is too long")).toStdString());
    return text;
  }
  bool converted = false;
  const double value = text.toDouble(&converted);
  if (!converted || !std::isfinite(value) || value < spec.minimum ||
      value > spec.maximum) {
    throw std::runtime_error(
        (name + QStringLiteral(" is outside its allowed range")).toStdString());
  }
  if (spec.kind == TokenKind::Integer) {
    bool integerConverted = false;
    const int integer = text.toInt(&integerConverted);
    if (!integerConverted)
      throw std::runtime_error(
          (name + QStringLiteral(" must be an integer")).toStdString());
    return integer;
  }
  return value;
}

double linearChannel(int channel) {
  const double value = static_cast<double>(channel) / 255.0;
  return value <= 0.04045 ? value / 12.92
                          : std::pow((value + 0.055) / 1.055, 2.4);
}

double luminance(const QColor &color) {
  return 0.2126 * linearChannel(color.red()) +
         0.7152 * linearChannel(color.green()) +
         0.0722 * linearChannel(color.blue());
}

double contrast(const QColor &first, const QColor &second) {
  const double firstLuminance = luminance(first);
  const double secondLuminance = luminance(second);
  return (std::max(firstLuminance, secondLuminance) + 0.05) /
         (std::min(firstLuminance, secondLuminance) + 0.05);
}

void validateContrast(const QVariantMap &tokens) {
  struct Pair {
    const char *foreground;
    const char *background;
    double minimum;
  };
  constexpr std::array pairs{
      Pair{"text", "surface", 4.5},      Pair{"textMuted", "surface", 4.5},
      Pair{"textFaint", "surface", 3.0}, Pair{"surface", "accent", 4.5},
      Pair{"text", "selection", 4.5},
  };
  for (const Pair &pair : pairs) {
    const QColor foreground =
        tokens.value(QString::fromLatin1(pair.foreground)).value<QColor>();
    const QColor background =
        tokens.value(QString::fromLatin1(pair.background)).value<QColor>();
    if (contrast(foreground, background) + 0.001 < pair.minimum) {
      throw std::runtime_error((QString::fromLatin1(pair.foreground) +
                                QStringLiteral(" on ") +
                                QString::fromLatin1(pair.background) +
                                QStringLiteral(" does not meet contrast ") +
                                QString::number(pair.minimum, 'f', 1))
                                   .toStdString());
    }
  }
}

QByteArray readThemeBytes(const QString &path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
    throw std::runtime_error(
        (QStringLiteral("cannot open ") + path).toStdString());
  if (file.size() < 1 || file.size() > maximumThemeBytes)
    throw std::runtime_error("theme file must contain 1 to 262144 bytes");
  return file.readAll();
}

QString userThemeDirectory() {
  return QDir(QStandardPaths::writableLocation(
                  QStandardPaths::AppConfigLocation))
      .filePath(QStringLiteral("themes"));
}

} // namespace

struct ThemeManager::ThemeDefinition {
  QString id;
  QString name;
  QString appearance;
  QVariantMap tokens;
  QString source;
  bool builtIn = false;
};

ThemeManager::ThemeManager(QObject *parent) : QObject(parent) {
  try {
    loadBuiltIn(QStringLiteral(":/kearne/themes/light.yml"));
    loadBuiltIn(QStringLiteral(":/kearne/themes/dark.yml"));
    loadUserThemes();
  } catch (const std::exception &error) {
    qFatal("Invalid built-in theme: %s", error.what());
  }

  selectionId_ = QStringLiteral("system");
  applySelection(false);
  connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
          [this] {
            if (selectionId_ == QStringLiteral("system"))
              applySelection(false);
          });
}

ThemeManager::~ThemeManager() = default;

qulonglong ThemeManager::revision() const { return revision_; }
QString ThemeManager::selectionId() const { return selectionId_; }
QString ThemeManager::activeThemeId() const { return activeThemeId_; }
QString ThemeManager::activeThemeName() const { return activeThemeName_; }
QString ThemeManager::appearance() const { return appearance_; }
QVariantMap ThemeManager::tokens() const { return activeTokens_; }
QString ThemeManager::lastError() const { return lastError_; }

std::vector<ThemeSummary> ThemeManager::summaries() const {
  std::vector<ThemeSummary> result{
      {QStringLiteral("system"), QStringLiteral("System")}};
  result.reserve(themes_.size() + 1);
  for (const ThemeDefinition &theme : themes_)
    result.push_back({theme.id, theme.name});
  return result;
}

QVariantList ThemeManager::qmlOptions() const {
  QVariantList result;
  const auto values = summaries();
  result.reserve(static_cast<qsizetype>(values.size()));
  for (const ThemeSummary &theme : values) {
    result.push_back(QVariantMap{{QStringLiteral("id"), theme.id},
                                 {QStringLiteral("label"), theme.name}});
  }
  return result;
}

QVariantList ThemeManager::tokenSchema() const {
  QVariantList result;
  result.reserve(static_cast<qsizetype>(tokenSpecs.size()));
  for (const TokenSpec &spec : tokenSpecs) {
    QString kind;
    switch (spec.kind) {
    case TokenKind::Color:
      kind = QStringLiteral("color");
      break;
    case TokenKind::Integer:
      kind = QStringLiteral("integer");
      break;
    case TokenKind::Real:
      kind = QStringLiteral("real");
      break;
    case TokenKind::String:
      kind = QStringLiteral("string");
      break;
    }
    QVariantMap descriptor{
        {QStringLiteral("name"), QString::fromLatin1(spec.name)},
        {QStringLiteral("kind"), kind}};
    if (spec.kind == TokenKind::Integer || spec.kind == TokenKind::Real) {
      descriptor.insert(QStringLiteral("minimum"), spec.minimum);
      descriptor.insert(QStringLiteral("maximum"), spec.maximum);
    }
    result.push_back(descriptor);
  }
  return result;
}

bool ThemeManager::validateThemeDocument(const QByteArray &document,
                                         QString *error) const {
  try {
    static_cast<void>(
        parseTheme(document, QStringLiteral("validation"), false));
    if (error)
      error->clear();
    return true;
  } catch (const std::exception &exception) {
    if (error)
      *error = QString::fromUtf8(exception.what());
    return false;
  }
}

ThemeManager::ThemeDefinition
ThemeManager::parseTheme(const QByteArray &document, const QString &source,
                         bool builtIn) const {
  YAML::Node root;
  try {
    root = YAML::Load(document.constData());
  } catch (const YAML::Exception &error) {
    throw std::runtime_error(std::string("invalid YAML: ") + error.what());
  }
  validateMapKeys(root, QStringLiteral("theme"),
                  {QStringLiteral("schema"), QStringLiteral("id"),
                   QStringLiteral("name"), QStringLiteral("appearance"),
                   QStringLiteral("extends"), QStringLiteral("tokens")});
  if (scalar(root["schema"], QStringLiteral("schema")) !=
      QStringLiteral("kearne.theme/v1"))
    throw std::runtime_error("schema must be kearne.theme/v1");

  ThemeDefinition theme;
  theme.id = scalar(root["id"], QStringLiteral("id"));
  theme.name = scalar(root["name"], QStringLiteral("name"));
  theme.source = source;
  theme.builtIn = builtIn;
  static const QRegularExpression validId(
      QStringLiteral("^[a-z0-9][a-z0-9.-]{0,63}$"));
  if (!validId.match(theme.id).hasMatch())
    throw std::runtime_error("id must match ^[a-z0-9][a-z0-9.-]{0,63}$");
  if (theme.name.trimmed().isEmpty() || theme.name.size() > 80)
    throw std::runtime_error("name must contain 1 to 80 characters");
  if (!builtIn && (theme.id == QStringLiteral("system") ||
                   theme.id == QStringLiteral("light") ||
                   theme.id == QStringLiteral("dark")))
    throw std::runtime_error("theme id is reserved");

  const QString baseId =
      root["extends"] ? scalar(root["extends"], QStringLiteral("extends"))
                      : QString{};
  if (!baseId.isEmpty()) {
    const auto base = std::find_if(themes_.cbegin(), themes_.cend(),
                                   [&baseId](const ThemeDefinition &candidate) {
                                     return candidate.id == baseId;
                                   });
    if (base == themes_.cend())
      throw std::runtime_error(
          (QStringLiteral("unknown base theme ") + baseId).toStdString());
    if (!builtIn && !base->builtIn)
      throw std::runtime_error(
          "imported themes may extend only built-in themes");
    theme.tokens = base->tokens;
    theme.appearance = base->appearance;
  }
  if (root["appearance"])
    theme.appearance = scalar(root["appearance"], QStringLiteral("appearance"));
  if (theme.appearance != QStringLiteral("light") &&
      theme.appearance != QStringLiteral("dark"))
    throw std::runtime_error("appearance must be light or dark");

  const YAML::Node tokens = root["tokens"];
  validateMapKeys(tokens, QStringLiteral("tokens"));
  for (const auto &entry : tokens) {
    const QString name = scalar(entry.first, QStringLiteral("token key"));
    const TokenSpec *spec = findTokenSpec(name);
    if (!spec)
      throw std::runtime_error(
          (QStringLiteral("unknown token ") + name).toStdString());
    theme.tokens.insert(name, parseToken(*spec, entry.second));
  }
  for (const TokenSpec &spec : tokenSpecs) {
    const QString name = QString::fromLatin1(spec.name);
    if (!theme.tokens.contains(name))
      throw std::runtime_error(
          (QStringLiteral("missing token ") + name).toStdString());
  }
  validateContrast(theme.tokens);
  return theme;
}

void ThemeManager::loadBuiltIn(const QString &resourcePath) {
  themes_.push_back(
      parseTheme(readThemeBytes(resourcePath), resourcePath, true));
}

void ThemeManager::loadUserThemes() {
  QDir directory(userThemeDirectory());
  if (!directory.exists())
    return;
  const QStringList files =
      directory.entryList({QStringLiteral("*.yml"), QStringLiteral("*.yaml")},
                          QDir::Files, QDir::Name);
  for (const QString &fileName : files) {
    const QString path = directory.filePath(fileName);
    try {
      ThemeDefinition theme = parseTheme(readThemeBytes(path), path, false);
      const auto existing =
          std::find_if(themes_.begin(), themes_.end(),
                       [&theme](const ThemeDefinition &candidate) {
                         return candidate.id == theme.id;
                       });
      if (existing == themes_.end())
        themes_.push_back(std::move(theme));
      else if (!existing->builtIn)
        *existing = std::move(theme);
    } catch (const std::exception &error) {
      lastError_ =
          QStringLiteral("%1: %2").arg(path, QString::fromUtf8(error.what()));
    }
  }
}

void ThemeManager::applySelection(bool didChangeSelection) {
  QString resolvedId = selectionId_;
  if (resolvedId == QStringLiteral("system")) {
    resolvedId =
        QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark
            ? QStringLiteral("dark")
            : QStringLiteral("light");
  }
  const auto theme =
      std::find_if(themes_.cbegin(), themes_.cend(),
                   [&resolvedId](const ThemeDefinition &candidate) {
                     return candidate.id == resolvedId;
                   });
  if (theme == themes_.cend())
    qFatal("Selected theme disappeared from the catalog");
  activeThemeId_ = theme->id;
  activeThemeName_ = theme->name;
  appearance_ = theme->appearance;
  activeTokens_ = theme->tokens;
  QFont applicationFont = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
  const QString family =
      activeTokens_.value(QStringLiteral("fontUiFamily")).toString();
  if (!family.isEmpty())
    applicationFont.setFamily(family);
  applicationFont.setPixelSize(
      activeTokens_.value(QStringLiteral("fontBody")).toInt());
  QGuiApplication::setFont(applicationFont);
  lastError_.clear();
  ++revision_;
  emit projectionChanged();
  if (didChangeSelection)
    emit selectionChanged(selectionId_);
}

bool ThemeManager::selectTheme(const QString &themeId) {
  if (themeId != QStringLiteral("system") &&
      std::none_of(
          themes_.cbegin(), themes_.cend(),
          [&themeId](const auto &theme) { return theme.id == themeId; })) {
    setError(QStringLiteral("Unknown theme: ") + themeId);
    return false;
  }
  if (selectionId_ == themeId)
    return true;
  selectionId_ = themeId;
  applySelection(true);
  return true;
}

bool ThemeManager::importTheme(const QUrl &source) {
  try {
    if (!source.isLocalFile())
      throw std::runtime_error("theme import requires a local file");
    const QString sourcePath = source.toLocalFile();
    const QByteArray document = readThemeBytes(sourcePath);
    ThemeDefinition theme = parseTheme(document, sourcePath, false);
    QDir directory;
    if (!directory.mkpath(userThemeDirectory()))
      throw std::runtime_error("cannot create the user theme directory");
    const QString destination =
        QDir(userThemeDirectory()).filePath(theme.id + QStringLiteral(".yml"));
    QSaveFile file(destination);
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(document) != document.size() || !file.commit())
      throw std::runtime_error("cannot persist the imported theme");
    QFile::setPermissions(destination,
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner);

    const QString importedId = theme.id;
    theme.source = destination;
    const auto existing =
        std::find_if(themes_.begin(), themes_.end(),
                     [&importedId](const ThemeDefinition &candidate) {
                       return candidate.id == importedId;
                     });
    if (existing == themes_.end())
      themes_.push_back(std::move(theme));
    else
      *existing = std::move(theme);
    const bool changed = selectionId_ != importedId;
    selectionId_ = importedId;
    applySelection(changed);
    if (!changed)
      emit selectionChanged(selectionId_);
    return true;
  } catch (const std::exception &error) {
    setError(QString::fromUtf8(error.what()));
    return false;
  }
}

bool ThemeManager::importThemePath(const QString &sourcePath) {
  const QString normalized = QDir::fromNativeSeparators(sourcePath.trimmed());
  if (!QDir::isAbsolutePath(normalized)) {
    setError(QStringLiteral("Theme path must be absolute"));
    return false;
  }
  return importTheme(QUrl::fromLocalFile(QDir::cleanPath(normalized)));
}

void ThemeManager::clearError() {
  if (lastError_.isEmpty())
    return;
  lastError_.clear();
  ++revision_;
  emit projectionChanged();
}

void ThemeManager::setError(const QString &message) {
  lastError_ = message;
  ++revision_;
  emit projectionChanged();
}

} // namespace kearne::ui
