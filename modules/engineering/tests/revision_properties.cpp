#include <kearne/engineering/revision.hpp>
#include <kearne/testkit/property.hpp>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using namespace kearne;
using namespace kearne::engineering;

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

template <typename Value>
Value id(std::uint64_t timestamp, std::uint64_t randomValue) {
  typename Value::RandomTail tail{};
  for (std::size_t index = 0; index < tail.size(); ++index)
    tail[index] = static_cast<std::uint8_t>(randomValue >> ((index % 8U) * 8U));
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

void verifyRevisionCodec(const testkit::PropertyProfile &profile) {
  testkit::checkProperty(
      "revision envelope round trip", profile,
      [](testkit::Random &random, std::uint64_t index) {
        const std::size_t parentCount = static_cast<std::size_t>(index % 3U);
        std::vector<RevisionId> parents;
        parents.reserve(parentCount);
        for (std::size_t parent = 0; parent < parentCount; ++parent)
          parents.push_back(digest<RevisionId>(static_cast<std::uint8_t>(
              random.next() + static_cast<std::uint64_t>(parent))));
        const bool requested = (random.next() & 1U) != 0;
        const bool permitted = (random.next() & 1U) != 0;
        const bool gestured = (random.next() & 1U) != 0;
        const ActorId actor = id<ActorId>(index + 1'000U, random.next());
        document::EngineeringRecord record{
            id<RecordId>(index + 2'000U, random.next()),
            std::nullopt,
            document::Lifecycle::Active,
            {"extension.generated", 1, {}},
            {actor, static_cast<Origin>(1U + random.next() % 7U), std::nullopt,
             random.next()}};
        record.value.bytes.resize(static_cast<std::size_t>(index % 1024U));
        for (std::uint8_t &byte : record.value.bytes)
          byte = static_cast<std::uint8_t>(random.next());
        std::vector<std::string> commandTypes;
        const std::size_t commandCount =
            static_cast<std::size_t>(index % 8U) + 1U;
        commandTypes.reserve(commandCount);
        for (std::size_t command = 0; command < commandCount; ++command)
          commandTypes.push_back("generated.command_" +
                                 std::to_string(command));
        RevisionEnvelope envelope{
            std::move(parents),
            id<TransactionId>(index + 3'000U, random.next()),
            requested
                ? std::optional{id<RequestId>(index + 4'000U, random.next())}
                : std::nullopt,
            requested ? std::optional{digest<ContentDigest>(
                            static_cast<std::uint8_t>(random.next()))}
                      : std::nullopt,
            actor,
            static_cast<Origin>(1U + random.next() % 7U),
            permitted ? std::optional{id<PermissionContextId>(index + 5'000U,
                                                              random.next())}
                      : std::nullopt,
            gestured
                ? std::optional{id<GestureId>(index + 6'000U, random.next())}
                : std::nullopt,
            digest<SchemaSetDigest>(static_cast<std::uint8_t>(random.next())),
            random.next(),
            digest<ContentDigest>(static_cast<std::uint8_t>(random.next())),
            std::move(commandTypes),
            {document::CreateRecord{std::move(record)}}};
        auto encoded = canonicalBytes(envelope);
        require(encoded.has_value(), "generated revision did not encode");
        auto decoded = decodeRevisionRecord(*encoded);
        require(decoded.has_value(), "generated revision did not decode");
        auto expectedId = revisionId(envelope);
        auto reencoded = canonicalBytes(decoded->envelope);
        require(expectedId && decoded->id == *expectedId && reencoded &&
                    *reencoded == *encoded,
                "revision codec changed identity or canonical bytes");

        const std::size_t cut =
            static_cast<std::size_t>(random.next() % encoded->size());
        require(!decodeRevisionRecord({encoded->data(), cut}),
                "truncated revision was accepted");
        RevisionDecodeLimits byteLimit;
        byteLimit.maximumEncodedBytes = encoded->size() - 1U;
        require(!decodeRevisionRecord(*encoded, byteLimit),
                "revision byte limit was ignored");
        RevisionDecodeLimits commandLimit;
        commandLimit.maximumCommandTypes = commandCount - 1U;
        require(!decodeRevisionRecord(*encoded, commandLimit),
                "revision command limit was ignored");
        RevisionDecodeLimits mutationLimit;
        mutationLimit.mutations.maximumMutations = 0;
        require(!decodeRevisionRecord(*encoded, mutationLimit),
                "revision mutation limits were ignored");
        document::Bytes trailing = *encoded;
        trailing.push_back(0);
        require(!decodeRevisionRecord(trailing),
                "revision trailing bytes were accepted");
      });

  RevisionEnvelope invalid{
      {},
      id<TransactionId>(1, 1),
      std::nullopt,
      std::nullopt,
      id<ActorId>(1, 2),
      static_cast<Origin>(0),
      std::nullopt,
      std::nullopt,
      digest<SchemaSetDigest>(1),
      1,
      digest<ContentDigest>(2),
      {"human words"},
      {document::DeleteRecord{id<RecordId>(1, 3), digest<ContentDigest>(3)}}};
  require(!canonicalBytes(invalid), "invalid revision vocabulary was encoded");
}

} // namespace

int main() {
  try {
    verifyRevisionCodec(kearne::testkit::propertyProfile());
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
