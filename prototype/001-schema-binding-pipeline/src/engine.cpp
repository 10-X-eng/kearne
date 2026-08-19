#include "engine.hpp"

#include "options.pb.h"

#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>

#include <algorithm>
#include <cctype>
#include <limits>
#include <string_view>
#include <utility>

namespace kearne::schema_prototype {
namespace {

namespace wire = kearne::schema::v1;

std::string childPath(std::string_view parent, std::string_view field) {
  return parent.empty() ? std::string(field)
                        : std::string(parent) + "." + std::string(field);
}

double numericValue(const google::protobuf::Message &message,
                    const google::protobuf::FieldDescriptor &field, int index) {
  const google::protobuf::Reflection *reflection = message.GetReflection();
  const bool repeated = field.is_repeated();
  switch (field.cpp_type()) {
  case google::protobuf::FieldDescriptor::CPPTYPE_INT32:
    return repeated ? reflection->GetRepeatedInt32(message, &field, index)
                    : reflection->GetInt32(message, &field);
  case google::protobuf::FieldDescriptor::CPPTYPE_INT64:
    return repeated
               ? static_cast<double>(
                     reflection->GetRepeatedInt64(message, &field, index))
               : static_cast<double>(reflection->GetInt64(message, &field));
  case google::protobuf::FieldDescriptor::CPPTYPE_UINT32:
    return repeated ? reflection->GetRepeatedUInt32(message, &field, index)
                    : reflection->GetUInt32(message, &field);
  case google::protobuf::FieldDescriptor::CPPTYPE_UINT64:
    return repeated
               ? static_cast<double>(
                     reflection->GetRepeatedUInt64(message, &field, index))
               : static_cast<double>(reflection->GetUInt64(message, &field));
  case google::protobuf::FieldDescriptor::CPPTYPE_FLOAT:
    return repeated ? reflection->GetRepeatedFloat(message, &field, index)
                    : reflection->GetFloat(message, &field);
  case google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE:
    return repeated ? reflection->GetRepeatedDouble(message, &field, index)
                    : reflection->GetDouble(message, &field);
  default:
    return std::numeric_limits<double>::quiet_NaN();
  }
}

std::optional<ValidationError>
validateMessage(const google::protobuf::Message &message,
                const std::string &path) {
  const google::protobuf::Descriptor *descriptor = message.GetDescriptor();
  const google::protobuf::Reflection *reflection = message.GetReflection();
  const auto &options = descriptor->options();
  if (options.HasExtension(wire::message_policy)) {
    const wire::MessagePolicy &policy =
        options.GetExtension(wire::message_policy);
    if (policy.max_serialized_bytes() > 0 &&
        message.ByteSizeLong() > policy.max_serialized_bytes()) {
      return ValidationError{"validation.message_too_large", path};
    }
    if (!policy.required_oneof().empty()) {
      const google::protobuf::OneofDescriptor *oneof =
          descriptor->FindOneofByName(policy.required_oneof());
      if (!oneof || !reflection->HasOneof(message, oneof)) {
        return ValidationError{"validation.required_oneof",
                               childPath(path, policy.required_oneof())};
      }
    }
  }

  for (int fieldIndex = 0; fieldIndex < descriptor->field_count();
       ++fieldIndex) {
    const google::protobuf::FieldDescriptor *field =
        descriptor->field(fieldIndex);
    const auto &fieldOptions = field->options();
    const wire::FieldPolicy *policy =
        fieldOptions.HasExtension(wire::field_policy)
            ? &fieldOptions.GetExtension(wire::field_policy)
            : nullptr;
    const int count = field->is_repeated()
                          ? reflection->FieldSize(message, field)
                          : (reflection->HasField(message, field) ? 1 : 0);
    const std::string fieldPath = childPath(path, field->name());
    if (policy && policy->required() && count == 0)
      return ValidationError{"validation.required", fieldPath};
    if (policy && field->is_repeated() && policy->max_items() > 0 &&
        count > static_cast<int>(policy->max_items())) {
      return ValidationError{"validation.max_items", fieldPath};
    }

    for (int valueIndex = 0; valueIndex < count; ++valueIndex) {
      if (field->cpp_type() ==
          google::protobuf::FieldDescriptor::CPPTYPE_STRING) {
        const std::string value =
            field->is_repeated()
                ? reflection->GetRepeatedString(message, field, valueIndex)
                : reflection->GetString(message, field);
        if (policy && value.size() < policy->min_length())
          return ValidationError{"validation.min_length", fieldPath};
        if (policy && policy->max_length() > 0 &&
            value.size() > policy->max_length())
          return ValidationError{"validation.max_length", fieldPath};
      } else if (field->cpp_type() ==
                 google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE) {
        const google::protobuf::Message &child =
            field->is_repeated()
                ? reflection->GetRepeatedMessage(message, field, valueIndex)
                : reflection->GetMessage(message, field);
        if (auto error = validateMessage(child, fieldPath))
          return error;
      } else if (policy) {
        const double value = numericValue(message, *field, valueIndex);
        if (policy->has_minimum() && value < policy->minimum())
          return ValidationError{"validation.minimum", fieldPath};
        if (policy->has_maximum() && value > policy->maximum())
          return ValidationError{"validation.maximum", fieldPath};
      }
    }
  }
  return std::nullopt;
}

wire::Diagnostic diagnostic(std::string code, std::string path = {}) {
  wire::Diagnostic result;
  result.set_code(std::move(code));
  if (!path.empty())
    result.set_field_path(std::move(path));
  return result;
}

wire::RpcResponse validationResponse(const wire::RpcRequest &request,
                                     const ValidationError &error) {
  wire::RpcResponse response;
  if (request.has_command()) {
    response.mutable_command()->set_committed(false);
    *response.mutable_command()->mutable_diagnostic() =
        diagnostic(error.code, error.path);
  } else if (request.has_query()) {
    *response.mutable_query()->mutable_diagnostic() =
        diagnostic(error.code, error.path);
  } else {
    *response.mutable_transport_diagnostic() =
        diagnostic(error.code, error.path);
  }
  return response;
}

template <typename Tag> class TypedId {
public:
  explicit TypedId(std::string_view value) : value_(value) {}
  const std::string &value() const { return value_; }

private:
  std::string value_;
};

struct ProjectTag;
struct RevisionTag;
struct RequestTag;
using ProjectId = TypedId<ProjectTag>;
using RevisionId = TypedId<RevisionTag>;
using RequestId = TypedId<RequestTag>;

class DisplayName {
public:
  explicit DisplayName(std::string_view value) : value_(value) {}
  const std::string &value() const { return value_; }

private:
  std::string value_;
};

struct RenameCommand {
  RequestId requestId;
  RevisionId baseRevisionId;
  ProjectId projectId;
  DisplayName displayName;
};

std::optional<ValidationError>
normalizeRename(const wire::CommandEnvelope &envelope, RenameCommand &command) {
  if (!envelope.has_rename_project())
    return ValidationError{"command.unsupported", "command.payload"};
  const std::string_view name = envelope.rename_project().display_name();
  if (std::any_of(name.begin(), name.end(), [](unsigned char value) {
        return value < 0x20 || value == 0x7f;
      })) {
    return ValidationError{"project.display_name_control_character",
                           "command.rename_project.display_name"};
  }
  command = RenameCommand{
      RequestId(envelope.request_id()), RevisionId(envelope.base_revision_id()),
      ProjectId(envelope.rename_project().project_id()), DisplayName(name)};
  return std::nullopt;
}

} // namespace

std::optional<ValidationError>
validateWire(const google::protobuf::Message &message) {
  return validateMessage(message,
                         std::string(message.GetDescriptor()->full_name()));
}

void parseAndValidate(std::span<const std::byte> bytes) {
  if (bytes.size() > kMaxFrameBytes)
    return;
  wire::RpcRequest request;
  if (request.ParseFromArray(bytes.data(), static_cast<int>(bytes.size())))
    (void)validateWire(request);
}

wire::RpcResponse Engine::handle(const wire::RpcRequest &request) {
  if (auto error = validateWire(request))
    return validationResponse(request, *error);

  wire::RpcResponse response;
  if (request.has_command()) {
    RenameCommand command{RequestId(""), RevisionId(""), ProjectId(""),
                          DisplayName("")};
    if (auto error = normalizeRename(request.command(), command))
      return validationResponse(request, *error);
    wire::CommandResult *result = response.mutable_command();
    if (command.baseRevisionId.value() != revisionId_) {
      *result->mutable_diagnostic() =
          diagnostic("revision.conflict", "command.base_revision_id");
      return response;
    }
    if (command.projectId.value() != projectId_) {
      *result->mutable_diagnostic() =
          diagnostic("project.not_found", "command.project_id");
      return response;
    }
    displayName_ = command.displayName.value();
    ++revisionSequence_;
    const std::string sequence = std::to_string(revisionSequence_);
    const std::size_t padding = sequence.size() < 4 ? 4 - sequence.size() : 0;
    revisionId_ = "revision-" + std::string(padding, '0') + sequence;
    result->set_committed(true);
    result->set_revision_id(revisionId_);
    return response;
  }

  const wire::QueryEnvelope &query = request.query();
  wire::ProjectMetadataResult *result = response.mutable_query();
  result->set_observed_revision_id(query.revision_id());
  if (query.revision_id() != revisionId_) {
    *result->mutable_diagnostic() =
        diagnostic("revision.unavailable", "query.revision_id");
    return response;
  }
  if (!query.has_get_project_metadata() ||
      query.get_project_metadata().project_id() != projectId_) {
    *result->mutable_diagnostic() =
        diagnostic("project.not_found", "query.project_id");
    return response;
  }
  result->set_project_id(projectId_);
  result->set_display_name(displayName_);
  return response;
}

wire::RpcRequest makeRenameRequest(std::string displayName) {
  wire::RpcRequest request;
  wire::CommandEnvelope *command = request.mutable_command();
  command->set_request_id("request-0001");
  command->set_base_revision_id("revision-0000");
  command->mutable_rename_project()->set_project_id("project-01");
  command->mutable_rename_project()->set_display_name(std::move(displayName));
  return request;
}

wire::RpcRequest makeMetadataQuery(std::string revisionId) {
  wire::RpcRequest request;
  wire::QueryEnvelope *query = request.mutable_query();
  query->set_revision_id(std::move(revisionId));
  query->set_limit(1);
  query->mutable_get_project_metadata()->set_project_id("project-01");
  return request;
}

} // namespace kearne::schema_prototype
