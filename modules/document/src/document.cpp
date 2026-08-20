#include <kearne/document/project_state_access.hpp>

#include <immer/map.hpp>
#include <immer/vector.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <queue>
#include <ranges>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace kearne::document {
namespace {

constexpr std::size_t tableShardCount = 1024;
static_assert((tableShardCount & (tableShardCount - 1)) == 0);

Result<void> invalid(std::string code, std::string summary) {
  return std::unexpected(diagnostic(std::move(code), std::move(summary)));
}

bool validStableName(std::string_view value) {
  return !value.empty() && value.size() <= 128 && value.front() >= 'a' &&
         value.front() <= 'z' && std::ranges::all_of(value, [](char character) {
           return (character >= 'a' && character <= 'z') ||
                  (character >= '0' && character <= '9') || character == '.' ||
                  character == '_' || character == '-';
         });
}

bool validPythonName(std::string_view value) {
  if (value.empty() || value.size() > 64 || !isValidUtf8(value))
    return false;
  for (std::size_t index = 0; index < value.size(); ++index) {
    const unsigned char character = value[index];
    if (character >= 0x80U)
      continue;
    const bool letter = (character >= 'a' && character <= 'z') ||
                        (character >= 'A' && character <= 'Z');
    if (!letter && character != '_' &&
        (index == 0 || character < '0' || character > '9'))
      return false;
  }
  return true;
}

bool validQualifiedPythonName(std::string_view value) {
  if (value.empty() || value.size() > 256 || !isValidUtf8(value))
    return false;
  std::size_t start = 0;
  while (start < value.size()) {
    const std::size_t end = value.find('.', start);
    if (!validPythonName(value.substr(start, end == std::string_view::npos
                                                 ? value.size() - start
                                                 : end - start)))
      return false;
    if (end == std::string_view::npos)
      return true;
    start = end + 1;
  }
  return false;
}

bool validModelValueKind(ModelValueKind kind) {
  switch (kind) {
  case ModelValueKind::Length:
  case ModelValueKind::SketchPlane:
  case ModelValueKind::Plane:
  case ModelValueKind::Sketch:
    return true;
  }
  return false;
}

bool validTopologyPublication(TopologyPublicationMode mode) {
  switch (mode) {
  case TopologyPublicationMode::Labeled:
  case TopologyPublicationMode::BodyOnly:
  case TopologyPublicationMode::Dumb:
    return true;
  }
  return false;
}

template <typename Value>
std::vector<const Value *> sortedById(const std::vector<Value> &values) {
  std::vector<const Value *> result;
  result.reserve(values.size());
  for (const Value &value : values)
    result.push_back(&value);
  std::ranges::sort(result, {},
                    [](const Value *value) { return value->id.bytes(); });
  return result;
}

Result<void> writeText(CanonicalWriter &writer, std::string_view value) {
  return writer.text(value);
}

template <typename Value>
void writeOptionalId(CanonicalWriter &writer,
                     const std::optional<Value> &value) {
  writer.boolean(value.has_value());
  if (value)
    writer.identifier(*value);
}

template <typename Value>
void writeOptionalDigest(CanonicalWriter &writer,
                         const std::optional<Value> &value) {
  writer.boolean(value.has_value());
  if (value)
    writer.digest(*value);
}

Result<void> writePayload(CanonicalWriter &writer,
                          const VersionedPayload &value) {
  if (auto result = writeText(writer, value.kind); !result)
    return result;
  writer.unsignedInteger(value.schemaVersion);
  writer.bytes(value.bytes);
  return {};
}

Result<void> writeContent(CanonicalWriter &writer, const ContentEntry &value) {
  writer.digest(value.digest);
  writer.unsignedInteger(value.byteSize);
  return writeText(writer, value.mediaType);
}

Result<void> writeRecord(CanonicalWriter &writer,
                         const EngineeringRecord &value) {
  writer.identifier(value.id);
  writeOptionalId(writer, value.owner);
  writer.unsignedInteger(static_cast<std::uint8_t>(value.lifecycle));
  if (auto result = writePayload(writer, value.value); !result)
    return result;
  writer.identifier(value.provenance.actor);
  writer.unsignedInteger(static_cast<std::uint8_t>(value.provenance.origin));
  writeOptionalId(writer, value.provenance.request);
  writer.unsignedInteger(value.provenance.createdAtUnixMilliseconds);
  return {};
}

Result<void> writeFunction(CanonicalWriter &writer,
                           const ModelFunctionContract &value) {
  writer.identifier(value.id);
  if (auto result = writeText(writer, value.module.value()); !result)
    return result;
  if (auto result = writeText(writer, value.qualifiedName); !result)
    return result;
  writer.digest(value.environment);
  writer.digest(value.capabilityProfile);
  writer.unsignedInteger(static_cast<std::uint8_t>(value.topologyPublication));
  const auto inputs = sortedById(value.inputs);
  writer.unsignedInteger(inputs.size());
  for (const ModelInputPort *input : inputs) {
    writer.identifier(input->id);
    if (auto result = writeText(writer, input->pythonName); !result)
      return result;
    writer.unsignedInteger(static_cast<std::uint8_t>(input->kind));
  }
  const auto outputs = sortedById(value.outputs);
  writer.unsignedInteger(outputs.size());
  for (const ModelOutputPort *output : outputs) {
    writer.identifier(output->id);
    if (auto result = writeText(writer, output->pythonName); !result)
      return result;
    writer.unsignedInteger(static_cast<std::uint8_t>(output->kind));
  }
  return {};
}

Result<void> writeCall(CanonicalWriter &writer, const ModelCall &value) {
  writer.identifier(value.id);
  writer.identifier(value.function);
  const auto bindings = sortedById(value.bindings);
  writer.unsignedInteger(bindings.size());
  for (const ModelInputBinding *binding : bindings) {
    writer.identifier(binding->id);
    writer.identifier(binding->input);
    auto result = std::visit(
        [&writer](const auto &bound) -> Result<void> {
          using Type = std::decay_t<decltype(bound)>;
          if constexpr (std::is_same_v<Type, Quantity<Length>>) {
            writer.unsignedInteger(1);
            return writer.binary64(bound.si());
          } else if constexpr (std::is_same_v<Type, DatumPlaneReference>) {
            writer.unsignedInteger(2);
            writer.identifier(bound.record);
          } else {
            writer.unsignedInteger(3);
            writer.identifier(bound.call);
            writer.identifier(bound.output);
          }
          return {};
        },
        binding->value);
    if (!result)
      return result;
  }
  return {};
}

Result<void> writeArtifact(CanonicalWriter &writer,
                           const ArtifactMetadata &value) {
  writer.identifier(value.id);
  writer.digest(value.digest);
  writer.unsignedInteger(value.byteSize);
  if (auto result = writeText(writer, value.mediaType); !result)
    return result;
  writer.boolean(value.derived);
  writeOptionalDigest(writer, value.sourceRevision);
  writeOptionalDigest(writer, value.evaluator);
  return {};
}

template <typename Value, typename Write>
Result<ContentDigest> digestEntity(std::string_view type,
                                   std::string_view context, const Value &value,
                                   Write write) {
  CanonicalWriter writer;
  writer.header(type, 1);
  if (auto result = write(writer, value); !result)
    return std::unexpected(std::move(result.error()));
  return hashCanonical<ContentDigest>(context, writer.value());
}

Result<ContentDigest> digestOf(const ContentEntry &value) {
  return digestEntity("content-entry", "kearne.content-entry.v1", value,
                      writeContent);
}

Bytes idBytes(const auto &id) { return {id.bytes().begin(), id.bytes().end()}; }

struct ContentPolicy {
  using Key = ProjectPath;
  using Value = ContentEntry;
  using Hash = ProjectPathHash;
  static constexpr std::string_view context = "content";
  static Bytes keyBytes(const Key &key) {
    return {key.value().begin(), key.value().end()};
  }
  static Result<ContentDigest> valueDigest(const Value &value) {
    return digestOf(value);
  }
};

struct RecordPolicy {
  using Key = RecordId;
  using Value = EngineeringRecord;
  using Hash = TypedIdHash<RecordIdTag>;
  static constexpr std::string_view context = "record";
  static Bytes keyBytes(const Key &key) { return idBytes(key); }
  static Result<ContentDigest> valueDigest(const Value &value) {
    return digestOf(value);
  }
};

struct FunctionPolicy {
  using Key = ModelFunctionId;
  using Value = ModelFunctionContract;
  using Hash = TypedIdHash<ModelFunctionIdTag>;
  static constexpr std::string_view context = "function";
  static Bytes keyBytes(const Key &key) { return idBytes(key); }
  static Result<ContentDigest> valueDigest(const Value &value) {
    return digestOf(value);
  }
};

struct CallPolicy {
  using Key = ModelCallId;
  using Value = ModelCall;
  using Hash = TypedIdHash<ModelCallIdTag>;
  static constexpr std::string_view context = "call";
  static Bytes keyBytes(const Key &key) { return idBytes(key); }
  static Result<ContentDigest> valueDigest(const Value &value) {
    return digestOf(value);
  }
};

struct ArtifactPolicy {
  using Key = ArtifactId;
  using Value = ArtifactMetadata;
  using Hash = TypedIdHash<ArtifactIdTag>;
  static constexpr std::string_view context = "artifact";
  static Bytes keyBytes(const Key &key) { return idBytes(key); }
  static Result<ContentDigest> valueDigest(const Value &value) {
    return digestOf(value);
  }
};

template <typename Policy> class PersistentTable final {
public:
  using Key = typename Policy::Key;
  using Value = typename Policy::Value;
  using Map = immer::map<Key, Value, typename Policy::Hash>;

  struct Bucket {
    Map values;
    ContentDigest digest;
  };

  [[nodiscard]] static Result<PersistentTable> create() {
    auto emptyDigest = digestBucket(0, Map{});
    if (!emptyDigest)
      return std::unexpected(std::move(emptyDigest.error()));
    immer::vector<Bucket> buckets;
    for (std::size_t index = 0; index < tableShardCount; ++index) {
      auto digest = index == 0 ? emptyDigest : digestBucket(index, Map{});
      if (!digest)
        return std::unexpected(std::move(digest.error()));
      buckets = std::move(buckets).push_back({{}, std::move(*digest)});
    }
    auto merkle = buildMerkle(buckets);
    if (!merkle)
      return std::unexpected(std::move(merkle.error()));
    auto root = digestRoot(*merkle, 0);
    if (!root)
      return std::unexpected(std::move(root.error()));
    return PersistentTable{std::move(buckets), std::move(*merkle), 0,
                           std::move(*root)};
  }

  [[nodiscard]] const Value *find(const Key &key) const {
    return buckets_[shard(key)].values.find(key);
  }

  [[nodiscard]] Result<PersistentTable> set(Key key, Value value) const {
    const std::size_t index = shard(key);
    Bucket bucket = buckets_[index];
    const bool inserted = bucket.values.find(key) == nullptr;
    bucket.values =
        std::move(bucket.values).set(std::move(key), std::move(value));
    auto digest = digestBucket(index, bucket.values);
    if (!digest)
      return std::unexpected(std::move(digest.error()));
    bucket.digest = std::move(*digest);
    auto buckets = buckets_.set(index, std::move(bucket));
    auto merkle = updateMerkle(merkle_, index, buckets[index].digest);
    if (!merkle)
      return std::unexpected(std::move(merkle.error()));
    const std::size_t size = size_ + static_cast<std::size_t>(inserted);
    auto root = digestRoot(*merkle, size);
    if (!root)
      return std::unexpected(std::move(root.error()));
    return PersistentTable{std::move(buckets), std::move(*merkle), size,
                           std::move(*root)};
  }

  [[nodiscard]] Result<PersistentTable> erase(const Key &key) const {
    const std::size_t index = shard(key);
    Bucket bucket = buckets_[index];
    if (!bucket.values.find(key))
      return *this;
    bucket.values = std::move(bucket.values).erase(key);
    auto digest = digestBucket(index, bucket.values);
    if (!digest)
      return std::unexpected(std::move(digest.error()));
    bucket.digest = std::move(*digest);
    auto buckets = buckets_.set(index, std::move(bucket));
    auto merkle = updateMerkle(merkle_, index, buckets[index].digest);
    if (!merkle)
      return std::unexpected(std::move(merkle.error()));
    auto root = digestRoot(*merkle, size_ - 1);
    if (!root)
      return std::unexpected(std::move(root.error()));
    return PersistentTable{std::move(buckets), std::move(*merkle), size_ - 1,
                           std::move(*root)};
  }

  [[nodiscard]] std::size_t size() const { return size_; }
  [[nodiscard]] const ContentDigest &digest() const { return digest_; }

  [[nodiscard]] std::vector<std::pair<Key, Value>> items() const {
    std::vector<std::pair<Key, Value>> result;
    result.reserve(size_);
    for (const Bucket &bucket : buckets_)
      result.insert(result.end(), bucket.values.begin(), bucket.values.end());
    std::ranges::sort(result, [](const auto &left, const auto &right) {
      return Policy::keyBytes(left.first) < Policy::keyBytes(right.first);
    });
    return result;
  }

private:
  PersistentTable(immer::vector<Bucket> buckets,
                  immer::vector<ContentDigest> merkle, std::size_t size,
                  ContentDigest digest)
      : buckets_(std::move(buckets)), merkle_(std::move(merkle)), size_(size),
        digest_(std::move(digest)) {}

  [[nodiscard]] static std::size_t shard(const Key &key) {
    const Bytes bytes = Policy::keyBytes(key);
    auto digest = hashCanonical<ContentDigest>("kearne.table.shard.v1", bytes);
    if (!digest)
      std::terminate();
    return ((static_cast<std::size_t>(digest->bytes()[0]) << 8U) |
            digest->bytes()[1]) &
           (tableShardCount - 1);
  }

  [[nodiscard]] static Result<ContentDigest> digestBucket(std::size_t index,
                                                          const Map &values) {
    struct Item {
      Bytes key;
      const Value *value;
    };
    std::vector<Item> items;
    items.reserve(values.size());
    for (const auto &[key, value] : values)
      items.push_back({Policy::keyBytes(key), &value});
    std::ranges::sort(items, {}, &Item::key);

    CanonicalWriter writer;
    writer.header("table-bucket", 1);
    if (auto result = writer.text(Policy::context); !result)
      return std::unexpected(std::move(result.error()));
    writer.unsignedInteger(index);
    writer.unsignedInteger(items.size());
    for (const Item &item : items) {
      writer.bytes(item.key);
      auto valueDigest = Policy::valueDigest(*item.value);
      if (!valueDigest)
        return std::unexpected(std::move(valueDigest.error()));
      writer.digest(*valueDigest);
    }
    return hashCanonical<ContentDigest>("kearne.table.bucket.v1",
                                        writer.value());
  }

  [[nodiscard]] static Result<ContentDigest>
  digestNode(std::size_t index, const ContentDigest &left,
             const ContentDigest &right) {
    CanonicalWriter writer;
    writer.header("table-merkle-node", 1);
    writer.unsignedInteger(index);
    writer.digest(left);
    writer.digest(right);
    return hashCanonical<ContentDigest>("kearne.table.merkle-node.v1",
                                        writer.value());
  }

  [[nodiscard]] static Result<immer::vector<ContentDigest>>
  buildMerkle(const immer::vector<Bucket> &buckets) {
    constexpr std::size_t leafStart = tableShardCount - 1;
    std::vector<ContentDigest> values;
    values.reserve(tableShardCount * 2 - 1);
    for (std::size_t index = 0; index < leafStart; ++index)
      values.push_back(buckets[0].digest);
    for (const Bucket &bucket : buckets)
      values.push_back(bucket.digest);
    for (std::size_t index = leafStart; index-- > 0;) {
      auto digest =
          digestNode(index, values[index * 2 + 1], values[index * 2 + 2]);
      if (!digest)
        return std::unexpected(std::move(digest.error()));
      values[index] = std::move(*digest);
    }
    return immer::vector<ContentDigest>{values.begin(), values.end()};
  }

  [[nodiscard]] static Result<immer::vector<ContentDigest>>
  updateMerkle(const immer::vector<ContentDigest> &base, std::size_t shardIndex,
               const ContentDigest &leafDigest) {
    std::size_t index = tableShardCount - 1 + shardIndex;
    auto result = base.set(index, leafDigest);
    while (index != 0) {
      index = (index - 1) / 2;
      auto digest =
          digestNode(index, result[index * 2 + 1], result[index * 2 + 2]);
      if (!digest)
        return std::unexpected(std::move(digest.error()));
      result = std::move(result).set(index, std::move(*digest));
    }
    return result;
  }

  [[nodiscard]] static Result<ContentDigest>
  digestRoot(const immer::vector<ContentDigest> &merkle, std::size_t size) {
    CanonicalWriter writer;
    writer.header("persistent-table", 1);
    if (auto result = writer.text(Policy::context); !result)
      return std::unexpected(std::move(result.error()));
    writer.unsignedInteger(tableShardCount);
    writer.unsignedInteger(size);
    writer.digest(merkle[0]);
    return hashCanonical<ContentDigest>("kearne.table.root.v1", writer.value());
  }

  immer::vector<Bucket> buckets_;
  immer::vector<ContentDigest> merkle_;
  std::size_t size_;
  ContentDigest digest_;
};

using ContentTable = PersistentTable<ContentPolicy>;
using RecordTable = PersistentTable<RecordPolicy>;
using FunctionTable = PersistentTable<FunctionPolicy>;
using CallTable = PersistentTable<CallPolicy>;
using ArtifactTable = PersistentTable<ArtifactPolicy>;

Result<ContentDigest>
projectRootDigest(ProjectId project, const SchemaSetDigest &schemaSet,
                  const ContentTable &content, const RecordTable &records,
                  const FunctionTable &functions, const CallTable &calls,
                  const ArtifactTable &artifacts) {
  CanonicalWriter writer;
  writer.header("project-state", 1);
  writer.identifier(project);
  writer.digest(schemaSet);
  writer.digest(content.digest());
  writer.digest(records.digest());
  writer.digest(functions.digest());
  writer.digest(calls.digest());
  writer.digest(artifacts.digest());
  return hashCanonical<ContentDigest>("kearne.project-state.v1",
                                      writer.value());
}

template <typename Value>
Result<ContentDigest> checkedDigest(const Value &value) {
  return digestOf(value);
}

} // namespace

Result<ProjectPath> ProjectPath::parse(std::string_view text) {
  if (text.empty() || text.size() > 1024 || text.front() == '/' ||
      text.back() == '/' || text.find("//") != std::string_view::npos ||
      !isValidUtf8(text))
    return std::unexpected(
        diagnostic("document.path.invalid", "project path is invalid"));
  std::size_t start = 0;
  while (start < text.size()) {
    const std::size_t end = text.find('/', start);
    const std::string_view segment =
        text.substr(start, end == std::string_view::npos ? text.size() - start
                                                         : end - start);
    if (segment.empty() || segment.size() > 255 || segment == "." ||
        segment == ".." ||
        std::ranges::any_of(segment, [](unsigned char character) {
          return character < 0x20U || character == 0x7fU || character == '\\';
        }))
      return std::unexpected(diagnostic("document.path.invalid-segment",
                                        "project path segment is invalid"));
    if (end == std::string_view::npos)
      break;
    start = end + 1;
  }
  return ProjectPath{std::string{text}};
}

std::size_t ProjectPathHash::operator()(const ProjectPath &path) const {
  std::size_t hash = sizeof(std::size_t) == 8
                         ? static_cast<std::size_t>(14695981039346656037ULL)
                         : static_cast<std::size_t>(2166136261U);
  const std::size_t prime = sizeof(std::size_t) == 8
                                ? static_cast<std::size_t>(1099511628211ULL)
                                : static_cast<std::size_t>(16777619U);
  for (const unsigned char byte : path.value()) {
    hash ^= byte;
    hash *= prime;
  }
  return hash;
}

Result<void> validate(const ContentEntry &value) {
  if (value.mediaType.empty() || value.mediaType.size() > 128 ||
      value.mediaType.find('/') == std::string::npos ||
      !isValidUtf8(value.mediaType))
    return invalid("document.content.invalid-media-type",
                   "content media type is invalid");
  return {};
}

Result<void> validate(const VersionedPayload &value) {
  if (!validStableName(value.kind) || value.schemaVersion == 0 ||
      value.bytes.size() > 16U * 1024U * 1024U)
    return invalid("document.payload.invalid", "versioned payload is invalid");
  return {};
}

Result<void> validate(const DatumPlanePayloadV1 &value) {
  constexpr double directionTolerance = 1.0e-10;
  const auto finite = [](const std::array<double, 3> &vector) {
    return std::ranges::all_of(vector,
                               [](double item) { return std::isfinite(item); });
  };
  if (!finite(value.originMetres))
    return invalid("document.datum-plane.origin-non-finite",
                   "datum plane SI origin must be finite");
  if (!finite(value.xDirection))
    return invalid("document.datum-plane.x-direction-non-finite",
                   "datum plane x direction must be finite");
  if (!finite(value.normalDirection))
    return invalid("document.datum-plane.normal-direction-non-finite",
                   "datum plane normal direction must be finite");
  if (std::ranges::any_of(value.originMetres, [](double item) {
        return std::abs(item) > maxDatumPlaneOriginMetres;
      }))
    return invalid("document.datum-plane.origin-range",
                   "datum plane SI origin exceeds the supported range");
  const auto dot = [](const std::array<double, 3> &left,
                      const std::array<double, 3> &right) {
    return left[0] * right[0] + left[1] * right[1] + left[2] * right[2];
  };
  if (std::abs(dot(value.xDirection, value.xDirection) - 1.0) >
      directionTolerance)
    return invalid("document.datum-plane.x-direction-non-unit",
                   "datum plane x direction must be a unit vector");
  if (std::abs(dot(value.normalDirection, value.normalDirection) - 1.0) >
      directionTolerance)
    return invalid("document.datum-plane.normal-direction-non-unit",
                   "datum plane normal direction must be a unit vector");
  if (std::abs(dot(value.xDirection, value.normalDirection)) >
      directionTolerance)
    return invalid("document.datum-plane.non-orthogonal",
                   "datum plane directions must define a right-handed frame");
  return {};
}

Result<Bytes> canonicalBytes(const DatumPlanePayloadV1 &value) {
  if (auto result = validate(value); !result)
    return std::unexpected(result.error());
  CanonicalWriter writer;
  writer.header("datum-plane", datumPlanePayloadSchemaVersion);
  for (double coordinate : value.originMetres)
    if (auto result = writer.binary64(coordinate); !result)
      return std::unexpected(result.error());
  for (double coordinate : value.xDirection)
    if (auto result = writer.binary64(coordinate); !result)
      return std::unexpected(result.error());
  for (double coordinate : value.normalDirection)
    if (auto result = writer.binary64(coordinate); !result)
      return std::unexpected(result.error());
  return std::move(writer).take();
}

Result<DatumPlanePayloadV1>
decodeDatumPlanePayloadV1(std::span<const std::uint8_t> bytes) {
  CanonicalReader reader(bytes);
  if (auto result =
          reader.header("datum-plane", datumPlanePayloadSchemaVersion);
      !result)
    return std::unexpected(result.error());
  DatumPlanePayloadV1 value{};
  for (double &coordinate : value.originMetres) {
    auto result = reader.binary64();
    if (!result)
      return std::unexpected(result.error());
    coordinate = *result;
  }
  for (double &coordinate : value.xDirection) {
    auto result = reader.binary64();
    if (!result)
      return std::unexpected(result.error());
    coordinate = *result;
  }
  for (double &coordinate : value.normalDirection) {
    auto result = reader.binary64();
    if (!result)
      return std::unexpected(result.error());
    coordinate = *result;
  }
  if (auto result = reader.end(); !result)
    return std::unexpected(result.error());
  if (auto result = validate(value); !result)
    return std::unexpected(result.error());
  return value;
}

Result<void> validate(const EngineeringRecord &value) {
  if (value.owner && *value.owner == value.id)
    return invalid("document.record.self-owned", "record cannot own itself");
  if (auto result = validate(value.value); !result)
    return result;
  if (value.value.kind == datumPlaneRecordKind) {
    if (value.value.schemaVersion != datumPlanePayloadSchemaVersion)
      return invalid("document.datum-plane.unsupported-schema",
                     "datum plane payload schema is unsupported");
    if (auto result = decodeDatumPlanePayloadV1(value.value.bytes); !result)
      return std::unexpected(result.error());
  }
  return {};
}

Result<void> validate(const ModelFunctionContract &value) {
  if (!validQualifiedPythonName(value.qualifiedName))
    return invalid("document.function.invalid-name",
                   "model function name is invalid");
  if (value.inputs.size() > maxModelInputs || value.outputs.empty() ||
      value.outputs.size() > maxModelOutputs)
    return invalid("document.function.invalid-port-count",
                   "model function port count is invalid");
  if (!validTopologyPublication(value.topologyPublication))
    return invalid("document.function.invalid-topology-publication",
                   "model function topology publication mode is invalid");

  std::unordered_set<ModelInputId, TypedIdHash<ModelInputIdTag>> inputIds;
  std::unordered_set<ModelOutputId, TypedIdHash<ModelOutputIdTag>> outputIds;
  std::unordered_set<std::string> inputNames;
  std::unordered_set<std::string> outputNames;
  for (const ModelInputPort &input : value.inputs)
    if (!validPythonName(input.pythonName) || !validModelValueKind(input.kind))
      return invalid("document.function.invalid-input",
                     "model function input is invalid");
    else if (!inputIds.insert(input.id).second ||
             !inputNames.insert(input.pythonName).second)
      return invalid("document.function.duplicate-input",
                     "model function input is duplicated");
  for (const ModelOutputPort &output : value.outputs)
    if (!validPythonName(output.pythonName) ||
        !validModelValueKind(output.kind))
      return invalid("document.function.invalid-output",
                     "model function output is invalid");
    else if (!outputIds.insert(output.id).second ||
             !outputNames.insert(output.pythonName).second)
      return invalid("document.function.duplicate-output",
                     "model function output is duplicated");
  return {};
}

Result<void> validate(const ModelCall &value) {
  if (value.bindings.size() > maxModelInputs)
    return invalid("document.call.too-many-bindings",
                   "model call has too many bindings");
  std::unordered_set<ModelBindingId, TypedIdHash<ModelBindingIdTag>> ids;
  std::unordered_set<ModelInputId, TypedIdHash<ModelInputIdTag>> inputs;
  for (const ModelInputBinding &binding : value.bindings) {
    if (!ids.insert(binding.id).second)
      return invalid("document.call.duplicate-binding",
                     "model call binding identity is duplicated");
    if (!inputs.insert(binding.input).second)
      return invalid("document.call.duplicate-input",
                     "model call input is bound more than once");
    if (const auto *length = std::get_if<Quantity<Length>>(&binding.value);
        length && !std::isfinite(length->si()))
      return invalid("document.call.non-finite-length",
                     "model call length is not finite");
  }
  return {};
}

Result<void> validate(const ArtifactMetadata &value) {
  if (value.mediaType.empty() || value.mediaType.size() > 128 ||
      value.mediaType.find('/') == std::string::npos ||
      !isValidUtf8(value.mediaType) ||
      value.derived !=
          (value.sourceRevision.has_value() && value.evaluator.has_value()))
    return invalid("document.artifact.invalid", "artifact metadata is invalid");
  return {};
}

Result<ContentDigest> digestOf(const EngineeringRecord &value) {
  if (auto result = validate(value); !result)
    return std::unexpected(std::move(result.error()));
  return digestEntity("engineering-record", "kearne.record.v1", value,
                      writeRecord);
}

Result<ContentDigest> digestOf(const ModelFunctionContract &value) {
  if (auto result = validate(value); !result)
    return std::unexpected(std::move(result.error()));
  return digestEntity("model-function", "kearne.function.v1", value,
                      writeFunction);
}

Result<ContentDigest> digestOf(const ModelCall &value) {
  if (auto result = validate(value); !result)
    return std::unexpected(std::move(result.error()));
  return digestEntity("model-call", "kearne.call.v1", value, writeCall);
}

Result<ContentDigest> digestOf(const ArtifactMetadata &value) {
  if (auto result = validate(value); !result)
    return std::unexpected(std::move(result.error()));
  return digestEntity("artifact-metadata", "kearne.artifact-metadata.v1", value,
                      writeArtifact);
}

Result<Bytes> canonicalBytes(const Mutation &mutation) {
  CanonicalWriter writer;
  writer.header("mutation", 1);
  auto result = std::visit(
      [&writer](const auto &value) -> Result<void> {
        using Type = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Type, PutContent>) {
          writer.unsignedInteger(1);
          if (auto text = writer.text(value.path.value()); !text)
            return text;
          writeOptionalDigest(writer, value.expectedPrior);
          return writeContent(writer, value.value);
        } else if constexpr (std::is_same_v<Type, MoveContent>) {
          writer.unsignedInteger(2);
          if (auto text = writer.text(value.from.value()); !text)
            return text;
          if (auto text = writer.text(value.to.value()); !text)
            return text;
          writer.digest(value.expected);
        } else if constexpr (std::is_same_v<Type, DeleteContent>) {
          writer.unsignedInteger(3);
          if (auto text = writer.text(value.path.value()); !text)
            return text;
          writer.digest(value.expected);
        } else if constexpr (std::is_same_v<Type, CreateRecord>) {
          writer.unsignedInteger(4);
          return writeRecord(writer, value.value);
        } else if constexpr (std::is_same_v<Type, ReplaceRecord>) {
          writer.unsignedInteger(5);
          writer.identifier(value.id);
          writer.digest(value.expected);
          return writeRecord(writer, value.value);
        } else if constexpr (std::is_same_v<Type, DeleteRecord>) {
          writer.unsignedInteger(6);
          writer.identifier(value.id);
          writer.digest(value.expected);
        } else if constexpr (std::is_same_v<Type, CreateFunction>) {
          writer.unsignedInteger(7);
          return writeFunction(writer, value.value);
        } else if constexpr (std::is_same_v<Type, ReplaceFunction>) {
          writer.unsignedInteger(8);
          writer.identifier(value.id);
          writer.digest(value.expected);
          return writeFunction(writer, value.value);
        } else if constexpr (std::is_same_v<Type, DeleteFunction>) {
          writer.unsignedInteger(9);
          writer.identifier(value.id);
          writer.digest(value.expected);
        } else if constexpr (std::is_same_v<Type, CreateCall>) {
          writer.unsignedInteger(10);
          return writeCall(writer, value.value);
        } else if constexpr (std::is_same_v<Type, ReplaceCall>) {
          writer.unsignedInteger(11);
          writer.identifier(value.id);
          writer.digest(value.expected);
          return writeCall(writer, value.value);
        } else if constexpr (std::is_same_v<Type, DeleteCall>) {
          writer.unsignedInteger(12);
          writer.identifier(value.id);
          writer.digest(value.expected);
        } else if constexpr (std::is_same_v<Type, AttachArtifact>) {
          writer.unsignedInteger(13);
          return writeArtifact(writer, value.value);
        } else if constexpr (std::is_same_v<Type, ReplaceArtifact>) {
          writer.unsignedInteger(14);
          writer.identifier(value.id);
          writer.digest(value.expected);
          return writeArtifact(writer, value.value);
        } else {
          writer.unsignedInteger(15);
          writer.identifier(value.id);
          writer.digest(value.expected);
        }
        return {};
      },
      mutation);
  if (!result)
    return std::unexpected(std::move(result.error()));
  return std::move(writer).take();
}

Result<Bytes> canonicalBytes(const MutationBatch &batch) {
  CanonicalWriter writer;
  writer.header("mutation-batch", 1);
  writer.unsignedInteger(batch.size());
  for (const Mutation &mutation : batch) {
    auto bytes = canonicalBytes(mutation);
    if (!bytes)
      return std::unexpected(std::move(bytes.error()));
    writer.bytes(*bytes);
  }
  return std::move(writer).take();
}

struct ProjectState::Data {
  ProjectId project;
  SchemaSetDigest schemaSet;
  ContentTable content;
  RecordTable records;
  FunctionTable functions;
  CallTable calls;
  ArtifactTable artifacts;
  ContentDigest rootDigest;
};

Result<ProjectState> ProjectState::create(ProjectId project,
                                          SchemaSetDigest schemaSet) {
  auto content = ContentTable::create();
  auto records = RecordTable::create();
  auto functions = FunctionTable::create();
  auto calls = CallTable::create();
  auto artifacts = ArtifactTable::create();
  if (!content || !records || !functions || !calls || !artifacts)
    return std::unexpected(diagnostic("document.state.initialize-failed",
                                      "project state could not initialize"));
  auto root = projectRootDigest(project, schemaSet, *content, *records,
                                *functions, *calls, *artifacts);
  if (!root)
    return std::unexpected(std::move(root.error()));
  return ProjectState{std::make_shared<const Data>(
      Data{std::move(project), std::move(schemaSet), std::move(*content),
           std::move(*records), std::move(*functions), std::move(*calls),
           std::move(*artifacts), std::move(*root)})};
}

ProjectId ProjectState::projectId() const { return data_->project; }
SchemaSetDigest ProjectState::schemaSet() const { return data_->schemaSet; }
ContentDigest ProjectState::rootDigest() const { return data_->rootDigest; }
std::size_t ProjectState::contentCount() const { return data_->content.size(); }
std::size_t ProjectState::recordCount() const { return data_->records.size(); }
std::size_t ProjectState::functionCount() const {
  return data_->functions.size();
}
std::size_t ProjectState::callCount() const { return data_->calls.size(); }
std::size_t ProjectState::artifactCount() const {
  return data_->artifacts.size();
}

std::optional<ContentEntry>
ProjectState::content(const ProjectPath &path) const {
  const ContentEntry *value = data_->content.find(path);
  return value ? std::optional{*value} : std::nullopt;
}

std::optional<EngineeringRecord> ProjectState::record(RecordId id) const {
  const EngineeringRecord *value = data_->records.find(id);
  return value ? std::optional{*value} : std::nullopt;
}

std::optional<ModelFunctionContract>
ProjectState::function(ModelFunctionId id) const {
  const ModelFunctionContract *value = data_->functions.find(id);
  return value ? std::optional{*value} : std::nullopt;
}

std::optional<ModelCall> ProjectState::call(ModelCallId id) const {
  const ModelCall *value = data_->calls.find(id);
  return value ? std::optional{*value} : std::nullopt;
}

std::optional<ArtifactMetadata> ProjectState::artifact(ArtifactId id) const {
  const ArtifactMetadata *value = data_->artifacts.find(id);
  return value ? std::optional{*value} : std::nullopt;
}

std::vector<std::pair<ProjectPath, ContentEntry>>
ProjectState::content() const {
  return data_->content.items();
}

std::vector<EngineeringRecord> ProjectState::records() const {
  std::vector<EngineeringRecord> result;
  result.reserve(data_->records.size());
  for (auto &[id, value] : data_->records.items())
    result.push_back(std::move(value));
  return result;
}

std::vector<ModelFunctionContract> ProjectState::functions() const {
  std::vector<ModelFunctionContract> result;
  result.reserve(data_->functions.size());
  for (auto &[id, value] : data_->functions.items())
    result.push_back(std::move(value));
  return result;
}

std::vector<ModelCall> ProjectState::calls() const {
  std::vector<ModelCall> result;
  result.reserve(data_->calls.size());
  for (auto &[id, value] : data_->calls.items())
    result.push_back(std::move(value));
  return result;
}

std::vector<ArtifactMetadata> ProjectState::artifacts() const {
  std::vector<ArtifactMetadata> result;
  result.reserve(data_->artifacts.size());
  for (auto &[id, value] : data_->artifacts.items())
    result.push_back(std::move(value));
  return result;
}

namespace internal {
namespace {

template <typename Table, typename Key>
Result<void> requireExpected(const Table &table, const Key &key,
                             const ContentDigest &expected) {
  const auto *current = table.find(key);
  if (!current)
    return invalid("document.mutation.missing", "mutation target is missing");
  auto digest = checkedDigest(*current);
  if (!digest)
    return std::unexpected(std::move(digest.error()));
  if (*digest != expected)
    return invalid("document.mutation.stale", "mutation target is stale");
  return {};
}

template <typename Table, typename Key, typename Value>
Result<void> create(Table &table, const Key &key, Value value) {
  if (table.find(key))
    return invalid("document.mutation.exists",
                   "mutation target already exists");
  if (auto result = document::validate(value); !result)
    return result;
  auto updated = table.set(key, std::move(value));
  if (!updated)
    return std::unexpected(std::move(updated.error()));
  table = std::move(*updated);
  return {};
}

template <typename Table, typename Key, typename Value>
Result<void> replace(Table &table, const Key &key,
                     const ContentDigest &expected, Value value) {
  if (value.id != key)
    return invalid("document.mutation.identity-change",
                   "replacement changes stable identity");
  if (auto result = requireExpected(table, key, expected); !result)
    return result;
  if (auto result = document::validate(value); !result)
    return result;
  auto updated = table.set(key, std::move(value));
  if (!updated)
    return std::unexpected(std::move(updated.error()));
  table = std::move(*updated);
  return {};
}

template <typename Table, typename Key>
Result<void> erase(Table &table, const Key &key,
                   const ContentDigest &expected) {
  if (auto result = requireExpected(table, key, expected); !result)
    return result;
  auto updated = table.erase(key);
  if (!updated)
    return std::unexpected(std::move(updated.error()));
  table = std::move(*updated);
  return {};
}

Result<void> apply(auto &state, const Mutation &mutation) {
  return std::visit(
      [&state](const auto &value) -> Result<void> {
        using Type = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Type, PutContent>) {
          const ContentEntry *prior = state.content.find(value.path);
          if (value.expectedPrior.has_value() != (prior != nullptr) ||
              (prior && prior->digest != *value.expectedPrior))
            return invalid("document.mutation.stale",
                           "content mutation target is stale");
          if (auto result = document::validate(value.value); !result)
            return result;
          auto updated = state.content.set(value.path, value.value);
          if (!updated)
            return std::unexpected(std::move(updated.error()));
          state.content = std::move(*updated);
          return {};
        } else if constexpr (std::is_same_v<Type, MoveContent>) {
          const ContentEntry *prior = state.content.find(value.from);
          if (!prior || prior->digest != value.expected ||
              state.content.find(value.to))
            return invalid("document.mutation.stale",
                           "content move precondition failed");
          auto inserted = state.content.set(value.to, *prior);
          if (!inserted)
            return std::unexpected(std::move(inserted.error()));
          auto removed = inserted->erase(value.from);
          if (!removed)
            return std::unexpected(std::move(removed.error()));
          state.content = std::move(*removed);
          return {};
        } else if constexpr (std::is_same_v<Type, DeleteContent>) {
          const ContentEntry *prior = state.content.find(value.path);
          if (!prior || prior->digest != value.expected)
            return invalid("document.mutation.stale",
                           "content delete precondition failed");
          auto updated = state.content.erase(value.path);
          if (!updated)
            return std::unexpected(std::move(updated.error()));
          state.content = std::move(*updated);
          return {};
        } else if constexpr (std::is_same_v<Type, CreateRecord>) {
          return create(state.records, value.value.id, value.value);
        } else if constexpr (std::is_same_v<Type, ReplaceRecord>) {
          return replace(state.records, value.id, value.expected, value.value);
        } else if constexpr (std::is_same_v<Type, DeleteRecord>) {
          return erase(state.records, value.id, value.expected);
        } else if constexpr (std::is_same_v<Type, CreateFunction>) {
          return create(state.functions, value.value.id, value.value);
        } else if constexpr (std::is_same_v<Type, ReplaceFunction>) {
          return replace(state.functions, value.id, value.expected,
                         value.value);
        } else if constexpr (std::is_same_v<Type, DeleteFunction>) {
          return erase(state.functions, value.id, value.expected);
        } else if constexpr (std::is_same_v<Type, CreateCall>) {
          return create(state.calls, value.value.id, value.value);
        } else if constexpr (std::is_same_v<Type, ReplaceCall>) {
          return replace(state.calls, value.id, value.expected, value.value);
        } else if constexpr (std::is_same_v<Type, DeleteCall>) {
          return erase(state.calls, value.id, value.expected);
        } else if constexpr (std::is_same_v<Type, AttachArtifact>) {
          return create(state.artifacts, value.value.id, value.value);
        } else if constexpr (std::is_same_v<Type, ReplaceArtifact>) {
          return replace(state.artifacts, value.id, value.expected,
                         value.value);
        } else {
          return erase(state.artifacts, value.id, value.expected);
        }
      },
      mutation);
}

bool requiresStructuralValidation(const Mutation &mutation) {
  return std::visit(
      []<typename Value>(const Value &) {
        using Type = std::decay_t<Value>;
        // Adding or replacing bytes at a stable path cannot invalidate record
        // ownership, function references, typed bindings, or the call DAG.
        // Artifact metadata is not referenced by another canonical table.
        return !std::is_same_v<Type, PutContent> &&
               !std::is_same_v<Type, AttachArtifact> &&
               !std::is_same_v<Type, ReplaceArtifact> &&
               !std::is_same_v<Type, DetachArtifact>;
      },
      mutation);
}

bool accepts(ModelValueKind required, ModelValueKind produced) {
  return required == produced || (required == ModelValueKind::SketchPlane &&
                                  produced == ModelValueKind::Plane);
}

const ModelInputPort *findInput(const ModelFunctionContract &function,
                                ModelInputId id) {
  const auto found =
      std::ranges::find(function.inputs, id, &ModelInputPort::id);
  return found == function.inputs.end() ? nullptr : &*found;
}

const ModelOutputPort *findOutput(const ModelFunctionContract &function,
                                  ModelOutputId id) {
  const auto found =
      std::ranges::find(function.outputs, id, &ModelOutputPort::id);
  return found == function.outputs.end() ? nullptr : &*found;
}

} // namespace

Result<void> ProjectStateAccess::validate(const ProjectState &state) {
  for (const EngineeringRecord &record : state.records()) {
    if (record.value.kind == datumPlaneRecordKind) {
      if (!record.owner)
        return invalid("document.datum-plane.missing-component",
                       "datum plane must belong to a component definition");
      const EngineeringRecord *component =
          state.data_->records.find(*record.owner);
      if (!component || component->value.kind != componentDefinitionRecordKind)
        return invalid("document.datum-plane.invalid-component",
                       "datum plane owner must be a component definition");
      if (component->lifecycle != Lifecycle::Active)
        return invalid("document.datum-plane.inactive-component",
                       "datum plane component definition must be active");
    }
    if (record.owner && !state.data_->records.find(*record.owner))
      return invalid("document.record.missing-owner",
                     "record owner does not exist");
    std::optional<RecordId> owner = record.owner;
    std::size_t depth = 0;
    while (owner) {
      if (*owner == record.id)
        return invalid("document.record.ownership-cycle",
                       "record ownership contains a cycle");
      const EngineeringRecord *parent = state.data_->records.find(*owner);
      if (!parent)
        break;
      owner = parent->owner;
      if (++depth > state.recordCount())
        return invalid("document.record.ownership-cycle",
                       "record ownership contains a cycle");
    }
  }
  const std::vector<ModelFunctionContract> functions = state.functions();
  std::unordered_set<ModelInputId, TypedIdHash<ModelInputIdTag>> inputIds;
  std::unordered_set<ModelOutputId, TypedIdHash<ModelOutputIdTag>> outputIds;
  for (const ModelFunctionContract &function : functions) {
    if (!state.data_->content.find(function.module))
      return invalid("document.function.missing-source",
                     "model function source does not exist");
    for (const ModelInputPort &input : function.inputs)
      if (!inputIds.insert(input.id).second)
        return invalid("document.function.duplicate-input-identity",
                       "model input identity is not project-unique");
    for (const ModelOutputPort &output : function.outputs)
      if (!outputIds.insert(output.id).second)
        return invalid("document.function.duplicate-output-identity",
                       "model output identity is not project-unique");
  }

  const std::vector<ModelCall> calls = state.calls();
  std::unordered_map<ModelCallId, std::size_t, TypedIdHash<ModelCallIdTag>>
      callIndexes;
  callIndexes.reserve(calls.size());
  for (std::size_t index = 0; index < calls.size(); ++index)
    callIndexes.emplace(calls[index].id, index);
  constexpr std::size_t noDependency = std::numeric_limits<std::size_t>::max();
  struct DependencyEdge {
    std::size_t dependent;
    std::size_t next;
  };
  std::vector<std::size_t> firstDependent(calls.size(), noDependency);
  std::vector<DependencyEdge> edges;
  edges.reserve(calls.size());
  std::vector<std::size_t> dependencyCounts(calls.size());
  std::unordered_set<ModelBindingId, TypedIdHash<ModelBindingIdTag>> bindingIds;

  for (std::size_t index = 0; index < calls.size(); ++index) {
    const ModelCall &call = calls[index];
    const ModelFunctionContract *function =
        state.data_->functions.find(call.function);
    if (!function)
      return invalid("document.call.missing-function",
                     "model call function does not exist");
    if (call.bindings.size() != function->inputs.size())
      return invalid("document.call.incomplete-bindings",
                     "model call does not bind every function input");
    for (const ModelInputBinding &binding : call.bindings) {
      if (!bindingIds.insert(binding.id).second)
        return invalid("document.call.duplicate-binding-identity",
                       "model binding identity is not project-unique");
      const ModelInputPort *input = findInput(*function, binding.input);
      if (!input)
        return invalid("document.call.unknown-input",
                       "model call binds an unknown function input");
      auto compatible = std::visit(
          [&](const auto &bound) -> Result<void> {
            using Type = std::decay_t<decltype(bound)>;
            if constexpr (std::is_same_v<Type, Quantity<Length>>) {
              if (input->kind != ModelValueKind::Length)
                return invalid("document.call.binding-type",
                               "model call binding type is incompatible");
            } else if constexpr (std::is_same_v<Type, DatumPlaneReference>) {
              const EngineeringRecord *datum =
                  state.data_->records.find(bound.record);
              if (!datum || datum->value.kind != datumPlaneRecordKind)
                return invalid("document.call.invalid-datum",
                               "model call datum is missing or has wrong kind");
              if (input->kind != ModelValueKind::SketchPlane &&
                  input->kind != ModelValueKind::Plane)
                return invalid("document.call.binding-type",
                               "model call binding type is incompatible");
            } else {
              const auto producerIndex = callIndexes.find(bound.call);
              if (producerIndex == callIndexes.end())
                return invalid("document.call.missing-producer",
                               "model call producer does not exist");
              const ModelCall &producer = calls[producerIndex->second];
              const ModelFunctionContract *producerFunction =
                  state.data_->functions.find(producer.function);
              if (!producerFunction)
                return invalid("document.call.missing-function",
                               "model call function does not exist");
              const ModelOutputPort *output =
                  findOutput(*producerFunction, bound.output);
              if (!output)
                return invalid("document.call.missing-output",
                               "model call output does not exist");
              if (!accepts(input->kind, output->kind))
                return invalid("document.call.binding-type",
                               "model call binding type is incompatible");
              edges.push_back({index, firstDependent[producerIndex->second]});
              firstDependent[producerIndex->second] = edges.size() - 1;
              ++dependencyCounts[index];
            }
            return {};
          },
          binding.value);
      if (!compatible)
        return compatible;
    }
  }

  std::queue<std::size_t> ready;
  for (std::size_t index = 0; index < dependencyCounts.size(); ++index)
    if (dependencyCounts[index] == 0)
      ready.push(index);
  std::size_t visited = 0;
  while (!ready.empty()) {
    const std::size_t producer = ready.front();
    ready.pop();
    ++visited;
    for (std::size_t edge = firstDependent[producer]; edge != noDependency;
         edge = edges[edge].next)
      if (--dependencyCounts[edges[edge].dependent] == 0)
        ready.push(edges[edge].dependent);
  }
  if (visited != calls.size())
    return invalid("document.call.dependency-cycle",
                   "model call dependencies contain a cycle");
  return {};
}

Result<ProjectState>
ProjectStateAccess::apply(const ProjectState &base,
                          std::span<const Mutation> mutations) {
  auto data = std::make_shared<ProjectState::Data>(*base.data_);
  bool validateStructure = false;
  for (const Mutation &mutation : mutations)
    if (auto result = internal::apply(*data, mutation); !result)
      return std::unexpected(std::move(result.error()));
    else
      validateStructure =
          validateStructure || requiresStructuralValidation(mutation);
  auto root = projectRootDigest(data->project, data->schemaSet, data->content,
                                data->records, data->functions, data->calls,
                                data->artifacts);
  if (!root)
    return std::unexpected(std::move(root.error()));
  data->rootDigest = std::move(*root);
  ProjectState result{std::move(data)};
  if (validateStructure)
    if (auto validation = validate(result); !validation)
      return std::unexpected(std::move(validation.error()));
  return result;
}

} // namespace internal
} // namespace kearne::document
