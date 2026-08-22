#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace kearne::sketch {

struct NurbsPoint {
  double x;
  double y;
  auto operator<=>(const NurbsPoint &) const = default;
};

struct NurbsView {
  std::span<const double> controlPointCoordinates;
  std::span<const double> knots;
  std::span<const double> weights;
  std::uint32_t degree;
};

struct NurbsProjection {
  NurbsPoint point;
  double parameter;
  double squaredDistance;
};

std::pair<double, double> nurbsDomain(NurbsView curve);
NurbsPoint evaluateNurbs(NurbsView curve, double parameter);
NurbsPoint differentiateNurbs(NurbsView curve, double parameter);
NurbsPoint differentiateNurbsSecond(NurbsView curve, double parameter);
std::size_t periodicNurbsTailCount(NurbsView curve, double coordinateTolerance,
                                   double relativeWeightTolerance);
NurbsProjection projectToNurbs(NurbsView curve, NurbsPoint query);

} // namespace kearne::sketch
