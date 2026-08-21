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

Result<render::SketchProvisionalDigest> payloadDigest(
    std::span<const render::PackedSketchProvisionalPrimitive> primitives) {
  QCryptographicHash hash{QCryptographicHash::Sha256};
  appendUnsigned(hash, primitives.size());
  for (const render::PackedSketchProvisionalPrimitive &primitive : primitives) {
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
                 std::uint32_t handleValue) {
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
  const auto classification =
      source.construction
          ? render::SketchProvisionalClassification::Construction
          : render::SketchProvisionalClassification::Regular;
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
    return std::unexpected(diagnostic(
        "desktop.sketch.provisional-bspline-shape",
        "B-spline previews must be projected as bounded line segments"));
  }
  std::array<render::Point2d, 2> points{};
  for (std::size_t index = 0U; index < source.points.size(); ++index)
    points[index] = {source.points[index].xMetres,
                     source.points[index].yMetres};
  return render::PackedSketchProvisionalPrimitive{
      *handle,         points,       pointCount, kind,
      classification,  radius,       startAngle, sweepAngle,
      secondaryRadius, rotationAngle};
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

  std::vector<render::PackedSketchProvisionalPrimitive> projected;
  projected.reserve(draftCount);
  for (const SketchPrimitiveProjection &primitive : primitives) {
    if (!primitive.draft)
      continue;
    auto value = projectPrimitive(
        primitive, static_cast<std::uint32_t>(projected.size() + 1U));
    if (!value)
      return std::unexpected(std::move(value.error()));
    projected.push_back(std::move(*value));
  }
  auto digest = payloadDigest(projected);
  if (!digest)
    return std::unexpected(std::move(digest.error()));
  return render::SketchProvisionalGeometry::create(
      {{std::move(identity.base), std::move(identity.editSession),
        std::move(identity.toolInstance)},
       std::move(identity.generation),
       std::move(*digest)},
      projected, limits);
}

} // namespace kearne::ui
