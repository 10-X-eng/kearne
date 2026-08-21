#include <kearne/adapters/sketch_wire.hpp>

#include <cstddef>
#include <iostream>
#include <iterator>
#include <span>
#include <string>

int main() {
  std::cin.sync_with_stdio(false);
  const std::string input{std::istreambuf_iterator<char>{std::cin},
                          std::istreambuf_iterator<char>{}};
  const auto bytes = std::as_bytes(std::span{input.data(), input.size()});
  auto definition = kearne::adapters::parseSketchDefinition(bytes);
  if (!definition) {
    std::cerr << definition.error().code << ": "
              << definition.error().summary << '\n';
    return 2;
  }
  auto output = kearne::adapters::serializeSketchDefinition(*definition);
  if (!output) {
    std::cerr << output.error().code << ": " << output.error().summary << '\n';
    return 3;
  }
  std::cout.write(output->data(), static_cast<std::streamsize>(output->size()));
  return std::cout.good() ? 0 : 4;
}
