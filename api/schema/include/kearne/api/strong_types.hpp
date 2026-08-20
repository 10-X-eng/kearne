#pragma once

#include <kearne/api/v1/engineering.pb.h>
#include <kearne/base/value.hpp>

#include <algorithm>
#include <string>

namespace kearne::api {

template <typename Id> [[nodiscard]] Result<Id> readId(const v1::UuidV7 &wire) {
  typename Id::Bytes bytes{};
  if (wire.value().size() != bytes.size())
    return std::unexpected(
        diagnostic("api.id.invalid-size", "wire identifier has invalid size"));
  std::ranges::copy(wire.value(), bytes.begin());
  return Id::fromBytes(std::move(bytes));
}

template <typename Id> void writeId(const Id &value, v1::UuidV7 *wire) {
  wire->set_value(
      std::string{reinterpret_cast<const char *>(value.bytes().data()),
                  value.bytes().size()});
}

template <typename Digest>
[[nodiscard]] Result<Digest> readDigest(const v1::Digest &wire) {
  typename Digest::Bytes bytes{};
  if (wire.value().size() != bytes.size())
    return std::unexpected(
        diagnostic("api.digest.invalid-size", "wire digest has invalid size"));
  std::ranges::copy(wire.value(), bytes.begin());
  const auto algorithm = wire.algorithm();
  return Digest::fromBytes(std::string{algorithm.data(), algorithm.size()},
                           std::move(bytes));
}

template <typename Digest>
void writeDigest(const Digest &value, v1::Digest *wire) {
  wire->set_algorithm(value.algorithm());
  wire->set_value(
      std::string{reinterpret_cast<const char *>(value.bytes().data()),
                  value.bytes().size()});
}

[[nodiscard]] Result<Origin> readOrigin(v1::Origin value);
[[nodiscard]] v1::Origin writeOrigin(Origin value);
void writeDiagnostic(const Diagnostic &value, v1::Diagnostic *wire);

} // namespace kearne::api
