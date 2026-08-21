#include <kearne/engineering/revision.hpp>

#include <kearne/document/canonical.hpp>

#include <algorithm>
#include <limits>
#include <ranges>
#include <utility>

namespace kearne::engineering {
namespace {

bool validOrigin(Origin value) {
  return value >= Origin::Human && value <= Origin::System;
}

bool validCommandType(std::string_view value) {
  return !value.empty() && value.size() <= 128 && value.front() >= 'a' &&
         value.front() <= 'z' && std::ranges::all_of(value, [](char character) {
           return (character >= 'a' && character <= 'z') ||
                  (character >= '0' && character <= '9') || character == '.' ||
                  character == '_' || character == '-';
         });
}

template <typename Tag>
Result<std::optional<TypedId<Tag>>>
readOptionalId(document::CanonicalReader &reader) {
  auto present = reader.boolean();
  if (!present)
    return std::unexpected(std::move(present.error()));
  if (!*present)
    return std::optional<TypedId<Tag>>{};
  auto value = reader.identifier<Tag>();
  if (!value)
    return std::unexpected(std::move(value.error()));
  return std::optional<TypedId<Tag>>{std::move(*value)};
}

} // namespace

Result<document::Bytes> canonicalBytes(const RevisionEnvelope &revision) {
  if (revision.parents.size() > 2 ||
      revision.request.has_value() != revision.requestDigest.has_value() ||
      !validOrigin(revision.origin) || revision.commandTypes.empty() ||
      !std::ranges::all_of(revision.commandTypes, validCommandType) ||
      revision.mutations.empty())
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

Result<RevisionEnvelope>
decodeRevisionEnvelope(std::span<const std::uint8_t> bytes,
                       RevisionDecodeLimits limits) {
  if (limits.maximumEncodedBytes == 0 || limits.maximumCommandTypes == 0 ||
      limits.maximumCommandTypeBytes == 0 ||
      limits.mutations.maximumEncodedBytes == 0 ||
      limits.mutations.maximumMutations == 0 ||
      limits.mutations.maximumMutationBytes == 0)
    return std::unexpected(diagnostic("engineering.revision.invalid-limits",
                                      "revision decode limits are invalid"));
  if (bytes.size() > limits.maximumEncodedBytes)
    return std::unexpected(diagnostic("engineering.revision.size-limit",
                                      "revision exceeds its byte limit"));
  document::CanonicalReader reader(bytes);
  if (auto result = reader.header("revision-envelope", 1); !result)
    return std::unexpected(std::move(result.error()));
  auto parentCount = reader.unsignedInteger();
  if (!parentCount)
    return std::unexpected(std::move(parentCount.error()));
  if (*parentCount > 2U)
    return std::unexpected(diagnostic("engineering.revision.parent-limit",
                                      "revision has too many parents"));
  std::vector<RevisionId> parents;
  parents.reserve(static_cast<std::size_t>(*parentCount));
  for (std::uint64_t index = 0; index < *parentCount; ++index) {
    auto parent = reader.digest<RevisionIdTag>();
    if (!parent)
      return std::unexpected(std::move(parent.error()));
    parents.push_back(std::move(*parent));
  }
  auto transaction = reader.identifier<TransactionIdTag>();
  if (!transaction)
    return std::unexpected(std::move(transaction.error()));
  auto hasRequest = reader.boolean();
  if (!hasRequest)
    return std::unexpected(std::move(hasRequest.error()));
  std::optional<RequestId> request;
  std::optional<ContentDigest> requestDigest;
  if (*hasRequest) {
    auto decodedRequest = reader.identifier<RequestIdTag>();
    if (!decodedRequest)
      return std::unexpected(std::move(decodedRequest.error()));
    auto decodedDigest = reader.digest<ContentDigestTag>();
    if (!decodedDigest)
      return std::unexpected(std::move(decodedDigest.error()));
    request = std::move(*decodedRequest);
    requestDigest = std::move(*decodedDigest);
  }
  auto actor = reader.identifier<ActorIdTag>();
  if (!actor)
    return std::unexpected(std::move(actor.error()));
  auto origin = reader.unsignedInteger();
  if (!origin || *origin > std::numeric_limits<std::uint8_t>::max())
    return std::unexpected(origin ? diagnostic("engineering.revision.origin",
                                               "revision origin is invalid")
                                  : std::move(origin.error()));
  const auto decodedOrigin = static_cast<Origin>(*origin);
  if (!validOrigin(decodedOrigin))
    return std::unexpected(diagnostic("engineering.revision.origin",
                                      "revision origin is invalid"));
  auto permission = readOptionalId<PermissionContextIdTag>(reader);
  if (!permission)
    return std::unexpected(std::move(permission.error()));
  auto gesture = readOptionalId<GestureIdTag>(reader);
  if (!gesture)
    return std::unexpected(std::move(gesture.error()));
  auto schemaSet = reader.digest<SchemaSetDigestTag>();
  if (!schemaSet)
    return std::unexpected(std::move(schemaSet.error()));
  auto committedAt = reader.unsignedInteger();
  if (!committedAt)
    return std::unexpected(std::move(committedAt.error()));
  auto root = reader.digest<ContentDigestTag>();
  if (!root)
    return std::unexpected(std::move(root.error()));
  auto commandCount = reader.unsignedInteger();
  if (!commandCount)
    return std::unexpected(std::move(commandCount.error()));
  if (*commandCount == 0 || *commandCount > limits.maximumCommandTypes)
    return std::unexpected(
        diagnostic("engineering.revision.command-limit",
                   "revision command type count is outside its limit"));
  std::vector<std::string> commandTypes;
  commandTypes.reserve(static_cast<std::size_t>(*commandCount));
  for (std::uint64_t index = 0; index < *commandCount; ++index) {
    auto commandType = reader.text(limits.maximumCommandTypeBytes);
    if (!commandType)
      return std::unexpected(std::move(commandType.error()));
    if (!validCommandType(*commandType))
      return std::unexpected(diagnostic("engineering.revision.command-type",
                                        "revision command type is invalid"));
    commandTypes.emplace_back(*commandType);
  }
  auto encodedMutations = reader.bytes(std::min(
      limits.maximumEncodedBytes, limits.mutations.maximumEncodedBytes));
  if (!encodedMutations)
    return std::unexpected(std::move(encodedMutations.error()));
  if (auto result = reader.end(); !result)
    return std::unexpected(std::move(result.error()));
  auto mutations =
      document::decodeMutationBatch(*encodedMutations, limits.mutations);
  if (!mutations)
    return std::unexpected(std::move(mutations.error()));
  RevisionEnvelope result{std::move(parents),     std::move(*transaction),
                          std::move(request),     std::move(requestDigest),
                          std::move(*actor),      decodedOrigin,
                          std::move(*permission), std::move(*gesture),
                          std::move(*schemaSet),  *committedAt,
                          std::move(*root),       std::move(commandTypes),
                          std::move(*mutations)};
  auto reencoded = canonicalBytes(result);
  if (!reencoded)
    return std::unexpected(std::move(reencoded.error()));
  if (!std::ranges::equal(*reencoded, bytes))
    return std::unexpected(diagnostic(
        "engineering.revision.noncanonical",
        "revision bytes do not have their canonical representation"));
  return result;
}

Result<RevisionRecord> decodeRevisionRecord(std::span<const std::uint8_t> bytes,
                                            RevisionDecodeLimits limits) {
  auto envelope = decodeRevisionEnvelope(bytes, limits);
  if (!envelope)
    return std::unexpected(std::move(envelope.error()));
  auto id = document::hashCanonical<RevisionId>("kearne.revision.v1", bytes);
  if (!id)
    return std::unexpected(std::move(id.error()));
  return RevisionRecord{std::move(*id), std::move(*envelope)};
}

} // namespace kearne::engineering
