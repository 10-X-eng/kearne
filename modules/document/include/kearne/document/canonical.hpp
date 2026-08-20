#pragma once

#include <kearne/base/value.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace kearne::document {

using Bytes = std::vector<std::uint8_t>;

[[nodiscard]] bool isValidUtf8(std::string_view text);

class CanonicalWriter final {
public:
  void header(std::string_view type, std::uint64_t version);
  void boolean(bool value);
  void unsignedInteger(std::uint64_t value);
  [[nodiscard]] Result<void> binary64(double value);
  void bytes(std::span<const std::uint8_t> value);
  [[nodiscard]] Result<void> text(std::string_view value);

  template <typename Tag> void identifier(const TypedId<Tag> &value) {
    bytes(value.bytes());
  }

  template <typename Tag> void digest(const TypedDigest<Tag> &value) {
    const auto ignored = text(value.algorithm());
    if (!ignored)
      std::terminate();
    bytes(value.bytes());
  }

  [[nodiscard]] const Bytes &value() const { return value_; }
  [[nodiscard]] Bytes take() && { return std::move(value_); }

private:
  Bytes value_;
};

class CanonicalReader final {
public:
  explicit CanonicalReader(std::span<const std::uint8_t> bytes) noexcept
      : bytes_(bytes) {}

  [[nodiscard]] Result<void> header(std::string_view type,
                                    std::uint64_t version);
  [[nodiscard]] Result<bool> boolean();
  [[nodiscard]] Result<std::uint64_t> unsignedInteger();
  [[nodiscard]] Result<double> binary64();
  [[nodiscard]] Result<std::span<const std::uint8_t>>
  bytes(std::size_t maximumSize);
  [[nodiscard]] Result<std::string_view> text(std::size_t maximumBytes);

  template <typename Tag> [[nodiscard]] Result<TypedId<Tag>> identifier() {
    constexpr std::size_t size =
        std::tuple_size_v<typename TypedId<Tag>::Bytes>;
    auto encoded = bytes(size);
    if (!encoded)
      return std::unexpected(encoded.error());
    if (encoded->size() != size)
      return std::unexpected(
          diagnostic("document.canonical.identifier-size",
                     "canonical identifier must contain exactly 16 bytes"));
    typename TypedId<Tag>::Bytes value{};
    std::copy(encoded->begin(), encoded->end(), value.begin());
    return TypedId<Tag>::fromBytes(value);
  }

  template <typename Tag> [[nodiscard]] Result<TypedDigest<Tag>> digest() {
    auto algorithm = text(32);
    if (!algorithm)
      return std::unexpected(algorithm.error());
    constexpr std::size_t size =
        std::tuple_size_v<typename TypedDigest<Tag>::Bytes>;
    auto encoded = bytes(size);
    if (!encoded)
      return std::unexpected(encoded.error());
    if (encoded->size() != size)
      return std::unexpected(
          diagnostic("document.canonical.digest-size",
                     "canonical digest must contain exactly 32 bytes"));
    typename TypedDigest<Tag>::Bytes value{};
    std::copy(encoded->begin(), encoded->end(), value.begin());
    return TypedDigest<Tag>::fromBytes(std::string(*algorithm), value);
  }

  [[nodiscard]] Result<void> end() const;
  [[nodiscard]] std::size_t remaining() const noexcept {
    return bytes_.size() - offset_;
  }

private:
  [[nodiscard]] Result<std::span<const std::uint8_t>> take(std::size_t size);

  std::span<const std::uint8_t> bytes_;
  std::size_t offset_ = 0;
};

template <typename Digest>
[[nodiscard]] Result<Digest> hashCanonical(std::string_view context,
                                           std::span<const std::uint8_t> bytes);

extern template Result<ContentDigest>
    hashCanonical<ContentDigest>(std::string_view,
                                 std::span<const std::uint8_t>);
extern template Result<ArtifactDigest>
    hashCanonical<ArtifactDigest>(std::string_view,
                                  std::span<const std::uint8_t>);
extern template Result<RevisionId>
    hashCanonical<RevisionId>(std::string_view, std::span<const std::uint8_t>);

} // namespace kearne::document
