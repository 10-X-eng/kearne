#pragma once

#include <kearne/document/mutation.hpp>

#include <cstdint>
#include <optional>
#include <span>
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

struct RevisionDecodeLimits {
  std::size_t maximumEncodedBytes = 512U * 1024U * 1024U;
  std::size_t maximumCommandTypes = 4'096U;
  std::size_t maximumCommandTypeBytes = 128U;
  document::MutationDecodeLimits mutations;
};

[[nodiscard]] Result<document::Bytes>
canonicalBytes(const RevisionEnvelope &revision);
[[nodiscard]] Result<RevisionId> revisionId(const RevisionEnvelope &revision);
[[nodiscard]] Result<RevisionEnvelope>
decodeRevisionEnvelope(std::span<const std::uint8_t> bytes,
                       RevisionDecodeLimits limits = {});
[[nodiscard]] Result<RevisionRecord>
decodeRevisionRecord(std::span<const std::uint8_t> bytes,
                     RevisionDecodeLimits limits = {});

} // namespace kearne::engineering
