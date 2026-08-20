#include "../tests/definition_generator.hpp"

#include <kearne/adapters/sketch_wire.hpp>
#include <kearne/testkit/distribution.hpp>

#include <chrono>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

template <typename Operation>
kearne::testkit::Distribution measure(Operation operation,
                                      std::size_t samples) {
  std::vector<double> milliseconds;
  milliseconds.reserve(samples);
  for (std::size_t sample = 0; sample < samples + 5; ++sample) {
    const auto start = Clock::now();
    operation();
    const auto end = Clock::now();
    if (sample >= 5)
      milliseconds.push_back(
          std::chrono::duration<double, std::milli>(end - start).count());
  }
  auto result = kearne::testkit::summarizeDistribution(milliseconds);
  if (!result)
    throw std::runtime_error("benchmark produced an invalid distribution");
  return *result;
}

void print(std::size_t entities, std::size_t constraints,
           std::string_view boundary,
           const kearne::testkit::Distribution &distribution) {
  std::cout << entities << " entities " << constraints << " constraints "
            << boundary << " p50=" << std::fixed << std::setprecision(3)
            << distribution.p50 << "ms p95=" << distribution.p95
            << "ms p99=" << distribution.p99
            << "ms max=" << distribution.maximum
            << "ms stddev=" << distribution.populationStandardDeviation
            << "ms\n";
}

void benchmark(std::size_t entities, std::size_t samples) {
  const auto definition = kearne::adapters::test::lineDefinition(entities);
  kearne::api::v1::SketchDefinition wire;
  if (!kearne::adapters::writeSketchDefinition(definition, &wire))
    throw std::runtime_error("benchmark definition did not convert");
  const std::string bytes = wire.SerializeAsString();
  const auto recovered = kearne::adapters::readSketchDefinition(wire);
  if (!recovered || *recovered != definition)
    throw std::runtime_error(
        "benchmark wire conversion changed the definition");
  const auto byteSpan = std::as_bytes(std::span{bytes.data(), bytes.size()});
  const auto parsed = kearne::adapters::parseSketchDefinition(byteSpan);
  if (!parsed || *parsed != definition)
    throw std::runtime_error("benchmark parse changed the definition");
  const std::size_t constraints = definition.constraints.size();

  print(entities, constraints, "domain-to-wire conversion",
        measure(
            [&] {
              kearne::api::v1::SketchDefinition output;
              if (!kearne::adapters::writeSketchDefinition(definition, &output))
                throw std::runtime_error("conversion failed");
            },
            samples));
  print(entities, constraints, "protobuf serialization only",
        measure(
            [&] {
              std::string output;
              if (!wire.SerializeToString(&output))
                throw std::runtime_error("serialization failed");
            },
            samples));
  print(entities, constraints, "protobuf parse only",
        measure(
            [&] {
              kearne::api::v1::SketchDefinition output;
              if (!output.ParseFromString(bytes))
                throw std::runtime_error("parse failed");
            },
            samples));
  print(entities, constraints, "wire-to-domain conversion and validation",
        measure(
            [&] {
              if (!kearne::adapters::readSketchDefinition(wire))
                throw std::runtime_error("conversion failed");
            },
            samples));
}

} // namespace

int main() {
  benchmark(100, 101);
  benchmark(1000, 31);
  benchmark(10'000, 11);
  return 0;
}
