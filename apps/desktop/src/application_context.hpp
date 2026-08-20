#pragma once

#include "navigation_device.hpp"
#include "sketch_camera_controller.hpp"
#include "theme_manager.hpp"
#include "ui_session.hpp"
#include "viewport_camera.hpp"
#include "workspace_state.hpp"

#include <QObject>
#include <QtQml/qqmlregistration.h>

namespace kearne::ui {

class ApplicationContext : public QObject {
  Q_OBJECT
  QML_NAMED_ELEMENT(App)
  QML_SINGLETON
  Q_PROPERTY(kearne::ui::UiSession *ui READ ui CONSTANT)
  Q_PROPERTY(kearne::ui::ThemeManager *themes READ themes CONSTANT)
  Q_PROPERTY(kearne::ui::WorkspaceState *workspace READ workspace CONSTANT)
  Q_PROPERTY(kearne::ui::ViewportCamera *camera READ camera CONSTANT)
  Q_PROPERTY(kearne::ui::SketchCameraController *sketchCamera READ sketchCamera
                 CONSTANT)
  Q_PROPERTY(kearne::ui::NavigationDevice *navigationDevice READ
                 navigationDevice CONSTANT)

public:
  explicit ApplicationContext(QObject *parent = nullptr);

  static void install(UiSession &ui, ThemeManager &themes,
                      WorkspaceState &workspace, ViewportCamera &camera,
                      SketchCameraController &sketchCamera,
                      NavigationDevice &navigationDevice);
  [[nodiscard]] UiSession *ui() const;
  [[nodiscard]] ThemeManager *themes() const;
  [[nodiscard]] WorkspaceState *workspace() const;
  [[nodiscard]] ViewportCamera *camera() const;
  [[nodiscard]] SketchCameraController *sketchCamera() const;
  [[nodiscard]] NavigationDevice *navigationDevice() const;

private:
  static UiSession *ui_;
  static ThemeManager *themes_;
  static WorkspaceState *workspace_;
  static ViewportCamera *camera_;
  static SketchCameraController *sketchCamera_;
  static NavigationDevice *navigationDevice_;
};

} // namespace kearne::ui
