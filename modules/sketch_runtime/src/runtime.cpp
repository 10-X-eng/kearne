#include <kearne/sketch_runtime/runtime.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <ranges>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace kearne::sketch_runtime {
namespace {

using EntitySet =
    std::unordered_set<SketchEntityId, TypedIdHash<SketchEntityIdTag>>;
using ConstraintSet =
    std::unordered_set<SketchConstraintId, TypedIdHash<SketchConstraintIdTag>>;

struct ValidatedResult {
  std::vector<sketch::Entity> geometry;
  std::vector<sketch::ConstraintResidual> residuals;
  std::vector<sketch::FreedomMode> modes;
  std::vector<SketchConstraintId> redundantConstraints;
  std::vector<sketch::ConflictSet> conflicts;
};

Diagnostic invalidSolverResult(const Diagnostic &cause) {
  Diagnostic result =
      diagnostic("sketch.runtime.invalid-solver-result",
                 "sketch solver returned inconsistent evidence");
  result.parameters.push_back(cause.code);
  result.detail = cause.summary;
  return result;
}

Result<void> validateDrag(const SketchRequest &request) {
  if (!request.drag)
    return {};
  const auto found =
      std::ranges::find(request.definition.entities, request.drag->point.entity,
                        sketch::entityId);
  if (found == request.definition.entities.end())
    return std::unexpected(diagnostic("sketch.runtime.drag-missing-entity",
                                      "drag target entity is missing"));
  const bool validKey = std::visit(
      [&request]<typename Entity>(const Entity &) {
        using Type = std::decay_t<Entity>;
        if constexpr (std::is_same_v<Type, sketch::PointEntity>)
          return request.drag->point.key == sketch::PointKey::Point;
        if constexpr (std::is_same_v<Type, sketch::LineEntity>)
          return request.drag->point.key == sketch::PointKey::Start ||
                 request.drag->point.key == sketch::PointKey::End;
        if constexpr (std::is_same_v<Type, sketch::ArcEntity>)
          return request.drag->point.key == sketch::PointKey::Center ||
                 request.drag->point.key == sketch::PointKey::Start ||
                 request.drag->point.key == sketch::PointKey::End;
        if constexpr (std::is_same_v<Type, sketch::EllipseEntity>)
          return request.drag->point.key == sketch::PointKey::Center ||
                 request.drag->point.key == sketch::PointKey::Major ||
                 request.drag->point.key == sketch::PointKey::Minor;
        if constexpr (std::is_same_v<Type, sketch::EllipticalArcEntity>)
          return request.drag->point.key == sketch::PointKey::Center ||
                 request.drag->point.key == sketch::PointKey::Major ||
                 request.drag->point.key == sketch::PointKey::Minor ||
                 request.drag->point.key == sketch::PointKey::Start ||
                 request.drag->point.key == sketch::PointKey::End;
        if constexpr (std::is_same_v<Type, sketch::HyperbolicArcEntity>)
          return request.drag->point.key == sketch::PointKey::Center ||
                 request.drag->point.key == sketch::PointKey::Major ||
                 request.drag->point.key == sketch::PointKey::Minor ||
                 request.drag->point.key == sketch::PointKey::Focus ||
                 request.drag->point.key == sketch::PointKey::Start ||
                 request.drag->point.key == sketch::PointKey::End;
        if constexpr (std::is_same_v<Type, sketch::ParabolicArcEntity>)
          return request.drag->point.key == sketch::PointKey::Center ||
                 request.drag->point.key == sketch::PointKey::Focus ||
                 request.drag->point.key == sketch::PointKey::Start ||
                 request.drag->point.key == sketch::PointKey::End;
        if constexpr (std::is_same_v<Type, sketch::BSplineEntity>)
          return request.drag->point.key == sketch::PointKey::Start ||
                 request.drag->point.key == sketch::PointKey::End;
        return request.drag->point.key == sketch::PointKey::Center;
      },
      *found);
  if (!validKey)
    return std::unexpected(diagnostic("sketch.runtime.drag-invalid-point-key",
                                      "drag point key is invalid"));
  const auto targetInRange = [&request](double value) {
    return std::isfinite(value) &&
           std::abs(value) <= request.numerical.maximumCoordinateMeters;
  };
  if (!targetInRange(request.drag->target.x.si()) ||
      !targetInRange(request.drag->target.y.si()))
    return std::unexpected(
        diagnostic("sketch.runtime.drag-target-range",
                   "drag target is outside the model range"));
  return {};
}

Result<void> validateRequest(const SketchRequest &request) {
  if (request.evidence.sourceDigest != request.definition.sourceDigest)
    return std::unexpected(diagnostic("sketch.runtime.stale-source",
                                      "sketch source evidence is stale"));
  if (auto valid = sketch::validate(request.definition, request.numerical);
      !valid)
    return std::unexpected(std::move(valid.error()));
  if (request.priorSolution && !request.priorSolution->empty()) {
    auto prior = sketch::evaluateResiduals(
        request.definition, *request.priorSolution, request.numerical);
    if (!prior)
      return std::unexpected(std::move(prior.error()));
  }
  return validateDrag(request);
}

std::size_t parameterCount(const sketch::Entity &entity) {
  return std::visit(
      []<typename Entity>(const Entity &value) {
        using Type = std::decay_t<Entity>;
        if constexpr (std::is_same_v<Type, sketch::PointEntity>)
          return std::size_t{2};
        if constexpr (std::is_same_v<Type, sketch::LineEntity>)
          return std::size_t{4};
        if constexpr (std::is_same_v<Type, sketch::CircleEntity>)
          return std::size_t{3};
        if constexpr (std::is_same_v<Type, sketch::EllipticalArcEntity> ||
                      std::is_same_v<Type, sketch::HyperbolicArcEntity>)
          return std::size_t{7};
        if constexpr (std::is_same_v<Type, sketch::ParabolicArcEntity>)
          return std::size_t{6};
        if constexpr (std::is_same_v<Type, sketch::BSplineEntity>)
          return value.controlPoints.size() * 3U;
        return std::size_t{5};
      },
      entity);
}

struct ReferencedEvidence {
  std::vector<sketch::FreedomMode> modes;
  std::vector<SketchConstraintId> redundantConstraints;
  std::vector<sketch::ConflictSet> conflicts;
};

bool modeLess(const sketch::FreedomMode &first,
              const sketch::FreedomMode &second) {
  const auto componentLess = [](const sketch::ModeComponent &left,
                                const sketch::ModeComponent &right) {
    if (left.entity != right.entity)
      return left.entity < right.entity;
    return std::lexicographical_compare(
        left.parameterDirection.begin(), left.parameterDirection.end(),
        right.parameterDirection.begin(), right.parameterDirection.end());
  };
  return std::lexicographical_compare(
      first.components.begin(), first.components.end(),
      second.components.begin(), second.components.end(), componentLess);
}

Result<ReferencedEvidence>
validateReferencedEvidence(const sketch::Definition &definition,
                           const sketch::SolveResult &result,
                           const sketch::NumericalProfile &profile) {
  std::unordered_map<SketchEntityId, std::size_t,
                     TypedIdHash<SketchEntityIdTag>>
      entityParameters;
  entityParameters.reserve(definition.entities.size());
  std::size_t variables = 0;
  for (const sketch::Entity &entity : definition.entities) {
    const std::size_t count = parameterCount(entity);
    entityParameters.emplace(sketch::entityId(entity), count);
    variables += count;
  }
  if (result.degreesOfFreedom > variables)
    return std::unexpected(
        diagnostic("sketch.runtime.invalid-degrees-of-freedom",
                   "solver degrees of freedom exceed variables"));
  if (result.modes.size() > result.degreesOfFreedom)
    return std::unexpected(
        diagnostic("sketch.runtime.too-many-modes",
                   "solver returned too many freedom modes"));
  if (result.modes.size() > profile.maximumModeVariables)
    return std::unexpected(
        diagnostic("sketch.runtime.mode-budget",
                   "solver freedom modes exceed the numerical profile budget"));
  if (result.redundantConstraints.size() > profile.maximumRedundancyConstraints)
    return std::unexpected(diagnostic(
        "sketch.runtime.constraint-analysis-budget",
        "solver constraint analysis exceeds the numerical profile budget"));

  ConstraintSet constraintIds;
  constraintIds.reserve(definition.constraints.size());
  for (const sketch::Constraint &constraint : definition.constraints)
    constraintIds.insert(sketch::constraintId(constraint));

  ReferencedEvidence normalized;
  normalized.redundantConstraints = result.redundantConstraints;
  ConstraintSet redundant;
  for (const SketchConstraintId id : result.redundantConstraints) {
    if (!constraintIds.contains(id) || !redundant.insert(id).second)
      return std::unexpected(
          diagnostic("sketch.runtime.invalid-redundant-id",
                     "solver returned an invalid redundant constraint"));
  }
  std::ranges::sort(normalized.redundantConstraints);
  normalized.conflicts.reserve(result.conflicts.size());
  for (const sketch::ConflictSet &conflict : result.conflicts) {
    if (conflict.constraints.empty())
      return std::unexpected(
          diagnostic("sketch.runtime.empty-conflict",
                     "solver returned an empty conflict set"));
    ConstraintSet members;
    for (const SketchConstraintId id : conflict.constraints) {
      if (!constraintIds.contains(id) || !members.insert(id).second)
        return std::unexpected(
            diagnostic("sketch.runtime.invalid-conflict-id",
                       "solver returned an invalid conflict constraint"));
    }
    sketch::ConflictSet normalizedConflict = conflict;
    std::ranges::sort(normalizedConflict.constraints);
    normalized.conflicts.push_back(std::move(normalizedConflict));
  }
  std::ranges::sort(normalized.conflicts, {},
                    &sketch::ConflictSet::constraints);
  if (std::ranges::adjacent_find(normalized.conflicts, {},
                                 &sketch::ConflictSet::constraints) !=
      normalized.conflicts.end())
    return std::unexpected(diagnostic("sketch.runtime.duplicate-conflict",
                                      "solver duplicated a conflict set"));

  normalized.modes.reserve(result.modes.size());
  for (const sketch::FreedomMode &mode : result.modes) {
    EntitySet members;
    bool nonzero = false;
    std::size_t modeVariables = 0;
    for (const sketch::ModeComponent &component : mode.components) {
      const auto found = entityParameters.find(component.entity);
      if (found == entityParameters.end() ||
          !members.insert(component.entity).second)
        return std::unexpected(
            diagnostic("sketch.runtime.invalid-mode-entity",
                       "solver mode references an invalid entity"));
      if (component.parameterDirection.size() != found->second ||
          std::ranges::any_of(component.parameterDirection, [](double value) {
            return !std::isfinite(value);
          }))
        return std::unexpected(diagnostic("sketch.runtime.invalid-mode-vector",
                                          "solver mode vector is invalid"));
      if (component.parameterDirection.size() >
          profile.maximumModeVariables -
              std::min(profile.maximumModeVariables, modeVariables))
        return std::unexpected(diagnostic(
            "sketch.runtime.mode-budget",
            "solver freedom mode exceeds the numerical profile budget"));
      modeVariables += component.parameterDirection.size();
      nonzero = nonzero ||
                std::ranges::any_of(component.parameterDirection,
                                    [](double value) { return value != 0.0; });
    }
    if (!nonzero)
      return std::unexpected(diagnostic("sketch.runtime.zero-mode",
                                        "solver returned a zero freedom mode"));
    sketch::FreedomMode normalizedMode = mode;
    std::ranges::sort(normalizedMode.components, {},
                      &sketch::ModeComponent::entity);
    normalized.modes.push_back(std::move(normalizedMode));
  }
  std::ranges::sort(normalized.modes, modeLess);
  if (std::ranges::adjacent_find(normalized.modes) != normalized.modes.end())
    return std::unexpected(diagnostic("sketch.runtime.duplicate-mode",
                                      "solver duplicated a freedom mode"));
  return normalized;
}

bool equivalentResidual(double first, double second) {
  const double scale = std::max({1.0, std::abs(first), std::abs(second)});
  return std::abs(first - second) <=
         64.0 * std::numeric_limits<double>::epsilon() * scale;
}

Result<std::vector<sketch::ConstraintResidual>> validateResidualClaims(
    const sketch::Definition &definition,
    const std::vector<sketch::ConstraintResidual> &independent,
    const std::vector<sketch::ConstraintResidual> &claimed) {
  if (claimed.size() != definition.constraints.size())
    return std::unexpected(diagnostic("sketch.runtime.residual-count",
                                      "solver residual count is incorrect"));
  std::unordered_map<SketchConstraintId, const sketch::ConstraintResidual *,
                     TypedIdHash<SketchConstraintIdTag>>
      byId;
  byId.reserve(claimed.size());
  for (const sketch::ConstraintResidual &residual : claimed) {
    if (!std::isfinite(residual.normalizedMaximum) ||
        residual.normalizedMaximum < 0.0 ||
        residual.satisfied != (residual.normalizedMaximum <= 1.0) ||
        !byId.emplace(residual.constraint, &residual).second)
      return std::unexpected(diagnostic("sketch.runtime.invalid-residual",
                                        "solver residual evidence is invalid"));
  }
  for (const sketch::ConstraintResidual &expected : independent) {
    const auto found = byId.find(expected.constraint);
    if (found == byId.end() || found->second->satisfied != expected.satisfied ||
        !equivalentResidual(found->second->normalizedMaximum,
                            expected.normalizedMaximum))
      return std::unexpected(
          diagnostic("sketch.runtime.residual-mismatch",
                     "solver residuals failed independent validation"));
  }
  return independent;
}

Result<std::vector<sketch::Entity>>
orderGeometry(const sketch::Definition &definition,
              const std::vector<sketch::Entity> &geometry) {
  if (geometry.size() != definition.entities.size())
    return std::unexpected(
        diagnostic("sketch.runtime.solution-count",
                   "solver geometry count does not match the definition"));
  std::unordered_map<SketchEntityId, const sketch::Entity *,
                     TypedIdHash<SketchEntityIdTag>>
      byId;
  byId.reserve(geometry.size());
  for (const sketch::Entity &entity : geometry) {
    if (!byId.emplace(sketch::entityId(entity), &entity).second)
      return std::unexpected(diagnostic("sketch.runtime.duplicate-solution-id",
                                        "solver duplicated a geometry entity"));
  }
  std::vector<sketch::Entity> ordered;
  ordered.reserve(definition.entities.size());
  for (const sketch::Entity &entity : definition.entities) {
    const auto found = byId.find(sketch::entityId(entity));
    if (found == byId.end())
      return std::unexpected(diagnostic("sketch.runtime.missing-solution-id",
                                        "solver dropped a geometry entity"));
    if (found->second->index() != entity.index())
      return std::unexpected(
          diagnostic("sketch.runtime.solution-kind",
                     "solver changed a geometry entity kind"));
    ordered.push_back(*found->second);
  }
  return ordered;
}

Result<ValidatedResult>
validateSolverResult(const SketchRequest &request,
                     const sketch::SolveResult &result) {
  if (result.iterations > request.numerical.maximumIterations)
    return std::unexpected(
        diagnostic("sketch.runtime.iteration-count",
                   "solver iterations exceed the numerical profile limit"));
  auto evidence =
      validateReferencedEvidence(request.definition, result, request.numerical);
  if (!evidence)
    return std::unexpected(std::move(evidence.error()));

  switch (result.status) {
  case sketch::SolveStatus::Cancelled:
    if (!request.cancellation.stop_requested())
      return std::unexpected(
          diagnostic("sketch.runtime.false-cancellation",
                     "solver reported cancellation without a request"));
    if (!result.geometry.empty() || !result.residuals.empty() ||
        result.degreesOfFreedom != 0 || !result.modes.empty() ||
        !result.redundantConstraints.empty() || !result.conflicts.empty())
      return std::unexpected(
          diagnostic("sketch.runtime.cancelled-payload",
                     "cancelled solve returned partial result state"));
    return ValidatedResult{};
  case sketch::SolveStatus::Diverged:
    if (result.geometry.empty()) {
      if (!result.residuals.empty() || result.degreesOfFreedom != 0 ||
          !result.modes.empty() || !result.redundantConstraints.empty() ||
          !result.conflicts.empty())
        return std::unexpected(
            diagnostic("sketch.runtime.partial-divergence",
                       "diverged solve returned partial result state"));
      return ValidatedResult{};
    }
    break;
  case sketch::SolveStatus::Solved:
  case sketch::SolveStatus::Underconstrained:
  case sketch::SolveStatus::Inconsistent:
    break;
  default:
    return std::unexpected(diagnostic("sketch.runtime.invalid-solve-status",
                                      "solver returned an invalid status"));
  }

  auto independent = sketch::evaluateResiduals(
      request.definition, result.geometry, request.numerical);
  if (!independent)
    return std::unexpected(std::move(independent.error()));
  auto residuals = validateResidualClaims(request.definition, *independent,
                                          result.residuals);
  if (!residuals)
    return std::unexpected(std::move(residuals.error()));
  const bool satisfied =
      std::ranges::all_of(*residuals, &sketch::ConstraintResidual::satisfied);
  if ((result.status == sketch::SolveStatus::Solved &&
       (!satisfied || result.degreesOfFreedom != 0)) ||
      (result.status == sketch::SolveStatus::Underconstrained &&
       (!satisfied || result.degreesOfFreedom == 0)) ||
      (result.status == sketch::SolveStatus::Inconsistent && satisfied))
    return std::unexpected(
        diagnostic("sketch.runtime.false-solve-status",
                   "solver status contradicts validated evidence"));
  auto ordered = orderGeometry(request.definition, result.geometry);
  if (!ordered)
    return std::unexpected(std::move(ordered.error()));
  return ValidatedResult{std::move(*ordered), std::move(*residuals),
                         std::move(evidence->modes),
                         std::move(evidence->redundantConstraints),
                         std::move(evidence->conflicts)};
}

void appendWithheldDiagnostic(std::vector<Diagnostic> &diagnostics,
                              sketch::SolveStatus status) {
  if (status == sketch::SolveStatus::Inconsistent)
    diagnostics.push_back(diagnostic("sketch.runtime.inconsistent",
                                     "inconsistent sketch was not published"));
  else if (status == sketch::SolveStatus::Diverged)
    diagnostics.push_back(diagnostic("sketch.runtime.diverged",
                                     "diverged sketch was not published"));
  else if (status == sketch::SolveStatus::Cancelled)
    diagnostics.push_back(diagnostic("sketch.runtime.cancelled",
                                     "cancelled sketch was not published",
                                     Severity::Information));
}

} // namespace

Result<SketchEvaluation> evaluateSketch(const SketchRequest &request,
                                        const sketch::Solver &solver) {
  if (auto valid = validateRequest(request); !valid)
    return std::unexpected(std::move(valid.error()));

  sketch::SolveInput input{
      request.definition,
      request.priorSolution.value_or(std::vector<sketch::Entity>{}),
      request.drag, request.numerical, request.cancellation};
  auto solved = solver.solve(input);
  if (!solved)
    return std::unexpected(std::move(solved.error()));

  SketchEvaluation evaluation{request.evidence,
                              {solved->status,
                               false,
                               request.cancellation.stop_requested(),
                               solved->degreesOfFreedom,
                               {},
                               {},
                               {},
                               {},
                               solved->iterations},
                              solved->diagnostics,
                              {}};
  auto validated = validateSolverResult(request, *solved);
  if (!validated) {
    evaluation.diagnostics.push_back(invalidSolverResult(validated.error()));
    return evaluation;
  }
  evaluation.solve.solverResultValid = true;
  evaluation.solve.residuals = std::move(validated->residuals);
  evaluation.solve.modes = std::move(validated->modes);
  evaluation.solve.redundantConstraints =
      std::move(validated->redundantConstraints);
  evaluation.solve.conflicts = std::move(validated->conflicts);
  evaluation.solve.cancellationObserved = request.cancellation.stop_requested();

  if (solved->status != sketch::SolveStatus::Solved &&
      solved->status != sketch::SolveStatus::Underconstrained) {
    appendWithheldDiagnostic(evaluation.diagnostics, solved->status);
    return evaluation;
  }
  if (evaluation.solve.cancellationObserved) {
    evaluation.diagnostics.push_back(
        diagnostic("sketch.runtime.cancelled-after-solve",
                   "completed sketch was withheld after cancellation",
                   Severity::Information));
    return evaluation;
  }

  auto projected =
      render::projectSketchScene(request.evidence.scene, validated->geometry);
  if (!projected) {
    evaluation.diagnostics.push_back(std::move(projected.error()));
    return evaluation;
  }
  if (request.cancellation.stop_requested()) {
    evaluation.solve.cancellationObserved = true;
    evaluation.diagnostics.push_back(
        diagnostic("sketch.runtime.cancelled-after-projection",
                   "projected sketch was withheld after cancellation",
                   Severity::Information));
    return evaluation;
  }
  evaluation.replacementScene =
      std::make_shared<const render::SketchSceneSnapshot>(
          std::move(*projected));
  return evaluation;
}

} // namespace kearne::sketch_runtime
