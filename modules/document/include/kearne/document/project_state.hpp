#pragma once

#include <kearne/document/model.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace kearne::document {

namespace internal {
class ProjectStateAccess;
}

class ProjectState final {
public:
  [[nodiscard]] static Result<ProjectState> create(ProjectId project,
                                                   SchemaSetDigest schemaSet);

  [[nodiscard]] ProjectId projectId() const;
  [[nodiscard]] SchemaSetDigest schemaSet() const;
  [[nodiscard]] ContentDigest rootDigest() const;
  [[nodiscard]] std::size_t contentCount() const;
  [[nodiscard]] std::size_t recordCount() const;
  [[nodiscard]] std::size_t functionCount() const;
  [[nodiscard]] std::size_t callCount() const;
  [[nodiscard]] std::size_t artifactCount() const;

  [[nodiscard]] std::optional<ContentEntry>
  content(const ProjectPath &path) const;
  [[nodiscard]] std::optional<EngineeringRecord> record(RecordId id) const;
  [[nodiscard]] std::optional<ModelFunctionContract>
  function(ModelFunctionId id) const;
  [[nodiscard]] std::optional<ModelCall> call(ModelCallId id) const;
  [[nodiscard]] std::optional<ArtifactMetadata> artifact(ArtifactId id) const;

  [[nodiscard]] std::vector<std::pair<ProjectPath, ContentEntry>>
  content() const;
  [[nodiscard]] std::vector<EngineeringRecord> records() const;
  [[nodiscard]] std::vector<ModelFunctionContract> functions() const;
  [[nodiscard]] std::vector<ModelCall> calls() const;
  [[nodiscard]] std::vector<ArtifactMetadata> artifacts() const;

private:
  struct Data;
  explicit ProjectState(std::shared_ptr<const Data> data)
      : data_(std::move(data)) {}
  std::shared_ptr<const Data> data_;
  friend class internal::ProjectStateAccess;
};

class ProjectSnapshot final {
public:
  ProjectSnapshot(ProjectState state, RevisionId revision)
      : state_(std::move(state)), revision_(std::move(revision)) {}
  [[nodiscard]] const ProjectState &state() const { return state_; }
  [[nodiscard]] const RevisionId &revisionId() const { return revision_; }

private:
  ProjectState state_;
  RevisionId revision_;
};

} // namespace kearne::document
