#include "sketch_prepared_products.hpp"

#include "sketch_projection_support.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <new>
#include <numbers>
#include <utility>

namespace kearne::ui {
namespace {

constexpr std::array<render::SketchStyle, 2> provisionalStyles{{
    {render::SketchStyleRole::Preview, render::SketchLinePattern::Solid, 1.5F,
     7.0F, 6U},
    {render::SketchStyleRole::Construction, render::SketchLinePattern::Dashed,
     1.0F, 6.0F, 7U},
}};

constexpr std::array<render::SketchStyle, 4> overlayPointStyles{{
    {render::SketchStyleRole::Hovered, render::SketchLinePattern::Solid, 1.5F,
     9.0F, 20U},
    {render::SketchStyleRole::Selected, render::SketchLinePattern::Solid, 1.5F,
     9.0F, 21U},
    {render::SketchStyleRole::Preview, render::SketchLinePattern::Solid, 1.5F,
     9.0F, 22U},
    {render::SketchStyleRole::Diagnostic, render::SketchLinePattern::Solid,
     1.5F, 9.0F, 23U},
}};

constexpr std::array<render::SketchStyle, 12> markerStyles{{
    {render::SketchStyleRole::Construction, render::SketchLinePattern::Solid,
     1.5F, 12.0F, 30U},
    {render::SketchStyleRole::Preview, render::SketchLinePattern::Solid, 1.5F,
     12.0F, 31U},
    {render::SketchStyleRole::Hovered, render::SketchLinePattern::Solid, 1.5F,
     12.0F, 32U},
    {render::SketchStyleRole::Regular, render::SketchLinePattern::Solid, 1.5F,
     12.0F, 33U},
    {render::SketchStyleRole::Preview, render::SketchLinePattern::Solid, 1.5F,
     12.0F, 34U},
    {render::SketchStyleRole::Construction, render::SketchLinePattern::Dotted,
     1.0F, 7.0F, 35U},
    {render::SketchStyleRole::Construction, render::SketchLinePattern::Solid,
     1.0F, 7.0F, 36U},
    {render::SketchStyleRole::Regular, render::SketchLinePattern::Solid, 1.0F,
     12.0F, 37U},
    {render::SketchStyleRole::Construction, render::SketchLinePattern::Solid,
     1.0F, 12.0F, 38U},
    {render::SketchStyleRole::Construction, render::SketchLinePattern::Dotted,
     1.0F, 12.0F, 39U},
    {render::SketchStyleRole::Hovered, render::SketchLinePattern::Solid, 1.5F,
     12.0F, 40U},
    {render::SketchStyleRole::Diagnostic, render::SketchLinePattern::Solid,
     1.5F, 12.0F, 41U},
}};

[[nodiscard]] std::uint16_t
markerStyle(const SketchMarkerRenderRecord &marker) {
  switch (marker.visual) {
  case render::SketchMarkerVisualState::Active:
    return static_cast<std::uint16_t>(
        static_cast<std::uint8_t>(marker.category) - 1U);
  case render::SketchMarkerVisualState::Reference:
    return 8U;
  case render::SketchMarkerVisualState::Suppressed:
    return 9U;
  case render::SketchMarkerVisualState::Redundant:
    return 10U;
  case render::SketchMarkerVisualState::Conflicting:
    return 11U;
  }
  return 11U;
}

struct OverlayPointContext {
  const PreparedSketchOverlay *overlay = nullptr;
  std::array<std::size_t, 5> offsets{};
};

[[nodiscard]] SketchVectorSourcePrimitive
overlayPointAt(const void *context, std::size_t index) noexcept {
  const auto &source = *static_cast<const OverlayPointContext *>(context);
  std::size_t role = 0U;
  while (role + 1U < source.offsets.size() &&
         index >= source.offsets[role + 1U])
    ++role;
  const auto &point = source.overlay->roleSets()[role]
                          ->pointInstances()[index - source.offsets[role]];
  return {static_cast<std::uint32_t>(index + 1U),
          static_cast<std::uint16_t>(role),
          SketchVectorKind::Point,
          true,
          point.positionMetres,
          point.positionMetres};
}

[[nodiscard]] render::Point2d
markerPosition(const PreparedSketchMarkers &markers,
               std::size_t index) noexcept {
  const SketchMarkerRenderRecord &marker = markers.markers()[index];
  const auto anchors =
      markers.anchors().subspan(marker.firstAnchor, marker.anchorCount);
  render::Point2d position;
  for (const SketchMarkerAnchorPoint &anchor : anchors) {
    position.x += anchor.positionMetres.x;
    position.y += anchor.positionMetres.y;
  }
  const double count = static_cast<double>(anchors.size());
  return {position.x / count, position.y / count};
}

[[nodiscard]] bool isAngleDimension(render::SketchMarkerKind kind) noexcept {
  return kind == render::SketchMarkerKind::AngleDimension;
}

[[nodiscard]] bool markerVisible(const SketchMarkerRenderRecord &marker,
                                 const SketchConstraintDisplay &display) {
  if (marker.category == render::SketchMarkerCategory::Dimension)
    return display.dimensionsVisible &&
           (marker.visual != render::SketchMarkerVisualState::Reference ||
            display.referenceDimensionsVisible);
  return marker.category != render::SketchMarkerCategory::Constraint ||
         display.constraintsVisible;
}

[[nodiscard]] double
displayedDimensionValue(const SketchMarkerRenderRecord &marker,
                        SketchLengthDisplayUnit unit) noexcept {
  if (isAngleDimension(marker.kind))
    return marker.valueSi * 180.0 / std::numbers::pi;
  switch (unit) {
  case SketchLengthDisplayUnit::Millimeter:
    return marker.valueSi * 1000.0;
  case SketchLengthDisplayUnit::Centimeter:
    return marker.valueSi * 100.0;
  case SketchLengthDisplayUnit::Meter:
    return marker.valueSi;
  case SketchLengthDisplayUnit::Inch:
    return marker.valueSi / 0.0254;
  }
  return marker.valueSi;
}

[[nodiscard]] int dimensionDecimals(render::SketchMarkerKind kind,
                                    SketchLengthDisplayUnit unit) noexcept {
  if (isAngleDimension(kind))
    return 2;
  switch (unit) {
  case SketchLengthDisplayUnit::Millimeter:
    return 3;
  case SketchLengthDisplayUnit::Centimeter:
  case SketchLengthDisplayUnit::Inch:
    return 4;
  case SketchLengthDisplayUnit::Meter:
    return 6;
  }
  return 3;
}

[[nodiscard]] bool formatDimension(const SketchMarkerRenderRecord &marker,
                                   SketchLengthDisplayUnit unit,
                                   std::array<std::uint8_t, 32> &destination,
                                   std::uint8_t &length) noexcept {
  std::array<char, 32> buffer{};
  char *cursor = buffer.data();
  char *const end = buffer.data() + buffer.size();
  const bool reference =
      marker.visual == render::SketchMarkerVisualState::Reference;
  if (reference)
    *cursor++ = '(';
  const auto formatted = std::to_chars(
      cursor, end - 2, displayedDimensionValue(marker, unit),
      std::chars_format::fixed, dimensionDecimals(marker.kind, unit));
  if (formatted.ec != std::errc{})
    return false;
  cursor = formatted.ptr;
  while (cursor > buffer.data() && cursor[-1] == '0')
    --cursor;
  if (cursor > buffer.data() && cursor[-1] == '.')
    --cursor;
  if (isAngleDimension(marker.kind))
    *cursor++ = static_cast<char>(176U);
  if (reference)
    *cursor++ = ')';
  length = static_cast<std::uint8_t>(cursor - buffer.data());
  std::transform(buffer.begin(), cursor, destination.begin(), [](char value) {
    return static_cast<std::uint8_t>(static_cast<unsigned char>(value));
  });
  return length != 0U;
}

[[nodiscard]] SketchVectorSourcePrimitive markerAt(const void *context,
                                                   std::size_t index) noexcept {
  const auto &markers = *static_cast<const PreparedSketchMarkers *>(context);
  const SketchMarkerRenderRecord &marker = markers.markers()[index];
  const auto anchors = markers.markerAnchors(marker.handle);
  const bool dimension =
      marker.category == render::SketchMarkerCategory::Dimension;
  const bool visible = markerVisible(marker, markers.display());
  if (marker.kind == render::SketchMarkerKind::SplineControlSegment ||
      marker.kind == render::SketchMarkerKind::SplineCurvatureSegment)
    return {marker.handle.value(),     markerStyle(marker),
            SketchVectorKind::Line,    visible,
            anchors[0].positionMetres, anchors[1].positionMetres};
  if (marker.kind == render::SketchMarkerKind::SplineControlPole)
    return {marker.handle.value(),     markerStyle(marker),
            SketchVectorKind::Point,   visible,
            anchors[0].positionMetres, anchors[0].positionMetres};
  if (marker.category == render::SketchMarkerCategory::SplineLabel) {
    SketchVectorSourcePrimitive result;
    result.sourceKey = marker.handle.value();
    result.style = markerStyle(marker);
    result.kind = SketchVectorKind::Text;
    result.visible = visible;
    result.first = anchors[0].positionMetres;
    result.second = result.first;
    std::array<char, 32> label{};
    char *begin = label.data();
    char *cursor = begin;
    char *end = label.data() + label.size();
    if (marker.kind == render::SketchMarkerKind::SplineKnotMultiplicityLabel)
      *cursor++ = '(';
    else if (marker.kind == render::SketchMarkerKind::SplinePoleWeightLabel)
      *cursor++ = '[';
    const auto formatted =
        marker.kind == render::SketchMarkerKind::SplinePoleWeightLabel
            ? std::to_chars(cursor, end - 1, marker.valueSi,
                            std::chars_format::general, 4)
            : std::to_chars(cursor, end - 1,
                            static_cast<unsigned int>(marker.valueSi));
    if (formatted.ec != std::errc{})
      return result;
    cursor = formatted.ptr;
    if (marker.kind == render::SketchMarkerKind::SplineKnotMultiplicityLabel)
      *cursor++ = ')';
    else if (marker.kind == render::SketchMarkerKind::SplinePoleWeightLabel)
      *cursor++ = ']';
    result.textLength = static_cast<std::uint8_t>(cursor - begin);
    std::transform(begin, cursor, result.text.begin(),
                   [](char value) { return static_cast<std::uint8_t>(value); });
    result.radius = static_cast<double>(result.textLength) * 7.5;
    result.secondaryRadius = 12.0;
    result.screenOffsetYLogicalPixels =
        marker.kind == render::SketchMarkerKind::SplineKnotMultiplicityLabel
            ? 12.0
            : -12.0;
    result.screenOffsetXLogicalPixels =
        marker.kind == render::SketchMarkerKind::SplineDegreeLabel ? 14.0 : 0.0;
    return result;
  }
  if (dimension) {
    SketchVectorSourcePrimitive result;
    result.sourceKey = marker.handle.value();
    result.style = markerStyle(marker);
    result.kind = SketchVectorKind::Dimension;
    result.visible = visible;
    if (marker.kind == render::SketchMarkerKind::AngleDimension) {
      result.first = anchors[2].positionMetres;
      result.second = anchors[0].positionMetres;
      result.third = anchors[1].positionMetres;
    } else {
      result.first = anchors[0].positionMetres;
      result.second = anchors[1].positionMetres;
      result.third = result.first;
    }
    result.glyph = static_cast<std::uint16_t>(marker.kind);
    if (!formatDimension(marker, markers.display().lengthUnit, result.text,
                         result.textLength))
      return result;
    result.radius = static_cast<double>(result.textLength) * 7.5;
    result.secondaryRadius = 12.0;
    result.screenOffsetXLogicalPixels = marker.screenOffsetXLogicalPixels;
    result.screenOffsetYLogicalPixels = marker.screenOffsetYLogicalPixels;
    return result;
  }
  const render::Point2d position = markerPosition(markers, index);
  SketchVectorSourcePrimitive result{marker.handle.value(),
                                     markerStyle(marker),
                                     SketchVectorKind::Glyph,
                                     visible,
                                     position,
                                     position,
                                     0.0,
                                     0.0,
                                     0.0,
                                     static_cast<std::uint16_t>(marker.kind)};
  result.screenOffsetXLogicalPixels = marker.screenOffsetXLogicalPixels;
  result.screenOffsetYLogicalPixels = marker.screenOffsetYLogicalPixels;
  return result;
}

[[nodiscard]] SketchVectorSourceBounds
markerBounds(const PreparedSketchMarkers &markers) noexcept {
  SketchVectorSourceBounds result;
  for (const SketchMarkerAnchorPoint &anchor : markers.anchors()) {
    const render::Point2d point = anchor.positionMetres;
    if (result.empty)
      result = {point, point, false};
    else {
      result.minimum.x = std::min(result.minimum.x, point.x);
      result.minimum.y = std::min(result.minimum.y, point.y);
      result.maximum.x = std::max(result.maximum.x, point.x);
      result.maximum.y = std::max(result.maximum.y, point.y);
    }
  }
  return result;
}

template <typename Position>
[[nodiscard]] SketchVectorSourceBounds
decorationBounds(std::size_t count, Position position) noexcept {
  SketchVectorSourceBounds result;
  for (std::size_t index = 0U; index < count; ++index) {
    const render::Point2d point = position(index);
    if (result.empty)
      result = {point, point, false};
    else {
      result.minimum.x = std::min(result.minimum.x, point.x);
      result.minimum.y = std::min(result.minimum.y, point.y);
      result.maximum.x = std::max(result.maximum.x, point.x);
      result.maximum.y = std::max(result.maximum.y, point.y);
    }
  }
  return result;
}

struct PreparedDecorationPacket {
  std::shared_ptr<const SketchVectorPacket> packet;
  std::shared_ptr<const std::vector<SketchVectorPrimitiveSpanRecord>>
      provenance;
};

[[nodiscard]] Result<PreparedDecorationPacket>
prepareDecorationPacket(const SketchVectorSource &source,
                        SketchVectorUploadOptions options,
                        std::shared_ptr<const SketchVectorPacket> reuse,
                        std::stop_token cancellation) {
  if (source.primitiveCount == 0U)
    return PreparedDecorationPacket{};
  auto built = SketchVectorPacketBuildAccess::build(
      source, options, std::move(reuse), cancellation);
  if (!built)
    return std::unexpected(std::move(built.error()));
  try {
    return PreparedDecorationPacket{
        std::make_shared<const SketchVectorPacket>(std::move(built->packet)),
        std::make_shared<const std::vector<SketchVectorPrimitiveSpanRecord>>(
            std::move(built->provenance))};
  } catch (const std::bad_alloc &) {
    return std::unexpected(diagnostic("desktop.sketch.decoration-memory",
                                      "Sketch decoration ran out of memory"));
  }
}

[[nodiscard]] Diagnostic cancelled() {
  return diagnostic("desktop.sketch.preparation-cancelled",
                    "Sketch product preparation was cancelled");
}

[[nodiscard]] SketchVectorKind sourceKind(render::SketchPrimitiveKind kind) {
  switch (kind) {
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
provisionalAt(const void *context, std::size_t index) noexcept {
  const auto &primitive =
      static_cast<const render::SketchProvisionalGeometry *>(context)
          ->primitives()[index];
  return {primitive.handle.value(),
          primitive.classification ==
                  render::SketchProvisionalClassification::Regular
              ? std::uint16_t{0U}
              : std::uint16_t{1U},
          sourceKind(primitive.kind),
          true,
          primitive.points[0],
          primitive.points[1],
          primitive.radius,
          primitive.startAngleRadians,
          primitive.sweepAngleRadians,
          0U,
          primitive.secondaryRadius,
          primitive.rotationAngleRadians};
}

[[nodiscard]] sketch::NurbsView
provisionalSplineAt(const void *context, std::size_t index) noexcept {
  const auto &source =
      *static_cast<const render::SketchProvisionalGeometry *>(context);
  const render::PackedSketchProvisionalPrimitive primitive =
      source.primitives()[index];
  if (primitive.kind != render::SketchPrimitiveKind::BSpline ||
      primitive.spline >= source.splines().size())
    return {};
  const render::PackedSketchSpline spline = source.splines()[primitive.spline];
  return {source.splineControlPointCoordinates().subspan(
              static_cast<std::size_t>(spline.firstControlPoint) * 2U,
              static_cast<std::size_t>(spline.controlPointCount) * 2U),
          source.splineKnots().subspan(
              spline.firstKnot, spline.controlPointCount + spline.degree + 1U),
          source.splineWeights().subspan(spline.firstWeight,
                                         spline.controlPointCount),
          spline.degree};
}

[[nodiscard]] SketchVectorSourceBounds
provisionalBounds(const render::SketchProvisionalGeometry &source) {
  SketchVectorSourceBounds result;
  for (std::size_t index = 0U; index < source.primitives().size(); ++index) {
    const auto primitive = provisionalAt(&source, index);
    auto current = sketchVectorPrimitiveBounds(
        primitive, primitive.kind == SketchVectorKind::BSpline
                       ? provisionalSplineAt(&source, index)
                       : sketch::NurbsView{});
    if (!current)
      return {};
    if (result.empty)
      result = *current;
    else {
      result.minimum.x = std::min(result.minimum.x, current->minimum.x);
      result.minimum.y = std::min(result.minimum.y, current->minimum.y);
      result.maximum.x = std::max(result.maximum.x, current->maximum.x);
      result.maximum.y = std::max(result.maximum.y, current->maximum.y);
    }
  }
  return result;
}

[[nodiscard]] Result<std::size_t>
sumRetained(std::initializer_list<std::size_t> values) {
  std::size_t result = 0U;
  for (std::size_t value : values)
    if (!detail::checkedSizeAdd(result, value, result))
      return std::unexpected(
          diagnostic("desktop.sketch.products-byte-overflow",
                     "Sketch product byte accounting overflowed"));
  return result;
}

} // namespace

PreparedSketchProvisional::PreparedSketchProvisional(
    std::shared_ptr<const render::SketchProvisionalGeometry> source,
    std::shared_ptr<const SketchVectorPacket> packet,
    std::vector<SketchVectorPrimitiveSpanRecord> provenance,
    PreparedSketchProvisionalMetrics metrics)
    : source_(std::move(source)), packet_(std::move(packet)),
      provenance_(std::move(provenance)), metrics_(metrics) {}

Result<std::shared_ptr<const PreparedSketchProvisional>>
prepareSketchProvisional(
    std::shared_ptr<const render::SketchProvisionalGeometry> source,
    SketchVectorUploadOptions upload,
    std::shared_ptr<const PreparedSketchProvisional> reuse,
    std::stop_token cancellation) {
  if (!source)
    return std::unexpected(diagnostic("desktop.sketch.null-provisional",
                                      "Cannot prepare null Sketch preview"));
  if (cancellation.stop_requested())
    return std::unexpected(cancelled());
  if (reuse && reuse->source() == source)
    return reuse;
  const SketchVectorSource input{
      provisionalStyles,           source.get(),
      source->primitives().size(), provisionalAt,
      provisionalBounds(*source),  provisionalSplineAt};
  auto built = SketchVectorPacketBuildAccess::build(
      input, upload, reuse ? reuse->packet() : nullptr, cancellation);
  if (!built)
    return std::unexpected(std::move(built.error()));
  const std::size_t provenanceBytes =
      built->provenance.capacity() * sizeof(SketchVectorPrimitiveSpanRecord);
  auto retained =
      sumRetained({sizeof(PreparedSketchProvisional),
                   built->packet.metrics().retainedBytes, provenanceBytes});
  if (!retained)
    return std::unexpected(std::move(retained.error()));
  PreparedSketchProvisionalMetrics metrics{
      built->provenance.size(), *retained, built->scratchBytes,
      std::max(*retained, built->peakBytes)};
  try {
    return std::shared_ptr<const PreparedSketchProvisional>(
        new PreparedSketchProvisional{
            std::move(source),
            std::make_shared<const SketchVectorPacket>(
                std::move(built->packet)),
            std::move(built->provenance), metrics});
  } catch (const std::bad_alloc &) {
    return std::unexpected(diagnostic("desktop.sketch.provisional-memory",
                                      "Sketch preview ran out of memory"));
  }
}

PreparedSketchProducts::PreparedSketchProducts(
    std::shared_ptr<const SketchSceneProducts> source,
    std::shared_ptr<const PreparedSketchScene> base,
    std::shared_ptr<const PreparedSketchOverlay> overlay,
    std::shared_ptr<const PreparedSketchProvisional> provisional,
    std::shared_ptr<const PreparedSketchMarkers> markers,
    std::shared_ptr<const SketchVectorPacket> overlayPointPacket,
    std::shared_ptr<const SketchVectorPacket> markerPacket,
    std::shared_ptr<const std::vector<SketchVectorPrimitiveSpanRecord>>
        markerProvenance,
    PreparedSketchProductsMetrics metrics)
    : source_(std::move(source)), base_(std::move(base)),
      overlay_(std::move(overlay)), provisional_(std::move(provisional)),
      markers_(std::move(markers)),
      overlayPointPacket_(std::move(overlayPointPacket)),
      markerPacket_(std::move(markerPacket)),
      markerProvenance_(std::move(markerProvenance)), metrics_(metrics) {}

Result<std::shared_ptr<const PreparedSketchProducts>>
PreparedSketchProducts::create(
    std::shared_ptr<const SketchSceneProducts> source,
    std::shared_ptr<const PreparedSketchScene> base,
    std::shared_ptr<const PreparedSketchOverlay> overlay,
    std::shared_ptr<const PreparedSketchProvisional> provisional,
    std::shared_ptr<const PreparedSketchMarkers> markers) {
  return createPrepared(std::move(source), std::move(base), std::move(overlay),
                        std::move(provisional), std::move(markers), {}, {}, {});
}

Result<std::shared_ptr<const PreparedSketchProducts>>
PreparedSketchProducts::createPrepared(
    std::shared_ptr<const SketchSceneProducts> source,
    std::shared_ptr<const PreparedSketchScene> base,
    std::shared_ptr<const PreparedSketchOverlay> overlay,
    std::shared_ptr<const PreparedSketchProvisional> provisional,
    std::shared_ptr<const PreparedSketchMarkers> markers,
    std::shared_ptr<const SketchVectorPacket> overlayPointPacket,
    std::shared_ptr<const SketchVectorPacket> markerPacket,
    std::shared_ptr<const std::vector<SketchVectorPrimitiveSpanRecord>>
        markerProvenance) {
  if (!source || !base)
    return std::unexpected(
        diagnostic("desktop.sketch.products-null-component",
                   "Sketch products require source and base components"));
  if (auto valid = validateSketchSceneProducts(*source); !valid)
    return std::unexpected(std::move(valid.error()));
  if (base->scene() != source->scene ||
      static_cast<bool>(overlay) != static_cast<bool>(source->overlay) ||
      static_cast<bool>(provisional) !=
          static_cast<bool>(source->provisional) ||
      static_cast<bool>(markers) != static_cast<bool>(source->markers) ||
      (overlay &&
       (overlay->source() != source->overlay || overlay->base() != base)) ||
      (provisional && provisional->source() != source->provisional) ||
      (markers &&
       (markers->source() != source->markers || markers->base() != base)))
    return std::unexpected(diagnostic("desktop.sketch.products-mismatch",
                                      "Prepared Sketch products do not match"));
  const bool needsOverlayPoints =
      overlay && overlay->metrics().pointInstanceCount != 0U;
  const std::size_t visibleMarkerCount =
      markers ? static_cast<std::size_t>(std::ranges::count_if(
                    markers->markers(),
                    [&markers](const auto &marker) {
                      return markerVisible(marker, markers->display());
                    }))
              : 0U;
  const bool needsMarkers = markers && markers->metrics().markerCount != 0U;
  if (static_cast<bool>(overlayPointPacket) != needsOverlayPoints)
    return std::unexpected(
        diagnostic("desktop.sketch.overlay-point-packet-mismatch",
                   "Sketch overlay point packet does not match its source"));
  if (static_cast<bool>(markerPacket) != needsMarkers)
    return std::unexpected(
        diagnostic("desktop.sketch.marker-vector-packet-mismatch",
                   "Sketch marker vector packet does not match its source"));
  if (static_cast<bool>(markerProvenance) != needsMarkers)
    return std::unexpected(diagnostic(
        "desktop.sketch.marker-provenance-presence-mismatch",
        "Sketch marker provenance presence does not match its source"));
  if (markerProvenance && markerProvenance->size() != visibleMarkerCount)
    return std::unexpected(
        diagnostic("desktop.sketch.marker-provenance-count-mismatch",
                   "Sketch marker provenance count does not match its source"));
  PreparedSketchProductsMetrics metrics;
  metrics.baseRetainedBytes = base->metrics().totalRetainedBytes;
  metrics.overlayRetainedBytes =
      overlay ? overlay->metrics().retainedBytes : 0U;
  metrics.overlayPointPacketRetainedBytes =
      overlayPointPacket ? overlayPointPacket->metrics().retainedBytes : 0U;
  metrics.provisionalRetainedBytes =
      provisional ? provisional->metrics().retainedBytes : 0U;
  metrics.markerRetainedBytes = markers ? markers->metrics().retainedBytes : 0U;
  metrics.markerPacketRetainedBytes =
      markerPacket ? markerPacket->metrics().retainedBytes : 0U;
  metrics.markerProvenanceRetainedBytes =
      markerProvenance ? markerProvenance->capacity() *
                             sizeof(SketchVectorPrimitiveSpanRecord)
                       : 0U;
  auto retained = sumRetained(
      {sizeof(PreparedSketchProducts), metrics.baseRetainedBytes,
       metrics.overlayRetainedBytes, metrics.overlayPointPacketRetainedBytes,
       metrics.provisionalRetainedBytes, metrics.markerRetainedBytes,
       metrics.markerPacketRetainedBytes,
       metrics.markerProvenanceRetainedBytes});
  if (!retained)
    return std::unexpected(std::move(retained.error()));
  metrics.totalRetainedBytes = *retained;
  try {
    return std::shared_ptr<const PreparedSketchProducts>(
        new PreparedSketchProducts{
            std::move(source), std::move(base), std::move(overlay),
            std::move(provisional), std::move(markers),
            std::move(overlayPointPacket), std::move(markerPacket),
            std::move(markerProvenance), metrics});
  } catch (const std::bad_alloc &) {
    return std::unexpected(diagnostic("desktop.sketch.products-memory",
                                      "Sketch products ran out of memory"));
  }
}

Result<std::shared_ptr<const PreparedSketchProducts>>
prepareSketchProducts(std::shared_ptr<const SketchSceneProducts> source,
                      SketchProductPreparationOptions options,
                      std::shared_ptr<const PreparedSketchProducts> reuse,
                      std::stop_token cancellation) {
  if (!source)
    return std::unexpected(diagnostic("desktop.sketch.null-products",
                                      "Cannot prepare null Sketch products"));
  if (auto valid = validateSketchSceneProducts(*source); !valid)
    return std::unexpected(std::move(valid.error()));
  if (cancellation.stop_requested())
    return std::unexpected(cancelled());

  std::shared_ptr<const PreparedSketchScene> base;
  if (reuse && reuse->source()->scene == source->scene)
    base = reuse->base();
  else {
    auto prepared =
        prepareSketchScene(source->scene, options.picking, options.upload,
                           reuse ? reuse->base() : nullptr, cancellation);
    if (!prepared)
      return std::unexpected(std::move(prepared.error()));
    base = std::move(*prepared);
  }

  std::shared_ptr<const PreparedSketchOverlay> overlay;
  if (source->overlay) {
    if (reuse && reuse->source()->overlay == source->overlay &&
        reuse->overlay() && reuse->overlay()->base() == base)
      overlay = reuse->overlay();
    else {
      auto prepared = prepareSketchOverlay(
          source->overlay, base, options.overlay,
          reuse ? reuse->overlay() : nullptr, cancellation);
      if (!prepared)
        return std::unexpected(std::move(prepared.error()));
      overlay = std::move(*prepared);
    }
  }

  std::shared_ptr<const SketchVectorPacket> overlayPoints;
  if (overlay && overlay->metrics().pointInstanceCount != 0U) {
    if (reuse && reuse->overlay() == overlay && reuse->overlayPointPacket())
      overlayPoints = reuse->overlayPointPacket();
    else {
      OverlayPointContext context;
      context.overlay = overlay.get();
      for (std::size_t role = 0U; role < overlay->roleSets().size(); ++role)
        context.offsets[role + 1U] =
            context.offsets[role] +
            overlay->roleSets()[role]->pointInstances().size();
      const SketchVectorSource pointSource{
          overlayPointStyles,
          &context,
          context.offsets.back(),
          overlayPointAt,
          decorationBounds(context.offsets.back(),
                           [&](std::size_t index) {
                             return overlayPointAt(&context, index).first;
                           }),
          nullptr};
      auto prepared = prepareDecorationPacket(
          pointSource, options.upload,
          reuse ? reuse->overlayPointPacket() : nullptr, cancellation);
      if (!prepared)
        return std::unexpected(std::move(prepared.error()));
      overlayPoints = std::move(prepared->packet);
    }
  }

  std::shared_ptr<const PreparedSketchProvisional> provisional;
  if (source->provisional) {
    auto prepared = prepareSketchProvisional(
        source->provisional, options.upload,
        reuse ? reuse->provisional() : nullptr, cancellation);
    if (!prepared)
      return std::unexpected(std::move(prepared.error()));
    provisional = std::move(*prepared);
  }

  std::shared_ptr<const PreparedSketchMarkers> markers;
  if (source->markers) {
    if (reuse && reuse->source()->markers == source->markers &&
        reuse->markers() && reuse->markers()->base() == base &&
        reuse->markers()->display() == source->constraintDisplay)
      markers = reuse->markers();
    else {
      auto prepared =
          prepareSketchMarkers(source->markers, base, options.markers,
                               reuse ? reuse->markers() : nullptr, cancellation,
                               source->constraintDisplay);
      if (!prepared)
        return std::unexpected(std::move(prepared.error()));
      markers = std::move(*prepared);
    }
  }

  std::shared_ptr<const SketchVectorPacket> markerPacket;
  std::shared_ptr<const std::vector<SketchVectorPrimitiveSpanRecord>>
      markerProvenance;
  if (markers && markers->metrics().markerCount != 0U) {
    if (reuse && reuse->markers() == markers && reuse->markerPacket() &&
        !reuse->markerProvenance().empty()) {
      markerPacket = reuse->markerPacket();
      markerProvenance = reuse->markerProvenance_;
    } else {
      const SketchVectorSource markerSource{
          markerStyles, markers.get(),          markers->markers().size(),
          markerAt,     markerBounds(*markers), nullptr};
      auto prepared = prepareDecorationPacket(
          markerSource, options.upload, reuse ? reuse->markerPacket() : nullptr,
          cancellation);
      if (!prepared)
        return std::unexpected(std::move(prepared.error()));
      markerPacket = std::move(prepared->packet);
      markerProvenance = std::move(prepared->provenance);
    }
  }
  if (cancellation.stop_requested())
    return std::unexpected(cancelled());
  return PreparedSketchProducts::createPrepared(
      std::move(source), std::move(base), std::move(overlay),
      std::move(provisional), std::move(markers), std::move(overlayPoints),
      std::move(markerPacket), std::move(markerProvenance));
}

} // namespace kearne::ui
