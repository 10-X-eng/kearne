#include "sketch_overlay_projection.hpp"
#include "sketch_projection_support.hpp"
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

constexpr std::array overlayRoles{
    SketchOverlayRole::Hovered,
    SketchOverlayRole::Selected,
    SketchOverlayRole::Preview,
    SketchOverlayRole::Diagnostic,
};

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

SketchOverlayRoleSetPtr
roleSet(const std::shared_ptr<const SketchSceneSnapshot> &base,
        SketchOverlayRole role, std::span<const SketchOverlayScope> scopes) {
  auto created = SketchOverlayRoleSet::create(base, role, scopes);
  require(created.has_value(), "overlay role-set fixture was invalid");
  return std::move(*created);
}

using RoleSets = std::array<SketchOverlayRoleSetPtr, 4>;

RoleSets emptyRoleSets(const std::shared_ptr<const SketchSceneSnapshot> &base) {
  RoleSets sets;
  for (std::size_t index = 0U; index < sets.size(); ++index)
    sets[index] = roleSet(base, overlayRoles[index], {});
  return sets;
}

std::shared_ptr<const SketchPresentationOverlay>
overlay(const std::shared_ptr<const SketchSceneSnapshot> &base,
        std::uint64_t generation, const RoleSets &sets) {
  auto version = SketchPresentationGeneration::create(generation);
  require(version.has_value(), "overlay generation fixture was invalid");
  auto created = SketchPresentationOverlay::create(base, *version, sets);
  require(created.has_value(), "overlay fixture was invalid");
  return std::move(*created);
}

const PackedSketchPrimitive &firstKind(const SketchSceneSnapshot &base,
                                       SketchPrimitiveKind kind) {
  const auto found =
      std::ranges::find(base.primitives(), kind, &PackedSketchPrimitive::kind);
  require(found != base.primitives().end(),
          "overlay fixture omitted a required primitive kind");
  return *found;
}

using TriangleReference = std::pair<std::uint32_t, std::uint32_t>;

std::vector<TriangleReference>
expanded(std::span<const SketchPrimitiveChunkSpan> spans) {
  std::vector<TriangleReference> triangles;
  for (const SketchPrimitiveChunkSpan span : spans)
    for (std::uint32_t offset = 0U; offset < span.indexCount; offset += 3U)
      triangles.emplace_back(span.chunk, span.firstIndex + offset);
  std::ranges::sort(triangles);
  return triangles;
}

std::vector<TriangleReference>
expectedEntityTriangles(const PreparedSketchScene &base,
                        std::span<const SketchOverlayScope> scopes) {
  std::vector<TriangleReference> triangles;
  const auto &index = *base.primitiveTessellationIndex();
  for (const SketchOverlayScope &scope : scopes) {
    if (scope.point)
      continue;
    const PackedSketchPrimitive *primitive =
        base.scene()->findPrimitive(scope.entity);
    require(primitive != nullptr, "expected overlay entity was absent");
    if (!hasFlag(primitive->flags, SketchPrimitiveFlags::Visible))
      continue;
    const SketchPrimitiveTessellationEntry *entry =
        index.find(primitive->handle);
    require(entry != nullptr, "expected overlay entity was not tessellated");
    const auto chunks = base.mesh()->chunks();
    for (const SketchPrimitiveChunkSpan span : index.spans(primitive->handle)) {
      require(span.chunk < chunks.size() &&
                  chunks[span.chunk]->style() == primitive->style,
              "indexed overlay geometry has foreign style ownership");
      for (std::uint32_t offset = 0U; offset < span.indexCount; offset += 3U)
        triangles.emplace_back(span.chunk, span.firstIndex + offset);
    }
  }
  std::ranges::sort(triangles);
  return triangles;
}

std::vector<SketchOverlayPointInstance>
expectedPoints(const PreparedSketchScene &base,
               std::span<const SketchOverlayScope> scopes) {
  std::vector<SketchOverlayPointInstance> points;
  for (const SketchOverlayScope &scope : scopes) {
    if (!scope.point)
      continue;
    const PackedSketchPrimitive *primitive =
        base.scene()->findPrimitive(scope.entity);
    require(primitive != nullptr, "expected overlay point entity was absent");
    if (!hasFlag(primitive->flags, SketchPrimitiveFlags::Visible))
      continue;
    const auto position =
        semanticPoint(*base.scene(), *primitive, *scope.point);
    require(position.has_value(), "expected overlay point key was invalid");
    points.push_back(
        {primitive->handle, *scope.point, *position, primitive->style});
  }
  return points;
}

void requireValidPreparedRole(const PreparedSketchScene &base,
                              const PreparedSketchOverlayRoleSet &prepared) {
  require(prepared.source() != nullptr &&
              prepared.source()->base() == base.scene(),
          "prepared role lost its exact semantic source");
  const auto chunks = base.mesh()->chunks();
  std::optional<SketchPrimitiveChunkSpan> previous;
  for (const SketchPrimitiveChunkSpan span : prepared.drawSpans()) {
    require(span.chunk < chunks.size() && span.indexCount != 0U &&
                span.firstIndex % 3U == 0U && span.indexCount % 3U == 0U &&
                static_cast<std::size_t>(span.firstIndex) <=
                    chunks[span.chunk]->indices().size() &&
                span.indexCount <=
                    chunks[span.chunk]->indices().size() - span.firstIndex,
            "prepared overlay draw span is out of bounds");
    if (previous) {
      const std::size_t previousEnd =
          static_cast<std::size_t>(previous->firstIndex) + previous->indexCount;
      require(
          previous->chunk < span.chunk ||
              (previous->chunk == span.chunk && previousEnd < span.firstIndex),
          "prepared overlay spans are unsorted, overlapping, or uncoalesced");
    }
    previous = span;
  }
  for (const SketchOverlayPointInstance &point : prepared.pointInstances()) {
    const auto found =
        std::ranges::find(base.scene()->primitives(), point.primitive,
                          &PackedSketchPrimitive::handle);
    require(found != base.scene()->primitives().end() &&
                found->style == point.style,
            "prepared overlay point has foreign primitive ownership");
    const auto expected = semanticPoint(*base.scene(), *found, point.point);
    require(expected && *expected == point.positionMetres,
            "prepared overlay point changed canonical SI geometry");
  }
  require(std::ranges::equal(prepared.pointInstances(),
                             expectedPoints(base, prepared.source()->scopes())),
          "prepared overlay changed point keys, order, or ownership");
  std::size_t rawSpanCount = 0U;
  for (const SketchOverlayScope &scope : prepared.source()->scopes()) {
    if (scope.point)
      continue;
    const PackedSketchPrimitive *primitive =
        base.scene()->findPrimitive(scope.entity);
    const SketchPrimitiveTessellationEntry *entry =
        base.primitiveTessellationIndex()->find(primitive->handle);
    rawSpanCount += entry ? entry->spanCount : 0U;
  }
  const auto &metrics = prepared.metrics();
  const std::size_t expectedPointScopes =
      static_cast<std::size_t>(std::ranges::count_if(
          prepared.source()->scopes(), [](const SketchOverlayScope &scope) {
            return scope.point.has_value();
          }));
  require(metrics.scopeCount == prepared.source()->scopes().size() &&
              metrics.entityScopeCount + metrics.pointScopeCount ==
                  metrics.scopeCount &&
              metrics.pointScopeCount == expectedPointScopes &&
              metrics.retainedBytes >=
                  sizeof(PreparedSketchOverlayRoleSet) +
                      prepared.drawSpans().size() *
                          sizeof(SketchPrimitiveChunkSpan) +
                      prepared.pointInstances().size() *
                          sizeof(SketchOverlayPointInstance) &&
              metrics.scratchBytes >=
                  rawSpanCount * sizeof(SketchPrimitiveChunkSpan) &&
              metrics.peakBytes == metrics.retainedBytes + metrics.scratchBytes,
          "prepared overlay role accounting is inconsistent");
}

void requireValidPreparedOverlay(const PreparedSketchOverlay &prepared) {
  std::size_t retainedBytes = sizeof(PreparedSketchOverlay);
  std::size_t scopeCount = 0U;
  std::size_t drawSpanCount = 0U;
  std::size_t pointInstanceCount = 0U;
  for (const PreparedSketchOverlayRoleSetPtr &role : prepared.roleSets()) {
    retainedBytes += role->metrics().retainedBytes;
    scopeCount += role->metrics().scopeCount;
    drawSpanCount += role->drawSpans().size();
    pointInstanceCount += role->pointInstances().size();
  }
  const auto &metrics = prepared.metrics();
  require(metrics.builtRoleSets + metrics.reusedRoleSets == 4U &&
              metrics.scopeCount == scopeCount &&
              metrics.drawSpanCount == drawSpanCount &&
              metrics.pointInstanceCount == pointInstanceCount &&
              metrics.retainedBytes == retainedBytes &&
              metrics.peakBytes == metrics.retainedBytes + metrics.scratchBytes,
          "prepared overlay packet accounting is inconsistent");
}

void verifyRoleOrderSpansAndPoints() {
  auto baseScene = scene(256, 7101, stamp(71, 1, 71, 71, 71, 1));
  auto base = preparedScene(baseScene, {});
  const PackedSketchPrimitive &line =
      firstKind(*baseScene, SketchPrimitiveKind::Line);
  const PackedSketchPrimitive &circle =
      firstKind(*baseScene, SketchPrimitiveKind::Circle);
  const PackedSketchPrimitive &arc =
      firstKind(*baseScene, SketchPrimitiveKind::Arc);

  RoleSets sets = emptyRoleSets(baseScene);
  const std::array hovered{
      SketchOverlayScope{line.entity, std::nullopt},
      SketchOverlayScope{line.entity, sketch::PointKey::Start},
      SketchOverlayScope{line.entity, sketch::PointKey::Start},
      SketchOverlayScope{line.entity, sketch::PointKey::End},
  };
  const std::array selected{
      SketchOverlayScope{line.entity, std::nullopt},
      SketchOverlayScope{circle.entity, std::nullopt},
      SketchOverlayScope{line.entity, sketch::PointKey::End},
  };
  const std::array preview{
      SketchOverlayScope{arc.entity, std::nullopt},
      SketchOverlayScope{arc.entity, sketch::PointKey::Center},
  };
  sets[0] = roleSet(baseScene, SketchOverlayRole::Hovered, hovered);
  sets[1] = roleSet(baseScene, SketchOverlayRole::Selected, selected);
  sets[2] = roleSet(baseScene, SketchOverlayRole::Preview, preview);
  auto source = overlay(baseScene, 1, sets);
  auto prepared = prepareSketchOverlay(source, base);
  require(prepared.has_value() && (*prepared)->source() == source &&
              (*prepared)->base() == base,
          "overlay preparation lost its exact packet or base");
  requireValidPreparedOverlay(**prepared);

  for (std::size_t index = 0U; index < overlayRoles.size(); ++index) {
    const auto &role = (*prepared)->roleSets()[index];
    require(role && role->role() == overlayRoles[index] &&
                (*prepared)->roleSet(overlayRoles[index]) == role,
            "prepared overlay role order is not fixed");
    requireValidPreparedRole(*base, *role);
    require(expanded(role->drawSpans()) ==
                expectedEntityTriangles(*base, role->source()->scopes()),
            "prepared overlay spans changed entity geometry ownership");
  }
  require(sets[0]->scopes().size() == 3U &&
              (*prepared)->roleSets()[0]->pointInstances().size() == 2U &&
              (*prepared)->roleSets()[1]->pointInstances().size() == 1U &&
              (*prepared)->roleSets()[2]->pointInstances().size() == 1U,
          "duplicate or overlapping entity/point scopes were conflated");
  require(expanded((*prepared)->roleSets()[0]->drawSpans()) ==
                  expectedEntityTriangles(*base, sets[0]->scopes()) &&
              expanded((*prepared)->roleSets()[1]->drawSpans()) !=
                  expanded((*prepared)->roleSets()[0]->drawSpans()),
          "cross-role overlap lost independent prepared geometry");
}

void verifyHiddenScopeOwnership() {
  const SceneStamp sceneStamp = stamp(71, 2, 71, 71, 71, 2);
  const auto visibleFlags =
      SketchPrimitiveFlags::Visible | SketchPrimitiveFlags::Selectable;
  auto hiddenHandle = SketchPrimitiveHandle::create(2U);
  auto visibleHandle = SketchPrimitiveHandle::create(1U);
  require(hiddenHandle && visibleHandle,
          "hidden overlay fixture handles were invalid");
  auto created = SketchSceneSnapshot::create(
      sceneStamp, styles(), {{0.0, 0.0}, {1.0, 1.0}, {2.0, 2.0}},
      {{id<SketchEntityId>(711U), *visibleHandle, 0U, 0U,
        SketchPrimitiveKind::Point, visibleFlags, 0.0, 0.0, 0.0},
       {id<SketchEntityId>(712U), *hiddenHandle, 1U, 0U,
        SketchPrimitiveKind::Line, SketchPrimitiveFlags::Selectable, 0.0, 0.0,
        0.0}});
  require(created.has_value(), "hidden overlay fixture scene was invalid");
  auto baseScene =
      std::make_shared<const SketchSceneSnapshot>(std::move(*created));
  auto base = preparedScene(baseScene, {});
  const std::array scopes{
      SketchOverlayScope{id<SketchEntityId>(712U), std::nullopt},
      SketchOverlayScope{id<SketchEntityId>(712U), sketch::PointKey::Start},
      SketchOverlayScope{id<SketchEntityId>(711U), sketch::PointKey::Point},
  };
  RoleSets sets = emptyRoleSets(baseScene);
  sets[1] = roleSet(baseScene, SketchOverlayRole::Selected, scopes);
  auto prepared = prepareSketchOverlay(overlay(baseScene, 1U, sets), base);
  require(prepared.has_value(), "hidden overlay scopes were rejected");
  const auto selected = (*prepared)->roleSet(SketchOverlayRole::Selected);
  require(selected->metrics().scopeCount == 3U &&
              selected->metrics().entityScopeCount == 1U &&
              selected->metrics().pointScopeCount == 2U &&
              selected->drawSpans().empty() &&
              selected->pointInstances().size() == 1U &&
              selected->pointInstances().front().primitive == *visibleHandle,
          "hidden overlay scopes entered displayed geometry or broke metrics");
  requireValidPreparedRole(*base, *selected);
  requireValidPreparedOverlay(**prepared);
}

std::size_t scaleCount(const testkit::PropertyProfile &profile) {
  return profile.iterations >= 1'000'000U ? 100'000U
         : profile.iterations >= 250'000U ? 50'000U
                                          : 10'000U;
}

void verifyLargeSelectionReuseAndCancellation(
    const testkit::PropertyProfile &profile) {
  const std::size_t count = scaleCount(profile);
  auto baseScene = scene(count, 7201, stamp(72, 1, 72, 72, 72, 1));
  auto base = preparedScene(baseScene, {});
  std::vector<SketchOverlayScope> selection;
  selection.reserve(baseScene->primitives().size());
  for (const PackedSketchPrimitive &primitive : baseScene->primitives())
    selection.push_back({primitive.entity, std::nullopt});

  RoleSets firstSets = emptyRoleSets(baseScene);
  firstSets[0] =
      roleSet(baseScene, SketchOverlayRole::Hovered,
              std::array{SketchOverlayScope{
                  baseScene->primitives().front().entity, std::nullopt}});
  firstSets[1] = roleSet(baseScene, SketchOverlayRole::Selected, selection);
  auto firstSource = overlay(baseScene, 1, firstSets);
  auto first = prepareSketchOverlay(firstSource, base);
  require(first.has_value() && (*first)->roleSet(SketchOverlayRole::Selected)
                                       ->metrics()
                                       .scopeCount == count,
          "scaled selection overlay preparation failed");
  requireValidPreparedOverlay(**first);
  requireValidPreparedRole(*base,
                           *(*first)->roleSet(SketchOverlayRole::Selected));

  RoleSets secondSets = firstSets;
  secondSets[0] =
      roleSet(baseScene, SketchOverlayRole::Hovered,
              std::array{SketchOverlayScope{
                  baseScene->primitives().back().entity, std::nullopt}});
  auto secondSource = overlay(baseScene, 2, secondSets);
  auto second = prepareSketchOverlay(secondSource, base, {}, *first);
  require(second.has_value() &&
              (*second)->roleSet(SketchOverlayRole::Selected) ==
                  (*first)->roleSet(SketchOverlayRole::Selected) &&
              (*second)->roleSet(SketchOverlayRole::Hovered) !=
                  (*first)->roleSet(SketchOverlayRole::Hovered) &&
              (*second)->metrics().reusedRoleSets == 3U &&
              (*second)->metrics().builtRoleSets == 1U &&
              (*second)->metrics().builtScopes == 1U,
          "one-entry hover update rebuilt stable selection work");
  for (std::size_t index = 1U; index < overlayRoles.size(); ++index)
    require((*second)->roleSets()[index] == (*first)->roleSets()[index],
            "hover update replaced an unchanged prepared role pointer");
  requireValidPreparedOverlay(**second);

  std::stop_source stopped;
  require(stopped.request_stop(), "overlay cancellation source was stopped");
  auto cancelled =
      prepareSketchOverlay(secondSource, base, {}, *first, stopped.get_token());
  require(!cancelled && cancelled.error().code ==
                            "desktop.sketch.overlay-preparation-cancelled",
          "cancelled overlay preparation returned partial output");

  std::stop_source crossThreadCancellation;
  ProjectionAllocationControl allocationGate;
  allocationGate.blockAt = 1U;
  std::optional<Result<std::shared_ptr<const PreparedSketchOverlay>>> result;
  std::thread worker([&] {
    projectionAllocationControl = &allocationGate;
    result.emplace(prepareSketchOverlay(firstSource, base, {}, {},
                                        crossThreadCancellation.get_token()));
  });
  while (!allocationGate.reached.load(std::memory_order_acquire))
    std::this_thread::yield();
  static_cast<void>(crossThreadCancellation.request_stop());
  allocationGate.release.store(true, std::memory_order_release);
  worker.join();
  require(result && !*result &&
              result->error().code ==
                  "desktop.sketch.overlay-preparation-cancelled",
          "mid-work overlay preparation ignored cancellation");

  ProjectionAllocationControl allocationFailure;
  allocationFailure.failAt = 1U;
  projectionAllocationControl = &allocationFailure;
  auto failed = prepareSketchOverlay(firstSource, base);
  projectionAllocationControl = nullptr;
  require(!failed && failed.error().code ==
                         "desktop.sketch.overlay-projection-allocation",
          "overlay allocation failure escaped its Result boundary");
}

void verifyCancellationPollerBoundaries() {
  std::stop_source intervalSource;
  kearne::ui::detail::CancellationPoller interval{intervalSource.get_token()};
  interval.checkpoint(255U);
  static_cast<void>(intervalSource.request_stop());
  interval.checkpoint(0U);
  bool intervalCancelled = false;
  try {
    interval.checkpoint(1U);
  } catch (const kearne::ui::detail::SketchProjectionCancelled &) {
    intervalCancelled = true;
  }

  std::stop_source maximumSource;
  kearne::ui::detail::CancellationPoller maximum{maximumSource.get_token()};
  maximum.checkpoint(std::numeric_limits<std::size_t>::max());
  static_cast<void>(maximumSource.request_stop());
  bool maximumCancelled = false;
  try {
    maximum.checkpoint(1U);
  } catch (const kearne::ui::detail::SketchProjectionCancelled &) {
    maximumCancelled = true;
  }
  require(intervalCancelled && maximumCancelled,
          "projection cancellation poll interval arithmetic was incorrect");
}

void verifyBaseAndLimitRejection() {
  auto firstScene = scene(32, 7301, stamp(73, 1, 73, 73, 73, 1));
  auto secondScene = scene(32, 7302, stamp(74, 1, 74, 74, 74, 1));
  auto firstBase = preparedScene(firstScene, {});
  auto secondBase = preparedScene(secondScene, {});
  auto source = overlay(firstScene, 1, emptyRoleSets(firstScene));
  auto wrongBase = prepareSketchOverlay(source, secondBase);
  require(!wrongBase && wrongBase.error().code ==
                            "desktop.sketch.overlay-projection-base-mismatch",
          "overlay preparation accepted a changed base");

  auto emptyMeasured = prepareSketchOverlay(source, firstBase);
  require(emptyMeasured.has_value(), "empty overlay fixture was rejected");
  SketchOverlayProjectionLimits emptyLimits;
  emptyLimits.maximumScopeCount = 0U;
  emptyLimits.maximumDrawSpanCount = 0U;
  emptyLimits.maximumPointInstanceCount = 0U;
  emptyLimits.maximumRetainedBytes = (*emptyMeasured)->metrics().retainedBytes;
  emptyLimits.maximumScratchBytes = 0U;
  emptyLimits.maximumPeakBytes = (*emptyMeasured)->metrics().peakBytes;
  require(prepareSketchOverlay(source, firstBase, emptyLimits).has_value(),
          "zero element and scratch budgets rejected an empty overlay");

  RoleSets selectedSets = emptyRoleSets(firstScene);
  selectedSets[1] =
      roleSet(firstScene, SketchOverlayRole::Selected,
              std::array{SketchOverlayScope{
                  firstScene->primitives().front().entity, std::nullopt}});
  auto selected = overlay(firstScene, 2, selectedSets);
  auto measured = prepareSketchOverlay(selected, firstBase);
  require(measured.has_value(), "overlay budget fixture was rejected");
  auto replacementBase = preparedScene(firstScene, {});
  auto changedPreparedBase =
      prepareSketchOverlay(selected, replacementBase, {}, *measured);
  require(changedPreparedBase.has_value() &&
              (*changedPreparedBase)->base() == replacementBase &&
              (*changedPreparedBase)->metrics().builtRoleSets == 4U &&
              (*changedPreparedBase)->metrics().reusedRoleSets == 0U &&
              (*changedPreparedBase)->roleSet(SketchOverlayRole::Selected) !=
                  (*measured)->roleSet(SketchOverlayRole::Selected),
          "changed prepared base reused stale overlay preparation");
  SketchOverlayProjectionLimits exact;
  exact.maximumScopeCount = 1U;
  exact.maximumDrawSpanCount = std::max<std::size_t>(
      1U,
      (*measured)->roleSet(SketchOverlayRole::Selected)->drawSpans().size());
  exact.maximumPointInstanceCount = 1U;
  exact.maximumRetainedBytes = (*measured)->metrics().retainedBytes;
  exact.maximumScratchBytes = (*measured)->metrics().scratchBytes;
  exact.maximumPeakBytes = (*measured)->metrics().peakBytes;
  require(prepareSketchOverlay(selected, firstBase, exact).has_value(),
          "exact overlay projection budgets were refused");

  SketchOverlayProjectionLimits retainedLimit = exact;
  --retainedLimit.maximumRetainedBytes;
  auto retained = prepareSketchOverlay(selected, firstBase, retainedLimit);
  require(!retained && retained.error().code ==
                           "desktop.sketch.overlay-projection-retained-limit",
          "overlay retained-byte limit was not enforced");

  SketchOverlayProjectionLimits scratchLimit = exact;
  --scratchLimit.maximumScratchBytes;
  auto scratch = prepareSketchOverlay(selected, firstBase, scratchLimit);
  require(!scratch && scratch.error().code ==
                          "desktop.sketch.overlay-projection-scratch-limit",
          "overlay scratch-byte limit was not enforced");

  SketchOverlayProjectionLimits peakLimit = exact;
  --peakLimit.maximumPeakBytes;
  auto peak = prepareSketchOverlay(selected, firstBase, peakLimit);
  require(!peak && peak.error().code ==
                       "desktop.sketch.overlay-projection-peak-limit",
          "overlay peak-byte limit was not enforced");

  const PackedSketchPrimitive &point =
      firstKind(*firstScene, SketchPrimitiveKind::Point);
  const std::array aggregateScopes{
      SketchOverlayScope{point.entity, std::nullopt},
      SketchOverlayScope{point.entity, sketch::PointKey::Point}};
  RoleSets aggregateSets = emptyRoleSets(firstScene);
  aggregateSets[0] =
      roleSet(firstScene, SketchOverlayRole::Hovered, aggregateScopes);
  aggregateSets[1] =
      roleSet(firstScene, SketchOverlayRole::Selected, aggregateScopes);
  auto aggregateSource = overlay(firstScene, 3U, aggregateSets);
  auto aggregateMeasured = prepareSketchOverlay(aggregateSource, firstBase);
  require(aggregateMeasured.has_value(),
          "multi-role overlay budget fixture was rejected");
  const PreparedSketchOverlayMetrics aggregateMetrics =
      (*aggregateMeasured)->metrics();
  SketchOverlayProjectionLimits aggregateExact{
      aggregateMetrics.scopeCount,         aggregateMetrics.drawSpanCount,
      aggregateMetrics.pointInstanceCount, aggregateMetrics.retainedBytes,
      aggregateMetrics.scratchBytes,       aggregateMetrics.peakBytes};
  require(prepareSketchOverlay(aggregateSource, firstBase, aggregateExact)
              .has_value(),
          "exact packet-wide overlay counts were refused");
  auto aggregateLimited = aggregateExact;
  --aggregateLimited.maximumScopeCount;
  auto scopeLimited = prepareSketchOverlay(
      aggregateSource, firstBase, aggregateLimited, *aggregateMeasured);
  require(!scopeLimited && scopeLimited.error().code ==
                               "desktop.sketch.overlay-projection-count-limit",
          "packet-wide overlay scope limit was applied per role");
  aggregateLimited = aggregateExact;
  --aggregateLimited.maximumDrawSpanCount;
  auto drawLimited = prepareSketchOverlay(aggregateSource, firstBase,
                                          aggregateLimited, *aggregateMeasured);
  require(!drawLimited && drawLimited.error().code ==
                              "desktop.sketch.overlay-projection-count-limit",
          "packet-wide overlay draw-span limit was applied per role");
  aggregateLimited = aggregateExact;
  --aggregateLimited.maximumPointInstanceCount;
  auto pointLimited = prepareSketchOverlay(
      aggregateSource, firstBase, aggregateLimited, *aggregateMeasured);
  require(!pointLimited && pointLimited.error().code ==
                               "desktop.sketch.overlay-projection-count-limit",
          "packet-wide overlay point limit was applied per role");
}

void verifyGeneratedOverlayProjection(const testkit::PropertyProfile &profile) {
  testkit::PropertyProfile bounded = profile;
  bounded.iterations = static_cast<std::uint64_t>(
      std::ceil(std::cbrt(static_cast<double>(profile.iterations))));
  testkit::checkProperty(
      "native overlay projection invariants", bounded,
      [](testkit::Random &random, std::uint64_t iteration) {
        const std::size_t count =
            static_cast<std::size_t>(random.next() % 480U + 32U);
        auto baseScene = scene(count, random.next(),
                               stamp(80 + iteration, 1, 80 + iteration,
                                     80 + iteration, 80 + iteration, 1));
        auto base = preparedScene(baseScene, {});
        RoleSets sets = emptyRoleSets(baseScene);
        for (std::size_t role = 0U; role < sets.size(); ++role) {
          std::vector<SketchOverlayScope> scopes;
          const std::size_t scopeCount =
              static_cast<std::size_t>(random.next() % count + 1U);
          scopes.reserve(scopeCount + scopeCount / 4U);
          for (std::size_t index = 0U; index < scopeCount; ++index) {
            const PackedSketchPrimitive &primitive =
                baseScene->primitives()[static_cast<std::size_t>(
                    random.next() % baseScene->primitives().size())];
            scopes.push_back({primitive.entity, std::nullopt});
            if (index % 4U == 0U)
              scopes.push_back(scopes.back());
          }
          sets[role] = roleSet(baseScene, overlayRoles[role], scopes);
        }
        auto source = overlay(baseScene, 1, sets);
        auto prepared = prepareSketchOverlay(source, base);
        require(prepared.has_value(),
                "generated overlay projection was rejected");
        requireValidPreparedOverlay(**prepared);
        for (std::size_t role = 0U; role < sets.size(); ++role) {
          const auto &projected = (*prepared)->roleSets()[role];
          require(projected->role() == overlayRoles[role] &&
                      expanded(projected->drawSpans()) ==
                          expectedEntityTriangles(*base, sets[role]->scopes()),
                  "generated overlay projection changed exact ownership");
          requireValidPreparedRole(*base, *projected);
        }
      });
}

} // namespace

int main(int argc, char *argv[]) {
  try {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication application(argc, argv);
    const auto profile = kearne::testkit::propertyProfile();
    verifyRoleOrderSpansAndPoints();
    verifyHiddenScopeOwnership();
    verifyCancellationPollerBoundaries();
    verifyLargeSelectionReuseAndCancellation(profile);
    verifyBaseAndLimitRejection();
    verifyGeneratedOverlayProjection(profile);
    std::cout << "verified native overlay projection at " << scaleCount(profile)
              << " scale and " << profile.iterations
              << " generated profile iterations\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
