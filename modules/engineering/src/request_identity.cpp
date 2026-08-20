#include <kearne/engineering/request_identity.hpp>

#include <kearne/engineering/command_intent.hpp>

#include <kearne/api/strong_types.hpp>
#include <kearne/api/v1/options.pb.h>
#include <kearne/api/wire_validation.hpp>

#include <google/protobuf/unknown_field_set.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

namespace kearne::engineering {
namespace {

namespace protobuf = google::protobuf;
namespace wire = api::v1;

Result<void> invalid(std::string code, std::string summary) {
  return std::unexpected(diagnostic(std::move(code), std::move(summary)));
}

std::string_view stringView(const auto &value) {
  return {value.data(), value.size()};
}

Result<void> rejectUnknownFields(const protobuf::Message &message) {
  const protobuf::Reflection *reflection = message.GetReflection();
  if (reflection->GetUnknownFields(message).field_count() != 0)
    return invalid("engineering.request.unknown-field",
                   "executable request contains an unknown field");
  const protobuf::Descriptor *descriptor = message.GetDescriptor();
  for (int fieldIndex = 0; fieldIndex < descriptor->field_count();
       ++fieldIndex) {
    const protobuf::FieldDescriptor *field = descriptor->field(fieldIndex);
    if (field->cpp_type() != protobuf::FieldDescriptor::CPPTYPE_MESSAGE)
      continue;
    const int count = field->is_repeated()
                          ? reflection->FieldSize(message, field)
                          : (reflection->HasField(message, field) ? 1 : 0);
    for (int index = 0; index < count; ++index) {
      const protobuf::Message &child =
          field->is_repeated()
              ? reflection->GetRepeatedMessage(message, field, index)
              : reflection->GetMessage(message, field);
      if (auto result = rejectUnknownFields(child); !result)
        return result;
    }
  }
  return {};
}

Result<void> verifyHandledFields(const protobuf::Descriptor &descriptor,
                                 std::span<const int> handled) {
  if (descriptor.field_count() != static_cast<int>(handled.size()))
    return invalid("engineering.command.identity-field-coverage",
                   "semantic identity field coverage is incomplete");
  std::unordered_set<int> seen;
  for (const int number : handled)
    if (!descriptor.FindFieldByNumber(number) || !seen.insert(number).second)
      return invalid("engineering.command.identity-field-coverage",
                     "semantic identity field coverage is invalid");
  return {};
}

Result<document::ContentEntry>
parseContentReference(const wire::ContentReference &value) {
  auto contentId = api::readDigest<ContentDigest>(value.digest());
  if (!contentId)
    return std::unexpected(std::move(contentId.error()));
  document::ContentEntry entry{*contentId, value.byte_size(),
                               std::string{stringView(value.media_type())}};
  if (auto validation = document::validate(entry); !validation)
    return std::unexpected(std::move(validation.error()));
  return entry;
}

Result<internal::CommandIntent> parseRename(const protobuf::Message &message) {
  const auto &command =
      static_cast<const wire::RenameProjectCommand &>(message);
  auto project = api::readId<ProjectId>(command.project_id());
  if (!project)
    return std::unexpected(std::move(project.error()));
  const std::string_view displayName = stringView(command.display_name());
  if (!document::isValidUtf8(displayName))
    return std::unexpected(
        diagnostic("document.text.invalid-utf8", "text is not valid UTF-8"));
  if (displayName.empty() || displayName.size() > 80)
    return std::unexpected(
        diagnostic("project.name.invalid", "project display name is invalid"));
  return internal::RenameProjectIntent{*project, std::string{displayName}};
}

Result<internal::CreateSourceModuleIntent>
parseCreateSourceValue(const wire::CreateSourceModuleCommand &command) {
  auto project = api::readId<ProjectId>(command.project_id());
  auto path = document::ProjectPath::parse(stringView(command.path()));
  auto content = parseContentReference(command.content());
  if (!project)
    return std::unexpected(std::move(project.error()));
  if (!path)
    return std::unexpected(std::move(path.error()));
  if (!content)
    return std::unexpected(std::move(content.error()));
  return internal::CreateSourceModuleIntent{*project, std::move(*path),
                                            std::move(*content)};
}

Result<internal::CommandIntent>
parseCreateSource(const protobuf::Message &message) {
  auto parsed = parseCreateSourceValue(
      static_cast<const wire::CreateSourceModuleCommand &>(message));
  if (!parsed)
    return std::unexpected(std::move(parsed.error()));
  return internal::CommandIntent{
      std::in_place_type<internal::CreateSourceModuleIntent>,
      std::move(*parsed)};
}

Result<internal::CommandIntent>
parseReplaceSource(const protobuf::Message &message) {
  const auto &command =
      static_cast<const wire::ReplaceSourceModuleCommand &>(message);
  auto project = api::readId<ProjectId>(command.project_id());
  auto path = document::ProjectPath::parse(stringView(command.path()));
  auto expected = api::readDigest<ContentDigest>(command.expected_prior());
  auto content = parseContentReference(command.content());
  if (!project)
    return std::unexpected(std::move(project.error()));
  if (!path)
    return std::unexpected(std::move(path.error()));
  if (!expected)
    return std::unexpected(std::move(expected.error()));
  if (!content)
    return std::unexpected(std::move(content.error()));
  return internal::ReplaceSourceModuleIntent{*project, std::move(*path),
                                             *expected, std::move(*content)};
}

Result<void> writeContentReference(document::CanonicalWriter &writer,
                                   const document::ContentEntry &value) {
  writer.digest(value.digest);
  writer.unsignedInteger(value.byteSize);
  return writer.text(value.mediaType);
}

Result<void> encodeRename(const internal::CommandIntent &intent,
                          document::CanonicalWriter &writer) {
  const auto &command = std::get<internal::RenameProjectIntent>(intent);
  writer.identifier(command.project);
  return writer.text(command.displayName);
}

Result<void> encodeCreateSource(const internal::CommandIntent &intent,
                                document::CanonicalWriter &writer) {
  const auto &command = std::get<internal::CreateSourceModuleIntent>(intent);
  writer.identifier(command.project);
  if (auto result = writer.text(command.path.value()); !result)
    return result;
  return writeContentReference(writer, command.content);
}

Result<void> encodeReplaceSource(const internal::CommandIntent &intent,
                                 document::CanonicalWriter &writer) {
  const auto &command = std::get<internal::ReplaceSourceModuleIntent>(intent);
  writer.identifier(command.project);
  if (auto result = writer.text(command.path.value()); !result)
    return result;
  writer.digest(command.expectedPrior);
  return writeContentReference(writer, command.content);
}

constexpr std::array uuidFields{1};
constexpr std::array digestFields{1, 2};
constexpr std::array contentReferenceFields{1, 2, 3};
constexpr std::array commandEnvelopeFields{1, 2, 3, 4, 5, 6, 20};
constexpr std::array transactionEnvelopeFields{1, 2, 3, 4, 5, 6, 20};
constexpr std::array renameFields{1, 2};
constexpr std::array createSourceFields{1, 2, 3};
constexpr std::array replaceSourceFields{1, 2, 3, 4};

const std::array intentRegistry{
    internal::CommandIntentRegistration{
        wire::RenameProjectCommand::descriptor(), parseRename, encodeRename,
        renameFields},
    internal::CommandIntentRegistration{
        wire::CreateSourceModuleCommand::descriptor(), parseCreateSource,
        encodeCreateSource, createSourceFields},
    internal::CommandIntentRegistration{
        wire::ReplaceSourceModuleCommand::descriptor(), parseReplaceSource,
        encodeReplaceSource, replaceSourceFields},
};
static_assert(std::variant_size_v<internal::CommandIntent> ==
              intentRegistry.size());

const internal::CommandIntentRegistration *
registration(const protobuf::Descriptor *descriptor) {
  const auto found =
      std::ranges::find(intentRegistry, descriptor,
                        &internal::CommandIntentRegistration::descriptor);
  return found == intentRegistry.end() ? nullptr : &*found;
}

const protobuf::Message *payload(const wire::CommandOperation &operation) {
  const protobuf::OneofDescriptor *choice =
      operation.GetDescriptor()->FindOneofByName("payload");
  const protobuf::Reflection *reflection = operation.GetReflection();
  const protobuf::FieldDescriptor *field =
      choice ? reflection->GetOneofFieldDescriptor(operation, choice) : nullptr;
  return field ? &reflection->GetMessage(operation, field) : nullptr;
}

template <typename Envelope, typename OperationAt>
Result<internal::CommandRequestIntent>
parseEnvelope(const Envelope &envelope, internal::RequestEnvelopeKind kind,
              std::size_t operationCount, OperationAt operationAt) {
  static const Result<void> coverage = internal::verifyCommandIntentRegistry();
  if (!coverage)
    return std::unexpected(coverage.error());
  if (auto unknown = rejectUnknownFields(envelope); !unknown)
    return std::unexpected(std::move(unknown.error()));
  if (auto validation = api::validateWire(envelope); !validation)
    return std::unexpected(std::move(validation.error()));

  auto request = api::readId<RequestId>(envelope.request_id());
  auto base = api::readDigest<RevisionId>(envelope.base_revision());
  auto actor = api::readId<ActorId>(envelope.actor_id());
  auto origin = api::readOrigin(envelope.origin());
  auto permission =
      api::readId<PermissionContextId>(envelope.permission_context_id());
  if (!request || !base || !actor || !origin || !permission)
    return std::unexpected(diagnostic("engineering.command.invalid-envelope",
                                      "command envelope conversion failed"));
  std::optional<GestureId> gesture;
  if (envelope.has_gesture_id()) {
    auto converted = api::readId<GestureId>(envelope.gesture_id());
    if (!converted)
      return std::unexpected(std::move(converted.error()));
    gesture = *converted;
  }

  std::vector<internal::CommandIntent> operations;
  operations.reserve(operationCount);
  for (std::size_t index = 0; index < operationCount; ++index) {
    const protobuf::Message *command = payload(operationAt(index));
    const internal::CommandIntentRegistration *entry =
        command ? registration(command->GetDescriptor()) : nullptr;
    if (!entry)
      return std::unexpected(diagnostic("engineering.command.unsupported",
                                        "command is not supported"));
    auto parsed = entry->parse(*command);
    if (!parsed)
      return std::unexpected(std::move(parsed.error()));
    operations.push_back(std::move(*parsed));
  }
  return internal::CommandRequestIntent{
      kind,    *request,    *base,   *actor,
      *origin, *permission, gesture, std::move(operations)};
}

Result<void> encodeWireCommand(const protobuf::Message &message,
                               document::CanonicalWriter &writer) {
  auto intent = internal::parseCommandIntent(message);
  if (!intent)
    return std::unexpected(std::move(intent.error()));
  return internal::commandIntentRegistration(*intent).encode(*intent, writer);
}

} // namespace

namespace internal {

std::span<const CommandIntentRegistration> commandIntentRegistry() {
  return intentRegistry;
}

Result<void> verifyCommandIntentRegistry() {
  const std::array sharedSchemas{
      std::pair{wire::UuidV7::descriptor(), std::span<const int>{uuidFields}},
      std::pair{wire::Digest::descriptor(), std::span<const int>{digestFields}},
      std::pair{wire::ContentReference::descriptor(),
                std::span<const int>{contentReferenceFields}},
      std::pair{wire::CommandEnvelope::descriptor(),
                std::span<const int>{commandEnvelopeFields}},
      std::pair{wire::TransactionEnvelope::descriptor(),
                std::span<const int>{transactionEnvelopeFields}},
  };
  for (const auto &[descriptor, fields] : sharedSchemas)
    if (auto coverage = verifyHandledFields(*descriptor, fields); !coverage)
      return coverage;

  std::unordered_set<const protobuf::Descriptor *> seen;
  std::size_t commands = 0;
  for (const protobuf::Descriptor *descriptor : api::registeredWireTypes()) {
    const wire::MessageRules &rules =
        descriptor->options().GetExtension(wire::message_rules);
    if (rules.surface() != wire::SURFACE_KIND_COMMAND)
      continue;
    ++commands;
    const CommandIntentRegistration *entry = registration(descriptor);
    if (!entry || !entry->parse || !entry->encode)
      return invalid("engineering.command.unhandled",
                     "registered command has no intent handler");
    if (auto coverage = verifyHandledFields(*descriptor, entry->handledFields);
        !coverage)
      return coverage;
    if (!seen.insert(descriptor).second)
      return invalid("engineering.command.intent-duplicate",
                     "command has multiple intent handlers");
  }
  if (commands != intentRegistry.size())
    return invalid("engineering.registry.extra-handler",
                   "intent handler has no registered schema");
  const protobuf::Descriptor &operation = *wire::CommandOperation::descriptor();
  if (operation.field_count() != static_cast<int>(intentRegistry.size()))
    return invalid("engineering.command.intent-operation-coverage",
                   "command operation coverage is incomplete");
  for (int index = 0; index < operation.field_count(); ++index)
    if (!registration(operation.field(index)->message_type()))
      return invalid("engineering.command.intent-operation-coverage",
                     "command operation has no intent handler");
  return {};
}

const CommandIntentRegistration &
commandIntentRegistration(const CommandIntent &intent) {
  return intentRegistry[intent.index()];
}

std::string_view commandIntentPermission(const CommandIntent &intent) {
  const wire::MessageRules &rules =
      commandIntentRegistration(intent).descriptor->options().GetExtension(
          wire::message_rules);
  return stringView(rules.permission());
}

std::string_view commandIntentStableName(const CommandIntent &intent) {
  const wire::MessageRules &rules =
      commandIntentRegistration(intent).descriptor->options().GetExtension(
          wire::message_rules);
  return stringView(rules.stable_name());
}

Result<CommandIntent> parseCommandIntent(const protobuf::Message &message) {
  static const Result<void> coverage = verifyCommandIntentRegistry();
  if (!coverage)
    return std::unexpected(coverage.error());
  if (auto unknown = rejectUnknownFields(message); !unknown)
    return std::unexpected(std::move(unknown.error()));
  if (auto validation = api::validateWire(message); !validation)
    return std::unexpected(std::move(validation.error()));
  const CommandIntentRegistration *entry =
      registration(message.GetDescriptor());
  if (!entry)
    return std::unexpected(diagnostic("engineering.command.unsupported",
                                      "command is not supported"));
  return entry->parse(message);
}

Result<CommandRequestIntent>
parseCommandRequest(const wire::CommandEnvelope &envelope) {
  return parseEnvelope(envelope, RequestEnvelopeKind::Command, 1,
                       [&envelope](std::size_t) -> const auto & {
                         return envelope.operation();
                       });
}

Result<CommandRequestIntent>
parseCommandRequest(const wire::TransactionEnvelope &envelope) {
  return parseEnvelope(envelope, RequestEnvelopeKind::Transaction,
                       static_cast<std::size_t>(envelope.operations_size()),
                       [&envelope](std::size_t index) -> const auto & {
                         return envelope.operations(static_cast<int>(index));
                       });
}

Result<ContentDigest>
semanticRequestDigest(const CommandRequestIntent &intent) {
  const protobuf::Descriptor *envelopeDescriptor =
      intent.kind == RequestEnvelopeKind::Command
          ? wire::CommandEnvelope::descriptor()
          : wire::TransactionEnvelope::descriptor();
  const wire::MessageRules &envelopeRules =
      envelopeDescriptor->options().GetExtension(wire::message_rules);
  document::CanonicalWriter writer;
  writer.header(stringView(envelopeRules.stable_name()),
                envelopeRules.schema_version());
  writer.identifier(intent.request);
  writer.digest(intent.base);
  writer.identifier(intent.actor);
  writer.unsignedInteger(static_cast<std::uint8_t>(intent.origin));
  writer.identifier(intent.permissionContext);
  writer.boolean(intent.gesture.has_value());
  if (intent.gesture)
    writer.identifier(*intent.gesture);
  writer.unsignedInteger(intent.operations.size());
  for (const CommandIntent &operation : intent.operations) {
    const CommandIntentRegistration &entry =
        commandIntentRegistration(operation);
    const wire::MessageRules &rules =
        entry.descriptor->options().GetExtension(wire::message_rules);
    if (auto result = writer.text(stringView(rules.stable_name())); !result)
      return std::unexpected(std::move(result.error()));
    writer.unsignedInteger(rules.schema_version());
    if (auto result = entry.encode(operation, writer); !result)
      return std::unexpected(std::move(result.error()));
  }
  const std::string_view context =
      intent.kind == RequestEnvelopeKind::Command
          ? "kearne.semantic-command-request.v1"
          : "kearne.semantic-transaction-request.v1";
  return document::hashCanonical<ContentDigest>(context, writer.value());
}

} // namespace internal

std::span<const SemanticCommandRegistration> semanticCommandRegistry() {
  static const std::vector<SemanticCommandRegistration> facade = [] {
    std::vector<SemanticCommandRegistration> result;
    result.reserve(intentRegistry.size());
    for (const internal::CommandIntentRegistration &entry : intentRegistry)
      result.push_back(
          {entry.descriptor, encodeWireCommand, entry.handledFields});
    return result;
  }();
  return facade;
}

Result<void> verifySemanticCommandRegistry() {
  return internal::verifyCommandIntentRegistry();
}

Result<ContentDigest>
semanticRequestDigest(const wire::CommandEnvelope &envelope) {
  auto intent = internal::parseCommandRequest(envelope);
  if (!intent)
    return std::unexpected(std::move(intent.error()));
  return internal::semanticRequestDigest(*intent);
}

Result<ContentDigest>
semanticRequestDigest(const wire::TransactionEnvelope &envelope) {
  auto intent = internal::parseCommandRequest(envelope);
  if (!intent)
    return std::unexpected(std::move(intent.error()));
  return internal::semanticRequestDigest(*intent);
}

} // namespace kearne::engineering
