#pragma once

#include "sketch_scene_products.hpp"
#include "sketch_vector_packet.hpp"

#include <kearne/render/sketch_scene.hpp>

#include <QColor>
#include <QMatrix4x4>
#include <QPointF>
#include <QRect>
#include <QRegion>
#include <QSize>
#include <QSizeF>
#include <QtGlobal>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <numbers>
#include <optional>
#include <span>
#include <stop_token>
#include <vector>

namespace kearne::ui {

class PreparedSketchProducts;
class PreparedSketchScene;
class SketchFrameRenderer;
struct SketchVectorPacketAdapterAccess;

struct SketchScenePalette {
  QRgb regular = qRgb(49, 116, 179);
  QRgb construction = qRgb(111, 122, 135);
  QRgb selected = qRgb(0, 151, 167);
  QRgb preview = qRgb(24, 154, 116);
  QRgb diagnostic = qRgb(210, 66, 74);
  QRgb hovered = qRgb(245, 158, 11);
  bool operator==(const SketchScenePalette &) const = default;

  [[nodiscard]] QRgb color(render::SketchStyleRole role) const;
};

// One native vector-record range inside an immutable upload chunk.
struct SketchPrimitiveChunkSpan {
  std::uint32_t chunk = 0;
  std::uint32_t firstRecord = 0;
  std::uint32_t recordCount = 0;
  bool operator==(const SketchPrimitiveChunkSpan &) const = default;
};

struct SketchPrimitiveVectorEntry {
  render::SketchPrimitiveHandle primitive;
  std::uint32_t firstSpan = 0;
  std::uint32_t spanCount = 0;
  std::uint32_t recordCount = 0;
  bool operator==(const SketchPrimitiveVectorEntry &) const = default;
};

class SketchPrimitiveVectorIndex final {
public:
  [[nodiscard]] std::span<const SketchPrimitiveVectorEntry>
  entries() const {
    return entries_;
  }
  [[nodiscard]] std::span<const SketchPrimitiveChunkSpan> spans() const {
    return spans_;
  }
  [[nodiscard]] const SketchPrimitiveVectorEntry *
  find(render::SketchPrimitiveHandle primitive) const;
  [[nodiscard]] std::span<const SketchPrimitiveChunkSpan>
  spans(render::SketchPrimitiveHandle primitive) const;
  [[nodiscard]] std::size_t retainedBytes() const { return retainedBytes_; }

private:
  SketchPrimitiveVectorIndex(
      std::vector<SketchPrimitiveVectorEntry> entries,
      std::vector<SketchPrimitiveChunkSpan> spans, std::size_t retainedBytes);

  std::vector<SketchPrimitiveVectorEntry> entries_;
  std::vector<SketchPrimitiveChunkSpan> spans_;
  std::size_t retainedBytes_ = 0;

  friend class SketchVectorPacket;
  friend struct SketchVectorPacketAdapterAccess;
  friend Result<std::shared_ptr<const PreparedSketchScene>>
      prepareSketchScene(std::shared_ptr<const render::SketchSceneSnapshot>,
                         render::SketchPickIndexOptions,
                         SketchVectorUploadOptions,
                         std::shared_ptr<const PreparedSketchScene>,
                         std::stop_token);
};

[[nodiscard]] Result<SketchVectorPacket>
buildSketchVectorPacket(const render::SketchSceneSnapshot &scene,
                        SketchVectorUploadOptions upload = {},
                        std::shared_ptr<const SketchVectorPacket> reuse = {},
                        std::stop_token cancellation = {});

// Complete camera-independent render input. Preparation is pure and may run on
// any worker thread; render synchronization only moves this immutable pointer.
class PreparedSketchScene final {
public:
  struct Metrics {
    std::size_t vectorRetainedBytes = 0U;
    std::size_t provenanceRetainedBytes = 0U;
    std::size_t pickIndexRetainedBytes = 0U;
    std::size_t totalRetainedBytes = 0U;
    bool operator==(const Metrics &) const = default;
  };

  [[nodiscard]] const render::SceneStamp &stamp() const { return stamp_; }
  [[nodiscard]] const std::shared_ptr<const render::SketchSceneSnapshot> &
  scene() const {
    return scene_;
  }
  [[nodiscard]] const std::shared_ptr<const render::SketchPickIndex> &
  pickIndex() const {
    return pickIndex_;
  }
  [[nodiscard]] const render::SketchPickIndexOptions &pickOptions() const {
    return pickOptions_;
  }
  [[nodiscard]] const std::shared_ptr<const SketchVectorPacket> &packet() const {
    return packet_;
  }
  [[nodiscard]] const std::shared_ptr<const SketchPrimitiveVectorIndex> &
  primitiveVectorIndex() const {
    return primitiveVectorIndex_;
  }
  [[nodiscard]] const Metrics &metrics() const { return metrics_; }

private:
  PreparedSketchScene(render::SceneStamp stamp,
                      std::shared_ptr<const render::SketchSceneSnapshot> scene,
                      std::shared_ptr<const render::SketchPickIndex> pickIndex,
                      render::SketchPickIndexOptions pickOptions,
                      std::shared_ptr<const SketchVectorPacket> packet,
                      std::shared_ptr<const SketchPrimitiveVectorIndex>
                          primitiveVectorIndex,
                      Metrics metrics);

  render::SceneStamp stamp_;
  std::shared_ptr<const render::SketchSceneSnapshot> scene_;
  std::shared_ptr<const render::SketchPickIndex> pickIndex_;
  render::SketchPickIndexOptions pickOptions_;
  std::shared_ptr<const SketchVectorPacket> packet_;
  std::shared_ptr<const SketchPrimitiveVectorIndex> primitiveVectorIndex_;
  Metrics metrics_;

  friend Result<std::shared_ptr<const PreparedSketchScene>>
      prepareSketchScene(std::shared_ptr<const render::SketchSceneSnapshot>,
                         render::SketchPickIndexOptions,
                         SketchVectorUploadOptions,
                         std::shared_ptr<const PreparedSketchScene>,
                         std::stop_token);
};

[[nodiscard]] Result<std::shared_ptr<const PreparedSketchScene>>
prepareSketchScene(std::shared_ptr<const render::SketchSceneSnapshot> scene,
                   render::SketchPickIndexOptions picking = {},
                   SketchVectorUploadOptions upload = {},
                   std::shared_ptr<const PreparedSketchScene> reuse = {},
                   std::stop_token cancellation = {});

struct SketchUploadSliceEntry {
  std::uint32_t chunk = 0;
  bool reuseResidentGeometry = false;
};

struct SketchUploadSlice {
  std::vector<SketchUploadSliceEntry> entries;
  std::size_t bytes = 0;
};

class ProgressiveSketchUpload final {
public:
  [[nodiscard]] static Result<ProgressiveSketchUpload> create(
      std::shared_ptr<const PreparedSketchScene> prepared,
      std::vector<std::uint32_t> requiredChunks,
      std::span<const std::shared_ptr<const SketchVectorChunk>> residentChunks);
  [[nodiscard]] static Result<ProgressiveSketchUpload> create(
      std::shared_ptr<const PreparedSketchScene> prepared,
      SketchChunkSequence requiredChunks,
      std::span<const std::shared_ptr<const SketchVectorChunk>> residentChunks);
  [[nodiscard]] static Result<ProgressiveSketchUpload> create(
      std::shared_ptr<const SketchVectorPacket> packet,
      SketchChunkSequence requiredChunks,
      std::span<const std::shared_ptr<const SketchVectorChunk>> residentChunks);

  [[nodiscard]] Result<SketchUploadSlice>
  takeNextSlice(std::size_t maximumBytes, std::size_t maximumChunks);
  [[nodiscard]] const std::shared_ptr<const SketchVectorPacket> &packet() const {
    return packet_;
  }
  [[nodiscard]] const SketchChunkSequence &requiredChunks() const {
    return requiredChunks_;
  }
  [[nodiscard]] bool complete() const {
    return cursor_ == requiredChunks_.size();
  }
  [[nodiscard]] std::size_t pendingCount() const {
    return requiredChunks_.size() - cursor_;
  }
  [[nodiscard]] std::size_t reusedCount() const { return reusedCount_; }
  [[nodiscard]] SketchChunkSequence releaseRequiredChunks();

private:
  static constexpr std::size_t maximumResidentIdentityBytes =
      16U * 1024U * 1024U;
  ProgressiveSketchUpload(std::shared_ptr<const SketchVectorPacket> packet,
                          SketchChunkSequence requiredChunks,
                          std::vector<const SketchVectorChunk *> resident);

  std::shared_ptr<const SketchVectorPacket> packet_;
  SketchChunkSequence requiredChunks_;
  std::vector<const SketchVectorChunk *> residentChunks_;
  std::size_t cursor_ = 0;
  std::size_t reusedCount_ = 0;
};

class SynchronizedSketchScene final {
public:
  SynchronizedSketchScene(
      std::shared_ptr<const PreparedSketchProducts> products,
      SketchViewTransform transform, SketchPickCoveragePolicy pickCoverage,
      std::shared_ptr<const SketchPresentedChunkCoverage> presentedChunks = {});

  [[nodiscard]] const std::shared_ptr<const PreparedSketchProducts> &
  products() const;
  [[nodiscard]] const std::shared_ptr<const PreparedSketchScene> &
  prepared() const;
  [[nodiscard]] const std::shared_ptr<const render::SketchSceneSnapshot> &
  scene() const;
  [[nodiscard]] const std::shared_ptr<const render::SketchPickIndex> &
  pickIndex() const;
  [[nodiscard]] const std::shared_ptr<const SketchVectorPacket> &packet() const;
  [[nodiscard]] const SketchViewTransform &transform() const {
    return transform_;
  }
  [[nodiscard]] const SketchPickCoveragePolicy &pickCoverage() const {
    return pickCoverage_;
  }
  [[nodiscard]] const std::shared_ptr<const SketchPresentedChunkCoverage> &
  presentedChunks() const {
    return presentedChunks_;
  }

private:
  std::shared_ptr<const PreparedSketchProducts> products_;
  SketchViewTransform transform_;
  SketchPickCoveragePolicy pickCoverage_;
  std::shared_ptr<const SketchPresentedChunkCoverage> presentedChunks_;
};

// Immutable evidence captured by the production render node for one recorded
// draw. The matrix maps item coordinates to clip space; together with the
// device viewport it defines the exact item-to-device transform used.
struct SketchPresentationEvidence {
  SketchScenePalette palette;
  float inheritedOpacity = 1.0F;
  bool scissorEnabled = false;
  QRect scissorDevicePixels;
  bool stencilEnabled = false;
  int stencilValue = 0;
  QRegion clipRegionDevicePixels;
  QMatrix4x4 itemToClip;
  qreal devicePixelRatio = 1.0;
  QSize deviceViewportPixels;
  std::uint64_t renderEpoch = 0U;
  std::uint64_t frameSerial = 0U;
  std::shared_ptr<const std::vector<std::uint32_t>> renderTargetFormat;
};

// Only SketchFrameRenderer can create this token. A synchronized scene is a
// candidate; this object proves that its exact products and coverage reached
// an RHI render pass.
struct SketchPresentedProductCoverage {
  std::shared_ptr<const SketchPresentedChunkCoverage> overlayPoints;
  std::shared_ptr<const SketchPresentedChunkCoverage> provisional;
  std::shared_ptr<const SketchPresentedChunkCoverage> markers;
};

class PresentedSketchFrame final {
public:
  [[nodiscard]] const std::shared_ptr<const SynchronizedSketchScene> &
  synchronized() const {
    return synchronized_;
  }
  [[nodiscard]] const SketchPresentationEvidence &evidence() const {
    return evidence_;
  }
  [[nodiscard]] const std::shared_ptr<const SketchPresentedChunkCoverage> &
  provisionalChunks() const {
    return productCoverage_.provisional;
  }
  [[nodiscard]] const std::shared_ptr<const SketchPresentedChunkCoverage> &
  overlayPointChunks() const {
    return productCoverage_.overlayPoints;
  }
  [[nodiscard]] const std::shared_ptr<const SketchPresentedChunkCoverage> &
  markerChunks() const {
    return productCoverage_.markers;
  }
  [[nodiscard]] const SketchPresentedProductCoverage &productCoverage() const {
    return productCoverage_;
  }

private:
  PresentedSketchFrame(
      std::shared_ptr<const SynchronizedSketchScene> synchronized,
      SketchPresentedProductCoverage productCoverage,
      SketchPresentationEvidence evidence);

  std::shared_ptr<const SynchronizedSketchScene> synchronized_;
  SketchPresentedProductCoverage productCoverage_;
  SketchPresentationEvidence evidence_;

  friend class SketchFrameRenderer;
};

enum class PreparedSketchSceneDecision : std::uint8_t {
  Accepted = 1,
  Duplicate = 2,
  StaleTarget = 3,
  StaleGeneration = 4,
  GenerationConflict = 5,
};

struct PreparedSketchSceneOffer {
  PreparedSketchSceneDecision decision;
  bool replacedPending = false;
  bool operator==(const PreparedSketchSceneOffer &) const = default;
};

struct SketchSynchronizationMetrics {
  std::uint64_t calls = 0;
  std::uint64_t preparedPacketInstalls = 0;
  std::uint64_t scalablePreparations = 0;
  bool operator==(const SketchSynchronizationMetrics &) const = default;
};

struct SketchItemPickEvidence {
  SketchItemPickEvidence(render::SceneStamp sceneStamp,
                         SketchProductStamp productStamp)
      : scene(std::move(sceneStamp)), product(std::move(productStamp)) {}

  render::SceneStamp scene;
  SketchProductStamp product;
  std::shared_ptr<const SketchSceneProducts> products;
  std::optional<render::SceneStamp> latestAcceptedScene;
  std::uint64_t cameraGeneration = 0;
  QSizeF viewportLogical;
  SketchPickCoveragePolicy pickCoverage;
  render::Point2d canonicalPoint;
  double canonicalToleranceMetres = 0.0;
  bool matchesLatestAcceptedScene = false;
  std::optional<render::SketchPickResult> hit;
  std::optional<double> displayedDistanceLogicalPixels;
  render::SketchPickMetrics analyticMetrics;
  std::uint32_t renderedSpanProbes = 0U;
  std::uint32_t renderedCurveEvaluations = 0U;
  std::shared_ptr<const PresentedSketchFrame> presented;
};

class SketchScenePresenter final {
public:
  SketchScenePresenter();
  SketchScenePresenter(const SketchScenePresenter &) = delete;
  SketchScenePresenter &operator=(const SketchScenePresenter &) = delete;

  void retarget(render::SceneTarget desired);
  [[nodiscard]] Result<PreparedSketchSceneOffer>
  publish(std::shared_ptr<const PreparedSketchProducts> prepared);
  [[nodiscard]] Result<SketchCameraDecision>
  publishCamera(SketchCamera2d camera);
  [[nodiscard]] Result<SketchPickCoverageDecision>
  publishPickCoverage(SketchPickCoveragePolicy policy);
  // Called during Qt Quick synchronization on the render thread. Publication
  // only swaps immutable pointers and builds a constant-cost view transform.
  [[nodiscard]] Result<std::shared_ptr<const SynchronizedSketchScene>>
  synchronize(QSizeF viewportLogical);

  [[nodiscard]] Result<SketchItemPickEvidence> pick(
      QPointF itemLogical, double toleranceLogicalPixels,
      render::SketchPickTargets targets = render::SketchPickTargets::All) const;
  [[nodiscard]] Result<SketchItemPickEvidence> pick(
      std::shared_ptr<const SynchronizedSketchScene> synchronized,
      QPointF itemLogical, double toleranceLogicalPixels,
      render::SketchPickTargets targets = render::SketchPickTargets::All) const;
  [[nodiscard]] std::shared_ptr<const SynchronizedSketchScene> current() const;
  [[nodiscard]] Result<std::shared_ptr<const void>> retirementOwner() const;
  void clear();
  [[nodiscard]] SketchSynchronizationMetrics synchronizationMetrics() const;
  [[nodiscard]] std::size_t pendingCount() const;

private:
  mutable std::mutex stateMutex_;
  std::optional<render::SceneTarget> desired_;
  std::optional<render::SceneStamp> latestAcceptedScene_;
  std::shared_ptr<const PreparedSketchProducts> pending_;
  SketchCamera2d camera_;
  SketchPickCoveragePolicy pickCoverage_;
  SketchSynchronizationMetrics synchronizationMetrics_;
  std::atomic<std::shared_ptr<const SynchronizedSketchScene>> current_;
};

} // namespace kearne::ui
