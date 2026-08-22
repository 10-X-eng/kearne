#include <kearne/sketch/nurbs.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>

namespace kearne::sketch {
namespace {

constexpr std::size_t maximumDegree = 25U;

struct HomogeneousPoint {
  double x;
  double y;
  double weight;
};

template <typename Pole>
HomogeneousPoint evaluateHomogeneous(std::span<const double> knots,
                                     std::size_t count, std::size_t degree,
                                     double parameter, Pole pole) {
  const double first = knots[degree];
  const double last = knots[count];
  parameter = std::clamp(parameter, first, last);
  std::size_t span = count - 1U;
  if (parameter < last) {
    const auto found = std::upper_bound(knots.begin() + degree,
                                        knots.begin() + count + 1U, parameter);
    span = static_cast<std::size_t>(found - knots.begin() - 1U);
  }

  std::array<HomogeneousPoint, maximumDegree + 1U> values{};
  for (std::size_t index = 0U; index <= degree; ++index)
    values[index] = pole(span - degree + index);
  for (std::size_t level = 1U; level <= degree; ++level) {
    for (std::size_t index = degree; index >= level; --index) {
      const std::size_t knot = span - degree + index;
      const double denominator = knots[index + 1U + span - level] - knots[knot];
      const double alpha =
          denominator == 0.0 ? 0.0 : (parameter - knots[knot]) / denominator;
      values[index] = {
          std::lerp(values[index - 1U].x, values[index].x, alpha),
          std::lerp(values[index - 1U].y, values[index].y, alpha),
          std::lerp(values[index - 1U].weight, values[index].weight, alpha)};
    }
  }
  return values[degree];
}

double squaredDistance(NurbsPoint first, NurbsPoint second) {
  const double x = first.x - second.x;
  const double y = first.y - second.y;
  return x * x + y * y;
}

} // namespace

std::pair<double, double> nurbsDomain(NurbsView curve) {
  return {curve.knots[curve.degree], curve.knots[curve.weights.size()]};
}

NurbsPoint evaluateNurbs(NurbsView curve, double parameter) {
  const std::size_t count = curve.weights.size();
  const double originX = curve.controlPointCoordinates[0];
  const double originY = curve.controlPointCoordinates[1];
  const HomogeneousPoint value = evaluateHomogeneous(
      curve.knots, count, curve.degree, parameter, [&](std::size_t pole) {
        const double weight = curve.weights[pole];
        return HomogeneousPoint{
            (curve.controlPointCoordinates[pole * 2U] - originX) * weight,
            (curve.controlPointCoordinates[pole * 2U + 1U] - originY) * weight,
            weight};
      });
  return {originX + value.x / value.weight,
          originY + value.y / value.weight};
}

NurbsPoint differentiateNurbs(NurbsView curve, double parameter) {
  const std::size_t count = curve.weights.size();
  const double originX = curve.controlPointCoordinates[0];
  const double originY = curve.controlPointCoordinates[1];
  const auto pole = [&](std::size_t index) {
    const double weight = curve.weights[index];
    return HomogeneousPoint{
        (curve.controlPointCoordinates[index * 2U] - originX) * weight,
        (curve.controlPointCoordinates[index * 2U + 1U] - originY) * weight,
        weight};
  };
  const HomogeneousPoint value =
      evaluateHomogeneous(curve.knots, count, curve.degree, parameter, pole);
  const HomogeneousPoint derivative = evaluateHomogeneous(
      curve.knots.subspan(1U, curve.knots.size() - 2U), count - 1U,
      curve.degree - 1U, parameter, [&](std::size_t index) {
        const HomogeneousPoint first = pole(index);
        const HomogeneousPoint second = pole(index + 1U);
        const double span =
            curve.knots[index + curve.degree + 1U] - curve.knots[index + 1U];
        const double scale =
            span == 0.0 ? 0.0 : static_cast<double>(curve.degree) / span;
        return HomogeneousPoint{(second.x - first.x) * scale,
                                (second.y - first.y) * scale,
                                (second.weight - first.weight) * scale};
      });
  const double denominator = value.weight * value.weight;
  return {(derivative.x * value.weight - value.x * derivative.weight) /
              denominator,
          (derivative.y * value.weight - value.y * derivative.weight) /
              denominator};
}

NurbsPoint differentiateNurbsSecond(NurbsView curve, double parameter) {
  const std::size_t count = curve.weights.size();
  const double originX = curve.controlPointCoordinates[0];
  const double originY = curve.controlPointCoordinates[1];
  const auto pole = [&](std::size_t index) {
    const double weight = curve.weights[index];
    return HomogeneousPoint{
        (curve.controlPointCoordinates[index * 2U] - originX) * weight,
        (curve.controlPointCoordinates[index * 2U + 1U] - originY) * weight,
        weight};
  };
  const auto firstPole = [&](std::size_t index) {
    const HomogeneousPoint first = pole(index);
    const HomogeneousPoint second = pole(index + 1U);
    const double span =
        curve.knots[index + curve.degree + 1U] - curve.knots[index + 1U];
    const double scale =
        span == 0.0 ? 0.0 : static_cast<double>(curve.degree) / span;
    return HomogeneousPoint{(second.x - first.x) * scale,
                            (second.y - first.y) * scale,
                            (second.weight - first.weight) * scale};
  };
  const HomogeneousPoint value =
      evaluateHomogeneous(curve.knots, count, curve.degree, parameter, pole);
  const HomogeneousPoint first = evaluateHomogeneous(
      curve.knots.subspan(1U, curve.knots.size() - 2U), count - 1U,
      curve.degree - 1U, parameter, firstPole);
  HomogeneousPoint second{};
  if (curve.degree >= 2U)
    second = evaluateHomogeneous(
        curve.knots.subspan(2U, curve.knots.size() - 4U), count - 2U,
        curve.degree - 2U, parameter, [&](std::size_t index) {
          const HomogeneousPoint previous = firstPole(index);
          const HomogeneousPoint next = firstPole(index + 1U);
          const double span = curve.knots[index + curve.degree + 1U] -
                              curve.knots[index + 2U];
          const double scale =
              span == 0.0 ? 0.0
                          : static_cast<double>(curve.degree - 1U) / span;
          return HomogeneousPoint{(next.x - previous.x) * scale,
                                  (next.y - previous.y) * scale,
                                  (next.weight - previous.weight) * scale};
        });
  const double weight2 = value.weight * value.weight;
  const double weight3 = weight2 * value.weight;
  return {(second.x * weight2 - value.x * second.weight * value.weight -
           2.0 * first.x * value.weight * first.weight +
           2.0 * value.x * first.weight * first.weight) /
              weight3,
          (second.y * weight2 - value.y * second.weight * value.weight -
           2.0 * first.y * value.weight * first.weight +
           2.0 * value.y * first.weight * first.weight) /
              weight3};
}

std::size_t periodicNurbsTailCount(NurbsView curve, double coordinateTolerance,
                                   double relativeWeightTolerance) {
  const std::size_t count = curve.weights.size();
  const auto [first, last] = nurbsDomain(curve);
  const std::size_t firstMultiplicity =
      static_cast<std::size_t>(std::ranges::count(curve.knots, first));
  const std::size_t lastMultiplicity =
      static_cast<std::size_t>(std::ranges::count(curve.knots, last));
  if (firstMultiplicity != lastMultiplicity || firstMultiplicity == 0U ||
      firstMultiplicity > curve.degree)
    return 0U;
  const std::size_t candidate = curve.degree + 1U - firstMultiplicity;
  if (candidate > count / 2U)
    return 0U;
  for (std::size_t index = 0U; index < candidate; ++index) {
    const std::size_t tail = count - candidate + index;
    const double weightScale = std::max(
        {1.0, std::abs(curve.weights[index]), std::abs(curve.weights[tail])});
    if (std::hypot(curve.controlPointCoordinates[index * 2U] -
                       curve.controlPointCoordinates[tail * 2U],
                   curve.controlPointCoordinates[index * 2U + 1U] -
                       curve.controlPointCoordinates[tail * 2U + 1U]) >
            coordinateTolerance ||
        std::abs(curve.weights[index] - curve.weights[tail]) >
            relativeWeightTolerance * weightScale)
      return 0U;
  }
  return candidate;
}

NurbsProjection projectToNurbs(NurbsView curve, NurbsPoint query) {
  NurbsProjection best{{}, 0.0, std::numeric_limits<double>::infinity()};
  const auto consider = [&](double parameter) {
    const NurbsPoint point = evaluateNurbs(curve, parameter);
    const double distance = squaredDistance(point, query);
    if (distance < best.squaredDistance)
      best = {point, parameter, distance};
    return distance;
  };

  const std::size_t count = curve.weights.size();
  const std::size_t samplesPerSpan =
      std::max<std::size_t>(8U, static_cast<std::size_t>(curve.degree) * 4U);
  constexpr double inversePhi = 0.6180339887498948482;
  for (std::size_t span = curve.degree; span < count; ++span) {
    const double first = curve.knots[span];
    const double last = curve.knots[span + 1U];
    if (!(first < last))
      continue;

    std::size_t localBest = 0U;
    double localDistance = std::numeric_limits<double>::infinity();
    for (std::size_t sample = 0U; sample <= samplesPerSpan; ++sample) {
      const double parameter = std::lerp(
          first, last,
          static_cast<double>(sample) / static_cast<double>(samplesPerSpan));
      const double distance = consider(parameter);
      if (distance < localDistance) {
        localBest = sample;
        localDistance = distance;
      }
    }

    double lower =
        std::lerp(first, last,
                  static_cast<double>(localBest == 0U ? 0U : localBest - 1U) /
                      static_cast<double>(samplesPerSpan));
    double upper = std::lerp(first, last,
                             static_cast<double>(localBest == samplesPerSpan
                                                     ? samplesPerSpan
                                                     : localBest + 1U) /
                                 static_cast<double>(samplesPerSpan));
    double left = upper - (upper - lower) * inversePhi;
    double right = lower + (upper - lower) * inversePhi;
    double leftDistance = consider(left);
    double rightDistance = consider(right);
    for (std::size_t iteration = 0U; iteration < 40U; ++iteration) {
      if (leftDistance <= rightDistance) {
        upper = right;
        right = left;
        rightDistance = leftDistance;
        left = upper - (upper - lower) * inversePhi;
        leftDistance = consider(left);
      } else {
        lower = left;
        left = right;
        leftDistance = rightDistance;
        right = lower + (upper - lower) * inversePhi;
        rightDistance = consider(right);
      }
    }
  }
  return best;
}

} // namespace kearne::sketch
