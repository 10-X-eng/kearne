#include <kearne/engineering/revision.hpp>

#include <kearne/document/canonical.hpp>

#include <utility>

namespace kearne::engineering {

Result<document::Bytes> canonicalBytes(const RevisionEnvelope &revision) {
  if (revision.parents.size() > 2 ||
      revision.request.has_value() != revision.requestDigest.has_value() ||
      revision.commandTypes.empty() || revision.mutations.empty())
    return std::unexpected(diagnostic("engineering.revision.invalid",
                                      "revision envelope is invalid"));

  document::CanonicalWriter writer;
  writer.header("revision-envelope", 1);
  writer.unsignedInteger(revision.parents.size());
  for (const RevisionId &parent : revision.parents)
    writer.digest(parent);
  writer.identifier(revision.transaction);
  writer.boolean(revision.request.has_value());
  if (revision.request) {
    writer.identifier(*revision.request);
    writer.digest(*revision.requestDigest);
  }
  writer.identifier(revision.actor);
  writer.unsignedInteger(static_cast<std::uint8_t>(revision.origin));
  writer.boolean(revision.permissionContext.has_value());
  if (revision.permissionContext)
    writer.identifier(*revision.permissionContext);
  writer.boolean(revision.gesture.has_value());
  if (revision.gesture)
    writer.identifier(*revision.gesture);
  writer.digest(revision.schemaSet);
  writer.unsignedInteger(revision.committedAtUnixMilliseconds);
  writer.digest(revision.projectRootDigest);
  writer.unsignedInteger(revision.commandTypes.size());
  for (const std::string &type : revision.commandTypes) {
    if (auto result = writer.text(type); !result)
      return std::unexpected(std::move(result.error()));
  }
  auto mutations = document::canonicalBytes(revision.mutations);
  if (!mutations)
    return std::unexpected(std::move(mutations.error()));
  writer.bytes(*mutations);
  return std::move(writer).take();
}

Result<RevisionId> revisionId(const RevisionEnvelope &revision) {
  auto bytes = canonicalBytes(revision);
  if (!bytes)
    return std::unexpected(std::move(bytes.error()));
  return document::hashCanonical<RevisionId>("kearne.revision.v1", *bytes);
}

} // namespace kearne::engineering
