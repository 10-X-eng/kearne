#include <kearne/sketch/model.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <numbers>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

namespace kearne::sketch {
namespace {

enum class EntityKind { Point, Line, Circle, Arc };

struct FlatEntity {
  SketchEntityId id;
  EntityKind kind;
  std::vector<double> values;
};

using EntityIndex = std::unordered_map<SketchEntityId, std::size_t,
                                       TypedIdHash<SketchEntityIdTag>>;

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
        } else {
          return {value.id,
                  EntityKind::Arc,
                  {value.center.x.si(), value.center.y.si(), value.radius.si(),
                   value.startAngle.si(), value.endAngle.si()}};
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
  case EntityKind::Arc:
    if (key == PointKey::Center)
      return PlainPoint{entity.values[0], entity.values[1]};
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
          if (auto valid = requireRadial(**first); !valid)
            return std::unexpected(std::move(valid.error()));
          if (auto valid = requireRadial(**second); !valid)
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
        if constexpr (std::is_same_v<Type, Fixed>) {
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
                (*selected)->kind == EntityKind::Arc && index >= 3;
            result.push_back(
                ((*selected)->values[index] - (*anchor)->values[index]) /
                (angular ? angleTolerance : lengthTolerance));
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
  const std::size_t coordinateCount = flat.kind == EntityKind::Line ? 4U : 2U;
  for (std::size_t index = 0; index < coordinateCount; ++index) {
    if (std::abs(flat.values[index]) > profile.maximumCoordinateMeters)
      return fail("coordinate-range", "exceeds the supported coordinate range");
  }
  if (isLine(flat) && length(flat) < profile.minimumLengthMeters)
    return fail("degenerate-line", "contains a degenerate line");
  if (isRadial(flat) && (radius(flat) < profile.minimumLengthMeters ||
                         radius(flat) > profile.maximumCoordinateMeters))
    return fail("invalid-radius", "contains an unsupported radius");
  if (flat.kind == EntityKind::Arc &&
      std::abs(flat.values[4] - flat.values[3]) < profile.angleToleranceRadians)
    return fail("degenerate-arc", "contains an arc with no angular span");
  if (flat.kind == EntityKind::Arc &&
      std::abs(flat.values[4] - flat.values[3]) > 2.0 * std::numbers::pi)
    return fail("unsupported-arc-span",
                "contains an arc spanning more than one turn");
  return {};
}

} // namespace

SketchEntityId entityId(const Entity &entity) {
  return std::visit([](const auto &value) { return value.id; }, entity);
}

SketchConstraintId constraintId(const Constraint &constraint) {
  return std::visit([](const auto &value) { return value.id; }, constraint);
}

std::size_t closedProfileCount(const Definition &definition) {
  std::map<PointRef, std::size_t> nodes;
  std::map<std::pair<double, double>, PointRef> coordinateNodes;
  std::vector<std::size_t> parents;
  std::vector<std::pair<PointRef, PointRef>> lineEdges;
  std::vector<std::pair<PointRef, PointRef>> coordinateEdges;
  std::size_t profiles = 0U;
  for (const Entity &entity : definition.entities) {
    std::visit(
        [&](const auto &value) {
          using Type = std::remove_cvref_t<decltype(value)>;
          if (value.construction)
            return;
          if constexpr (std::is_same_v<Type, CircleEntity>) {
            ++profiles;
          } else if constexpr (std::is_same_v<Type, LineEntity>) {
            for (const PointKey key : {PointKey::Start, PointKey::End}) {
              const PointRef point{value.id, key};
              if (!nodes.contains(point)) {
                const std::size_t index = parents.size();
                nodes.emplace(point, index);
                parents.push_back(index);
              }
              const Point2 &coordinate =
                  key == PointKey::Start ? value.start : value.end;
              const auto [same, inserted] = coordinateNodes.emplace(
                  std::pair{coordinate.x.si(), coordinate.y.si()}, point);
              if (!inserted)
                coordinateEdges.emplace_back(same->second, point);
            }
            lineEdges.emplace_back(PointRef{value.id, PointKey::Start},
                                   PointRef{value.id, PointKey::End});
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
