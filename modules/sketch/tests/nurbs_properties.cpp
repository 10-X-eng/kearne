#include <kearne/sketch/nurbs.hpp>
#include <kearne/testkit/property.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

using namespace kearne;

struct CurveData {
  std::vector<double> coordinates;
  std::vector<double> knots;
  std::vector<double> weights;
  std::uint32_t degree = 0U;

  sketch::NurbsView view() const {
    return {coordinates, knots, weights, degree};
  }
};

CurveData curve(testkit::Random &random) {
  CurveData result;
  result.degree = static_cast<std::uint32_t>(1U + random.next() % 5U);
  const std::size_t count =
      result.degree + 1U + static_cast<std::size_t>(random.next() % 5U);
  result.coordinates.reserve(count * 2U);
  result.weights.reserve(count);
  for (std::size_t index = 0U; index < count; ++index) {
    result.coordinates.push_back(random.between(-100.0, 100.0));
    result.coordinates.push_back(random.between(-100.0, 100.0));
    result.weights.push_back(std::pow(10.0, random.between(-1.0, 1.0)));
  }
  result.knots.insert(result.knots.end(), result.degree + 1U, 0.0);
  const std::size_t interior = count - result.degree - 1U;
  for (std::size_t index = 0U; index < interior; ++index)
    result.knots.push_back(static_cast<double>(index + 1U) /
                           static_cast<double>(interior + 1U));
  result.knots.insert(result.knots.end(), result.degree + 1U, 1.0);
  return result;
}

double squaredDistance(sketch::NurbsPoint first, sketch::NurbsPoint second) {
  const double dx = first.x - second.x;
  const double dy = first.y - second.y;
  return dx * dx + dy * dy;
}

void requireNear(double actual, double expected, double scale,
                 const char *message) {
  const double tolerance = 2.0e-9 * std::max({1.0, scale, std::abs(expected)});
  if (std::abs(actual - expected) > tolerance)
    throw std::runtime_error(message);
}

void requireApprox(double actual, double expected, const char *message) {
  const double tolerance =
      2.0e-4 * std::max({1.0, std::abs(actual), std::abs(expected)});
  if (std::abs(actual - expected) > tolerance)
    throw std::runtime_error(message);
}

void verifyNativeEvaluation(const testkit::PropertyProfile &profile) {
  testkit::checkProperty(
      "native rational NURBS invariants", profile,
      [](testkit::Random &random, std::uint64_t) {
        CurveData source = curve(random);
        const auto [firstParameter, lastParameter] =
            sketch::nurbsDomain(source.view());
        const sketch::NurbsPoint first =
            sketch::evaluateNurbs(source.view(), firstParameter);
        const sketch::NurbsPoint last =
            sketch::evaluateNurbs(source.view(), lastParameter);
        requireNear(first.x, source.coordinates[0], 100.0,
                    "NURBS start did not interpolate its first pole");
        requireNear(first.y, source.coordinates[1], 100.0,
                    "NURBS start did not interpolate its first pole");
        requireNear(last.x, source.coordinates[source.coordinates.size() - 2U],
                    100.0, "NURBS end did not interpolate its last pole");
        requireNear(last.y, source.coordinates.back(), 100.0,
                    "NURBS end did not interpolate its last pole");

        const std::size_t span =
            source.degree + random.next() %
                                (source.weights.size() - source.degree);
        const double parameter =
            std::lerp(source.knots[span], source.knots[span + 1U],
                      random.between(0.2, 0.8));
        const sketch::NurbsPoint evaluated =
            sketch::evaluateNurbs(source.view(), parameter);
        const sketch::NurbsPoint derivative =
            sketch::differentiateNurbs(source.view(), parameter);
        const sketch::NurbsPoint secondDerivative =
            sketch::differentiateNurbsSecond(source.view(), parameter);
        const double step =
            (source.knots[span + 1U] - source.knots[span]) * 1.0e-5;
        const sketch::NurbsPoint derivativeBefore =
            sketch::differentiateNurbs(source.view(), parameter - step);
        const sketch::NurbsPoint derivativeAfter =
            sketch::differentiateNurbs(source.view(), parameter + step);
        requireApprox((derivativeAfter.x - derivativeBefore.x) /
                          (2.0 * step),
                      secondDerivative.x,
                      "NURBS second derivative disagrees with its first derivative");
        requireApprox((derivativeAfter.y - derivativeBefore.y) /
                          (2.0 * step),
                      secondDerivative.y,
                      "NURBS second derivative disagrees with its first derivative");
        const double scale = std::pow(10.0, random.between(-3.0, 3.0));
        const double translateX = random.between(-1.0e5, 1.0e5);
        const double translateY = random.between(-1.0e5, 1.0e5);
        CurveData transformed = source;
        for (std::size_t index = 0U; index < transformed.weights.size();
             ++index) {
          transformed.coordinates[index * 2U] =
              source.coordinates[index * 2U] * scale + translateX;
          transformed.coordinates[index * 2U + 1U] =
              source.coordinates[index * 2U + 1U] * scale + translateY;
        }
        const sketch::NurbsPoint moved =
            sketch::evaluateNurbs(transformed.view(), parameter);
        const sketch::NurbsPoint movedDerivative =
            sketch::differentiateNurbs(transformed.view(), parameter);
        const sketch::NurbsPoint movedSecondDerivative =
            sketch::differentiateNurbsSecond(transformed.view(), parameter);
        requireNear(moved.x, evaluated.x * scale + translateX,
                    std::abs(translateX),
                    "NURBS evaluation changed under affine coordinates");
        requireNear(moved.y, evaluated.y * scale + translateY,
                    std::abs(translateY),
                    "NURBS evaluation changed under affine coordinates");
        requireNear(movedDerivative.x, derivative.x * scale,
                    std::abs(derivative.x * scale),
                    "NURBS derivative changed under affine coordinates");
        requireNear(movedDerivative.y, derivative.y * scale,
                    std::abs(derivative.y * scale),
                    "NURBS derivative changed under affine coordinates");
        requireNear(movedSecondDerivative.x, secondDerivative.x * scale,
                    std::abs(secondDerivative.x * scale),
                    "NURBS second derivative changed under affine coordinates");
        requireNear(movedSecondDerivative.y, secondDerivative.y * scale,
                    std::abs(secondDerivative.y * scale),
                    "NURBS second derivative changed under affine coordinates");

        CurveData rescaledWeights = source;
        const double weightScale =
            std::pow(10.0, random.between(-3.0, 3.0));
        for (double &weight : rescaledWeights.weights)
          weight *= weightScale;
        const sketch::NurbsPoint invariant =
            sketch::evaluateNurbs(rescaledWeights.view(), parameter);
        requireNear(invariant.x, evaluated.x, 100.0,
                    "NURBS changed under uniform weight scaling");
        requireNear(invariant.y, evaluated.y, 100.0,
                    "NURBS changed under uniform weight scaling");

        const sketch::NurbsPoint query{random.between(-120.0, 120.0),
                                       random.between(-120.0, 120.0)};
        const sketch::NurbsProjection projected =
            sketch::projectToNurbs(source.view(), query);
        if (!std::isfinite(projected.parameter) ||
            !std::isfinite(projected.squaredDistance) ||
            projected.parameter < firstParameter ||
            projected.parameter > lastParameter ||
            projected.squaredDistance < 0.0 ||
            squaredDistance(projected.point, query) >
                projected.squaredDistance + 1.0e-8 ||
            projected.squaredDistance >
                std::min(squaredDistance(first, query),
                         squaredDistance(last, query)) +
                    1.0e-8)
          throw std::runtime_error("native NURBS projection is inconsistent");
      });
}

} // namespace

int main() {
  try {
    verifyNativeEvaluation(kearne::testkit::propertyProfile());
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
