#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace kearne::testkit {

struct Distribution {
  std::size_t samples;
  double minimum;
  double p50;
  double p95;
  double p99;
  double maximum;
  double mean;
  double populationStandardDeviation;
  bool operator==(const Distribution &) const = default;
};

[[nodiscard]] inline std::optional<Distribution>
summarizeDistribution(std::span<const double> samples) {
  if (samples.empty() || std::ranges::any_of(samples, [](double value) {
        return !std::isfinite(value);
      }))
    return std::nullopt;

  std::vector<double> ordered(samples.begin(), samples.end());
  std::ranges::sort(ordered);
  const auto percentile = [&ordered](std::size_t numerator,
                                     std::size_t denominator) {
    const std::size_t rank =
        (numerator * ordered.size() + denominator - 1) / denominator;
    return ordered[std::max<std::size_t>(rank, 1) - 1];
  };

  double mean = 0.0;
  double squaredDistance = 0.0;
  std::size_t count = 0;
  for (const double value : ordered) {
    ++count;
    const double delta = value - mean;
    mean += delta / static_cast<double>(count);
    squaredDistance += delta * (value - mean);
  }

  return Distribution{
      ordered.size(),
      ordered.front(),
      percentile(1, 2),
      percentile(95, 100),
      percentile(99, 100),
      ordered.back(),
      mean,
      std::sqrt(squaredDistance / static_cast<double>(ordered.size()))};
}

} // namespace kearne::testkit
