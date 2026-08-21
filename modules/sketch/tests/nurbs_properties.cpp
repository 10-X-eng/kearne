#include <kearne/sketch/nurbs.hpp>
#include <kearne/testkit/property.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {

using namespace kearne;

double squaredDistanceToSegment(sketch::NurbsPoint point,
                                sketch::NurbsPoint first,
                                sketch::NurbsPoint last) {
  const double x = last.x - first.x;
  const double y = last.y - first.y;
  const double denominator = x * x + y * y;
  const double parameter =
      denominator == 0.0
          ? 0.0
          : std::clamp(((point.x - first.x) * x +
                        (point.y - first.y) * y) /
                           denominator,
                       0.0, 1.0);
  const double dx = point.x - std::lerp(first.x, last.x, parameter);
  const double dy = point.y - std::lerp(first.y, last.y, parameter);
  return dx * dx + dy * dy;
}

void verifyCertifiedTessellation(const testkit::PropertyProfile &profile) {
  testkit::checkProperty(
      "certified rational NURBS tessellation", profile,
      [](testkit::Random &random, std::uint64_t) {
        const std::uint32_t degree =
            static_cast<std::uint32_t>(2U + random.next() % 4U);
        const std::size_t count = degree + 1U;
        const double scale = std::pow(10.0, random.between(-3.0, 3.0));
        std::vector<double> coordinates;
        std::vector<double> weights;
        std::vector<double> knots;
        coordinates.reserve(count * 2U);
        weights.reserve(count);
        knots.reserve(count + degree + 1U);
        for (std::size_t index = 0U; index < count; ++index) {
          coordinates.push_back(random.between(-scale, scale));
          coordinates.push_back(random.between(-scale, scale));
          weights.push_back(std::pow(10.0, random.between(-0.6, 0.6)));
        }
        knots.insert(knots.end(), degree + 1U, 0.0);
        knots.insert(knots.end(), degree + 1U, 1.0);
        const sketch::NurbsView curve{coordinates, knots, weights, degree};
        const double requestedError = scale * 0.01;
        auto polyline =
            sketch::tessellateNurbs(curve, requestedError, 16'384U);
        if (!polyline)
          throw std::runtime_error(polyline.error().code + ": " +
                                   polyline.error().summary);
        if (polyline->points.size() < 2U ||
            polyline->maximumCertifiedDeviation > requestedError)
          throw std::runtime_error(
              "tessellator violated its requested accuracy");

        double observed = 0.0;
        constexpr std::size_t samples = 65U;
        for (std::size_t sample = 0U; sample <= samples; ++sample) {
          const double parameter =
              static_cast<double>(sample) / static_cast<double>(samples);
          const sketch::NurbsPoint point =
              sketch::evaluateNurbs(curve, parameter);
          double nearest = std::numeric_limits<double>::infinity();
          for (std::size_t segment = 1U; segment < polyline->points.size();
               ++segment)
            nearest = std::min(
                nearest,
                squaredDistanceToSegment(point, polyline->points[segment - 1U],
                                         polyline->points[segment]));
          observed = std::max(observed, std::sqrt(nearest));
        }
        const double numericalAllowance = scale * 1.0e-10;
        if (observed >
            polyline->maximumCertifiedDeviation + numericalAllowance)
          throw std::runtime_error(
              "rendered chord escaped its certified curve bound");
      });
}

} // namespace

int main() {
  try {
    verifyCertifiedTessellation(kearne::testkit::propertyProfile());
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
