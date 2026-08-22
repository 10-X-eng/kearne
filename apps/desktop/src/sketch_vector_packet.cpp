#include "sketch_vector_packet.hpp"
#include "sketch_dash_pattern.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <numbers>
#include <numeric>
#include <ranges>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace kearne::ui {
namespace {

constexpr double fullTurn = 2.0 * std::numbers::pi;

[[nodiscard]] bool finite(render::Point2d point) {
  return std::isfinite(point.x) && std::isfinite(point.y);
}

[[nodiscard]] bool validCoverage(SketchPickCoveragePolicy policy) {
  return policy.generation != 0U &&
         std::isfinite(policy.maximumToleranceLogicalPixels) &&
         policy.maximumToleranceLogicalPixels >= 0.0 &&
         policy.maximumToleranceLogicalPixels <=
             SketchPickCoveragePolicy::
                 maximumConfigurableToleranceLogicalPixels &&
         policy.maximumCurveEvaluations != 0U &&
         policy.maximumCurveEvaluations <=
             SketchPickCoveragePolicy::maximumConfigurableCurveEvaluations &&
         policy.maximumResidentSpanProbes != 0U &&
         policy.maximumResidentSpanProbes <=
             SketchPickCoveragePolicy::maximumConfigurableResidentSpanProbes;
}

[[nodiscard]] std::array<float, 4> splitPoint(render::Point2d point,
                                              render::Point2d origin) {
  const double x = point.x - origin.x;
  const double y = point.y - origin.y;
  const float highX = static_cast<float>(x);
  const float highY = static_cast<float>(y);
  return {highX, highY, static_cast<float>(x - highX),
          static_cast<float>(y - highY)};
}

[[nodiscard]] SketchVectorBounds bounds(SketchVectorSourceBounds source,
                                        double screenRadius = 0.0) {
  return {source.minimum.x, source.minimum.y, source.maximum.x,
          source.maximum.y, source.empty,     screenRadius};
}

void include(SketchVectorBounds &target, const SketchVectorBounds &value) {
  if (value.empty)
    return;
  if (target.empty) {
    target = value;
    return;
  }
  target.minimumX = std::min(target.minimumX, value.minimumX);
  target.minimumY = std::min(target.minimumY, value.minimumY);
  target.maximumX = std::max(target.maximumX, value.maximumX);
  target.maximumY = std::max(target.maximumY, value.maximumY);
  target.maximumScreenRadiusLogicalPixels =
      std::max(target.maximumScreenRadiusLogicalPixels,
               value.maximumScreenRadiusLogicalPixels);
}

[[nodiscard]] bool onSignedSweep(double parameter, double start, double sweep) {
  const auto positive = [](double value) {
    value = std::fmod(value, fullTurn);
    return value < 0.0 ? value + fullTurn : value;
  };
  return sweep >= 0.0 ? positive(parameter - start) <= sweep
                      : positive(start - parameter) <= -sweep;
}

[[nodiscard]] render::Point2d rotatedPoint(render::Point2d center,
                                           double localX, double localY,
                                           double rotation) {
  const double cosine = std::cos(rotation);
  const double sine = std::sin(rotation);
  return {center.x + cosine * localX - sine * localY,
          center.y + sine * localX + cosine * localY};
}

[[nodiscard]] SketchVectorSourceBounds pointBounds(render::Point2d point) {
  return {point, point, false};
}

[[nodiscard]] Result<SketchVectorSourceBounds>
analyticBounds(const SketchVectorSourcePrimitive &primitive) {
  if (!finite(primitive.first) || !finite(primitive.second) ||
      !std::isfinite(primitive.radius) ||
      !std::isfinite(primitive.secondaryRadius) ||
      !std::isfinite(primitive.startAngleRadians) ||
      !std::isfinite(primitive.sweepAngleRadians) ||
      !std::isfinite(primitive.rotationAngleRadians) ||
      !std::isfinite(primitive.screenOffsetXLogicalPixels) ||
      !std::isfinite(primitive.screenOffsetYLogicalPixels))
    return std::unexpected(diagnostic("desktop.sketch.invalid-vector-source",
                                      "Sketch vector data is not finite"));
  switch (primitive.kind) {
  case SketchVectorKind::Point:
  case SketchVectorKind::Glyph:
    return pointBounds(primitive.first);
  case SketchVectorKind::Text:
    if (primitive.textLength == 0U ||
        primitive.textLength > primitive.text.size() ||
        !(primitive.radius > 0.0) || !(primitive.secondaryRadius > 0.0) ||
        !std::ranges::all_of(
            primitive.text.begin(),
            primitive.text.begin() + primitive.textLength,
            [](std::uint8_t value) { return value >= 32U && value <= 126U; }))
      break;
    return pointBounds(primitive.first);
  case SketchVectorKind::Line:
    if (primitive.first == primitive.second)
      return std::unexpected(diagnostic("desktop.sketch.zero-vector-line",
                                        "Sketch line has zero length"));
    return SketchVectorSourceBounds{
        {std::min(primitive.first.x, primitive.second.x),
         std::min(primitive.first.y, primitive.second.y)},
        {std::max(primitive.first.x, primitive.second.x),
         std::max(primitive.first.y, primitive.second.y)},
        false};
  case SketchVectorKind::Circle:
    if (!(primitive.radius > 0.0))
      break;
    return SketchVectorSourceBounds{{primitive.first.x - primitive.radius,
                                     primitive.first.y - primitive.radius},
                                    {primitive.first.x + primitive.radius,
                                     primitive.first.y + primitive.radius},
                                    false};
  case SketchVectorKind::Arc: {
    if (!(primitive.radius > 0.0) || primitive.sweepAngleRadians == 0.0 ||
        std::abs(primitive.sweepAngleRadians) > fullTurn)
      break;
    const auto at = [&](double angle) {
      return render::Point2d{
          primitive.first.x + primitive.radius * std::cos(angle),
          primitive.first.y + primitive.radius * std::sin(angle)};
    };
    SketchVectorSourceBounds result{at(primitive.startAngleRadians),
                                    at(primitive.startAngleRadians), false};
    const auto add = [&](render::Point2d point) {
      result.minimum.x = std::min(result.minimum.x, point.x);
      result.minimum.y = std::min(result.minimum.y, point.y);
      result.maximum.x = std::max(result.maximum.x, point.x);
      result.maximum.y = std::max(result.maximum.y, point.y);
    };
    add(at(primitive.startAngleRadians + primitive.sweepAngleRadians));
    for (double cardinal : {0.0, std::numbers::pi / 2.0, std::numbers::pi,
                            3.0 * std::numbers::pi / 2.0})
      if (onSignedSweep(cardinal, primitive.startAngleRadians,
                        primitive.sweepAngleRadians))
        add(at(cardinal));
    return result;
  }
  case SketchVectorKind::Ellipse: {
    if (!(primitive.radius > 0.0) || !(primitive.secondaryRadius > 0.0))
      break;
    const double cosine = std::cos(primitive.rotationAngleRadians);
    const double sine = std::sin(primitive.rotationAngleRadians);
    const double x =
        std::hypot(primitive.radius * cosine, primitive.secondaryRadius * sine);
    const double y =
        std::hypot(primitive.radius * sine, primitive.secondaryRadius * cosine);
    return SketchVectorSourceBounds{
        {primitive.first.x - x, primitive.first.y - y},
        {primitive.first.x + x, primitive.first.y + y},
        false};
  }
  case SketchVectorKind::EllipticalArc: {
    if (!(primitive.radius > 0.0) || !(primitive.secondaryRadius > 0.0) ||
        primitive.sweepAngleRadians == 0.0 ||
        std::abs(primitive.sweepAngleRadians) > fullTurn)
      break;
    const auto at = [&](double parameter) {
      return rotatedPoint(primitive.first,
                          primitive.radius * std::cos(parameter),
                          primitive.secondaryRadius * std::sin(parameter),
                          primitive.rotationAngleRadians);
    };
    SketchVectorSourceBounds result{at(primitive.startAngleRadians),
                                    at(primitive.startAngleRadians), false};
    const auto add = [&](render::Point2d point) {
      result.minimum.x = std::min(result.minimum.x, point.x);
      result.minimum.y = std::min(result.minimum.y, point.y);
      result.maximum.x = std::max(result.maximum.x, point.x);
      result.maximum.y = std::max(result.maximum.y, point.y);
    };
    add(at(primitive.startAngleRadians + primitive.sweepAngleRadians));
    const double rotation = primitive.rotationAngleRadians;
    const std::array extrema{
        std::atan2(-primitive.secondaryRadius * std::sin(rotation),
                   primitive.radius * std::cos(rotation)),
        std::atan2(-primitive.secondaryRadius * std::sin(rotation),
                   primitive.radius * std::cos(rotation)) +
            std::numbers::pi,
        std::atan2(primitive.secondaryRadius * std::cos(rotation),
                   primitive.radius * std::sin(rotation)),
        std::atan2(primitive.secondaryRadius * std::cos(rotation),
                   primitive.radius * std::sin(rotation)) +
            std::numbers::pi};
    for (double parameter : extrema)
      if (onSignedSweep(parameter, primitive.startAngleRadians,
                        primitive.sweepAngleRadians))
        add(at(parameter));
    return result;
  }
  case SketchVectorKind::HyperbolicArc:
  case SketchVectorKind::ParabolicArc: {
    const bool hyperbolic = primitive.kind == SketchVectorKind::HyperbolicArc;
    if (!(primitive.radius > 0.0) || primitive.sweepAngleRadians == 0.0 ||
        (hyperbolic && !(primitive.secondaryRadius > 0.0)))
      break;
    const double first = primitive.startAngleRadians;
    const double last = first + primitive.sweepAngleRadians;
    const auto at = [&](double parameter) {
      const double x = hyperbolic
                           ? primitive.radius * std::cosh(parameter)
                           : parameter * parameter / (4.0 * primitive.radius);
      const double y = hyperbolic
                           ? primitive.secondaryRadius * std::sinh(parameter)
                           : parameter;
      return rotatedPoint(primitive.first, x, y,
                          primitive.rotationAngleRadians);
    };
    SketchVectorSourceBounds result{at(first), at(first), false};
    const auto add = [&](double parameter) {
      const render::Point2d point = at(parameter);
      result.minimum.x = std::min(result.minimum.x, point.x);
      result.minimum.y = std::min(result.minimum.y, point.y);
      result.maximum.x = std::max(result.maximum.x, point.x);
      result.maximum.y = std::max(result.maximum.y, point.y);
    };
    add(last);
    if ((first <= 0.0 && last >= 0.0) || (last <= 0.0 && first >= 0.0))
      add(0.0);
    return result;
  }
  case SketchVectorKind::BSpline:
    return std::unexpected(
        diagnostic("desktop.sketch.missing-vector-spline",
                   "NURBS bounds require native spline data"));
  }
  return std::unexpected(diagnostic("desktop.sketch.invalid-vector-curve",
                                    "Sketch vector curve is invalid"));
}

[[nodiscard]] std::uint64_t hashBytes(std::uint64_t hash, const void *data,
                                      std::size_t bytes) {
  constexpr std::uint64_t prime = 1099511628211ULL;
  const auto *cursor = static_cast<const std::byte *>(data);
  for (std::size_t index = 0U; index < bytes; ++index) {
    hash ^= std::to_integer<std::uint8_t>(cursor[index]);
    hash *= prime;
  }
  return hash;
}

struct PendingRecord {
  SketchVectorRecord record;
  std::vector<SketchVectorData> data;
  SketchVectorBounds bounds;
  std::uint32_t sourceKey = 0U;
  SketchVectorShaderFamily shaderFamily = SketchVectorShaderFamily::Basic;
  std::uint16_t style = 0U;
  std::uint16_t layer = 0U;
  std::int64_t tileX = 0;
  std::int64_t tileY = 0;
};

[[nodiscard]] SketchVectorRecord
record(const SketchVectorSourcePrimitive &primitive,
       SketchVectorSourceBounds primitiveBounds, render::Point2d origin,
       const render::SketchStyle &style, double firstParameter,
       double lastParameter, double pathStart = 0.0) {
  SketchVectorRecord result;
  result.meta = {static_cast<std::uint32_t>(primitive.kind), 0U, 0U,
                 primitive.sourceKey};
  result.boundsMinimum = splitPoint(primitiveBounds.minimum, origin);
  result.boundsMaximum = splitPoint(primitiveBounds.maximum, origin);
  result.first = splitPoint(primitive.first, origin);
  result.second = splitPoint(primitive.second, origin);
  result.shape = {static_cast<float>(primitive.radius),
                  static_cast<float>(primitive.secondaryRadius),
                  static_cast<float>(
                      primitive.kind == SketchVectorKind::Text
                          ? primitive.screenOffsetXLogicalPixels
                          : primitive.startAngleRadians),
                  static_cast<float>(
                      primitive.kind == SketchVectorKind::Text
                          ? primitive.screenOffsetYLogicalPixels
                          : primitive.sweepAngleRadians)};
  result.domain = {static_cast<float>(primitive.rotationAngleRadians),
                   static_cast<float>(firstParameter),
                   static_cast<float>(lastParameter),
                   static_cast<float>(pathStart)};
  const SketchDashPattern pattern = dashPattern(style);
  result.appearance = {style.strokeWidthPixels, style.pointDiameterPixels,
                       pattern.onLogicalPixels, pattern.periodLogicalPixels};
  if (primitive.kind == SketchVectorKind::Glyph)
    result.meta[2] = primitive.glyph;
  if (primitive.kind == SketchVectorKind::Text)
    result.meta[2] = primitive.textLength;
  return result;
}

[[nodiscard]] double nurbsSpanLength(sketch::NurbsView curve, double first,
                                     double last) {
  // Fixed Gauss-Legendre integration evaluates the native rational curve;
  // it creates no intermediate geometry and keeps dash phase deterministic.
  constexpr std::array abscissa{0.095012509837637440185319335424958063,
                                0.281603550779258913230460501460496106,
                                0.458016777657227386342419442983577574,
                                0.617876244402643748446671764048791019,
                                0.755404408355003033895101194847442268,
                                0.865631202387831743880467897712393132,
                                0.944575023073232576077988415534608345,
                                0.989400934991649932596154173450332628};
  constexpr std::array weight{0.189450610455068496285396723208283105,
                              0.182603415044923588866763667969219939,
                              0.169156519395002538189312079030359962,
                              0.149595988816576732081501730547478549,
                              0.124628971255533872052476282192016420,
                              0.095158511682492784809925107602246226,
                              0.062253523938647892862843836994377694,
                              0.027152459411754094851780572456018104};
  const double midpoint = std::midpoint(first, last);
  const double half = (last - first) * 0.5;
  double sum = 0.0;
  for (std::size_t index = 0U; index < abscissa.size(); ++index) {
    const auto speed = [&](double parameter) {
      const sketch::NurbsPoint derivative =
          sketch::differentiateNurbs(curve, parameter);
      return std::hypot(derivative.x, derivative.y);
    };
    sum += weight[index] * (speed(midpoint - half * abscissa[index]) +
                            speed(midpoint + half * abscissa[index]));
  }
  return half * sum;
}

[[nodiscard]] std::size_t payloadBytes(const PendingRecord &pending) {
  return sizeof(SketchVectorRecord) +
         pending.data.size() * sizeof(SketchVectorData);
}

} // namespace

SketchViewTransform::SketchViewTransform(SketchCamera2d camera,
                                         QSizeF viewportLogical, double cosine,
                                         double sine)
    : camera_(camera), viewportLogical_(viewportLogical), cosine_(cosine),
      sine_(sine) {}

Result<SketchViewTransform>
SketchViewTransform::create(SketchCamera2d camera, QSizeF viewportLogical) {
  if (camera.generation == 0U || !finite(camera.centerMetres) ||
      !std::isfinite(camera.metresPerLogicalPixel) ||
      camera.metresPerLogicalPixel <= 0.0 ||
      !std::isfinite(camera.rotationRadians) ||
      !std::isfinite(viewportLogical.width()) ||
      !std::isfinite(viewportLogical.height()) ||
      viewportLogical.width() <= 0.0 || viewportLogical.height() <= 0.0)
    return std::unexpected(diagnostic("desktop.sketch.invalid-vector-view",
                                      "Sketch vector view is invalid"));
  return SketchViewTransform{camera, viewportLogical,
                             std::cos(camera.rotationRadians),
                             std::sin(camera.rotationRadians)};
}

QPointF SketchViewTransform::toItem(render::Point2d point) const {
  const double x = point.x - camera_.centerMetres.x;
  const double y = point.y - camera_.centerMetres.y;
  return {viewportLogical_.width() * 0.5 +
              (cosine_ * x - sine_ * y) / camera_.metresPerLogicalPixel,
          viewportLogical_.height() * 0.5 -
              (sine_ * x + cosine_ * y) / camera_.metresPerLogicalPixel};
}

render::Point2d SketchViewTransform::toCanonical(QPointF item) const {
  const double x = (item.x() - viewportLogical_.width() * 0.5) *
                   camera_.metresPerLogicalPixel;
  const double y = -(item.y() - viewportLogical_.height() * 0.5) *
                   camera_.metresPerLogicalPixel;
  return {camera_.centerMetres.x + cosine_ * x + sine_ * y,
          camera_.centerMetres.y - sine_ * x + cosine_ * y};
}

Result<QMatrix4x4>
SketchViewTransform::itemMatrix(render::Point2d origin) const {
  auto view = gpuView(origin);
  if (!view)
    return std::unexpected(std::move(view.error()));
  QMatrix4x4 matrix;
  matrix.translate(static_cast<float>(viewportLogical_.width() * 0.5),
                   static_cast<float>(viewportLogical_.height() * 0.5));
  matrix.rotate(
      static_cast<float>(-camera_.rotationRadians * 180.0 / std::numbers::pi),
      0.0F, 0.0F, 1.0F);
  matrix.scale(1.0F / view->metresPerLogicalPixel,
               -1.0F / view->metresPerLogicalPixel);
  matrix.translate(-view->centerOffsetX - view->centerOffsetXLow,
                   -view->centerOffsetY - view->centerOffsetYLow);
  return matrix;
}

Result<SketchGpuView>
SketchViewTransform::gpuView(render::Point2d origin) const {
  const double x = camera_.centerMetres.x - origin.x;
  const double y = camera_.centerMetres.y - origin.y;
  const float highX = static_cast<float>(x);
  const float highY = static_cast<float>(y);
  const float scale = static_cast<float>(camera_.metresPerLogicalPixel);
  if (!std::isfinite(highX) || !std::isfinite(highY) || !std::isfinite(scale) ||
      scale <= 0.0F)
    return std::unexpected(
        diagnostic("desktop.sketch.unrepresentable-vector-view",
                   "Sketch vector view exceeds GPU numeric range"));
  return SketchGpuView{highX,
                       highY,
                       static_cast<float>(x - highX),
                       static_cast<float>(y - highY),
                       scale,
                       static_cast<float>(cosine_),
                       static_cast<float>(sine_)};
}

bool SketchVectorBounds::intersects(const SketchVectorBounds &other,
                                    double expansion) const {
  if (empty || other.empty || !std::isfinite(expansion) || expansion < 0.0)
    return false;
  return maximumX + expansion >= other.minimumX &&
         minimumX - expansion <= other.maximumX &&
         maximumY + expansion >= other.minimumY &&
         minimumY - expansion <= other.maximumY;
}

SketchVectorChunk::SketchVectorChunk(SketchVectorShaderFamily shaderFamily,
                                     std::uint16_t style, std::uint16_t layer,
                                     std::vector<SketchVectorRecord> records,
                                     std::vector<SketchVectorData> data,
                                     SketchVectorBounds bounds,
                                     std::size_t payloadBytes,
                                     std::uint64_t contentHash)
    : shaderFamily_(shaderFamily), style_(style), layer_(layer),
      records_(std::move(records)), data_(std::move(data)), bounds_(bounds),
      payloadBytes_(payloadBytes), contentHash_(contentHash) {}

SketchVectorPacket::SketchVectorPacket(
    render::Point2d origin, std::vector<render::SketchStyle> styles,
    std::vector<std::shared_ptr<const SketchVectorChunk>> chunks,
    std::vector<SpatialNode> spatialIndex, std::uint32_t spatialRoot,
    std::size_t maximumChunkBytes, SketchVectorPacketMetrics metrics,
    std::uint8_t shaderFamilyMask)
    : originMetres_(origin), styles_(std::move(styles)),
      chunks_(std::move(chunks)), spatialIndex_(std::move(spatialIndex)),
      spatialRoot_(spatialRoot), maximumChunkBytes_(maximumChunkBytes),
      metrics_(metrics), shaderFamilyMask_(shaderFamilyMask) {}

Result<std::vector<std::uint32_t>>
SketchVectorPacket::visibleChunks(const SketchViewTransform &transform,
                                  SketchPickCoveragePolicy pickCoverage) const {
  if (!validCoverage(pickCoverage))
    return std::unexpected(
        diagnostic("desktop.sketch.invalid-vector-coverage",
                   "Sketch vector pick coverage is invalid"));
  if (chunks_.empty())
    return std::vector<std::uint32_t>{};
  const QSizeF size = transform.viewportLogical();
  const std::array corners{
      transform.toCanonical({0.0, 0.0}),
      transform.toCanonical({size.width(), 0.0}),
      transform.toCanonical({0.0, size.height()}),
      transform.toCanonical({size.width(), size.height()})};
  SketchVectorBounds viewport{corners[0].x, corners[0].y, corners[0].x,
                              corners[0].y, false,        0.0};
  for (const render::Point2d corner : corners) {
    viewport.minimumX = std::min(viewport.minimumX, corner.x);
    viewport.minimumY = std::min(viewport.minimumY, corner.y);
    viewport.maximumX = std::max(viewport.maximumX, corner.x);
    viewport.maximumY = std::max(viewport.maximumY, corner.y);
  }
  try {
    std::vector<std::uint32_t> result;
    std::vector<std::uint32_t> stack{spatialRoot_};
    while (!stack.empty()) {
      const std::uint32_t index = stack.back();
      stack.pop_back();
      const SpatialNode &node = spatialIndex_[index];
      const double screenExpansion =
          (node.bounds.maximumScreenRadiusLogicalPixels +
           pickCoverage.maximumToleranceLogicalPixels) *
          transform.camera().metresPerLogicalPixel;
      if (!node.bounds.intersects(viewport, screenExpansion))
        continue;
      if (node.leaf)
        result.push_back(node.first);
      else {
        stack.push_back(node.second);
        stack.push_back(node.first);
      }
    }
    std::ranges::sort(result);
    return result;
  } catch (const std::bad_alloc &) {
    return std::unexpected(
        diagnostic("desktop.sketch.vector-visibility-memory",
                   "Sketch vector visibility ran out of memory"));
  }
}

Result<SketchChunkSequence>
SketchChunkSequence::create(const SketchVectorPacket &packet,
                            std::span<const std::uint32_t> chunks) {
  try {
    SketchChunkSequence result;
    result.packet_ = &packet;
    result.membership_.resize((packet.chunks().size() + 63U) / 64U, 0U);
    result.chunks_.reserve(chunks.size());
    for (std::uint32_t chunk : chunks) {
      if (chunk >= packet.chunks().size() || result.contains(chunk))
        return std::unexpected(
            diagnostic("desktop.sketch.invalid-vector-chunk-sequence",
                       "Sketch vector chunk sequence is invalid"));
      result.membership_[chunk / 64U] |= std::uint64_t{1} << (chunk % 64U);
      result.chunks_.push_back(chunk);
    }
    return result;
  } catch (const std::bad_alloc &) {
    return std::unexpected(
        diagnostic("desktop.sketch.vector-chunk-sequence-memory",
                   "Sketch vector chunk sequence ran out of memory"));
  }
}

bool SketchChunkSequence::contains(std::uint32_t chunk) const {
  return chunk / 64U < membership_.size() &&
         (membership_[chunk / 64U] & (std::uint64_t{1} << (chunk % 64U))) != 0U;
}

Result<ProgressiveSketchVisibility> ProgressiveSketchVisibility::create(
    std::shared_ptr<const SketchVectorPacket> packet,
    const SketchViewTransform &transform, SketchPickCoveragePolicy coverage) {
  if (!packet)
    return std::unexpected(diagnostic("desktop.sketch.null-vector-packet",
                                      "Sketch vector packet is missing"));
  auto visible = packet->visibleChunks(transform, coverage);
  if (!visible)
    return std::unexpected(std::move(visible.error()));
  auto sequence = SketchChunkSequence::create(*packet, *visible);
  if (!sequence)
    return std::unexpected(std::move(sequence.error()));
  ProgressiveSketchVisibility result;
  result.packet_ = std::move(packet);
  result.selected_ = std::move(*visible);
  result.sequence_ = std::move(*sequence);
  result.spatialNodesVisited_ = result.selected_.empty() ? 0U : 1U;
  return result;
}

Result<SketchVisibilitySlice>
ProgressiveSketchVisibility::takeNextSlice(std::size_t maximumSpatialNodes,
                                           std::size_t maximumVisibleChunks) {
  if (maximumSpatialNodes == 0U || maximumVisibleChunks == 0U)
    return std::unexpected(diagnostic("desktop.sketch.invalid-visibility-slice",
                                      "Sketch visibility slice is invalid"));
  SketchVisibilitySlice result;
  const std::size_t count =
      std::min(maximumVisibleChunks, selected_.size() - cursor_);
  result.chunks.insert(result.chunks.end(), selected_.begin() + cursor_,
                       selected_.begin() + cursor_ + count);
  result.spatialNodesVisited = count == 0U ? 0U : 1U;
  cursor_ += count;
  return result;
}

Result<std::shared_ptr<const SketchPresentedChunkCoverage>>
SketchPresentedChunkCoverage::create(const SketchVectorPacket &packet,
                                     std::vector<std::uint32_t> chunks,
                                     std::size_t maximumRetainedBytes) {
  auto sequence = SketchChunkSequence::create(packet, chunks);
  if (!sequence)
    return std::unexpected(std::move(sequence.error()));
  return create(packet, *sequence, maximumRetainedBytes);
}

Result<std::shared_ptr<const SketchPresentedChunkCoverage>>
SketchPresentedChunkCoverage::create(const SketchVectorPacket &packet,
                                     SketchChunkSequence &chunks,
                                     std::size_t maximumRetainedBytes) {
  const std::size_t retained =
      sizeof(SketchPresentedChunkCoverage) +
      chunks.membership_.capacity() * sizeof(std::uint64_t);
  if (chunks.packet_ != &packet || retained > maximumRetainedBytes)
    return std::unexpected(diagnostic("desktop.sketch.vector-coverage-limit",
                                      "Sketch vector coverage is invalid"));
  double screenRadius = 0.0;
  for (std::uint32_t chunk : chunks.chunks_)
    screenRadius = std::max(
        screenRadius,
        packet.chunks()[chunk]->bounds().maximumScreenRadiusLogicalPixels);
  try {
    return std::shared_ptr<const SketchPresentedChunkCoverage>(
        new SketchPresentedChunkCoverage{chunks.membership_, chunks.size(),
                                         screenRadius, retained});
  } catch (const std::bad_alloc &) {
    return std::unexpected(
        diagnostic("desktop.sketch.vector-coverage-memory",
                   "Sketch vector coverage ran out of memory"));
  }
}

bool SketchPresentedChunkCoverage::contains(std::uint32_t chunk) const {
  return chunk / 64U < membership_.size() &&
         (membership_[chunk / 64U] & (std::uint64_t{1} << (chunk % 64U))) != 0U;
}

Result<SketchVectorSourceBounds>
sketchVectorPrimitiveBounds(const SketchVectorSourcePrimitive &primitive) {
  return analyticBounds(primitive);
}

Result<SketchVectorSourceBounds>
sketchVectorPrimitiveBounds(const SketchVectorSourcePrimitive &primitive,
                            sketch::NurbsView spline) {
  if (primitive.kind != SketchVectorKind::BSpline)
    return analyticBounds(primitive);
  const std::size_t count = spline.weights.size();
  if (spline.degree == 0U || spline.degree > 25U || count <= spline.degree ||
      spline.controlPointCoordinates.size() != count * 2U ||
      spline.knots.size() != count + spline.degree + 1U ||
      !std::ranges::is_sorted(spline.knots) ||
      !std::ranges::all_of(spline.weights, [](double value) {
        return std::isfinite(value) && value > 0.0;
      }))
    return std::unexpected(diagnostic("desktop.sketch.invalid-vector-spline",
                                      "Sketch NURBS data is invalid"));
  SketchVectorSourceBounds result;
  for (std::size_t index = 0U; index < count; ++index) {
    const render::Point2d point{
        spline.controlPointCoordinates[index * 2U],
        spline.controlPointCoordinates[index * 2U + 1U]};
    if (!finite(point))
      return std::unexpected(diagnostic("desktop.sketch.invalid-vector-spline",
                                        "Sketch NURBS pole is not finite"));
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

Result<SketchVectorPacketBuildOutput> SketchVectorPacketBuildAccess::build(
    const SketchVectorSource &source, SketchVectorUploadOptions options,
    std::shared_ptr<const SketchVectorPacket> reuse,
    std::stop_token cancellation) {
  try {
    if ((source.primitiveCount != 0U &&
         (!source.primitiveAt || !source.primitiveContext)) ||
        (source.primitiveCount == 0U) != source.bounds.empty ||
        options.maximumChunkBytes < sizeof(SketchVectorRecord) ||
        options.maximumChunks == 0U || options.maximumRecords == 0U ||
        options.maximumDataRecords == 0U ||
        !std::isfinite(options.spatialTileMetres) ||
        options.spatialTileMetres <= 0.0 ||
        options.maximumRetainedBytes == 0U ||
        options.maximumScratchBytes == 0U || options.maximumPeakBytes == 0U)
      return std::unexpected(
          diagnostic("desktop.sketch.invalid-vector-options",
                     "Sketch vector preparation is invalid"));
    if (source.styles.empty() && source.primitiveCount != 0U)
      return std::unexpected(diagnostic("desktop.sketch.missing-vector-style",
                                        "Sketch vector data has no styles"));
    const render::Point2d origin =
        source.bounds.empty
            ? render::Point2d{}
            : render::Point2d{std::midpoint(source.bounds.minimum.x,
                                            source.bounds.maximum.x),
                              std::midpoint(source.bounds.minimum.y,
                                            source.bounds.maximum.y)};
    std::vector<PendingRecord> pending;
    pending.reserve(source.primitiveCount);
    std::vector<std::uint32_t> keys;
    keys.reserve(source.primitiveCount);
    SketchVectorPacketMetrics metrics;
    metrics.inputPrimitives = source.primitiveCount;
    for (std::size_t index = 0U; index < source.primitiveCount; ++index) {
      if (cancellation.stop_requested())
        return std::unexpected(
            diagnostic("desktop.sketch.vector-cancelled",
                       "Sketch vector preparation was cancelled"));
      const SketchVectorSourcePrimitive primitive =
          source.primitiveAt(source.primitiveContext, index);
      if (primitive.sourceKey == 0U || primitive.style >= source.styles.size())
        return std::unexpected(
            diagnostic("desktop.sketch.invalid-vector-source",
                       "Sketch vector identity or style is invalid"));
      keys.push_back(primitive.sourceKey);
      const sketch::NurbsView spline =
          primitive.kind == SketchVectorKind::BSpline && source.splineAt
              ? source.splineAt(source.primitiveContext, index)
              : sketch::NurbsView{};
      auto primitiveBounds = sketchVectorPrimitiveBounds(primitive, spline);
      if (!primitiveBounds)
        return std::unexpected(std::move(primitiveBounds.error()));
      if (!primitive.visible)
        continue;
      const render::SketchStyle &style = source.styles[primitive.style];
      if (!std::isfinite(style.strokeWidthPixels) ||
          style.strokeWidthPixels <= 0.0F ||
          !std::isfinite(style.pointDiameterPixels) ||
          style.pointDiameterPixels <= 0.0F)
        return std::unexpected(diagnostic("desktop.sketch.invalid-vector-style",
                                          "Sketch vector style is invalid"));
      const auto append =
          [&](SketchVectorRecord value, std::vector<SketchVectorData> data,
              SketchVectorSourceBounds recordBounds, double screenRadius) {
            if (pending.size() == options.maximumRecords)
              return false;
            const double centerX =
                std::midpoint(recordBounds.minimum.x, recordBounds.maximum.x);
            const double centerY =
                std::midpoint(recordBounds.minimum.y, recordBounds.maximum.y);
            const long double tileX =
                std::floor(centerX / options.spatialTileMetres);
            const long double tileY =
                std::floor(centerY / options.spatialTileMetres);
            if (!std::isfinite(tileX) || !std::isfinite(tileY) ||
                tileX < std::numeric_limits<std::int64_t>::min() ||
                tileX > std::numeric_limits<std::int64_t>::max() ||
                tileY < std::numeric_limits<std::int64_t>::min() ||
                tileY > std::numeric_limits<std::int64_t>::max())
              return false;
            pending.push_back(
                {std::move(value), std::move(data),
                 bounds(recordBounds, screenRadius), primitive.sourceKey,
                 primitive.kind != SketchVectorKind::BSpline
                     ? SketchVectorShaderFamily::Basic
                     : (spline.degree <= 3U
                            ? SketchVectorShaderFamily::NurbsLowDegree
                            : SketchVectorShaderFamily::NurbsGeneral),
                 primitive.style, style.layer, static_cast<std::int64_t>(tileX),
                 static_cast<std::int64_t>(tileY)});
            return true;
          };
      if (primitive.kind == SketchVectorKind::BSpline) {
        const std::size_t count = spline.weights.size();
        double pathStart = 0.0;
        for (std::size_t span = spline.degree; span < count; ++span) {
          const double firstParameter = spline.knots[span];
          const double lastParameter = spline.knots[span + 1U];
          if (!(firstParameter < lastParameter))
            continue;
          SketchVectorSourceBounds spanBounds;
          std::vector<SketchVectorData> data;
          const std::size_t poleCount = spline.degree + 1U;
          data.reserve(poleCount * 2U + spline.degree * 2U + 2U);
          for (std::size_t local = 0U; local < poleCount; ++local) {
            const std::size_t pole = span - spline.degree + local;
            const render::Point2d point{
                spline.controlPointCoordinates[pole * 2U],
                spline.controlPointCoordinates[pole * 2U + 1U]};
            data.push_back({splitPoint(point, origin)});
            if (spanBounds.empty)
              spanBounds = {point, point, false};
            else {
              spanBounds.minimum.x = std::min(spanBounds.minimum.x, point.x);
              spanBounds.minimum.y = std::min(spanBounds.minimum.y, point.y);
              spanBounds.maximum.x = std::max(spanBounds.maximum.x, point.x);
              spanBounds.maximum.y = std::max(spanBounds.maximum.y, point.y);
            }
          }
          for (std::size_t local = 0U; local < poleCount; ++local) {
            const std::size_t pole = span - spline.degree + local;
            data.push_back(
                {{static_cast<float>(spline.weights[pole]), 0.0F, 0.0F, 0.0F}});
          }
          const std::size_t knotCount = spline.degree * 2U + 2U;
          for (std::size_t local = 0U; local < knotCount; ++local)
            data.push_back({{static_cast<float>(
                                 spline.knots[span - spline.degree + local]),
                             0.0F, 0.0F, 0.0F}});
          SketchVectorRecord value =
              record(primitive, spanBounds, origin, style, firstParameter,
                     lastParameter, pathStart);
          value.meta[2] = spline.degree;
          if (!append(std::move(value), std::move(data), spanBounds, 0.0))
            return std::unexpected(
                diagnostic("desktop.sketch.vector-record-limit",
                           "Sketch vector record limit was exceeded"));
          pathStart += nurbsSpanLength(spline, firstParameter, lastParameter);
        }
      } else {
        const double firstParameter =
            primitive.kind == SketchVectorKind::Circle ||
                    primitive.kind == SketchVectorKind::Ellipse
                ? 0.0
                : primitive.startAngleRadians;
        const double lastParameter =
            primitive.kind == SketchVectorKind::Circle ||
                    primitive.kind == SketchVectorKind::Ellipse
                ? fullTurn
                : primitive.startAngleRadians + primitive.sweepAngleRadians;
        double screenRadius = style.strokeWidthPixels * 0.5 + 1.0;
        if (primitive.kind == SketchVectorKind::Text)
          screenRadius =
              std::hypot(primitive.radius * 0.5 +
                             std::abs(primitive.screenOffsetXLogicalPixels),
                         primitive.secondaryRadius * 0.5 +
                             std::abs(primitive.screenOffsetYLogicalPixels)) +
              1.0;
        else if (primitive.kind == SketchVectorKind::Point ||
                 primitive.kind == SketchVectorKind::Glyph)
          screenRadius = style.pointDiameterPixels * 0.5 + 1.0;
        std::vector<SketchVectorData> data;
        if (primitive.kind == SketchVectorKind::Text) {
          data.resize((primitive.textLength + 3U) / 4U);
          for (std::size_t character = 0U;
               character < primitive.textLength; ++character)
            data[character / 4U].value[character % 4U] =
                static_cast<float>(primitive.text[character]);
        }
        if (!append(record(primitive, *primitiveBounds, origin, style,
                           firstParameter, lastParameter),
                    std::move(data), *primitiveBounds, screenRadius))
          return std::unexpected(
              diagnostic("desktop.sketch.vector-record-limit",
                         "Sketch vector record limit was exceeded"));
      }
      ++metrics.visiblePrimitives;
    }
    std::ranges::sort(keys);
    if (std::ranges::adjacent_find(keys) != keys.end())
      return std::unexpected(
          diagnostic("desktop.sketch.duplicate-vector-key",
                     "Sketch vector identities are not unique"));
    std::ranges::sort(
        pending, [](const PendingRecord &left, const PendingRecord &right) {
          if (left.layer != right.layer)
            return left.layer < right.layer;
          if (left.style != right.style)
            return left.style < right.style;
          if (left.shaderFamily != right.shaderFamily)
            return left.shaderFamily < right.shaderFamily;
          if (left.tileY != right.tileY)
            return left.tileY < right.tileY;
          if (left.tileX != right.tileX)
            return left.tileX < right.tileX;
          return left.sourceKey < right.sourceKey;
        });

    std::unordered_multimap<std::uint64_t,
                            std::shared_ptr<const SketchVectorChunk>>
        reusable;
    if (reuse)
      for (const auto &chunk : reuse->chunks())
        reusable.emplace(chunk->contentHash(), chunk);
    std::vector<std::shared_ptr<const SketchVectorChunk>> chunks;
    std::vector<SketchVectorPrimitiveSpanRecord> provenance;
    std::size_t cursor = 0U;
    while (cursor < pending.size()) {
      if (chunks.size() == options.maximumChunks)
        return std::unexpected(
            diagnostic("desktop.sketch.vector-chunk-limit",
                       "Sketch vector chunk limit was exceeded"));
      const SketchVectorShaderFamily shaderFamily =
          pending[cursor].shaderFamily;
      const std::uint16_t style = pending[cursor].style;
      const std::uint16_t layer = pending[cursor].layer;
      const std::int64_t tileX = pending[cursor].tileX;
      const std::int64_t tileY = pending[cursor].tileY;
      std::vector<SketchVectorRecord> records;
      std::vector<SketchVectorData> data;
      SketchVectorBounds chunkBounds;
      std::size_t bytes = 0U;
      std::size_t end = cursor;
      while (end < pending.size() && pending[end].style == style &&
             pending[end].layer == layer &&
             pending[end].shaderFamily == shaderFamily &&
             pending[end].tileX == tileX && pending[end].tileY == tileY) {
        const std::size_t next = payloadBytes(pending[end]);
        if (next > options.maximumChunkBytes)
          return std::unexpected(diagnostic(
              "desktop.sketch.vector-primitive-payload",
              "One Sketch vector primitive exceeds the chunk limit"));
        if (!records.empty() && next > options.maximumChunkBytes - bytes)
          break;
        SketchVectorRecord value = pending[end].record;
        if (data.size() > std::numeric_limits<std::uint32_t>::max())
          return std::unexpected(
              diagnostic("desktop.sketch.vector-data-range",
                         "Sketch vector data offset overflowed"));
        value.meta[1] = static_cast<std::uint32_t>(data.size());
        records.push_back(value);
        data.insert(data.end(), pending[end].data.begin(),
                    pending[end].data.end());
        include(chunkBounds, pending[end].bounds);
        bytes += next;
        ++end;
      }
      std::uint64_t hash = 1469598103934665603ULL;
      hash = hashBytes(hash, &shaderFamily, sizeof(shaderFamily));
      hash = hashBytes(hash, &style, sizeof(style));
      hash = hashBytes(hash, &layer, sizeof(layer));
      hash = hashBytes(hash, records.data(),
                       records.size() * sizeof(SketchVectorRecord));
      hash =
          hashBytes(hash, data.data(), data.size() * sizeof(SketchVectorData));
      std::shared_ptr<const SketchVectorChunk> selected;
      const auto range = reusable.equal_range(hash);
      for (auto candidate = range.first; candidate != range.second;
           ++candidate) {
        const auto &chunk = candidate->second;
        if (chunk && chunk->shaderFamily() == shaderFamily &&
            chunk->style() == style && chunk->layer() == layer &&
            std::ranges::equal(chunk->records(), records) &&
            std::ranges::equal(chunk->data(), data)) {
          selected = chunk;
          reusable.erase(candidate);
          break;
        }
      }
      if (!selected)
        selected =
            std::shared_ptr<const SketchVectorChunk>(new SketchVectorChunk{
                shaderFamily, style, layer, std::move(records), std::move(data),
                chunkBounds, bytes, hash});
      const std::uint32_t chunkIndex =
          static_cast<std::uint32_t>(chunks.size());
      std::size_t local = cursor;
      while (local < end) {
        const std::uint32_t sourceKey = pending[local].sourceKey;
        const std::size_t firstRecord = local - cursor;
        do {
          ++local;
        } while (local < end && pending[local].sourceKey == sourceKey);
        provenance.push_back(
            {sourceKey, chunkIndex, static_cast<std::uint32_t>(firstRecord),
             static_cast<std::uint32_t>(local - cursor - firstRecord)});
      }
      metrics.records += selected->records().size();
      metrics.dataRecords += selected->data().size();
      chunks.push_back(std::move(selected));
      cursor = end;
    }
    std::ranges::sort(provenance, [](const auto &left, const auto &right) {
      if (left.sourceKey != right.sourceKey)
        return left.sourceKey < right.sourceKey;
      if (left.chunk != right.chunk)
        return left.chunk < right.chunk;
      return left.firstRecord < right.firstRecord;
    });

    std::vector<SketchVectorPacket::SpatialNode> spatial;
    std::uint32_t root = 0U;
    if (!chunks.empty()) {
      std::vector<std::uint32_t> order(chunks.size());
      std::iota(order.begin(), order.end(), 0U);
      spatial.reserve(chunks.size() * 2U - 1U);
      const std::function<std::uint32_t(std::size_t, std::size_t)> buildIndex =
          [&](std::size_t begin, std::size_t finish) {
            SketchVectorBounds nodeBounds;
            for (std::size_t index = begin; index < finish; ++index)
              include(nodeBounds, chunks[order[index]]->bounds());
            const std::uint32_t node =
                static_cast<std::uint32_t>(spatial.size());
            spatial.push_back({nodeBounds});
            if (finish - begin == 1U) {
              spatial[node].first = order[begin];
              spatial[node].leaf = true;
              return node;
            }
            const bool splitX = nodeBounds.maximumX - nodeBounds.minimumX >=
                                nodeBounds.maximumY - nodeBounds.minimumY;
            const std::size_t middle = begin + (finish - begin) / 2U;
            std::ranges::nth_element(
                order.begin() + static_cast<std::ptrdiff_t>(begin),
                order.begin() + static_cast<std::ptrdiff_t>(middle),
                order.begin() + static_cast<std::ptrdiff_t>(finish),
                [&](std::uint32_t left, std::uint32_t right) {
                  const auto &a = chunks[left]->bounds();
                  const auto &b = chunks[right]->bounds();
                  return splitX ? std::midpoint(a.minimumX, a.maximumX) <
                                      std::midpoint(b.minimumX, b.maximumX)
                                : std::midpoint(a.minimumY, a.maximumY) <
                                      std::midpoint(b.minimumY, b.maximumY);
                });
            spatial[node].first = buildIndex(begin, middle);
            spatial[node].second = buildIndex(middle, finish);
            return node;
          };
      root = buildIndex(0U, order.size());
    }

    metrics.chunks = chunks.size();
    for (const auto &chunk : chunks)
      metrics.retainedBytes += sizeof(SketchVectorChunk) +
                               chunk->records().size_bytes() +
                               chunk->data().size_bytes();
    metrics.retainedBytes += sizeof(SketchVectorPacket) +
                             source.styles.size_bytes() +
                             chunks.capacity() * sizeof(chunks.front()) +
                             spatial.capacity() * sizeof(spatial.front());
    metrics.scratchBytes = pending.capacity() * sizeof(PendingRecord) +
                           keys.capacity() * sizeof(std::uint32_t);
    metrics.peakBytes = metrics.retainedBytes + metrics.scratchBytes;
    if (metrics.records > options.maximumRecords ||
        metrics.dataRecords > options.maximumDataRecords ||
        metrics.retainedBytes > options.maximumRetainedBytes ||
        metrics.scratchBytes > options.maximumScratchBytes ||
        metrics.peakBytes > options.maximumPeakBytes)
      return std::unexpected(
          diagnostic("desktop.sketch.vector-memory-limit",
                     "Sketch vector packet exceeds its memory limits"));
    std::vector<render::SketchStyle> styles(source.styles.begin(),
                                            source.styles.end());
    const std::size_t retained =
        metrics.retainedBytes +
        provenance.capacity() * sizeof(provenance.front());
    std::uint8_t shaderFamilyMask = 0U;
    for (const auto &chunk : chunks)
      shaderFamilyMask |= static_cast<std::uint8_t>(
          1U << static_cast<std::uint8_t>(chunk->shaderFamily()));
    return SketchVectorPacketBuildOutput{
        SketchVectorPacket{origin, std::move(styles), std::move(chunks),
                           std::move(spatial), root, options.maximumChunkBytes,
                           metrics, shaderFamilyMask},
        std::move(provenance),
        keys.size(),
        retained,
        metrics.scratchBytes,
        metrics.peakBytes};
  } catch (const std::bad_alloc &) {
    return std::unexpected(
        diagnostic("desktop.sketch.vector-memory",
                   "Sketch vector preparation ran out of memory"));
  } catch (const std::length_error &) {
    return std::unexpected(
        diagnostic("desktop.sketch.vector-capacity",
                   "Sketch vector preparation exceeded capacity"));
  }
}

} // namespace kearne::ui
