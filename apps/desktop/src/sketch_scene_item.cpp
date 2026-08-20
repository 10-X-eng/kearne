#include "sketch_scene_item.hpp"

#include <QMetaObject>
#include <QQuickWindow>

#include <cmath>
#include <new>
#include <utility>

namespace kearne::ui {
namespace {

struct SketchItemRetirementOwner {
  std::shared_ptr<const void> presenter;
  std::shared_ptr<const PresentedSketchFrame> presented;
};

} // namespace

SketchSceneItem::SketchSceneItem(QQuickItem *parent)
    : QQuickItem(parent),
      rendererState_(std::make_shared<SketchFrameRendererState>()) {
  setFlag(ItemHasContents, true);
  if (auto *initialWindow = window())
    sceneGraphInvalidatedConnection_ = QObject::connect(
        initialWindow, &QQuickWindow::sceneGraphInvalidated, this,
        [state = rendererState_] { state->invalidate(); },
        Qt::DirectConnection);
}

void SketchSceneItem::retarget(render::SceneTarget desired) {
  presenter_.retarget(std::move(desired));
  requestFrame();
}

Result<PreparedSketchSceneOffer> SketchSceneItem::publishProducts(
    std::shared_ptr<const PreparedSketchProducts> prepared) {
  auto offered = presenter_.publish(std::move(prepared));
  if (offered && offered->decision == PreparedSketchSceneDecision::Accepted)
    requestFrame();
  return offered;
}

Result<SketchCameraDecision>
SketchSceneItem::publishCamera(SketchCamera2d camera) {
  auto offered = presenter_.publishCamera(camera);
  if (offered && *offered == SketchCameraDecision::Accepted)
    requestFrame();
  return offered;
}

Result<SketchPickCoverageDecision>
SketchSceneItem::publishPickCoverage(SketchPickCoveragePolicy policy) {
  auto offered = presenter_.publishPickCoverage(policy);
  if (offered && *offered == SketchPickCoverageDecision::Accepted)
    requestFrame();
  return offered;
}

SketchCurveLod SketchSceneItem::requestedLod() const {
  return presenter_.requestedLod();
}

void SketchSceneItem::setPalette(SketchScenePalette palette) {
  {
    std::scoped_lock lock{presentationMutex_};
    if (palette_ == palette)
      return;
    palette_ = palette;
    ++paletteGeneration_;
  }
  requestFrame();
}

Result<SketchItemPickEvidence>
SketchSceneItem::pick(QPointF itemLogical, double toleranceLogicalPixels,
                      render::SketchPickTargets targets) const {
  if (!window() || !isVisible() || opacity() <= 0.0 ||
      !window()->isSceneGraphInitialized())
    return std::unexpected(diagnostic("desktop.sketch.not-presented",
                                      "sketch item is not visibly presented"));
  const auto frame = rendererState_->presented();
  if (!frame)
    return std::unexpected(
        diagnostic("desktop.sketch.missing-presented-frame",
                   "no sketch frame has reached the render pass"));
  const auto &synchronized = frame->synchronized();
  if (!std::isfinite(toleranceLogicalPixels) || toleranceLogicalPixels < 0.0 ||
      toleranceLogicalPixels >
          synchronized->pickCoverage().maximumToleranceLogicalPixels)
    return std::unexpected(
        diagnostic("desktop.sketch.pick-tolerance-policy",
                   "pick tolerance violates the presented coverage policy"));
  const QSizeF viewport = synchronized->transform().viewportLogical();
  if (!QRectF{QPointF{}, viewport}.contains(itemLogical))
    return std::unexpected(
        diagnostic("desktop.sketch.pick-outside-presented-viewport",
                   "pick point is outside the presented sketch viewport"));
  auto evidence = presenter_.pick(synchronized, itemLogical,
                                  toleranceLogicalPixels, targets);
  if (evidence)
    evidence->presented = frame;
  return evidence;
}

std::shared_ptr<const PresentedSketchFrame>
SketchSceneItem::presentedFrame() const {
  return rendererState_->presented();
}

std::shared_ptr<const SynchronizedSketchScene>
SketchSceneItem::presentedScene() const {
  const auto frame = presentedFrame();
  return frame ? frame->synchronized() : nullptr;
}

Result<std::shared_ptr<const void>> SketchSceneItem::retirementOwner() const {
  auto presenter = presenter_.retirementOwner();
  if (!presenter)
    return std::unexpected(std::move(presenter.error()));
  auto presented = rendererState_->presented();
  if (!*presenter)
    return std::static_pointer_cast<const void>(std::move(presented));
  if (!presented)
    return std::move(*presenter);
  try {
    return std::static_pointer_cast<const void>(
        std::make_shared<const SketchItemRetirementOwner>(
            SketchItemRetirementOwner{std::move(*presenter),
                                      std::move(presented)}));
  } catch (const std::bad_alloc &) {
    return std::unexpected(
        diagnostic("desktop.sketch.retirement-allocation",
                   "sketch item retirement allocation failed"));
  }
}

void SketchSceneItem::clearPresentation() {
  presenter_.clear();
  rendererState_->invalidate();
  requestFrame();
}

SketchMeshMetrics SketchSceneItem::meshMetrics() const {
  return rendererState_->meshMetrics();
}

std::uint64_t SketchSceneItem::geometryBuildCount() const {
  return rendererState_->geometryBuildCount();
}

SketchSynchronizationMetrics SketchSceneItem::synchronizationMetrics() const {
  return presenter_.synchronizationMetrics();
}

SketchGpuUploadMetrics SketchSceneItem::gpuUploadMetrics() const {
  return rendererState_->uploadMetrics();
}

Diagnostic SketchSceneItem::lastDiagnostic() const {
  {
    std::scoped_lock lock{presentationMutex_};
    if (!lastDiagnostic_.code.empty())
      return lastDiagnostic_;
  }
  return rendererState_->lastDiagnostic();
}

QSGNode *SketchSceneItem::updatePaintNode(QSGNode *oldNode,
                                          UpdatePaintNodeData *) {
  updateQueued_.store(false, std::memory_order_release);
  if (width() <= 0.0 || height() <= 0.0 || !window()) {
    rendererState_->invalidate();
    return oldNode;
  }
  auto desired = presenter_.synchronize({width(), height()});
  if (!desired) {
    rendererState_->invalidate();
    setLastDiagnostic(std::move(desired.error()));
    return oldNode;
  }

  SketchScenePalette palette;
  std::uint64_t paletteGeneration = 0U;
  {
    std::scoped_lock lock{presentationMutex_};
    palette = palette_;
    paletteGeneration = paletteGeneration_;
    lastDiagnostic_ = {};
  }
  auto *renderer = static_cast<SketchFrameRenderer *>(oldNode);
  if (!renderer)
    renderer = new SketchFrameRenderer{*window(), rendererState_};
  renderer->synchronize(*desired, palette, paletteGeneration,
                        QRectF{0.0, 0.0, width(), height()});
  return renderer;
}

void SketchSceneItem::releaseResources() { rendererState_->invalidate(); }

void SketchSceneItem::itemChange(ItemChange change,
                                 const ItemChangeData &value) {
  QQuickItem::itemChange(change, value);
  if (change == ItemSceneChange) {
    QObject::disconnect(sceneGraphInvalidatedConnection_);
    rendererState_->invalidate();
    if (value.window)
      sceneGraphInvalidatedConnection_ = QObject::connect(
          value.window, &QQuickWindow::sceneGraphInvalidated, this,
          [state = rendererState_] { state->invalidate(); },
          Qt::DirectConnection);
    if (value.window)
      requestFrame();
    return;
  }
  if ((change == ItemVisibleHasChanged && !isVisible()) ||
      (change == ItemOpacityHasChanged && opacity() <= 0.0)) {
    rendererState_->invalidate();
    return;
  }
  if (change == ItemVisibleHasChanged || change == ItemOpacityHasChanged)
    requestFrame();
}

void SketchSceneItem::geometryChange(const QRectF &newGeometry,
                                     const QRectF &oldGeometry) {
  QQuickItem::geometryChange(newGeometry, oldGeometry);
  if (newGeometry.size() != oldGeometry.size())
    requestFrame();
}

void SketchSceneItem::requestFrame() {
  if (updateQueued_.exchange(true, std::memory_order_acq_rel))
    return;
  QMetaObject::invokeMethod(
      this,
      [this] {
        updateQueued_.store(false, std::memory_order_release);
        update();
      },
      Qt::QueuedConnection);
}

void SketchSceneItem::setLastDiagnostic(Diagnostic diagnostic) {
  std::scoped_lock lock{presentationMutex_};
  lastDiagnostic_ = std::move(diagnostic);
}

} // namespace kearne::ui
