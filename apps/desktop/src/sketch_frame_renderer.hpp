#pragma once

#include "sketch_scene_projection.hpp"

#include <QRectF>
#include <QSGRenderNode>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>

class QQuickWindow;

namespace kearne::ui {

struct SketchGpuUploadPolicy {
  static constexpr std::size_t maximumChunkBytes = 1U * 1024U * 1024U;
  static constexpr std::size_t maximumBytesPerFrame = 2U * 1024U * 1024U;
  static constexpr std::size_t maximumChunksPerFrame = 32U;
  static constexpr std::size_t maximumSpatialNodesPerFrame = 128U;
  static constexpr std::size_t maximumVisibleChunksPerFrame = 64U;
  static constexpr std::size_t maximumRetiredLayers = 2U;
  static constexpr std::uint64_t frameBudgetNanoseconds = 16'666'667U;
};

struct SketchPipelineWarmup {
  bool lowDegreeNurbs = false;
  bool generalNurbs = false;
  bool operator==(const SketchPipelineWarmup &) const = default;
};

struct SketchGpuUploadMetrics {
  std::uint64_t slices = 0;
  std::uint64_t chunksUploaded = 0;
  std::uint64_t coherentSwaps = 0;
  std::uint64_t reusedChunks = 0;
  std::uint64_t canceledStagings = 0;
  std::uint64_t rejectedPackets = 0;
  std::uint64_t chunksRetired = 0;
  std::uint64_t frameBudgetExceeded = 0;
  std::uint64_t visibilitySlices = 0;
  std::uint64_t visibilityNodesVisited = 0;
  std::uint64_t cpuReclaimsAccepted = 0;
  std::uint64_t cpuReclaimsReleased = 0;
  std::uint64_t cpuReclaimsSaturated = 0;
  std::uint64_t cpuReclaimsContended = 0;
  std::uint64_t lastSliceNanoseconds = 0;
  std::uint64_t maximumSliceNanoseconds = 0;
  std::size_t lastSliceBytes = 0;
  std::size_t maximumSliceBytes = 0;
  std::size_t lastSliceChunks = 0;
  std::size_t maximumSliceChunks = 0;
  std::size_t maximumNodeOperationsPerFrame = 0;
  std::size_t maximumVisibilityNodesPerFrame = 0;
  std::size_t maximumVisibleChunksSelectedPerFrame = 0;
  std::size_t maximumPresentedChunks = 0;
  std::size_t maximumStagingChunks = 0;
  std::size_t maximumRetiredChunks = 0;
  std::size_t maximumRetiredLayers = 0;
  std::size_t maximumPresentedReferencedBytes = 0;
  std::size_t maximumStagingReferencedBytes = 0;
  std::size_t maximumPresentedChunkSequenceBytes = 0;
  std::size_t maximumStagingChunkSequenceBytes = 0;
  std::size_t maximumRetiredReferencedBytes = 0;
  std::size_t maximumCpuReclaimQueue = 0;
  std::size_t cpuReclaimsOutstanding = 0;
  std::size_t bytesUploaded = 0;
  bool operator==(const SketchGpuUploadMetrics &) const = default;
};

class SketchFrameRendererState final {
public:
  [[nodiscard]] std::shared_ptr<const PresentedSketchFrame> presented() const;
  [[nodiscard]] SketchVectorPacketMetrics vectorPacketMetrics() const;
  [[nodiscard]] SketchGpuUploadMetrics uploadMetrics() const;
  [[nodiscard]] std::uint64_t geometryBuildCount() const;
  [[nodiscard]] Diagnostic lastDiagnostic() const;
  [[nodiscard]] bool pipelineReady(SketchPipelineWarmup warmup) const noexcept;
  void invalidate() noexcept;

private:
  void publish(std::shared_ptr<const PresentedSketchFrame> frame,
               SketchVectorPacketMetrics vectorPacketMetrics,
               SketchGpuUploadMetrics uploadMetrics,
               std::uint64_t geometryBuildCount, bool clearDiagnostic,
               SketchPipelineWarmup readyPipelines);
  void report(Diagnostic diagnostic, SketchGpuUploadMetrics uploadMetrics);

  std::atomic<std::shared_ptr<const PresentedSketchFrame>> presented_;
  std::atomic_bool lowDegreeNurbsReady_ = false;
  std::atomic_bool generalNurbsReady_ = false;
  mutable std::mutex mutex_;
  SketchVectorPacketMetrics vectorPacketMetrics_;
  SketchGpuUploadMetrics uploadMetrics_;
  Diagnostic diagnostic_;
  std::uint64_t geometryBuildCount_ = 0U;

  friend class SketchFrameRenderer;
};

class SketchFrameRenderer final : public QSGRenderNode {
public:
  SketchFrameRenderer(QQuickWindow &window,
                      std::shared_ptr<SketchFrameRendererState> state);
  ~SketchFrameRenderer() override;

  void synchronize(std::shared_ptr<const SynchronizedSketchScene> desired,
                   SketchScenePalette palette, std::uint64_t paletteGeneration,
                   QRectF itemRect, SketchPipelineWarmup warmup = {});

  void prepare() override;
  void render(const RenderState *state) override;
  void releaseResources() override;
  [[nodiscard]] RenderingFlags flags() const override;
  [[nodiscard]] StateFlags changedStates() const override;
  [[nodiscard]] QRectF rect() const override;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Call after every Qt Quick window has been destroyed.
[[nodiscard]] bool shutdownSketchSceneResources(
    std::chrono::milliseconds drainTimeout = std::chrono::seconds{5}) noexcept;

} // namespace kearne::ui
