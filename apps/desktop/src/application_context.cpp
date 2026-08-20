#include "application_context.hpp"

#include "navigation_device.hpp"

#include <QtGlobal>

namespace kearne::ui {

UiSession *ApplicationContext::ui_ = nullptr;
ThemeManager *ApplicationContext::themes_ = nullptr;
WorkspaceState *ApplicationContext::workspace_ = nullptr;
ViewportCamera *ApplicationContext::camera_ = nullptr;
SketchCameraController *ApplicationContext::sketchCamera_ = nullptr;
NavigationDevice *ApplicationContext::navigationDevice_ = nullptr;

ApplicationContext::ApplicationContext(QObject *parent) : QObject(parent) {
  Q_ASSERT(ui_);
  Q_ASSERT(themes_);
  Q_ASSERT(workspace_);
  Q_ASSERT(camera_);
  Q_ASSERT(sketchCamera_);
  Q_ASSERT(navigationDevice_);
}

void ApplicationContext::install(UiSession &ui, ThemeManager &themes,
                                 WorkspaceState &workspace,
                                 ViewportCamera &camera,
                                 SketchCameraController &sketchCamera,
                                 NavigationDevice &navigationDevice) {
  Q_ASSERT(!ui_);
  Q_ASSERT(!themes_);
  Q_ASSERT(!workspace_);
  Q_ASSERT(!camera_);
  Q_ASSERT(!sketchCamera_);
  Q_ASSERT(!navigationDevice_);
  ui_ = &ui;
  themes_ = &themes;
  workspace_ = &workspace;
  camera_ = &camera;
  sketchCamera_ = &sketchCamera;
  navigationDevice_ = &navigationDevice;
}

UiSession *ApplicationContext::ui() const { return ui_; }
ThemeManager *ApplicationContext::themes() const { return themes_; }
WorkspaceState *ApplicationContext::workspace() const { return workspace_; }
ViewportCamera *ApplicationContext::camera() const { return camera_; }
SketchCameraController *ApplicationContext::sketchCamera() const {
  return sketchCamera_;
}
NavigationDevice *ApplicationContext::navigationDevice() const {
  return navigationDevice_;
}

} // namespace kearne::ui
