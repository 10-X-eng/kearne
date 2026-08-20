#include <kearne/api/strong_types.hpp>
#include <kearne/api/v1/engineering.pb.h>
#include <kearne/api/v1/options.pb.h>
#include <kearne/api/wire_validation.hpp>

#include <google/protobuf/unknown_field_set.h>

#include <set>
#include <stdexcept>
#include <string>

namespace {

namespace protobuf = google::protobuf;
namespace wire = kearne::api::v1;

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

std::string uuidBytes() {
  std::string bytes(16, '\0');
  bytes[6] = static_cast<char>(0x70);
  bytes[8] = static_cast<char>(0x80);
  return bytes;
}

void setUuid(wire::UuidV7 *value) { value->set_value(uuidBytes()); }

void setDigest(wire::Digest *value) {
  value->set_algorithm("sha256");
  value->set_value(std::string(32, '\x2a'));
}

void setContent(wire::ContentReference *value) {
  setDigest(value->mutable_digest());
  value->set_byte_size(4096);
  value->set_media_type("text/x-python; charset=utf-8");
}

wire::CommandEnvelope command() {
  wire::CommandEnvelope value;
  setUuid(value.mutable_request_id());
  setDigest(value.mutable_base_revision());
  setUuid(value.mutable_actor_id());
  value.set_origin(wire::ORIGIN_HUMAN);
  setUuid(value.mutable_permission_context_id());
  wire::RenameProjectCommand *rename =
      value.mutable_operation()->mutable_rename_project();
  setUuid(rename->mutable_project_id());
  rename->set_display_name("Mounting Plate");
  return value;
}

void verifyDescriptors() {
  std::set<std::string> stableNames;
  for (const protobuf::Descriptor *messagePointer :
       kearne::api::registeredWireTypes()) {
    const protobuf::Descriptor &message = *messagePointer;
    require(message.options().HasExtension(wire::message_rules),
            "public message has no rules");
    const wire::MessageRules &rules =
        message.options().GetExtension(wire::message_rules);
    require(!rules.stable_name().empty() && rules.schema_version() != 0 &&
                rules.surface() != wire::SURFACE_KIND_UNSPECIFIED &&
                rules.max_serialized_bytes() != 0,
            "message registration is incomplete");
    require(stableNames.emplace(rules.stable_name()).second,
            "stable message name is duplicated");
    if (rules.surface() == wire::SURFACE_KIND_COMMAND)
      require(!rules.permission().empty(), "command has no permission");
    for (int fieldIndex = 0; fieldIndex < message.field_count(); ++fieldIndex) {
      const protobuf::FieldDescriptor &field = *message.field(fieldIndex);
      if (field.cpp_type() == protobuf::FieldDescriptor::CPPTYPE_STRING ||
          field.is_repeated()) {
        require(field.options().HasExtension(wire::field_rules),
                "bounded field has no rules");
        const wire::FieldRules &fieldRules =
            field.options().GetExtension(wire::field_rules);
        if (field.cpp_type() == protobuf::FieldDescriptor::CPPTYPE_STRING)
          require(fieldRules.max_length() != 0,
                  "string or bytes field is unbounded");
        if (field.is_repeated())
          require(fieldRules.max_items() != 0, "repeated field is unbounded");
      }
    }
  }
}

void verifyValidation() {
  wire::CommandEnvelope value = command();
  require(kearne::api::validateWire(value).has_value(),
          "valid command was rejected");
  value.mutable_operation()->mutable_rename_project()->set_display_name(
      std::string(81, 'x'));
  require(!kearne::api::validateWire(value),
          "oversized command field was accepted");
  value = {};
  require(!kearne::api::validateWire(value),
          "empty command envelope was accepted");
  value = command();
  std::string invalidUuid = uuidBytes();
  invalidUuid[6] = '\0';
  value.mutable_request_id()->set_value(invalidUuid);
  require(!kearne::api::validateWire(value), "non-v7 UUID was accepted");

  wire::QueryEnvelope query;
  setDigest(query.mutable_revision());
  query.set_limit(0);
  setUuid(query.mutable_actor_id());
  query.set_origin(wire::ORIGIN_HUMAN);
  setUuid(query.mutable_permission_context_id());
  setUuid(query.mutable_get_project_metadata()->mutable_project_id());
  require(!kearne::api::validateWire(query), "zero query limit was accepted");
  query.set_limit(1000);
  require(kearne::api::validateWire(query).has_value(),
          "bounded query was rejected");

  value = command();
  value.mutable_operation()->clear_rename_project();
  auto *source = value.mutable_operation()->mutable_create_source_module();
  setUuid(source->mutable_project_id());
  source->set_path("models/bracket.py");
  setContent(source->mutable_content());
  require(kearne::api::validateWire(value).has_value() &&
              value.ByteSizeLong() < 4096,
          "bounded source command was rejected");
  source->mutable_content()->set_byte_size(16777217);
  require(!kearne::api::validateWire(value),
          "oversized source content reference was accepted");
  source->mutable_content()->set_byte_size(4096);
  source->set_path(std::string(1025, 'x'));
  require(!kearne::api::validateWire(value),
          "oversized project path was accepted");

  wire::TransactionEnvelope transaction;
  *transaction.mutable_request_id() = value.request_id();
  *transaction.mutable_base_revision() = value.base_revision();
  *transaction.mutable_actor_id() = value.actor_id();
  transaction.set_origin(value.origin());
  *transaction.mutable_permission_context_id() = value.permission_context_id();
  for (int index = 0; index < 16; ++index)
    *transaction.add_operations() = command().operation();
  require(kearne::api::validateWire(transaction).has_value() &&
              transaction.ByteSizeLong() < 4096,
          "bounded transaction was rejected");
  *transaction.add_operations() = command().operation();
  require(!kearne::api::validateWire(transaction),
          "transaction operation limit was not enforced");
  transaction.clear_operations();
  require(!kearne::api::validateWire(transaction),
          "empty transaction was accepted");
}

void verifyStrongTypes() {
  wire::UuidV7 id;
  setUuid(&id);
  const auto project = kearne::api::readId<kearne::ProjectId>(id);
  require(project.has_value(), "valid wire ID did not convert");
  wire::UuidV7 recoveredId;
  kearne::api::writeId(*project, &recoveredId);
  require(recoveredId.SerializeAsString() == id.SerializeAsString(),
          "strong ID conversion did not round trip");

  wire::Digest digest;
  setDigest(&digest);
  const auto revision = kearne::api::readDigest<kearne::RevisionId>(digest);
  require(revision.has_value(), "valid wire digest did not convert");
  wire::Digest recoveredDigest;
  kearne::api::writeDigest(*revision, &recoveredDigest);
  require(recoveredDigest.SerializeAsString() == digest.SerializeAsString(),
          "strong digest conversion did not round trip");
  require(kearne::api::readOrigin(wire::ORIGIN_AI) == kearne::Origin::AI,
          "wire origin did not convert");
}

void verifyUnknownFields() {
  const std::string encoded =
      command().SerializeAsString() + std::string("\xa2\x06\x03new", 6);
  wire::CommandEnvelope recovered;
  require(recovered.ParseFromString(encoded), "unknown field did not parse");
  const protobuf::UnknownFieldSet &unknown =
      recovered.GetReflection()->GetUnknownFields(recovered);
  require(unknown.field_count() == 1 && unknown.field(0).number() == 100 &&
              unknown.field(0).length_delimited() == "new",
          "unknown field was not preserved");

  wire::OpaqueEntity entity;
  entity.set_kind("vendor.future");
  entity.set_schema_version(91);
  entity.set_payload(std::string("\0\xffopaque", 8));
  wire::OpaqueEntity roundTrip;
  require(roundTrip.ParseFromString(entity.SerializeAsString()) &&
              roundTrip.SerializeAsString() == entity.SerializeAsString(),
          "opaque entity changed during round trip");
}

} // namespace

int main() {
  verifyDescriptors();
  verifyValidation();
  verifyStrongTypes();
  verifyUnknownFields();
  return 0;
}
