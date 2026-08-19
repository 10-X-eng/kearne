#include "engine.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data,
                                      std::size_t size) {
  kearne::schema_prototype::parseAndValidate(
      std::span(reinterpret_cast<const std::byte *>(data), size));
  return 0;
}
