#include <kearne/api/strong_types.hpp>
#include <kearne/engineering/request_identity.hpp>
#include <kearne/testkit/property.hpp>

#include <google/protobuf/unknown_field_set.h>

#include <array>
#include <concepts>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using namespace kearne;
namespace wire = kearne::api::v1;
using kearne::engineering::semanticRequestDigest;

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

template <typename Value>
Value id(std::uint64_t timestamp, std::uint64_t randomValue) {
  typename Value::RandomTail tail{};
  for (std::size_t index = 0; index < tail.size(); ++index)
    tail[index] = static_cast<std::uint8_t>(randomValue >> ((index % 8) * 8));
  auto result = Value::create(timestamp, tail);
  require(result.has_value(), "test identifier could not be created");
  return std::move(*result);
}

template <typename Value> Value digest(std::uint8_t fill) {
  typename Value::Bytes bytes{};
  bytes.fill(fill);
  auto result = Value::fromBytes("blake3", bytes);
  require(result.has_value(), "test digest could not be created");
  return std::move(*result);
}

wire::ContentReference
contentReference(std::uint8_t fill = 8,
                 std::string mediaType = "text/x-python; charset=utf-8") {
  wire::ContentReference value;
  api::writeDigest(digest<ContentDigest>(fill), value.mutable_digest());
  value.set_byte_size(4096);
  value.set_media_type(std::move(mediaType));
  return value;
}

wire::CommandOperation renameOperation(std::string displayName) {
  wire::CommandOperation operation;
  auto *rename = operation.mutable_rename_project();
  api::writeId(id<ProjectId>(10, 1), rename->mutable_project_id());
  rename->set_display_name(std::move(displayName));
  return operation;
}

wire::CommandOperation createSourceOperation(std::string path,
                                             std::uint8_t contentFill = 8) {
  wire::CommandOperation operation;
  auto *create = operation.mutable_create_source_module();
  api::writeId(id<ProjectId>(10, 1), create->mutable_project_id());
  create->set_path(std::move(path));
  *create->mutable_content() = contentReference(contentFill);
  return operation;
}

wire::CommandOperation replaceSourceOperation(std::string path,
                                              std::uint8_t expectedFill,
                                              std::uint8_t contentFill) {
  wire::CommandOperation operation;
  auto *replace = operation.mutable_replace_source_module();
  api::writeId(id<ProjectId>(10, 1), replace->mutable_project_id());
  replace->set_path(std::move(path));
  api::writeDigest(digest<ContentDigest>(expectedFill),
                   replace->mutable_expected_prior());
  *replace->mutable_content() = contentReference(contentFill);
  return operation;
}

template <typename Envelope>
void setEnvelopeFields(Envelope &envelope, RequestId request, bool reverse) {
  const RevisionId base = digest<RevisionId>(2);
  const ActorId actor = id<ActorId>(12, 3);
  const PermissionContextId permission = id<PermissionContextId>(13, 4);
  const GestureId gesture = id<GestureId>(14, 5);
  if (!reverse) {
    api::writeId(request, envelope.mutable_request_id());
    api::writeDigest(base, envelope.mutable_base_revision());
    api::writeId(actor, envelope.mutable_actor_id());
    envelope.set_origin(wire::ORIGIN_AI);
    api::writeId(permission, envelope.mutable_permission_context_id());
    api::writeId(gesture, envelope.mutable_gesture_id());
  } else {
    api::writeId(gesture, envelope.mutable_gesture_id());
    api::writeId(permission, envelope.mutable_permission_context_id());
    envelope.set_origin(wire::ORIGIN_AI);
    api::writeId(actor, envelope.mutable_actor_id());
    api::writeDigest(base, envelope.mutable_base_revision());
    api::writeId(request, envelope.mutable_request_id());
  }
}

wire::CommandEnvelope commandEnvelope(const wire::CommandOperation &operation,
                                      RequestId request,
                                      bool reverseFields = false) {
  wire::CommandEnvelope envelope;
  if (!reverseFields) {
    setEnvelopeFields(envelope, request, false);
    *envelope.mutable_operation() = operation;
  } else {
    *envelope.mutable_operation() = operation;
    setEnvelopeFields(envelope, request, true);
  }
  return envelope;
}

wire::TransactionEnvelope
transactionEnvelope(std::span<const wire::CommandOperation> operations,
                    RequestId request, bool reverseFields = false) {
  wire::TransactionEnvelope envelope;
  if (reverseFields)
    for (const wire::CommandOperation &operation : operations)
      *envelope.add_operations() = operation;
  setEnvelopeFields(envelope, request, reverseFields);
  if (!reverseFields)
    for (const wire::CommandOperation &operation : operations)
      *envelope.add_operations() = operation;
  return envelope;
}

void requireError(const auto &result, std::string_view code,
                  const char *message) {
  require(!result && result.error().code == code, message);
}

void verifyDomainsAndOrdering() {
  static_assert(!std::same_as<RequestId, ActorId>);
  static_assert(!std::same_as<ProjectId, GestureId>);
  require(engineering::verifySemanticCommandRegistry().has_value() &&
              !engineering::semanticCommandRegistry().empty(),
          "semantic command registry coverage failed");

  const RequestId request = id<RequestId>(20, 1);
  const wire::CommandOperation rename = renameOperation("Kearne café Δ");
  const wire::CommandEnvelope single = commandEnvelope(rename, request);
  const std::array oneOperation{rename};
  const wire::TransactionEnvelope transaction =
      transactionEnvelope(oneOperation, request);
  auto singleDigest = semanticRequestDigest(single);
  auto transactionDigest = semanticRequestDigest(transaction);
  require(singleDigest && transactionDigest &&
              *singleDigest != *transactionDigest,
          "single and transaction request domains collided");

  const wire::CommandEnvelope reordered =
      commandEnvelope(rename, request, true);
  auto reorderedDigest = semanticRequestDigest(reordered);
  require(reorderedDigest && *reorderedDigest == *singleDigest,
          "protobuf field-setting order changed semantic identity");
  wire::CommandEnvelope retried;
  require(retried.ParseFromString(single.SerializeAsString()),
          "retry envelope did not round trip");
  auto retryDigest = semanticRequestDigest(retried);
  require(retryDigest && *retryDigest == *singleDigest,
          "exact retry changed semantic identity");

  const std::array ordered{renameOperation("first"), renameOperation("second")};
  const std::array reversed{ordered[1], ordered[0]};
  auto orderedDigest =
      semanticRequestDigest(transactionEnvelope(ordered, id<RequestId>(21, 1)));
  auto reversedDigest = semanticRequestDigest(
      transactionEnvelope(reversed, id<RequestId>(21, 1)));
  require(orderedDigest && reversedDigest && *orderedDigest != *reversedDigest,
          "transaction operation order did not affect semantic identity");

  wire::CommandEnvelope swappedIds = single;
  const RequestId actorBytes = id<RequestId>(12, 3);
  const ActorId requestBytes = id<ActorId>(20, 1);
  api::writeId(actorBytes, swappedIds.mutable_request_id());
  api::writeId(requestBytes, swappedIds.mutable_actor_id());
  auto swappedDigest = semanticRequestDigest(swappedIds);
  require(swappedDigest && *swappedDigest != *singleDigest,
          "typed envelope ID roles did not affect semantic identity");
}

void verifyRejectionAndBoundaries() {
  const RequestId request = id<RequestId>(30, 1);
  wire::CommandEnvelope missing;
  requireError(semanticRequestDigest(missing), "api.wire.invalid",
               "missing required request fields were accepted");

  wire::CommandEnvelope nestedUnknown =
      commandEnvelope(renameOperation("unknown"), request);
  auto *rename = nestedUnknown.mutable_operation()->mutable_rename_project();
  rename->GetReflection()->MutableUnknownFields(rename)->AddVarint(999, 1);
  requireError(semanticRequestDigest(nestedUnknown),
               "engineering.request.unknown-field",
               "nested unknown executable field was accepted");
  wire::CommandEnvelope topUnknown =
      commandEnvelope(renameOperation("unknown"), request);
  topUnknown.GetReflection()
      ->MutableUnknownFields(&topUnknown)
      ->AddLengthDelimited(999, "opaque");
  requireError(semanticRequestDigest(topUnknown),
               "engineering.request.unknown-field",
               "top-level unknown executable field was accepted");

  wire::CommandEnvelope invalidUtf8 =
      commandEnvelope(renameOperation(std::string(1, '\xff')), request);
  requireError(semanticRequestDigest(invalidUtf8), "document.text.invalid-utf8",
               "invalid UTF-8 entered semantic request identity");
  wire::CommandEnvelope invalidPath = commandEnvelope(
      createSourceOperation("../escape.py"), id<RequestId>(31, 1));
  requireError(semanticRequestDigest(invalidPath),
               "document.path.invalid-segment",
               "invalid project path entered semantic request identity");

  std::string boundaryPath;
  for (int segment = 0; segment < 5; ++segment) {
    if (!boundaryPath.empty())
      boundaryPath.push_back('/');
    boundaryPath.append(204, static_cast<char>('a' + segment));
  }
  require(boundaryPath.size() == 1024,
          "boundary path generator has wrong size");
  const std::string boundaryMedia = "application/" + std::string(52, 'x');
  require(boundaryMedia.size() == 64,
          "boundary media generator has wrong size");
  wire::CommandOperation boundaryOperation;
  auto *create = boundaryOperation.mutable_create_source_module();
  api::writeId(id<ProjectId>(10, 1), create->mutable_project_id());
  create->set_path(boundaryPath);
  *create->mutable_content() = contentReference(9, boundaryMedia);
  auto boundary = semanticRequestDigest(
      commandEnvelope(boundaryOperation, id<RequestId>(32, 1)));
  require(boundary.has_value(), "valid path/content boundary was rejected");
  create->mutable_content()->set_byte_size(4095);
  auto changed = semanticRequestDigest(
      commandEnvelope(boundaryOperation, id<RequestId>(32, 1)));
  require(changed && *changed != *boundary,
          "content-reference field did not affect semantic identity");

  const RequestId replaceRequest = id<RequestId>(33, 1);
  auto replace = semanticRequestDigest(commandEnvelope(
      replaceSourceOperation("models/part.py", 10, 11), replaceRequest));
  auto changedPrior = semanticRequestDigest(commandEnvelope(
      replaceSourceOperation("models/part.py", 12, 11), replaceRequest));
  auto changedReplacement = semanticRequestDigest(commandEnvelope(
      replaceSourceOperation("models/part.py", 10, 13), replaceRequest));
  require(replace && changedPrior && changedReplacement &&
              *replace != *changedPrior && *replace != *changedReplacement,
          "replace-source fields did not affect semantic identity");
}

void verifyGeneratedIdentity(const testkit::PropertyProfile &profile) {
  testkit::checkProperty(
      "semantic request identity", profile,
      [](testkit::Random &random, std::uint64_t index) {
        const RequestId request = id<RequestId>(100 + index, random.next());
        const std::string name = "generated_" + std::to_string(random.next());
        const wire::CommandOperation operation = renameOperation(name);
        const wire::CommandEnvelope forward =
            commandEnvelope(operation, request, false);
        const wire::CommandEnvelope reverse =
            commandEnvelope(operation, request, true);
        auto forwardDigest = semanticRequestDigest(forward);
        auto reverseDigest = semanticRequestDigest(reverse);
        require(forwardDigest && reverseDigest &&
                    *forwardDigest == *reverseDigest,
                "field-setting metamorphism changed request identity");

        wire::CommandEnvelope changed = forward;
        switch (index % 7) {
        case 0:
          api::writeId(id<RequestId>(101 + index, random.next()),
                       changed.mutable_request_id());
          break;
        case 1:
          api::writeDigest(digest<RevisionId>(4),
                           changed.mutable_base_revision());
          break;
        case 2:
          api::writeId(id<ActorId>(102 + index, random.next()),
                       changed.mutable_actor_id());
          break;
        case 3:
          changed.set_origin(wire::ORIGIN_PLUGIN);
          break;
        case 4:
          api::writeId(id<PermissionContextId>(103 + index, random.next()),
                       changed.mutable_permission_context_id());
          break;
        case 5:
          changed.clear_gesture_id();
          break;
        default:
          changed.mutable_operation()
              ->mutable_rename_project()
              ->set_display_name(name + "_changed");
          break;
        }
        auto changedDigest = semanticRequestDigest(changed);
        require(changedDigest && *changedDigest != *forwardDigest,
                "semantic request field did not affect identity");
      });
}

void verifyMutationFuzz(const testkit::PropertyProfile &profile) {
  const wire::CommandOperation rename = renameOperation("mutation_fuzz");
  const std::array operations{rename, createSourceOperation("models/fuzz.py"),
                              replaceSourceOperation("models/fuzz.py", 14, 15)};
  const wire::CommandEnvelope command =
      commandEnvelope(rename, id<RequestId>(200, 1));
  const wire::TransactionEnvelope transaction =
      transactionEnvelope(operations, id<RequestId>(201, 1));
  testkit::checkProperty(
      "semantic request mutation fuzz", profile,
      [&](testkit::Random &random, std::uint64_t index) {
        std::string bytes = index % 2 == 0 ? command.SerializeAsString()
                                           : transaction.SerializeAsString();
        const std::size_t mutations = static_cast<std::size_t>(index % 4) + 1;
        for (std::size_t mutation = 0; mutation < mutations; ++mutation) {
          if (index % 5 == 0 && bytes.size() < 4096) {
            bytes.push_back(static_cast<char>(random.next()));
          } else if (!bytes.empty()) {
            const std::size_t offset = static_cast<std::size_t>(
                random.next() % static_cast<std::uint64_t>(bytes.size()));
            bytes[offset] ^= static_cast<char>((random.next() & 0xffU) | 1U);
          }
        }
        if (index % 2 == 0) {
          wire::CommandEnvelope parsed;
          if (!parsed.ParseFromString(bytes))
            return;
          auto first = semanticRequestDigest(parsed);
          auto second = semanticRequestDigest(parsed);
          require(first.has_value() == second.has_value() &&
                      (!first || *first == *second),
                  "fuzzed command identity was nondeterministic");
        } else {
          wire::TransactionEnvelope parsed;
          if (!parsed.ParseFromString(bytes))
            return;
          auto first = semanticRequestDigest(parsed);
          auto second = semanticRequestDigest(parsed);
          require(first.has_value() == second.has_value() &&
                      (!first || *first == *second),
                  "fuzzed transaction identity was nondeterministic");
        }
      });
}

} // namespace

int main() {
  try {
    const auto profile = kearne::testkit::propertyProfile();
    verifyDomainsAndOrdering();
    verifyRejectionAndBoundaries();
    verifyGeneratedIdentity(profile);
    verifyMutationFuzz(profile);
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
