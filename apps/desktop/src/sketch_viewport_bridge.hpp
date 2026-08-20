#pragma once

#include "sketch_camera_controller.hpp"
#include "sketch_scene_publication.hpp"

#include <kearne/base/value.hpp>

#include <QObject>

#include <chrono>
#include <memory>

class QQuickItem;

namespace kearne::ui {

class UiSession;

// Publishes the frontend's immutable Sketch scene to one native Quick item.
// Geometry preparation stays off the UI thread; the bridge only validates and
// advances typed presentation generations on the owning thread.
class SketchViewportBridge final : public QObject {
public:
  [[nodiscard]] static Result<std::unique_ptr<SketchViewportBridge>>
  create(QQuickItem &host, UiSession &session, SketchCameraController &camera);
  ~SketchViewportBridge() override;

  SketchViewportBridge(const SketchViewportBridge &) = delete;
  SketchViewportBridge &operator=(const SketchViewportBridge &) = delete;

  [[nodiscard]] Result<void>
  shutdown(std::chrono::milliseconds timeout = std::chrono::seconds{5});
  [[nodiscard]] SketchSceneItem *item() const { return item_.get(); }
  [[nodiscard]] bool presentationCurrent() const;
  [[nodiscard]] Diagnostic lastDiagnostic() const { return lastDiagnostic_; }

private:
  SketchViewportBridge(QQuickItem &host, UiSession &session,
                       SketchCameraController &camera);

  [[nodiscard]] Result<void> initialize();
  [[nodiscard]] Result<void> publishScene();
  [[nodiscard]] Result<void> publishCamera();
  void synchronizeGeometry();
  void record(Result<void> result);

  QQuickItem &host_;
  UiSession &session_;
  SketchCameraController &camera_;
  SketchPreparationExecutor executor_;
  std::unique_ptr<SketchSceneItem> item_;
  std::unique_ptr<SketchScenePublicationController> publication_;
  std::shared_ptr<const render::SketchSceneSnapshot> publishedScene_;
  std::uint64_t productGeneration_ = 0U;
  Diagnostic lastDiagnostic_;
  bool stopped_ = false;
};

} // namespace kearne::ui
