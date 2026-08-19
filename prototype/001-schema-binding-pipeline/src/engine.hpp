#pragma once

#include "api.pb.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace kearne::schema_prototype {

inline constexpr std::uint32_t kMaxFrameBytes = 64 * 1024;

struct ValidationError {
  std::string code;
  std::string path;
};

std::optional<ValidationError>
validateWire(const google::protobuf::Message &message);
void parseAndValidate(std::span<const std::byte> bytes);

class Engine {
public:
  kearne::schema::v1::RpcResponse
  handle(const kearne::schema::v1::RpcRequest &request);

private:
  std::string projectId_ = "project-01";
  std::string displayName_ = "Untitled";
  std::string revisionId_ = "revision-0000";
  std::uint64_t revisionSequence_ = 0;
};

kearne::schema::v1::RpcRequest makeRenameRequest(std::string displayName);
kearne::schema::v1::RpcRequest makeMetadataQuery(std::string revisionId);

} // namespace kearne::schema_prototype
