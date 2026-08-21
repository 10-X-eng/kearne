#pragma once

#include <kearne/sketch/model.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace kearne::adapters {

enum class BSplineCreation : std::uint8_t {
  ControlPoints = 1,
  Interpolation = 2,
};

enum class BSplineEdit : std::uint8_t {
  IncreaseDegree = 1,
  DecreaseDegree = 2,
  IncreaseKnotMultiplicity = 3,
  DecreaseKnotMultiplicity = 4,
  InsertKnot = 5,
  SetPoleWeight = 6,
};

struct BSplineRequest {
  SketchEntityId id;
  std::vector<sketch::Point2> points;
  BSplineCreation creation = BSplineCreation::ControlPoints;
  std::uint32_t degree = 3U;
  bool periodic = false;
  bool construction = false;
  double interpolationToleranceMetres = 1.0e-8;
};

struct CanonicalBSpline {
  std::vector<sketch::Point2> controlPoints;
  std::vector<sketch::DimensionlessValue> knots;
  std::vector<sketch::DimensionlessValue> weights;
  std::uint32_t degree = 3U;
  bool periodic = false;
};

struct BSplineEditRequest {
  sketch::BSplineEntity entity;
  BSplineEdit edit = BSplineEdit::IncreaseDegree;
  std::size_t index = 0U;
  double value = 0.0;
  double maximumDeviationMetres = 0.0;
};

[[nodiscard]] Result<CanonicalBSpline>
createBSplineGeometry(std::span<const sketch::Point2> points,
                      BSplineCreation creation, std::uint32_t degree,
                      bool periodic,
                      double interpolationToleranceMetres = 1.0e-8);

[[nodiscard]] Result<sketch::BSplineEntity>
createBSpline(const BSplineRequest &request);

[[nodiscard]] Result<sketch::BSplineEntity>
editBSpline(const BSplineEditRequest &request);

} // namespace kearne::adapters
