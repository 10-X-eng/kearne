#pragma once

#include <kearne/sketch/model.hpp>
#include <kearne/testkit/property.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
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
  sketch::Definition result{sourceDigest(), {}, {}};
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
      sketch::Fixed{constraintId(10), arcId},
      sketch::Collinear{constraintId(11), firstLine, secondLine},
      sketch::Distance{constraintId(12), point, end, length(0.03)},
      sketch::HorizontalDistance{constraintId(13), point, end,
                                 length(0.03 + offset)},
      sketch::VerticalDistance{constraintId(14), point, end, length(-0.002)},
      sketch::Radius{constraintId(15), firstCircle, length(0.01)},
      sketch::Diameter{constraintId(16), secondCircle, length(0.02)},
      sketch::AngleBetween{constraintId(17), firstLine, secondLine, angle(0.5)},
  };
  return result;
}

inline sketch::Definition lineDefinition(std::size_t count) {
  sketch::Definition result{sourceDigest(), {}, {}};
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
