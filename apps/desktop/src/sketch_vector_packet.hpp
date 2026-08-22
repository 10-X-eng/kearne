#pragma once

#include <kearne/render/sketch_scene.hpp>
#include <kearne/sketch/nurbs.hpp>

#include <QMatrix4x4>
#include <QPointF>
#include <QSizeF>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
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
  static constexpr std::uint32_t defaultMaximumCurveEvaluations = 16'384U;
  static constexpr std::uint32_t defaultMaximumResidentSpanProbes = 4096U;
  static constexpr std::uint32_t maximumConfigurableCurveEvaluations = 65'536U;
  static constexpr std::uint32_t maximumConfigurableResidentSpanProbes =
      16'384U;

  std::uint64_t generation = 1;
  double maximumToleranceLogicalPixels = defaultMaximumToleranceLogicalPixels;
  std::uint32_t maximumCurveEvaluations = defaultMaximumCurveEvaluations;
  std::uint32_t maximumResidentSpanProbes = defaultMaximumResidentSpanProbes;
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
  itemMatrix(render::Point2d packetOriginMetres) const;
  [[nodiscard]] Result<SketchGpuView>
  gpuView(render::Point2d packetOriginMetres) const;

private:
  SketchViewTransform(SketchCamera2d camera, QSizeF viewportLogical,
                      double cosine, double sine);

  SketchCamera2d camera_;
  QSizeF viewportLogical_;
  double cosine_ = 1.0;
  double sine_ = 0.0;
};

enum class SketchVectorKind : std::uint32_t {
  Point = 1,
  Line = 2,
  Circle = 3,
  Arc = 4,
  Ellipse = 5,
  EllipticalArc = 6,
  HyperbolicArc = 7,
  ParabolicArc = 8,
  BSpline = 9,
  Glyph = 10,
  Text = 11,
  Dimension = 12,
};

enum class SketchVectorShaderFamily : std::uint8_t {
  Basic = 1,
  NurbsLowDegree = 2,
  NurbsGeneral = 3,
  Annotation = 4,
};

// std430-compatible. Coordinates are relative to the packet origin and split
// into high/low floats. Bounds are native curve bounds, never polygon points.
struct alignas(16) SketchVectorRecord {
  std::array<std::uint32_t, 4> meta{};  // kind, data offset, degree, source key
  std::array<float, 4> boundsMinimum{}; // x high, y high, x low, y low
  std::array<float, 4> boundsMaximum{};
  std::array<float, 4> first{};
  std::array<float, 4> second{};
  // Curves: radii/start/sweep. Text: width/height/x-offset/y-offset pixels.
  std::array<float, 4> shape{};
  std::array<float, 4> domain{}; // rotation, first parameter, last, path start
  std::array<float, 4> appearance{}; // stroke, point, dash-on, dash-period px
  bool operator==(const SketchVectorRecord &) const = default;
};

struct alignas(16) SketchVectorData {
  std::array<float, 4> value{};
  bool operator==(const SketchVectorData &) const = default;
};

struct SketchVectorPacketMetrics {
  std::size_t inputPrimitives = 0U;
  std::size_t visiblePrimitives = 0U;
  std::size_t records = 0U;
  std::size_t dataRecords = 0U;
  std::size_t chunks = 0U;
  std::size_t retainedBytes = 0U;
  std::size_t scratchBytes = 0U;
  std::size_t peakBytes = 0U;
  bool operator==(const SketchVectorPacketMetrics &) const = default;
};

struct SketchVectorUploadOptions {
  static constexpr std::size_t defaultMaximumRetainedBytes =
      512U * 1024U * 1024U;
  static constexpr std::size_t defaultMaximumScratchBytes =
      1024U * 1024U * 1024U;
  static constexpr std::size_t defaultMaximumPeakBytes = 1536U * 1024U * 1024U;

  std::size_t maximumChunkBytes = 1U * 1024U * 1024U;
  std::size_t maximumChunks = 1'000'000U;
  std::size_t maximumRecords = 8'000'000U;
  std::size_t maximumDataRecords = 32'000'000U;
  double spatialTileMetres = 0.5;
  std::size_t maximumRetainedBytes = defaultMaximumRetainedBytes;
  std::size_t maximumScratchBytes = defaultMaximumScratchBytes;
  std::size_t maximumPeakBytes = defaultMaximumPeakBytes;
};

struct SketchVectorPrimitiveSpanRecord {
  std::uint32_t sourceKey = 0U;
  std::uint32_t chunk = 0U;
  std::uint32_t firstRecord = 0U;
  std::uint32_t recordCount = 0U;
  bool operator==(const SketchVectorPrimitiveSpanRecord &) const = default;
};

struct SketchVectorBounds {
  double minimumX = 0.0;
  double minimumY = 0.0;
  double maximumX = 0.0;
  double maximumY = 0.0;
  bool empty = true;
  double maximumScreenRadiusLogicalPixels = 0.0;

  [[nodiscard]] bool intersects(const SketchVectorBounds &other,
                                double expansionMetres = 0.0) const;
  bool operator==(const SketchVectorBounds &) const = default;
};

class SketchVectorChunk final {
public:
  [[nodiscard]] SketchVectorShaderFamily shaderFamily() const {
    return shaderFamily_;
  }
  [[nodiscard]] std::uint16_t style() const { return style_; }
  [[nodiscard]] std::uint16_t layer() const { return layer_; }
  [[nodiscard]] std::span<const SketchVectorRecord> records() const {
    return records_;
  }
  [[nodiscard]] std::span<const SketchVectorData> data() const { return data_; }
  [[nodiscard]] const SketchVectorBounds &bounds() const { return bounds_; }
  [[nodiscard]] std::size_t payloadBytes() const { return payloadBytes_; }

private:
  SketchVectorChunk(SketchVectorShaderFamily shaderFamily, std::uint16_t style,
                    std::uint16_t layer,
                    std::vector<SketchVectorRecord> records,
                    std::vector<SketchVectorData> data,
                    SketchVectorBounds bounds, std::size_t payloadBytes,
                    std::uint64_t contentHash);

  [[nodiscard]] std::uint64_t contentHash() const { return contentHash_; }

  SketchVectorShaderFamily shaderFamily_ = SketchVectorShaderFamily::Basic;
  std::uint16_t style_ = 0U;
  std::uint16_t layer_ = 0U;
  std::vector<SketchVectorRecord> records_;
  std::vector<SketchVectorData> data_;
  SketchVectorBounds bounds_;
  std::size_t payloadBytes_ = 0U;
  std::uint64_t contentHash_ = 0U;

  friend class SketchVectorPacket;
  friend struct SketchVectorPacketBuildAccess;
};

class SketchVectorPacket final {
public:
  [[nodiscard]]
  std::span<const std::shared_ptr<const SketchVectorChunk>> chunks() const {
    return chunks_;
  }
  [[nodiscard]] std::span<const render::SketchStyle> styles() const {
    return styles_;
  }
  [[nodiscard]] render::Point2d originMetres() const { return originMetres_; }
  [[nodiscard]] const SketchVectorPacketMetrics &metrics() const {
    return metrics_;
  }
  [[nodiscard]] std::size_t maximumChunkBytes() const {
    return maximumChunkBytes_;
  }
  [[nodiscard]] bool
  requiresShaderFamily(SketchVectorShaderFamily family) const {
    return (shaderFamilyMask_ & static_cast<std::uint8_t>(
                                    1U << static_cast<std::uint8_t>(family))) !=
           0U;
  }
  [[nodiscard]] Result<std::vector<std::uint32_t>>
  visibleChunks(const SketchViewTransform &transform,
                SketchPickCoveragePolicy pickCoverage = {}) const;

private:
  struct SpatialNode {
    SketchVectorBounds bounds;
    std::uint32_t first = 0U;
    std::uint32_t second = 0U;
    bool leaf = false;
  };

  SketchVectorPacket(
      render::Point2d originMetres, std::vector<render::SketchStyle> styles,
      std::vector<std::shared_ptr<const SketchVectorChunk>> chunks,
      std::vector<SpatialNode> spatialIndex, std::uint32_t spatialRoot,
      std::size_t maximumChunkBytes, SketchVectorPacketMetrics metrics,
      std::uint8_t shaderFamilyMask);

  render::Point2d originMetres_;
  std::vector<render::SketchStyle> styles_;
  std::vector<std::shared_ptr<const SketchVectorChunk>> chunks_;
  std::vector<SpatialNode> spatialIndex_;
  std::uint32_t spatialRoot_ = 0U;
  std::size_t maximumChunkBytes_ = 0U;
  SketchVectorPacketMetrics metrics_;
  std::uint8_t shaderFamilyMask_ = 0U;

  friend struct SketchVectorPacketBuildAccess;
  friend class SketchChunkSequence;
};

class SketchChunkSequence final {
public:
  SketchChunkSequence() = default;
  SketchChunkSequence(SketchChunkSequence &&) noexcept = default;
  SketchChunkSequence &operator=(SketchChunkSequence &&) noexcept = default;
  SketchChunkSequence(const SketchChunkSequence &) = delete;
  SketchChunkSequence &operator=(const SketchChunkSequence &) = delete;

  [[nodiscard]] std::uint32_t operator[](std::size_t index) const {
    return chunks_[index];
  }
  [[nodiscard]] std::size_t size() const { return chunks_.size(); }
  [[nodiscard]] bool empty() const { return chunks_.empty(); }
  [[nodiscard]] std::size_t retainedOrderBytes() const {
    return chunks_.capacity() * sizeof(std::uint32_t);
  }

private:
  [[nodiscard]] static Result<SketchChunkSequence>
  create(const SketchVectorPacket &packet,
         std::span<const std::uint32_t> chunks);
  [[nodiscard]] bool contains(std::uint32_t chunk) const;

  const SketchVectorPacket *packet_ = nullptr;
  std::vector<std::uint32_t> chunks_;
  std::vector<std::uint64_t> membership_;

  friend class ProgressiveSketchVisibility;
  friend class ProgressiveSketchUpload;
  friend class SketchPresentedChunkCoverage;
};

struct SketchVisibilitySlice {
  std::vector<std::uint32_t> chunks;
  std::size_t spatialNodesVisited = 0U;
};

class ProgressiveSketchVisibility final {
public:
  [[nodiscard]] static Result<ProgressiveSketchVisibility>
  create(std::shared_ptr<const SketchVectorPacket> packet,
         const SketchViewTransform &transform,
         SketchPickCoveragePolicy pickCoverage = {});
  [[nodiscard]] Result<SketchVisibilitySlice>
  takeNextSlice(std::size_t maximumSpatialNodes,
                std::size_t maximumVisibleChunks);
  [[nodiscard]] bool complete() const { return cursor_ == selected_.size(); }
  [[nodiscard]] const SketchChunkSequence &selectedChunks() const {
    return sequence_;
  }
  [[nodiscard]] SketchChunkSequence releaseSelectedChunks() {
    return std::move(sequence_);
  }
  [[nodiscard]] std::size_t spatialNodesVisited() const {
    return spatialNodesVisited_;
  }

private:
  std::shared_ptr<const SketchVectorPacket> packet_;
  std::vector<std::uint32_t> selected_;
  SketchChunkSequence sequence_;
  std::size_t cursor_ = 0U;
  std::size_t spatialNodesVisited_ = 0U;
};

class SketchPresentedChunkCoverage final {
public:
  static constexpr std::size_t defaultMaximumRetainedBytes = 4U * 1024U * 1024U;

  [[nodiscard]] static Result<
      std::shared_ptr<const SketchPresentedChunkCoverage>>
  create(const SketchVectorPacket &packet, std::vector<std::uint32_t> chunks,
         std::size_t maximumRetainedBytes = defaultMaximumRetainedBytes);
  [[nodiscard]] static Result<
      std::shared_ptr<const SketchPresentedChunkCoverage>>
  create(const SketchVectorPacket &packet, SketchChunkSequence &chunks,
         std::size_t maximumRetainedBytes = defaultMaximumRetainedBytes);
  [[nodiscard]] bool contains(std::uint32_t chunk) const;
  [[nodiscard]] std::size_t size() const { return size_; }
  [[nodiscard]] double maximumScreenRadiusLogicalPixels() const {
    return maximumScreenRadiusLogicalPixels_;
  }
  [[nodiscard]] std::size_t retainedBytes() const { return retainedBytes_; }

private:
  SketchPresentedChunkCoverage(std::vector<std::uint64_t> membership,
                               std::size_t size, double maximumScreenRadius,
                               std::size_t retainedBytes)
      : membership_(std::move(membership)), size_(size),
        maximumScreenRadiusLogicalPixels_(maximumScreenRadius),
        retainedBytes_(retainedBytes) {}

  std::vector<std::uint64_t> membership_;
  std::size_t size_ = 0U;
  double maximumScreenRadiusLogicalPixels_ = 0.0;
  std::size_t retainedBytes_ = 0U;
};

struct SketchVectorSourcePrimitive {
  SketchVectorSourcePrimitive() = default;
  SketchVectorSourcePrimitive(
      std::uint32_t requestedSourceKey, std::uint16_t requestedStyle,
      SketchVectorKind requestedKind, bool requestedVisible,
      render::Point2d requestedFirst, render::Point2d requestedSecond = {},
      double requestedRadius = 0.0, double requestedStartAngleRadians = 0.0,
      double requestedSweepAngleRadians = 0.0,
      std::uint16_t requestedGlyph = 0U, double requestedSecondaryRadius = 0.0,
      double requestedRotationAngleRadians = 0.0)
      : sourceKey(requestedSourceKey), style(requestedStyle),
        kind(requestedKind), visible(requestedVisible), first(requestedFirst),
        second(requestedSecond), radius(requestedRadius),
        startAngleRadians(requestedStartAngleRadians),
        sweepAngleRadians(requestedSweepAngleRadians), glyph(requestedGlyph),
        secondaryRadius(requestedSecondaryRadius),
        rotationAngleRadians(requestedRotationAngleRadians) {}

  std::uint32_t sourceKey = 0U;
  std::uint16_t style = 0U;
  SketchVectorKind kind = SketchVectorKind::Point;
  bool visible = false;
  render::Point2d first;
  render::Point2d second;
  double radius = 0.0;
  double startAngleRadians = 0.0;
  double sweepAngleRadians = 0.0;
  std::uint16_t glyph = 0U;
  double secondaryRadius = 0.0;
  double rotationAngleRadians = 0.0;
  double screenOffsetXLogicalPixels = 0.0;
  double screenOffsetYLogicalPixels = 0.0;
  std::array<std::uint8_t, 32> text{};
  std::uint8_t textLength = 0U;
  render::Point2d third;
};

struct SketchVectorSourceBounds {
  render::Point2d minimum;
  render::Point2d maximum;
  bool empty = true;
};

struct SketchVectorSource {
  using PrimitiveAt = SketchVectorSourcePrimitive (*)(const void *,
                                                      std::size_t) noexcept;
  using SplineAt = sketch::NurbsView (*)(const void *, std::size_t) noexcept;

  std::span<const render::SketchStyle> styles;
  const void *primitiveContext = nullptr;
  std::size_t primitiveCount = 0U;
  PrimitiveAt primitiveAt = nullptr;
  SketchVectorSourceBounds bounds;
  SplineAt splineAt = nullptr;
};

struct SketchVectorPacketBuildOutput {
  SketchVectorPacket packet;
  std::vector<SketchVectorPrimitiveSpanRecord> provenance;
  std::size_t indexedPrimitives = 0U;
  std::size_t retainedBytes = 0U;
  std::size_t scratchBytes = 0U;
  std::size_t peakBytes = 0U;
};

[[nodiscard]] Result<SketchVectorSourceBounds>
sketchVectorPrimitiveBounds(const SketchVectorSourcePrimitive &primitive);
[[nodiscard]] Result<SketchVectorSourceBounds>
sketchVectorPrimitiveBounds(const SketchVectorSourcePrimitive &primitive,
                            sketch::NurbsView spline);

struct SketchVectorPacketBuildAccess {
  [[nodiscard]] static Result<SketchVectorPacketBuildOutput>
  build(const SketchVectorSource &source,
        SketchVectorUploadOptions options = {},
        std::shared_ptr<const SketchVectorPacket> reuse = {},
        std::stop_token cancellation = {});
};

} // namespace kearne::ui
