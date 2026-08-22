#include "sketch_overlay_projection.hpp"
#include "sketch_projection_support.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <new>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <utility>

namespace kearne::ui {
namespace {

[[nodiscard]] Diagnostic cancelledPreparation() {
  return diagnostic("desktop.sketch.overlay-preparation-cancelled",
                    "sketch overlay preparation was cancelled");
}

[[nodiscard]] std::optional<std::size_t>
roleIndex(render::SketchOverlayRole role) {
  switch (role) {
  case render::SketchOverlayRole::Hovered:
    return 0U;
  case render::SketchOverlayRole::Selected:
    return 1U;
  case render::SketchOverlayRole::Preview:
    return 2U;
  case render::SketchOverlayRole::Diagnostic:
    return 3U;
  }
  return std::nullopt;
}

[[nodiscard]] render::SketchOverlayRole roleAt(std::size_t index) {
  constexpr std::array roles{
      render::SketchOverlayRole::Hovered,
      render::SketchOverlayRole::Selected,
      render::SketchOverlayRole::Preview,
      render::SketchOverlayRole::Diagnostic,
  };
  return roles[index];
}

[[nodiscard]] Result<PreparedSketchOverlayRoleMetrics>
roleMetrics(std::size_t scopeCount, std::size_t entityScopeCount,
            std::size_t pointScopeCount, std::size_t drawSpanCapacity,
            std::size_t pointCapacity, std::size_t scratchSpanCapacity) {
  PreparedSketchOverlayRoleMetrics metrics;
  metrics.scopeCount = scopeCount;
  metrics.entityScopeCount = entityScopeCount;
  metrics.pointScopeCount = pointScopeCount;
  std::size_t drawBytes = 0U;
  std::size_t pointBytes = 0U;
  std::size_t scratchBytes = 0U;
  std::size_t retainedPayload = 0U;
  if (!detail::checkedSizeMultiply(
          drawSpanCapacity, sizeof(SketchPrimitiveChunkSpan), drawBytes) ||
      !detail::checkedSizeMultiply(
          pointCapacity, sizeof(SketchOverlayPointInstance), pointBytes) ||
      !detail::checkedSizeMultiply(scratchSpanCapacity,
                                   sizeof(SketchPrimitiveChunkSpan),
                                   scratchBytes) ||
      !detail::checkedSizeAdd(drawBytes, pointBytes, retainedPayload) ||
      !detail::checkedSizeAdd(sizeof(PreparedSketchOverlayRoleSet),
                              retainedPayload, metrics.retainedBytes) ||
      !detail::checkedSizeAdd(metrics.retainedBytes, scratchBytes,
                              metrics.peakBytes))
    return std::unexpected(
        diagnostic("desktop.sketch.overlay-projection-memory-overflow",
                   "sketch overlay projection byte accounting overflowed"));
  metrics.scratchBytes = scratchBytes;
  return metrics;
}

[[nodiscard]] Result<void>
validateRoleLimits(const PreparedSketchOverlayRoleMetrics &metrics,
                   std::size_t drawSpanCount, std::size_t pointInstanceCount,
                   const SketchOverlayProjectionLimits &limits) {
  if (metrics.scopeCount > limits.maximumScopeCount ||
      drawSpanCount > limits.maximumDrawSpanCount ||
      pointInstanceCount > limits.maximumPointInstanceCount)
    return std::unexpected(diagnostic(
        "desktop.sketch.overlay-projection-count-limit",
        "sketch overlay projection exceeded a packed element limit"));
  if (metrics.retainedBytes > limits.maximumRetainedBytes)
    return std::unexpected(diagnostic(
        "desktop.sketch.overlay-projection-retained-limit",
        "sketch overlay projection exceeded its retained byte limit"));
  if (metrics.scratchBytes > limits.maximumScratchBytes)
    return std::unexpected(diagnostic(
        "desktop.sketch.overlay-projection-scratch-limit",
        "sketch overlay projection exceeded its scratch byte limit"));
  if (metrics.peakBytes > limits.maximumPeakBytes)
    return std::unexpected(
        diagnostic("desktop.sketch.overlay-projection-peak-limit",
                   "sketch overlay projection exceeded its peak byte limit"));
  return {};
}

struct RoleBuild {
  std::vector<SketchPrimitiveChunkSpan> drawSpans;
  std::vector<SketchOverlayPointInstance> points;
  PreparedSketchOverlayRoleMetrics metrics;
};

[[nodiscard]] Result<RoleBuild>
buildRole(const render::SketchOverlayRoleSetPtr &source,
          const PreparedSketchScene &base,
          const SketchOverlayProjectionLimits &limits,
          detail::CancellationPoller &cancellation) {
  const auto scopes = source->scopes();
  if (scopes.size() > limits.maximumScopeCount)
    return std::unexpected(
        diagnostic("desktop.sketch.overlay-projection-count-limit",
                   "sketch overlay projection exceeded its scope limit"));
  const auto &scene = *base.scene();
  const auto &primitiveIndex = *base.primitiveVectorIndex();
  std::size_t entityScopeCount = 0U;
  std::size_t pointScopeCount = 0U;
  std::size_t rawSpanCount = 0U;
  std::size_t visiblePointCount = 0U;
  for (const render::SketchOverlayScope &scope : scopes) {
    cancellation.checkpoint();
    const render::PackedSketchPrimitive *primitive =
        scene.findPrimitive(scope.entity);
    if (!primitive)
      return std::unexpected(diagnostic(
          "desktop.sketch.overlay-projection-unknown-entity",
          "sketch overlay preparation references unknown base geometry"));
    if (scope.point) {
      ++pointScopeCount;
      if (render::hasFlag(primitive->flags,
                          render::SketchPrimitiveFlags::Visible))
        ++visiblePointCount;
      continue;
    }
    ++entityScopeCount;
    const SketchPrimitiveVectorEntry *entry =
        primitiveIndex.find(primitive->handle);
    const bool visible = render::hasFlag(primitive->flags,
                                         render::SketchPrimitiveFlags::Visible);
    if (!visible) {
      if (entry)
        return std::unexpected(diagnostic(
            "desktop.sketch.overlay-projection-hidden-primitive",
            "hidden overlay geometry owns prepared vector ranges"));
      continue;
    }
    if (!entry) {
      return std::unexpected(diagnostic(
          "desktop.sketch.overlay-projection-missing-primitive",
          "visible overlay geometry has no prepared vector ranges"));
    }
    const auto spans = primitiveIndex.spans(primitive->handle);
    if (spans.empty() || spans.size() != entry->spanCount)
      return std::unexpected(diagnostic(
          "desktop.sketch.overlay-projection-invalid-span",
          "visible overlay geometry has an invalid vector range"));
    if (spans.size() > std::numeric_limits<std::size_t>::max() - rawSpanCount)
      return std::unexpected(
          diagnostic("desktop.sketch.overlay-projection-memory-overflow",
                     "sketch overlay span count overflowed"));
    rawSpanCount += spans.size();
  }
  if (visiblePointCount > limits.maximumPointInstanceCount)
    return std::unexpected(diagnostic(
        "desktop.sketch.overlay-projection-count-limit",
        "sketch overlay projection exceeded its point-instance limit"));

  std::vector<SketchPrimitiveChunkSpan> rawSpans;
  std::vector<SketchOverlayPointInstance> points;
  if (rawSpanCount > rawSpans.max_size() ||
      visiblePointCount > points.max_size())
    return std::unexpected(
        diagnostic("desktop.sketch.overlay-projection-capacity-limit",
                   "sketch overlay projection exceeds vector capacity"));
  auto minimumMetrics =
      roleMetrics(scopes.size(), entityScopeCount, pointScopeCount, 0U,
                  visiblePointCount, rawSpanCount);
  if (!minimumMetrics)
    return std::unexpected(std::move(minimumMetrics.error()));
  if (auto bounded =
          validateRoleLimits(*minimumMetrics, 0U, visiblePointCount, limits);
      !bounded)
    return std::unexpected(std::move(bounded.error()));

  rawSpans.reserve(rawSpanCount);
  auto rawCapacityMetrics =
      roleMetrics(scopes.size(), entityScopeCount, pointScopeCount, 0U,
                  visiblePointCount, rawSpans.capacity());
  if (!rawCapacityMetrics)
    return std::unexpected(std::move(rawCapacityMetrics.error()));
  if (auto bounded = validateRoleLimits(*rawCapacityMetrics, 0U,
                                        visiblePointCount, limits);
      !bounded)
    return std::unexpected(std::move(bounded.error()));
  points.reserve(visiblePointCount);
  auto pointCapacityMetrics =
      roleMetrics(scopes.size(), entityScopeCount, pointScopeCount, 0U,
                  points.capacity(), rawSpans.capacity());
  if (!pointCapacityMetrics)
    return std::unexpected(std::move(pointCapacityMetrics.error()));
  if (auto bounded = validateRoleLimits(*pointCapacityMetrics, 0U,
                                        visiblePointCount, limits);
      !bounded)
    return std::unexpected(std::move(bounded.error()));
  const auto chunks = base.packet()->chunks();
  for (const render::SketchOverlayScope &scope : scopes) {
    cancellation.checkpoint();
    const render::PackedSketchPrimitive *primitive =
        scene.findPrimitive(scope.entity);
    if (!render::hasFlag(primitive->flags,
                         render::SketchPrimitiveFlags::Visible))
      continue;
    if (scope.point) {
      const auto position =
          render::semanticPoint(scene, *primitive, *scope.point);
      if (!position)
        return std::unexpected(diagnostic(
            "desktop.sketch.overlay-projection-invalid-point",
            "sketch overlay preparation references an invalid semantic point"));
      points.push_back(
          {primitive->handle, *scope.point, *position, primitive->style});
      continue;
    }
    const SketchPrimitiveVectorEntry *entry =
        primitiveIndex.find(primitive->handle);
    const auto spans = primitiveIndex.spans(primitive->handle);
    if (!entry || spans.empty() || spans.size() != entry->spanCount)
      return std::unexpected(diagnostic(
          "desktop.sketch.overlay-projection-invalid-span",
          "visible overlay geometry has an invalid vector range"));
    std::size_t recordCount = 0U;
    for (const SketchPrimitiveChunkSpan span : spans) {
      cancellation.checkpoint();
      if (span.chunk >= chunks.size() || span.recordCount == 0U ||
          static_cast<std::size_t>(span.firstRecord) >
              chunks[span.chunk]->records().size() ||
          span.recordCount >
              chunks[span.chunk]->records().size() - span.firstRecord ||
          chunks[span.chunk]->style() != primitive->style ||
          chunks[span.chunk]->layer() != scene.styles()[primitive->style].layer)
        return std::unexpected(diagnostic(
            "desktop.sketch.overlay-projection-invalid-span",
            "sketch overlay preparation found an invalid primitive range"));
      if (!detail::checkedSizeAdd(recordCount, span.recordCount, recordCount))
        return std::unexpected(
            diagnostic("desktop.sketch.overlay-projection-span-overflow",
                       "sketch overlay primitive record count overflowed"));
      rawSpans.push_back(span);
    }
    if (recordCount != entry->recordCount)
      return std::unexpected(
          diagnostic("desktop.sketch.overlay-projection-invalid-span",
                     "sketch overlay primitive record total is inconsistent"));
  }
  cancellation.checkpointNow();
  std::ranges::sort(rawSpans, [&](const SketchPrimitiveChunkSpan &first,
                                  const SketchPrimitiveChunkSpan &second) {
    cancellation.checkpoint();
    return first.chunk != second.chunk ? first.chunk < second.chunk
                                       : first.firstRecord < second.firstRecord;
  });

  std::size_t canonicalSpanCount = 0U;
  std::optional<SketchPrimitiveChunkSpan> previous;
  for (const SketchPrimitiveChunkSpan span : rawSpans) {
    cancellation.checkpoint();
    const std::size_t previousEnd =
        previous ? static_cast<std::size_t>(previous->firstRecord) +
                       previous->recordCount
                 : 0U;
    if (previous && previous->chunk == span.chunk &&
        span.firstRecord < previousEnd)
      return std::unexpected(
          diagnostic("desktop.sketch.overlay-projection-overlapping-span",
                     "sketch overlay primitive ranges overlap"));
    if (!previous || previous->chunk != span.chunk ||
        previousEnd != span.firstRecord) {
      ++canonicalSpanCount;
      previous = span;
    } else {
      if (span.recordCount >
          std::numeric_limits<std::uint32_t>::max() - previous->recordCount)
        return std::unexpected(diagnostic(
            "desktop.sketch.overlay-projection-span-overflow",
            "coalesced sketch overlay range exceeds packed record capacity"));
      previous->recordCount += span.recordCount;
    }
  }
  auto canonicalMetrics =
      roleMetrics(scopes.size(), entityScopeCount, pointScopeCount,
                  canonicalSpanCount, points.capacity(), rawSpans.capacity());
  if (!canonicalMetrics)
    return std::unexpected(std::move(canonicalMetrics.error()));
  if (auto bounded = validateRoleLimits(*canonicalMetrics, canonicalSpanCount,
                                        points.size(), limits);
      !bounded)
    return std::unexpected(std::move(bounded.error()));
  std::vector<SketchPrimitiveChunkSpan> drawSpans;
  if (canonicalSpanCount > drawSpans.max_size())
    return std::unexpected(
        diagnostic("desktop.sketch.overlay-projection-capacity-limit",
                   "sketch overlay projection exceeds vector capacity"));
  drawSpans.reserve(canonicalSpanCount);
  auto drawCapacityMetrics =
      roleMetrics(scopes.size(), entityScopeCount, pointScopeCount,
                  drawSpans.capacity(), points.capacity(), rawSpans.capacity());
  if (!drawCapacityMetrics)
    return std::unexpected(std::move(drawCapacityMetrics.error()));
  if (auto bounded = validateRoleLimits(
          *drawCapacityMetrics, canonicalSpanCount, points.size(), limits);
      !bounded)
    return std::unexpected(std::move(bounded.error()));
  for (const SketchPrimitiveChunkSpan span : rawSpans) {
    cancellation.checkpoint();
    const std::size_t previousEnd =
        drawSpans.empty()
            ? 0U
            : static_cast<std::size_t>(drawSpans.back().firstRecord) +
                  drawSpans.back().recordCount;
    if (drawSpans.empty() || drawSpans.back().chunk != span.chunk ||
        previousEnd != span.firstRecord) {
      drawSpans.push_back(span);
    } else {
      if (span.recordCount > std::numeric_limits<std::uint32_t>::max() -
                                 drawSpans.back().recordCount)
        return std::unexpected(diagnostic(
            "desktop.sketch.overlay-projection-span-overflow",
            "coalesced sketch overlay range exceeds packed record capacity"));
      drawSpans.back().recordCount += span.recordCount;
    }
  }
  auto metrics =
      roleMetrics(scopes.size(), entityScopeCount, pointScopeCount,
                  drawSpans.capacity(), points.capacity(), rawSpans.capacity());
  if (!metrics)
    return std::unexpected(std::move(metrics.error()));
  if (auto bounded =
          validateRoleLimits(*metrics, drawSpans.size(), points.size(), limits);
      !bounded)
    return std::unexpected(std::move(bounded.error()));
  cancellation.checkpointNow();
  return RoleBuild{std::move(drawSpans), std::move(points), *metrics};
}

} // namespace

PreparedSketchOverlayRoleSet::PreparedSketchOverlayRoleSet(
    render::SketchOverlayRoleSetPtr source,
    std::vector<SketchPrimitiveChunkSpan> drawSpans,
    std::vector<SketchOverlayPointInstance> pointInstances,
    PreparedSketchOverlayRoleMetrics metrics)
    : source_(std::move(source)), drawSpans_(std::move(drawSpans)),
      pointInstances_(std::move(pointInstances)), metrics_(metrics) {}

PreparedSketchOverlay::PreparedSketchOverlay(
    std::shared_ptr<const render::SketchPresentationOverlay> source,
    std::shared_ptr<const PreparedSketchScene> base,
    std::array<PreparedSketchOverlayRoleSetPtr, 4> roleSets,
    PreparedSketchOverlayMetrics metrics)
    : source_(std::move(source)), base_(std::move(base)),
      roleSets_(std::move(roleSets)), metrics_(metrics) {}

PreparedSketchOverlayRoleSetPtr
PreparedSketchOverlay::roleSet(render::SketchOverlayRole role) const {
  const auto index = roleIndex(role);
  return index ? roleSets_[*index] : nullptr;
}

Result<std::shared_ptr<const PreparedSketchOverlay>> prepareSketchOverlay(
    std::shared_ptr<const render::SketchPresentationOverlay> overlay,
    std::shared_ptr<const PreparedSketchScene> base,
    SketchOverlayProjectionLimits limits,
    std::shared_ptr<const PreparedSketchOverlay> reuse,
    std::stop_token cancellationToken) {
  detail::CancellationPoller cancellation{cancellationToken};
  try {
    cancellation.checkpointNow();
    if (!overlay)
      return std::unexpected(
          diagnostic("desktop.sketch.overlay-projection-null-overlay",
                     "sketch overlay preparation requires an overlay"));
    if (!base || !base->scene() || !base->packet() ||
        !base->primitiveVectorIndex())
      return std::unexpected(diagnostic(
          "desktop.sketch.overlay-projection-null-base",
          "sketch overlay preparation requires a prepared base scene"));
    if (overlay->base() != base->scene())
      return std::unexpected(diagnostic(
          "desktop.sketch.overlay-projection-base-mismatch",
          "sketch overlay does not retain the exact prepared base scene"));
    std::array<PreparedSketchOverlayRoleSetPtr, 4> preparedRoles;
    PreparedSketchOverlayMetrics metrics;
    const auto includeRoleMetrics =
        [&](const PreparedSketchOverlayRoleSet &prepared) -> Result<void> {
      if (!detail::checkedSizeAdd(metrics.scopeCount,
                                  prepared.metrics().scopeCount,
                                  metrics.scopeCount) ||
          !detail::checkedSizeAdd(metrics.drawSpanCount,
                                  prepared.drawSpans().size(),
                                  metrics.drawSpanCount) ||
          !detail::checkedSizeAdd(metrics.pointInstanceCount,
                                  prepared.pointInstances().size(),
                                  metrics.pointInstanceCount))
        return std::unexpected(
            diagnostic("desktop.sketch.overlay-projection-count-overflow",
                       "sketch overlay packet element count overflowed"));
      if (metrics.scopeCount > limits.maximumScopeCount ||
          metrics.drawSpanCount > limits.maximumDrawSpanCount ||
          metrics.pointInstanceCount > limits.maximumPointInstanceCount)
        return std::unexpected(diagnostic(
            "desktop.sketch.overlay-projection-count-limit",
            "sketch overlay packet exceeded a packed element limit"));
      return {};
    };
    for (std::size_t index = 0U; index < preparedRoles.size(); ++index) {
      cancellation.checkpointNow();
      const render::SketchOverlayRole role = roleAt(index);
      const render::SketchOverlayRoleSetPtr source = overlay->roleSet(role);
      PreparedSketchOverlayRoleSetPtr prior;
      if (reuse && reuse->base() == base)
        prior = reuse->roleSet(role);
      if (prior && prior->source() == source) {
        if (prior->metrics().scopeCount > limits.maximumScopeCount ||
            prior->drawSpans().size() > limits.maximumDrawSpanCount ||
            prior->pointInstances().size() > limits.maximumPointInstanceCount ||
            prior->metrics().retainedBytes > limits.maximumRetainedBytes)
          return std::unexpected(diagnostic(
              "desktop.sketch.overlay-projection-reuse-limit",
              "reused sketch overlay role exceeds current projection limits"));
        preparedRoles[index] = std::move(prior);
        ++metrics.reusedRoleSets;
        if (auto included = includeRoleMetrics(*preparedRoles[index]);
            !included)
          return std::unexpected(std::move(included.error()));
        continue;
      }
      auto built = buildRole(source, *base, limits, cancellation);
      if (!built)
        return std::unexpected(std::move(built.error()));
      if (!detail::checkedSizeAdd(metrics.builtScopes,
                                  built->metrics.scopeCount,
                                  metrics.builtScopes))
        return std::unexpected(
            diagnostic("desktop.sketch.overlay-projection-count-overflow",
                       "built sketch overlay scope count overflowed"));
      metrics.scratchBytes =
          std::max(metrics.scratchBytes, built->metrics.scratchBytes);
      preparedRoles[index] =
          std::shared_ptr<const PreparedSketchOverlayRoleSet>(
              new PreparedSketchOverlayRoleSet{
                  source, std::move(built->drawSpans), std::move(built->points),
                  built->metrics});
      ++metrics.builtRoleSets;
      if (auto included = includeRoleMetrics(*preparedRoles[index]); !included)
        return std::unexpected(std::move(included.error()));
    }

    metrics.retainedBytes = sizeof(PreparedSketchOverlay);
    for (const PreparedSketchOverlayRoleSetPtr &role : preparedRoles) {
      if (!detail::checkedSizeAdd(metrics.retainedBytes,
                                  role->metrics().retainedBytes,
                                  metrics.retainedBytes))
        return std::unexpected(
            diagnostic("desktop.sketch.overlay-projection-memory-overflow",
                       "sketch overlay packet byte accounting overflowed"));
    }
    if (!detail::checkedSizeAdd(metrics.retainedBytes, metrics.scratchBytes,
                                metrics.peakBytes))
      return std::unexpected(
          diagnostic("desktop.sketch.overlay-projection-memory-overflow",
                     "sketch overlay packet byte accounting overflowed"));
    if (metrics.retainedBytes > limits.maximumRetainedBytes)
      return std::unexpected(
          diagnostic("desktop.sketch.overlay-projection-retained-limit",
                     "sketch overlay packet exceeded its retained byte limit"));
    if (metrics.scratchBytes > limits.maximumScratchBytes)
      return std::unexpected(
          diagnostic("desktop.sketch.overlay-projection-scratch-limit",
                     "sketch overlay packet exceeded its scratch byte limit"));
    if (metrics.peakBytes > limits.maximumPeakBytes)
      return std::unexpected(
          diagnostic("desktop.sketch.overlay-projection-peak-limit",
                     "sketch overlay packet exceeded its peak byte limit"));
    cancellation.checkpointNow();
    auto prepared = std::shared_ptr<const PreparedSketchOverlay>(
        new PreparedSketchOverlay{std::move(overlay), std::move(base),
                                  std::move(preparedRoles), metrics});
    cancellation.checkpointNow();
    return prepared;
  } catch (const detail::SketchProjectionCancelled &) {
    return std::unexpected(cancelledPreparation());
  } catch (const std::bad_alloc &) {
    return std::unexpected(diagnostic(
        "desktop.sketch.overlay-projection-allocation",
        "sketch overlay preparation could not allocate bounded storage"));
  } catch (const std::length_error &) {
    return std::unexpected(
        diagnostic("desktop.sketch.overlay-projection-capacity-limit",
                   "sketch overlay projection exceeds vector capacity"));
  }
}

} // namespace kearne::ui
