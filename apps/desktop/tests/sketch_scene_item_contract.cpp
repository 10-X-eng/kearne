#include "sketch_scene_fixture.hpp"
#include "sketch_scene_item.hpp"
#include "sketch_stroke_mesh_build.hpp"
#include "sketch_stroke_pattern.hpp"

#include "bounded_artifact_reclaimer.hpp"

#include <kearne/testkit/property.hpp>

#include <QGuiApplication>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <numbers>
#include <numeric>
#include <optional>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

thread_local bool measureAllocations = false;
thread_local std::size_t measuredAllocations = 0;
thread_local std::size_t maximumMeasuredAllocationBytes = 0U;
struct PreparationAllocationGate {
  std::atomic<bool> reached = false;
  std::atomic<bool> release = false;
  std::size_t stopAtAllocation = 1U;
  std::size_t allocations = 0U;
};
thread_local PreparationAllocationGate *preparationAllocationGate = nullptr;

#if defined(__GNUC__) || defined(__clang__)
[[gnu::noinline]]
#endif
void *allocateMemory(std::size_t size) {
  return std::malloc(size == 0U ? 1U : size);
}

#if defined(__GNUC__) || defined(__clang__)
[[gnu::noinline]]
#endif
void releaseMemory(void *memory) {
  std::free(memory);
}

} // namespace

void *operator new(std::size_t size) {
  if (preparationAllocationGate) {
    PreparationAllocationGate *gate = preparationAllocationGate;
    ++gate->allocations;
    if (gate->allocations == gate->stopAtAllocation) {
      preparationAllocationGate = nullptr;
      gate->reached.store(true, std::memory_order_release);
      while (!gate->release.load(std::memory_order_acquire))
        std::this_thread::yield();
    }
  }
  if (measureAllocations)
    ++measuredAllocations;
  if (measureAllocations)
    maximumMeasuredAllocationBytes =
        std::max(maximumMeasuredAllocationBytes, size);
  if (void *memory = allocateMemory(size))
    return memory;
  throw std::bad_alloc{};
}

void *operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void *memory) noexcept { releaseMemory(memory); }
void operator delete[](void *memory) noexcept { releaseMemory(memory); }
void operator delete(void *memory, std::size_t) noexcept {
  releaseMemory(memory);
}
void operator delete[](void *memory, std::size_t) noexcept {
  releaseMemory(memory);
}

namespace {

using namespace kearne;
using namespace kearne::render;
using namespace kearne::ui;
using namespace kearne::ui::test;

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

SketchStrokeSourcePrimitive
snapshotStrokePrimitive(const void *opaque, std::size_t index) noexcept {
  const auto &snapshot = *static_cast<const SketchSceneSnapshot *>(opaque);
  const PackedSketchPrimitive &primitive = snapshot.primitives()[index];
  SketchStrokeSourceKind kind = SketchStrokeSourceKind::Point;
  switch (primitive.kind) {
  case SketchPrimitiveKind::Point:
    kind = SketchStrokeSourceKind::Point;
    break;
  case SketchPrimitiveKind::Line:
    kind = SketchStrokeSourceKind::Line;
    break;
  case SketchPrimitiveKind::Circle:
    kind = SketchStrokeSourceKind::Circle;
    break;
  case SketchPrimitiveKind::Arc:
    kind = SketchStrokeSourceKind::Arc;
    break;
  }
  const Point2d first = snapshot.points()[primitive.firstPoint];
  const Point2d second = primitive.kind == SketchPrimitiveKind::Line
                             ? snapshot.points()[primitive.firstPoint + 1U]
                             : first;
  return {primitive.handle.value(),
          primitive.style,
          kind,
          hasFlag(primitive.flags, SketchPrimitiveFlags::Visible),
          first,
          second,
          primitive.radius,
          primitive.startAngleRadians,
          primitive.sweepAngleRadians};
}

SketchStrokeMeshSource neutralSource(const SketchSceneSnapshot &snapshot) {
  const auto &bounds = snapshot.bounds();
  return {snapshot.styles(),
          &snapshot,
          snapshot.primitives().size(),
          snapshotStrokePrimitive,
          {bounds.minimum, bounds.maximum, bounds.empty}};
}

void requireSameNeutralMesh(const SketchSceneMesh &actual,
                            const SketchSceneMesh &expected) {
  require(actual.originMetres() == expected.originMetres() &&
              actual.lod() == expected.lod() &&
              std::ranges::equal(actual.styles(), expected.styles()) &&
              actual.metrics() == expected.metrics() &&
              actual.maximumChunkBytes() == expected.maximumChunkBytes() &&
              actual.spatialTileSizeMetres() ==
                  expected.spatialTileSizeMetres() &&
              actual.chunks().size() == expected.chunks().size(),
          "neutral and snapshot mesh metadata disagree");
  for (std::size_t index = 0U; index < actual.chunks().size(); ++index) {
    const auto &left = actual.chunks()[index];
    const auto &right = expected.chunks()[index];
    require(left->style() == right->style() &&
                left->layer() == right->layer() &&
                left->bounds() == right->bounds() &&
                left->payloadBytes() == right->payloadBytes() &&
                std::ranges::equal(left->vertices(), right->vertices()) &&
                std::ranges::equal(left->indices(), right->indices()),
            "neutral and snapshot chunk payloads disagree");
  }
}

void requireSamePick(const std::optional<SketchPickResult> &actual,
                     const std::optional<SketchPickResult> &expected) {
  require(actual.has_value() == expected.has_value(),
          "item and canonical pick presence disagree");
  if (!actual)
    return;
  require(actual->scene == expected->scene &&
              actual->entity == expected->entity &&
              actual->primitive == expected->primitive &&
              actual->pointKey == expected->pointKey &&
              std::hypot(actual->closestPoint.x - expected->closestPoint.x,
                         actual->closestPoint.y - expected->closestPoint.y) <=
                  1.0e-12 &&
              std::abs(actual->distance - expected->distance) <= 1.0e-12,
          "item and canonical pick identity disagree");
}

testkit::PropertyProfile
boundedPreparationProfile(const testkit::PropertyProfile &profile) {
  testkit::PropertyProfile bounded = profile;
  bounded.iterations = static_cast<std::uint64_t>(
      std::ceil(std::pow(static_cast<double>(profile.iterations), 0.75)));
  return bounded;
}

struct ReclaimerProbeState {
  std::mutex mutex;
  std::condition_variable changed;
  std::thread::id producer;
  std::vector<std::thread::id> releaseThreads;
  bool workerEntered = false;
  bool releaseWorker = false;
};

struct ReclaimerProbe {
  std::shared_ptr<ReclaimerProbeState> state;
  bool blocks = false;
  bool armed = true;

  ReclaimerProbe(std::shared_ptr<ReclaimerProbeState> nextState,
                 bool shouldBlock = false) noexcept
      : state(std::move(nextState)), blocks(shouldBlock) {}
  ReclaimerProbe(ReclaimerProbe &&other) noexcept
      : state(std::move(other.state)), blocks(other.blocks),
        armed(std::exchange(other.armed, false)) {}
  ReclaimerProbe &operator=(ReclaimerProbe &&) = delete;
  ReclaimerProbe(const ReclaimerProbe &) = delete;
  ReclaimerProbe &operator=(const ReclaimerProbe &) = delete;

  ~ReclaimerProbe() noexcept {
    if (!armed)
      return;
    std::unique_lock lock{state->mutex};
    if (blocks) {
      state->workerEntered = true;
      state->changed.notify_all();
      state->changed.wait(lock, [&] { return state->releaseWorker; });
    }
    state->releaseThreads.push_back(std::this_thread::get_id());
    state->changed.notify_all();
  }
};

void verifyBoundedReclaimer(const testkit::PropertyProfile &profile) {
  BoundedArtifactReclaimer<std::uint64_t, 2U> allocationChecked;
  measuredAllocations = 0U;
  measureAllocations = true;
  const bool allocationCheckedAccepted =
      allocationChecked.tryReclaim([]() noexcept { return 1U; });
  measureAllocations = false;
  require(allocationCheckedAccepted && measuredAllocations == 0U &&
              allocationChecked.waitUntilEmpty(std::chrono::seconds{2}),
          "reclaimer submission allocated on the producer thread");

  auto blockedState = std::make_shared<ReclaimerProbeState>();
  blockedState->producer = std::this_thread::get_id();
  BoundedArtifactReclaimer<ReclaimerProbe, 4U> blocked;
  require(blocked.tryReclaim(
              [&]() noexcept { return ReclaimerProbe{blockedState, true}; }),
          "reclaimer rejected its first artifact");
  {
    std::unique_lock lock{blockedState->mutex};
    require(blockedState->changed.wait_for(
                lock, std::chrono::seconds{2},
                [&] { return blockedState->workerEntered; }),
            "reclaimer worker did not consume its first artifact");
  }
  for (std::size_t index = 0; index < 4U; ++index)
    require(blocked.tryReclaim(
                [&]() noexcept { return ReclaimerProbe{blockedState}; }),
            "reclaimer saturated before its fixed capacity");
  std::optional<ReclaimerProbe> retained{std::in_place, blockedState, false};
  std::size_t failedFactories = 0;
  require(!blocked.tryReclaim([&]() noexcept {
    ++failedFactories;
    return std::move(*retained);
  }) && retained->armed &&
              failedFactories == 0U,
          "saturated reclaimer moved or dropped the caller artifact");
  const ArtifactReclaimerMetrics saturated = blocked.metrics();
  require(saturated.accepted == 5U && saturated.outstanding == 5U &&
              saturated.maximumQueued == 4U && saturated.saturated == 1U,
          "reclaimer saturation accounting is inconsistent");
  {
    std::scoped_lock lock{blockedState->mutex};
    blockedState->releaseWorker = true;
  }
  blockedState->changed.notify_all();
  require(blocked.waitUntilEmpty(std::chrono::seconds{2}),
          "reclaimer did not recover after worker unavailability");
  blocked.shutdown();
  const ArtifactReclaimerMetrics drained = blocked.metrics();
  require(drained.accepted == drained.released && drained.released == 5U &&
              drained.outstanding == 0U &&
              blockedState->releaseThreads.size() == 5U &&
              std::ranges::none_of(blockedState->releaseThreads,
                                   [&](std::thread::id thread) {
                                     return thread == blockedState->producer;
                                   }),
          "reclaimer dropped or released CPU state on the producer thread");
  require(!blocked.tryReclaim([&]() noexcept {
    ++failedFactories;
    return std::move(*retained);
  }) && retained->armed &&
              failedFactories == 0U && blocked.metrics().closed == 1U,
          "closed reclaimer consumed a retained caller artifact");
  retained->armed = false;
  retained.reset();

  constexpr std::size_t producerCount = 4U;
  constexpr std::size_t artifactsPerProducer = 32U;
  auto concurrentState = std::make_shared<ReclaimerProbeState>();
  concurrentState->releaseThreads.reserve(producerCount * artifactsPerProducer);
  BoundedArtifactReclaimer<ReclaimerProbe, 8U> concurrent;
  std::array<std::thread::id, producerCount> producerThreads{};
  std::vector<std::thread> producers;
  producers.reserve(producerCount);
  for (std::size_t producer = 0; producer < producerCount; ++producer) {
    producers.emplace_back([&, producer] {
      producerThreads[producer] = std::this_thread::get_id();
      for (std::size_t artifact = 0; artifact < artifactsPerProducer;
           ++artifact)
        while (!concurrent.tryReclaim(
            [&]() noexcept { return ReclaimerProbe{concurrentState}; }))
          std::this_thread::yield();
    });
  }
  for (std::thread &producer : producers)
    producer.join();
  concurrent.shutdown();
  const ArtifactReclaimerMetrics concurrentMetrics = concurrent.metrics();
  require(
      concurrentMetrics.accepted == producerCount * artifactsPerProducer &&
          concurrentMetrics.released == concurrentMetrics.accepted &&
          concurrentMetrics.outstanding == 0U &&
          concurrentMetrics.maximumQueued <= 8U &&
          concurrentState->releaseThreads.size() ==
              producerCount * artifactsPerProducer &&
          std::ranges::all_of(concurrentState->releaseThreads,
                              [&](std::thread::id released) {
                                return std::ranges::none_of(
                                    producerThreads,
                                    [&](std::thread::id producer) {
                                      return released == producer;
                                    });
                              }),
      "multi-producer reclamation was not bounded, exact, and off-producer");

  testkit::PropertyProfile generated = profile;
  generated.iterations = static_cast<std::uint64_t>(
      std::ceil(std::cbrt(static_cast<double>(profile.iterations))));
  testkit::checkProperty(
      "bounded artifact reclamation shutdown", generated,
      [](testkit::Random &random, std::uint64_t) {
        auto state = std::make_shared<ReclaimerProbeState>();
        state->producer = std::this_thread::get_id();
        BoundedArtifactReclaimer<ReclaimerProbe, 8U> reclaimer;
        const std::size_t count =
            static_cast<std::size_t>(random.next() % 8U + 1U);
        for (std::size_t index = 0; index < count; ++index)
          while (!reclaimer.tryReclaim(
              [&]() noexcept { return ReclaimerProbe{state}; }))
            std::this_thread::yield();
        reclaimer.shutdown();
        const ArtifactReclaimerMetrics metrics = reclaimer.metrics();
        require(metrics.accepted == count && metrics.released == count &&
                    metrics.outstanding == 0U && metrics.maximumQueued <= 8U &&
                    state->releaseThreads.size() == count &&
                    std::ranges::none_of(state->releaseThreads,
                                         [&](std::thread::id thread) {
                                           return thread == state->producer;
                                         }),
                "generated reclaimer shutdown lost or mis-threaded state");
      });
}

void verifyTransforms(const testkit::PropertyProfile &profile) {
  testkit::checkProperty(
      "sketch camera transform round trip", profile,
      [](testkit::Random &random, std::uint64_t index) {
        const QSizeF viewport{random.between(1.0, 8'000.0),
                              random.between(1.0, 8'000.0)};
        const double scale = std::pow(10.0, random.between(-7.0, -1.0));
        const SketchCamera2d generated{
            index + 1U,
            {random.between(-1'000.0, 1'000.0),
             random.between(-1'000.0, 1'000.0)},
            scale,
            random.between(-std::numbers::pi, std::numbers::pi)};
        auto transform = SketchViewTransform::create(generated, viewport);
        require(transform.has_value(), "valid sketch transform was rejected");
        const Point2d canonical{generated.centerMetres.x +
                                    random.between(-viewport.width() * 0.45,
                                                   viewport.width() * 0.45) *
                                        scale,
                                generated.centerMetres.y +
                                    random.between(-viewport.height() * 0.45,
                                                   viewport.height() * 0.45) *
                                        scale};
        const QPointF item = transform->toItem(canonical);
        const Point2d recovered = transform->toCanonical(item);
        const double tolerance = 1.0e-12 * std::max({1.0, std::abs(canonical.x),
                                                     std::abs(canonical.y)});
        require(std::abs(recovered.x - canonical.x) <= tolerance &&
                    std::abs(recovered.y - canonical.y) <= tolerance,
                "sketch transform did not round trip canonical metres");
        const QPointF center = transform->toItem(generated.centerMetres);
        require(center ==
                    QPointF(viewport.width() * 0.5, viewport.height() * 0.5),
                "sketch camera center is not the viewport center");
        const Point2d origin = canonical;
        auto matrix = transform->itemMatrix(origin);
        require(matrix.has_value(), "finite scene-local matrix was rejected");
        const QPointF matrixMapped = matrix->map(QPointF{});
        require(std::hypot(matrixMapped.x() - item.x(),
                           matrixMapped.y() - item.y()) <= 0.01,
                "scene-local GPU matrix disagrees with canonical transform");
      });

  SketchCamera2d invalid;
  invalid.metresPerLogicalPixel = 0.0;
  require(!SketchViewTransform::create(invalid, {100.0, 100.0}) &&
              !SketchViewTransform::create({}, {0.0, 100.0}),
          "invalid sketch camera or viewport was accepted");
  auto oversized = SketchViewTransform::create(
      {}, {std::numeric_limits<double>::max(), 100.0});
  require(!oversized && oversized.error().code ==
                            "desktop.sketch.unrepresentable-viewport",
          "viewport exceeding GPU float range was accepted");
}

void verifyCameraState(const testkit::PropertyProfile &profile) {
  SketchScenePresenter presenter;
  std::uint64_t highest = 1;
  testkit::checkProperty(
      "sketch camera latest-wins state", profile,
      [&](testkit::Random &random, std::uint64_t index) {
        const std::uint64_t generation = random.next() % (index + 32U) + 1U;
        auto offered = presenter.publishCamera(camera(generation));
        require(offered.has_value(), "valid generated camera was rejected");
        const SketchCameraDecision expected =
            generation > highest    ? SketchCameraDecision::Accepted
            : generation == highest ? SketchCameraDecision::Duplicate
                                    : SketchCameraDecision::StaleGeneration;
        require(*offered == expected,
                "camera publication disagrees with latest-wins model");
        highest = std::max(highest, generation);
        if (index % 97U == 0U) {
          SketchCamera2d conflict = camera(highest);
          conflict.rotationRadians += 0.1;
          auto rejected = presenter.publishCamera(conflict);
          require(rejected &&
                      *rejected == SketchCameraDecision::GenerationConflict,
                  "same-generation camera conflict was accepted");
        }
      });
}

void verifyPickCoverageState(const testkit::PropertyProfile &profile) {
  SketchScenePresenter presenter;
  std::uint64_t highest = 1U;
  double acceptedMaximum =
      SketchPickCoveragePolicy::defaultMaximumToleranceLogicalPixels;
  testkit::checkProperty(
      "sketch pick coverage latest-wins state", profile,
      [&](testkit::Random &random, std::uint64_t index) {
        const std::uint64_t generation = random.next() % (index + 32U) + 1U;
        const double maximum =
            generation == highest
                ? acceptedMaximum
                : random.between(0.0,
                                 SketchPickCoveragePolicy::
                                     maximumConfigurableToleranceLogicalPixels);
        auto offered = presenter.publishPickCoverage({generation, maximum});
        require(offered.has_value(),
                "valid generated pick coverage was rejected");
        const SketchPickCoverageDecision expected =
            generation > highest ? SketchPickCoverageDecision::Accepted
            : generation == highest
                ? SketchPickCoverageDecision::Duplicate
                : SketchPickCoverageDecision::StaleGeneration;
        require(*offered == expected,
                "pick coverage publication disagrees with latest-wins model");
        if (generation > highest) {
          highest = generation;
          acceptedMaximum = maximum;
        }
      });

  auto conflict = presenter.publishPickCoverage(
      {highest, acceptedMaximum == 0.0 ? 1.0 : acceptedMaximum * 0.5});
  require(conflict &&
              *conflict == SketchPickCoverageDecision::GenerationConflict,
          "same-generation pick coverage conflict was accepted");
  for (const SketchPickCoveragePolicy invalid :
       {SketchPickCoveragePolicy{0U, 1.0},
        SketchPickCoveragePolicy{highest + 1U, -1.0},
        SketchPickCoveragePolicy{
            highest + 1U, SketchPickCoveragePolicy::
                                  maximumConfigurableToleranceLogicalPixels +
                              1.0},
        SketchPickCoveragePolicy{highest + 1U,
                                 std::numeric_limits<double>::quiet_NaN()}})
    require(!presenter.publishPickCoverage(invalid),
            "invalid pick coverage policy was accepted");

  SketchPickCoveragePolicy ceiling;
  ceiling.generation = highest + 1U;
  ceiling.maximumRenderedTriangleTests =
      SketchPickCoveragePolicy::maximumConfigurableRenderedTriangleTests;
  ceiling.maximumRenderedSpanProbes =
      SketchPickCoveragePolicy::maximumConfigurableRenderedSpanProbes;
  ceiling.maximumPatternIntervals =
      SketchPickCoveragePolicy::maximumConfigurablePatternIntervals;
  require(presenter.publishPickCoverage(ceiling).has_value(),
          "bounded pick work ceilings were rejected");
  const auto rejectExcessive = [&](SketchPickCoveragePolicy policy) {
    ++policy.generation;
    require(!presenter.publishPickCoverage(policy),
            "unbounded rendered pick work policy was accepted");
  };
  auto excessive = ceiling;
  excessive.maximumRenderedTriangleTests =
      SketchPickCoveragePolicy::maximumConfigurableRenderedTriangleTests + 1U;
  rejectExcessive(excessive);
  excessive = ceiling;
  excessive.maximumRenderedSpanProbes =
      SketchPickCoveragePolicy::maximumConfigurableRenderedSpanProbes + 1U;
  rejectExcessive(excessive);
  excessive = ceiling;
  excessive.maximumPatternIntervals =
      SketchPickCoveragePolicy::maximumConfigurablePatternIntervals + 1U;
  rejectExcessive(excessive);
}

void verifyPublication(const testkit::PropertyProfile &profile) {
  const SceneTarget target = stamp(1, 1, 1, 1, 1, 1).target;
  SketchScenePresenter presenter;
  presenter.retarget(target);
  std::uint64_t highest = 0;
  std::shared_ptr<const PreparedSketchProducts> highestPacket;
  testkit::checkProperty(
      "sketch scene publication state", profile,
      [&](testkit::Random &random, std::uint64_t index) {
        const std::uint64_t generation = random.next() % (index + 32U) + 1U;
        auto generated =
            scene(static_cast<std::size_t>(generation % 8U + 1U), generation,
                  stamp(1, generation, 1, 1, 1, generation));
        auto packet = generation == highest
                          ? highestPacket
                          : preparedProductPacket(preparedScene(
                                generated, presenter.requestedLod()));
        auto offered = presenter.publish(packet);
        require(offered.has_value(), "valid generated scene was rejected");
        const PreparedSketchSceneDecision expected =
            generation > highest ? PreparedSketchSceneDecision::Accepted
            : generation == highest
                ? PreparedSketchSceneDecision::Duplicate
                : PreparedSketchSceneDecision::StaleGeneration;
        require(offered->decision == expected,
                "scene publication disagrees with latest-wins model");
        require(presenter.pendingCount() <= 1U,
                "scene publication exceeded its bounded pending slot");
        if (generation > highest) {
          highest = generation;
          highestPacket = std::move(packet);
        }
        if (index % 31U == 0U) {
          auto synchronized = presenter.synchronize({1200.0, 800.0});
          require(synchronized &&
                      (*synchronized)->scene()->stamp().generation.value() ==
                          highest,
                  "synchronization installed a stale scene generation");
        }
      });
}

std::shared_ptr<const SketchSceneSnapshot>
reorderedScene(std::shared_ptr<const SketchSceneSnapshot> source,
               SceneStamp nextStamp, std::uint64_t seed) {
  std::vector<std::size_t> order(source->primitives().size());
  std::iota(order.begin(), order.end(), 0U);
  testkit::Random random{seed};
  for (std::size_t end = order.size(); end > 1U; --end)
    std::swap(order[end - 1U],
              order[static_cast<std::size_t>(random.next() % end)]);
  std::vector<Point2d> points;
  std::vector<PackedSketchPrimitive> primitives;
  points.reserve(source->points().size());
  primitives.reserve(source->primitives().size());
  for (const std::size_t index : order) {
    PackedSketchPrimitive primitive = source->primitives()[index];
    const std::size_t pointCount =
        primitive.kind == SketchPrimitiveKind::Line ? 2U : 1U;
    const auto sourcePoints =
        source->points().subspan(primitive.firstPoint, pointCount);
    primitive.firstPoint = static_cast<std::uint32_t>(points.size());
    points.insert(points.end(), sourcePoints.begin(), sourcePoints.end());
    primitives.push_back(primitive);
  }
  auto rebuilt = SketchSceneSnapshot::create(
      std::move(nextStamp), styles(), std::move(points), std::move(primitives));
  require(rebuilt.has_value(), "shuffled sketch scene was invalid");
  return std::make_shared<const SketchSceneSnapshot>(std::move(*rebuilt));
}

void requireValidPrimitiveTessellationIndex(
    const SketchSceneSnapshot &scene, const SketchSceneMesh &mesh,
    const SketchPrimitiveTessellationIndex &index);

SketchChunkBounds visibleBounds(const SketchSceneMesh &mesh,
                                const SketchViewTransform &transform,
                                SketchPickCoveragePolicy pickCoverage = {}) {
  SketchChunkBounds bounds;
  const QSizeF viewport = transform.viewportLogical();
  for (const QPointF item :
       std::array{QPointF{0.0, 0.0}, QPointF{viewport.width(), 0.0},
                  QPointF{0.0, viewport.height()},
                  QPointF{viewport.width(), viewport.height()}}) {
    const Point2d canonical = transform.toCanonical(item);
    const double x = canonical.x - mesh.originMetres().x;
    const double y = canonical.y - mesh.originMetres().y;
    if (bounds.empty) {
      bounds = {x, y, x, y, false};
    } else {
      bounds.minimumX = std::min(bounds.minimumX, x);
      bounds.minimumY = std::min(bounds.minimumY, y);
      bounds.maximumX = std::max(bounds.maximumX, x);
      bounds.maximumY = std::max(bounds.maximumY, y);
    }
  }
  const double margin = pickCoverage.maximumToleranceLogicalPixels *
                        transform.camera().metresPerLogicalPixel;
  bounds.minimumX -= margin;
  bounds.minimumY -= margin;
  bounds.maximumX += margin;
  bounds.maximumY += margin;
  return bounds;
}

void verifySpatialChunksAndProgression() {
  const SceneStamp initialStamp = stamp(13, 1, 13, 13, 13, 1);
  auto generated = scene(8'000, 91, initialStamp);
  generated = reorderedScene(generated, initialStamp, 92);
  constexpr double metresPerPixel = 0.0001;
  const SketchCurveLod lod =
      SketchCurveLod::forMetresPerLogicalPixel(metresPerPixel);
  SketchUploadOptions upload;
  upload.maximumChunkBytes = 32U * 1024U;
  auto prepared = prepareSketchScene(generated, lod, {}, {}, upload);
  require(prepared.has_value(),
          "spatially interleaved scene preparation failed");
  requireValidPrimitiveTessellationIndex(
      *generated, *(*prepared)->mesh(),
      *(*prepared)->primitiveTessellationIndex());
  auto transform = SketchViewTransform::create({2, {}, metresPerPixel, 0.37},
                                               {1000.0, 700.0});
  require(transform.has_value(), "spatial selection transform failed");
  auto selected = (*prepared)->mesh()->visibleChunks(*transform);
  require(selected.has_value() && !selected->chunks.empty(),
          "spatial index selected no visible chunks");
  std::vector<std::uint32_t> brute;
  const SketchChunkBounds viewport =
      visibleBounds(*(*prepared)->mesh(), *transform);
  const auto chunks = (*prepared)->mesh()->chunks();
  for (std::uint32_t index = 0; index < chunks.size(); ++index)
    if (chunks[index]->bounds().intersects(
            viewport, chunks[index]->bounds().maximumExtrusionLogicalPixels *
                          transform->camera().metresPerLogicalPixel))
      brute.push_back(index);
  require(selected->chunks == brute,
          "spatial index disagrees with exhaustive visible selection");
  require(chunks.size() > 256U &&
              selected->chunks.size() >
                  SketchGpuUploadPolicy::maximumChunksPerFrame &&
              selected->spatialNodesVisited < chunks.size() / 2U &&
              selected->chunks.size() < chunks.size() / 4U,
          "shuffled scene visibility did not remain sublinear");

  auto progressiveVisibility =
      ProgressiveSketchVisibility::create((*prepared)->mesh(), *transform);
  require(progressiveVisibility.has_value(),
          "progressive spatial selection failed");
  std::vector<std::uint32_t> progressivelySelected;
  while (!progressiveVisibility->complete()) {
    auto slice = progressiveVisibility->takeNextSlice(17U, 5U);
    require(slice && slice->spatialNodesVisited <= 17U &&
                slice->chunks.size() <= 5U && slice->spatialNodesVisited > 0U,
            "progressive visibility exceeded its metadata slice");
    progressivelySelected.insert(progressivelySelected.end(),
                                 slice->chunks.begin(), slice->chunks.end());
  }
  std::ranges::sort(progressivelySelected);
  require(progressivelySelected == selected->chunks,
          "progressive visibility did not converge exactly");

  auto allVisibleTransform =
      SketchViewTransform::create({3, {}, 1.0, -0.71}, {10000.0, 10000.0});
  require(allVisibleTransform.has_value(), "all-visible camera failed");
  auto allVisible = ProgressiveSketchVisibility::create((*prepared)->mesh(),
                                                        *allVisibleTransform);
  require(allVisible.has_value(), "all-visible progression failed");
  std::vector<std::uint32_t> allVisibleChunks;
  while (!allVisible->complete()) {
    auto slice = allVisible->takeNextSlice(31U, 7U);
    require(slice && slice->spatialNodesVisited <= 31U &&
                slice->chunks.size() <= 7U && slice->spatialNodesVisited > 0U,
            "all-visible progression exceeded its metadata ceiling");
    allVisibleChunks.insert(allVisibleChunks.end(), slice->chunks.begin(),
                            slice->chunks.end());
  }
  std::ranges::sort(allVisibleChunks);
  require(allVisibleChunks.size() == chunks.size() &&
              std::ranges::equal(
                  allVisibleChunks,
                  std::views::iota(std::uint32_t{0},
                                   static_cast<std::uint32_t>(chunks.size()))),
          "all-visible progression omitted or duplicated a chunk");

  auto progressive = ProgressiveSketchUpload::create(
      *prepared, selected->chunks,
      std::span<const std::shared_ptr<const SketchUploadChunk>>{});
  require(progressive.has_value(), "progressive upload schedule failed");
  std::vector<std::uint32_t> uploaded;
  while (!progressive->complete()) {
    auto slice = progressive->takeNextSlice(64U * 1024U, 3U);
    require(slice && !slice->entries.empty() && slice->bytes <= 64U * 1024U &&
                slice->entries.size() <= 3U,
            "progressive upload exceeded its byte or node slice bound");
    for (const SketchUploadSliceEntry entry : slice->entries)
      uploaded.push_back(entry.chunk);
  }
  require(uploaded == selected->chunks,
          "progressive upload did not converge in deterministic chunk order");
  const SketchChunkSequence sealedSelection =
      progressive->releaseRequiredChunks();
  bool exactSealedSelection = sealedSelection.size() == selected->chunks.size();
  for (std::size_t index = 0;
       exactSealedSelection && index < sealedSelection.size(); ++index)
    exactSealedSelection = sealedSelection[index] == selected->chunks[index];
  require(exactSealedSelection,
          "completed upload released a missing or reordered resident set");
  auto duplicated = ProgressiveSketchUpload::create(
      *prepared,
      std::vector<std::uint32_t>{selected->chunks.front(),
                                 selected->chunks.front()},
      std::span<const std::shared_ptr<const SketchUploadChunk>>{});
  require(!duplicated && duplicated.error().code ==
                             "desktop.sketch.invalid-visible-chunks",
          "duplicate external visibility metadata entered staging");

  std::vector<std::shared_ptr<const SketchUploadChunk>> resident(chunks.begin(),
                                                                 chunks.end());
  auto reusedSchedule =
      ProgressiveSketchUpload::create(*prepared, selected->chunks, resident);
  require(reusedSchedule.has_value(), "resident reuse schedule failed");
  std::size_t reusedScheduled = 0;
  while (!reusedSchedule->complete()) {
    auto slice = reusedSchedule->takeNextSlice(64U * 1024U, 3U);
    require(slice && !slice->entries.empty() && slice->bytes == 0U &&
                slice->entries.size() <= 3U &&
                std::ranges::all_of(slice->entries,
                                    [](const auto &entry) {
                                      return entry.reuseResidentGeometry;
                                    }),
            "resident reuse bypassed the strict node slice ceiling");
    reusedScheduled += slice->entries.size();
  }
  require(reusedScheduled == selected->chunks.size(),
          "resident reuse schedule did not converge exactly");

  std::vector<Point2d> editedPoints(generated->points().begin(),
                                    generated->points().end());
  editedPoints.front().x -= 2.0;
  std::vector<PackedSketchPrimitive> editedPrimitives(
      generated->primitives().begin(), generated->primitives().end());
  auto editedScene = SketchSceneSnapshot::create(
      stamp(13, 2, 13, 13, 13, 2), styles(), std::move(editedPoints),
      std::move(editedPrimitives));
  require(editedScene.has_value(), "local extremal edit scene failed");
  auto editedSnapshot =
      std::make_shared<const SketchSceneSnapshot>(std::move(*editedScene));
  auto edited =
      prepareSketchScene(editedSnapshot, lod, {}, {}, upload, *prepared);
  require(edited && (*edited)->mesh()->originMetres() ==
                        (*prepared)->mesh()->originMetres(),
          "compatible extremal edit changed the stable floating origin");
  requireValidPrimitiveTessellationIndex(
      *editedSnapshot, *(*edited)->mesh(),
      *(*edited)->primitiveTessellationIndex());
  std::unordered_set<const SketchUploadChunk *> original;
  for (const auto &chunk : chunks)
    original.insert(chunk.get());
  std::size_t reused = 0;
  for (const auto &chunk : (*edited)->mesh()->chunks())
    reused += original.contains(chunk.get()) ? 1U : 0U;
  require(reused > chunks.size() * 9U / 10U,
          "local edit invalidated unrelated spatial chunk identities");
}

void verifyGeneratedSpatialChunkProperties(
    const testkit::PropertyProfile &profile) {
  testkit::PropertyProfile spatialProfile = profile;
  spatialProfile.iterations = static_cast<std::uint64_t>(
      std::ceil(std::cbrt(static_cast<double>(profile.iterations))));
  testkit::checkProperty(
      "shuffled spatial chunks, culling, reuse, and slices", spatialProfile,
      [](testkit::Random &random, std::uint64_t index) {
        const std::size_t count =
            static_cast<std::size_t>(random.next() % 257U + 256U);
        const SceneStamp firstStamp =
            stamp(20 + index, 1, 20 + index, 20 + index, 20 + index, 1);
        auto generated = reorderedScene(scene(count, random.next(), firstStamp),
                                        firstStamp, random.next());
        const double metresPerPixel = random.between(0.00001, 0.00003);
        const SketchCurveLod lod =
            SketchCurveLod::forMetresPerLogicalPixel(metresPerPixel);
        SketchUploadOptions upload;
        upload.maximumChunkBytes = static_cast<std::size_t>(
            random.next() % (28U * 1024U) + 4U * 1024U);
        upload.spatialTileLogicalPixels = random.between(64.0, 256.0);
        auto prepared = prepareSketchScene(generated, lod, {}, {}, upload);
        require(prepared.has_value(),
                "generated shuffled preparation was rejected");
        requireValidPrimitiveTessellationIndex(
            *generated, *(*prepared)->mesh(),
            *(*prepared)->primitiveTessellationIndex());
        const QSizeF viewport{random.between(320.0, 800.0),
                              random.between(240.0, 600.0)};
        auto transform = SketchViewTransform::create(
            {2,
             {random.between(-0.05, 0.05), random.between(-0.05, 0.05)},
             metresPerPixel,
             random.between(-std::numbers::pi, std::numbers::pi)},
            viewport);
        require(transform.has_value(), "generated spatial camera failed");
        const SketchPickCoveragePolicy pickCoverage{
            index + 1U,
            random.between(0.0, SketchPickCoveragePolicy::
                                    maximumConfigurableToleranceLogicalPixels)};
        auto selected =
            (*prepared)->mesh()->visibleChunks(*transform, pickCoverage);
        require(selected.has_value(), "generated spatial query failed");
        auto zeroCoverage =
            (*prepared)->mesh()->visibleChunks(*transform, {index + 1U, 0.0});
        auto maximumCoverage = (*prepared)->mesh()->visibleChunks(
            *transform,
            {index + 1U, SketchPickCoveragePolicy::
                             maximumConfigurableToleranceLogicalPixels});
        require(zeroCoverage && maximumCoverage &&
                    std::ranges::includes(maximumCoverage->chunks,
                                          zeroCoverage->chunks),
                "bounded pick coverage was not monotonic");
        const SketchChunkBounds viewportBoundsValue =
            visibleBounds(*(*prepared)->mesh(), *transform, pickCoverage);
        std::vector<std::uint32_t> brute;
        const auto chunks = (*prepared)->mesh()->chunks();
        for (std::uint32_t chunk = 0; chunk < chunks.size(); ++chunk)
          if (chunks[chunk]->bounds().intersects(
                  viewportBoundsValue,
                  chunks[chunk]->bounds().maximumExtrusionLogicalPixels *
                      transform->camera().metresPerLogicalPixel))
            brute.push_back(chunk);
        require(selected->chunks == brute &&
                    selected->spatialNodesVisited < chunks.size(),
                "generated spatial query scanned broadly or missed a chunk");

        const std::size_t visibilityNodeLimit =
            static_cast<std::size_t>(random.next() % 19U + 1U);
        const std::size_t visibilityChunkLimit =
            static_cast<std::size_t>(random.next() % 11U + 1U);
        auto progressiveVisibility = ProgressiveSketchVisibility::create(
            (*prepared)->mesh(), *transform, pickCoverage);
        require(progressiveVisibility.has_value(),
                "generated progressive visibility failed");
        std::vector<std::uint32_t> progressivelySelected;
        while (!progressiveVisibility->complete()) {
          auto slice = progressiveVisibility->takeNextSlice(
              visibilityNodeLimit, visibilityChunkLimit);
          require(slice && slice->spatialNodesVisited <= visibilityNodeLimit &&
                      slice->chunks.size() <= visibilityChunkLimit &&
                      slice->spatialNodesVisited > 0U,
                  "generated visibility slice exceeded its ceiling");
          progressivelySelected.insert(progressivelySelected.end(),
                                       slice->chunks.begin(),
                                       slice->chunks.end());
        }
        std::ranges::sort(progressivelySelected);
        require(progressivelySelected == selected->chunks,
                "generated progressive visibility did not converge exactly");

        require(
            !(*prepared)->mesh()->visibleChunks(
                *transform, {0U, pickCoverage.maximumToleranceLogicalPixels}) &&
                !ProgressiveSketchVisibility::create(
                    (*prepared)->mesh(), *transform,
                    {index + 1U,
                     SketchPickCoveragePolicy::
                             maximumConfigurableToleranceLogicalPixels +
                         1.0}),
            "invalid pick coverage entered visibility selection");

        const std::size_t nodeLimit =
            static_cast<std::size_t>(random.next() % 8U + 1U);
        auto progressive = ProgressiveSketchUpload::create(
            *prepared, selected->chunks,
            std::span<const std::shared_ptr<const SketchUploadChunk>>{});
        require(progressive.has_value(),
                "generated progressive schedule failed");
        std::vector<std::uint32_t> uploaded;
        while (!progressive->complete()) {
          auto slice =
              progressive->takeNextSlice(upload.maximumChunkBytes, nodeLimit);
          require(slice && !slice->entries.empty() &&
                      slice->bytes <= upload.maximumChunkBytes &&
                      slice->entries.size() <= nodeLimit,
                  "generated slice exceeded a resource ceiling");
          for (const SketchUploadSliceEntry entry : slice->entries)
            uploaded.push_back(entry.chunk);
        }
        require(uploaded == selected->chunks,
                "generated progressive schedule did not converge exactly");

        std::vector<Point2d> points(generated->points().begin(),
                                    generated->points().end());
        points.front().x -= random.between(0.5, 2.0);
        std::vector<PackedSketchPrimitive> primitives(
            generated->primitives().begin(), generated->primitives().end());
        auto editedScene = SketchSceneSnapshot::create(
            stamp(20 + index, 2, 20 + index, 20 + index, 20 + index, 2),
            styles(), std::move(points), std::move(primitives));
        require(editedScene.has_value(), "generated localized edit failed");
        auto editedSnapshot = std::make_shared<const SketchSceneSnapshot>(
            std::move(*editedScene));
        auto edited =
            prepareSketchScene(editedSnapshot, lod, {}, {}, upload, *prepared);
        require(edited && (*edited)->mesh()->originMetres() ==
                              (*prepared)->mesh()->originMetres(),
                "generated localized edit changed its compatible origin");
        requireValidPrimitiveTessellationIndex(
            *editedSnapshot, *(*edited)->mesh(),
            *(*edited)->primitiveTessellationIndex());
        std::unordered_set<const SketchUploadChunk *> identities;
        for (const auto &chunk : chunks)
          identities.insert(chunk.get());
        std::size_t reused = 0;
        for (const auto &chunk : (*edited)->mesh()->chunks())
          reused += identities.contains(chunk.get()) ? 1U : 0U;
        require(reused > chunks.size() * 4U / 5U,
                "generated localized edit cascaded across spatial chunks");
      });
}

void verifyBoundedRetargeting() {
  const SceneStamp firstStamp = stamp(11, 1, 11, 11, 11, 1);
  SketchScenePresenter presenter;
  presenter.retarget(firstStamp.target);
  auto firstBase =
      preparedScene(scene(8, 1, firstStamp), presenter.requestedLod());
  auto secondBase = preparedScene(scene(9, 2, stamp(11, 2, 11, 11, 11, 2)),
                                  presenter.requestedLod());
  auto latestBase = preparedScene(scene(10, 3, stamp(11, 3, 11, 11, 11, 3)),
                                  presenter.requestedLod());
  auto first = preparedProductPacket(firstBase);
  auto second = preparedProductPacket(secondBase);
  auto latest = preparedProductPacket(latestBase);
  require(presenter.publish(first) ==
                  PreparedSketchSceneOffer{
                      PreparedSketchSceneDecision::Accepted, false} &&
              presenter.publish(second) ==
                  PreparedSketchSceneOffer{
                      PreparedSketchSceneDecision::Accepted, true} &&
              presenter.publish(latest) ==
                  PreparedSketchSceneOffer{
                      PreparedSketchSceneDecision::Accepted, true} &&
              presenter.pendingCount() == 1U,
          "prepared publication did not keep one latest pending packet");
  auto frame = presenter.synchronize({1200.0, 800.0});
  require(frame && (*frame)->prepared() == latestBase &&
              presenter.pendingCount() == 0U,
          "synchronization did not install the exact latest packet");

  auto pendingBase = preparedScene(scene(11, 4, stamp(11, 4, 11, 11, 11, 4)),
                                   presenter.requestedLod());
  auto pending = preparedProductPacket(pendingBase);
  require(presenter.publish(pending) ==
              PreparedSketchSceneOffer{PreparedSketchSceneDecision::Accepted,
                                       false},
          "same-target newer packet was not accepted for staging");
  auto pendingEvidence = presenter.pick(*frame, {600.0, 400.0}, 4.0);
  require(pendingEvidence && pendingEvidence->scene == latestBase->stamp() &&
              pendingEvidence->latestAcceptedScene == pendingBase->stamp() &&
              !pendingEvidence->matchesLatestAcceptedScene,
          "visible old frame became editable before same-target swap");

  const SceneStamp nextStamp = stamp(12, 1, 12, 12, 12, 1);
  presenter.retarget(nextStamp.target);
  auto retained = presenter.synchronize({1200.0, 800.0});
  require(retained && *retained == *frame,
          "retargeting discarded the last valid prepared frame");
  auto evidence = presenter.pick({600.0, 400.0}, 4.0);
  require(evidence && !evidence->matchesLatestAcceptedScene,
          "retained frame was reported as the new desired target");
  require(presenter.publish(latest) ==
              PreparedSketchSceneOffer{PreparedSketchSceneDecision::StaleTarget,
                                       false},
          "old-target prepared packet was accepted after retargeting");
  auto nextBase =
      preparedScene(scene(7, 4, nextStamp), presenter.requestedLod());
  auto next = preparedProductPacket(nextBase);
  require(presenter.publish(next) ==
              PreparedSketchSceneOffer{PreparedSketchSceneDecision::Accepted,
                                       false},
          "new-target prepared packet was rejected after retargeting");
  frame = presenter.synchronize({1200.0, 800.0});
  require(frame && (*frame)->prepared() == nextBase &&
              presenter.synchronizationMetrics().scalablePreparations == 0U,
          "new-target synchronization did not install the prepared packet");
}

void verifyResizeAndPicking(const testkit::PropertyProfile &profile) {
  testkit::checkProperty(
      "sketch resize and semantic picking", profile,
      [](testkit::Random &random, std::uint64_t index) {
        const std::size_t count =
            static_cast<std::size_t>(random.next() % 64U + 1U);
        auto generated = scene(count, random.next(),
                               stamp(2, index + 1U, 2, 2, 2, index + 1U));
        SketchScenePresenter presenter;
        presenter.retarget(generated->stamp().target);
        SketchCamera2d view{2, {0.0, 0.0}, 0.0002, random.between(-1.0, 1.0)};
        require(presenter.publishCamera(view) == SketchCameraDecision::Accepted,
                "valid camera publication failed");
        require(presenter
                    .publish(preparedProductPacket(
                        preparedScene(generated, presenter.requestedLod())))
                    .has_value(),
                "valid scene publication failed");
        const QSizeF firstSize{random.between(320.0, 2400.0),
                               random.between(240.0, 1600.0)};
        auto first = presenter.synchronize(firstSize);
        require(first.has_value(), "valid scene synchronization failed");
        auto repeated = presenter.synchronize(firstSize);
        require(repeated && *repeated == *first,
                "unchanged synchronization rebuilt its frame");

        const QSizeF secondSize{firstSize.width() + 37.0,
                                firstSize.height() + 19.0};
        auto resized = presenter.synchronize(secondSize);
        require(resized && *resized != *first &&
                    (*resized)->pickIndex() == (*first)->pickIndex(),
                "resize rebuilt scene-only picking state");
        const QPointF center =
            (*resized)->transform().toItem(view.centerMetres);
        require(center == QPointF(secondSize.width() * 0.5,
                                  secondSize.height() * 0.5),
                "resize did not recenter the deterministic transform");

        const Point2d query{random.between(-0.05, 0.05),
                            random.between(-0.05, 0.05)};
        constexpr double toleranceMetres = 0.003;
        auto canonicalIndex = SketchPickIndex::build(generated);
        require(canonicalIndex.has_value(), "canonical pick index failed");
        auto expected = canonicalIndex->pick(
            {query, toleranceMetres, SketchPickTargets::All});
        require(expected.has_value(), "canonical pick query failed");
        const QPointF item = (*resized)->transform().toItem(query);
        auto actual =
            presenter.pick(item, toleranceMetres / view.metresPerLogicalPixel,
                           SketchPickTargets::All);
        require(actual && actual->scene == generated->stamp() &&
                    actual->cameraGeneration == view.generation &&
                    actual->viewportLogical == secondSize &&
                    actual->pickCoverage == SketchPickCoveragePolicy{} &&
                    actual->matchesLatestAcceptedScene,
                "item pick omitted revision/evaluation/camera evidence");
        requireSamePick(actual->hit, *expected);
        auto maximumTolerance = presenter.pick(
            center,
            SketchPickCoveragePolicy::defaultMaximumToleranceLogicalPixels,
            SketchPickTargets::All);
        auto aboveTolerance = presenter.pick(
            center,
            std::nextafter(
                SketchPickCoveragePolicy::defaultMaximumToleranceLogicalPixels,
                std::numeric_limits<double>::infinity()),
            SketchPickTargets::All);
        require(maximumTolerance && !aboveTolerance &&
                    aboveTolerance.error().code ==
                        "desktop.sketch.invalid-pick",
                "synchronized pick tolerance bound was not exact");
      });
}

void requireValidPrimitiveTessellationIndex(
    const SketchSceneSnapshot &scene, const SketchSceneMesh &mesh,
    const SketchPrimitiveTessellationIndex &index) {
  const auto entries = index.entries();
  const auto spans = index.spans();
  const auto chunks = mesh.chunks();
  std::size_t visiblePrimitives = 0U;
  std::unordered_map<std::uint32_t, const PackedSketchPrimitive *> primitives;
  primitives.reserve(scene.primitives().size());
  for (const PackedSketchPrimitive &primitive : scene.primitives()) {
    const bool inserted =
        primitives.emplace(primitive.handle.value(), &primitive).second;
    require(inserted, "validated scene contained duplicate primitive handles");
    if (hasFlag(primitive.flags, SketchPrimitiveFlags::Visible)) {
      ++visiblePrimitives;
      require(index.find(primitive.handle) != nullptr,
              "visible primitive has no tessellated chunk ranges");
    } else {
      require(index.find(primitive.handle) == nullptr,
              "hidden primitive entered the tessellation index");
    }
  }
  require(entries.size() == visiblePrimitives,
          "primitive index entry count is inconsistent");

  std::vector<std::pair<std::uint32_t, std::uint32_t>> reconstructed;
  reconstructed.reserve(mesh.metrics().indices / 3U);
  const std::size_t expectedIndexBytes =
      sizeof(SketchPrimitiveTessellationIndex) +
      entries.size() * sizeof(SketchPrimitiveTessellationEntry) +
      spans.size() * sizeof(SketchPrimitiveChunkSpan);
  require(index.retainedBytes() >= expectedIndexBytes,
          "primitive index retained-byte count omitted packed payload");
  for (std::size_t ordinal = 0U; ordinal < entries.size(); ++ordinal) {
    const SketchPrimitiveTessellationEntry &entry = entries[ordinal];
    require(ordinal == 0U || entries[ordinal - 1U].primitive < entry.primitive,
            "primitive tessellation entries are not strictly ordered");
    const auto source = primitives.find(entry.primitive.value());
    require(source != primitives.end() &&
                hasFlag(source->second->flags, SketchPrimitiveFlags::Visible) &&
                index.find(entry.primitive) == &entry,
            "primitive tessellation lookup returned the wrong entry");
    const auto primitiveSpans = index.spans(entry.primitive);
    require(!primitiveSpans.empty() && primitiveSpans.size() == entry.spanCount,
            "primitive tessellation entry has no exact ranges");
    std::size_t primitiveIndexCount = 0U;
    for (const SketchPrimitiveChunkSpan span : primitiveSpans) {
      require(span.chunk < chunks.size() && span.indexCount != 0U &&
                  span.firstIndex % 3U == 0U && span.indexCount % 3U == 0U &&
                  span.firstIndex <= chunks[span.chunk]->indices().size() &&
                  span.indexCount <=
                      chunks[span.chunk]->indices().size() - span.firstIndex &&
                  chunks[span.chunk]->style() == source->second->style,
              "primitive tessellation range is malformed");
      primitiveIndexCount += span.indexCount;
      for (std::uint32_t offset = 0U; offset < span.indexCount; offset += 3U) {
        const std::uint32_t first = span.firstIndex + offset;
        reconstructed.emplace_back(span.chunk, first);
        for (std::uint32_t corner = 0U; corner < 3U; ++corner)
          require(chunks[span.chunk]->indices()[first + corner] <
                      chunks[span.chunk]->vertices().size(),
                  "reconstructed primitive triangle references no vertex");
      }
    }
    require(primitiveIndexCount == entry.indexCount,
            "primitive tessellation entry index total is inconsistent");
  }

  std::ranges::sort(reconstructed);
  std::vector<std::pair<std::uint32_t, std::uint32_t>> expected;
  expected.reserve(mesh.metrics().indices / 3U);
  for (std::uint32_t chunk = 0U; chunk < chunks.size(); ++chunk)
    for (std::uint32_t first = 0U; first < chunks[chunk]->indices().size();
         first += 3U)
      expected.emplace_back(chunk, first);
  require(reconstructed == expected,
          "primitive ranges did not reconstruct the exact mesh triangles");

  measuredAllocations = 0U;
  measureAllocations = true;
  for (const SketchPrimitiveTessellationEntry &entry : entries)
    require(index.find(entry.primitive) == &entry,
            "allocation-free binary lookup changed identity");
  measureAllocations = false;
  require(measuredAllocations == 0U,
          "primitive tessellation lookup allocated with base size");
  if (entries.size() > 1U) {
    SketchPrimitiveTessellationEntry foreign = entries.front();
    foreign.firstSpan = entries.back().firstSpan;
    foreign.spanCount = entries.back().spanCount;
    foreign.indexCount = entries.back().indexCount;
    require(foreign.firstSpan != entries.front().firstSpan &&
                std::ranges::equal(index.spans(foreign.primitive),
                                   index.spans(entries.front().primitive)) &&
                index.spans(foreign.primitive).data() !=
                    index.spans(entries.back().primitive).data(),
            "foreign range metadata relabeled primitive tessellation data");
  }
}

void requireValidMesh(
    const SketchSceneSnapshot &scene, const SketchSceneMesh &mesh,
    const SketchPrimitiveTessellationIndex &tessellationIndex) {
  std::size_t vertices = 0;
  std::size_t indices = 0;
  std::uint16_t previousLayer = 0;
  bool first = true;
  for (const auto &chunk : mesh.chunks()) {
    require(first || chunk->layer() >= previousLayer,
            "mesh batches are not in deterministic layer order");
    first = false;
    previousLayer = chunk->layer();
    for (const SketchMeshVertex vertex : chunk->vertices())
      require(std::isfinite(vertex.x) && std::isfinite(vertex.y) &&
                  std::isfinite(vertex.xLow) && std::isfinite(vertex.yLow) &&
                  std::isfinite(vertex.extrusionX) &&
                  std::isfinite(vertex.extrusionY) &&
                  std::isfinite(vertex.pathDistanceMetres) &&
                  vertex.pathDistanceMetres >= 0.0F,
              "mesh contains a non-finite vertex");
    for (const std::uint32_t vertexIndex : chunk->indices())
      require(vertexIndex < chunk->vertices().size(),
              "mesh index is outside its style batch");
    require(chunk->indices().size() % 3U == 0U,
            "mesh is not composed of triangles");
    require(chunk->payloadBytes() <= mesh.maximumChunkBytes(),
            "mesh chunk exceeds its immutable payload bound");
    vertices += chunk->vertices().size();
    indices += chunk->indices().size();
  }
  require(mesh.metrics().batches == mesh.chunks().size() &&
              mesh.metrics().vertices == vertices &&
              mesh.metrics().indices == indices &&
              mesh.metrics().bytes == vertices * sizeof(SketchMeshVertex) +
                                          indices * sizeof(std::uint32_t) &&
              mesh.metrics().retainedMeshBytes >= mesh.metrics().bytes &&
              mesh.metrics().peakPreparationMeshBytes >=
                  mesh.metrics().retainedMeshBytes,
          "mesh resource accounting is inconsistent");
  requireValidPrimitiveTessellationIndex(scene, mesh, tessellationIndex);
}

std::vector<SketchMeshVertex>
primitiveTrianglePayload(const SketchSceneMesh &mesh,
                         const SketchPrimitiveTessellationIndex &index,
                         SketchPrimitiveHandle primitive) {
  const SketchPrimitiveTessellationEntry *entry = index.find(primitive);
  require(entry != nullptr, "primitive payload lookup missed visible geometry");
  std::vector<SketchMeshVertex> payload;
  payload.reserve(entry->indexCount);
  const auto chunks = mesh.chunks();
  for (const SketchPrimitiveChunkSpan span : index.spans(primitive)) {
    const auto vertices = chunks[span.chunk]->vertices();
    const auto indices = chunks[span.chunk]->indices();
    for (std::uint32_t offset = 0U; offset < span.indexCount; ++offset)
      payload.push_back(vertices[indices[span.firstIndex + offset]]);
  }
  return payload;
}

void verifyPrimitiveSemanticIdentity() {
  const SceneStamp firstStamp = stamp(33, 1, 33, 33, 33, 1);
  auto originalScene = scene(128, 3301, firstStamp);
  SketchUploadOptions upload;
  upload.maximumChunkBytes = 8U * 1024U;
  auto original = prepareSketchScene(originalScene, {}, {}, {}, upload);
  require(original.has_value(), "semantic identity reference mesh failed");

  const auto primitives = originalScene->primitives();
  std::optional<std::pair<std::size_t, std::size_t>> selected;
  for (std::size_t first = 0U; first < primitives.size() && !selected;
       ++first) {
    for (std::size_t second = first + 1U; second < primitives.size();
         ++second) {
      if (primitives[first].style == primitives[second].style &&
          primitiveTrianglePayload(*(*original)->mesh(),
                                   *(*original)->primitiveTessellationIndex(),
                                   primitives[first].handle) !=
              primitiveTrianglePayload(
                  *(*original)->mesh(),
                  *(*original)->primitiveTessellationIndex(),
                  primitives[second].handle)) {
        selected = std::pair{first, second};
        break;
      }
    }
  }
  require(selected.has_value(),
          "semantic identity fixture had no distinct same-style geometry");
  const auto [first, second] = *selected;
  const SketchPrimitiveHandle firstHandle = primitives[first].handle;
  const SketchPrimitiveHandle secondHandle = primitives[second].handle;
  const auto firstPayload = primitiveTrianglePayload(
      *(*original)->mesh(), *(*original)->primitiveTessellationIndex(),
      firstHandle);
  const auto secondPayload = primitiveTrianglePayload(
      *(*original)->mesh(), *(*original)->primitiveTessellationIndex(),
      secondHandle);

  std::vector<Point2d> swappedPoints(originalScene->points().begin(),
                                     originalScene->points().end());
  std::vector<PackedSketchPrimitive> swappedPrimitives(primitives.begin(),
                                                       primitives.end());
  std::swap(swappedPrimitives[first].handle, swappedPrimitives[second].handle);
  auto swappedScene = SketchSceneSnapshot::create(
      stamp(33, 2, 33, 33, 33, 2), styles(), std::move(swappedPoints),
      std::move(swappedPrimitives));
  require(swappedScene.has_value(), "handle-swap scene was invalid");
  auto swappedSnapshot =
      std::make_shared<const SketchSceneSnapshot>(std::move(*swappedScene));
  auto swapped =
      prepareSketchScene(swappedSnapshot, {}, {}, {}, upload, *original);
  require(swapped.has_value(), "handle-swap mesh preparation failed");
  requireValidPrimitiveTessellationIndex(
      *swappedSnapshot, *(*swapped)->mesh(),
      *(*swapped)->primitiveTessellationIndex());

  const auto originalChunks = (*original)->mesh()->chunks();
  const auto swappedChunks = (*swapped)->mesh()->chunks();
  require(originalChunks.size() == swappedChunks.size() &&
              std::ranges::equal(originalChunks, swappedChunks),
          "semantic-only handle swap invalidated geometry chunks");
  require(
      primitiveTrianglePayload(*(*swapped)->mesh(),
                               *(*swapped)->primitiveTessellationIndex(),
                               secondHandle) == firstPayload &&
          primitiveTrianglePayload(*(*swapped)->mesh(),
                                   *(*swapped)->primitiveTessellationIndex(),
                                   firstHandle) == secondPayload,
      "primitive index associated a handle with same-style foreign geometry");
}

void verifyPreparationCancellation(const testkit::PropertyProfile &profile) {
  const std::array scales{std::size_t{10}, std::size_t{1'000},
                          std::size_t{10'000}};
  std::array<std::shared_ptr<const SketchSceneSnapshot>, scales.size()> scenes{
      scene(scales[0], 81, stamp(30, 1, 30, 30, 30, 1)),
      scene(scales[1], 82, stamp(30, 2, 30, 30, 30, 2)),
      scene(scales[2], 83, stamp(30, 3, 30, 30, 30, 3)),
  };
  testkit::checkProperty(
      "scaled sketch preparation cancellation", profile,
      [&](testkit::Random &random, std::uint64_t) {
        const auto &generated =
            scenes[static_cast<std::size_t>(random.next() % scenes.size())];
        std::stop_source source;
        require(source.request_stop(),
                "generated cancellation source was already stopped");
        auto mesh = buildSketchSceneMesh(
            *generated, {}, {}, {}, std::shared_ptr<const SketchSceneMesh>{},
            source.get_token());
        auto prepared = prepareSketchScene(
            generated, {}, {}, {}, {},
            std::shared_ptr<const PreparedSketchScene>{}, source.get_token());
        require(
            !mesh &&
                mesh.error().code == "desktop.sketch.preparation-cancelled" &&
                !prepared &&
                prepared.error().code == "desktop.sketch.preparation-cancelled",
            "cancelled preparation returned a partial or unstable result");
      });

  std::stop_source live;
  auto completed = prepareSketchScene(
      scenes.back(), {}, {}, {}, {},
      std::shared_ptr<const PreparedSketchScene>{}, live.get_token());
  require(completed && (*completed)->scene() == scenes.back(),
          "live cancellation token changed successful preparation");

  auto large = scene(20'000, 84, stamp(31, 1, 31, 31, 31, 1));
  PreparationAllocationGate gate;
  std::stop_source source;
  std::optional<Result<std::shared_ptr<const PreparedSketchScene>>> result;
  std::thread worker([&] {
    preparationAllocationGate = &gate;
    result.emplace(prepareSketchScene(
        large, {}, {}, {}, {}, std::shared_ptr<const PreparedSketchScene>{},
        source.get_token()));
    preparationAllocationGate = nullptr;
  });
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds{2};
  while (!gate.reached.load(std::memory_order_acquire) &&
         std::chrono::steady_clock::now() < deadline)
    std::this_thread::yield();
  const bool entered = gate.reached.load(std::memory_order_acquire);
  static_cast<void>(source.request_stop());
  gate.release.store(true, std::memory_order_release);
  worker.join();
  require(entered && result && !*result &&
              result->error().code == "desktop.sketch.preparation-cancelled",
          "in-flight worker preparation did not cancel coherently");

  auto phased = scene(4'000, 85, stamp(32, 1, 32, 32, 32, 1));
  measuredAllocations = 0U;
  measureAllocations = true;
  auto allocationBaseline = prepareSketchScene(phased);
  measureAllocations = false;
  require(allocationBaseline.has_value() && measuredAllocations >= 8U,
          "preparation phase allocation baseline was invalid");
  const std::size_t allocationCount = measuredAllocations;
  std::array<std::size_t, 6> cancellationOrdinals{
      1U,
      std::max<std::size_t>(1U, allocationCount / 8U),
      std::max<std::size_t>(1U, allocationCount / 4U),
      std::max<std::size_t>(1U, allocationCount / 2U),
      std::max<std::size_t>(1U, allocationCount * 3U / 4U),
      std::max<std::size_t>(1U, allocationCount * 7U / 8U),
  };
  std::ranges::sort(cancellationOrdinals);
  const auto uniqueOrdinals = std::ranges::unique(cancellationOrdinals);
  const std::size_t uniqueOrdinalCount = static_cast<std::size_t>(
      std::distance(cancellationOrdinals.begin(), uniqueOrdinals.begin()));
  for (const std::size_t ordinal :
       std::span{cancellationOrdinals}.first(uniqueOrdinalCount)) {
    PreparationAllocationGate phaseGate;
    phaseGate.stopAtAllocation = ordinal;
    std::stop_source phaseCancellation;
    std::optional<Result<std::shared_ptr<const PreparedSketchScene>>>
        phaseResult;
    std::thread phaseWorker([&] {
      preparationAllocationGate = &phaseGate;
      phaseResult.emplace(prepareSketchScene(phased, {}, {}, {}, {}, {},
                                             phaseCancellation.get_token()));
      preparationAllocationGate = nullptr;
    });
    const auto phaseDeadline =
        std::chrono::steady_clock::now() + std::chrono::seconds{30};
    while (!phaseGate.reached.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < phaseDeadline)
      std::this_thread::yield();
    const bool phaseReached = phaseGate.reached.load(std::memory_order_acquire);
    static_cast<void>(phaseCancellation.request_stop());
    phaseGate.release.store(true, std::memory_order_release);
    phaseWorker.join();
    require(phaseReached && phaseResult && !*phaseResult &&
                phaseResult->error().code ==
                    "desktop.sketch.preparation-cancelled",
            "preparation phase ignored cancellation without partial output");
  }
}

struct NeutralFaultSource {
  std::vector<SketchStrokeSourcePrimitive> primitives;
  std::size_t calls = 0U;
  bool corruptSecondPass = false;
  bool mutateSecondPass = false;
};

SketchStrokeSourcePrimitive neutralFaultPrimitive(const void *opaque,
                                                  std::size_t index) noexcept {
  auto &source = *const_cast<NeutralFaultSource *>(
      static_cast<const NeutralFaultSource *>(opaque));
  SketchStrokeSourcePrimitive primitive = source.primitives[index];
  ++source.calls;
  if (source.corruptSecondPass && source.calls > source.primitives.size())
    primitive.style = std::numeric_limits<std::uint16_t>::max();
  if (source.mutateSecondPass && source.calls > source.primitives.size())
    primitive.visible = !primitive.visible;
  return primitive;
}

void verifyNeutralStrokeKernel() {
  for (const std::size_t count : {1'000U, 10'000U}) {
    auto generated =
        scene(count, count + 700U, stamp(73, count, 73, 73, 73, count));
    auto neutral = SketchStrokeMeshBuildAccess::build(neutralSource(*generated),
                                                      {}, {}, {}, {}, {});
    auto adapted = buildSketchSceneMesh(*generated);
    auto prepared = prepareSketchScene(generated);
    require(neutral && adapted && prepared,
            "valid neutral stroke source failed to build");
    requireSameNeutralMesh(neutral->mesh, *adapted);
    require(neutral->retainedOutputBytes ==
                    neutral->mesh.metrics().retainedMeshBytes +
                        neutral->sourceProvenanceBytes &&
                neutral->scratchBytes ==
                    neutral->mesh.metrics().preparationScratchBytes &&
                neutral->peakBytes ==
                    neutral->retainedOutputBytes + neutral->scratchBytes &&
                neutral->sourceProvenanceSpans == neutral->provenance.size(),
            "neutral stroke memory accounting is inconsistent");
    std::size_t provenanceIndex = 0U;
    std::size_t visibleEntries = 0U;
    for (const PackedSketchPrimitive &primitive : generated->primitives()) {
      if (!hasFlag(primitive.flags, SketchPrimitiveFlags::Visible))
        continue;
      ++visibleEntries;
      const auto spans =
          (*prepared)->primitiveTessellationIndex()->spans(primitive.handle);
      require(!spans.empty(), "visible primitive omitted typed provenance");
      for (const SketchPrimitiveChunkSpan &span : spans) {
        require(provenanceIndex < neutral->provenance.size(),
                "neutral provenance ended before typed provenance");
        const SketchStrokePrimitiveSpanRecord &record =
            neutral->provenance[provenanceIndex++];
        require(record.sourceKey == primitive.handle.value() &&
                    record.chunk == span.chunk &&
                    record.firstIndex == span.firstIndex &&
                    record.indexCount == span.indexCount,
                "neutral and typed provenance disagree");
      }
    }
    require(provenanceIndex == neutral->provenance.size() &&
                visibleEntries == neutral->sourceProvenanceEntries,
            "neutral provenance contains unowned records");
    const auto &preparedMetrics = (*prepared)->metrics();
    require(
        preparedMetrics.meshRetainedBytes ==
                (*prepared)->mesh()->metrics().retainedMeshBytes &&
            preparedMetrics.provenanceRetainedBytes ==
                (*prepared)->primitiveTessellationIndex()->retainedBytes() &&
            preparedMetrics.pickIndexRetainedBytes ==
                (*prepared)->pickIndex()->retainedBytes() &&
            preparedMetrics.totalRetainedBytes ==
                sizeof(PreparedSketchScene) +
                    preparedMetrics.meshRetainedBytes +
                    preparedMetrics.provenanceRetainedBytes +
                    preparedMetrics.pickIndexRetainedBytes,
        "prepared scene retained bytes are double-counted or incomplete");

    if (count == 1'000U) {
      SketchUploadOptions exact;
      exact.maximumRetainedMeshBytes = neutral->retainedOutputBytes;
      exact.maximumPreparationScratchBytes = neutral->scratchBytes;
      exact.maximumPreparationPeakBytes = neutral->peakBytes;
      require(SketchStrokeMeshBuildAccess::build(neutralSource(*generated), {},
                                                 {}, exact, {}, {})
                  .has_value(),
              "exact neutral stroke memory ceilings were rejected");
      if (neutral->retainedOutputBytes > 1U) {
        SketchUploadOptions limited;
        limited.maximumRetainedMeshBytes = neutral->retainedOutputBytes - 1U;
        auto rejected = SketchStrokeMeshBuildAccess::build(
            neutralSource(*generated), {}, {}, limited, {}, {});
        require(!rejected && rejected.error().code ==
                                 "desktop.sketch.mesh-retained-budget",
                "retained neutral stroke boundary was not exact");
      }
      if (neutral->scratchBytes > 1U) {
        SketchUploadOptions limited;
        limited.maximumPreparationScratchBytes = neutral->scratchBytes - 1U;
        auto rejected = SketchStrokeMeshBuildAccess::build(
            neutralSource(*generated), {}, {}, limited, {}, {});
        require(!rejected && rejected.error().code ==
                                 "desktop.sketch.mesh-scratch-budget",
                "scratch neutral stroke boundary was not exact");
      }
      if (neutral->peakBytes > 1U) {
        SketchUploadOptions limited;
        limited.maximumPreparationPeakBytes = neutral->peakBytes - 1U;
        auto rejected = SketchStrokeMeshBuildAccess::build(
            neutralSource(*generated), {}, {}, limited, {}, {});
        require(!rejected &&
                    rejected.error().code == "desktop.sketch.mesh-peak-budget",
                "peak neutral stroke boundary was not exact");
      }
    }
  }

  const std::array<SketchStyle, 1> validStyles{
      {{SketchStyleRole::Regular, SketchLinePattern::Solid, 1.0F, 4.0F, 0U}}};
  const SketchStrokeSourcePrimitive point{
      1U,  0U, SketchStrokeSourceKind::Point, true, {0.0, 0.0}, {0.0, 0.0}, 0.0,
      0.0, 0.0};
  const auto buildFault = [&](NeutralFaultSource &fixture,
                              SketchStrokeSourceBounds bounds,
                              std::span<const SketchStyle> styles = {}) {
    if (styles.empty())
      styles = validStyles;
    fixture.calls = 0U;
    return SketchStrokeMeshBuildAccess::build({styles, &fixture,
                                               fixture.primitives.size(),
                                               neutralFaultPrimitive, bounds},
                                              {}, {}, {}, {}, {});
  };
  const SketchStrokeSourceBounds pointBounds{{0.0, 0.0}, {0.0, 0.0}, false};
  NeutralFaultSource duplicate{{point, point}};
  duplicate.primitives[1].sourceKey = 1U;
  duplicate.primitives[1].visible = false;
  auto duplicateResult = buildFault(duplicate, pointBounds);
  require(!duplicateResult && duplicateResult.error().code ==
                                  "desktop.sketch.duplicate-source-key",
          "duplicate hidden neutral source keys were accepted");
  NeutralFaultSource unordered{{point, point}};
  unordered.primitives[0].sourceKey = 2U;
  unordered.primitives[1].sourceKey = 1U;
  auto unorderedResult = buildFault(unordered, pointBounds);
  require(unorderedResult.has_value(),
          "neutral stroke kernel depended on source ordering");
  NeutralFaultSource invalid{{point}};
  invalid.primitives[0].kind = static_cast<SketchStrokeSourceKind>(255U);
  require(!buildFault(invalid, pointBounds),
          "unknown neutral primitive kind was accepted");
  invalid.primitives[0] = point;
  invalid.primitives[0].first.x = std::numeric_limits<double>::quiet_NaN();
  require(!buildFault(invalid, pointBounds),
          "non-finite neutral primitive was accepted");
  invalid.primitives[0] = point;
  invalid.primitives[0].radius = -1.0;
  require(!buildFault(invalid, pointBounds),
          "negative neutral geometry was accepted");
  invalid.primitives[0] = point;
  invalid.primitives[0].first.x = 1.0;
  require(!buildFault(invalid, pointBounds),
          "neutral primitive escaped declared bounds");
  invalid.primitives[0] = point;
  invalid.corruptSecondPass = true;
  require(!buildFault(invalid, pointBounds),
          "stateful neutral source bypassed second-pass validation");
  invalid.corruptSecondPass = false;
  invalid.mutateSecondPass = true;
  auto changed = buildFault(invalid, pointBounds);
  require(!changed &&
              changed.error().code == "desktop.sketch.changed-mesh-source",
          "valid source mutation produced a mixed preparation");
  invalid.mutateSecondPass = false;
  auto invalidBounds = pointBounds;
  invalidBounds.minimum.x = 1.0;
  require(!buildFault(invalid, invalidBounds),
          "inverted neutral source bounds were accepted");
  std::stop_source stopped;
  static_cast<void>(stopped.request_stop());
  auto cancelled = SketchStrokeMeshBuildAccess::build(
      {validStyles, &invalid, 1U, neutralFaultPrimitive, pointBounds}, {}, {},
      {}, {}, stopped.get_token());
  require(!cancelled &&
              cancelled.error().code == "desktop.sketch.preparation-cancelled",
          "pre-cancelled neutral stroke build produced output");
  auto empty = SketchStrokeMeshBuildAccess::build(
      {{}, nullptr, 0U, nullptr, {}}, {}, {}, {}, {}, {});
  require(empty && empty->mesh.chunks().empty(),
          "empty neutral stroke source required a callback");
  SketchUploadOptions excessive;
  excessive.maximumPreparationPeakBytes =
      SketchUploadOptions::maximumConfigurablePreparationPeakBytes + 1U;
  require(!SketchStrokeMeshBuildAccess::build(
              {validStyles, &invalid, 1U, neutralFaultPrimitive, pointBounds},
              {}, {}, excessive, {}, {}),
          "excessive neutral stroke configuration was accepted");

  NeutralFaultSource horizontalGlyph{{point}};
  horizontalGlyph.primitives[0].kind = SketchStrokeSourceKind::Glyph;
  horizontalGlyph.primitives[0].glyph =
      static_cast<std::uint16_t>(SketchMarkerKind::HorizontalConstraint);
  auto horizontal = buildFault(horizontalGlyph, pointBounds);
  NeutralFaultSource verticalGlyph = horizontalGlyph;
  verticalGlyph.primitives[0].glyph =
      static_cast<std::uint16_t>(SketchMarkerKind::VerticalConstraint);
  auto vertical = buildFault(verticalGlyph, pointBounds);
  require(horizontal && vertical && horizontal->mesh.chunks().size() == 1U &&
              vertical->mesh.chunks().size() == 1U &&
              horizontal->provenance.size() == 1U &&
              vertical->provenance.size() == 1U &&
              horizontal->mesh.chunks()[0]
                      ->bounds()
                      .maximumExtrusionLogicalPixels >= 2.0 &&
              !std::ranges::equal(horizontal->mesh.chunks()[0]->vertices(),
                                  vertical->mesh.chunks()[0]->vertices()),
          "neutral glyphs lost fixed-pixel geometry or kind identity");
  auto edgeTransform = SketchViewTransform::create(
      {1U, {0.0515, 0.0}, 0.001, 0.0}, {100.0, 100.0});
  require(edgeTransform.has_value(), "glyph edge transform was invalid");
  auto edgeVisible = horizontal->mesh.visibleChunks(
      *edgeTransform, SketchPickCoveragePolicy{1U, 0.0});
  require(edgeVisible && !edgeVisible->chunks.empty(),
          "fixed-pixel glyph culling clipped a visible edge symbol");
  NeutralFaultSource invalidGlyph = horizontalGlyph;
  invalidGlyph.primitives[0].glyph = 0U;
  require(!buildFault(invalidGlyph, pointBounds),
          "zero neutral glyph code was accepted");

  NeutralFaultSource expensiveCurve{{point}};
  expensiveCurve.primitives[0].kind = SketchStrokeSourceKind::Circle;
  expensiveCurve.primitives[0].first = {0.0, 0.0};
  expensiveCurve.primitives[0].radius = 1.0e12;
  const SketchStrokeSourceBounds curveBounds{
      {-1.0e12, -1.0e12}, {1.0e12, 1.0e12}, false};
  SketchUploadOptions guarded;
  guarded.maximumPreparationScratchBytes = 16U * 1024U;
  maximumMeasuredAllocationBytes = 0U;
  expensiveCurve.calls = 0U;
  measureAllocations = true;
  auto guardedResult = SketchStrokeMeshBuildAccess::build(
      {validStyles, &expensiveCurve, expensiveCurve.primitives.size(),
       neutralFaultPrimitive, curveBounds},
      {}, {}, guarded, {}, {});
  measureAllocations = false;
  require(!guardedResult &&
              guardedResult.error().code ==
                  "desktop.sketch.mesh-scratch-budget" &&
              maximumMeasuredAllocationBytes <=
                  guarded.maximumPreparationScratchBytes,
          "curve scratch budget was checked only after a forbidden allocation");
}

void verifyMeshGeneration(const testkit::PropertyProfile &profile) {
  for (const std::size_t size : {10U, 100U, 1'000U, 10'000U}) {
    auto generated = scene(size, size, stamp(3, size, 3, 3, 3, size));
    auto prepared = prepareSketchScene(generated);
    require(prepared && (*prepared)->stamp() == generated->stamp() &&
                (*prepared)->scene() == generated &&
                &(*prepared)->pickIndex()->scene() == generated.get() &&
                (*prepared)->mesh()->lod() == (*prepared)->lod(),
            "prepared scene did not preserve exact immutable inputs");
    require((*prepared)->primitiveTessellationIndex() != nullptr,
            "prepared scene omitted typed primitive provenance");
    requireValidMesh(*generated, *(*prepared)->mesh(),
                     *(*prepared)->primitiveTessellationIndex());
  }
  testkit::checkProperty(
      "batched sketch mesh invariants", profile,
      [&](testkit::Random &random, std::uint64_t index) {
        const std::size_t count =
            static_cast<std::size_t>(random.next() % 64U + 1U);
        auto generated = scene(count, random.next(),
                               stamp(4, index + 1U, 4, 4, 4, index + 1U));
        auto prepared = prepareSketchScene(generated);
        require(prepared.has_value(), "valid generated mesh was rejected");
        requireValidMesh(*generated, *(*prepared)->mesh(),
                         *(*prepared)->primitiveTessellationIndex());
      });

  SketchTessellationOptions constrained;
  constrained.maximumVertices = 1;
  constrained.maximumIndices = 1;
  auto rejected = buildSketchSceneMesh(*scene(1, 1, stamp(5, 1, 5, 5, 5, 1)),
                                       {}, constrained);
  require(!rejected && rejected.error().code == "desktop.sketch.mesh-budget",
          "mesh resource budget was not enforced");

  if constexpr (std::numeric_limits<std::size_t>::max() >
                std::numeric_limits<std::uint32_t>::max()) {
    SketchTessellationOptions unpackable;
    unpackable.maximumIndices =
        static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) +
        1U;
    auto invalid = buildSketchSceneMesh(*scene(1, 2, stamp(5, 90, 5, 5, 5, 90)),
                                        {}, unpackable);
    require(!invalid && invalid.error().code ==
                            "desktop.sketch.invalid-tessellation-options",
            "unpackable source-batch index range was accepted");
  }

  auto visibilitySource = scene(4, 51, stamp(5, 2, 5, 5, 5, 2));
  std::vector<Point2d> visibilityPoints(visibilitySource->points().begin(),
                                        visibilitySource->points().end());
  std::vector<PackedSketchPrimitive> visibilityPrimitives(
      visibilitySource->primitives().begin(),
      visibilitySource->primitives().end());
  const SketchPrimitiveHandle hiddenHandle = visibilityPrimitives[1].handle;
  visibilityPrimitives[1].flags = SketchPrimitiveFlags::Selectable;
  auto visibilityScene = SketchSceneSnapshot::create(
      stamp(5, 3, 5, 5, 5, 3), styles(), std::move(visibilityPoints),
      std::move(visibilityPrimitives));
  require(visibilityScene.has_value(), "hidden primitive scene was invalid");
  auto visibilitySnapshot =
      std::make_shared<const SketchSceneSnapshot>(std::move(*visibilityScene));
  auto visibilityPrepared = prepareSketchScene(visibilitySnapshot);
  require(visibilityPrepared.has_value() &&
              (*visibilityPrepared)
                      ->primitiveTessellationIndex()
                      ->find(hiddenHandle) == nullptr,
          "hidden primitive received tessellated ranges");
  requireValidMesh(*visibilitySnapshot, *(*visibilityPrepared)->mesh(),
                   *(*visibilityPrepared)->primitiveTessellationIndex());

  auto duplicateSource = scene(2, 52, stamp(5, 4, 5, 5, 5, 4));
  std::vector<Point2d> duplicatePoints(duplicateSource->points().begin(),
                                       duplicateSource->points().end());
  std::vector<PackedSketchPrimitive> duplicatePrimitives(
      duplicateSource->primitives().begin(),
      duplicateSource->primitives().end());
  duplicatePrimitives[1].handle = duplicatePrimitives[0].handle;
  auto duplicateScene = SketchSceneSnapshot::create(
      stamp(5, 5, 5, 5, 5, 5), styles(), std::move(duplicatePoints),
      std::move(duplicatePrimitives));
  require(!duplicateScene &&
              duplicateScene.error().code == "render.sketch.duplicate-handle",
          "duplicate primitive handles reached tessellation");
}

void verifyLodReusesScenePicking() {
  auto generated = scene(1'000, 140, stamp(14, 1, 14, 14, 14, 1));
  auto coarse = prepareSketchScene(generated, SketchCurveLod{-13});
  require(coarse.has_value(), "reference LOD preparation failed");
  auto fine =
      prepareSketchScene(generated, SketchCurveLod{-17}, {}, {}, {}, *coarse);
  require(fine && (*fine)->mesh() != (*coarse)->mesh() &&
              (*fine)->pickIndex() == (*coarse)->pickIndex(),
          "LOD-only preparation rebuilt camera-independent picking");

  render::SketchPickIndexOptions changedOptions;
  changedOptions.maximumLeafTargets = 4U;
  auto changed = prepareSketchScene(generated, SketchCurveLod{-18}, {},
                                    changedOptions, {}, *fine);
  require(changed && (*changed)->pickIndex() != (*fine)->pickIndex() &&
              (*changed)->pickOptions().maximumLeafTargets == 4U,
          "changed pick-index policy reused an incompatible index");
}

void verifyRetainedGeometry() {
  const SceneStamp firstStamp = stamp(6, 1, 6, 6, 6, 1);
  auto generated = scene(1'000, 13, firstStamp);
  SketchScenePresenter presenter;
  presenter.retarget(firstStamp.target);
  require(presenter.publishCamera({2, {}, 0.0002, 0.0}) ==
              SketchCameraDecision::Accepted,
          "reference retained camera publication failed");
  auto firstPrepared = preparedScene(generated, presenter.requestedLod());
  auto firstProducts = preparedProductPacket(firstPrepared);
  require(presenter.publish(firstProducts).has_value(),
          "reference retained scene publication failed");
  auto frame = presenter.synchronize({1200.0, 800.0});
  require(frame.has_value(), "reference retained scene synchronization failed");
  auto firstMesh = (*frame)->mesh();
  require(firstMesh == firstPrepared->mesh(),
          "synchronization did not retain the prepared mesh");

  for (std::uint64_t generation = 3; generation <= 2'001; ++generation) {
    SketchCamera2d moved{generation,
                         {static_cast<double>(generation % 101U) * 0.01,
                          -static_cast<double>(generation % 83U) * 0.01},
                         0.0002,
                         static_cast<double>(generation % 360U) *
                             std::numbers::pi / 180.0};
    require(presenter.publishCamera(moved) == SketchCameraDecision::Accepted,
            "generated retained camera was rejected");
    const QSizeF viewport{640.0 + static_cast<double>(generation % 1'600U),
                          480.0 + static_cast<double>(generation % 900U)};
    frame = presenter.synchronize(viewport);
    require(frame.has_value(), "generated retained frame failed");
    require((*frame)->mesh() == firstMesh &&
                (*frame)->prepared() == firstPrepared,
            "camera, offscreen pan, rotation, or resize rebuilt geometry");
  }
  require(presenter.synchronizationMetrics().scalablePreparations == 0U,
          "render synchronization performed scalable preparation");

  SketchCamera2d finer = camera(2'002);
  finer.metresPerLogicalPixel = 0.00005;
  require(presenter.publishCamera(finer) == SketchCameraDecision::Accepted,
          "finer LOD camera was rejected");
  frame = presenter.synchronize({1600.0, 900.0});
  require(frame.has_value(), "finer LOD frame failed");
  require((*frame)->prepared() == firstPrepared &&
              (*frame)->mesh() == firstMesh,
          "missing finer LOD preparation discarded the last valid frame");
  require(presenter.publish(firstProducts) ==
              PreparedSketchSceneOffer{PreparedSketchSceneDecision::StaleLod,
                                       false},
          "stale LOD packet was accepted after a camera transition");
  auto finerPrepared = preparedScene(generated, presenter.requestedLod());
  auto finerProducts = preparedProductPacket(finerPrepared, 2U, 2U);
  require(finerPrepared->mesh() != firstMesh &&
              finerPrepared->lod() != firstPrepared->lod() &&
              presenter.publish(finerProducts) ==
                  PreparedSketchSceneOffer{
                      PreparedSketchSceneDecision::Accepted, false},
          "finer LOD preparation was not accepted");
  frame = presenter.synchronize({1600.0, 900.0});
  require(frame && (*frame)->prepared() == finerPrepared,
          "finer LOD packet was not installed exactly");

  auto replacement = scene(1'001, 14, stamp(6, 2, 6, 6, 6, 2));
  auto replacementPrepared =
      preparedScene(replacement, presenter.requestedLod());
  auto replacementProducts = preparedProductPacket(replacementPrepared, 3U, 3U);
  require(presenter.publish(replacementProducts).has_value(),
          "replacement retained scene publication failed");
  frame = presenter.synchronize({1600.0, 900.0});
  require(frame.has_value(), "replacement retained frame failed");
  require((*frame)->prepared() == replacementPrepared &&
              (*frame)->mesh() == replacementPrepared->mesh() &&
              presenter.synchronizationMetrics().preparedPacketInstalls == 3U &&
              presenter.synchronizationMetrics().scalablePreparations == 0U,
          "new scene generation did not install one prepared packet");
}

void verifyLargeCoordinatePrecision() {
  constexpr double base = 1'000'000.0;
  std::vector<Point2d> points{{base + 0.001, base - 0.002},
                              {base + 0.006, base + 0.003},
                              {base + 0.004, base + 0.001},
                              {base + 16'384.0, base}};
  auto lineHandle = SketchPrimitiveHandle::create(1);
  auto circleHandle = SketchPrimitiveHandle::create(2);
  auto pointHandle = SketchPrimitiveHandle::create(3);
  require(lineHandle && circleHandle && pointHandle,
          "precision primitive handle failed");
  std::vector<PackedSketchPrimitive> primitives{
      {id<SketchEntityId>(91), *lineHandle, 0, 0, SketchPrimitiveKind::Line,
       SketchPrimitiveFlags::Visible | SketchPrimitiveFlags::Selectable, 0.0,
       0.0, 0.0},
      {id<SketchEntityId>(92), *circleHandle, 2, 0, SketchPrimitiveKind::Circle,
       SketchPrimitiveFlags::Visible | SketchPrimitiveFlags::Selectable, 0.002,
       0.0, 0.0},
      {id<SketchEntityId>(93), *pointHandle, 3, 0, SketchPrimitiveKind::Point,
       SketchPrimitiveFlags::Visible | SketchPrimitiveFlags::Selectable, 0.0,
       0.0, 0.0}};
  auto created =
      SketchSceneSnapshot::create(stamp(7, 1, 7, 7, 7, 1), styles(),
                                  std::move(points), std::move(primitives));
  require(created.has_value(), "large-coordinate reference scene failed");
  auto mesh = buildSketchSceneMesh(*created);
  require(mesh.has_value(), "large-coordinate reference mesh failed");
  require(std::abs(mesh->originMetres().x - (base + 8'192.0)) < 1.0 &&
              std::abs(mesh->originMetres().y - base) < 0.02,
          "mesh did not establish a scene-local floating origin");
  for (const auto &chunk : mesh->chunks())
    for (const SketchMeshVertex vertex : chunk->vertices())
      require(std::abs(vertex.x) < 20'000.0F && std::abs(vertex.y) < 20'000.0F,
              "absolute SI coordinates were narrowed into GPU floats");

  const SketchCamera2d view{8, {base + 0.003, base}, 0.00001, 0.31};
  auto transform = SketchViewTransform::create(view, {1920.0, 1080.0});
  require(transform.has_value(), "large-coordinate camera failed");
  const SketchMeshVertex vertex = mesh->chunks().front()->vertices().front();
  const Point2d canonical{
      mesh->originMetres().x + static_cast<double>(vertex.x) + vertex.xLow,
      mesh->originMetres().y + static_cast<double>(vertex.y) + vertex.yLow};
  const QPointF direct = transform->toItem(canonical);
  auto viewUniforms = transform->gpuView(mesh->originMetres());
  require(viewUniforms.has_value(), "finite GPU view was rejected");
  const float relativeX = (vertex.x - viewUniforms->centerOffsetX) +
                          (vertex.xLow - viewUniforms->centerOffsetXLow);
  const float relativeY = (vertex.y - viewUniforms->centerOffsetY) +
                          (vertex.yLow - viewUniforms->centerOffsetYLow);
  const QPointF packed{960.0F + (viewUniforms->cosine * relativeX -
                                 viewUniforms->sine * relativeY) /
                                    viewUniforms->metresPerLogicalPixel,
                       540.0F - (viewUniforms->sine * relativeX +
                                 viewUniforms->cosine * relativeY) /
                                    viewUniforms->metresPerLogicalPixel};
  require(std::hypot(direct.x() - packed.x(), direct.y() - packed.y()) < 0.05,
          "floating-origin GPU transform lost millimetre geometry");
}

std::shared_ptr<const SketchSceneSnapshot>
fixedScene(SceneStamp sceneStamp, std::vector<SketchStyle> sceneStyles,
           std::vector<Point2d> points,
           std::vector<PackedSketchPrimitive> primitives) {
  auto created =
      SketchSceneSnapshot::create(std::move(sceneStamp), std::move(sceneStyles),
                                  std::move(points), std::move(primitives));
  require(created.has_value(), "rendered-pick fixture scene was invalid");
  return std::make_shared<const SketchSceneSnapshot>(std::move(*created));
}

std::shared_ptr<const SynchronizedSketchScene>
displayedFrame(const std::shared_ptr<const PreparedSketchScene> &prepared,
               SketchCamera2d camera, QSizeF viewport,
               std::vector<std::uint32_t> chunks = {},
               SketchPickCoveragePolicy pickCoverage = {}) {
  auto transform = SketchViewTransform::create(camera, viewport);
  require(transform.has_value(), "rendered-pick fixture transform failed");
  if (chunks.empty()) {
    chunks.resize(prepared->mesh()->chunks().size());
    std::iota(chunks.begin(), chunks.end(), 0U);
  }
  auto coverage = SketchPresentedChunkCoverage::create(*prepared->mesh(),
                                                       std::move(chunks));
  require(coverage.has_value(), "rendered-pick fixture coverage failed");
  return std::make_shared<const SynchronizedSketchScene>(
      preparedProductPacket(prepared), std::move(*transform), pickCoverage,
      std::move(*coverage));
}

Result<SketchItemPickEvidence>
displayedPick(const std::shared_ptr<const SynchronizedSketchScene> &frame,
              QPointF item, double tolerance,
              SketchPickTargets targets = SketchPickTargets::All) {
  SketchScenePresenter presenter;
  return presenter.pick(frame, item, tolerance, targets);
}

QPointF projectedVertex(const SynchronizedSketchScene &frame,
                        const SketchMeshVertex &vertex) {
  auto view = frame.transform().gpuView(frame.mesh()->originMetres());
  require(view.has_value(), "rendered-pick fixture GPU view failed");
  const float relativeX =
      (vertex.x - view->centerOffsetX) + (vertex.xLow - view->centerOffsetXLow);
  const float relativeY =
      (vertex.y - view->centerOffsetY) + (vertex.yLow - view->centerOffsetYLow);
  float x =
      static_cast<float>(frame.transform().viewportLogical().width()) * 0.5F;
  float y =
      static_cast<float>(frame.transform().viewportLogical().height()) * 0.5F;
  x += (view->cosine * relativeX - view->sine * relativeY) /
       view->metresPerLogicalPixel;
  y -= (view->sine * relativeX + view->cosine * relativeY) /
       view->metresPerLogicalPixel;
  x += view->cosine * vertex.extrusionX - view->sine * vertex.extrusionY;
  y -= view->sine * vertex.extrusionX + view->cosine * vertex.extrusionY;
  return {x, y};
}

void verifyExactRenderedPicking() {
  const auto selectable =
      SketchPrimitiveFlags::Visible | SketchPrimitiveFlags::Selectable;
  const QSizeF viewport{320.0, 240.0};
  const SketchCamera2d camera{2U, {}, 0.001, 0.0};

  const std::vector<SketchStyle> layeredStyles{
      {SketchStyleRole::Regular, SketchLinePattern::Solid, 2.0F, 12.0F, 0U},
      {SketchStyleRole::Regular, SketchLinePattern::Solid, 2.0F, 12.0F, 5U}};
  auto coincident = fixedScene(
      stamp(51, 1, 51, 51, 51, 1), layeredStyles, {{0.0, 0.0}, {0.0, 0.0}},
      {{id<SketchEntityId>(511U), *SketchPrimitiveHandle::create(1U), 0U, 0U,
        SketchPrimitiveKind::Point, selectable, 0.0, 0.0, 0.0},
       {id<SketchEntityId>(512U), *SketchPrimitiveHandle::create(2U), 1U, 1U,
        SketchPrimitiveKind::Point, selectable, 0.0, 0.0, 0.0}});
  auto coincidentPrepared = preparedScene(
      coincident,
      SketchCurveLod::forMetresPerLogicalPixel(camera.metresPerLogicalPixel));
  const auto &coincidentIndex =
      *coincidentPrepared->primitiveTessellationIndex();
  const std::uint32_t lowerChunk =
      coincidentIndex.spans(*SketchPrimitiveHandle::create(1U)).front().chunk;
  auto coincidentFrame =
      displayedFrame(coincidentPrepared, camera, viewport, {lowerChunk});
  auto coincidentPick = displayedPick(coincidentFrame, {160.0, 120.0}, 0.0,
                                      SketchPickTargets::Points);
  require(coincidentPick && coincidentPick->hit &&
              coincidentPick->hit->primitive.value() == 1U,
          "nonresident analytic winner hid a displayed coincident point");

  const std::vector<SketchStyle> solidStyle{
      {SketchStyleRole::Regular, SketchLinePattern::Solid, 2.0F, 12.0F, 0U}};
  auto pointScene = fixedScene(
      stamp(52, 1, 52, 52, 52, 1), solidStyle, {{0.0, 0.0}},
      {{id<SketchEntityId>(521U), *SketchPrimitiveHandle::create(1U), 0U, 0U,
        SketchPrimitiveKind::Point, selectable, 0.0, 0.0, 0.0}});
  auto pointPrepared = preparedScene(
      pointScene,
      SketchCurveLod::forMetresPerLogicalPixel(camera.metresPerLogicalPixel));
  auto pointFrame = displayedFrame(pointPrepared, camera, viewport);
  const double middleAngle = std::numbers::pi / 12.0;
  auto polygonVertex =
      displayedPick(pointFrame, {165.9, 120.0}, 0.0, SketchPickTargets::Points);
  auto polygonGap = displayedPick(pointFrame,
                                  {160.0 + 5.9 * std::cos(middleAngle),
                                   120.0 - 5.9 * std::sin(middleAngle)},
                                  0.0, SketchPickTargets::Points);
  require(polygonVertex && polygonVertex->hit && polygonGap && !polygonGap->hit,
          "point picking ignored the rendered twelve-gon boundary");

  auto lineScene = fixedScene(
      stamp(53, 1, 53, 53, 53, 1), solidStyle, {{-0.02, 0.0}, {0.02, 0.0}},
      {{id<SketchEntityId>(531U), *SketchPrimitiveHandle::create(1U), 0U, 0U,
        SketchPrimitiveKind::Line, selectable, 0.0, 0.0, 0.0}});
  auto linePrepared = preparedScene(
      lineScene,
      SketchCurveLod::forMetresPerLogicalPixel(camera.metresPerLogicalPixel));
  auto lineFrame = displayedFrame(linePrepared, camera, viewport);
  auto buttMiss =
      displayedPick(lineFrame, {180.5, 120.0}, 0.0, SketchPickTargets::Curves);
  auto buttTolerance =
      displayedPick(lineFrame, {180.5, 120.0}, 1.0, SketchPickTargets::Curves);
  require(buttMiss && !buttMiss->hit && buttTolerance && buttTolerance->hit,
          "line picking did not follow the rendered butt cap");

  SketchTessellationOptions coarse;
  coarse.maximumArcStepRadians = std::numbers::pi / 2.0;
  coarse.minimumCircleSegments = 3U;
  coarse.maximumCurveSegments = 3U;
  const std::vector<SketchStyle> arcStyle{
      {SketchStyleRole::Regular, SketchLinePattern::Solid, 0.5F, 12.0F, 0U}};
  auto arcScene =
      fixedScene(stamp(54, 1, 54, 54, 54, 1), arcStyle, {{0.0, 0.0}},
                 {{id<SketchEntityId>(541U), *SketchPrimitiveHandle::create(1U),
                   0U, 0U, SketchPrimitiveKind::Arc, selectable, 0.02, 0.0,
                   std::numbers::pi / 2.0}});
  auto arcPreparedResult = prepareSketchScene(
      arcScene,
      SketchCurveLod::forMetresPerLogicalPixel(camera.metresPerLogicalPixel),
      coarse);
  if (!arcPreparedResult)
    throw std::runtime_error("coarse rendered arc was rejected: " +
                             arcPreparedResult.error().code);
  auto arcFrame = displayedFrame(*arcPreparedResult, camera, viewport);
  const QPointF firstArcEndpoint = arcFrame->transform().toItem({0.02, 0.0});
  const QPointF secondArcEndpoint =
      arcFrame->transform().toItem({0.02 * std::cos(std::numbers::pi / 6.0),
                                    0.02 * std::sin(std::numbers::pi / 6.0)});
  const QPointF chordMidpoint = (firstArcEndpoint + secondArcEndpoint) * 0.5;
  auto chordMidpointPick =
      displayedPick(arcFrame, chordMidpoint, 0.0, SketchPickTargets::Curves);
  require(chordMidpointPick && chordMidpointPick->hit &&
              chordMidpointPick->displayedDistanceLogicalPixels == 0.0 &&
              arcFrame->presentedChunks()->maximumAnalyticDeviationMetres() >
                  arcFrame->presentedChunks()->maximumExtrusionLogicalPixels() *
                      camera.metresPerLogicalPixel,
          "resident chord deviation was absent from broad-phase coverage");
  const double halfSegment = std::numbers::pi / 12.0;
  const QPointF analyticArc{160.0 + 20.0 * std::cos(halfSegment),
                            120.0 - 20.0 * std::sin(halfSegment)};
  auto tessellatedMiss =
      displayedPick(arcFrame, analyticArc, 0.0, SketchPickTargets::Curves);
  auto tessellatedTolerance =
      displayedPick(arcFrame, analyticArc, 0.5, SketchPickTargets::Curves);
  require(tessellatedMiss && !tessellatedMiss->hit && tessellatedTolerance &&
              tessellatedTolerance->hit,
          "arc picking used the analytic curve instead of rendered triangles");

  const auto arcVertices =
      arcPreparedResult.value()->mesh()->chunks().front()->vertices();
  const auto miter = std::ranges::max_element(
      arcVertices, {}, [](const SketchMeshVertex &vertex) {
        return std::hypot(static_cast<double>(vertex.extrusionX),
                          static_cast<double>(vertex.extrusionY));
      });
  require(miter != arcVertices.end(), "rendered arc omitted miter vertices");
  const QPointF miterPoint = projectedVertex(*arcFrame, *miter);
  auto miterPick =
      displayedPick(arcFrame, miterPoint, 0.05, SketchPickTargets::Curves);
  const double miterExtrusion =
      std::hypot(static_cast<double>(miter->extrusionX),
                 static_cast<double>(miter->extrusionY));
  require(miterExtrusion > 0.25,
          "rendered arc did not produce its expected miter extrusion");
  require(arcPreparedResult.value()
                  ->mesh()
                  ->chunks()
                  .front()
                  ->bounds()
                  .maximumExtrusionLogicalPixels >= miterExtrusion,
          "miter extrusion was absent from chunk culling bounds");
  require(miterPick && miterPick->hit,
          "rendered miter vertex was absent from exact picking");

  const std::vector<SketchStyle> dashedStyle{{SketchStyleRole::Construction,
                                              SketchLinePattern::Dashed, 2.0F,
                                              8.0F, 0U}};
  auto dashScene = fixedScene(
      stamp(55, 1, 55, 55, 55, 1), dashedStyle, {{-0.05, 0.0}, {0.05, 0.0}},
      {{id<SketchEntityId>(551U), *SketchPrimitiveHandle::create(1U), 0U, 0U,
        SketchPrimitiveKind::Line, selectable, 0.0, 0.0, 0.0}});
  SketchUploadOptions narrowTiles;
  narrowTiles.spatialTileLogicalPixels = 8.0;
  auto dashPreparedResult = prepareSketchScene(
      dashScene,
      SketchCurveLod::forMetresPerLogicalPixel(camera.metresPerLogicalPixel),
      {}, {}, narrowTiles);
  require(dashPreparedResult.has_value(), "dashed rendered line was rejected");
  auto dashFrame = displayedFrame(*dashPreparedResult, camera, viewport);
  auto dashOn =
      displayedPick(dashFrame, {115.0, 120.0}, 0.0, SketchPickTargets::Curves);
  auto dashGap =
      displayedPick(dashFrame, {118.0, 120.0}, 0.0, SketchPickTargets::Curves);
  require(dashOn && dashOn->hit && dashGap && !dashGap->hit,
          "dash-gap picking diverged from the centralized stroke pattern");

  auto partialPreparedResult = prepareSketchScene(
      lineScene,
      SketchCurveLod::forMetresPerLogicalPixel(camera.metresPerLogicalPixel),
      {}, {}, narrowTiles);
  require(partialPreparedResult.has_value(),
          "partial-residency solid line was rejected");
  const auto partialSpans =
      partialPreparedResult.value()->primitiveTessellationIndex()->spans(
          *SketchPrimitiveHandle::create(1U));
  require(partialSpans.size() > 1U,
          "partial-residency fixture did not cross upload chunks");
  const auto &residentChunk = partialPreparedResult.value()
                                  ->mesh()
                                  ->chunks()[partialSpans.front().chunk];
  const auto residentIndices = residentChunk->indices();
  const auto residentVertices = residentChunk->vertices();
  const std::array residentTriangle{
      residentVertices[residentIndices[partialSpans.front().firstIndex]],
      residentVertices[residentIndices[partialSpans.front().firstIndex + 1U]],
      residentVertices[residentIndices[partialSpans.front().firstIndex + 2U]]};
  auto partialFrame = displayedFrame(*partialPreparedResult, camera, viewport,
                                     {partialSpans.front().chunk});
  QPointF residentCentroid;
  for (const SketchMeshVertex &vertex : residentTriangle)
    residentCentroid += projectedVertex(*partialFrame, vertex) / 3.0;
  auto residentPick = displayedPick(partialFrame, residentCentroid, 0.0,
                                    SketchPickTargets::Curves);
  const auto &missingChunk = partialPreparedResult.value()
                                 ->mesh()
                                 ->chunks()[partialSpans.back().chunk];
  const auto missingIndices = missingChunk->indices();
  const auto missingVertices = missingChunk->vertices();
  QPointF missingCentroid;
  for (std::size_t corner = 0U; corner < 3U; ++corner)
    missingCentroid +=
        projectedVertex(
            *partialFrame,
            missingVertices[missingIndices[partialSpans.back().firstIndex +
                                           corner]]) /
        3.0;
  auto missingPick = displayedPick(partialFrame, missingCentroid, 0.0,
                                   SketchPickTargets::Curves);
  require(residentPick && residentPick->hit && missingPick &&
              !missingPick->hit && residentPick->renderedSpanProbes > 0U &&
              residentPick->renderedTriangleTests > 0U &&
              residentPick->displayedDistanceLogicalPixels == 0.0,
          "partial multi-chunk residency leaked nonresident triangles");

  SketchPickCoveragePolicy spanBudget;
  spanBudget.maximumRenderedSpanProbes = 1U;
  auto spanBudgetFrame =
      displayedFrame(*partialPreparedResult, camera, viewport,
                     {partialSpans.back().chunk}, spanBudget);
  auto spanLimited = displayedPick(spanBudgetFrame, missingCentroid, 0.0,
                                   SketchPickTargets::Curves);
  require(!spanLimited &&
              spanLimited.error().code == "desktop.sketch.rendered-pick-budget",
          "nonresident span probes escaped their explicit pick budget");

  const std::vector<SketchStyle> rankingStyles{
      {SketchStyleRole::Regular, SketchLinePattern::Solid, 0.5F, 8.0F, 0U},
      {SketchStyleRole::Regular, SketchLinePattern::Solid, 6.0F, 8.0F, 5U}};
  auto rankingScene = fixedScene(
      stamp(56, 1, 56, 56, 56, 1), rankingStyles,
      {{-0.02, 0.0}, {0.02, 0.0}, {-0.02, 0.003}, {0.02, 0.003}},
      {{id<SketchEntityId>(561U), *SketchPrimitiveHandle::create(1U), 0U, 0U,
        SketchPrimitiveKind::Line, selectable, 0.0, 0.0, 0.0},
       {id<SketchEntityId>(562U), *SketchPrimitiveHandle::create(2U), 2U, 1U,
        SketchPrimitiveKind::Line, selectable, 0.0, 0.0, 0.0}});
  auto rankingPrepared = preparedScene(
      rankingScene,
      SketchCurveLod::forMetresPerLogicalPixel(camera.metresPerLogicalPixel));
  auto rankingFrame = displayedFrame(rankingPrepared, camera, viewport);
  const QPointF rankingQuery = rankingFrame->transform().toItem({0.0, 0.001});
  auto ranked =
      displayedPick(rankingFrame, rankingQuery, 1.0, SketchPickTargets::Curves);
  require(ranked && ranked->hit && ranked->hit->primitive.value() == 2U &&
              ranked->hit->distance > 0.001 &&
              ranked->displayedDistanceLogicalPixels == 0.0,
          "analytic distance outranked closer displayed stroke coverage");

  SketchTessellationOptions maximumCurve;
  maximumCurve.maximumArcStepRadians = 2.0 * std::numbers::pi;
  maximumCurve.minimumCircleSegments = 4096U;
  maximumCurve.maximumCurveSegments = 4096U;
  auto maximumCircleScene = fixedScene(
      stamp(57, 1, 57, 57, 57, 1), arcStyle, {{0.0, 0.0}},
      {{id<SketchEntityId>(571U), *SketchPrimitiveHandle::create(1U), 0U, 0U,
        SketchPrimitiveKind::Circle, selectable, 0.05, 0.0, 0.0}});
  auto maximumCirclePrepared = prepareSketchScene(
      maximumCircleScene,
      SketchCurveLod::forMetresPerLogicalPixel(camera.metresPerLogicalPixel),
      maximumCurve);
  require(maximumCirclePrepared.has_value(),
          "maximum-segment rendered circle was rejected");
  auto maximumCircleFrame =
      displayedFrame(*maximumCirclePrepared, camera, viewport);
  const std::array maximumCircleAngles{0.0, std::numbers::pi,
                                       2.0 * std::numbers::pi -
                                           2.0 * std::numbers::pi / 4096.0};
  for (const double angle : maximumCircleAngles) {
    const QPointF query = maximumCircleFrame->transform().toItem(
        {0.05 * std::cos(angle), 0.05 * std::sin(angle)});
    auto maximumCirclePick = displayedPick(maximumCircleFrame, query, 0.0,
                                           SketchPickTargets::Curves);
    require(
        maximumCirclePick && maximumCirclePick->hit &&
            maximumCirclePick->displayedDistanceLogicalPixels == 0.0 &&
            maximumCirclePick->renderedTriangleTests <=
                SketchPickCoveragePolicy::defaultMaximumRenderedTriangleTests,
        "one maximum-quality primitive exceeded default pick work");
  }

  SketchPickCoveragePolicy triangleBudget;
  triangleBudget.maximumRenderedTriangleTests = 1U;
  auto budgetFrame =
      displayedFrame(pointPrepared, camera, viewport, {}, triangleBudget);
  auto triangleLimited = displayedPick(
      budgetFrame, {160.0 + 5.9 * std::cos(std::numbers::pi), 120.0}, 0.0,
      SketchPickTargets::Points);
  require(!triangleLimited && triangleLimited.error().code ==
                                  "desktop.sketch.rendered-pick-budget",
          "rendered triangle budget returned a partial pick");

  SketchPickCoveragePolicy patternBudget;
  patternBudget.maximumPatternIntervals = 1U;
  auto patternBudgetFrame =
      displayedFrame(*dashPreparedResult, camera, viewport, {}, patternBudget);
  auto patternLimited = displayedPick(patternBudgetFrame, {125.0, 120.0}, 0.0,
                                      SketchPickTargets::Curves);
  require(!patternLimited && patternLimited.error().code ==
                                 "desktop.sketch.rendered-pick-budget",
          "rendered pattern budget returned a partial pick");

  auto coverageLimited = SketchPresentedChunkCoverage::create(
      *pointPrepared->mesh(), std::vector<std::uint32_t>{0U},
      sizeof(SketchPresentedChunkCoverage) - 1U);
  require(!coverageLimited && coverageLimited.error().code ==
                                  "desktop.sketch.pick-coverage-budget",
          "presented chunk coverage ignored its retained budget");

  measuredAllocations = 0U;
  measureAllocations = true;
  auto allocationFree =
      displayedPick(pointFrame, {160.0, 120.0}, 0.0, SketchPickTargets::Points);
  measureAllocations = false;
  require(allocationFree && allocationFree->hit && measuredAllocations == 0U,
          "rendered picking allocated inside its bounded query workspace");

  const SketchStyle extremePattern{SketchStyleRole::Construction,
                                   SketchLinePattern::Dashed,
                                   std::numeric_limits<float>::max(), 1.0F, 0U};
  const SketchStrokePattern boundedPattern = strokePattern(extremePattern);
  require(std::isfinite(boundedPattern.onLogicalPixels) &&
              std::isfinite(boundedPattern.periodLogicalPixels) &&
              boundedPattern.onLogicalPixels <=
                  boundedPattern.periodLogicalPixels,
          "extreme centralized stroke pattern overflowed");

  auto longPatternScene = fixedScene(
      stamp(58, 1, 58, 58, 58, 1), dashedStyle, {{0.0, 0.0}, {1.0e10, 0.0}},
      {{id<SketchEntityId>(581U), *SketchPrimitiveHandle::create(1U), 0U, 0U,
        SketchPrimitiveKind::Line, selectable, 0.0, 0.0, 0.0}});
  const SketchCamera2d tinyScale{2U, {}, 1.0e-30, 0.0};
  SketchUploadOptions longPatternUpload;
  longPatternUpload.spatialTileLogicalPixels = 1.0e30;
  auto longPatternPrepared = prepareSketchScene(
      longPatternScene,
      SketchCurveLod::forMetresPerLogicalPixel(tinyScale.metresPerLogicalPixel),
      {}, {}, longPatternUpload);
  require(longPatternPrepared.has_value(),
          "finite long patterned line was rejected during preparation");
  auto longPatternFrame =
      displayedFrame(*longPatternPrepared, tinyScale, viewport);
  auto longPatternPick = displayedPick(longPatternFrame, {160.0, 120.0}, 0.0,
                                       SketchPickTargets::Curves);
  require(!longPatternPick &&
              longPatternPick.error().code ==
                  "desktop.sketch.unrepresentable-pattern-phase",
          "unrepresentable shader-float dash phase reached exact picking");
}

void verifyGeneratedRenderedDifferential() {
  testkit::Random random{0x8f43c2d19aULL};
  constexpr auto selectable =
      SketchPrimitiveFlags::Visible | SketchPrimitiveFlags::Selectable;
  const QSizeF viewport{320.0, 240.0};
  SketchUploadOptions tiles;
  tiles.spatialTileLogicalPixels = 8.0;
  for (std::uint64_t iteration = 0U; iteration < 24U; ++iteration) {
    const bool dashed = (random.next() & 1U) != 0U;
    const SketchStyle style{SketchStyleRole::Regular,
                            dashed ? SketchLinePattern::Dashed
                                   : SketchLinePattern::Solid,
                            2.0F, 8.0F, 0U};
    const Point2d center{1.0e6 + static_cast<double>(iteration),
                         -1.0e6 + static_cast<double>(iteration)};
    auto generated = fixedScene(
        stamp(70U + iteration, 1, 70U + iteration, 70U + iteration,
              70U + iteration, 1),
        {style}, {{center.x - 0.05, center.y}, {center.x + 0.05, center.y}},
        {{id<SketchEntityId>(700U + iteration),
          *SketchPrimitiveHandle::create(1U), 0U, 0U, SketchPrimitiveKind::Line,
          selectable, 0.0, 0.0, 0.0}});
    const SketchCamera2d camera{
        2U, center, 0.001, random.between(-std::numbers::pi, std::numbers::pi)};
    auto prepared = prepareSketchScene(
        generated,
        SketchCurveLod::forMetresPerLogicalPixel(camera.metresPerLogicalPixel),
        {}, {}, tiles);
    require(prepared.has_value(), "generated rendered fixture was rejected");
    const auto spans = prepared.value()->primitiveTessellationIndex()->spans(
        *SketchPrimitiveHandle::create(1U));
    require(spans.size() > 1U,
            "generated rendered fixture lacked residency choices");
    const std::size_t chosen =
        static_cast<std::size_t>(random.next() % spans.size());
    const bool resident = (random.next() & 1U) != 0U;
    const std::uint32_t coverageChunk =
        resident ? spans[chosen].chunk
                 : spans[(chosen + 1U) % spans.size()].chunk;
    auto frame = displayedFrame(*prepared, camera, viewport, {coverageChunk});
    const auto &chunk = prepared.value()->mesh()->chunks()[spans[chosen].chunk];
    const auto indices = chunk->indices();
    const auto vertices = chunk->vertices();
    const std::size_t triangleCount = spans[chosen].indexCount / 3U;
    const std::size_t triangle = static_cast<std::size_t>(
        random.next() % std::max<std::size_t>(triangleCount, 1U));
    const std::size_t first = spans[chosen].firstIndex + triangle * 3U;
    QPointF query;
    float pathMetres = 0.0F;
    for (std::size_t corner = 0U; corner < 3U; ++corner) {
      const SketchMeshVertex &vertex = vertices[indices[first + corner]];
      query += projectedVertex(*frame, vertex) / 3.0;
      pathMetres += vertex.pathDistanceMetres / 3.0F;
    }
    bool patternedVisible = true;
    if (dashed) {
      const SketchStrokePattern pattern = strokePattern(style);
      const float logicalPath =
          pathMetres / static_cast<float>(camera.metresPerLogicalPixel);
      patternedVisible = std::fmod(logicalPath, pattern.periodLogicalPixels) <=
                         pattern.onLogicalPixels;
    }
    auto picked = displayedPick(frame, query, 0.0, SketchPickTargets::Curves);
    require(picked && static_cast<bool>(picked->hit) ==
                          (resident && patternedVisible),
            "generated shader-equivalent residency/style/transform oracle "
            "disagreed with rendered picking");
  }
}

void verifyProjectionEdgeGuards() {
  constexpr double extreme = std::numeric_limits<double>::max() * 0.75;
  auto transform = SketchViewTransform::create(
      {9, {extreme, extreme}, 1.0, 0.0}, {1920.0, 1080.0});
  require(transform.has_value(), "finite extreme camera was rejected early");
  const Point2d opposite{-extreme, -extreme};
  auto matrix = transform->itemMatrix(opposite);
  auto gpuView = transform->gpuView(opposite);
  require(!matrix &&
              matrix.error().code == "desktop.sketch.unrepresentable-matrix" &&
              !gpuView &&
              gpuView.error().code == "desktop.sketch.unrepresentable-gpu-view",
          "non-finite extreme camera/origin projection reached GPU state");

  auto handle = SketchPrimitiveHandle::create(1);
  require(handle.has_value(), "tiny-circle primitive handle failed");
  std::vector<PackedSketchPrimitive> primitives{
      {id<SketchEntityId>(101), *handle, 0, 0, SketchPrimitiveKind::Circle,
       SketchPrimitiveFlags::Visible | SketchPrimitiveFlags::Selectable,
       std::numeric_limits<double>::denorm_min(), 0.0, 0.0}};
  auto tiny = SketchSceneSnapshot::create(stamp(9, 1, 9, 9, 9, 1), styles(),
                                          {{0.0, 0.0}}, std::move(primitives));
  require(tiny.has_value(), "valid tiny canonical circle was rejected");
  auto projected = buildSketchSceneMesh(*tiny);
  require(!projected && projected.error().code ==
                            "desktop.sketch.unrepresentable-segment",
          "collapsed tessellation segment was normalized");
}

} // namespace

int main(int argc, char *argv[]) {
  try {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication application(argc, argv);
    SketchSceneItem item;
    require(item.flags().testFlag(QQuickItem::ItemHasContents),
            "sketch scene item does not own scene-graph content");
    const auto profile = kearne::testkit::propertyProfile();
    const auto preparationProfile = boundedPreparationProfile(profile);
    verifyBoundedReclaimer(profile);
    verifyTransforms(profile);
    verifyCameraState(profile);
    verifyPickCoverageState(profile);
    verifyPublication(preparationProfile);
    verifySpatialChunksAndProgression();
    verifyGeneratedSpatialChunkProperties(profile);
    verifyBoundedRetargeting();
    verifyResizeAndPicking(preparationProfile);
    verifyPreparationCancellation(preparationProfile);
    verifyNeutralStrokeKernel();
    verifyMeshGeneration(preparationProfile);
    verifyPrimitiveSemanticIdentity();
    verifyLodReusesScenePicking();
    verifyRetainedGeometry();
    verifyLargeCoordinatePrecision();
    verifyExactRenderedPicking();
    verifyGeneratedRenderedDifferential();
    verifyProjectionEdgeGuards();
    std::cout << "verified " << profile.iterations << " cheap and "
              << preparationProfile.iterations
              << " preparation cases per sketch scene adapter property\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
