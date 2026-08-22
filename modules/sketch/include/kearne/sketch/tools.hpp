#pragma once

#include <kearne/sketch/edit.hpp>

#include <array>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace kearne::sketch {

struct PrimitiveToolIds {
  SketchObjectId object;
  SketchEntityId entity;
};

struct PointToolInput {
  PrimitiveToolIds ids;
  Point2 point;
  bool construction = false;
};

struct LineToolInput {
  PrimitiveToolIds ids;
  Point2 start;
  Point2 end;
  bool construction = false;
};

struct CircleToolInput {
  PrimitiveToolIds ids;
  Point2 center;
  LengthValue radius;
  bool construction = false;
};

struct ArcToolInput {
  PrimitiveToolIds ids;
  Point2 center;
  LengthValue radius;
  AngleValue startAngle;
  AngleValue endAngle;
  bool construction = false;
};

struct EllipseToolInput {
  PrimitiveToolIds ids;
  Point2 center;
  LengthValue majorRadius;
  LengthValue minorRadius;
  AngleValue rotation;
  bool construction = false;
};

struct EllipticalArcToolInput {
  PrimitiveToolIds ids;
  Point2 center;
  LengthValue majorRadius;
  LengthValue minorRadius;
  AngleValue rotation;
  AngleValue startParameter;
  AngleValue endParameter;
  bool construction = false;
};

struct HyperbolicArcToolInput {
  PrimitiveToolIds ids;
  Point2 center;
  LengthValue majorRadius;
  LengthValue minorRadius;
  AngleValue rotation;
  DimensionlessValue startParameter;
  DimensionlessValue endParameter;
  bool construction = false;
};

struct ParabolicArcToolInput {
  PrimitiveToolIds ids;
  Point2 vertex;
  LengthValue focalLength;
  AngleValue rotation;
  LengthValue startParameter;
  LengthValue endParameter;
  bool construction = false;
};

struct BSplineToolInput {
  PrimitiveToolIds ids;
  std::vector<Point2> controlPoints;
  std::vector<DimensionlessValue> knots;
  std::vector<DimensionlessValue> weights;
  std::uint32_t degree = 3U;
  bool periodic = false;
  bool construction = false;
};

struct RectangleToolIds {
  SketchObjectId object;
  std::array<SketchEntityId, 4> edges;
  std::array<SketchConstraintId, 8> constraints;
};

struct RectangleToolInput {
  RectangleToolIds ids;
  Point2 firstCorner;
  Point2 oppositeCorner;
  bool construction = false;
};

struct SlotToolIds {
  SketchObjectId object;
  std::array<SketchEntityId, 4> curves;
  std::array<SketchConstraintId, 9> constraints;
};

struct SlotToolInput {
  SlotToolIds ids;
  Point2 startCenter;
  Point2 endCenter;
  LengthValue radius;
  bool construction = false;
  SketchObjectKind objectKind = SketchObjectKind::Slot;
};

struct ArcSlotToolIds {
  SketchObjectId object;
  std::array<SketchEntityId, 4> curves;
  std::array<SketchConstraintId, 10> constraints;
};

struct ArcSlotToolInput {
  ArcSlotToolIds ids;
  Point2 center;
  LengthValue centerlineRadius;
  AngleValue startAngle;
  AngleValue sweepAngle;
  LengthValue slotRadius;
  bool construction = false;
};

struct PolylineToolIds {
  SketchObjectId object;
  std::vector<SketchEntityId> segments;
  std::vector<SketchConstraintId> constraints;
};

struct PolylineToolInput {
  PolylineToolIds ids;
  std::vector<Point2> points;
  bool closed = false;
  bool construction = false;
};

struct RegularPolygonToolIds {
  SketchObjectId object;
  std::vector<SketchEntityId> sides;
  std::vector<SketchConstraintId> constraints;
};

struct RegularPolygonToolInput {
  RegularPolygonToolIds ids;
  Point2 center;
  Point2 vertex;
  std::size_t sideCount = 3U;
  bool construction = false;
};

using ToolInput =
    std::variant<PointToolInput, LineToolInput, CircleToolInput, ArcToolInput,
                 EllipseToolInput, EllipticalArcToolInput,
                 HyperbolicArcToolInput, ParabolicArcToolInput,
                 BSplineToolInput, RectangleToolInput, SlotToolInput,
                 ArcSlotToolInput, PolylineToolInput, RegularPolygonToolInput>;

[[nodiscard]] std::string nextSketchObjectLabel(const Definition &current,
                                                std::string_view prefix);
[[nodiscard]] std::string_view
sketchObjectLabelPrefix(SketchObjectKind kind) noexcept;
[[nodiscard]] SketchObject
curveGroupAfterPartialEdit(const SketchObject &object);

[[nodiscard]] Result<AppliedEdits>
applyTool(const Definition &current, const ToolInput &input,
          const NumericalProfile &profile = {});

} // namespace kearne::sketch
