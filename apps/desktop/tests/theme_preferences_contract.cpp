#include "theme_manager.hpp"
#include "user_preferences.hpp"
#include "viewport_camera.hpp"
#include "workspace_state.hpp"

#include <QColor>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QTemporaryDir>

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

void writeFile(const QString &path, const QByteArray &bytes) {
  QSaveFile file(path);
  require(file.open(QIODevice::WriteOnly), "could not open test file");
  require(file.write(bytes) == bytes.size(), "could not write test file");
  require(file.commit(), "could not commit test file");
}

QString yamlScalar(const QVariant &value) {
  if (value.metaType().id() == QMetaType::QColor) {
    const QColor color = value.value<QColor>();
    return QStringLiteral("\"") +
           color.name(color.alpha() == 255 ? QColor::HexRgb : QColor::HexArgb) +
           QStringLiteral("\"");
  }
  if (value.metaType().id() == QMetaType::QString) {
    QString escaped = value.toString();
    escaped.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
    escaped.replace(QStringLiteral("\""), QStringLiteral("\\\""));
    return QStringLiteral("\"") + escaped + QStringLiteral("\"");
  }
  return value.toString();
}

QByteArray themeDocument(const QString &id, const QString &base,
                         const QString &token, const QString &value) {
  return QStringLiteral("schema: kearne.theme/v1\n"
                        "id: %1\n"
                        "name: Contract theme\n"
                        "appearance: light\n"
                        "extends: %2\n"
                        "tokens:\n"
                        "  %3: %4\n")
      .arg(id, base, token, value)
      .toUtf8();
}

void verifyThemeSchema(kearne::ui::ThemeManager &themes) {
  require(themes.selectTheme(QStringLiteral("light")),
          "light theme unavailable");
  const QVariantMap light = themes.tokens();
  const QVariantList schema = themes.tokenSchema();
  require(schema.size() == light.size(), "theme schema and token set diverged");
  int index = 0;
  for (const QVariant &entry : schema) {
    const QVariantMap descriptor = entry.toMap();
    const QString name = descriptor.value(QStringLiteral("name")).toString();
    require(light.contains(name), "schema token missing from light theme");
    const QByteArray valid = themeDocument(
        QStringLiteral("contract.%1").arg(index), QStringLiteral("light"), name,
        yamlScalar(light.value(name)));
    QString error;
    require(themes.validateThemeDocument(valid, &error),
            "valid schema-derived theme override was rejected");
    const QByteArray wrongType =
        themeDocument(QStringLiteral("contract.invalid.%1").arg(index),
                      QStringLiteral("light"), name, QStringLiteral("[]"));
    require(!themes.validateThemeDocument(wrongType, &error),
            "wrong token type was accepted");
    ++index;
  }

  QString error;
  require(!themes.validateThemeDocument(
              themeDocument(QStringLiteral("contract.unknown"),
                            QStringLiteral("light"),
                            QStringLiteral("notAToken"), QStringLiteral("1")),
              &error),
          "unknown theme token was accepted");
  require(!themes.validateThemeDocument(
              QByteArrayLiteral(
                  "schema: kearne.theme/v1\nid: contract.empty\nname: Empty\n"
                  "appearance: light\ntokens: {}\n"),
              &error),
          "incomplete root theme was accepted");

  QRandomGenerator random(0x4b454152u);
  for (int sample = 0; sample < 256; ++sample) {
    QByteArray bytes;
    const int size = 1 + static_cast<int>(random.bounded(192u));
    bytes.reserve(size);
    for (int byte = 0; byte < size; ++byte)
      bytes.push_back(static_cast<char>(32u + random.bounded(95u)));
    static_cast<void>(themes.validateThemeDocument(bytes, &error));
  }

  const QVariantMap lightTokens = themes.tokens();
  require(themes.selectTheme(QStringLiteral("dark")), "dark theme unavailable");
  require(themes.tokens() != lightTokens, "light and dark token sets match");
}

void verifyThemeImport(kearne::ui::ThemeManager &themes,
                       const QString &profile) {
  const QString source = profile + QStringLiteral("/custom.yml");
  writeFile(source,
            themeDocument(QStringLiteral("user.contract"),
                          QStringLiteral("dark"), QStringLiteral("accent"),
                          QStringLiteral("\"#43a7c7\"")));
  require(themes.importTheme(QUrl::fromLocalFile(source)),
          "valid user theme import failed");
  require(themes.importThemePath(source), "valid theme path import failed");
  require(!themes.importThemePath(QStringLiteral("relative-theme.yml")),
          "relative theme path was accepted");
  require(themes.selectionId() == QStringLiteral("user.contract"),
          "imported theme was not selected");

  kearne::ui::ThemeManager restarted;
  require(restarted.selectTheme(QStringLiteral("user.contract")),
          "imported theme did not survive restart");
}

void verifyPreferences(const QString &profile) {
  const QString path = profile + QStringLiteral("/preferences.json");
  kearne::ui::UserPreferences preferences(path);
  require(preferences.value(QStringLiteral("theme")) ==
              QStringLiteral("system"),
          "theme default changed");
  require(preferences.value(QStringLiteral("default-length-unit")) ==
              QStringLiteral("mm"),
          "unit default changed");
  require(preferences.value(QStringLiteral("interface-density")) ==
              QStringLiteral("compact"),
          "interface density default changed");
  require(preferences.value(QStringLiteral("navigation-profile")) ==
              QStringLiteral("solidworks"),
          "navigation profile default changed");
  require(preferences.value(QStringLiteral("zoom-direction")) ==
              QStringLiteral("standard"),
          "zoom direction default changed");
  require(preferences.setValue(QStringLiteral("theme"),
                               QStringLiteral("user.contract")),
          "theme preference did not persist");
  require(preferences.setValue(QStringLiteral("default-length-unit"),
                               QStringLiteral("in")),
          "unit preference did not persist");
  require(preferences.setValue(QStringLiteral("interface-density"),
                               QStringLiteral("comfortable")),
          "interface density did not persist");
  require(preferences.setValue(QStringLiteral("navigation-profile"),
                               QStringLiteral("solidworks")),
          "navigation profile did not persist");
  require(preferences.setValue(QStringLiteral("zoom-direction"),
                               QStringLiteral("reversed")),
          "zoom direction did not persist");
  require(!preferences.setValue(QStringLiteral("unknown"), QStringLiteral("x")),
          "unknown preference was accepted");
  require(!preferences.setValue(QStringLiteral("navigation-profile"),
                                QStringLiteral("unknown")),
          "unknown navigation profile was accepted");
  require(!preferences.setValue(QStringLiteral("zoom-direction"),
                                QStringLiteral("sideways")),
          "unknown zoom direction was accepted");
  require(!preferences.setValue(QStringLiteral("interface-density"),
                                QStringLiteral("sprawling")),
          "unknown interface density was accepted");

  kearne::ui::UserPreferences restarted(path);
  require(restarted.value(QStringLiteral("theme")) ==
              QStringLiteral("user.contract"),
          "theme preference did not survive restart");
  require(restarted.value(QStringLiteral("default-length-unit")) ==
              QStringLiteral("in"),
          "unit preference did not survive restart");
  require(restarted.value(QStringLiteral("interface-density")) ==
              QStringLiteral("comfortable"),
          "interface density did not survive restart");
  require(restarted.value(QStringLiteral("navigation-profile")) ==
              QStringLiteral("solidworks"),
          "navigation profile did not survive restart");
  require(restarted.value(QStringLiteral("zoom-direction")) ==
              QStringLiteral("reversed"),
          "zoom direction did not survive restart");
  const auto exposed = QFileDevice::ReadGroup | QFileDevice::WriteGroup |
                       QFileDevice::ExeGroup | QFileDevice::ReadOther |
                       QFileDevice::WriteOther | QFileDevice::ExeOther;
  require((QFileInfo(path).permissions() & exposed) ==
              QFileDevice::Permissions{},
          "preference file permissions are too broad");

  const QList<QByteArray> malformed{
      QByteArray{},
      QByteArrayLiteral("{}"),
      QByteArrayLiteral(
          "{\"schema\":\"kearne.user-preferences/v2\",\"values\":{}}"),
      QByteArrayLiteral(
          "{\"schema\":\"kearne.user-preferences/v1\",\"values\":{\"bad\":1}}"),
      QByteArrayLiteral("{\"schema\":\"kearne.user-preferences/"
                        "v1\",\"values\":{\"theme\":1}}"),
  };
  int index = 0;
  for (const QByteArray &bytes : malformed) {
    const QString malformedPath =
        profile + QStringLiteral("/malformed-%1.json").arg(index++);
    writeFile(malformedPath, bytes);
    kearne::ui::UserPreferences invalid(malformedPath);
    require(!invalid.lastError().isEmpty(),
            "malformed preference document had no diagnostic");
    require(invalid.value(QStringLiteral("theme")) == QStringLiteral("system"),
            "malformed preference document changed defaults");
  }

  const QString blockingPath = profile + QStringLiteral("/not-a-directory");
  writeFile(blockingPath, QByteArrayLiteral("block"));
  kearne::ui::UserPreferences blocked(blockingPath +
                                      QStringLiteral("/preferences.json"));
  require(!blocked.setValue(QStringLiteral("theme"), QStringLiteral("dark")),
          "preference write fault was not reported");
}

void verifyViewportCamera() {
  using kearne::ui::ViewportCamera;
  ViewportCamera camera;
  const int middle = static_cast<int>(Qt::MiddleButton);
  const int right = static_cast<int>(Qt::RightButton);
  const int shift = static_cast<int>(Qt::ShiftModifier);
  const int control = static_cast<int>(Qt::ControlModifier);

  camera.setNavigationProfile(QStringLiteral("fusion"));
  camera.fit();
  require(camera.applyPointerDrag(middle, 0, 10, 5) && camera.panX() == 10,
          "Fusion pan mapping failed");
  const qreal fusionYaw = camera.yaw();
  require(camera.applyPointerDrag(middle, shift, 10, 5) &&
              camera.yaw() != fusionYaw,
          "Fusion orbit mapping failed");

  camera.setNavigationProfile(QStringLiteral("solidworks"));
  const qreal solidworksYaw = camera.yaw();
  require(camera.applyPointerDrag(middle, 0, 10, 5) &&
              camera.yaw() != solidworksYaw,
          "SolidWorks orbit mapping failed");
  const qreal solidworksPan = camera.panX();
  require(camera.applyPointerDrag(middle, control, 10, 5) &&
              camera.panX() != solidworksPan,
          "SolidWorks pan mapping failed");

  camera.setNavigationProfile(QStringLiteral("onshape"));
  const qreal onshapeYaw = camera.yaw();
  require(camera.applyPointerDrag(right, 0, 10, 5) &&
              camera.yaw() != onshapeYaw,
          "Onshape orbit mapping failed");
  require(!camera.applyPointerDrag(static_cast<int>(Qt::LeftButton), 0, 10, 5),
          "selection drag was consumed as navigation");

  QRandomGenerator random(0x56494557u);
  for (int operation = 0; operation < 10'000; ++operation) {
    switch (random.bounded(6u)) {
    case 0:
      camera.orbit(static_cast<qreal>(random.bounded(2001u)) - 1000.0,
                   static_cast<qreal>(random.bounded(2001u)) - 1000.0);
      break;
    case 1:
      camera.pan(static_cast<qreal>(random.bounded(2001u)) - 1000.0,
                 static_cast<qreal>(random.bounded(2001u)) - 1000.0);
      break;
    case 2:
      camera.zoom((static_cast<qreal>(random.bounded(2001u)) - 1000.0) / 100.0);
      break;
    case 3:
      camera.applySpaceMotion(
          (static_cast<qreal>(random.bounded(2001u)) - 1000.0) / 1000.0,
          (static_cast<qreal>(random.bounded(2001u)) - 1000.0) / 1000.0,
          (static_cast<qreal>(random.bounded(2001u)) - 1000.0) / 1000.0,
          (static_cast<qreal>(random.bounded(2001u)) - 1000.0) / 1000.0,
          (static_cast<qreal>(random.bounded(2001u)) - 1000.0) / 1000.0,
          (static_cast<qreal>(random.bounded(2001u)) - 1000.0) / 1000.0,
          static_cast<qreal>(1u + random.bounded(64u)));
      break;
    case 4:
      camera.applyWheel(static_cast<qreal>(random.bounded(2401u)) - 1200.0);
      break;
    default:
      require(camera.setView(
                  QStringList{QStringLiteral("front"), QStringLiteral("back"),
                              QStringLiteral("left"), QStringLiteral("right"),
                              QStringLiteral("top"), QStringLiteral("bottom"),
                              QStringLiteral("isometric")}
                      .at(static_cast<qsizetype>(random.bounded(7u)))),
              "declared camera preset was rejected");
      break;
    }
    require(std::isfinite(camera.yaw()) && std::isfinite(camera.pitch()) &&
                std::isfinite(camera.roll()) && std::isfinite(camera.panX()) &&
                std::isfinite(camera.panY()) &&
                std::isfinite(camera.distance()),
            "camera state became non-finite");
    require(camera.pitch() >= -90.0 && camera.pitch() <= 90.0 &&
                std::abs(camera.panX()) <= 1'000'000.0 &&
                std::abs(camera.panY()) <= 1'000'000.0 &&
                camera.distance() >= 36.0 && camera.distance() <= 1600.0,
            "camera state escaped its contract bounds");
  }
}

void verifyWorkspaceState(const QString &profile) {
  const QString path = profile + QStringLiteral("/workspace-state.json");
  {
    kearne::ui::WorkspaceState state(path);
    state.setPanelWidths(120, 900);
    state.setStructureVisible(false);
    state.setGridVisible(false);
    state.setDisplayMode(QStringLiteral("wireframe"));
    state.selectWorkspace(QStringLiteral("sketch"));
    state.setPanelWidths(320, 410);
    state.setInspectorVisible(false);
    state.setGridSnapEnabled(false);
    state.setDisplayMode(QStringLiteral("shaded"));
    require(state.flush(), "workspace state did not persist");
  }

  kearne::ui::WorkspaceState restarted(path);
  require(restarted.structureWidth() == 200 &&
              restarted.inspectorWidth() == 480 &&
              !restarted.structureVisible() && !restarted.gridVisible() &&
              restarted.displayMode() == QStringLiteral("wireframe"),
          "model workspace state did not survive restart");
  restarted.selectWorkspace(QStringLiteral("sketch"));
  require(restarted.structureWidth() == 320 &&
              restarted.inspectorWidth() == 410 &&
              !restarted.inspectorVisible() && !restarted.gridSnapEnabled() &&
              restarted.displayMode() == QStringLiteral("shaded"),
          "workspace-specific state did not survive restart");
  restarted.setDisplayMode(QStringLiteral("invalid"));
  require(restarted.displayMode() == QStringLiteral("shaded"),
          "invalid display mode changed workspace state");
  QFile persistedFile(path);
  require(persistedFile.open(QIODevice::ReadOnly),
          "persisted workspace state could not be read");
  const QJsonDocument persisted =
      QJsonDocument::fromJson(persistedFile.readAll());
  require(persisted.object().value(QStringLiteral("schema")).toString() ==
              QStringLiteral("kearne.workspace-state/v2"),
          "workspace state did not use the current schema");
  const auto exposed = QFileDevice::ReadGroup | QFileDevice::WriteGroup |
                       QFileDevice::ExeGroup | QFileDevice::ReadOther |
                       QFileDevice::WriteOther | QFileDevice::ExeOther;
  require((QFileInfo(path).permissions() & exposed) ==
              QFileDevice::Permissions{},
          "workspace state permissions are too broad");

  const QString legacyPath =
      profile + QStringLiteral("/workspace-state-v1.json");
  writeFile(
      legacyPath,
      QByteArrayLiteral("{\"schema\":\"kearne.workspace-state/v1\","
                        "\"workspaces\":{\"model\":{"
                        "\"structure_width\":256,\"inspector_width\":336,"
                        "\"structure_visible\":true,\"inspector_visible\":true,"
                        "\"grid_visible\":true,\"grid_snap_enabled\":true}}}"));
  kearne::ui::WorkspaceState legacy(legacyPath);
  require(legacy.lastError().isEmpty() &&
              legacy.displayMode() == QStringLiteral("shaded-edges") &&
              legacy.flush(),
          "workspace state v1 did not migrate with a safe display default");

  const QString malformedPath =
      profile + QStringLiteral("/workspace-state-malformed.json");
  writeFile(
      malformedPath,
      QByteArrayLiteral("{\"schema\":\"kearne.workspace-state/v1\","
                        "\"workspaces\":{\"model\":{\"structure_width\":1}}}"));
  kearne::ui::WorkspaceState malformed(malformedPath);
  require(!malformed.lastError().isEmpty(),
          "malformed workspace state had no diagnostic");
  require(malformed.structureWidth() == 256 &&
              malformed.inspectorWidth() == 336,
          "malformed workspace state changed defaults");
}

} // namespace

int main(int argc, char *argv[]) {
  try {
    QTemporaryDir profile;
    require(profile.isValid(), "could not create isolated test profile");
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("XDG_CONFIG_HOME", profile.path().toUtf8());
    qputenv("XDG_DATA_HOME",
            (profile.path() + QStringLiteral("/data")).toUtf8());
    QGuiApplication application(argc, argv);
    kearne::ui::ThemeManager themes;
    verifyThemeSchema(themes);
    verifyThemeImport(themes, profile.path());
    verifyPreferences(profile.path());
    verifyViewportCamera();
    verifyWorkspaceState(profile.path());
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
