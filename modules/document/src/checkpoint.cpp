#include <kearne/document/checkpoint.hpp>
#include <kearne/document/project_state_access.hpp>

#include <limits>
#include <utility>

namespace kearne::document {
namespace {

Result<std::size_t> checkpointMutationCount(const ProjectState &state) {
  std::size_t count = 0;
  const auto add = [&count](std::size_t value) -> Result<void> {
    if (value > std::numeric_limits<std::size_t>::max() - count)
      return std::unexpected(
          diagnostic("document.checkpoint.count-overflow",
                     "project contains too many values to checkpoint"));
    count += value;
    return {};
  };
  if (auto result = add(state.contentCount()); !result)
    return std::unexpected(std::move(result.error()));
  if (auto result = add(state.recordCount()); !result)
    return std::unexpected(std::move(result.error()));
  if (auto result = add(state.functionCount()); !result)
    return std::unexpected(std::move(result.error()));
  if (auto result = add(state.callCount()); !result)
    return std::unexpected(std::move(result.error()));
  if (auto result = add(state.artifactCount()); !result)
    return std::unexpected(std::move(result.error()));
  return count;
}

Result<MutationBatch> checkpointMutations(const ProjectState &state) {
  auto count = checkpointMutationCount(state);
  if (!count)
    return std::unexpected(std::move(count.error()));
  MutationBatch result;
  result.reserve(*count);
  for (auto &[path, value] : state.content())
    result.emplace_back(
        PutContent{std::move(path), std::nullopt, std::move(value)});
  for (EngineeringRecord &value : state.records())
    result.emplace_back(CreateRecord{std::move(value)});
  for (ModelFunctionContract &value : state.functions())
    result.emplace_back(CreateFunction{std::move(value)});
  for (ModelCall &value : state.calls())
    result.emplace_back(CreateCall{std::move(value)});
  for (ArtifactMetadata &value : state.artifacts())
    result.emplace_back(AttachArtifact{std::move(value)});
  return result;
}

void writeCheckpointPayload(CanonicalWriter &writer, ProjectId project,
                            const SchemaSetDigest &schemaSet,
                            const ContentDigest &root,
                            const RevisionId &revision,
                            std::span<const std::uint8_t> mutations) {
  writer.header("project-checkpoint", 1);
  writer.identifier(project);
  writer.digest(schemaSet);
  writer.digest(root);
  writer.digest(revision);
  writer.bytes(mutations);
}

} // namespace

Result<Bytes> canonicalBytes(const ProjectSnapshot &snapshot,
                             ProjectCheckpointLimits limits) {
  if (limits.maximumEncodedBytes == 0 || limits.maximumMutations == 0 ||
      limits.maximumMutationBytes == 0)
    return std::unexpected(diagnostic("document.checkpoint.invalid-limits",
                                      "checkpoint limits are invalid"));
  if (auto result = internal::ProjectStateAccess::validate(snapshot.state());
      !result)
    return std::unexpected(std::move(result.error()));
  auto count = checkpointMutationCount(snapshot.state());
  if (!count)
    return std::unexpected(std::move(count.error()));
  if (*count > limits.maximumMutations)
    return std::unexpected(diagnostic(
        "document.checkpoint.count-limit",
        "project contains too many values for the checkpoint limit"));
  auto mutations = checkpointMutations(snapshot.state());
  if (!mutations)
    return std::unexpected(std::move(mutations.error()));
  auto encodedMutations = canonicalBytes(*mutations);
  if (!encodedMutations)
    return std::unexpected(std::move(encodedMutations.error()));
  if (encodedMutations->size() > limits.maximumEncodedBytes)
    return std::unexpected(diagnostic(
        "document.checkpoint.size-limit",
        "project checkpoint mutations exceed the encoded byte limit"));

  CanonicalWriter writer;
  writeCheckpointPayload(
      writer, snapshot.state().projectId(), snapshot.state().schemaSet(),
      snapshot.state().rootDigest(), snapshot.revisionId(), *encodedMutations);
  auto checksum =
      hashCanonical<ContentDigest>("kearne.checkpoint.v1", writer.value());
  if (!checksum)
    return std::unexpected(std::move(checksum.error()));
  writer.digest(*checksum);
  if (writer.value().size() > limits.maximumEncodedBytes)
    return std::unexpected(
        diagnostic("document.checkpoint.size-limit",
                   "project checkpoint exceeds the encoded byte limit"));
  return std::move(writer).take();
}

Result<ProjectSnapshot>
decodeProjectCheckpoint(std::span<const std::uint8_t> bytes,
                        ProjectCheckpointLimits limits) {
  if (limits.maximumEncodedBytes == 0 || limits.maximumMutations == 0 ||
      limits.maximumMutationBytes == 0)
    return std::unexpected(diagnostic("document.checkpoint.invalid-limits",
                                      "checkpoint decode limits are invalid"));
  if (bytes.size() > limits.maximumEncodedBytes)
    return std::unexpected(diagnostic("document.checkpoint.size-limit",
                                      "project checkpoint exceeds its limit"));

  CanonicalReader reader(bytes);
  if (auto result = reader.header("project-checkpoint", 1); !result)
    return std::unexpected(std::move(result.error()));
  auto project = reader.identifier<ProjectIdTag>();
  if (!project)
    return std::unexpected(std::move(project.error()));
  auto schemaSet = reader.digest<SchemaSetDigestTag>();
  if (!schemaSet)
    return std::unexpected(std::move(schemaSet.error()));
  auto expectedRoot = reader.digest<ContentDigestTag>();
  if (!expectedRoot)
    return std::unexpected(std::move(expectedRoot.error()));
  auto revision = reader.digest<RevisionIdTag>();
  if (!revision)
    return std::unexpected(std::move(revision.error()));
  auto encodedMutations = reader.bytes(limits.maximumEncodedBytes);
  if (!encodedMutations)
    return std::unexpected(std::move(encodedMutations.error()));
  auto expectedChecksum = reader.digest<ContentDigestTag>();
  if (!expectedChecksum)
    return std::unexpected(std::move(expectedChecksum.error()));
  if (auto result = reader.end(); !result)
    return std::unexpected(std::move(result.error()));

  CanonicalWriter checksumWriter;
  writeCheckpointPayload(checksumWriter, *project, *schemaSet, *expectedRoot,
                         *revision, *encodedMutations);
  auto actualChecksum = hashCanonical<ContentDigest>("kearne.checkpoint.v1",
                                                     checksumWriter.value());
  if (!actualChecksum)
    return std::unexpected(std::move(actualChecksum.error()));
  if (*actualChecksum != *expectedChecksum)
    return std::unexpected(
        diagnostic("document.checkpoint.checksum-mismatch",
                   "project checkpoint bytes failed their integrity check"));

  auto mutations = decodeMutationBatch(
      *encodedMutations, {limits.maximumEncodedBytes, limits.maximumMutations,
                          limits.maximumMutationBytes});
  if (!mutations)
    return std::unexpected(std::move(mutations.error()));
  auto empty = ProjectState::create(std::move(*project), std::move(*schemaSet));
  if (!empty)
    return std::unexpected(std::move(empty.error()));
  auto restored = internal::ProjectStateAccess::apply(*empty, *mutations);
  if (!restored)
    return std::unexpected(std::move(restored.error()));
  if (restored->rootDigest() != *expectedRoot)
    return std::unexpected(
        diagnostic("document.checkpoint.root-mismatch",
                   "project checkpoint does not recreate its declared state"));
  return ProjectSnapshot{std::move(*restored), std::move(*revision)};
}

} // namespace kearne::document
