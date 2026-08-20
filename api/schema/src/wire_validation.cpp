#include <kearne/api/wire_validation.hpp>

#include <kearne/api/v1/engineering.pb.h>
#include <kearne/api/v1/options.pb.h>
#include <kearne/api/v1/sketch.pb.h>
#include <kearne/api/v1/worker.pb.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <ranges>
#include <string>
#include <vector>

namespace kearne::api {
namespace {

namespace protobuf = google::protobuf;

Result<void> invalid(const protobuf::FieldDescriptor &field,
                     std::string reason) {
  Diagnostic value = diagnostic("api.wire.invalid", std::move(reason));
  value.parameters.emplace_back(field.full_name());
  return std::unexpected(std::move(value));
}

std::size_t length(const protobuf::Message &message,
                   const protobuf::FieldDescriptor &field, int index) {
  const protobuf::Reflection *reflection = message.GetReflection();
  return field.is_repeated()
             ? reflection->GetRepeatedString(message, &field, index).size()
             : reflection->GetString(message, &field).size();
}

double numericValue(const protobuf::Message &message,
                    const protobuf::FieldDescriptor &field, int index) {
  const protobuf::Reflection *reflection = message.GetReflection();
  using Kind = protobuf::FieldDescriptor::CppType;
  switch (field.cpp_type()) {
  case Kind::CPPTYPE_INT32:
    return field.is_repeated()
               ? reflection->GetRepeatedInt32(message, &field, index)
               : reflection->GetInt32(message, &field);
  case Kind::CPPTYPE_INT64:
    return static_cast<double>(
        field.is_repeated()
            ? reflection->GetRepeatedInt64(message, &field, index)
            : reflection->GetInt64(message, &field));
  case Kind::CPPTYPE_UINT32:
    return field.is_repeated()
               ? reflection->GetRepeatedUInt32(message, &field, index)
               : reflection->GetUInt32(message, &field);
  case Kind::CPPTYPE_UINT64:
    return static_cast<double>(
        field.is_repeated()
            ? reflection->GetRepeatedUInt64(message, &field, index)
            : reflection->GetUInt64(message, &field));
  case Kind::CPPTYPE_FLOAT:
    return field.is_repeated()
               ? reflection->GetRepeatedFloat(message, &field, index)
               : reflection->GetFloat(message, &field);
  case Kind::CPPTYPE_DOUBLE:
    return field.is_repeated()
               ? reflection->GetRepeatedDouble(message, &field, index)
               : reflection->GetDouble(message, &field);
  default:
    return 0.0;
  }
}

Result<void> validateMessage(const protobuf::Message &message) {
  const protobuf::Descriptor &descriptor = *message.GetDescriptor();
  const protobuf::Reflection *reflection = message.GetReflection();
  if (!descriptor.options().HasExtension(v1::message_rules))
    return std::unexpected(
        diagnostic("api.wire.unregistered", "wire message is not registered"));
  const v1::MessageRules &messageRules =
      descriptor.options().GetExtension(v1::message_rules);
  if (message.ByteSizeLong() > messageRules.max_serialized_bytes())
    return std::unexpected(
        diagnostic("api.wire.too-large", "wire message exceeds its limit"));
  if (&descriptor == v1::UuidV7::descriptor()) {
    const protobuf::FieldDescriptor &field =
        *descriptor.FindFieldByName("value");
    const std::string bytes = reflection->GetString(message, &field);
    if (bytes.size() != 16 ||
        (static_cast<unsigned char>(bytes[6]) >> 4U) != 7U ||
        (static_cast<unsigned char>(bytes[8]) & 0xc0U) != 0x80U)
      return invalid(field, "identifier is not an RFC 9562 UUIDv7");
  }
  if (&descriptor == v1::Digest::descriptor()) {
    const protobuf::FieldDescriptor &field =
        *descriptor.FindFieldByName("algorithm");
    const std::string algorithm = reflection->GetString(message, &field);
    if (!std::ranges::all_of(algorithm, [](const char character) {
          return (character >= 'a' && character <= 'z') ||
                 (character >= '0' && character <= '9') || character == '-';
        }))
      return invalid(field, "digest algorithm ID is invalid");
  }
  for (const std::string &oneofName : messageRules.required_oneof()) {
    const protobuf::OneofDescriptor *oneof =
        descriptor.FindOneofByName(oneofName);
    if (!oneof || !reflection->HasOneof(message, oneof))
      return std::unexpected(diagnostic("api.wire.required-oneof",
                                        "required message choice is missing"));
  }

  for (int fieldIndex = 0; fieldIndex < descriptor.field_count();
       ++fieldIndex) {
    const protobuf::FieldDescriptor &field = *descriptor.field(fieldIndex);
    const int count = field.is_repeated()
                          ? reflection->FieldSize(message, &field)
                          : (reflection->HasField(message, &field) ? 1 : 0);
    if (field.options().HasExtension(v1::field_rules)) {
      const v1::FieldRules &rules =
          field.options().GetExtension(v1::field_rules);
      if (rules.required() && count == 0)
        return invalid(field, "required field is missing");
      if (rules.max_items() != 0 && count > static_cast<int>(rules.max_items()))
        return invalid(field, "field has too many values");
      if (field.cpp_type() == protobuf::FieldDescriptor::CPPTYPE_STRING) {
        for (int index = 0; index < count; ++index) {
          const std::size_t size = length(message, field, index);
          if (size < rules.min_length() ||
              (rules.max_length() != 0 && size > rules.max_length()))
            return invalid(field, "field length is outside its limits");
        }
      }
      if (rules.has_numeric_range()) {
        for (int index = 0; index < count; ++index) {
          const double value = numericValue(message, field, index);
          if (!std::isfinite(value) || value < rules.minimum() ||
              value > rules.maximum())
            return invalid(field, "field value is outside its limits");
        }
      }
      if (rules.disallow_default() && count != 0 &&
          field.cpp_type() == protobuf::FieldDescriptor::CPPTYPE_ENUM &&
          reflection->GetEnumValue(message, &field) == 0)
        return invalid(field, "field uses its unspecified value");
    }
    if (field.cpp_type() == protobuf::FieldDescriptor::CPPTYPE_MESSAGE) {
      for (int index = 0; index < count; ++index) {
        const protobuf::Message &child =
            field.is_repeated()
                ? reflection->GetRepeatedMessage(message, &field, index)
                : reflection->GetMessage(message, &field);
        if (auto result = validateMessage(child); !result)
          return result;
      }
    }
  }
  return {};
}

} // namespace

Result<void> validateWire(const google::protobuf::Message &message) {
  return validateMessage(message);
}

std::span<const google::protobuf::Descriptor *const> registeredWireTypes() {
  static const std::vector<const protobuf::Descriptor *> descriptors = [] {
    std::vector<const protobuf::Descriptor *> result;
    const auto append =
        [&result](const auto &self,
                  const protobuf::Descriptor &descriptor) -> void {
      result.push_back(&descriptor);
      for (int index = 0; index < descriptor.nested_type_count(); ++index)
        self(self, *descriptor.nested_type(index));
    };
    const std::array files{v1::CommandEnvelope::descriptor()->file(),
                           v1::SketchDefinition::descriptor()->file(),
                           v1::WorkerJobEnvelope::descriptor()->file()};
    for (const protobuf::FileDescriptor *file : files)
      for (int index = 0; index < file->message_type_count(); ++index)
        append(append, *file->message_type(index));
    return result;
  }();
  return descriptors;
}

} // namespace kearne::api
