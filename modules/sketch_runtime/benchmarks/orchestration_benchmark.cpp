#include <kearne/sketch_runtime/runtime.hpp>
#include <kearne/testkit/distribution.hpp>

#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

using namespace kearne;
namespace model = kearne::sketch;
namespace render = kearne::render;
namespace runtime = kearne::sketch_runtime;

template <typename Id> Id id(std::uint64_t value) {
  typename Id::RandomTail random{};
  for (std::size_t index = 0; index < random.size(); ++index)
    random[index] = static_cast<std::uint8_t>(value >> ((index % 8U) * 8U));
  auto result = Id::create(value, random);
  if (!result)
    throw std::runtime_error("could not create benchmark ID");
  return *result;
}

template <typename Digest> Digest digest(std::uint8_t value) {
  typename Digest::Bytes bytes{};
  bytes.fill(value);
  auto result = Digest::fromBytes("blake3", bytes);
  if (!result)
    throw std::runtime_error("could not create benchmark digest");
  return *result;
}

model::LengthValue length(double value) {
  auto result = model::LengthValue::fromSi(value);
  if (!result)
    throw std::runtime_error("could not create benchmark length");
  return *result;
}

class ReturningSolver final : public model::Solver {
public:
  explicit ReturningSolver(model::SolveResult result)
      : result_(std::move(result)) {}

  Result<model::SolveResult> solve(const model::SolveInput &) const override {
    return result_;
  }

private:
  model::SolveResult result_;
};

runtime::SketchRequest benchmarkRequest(std::size_t entityCount) {
  model::Definition definition{digest<ContentDigest>(1), {}, {}};
  definition.entities.reserve(entityCount);
  for (std::size_t index = 0; index < entityCount; ++index) {
    const double x = static_cast<double>(index) * 0.002;
    definition.entities.push_back(
        model::LineEntity{id<SketchEntityId>(index + 1U),
                          {length(x), length(0.0)},
                          {length(x + 0.001), length(0.001)}});
  }
  auto session = render::RenderSessionHandle::create(1);
  auto generation = render::SceneGeneration::create(1);
  if (!session || !generation)
    throw std::runtime_error("could not create benchmark scene identity");
  runtime::EvaluationEvidence evidence{
      {{*session,
        {id<ModelBindingId>(2), digest<RevisionId>(3)},
        digest<render::EvaluationKey>(4)},
       *generation,
       digest<render::SceneDigest>(5)},
      definition.sourceDigest};
  return {std::move(definition),
          std::move(evidence),
          std::nullopt,
          std::nullopt,
          {},
          {}};
}

void run(std::size_t entityCount, std::size_t samples) {
  runtime::SketchRequest request = benchmarkRequest(entityCount);
  model::SolveResult result;
  result.status = model::SolveStatus::Underconstrained;
  result.geometry = request.definition.entities;
  result.degreesOfFreedom = entityCount * 4U;
  ReturningSolver solver{std::move(result)};

  const auto evaluate = [&] {
    auto outcome = runtime::evaluateSketch(request, solver);
    if (!outcome || !outcome->solve.solverResultValid ||
        !outcome->replacementScene ||
        outcome->replacementScene->primitives().size() != entityCount)
      throw std::runtime_error("benchmark orchestration result is incorrect");
  };
  for (std::size_t warmup = 0; warmup < 5; ++warmup)
    evaluate();

  std::vector<double> milliseconds;
  milliseconds.reserve(samples);
  for (std::size_t sample = 0; sample < samples; ++sample) {
    const auto start = std::chrono::steady_clock::now();
    evaluate();
    const auto end = std::chrono::steady_clock::now();
    milliseconds.push_back(
        std::chrono::duration<double, std::milli>(end - start).count());
  }
  const auto distribution = testkit::summarizeDistribution(milliseconds);
  if (!distribution)
    throw std::runtime_error("benchmark distribution is invalid");
  std::cout << std::fixed << std::setprecision(3) << "entities=" << entityCount
            << " samples=" << distribution->samples
            << " p50_ms=" << distribution->p50
            << " p95_ms=" << distribution->p95
            << " p99_ms=" << distribution->p99
            << " max_ms=" << distribution->maximum << '\n';
}

} // namespace

int main() {
  try {
    run(100, 50);
    run(1'000, 20);
    run(10'000, 7);
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
