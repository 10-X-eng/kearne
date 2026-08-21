#pragma once

#include "sketch_tool_gesture.hpp"

#include <vector>

namespace kearne::ui::test {

inline std::vector<LocalSketchToolPoint>
definingSketchToolPoints(LocalSketchToolKind kind) {
  switch (kind) {
  case LocalSketchToolKind::Point:
    return {{0.0, 0.0}};
  case LocalSketchToolKind::Line:
  case LocalSketchToolKind::Circle:
  case LocalSketchToolKind::Rectangle:
  case LocalSketchToolKind::CenterRectangle:
    return {{0.0, 0.0}, {2.0, 1.0}};
  case LocalSketchToolKind::Polyline:
    return {{0.0, 0.0}, {2.0, 1.0}, {3.0, -1.0}};
  case LocalSketchToolKind::Arc:
    return {{0.0, 0.0}, {2.0, 0.0}, {0.0, 2.0}};
  case LocalSketchToolKind::ThreePointArc:
  case LocalSketchToolKind::ThreePointCircle:
  case LocalSketchToolKind::ThreePointEllipse:
    return {{-1.0, 0.0}, {1.0, 0.0}, {0.0, 1.0}};
  case LocalSketchToolKind::Ellipse:
    return {{0.0, 0.0}, {2.0, 0.0}, {0.0, 1.0}};
  case LocalSketchToolKind::EllipticalArc:
    return {{0.0, 0.0}, {2.0, 0.0}, {0.0, 1.0}, {2.0, 0.0}, {0.0, 1.0}};
  case LocalSketchToolKind::HyperbolicArc:
    return {{0.0, 0.0}, {2.0, 0.0}, {3.0, 1.0}, {3.0, -1.0}};
  case LocalSketchToolKind::ParabolicArc:
    return {{2.0, 0.0}, {0.0, 0.0}, {1.0, 1.0}, {1.0, -1.0}};
  case LocalSketchToolKind::Slot:
  case LocalSketchToolKind::Oblong:
    return {{0.0, 0.0}, {2.0, 0.0}, {1.0, 0.5}};
  case LocalSketchToolKind::ArcSlot:
    return {{0.0, 0.0}, {2.0, 0.0}, {0.0, 2.0}, {2.5, 0.0}};
  case LocalSketchToolKind::Triangle:
  case LocalSketchToolKind::Square:
  case LocalSketchToolKind::Pentagon:
  case LocalSketchToolKind::Hexagon:
  case LocalSketchToolKind::Heptagon:
  case LocalSketchToolKind::Octagon:
  case LocalSketchToolKind::RegularPolygon:
    return {{0.0, 0.0}, {2.0, 0.0}};
  case LocalSketchToolKind::BSpline:
  case LocalSketchToolKind::PeriodicBSpline:
  case LocalSketchToolKind::InterpolatedBSpline:
  case LocalSketchToolKind::PeriodicInterpolatedBSpline:
    return {{0.0, 0.0}, {1.0, 1.0}, {2.0, -0.5}, {3.0, 0.5}};
  }
  return {};
}

} // namespace kearne::ui::test
