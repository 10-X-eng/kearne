#include <kearne/document/canonical.hpp>

#include <blake3.h>

#include <bit>
#include <cmath>
#include <exception>
#include <limits>
#include <string>

namespace kearne::document {
namespace {

bool continuation(const unsigned char value) {
  return (value & 0xc0U) == 0x80U;
}

} // namespace

bool isValidUtf8(std::string_view text) {
  const auto *bytes = reinterpret_cast<const unsigned char *>(text.data());
  std::size_t index = 0;
  while (index < text.size()) {
    const unsigned char first = bytes[index++];
    if (first <= 0x7fU)
      continue;
    if (first >= 0xc2U && first <= 0xdfU) {
      if (index >= text.size() || !continuation(bytes[index++]))
        return false;
      continue;
    }
    if (first >= 0xe0U && first <= 0xefU) {
      if (index + 1 >= text.size())
        return false;
      const unsigned char second = bytes[index++];
      const unsigned char third = bytes[index++];
      if (!continuation(second) || !continuation(third) ||
          (first == 0xe0U && second < 0xa0U) ||
          (first == 0xedU && second >= 0xa0U))
        return false;
      continue;
    }
    if (first >= 0xf0U && first <= 0xf4U) {
      if (index + 2 >= text.size())
        return false;
      const unsigned char second = bytes[index++];
      const unsigned char third = bytes[index++];
      const unsigned char fourth = bytes[index++];
      if (!continuation(second) || !continuation(third) ||
          !continuation(fourth) || (first == 0xf0U && second < 0x90U) ||
          (first == 0xf4U && second >= 0x90U))
        return false;
      continue;
    }
    return false;
  }
  return true;
}

void CanonicalWriter::header(std::string_view type, std::uint64_t version) {
  constexpr std::array<std::uint8_t, 4> magic{'K', 'C', 'E', '1'};
  bytes(magic);
  const auto ignored = text(type);
  if (!ignored)
    std::terminate();
  unsignedInteger(version);
}

void CanonicalWriter::boolean(bool value) {
  value_.push_back(static_cast<std::uint8_t>(value));
}

void CanonicalWriter::unsignedInteger(std::uint64_t value) {
  do {
    std::uint8_t byte = static_cast<std::uint8_t>(value & 0x7fU);
    value >>= 7U;
    if (value != 0)
      byte |= 0x80U;
    value_.push_back(byte);
  } while (value != 0);
}

Result<void> CanonicalWriter::binary64(double value) {
  if (!std::isfinite(value))
    return std::unexpected(diagnostic("document.number.non-finite",
                                      "canonical number is not finite"));
  const std::uint64_t bits =
      std::bit_cast<std::uint64_t>(value == 0.0 ? 0.0 : value);
  for (unsigned shift = 64; shift != 0; shift -= 8)
    value_.push_back(static_cast<std::uint8_t>(bits >> (shift - 8)));
  return {};
}

void CanonicalWriter::bytes(std::span<const std::uint8_t> value) {
  unsignedInteger(value.size());
  value_.insert(value_.end(), value.begin(), value.end());
}

Result<void> CanonicalWriter::text(std::string_view value) {
  if (!isValidUtf8(value))
    return std::unexpected(
        diagnostic("document.text.invalid-utf8", "text is not valid UTF-8"));
  bytes({reinterpret_cast<const std::uint8_t *>(value.data()), value.size()});
  return {};
}

Result<void> CanonicalReader::header(std::string_view type,
                                     std::uint64_t version) {
  constexpr std::array<std::uint8_t, 4> magic{'K', 'C', 'E', '1'};
  auto encodedMagic = bytes(magic.size());
  if (!encodedMagic)
    return std::unexpected(encodedMagic.error());
  if (!std::equal(encodedMagic->begin(), encodedMagic->end(), magic.begin(),
                  magic.end()))
    return std::unexpected(
        diagnostic("document.canonical.header",
                   "canonical input has the wrong encoding header"));

  auto encodedType = text(type.size());
  if (!encodedType)
    return std::unexpected(encodedType.error());
  if (*encodedType != type)
    return std::unexpected(diagnostic(
        "document.canonical.type", "canonical input has the wrong value type"));

  auto encodedVersion = unsignedInteger();
  if (!encodedVersion)
    return std::unexpected(encodedVersion.error());
  if (*encodedVersion != version)
    return std::unexpected(
        diagnostic("document.canonical.version",
                   "canonical input has an unsupported version"));
  return {};
}

Result<bool> CanonicalReader::boolean() {
  auto encoded = take(1);
  if (!encoded)
    return std::unexpected(encoded.error());
  if ((*encoded)[0] > 1)
    return std::unexpected(
        diagnostic("document.canonical.bool",
                   "canonical boolean must be encoded as zero or one"));
  return (*encoded)[0] == 1;
}

Result<std::uint64_t> CanonicalReader::unsignedInteger() {
  std::uint64_t value = 0;
  for (std::uint32_t index = 0; index < 10; ++index) {
    auto encoded = take(1);
    if (!encoded)
      return std::unexpected(encoded.error());
    const auto byte = (*encoded)[0];
    const auto payload = static_cast<std::uint8_t>(byte & 0x7fU);
    if (index == 9 && (payload > 1 || (byte & 0x80U) != 0))
      return std::unexpected(
          diagnostic("document.canonical.integer-overflow",
                     "canonical integer exceeds unsigned 64-bit range"));
    value |= static_cast<std::uint64_t>(payload) << (index * 7U);
    if ((byte & 0x80U) == 0) {
      if (index > 0 && payload == 0)
        return std::unexpected(
            diagnostic("document.canonical.noncanonical-integer",
                       "canonical integer has an overlong encoding"));
      return value;
    }
  }
  return std::unexpected(
      diagnostic("document.canonical.integer-overflow",
                 "canonical integer exceeds unsigned 64-bit range"));
}

Result<double> CanonicalReader::binary64() {
  auto encoded = take(sizeof(std::uint64_t));
  if (!encoded)
    return std::unexpected(encoded.error());
  std::uint64_t bits = 0;
  for (const auto byte : *encoded)
    bits = (bits << 8U) | byte;
  constexpr std::uint64_t exponent = 0x7ff0000000000000ULL;
  constexpr std::uint64_t negativeZero = 0x8000000000000000ULL;
  if ((bits & exponent) == exponent)
    return std::unexpected(
        diagnostic("document.canonical.non-finite",
                   "canonical binary64 value must be finite"));
  if (bits == negativeZero)
    return std::unexpected(
        diagnostic("document.canonical.negative-zero",
                   "canonical binary64 zero must be positive zero"));
  return std::bit_cast<double>(bits);
}

Result<std::span<const std::uint8_t>>
CanonicalReader::bytes(std::size_t maximumSize) {
  auto size = unsignedInteger();
  if (!size)
    return std::unexpected(size.error());
  if (*size > maximumSize || *size > std::numeric_limits<std::size_t>::max())
    return std::unexpected(
        diagnostic("document.canonical.length-limit",
                   "canonical byte string exceeds its bounded size"));
  return take(static_cast<std::size_t>(*size));
}

Result<std::string_view> CanonicalReader::text(std::size_t maximumBytes) {
  auto encoded = bytes(maximumBytes);
  if (!encoded)
    return std::unexpected(encoded.error());
  if (encoded->empty())
    return std::string_view{};
  const std::string_view value(reinterpret_cast<const char *>(encoded->data()),
                               encoded->size());
  if (!isValidUtf8(value))
    return std::unexpected(diagnostic("document.canonical.invalid-utf8",
                                      "canonical text is not valid UTF-8"));
  return value;
}

Result<void> CanonicalReader::end() const {
  if (offset_ != bytes_.size())
    return std::unexpected(
        diagnostic("document.canonical.trailing-bytes",
                   "canonical input contains trailing bytes"));
  return {};
}

Result<std::span<const std::uint8_t>> CanonicalReader::take(std::size_t size) {
  if (size > remaining())
    return std::unexpected(
        diagnostic("document.canonical.truncated",
                   "canonical input ended before the value"));
  const auto value = bytes_.subspan(offset_, size);
  offset_ += size;
  return value;
}

template <typename Digest>
Result<Digest> hashCanonical(std::string_view context,
                             std::span<const std::uint8_t> bytes) {
  if (context.empty() || context.size() > 128 ||
      context.find('\0') != std::string_view::npos || !isValidUtf8(context))
    return std::unexpected(diagnostic("document.digest.invalid-context",
                                      "digest context is invalid"));
  const std::string ownedContext{context};
  blake3_hasher hasher;
  blake3_hasher_init_derive_key(&hasher, ownedContext.c_str());
  blake3_hasher_update(&hasher, bytes.data(), bytes.size());
  typename Digest::Bytes output{};
  blake3_hasher_finalize(&hasher, output.data(), output.size());
  return Digest::fromBytes("blake3", std::move(output));
}

template Result<ContentDigest>
    hashCanonical<ContentDigest>(std::string_view,
                                 std::span<const std::uint8_t>);
template Result<ArtifactDigest>
    hashCanonical<ArtifactDigest>(std::string_view,
                                  std::span<const std::uint8_t>);
template Result<RevisionId>
    hashCanonical<RevisionId>(std::string_view, std::span<const std::uint8_t>);

} // namespace kearne::document
