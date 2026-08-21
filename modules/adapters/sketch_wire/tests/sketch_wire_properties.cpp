#include "definition_generator.hpp"

#include <kearne/adapters/sketch_wire.hpp>
#include <kearne/api/wire_validation.hpp>
#include <kearne/testkit/property.hpp>

#include <google/protobuf/unknown_field_set.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <variant>

namespace {

namespace adapters = kearne::adapters;
namespace sketch = kearne::sketch;
namespace test = kearne::adapters::test;
namespace wire = kearne::api::v1;

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

void verifyGeneratedRoundTrips() {
  static_assert(std::variant_size_v<sketch::Entity> == 9);
  static_assert(std::variant_size_v<sketch::Constraint> == 22);
  const auto profile = kearne::testkit::propertyProfile();
  kearne::testkit::checkProperty(
      "sketch wire round trip", profile,
      [](kearne::testkit::Random &random, std::uint64_t index) {
        sketch::Definition definition = test::completeDefinition(random.next());
        if (index % 2U == 0)
          std::ranges::reverse(definition.entities);
        if (index % 3U == 0)
          std::ranges::reverse(definition.constraints);

        wire::SketchDefinition encoded;
        auto written = adapters::writeSketchDefinition(definition, &encoded);
        if (!written)
          throw std::runtime_error("valid definition did not convert to wire: " +
                                   written.error().code + ": " +
                                   written.error().summary);
        require(encoded.entities_size() ==
                        static_cast<int>(definition.entities.size()) &&
                    encoded.objects_size() ==
                        static_cast<int>(definition.objects.size()) &&
                    encoded.constraints_size() ==
                        static_cast<int>(definition.constraints.size()),
                "source order or element counts changed in wire conversion");

        auto recovered = adapters::readSketchDefinition(encoded);
        require(recovered.has_value() && *recovered == definition,
                "wire definition did not round trip exactly");
        auto bytes = adapters::serializeSketchDefinition(definition);
        require(bytes.has_value(), "definition did not serialize");
        const auto span =
            std::as_bytes(std::span{bytes->data(), bytes->size()});
        auto parsed = adapters::parseSketchDefinition(span);
        require(parsed.has_value() && *parsed == definition,
                "serialized definition did not round trip exactly");
      });
}

void verifyFailClosed() {
  const sketch::Definition definition = test::completeDefinition();
  wire::SketchDefinition encoded;
  require(adapters::writeSketchDefinition(definition, &encoded).has_value(),
          "fixture did not convert");

  wire::SketchDefinition unknown = encoded;
  unknown.GetReflection()->MutableUnknownFields(&unknown)->AddLengthDelimited(
      1000, "future");
  require(!adapters::readSketchDefinition(unknown),
          "unknown root field was accepted");

  unknown = encoded;
  auto *point = unknown.mutable_entities(0)->mutable_point()->mutable_at();
  point->GetReflection()->MutableUnknownFields(point)->AddVarint(1000, 1);
  require(!adapters::readSketchDefinition(unknown),
          "unknown nested field was accepted");

  wire::SketchDefinition invalid = encoded;
  invalid.mutable_entities(0)->mutable_point()->mutable_at()->set_x(
      std::numeric_limits<double>::quiet_NaN());
  require(!adapters::readSketchDefinition(invalid),
          "non-finite coordinate was accepted");

  invalid = encoded;
  invalid.mutable_constraints(0)
      ->mutable_coincident()
      ->mutable_first()
      ->set_key(static_cast<wire::SketchPointKey>(999));
  require(!adapters::readSketchDefinition(invalid),
          "unknown point key was accepted");

  std::string bytes = encoded.SerializeAsString();
  bytes.push_back(static_cast<char>(0x80));
  const auto span = std::as_bytes(std::span{bytes.data(), bytes.size()});
  require(!adapters::parseSketchDefinition(span),
          "malformed trailing varint was accepted");

  require(!adapters::writeSketchDefinition(definition, nullptr),
          "null output pointer was accepted");
}

void verifyRegistryEnrollment() {
  const auto descriptors = kearne::api::registeredWireTypes();
  const auto contains = [&descriptors](const auto *wanted) {
    return std::ranges::find(descriptors, wanted) != descriptors.end();
  };
  require(contains(wire::SketchDefinition::descriptor()),
          "sketch definition is absent from the wire registry");
  require(contains(wire::SketchEntity::descriptor()) &&
              contains(wire::SketchConstraint::descriptor()) &&
              contains(wire::SketchObject::descriptor()) &&
              contains(wire::SketchObjectMember::descriptor()),
          "sketch executable members are absent from the wire registry");
}

} // namespace

int main() {
  verifyRegistryEnrollment();
  verifyGeneratedRoundTrips();
  verifyFailClosed();
  return 0;
}
