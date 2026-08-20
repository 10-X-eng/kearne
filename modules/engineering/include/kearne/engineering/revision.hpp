#pragma once

#include <kearne/document/mutation.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace kearne::engineering {

struct RevisionEnvelope {
  std::vector<RevisionId> parents;
  TransactionId transaction;
  std::optional<RequestId> request;
  std::optional<ContentDigest> requestDigest;
  ActorId actor;
  Origin origin;
  std::optional<PermissionContextId> permissionContext;
  std::optional<GestureId> gesture;
  SchemaSetDigest schemaSet;
  std::uint64_t committedAtUnixMilliseconds;
  ContentDigest projectRootDigest;
  std::vector<std::string> commandTypes;
  document::MutationBatch mutations;
};

struct RevisionRecord {
  RevisionId id;
  RevisionEnvelope envelope;
};

[[nodiscard]] Result<document::Bytes>
canonicalBytes(const RevisionEnvelope &revision);
[[nodiscard]] Result<RevisionId> revisionId(const RevisionEnvelope &revision);

} // namespace kearne::engineering
