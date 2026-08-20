#pragma once

#include <kearne/base/value.hpp>
#include <kearne/document/canonical.hpp>

#include <array>
#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace kearne::document {

class ProjectPath final {
public:
  [[nodiscard]] static Result<ProjectPath> parse(std::string_view text);
  [[nodiscard]] const std::string &value() const { return value_; }
  auto operator<=>(const ProjectPath &) const = default;

private:
  explicit ProjectPath(std::string value) : value_(std::move(value)) {}
  std::string value_;
};

struct ProjectPathHash {
  [[nodiscard]] std::size_t operator()(const ProjectPath &path) const;
};

enum class Lifecycle : std::uint8_t { Active = 1, Suppressed = 2 };

struct CreationProvenance {
  ActorId actor;
  Origin origin;
  std::optional<RequestId> request;
  std::uint64_t createdAtUnixMilliseconds;
  bool operator==(const CreationProvenance &) const = default;
};

struct ContentEntry {
  ContentDigest digest;
  std::uint64_t byteSize;
  std::string mediaType;
  bool operator==(const ContentEntry &) const = default;
};

struct VersionedPayload {
  std::string kind;
  std::uint32_t schemaVersion;
  Bytes bytes;
  bool operator==(const VersionedPayload &) const = default;
};

struct EngineeringRecord {
  RecordId id;
  std::optional<RecordId> owner;
  Lifecycle lifecycle;
  VersionedPayload value;
  CreationProvenance provenance;
  bool operator==(const EngineeringRecord &) const = default;
};

inline constexpr std::string_view datumPlaneRecordKind = "kearne.datum.plane";
inline constexpr std::string_view componentDefinitionRecordKind =
    "kearne.component.definition";
inline constexpr std::uint32_t datumPlanePayloadSchemaVersion = 1;
inline constexpr double maxDatumPlaneOriginMetres = 1.0e6;
inline constexpr std::size_t maxModelInputs = 64;
inline constexpr std::size_t maxModelOutputs = 64;

struct DatumPlanePayloadV1 {
  std::array<double, 3> originMetres;
  std::array<double, 3> xDirection;
  std::array<double, 3> normalDirection;
  bool operator==(const DatumPlanePayloadV1 &) const = default;
};

enum class TopologyPublicationMode : std::uint8_t {
  Labeled = 1,
  BodyOnly = 2,
  Dumb = 3,
};

enum class ModelValueKind : std::uint8_t {
  Length = 1,
  SketchPlane = 2,
  Plane = 3,
  Sketch = 4,
};

struct ModelInputPort {
  ModelInputId id;
  std::string pythonName;
  ModelValueKind kind;
  bool operator==(const ModelInputPort &) const = default;
};

struct ModelOutputPort {
  ModelOutputId id;
  std::string pythonName;
  ModelValueKind kind;
  bool operator==(const ModelOutputPort &) const = default;
};

struct ModelFunctionContract {
  ModelFunctionId id;
  ProjectPath module;
  std::string qualifiedName;
  EnvironmentDigest environment;
  CapabilityProfileDigest capabilityProfile;
  std::vector<ModelInputPort> inputs;
  std::vector<ModelOutputPort> outputs;
  TopologyPublicationMode topologyPublication =
      TopologyPublicationMode::BodyOnly;
  bool operator==(const ModelFunctionContract &) const = default;
};

struct DatumPlaneReference {
  RecordId record;
  bool operator==(const DatumPlaneReference &) const = default;
};

struct NamedOutputReference {
  ModelCallId call;
  ModelOutputId output;
  bool operator==(const NamedOutputReference &) const = default;
};

using ModelBindingValue =
    std::variant<Quantity<Length>, DatumPlaneReference, NamedOutputReference>;

struct ModelInputBinding {
  ModelBindingId id;
  ModelInputId input;
  ModelBindingValue value;
  bool operator==(const ModelInputBinding &) const = default;
};

struct ModelCall {
  ModelCallId id;
  ModelFunctionId function;
  std::vector<ModelInputBinding> bindings;
  bool operator==(const ModelCall &) const = default;
};

struct ArtifactMetadata {
  ArtifactId id;
  ArtifactDigest digest;
  std::uint64_t byteSize;
  std::string mediaType;
  bool derived;
  std::optional<RevisionId> sourceRevision;
  std::optional<EvaluatorDigest> evaluator;
  bool operator==(const ArtifactMetadata &) const = default;
};

[[nodiscard]] Result<void> validate(const ContentEntry &value);
[[nodiscard]] Result<void> validate(const VersionedPayload &value);
[[nodiscard]] Result<void> validate(const DatumPlanePayloadV1 &value);
[[nodiscard]] Result<void> validate(const EngineeringRecord &value);
[[nodiscard]] Result<void> validate(const ModelFunctionContract &value);
[[nodiscard]] Result<void> validate(const ModelCall &value);
[[nodiscard]] Result<void> validate(const ArtifactMetadata &value);

[[nodiscard]] Result<Bytes> canonicalBytes(const DatumPlanePayloadV1 &value);
[[nodiscard]] Result<DatumPlanePayloadV1>
decodeDatumPlanePayloadV1(std::span<const std::uint8_t> bytes);

[[nodiscard]] Result<ContentDigest> digestOf(const EngineeringRecord &value);
[[nodiscard]] Result<ContentDigest>
digestOf(const ModelFunctionContract &value);
[[nodiscard]] Result<ContentDigest> digestOf(const ModelCall &value);
[[nodiscard]] Result<ContentDigest> digestOf(const ArtifactMetadata &value);

} // namespace kearne::document
