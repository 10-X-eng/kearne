#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <ranges>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kearne {

enum class Severity : std::uint8_t { Information, Warning, Error, Fatal };
enum class Origin : std::uint8_t {
  Human = 1,
  Python = 2,
  AI = 3,
  Plugin = 4,
  Import = 5,
  Replay = 6,
  System = 7,
};

struct Diagnostic {
  std::string code;
  Severity severity = Severity::Error;
  std::string summary;
  std::vector<std::string> parameters;
  std::string detail;

  bool operator==(const Diagnostic &) const = default;
};

template <typename Value> using Result = std::expected<Value, Diagnostic>;

inline Diagnostic diagnostic(std::string code, std::string summary,
                             Severity severity = Severity::Error) {
  return {std::move(code), severity, std::move(summary), {}, {}};
}

namespace detail {

inline constexpr char hexadecimal[] = "0123456789abcdef";

template <std::size_t Size>
std::string encodeHex(const std::array<std::uint8_t, Size> &bytes) {
  std::string result(Size * 2, '0');
  for (std::size_t index = 0; index < Size; ++index) {
    result[index * 2] = hexadecimal[bytes[index] >> 4U];
    result[index * 2 + 1] = hexadecimal[bytes[index] & 0x0fU];
  }
  return result;
}

inline int decodeNibble(char value) {
  if (value >= '0' && value <= '9')
    return value - '0';
  if (value >= 'a' && value <= 'f')
    return value - 'a' + 10;
  if (value >= 'A' && value <= 'F')
    return value - 'A' + 10;
  return -1;
}

template <std::size_t Size>
Result<std::array<std::uint8_t, Size>> decodeHex(std::string_view text,
                                                 std::string_view code) {
  if (text.size() != Size * 2)
    return std::unexpected(
        diagnostic(std::string{code}, "hexadecimal value has the wrong size"));
  std::array<std::uint8_t, Size> result{};
  for (std::size_t index = 0; index < Size; ++index) {
    const int high = decodeNibble(text[index * 2]);
    const int low = decodeNibble(text[index * 2 + 1]);
    if (high < 0 || low < 0)
      return std::unexpected(diagnostic(
          std::string{code}, "hexadecimal value contains an invalid digit"));
    result[index] = static_cast<std::uint8_t>((high << 4) | low);
  }
  return result;
}

} // namespace detail

template <typename Tag> class TypedId final {
public:
  using Bytes = std::array<std::uint8_t, 16>;
  using RandomTail = std::array<std::uint8_t, 10>;

  [[nodiscard]] static Result<TypedId> create(std::uint64_t unixMilliseconds,
                                              const RandomTail &random) {
    if (unixMilliseconds >= (std::uint64_t{1} << 48U))
      return std::unexpected(diagnostic("base.id.timestamp-range",
                                        "UUIDv7 timestamp is too large"));
    Bytes bytes{};
    for (std::size_t index = 0; index < 6; ++index) {
      const unsigned shift = static_cast<unsigned>((5 - index) * 8);
      bytes[index] = static_cast<std::uint8_t>(unixMilliseconds >> shift);
    }
    bytes[6] = static_cast<std::uint8_t>(0x70U | (random[0] & 0x0fU));
    bytes[7] = random[1];
    bytes[8] = static_cast<std::uint8_t>(0x80U | (random[2] & 0x3fU));
    std::copy(random.cbegin() + 3, random.cend(), bytes.begin() + 9);
    return TypedId{bytes};
  }

  [[nodiscard]] static Result<TypedId> fromBytes(Bytes bytes) {
    if ((bytes[6] >> 4U) != 7U || (bytes[8] & 0xc0U) != 0x80U)
      return std::unexpected(diagnostic(
          "base.id.not-uuidv7", "identifier is not an RFC 9562 UUIDv7"));
    return TypedId{std::move(bytes)};
  }

  [[nodiscard]] static Result<TypedId> parse(std::string_view text) {
    if (text.size() != 36 || text[8] != '-' || text[13] != '-' ||
        text[18] != '-' || text[23] != '-')
      return std::unexpected(
          diagnostic("base.id.invalid", "identifier is not a canonical UUID"));
    std::string compact;
    compact.reserve(32);
    for (const char character : text) {
      if (character != '-')
        compact.push_back(character);
    }
    auto bytes = detail::decodeHex<16>(compact, "base.id.invalid");
    if (!bytes)
      return std::unexpected(std::move(bytes.error()));
    return fromBytes(std::move(*bytes));
  }

  [[nodiscard]] const Bytes &bytes() const { return bytes_; }
  [[nodiscard]] std::string toString() const {
    const std::string compact = detail::encodeHex(bytes_);
    return compact.substr(0, 8) + '-' + compact.substr(8, 4) + '-' +
           compact.substr(12, 4) + '-' + compact.substr(16, 4) + '-' +
           compact.substr(20);
  }
  auto operator<=>(const TypedId &) const = default;

private:
  explicit TypedId(Bytes bytes) : bytes_(std::move(bytes)) {}
  Bytes bytes_;
};

struct ProjectIdTag;
struct RequestIdTag;
struct ActorIdTag;
struct JobIdTag;
struct PermissionContextIdTag;
struct GestureIdTag;
struct TransactionIdTag;
struct RecordIdTag;
struct ModelFunctionIdTag;
struct ModelInputIdTag;
struct ModelOutputIdTag;
struct ModelCallIdTag;
struct ModelBindingIdTag;
struct ArtifactIdTag;
struct WorkerInstanceIdTag;
struct SketchObjectIdTag;
struct SketchEntityIdTag;
struct SketchConstraintIdTag;

using ProjectId = TypedId<ProjectIdTag>;
using RequestId = TypedId<RequestIdTag>;
using ActorId = TypedId<ActorIdTag>;
using JobId = TypedId<JobIdTag>;
using PermissionContextId = TypedId<PermissionContextIdTag>;
using GestureId = TypedId<GestureIdTag>;
using TransactionId = TypedId<TransactionIdTag>;
using RecordId = TypedId<RecordIdTag>;
using ModelFunctionId = TypedId<ModelFunctionIdTag>;
using ModelInputId = TypedId<ModelInputIdTag>;
using ModelOutputId = TypedId<ModelOutputIdTag>;
using ModelCallId = TypedId<ModelCallIdTag>;
using ModelBindingId = TypedId<ModelBindingIdTag>;
using ArtifactId = TypedId<ArtifactIdTag>;
using WorkerInstanceId = TypedId<WorkerInstanceIdTag>;
using SketchObjectId = TypedId<SketchObjectIdTag>;
using SketchEntityId = TypedId<SketchEntityIdTag>;
using SketchConstraintId = TypedId<SketchConstraintIdTag>;

template <typename Tag> struct TypedIdHash {
  [[nodiscard]] std::size_t operator()(const TypedId<Tag> &value) const {
    std::size_t hash = sizeof(std::size_t) == 8
                           ? static_cast<std::size_t>(14695981039346656037ULL)
                           : static_cast<std::size_t>(2166136261U);
    const std::size_t prime = sizeof(std::size_t) == 8
                                  ? static_cast<std::size_t>(1099511628211ULL)
                                  : static_cast<std::size_t>(16777619U);
    for (const std::uint8_t byte : value.bytes()) {
      hash ^= byte;
      hash *= prime;
    }
    return hash;
  }
};

template <typename Tag> class TypedDigest final {
public:
  using Bytes = std::array<std::uint8_t, 32>;

  [[nodiscard]] static Result<TypedDigest> fromBytes(std::string algorithm,
                                                     Bytes bytes) {
    if (algorithm.empty() || algorithm.size() > 32 ||
        !std::ranges::all_of(algorithm, [](const char character) {
          return (character >= 'a' && character <= 'z') ||
                 (character >= '0' && character <= '9') || character == '-';
        }))
      return std::unexpected(diagnostic("base.digest.invalid-algorithm",
                                        "digest algorithm ID is invalid"));
    return TypedDigest{std::move(algorithm), std::move(bytes)};
  }

  [[nodiscard]] static Result<TypedDigest> parse(std::string_view text) {
    const std::size_t separator = text.find(':');
    if (separator == std::string_view::npos)
      return std::unexpected(
          diagnostic("base.digest.invalid", "digest has no algorithm"));
    const std::string algorithm{text.substr(0, separator)};
    auto bytes = detail::decodeHex<32>(text.substr(separator + 1),
                                       "base.digest.invalid");
    if (!bytes)
      return std::unexpected(std::move(bytes.error()));
    return fromBytes(algorithm, std::move(*bytes));
  }

  [[nodiscard]] const std::string &algorithm() const { return algorithm_; }
  [[nodiscard]] const Bytes &bytes() const { return bytes_; }
  [[nodiscard]] std::string toString() const {
    return algorithm_ + ':' + detail::encodeHex(bytes_);
  }

  auto operator<=>(const TypedDigest &) const = default;

private:
  TypedDigest(std::string algorithm, Bytes bytes)
      : algorithm_(std::move(algorithm)), bytes_(std::move(bytes)) {}
  std::string algorithm_;
  Bytes bytes_;
};

struct RevisionIdTag;
struct ContentDigestTag;
struct ArtifactDigestTag;
struct SchemaSetDigestTag;
struct EvaluatorDigestTag;
struct EnvironmentDigestTag;
struct CapabilityProfileDigestTag;
struct EvaluationKeyTag;

using RevisionId = TypedDigest<RevisionIdTag>;
using ContentDigest = TypedDigest<ContentDigestTag>;
using ArtifactDigest = TypedDigest<ArtifactDigestTag>;
using SchemaSetDigest = TypedDigest<SchemaSetDigestTag>;
using EvaluatorDigest = TypedDigest<EvaluatorDigestTag>;
using EnvironmentDigest = TypedDigest<EnvironmentDigestTag>;
using CapabilityProfileDigest = TypedDigest<CapabilityProfileDigestTag>;
using EvaluationKey = TypedDigest<EvaluationKeyTag>;

template <typename Tag> struct TypedDigestHash {
  [[nodiscard]] std::size_t operator()(const TypedDigest<Tag> &value) const {
    std::size_t hash = std::hash<std::string>{}(value.algorithm());
    for (const std::uint8_t byte : value.bytes())
      hash = (hash ^ byte) * static_cast<std::size_t>(1099511628211ULL);
    return hash;
  }
};

struct Length {};
struct Mass {};
struct Time {};
struct Angle {};
struct TemperatureAbsolute {};
struct TemperatureDelta {};
struct ElectricCurrent {};
struct Amount {};
struct LuminousIntensity {};
struct Dimensionless {};

template <typename Dimension> class Quantity final {
public:
  [[nodiscard]] static Result<Quantity> fromSi(double value) {
    if (!std::isfinite(value))
      return std::unexpected(
          diagnostic("base.quantity.non-finite", "quantity is not finite"));
    return Quantity{value};
  }

  [[nodiscard]] static Result<Quantity> fromUnit(double value,
                                                 double siPerUnit) {
    if (!std::isfinite(value) || !std::isfinite(siPerUnit) ||
        siPerUnit <= 0.0 || !std::isfinite(value * siPerUnit))
      return std::unexpected(diagnostic("base.quantity.invalid-unit-value",
                                        "unit value is invalid"));
    return Quantity{value * siPerUnit};
  }

  [[nodiscard]] double si() const { return si_; }
  [[nodiscard]] Result<double> in(double siPerUnit) const {
    if (!std::isfinite(siPerUnit) || siPerUnit <= 0.0)
      return std::unexpected(
          diagnostic("base.unit.invalid-scale", "unit scale is invalid"));
    return si_ / siPerUnit;
  }

  auto operator<=>(const Quantity &) const = default;

private:
  explicit Quantity(double si) : si_(si == 0.0 ? 0.0 : si) {}
  double si_;
};

using CancellationToken = std::stop_token;
using CancellationSource = std::stop_source;

struct ProgressEvent {
  std::uint64_t sequence = 0;
  double completed = 0.0;
  std::string phase;

  [[nodiscard]] static Result<ProgressEvent>
  create(std::uint64_t sequence, double completed, std::string phase) {
    if (!std::isfinite(completed) || completed < 0.0 || completed > 1.0)
      return std::unexpected(diagnostic("base.progress.out-of-range",
                                        "progress is outside zero to one"));
    return ProgressEvent{sequence, completed, std::move(phase)};
  }
};

} // namespace kearne
