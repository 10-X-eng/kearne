#include "scene_generator.hpp"

#include <kearne/testkit/distribution.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <new>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {
struct AllocationGate {
  std::atomic<bool> reached = false;
  std::atomic<bool> release = false;
};
thread_local AllocationGate *allocationGate = nullptr;

#if defined(__GNUC__) || defined(__clang__)
[[gnu::noinline]]
#endif
void *allocateMemory(std::size_t size) {
  return std::malloc(size == 0 ? 1U : size);
}

#if defined(__GNUC__) || defined(__clang__)
[[gnu::noinline]]
#endif
void releaseMemory(void *memory) {
  std::free(memory);
}
} // namespace

void *operator new(std::size_t size) {
  if (allocationGate) {
    AllocationGate *gate = std::exchange(allocationGate, nullptr);
    gate->reached.store(true, std::memory_order_release);
    while (!gate->release.load(std::memory_order_acquire))
      std::this_thread::yield();
  }
  if (void *memory = allocateMemory(size))
    return memory;
  throw std::bad_alloc{};
}

void *operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void *memory) noexcept { releaseMemory(memory); }
void operator delete[](void *memory) noexcept { releaseMemory(memory); }
void operator delete(void *memory, std::size_t) noexcept {
  releaseMemory(memory);
}
void operator delete[](void *memory, std::size_t) noexcept {
  releaseMemory(memory);
}

namespace {

using namespace kearne::render;
using namespace kearne::render::test;

using Clock = std::chrono::steady_clock;

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

std::string_view profileName(PickSceneProfile profile) {
  switch (profile) {
  case PickSceneProfile::Sparse:
    return "sparse";
  case PickSceneProfile::Coincident:
    return "coincident";
  case PickSceneProfile::Concentric:
    return "concentric";
  case PickSceneProfile::GlobalLines:
    return "global-lines";
  case PickSceneProfile::Outlier:
    return "outlier";
  }
  throw std::runtime_error("pick profile is invalid");
}

std::vector<SketchPickQuery>
profileQueries(const SketchSceneSnapshot &generated, PickSceneProfile profile,
               std::size_t count) {
  if (profile == PickSceneProfile::Sparse)
    return queries(generated, count, 91);
  std::vector<SketchPickQuery> result;
  result.reserve(count);
  const std::size_t primitiveCount = generated.primitives().size();
  const std::size_t columns = std::max<std::size_t>(
      1, static_cast<std::size_t>(std::ceil(std::sqrt(primitiveCount))));
  for (std::size_t index = 0; index < count; ++index) {
    switch (profile) {
    case PickSceneProfile::Coincident:
      result.push_back({{0.0, 0.0}, 0.0, SketchPickTargets::Points});
      break;
    case PickSceneProfile::Concentric: {
      const double radius =
          1.0 + static_cast<double>(index % primitiveCount) * 1.0e-6;
      result.push_back({{radius, 0.0}, 1.0e-9, SketchPickTargets::Curves});
      break;
    }
    case PickSceneProfile::GlobalLines: {
      const double y = static_cast<double>(index % primitiveCount) * 1.0e-6;
      result.push_back({{0.0, y}, 1.0e-9, SketchPickTargets::Curves});
      break;
    }
    case PickSceneProfile::Outlier: {
      const std::size_t ordinal =
          index % std::max<std::size_t>(1, primitiveCount - 1U);
      result.push_back({{static_cast<double>(ordinal % columns) * 1.0e-4,
                         static_cast<double>(ordinal / columns) * 1.0e-4},
                        0.0,
                        SketchPickTargets::Points});
      break;
    }
    case PickSceneProfile::Sparse:
      break;
    }
  }
  return result;
}

void reportQueries(std::size_t primitiveCount, std::string_view profile,
                   const SketchPickIndex &index,
                   const std::vector<SketchPickQuery> &queries) {
  std::vector<double> microseconds;
  std::vector<double> visited;
  std::vector<double> refined;
  std::vector<double> passes;
  microseconds.reserve(queries.size());
  visited.reserve(queries.size());
  refined.reserve(queries.size());
  passes.reserve(queries.size());
  std::size_t hits = 0;
  std::size_t misses = 0;
  std::size_t refusals = 0;
  std::uint64_t checksum = 0;
  for (const SketchPickQuery &query : queries) {
    const auto start = Clock::now();
    const SketchPickOutcome outcome = index.query(query);
    const auto finish = Clock::now();
    require(outcome.status != SketchPickStatus::InvalidQuery &&
                outcome.status != SketchPickStatus::NonFiniteArithmetic,
            "timed query was invalid or nonrepresentable");
    require((outcome.status != SketchPickStatus::WorkBudgetExceeded) ||
                !outcome.result,
            "timed refusal returned a partial result");
    microseconds.push_back(
        std::chrono::duration<double, std::micro>(finish - start).count());
    visited.push_back(static_cast<double>(outcome.metrics.visitedNodes));
    refined.push_back(static_cast<double>(outcome.metrics.refinedTargets));
    passes.push_back(static_cast<double>(outcome.metrics.passes));
    if (outcome.status == SketchPickStatus::Hit) {
      ++hits;
      checksum += outcome.result->primitive.value();
    } else if (outcome.status == SketchPickStatus::Miss) {
      ++misses;
    } else {
      ++refusals;
    }
  }
  const auto time = kearne::testkit::summarizeDistribution(microseconds);
  const auto nodes = kearne::testkit::summarizeDistribution(visited);
  const auto targets = kearne::testkit::summarizeDistribution(refined);
  const auto passCounts = kearne::testkit::summarizeDistribution(passes);
  require(time && nodes && targets && passCounts,
          "query distribution summary failed");
  std::cout << "query," << primitiveCount << ',' << profile << ','
            << queries.size() << ",us," << std::fixed << std::setprecision(3)
            << time->p50 << ',' << time->p95 << ',' << time->p99 << ','
            << time->maximum << ',' << index.targetCount() << ','
            << index.leafCount() << ',' << index.nodeCount() << ','
            << index.retainedBytes() << ',' << index.scratchBytes() << ','
            << index.peakBuildBytes() << ',' << nodes->p95 << ','
            << nodes->maximum << ',' << targets->p95 << ',' << targets->maximum
            << ',' << passCounts->p95 << ',' << passCounts->maximum << ','
            << hits << ',' << misses << ',' << refusals << ',' << checksum
            << '\n';
}

double cancellationLatencyMilliseconds(
    const std::shared_ptr<const SketchSceneSnapshot> &generated) {
  std::stop_source source;
  AllocationGate gate;
  std::optional<kearne::Result<SketchPickIndex>> result;
  std::thread builder([&] {
    allocationGate = &gate;
    result.emplace(SketchPickIndex::build(generated, {}, source.get_token()));
  });
  while (!gate.reached.load(std::memory_order_acquire))
    std::this_thread::yield();
  const auto start = Clock::now();
  source.request_stop();
  gate.release.store(true, std::memory_order_release);
  builder.join();
  const auto finish = Clock::now();
  require(result && !*result && result->error().code == "render.pick.cancelled",
          "timed cancellation published an index");
  return std::chrono::duration<double, std::milli>(finish - start).count();
}

void benchmark(std::size_t primitiveCount, PickSceneProfile profile) {
  auto generated =
      pickScene(primitiveCount, profile, primitiveCount,
                stamp(1, primitiveCount, 1, 1,
                      static_cast<std::uint64_t>(profile), primitiveCount));
  auto validated = SketchPickIndex::build(generated);
  require(validated.has_value(), "benchmark validation build failed");
  require(validated->targetCount() <= primitiveCount * 4U &&
              validated->peakBuildBytes() ==
                  validated->retainedBytes() + validated->scratchBytes(),
          "benchmark index violated its target or byte envelope");

  const auto validationQueries = profileQueries(*generated, profile, 1);
  const SketchPickOutcome validation =
      validated->query(validationQueries.front());
  if (validation.status == SketchPickStatus::Hit ||
      validation.status == SketchPickStatus::Miss)
    requireEquivalent(validation.result,
                      bruteForcePick(*generated, validationQueries.front()));
  else
    require(validation.status == SketchPickStatus::WorkBudgetExceeded &&
                !validation.result,
            "benchmark validation returned an invalid refusal");

  const std::size_t buildSamples = primitiveCount == 1'000U    ? 11U
                                   : primitiveCount == 10'000U ? 7U
                                                               : 3U;
  std::vector<double> buildMilliseconds;
  buildMilliseconds.reserve(buildSamples);
  std::uint64_t checksum = 0;
  for (std::size_t trial = 0; trial < buildSamples; ++trial) {
    const auto start = Clock::now();
    auto index = SketchPickIndex::build(generated);
    const auto finish = Clock::now();
    require(index.has_value(), "timed index build failed");
    buildMilliseconds.push_back(
        std::chrono::duration<double, std::milli>(finish - start).count());
    checksum += index->nodeCount() + index->targetCount();
  }
  const auto build = kearne::testkit::summarizeDistribution(buildMilliseconds);
  require(build.has_value(), "build distribution summary failed");
  std::cout << "build," << primitiveCount << ',' << profileName(profile) << ','
            << buildSamples << ",ms," << std::fixed << std::setprecision(3)
            << build->p50 << ',' << build->p95 << ',' << build->p99 << ','
            << build->maximum << ',' << validated->targetCount() << ','
            << validated->leafCount() << ',' << validated->nodeCount() << ','
            << validated->retainedBytes() << ',' << validated->scratchBytes()
            << ',' << validated->peakBuildBytes() << ",0,0,0,0,0,0,0,0,0,"
            << checksum << '\n';

  reportQueries(primitiveCount, profileName(profile), *validated,
                profileQueries(*generated, profile, 2'000));
  if (profile == PickSceneProfile::Sparse) {
    const Bounds2d bounds = generated->bounds();
    const Point2d center{std::midpoint(bounds.minimum.x, bounds.maximum.x),
                         std::midpoint(bounds.minimum.y, bounds.maximum.y)};
    const double tolerance = std::max(bounds.maximum.x - bounds.minimum.x,
                                      bounds.maximum.y - bounds.minimum.y) +
                             1.0;
    reportQueries(primitiveCount, "wide-tolerance", *validated,
                  std::vector<SketchPickQuery>(
                      2'000, {center, tolerance, SketchPickTargets::All}));
    reportQueries(primitiveCount, "outside", *validated,
                  std::vector<SketchPickQuery>(
                      2'000, {{bounds.maximum.x + tolerance * 2.0,
                               bounds.maximum.y + tolerance * 2.0},
                              0.0,
                              SketchPickTargets::All}));
    const double cancellation = cancellationLatencyMilliseconds(generated);
    std::cout << "cancel," << primitiveCount << ",sparse,1,ms," << std::fixed
              << std::setprecision(3) << cancellation << ',' << cancellation
              << ',' << cancellation << ',' << cancellation
              << ",0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0\n";
  }
}

} // namespace

int main() {
  try {
    std::cout
        << "record,primitives,profile,samples,unit,p50,p95,p99,max,targets,"
           "leaves,nodes,retained_payload_bytes,scratch_payload_bytes,"
           "peak_build_payload_bytes,visited_p95,visited_max,refined_p95,"
           "refined_max,passes_p95,passes_max,hits,misses,refusals,checksum\n";
    for (const std::size_t size : std::array{1'000U, 10'000U, 100'000U}) {
      for (const PickSceneProfile profile : {
               PickSceneProfile::Sparse,
               PickSceneProfile::Coincident,
               PickSceneProfile::Concentric,
               PickSceneProfile::GlobalLines,
               PickSceneProfile::Outlier,
           })
        benchmark(size, profile);
    }
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
