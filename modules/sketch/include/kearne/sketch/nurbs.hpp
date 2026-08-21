#pragma once

#include <kearne/base/value.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <stop_token>
#include <utility>
#include <vector>

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

struct NurbsPolyline {
  std::vector<NurbsPoint> points;
  double maximumCertifiedDeviation;
  std::size_t retainedBytes;
  std::size_t scratchBytes;
  std::size_t peakBytes;
};

std::pair<double, double> nurbsDomain(NurbsView curve);
NurbsPoint evaluateNurbs(NurbsView curve, double parameter);
NurbsPoint differentiateNurbs(NurbsView curve, double parameter);
std::size_t periodicNurbsTailCount(NurbsView curve, double coordinateTolerance,
                                   double relativeWeightTolerance);
NurbsProjection projectToNurbs(NurbsView curve, NurbsPoint query);
Result<NurbsPolyline> tessellateNurbs(NurbsView curve, double maximumError,
                                      std::size_t maximumSegments,
                                      std::stop_token cancellation = {});

} // namespace kearne::sketch
