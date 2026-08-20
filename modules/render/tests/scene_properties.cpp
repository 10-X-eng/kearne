#include "scene_generator.hpp"

#include <kearne/testkit/property.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <numbers>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>

namespace {
thread_local bool measureAllocations = false;
thread_local std::size_t measuredAllocations = 0;
struct AllocationGate {
  std::atomic<bool> reached = false;
  std::atomic<bool> release = false;
};
thread_local AllocationGate *allocationGate = nullptr;

#if defined(__GNUC__) || defined(__clang__)
[[gnu::noinline]]
#endif
void *allocateMemory(std::size_t size) {
  return std::malloc(size == 0 ? 1U : size);
}

#if defined(__GNUC__) || defined(__clang__)
[[gnu::noinline]]
#endif
void releaseMemory(void *memory) {
  std::free(memory);
}
} // namespace

void *operator new(std::size_t size) {
  if (allocationGate) {
    AllocationGate *gate = std::exchange(allocationGate, nullptr);
    gate->reached.store(true, std::memory_order_release);
    while (!gate->release.load(std::memory_order_acquire))
      std::this_thread::yield();
  }
  if (measureAllocations)
    ++measuredAllocations;
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
using namespace kearne::render::test;

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

SketchSceneEnvelope
fullEnvelope(std::shared_ptr<const SketchSceneSnapshot> snapshot) {
  auto envelope = SketchSceneEnvelope::full(std::move(snapshot));
  require(envelope.has_value(), "valid full scene envelope was rejected");
  return std::move(*envelope);
}

SketchSceneEnvelope
deltaEnvelope(std::shared_ptr<const SketchSceneDelta> delta) {
  auto envelope = SketchSceneEnvelope::delta(std::move(delta));
  require(envelope.has_value(), "valid delta scene envelope was rejected");
  return std::move(*envelope);
}

SketchSceneDelta emptyDelta(SceneStamp base, SceneStamp target) {
  auto delta = SketchSceneDelta::create(std::move(base), std::move(target),
                                        std::nullopt, {}, {});
  require(delta.has_value(), "valid empty scene delta was rejected");
  return std::move(*delta);
}

template <typename Dimension> Quantity<Dimension> quantity(double value) {
  auto result = Quantity<Dimension>::fromSi(value);
  require(result.has_value(), "generated quantity was rejected");
  return *result;
}

SketchPresentationGeneration presentationGeneration(std::uint64_t value) {
  auto result = SketchPresentationGeneration::create(value);
  require(result.has_value(), "generated presentation generation was invalid");
  return *result;
}

std::shared_ptr<const SketchPresentationOverlay>
presentation(std::shared_ptr<const SketchSceneSnapshot> base,
             std::uint64_t generation,
             std::span<const SketchOverlayRoleSetPtr> sets) {
  auto result = SketchPresentationOverlay::create(
      std::move(base), presentationGeneration(generation), sets);
  require(result.has_value(), "valid sketch presentation was rejected");
  return std::move(*result);
}

std::shared_ptr<const SketchOverlayRoleSet>
overlayRoleSet(std::shared_ptr<const SketchSceneSnapshot> base,
               SketchOverlayRole role,
               std::span<const SketchOverlayScope> scopes = {},
               SketchOverlayRoleSetLimits limits = {}) {
  auto result =
      SketchOverlayRoleSet::create(std::move(base), role, scopes, limits);
  require(result.has_value(), "valid sketch overlay role set was rejected");
  return std::move(*result);
}

using OverlayRoleSets = std::array<SketchOverlayRoleSetPtr, 4>;

OverlayRoleSets
overlayRoleSets(const std::shared_ptr<const SketchSceneSnapshot> &base,
                std::span<const SketchOverlayScope> hovered = {},
                std::span<const SketchOverlayScope> selected = {},
                std::span<const SketchOverlayScope> preview = {},
                std::span<const SketchOverlayScope> diagnostic = {}) {
  return {overlayRoleSet(base, SketchOverlayRole::Hovered, hovered),
          overlayRoleSet(base, SketchOverlayRole::Selected, selected),
          overlayRoleSet(base, SketchOverlayRole::Preview, preview),
          overlayRoleSet(base, SketchOverlayRole::Diagnostic, diagnostic)};
}

std::shared_ptr<const SketchPresentationOverlay>
presentation(const std::shared_ptr<const SketchSceneSnapshot> &base,
             std::uint64_t generation) {
  const OverlayRoleSets sets = overlayRoleSets(base);
  return presentation(base, generation, sets);
}

SketchEditSessionHandle editSession(std::uint64_t value) {
  auto result = SketchEditSessionHandle::create(value);
  require(result.has_value(), "generated edit-session handle was invalid");
  return *result;
}

SketchToolInstanceHandle toolInstance(std::uint64_t value) {
  auto result = SketchToolInstanceHandle::create(value);
  require(result.has_value(), "generated tool-instance handle was invalid");
  return *result;
}

SketchProvisionalGeneration provisionalGeneration(std::uint64_t value) {
  auto result = SketchProvisionalGeneration::create(value);
  require(result.has_value(), "generated provisional generation was invalid");
  return *result;
}

SketchProvisionalPrimitiveHandle provisionalHandle(std::uint32_t value) {
  auto result = SketchProvisionalPrimitiveHandle::create(value);
  require(result.has_value(), "generated provisional handle was invalid");
  return *result;
}

SketchProvisionalTarget provisionalTarget(SceneStamp base,
                                          std::uint64_t session = 1U,
                                          std::uint64_t tool = 1U) {
  return {std::move(base), editSession(session), toolInstance(tool)};
}

SketchProvisionalStamp provisionalStamp(SketchProvisionalTarget target,
                                        std::uint64_t generation,
                                        std::uint64_t payload) {
  return {std::move(target), provisionalGeneration(generation),
          digest<SketchProvisionalDigest>(payload)};
}

PackedSketchProvisionalPrimitive
provisionalPoint(std::uint32_t handle, Point2d point,
                 SketchProvisionalClassification classification =
                     SketchProvisionalClassification::Regular) {
  return {provisionalHandle(handle),
          {point, Point2d{}},
          std::uint8_t{1},
          SketchPrimitiveKind::Point,
          classification,
          0.0,
          0.0,
          0.0};
}

PackedSketchProvisionalPrimitive
provisionalLine(std::uint32_t handle, Point2d start, Point2d end,
                SketchProvisionalClassification classification =
                    SketchProvisionalClassification::Regular) {
  return {provisionalHandle(handle),
          {start, end},
          std::uint8_t{2},
          SketchPrimitiveKind::Line,
          classification,
          0.0,
          0.0,
          0.0};
}

PackedSketchProvisionalPrimitive
provisionalCircle(std::uint32_t handle, Point2d center, double radius,
                  SketchProvisionalClassification classification =
                      SketchProvisionalClassification::Regular) {
  return {provisionalHandle(handle),
          {center, Point2d{}},
          std::uint8_t{1},
          SketchPrimitiveKind::Circle,
          classification,
          radius,
          0.0,
          0.0};
}

PackedSketchProvisionalPrimitive
provisionalArc(std::uint32_t handle, Point2d center, double radius,
               double start, double sweep,
               SketchProvisionalClassification classification =
                   SketchProvisionalClassification::Regular) {
  return {provisionalHandle(handle),
          {center, Point2d{}},
          std::uint8_t{1},
          SketchPrimitiveKind::Arc,
          classification,
          radius,
          start,
          sweep};
}

std::shared_ptr<const SketchProvisionalGeometry> provisionalGeometry(
    SketchProvisionalStamp stamp,
    std::span<const PackedSketchProvisionalPrimitive> primitives = {},
    SketchProvisionalLimits limits = {}) {
  auto result =
      SketchProvisionalGeometry::create(std::move(stamp), primitives, limits);
  require(result.has_value(), "valid provisional geometry was rejected");
  return std::move(*result);
}

std::vector<PackedSketchProvisionalPrimitive> provisionalFixture() {
  return {
      provisionalArc(4U, {0.03, -0.02}, 0.01, -0.4, 1.2),
      provisionalPoint(1U, {-0.02, 0.01}),
      provisionalCircle(3U, {0.02, 0.03}, 0.008,
                        SketchProvisionalClassification::Construction),
      provisionalLine(2U, {-0.01, -0.01}, {0.04, 0.05}),
  };
}

SketchMarkerGeneration markerGeneration(std::uint64_t value) {
  auto result = SketchMarkerGeneration::create(value);
  require(result.has_value(), "generated marker generation was invalid");
  return *result;
}

SketchMarkerHandle markerHandle(std::uint32_t value) {
  auto result = SketchMarkerHandle::create(value);
  require(result.has_value(), "generated marker handle was invalid");
  return *result;
}

SketchMarkerViewGeneration markerViewGeneration(std::uint64_t value) {
  auto result = SketchMarkerViewGeneration::create(value);
  require(result.has_value(), "generated marker view generation was invalid");
  return *result;
}

SketchMarkerTarget markerTarget(
    SceneStamp base, std::uint64_t session = 1U, std::uint64_t tool = 1U,
    std::optional<SketchProvisionalReference> provisional = std::nullopt,
    std::uint64_t view = 1U) {
  return {std::move(base),
          SketchMarkerInteraction{editSession(session), toolInstance(tool)},
          std::move(provisional), markerViewGeneration(view)};
}

SketchMarkerTarget persistentMarkerTarget(SceneStamp base) {
  return {std::move(base), std::nullopt, std::nullopt, std::nullopt};
}

SketchProvisionalReference
provisionalReference(const SketchProvisionalGeometry &geometry) {
  return {geometry.stamp().generation, geometry.stamp().payload};
}

SketchMarkerStamp markerStamp(SketchMarkerTarget target,
                              std::uint64_t generation, std::uint64_t payload) {
  return {std::move(target), markerGeneration(generation),
          digest<SketchMarkerDigest>(payload)};
}

SketchMarkerPointLocation pointLocation(sketch::PointKey point) {
  return {point};
}

SketchMarkerCurveLocation curveLocation(double normalizedParameter) {
  return {normalizedParameter};
}

struct MarkerInput {
  std::vector<SketchMarkerAnchor> anchors;
  std::vector<PackedSketchMarker> markers;

  void add(std::uint32_t handle, SketchMarkerKind kind, double valueSi,
           std::initializer_list<SketchMarkerAnchor> markerAnchors,
           std::optional<SketchConstraintId> constraint = std::nullopt) {
    require(markerAnchors.size() <= std::numeric_limits<std::uint8_t>::max(),
            "test marker anchor arity overflowed");
    markers.push_back({markerHandle(handle), std::move(constraint),
                       static_cast<std::uint32_t>(anchors.size()),
                       static_cast<std::uint8_t>(markerAnchors.size()), kind,
                       valueSi});
    anchors.insert(anchors.end(), markerAnchors.begin(), markerAnchors.end());
  }
};

MarkerInput permuteMarkers(const MarkerInput &source,
                           std::span<const std::size_t> order) {
  MarkerInput result;
  result.markers.reserve(order.size());
  result.anchors.reserve(source.anchors.size());
  for (const std::size_t index : order) {
    const PackedSketchMarker &marker = source.markers[index];
    PackedSketchMarker copy = marker;
    copy.firstAnchor = static_cast<std::uint32_t>(result.anchors.size());
    result.markers.push_back(copy);
    const auto begin = source.anchors.begin() + marker.firstAnchor;
    result.anchors.insert(result.anchors.end(), begin,
                          begin + marker.anchorCount);
  }
  return result;
}

std::shared_ptr<const SketchMarkerPacket>
markerPacket(SketchMarkerStamp stamp,
             std::shared_ptr<const SketchSceneSnapshot> base,
             std::shared_ptr<const SketchProvisionalGeometry> provisional,
             const MarkerInput &input, SketchMarkerLimits limits = {}) {
  auto result = SketchMarkerPacket::create(
      std::move(stamp), std::move(base), std::move(provisional), input.anchors,
      input.markers, limits);
  require(result.has_value(), "valid sketch marker packet was rejected");
  return std::move(*result);
}

MarkerInput markerFixture(const SketchSceneSnapshot &base) {
  const auto primitives = base.primitives();
  require(primitives.size() >= 4U,
          "marker fixture base has insufficient primitives");
  MarkerInput input;
  input.add(5U, SketchMarkerKind::EndpointSnap, 0.0,
            {SketchCanonicalMarkerAnchor{{-0.0, 0.025}}});
  input.add(2U, SketchMarkerKind::HorizontalInference, 0.0,
            {SketchBaseMarkerAnchor{primitives[1].entity, curveLocation(0.5)}});
  input.add(4U, SketchMarkerKind::DistanceDimension, 0.05,
            {SketchBaseMarkerAnchor{primitives[1].entity,
                                    pointLocation(sketch::PointKey::Start)},
             SketchBaseMarkerAnchor{primitives[1].entity,
                                    pointLocation(sketch::PointKey::End)}},
            id<SketchConstraintId>(5'004U));
  input.add(
      1U, SketchMarkerKind::CoincidentConstraint, 0.0,
      {SketchBaseMarkerAnchor{primitives[0].entity,
                              pointLocation(sketch::PointKey::Point)},
       SketchProvisionalMarkerAnchor{provisionalHandle(2U),
                                     pointLocation(sketch::PointKey::Start)}},
      id<SketchConstraintId>(5'001U));
  input.add(3U, SketchMarkerKind::TranslationDegreeOfFreedom, 0.0,
            {SketchCanonicalMarkerAnchor{{0.04, -0.02}}});
  return input;
}

SketchPrimitiveHandle primitiveHandle(std::uint32_t value) {
  auto result = SketchPrimitiveHandle::create(value);
  require(result.has_value(), "generated primitive handle was invalid");
  return *result;
}

std::shared_ptr<const SketchSceneSnapshot>
markerResolutionScene(SceneStamp sceneStamp) {
  constexpr double remote = 1.0e12;
  std::vector<Point2d> points{{remote, -remote},
                              {remote + 128.0, -remote + 64.0},
                              {remote + 640.0, -remote + 320.0},
                              {remote + 1'024.0, -remote + 1'024.0},
                              {remote - 1'024.0, -remote - 1'024.0}};
  const auto flags =
      SketchPrimitiveFlags::Visible | SketchPrimitiveFlags::Selectable;
  std::vector<PackedSketchPrimitive> primitives{
      {id<SketchEntityId>(71U), primitiveHandle(1U), 0U, 0U,
       SketchPrimitiveKind::Point, flags, 0.0, 0.0, 0.0},
      {id<SketchEntityId>(72U), primitiveHandle(2U), 1U, 0U,
       SketchPrimitiveKind::Line, flags, 0.0, 0.0, 0.0},
      {id<SketchEntityId>(73U), primitiveHandle(3U), 3U, 0U,
       SketchPrimitiveKind::Circle, flags, 256.0, 0.0, 0.0},
      {id<SketchEntityId>(74U), primitiveHandle(4U), 4U, 0U,
       SketchPrimitiveKind::Arc, flags, 512.0, 0.35, -2.4}};
  auto palette = styles();
  palette.resize(1U);
  auto result =
      SketchSceneSnapshot::create(std::move(sceneStamp), std::move(palette),
                                  std::move(points), std::move(primitives));
  require(result.has_value(), "marker resolution scene was rejected");
  return std::make_shared<const SketchSceneSnapshot>(std::move(*result));
}

bool nearCoordinate(double actual, double expected, double maximumUlps = 8.0) {
  const double upward = std::abs(
      std::nextafter(expected, std::numeric_limits<double>::infinity()) -
      expected);
  const double downward = std::abs(
      expected -
      std::nextafter(expected, -std::numeric_limits<double>::infinity()));
  const double spacing =
      std::max({upward, downward, std::numeric_limits<double>::denorm_min()});
  return std::isfinite(actual) &&
         std::abs(actual - expected) <= maximumUlps * spacing;
}

bool nearPoint(Point2d actual, Point2d expected, double maximumUlps = 8.0) {
  return nearCoordinate(actual.x, expected.x, maximumUlps) &&
         nearCoordinate(actual.y, expected.y, maximumUlps);
}

Point2d referenceRadial(Point2d center, double radius, double angle) {
  const long double preciseRadius = static_cast<long double>(radius);
  const long double preciseAngle = static_cast<long double>(angle);
  return {static_cast<double>(static_cast<long double>(center.x) +
                              preciseRadius * std::cos(preciseAngle)),
          static_cast<double>(static_cast<long double>(center.y) +
                              preciseRadius * std::sin(preciseAngle))};
}

Point2d
requireResolvedMarker(const SketchMarkerAnchor &anchor,
                      const SketchSceneSnapshot &base,
                      const SketchProvisionalGeometry *provisional = nullptr) {
  auto result = resolveSketchMarkerAnchor(anchor, base, provisional);
  require(result.has_value(), "valid sketch marker anchor did not resolve");
  return *result;
}

void verifySketchMarkerAnchorResolution(
    const testkit::PropertyProfile &profile) {
  const SceneStamp baseStamp = stamp(47, 1, 2, 3, 4, 5);
  auto base = markerResolutionScene(baseStamp);
  constexpr double remote = 1.0e12;
  std::vector<PackedSketchProvisionalPrimitive> provisionalPrimitives{
      provisionalPoint(1U, {remote + 2'048.0, -remote + 2'048.0}),
      provisionalLine(2U, {remote + 3'000.0, -remote + 3'200.0},
                      {remote + 3'800.0, -remote + 3'600.0}),
      provisionalCircle(3U, {remote + 4'096.0, -remote + 4'096.0}, 384.0),
      provisionalArc(4U, {remote - 4'096.0, -remote - 4'096.0}, 768.0, -0.2,
                     -1.7)};
  auto provisional = provisionalGeometry(
      provisionalStamp(provisionalTarget(baseStamp, 6U, 7U), 8U, 9U),
      provisionalPrimitives);
  const auto basePrimitives = base->primitives();

  const std::array semanticAnchors{
      SketchMarkerAnchor{SketchBaseMarkerAnchor{
          basePrimitives[0].entity, pointLocation(sketch::PointKey::Point)}},
      SketchMarkerAnchor{SketchBaseMarkerAnchor{
          basePrimitives[1].entity, pointLocation(sketch::PointKey::Start)}},
      SketchMarkerAnchor{SketchBaseMarkerAnchor{
          basePrimitives[1].entity, pointLocation(sketch::PointKey::End)}},
      SketchMarkerAnchor{SketchBaseMarkerAnchor{
          basePrimitives[2].entity, pointLocation(sketch::PointKey::Center)}},
      SketchMarkerAnchor{SketchBaseMarkerAnchor{
          basePrimitives[3].entity, pointLocation(sketch::PointKey::Center)}},
      SketchMarkerAnchor{SketchBaseMarkerAnchor{
          basePrimitives[3].entity, pointLocation(sketch::PointKey::Start)}},
      SketchMarkerAnchor{SketchBaseMarkerAnchor{
          basePrimitives[3].entity, pointLocation(sketch::PointKey::End)}},
      SketchMarkerAnchor{SketchProvisionalMarkerAnchor{
          provisionalHandle(1U), pointLocation(sketch::PointKey::Point)}},
      SketchMarkerAnchor{SketchProvisionalMarkerAnchor{
          provisionalHandle(2U), pointLocation(sketch::PointKey::Start)}},
      SketchMarkerAnchor{SketchProvisionalMarkerAnchor{
          provisionalHandle(2U), pointLocation(sketch::PointKey::End)}},
      SketchMarkerAnchor{SketchProvisionalMarkerAnchor{
          provisionalHandle(3U), pointLocation(sketch::PointKey::Center)}},
      SketchMarkerAnchor{SketchProvisionalMarkerAnchor{
          provisionalHandle(4U), pointLocation(sketch::PointKey::Center)}},
      SketchMarkerAnchor{SketchProvisionalMarkerAnchor{
          provisionalHandle(4U), pointLocation(sketch::PointKey::Start)}},
      SketchMarkerAnchor{SketchProvisionalMarkerAnchor{
          provisionalHandle(4U), pointLocation(sketch::PointKey::End)}}};
  for (const SketchMarkerAnchor &anchor : semanticAnchors) {
    const Point2d first =
        requireResolvedMarker(anchor, *base, provisional.get());
    const Point2d second =
        requireResolvedMarker(anchor, *base, provisional.get());
    require(first == second,
            "semantic marker anchor resolution was not deterministic");
  }

  const SketchMarkerAnchor circleZero{
      SketchBaseMarkerAnchor{basePrimitives[2].entity, curveLocation(0.0)}};
  const SketchMarkerAnchor circleOne{
      SketchBaseMarkerAnchor{basePrimitives[2].entity, curveLocation(1.0)}};
  require(requireResolvedMarker(circleZero, *base) ==
              requireResolvedMarker(circleOne, *base),
          "circle curve seam did not resolve to one canonical point");
  const Point2d canonical =
      requireResolvedMarker(SketchCanonicalMarkerAnchor{{-0.0, -0.0}}, *base);
  require(canonical == Point2d{} && !std::signbit(canonical.x) &&
              !std::signbit(canonical.y),
          "canonical marker anchor retained negative zero");
  const SceneStamp otherBaseStamp = stamp(47, 2, 2, 3, 4, 6);
  auto crossBaseProvisional = provisionalGeometry(
      provisionalStamp(provisionalTarget(otherBaseStamp, 6U, 7U), 8U, 9U),
      provisionalPrimitives);
  auto crossBaseResolution = resolveSketchMarkerAnchor(
      SketchProvisionalMarkerAnchor{provisionalHandle(1U),
                                    pointLocation(sketch::PointKey::Point)},
      *base, crossBaseProvisional.get());
  require(!crossBaseResolution &&
              crossBaseResolution.error().code ==
                  "render.sketch.marker-provisional-base-mismatch",
          "marker anchor resolved a handle from another base scene");

  testkit::checkProperty(
      "marker anchors resolve every curve kind at remote coordinates", profile,
      [&](testkit::Random &random, std::uint64_t) {
        const double parameter = random.between(0.0, 1.0);
        const Point2d lineStart = base->points()[1U];
        const Point2d lineEnd = base->points()[2U];
        const Point2d expectedLine{
            static_cast<double>(std::lerp(static_cast<long double>(lineStart.x),
                                          static_cast<long double>(lineEnd.x),
                                          static_cast<long double>(parameter))),
            static_cast<double>(
                std::lerp(static_cast<long double>(lineStart.y),
                          static_cast<long double>(lineEnd.y),
                          static_cast<long double>(parameter)))};
        const Point2d actualLine = requireResolvedMarker(
            SketchBaseMarkerAnchor{basePrimitives[1].entity,
                                   curveLocation(parameter)},
            *base);

        const Point2d circleCenter = base->points()[3U];
        const double circleAngle =
            parameter == 1.0 ? 0.0 : 2.0 * std::numbers::pi * parameter;
        const Point2d actualCircle = requireResolvedMarker(
            SketchBaseMarkerAnchor{basePrimitives[2].entity,
                                   curveLocation(parameter)},
            *base);
        const Point2d expectedCircle =
            referenceRadial(circleCenter, 256.0, circleAngle);

        const auto &draftArc = provisionalPrimitives[3U];
        const double arcAngle =
            parameter == 0.0 ? draftArc.startAngleRadians
            : parameter == 1.0
                ? draftArc.startAngleRadians + draftArc.sweepAngleRadians
                : draftArc.startAngleRadians +
                      draftArc.sweepAngleRadians * parameter;
        const Point2d actualArc = requireResolvedMarker(
            SketchProvisionalMarkerAnchor{provisionalHandle(4U),
                                          curveLocation(parameter)},
            *base, provisional.get());
        const Point2d expectedArc =
            referenceRadial(draftArc.points[0], draftArc.radius, arcAngle);
        require(nearPoint(actualLine, expectedLine, 2.0) &&
                    nearPoint(actualCircle, expectedCircle) &&
                    nearPoint(actualArc, expectedArc) &&
                    actualArc == requireResolvedMarker(
                                     SketchProvisionalMarkerAnchor{
                                         provisionalHandle(4U),
                                         curveLocation(parameter)},
                                     *base, provisional.get()),
                "remote marker curve resolution lost precision or changed");
      });

  testkit::checkProperty(
      "invalid marker point and curve locations are refused", profile,
      [&](testkit::Random &, std::uint64_t index) {
        SketchMarkerAnchor anchor = SketchCanonicalMarkerAnchor{{0.0, 0.0}};
        const SketchProvisionalGeometry *dependency = provisional.get();
        const char *expected = "";
        switch (index % 12U) {
        case 0U:
          anchor =
              SketchBaseMarkerAnchor{id<SketchEntityId>(999'990U),
                                     pointLocation(sketch::PointKey::Point)};
          expected = "render.sketch.marker-unknown-base-entity";
          break;
        case 1U:
          anchor = SketchBaseMarkerAnchor{
              basePrimitives[1].entity, pointLocation(sketch::PointKey::Point)};
          expected = "render.sketch.marker-invalid-base-point";
          break;
        case 2U:
          anchor = SketchBaseMarkerAnchor{basePrimitives[0].entity,
                                          curveLocation(0.5)};
          expected = "render.sketch.marker-invalid-base-curve-location";
          break;
        case 3U:
          anchor = SketchBaseMarkerAnchor{
              basePrimitives[1].entity,
              curveLocation(std::numeric_limits<double>::quiet_NaN())};
          expected = "render.sketch.marker-invalid-base-curve-location";
          break;
        case 4U:
          anchor = SketchBaseMarkerAnchor{basePrimitives[2].entity,
                                          curveLocation(-0.001)};
          expected = "render.sketch.marker-invalid-base-curve-location";
          break;
        case 5U:
          anchor = SketchBaseMarkerAnchor{basePrimitives[3].entity,
                                          curveLocation(1.001)};
          expected = "render.sketch.marker-invalid-base-curve-location";
          break;
        case 6U:
          anchor = SketchProvisionalMarkerAnchor{
              provisionalHandle(1U), pointLocation(sketch::PointKey::Point)};
          dependency = nullptr;
          expected = "render.sketch.marker-missing-provisional";
          break;
        case 7U:
          anchor = SketchProvisionalMarkerAnchor{
              provisionalHandle(999U), pointLocation(sketch::PointKey::Point)};
          expected = "render.sketch.marker-unknown-provisional-primitive";
          break;
        case 8U:
          anchor = SketchProvisionalMarkerAnchor{
              provisionalHandle(3U), pointLocation(sketch::PointKey::Start)};
          expected = "render.sketch.marker-invalid-provisional-point";
          break;
        case 9U:
          anchor = SketchProvisionalMarkerAnchor{provisionalHandle(1U),
                                                 curveLocation(0.5)};
          expected = "render.sketch.marker-invalid-provisional-curve-location";
          break;
        case 10U:
          anchor = SketchProvisionalMarkerAnchor{
              provisionalHandle(2U),
              curveLocation(std::numeric_limits<double>::infinity())};
          expected = "render.sketch.marker-invalid-provisional-curve-location";
          break;
        case 11U:
          anchor = SketchCanonicalMarkerAnchor{
              {0.0, std::numeric_limits<double>::quiet_NaN()}};
          expected = "render.sketch.marker-invalid-canonical-point";
          break;
        }
        auto result = resolveSketchMarkerAnchor(anchor, *base, dependency);
        require(!result && result.error().code == expected,
                "invalid marker anchor location was accepted");
      });
}

void verifySketchProjection(const testkit::PropertyProfile &profile) {
  testkit::checkProperty(
      "typed sketch geometry projects without unit or identity loss", profile,
      [](testkit::Random &random, std::uint64_t index) {
        const SketchEntityId entityId = id<SketchEntityId>(index + 100U);
        const double x = random.between(-1.0e3, 1.0e3);
        const double y = random.between(-1.0e3, 1.0e3);
        const bool construction = index % 5U == 0U;
        std::vector<sketch::Entity> geometry;
        SketchPrimitiveKind expectedKind = SketchPrimitiveKind::Point;
        switch (index % 4U) {
        case 0:
          geometry.push_back(
              sketch::PointEntity{entityId,
                                  {quantity<Length>(x), quantity<Length>(y)},
                                  construction});
          break;
        case 1:
          expectedKind = SketchPrimitiveKind::Line;
          geometry.push_back(sketch::LineEntity{
              entityId,
              {quantity<Length>(x), quantity<Length>(y)},
              {quantity<Length>(x + 0.25), quantity<Length>(y + 0.5)},
              construction});
          break;
        case 2:
          expectedKind = SketchPrimitiveKind::Circle;
          geometry.push_back(
              sketch::CircleEntity{entityId,
                                   {quantity<Length>(x), quantity<Length>(y)},
                                   quantity<Length>(0.5),
                                   construction});
          break;
        case 3:
          expectedKind = SketchPrimitiveKind::Arc;
          geometry.push_back(
              sketch::ArcEntity{entityId,
                                {quantity<Length>(x), quantity<Length>(y)},
                                quantity<Length>(0.5),
                                quantity<Angle>(-std::numbers::pi / 3.0),
                                quantity<Angle>(std::numbers::pi / 2.0),
                                construction});
          break;
        }
        auto projected = projectSketchScene(
            stamp(9, index + 1U, 1, 1, 1, index + 1U), geometry);
        require(projected.has_value(),
                "valid typed sketch projection was rejected");
        const PackedSketchPrimitive &primitive =
            projected->primitives().front();
        const std::uint16_t expectedStyle = construction ? 1U : 0U;
        require(
            primitive.entity == entityId && primitive.kind == expectedKind &&
                primitive.style == expectedStyle &&
                projected->styles().size() == 2U &&
                projected->styles()[0].role == SketchStyleRole::Regular &&
                projected->styles()[1].role == SketchStyleRole::Construction &&
                projected->points().front() == Point2d{x, y},
            "typed sketch projection changed semantic geometry");
      });
}

void verifySketchPresentation(const testkit::PropertyProfile &profile) {
  require(!SketchPresentationGeneration::create(0),
          "zero presentation generation was accepted");

  const SceneStamp baseStamp = stamp(41, 8, 4, 5, 6, 7);
  auto base = scene(4, 400, baseStamp);
  const SketchEntityId point = base->primitives()[0].entity;
  const SketchEntityId line = base->primitives()[1].entity;
  const SketchEntityId circle = base->primitives()[2].entity;
  const SketchEntityId arc = base->primitives()[3].entity;
  require(base->findPrimitive(point) == &base->primitives()[0] &&
              base->findPrimitive(arc) == &base->primitives()[3] &&
              !base->findPrimitive(id<SketchEntityId>(999'991U)),
          "immutable semantic index returned the wrong primitive");

  auto empty = presentation(base, 1);
  require(
      empty->resolve({line, std::nullopt}) == SketchStyleRole::Construction &&
          empty->resolve({circle, std::nullopt}) == SketchStyleRole::Regular,
      "empty overlay changed base geometry roles");

  const std::vector<SketchOverlayScope> hovered{
      {point, std::nullopt},         {line, sketch::PointKey::Start},
      {line, sketch::PointKey::End}, {circle, std::nullopt},
      {circle, std::nullopt},
  };
  const std::array selected{SketchOverlayScope{point, std::nullopt},
                            SketchOverlayScope{line, std::nullopt}};
  const std::array preview{SketchOverlayScope{point, std::nullopt},
                           SketchOverlayScope{line, sketch::PointKey::Start}};
  const std::array diagnostic{SketchOverlayScope{point, std::nullopt}};

  const SceneStamp savedStamp = base->stamp();
  const Point2d *savedPoints = base->points().data();
  const PackedSketchPrimitive *savedPrimitives = base->primitives().data();
  const std::size_t savedSemanticBytes = base->semanticIndexBytes();
  auto pickIndex = SketchPickIndex::build(base);
  require(pickIndex.has_value(), "presentation base pick index was rejected");

  testkit::checkProperty(
      "sketch overlay role sets normalize independently", profile,
      [&](testkit::Random &random, std::uint64_t index) {
        std::vector<SketchOverlayScope> permuted = hovered;
        for (std::size_t position = permuted.size(); position > 1U;
             --position) {
          const std::size_t other =
              static_cast<std::size_t>(random.next() % position);
          std::swap(permuted[position - 1U], permuted[other]);
        }
        const std::uint64_t generation = index + 2U;
        const OverlayRoleSets canonicalSets =
            overlayRoleSets(base, hovered, selected, preview, diagnostic);
        const OverlayRoleSets normalizedSets =
            overlayRoleSets(base, permuted, selected, preview, diagnostic);
        auto canonical = presentation(base, generation, canonicalSets);
        auto normalized = presentation(base, generation, normalizedSets);
        require(canonical->payloadDigest() == normalized->payloadDigest() &&
                    canonicalSets[0]->digest() == normalizedSets[0]->digest() &&
                    std::ranges::equal(canonicalSets[0]->scopes(),
                                       normalizedSets[0]->scopes()) &&
                    normalizedSets[0]->scopes().size() == 4U,
                "role-set normalization depended on input order or duplicates");
        require(normalized->resolve({point, std::nullopt}) ==
                        SketchStyleRole::Diagnostic &&
                    normalized->resolve({line, sketch::PointKey::Start}) ==
                        SketchStyleRole::Preview &&
                    normalized->resolve({line, sketch::PointKey::End}) ==
                        SketchStyleRole::Selected &&
                    normalized->resolve({circle, std::nullopt}) ==
                        SketchStyleRole::Hovered &&
                    normalized->resolve({arc, std::nullopt}) ==
                        SketchStyleRole::Regular,
                "overlay precedence disagrees with the semantic contract");
        require(normalized->base().get() == base.get() &&
                    base->stamp() == savedStamp &&
                    base->points().data() == savedPoints &&
                    base->primitives().data() == savedPrimitives &&
                    base->semanticIndexBytes() == savedSemanticBytes &&
                    &pickIndex->scene() == base.get(),
                "presentation mutation rebuilt or changed immutable base data");
      });

  const std::array unknownScope{
      SketchOverlayScope{id<SketchEntityId>(999'992U), std::nullopt}};
  auto unknown = SketchOverlayRoleSet::create(base, SketchOverlayRole::Selected,
                                              unknownScope);
  require(!unknown &&
              unknown.error().code == "render.sketch.overlay-unknown-entity",
          "overlay role set accepted an unknown semantic entity");
  const std::array invalidPointScope{
      SketchOverlayScope{point, sketch::PointKey::Start}};
  auto invalidPoint = SketchOverlayRoleSet::create(
      base, SketchOverlayRole::Selected, invalidPointScope);
  require(!invalidPoint && invalidPoint.error().code ==
                               "render.sketch.overlay-invalid-point",
          "overlay role set accepted an invalid semantic point key");
  auto invalidRole = SketchOverlayRoleSet::create(
      base, static_cast<SketchOverlayRole>(99), diagnostic);
  require(!invalidRole &&
              invalidRole.error().code == "render.sketch.overlay-invalid-role",
          "overlay role set accepted an invalid presentation role");

  const OverlayRoleSets validSets =
      overlayRoleSets(base, hovered, selected, preview, diagnostic);
  auto wrongCount = SketchPresentationOverlay::create(
      base, presentationGeneration(2),
      std::span<const SketchOverlayRoleSetPtr>{validSets}.first<3>());
  require(!wrongCount &&
              wrongCount.error().code == "render.sketch.overlay-role-set-count",
          "overlay accepted an absent role set");
  OverlayRoleSets missing = validSets;
  missing[0].reset();
  auto absent = SketchPresentationOverlay::create(
      base, presentationGeneration(2), missing);
  require(!absent &&
              absent.error().code == "render.sketch.overlay-missing-role-set",
          "overlay accepted a null role set");
  OverlayRoleSets duplicated = validSets;
  duplicated[1] = duplicated[0];
  auto duplicate = SketchPresentationOverlay::create(
      base, presentationGeneration(2), duplicated);
  require(!duplicate && duplicate.error().code ==
                            "render.sketch.overlay-duplicate-role-set",
          "overlay accepted duplicate role sets");
  auto equivalentBase = scene(4, 400, baseStamp);
  OverlayRoleSets wrongBase = validSets;
  wrongBase[0] = overlayRoleSet(equivalentBase, SketchOverlayRole::Hovered);
  auto mismatched = SketchPresentationOverlay::create(
      base, presentationGeneration(2), wrongBase);
  require(!mismatched &&
              mismatched.error().code == "render.sketch.overlay-role-set-base",
          "overlay accepted a role set from a different scene instance");
}

void verifySketchPresentationScaling() {
  for (const std::size_t count : {1U, 100U, 10'000U}) {
    auto base =
        scene(count, 500 + count, stamp(42, count, 4, 5, 6, 500 + count));
    std::vector<SketchOverlayScope> input;
    input.reserve(count);
    for (const PackedSketchPrimitive &primitive : base->primitives())
      input.push_back({primitive.entity, std::nullopt});
    auto selected = overlayRoleSet(base, SketchOverlayRole::Selected, input);
    OverlayRoleSets sets = overlayRoleSets(base);
    sets[1] = selected;
    auto overlay = presentation(base, count, sets);
    const std::size_t scopeBytes = count * sizeof(SketchOverlayScope);
    const std::size_t expectedScratch =
        (count + (count > 1U ? count : 0U)) * sizeof(SketchOverlayScope);
    require(selected->scopes().size() == count &&
                selected->inputBytes() == scopeBytes &&
                selected->retainedBytes() ==
                    sizeof(SketchOverlayRoleSet) + scopeBytes &&
                selected->scratchBytes() == expectedScratch &&
                selected->peakBuildBytes() ==
                    selected->retainedBytes() + selected->scratchBytes() &&
                base->semanticIndexBytes() >= count * sizeof(std::uint32_t) &&
                base->semanticIndexBytes() <=
                    count * 2U * sizeof(std::uint32_t),
            "overlay or semantic index violated its linear memory profile");
    for (const std::size_t ordinal : {std::size_t{0}, count / 2U, count - 1U}) {
      const SketchEntityId entity = base->primitives()[ordinal].entity;
      require(base->findPrimitive(entity) == &base->primitives()[ordinal] &&
                  overlay->resolve({entity, std::nullopt}) ==
                      SketchStyleRole::Selected,
              "scaled semantic lookup returned the wrong presentation");
    }

    const SketchOverlayRoleSetLimits exact{
        count, selected->inputBytes(), selected->retainedBytes(),
        selected->scratchBytes(), selected->peakBuildBytes()};
    require(SketchOverlayRoleSet::create(base, SketchOverlayRole::Selected,
                                         input, exact)
                .has_value(),
            "exact overlay role-set byte limits were refused");
    if (count > 0U) {
      auto limited = exact;
      --limited.maximumScopeCount;
      auto countLimited = SketchOverlayRoleSet::create(
          base, SketchOverlayRole::Selected, input, limited);
      require(!countLimited && countLimited.error().code ==
                                   "render.sketch.overlay-count-limit",
              "overlay role-set count limit was ignored");
      limited = exact;
      --limited.maximumInputBytes;
      auto inputLimited = SketchOverlayRoleSet::create(
          base, SketchOverlayRole::Selected, input, limited);
      require(!inputLimited && inputLimited.error().code ==
                                   "render.sketch.overlay-input-limit",
              "overlay role-set input byte limit was ignored");
      limited = exact;
      --limited.maximumRetainedBytes;
      auto retainedLimited = SketchOverlayRoleSet::create(
          base, SketchOverlayRole::Selected, input, limited);
      require(!retainedLimited && retainedLimited.error().code ==
                                      "render.sketch.overlay-memory-limit",
              "overlay role-set retained byte limit was ignored");
      limited = exact;
      --limited.maximumScratchBytes;
      auto scratchLimited = SketchOverlayRoleSet::create(
          base, SketchOverlayRole::Selected, input, limited);
      require(!scratchLimited && scratchLimited.error().code ==
                                     "render.sketch.overlay-scratch-limit",
              "overlay role-set scratch byte limit was ignored");
      limited = exact;
      --limited.maximumPeakBuildBytes;
      auto peakLimited = SketchOverlayRoleSet::create(
          base, SketchOverlayRole::Selected, input, limited);
      require(!peakLimited && peakLimited.error().code ==
                                  "render.sketch.overlay-peak-build-limit",
              "overlay role-set peak byte limit was ignored");
    }
  }

  auto base = scene(10'000U, 912, stamp(42, 20'000U, 4, 5, 6, 912));
  std::vector<SketchOverlayScope> selectedScopes;
  selectedScopes.reserve(base->primitives().size());
  for (const PackedSketchPrimitive &primitive : base->primitives())
    selectedScopes.push_back({primitive.entity, std::nullopt});
  const auto selected =
      overlayRoleSet(base, SketchOverlayRole::Selected, selectedScopes);
  const SketchOverlayScope *selectedData = selected->scopes().data();
  OverlayRoleSets stable = overlayRoleSets(base);
  stable[1] = selected;
  for (std::uint64_t index = 0U; index < 10'000U; ++index) {
    const std::array hovered{SketchOverlayScope{
        base->primitives()[index % base->primitives().size()].entity,
        std::nullopt}};
    stable[0] = overlayRoleSet(base, SketchOverlayRole::Hovered, hovered);
    auto overlay = presentation(base, index + 1U, stable);
    require(
        overlay->roleSet(SketchOverlayRole::Selected).get() == selected.get() &&
            overlay->roleSet(SketchOverlayRole::Selected)->scopes().data() ==
                selectedData &&
            overlay->roleSet(SketchOverlayRole::Hovered)->scopes().size() == 1U,
        "hover replacement copied or replaced the stable selection set");
  }

  std::stop_source stopped;
  stopped.request_stop();
  auto preCancelled =
      SketchOverlayRoleSet::create(base, SketchOverlayRole::Selected,
                                   selectedScopes, {}, stopped.get_token());
  require(!preCancelled &&
              preCancelled.error().code == "render.sketch.overlay-cancelled",
          "pre-cancelled overlay role-set construction published a set");

  std::stop_source sourceStop;
  AllocationGate gate;
  std::optional<Result<std::shared_ptr<const SketchOverlayRoleSet>>>
      concurrentBuild;
  std::thread builder([&] {
    allocationGate = &gate;
    concurrentBuild.emplace(SketchOverlayRoleSet::create(
        base, SketchOverlayRole::Selected, selectedScopes, {},
        sourceStop.get_token()));
  });
  while (!gate.reached.load(std::memory_order_acquire))
    std::this_thread::yield();
  sourceStop.request_stop();
  gate.release.store(true, std::memory_order_release);
  builder.join();
  require(concurrentBuild && !*concurrentBuild &&
              concurrentBuild->error().code ==
                  "render.sketch.overlay-cancelled",
          "mid-build cancellation published an overlay role set");
}

void verifyLatestSketchPresentation(const testkit::PropertyProfile &profile) {
  const SceneStamp sceneStamp = stamp(43, 10, 70, 71, 72, 73);
  auto base = scene(4, 600, sceneStamp);
  const SketchEntityId entity = base->primitives().front().entity;
  const std::array scope{SketchOverlayScope{entity, std::nullopt}};
  OverlayRoleSets selected = overlayRoleSets(base, {}, scope);
  OverlayRoleSets preview = overlayRoleSets(base, {}, {}, scope);

  LatestSketchPresentation publisher{sceneStamp};
  auto first = publisher.publish(presentation(base, 2, selected));
  require(first && *first == SketchOverlayDecision::Accepted &&
              publisher.retainedCount() == 1U,
          "latest presentation rejected its first generation");

  auto equivalentBase = scene(4, 600, sceneStamp);
  const OverlayRoleSets equivalentSelected =
      overlayRoleSets(equivalentBase, {}, scope);
  auto duplicate =
      publisher.publish(presentation(equivalentBase, 2, equivalentSelected));
  require(duplicate && *duplicate == SketchOverlayDecision::Duplicate,
          "normalized presentation duplicate was not detected");
  auto conflict = publisher.publish(presentation(base, 2, preview));
  require(conflict && *conflict == SketchOverlayDecision::GenerationConflict,
          "same-generation presentation conflict was not detected");
  auto stale = publisher.publish(presentation(base, 1, preview));
  require(stale && *stale == SketchOverlayDecision::StaleGeneration,
          "stale presentation generation was accepted");
  auto newer = publisher.publish(presentation(base, 3, preview));
  require(newer && *newer == SketchOverlayDecision::Accepted &&
              publisher.latest()->generation().value() == 3U &&
              publisher.retainedCount() == 1U,
          "latest-only presentation did not replace its generation");

  const std::array mismatchedStamps{
      stamp(44, 10, 70, 71, 72, 73),  stamp(43, 10, 700, 71, 72, 73),
      stamp(43, 10, 70, 710, 72, 73), stamp(43, 10, 70, 71, 720, 73),
      stamp(43, 11, 70, 71, 72, 73),  stamp(43, 10, 70, 71, 72, 730),
  };
  for (std::size_t index = 0; index < mismatchedStamps.size(); ++index) {
    auto mismatchedBase = scene(0, 700 + index, mismatchedStamps[index]);
    auto decision = publisher.publish(presentation(mismatchedBase, 4));
    require(decision && *decision == SketchOverlayDecision::StaleScene &&
                publisher.latest()->generation().value() == 3U,
            "publisher accepted an independently mismatched scene stamp");
  }
  require(!publisher.publish(nullptr) && publisher.retainedCount() == 1U,
          "publisher accepted a null presentation");

  testkit::checkProperty(
      "latest overlay rejects stale conflicts and accepts duplicates", profile,
      [&](testkit::Random &, std::uint64_t index) {
        LatestSketchPresentation generatedPublisher{sceneStamp};
        const std::uint64_t generation = index + 2U;
        auto accepted = generatedPublisher.publish(
            presentation(base, generation, selected));
        auto generatedDuplicate = generatedPublisher.publish(
            presentation(base, generation, selected));
        auto generatedConflict =
            generatedPublisher.publish(presentation(base, generation, preview));
        auto generatedStale = generatedPublisher.publish(
            presentation(base, generation - 1U, preview));
        require(accepted && *accepted == SketchOverlayDecision::Accepted &&
                    generatedDuplicate &&
                    *generatedDuplicate == SketchOverlayDecision::Duplicate &&
                    generatedConflict &&
                    *generatedConflict ==
                        SketchOverlayDecision::GenerationConflict &&
                    generatedStale &&
                    *generatedStale == SketchOverlayDecision::StaleGeneration,
                "generated latest-overlay ordering contract failed");
      });

  publisher.retarget(sceneStamp);
  require(publisher.retainedCount() == 1U,
          "same-stamp retarget discarded the current presentation");
  const SceneStamp nextScene = stamp(43, 11, 70, 71, 72, 74);
  publisher.retarget(nextScene);
  require(publisher.retainedCount() == 0U && !publisher.latest(),
          "new scene stamp retained stale presentation state");
}

void verifySketchProvisionalGeometry(const testkit::PropertyProfile &profile) {
  require(!SketchEditSessionHandle::create(0U),
          "zero edit-session handle was accepted");
  require(!SketchToolInstanceHandle::create(0U),
          "zero tool-instance handle was accepted");
  require(!SketchProvisionalGeneration::create(0U),
          "zero provisional generation was accepted");
  require(!SketchProvisionalPrimitiveHandle::create(0U),
          "zero provisional primitive handle was accepted");

  const SceneStamp baseStamp = stamp(45, 9, 4, 5, 6, 7);
  auto base = scene(8, 800, baseStamp);
  const Point2d *basePoints = base->points().data();
  const PackedSketchPrimitive *basePrimitives = base->primitives().data();
  auto basePickIndex = SketchPickIndex::build(base);
  require(basePickIndex.has_value(),
          "provisional test base pick index was rejected");
  const auto source = provisionalFixture();

  testkit::checkProperty(
      "provisional geometry is canonical under input permutation", profile,
      [&](testkit::Random &random, std::uint64_t index) {
        std::vector<PackedSketchProvisionalPrimitive> permuted = source;
        for (std::size_t position = permuted.size(); position > 1U;
             --position) {
          const std::size_t other =
              static_cast<std::size_t>(random.next() % position);
          std::swap(permuted[position - 1U], permuted[other]);
        }
        const auto identity = provisionalStamp(
            provisionalTarget(baseStamp, 20U, 30U), index + 1U, index + 100U);
        auto canonical = provisionalGeometry(identity, source);
        auto normalized = provisionalGeometry(identity, permuted);
        require(std::ranges::equal(canonical->primitives(),
                                   normalized->primitives()) &&
                    normalized->primitives().size() == source.size(),
                "provisional normalization depended on input order");
        for (std::uint32_t handle = 1U; handle <= source.size(); ++handle) {
          const auto typed = provisionalHandle(handle);
          require(normalized->findPrimitive(typed) ==
                      &normalized->primitives()[handle - 1U],
                  "provisional deterministic lookup returned the wrong slot");
        }
        require(normalized->stamp() == identity && base->stamp() == baseStamp &&
                    base->points().data() == basePoints &&
                    base->primitives().data() == basePrimitives &&
                    &basePickIndex->scene() == base.get(),
                "provisional geometry changed evaluated base state");
      });

  testkit::checkProperty(
      "invalid provisional geometry is refused", profile,
      [&](testkit::Random &random, std::uint64_t index) {
        const double finiteValue = random.between(-10.0, 10.0);
        PackedSketchProvisionalPrimitive invalid =
            provisionalPoint(1U, {finiteValue, finiteValue + 1.0});
        const char *expected = "";
        switch (index % 13U) {
        case 0U:
          invalid.kind = static_cast<SketchPrimitiveKind>(99);
          expected = "render.sketch.provisional-invalid-kind";
          break;
        case 1U:
          invalid.pointCount = std::uint8_t{2};
          expected = "render.sketch.provisional-point-arity";
          break;
        case 2U:
          invalid.points[1] = {1.0, 1.0};
          expected = "render.sketch.provisional-unused-point";
          break;
        case 3U:
          invalid.points[0].x = std::numeric_limits<double>::infinity();
          expected = "render.sketch.provisional-non-finite-point";
          break;
        case 4U:
          invalid.classification =
              static_cast<SketchProvisionalClassification>(99);
          expected = "render.sketch.provisional-classification";
          break;
        case 5U:
          invalid.radius = 1.0;
          expected = "render.sketch.provisional-unused-curve-parameters";
          break;
        case 6U:
          invalid = provisionalLine(1U, {1.0, 1.0}, {1.0, 1.0});
          expected = "render.sketch.provisional-degenerate-line";
          break;
        case 7U:
          invalid = provisionalCircle(1U, {0.0, 0.0}, 0.0);
          expected = "render.sketch.provisional-invalid-circle";
          break;
        case 8U:
          invalid = provisionalCircle(1U, {0.0, 0.0}, 1.0);
          invalid.sweepAngleRadians = 1.0;
          expected = "render.sketch.provisional-invalid-circle";
          break;
        case 9U:
          invalid = provisionalArc(1U, {0.0, 0.0}, 1.0, 0.0, 0.0);
          expected = "render.sketch.provisional-invalid-arc";
          break;
        case 10U:
          invalid = provisionalArc(1U, {0.0, 0.0}, 1.0, 0.0,
                                   2.0 * std::numbers::pi + 0.1);
          expected = "render.sketch.provisional-invalid-arc";
          break;
        case 11U:
          invalid =
              provisionalArc(1U, {0.0, 0.0}, 1.0,
                             std::numeric_limits<double>::infinity(), 1.0);
          expected = "render.sketch.provisional-non-finite-curve";
          break;
        case 12U:
          invalid =
              provisionalCircle(1U, {std::numeric_limits<double>::max(), 0.0},
                                std::numeric_limits<double>::max());
          expected = "render.sketch.provisional-unrepresentable-curve";
          break;
        }
        auto actual = SketchProvisionalGeometry::create(
            provisionalStamp(provisionalTarget(baseStamp), index + 1U,
                             index + 1U),
            std::span{&invalid, 1U});
        require(!actual && actual.error().code == expected,
                "generated invalid provisional primitive was accepted");
      });

  auto duplicateInput = source;
  duplicateInput.back().handle = duplicateInput.front().handle;
  auto duplicate = SketchProvisionalGeometry::create(
      provisionalStamp(provisionalTarget(baseStamp), 1U, 1U), duplicateInput);
  require(!duplicate && duplicate.error().code ==
                            "render.sketch.provisional-duplicate-handle",
          "provisional geometry accepted duplicate tool-local handles");
}

void verifySketchProvisionalBudgetsAndCancellation(
    const testkit::PropertyProfile &profile) {
  const SceneStamp baseStamp = stamp(46, 1, 4, 5, 6, 7);
  const auto source = provisionalFixture();
  const auto identity =
      provisionalStamp(provisionalTarget(baseStamp, 2U, 3U), 1U, 4U);
  auto measured = provisionalGeometry(identity, source);
  require(measured->inputBytes() ==
                  source.size() * sizeof(PackedSketchProvisionalPrimitive) &&
              measured->peakBuildBytes() ==
                  measured->retainedBytes() + measured->scratchBytes(),
          "provisional byte accounting is inconsistent");

  const SketchProvisionalLimits exact{
      measured->inputBytes(), measured->retainedBytes(),
      measured->scratchBytes(), measured->peakBuildBytes()};
  require(
      SketchProvisionalGeometry::create(identity, source, exact).has_value(),
      "exact provisional byte limits were refused");

  SketchProvisionalLimits limited = exact;
  --limited.maximumInputBytes;
  auto inputLimited =
      SketchProvisionalGeometry::create(identity, source, limited);
  require(!inputLimited && inputLimited.error().code ==
                               "render.sketch.provisional-input-limit",
          "provisional input byte limit was ignored");
  limited = exact;
  --limited.maximumRetainedBytes;
  auto retainedLimited =
      SketchProvisionalGeometry::create(identity, source, limited);
  require(!retainedLimited && retainedLimited.error().code ==
                                  "render.sketch.provisional-memory-limit",
          "provisional retained byte limit was ignored");
  limited = exact;
  --limited.maximumScratchBytes;
  auto scratchLimited =
      SketchProvisionalGeometry::create(identity, source, limited);
  require(!scratchLimited && scratchLimited.error().code ==
                                 "render.sketch.provisional-scratch-limit",
          "provisional scratch byte limit was ignored");
  limited = exact;
  --limited.maximumPeakBuildBytes;
  auto peakLimited =
      SketchProvisionalGeometry::create(identity, source, limited);
  require(!peakLimited && peakLimited.error().code ==
                              "render.sketch.provisional-peak-build-limit",
          "provisional peak byte limit was ignored");

  testkit::checkProperty(
      "provisional payload bytes scale with generated input", profile,
      [&](testkit::Random &random, std::uint64_t index) {
        const std::size_t count = static_cast<std::size_t>(index % 129U);
        std::vector<PackedSketchProvisionalPrimitive> generated;
        generated.reserve(count);
        for (std::size_t ordinal = 0; ordinal < count; ++ordinal) {
          const double x = random.between(-100.0, 100.0);
          generated.push_back(provisionalPoint(
              static_cast<std::uint32_t>(ordinal + 1U), {x, x + 0.5}));
        }
        auto actual = provisionalGeometry(
            provisionalStamp(provisionalTarget(baseStamp, 5U, 6U), index + 1U,
                             index + 10U),
            generated);
        require(actual->inputBytes() ==
                        count * sizeof(PackedSketchProvisionalPrimitive) &&
                    actual->peakBuildBytes() ==
                        actual->retainedBytes() + actual->scratchBytes() &&
                    actual->primitives().size() == count,
                "generated provisional byte accounting did not scale linearly");
      });

  std::stop_source stopped;
  stopped.request_stop();
  auto preCancelled = SketchProvisionalGeometry::create(identity, source, {},
                                                        stopped.get_token());
  require(!preCancelled && preCancelled.error().code ==
                               "render.sketch.provisional-cancelled",
          "pre-cancelled provisional construction published geometry");

  std::vector<PackedSketchProvisionalPrimitive> cancellable;
  cancellable.reserve(10'000U);
  for (std::uint32_t index = 0; index < 10'000U; ++index)
    cancellable.push_back(
        provisionalPoint(index + 1U, {static_cast<double>(index), 0.0}));
  std::stop_source sourceStop;
  AllocationGate gate;
  std::optional<Result<std::shared_ptr<const SketchProvisionalGeometry>>>
      concurrentBuild;
  std::thread builder([&] {
    allocationGate = &gate;
    concurrentBuild.emplace(SketchProvisionalGeometry::create(
        identity, cancellable, {}, sourceStop.get_token()));
  });
  while (!gate.reached.load(std::memory_order_acquire))
    std::this_thread::yield();
  sourceStop.request_stop();
  gate.release.store(true, std::memory_order_release);
  builder.join();
  require(concurrentBuild && !*concurrentBuild &&
              concurrentBuild->error().code ==
                  "render.sketch.provisional-cancelled",
          "mid-build cancellation published provisional geometry");
}

void verifyLatestSketchProvisionalGeometry(
    const testkit::PropertyProfile &profile) {
  const SceneStamp baseStamp = stamp(47, 10, 70, 71, 72, 73);
  const SketchProvisionalTarget target = provisionalTarget(baseStamp, 80U, 90U);
  const auto source = provisionalFixture();
  LatestSketchProvisionalGeometry publisher{target};

  auto first = provisionalGeometry(provisionalStamp(target, 2U, 100U), source);
  auto firstOffer = publisher.publish(first);
  require(firstOffer && *firstOffer == SketchProvisionalDecision::Accepted &&
              publisher.retainedCount() == 1U,
          "latest provisional publisher rejected its first generation");

  auto permuted = source;
  std::ranges::reverse(permuted);
  auto duplicate = publisher.publish(
      provisionalGeometry(provisionalStamp(target, 2U, 100U), permuted));
  require(duplicate && *duplicate == SketchProvisionalDecision::Duplicate,
          "canonical provisional duplicate was not detected");
  auto digestConflict = publisher.publish(
      provisionalGeometry(provisionalStamp(target, 2U, 101U), source));
  require(digestConflict &&
              *digestConflict == SketchProvisionalDecision::GenerationConflict,
          "same-generation provisional digest conflict was not detected");
  auto changed = source;
  changed.front().radius += 0.001;
  auto payloadConflict = publisher.publish(
      provisionalGeometry(provisionalStamp(target, 2U, 100U), changed));
  require(payloadConflict &&
              *payloadConflict == SketchProvisionalDecision::GenerationConflict,
          "same-digest provisional payload conflict was not detected");
  auto stale = publisher.publish(
      provisionalGeometry(provisionalStamp(target, 1U, 99U), source));
  require(stale && *stale == SketchProvisionalDecision::StaleGeneration,
          "stale provisional generation was accepted");
  auto newer = publisher.publish(
      provisionalGeometry(provisionalStamp(target, 3U, 102U), source));
  require(newer && *newer == SketchProvisionalDecision::Accepted &&
              publisher.latest()->stamp().generation.value() == 3U &&
              publisher.retainedCount() == 1U,
          "latest-only provisional state was not replaced");
  require(!publisher.publish(nullptr) && publisher.retainedCount() == 1U,
          "latest provisional publisher accepted null geometry");

  testkit::checkProperty(
      "provisional target rejects every independent identity mismatch", profile,
      [&](testkit::Random &, std::uint64_t index) {
        const std::uint64_t value = (index + 1U) * 32U;
        const SceneStamp expectedBase =
            stamp(value + 1U, value + 2U, value + 3U, value + 4U, value + 5U,
                  value + 6U);
        const SketchProvisionalTarget expected =
            provisionalTarget(expectedBase, value + 7U, value + 8U);
        SketchProvisionalTarget candidate = expected;
        switch (index % 8U) {
        case 0U:
          candidate.base.target.session =
              *RenderSessionHandle::create(value + 9U);
          break;
        case 1U:
          candidate.base.target.evaluatedPlane.attachmentBinding =
              id<ModelBindingId>(value + 9U);
          break;
        case 2U:
          candidate.base.target.evaluatedPlane.revision =
              digest<RevisionId>(value + 9U);
          break;
        case 3U:
          candidate.base.target.evaluation = digest<EvaluationKey>(value + 9U);
          break;
        case 4U:
          candidate.base.generation = *SceneGeneration::create(value + 9U);
          break;
        case 5U:
          candidate.base.digest = digest<SceneDigest>(value + 9U);
          break;
        case 6U:
          candidate.editSession = editSession(value + 9U);
          break;
        case 7U:
          candidate.toolInstance = toolInstance(value + 9U);
          break;
        }
        LatestSketchProvisionalGeometry exactPublisher{expected};
        auto decision = exactPublisher.publish(provisionalGeometry(
            provisionalStamp(candidate, 1U, value + 10U), source));
        require(decision &&
                    *decision == SketchProvisionalDecision::StaleTarget &&
                    exactPublisher.retainedCount() == 0U,
                "provisional target accepted an independent mismatch");
      });

  publisher.retarget(target);
  require(publisher.retainedCount() == 1U,
          "same-target provisional retarget discarded current geometry");
  const SketchProvisionalTarget nextTarget =
      provisionalTarget(baseStamp, 80U, 91U);
  publisher.retarget(nextTarget);
  require(publisher.retainedCount() == 0U && !publisher.latest(),
          "new provisional target retained stale geometry");
}

void verifySketchMarkerPacket(const testkit::PropertyProfile &profile) {
  require(!SketchMarkerGeneration::create(0U),
          "zero sketch marker generation was accepted");
  require(!SketchMarkerHandle::create(0U),
          "zero sketch marker handle was accepted");
  require(!SketchMarkerViewGeneration::create(0U),
          "zero sketch marker view generation was accepted");
  require(markerCategory(SketchMarkerKind::CoincidentConstraint) ==
                  SketchMarkerCategory::Constraint &&
              markerCategory(SketchMarkerKind::HorizontalInference) ==
                  SketchMarkerCategory::Inference &&
              markerCategory(SketchMarkerKind::TranslationDegreeOfFreedom) ==
                  SketchMarkerCategory::DegreeOfFreedom &&
              markerCategory(SketchMarkerKind::DistanceDimension) ==
                  SketchMarkerCategory::Dimension &&
              markerCategory(SketchMarkerKind::EndpointSnap) ==
                  SketchMarkerCategory::SnapCursor &&
              !markerCategory(static_cast<SketchMarkerKind>(255)),
          "sketch marker categories are incomplete or accept invalid kinds");

  constexpr std::array allKinds{
      SketchMarkerKind::CoincidentConstraint,
      SketchMarkerKind::HorizontalConstraint,
      SketchMarkerKind::VerticalConstraint,
      SketchMarkerKind::ParallelConstraint,
      SketchMarkerKind::PerpendicularConstraint,
      SketchMarkerKind::TangentConstraint,
      SketchMarkerKind::EqualConstraint,
      SketchMarkerKind::ConcentricConstraint,
      SketchMarkerKind::MidpointConstraint,
      SketchMarkerKind::FixedConstraint,
      SketchMarkerKind::CollinearConstraint,
      SketchMarkerKind::HorizontalInference,
      SketchMarkerKind::VerticalInference,
      SketchMarkerKind::ParallelInference,
      SketchMarkerKind::PerpendicularInference,
      SketchMarkerKind::TangentInference,
      SketchMarkerKind::CollinearInference,
      SketchMarkerKind::TranslationDegreeOfFreedom,
      SketchMarkerKind::RotationDegreeOfFreedom,
      SketchMarkerKind::DistanceDimension,
      SketchMarkerKind::HorizontalDistanceDimension,
      SketchMarkerKind::VerticalDistanceDimension,
      SketchMarkerKind::RadiusDimension,
      SketchMarkerKind::DiameterDimension,
      SketchMarkerKind::AngleDimension,
      SketchMarkerKind::EndpointSnap,
      SketchMarkerKind::MidpointSnap,
      SketchMarkerKind::CenterSnap,
      SketchMarkerKind::IntersectionSnap,
      SketchMarkerKind::QuadrantSnap,
      SketchMarkerKind::GridSnap,
  };

  const SceneStamp baseStamp = stamp(48, 1, 2, 3, 4, 5);
  auto base = scene(4, 900, baseStamp);
  const SketchProvisionalTarget draftTarget =
      provisionalTarget(baseStamp, 6U, 7U);
  auto draft = provisionalGeometry(provisionalStamp(draftTarget, 8U, 9U),
                                   provisionalFixture());
  const SketchMarkerTarget target =
      markerTarget(baseStamp, 6U, 7U, provisionalReference(*draft));
  const MarkerInput source = markerFixture(*base);
  const Point2d *basePoints = base->points().data();
  const PackedSketchProvisionalPrimitive *draftPrimitives =
      draft->primitives().data();

  testkit::checkProperty(
      "every sketch marker kind has valid generated packet coverage", profile,
      [&](testkit::Random &random, std::uint64_t index) {
        const SketchMarkerKind kind = allKinds[index % allKinds.size()];
        const auto category = markerCategory(kind);
        require(category.has_value(), "declared marker kind has no category");
        const bool semantic = *category == SketchMarkerCategory::Constraint ||
                              *category == SketchMarkerCategory::Dimension;
        const bool screenDerived =
            *category == SketchMarkerCategory::Inference ||
            *category == SketchMarkerCategory::SnapCursor;
        MarkerInput one;
        one.add(1U, kind,
                *category == SketchMarkerCategory::Dimension
                    ? random.between(-1.0e3, 1.0e3)
                    : 0.0,
                {SketchCanonicalMarkerAnchor{{0.01, -0.02}}},
                semantic ? std::optional{id<SketchConstraintId>(index + 1U)}
                         : std::nullopt);
        auto packet = SketchMarkerPacket::create(
            markerStamp(screenDerived ? markerTarget(baseStamp, 6U, 7U)
                                      : persistentMarkerTarget(baseStamp),
                        index + 1U, index + 1U),
            base, nullptr, one.anchors, one.markers);
        require(packet.has_value() && packet->get()->markers().size() == 1U,
                "declared marker kind failed packet validation");
      });

  testkit::checkProperty(
      "marker packet is canonical under whole-marker permutation", profile,
      [&](testkit::Random &random, std::uint64_t index) {
        std::vector<std::size_t> order(source.markers.size());
        std::iota(order.begin(), order.end(), 0U);
        for (std::size_t position = order.size(); position > 1U; --position) {
          const std::size_t other =
              static_cast<std::size_t>(random.next() % position);
          std::swap(order[position - 1U], order[other]);
        }
        const MarkerInput permuted = permuteMarkers(source, order);
        const auto identity = markerStamp(target, index + 1U, index + 100U);
        auto canonical = markerPacket(identity, base, draft, source);
        auto normalized = markerPacket(identity, base, draft, permuted);
        require(
            std::ranges::equal(canonical->markers(), normalized->markers()) &&
                std::ranges::equal(canonical->anchors(),
                                   normalized->anchors()) &&
                normalized->markers().size() == source.markers.size() &&
                normalized->anchors().size() == source.anchors.size(),
            "marker canonicalization depended on source order");
        for (std::uint32_t handle = 1U; handle <= source.markers.size();
             ++handle) {
          const PackedSketchMarker *marker =
              normalized->findMarker(markerHandle(handle));
          require(marker && !normalized->markerAnchors(marker->handle).empty(),
                  "marker lookup or packed anchor range was invalid");
        }
        const PackedSketchMarker *snap =
            normalized->findMarker(markerHandle(5U));
        require(snap, "canonical marker packet lost its snap cursor");
        PackedSketchMarker foreign = *snap;
        foreign.firstAnchor = normalized->markers().front().firstAnchor;
        foreign.anchorCount = normalized->markers().front().anchorCount;
        const auto ownedSnapAnchors = normalized->markerAnchors(foreign.handle);
        const auto *canonicalAnchor =
            std::get_if<SketchCanonicalMarkerAnchor>(&ownedSnapAnchors.front());
        const PackedSketchMarker *coincident =
            normalized->findConstraint(id<SketchConstraintId>(5'001U));
        const PackedSketchMarker *dimension =
            normalized->findConstraint(id<SketchConstraintId>(5'004U));
        require(
            foreign.firstAnchor != snap->firstAnchor &&
                ownedSnapAnchors.data() ==
                    normalized->anchors().data() + snap->firstAnchor &&
                ownedSnapAnchors.size() == snap->anchorCount &&
                normalized->markerAnchors(markerHandle(999'999U)).empty() &&
                canonicalAnchor && canonicalAnchor->point.x == 0.0 &&
                !std::signbit(canonicalAnchor->point.x) && coincident &&
                coincident->handle == markerHandle(1U) && dimension &&
                dimension->handle == markerHandle(4U) &&
                !normalized->findConstraint(id<SketchConstraintId>(999'999U)) &&
                normalized->base().get() == base.get() &&
                normalized->provisional().get() == draft.get() &&
                base->points().data() == basePoints &&
                draft->primitives().data() == draftPrimitives,
            "marker normalization changed canonical coordinates or exact "
            "dependencies");
      });

  testkit::checkProperty(
      "malformed sketch markers are refused", profile,
      [&](testkit::Random &, std::uint64_t index) {
        MarkerInput invalid;
        const char *expected = "";
        switch (index % 18U) {
        case 0U:
          invalid.add(1U, static_cast<SketchMarkerKind>(255), 0.0,
                      {SketchCanonicalMarkerAnchor{{0.0, 0.0}}});
          expected = "render.sketch.marker-invalid-kind";
          break;
        case 1U:
          invalid.add(1U, SketchMarkerKind::EndpointSnap, 0.0,
                      {SketchCanonicalMarkerAnchor{{0.0, 0.0}}});
          invalid.markers.front().firstAnchor = 1U;
          expected = "render.sketch.marker-non-packed-anchors";
          break;
        case 2U:
          invalid.markers.push_back({markerHandle(1U), std::nullopt, 0U, 0U,
                                     SketchMarkerKind::EndpointSnap, 0.0});
          expected = "render.sketch.marker-anchor-arity";
          break;
        case 3U:
          invalid.add(1U, SketchMarkerKind::EndpointSnap, 0.0,
                      {SketchCanonicalMarkerAnchor{{0.0, 0.0}},
                       SketchCanonicalMarkerAnchor{{1.0, 0.0}}});
          expected = "render.sketch.marker-anchor-arity";
          break;
        case 4U:
          invalid.add(1U, SketchMarkerKind::HorizontalConstraint, 1.0,
                      {SketchCanonicalMarkerAnchor{{0.0, 0.0}}});
          expected = "render.sketch.marker-invalid-value";
          break;
        case 5U:
          invalid.add(1U, SketchMarkerKind::DistanceDimension,
                      std::numeric_limits<double>::infinity(),
                      {SketchCanonicalMarkerAnchor{{0.0, 0.0}}});
          expected = "render.sketch.marker-invalid-value";
          break;
        case 6U:
          invalid.add(1U, SketchMarkerKind::EndpointSnap, 0.0,
                      {SketchCanonicalMarkerAnchor{{0.0, 0.0}}});
          invalid.anchors.push_back(SketchCanonicalMarkerAnchor{{1.0, 1.0}});
          expected = "render.sketch.marker-unused-anchors";
          break;
        case 7U:
          invalid.add(
              1U, SketchMarkerKind::FixedConstraint, 0.0,
              {SketchBaseMarkerAnchor{id<SketchEntityId>(999'991U),
                                      pointLocation(sketch::PointKey::Point)}},
              id<SketchConstraintId>(index + 1U));
          expected = "render.sketch.marker-unknown-base-entity";
          break;
        case 8U:
          invalid.add(
              1U, SketchMarkerKind::FixedConstraint, 0.0,
              {SketchBaseMarkerAnchor{base->primitives()[0].entity,
                                      pointLocation(sketch::PointKey::Start)}},
              id<SketchConstraintId>(index + 1U));
          expected = "render.sketch.marker-invalid-base-point";
          break;
        case 9U:
          invalid.add(1U, SketchMarkerKind::FixedConstraint, 0.0,
                      {SketchProvisionalMarkerAnchor{
                          provisionalHandle(999U),
                          pointLocation(sketch::PointKey::Point)}},
                      id<SketchConstraintId>(index + 1U));
          expected = "render.sketch.marker-unknown-provisional-primitive";
          break;
        case 10U:
          invalid.add(1U, SketchMarkerKind::FixedConstraint, 0.0,
                      {SketchProvisionalMarkerAnchor{
                          provisionalHandle(1U),
                          pointLocation(sketch::PointKey::Start)}},
                      id<SketchConstraintId>(index + 1U));
          expected = "render.sketch.marker-invalid-provisional-point";
          break;
        case 11U:
          invalid.add(1U, SketchMarkerKind::EndpointSnap, 0.0,
                      {SketchCanonicalMarkerAnchor{
                          {std::numeric_limits<double>::quiet_NaN(), 0.0}}});
          expected = "render.sketch.marker-invalid-canonical-point";
          break;
        case 12U:
          invalid.add(1U, SketchMarkerKind::FixedConstraint, 0.0,
                      {SketchCanonicalMarkerAnchor{{0.0, 0.0}}});
          expected = "render.sketch.marker-missing-constraint";
          break;
        case 13U:
          invalid.add(1U, SketchMarkerKind::GridSnap, 0.0,
                      {SketchCanonicalMarkerAnchor{{0.0, 0.0}}},
                      id<SketchConstraintId>(index + 1U));
          expected = "render.sketch.marker-unexpected-constraint";
          break;
        case 14U:
          invalid.add(1U, SketchMarkerKind::FixedConstraint, 0.0,
                      {SketchBaseMarkerAnchor{base->primitives()[0].entity,
                                              curveLocation(0.5)}},
                      id<SketchConstraintId>(index + 1U));
          expected = "render.sketch.marker-invalid-base-curve-location";
          break;
        case 15U:
          invalid.add(
              1U, SketchMarkerKind::FixedConstraint, 0.0,
              {SketchBaseMarkerAnchor{
                  base->primitives()[1].entity,
                  curveLocation(std::numeric_limits<double>::quiet_NaN())}},
              id<SketchConstraintId>(index + 1U));
          expected = "render.sketch.marker-invalid-base-curve-location";
          break;
        case 16U:
          invalid.add(1U, SketchMarkerKind::FixedConstraint, 0.0,
                      {SketchProvisionalMarkerAnchor{provisionalHandle(1U),
                                                     curveLocation(0.5)}},
                      id<SketchConstraintId>(index + 1U));
          expected = "render.sketch.marker-invalid-provisional-curve-location";
          break;
        case 17U:
          invalid.add(1U, SketchMarkerKind::FixedConstraint, 0.0,
                      {SketchProvisionalMarkerAnchor{provisionalHandle(2U),
                                                     curveLocation(1.001)}},
                      id<SketchConstraintId>(index + 1U));
          expected = "render.sketch.marker-invalid-provisional-curve-location";
          break;
        }
        auto actual = SketchMarkerPacket::create(
            markerStamp(target, index + 1U, index + 1U), base, draft,
            invalid.anchors, invalid.markers);
        require(!actual && actual.error().code == expected,
                "generated malformed sketch marker was accepted");
      });

  testkit::checkProperty(
      "marker identity retains only generated dependencies in use", profile,
      [&](testkit::Random &, std::uint64_t index) {
        MarkerInput input;
        SketchMarkerTarget candidate = persistentMarkerTarget(baseStamp);
        std::shared_ptr<const SketchProvisionalGeometry> dependency;
        const char *expected = "";
        switch (index % 4U) {
        case 0U:
          input.add(1U, SketchMarkerKind::FixedConstraint, 0.0,
                    {SketchCanonicalMarkerAnchor{{0.0, 0.0}}},
                    id<SketchConstraintId>(index + 1U));
          candidate = markerTarget(baseStamp, 6U, 7U);
          expected = "render.sketch.marker-unused-view";
          break;
        case 1U:
          input.add(1U, SketchMarkerKind::FixedConstraint, 0.0,
                    {SketchCanonicalMarkerAnchor{{0.0, 0.0}}},
                    id<SketchConstraintId>(index + 1U));
          candidate = markerTarget(baseStamp, 6U, 7U);
          candidate.view.reset();
          expected = "render.sketch.marker-unused-interaction";
          break;
        case 2U:
          input.add(1U, SketchMarkerKind::GridSnap, 0.0,
                    {SketchCanonicalMarkerAnchor{{0.0, 0.0}}});
          candidate = target;
          dependency = draft;
          expected = "render.sketch.marker-unused-provisional";
          break;
        case 3U:
          input.add(1U, SketchMarkerKind::GridSnap, 0.0,
                    {SketchProvisionalMarkerAnchor{
                        provisionalHandle(1U),
                        pointLocation(sketch::PointKey::Point)}});
          candidate = markerTarget(baseStamp, 6U, 7U);
          dependency = draft;
          expected = "render.sketch.marker-provisional-target-required";
          break;
        }
        auto actual = SketchMarkerPacket::create(
            markerStamp(candidate, index + 1U, index + 1U), base, dependency,
            input.anchors, input.markers);
        require(!actual && actual.error().code == expected,
                "generated marker packet retained an unused dependency");
      });

  MarkerInput duplicate;
  duplicate.add(1U, SketchMarkerKind::EndpointSnap, 0.0,
                {SketchCanonicalMarkerAnchor{{0.0, 0.0}}});
  duplicate.add(1U, SketchMarkerKind::CenterSnap, 0.0,
                {SketchCanonicalMarkerAnchor{{1.0, 1.0}}});
  auto duplicateResult = SketchMarkerPacket::create(
      markerStamp(markerTarget(baseStamp, 6U, 7U), 1U, 1U), base, nullptr,
      duplicate.anchors, duplicate.markers);
  require(!duplicateResult && duplicateResult.error().code ==
                                  "render.sketch.marker-duplicate-handle",
          "sketch marker packet accepted duplicate marker handles");

  MarkerInput duplicateConstraint;
  for (std::uint32_t handle : {1U, 2U})
    duplicateConstraint.add(
        handle, SketchMarkerKind::FixedConstraint, 0.0,
        {SketchCanonicalMarkerAnchor{{static_cast<double>(handle), 0.0}}},
        id<SketchConstraintId>(9'000U));
  auto duplicateSemantic = SketchMarkerPacket::create(
      markerStamp(persistentMarkerTarget(baseStamp), 1U, 1U), base, nullptr,
      duplicateConstraint.anchors, duplicateConstraint.markers);
  require(!duplicateSemantic && duplicateSemantic.error().code ==
                                    "render.sketch.marker-duplicate-constraint",
          "marker packet accepted duplicate canonical constraint identities");

  MarkerInput signedZero;
  signedZero.add(1U, SketchMarkerKind::FixedConstraint, 0.0,
                 {SketchBaseMarkerAnchor{base->primitives()[1].entity,
                                         curveLocation(-0.0)}},
                 id<SketchConstraintId>(9'001U));
  auto normalizedZero =
      markerPacket(markerStamp(persistentMarkerTarget(baseStamp), 2U, 2U), base,
                   nullptr, signedZero);
  const auto *baseAnchor =
      std::get_if<SketchBaseMarkerAnchor>(&normalizedZero->anchors().front());
  const auto *curve =
      baseAnchor ? std::get_if<SketchMarkerCurveLocation>(&baseAnchor->location)
                 : nullptr;
  require(curve && curve->normalizedParameter == 0.0 &&
              !std::signbit(curve->normalizedParameter),
          "marker packet retained a negative-zero curve parameter");
}

void verifySketchMarkerDependencies() {
  const SceneStamp baseStamp = stamp(49, 1, 2, 3, 4, 5);
  auto base = scene(4, 950, baseStamp);
  auto otherBase = scene(4, 951, stamp(49, 2, 2, 3, 4, 6));
  auto draft = provisionalGeometry(
      provisionalStamp(provisionalTarget(baseStamp, 6U, 7U), 8U, 9U),
      provisionalFixture());
  const SketchMarkerTarget target =
      markerTarget(baseStamp, 6U, 7U, provisionalReference(*draft));
  MarkerInput input;
  input.add(
      1U, SketchMarkerKind::EndpointSnap, 0.0,
      {SketchProvisionalMarkerAnchor{provisionalHandle(1U),
                                     pointLocation(sketch::PointKey::Point)}});

  auto missingBase =
      SketchMarkerPacket::create(markerStamp(target, 1U, 1U), nullptr, draft,
                                 input.anchors, input.markers);
  require(!missingBase &&
              missingBase.error().code == "render.sketch.marker-missing-base",
          "marker packet accepted a missing base dependency");
  auto wrongBase =
      SketchMarkerPacket::create(markerStamp(target, 1U, 1U), otherBase, draft,
                                 input.anchors, input.markers);
  require(!wrongBase &&
              wrongBase.error().code == "render.sketch.marker-base-mismatch",
          "marker packet accepted a mismatched base dependency");
  auto missingDraft = SketchMarkerPacket::create(
      markerStamp(target, 1U, 1U), base, nullptr, input.anchors, input.markers);
  require(!missingDraft && missingDraft.error().code ==
                               "render.sketch.marker-missing-provisional",
          "marker packet accepted a missing provisional dependency");
  const SketchMarkerTarget baseOnly = markerTarget(baseStamp, 6U, 7U);
  auto unexpectedDraft = SketchMarkerPacket::create(
      markerStamp(baseOnly, 1U, 1U), base, draft, input.anchors, input.markers);
  require(!unexpectedDraft &&
              unexpectedDraft.error().code ==
                  "render.sketch.marker-provisional-target-required",
          "marker packet accepted an unstamped provisional anchor");
  SketchMarkerTarget mismatched = target;
  mismatched.provisional = SketchProvisionalReference{
      provisionalGeneration(8U), digest<SketchProvisionalDigest>(99U)};
  auto wrongDraft =
      SketchMarkerPacket::create(markerStamp(mismatched, 1U, 1U), base, draft,
                                 input.anchors, input.markers);
  require(!wrongDraft && wrongDraft.error().code ==
                             "render.sketch.marker-provisional-mismatch",
          "marker packet accepted a mismatched provisional dependency");

  MarkerInput provisionalAnchor;
  provisionalAnchor.add(
      1U, SketchMarkerKind::FixedConstraint, 0.0,
      {SketchProvisionalMarkerAnchor{provisionalHandle(1U),
                                     pointLocation(sketch::PointKey::Point)}},
      id<SketchConstraintId>(9'001U));
  auto undeclaredAnchor = SketchMarkerPacket::create(
      markerStamp(baseOnly, 1U, 1U), base, nullptr, provisionalAnchor.anchors,
      provisionalAnchor.markers);
  require(!undeclaredAnchor && undeclaredAnchor.error().code ==
                                   "render.sketch.marker-missing-provisional",
          "base-only marker target accepted a provisional anchor");

  const SketchMarkerTarget persistentTarget = persistentMarkerTarget(baseStamp);
  MarkerInput persistent;
  persistent.add(1U, SketchMarkerKind::FixedConstraint, 0.0,
                 {SketchCanonicalMarkerAnchor{{0.0, 0.0}}},
                 id<SketchConstraintId>(9'101U));
  persistent.add(2U, SketchMarkerKind::DistanceDimension, 0.01,
                 {SketchCanonicalMarkerAnchor{{0.0, 0.0}},
                  SketchCanonicalMarkerAnchor{{0.01, 0.0}}},
                 id<SketchConstraintId>(9'102U));
  persistent.add(3U, SketchMarkerKind::TranslationDegreeOfFreedom, 0.0,
                 {SketchCanonicalMarkerAnchor{{0.02, 0.0}}});
  require(SketchMarkerPacket::create(markerStamp(persistentTarget, 1U, 1U),
                                     base, nullptr, persistent.anchors,
                                     persistent.markers)
              .has_value(),
          "persistent annotations required an active geometry tool");

  MarkerInput screenDerived;
  screenDerived.add(1U, SketchMarkerKind::GridSnap, 0.0,
                    {SketchCanonicalMarkerAnchor{{0.0, 0.0}}});
  auto noInteraction = SketchMarkerPacket::create(
      markerStamp(persistentTarget, 1U, 1U), base, nullptr,
      screenDerived.anchors, screenDerived.markers);
  require(!noInteraction && noInteraction.error().code ==
                                "render.sketch.marker-interaction-required",
          "screen-derived marker accepted no interaction identity");
  SketchMarkerTarget noView = baseOnly;
  noView.view.reset();
  auto unstampedView =
      SketchMarkerPacket::create(markerStamp(noView, 1U, 1U), base, nullptr,
                                 screenDerived.anchors, screenDerived.markers);
  require(!unstampedView && unstampedView.error().code ==
                                "render.sketch.marker-view-required",
          "screen-derived marker accepted no view generation");
  auto unusedView =
      SketchMarkerPacket::create(markerStamp(baseOnly, 1U, 1U), base, nullptr,
                                 persistent.anchors, persistent.markers);
  require(!unusedView &&
              unusedView.error().code == "render.sketch.marker-unused-view",
          "persistent-only marker packet accepted an unused view identity");
  SketchMarkerTarget bareInteraction = baseOnly;
  bareInteraction.view.reset();
  auto unusedInteraction = SketchMarkerPacket::create(
      markerStamp(bareInteraction, 1U, 1U), base, nullptr, persistent.anchors,
      persistent.markers);
  require(!unusedInteraction && unusedInteraction.error().code ==
                                    "render.sketch.marker-unused-interaction",
          "persistent-only marker packet accepted bare interaction identity");
  auto unusedProvisional =
      SketchMarkerPacket::create(markerStamp(target, 1U, 1U), base, draft,
                                 screenDerived.anchors, screenDerived.markers);
  require(!unusedProvisional && unusedProvisional.error().code ==
                                    "render.sketch.marker-unused-provisional",
          "marker packet accepted an unanchored provisional dependency");
  SketchMarkerTarget noDraftInteraction = persistentTarget;
  noDraftInteraction.provisional = provisionalReference(*draft);
  auto invalidDraftTarget = SketchMarkerPacket::create(
      markerStamp(noDraftInteraction, 1U, 1U), base, draft,
      provisionalAnchor.anchors, provisionalAnchor.markers);
  require(!invalidDraftTarget &&
              invalidDraftTarget.error().code ==
                  "render.sketch.marker-provisional-interaction",
          "provisional marker target accepted no edit/tool identity");
}

void verifySketchMarkerBudgetsAndCancellation() {
  const SceneStamp baseStamp = stamp(50, 1, 2, 3, 4, 5);
  auto base = scene(0, 1000, baseStamp);
  const SketchMarkerTarget target = markerTarget(baseStamp, 6U, 7U);
  for (const std::size_t count : {1U, 100U, 10'000U}) {
    MarkerInput input;
    input.markers.reserve(count);
    input.anchors.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
      input.add(
          static_cast<std::uint32_t>(index + 1U), SketchMarkerKind::GridSnap,
          0.0,
          {SketchCanonicalMarkerAnchor{{static_cast<double>(index), 0.0}}});
    }
    auto packet =
        markerPacket(markerStamp(target, count, count), base, nullptr, input);
    const std::size_t exactInput =
        count * (sizeof(PackedSketchMarker) + sizeof(SketchMarkerAnchor));
    require(packet->inputBytes() == exactInput &&
                packet->markers().size() == count &&
                packet->anchors().size() == count &&
                packet->retainedBytes() >=
                    sizeof(SketchMarkerPacket) + exactInput &&
                packet->retainedBytes() <=
                    sizeof(SketchMarkerPacket) + exactInput * 2U &&
                packet->scratchBytes() <=
                    count * sizeof(PackedSketchMarker) * 2U &&
                packet->peakBuildBytes() ==
                    packet->retainedBytes() + packet->scratchBytes(),
            "marker packet violated its bounded linear memory profile");

    const SketchMarkerLimits exact{count,
                                   count,
                                   packet->inputBytes(),
                                   packet->retainedBytes(),
                                   packet->scratchBytes(),
                                   packet->peakBuildBytes()};
    require(SketchMarkerPacket::create(markerStamp(target, count, count), base,
                                       nullptr, input.anchors, input.markers,
                                       exact)
                .has_value(),
            "exact sketch marker limits were refused");
    SketchMarkerLimits limited = exact;
    --limited.maximumMarkerCount;
    auto markerLimited = SketchMarkerPacket::create(
        markerStamp(target, count, count), base, nullptr, input.anchors,
        input.markers, limited);
    require(!markerLimited && markerLimited.error().code ==
                                  "render.sketch.marker-count-limit",
            "sketch marker count limit was ignored");
    limited = exact;
    --limited.maximumAnchorCount;
    auto anchorLimited = SketchMarkerPacket::create(
        markerStamp(target, count, count), base, nullptr, input.anchors,
        input.markers, limited);
    require(!anchorLimited && anchorLimited.error().code ==
                                  "render.sketch.marker-anchor-count-limit",
            "sketch marker anchor count limit was ignored");
    limited = exact;
    --limited.maximumInputBytes;
    auto inputLimited = SketchMarkerPacket::create(
        markerStamp(target, count, count), base, nullptr, input.anchors,
        input.markers, limited);
    require(!inputLimited &&
                inputLimited.error().code == "render.sketch.marker-input-limit",
            "sketch marker input byte limit was ignored");
    limited = exact;
    --limited.maximumRetainedBytes;
    auto memoryLimited = SketchMarkerPacket::create(
        markerStamp(target, count, count), base, nullptr, input.anchors,
        input.markers, limited);
    require(!memoryLimited && memoryLimited.error().code ==
                                  "render.sketch.marker-memory-limit",
            "sketch marker retained byte limit was ignored");
    limited = exact;
    if (limited.maximumScratchBytes > 0U) {
      --limited.maximumScratchBytes;
      auto scratchLimited = SketchMarkerPacket::create(
          markerStamp(target, count, count), base, nullptr, input.anchors,
          input.markers, limited);
      require(!scratchLimited && scratchLimited.error().code ==
                                     "render.sketch.marker-scratch-limit",
              "sketch marker scratch byte limit was ignored");
    }
    limited = exact;
    --limited.maximumPeakBuildBytes;
    auto peakLimited = SketchMarkerPacket::create(
        markerStamp(target, count, count), base, nullptr, input.anchors,
        input.markers, limited);
    require(!peakLimited && peakLimited.error().code ==
                                "render.sketch.marker-peak-build-limit",
            "sketch marker peak byte limit was ignored");
  }

  MarkerInput cancellable;
  cancellable.markers.reserve(10'000U);
  cancellable.anchors.reserve(10'000U);
  for (std::uint32_t index = 0U; index < 10'000U; ++index)
    cancellable.add(
        index + 1U, SketchMarkerKind::GridSnap, 0.0,
        {SketchCanonicalMarkerAnchor{{static_cast<double>(index), 0.0}}});
  std::stop_source stopped;
  stopped.request_stop();
  auto preCancelled = SketchMarkerPacket::create(
      markerStamp(target, 1U, 1U), base, nullptr, cancellable.anchors,
      cancellable.markers, {}, stopped.get_token());
  require(!preCancelled &&
              preCancelled.error().code == "render.sketch.marker-cancelled",
          "pre-cancelled marker construction published a packet");

  std::stop_source sourceStop;
  AllocationGate gate;
  std::optional<Result<std::shared_ptr<const SketchMarkerPacket>>>
      concurrentBuild;
  std::thread builder([&] {
    allocationGate = &gate;
    concurrentBuild.emplace(SketchMarkerPacket::create(
        markerStamp(target, 2U, 2U), base, nullptr, cancellable.anchors,
        cancellable.markers, {}, sourceStop.get_token()));
  });
  while (!gate.reached.load(std::memory_order_acquire))
    std::this_thread::yield();
  sourceStop.request_stop();
  gate.release.store(true, std::memory_order_release);
  builder.join();
  require(concurrentBuild && !*concurrentBuild &&
              concurrentBuild->error().code == "render.sketch.marker-cancelled",
          "mid-build cancellation published a marker packet");
}

void verifyLatestSketchMarkerPacket(const testkit::PropertyProfile &profile) {
  const SceneStamp baseStamp = stamp(51, 1, 2, 3, 4, 5);
  auto base = scene(4, 1100, baseStamp);
  auto draft = provisionalGeometry(
      provisionalStamp(provisionalTarget(baseStamp, 6U, 7U), 8U, 9U),
      provisionalFixture());
  const SketchMarkerTarget target =
      markerTarget(baseStamp, 6U, 7U, provisionalReference(*draft));
  const MarkerInput source = markerFixture(*base);
  LatestSketchMarkerPacket publisher{target};

  auto first = markerPacket(markerStamp(target, 2U, 100U), base, draft, source);
  auto firstOffer = publisher.publish(first);
  require(firstOffer && *firstOffer == SketchMarkerDecision::Accepted &&
              publisher.retainedCount() == 1U,
          "latest marker publisher rejected its first generation");
  const std::array<std::size_t, 5> reverseOrder{4U, 3U, 2U, 1U, 0U};
  const MarkerInput reversed = permuteMarkers(source, reverseOrder);
  auto duplicate = publisher.publish(
      markerPacket(markerStamp(target, 2U, 100U), base, draft, reversed));
  require(duplicate && *duplicate == SketchMarkerDecision::Duplicate,
          "canonical marker duplicate was not detected");
  auto digestConflict = publisher.publish(
      markerPacket(markerStamp(target, 2U, 101U), base, draft, source));
  require(digestConflict &&
              *digestConflict == SketchMarkerDecision::GenerationConflict,
          "same-generation marker digest conflict was not detected");
  MarkerInput changed = source;
  changed.markers[2].valueSi += 0.001;
  auto payloadConflict = publisher.publish(
      markerPacket(markerStamp(target, 2U, 100U), base, draft, changed));
  require(payloadConflict &&
              *payloadConflict == SketchMarkerDecision::GenerationConflict,
          "same-digest marker payload conflict was not detected");
  auto stale = publisher.publish(
      markerPacket(markerStamp(target, 1U, 99U), base, draft, source));
  require(stale && *stale == SketchMarkerDecision::StaleGeneration,
          "stale marker generation was accepted");
  auto newer = publisher.publish(
      markerPacket(markerStamp(target, 3U, 102U), base, draft, source));
  require(newer && *newer == SketchMarkerDecision::Accepted &&
              publisher.latest()->stamp().generation.value() == 3U &&
              publisher.retainedCount() == 1U,
          "latest-only marker packet was not replaced");
  require(!publisher.publish(nullptr) && publisher.retainedCount() == 1U,
          "latest marker publisher accepted a null packet");

  testkit::checkProperty(
      "marker target rejects every independent identity mismatch", profile,
      [&](testkit::Random &, std::uint64_t index) {
        const std::uint64_t value = (index + 1U) * 32U;
        const SketchMarkerTarget expected =
            markerTarget(stamp(value + 1U, value + 2U, value + 3U, value + 4U,
                               value + 5U, value + 6U),
                         value + 7U, value + 8U,
                         SketchProvisionalReference{
                             provisionalGeneration(value + 9U),
                             digest<SketchProvisionalDigest>(value + 10U)});
        SketchMarkerTarget candidate = expected;
        switch (index % 11U) {
        case 0U:
          candidate.base.target.session =
              *RenderSessionHandle::create(value + 11U);
          break;
        case 1U:
          candidate.base.target.evaluatedPlane.attachmentBinding =
              id<ModelBindingId>(value + 11U);
          break;
        case 2U:
          candidate.base.target.evaluatedPlane.revision =
              digest<RevisionId>(value + 11U);
          break;
        case 3U:
          candidate.base.target.evaluation = digest<EvaluationKey>(value + 11U);
          break;
        case 4U:
          candidate.base.generation = *SceneGeneration::create(value + 11U);
          break;
        case 5U:
          candidate.base.digest = digest<SceneDigest>(value + 11U);
          break;
        case 6U:
          candidate.interaction->editSession = editSession(value + 11U);
          break;
        case 7U:
          candidate.interaction->toolInstance = toolInstance(value + 11U);
          break;
        case 8U:
          candidate.provisional->generation =
              provisionalGeneration(value + 11U);
          break;
        case 9U:
          candidate.provisional->payload =
              digest<SketchProvisionalDigest>(value + 11U);
          break;
        case 10U:
          candidate.view = markerViewGeneration(value + 11U);
          break;
        }
        auto candidateBase = scene(0, value + 100U, candidate.base);
        const SketchProvisionalTarget candidateDraftTarget{
            candidate.base, candidate.interaction->editSession,
            candidate.interaction->toolInstance};
        auto candidateDraft = provisionalGeometry(
            {candidateDraftTarget, candidate.provisional->generation,
             candidate.provisional->payload},
            provisionalFixture());
        MarkerInput one;
        one.add(1U, SketchMarkerKind::GridSnap, 0.0,
                {SketchProvisionalMarkerAnchor{
                    provisionalHandle(1U),
                    pointLocation(sketch::PointKey::Point)}});
        LatestSketchMarkerPacket exactPublisher{expected};
        auto decision = exactPublisher.publish(
            markerPacket(markerStamp(candidate, 1U, value + 12U), candidateBase,
                         candidateDraft, one));
        require(decision && *decision == SketchMarkerDecision::StaleTarget &&
                    exactPublisher.retainedCount() == 0U,
                "marker target accepted an independent identity mismatch");
      });

  publisher.retarget(target);
  require(publisher.retainedCount() == 1U,
          "same-target marker retarget discarded current state");
  SketchMarkerTarget nextTarget = target;
  nextTarget.interaction->toolInstance = toolInstance(99U);
  publisher.retarget(nextTarget);
  require(publisher.retainedCount() == 0U && !publisher.latest(),
          "new marker target retained stale marker state");
}

void verifySceneSchema(const testkit::PropertyProfile &profile) {
  require(!SceneGeneration::create(0), "zero scene generation was accepted");
  require(!RenderSessionHandle::create(0),
          "zero render session handle was accepted");
  require(!SketchPrimitiveHandle::create(0),
          "zero primitive handle was accepted");

  auto empty = SketchSceneSnapshot::create(stamp(1, 1, 1, 1, 1, 1), {}, {}, {});
  require(empty.has_value() && empty->bounds().empty,
          "empty scene does not expose empty bounds");

  auto statefulStyles = styles();
  statefulStyles.front().role = SketchStyleRole::Selected;
  auto statefulBase = SketchSceneSnapshot::create(
      stamp(1, 1, 1, 1, 1, 2), std::move(statefulStyles), {}, {});
  require(!statefulBase &&
              statefulBase.error().code == "render.sketch.base-style-role",
          "base scene accepted transient presentation state");

  auto hugeHandle = SketchPrimitiveHandle::create(1);
  require(hugeHandle.has_value(), "test primitive handle could not be created");
  const double huge = std::numeric_limits<double>::max();
  auto unrepresentable = SketchSceneSnapshot::create(
      stamp(1, 2, 1, 1, 1, 2), styles(), {{huge, huge}},
      {{id<SketchEntityId>(1), *hugeHandle, 0, 0, SketchPrimitiveKind::Circle,
        SketchPrimitiveFlags::Visible | SketchPrimitiveFlags::Selectable, huge,
        0.0, 0.0}});
  require(!unrepresentable && unrepresentable.error().code ==
                                  "render.sketch.unrepresentable-bounds",
          "scene with non-finite derived bounds was accepted");

  testkit::checkProperty(
      "sketch scene schema rejection", profile,
      [](testkit::Random &random, std::uint64_t index) {
        const auto firstHandle = SketchPrimitiveHandle::create(1);
        const auto secondHandle = SketchPrimitiveHandle::create(2);
        require(firstHandle && secondHandle,
                "test primitive handles could not be created");
        const SketchEntityId firstEntity = id<SketchEntityId>(index * 2U + 1U);
        const SketchEntityId secondEntity =
            index % 4U == 0U ? firstEntity
                             : id<SketchEntityId>(index * 2U + 2U);
        std::vector<Point2d> points{
            {random.between(-1.0, 1.0), random.between(-1.0, 1.0)}};
        std::vector<PackedSketchPrimitive> primitives{
            {firstEntity, *firstHandle, 0, 0, SketchPrimitiveKind::Point,
             SketchPrimitiveFlags::Visible | SketchPrimitiveFlags::Selectable,
             0.0, 0.0, 0.0}};
        const char *expected = nullptr;
        switch (index % 4U) {
        case 0:
          points.push_back({points.front().x + 1.0, points.front().y});
          primitives.push_back({secondEntity, *secondHandle, 1, 0,
                                SketchPrimitiveKind::Point,
                                SketchPrimitiveFlags::Visible, 0.0, 0.0, 0.0});
          expected = "render.sketch.duplicate-entity";
          break;
        case 1:
          primitives.front().style = 5;
          expected = "render.sketch.style-range";
          break;
        case 2:
          points.push_back(points.front());
          primitives.front().kind = SketchPrimitiveKind::Line;
          expected = "render.sketch.degenerate-line";
          break;
        case 3:
          points.push_back({points.front().x + 1.0, points.front().y});
          expected = "render.sketch.unused-points";
          break;
        }
        auto created = SketchSceneSnapshot::create(
            stamp(1, index + 1U, 1, 1, 1, index + 1U), styles(),
            std::move(points), std::move(primitives));
        require(!created && created.error().code == expected,
                "malformed scene violated its diagnostic contract");
      });
}

void verifyIndexedPicking(const testkit::PropertyProfile &profile) {
  auto arc = SketchSceneSnapshot::create(
      stamp(1, 1, 1, 1, 1, 1), styles(), {{0.0, 0.0}},
      {{id<SketchEntityId>(1), *SketchPrimitiveHandle::create(1), 0, 0,
        SketchPrimitiveKind::Arc,
        SketchPrimitiveFlags::Visible | SketchPrimitiveFlags::Selectable, 10.0,
        0.0, 0.1}});
  require(arc.has_value(), "valid arc scene was rejected");
  auto arcIndex = SketchPickIndex::build(
      std::make_shared<const SketchSceneSnapshot>(std::move(*arc)));
  require(arcIndex.has_value(), "valid arc index was rejected");
  auto arcCenter =
      arcIndex->pick({{0.0, 0.0}, 0.001, SketchPickTargets::Points});
  require(arcCenter && *arcCenter &&
              (*arcCenter)->pointKey == sketch::PointKey::Center &&
              (*arcCenter)->closestPoint == Point2d{0.0, 0.0},
          "arc center is missing from the spatial pick index");

  auto layered = SketchSceneSnapshot::create(
      stamp(1, 2, 1, 1, 1, 2), styles(), {{0.0, 0.0}, {0.0, 0.0}},
      {{id<SketchEntityId>(2), *SketchPrimitiveHandle::create(1), 0, 0,
        SketchPrimitiveKind::Point,
        SketchPrimitiveFlags::Visible | SketchPrimitiveFlags::Selectable, 0.0,
        0.0, 0.0},
       {id<SketchEntityId>(3), *SketchPrimitiveHandle::create(2), 1, 1,
        SketchPrimitiveKind::Point,
        SketchPrimitiveFlags::Visible | SketchPrimitiveFlags::Selectable, 0.0,
        0.0, 0.0}});
  require(layered.has_value(), "valid layered scene was rejected");
  auto layeredIndex = SketchPickIndex::build(
      std::make_shared<const SketchSceneSnapshot>(std::move(*layered)));
  require(layeredIndex.has_value(), "valid layered index was rejected");
  auto layeredPick =
      layeredIndex->pick({{0.0, 0.0}, 0.0, SketchPickTargets::Points});
  require(layeredPick && *layeredPick &&
              (*layeredPick)->primitive.value() == 2U,
          "pick tie did not prefer the higher presentation layer");

  for (const std::size_t size : {10U, 100U, 1'000U, 10'000U}) {
    auto generated = scene(size, size, stamp(1, size, 1, 1, 1, size));
    auto index = SketchPickIndex::build(generated);
    require(index.has_value(), "valid pick index was rejected");
    require(index->targetCount() <= size * 4U &&
                index->indexedReferenceCount() == index->targetCount(),
            "pick index exceeded the analytic target bound");
    for (const SketchPickQuery &query : queries(*generated, 160, size + 1U)) {
      auto actual = index->pick(query);
      require(actual.has_value(), "valid indexed pick was rejected");
      requireEquivalent(*actual, bruteForcePick(*generated, query));
    }
  }

  testkit::checkProperty(
      "indexed analytic picking matches brute force", profile,
      [](testkit::Random &random, std::uint64_t index) {
        const std::size_t count =
            static_cast<std::size_t>(random.next() % 64U + 1U);
        auto generated = scene(count, random.next(),
                               stamp(1, index + 1U, 1, 1, 1, index + 1U));
        auto spatial = SketchPickIndex::build(generated);
        require(spatial.has_value(), "valid generated index was rejected");
        const auto generatedQueries = queries(*generated, 1, random.next());
        const SketchPickQuery query = generatedQueries.front();
        auto actual = spatial->pick(query);
        require(actual.has_value(), "valid generated query was rejected");
        requireEquivalent(*actual, bruteForcePick(*generated, query));

        const Point2d translation{random.between(-100.0, 100.0),
                                  random.between(-100.0, 100.0)};
        std::vector<Point2d> translatedPoints(generated->points().begin(),
                                              generated->points().end());
        for (Point2d &point : translatedPoints) {
          point.x += translation.x;
          point.y += translation.y;
        }
        auto translated = SketchSceneSnapshot::create(
            stamp(2, index + 1U, 2, 2, 2, index + 1U),
            std::vector<SketchStyle>(generated->styles().begin(),
                                     generated->styles().end()),
            std::move(translatedPoints),
            std::vector<PackedSketchPrimitive>(generated->primitives().begin(),
                                               generated->primitives().end()));
        require(translated.has_value(), "translated pick scene was rejected");
        auto translatedIndex =
            SketchPickIndex::build(std::make_shared<const SketchSceneSnapshot>(
                std::move(*translated)));
        require(translatedIndex.has_value(),
                "translated pick index was rejected");
        const SketchPickQuery translatedQuery{
            {query.point.x + translation.x, query.point.y + translation.y},
            query.tolerance,
            query.targets};
        auto translatedPick = translatedIndex->pick(translatedQuery);
        require(translatedPick &&
                    translatedPick->has_value() == actual->has_value(),
                "pick translation changed hit presence");
        if (*actual) {
          const double tolerance =
              8192.0 * std::numeric_limits<double>::epsilon() *
              std::max({1.0, std::abs(translation.x), std::abs(translation.y)});
          require(
              (*translatedPick)->entity == (*actual)->entity &&
                  (*translatedPick)->primitive == (*actual)->primitive &&
                  (*translatedPick)->pointKey == (*actual)->pointKey &&
                  std::abs((*translatedPick)->distance - (*actual)->distance) <=
                      tolerance &&
                  std::abs((*translatedPick)->closestPoint.x - translation.x -
                           (*actual)->closestPoint.x) <= tolerance &&
                  std::abs((*translatedPick)->closestPoint.y - translation.y -
                           (*actual)->closestPoint.y) <= tolerance,
              "pick translation changed closest-point evidence");
        }
      });

  auto generated = scene(4, 8, stamp(1, 1, 1, 1, 1, 1));
  auto index = SketchPickIndex::build(generated);
  require(index.has_value(), "valid pick index was rejected");
  const SketchPickQuery nonFinite{
      {std::numeric_limits<double>::quiet_NaN(), 0.0},
      1.0,
      SketchPickTargets::All};
  const SketchPickQuery negative{{0.0, 0.0}, -1.0, SketchPickTargets::All};
  require(!index->pick(nonFinite) && !index->pick(negative),
          "invalid pick query was accepted");
}

void verifyAdversarialPicking(const testkit::PropertyProfile &profile) {
  testkit::checkProperty(
      "adversarial packed picking matches brute force", profile,
      [](testkit::Random &random, std::uint64_t index) {
        const auto kind = static_cast<PickSceneProfile>(index % 5U + 1U);
        const std::size_t count =
            static_cast<std::size_t>(random.next() % 48U + 1U);
        auto generated =
            pickScene(count, kind, random.next(),
                      stamp(30, index + 1U, 1, 30, 30, index + 1U));
        auto spatial = SketchPickIndex::build(generated);
        require(spatial.has_value(), "adversarial pick index build failed");
        SketchPickQuery query;
        switch (kind) {
        case PickSceneProfile::Sparse:
          query = queries(*generated, 1, random.next()).front();
          break;
        case PickSceneProfile::Coincident:
          query = {{0.0, 0.0}, 0.0, SketchPickTargets::Points};
          break;
        case PickSceneProfile::Concentric:
          query = {{1.0, 0.0}, 0.001, SketchPickTargets::All};
          break;
        case PickSceneProfile::GlobalLines:
          query = {{0.0, 0.0}, 0.001, SketchPickTargets::Curves};
          break;
        case PickSceneProfile::Outlier:
          query = {{0.0, 0.0}, 0.001, SketchPickTargets::Points};
          break;
        }
        const SketchPickOutcome actual = spatial->query(query);
        require(actual.status == SketchPickStatus::Hit ||
                    actual.status == SketchPickStatus::Miss,
                "bounded adversarial query refused a small exact case");
        requireEquivalent(actual.result, bruteForcePick(*generated, query));

        const SketchPickQuery outside{
            {2.0e9, 2.0e9}, 0.001, SketchPickTargets::All};
        const SketchPickOutcome missed = spatial->query(outside);
        require(missed.status == SketchPickStatus::Miss && !missed.result,
                "outside-scene query did not finish as a miss");
      });
}

std::shared_ptr<const SketchSceneSnapshot>
makePickScene(SceneStamp sceneStamp, std::vector<Point2d> points,
              std::vector<PackedSketchPrimitive> primitives) {
  auto created =
      SketchSceneSnapshot::create(std::move(sceneStamp), styles(),
                                  std::move(points), std::move(primitives));
  require(created.has_value(), "fixed pick scene was rejected");
  return std::make_shared<const SketchSceneSnapshot>(std::move(*created));
}

PackedSketchPrimitive pointPrimitive(std::uint64_t entity, std::uint32_t handle,
                                     std::uint32_t firstPoint,
                                     std::uint16_t style = 0) {
  return {id<SketchEntityId>(entity),
          *SketchPrimitiveHandle::create(handle),
          firstPoint,
          style,
          SketchPrimitiveKind::Point,
          SketchPrimitiveFlags::Visible | SketchPrimitiveFlags::Selectable,
          0.0,
          0.0,
          0.0};
}

void verifyExplicitTieOrder() {
  auto pointOverCurve = makePickScene(
      stamp(31, 1, 1, 31, 31, 1), {{-1.0, 0.0}, {1.0, 0.0}, {0.0, 0.0}},
      {{id<SketchEntityId>(1), *SketchPrimitiveHandle::create(1), 0, 4,
        SketchPrimitiveKind::Line,
        SketchPrimitiveFlags::Visible | SketchPrimitiveFlags::Selectable, 0.0,
        0.0, 0.0},
       pointPrimitive(2, 2, 2, 0)});
  auto first = SketchPickIndex::build(pointOverCurve);
  require(first.has_value(), "point-over-curve index build failed");
  auto pointPick = first->query({{0.0, 0.0}, 0.0, SketchPickTargets::All});
  require(pointPick.status == SketchPickStatus::Hit && pointPick.result &&
              pointPick.result->primitive.value() == 2U &&
              pointPick.result->pointKey.has_value(),
          "equal-distance point did not outrank a higher-layer curve");

  auto ordinalScene =
      makePickScene(stamp(31, 2, 1, 31, 31, 2), {{0.0, 0.0}, {0.0, 0.0}},
                    {pointPrimitive(3, 1, 0), pointPrimitive(4, 2, 1)});
  auto ordinalIndex = SketchPickIndex::build(ordinalScene);
  require(ordinalIndex.has_value(), "ordinal tie index build failed");
  auto ordinalPick =
      ordinalIndex->query({{0.0, 0.0}, 0.0, SketchPickTargets::Points});
  require(ordinalPick.result && ordinalPick.result->primitive.value() == 2U,
          "equal-layer tie did not prefer the later primitive ordinal");

  struct EligibilityFixture {
    SketchPrimitiveHandle rejected;
    std::uint32_t evaluations = 0U;
    static SketchPickEligibility::Evaluation
    evaluate(void *opaque, const SketchPickResult &candidate) noexcept {
      auto &fixture = *static_cast<EligibilityFixture *>(opaque);
      ++fixture.evaluations;
      return {candidate.primitive == fixture.rejected
                  ? SketchPickEligibilityDecision::Ineligible
                  : SketchPickEligibilityDecision::Eligible,
              candidate.distance};
    }
  } eligibility{*SketchPrimitiveHandle::create(2U)};
  std::array<std::uint32_t, SketchPickIndex::recommendedQueryStackCapacity>
      workspace{};
  const auto eligiblePick = ordinalIndex->query(
      {{0.0, 0.0}, 0.0, SketchPickTargets::Points}, {workspace},
      {&eligibility, EligibilityFixture::evaluate});
  require(eligiblePick.status == SketchPickStatus::Hit && eligiblePick.result &&
              eligiblePick.result->primitive.value() == 1U &&
              eligiblePick.metrics.passes == 1U &&
              eligibility.evaluations == 2U,
          "ineligible analytic winner hid a valid coincident candidate");
  struct ExhaustAfterCandidate {
    std::uint32_t evaluations = 0U;
    static SketchPickEligibility::Evaluation
    evaluate(void *opaque, const SketchPickResult &) noexcept {
      auto &fixture = *static_cast<ExhaustAfterCandidate *>(opaque);
      ++fixture.evaluations;
      return fixture.evaluations == 1U
                 ? SketchPickEligibility::
                       Evaluation{SketchPickEligibilityDecision::Eligible,
                                  0.001}
                 : SketchPickEligibility::Evaluation{
                       SketchPickEligibilityDecision::WorkBudgetExceeded, 0.0};
    }
  } exhaustion;
  const auto exhaustedPick = ordinalIndex->query(
      {{0.0, 0.0}, 0.0, SketchPickTargets::Points}, {workspace},
      {&exhaustion, ExhaustAfterCandidate::evaluate});
  require(exhaustedPick.status == SketchPickStatus::WorkBudgetExceeded &&
              !exhaustedPick.result && exhaustion.evaluations == 2U,
          "partial positive-distance candidate escaped later budget refusal");
  const auto missingWorkspace =
      ordinalIndex->query({{0.0, 0.0}, 0.0, SketchPickTargets::Points}, {{}});
  require(missingWorkspace.status == SketchPickStatus::WorkBudgetExceeded &&
              !missingWorkspace.result,
          "empty caller-owned query workspace did not fail explicitly");

  auto arcScene = makePickScene(
      stamp(31, 3, 1, 31, 31, 3), {{0.0, 0.0}},
      {{id<SketchEntityId>(5), *SketchPrimitiveHandle::create(1), 0, 0,
        SketchPrimitiveKind::Arc,
        SketchPrimitiveFlags::Visible | SketchPrimitiveFlags::Selectable, 1.0,
        0.0, std::numbers::pi / 2.0}});
  auto arcIndex = SketchPickIndex::build(arcScene);
  require(arcIndex.has_value(), "semantic-point tie index build failed");
  auto arcPick = arcIndex->query({{0.5, 0.0}, 0.5, SketchPickTargets::Points});
  require(arcPick.result &&
              arcPick.result->pointKey == sketch::PointKey::Center,
          "same-primitive point tie did not follow semantic point order");

  const auto offsetUlps = [](double value, std::uint64_t ulps) {
    return std::bit_cast<double>(std::bit_cast<std::uint64_t>(value) + ulps);
  };
  const double near = offsetUlps(1.0, 800U);
  const double outsideWindow = offsetUlps(1.0, 1'600U);
  auto chainedScene =
      makePickScene(stamp(31, 4, 1, 31, 31, 4),
                    {{1.0, 0.0}, {-near, 0.0}, {outsideWindow, 0.0}},
                    {pointPrimitive(6, 1, 0, 0), pointPrimitive(7, 2, 1, 1),
                     pointPrimitive(8, 3, 2, 4)});
  auto chainedIndex = SketchPickIndex::build(chainedScene);
  require(chainedIndex.has_value(), "chained-distance index build failed");
  const SketchPickQuery chainedQuery{
      {0.0, 0.0}, 2.0, SketchPickTargets::Points};
  const auto chainedPick = chainedIndex->query(chainedQuery);
  require(chainedPick.status == SketchPickStatus::Hit && chainedPick.result &&
              chainedPick.result->primitive.value() == 2U,
          "chained near-equivalence escaped the global minimum window");

  auto permutedScene =
      makePickScene(stamp(31, 5, 1, 31, 31, 5),
                    {{-near, 0.0}, {outsideWindow, 0.0}, {1.0, 0.0}},
                    {pointPrimitive(7, 2, 0, 1), pointPrimitive(8, 3, 1, 4),
                     pointPrimitive(6, 1, 2, 0)});
  auto permutedIndex = SketchPickIndex::build(permutedScene);
  require(permutedIndex.has_value(), "permuted-distance index build failed");
  const auto permutedPick = permutedIndex->query(chainedQuery);
  require(permutedPick.status == SketchPickStatus::Hit && permutedPick.result &&
              permutedPick.result->primitive.value() == 2U,
          "pick identity depended on primitive or BVH traversal order");

  const double separated = offsetUlps(1.0, 2'048U);
  auto separatedScene =
      makePickScene(stamp(31, 6, 1, 31, 31, 6), {{1.0, 0.0}, {separated, 0.0}},
                    {pointPrimitive(9, 1, 0, 0), pointPrimitive(10, 2, 1, 4)});
  auto separatedIndex = SketchPickIndex::build(separatedScene);
  require(separatedIndex.has_value(), "separated-distance index build failed");
  const auto separatedPick = separatedIndex->query(chainedQuery);
  require(separatedPick.status == SketchPickStatus::Hit &&
              separatedPick.result &&
              separatedPick.result->primitive.value() == 1U,
          "structural precedence displaced a materially closer target");
}

void verifyBudgetsCancellationAndMemory() {
  auto dense = pickScene(128, PickSceneProfile::Coincident, 40,
                         stamp(40, 1, 1, 40, 40, 1));
  SketchPickIndexOptions refinementLimit;
  refinementLimit.maximumRefinedTargetsPerPass = 8;
  auto refinementIndex = SketchPickIndex::build(dense, refinementLimit);
  require(refinementIndex.has_value(), "bounded dense index build failed");
  const SketchPickQuery denseQuery{{0.0, 0.0}, 0.0, SketchPickTargets::Points};
  auto refused = refinementIndex->query(denseQuery);
  require(refused.status == SketchPickStatus::WorkBudgetExceeded &&
              !refused.result && refused.metrics.refinedTargets == 8U,
          "refinement budget returned a partial or late result");
  auto refusedAdapter = refinementIndex->pick(denseQuery);
  require(!refusedAdapter &&
              refusedAdapter.error().code == "render.pick.query-budget",
          "compatibility pick did not expose exact budget refusal");

  SketchPickIndexOptions visitLimit;
  visitLimit.maximumVisitedNodesPerPass = 1;
  auto visitIndex = SketchPickIndex::build(dense, visitLimit);
  require(visitIndex.has_value(), "node-limited index build failed");
  auto visitRefused = visitIndex->query(denseQuery);
  require(visitRefused.status == SketchPickStatus::WorkBudgetExceeded &&
              !visitRefused.result && visitRefused.metrics.visitedNodes == 1U,
          "node budget returned a partial or late result");

  auto one = pickScene(1, PickSceneProfile::Coincident, 41,
                       stamp(40, 2, 1, 40, 40, 2));
  SketchPickIndexOptions exactLimit;
  exactLimit.maximumVisitedNodesPerPass = 1;
  exactLimit.maximumRefinedTargetsPerPass = 1;
  auto exactIndex = SketchPickIndex::build(one, exactLimit);
  require(exactIndex.has_value(), "exact-limit index build failed");
  auto exact = exactIndex->query(denseQuery);
  require(exact.status == SketchPickStatus::Hit && exact.result &&
              exact.metrics == SketchPickMetrics{1, 1, 1},
          "query completing exactly at its budget was refused");

  SketchPickIndexOptions retainedLimit;
  retainedLimit.maximumRetainedBytes = 1;
  auto retainedRefused = SketchPickIndex::build(dense, retainedLimit);
  require(!retainedRefused && retainedRefused.error().code ==
                                  "render.pick.retained-byte-budget",
          "retained byte budget was not checked before allocation");
  SketchPickIndexOptions scratchLimit;
  scratchLimit.maximumScratchBytes = 1;
  auto scratchRefused = SketchPickIndex::build(dense, scratchLimit);
  require(!scratchRefused &&
              scratchRefused.error().code == "render.pick.scratch-byte-budget",
          "scratch byte budget was not checked before allocation");
  SketchPickIndexOptions peakLimit;
  peakLimit.maximumPeakBuildBytes = 1;
  auto peakRefused = SketchPickIndex::build(dense, peakLimit);
  require(!peakRefused &&
              peakRefused.error().code == "render.pick.peak-byte-budget",
          "coexisting retained and scratch bytes were not bounded");

  auto normal = SketchPickIndex::build(dense);
  require(normal &&
              normal->retainedBytes() <=
                  SketchPickIndexOptions{}.maximumRetainedBytes &&
              normal->scratchBytes() <=
                  SketchPickIndexOptions{}.maximumScratchBytes &&
              normal->peakBuildBytes() ==
                  normal->retainedBytes() + normal->scratchBytes(),
          "pick index byte accounting is inconsistent");

  for (const std::size_t size : {0U, 1U, 64U, 1'000U}) {
    std::stop_source source;
    source.request_stop();
    auto cancelledBuild = SketchPickIndex::build(
        pickScene(size, PickSceneProfile::Outlier, size + 50U,
                  stamp(41, size + 1U, 1, 41, 41, size + 1U)),
        {}, source.get_token());
    require(!cancelledBuild &&
                cancelledBuild.error().code == "render.pick.cancelled",
            "pre-cancelled build published an index");
  }

  auto cancellableScene = pickScene(10'000, PickSceneProfile::GlobalLines, 55,
                                    stamp(41, 2'000, 1, 41, 41, 2'000));
  std::stop_source source;
  AllocationGate gate;
  std::optional<Result<SketchPickIndex>> concurrentBuild;
  std::thread builder([&] {
    allocationGate = &gate;
    concurrentBuild.emplace(
        SketchPickIndex::build(cancellableScene, {}, source.get_token()));
  });
  while (!gate.reached.load(std::memory_order_acquire))
    std::this_thread::yield();
  source.request_stop();
  gate.release.store(true, std::memory_order_release);
  builder.join();
  require(concurrentBuild && !*concurrentBuild &&
              concurrentBuild->error().code == "render.pick.cancelled",
          "mid-build cancellation published an index");
}

void verifyNumericAndImmutableQueries() {
  const std::array<std::pair<double, double>, 4> profiles{{
      {0.0, 1.0e-150},
      {8.0, 1.0},
      {8.0e140, 1.0e140},
      {1.0e150, 1.0e140},
  }};
  std::uint64_t generation = 1;
  for (const auto &[translation, scale] : profiles) {
    const Point2d start{translation, translation};
    const Point2d end{translation + scale, translation};
    auto numeric = makePickScene(
        stamp(42, generation, 1, 42, 42, generation), {start, end},
        {{id<SketchEntityId>(generation), *SketchPrimitiveHandle::create(1), 0,
          0, SketchPrimitiveKind::Line,
          SketchPrimitiveFlags::Visible | SketchPrimitiveFlags::Selectable, 0.0,
          0.0, 0.0}});
    auto index = SketchPickIndex::build(numeric);
    require(index.has_value(), "finite scaled line index was rejected");
    const SketchPickQuery query{{std::midpoint(start.x, end.x), translation},
                                std::abs(scale) * 1.0e-8,
                                SketchPickTargets::Curves};
    const auto actual = index->query(query);
    const double closestTolerance =
        8.0 * std::numeric_limits<double>::epsilon() *
        std::max({1.0, std::abs(query.point.x), std::abs(query.point.y)});
    require(actual.status == SketchPickStatus::Hit && actual.result &&
                std::abs(actual.result->closestPoint.x - query.point.x) <=
                    closestTolerance &&
                std::abs(actual.result->closestPoint.y - query.point.y) <=
                    closestTolerance,
            "scaled translated line was not pickable");
    requireEquivalent(actual.result, bruteForcePick(*numeric, query));

    const Point2d projectedQuery{translation - scale * 0.25,
                                 translation + scale * 0.2};
    const Point2d projectionCenter{translation + scale * 0.5,
                                   translation + scale * 0.2};
    constexpr double angle = 0.63;
    const double cosine = std::cos(angle);
    const double sine = std::sin(angle);
    const double metresPerPixel = std::abs(scale) * 1.0e-3;
    const double relativeX = projectedQuery.x - projectionCenter.x;
    const double relativeY = projectedQuery.y - projectionCenter.y;
    const double itemX =
        (cosine * relativeX - sine * relativeY) / metresPerPixel;
    const double itemY =
        -(sine * relativeX + cosine * relativeY) / metresPerPixel;
    const double rotatedX = itemX * metresPerPixel;
    const double rotatedY = -itemY * metresPerPixel;
    const Point2d roundTrip{
        projectionCenter.x + cosine * rotatedX + sine * rotatedY,
        projectionCenter.y - sine * rotatedX + cosine * rotatedY};
    const double projectedTolerance = std::abs(scale) * 0.5;
    const auto direct = index->query(
        {projectedQuery, projectedTolerance, SketchPickTargets::All});
    const auto projected =
        index->query({roundTrip, projectedTolerance, SketchPickTargets::All});
    require(direct.status == SketchPickStatus::Hit && direct.result &&
                direct.result->pointKey == sketch::PointKey::Start &&
                projected.status == SketchPickStatus::Hit && projected.result &&
                projected.result->entity == direct.result->entity &&
                projected.result->pointKey == direct.result->pointKey,
            "projection round-trip changed the selected semantic target");
    ++generation;
  }

  auto generated =
      pickScene(96, PickSceneProfile::Outlier, 60, stamp(43, 1, 1, 43, 43, 1));
  auto first = SketchPickIndex::build(generated);
  auto second = SketchPickIndex::build(generated);
  require(first && second && first->leafCount() == second->leafCount() &&
              first->nodeCount() == second->nodeCount() &&
              first->targetCount() == second->targetCount() &&
              first->retainedBytes() == second->retainedBytes(),
          "identical scenes did not rebuild deterministically");
  const SketchPickQuery query{{0.0002, 0.0002}, 0.001, SketchPickTargets::All};
  const auto expected = first->query(query);
  const auto repeated = second->query(query);
  require(expected.status == repeated.status &&
              expected.result == repeated.result &&
              expected.metrics == repeated.metrics,
          "identical indexes produced different query evidence");

  const SketchPickQuery overflow{{std::numeric_limits<double>::max(), 0.0},
                                 std::numeric_limits<double>::max(),
                                 SketchPickTargets::All};
  require(first->query(overflow).status ==
              SketchPickStatus::NonFiniteArithmetic,
          "non-finite derived query bounds were not refused");

  (void)first->query(query);
  measuredAllocations = 0;
  measureAllocations = true;
  const auto allocationChecked = first->query(query);
  measureAllocations = false;
  require(measuredAllocations == 0 &&
              allocationChecked.status == expected.status &&
              allocationChecked.result == expected.result,
          "allocation-free query performed dynamic allocation");

  std::atomic<bool> concurrentMismatch = false;
  std::vector<std::thread> threads;
  for (std::size_t worker = 0; worker < 4U; ++worker) {
    threads.emplace_back([&] {
      for (std::size_t iteration = 0; iteration < 500U; ++iteration) {
        const auto actual = first->query(query);
        if (actual.status != expected.status ||
            actual.result != expected.result ||
            actual.metrics != expected.metrics)
          concurrentMismatch.store(true, std::memory_order_relaxed);
      }
    });
  }
  for (std::thread &thread : threads)
    thread.join();
  require(!concurrentMismatch.load(std::memory_order_relaxed),
          "immutable concurrent queries diverged");
}

void verifyDeltaApplication() {
  auto base = scene(4, 40, stamp(4, 1, 1, 1, 1, 1));
  std::vector<SketchStyle> replacementStyles = styles();
  replacementStyles.back().strokeWidthPixels = 3.0F;

  const PackedSketchPrimitive &oldFirst = base->primitives().front();
  PackedSketchPrimitive upsert = oldFirst;
  upsert.firstPoint = 0;
  upsert.style = 4;
  SketchPrimitiveBatch batch{{base->points().front()}, {upsert}};
  const SceneStamp target = stamp(4, 2, 1, 2, 2, 2);
  auto delta = SketchSceneDelta::create(
      base->stamp(), target, replacementStyles, {base->primitives()[1].handle},
      std::move(batch));
  require(delta.has_value(), "valid scene delta was rejected");
  auto applied = applySceneDelta(*base, *delta);
  require(applied.has_value(), "valid scene delta did not apply");
  require((*applied)->stamp() == target &&
              (*applied)->primitives().size() == 3U &&
              (*applied)->primitives().front().style == 4U &&
              (*applied)->styles().back().strokeWidthPixels == 3.0F,
          "scene delta did not atomically replace its target state");

  auto missingHandle = SketchPrimitiveHandle::create(999);
  require(missingHandle.has_value(), "test handle could not be created");
  auto missing =
      SketchSceneDelta::create(base->stamp(), stamp(4, 3, 1, 2, 2, 3),
                               std::nullopt, {*missingHandle}, {});
  require(missing.has_value(), "missing-remove delta schema was rejected");
  auto missingResult = applySceneDelta(*base, *missing);
  require(!missingResult &&
              missingResult.error().code == "render.scene.delta-missing-remove",
          "delta silently ignored a missing removal");

  auto newHandle = SketchPrimitiveHandle::create(1000);
  require(newHandle.has_value(), "test handle could not be created");
  SketchPrimitiveBatch duplicateEntity{
      {{9.0, 9.0}},
      {{base->primitives().front().entity, *newHandle, 0, 0,
        SketchPrimitiveKind::Point,
        SketchPrimitiveFlags::Visible | SketchPrimitiveFlags::Selectable, 0.0,
        0.0, 0.0}}};
  auto duplicate =
      SketchSceneDelta::create(base->stamp(), stamp(4, 4, 1, 2, 2, 4),
                               std::nullopt, {}, std::move(duplicateEntity));
  require(duplicate.has_value(), "duplicate-entity delta schema was rejected");
  auto duplicateResult = applySceneDelta(*base, *duplicate);
  require(!duplicateResult &&
              duplicateResult.error().code == "render.sketch.duplicate-entity",
          "delta introduced duplicate semantic identity");

  auto crossBinding = SketchSceneDelta::create(
      base->stamp(), stamp(4, 5, 2, 2, 2, 5), std::nullopt, {}, {});
  require(!crossBinding && crossBinding.error().code ==
                               "render.scene.delta-attachment-binding",
          "delta crossed plane attachment bindings");
}

void verifySceneTargetIdentity(const testkit::PropertyProfile &profile) {
  testkit::checkProperty(
      "scene target identity rejects independent mismatches", profile,
      [](testkit::Random &, std::uint64_t index) {
        const std::uint64_t value = (index + 1U) * 16U;
        const SceneStamp expected = stamp(value + 1U, value + 2U, value + 3U,
                                          value + 4U, value + 5U, value + 6U);
        const auto decisions = [&](const SceneStamp &candidate) {
          SceneStamp base = candidate;
          base.generation = *SceneGeneration::create(value + 1U);
          base.digest = digest<SceneDigest>(value + 7U);
          auto delta = std::make_shared<const SketchSceneDelta>(
              emptyDelta(base, candidate));
          return std::array{
              assessSceneEnvelope(expected.target, std::nullopt,
                                  fullEnvelope(scene(0, value, candidate))),
              assessSceneEnvelope(expected.target, base, deltaEnvelope(delta))};
        };
        const auto exact = decisions(expected);
        require(exact[0] == SceneEnvelopeDecision::AcceptFull &&
                    exact[1] == SceneEnvelopeDecision::AcceptDelta,
                "exact scene target was rejected");
        const auto requireStale = [&](const SceneStamp &candidate,
                                      const char *message) {
          const auto actual = decisions(candidate);
          require(std::ranges::all_of(
                      actual,
                      [](SceneEnvelopeDecision decision) {
                        return decision == SceneEnvelopeDecision::StaleTarget;
                      }),
                  message);
        };

        SceneStamp changed = expected;
        changed.target.session = *RenderSessionHandle::create(value + 7U);
        requireStale(changed,
                     "render session mismatch escaped scene target identity");

        changed = expected;
        changed.target.evaluatedPlane.attachmentBinding =
            id<ModelBindingId>(value + 7U);
        requireStale(
            changed,
            "attachment binding mismatch escaped scene target identity");

        changed = expected;
        changed.target.evaluatedPlane.revision = digest<RevisionId>(value + 7U);
        requireStale(changed,
                     "plane revision mismatch escaped scene target identity");

        changed = expected;
        changed.target.evaluation = digest<EvaluationKey>(value + 7U);
        requireStale(changed,
                     "evaluation mismatch escaped scene target identity");
      });
}

void verifyLineage() {
  auto initial = scene(0, 1, stamp(1, 1, 1, 1, 1, 1));
  LatestSketchSceneMailbox mailbox{initial->stamp().target};
  auto initialOffer = mailbox.offer(fullEnvelope(initial));
  require(initialOffer &&
              initialOffer->decision == SceneEnvelopeDecision::AcceptFull &&
              mailbox.pendingCount() == 1U,
          "mailbox rejected its initial full scene");
  require(mailbox.takeLatest() == initial,
          "mailbox did not install its pending scene");

  const SceneStamp secondStamp = stamp(1, 2, 1, 2, 2, 2);
  mailbox.retarget(secondStamp.target);
  auto secondDelta = std::make_shared<const SketchSceneDelta>(
      emptyDelta(initial->stamp(), secondStamp));
  auto secondOffer = mailbox.offer(deltaEnvelope(secondDelta));
  require(secondOffer &&
              secondOffer->decision == SceneEnvelopeDecision::AcceptDelta,
          "exact-base cross-revision delta was rejected");

  const SceneStamp stalePendingStamp = stamp(1, 4, 1, 99, 99, 99);
  auto stalePendingDelta = std::make_shared<const SketchSceneDelta>(
      emptyDelta(initial->stamp(), stalePendingStamp));
  auto stalePending = mailbox.offer(deltaEnvelope(stalePendingDelta));
  require(stalePending &&
              stalePending->decision == SceneEnvelopeDecision::StaleTarget,
          "cumulative delta bypassed target identity validation");

  const SceneStamp thirdStamp = stamp(1, 3, 1, 2, 2, 3);
  auto thirdDelta = std::make_shared<const SketchSceneDelta>(
      emptyDelta(initial->stamp(), thirdStamp));
  auto thirdOffer = mailbox.offer(deltaEnvelope(thirdDelta));
  require(thirdOffer && thirdOffer->replacedPending &&
              mailbox.pendingCount() == 1U,
          "cumulative delta did not replace an intermediate generation");
  auto latest = mailbox.takeLatest();
  require(latest && latest->stamp() == thirdStamp,
          "mailbox did not install the latest cumulative delta");

  auto duplicate = mailbox.offer(fullEnvelope(scene(0, 3, thirdStamp)));
  require(duplicate && duplicate->decision == SceneEnvelopeDecision::Duplicate,
          "idempotent scene replay was not classified as duplicate");

  SceneStamp conflictStamp = thirdStamp;
  conflictStamp.digest = digest<SceneDigest>(999);
  auto conflict = mailbox.offer(fullEnvelope(scene(0, 999, conflictStamp)));
  require(conflict &&
              conflict->decision == SceneEnvelopeDecision::GenerationConflict,
          "same-generation digest conflict was not detected");

  const SceneStamp resetStamp = stamp(1, 1, 1, 3, 3, 4);
  mailbox.retarget(resetStamp.target);
  auto reset = mailbox.offer(fullEnvelope(scene(0, 4, resetStamp)));
  require(reset && reset->decision == SceneEnvelopeDecision::StaleGeneration,
          "same-session generation reset was accepted");

  const SceneStamp restartedStamp = stamp(2, 1, 1, 3, 3, 5);
  mailbox.retarget(restartedStamp.target);
  auto restarted = mailbox.offer(fullEnvelope(scene(0, 5, restartedStamp)));
  require(restarted && restarted->decision == SceneEnvelopeDecision::AcceptFull,
          "new render session could not restart generation numbering");
  require(mailbox.takeLatest()->stamp() == restartedStamp,
          "new render session scene was not installed");

  const SceneStamp wrongTarget = stamp(2, 2, 1, 4, 4, 6);
  auto staleTarget = mailbox.offer(fullEnvelope(scene(0, 6, wrongTarget)));
  require(staleTarget &&
              staleTarget->decision == SceneEnvelopeDecision::StaleTarget,
          "wrong revision/evaluation scene was accepted");

  const SceneStamp installed = stamp(5, 1, 1, 1, 1, 1);
  const SceneStamp aheadBase = stamp(5, 3, 1, 3, 3, 3);
  const SceneStamp target = stamp(5, 4, 1, 4, 4, 4);
  auto gapDelta =
      std::make_shared<const SketchSceneDelta>(emptyDelta(aheadBase, target));
  require(
      assessSceneEnvelope(target.target, installed, deltaEnvelope(gapDelta)) ==
          SceneEnvelopeDecision::GenerationGap,
      "delta generation gap was not detected");

  const SceneStamp missingTarget = stamp(6, 2, 1, 2, 2, 2);
  auto missingBase = std::make_shared<const SketchSceneDelta>(
      emptyDelta(stamp(6, 1, 1, 1, 1, 1), missingTarget));
  require(assessSceneEnvelope(missingTarget.target, std::nullopt,
                              deltaEnvelope(missingBase)) ==
              SceneEnvelopeDecision::MissingBase,
          "delta without an installed base was accepted");

  const SceneStamp mismatchedInstalled = stamp(7, 1, 1, 1, 1, 1);
  const SceneStamp mismatchedTarget = stamp(7, 2, 1, 2, 2, 2);
  auto mismatchedBase = std::make_shared<const SketchSceneDelta>(
      emptyDelta(stamp(7, 1, 1, 1, 1, 9), mismatchedTarget));
  require(assessSceneEnvelope(mismatchedTarget.target, mismatchedInstalled,
                              deltaEnvelope(mismatchedBase)) ==
              SceneEnvelopeDecision::BaseMismatch,
          "delta with a different base digest was accepted");
}

void verifyLatestWinsStateMachine(const testkit::PropertyProfile &profile) {
  const SceneTarget target = stamp(20, 1, 1, 20, 20, 1).target;
  LatestSketchSceneMailbox mailbox{target};
  std::uint64_t highest = 0;
  testkit::checkProperty(
      "latest-wins mailbox state machine", profile,
      [&](testkit::Random &random, std::uint64_t index) {
        const std::uint64_t generation = random.next() % (index + 32U) + 1U;
        const bool hadPending = mailbox.pendingCount() == 1U;
        auto offered = mailbox.offer(fullEnvelope(scene(
            0, generation, stamp(20, generation, 1, 20, 20, generation))));
        require(offered.has_value(), "valid full scene offer failed");
        const SceneEnvelopeDecision expected =
            generation > highest    ? SceneEnvelopeDecision::AcceptFull
            : generation == highest ? SceneEnvelopeDecision::Duplicate
                                    : SceneEnvelopeDecision::StaleGeneration;
        require(offered->decision == expected,
                "mailbox disagrees with latest-wins reference model");
        if (generation > highest) {
          require(offered->replacedPending == hadPending,
                  "mailbox pending replacement flag is incorrect");
          highest = generation;
        }
        require(mailbox.pendingCount() <= 1U,
                "mailbox retained more than one pending generation");
        if (index % 29U == 0U) {
          auto installedScene = mailbox.takeLatest();
          require(installedScene &&
                      installedScene->stamp().generation.value() == highest,
                  "mailbox installed a stale generation");
        }
      });
}

} // namespace

int main() {
  try {
    const auto profile = kearne::testkit::propertyProfile();
    verifySketchProjection(profile);
    verifySketchPresentation(profile);
    verifySketchPresentationScaling();
    verifyLatestSketchPresentation(profile);
    verifySketchProvisionalGeometry(profile);
    verifySketchProvisionalBudgetsAndCancellation(profile);
    verifyLatestSketchProvisionalGeometry(profile);
    verifySketchMarkerAnchorResolution(profile);
    verifySketchMarkerPacket(profile);
    verifySketchMarkerDependencies();
    verifySketchMarkerBudgetsAndCancellation();
    verifyLatestSketchMarkerPacket(profile);
    verifySceneSchema(profile);
    verifyIndexedPicking(profile);
    verifyAdversarialPicking(profile);
    verifyExplicitTieOrder();
    verifyBudgetsCancellationAndMemory();
    verifyNumericAndImmutableQueries();
    verifyDeltaApplication();
    verifySceneTargetIdentity(profile);
    verifyLineage();
    verifyLatestWinsStateMachine(profile);
    std::cout << "verified " << profile.iterations
              << " generated cases per render property\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
