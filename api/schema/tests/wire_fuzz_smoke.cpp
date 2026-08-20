#include <kearne/testkit/property.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data,
                                      std::size_t size);
void configureWireFuzzLogging();

int main() {
  configureWireFuzzLogging();
  const auto profile = kearne::testkit::propertyProfile();
  kearne::testkit::checkProperty(
      "wire parser", profile,
      [](kearne::testkit::Random &random, std::uint64_t index) {
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(index % 4097));
        for (std::uint8_t &byte : bytes)
          byte = static_cast<std::uint8_t>(random.next());
        LLVMFuzzerTestOneInput(bytes.data(), bytes.size());
      });
  return 0;
}
