#include <kearne/sketch_runtime/runtime.hpp>
#include <kearne/testkit/property.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numbers>
#include <ranges>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

using namespace kearne;
namespace model = kearne::sketch;
namespace render = kearne::render;
namespace runtime = kearne::sketch_runtime;

void require(bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}

template <typename Id> Id id(std::uint64_t value) {
  typename Id::RandomTail random{};
  for (std::size_t index = 0; index < random.size(); ++index)
    random[index] = static_cast<std::uint8_t>(value >> ((index % 8U) * 8U));
  auto result = Id::create(value & ((std::uint64_t{1} << 48U) - 1U), random);
  require(result.has_value(), "could not create generated ID");
  return *result;
}

template <typename Digest> Digest digest(std::uint64_t value) {
  typename Digest::Bytes bytes{};
  for (std::size_t index = 0; index < bytes.size(); ++index)
    bytes[index] = static_cast<std::uint8_t>(value + index * 29U);
  auto result = Digest::fromBytes("blake3", bytes);
  require(result.has_value(), "could not create generated digest");
  return *result;
}

template <typename Dimension>
Quantity<Dimension> quantity(double value, bool throughMillimetres = false) {
  auto result = throughMillimetres
                    ? Quantity<Dimension>::fromUnit(value * 1'000.0, 0.001)
                    : Quantity<Dimension>::fromSi(value);
  require(result.has_value(), "could not create generated quantity");
  return *result;
}

model::Point2 point(double x, double y, bool throughMillimetres = false) {
  return {quantity<Length>(x, throughMillimetres),
          quantity<Length>(y, throughMillimetres)};
}

runtime::EvaluationEvidence evidence(std::uint64_t seed, ContentDigest source) {
  auto session = render::RenderSessionHandle::create(seed + 1U);
  auto generation = render::SceneGeneration::create(seed + 1U);
  require(session && generation, "could not create scene identity");
  return {{{*session,
            {id<ModelBindingId>(seed + 2U), digest<RevisionId>(seed + 3U)},
            digest<render::EvaluationKey>(seed + 4U)},
           *generation,
           digest<render::SceneDigest>(seed + 5U)},
          std::move(source)};
}

model::Definition definition(std::uint64_t seed, double x, double y,
                             double scale, bool throughMillimetres = false) {
  const SketchEntityId pointId = id<SketchEntityId>(seed + 1U);
  const SketchEntityId lineId = id<SketchEntityId>(seed + 2U);
  const SketchEntityId circleId = id<SketchEntityId>(seed + 3U);
  const SketchEntityId arcId = id<SketchEntityId>(seed + 4U);
  return {
      digest<ContentDigest>(seed),
      {},
      {model::PointEntity{pointId, point(x, y, throughMillimetres)},
       model::LineEntity{lineId, point(x, y, throughMillimetres),
                         point(x + scale, y, throughMillimetres)},
       model::CircleEntity{circleId,
                           point(x + scale * 2.0, y, throughMillimetres),
                           quantity<Length>(scale * 0.25, throughMillimetres)},
       model::ArcEntity{arcId, point(x + scale * 3.0, y, throughMillimetres),
                        quantity<Length>(scale * 0.5, throughMillimetres),
                        quantity<Angle>(-std::numbers::pi / 3.0),
                        quantity<Angle>(std::numbers::pi / 2.0)}},
      {model::Horizontal{id<SketchConstraintId>(seed + 11U), lineId}}};
}

runtime::SketchRequest request(model::Definition value, std::uint64_t seed) {
  runtime::EvaluationEvidence identity = evidence(seed, value.sourceDigest);
  return {std::move(value),
          std::move(identity),
          std::nullopt,
          std::nullopt,
          {},
          {}};
}

model::SolveResult resultFor(const model::Definition &definition,
                             model::SolveStatus status,
                             std::size_t degreesOfFreedom,
                             std::vector<model::Entity> geometry = {}) {
  model::SolveResult result;
  result.status = status;
  result.degreesOfFreedom = degreesOfFreedom;
  if (status == model::SolveStatus::Cancelled)
    return result;
  result.geometry =
      geometry.empty() ? definition.entities : std::move(geometry);
  auto residuals = model::evaluateResiduals(definition, result.geometry, {});
  require(residuals.has_value(), "generated solver result is invalid");
  result.residuals = std::move(*residuals);
  return result;
}

class ScriptedSolver final : public model::Solver {
public:
  explicit ScriptedSolver(model::SolveResult result,
                          CancellationSource *cancelDuringSolve = nullptr)
      : result_(std::move(result)), cancelDuringSolve_(cancelDuringSolve) {}

  Result<model::SolveResult> solve(const model::SolveInput &) const override {
    ++calls;
    if (cancelDuringSolve_)
      cancelDuringSolve_->request_stop();
    return result_;
  }

  mutable std::size_t calls = 0;

private:
  model::SolveResult result_;
  CancellationSource *cancelDuringSolve_;
};

void requireWithheld(const runtime::SketchEvaluation &evaluation,
                     const char *message) {
  require(!evaluation.replacementScene, message);
}

void requireInvalidSolverEvidence(const runtime::SketchEvaluation &evaluation) {
  requireWithheld(evaluation, "invalid solver evidence published a scene");
  require(!evaluation.solve.solverResultValid,
          "adversarial solver evidence was trusted");
  require(std::ranges::any_of(evaluation.diagnostics,
                              [](const Diagnostic &item) {
                                return item.code ==
                                       "sketch.runtime.invalid-solver-result";
                              }),
          "invalid solver evidence lacks a runtime diagnostic");
}

bool near(double first, double second) {
  return std::abs(first - second) <=
         1.0e-12 * std::max({1.0, std::abs(first), std::abs(second)});
}

void verifyRuntimeContract(const testkit::PropertyProfile &profile) {
  testkit::checkProperty(
      "sketch runtime orchestration model", profile,
      [](testkit::Random &random, std::uint64_t index) {
        const std::uint64_t seed = index * 32U + 1'000U;
        const double x = random.between(-10.0, 10.0);
        const double y = random.between(-10.0, 10.0);
        const double scale = random.between(0.01, 5.0);
        model::Definition source = definition(seed, x, y, scale);
        runtime::SketchRequest input = request(source, seed + 100U);
        model::SolveResult honest =
            resultFor(source, model::SolveStatus::Underconstrained, 1U);

        switch (index % 32U) {
        case 0: {
          ScriptedSolver solver{honest};
          auto evaluated = runtime::evaluateSketch(input, solver);
          require(evaluated && evaluated->replacementScene &&
                      evaluated->solve.solverResultValid && solver.calls == 1U,
                  "honest underconstrained solve was not published");
          require(evaluated->replacementScene->stamp() == input.evidence.scene,
                  "published scene lost evaluation identity");
          break;
        }
        case 1: {
          ScriptedSolver solver{
              resultFor(source, model::SolveStatus::Solved, 0U)};
          auto evaluated = runtime::evaluateSketch(input, solver);
          require(evaluated && evaluated->replacementScene &&
                      evaluated->solve.status == model::SolveStatus::Solved,
                  "validated solved result was not published");
          break;
        }
        case 2: {
          std::ranges::reverse(honest.geometry);
          ScriptedSolver solver{honest};
          auto evaluated = runtime::evaluateSketch(input, solver);
          require(evaluated && evaluated->replacementScene,
                  "reordered solution was rejected");
          for (std::size_t entity = 0; entity < source.entities.size();
               ++entity)
            require(evaluated->replacementScene->primitives()[entity].entity ==
                        model::entityId(source.entities[entity]),
                    "publication order depends on solver ordering");
          break;
        }
        case 3: {
          honest.geometry.pop_back();
          ScriptedSolver solver{honest};
          auto evaluated = runtime::evaluateSketch(input, solver);
          require(evaluated.has_value(),
                  "dropped-ID result escaped the outcome boundary");
          requireInvalidSolverEvidence(*evaluated);
          break;
        }
        case 4: {
          std::get<model::PointEntity>(honest.geometry.front()).id =
              id<SketchEntityId>(seed + 900U);
          ScriptedSolver solver{honest};
          auto evaluated = runtime::evaluateSketch(input, solver);
          require(evaluated.has_value(),
                  "changed-ID result escaped the outcome boundary");
          requireInvalidSolverEvidence(*evaluated);
          break;
        }
        case 5: {
          honest.residuals.front().normalizedMaximum = 0.5;
          honest.residuals.front().satisfied = true;
          ScriptedSolver solver{honest};
          auto evaluated = runtime::evaluateSketch(input, solver);
          require(evaluated.has_value(),
                  "false residual escaped the outcome boundary");
          requireInvalidSolverEvidence(*evaluated);
          break;
        }
        case 6: {
          auto unsatisfied = source.entities;
          auto &line = std::get<model::LineEntity>(unsatisfied[1]);
          line.end.y = quantity<Length>(line.end.y.si() + scale);
          model::SolveResult falseStatus =
              resultFor(source, model::SolveStatus::Underconstrained, 1U,
                        std::move(unsatisfied));
          ScriptedSolver solver{std::move(falseStatus)};
          auto evaluated = runtime::evaluateSketch(input, solver);
          require(evaluated.has_value(),
                  "false status escaped the outcome boundary");
          requireInvalidSolverEvidence(*evaluated);
          break;
        }
        case 7: {
          auto unsatisfied = source.entities;
          auto &line = std::get<model::LineEntity>(unsatisfied[1]);
          line.end.y = quantity<Length>(line.end.y.si() + scale);
          ScriptedSolver solver{resultFor(source,
                                          model::SolveStatus::Inconsistent, 1U,
                                          std::move(unsatisfied))};
          auto evaluated = runtime::evaluateSketch(input, solver);
          require(evaluated && evaluated->solve.solverResultValid,
                  "honest inconsistent result was rejected");
          requireWithheld(*evaluated,
                          "inconsistent result replaced last-known-good scene");
          break;
        }
        case 8: {
          ScriptedSolver solver{
              resultFor(source, model::SolveStatus::Diverged, 1U)};
          auto evaluated = runtime::evaluateSketch(input, solver);
          require(evaluated && evaluated->solve.solverResultValid,
                  "well-formed divergence evidence was rejected");
          requireWithheld(*evaluated,
                          "diverged result replaced last-known-good scene");
          break;
        }
        case 9: {
          CancellationSource cancellation;
          cancellation.request_stop();
          input.cancellation = cancellation.get_token();
          ScriptedSolver solver{
              resultFor(source, model::SolveStatus::Cancelled, 0U)};
          auto evaluated = runtime::evaluateSketch(input, solver);
          require(evaluated && evaluated->solve.solverResultValid &&
                      evaluated->solve.cancellationObserved,
                  "requested cancellation evidence was rejected");
          requireWithheld(*evaluated,
                          "cancelled result replaced last-known-good scene");
          break;
        }
        case 10: {
          ScriptedSolver solver{
              resultFor(source, model::SolveStatus::Cancelled, 0U)};
          auto evaluated = runtime::evaluateSketch(input, solver);
          require(evaluated.has_value(),
                  "false cancellation escaped outcome boundary");
          requireInvalidSolverEvidence(*evaluated);
          break;
        }
        case 11:
        case 12: {
          ScriptedSolver solver{honest};
          auto evaluated = runtime::evaluateSketch(input, solver);
          require(evaluated && evaluated->solve.solverResultValid &&
                      evaluated->replacementScene &&
                      std::ranges::none_of(
                          evaluated->replacementScene->primitives(),
                          [&](const render::PackedSketchPrimitive &primitive) {
                            const render::SketchStyleRole role =
                                evaluated->replacementScene
                                    ->styles()[primitive.style]
                                    .role;
                            return role == render::SketchStyleRole::Selected ||
                                   role == render::SketchStyleRole::Preview;
                          }),
                  "base evaluation published presentation overlay styles");
          break;
        }
        case 13: {
          input.evidence.sourceDigest = digest<ContentDigest>(seed + 800U);
          ScriptedSolver solver{honest};
          auto evaluated = runtime::evaluateSketch(input, solver);
          require(!evaluated &&
                      evaluated.error().code == "sketch.runtime.stale-source" &&
                      solver.calls == 0U,
                  "stale source identity reached the solver");
          break;
        }
        case 14: {
          honest.residuals.front().constraint =
              id<SketchConstraintId>(seed + 700U);
          ScriptedSolver solver{honest};
          auto evaluated = runtime::evaluateSketch(input, solver);
          require(evaluated.has_value(),
                  "changed residual ID escaped outcome boundary");
          requireInvalidSolverEvidence(*evaluated);
          break;
        }
        case 15: {
          honest.modes = {model::FreedomMode{{model::ModeComponent{
              id<SketchEntityId>(seed + 700U), {1.0, 0.0}}}}};
          ScriptedSolver solver{honest};
          auto evaluated = runtime::evaluateSketch(input, solver);
          require(evaluated.has_value(),
                  "changed mode ID escaped outcome boundary");
          requireInvalidSolverEvidence(*evaluated);
          break;
        }
        case 16: {
          CancellationSource cancellation;
          input.cancellation = cancellation.get_token();
          ScriptedSolver solver{honest, &cancellation};
          auto evaluated = runtime::evaluateSketch(input, solver);
          require(evaluated && evaluated->solve.solverResultValid &&
                      evaluated->solve.cancellationObserved,
                  "post-solve cancellation lost solve evidence");
          requireWithheld(*evaluated,
                          "post-solve cancellation published a scene");
          break;
        }
        case 17: {
          model::Definition equivalent = definition(seed, x, y, scale, true);
          runtime::SketchRequest equivalentInput =
              request(equivalent, seed + 200U);
          ScriptedSolver first{honest};
          ScriptedSolver second{
              resultFor(equivalent, model::SolveStatus::Underconstrained, 1U)};
          auto firstEvaluation = runtime::evaluateSketch(input, first);
          auto secondEvaluation =
              runtime::evaluateSketch(equivalentInput, second);
          require(firstEvaluation && secondEvaluation &&
                      firstEvaluation->replacementScene &&
                      secondEvaluation->replacementScene,
                  "unit-equivalent geometry was not published");
          require(
              std::ranges::equal(
                  firstEvaluation->replacementScene->points(),
                  secondEvaluation->replacementScene->points(),
                  [](render::Point2d firstPoint, render::Point2d secondPoint) {
                    return near(firstPoint.x, secondPoint.x) &&
                           near(firstPoint.y, secondPoint.y);
                  }),
              "unit-equivalent inputs changed projected geometry");
          break;
        }
        case 18: {
          std::vector<model::Entity> stalePrior = source.entities;
          std::get<model::PointEntity>(stalePrior.front()).id =
              id<SketchEntityId>(seed + 600U);
          input.priorSolution = std::move(stalePrior);
          ScriptedSolver solver{honest};
          auto evaluated = runtime::evaluateSketch(input, solver);
          require(!evaluated && solver.calls == 0U,
                  "invalid prior solution reached the solver");
          break;
        }
        case 19: {
          input.drag = model::DragTarget{
              {model::entityId(source.entities.front()), model::PointKey::End},
              point(x, y)};
          ScriptedSolver solver{honest};
          auto evaluated = runtime::evaluateSketch(input, solver);
          require(!evaluated && solver.calls == 0U,
                  "invalid drag reference reached the solver");
          break;
        }
        case 20: {
          ScriptedSolver first{honest};
          auto firstEvaluation = runtime::evaluateSketch(input, first);
          std::ranges::reverse(honest.geometry);
          ScriptedSolver second{honest};
          auto secondEvaluation = runtime::evaluateSketch(input, second);
          require(firstEvaluation && secondEvaluation &&
                      firstEvaluation->replacementScene &&
                      secondEvaluation->replacementScene &&
                      std::ranges::equal(
                          firstEvaluation->replacementScene->points(),
                          secondEvaluation->replacementScene->points()) &&
                      std::ranges::equal(
                          firstEvaluation->replacementScene->primitives(),
                          secondEvaluation->replacementScene->primitives()),
                  "publication is nondeterministic under solver reordering");
          break;
        }
        case 21: {
          honest.modes = {model::FreedomMode{{model::ModeComponent{
              model::entityId(source.entities.front()), {0.0, 0.0}}}}};
          ScriptedSolver solver{honest};
          auto evaluated = runtime::evaluateSketch(input, solver);
          require(evaluated.has_value(),
                  "zero freedom mode escaped outcome boundary");
          requireInvalidSolverEvidence(*evaluated);
          break;
        }
        case 22: {
          const model::FreedomMode mode{{
              {model::entityId(source.entities.front()), {1.0, 0.0}},
              {model::entityId(source.entities[1]), {0.0, 1.0, 0.0, 0.0}},
          }};
          model::FreedomMode reordered = mode;
          std::ranges::reverse(reordered.components);
          honest.degreesOfFreedom = 2U;
          honest.modes = {mode, std::move(reordered)};
          ScriptedSolver solver{honest};
          auto evaluated = runtime::evaluateSketch(input, solver);
          require(evaluated.has_value(),
                  "duplicate freedom mode escaped outcome boundary");
          requireInvalidSolverEvidence(*evaluated);
          break;
        }
        case 23: {
          source.constraints.push_back(
              model::Horizontal{id<SketchConstraintId>(seed + 12U),
                                model::entityId(source.entities[1])});
          input = request(source, seed + 100U);
          honest = resultFor(source, model::SolveStatus::Underconstrained, 1U);
          const model::ConflictSet conflict{
              {model::constraintId(source.constraints[0]),
               model::constraintId(source.constraints[1])},
              false};
          model::ConflictSet reordered = conflict;
          std::ranges::reverse(reordered.constraints);
          honest.conflicts = {conflict, std::move(reordered)};
          ScriptedSolver solver{honest};
          auto evaluated = runtime::evaluateSketch(input, solver);
          require(evaluated.has_value(),
                  "duplicate conflict escaped outcome boundary");
          requireInvalidSolverEvidence(*evaluated);
          break;
        }
        case 24: {
          CancellationSource cancellation;
          cancellation.request_stop();
          input.cancellation = cancellation.get_token();
          ScriptedSolver solver{honest};
          auto evaluated = runtime::evaluateSketch(input, solver);
          require(evaluated && evaluated->solve.solverResultValid &&
                      evaluated->solve.cancellationObserved,
                  "already-cancelled success lost solve evidence");
          requireWithheld(*evaluated,
                          "already-cancelled success published a scene");
          break;
        }
        case 25: {
          model::Definition unconstrained = source;
          unconstrained.constraints.clear();
          runtime::SketchRequest unconstrainedInput =
              request(unconstrained, seed + 300U);
          model::SolveResult invalidGeometry = resultFor(
              unconstrained, model::SolveStatus::Underconstrained, 1U);
          const SketchEntityId pointId =
              model::entityId(unconstrained.entities.front());
          if ((index / 28U) % 2U == 0U) {
            invalidGeometry.geometry.front() = model::CircleEntity{
                pointId, point(x, y), quantity<Length>(scale)};
          } else {
            invalidGeometry.geometry.front() = model::PointEntity{
                pointId,
                point(input.numerical.maximumCoordinateMeters * 2.0, y)};
          }
          ScriptedSolver solver{std::move(invalidGeometry)};
          auto evaluated = runtime::evaluateSketch(unconstrainedInput, solver);
          require(evaluated.has_value(),
                  "unconstrained invalid geometry escaped outcome boundary");
          requireInvalidSolverEvidence(*evaluated);
          break;
        }
        case 26: {
          const double translationX = random.between(-5.0, 5.0);
          const double translationY = random.between(-5.0, 5.0);
          const double factor = random.between(0.1, 4.0);
          model::Definition transformed = definition(
              seed, x + translationX, y + translationY, scale * factor);
          runtime::SketchRequest transformedInput =
              request(transformed, seed + 400U);
          ScriptedSolver first{honest};
          ScriptedSolver second{
              resultFor(transformed, model::SolveStatus::Underconstrained, 1U)};
          auto firstEvaluation = runtime::evaluateSketch(input, first);
          auto secondEvaluation =
              runtime::evaluateSketch(transformedInput, second);
          require(firstEvaluation && secondEvaluation &&
                      firstEvaluation->replacementScene &&
                      secondEvaluation->replacementScene,
                  "transformed valid geometry was not published");
          const auto firstPoints = firstEvaluation->replacementScene->points();
          const auto secondPoints =
              secondEvaluation->replacementScene->points();
          require(firstPoints.size() == secondPoints.size(),
                  "transform changed projected point cardinality");
          for (std::size_t pointIndex = 0; pointIndex < firstPoints.size();
               ++pointIndex) {
            require(near(secondPoints[pointIndex].x,
                         x + translationX +
                             (firstPoints[pointIndex].x - x) * factor) &&
                        near(secondPoints[pointIndex].y,
                             y + translationY +
                                 (firstPoints[pointIndex].y - y) * factor),
                    "projection violated rigid/scale geometry metamorphism");
          }
          const auto firstPrimitives =
              firstEvaluation->replacementScene->primitives();
          const auto secondPrimitives =
              secondEvaluation->replacementScene->primitives();
          for (std::size_t primitive = 0; primitive < firstPrimitives.size();
               ++primitive)
            require(near(secondPrimitives[primitive].radius,
                         firstPrimitives[primitive].radius * factor) &&
                        near(secondPrimitives[primitive].startAngleRadians,
                             firstPrimitives[primitive].startAngleRadians) &&
                        near(secondPrimitives[primitive].sweepAngleRadians,
                             firstPrimitives[primitive].sweepAngleRadians),
                    "projection violated curve transform metamorphism");
          break;
        }
        case 27: {
          honest.modes = {model::FreedomMode{{model::ModeComponent{
              model::entityId(source.entities.front()),
              {std::numeric_limits<double>::infinity(), 0.0}}}}};
          ScriptedSolver solver{honest};
          auto evaluated = runtime::evaluateSketch(input, solver);
          require(evaluated.has_value(),
                  "non-finite freedom mode escaped outcome boundary");
          requireInvalidSolverEvidence(*evaluated);
          break;
        }
        case 28: {
          honest.geometry.push_back(
              model::PointEntity{id<SketchEntityId>(seed + 999U), point(x, y)});
          ScriptedSolver solver{honest};
          auto evaluated = runtime::evaluateSketch(input, solver);
          require(evaluated.has_value(),
                  "extra geometry escaped the outcome boundary");
          requireInvalidSolverEvidence(*evaluated);
          break;
        }
        case 29: {
          honest.iterations = input.numerical.maximumIterations + 1U;
          ScriptedSolver solver{honest};
          auto evaluated = runtime::evaluateSketch(input, solver);
          require(evaluated.has_value(),
                  "excess solver iterations escaped the outcome boundary");
          requireInvalidSolverEvidence(*evaluated);
          break;
        }
        case 30: {
          source.constraints.push_back(
              model::Horizontal{id<SketchConstraintId>(seed + 12U),
                                model::entityId(source.entities[1])});
          input = request(source, seed + 100U);
          honest = resultFor(source, model::SolveStatus::Underconstrained, 1U);
          honest.redundantConstraints = {
              model::constraintId(source.constraints[1]),
              model::constraintId(source.constraints[0])};
          honest.conflicts = {{honest.redundantConstraints, false}};
          honest.modes = {model::FreedomMode{{
              {model::entityId(source.entities[1]), {1.0, 0.0, 0.0, 0.0}},
              {model::entityId(source.entities[0]), {0.0, 1.0}},
          }}};
          ScriptedSolver solver{honest};
          auto evaluated = runtime::evaluateSketch(input, solver);
          require(evaluated && evaluated->solve.solverResultValid &&
                      std::ranges::is_sorted(
                          evaluated->solve.redundantConstraints) &&
                      std::ranges::is_sorted(
                          evaluated->solve.conflicts.front().constraints) &&
                      std::ranges::is_sorted(
                          evaluated->solve.modes.front().components, {},
                          &model::ModeComponent::entity),
                  "solver evidence was not canonicalized once");
          break;
        }
        case 31: {
          input.numerical.maximumModeVariables = 1U;
          honest.modes = {model::FreedomMode{{model::ModeComponent{
              model::entityId(source.entities.front()), {1.0, 0.0}}}}};
          ScriptedSolver solver{honest};
          auto evaluated = runtime::evaluateSketch(input, solver);
          require(evaluated.has_value(),
                  "freedom-mode budget escaped the outcome boundary");
          requireInvalidSolverEvidence(*evaluated);
          break;
        }
        }
      });
}

} // namespace

int main() {
  try {
    verifyRuntimeContract(kearne::testkit::propertyProfile());
    std::cout << "sketch runtime contract properties passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
