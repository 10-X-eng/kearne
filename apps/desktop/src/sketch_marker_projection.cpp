#include "sketch_marker_projection.hpp"
#include "sketch_projection_support.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <new>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace kearne::ui {
namespace {

struct MarkerLayoutEntry {
  std::uint64_t anchorX = 0U;
  std::uint64_t anchorY = 0U;
  std::uint32_t marker = 0U;
  std::uint8_t family = 0U;
  std::uint8_t priority = 0U;
};

[[nodiscard]] Result<PreparedSketchMarkerMetrics>
markerMetrics(std::size_t markerCount, std::size_t anchorCount,
              std::size_t markerCapacity, std::size_t anchorCapacity,
              std::size_t layoutCapacity) {
  PreparedSketchMarkerMetrics metrics;
  metrics.markerCount = markerCount;
  metrics.anchorCount = anchorCount;
  std::size_t markerBytes = 0U;
  std::size_t anchorBytes = 0U;
  std::size_t layoutBytes = 0U;
  std::size_t retainedPayload = 0U;
  if (!detail::checkedSizeMultiply(
          markerCapacity, sizeof(SketchMarkerRenderRecord), markerBytes) ||
      !detail::checkedSizeMultiply(
          anchorCapacity, sizeof(SketchMarkerAnchorPoint), anchorBytes) ||
      !detail::checkedSizeMultiply(layoutCapacity, sizeof(MarkerLayoutEntry),
                                   layoutBytes) ||
      !detail::checkedSizeAdd(markerBytes, anchorBytes, retainedPayload) ||
      !detail::checkedSizeAdd(sizeof(PreparedSketchMarkers), retainedPayload,
                              metrics.retainedBytes))
    return std::unexpected(
        diagnostic("desktop.sketch.marker-projection-memory-overflow",
                   "sketch marker projection byte accounting overflowed"));
  metrics.scratchBytes = layoutBytes;
  if (!detail::checkedSizeAdd(metrics.retainedBytes, metrics.scratchBytes,
                              metrics.peakBytes))
    return std::unexpected(
        diagnostic("desktop.sketch.marker-projection-memory-overflow",
                   "sketch marker projection peak accounting overflowed"));
  return metrics;
}

[[nodiscard]] std::uint64_t coordinateKey(double value) noexcept {
  return std::bit_cast<std::uint64_t>(value == 0.0 ? 0.0 : value);
}

[[nodiscard]] std::uint8_t
layoutPriority(render::SketchMarkerVisualState state) noexcept {
  switch (state) {
  case render::SketchMarkerVisualState::Conflicting:
    return 0U;
  case render::SketchMarkerVisualState::Redundant:
    return 1U;
  case render::SketchMarkerVisualState::Active:
    return 2U;
  case render::SketchMarkerVisualState::Reference:
    return 3U;
  case render::SketchMarkerVisualState::Suppressed:
    return 4U;
  }
  return 4U;
}

[[nodiscard]] Result<std::size_t>
layoutMarkers(std::vector<SketchMarkerRenderRecord> &markers,
              std::span<const SketchMarkerAnchorPoint> anchors,
              detail::CancellationPoller &cancellation) {
  std::vector<MarkerLayoutEntry> layout;
  if (markers.size() > layout.max_size())
    return std::unexpected(
        diagnostic("desktop.sketch.marker-projection-capacity-limit",
                   "sketch marker layout exceeds vector capacity"));
  layout.reserve(markers.size());
  for (std::size_t index = 0U; index < markers.size(); ++index) {
    cancellation.checkpoint();
    const SketchMarkerRenderRecord &marker = markers[index];
    if (marker.category != render::SketchMarkerCategory::Constraint &&
        marker.category != render::SketchMarkerCategory::Dimension)
      continue;
    const auto markerAnchors =
        anchors.subspan(marker.firstAnchor, marker.anchorCount);
    render::Point2d center;
    for (const SketchMarkerAnchorPoint &anchor : markerAnchors) {
      center.x += anchor.positionMetres.x;
      center.y += anchor.positionMetres.y;
    }
    const double count = static_cast<double>(markerAnchors.size());
    center.x /= count;
    center.y /= count;
    layout.push_back(
        {coordinateKey(center.x), coordinateKey(center.y),
         static_cast<std::uint32_t>(index),
         static_cast<std::uint8_t>(marker.category ==
                                   render::SketchMarkerCategory::Dimension),
         layoutPriority(marker.visual)});
  }
  const std::size_t layoutCapacity = layout.capacity();
  std::ranges::sort(layout, [](const MarkerLayoutEntry &first,
                               const MarkerLayoutEntry &second) {
    return std::tie(first.anchorX, first.anchorY, first.family, first.priority,
                    first.marker) < std::tie(second.anchorX, second.anchorY,
                                             second.family, second.priority,
                                             second.marker);
  });
  constexpr std::array columns{0, 1, -1, 2, -2, 3, -3, 4, -4};
  std::size_t groupStart = 0U;
  while (groupStart < layout.size()) {
    cancellation.checkpoint();
    std::size_t groupEnd = groupStart + 1U;
    while (groupEnd < layout.size() &&
           layout[groupEnd].anchorX == layout[groupStart].anchorX &&
           layout[groupEnd].anchorY == layout[groupStart].anchorY &&
           layout[groupEnd].family == layout[groupStart].family)
      ++groupEnd;
    for (std::size_t position = groupStart; position < groupEnd; ++position) {
      const std::size_t ordinal = position - groupStart;
      SketchMarkerRenderRecord &marker = markers[layout[position].marker];
      if (marker.category == render::SketchMarkerCategory::Constraint) {
        const std::size_t slot = ordinal % (columns.size() * columns.size());
        marker.screenOffsetXLogicalPixels =
            static_cast<float>(columns[slot % columns.size()] * 14);
        marker.screenOffsetYLogicalPixels =
            -static_cast<float>((slot / columns.size() + 1U) * 14U);
      } else if (ordinal != 0U) {
        const float lane = static_cast<float>(ordinal * 18U);
        if (marker.kind == render::SketchMarkerKind::VerticalDistanceDimension)
          marker.screenOffsetXLogicalPixels = lane;
        else
          marker.screenOffsetYLogicalPixels = -lane;
      }
    }
    groupStart = groupEnd;
  }
  cancellation.checkpointNow();
  return layoutCapacity;
}

[[nodiscard]] Result<void>
validateLimits(const PreparedSketchMarkerMetrics &metrics,
               const SketchMarkerProjectionLimits &limits) {
  if (metrics.markerCount > limits.maximumMarkerCount)
    return std::unexpected(
        diagnostic("desktop.sketch.marker-projection-marker-limit",
                   "sketch marker projection exceeded its marker limit"));
  if (metrics.anchorCount > limits.maximumAnchorCount)
    return std::unexpected(
        diagnostic("desktop.sketch.marker-projection-anchor-limit",
                   "sketch marker projection exceeded its anchor limit"));
  if (metrics.retainedBytes > limits.maximumRetainedBytes)
    return std::unexpected(diagnostic(
        "desktop.sketch.marker-projection-retained-limit",
        "sketch marker projection exceeded its retained byte limit"));
  if (metrics.scratchBytes > limits.maximumScratchBytes)
    return std::unexpected(
        diagnostic("desktop.sketch.marker-projection-scratch-limit",
                   "sketch marker projection exceeded its scratch byte limit"));
  if (metrics.peakBytes > limits.maximumPeakBytes)
    return std::unexpected(
        diagnostic("desktop.sketch.marker-projection-peak-limit",
                   "sketch marker projection exceeded its peak byte limit"));
  return {};
}

[[nodiscard]] Diagnostic cancelledPreparation() {
  return diagnostic("desktop.sketch.marker-preparation-cancelled",
                    "sketch marker preparation was cancelled");
}

} // namespace

PreparedSketchMarkers::PreparedSketchMarkers(
    std::shared_ptr<const render::SketchMarkerPacket> source,
    std::shared_ptr<const PreparedSketchScene> base,
    std::vector<SketchMarkerRenderRecord> markers,
    std::vector<SketchMarkerAnchorPoint> anchors,
    PreparedSketchMarkerMetrics metrics, SketchConstraintDisplay display)
    : source_(std::move(source)), base_(std::move(base)),
      markers_(std::move(markers)), anchors_(std::move(anchors)),
      metrics_(metrics), display_(display) {}

std::span<const SketchMarkerAnchorPoint>
PreparedSketchMarkers::markerAnchors(render::SketchMarkerHandle handle) const {
  const auto found = std::ranges::lower_bound(
      markers_, handle, {}, &SketchMarkerRenderRecord::handle);
  if (found == markers_.end() || found->handle != handle ||
      found->anchorCount == 0U ||
      static_cast<std::size_t>(found->firstAnchor) >= anchors_.size() ||
      found->anchorCount > anchors_.size() - found->firstAnchor)
    return {};
  return {anchors_.data() + found->firstAnchor, found->anchorCount};
}

const SketchMarkerRenderRecord *
PreparedSketchMarkers::findMarker(render::SketchMarkerHandle handle) const {
  const auto found = std::ranges::lower_bound(
      markers_, handle, {}, &SketchMarkerRenderRecord::handle);
  return found != markers_.end() && found->handle == handle ? &*found : nullptr;
}

Result<std::shared_ptr<const PreparedSketchMarkers>>
prepareSketchMarkers(std::shared_ptr<const render::SketchMarkerPacket> source,
                     std::shared_ptr<const PreparedSketchScene> base,
                     SketchMarkerProjectionLimits limits,
                     std::shared_ptr<const PreparedSketchMarkers> reuse,
                     std::stop_token cancellationToken,
                     SketchConstraintDisplay display) {
  detail::CancellationPoller cancellation{cancellationToken};
  try {
    cancellation.checkpointNow();
    if (!source || !source->base())
      return std::unexpected(
          diagnostic("desktop.sketch.marker-projection-null-source",
                     "sketch marker preparation requires a source packet"));
    if (!base || !base->scene())
      return std::unexpected(
          diagnostic("desktop.sketch.marker-projection-null-base",
                     "sketch marker preparation requires a prepared base"));
    if (source->base() != base->scene())
      return std::unexpected(
          diagnostic("desktop.sketch.marker-projection-base-mismatch",
                     "sketch marker packet does not retain the exact prepared "
                     "base scene"));

    if (reuse && reuse->source() == source && reuse->base() == base &&
        reuse->display() == display) {
      if (auto bounded = validateLimits(reuse->metrics(), limits); !bounded)
        return std::unexpected(std::move(bounded.error()));
      cancellation.checkpointNow();
      return reuse;
    }

    const auto sourceMarkers = source->markers();
    const auto sourceAnchors = source->anchors();
    std::vector<SketchMarkerRenderRecord> markers;
    std::vector<SketchMarkerAnchorPoint> anchors;
    if (sourceMarkers.size() > markers.max_size() ||
        sourceAnchors.size() > anchors.max_size())
      return std::unexpected(
          diagnostic("desktop.sketch.marker-projection-capacity-limit",
                     "sketch marker projection exceeds vector capacity"));
    auto requested = markerMetrics(sourceMarkers.size(), sourceAnchors.size(),
                                   sourceMarkers.size(), sourceAnchors.size(),
                                   sourceMarkers.size());
    if (!requested)
      return std::unexpected(std::move(requested.error()));
    if (auto bounded = validateLimits(*requested, limits); !bounded)
      return std::unexpected(std::move(bounded.error()));

    markers.reserve(sourceMarkers.size());
    auto markerCapacity = markerMetrics(
        sourceMarkers.size(), sourceAnchors.size(), markers.capacity(),
        sourceAnchors.size(), sourceMarkers.size());
    if (!markerCapacity)
      return std::unexpected(std::move(markerCapacity.error()));
    if (auto bounded = validateLimits(*markerCapacity, limits); !bounded)
      return std::unexpected(std::move(bounded.error()));
    std::size_t nextAnchor = 0U;
    std::uint32_t previousHandle = 0U;
    for (const render::PackedSketchMarker &marker : sourceMarkers) {
      cancellation.checkpoint();
      const auto category = render::markerCategory(marker.kind);
      const bool singleAnchor =
          category &&
          (*category == render::SketchMarkerCategory::DegreeOfFreedom ||
           *category == render::SketchMarkerCategory::SnapCursor ||
           *category == render::SketchMarkerCategory::SplineLabel ||
           marker.kind == render::SketchMarkerKind::SplineControlPole);
      const bool guideSegment =
          marker.kind == render::SketchMarkerKind::SplineControlSegment ||
          marker.kind == render::SketchMarkerKind::SplineCurvatureSegment;
      const bool semantic =
          category && (*category == render::SketchMarkerCategory::Constraint ||
                       *category == render::SketchMarkerCategory::Dimension);
      const bool dimension =
          category && *category == render::SketchMarkerCategory::Dimension;
      const bool label =
          category && *category == render::SketchMarkerCategory::SplineLabel;
      const bool validLabel =
          !label ||
          (marker.kind == render::SketchMarkerKind::SplineDegreeLabel
               ? marker.valueSi >= 1.0 && marker.valueSi <= 25.0 &&
                     marker.valueSi == std::floor(marker.valueSi)
           : marker.kind ==
                   render::SketchMarkerKind::SplineKnotMultiplicityLabel
               ? marker.valueSi >= 1.0 && marker.valueSi <= 26.0 &&
                     marker.valueSi == std::floor(marker.valueSi)
           : marker.kind == render::SketchMarkerKind::SplinePoleWeightLabel
               ? marker.valueSi > 0.0
               : false);
      if (!category || marker.handle.value() <= previousHandle ||
          marker.firstAnchor != nextAnchor ||
          static_cast<std::size_t>(marker.firstAnchor) > sourceAnchors.size() ||
          marker.anchorCount > sourceAnchors.size() - marker.firstAnchor ||
          marker.anchorCount == 0U || marker.anchorCount > 3U ||
          (singleAnchor && marker.anchorCount != 1U) ||
          (guideSegment && marker.anchorCount != 2U) ||
          semantic != marker.constraint.has_value() ||
          !std::isfinite(marker.valueSi) ||
          (!dimension && !label && marker.valueSi != 0.0) || !validLabel)
        return std::unexpected(diagnostic(
            "desktop.sketch.marker-projection-malformed-source",
            "sketch marker source has invalid order, ranges, or values"));
      previousHandle = marker.handle.value();
      nextAnchor += marker.anchorCount;
      markers.push_back({marker.handle, marker.constraint, marker.valueSi,
                         marker.firstAnchor, marker.anchorCount, marker.kind,
                         *category, marker.visual, 0.0F, 0.0F});
    }
    if (nextAnchor != sourceAnchors.size())
      return std::unexpected(
          diagnostic("desktop.sketch.marker-projection-malformed-source",
                     "sketch marker source has unused anchors"));

    anchors.reserve(sourceAnchors.size());
    auto anchorCapacity = markerMetrics(
        sourceMarkers.size(), sourceAnchors.size(), markers.capacity(),
        anchors.capacity(), sourceMarkers.size());
    if (!anchorCapacity)
      return std::unexpected(std::move(anchorCapacity.error()));
    if (auto bounded = validateLimits(*anchorCapacity, limits); !bounded)
      return std::unexpected(std::move(bounded.error()));
    for (const render::SketchMarkerAnchor &anchor : sourceAnchors) {
      cancellation.checkpoint();
      auto position = render::resolveSketchMarkerAnchor(
          anchor, *source->base(), source->provisional().get());
      if (!position)
        return std::unexpected(std::move(position.error()));
      anchors.push_back({*position});
    }
    cancellation.checkpointNow();

    auto layoutCapacity = layoutMarkers(markers, anchors, cancellation);
    if (!layoutCapacity)
      return std::unexpected(std::move(layoutCapacity.error()));

    auto actual =
        markerMetrics(sourceMarkers.size(), sourceAnchors.size(),
                      markers.capacity(), anchors.capacity(), *layoutCapacity);
    if (!actual)
      return std::unexpected(std::move(actual.error()));
    if (auto bounded = validateLimits(*actual, limits); !bounded)
      return std::unexpected(std::move(bounded.error()));
    auto prepared =
        std::shared_ptr<const PreparedSketchMarkers>(new PreparedSketchMarkers{
            std::move(source), std::move(base), std::move(markers),
            std::move(anchors), *actual, display});
    cancellation.checkpointNow();
    return prepared;
  } catch (const detail::SketchProjectionCancelled &) {
    return std::unexpected(cancelledPreparation());
  } catch (const std::bad_alloc &) {
    return std::unexpected(diagnostic(
        "desktop.sketch.marker-projection-allocation",
        "sketch marker preparation could not allocate bounded storage"));
  } catch (const std::length_error &) {
    return std::unexpected(
        diagnostic("desktop.sketch.marker-projection-capacity-limit",
                   "sketch marker projection exceeds vector capacity"));
  }
}

} // namespace kearne::ui
