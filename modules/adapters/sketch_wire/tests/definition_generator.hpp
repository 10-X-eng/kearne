#pragma once

#include <kearne/sketch/model.hpp>
#include <kearne/testkit/property.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <numbers>
#include <stdexcept>
#include <string>

namespace kearne::adapters::test {

template <typename Id> Id id(std::uint64_t index) {
  typename Id::RandomTail tail{};
  for (std::size_t offset = 0; offset < tail.size(); ++offset)
    tail[offset] = static_cast<std::uint8_t>(index >> ((offset % 8U) * 8U));
  auto value =
      Id::create(1'700'000'000'000ULL + index % 1'000'000'000ULL, tail);
  if (!value)
    throw std::runtime_error(value.error().summary);
  return *value;
}

inline sketch::LengthValue length(double value) {
  auto result = sketch::LengthValue::fromSi(value);
  if (!result)
    throw std::runtime_error(result.error().summary);
  return *result;
}

inline sketch::AngleValue angle(double value) {
  auto result = sketch::AngleValue::fromSi(value);
  if (!result)
    throw std::runtime_error(result.error().summary);
  return *result;
}

inline sketch::DimensionlessValue dimensionless(double value) {
  auto result = sketch::DimensionlessValue::fromSi(value);
  if (!result)
    throw std::runtime_error(result.error().summary);
  return *result;
}

inline ContentDigest sourceDigest() {
  auto result = ContentDigest::parse("blake3:6e82d967b887a378d96d00d3e8d8fc8c"
                                     "72247cdcb197b6ee6815a9af954f1e4d");
  if (!result)
    throw std::runtime_error(result.error().summary);
  return *result;
}

inline sketch::Definition completeDefinition(std::uint64_t seed = 1) {
  const double offset = static_cast<double>(seed % 1000U) * 1.0e-6;
  const SketchEntityId pointId = id<SketchEntityId>(seed + 1);
  const SketchEntityId firstLine = id<SketchEntityId>(seed + 2);
  const SketchEntityId secondLine = id<SketchEntityId>(seed + 3);
  const SketchEntityId firstCircle = id<SketchEntityId>(seed + 4);
  const SketchEntityId secondCircle = id<SketchEntityId>(seed + 5);
  const SketchEntityId arcId = id<SketchEntityId>(seed + 6);
  const SketchEntityId ellipseId = id<SketchEntityId>(seed + 7);
  const SketchEntityId ellipticalArcId = id<SketchEntityId>(seed + 8);
  const SketchEntityId hyperbolicArcId = id<SketchEntityId>(seed + 9);
  const SketchEntityId parabolicArcId = id<SketchEntityId>(seed + 10);
  const SketchEntityId bsplineId = id<SketchEntityId>(seed + 11);
  const std::array rectangleLines{
      id<SketchEntityId>(seed + 20), id<SketchEntityId>(seed + 21),
      id<SketchEntityId>(seed + 22), id<SketchEntityId>(seed + 23)};
  const std::array slotCurves{
      id<SketchEntityId>(seed + 30), id<SketchEntityId>(seed + 31),
      id<SketchEntityId>(seed + 32), id<SketchEntityId>(seed + 33)};
  const std::array arcSlotCurves{
      id<SketchEntityId>(seed + 40), id<SketchEntityId>(seed + 41),
      id<SketchEntityId>(seed + 42), id<SketchEntityId>(seed + 43)};
  const std::array polylineSegments{id<SketchEntityId>(seed + 50),
                                    id<SketchEntityId>(seed + 51)};
  const std::array polygonSides{id<SketchEntityId>(seed + 60),
                                id<SketchEntityId>(seed + 61),
                                id<SketchEntityId>(seed + 62)};
  const std::array oblongCurves{
      id<SketchEntityId>(seed + 70), id<SketchEntityId>(seed + 71),
      id<SketchEntityId>(seed + 72), id<SketchEntityId>(seed + 73)};
  sketch::Definition result{sourceDigest(), {}, {}, {}};
  result.entities = {
      sketch::PointEntity{pointId, {length(offset), length(0.002)}},
      sketch::LineEntity{firstLine,
                         {length(0.0), length(0.0)},
                         {length(0.04 + offset), length(0.0)}},
      sketch::LineEntity{secondLine,
                         {length(0.0), length(0.02)},
                         {length(0.04), length(0.02 + offset)}},
      sketch::CircleEntity{
          firstCircle, {length(0.0), length(0.0)}, length(0.01)},
      sketch::CircleEntity{
          secondCircle, {length(0.02), length(0.0)}, length(0.01)},
      sketch::ArcEntity{arcId,
                        {length(0.03), length(0.03)},
                        length(0.005),
                        angle(0.1),
                        angle(1.7),
                        seed % 2U == 0},
      sketch::EllipseEntity{ellipseId,
                            {length(0.07), length(0.04)},
                            length(0.012),
                            length(0.006),
                            angle(0.3),
                            seed % 2U != 0},
      sketch::EllipticalArcEntity{ellipticalArcId,
                                  {length(0.08), length(0.07)},
                                  length(0.014),
                                  length(0.005),
                                  angle(-0.2),
                                  angle(0.4),
                                  angle(2.3),
                                  seed % 2U == 0},
      sketch::HyperbolicArcEntity{hyperbolicArcId,
                                  {length(0.12), length(0.07)},
                                  length(0.011),
                                  length(0.017),
                                  angle(0.37),
                                  dimensionless(-0.8),
                                  dimensionless(1.2 + offset),
                                  seed % 2U != 0},
      sketch::ParabolicArcEntity{parabolicArcId,
                                 {length(0.15), length(0.07)},
                                 length(0.006),
                                 angle(-0.41),
                                 length(-0.015),
                                 length(0.021 + offset),
                                 seed % 2U == 0},
      sketch::BSplineEntity{
          bsplineId,
          {{length(0.17), length(0.07)},
           {length(0.18), length(0.09 + offset)},
           {length(0.20), length(0.05)},
           {length(0.22), length(0.075)}},
          {dimensionless(0.0), dimensionless(0.0), dimensionless(0.0),
           dimensionless(0.0), dimensionless(1.0), dimensionless(1.0),
           dimensionless(1.0), dimensionless(1.0)},
          {dimensionless(1.0), dimensionless(0.75), dimensionless(1.25),
           dimensionless(1.0)},
          3U,
          false,
          seed % 2U != 0},
      sketch::LineEntity{rectangleLines[0],
                         {length(0.10), length(0.10)},
                         {length(0.14), length(0.10)}},
      sketch::LineEntity{rectangleLines[1],
                         {length(0.14), length(0.10)},
                         {length(0.14), length(0.13)}},
      sketch::LineEntity{rectangleLines[2],
                         {length(0.14), length(0.13)},
                         {length(0.10), length(0.13)}},
      sketch::LineEntity{rectangleLines[3],
                         {length(0.10), length(0.13)},
                         {length(0.10), length(0.10)}},
      sketch::ArcEntity{slotCurves[0],
                        {length(0.20), length(0.10)},
                        length(0.01),
                        angle(std::numbers::pi / 2.0),
                        angle(3.0 * std::numbers::pi / 2.0)},
      sketch::ArcEntity{slotCurves[1],
                        {length(0.25), length(0.10)},
                        length(0.01),
                        angle(3.0 * std::numbers::pi / 2.0),
                        angle(5.0 * std::numbers::pi / 2.0)},
      sketch::LineEntity{slotCurves[2],
                         {length(0.20), length(0.11)},
                         {length(0.25), length(0.11)}},
      sketch::LineEntity{slotCurves[3],
                         {length(0.20), length(0.09)},
                         {length(0.25), length(0.09)}},
      sketch::ArcEntity{arcSlotCurves[0],
                        {length(0.35), length(0.10)},
                        length(0.045),
                        angle(0.0),
                        angle(std::numbers::pi / 2.0)},
      sketch::ArcEntity{arcSlotCurves[1],
                        {length(0.35), length(0.14)},
                        length(0.005),
                        angle(std::numbers::pi / 2.0),
                        angle(3.0 * std::numbers::pi / 2.0)},
      sketch::ArcEntity{arcSlotCurves[2],
                        {length(0.35), length(0.10)},
                        length(0.035),
                        angle(std::numbers::pi / 2.0),
                        angle(0.0)},
      sketch::ArcEntity{arcSlotCurves[3],
                        {length(0.39), length(0.10)},
                        length(0.005),
                        angle(std::numbers::pi),
                        angle(2.0 * std::numbers::pi)},
      sketch::LineEntity{polylineSegments[0],
                         {length(0.40), length(0.20)},
                         {length(0.43), length(0.22)}},
      sketch::LineEntity{polylineSegments[1],
                         {length(0.43), length(0.22)},
                         {length(0.46), length(0.20)}},
      sketch::LineEntity{polygonSides[0],
                         {length(0.50), length(0.20)},
                         {length(0.54), length(0.20)}},
      sketch::LineEntity{polygonSides[1],
                         {length(0.54), length(0.20)},
                         {length(0.52), length(0.23464101615137755)}},
      sketch::LineEntity{polygonSides[2],
                         {length(0.52), length(0.23464101615137755)},
                         {length(0.50), length(0.20)}},
      sketch::ArcEntity{oblongCurves[0],
                        {length(0.20), length(0.30)},
                        length(0.01),
                        angle(std::numbers::pi / 2.0),
                        angle(3.0 * std::numbers::pi / 2.0)},
      sketch::ArcEntity{oblongCurves[1],
                        {length(0.25), length(0.30)},
                        length(0.01),
                        angle(3.0 * std::numbers::pi / 2.0),
                        angle(5.0 * std::numbers::pi / 2.0)},
      sketch::LineEntity{oblongCurves[2],
                         {length(0.20), length(0.31)},
                         {length(0.25), length(0.31)}},
      sketch::LineEntity{oblongCurves[3],
                         {length(0.20), length(0.29)},
                         {length(0.25), length(0.29)}},
  };
  result.objects = {
      sketch::SketchObject{id<SketchObjectId>(seed + 1'001),
                           "Point 1",
                           sketch::SketchObjectKind::Point,
                           {{"point", pointId}}},
      sketch::SketchObject{id<SketchObjectId>(seed + 1'002),
                           "Line 1",
                           sketch::SketchObjectKind::Line,
                           {{"curve", firstLine}}},
      sketch::SketchObject{id<SketchObjectId>(seed + 1'003),
                           "Circle 1",
                           sketch::SketchObjectKind::Circle,
                           {{"curve", firstCircle}}},
      sketch::SketchObject{id<SketchObjectId>(seed + 1'004),
                           "Arc 1",
                           sketch::SketchObjectKind::Arc,
                           {{"curve", arcId}}},
      sketch::SketchObject{id<SketchObjectId>(seed + 1'011),
                           "Ellipse 1",
                           sketch::SketchObjectKind::Ellipse,
                           {{"curve", ellipseId}}},
      sketch::SketchObject{id<SketchObjectId>(seed + 1'012),
                           "Elliptical Arc 1",
                           sketch::SketchObjectKind::EllipticalArc,
                           {{"curve", ellipticalArcId}}},
      sketch::SketchObject{id<SketchObjectId>(seed + 1'013),
                           "Hyperbolic Arc 1",
                           sketch::SketchObjectKind::HyperbolicArc,
                           {{"curve", hyperbolicArcId}}},
      sketch::SketchObject{id<SketchObjectId>(seed + 1'014),
                           "Parabolic Arc 1",
                           sketch::SketchObjectKind::ParabolicArc,
                           {{"curve", parabolicArcId}}},
      sketch::SketchObject{id<SketchObjectId>(seed + 1'015),
                           "B-spline 1",
                           sketch::SketchObjectKind::BSpline,
                           {{"curve", bsplineId}}},
      sketch::SketchObject{id<SketchObjectId>(seed + 1'005),
                           "Rectangle 1",
                           sketch::SketchObjectKind::Rectangle,
                           {{"bottom", rectangleLines[0]},
                            {"right", rectangleLines[1]},
                            {"top", rectangleLines[2]},
                            {"left", rectangleLines[3]}}},
      sketch::SketchObject{id<SketchObjectId>(seed + 1'006),
                           "Slot 1",
                           sketch::SketchObjectKind::Slot,
                           {{"start_cap", slotCurves[0]},
                            {"end_cap", slotCurves[1]},
                            {"top_side", slotCurves[2]},
                            {"bottom_side", slotCurves[3]}}},
      sketch::SketchObject{id<SketchObjectId>(seed + 1'007),
                           "Arc Slot 1",
                           sketch::SketchObjectKind::ArcSlot,
                           {{"outer", arcSlotCurves[0]},
                            {"end_cap", arcSlotCurves[1]},
                            {"inner", arcSlotCurves[2]},
                            {"start_cap", arcSlotCurves[3]}}},
      sketch::SketchObject{id<SketchObjectId>(seed + 1'008),
                           "Polyline 1",
                           sketch::SketchObjectKind::Polyline,
                           {{"segment_1", polylineSegments[0]},
                            {"segment_2", polylineSegments[1]}}},
      sketch::SketchObject{id<SketchObjectId>(seed + 1'009),
                           "Triangle 1",
                           sketch::SketchObjectKind::RegularPolygon,
                           {{"side_1", polygonSides[0]},
                            {"side_2", polygonSides[1]},
                            {"side_3", polygonSides[2]}}},
      sketch::SketchObject{id<SketchObjectId>(seed + 1'010),
                           "Oblong 1",
                           sketch::SketchObjectKind::Oblong,
                           {{"start_cap", oblongCurves[0]},
                            {"end_cap", oblongCurves[1]},
                            {"top_side", oblongCurves[2]},
                            {"bottom_side", oblongCurves[3]}}},
      sketch::SketchObject{id<SketchObjectId>(seed + 1'016),
                           "Imported guides (modified)",
                           sketch::SketchObjectKind::CurveGroup,
                           {{"guide", secondLine},
                            {"clearance", secondCircle}}},
  };
  const sketch::PointRef point{pointId, sketch::PointKey::Point};
  const sketch::PointRef start{firstLine, sketch::PointKey::Start};
  const sketch::PointRef end{firstLine, sketch::PointKey::End};
  const sketch::PointRef center{firstCircle, sketch::PointKey::Center};
  const auto constraintId = [seed](std::uint64_t index) {
    return id<SketchConstraintId>(seed + 100 + index);
  };
  result.constraints = {
      sketch::Coincident{constraintId(1), point, start},
      sketch::Horizontal{constraintId(2), firstLine},
      sketch::Vertical{constraintId(3), secondLine},
      sketch::Parallel{constraintId(4), firstLine, secondLine},
      sketch::Perpendicular{constraintId(5), firstLine, secondLine},
      sketch::Tangent{constraintId(6), firstLine, firstCircle,
                      seed % 2U == 0 ? sketch::Tangency::Internal
                                     : sketch::Tangency::External},
      sketch::Concentric{constraintId(7), firstCircle, secondCircle},
      sketch::Equal{constraintId(8), firstLine, secondLine},
      sketch::Midpoint{constraintId(9), center, firstLine},
      sketch::Block{constraintId(10), arcId},
      sketch::Collinear{constraintId(11), firstLine, secondLine},
      sketch::Distance{constraintId(12), point, end, length(0.03)},
      sketch::HorizontalDistance{constraintId(13), point, end,
                                 length(0.03 + offset)},
      sketch::VerticalDistance{constraintId(14), point, end, length(-0.002)},
      sketch::Radius{constraintId(15), firstCircle, length(0.01)},
      sketch::Diameter{constraintId(16), secondCircle, length(0.02)},
      sketch::AngleBetween{constraintId(17), firstLine, secondLine, angle(0.5)},
      sketch::PointOnObject{constraintId(18), point, firstCircle},
      sketch::Symmetric{constraintId(19), point, end, secondLine},
      sketch::Lock{constraintId(20), point, {length(offset), length(0.002)}},
      sketch::SymmetricAboutPoint{constraintId(21), start, end, point},
      sketch::Group{constraintId(22), {firstLine, firstCircle}},
  };
  return result;
}

inline sketch::Definition lineDefinition(std::size_t count) {
  sketch::Definition result{sourceDigest(), {}, {}, {}};
  result.entities.reserve(count);
  result.constraints.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    const double coordinate = static_cast<double>(index) * 1.0e-5;
    result.entities.emplace_back(
        sketch::LineEntity{id<SketchEntityId>(index + 1),
                           {length(coordinate), length(0.0)},
                           {length(coordinate + 0.001), length(0.001)},
                           index % 5U == 0});
  }
  for (std::size_t index = 0; index < count; ++index)
    result.constraints.emplace_back(sketch::Horizontal{
        id<SketchConstraintId>(index + 1), id<SketchEntityId>(index + 1)});
  return result;
}

} // namespace kearne::adapters::test
