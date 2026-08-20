#include <kearne/adapters/ceres_sketch_solver.hpp>
#include <kearne/testkit/distribution.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace {

namespace model = kearne::sketch;
using kearne::ContentDigest;
using kearne::Length;
using kearne::Quantity;
using kearne::SketchConstraintId;
using kearne::SketchEntityId;
using kearne::adapters::CeresSketchSolver;

template <typename Id> Id id(std::uint64_t value) {
  typename Id::RandomTail random{};
  for (std::size_t index = 0; index < random.size(); ++index)
    random[index] = static_cast<std::uint8_t>(value >> ((index % 8U) * 8U));
  auto result = Id::create(value & ((std::uint64_t{1} << 48U) - 1U), random);
  if (!result)
    throw std::runtime_error("could not create benchmark ID");
  return *result;
}

model::LengthValue length(double value) {
  auto result = Quantity<Length>::fromSi(value);
  if (!result)
    throw std::runtime_error("invalid benchmark length");
  return *result;
}

model::Point2 point(double x, double y) { return {length(x), length(y)}; }

model::SolveInput dimensionedPointChain(std::size_t count) {
  ContentDigest::Bytes bytes{};
  auto digest = ContentDigest::fromBytes("blake3-256", bytes);
  if (!digest)
    throw std::runtime_error("could not create benchmark digest");
  model::Definition definition{*digest, {}, {}};
  definition.entities.reserve(count);
  definition.constraints.reserve(count * 2U);
  for (std::size_t index = 0; index < count; ++index) {
    const std::uint64_t value = static_cast<std::uint64_t>(index);
    const SketchEntityId entity = id<SketchEntityId>(value + 1);
    definition.entities.push_back(model::PointEntity{
        entity, point(static_cast<double>(index) * 0.001, 0.0)});
    if (index == 0) {
      definition.constraints.push_back(model::Fixed{
          id<SketchConstraintId>(static_cast<std::uint64_t>(count) + 1),
          entity});
      continue;
    }
    const SketchEntityId previous =
        id<SketchEntityId>(static_cast<std::uint64_t>(index));
    const std::uint64_t constraint =
        static_cast<std::uint64_t>(count) + value * 2U;
    definition.constraints.push_back(
        model::HorizontalDistance{id<SketchConstraintId>(constraint),
                                  {previous, model::PointKey::Point},
                                  {entity, model::PointKey::Point},
                                  length(0.001)});
    definition.constraints.push_back(
        model::VerticalDistance{id<SketchConstraintId>(constraint + 1),
                                {previous, model::PointKey::Point},
                                {entity, model::PointKey::Point},
                                length(0.0)});
  }
  std::vector<model::Entity> prior = definition.entities;
  for (std::size_t index = 0; index < prior.size(); ++index) {
    auto &selected = std::get<model::PointEntity>(prior[index]);
    selected.point =
        point(selected.point.x.si() +
                  std::sin(static_cast<double>(index) * 0.1) * 0.0002,
              std::cos(static_cast<double>(index) * 0.07) * 0.0002);
  }
  return {std::move(definition), std::move(prior), std::nullopt, {}, {}};
}

void measure(std::size_t count, std::size_t repetitions) {
  CeresSketchSolver solver;
  model::SolveInput input = dimensionedPointChain(count);
  auto warm = solver.solve(input);
  if (!warm || warm->status != model::SolveStatus::Solved)
    throw std::runtime_error("benchmark warmup failed");

  std::vector<double> milliseconds;
  milliseconds.reserve(repetitions);
  for (std::size_t repetition = 0; repetition < repetitions; ++repetition) {
    const auto started = std::chrono::steady_clock::now();
    auto result = solver.solve(input);
    const auto finished = std::chrono::steady_clock::now();
    if (!result || result->status != model::SolveStatus::Solved ||
        result->degreesOfFreedom != 0)
      throw std::runtime_error("benchmark solve failed");
    milliseconds.push_back(
        std::chrono::duration<double, std::milli>(finished - started).count());
  }
  const auto distribution =
      kearne::testkit::summarizeDistribution(milliseconds);
  if (!distribution)
    throw std::runtime_error("benchmark distribution failed");
  std::printf("entities=%zu constraints=%zu iterations=%u repetitions=%zu "
              "p50_ms=%.3f p95_ms=%.3f p99_ms=%.3f max_ms=%.3f "
              "stddev_ms=%.3f\n",
              count, input.definition.constraints.size(), warm->iterations,
              repetitions, distribution->p50, distribution->p95,
              distribution->p99, distribution->maximum,
              distribution->populationStandardDeviation);
}

} // namespace

int main() {
  try {
    measure(100, 11);
    measure(1'000, 7);
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
  }
}
