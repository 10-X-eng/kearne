#include <kearne/sketch/modify.hpp>
#include <kearne/sketch/tools.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>
#include <set>
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

void dissolvePartialObjects(const Definition &current,
                            const std::set<SketchEntityId> &affected,
                            std::vector<Edit> &edits) {
  for (const SketchObject &object : current.objects)
    if (object.members.size() > 1U &&
        std::ranges::any_of(object.members, [&](const auto &member) {
          return affected.contains(member.entity);
        }))
      edits.emplace_back(DeleteObject{object.id});
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
  const double startDistance = std::hypot(reference.x.si() - line.start.x.si(),
                                          reference.y.si() - line.start.y.si());
  const double endDistance = std::hypot(reference.x.si() - line.end.x.si(),
                                        reference.y.si() - line.end.y.si());
  const bool retainStart = startDistance <= endDistance;
  const Point2 endpoint = retainStart ? line.start : line.end;
  auto direction = normalized(endpoint - intersection, profile);
  if (!direction)
    return std::unexpected(std::move(direction.error()));
  return RetainedRay{endpoint, *direction, retainStart};
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
  dissolvePartialObjects(current, affected, edits);
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
    for (const SketchObject &object : current.objects)
      if (std::ranges::any_of(object.members, [&](const auto &member) {
            return selected.contains(member.entity);
          }))
        edits.emplace_back(DeleteObject{object.id});
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

} // namespace kearne::sketch
