#include <kearne/sketch/model.hpp>
#include <kearne/sketch/nurbs.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <numbers>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace kearne::sketch {
namespace {

enum class EntityKind {
  Point,
  Line,
  Circle,
  Arc,
  Ellipse,
  EllipticalArc,
  HyperbolicArc,
  ParabolicArc,
  BSpline,
};

struct FlatEntity {
  SketchEntityId id;
  EntityKind kind;
  std::vector<double> values;
  std::vector<double> knots;
  std::size_t controlPointCount = 0U;
  std::uint32_t degree = 0U;
  bool periodic = false;

  FlatEntity(SketchEntityId requestedId, EntityKind requestedKind,
             std::vector<double> requestedValues)
      : id(requestedId), kind(requestedKind),
        values(std::move(requestedValues)) {}

  FlatEntity(SketchEntityId requestedId, EntityKind requestedKind,
             std::vector<double> requestedValues,
             std::vector<double> requestedKnots,
             std::size_t requestedControlPointCount,
             std::uint32_t requestedDegree, bool requestedPeriodic)
      : id(requestedId), kind(requestedKind),
        values(std::move(requestedValues)), knots(std::move(requestedKnots)),
        controlPointCount(requestedControlPointCount), degree(requestedDegree),
        periodic(requestedPeriodic) {}
};

using EntityIndex = std::unordered_map<SketchEntityId, std::size_t,
                                       TypedIdHash<SketchEntityIdTag>>;

bool validUtf8(std::string_view text) {
  const auto *bytes = reinterpret_cast<const unsigned char *>(text.data());
  std::size_t index = 0;
  while (index < text.size()) {
    const unsigned char first = bytes[index++];
    if (first <= 0x7fU)
      continue;
    std::size_t remaining = 0;
    if (first >= 0xc2U && first <= 0xdfU)
      remaining = 1;
    else if (first >= 0xe0U && first <= 0xefU)
      remaining = 2;
    else if (first >= 0xf0U && first <= 0xf4U)
      remaining = 3;
    else
      return false;
    if (remaining > text.size() - index)
      return false;
    const unsigned char second = bytes[index];
    if ((first == 0xe0U && second < 0xa0U) ||
        (first == 0xedU && second >= 0xa0U) ||
        (first == 0xf0U && second < 0x90U) ||
        (first == 0xf4U && second >= 0x90U))
      return false;
    for (std::size_t count = 0; count < remaining; ++count)
      if ((bytes[index++] & 0xc0U) != 0x80U)
        return false;
  }
  return true;
}

FlatEntity flatten(const Entity &entity) {
  return std::visit(
      []<typename Value>(const Value &value) -> FlatEntity {
        using Type = std::decay_t<Value>;
        if constexpr (std::is_same_v<Type, PointEntity>) {
          return {value.id,
                  EntityKind::Point,
                  {value.point.x.si(), value.point.y.si()}};
        } else if constexpr (std::is_same_v<Type, LineEntity>) {
          return {value.id,
                  EntityKind::Line,
                  {value.start.x.si(), value.start.y.si(), value.end.x.si(),
                   value.end.y.si()}};
        } else if constexpr (std::is_same_v<Type, CircleEntity>) {
          return {
              value.id,
              EntityKind::Circle,
              {value.center.x.si(), value.center.y.si(), value.radius.si()}};
        } else if constexpr (std::is_same_v<Type, ArcEntity>) {
          return {value.id,
                  EntityKind::Arc,
                  {value.center.x.si(), value.center.y.si(), value.radius.si(),
                   value.startAngle.si(), value.endAngle.si()}};
        } else if constexpr (std::is_same_v<Type, EllipseEntity>) {
          return {value.id,
                  EntityKind::Ellipse,
                  {value.center.x.si(), value.center.y.si(),
                   value.majorRadius.si(), value.minorRadius.si(),
                   value.rotation.si()}};
        } else if constexpr (std::is_same_v<Type, EllipticalArcEntity>) {
          return {value.id,
                  EntityKind::EllipticalArc,
                  {value.center.x.si(), value.center.y.si(),
                   value.majorRadius.si(), value.minorRadius.si(),
                   value.rotation.si(), value.startParameter.si(),
                   value.endParameter.si()}};
        } else if constexpr (std::is_same_v<Type, HyperbolicArcEntity>) {
          return {value.id,
                  EntityKind::HyperbolicArc,
                  {value.center.x.si(), value.center.y.si(),
                   value.majorRadius.si(), value.minorRadius.si(),
                   value.rotation.si(), value.startParameter.si(),
                   value.endParameter.si()}};
        } else if constexpr (std::is_same_v<Type, ParabolicArcEntity>) {
          return {value.id,
                  EntityKind::ParabolicArc,
                  {value.vertex.x.si(), value.vertex.y.si(),
                   value.focalLength.si(), value.rotation.si(),
                   value.startParameter.si(), value.endParameter.si()}};
        } else {
          std::vector<double> values;
          values.reserve(value.controlPoints.size() * 2U +
                         value.weights.size());
          for (const Point2 &point : value.controlPoints) {
            values.push_back(point.x.si());
            values.push_back(point.y.si());
          }
          for (const DimensionlessValue weight : value.weights)
            values.push_back(weight.si());
          std::vector<double> knots;
          knots.reserve(value.knots.size());
          for (const DimensionlessValue knot : value.knots)
            knots.push_back(knot.si());
          return {
              value.id,         EntityKind::BSpline,        std::move(values),
              std::move(knots), value.controlPoints.size(), value.degree,
              value.periodic};
        }
      },
      entity);
}

Result<std::vector<FlatEntity>> flatten(const std::vector<Entity> &entities) {
  std::vector<FlatEntity> result;
  result.reserve(entities.size());
  for (const Entity &entity : entities)
    result.push_back(flatten(entity));
  return result;
}

EntityIndex indexOf(const std::vector<FlatEntity> &entities) {
  EntityIndex result;
  result.reserve(entities.size());
  for (std::size_t index = 0; index < entities.size(); ++index)
    result.emplace(entities[index].id, index);
  return result;
}

Result<const FlatEntity *> find(const std::vector<FlatEntity> &entities,
                                const EntityIndex &index, SketchEntityId id) {
  const auto found = index.find(id);
  if (found == index.end())
    return std::unexpected(
        diagnostic("sketch.reference.missing-entity",
                   "constraint references a missing entity"));
  return &entities[found->second];
}

struct PlainPoint {
  double x;
  double y;
};

PlainPoint rotatedPoint(const FlatEntity &entity, double localX, double localY,
                        std::size_t rotationIndex) {
  const double cosine = std::cos(entity.values[rotationIndex]);
  const double sine = std::sin(entity.values[rotationIndex]);
  return {entity.values[0] + cosine * localX - sine * localY,
          entity.values[1] + sine * localX + cosine * localY};
}

PlainPoint ellipsePoint(const FlatEntity &entity, double parameter) {
  const double localX = entity.values[2] * std::cos(parameter);
  const double localY = entity.values[3] * std::sin(parameter);
  return rotatedPoint(entity, localX, localY, 4U);
}

PlainPoint hyperbolaPoint(const FlatEntity &entity, double parameter) {
  return rotatedPoint(entity, entity.values[2] * std::cosh(parameter),
                      entity.values[3] * std::sinh(parameter), 4U);
}

PlainPoint parabolaPoint(const FlatEntity &entity, double parameter) {
  return rotatedPoint(entity, parameter * parameter / (4.0 * entity.values[2]),
                      parameter, 3U);
}

NurbsView bsplineView(const FlatEntity &entity) {
  return {std::span{entity.values}.first(entity.controlPointCount * 2U),
          entity.knots,
          std::span{entity.values}.subspan(entity.controlPointCount * 2U),
          entity.degree};
}

PlainPoint bsplinePoint(const FlatEntity &entity, double parameter) {
  const NurbsPoint point = evaluateNurbs(bsplineView(entity), parameter);
  return {point.x, point.y};
}

double bsplineDistance(const FlatEntity &entity, PlainPoint query) {
  return std::sqrt(
      projectToNurbs(bsplineView(entity), {query.x, query.y}).squaredDistance);
}

Result<PlainPoint> point(const FlatEntity &entity, PointKey key) {
  switch (entity.kind) {
  case EntityKind::Point:
    if (key == PointKey::Point)
      return PlainPoint{entity.values[0], entity.values[1]};
    break;
  case EntityKind::Line:
    if (key == PointKey::Start)
      return PlainPoint{entity.values[0], entity.values[1]};
    if (key == PointKey::End)
      return PlainPoint{entity.values[2], entity.values[3]};
    break;
  case EntityKind::Circle:
    if (key == PointKey::Center)
      return PlainPoint{entity.values[0], entity.values[1]};
    break;
  case EntityKind::Arc:
    if (key == PointKey::Center)
      return PlainPoint{entity.values[0], entity.values[1]};
    if (key == PointKey::Start || key == PointKey::End) {
      const double angle = entity.values[key == PointKey::Start ? 3U : 4U];
      return PlainPoint{entity.values[0] + entity.values[2] * std::cos(angle),
                        entity.values[1] + entity.values[2] * std::sin(angle)};
    }
    break;
  case EntityKind::Ellipse:
    if (key == PointKey::Center)
      return PlainPoint{entity.values[0], entity.values[1]};
    if (key == PointKey::Major)
      return ellipsePoint(entity, 0.0);
    if (key == PointKey::Minor)
      return ellipsePoint(entity, std::numbers::pi / 2.0);
    break;
  case EntityKind::EllipticalArc:
    if (key == PointKey::Center)
      return PlainPoint{entity.values[0], entity.values[1]};
    if (key == PointKey::Major)
      return ellipsePoint(entity, 0.0);
    if (key == PointKey::Minor)
      return ellipsePoint(entity, std::numbers::pi / 2.0);
    if (key == PointKey::Start || key == PointKey::End)
      return ellipsePoint(entity,
                          entity.values[key == PointKey::Start ? 5U : 6U]);
    break;
  case EntityKind::HyperbolicArc:
    if (key == PointKey::Center)
      return PlainPoint{entity.values[0], entity.values[1]};
    if (key == PointKey::Major)
      return hyperbolaPoint(entity, 0.0);
    if (key == PointKey::Minor)
      return rotatedPoint(entity, entity.values[2], entity.values[3], 4U);
    if (key == PointKey::Focus)
      return rotatedPoint(
          entity, std::hypot(entity.values[2], entity.values[3]), 0.0, 4U);
    if (key == PointKey::Start || key == PointKey::End)
      return hyperbolaPoint(entity,
                            entity.values[key == PointKey::Start ? 5U : 6U]);
    break;
  case EntityKind::ParabolicArc:
    if (key == PointKey::Center)
      return PlainPoint{entity.values[0], entity.values[1]};
    if (key == PointKey::Focus)
      return rotatedPoint(entity, entity.values[2], 0.0, 3U);
    if (key == PointKey::Start || key == PointKey::End)
      return parabolaPoint(entity,
                           entity.values[key == PointKey::Start ? 4U : 5U]);
    break;
  case EntityKind::BSpline:
    if (key == PointKey::Start)
      return bsplinePoint(entity, entity.knots[entity.degree]);
    if (key == PointKey::End)
      return bsplinePoint(entity, entity.knots[entity.controlPointCount]);
    break;
  }
  return std::unexpected(diagnostic("sketch.reference.invalid-point-key",
                                    "point key is invalid for the entity"));
}

Result<PlainPoint> point(const std::vector<FlatEntity> &entities,
                         const EntityIndex &index, const PointRef &reference) {
  auto entity = find(entities, index, reference.entity);
  if (!entity)
    return std::unexpected(std::move(entity.error()));
  return point(**entity, reference.key);
}

bool isLine(const FlatEntity &entity) {
  return entity.kind == EntityKind::Line;
}
bool isRadial(const FlatEntity &entity) {
  return entity.kind == EntityKind::Circle || entity.kind == EntityKind::Arc;
}
bool isEllipse(const FlatEntity &entity) {
  return entity.kind == EntityKind::Ellipse ||
         entity.kind == EntityKind::EllipticalArc;
}
bool isHyperbola(const FlatEntity &entity) {
  return entity.kind == EntityKind::HyperbolicArc;
}
bool isParabola(const FlatEntity &entity) {
  return entity.kind == EntityKind::ParabolicArc;
}
bool isBSpline(const FlatEntity &entity) {
  return entity.kind == EntityKind::BSpline;
}
bool hasCenter(const FlatEntity &entity) {
  return isRadial(entity) || isEllipse(entity) || isHyperbola(entity);
}

PlainPoint start(const FlatEntity &line) {
  return {line.values[0], line.values[1]};
}
PlainPoint end(const FlatEntity &line) {
  return {line.values[2], line.values[3]};
}
PlainPoint center(const FlatEntity &curve) {
  return {curve.values[0], curve.values[1]};
}
double radius(const FlatEntity &curve) { return curve.values[2]; }
double dx(const FlatEntity &line) { return line.values[2] - line.values[0]; }
double dy(const FlatEntity &line) { return line.values[3] - line.values[1]; }
double length(const FlatEntity &line) { return std::hypot(dx(line), dy(line)); }

bool parameterWithin(double parameter, double first, double second) {
  const auto [minimum, maximum] = std::minmax(first, second);
  return parameter >= minimum && parameter <= maximum;
}

bool hyperbolaWithinCoordinateRange(const FlatEntity &entity,
                                    double maximumCoordinate) {
  const double first = entity.values[5];
  const double second = entity.values[6];
  std::array<double, 4> parameters{first, second, first, first};
  std::size_t count = 2U;
  const double cosine = std::cos(entity.values[4]);
  const double sine = std::sin(entity.values[4]);
  const auto addExtremum = [&](double sinhCoefficient, double coshCoefficient) {
    if (sinhCoefficient == 0.0 ||
        std::abs(coshCoefficient) >= std::abs(sinhCoefficient))
      return;
    const double parameter = std::atanh(-coshCoefficient / sinhCoefficient);
    if (parameterWithin(parameter, first, second))
      parameters[count++] = parameter;
  };
  addExtremum(cosine * entity.values[2], -sine * entity.values[3]);
  addExtremum(sine * entity.values[2], cosine * entity.values[3]);
  for (std::size_t index = 0U; index < count; ++index) {
    const PlainPoint candidate = hyperbolaPoint(entity, parameters[index]);
    if (!std::isfinite(candidate.x) || !std::isfinite(candidate.y) ||
        std::abs(candidate.x) > maximumCoordinate ||
        std::abs(candidate.y) > maximumCoordinate)
      return false;
  }
  return true;
}

bool parabolaWithinCoordinateRange(const FlatEntity &entity,
                                   double maximumCoordinate) {
  const double first = entity.values[4];
  const double second = entity.values[5];
  std::array<double, 4> parameters{first, second, first, first};
  std::size_t count = 2U;
  const double focalLength = entity.values[2];
  const double cosine = std::cos(entity.values[3]);
  const double sine = std::sin(entity.values[3]);
  const auto addExtremum = [&](double divisor, double numerator) {
    if (divisor == 0.0)
      return;
    const double parameter = 2.0 * focalLength * numerator / divisor;
    if (parameterWithin(parameter, first, second))
      parameters[count++] = parameter;
  };
  addExtremum(cosine, sine);
  addExtremum(sine, -cosine);
  for (std::size_t index = 0U; index < count; ++index) {
    const PlainPoint candidate = parabolaPoint(entity, parameters[index]);
    if (!std::isfinite(candidate.x) || !std::isfinite(candidate.y) ||
        std::abs(candidate.x) > maximumCoordinate ||
        std::abs(candidate.y) > maximumCoordinate)
      return false;
  }
  return true;
}

std::vector<PlainPoint> rigidPoints(const FlatEntity &entity) {
  switch (entity.kind) {
  case EntityKind::Point:
    return {{entity.values[0], entity.values[1]}};
  case EntityKind::Line:
    return {{entity.values[0], entity.values[1]},
            {entity.values[2], entity.values[3]}};
  case EntityKind::Circle:
    return {{entity.values[0], entity.values[1]}};
  case EntityKind::Arc:
    return {{entity.values[0], entity.values[1]},
            {entity.values[0] + entity.values[2] * std::cos(entity.values[3]),
             entity.values[1] + entity.values[2] * std::sin(entity.values[3])},
            {entity.values[0] + entity.values[2] * std::cos(entity.values[4]),
             entity.values[1] + entity.values[2] * std::sin(entity.values[4])}};
  case EntityKind::Ellipse:
    return {{entity.values[0], entity.values[1]},
            ellipsePoint(entity, 0.0),
            ellipsePoint(entity, std::numbers::pi / 2.0)};
  case EntityKind::EllipticalArc:
    return {{entity.values[0], entity.values[1]},
            ellipsePoint(entity, 0.0),
            ellipsePoint(entity, std::numbers::pi / 2.0),
            ellipsePoint(entity, entity.values[5]),
            ellipsePoint(entity, entity.values[6])};
  case EntityKind::HyperbolicArc:
    return {{entity.values[0], entity.values[1]},
            hyperbolaPoint(entity, 0.0),
            rotatedPoint(entity, entity.values[2], entity.values[3], 4U),
            hyperbolaPoint(entity, entity.values[5]),
            hyperbolaPoint(entity, entity.values[6])};
  case EntityKind::ParabolicArc:
    return {{entity.values[0], entity.values[1]},
            rotatedPoint(entity, entity.values[2], 0.0, 3U),
            parabolaPoint(entity, entity.values[4]),
            parabolaPoint(entity, entity.values[5])};
  case EntityKind::BSpline: {
    std::vector<PlainPoint> points;
    points.reserve(entity.controlPointCount);
    for (std::size_t index = 0U; index < entity.controlPointCount; ++index)
      points.push_back(
          {entity.values[index * 2U], entity.values[index * 2U + 1U]});
    return points;
  }
  }
  std::unreachable();
}

std::optional<std::pair<std::size_t, std::size_t>>
rigidAxis(std::span<const PlainPoint> points, double minimumLength) {
  for (std::size_t first = 0U; first < points.size(); ++first)
    for (std::size_t second = first + 1U; second < points.size(); ++second)
      if (std::hypot(points[second].x - points[first].x,
                     points[second].y - points[first].y) >= minimumLength)
        return std::pair{first, second};
  return std::nullopt;
}

Result<void> requireLine(const FlatEntity &entity) {
  if (!isLine(entity))
    return std::unexpected(diagnostic("sketch.constraint.requires-line",
                                      "constraint requires a line"));
  return {};
}

Result<void> requireRadial(const FlatEntity &entity) {
  if (!isRadial(entity))
    return std::unexpected(diagnostic("sketch.constraint.requires-radial",
                                      "constraint requires a circle or arc"));
  return {};
}

Result<void> requireCentered(const FlatEntity &entity) {
  if (!hasCenter(entity))
    return std::unexpected(
        diagnostic("sketch.constraint.requires-centered-curve",
                   "constraint requires a centered curve"));
  return {};
}

double cross(double ax, double ay, double bx, double by) {
  return ax * by - ay * bx;
}

double dot(double ax, double ay, double bx, double by) {
  return ax * bx + ay * by;
}

Result<std::vector<double>> residualsFor(
    const Constraint &constraint, const std::vector<FlatEntity> &current,
    const EntityIndex &currentIndex, const std::vector<FlatEntity> &reference,
    const EntityIndex &referenceIndex, const NumericalProfile &profile) {
  const double lengthTolerance = profile.lengthToleranceMeters;
  const double angleTolerance = profile.angleToleranceRadians;
  const auto entity = [&](SketchEntityId id) {
    return find(current, currentIndex, id);
  };
  const auto referenceEntity = [&](SketchEntityId id) {
    return find(reference, referenceIndex, id);
  };

  return std::visit(
      [&]<typename Value>(const Value &value) -> Result<std::vector<double>> {
        using Type = std::decay_t<Value>;
        if constexpr (std::is_same_v<Type, Coincident> ||
                      std::is_same_v<Type, Distance> ||
                      std::is_same_v<Type, HorizontalDistance> ||
                      std::is_same_v<Type, VerticalDistance>) {
          auto first = point(current, currentIndex, value.first);
          auto second = point(current, currentIndex, value.second);
          if (!first || !second)
            return std::unexpected(first ? std::move(second.error())
                                         : std::move(first.error()));
          const double deltaX = second->x - first->x;
          const double deltaY = second->y - first->y;
          if constexpr (std::is_same_v<Type, Coincident>) {
            return std::vector<double>{deltaX / lengthTolerance,
                                       deltaY / lengthTolerance};
          } else if constexpr (std::is_same_v<Type, Distance>) {
            return std::vector<double>{
                (std::hypot(deltaX, deltaY) - value.value.si()) /
                lengthTolerance};
          } else if constexpr (std::is_same_v<Type, HorizontalDistance>) {
            return std::vector<double>{(deltaX - value.value.si()) /
                                       lengthTolerance};
          } else {
            return std::vector<double>{(deltaY - value.value.si()) /
                                       lengthTolerance};
          }
        }
        if constexpr (std::is_same_v<Type, Horizontal> ||
                      std::is_same_v<Type, Vertical>) {
          auto line = entity(value.line);
          if (!line)
            return std::unexpected(std::move(line.error()));
          if (auto valid = requireLine(**line); !valid)
            return std::unexpected(std::move(valid.error()));
          if constexpr (std::is_same_v<Type, Horizontal>)
            return std::vector<double>{dy(**line) / lengthTolerance};
          return std::vector<double>{dx(**line) / lengthTolerance};
        }
        if constexpr (std::is_same_v<Type, Parallel> ||
                      std::is_same_v<Type, Perpendicular> ||
                      std::is_same_v<Type, Collinear> ||
                      std::is_same_v<Type, AngleBetween>) {
          auto first = entity(value.first);
          auto second = entity(value.second);
          if (!first || !second)
            return std::unexpected(first ? std::move(second.error())
                                         : std::move(first.error()));
          if (auto valid = requireLine(**first); !valid)
            return std::unexpected(std::move(valid.error()));
          if (auto valid = requireLine(**second); !valid)
            return std::unexpected(std::move(valid.error()));
          const double firstLength = length(**first);
          const double secondLength = length(**second);
          const double normalizedCross =
              cross(dx(**first), dy(**first), dx(**second), dy(**second)) /
              (firstLength * secondLength);
          const double normalizedDot =
              dot(dx(**first), dy(**first), dx(**second), dy(**second)) /
              (firstLength * secondLength);
          if constexpr (std::is_same_v<Type, Parallel>)
            return std::vector<double>{normalizedCross / angleTolerance};
          if constexpr (std::is_same_v<Type, Perpendicular>)
            return std::vector<double>{normalizedDot / angleTolerance};
          if constexpr (std::is_same_v<Type, AngleBetween>)
            return std::vector<double>{
                (normalizedCross - std::sin(value.value.si())) / angleTolerance,
                (normalizedDot - std::cos(value.value.si())) / angleTolerance};
          const PlainPoint offset{start(**second).x - start(**first).x,
                                  start(**second).y - start(**first).y};
          const double lineDistance =
              cross(dx(**first), dy(**first), offset.x, offset.y) / firstLength;
          return std::vector<double>{normalizedCross / angleTolerance,
                                     lineDistance / lengthTolerance};
        }
        if constexpr (std::is_same_v<Type, Tangent>) {
          auto first = entity(value.first);
          auto second = entity(value.second);
          if (!first || !second)
            return std::unexpected(first ? std::move(second.error())
                                         : std::move(first.error()));
          if (isLine(**second) && isRadial(**first))
            std::swap(first, second);
          if (isLine(**first) && isRadial(**second)) {
            const PlainPoint curveCenter = center(**second);
            const PlainPoint lineStart = start(**first);
            const double separation =
                std::abs(cross(dx(**first), dy(**first),
                               curveCenter.x - lineStart.x,
                               curveCenter.y - lineStart.y)) /
                length(**first);
            return std::vector<double>{(separation - radius(**second)) /
                                       lengthTolerance};
          }
          if (isRadial(**first) && isRadial(**second)) {
            const PlainPoint firstCenter = center(**first);
            const PlainPoint secondCenter = center(**second);
            const double separation = std::hypot(
                secondCenter.x - firstCenter.x, secondCenter.y - firstCenter.y);
            const double target =
                value.mode == Tangency::External
                    ? radius(**first) + radius(**second)
                    : std::abs(radius(**first) - radius(**second));
            return std::vector<double>{(separation - target) / lengthTolerance};
          }
          return std::unexpected(diagnostic(
              "sketch.constraint.invalid-tangent",
              "tangent requires a line and radial curve or two radial curves"));
        }
        if constexpr (std::is_same_v<Type, Concentric>) {
          auto first = entity(value.first);
          auto second = entity(value.second);
          if (!first || !second)
            return std::unexpected(first ? std::move(second.error())
                                         : std::move(first.error()));
          if (auto valid = requireCentered(**first); !valid)
            return std::unexpected(std::move(valid.error()));
          if (auto valid = requireCentered(**second); !valid)
            return std::unexpected(std::move(valid.error()));
          return std::vector<double>{
              (center(**second).x - center(**first).x) / lengthTolerance,
              (center(**second).y - center(**first).y) / lengthTolerance};
        }
        if constexpr (std::is_same_v<Type, Equal>) {
          auto first = entity(value.first);
          auto second = entity(value.second);
          if (!first || !second)
            return std::unexpected(first ? std::move(second.error())
                                         : std::move(first.error()));
          if (isLine(**first) && isLine(**second))
            return std::vector<double>{(length(**second) - length(**first)) /
                                       lengthTolerance};
          if (isRadial(**first) && isRadial(**second))
            return std::vector<double>{(radius(**second) - radius(**first)) /
                                       lengthTolerance};
          return std::unexpected(
              diagnostic("sketch.constraint.invalid-equal",
                         "equal requires two lines or two radial curves"));
        }
        if constexpr (std::is_same_v<Type, Midpoint>) {
          auto selected = point(current, currentIndex, value.point);
          auto line = entity(value.line);
          if (!selected || !line)
            return std::unexpected(selected ? std::move(line.error())
                                            : std::move(selected.error()));
          if (auto valid = requireLine(**line); !valid)
            return std::unexpected(std::move(valid.error()));
          return std::vector<double>{
              (selected->x - (start(**line).x + end(**line).x) * 0.5) /
                  lengthTolerance,
              (selected->y - (start(**line).y + end(**line).y) * 0.5) /
                  lengthTolerance};
        }
        if constexpr (std::is_same_v<Type, PointOnObject>) {
          auto selected = point(current, currentIndex, value.point);
          auto curve = entity(value.curve);
          if (!selected || !curve)
            return std::unexpected(selected ? std::move(curve.error())
                                            : std::move(selected.error()));
          if (isLine(**curve)) {
            const double separation =
                cross(dx(**curve), dy(**curve), selected->x - start(**curve).x,
                      selected->y - start(**curve).y) /
                length(**curve);
            return std::vector<double>{separation / lengthTolerance};
          }
          if (isRadial(**curve))
            return std::vector<double>{
                (std::hypot(selected->x - center(**curve).x,
                            selected->y - center(**curve).y) -
                 radius(**curve)) /
                lengthTolerance};
          if (isEllipse(**curve)) {
            const double cosine = std::cos((*curve)->values[4]);
            const double sine = std::sin((*curve)->values[4]);
            const double offsetX = selected->x - center(**curve).x;
            const double offsetY = selected->y - center(**curve).y;
            const double localX = cosine * offsetX + sine * offsetY;
            const double localY = -sine * offsetX + cosine * offsetY;
            const double normalized =
                std::pow(localX / (*curve)->values[2], 2.0) +
                std::pow(localY / (*curve)->values[3], 2.0) - 1.0;
            return std::vector<double>{
                normalized *
                std::min((*curve)->values[2], (*curve)->values[3]) /
                (2.0 * lengthTolerance)};
          }
          if (isHyperbola(**curve)) {
            const double cosine = std::cos((*curve)->values[4]);
            const double sine = std::sin((*curve)->values[4]);
            const double offsetX = selected->x - center(**curve).x;
            const double offsetY = selected->y - center(**curve).y;
            const double localX = cosine * offsetX + sine * offsetY;
            const double localY = -sine * offsetX + cosine * offsetY;
            const double normalized =
                std::pow(localX / (*curve)->values[2], 2.0) -
                std::pow(localY / (*curve)->values[3], 2.0) - 1.0;
            return std::vector<double>{
                normalized *
                std::min((*curve)->values[2], (*curve)->values[3]) /
                (2.0 * lengthTolerance)};
          }
          if (isParabola(**curve)) {
            const double cosine = std::cos((*curve)->values[3]);
            const double sine = std::sin((*curve)->values[3]);
            const double offsetX = selected->x - center(**curve).x;
            const double offsetY = selected->y - center(**curve).y;
            const double localX = cosine * offsetX + sine * offsetY;
            const double localY = -sine * offsetX + cosine * offsetY;
            const double implicit =
                localY * localY - 4.0 * (*curve)->values[2] * localX;
            const double scale =
                std::max(2.0 * (*curve)->values[2], std::abs(localY));
            return std::vector<double>{implicit / (scale * lengthTolerance)};
          }
          if (isBSpline(**curve))
            return std::vector<double>{bsplineDistance(**curve, *selected) /
                                       lengthTolerance};
          return std::unexpected(
              diagnostic("sketch.constraint.invalid-point-on-object",
                         "point-on-object requires a point and a curve"));
        }
        if constexpr (std::is_same_v<Type, Symmetric>) {
          auto first = point(current, currentIndex, value.first);
          auto second = point(current, currentIndex, value.second);
          auto axis = entity(value.axis);
          if (!first || !second || !axis) {
            if (!first)
              return std::unexpected(std::move(first.error()));
            if (!second)
              return std::unexpected(std::move(second.error()));
            return std::unexpected(std::move(axis.error()));
          }
          if (auto valid = requireLine(**axis); !valid)
            return std::unexpected(std::move(valid.error()));
          const double axisLength = length(**axis);
          const double axisX = dx(**axis);
          const double axisY = dy(**axis);
          const double midpointX = (first->x + second->x) * 0.5;
          const double midpointY = (first->y + second->y) * 0.5;
          return std::vector<double>{
              cross(axisX, axisY, midpointX - start(**axis).x,
                    midpointY - start(**axis).y) /
                  (axisLength * lengthTolerance),
              dot(axisX, axisY, second->x - first->x, second->y - first->y) /
                  (axisLength * lengthTolerance)};
        }
        if constexpr (std::is_same_v<Type, SymmetricAboutPoint>) {
          auto first = point(current, currentIndex, value.first);
          auto second = point(current, currentIndex, value.second);
          auto symmetryCenter = point(current, currentIndex, value.center);
          if (!first)
            return std::unexpected(std::move(first.error()));
          if (!second)
            return std::unexpected(std::move(second.error()));
          if (!symmetryCenter)
            return std::unexpected(std::move(symmetryCenter.error()));
          return std::vector<double>{
              ((first->x + second->x) * 0.5 - symmetryCenter->x) /
                  lengthTolerance,
              ((first->y + second->y) * 0.5 - symmetryCenter->y) /
                  lengthTolerance};
        }
        if constexpr (std::is_same_v<Type, Lock>) {
          auto selected = point(current, currentIndex, value.point);
          if (!selected)
            return std::unexpected(std::move(selected.error()));
          return std::vector<double>{
              (selected->x - value.position.x.si()) / lengthTolerance,
              (selected->y - value.position.y.si()) / lengthTolerance};
        }
        if constexpr (std::is_same_v<Type, Block>) {
          auto selected = entity(value.entity);
          auto anchor = referenceEntity(value.entity);
          if (!selected || !anchor)
            return std::unexpected(selected ? std::move(anchor.error())
                                            : std::move(selected.error()));
          if ((*selected)->kind != (*anchor)->kind)
            return std::unexpected(diagnostic(
                "sketch.solution.entity-kind-mismatch",
                "solution entity kind differs from its source entity"));
          std::vector<double> result;
          result.reserve((*selected)->values.size());
          for (std::size_t index = 0; index < (*selected)->values.size();
               ++index) {
            const bool angular =
                ((*selected)->kind == EntityKind::Arc && index >= 3) ||
                ((*selected)->kind == EntityKind::Ellipse && index == 4) ||
                ((*selected)->kind == EntityKind::EllipticalArc &&
                 index >= 4) ||
                ((*selected)->kind == EntityKind::HyperbolicArc &&
                 index >= 4) ||
                ((*selected)->kind == EntityKind::ParabolicArc && index == 3);
            result.push_back(
                ((*selected)->values[index] - (*anchor)->values[index]) /
                (angular ? angleTolerance : lengthTolerance));
          }
          return result;
        }
        if constexpr (std::is_same_v<Type, Group>) {
          if (value.entities.size() < 2U || value.entities.size() > 1'024U)
            return std::unexpected(diagnostic(
                "sketch.constraint.invalid-group",
                "group needs between two and 1024 distinct entities"));
          std::unordered_set<SketchEntityId, TypedIdHash<SketchEntityIdTag>>
              unique;
          std::vector<PlainPoint> currentPoints;
          std::vector<PlainPoint> referencePoints;
          std::vector<double> result;
          unique.reserve(value.entities.size());
          for (const SketchEntityId id : value.entities) {
            if (!unique.insert(id).second)
              return std::unexpected(diagnostic(
                  "sketch.constraint.invalid-group",
                  "group needs between two and 1024 distinct entities"));
            auto selected = entity(id);
            auto anchor = referenceEntity(id);
            if (!selected || !anchor)
              return std::unexpected(selected ? std::move(anchor.error())
                                              : std::move(selected.error()));
            if ((*selected)->kind != (*anchor)->kind)
              return std::unexpected(diagnostic(
                  "sketch.solution.entity-kind-mismatch",
                  "solution entity kind differs from its source entity"));
            auto selectedPoints = rigidPoints(**selected);
            auto anchorPoints = rigidPoints(**anchor);
            currentPoints.insert(currentPoints.end(), selectedPoints.begin(),
                                 selectedPoints.end());
            referencePoints.insert(referencePoints.end(), anchorPoints.begin(),
                                   anchorPoints.end());
            if ((*selected)->kind == EntityKind::Circle)
              result.push_back((radius(**selected) - radius(**anchor)) /
                               lengthTolerance);
          }
          const auto axis =
              rigidAxis(referencePoints, profile.minimumLengthMeters);
          if (!axis) {
            for (std::size_t index = 1U; index < referencePoints.size();
                 ++index) {
              result.push_back(
                  ((currentPoints[index].x - currentPoints[0].x) -
                   (referencePoints[index].x - referencePoints[0].x)) /
                  lengthTolerance);
              result.push_back(
                  ((currentPoints[index].y - currentPoints[0].y) -
                   (referencePoints[index].y - referencePoints[0].y)) /
                  lengthTolerance);
            }
            return result;
          }
          const auto [firstIndex, secondIndex] = *axis;
          const PlainPoint referenceAxis{
              referencePoints[secondIndex].x - referencePoints[firstIndex].x,
              referencePoints[secondIndex].y - referencePoints[firstIndex].y};
          const PlainPoint currentAxis{
              currentPoints[secondIndex].x - currentPoints[firstIndex].x,
              currentPoints[secondIndex].y - currentPoints[firstIndex].y};
          const double referenceLength =
              std::hypot(referenceAxis.x, referenceAxis.y);
          const double currentLength =
              std::max(std::hypot(currentAxis.x, currentAxis.y),
                       profile.minimumLengthMeters);
          result.push_back((currentLength - referenceLength) / lengthTolerance);
          for (std::size_t index = 0U; index < referencePoints.size();
               ++index) {
            if (index == firstIndex || index == secondIndex)
              continue;
            const PlainPoint referenceOffset{
                referencePoints[index].x - referencePoints[firstIndex].x,
                referencePoints[index].y - referencePoints[firstIndex].y};
            const PlainPoint currentOffset{
                currentPoints[index].x - currentPoints[firstIndex].x,
                currentPoints[index].y - currentPoints[firstIndex].y};
            result.push_back((dot(currentAxis.x, currentAxis.y, currentOffset.x,
                                  currentOffset.y) /
                                  currentLength -
                              dot(referenceAxis.x, referenceAxis.y,
                                  referenceOffset.x, referenceOffset.y) /
                                  referenceLength) /
                             lengthTolerance);
            result.push_back((cross(currentAxis.x, currentAxis.y,
                                    currentOffset.x, currentOffset.y) /
                                  currentLength -
                              cross(referenceAxis.x, referenceAxis.y,
                                    referenceOffset.x, referenceOffset.y) /
                                  referenceLength) /
                             lengthTolerance);
          }
          return result;
        }
        if constexpr (std::is_same_v<Type, Radius> ||
                      std::is_same_v<Type, Diameter>) {
          auto curve = entity(value.curve);
          if (!curve)
            return std::unexpected(std::move(curve.error()));
          if (auto valid = requireRadial(**curve); !valid)
            return std::unexpected(std::move(valid.error()));
          const double measured = std::is_same_v<Type, Diameter>
                                      ? radius(**curve) * 2.0
                                      : radius(**curve);
          return std::vector<double>{(measured - value.value.si()) /
                                     lengthTolerance};
        }
        return std::unexpected(diagnostic("sketch.constraint.unhandled",
                                          "constraint kind is not handled"));
      },
      constraint);
}

Result<void> validateProfile(const NumericalProfile &profile) {
  const std::array positive{
      profile.typicalLengthMeters,     profile.minimumLengthMeters,
      profile.maximumCoordinateMeters, profile.lengthToleranceMeters,
      profile.angleToleranceRadians,   profile.rankRelativeTolerance};
  if (!std::ranges::all_of(
          positive,
          [](double value) { return std::isfinite(value) && value > 0.0; }) ||
      profile.minimumLengthMeters >= profile.typicalLengthMeters ||
      profile.lengthToleranceMeters >= profile.typicalLengthMeters ||
      profile.maximumCoordinateMeters <= profile.typicalLengthMeters ||
      profile.angleToleranceRadians >= 1.0 ||
      profile.rankRelativeTolerance >= 1.0 || profile.maximumIterations == 0 ||
      profile.maximumIterations >
          static_cast<std::uint32_t>(std::numeric_limits<int>::max()))
    return std::unexpected(diagnostic("sketch.numerical.invalid-profile",
                                      "sketch numerical profile is invalid"));
  return {};
}

Result<void> validateGeometry(const FlatEntity &flat,
                              const NumericalProfile &profile,
                              std::string_view codePrefix,
                              std::string_view subject) {
  const auto fail = [&](std::string_view suffix, std::string_view reason) {
    return std::unexpected(
        diagnostic(std::string{codePrefix} + "." + std::string{suffix},
                   std::string{subject} + " " + std::string{reason}));
  };
  if (!std::ranges::all_of(flat.values,
                           [](double value) { return std::isfinite(value); }))
    return fail("non-finite", "is not finite");
  const std::size_t coordinateCount = flat.kind == EntityKind::Line ? 4U
                                      : flat.kind == EntityKind::BSpline
                                          ? flat.controlPointCount * 2U
                                          : 2U;
  for (std::size_t index = 0; index < coordinateCount; ++index) {
    if (std::abs(flat.values[index]) > profile.maximumCoordinateMeters)
      return fail("coordinate-range", "exceeds the supported coordinate range");
  }
  if (isLine(flat) && length(flat) < profile.minimumLengthMeters)
    return fail("degenerate-line", "contains a degenerate line");
  if (isRadial(flat) && (radius(flat) < profile.minimumLengthMeters ||
                         radius(flat) > profile.maximumCoordinateMeters))
    return fail("invalid-radius", "contains an unsupported radius");
  if (isEllipse(flat) && (flat.values[2] < profile.minimumLengthMeters ||
                          flat.values[2] > profile.maximumCoordinateMeters ||
                          flat.values[3] < profile.minimumLengthMeters ||
                          flat.values[3] > flat.values[2]))
    return fail("invalid-axis",
                "contains unsupported major or minor ellipse radii");
  if (isHyperbola(flat) && (flat.values[2] < profile.minimumLengthMeters ||
                            flat.values[2] > profile.maximumCoordinateMeters ||
                            flat.values[3] < profile.minimumLengthMeters ||
                            flat.values[3] > profile.maximumCoordinateMeters))
    return fail("invalid-axis", "contains unsupported hyperbola axis radii");
  if (isParabola(flat) && (flat.values[2] < profile.minimumLengthMeters ||
                           flat.values[2] > profile.maximumCoordinateMeters))
    return fail("invalid-focal-length",
                "contains an unsupported parabola focal length");
  if (flat.kind == EntityKind::Arc &&
      std::abs(flat.values[4] - flat.values[3]) < profile.angleToleranceRadians)
    return fail("degenerate-arc", "contains an arc with no angular span");
  if (flat.kind == EntityKind::Arc &&
      std::abs(flat.values[4] - flat.values[3]) > 2.0 * std::numbers::pi)
    return fail("unsupported-arc-span",
                "contains an arc spanning more than one turn");
  if (flat.kind == EntityKind::EllipticalArc &&
      std::abs(flat.values[6] - flat.values[5]) < profile.angleToleranceRadians)
    return fail("degenerate-elliptical-arc",
                "contains an elliptical arc with no parameter span");
  if (flat.kind == EntityKind::EllipticalArc &&
      std::abs(flat.values[6] - flat.values[5]) > 2.0 * std::numbers::pi)
    return fail("unsupported-elliptical-arc-span",
                "contains an elliptical arc spanning more than one turn");
  if (flat.kind == EntityKind::HyperbolicArc &&
      std::abs(flat.values[6] - flat.values[5]) < profile.angleToleranceRadians)
    return fail("degenerate-hyperbolic-arc",
                "contains a hyperbolic arc with no parameter span");
  if (flat.kind == EntityKind::ParabolicArc &&
      std::abs(flat.values[5] - flat.values[4]) < profile.minimumLengthMeters)
    return fail("degenerate-parabolic-arc",
                "contains a parabolic arc with no parameter span");
  if (flat.kind == EntityKind::HyperbolicArc &&
      !hyperbolaWithinCoordinateRange(flat, profile.maximumCoordinateMeters))
    return fail("coordinate-range",
                "extends beyond the supported coordinate range");
  if (flat.kind == EntityKind::ParabolicArc &&
      !parabolaWithinCoordinateRange(flat, profile.maximumCoordinateMeters))
    return fail("coordinate-range",
                "extends beyond the supported coordinate range");
  if (flat.kind == EntityKind::BSpline) {
    if (flat.controlPointCount < 2U || flat.controlPointCount > 1'024U ||
        flat.degree == 0U || flat.degree > 25U ||
        flat.degree >= flat.controlPointCount)
      return fail("invalid-bspline-degree",
                  "contains an unsupported B-spline degree or pole count");
    if (flat.knots.size() != flat.controlPointCount +
                                 static_cast<std::size_t>(flat.degree) + 1U ||
        !std::ranges::all_of(flat.knots,
                             [](double knot) { return std::isfinite(knot); }) ||
        !std::ranges::is_sorted(flat.knots) ||
        !(flat.knots[flat.degree] < flat.knots[flat.controlPointCount]))
      return fail("invalid-bspline-knots",
                  "contains an invalid B-spline knot sequence");
    if (flat.values.size() != flat.controlPointCount * 3U)
      return fail("invalid-bspline-weights",
                  "contains the wrong number of B-spline weights");
    for (std::size_t index = 0U; index < flat.controlPointCount; ++index) {
      const double weight = flat.values[flat.controlPointCount * 2U + index];
      if (weight < 1.0e-12 || weight > 1.0e12)
        return fail("invalid-bspline-weights",
                    "contains an unsupported B-spline weight");
    }
    bool distinct = false;
    for (std::size_t index = 1U; index < flat.controlPointCount; ++index)
      distinct = distinct ||
                 std::hypot(flat.values[index * 2U] - flat.values[0],
                            flat.values[index * 2U + 1U] - flat.values[1]) >=
                     profile.minimumLengthMeters;
    if (!distinct)
      return fail("degenerate-bspline", "contains a degenerate B-spline");
    if (flat.periodic) {
      if (periodicNurbsTailCount(bsplineView(flat), 0.0, 0.0) == 0U)
        return fail("invalid-periodic-bspline",
                    "does not contain a canonical periodic B-spline seam");
      const PlainPoint first = bsplinePoint(flat, flat.knots[flat.degree]);
      const PlainPoint last =
          bsplinePoint(flat, flat.knots[flat.controlPointCount]);
      if (std::hypot(last.x - first.x, last.y - first.y) >
          profile.lengthToleranceMeters)
        return fail("open-periodic-bspline",
                    "marks an open B-spline as periodic");
    }
  }
  return {};
}

} // namespace

SketchEntityId entityId(const Entity &entity) {
  return std::visit([](const auto &value) { return value.id; }, entity);
}

SketchConstraintId constraintId(const Constraint &constraint) {
  return std::visit([](const auto &value) { return value.id; }, constraint);
}

std::vector<SketchEntityId> constraintEntityIds(const Constraint &constraint) {
  return std::visit(
      []<typename Value>(const Value &value) {
        using Type = std::remove_cvref_t<Value>;
        if constexpr (std::is_same_v<Type, Coincident> ||
                      std::is_same_v<Type, Distance> ||
                      std::is_same_v<Type, HorizontalDistance> ||
                      std::is_same_v<Type, VerticalDistance>)
          return std::vector{value.first.entity, value.second.entity};
        if constexpr (std::is_same_v<Type, Horizontal> ||
                      std::is_same_v<Type, Vertical>)
          return std::vector{value.line};
        if constexpr (std::is_same_v<Type, Block>)
          return std::vector{value.entity};
        if constexpr (std::is_same_v<Type, Parallel> ||
                      std::is_same_v<Type, Perpendicular> ||
                      std::is_same_v<Type, Tangent> ||
                      std::is_same_v<Type, Concentric> ||
                      std::is_same_v<Type, Equal> ||
                      std::is_same_v<Type, Collinear> ||
                      std::is_same_v<Type, AngleBetween>)
          return std::vector{value.first, value.second};
        if constexpr (std::is_same_v<Type, Midpoint>)
          return std::vector{value.point.entity, value.line};
        if constexpr (std::is_same_v<Type, PointOnObject>)
          return std::vector{value.point.entity, value.curve};
        if constexpr (std::is_same_v<Type, Symmetric>)
          return std::vector{value.first.entity, value.second.entity,
                             value.axis};
        if constexpr (std::is_same_v<Type, SymmetricAboutPoint>)
          return std::vector{value.first.entity, value.second.entity,
                             value.center.entity};
        if constexpr (std::is_same_v<Type, Lock>)
          return std::vector{value.point.entity};
        if constexpr (std::is_same_v<Type, Group>)
          return value.entities;
        if constexpr (std::is_same_v<Type, Radius> ||
                      std::is_same_v<Type, Diameter>)
          return std::vector{value.curve};
      },
      constraint);
}

Result<Point2> resolvePoint(const Definition &definition, PointRef reference) {
  auto entities = flatten(definition.entities);
  if (!entities)
    return std::unexpected(std::move(entities.error()));
  auto resolved = point(*entities, indexOf(*entities), reference);
  if (!resolved)
    return std::unexpected(std::move(resolved.error()));
  auto x = LengthValue::fromSi(resolved->x);
  auto y = LengthValue::fromSi(resolved->y);
  if (!x)
    return std::unexpected(std::move(x.error()));
  if (!y)
    return std::unexpected(std::move(y.error()));
  return Point2{*x, *y};
}

std::size_t closedProfileCount(const Definition &definition) {
  std::map<PointRef, std::size_t> nodes;
  std::map<std::pair<double, double>, PointRef> coordinateNodes;
  std::vector<std::size_t> parents;
  std::vector<std::pair<PointRef, PointRef>> lineEdges;
  std::vector<std::pair<PointRef, PointRef>> coordinateEdges;
  std::size_t profiles = 0U;
  const auto appendCurve = [&](SketchEntityId id, Point2 start, Point2 end) {
    for (const auto &[key, coordinate] :
         {std::pair{PointKey::Start, start}, std::pair{PointKey::End, end}}) {
      const PointRef point{id, key};
      if (!nodes.contains(point)) {
        const std::size_t index = parents.size();
        nodes.emplace(point, index);
        parents.push_back(index);
      }
      const auto [same, inserted] = coordinateNodes.emplace(
          std::pair{coordinate.x.si(), coordinate.y.si()}, point);
      if (!inserted)
        coordinateEdges.emplace_back(same->second, point);
    }
    lineEdges.emplace_back(PointRef{id, PointKey::Start},
                           PointRef{id, PointKey::End});
  };
  for (const Entity &entity : definition.entities) {
    std::visit(
        [&](const auto &value) {
          using Type = std::remove_cvref_t<decltype(value)>;
          if (value.construction)
            return;
          if constexpr (std::is_same_v<Type, CircleEntity>) {
            ++profiles;
          } else if constexpr (std::is_same_v<Type, EllipseEntity>) {
            ++profiles;
          } else if constexpr (std::is_same_v<Type, LineEntity>) {
            appendCurve(value.id, value.start, value.end);
          } else if constexpr (std::is_same_v<Type, ArcEntity>) {
            const auto endpoint = [&](AngleValue angle) {
              return Point2{
                  LengthValue::fromSi(value.center.x.si() +
                                      value.radius.si() * std::cos(angle.si()))
                      .value(),
                  LengthValue::fromSi(value.center.y.si() +
                                      value.radius.si() * std::sin(angle.si()))
                      .value()};
            };
            appendCurve(value.id, endpoint(value.startAngle),
                        endpoint(value.endAngle));
          } else if constexpr (std::is_same_v<Type, EllipticalArcEntity>) {
            const double cosine = std::cos(value.rotation.si());
            const double sine = std::sin(value.rotation.si());
            const auto endpoint = [&](AngleValue parameter) {
              const double localX =
                  value.majorRadius.si() * std::cos(parameter.si());
              const double localY =
                  value.minorRadius.si() * std::sin(parameter.si());
              return Point2{LengthValue::fromSi(value.center.x.si() +
                                                cosine * localX - sine * localY)
                                .value(),
                            LengthValue::fromSi(value.center.y.si() +
                                                sine * localX + cosine * localY)
                                .value()};
            };
            appendCurve(value.id, endpoint(value.startParameter),
                        endpoint(value.endParameter));
          } else if constexpr (std::is_same_v<Type, HyperbolicArcEntity>) {
            const double cosine = std::cos(value.rotation.si());
            const double sine = std::sin(value.rotation.si());
            const auto endpoint = [&](DimensionlessValue parameter) {
              const double localX =
                  value.majorRadius.si() * std::cosh(parameter.si());
              const double localY =
                  value.minorRadius.si() * std::sinh(parameter.si());
              return Point2{LengthValue::fromSi(value.center.x.si() +
                                                cosine * localX - sine * localY)
                                .value(),
                            LengthValue::fromSi(value.center.y.si() +
                                                sine * localX + cosine * localY)
                                .value()};
            };
            appendCurve(value.id, endpoint(value.startParameter),
                        endpoint(value.endParameter));
          } else if constexpr (std::is_same_v<Type, ParabolicArcEntity>) {
            const double cosine = std::cos(value.rotation.si());
            const double sine = std::sin(value.rotation.si());
            const auto endpoint = [&](LengthValue parameter) {
              const double localX = parameter.si() * parameter.si() /
                                    (4.0 * value.focalLength.si());
              const double localY = parameter.si();
              return Point2{LengthValue::fromSi(value.vertex.x.si() +
                                                cosine * localX - sine * localY)
                                .value(),
                            LengthValue::fromSi(value.vertex.y.si() +
                                                sine * localX + cosine * localY)
                                .value()};
            };
            appendCurve(value.id, endpoint(value.startParameter),
                        endpoint(value.endParameter));
          } else if constexpr (std::is_same_v<Type, BSplineEntity>) {
            const FlatEntity flat = flatten(Entity{value});
            const auto endpoint = [&](double parameter) {
              const PlainPoint point = bsplinePoint(flat, parameter);
              return Point2{LengthValue::fromSi(point.x).value(),
                            LengthValue::fromSi(point.y).value()};
            };
            if (value.periodic)
              ++profiles;
            else
              appendCurve(value.id, endpoint(flat.knots[flat.degree]),
                          endpoint(flat.knots[flat.controlPointCount]));
          }
        },
        entity);
  }
  const auto root = [&parents](std::size_t index) {
    while (parents[index] != index) {
      parents[index] = parents[parents[index]];
      index = parents[index];
    }
    return index;
  };
  const auto join = [&nodes, &parents, &root](PointRef first, PointRef second) {
    const auto firstNode = nodes.find(first);
    const auto secondNode = nodes.find(second);
    if (firstNode == nodes.end() || secondNode == nodes.end())
      return;
    const std::size_t firstRoot = root(firstNode->second);
    const std::size_t secondRoot = root(secondNode->second);
    if (firstRoot != secondRoot)
      parents[secondRoot] = firstRoot;
  };
  for (const auto &[first, second] : coordinateEdges)
    join(first, second);
  for (const Constraint &constraint : definition.constraints) {
    const auto *coincident = std::get_if<Coincident>(&constraint);
    if (!coincident)
      continue;
    join(coincident->first, coincident->second);
  }
  std::set<std::size_t> vertices;
  for (std::size_t index = 0; index < parents.size(); ++index)
    vertices.insert(root(index));
  for (const auto &[firstPoint, secondPoint] : lineEdges) {
    const std::size_t firstRoot = root(nodes.at(firstPoint));
    const std::size_t secondRoot = root(nodes.at(secondPoint));
    if (firstRoot != secondRoot)
      parents[secondRoot] = firstRoot;
  }
  std::set<std::size_t> components;
  for (const auto &[point, index] : nodes) {
    static_cast<void>(point);
    components.insert(root(index));
  }
  if (lineEdges.size() + components.size() >= vertices.size())
    profiles += lineEdges.size() + components.size() - vertices.size();
  return profiles;
}

Result<void> validate(const Entity &entity, const NumericalProfile &profile) {
  if (auto valid = validateProfile(profile); !valid)
    return valid;
  return validateGeometry(flatten(entity), profile, "sketch.entity",
                          "sketch entity");
}

Result<void> validateConstraint(const Definition &definition,
                                const Constraint &constraint,
                                const NumericalProfile &profile) {
  if (auto valid = validateProfile(profile); !valid)
    return valid;
  auto flat = flatten(definition.entities);
  if (!flat)
    return std::unexpected(std::move(flat.error()));
  for (const FlatEntity &entity : *flat)
    if (auto valid = validateGeometry(entity, profile, "sketch.entity",
                                      "sketch entity");
        !valid)
      return valid;
  const EntityIndex index = indexOf(*flat);
  auto checked = residualsFor(constraint, *flat, index, *flat, index, profile);
  if (!checked)
    return std::unexpected(std::move(checked.error()));
  return {};
}

Result<void> validate(const Definition &definition,
                      const NumericalProfile &profile) {
  if (auto valid = validateProfile(profile); !valid)
    return valid;
  std::unordered_set<SketchEntityId, TypedIdHash<SketchEntityIdTag>> ids;
  ids.reserve(definition.entities.size());
  for (const Entity &entity : definition.entities) {
    const FlatEntity flat = flatten(entity);
    if (!ids.insert(flat.id).second)
      return std::unexpected(diagnostic("sketch.entity.duplicate-id",
                                        "sketch entity ID is duplicated"));
    if (auto valid =
            validateGeometry(flat, profile, "sketch.entity", "sketch entity");
        !valid)
      return valid;
  }

  std::unordered_set<SketchObjectId, TypedIdHash<SketchObjectIdTag>> objectIds;
  std::unordered_set<std::string_view> objectLabels;
  std::unordered_set<SketchEntityId, TypedIdHash<SketchEntityIdTag>> owned;
  objectIds.reserve(definition.objects.size());
  objectLabels.reserve(definition.objects.size());
  owned.reserve(definition.entities.size());
  constexpr std::array rectangleRoles{
      std::string_view{"bottom"}, std::string_view{"right"},
      std::string_view{"top"}, std::string_view{"left"}};
  constexpr std::array pointRoles{std::string_view{"point"}};
  constexpr std::array curveRoles{std::string_view{"curve"}};
  constexpr std::array slotRoles{
      std::string_view{"start_cap"}, std::string_view{"end_cap"},
      std::string_view{"top_side"}, std::string_view{"bottom_side"}};
  constexpr std::array arcSlotRoles{
      std::string_view{"outer"}, std::string_view{"end_cap"},
      std::string_view{"inner"}, std::string_view{"start_cap"}};
  for (const SketchObject &object : definition.objects) {
    if (!objectIds.insert(object.id).second)
      return std::unexpected(diagnostic("sketch.object.duplicate-id",
                                        "Sketch object ID is duplicated"));
    if (object.label.empty() || object.label.size() > 128U ||
        !validUtf8(object.label) ||
        std::ranges::all_of(object.label, [](unsigned char value) {
          return value <= 0x20U || value == 0x7fU;
        }))
      return std::unexpected(diagnostic("sketch.object.invalid-label",
                                        "Sketch object label is invalid"));
    if (!objectLabels.insert(object.label).second)
      return std::unexpected(diagnostic("sketch.object.duplicate-label",
                                        "Sketch object label is duplicated"));
    std::span<const std::string_view> expectedRoles;
    const auto memberIsCompatible = [&](const SketchObjectMember &member,
                                        const Entity &entity) {
      switch (object.kind) {
      case SketchObjectKind::Rectangle:
      case SketchObjectKind::Line:
      case SketchObjectKind::Polyline:
      case SketchObjectKind::RegularPolygon:
        return std::holds_alternative<LineEntity>(entity);
      case SketchObjectKind::Point:
        return std::holds_alternative<PointEntity>(entity);
      case SketchObjectKind::Circle:
        return std::holds_alternative<CircleEntity>(entity);
      case SketchObjectKind::Arc:
      case SketchObjectKind::Fillet:
        return std::holds_alternative<ArcEntity>(entity);
      case SketchObjectKind::Chamfer:
        return std::holds_alternative<LineEntity>(entity);
      case SketchObjectKind::Offset:
        return std::holds_alternative<LineEntity>(entity) ||
               std::holds_alternative<CircleEntity>(entity) ||
               std::holds_alternative<ArcEntity>(entity);
      case SketchObjectKind::CurveGroup:
        return !std::holds_alternative<PointEntity>(entity);
      case SketchObjectKind::JoinedCurve:
        return std::holds_alternative<BSplineEntity>(entity);
      case SketchObjectKind::Ellipse:
        return std::holds_alternative<EllipseEntity>(entity);
      case SketchObjectKind::EllipticalArc:
        return std::holds_alternative<EllipticalArcEntity>(entity);
      case SketchObjectKind::HyperbolicArc:
        return std::holds_alternative<HyperbolicArcEntity>(entity);
      case SketchObjectKind::ParabolicArc:
        return std::holds_alternative<ParabolicArcEntity>(entity);
      case SketchObjectKind::BSpline:
        return std::holds_alternative<BSplineEntity>(entity);
      case SketchObjectKind::Slot:
      case SketchObjectKind::Oblong:
        return member.role == "start_cap" || member.role == "end_cap"
                   ? std::holds_alternative<ArcEntity>(entity)
                   : std::holds_alternative<LineEntity>(entity);
      case SketchObjectKind::ArcSlot:
        return std::holds_alternative<ArcEntity>(entity);
      }
      return false;
    };
    switch (object.kind) {
    case SketchObjectKind::Rectangle:
      expectedRoles = rectangleRoles;
      break;
    case SketchObjectKind::Point:
      expectedRoles = pointRoles;
      break;
    case SketchObjectKind::Line:
    case SketchObjectKind::Circle:
    case SketchObjectKind::Arc:
    case SketchObjectKind::Fillet:
    case SketchObjectKind::Chamfer:
    case SketchObjectKind::Offset:
    case SketchObjectKind::JoinedCurve:
    case SketchObjectKind::Ellipse:
    case SketchObjectKind::EllipticalArc:
    case SketchObjectKind::HyperbolicArc:
    case SketchObjectKind::ParabolicArc:
    case SketchObjectKind::BSpline:
      expectedRoles = curveRoles;
      break;
    case SketchObjectKind::Slot:
    case SketchObjectKind::Oblong:
      expectedRoles = slotRoles;
      break;
    case SketchObjectKind::ArcSlot:
      expectedRoles = arcSlotRoles;
      break;
    case SketchObjectKind::Polyline:
    case SketchObjectKind::RegularPolygon:
    case SketchObjectKind::CurveGroup:
      break;
    }
    const bool dynamicRoles = object.kind == SketchObjectKind::Polyline ||
                              object.kind == SketchObjectKind::RegularPolygon ||
                              object.kind == SketchObjectKind::CurveGroup;
    if ((dynamicRoles && object.members.empty()) ||
        (object.kind == SketchObjectKind::RegularPolygon &&
         object.members.size() < 3U) ||
        (!dynamicRoles && (expectedRoles.empty() ||
                           object.members.size() != expectedRoles.size())))
      return std::unexpected(diagnostic("sketch.object.invalid-kind",
                                        "Sketch object kind is invalid"));
    std::unordered_set<std::string_view> roles;
    roles.reserve(object.members.size());
    for (std::size_t index = 0U; index < object.members.size(); ++index) {
      const SketchObjectMember &member = object.members[index];
      const std::string dynamicPrefix =
          object.kind == SketchObjectKind::RegularPolygon ? "side_"
                                                          : "segment_";
      const bool validRole =
          object.kind == SketchObjectKind::CurveGroup
              ? !member.role.empty() && member.role.size() <= 32U &&
                    std::ranges::all_of(member.role, [](unsigned char value) {
                      return (value >= 'a' && value <= 'z') ||
                             (value >= '0' && value <= '9') || value == '_';
                    })
              : dynamicRoles
              ? member.role == dynamicPrefix + std::to_string(index + 1U)
              : std::ranges::find(expectedRoles, member.role) !=
                    expectedRoles.end();
      if (!roles.insert(member.role).second || !validRole)
        return std::unexpected(diagnostic("sketch.object.invalid-role",
                                          "Sketch object role is invalid"));
      if (!owned.insert(member.entity).second)
        return std::unexpected(
            diagnostic("sketch.object.duplicate-member",
                       "Sketch entity belongs to more than one object"));
      const auto found =
          std::ranges::find(definition.entities, member.entity, entityId);
      if (found == definition.entities.end() ||
          !memberIsCompatible(member, *found))
        return std::unexpected(
            diagnostic("sketch.object.invalid-member",
                       "Sketch object member is missing or incompatible"));
    }
  }

  std::unordered_set<SketchConstraintId, TypedIdHash<SketchConstraintIdTag>>
      constraintIds;
  constraintIds.reserve(definition.constraints.size());
  for (const Constraint &constraint : definition.constraints) {
    if (!constraintIds.insert(constraintId(constraint)).second)
      return std::unexpected(diagnostic("sketch.constraint.duplicate-id",
                                        "sketch constraint ID is duplicated"));
    const auto invalidValue = std::visit(
        []<typename Value>(const Value &value) {
          using Type = std::decay_t<Value>;
          if constexpr (std::is_same_v<Type, Distance>)
            return value.value.si() < 0.0;
          if constexpr (std::is_same_v<Type, Radius> ||
                        std::is_same_v<Type, Diameter>)
            return value.value.si() <= 0.0;
          return false;
        },
        constraint);
    if (invalidValue)
      return std::unexpected(diagnostic("sketch.constraint.invalid-value",
                                        "constraint value is invalid"));
  }
  auto flat = flatten(definition.entities);
  const EntityIndex index = indexOf(*flat);
  for (const Constraint &constraint : definition.constraints) {
    auto checked =
        residualsFor(constraint, *flat, index, *flat, index, profile);
    if (!checked)
      return std::unexpected(std::move(checked.error()));
  }
  return {};
}

Result<std::vector<ConstraintResidual>>
evaluateResiduals(const Definition &definition,
                  const std::vector<Entity> &geometry,
                  const NumericalProfile &profile) {
  if (auto valid = validate(definition, profile); !valid)
    return std::unexpected(std::move(valid.error()));
  if (geometry.size() != definition.entities.size())
    return std::unexpected(diagnostic("sketch.solution.entity-count",
                                      "solution entity count is incorrect"));
  auto reference = flatten(definition.entities);
  auto current = flatten(geometry);
  const EntityIndex referenceIndex = indexOf(*reference);
  const EntityIndex currentIndex = indexOf(*current);
  if (currentIndex.size() != current->size())
    return std::unexpected(diagnostic("sketch.solution.duplicate-id",
                                      "solution entity ID is duplicated"));
  for (const FlatEntity &entity : *current) {
    if (auto valid = validateGeometry(entity, profile, "sketch.solution",
                                      "solution geometry");
        !valid)
      return std::unexpected(std::move(valid.error()));
  }
  for (const FlatEntity &entity : *reference) {
    auto solved = find(*current, currentIndex, entity.id);
    if (!solved)
      return std::unexpected(std::move(solved.error()));
    if ((*solved)->kind != entity.kind)
      return std::unexpected(
          diagnostic("sketch.solution.entity-kind-mismatch",
                     "solution entity kind differs from its source entity"));
  }

  std::vector<ConstraintResidual> result;
  result.reserve(definition.constraints.size());
  for (const Constraint &constraint : definition.constraints) {
    auto values = residualsFor(constraint, *current, currentIndex, *reference,
                               referenceIndex, profile);
    if (!values)
      return std::unexpected(std::move(values.error()));
    double maximum = 0.0;
    for (const double value : *values) {
      if (!std::isfinite(value))
        return std::unexpected(diagnostic("sketch.solution.non-finite-residual",
                                          "constraint residual is not finite"));
      maximum = std::max(maximum, std::abs(value));
    }
    result.push_back({constraintId(constraint), maximum, maximum <= 1.0});
  }
  return result;
}

} // namespace kearne::sketch
