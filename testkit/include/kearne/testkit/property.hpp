#pragma once

#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace kearne::testkit {

class Random final {
public:
  explicit Random(std::uint64_t seed) : state_(seed) {}

  [[nodiscard]] std::uint64_t next() {
    state_ += 0x9e3779b97f4a7c15ULL;
    std::uint64_t value = state_;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
  }

  [[nodiscard]] double between(double minimum, double maximum) {
    constexpr double divisor = static_cast<double>(std::uint64_t{1} << 53U);
    const double unit = static_cast<double>(next() >> 11U) / divisor;
    return minimum + (maximum - minimum) * unit;
  }

private:
  std::uint64_t state_;
};

struct PropertyProfile {
  struct ReplayCase {
    std::uint64_t index = 0;
    std::uint64_t seed = 0;
    bool operator==(const ReplayCase &) const = default;
  };

  std::uint64_t seed = 0x4b4541524e45ULL;
  std::uint64_t iterations = 10'000;
  std::uint64_t shardIndex = 0;
  std::uint64_t shardCount = 1;
  std::optional<ReplayCase> replay;
};

inline std::optional<std::uint64_t> environmentInteger(const char *name) {
  const char *text = std::getenv(name);
  if (!text)
    return std::nullopt;
  std::uint64_t value = 0;
  const std::string_view input{text};
  const auto parsed =
      std::from_chars(input.data(), input.data() + input.size(), value);
  if (parsed.ec != std::errc{} || parsed.ptr != input.data() + input.size())
    throw std::runtime_error(std::string{name} + " is not an unsigned integer");
  return value;
}

inline std::uint64_t environmentInteger(const char *name,
                                        std::uint64_t fallback) {
  const auto value = environmentInteger(name);
  return value.value_or(fallback);
}

inline PropertyProfile propertyProfile() {
  PropertyProfile profile;
  const std::string_view name = std::getenv("KEARNE_TEST_PROFILE")
                                    ? std::getenv("KEARNE_TEST_PROFILE")
                                    : "change";
  if (name != "change" && name != "nightly" && name != "release")
    throw std::runtime_error("KEARNE_TEST_PROFILE is unsupported");
  profile.iterations = name == "release"   ? 1'000'000
                       : name == "nightly" ? 250'000
                                           : 10'000;
  profile.seed = environmentInteger("KEARNE_TEST_SEED", profile.seed);
  profile.iterations =
      environmentInteger("KEARNE_TEST_ITERATIONS", profile.iterations);
  profile.shardIndex = environmentInteger("KEARNE_TEST_SHARD_INDEX", 0);
  profile.shardCount = environmentInteger("KEARNE_TEST_SHARD_COUNT", 1);
  const auto replaySeed = environmentInteger("KEARNE_TEST_CASE_SEED");
  const auto replayIndex = environmentInteger("KEARNE_TEST_CASE_INDEX");
  if (replaySeed.has_value() != replayIndex.has_value())
    throw std::runtime_error("KEARNE_TEST_CASE_SEED and KEARNE_TEST_CASE_INDEX "
                             "must be set together");
  if (replaySeed)
    profile.replay = PropertyProfile::ReplayCase{*replayIndex, *replaySeed};
  if (profile.iterations == 0 || profile.shardCount == 0 ||
      profile.shardIndex >= profile.shardCount)
    throw std::runtime_error("invalid Kearne property-test profile");
  return profile;
}

template <typename Property>
void checkProperty(std::string_view name, const PropertyProfile &profile,
                   Property property) {
  const auto run = [&](std::uint64_t caseSeed, std::uint64_t index) {
    try {
      Random caseRandom{caseSeed};
      property(caseRandom, index);
    } catch (const std::exception &error) {
      throw std::runtime_error(std::string{name} + " failed at iteration " +
                               std::to_string(index) + ", seed " +
                               std::to_string(caseSeed) + ": " + error.what());
    }
  };
  if (profile.replay) {
    run(profile.replay->seed, profile.replay->index);
    return;
  }
  Random random{profile.seed};
  for (std::uint64_t index = 0; index < profile.iterations; ++index) {
    const std::uint64_t caseSeed = random.next();
    if (index % profile.shardCount != profile.shardIndex)
      continue;
    run(caseSeed, index);
  }
}

} // namespace kearne::testkit
