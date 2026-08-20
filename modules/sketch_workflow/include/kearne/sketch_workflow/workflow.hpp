#pragma once

#include <kearne/document/content_store.hpp>
#include <kearne/engineering/service.hpp>
#include <kearne/sketch/tools.hpp>
#include <kearne/sketch_runtime/runtime.hpp>

#include <optional>
#include <stop_token>
#include <string>
#include <string_view>

namespace kearne::sketch_workflow {

struct SourceRevision {
  document::Bytes bytes;
  ContentDigest digest;
  bool operator==(const SourceRevision &) const = default;
};

class SourceEditor {
public:
  virtual ~SourceEditor() = default;
  [[nodiscard]] virtual Result<SourceRevision>
  create(JobId job, std::string_view functionName,
         std::stop_token cancellation = {}) = 0;
  [[nodiscard]] virtual Result<SourceRevision>
  apply(JobId job, std::span<const std::uint8_t> source,
        std::string_view functionName, const sketch::AppliedEdits &edits,
        std::stop_token cancellation = {}) = 0;
};

struct ActorContext {
  ProjectId project;
  ActorId actor;
  PermissionContextId permission;
};

struct SketchAddress {
  std::string sourcePath;
  std::string functionName;
};

struct OperationContext {
  RequestId request;
  TransactionId transaction;
  JobId sourceJob;
  RevisionId baseRevision;
  Origin origin = Origin::Human;
  std::optional<GestureId> gesture;
  std::uint64_t committedAtUnixMilliseconds = 0;
};

struct EvaluationIdentity {
  render::RenderSessionHandle renderSession;
  ModelBindingId attachmentBinding;
  EvaluationKey evaluation;
  render::SceneGeneration sceneGeneration;
  render::SceneDigest sceneDigest;
};

struct SketchState {
  SketchAddress address;
  SourceRevision source;
  sketch::Definition definition;
  RevisionId revision;
  std::optional<sketch_runtime::SketchEvaluation> evaluation;
  std::optional<Diagnostic> evaluationFailure;
};

class Workflow final {
public:
  Workflow(ActorContext actor, document::ContentStore &contentStore,
           engineering::EngineeringService &engineering,
           SourceEditor &sourceEditor, const sketch::Solver &solver);

  [[nodiscard]] Result<SketchState> create(SketchAddress address,
                                           const OperationContext &operation,
                                           const EvaluationIdentity &evaluation,
                                           std::stop_token cancellation = {});
  [[nodiscard]] Result<SketchState>
  applyTool(const SketchState &current, const OperationContext &operation,
            const sketch::ToolInput &tool, const EvaluationIdentity &evaluation,
            std::stop_token cancellation = {});

private:
  [[nodiscard]] Result<RevisionId>
  commitSource(const SketchAddress &address, const SourceRevision &source,
               const OperationContext &operation,
               std::optional<ContentDigest> expectedPrior);
  [[nodiscard]] SketchState evaluate(SketchAddress address,
                                     SourceRevision source,
                                     sketch::Definition definition,
                                     RevisionId revision,
                                     const EvaluationIdentity &identity,
                                     std::stop_token cancellation) const;

  ActorContext actor_;
  document::ContentStore &contentStore_;
  engineering::EngineeringService &engineering_;
  SourceEditor &sourceEditor_;
  const sketch::Solver &solver_;
};

} // namespace kearne::sketch_workflow
