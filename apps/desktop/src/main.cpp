#include "application_context.hpp"
#include "desktop_controller.hpp"
#include "local_sketch_session.hpp"
#include "navigation_device.hpp"
#include "observation_controller.hpp"
#include "sketch_camera_controller.hpp"
#include "sketch_scene_item.hpp"
#include "sketch_viewport_bridge.hpp"
#include "theme_manager.hpp"
#include "ui_session.hpp"
#include "user_preferences.hpp"
#include "viewport_camera.hpp"
#include "workspace_state.hpp"

#include <QAccessible>
#include <QCommandLineParser>
#include <QDir>
#include <QGuiApplication>
#include <QProcessEnvironment>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSurfaceFormat>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

std::vector<kearne::ui::UiOption>
themeOptions(const kearne::ui::ThemeManager &themes) {
  std::vector<kearne::ui::UiOption> result;
  const auto summaries = themes.summaries();
  result.reserve(summaries.size());
  for (const kearne::ui::ThemeSummary &theme : summaries)
    result.emplace_back(theme.id, theme.name);
  return result;
}

kearne::ui::LocalSketchSessionConfig localSketchSessionConfig() {
  QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
  const QDir applicationDirectory{QCoreApplication::applicationDirPath()};
  QString runtimeRoot = applicationDirectory.filePath(QStringLiteral("python"));
  if (!QDir{runtimeRoot}.exists())
    runtimeRoot = applicationDirectory.filePath(
        QStringLiteral("../share/kearne/python"));
  QStringList pythonPaths{
      QDir{runtimeRoot}.filePath(QStringLiteral("sdk")),
      QDir{runtimeRoot}.filePath(QStringLiteral("generated")),
  };
  environment.insert(QStringLiteral("PYTHONPATH"),
                     pythonPaths.join(QDir::listSeparator()));
  environment.insert(QStringLiteral("PYTHONNOUSERSITE"), QStringLiteral("1"));
  environment.insert(QStringLiteral("PYTHONDONTWRITEBYTECODE"),
                     QStringLiteral("1"));
  environment.remove(QStringLiteral("PYTHONHOME"));
  return {
      QStringLiteral(KEARNE_LOCAL_PYTHON),
      {QStringLiteral("-m"), QStringLiteral("kearne._worker")},
      std::move(environment),
      4U,
  };
}

} // namespace

int main(int argc, char *argv[]) {
  QGuiApplication::setOrganizationName(QStringLiteral("Kearne"));
  QGuiApplication::setApplicationName(QStringLiteral("Kearne"));
  QGuiApplication::setApplicationVersion(QStringLiteral(KEARNE_VERSION));
  if (!qEnvironmentVariableIsSet("QT_QUICK_CONTROLS_STYLE"))
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
  QSurfaceFormat graphicsFormat;
  graphicsFormat.setVersion(4, 3);
  graphicsFormat.setProfile(QSurfaceFormat::CoreProfile);
  QSurfaceFormat::setDefaultFormat(graphicsFormat);
  QGuiApplication application(argc, argv);

  QCommandLineParser parser;
  parser.setApplicationDescription(QStringLiteral("Kearne mechanical CAD"));
  parser.addHelpOption();
  parser.addVersionOption();
  QCommandLineOption captureOption(
      QStringLiteral("capture-dir"),
      QStringLiteral("Capture the visible Kearne session and exit."),
      QStringLiteral("directory"));
  QCommandLineOption workspaceOption(
      QStringLiteral("workspace"),
      QStringLiteral("Select the initial workspace."), QStringLiteral("id"),
      QStringLiteral("model"));
  QCommandLineOption surfaceOption(
      QStringLiteral("surface"),
      QStringLiteral("Select the initial application surface."),
      QStringLiteral("id"), QStringLiteral("editor"));
  QCommandLineOption stateOption(
      QStringLiteral("ui-state"),
      QStringLiteral("Select deterministic contract state."),
      QStringLiteral("id"));
  QCommandLineOption inspectorOption(
      QStringLiteral("inspector-page"),
      QStringLiteral("Select the initial inspector page."),
      QStringLiteral("id"), QStringLiteral("properties"));
  QCommandLineOption settingsCategoryOption(
      QStringLiteral("settings-category"),
      QStringLiteral("Select the initial settings category."),
      QStringLiteral("id"), QStringLiteral("appearance"));
  QCommandLineOption themeOption(
      QStringLiteral("theme"),
      QStringLiteral(
          "Select the initial UI theme without changing project data."),
      QStringLiteral("id"));
  QCommandLineOption actionOption(
      QStringLiteral("ui-action"),
      QStringLiteral("Invoke a visible control by semantic ID before capture."),
      QStringLiteral("semantic-id"));
  QCommandLineOption operationOption(
      QStringLiteral("ui-operation"),
      QStringLiteral(
          "Perform a JSON-encoded semantic operation before capture."),
      QStringLiteral("json"));
  QCommandLineOption designEngineOption(
      QStringLiteral("design-engine"),
      QStringLiteral("Use the design engine instead of capture fixtures."));
  QCommandLineOption widthOption(
      QStringLiteral("width"), QStringLiteral("Window width."),
      QStringLiteral("pixels"), QStringLiteral("1440"));
  QCommandLineOption heightOption(
      QStringLiteral("height"), QStringLiteral("Window height."),
      QStringLiteral("pixels"), QStringLiteral("900"));
  parser.addOptions({captureOption, workspaceOption, surfaceOption, stateOption,
                     inspectorOption, settingsCategoryOption, themeOption,
                     actionOption, operationOption, designEngineOption,
                     widthOption, heightOption});
  parser.process(application);
  if ((parser.isSet(actionOption) || parser.isSet(operationOption)) &&
      !parser.isSet(captureOption)) {
    std::cerr << "semantic UI operations require --capture-dir\n";
    return EXIT_FAILURE;
  }
  if (parser.isSet(captureOption))
    QAccessible::setActive(true);

  kearne::ui::UserPreferences preferences;
  kearne::ui::ThemeManager themes;
  const QString initialTheme =
      parser.isSet(themeOption)
          ? parser.value(themeOption)
          : preferences.value(QStringLiteral("theme")).toString();
  if (!themes.selectTheme(initialTheme)) {
    if (parser.isSet(themeOption)) {
      std::cerr << themes.lastError().toStdString() << '\n';
      return EXIT_FAILURE;
    }
    themes.selectTheme(QStringLiteral("system"));
    if (!preferences.setValue(QStringLiteral("theme"),
                              QStringLiteral("system")))
      std::cerr << preferences.lastError().toStdString() << '\n';
  }

  const QString navigationProfile =
      preferences.value(QStringLiteral("navigation-profile")).toString();
  const QString zoomDirection =
      preferences.value(QStringLiteral("zoom-direction")).toString();
  kearne::ui::ViewportCamera camera(
      navigationProfile, zoomDirection == QStringLiteral("reversed"));
  kearne::ui::SketchCameraController sketchCamera;
  kearne::ui::NavigationTargetRouter navigationRouter(camera);
  kearne::ui::NavigationDevice navigationDevice(navigationRouter);
  const bool useDesignEngine =
      !parser.isSet(captureOption) || parser.isSet(designEngineOption);
  std::unique_ptr<kearne::ui::FrontendController> frontend =
      useDesignEngine
          ? kearne::ui::makeDesktopController(
                std::make_unique<kearne::ui::LocalSketchSession>(
                    localSketchSessionConfig()),
                themeOptions(themes), themes.selectionId(),
                preferences.value(QStringLiteral("default-length-unit"))
                    .toString(),
                preferences.value(QStringLiteral("interface-density"))
                    .toString(),
                navigationProfile, zoomDirection)
          : kearne::ui::makeCaptureDesktopController(
                themeOptions(themes), themes.selectionId(),
                preferences.value(QStringLiteral("default-length-unit"))
                    .toString(),
                preferences.value(QStringLiteral("interface-density"))
                    .toString(),
                navigationProfile, zoomDirection);
  kearne::ui::UiSession session(std::move(frontend));
  kearne::ui::WorkspaceState workspaceState;
  QObject::connect(
      &session, &kearne::ui::UiSession::preferenceChanged, &themes,
      [&themes, &preferences, &camera](const QString &preferenceId,
                                       const QVariant &value) {
        if (preferenceId == QStringLiteral("theme") &&
            !themes.selectTheme(value.toString()))
          return;
        if (preferenceId == QStringLiteral("navigation-profile"))
          camera.setNavigationProfile(value.toString());
        if (preferenceId == QStringLiteral("zoom-direction"))
          camera.setZoomReversed(value.toString() ==
                                 QStringLiteral("reversed"));
        if ((preferenceId == QStringLiteral("theme") ||
             preferenceId == QStringLiteral("default-length-unit") ||
             preferenceId == QStringLiteral("interface-density") ||
             preferenceId == QStringLiteral("navigation-profile") ||
             preferenceId == QStringLiteral("zoom-direction")) &&
            !preferences.setValue(preferenceId, value))
          std::cerr << preferences.lastError().toStdString() << '\n';
      });
  QObject::connect(&themes, &kearne::ui::ThemeManager::selectionChanged,
                   &session, [&session, &themes, &preferences] {
                     if (!preferences.setValue(QStringLiteral("theme"),
                                               themes.selectionId()))
                       std::cerr << preferences.lastError().toStdString()
                                 << '\n';
                     session.replacePreferenceOptions(QStringLiteral("theme"),
                                                      themeOptions(themes),
                                                      themes.selectionId());
                   });

  session.navigateTo(parser.value(surfaceOption));
  session.selectWorkspace(parser.value(workspaceOption));
  session.selectInspectorPage(parser.value(inspectorOption));
  session.selectSettingsCategory(parser.value(settingsCategoryOption));
  if (parser.isSet(stateOption))
    session.requestCommand(QStringLiteral("capture.state.") +
                           parser.value(stateOption));
  workspaceState.selectWorkspace(session.activeWorkspaceId());
  QObject::connect(&session, &kearne::ui::UiSession::projectionChanged,
                   &workspaceState, [&session, &workspaceState] {
                     workspaceState.selectWorkspace(
                         session.activeWorkspaceId());
                   });

  kearne::ui::ApplicationContext::install(
      session, themes, workspaceState, camera, sketchCamera, navigationDevice);
  int exitCode = EXIT_FAILURE;
  {
    QQmlApplicationEngine engine;
    engine.loadFromModule(QStringLiteral("Kearne.UI"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty())
      return EXIT_FAILURE;

    auto *window =
        qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    if (!window) {
      std::cerr << "root QML object is not a window\n";
      return EXIT_FAILURE;
    }
    bool widthOk = false;
    bool heightOk = false;
    const int width = parser.value(widthOption).toInt(&widthOk);
    const int height = parser.value(heightOption).toInt(&heightOk);
    if (!widthOk || !heightOk || width < 800 || height < 600) {
      std::cerr << "capture dimensions must be at least 800x600\n";
      return EXIT_FAILURE;
    }
    window->resize(width, height);

    std::unique_ptr<kearne::ui::SketchViewportBridge> sketchViewport;
    if (useDesignEngine) {
      auto *host = window->findChild<QQuickItem *>(
          QStringLiteral("nativeSketchSceneHost"));
      if (!host) {
        std::cerr << "native Sketch viewport host is missing\n";
        return EXIT_FAILURE;
      }
      auto created = kearne::ui::SketchViewportBridge::create(*host, session,
                                                              sketchCamera);
      if (!created) {
        std::cerr << created.error().summary << '\n';
        return EXIT_FAILURE;
      }
      sketchViewport = std::move(*created);
    }

    std::unique_ptr<kearne::ui::ObservationController> observation;
    if (parser.isSet(captureOption)) {
      try {
        observation = std::make_unique<kearne::ui::ObservationController>(
            *window, session, themes, parser.value(captureOption),
            kearne::ui::parseSemanticOperations(parser.values(actionOption),
                                                parser.values(operationOption)),
            [&sketchViewport] {
              return !sketchViewport || sketchViewport->presentationCurrent();
            },
            &application);
      } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
      }
    }
    exitCode = application.exec();
    if (sketchViewport) {
      auto stopped = sketchViewport->shutdown();
      if (!stopped) {
        std::cerr << stopped.error().summary << '\n';
        exitCode = EXIT_FAILURE;
      }
    }
  }
  if (!kearne::ui::shutdownSketchSceneResources()) {
    std::cerr << "sketch render resources did not drain before shutdown\n";
    return EXIT_FAILURE;
  }
  return exitCode;
}
