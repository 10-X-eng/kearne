#include <kearne/sketch/tools.hpp>

#include <vector>

namespace kearne::sketch {
namespace {

std::vector<Edit> compile(const PointToolInput &input) {
  return {AppendEntity{PointEntity{input.id, input.point, input.construction}}};
}

std::vector<Edit> compile(const LineToolInput &input) {
  return {AppendEntity{
      LineEntity{input.id, input.start, input.end, input.construction}}};
}

std::vector<Edit> compile(const CircleToolInput &input) {
  return {AppendEntity{
      CircleEntity{input.id, input.center, input.radius, input.construction}}};
}

std::vector<Edit> compile(const ArcToolInput &input) {
  return {AppendEntity{ArcEntity{input.id, input.center, input.radius,
                                 input.startAngle, input.endAngle,
                                 input.construction}}};
}

std::vector<Edit> compile(const RectangleToolInput &input) {
  const Point2 second{input.oppositeCorner.x, input.firstCorner.y};
  const Point2 fourth{input.firstCorner.x, input.oppositeCorner.y};
  const std::array points{input.firstCorner, second, input.oppositeCorner,
                          fourth};
  std::vector<Edit> result;
  result.reserve(12);
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
  return result;
}

} // namespace

Result<AppliedEdits> applyTool(const Definition &current,
                               const ToolInput &input,
                               const NumericalProfile &profile) {
  return std::visit(
      [&](const auto &value) {
        const std::vector<Edit> edits = compile(value);
        return applyEdits(current, edits, profile);
      },
      input);
}

} // namespace kearne::sketch
