#pragma once

#include <kearne/document/model.hpp>

#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace kearne::document {

struct PutContent {
  ProjectPath path;
  std::optional<ContentDigest> expectedPrior;
  ContentEntry value;
};

struct MoveContent {
  ProjectPath from;
  ProjectPath to;
  ContentDigest expected;
};

struct DeleteContent {
  ProjectPath path;
  ContentDigest expected;
};

template <typename Entity> struct CreateEntity {
  Entity value;
};

template <typename Id, typename Entity> struct ReplaceEntity {
  Id id;
  ContentDigest expected;
  Entity value;
};

template <typename Id> struct DeleteEntity {
  Id id;
  ContentDigest expected;
};

using CreateRecord = CreateEntity<EngineeringRecord>;
using ReplaceRecord = ReplaceEntity<RecordId, EngineeringRecord>;
using DeleteRecord = DeleteEntity<RecordId>;
using CreateFunction = CreateEntity<ModelFunctionContract>;
using ReplaceFunction = ReplaceEntity<ModelFunctionId, ModelFunctionContract>;
using DeleteFunction = DeleteEntity<ModelFunctionId>;
using CreateCall = CreateEntity<ModelCall>;
using ReplaceCall = ReplaceEntity<ModelCallId, ModelCall>;
using DeleteCall = DeleteEntity<ModelCallId>;
using AttachArtifact = CreateEntity<ArtifactMetadata>;
using ReplaceArtifact = ReplaceEntity<ArtifactId, ArtifactMetadata>;
using DetachArtifact = DeleteEntity<ArtifactId>;

using Mutation =
    std::variant<PutContent, MoveContent, DeleteContent, CreateRecord,
                 ReplaceRecord, DeleteRecord, CreateFunction, ReplaceFunction,
                 DeleteFunction, CreateCall, ReplaceCall, DeleteCall,
                 AttachArtifact, ReplaceArtifact, DetachArtifact>;
using MutationBatch = std::vector<Mutation>;

struct MutationDecodeLimits {
  std::size_t maximumEncodedBytes = 512U * 1024U * 1024U;
  std::size_t maximumMutations = 1'000'000U;
  std::size_t maximumMutationBytes = 17U * 1024U * 1024U;
};

[[nodiscard]] Result<Bytes> canonicalBytes(const Mutation &mutation);
[[nodiscard]] Result<Bytes> canonicalBytes(const MutationBatch &batch);
[[nodiscard]] Result<Mutation>
decodeMutation(std::span<const std::uint8_t> bytes,
               std::size_t maximumEncodedBytes =
                   MutationDecodeLimits{}.maximumMutationBytes);
[[nodiscard]] Result<MutationBatch>
decodeMutationBatch(std::span<const std::uint8_t> bytes,
                    MutationDecodeLimits limits = {});

} // namespace kearne::document
