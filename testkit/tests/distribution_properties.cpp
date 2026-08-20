#include <kearne/testkit/distribution.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <random>
#include <span>
#include <vector>

namespace {

void require(bool condition, const char *message) {
  if (condition)
    return;
  std::cerr << message << '\n';
  std::exit(EXIT_FAILURE);
}

bool near(double left, double right) {
  const double scale = std::max({1.0, std::abs(left), std::abs(right)});
  return std::abs(left - right) <= 1.0e-10 * scale;
}

} // namespace

int main() {
  using kearne::testkit::summarizeDistribution;

  require(!summarizeDistribution({}), "empty series must be rejected");
  const std::array invalid{1.0, std::numeric_limits<double>::infinity()};
  require(!summarizeDistribution(invalid),
          "non-finite samples must be rejected");

  std::mt19937_64 random{0x4b6561726e65ULL};
  std::uniform_int_distribution<std::size_t> sizeDistribution{1, 2048};
  std::uniform_real_distribution<double> valueDistribution{-1.0e6, 1.0e6};
  std::uniform_real_distribution<double> scaleDistribution{0.01, 100.0};
  for (std::size_t trial = 0; trial < 250; ++trial) {
    std::vector<double> values(sizeDistribution(random));
    std::ranges::generate(values, [&] { return valueDistribution(random); });
    const auto summary = summarizeDistribution(values);
    require(summary.has_value(), "finite series must summarize");
    require(summary->samples == values.size(), "sample count must be exact");
    require(summary->minimum <= summary->p50 && summary->p50 <= summary->p95 &&
                summary->p95 <= summary->p99 &&
                summary->p99 <= summary->maximum,
            "percentiles must be ordered and bounded");
    require(summary->populationStandardDeviation >= 0.0,
            "variability must be non-negative");

    std::ranges::shuffle(values, random);
    const auto shuffled = summarizeDistribution(values);
    require(shuffled.has_value() && shuffled->samples == summary->samples &&
                shuffled->minimum == summary->minimum &&
                shuffled->p50 == summary->p50 &&
                shuffled->p95 == summary->p95 &&
                shuffled->p99 == summary->p99 &&
                shuffled->maximum == summary->maximum &&
                near(shuffled->mean, summary->mean) &&
                near(shuffled->populationStandardDeviation,
                     summary->populationStandardDeviation),
            "sample order must preserve the distribution");

    const double scale = scaleDistribution(random);
    const double offset = valueDistribution(random);
    std::ranges::transform(values, values.begin(), [=](double value) {
      return value * scale + offset;
    });
    const auto transformed = summarizeDistribution(values);
    require(transformed.has_value(), "finite affine series must summarize");
    require(near(transformed->minimum, summary->minimum * scale + offset) &&
                near(transformed->p50, summary->p50 * scale + offset) &&
                near(transformed->p95, summary->p95 * scale + offset) &&
                near(transformed->p99, summary->p99 * scale + offset) &&
                near(transformed->maximum, summary->maximum * scale + offset) &&
                near(transformed->mean, summary->mean * scale + offset) &&
                near(transformed->populationStandardDeviation,
                     summary->populationStandardDeviation * scale),
            "positive affine transforms must preserve distribution relations");
  }
}
