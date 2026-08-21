#pragma once

#include <kearne/render/sketch_scene.hpp>

#include <QMatrix4x4>
#include <QPointF>
#include <QSizeF>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <numbers>
#include <span>
#include <stop_token>
#include <vector>

namespace kearne::ui {

struct SketchCamera2d {
  std::uint64_t generation = 1;
  render::Point2d centerMetres;
  double metresPerLogicalPixel = 0.001;
  double rotationRadians = 0.0;
  bool operator==(const SketchCamera2d &) const = default;
};

struct SketchGpuView {
  float centerOffsetX = 0.0F;
  float centerOffsetY = 0.0F;
  float centerOffsetXLow = 0.0F;
  float centerOffsetYLow = 0.0F;
  float metresPerLogicalPixel = 0.0F;
  float cosine = 1.0F;
  float sine = 0.0F;
  bool operator==(const SketchGpuView &) const = default;
};

enum class SketchCameraDecision : std::uint8_t {
  Accepted = 1,
  Duplicate = 2,
  StaleGeneration = 3,
  GenerationConflict = 4,
};

struct SketchPickCoveragePolicy {
  static constexpr double defaultMaximumToleranceLogicalPixels = 16.0;
  static constexpr double maximumConfigurableToleranceLogicalPixels = 64.0;
  static constexpr std::uint32_t defaultMaximumRenderedTriangleTests = 16'384U;
  static constexpr std::uint32_t defaultMaximumRenderedSpanProbes = 4096U;
  static constexpr std::uint32_t defaultMaximumPatternIntervals = 4096U;
  static constexpr std::uint32_t maximumConfigurableRenderedTriangleTests =
      65'536U;
  static constexpr std::uint32_t maximumConfigurableRenderedSpanProbes =
      16'384U;
  static constexpr std::uint32_t maximumConfigurablePatternIntervals = 65'536U;

  std::uint64_t generation = 1;
  double maximumToleranceLogicalPixels = defaultMaximumToleranceLogicalPixels;
  std::uint32_t maximumRenderedTriangleTests =
      defaultMaximumRenderedTriangleTests;
  std::uint32_t maximumRenderedSpanProbes = defaultMaximumRenderedSpanProbes;
  std::uint32_t maximumPatternIntervals = defaultMaximumPatternIntervals;
  bool operator==(const SketchPickCoveragePolicy &) const = default;
};

enum class SketchPickCoverageDecision : std::uint8_t {
  Accepted = 1,
  Duplicate = 2,
  StaleGeneration = 3,
  GenerationConflict = 4,
};

class SketchViewTransform final {
public:
  [[nodiscard]] static Result<SketchViewTransform>
  create(SketchCamera2d camera, QSizeF viewportLogical);

  [[nodiscard]] const SketchCamera2d &camera() const { return camera_; }
  [[nodiscard]] QSizeF viewportLogical() const { return viewportLogical_; }
  [[nodiscard]] QPointF toItem(render::Point2d canonicalMetres) const;
  [[nodiscard]] render::Point2d toCanonical(QPointF itemLogical) const;
  [[nodiscard]] Result<QMatrix4x4>
  itemMatrix(render::Point2d sceneOriginMetres) const;
  [[nodiscard]] Result<SketchGpuView>
  gpuView(render::Point2d sceneOriginMetres) const;

private:
  SketchViewTransform(SketchCamera2d camera, QSizeF viewportLogical,
                      double cosine, double sine);

  SketchCamera2d camera_;
  QSizeF viewportLogical_;
  double cosine_ = 1.0;
  double sine_ = 0.0;
};

struct SketchMeshVertex {
  float x = 0.0F;
  float y = 0.0F;
  float xLow = 0.0F;
  float yLow = 0.0F;
  float extrusionX = 0.0F;
  float extrusionY = 0.0F;
  float pathDistanceMetres = 0.0F;
  float coverageDistancePixels = 0.0F;
  float coverageRadiusPixels = 0.0F;
  float patternOnLogicalPixels = 0.0F;
  float patternPeriodLogicalPixels = 0.0F;
  bool operator==(const SketchMeshVertex &) const = default;
};

// Memory metrics include owned objects and container capacities. They exclude
// allocator metadata and conservatively combine non-overlapping preparation
// phases, so a configured byte ceiling is never understated.
struct SketchMeshMetrics {
  std::size_t inputPrimitives = 0;
  std::size_t visiblePrimitives = 0;
  std::size_t batches = 0;
  std::size_t vertices = 0;
  std::size_t indices = 0;
  std::size_t bytes = 0;
  std::size_t retainedMeshBytes = 0;
  std::size_t preparationScratchBytes = 0;
  std::size_t peakPreparationMeshBytes = 0;
  bool operator==(const SketchMeshMetrics &) const = default;
};

struct SketchCurveLod {
  int scaleExponent = -14;
  bool operator==(const SketchCurveLod &) const = default;

  [[nodiscard]] static SketchCurveLod
  forMetresPerLogicalPixel(double metresPerLogicalPixel);
  [[nodiscard]] double maximumChordErrorMetres() const;
};

struct SketchTessellationOptions {
  double maximumArcStepRadians = std::numbers::pi / 24.0;
  std::size_t minimumCircleSegments = 32;
  std::size_t maximumCurveSegments = 4096;
  std::size_t maximumVertices = 8'000'000;
  std::size_t maximumIndices = 12'000'000;
};

struct SketchUploadOptions {
  static constexpr std::size_t defaultMaximumRetainedMeshBytes =
      512U * 1024U * 1024U;
  static constexpr std::size_t defaultMaximumPreparationScratchBytes =
      1024U * 1024U * 1024U;
  static constexpr std::size_t defaultMaximumPreparationPeakBytes =
      1536U * 1024U * 1024U;
  static constexpr std::size_t maximumConfigurableRetainedMeshBytes =
      2048ULL * 1024ULL * 1024ULL;
  static constexpr std::size_t maximumConfigurablePreparationScratchBytes =
      4096ULL * 1024ULL * 1024ULL;
  static constexpr std::size_t maximumConfigurablePreparationPeakBytes =
      6144ULL * 1024ULL * 1024ULL;

  std::size_t maximumChunkBytes = 1U * 1024U * 1024U;
  std::size_t maximumChunks = 1'000'000U;
  double spatialTileLogicalPixels = 512.0;
  std::size_t maximumRetainedMeshBytes = defaultMaximumRetainedMeshBytes;
  std::size_t maximumPreparationScratchBytes =
      defaultMaximumPreparationScratchBytes;
  std::size_t maximumPreparationPeakBytes = defaultMaximumPreparationPeakBytes;
};

struct SketchStrokePrimitiveSpanRecord {
  std::uint32_t sourceKey = 0;
  std::uint32_t chunk = 0;
  std::uint32_t firstIndex = 0;
  std::uint32_t indexCount = 0;
  bool operator==(const SketchStrokePrimitiveSpanRecord &) const = default;
};

struct SketchChunkBounds {
  double minimumX = 0.0;
  double minimumY = 0.0;
  double maximumX = 0.0;
  double maximumY = 0.0;
  bool empty = true;
  double maximumExtrusionLogicalPixels = 0.0;
  double maximumAnalyticDeviationMetres = 0.0;
  double maximumPathDistanceMetres = 0.0;
  double maximumPatternedPathDistanceMetres = 0.0;

  [[nodiscard]] bool intersects(const SketchChunkBounds &other,
                                double extrusionMetres = 0.0) const;
  bool operator==(const SketchChunkBounds &) const = default;
};

class SketchUploadChunk final {
public:
  [[nodiscard]] std::uint16_t style() const { return style_; }
  [[nodiscard]] std::uint16_t layer() const { return layer_; }
  [[nodiscard]] std::span<const SketchMeshVertex> vertices() const {
    return vertices_;
  }
  [[nodiscard]] std::span<const std::uint32_t> indices() const {
    return indices_;
  }
  [[nodiscard]] const SketchChunkBounds &bounds() const { return bounds_; }
  [[nodiscard]] std::size_t payloadBytes() const { return payloadBytes_; }

private:
  SketchUploadChunk(std::uint16_t style, std::uint16_t layer,
                    std::vector<SketchMeshVertex> vertices,
                    std::vector<std::uint32_t> indices,
                    SketchChunkBounds bounds, std::size_t payloadBytes,
                    std::uint64_t contentHash);

  [[nodiscard]] std::uint64_t contentHash() const { return contentHash_; }

  std::uint16_t style_ = 0;
  std::uint16_t layer_ = 0;
  std::vector<SketchMeshVertex> vertices_;
  std::vector<std::uint32_t> indices_;
  SketchChunkBounds bounds_;
  std::size_t payloadBytes_ = 0;
  std::uint64_t contentHash_ = 0;

  friend class SketchSceneMesh;
  friend struct SketchStrokeMeshBuildAccess;
};

class ProgressiveSketchVisibility;
class ProgressiveSketchUpload;
class SketchSceneMesh;

struct SketchVisibleChunkSelection {
  std::vector<std::uint32_t> chunks;
  std::size_t spatialNodesVisited = 0;
};

struct SketchVisibilitySlice {
  std::vector<std::uint32_t> chunks;
  std::size_t spatialNodesVisited = 0;
};

class SketchChunkSequence final {
public:
  SketchChunkSequence() = default;
  SketchChunkSequence(SketchChunkSequence &&) noexcept = default;
  SketchChunkSequence &operator=(SketchChunkSequence &&) noexcept = default;
  SketchChunkSequence(const SketchChunkSequence &) = delete;
  SketchChunkSequence &operator=(const SketchChunkSequence &) = delete;

  [[nodiscard]] std::uint32_t operator[](std::size_t index) const;
  [[nodiscard]] std::size_t size() const { return size_; }
  [[nodiscard]] bool empty() const { return size_ == 0U; }
  [[nodiscard]] std::size_t retainedOrderBytes() const;

private:
  [[nodiscard]] static Result<SketchChunkSequence>
  create(const SketchSceneMesh &mesh, std::size_t maximumMembershipBytes);
  [[nodiscard]] Result<void> push_back(std::uint32_t chunk);

  static constexpr std::size_t firstBlockSize = 64U;
  std::vector<std::unique_ptr<std::uint32_t[]>> blocks_;
  std::vector<std::uint64_t> membershipWords_;
  std::size_t size_ = 0;
  std::size_t capacity_ = 0;
  std::size_t maximumSize_ = 0;
  const SketchSceneMesh *meshIdentity_ = nullptr;
  double maximumExtrusionLogicalPixels_ = 0.0;
  double maximumAnalyticDeviationMetres_ = 0.0;
  double maximumPatternedPathDistanceMetres_ = 0.0;

  friend class SketchPresentedChunkCoverage;
  friend class ProgressiveSketchVisibility;
  friend class ProgressiveSketchUpload;
};

class SketchSceneMesh final {
public:
  [[nodiscard]]
  std::span<const std::shared_ptr<const SketchUploadChunk>> chunks() const {
    return chunks_;
  }
  [[nodiscard]] std::span<const render::SketchStyle> styles() const {
    return styles_;
  }
  [[nodiscard]] const SketchMeshMetrics &metrics() const { return metrics_; }
  [[nodiscard]] render::Point2d originMetres() const { return originMetres_; }
  [[nodiscard]] SketchCurveLod lod() const { return lod_; }
  [[nodiscard]] std::size_t maximumChunkBytes() const {
    return maximumChunkBytes_;
  }
  [[nodiscard]] double spatialTileSizeMetres() const {
    return spatialTileSizeMetres_;
  }
  [[nodiscard]] Result<void>
  validatePatternedPhase(const SketchGpuView &view) const;
  [[nodiscard]] Result<SketchVisibleChunkSelection>
  visibleChunks(const SketchViewTransform &transform,
                SketchPickCoveragePolicy pickCoverage = {}) const;

private:
  struct SpatialNode {
    SketchChunkBounds bounds;
    std::uint32_t first = 0;
    std::uint32_t second = 0;
    bool leaf = false;
  };

  SketchSceneMesh(render::Point2d originMetres, SketchCurveLod lod,
                  std::vector<render::SketchStyle> styles,
                  std::vector<std::shared_ptr<const SketchUploadChunk>> chunks,
                  std::vector<SpatialNode> spatialIndex,
                  std::uint32_t spatialRoot, std::size_t maximumChunkBytes,
                  double spatialTileSizeMetres, SketchMeshMetrics metrics);

  render::Point2d originMetres_;
  SketchCurveLod lod_;
  std::vector<render::SketchStyle> styles_;
  std::vector<std::shared_ptr<const SketchUploadChunk>> chunks_;
  std::vector<SpatialNode> spatialIndex_;
  std::uint32_t spatialRoot_ = 0;
  std::size_t maximumChunkBytes_ = 0;
  double spatialTileSizeMetres_ = 0.0;
  SketchMeshMetrics metrics_;

  friend class ProgressiveSketchVisibility;
  friend struct SketchStrokeMeshBuildAccess;
};

class SketchPresentedChunkCoverage final {
public:
  static constexpr std::size_t defaultMaximumRetainedBytes = 4U * 1024U * 1024U;

  [[nodiscard]] static Result<
      std::shared_ptr<const SketchPresentedChunkCoverage>>
  create(const SketchSceneMesh &mesh, std::vector<std::uint32_t> chunks,
         std::size_t maximumRetainedBytes = defaultMaximumRetainedBytes);
  [[nodiscard]] static Result<
      std::shared_ptr<const SketchPresentedChunkCoverage>>
  create(const SketchSceneMesh &mesh, SketchChunkSequence &chunks,
         std::size_t maximumRetainedBytes = defaultMaximumRetainedBytes);
  [[nodiscard]] bool contains(std::uint32_t chunk) const;
  [[nodiscard]] std::size_t size() const { return size_; }
  [[nodiscard]] double maximumExtrusionLogicalPixels() const {
    return maximumExtrusionLogicalPixels_;
  }
  [[nodiscard]] std::size_t retainedBytes() const { return retainedBytes_; }
  [[nodiscard]] double maximumAnalyticDeviationMetres() const {
    return maximumAnalyticDeviationMetres_;
  }
  [[nodiscard]] double maximumPatternedPathDistanceMetres() const {
    return maximumPatternedPathDistanceMetres_;
  }

private:
  SketchPresentedChunkCoverage(std::vector<std::uint64_t> membershipWords,
                               std::size_t size,
                               double maximumExtrusionLogicalPixels,
                               double maximumAnalyticDeviationMetres,
                               double maximumPatternedPathDistanceMetres,
                               std::size_t retainedBytes);

  std::vector<std::uint64_t> membershipWords_;
  std::size_t size_ = 0U;
  double maximumExtrusionLogicalPixels_ = 0.0;
  double maximumAnalyticDeviationMetres_ = 0.0;
  double maximumPatternedPathDistanceMetres_ = 0.0;
  std::size_t retainedBytes_ = 0U;
};

class ProgressiveSketchVisibility final {
public:
  [[nodiscard]] static Result<ProgressiveSketchVisibility>
  create(std::shared_ptr<const SketchSceneMesh> mesh,
         const SketchViewTransform &transform,
         SketchPickCoveragePolicy pickCoverage = {});

  [[nodiscard]] Result<SketchVisibilitySlice>
  takeNextSlice(std::size_t maximumSpatialNodes,
                std::size_t maximumVisibleChunks);
  [[nodiscard]] bool complete() const { return pendingNodes_.empty(); }
  [[nodiscard]] const SketchChunkSequence &selectedChunks() const {
    return selectedChunks_;
  }
  [[nodiscard]] SketchChunkSequence releaseSelectedChunks();
  [[nodiscard]] std::size_t spatialNodesVisited() const {
    return spatialNodesVisited_;
  }

private:
  ProgressiveSketchVisibility(std::shared_ptr<const SketchSceneMesh> mesh,
                              SketchChunkBounds visible,
                              std::vector<std::uint32_t> pendingNodes,
                              SketchChunkSequence selectedChunks,
                              double transformMetresPerLogicalPixel);

  std::shared_ptr<const SketchSceneMesh> mesh_;
  SketchChunkBounds visible_;
  std::vector<std::uint32_t> pendingNodes_;
  SketchChunkSequence selectedChunks_;
  std::size_t spatialNodesVisited_ = 0;
  double transformMetresPerLogicalPixel_ = 0.0;
};

} // namespace kearne::ui
