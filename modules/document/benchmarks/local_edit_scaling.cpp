#include <kearne/document/project_state_access.hpp>
#include <kearne/testkit/distribution.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace kearne;
using namespace kearne::document;

template <typename Id> Id id(std::uint64_t value) {
  typename Id::RandomTail tail{};
  for (std::size_t index = 0; index < tail.size(); ++index)
    tail[index] = static_cast<std::uint8_t>(value >> ((index % 8U) * 8U));
  auto created = Id::create(value, tail);
  if (!created)
    throw std::runtime_error("benchmark identifier is invalid");
  return std::move(*created);
}

template <typename Digest> Digest digest(std::uint64_t value) {
  typename Digest::Bytes bytes{};
  for (std::size_t index = 0; index < bytes.size(); ++index)
    bytes[index] = static_cast<std::uint8_t>(value >> ((index % 8U) * 8U));
  auto created = Digest::fromBytes("blake3", bytes);
  if (!created)
    throw std::runtime_error("benchmark digest is invalid");
  return std::move(*created);
}

struct Fixture {
  ProjectState state;
  ProjectPath module;
  ContentEntry content;
};

Fixture fixture(std::size_t functionCount) {
  auto state =
      ProjectState::create(id<ProjectId>(1), digest<SchemaSetDigest>(1));
  auto module = ProjectPath::parse("models/scaling.py");
  if (!state || !module)
    throw std::runtime_error("benchmark project could not be initialized");
  ContentEntry content{digest<ContentDigest>(2), 32,
                       "text/x-python; charset=utf-8"};
  MutationBatch mutations;
  mutations.reserve(functionCount + 1U);
  mutations.emplace_back(PutContent{*module, std::nullopt, content});
  for (std::size_t index = 0; index < functionCount; ++index) {
    mutations.emplace_back(CreateFunction{
        ModelFunctionContract{id<ModelFunctionId>(index + 10U),
                              *module,
                              "function_" + std::to_string(index),
                              digest<EnvironmentDigest>(3),
                              digest<CapabilityProfileDigest>(4),
                              {},
                              {{id<ModelOutputId>(index + functionCount + 10U),
                                "result", ModelValueKind::Sketch}}}});
  }
  auto populated = internal::ProjectStateAccess::apply(*state, mutations);
  if (!populated || populated->functionCount() != functionCount)
    throw std::runtime_error("benchmark project population failed");
  return {std::move(*populated), std::move(*module), std::move(content)};
}

void run(std::size_t functionCount, std::size_t samples) {
  const Fixture base = fixture(functionCount);
  ContentEntry replacement{digest<ContentDigest>(5), 48,
                           "text/x-python; charset=utf-8"};
  const Mutation mutation =
      PutContent{base.module, base.content.digest, replacement};
  const auto edit = [&] {
    auto updated =
        internal::ProjectStateAccess::apply(base.state, {&mutation, 1});
    if (!updated || updated->functionCount() != functionCount ||
        updated->content(base.module) != replacement ||
        updated->rootDigest() == base.state.rootDigest())
      throw std::runtime_error("timed local edit produced the wrong state");
  };
  for (std::size_t warmup = 0; warmup < 5U; ++warmup)
    edit();

  std::vector<double> milliseconds;
  milliseconds.reserve(samples);
  for (std::size_t sample = 0; sample < samples; ++sample) {
    const auto start = std::chrono::steady_clock::now();
    edit();
    const auto end = std::chrono::steady_clock::now();
    milliseconds.push_back(
        std::chrono::duration<double, std::milli>(end - start).count());
  }
  auto distribution = testkit::summarizeDistribution(milliseconds);
  if (!distribution)
    throw std::runtime_error("benchmark distribution is invalid");
  std::cout << std::fixed << std::setprecision(3)
            << "functions=" << functionCount
            << " samples=" << distribution->samples
            << " p50_ms=" << distribution->p50
            << " p95_ms=" << distribution->p95
            << " p99_ms=" << distribution->p99
            << " max_ms=" << distribution->maximum
            << " stddev_ms=" << distribution->populationStandardDeviation
            << '\n';
}

} // namespace

int main() {
  try {
    run(100, 50);
    run(1'000, 25);
    run(10'000, 10);
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
