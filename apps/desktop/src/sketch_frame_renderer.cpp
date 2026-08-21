#include "sketch_frame_renderer.hpp"

#include "bounded_artifact_reclaimer.hpp"
#include "sketch_prepared_products.hpp"
#include "sketch_projection_support.hpp"
#include "sketch_stroke_pattern.hpp"

#include <QElapsedTimer>
#include <QFile>
#include <QQuickWindow>
#include <QSGRendererInterface>

#include <rhi/qrhi.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <optional>
#include <thread>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace kearne::ui {
namespace {

constexpr std::size_t uniformRecordBytes = 128U;
constexpr std::size_t styleRoleCount = 6U;
constexpr std::size_t linePatternCount = 3U;
constexpr std::size_t geometryLayerCount = 4U;
constexpr std::size_t maximumOverlayDrawCallsPerFrame = 4096U;
constexpr std::size_t uniformRecordsPerLayer =
    styleRoleCount * linePatternCount;
constexpr std::size_t uniformRecordCount =
    geometryLayerCount * uniformRecordsPerLayer;

enum class GeometryLayer : std::uint8_t {
  Base = 0,
  OverlayPoints,
  Provisional,
  Markers,
};

[[nodiscard]] constexpr std::size_t layerIndex(GeometryLayer layer) {
  return static_cast<std::size_t>(layer);
}

[[nodiscard]] const std::shared_ptr<const SketchSceneMesh> &
productMesh(const PreparedSketchProducts &products, std::size_t layer) {
  static const std::shared_ptr<const SketchSceneMesh> absent;
  switch (static_cast<GeometryLayer>(layer)) {
  case GeometryLayer::Base:
    return products.base()->mesh();
  case GeometryLayer::OverlayPoints:
    return products.overlayPointMesh();
  case GeometryLayer::Provisional:
    return products.provisional() ? products.provisional()->mesh() : absent;
  case GeometryLayer::Markers:
    return products.markerMesh();
  }
  return absent;
}

[[nodiscard]] bool sameView(const SynchronizedSketchScene &first,
                            const SynchronizedSketchScene &second) {
  return first.products() == second.products() &&
         first.transform().camera() == second.transform().camera() &&
         first.transform().viewportLogical() ==
             second.transform().viewportLogical() &&
         first.pickCoverage() == second.pickCoverage();
}

[[nodiscard]] constexpr std::size_t saturatedAdd(std::size_t first,
                                                 std::size_t second) {
  return second > std::numeric_limits<std::size_t>::max() - first
             ? std::numeric_limits<std::size_t>::max()
             : first + second;
}

[[nodiscard]] std::size_t uniformIndex(render::SketchStyleRole role,
                                       render::SketchLinePattern pattern,
                                       std::size_t layer = 0U) {
  const auto roleValue = static_cast<std::size_t>(role);
  const auto patternValue = static_cast<std::size_t>(pattern);
  Q_ASSERT(roleValue >= 1U && roleValue <= styleRoleCount);
  Q_ASSERT(patternValue >= 1U && patternValue <= linePatternCount);
  Q_ASSERT(layer < geometryLayerCount);
  return layer * uniformRecordsPerLayer + (roleValue - 1U) * linePatternCount +
         patternValue - 1U;
}

[[nodiscard]] render::SketchStyleRole
overlayStyleRole(render::SketchOverlayRole role) {
  switch (role) {
  case render::SketchOverlayRole::Hovered:
    return render::SketchStyleRole::Hovered;
  case render::SketchOverlayRole::Selected:
    return render::SketchStyleRole::Selected;
  case render::SketchOverlayRole::Preview:
    return render::SketchStyleRole::Preview;
  case render::SketchOverlayRole::Diagnostic:
    return render::SketchStyleRole::Diagnostic;
  }
  return render::SketchStyleRole::Regular;
}

[[nodiscard]] Result<QShader> loadShader(const char *path) {
  QFile file{QString::fromLatin1(path)};
  if (!file.open(QFile::ReadOnly))
    return std::unexpected(
        diagnostic("desktop.sketch.shader-open",
                   "sketch renderer could not open a packaged shader"));
  QShader shader = QShader::fromSerialized(file.readAll());
  if (!shader.isValid())
    return std::unexpected(
        diagnostic("desktop.sketch.shader-invalid",
                   "sketch renderer loaded an invalid packaged shader"));
  return shader;
}

struct RendererCpuArtifact {
  static constexpr std::size_t retainedLayerCapacity =
      geometryLayerCount * (SketchGpuUploadPolicy::maximumRetiredLayers + 2U);
  std::shared_ptr<const SynchronizedSketchScene> desired;
  std::shared_ptr<const SynchronizedSketchScene> presented;
  std::shared_ptr<const SynchronizedSketchScene> staging;
  std::array<std::shared_ptr<const SynchronizedSketchScene>,
             SketchGpuUploadPolicy::maximumRetiredLayers>
      retired;
  std::array<std::optional<ProgressiveSketchVisibility>, geometryLayerCount>
      visibility;
  std::array<std::optional<ProgressiveSketchUpload>, geometryLayerCount> upload;
  std::array<std::array<std::optional<ProgressiveSketchVisibility>,
                        geometryLayerCount>,
             SketchGpuUploadPolicy::maximumRetiredLayers>
      retiredVisibility;
  std::array<
      std::array<std::optional<ProgressiveSketchUpload>, geometryLayerCount>,
      SketchGpuUploadPolicy::maximumRetiredLayers>
      retiredUpload;
  std::array<SketchChunkSequence, retainedLayerCapacity> sequences;
  std::array<std::shared_ptr<const SketchPresentedChunkCoverage>,
             retainedLayerCapacity>
      coverages;
  std::size_t sequenceCount = 0U;
  std::size_t coverageCount = 0U;
};

using RendererReclaimArtifact =
    std::variant<std::shared_ptr<const SynchronizedSketchScene>,
                 RendererCpuArtifact>;
using RendererReclaimer =
    BoundedArtifactReclaimer<RendererReclaimArtifact, 32U>;

RendererReclaimer &sharedRendererReclaimer() {
  static RendererReclaimer reclaimer;
  return reclaimer;
}

void initializeRendererReclaimer() {
  static_cast<void>(sharedRendererReclaimer());
}

Q_COREAPP_STARTUP_FUNCTION(initializeRendererReclaimer)

void reclaimSynchronouslyEnqueued(RendererReclaimArtifact artifact) noexcept {
  while (!sharedRendererReclaimer().tryReclaim(
      [&artifact]() noexcept { return std::move(artifact); }))
    std::this_thread::yield();
}

[[nodiscard]] bool rhiBackendAvailable(QQuickWindow &window) {
  const auto *renderer = window.rendererInterface();
  if (!renderer ||
      !QSGRendererInterface::isApiRhiBased(renderer->graphicsApi()))
    return false;
  const auto api = renderer->graphicsApi();
  return api != QSGRendererInterface::Unknown &&
         api != QSGRendererInterface::Software &&
         api != QSGRendererInterface::Null;
}

} // namespace

std::shared_ptr<const PresentedSketchFrame>
SketchFrameRendererState::presented() const {
  return presented_.load(std::memory_order_acquire);
}

SketchMeshMetrics SketchFrameRendererState::meshMetrics() const {
  std::scoped_lock lock{mutex_};
  return meshMetrics_;
}

SketchGpuUploadMetrics SketchFrameRendererState::uploadMetrics() const {
  std::scoped_lock lock{mutex_};
  return uploadMetrics_;
}

std::uint64_t SketchFrameRendererState::geometryBuildCount() const {
  std::scoped_lock lock{mutex_};
  return geometryBuildCount_;
}

Diagnostic SketchFrameRendererState::lastDiagnostic() const {
  std::scoped_lock lock{mutex_};
  return diagnostic_;
}

void SketchFrameRendererState::invalidate() noexcept {
  presented_.store({}, std::memory_order_release);
}

void SketchFrameRendererState::publish(
    std::shared_ptr<const PresentedSketchFrame> frame,
    SketchMeshMetrics meshMetrics, SketchGpuUploadMetrics uploadMetrics,
    std::uint64_t geometryBuildCount, bool clearDiagnostic) {
  {
    std::scoped_lock lock{mutex_};
    meshMetrics_ = meshMetrics;
    uploadMetrics_ = uploadMetrics;
    geometryBuildCount_ = geometryBuildCount;
    if (clearDiagnostic)
      diagnostic_ = {};
  }
  presented_.store(std::move(frame), std::memory_order_release);
}

void SketchFrameRendererState::report(Diagnostic diagnostic,
                                      SketchGpuUploadMetrics uploadMetrics) {
  std::scoped_lock lock{mutex_};
  diagnostic_ = std::move(diagnostic);
  uploadMetrics_ = uploadMetrics;
}

struct SketchFrameRenderer::Impl {
  struct GpuChunk {
    std::unique_ptr<QRhiBuffer> vertices;
    std::unique_ptr<QRhiBuffer> indices;
    std::size_t payloadBytes = 0U;
    std::size_t leases = 0U;
  };

  struct GpuLayer {
    SketchChunkSequence chunks;
    std::shared_ptr<const SketchPresentedChunkCoverage> coverage;
    std::size_t referencedBytes = 0U;
  };

  struct GpuFrame {
    std::shared_ptr<const SynchronizedSketchScene> synchronized;
    std::array<GpuLayer, geometryLayerCount> layers;
  };

  struct StagingLayer {
    std::optional<ProgressiveSketchVisibility> visibility;
    std::optional<ProgressiveSketchUpload> upload;
    std::optional<SketchChunkSequence> completedChunks;
    std::shared_ptr<const SketchPresentedChunkCoverage> coverage;
    std::size_t leasedChunks = 0U;
    std::size_t referencedBytes = 0U;
  };

  struct Staging {
    std::shared_ptr<const SynchronizedSketchScene> synchronized;
    std::array<StagingLayer, geometryLayerCount> layers;
    std::size_t activeLayer = 0U;
    std::size_t visibilityComparedChunks = 0U;
    bool visibilityMatchesPresented = false;
    bool rejected = false;
  };

  struct RetiredLayer {
    SketchChunkSequence chunks;
    std::size_t leasedChunks = 0U;
    std::size_t cursor = 0U;
    std::size_t referencedBytes = 0U;
    std::shared_ptr<const SketchPresentedChunkCoverage> coverage;
    std::optional<ProgressiveSketchVisibility> visibility;
    std::optional<ProgressiveSketchUpload> upload;
  };

  struct RetiredFrame {
    std::shared_ptr<const SynchronizedSketchScene> synchronized;
    std::array<RetiredLayer, geometryLayerCount> layers;
  };

  struct RecordedFrame {
    std::shared_ptr<const SynchronizedSketchScene> synchronized;
    SketchPresentedProductCoverage productCoverage;
    SketchPresentationEvidence evidence;
  };

  SketchFrameRenderer &owner;
  QQuickWindow &window;
  std::shared_ptr<SketchFrameRendererState> state;
  std::shared_ptr<const SynchronizedSketchScene> desired;
  std::optional<GpuFrame> presented;
  std::optional<Staging> staging;
  std::array<std::optional<RetiredFrame>,
             SketchGpuUploadPolicy::maximumRetiredLayers>
      retired;
  std::size_t retiredRead = 0U;
  std::size_t retiredCount = 0U;
  std::unordered_map<const SketchUploadChunk *, GpuChunk> resources;
  SketchScenePalette palette;
  std::uint64_t paletteGeneration = 0U;
  QRectF itemRect;
  QRhi *rhi = nullptr;
  std::unique_ptr<QRhiBuffer> uniformBuffer;
  std::unique_ptr<QRhiShaderResourceBindings> resourceBindings;
  std::unique_ptr<QRhiGraphicsPipeline> pipeline;
  std::unique_ptr<QRhiGraphicsPipeline> stencilPipeline;
  QVector<QRhiShaderStage> shaders;
  QVector<quint32> renderPassFormat;
  std::shared_ptr<const std::vector<std::uint32_t>> publishedRenderPassFormat;
  int sampleCount = 0;
  std::size_t uniformStride = 0U;
  std::vector<std::byte> uniformData;
  SketchGpuUploadMetrics metrics;
  std::uint64_t geometryBuildCount = 0U;
  std::uint64_t frameSerial = 0U;
  std::uint64_t renderEpoch = 0U;
  bool shaderFailure = false;

  explicit Impl(SketchFrameRenderer &owningNode, QQuickWindow &owningWindow,
                std::shared_ptr<SketchFrameRendererState> sharedState)
      : owner(owningNode), window(owningWindow), state(std::move(sharedState)) {
    static std::atomic_uint64_t nextEpoch = 1U;
    renderEpoch = nextEpoch.fetch_add(1U, std::memory_order_relaxed);
    auto vertex = loadShader(":/kearne/shaders/sketch_scene.vert.qsb");
    auto fragment = loadShader(":/kearne/shaders/sketch_scene.frag.qsb");
    if (!vertex || !fragment) {
      shaderFailure = true;
      state->report(vertex ? fragment.error() : vertex.error(), metrics);
      return;
    }
    shaders = {{QRhiShaderStage::Vertex, std::move(*vertex)},
               {QRhiShaderStage::Fragment, std::move(*fragment)}};
  }

  ~Impl() { release(); }

  [[nodiscard]] std::size_t retiredIndex(std::size_t offset) const {
    return (retiredRead + offset) % retired.size();
  }

  [[nodiscard]] bool queueRetired(RetiredFrame frame) {
    if (retiredCount == retired.size())
      return false;
    retired[retiredIndex(retiredCount)].emplace(std::move(frame));
    ++retiredCount;
    metrics.maximumRetiredLayers =
        std::max(metrics.maximumRetiredLayers, retiredCount);
    return true;
  }

  void popRetired() {
    Q_ASSERT(retiredCount != 0U);
    retired[retiredRead].reset();
    retiredRead = (retiredRead + 1U) % retired.size();
    --retiredCount;
  }

  void scheduleFrame() { window.update(); }

  void fail(Diagnostic diagnostic, bool invalidate = false) {
    if (invalidate)
      state->invalidate();
    state->report(std::move(diagnostic), metrics);
  }

  void releaseChunk(const SketchUploadChunk *identity) {
    auto found = resources.find(identity);
    if (found == resources.end() || found->second.leases == 0U)
      return;
    --found->second.leases;
    if (found->second.leases == 0U) {
      metrics.chunksRetired += 1U;
      resources.erase(found);
    }
  }

  void retireSlice() {
    std::size_t retiredChunks = 0U;
    while (retiredChunks < SketchGpuUploadPolicy::maximumChunksPerFrame &&
           retiredCount != 0U) {
      RetiredFrame &frame = *retired[retiredRead];
      bool complete = true;
      for (std::size_t layerIndex = 0U; layerIndex < frame.layers.size();
           ++layerIndex) {
        RetiredLayer &layer = frame.layers[layerIndex];
        const auto &mesh =
            productMesh(*frame.synchronized->products(), layerIndex);
        Q_ASSERT(mesh || layer.leasedChunks == 0U);
        while (mesh &&
               retiredChunks < SketchGpuUploadPolicy::maximumChunksPerFrame &&
               layer.cursor < layer.leasedChunks) {
          const auto &chunk = mesh->chunks()[layer.chunks[layer.cursor]];
          releaseChunk(chunk.get());
          layer.referencedBytes -= chunk->payloadBytes();
          ++layer.cursor;
          ++retiredChunks;
        }
        if (layer.cursor != layer.leasedChunks) {
          complete = false;
          break;
        }
      }
      if (!complete)
        break;
      if (!sharedRendererReclaimer().tryReclaim(
              [&frame]() noexcept -> RendererReclaimArtifact {
                RendererCpuArtifact artifact;
                artifact.presented = std::move(frame.synchronized);
                for (std::size_t layerIndex = 0U;
                     layerIndex < frame.layers.size(); ++layerIndex) {
                  RetiredLayer &layer = frame.layers[layerIndex];
                  artifact.sequences[artifact.sequenceCount++] =
                      std::move(layer.chunks);
                  if (layer.coverage)
                    artifact.coverages[artifact.coverageCount++] =
                        std::move(layer.coverage);
                  artifact.visibility[layerIndex] = std::move(layer.visibility);
                  artifact.upload[layerIndex] = std::move(layer.upload);
                }
                return artifact;
              }))
        break;
      popRetired();
    }
    metrics.maximumNodeOperationsPerFrame =
        std::max(metrics.maximumNodeOperationsPerFrame, retiredChunks);
    const ArtifactReclaimerMetrics reclaimed =
        sharedRendererReclaimer().metrics();
    metrics.cpuReclaimsAccepted = reclaimed.accepted;
    metrics.cpuReclaimsReleased = reclaimed.released;
    metrics.cpuReclaimsSaturated = reclaimed.saturated;
    metrics.cpuReclaimsContended = reclaimed.contended;
    metrics.cpuReclaimsOutstanding = reclaimed.outstanding;
    metrics.maximumCpuReclaimQueue =
        std::max(metrics.maximumCpuReclaimQueue, reclaimed.maximumQueued);
  }

  [[nodiscard]] bool cancelStaging() {
    if (!staging)
      return true;
    if (retiredCount == retired.size())
      return false;
    RetiredFrame retiredFrame;
    retiredFrame.synchronized = staging->synchronized;
    for (std::size_t layerIndex = 0U; layerIndex < retiredFrame.layers.size();
         ++layerIndex) {
      StagingLayer &source = staging->layers[layerIndex];
      RetiredLayer &target = retiredFrame.layers[layerIndex];
      if (source.completedChunks)
        target.chunks = std::move(*source.completedChunks);
      else if (source.upload)
        target.chunks = source.upload->releaseRequiredChunks();
      target.leasedChunks = source.leasedChunks;
      target.referencedBytes = source.referencedBytes;
      target.coverage = std::move(source.coverage);
      target.visibility = std::move(source.visibility);
      target.upload = std::move(source.upload);
    }
    const bool queued = queueRetired(std::move(retiredFrame));
    Q_ASSERT(queued);
    staging.reset();
    ++metrics.canceledStagings;
    return true;
  }

  void rejectStaging() {
    if (!staging)
      return;
    staging->rejected = true;
    if (!cancelStaging())
      scheduleFrame();
  }

  [[nodiscard]] bool stagingMatchesDesired() const {
    return staging && desired && sameView(*staging->synchronized, *desired);
  }

  [[nodiscard]] bool presentedMatchesDesired() const {
    return presented && desired && sameView(*presented->synchronized, *desired);
  }

  [[nodiscard]] std::shared_ptr<const SynchronizedSketchScene>
  drawSynchronized() const {
    if (!presented)
      return {};
    if (staging && staging->synchronized->prepared() ==
                       presented->synchronized->prepared())
      return staging->synchronized;
    return presented->synchronized;
  }

  void beginStaging() {
    auto visibility = ProgressiveSketchVisibility::create(
        desired->mesh(), desired->transform(), desired->pickCoverage());
    if (!visibility) {
      fail(std::move(visibility.error()));
      return;
    }
    staging.emplace();
    staging->synchronized = desired;
    staging->layers[layerIndex(GeometryLayer::Base)].visibility.emplace(
        std::move(*visibility));
    staging->visibilityMatchesPresented =
        presented &&
        presented->synchronized->prepared() == desired->prepared() &&
        !presented->synchronized->products()->provisional() &&
        !presented->synchronized->products()->overlay() &&
        !presented->synchronized->products()->markers() &&
        !desired->products()->provisional() &&
        !desired->products()->overlay() && !desired->products()->markers();
  }

  [[nodiscard]] bool
  createChunkResource(const std::shared_ptr<const SketchUploadChunk> &chunk,
                      QRhiResourceUpdateBatch &updates) {
    if (chunk->vertices().size() >
            std::numeric_limits<quint32>::max() / sizeof(SketchMeshVertex) ||
        chunk->indices().size() >
            std::numeric_limits<quint32>::max() / sizeof(std::uint32_t)) {
      fail(diagnostic("desktop.sketch.gpu-buffer-limit",
                      "sketch upload chunk exceeds the QRhi buffer limit"));
      return false;
    }
    const auto vertexBytes = static_cast<quint32>(chunk->vertices().size() *
                                                  sizeof(SketchMeshVertex));
    const auto indexBytes =
        static_cast<quint32>(chunk->indices().size() * sizeof(std::uint32_t));
    GpuChunk gpu;
    gpu.vertices.reset(rhi->newBuffer(QRhiBuffer::Immutable,
                                      QRhiBuffer::VertexBuffer, vertexBytes));
    gpu.indices.reset(rhi->newBuffer(QRhiBuffer::Immutable,
                                     QRhiBuffer::IndexBuffer, indexBytes));
    if (!gpu.vertices || !gpu.indices || !gpu.vertices->create() ||
        !gpu.indices->create()) {
      fail(diagnostic("desktop.sketch.gpu-buffer-create",
                      "sketch renderer could not create a GPU buffer"));
      return false;
    }
    updates.uploadStaticBuffer(gpu.vertices.get(), chunk->vertices().data());
    updates.uploadStaticBuffer(gpu.indices.get(), chunk->indices().data());
    gpu.payloadBytes = chunk->payloadBytes();
    try {
      auto [_, inserted] = resources.emplace(chunk.get(), std::move(gpu));
      if (!inserted) {
        fail(diagnostic("desktop.sketch.gpu-resource-conflict",
                        "sketch GPU resource identity conflicted"));
        return false;
      }
    } catch (const std::bad_alloc &) {
      fail(diagnostic("desktop.sketch.gpu-resource-allocation",
                      "sketch GPU resource registry allocation failed"));
      return false;
    }
    return true;
  }

  enum class StageProgress : std::uint8_t { Failed, Pending, Complete };

  [[nodiscard]] StageProgress
  advanceVisibilityLayer(std::optional<ProgressiveSketchVisibility> &visibility,
                         std::optional<ProgressiveSketchUpload> &upload,
                         const std::shared_ptr<const SketchSceneMesh> &mesh,
                         bool comparePresentedBase) {
    if (!staging || !visibility)
      return StageProgress::Complete;
    auto slice = visibility->takeNextSlice(
        SketchGpuUploadPolicy::maximumSpatialNodesPerFrame,
        SketchGpuUploadPolicy::maximumVisibleChunksPerFrame);
    if (!slice) {
      fail(std::move(slice.error()));
      rejectStaging();
      return StageProgress::Failed;
    }
    ++metrics.visibilitySlices;
    metrics.visibilityNodesVisited += slice->spatialNodesVisited;
    metrics.maximumVisibilityNodesPerFrame = std::max(
        metrics.maximumVisibilityNodesPerFrame, slice->spatialNodesVisited);
    metrics.maximumVisibleChunksSelectedPerFrame = std::max(
        metrics.maximumVisibleChunksSelectedPerFrame, slice->chunks.size());
    if (comparePresentedBase && staging->visibilityMatchesPresented) {
      const SketchChunkSequence &presentedBase =
          presented->layers[layerIndex(GeometryLayer::Base)].chunks;
      for (const std::uint32_t chunk : slice->chunks) {
        if (staging->visibilityComparedChunks >= presentedBase.size() ||
            presentedBase[staging->visibilityComparedChunks] != chunk)
          staging->visibilityMatchesPresented = false;
        ++staging->visibilityComparedChunks;
      }
    }
    if (!visibility->complete()) {
      scheduleFrame();
      return StageProgress::Pending;
    }
    if (comparePresentedBase && staging->visibilityMatchesPresented &&
        staging->visibilityComparedChunks ==
            presented->layers[layerIndex(GeometryLayer::Base)].chunks.size()) {
      try {
        presented->synchronized =
            std::make_shared<const SynchronizedSketchScene>(
                staging->synchronized->products(),
                staging->synchronized->transform(),
                staging->synchronized->pickCoverage(),
                presented->synchronized->presentedChunks());
      } catch (const std::bad_alloc &) {
        fail(diagnostic("desktop.sketch.presented-frame-allocation",
                        "sketch presented frame allocation failed"));
        rejectStaging();
        return StageProgress::Failed;
      }
      staging.reset();
      return StageProgress::Pending;
    }
    auto createdUpload = ProgressiveSketchUpload::create(
        mesh, visibility->releaseSelectedChunks(), {});
    visibility.reset();
    if (!createdUpload) {
      fail(std::move(createdUpload.error()));
      rejectStaging();
      return StageProgress::Failed;
    }
    upload.emplace(std::move(*createdUpload));
    return StageProgress::Complete;
  }

  [[nodiscard]] bool advanceVisibility() {
    if (!staging)
      return false;
    if (staging->activeLayer >= geometryLayerCount)
      return true;
    StagingLayer &layer = staging->layers[staging->activeLayer];
    const auto &mesh =
        productMesh(*staging->synchronized->products(), staging->activeLayer);
    Q_ASSERT(mesh);
    if (layer.visibility)
      return advanceVisibilityLayer(layer.visibility, layer.upload, mesh,
                                    staging->activeLayer ==
                                        layerIndex(GeometryLayer::Base)) ==
             StageProgress::Complete;
    return true;
  }

  [[nodiscard]] StageProgress
  advanceUploadLayer(ProgressiveSketchUpload &upload, std::size_t &leasedChunks,
                     std::size_t &referencedBytes) {
    QElapsedTimer timer;
    timer.start();
    auto slice =
        upload.takeNextSlice(SketchGpuUploadPolicy::maximumBytesPerFrame,
                             SketchGpuUploadPolicy::maximumChunksPerFrame);
    if (!slice) {
      fail(std::move(slice.error()));
      rejectStaging();
      return StageProgress::Failed;
    }

    QRhiResourceUpdateBatch *updates = nullptr;
    std::size_t uploadedChunks = 0U;
    std::size_t uploadedBytes = 0U;
    const auto chunks = upload.mesh()->chunks();
    for (const SketchUploadSliceEntry entry : slice->entries) {
      const auto &chunk = chunks[entry.chunk];
      std::size_t nextReferencedBytes = 0U;
      if (!detail::checkedSizeAdd(referencedBytes, chunk->payloadBytes(),
                                  nextReferencedBytes)) {
        if (updates)
          updates->release();
        fail(diagnostic("desktop.sketch.gpu-byte-overflow",
                        "sketch GPU byte accounting overflowed"));
        rejectStaging();
        return StageProgress::Failed;
      }
      auto found = resources.find(chunk.get());
      if (found == resources.end()) {
        if (!updates)
          updates = rhi->nextResourceUpdateBatch();
        if (!createChunkResource(chunk, *updates)) {
          if (updates)
            updates->release();
          rejectStaging();
          return StageProgress::Failed;
        }
        found = resources.find(chunk.get());
        ++uploadedChunks;
        const bool accounted = detail::checkedSizeAdd(
            uploadedBytes, chunk->payloadBytes(), uploadedBytes);
        Q_ASSERT(accounted);
      } else {
        ++metrics.reusedChunks;
      }
      ++found->second.leases;
      ++leasedChunks;
      referencedBytes = nextReferencedBytes;
    }
    if (updates)
      owner.commandBuffer()->resourceUpdate(updates);

    const auto elapsed =
        static_cast<std::uint64_t>(std::max<qint64>(0, timer.nsecsElapsed()));
    if (!slice->entries.empty()) {
      ++metrics.slices;
      metrics.chunksUploaded += uploadedChunks;
      metrics.bytesUploaded += uploadedBytes;
      metrics.lastSliceBytes = uploadedBytes;
      metrics.maximumSliceBytes =
          std::max(metrics.maximumSliceBytes, uploadedBytes);
      metrics.lastSliceChunks = slice->entries.size();
      metrics.maximumSliceChunks =
          std::max(metrics.maximumSliceChunks, slice->entries.size());
      metrics.lastSliceNanoseconds = elapsed;
      metrics.maximumSliceNanoseconds =
          std::max(metrics.maximumSliceNanoseconds, elapsed);
      if (elapsed > SketchGpuUploadPolicy::frameBudgetNanoseconds)
        ++metrics.frameBudgetExceeded;
    }
    if (!upload.complete()) {
      scheduleFrame();
      return StageProgress::Pending;
    }
    return StageProgress::Complete;
  }

  [[nodiscard]] bool beginNextLayer() {
    Q_ASSERT(staging);
    while (++staging->activeLayer < geometryLayerCount) {
      const auto &mesh =
          productMesh(*staging->synchronized->products(), staging->activeLayer);
      if (!mesh)
        continue;
      auto visibility = ProgressiveSketchVisibility::create(
          mesh, staging->synchronized->transform(),
          staging->synchronized->pickCoverage());
      if (!visibility) {
        fail(std::move(visibility.error()));
        staging->rejected = true;
        if (!cancelStaging())
          scheduleFrame();
        ++metrics.rejectedPackets;
        return false;
      }
      staging->layers[staging->activeLayer].visibility.emplace(
          std::move(*visibility));
      scheduleFrame();
      return false;
    }
    return sealStaging();
  }

  [[nodiscard]] bool finishLayerUpload() {
    if (!staging || staging->activeLayer >= geometryLayerCount)
      return false;
    StagingLayer &layer = staging->layers[staging->activeLayer];
    const auto &mesh =
        productMesh(*staging->synchronized->products(), staging->activeLayer);
    if (!mesh || !layer.upload || !layer.upload->complete())
      return false;
    layer.completedChunks.emplace(layer.upload->releaseRequiredChunks());
    layer.upload.reset();
    auto coverage =
        SketchPresentedChunkCoverage::create(*mesh, *layer.completedChunks);
    if (!coverage) {
      fail(std::move(coverage.error()));
      staging->rejected = true;
      if (!cancelStaging())
        scheduleFrame();
      ++metrics.rejectedPackets;
      return false;
    }
    layer.coverage = std::move(*coverage);
    return beginNextLayer();
  }

  [[nodiscard]] bool advanceUpload() {
    if (!staging)
      return false;
    if (staging->activeLayer < geometryLayerCount) {
      StagingLayer &layer = staging->layers[staging->activeLayer];
      if (!layer.upload)
        return false;
      const StageProgress progress = advanceUploadLayer(
          *layer.upload, layer.leasedChunks, layer.referencedBytes);
      if (progress != StageProgress::Complete)
        return false;
      return finishLayerUpload();
    }
    return sealStaging();
  }

  [[nodiscard]] bool sealStaging() {
    if (!staging)
      return false;
    for (std::size_t layerIndex = 0U; layerIndex < geometryLayerCount;
         ++layerIndex) {
      const bool required =
          bool(productMesh(*staging->synchronized->products(), layerIndex));
      if (required != bool(staging->layers[layerIndex].completedChunks) ||
          required != bool(staging->layers[layerIndex].coverage))
        return false;
    }
    if (presented && retiredCount == retired.size()) {
      scheduleFrame();
      return false;
    }
    std::shared_ptr<const SynchronizedSketchScene> synchronized;
    try {
      synchronized = std::make_shared<const SynchronizedSketchScene>(
          staging->synchronized->products(), staging->synchronized->transform(),
          staging->synchronized->pickCoverage(),
          staging->layers[layerIndex(GeometryLayer::Base)].coverage);
    } catch (const std::bad_alloc &) {
      fail(diagnostic("desktop.sketch.presented-frame-allocation",
                      "sketch presented frame allocation failed"));
      staging->rejected = true;
      if (!cancelStaging())
        scheduleFrame();
      return false;
    }
    bool newGeometry = !presented;
    if (presented)
      for (std::size_t layerIndex = 0U; layerIndex < geometryLayerCount;
           ++layerIndex)
        newGeometry =
            newGeometry ||
            productMesh(*presented->synchronized->products(), layerIndex) !=
                productMesh(*synchronized->products(), layerIndex);
    if (presented) {
      RetiredFrame previous;
      previous.synchronized = std::move(presented->synchronized);
      for (std::size_t layerIndex = 0U; layerIndex < geometryLayerCount;
           ++layerIndex) {
        previous.layers[layerIndex].chunks =
            std::move(presented->layers[layerIndex].chunks);
        previous.layers[layerIndex].leasedChunks =
            previous.layers[layerIndex].chunks.size();
        previous.layers[layerIndex].referencedBytes =
            presented->layers[layerIndex].referencedBytes;
        previous.layers[layerIndex].coverage =
            std::move(presented->layers[layerIndex].coverage);
      }
      const bool queued = queueRetired(std::move(previous));
      Q_ASSERT(queued);
    }
    GpuFrame next;
    next.synchronized = std::move(synchronized);
    for (std::size_t layerIndex = 0U; layerIndex < geometryLayerCount;
         ++layerIndex) {
      StagingLayer &source = staging->layers[layerIndex];
      if (source.completedChunks)
        next.layers[layerIndex].chunks = std::move(*source.completedChunks);
      next.layers[layerIndex].coverage = std::move(source.coverage);
      next.layers[layerIndex].referencedBytes = source.referencedBytes;
    }
    presented.emplace(std::move(next));
    staging.reset();
    ++metrics.coherentSwaps;
    if (newGeometry)
      ++geometryBuildCount;
    return true;
  }

  void advanceFrame() {
    retireSlice();
    if (!desired)
      return;
    for (std::size_t layerIndex = 0U; layerIndex < geometryLayerCount;
         ++layerIndex) {
      const auto &mesh = productMesh(*desired->products(), layerIndex);
      if (!mesh)
        continue;
      auto gpuView = desired->transform().gpuView(mesh->originMetres());
      if (!gpuView) {
        fail(std::move(gpuView.error()));
        return;
      }
      if (auto phase = mesh->validatePatternedPhase(*gpuView); !phase) {
        fail(std::move(phase.error()));
        return;
      }
      if (mesh->maximumChunkBytes() >
          SketchGpuUploadPolicy::maximumChunkBytes) {
        ++metrics.rejectedPackets;
        fail(diagnostic(
            "desktop.sketch.gpu-upload-chunk-invariant",
            "prepared sketch upload chunks exceed the production bound"));
        return;
      }
    }
    if (staging && staging->rejected) {
      if (!cancelStaging())
        scheduleFrame();
      return;
    }
    if (staging && !stagingMatchesDesired()) {
      if (!cancelStaging()) {
        scheduleFrame();
        return;
      }
    }
    if (presentedMatchesDesired())
      return;
    if (!staging)
      beginStaging();
    if (!advanceVisibility())
      return;
    static_cast<void>(advanceUpload());
  }

  [[nodiscard]] bool ensureCommonResources() {
    if (shaderFailure || !rhiBackendAvailable(window) || !window.rhi() ||
        !owner.renderTarget() ||
        !owner.renderTarget()->renderPassDescriptor() ||
        !owner.commandBuffer()) {
      fail(diagnostic("desktop.sketch.rhi-unavailable",
                      "sketch presentation requires an RHI graphics backend"),
           true);
      return false;
    }
    QRhi *nextRhi = window.rhi();
    if (rhi && rhi != nextRhi)
      release();
    rhi = nextRhi;
    if (!uniformBuffer) {
      uniformStride = std::max(uniformRecordBytes,
                               static_cast<std::size_t>(rhi->ubufAlignment()));
      std::size_t total = 0U;
      if (!detail::checkedSizeMultiply(uniformStride, uniformRecordCount,
                                       total) ||
          total > std::numeric_limits<quint32>::max()) {
        fail(diagnostic("desktop.sketch.uniform-buffer-limit",
                        "sketch uniform buffer exceeds the QRhi limit"),
             true);
        return false;
      }
      uniformBuffer.reset(rhi->newBuffer(QRhiBuffer::Dynamic,
                                         QRhiBuffer::UniformBuffer,
                                         static_cast<quint32>(total)));
      if (!uniformBuffer || !uniformBuffer->create()) {
        fail(diagnostic("desktop.sketch.uniform-buffer-create",
                        "sketch renderer could not create its uniform buffer"),
             true);
        return false;
      }
      try {
        uniformData.resize(total);
      } catch (const std::bad_alloc &) {
        fail(diagnostic("desktop.sketch.uniform-allocation",
                        "sketch renderer uniform allocation failed"),
             true);
        return false;
      }
    }
    if (!resourceBindings) {
      resourceBindings.reset(rhi->newShaderResourceBindings());
      if (!resourceBindings) {
        fail(diagnostic("desktop.sketch.resource-binding-create",
                        "sketch renderer could not create resource bindings"),
             true);
        return false;
      }
      resourceBindings->setBindings(
          {QRhiShaderResourceBinding::uniformBufferWithDynamicOffset(
              0,
              QRhiShaderResourceBinding::VertexStage |
                  QRhiShaderResourceBinding::FragmentStage,
              uniformBuffer.get(), static_cast<quint32>(uniformRecordBytes))});
      if (!resourceBindings->create()) {
        fail(diagnostic("desktop.sketch.resource-binding-create",
                        "sketch renderer could not create resource bindings"),
             true);
        return false;
      }
    }
    return ensurePipelines();
  }

  [[nodiscard]] bool buildPipeline(bool stencil,
                                   std::unique_ptr<QRhiGraphicsPipeline> &out) {
    out.reset(rhi->newGraphicsPipeline());
    if (!out)
      return false;
    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable = true;
    out->setTargetBlends({blend});
    out->setTopology(QRhiGraphicsPipeline::Triangles);
    out->setCullMode(QRhiGraphicsPipeline::None);
    out->setSampleCount(owner.renderTarget()->sampleCount());
    out->setShaderResourceBindings(resourceBindings.get());
    out->setShaderStages(shaders.cbegin(), shaders.cend());
    QRhiVertexInputLayout input;
    input.setBindings({{static_cast<quint32>(sizeof(SketchMeshVertex))}});
    input.setAttributes(
        {{0, 0, QRhiVertexInputAttribute::Float2,
          static_cast<quint32>(offsetof(SketchMeshVertex, x))},
         {0, 1, QRhiVertexInputAttribute::Float2,
          static_cast<quint32>(offsetof(SketchMeshVertex, xLow))},
         {0, 2, QRhiVertexInputAttribute::Float2,
          static_cast<quint32>(offsetof(SketchMeshVertex, extrusionX))},
         {0, 3, QRhiVertexInputAttribute::Float,
          static_cast<quint32>(offsetof(SketchMeshVertex, pathDistanceMetres))},
         {0, 4, QRhiVertexInputAttribute::Float2,
          static_cast<quint32>(
              offsetof(SketchMeshVertex, coverageDistancePixels))},
         {0, 5, QRhiVertexInputAttribute::Float2,
          static_cast<quint32>(
              offsetof(SketchMeshVertex, patternOnLogicalPixels))}});
    out->setVertexInputLayout(input);
    QRhiGraphicsPipeline::Flags flags = QRhiGraphicsPipeline::UsesScissor;
    if (stencil) {
      flags |= QRhiGraphicsPipeline::UsesStencilRef;
      QRhiGraphicsPipeline::StencilOpState stencilState;
      stencilState.compareOp = QRhiGraphicsPipeline::Equal;
      out->setStencilTest(true);
      out->setStencilFront(stencilState);
      out->setStencilBack(stencilState);
      out->setStencilWriteMask(0U);
    }
    out->setFlags(flags);
    out->setRenderPassDescriptor(owner.renderTarget()->renderPassDescriptor());
    return out->create();
  }

  [[nodiscard]] bool ensurePipelines() {
    const QVector<quint32> format =
        owner.renderTarget()->renderPassDescriptor()->serializedFormat();
    const int nextSamples = owner.renderTarget()->sampleCount();
    if (pipeline &&
        (format != renderPassFormat || nextSamples != sampleCount)) {
      pipeline.reset();
      stencilPipeline.reset();
      publishedRenderPassFormat.reset();
    }
    if (pipeline)
      return true;
    if (!buildPipeline(false, pipeline) ||
        !buildPipeline(true, stencilPipeline)) {
      pipeline.reset();
      stencilPipeline.reset();
      fail(diagnostic("desktop.sketch.pipeline-create",
                      "sketch renderer could not create its graphics pipeline"),
           true);
      return false;
    }
    renderPassFormat = format;
    sampleCount = nextSamples;
    try {
      publishedRenderPassFormat =
          std::make_shared<const std::vector<std::uint32_t>>(format.cbegin(),
                                                             format.cend());
    } catch (const std::bad_alloc &) {
      fail(diagnostic("desktop.sketch.pipeline-format-allocation",
                      "sketch render-target format allocation failed"),
           true);
      return false;
    }
    return true;
  }

  void updateUniforms() {
    const auto drawn = drawSynchronized();
    if (!drawn || !presented)
      return;
    const QMatrix4x4 mvp = *owner.projectionMatrix() * *owner.matrix();
    const QSizeF viewport = drawn->transform().viewportLogical();
    const float opacity = static_cast<float>(owner.inheritedOpacity());
    const auto writeLayer = [&](const SketchSceneMesh &mesh,
                                std::size_t layer) -> bool {
      auto gpuView = drawn->transform().gpuView(mesh.originMetres());
      if (!gpuView) {
        fail(std::move(gpuView.error()));
        return false;
      }
      for (std::size_t roleIndex = 0U; roleIndex < styleRoleCount;
           ++roleIndex) {
        const auto role = static_cast<render::SketchStyleRole>(roleIndex + 1U);
        const QColor source = QColor::fromRgba(palette.color(role));
        const float alpha = static_cast<float>(source.alphaF());
        const std::array<float, 4> color{
            static_cast<float>(source.redF()) * alpha,
            static_cast<float>(source.greenF()) * alpha,
            static_cast<float>(source.blueF()) * alpha, alpha};
        for (std::size_t patternIndex = 0U; patternIndex < linePatternCount;
             ++patternIndex) {
          const auto pattern =
              static_cast<render::SketchLinePattern>(patternIndex + 1U);
          const render::SketchStyle style{role, pattern, 1.0F, 1.0F, 0U};
          const SketchStrokePattern stroke = strokePattern(style);
          std::byte *record =
              uniformData.data() +
              uniformStride * uniformIndex(role, pattern, layer);
          const std::array<float, 4> cameraHigh{
              gpuView->centerOffsetX, gpuView->centerOffsetY,
              gpuView->metresPerLogicalPixel, opacity};
          const std::array<float, 4> cameraLowRotation{
              gpuView->centerOffsetXLow, gpuView->centerOffsetYLow,
              gpuView->cosine, gpuView->sine};
          const std::array<float, 4> viewportPattern{
              static_cast<float>(viewport.width()),
              static_cast<float>(viewport.height()), stroke.onLogicalPixels,
              stroke.periodLogicalPixels};
          std::memcpy(record, mvp.constData(), 64U);
          std::memcpy(record + 64U, color.data(), sizeof(color));
          std::memcpy(record + 80U, cameraHigh.data(), sizeof(cameraHigh));
          std::memcpy(record + 96U, cameraLowRotation.data(),
                      sizeof(cameraLowRotation));
          std::memcpy(record + 112U, viewportPattern.data(),
                      sizeof(viewportPattern));
        }
      }
      return true;
    };
    for (std::size_t layerIndex = 0U; layerIndex < geometryLayerCount;
         ++layerIndex) {
      const auto &mesh =
          productMesh(*presented->synchronized->products(), layerIndex);
      if (!writeLayer(mesh ? *mesh : *presented->synchronized->mesh(),
                      layerIndex))
        return;
    }
    QRhiResourceUpdateBatch *updates = rhi->nextResourceUpdateBatch();
    updates->updateDynamicBuffer(uniformBuffer.get(), 0U,
                                 static_cast<quint32>(uniformData.size()),
                                 uniformData.data());
    owner.commandBuffer()->resourceUpdate(updates);
  }

  void prepare() {
    if (!ensureCommonResources())
      return;
    advanceFrame();
    updateUniforms();
    std::size_t presentedChunks = 0U;
    std::size_t stagingChunks = 0U;
    std::size_t presentedBytes = 0U;
    std::size_t stagingBytes = 0U;
    std::size_t presentedSequenceBytes = 0U;
    std::size_t stagingSequenceBytes = 0U;
    if (presented)
      for (const GpuLayer &layer : presented->layers) {
        presentedChunks = saturatedAdd(presentedChunks, layer.chunks.size());
        presentedBytes = saturatedAdd(presentedBytes, layer.referencedBytes);
        presentedSequenceBytes = saturatedAdd(
            presentedSequenceBytes, layer.chunks.retainedOrderBytes());
      }
    if (staging)
      for (const StagingLayer &layer : staging->layers) {
        stagingChunks = saturatedAdd(stagingChunks, layer.leasedChunks);
        stagingBytes = saturatedAdd(stagingBytes, layer.referencedBytes);
        if (layer.upload)
          stagingSequenceBytes =
              saturatedAdd(stagingSequenceBytes,
                           layer.upload->requiredChunks().retainedOrderBytes());
        if (layer.completedChunks)
          stagingSequenceBytes =
              saturatedAdd(stagingSequenceBytes,
                           layer.completedChunks->retainedOrderBytes());
      }
    metrics.maximumPresentedChunks =
        std::max(metrics.maximumPresentedChunks, presentedChunks);
    metrics.maximumStagingChunks =
        std::max(metrics.maximumStagingChunks, stagingChunks);
    metrics.maximumRetiredLayers =
        std::max(metrics.maximumRetiredLayers, retiredCount);
    metrics.maximumPresentedReferencedBytes =
        std::max(metrics.maximumPresentedReferencedBytes, presentedBytes);
    metrics.maximumStagingReferencedBytes =
        std::max(metrics.maximumStagingReferencedBytes, stagingBytes);
    metrics.maximumPresentedChunkSequenceBytes = std::max(
        metrics.maximumPresentedChunkSequenceBytes, presentedSequenceBytes);
    metrics.maximumStagingChunkSequenceBytes = std::max(
        metrics.maximumStagingChunkSequenceBytes, stagingSequenceBytes);
    std::size_t retiredChunks = 0U;
    std::size_t retiredBytes = 0U;
    for (std::size_t index = 0U; index < retiredCount; ++index) {
      const auto &entry = retired[retiredIndex(index)];
      for (const RetiredLayer &layer : entry->layers) {
        retiredChunks =
            saturatedAdd(retiredChunks, layer.leasedChunks - layer.cursor);
        retiredBytes = saturatedAdd(retiredBytes, layer.referencedBytes);
      }
    }
    metrics.maximumRetiredChunks =
        std::max(metrics.maximumRetiredChunks, retiredChunks);
    metrics.maximumRetiredReferencedBytes =
        std::max(metrics.maximumRetiredReferencedBytes, retiredBytes);
    if (staging || retiredCount != 0U)
      scheduleFrame();
  }

  [[nodiscard]] std::optional<RecordedFrame>
  record(const QSGRenderNode::RenderState *renderState) {
    if (!presented || !pipeline || !resourceBindings || !uniformBuffer ||
        !owner.renderTarget() || !owner.commandBuffer())
      return std::nullopt;
    const auto drawn = drawSynchronized();
    if (!drawn)
      return std::nullopt;
    const QSize targetSize = owner.renderTarget()->pixelSize();
    if (targetSize.isEmpty()) {
      state->invalidate();
      return std::nullopt;
    }
    const bool scissorEnabled = renderState && renderState->scissorEnabled();
    const bool stencilEnabled = renderState && renderState->stencilEnabled();
    const QRect scissor = scissorEnabled ? renderState->scissorRect()
                                         : QRect{QPoint{}, targetSize};
    if (renderState && renderState->clipRegion() &&
        !renderState->clipRegion()->isEmpty() && !scissorEnabled &&
        !stencilEnabled) {
      fail(diagnostic("desktop.sketch.unsupported-rhi-clip",
                      "sketch renderer cannot represent the active clip"),
           true);
      return std::nullopt;
    }

    QRhiCommandBuffer *commands = owner.commandBuffer();
    commands->setGraphicsPipeline(stencilEnabled ? stencilPipeline.get()
                                                 : pipeline.get());
    commands->setViewport(
        QRhiViewport{0.0F, 0.0F, static_cast<float>(targetSize.width()),
                     static_cast<float>(targetSize.height())});
    commands->setScissor(QRhiScissor{scissor.x(), scissor.y(), scissor.width(),
                                     scissor.height()});
    if (stencilEnabled)
      commands->setStencilRef(
          static_cast<quint32>(renderState->stencilValue()));

    const auto drawMesh = [&](const SketchSceneMesh &mesh,
                              const SketchChunkSequence &sequence,
                              std::size_t layer) -> bool {
      const auto chunks = mesh.chunks();
      const auto styles = mesh.styles();
      for (std::size_t index = 0U; index < sequence.size(); ++index) {
        const auto &chunk = chunks[sequence[index]];
        const auto found = resources.find(chunk.get());
        if (found == resources.end()) {
          fail(diagnostic("desktop.sketch.missing-gpu-chunk",
                          "presented sketch frame lost a GPU chunk"),
               true);
          return false;
        }
        const render::SketchStyle &style = styles[chunk->style()];
        const auto offset = static_cast<quint32>(
            uniformStride * uniformIndex(style.role, style.linePattern, layer));
        const QRhiCommandBuffer::DynamicOffset dynamicOffset{0, offset};
        commands->setShaderResources(resourceBindings.get(), 1, &dynamicOffset);
        const QRhiCommandBuffer::VertexInput binding{
            found->second.vertices.get(), 0U};
        commands->setVertexInput(0, 1, &binding, found->second.indices.get(),
                                 0U, QRhiCommandBuffer::IndexUInt32);
        commands->drawIndexed(static_cast<quint32>(chunk->indices().size()));
      }
      return true;
    };
    const GpuLayer &baseLayer =
        presented->layers[layerIndex(GeometryLayer::Base)];
    if (!drawMesh(*presented->synchronized->mesh(), baseLayer.chunks,
                  layerIndex(GeometryLayer::Base)))
      return std::nullopt;
    const auto &overlay = presented->synchronized->products()->overlay();
    if (overlay) {
      const SketchSceneMesh &mesh = *presented->synchronized->mesh();
      const auto chunks = mesh.chunks();
      const auto styles = mesh.styles();
      std::size_t drawCalls = 0U;
      for (const PreparedSketchOverlayRoleSetPtr &role : overlay->roleSets()) {
        if (!role)
          continue;
        const auto spans = role->drawSpans();
        for (std::size_t chunkIndex = 0U; chunkIndex < baseLayer.chunks.size();
             ++chunkIndex) {
          const std::uint32_t visibleChunk = baseLayer.chunks[chunkIndex];
          auto span =
              std::ranges::lower_bound(spans, visibleChunk, std::less<>{},
                                       &SketchPrimitiveChunkSpan::chunk);
          for (; span != spans.end() && span->chunk == visibleChunk; ++span) {
            if (++drawCalls > maximumOverlayDrawCallsPerFrame) {
              fail(diagnostic(
                       "desktop.sketch.overlay-draw-call-bound",
                       "visible sketch overlay exceeds its draw-call bound"),
                   true);
              return std::nullopt;
            }
            const auto &chunk = chunks[visibleChunk];
            const auto resource = resources.find(chunk.get());
            if (resource == resources.end()) {
              fail(diagnostic("desktop.sketch.missing-overlay-gpu-chunk",
                              "sketch overlay lost a resident GPU chunk"),
                   true);
              return std::nullopt;
            }
            const render::SketchStyle &baseStyle = styles[chunk->style()];
            const auto offset = static_cast<quint32>(
                uniformStride * uniformIndex(overlayStyleRole(role->role()),
                                             baseStyle.linePattern, 0U));
            const QRhiCommandBuffer::DynamicOffset dynamicOffset{0, offset};
            commands->setShaderResources(resourceBindings.get(), 1,
                                         &dynamicOffset);
            const QRhiCommandBuffer::VertexInput binding{
                resource->second.vertices.get(), 0U};
            commands->setVertexInput(0, 1, &binding,
                                     resource->second.indices.get(), 0U,
                                     QRhiCommandBuffer::IndexUInt32);
            commands->drawIndexed(span->indexCount, 1U, span->firstIndex);
          }
        }
      }
    }
    constexpr std::array<GeometryLayer, 3> decorationOrder{
        GeometryLayer::OverlayPoints,
        GeometryLayer::Provisional,
        GeometryLayer::Markers,
    };
    for (const GeometryLayer layer : decorationOrder) {
      const std::size_t index = layerIndex(layer);
      const auto &mesh =
          productMesh(*presented->synchronized->products(), index);
      if (mesh && !drawMesh(*mesh, presented->layers[index].chunks, index))
        return std::nullopt;
    }

    if (drawn != presented->synchronized) {
      state->invalidate();
      return std::nullopt;
    }

    SketchPresentationEvidence evidence;
    evidence.palette = palette;
    evidence.inheritedOpacity = static_cast<float>(owner.inheritedOpacity());
    evidence.scissorEnabled = scissorEnabled;
    evidence.scissorDevicePixels = scissor;
    evidence.stencilEnabled = stencilEnabled;
    evidence.stencilValue = renderState ? renderState->stencilValue() : 0;
    if (renderState && renderState->clipRegion())
      evidence.clipRegionDevicePixels = *renderState->clipRegion();
    evidence.itemToClip = *owner.projectionMatrix() * *owner.matrix();
    evidence.devicePixelRatio = owner.renderTarget()->devicePixelRatio();
    evidence.deviceViewportPixels = targetSize;
    evidence.renderEpoch = renderEpoch;
    evidence.frameSerial = ++frameSerial;
    evidence.renderTargetFormat = publishedRenderPassFormat;
    SketchPresentedProductCoverage coverage;
    coverage.overlayPoints =
        presented->layers[layerIndex(GeometryLayer::OverlayPoints)].coverage;
    coverage.provisional =
        presented->layers[layerIndex(GeometryLayer::Provisional)].coverage;
    coverage.markers =
        presented->layers[layerIndex(GeometryLayer::Markers)].coverage;
    return RecordedFrame{presented->synchronized, std::move(coverage),
                         std::move(evidence)};
  }

  void release() {
    state->invalidate();
    pipeline.reset();
    stencilPipeline.reset();
    resourceBindings.reset();
    uniformBuffer.reset();
    resources.clear();
    renderPassFormat.clear();
    publishedRenderPassFormat.reset();
    sampleCount = 0;
    uniformStride = 0U;
    uniformData.clear();
    rhi = nullptr;
    ++renderEpoch;

    RendererCpuArtifact artifact;
    artifact.desired = std::move(desired);
    if (presented) {
      artifact.presented = std::move(presented->synchronized);
      for (GpuLayer &layer : presented->layers) {
        artifact.sequences[artifact.sequenceCount++] = std::move(layer.chunks);
        if (layer.coverage)
          artifact.coverages[artifact.coverageCount++] =
              std::move(layer.coverage);
      }
    }
    if (staging) {
      artifact.staging = std::move(staging->synchronized);
      for (std::size_t layerIndex = 0U; layerIndex < geometryLayerCount;
           ++layerIndex) {
        StagingLayer &layer = staging->layers[layerIndex];
        artifact.visibility[layerIndex] = std::move(layer.visibility);
        artifact.upload[layerIndex] = std::move(layer.upload);
        if (layer.completedChunks)
          artifact.sequences[artifact.sequenceCount++] =
              std::move(*layer.completedChunks);
        if (layer.coverage)
          artifact.coverages[artifact.coverageCount++] =
              std::move(layer.coverage);
      }
    }
    for (std::size_t index = 0U; index < retiredCount; ++index) {
      auto &entry = retired[retiredIndex(index)];
      if (entry) {
        artifact.retired[index] = std::move(entry->synchronized);
        for (std::size_t layerIndex = 0U; layerIndex < geometryLayerCount;
             ++layerIndex) {
          RetiredLayer &layer = entry->layers[layerIndex];
          artifact.sequences[artifact.sequenceCount++] =
              std::move(layer.chunks);
          if (layer.coverage)
            artifact.coverages[artifact.coverageCount++] =
                std::move(layer.coverage);
          artifact.retiredVisibility[index][layerIndex] =
              std::move(layer.visibility);
          artifact.retiredUpload[index][layerIndex] = std::move(layer.upload);
        }
      }
      entry.reset();
    }
    presented.reset();
    staging.reset();
    retiredRead = 0U;
    retiredCount = 0U;
    if (artifact.desired || artifact.presented || artifact.staging ||
        std::ranges::any_of(artifact.visibility,
                            [](const auto &value) { return bool(value); }) ||
        std::ranges::any_of(artifact.upload,
                            [](const auto &value) { return bool(value); }) ||
        artifact.sequenceCount != 0U || artifact.coverageCount != 0U ||
        std::ranges::any_of(artifact.retired, [](const auto &retained) {
          return bool(retained);
        }))
      reclaimSynchronouslyEnqueued(std::move(artifact));
  }
};

SketchFrameRenderer::SketchFrameRenderer(
    QQuickWindow &window, std::shared_ptr<SketchFrameRendererState> state)
    : impl_(std::make_unique<Impl>(*this, window, std::move(state))) {}

SketchFrameRenderer::~SketchFrameRenderer() = default;

void SketchFrameRenderer::synchronize(
    std::shared_ptr<const SynchronizedSketchScene> desired,
    SketchScenePalette palette, std::uint64_t paletteGeneration,
    QRectF itemRect) {
  impl_->desired = std::move(desired);
  impl_->palette = palette;
  impl_->paletteGeneration = paletteGeneration;
  impl_->itemRect = itemRect;
  markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);
}

void SketchFrameRenderer::prepare() { impl_->prepare(); }

void SketchFrameRenderer::render(const RenderState *renderState) {
  auto recorded = impl_->record(renderState);
  if (!recorded)
    return;
  try {
    const SketchMeshMetrics meshMetrics =
        recorded->synchronized->mesh()->metrics();
    const bool exactCurrent =
        impl_->desired && sameView(*recorded->synchronized, *impl_->desired);
    auto presented = std::shared_ptr<const PresentedSketchFrame>(
        new PresentedSketchFrame{std::move(recorded->synchronized),
                                 std::move(recorded->productCoverage),
                                 std::move(recorded->evidence)});
    impl_->state->publish(std::move(presented), meshMetrics, impl_->metrics,
                          impl_->geometryBuildCount, exactCurrent);
  } catch (const std::bad_alloc &) {
    impl_->fail(diagnostic("desktop.sketch.presented-frame-allocation",
                           "sketch presented frame allocation failed"),
                true);
  }
}

void SketchFrameRenderer::releaseResources() { impl_->release(); }

QSGRenderNode::RenderingFlags SketchFrameRenderer::flags() const {
  return QSGRenderNode::NoExternalRendering |
         QSGRenderNode::DepthAwareRendering |
         QSGRenderNode::BoundedRectRendering;
}

QSGRenderNode::StateFlags SketchFrameRenderer::changedStates() const {
  return QSGRenderNode::ViewportState | QSGRenderNode::ScissorState;
}

QRectF SketchFrameRenderer::rect() const { return impl_->itemRect; }

bool shutdownSketchSceneResources(
    std::chrono::milliseconds drainTimeout) noexcept {
  RendererReclaimer &reclaimer = sharedRendererReclaimer();
  const bool drained = reclaimer.waitUntilEmpty(drainTimeout);
  reclaimer.shutdown();
  return drained && reclaimer.metrics().outstanding == 0U;
}

} // namespace kearne::ui
