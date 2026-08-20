#include <kearne/base/value.hpp>
#include <kearne/testkit/property.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {

struct ProjectTag;
struct RevisionTag;

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

template <std::size_t Size>
std::array<std::uint8_t, Size> randomBytes(kearne::testkit::Random &random) {
  std::array<std::uint8_t, Size> bytes{};
  for (std::uint8_t &byte : bytes)
    byte = static_cast<std::uint8_t>(random.next());
  return bytes;
}

void verifyIdentifiers(const kearne::testkit::PropertyProfile &profile) {
  static_assert(!std::same_as<kearne::ModelInputId, kearne::ModelOutputId>);
  static_assert(!std::same_as<kearne::ModelBindingId, kearne::ModelInputId>);
  static_assert(!std::same_as<kearne::EnvironmentDigest,
                              kearne::CapabilityProfileDigest>);
  kearne::testkit::checkProperty(
      "typed identifier and digest round trip", profile,
      [](kearne::testkit::Random &random, std::uint64_t index) {
        const std::uint64_t timestamp =
            random.next() % (std::uint64_t{1} << 48U);
        const auto project = kearne::TypedId<ProjectTag>::create(
            timestamp, randomBytes<10>(random));
        require(project.has_value(), "valid UUIDv7 inputs were rejected");
        const auto parsedProject =
            kearne::TypedId<ProjectTag>::parse(project->toString());
        require(parsedProject && *parsedProject == *project,
                "typed identifier did not round trip");
        if (timestamp + 1 < (std::uint64_t{1} << 48U)) {
          const auto later = kearne::TypedId<ProjectTag>::create(
              timestamp + 1, randomBytes<10>(random));
          require(later && *project < *later,
                  "UUIDv7 byte order does not follow its timestamp");
        }
        require(!kearne::TypedId<RevisionTag>::parse(
                    index % 2 == 0 ? project->toString() + "0" : "not-hex"),
                "invalid typed identifier was accepted");

        std::string wrongVersion = project->toString();
        wrongVersion[index % 2 == 0 ? 14 : 19] = index % 2 == 0 ? '4' : '0';
        require(!kearne::TypedId<ProjectTag>::parse(wrongVersion),
                "non-UUIDv7 identifier was accepted");

        const std::string algorithm = index % 2 == 0 ? "sha256" : "blake3";
        const auto digest = kearne::ContentDigest::fromBytes(
            algorithm, randomBytes<32>(random));
        require(digest.has_value(), "valid digest was rejected");
        const auto parsedDigest =
            kearne::ContentDigest::parse(digest->toString());
        require(parsedDigest && *parsedDigest == *digest,
                "digest did not round trip");
      });
}

void verifyQuantities(const kearne::testkit::PropertyProfile &profile) {
  kearne::testkit::checkProperty(
      "finite quantity unit equivalence", profile,
      [](kearne::testkit::Random &random, std::uint64_t index) {
        if (index % 8 == 0) {
          const std::array invalid{
              std::numeric_limits<double>::infinity(),
              -std::numeric_limits<double>::infinity(),
              std::numeric_limits<double>::quiet_NaN(),
          };
          const auto quantity = kearne::Quantity<kearne::Length>::fromSi(
              invalid[random.next() % invalid.size()]);
          require(!quantity &&
                      quantity.error().code == "base.quantity.non-finite",
                  "non-finite quantity was accepted");
          return;
        }
        const double value = random.between(-1.0e9, 1.0e9);
        const double scale = std::pow(10.0, random.between(-9.0, 9.0));
        const auto quantity =
            kearne::Quantity<kearne::Length>::fromUnit(value, scale);
        require(quantity.has_value(), "finite unit value was rejected");
        const auto recovered = quantity->in(scale);
        const double tolerance = 1e-12 * std::max(1.0, std::abs(value));
        require(recovered && std::abs(*recovered - value) <= tolerance,
                "unit-equivalent quantity did not round trip");
      });
}

void verifyProgress(const kearne::testkit::PropertyProfile &profile) {
  kearne::testkit::checkProperty(
      "progress range", profile,
      [](kearne::testkit::Random &random, std::uint64_t index) {
        const double completed = random.between(-0.5, 1.5);
        const auto progress =
            kearne::ProgressEvent::create(index, completed, "generated");
        require(progress.has_value() == (completed >= 0.0 && completed <= 1.0),
                "progress range validation disagrees with the contract");
      });
}

} // namespace

int main() {
  try {
    const auto profile = kearne::testkit::propertyProfile();
    verifyIdentifiers(profile);
    verifyQuantities(profile);
    verifyProgress(profile);
    std::cout << "verified " << profile.iterations
              << " generated cases per base property\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
