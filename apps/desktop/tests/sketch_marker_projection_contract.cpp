#include "sketch_marker_projection.hpp"
#include "sketch_scene_fixture.hpp"

#include <kearne/testkit/property.hpp>

#include <QGuiApplication>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace {

struct ProjectionAllocationControl {
  std::atomic_bool reached = false;
  std::atomic_bool release = false;
  std::size_t allocation = 0U;
  std::size_t blockAt = 0U;
  std::size_t failAt = 0U;
};

thread_local ProjectionAllocationControl *projectionAllocationControl = nullptr;

void *allocateMemory(std::size_t size) {
  return std::malloc(size == 0U ? 1U : size);
}

void releaseMemory(void *memory) { std::free(memory); }

} // namespace

void *operator new(std::size_t size) {
  if (projectionAllocationControl) {
    ProjectionAllocationControl *control = projectionAllocationControl;
    ++control->allocation;
    if (control->allocation == control->failAt) {
      projectionAllocationControl = nullptr;
      throw std::bad_alloc{};
    }
    if (control->allocation == control->blockAt) {
      projectionAllocationControl = nullptr;
      control->reached.store(true, std::memory_order_release);
      while (!control->release.load(std::memory_order_acquire))
        std::this_thread::yield();
    }
  }
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

SketchEditSessionHandle editSession(std::uint64_t value) {
  auto created = SketchEditSessionHandle::create(value);
  require(created.has_value(), "marker edit-session fixture was invalid");
  return *created;
}

SketchToolInstanceHandle toolInstance(std::uint64_t value) {
  auto created = SketchToolInstanceHandle::create(value);
  require(created.has_value(), "marker tool-instance fixture was invalid");
  return *created;
}

SketchProvisionalGeneration provisionalGeneration(std::uint64_t value) {
  auto created = SketchProvisionalGeneration::create(value);
  require(created.has_value(), "marker provisional generation was invalid");
  return *created;
}

SketchProvisionalPrimitiveHandle provisionalHandle(std::uint32_t value) {
  auto created = SketchProvisionalPrimitiveHandle::create(value);
  require(created.has_value(), "marker provisional handle was invalid");
  return *created;
}

SketchMarkerGeneration markerGeneration(std::uint64_t value) {
  auto created = SketchMarkerGeneration::create(value);
  require(created.has_value(), "marker generation fixture was invalid");
  return *created;
}

SketchMarkerViewGeneration markerView(std::uint64_t value) {
  auto created = SketchMarkerViewGeneration::create(value);
  require(created.has_value(), "marker view fixture was invalid");
  return *created;
}

SketchMarkerHandle markerHandle(std::uint32_t value) {
  auto created = SketchMarkerHandle::create(value);
  require(created.has_value(), "marker handle fixture was invalid");
  return *created;
}

SketchProvisionalTarget provisionalTarget(const SceneStamp &base) {
  return {base, editSession(61U), toolInstance(62U)};
}

SketchProvisionalStamp provisionalStamp(const SceneStamp &base) {
  return {provisionalTarget(base), provisionalGeneration(1U),
          digest<SketchProvisionalDigest>(63U)};
}

std::shared_ptr<const SketchProvisionalGeometry>
provisionalGeometry(const SceneStamp &base) {
  const std::array primitives{
      PackedSketchProvisionalPrimitive{
          provisionalHandle(1U), std::array{Point2d{-0.02, 0.01}, Point2d{}},
          std::uint8_t{1U}, SketchPrimitiveKind::Point,
          SketchProvisionalClassification::Regular, 0.0, 0.0, 0.0},
      PackedSketchProvisionalPrimitive{
          provisionalHandle(2U),
          std::array{Point2d{-0.01, -0.01}, Point2d{0.04, 0.05}},
          std::uint8_t{2U}, SketchPrimitiveKind::Line,
          SketchProvisionalClassification::Regular, 0.0, 0.0, 0.0},
  };
  auto created =
      SketchProvisionalGeometry::create(provisionalStamp(base), primitives);
  require(created.has_value(), "marker provisional fixture was rejected");
  return std::move(*created);
}

SketchMarkerTarget markerTarget(
    const SceneStamp &base,
    const std::shared_ptr<const SketchProvisionalGeometry> &provisional) {
  return {base, SketchMarkerInteraction{editSession(61U), toolInstance(62U)},
          SketchProvisionalReference{provisional->stamp().generation,
                                     provisional->stamp().payload},
          markerView(64U)};
}

SketchMarkerTarget persistentMarkerTarget(const SceneStamp &base) {
  return {base, std::nullopt, std::nullopt, std::nullopt};
}

SketchMarkerStamp markerStamp(SketchMarkerTarget target,
                              std::uint64_t generation, std::uint64_t payload) {
  return {std::move(target), markerGeneration(generation),
          digest<SketchMarkerDigest>(payload)};
}

SketchMarkerPointLocation point(sketch::PointKey key) { return {key}; }

SketchMarkerCurveLocation curve(double parameter) { return {parameter}; }

struct MarkerInput {
  std::vector<SketchMarkerAnchor> anchors;
  std::vector<PackedSketchMarker> markers;

  void add(std::uint32_t handle, SketchMarkerKind kind, double valueSi,
           std::span<const SketchMarkerAnchor> markerAnchors,
           std::optional<SketchConstraintId> constraint = std::nullopt) {
    require(markerAnchors.size() <= std::numeric_limits<std::uint8_t>::max(),
            "marker fixture anchor range overflowed");
    markers.push_back({markerHandle(handle), std::move(constraint),
                       static_cast<std::uint32_t>(anchors.size()),
                       static_cast<std::uint8_t>(markerAnchors.size()), kind,
                       valueSi});
    anchors.insert(anchors.end(), markerAnchors.begin(), markerAnchors.end());
  }
};

std::shared_ptr<const SketchMarkerPacket>
markerPacket(SketchMarkerStamp identity,
             const std::shared_ptr<const SketchSceneSnapshot> &base,
             const std::shared_ptr<const SketchProvisionalGeometry> &draft,
             const MarkerInput &input) {
  auto created = SketchMarkerPacket::create(std::move(identity), base, draft,
                                            input.anchors, input.markers);
  require(created.has_value(), "marker packet fixture was rejected");
  return std::move(*created);
}

MarkerInput categoryFixture(const SketchSceneSnapshot &base) {
  const auto primitives = base.primitives();
  require(primitives.size() >= 2U &&
              primitives[0].kind == SketchPrimitiveKind::Point &&
              primitives[1].kind == SketchPrimitiveKind::Line,
          "marker category fixture has incompatible geometry");
  MarkerInput input;
  const std::array snapAnchors{SketchMarkerAnchor{
      SketchProvisionalMarkerAnchor{provisionalHandle(2U), curve(0.25)}}};
  input.add(50U, SketchMarkerKind::EndpointSnap, 0.0, snapAnchors);
  const std::array inferenceAnchors{SketchMarkerAnchor{
      SketchBaseMarkerAnchor{primitives[1].entity, curve(0.5)}}};
  input.add(20U, SketchMarkerKind::HorizontalInference, 0.0, inferenceAnchors);
  const std::array dimensionAnchors{
      SketchMarkerAnchor{SketchBaseMarkerAnchor{
          primitives[1].entity, point(sketch::PointKey::Start)}},
      SketchMarkerAnchor{SketchBaseMarkerAnchor{primitives[1].entity,
                                                point(sketch::PointKey::End)}}};
  input.add(40U, SketchMarkerKind::DistanceDimension, 0.05, dimensionAnchors,
            id<SketchConstraintId>(5'040U));
  const std::array constraintAnchors{
      SketchMarkerAnchor{SketchBaseMarkerAnchor{
          primitives[0].entity, point(sketch::PointKey::Point)}},
      SketchMarkerAnchor{SketchProvisionalMarkerAnchor{
          provisionalHandle(1U), point(sketch::PointKey::Point)}}};
  input.add(10U, SketchMarkerKind::CoincidentConstraint, 0.0, constraintAnchors,
            id<SketchConstraintId>(5'010U));
  const std::array freedomAnchors{
      SketchMarkerAnchor{SketchCanonicalMarkerAnchor{{-0.0, -0.02}}}};
  input.add(30U, SketchMarkerKind::TranslationDegreeOfFreedom, 0.0,
            freedomAnchors);
  return input;
}

void requireExactProjection(const PreparedSketchMarkers &prepared) {
  require(prepared.source() && prepared.base() &&
              prepared.source()->base() == prepared.base()->scene() &&
              prepared.markers().size() ==
                  prepared.source()->markers().size() &&
              prepared.anchors().size() == prepared.source()->anchors().size(),
          "prepared marker packet lost an exact source dependency");
  SketchMarkerRenderRecord foreign = prepared.markers().front();
  foreign.firstAnchor = prepared.markers().back().firstAnchor;
  foreign.anchorCount = prepared.markers().back().anchorCount;
  const auto ownedAnchors = prepared.markerAnchors(foreign.handle);
  require(ownedAnchors.size() == prepared.markers().front().anchorCount &&
              ownedAnchors.data() == prepared.anchors().data() +
                                         prepared.markers().front().firstAnchor,
          "foreign marker range metadata relabeled prepared anchors");
  if (prepared.markers().size() > 1U)
    require(foreign.firstAnchor != prepared.markers().front().firstAnchor &&
                ownedAnchors.data() !=
                    prepared.anchors().data() + foreign.firstAnchor,
            "in-bounds foreign marker metadata selected prepared anchors");
  for (std::size_t index = 0U; index < prepared.markers().size(); ++index) {
    const PackedSketchMarker &source = prepared.source()->markers()[index];
    const SketchMarkerRenderRecord &actual = prepared.markers()[index];
    const auto category = markerCategory(source.kind);
    require(category && actual.handle == source.handle &&
                actual.constraint == source.constraint &&
                actual.valueSi == source.valueSi &&
                actual.firstAnchor == source.firstAnchor &&
                actual.anchorCount == source.anchorCount &&
                actual.kind == source.kind && actual.category == *category &&
                prepared.markerAnchors(actual.handle).size() ==
                    source.anchorCount,
            "prepared marker changed ordering, ownership, or anchor ranges");
  }
  for (std::size_t index = 0U; index < prepared.anchors().size(); ++index) {
    auto expected = resolveSketchMarkerAnchor(
        prepared.source()->anchors()[index], *prepared.source()->base(),
        prepared.source()->provisional().get());
    require(expected && prepared.anchors()[index].positionMetres == *expected,
            "prepared marker anchor changed canonical SI position");
  }
  const auto &metrics = prepared.metrics();
  require(
      metrics.markerCount == prepared.markers().size() &&
          metrics.anchorCount == prepared.anchors().size() &&
          metrics.retainedBytes >=
              sizeof(PreparedSketchMarkers) +
                  prepared.markers().size() * sizeof(SketchMarkerRenderRecord) +
                  prepared.anchors().size() * sizeof(SketchMarkerAnchorPoint) &&
          metrics.scratchBytes == 0U &&
          metrics.peakBytes == metrics.retainedBytes,
      "prepared marker accounting is inconsistent");
}

void verifyCategoriesAnchorsAndOrder() {
  const SceneStamp baseStamp = stamp(91, 1, 91, 91, 91, 1);
  auto baseScene = scene(16U, 9'101U, baseStamp);
  auto base = preparedScene(baseScene, {});
  auto draft = provisionalGeometry(baseStamp);
  const MarkerInput input = categoryFixture(*baseScene);
  auto source =
      markerPacket(markerStamp(markerTarget(baseStamp, draft), 1U, 1U),
                   baseScene, draft, input);
  const Point2d *basePoints = baseScene->points().data();
  const PackedSketchProvisionalPrimitive *draftPrimitives =
      draft->primitives().data();
  auto prepared = prepareSketchMarkers(source, base);
  require(prepared.has_value() && (*prepared)->source() == source &&
              (*prepared)->base() == base,
          "valid marker projection was rejected");
  requireExactProjection(**prepared);
  require(std::ranges::is_sorted((*prepared)->markers(), {},
                                 &SketchMarkerRenderRecord::handle) &&
              (*prepared)->markers().front().handle.value() == 10U &&
              (*prepared)->markers().back().handle.value() == 50U &&
              baseScene->points().data() == basePoints &&
              draft->primitives().data() == draftPrimitives,
          "marker projection changed deterministic order or source geometry");

  std::array<bool, 5> categories{};
  for (const SketchMarkerRenderRecord &marker : (*prepared)->markers())
    categories[static_cast<std::size_t>(marker.category) - 1U] = true;
  require(std::ranges::all_of(categories, [](bool present) { return present; }),
          "marker contract omitted a render category");

  std::array<bool, 3> variants{};
  bool basePoint = false;
  bool baseCurve = false;
  bool provisionalPoint = false;
  bool provisionalCurve = false;
  for (const SketchMarkerAnchor &anchor : source->anchors()) {
    variants[anchor.index()] = true;
    if (const auto *evaluated = std::get_if<SketchBaseMarkerAnchor>(&anchor)) {
      basePoint =
          basePoint || std::holds_alternative<SketchMarkerPointLocation>(
                           evaluated->location);
      baseCurve =
          baseCurve || std::holds_alternative<SketchMarkerCurveLocation>(
                           evaluated->location);
    } else if (const auto *provisional =
                   std::get_if<SketchProvisionalMarkerAnchor>(&anchor)) {
      provisionalPoint =
          provisionalPoint || std::holds_alternative<SketchMarkerPointLocation>(
                                  provisional->location);
      provisionalCurve =
          provisionalCurve || std::holds_alternative<SketchMarkerCurveLocation>(
                                  provisional->location);
    }
  }
  require(std::ranges::all_of(variants, [](bool present) { return present; }) &&
              basePoint && baseCurve && provisionalPoint && provisionalCurve,
          "marker contract omitted an anchor variant");
}

struct LargeFixture {
  std::shared_ptr<const SketchSceneSnapshot> scene;
  std::shared_ptr<const PreparedSketchScene> base;
  MarkerInput input;
  std::shared_ptr<const SketchMarkerPacket> source;
};

LargeFixture largeFixture(std::size_t count) {
  const SceneStamp baseStamp = stamp(92, 1, 92, 92, 92, 1);
  LargeFixture fixture;
  fixture.scene = scene(4U, 9'201U, baseStamp);
  fixture.base = preparedScene(fixture.scene, {});
  fixture.input.markers.reserve(count);
  fixture.input.anchors.reserve(count);
  for (std::size_t ordinal = count; ordinal > 0U; --ordinal) {
    const std::array anchors{SketchMarkerAnchor{
        SketchCanonicalMarkerAnchor{{static_cast<double>(ordinal) * 1.0e-6,
                                     -static_cast<double>(ordinal) * 2.0e-6}}}};
    fixture.input.add(static_cast<std::uint32_t>(ordinal),
                      SketchMarkerKind::TranslationDegreeOfFreedom, 0.0,
                      anchors);
  }
  fixture.source =
      markerPacket(markerStamp(persistentMarkerTarget(baseStamp), 1U, 1U),
                   fixture.scene, nullptr, fixture.input);
  return fixture;
}

std::size_t scaleCount(const testkit::PropertyProfile &profile) {
  return profile.iterations >= 1'000'000U ? 1'000'000U
         : profile.iterations >= 250'000U ? 100'000U
                                          : 10'000U;
}

void verifyScaleReuseAndCancellation(const testkit::PropertyProfile &profile) {
  const std::size_t count = scaleCount(profile);
  LargeFixture fixture = largeFixture(count);
  auto first = prepareSketchMarkers(fixture.source, fixture.base);
  require(first.has_value() && (*first)->markers().size() == count &&
              (*first)->anchors().size() == count &&
              (*first)->markers().front().handle.value() == 1U &&
              (*first)->markers().back().handle.value() ==
                  static_cast<std::uint32_t>(count),
          "scaled marker projection failed or changed source order");
  requireExactProjection(**first);

  auto reused = prepareSketchMarkers(fixture.source, fixture.base, {}, *first);
  require(reused.has_value() && *reused == *first,
          "unchanged marker source and base were not reused in constant work");

  auto replacementBase = preparedScene(fixture.scene, {});
  auto freshBase =
      prepareSketchMarkers(fixture.source, replacementBase, {}, *first);
  require(freshBase.has_value() && *freshBase != *first &&
              (*freshBase)->base() == replacementBase,
          "changed prepared base reused stale marker preparation");

  auto changedSource = markerPacket(
      markerStamp(persistentMarkerTarget(fixture.scene->stamp()), 2U, 2U),
      fixture.scene, nullptr, fixture.input);
  auto freshSource =
      prepareSketchMarkers(changedSource, fixture.base, {}, *first);
  require(freshSource.has_value() && *freshSource != *first &&
              (*freshSource)->source() == changedSource,
          "changed marker source reused stale marker preparation");

  std::stop_source stopped;
  static_cast<void>(stopped.request_stop());
  auto cancelled = prepareSketchMarkers(fixture.source, fixture.base, {}, {},
                                        stopped.get_token());
  require(!cancelled && cancelled.error().code ==
                            "desktop.sketch.marker-preparation-cancelled",
          "cancelled marker preparation published a packet");

  std::stop_source midWorkSource;
  ProjectionAllocationControl allocationGate;
  allocationGate.blockAt = 1U;
  std::optional<Result<std::shared_ptr<const PreparedSketchMarkers>>> result;
  std::thread worker([&] {
    projectionAllocationControl = &allocationGate;
    result.emplace(prepareSketchMarkers(fixture.source, fixture.base, {}, {},
                                        midWorkSource.get_token()));
  });
  while (!allocationGate.reached.load(std::memory_order_acquire))
    std::this_thread::yield();
  static_cast<void>(midWorkSource.request_stop());
  allocationGate.release.store(true, std::memory_order_release);
  worker.join();
  require(result && !*result &&
              result->error().code ==
                  "desktop.sketch.marker-preparation-cancelled",
          "mid-work marker preparation ignored cancellation");

  ProjectionAllocationControl allocationFailure;
  allocationFailure.failAt = 1U;
  projectionAllocationControl = &allocationFailure;
  auto failed = prepareSketchMarkers(fixture.source, fixture.base);
  projectionAllocationControl = nullptr;
  require(!failed && failed.error().code ==
                         "desktop.sketch.marker-projection-allocation",
          "marker allocation failure escaped its Result boundary");
}

void verifyDependencyAndBudgetRejection() {
  LargeFixture fixture = largeFixture(32U);
  MarkerInput emptyInput;
  auto emptySource = markerPacket(
      markerStamp(persistentMarkerTarget(fixture.scene->stamp()), 9U, 9U),
      fixture.scene, nullptr, emptyInput);
  auto empty = prepareSketchMarkers(emptySource, fixture.base);
  require(empty && (*empty)->markers().empty() && (*empty)->anchors().empty() &&
              (*empty)->markerAnchors(markerHandle(999U)).empty(),
          "empty marker projection or missing-handle lookup was invalid");
  auto nullSource = prepareSketchMarkers({}, fixture.base);
  require(!nullSource && nullSource.error().code ==
                             "desktop.sketch.marker-projection-null-source",
          "marker preparation accepted a null source");
  auto nullBase = prepareSketchMarkers(fixture.source, {});
  require(!nullBase && nullBase.error().code ==
                           "desktop.sketch.marker-projection-null-base",
          "marker preparation accepted a null prepared base");
  auto otherScene = scene(4U, 9'301U, stamp(93, 1, 93, 93, 93, 1));
  auto otherBase = preparedScene(otherScene, {});
  auto mismatched = prepareSketchMarkers(fixture.source, otherBase);
  require(!mismatched && mismatched.error().code ==
                             "desktop.sketch.marker-projection-base-mismatch",
          "marker preparation accepted a mismatched exact base");

  MarkerInput malformed;
  const std::array anchors{
      SketchMarkerAnchor{SketchCanonicalMarkerAnchor{{0.0, 0.0}}}};
  malformed.add(1U, static_cast<SketchMarkerKind>(255), 0.0, anchors);
  auto rejectedSource = SketchMarkerPacket::create(
      markerStamp(persistentMarkerTarget(fixture.scene->stamp()), 3U, 3U),
      fixture.scene, nullptr, malformed.anchors, malformed.markers);
  require(!rejectedSource && rejectedSource.error().code ==
                                 "render.sketch.marker-invalid-kind",
          "malformed marker source crossed the immutable packet boundary");

  auto measured = prepareSketchMarkers(fixture.source, fixture.base);
  require(measured.has_value(), "marker budget fixture was rejected");
  const PreparedSketchMarkerMetrics metrics = (*measured)->metrics();
  const SketchMarkerProjectionLimits exact{
      metrics.markerCount, metrics.anchorCount, metrics.retainedBytes,
      metrics.scratchBytes, metrics.peakBytes};
  require(prepareSketchMarkers(fixture.source, fixture.base, exact).has_value(),
          "exact marker projection limits were refused");

  auto limited = exact;
  --limited.maximumMarkerCount;
  auto markerLimit =
      prepareSketchMarkers(fixture.source, fixture.base, limited);
  require(!markerLimit && markerLimit.error().code ==
                              "desktop.sketch.marker-projection-marker-limit",
          "marker projection count limit was ignored");
  limited = exact;
  --limited.maximumAnchorCount;
  auto anchorLimit =
      prepareSketchMarkers(fixture.source, fixture.base, limited);
  require(!anchorLimit && anchorLimit.error().code ==
                              "desktop.sketch.marker-projection-anchor-limit",
          "marker projection anchor limit was ignored");
  limited = exact;
  --limited.maximumRetainedBytes;
  auto retained = prepareSketchMarkers(fixture.source, fixture.base, limited);
  require(!retained && retained.error().code ==
                           "desktop.sketch.marker-projection-retained-limit",
          "marker projection retained-byte limit was ignored");
  limited = exact;
  --limited.maximumPeakBytes;
  auto peak = prepareSketchMarkers(fixture.source, fixture.base, limited);
  require(!peak && peak.error().code ==
                       "desktop.sketch.marker-projection-peak-limit",
          "marker projection peak-byte limit was ignored");
}

void verifyGeneratedProjection(const testkit::PropertyProfile &profile) {
  testkit::PropertyProfile bounded = profile;
  bounded.iterations = static_cast<std::uint64_t>(
      std::ceil(std::cbrt(static_cast<double>(profile.iterations))));
  testkit::checkProperty(
      "native marker projection invariants", bounded,
      [](testkit::Random &random, std::uint64_t iteration) {
        const std::size_t count =
            static_cast<std::size_t>(random.next() % 480U + 1U);
        const SceneStamp baseStamp =
            stamp(100U + iteration, 1U, 100U + iteration, 100U + iteration,
                  100U + iteration, 1U);
        auto baseScene = scene(4U, random.next(), baseStamp);
        auto base = preparedScene(baseScene, {});
        MarkerInput input;
        input.markers.reserve(count);
        input.anchors.reserve(count);
        for (std::size_t ordinal = count; ordinal > 0U; --ordinal) {
          const std::array anchors{
              SketchMarkerAnchor{SketchCanonicalMarkerAnchor{
                  {random.between(-1.0, 1.0), random.between(-1.0, 1.0)}}}};
          input.add(static_cast<std::uint32_t>(ordinal),
                    SketchMarkerKind::RotationDegreeOfFreedom, 0.0, anchors);
        }
        auto source =
            markerPacket(markerStamp(persistentMarkerTarget(baseStamp), 1U, 1U),
                         baseScene, nullptr, input);
        auto first = prepareSketchMarkers(source, base);
        auto second = prepareSketchMarkers(source, base);
        require(
            first.has_value() && second.has_value() &&
                std::ranges::equal((*first)->markers(), (*second)->markers()) &&
                std::ranges::equal((*first)->anchors(), (*second)->anchors()),
            "generated marker projection was nondeterministic");
        requireExactProjection(**first);
      });
}

} // namespace

int main(int argc, char *argv[]) {
  try {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication application(argc, argv);
    const auto profile = kearne::testkit::propertyProfile();
    verifyCategoriesAnchorsAndOrder();
    verifyScaleReuseAndCancellation(profile);
    verifyDependencyAndBudgetRejection();
    verifyGeneratedProjection(profile);
    std::cout << "verified native marker projection at " << scaleCount(profile)
              << " scale and " << profile.iterations
              << " generated profile iterations\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
