#include "engine.hpp"

#include "evolution.pb.h"

#include <google/protobuf/stubs/common.h>
#include <google/protobuf/util/json_util.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

namespace wire = kearne::schema::v1;
namespace evolution = kearne::schema::evolution;
using kearne::schema_prototype::Engine;

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

wire::RpcResponse transportError(std::string code) {
  wire::RpcResponse response;
  response.mutable_transport_diagnostic()->set_code(std::move(code));
  return response;
}

bool readExact(std::istream &stream, char *buffer, std::size_t size) {
  stream.read(buffer, static_cast<std::streamsize>(size));
  return stream.gcount() == static_cast<std::streamsize>(size);
}

enum class LineResult { line, end, too_large };

LineResult readBoundedLine(std::istream &stream, std::string &line) {
  line.clear();
  char character = 0;
  while (stream.get(character)) {
    if (character == '\n')
      return LineResult::line;
    if (line.size() == kearne::schema_prototype::kMaxFrameBytes) {
      while (stream.get(character) && character != '\n') {
      }
      return LineResult::too_large;
    }
    line.push_back(character);
  }
  return line.empty() ? LineResult::end : LineResult::line;
}

void writeFrame(const wire::RpcResponse &response) {
  const std::string bytes = response.SerializeAsString();
  const std::uint32_t size = static_cast<std::uint32_t>(bytes.size());
  const std::array<char, 4> prefix{static_cast<char>((size >> 24) & 0xff),
                                   static_cast<char>((size >> 16) & 0xff),
                                   static_cast<char>((size >> 8) & 0xff),
                                   static_cast<char>(size & 0xff)};
  std::cout.write(prefix.data(), prefix.size());
  std::cout.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  std::cout.flush();
}

int runBinaryServer() {
  Engine engine;
  for (;;) {
    std::array<unsigned char, 4> prefix{};
    if (!readExact(std::cin, reinterpret_cast<char *>(prefix.data()),
                   prefix.size()))
      return std::cin.eof() ? 0 : 3;
    const std::uint32_t size =
        (std::uint32_t(prefix[0]) << 24) | (std::uint32_t(prefix[1]) << 16) |
        (std::uint32_t(prefix[2]) << 8) | std::uint32_t(prefix[3]);
    if (size > kearne::schema_prototype::kMaxFrameBytes) {
      std::cerr << "frame exceeds negotiated inline limit\n";
      return 4;
    }
    std::string bytes(size, '\0');
    if (!readExact(std::cin, bytes.data(), bytes.size()))
      return 3;
    wire::RpcRequest request;
    if (!request.ParseFromString(bytes)) {
      writeFrame(transportError("wire.invalid_binary"));
      continue;
    }
    writeFrame(engine.handle(request));
  }
}

int runJsonServer() {
  Engine engine;
  std::string line;
  google::protobuf::util::JsonPrintOptions printOptions;
  printOptions.preserve_proto_field_names = true;
  for (;;) {
    const LineResult lineResult = readBoundedLine(std::cin, line);
    if (lineResult == LineResult::end)
      return 0;
    wire::RpcRequest request;
    wire::RpcResponse response;
    if (lineResult == LineResult::too_large) {
      response = transportError("wire.frame_too_large");
    } else if (const auto status =
                   google::protobuf::util::JsonStringToMessage(line, &request);
               !status.ok()) {
      response = transportError("wire.invalid_json");
    } else {
      response = engine.handle(request);
    }
    std::string json;
    require(google::protobuf::util::MessageToJsonString(response, &json,
                                                        printOptions)
                .ok(),
            "cannot encode JSON response");
    std::cout << json << '\n';
  }
}

int runSelfTest() {
  Engine engine;
  const wire::RpcResponse command =
      engine.handle(kearne::schema_prototype::makeRenameRequest("Mounting Plate"));
  require(command.has_command() && command.command().committed(),
          "valid command rejected");
  require(command.command().revision_id() == "revision-0001",
          "unexpected revision");

  const wire::RpcResponse query =
      engine.handle(kearne::schema_prototype::makeMetadataQuery(
          std::string(command.command().revision_id())));
  require(query.has_query(), "query response missing");
  require(query.query().display_name() == "Mounting Plate",
          "semantic result mismatch");
  require(query.query().observed_revision_id() == "revision-0001",
          "query did not report revision");

  Engine invalidEngine;
  const wire::RpcResponse invalid =
      invalidEngine.handle(kearne::schema_prototype::makeRenameRequest(""));
  require(invalid.command().diagnostic().code() == "validation.min_length",
          "descriptor bound was not enforced");

  wire::EntityRecord unknownEntity;
  unknownEntity.set_kind("vendor.future_entity");
  unknownEntity.set_schema_version(91);
  unknownEntity.set_opaque_payload(std::string("\x00\xffopaque\x7f", 9));
  const std::string unknownBytes = unknownEntity.SerializeAsString();
  wire::EntityRecord recoveredEntity;
  require(recoveredEntity.ParseFromString(unknownBytes),
          "unknown entity did not parse");
  require(recoveredEntity.SerializeAsString() == unknownBytes,
          "unknown entity payload changed");

  wire::EventEnvelope event;
  event.set_sequence(1);
  event.mutable_revision_committed()->set_revision_id("revision-0001");
  event.mutable_revision_committed()->set_request_id("request-0001");
  require(!kearne::schema_prototype::validateWire(event),
          "event contract rejected");

  wire::WorkerJob job;
  job.set_worker_instance_id("worker-01");
  job.set_job_id("job-01");
  job.mutable_validate_command()->set_command(
      kearne::schema_prototype::makeRenameRequest("Mounting Plate")
          .SerializeAsString());
  require(!kearne::schema_prototype::validateWire(job), "worker contract rejected");

  evolution::V2Record future;
  future.set_known("known");
  future.set_future("preserved");
  evolution::V1Record oldReader;
  require(oldReader.ParseFromString(future.SerializeAsString()),
          "old reader failed");
  evolution::V2Record binaryRoundTrip;
  require(binaryRoundTrip.ParseFromString(oldReader.SerializeAsString()),
          "binary evolution round trip failed");
  require(binaryRoundTrip.future() == "preserved",
          "binary unknown field was lost");

  std::string oldJson;
  require(google::protobuf::util::MessageToJsonString(oldReader, &oldJson).ok(),
          "old JSON encoding failed");
  evolution::V2Record jsonRoundTrip;
  require(
      google::protobuf::util::JsonStringToMessage(oldJson, &jsonRoundTrip).ok(),
      "new JSON decoding failed");
  require(jsonRoundTrip.future().empty(),
          "JSON unexpectedly retained an unknown field");

  std::cout
      << "{\"adapter\":\"cpp-in-process\",\"display_name\":\"Mounting Plate\","
         "\"revision_id\":\"revision-0001\",\"unknown_binary_preserved\":true,"
         "\"unknown_json_preserved\":false}\n";
  return 0;
}

} // namespace

int main(int argc, char **argv) {
  try {
    if (argc == 2 && std::string(argv[1]) == "--self-test")
      return runSelfTest();
    if (argc == 2 && std::string(argv[1]) == "--binary-server")
      return runBinaryServer();
    if (argc == 2 && std::string(argv[1]) == "--json-server")
      return runJsonServer();
    if (argc == 2 && std::string(argv[1]) == "--version") {
      std::cout << GOOGLE_PROTOBUF_VERSION << '\n';
      return 0;
    }
    std::cerr << "usage: kearne-schema-probe "
                 "--self-test|--binary-server|--json-server|--version\n";
    return 2;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
