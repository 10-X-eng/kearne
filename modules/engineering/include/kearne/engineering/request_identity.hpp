#pragma once

#include <kearne/api/v1/engineering.pb.h>
#include <kearne/document/canonical.hpp>

#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>

#include <span>

namespace kearne::engineering {

using SemanticCommandEncoder = Result<void> (*)(
    const google::protobuf::Message &, document::CanonicalWriter &);

struct SemanticCommandRegistration {
  const google::protobuf::Descriptor *descriptor;
  SemanticCommandEncoder encode;
  std::span<const int> handledFields;
};

[[nodiscard]] std::span<const SemanticCommandRegistration>
semanticCommandRegistry();
[[nodiscard]] Result<void> verifySemanticCommandRegistry();

[[nodiscard]] Result<ContentDigest>
semanticRequestDigest(const api::v1::CommandEnvelope &envelope);
[[nodiscard]] Result<ContentDigest>
semanticRequestDigest(const api::v1::TransactionEnvelope &envelope);

} // namespace kearne::engineering
