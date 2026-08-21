#include <kearne/sketch/edit.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace kearne::sketch {
namespace {

constexpr std::size_t maximumSourceEditBatch = 65'536U;

template <typename Values, typename Id, typename GetId>
auto findById(Values &values, const Id &id, GetId getId) {
  return std::ranges::find(values, id, getId);
}

std::string targetKey(const Edit &edit) {
  return std::visit(
      []<typename Value>(const Value &value) {
        using Type = std::remove_cvref_t<Value>;
        if constexpr (std::is_same_v<Type, AppendObject> ||
                      std::is_same_v<Type, ReplaceObject>) {
          return "object:" + value.value.id.toString();
        } else if constexpr (std::is_same_v<Type, DeleteObject>) {
          return "object:" + value.id.toString();
        } else if constexpr (std::is_same_v<Type, AppendEntity> ||
                             std::is_same_v<Type, ReplaceEntity>) {
          return "entity:" + entityId(value.value).toString();
        } else if constexpr (std::is_same_v<Type, DeleteEntity>) {
          return "entity:" + value.id.toString();
        } else if constexpr (std::is_same_v<Type, AppendConstraint> ||
                             std::is_same_v<Type, ReplaceConstraint>) {
          return "constraint:" + constraintId(value.value).toString();
        } else {
          return "constraint:" + value.id.toString();
        }
      },
      edit);
}

template <typename Id>
SourceEditIntent intent(SourceEditAction action, SourceSection section, Id id) {
  return {action, section, id};
}

Result<SourceEditIntent> apply(Definition &target, const Edit &edit) {
  return std::visit(
      [&]<typename Value>(const Value &value) -> Result<SourceEditIntent> {
        using Type = std::remove_cvref_t<Value>;
        if constexpr (std::is_same_v<Type, AppendObject>) {
          const SketchObjectId id = value.value.id;
          if (findById(target.objects, id, &SketchObject::id) !=
              target.objects.end())
            return std::unexpected(diagnostic("sketch.edit.object-exists",
                                              "Sketch object already exists"));
          target.objects.push_back(value.value);
          return intent(SourceEditAction::Append, SourceSection::Objects, id);
        } else if constexpr (std::is_same_v<Type, ReplaceObject>) {
          const SketchObjectId id = value.value.id;
          auto found = findById(target.objects, id, &SketchObject::id);
          if (found == target.objects.end())
            return std::unexpected(diagnostic("sketch.edit.object-missing",
                                              "Sketch object is missing"));
          if (found->kind != value.value.kind)
            return std::unexpected(
                diagnostic("sketch.edit.object-kind",
                           "Sketch object replacement changes its kind"));
          *found = value.value;
          return intent(SourceEditAction::Replace, SourceSection::Objects, id);
        } else if constexpr (std::is_same_v<Type, DeleteObject>) {
          auto found = findById(target.objects, value.id, &SketchObject::id);
          if (found == target.objects.end())
            return std::unexpected(diagnostic("sketch.edit.object-missing",
                                              "Sketch object is missing"));
          target.objects.erase(found);
          return intent(SourceEditAction::Delete, SourceSection::Objects,
                        value.id);
        } else if constexpr (std::is_same_v<Type, AppendEntity>) {
          const SketchEntityId id = entityId(value.value);
          if (findById(target.entities, id, entityId) != target.entities.end())
            return std::unexpected(diagnostic("sketch.edit.entity-exists",
                                              "Sketch entity already exists"));
          target.entities.push_back(value.value);
          return intent(SourceEditAction::Append, SourceSection::Entities, id);
        } else if constexpr (std::is_same_v<Type, ReplaceEntity>) {
          const SketchEntityId id = entityId(value.value);
          auto found = findById(target.entities, id, entityId);
          if (found == target.entities.end())
            return std::unexpected(diagnostic("sketch.edit.entity-missing",
                                              "Sketch entity is missing"));
          if (found->index() != value.value.index())
            return std::unexpected(
                diagnostic("sketch.edit.entity-kind",
                           "Sketch entity replacement changes its kind"));
          *found = value.value;
          return intent(SourceEditAction::Replace, SourceSection::Entities, id);
        } else if constexpr (std::is_same_v<Type, DeleteEntity>) {
          auto found = findById(target.entities, value.id, entityId);
          if (found == target.entities.end())
            return std::unexpected(diagnostic("sketch.edit.entity-missing",
                                              "Sketch entity is missing"));
          target.entities.erase(found);
          return intent(SourceEditAction::Delete, SourceSection::Entities,
                        value.id);
        } else if constexpr (std::is_same_v<Type, AppendConstraint>) {
          const SketchConstraintId id = constraintId(value.value);
          if (findById(target.constraints, id, constraintId) !=
              target.constraints.end())
            return std::unexpected(
                diagnostic("sketch.edit.constraint-exists",
                           "Sketch constraint already exists"));
          target.constraints.push_back(value.value);
          return intent(SourceEditAction::Append, SourceSection::Constraints,
                        id);
        } else if constexpr (std::is_same_v<Type, ReplaceConstraint>) {
          const SketchConstraintId id = constraintId(value.value);
          auto found = findById(target.constraints, id, constraintId);
          if (found == target.constraints.end())
            return std::unexpected(diagnostic("sketch.edit.constraint-missing",
                                              "Sketch constraint is missing"));
          if (found->index() != value.value.index())
            return std::unexpected(
                diagnostic("sketch.edit.constraint-kind",
                           "Sketch constraint replacement changes its kind"));
          *found = value.value;
          return intent(SourceEditAction::Replace, SourceSection::Constraints,
                        id);
        } else {
          auto found = findById(target.constraints, value.id, constraintId);
          if (found == target.constraints.end())
            return std::unexpected(diagnostic("sketch.edit.constraint-missing",
                                              "Sketch constraint is missing"));
          target.constraints.erase(found);
          return intent(SourceEditAction::Delete, SourceSection::Constraints,
                        value.id);
        }
      },
      edit);
}

bool setEntityPoint(Entity &entity, PointKey key, Point2 point) {
  return std::visit(
      [key, point = std::move(point)](auto &value) {
        using Type = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::is_same_v<Type, PointEntity>) {
          if (key != PointKey::Point)
            return false;
          value.point = point;
          return true;
        } else if constexpr (std::is_same_v<Type, LineEntity>) {
          if (key == PointKey::Start)
            value.start = point;
          else if (key == PointKey::End)
            value.end = point;
          else
            return false;
          return true;
        } else if constexpr (std::is_same_v<Type, CircleEntity> ||
                             std::is_same_v<Type, ArcEntity> ||
                             std::is_same_v<Type, EllipseEntity> ||
                             std::is_same_v<Type, EllipticalArcEntity> ||
                             std::is_same_v<Type, HyperbolicArcEntity>) {
          if (key != PointKey::Center)
            return false;
          value.center = point;
          return true;
        } else if constexpr (std::is_same_v<Type, ParabolicArcEntity>) {
          if (key != PointKey::Center)
            return false;
          value.vertex = point;
          return true;
        } else if constexpr (std::is_same_v<Type, BSplineEntity>) {
          if (value.periodic || value.controlPoints.empty())
            return false;
          if (key == PointKey::Start)
            value.controlPoints.front() = point;
          else if (key == PointKey::End)
            value.controlPoints.back() = point;
          else
            return false;
          return true;
        } else
          return false;
      },
      entity);
}

Result<LengthValue> length(double metres) {
  return LengthValue::fromSi(metres);
}

} // namespace

Result<AppliedEdits> applyEdits(const Definition &current,
                                std::span<const Edit> edits,
                                const NumericalProfile &profile) {
  if (edits.empty() || edits.size() > maximumSourceEditBatch)
    return std::unexpected(
        diagnostic("sketch.edit.batch-size", "Sketch edit batch is invalid"));
  if (auto valid = validate(current, profile); !valid)
    return std::unexpected(std::move(valid.error()));

  std::unordered_set<std::string> targets;
  targets.reserve(edits.size());
  AppliedEdits result{current, {}};
  result.sourceEdits.reserve(edits.size());
  for (const Edit &edit : edits) {
    if (!targets.insert(targetKey(edit)).second)
      return std::unexpected(
          diagnostic("sketch.edit.duplicate-target",
                     "Sketch edit target is duplicated in the batch"));
    auto applied = apply(result.target, edit);
    if (!applied)
      return std::unexpected(std::move(applied.error()));
    result.sourceEdits.push_back(std::move(*applied));
  }
  if (auto valid = validate(result.target, profile); !valid)
    return std::unexpected(std::move(valid.error()));
  return result;
}

Result<AppliedEdits> toggleConstruction(const Definition &current,
                                        SketchEntityId entity,
                                        const NumericalProfile &profile) {
  const auto found = findById(current.entities, entity, entityId);
  if (found == current.entities.end())
    return std::unexpected(
        diagnostic("sketch.edit.entity-missing", "Sketch entity is missing"));
  Entity replacement = *found;
  std::visit([](auto &value) { value.construction = !value.construction; },
             replacement);
  const std::array<Edit, 1> edits{ReplaceEntity{std::move(replacement)}};
  return applyEdits(current, edits, profile);
}

Result<AppliedEdits> dragCurve(const Definition &current,
                               const CurveDragEdit &drag,
                               const NumericalProfile &profile) {
  const auto selected = findById(current.entities, drag.entity, entityId);
  if (selected == current.entities.end())
    return std::unexpected(
        diagnostic("sketch.edit.entity-missing", "Sketch entity is missing"));
  if (std::holds_alternative<CircleEntity>(*selected) ||
      std::holds_alternative<ArcEntity>(*selected)) {
    Entity replacement = *selected;
    const Point2 center = std::holds_alternative<CircleEntity>(replacement)
                              ? std::get<CircleEntity>(replacement).center
                              : std::get<ArcEntity>(replacement).center;
    auto radius = length(std::hypot(drag.current.x.si() - center.x.si(),
                                    drag.current.y.si() - center.y.si()));
    if (!radius || radius->si() <= profile.minimumLengthMeters)
      return std::unexpected(diagnostic("sketch.edit.drag-degenerate",
                                        "curve drag produced no radius"));
    std::visit(
        [&radius](auto &value) {
          using Type = std::remove_cvref_t<decltype(value)>;
          if constexpr (std::is_same_v<Type, CircleEntity> ||
                        std::is_same_v<Type, ArcEntity>)
            value.radius = *radius;
        },
        replacement);
    const std::array<Edit, 1> edits{ReplaceEntity{std::move(replacement)}};
    return applyEdits(current, edits, profile);
  }

  if (std::holds_alternative<EllipseEntity>(*selected) ||
      std::holds_alternative<EllipticalArcEntity>(*selected)) {
    Entity replacement = *selected;
    auto scaled = std::visit(
        [&drag, &profile](auto &value) -> Result<void> {
          using Type = std::remove_cvref_t<decltype(value)>;
          if constexpr (std::is_same_v<Type, EllipseEntity> ||
                        std::is_same_v<Type, EllipticalArcEntity>) {
            const double offsetX = drag.current.x.si() - value.center.x.si();
            const double offsetY = drag.current.y.si() - value.center.y.si();
            const double cosine = std::cos(value.rotation.si());
            const double sine = std::sin(value.rotation.si());
            const double localX = cosine * offsetX + sine * offsetY;
            const double localY = -sine * offsetX + cosine * offsetY;
            const double scale = std::hypot(localX / value.majorRadius.si(),
                                            localY / value.minorRadius.si());
            if (!std::isfinite(scale) ||
                value.minorRadius.si() * scale < profile.minimumLengthMeters)
              return std::unexpected(
                  diagnostic("sketch.edit.drag-degenerate",
                             "curve drag produced a degenerate ellipse"));
            auto major = length(value.majorRadius.si() * scale);
            auto minor = length(value.minorRadius.si() * scale);
            if (!major)
              return std::unexpected(std::move(major.error()));
            if (!minor)
              return std::unexpected(std::move(minor.error()));
            value.majorRadius = *major;
            value.minorRadius = *minor;
            return {};
          }
          return std::unexpected(diagnostic("sketch.edit.drag-not-curve",
                                            "Sketch entity is not a curve"));
        },
        replacement);
    if (!scaled)
      return std::unexpected(std::move(scaled.error()));
    const std::array<Edit, 1> edits{ReplaceEntity{std::move(replacement)}};
    return applyEdits(current, edits, profile);
  }

  if (std::holds_alternative<HyperbolicArcEntity>(*selected)) {
    auto replacement = std::get<HyperbolicArcEntity>(*selected);
    const double offsetX = drag.current.x.si() - replacement.center.x.si();
    const double offsetY = drag.current.y.si() - replacement.center.y.si();
    const double cosine = std::cos(replacement.rotation.si());
    const double sine = std::sin(replacement.rotation.si());
    const double localX = cosine * offsetX + sine * offsetY;
    const double localY = -sine * offsetX + cosine * offsetY;
    const double scaleSquared =
        std::pow(localX / replacement.majorRadius.si(), 2.0) -
        std::pow(localY / replacement.minorRadius.si(), 2.0);
    if (!std::isfinite(scaleSquared) || scaleSquared <= 0.0)
      return std::unexpected(
          diagnostic("sketch.edit.drag-degenerate",
                     "curve drag produced a degenerate hyperbola"));
    const double scale = std::sqrt(scaleSquared);
    if (!std::isfinite(scale) ||
        replacement.majorRadius.si() * scale < profile.minimumLengthMeters ||
        replacement.minorRadius.si() * scale < profile.minimumLengthMeters)
      return std::unexpected(
          diagnostic("sketch.edit.drag-degenerate",
                     "curve drag produced a degenerate hyperbola"));
    auto major = length(replacement.majorRadius.si() * scale);
    auto minor = length(replacement.minorRadius.si() * scale);
    if (!major)
      return std::unexpected(std::move(major.error()));
    if (!minor)
      return std::unexpected(std::move(minor.error()));
    replacement.majorRadius = *major;
    replacement.minorRadius = *minor;
    const std::array<Edit, 1> edits{ReplaceEntity{std::move(replacement)}};
    return applyEdits(current, edits, profile);
  }

  if (std::holds_alternative<ParabolicArcEntity>(*selected)) {
    auto replacement = std::get<ParabolicArcEntity>(*selected);
    const double offsetX = drag.current.x.si() - replacement.vertex.x.si();
    const double offsetY = drag.current.y.si() - replacement.vertex.y.si();
    const double cosine = std::cos(replacement.rotation.si());
    const double sine = std::sin(replacement.rotation.si());
    const double localX = cosine * offsetX + sine * offsetY;
    const double localY = -sine * offsetX + cosine * offsetY;
    const double scale =
        localY * localY / (4.0 * replacement.focalLength.si() * localX);
    if (!std::isfinite(scale) || localX <= 0.0 ||
        replacement.focalLength.si() * scale < profile.minimumLengthMeters)
      return std::unexpected(
          diagnostic("sketch.edit.drag-degenerate",
                     "curve drag produced a degenerate parabola"));
    auto focal = length(replacement.focalLength.si() * scale);
    auto start = length(replacement.startParameter.si() * scale);
    auto end = length(replacement.endParameter.si() * scale);
    if (!focal)
      return std::unexpected(std::move(focal.error()));
    if (!start)
      return std::unexpected(std::move(start.error()));
    if (!end)
      return std::unexpected(std::move(end.error()));
    replacement.focalLength = *focal;
    replacement.startParameter = *start;
    replacement.endParameter = *end;
    const std::array<Edit, 1> edits{ReplaceEntity{std::move(replacement)}};
    return applyEdits(current, edits, profile);
  }

  if (std::holds_alternative<BSplineEntity>(*selected)) {
    auto replacement = std::get<BSplineEntity>(*selected);
    const double deltaX = drag.current.x.si() - drag.first.x.si();
    const double deltaY = drag.current.y.si() - drag.first.y.si();
    for (Point2 &point : replacement.controlPoints) {
      auto x = length(point.x.si() + deltaX);
      auto y = length(point.y.si() + deltaY);
      if (!x || !y)
        return std::unexpected(diagnostic(
            "sketch.edit.drag-range", "curve drag exceeded coordinate range"));
      point = {*x, *y};
    }
    const std::array<Edit, 1> edits{ReplaceEntity{std::move(replacement)}};
    return applyEdits(current, edits, profile);
  }

  const auto *line = std::get_if<LineEntity>(&*selected);
  if (!line)
    return std::unexpected(diagnostic("sketch.edit.drag-not-curve",
                                      "Sketch entity is not a curve"));
  double deltaX = drag.current.x.si() - drag.first.x.si();
  double deltaY = drag.current.y.si() - drag.first.y.si();
  const bool horizontal = std::ranges::any_of(
      current.constraints, [&drag](const Constraint &value) {
        const auto *constraint = std::get_if<Horizontal>(&value);
        return constraint && constraint->line == drag.entity;
      });
  const bool vertical = std::ranges::any_of(
      current.constraints, [&drag](const Constraint &value) {
        const auto *constraint = std::get_if<Vertical>(&value);
        return constraint && constraint->line == drag.entity;
      });
  if (horizontal)
    deltaX = 0.0;
  if (vertical)
    deltaY = 0.0;
  const auto moved = [deltaX, deltaY](Point2 point) -> Result<Point2> {
    auto x = length(point.x.si() + deltaX);
    auto y = length(point.y.si() + deltaY);
    if (!x)
      return std::unexpected(std::move(x.error()));
    if (!y)
      return std::unexpected(std::move(y.error()));
    return Point2{*x, *y};
  };
  auto movedStart = moved(line->start);
  auto movedEnd = moved(line->end);
  if (!movedStart)
    return std::unexpected(std::move(movedStart.error()));
  if (!movedEnd)
    return std::unexpected(std::move(movedEnd.error()));

  std::map<SketchEntityId, const Entity *> entitiesById;
  for (const Entity &entity : current.entities)
    entitiesById.emplace(entityId(entity), &entity);
  std::map<PointRef, std::vector<PointRef>> graph;
  for (const Constraint &value : current.constraints) {
    const auto *coincident = std::get_if<Coincident>(&value);
    if (!coincident)
      continue;
    graph[coincident->first].push_back(coincident->second);
    graph[coincident->second].push_back(coincident->first);
  }
  std::map<SketchEntityId, Entity> replacements;
  const auto replacePoint = [&](const PointRef &reference, Point2 point) {
    auto replacement = replacements.find(reference.entity);
    if (replacement == replacements.end()) {
      const auto source = entitiesById.find(reference.entity);
      if (source == entitiesById.end())
        return false;
      replacement =
          replacements.emplace(reference.entity, *source->second).first;
    }
    return setEntityPoint(replacement->second, reference.key, std::move(point));
  };
  const auto moveComponent = [&](PointRef seed, Point2 point) {
    std::deque<PointRef> pending{seed};
    std::set<PointRef> visited;
    while (!pending.empty()) {
      const PointRef currentPoint = pending.front();
      pending.pop_front();
      if (!visited.insert(currentPoint).second ||
          !replacePoint(currentPoint, point))
        continue;
      const auto neighbors = graph.find(currentPoint);
      if (neighbors != graph.end())
        pending.insert(pending.end(), neighbors->second.begin(),
                       neighbors->second.end());
    }
  };
  moveComponent({drag.entity, PointKey::Start}, *movedStart);
  moveComponent({drag.entity, PointKey::End}, *movedEnd);
  std::vector<Edit> edits;
  edits.reserve(replacements.size());
  for (auto &[entity, replacement] : replacements) {
    static_cast<void>(entity);
    edits.push_back(ReplaceEntity{std::move(replacement)});
  }
  return applyEdits(current, edits, profile);
}

Result<AppliedEdits>
removeAxisAlignment(const Definition &current,
                    std::span<const SketchEntityId> entities,
                    const NumericalProfile &profile) {
  if (entities.empty() || entities.size() > maximumSourceEditBatch)
    return std::unexpected(diagnostic(
        "sketch.edit.axis-selection",
        "Remove axis alignment needs between one and 1024 selected lines"));
  std::unordered_set<SketchEntityId, TypedIdHash<SketchEntityIdTag>> selected;
  selected.reserve(entities.size());
  for (const SketchEntityId id : entities) {
    if (!selected.insert(id).second)
      return std::unexpected(
          diagnostic("sketch.edit.axis-selection",
                     "Remove axis alignment needs distinct selected lines"));
    const auto found = findById(current.entities, id, entityId);
    if (found == current.entities.end() ||
        !std::holds_alternative<LineEntity>(*found))
      return std::unexpected(
          diagnostic("sketch.edit.axis-selection",
                     "Remove axis alignment needs selected lines"));
  }
  std::vector<Edit> edits;
  for (const Constraint &constraint : current.constraints) {
    const SketchEntityId *line = std::visit(
        [](const auto &value) -> const SketchEntityId * {
          using Type = std::remove_cvref_t<decltype(value)>;
          if constexpr (std::is_same_v<Type, Horizontal> ||
                        std::is_same_v<Type, Vertical>)
            return &value.line;
          return nullptr;
        },
        constraint);
    if (line && selected.contains(*line))
      edits.push_back(DeleteConstraint{constraintId(constraint)});
  }
  if (edits.empty())
    return std::unexpected(diagnostic(
        "sketch.edit.axis-alignment-missing",
        "Selected lines have no horizontal or vertical constraints"));
  return applyEdits(current, edits, profile);
}

} // namespace kearne::sketch
