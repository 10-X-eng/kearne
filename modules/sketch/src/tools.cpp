#include <kearne/sketch/tools.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <numbers>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace kearne::sketch {

std::string nextSketchObjectLabel(const Definition &current,
                                  std::string_view prefix) {
  std::vector<bool> used(current.objects.size() + 2U, false);
  used.front() = true;
  for (const SketchObject &object : current.objects) {
    if (!object.label.starts_with(prefix))
      continue;
    const std::string_view suffix =
        std::string_view{object.label}.substr(prefix.size());
    std::size_t index = 0U;
    const auto parsed =
        std::from_chars(suffix.data(), suffix.data() + suffix.size(), index);
    if (parsed.ec == std::errc{} &&
        parsed.ptr == suffix.data() + suffix.size() && index > 0U &&
        index < used.size())
      used[index] = true;
  }
  const auto available = std::ranges::find(used, false);
  return std::string{prefix} +
         std::to_string(static_cast<std::size_t>(available - used.begin()));
}

std::string_view sketchObjectLabelPrefix(SketchObjectKind kind) noexcept {
  switch (kind) {
  case SketchObjectKind::Rectangle:
    return "Rectangle ";
  case SketchObjectKind::Point:
    return "Point ";
  case SketchObjectKind::Line:
    return "Line ";
  case SketchObjectKind::Circle:
    return "Circle ";
  case SketchObjectKind::Arc:
    return "Arc ";
  case SketchObjectKind::Slot:
    return "Slot ";
  case SketchObjectKind::ArcSlot:
    return "Arc Slot ";
  case SketchObjectKind::Polyline:
    return "Polyline ";
  case SketchObjectKind::RegularPolygon:
    return "Polygon ";
  case SketchObjectKind::Oblong:
    return "Oblong ";
  case SketchObjectKind::Ellipse:
    return "Ellipse ";
  case SketchObjectKind::EllipticalArc:
    return "Elliptical Arc ";
  case SketchObjectKind::HyperbolicArc:
    return "Hyperbolic Arc ";
  case SketchObjectKind::ParabolicArc:
    return "Parabolic Arc ";
  case SketchObjectKind::BSpline:
    return "B-spline ";
  case SketchObjectKind::Fillet:
    return "Fillet ";
  case SketchObjectKind::Chamfer:
    return "Chamfer ";
  case SketchObjectKind::Offset:
    return "Offset ";
  case SketchObjectKind::JoinedCurve:
    return "Joined curve ";
  }
  return "Sketch object ";
}

namespace {

template <typename Entity>
std::vector<Edit>
compilePrimitive(const Definition &current, const PrimitiveToolIds &ids,
                 std::string_view labelPrefix, SketchObjectKind kind,
                 std::string_view role, Entity entity) {
  return {
      AppendEntity{std::move(entity)},
      AppendObject{SketchObject{ids.object,
                                nextSketchObjectLabel(current, labelPrefix),
                                kind,
                                {{std::string{role}, ids.entity}}}},
  };
}

std::vector<Edit> compile(const Definition &current,
                          const PointToolInput &input) {
  return compilePrimitive(
      current, input.ids, "Point ", SketchObjectKind::Point, "point",
      PointEntity{input.ids.entity, input.point, input.construction});
}

std::vector<Edit> compile(const Definition &current,
                          const LineToolInput &input) {
  return compilePrimitive(
      current, input.ids, "Line ", SketchObjectKind::Line, "curve",
      LineEntity{input.ids.entity, input.start, input.end, input.construction});
}

std::vector<Edit> compile(const Definition &current,
                          const CircleToolInput &input) {
  return compilePrimitive(current, input.ids, "Circle ",
                          SketchObjectKind::Circle, "curve",
                          CircleEntity{input.ids.entity, input.center,
                                       input.radius, input.construction});
}

std::vector<Edit> compile(const Definition &current,
                          const ArcToolInput &input) {
  return compilePrimitive(
      current, input.ids, "Arc ", SketchObjectKind::Arc, "curve",
      ArcEntity{input.ids.entity, input.center, input.radius, input.startAngle,
                input.endAngle, input.construction});
}

std::vector<Edit> compile(const Definition &current,
                          const EllipseToolInput &input) {
  return compilePrimitive(
      current, input.ids, "Ellipse ", SketchObjectKind::Ellipse, "curve",
      EllipseEntity{input.ids.entity, input.center, input.majorRadius,
                    input.minorRadius, input.rotation, input.construction});
}

std::vector<Edit> compile(const Definition &current,
                          const EllipticalArcToolInput &input) {
  return compilePrimitive(
      current, input.ids, "Elliptical Arc ", SketchObjectKind::EllipticalArc,
      "curve",
      EllipticalArcEntity{input.ids.entity, input.center, input.majorRadius,
                          input.minorRadius, input.rotation,
                          input.startParameter, input.endParameter,
                          input.construction});
}

std::vector<Edit> compile(const Definition &current,
                          const HyperbolicArcToolInput &input) {
  return compilePrimitive(
      current, input.ids, "Hyperbolic Arc ", SketchObjectKind::HyperbolicArc,
      "curve",
      HyperbolicArcEntity{input.ids.entity, input.center, input.majorRadius,
                          input.minorRadius, input.rotation,
                          input.startParameter, input.endParameter,
                          input.construction});
}

std::vector<Edit> compile(const Definition &current,
                          const ParabolicArcToolInput &input) {
  return compilePrimitive(
      current, input.ids, "Parabolic Arc ", SketchObjectKind::ParabolicArc,
      "curve",
      ParabolicArcEntity{input.ids.entity, input.vertex, input.focalLength,
                         input.rotation, input.startParameter,
                         input.endParameter, input.construction});
}

std::vector<Edit> compile(const Definition &current,
                          const BSplineToolInput &input) {
  return compilePrimitive(
      current, input.ids, "B-spline ", SketchObjectKind::BSpline, "curve",
      BSplineEntity{input.ids.entity, input.controlPoints, input.knots,
                    input.weights, input.degree, input.periodic,
                    input.construction});
}

std::vector<Edit> compile(const Definition &current,
                          const RectangleToolInput &input) {
  const Point2 second{input.oppositeCorner.x, input.firstCorner.y};
  const Point2 fourth{input.firstCorner.x, input.oppositeCorner.y};
  const std::array points{input.firstCorner, second, input.oppositeCorner,
                          fourth};
  std::vector<Edit> result;
  result.reserve(13);
  for (std::size_t index = 0; index < input.ids.edges.size(); ++index)
    result.push_back(AppendEntity{
        LineEntity{input.ids.edges[index], points[index],
                   points[(index + 1U) % 4U], input.construction}});
  for (std::size_t index = 0; index < input.ids.edges.size(); ++index)
    result.push_back(AppendConstraint{
        Coincident{input.ids.constraints[index],
                   {input.ids.edges[index], PointKey::End},
                   {input.ids.edges[(index + 1U) % 4U], PointKey::Start}}});
  result.push_back(AppendConstraint{
      Horizontal{input.ids.constraints[4], input.ids.edges[0]}});
  result.push_back(
      AppendConstraint{Vertical{input.ids.constraints[5], input.ids.edges[1]}});
  result.push_back(AppendConstraint{
      Horizontal{input.ids.constraints[6], input.ids.edges[2]}});
  result.push_back(
      AppendConstraint{Vertical{input.ids.constraints[7], input.ids.edges[3]}});
  const bool firstIsLeft = input.firstCorner.x < input.oppositeCorner.x;
  const bool firstIsBottom = input.firstCorner.y < input.oppositeCorner.y;
  const std::array members{
      SketchObjectMember{"bottom", input.ids.edges[firstIsBottom ? 0U : 2U]},
      SketchObjectMember{"right", input.ids.edges[firstIsLeft ? 1U : 3U]},
      SketchObjectMember{"top", input.ids.edges[firstIsBottom ? 2U : 0U]},
      SketchObjectMember{"left", input.ids.edges[firstIsLeft ? 3U : 1U]},
  };
  result.push_back(
      AppendObject{SketchObject{input.ids.object,
                                nextSketchObjectLabel(current, "Rectangle "),
                                SketchObjectKind::Rectangle,
                                {members.begin(), members.end()}}});
  return result;
}

Result<Point2> point(double x, double y) {
  auto xValue = LengthValue::fromSi(x);
  auto yValue = LengthValue::fromSi(y);
  if (!xValue || !yValue)
    return std::unexpected(
        diagnostic("sketch.tool.non-finite-point",
                   "Sketch tool produced a non-finite point"));
  return Point2{*xValue, *yValue};
}

Result<std::vector<Edit>> compile(const Definition &current,
                                  const SlotToolInput &input) {
  if (input.objectKind != SketchObjectKind::Slot &&
      input.objectKind != SketchObjectKind::Oblong)
    return std::unexpected(diagnostic("sketch.tool.slot-object-kind",
                                      "Slot tool object kind is unsupported",
                                      Severity::Fatal));
  const double dx = input.endCenter.x.si() - input.startCenter.x.si();
  const double dy = input.endCenter.y.si() - input.startCenter.y.si();
  const double length = std::hypot(dx, dy);
  if (!std::isfinite(length) || length == 0.0 || input.radius.si() <= 0.0)
    return std::unexpected(diagnostic("sketch.tool.degenerate-slot",
                                      "Slot axis and radius must be positive"));
  const double nx = -dy * input.radius.si() / length;
  const double ny = dx * input.radius.si() / length;
  auto startTop =
      point(input.startCenter.x.si() + nx, input.startCenter.y.si() + ny);
  auto startBottom =
      point(input.startCenter.x.si() - nx, input.startCenter.y.si() - ny);
  auto endTop = point(input.endCenter.x.si() + nx, input.endCenter.y.si() + ny);
  auto endBottom =
      point(input.endCenter.x.si() - nx, input.endCenter.y.si() - ny);
  auto startAngle =
      AngleValue::fromSi(std::atan2(dy, dx) + std::numbers::pi / 2.0);
  auto startEndAngle =
      AngleValue::fromSi(std::atan2(dy, dx) + 3.0 * std::numbers::pi / 2.0);
  auto endAngle =
      AngleValue::fromSi(std::atan2(dy, dx) + 5.0 * std::numbers::pi / 2.0);
  if (!startTop || !startBottom || !endTop || !endBottom || !startAngle ||
      !startEndAngle || !endAngle)
    return std::unexpected(diagnostic("sketch.tool.non-finite-slot",
                                      "Slot geometry is not finite"));

  const auto &curve = input.ids.curves;
  const auto &constraint = input.ids.constraints;
  std::vector<Edit> result{
      AppendEntity{ArcEntity{curve[0], input.startCenter, input.radius,
                             *startAngle, *startEndAngle, input.construction}},
      AppendEntity{ArcEntity{curve[1], input.endCenter, input.radius,
                             *startEndAngle, *endAngle, input.construction}},
      AppendEntity{
          LineEntity{curve[2], *startTop, *endTop, input.construction}},
      AppendEntity{
          LineEntity{curve[3], *startBottom, *endBottom, input.construction}},
      AppendConstraint{Coincident{constraint[0],
                                  {curve[0], PointKey::Start},
                                  {curve[2], PointKey::Start}}},
      AppendConstraint{Coincident{constraint[1],
                                  {curve[0], PointKey::End},
                                  {curve[3], PointKey::Start}}},
      AppendConstraint{Coincident{constraint[2],
                                  {curve[1], PointKey::Start},
                                  {curve[3], PointKey::End}}},
      AppendConstraint{Coincident{
          constraint[3], {curve[1], PointKey::End}, {curve[2], PointKey::End}}},
      AppendConstraint{Tangent{constraint[4], curve[2], curve[0]}},
      AppendConstraint{Tangent{constraint[5], curve[2], curve[1]}},
      AppendConstraint{Tangent{constraint[6], curve[3], curve[0]}},
      AppendConstraint{Tangent{constraint[7], curve[3], curve[1]}},
      AppendConstraint{Equal{constraint[8], curve[0], curve[1]}},
      AppendObject{SketchObject{
          input.ids.object,
          nextSketchObjectLabel(
              current, input.objectKind == SketchObjectKind::Oblong ? "Oblong "
                                                                    : "Slot "),
          input.objectKind,
          {{"start_cap", curve[0]},
           {"end_cap", curve[1]},
           {"top_side", curve[2]},
           {"bottom_side", curve[3]}}}},
  };
  return result;
}

Result<std::vector<Edit>> compile(const Definition &current,
                                  const ArcSlotToolInput &input) {
  const double centerlineRadius = input.centerlineRadius.si();
  const double slotRadius = input.slotRadius.si();
  const double sweep = input.sweepAngle.si();
  if (centerlineRadius <= 0.0 || slotRadius <= 0.0 ||
      slotRadius >= centerlineRadius || sweep == 0.0 ||
      std::abs(sweep) >= 2.0 * std::numbers::pi)
    return std::unexpected(diagnostic("sketch.tool.degenerate-arc-slot",
                                      "Arc Slot radii and sweep must define a "
                                      "non-self-intersecting profile"));
  const double direction = std::copysign(1.0, sweep);
  const double start = input.startAngle.si();
  const double end = start + sweep;
  auto startCenter =
      point(input.center.x.si() + centerlineRadius * std::cos(start),
            input.center.y.si() + centerlineRadius * std::sin(start));
  auto endCenter =
      point(input.center.x.si() + centerlineRadius * std::cos(end),
            input.center.y.si() + centerlineRadius * std::sin(end));
  auto outerRadius = LengthValue::fromSi(centerlineRadius + slotRadius);
  auto innerRadius = LengthValue::fromSi(centerlineRadius - slotRadius);
  auto endCapStart = AngleValue::fromSi(end);
  auto endCapEnd = AngleValue::fromSi(end + direction * std::numbers::pi);
  auto innerStart = AngleValue::fromSi(end);
  auto innerEnd = AngleValue::fromSi(start);
  auto startCapStart = AngleValue::fromSi(start + std::numbers::pi);
  auto startCapEnd = AngleValue::fromSi(start + std::numbers::pi +
                                        direction * std::numbers::pi);
  auto outerEnd = AngleValue::fromSi(end);
  if (!startCenter || !endCenter || !outerRadius || !innerRadius ||
      !endCapStart || !endCapEnd || !innerStart || !innerEnd ||
      !startCapStart || !startCapEnd || !outerEnd)
    return std::unexpected(diagnostic("sketch.tool.non-finite-arc-slot",
                                      "Arc Slot geometry is not finite"));

  const auto &curve = input.ids.curves;
  const auto &constraint = input.ids.constraints;
  std::vector<Edit> result{
      AppendEntity{ArcEntity{curve[0], input.center, *outerRadius,
                             input.startAngle, *outerEnd, input.construction}},
      AppendEntity{ArcEntity{curve[1], *endCenter, input.slotRadius,
                             *endCapStart, *endCapEnd, input.construction}},
      AppendEntity{ArcEntity{curve[2], input.center, *innerRadius, *innerStart,
                             *innerEnd, input.construction}},
      AppendEntity{ArcEntity{curve[3], *startCenter, input.slotRadius,
                             *startCapStart, *startCapEnd, input.construction}},
      AppendConstraint{Coincident{constraint[0],
                                  {curve[0], PointKey::End},
                                  {curve[1], PointKey::Start}}},
      AppendConstraint{Coincident{constraint[1],
                                  {curve[1], PointKey::End},
                                  {curve[2], PointKey::Start}}},
      AppendConstraint{Coincident{constraint[2],
                                  {curve[2], PointKey::End},
                                  {curve[3], PointKey::Start}}},
      AppendConstraint{Coincident{constraint[3],
                                  {curve[3], PointKey::End},
                                  {curve[0], PointKey::Start}}},
      AppendConstraint{
          Tangent{constraint[4], curve[0], curve[1], Tangency::Internal}},
      AppendConstraint{Tangent{constraint[5], curve[1], curve[2]}},
      AppendConstraint{Tangent{constraint[6], curve[2], curve[3]}},
      AppendConstraint{
          Tangent{constraint[7], curve[3], curve[0], Tangency::Internal}},
      AppendConstraint{Concentric{constraint[8], curve[0], curve[2]}},
      AppendConstraint{Equal{constraint[9], curve[1], curve[3]}},
      AppendObject{SketchObject{input.ids.object,
                                nextSketchObjectLabel(current, "Arc Slot "),
                                SketchObjectKind::ArcSlot,
                                {{"outer", curve[0]},
                                 {"end_cap", curve[1]},
                                 {"inner", curve[2]},
                                 {"start_cap", curve[3]}}}},
  };
  return result;
}

Result<std::vector<Edit>> compile(const Definition &current,
                                  const PolylineToolInput &input) {
  const std::size_t pointCount = input.points.size();
  if (pointCount < (input.closed ? 3U : 2U))
    return std::unexpected(
        diagnostic("sketch.tool.polyline-point-count",
                   "Polyline needs two points, or three points when closed"));
  const std::size_t segmentCount = input.closed ? pointCount : pointCount - 1U;
  const std::size_t constraintCount =
      input.closed ? segmentCount : segmentCount - 1U;
  if (input.ids.segments.size() != segmentCount ||
      input.ids.constraints.size() != constraintCount)
    return std::unexpected(
        diagnostic("sketch.tool.polyline-id-count",
                   "Polyline stable IDs do not match its segment topology",
                   Severity::Fatal));
  for (std::size_t index = 0U; index < segmentCount; ++index) {
    if (input.points[index] == input.points[(index + 1U) % pointCount])
      return std::unexpected(
          diagnostic("sketch.tool.degenerate-polyline-segment",
                     "Polyline consecutive points must differ"));
  }

  std::vector<Edit> result;
  result.reserve(segmentCount + constraintCount + 1U);
  for (std::size_t index = 0U; index < segmentCount; ++index)
    result.push_back(AppendEntity{LineEntity{
        input.ids.segments[index], input.points[index],
        input.points[(index + 1U) % pointCount], input.construction}});
  for (std::size_t index = 0U; index < constraintCount; ++index)
    result.push_back(AppendConstraint{Coincident{
        input.ids.constraints[index],
        {input.ids.segments[index], PointKey::End},
        {input.ids.segments[(index + 1U) % segmentCount], PointKey::Start}}});
  std::vector<SketchObjectMember> members;
  members.reserve(segmentCount);
  for (std::size_t index = 0U; index < segmentCount; ++index)
    members.push_back(
        {"segment_" + std::to_string(index + 1U), input.ids.segments[index]});
  result.push_back(AppendObject{SketchObject{
      input.ids.object, nextSketchObjectLabel(current, "Polyline "),
      SketchObjectKind::Polyline, std::move(members)}});
  return result;
}

Result<std::vector<Edit>> compile(const Definition &current,
                                  const RegularPolygonToolInput &input) {
  constexpr std::size_t minimumSides = 3U;
  constexpr std::size_t maximumSides = 128U;
  if (input.sideCount < minimumSides || input.sideCount > maximumSides)
    return std::unexpected(
        diagnostic("sketch.tool.polygon-side-count",
                   "Regular Polygon needs between 3 and 128 sides"));
  const std::size_t constraintCount = 3U * input.sideCount - 2U;
  if (input.ids.sides.size() != input.sideCount ||
      input.ids.constraints.size() != constraintCount)
    return std::unexpected(
        diagnostic("sketch.tool.polygon-id-count",
                   "Regular Polygon stable IDs do not match its topology",
                   Severity::Fatal));
  const double dx = input.vertex.x.si() - input.center.x.si();
  const double dy = input.vertex.y.si() - input.center.y.si();
  const double radius = std::hypot(dx, dy);
  if (!std::isfinite(radius) || radius == 0.0)
    return std::unexpected(
        diagnostic("sketch.tool.degenerate-polygon",
                   "Regular Polygon radius must be positive"));
  const double startAngle = std::atan2(dy, dx);
  const double turn =
      2.0 * std::numbers::pi / static_cast<double>(input.sideCount);
  std::vector<Point2> vertices;
  vertices.reserve(input.sideCount);
  for (std::size_t index = 0U; index < input.sideCount; ++index) {
    const double angle = startAngle + turn * static_cast<double>(index);
    auto vertex = point(input.center.x.si() + radius * std::cos(angle),
                        input.center.y.si() + radius * std::sin(angle));
    if (!vertex)
      return std::unexpected(std::move(vertex.error()));
    vertices.push_back(*vertex);
  }

  std::vector<Edit> result;
  result.reserve(input.sideCount + constraintCount + 1U);
  for (std::size_t index = 0U; index < input.sideCount; ++index)
    result.push_back(AppendEntity{LineEntity{
        input.ids.sides[index], vertices[index],
        vertices[(index + 1U) % input.sideCount], input.construction}});
  std::size_t constraint = 0U;
  for (std::size_t index = 0U; index < input.sideCount; ++index)
    result.push_back(AppendConstraint{Coincident{
        input.ids.constraints[constraint++],
        {input.ids.sides[index], PointKey::End},
        {input.ids.sides[(index + 1U) % input.sideCount], PointKey::Start}}});
  for (std::size_t index = 1U; index < input.sideCount; ++index)
    result.push_back(
        AppendConstraint{Equal{input.ids.constraints[constraint++],
                               input.ids.sides[0], input.ids.sides[index]}});
  auto angleValue = AngleValue::fromSi(turn);
  if (!angleValue)
    return std::unexpected(std::move(angleValue.error()));
  for (std::size_t index = 0U; index + 1U < input.sideCount; ++index)
    result.push_back(AppendConstraint{AngleBetween{
        input.ids.constraints[constraint++], input.ids.sides[index],
        input.ids.sides[index + 1U], *angleValue}});

  std::vector<SketchObjectMember> members;
  members.reserve(input.sideCount);
  for (std::size_t index = 0U; index < input.sideCount; ++index)
    members.push_back(
        {"side_" + std::to_string(index + 1U), input.ids.sides[index]});
  const std::string_view prefix = input.sideCount == 3U   ? "Triangle "
                                  : input.sideCount == 4U ? "Square "
                                                          : "Polygon ";
  result.push_back(AppendObject{
      SketchObject{input.ids.object, nextSketchObjectLabel(current, prefix),
                   SketchObjectKind::RegularPolygon, std::move(members)}});
  return result;
}

} // namespace

Result<AppliedEdits> applyTool(const Definition &current,
                               const ToolInput &input,
                               const NumericalProfile &profile) {
  return std::visit(
      [&](const auto &value) {
        auto edits = compile(current, value);
        if constexpr (std::is_same_v<decltype(edits),
                                     Result<std::vector<Edit>>>) {
          if (!edits)
            return Result<AppliedEdits>{
                std::unexpected(std::move(edits.error()))};
          return applyEdits(current, *edits, profile);
        } else {
          return applyEdits(current, edits, profile);
        }
      },
      input);
}

} // namespace kearne::sketch
