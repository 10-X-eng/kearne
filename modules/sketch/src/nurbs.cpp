#include <kearne/sketch/nurbs.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <new>
#include <numeric>

namespace kearne::sketch {
namespace {

constexpr std::size_t maximumDegree = 25U;

struct HomogeneousPoint {
  double x;
  double y;
  double weight;
};

struct LongHomogeneousPoint {
  long double x;
  long double y;
  long double weight;
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

double conservativeDouble(long double value) {
  if (!(value > 0.0L))
    return 0.0;
  if (!std::isfinite(value) ||
      value >= static_cast<long double>(std::numeric_limits<double>::max()))
    return std::numeric_limits<double>::infinity();
  return std::nextafter(static_cast<double>(value),
                        std::numeric_limits<double>::infinity());
}

LongHomogeneousPoint longHomogeneousPole(NurbsView curve,
                                         std::size_t index,
                                         long double originX,
                                         long double originY) {
  const long double weight = curve.weights[index];
  return {static_cast<long double>(
              curve.controlPointCoordinates[index * 2U] - originX) *
              weight,
          static_cast<long double>(
              curve.controlPointCoordinates[index * 2U + 1U] - originY) *
              weight,
          weight};
}

LongHomogeneousPoint homogeneousDerivative(NurbsView curve,
                                            std::size_t index,
                                            long double originX,
                                            long double originY) {
  const LongHomogeneousPoint first =
      longHomogeneousPole(curve, index, originX, originY);
  const LongHomogeneousPoint second =
      longHomogeneousPole(curve, index + 1U, originX, originY);
  const long double denominator =
      static_cast<long double>(curve.knots[index + curve.degree + 1U]) -
      static_cast<long double>(curve.knots[index + 1U]);
  if (!(denominator > 0.0L))
    return {};
  const long double scale =
      static_cast<long double>(curve.degree) / denominator;
  return {(second.x - first.x) * scale, (second.y - first.y) * scale,
          (second.weight - first.weight) * scale};
}

double spanSecondDerivativeBound(NurbsView curve, std::size_t span) {
  if (curve.degree < 2U)
    return 0.0;

  const std::size_t firstPole = span - curve.degree;
  const long double originX =
      curve.controlPointCoordinates[firstPole * 2U];
  const long double originY =
      curve.controlPointCoordinates[firstPole * 2U + 1U];
  long double minimumWeight = std::numeric_limits<long double>::infinity();
  long double maximumX = 0.0L;
  long double maximumY = 0.0L;
  for (std::size_t index = firstPole; index <= span; ++index) {
    const LongHomogeneousPoint pole =
        longHomogeneousPole(curve, index, originX, originY);
    minimumWeight = std::min(minimumWeight, pole.weight);
    maximumX = std::max(maximumX, std::abs(pole.x));
    maximumY = std::max(maximumY, std::abs(pole.y));
  }

  long double maximumFirstX = 0.0L;
  long double maximumFirstY = 0.0L;
  long double maximumFirstWeight = 0.0L;
  for (std::size_t index = firstPole; index < span; ++index) {
    const LongHomogeneousPoint derivative =
        homogeneousDerivative(curve, index, originX, originY);
    maximumFirstX = std::max(maximumFirstX, std::abs(derivative.x));
    maximumFirstY = std::max(maximumFirstY, std::abs(derivative.y));
    maximumFirstWeight = std::max(
        maximumFirstWeight,
        std::abs(derivative.weight));
  }

  long double maximumSecondX = 0.0L;
  long double maximumSecondY = 0.0L;
  long double maximumSecondWeight = 0.0L;
  for (std::size_t index = firstPole; index + 1U < span; ++index) {
    const LongHomogeneousPoint first =
        homogeneousDerivative(curve, index, originX, originY);
    const LongHomogeneousPoint second =
        homogeneousDerivative(curve, index + 1U, originX, originY);
    const long double denominator =
        static_cast<long double>(curve.knots[index + curve.degree + 1U]) -
        static_cast<long double>(curve.knots[index + 2U]);
    if (!(denominator > 0.0L))
      continue;
    const long double scale =
        static_cast<long double>(curve.degree - 1U) / denominator;
    maximumSecondX = std::max(
        maximumSecondX,
        std::abs((second.x - first.x) * scale));
    maximumSecondY = std::max(
        maximumSecondY,
        std::abs((second.y - first.y) * scale));
    maximumSecondWeight = std::max(
        maximumSecondWeight,
        std::abs((second.weight - first.weight) * scale));
  }

  const long double weight2 = minimumWeight * minimumWeight;
  const long double weight3 = weight2 * minimumWeight;
  const auto coordinateBound = [&](long double coordinate,
                                   long double firstDerivative,
                                   long double secondDerivative) {
    return secondDerivative / minimumWeight +
           coordinate * maximumSecondWeight / weight2 +
           2.0L * firstDerivative * maximumFirstWeight / weight2 +
           2.0L * coordinate * maximumFirstWeight * maximumFirstWeight /
               weight3;
  };
  const long double x =
      coordinateBound(maximumX, maximumFirstX, maximumSecondX);
  const long double y =
      coordinateBound(maximumY, maximumFirstY, maximumSecondY);
  return conservativeDouble(std::hypot(x, y));
}

double chordDeviationBound(double secondDerivativeBound, double first,
                           double last) {
  const long double width =
      static_cast<long double>(last) - static_cast<long double>(first);
  return conservativeDouble(static_cast<long double>(secondDerivativeBound) *
                            width * width / 8.0L);
}

} // namespace

std::pair<double, double> nurbsDomain(NurbsView curve) {
  return {curve.knots[curve.degree], curve.knots[curve.weights.size()]};
}

NurbsPoint evaluateNurbs(NurbsView curve, double parameter) {
  const std::size_t count = curve.weights.size();
  const HomogeneousPoint value = evaluateHomogeneous(
      curve.knots, count, curve.degree, parameter, [&](std::size_t pole) {
        const double weight = curve.weights[pole];
        return HomogeneousPoint{
            curve.controlPointCoordinates[pole * 2U] * weight,
            curve.controlPointCoordinates[pole * 2U + 1U] * weight, weight};
      });
  return {value.x / value.weight, value.y / value.weight};
}

NurbsPoint differentiateNurbs(NurbsView curve, double parameter) {
  const std::size_t count = curve.weights.size();
  const auto pole = [&](std::size_t index) {
    const double weight = curve.weights[index];
    return HomogeneousPoint{
        curve.controlPointCoordinates[index * 2U] * weight,
        curve.controlPointCoordinates[index * 2U + 1U] * weight, weight};
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

Result<NurbsPolyline> tessellateNurbs(NurbsView curve, double maximumError,
                                      std::size_t maximumSegments,
                                      std::stop_token cancellation) {
  if (!std::isfinite(maximumError) || maximumError <= 0.0 ||
      maximumSegments == 0U)
    return std::unexpected(diagnostic("sketch.nurbs.invalid-tessellation",
                                      "NURBS tessellation limits are invalid"));
  if (curve.degree == 0U || curve.degree > maximumDegree ||
      curve.weights.size() <= curve.degree ||
      curve.controlPointCoordinates.size() != curve.weights.size() * 2U ||
      curve.knots.size() != curve.weights.size() + curve.degree + 1U ||
      !std::ranges::all_of(curve.controlPointCoordinates, [](double value) {
        return std::isfinite(value);
      }) ||
      !std::ranges::all_of(curve.knots,
                           [](double value) { return std::isfinite(value); }) ||
      !std::ranges::all_of(curve.weights, [](double value) {
        return std::isfinite(value) && value > 0.0;
      }) ||
      !std::ranges::is_sorted(curve.knots) ||
      !(curve.knots[curve.degree] < curve.knots[curve.weights.size()]))
    return std::unexpected(diagnostic("sketch.nurbs.invalid-curve",
                                      "NURBS curve data is invalid"));
  struct Segment {
    double first;
    double last;
    NurbsPoint firstPoint;
    NurbsPoint lastPoint;
    double secondDerivativeBound;
  };
  try {
    const std::size_t seedsPerSpan =
        std::max<std::size_t>(2U, static_cast<std::size_t>(curve.degree) * 2U);
    std::vector<Segment> seeds;
    for (std::size_t span = curve.degree; span < curve.weights.size(); ++span) {
      const double first = curve.knots[span];
      const double last = curve.knots[span + 1U];
      if (!(first < last))
        continue;
      const double secondDerivativeBound =
          spanSecondDerivativeBound(curve, span);
      for (std::size_t seed = 0U; seed < seedsPerSpan; ++seed) {
        const double lower = std::lerp(first, last,
                                       static_cast<double>(seed) /
                                           static_cast<double>(seedsPerSpan));
        const double upper = std::lerp(first, last,
                                       static_cast<double>(seed + 1U) /
                                           static_cast<double>(seedsPerSpan));
        seeds.push_back({lower, upper, evaluateNurbs(curve, lower),
                         evaluateNurbs(curve, upper), secondDerivativeBound});
      }
    }
    if (seeds.empty() || seeds.size() > maximumSegments)
      return std::unexpected(
          diagnostic("sketch.nurbs.tessellation-budget",
                     "NURBS tessellation exceeds its segment budget"));
    std::ranges::reverse(seeds);
    std::vector<Segment> stack = std::move(seeds);

    NurbsPolyline result{{stack.back().firstPoint}, 0.0, 0U, 0U, 0U};
    result.points.reserve(
        std::min(maximumSegments + 1U, stack.size() * 2U + 1U));
    std::size_t work = 0U;
    while (!stack.empty()) {
      if ((++work & 0xffU) == 0U && cancellation.stop_requested())
        return std::unexpected(diagnostic("sketch.nurbs.cancelled",
                                          "NURBS tessellation was cancelled"));
      const Segment segment = stack.back();
      stack.pop_back();
      const double middle = std::midpoint(segment.first, segment.last);
      const NurbsPoint middlePoint = evaluateNurbs(curve, middle);
      const double deviation = chordDeviationBound(
          segment.secondDerivativeBound, segment.first, segment.last);
      if (deviation <= maximumError) {
        result.points.push_back(segment.lastPoint);
        result.maximumCertifiedDeviation =
            std::max(result.maximumCertifiedDeviation, deviation);
        continue;
      }
      if (result.points.size() + stack.size() >= maximumSegments)
        return std::unexpected(
            diagnostic("sketch.nurbs.tessellation-budget",
                       "NURBS tessellation exceeds its segment budget"));
      stack.push_back({middle, segment.last, middlePoint, segment.lastPoint,
                       segment.secondDerivativeBound});
      stack.push_back({segment.first, middle, segment.firstPoint, middlePoint,
                       segment.secondDerivativeBound});
    }
    if (cancellation.stop_requested())
      return std::unexpected(diagnostic("sketch.nurbs.cancelled",
                                        "NURBS tessellation was cancelled"));
    result.retainedBytes = result.points.capacity() * sizeof(NurbsPoint);
    result.scratchBytes = stack.capacity() * sizeof(Segment);
    result.peakBytes = result.retainedBytes + result.scratchBytes;
    return result;
  } catch (const std::bad_alloc &) {
    return std::unexpected(diagnostic("sketch.nurbs.allocation-failed",
                                      "NURBS tessellation allocation failed"));
  }
}

} // namespace kearne::sketch
