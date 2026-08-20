#pragma once

#include "sketch_frame_renderer.hpp"
#include "sketch_prepared_products.hpp"
#include "sketch_scene_projection.hpp"

#include <QQuickItem>

#include <atomic>
#include <memory>
#include <mutex>

namespace kearne::ui {

class SketchSceneItem : public QQuickItem {
public:
  explicit SketchSceneItem(QQuickItem *parent = nullptr);

  void retarget(render::SceneTarget desired);
  [[nodiscard]] Result<PreparedSketchSceneOffer>
  publishProducts(std::shared_ptr<const PreparedSketchProducts> prepared);
  [[nodiscard]] Result<SketchCameraDecision>
  publishCamera(SketchCamera2d camera);
  [[nodiscard]] Result<SketchPickCoverageDecision>
  publishPickCoverage(SketchPickCoveragePolicy policy);
  [[nodiscard]] SketchCurveLod requestedLod() const;
  void setPalette(SketchScenePalette palette);

  [[nodiscard]] Result<SketchItemPickEvidence> pick(
      QPointF itemLogical, double toleranceLogicalPixels,
      render::SketchPickTargets targets = render::SketchPickTargets::All) const;
  [[nodiscard]] std::shared_ptr<const PresentedSketchFrame>
  presentedFrame() const;
  [[nodiscard]] std::shared_ptr<const SynchronizedSketchScene>
  presentedScene() const;
  [[nodiscard]] Result<std::shared_ptr<const void>> retirementOwner() const;
  void clearPresentation();
  [[nodiscard]] SketchMeshMetrics meshMetrics() const;
  [[nodiscard]] std::uint64_t geometryBuildCount() const;
  [[nodiscard]] SketchSynchronizationMetrics synchronizationMetrics() const;
  [[nodiscard]] SketchGpuUploadMetrics gpuUploadMetrics() const;
  [[nodiscard]] Diagnostic lastDiagnostic() const;

protected:
  QSGNode *updatePaintNode(QSGNode *oldNode,
                           UpdatePaintNodeData *data) override;
  void releaseResources() override;
  void itemChange(ItemChange change, const ItemChangeData &value) override;
  void geometryChange(const QRectF &newGeometry,
                      const QRectF &oldGeometry) override;

private:
  void requestFrame();
  void setLastDiagnostic(Diagnostic diagnostic);

  SketchScenePresenter presenter_;
  mutable std::mutex presentationMutex_;
  SketchScenePalette palette_;
  Diagnostic lastDiagnostic_;
  std::uint64_t paletteGeneration_ = 1;
  std::atomic_bool updateQueued_ = false;
  std::shared_ptr<SketchFrameRendererState> rendererState_;
  QMetaObject::Connection sceneGraphInvalidatedConnection_;
};

} // namespace kearne::ui
