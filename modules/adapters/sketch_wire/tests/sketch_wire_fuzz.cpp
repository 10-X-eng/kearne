#include <kearne/adapters/sketch_wire.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data,
                                      std::size_t size) {
  if (size == 0 || size > kearne::adapters::maximumSketchDefinitionWireBytes)
    return 0;
  const auto bytes = std::as_bytes(std::span{data, size});
  static_cast<void>(kearne::adapters::parseSketchDefinition(bytes));
  return 0;
}
