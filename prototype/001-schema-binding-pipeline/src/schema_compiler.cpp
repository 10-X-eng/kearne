#include "api.pb.h"
#include "options.pb.h"

#include <google/protobuf/descriptor.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace wire = kearne::schema::v1;

std::string jsonQuoted(std::string_view value) {
  std::ostringstream output;
  output << '"';
  for (unsigned char character : value) {
    switch (character) {
    case '"':
      output << "\\\"";
      break;
    case '\\':
      output << "\\\\";
      break;
    case '\b':
      output << "\\b";
      break;
    case '\f':
      output << "\\f";
      break;
    case '\n':
      output << "\\n";
      break;
    case '\r':
      output << "\\r";
      break;
    case '\t':
      output << "\\t";
      break;
    default:
      if (character < 0x20)
        output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
               << static_cast<int>(character) << std::dec;
      else
        output << static_cast<char>(character);
    }
  }
  output << '"';
  return output.str();
}

std::string number(double value) {
  std::ostringstream output;
  output << std::setprecision(std::numeric_limits<double>::max_digits10)
         << value;
  return output.str();
}

const wire::MessagePolicy &
messagePolicy(const google::protobuf::Descriptor &descriptor) {
  const auto &options = descriptor.options();
  if (!options.HasExtension(wire::message_policy))
    throw std::runtime_error(std::string(descriptor.full_name()) +
                             " lacks message_policy");
  const wire::MessagePolicy &policy =
      options.GetExtension(wire::message_policy);
  if (policy.stable_id().empty() || policy.schema_version() == 0 ||
      policy.max_serialized_bytes() == 0) {
    throw std::runtime_error(std::string(descriptor.full_name()) +
                             " has incomplete message_policy");
  }
  return policy;
}

const wire::FieldPolicy *
fieldPolicy(const google::protobuf::FieldDescriptor &field) {
  const auto &options = field.options();
  return options.HasExtension(wire::field_policy)
             ? &options.GetExtension(wire::field_policy)
             : nullptr;
}

void verifyBound(const google::protobuf::FieldDescriptor &field) {
  const wire::FieldPolicy *policy = fieldPolicy(field);
  if ((field.cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_STRING) &&
      (!policy || policy->max_length() == 0)) {
    throw std::runtime_error(std::string(field.full_name()) +
                             " lacks max_length");
  }
  if (field.is_repeated() && (!policy || policy->max_items() == 0))
    throw std::runtime_error(std::string(field.full_name()) +
                             " lacks max_items");
}

std::string fieldSchema(const google::protobuf::FieldDescriptor &field) {
  verifyBound(field);
  const wire::FieldPolicy *policy = fieldPolicy(field);
  std::vector<std::string> members;
  switch (field.cpp_type()) {
  case google::protobuf::FieldDescriptor::CPPTYPE_STRING:
    members.push_back("\"type\":\"string\"");
    if (field.type() == google::protobuf::FieldDescriptor::TYPE_BYTES)
      members.push_back("\"contentEncoding\":\"base64\"");
    if (policy && policy->min_length() > 0)
      members.push_back("\"minLength\":" +
                        std::to_string(policy->min_length()));
    if (policy && policy->max_length() > 0)
      members.push_back("\"maxLength\":" +
                        std::to_string(policy->max_length()));
    break;
  case google::protobuf::FieldDescriptor::CPPTYPE_BOOL:
    members.push_back("\"type\":\"boolean\"");
    break;
  case google::protobuf::FieldDescriptor::CPPTYPE_FLOAT:
  case google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE:
    members.push_back("\"type\":\"number\"");
    break;
  case google::protobuf::FieldDescriptor::CPPTYPE_ENUM: {
    members.push_back("\"type\":\"string\"");
    std::string values = "\"enum\":[";
    for (int index = 0; index < field.enum_type()->value_count(); ++index) {
      if (index > 0)
        values += ',';
      values += jsonQuoted(field.enum_type()->value(index)->name());
    }
    values += ']';
    members.push_back(std::move(values));
    break;
  }
  case google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE:
    members.push_back(
        "\"$ref\":" +
        jsonQuoted(std::string("#/$defs/") +
                   std::string(field.message_type()->full_name())));
    break;
  default:
    members.push_back("\"type\":\"integer\"");
  }
  if (policy && policy->has_minimum())
    members.push_back("\"minimum\":" + number(policy->minimum()));
  if (policy && policy->has_maximum())
    members.push_back("\"maximum\":" + number(policy->maximum()));
  if (policy && !policy->description().empty())
    members.push_back("\"description\":" + jsonQuoted(policy->description()));
  if (policy && !policy->semantic_type().empty())
    members.push_back("\"x-kearne-semantic-type\":" +
                      jsonQuoted(policy->semantic_type()));
  if (policy && !policy->dimension().empty())
    members.push_back("\"x-kearne-dimension\":" +
                      jsonQuoted(policy->dimension()));

  std::string result = "{";
  for (std::size_t index = 0; index < members.size(); ++index) {
    if (index > 0)
      result += ',';
    result += members[index];
  }
  result += '}';
  if (field.is_repeated()) {
    result = "{\"type\":\"array\",\"maxItems\":" +
             std::to_string(policy->max_items()) + ",\"items\":" + result + '}';
  }
  return result;
}

std::string jsonSchema(const google::protobuf::Descriptor &descriptor) {
  const wire::MessagePolicy &policy = messagePolicy(descriptor);
  std::vector<const google::protobuf::FieldDescriptor *> fields;
  for (int index = 0; index < descriptor.field_count(); ++index)
    fields.push_back(descriptor.field(index));
  std::sort(fields.begin(), fields.end(),
            [](const auto *left, const auto *right) {
              return left->number() < right->number();
            });

  std::string properties = "{";
  std::string required = "[";
  bool firstProperty = true;
  bool firstRequired = true;
  for (const google::protobuf::FieldDescriptor *field : fields) {
    if (field->containing_oneof())
      continue;
    if (!firstProperty)
      properties += ',';
    firstProperty = false;
    properties += jsonQuoted(field->json_name()) + ':' + fieldSchema(*field);
    const wire::FieldPolicy *fieldOptions = fieldPolicy(*field);
    if (fieldOptions && fieldOptions->required()) {
      if (!firstRequired)
        required += ',';
      firstRequired = false;
      required += jsonQuoted(field->json_name());
    }
  }
  properties += '}';
  required += ']';
  return "{\"$schema\":\"https://json-schema.org/draft/2020-12/schema\","
         "\"$id\":" +
         jsonQuoted(std::string("urn:kearne:") +
                    std::string(policy.stable_id()) + ":v" +
                    std::to_string(policy.schema_version())) +
         ",\"type\":\"object\",\"additionalProperties\":false,\"properties\":" +
         properties + ",\"required\":" + required + '}';
}

std::string descriptorJson(const google::protobuf::Descriptor &descriptor) {
  const wire::MessagePolicy &policy = messagePolicy(descriptor);
  if (policy.permission().empty() || policy.summary().empty() ||
      policy.documentation_key().empty()) {
    throw std::runtime_error(std::string(descriptor.full_name()) +
                             " lacks public descriptor metadata");
  }
  return "{\"stable_id\":" + jsonQuoted(policy.stable_id()) +
         ",\"schema_version\":" + std::to_string(policy.schema_version()) +
         ",\"permission\":" + jsonQuoted(policy.permission()) +
         ",\"summary\":" + jsonQuoted(policy.summary()) +
         ",\"documentation_key\":" + jsonQuoted(policy.documentation_key()) +
         ",\"ai_exposed\":" + (policy.ai_exposed() ? "true" : "false") +
         ",\"input_schema\":" + jsonSchema(descriptor) + '}';
}

void writeFile(const std::filesystem::path &path, const std::string &contents) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output)
    throw std::runtime_error("cannot open " + path.string());
  output << contents << '\n';
  if (!output)
    throw std::runtime_error("cannot write " + path.string());
}

} // namespace

int main(int argc, char **argv) {
  try {
    if (argc != 3 || std::string(argv[1]) != "--output")
      return 2;
    const std::filesystem::path outputDirectory = argv[2];
    std::filesystem::create_directories(outputDirectory);

    const google::protobuf::Descriptor &command =
        *wire::RenameProjectRequest::descriptor();
    const google::protobuf::Descriptor &query =
        *wire::GetProjectMetadataQuery::descriptor();
    const std::string commandJson = descriptorJson(command);
    const std::string queryJson = descriptorJson(query);
    writeFile(outputDirectory / "command-registry.json",
              "{\"commands\":[" + commandJson + "],\"queries\":[" + queryJson +
                  "]}");

    const wire::MessagePolicy &policy = messagePolicy(command);
    writeFile(outputDirectory / "project.rename.tool.json",
              "{\"name\":" + jsonQuoted(policy.stable_id()) +
                  ",\"description\":" + jsonQuoted(policy.summary()) +
                  ",\"inputSchema\":" + jsonSchema(command) +
                  ",\"x-kearne\":{\"schemaVersion\":" +
                  std::to_string(policy.schema_version()) +
                  ",\"permission\":" + jsonQuoted(policy.permission()) + "}}");
    std::cout << "{\"commands\":1,\"queries\":1,\"tools\":1}\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
