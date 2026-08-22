#include <kearne/sketch/modify.hpp>
#include <kearne/sketch/tools.hpp>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <numbers>
#include <optional>
#include <set>
#include <string>
#include <utility>

namespace kearne::sketch {
namespace {

struct Vector2 {
  double x = 0.0;
  double y = 0.0;
};

Vector2 operator-(Point2 first, Point2 second) {
  return {first.x.si() - second.x.si(), first.y.si() - second.y.si()};
}

Vector2 operator+(Vector2 first, Vector2 second) {
  return {first.x + second.x, first.y + second.y};
}

Vector2 operator*(Vector2 value, double scale) {
  return {value.x * scale, value.y * scale};
}

double dot(Vector2 first, Vector2 second) {
  return first.x * second.x + first.y * second.y;
}

double cross(Vector2 first, Vector2 second) {
  return first.x * second.y - first.y * second.x;
}

double magnitude(Vector2 value) { return std::hypot(value.x, value.y); }

Result<Vector2> normalized(Vector2 value, const NumericalProfile &profile) {
  const double length = magnitude(value);
  if (!std::isfinite(length) || length < profile.minimumLengthMeters)
    return std::unexpected(diagnostic("sketch.modify.degenerate-direction",
                                      "Curve direction is degenerate"));
  return value * (1.0 / length);
}

Result<Point2> point(double x, double y, const NumericalProfile &profile) {
  if (!std::isfinite(x) || !std::isfinite(y) ||
      std::abs(x) > profile.maximumCoordinateMeters ||
      std::abs(y) > profile.maximumCoordinateMeters)
    return std::unexpected(diagnostic("sketch.modify.coordinate-range",
                                      "Curve edit produces an invalid point"));
  auto xValue = LengthValue::fromSi(x);
  auto yValue = LengthValue::fromSi(y);
  if (!xValue || !yValue)
    return std::unexpected(diagnostic("sketch.modify.coordinate-range",
                                      "Curve edit produces an invalid point"));
  return Point2{*xValue, *yValue};
}

Result<Point2> translated(Point2 origin, Vector2 offset,
                          const NumericalProfile &profile) {
  return point(origin.x.si() + offset.x, origin.y.si() + offset.y, profile);
}

const Entity *findEntity(const Definition &definition, SketchEntityId id) {
  const auto found = std::ranges::find(definition.entities, id, entityId);
  return found == definition.entities.end() ? nullptr : &*found;
}

Result<void> collectConstraintDeletes(const Definition &current,
                                      const std::set<SketchEntityId> &affected,
                                      ExternalConstraintPolicy policy,
                                      std::vector<Edit> &edits) {
  if (policy != ExternalConstraintPolicy::Refuse &&
      policy != ExternalConstraintPolicy::Detach)
    return std::unexpected(diagnostic("sketch.modify.constraint-policy",
                                      "Constraint policy is invalid"));
  for (const Constraint &constraint : current.constraints) {
    const auto references = constraintEntityIds(constraint);
    if (!std::ranges::any_of(references, [&](SketchEntityId id) {
          return affected.contains(id);
        }))
      continue;
    if (policy == ExternalConstraintPolicy::Refuse)
      return std::unexpected(
          diagnostic("sketch.modify.constrained-geometry",
                     "Selected geometry has constraints that must be removed"));
    edits.emplace_back(DeleteConstraint{constraintId(constraint)});
  }
  return {};
}

void preservePartialObjects(const Definition &current,
                            const std::set<SketchEntityId> &affected,
                            std::vector<Edit> &edits) {
  for (const SketchObject &object : current.objects)
    if (object.kind != SketchObjectKind::CurveGroup &&
        object.members.size() > 1U &&
        std::ranges::any_of(object.members, [&](const auto &member) {
          return affected.contains(member.entity);
        }))
      edits.emplace_back(
          ReplaceObject{curveGroupAfterPartialEdit(object)});
}

PointKey editedEndpoint(bool retainedStart) {
  return retainedStart ? PointKey::End : PointKey::Start;
}

struct RetainedRay {
  Point2 endpoint;
  Vector2 direction;
  bool retainedStart = true;
};

Result<RetainedRay> retainedRay(const LineEntity &line, Point2 intersection,
                                Point2 reference,
                                const NumericalProfile &profile) {
  const Vector2 startOffset = line.start - intersection;
  const Vector2 endOffset = line.end - intersection;
  const double startLength = magnitude(startOffset);
  const double endLength = magnitude(endOffset);
  const bool validStart = std::isfinite(startLength) &&
                          startLength >= profile.minimumLengthMeters;
  const bool validEnd = std::isfinite(endLength) &&
                        endLength >= profile.minimumLengthMeters;
  if (!validStart && !validEnd)
    return std::unexpected(diagnostic("sketch.modify.degenerate-direction",
                                      "Curve direction is degenerate"));

  bool retainStart = validStart;
  if (validStart && validEnd) {
    const Vector2 startDirection = startOffset * (1.0 / startLength);
    const Vector2 endDirection = endOffset * (1.0 / endLength);
    const Vector2 referenceOffset = reference - intersection;
    const double startScore = dot(startDirection, referenceOffset);
    const double endScore = dot(endDirection, referenceOffset);
    retainStart = std::abs(startScore - endScore) <=
                          profile.minimumLengthMeters
                      ? startLength >= endLength
                      : startScore > endScore;
  }
  const Point2 endpoint = retainStart ? line.start : line.end;
  const double endpointLength = retainStart ? startLength : endLength;
  const Vector2 direction =
      (retainStart ? startOffset : endOffset) * (1.0 / endpointLength);
  return RetainedRay{endpoint, direction, retainStart};
}

LineEntity trimmedLine(const LineEntity &source, const RetainedRay &ray,
                       Point2 trim) {
  LineEntity result = source;
  if (ray.retainedStart)
    result.end = trim;
  else
    result.start = trim;
  return result;
}

Result<LengthValue> length(double value) {
  auto result = LengthValue::fromSi(value);
  if (!result)
    return std::unexpected(diagnostic("sketch.modify.length-range",
                                      "Curve edit produces an invalid length"));
  return result;
}

Result<AngleValue> angle(double value) {
  auto result = AngleValue::fromSi(value);
  if (!result)
    return std::unexpected(diagnostic("sketch.modify.angle-range",
                                      "Curve edit produces an invalid angle"));
  return result;
}

bool isCurve(const Entity &entity) {
  return !std::holds_alternative<PointEntity>(entity);
}

bool construction(const Entity &entity) {
  return std::visit([](const auto &value) { return value.construction; },
                    entity);
}

std::string splitMemberRole(const SketchObject &object,
                            std::string_view sourceRole) {
  for (std::size_t part = 2U;; ++part) {
    const std::string suffix = "_part_" + std::to_string(part);
    const std::size_t baseLength = std::min(
        sourceRole.size(), static_cast<std::size_t>(32U - suffix.size()));
    std::string candidate{sourceRole.substr(0U, baseLength)};
    candidate += suffix;
    if (std::ranges::none_of(object.members, [&](const auto &member) {
          return member.role == candidate;
        }))
      return candidate;
  }
}

Result<AppliedEdits>
replaceCurveWithSegments(const Definition &current, CurvePick curve,
                         const std::vector<Entity> &segments,
                         const std::vector<Constraint> &appendedConstraints,
                         ExternalConstraintPolicy constraintPolicy,
                         const NumericalProfile &profile) {
  const Entity *source = findEntity(current, curve.entity);
  std::vector<Edit> edits;
  edits.reserve(current.constraints.size() + appendedConstraints.size() + 4U);
  if (auto detached = collectConstraintDeletes(
          current, {curve.entity}, constraintPolicy, edits);
      !detached)
    return std::unexpected(std::move(detached.error()));

  for (const SketchObject &owner : current.objects) {
    const auto member = std::ranges::find(
        owner.members, curve.entity, &SketchObjectMember::entity);
    if (member == owner.members.end())
      continue;
    if (segments.empty()) {
      SketchObject remainder = owner;
      remainder.members.erase(remainder.members.begin() +
                              std::distance(owner.members.begin(), member));
      if (remainder.members.empty())
        edits.emplace_back(DeleteObject{owner.id});
      else
        edits.emplace_back(
            ReplaceObject{curveGroupAfterPartialEdit(remainder)});
    } else if (segments.size() == 2U || owner.members.size() > 1U ||
               source->index() != segments.front().index()) {
      SketchObject replacement = curveGroupAfterPartialEdit(owner);
      if (segments.size() == 2U) {
        const auto insertion = replacement.members.begin() +
                               std::distance(owner.members.begin(), member) + 1;
        replacement.members.insert(
            insertion,
            {splitMemberRole(replacement, member->role),
             entityId(segments[1])});
      }
      if (replacement != owner)
        edits.emplace_back(ReplaceObject{std::move(replacement)});
    }
    break;
  }

  if (segments.empty()) {
    edits.emplace_back(DeleteEntity{curve.entity});
  } else {
    if (source->index() == segments.front().index())
      edits.emplace_back(ReplaceEntity{segments.front()});
    else
      edits.emplace_back(RetypeEntity{segments.front()});
    if (segments.size() == 2U)
      edits.emplace_back(AppendEntity{segments[1]});
  }
  for (const Constraint &constraint : appendedConstraints)
    edits.emplace_back(AppendConstraint{constraint});
  return applyEdits(current, edits, profile);
}

PointKey oppositeEndpoint(PointKey key) {
  return key == PointKey::Start ? PointKey::End : PointKey::Start;
}

bool sameEntityPair(SketchEntityId first, SketchEntityId second,
                    const JoinEdit &edit) {
  return (first == edit.first.entity && second == edit.second.entity) ||
         (first == edit.second.entity && second == edit.first.entity);
}

bool joinedSeam(const Coincident &constraint, const JoinEdit &edit) {
  return (constraint.first == edit.first && constraint.second == edit.second) ||
         (constraint.first == edit.second && constraint.second == edit.first);
}

bool remapJoinPoint(PointRef &point, const JoinEdit &edit) {
  if (point.entity != edit.first.entity && point.entity != edit.second.entity)
    return true;
  const PointRef selected = point.entity == edit.first.entity ? edit.first
                                                              : edit.second;
  if (point.key != oppositeEndpoint(selected.key))
    return false;
  point = {edit.joined.id, point.entity == edit.first.entity
                               ? PointKey::Start
                               : PointKey::End};
  return true;
}

std::optional<Constraint> remapJoinConstraint(const Constraint &constraint,
                                              const JoinEdit &edit) {
  if (const auto *coincident = std::get_if<Coincident>(&constraint);
      coincident && joinedSeam(*coincident, edit))
    return std::nullopt;
  if (const auto *tangent = std::get_if<Tangent>(&constraint);
      tangent && sameEntityPair(tangent->first, tangent->second, edit))
    return std::nullopt;

  Constraint replacement = constraint;
  const bool mapped = std::visit(
      [&]<typename Value>(Value &value) {
        using Type = std::decay_t<Value>;
        if constexpr (std::is_same_v<Type, Coincident>) {
          return remapJoinPoint(value.first, edit) &&
                 remapJoinPoint(value.second, edit);
        } else if constexpr (std::is_same_v<Type, PointOnObject>) {
          if (!remapJoinPoint(value.point, edit))
            return false;
          if (value.curve == edit.first.entity ||
              value.curve == edit.second.entity)
            value.curve = edit.joined.id;
          return true;
        } else if constexpr (std::is_same_v<Type, SymmetricAboutPoint>) {
          return remapJoinPoint(value.first, edit) &&
                 remapJoinPoint(value.second, edit) &&
                 remapJoinPoint(value.center, edit);
        } else if constexpr (std::is_same_v<Type, Lock>) {
          return remapJoinPoint(value.point, edit);
        } else if constexpr (std::is_same_v<Type, Distance> ||
                             std::is_same_v<Type, HorizontalDistance> ||
                             std::is_same_v<Type, VerticalDistance>) {
          return remapJoinPoint(value.first, edit) &&
                 remapJoinPoint(value.second, edit);
        } else if constexpr (std::is_same_v<Type, Group>) {
          for (SketchEntityId &entity : value.entities)
            if (entity == edit.first.entity || entity == edit.second.entity)
              entity = edit.joined.id;
          std::ranges::sort(value.entities);
          const auto unique = std::ranges::unique(value.entities);
          value.entities.erase(unique.begin(), unique.end());
          return value.entities.size() >= 2U;
        } else {
          const auto references = constraintEntityIds(Constraint{value});
          return std::ranges::none_of(references, [&](SketchEntityId entity) {
            return entity == edit.first.entity ||
                   entity == edit.second.entity;
          });
        }
      },
      replacement);
  return mapped ? std::optional<Constraint>{std::move(replacement)}
                : std::optional<Constraint>{};
}

} // namespace

Result<AppliedEdits> editLineCorner(const Definition &current,
                                    const CornerEdit &edit,
                                    const NumericalProfile &profile) {
  if (auto valid = validate(current, profile); !valid)
    return std::unexpected(std::move(valid.error()));
  if ((edit.kind != CornerEditKind::Fillet &&
       edit.kind != CornerEditKind::Chamfer) ||
      edit.first.entity == edit.second.entity ||
      !std::isfinite(edit.size.si()) ||
      edit.size.si() < profile.minimumLengthMeters ||
      edit.size.si() > profile.maximumCoordinateMeters)
    return std::unexpected(diagnostic("sketch.modify.corner-input",
                                      "Corner edit input is invalid"));
  const Entity *firstEntity = findEntity(current, edit.first.entity);
  const Entity *secondEntity = findEntity(current, edit.second.entity);
  const auto *first =
      firstEntity ? std::get_if<LineEntity>(firstEntity) : nullptr;
  const auto *second =
      secondEntity ? std::get_if<LineEntity>(secondEntity) : nullptr;
  if (!first || !second)
    return std::unexpected(diagnostic("sketch.modify.corner-lines",
                                      "Fillet and Chamfer require two lines"));

  const Vector2 firstDirection = first->end - first->start;
  const Vector2 secondDirection = second->end - second->start;
  const double denominator = cross(firstDirection, secondDirection);
  if (std::abs(denominator) <= profile.angleToleranceRadians *
                                   magnitude(firstDirection) *
                                   magnitude(secondDirection))
    return std::unexpected(diagnostic("sketch.modify.parallel-lines",
                                      "Parallel lines do not define a corner"));
  const Vector2 between = second->start - first->start;
  const double firstParameter = cross(between, secondDirection) / denominator;
  auto intersection =
      translated(first->start, firstDirection * firstParameter, profile);
  if (!intersection)
    return std::unexpected(std::move(intersection.error()));
  auto firstRay =
      retainedRay(*first, *intersection, edit.first.reference, profile);
  auto secondRay =
      retainedRay(*second, *intersection, edit.second.reference, profile);
  if (!firstRay)
    return std::unexpected(std::move(firstRay.error()));
  if (!secondRay)
    return std::unexpected(std::move(secondRay.error()));
  const double cosine =
      std::clamp(dot(firstRay->direction, secondRay->direction), -1.0, 1.0);
  const double cornerAngle = std::acos(cosine);
  if (cornerAngle <= profile.angleToleranceRadians ||
      std::numbers::pi - cornerAngle <= profile.angleToleranceRadians)
    return std::unexpected(diagnostic("sketch.modify.corner-angle",
                                      "Corner angle is unsupported"));
  const double setback = edit.kind == CornerEditKind::Fillet
                             ? edit.size.si() / std::tan(cornerAngle * 0.5)
                             : edit.size.si();
  if (!std::isfinite(setback) ||
      setback >= magnitude(firstRay->endpoint - *intersection) -
                     profile.minimumLengthMeters ||
      setback >= magnitude(secondRay->endpoint - *intersection) -
                     profile.minimumLengthMeters)
    return std::unexpected(diagnostic("sketch.modify.corner-size",
                                      "Corner size does not fit both lines"));
  auto firstTrim =
      translated(*intersection, firstRay->direction * setback, profile);
  auto secondTrim =
      translated(*intersection, secondRay->direction * setback, profile);
  if (!firstTrim)
    return std::unexpected(std::move(firstTrim.error()));
  if (!secondTrim)
    return std::unexpected(std::move(secondTrim.error()));

  std::vector<Edit> edits;
  edits.reserve(current.constraints.size() + 10U);
  const std::set affected{edit.first.entity, edit.second.entity};
  if (auto detached =
          collectConstraintDeletes(current, affected, edit.constraints, edits);
      !detached)
    return std::unexpected(std::move(detached.error()));
  preservePartialObjects(current, affected, edits);
  edits.emplace_back(ReplaceEntity{trimmedLine(*first, *firstRay, *firstTrim)});
  edits.emplace_back(
      ReplaceEntity{trimmedLine(*second, *secondRay, *secondTrim)});

  const PointRef firstPoint{first->id, editedEndpoint(firstRay->retainedStart)};
  const PointRef secondPoint{second->id,
                             editedEndpoint(secondRay->retainedStart)};
  if (edit.kind == CornerEditKind::Chamfer) {
    edits.emplace_back(
        AppendEntity{LineEntity{edit.output.curve, *firstTrim, *secondTrim,
                                first->construction && second->construction}});
    edits.emplace_back(
        AppendObject{SketchObject{edit.output.object,
                                  nextSketchObjectLabel(current, "Chamfer "),
                                  SketchObjectKind::Chamfer,
                                  {{"curve", edit.output.curve}}}});
    edits.emplace_back(
        AppendConstraint{Coincident{edit.output.constraints[0],
                                    firstPoint,
                                    {edit.output.curve, PointKey::Start}}});
    edits.emplace_back(
        AppendConstraint{Coincident{edit.output.constraints[1],
                                    {edit.output.curve, PointKey::End},
                                    secondPoint}});
  } else {
    auto bisector =
        normalized(firstRay->direction + secondRay->direction, profile);
    if (!bisector)
      return std::unexpected(std::move(bisector.error()));
    const double centerDistance = edit.size.si() / std::sin(cornerAngle * 0.5);
    auto center =
        translated(*intersection, *bisector * centerDistance, profile);
    if (!center)
      return std::unexpected(std::move(center.error()));
    const double start = std::atan2(firstTrim->y.si() - center->y.si(),
                                    firstTrim->x.si() - center->x.si());
    double sweep =
        std::remainder(std::atan2(secondTrim->y.si() - center->y.si(),
                                  secondTrim->x.si() - center->x.si()) -
                           start,
                       2.0 * std::numbers::pi);
    if (std::abs(sweep) > std::numbers::pi)
      sweep -= std::copysign(2.0 * std::numbers::pi, sweep);
    auto radius = length(edit.size.si());
    auto startAngle = angle(start);
    auto endAngle = angle(start + sweep);
    if (!radius || !startAngle || !endAngle)
      return std::unexpected(
          diagnostic("sketch.modify.fillet-geometry", "Fillet arc is invalid"));
    edits.emplace_back(AppendEntity{
        ArcEntity{edit.output.curve, *center, *radius, *startAngle, *endAngle,
                  first->construction && second->construction}});
    edits.emplace_back(
        AppendObject{SketchObject{edit.output.object,
                                  nextSketchObjectLabel(current, "Fillet "),
                                  SketchObjectKind::Fillet,
                                  {{"curve", edit.output.curve}}}});
    edits.emplace_back(
        AppendConstraint{Coincident{edit.output.constraints[0],
                                    firstPoint,
                                    {edit.output.curve, PointKey::Start}}});
    edits.emplace_back(
        AppendConstraint{Coincident{edit.output.constraints[1],
                                    {edit.output.curve, PointKey::End},
                                    secondPoint}});
    edits.emplace_back(
        AppendConstraint{Tangent{edit.output.constraints[2], first->id,
                                 edit.output.curve, Tangency::External}});
    edits.emplace_back(
        AppendConstraint{Tangent{edit.output.constraints[3], edit.output.curve,
                                 second->id, Tangency::External}});
    edits.emplace_back(AppendConstraint{
        Radius{edit.output.constraints[4], edit.output.curve, *radius}});
  }
  return applyEdits(current, edits, profile);
}

Result<AppliedEdits> offsetCurves(const Definition &current,
                                  const OffsetEdit &edit,
                                  const NumericalProfile &profile) {
  if (auto valid = validate(current, profile); !valid)
    return std::unexpected(std::move(valid.error()));
  if (edit.curves.empty() || edit.curves.size() > 1'024U ||
      edit.outputs.size() != edit.curves.size() ||
      (edit.sourceMode != OffsetSourceMode::Keep &&
       edit.sourceMode != OffsetSourceMode::Delete) ||
      !std::isfinite(edit.distance.si()) ||
      std::abs(edit.distance.si()) < profile.minimumLengthMeters ||
      std::abs(edit.distance.si()) > profile.maximumCoordinateMeters)
    return std::unexpected(
        diagnostic("sketch.offset.input", "Offset input is invalid"));
  const std::set<SketchEntityId> selected(edit.curves.begin(),
                                          edit.curves.end());
  if (selected.size() != edit.curves.size())
    return std::unexpected(diagnostic("sketch.offset.duplicate-selection",
                                      "Offset selection is duplicated"));

  std::vector<Edit> edits;
  edits.reserve(edit.curves.size() * 3U + current.constraints.size());
  if (edit.sourceMode == OffsetSourceMode::Delete) {
    if (auto detached = collectConstraintDeletes(current, selected,
                                                 edit.constraints, edits);
        !detached)
      return std::unexpected(std::move(detached.error()));
    for (const SketchObject &object : current.objects) {
      if (!std::ranges::any_of(object.members, [&](const auto &member) {
            return selected.contains(member.entity);
          }))
        continue;
      SketchObject remainder = curveGroupAfterPartialEdit(object);
      std::erase_if(remainder.members, [&](const auto &member) {
        return selected.contains(member.entity);
      });
      if (remainder.members.empty())
        edits.emplace_back(DeleteObject{object.id});
      else
        edits.emplace_back(ReplaceObject{std::move(remainder)});
    }
    for (SketchEntityId id : edit.curves)
      edits.emplace_back(DeleteEntity{id});
  }

  Definition labels = current;
  for (std::size_t index = 0U; index < edit.curves.size(); ++index) {
    const Entity *source = findEntity(current, edit.curves[index]);
    if (!source)
      return std::unexpected(
          diagnostic("sketch.offset.curve-missing", "Offset curve is missing"));
    std::optional<Entity> output;
    if (const auto *line = std::get_if<LineEntity>(source)) {
      auto direction = normalized(line->end - line->start, profile);
      if (!direction)
        return std::unexpected(std::move(direction.error()));
      const Vector2 shift{-direction->y * edit.distance.si(),
                          direction->x * edit.distance.si()};
      auto start = translated(line->start, shift, profile);
      auto end = translated(line->end, shift, profile);
      if (!start || !end)
        return std::unexpected(
            diagnostic("sketch.offset.line", "Line offset is invalid"));
      output = LineEntity{edit.outputs[index].curve, *start, *end,
                          line->construction};
    } else if (const auto *circle = std::get_if<CircleEntity>(source)) {
      auto radius = length(circle->radius.si() + edit.distance.si());
      if (!radius || radius->si() < profile.minimumLengthMeters)
        return std::unexpected(diagnostic("sketch.offset.radius",
                                          "Offset collapses the curve radius"));
      output = CircleEntity{edit.outputs[index].curve, circle->center, *radius,
                            circle->construction};
    } else if (const auto *arc = std::get_if<ArcEntity>(source)) {
      auto radius = length(arc->radius.si() + edit.distance.si());
      if (!radius || radius->si() < profile.minimumLengthMeters)
        return std::unexpected(diagnostic("sketch.offset.radius",
                                          "Offset collapses the curve radius"));
      output = ArcEntity{
          edit.outputs[index].curve, arc->center,   *radius,
          arc->startAngle,           arc->endAngle, arc->construction};
    } else {
      return std::unexpected(
          diagnostic("sketch.offset.curve-kind",
                     "Offset requires line, circle, or circular arc geometry"));
    }
    edits.emplace_back(AppendEntity{std::move(*output)});
    const std::string label = nextSketchObjectLabel(labels, "Offset ");
    SketchObject object{edit.outputs[index].object,
                        label,
                        SketchObjectKind::Offset,
                        {{"curve", edit.outputs[index].curve}}};
    edits.emplace_back(AppendObject{object});
    labels.objects.push_back(std::move(object));
  }
  return applyEdits(current, edits, profile);
}

Result<AppliedEdits> extendCurve(const Definition &current,
                                 const ExtendEdit &edit,
                                 const NumericalProfile &profile) {
  if (auto valid = validate(current, profile); !valid)
    return std::unexpected(std::move(valid.error()));
  const Entity *source = findEntity(current, edit.curve.entity);
  if (!source)
    return std::unexpected(
        diagnostic("sketch.extend.curve-missing", "Extend curve is missing"));

  std::optional<Entity> replacement;
  if (const auto *line = std::get_if<LineEntity>(source)) {
    const Vector2 axis = line->end - line->start;
    const double squaredLength = dot(axis, axis);
    if (!std::isfinite(squaredLength) ||
        squaredLength <
            profile.minimumLengthMeters * profile.minimumLengthMeters)
      return std::unexpected(diagnostic("sketch.extend.degenerate-line",
                                        "Extend line is degenerate"));
    const double startDistance = magnitude(edit.curve.reference - line->start);
    const double endDistance = magnitude(edit.curve.reference - line->end);
    const bool moveStart = startDistance <= endDistance;
    const double parameter =
        dot(edit.target - line->start, axis) / squaredLength;
    const double originalParameter = moveStart ? 0.0 : 1.0;
    if (!std::isfinite(parameter) ||
        std::abs(parameter - originalParameter) * std::sqrt(squaredLength) <
            profile.minimumLengthMeters ||
        (moveStart && parameter >= 1.0) || (!moveStart && parameter <= 0.0))
      return std::unexpected(
          diagnostic("sketch.extend.endpoint-switch",
                     "Target crosses the opposite endpoint"));
    auto target = translated(line->start, axis * parameter, profile);
    if (!target)
      return std::unexpected(std::move(target.error()));
    LineEntity result = *line;
    if (moveStart)
      result.start = *target;
    else
      result.end = *target;
    replacement = std::move(result);
  } else if (const auto *arc = std::get_if<ArcEntity>(source)) {
    const auto endpoint = [&](double parameter) {
      return point(arc->center.x.si() + arc->radius.si() * std::cos(parameter),
                   arc->center.y.si() + arc->radius.si() * std::sin(parameter),
                   profile);
    };
    auto start = endpoint(arc->startAngle.si());
    auto end = endpoint(arc->endAngle.si());
    if (!start || !end)
      return std::unexpected(
          diagnostic("sketch.extend.arc-endpoint", "Arc endpoint is invalid"));
    const bool moveStart = magnitude(edit.curve.reference - *start) <=
                           magnitude(edit.curve.reference - *end);
    if (magnitude(edit.target - arc->center) < profile.minimumLengthMeters)
      return std::unexpected(diagnostic("sketch.extend.arc-center",
                                        "Arc cannot extend toward its center"));
    const double targetAngle =
        std::atan2(edit.target.y.si() - arc->center.y.si(),
                   edit.target.x.si() - arc->center.x.si());
    const double endpointAngle =
        moveStart ? arc->startAngle.si() : arc->endAngle.si();
    const double unwrapped =
        endpointAngle +
        std::remainder(targetAngle - endpointAngle, 2.0 * std::numbers::pi);
    const double originalSpan = arc->endAngle.si() - arc->startAngle.si();
    const double resultStart = moveStart ? unwrapped : arc->startAngle.si();
    const double resultEnd = moveStart ? arc->endAngle.si() : unwrapped;
    const double resultSpan = resultEnd - resultStart;
    if (!std::isfinite(resultSpan) ||
        std::abs(resultSpan - originalSpan) < profile.angleToleranceRadians ||
        std::abs(resultSpan) < profile.angleToleranceRadians ||
        std::abs(resultSpan) > 2.0 * std::numbers::pi ||
        std::signbit(resultSpan) != std::signbit(originalSpan))
      return std::unexpected(
          diagnostic("sketch.extend.endpoint-switch",
                     "Target crosses the opposite endpoint"));
    auto startAngle = angle(resultStart);
    auto endAngle = angle(resultEnd);
    if (!startAngle || !endAngle)
      return std::unexpected(
          diagnostic("sketch.extend.arc-angle", "Arc extension is invalid"));
    ArcEntity result = *arc;
    result.startAngle = *startAngle;
    result.endAngle = *endAngle;
    replacement = std::move(result);
  } else {
    return std::unexpected(diagnostic(
        "sketch.extend.curve-kind", "Extend requires a line or circular arc"));
  }

  std::vector<Edit> edits;
  edits.reserve(current.constraints.size() + 1U);
  if (auto detached = collectConstraintDeletes(current, {edit.curve.entity},
                                               edit.constraints, edits);
      !detached)
    return std::unexpected(std::move(detached.error()));
  edits.emplace_back(ReplaceEntity{std::move(*replacement)});
  return applyEdits(current, edits, profile);
}

Result<AppliedEdits> trimCurve(const Definition &current, const TrimEdit &edit,
                               const NumericalProfile &profile) {
  if (auto valid = validate(current, profile); !valid)
    return std::unexpected(std::move(valid.error()));
  const Entity *source = findEntity(current, edit.curve.entity);
  if (!source)
    return std::unexpected(
        diagnostic("sketch.trim.curve-missing", "Trim curve is missing"));
  if (!isCurve(*source) || edit.retained.size() > 2U ||
      edit.boundaries.size() > 2U ||
      (edit.retained.empty() && !edit.boundaries.empty()) ||
      (edit.constraints != ExternalConstraintPolicy::Refuse &&
       edit.constraints != ExternalConstraintPolicy::Detach))
    return std::unexpected(
        diagnostic("sketch.trim.input", "Trim input is invalid"));
  if (!edit.retained.empty() &&
      (entityId(edit.retained.front()) != edit.curve.entity ||
       !isCurve(edit.retained.front())))
    return std::unexpected(diagnostic(
        "sketch.trim.primary-identity",
        "First retained segment must preserve the trimmed curve identity"));
  if (edit.retained.size() == 2U &&
      (entityId(edit.retained[1]) == edit.curve.entity ||
       !isCurve(edit.retained[1])))
    return std::unexpected(diagnostic(
        "sketch.trim.split-identity",
        "Second retained segment needs a distinct curve identity"));

  std::set<SketchConstraintId> boundaryIds;
  std::set<PointRef> boundaryPoints;
  for (const PointOnObject &boundary : edit.boundaries) {
    const bool retainedEndpoint =
        (boundary.point.key == PointKey::Start ||
         boundary.point.key == PointKey::End) &&
        std::ranges::any_of(edit.retained, [&](const Entity &entity) {
          return entityId(entity) == boundary.point.entity;
        });
    const Entity *cuttingCurve = findEntity(current, boundary.curve);
    if (!retainedEndpoint || boundary.curve == edit.curve.entity ||
        !cuttingCurve || !isCurve(*cuttingCurve) ||
        !boundaryIds.insert(boundary.id).second ||
        !boundaryPoints.insert(boundary.point).second)
      return std::unexpected(diagnostic(
          "sketch.trim.boundary", "Trim boundary constraint is invalid"));
  }

  std::vector<Constraint> boundaries;
  boundaries.reserve(edit.boundaries.size());
  for (const PointOnObject &boundary : edit.boundaries)
    boundaries.emplace_back(boundary);
  return replaceCurveWithSegments(current, edit.curve, edit.retained,
                                  boundaries, edit.constraints, profile);
}

Result<AppliedEdits> splitCurve(const Definition &current,
                                const SplitEdit &edit,
                                const NumericalProfile &profile) {
  if (auto valid = validate(current, profile); !valid)
    return std::unexpected(std::move(valid.error()));
  const Entity *source = findEntity(current, edit.curve.entity);
  if (!source)
    return std::unexpected(
        diagnostic("sketch.split.curve-missing", "Split curve is missing"));
  if (!isCurve(*source) || edit.segments.empty() || edit.segments.size() > 2U ||
      entityId(edit.segments.front()) != edit.curve.entity ||
      !isCurve(edit.segments.front()) ||
      (edit.segments.size() == 2U &&
       (entityId(edit.segments[1]) == edit.curve.entity ||
        !isCurve(edit.segments[1]))) ||
      edit.seam.first.entity != edit.curve.entity ||
      edit.seam.first.key != PointKey::End ||
      edit.seam.second.entity != entityId(edit.segments.back()) ||
      edit.seam.second.key != PointKey::Start ||
      (edit.constraints != ExternalConstraintPolicy::Refuse &&
       edit.constraints != ExternalConstraintPolicy::Detach))
    return std::unexpected(
        diagnostic("sketch.split.input", "Split input is invalid"));
  return replaceCurveWithSegments(current, edit.curve, edit.segments,
                                  {Constraint{edit.seam}}, edit.constraints,
                                  profile);
}

Result<AppliedEdits> joinCurves(const Definition &current,
                                const JoinEdit &edit,
                                const NumericalProfile &profile) {
  if (auto valid = validate(current, profile); !valid)
    return std::unexpected(std::move(valid.error()));
  const Entity *first = findEntity(current, edit.first.entity);
  const Entity *second = findEntity(current, edit.second.entity);
  const bool endpoints =
      (edit.first.key == PointKey::Start || edit.first.key == PointKey::End) &&
      (edit.second.key == PointKey::Start ||
       edit.second.key == PointKey::End);
  if (!first || !second || first == second || !isCurve(*first) ||
      !isCurve(*second) || !endpoints || edit.joined.id != edit.first.entity ||
      edit.joined.periodic || construction(*first) != edit.joined.construction ||
      construction(*second) != edit.joined.construction ||
      (edit.constraints != ExternalConstraintPolicy::Refuse &&
       edit.constraints != ExternalConstraintPolicy::Detach))
    return std::unexpected(
        diagnostic("sketch.join.input", "Join input is invalid"));
  auto firstSeam = resolvePoint(current, edit.first);
  auto secondSeam = resolvePoint(current, edit.second);
  auto firstFar =
      resolvePoint(current, {edit.first.entity, oppositeEndpoint(edit.first.key)});
  auto secondFar = resolvePoint(
      current, {edit.second.entity, oppositeEndpoint(edit.second.key)});
  if (!firstSeam || !secondSeam || !firstFar || !secondFar ||
      edit.joined.controlPoints.empty() ||
      magnitude(*firstSeam - *secondSeam) >
                        profile.lengthToleranceMeters ||
      magnitude(*firstFar - edit.joined.controlPoints.front()) >
          profile.lengthToleranceMeters ||
      magnitude(*secondFar - edit.joined.controlPoints.back()) >
          profile.lengthToleranceMeters)
    return std::unexpected(diagnostic(
        "sketch.join.geometry", "Joined curve does not preserve its endpoints"));

  std::vector<Edit> edits;
  edits.reserve(current.objects.size() + current.constraints.size() + 4U);
  for (const Constraint &constraint : current.constraints) {
    const auto references = constraintEntityIds(constraint);
    if (std::ranges::none_of(references, [&](SketchEntityId entity) {
          return entity == edit.first.entity || entity == edit.second.entity;
        }))
      continue;
    auto replacement = remapJoinConstraint(constraint, edit);
    if (!replacement) {
      const bool consumed =
          (std::holds_alternative<Coincident>(constraint) &&
           joinedSeam(std::get<Coincident>(constraint), edit)) ||
          (std::holds_alternative<Tangent>(constraint) &&
           sameEntityPair(std::get<Tangent>(constraint).first,
                          std::get<Tangent>(constraint).second, edit));
      if (!consumed && edit.constraints == ExternalConstraintPolicy::Refuse)
        return std::unexpected(diagnostic(
            "sketch.join.constrained-geometry",
            "Selected curves have constraints that cannot be preserved"));
      edits.emplace_back(DeleteConstraint{constraintId(constraint)});
    } else if (*replacement != constraint) {
      edits.emplace_back(ReplaceConstraint{std::move(*replacement)});
    }
  }

  const std::set selected{edit.first.entity, edit.second.entity};
  for (const SketchObject &owner : current.objects) {
    SketchObject remainder = owner;
    std::erase_if(remainder.members, [&](const SketchObjectMember &member) {
      return selected.contains(member.entity);
    });
    if (remainder.members.size() == owner.members.size())
      continue;
    if (remainder.members.empty())
      edits.emplace_back(DeleteObject{owner.id});
    else
      edits.emplace_back(
          ReplaceObject{curveGroupAfterPartialEdit(remainder)});
  }
  if (first->index() == Entity{edit.joined}.index())
    edits.emplace_back(ReplaceEntity{edit.joined});
  else
    edits.emplace_back(RetypeEntity{edit.joined});
  edits.emplace_back(DeleteEntity{edit.second.entity});
  edits.emplace_back(AppendObject{SketchObject{
      edit.object, nextSketchObjectLabel(current, "Joined curve "),
      SketchObjectKind::JoinedCurve, {{"curve", edit.joined.id}}}});
  return applyEdits(current, edits, profile);
}

Result<AppliedEdits> convertToNurbs(const Definition &current,
                                    const ConvertToNurbsEdit &edit,
                                    const NumericalProfile &profile) {
  if (auto valid = validate(current, profile); !valid)
    return std::unexpected(std::move(valid.error()));
  const Entity *source = findEntity(current, edit.curve.id);
  if (!source || !isCurve(*source) ||
      std::holds_alternative<BSplineEntity>(*source) ||
      construction(*source) != edit.curve.construction ||
      (edit.constraints != ExternalConstraintPolicy::Refuse &&
       edit.constraints != ExternalConstraintPolicy::Detach))
    return std::unexpected(diagnostic(
        "sketch.convert-to-nurbs.input", "NURBS conversion input is invalid"));
  if (auto valid = validate(Entity{edit.curve}, profile); !valid)
    return std::unexpected(std::move(valid.error()));

  std::vector<Edit> structuralEdits;
  structuralEdits.reserve(current.objects.size() + 1U);
  for (const SketchObject &owner : current.objects) {
    const auto member = std::ranges::find(
        owner.members, edit.curve.id, &SketchObjectMember::entity);
    if (member == owner.members.end())
      continue;
    SketchObject replacement = owner;
    if (owner.members.size() == 1U) {
      replacement.kind = SketchObjectKind::BSpline;
      replacement.label = nextSketchObjectLabel(current, "B-spline ");
    } else {
      replacement = curveGroupAfterPartialEdit(owner);
    }
    structuralEdits.emplace_back(ReplaceObject{std::move(replacement)});
  }
  structuralEdits.emplace_back(RetypeEntity{edit.curve});

  std::vector<const Constraint *> affected;
  affected.reserve(current.constraints.size());
  std::vector<Edit> detachedEdits = structuralEdits;
  detachedEdits.reserve(structuralEdits.size() + current.constraints.size());
  for (const Constraint &constraint : current.constraints) {
    const auto references = constraintEntityIds(constraint);
    if (std::ranges::find(references, edit.curve.id) == references.end())
      continue;
    affected.push_back(&constraint);
    detachedEdits.emplace_back(DeleteConstraint{constraintId(constraint)});
  }
  if (affected.empty())
    return applyEdits(current, structuralEdits, profile);

  auto detached = applyEdits(current, detachedEdits, profile);
  if (!detached)
    return std::unexpected(std::move(detached.error()));
  std::vector<Edit> finalEdits = structuralEdits;
  finalEdits.reserve(detachedEdits.size());
  for (const Constraint *constraint : affected) {
    if (validateConstraint(detached->target, *constraint, profile))
      continue;
    if (edit.constraints == ExternalConstraintPolicy::Refuse)
      return std::unexpected(diagnostic(
          "sketch.convert-to-nurbs.constrained-geometry",
          "Selected curve has constraints that cannot be preserved"));
    finalEdits.emplace_back(DeleteConstraint{constraintId(*constraint)});
  }
  return applyEdits(current, finalEdits, profile);
}

} // namespace kearne::sketch
