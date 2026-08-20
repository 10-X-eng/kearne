#include <kearne/render/sketch_scene.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <functional>
#include <limits>
#include <new>
#include <numbers>
#include <numeric>
#include <ranges>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace kearne::render {
namespace {

constexpr double fullTurn = 2.0 * std::numbers::pi;
constexpr std::uint8_t knownPrimitiveFlags =
    static_cast<std::uint8_t>(SketchPrimitiveFlags::Visible) |
    static_cast<std::uint8_t>(SketchPrimitiveFlags::Selectable);

struct PrimitiveHandleHash {
  std::size_t operator()(SketchPrimitiveHandle handle) const {
    return std::hash<std::uint32_t>{}(handle.value());
  }
};

bool finite(Point2d point) {
  return std::isfinite(point.x) && std::isfinite(point.y);
}

std::size_t requiredPointCount(SketchPrimitiveKind kind) {
  switch (kind) {
  case SketchPrimitiveKind::Point:
  case SketchPrimitiveKind::Circle:
  case SketchPrimitiveKind::Arc:
    return 1;
  case SketchPrimitiveKind::Line:
    return 2;
  }
  return 0;
}

Result<void> validateStyle(const SketchStyle &style) {
  const auto role = static_cast<std::uint8_t>(style.role);
  const auto pattern = static_cast<std::uint8_t>(style.linePattern);
  if (role < static_cast<std::uint8_t>(SketchStyleRole::Regular) ||
      role > static_cast<std::uint8_t>(SketchStyleRole::Hovered) ||
      pattern < static_cast<std::uint8_t>(SketchLinePattern::Solid) ||
      pattern > static_cast<std::uint8_t>(SketchLinePattern::Dotted) ||
      !std::isfinite(style.strokeWidthPixels) ||
      style.strokeWidthPixels <= 0.0F ||
      !std::isfinite(style.pointDiameterPixels) ||
      style.pointDiameterPixels <= 0.0F)
    return std::unexpected(
        diagnostic("render.sketch.invalid-style", "sketch style is invalid"));
  return {};
}

Result<void> validateBaseStyle(const SketchStyle &style) {
  if (auto valid = validateStyle(style); !valid)
    return valid;
  if (style.role != SketchStyleRole::Regular &&
      style.role != SketchStyleRole::Construction)
    return std::unexpected(
        diagnostic("render.sketch.base-style-role",
                   "sketch base scene style contains presentation state"));
  return {};
}

Result<void> validatePrimitive(const PackedSketchPrimitive &primitive,
                               std::span<const Point2d> points,
                               std::optional<std::size_t> styleCount) {
  const std::size_t count = requiredPointCount(primitive.kind);
  if (count == 0)
    return std::unexpected(diagnostic("render.sketch.invalid-kind",
                                      "sketch primitive kind is invalid"));
  if (static_cast<std::size_t>(primitive.firstPoint) > points.size() ||
      count > points.size() - primitive.firstPoint)
    return std::unexpected(
        diagnostic("render.sketch.point-range",
                   "sketch primitive point range is invalid"));
  if (styleCount && primitive.style >= *styleCount)
    return std::unexpected(diagnostic("render.sketch.style-range",
                                      "sketch primitive style is missing"));
  if ((static_cast<std::uint8_t>(primitive.flags) & ~knownPrimitiveFlags) != 0)
    return std::unexpected(diagnostic("render.sketch.invalid-flags",
                                      "sketch primitive flags are invalid"));
  for (std::size_t index = 0; index < count; ++index) {
    if (!finite(points[primitive.firstPoint + index]))
      return std::unexpected(
          diagnostic("render.sketch.non-finite-point",
                     "sketch primitive point is not finite"));
  }
  if (primitive.kind == SketchPrimitiveKind::Line &&
      points[primitive.firstPoint] == points[primitive.firstPoint + 1])
    return std::unexpected(diagnostic("render.sketch.degenerate-line",
                                      "zero-length sketch line is invalid"));
  if (!std::isfinite(primitive.radius) ||
      !std::isfinite(primitive.startAngleRadians) ||
      !std::isfinite(primitive.sweepAngleRadians))
    return std::unexpected(diagnostic("render.sketch.non-finite-curve",
                                      "sketch primitive curve is not finite"));
  if (primitive.kind == SketchPrimitiveKind::Circle) {
    if (primitive.radius <= 0.0 || primitive.startAngleRadians != 0.0 ||
        primitive.sweepAngleRadians != 0.0)
      return std::unexpected(
          diagnostic("render.sketch.invalid-circle",
                     "sketch circle parameters are invalid"));
  } else if (primitive.kind == SketchPrimitiveKind::Arc) {
    if (primitive.radius <= 0.0 || primitive.sweepAngleRadians == 0.0 ||
        std::abs(primitive.sweepAngleRadians) > fullTurn)
      return std::unexpected(diagnostic("render.sketch.invalid-arc",
                                        "sketch arc parameters are invalid"));
  } else if (primitive.radius != 0.0 || primitive.startAngleRadians != 0.0 ||
             primitive.sweepAngleRadians != 0.0) {
    return std::unexpected(
        diagnostic("render.sketch.unused-curve-parameters",
                   "non-curve primitive has curve parameters"));
  }
  return {};
}

Result<void> validateBatch(const SketchPrimitiveBatch &batch,
                           std::optional<std::size_t> styleCount) {
  if (batch.points.size() > std::numeric_limits<std::uint32_t>::max())
    return std::unexpected(diagnostic("render.sketch.too-many-points",
                                      "sketch scene has too many points"));
  if (batch.primitives.size() > std::numeric_limits<std::uint32_t>::max())
    return std::unexpected(diagnostic("render.sketch.too-many-primitives",
                                      "sketch scene has too many primitives"));
  std::size_t nextPoint = 0;
  std::unordered_set<SketchPrimitiveHandle, PrimitiveHandleHash> handles;
  std::unordered_set<SketchEntityId, TypedIdHash<SketchEntityIdTag>> entities;
  handles.reserve(batch.primitives.size());
  entities.reserve(batch.primitives.size());
  for (const PackedSketchPrimitive &primitive : batch.primitives) {
    if (primitive.firstPoint != nextPoint)
      return std::unexpected(
          diagnostic("render.sketch.non-packed-points",
                     "sketch primitive points are not packed"));
    if (auto valid = validatePrimitive(primitive, batch.points, styleCount);
        !valid)
      return valid;
    if (!handles.insert(primitive.handle).second)
      return std::unexpected(
          diagnostic("render.sketch.duplicate-handle",
                     "sketch primitive handle is duplicated"));
    if (!entities.insert(primitive.entity).second)
      return std::unexpected(diagnostic("render.sketch.duplicate-entity",
                                        "sketch entity is duplicated"));
    nextPoint += requiredPointCount(primitive.kind);
  }
  if (nextPoint != batch.points.size())
    return std::unexpected(diagnostic("render.sketch.unused-points",
                                      "sketch scene contains unused points"));
  return {};
}

double positiveAngle(double angle) {
  angle = std::fmod(angle, fullTurn);
  return angle < 0.0 ? angle + fullTurn : angle;
}

bool angleOnArc(double angle, double start, double sweep) {
  if (sweep > 0.0)
    return positiveAngle(angle - start) <= sweep;
  return positiveAngle(start - angle) <= -sweep;
}

Point2d radialPoint(Point2d center, double radius, double angle) {
  return {center.x + radius * std::cos(angle),
          center.y + radius * std::sin(angle)};
}

Bounds2d primitiveBounds(std::span<const Point2d> points,
                         const PackedSketchPrimitive &primitive) {
  const Point2d first = points[primitive.firstPoint];
  if (primitive.kind == SketchPrimitiveKind::Point)
    return {first, first, false};
  if (primitive.kind == SketchPrimitiveKind::Line) {
    const Point2d second = points[primitive.firstPoint + 1];
    return {{std::min(first.x, second.x), std::min(first.y, second.y)},
            {std::max(first.x, second.x), std::max(first.y, second.y)},
            false};
  }
  if (primitive.kind == SketchPrimitiveKind::Circle) {
    return {{first.x - primitive.radius, first.y - primitive.radius},
            {first.x + primitive.radius, first.y + primitive.radius},
            false};
  }
  const double endAngle =
      primitive.startAngleRadians + primitive.sweepAngleRadians;
  const Point2d start =
      radialPoint(first, primitive.radius, primitive.startAngleRadians);
  const Point2d end = radialPoint(first, primitive.radius, endAngle);
  Bounds2d bounds{{std::min(start.x, end.x), std::min(start.y, end.y)},
                  {std::max(start.x, end.x), std::max(start.y, end.y)},
                  false};
  for (const double cardinal : {0.0, std::numbers::pi / 2.0, std::numbers::pi,
                                3.0 * std::numbers::pi / 2.0}) {
    if (!angleOnArc(cardinal, primitive.startAngleRadians,
                    primitive.sweepAngleRadians))
      continue;
    const Point2d point = radialPoint(first, primitive.radius, cardinal);
    bounds.minimum.x = std::min(bounds.minimum.x, point.x);
    bounds.minimum.y = std::min(bounds.minimum.y, point.y);
    bounds.maximum.x = std::max(bounds.maximum.x, point.x);
    bounds.maximum.y = std::max(bounds.maximum.y, point.y);
  }
  return bounds;
}

Bounds2d sceneBounds(std::span<const Point2d> points,
                     std::span<const PackedSketchPrimitive> primitives) {
  if (primitives.empty())
    return {};
  Bounds2d result = primitiveBounds(points, primitives.front());
  for (const PackedSketchPrimitive &primitive : primitives.subspan(1)) {
    const Bounds2d bounds = primitiveBounds(points, primitive);
    result.minimum.x = std::min(result.minimum.x, bounds.minimum.x);
    result.minimum.y = std::min(result.minimum.y, bounds.minimum.y);
    result.maximum.x = std::max(result.maximum.x, bounds.maximum.x);
    result.maximum.y = std::max(result.maximum.y, bounds.maximum.y);
  }
  return result;
}

void appendPrimitive(std::vector<Point2d> &points,
                     std::vector<PackedSketchPrimitive> &primitives,
                     const PackedSketchPrimitive &source,
                     std::span<const Point2d> sourcePoints) {
  PackedSketchPrimitive primitive = source;
  primitive.firstPoint = static_cast<std::uint32_t>(points.size());
  const std::size_t count = requiredPointCount(source.kind);
  const auto primitivePoints = sourcePoints.subspan(source.firstPoint, count);
  points.insert(points.end(), primitivePoints.begin(), primitivePoints.end());
  primitives.push_back(std::move(primitive));
}

struct PickAabb {
  double minimumX = 0.0;
  double minimumY = 0.0;
  double maximumX = 0.0;
  double maximumY = 0.0;
};

enum class PickTargetKind : std::uint8_t { Curve = 1, Point = 2 };

struct PickTarget {
  std::uint32_t ordinal = 0;
  sketch::PointKey pointKey = sketch::PointKey::Point;
  PickTargetKind kind = PickTargetKind::Curve;
  std::uint8_t tieRank = 0;
};

struct PickBuildTarget {
  PickAabb bounds;
  double centerX = 0.0;
  double centerY = 0.0;
  PickTarget target;
};

struct PickNode {
  PickAabb bounds;
  std::uint32_t first = 0;
  std::uint32_t count = 0;
  bool leaf = false;
};

constexpr std::size_t pickBranchFactor = 8;
constexpr std::size_t cancellationPollInterval = 256;

bool finite(PickAabb bounds) {
  return std::isfinite(bounds.minimumX) && std::isfinite(bounds.minimumY) &&
         std::isfinite(bounds.maximumX) && std::isfinite(bounds.maximumY) &&
         bounds.minimumX <= bounds.maximumX &&
         bounds.minimumY <= bounds.maximumY;
}

PickAabb pickAabb(Bounds2d bounds) {
  return {bounds.minimum.x, bounds.minimum.y, bounds.maximum.x,
          bounds.maximum.y};
}

PickAabb pointAabb(Point2d point) {
  return {point.x, point.y, point.x, point.y};
}

void include(PickAabb &target, PickAabb value) {
  target.minimumX = std::min(target.minimumX, value.minimumX);
  target.minimumY = std::min(target.minimumY, value.minimumY);
  target.maximumX = std::max(target.maximumX, value.maximumX);
  target.maximumY = std::max(target.maximumY, value.maximumY);
}

bool intersects(PickAabb first, PickAabb second) {
  return first.maximumX >= second.minimumX &&
         first.minimumX <= second.maximumX &&
         first.maximumY >= second.minimumY && first.minimumY <= second.maximumY;
}

bool checkedAdd(std::size_t first, std::size_t second, std::size_t &result) {
  if (second > std::numeric_limits<std::size_t>::max() - first)
    return false;
  result = first + second;
  return true;
}

bool checkedMultiply(std::size_t first, std::size_t second,
                     std::size_t &result) {
  if (first != 0 && second > std::numeric_limits<std::size_t>::max() / first)
    return false;
  result = first * second;
  return true;
}

std::size_t ceilDivide(std::size_t value, std::size_t divisor) {
  return value / divisor + (value % divisor != 0 ? 1U : 0U);
}

std::size_t ceilSquareRoot(std::size_t value) {
  if (value <= 1)
    return value;
  std::size_t root =
      static_cast<std::size_t>(std::sqrt(static_cast<long double>(value)));
  while (root < value / root || (root * root < value))
    ++root;
  while ((root - 1U) >= value / (root - 1U) &&
         (root - 1U) * (root - 1U) >= value)
    --root;
  return root;
}

bool cancelled(std::stop_token token, std::size_t &work) {
  ++work;
  if (work < cancellationPollInterval)
    return false;
  work = 0;
  return token.stop_requested();
}

template <typename Less>
bool mergeSortRange(PickBuildTarget *values, PickBuildTarget *scratch,
                    std::size_t begin, std::size_t end, Less less,
                    std::stop_token cancellation) {
  const std::size_t count = end - begin;
  if (count < 2)
    return !cancellation.stop_requested();
  std::size_t work = 0;
  PickBuildTarget *source = values;
  PickBuildTarget *destination = scratch;
  for (std::size_t width = 1; width < count;) {
    for (std::size_t left = begin; left < end;) {
      const std::size_t middle = std::min(end, left + width);
      const std::size_t right = std::min(end, middle + width);
      std::size_t first = left;
      std::size_t second = middle;
      std::size_t output = left;
      while (first < middle || second < right) {
        if (second == right ||
            (first < middle && !less(source[second], source[first])))
          destination[output++] = source[first++];
        else
          destination[output++] = source[second++];
        if (cancelled(cancellation, work))
          return false;
      }
      left = right;
    }
    std::swap(source, destination);
    if (width > count / 2U)
      break;
    width *= 2U;
  }
  if (source != values) {
    for (std::size_t index = begin; index < end; ++index) {
      values[index] = source[index];
      if (cancelled(cancellation, work))
        return false;
    }
  }
  return !cancellation.stop_requested();
}

double robustPointDistance(Point2d first, Point2d second) {
  const double x = first.x - second.x;
  const double y = first.y - second.y;
  if (std::isfinite(x) && std::isfinite(y))
    return std::hypot(x, y);
  const double scale = std::max({std::abs(first.x), std::abs(first.y),
                                 std::abs(second.x), std::abs(second.y)});
  if (scale == 0.0)
    return 0.0;
  return std::hypot(first.x / scale - second.x / scale,
                    first.y / scale - second.y / scale) *
         scale;
}

struct PickRefinement {
  Point2d closestPoint;
  double distance = 0.0;
};

PickRefinement robustPointRefinement(Point2d query, Point2d point) {
  return {point, robustPointDistance(query, point)};
}

PickRefinement robustLineRefinement(Point2d query, Point2d start, Point2d end) {
  const double x = end.x - start.x;
  const double y = end.y - start.y;
  const double queryX = query.x - start.x;
  const double queryY = query.y - start.y;
  if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(queryX) ||
      !std::isfinite(queryY))
    return {{}, std::numeric_limits<double>::quiet_NaN()};
  const double length = std::hypot(x, y);
  if (!std::isfinite(length))
    return {{}, std::numeric_limits<double>::quiet_NaN()};
  if (length == 0.0)
    return robustPointRefinement(query, start);
  const double scale = std::max({std::abs(queryX), std::abs(queryY), length});
  if (scale == 0.0)
    return {start, 0.0};
  const double unitX = x / length;
  const double unitY = y / length;
  const double normalizedQueryX = queryX / scale;
  const double normalizedQueryY = queryY / scale;
  const double projection = normalizedQueryX * unitX + normalizedQueryY * unitY;
  const double normalizedLength = length / scale;
  if (projection <= 0.0)
    return robustPointRefinement(query, start);
  if (projection >= normalizedLength)
    return robustPointRefinement(query, end);
  const double amount = projection / normalizedLength;
  const Point2d closest{std::lerp(start.x, end.x, amount),
                        std::lerp(start.y, end.y, amount)};
  return {closest, std::hypot(normalizedQueryX - projection * unitX,
                              normalizedQueryY - projection * unitY) *
                       scale};
}

PickRefinement robustCurveRefinement(const SketchSceneSnapshot &scene,
                                     const PackedSketchPrimitive &primitive,
                                     Point2d query) {
  const Point2d first = scene.points()[primitive.firstPoint];
  if (primitive.kind == SketchPrimitiveKind::Line)
    return robustLineRefinement(query, first,
                                scene.points()[primitive.firstPoint + 1]);
  const double radialDistance = robustPointDistance(query, first);
  double x = query.x - first.x;
  double y = query.y - first.y;
  if (!std::isfinite(x) || !std::isfinite(y)) {
    const double scale = std::max({std::abs(query.x), std::abs(query.y),
                                   std::abs(first.x), std::abs(first.y)});
    x = query.x / scale - first.x / scale;
    y = query.y / scale - first.y / scale;
  }
  const double angle = radialDistance == 0.0 ? 0.0 : std::atan2(y, x);
  if (primitive.kind == SketchPrimitiveKind::Circle)
    return {radialPoint(first, primitive.radius, angle),
            std::abs(radialDistance - primitive.radius)};
  if (primitive.kind == SketchPrimitiveKind::Arc) {
    if (angleOnArc(angle, primitive.startAngleRadians,
                   primitive.sweepAngleRadians))
      return {radialPoint(first, primitive.radius, angle),
              std::abs(radialDistance - primitive.radius)};
    const Point2d start =
        radialPoint(first, primitive.radius, primitive.startAngleRadians);
    const Point2d end =
        radialPoint(first, primitive.radius,
                    primitive.startAngleRadians + primitive.sweepAngleRadians);
    const double startDistance = robustPointDistance(query, start);
    const double endDistance = robustPointDistance(query, end);
    return startDistance <= endDistance ? PickRefinement{start, startDistance}
                                        : PickRefinement{end, endDistance};
  }
  return {{}, std::numeric_limits<double>::quiet_NaN()};
}

std::array<sketch::PointKey, 3>
pointKeys(const PackedSketchPrimitive &primitive, std::size_t &count) {
  using Key = sketch::PointKey;
  if (primitive.kind == SketchPrimitiveKind::Point) {
    count = 1;
    return {Key::Point, Key::Point, Key::Point};
  }
  if (primitive.kind == SketchPrimitiveKind::Line) {
    count = 2;
    return {Key::Start, Key::End, Key::End};
  }
  if (primitive.kind == SketchPrimitiveKind::Circle) {
    count = 1;
    return {Key::Center, Key::Center, Key::Center};
  }
  count = 3;
  return {Key::Center, Key::Start, Key::End};
}

std::optional<std::size_t> overlayRoleIndex(SketchOverlayRole role) {
  const auto value = static_cast<std::uint8_t>(role);
  if (value < static_cast<std::uint8_t>(SketchOverlayRole::Hovered) ||
      value > static_cast<std::uint8_t>(SketchOverlayRole::Diagnostic))
    return std::nullopt;
  return static_cast<std::size_t>(value - 1U);
}

SketchStyleRole styleRole(SketchOverlayRole role) {
  switch (role) {
  case SketchOverlayRole::Hovered:
    return SketchStyleRole::Hovered;
  case SketchOverlayRole::Selected:
    return SketchStyleRole::Selected;
  case SketchOverlayRole::Preview:
    return SketchStyleRole::Preview;
  case SketchOverlayRole::Diagnostic:
    return SketchStyleRole::Diagnostic;
  }
  return SketchStyleRole::Regular;
}

class OverlayHash final {
public:
  void byte(std::uint8_t value) {
    for (std::size_t lane = 0; lane < state_.size(); ++lane) {
      state_[lane] ^= static_cast<std::uint64_t>(value) +
                      0x9e3779b97f4a7c15ULL +
                      (static_cast<std::uint64_t>(lane) << 8U);
      state_[lane] *= primes_[lane];
      state_[lane] ^= state_[lane] >> 29U;
    }
  }

  void unsignedInteger(std::uint64_t value) {
    for (unsigned shift = 0U; shift < 64U; shift += 8U)
      byte(static_cast<std::uint8_t>(value >> shift));
  }

  template <typename Digest> Digest finish() const {
    Digest result;
    for (std::size_t lane = 0; lane < state_.size(); ++lane) {
      std::uint64_t value = state_[lane];
      value ^= value >> 30U;
      value *= 0xbf58476d1ce4e5b9ULL;
      value ^= value >> 27U;
      value *= 0x94d049bb133111ebULL;
      value ^= value >> 31U;
      for (std::size_t byteIndex = 0; byteIndex < 8U; ++byteIndex)
        result.bytes[lane * 8U + byteIndex] =
            static_cast<std::uint8_t>(value >> (byteIndex * 8U));
    }
    return result;
  }

private:
  std::array<std::uint64_t, 4> state_{
      0x243f6a8885a308d3ULL, 0x13198a2e03707344ULL, 0xa4093822299f31d0ULL,
      0x082efa98ec4e6c89ULL};
  static constexpr std::array<std::uint64_t, 4> primes_{
      0x100000001b3ULL, 0x9e3779b185ebca87ULL, 0xc2b2ae3d27d4eb4fULL,
      0x165667b19e3779f9ULL};
};

Result<SketchOverlayRoleSetDigest>
hashOverlayRoleSet(SketchOverlayRole role,
                   std::span<const SketchOverlayScope> scopes,
                   std::stop_token cancellation) {
  OverlayHash hash;
  hash.byte(static_cast<std::uint8_t>(role));
  hash.unsignedInteger(scopes.size());
  std::size_t work = 0U;
  for (const SketchOverlayScope &scope : scopes) {
    for (const std::uint8_t byte : scope.entity.bytes())
      hash.byte(byte);
    hash.byte(scope.point ? static_cast<std::uint8_t>(*scope.point) : 0U);
    if (cancelled(cancellation, work))
      return std::unexpected(
          diagnostic("render.sketch.overlay-cancelled",
                     "sketch overlay role-set construction was cancelled"));
  }
  if (cancellation.stop_requested())
    return std::unexpected(
        diagnostic("render.sketch.overlay-cancelled",
                   "sketch overlay role-set construction was cancelled"));
  return hash.finish<SketchOverlayRoleSetDigest>();
}

SketchOverlayDigest
hashOverlay(std::span<const SketchOverlayRoleSetPtr, 4> roleSets) {
  OverlayHash hash;
  for (const auto &set : roleSets) {
    hash.byte(static_cast<std::uint8_t>(set->role()));
    for (const std::uint8_t byte : set->digest().bytes)
      hash.byte(byte);
  }
  return hash.finish<SketchOverlayDigest>();
}

struct OverlayRoleSetBytes {
  std::size_t input = 0U;
  std::size_t retained = 0U;
  std::size_t scratch = 0U;
  std::size_t peak = 0U;
};

Result<OverlayRoleSetBytes>
overlayRoleSetBytes(std::size_t inputCount, std::size_t retainedCapacity,
                    std::size_t workingCapacity,
                    std::size_t sortScratchCapacity) {
  OverlayRoleSetBytes result;
  std::size_t retainedPayload = 0U;
  std::size_t working = 0U;
  std::size_t sorting = 0U;
  if (!checkedMultiply(inputCount, sizeof(SketchOverlayScope), result.input) ||
      !checkedMultiply(retainedCapacity, sizeof(SketchOverlayScope),
                       retainedPayload) ||
      !checkedAdd(sizeof(SketchOverlayRoleSet), retainedPayload,
                  result.retained) ||
      !checkedMultiply(workingCapacity, sizeof(SketchOverlayScope), working) ||
      !checkedMultiply(sortScratchCapacity, sizeof(SketchOverlayScope),
                       sorting) ||
      !checkedAdd(working, sorting, result.scratch) ||
      !checkedAdd(result.retained, result.scratch, result.peak))
    return std::unexpected(
        diagnostic("render.sketch.overlay-byte-overflow",
                   "sketch overlay role-set byte count overflowed"));
  return result;
}

Result<void>
validateOverlayRoleSetLimits(std::size_t scopeCount,
                             const OverlayRoleSetBytes &bytes,
                             const SketchOverlayRoleSetLimits &limits) {
  if (scopeCount > limits.maximumScopeCount)
    return std::unexpected(
        diagnostic("render.sketch.overlay-count-limit",
                   "sketch overlay role-set scope count exceeds its limit"));
  if (bytes.input > limits.maximumInputBytes)
    return std::unexpected(
        diagnostic("render.sketch.overlay-input-limit",
                   "sketch overlay role-set input exceeds its byte limit"));
  if (bytes.retained > limits.maximumRetainedBytes)
    return std::unexpected(diagnostic(
        "render.sketch.overlay-memory-limit",
        "sketch overlay role-set exceeds its retained memory limit"));
  if (bytes.scratch > limits.maximumScratchBytes)
    return std::unexpected(
        diagnostic("render.sketch.overlay-scratch-limit",
                   "sketch overlay role-set exceeds its scratch memory limit"));
  if (bytes.peak > limits.maximumPeakBuildBytes)
    return std::unexpected(diagnostic(
        "render.sketch.overlay-peak-build-limit",
        "sketch overlay role-set exceeds its peak build memory limit"));
  return {};
}

bool validProvisionalClassification(SketchProvisionalClassification value) {
  return value == SketchProvisionalClassification::Regular ||
         value == SketchProvisionalClassification::Construction;
}

Result<void> validateProvisionalPrimitive(
    const PackedSketchProvisionalPrimitive &primitive) {
  const std::size_t count = requiredPointCount(primitive.kind);
  if (count == 0U)
    return std::unexpected(
        diagnostic("render.sketch.provisional-invalid-kind",
                   "provisional sketch primitive kind is invalid"));
  if (primitive.pointCount != count)
    return std::unexpected(
        diagnostic("render.sketch.provisional-point-arity",
                   "provisional sketch primitive point arity is invalid"));
  if (!validProvisionalClassification(primitive.classification))
    return std::unexpected(
        diagnostic("render.sketch.provisional-classification",
                   "provisional sketch classification is invalid"));
  for (std::size_t index = 0; index < count; ++index) {
    if (!finite(primitive.points[index]))
      return std::unexpected(
          diagnostic("render.sketch.provisional-non-finite-point",
                     "provisional sketch point is not finite"));
  }
  for (std::size_t index = count; index < primitive.points.size(); ++index) {
    if (primitive.points[index] != Point2d{})
      return std::unexpected(
          diagnostic("render.sketch.provisional-unused-point",
                     "provisional sketch primitive has nonzero unused points"));
  }
  if (primitive.kind == SketchPrimitiveKind::Line &&
      primitive.points[0] == primitive.points[1])
    return std::unexpected(
        diagnostic("render.sketch.provisional-degenerate-line",
                   "zero-length provisional sketch line is invalid"));
  if (!std::isfinite(primitive.radius) ||
      !std::isfinite(primitive.startAngleRadians) ||
      !std::isfinite(primitive.sweepAngleRadians))
    return std::unexpected(
        diagnostic("render.sketch.provisional-non-finite-curve",
                   "provisional sketch curve is not finite"));
  if (primitive.kind == SketchPrimitiveKind::Circle) {
    if (primitive.radius <= 0.0 || primitive.startAngleRadians != 0.0 ||
        primitive.sweepAngleRadians != 0.0)
      return std::unexpected(
          diagnostic("render.sketch.provisional-invalid-circle",
                     "provisional sketch circle parameters are invalid"));
  } else if (primitive.kind == SketchPrimitiveKind::Arc) {
    if (primitive.radius <= 0.0 || primitive.sweepAngleRadians == 0.0 ||
        std::abs(primitive.sweepAngleRadians) > fullTurn)
      return std::unexpected(
          diagnostic("render.sketch.provisional-invalid-arc",
                     "provisional sketch arc parameters are invalid"));
  } else if (primitive.radius != 0.0 || primitive.startAngleRadians != 0.0 ||
             primitive.sweepAngleRadians != 0.0) {
    return std::unexpected(
        diagnostic("render.sketch.provisional-unused-curve-parameters",
                   "provisional non-curve has curve parameters"));
  }
  if ((primitive.kind == SketchPrimitiveKind::Circle ||
       primitive.kind == SketchPrimitiveKind::Arc) &&
      (!finite(Point2d{primitive.points[0].x + primitive.radius,
                       primitive.points[0].y + primitive.radius}) ||
       !finite(Point2d{primitive.points[0].x - primitive.radius,
                       primitive.points[0].y - primitive.radius})))
    return std::unexpected(
        diagnostic("render.sketch.provisional-unrepresentable-curve",
                   "provisional sketch curve bounds are not finite"));
  return {};
}

struct ProvisionalByteCounts {
  std::size_t input = 0;
  std::size_t retained = 0;
  std::size_t scratch = 0;
  std::size_t peak = 0;
};

Result<ProvisionalByteCounts>
provisionalByteCounts(std::size_t inputCount, std::size_t retainedCapacity,
                      std::size_t scratchCapacity) {
  ProvisionalByteCounts result;
  if (!checkedMultiply(inputCount, sizeof(PackedSketchProvisionalPrimitive),
                       result.input))
    return std::unexpected(
        diagnostic("render.sketch.provisional-input-limit",
                   "provisional sketch input byte count overflowed"));
  std::size_t retainedPayload = 0;
  if (!checkedMultiply(retainedCapacity,
                       sizeof(PackedSketchProvisionalPrimitive),
                       retainedPayload) ||
      !checkedAdd(sizeof(SketchProvisionalGeometry), retainedPayload,
                  result.retained))
    return std::unexpected(
        diagnostic("render.sketch.provisional-memory-limit",
                   "provisional sketch retained byte count overflowed"));
  if (!checkedMultiply(scratchCapacity,
                       sizeof(PackedSketchProvisionalPrimitive),
                       result.scratch))
    return std::unexpected(
        diagnostic("render.sketch.provisional-scratch-limit",
                   "provisional sketch scratch byte count overflowed"));
  if (!checkedAdd(result.retained, result.scratch, result.peak))
    return std::unexpected(
        diagnostic("render.sketch.provisional-peak-build-limit",
                   "provisional sketch peak byte count overflowed"));
  return result;
}

Result<void> validateProvisionalLimits(const ProvisionalByteCounts &bytes,
                                       const SketchProvisionalLimits &limits) {
  if (bytes.input > limits.maximumInputBytes)
    return std::unexpected(
        diagnostic("render.sketch.provisional-input-limit",
                   "provisional sketch input exceeds its byte limit"));
  if (bytes.retained > limits.maximumRetainedBytes)
    return std::unexpected(
        diagnostic("render.sketch.provisional-memory-limit",
                   "provisional sketch exceeds its retained memory limit"));
  if (bytes.scratch > limits.maximumScratchBytes)
    return std::unexpected(
        diagnostic("render.sketch.provisional-scratch-limit",
                   "provisional sketch exceeds its scratch memory limit"));
  if (bytes.peak > limits.maximumPeakBuildBytes)
    return std::unexpected(
        diagnostic("render.sketch.provisional-peak-build-limit",
                   "provisional sketch exceeds its peak build memory limit"));
  return {};
}

template <typename Value, typename Less>
bool mergeSortVector(std::vector<Value> &values, std::vector<Value> &scratch,
                     Less less, std::stop_token cancellation) {
  if (values.size() < 2U)
    return !cancellation.stop_requested();
  auto *source = &values;
  auto *destination = &scratch;
  std::size_t work = 0;
  for (std::size_t width = 1U; width < values.size();) {
    for (std::size_t left = 0; left < values.size();) {
      const std::size_t middle = std::min(values.size(), left + width);
      const std::size_t right = std::min(values.size(), middle + width);
      std::size_t first = left;
      std::size_t second = middle;
      std::size_t output = left;
      while (first < middle || second < right) {
        if (second == right ||
            (first < middle && !less((*source)[second], (*source)[first])))
          (*destination)[output++] = (*source)[first++];
        else
          (*destination)[output++] = (*source)[second++];
        if (cancelled(cancellation, work))
          return false;
      }
      left = right;
    }
    std::swap(source, destination);
    if (width > values.size() / 2U)
      break;
    width *= 2U;
  }
  if (source != &values)
    values.swap(scratch);
  return !cancellation.stop_requested();
}

bool sortProvisionalPrimitives(
    std::vector<PackedSketchProvisionalPrimitive> &values,
    std::vector<PackedSketchProvisionalPrimitive> &scratch,
    std::stop_token cancellation) {
  return mergeSortVector(
      values, scratch,
      [](const auto &first, const auto &second) {
        return first.handle < second.handle;
      },
      cancellation);
}

struct MarkerPrimitiveGeometry {
  SketchPrimitiveKind kind;
  std::array<Point2d, 2> points;
  double radius;
  double startAngleRadians;
  double sweepAngleRadians;
};

std::optional<Point2d>
markerSemanticPoint(const MarkerPrimitiveGeometry &primitive,
                    sketch::PointKey point) {
  switch (primitive.kind) {
  case SketchPrimitiveKind::Point:
    if (point == sketch::PointKey::Point)
      return primitive.points[0];
    break;
  case SketchPrimitiveKind::Line:
    if (point == sketch::PointKey::Start)
      return primitive.points[0];
    if (point == sketch::PointKey::End)
      return primitive.points[1];
    break;
  case SketchPrimitiveKind::Circle:
    if (point == sketch::PointKey::Center)
      return primitive.points[0];
    break;
  case SketchPrimitiveKind::Arc:
    if (point == sketch::PointKey::Center)
      return primitive.points[0];
    if (point == sketch::PointKey::Start)
      return radialPoint(primitive.points[0], primitive.radius,
                         primitive.startAngleRadians);
    if (point == sketch::PointKey::End)
      return radialPoint(primitive.points[0], primitive.radius,
                         primitive.startAngleRadians +
                             primitive.sweepAngleRadians);
    break;
  }
  return std::nullopt;
}

std::optional<Point2d>
markerCurvePoint(const MarkerPrimitiveGeometry &primitive, double parameter) {
  if (!std::isfinite(parameter) || parameter < 0.0 || parameter > 1.0)
    return std::nullopt;
  switch (primitive.kind) {
  case SketchPrimitiveKind::Point:
    return std::nullopt;
  case SketchPrimitiveKind::Line:
    return Point2d{
        std::lerp(primitive.points[0].x, primitive.points[1].x, parameter),
        std::lerp(primitive.points[0].y, primitive.points[1].y, parameter)};
  case SketchPrimitiveKind::Circle: {
    const double angle = parameter == 1.0 ? 0.0 : fullTurn * parameter;
    return radialPoint(primitive.points[0], primitive.radius, angle);
  }
  case SketchPrimitiveKind::Arc: {
    const double angle =
        parameter == 0.0 ? primitive.startAngleRadians
        : parameter == 1.0
            ? primitive.startAngleRadians + primitive.sweepAngleRadians
            : primitive.startAngleRadians +
                  primitive.sweepAngleRadians * parameter;
    return radialPoint(primitive.points[0], primitive.radius, angle);
  }
  }
  return std::nullopt;
}

Result<Point2d> resolveMarkerPrimitiveLocation(
    const SketchMarkerPrimitiveLocation &location,
    const MarkerPrimitiveGeometry &primitive, const char *invalidPointCode,
    const char *invalidPointMessage, const char *invalidCurveCode,
    const char *invalidCurveMessage) {
  if (const auto *point = std::get_if<SketchMarkerPointLocation>(&location)) {
    auto resolved = markerSemanticPoint(primitive, point->point);
    if (!resolved)
      return std::unexpected(diagnostic(invalidPointCode, invalidPointMessage));
    return *resolved;
  }
  const auto *curve = std::get_if<SketchMarkerCurveLocation>(&location);
  if (!curve)
    return std::unexpected(diagnostic(invalidCurveCode, invalidCurveMessage));
  auto resolved = markerCurvePoint(primitive, curve->normalizedParameter);
  if (!resolved)
    return std::unexpected(diagnostic(invalidCurveCode, invalidCurveMessage));
  return *resolved;
}

bool isDimension(SketchMarkerKind kind) {
  const auto category = markerCategory(kind);
  return category && *category == SketchMarkerCategory::Dimension;
}

std::pair<std::uint8_t, std::uint8_t>
markerAnchorRange(SketchMarkerCategory category) {
  switch (category) {
  case SketchMarkerCategory::Constraint:
  case SketchMarkerCategory::Inference:
  case SketchMarkerCategory::Dimension:
    return {1U, 3U};
  case SketchMarkerCategory::DegreeOfFreedom:
  case SketchMarkerCategory::SnapCursor:
    return {1U, 1U};
  }
  return {0U, 0U};
}

Result<void> validateMarkerAnchor(
    const SketchMarkerAnchor &anchor, const SketchSceneSnapshot &base,
    const std::shared_ptr<const SketchProvisionalGeometry> &provisional) {
  auto resolved = resolveSketchMarkerAnchor(anchor, base, provisional.get());
  if (!resolved)
    return std::unexpected(std::move(resolved.error()));
  return {};
}

struct MarkerByteCounts {
  std::size_t input = 0;
  std::size_t retained = 0;
  std::size_t scratch = 0;
  std::size_t peak = 0;
};

Result<MarkerByteCounts> markerByteCounts(
    std::size_t inputMarkerCount, std::size_t inputAnchorCount,
    std::size_t retainedMarkerCapacity, std::size_t retainedAnchorCapacity,
    std::size_t retainedConstraintCapacity, std::size_t scratchMarkerCapacity,
    std::size_t scratchConstraintCapacity) {
  MarkerByteCounts result;
  std::size_t inputMarkers = 0;
  std::size_t inputAnchors = 0;
  if (!checkedMultiply(inputMarkerCount, sizeof(PackedSketchMarker),
                       inputMarkers) ||
      !checkedMultiply(inputAnchorCount, sizeof(SketchMarkerAnchor),
                       inputAnchors) ||
      !checkedAdd(inputMarkers, inputAnchors, result.input))
    return std::unexpected(
        diagnostic("render.sketch.marker-input-limit",
                   "sketch marker input byte count overflowed"));
  std::size_t retainedMarkers = 0;
  std::size_t retainedAnchors = 0;
  std::size_t retainedConstraints = 0;
  std::size_t retainedPayload = 0;
  if (!checkedMultiply(retainedMarkerCapacity, sizeof(PackedSketchMarker),
                       retainedMarkers) ||
      !checkedMultiply(retainedAnchorCapacity, sizeof(SketchMarkerAnchor),
                       retainedAnchors) ||
      !checkedMultiply(retainedConstraintCapacity, sizeof(std::uint32_t),
                       retainedConstraints) ||
      !checkedAdd(retainedMarkers, retainedAnchors, retainedPayload) ||
      !checkedAdd(retainedPayload, retainedConstraints, retainedPayload) ||
      !checkedAdd(sizeof(SketchMarkerPacket), retainedPayload, result.retained))
    return std::unexpected(
        diagnostic("render.sketch.marker-memory-limit",
                   "sketch marker retained byte count overflowed"));
  std::size_t scratchMarkers = 0;
  std::size_t scratchConstraints = 0;
  if (!checkedMultiply(scratchMarkerCapacity, sizeof(PackedSketchMarker),
                       scratchMarkers) ||
      !checkedMultiply(scratchConstraintCapacity, sizeof(std::uint32_t),
                       scratchConstraints) ||
      !checkedAdd(scratchMarkers, scratchConstraints, result.scratch))
    return std::unexpected(
        diagnostic("render.sketch.marker-scratch-limit",
                   "sketch marker scratch byte count overflowed"));
  if (!checkedAdd(result.retained, result.scratch, result.peak))
    return std::unexpected(
        diagnostic("render.sketch.marker-peak-build-limit",
                   "sketch marker peak byte count overflowed"));
  return result;
}

Result<void> validateMarkerLimits(std::size_t markerCount,
                                  std::size_t anchorCount,
                                  const MarkerByteCounts &bytes,
                                  const SketchMarkerLimits &limits) {
  if (markerCount > limits.maximumMarkerCount)
    return std::unexpected(diagnostic("render.sketch.marker-count-limit",
                                      "sketch marker count exceeds its limit"));
  if (anchorCount > limits.maximumAnchorCount)
    return std::unexpected(
        diagnostic("render.sketch.marker-anchor-count-limit",
                   "sketch marker anchor count exceeds its limit"));
  if (bytes.input > limits.maximumInputBytes)
    return std::unexpected(
        diagnostic("render.sketch.marker-input-limit",
                   "sketch marker input exceeds its byte limit"));
  if (bytes.retained > limits.maximumRetainedBytes)
    return std::unexpected(
        diagnostic("render.sketch.marker-memory-limit",
                   "sketch marker packet exceeds its retained memory limit"));
  if (bytes.scratch > limits.maximumScratchBytes)
    return std::unexpected(
        diagnostic("render.sketch.marker-scratch-limit",
                   "sketch marker packet exceeds its scratch memory limit"));
  if (bytes.peak > limits.maximumPeakBuildBytes)
    return std::unexpected(
        diagnostic("render.sketch.marker-peak-build-limit",
                   "sketch marker packet exceeds its peak build memory limit"));
  return {};
}

bool sortMarkers(std::vector<PackedSketchMarker> &values,
                 std::vector<PackedSketchMarker> &scratch,
                 std::stop_token cancellation) {
  return mergeSortVector(
      values, scratch,
      [](const auto &first, const auto &second) {
        return first.handle < second.handle;
      },
      cancellation);
}

bool sortConstraintIndex(std::vector<std::uint32_t> &values,
                         std::vector<std::uint32_t> &scratch,
                         std::span<const PackedSketchMarker> markers,
                         std::stop_token cancellation) {
  return mergeSortVector(
      values, scratch,
      [markers](std::uint32_t first, std::uint32_t second) {
        return *markers[first].constraint < *markers[second].constraint;
      },
      cancellation);
}

Point2d canonicalPoint(Point2d point) {
  if (point.x == 0.0)
    point.x = 0.0;
  if (point.y == 0.0)
    point.y = 0.0;
  return point;
}

bool equivalentPickDistance(double first, double second) {
  if (first == second)
    return true;
  if (!std::isfinite(first) || !std::isfinite(second) || first < 0.0 ||
      second < 0.0)
    return false;
  const std::uint64_t firstBits = std::bit_cast<std::uint64_t>(first);
  const std::uint64_t secondBits = std::bit_cast<std::uint64_t>(second);
  const std::uint64_t difference =
      firstBits > secondBits ? firstBits - secondBits : secondBits - firstBits;
  return difference <= 1'024U;
}

} // namespace

Result<SceneGeneration> SceneGeneration::create(std::uint64_t value) {
  if (value == 0)
    return std::unexpected(diagnostic("render.scene.zero-generation",
                                      "scene generation must be positive"));
  return SceneGeneration{value};
}

Result<SketchPresentationGeneration>
SketchPresentationGeneration::create(std::uint64_t value) {
  if (value == 0)
    return std::unexpected(
        diagnostic("render.sketch.zero-presentation-generation",
                   "sketch presentation generation must be positive"));
  return SketchPresentationGeneration{value};
}

Result<RenderSessionHandle> RenderSessionHandle::create(std::uint64_t value) {
  if (value == 0)
    return std::unexpected(diagnostic("render.scene.zero-session-handle",
                                      "render session handle must be nonzero"));
  return RenderSessionHandle{value};
}

Result<SketchPrimitiveHandle>
SketchPrimitiveHandle::create(std::uint32_t value) {
  if (value == 0)
    return std::unexpected(
        diagnostic("render.sketch.zero-primitive-handle",
                   "sketch primitive handle must be nonzero"));
  return SketchPrimitiveHandle{value};
}

SketchSceneSnapshot::SketchSceneSnapshot(
    SceneStamp stamp, Bounds2d bounds, std::vector<SketchStyle> styles,
    std::vector<Point2d> points, std::vector<PackedSketchPrimitive> primitives,
    std::vector<std::uint32_t> semanticIndex)
    : stamp_(std::move(stamp)), bounds_(bounds), styles_(std::move(styles)),
      points_(std::move(points)), primitives_(std::move(primitives)),
      semanticIndex_(std::move(semanticIndex)) {}

Result<SketchSceneSnapshot>
SketchSceneSnapshot::create(SceneStamp stamp, std::vector<SketchStyle> styles,
                            std::vector<Point2d> points,
                            std::vector<PackedSketchPrimitive> primitives) {
  if (styles.size() >
      static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max()) + 1U)
    return std::unexpected(diagnostic("render.sketch.too-many-styles",
                                      "sketch scene has too many styles"));
  for (const SketchStyle &style : styles) {
    if (auto valid = validateBaseStyle(style); !valid)
      return std::unexpected(std::move(valid.error()));
  }
  SketchPrimitiveBatch batch{std::move(points), std::move(primitives)};
  if (auto valid = validateBatch(batch, styles.size()); !valid)
    return std::unexpected(std::move(valid.error()));
  const Bounds2d bounds = sceneBounds(batch.points, batch.primitives);
  if (!bounds.empty && (!finite(bounds.minimum) || !finite(bounds.maximum)))
    return std::unexpected(
        diagnostic("render.sketch.unrepresentable-bounds",
                   "sketch bounds exceed finite coordinate range"));
  std::vector<std::uint32_t> semanticIndex;
  try {
    semanticIndex.reserve(batch.primitives.size());
    for (std::size_t ordinal = 0; ordinal < batch.primitives.size(); ++ordinal)
      semanticIndex.push_back(static_cast<std::uint32_t>(ordinal));
    std::ranges::sort(semanticIndex, [&batch](std::uint32_t first,
                                              std::uint32_t second) {
      return batch.primitives[first].entity < batch.primitives[second].entity;
    });
  } catch (const std::bad_alloc &) {
    return std::unexpected(
        diagnostic("render.sketch.semantic-index-allocation",
                   "sketch semantic index could not be allocated"));
  }
  return SketchSceneSnapshot{std::move(stamp),
                             bounds,
                             std::move(styles),
                             std::move(batch.points),
                             std::move(batch.primitives),
                             std::move(semanticIndex)};
}

const PackedSketchPrimitive *
SketchSceneSnapshot::findPrimitive(SketchEntityId entity) const {
  const auto found = std::lower_bound(
      semanticIndex_.begin(), semanticIndex_.end(), entity,
      [this](std::uint32_t ordinal, const SketchEntityId &value) {
        return primitives_[ordinal].entity < value;
      });
  if (found == semanticIndex_.end() || primitives_[*found].entity != entity)
    return nullptr;
  return &primitives_[*found];
}

std::size_t SketchSceneSnapshot::semanticIndexBytes() const {
  return semanticIndex_.capacity() * sizeof(std::uint32_t);
}

SketchProjectionStyles defaultSketchProjectionStyles() {
  return {{SketchStyleRole::Regular, SketchLinePattern::Solid, 1.5F, 7.0F, 1},
          {SketchStyleRole::Construction, SketchLinePattern::Dashed, 1.0F, 6.0F,
           0}};
}

Result<SketchSceneSnapshot>
projectSketchScene(SceneStamp stamp, std::span<const sketch::Entity> geometry,
                   SketchProjectionStyles styles) {
  if (styles.regular.role != SketchStyleRole::Regular ||
      styles.construction.role != SketchStyleRole::Construction)
    return std::unexpected(
        diagnostic("render.sketch.projection-style-role",
                   "sketch projection style roles are inconsistent"));
  if (geometry.size() > std::numeric_limits<std::uint32_t>::max() / 2U)
    return std::unexpected(diagnostic("render.sketch.too-many-entities",
                                      "sketch projection is too large"));

  std::unordered_set<SketchEntityId, TypedIdHash<SketchEntityIdTag>> ids;
  ids.reserve(geometry.size());
  std::vector<Point2d> points;
  std::vector<PackedSketchPrimitive> primitives;
  points.reserve(geometry.size() * 2U);
  primitives.reserve(geometry.size());
  for (std::size_t index = 0; index < geometry.size(); ++index) {
    const sketch::Entity &entity = geometry[index];
    const SketchEntityId id = sketch::entityId(entity);
    if (!ids.insert(id).second)
      return std::unexpected(diagnostic("render.sketch.duplicate-entity",
                                        "sketch entity is duplicated"));
    auto handle =
        SketchPrimitiveHandle::create(static_cast<std::uint32_t>(index + 1U));
    if (!handle)
      return std::unexpected(std::move(handle.error()));
    PackedSketchPrimitive primitive{id,
                                    *handle,
                                    static_cast<std::uint32_t>(points.size()),
                                    0,
                                    SketchPrimitiveKind::Point,
                                    SketchPrimitiveFlags::Visible |
                                        SketchPrimitiveFlags::Selectable,
                                    0.0,
                                    0.0,
                                    0.0};
    bool construction = false;
    std::visit(
        [&]<typename Value>(const Value &value) {
          using Type = std::decay_t<Value>;
          construction = value.construction;
          if constexpr (std::is_same_v<Type, sketch::PointEntity>) {
            primitive.kind = SketchPrimitiveKind::Point;
            points.push_back({value.point.x.si(), value.point.y.si()});
          } else if constexpr (std::is_same_v<Type, sketch::LineEntity>) {
            primitive.kind = SketchPrimitiveKind::Line;
            points.push_back({value.start.x.si(), value.start.y.si()});
            points.push_back({value.end.x.si(), value.end.y.si()});
          } else if constexpr (std::is_same_v<Type, sketch::CircleEntity>) {
            primitive.kind = SketchPrimitiveKind::Circle;
            primitive.radius = value.radius.si();
            points.push_back({value.center.x.si(), value.center.y.si()});
          } else {
            primitive.kind = SketchPrimitiveKind::Arc;
            primitive.radius = value.radius.si();
            primitive.startAngleRadians = value.startAngle.si();
            primitive.sweepAngleRadians =
                value.endAngle.si() - value.startAngle.si();
            points.push_back({value.center.x.si(), value.center.y.si()});
          }
        },
        entity);
    primitive.style = construction ? 1U : 0U;
    primitives.push_back(std::move(primitive));
  }

  std::vector<SketchStyle> palette{styles.regular, styles.construction};
  return SketchSceneSnapshot::create(std::move(stamp), std::move(palette),
                                     std::move(points), std::move(primitives));
}

std::optional<Point2d> semanticPoint(const SketchSceneSnapshot &scene,
                                     const PackedSketchPrimitive &primitive,
                                     sketch::PointKey key) {
  MarkerPrimitiveGeometry geometry{
      primitive.kind,
      {scene.points()[primitive.firstPoint], Point2d{}},
      primitive.radius,
      primitive.startAngleRadians,
      primitive.sweepAngleRadians};
  if (primitive.kind == SketchPrimitiveKind::Line)
    geometry.points[1] = scene.points()[primitive.firstPoint + 1U];
  return markerSemanticPoint(geometry, key);
}

Result<Point2d>
resolveSketchMarkerAnchor(const SketchMarkerAnchor &anchor,
                          const SketchSceneSnapshot &base,
                          const SketchProvisionalGeometry *provisional) {
  if (const auto *evaluated = std::get_if<SketchBaseMarkerAnchor>(&anchor)) {
    const PackedSketchPrimitive *primitive =
        base.findPrimitive(evaluated->entity);
    if (!primitive)
      return std::unexpected(
          diagnostic("render.sketch.marker-unknown-base-entity",
                     "sketch marker anchor references unknown base geometry"));
    MarkerPrimitiveGeometry geometry{
        primitive->kind,
        {base.points()[primitive->firstPoint], Point2d{}},
        primitive->radius,
        primitive->startAngleRadians,
        primitive->sweepAngleRadians};
    if (primitive->kind == SketchPrimitiveKind::Line)
      geometry.points[1] = base.points()[primitive->firstPoint + 1U];
    auto resolved = resolveMarkerPrimitiveLocation(
        evaluated->location, geometry,
        "render.sketch.marker-invalid-base-point",
        "sketch marker anchor references an invalid base point",
        "render.sketch.marker-invalid-base-curve-location",
        "sketch marker anchor has an invalid base curve location");
    if (!resolved)
      return resolved;
    return canonicalPoint(*resolved);
  }
  if (const auto *draft = std::get_if<SketchProvisionalMarkerAnchor>(&anchor)) {
    if (!provisional)
      return std::unexpected(
          diagnostic("render.sketch.marker-missing-provisional",
                     "provisional sketch marker anchor has no dependency"));
    if (provisional->stamp().target.base != base.stamp())
      return std::unexpected(diagnostic(
          "render.sketch.marker-provisional-base-mismatch",
          "provisional sketch marker anchor belongs to another base scene"));
    const PackedSketchProvisionalPrimitive *primitive =
        provisional->findPrimitive(draft->primitive);
    if (!primitive)
      return std::unexpected(diagnostic(
          "render.sketch.marker-unknown-provisional-primitive",
          "sketch marker anchor references unknown provisional geometry"));
    const MarkerPrimitiveGeometry geometry{
        primitive->kind, primitive->points, primitive->radius,
        primitive->startAngleRadians, primitive->sweepAngleRadians};
    auto resolved = resolveMarkerPrimitiveLocation(
        draft->location, geometry,
        "render.sketch.marker-invalid-provisional-point",
        "sketch marker anchor references an invalid provisional point",
        "render.sketch.marker-invalid-provisional-curve-location",
        "sketch marker anchor has an invalid provisional curve location");
    if (!resolved)
      return resolved;
    return canonicalPoint(*resolved);
  }
  const auto *canonical = std::get_if<SketchCanonicalMarkerAnchor>(&anchor);
  if (!canonical || !finite(canonical->point))
    return std::unexpected(
        diagnostic("render.sketch.marker-invalid-canonical-point",
                   "sketch marker canonical anchor is not finite"));
  return canonicalPoint(canonical->point);
}

SketchOverlayRoleSet::SketchOverlayRoleSet(
    std::shared_ptr<const SketchSceneSnapshot> base, SketchOverlayRole role,
    SketchOverlayRoleSetDigest digest, std::vector<SketchOverlayScope> scopes,
    std::size_t inputBytes, std::size_t retainedBytes, std::size_t scratchBytes,
    std::size_t peakBuildBytes)
    : base_(std::move(base)), role_(role), digest_(digest),
      scopes_(std::move(scopes)), inputBytes_(inputBytes),
      retainedBytes_(retainedBytes), scratchBytes_(scratchBytes),
      peakBuildBytes_(peakBuildBytes) {}

Result<std::shared_ptr<const SketchOverlayRoleSet>>
SketchOverlayRoleSet::create(std::shared_ptr<const SketchSceneSnapshot> base,
                             SketchOverlayRole role,
                             std::span<const SketchOverlayScope> scopes,
                             SketchOverlayRoleSetLimits limits) {
  return create(std::move(base), role, scopes, limits, {});
}

Result<std::shared_ptr<const SketchOverlayRoleSet>>
SketchOverlayRoleSet::create(std::shared_ptr<const SketchSceneSnapshot> base,
                             SketchOverlayRole role,
                             std::span<const SketchOverlayScope> scopes,
                             SketchOverlayRoleSetLimits limits,
                             std::stop_token cancellation) {
  const auto cancelledResult = [] {
    return std::unexpected(
        diagnostic("render.sketch.overlay-cancelled",
                   "sketch overlay role-set construction was cancelled"));
  };
  if (cancellation.stop_requested())
    return cancelledResult();
  if (!base)
    return std::unexpected(
        diagnostic("render.sketch.overlay-null-base",
                   "sketch overlay role-set base is missing"));
  if (!overlayRoleIndex(role))
    return std::unexpected(
        diagnostic("render.sketch.overlay-invalid-role",
                   "sketch overlay role-set role is invalid"));
  const std::size_t sortScratchCount = scopes.size() > 1U ? scopes.size() : 0U;
  auto minimum =
      overlayRoleSetBytes(scopes.size(), 0U, scopes.size(), sortScratchCount);
  if (!minimum)
    return std::unexpected(std::move(minimum.error()));
  if (auto bounded =
          validateOverlayRoleSetLimits(scopes.size(), *minimum, limits);
      !bounded)
    return std::unexpected(std::move(bounded.error()));

  try {
    std::vector<SketchOverlayScope> working{scopes.begin(), scopes.end()};
    if (cancellation.stop_requested())
      return cancelledResult();
    std::size_t work = 0U;
    for (const SketchOverlayScope &scope : working) {
      const PackedSketchPrimitive *primitive =
          base->findPrimitive(scope.entity);
      if (!primitive)
        return std::unexpected(
            diagnostic("render.sketch.overlay-unknown-entity",
                       "sketch overlay role-set references an unknown entity"));
      if (scope.point && !semanticPoint(*base, *primitive, *scope.point))
        return std::unexpected(
            diagnostic("render.sketch.overlay-invalid-point",
                       "sketch overlay role-set point is invalid"));
      if (cancelled(cancellation, work))
        return cancelledResult();
    }
    std::vector<SketchOverlayScope> sortScratch;
    if (working.size() > 1U)
      sortScratch = working;
    if (!mergeSortVector(working, sortScratch, std::less<>{}, cancellation))
      return cancelledResult();
    const auto unique = std::ranges::unique(working);
    working.erase(unique.begin(), unique.end());
    if (cancellation.stop_requested())
      return cancelledResult();

    auto requested =
        overlayRoleSetBytes(scopes.size(), working.size(), working.capacity(),
                            sortScratch.capacity());
    if (!requested)
      return std::unexpected(std::move(requested.error()));
    if (auto bounded =
            validateOverlayRoleSetLimits(scopes.size(), *requested, limits);
        !bounded)
      return std::unexpected(std::move(bounded.error()));

    std::vector<SketchOverlayScope> normalized;
    normalized.reserve(working.size());
    for (const SketchOverlayScope &scope : working) {
      normalized.push_back(scope);
      if (cancelled(cancellation, work))
        return cancelledResult();
    }
    auto digest = hashOverlayRoleSet(role, normalized, cancellation);
    if (!digest)
      return std::unexpected(std::move(digest.error()));
    auto actual =
        overlayRoleSetBytes(scopes.size(), normalized.capacity(),
                            working.capacity(), sortScratch.capacity());
    if (!actual)
      return std::unexpected(std::move(actual.error()));
    if (auto bounded =
            validateOverlayRoleSetLimits(scopes.size(), *actual, limits);
        !bounded)
      return std::unexpected(std::move(bounded.error()));
    auto created =
        std::shared_ptr<const SketchOverlayRoleSet>{new SketchOverlayRoleSet{
            std::move(base), role, *digest, std::move(normalized),
            actual->input, actual->retained, actual->scratch, actual->peak}};
    if (cancellation.stop_requested())
      return cancelledResult();
    return created;
  } catch (const std::bad_alloc &) {
    return std::unexpected(
        diagnostic("render.sketch.overlay-allocation",
                   "sketch overlay role-set allocation failed"));
  }
}

bool SketchOverlayRoleSet::contains(SketchOverlayScope scope) const {
  return std::ranges::binary_search(scopes_, scope);
}

SketchPresentationOverlay::SketchPresentationOverlay(
    std::shared_ptr<const SketchSceneSnapshot> base,
    SketchPresentationGeneration generation, SketchOverlayDigest payloadDigest,
    std::array<SketchOverlayRoleSetPtr, 4> roleSets)
    : base_(std::move(base)), generation_(generation),
      payloadDigest_(payloadDigest), roleSets_(std::move(roleSets)) {}

Result<std::shared_ptr<const SketchPresentationOverlay>>
SketchPresentationOverlay::create(
    std::shared_ptr<const SketchSceneSnapshot> base,
    SketchPresentationGeneration generation,
    std::span<const SketchOverlayRoleSetPtr> roleSets) {
  if (!base)
    return std::unexpected(diagnostic("render.sketch.overlay-null-base",
                                      "sketch overlay base is missing"));
  if (roleSets.size() != 4U)
    return std::unexpected(
        diagnostic("render.sketch.overlay-role-set-count",
                   "sketch overlay requires exactly one set for every role"));
  std::array<SketchOverlayRoleSetPtr, 4> canonical;
  for (const SketchOverlayRoleSetPtr &set : roleSets) {
    if (!set)
      return std::unexpected(
          diagnostic("render.sketch.overlay-missing-role-set",
                     "sketch overlay role set is missing"));
    const auto index = overlayRoleIndex(set->role());
    if (!index)
      return std::unexpected(diagnostic(
          "render.sketch.overlay-wrong-role-set",
          "sketch overlay contains a role set with an invalid role"));
    if (canonical[*index])
      return std::unexpected(
          diagnostic("render.sketch.overlay-duplicate-role-set",
                     "sketch overlay contains duplicate role sets"));
    if (set->base().get() != base.get())
      return std::unexpected(diagnostic(
          "render.sketch.overlay-role-set-base",
          "sketch overlay role set does not retain its exact base scene"));
    canonical[*index] = set;
  }
  if (std::ranges::any_of(canonical, [](const auto &set) { return !set; }))
    return std::unexpected(diagnostic("render.sketch.overlay-missing-role-set",
                                      "sketch overlay role set is missing"));
  const SketchOverlayDigest digest = hashOverlay(canonical);
  try {
    return std::shared_ptr<const SketchPresentationOverlay>{
        new SketchPresentationOverlay{std::move(base), generation, digest,
                                      std::move(canonical)}};
  } catch (const std::bad_alloc &) {
    return std::unexpected(diagnostic("render.sketch.overlay-allocation",
                                      "sketch overlay allocation failed"));
  }
}

SketchOverlayRoleSetPtr
SketchPresentationOverlay::roleSet(SketchOverlayRole role) const {
  const auto index = overlayRoleIndex(role);
  return index ? roleSets_[*index] : nullptr;
}

std::optional<SketchStyleRole>
SketchPresentationOverlay::resolve(SketchOverlayScope scope) const {
  const PackedSketchPrimitive *primitive = base_->findPrimitive(scope.entity);
  if (!primitive ||
      (scope.point && !semanticPoint(*base_, *primitive, *scope.point)))
    return std::nullopt;

  SketchStyleRole resolved = base_->styles()[primitive->style].role;
  const SketchOverlayScope entityScope{scope.entity, std::nullopt};
  for (const SketchOverlayRoleSetPtr &set : roleSets_) {
    if (set->contains(entityScope) || (scope.point && set->contains(scope)))
      resolved = styleRole(set->role());
  }
  return resolved;
}

LatestSketchPresentation::LatestSketchPresentation(SceneStamp scene)
    : scene_(std::move(scene)) {}

void LatestSketchPresentation::retarget(SceneStamp scene) {
  std::scoped_lock lock{mutex_};
  if (scene_ == scene)
    return;
  scene_ = std::move(scene);
  latest_.reset();
}

Result<SketchOverlayDecision> LatestSketchPresentation::publish(
    std::shared_ptr<const SketchPresentationOverlay> overlay) {
  if (!overlay)
    return std::unexpected(diagnostic("render.sketch.overlay-null-publish",
                                      "published sketch overlay is missing"));
  std::scoped_lock lock{mutex_};
  if (overlay->base()->stamp() != scene_)
    return SketchOverlayDecision::StaleScene;
  if (!latest_) {
    latest_ = std::move(overlay);
    return SketchOverlayDecision::Accepted;
  }
  if (overlay->generation() < latest_->generation())
    return SketchOverlayDecision::StaleGeneration;
  if (overlay->generation() == latest_->generation()) {
    if (overlay->payloadDigest() == latest_->payloadDigest())
      return SketchOverlayDecision::Duplicate;
    return SketchOverlayDecision::GenerationConflict;
  }
  latest_ = std::move(overlay);
  return SketchOverlayDecision::Accepted;
}

std::shared_ptr<const SketchPresentationOverlay>
LatestSketchPresentation::latest() const {
  std::scoped_lock lock{mutex_};
  return latest_;
}

std::size_t LatestSketchPresentation::retainedCount() const {
  std::scoped_lock lock{mutex_};
  return latest_ ? 1U : 0U;
}

Result<SketchEditSessionHandle>
SketchEditSessionHandle::create(std::uint64_t value) {
  if (value == 0U)
    return std::unexpected(
        diagnostic("render.sketch.zero-edit-session-handle",
                   "sketch edit-session handle must be nonzero"));
  return SketchEditSessionHandle{value};
}

Result<SketchToolInstanceHandle>
SketchToolInstanceHandle::create(std::uint64_t value) {
  if (value == 0U)
    return std::unexpected(
        diagnostic("render.sketch.zero-tool-instance-handle",
                   "sketch tool-instance handle must be nonzero"));
  return SketchToolInstanceHandle{value};
}

Result<SketchProvisionalGeneration>
SketchProvisionalGeneration::create(std::uint64_t value) {
  if (value == 0U)
    return std::unexpected(
        diagnostic("render.sketch.zero-provisional-generation",
                   "sketch provisional generation must be nonzero"));
  return SketchProvisionalGeneration{value};
}

Result<SketchProvisionalPrimitiveHandle>
SketchProvisionalPrimitiveHandle::create(std::uint32_t value) {
  if (value == 0U)
    return std::unexpected(
        diagnostic("render.sketch.zero-provisional-primitive-handle",
                   "sketch provisional primitive handle must be nonzero"));
  return SketchProvisionalPrimitiveHandle{value};
}

SketchProvisionalGeometry::SketchProvisionalGeometry(
    SketchProvisionalStamp stamp,
    std::vector<PackedSketchProvisionalPrimitive> primitives,
    std::size_t inputBytes, std::size_t retainedBytes, std::size_t scratchBytes,
    std::size_t peakBuildBytes)
    : stamp_(std::move(stamp)), primitives_(std::move(primitives)),
      inputBytes_(inputBytes), retainedBytes_(retainedBytes),
      scratchBytes_(scratchBytes), peakBuildBytes_(peakBuildBytes) {}

Result<std::shared_ptr<const SketchProvisionalGeometry>>
SketchProvisionalGeometry::create(
    SketchProvisionalStamp stamp,
    std::span<const PackedSketchProvisionalPrimitive> primitives,
    SketchProvisionalLimits limits) {
  return create(std::move(stamp), primitives, limits, {});
}

Result<std::shared_ptr<const SketchProvisionalGeometry>>
SketchProvisionalGeometry::create(
    SketchProvisionalStamp stamp,
    std::span<const PackedSketchProvisionalPrimitive> primitives,
    SketchProvisionalLimits limits, std::stop_token cancellation) {
  if (cancellation.stop_requested())
    return std::unexpected(
        diagnostic("render.sketch.provisional-cancelled",
                   "provisional sketch construction was cancelled"));
  const std::size_t scratchCount =
      primitives.size() > 1U ? primitives.size() : 0U;
  auto requested =
      provisionalByteCounts(primitives.size(), primitives.size(), scratchCount);
  if (!requested)
    return std::unexpected(std::move(requested.error()));
  if (auto bounded = validateProvisionalLimits(*requested, limits); !bounded)
    return std::unexpected(std::move(bounded.error()));

  std::size_t validationWork = 0;
  for (const PackedSketchProvisionalPrimitive &primitive : primitives) {
    if (auto valid = validateProvisionalPrimitive(primitive); !valid)
      return std::unexpected(std::move(valid.error()));
    if (cancelled(cancellation, validationWork))
      return std::unexpected(
          diagnostic("render.sketch.provisional-cancelled",
                     "provisional sketch construction was cancelled"));
  }
  if (cancellation.stop_requested())
    return std::unexpected(
        diagnostic("render.sketch.provisional-cancelled",
                   "provisional sketch construction was cancelled"));

  try {
    std::vector<PackedSketchProvisionalPrimitive> normalized{primitives.begin(),
                                                             primitives.end()};
    if (cancellation.stop_requested())
      return std::unexpected(
          diagnostic("render.sketch.provisional-cancelled",
                     "provisional sketch construction was cancelled"));
    std::vector<PackedSketchProvisionalPrimitive> scratch;
    if (normalized.size() > 1U)
      scratch = normalized;
    if (!sortProvisionalPrimitives(normalized, scratch, cancellation))
      return std::unexpected(
          diagnostic("render.sketch.provisional-cancelled",
                     "provisional sketch construction was cancelled"));
    for (std::size_t index = 1U; index < normalized.size(); ++index) {
      if (normalized[index - 1U].handle == normalized[index].handle)
        return std::unexpected(
            diagnostic("render.sketch.provisional-duplicate-handle",
                       "provisional sketch primitive handle is duplicated"));
      if (cancelled(cancellation, validationWork))
        return std::unexpected(
            diagnostic("render.sketch.provisional-cancelled",
                       "provisional sketch construction was cancelled"));
    }
    if (cancellation.stop_requested())
      return std::unexpected(
          diagnostic("render.sketch.provisional-cancelled",
                     "provisional sketch construction was cancelled"));

    auto actual = provisionalByteCounts(
        primitives.size(), normalized.capacity(), scratch.capacity());
    if (!actual)
      return std::unexpected(std::move(actual.error()));
    if (auto bounded = validateProvisionalLimits(*actual, limits); !bounded)
      return std::unexpected(std::move(bounded.error()));
    auto created = std::shared_ptr<const SketchProvisionalGeometry>{
        new SketchProvisionalGeometry{std::move(stamp), std::move(normalized),
                                      actual->input, actual->retained,
                                      actual->scratch, actual->peak}};
    if (cancellation.stop_requested())
      return std::unexpected(
          diagnostic("render.sketch.provisional-cancelled",
                     "provisional sketch construction was cancelled"));
    return created;
  } catch (const std::bad_alloc &) {
    return std::unexpected(
        diagnostic("render.sketch.provisional-allocation",
                   "provisional sketch geometry could not be allocated"));
  }
}

const PackedSketchProvisionalPrimitive *
SketchProvisionalGeometry::findPrimitive(
    SketchProvisionalPrimitiveHandle handle) const {
  const auto found = std::ranges::lower_bound(
      primitives_, handle, {}, &PackedSketchProvisionalPrimitive::handle);
  return found != primitives_.end() && found->handle == handle ? &*found
                                                               : nullptr;
}

LatestSketchProvisionalGeometry::LatestSketchProvisionalGeometry(
    SketchProvisionalTarget target)
    : target_(std::move(target)) {}

void LatestSketchProvisionalGeometry::retarget(SketchProvisionalTarget target) {
  std::scoped_lock lock{mutex_};
  if (target_ == target)
    return;
  target_ = std::move(target);
  latest_.reset();
}

Result<SketchProvisionalDecision> LatestSketchProvisionalGeometry::publish(
    std::shared_ptr<const SketchProvisionalGeometry> geometry) {
  if (!geometry)
    return std::unexpected(
        diagnostic("render.sketch.provisional-null-publish",
                   "published provisional sketch geometry is missing"));
  std::scoped_lock lock{mutex_};
  if (geometry->stamp().target != target_)
    return SketchProvisionalDecision::StaleTarget;
  if (!latest_) {
    latest_ = std::move(geometry);
    return SketchProvisionalDecision::Accepted;
  }
  if (geometry->stamp().generation < latest_->stamp().generation)
    return SketchProvisionalDecision::StaleGeneration;
  if (geometry->stamp().generation == latest_->stamp().generation) {
    if (geometry->stamp().payload == latest_->stamp().payload &&
        std::ranges::equal(geometry->primitives(), latest_->primitives()))
      return SketchProvisionalDecision::Duplicate;
    return SketchProvisionalDecision::GenerationConflict;
  }
  latest_ = std::move(geometry);
  return SketchProvisionalDecision::Accepted;
}

std::shared_ptr<const SketchProvisionalGeometry>
LatestSketchProvisionalGeometry::latest() const {
  std::scoped_lock lock{mutex_};
  return latest_;
}

std::size_t LatestSketchProvisionalGeometry::retainedCount() const {
  std::scoped_lock lock{mutex_};
  return latest_ ? 1U : 0U;
}

Result<SketchMarkerGeneration>
SketchMarkerGeneration::create(std::uint64_t value) {
  if (value == 0U)
    return std::unexpected(
        diagnostic("render.sketch.zero-marker-generation",
                   "sketch marker generation must be nonzero"));
  return SketchMarkerGeneration{value};
}

Result<SketchMarkerViewGeneration>
SketchMarkerViewGeneration::create(std::uint64_t value) {
  if (value == 0U)
    return std::unexpected(
        diagnostic("render.sketch.zero-marker-view-generation",
                   "sketch marker view generation must be nonzero"));
  return SketchMarkerViewGeneration{value};
}

Result<SketchMarkerHandle> SketchMarkerHandle::create(std::uint32_t value) {
  if (value == 0U)
    return std::unexpected(diagnostic("render.sketch.zero-marker-handle",
                                      "sketch marker handle must be nonzero"));
  return SketchMarkerHandle{value};
}

std::optional<SketchMarkerCategory> markerCategory(SketchMarkerKind kind) {
  switch (kind) {
  case SketchMarkerKind::CoincidentConstraint:
  case SketchMarkerKind::HorizontalConstraint:
  case SketchMarkerKind::VerticalConstraint:
  case SketchMarkerKind::ParallelConstraint:
  case SketchMarkerKind::PerpendicularConstraint:
  case SketchMarkerKind::TangentConstraint:
  case SketchMarkerKind::EqualConstraint:
  case SketchMarkerKind::ConcentricConstraint:
  case SketchMarkerKind::MidpointConstraint:
  case SketchMarkerKind::FixedConstraint:
  case SketchMarkerKind::CollinearConstraint:
    return SketchMarkerCategory::Constraint;
  case SketchMarkerKind::HorizontalInference:
  case SketchMarkerKind::VerticalInference:
  case SketchMarkerKind::ParallelInference:
  case SketchMarkerKind::PerpendicularInference:
  case SketchMarkerKind::TangentInference:
  case SketchMarkerKind::CollinearInference:
    return SketchMarkerCategory::Inference;
  case SketchMarkerKind::TranslationDegreeOfFreedom:
  case SketchMarkerKind::RotationDegreeOfFreedom:
    return SketchMarkerCategory::DegreeOfFreedom;
  case SketchMarkerKind::DistanceDimension:
  case SketchMarkerKind::HorizontalDistanceDimension:
  case SketchMarkerKind::VerticalDistanceDimension:
  case SketchMarkerKind::RadiusDimension:
  case SketchMarkerKind::DiameterDimension:
  case SketchMarkerKind::AngleDimension:
    return SketchMarkerCategory::Dimension;
  case SketchMarkerKind::EndpointSnap:
  case SketchMarkerKind::MidpointSnap:
  case SketchMarkerKind::CenterSnap:
  case SketchMarkerKind::IntersectionSnap:
  case SketchMarkerKind::QuadrantSnap:
  case SketchMarkerKind::GridSnap:
    return SketchMarkerCategory::SnapCursor;
  }
  return std::nullopt;
}

SketchMarkerPacket::SketchMarkerPacket(
    SketchMarkerStamp stamp, std::shared_ptr<const SketchSceneSnapshot> base,
    std::shared_ptr<const SketchProvisionalGeometry> provisional,
    std::vector<SketchMarkerAnchor> anchors,
    std::vector<PackedSketchMarker> markers,
    std::vector<std::uint32_t> constraintIndex, std::size_t inputBytes,
    std::size_t retainedBytes, std::size_t scratchBytes,
    std::size_t peakBuildBytes)
    : stamp_(std::move(stamp)), base_(std::move(base)),
      provisional_(std::move(provisional)), anchors_(std::move(anchors)),
      markers_(std::move(markers)),
      constraintIndex_(std::move(constraintIndex)), inputBytes_(inputBytes),
      retainedBytes_(retainedBytes), scratchBytes_(scratchBytes),
      peakBuildBytes_(peakBuildBytes) {}

Result<std::shared_ptr<const SketchMarkerPacket>> SketchMarkerPacket::create(
    SketchMarkerStamp stamp, std::shared_ptr<const SketchSceneSnapshot> base,
    std::shared_ptr<const SketchProvisionalGeometry> provisional,
    std::span<const SketchMarkerAnchor> anchors,
    std::span<const PackedSketchMarker> markers, SketchMarkerLimits limits) {
  return create(std::move(stamp), std::move(base), std::move(provisional),
                anchors, markers, limits, {});
}

Result<std::shared_ptr<const SketchMarkerPacket>> SketchMarkerPacket::create(
    SketchMarkerStamp stamp, std::shared_ptr<const SketchSceneSnapshot> base,
    std::shared_ptr<const SketchProvisionalGeometry> provisional,
    std::span<const SketchMarkerAnchor> anchors,
    std::span<const PackedSketchMarker> markers, SketchMarkerLimits limits,
    std::stop_token cancellation) {
  const auto cancelledResult = [] {
    return std::unexpected(
        diagnostic("render.sketch.marker-cancelled",
                   "sketch marker construction was cancelled"));
  };
  if (cancellation.stop_requested())
    return cancelledResult();
  if (!base)
    return std::unexpected(
        diagnostic("render.sketch.marker-missing-base",
                   "sketch marker packet has no evaluated base scene"));
  if (base->stamp() != stamp.target.base)
    return std::unexpected(
        diagnostic("render.sketch.marker-base-mismatch",
                   "sketch marker base does not match its exact target"));
  if (markers.size() > std::numeric_limits<std::uint32_t>::max() ||
      anchors.size() > std::numeric_limits<std::uint32_t>::max())
    return std::unexpected(
        diagnostic("render.sketch.marker-count-limit",
                   "sketch marker packed index range was exceeded"));

  auto minimum =
      markerByteCounts(markers.size(), anchors.size(), 0U, 0U, 0U, 0U, 0U);
  if (!minimum)
    return std::unexpected(std::move(minimum.error()));
  if (auto bounded = validateMarkerLimits(markers.size(), anchors.size(),
                                          *minimum, limits);
      !bounded)
    return std::unexpected(std::move(bounded.error()));

  std::size_t nextAnchor = 0U;
  std::size_t constraintCount = 0U;
  std::size_t work = 0U;
  bool viewDependent = false;
  for (const PackedSketchMarker &marker : markers) {
    if (marker.firstAnchor != nextAnchor)
      return std::unexpected(
          diagnostic("render.sketch.marker-non-packed-anchors",
                     "sketch marker anchors are not packed"));
    const auto category = markerCategory(marker.kind);
    if (!category)
      return std::unexpected(diagnostic("render.sketch.marker-invalid-kind",
                                        "sketch marker kind is invalid"));
    const auto [minimumAnchors, maximumAnchors] = markerAnchorRange(*category);
    if (marker.anchorCount < minimumAnchors ||
        marker.anchorCount > maximumAnchors ||
        static_cast<std::size_t>(marker.firstAnchor) > anchors.size() ||
        marker.anchorCount > anchors.size() - marker.firstAnchor)
      return std::unexpected(
          diagnostic("render.sketch.marker-anchor-arity",
                     "sketch marker anchor arity is invalid"));
    if (!std::isfinite(marker.valueSi) ||
        (!isDimension(marker.kind) && marker.valueSi != 0.0))
      return std::unexpected(
          diagnostic("render.sketch.marker-invalid-value",
                     "sketch marker SI value is invalid for its kind"));
    const bool semantic = *category == SketchMarkerCategory::Constraint ||
                          *category == SketchMarkerCategory::Dimension;
    if (semantic && !marker.constraint)
      return std::unexpected(
          diagnostic("render.sketch.marker-missing-constraint",
                     "persistent sketch marker has no constraint identity"));
    if (!semantic && marker.constraint)
      return std::unexpected(diagnostic(
          "render.sketch.marker-unexpected-constraint",
          "transient sketch marker has persistent constraint identity"));
    if (semantic)
      ++constraintCount;
    const bool screenDerived = *category == SketchMarkerCategory::Inference ||
                               *category == SketchMarkerCategory::SnapCursor;
    viewDependent = viewDependent || screenDerived;
    nextAnchor += marker.anchorCount;
    if (cancelled(cancellation, work))
      return cancelledResult();
  }
  if (nextAnchor != anchors.size())
    return std::unexpected(
        diagnostic("render.sketch.marker-unused-anchors",
                   "sketch marker packet contains unused anchors"));
  bool provisionalAnchored = false;
  for (const SketchMarkerAnchor &anchor : anchors) {
    provisionalAnchored =
        provisionalAnchored ||
        std::holds_alternative<SketchProvisionalMarkerAnchor>(anchor);
    if (auto valid = validateMarkerAnchor(anchor, *base, provisional); !valid)
      return std::unexpected(std::move(valid.error()));
    if (cancelled(cancellation, work))
      return cancelledResult();
  }
  if (provisionalAnchored) {
    if (!stamp.target.provisional)
      return std::unexpected(diagnostic(
          "render.sketch.marker-provisional-target-required",
          "provisional marker anchor has no exact target reference"));
    if (!stamp.target.interaction)
      return std::unexpected(diagnostic(
          "render.sketch.marker-provisional-interaction",
          "provisional marker anchor requires edit and tool identity"));
    const SketchProvisionalStamp &dependency = provisional->stamp();
    const SketchProvisionalTarget expectedProvisional{
        stamp.target.base, stamp.target.interaction->editSession,
        stamp.target.interaction->toolInstance};
    if (dependency.target != expectedProvisional ||
        dependency.generation != stamp.target.provisional->generation ||
        dependency.payload != stamp.target.provisional->payload)
      return std::unexpected(diagnostic(
          "render.sketch.marker-provisional-mismatch",
          "sketch marker provisional dependency does not match its target"));
  } else if (stamp.target.provisional || provisional) {
    return std::unexpected(diagnostic(
        "render.sketch.marker-unused-provisional",
        "sketch marker packet has an unused provisional dependency"));
  }
  if (viewDependent && !stamp.target.interaction)
    return std::unexpected(
        diagnostic("render.sketch.marker-interaction-required",
                   "screen-derived marker has no interaction identity"));
  if (viewDependent && !stamp.target.view)
    return std::unexpected(
        diagnostic("render.sketch.marker-view-required",
                   "screen-derived marker has no view generation"));
  if (!viewDependent && stamp.target.view)
    return std::unexpected(
        diagnostic("render.sketch.marker-unused-view",
                   "sketch marker packet has an unused view generation"));
  if (!provisionalAnchored && !viewDependent && stamp.target.interaction)
    return std::unexpected(
        diagnostic("render.sketch.marker-unused-interaction",
                   "sketch marker packet has unused interaction identity"));
  if (cancellation.stop_requested())
    return cancelledResult();

  const std::size_t markerScratchCount =
      markers.size() > 1U ? markers.size() : 0U;
  const std::size_t constraintScratchCount =
      constraintCount > 1U ? constraintCount : 0U;
  auto requested = markerByteCounts(
      markers.size(), anchors.size(), markers.size(), anchors.size(),
      constraintCount, markerScratchCount, constraintScratchCount);
  if (!requested)
    return std::unexpected(std::move(requested.error()));
  if (auto bounded = validateMarkerLimits(markers.size(), anchors.size(),
                                          *requested, limits);
      !bounded)
    return std::unexpected(std::move(bounded.error()));

  try {
    std::vector<PackedSketchMarker> normalizedMarkers{markers.begin(),
                                                      markers.end()};
    if (cancellation.stop_requested())
      return cancelledResult();
    std::vector<PackedSketchMarker> scratch;
    if (normalizedMarkers.size() > 1U)
      scratch = normalizedMarkers;
    if (!sortMarkers(normalizedMarkers, scratch, cancellation))
      return cancelledResult();
    std::vector<SketchMarkerAnchor> normalizedAnchors;
    normalizedAnchors.reserve(anchors.size());
    std::vector<std::uint32_t> constraintIndex;
    constraintIndex.reserve(constraintCount);
    for (std::size_t index = 0U; index < normalizedMarkers.size(); ++index) {
      PackedSketchMarker &marker = normalizedMarkers[index];
      if (index > 0U && normalizedMarkers[index - 1U].handle == marker.handle)
        return std::unexpected(
            diagnostic("render.sketch.marker-duplicate-handle",
                       "sketch marker handle is duplicated"));
      const std::uint32_t sourceAnchor = marker.firstAnchor;
      marker.firstAnchor = static_cast<std::uint32_t>(normalizedAnchors.size());
      if (marker.constraint)
        constraintIndex.push_back(static_cast<std::uint32_t>(index));
      if (marker.valueSi == 0.0)
        marker.valueSi = 0.0;
      for (std::uint8_t ordinal = 0U; ordinal < marker.anchorCount; ++ordinal) {
        SketchMarkerAnchor anchor = anchors[sourceAnchor + ordinal];
        if (auto *point = std::get_if<SketchCanonicalMarkerAnchor>(&anchor)) {
          point->point = canonicalPoint(point->point);
        } else {
          SketchMarkerPrimitiveLocation *location = nullptr;
          if (auto *baseAnchor = std::get_if<SketchBaseMarkerAnchor>(&anchor))
            location = &baseAnchor->location;
          else if (auto *provisionalAnchor =
                       std::get_if<SketchProvisionalMarkerAnchor>(&anchor))
            location = &provisionalAnchor->location;
          if (location) {
            auto *curve = std::get_if<SketchMarkerCurveLocation>(location);
            if (curve && curve->normalizedParameter == 0.0)
              curve->normalizedParameter = 0.0;
          }
        }
        normalizedAnchors.push_back(std::move(anchor));
        if (cancelled(cancellation, work))
          return cancelledResult();
      }
    }
    std::vector<std::uint32_t> constraintScratch;
    if (constraintIndex.size() > 1U)
      constraintScratch = constraintIndex;
    if (!sortConstraintIndex(constraintIndex, constraintScratch,
                             normalizedMarkers, cancellation))
      return cancelledResult();
    for (std::size_t index = 1U; index < constraintIndex.size(); ++index) {
      if (normalizedMarkers[constraintIndex[index - 1U]].constraint ==
          normalizedMarkers[constraintIndex[index]].constraint)
        return std::unexpected(
            diagnostic("render.sketch.marker-duplicate-constraint",
                       "sketch marker constraint identity is duplicated"));
      if (cancelled(cancellation, work))
        return cancelledResult();
    }
    if (cancellation.stop_requested())
      return cancelledResult();
    auto actual = markerByteCounts(
        markers.size(), anchors.size(), normalizedMarkers.capacity(),
        normalizedAnchors.capacity(), constraintIndex.capacity(),
        scratch.capacity(), constraintScratch.capacity());
    if (!actual)
      return std::unexpected(std::move(actual.error()));
    if (auto bounded = validateMarkerLimits(markers.size(), anchors.size(),
                                            *actual, limits);
        !bounded)
      return std::unexpected(std::move(bounded.error()));
    auto created =
        std::shared_ptr<const SketchMarkerPacket>{new SketchMarkerPacket{
            std::move(stamp), std::move(base), std::move(provisional),
            std::move(normalizedAnchors), std::move(normalizedMarkers),
            std::move(constraintIndex), actual->input, actual->retained,
            actual->scratch, actual->peak}};
    if (cancellation.stop_requested())
      return cancelledResult();
    return created;
  } catch (const std::bad_alloc &) {
    return std::unexpected(
        diagnostic("render.sketch.marker-allocation",
                   "sketch marker packet could not be allocated"));
  }
}

const PackedSketchMarker *
SketchMarkerPacket::findMarker(SketchMarkerHandle handle) const {
  const auto found = std::ranges::lower_bound(markers_, handle, {},
                                              &PackedSketchMarker::handle);
  return found != markers_.end() && found->handle == handle ? &*found : nullptr;
}

const PackedSketchMarker *
SketchMarkerPacket::findConstraint(SketchConstraintId constraint) const {
  const auto found = std::lower_bound(
      constraintIndex_.begin(), constraintIndex_.end(), constraint,
      [this](std::uint32_t index, const SketchConstraintId &value) {
        return *markers_[index].constraint < value;
      });
  return found != constraintIndex_.end() &&
                 *markers_[*found].constraint == constraint
             ? &markers_[*found]
             : nullptr;
}

std::span<const SketchMarkerAnchor>
SketchMarkerPacket::markerAnchors(SketchMarkerHandle handle) const {
  const PackedSketchMarker *marker = findMarker(handle);
  if (!marker)
    return {};
  if (marker->anchorCount == 0U ||
      static_cast<std::size_t>(marker->firstAnchor) >= anchors_.size() ||
      marker->anchorCount > anchors_.size() - marker->firstAnchor)
    return {};
  return {anchors_.data() + marker->firstAnchor, marker->anchorCount};
}

LatestSketchMarkerPacket::LatestSketchMarkerPacket(SketchMarkerTarget target)
    : target_(std::move(target)) {}

void LatestSketchMarkerPacket::retarget(SketchMarkerTarget target) {
  std::scoped_lock lock{mutex_};
  if (target_ == target)
    return;
  target_ = std::move(target);
  latest_.reset();
}

Result<SketchMarkerDecision> LatestSketchMarkerPacket::publish(
    std::shared_ptr<const SketchMarkerPacket> packet) {
  if (!packet)
    return std::unexpected(
        diagnostic("render.sketch.marker-null-publish",
                   "published sketch marker packet is missing"));
  std::scoped_lock lock{mutex_};
  if (packet->stamp().target != target_)
    return SketchMarkerDecision::StaleTarget;
  if (!latest_) {
    latest_ = std::move(packet);
    return SketchMarkerDecision::Accepted;
  }
  if (packet->stamp().generation < latest_->stamp().generation)
    return SketchMarkerDecision::StaleGeneration;
  if (packet->stamp().generation == latest_->stamp().generation) {
    if (packet->stamp().payload == latest_->stamp().payload &&
        std::ranges::equal(packet->anchors(), latest_->anchors()) &&
        std::ranges::equal(packet->markers(), latest_->markers()))
      return SketchMarkerDecision::Duplicate;
    return SketchMarkerDecision::GenerationConflict;
  }
  latest_ = std::move(packet);
  return SketchMarkerDecision::Accepted;
}

std::shared_ptr<const SketchMarkerPacket>
LatestSketchMarkerPacket::latest() const {
  std::scoped_lock lock{mutex_};
  return latest_;
}

std::size_t LatestSketchMarkerPacket::retainedCount() const {
  std::scoped_lock lock{mutex_};
  return latest_ ? 1U : 0U;
}

SketchSceneDelta::SketchSceneDelta(
    SceneStamp base, SceneStamp target,
    std::optional<std::vector<SketchStyle>> replacementStyles,
    std::vector<SketchPrimitiveHandle> removed, SketchPrimitiveBatch upserts)
    : base_(std::move(base)), target_(std::move(target)),
      replacementStyles_(std::move(replacementStyles)),
      removed_(std::move(removed)), upserts_(std::move(upserts)) {}

Result<SketchSceneDelta> SketchSceneDelta::create(
    SceneStamp base, SceneStamp target,
    std::optional<std::vector<SketchStyle>> replacementStyles,
    std::vector<SketchPrimitiveHandle> removed, SketchPrimitiveBatch upserts) {
  if (base.target.session != target.target.session)
    return std::unexpected(diagnostic("render.scene.delta-session",
                                      "scene delta crosses render sessions"));
  if (base.target.evaluatedPlane.attachmentBinding !=
      target.target.evaluatedPlane.attachmentBinding)
    return std::unexpected(
        diagnostic("render.scene.delta-attachment-binding",
                   "scene delta crosses plane attachment bindings"));
  if (target.generation <= base.generation)
    return std::unexpected(
        diagnostic("render.scene.delta-generation",
                   "scene delta generation did not advance"));
  if (replacementStyles) {
    if (replacementStyles->size() >
        static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max()) +
            1U)
      return std::unexpected(diagnostic("render.sketch.too-many-styles",
                                        "sketch scene has too many styles"));
    for (const SketchStyle &style : *replacementStyles) {
      if (auto valid = validateBaseStyle(style); !valid)
        return std::unexpected(std::move(valid.error()));
    }
  }
  if (auto valid = validateBatch(upserts, std::nullopt); !valid)
    return std::unexpected(std::move(valid.error()));
  std::unordered_set<SketchPrimitiveHandle, PrimitiveHandleHash> changed;
  changed.reserve(removed.size() + upserts.primitives.size());
  for (const SketchPrimitiveHandle handle : removed) {
    if (!changed.insert(handle).second)
      return std::unexpected(diagnostic("render.scene.delta-duplicate-remove",
                                        "scene delta removes a handle twice"));
  }
  for (const PackedSketchPrimitive &primitive : upserts.primitives) {
    if (!changed.insert(primitive.handle).second)
      return std::unexpected(
          diagnostic("render.scene.delta-overlap",
                     "scene delta removes and upserts one handle"));
  }
  return SketchSceneDelta{std::move(base), std::move(target),
                          std::move(replacementStyles), std::move(removed),
                          std::move(upserts)};
}

Result<std::shared_ptr<const SketchSceneSnapshot>>
applySceneDelta(const SketchSceneSnapshot &base,
                const SketchSceneDelta &delta) {
  if (base.stamp() != delta.base())
    return std::unexpected(diagnostic("render.scene.delta-base-mismatch",
                                      "scene delta base is not installed"));
  const std::vector<SketchStyle> styles =
      delta.replacementStyles()
          ? *delta.replacementStyles()
          : std::vector<SketchStyle>(base.styles().begin(),
                                     base.styles().end());
  std::unordered_set<SketchPrimitiveHandle, PrimitiveHandleHash> removed(
      delta.removed().begin(), delta.removed().end());
  std::unordered_map<SketchPrimitiveHandle, std::size_t, PrimitiveHandleHash>
      upserts;
  upserts.reserve(delta.upserts().primitives.size());
  for (std::size_t index = 0; index < delta.upserts().primitives.size();
       ++index)
    upserts.emplace(delta.upserts().primitives[index].handle, index);

  std::vector<Point2d> points;
  std::vector<PackedSketchPrimitive> primitives;
  points.reserve(base.points().size() + delta.upserts().points.size());
  primitives.reserve(base.primitives().size() +
                     delta.upserts().primitives.size());
  std::unordered_set<SketchPrimitiveHandle, PrimitiveHandleHash> consumed;
  std::unordered_set<SketchPrimitiveHandle, PrimitiveHandleHash> foundRemoved;
  for (const PackedSketchPrimitive &primitive : base.primitives()) {
    if (removed.contains(primitive.handle)) {
      foundRemoved.insert(primitive.handle);
      continue;
    }
    const auto replacement = upserts.find(primitive.handle);
    if (replacement == upserts.end()) {
      appendPrimitive(points, primitives, primitive, base.points());
      continue;
    }
    const PackedSketchPrimitive &upsert =
        delta.upserts().primitives[replacement->second];
    appendPrimitive(points, primitives, upsert, delta.upserts().points);
    consumed.insert(upsert.handle);
  }
  if (foundRemoved.size() != removed.size())
    return std::unexpected(diagnostic("render.scene.delta-missing-remove",
                                      "scene delta removes a missing handle"));
  for (const PackedSketchPrimitive &primitive : delta.upserts().primitives) {
    if (!consumed.contains(primitive.handle))
      appendPrimitive(points, primitives, primitive, delta.upserts().points);
  }
  auto snapshot = SketchSceneSnapshot::create(
      delta.target(), styles, std::move(points), std::move(primitives));
  if (!snapshot)
    return std::unexpected(std::move(snapshot.error()));
  return std::make_shared<const SketchSceneSnapshot>(std::move(*snapshot));
}

Result<SketchSceneEnvelope>
SketchSceneEnvelope::full(std::shared_ptr<const SketchSceneSnapshot> snapshot) {
  if (!snapshot)
    return std::unexpected(diagnostic("render.scene.null-snapshot",
                                      "scene envelope snapshot is null"));
  return SketchSceneEnvelope{std::variant<Full, Delta>{std::move(snapshot)}};
}

Result<SketchSceneEnvelope>
SketchSceneEnvelope::delta(std::shared_ptr<const SketchSceneDelta> delta) {
  if (!delta)
    return std::unexpected(
        diagnostic("render.scene.null-delta", "scene envelope delta is null"));
  return SketchSceneEnvelope{
      std::variant<Full, Delta>{std::in_place_type<Delta>, std::move(delta)}};
}

const SceneStamp &SketchSceneEnvelope::target() const {
  if (isFull())
    return std::get<Full>(data_)->stamp();
  return std::get<Delta>(data_)->target();
}

std::optional<SceneStamp> SketchSceneEnvelope::base() const {
  if (isFull())
    return std::nullopt;
  return std::get<Delta>(data_)->base();
}

const SketchSceneEnvelope::Full &SketchSceneEnvelope::snapshot() const {
  return std::get<Full>(data_);
}

const SketchSceneEnvelope::Delta &SketchSceneEnvelope::sceneDelta() const {
  return std::get<Delta>(data_);
}

SceneEnvelopeDecision
assessSceneEnvelope(const SceneTarget &desired,
                    const std::optional<SceneStamp> &installed,
                    const SketchSceneEnvelope &envelope) {
  if (envelope.target().target != desired)
    return SceneEnvelopeDecision::StaleTarget;
  if (!installed)
    return envelope.isFull() ? SceneEnvelopeDecision::AcceptFull
                             : SceneEnvelopeDecision::MissingBase;
  if (installed->target.session != desired.session)
    return envelope.isFull() ? SceneEnvelopeDecision::AcceptFull
                             : SceneEnvelopeDecision::BaseMismatch;
  if (envelope.target().generation < installed->generation)
    return SceneEnvelopeDecision::StaleGeneration;
  if (envelope.target().generation == installed->generation) {
    return envelope.target() == *installed
               ? SceneEnvelopeDecision::Duplicate
               : SceneEnvelopeDecision::GenerationConflict;
  }
  if (envelope.isFull())
    return SceneEnvelopeDecision::AcceptFull;
  const SceneStamp base = *envelope.base();
  if (base == *installed)
    return SceneEnvelopeDecision::AcceptDelta;
  if (base.target.session != installed->target.session)
    return SceneEnvelopeDecision::BaseMismatch;
  if (base.generation < installed->generation)
    return SceneEnvelopeDecision::StaleGeneration;
  if (base.generation > installed->generation)
    return SceneEnvelopeDecision::GenerationGap;
  return SceneEnvelopeDecision::BaseMismatch;
}

LatestSketchSceneMailbox::LatestSketchSceneMailbox(SceneTarget desired)
    : desired_(std::move(desired)) {}

void LatestSketchSceneMailbox::retarget(SceneTarget desired) {
  std::scoped_lock lock{mutex_};
  desired_ = std::move(desired);
  pending_.reset();
}

Result<SceneOffer>
LatestSketchSceneMailbox::offer(const SketchSceneEnvelope &envelope) {
  std::scoped_lock lock{mutex_};
  const auto pendingStamp =
      pending_ ? std::optional<SceneStamp>{pending_->stamp()} : std::nullopt;
  SceneEnvelopeDecision decision =
      assessSceneEnvelope(desired_, pendingStamp, envelope);
  std::shared_ptr<const SketchSceneSnapshot> base = pending_;

  if (!pending_) {
    const auto installedStamp =
        installed_ ? std::optional<SceneStamp>{installed_->stamp()}
                   : std::nullopt;
    decision = assessSceneEnvelope(desired_, installedStamp, envelope);
    base = installed_;
  } else if (!envelope.isFull() && envelope.target().target == desired_ &&
             installed_ && envelope.base() &&
             *envelope.base() == installed_->stamp() &&
             envelope.target().generation > pending_->stamp().generation) {
    // A cumulative delta from the installed base may replace an unconsumed
    // pending generation without retaining the intermediate delta.
    decision = SceneEnvelopeDecision::AcceptDelta;
    base = installed_;
  }

  if (decision != SceneEnvelopeDecision::AcceptFull &&
      decision != SceneEnvelopeDecision::AcceptDelta)
    return SceneOffer{decision, false};

  std::shared_ptr<const SketchSceneSnapshot> replacement;
  if (decision == SceneEnvelopeDecision::AcceptFull) {
    replacement = envelope.snapshot();
  } else {
    if (!base)
      return SceneOffer{SceneEnvelopeDecision::MissingBase, false};
    auto applied = applySceneDelta(*base, *envelope.sceneDelta());
    if (!applied)
      return std::unexpected(std::move(applied.error()));
    replacement = std::move(*applied);
  }
  const bool replaced = static_cast<bool>(pending_);
  pending_ = std::move(replacement);
  return SceneOffer{decision, replaced};
}

std::shared_ptr<const SketchSceneSnapshot>
LatestSketchSceneMailbox::takeLatest() {
  std::scoped_lock lock{mutex_};
  if (pending_)
    installed_ = std::exchange(pending_, {});
  return installed_;
}

std::shared_ptr<const SketchSceneSnapshot>
LatestSketchSceneMailbox::installed() const {
  std::scoped_lock lock{mutex_};
  return installed_;
}

std::size_t LatestSketchSceneMailbox::pendingCount() const {
  std::scoped_lock lock{mutex_};
  return pending_ ? 1U : 0U;
}

struct SketchPickIndex::Data {
  std::shared_ptr<const SketchSceneSnapshot> scene;
  SketchPickIndexOptions options;
  std::unique_ptr<PickNode[]> nodes;
  std::unique_ptr<PickTarget[]> targets;
  std::size_t nodeCount = 0;
  std::size_t leafCount = 0;
  std::size_t targetCount = 0;
  std::size_t retainedBytes = 0;
  std::size_t scratchBytes = 0;
  std::size_t peakBuildBytes = 0;
  std::uint32_t root = 0;
};

Result<SketchPickIndex>
SketchPickIndex::build(std::shared_ptr<const SketchSceneSnapshot> scene,
                       SketchPickIndexOptions options) {
  return build(std::move(scene), options, {});
}

Result<SketchPickIndex>
SketchPickIndex::build(std::shared_ptr<const SketchSceneSnapshot> scene,
                       SketchPickIndexOptions options,
                       std::stop_token cancellation) {
  if (!scene)
    return std::unexpected(
        diagnostic("render.pick.null-scene", "pick index scene is null"));
  if (options.maximumRetainedBytes == 0 || options.maximumScratchBytes == 0 ||
      options.maximumPeakBuildBytes == 0 || options.maximumLeafTargets == 0 ||
      options.maximumVisitedNodesPerPass == 0 ||
      options.maximumRefinedTargetsPerPass == 0 ||
      options.maximumVisitedNodesPerPass >
          std::numeric_limits<std::uint32_t>::max() / 2U ||
      options.maximumRefinedTargetsPerPass >
          std::numeric_limits<std::uint32_t>::max() / 2U)
    return std::unexpected(diagnostic("render.pick.invalid-index-options",
                                      "pick index options must be positive"));
  if (cancellation.stop_requested())
    return std::unexpected(
        diagnostic("render.pick.cancelled", "pick index build was cancelled"));

  std::size_t targetCount = 0;
  std::size_t cancellationWork = 0;
  for (const PackedSketchPrimitive &primitive : scene->primitives()) {
    if (cancelled(cancellation, cancellationWork))
      return std::unexpected(diagnostic("render.pick.cancelled",
                                        "pick index build was cancelled"));
    if (!hasFlag(primitive.flags, SketchPrimitiveFlags::Visible) ||
        !hasFlag(primitive.flags, SketchPrimitiveFlags::Selectable))
      continue;
    std::size_t pointCount = 0;
    (void)pointKeys(primitive, pointCount);
    const std::size_t added =
        pointCount + (primitive.kind == SketchPrimitiveKind::Point ? 0U : 1U);
    if (!checkedAdd(targetCount, added, targetCount))
      return std::unexpected(diagnostic("render.pick.index-size-overflow",
                                        "pick target count overflowed"));
  }
  if (targetCount > std::numeric_limits<std::uint32_t>::max())
    return std::unexpected(diagnostic("render.pick.too-many-targets",
                                      "pick index has too many targets"));

  const std::size_t leafCount =
      ceilDivide(targetCount, options.maximumLeafTargets);
  std::size_t nodeCount = leafCount;
  for (std::size_t level = leafCount; level > 1U;) {
    level = ceilDivide(level, pickBranchFactor);
    if (!checkedAdd(nodeCount, level, nodeCount))
      return std::unexpected(diagnostic("render.pick.index-size-overflow",
                                        "pick node count overflowed"));
  }
  if (nodeCount > std::numeric_limits<std::uint32_t>::max())
    return std::unexpected(diagnostic("render.pick.too-many-nodes",
                                      "pick index has too many nodes"));

  std::size_t targetBytes = 0;
  std::size_t nodeBytes = 0;
  std::size_t retainedBytes = sizeof(Data);
  std::size_t oneScratchBytes = 0;
  std::size_t scratchBytes = 0;
  std::size_t peakBuildBytes = 0;
  if (!checkedMultiply(targetCount, sizeof(PickTarget), targetBytes) ||
      !checkedMultiply(nodeCount, sizeof(PickNode), nodeBytes) ||
      !checkedAdd(retainedBytes, targetBytes, retainedBytes) ||
      !checkedAdd(retainedBytes, nodeBytes, retainedBytes) ||
      !checkedMultiply(targetCount, sizeof(PickBuildTarget), oneScratchBytes) ||
      !checkedMultiply(oneScratchBytes, 2U, scratchBytes) ||
      !checkedAdd(retainedBytes, scratchBytes, peakBuildBytes))
    return std::unexpected(diagnostic("render.pick.index-size-overflow",
                                      "pick index byte count overflowed"));
  if (retainedBytes > options.maximumRetainedBytes)
    return std::unexpected(
        diagnostic("render.pick.retained-byte-budget",
                   "pick index exceeds its retained budget"));
  if (scratchBytes > options.maximumScratchBytes)
    return std::unexpected(diagnostic("render.pick.scratch-byte-budget",
                                      "pick index exceeds its scratch budget"));
  if (peakBuildBytes > options.maximumPeakBuildBytes)
    return std::unexpected(diagnostic("render.pick.peak-byte-budget",
                                      "pick index exceeds its peak budget"));

  try {
    auto data = std::make_shared<Data>();
    data->scene = std::move(scene);
    data->options = options;
    data->nodeCount = nodeCount;
    data->leafCount = leafCount;
    data->targetCount = targetCount;
    data->retainedBytes = retainedBytes;
    data->scratchBytes = scratchBytes;
    data->peakBuildBytes = peakBuildBytes;
    if (nodeCount != 0)
      data->nodes = std::make_unique<PickNode[]>(nodeCount);
    if (targetCount != 0)
      data->targets = std::make_unique<PickTarget[]>(targetCount);
    if (targetCount == 0)
      return SketchPickIndex{std::move(data)};

    auto buildTargets = std::make_unique<PickBuildTarget[]>(targetCount);
    auto sortScratch = std::make_unique<PickBuildTarget[]>(targetCount);
    std::size_t write = 0;
    for (std::size_t ordinal = 0; ordinal < data->scene->primitives().size();
         ++ordinal) {
      const PackedSketchPrimitive &primitive =
          data->scene->primitives()[ordinal];
      if (cancelled(cancellation, cancellationWork))
        return std::unexpected(diagnostic("render.pick.cancelled",
                                          "pick index build was cancelled"));
      if (!hasFlag(primitive.flags, SketchPrimitiveFlags::Visible) ||
          !hasFlag(primitive.flags, SketchPrimitiveFlags::Selectable))
        continue;
      std::size_t pointCount = 0;
      const auto keys = pointKeys(primitive, pointCount);
      for (std::size_t pointIndex = 0; pointIndex < pointCount; ++pointIndex) {
        const auto point =
            semanticPoint(*data->scene, primitive, keys[pointIndex]);
        if (!point)
          return std::unexpected(diagnostic("render.pick.missing-point",
                                            "semantic pick point is missing"));
        const PickAabb bounds = pointAabb(*point);
        buildTargets[write++] = {bounds,
                                 point->x,
                                 point->y,
                                 {static_cast<std::uint32_t>(ordinal),
                                  keys[pointIndex], PickTargetKind::Point,
                                  static_cast<std::uint8_t>(pointIndex)}};
      }
      if (primitive.kind != SketchPrimitiveKind::Point) {
        const PickAabb bounds =
            pickAabb(primitiveBounds(data->scene->points(), primitive));
        if (!finite(bounds))
          return std::unexpected(
              diagnostic("render.pick.unrepresentable-bounds",
                         "pick target bounds exceed finite coordinate range"));
        buildTargets[write++] = {
            bounds,
            std::midpoint(bounds.minimumX, bounds.maximumX),
            std::midpoint(bounds.minimumY, bounds.maximumY),
            {static_cast<std::uint32_t>(ordinal), sketch::PointKey::Point,
             PickTargetKind::Curve, 0}};
      }
    }

    const auto totalLess = [](const PickBuildTarget &first,
                              const PickBuildTarget &second, bool yFirst) {
      const double firstPrimary = yFirst ? first.centerY : first.centerX;
      const double secondPrimary = yFirst ? second.centerY : second.centerX;
      if (firstPrimary != secondPrimary)
        return firstPrimary < secondPrimary;
      const double firstSecondary = yFirst ? first.centerX : first.centerY;
      const double secondSecondary = yFirst ? second.centerX : second.centerY;
      if (firstSecondary != secondSecondary)
        return firstSecondary < secondSecondary;
      if (first.target.ordinal != second.target.ordinal)
        return first.target.ordinal < second.target.ordinal;
      if (first.target.kind != second.target.kind)
        return first.target.kind < second.target.kind;
      return first.target.tieRank < second.target.tieRank;
    };
    if (!mergeSortRange(
            buildTargets.get(), sortScratch.get(), 0, targetCount,
            [&](const auto &first, const auto &second) {
              return totalLess(first, second, false);
            },
            cancellation))
      return std::unexpected(diagnostic("render.pick.cancelled",
                                        "pick index build was cancelled"));
    const std::size_t sliceCount = ceilSquareRoot(leafCount);
    const std::size_t targetsPerSlice = ceilDivide(targetCount, sliceCount);
    for (std::size_t begin = 0; begin < targetCount; begin += targetsPerSlice) {
      const std::size_t end = std::min(targetCount, begin + targetsPerSlice);
      if (!mergeSortRange(
              buildTargets.get(), sortScratch.get(), begin, end,
              [&](const auto &first, const auto &second) {
                return totalLess(first, second, true);
              },
              cancellation))
        return std::unexpected(diagnostic("render.pick.cancelled",
                                          "pick index build was cancelled"));
    }

    for (std::size_t index = 0; index < targetCount; ++index) {
      data->targets[index] = buildTargets[index].target;
      if (cancelled(cancellation, cancellationWork))
        return std::unexpected(diagnostic("render.pick.cancelled",
                                          "pick index build was cancelled"));
    }
    std::size_t nodeWrite = 0;
    for (std::size_t first = 0; first < targetCount;
         first += options.maximumLeafTargets) {
      const std::size_t count = std::min<std::size_t>(
          options.maximumLeafTargets, targetCount - first);
      PickAabb bounds = buildTargets[first].bounds;
      for (std::size_t index = first + 1U; index < first + count; ++index)
        include(bounds, buildTargets[index].bounds);
      data->nodes[nodeWrite++] = {bounds, static_cast<std::uint32_t>(first),
                                  static_cast<std::uint32_t>(count), true};
      if (cancelled(cancellation, cancellationWork))
        return std::unexpected(diagnostic("render.pick.cancelled",
                                          "pick index build was cancelled"));
    }
    std::size_t levelFirst = 0;
    std::size_t levelCount = leafCount;
    while (levelCount > 1U) {
      const std::size_t parentFirst = nodeWrite;
      for (std::size_t first = 0; first < levelCount;
           first += pickBranchFactor) {
        const std::size_t count =
            std::min(pickBranchFactor, levelCount - first);
        PickAabb bounds = data->nodes[levelFirst + first].bounds;
        for (std::size_t index = 1; index < count; ++index)
          include(bounds, data->nodes[levelFirst + first + index].bounds);
        data->nodes[nodeWrite++] = {
            bounds, static_cast<std::uint32_t>(levelFirst + first),
            static_cast<std::uint32_t>(count), false};
        if (cancelled(cancellation, cancellationWork))
          return std::unexpected(diagnostic("render.pick.cancelled",
                                            "pick index build was cancelled"));
      }
      levelFirst = parentFirst;
      levelCount = ceilDivide(levelCount, pickBranchFactor);
    }
    data->root = static_cast<std::uint32_t>(nodeWrite - 1U);
    if (cancellation.stop_requested())
      return std::unexpected(diagnostic("render.pick.cancelled",
                                        "pick index build was cancelled"));
    return SketchPickIndex{std::move(data)};
  } catch (const std::bad_alloc &) {
    return std::unexpected(diagnostic("render.pick.allocation-failed",
                                      "pick index allocation failed"));
  }
}

Result<std::optional<SketchPickResult>>
SketchPickIndex::pick(const SketchPickQuery &query) const {
  SketchPickOutcome outcome = this->query(query);
  switch (outcome.status) {
  case SketchPickStatus::Hit:
  case SketchPickStatus::Miss:
    return std::move(outcome.result);
  case SketchPickStatus::WorkBudgetExceeded:
    return std::unexpected(diagnostic("render.pick.query-budget",
                                      "pick query exceeded its work budget"));
  case SketchPickStatus::InvalidQuery:
    return std::unexpected(diagnostic("render.pick.invalid-query",
                                      "sketch pick query is invalid"));
  case SketchPickStatus::NonFiniteArithmetic:
    return std::unexpected(
        diagnostic("render.pick.non-finite-arithmetic",
                   "pick query produced non-finite arithmetic"));
  }
  return std::unexpected(
      diagnostic("render.pick.invalid-status", "pick query status is invalid"));
}

SketchPickOutcome SketchPickIndex::query(const SketchPickQuery &query) const {
  std::array<std::uint32_t, recommendedQueryStackCapacity> stack{};
  return this->query(query, {stack}, {});
}

SketchPickOutcome
SketchPickIndex::query(const SketchPickQuery &query,
                       SketchPickQueryWorkspace workspace,
                       SketchPickEligibility eligibility) const {
  if (!finite(query.point) || !std::isfinite(query.tolerance) ||
      query.tolerance < 0.0 ||
      (static_cast<std::uint8_t>(query.targets) &
       ~static_cast<std::uint8_t>(SketchPickTargets::All)) != 0)
    return {SketchPickStatus::InvalidQuery, std::nullopt, {}, std::nullopt};
  if ((eligibility.context == nullptr) != (eligibility.evaluate == nullptr))
    return {SketchPickStatus::InvalidQuery, std::nullopt, {}, std::nullopt};
  if (query.targets == SketchPickTargets::None || data_->targetCount == 0)
    return {SketchPickStatus::Miss, std::nullopt, {}, std::nullopt};
  const PickAabb queryBounds{
      query.point.x - query.tolerance, query.point.y - query.tolerance,
      query.point.x + query.tolerance, query.point.y + query.tolerance};
  if (!finite(queryBounds))
    return {
        SketchPickStatus::NonFiniteArithmetic, std::nullopt, {}, std::nullopt};
  if (!intersects(queryBounds, data_->nodes[data_->root].bounds))
    return {SketchPickStatus::Miss, std::nullopt, {}, std::nullopt};
  if (workspace.nodeStack.empty())
    return {
        SketchPickStatus::WorkBudgetExceeded, std::nullopt, {}, std::nullopt};

  struct Candidate {
    SketchPickResult result;
    double rankingDistance;
    std::uint16_t layer;
    std::uint32_t ordinal;
    std::uint8_t tieRank;
  };
  std::optional<Candidate> best;
  double minimumDistance = std::numeric_limits<double>::infinity();
  const auto structurallyBetter = [](const Candidate &candidate,
                                     const Candidate &current) {
    const bool candidatePoint = candidate.result.pointKey.has_value();
    const bool currentPoint = current.result.pointKey.has_value();
    if (candidatePoint != currentPoint)
      return candidatePoint;
    if (candidate.layer != current.layer)
      return candidate.layer > current.layer;
    if (candidate.ordinal != current.ordinal)
      return candidate.ordinal > current.ordinal;
    return candidate.tieRank < current.tieRank;
  };

  SketchPickMetrics metrics{0U, 0U, 1U};
  std::size_t stackSize = 1U;
  workspace.nodeStack[0] = data_->root;
  while (stackSize != 0U) {
    if (metrics.visitedNodes == data_->options.maximumVisitedNodesPerPass)
      return {SketchPickStatus::WorkBudgetExceeded, std::nullopt, metrics,
              std::nullopt};
    const PickNode &node = data_->nodes[workspace.nodeStack[--stackSize]];
    ++metrics.visitedNodes;
    if (!intersects(queryBounds, node.bounds))
      continue;
    if (!node.leaf) {
      for (std::size_t offset = node.count; offset > 0U; --offset) {
        const std::uint32_t child =
            node.first + static_cast<std::uint32_t>(offset - 1U);
        if (!intersects(queryBounds, data_->nodes[child].bounds))
          continue;
        if (stackSize == workspace.nodeStack.size())
          return {SketchPickStatus::WorkBudgetExceeded, std::nullopt, metrics,
                  std::nullopt};
        workspace.nodeStack[stackSize++] = child;
      }
      continue;
    }
    for (std::size_t offset = 0U; offset < node.count; ++offset) {
      const PickTarget &target = data_->targets[node.first + offset];
      const bool point = target.kind == PickTargetKind::Point;
      if ((point && !hasTarget(query.targets, SketchPickTargets::Points)) ||
          (!point && !hasTarget(query.targets, SketchPickTargets::Curves)))
        continue;
      if (metrics.refinedTargets == data_->options.maximumRefinedTargetsPerPass)
        return {SketchPickStatus::WorkBudgetExceeded, std::nullopt, metrics,
                std::nullopt};
      ++metrics.refinedTargets;
      const PackedSketchPrimitive &primitive =
          data_->scene->primitives()[target.ordinal];
      PickRefinement refinement;
      std::optional<sketch::PointKey> pointKey;
      if (point) {
        const auto semantic =
            semanticPoint(*data_->scene, primitive, target.pointKey);
        refinement = robustPointRefinement(query.point, *semantic);
        pointKey = target.pointKey;
      } else {
        refinement =
            robustCurveRefinement(*data_->scene, primitive, query.point);
      }
      if (!std::isfinite(refinement.distance) ||
          !finite(refinement.closestPoint))
        return {SketchPickStatus::NonFiniteArithmetic, std::nullopt, metrics,
                std::nullopt};
      if (refinement.distance > query.tolerance)
        continue;
      Candidate candidate{{data_->scene->stamp(), primitive.entity,
                           primitive.handle, pointKey, refinement.closestPoint,
                           refinement.distance},
                          refinement.distance,
                          data_->scene->styles()[primitive.style].layer,
                          target.ordinal,
                          target.tieRank};
      if (eligibility.evaluate) {
        const SketchPickEligibility::Evaluation evaluated =
            eligibility.evaluate(eligibility.context, candidate.result);
        switch (evaluated.decision) {
        case SketchPickEligibilityDecision::Eligible:
          if (!std::isfinite(evaluated.rankingDistance) ||
              evaluated.rankingDistance < 0.0)
            return {SketchPickStatus::NonFiniteArithmetic, std::nullopt,
                    metrics, std::nullopt};
          candidate.rankingDistance = evaluated.rankingDistance;
          break;
        case SketchPickEligibilityDecision::Ineligible:
          continue;
        case SketchPickEligibilityDecision::WorkBudgetExceeded:
          return {SketchPickStatus::WorkBudgetExceeded, std::nullopt, metrics,
                  std::nullopt};
        case SketchPickEligibilityDecision::NonFiniteArithmetic:
          return {SketchPickStatus::NonFiniteArithmetic, std::nullopt, metrics,
                  std::nullopt};
        }
      }
      if (!best || (!equivalentPickDistance(candidate.rankingDistance,
                                            minimumDistance) &&
                    candidate.rankingDistance < minimumDistance)) {
        minimumDistance = candidate.rankingDistance;
        best = std::move(candidate);
      } else if (candidate.rankingDistance < minimumDistance) {
        minimumDistance = candidate.rankingDistance;
        if (!equivalentPickDistance(best->rankingDistance, minimumDistance) ||
            structurallyBetter(candidate, *best))
          best = std::move(candidate);
      } else if (equivalentPickDistance(candidate.rankingDistance,
                                        minimumDistance) &&
                 structurallyBetter(candidate, *best)) {
        best = std::move(candidate);
      }
    }
  }
  if (!best)
    return {SketchPickStatus::Miss, std::nullopt, metrics, std::nullopt};
  const double rankingDistance = best->rankingDistance;
  return {SketchPickStatus::Hit, std::move(best->result), metrics,
          rankingDistance};
}

const SketchSceneSnapshot &SketchPickIndex::scene() const {
  return *data_->scene;
}

std::size_t SketchPickIndex::leafCount() const { return data_->leafCount; }

std::size_t SketchPickIndex::nodeCount() const { return data_->nodeCount; }

std::size_t SketchPickIndex::targetCount() const { return data_->targetCount; }

std::size_t SketchPickIndex::retainedBytes() const {
  return data_->retainedBytes;
}

std::size_t SketchPickIndex::scratchBytes() const {
  return data_->scratchBytes;
}

std::size_t SketchPickIndex::peakBuildBytes() const {
  return data_->peakBuildBytes;
}

std::size_t SketchPickIndex::indexedReferenceCount() const {
  return data_->targetCount;
}

} // namespace kearne::render
