#pragma once

#include <kearne/engineering/request_identity.hpp>

#include <kearne/document/model.hpp>

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace kearne::engineering::internal {

enum class RequestEnvelopeKind { Command, Transaction };

struct RenameProjectIntent {
  ProjectId project;
  std::string displayName;
};

struct CreateSourceModuleIntent {
  ProjectId project;
  document::ProjectPath path;
  document::ContentEntry content;
};

struct ReplaceSourceModuleIntent {
  ProjectId project;
  document::ProjectPath path;
  ContentDigest expectedPrior;
  document::ContentEntry content;
};

using CommandIntent =
    std::variant<RenameProjectIntent, CreateSourceModuleIntent,
                 ReplaceSourceModuleIntent>;

struct CommandRequestIntent {
  RequestEnvelopeKind kind;
  RequestId request;
  RevisionId base;
  ActorId actor;
  Origin origin;
  PermissionContextId permissionContext;
  std::optional<GestureId> gesture;
  std::vector<CommandIntent> operations;
};

using CommandIntentParser =
    Result<CommandIntent> (*)(const google::protobuf::Message &);
using CommandIntentEncoder = Result<void> (*)(const CommandIntent &,
                                              document::CanonicalWriter &);

struct CommandIntentRegistration {
  const google::protobuf::Descriptor *descriptor;
  CommandIntentParser parse;
  CommandIntentEncoder encode;
  std::span<const int> handledFields;
};

[[nodiscard]] std::span<const CommandIntentRegistration>
commandIntentRegistry();
[[nodiscard]] Result<void> verifyCommandIntentRegistry();
[[nodiscard]] const CommandIntentRegistration &
commandIntentRegistration(const CommandIntent &intent);
[[nodiscard]] std::string_view
commandIntentPermission(const CommandIntent &intent);
[[nodiscard]] std::string_view
commandIntentStableName(const CommandIntent &intent);
[[nodiscard]] Result<CommandIntent>
parseCommandIntent(const google::protobuf::Message &message);

[[nodiscard]] Result<CommandRequestIntent>
parseCommandRequest(const api::v1::CommandEnvelope &envelope);
[[nodiscard]] Result<CommandRequestIntent>
parseCommandRequest(const api::v1::TransactionEnvelope &envelope);
[[nodiscard]] Result<ContentDigest>
semanticRequestDigest(const CommandRequestIntent &intent);

} // namespace kearne::engineering::internal
