#include "sketch_provisional_projection.hpp"

#include <QByteArrayView>
#include <QCryptographicHash>

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace kearne::ui {
namespace {

void appendUnsigned(QCryptographicHash &hash, std::uint64_t value) {
  std::array<char, 8> bytes{};
  for (std::size_t index = 0U; index < bytes.size(); ++index)
    bytes[bytes.size() - index - 1U] = static_cast<char>(value >> (index * 8U));
  hash.addData(
      QByteArrayView{bytes.data(), static_cast<qsizetype>(bytes.size())});
}

void appendDouble(QCryptographicHash &hash, double value) {
  appendUnsigned(hash, std::bit_cast<std::uint64_t>(value));
}

Result<render::SketchProvisionalDigest>
payloadDigest(const render::SketchProvisionalBatch &batch) {
  QCryptographicHash hash{QCryptographicHash::Sha256};
  appendUnsigned(hash, batch.primitives.size());
  for (const render::PackedSketchProvisionalPrimitive &primitive :
       batch.primitives) {
    appendUnsigned(hash, primitive.handle.value());
    appendUnsigned(hash, static_cast<std::uint8_t>(primitive.kind));
    appendUnsigned(hash, static_cast<std::uint8_t>(primitive.classification));
    appendUnsigned(hash, primitive.pointCount);
    for (const render::Point2d point : primitive.points) {
      appendDouble(hash, point.x);
      appendDouble(hash, point.y);
    }
    appendDouble(hash, primitive.radius);
    appendDouble(hash, primitive.startAngleRadians);
    appendDouble(hash, primitive.sweepAngleRadians);
    appendDouble(hash, primitive.secondaryRadius);
    appendDouble(hash, primitive.rotationAngleRadians);
    appendUnsigned(hash, primitive.spline);
  }
  appendUnsigned(hash, batch.splineControlPointCoordinates.size());
  for (double value : batch.splineControlPointCoordinates)
    appendDouble(hash, value);
  appendUnsigned(hash, batch.splineKnots.size());
  for (double value : batch.splineKnots)
    appendDouble(hash, value);
  appendUnsigned(hash, batch.splineWeights.size());
  for (double value : batch.splineWeights)
    appendDouble(hash, value);
  appendUnsigned(hash, batch.splines.size());
  for (const render::PackedSketchSpline &spline : batch.splines) {
    appendUnsigned(hash, spline.firstControlPoint);
    appendUnsigned(hash, spline.controlPointCount);
    appendUnsigned(hash, spline.firstKnot);
    appendUnsigned(hash, spline.firstWeight);
    appendUnsigned(hash, spline.degree);
    appendUnsigned(hash, spline.periodic);
  }
  const QByteArray bytes = hash.result();
  render::SketchProvisionalDigest::Bytes payload{};
  if (bytes.size() != static_cast<qsizetype>(payload.size()))
    return std::unexpected(diagnostic(
        "desktop.sketch.provisional-digest",
        "Sketch preview digest has an unexpected size", Severity::Fatal));
  std::copy(bytes.cbegin(), bytes.cend(), payload.begin());
  return render::SketchProvisionalDigest::fromBytes("sha256", payload);
}

Result<render::PackedSketchProvisionalPrimitive>
projectPrimitive(const SketchPrimitiveProjection &source,
                 std::uint32_t handleValue,
                 render::SketchProvisionalBatch &batch) {
  auto handle = render::SketchProvisionalPrimitiveHandle::create(handleValue);
  if (!handle)
    return std::unexpected(std::move(handle.error()));
  render::SketchPrimitiveKind kind = render::SketchPrimitiveKind::Point;
  std::uint8_t pointCount = 0U;
  double radius = 0.0;
  double startAngle = 0.0;
  double sweepAngle = 0.0;
  double secondaryRadius = 0.0;
  double rotationAngle = 0.0;
  std::uint32_t spline = std::numeric_limits<std::uint32_t>::max();
  const auto classification =
      source.construction
          ? render::SketchProvisionalClassification::Construction
          : render::SketchProvisionalClassification::Regular;
  if (source.kind != SketchPrimitiveKind::BSpline &&
      (!source.splineKnots.empty() || !source.splineWeights.empty() ||
       source.splineDegree != 0U || source.splinePeriodic))
    return std::unexpected(diagnostic(
        "desktop.sketch.provisional-unused-bspline",
        "Non-B-spline preview contains B-spline data"));
  switch (source.kind) {
  case SketchPrimitiveKind::Point:
    if (source.points.size() != 1U)
      return std::unexpected(
          diagnostic("desktop.sketch.provisional-point-shape",
                     "Sketch point preview requires one canonical point"));
    kind = render::SketchPrimitiveKind::Point;
    pointCount = 1U;
    break;
  case SketchPrimitiveKind::Line:
    if (source.points.size() != 2U)
      return std::unexpected(
          diagnostic("desktop.sketch.provisional-line-shape",
                     "Sketch line preview requires two canonical points"));
    kind = render::SketchPrimitiveKind::Line;
    pointCount = 2U;
    break;
  case SketchPrimitiveKind::Circle:
    if (source.points.size() != 1U)
      return std::unexpected(
          diagnostic("desktop.sketch.provisional-circle-shape",
                     "Sketch circle preview requires one canonical center"));
    kind = render::SketchPrimitiveKind::Circle;
    pointCount = 1U;
    radius = source.radiusMetres;
    break;
  case SketchPrimitiveKind::Arc:
    if (source.points.size() != 1U)
      return std::unexpected(
          diagnostic("desktop.sketch.provisional-arc-shape",
                     "Sketch arc preview requires one canonical center"));
    kind = render::SketchPrimitiveKind::Arc;
    pointCount = 1U;
    radius = source.radiusMetres;
    startAngle = source.startAngleRadians;
    sweepAngle = source.sweepAngleRadians;
    break;
  case SketchPrimitiveKind::Ellipse:
  case SketchPrimitiveKind::EllipticalArc:
  case SketchPrimitiveKind::HyperbolicArc:
  case SketchPrimitiveKind::ParabolicArc:
    if (source.points.size() != 1U)
      return std::unexpected(
          diagnostic("desktop.sketch.provisional-ellipse-shape",
                     "Sketch ellipse preview requires one canonical center"));
    if (source.kind == SketchPrimitiveKind::Ellipse)
      kind = render::SketchPrimitiveKind::Ellipse;
    else if (source.kind == SketchPrimitiveKind::EllipticalArc)
      kind = render::SketchPrimitiveKind::EllipticalArc;
    else if (source.kind == SketchPrimitiveKind::HyperbolicArc)
      kind = render::SketchPrimitiveKind::HyperbolicArc;
    else
      kind = render::SketchPrimitiveKind::ParabolicArc;
    pointCount = 1U;
    radius = source.radiusMetres;
    secondaryRadius = source.secondaryRadiusMetres;
    rotationAngle = source.rotationAngleRadians;
    if (source.kind != SketchPrimitiveKind::Ellipse) {
      startAngle = source.startAngleRadians;
      sweepAngle = source.sweepAngleRadians;
    }
    break;
  case SketchPrimitiveKind::BSpline:
    if (source.points.size() < 2U || source.splineDegree == 0U ||
        source.splineDegree >= source.points.size() ||
        source.splineWeights.size() != source.points.size() ||
        source.splineKnots.size() !=
            source.points.size() + source.splineDegree + 1U ||
        source.points.size() > std::numeric_limits<std::uint32_t>::max() ||
        batch.splineControlPointCoordinates.size() / 2U >
            std::numeric_limits<std::uint32_t>::max() ||
        batch.splineKnots.size() >
            std::numeric_limits<std::uint32_t>::max() ||
        batch.splineWeights.size() >
            std::numeric_limits<std::uint32_t>::max() ||
        batch.splines.size() >
            static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
      return std::unexpected(diagnostic(
          "desktop.sketch.provisional-bspline-shape",
          "B-spline preview requires exact control points, knots, weights, and degree"));
    kind = render::SketchPrimitiveKind::BSpline;
    spline = static_cast<std::uint32_t>(batch.splines.size());
    batch.splines.push_back(
        {static_cast<std::uint32_t>(
             batch.splineControlPointCoordinates.size() / 2U),
         static_cast<std::uint32_t>(source.points.size()),
         static_cast<std::uint32_t>(batch.splineKnots.size()),
         static_cast<std::uint32_t>(batch.splineWeights.size()),
         source.splineDegree, source.splinePeriodic});
    for (const PlanePoint point : source.points) {
      batch.splineControlPointCoordinates.push_back(point.xMetres);
      batch.splineControlPointCoordinates.push_back(point.yMetres);
    }
    batch.splineKnots.insert(batch.splineKnots.end(),
                             source.splineKnots.begin(),
                             source.splineKnots.end());
    batch.splineWeights.insert(batch.splineWeights.end(),
                               source.splineWeights.begin(),
                               source.splineWeights.end());
    break;
  }
  std::array<render::Point2d, 2> points{};
  for (std::size_t index = 0U; index < pointCount; ++index)
    points[index] = {source.points[index].xMetres,
                     source.points[index].yMetres};
  return render::PackedSketchProvisionalPrimitive{
      *handle,         points,       pointCount, kind,
      classification,  radius,       startAngle, sweepAngle,
      secondaryRadius, rotationAngle, spline};
}

} // namespace

Result<std::shared_ptr<const render::SketchProvisionalGeometry>>
projectSketchProvisional(SketchProvisionalProjectionIdentity identity,
                         std::span<const SketchPrimitiveProjection> primitives,
                         render::SketchProvisionalLimits limits) {
  const std::size_t draftCount =
      std::ranges::count(primitives, true, &SketchPrimitiveProjection::draft);
  if (draftCount == 0U)
    return std::shared_ptr<const render::SketchProvisionalGeometry>{};
  if (draftCount > std::numeric_limits<std::uint32_t>::max())
    return std::unexpected(
        diagnostic("desktop.sketch.provisional-handle-range",
                   "Sketch preview exceeds process-local handle capacity"));

  render::SketchProvisionalBatch projected;
  projected.primitives.reserve(draftCount);
  for (const SketchPrimitiveProjection &primitive : primitives) {
    if (!primitive.draft)
      continue;
    auto value = projectPrimitive(
        primitive,
        static_cast<std::uint32_t>(projected.primitives.size() + 1U),
        projected);
    if (!value)
      return std::unexpected(std::move(value.error()));
    projected.primitives.push_back(std::move(*value));
  }
  auto digest = payloadDigest(projected);
  if (!digest)
    return std::unexpected(std::move(digest.error()));
  return render::SketchProvisionalGeometry::create(
      {{std::move(identity.base), std::move(identity.editSession),
        std::move(identity.toolInstance)},
       std::move(identity.generation),
       std::move(*digest)},
      std::move(projected), limits);
}

} // namespace kearne::ui
