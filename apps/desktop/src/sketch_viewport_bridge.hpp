#pragma once

#include "sketch_camera_controller.hpp"
#include "sketch_scene_publication.hpp"

#include <kearne/base/value.hpp>

#include <QObject>
#include <QPointF>

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

class QQuickItem;
class QQuickWindow;

namespace kearne::ui {

class UiSession;
struct SketchPickSelection;
struct SketchPrimitiveProjection;
struct SketchSelectionScope;

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
  [[nodiscard]] QString presentationStatus() const;
  [[nodiscard]] Diagnostic lastDiagnostic() const { return lastDiagnostic_; }

private:
  SketchViewportBridge(QQuickItem &host, UiSession &session,
                       SketchCameraController &camera);

  [[nodiscard]] Result<void> initialize();
  [[nodiscard]] Result<void> publishProjection();
  [[nodiscard]] Result<void> publishScene();
  [[nodiscard]] Result<void> publishProvisional();
  [[nodiscard]] Result<void> publishCamera();
  [[nodiscard]] Result<void>
  publishOverlay(std::span<const SketchSelectionScope> hover,
                 std::span<const SketchSelectionScope> selected);
  void subscribeToWindow(QQuickWindow *window);
  void repickHoverAfterPresentedFrame();
  void synchronizeGeometry();
  void record(Result<void> result);

  QQuickItem &host_;
  UiSession &session_;
  SketchCameraController &camera_;
  SketchPreparationExecutor executor_;
  std::unique_ptr<SketchSceneItem> item_;
  std::unique_ptr<SketchScenePublicationController> publication_;
  std::shared_ptr<const render::SketchSceneSnapshot> publishedScene_;
  std::shared_ptr<const render::SketchPresentationOverlay> overlay_;
  std::shared_ptr<const render::SketchProvisionalGeometry> provisional_;
  std::shared_ptr<const render::SketchMarkerPacket> markers_;
  std::array<render::SketchOverlayRoleSetPtr, 4> overlayRoleSets_;
  std::vector<SketchSelectionScope> hovered_;
  std::optional<QPointF> lastPickItemPoint_;
  std::optional<QPointF> lastHoverItemPoint_;
  std::optional<SketchProductStamp> requestedProducts_;
  std::vector<SketchSelectionScope> selected_;
  std::vector<SketchPrimitiveProjection> publishedDraft_;
  QString provisionalCommand_;
  std::uint64_t toolInstanceGeneration_ = 0U;
  std::uint64_t provisionalGeneration_ = 0U;
  std::uint64_t productGeneration_ = 0U;
  std::uint64_t presentationGeneration_ = 0U;
  std::uint64_t markerGeneration_ = 0U;
  QMetaObject::Connection frameSwappedConnection_;
  Diagnostic lastDiagnostic_;
  bool hoverRepickPending_ = false;
  bool presentationPublished_ = false;
  std::uint8_t splineAnnotationsPublished_ = 0U;
  SketchConstraintDisplay constraintDisplayPublished_{};
  SketchMarkerEmphasis markerEmphasisPublished_{};
  QString hoveredConstraintId_;
  bool stopped_ = false;
};

} // namespace kearne::ui
