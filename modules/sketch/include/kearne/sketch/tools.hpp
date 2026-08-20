#pragma once

#include <kearne/sketch/edit.hpp>

#include <array>
#include <variant>

namespace kearne::sketch {

struct PointToolInput {
  SketchEntityId id;
  Point2 point;
  bool construction = false;
};

struct LineToolInput {
  SketchEntityId id;
  Point2 start;
  Point2 end;
  bool construction = false;
};

struct CircleToolInput {
  SketchEntityId id;
  Point2 center;
  LengthValue radius;
  bool construction = false;
};

struct ArcToolInput {
  SketchEntityId id;
  Point2 center;
  LengthValue radius;
  AngleValue startAngle;
  AngleValue endAngle;
  bool construction = false;
};

struct RectangleToolIds {
  std::array<SketchEntityId, 4> edges;
  std::array<SketchConstraintId, 8> constraints;
};

struct RectangleToolInput {
  RectangleToolIds ids;
  Point2 firstCorner;
  Point2 oppositeCorner;
  bool construction = false;
};

using ToolInput = std::variant<PointToolInput, LineToolInput, CircleToolInput,
                               ArcToolInput, RectangleToolInput>;

[[nodiscard]] Result<AppliedEdits>
applyTool(const Definition &current, const ToolInput &input,
          const NumericalProfile &profile = {});

} // namespace kearne::sketch
