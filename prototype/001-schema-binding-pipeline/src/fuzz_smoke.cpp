#include "engine.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data,
                                      std::size_t size);

int main(int argc, char **argv) {
  std::uint64_t seed = 0;
  std::size_t cases = 0;
  for (int index = 1; index + 1 < argc; index += 2) {
    const std::string option = argv[index];
    if (option == "--seed")
      seed = std::stoull(argv[index + 1]);
    else if (option == "--cases")
      cases = std::stoull(argv[index + 1]);
    else
      return 2;
  }
  if (seed == 0 || cases == 0)
    return 2;

  std::mt19937_64 random(seed);
  std::uniform_int_distribution<std::size_t> length(0, 80 * 1024);
  std::uniform_int_distribution<unsigned int> byte(0, 255);
  const std::string valid =
      kearne::schema_prototype::makeRenameRequest("Mounting Plate")
          .SerializeAsString();
  for (std::size_t caseIndex = 0; caseIndex < cases; ++caseIndex) {
    std::vector<std::uint8_t> input;
    if (caseIndex % 10 == 0) {
      input.assign(valid.begin(), valid.end());
      if (!input.empty())
        input[random() % input.size()] ^=
            static_cast<std::uint8_t>(1U << (random() % 8));
    } else {
      input.resize(length(random));
      for (std::uint8_t &value : input)
        value = static_cast<std::uint8_t>(byte(random));
    }
    LLVMFuzzerTestOneInput(input.data(), input.size());
  }
  std::cout << "{\"cases\":" << cases << ",\"seed\":" << seed << "}\n";
  return 0;
}
