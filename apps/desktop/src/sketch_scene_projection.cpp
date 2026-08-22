#include "sketch_scene_projection.hpp"
#include "sketch_prepared_products.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <new>
#include <ranges>
#include <utility>

namespace kearne::ui {
namespace {

[[nodiscard]] bool finite(QPointF point) {
  return std::isfinite(point.x()) && std::isfinite(point.y());
}

[[nodiscard]] bool finite(render::Point2d point) {
  return std::isfinite(point.x) && std::isfinite(point.y);
}

[[nodiscard]] bool samePickOptions(render::SketchPickIndexOptions first,
                                   render::SketchPickIndexOptions second) {
  return first.maximumRetainedBytes == second.maximumRetainedBytes &&
         first.maximumScratchBytes == second.maximumScratchBytes &&
         first.maximumPeakBuildBytes == second.maximumPeakBuildBytes &&
         first.maximumLeafTargets == second.maximumLeafTargets &&
         first.maximumVisitedNodesPerPass ==
             second.maximumVisitedNodesPerPass &&
         first.maximumRefinedTargetsPerPass ==
             second.maximumRefinedTargetsPerPass;
}

[[nodiscard]] bool validPickCoverage(SketchPickCoveragePolicy policy) {
  return policy.generation != 0U &&
         std::isfinite(policy.maximumToleranceLogicalPixels) &&
         policy.maximumToleranceLogicalPixels >= 0.0 &&
         policy.maximumToleranceLogicalPixels <=
             SketchPickCoveragePolicy::
                 maximumConfigurableToleranceLogicalPixels &&
         policy.maximumCurveEvaluations != 0U &&
         policy.maximumCurveEvaluations <=
             SketchPickCoveragePolicy::maximumConfigurableCurveEvaluations &&
         policy.maximumResidentSpanProbes != 0U &&
         policy.maximumResidentSpanProbes <=
             SketchPickCoveragePolicy::maximumConfigurableResidentSpanProbes;
}

[[nodiscard]] SketchVectorKind kind(render::SketchPrimitiveKind value) {
  switch (value) {
  case render::SketchPrimitiveKind::Point:
    return SketchVectorKind::Point;
  case render::SketchPrimitiveKind::Line:
    return SketchVectorKind::Line;
  case render::SketchPrimitiveKind::Circle:
    return SketchVectorKind::Circle;
  case render::SketchPrimitiveKind::Arc:
    return SketchVectorKind::Arc;
  case render::SketchPrimitiveKind::Ellipse:
    return SketchVectorKind::Ellipse;
  case render::SketchPrimitiveKind::EllipticalArc:
    return SketchVectorKind::EllipticalArc;
  case render::SketchPrimitiveKind::HyperbolicArc:
    return SketchVectorKind::HyperbolicArc;
  case render::SketchPrimitiveKind::ParabolicArc:
    return SketchVectorKind::ParabolicArc;
  case render::SketchPrimitiveKind::BSpline:
    return SketchVectorKind::BSpline;
  }
  return SketchVectorKind::Point;
}

[[nodiscard]] SketchVectorSourcePrimitive
basePrimitiveAt(const void *context, std::size_t index) noexcept {
  const auto &scene = *static_cast<const render::SketchSceneSnapshot *>(context);
  const auto primitive = scene.primitives()[index];
  const std::size_t pointCount =
      primitive.kind == render::SketchPrimitiveKind::Line
          ? 2U
          : primitive.kind == render::SketchPrimitiveKind::BSpline ? 0U : 1U;
  const auto points = scene.points().subspan(primitive.firstPoint, pointCount);
  return {primitive.handle.value(),
          primitive.style,
          kind(primitive.kind),
          render::hasFlag(primitive.flags,
                          render::SketchPrimitiveFlags::Visible),
          pointCount == 0U ? render::Point2d{} : points[0],
          pointCount == 2U ? points[1] : render::Point2d{},
          primitive.radius,
          primitive.startAngleRadians,
          primitive.sweepAngleRadians,
          0U,
          primitive.secondaryRadius,
          primitive.rotationAngleRadians};
}

[[nodiscard]] sketch::NurbsView
baseSplineAt(const void *context, std::size_t index) noexcept {
  const auto &scene = *static_cast<const render::SketchSceneSnapshot *>(context);
  const render::PackedSketchPrimitive primitive = scene.primitives()[index];
  if (primitive.kind != render::SketchPrimitiveKind::BSpline ||
      primitive.spline >= scene.splines().size())
    return {};
  const render::PackedSketchSpline spline = scene.splines()[primitive.spline];
  return {scene.splineControlPointCoordinates().subspan(
              spline.firstControlPoint * 2U,
              spline.controlPointCount * 2U),
          scene.splineKnots().subspan(
              spline.firstKnot,
              spline.controlPointCount + spline.degree + 1U),
          scene.splineWeights().subspan(spline.firstWeight,
                                        spline.controlPointCount),
          spline.degree};
}

[[nodiscard]] SketchVectorSourceBounds
sourceBounds(const render::SketchSceneSnapshot &scene) {
  return {scene.bounds().minimum, scene.bounds().maximum, scene.bounds().empty};
}

struct BuiltBaseVectorPacket {
  SketchVectorPacket packet;
  std::vector<SketchVectorPrimitiveSpanRecord> provenance;
  std::size_t indexedPrimitives = 0U;
  std::size_t retainedBytes = 0U;
};

[[nodiscard]] Result<BuiltBaseVectorPacket>
buildBaseVectorPacket(const render::SketchSceneSnapshot &scene,
                      SketchVectorUploadOptions upload,
                      std::shared_ptr<const SketchVectorPacket> reuse,
                      std::stop_token cancellation) {
  const SketchVectorSource source{scene.styles(),
                                  &scene,
                                  scene.primitives().size(),
                                  basePrimitiveAt,
                                  sourceBounds(scene),
                                  baseSplineAt};
  auto built = SketchVectorPacketBuildAccess::build(
      source, upload, std::move(reuse), cancellation);
  if (!built)
    return std::unexpected(std::move(built.error()));
  return BuiltBaseVectorPacket{std::move(built->packet),
                               std::move(built->provenance),
                               built->indexedPrimitives,
                               built->retainedBytes};
}

} // namespace

QRgb SketchScenePalette::color(render::SketchStyleRole role) const {
  switch (role) {
  case render::SketchStyleRole::Regular:
    return regular;
  case render::SketchStyleRole::Construction:
    return construction;
  case render::SketchStyleRole::Selected:
    return selected;
  case render::SketchStyleRole::Preview:
    return preview;
  case render::SketchStyleRole::Diagnostic:
    return diagnostic;
  case render::SketchStyleRole::Hovered:
    return hovered;
  }
  return regular;
}

SketchPrimitiveVectorIndex::SketchPrimitiveVectorIndex(
    std::vector<SketchPrimitiveVectorEntry> entries,
    std::vector<SketchPrimitiveChunkSpan> spans, std::size_t retainedBytes)
    : entries_(std::move(entries)), spans_(std::move(spans)),
      retainedBytes_(retainedBytes) {}

const SketchPrimitiveVectorEntry *SketchPrimitiveVectorIndex::find(
    render::SketchPrimitiveHandle primitive) const {
  const auto found = std::ranges::lower_bound(
      entries_, primitive, {}, &SketchPrimitiveVectorEntry::primitive);
  return found != entries_.end() && found->primitive == primitive ? &*found
                                                                  : nullptr;
}

std::span<const SketchPrimitiveChunkSpan>
SketchPrimitiveVectorIndex::spans(
    render::SketchPrimitiveHandle primitive) const {
  const auto *entry = find(primitive);
  return entry ? std::span<const SketchPrimitiveChunkSpan>{spans_}.subspan(
                     entry->firstSpan, entry->spanCount)
               : std::span<const SketchPrimitiveChunkSpan>{};
}

Result<SketchVectorPacket>
buildSketchVectorPacket(const render::SketchSceneSnapshot &scene,
                        SketchVectorUploadOptions upload,
                        std::shared_ptr<const SketchVectorPacket> reuse,
                        std::stop_token cancellation) {
  auto built = buildBaseVectorPacket(scene, upload, std::move(reuse),
                                     cancellation);
  if (!built)
    return std::unexpected(std::move(built.error()));
  return std::move(built->packet);
}

PreparedSketchScene::PreparedSketchScene(
    render::SceneStamp stamp,
    std::shared_ptr<const render::SketchSceneSnapshot> scene,
    std::shared_ptr<const render::SketchPickIndex> pickIndex,
    render::SketchPickIndexOptions pickOptions,
    std::shared_ptr<const SketchVectorPacket> packet,
    std::shared_ptr<const SketchPrimitiveVectorIndex> primitiveVectorIndex,
    Metrics metrics)
    : stamp_(std::move(stamp)), scene_(std::move(scene)),
      pickIndex_(std::move(pickIndex)), pickOptions_(pickOptions),
      packet_(std::move(packet)),
      primitiveVectorIndex_(std::move(primitiveVectorIndex)), metrics_(metrics) {}

Result<std::shared_ptr<const PreparedSketchScene>> prepareSketchScene(
    std::shared_ptr<const render::SketchSceneSnapshot> scene,
    render::SketchPickIndexOptions picking, SketchVectorUploadOptions upload,
    std::shared_ptr<const PreparedSketchScene> reuse,
    std::stop_token cancellation) {
  if (!scene)
    return std::unexpected(diagnostic("desktop.sketch.null-vector-scene",
                                      "Cannot prepare a null Sketch scene"));
  if (reuse && reuse->scene() == scene &&
      samePickOptions(reuse->pickOptions(), picking))
    return reuse;
  auto built = buildBaseVectorPacket(
      *scene, upload, reuse ? reuse->packet() : nullptr, cancellation);
  if (!built)
    return std::unexpected(std::move(built.error()));
  auto pick = render::SketchPickIndex::build(scene, picking, cancellation);
  if (!pick)
    return std::unexpected(std::move(pick.error()));
  try {
    std::vector<SketchPrimitiveVectorEntry> entries;
    std::vector<SketchPrimitiveChunkSpan> spans;
    entries.reserve(built->indexedPrimitives);
    spans.reserve(built->provenance.size());
    std::size_t cursor = 0U;
    while (cursor < built->provenance.size()) {
      const std::uint32_t source = built->provenance[cursor].sourceKey;
      const std::uint32_t firstSpan = static_cast<std::uint32_t>(spans.size());
      std::uint32_t recordCount = 0U;
      do {
        const auto &value = built->provenance[cursor++];
        spans.push_back({value.chunk, value.firstRecord, value.recordCount});
        recordCount += value.recordCount;
      } while (cursor < built->provenance.size() &&
               built->provenance[cursor].sourceKey == source);
      auto handle = render::SketchPrimitiveHandle::create(source);
      if (!handle)
        return std::unexpected(std::move(handle.error()));
      entries.push_back({*handle, firstSpan,
                         static_cast<std::uint32_t>(spans.size() - firstSpan),
                         recordCount});
    }
    const std::size_t provenanceBytes =
        entries.capacity() * sizeof(SketchPrimitiveVectorEntry) +
        spans.capacity() * sizeof(SketchPrimitiveChunkSpan) +
        sizeof(SketchPrimitiveVectorIndex);
    auto index = std::shared_ptr<const SketchPrimitiveVectorIndex>(
        new SketchPrimitiveVectorIndex{std::move(entries), std::move(spans),
                                       provenanceBytes});
    auto packet = std::make_shared<const SketchVectorPacket>(
        std::move(built->packet));
    PreparedSketchScene::Metrics metrics{
        packet->metrics().retainedBytes, provenanceBytes,
        pick->retainedBytes(), packet->metrics().retainedBytes +
                                   provenanceBytes + pick->retainedBytes()};
    return std::shared_ptr<const PreparedSketchScene>(new PreparedSketchScene{
        scene->stamp(), std::move(scene),
        std::make_shared<const render::SketchPickIndex>(std::move(*pick)),
        picking, std::move(packet), std::move(index), metrics});
  } catch (const std::bad_alloc &) {
    return std::unexpected(diagnostic("desktop.sketch.vector-scene-memory",
                                      "Sketch vector scene ran out of memory"));
  }
}

ProgressiveSketchUpload::ProgressiveSketchUpload(
    std::shared_ptr<const SketchVectorPacket> packet,
    SketchChunkSequence requiredChunks,
    std::vector<const SketchVectorChunk *> resident)
    : packet_(std::move(packet)), requiredChunks_(std::move(requiredChunks)),
      residentChunks_(std::move(resident)) {}

Result<ProgressiveSketchUpload> ProgressiveSketchUpload::create(
    std::shared_ptr<const PreparedSketchScene> prepared,
    std::vector<std::uint32_t> requiredChunks,
    std::span<const std::shared_ptr<const SketchVectorChunk>> resident) {
  if (!prepared)
    return std::unexpected(diagnostic("desktop.sketch.null-vector-upload",
                                      "Sketch vector upload is missing"));
  auto sequence = SketchChunkSequence::create(*prepared->packet(), requiredChunks);
  if (!sequence)
    return std::unexpected(std::move(sequence.error()));
  return create(prepared->packet(), std::move(*sequence), resident);
}

Result<ProgressiveSketchUpload> ProgressiveSketchUpload::create(
    std::shared_ptr<const PreparedSketchScene> prepared,
    SketchChunkSequence requiredChunks,
    std::span<const std::shared_ptr<const SketchVectorChunk>> resident) {
  if (!prepared)
    return std::unexpected(diagnostic("desktop.sketch.null-vector-upload",
                                      "Sketch vector upload is missing"));
  return create(prepared->packet(), std::move(requiredChunks), resident);
}

Result<ProgressiveSketchUpload> ProgressiveSketchUpload::create(
    std::shared_ptr<const SketchVectorPacket> packet,
    SketchChunkSequence requiredChunks,
    std::span<const std::shared_ptr<const SketchVectorChunk>> resident) {
  if (!packet || requiredChunks.packet_ != packet.get())
    return std::unexpected(diagnostic("desktop.sketch.invalid-vector-upload",
                                      "Sketch vector upload does not match its packet"));
  try {
    std::vector<const SketchVectorChunk *> identities;
    identities.reserve(resident.size());
    for (const auto &chunk : resident)
      if (chunk)
        identities.push_back(chunk.get());
    std::ranges::sort(identities);
    identities.erase(std::ranges::unique(identities).begin(), identities.end());
    return ProgressiveSketchUpload{std::move(packet),
                                   std::move(requiredChunks),
                                   std::move(identities)};
  } catch (const std::bad_alloc &) {
    return std::unexpected(diagnostic("desktop.sketch.vector-upload-memory",
                                      "Sketch vector upload ran out of memory"));
  }
}

Result<SketchUploadSlice> ProgressiveSketchUpload::takeNextSlice(
    std::size_t maximumBytes, std::size_t maximumChunks) {
  if (maximumBytes == 0U || maximumChunks == 0U)
    return std::unexpected(diagnostic("desktop.sketch.invalid-vector-slice",
                                      "Sketch vector upload slice is invalid"));
  SketchUploadSlice result;
  while (cursor_ < requiredChunks_.size() &&
         result.entries.size() < maximumChunks) {
    const std::uint32_t index = requiredChunks_[cursor_];
    const auto &chunk = packet_->chunks()[index];
    const bool reuse = std::ranges::binary_search(residentChunks_, chunk.get());
    const std::size_t bytes = reuse ? 0U : chunk->payloadBytes();
    if (!result.entries.empty() && bytes > maximumBytes - result.bytes)
      break;
    if (result.entries.empty() && bytes > maximumBytes)
      return std::unexpected(diagnostic("desktop.sketch.vector-slice-limit",
                                        "Sketch vector chunk exceeds upload slice"));
    result.entries.push_back({index, reuse});
    result.bytes += bytes;
    reusedCount_ += reuse ? 1U : 0U;
    ++cursor_;
  }
  return result;
}

SketchChunkSequence ProgressiveSketchUpload::releaseRequiredChunks() {
  return std::move(requiredChunks_);
}

SynchronizedSketchScene::SynchronizedSketchScene(
    std::shared_ptr<const PreparedSketchProducts> products,
    SketchViewTransform transform, SketchPickCoveragePolicy pickCoverage,
    std::shared_ptr<const SketchPresentedChunkCoverage> presentedChunks)
    : products_(std::move(products)), transform_(std::move(transform)),
      pickCoverage_(pickCoverage), presentedChunks_(std::move(presentedChunks)) {}

const std::shared_ptr<const PreparedSketchProducts> &
SynchronizedSketchScene::products() const {
  return products_;
}

const std::shared_ptr<const PreparedSketchScene> &
SynchronizedSketchScene::prepared() const {
  return products_->base();
}

const std::shared_ptr<const render::SketchSceneSnapshot> &
SynchronizedSketchScene::scene() const {
  return products_->base()->scene();
}

const std::shared_ptr<const render::SketchPickIndex> &
SynchronizedSketchScene::pickIndex() const {
  return products_->base()->pickIndex();
}

const std::shared_ptr<const SketchVectorPacket> &
SynchronizedSketchScene::packet() const {
  return products_->base()->packet();
}

PresentedSketchFrame::PresentedSketchFrame(
    std::shared_ptr<const SynchronizedSketchScene> synchronized,
    SketchPresentedProductCoverage productCoverage,
    SketchPresentationEvidence evidence)
    : synchronized_(std::move(synchronized)),
      productCoverage_(std::move(productCoverage)),
      evidence_(std::move(evidence)) {}

SketchScenePresenter::SketchScenePresenter() = default;

void SketchScenePresenter::retarget(render::SceneTarget desired) {
  std::scoped_lock lock{stateMutex_};
  desired_ = std::move(desired);
  latestAcceptedScene_.reset();
  pending_.reset();
}

Result<PreparedSketchSceneOffer> SketchScenePresenter::publish(
    std::shared_ptr<const PreparedSketchProducts> prepared) {
  if (!prepared)
    return std::unexpected(diagnostic("desktop.sketch.null-prepared-products",
                                      "Cannot publish null Sketch products"));
  std::scoped_lock lock{stateMutex_};
  if (!desired_)
    return std::unexpected(diagnostic("desktop.sketch.missing-target",
                                      "Sketch target is not set"));
  if (prepared->stamp().target != *desired_)
    return PreparedSketchSceneOffer{PreparedSketchSceneDecision::StaleTarget,
                                    false};
  std::shared_ptr<const PreparedSketchProducts> installed = pending_;
  if (!installed) {
    const auto current = current_.load(std::memory_order_acquire);
    if (current && current->products()->stamp().target == *desired_)
      installed = current->products();
  }
  if (installed) {
    if (prepared->stamp().generation < installed->stamp().generation)
      return PreparedSketchSceneOffer{
          PreparedSketchSceneDecision::StaleGeneration, false};
    if (prepared->stamp().generation == installed->stamp().generation) {
      if (prepared->stamp() != installed->stamp() ||
          !sameSketchSceneProductComponents(*prepared->source(),
                                            *installed->source()))
        return PreparedSketchSceneOffer{
            PreparedSketchSceneDecision::GenerationConflict, false};
      latestAcceptedScene_ = prepared->base()->stamp();
      return PreparedSketchSceneOffer{PreparedSketchSceneDecision::Duplicate,
                                      false};
    }
  }
  const bool replaced = static_cast<bool>(pending_);
  latestAcceptedScene_ = prepared->base()->stamp();
  pending_ = std::move(prepared);
  return PreparedSketchSceneOffer{PreparedSketchSceneDecision::Accepted,
                                  replaced};
}

Result<SketchCameraDecision>
SketchScenePresenter::publishCamera(SketchCamera2d camera) {
  if (camera.generation == 0U || !finite(camera.centerMetres) ||
      !std::isfinite(camera.metresPerLogicalPixel) ||
      camera.metresPerLogicalPixel <= 0.0 ||
      !std::isfinite(camera.rotationRadians))
    return std::unexpected(diagnostic("desktop.sketch.invalid-camera",
                                      "Sketch camera is invalid"));
  std::scoped_lock lock{stateMutex_};
  if (camera.generation < camera_.generation)
    return SketchCameraDecision::StaleGeneration;
  if (camera.generation == camera_.generation)
    return camera == camera_ ? SketchCameraDecision::Duplicate
                             : SketchCameraDecision::GenerationConflict;
  camera_ = camera;
  return SketchCameraDecision::Accepted;
}

Result<SketchPickCoverageDecision>
SketchScenePresenter::publishPickCoverage(SketchPickCoveragePolicy policy) {
  if (!validPickCoverage(policy))
    return std::unexpected(diagnostic("desktop.sketch.invalid-pick-coverage",
                                      "Sketch pick coverage is invalid"));
  std::scoped_lock lock{stateMutex_};
  if (policy.generation < pickCoverage_.generation)
    return SketchPickCoverageDecision::StaleGeneration;
  if (policy.generation == pickCoverage_.generation)
    return policy == pickCoverage_
               ? SketchPickCoverageDecision::Duplicate
               : SketchPickCoverageDecision::GenerationConflict;
  pickCoverage_ = policy;
  return SketchPickCoverageDecision::Accepted;
}

Result<std::shared_ptr<const SynchronizedSketchScene>>
SketchScenePresenter::synchronize(QSizeF viewportLogical) {
  std::shared_ptr<const PreparedSketchProducts> prepared;
  bool installed = false;
  SketchCamera2d camera;
  SketchPickCoveragePolicy coverage;
  {
    std::scoped_lock lock{stateMutex_};
    ++synchronizationMetrics_.calls;
    if (!desired_)
      return std::unexpected(diagnostic("desktop.sketch.missing-target",
                                        "Sketch target is not set"));
    camera = camera_;
    coverage = pickCoverage_;
    if (pending_) {
      prepared = std::exchange(pending_, {});
      installed = true;
    } else if (const auto current =
                   current_.load(std::memory_order_acquire)) {
      prepared = current->products();
    }
  }
  if (!prepared)
    return std::unexpected(diagnostic("desktop.sketch.missing-vector-scene",
                                      "No Sketch vector scene is available"));
  auto transform = SketchViewTransform::create(camera, viewportLogical);
  if (!transform)
    return std::unexpected(std::move(transform.error()));
  const auto previous = current_.load(std::memory_order_acquire);
  if (previous && previous->products() == prepared &&
      previous->transform().camera() == camera &&
      previous->transform().viewportLogical() == viewportLogical &&
      previous->pickCoverage() == coverage)
    return previous;
  auto synchronized = std::make_shared<const SynchronizedSketchScene>(
      std::move(prepared), std::move(*transform), coverage);
  current_.store(synchronized, std::memory_order_release);
  if (installed) {
    std::scoped_lock lock{stateMutex_};
    ++synchronizationMetrics_.preparedPacketInstalls;
  }
  return synchronized;
}

Result<SketchItemPickEvidence> SketchScenePresenter::pick(
    QPointF itemLogical, double toleranceLogicalPixels,
    render::SketchPickTargets targets) const {
  return pick(current_.load(std::memory_order_acquire), itemLogical,
              toleranceLogicalPixels, targets);
}

Result<SketchItemPickEvidence> SketchScenePresenter::pick(
    std::shared_ptr<const SynchronizedSketchScene> frame, QPointF itemLogical,
    double toleranceLogicalPixels, render::SketchPickTargets targets) const {
  if (!frame)
    return std::unexpected(diagnostic("desktop.sketch.missing-frame",
                                      "No Sketch frame is synchronized"));
  if (!finite(itemLogical) || !std::isfinite(toleranceLogicalPixels) ||
      toleranceLogicalPixels < 0.0 ||
      toleranceLogicalPixels >
          frame->pickCoverage().maximumToleranceLogicalPixels)
    return std::unexpected(diagnostic("desktop.sketch.invalid-pick",
                                      "Sketch pick is invalid"));
  const render::Point2d canonical = frame->transform().toCanonical(itemLogical);
  const double tolerance = toleranceLogicalPixels *
                           frame->transform().camera().metresPerLogicalPixel;
  const render::SketchPickOutcome outcome =
      frame->pickIndex()->query({canonical, tolerance, targets});
  if (outcome.status == render::SketchPickStatus::WorkBudgetExceeded)
    return std::unexpected(diagnostic("desktop.sketch.pick-budget",
                                      "Sketch pick exceeded its work budget"));
  if (outcome.status == render::SketchPickStatus::InvalidQuery ||
      outcome.status == render::SketchPickStatus::NonFiniteArithmetic)
    return std::unexpected(diagnostic("desktop.sketch.pick-arithmetic",
                                      "Sketch pick could not be evaluated"));
  std::optional<render::SketchPickResult> hit = outcome.result;
  std::uint32_t spanProbes = 0U;
  if (hit && frame->presentedChunks()) {
    const auto spans =
        frame->prepared()->primitiveVectorIndex()->spans(hit->primitive);
    bool resident = false;
    for (const auto span : spans) {
      if (++spanProbes > frame->pickCoverage().maximumResidentSpanProbes)
        return std::unexpected(diagnostic("desktop.sketch.pick-span-budget",
                                          "Sketch pick exceeded its resident span budget"));
      resident = resident || frame->presentedChunks()->contains(span.chunk);
    }
    if (!resident)
      hit.reset();
  }
  std::optional<render::SceneStamp> latest;
  {
    std::scoped_lock lock{stateMutex_};
    latest = latestAcceptedScene_;
  }
  SketchItemPickEvidence evidence{frame->scene()->stamp(),
                                  frame->products()->stamp()};
  evidence.products = frame->products()->source();
  evidence.latestAcceptedScene = latest;
  evidence.cameraGeneration = frame->transform().camera().generation;
  evidence.viewportLogical = frame->transform().viewportLogical();
  evidence.pickCoverage = frame->pickCoverage();
  evidence.canonicalPoint = canonical;
  evidence.canonicalToleranceMetres = tolerance;
  evidence.matchesLatestAcceptedScene =
      latest && *latest == frame->scene()->stamp();
  evidence.hit = std::move(hit);
  if (outcome.result)
    evidence.displayedDistanceLogicalPixels =
        outcome.result->distance /
        frame->transform().camera().metresPerLogicalPixel;
  evidence.analyticMetrics = outcome.metrics;
  evidence.renderedSpanProbes = spanProbes;
  evidence.renderedCurveEvaluations = outcome.metrics.refinedTargets;
  return evidence;
}

std::shared_ptr<const SynchronizedSketchScene>
SketchScenePresenter::current() const {
  return current_.load(std::memory_order_acquire);
}

namespace {
struct SketchPresenterRetirementOwner {
  std::shared_ptr<const PreparedSketchProducts> pending;
  std::shared_ptr<const SynchronizedSketchScene> current;
};
} // namespace

Result<std::shared_ptr<const void>>
SketchScenePresenter::retirementOwner() const {
  std::scoped_lock lock{stateMutex_};
  auto current = current_.load(std::memory_order_acquire);
  if (!pending_ && !current)
    return std::shared_ptr<const void>{};
  try {
    return std::static_pointer_cast<const void>(
        std::make_shared<const SketchPresenterRetirementOwner>(
            SketchPresenterRetirementOwner{pending_, std::move(current)}));
  } catch (const std::bad_alloc &) {
    return std::unexpected(diagnostic("desktop.sketch.retirement-memory",
                                      "Sketch retirement ran out of memory"));
  }
}

void SketchScenePresenter::clear() {
  std::scoped_lock lock{stateMutex_};
  desired_.reset();
  latestAcceptedScene_.reset();
  pending_.reset();
  current_.store({}, std::memory_order_release);
}

SketchSynchronizationMetrics
SketchScenePresenter::synchronizationMetrics() const {
  std::scoped_lock lock{stateMutex_};
  return synchronizationMetrics_;
}

std::size_t SketchScenePresenter::pendingCount() const {
  std::scoped_lock lock{stateMutex_};
  return pending_ ? 1U : 0U;
}

} // namespace kearne::ui
