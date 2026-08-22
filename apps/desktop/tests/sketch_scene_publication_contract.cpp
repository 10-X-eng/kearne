#include "sketch_scene_fixture.hpp"
#include "sketch_scene_publication.hpp"

#include <kearne/testkit/property.hpp>

#include <QCoreApplication>
#include <QEventLoop>
#include <QGuiApplication>
#include <QMetaObject>
#include <QThread>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <source_location>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace kearne;
using namespace kearne::render;
using namespace kearne::ui;
using namespace kearne::ui::test;

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

template <typename Predicate>
void pumpUntil(Predicate predicate,
               std::chrono::seconds timeout = std::chrono::seconds{10},
               std::source_location caller = std::source_location::current()) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (!predicate() && std::chrono::steady_clock::now() < deadline) {
    QCoreApplication::processEvents(QEventLoop::AllEvents);
    std::this_thread::yield();
  }
  if (!predicate())
    throw std::runtime_error(
        "asynchronous sketch preparation did not converge at line " +
        std::to_string(caller.line()));
}

template <typename Predicate>
void waitWithoutEvents(
    Predicate predicate,
    std::chrono::seconds timeout = std::chrono::seconds{10},
    std::source_location caller = std::source_location::current()) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (!predicate() && std::chrono::steady_clock::now() < deadline)
    std::this_thread::yield();
  if (!predicate())
    throw std::runtime_error(
        "background sketch preparation did not converge at line " +
        std::to_string(caller.line()));
}

SketchProductStamp productStamp(const SceneTarget &target,
                                std::uint64_t generation,
                                std::uint64_t payload) {
  auto version = SketchProductGeneration::create(generation);
  require(version.has_value(), "generated product generation was invalid");
  return {target, *version, digest<SketchProductDigest>(payload)};
}

SketchSceneProducts products(
    std::shared_ptr<const SketchSceneSnapshot> base, std::uint64_t generation,
    std::uint64_t payload,
    std::shared_ptr<const SketchProvisionalGeometry> provisionalGeometry = {}) {
  return {productStamp(base->stamp().target, generation, payload),
          std::move(base),
          {},
          std::move(provisionalGeometry),
          {}};
}

std::shared_ptr<const SketchSceneProducts>
ownedProducts(SketchProductStamp stamp,
              std::shared_ptr<const SketchSceneSnapshot> base) {
  return std::make_shared<const SketchSceneProducts>(
      SketchSceneProducts{std::move(stamp), std::move(base), {}, {}, {}});
}

void shutdownController(SketchScenePublicationController &controller,
                        const char *failureMessage) {
  std::optional<Diagnostic> terminalFailure;
  pumpUntil([&] {
    auto stopped = controller.shutdown();
    if (stopped)
      return true;
    if (stopped.error().code == "desktop.sketch.preparation-backpressure") {
      require(!controller.isShutdown() && controller.metrics().subscribed,
              "shutdown backpressure changed controller ownership");
      return false;
    }
    terminalFailure = std::move(stopped.error());
    return true;
  });
  require(!terminalFailure, failureMessage);
}

std::shared_ptr<const SketchProvisionalGeometry>
provisional(const SceneStamp &base, std::uint64_t generation) {
  auto edit = SketchEditSessionHandle::create(1);
  auto tool = SketchToolInstanceHandle::create(1);
  auto version = SketchProvisionalGeneration::create(generation);
  auto handle = SketchProvisionalPrimitiveHandle::create(1);
  require(edit && tool && version && handle,
          "generated provisional identity was invalid");
  const SketchProvisionalStamp provisionalStamp{
      {base, *edit, *tool},
      *version,
      digest<SketchProvisionalDigest>(generation)};
  const std::vector<PackedSketchProvisionalPrimitive> primitives{{
      *handle,
      std::array<Point2d, 2>{{{0.0, 0.0}, {0.0, 0.0}}},
      1,
      SketchPrimitiveKind::Point,
      SketchProvisionalClassification::Regular,
      0.0,
      0.0,
      0.0,
  }};
  auto created =
      SketchProvisionalGeometry::create(provisionalStamp, primitives);
  require(created.has_value(), "generated provisional geometry was invalid");
  return std::move(*created);
}

Result<std::shared_ptr<const PreparedSketchProducts>>
failedPreparation(const SketchPreparationRequest &,
                  std::shared_ptr<const PreparedSketchProducts>,
                  std::stop_token) {
  return std::unexpected(
      diagnostic("desktop.test.preparation", "generated preparation result"));
}

void verifyProductGenerationInvariant() {
  const auto generation = SketchProductGeneration::create(0);
  require(!generation && generation.error().code ==
                             "desktop.sketch.product-generation-zero",
          "zero product generation was accepted");
}

void verifyCompletePreparedPacket() {
  const SceneStamp baseStamp = stamp(240, 1, 240, 240, 240, 1);
  auto base = scene(32, 240, baseStamp);

  constexpr std::array roles{
      SketchOverlayRole::Hovered, SketchOverlayRole::Selected,
      SketchOverlayRole::Preview, SketchOverlayRole::Diagnostic};
  std::array<SketchOverlayRoleSetPtr, 4> roleSets;
  for (std::size_t index = 0U; index < roleSets.size(); ++index) {
    const std::array<SketchOverlayScope, 1> selected{{
        {base->primitives().front().entity, sketch::PointKey::Point},
    }};
    const auto scopes = roles[index] == SketchOverlayRole::Selected
                            ? std::span<const SketchOverlayScope>{selected}
                            : std::span<const SketchOverlayScope>{};
    auto created = SketchOverlayRoleSet::create(base, roles[index], scopes);
    require(created.has_value(), "complete packet overlay role was invalid");
    roleSets[index] = std::move(*created);
  }
  auto overlayGeneration = SketchPresentationGeneration::create(1U);
  require(overlayGeneration.has_value(),
          "complete packet overlay generation was invalid");
  auto overlay =
      SketchPresentationOverlay::create(base, *overlayGeneration, roleSets);
  require(overlay.has_value(), "complete packet overlay was invalid");

  auto draft = provisional(baseStamp, 1U);
  auto edit = SketchEditSessionHandle::create(1U);
  auto tool = SketchToolInstanceHandle::create(1U);
  auto markerGeneration = SketchMarkerGeneration::create(1U);
  auto markerView = SketchMarkerViewGeneration::create(1U);
  auto markerHandle = SketchMarkerHandle::create(1U);
  auto controlSegmentHandle = SketchMarkerHandle::create(2U);
  auto controlPoleHandle = SketchMarkerHandle::create(3U);
  auto curvatureHandle = SketchMarkerHandle::create(4U);
  auto labelHandle = SketchMarkerHandle::create(5U);
  auto dimensionHandle = SketchMarkerHandle::create(6U);
  auto draftHandle = SketchProvisionalPrimitiveHandle::create(1U);
  require(edit && tool && markerGeneration && markerView && markerHandle &&
              controlSegmentHandle && controlPoleHandle && curvatureHandle &&
              labelHandle && dimensionHandle && draftHandle,
          "complete packet marker identity was invalid");
  const SketchMarkerStamp markerStamp{
      {baseStamp, SketchMarkerInteraction{*edit, *tool},
       SketchProvisionalReference{draft->stamp().generation,
                                  draft->stamp().payload},
       *markerView},
      *markerGeneration,
      digest<SketchMarkerDigest>(1U)};
  const std::array<SketchMarkerAnchor, 9> anchors{
      SketchProvisionalMarkerAnchor{
          *draftHandle, SketchMarkerPointLocation{sketch::PointKey::Point}},
      SketchCanonicalMarkerAnchor{{0.01, 0.02}},
      SketchCanonicalMarkerAnchor{{0.03, 0.04}},
      SketchCanonicalMarkerAnchor{{0.03, 0.04}},
      SketchCanonicalMarkerAnchor{{0.01, 0.02}},
      SketchCanonicalMarkerAnchor{{0.01, 0.05}},
      SketchCanonicalMarkerAnchor{{0.02, 0.03}},
      SketchCanonicalMarkerAnchor{{-0.025, -0.01}},
      SketchCanonicalMarkerAnchor{{0.025, -0.01}},
  };
  const std::array<PackedSketchMarker, 6> markerValues{{
      {*markerHandle, std::nullopt, 0U, 1U, SketchMarkerKind::EndpointSnap,
       0.0},
      {*controlSegmentHandle, std::nullopt, 1U, 2U,
       SketchMarkerKind::SplineControlSegment, 0.0},
      {*controlPoleHandle, std::nullopt, 3U, 1U,
       SketchMarkerKind::SplineControlPole, 0.0},
      {*curvatureHandle, std::nullopt, 4U, 2U,
       SketchMarkerKind::SplineCurvatureSegment, 0.0},
      {*labelHandle, std::nullopt, 6U, 1U, SketchMarkerKind::SplineDegreeLabel,
       3.0},
      {*dimensionHandle, id<SketchConstraintId>(26U), 7U, 2U,
       SketchMarkerKind::HorizontalDistanceDimension, 0.05},
  }};
  auto markers = SketchMarkerPacket::create(markerStamp, base, draft, anchors,
                                            markerValues);
  require(markers.has_value(), "complete packet markers were invalid");

  auto source = std::make_shared<const SketchSceneProducts>(
      SketchSceneProducts{productStamp(baseStamp.target, 1U, 240U), base,
                          *overlay, draft, *markers});
  auto prepared = prepareSketchProducts(source);
  require(prepared && (*prepared)->source() == source &&
              (*prepared)->base()->scene() == base &&
              (*prepared)->overlay()->source() == *overlay &&
              (*prepared)->provisional()->source() == draft &&
              (*prepared)->markers()->source() == *markers &&
              (*prepared)->overlayPointPacket() &&
              (*prepared)->markerPacket() &&
              (*prepared)->markerProvenance().size() == markerValues.size() &&
              !(*prepared)->provisional()->provenance().empty() &&
              (*prepared)->metrics().overlayPointPacketRetainedBytes ==
                  (*prepared)->overlayPointPacket()->metrics().retainedBytes &&
              (*prepared)->metrics().markerPacketRetainedBytes ==
                  (*prepared)->markerPacket()->metrics().retainedBytes &&
              (*prepared)->metrics().totalRetainedBytes ==
                  sizeof(PreparedSketchProducts) +
                      (*prepared)->metrics().baseRetainedBytes +
                      (*prepared)->metrics().overlayRetainedBytes +
                      (*prepared)->metrics().overlayPointPacketRetainedBytes +
                      (*prepared)->metrics().provisionalRetainedBytes +
                      (*prepared)->metrics().markerRetainedBytes +
                      (*prepared)->metrics().markerPacketRetainedBytes +
                      (*prepared)->metrics().markerProvenanceRetainedBytes,
          "complete product packet lost an exact prepared component");
  std::array<bool, 5> markerVectorKinds{};
  for (const auto &chunk : (*prepared)->markerPacket()->chunks())
    for (const SketchVectorRecord &record : chunk->records()) {
      if (record.meta[0] == static_cast<std::uint32_t>(SketchVectorKind::Glyph))
        markerVectorKinds[0] = true;
      if (record.meta[0] == static_cast<std::uint32_t>(SketchVectorKind::Line))
        markerVectorKinds[1] = true;
      if (record.meta[0] == static_cast<std::uint32_t>(SketchVectorKind::Point))
        markerVectorKinds[2] = true;
      if (record.meta[0] == static_cast<std::uint32_t>(SketchVectorKind::Text))
        markerVectorKinds[3] = true;
      if (record.meta[0] ==
          static_cast<std::uint32_t>(SketchVectorKind::Dimension)) {
        markerVectorKinds[4] = true;
        const auto data = chunk->data();
        require(record.meta[1] + 1U < data.size() &&
                    data[record.meta[1] + 1U].value[0] == 53.0F &&
                    data[record.meta[1] + 1U].value[1] == 48.0F,
                "millimetre dimension text was not formatted as 50");
      }
    }
  require(std::ranges::all_of(markerVectorKinds,
                              [](bool present) { return present; }),
          "marker packet did not retain glyph, guide, pole, text, and "
          "dimension vectors");

  auto nextSource = std::make_shared<const SketchSceneProducts>(
      SketchSceneProducts{productStamp(baseStamp.target, 2U, 241U), base,
                          *overlay, draft, *markers});
  auto reused = prepareSketchProducts(nextSource, {}, *prepared);
  require(reused && (*reused)->source() == nextSource &&
              (*reused)->base() == (*prepared)->base() &&
              (*reused)->overlay() == (*prepared)->overlay() &&
              (*reused)->provisional() == (*prepared)->provisional() &&
              (*reused)->markers() == (*prepared)->markers() &&
              (*reused)->overlayPointPacket() ==
                  (*prepared)->overlayPointPacket() &&
              (*reused)->markerPacket() == (*prepared)->markerPacket() &&
              (*reused)->markerProvenance().data() ==
                  (*prepared)->markerProvenance().data(),
          "same-component product update rebuilt immutable preparation");

  auto inchSource = std::make_shared<const SketchSceneProducts>(
      SketchSceneProducts{productStamp(baseStamp.target, 3U, 242U),
                          base,
                          *overlay,
                          draft,
                          *markers,
                          {SketchLengthDisplayUnit::Inch}});
  auto inchPrepared = prepareSketchProducts(inchSource, {}, *reused);
  require(inchPrepared && (*inchPrepared)->base() == (*reused)->base() &&
              (*inchPrepared)->overlay() == (*reused)->overlay() &&
              (*inchPrepared)->provisional() == (*reused)->provisional() &&
              (*inchPrepared)->markers() != (*reused)->markers() &&
              (*inchPrepared)->markerPacket() != (*reused)->markerPacket(),
          "unit-only dimension presentation rebuilt geometry or reused stale "
          "annotation text");
  auto emphasizedSource = std::make_shared<const SketchSceneProducts>(
      SketchSceneProducts{productStamp(baseStamp.target, 4U, 243U),
                          base,
                          *overlay,
                          draft,
                          *markers,
                          {SketchLengthDisplayUnit::Inch},
                          {dimensionHandle->value(), markerHandle->value()}});
  auto emphasized = prepareSketchProducts(emphasizedSource, {}, *inchPrepared);
  require(emphasized &&
              (*emphasized)->markerPacket() ==
                  (*inchPrepared)->markerPacket() &&
              (*emphasized)->markerProvenance().data() ==
                  (*inchPrepared)->markerProvenance().data() &&
              (*emphasized)->markers() == (*inchPrepared)->markers(),
          "hover or selection emphasis rebuilt immutable marker geometry");

  auto incomplete = PreparedSketchProducts::create(
      nextSource, (*prepared)->base(), (*prepared)->overlay(),
      (*prepared)->provisional());
  require(!incomplete &&
              incomplete.error().code == "desktop.sketch.products-mismatch",
          "prepared packet accepted a missing declared component");
  std::stop_source cancellation;
  cancellation.request_stop();
  auto cancelled = prepareSketchProducts(nextSource, {}, *prepared,
                                         cancellation.get_token());
  require(!cancelled &&
              cancelled.error().code == "desktop.sketch.preparation-cancelled",
          "pre-cancelled complete packet preparation performed work");
}

class RecordingSink final : public QObject, public SketchPreparationSink {
public:
  void deliver(const SketchPreparationCompletionView &completion) override {
    require(QThread::currentThread() == thread(),
            "preparation callback escaped the UI thread");
    if (lastSubscription_ == completion.subscription)
      require(completion.epoch.value() > lastEpoch_,
              "preparation epoch did not advance");
    lastSubscription_ = completion.subscription;
    lastEpoch_ = completion.epoch.value();
    lastProduct_ = completion.product;
    if (!completion.prepared)
      lastErrorCode_ = completion.prepared.error().code;
    else
      lastErrorCode_.reset();
    ++deliveries_;
  }

  [[nodiscard]] std::uint64_t deliveries() const { return deliveries_; }
  [[nodiscard]] std::uint64_t lastEpoch() const { return lastEpoch_; }
  [[nodiscard]] const std::optional<SketchProductStamp> &lastProduct() const {
    return lastProduct_;
  }
  [[nodiscard]] const std::optional<std::string> &lastErrorCode() const {
    return lastErrorCode_;
  }

private:
  std::optional<SketchPreparationSubscription> lastSubscription_;
  std::optional<SketchProductStamp> lastProduct_;
  std::optional<std::string> lastErrorCode_;
  std::uint64_t lastEpoch_ = 0;
  std::uint64_t deliveries_ = 0;
};

class AtomicRecordingSink final : public QObject, public SketchPreparationSink {
public:
  explicit AtomicRecordingSink(std::atomic_uint64_t &deliveries)
      : deliveries_(deliveries) {}

  void deliver(const SketchPreparationCompletionView &) override {
    deliveries_.fetch_add(1, std::memory_order_relaxed);
  }

private:
  std::atomic_uint64_t &deliveries_;
};

class ThrowingSink final : public QObject, public SketchPreparationSink {
public:
  void deliver(const SketchPreparationCompletionView &) override {
    ++deliveries_;
    throw std::runtime_error("generated sink failure");
  }

  [[nodiscard]] std::uint64_t deliveries() const { return deliveries_; }

private:
  std::uint64_t deliveries_ = 0;
};

class ReentrantSink final : public QObject, public SketchPreparationSink {
public:
  explicit ReentrantSink(SketchPreparationExecutor &executor)
      : executor_(executor) {}

  void setSubscription(SketchPreparationSubscription subscription) {
    subscription_ = subscription;
  }

  void deliver(const SketchPreparationCompletionView &) override {
    ++deliveries_;
    unsubscribed_ =
        subscription_ && executor_.unsubscribe(*subscription_).has_value();
    executor_.requestShutdown();
    waitRejected_ = !executor_.waitUntilDrained(std::chrono::milliseconds{0});
    executor_.join();
    throw std::runtime_error("generated reentrant sink failure");
  }

  [[nodiscard]] bool contained() const {
    return deliveries_ == 1U && unsubscribed_ && waitRejected_;
  }

private:
  SketchPreparationExecutor &executor_;
  std::optional<SketchPreparationSubscription> subscription_;
  std::uint64_t deliveries_ = 0;
  bool unsubscribed_ = false;
  bool waitRejected_ = false;
};

[[nodiscard]] bool
preparationMetricsReconcile(const SketchPreparationExecutorMetrics &metrics) {
  return metrics.started == metrics.completed + metrics.failed +
                                metrics.cancelled + metrics.stale +
                                metrics.activePreparations;
}

void verifyThrowingAndReentrantSinks() {
  const SceneStamp baseStamp = stamp(220, 1, 220, 220, 220, 1);
  auto base = scene(1, 220, baseStamp);
  {
    SketchPreparationExecutor executor{{1, 1, 1}, failedPreparation};
    ThrowingSink sink;
    auto subscription = executor.subscribe(sink, sink);
    require(subscription.has_value(), "throwing sink subscription failed");
    for (std::uint64_t generation = 1; generation <= 2; ++generation) {
      require(executor
                  .submit(*subscription,
                          ownedProducts(productStamp(baseStamp.target,
                                                     generation, generation),
                                        base),
                          {})
                  .has_value(),
              "throwing sink submission failed");
      pumpUntil([&] {
        const auto metrics = executor.metrics();
        return sink.deliveries() == generation &&
               metrics.pendingCompletions == 0U &&
               metrics.activePreparations == 0U;
      });
    }
    const auto metrics = executor.metrics();
    require(metrics.deliveryFailures == 2U && metrics.delivered == 0U &&
                preparationMetricsReconcile(metrics),
            "throwing sink escaped cleanup or metric reconciliation");
    require(executor.unsubscribe(*subscription).has_value(),
            "throwing sink unsubscribe failed");
    executor.requestShutdown();
    require(executor.waitUntilDrained(std::chrono::seconds{5}),
            "throwing sink executor did not drain");
    executor.join();
  }

  {
    SketchPreparationExecutor executor{{1, 1, 1}, failedPreparation};
    ReentrantSink sink{executor};
    auto subscription = executor.subscribe(sink, sink);
    require(subscription.has_value(), "reentrant sink subscription failed");
    sink.setSubscription(*subscription);
    require(
        executor
            .submit(*subscription,
                    ownedProducts(productStamp(baseStamp.target, 1, 1), base),
                    {})
            .has_value(),
        "reentrant sink submission failed");
    pumpUntil([&] {
      const auto metrics = executor.metrics();
      return metrics.leasedSubscriptions == 0U &&
             metrics.pendingCompletions == 0U;
    });
    const auto metrics = executor.metrics();
    require(sink.contained() && metrics.deliveryFailures == 1U &&
                metrics.lifecycleRejections == 2U &&
                preparationMetricsReconcile(metrics),
            "reentrant sink escaped its lifecycle boundary");
    require(executor.waitUntilDrained(std::chrono::seconds{5}),
            "reentrant sink executor did not drain");
    executor.join();
  }
}

void verifyStaleDeliveryCannotConsumeReplacement() {
  std::mutex latchMutex;
  std::condition_variable latch;
  bool secondEntered = false;
  bool releaseSecond = false;
  std::atomic_bool boundaryObserved = false;
  std::atomic_bool secondStartedAtBoundary = false;
  const auto prepare = [&](const SketchPreparationRequest &request,
                           std::shared_ptr<const PreparedSketchProducts>,
                           std::stop_token stop)
      -> Result<std::shared_ptr<const PreparedSketchProducts>> {
    if (request.epoch.value() == 2U) {
      std::stop_callback wakeOnStop{stop, [&] { latch.notify_all(); }};
      std::unique_lock lock{latchMutex};
      secondEntered = true;
      latch.notify_all();
      latch.wait(lock, [&] { return releaseSecond || stop.stop_requested(); });
    }
    return std::unexpected(
        diagnostic("desktop.test.preparation", "generated preparation result"));
  };
  const auto fault = [&](SketchPreparationExecutor::FaultSite site) {
    if (site != SketchPreparationExecutor::FaultSite::DeliveryBoundary)
      return;
    bool secondStarted = false;
    {
      std::scoped_lock lock{latchMutex};
      secondStarted = secondEntered;
      releaseSecond = true;
    }
    boundaryObserved.store(true, std::memory_order_release);
    secondStartedAtBoundary.store(secondStarted, std::memory_order_release);
    latch.notify_all();
  };

  SketchPreparationExecutor executor{{1, 1, 1}, prepare, fault};
  RecordingSink sink;
  auto subscription = executor.subscribe(sink, sink);
  require(subscription.has_value(), "ABA subscription failed");
  const SceneStamp baseStamp = stamp(230, 1, 230, 230, 230, 1);
  auto base = scene(1, 230, baseStamp);
  require(executor
              .submit(*subscription,
                      ownedProducts(productStamp(baseStamp.target, 1, 1), base),
                      {})
              .has_value(),
          "first ABA submission failed");
  waitWithoutEvents(
      [&] { return executor.metrics().pendingCompletions == 1U; });
  require(executor
              .submit(*subscription,
                      ownedProducts(productStamp(baseStamp.target, 2, 2), base),
                      {})
              .has_value(),
          "replacement ABA submission failed");
  require(executor.metrics().pendingPreparations == 1U,
          "queued completion did not serialize its replacement");

  QCoreApplication::processEvents(QEventLoop::AllEvents);
  pumpUntil([&] {
    const auto metrics = executor.metrics();
    return sink.deliveries() == 1U && metrics.pendingCompletions == 0U &&
           metrics.activePreparations == 0U;
  });
  require(boundaryObserved.load(std::memory_order_acquire) &&
              !secondStartedAtBoundary.load(std::memory_order_acquire) &&
              sink.lastEpoch() == 2U,
          "stale delivery consumed its in-place replacement");
  require(executor.unsubscribe(*subscription).has_value(),
          "ABA unsubscribe failed");
  executor.requestShutdown();
  require(executor.waitUntilDrained(std::chrono::seconds{5}),
          "ABA executor did not drain");
  executor.join();
}

void verifyInternalFaultContainment() {
  std::atomic_uint64_t starts = 0;
  std::atomic_uint64_t completions = 0;
  const auto fault = [&](SketchPreparationExecutor::FaultSite site) {
    if (site == SketchPreparationExecutor::FaultSite::Start &&
        starts.fetch_add(1, std::memory_order_relaxed) == 0U)
      throw std::bad_alloc{};
    if (site == SketchPreparationExecutor::FaultSite::Completion &&
        completions.fetch_add(1, std::memory_order_relaxed) == 0U)
      throw std::bad_alloc{};
  };
  const auto exact = [](const SketchPreparationRequest &request,
                        std::shared_ptr<const PreparedSketchProducts> reuse,
                        std::stop_token stop) {
    return prepareSketchProducts(request.products, request.options,
                                 std::move(reuse), stop);
  };
  SketchPreparationExecutor executor{{1, 1, 1}, exact, fault};
  RecordingSink sink;
  auto subscription = executor.subscribe(sink, sink);
  require(subscription.has_value(), "fault subscription failed");
  const SceneStamp baseStamp = stamp(221, 1, 221, 221, 221, 1);
  auto base = scene(1, 221, baseStamp);
  const std::array<const char *, 3> errors{
      "desktop.sketch.preparation-start-failed",
      "desktop.sketch.preparation-completion-failed", nullptr};
  for (std::uint64_t generation = 1; generation <= errors.size();
       ++generation) {
    require(executor
                .submit(*subscription,
                        ownedProducts(productStamp(baseStamp.target, generation,
                                                   generation),
                                      base),
                        {})
                .has_value(),
            "fault submission failed");
    pumpUntil([&] {
      const auto metrics = executor.metrics();
      return sink.deliveries() == generation &&
             metrics.pendingCompletions == 0U &&
             metrics.activePreparations == 0U;
    });
    if (errors[generation - 1U])
      require(sink.lastErrorCode() &&
                  *sink.lastErrorCode() == errors[generation - 1U],
              "internal fault returned the wrong diagnostic");
    else
      require(!sink.lastErrorCode(),
              "executor did not recover after internal faults");
  }
  const auto metrics = executor.metrics();
  require(metrics.startFailures == 1U && metrics.started == 2U &&
              metrics.failed == 1U && metrics.completed == 1U &&
              preparationMetricsReconcile(metrics),
          "internal fault metrics did not reconcile");
  require(executor.unsubscribe(*subscription).has_value(),
          "fault unsubscribe failed");
  executor.requestShutdown();
  require(executor.waitUntilDrained(std::chrono::seconds{5}),
          "fault executor did not drain");
  executor.join();
}

void verifyPreparedResultValidation() {
  const auto adversarial = [](const SketchPreparationRequest &request,
                              std::shared_ptr<const PreparedSketchProducts>,
                              std::stop_token stop)
      -> Result<std::shared_ptr<const PreparedSketchProducts>> {
    switch (request.products->stamp.generation.value()) {
    case 1:
      return std::shared_ptr<const PreparedSketchProducts>{};
    case 2: {
      auto twin = scene(1, 922, request.products->scene->stamp());
      auto source = ownedProducts(request.products->stamp, std::move(twin));
      return prepareSketchProducts(std::move(source), request.options, {},
                                   stop);
    }
    case 3: {
      auto wrong = scene(1, 923, stamp(923, 1, 923, 923, 923, 1));
      const SceneTarget wrongTarget = wrong->stamp().target;
      auto source = ownedProducts(
          productStamp(wrongTarget, request.products->stamp.generation.value(),
                       923U),
          std::move(wrong));
      return prepareSketchProducts(std::move(source), request.options, {},
                                   stop);
    }
    case 4: {
      auto generation = SceneGeneration::create(
          request.products->scene->stamp().generation.value() + 1U);
      if (!generation)
        return std::unexpected(std::move(generation.error()));
      SceneStamp changed{request.products->scene->stamp().target, *generation,
                         digest<SceneDigest>(924)};
      auto wrong = scene(1, 924, std::move(changed));
      auto source = ownedProducts(request.products->stamp, std::move(wrong));
      return prepareSketchProducts(std::move(source), request.options, {},
                                   stop);
    }
    case 5: {
      SketchProductPreparationOptions wrong = request.options;
      ++wrong.picking.maximumLeafTargets;
      return prepareSketchProducts(request.products, wrong, {}, stop);
    }
    case 7:
      return std::unexpected(
          diagnostic("desktop.sketch.preparation-cancelled",
                     "generated cancellation without a stop request"));
    default:
      return prepareSketchProducts(request.products, request.options, {}, stop);
    }
  };
  SketchPreparationExecutor executor{{1, 1, 1}, adversarial};
  RecordingSink sink;
  auto subscription = executor.subscribe(sink, sink);
  require(subscription.has_value(), "validator subscription failed");
  const SceneStamp baseStamp = stamp(222, 1, 222, 222, 222, 1);
  auto base = scene(1, 222, baseStamp);
  const std::array<const char *, 7> expected{
      "desktop.sketch.preparation-null-result",
      "desktop.sketch.preparation-result-instance",
      "desktop.sketch.preparation-result-stamp",
      "desktop.sketch.preparation-result-instance",
      "desktop.sketch.preparation-result-options",
      nullptr,
      "desktop.sketch.preparation-cancelled"};
  for (std::uint64_t generation = 1; generation <= expected.size();
       ++generation) {
    require(executor
                .submit(*subscription,
                        ownedProducts(productStamp(baseStamp.target, generation,
                                                   generation),
                                      base),
                        {})
                .has_value(),
            "validator submission failed");
    pumpUntil([&] {
      const auto metrics = executor.metrics();
      return sink.deliveries() == generation &&
             metrics.pendingCompletions == 0U &&
             metrics.activePreparations == 0U;
    });
    if (expected[generation - 1U])
      require(sink.lastErrorCode() &&
                  *sink.lastErrorCode() == expected[generation - 1U],
              "invalid prepared result returned the wrong diagnostic");
    else
      require(!sink.lastErrorCode(), "exact prepared result was rejected");
  }
  const auto metrics = executor.metrics();
  require(metrics.started == 7U && metrics.failed == 6U &&
              metrics.cancelled == 0U && metrics.completed == 1U &&
              preparationMetricsReconcile(metrics),
          "prepared-result validation metrics did not reconcile");
  require(executor.unsubscribe(*subscription).has_value(),
          "validator unsubscribe failed");
  executor.requestShutdown();
  require(executor.waitUntilDrained(std::chrono::seconds{5}),
          "validator executor did not drain");
  executor.join();
}

void verifyPrepareLifecycleRejection() {
  SketchPreparationExecutor *executorAddress = nullptr;
  std::atomic_bool waitRejected = false;
  const auto reentrant =
      [&](const SketchPreparationRequest &request,
          std::shared_ptr<const PreparedSketchProducts> reuse,
          std::stop_token stop) {
        executorAddress->requestShutdown();
        waitRejected.store(
            !executorAddress->waitUntilDrained(std::chrono::milliseconds{0}),
            std::memory_order_release);
        executorAddress->join();
        return prepareSketchProducts(request.products, request.options,
                                     std::move(reuse), stop);
      };
  SketchPreparationExecutor executor{{1, 1, 1}, reentrant};
  executorAddress = &executor;
  RecordingSink sink;
  auto subscription = executor.subscribe(sink, sink);
  require(subscription.has_value(), "lifecycle subscription failed");
  const SceneStamp baseStamp = stamp(223, 1, 223, 223, 223, 1);
  auto base = scene(1, 223, baseStamp);
  require(executor
              .submit(*subscription,
                      ownedProducts(productStamp(baseStamp.target, 1, 1), base),
                      {})
              .has_value(),
          "lifecycle submission failed");
  pumpUntil([&] {
    const auto metrics = executor.metrics();
    return sink.deliveries() == 1U && metrics.pendingCompletions == 0U;
  });
  const auto metrics = executor.metrics();
  require(waitRejected.load(std::memory_order_acquire) &&
              metrics.lifecycleRejections == 3U && !executor.isStopping() &&
              preparationMetricsReconcile(metrics),
          "worker lifecycle calls crossed the UI ownership boundary");
  require(executor.unsubscribe(*subscription).has_value(),
          "lifecycle unsubscribe failed");
  executor.requestShutdown();
  require(executor.waitUntilDrained(std::chrono::seconds{5}),
          "lifecycle executor did not drain");
  executor.join();
}

void verifyExecutorLifetimeObservation() {
  SketchSceneItem item;
  auto executor = std::make_unique<SketchPreparationExecutor>(
      SketchPreparationLimits{1, 1, 1}, failedPreparation);
  auto controller =
      std::make_unique<SketchScenePublicationController>(item, *executor);
  const SceneStamp baseStamp = stamp(224, 1, 224, 224, 224, 1);
  auto base = scene(1, 224, baseStamp);
  require(controller->retarget(baseStamp.target).has_value(),
          "observed executor target failed");
  executor.reset();
  require(!controller->metrics().subscribed,
          "controller retained a destroyed executor subscription");
  auto published = controller->publishProducts(products(base, 1, 1));
  require(!published && published.error().code ==
                            "desktop.sketch.publication-executor-destroyed",
          "controller dereferenced or masked its destroyed executor");
  controller.reset();
}

void verifySinkLifetimeIdentity() {
  SketchPreparationExecutor executor{{1, 1, 1}, failedPreparation};
  QObject unrelatedLifetime;
  RecordingSink sink;
  auto subscription = executor.subscribe(unrelatedLifetime, sink);
  require(!subscription &&
              subscription.error().code ==
                  "desktop.sketch.preparation-sink-lifetime" &&
              executor.metrics().leasedSubscriptions == 0U,
          "executor accepted a sink outside its observed QObject lifetime");
  executor.requestShutdown();
  require(executor.waitUntilDrained(std::chrono::seconds{5}),
          "sink-lifetime executor did not drain");
  executor.join();
}

void verifyControllerShutdownAffinity() {
  SketchPreparationExecutor executor{{1, 1, 1}};
  SketchSceneItem item;
  SketchScenePublicationController controller{item, executor};
  std::optional<Diagnostic> wrongThreadError;
  std::thread wrongThread([&] {
    auto stopped = controller.shutdown();
    if (!stopped)
      wrongThreadError = std::move(stopped.error());
  });
  wrongThread.join();
  require(wrongThreadError &&
              wrongThreadError->code == "desktop.sketch.publication-thread" &&
              !controller.isShutdown() && controller.metrics().subscribed,
          "wrong-thread shutdown changed controller ownership");

  const SceneStamp baseStamp = stamp(231, 1, 231, 231, 231, 1);
  auto base = scene(1, 231, baseStamp);
  require(controller.retarget(baseStamp.target).has_value(),
          "shutdown-affinity target failed");
  require(controller.publishProducts(products(base, 1, 1)).has_value(),
          "controller did not recover after rejected shutdown");
  pumpUntil([&] { return controller.metrics().itemPublications == 1U; });
  shutdownController(controller, "owner-thread controller shutdown failed");
  executor.requestShutdown();
  require(executor.waitUntilDrained(std::chrono::seconds{5}),
          "shutdown-affinity executor did not drain");
  executor.join();
}

void verifyControllerGlobalGenerationClock() {
  SketchPreparationExecutor executor{{1, 1, 2}};
  SketchSceneItem item;
  SketchScenePublicationController controller{item, executor};
  const SceneStamp firstStamp = stamp(225, 1, 225, 225, 225, 1);
  const SceneStamp secondStamp = stamp(226, 1, 226, 226, 226, 1);
  auto first = scene(1, 225, firstStamp);
  auto second = scene(1, 226, secondStamp);
  std::uint64_t expectedPublications = 0;
  const auto publishAndDrain =
      [&](std::shared_ptr<const SketchSceneSnapshot> base,
          std::uint64_t generation) {
        auto offered = controller.publishProducts(
            products(std::move(base), generation, generation));
        require(offered.has_value(), "global generation packet failed");
        ++expectedPublications;
        pumpUntil([&] {
          const auto executorMetrics = executor.metrics();
          return controller.metrics().itemPublications >=
                     expectedPublications &&
                 executorMetrics.pendingRetirements == 0U;
        });
      };

  require(controller.retarget(firstStamp.target).has_value(),
          "first global generation target failed");
  publishAndDrain(first, 1);
  require(controller.retarget(firstStamp.target).has_value(),
          "same-target retarget failed");
  auto repeated = controller.publishProducts(products(first, 1, 1));
  require(!repeated && repeated.error().code ==
                           "desktop.sketch.publication-product-conflict",
          "A to A retarget reset the controller-global generation clock");
  publishAndDrain(first, 2);

  require(controller.retarget(secondStamp.target).has_value(),
          "second global generation target failed");
  publishAndDrain(second, 3);
  require(controller.retarget(firstStamp.target).has_value(),
          "return global generation target failed");
  auto returned = controller.publishProducts(products(first, 3, 3));
  require(!returned && returned.error().code ==
                           "desktop.sketch.publication-product-conflict",
          "A to B to A retarget reset the controller-global generation clock");
  publishAndDrain(first, 4);
  shutdownController(controller,
                     "global generation controller shutdown failed");
  executor.requestShutdown();
  require(executor.waitUntilDrained(std::chrono::seconds{5}),
          "global generation executor did not drain");
  executor.join();
}

void verifyMaximumCapacityRingWrap() {
  constexpr std::size_t capacity = 64;
  SketchPreparationExecutor executor{{capacity, 2, 7}, failedPreparation};
  std::array<RecordingSink, capacity> sinks;
  std::array<std::uint64_t, capacity> incarnations{};
  const SceneStamp baseStamp = stamp(227, 1, 227, 227, 227, 1);
  auto base = scene(1, 227, baseStamp);
  std::uint64_t expectedDeliveries = 0;
  for (std::uint64_t cycle = 1; cycle <= 3; ++cycle) {
    std::array<std::optional<SketchPreparationSubscription>, capacity>
        subscriptions;
    for (std::size_t index = 0; index < capacity; ++index) {
      auto subscribed = executor.subscribe(sinks[index], sinks[index]);
      require(subscribed.has_value() && subscribed->slot() == index,
              "maximum-capacity subscription order failed");
      if (cycle != 1U)
        require(subscribed->incarnation() > incarnations[index],
                "ring slot incarnation did not advance");
      incarnations[index] = subscribed->incarnation();
      subscriptions[index] = *subscribed;
    }
    for (std::size_t index = 0; index < capacity; ++index) {
      const std::uint64_t generation = cycle * capacity + index + 1U;
      require(executor
                  .submit(*subscriptions[index],
                          ownedProducts(productStamp(baseStamp.target,
                                                     generation, generation),
                                        base),
                          {})
                  .has_value(),
              "maximum-capacity submission failed");
    }
    expectedDeliveries += capacity;
    pumpUntil([&] {
      std::uint64_t delivered = 0;
      for (const auto &sink : sinks)
        delivered += sink.deliveries();
      const auto metrics = executor.metrics();
      return delivered == expectedDeliveries &&
             metrics.activePreparations == 0U &&
             metrics.pendingCompletions == 0U;
    });
    for (const auto &subscription : subscriptions)
      require(executor.unsubscribe(*subscription).has_value(),
              "maximum-capacity unsubscribe failed");
    pumpUntil([&] { return executor.metrics().leasedSubscriptions == 0U; });
  }
  const auto metrics = executor.metrics();
  require(
      metrics.subscriptions == capacity * 3U &&
          metrics.unsubscriptions == capacity * 3U &&
          metrics.maximumPendingPreparations <= capacity &&
          metrics.maximumPendingCompletions <= capacity &&
          metrics.maximumPendingRetirements <=
              capacity *
                  SketchPreparationLimits::maximumRetirementsPerSubscription &&
          metrics.maximumUiDeliveriesInTurn <= 7U &&
          preparationMetricsReconcile(metrics),
      "maximum-capacity ring wrap escaped a fixed bound");
  executor.requestShutdown();
  require(executor.waitUntilDrained(std::chrono::seconds{5}),
          "maximum-capacity executor did not drain");
  executor.join();
}

void verifyMovedLifetimeRetirement() {
  std::mutex startMutex;
  std::condition_variable startChanged;
  bool startEntered = false;
  bool releaseStart = false;
  const auto fault = [&](SketchPreparationExecutor::FaultSite site) {
    if (site != SketchPreparationExecutor::FaultSite::Start)
      return;
    std::unique_lock lock{startMutex};
    startEntered = true;
    startChanged.notify_all();
    startChanged.wait(lock, [&] { return releaseStart; });
  };
  SketchPreparationExecutor executor{{1, 1, 1}, failedPreparation, fault};
  struct ReleaseStartOnExit {
    std::mutex &mutex;
    std::condition_variable &changed;
    bool &release;
    ~ReleaseStartOnExit() {
      {
        std::scoped_lock lock{mutex};
        release = true;
      }
      changed.notify_all();
    }
  } releaseOnExit{startMutex, startChanged, releaseStart};
  std::atomic_uint64_t deliveries = 0;
  auto *lifetime = new AtomicRecordingSink{deliveries};
  std::atomic_bool lifetimeDestroyed = false;
  QObject::connect(lifetime, &QObject::destroyed, [&] {
    lifetimeDestroyed.store(true, std::memory_order_release);
  });
  auto subscription = executor.subscribe(*lifetime, *lifetime);
  require(subscription.has_value(), "moved lifetime subscription failed");
  const SceneStamp baseStamp = stamp(229, 1, 229, 229, 229, 1);
  auto base = scene(1, 229, baseStamp);
  require(executor
              .submit(*subscription,
                      ownedProducts(productStamp(baseStamp.target, 1, 1), base),
                      {})
              .has_value(),
          "moved lifetime submission failed");
  {
    std::unique_lock lock{startMutex};
    require(startChanged.wait_for(lock, std::chrono::seconds{5},
                                  [&] { return startEntered; }),
            "moved lifetime preparation did not enter its start boundary");
  }
  QThread alternate;
  alternate.start();
  lifetime->moveToThread(&alternate);
  require(
      QMetaObject::invokeMethod(lifetime, "deleteLater", Qt::QueuedConnection),
      "moved lifetime deletion was not queued");
  pumpUntil([&] { return lifetimeDestroyed.load(std::memory_order_acquire); });
  {
    std::scoped_lock lock{startMutex};
    releaseStart = true;
  }
  startChanged.notify_all();
  pumpUntil([&] { return executor.metrics().leasedSubscriptions == 0U; });
  require(deliveries.load(std::memory_order_relaxed) == 0U,
          "moved lifetime received a preparation callback");
  alternate.quit();
  require(alternate.wait(5000), "moved lifetime thread did not stop");

  RecordingSink replacement;
  auto reused = executor.subscribe(replacement, replacement);
  require(reused && reused->slot() == subscription->slot() &&
              reused->incarnation() != subscription->incarnation(),
          "affinity retirement did not recover executor capacity");
  require(executor.unsubscribe(*reused).has_value(),
          "moved lifetime replacement unsubscribe failed");
  executor.requestShutdown();
  require(executor.waitUntilDrained(std::chrono::seconds{5}),
          "moved lifetime executor did not drain");
  executor.join();
}

void verifyGeneratedExecutorModel(const testkit::PropertyProfile &profile) {
  SketchPreparationExecutor executor{{8, 2, 4}, failedPreparation};
  std::vector<std::unique_ptr<RecordingSink>> sinks;
  std::vector<std::optional<SketchPreparationSubscription>> subscriptions(4);
  std::vector<std::uint64_t> generations(4, 0);
  for (std::size_t index = 0; index < subscriptions.size(); ++index) {
    sinks.push_back(std::make_unique<RecordingSink>());
    auto subscribed = executor.subscribe(*sinks.back(), *sinks.back());
    require(subscribed.has_value(), "model subscription was rejected");
    subscriptions[index] = *subscribed;
  }
  const SceneStamp baseStamp = stamp(201, 1, 201, 201, 201, 1);
  auto base = scene(1, 201, baseStamp);
  std::optional<Diagnostic> crossThreadError;
  std::thread wrongThread([&] {
    auto submitted = executor.submit(
        *subscriptions.front(),
        ownedProducts(productStamp(baseStamp.target, 1, 1), base), {});
    if (!submitted)
      crossThreadError = submitted.error();
  });
  wrongThread.join();
  require(crossThreadError &&
              crossThreadError->code == "desktop.sketch.publication-thread",
          "executor UI-thread guard returned the wrong diagnostic");

  auto otherTarget = scene(1, 202, stamp(211, 1, 211, 211, 211, 1));
  auto mismatched = executor.submit(
      *subscriptions.front(),
      ownedProducts(productStamp(baseStamp.target, 1, 1), otherTarget), {});
  require(!mismatched &&
              mismatched.error().code == "desktop.sketch.products-target",
          "executor accepted mismatched product and scene targets");

  testkit::checkProperty(
      "shared preparation executor model", profile,
      [&](testkit::Random &random, std::uint64_t index) {
        const std::size_t view = random.next() % subscriptions.size();
        const std::uint64_t operation = random.next() % 12U;
        if (!subscriptions[view]) {
          auto subscribed = executor.subscribe(*sinks[view], *sinks[view]);
          if (subscribed)
            subscriptions[view] = *subscribed;
        } else if (operation == 0U) {
          require(executor.unsubscribe(*subscriptions[view]).has_value(),
                  "model unsubscribe was rejected");
          subscriptions[view].reset();
        } else if (operation == 1U) {
          require(executor.invalidate(*subscriptions[view]).has_value(),
                  "model invalidation was rejected");
        } else {
          const std::uint64_t generation = ++generations[view];
          auto submitted = executor.submit(
              *subscriptions[view],
              ownedProducts(productStamp(baseStamp.target, generation,
                                         random.next() ^ generation),
                            base),
              {});
          require(submitted || submitted.error().code ==
                                   "desktop.sketch.preparation-backpressure",
                  "model submission failed outside bounded backpressure");
        }

        if ((index & 127U) == 0U)
          QCoreApplication::processEvents(QEventLoop::AllEvents);
        const auto metrics = executor.metrics();
        require(metrics.workerCount == 2U, "worker count changed");
        require(metrics.activePreparations <= 2U,
                "active preparations exceeded worker count");
        require(metrics.pendingPreparations <= 8U,
                "pending preparations exceeded fixed slot capacity");
        require(metrics.pendingCompletions <= 8U,
                "pending completions exceeded fixed slot capacity");
        require(metrics.maximumActivePreparations <= 2U,
                "maximum active preparations exceeded worker count");
        require(metrics.maximumPendingPreparations <= 8U,
                "maximum pending preparations exceeded fixed slot capacity");
        require(metrics.maximumPendingCompletions <= 8U,
                "maximum pending completions exceeded fixed slot capacity");
        require(metrics.pendingRetirements <= 16U &&
                    metrics.maximumPendingRetirements <= 16U,
                "worker retirement exceeded fixed slot capacity");
        require(metrics.maximumUiDeliveriesInTurn <= 4U,
                "UI drain exceeded its delivery slice");
        require(preparationMetricsReconcile(metrics),
                "model preparation metrics did not reconcile");
      });

  for (auto &subscription : subscriptions) {
    if (subscription) {
      require(executor.unsubscribe(*subscription).has_value(),
              "final model unsubscribe was rejected");
      subscription.reset();
    }
  }
  pumpUntil([&] { return executor.metrics().leasedSubscriptions == 0U; });
  executor.requestShutdown();
  require(executor.waitUntilDrained(std::chrono::seconds{5}),
          "model executor did not drain");
  executor.join();
}

void verifyNonblockingUnsubscribeAndAba() {
  std::mutex latchMutex;
  std::condition_variable latch;
  bool entered = false;
  bool release = false;
  const auto blocked = [&](const SketchPreparationRequest &request,
                           std::shared_ptr<const PreparedSketchProducts> reuse,
                           std::stop_token stop)
      -> Result<std::shared_ptr<const PreparedSketchProducts>> {
    {
      std::unique_lock lock{latchMutex};
      entered = true;
      latch.notify_all();
      latch.wait(lock, [&] { return release || stop.stop_requested(); });
    }
    if (stop.stop_requested())
      return std::unexpected(diagnostic("desktop.sketch.preparation-cancelled",
                                        "test cancellation"));
    return prepareSketchProducts(request.products, request.options,
                                 std::move(reuse), stop);
  };

  SketchPreparationExecutor executor{{1, 1, 1}, blocked};
  RecordingSink first;
  auto firstSubscription = executor.subscribe(first, first);
  require(firstSubscription.has_value(), "latch subscription was rejected");
  const SceneStamp baseStamp = stamp(202, 1, 202, 202, 202, 1);
  auto base = scene(1, 202, baseStamp);
  require(executor
              .submit(*firstSubscription,
                      ownedProducts(productStamp(baseStamp.target, 1, 1), base),
                      {})
              .has_value(),
          "latch submission was rejected");
  {
    std::unique_lock lock{latchMutex};
    require(
        latch.wait_for(lock, std::chrono::seconds{5}, [&] { return entered; }),
        "latch preparation did not start");
  }

  require(executor.unsubscribe(*firstSubscription).has_value(),
          "latch unsubscribe was rejected");
  {
    std::scoped_lock lock{latchMutex};
    release = true;
  }
  latch.notify_all();
  pumpUntil([&] { return executor.metrics().leasedSubscriptions == 0U; });
  require(first.deliveries() == 0U,
          "unsubscribed sink received a stale callback");

  RecordingSink replacement;
  auto replacementSubscription = executor.subscribe(replacement, replacement);
  require(replacementSubscription.has_value() &&
              replacementSubscription->slot() == firstSubscription->slot() &&
              replacementSubscription->incarnation() !=
                  firstSubscription->incarnation(),
          "reused subscription slot did not change incarnation");
  auto stale = executor.submit(
      *firstSubscription,
      ownedProducts(productStamp(baseStamp.target, 2, 2), base), {});
  require(!stale &&
              stale.error().code == "desktop.sketch.preparation-subscription",
          "stale subscription passed the ABA boundary");
  require(executor.unsubscribe(*replacementSubscription).has_value(),
          "replacement unsubscribe was rejected");
  pumpUntil([&] { return executor.metrics().leasedSubscriptions == 0U; });

  auto dying = std::make_unique<RecordingSink>();
  auto dyingSubscription = executor.subscribe(*dying, *dying);
  require(dyingSubscription.has_value(),
          "lifetime-owned subscription was rejected");
  dying.reset();
  pumpUntil([&] { return executor.metrics().leasedSubscriptions == 0U; });
  RecordingSink recovered;
  auto recoveredSubscription = executor.subscribe(recovered, recovered);
  require(recoveredSubscription.has_value(),
          "destroyed lifetime did not recover executor capacity");
  require(executor.unsubscribe(*recoveredSubscription).has_value(),
          "recovered unsubscribe was rejected");
  executor.requestShutdown();
  require(executor.waitUntilDrained(std::chrono::seconds{5}),
          "ABA executor did not drain");
  executor.join();
}

void verifyCoalescedBoundedUiDrain(const testkit::PropertyProfile &profile) {
  SketchPreparationExecutor executor{{1, 1, 3}, failedPreparation};
  RecordingSink sink;
  auto subscription = executor.subscribe(sink, sink);
  require(subscription.has_value(), "flood subscription was rejected");
  const SceneStamp baseStamp = stamp(203, 1, 203, 203, 203, 1);
  auto base = scene(1, 203, baseStamp);
  std::uint64_t latestAccepted = 0;
  std::uint64_t backpressure = 0;
  for (std::uint64_t index = 1; index <= profile.iterations; ++index) {
    auto submitted = executor.submit(
        *subscription,
        ownedProducts(productStamp(baseStamp.target, index, index), base), {});
    require(submitted || submitted.error().code ==
                             "desktop.sketch.preparation-backpressure",
            "flood submission failed outside bounded backpressure");
    if (submitted)
      latestAccepted = index;
    else
      ++backpressure;
  }
  waitWithoutEvents([&] { return executor.metrics().uiDrainPosts != 0U; });
  const auto queued = executor.metrics();
  require(queued.uiDrainPosts == 1U && queued.pendingPreparations <= 1U &&
              queued.pendingCompletions <= 1U &&
              queued.pendingRetirements <= 2U,
          "no-event-loop flood queued more than one UI drain or mailbox");
  pumpUntil([&] {
    const auto metrics = executor.metrics();
    return metrics.delivered != 0U && metrics.pendingPreparations == 0U &&
           metrics.pendingCompletions == 0U && metrics.activePreparations == 0U;
  });
  const auto metrics = executor.metrics();
  require(metrics.maximumPendingPreparations == 1U &&
              metrics.maximumPendingCompletions == 1U &&
              metrics.maximumUiDeliveriesInTurn <= 3U &&
              metrics.uiDrainPosts == metrics.uiDrainTurns,
          "flood escaped the bounded coalesced mailboxes");
  require(sink.lastProduct() &&
              sink.lastProduct()->generation.value() == latestAccepted,
          "flood did not deliver the latest product epoch");
  require(metrics.backpressure == backpressure,
          "flood backpressure metrics did not reconcile");
  require(executor.unsubscribe(*subscription).has_value(),
          "flood unsubscribe was rejected");
  executor.requestShutdown();
  require(executor.waitUntilDrained(std::chrono::seconds{5}),
          "flood executor did not drain");
  executor.join();
}

struct ThreadProbe {
  std::atomic_bool destroyed = false;
  std::thread::id thread;
};

struct ProbedSceneOwner {
  ProbedSceneOwner(std::shared_ptr<const SketchSceneSnapshot> ownedScene,
                   std::shared_ptr<ThreadProbe> ownedProbe)
      : scene(std::move(ownedScene)), probe(std::move(ownedProbe)) {}
  ProbedSceneOwner(const ProbedSceneOwner &) = delete;
  std::shared_ptr<const SketchSceneSnapshot> scene;
  std::shared_ptr<ThreadProbe> probe;
  ~ProbedSceneOwner() {
    probe->thread = std::this_thread::get_id();
    probe->destroyed.store(true, std::memory_order_release);
  }
};

struct ProbedProvisionalOwner {
  ProbedProvisionalOwner(
      std::shared_ptr<const SketchProvisionalGeometry> ownedProvisional,
      std::shared_ptr<ThreadProbe> ownedProbe)
      : provisional(std::move(ownedProvisional)), probe(std::move(ownedProbe)) {
  }
  ProbedProvisionalOwner(const ProbedProvisionalOwner &) = delete;
  std::shared_ptr<const SketchProvisionalGeometry> provisional;
  std::shared_ptr<ThreadProbe> probe;
  ~ProbedProvisionalOwner() {
    probe->thread = std::this_thread::get_id();
    probe->destroyed.store(true, std::memory_order_release);
  }
};

class BlockingPreparation final {
public:
  class ReleaseGuard final {
  public:
    explicit ReleaseGuard(BlockingPreparation &owner) : owner_(owner) {}
    ReleaseGuard(const ReleaseGuard &) = delete;
    ReleaseGuard &operator=(const ReleaseGuard &) = delete;
    ~ReleaseGuard() { owner_.release(); }

  private:
    BlockingPreparation &owner_;
  };

  Result<std::shared_ptr<const PreparedSketchProducts>>
  prepare(const SketchPreparationRequest &request,
          std::shared_ptr<const PreparedSketchProducts> reuse,
          std::stop_token stop) {
    {
      std::unique_lock lock{mutex_};
      entered_ = true;
      changed_.notify_all();
      changed_.wait(lock, [&] { return released_; });
    }
    if (stop.stop_requested())
      return std::unexpected(diagnostic("desktop.sketch.preparation-cancelled",
                                        "test cancellation"));
    return prepareSketchProducts(request.products, request.options,
                                 std::move(reuse), stop);
  }

  [[nodiscard]] bool waitUntilEntered() {
    std::unique_lock lock{mutex_};
    return changed_.wait_for(lock, std::chrono::seconds{5},
                             [&] { return entered_; });
  }

  void release() {
    {
      std::scoped_lock lock{mutex_};
      released_ = true;
    }
    changed_.notify_all();
  }

  [[nodiscard]] ReleaseGuard releaseOnExit() { return ReleaseGuard{*this}; }

private:
  std::mutex mutex_;
  std::condition_variable changed_;
  bool entered_ = false;
  bool released_ = false;
};

void verifySchedulingFailureRecovery() {
  {
    std::atomic_uint64_t submissions = 0;
    const auto fault = [&](SketchPreparationExecutor::FaultSite site) {
      if (site == SketchPreparationExecutor::FaultSite::Submission &&
          submissions.fetch_add(1, std::memory_order_relaxed) == 0U)
        throw std::bad_alloc{};
    };
    SketchPreparationExecutor executor{{1, 1, 1}, {}, fault};
    SketchSceneItem item;
    SketchScenePublicationController controller{item, executor};
    const SceneStamp baseStamp = stamp(232, 1, 232, 232, 232, 1);
    auto base = scene(1, 232, baseStamp);
    require(controller.retarget(baseStamp.target).has_value(),
            "allocation-recovery target failed");
    auto failed = controller.publishProducts(products(base, 1, 1));
    require(!failed &&
                failed.error().code ==
                    "desktop.sketch.preparation-request-allocation" &&
                controller.metrics().productPublications == 1U &&
                controller.metrics().preparationRequests == 0U,
            "submission allocation failure was masked as publication success");
    auto retried = controller.publishProducts(products(base, 1, 1));
    require(retried && !retried->productsChanged &&
                retried->preparationScheduled,
            "identical packet did not retry failed scheduling");
    pumpUntil([&] { return controller.metrics().itemPublications == 1U; });
    shutdownController(controller,
                       "allocation-recovery controller shutdown failed");
    executor.requestShutdown();
    require(executor.waitUntilDrained(std::chrono::seconds{5}),
            "allocation-recovery executor did not drain");
    executor.join();
  }

  {
    BlockingPreparation blocking;
    const auto prepare =
        [&](const SketchPreparationRequest &request,
            std::shared_ptr<const PreparedSketchProducts> reuse,
            std::stop_token stop) {
          return blocking.prepare(request, std::move(reuse), stop);
        };
    SketchPreparationExecutor executor{{1, 1, 1}, prepare};
    auto releaseOnExit = blocking.releaseOnExit();
    SketchSceneItem item;
    SketchScenePublicationController controller{item, executor};
    const SceneStamp firstStamp = stamp(233, 1, 233, 233, 233, 1);
    const SceneStamp secondStamp = stamp(233, 2, 233, 233, 233, 2);
    const SceneStamp thirdStamp = stamp(233, 3, 233, 233, 233, 3);
    const SceneStamp fourthStamp = stamp(233, 4, 233, 233, 233, 4);
    auto first = scene(1, 233, firstStamp);
    auto second = scene(1, 234, secondStamp);
    auto third = scene(1, 235, thirdStamp);
    auto fourth = scene(1, 236, fourthStamp);
    require(controller.retarget(firstStamp.target).has_value(),
            "backpressure-recovery target failed");
    require(controller.publishProducts(products(first, 1, 1)).has_value(),
            "backpressure active packet failed");
    require(blocking.waitUntilEntered(),
            "backpressure active preparation did not start");
    require(controller.publishProducts(products(second, 2, 2)).has_value(),
            "backpressure pending packet failed");
    require(controller.publishProducts(products(third, 3, 3)).has_value(),
            "backpressure replacement packet failed");
    auto saturated = controller.publishProducts(products(fourth, 4, 4));
    require(!saturated &&
                saturated.error().code ==
                    "desktop.sketch.preparation-backpressure" &&
                controller.metrics().productPublications == 4U &&
                controller.metrics().preparationRequests == 3U,
            "scheduling backpressure was masked as publication success");
    blocking.release();
    pumpUntil([&] {
      auto retained = controller.currentProducts();
      return controller.metrics().itemPublications == 1U && retained &&
             retained->stamp.generation.value() == 4U;
    });
    shutdownController(controller,
                       "backpressure-recovery controller shutdown failed");
    executor.requestShutdown();
    require(executor.waitUntilDrained(std::chrono::seconds{5}),
            "backpressure-recovery executor did not drain");
    executor.join();
  }

  {
    BlockingPreparation blocking;
    const auto prepare =
        [&](const SketchPreparationRequest &request,
            std::shared_ptr<const PreparedSketchProducts> reuse,
            std::stop_token stop) {
          return blocking.prepare(request, std::move(reuse), stop);
        };
    SketchPreparationExecutor executor{{1, 1, 1}, prepare};
    auto releaseOnExit = blocking.releaseOnExit();
    SketchSceneItem item;
    SketchScenePublicationController controller{item, executor};
    const SceneStamp baseStamp = stamp(234, 1, 234, 234, 234, 1);
    auto base = scene(1, 234, baseStamp);
    require(controller.retarget(baseStamp.target).has_value(),
            "item-rejection target failed");
    require(controller.publishProducts(products(base, 1, 1)).has_value(),
            "item-rejection packet failed");
    require(blocking.waitUntilEntered(),
            "item-rejection preparation did not start");
    item.retarget(stamp(235, 1, 235, 235, 235, 1).target);
    blocking.release();
    pumpUntil([&] {
      return controller.metrics().itemRejections == 1U &&
             executor.metrics().pendingCompletions == 0U;
    });
    item.retarget(baseStamp.target);
    auto retried = controller.publishProducts(products(base, 1, 1));
    require(retried && !retried->productsChanged &&
                retried->preparationScheduled,
            "item rejection suppressed an identical retry");
    pumpUntil([&] { return controller.metrics().itemPublications == 1U; });
    shutdownController(controller, "item-rejection controller shutdown failed");
    executor.requestShutdown();
    require(executor.waitUntilDrained(std::chrono::seconds{5}),
            "item-rejection executor did not drain");
    executor.join();
  }
}

void verifyControllerShutdownRetiresLatestProduct() {
  SketchPreparationExecutor executor{{1, 1, 1}};
  SketchSceneItem item;
  SketchScenePublicationController controller{item, executor};
  const SceneStamp baseStamp = stamp(236, 1, 236, 236, 236, 1);
  auto base = scene(1, 236, baseStamp);
  auto created = provisional(baseStamp, 1);
  auto probe = std::make_shared<ThreadProbe>();
  auto owner = std::make_shared<ProbedProvisionalOwner>(created, probe);
  std::shared_ptr<const SketchProvisionalGeometry> aliased(owner,
                                                           created.get());
  require(controller.retarget(baseStamp.target).has_value(),
          "shutdown-retirement target failed");
  require(controller.publishProducts(products(base, 1, 1, std::move(aliased)))
              .has_value(),
          "shutdown-retirement packet failed");
  owner.reset();
  created.reset();
  pumpUntil([&] { return controller.metrics().itemPublications == 1U; });
  shutdownController(controller,
                     "shutdown-retirement controller shutdown failed");
  pumpUntil([&] { return probe->destroyed.load(std::memory_order_acquire); });
  require(probe->thread != std::this_thread::get_id(),
          "latest product owner was destroyed on the UI thread");
  executor.requestShutdown();
  require(executor.waitUntilDrained(std::chrono::seconds{5}),
          "shutdown-retirement executor did not drain");
  executor.join();
}

void verifyWorkerSideStaleRelease() {
  SketchPreparationExecutor executor{{1, 1, 1}};
  RecordingSink sink;
  auto subscription = executor.subscribe(sink, sink);
  require(subscription.has_value(), "probe subscription was rejected");
  const SceneStamp baseStamp = stamp(204, 1, 204, 204, 204, 1);
  auto created = scene(200, 204, baseStamp);
  auto probe = std::make_shared<ThreadProbe>();
  auto owner = std::make_shared<ProbedSceneOwner>(created, probe);
  std::shared_ptr<const SketchSceneSnapshot> aliased(owner, created.get());
  created.reset();
  require(
      executor
          .submit(*subscription,
                  ownedProducts(productStamp(baseStamp.target, 1, 1), aliased),
                  {})
          .has_value(),
      "probe submission was rejected");
  pumpUntil([&] { return executor.metrics().started != 0U; });
  require(executor.unsubscribe(*subscription).has_value(),
          "probe unsubscribe was rejected");
  aliased.reset();
  owner.reset();
  pumpUntil([&] { return probe->destroyed.load(std::memory_order_acquire); });
  require(probe->thread != std::this_thread::get_id(),
          "stale preparation artifacts were released on the UI thread");
  executor.requestShutdown();
  require(executor.waitUntilDrained(std::chrono::seconds{5}),
          "probe executor did not drain");
  executor.join();
}

void verifyBoundedWorkerRetirement() {
  const SceneStamp baseStamp = stamp(228, 1, 228, 228, 228, 1);
  auto base = scene(1, 228, baseStamp);
  {
    BlockingPreparation blocking;
    const auto prepare =
        [&](const SketchPreparationRequest &request,
            std::shared_ptr<const PreparedSketchProducts> reuse,
            std::stop_token stop) {
          return blocking.prepare(request, std::move(reuse), stop);
        };
    SketchPreparationExecutor executor{{1, 1, 1}, prepare};
    auto releaseOnExit = blocking.releaseOnExit();
    RecordingSink sink;
    auto subscription = executor.subscribe(sink, sink);
    require(subscription.has_value(), "pending retirement subscription failed");
    require(
        executor
            .submit(*subscription,
                    ownedProducts(productStamp(baseStamp.target, 1, 1), base),
                    {})
            .has_value(),
        "pending retirement active submission failed");
    require(blocking.waitUntilEntered(),
            "pending retirement preparation did not start");

    auto probe = std::make_shared<ThreadProbe>();
    auto owner = std::make_shared<ProbedSceneOwner>(base, probe);
    std::shared_ptr<const SketchSceneSnapshot> aliased(owner, base.get());
    require(executor
                .submit(*subscription,
                        ownedProducts(productStamp(baseStamp.target, 2, 2),
                                      aliased),
                        {})
                .has_value(),
            "pending retirement probe submission failed");
    aliased.reset();
    owner.reset();
    require(
        executor
            .submit(*subscription,
                    ownedProducts(productStamp(baseStamp.target, 3, 3), base),
                    {})
            .has_value(),
        "first pending replacement failed");
    require(
        executor
            .submit(*subscription,
                    ownedProducts(productStamp(baseStamp.target, 4, 4), base),
                    {})
            .has_value(),
        "second pending replacement failed");
    auto saturated = executor.submit(
        *subscription,
        ownedProducts(productStamp(baseStamp.target, 5, 5), base), {});
    require(!saturated &&
                saturated.error().code ==
                    "desktop.sketch.preparation-backpressure" &&
                executor.metrics().pendingRetirements == 2U &&
                !probe->destroyed.load(std::memory_order_acquire),
            "pending retirement did not apply fixed nonblocking backpressure");
    blocking.release();
    pumpUntil([&] {
      const auto metrics = executor.metrics();
      return probe->destroyed.load(std::memory_order_acquire) &&
             sink.deliveries() == 1U && metrics.pendingRetirements == 0U &&
             metrics.activePreparations == 0U;
    });
    require(probe->thread != std::this_thread::get_id() && sink.lastProduct() &&
                sink.lastProduct()->generation.value() == 4U,
            "superseded pending owner left the worker or changed acceptance");
    const auto metrics = executor.metrics();
    require(metrics.backpressure == 1U && metrics.retiredPending == 2U &&
                preparationMetricsReconcile(metrics),
            "pending retirement metrics did not reconcile");
    require(executor.unsubscribe(*subscription).has_value(),
            "pending retirement unsubscribe failed");
    executor.requestShutdown();
    require(executor.waitUntilDrained(std::chrono::seconds{5}),
            "pending retirement executor did not drain");
    executor.join();
  }

  {
    BlockingPreparation blocking;
    const auto prepare =
        [&](const SketchPreparationRequest &request,
            std::shared_ptr<const PreparedSketchProducts> reuse,
            std::stop_token stop) {
          return blocking.prepare(request, std::move(reuse), stop);
        };
    SketchPreparationExecutor executor{{1, 1, 1}, prepare};
    auto releaseOnExit = blocking.releaseOnExit();
    SketchSceneItem item;
    SketchScenePublicationController controller{item, executor};
    require(controller.retarget(baseStamp.target).has_value(),
            "product retirement target failed");
    auto probe = std::make_shared<ThreadProbe>();
    auto created = provisional(baseStamp, 1);
    auto owner = std::make_shared<ProbedProvisionalOwner>(created, probe);
    std::shared_ptr<const SketchProvisionalGeometry> aliased(owner,
                                                             created.get());
    auto first = products(base, 1, 1, aliased);
    require(controller.publishProducts(std::move(first)).has_value(),
            "product retirement probe packet failed");
    require(blocking.waitUntilEntered(),
            "product retirement preparation did not start");
    aliased.reset();
    owner.reset();
    created.reset();
    require(controller.publishProducts(products(base, 2, 2)).has_value(),
            "first product replacement failed");
    require(controller.publishProducts(products(base, 3, 3)).has_value(),
            "second product replacement failed");
    auto saturated = controller.publishProducts(products(base, 4, 4));
    require(!saturated &&
                saturated.error().code ==
                    "desktop.sketch.preparation-backpressure" &&
                executor.metrics().pendingRetirements == 2U &&
                !probe->destroyed.load(std::memory_order_acquire),
            "product replacement did not apply fixed nonblocking backpressure");
    blocking.release();
    pumpUntil([&] {
      const auto metrics = executor.metrics();
      return probe->destroyed.load(std::memory_order_acquire) &&
             controller.metrics().itemPublications == 1U &&
             metrics.pendingRetirements == 0U;
    });
    require(probe->thread != std::this_thread::get_id(),
            "replaced product packet was destroyed on the UI thread");
    auto retried = controller.publishProducts(products(base, 4, 4));
    require(retried.has_value(),
            "product packet did not recover after retirement backpressure");
    auto retained = controller.currentProducts();
    require(retained && retained->stamp.generation.value() == 4U,
            "product retry did not become current evidence");
    shutdownController(controller,
                       "product retirement controller shutdown failed");
    executor.requestShutdown();
    require(executor.waitUntilDrained(std::chrono::seconds{5}),
            "product retirement executor did not drain");
    executor.join();
  }
}

void verifyGlobalProductStamp() {
  SketchPreparationExecutor executor{{2, 1, 2}};
  SketchSceneItem item;
  SketchScenePublicationController controller{item, executor};
  const SceneStamp baseStamp = stamp(205, 1, 205, 205, 205, 1);
  auto base = scene(1, 205, baseStamp);
  require(controller.retarget(baseStamp.target).has_value(),
          "product target was rejected");

  auto interaction = provisional(baseStamp, 1);
  require(controller.publishProducts(products(base, 1, 11, interaction))
              .has_value(),
          "initial product packet was rejected");
  auto duplicate =
      controller.publishProducts(products(base, 1, 11, interaction));
  require(duplicate && !duplicate->productsChanged,
          "identical product stamp was not a duplicate");
  auto twin = scene(1, 205, baseStamp);
  auto identityConflict =
      controller.publishProducts(products(twin, 1, 11, interaction));
  require(!identityConflict &&
              identityConflict.error().code ==
                  "desktop.sketch.publication-product-conflict",
          "equal product stamp masked conflicting exact components");
  auto conflict = controller.publishProducts(products(base, 1, 12));
  require(!conflict && conflict.error().code ==
                           "desktop.sketch.publication-product-conflict",
          "same-generation product conflict was accepted");
  pumpUntil([&] { return controller.metrics().itemPublications == 1U; });
  const std::uint64_t requests = controller.metrics().preparationRequests;
  auto cleared = controller.publishProducts(products(base, 2, 13));
  require(cleared && cleared->preparationScheduled,
          "null product clear was rejected");
  auto retained = controller.currentProducts();
  require(retained && !retained->provisional &&
              controller.metrics().preparationRequests == requests + 1U,
          "same-base product update bypassed packet preparation");
  auto resurrected =
      controller.publishProducts(products(base, 1, 11, interaction));
  require(!resurrected && resurrected.error().code ==
                              "desktop.sketch.publication-stale-products",
          "cleared products were resurrected");

  auto wrong = scene(1, 206, stamp(206, 1, 206, 206, 206, 1));
  auto wrongTarget = controller.publishProducts(products(wrong, 3, 14));
  require(!wrongTarget && wrongTarget.error().code ==
                              "desktop.sketch.publication-stale-target",
          "cross-target packet was accepted");

  auto edit = SketchEditSessionHandle::create(7);
  auto tool = SketchToolInstanceHandle::create(7);
  auto markerGeneration = SketchMarkerGeneration::create(1);
  auto viewGeneration = SketchMarkerViewGeneration::create(10);
  auto markerHandle = SketchMarkerHandle::create(1);
  require(edit && tool && markerGeneration && viewGeneration && markerHandle,
          "view marker identity was invalid");
  const SketchMarkerStamp markerStamp{{baseStamp,
                                       SketchMarkerInteraction{*edit, *tool},
                                       std::nullopt, *viewGeneration},
                                      *markerGeneration,
                                      digest<SketchMarkerDigest>(1)};
  const std::vector<SketchMarkerAnchor> anchors{
      SketchCanonicalMarkerAnchor{{0.0, 0.0}}};
  const std::vector<PackedSketchMarker> markerValues{
      {*markerHandle, std::nullopt, 0, 1, SketchMarkerKind::EndpointSnap, 0.0}};
  auto createdMarkers =
      SketchMarkerPacket::create(markerStamp, base, {}, anchors, markerValues);
  require(createdMarkers.has_value(), "view marker packet was invalid");
  require(controller.publishMarkerView(*viewGeneration).has_value(),
          "current marker view was rejected");
  auto viewPacket = products(base, 3, 15);
  viewPacket.markers = *createdMarkers;
  require(controller.publishProducts(viewPacket).has_value(),
          "packet for the current marker view was rejected");
  pumpUntil([&] { return executor.metrics().pendingRetirements == 0U; });
  require(controller.publishCamera({2, {}, 0.001, 0.0}) ==
              SketchCameraDecision::Accepted,
          "marker invalidation camera was rejected");
  retained = controller.currentProducts();
  require(!retained,
          "camera change exposed stale view-dependent marker evidence");
  auto staleViewPacket = controller.publishProducts(std::move(viewPacket));
  require(!staleViewPacket && staleViewPacket.error().code ==
                                  "desktop.sketch.publication-marker-view",
          "camera change did not invalidate view-stamped markers");
  auto nextView = SketchMarkerViewGeneration::create(11);
  auto nextMarkerGeneration = SketchMarkerGeneration::create(2);
  require(nextView && nextMarkerGeneration,
          "replacement marker view identity was invalid");
  require(controller.publishMarkerView(*nextView).has_value(),
          "replacement marker view was rejected");
  const SketchMarkerStamp nextMarkerStamp{
      {baseStamp, SketchMarkerInteraction{*edit, *tool}, std::nullopt,
       *nextView},
      *nextMarkerGeneration,
      digest<SketchMarkerDigest>(2)};
  auto nextMarkers = SketchMarkerPacket::create(nextMarkerStamp, base, {},
                                                anchors, markerValues);
  require(nextMarkers.has_value(), "replacement marker packet was invalid");
  auto replacementViewPacket = products(base, 4, 16);
  replacementViewPacket.markers = *nextMarkers;
  require(
      controller.publishProducts(std::move(replacementViewPacket)).has_value(),
      "replacement marker view packet was rejected");
  retained = controller.currentProducts();
  require(retained && retained->markers == *nextMarkers,
          "replacement marker view did not restore exact evidence");
  auto oldViewAtNewGeneration = products(base, 5, 17);
  oldViewAtNewGeneration.markers = *createdMarkers;
  auto reinserted =
      controller.publishProducts(std::move(oldViewAtNewGeneration));
  require(!reinserted && reinserted.error().code ==
                             "desktop.sketch.publication-marker-view",
          "an older marker view was reinserted at a new product generation");
  shutdownController(controller, "product controller shutdown failed");
  executor.requestShutdown();
  require(executor.waitUntilDrained(std::chrono::seconds{5}),
          "product executor did not drain");
  executor.join();
}

void verifySceneReplay() {
  SketchPreparationExecutor executor{{2, 1, 2}};
  SketchSceneItem item;
  SketchScenePublicationController controller{item, executor};
  const SceneStamp firstStamp = stamp(207, 1, 207, 207, 207, 1);
  const SceneStamp secondStamp = stamp(207, 2, 207, 207, 207, 2);
  auto first = scene(1, 207, firstStamp);
  auto second = scene(1, 208, secondStamp);
  require(controller.retarget(firstStamp.target).has_value(),
          "scene replay target was rejected");
  require(controller.publishProducts(products(second, 1, 207)).has_value(),
          "newer scene was rejected before replay");
  pumpUntil([&] { return controller.metrics().itemPublications == 1U; });
  require(controller.publishProducts(products(first, 2, 208)).has_value(),
          "history replay was mistaken for stale publication");
  pumpUntil([&] { return controller.metrics().itemPublications == 2U; });
  const auto current = controller.currentProducts();
  require(current && current->scene == first,
          "history replay did not retain the exact restored scene");
  shutdownController(controller, "scene replay controller shutdown failed");
  executor.requestShutdown();
  require(executor.waitUntilDrained(std::chrono::seconds{5}),
          "scene replay executor did not drain");
  executor.join();
}

void verifyRetargetPreservesLastEvidence() {
  SketchPreparationExecutor executor{{2, 1, 2}};
  SketchSceneItem item;
  SketchScenePublicationController controller{item, executor};
  const SceneStamp firstStamp = stamp(207, 1, 207, 207, 207, 1);
  auto first = scene(1, 207, firstStamp);
  require(controller.retarget(firstStamp.target).has_value(),
          "first evidence target was rejected");
  require(controller.publishProducts(products(first, 1, 1)).has_value(),
          "first evidence packet was rejected");
  pumpUntil([&] { return controller.metrics().itemPublications == 1U; });
  require(controller.currentProducts() != nullptr,
          "accepted evidence was not retained");

  const SceneTarget nextTarget = stamp(208, 1, 208, 208, 208, 1).target;
  require(controller.retarget(nextTarget).has_value(),
          "replacement evidence target was rejected");
  require(controller.currentProducts() == nullptr,
          "retarget retained a packet for the previous publication target");
  shutdownController(controller, "evidence controller shutdown failed");
  executor.requestShutdown();
  require(executor.waitUntilDrained(std::chrono::seconds{5}),
          "evidence executor did not drain");
  executor.join();
}

void verifyMultiViewIsolation() {
  SketchPreparationExecutor executor{{3, 2, 2}};
  std::array<RecordingSink, 3> sinks;
  std::array<SketchPreparationSubscription, 3> subscriptions = [&] {
    std::array<std::optional<SketchPreparationSubscription>, 3> created;
    for (std::size_t index = 0; index < created.size(); ++index) {
      auto subscription = executor.subscribe(sinks[index], sinks[index]);
      require(subscription.has_value(), "view subscription was rejected");
      created[index] = *subscription;
    }
    return std::array<SketchPreparationSubscription, 3>{
        *created[0], *created[1], *created[2]};
  }();
  const SceneStamp baseStamp = stamp(209, 1, 209, 209, 209, 1);
  auto base = scene(2, 209, baseStamp);
  for (std::size_t index = 0; index < subscriptions.size(); ++index) {
    SketchProductPreparationOptions options{};
    options.picking.maximumLeafTargets += static_cast<std::uint32_t>(index);
    require(executor
                .submit(subscriptions[index],
                        ownedProducts(productStamp(baseStamp.target, index + 1U,
                                                   index + 1U),
                                      base),
                        options)
                .has_value(),
            "view submission was rejected");
  }
  require(executor.invalidate(subscriptions[1]).has_value(),
          "view-local cancellation was rejected");
  pumpUntil([&] {
    return sinks[0].deliveries() != 0U && sinks[2].deliveries() != 0U;
  });
  require(sinks[1].deliveries() == 0U,
          "one view's cancelled epoch leaked into its callback");
  require(executor.metrics().maximumActivePreparations <= 2U,
          "multi-view work exceeded the fixed worker pool");
  for (const auto subscription : subscriptions)
    require(executor.unsubscribe(subscription).has_value(),
            "view unsubscribe was rejected");
  executor.requestShutdown();
  require(executor.waitUntilDrained(std::chrono::seconds{5}),
          "multi-view executor did not drain");
  executor.join();
}

void verifyTerminalShutdownDrain() {
  bool rejectedLimits = false;
  try {
    SketchPreparationExecutor invalid{{0, 1, 1}, failedPreparation};
  } catch (const std::invalid_argument &) {
    rejectedLimits = true;
  }
  require(rejectedLimits, "zero executor limits were accepted");

  std::atomic_uint64_t faults = 0;
  const auto faulting = [&](const SketchPreparationRequest &,
                            std::shared_ptr<const PreparedSketchProducts>,
                            std::stop_token)
      -> Result<std::shared_ptr<const PreparedSketchProducts>> {
    const std::uint64_t fault = faults.fetch_add(1, std::memory_order_relaxed);
    if (fault == 0U)
      throw std::runtime_error("generated standard exception");
    if (fault == 1U)
      throw 7;
    return std::unexpected(
        diagnostic("desktop.test.preparation", "generated preparation result"));
  };
  SketchPreparationExecutor executor{{4, 2, 2}, faulting};
  std::array<RecordingSink, 4> sinks;
  const SceneStamp baseStamp = stamp(210, 1, 210, 210, 210, 1);
  auto base = scene(1, 210, baseStamp);
  std::array<SketchPreparationSubscription, 4> subscriptions = [&] {
    std::array<std::optional<SketchPreparationSubscription>, 4> created;
    for (std::size_t index = 0; index < sinks.size(); ++index) {
      auto subscription = executor.subscribe(sinks[index], sinks[index]);
      require(subscription.has_value(), "shutdown subscription was rejected");
      created[index] = *subscription;
    }
    return std::array<SketchPreparationSubscription, 4>{
        *created[0], *created[1], *created[2], *created[3]};
  }();
  require(executor
              .submit(subscriptions[0],
                      ownedProducts(productStamp(baseStamp.target, 1, 1), base),
                      {})
              .has_value(),
          "standard-fault submission was rejected");
  pumpUntil([&] { return sinks[0].deliveries() == 1U; });
  require(sinks[0].lastErrorCode() &&
              *sinks[0].lastErrorCode() ==
                  "desktop.sketch.preparation-exception",
          "standard preparation exception escaped its diagnostic boundary");
  require(executor
              .submit(subscriptions[1],
                      ownedProducts(productStamp(baseStamp.target, 2, 2), base),
                      {})
              .has_value(),
          "unknown-fault submission was rejected");
  pumpUntil([&] { return sinks[1].deliveries() == 1U; });
  require(sinks[1].lastErrorCode() &&
              *sinks[1].lastErrorCode() ==
                  "desktop.sketch.preparation-exception",
          "unknown preparation exception escaped its diagnostic boundary");
  for (std::size_t index = 2; index < sinks.size(); ++index) {
    require(executor
                .submit(subscriptions[index],
                        ownedProducts(productStamp(baseStamp.target, index + 1U,
                                                   index + 1U),
                                      base),
                        {})
                .has_value(),
            "shutdown submission was rejected");
  }
  executor.requestShutdown();
  require(executor.waitUntilDrained(std::chrono::seconds{5}),
          "terminal shutdown did not drain all slots");
  executor.join();
  const auto before = [&] {
    std::uint64_t result = 0;
    for (const auto &sink : sinks)
      result += sink.deliveries();
    return result;
  }();
  QCoreApplication::processEvents(QEventLoop::AllEvents);
  std::uint64_t after = 0;
  for (const auto &sink : sinks)
    after += sink.deliveries();
  const auto metrics = executor.metrics();
  require(before == after && metrics.leasedSubscriptions == 0U &&
              metrics.activePreparations == 0U &&
              metrics.pendingPreparations == 0U &&
              metrics.pendingCompletions == 0U &&
              preparationMetricsReconcile(metrics),
          "callbacks or artifacts survived terminal shutdown");
}

} // namespace

int main(int argc, char **argv) {
  try {
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    QGuiApplication application{argc, argv};
    const auto profile = kearne::testkit::propertyProfile();
    verifyProductGenerationInvariant();
    verifyCompletePreparedPacket();
    verifyThrowingAndReentrantSinks();
    verifyStaleDeliveryCannotConsumeReplacement();
    verifyInternalFaultContainment();
    verifyPreparedResultValidation();
    verifyPrepareLifecycleRejection();
    verifyExecutorLifetimeObservation();
    verifySinkLifetimeIdentity();
    verifyControllerShutdownAffinity();
    verifyControllerGlobalGenerationClock();
    verifyMaximumCapacityRingWrap();
    verifyMovedLifetimeRetirement();
    verifyGeneratedExecutorModel(profile);
    verifyNonblockingUnsubscribeAndAba();
    verifyCoalescedBoundedUiDrain(profile);
    verifySchedulingFailureRecovery();
    verifyControllerShutdownRetiresLatestProduct();
    verifyWorkerSideStaleRelease();
    verifyBoundedWorkerRetirement();
    verifyGlobalProductStamp();
    verifySceneReplay();
    verifyRetargetPreservesLastEvidence();
    verifyMultiViewIsolation();
    verifyTerminalShutdownDrain();
    std::cout << "sketch scene publication contract passed\n";
    return EXIT_SUCCESS;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
