#include <kearne/document/content_store.hpp>
#include <kearne/document/content_store_access.hpp>
#include <kearne/document/project_state_access.hpp>
#include <kearne/testkit/property.hpp>

#include <algorithm>
#include <array>
#include <barrier>
#include <bit>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace kearne;
using namespace kearne::document;

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

template <typename Value>
Value id(std::uint64_t timestamp, std::uint64_t randomValue) {
  typename Value::RandomTail tail{};
  for (std::size_t index = 0; index < tail.size(); ++index)
    tail[index] = static_cast<std::uint8_t>(randomValue >> ((index % 8) * 8));
  auto result = Value::create(timestamp, tail);
  require(result.has_value(), "test identifier could not be created");
  return std::move(*result);
}

template <typename Value> Value digest(std::uint8_t fill) {
  typename Value::Bytes bytes{};
  bytes.fill(fill);
  auto result = Value::fromBytes("blake3", bytes);
  require(result.has_value(), "test digest could not be created");
  return std::move(*result);
}

EngineeringRecord record(RecordId recordId, ActorId actor, std::uint64_t value,
                         std::optional<RecordId> owner = std::nullopt) {
  Bytes payload(8);
  for (std::size_t index = 0; index < payload.size(); ++index)
    payload[index] = static_cast<std::uint8_t>(value >> (index * 8));
  return {recordId,
          std::move(owner),
          Lifecycle::Active,
          {"test.generated", 1, std::move(payload)},
          {actor, Origin::System, std::nullopt, 1}};
}

ProjectState emptyState() {
  auto state =
      ProjectState::create(id<ProjectId>(1, 1), digest<SchemaSetDigest>(1));
  require(state.has_value(), "empty project state could not be created");
  return std::move(*state);
}

ModelFunctionContract functionContract(ModelFunctionId functionId,
                                       const ProjectPath &module,
                                       std::string qualifiedName,
                                       std::vector<ModelInputPort> inputs,
                                       std::vector<ModelOutputPort> outputs) {
  return {functionId,
          module,
          std::move(qualifiedName),
          digest<EnvironmentDigest>(2),
          digest<CapabilityProfileDigest>(3),
          std::move(inputs),
          std::move(outputs),
          TopologyPublicationMode::BodyOnly};
}

EngineeringRecord componentDefinition(RecordId recordId, ActorId actor,
                                      Lifecycle lifecycle = Lifecycle::Active) {
  EngineeringRecord value = record(recordId, actor, 0);
  value.lifecycle = lifecycle;
  value.value.kind = componentDefinitionRecordKind;
  return value;
}

DatumPlanePayloadV1 defaultDatumPlanePayload() {
  return {{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 0.0, 1.0}};
}

EngineeringRecord
datumPlane(RecordId recordId, RecordId componentId, ActorId actor,
           DatumPlanePayloadV1 payload = defaultDatumPlanePayload()) {
  auto encoded = canonicalBytes(payload);
  require(encoded.has_value(), "test datum plane could not be encoded");
  EngineeringRecord value = record(recordId, actor, 0, componentId);
  value.value = {std::string{datumPlaneRecordKind},
                 datumPlanePayloadSchemaVersion, std::move(*encoded)};
  return value;
}

ContentEntry pythonSource(std::string_view source) {
  auto sourceDigest = contentDigest(
      {reinterpret_cast<const std::uint8_t *>(source.data()), source.size()});
  require(sourceDigest.has_value(), "source did not hash");
  return {*sourceDigest, source.size(), "text/x-python"};
}

struct CanonicalFixture {
  bool flag;
  std::uint64_t integer;
  double number;
  Bytes blob;
  std::string text;
  ModelCallId identifier;
  ContentDigest digest;
};

Result<void> decodeCanonicalFixture(std::span<const std::uint8_t> bytes,
                                    const CanonicalFixture &expected) {
  CanonicalReader reader(bytes);
  if (auto result = reader.header("generated-fixture", 1); !result)
    return result;
  auto flag = reader.boolean();
  if (!flag)
    return std::unexpected(flag.error());
  auto integer = reader.unsignedInteger();
  if (!integer)
    return std::unexpected(integer.error());
  auto number = reader.binary64();
  if (!number)
    return std::unexpected(number.error());
  auto blob = reader.bytes(expected.blob.size());
  if (!blob)
    return std::unexpected(blob.error());
  auto text = reader.text(expected.text.size());
  if (!text)
    return std::unexpected(text.error());
  auto identifier = reader.identifier<ModelCallIdTag>();
  if (!identifier)
    return std::unexpected(identifier.error());
  auto contentDigest = reader.digest<ContentDigestTag>();
  if (!contentDigest)
    return std::unexpected(contentDigest.error());
  if (*flag != expected.flag || *integer != expected.integer ||
      *number != expected.number || !std::ranges::equal(*blob, expected.blob) ||
      *text != expected.text || *identifier != expected.identifier ||
      *contentDigest != expected.digest)
    return std::unexpected(
        diagnostic("test.canonical.mismatch", "canonical value changed"));
  return reader.end();
}

void verifyCanonicalOrder() {
  const ActorId actor = id<ActorId>(1, 2);
  MutationBatch forward;
  for (std::uint64_t index = 0; index < 64; ++index)
    forward.emplace_back(
        CreateRecord{record(id<RecordId>(index + 2, index + 9), actor, index)});
  MutationBatch reverse = forward;
  std::ranges::reverse(reverse);
  const ProjectState base = emptyState();
  auto left = internal::ProjectStateAccess::apply(base, forward);
  auto right = internal::ProjectStateAccess::apply(base, reverse);
  require(left && right && left->rootDigest() == right->rootDigest(),
          "project root depends on mutation insertion order");
  require(base.recordCount() == 0, "published ancestor was mutated");
}

void verifyGeneratedHistory(const testkit::PropertyProfile &profile) {
  ProjectState state = emptyState();
  const ActorId actor = id<ActorId>(1, 3);
  std::array<std::optional<EngineeringRecord>, 64> reference;
  std::optional<ProjectState> retainedAncestor;

  testkit::checkProperty(
      "immutable document history", profile,
      [&](testkit::Random &random, std::uint64_t index) {
        const std::size_t slot = static_cast<std::size_t>(random.next() % 64);
        const RecordId recordId = id<RecordId>(slot + 10, slot + 100);
        const EngineeringRecord next = record(recordId, actor, random.next());
        Mutation mutation = [&]() -> Mutation {
          if (!reference[slot])
            return CreateRecord{next};
          auto expected = digestOf(*reference[slot]);
          require(expected.has_value(), "reference record did not hash");
          return ReplaceRecord{recordId, *expected, next};
        }();
        const ProjectState before = state;
        auto updated =
            internal::ProjectStateAccess::apply(state, {&mutation, 1});
        require(updated.has_value(), "valid generated mutation was rejected");
        state = std::move(*updated);
        reference[slot] = next;
        require(before.record(recordId) != state.record(recordId),
                "published record changed in place");
        require(state.recordCount() ==
                    static_cast<std::size_t>(std::ranges::count_if(
                        reference,
                        [](const auto &value) { return value.has_value(); })),
                "project state disagrees with reference model");

        if (index == 127)
          retainedAncestor = state;
        if (retainedAncestor && index > 127)
          require(retainedAncestor->recordCount() <= state.recordCount(),
                  "retained ancestor changed after publication");

        if (index % 97 == 0 && reference[slot]) {
          auto expected = digestOf(*reference[slot]);
          require(expected.has_value(), "reference record did not hash");
          ContentDigest::Bytes wrongBytes = expected->bytes();
          wrongBytes[0] ^= 0xffU;
          auto wrong = ContentDigest::fromBytes("blake3", wrongBytes);
          require(wrong.has_value(), "stale test digest could not be created");
          Mutation stale = ReplaceRecord{recordId, *wrong, next};
          auto rejected =
              internal::ProjectStateAccess::apply(state, {&stale, 1});
          require(!rejected &&
                      rejected.error().code == "document.mutation.stale",
                  "stale replacement was accepted");
        }
      });
}

void verifyAtomicGraphValidation() {
  const ProjectState base = emptyState();
  const ActorId actor = id<ActorId>(1, 4);
  const RecordId missing = id<RecordId>(2, 4);
  const RecordId child = id<RecordId>(3, 4);
  Mutation invalid = CreateRecord{record(child, actor, 1, missing)};
  auto rejected = internal::ProjectStateAccess::apply(base, {&invalid, 1});
  require(!rejected && base.recordCount() == 0,
          "invalid batch leaked staged state");

  const auto firstPath = ProjectPath::parse("models/bracket.py");
  const auto secondPath = ProjectPath::parse("models/moved-bracket.py");
  require(firstPath && secondPath, "valid project path was rejected");
  const std::string source = "def bracket(width):\n    return width\n";
  const ContentEntry entry = pythonSource(source);
  const ModelFunctionId functionId = id<ModelFunctionId>(4, 5);
  const ModelFunctionContract function = functionContract(
      functionId, *firstPath, "bracket",
      {{id<ModelInputId>(5, 1), "width", ModelValueKind::Length}},
      {{id<ModelOutputId>(6, 1), "sketch", ModelValueKind::Sketch}});
  MutationBatch create{PutContent{*firstPath, std::nullopt, entry},
                       CreateFunction{function}};
  auto created = internal::ProjectStateAccess::apply(base, create);
  require(created && created->function(functionId),
          "atomic source and function creation failed");

  Mutation moveOnly = MoveContent{*firstPath, *secondPath, entry.digest};
  require(!internal::ProjectStateAccess::apply(*created, {&moveOnly, 1}),
          "source move left a dangling function contract");
  auto priorFunction = digestOf(function);
  require(priorFunction.has_value(), "function contract did not hash");
  ModelFunctionContract moved = function;
  moved.module = *secondPath;
  MutationBatch moveAndRetarget{
      moveOnly, ReplaceFunction{functionId, *priorFunction, moved}};
  auto movedState =
      internal::ProjectStateAccess::apply(*created, moveAndRetarget);
  require(movedState && movedState->function(functionId)->module == *secondPath,
          "atomic source move and contract retarget failed");
}

void verifyMutationCodec(const testkit::PropertyProfile &profile) {
  const auto firstPath = ProjectPath::parse("models/generated.py");
  const auto secondPath = ProjectPath::parse("models/renamed.py");
  require(firstPath && secondPath, "mutation codec paths were rejected");
  testkit::checkProperty(
      "mutation codec round trip", profile,
      [&](testkit::Random &random, std::uint64_t index) {
        const std::uint64_t salt = random.next();
        const ActorId actor = id<ActorId>(index + 2'000U, salt);
        const RecordId recordId = id<RecordId>(index + 3'000U, salt + 1U);
        EngineeringRecord generated = record(recordId, actor, salt);
        generated.provenance.origin =
            static_cast<Origin>(1U + random.next() % 7U);
        generated.provenance.request = id<RequestId>(index + 4'000U, salt + 2U);
        const ModelInputId inputId =
            id<ModelInputId>(index + 5'000U, salt + 3U);
        const ModelFunctionId functionId =
            id<ModelFunctionId>(index + 6'000U, salt + 4U);
        const ModelFunctionContract function =
            functionContract(functionId, *firstPath, "generated.part",
                             {{inputId, "length", ModelValueKind::Length}},
                             {{id<ModelOutputId>(index + 7'000U, salt + 5U),
                               "result", ModelValueKind::Sketch}});
        auto length = Quantity<Length>::fromSi(random.between(
            -maxDatumPlaneOriginMetres, maxDatumPlaneOriginMetres));
        require(length.has_value(), "generated mutation length was rejected");
        const ModelCallId callId = id<ModelCallId>(index + 8'000U, salt + 6U);
        const ModelCall call{callId,
                             functionId,
                             {{id<ModelBindingId>(index + 9'000U, salt + 7U),
                               inputId, *length}}};
        const ArtifactId artifactId =
            id<ArtifactId>(index + 10'000U, salt + 8U);
        const ArtifactMetadata artifact{
            artifactId,
            digest<ArtifactDigest>(static_cast<std::uint8_t>(salt)),
            random.next(),
            "application/vnd.kearne.generated",
            false,
            std::nullopt,
            std::nullopt};
        const ContentEntry content{
            digest<ContentDigest>(static_cast<std::uint8_t>(salt + 1U)),
            random.next(), "text/x-python"};
        const ContentDigest expected =
            digest<ContentDigest>(static_cast<std::uint8_t>(salt + 2U));
        const Mutation mutation = [&]() -> Mutation {
          switch (index % 15U) {
          case 0:
            return PutContent{*firstPath, std::nullopt, content};
          case 1:
            return MoveContent{*firstPath, *secondPath, expected};
          case 2:
            return DeleteContent{*firstPath, expected};
          case 3:
            return CreateRecord{generated};
          case 4:
            return ReplaceRecord{recordId, expected, generated};
          case 5:
            return DeleteRecord{recordId, expected};
          case 6:
            return CreateFunction{function};
          case 7:
            return ReplaceFunction{functionId, expected, function};
          case 8:
            return DeleteFunction{functionId, expected};
          case 9:
            return CreateCall{call};
          case 10:
            return ReplaceCall{callId, expected, call};
          case 11:
            return DeleteCall{callId, expected};
          case 12:
            return AttachArtifact{artifact};
          case 13:
            return ReplaceArtifact{artifactId, expected, artifact};
          default:
            return DetachArtifact{artifactId, expected};
          }
        }();
        auto encoded = canonicalBytes(mutation);
        require(encoded.has_value(), "generated mutation did not encode");
        auto decoded = decodeMutation(*encoded);
        require(decoded.has_value(), "generated mutation did not decode");
        auto reencoded = canonicalBytes(*decoded);
        require(reencoded && *reencoded == *encoded,
                "mutation codec changed canonical bytes");

        const std::size_t cut =
            static_cast<std::size_t>(random.next() % encoded->size());
        require(!decodeMutation({encoded->data(), cut}),
                "truncated mutation was accepted");
        require(!decodeMutation(*encoded, encoded->size() - 1U),
                "mutation byte limit was ignored");
        Bytes trailing = *encoded;
        trailing.push_back(0);
        require(!decodeMutation(trailing),
                "mutation trailing bytes were accepted");
      });
}

void verifyCanonicalReader(const testkit::PropertyProfile &profile) {
  testkit::checkProperty(
      "bounded canonical reader faults", profile,
      [](testkit::Random &random, std::uint64_t index) {
        CanonicalFixture fixture{
            (random.next() & 1U) != 0,
            random.next(),
            index % 19U == 0U ? -0.0 : random.between(-1.0e12, 1.0e12),
            Bytes(static_cast<std::size_t>(index % 129U)),
            std::string(static_cast<std::size_t>(index % 65U), 'a'),
            id<ModelCallId>(index + 1'000U, random.next()),
            digest<ContentDigest>(static_cast<std::uint8_t>(random.next()))};
        for (std::uint8_t &byte : fixture.blob)
          byte = static_cast<std::uint8_t>(random.next());
        for (char &character : fixture.text)
          character = static_cast<char>('a' + random.next() % 26U);

        CanonicalWriter writer;
        writer.header("generated-fixture", 1);
        writer.boolean(fixture.flag);
        writer.unsignedInteger(fixture.integer);
        require(writer.binary64(fixture.number).has_value(),
                "generated finite number was rejected");
        writer.bytes(fixture.blob);
        require(writer.text(fixture.text).has_value(),
                "generated ASCII text was rejected");
        writer.identifier(fixture.identifier);
        writer.digest(fixture.digest);
        const Bytes encoded = std::move(writer).take();
        require(decodeCanonicalFixture(encoded, fixture).has_value(),
                "canonical fixture did not round trip");

        const std::size_t cut =
            static_cast<std::size_t>(random.next() % encoded.size());
        require(!decodeCanonicalFixture({encoded.data(), cut}, fixture),
                "truncated canonical input was accepted");

        Bytes trailing = encoded;
        trailing.push_back(static_cast<std::uint8_t>(random.next()));
        require(!decodeCanonicalFixture(trailing, fixture),
                "canonical trailing bytes were accepted");

        Bytes wrongMagic = encoded;
        wrongMagic[1U + random.next() % 4U] ^= 0xffU;
        require(!decodeCanonicalFixture(wrongMagic, fixture),
                "wrong canonical header was accepted");

        CanonicalWriter wrongVersion;
        wrongVersion.header("generated-fixture", 2);
        CanonicalReader versionReader(wrongVersion.value());
        require(!versionReader.header("generated-fixture", 1),
                "wrong canonical version was accepted");

        const std::uint8_t shortValue =
            static_cast<std::uint8_t>(random.next() & 0x7fU);
        const Bytes overlongLength{
            static_cast<std::uint8_t>(shortValue | 0x80U), 0};
        CanonicalReader overlongReader(overlongLength);
        require(!overlongReader.bytes(128),
                "overlong canonical length was accepted");

        CanonicalWriter boundedWriter;
        const Bytes boundedValue(
            static_cast<std::size_t>(random.next() % 64U) + 1U, 0);
        boundedWriter.bytes(boundedValue);
        CanonicalReader boundedReader(boundedWriter.value());
        require(!boundedReader.bytes(boundedValue.size() - 1U),
                "canonical byte bound was not enforced");

        const Bytes invalidBool{
            static_cast<std::uint8_t>(2U + random.next() % 254U)};
        CanonicalReader boolReader(invalidBool);
        require(!boolReader.boolean(),
                "invalid canonical boolean was accepted");

        std::uint64_t invalidBits =
            index % 2U == 0U ? 0x8000000000000000ULL
                             : 0x7ff0000000000000ULL |
                                   (random.next() & 0x000fffffffffffffULL);
        Bytes invalidNumber(8);
        for (std::size_t byte = 0; byte < invalidNumber.size(); ++byte)
          invalidNumber[byte] = static_cast<std::uint8_t>(
              invalidBits >> ((invalidNumber.size() - byte - 1U) * 8U));
        CanonicalReader numberReader(invalidNumber);
        require(!numberReader.binary64(),
                "noncanonical binary64 value was accepted");

        Bytes overflow(10, 0x80U);
        overflow.back() = static_cast<std::uint8_t>(2U + random.next() % 126U);
        CanonicalReader overflowReader(overflow);
        require(!overflowReader.unsignedInteger(),
                "overflowing canonical integer was accepted");
      });
}

void verifyCanonicalModelValues(const testkit::PropertyProfile &profile) {
  static_assert(!std::same_as<ModelInputId, ModelOutputId>);
  static_assert(!std::same_as<ModelBindingId, ModelInputId>);
  const ModelInputId inputIdentity = id<ModelInputId>(20, 9);
  const ModelOutputId outputIdentity = id<ModelOutputId>(20, 9);
  require(inputIdentity.bytes() == outputIdentity.bytes(),
          "typed UUID test did not share raw bytes");

  CanonicalWriter positiveZero;
  CanonicalWriter negativeZero;
  require(positiveZero.binary64(0.0) && negativeZero.binary64(-0.0) &&
              positiveZero.value() == negativeZero.value() &&
              positiveZero.value() == Bytes(8, 0),
          "canonical binary64 did not normalize negative zero");
  CanonicalWriter one;
  require(one.binary64(1.0) &&
              one.value() == Bytes{0x3fU, 0xf0U, 0, 0, 0, 0, 0, 0},
          "canonical binary64 is not IEEE big-endian");
  CanonicalWriter nonFinite;
  require(!nonFinite.binary64(std::numeric_limits<double>::infinity()) &&
              nonFinite.value().empty(),
          "canonical binary64 accepted a non-finite value");
  const auto negativeQuantity = Quantity<Length>::fromSi(-0.0);
  require(negativeQuantity && !std::signbit(negativeQuantity->si()),
          "SI quantity retained negative zero");

  const auto module = ProjectPath::parse("models/generated.py");
  require(module.has_value(), "generated module path was rejected");
  testkit::checkProperty(
      "model declaration reorder metamorphism", profile,
      [&](testkit::Random &random, std::uint64_t index) {
        const std::size_t count = static_cast<std::size_t>(index % 16) + 1;
        std::vector<ModelInputPort> inputs;
        std::vector<ModelOutputPort> outputs;
        std::vector<ModelInputBinding> bindings;
        inputs.reserve(count);
        bindings.reserve(count);
        for (std::size_t port = 0; port < count; ++port) {
          const ModelInputId input =
              id<ModelInputId>(100 + port, index + port + 1);
          inputs.push_back(
              {input, "input_" + std::to_string(port), ModelValueKind::Length});
          auto length = Quantity<Length>::fromSi(random.between(-1.0e6, 1.0e6));
          require(length.has_value(), "finite generated length was rejected");
          bindings.push_back({id<ModelBindingId>(200 + port, index + port + 2),
                              input, *length});
        }
        const std::size_t outputCount = static_cast<std::size_t>(index % 8) + 1;
        outputs.reserve(outputCount);
        for (std::size_t port = 0; port < outputCount; ++port)
          outputs.push_back({id<ModelOutputId>(300 + port, index + port + 3),
                             "output_" + std::to_string(port),
                             ModelValueKind::Sketch});
        ModelFunctionContract forward =
            functionContract(id<ModelFunctionId>(400, index + 4), *module,
                             "generated.part", inputs, outputs);
        forward.topologyPublication =
            static_cast<TopologyPublicationMode>(1U + random.next() % 3U);
        ModelFunctionContract reverse = forward;
        std::ranges::reverse(reverse.inputs);
        std::ranges::reverse(reverse.outputs);
        auto forwardDigest = digestOf(forward);
        auto reverseDigest = digestOf(reverse);
        require(forwardDigest && reverseDigest &&
                    *forwardDigest == *reverseDigest,
                "function digest depends on port order");
        ModelFunctionContract otherPublication = forward;
        otherPublication.topologyPublication =
            static_cast<TopologyPublicationMode>(
                1U +
                static_cast<std::uint8_t>(forward.topologyPublication) % 3U);
        auto otherPublicationDigest = digestOf(otherPublication);
        require(otherPublicationDigest &&
                    *otherPublicationDigest != *forwardDigest,
                "function digest omits topology publication mode");
        ModelFunctionContract invalidPublication = forward;
        const auto invalidMode =
            static_cast<std::uint8_t>(4U + random.next() % 252U);
        invalidPublication.topologyPublication =
            std::bit_cast<TopologyPublicationMode>(invalidMode);
        require(!validate(invalidPublication),
                "invalid topology publication mode was accepted");

        ModelCall forwardCall{id<ModelCallId>(500, index + 5), forward.id,
                              bindings};
        ModelCall reverseCall = forwardCall;
        std::ranges::reverse(reverseCall.bindings);
        auto forwardCallDigest = digestOf(forwardCall);
        auto reverseCallDigest = digestOf(reverseCall);
        require(forwardCallDigest && reverseCallDigest &&
                    *forwardCallDigest == *reverseCallDigest,
                "call digest depends on binding order");
      });
}

void verifyDatumPlanePayloads(const testkit::PropertyProfile &profile) {
  testkit::checkProperty(
      "datum plane payload metamorphisms", profile,
      [](testkit::Random &random, std::uint64_t index) {
        constexpr double pi = 3.14159265358979323846;
        const auto normalize = [](std::array<double, 3> vector) {
          const double magnitude =
              std::sqrt(vector[0] * vector[0] + vector[1] * vector[1] +
                        vector[2] * vector[2]);
          for (double &coordinate : vector)
            coordinate /= magnitude;
          return vector;
        };
        const auto cross = [](const std::array<double, 3> &left,
                              const std::array<double, 3> &right) {
          return std::array{left[1] * right[2] - left[2] * right[1],
                            left[2] * right[0] - left[0] * right[2],
                            left[0] * right[1] - left[1] * right[0]};
        };
        std::array<double, 3> rawNormal{random.between(-1.0, 1.0),
                                        random.between(-1.0, 1.0),
                                        random.between(-1.0, 1.0)};
        if (std::abs(rawNormal[0]) + std::abs(rawNormal[1]) +
                std::abs(rawNormal[2]) <
            1.0e-12)
          rawNormal = {0.0, 0.0, 1.0};
        const std::array<double, 3> normal = normalize(rawNormal);
        std::array<double, 3> auxiliary{};
        const std::array absolute{std::abs(normal[0]), std::abs(normal[1]),
                                  std::abs(normal[2])};
        const auto leastAligned = std::ranges::min_element(absolute);
        auxiliary[static_cast<std::size_t>(leastAligned - absolute.begin())] =
            1.0;
        const std::array<double, 3> firstAxis =
            normalize(cross(auxiliary, normal));
        const std::array<double, 3> secondAxis = cross(normal, firstAxis);
        const double angle = random.between(-pi, pi);
        const std::array<double, 3> xDirection{
            std::cos(angle) * firstAxis[0] + std::sin(angle) * secondAxis[0],
            std::cos(angle) * firstAxis[1] + std::sin(angle) * secondAxis[1],
            std::cos(angle) * firstAxis[2] + std::sin(angle) * secondAxis[2]};
        DatumPlanePayloadV1 payload{{random.between(-maxDatumPlaneOriginMetres,
                                                    maxDatumPlaneOriginMetres),
                                     random.between(-maxDatumPlaneOriginMetres,
                                                    maxDatumPlaneOriginMetres),
                                     random.between(-maxDatumPlaneOriginMetres,
                                                    maxDatumPlaneOriginMetres)},
                                    xDirection,
                                    normal};
        auto encoded = canonicalBytes(payload);
        require(encoded.has_value(), "generated datum plane was rejected");
        auto decoded = decodeDatumPlanePayloadV1(*encoded);
        require(decoded && *decoded == payload,
                "datum plane payload did not round trip");
        auto reencoded = canonicalBytes(*decoded);
        require(reencoded && *reencoded == *encoded,
                "datum plane canonical encoding was not deterministic");

        DatumPlanePayloadV1 positiveZero = payload;
        const std::size_t zeroCoordinate =
            static_cast<std::size_t>(random.next() % 3U);
        positiveZero.originMetres[zeroCoordinate] = 0.0;
        DatumPlanePayloadV1 negativeZero = positiveZero;
        negativeZero.originMetres[zeroCoordinate] = -0.0;
        auto positiveBytes = canonicalBytes(positiveZero);
        auto negativeBytes = canonicalBytes(negativeZero);
        require(positiveBytes && negativeBytes &&
                    *positiveBytes == *negativeBytes,
                "datum plane encoding retained negative zero");

        CanonicalWriter prefix;
        prefix.header("datum-plane", datumPlanePayloadSchemaVersion);
        Bytes noncanonicalZero = *positiveBytes;
        const std::size_t zeroOffset =
            prefix.value().size() + zeroCoordinate * sizeof(double);
        std::fill_n(noncanonicalZero.begin() +
                        static_cast<std::ptrdiff_t>(zeroOffset),
                    sizeof(double), 0);
        noncanonicalZero[zeroOffset] = 0x80U;
        require(!decodeDatumPlanePayloadV1(noncanonicalZero),
                "noncanonical datum plane negative zero was accepted");

        const std::size_t cut =
            static_cast<std::size_t>(random.next() % encoded->size());
        EngineeringRecord malformed =
            datumPlane(id<RecordId>(index + 2'000U, random.next()),
                       id<RecordId>(index + 3'000U, random.next()),
                       id<ActorId>(index + 4'000U, random.next()), payload);
        malformed.value.bytes.resize(cut);
        require(!validate(malformed),
                "truncated known datum plane record was accepted");

        DatumPlanePayloadV1 invalid = payload;
        std::string_view expectedCode;
        switch (index % 7U) {
        case 0:
          invalid.originMetres[random.next() % 3U] =
              std::numeric_limits<double>::infinity();
          expectedCode = "document.datum-plane.origin-non-finite";
          break;
        case 1:
          invalid.xDirection[random.next() % 3U] =
              std::numeric_limits<double>::quiet_NaN();
          expectedCode = "document.datum-plane.x-direction-non-finite";
          break;
        case 2:
          invalid.normalDirection[random.next() % 3U] =
              -std::numeric_limits<double>::infinity();
          expectedCode = "document.datum-plane.normal-direction-non-finite";
          break;
        case 3:
          invalid.originMetres[random.next() % 3U] =
              maxDatumPlaneOriginMetres + random.between(1.0, 1.0e6);
          expectedCode = "document.datum-plane.origin-range";
          break;
        case 4:
          invalid.xDirection = {2.0, 0.0, 0.0};
          expectedCode = "document.datum-plane.x-direction-non-unit";
          break;
        case 5:
          invalid.normalDirection = {0.0, 0.0, 2.0};
          expectedCode = "document.datum-plane.normal-direction-non-unit";
          break;
        default:
          invalid.normalDirection = invalid.xDirection;
          expectedCode = "document.datum-plane.non-orthogonal";
          break;
        }
        auto rejected = validate(invalid);
        require(!rejected && rejected.error().code == expectedCode,
                "invalid datum plane did not identify the broken field");

        EngineeringRecord first =
            datumPlane(id<RecordId>(index + 5'000U, random.next()),
                       id<RecordId>(index + 6'000U, random.next()),
                       id<ActorId>(index + 7'000U, random.next()), payload);
        EngineeringRecord second = first;
        second.value.bytes = std::move(*reencoded);
        auto firstDigest = digestOf(first);
        auto secondDigest = digestOf(second);
        require(firstDigest && secondDigest && *firstDigest == *secondDigest,
                "equivalent datum planes produced different digests");

        EngineeringRecord unknown =
            record(id<RecordId>(index + 8'000U, random.next()),
                   id<ActorId>(index + 9'000U, random.next()), random.next());
        for (std::uint8_t &byte : unknown.value.bytes)
          byte = static_cast<std::uint8_t>(random.next());
        const Bytes preserved = unknown.value.bytes;
        require(validate(unknown) && digestOf(unknown) &&
                    unknown.value.bytes == preserved,
                "unknown versioned payload was not byte-preserved");
      });
}

void verifyDatumPlaneOwnership(const testkit::PropertyProfile &profile) {
  testkit::PropertyProfile graphProfile = profile;
  if (!graphProfile.replay)
    graphProfile.iterations =
        std::max((profile.iterations + 127U) / 128U, profile.shardCount * 64U);
  testkit::checkProperty(
      "datum plane ownership graph", graphProfile,
      [](testkit::Random &random, std::uint64_t index) {
        const ProjectState base = emptyState();
        const ActorId actor = id<ActorId>(index + 10'000U, random.next());
        const RecordId componentId =
            id<RecordId>(index + 11'000U, random.next());
        const RecordId datumId = id<RecordId>(index + 12'000U, random.next());
        const EngineeringRecord component =
            componentDefinition(componentId, actor);
        const EngineeringRecord plane = datumPlane(datumId, componentId, actor);
        MutationBatch forward{CreateRecord{component}, CreateRecord{plane}};
        MutationBatch reverse = forward;
        std::ranges::reverse(reverse);
        auto left = internal::ProjectStateAccess::apply(base, forward);
        auto right = internal::ProjectStateAccess::apply(base, reverse);
        require(left && right && left->rootDigest() == right->rootDigest(),
                "datum plane graph depends on mutation order");

        EngineeringRecord invalidPlane = plane;
        MutationBatch invalidBatch;
        std::string_view expectedCode;
        switch (index % 4U) {
        case 0:
          invalidPlane.owner.reset();
          invalidBatch.emplace_back(CreateRecord{invalidPlane});
          expectedCode = "document.datum-plane.missing-component";
          break;
        case 1:
          invalidPlane.owner = id<RecordId>(index + 13'000U, random.next());
          invalidBatch.emplace_back(CreateRecord{invalidPlane});
          expectedCode = "document.datum-plane.invalid-component";
          break;
        case 2: {
          EngineeringRecord wrongOwner = component;
          wrongOwner.value.kind = "test.not-a-component";
          invalidBatch.emplace_back(CreateRecord{wrongOwner});
          invalidBatch.emplace_back(CreateRecord{invalidPlane});
          expectedCode = "document.datum-plane.invalid-component";
          break;
        }
        default:
          invalidBatch.emplace_back(CreateRecord{
              componentDefinition(componentId, actor, Lifecycle::Suppressed)});
          invalidBatch.emplace_back(CreateRecord{invalidPlane});
          expectedCode = "document.datum-plane.inactive-component";
          break;
        }
        auto rejected = internal::ProjectStateAccess::apply(base, invalidBatch);
        require(!rejected && rejected.error().code == expectedCode,
                "invalid datum plane owner was accepted");
      });
}

void verifyTypedCallGraph() {
  const ProjectState empty = emptyState();
  const auto module = ProjectPath::parse("models/graph.py");
  require(module.has_value(), "graph module path was rejected");
  const ActorId actor = id<ActorId>(30, 1);
  const RecordId componentId = id<RecordId>(30, 2);
  const RecordId datumId = id<RecordId>(31, 1);
  const RecordId wrongDatumId = id<RecordId>(32, 1);
  const ModelFunctionContract plane = functionContract(
      id<ModelFunctionId>(33, 1), *module, "make_plane", {},
      {{id<ModelOutputId>(34, 1), "plane", ModelValueKind::Plane}});
  const ModelInputId widthInput = id<ModelInputId>(35, 1);
  const ModelInputId planeInput = id<ModelInputId>(36, 1);
  const ModelFunctionContract sketch = functionContract(
      id<ModelFunctionId>(37, 1), *module, "make_sketch",
      {{widthInput, "width", ModelValueKind::Length},
       {planeInput, "plane", ModelValueKind::SketchPlane}},
      {{id<ModelOutputId>(38, 1), "sketch", ModelValueKind::Sketch}});
  const ModelInputId priorSketchInput = id<ModelInputId>(39, 1);
  const ModelOutputId nextSketchOutput = id<ModelOutputId>(40, 1);
  const ModelFunctionContract transform =
      functionContract(id<ModelFunctionId>(41, 1), *module, "transform_sketch",
                       {{priorSketchInput, "sketch", ModelValueKind::Sketch}},
                       {{nextSketchOutput, "result", ModelValueKind::Sketch}});
  const ContentEntry source = pythonSource("# generated graph\n");
  MutationBatch foundation{
      PutContent{*module, std::nullopt, source},
      CreateRecord{componentDefinition(componentId, actor)},
      CreateRecord{datumPlane(datumId, componentId, actor)},
      CreateRecord{record(wrongDatumId, actor, 0, componentId)},
      CreateFunction{plane},
      CreateFunction{sketch},
      CreateFunction{transform}};
  auto ready = internal::ProjectStateAccess::apply(empty, foundation);
  require(ready.has_value(), "typed graph foundation was rejected");

  const auto width = Quantity<Length>::fromSi(0.025);
  require(width.has_value(), "valid SI length was rejected");
  const ModelCallId planeCallId = id<ModelCallId>(42, 1);
  const ModelCall planeCall{planeCallId, plane.id, {}};
  const ModelCall sketchCall{
      id<ModelCallId>(43, 1),
      sketch.id,
      {{id<ModelBindingId>(44, 1), widthInput, *width},
       {id<ModelBindingId>(45, 1), planeInput,
        NamedOutputReference{planeCallId, plane.outputs.front().id}}}};
  MutationBatch validGraph{CreateCall{sketchCall}, CreateCall{planeCall}};
  auto valid = internal::ProjectStateAccess::apply(*ready, validGraph);
  require(valid && valid->callCount() == 2,
          "valid forward-referenced call graph was rejected");

  const ModelCall datumCall{
      id<ModelCallId>(46, 1),
      sketch.id,
      {{id<ModelBindingId>(47, 1), widthInput, *width},
       {id<ModelBindingId>(48, 1), planeInput, DatumPlaneReference{datumId}}}};
  Mutation validDatum = CreateCall{datumCall};
  require(
      internal::ProjectStateAccess::apply(*ready, {&validDatum, 1}).has_value(),
      "valid datum-bound call was rejected");

  const auto rejectedCode = [&](ModelCall call, std::string_view code) {
    Mutation mutation = CreateCall{std::move(call)};
    auto result = internal::ProjectStateAccess::apply(*ready, {&mutation, 1});
    require(!result && result.error().code == code,
            "invalid call did not produce its stable diagnostic");
  };
  rejectedCode({id<ModelCallId>(50, 1),
                sketch.id,
                {{id<ModelBindingId>(51, 1), widthInput, *width}}},
               "document.call.incomplete-bindings");
  rejectedCode(
      {id<ModelCallId>(52, 1),
       sketch.id,
       {{id<ModelBindingId>(53, 1), widthInput, *width},
        {id<ModelBindingId>(53, 1), planeInput, DatumPlaneReference{datumId}}}},
      "document.call.duplicate-binding");
  rejectedCode({id<ModelCallId>(54, 1),
                sketch.id,
                {{id<ModelBindingId>(55, 1), widthInput, *width},
                 {id<ModelBindingId>(56, 1), widthInput, *width}}},
               "document.call.duplicate-input");
  rejectedCode(
      {id<ModelCallId>(57, 1),
       sketch.id,
       {{id<ModelBindingId>(58, 1), widthInput, DatumPlaneReference{datumId}},
        {id<ModelBindingId>(59, 1), planeInput, DatumPlaneReference{datumId}}}},
      "document.call.binding-type");
  rejectedCode({id<ModelCallId>(60, 1),
                sketch.id,
                {{id<ModelBindingId>(61, 1), widthInput, *width},
                 {id<ModelBindingId>(62, 1), planeInput,
                  DatumPlaneReference{wrongDatumId}}}},
               "document.call.invalid-datum");
  rejectedCode({id<ModelCallId>(63, 1),
                sketch.id,
                {{id<ModelBindingId>(64, 1), widthInput, *width},
                 {id<ModelBindingId>(65, 1), planeInput,
                  NamedOutputReference{id<ModelCallId>(66, 1),
                                       plane.outputs.front().id}}}},
               "document.call.missing-producer");
  const ModelCall missingOutput{
      id<ModelCallId>(67, 1),
      sketch.id,
      {{id<ModelBindingId>(68, 1), widthInput, *width},
       {id<ModelBindingId>(69, 1), planeInput,
        NamedOutputReference{planeCallId, id<ModelOutputId>(69, 2)}}}};
  MutationBatch missingOutputGraph{CreateCall{planeCall},
                                   CreateCall{missingOutput}};
  auto rejectedOutput =
      internal::ProjectStateAccess::apply(*ready, missingOutputGraph);
  require(!rejectedOutput &&
              rejectedOutput.error().code == "document.call.missing-output",
          "missing named output was accepted");

  const ModelCallId leftId = id<ModelCallId>(70, 1);
  const ModelCallId rightId = id<ModelCallId>(71, 1);
  const ModelCall left{leftId,
                       transform.id,
                       {{id<ModelBindingId>(72, 1), priorSketchInput,
                         NamedOutputReference{rightId, nextSketchOutput}}}};
  const ModelCall right{rightId,
                        transform.id,
                        {{id<ModelBindingId>(73, 1), priorSketchInput,
                          NamedOutputReference{leftId, nextSketchOutput}}}};
  MutationBatch cycle{CreateCall{left}, CreateCall{right}};
  auto rejectedCycle = internal::ProjectStateAccess::apply(*ready, cycle);
  require(!rejectedCycle &&
              rejectedCycle.error().code == "document.call.dependency-cycle",
          "cyclic call graph was accepted");

  ModelFunctionContract duplicate = sketch;
  duplicate.inputs[1].id = duplicate.inputs[0].id;
  auto duplicateResult = validate(duplicate);
  require(!duplicateResult && duplicateResult.error().code ==
                                  "document.function.duplicate-input",
          "duplicate stable input identity was accepted");

  ModelFunctionContract duplicateProjectInput = functionContract(
      id<ModelFunctionId>(74, 1), *module, "duplicate_project_input",
      {{widthInput, "other_width", ModelValueKind::Length}},
      {{id<ModelOutputId>(75, 1), "result", ModelValueKind::Sketch}});
  Mutation duplicatePort = CreateFunction{duplicateProjectInput};
  auto rejectedPort =
      internal::ProjectStateAccess::apply(*ready, {&duplicatePort, 1});
  require(!rejectedPort && rejectedPort.error().code ==
                               "document.function.duplicate-input-identity",
          "project-wide duplicate input identity was accepted");

  const ModelBindingId sharedBindingId = id<ModelBindingId>(76, 1);
  const ModelCall firstBindingOwner{
      id<ModelCallId>(77, 1),
      sketch.id,
      {{sharedBindingId, widthInput, *width},
       {id<ModelBindingId>(78, 1), planeInput, DatumPlaneReference{datumId}}}};
  const ModelCall secondBindingOwner{
      id<ModelCallId>(79, 1),
      sketch.id,
      {{sharedBindingId, widthInput, *width},
       {id<ModelBindingId>(80, 1), planeInput, DatumPlaneReference{datumId}}}};
  MutationBatch duplicateBindingGraph{CreateCall{firstBindingOwner},
                                      CreateCall{secondBindingOwner}};
  auto rejectedBinding =
      internal::ProjectStateAccess::apply(*ready, duplicateBindingGraph);
  require(!rejectedBinding && rejectedBinding.error().code ==
                                  "document.call.duplicate-binding-identity",
          "project-wide duplicate binding identity was accepted");
}

void verifyNonStructuralMutationClosure(
    const testkit::PropertyProfile &profile) {
  ProjectState state = emptyState();
  std::array<std::optional<ContentEntry>, 64> contentModel;
  std::array<std::optional<ArtifactMetadata>, 64> artifactModel;
  auto sourcePath = ProjectPath::parse("generated/source_0.py");
  require(sourcePath.has_value(), "fast-path source path was invalid");
  const ContentEntry initialSource{digest<ContentDigest>(6), 16,
                                   "text/x-python; charset=utf-8"};
  const ModelFunctionContract function = functionContract(
      id<ModelFunctionId>(900, 1), *sourcePath, "generated_function", {},
      {{id<ModelOutputId>(901, 1), "result", ModelValueKind::Sketch}});
  MutationBatch foundation{PutContent{*sourcePath, std::nullopt, initialSource},
                           CreateFunction{function}};
  auto initialized = internal::ProjectStateAccess::apply(state, foundation);
  require(initialized.has_value(), "fast-path foundation was rejected");
  state = std::move(*initialized);
  contentModel[0] = initialSource;
  testkit::checkProperty(
      "nonstructural mutation closure", profile,
      [&](testkit::Random &random, std::uint64_t index) {
        const std::size_t slot = static_cast<std::size_t>(random.next() % 64U);
        Mutation mutation = [&]() -> Mutation {
          if (index % 2U == 0U) {
            auto path = ProjectPath::parse("generated/source_" +
                                           std::to_string(slot) + ".py");
            require(path.has_value(), "generated source path was invalid");
            const ContentEntry next{
                digest<ContentDigest>(static_cast<std::uint8_t>(index + 7U)),
                index % 4096U, "text/x-python; charset=utf-8"};
            const std::optional<ContentDigest> prior =
                contentModel[slot] ? std::optional{contentModel[slot]->digest}
                                   : std::nullopt;
            contentModel[slot] = next;
            return PutContent{std::move(*path), prior, next};
          }

          const ArtifactId artifactId = id<ArtifactId>(slot + 1'000U, slot);
          if (!artifactModel[slot]) {
            ArtifactMetadata next{
                artifactId,
                digest<ArtifactDigest>(static_cast<std::uint8_t>(index + 11U)),
                index % 8192U,
                "application/vnd.kearne.generated",
                false,
                std::nullopt,
                std::nullopt};
            artifactModel[slot] = next;
            return AttachArtifact{std::move(next)};
          }
          auto expected = digestOf(*artifactModel[slot]);
          require(expected.has_value(), "generated artifact did not hash");
          if (index % 5U == 0U) {
            artifactModel[slot].reset();
            return DetachArtifact{artifactId, *expected};
          }
          ArtifactMetadata next = *artifactModel[slot];
          next.digest =
              digest<ArtifactDigest>(static_cast<std::uint8_t>(index + 13U));
          next.byteSize = index % 8192U;
          artifactModel[slot] = next;
          return ReplaceArtifact{artifactId, *expected, std::move(next)};
        }();

        auto updated =
            internal::ProjectStateAccess::apply(state, {&mutation, 1});
        require(updated && internal::ProjectStateAccess::validate(*updated),
                "nonstructural fast path violated a project invariant");
        state = std::move(*updated);
      });
}

void verifyCrossLanguageContentDigests() {
  const std::array vectors{
      std::pair{
          Bytes{},
          "blake3:"
          "6e82d967b887a378d96d00d3e8d8fc8c72247cdcb197b6ee6815a9af954f1e4d"},
      std::pair{
          Bytes{'c', 'a', 'f', 0xc3U, 0xa9U, '\r', '\n', 0xceU, 0x94U, ' ', '=',
                ' ', '1', '\r', '\n'},
          "blake3:"
          "c6f236c078234cfd0939564dab46692ffb0a937c586e5b32b1b544ffc66fec24"}};
  for (const auto &[bytes, expectedText] : vectors) {
    auto expected = ContentDigest::parse(expectedText);
    auto actual = contentDigest(bytes);
    require(expected && actual && *actual == *expected,
            "C++ content digest disagrees with the Python fixed vector");
  }
}

void verifyContentStore(const testkit::PropertyProfile &profile) {
  testkit::checkProperty(
      "digest addressed content store", profile,
      [&](testkit::Random &random, std::uint64_t index) {
        InMemoryContentStore store({4096, 4096});
        Bytes bytes(static_cast<std::size_t>(index % 4097));
        for (std::uint8_t &byte : bytes)
          byte = static_cast<std::uint8_t>(random.next());
        auto contentId = contentDigest(bytes);
        require(contentId.has_value(), "generated content did not hash");
        require(store.put(*contentId, bytes).has_value() &&
                    store.put(*contentId, bytes).has_value(),
                "valid or repeated content was rejected");
        auto recovered = store.get(*contentId);
        require(recovered && **recovered == bytes,
                "stored content did not round trip");
        require(store.size() == 1,
                "content store identity is not digest based");
      });

  InMemoryContentStore store({4096, 4096});
  const Bytes sharedBytes{'s', 'h', 'a', 'r', 'e', 'd'};
  auto sharedDigest = contentDigest(sharedBytes);
  require(sharedDigest.has_value(), "shared test content did not hash");
  std::array<Result<void>, 8> outcomes;
  std::vector<std::jthread> writers;
  writers.reserve(outcomes.size());
  for (std::size_t index = 0; index < outcomes.size(); ++index)
    writers.emplace_back([&, index] {
      outcomes[index] = store.put(*sharedDigest, sharedBytes);
    });
  writers.clear();
  require(std::ranges::all_of(
              outcomes, [](const auto &value) { return value.has_value(); }),
          "concurrent publication was not idempotent");

  ContentDigest::Bytes wrongBytes = sharedDigest->bytes();
  wrongBytes[0] ^= 0xffU;
  auto wrong = ContentDigest::fromBytes("blake3", wrongBytes);
  require(wrong.has_value(), "false content digest could not be created");
  auto rejected = store.put(*wrong, sharedBytes);
  require(!rejected &&
              rejected.error().code == "document.content.digest-mismatch",
          "content with a false digest was accepted");

  testkit::checkProperty(
      "content store capacity boundaries", profile,
      [](testkit::Random &random, std::uint64_t index) {
        const std::size_t maxBlob = static_cast<std::size_t>(index % 4096) + 1;
        const std::size_t remainder = static_cast<std::size_t>(
            random.next() % static_cast<std::uint64_t>(maxBlob + 1));
        InMemoryContentStore bounded({maxBlob, maxBlob + remainder});
        const auto generatedBytes = [&random](std::size_t size) {
          Bytes value(size);
          for (std::uint8_t &byte : value)
            byte = static_cast<std::uint8_t>(random.next());
          return value;
        };
        const Bytes first = generatedBytes(maxBlob);
        auto firstId = contentDigest(first);
        require(firstId && bounded.put(*firstId, first),
                "maximum-size blob was rejected");
        require(bounded.put(*firstId, first) && bounded.size() == 1 &&
                    bounded.byteSize() == first.size(),
                "duplicate content consumed capacity");

        const Bytes oversized = generatedBytes(maxBlob + 1);
        auto oversizedId = contentDigest(oversized);
        require(oversizedId.has_value(), "oversized content did not hash");
        auto oversizedResult = bounded.put(*oversizedId, oversized);
        require(!oversizedResult && oversizedResult.error().code ==
                                        "document.content.blob-too-large",
                "per-blob limit was not enforced");

        const Bytes second = generatedBytes(remainder);
        Bytes distinctSecond = second;
        if (distinctSecond == first && !distinctSecond.empty())
          distinctSecond[0] ^= 0xffU;
        auto secondId = contentDigest(distinctSecond);
        require(secondId && bounded.put(*secondId, distinctSecond) &&
                    bounded.byteSize() == maxBlob + remainder,
                "exact store capacity was rejected");
        std::uint8_t candidate = 0;
        while (
            (first.size() == 1 && first.front() == candidate) ||
            (distinctSecond.size() == 1 && distinctSecond.front() == candidate))
          ++candidate;
        Bytes extra{candidate};
        auto extraId = contentDigest(extra);
        require(extraId.has_value(), "capacity test content did not hash");
        auto full = bounded.put(*extraId, std::move(extra));
        require(!full &&
                    full.error().code == "document.content.capacity-exceeded",
                "total store capacity was not enforced");
      });

  InMemoryContentStore corrupted({16, 32});
  const Bytes expectedBytes{'s', 'a', 'f', 'e'};
  auto expectedId = contentDigest(expectedBytes);
  require(expectedId.has_value(), "collision test content did not hash");
  internal::ContentStoreFaultAccess::injectUnverified(
      corrupted, *expectedId, Bytes{'e', 'v', 'i', 'l'});
  auto collision = corrupted.put(*expectedId, expectedBytes);
  require(!collision && collision.error().severity == Severity::Fatal &&
              collision.error().code == "document.content.collision",
          "digest collision or corruption was not detected");

  InMemoryContentStore contested({8, 8});
  std::barrier rendezvous{2};
  std::array<Result<void>, 2> capacityOutcomes;
  const std::array contestedBytes{
      Bytes{'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a'},
      Bytes{'b', 'b', 'b', 'b', 'b', 'b', 'b', 'b'}};
  auto contestedLeft = contentDigest(contestedBytes[0]);
  auto contestedRight = contentDigest(contestedBytes[1]);
  require(contestedLeft && contestedRight,
          "contested content could not be hashed");
  std::array<ContentDigest, 2> contestedIds{*contestedLeft, *contestedRight};
  std::vector<std::jthread> contenders;
  for (std::size_t index = 0; index < capacityOutcomes.size(); ++index)
    contenders.emplace_back([&, index] {
      rendezvous.arrive_and_wait();
      capacityOutcomes[index] =
          contested.put(contestedIds[index], contestedBytes[index]);
    });
  contenders.clear();
  const std::size_t successes = static_cast<std::size_t>(std::ranges::count_if(
      capacityOutcomes, [](const auto &value) { return value.has_value(); }));
  require(successes == 1 && contested.size() == 1 && contested.byteSize() == 8,
          "concurrent writers exceeded store capacity");
}

} // namespace

int main() {
  try {
    const auto profile = kearne::testkit::propertyProfile();
    verifyCanonicalOrder();
    verifyGeneratedHistory(profile);
    verifyAtomicGraphValidation();
    verifyMutationCodec(profile);
    verifyCanonicalReader(profile);
    verifyCanonicalModelValues(profile);
    verifyDatumPlanePayloads(profile);
    verifyDatumPlaneOwnership(profile);
    verifyTypedCallGraph();
    verifyNonStructuralMutationClosure(profile);
    verifyCrossLanguageContentDigests();
    verifyContentStore(profile);
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
