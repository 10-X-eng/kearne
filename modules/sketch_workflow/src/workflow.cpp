#include <kearne/sketch_workflow/workflow.hpp>

#include <kearne/api/strong_types.hpp>

#include <string>
#include <unordered_map>
#include <utility>

namespace kearne::sketch_workflow {
namespace {

namespace wire = api::v1;
constexpr std::string_view pythonMediaType = "text/x-python; charset=utf-8";

void writeEnvelope(const ActorContext &actor, const OperationContext &operation,
                   wire::CommandEnvelope &command) {
  api::writeId(operation.request, command.mutable_request_id());
  api::writeDigest(operation.baseRevision, command.mutable_base_revision());
  api::writeId(actor.actor, command.mutable_actor_id());
  command.set_origin(api::writeOrigin(operation.origin));
  api::writeId(actor.permission, command.mutable_permission_context_id());
  if (operation.gesture)
    api::writeId(*operation.gesture, command.mutable_gesture_id());
}

void writeContent(const SourceRevision &source,
                  wire::ContentReference &reference) {
  api::writeDigest(source.digest, reference.mutable_digest());
  reference.set_byte_size(source.bytes.size());
  reference.set_media_type(pythonMediaType);
}

Diagnostic commandFailure(const wire::CommandResult &result) {
  if (result.diagnostics().empty())
    return diagnostic("sketch.workflow.commit-failed",
                      "Sketch source command was not committed");
  const auto code = result.diagnostics(0).code();
  return diagnostic(std::string{code.data(), code.size()},
                    "Sketch source command was not committed");
}

Diagnostic cancelledBeforeCommit() {
  return diagnostic("sketch.workflow.cancelled-before-commit",
                    "Sketch operation was cancelled before commit",
                    Severity::Information);
}

std::optional<std::vector<sketch::Entity>>
preservedSolution(const SketchState &current,
                  const sketch::Definition &definition) {
  if (!current.evaluation || !current.evaluation->replacementScene ||
      current.evaluation->geometry.empty())
    return std::nullopt;
  std::unordered_map<SketchEntityId, const sketch::Entity *,
                     TypedIdHash<SketchEntityIdTag>>
      prior;
  prior.reserve(current.evaluation->geometry.size());
  for (const sketch::Entity &entity : current.evaluation->geometry)
    prior.emplace(sketch::entityId(entity), &entity);
  std::unordered_map<SketchEntityId, const sketch::Entity *,
                     TypedIdHash<SketchEntityIdTag>>
      authored;
  authored.reserve(current.definition.entities.size());
  for (const sketch::Entity &entity : current.definition.entities)
    authored.emplace(sketch::entityId(entity), &entity);
  std::vector<sketch::Entity> result;
  result.reserve(definition.entities.size());
  for (const sketch::Entity &entity : definition.entities) {
    const SketchEntityId id = sketch::entityId(entity);
    const auto found = prior.find(id);
    const auto source = authored.find(id);
    const bool unchanged =
        source != authored.end() && *source->second == entity;
    result.push_back(unchanged && found != prior.end() &&
                             found->second->index() == entity.index()
                         ? *found->second
                         : entity);
  }
  return result;
}

} // namespace

Workflow::Workflow(ActorContext actor, document::ContentStore &contentStore,
                   engineering::EngineeringService &engineering,
                   SourceEditor &sourceEditor, const sketch::Solver &solver)
    : actor_(std::move(actor)), contentStore_(contentStore),
      engineering_(engineering), sourceEditor_(sourceEditor), solver_(solver) {}

Result<RevisionId>
Workflow::commitSource(const SketchAddress &address,
                       const SourceRevision &source,
                       const OperationContext &operation,
                       std::optional<ContentDigest> expectedPrior) {
  if (auto staged = contentStore_.put(source.digest, source.bytes); !staged)
    return std::unexpected(std::move(staged.error()));

  wire::CommandEnvelope command;
  writeEnvelope(actor_, operation, command);
  if (expectedPrior) {
    wire::ReplaceSourceModuleCommand *replace =
        command.mutable_operation()->mutable_replace_source_module();
    api::writeId(actor_.project, replace->mutable_project_id());
    replace->set_path(address.sourcePath);
    api::writeDigest(*expectedPrior, replace->mutable_expected_prior());
    writeContent(source, *replace->mutable_content());
  } else {
    wire::CreateSourceModuleCommand *create =
        command.mutable_operation()->mutable_create_source_module();
    api::writeId(actor_.project, create->mutable_project_id());
    create->set_path(address.sourcePath);
    writeContent(source, *create->mutable_content());
  }
  auto committed = engineering_.submit(
      command, {operation.transaction, operation.committedAtUnixMilliseconds});
  if (!committed)
    return std::unexpected(std::move(committed.error()));
  if (!committed->committed())
    return std::unexpected(commandFailure(*committed));
  auto revision = api::readDigest<RevisionId>(committed->revision());
  if (!revision)
    return std::unexpected(std::move(revision.error()));
  return revision;
}

SketchState
Workflow::evaluate(SketchAddress address, SourceRevision source,
                   sketch::Definition definition, RevisionId revision,
                   const EvaluationIdentity &identity,
                   std::optional<std::vector<sketch::Entity>> priorSolution,
                   std::stop_token cancellation) const {
  const sketch_runtime::EvaluationEvidence evidence{
      {{identity.renderSession,
        {identity.attachmentBinding, revision},
        identity.evaluation},
       identity.sceneGeneration,
       identity.sceneDigest},
      source.digest};
  auto evaluated = sketch_runtime::evaluateSketch({definition,
                                                   evidence,
                                                   std::move(priorSolution),
                                                   std::nullopt,
                                                   {},
                                                   cancellation},
                                                  solver_);
  if (!evaluated)
    return {std::move(address),    std::move(source),
            std::move(definition), std::move(revision),
            std::nullopt,          std::move(evaluated.error())};
  return {std::move(address),  std::move(source),     std::move(definition),
          std::move(revision), std::move(*evaluated), std::nullopt};
}

Result<SketchState> Workflow::create(SketchAddress address,
                                     const OperationContext &operation,
                                     const EvaluationIdentity &evaluation,
                                     std::stop_token cancellation) {
  if (cancellation.stop_requested())
    return std::unexpected(cancelledBeforeCommit());
  if (operation.baseRevision != engineering_.headSnapshot()->revisionId())
    return std::unexpected(
        diagnostic("sketch.workflow.stale-base",
                   "Sketch creation is based on another project revision"));
  auto source = sourceEditor_.create(operation.sourceJob, address.functionName,
                                     cancellation);
  if (!source)
    return std::unexpected(std::move(source.error()));
  if (cancellation.stop_requested())
    return std::unexpected(cancelledBeforeCommit());
  auto revision = commitSource(address, *source, operation, std::nullopt);
  if (!revision)
    return std::unexpected(std::move(revision.error()));
  sketch::Definition definition{source->digest, {}, {}, {}};
  return evaluate(std::move(address), std::move(*source), std::move(definition),
                  std::move(*revision), evaluation, std::nullopt, cancellation);
}

Result<SketchState> Workflow::applyTool(const SketchState &current,
                                        const OperationContext &operation,
                                        const sketch::ToolInput &tool,
                                        const EvaluationIdentity &evaluation,
                                        std::stop_token cancellation) {
  if (cancellation.stop_requested())
    return std::unexpected(cancelledBeforeCommit());
  if (current.revision != operation.baseRevision ||
      current.revision != engineering_.headSnapshot()->revisionId())
    return std::unexpected(
        diagnostic("sketch.workflow.stale-base",
                   "Sketch edit is based on another project revision"));
  auto edits = sketch::applyTool(current.definition, tool);
  if (!edits)
    return std::unexpected(std::move(edits.error()));
  return applyEdits(current, operation, std::move(*edits), evaluation,
                    cancellation);
}

Result<SketchState> Workflow::applyEdits(const SketchState &current,
                                         const OperationContext &operation,
                                         sketch::AppliedEdits edits,
                                         const EvaluationIdentity &evaluation,
                                         std::stop_token cancellation) {
  if (cancellation.stop_requested())
    return std::unexpected(cancelledBeforeCommit());
  if (current.revision != operation.baseRevision ||
      current.revision != engineering_.headSnapshot()->revisionId())
    return std::unexpected(
        diagnostic("sketch.workflow.stale-base",
                   "Sketch edit is based on another project revision"));
  if (edits.sourceEdits.empty() ||
      edits.target.sourceDigest != current.definition.sourceDigest)
    return std::unexpected(
        diagnostic("sketch.workflow.edit-definition",
                   "Sketch edits are based on another definition"));
  auto source =
      sourceEditor_.apply(operation.sourceJob, current.source.bytes,
                          current.address.functionName, edits, cancellation);
  if (!source)
    return std::unexpected(std::move(source.error()));
  if (cancellation.stop_requested())
    return std::unexpected(cancelledBeforeCommit());
  sketch::Definition definition = std::move(edits.target);
  definition.sourceDigest = source->digest;
  auto revision =
      commitSource(current.address, *source, operation, current.source.digest);
  if (!revision)
    return std::unexpected(std::move(revision.error()));
  auto prior = preservedSolution(current, definition);
  return evaluate(current.address, std::move(*source), std::move(definition),
                  std::move(*revision), evaluation, std::move(prior),
                  cancellation);
}

Result<SketchState> Workflow::replaceSource(
    const SketchState &current, const OperationContext &operation,
    SourceRevision source, sketch::Definition definition,
    const EvaluationIdentity &evaluation, std::stop_token cancellation) {
  if (cancellation.stop_requested())
    return std::unexpected(cancelledBeforeCommit());
  if (current.revision != operation.baseRevision ||
      current.revision != engineering_.headSnapshot()->revisionId())
    return std::unexpected(
        diagnostic("sketch.workflow.stale-base",
                   "Sketch edit is based on another project revision"));
  if (source.digest != definition.sourceDigest)
    return std::unexpected(
        diagnostic("sketch.workflow.source-definition",
                   "recognized Sketch definition is based on another source"));
  auto revision =
      commitSource(current.address, source, operation, current.source.digest);
  if (!revision)
    return std::unexpected(std::move(revision.error()));
  auto prior = preservedSolution(current, definition);
  return evaluate(current.address, std::move(source), std::move(definition),
                  std::move(*revision), evaluation, std::move(prior),
                  cancellation);
}

} // namespace kearne::sketch_workflow
