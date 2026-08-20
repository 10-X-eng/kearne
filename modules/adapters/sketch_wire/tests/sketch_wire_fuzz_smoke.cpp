#include <kearne/testkit/property.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data,
                                      std::size_t size);

int main() {
  const auto profile = kearne::testkit::propertyProfile();
  kearne::testkit::checkProperty(
      "sketch wire parser", profile,
      [](kearne::testkit::Random &random, std::uint64_t index) {
        std::vector<std::uint8_t> bytes(
            static_cast<std::size_t>(index % 16'385U));
        for (std::uint8_t &byte : bytes)
          byte = static_cast<std::uint8_t>(random.next());
        LLVMFuzzerTestOneInput(bytes.data(), bytes.size());
      });
  return 0;
}
