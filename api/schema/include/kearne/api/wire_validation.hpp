#pragma once

#include <kearne/base/value.hpp>

#include <google/protobuf/message.h>

#include <span>

namespace kearne::api {

[[nodiscard]] Result<void>
validateWire(const google::protobuf::Message &message);

[[nodiscard]] std::span<const google::protobuf::Descriptor *const>
registeredWireTypes();

} // namespace kearne::api
