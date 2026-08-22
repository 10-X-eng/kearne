#pragma once

#include <kearne/render/sketch_scene.hpp>
#include <kearne/sketch/model.hpp>

#include <memory>
#include <optional>
#include <vector>

namespace kearne::sketch_runtime {

struct EvaluationEvidence {
  render::SceneStamp scene;
  ContentDigest sourceDigest;
  bool operator==(const EvaluationEvidence &) const = default;
};

struct SketchRequest {
  sketch::Definition definition;
  EvaluationEvidence evidence;
  std::optional<std::vector<sketch::Entity>> priorSolution;
  std::optional<sketch::DragTarget> drag;
  sketch::NumericalProfile numerical;
  CancellationToken cancellation;
};

struct SolveEvidence {
  sketch::SolveStatus status = sketch::SolveStatus::Diverged;
  bool solverResultValid = false;
  bool cancellationObserved = false;
  // Structurally bounded solver evidence. Independent rank analysis belongs
  // to a solver conformance layer and is not recomputed by this port.
  std::size_t degreesOfFreedom = 0;
  std::vector<sketch::ConstraintResidual> residuals;
  std::vector<sketch::FreedomMode> modes;
  std::vector<SketchConstraintId> redundantConstraints;
  std::vector<sketch::ConflictSet> conflicts;
  std::uint32_t iterations = 0;
};

struct SketchEvaluation {
  EvaluationEvidence evidence;
  SolveEvidence solve;
  std::vector<Diagnostic> diagnostics;
  std::vector<sketch::Entity> geometry;
  std::shared_ptr<const render::SketchSceneSnapshot> replacementScene;
};

[[nodiscard]] Result<SketchEvaluation>
evaluateSketch(const SketchRequest &request, const sketch::Solver &solver);

} // namespace kearne::sketch_runtime
