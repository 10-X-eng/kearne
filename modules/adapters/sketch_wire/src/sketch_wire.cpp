#include <kearne/adapters/sketch_wire.hpp>

#include <kearne/api/strong_types.hpp>
#include <kearne/api/wire_validation.hpp>

#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/unknown_field_set.h>

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace kearne::adapters {
namespace {

namespace protobuf = google::protobuf;
namespace wire = api::v1;

Result<void> invalid(std::string code, std::string summary) {
  return std::unexpected(diagnostic(std::move(code), std::move(summary)));
}

template <std::size_t Size>
bool hasExactFields(const protobuf::Descriptor &descriptor,
                    const std::array<int, Size> &numbers) {
  if (descriptor.field_count() != static_cast<int>(numbers.size()))
    return false;
  for (const int number : numbers)
    if (descriptor.FindFieldByNumber(number) == nullptr)
      return false;
  return true;
}

template <std::size_t Size>
bool hasExactFields(const protobuf::OneofDescriptor &descriptor,
                    const std::array<int, Size> &numbers) {
  if (descriptor.field_count() != static_cast<int>(numbers.size()))
    return false;
  for (const int number : numbers) {
    const protobuf::FieldDescriptor *field =
        descriptor.containing_type()->FindFieldByNumber(number);
    if (field == nullptr || field->containing_oneof() != &descriptor)
      return false;
  }
  return true;
}

std::optional<Diagnostic> schemaCoverageError() {
  static const std::optional<Diagnostic> result = []() {
    const bool complete =
        hasExactFields(*wire::SketchPoint2::descriptor(), std::array{1, 2}) &&
        hasExactFields(*wire::SketchPointGeometry::descriptor(),
                       std::array{1}) &&
        hasExactFields(*wire::SketchLineGeometry::descriptor(),
                       std::array{1, 2}) &&
        hasExactFields(*wire::SketchCircleGeometry::descriptor(),
                       std::array{1, 2}) &&
        hasExactFields(*wire::SketchArcGeometry::descriptor(),
                       std::array{1, 2, 3, 4}) &&
        hasExactFields(*wire::SketchEntity::descriptor(),
                       std::array{1, 2, 20, 21, 22, 23}) &&
        hasExactFields(*wire::SketchPointReference::descriptor(),
                       std::array{1, 2}) &&
        hasExactFields(*wire::SketchPointPairConstraint::descriptor(),
                       std::array{1, 2}) &&
        hasExactFields(*wire::SketchLineConstraint::descriptor(),
                       std::array{1}) &&
        hasExactFields(*wire::SketchEntityPairConstraint::descriptor(),
                       std::array{1, 2}) &&
        hasExactFields(*wire::SketchTangentConstraint::descriptor(),
                       std::array{1, 2, 3}) &&
        hasExactFields(*wire::SketchMidpointConstraint::descriptor(),
                       std::array{1, 2}) &&
        hasExactFields(*wire::SketchEntityConstraint::descriptor(),
                       std::array{1}) &&
        hasExactFields(*wire::SketchPointPairLengthConstraint::descriptor(),
                       std::array{1, 2, 3}) &&
        hasExactFields(*wire::SketchCurveLengthConstraint::descriptor(),
                       std::array{1, 2}) &&
        hasExactFields(*wire::SketchEntityPairAngleConstraint::descriptor(),
                       std::array{1, 2, 3}) &&
        hasExactFields(*wire::SketchConstraint::descriptor(),
                       std::array{1, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
                                  31, 32, 33, 34, 35, 36}) &&
        hasExactFields(*wire::SketchDefinition::descriptor(),
                       std::array{1, 2, 3});
    const protobuf::OneofDescriptor *geometry =
        wire::SketchEntity::descriptor()->FindOneofByName("geometry");
    const protobuf::OneofDescriptor *relation =
        wire::SketchConstraint::descriptor()->FindOneofByName("relation");
    if (complete && geometry != nullptr &&
        hasExactFields(*geometry, std::array{20, 21, 22, 23}) &&
        relation != nullptr &&
        hasExactFields(*relation, std::array{20, 21, 22, 23, 24, 25, 26, 27, 28,
                                             29, 30, 31, 32, 33, 34, 35, 36}))
      return std::optional<Diagnostic>{};
    return std::optional<Diagnostic>{diagnostic(
        "sketch.wire.conversion-registry-stale",
        "sketch wire schema changed without complete adapter conversion")};
  }();
  return result;
}

bool containsUnknownFields(const protobuf::Message &message) {
  const protobuf::Reflection &reflection = *message.GetReflection();
  if (reflection.GetUnknownFields(message).field_count() != 0)
    return true;
  const protobuf::Descriptor &descriptor = *message.GetDescriptor();
  for (int fieldIndex = 0; fieldIndex < descriptor.field_count();
       ++fieldIndex) {
    const protobuf::FieldDescriptor *field = descriptor.field(fieldIndex);
    if (field->cpp_type() != protobuf::FieldDescriptor::CPPTYPE_MESSAGE)
      continue;
    const int count = field->is_repeated()
                          ? reflection.FieldSize(message, field)
                          : (reflection.HasField(message, field) ? 1 : 0);
    for (int index = 0; index < count; ++index) {
      const protobuf::Message &nested =
          field->is_repeated()
              ? reflection.GetRepeatedMessage(message, field, index)
              : reflection.GetMessage(message, field);
      if (containsUnknownFields(nested))
        return true;
    }
  }
  return false;
}

Result<sketch::LengthValue> readLength(double value) {
  return sketch::LengthValue::fromSi(value);
}

Result<sketch::AngleValue> readAngle(double value) {
  return sketch::AngleValue::fromSi(value);
}

Result<sketch::Point2> readPoint(const wire::SketchPoint2 &value) {
  auto x = readLength(value.x());
  auto y = readLength(value.y());
  if (!x)
    return std::unexpected(std::move(x.error()));
  if (!y)
    return std::unexpected(std::move(y.error()));
  return sketch::Point2{*x, *y};
}

void writePoint(const sketch::Point2 &value, wire::SketchPoint2 *result) {
  result->set_x(value.x.si());
  result->set_y(value.y.si());
}

Result<sketch::PointKey> readPointKey(wire::SketchPointKey value) {
  switch (value) {
  case wire::SKETCH_POINT_KEY_POINT:
    return sketch::PointKey::Point;
  case wire::SKETCH_POINT_KEY_START:
    return sketch::PointKey::Start;
  case wire::SKETCH_POINT_KEY_END:
    return sketch::PointKey::End;
  case wire::SKETCH_POINT_KEY_CENTER:
    return sketch::PointKey::Center;
  default:
    return std::unexpected(diagnostic("sketch.wire.invalid-point-key",
                                      "wire point key is unsupported"));
  }
}

wire::SketchPointKey writePointKey(sketch::PointKey value) {
  switch (value) {
  case sketch::PointKey::Point:
    return wire::SKETCH_POINT_KEY_POINT;
  case sketch::PointKey::Start:
    return wire::SKETCH_POINT_KEY_START;
  case sketch::PointKey::End:
    return wire::SKETCH_POINT_KEY_END;
  case sketch::PointKey::Center:
    return wire::SKETCH_POINT_KEY_CENTER;
  }
  std::terminate();
}

Result<sketch::PointRef>
readPointReference(const wire::SketchPointReference &value) {
  auto entity = api::readId<SketchEntityId>(value.entity());
  auto key = readPointKey(value.key());
  if (!entity)
    return std::unexpected(std::move(entity.error()));
  if (!key)
    return std::unexpected(std::move(key.error()));
  return sketch::PointRef{*entity, *key};
}

void writePointReference(const sketch::PointRef &value,
                         wire::SketchPointReference *result) {
  api::writeId(value.entity, result->mutable_entity());
  result->set_key(writePointKey(value.key));
}

Result<SketchEntityId> readEntityId(const wire::UuidV7 &value) {
  return api::readId<SketchEntityId>(value);
}

struct EntityPair {
  SketchEntityId first;
  SketchEntityId second;
};

Result<EntityPair>
readEntityPair(const wire::SketchEntityPairConstraint &value) {
  auto first = readEntityId(value.first());
  auto second = readEntityId(value.second());
  if (!first)
    return std::unexpected(std::move(first.error()));
  if (!second)
    return std::unexpected(std::move(second.error()));
  return EntityPair{*first, *second};
}

template <typename Pair>
void writeEntityPair(SketchEntityId first, SketchEntityId second,
                     Pair *result) {
  api::writeId(first, result->mutable_first());
  api::writeId(second, result->mutable_second());
}

Result<sketch::Entity> readEntity(const wire::SketchEntity &value) {
  auto id = readEntityId(value.id());
  if (!id)
    return std::unexpected(std::move(id.error()));
  switch (value.geometry_case()) {
  case wire::SketchEntity::kPoint: {
    auto at = readPoint(value.point().at());
    if (!at)
      return std::unexpected(std::move(at.error()));
    return sketch::PointEntity{*id, *at, value.construction()};
  }
  case wire::SketchEntity::kLine: {
    auto start = readPoint(value.line().start());
    auto end = readPoint(value.line().end());
    if (!start)
      return std::unexpected(std::move(start.error()));
    if (!end)
      return std::unexpected(std::move(end.error()));
    return sketch::LineEntity{*id, *start, *end, value.construction()};
  }
  case wire::SketchEntity::kCircle: {
    auto center = readPoint(value.circle().center());
    auto radius = readLength(value.circle().radius());
    if (!center)
      return std::unexpected(std::move(center.error()));
    if (!radius)
      return std::unexpected(std::move(radius.error()));
    return sketch::CircleEntity{*id, *center, *radius, value.construction()};
  }
  case wire::SketchEntity::kArc: {
    auto center = readPoint(value.arc().center());
    auto radius = readLength(value.arc().radius());
    auto start = readAngle(value.arc().start_angle());
    auto end = readAngle(value.arc().end_angle());
    if (!center)
      return std::unexpected(std::move(center.error()));
    if (!radius)
      return std::unexpected(std::move(radius.error()));
    if (!start)
      return std::unexpected(std::move(start.error()));
    if (!end)
      return std::unexpected(std::move(end.error()));
    return sketch::ArcEntity{*id,    *center, *radius,
                             *start, *end,    value.construction()};
  }
  default:
    return std::unexpected(diagnostic("sketch.wire.unsupported-entity",
                                      "wire sketch entity is unsupported"));
  }
}

void writeEntity(const sketch::Entity &value, wire::SketchEntity *result) {
  std::visit(
      [&]<typename Entity>(const Entity &entity) {
        api::writeId(entity.id, result->mutable_id());
        result->set_construction(entity.construction);
        if constexpr (std::is_same_v<Entity, sketch::PointEntity>) {
          writePoint(entity.point, result->mutable_point()->mutable_at());
        } else if constexpr (std::is_same_v<Entity, sketch::LineEntity>) {
          writePoint(entity.start, result->mutable_line()->mutable_start());
          writePoint(entity.end, result->mutable_line()->mutable_end());
        } else if constexpr (std::is_same_v<Entity, sketch::CircleEntity>) {
          writePoint(entity.center, result->mutable_circle()->mutable_center());
          result->mutable_circle()->set_radius(entity.radius.si());
        } else {
          static_assert(std::is_same_v<Entity, sketch::ArcEntity>);
          writePoint(entity.center, result->mutable_arc()->mutable_center());
          result->mutable_arc()->set_radius(entity.radius.si());
          result->mutable_arc()->set_start_angle(entity.startAngle.si());
          result->mutable_arc()->set_end_angle(entity.endAngle.si());
        }
      },
      value);
}

Result<std::pair<sketch::PointRef, sketch::PointRef>>
readPointPair(const wire::SketchPointPairConstraint &value) {
  auto first = readPointReference(value.first());
  auto second = readPointReference(value.second());
  if (!first)
    return std::unexpected(std::move(first.error()));
  if (!second)
    return std::unexpected(std::move(second.error()));
  return std::pair{*first, *second};
}

Result<std::pair<sketch::PointRef, sketch::PointRef>>
readPointPair(const wire::SketchPointPairLengthConstraint &value) {
  auto first = readPointReference(value.first());
  auto second = readPointReference(value.second());
  if (!first)
    return std::unexpected(std::move(first.error()));
  if (!second)
    return std::unexpected(std::move(second.error()));
  return std::pair{*first, *second};
}

template <typename Pair>
void writePointPair(const sketch::PointRef &first,
                    const sketch::PointRef &second, Pair *result) {
  writePointReference(first, result->mutable_first());
  writePointReference(second, result->mutable_second());
}

Result<sketch::Constraint> readConstraint(const wire::SketchConstraint &value) {
  auto id = api::readId<SketchConstraintId>(value.id());
  if (!id)
    return std::unexpected(std::move(id.error()));
  switch (value.relation_case()) {
  case wire::SketchConstraint::kCoincident: {
    auto pair = readPointPair(value.coincident());
    if (!pair)
      return std::unexpected(std::move(pair.error()));
    return sketch::Coincident{*id, pair->first, pair->second};
  }
  case wire::SketchConstraint::kHorizontal: {
    auto line = readEntityId(value.horizontal().line());
    if (!line)
      return std::unexpected(std::move(line.error()));
    return sketch::Horizontal{*id, *line};
  }
  case wire::SketchConstraint::kVertical: {
    auto line = readEntityId(value.vertical().line());
    if (!line)
      return std::unexpected(std::move(line.error()));
    return sketch::Vertical{*id, *line};
  }
  case wire::SketchConstraint::kParallel:
  case wire::SketchConstraint::kPerpendicular:
  case wire::SketchConstraint::kConcentric:
  case wire::SketchConstraint::kEqual:
  case wire::SketchConstraint::kCollinear: {
    const wire::SketchEntityPairConstraint *payload = nullptr;
    switch (value.relation_case()) {
    case wire::SketchConstraint::kParallel:
      payload = &value.parallel();
      break;
    case wire::SketchConstraint::kPerpendicular:
      payload = &value.perpendicular();
      break;
    case wire::SketchConstraint::kConcentric:
      payload = &value.concentric();
      break;
    case wire::SketchConstraint::kEqual:
      payload = &value.equal();
      break;
    case wire::SketchConstraint::kCollinear:
      payload = &value.collinear();
      break;
    default:
      std::terminate();
    }
    auto pair = readEntityPair(*payload);
    if (!pair)
      return std::unexpected(std::move(pair.error()));
    switch (value.relation_case()) {
    case wire::SketchConstraint::kParallel:
      return sketch::Parallel{*id, pair->first, pair->second};
    case wire::SketchConstraint::kPerpendicular:
      return sketch::Perpendicular{*id, pair->first, pair->second};
    case wire::SketchConstraint::kConcentric:
      return sketch::Concentric{*id, pair->first, pair->second};
    case wire::SketchConstraint::kEqual:
      return sketch::Equal{*id, pair->first, pair->second};
    case wire::SketchConstraint::kCollinear:
      return sketch::Collinear{*id, pair->first, pair->second};
    default:
      std::terminate();
    }
  }
  case wire::SketchConstraint::kTangent: {
    auto first = readEntityId(value.tangent().first());
    auto second = readEntityId(value.tangent().second());
    if (!first)
      return std::unexpected(std::move(first.error()));
    if (!second)
      return std::unexpected(std::move(second.error()));
    sketch::Tangency mode;
    switch (value.tangent().mode()) {
    case wire::SKETCH_TANGENCY_EXTERNAL:
      mode = sketch::Tangency::External;
      break;
    case wire::SKETCH_TANGENCY_INTERNAL:
      mode = sketch::Tangency::Internal;
      break;
    default:
      return std::unexpected(diagnostic("sketch.wire.invalid-tangency",
                                        "wire tangency mode is unsupported"));
    }
    return sketch::Tangent{*id, *first, *second, mode};
  }
  case wire::SketchConstraint::kMidpoint: {
    auto point = readPointReference(value.midpoint().point());
    auto line = readEntityId(value.midpoint().line());
    if (!point)
      return std::unexpected(std::move(point.error()));
    if (!line)
      return std::unexpected(std::move(line.error()));
    return sketch::Midpoint{*id, *point, *line};
  }
  case wire::SketchConstraint::kFixed: {
    auto entity = readEntityId(value.fixed().entity());
    if (!entity)
      return std::unexpected(std::move(entity.error()));
    return sketch::Fixed{*id, *entity};
  }
  case wire::SketchConstraint::kDistance:
  case wire::SketchConstraint::kHorizontalDistance:
  case wire::SketchConstraint::kVerticalDistance: {
    const wire::SketchPointPairLengthConstraint *payload =
        value.relation_case() == wire::SketchConstraint::kDistance
            ? &value.distance()
        : value.relation_case() == wire::SketchConstraint::kHorizontalDistance
            ? &value.horizontal_distance()
            : &value.vertical_distance();
    auto pair = readPointPair(*payload);
    auto length = readLength(payload->value());
    if (!pair)
      return std::unexpected(std::move(pair.error()));
    if (!length)
      return std::unexpected(std::move(length.error()));
    if (value.relation_case() == wire::SketchConstraint::kDistance)
      return sketch::Distance{*id, pair->first, pair->second, *length};
    if (value.relation_case() == wire::SketchConstraint::kHorizontalDistance)
      return sketch::HorizontalDistance{*id, pair->first, pair->second,
                                        *length};
    return sketch::VerticalDistance{*id, pair->first, pair->second, *length};
  }
  case wire::SketchConstraint::kRadius:
  case wire::SketchConstraint::kDiameter: {
    const wire::SketchCurveLengthConstraint &payload =
        value.relation_case() == wire::SketchConstraint::kRadius
            ? value.radius()
            : value.diameter();
    auto curve = readEntityId(payload.curve());
    auto length = readLength(payload.value());
    if (!curve)
      return std::unexpected(std::move(curve.error()));
    if (!length)
      return std::unexpected(std::move(length.error()));
    if (value.relation_case() == wire::SketchConstraint::kRadius)
      return sketch::Radius{*id, *curve, *length};
    return sketch::Diameter{*id, *curve, *length};
  }
  case wire::SketchConstraint::kAngle: {
    auto first = readEntityId(value.angle().first());
    auto second = readEntityId(value.angle().second());
    auto angle = readAngle(value.angle().value());
    if (!first)
      return std::unexpected(std::move(first.error()));
    if (!second)
      return std::unexpected(std::move(second.error()));
    if (!angle)
      return std::unexpected(std::move(angle.error()));
    return sketch::AngleBetween{*id, *first, *second, *angle};
  }
  default:
    return std::unexpected(diagnostic("sketch.wire.unsupported-constraint",
                                      "wire sketch constraint is unsupported"));
  }
}

void writeConstraint(const sketch::Constraint &value,
                     wire::SketchConstraint *result) {
  std::visit(
      [&]<typename Constraint>(const Constraint &constraint) {
        api::writeId(constraint.id, result->mutable_id());
        if constexpr (std::is_same_v<Constraint, sketch::Coincident>) {
          writePointPair(constraint.first, constraint.second,
                         result->mutable_coincident());
        } else if constexpr (std::is_same_v<Constraint, sketch::Horizontal>) {
          api::writeId(constraint.line,
                       result->mutable_horizontal()->mutable_line());
        } else if constexpr (std::is_same_v<Constraint, sketch::Vertical>) {
          api::writeId(constraint.line,
                       result->mutable_vertical()->mutable_line());
        } else if constexpr (std::is_same_v<Constraint, sketch::Parallel>) {
          writeEntityPair(constraint.first, constraint.second,
                          result->mutable_parallel());
        } else if constexpr (std::is_same_v<Constraint,
                                            sketch::Perpendicular>) {
          writeEntityPair(constraint.first, constraint.second,
                          result->mutable_perpendicular());
        } else if constexpr (std::is_same_v<Constraint, sketch::Tangent>) {
          auto *payload = result->mutable_tangent();
          writeEntityPair(constraint.first, constraint.second, payload);
          payload->set_mode(constraint.mode == sketch::Tangency::External
                                ? wire::SKETCH_TANGENCY_EXTERNAL
                                : wire::SKETCH_TANGENCY_INTERNAL);
        } else if constexpr (std::is_same_v<Constraint, sketch::Concentric>) {
          writeEntityPair(constraint.first, constraint.second,
                          result->mutable_concentric());
        } else if constexpr (std::is_same_v<Constraint, sketch::Equal>) {
          writeEntityPair(constraint.first, constraint.second,
                          result->mutable_equal());
        } else if constexpr (std::is_same_v<Constraint, sketch::Midpoint>) {
          writePointReference(constraint.point,
                              result->mutable_midpoint()->mutable_point());
          api::writeId(constraint.line,
                       result->mutable_midpoint()->mutable_line());
        } else if constexpr (std::is_same_v<Constraint, sketch::Fixed>) {
          api::writeId(constraint.entity,
                       result->mutable_fixed()->mutable_entity());
        } else if constexpr (std::is_same_v<Constraint, sketch::Collinear>) {
          writeEntityPair(constraint.first, constraint.second,
                          result->mutable_collinear());
        } else if constexpr (std::is_same_v<Constraint, sketch::Distance>) {
          auto *payload = result->mutable_distance();
          writePointPair(constraint.first, constraint.second, payload);
          payload->set_value(constraint.value.si());
        } else if constexpr (std::is_same_v<Constraint,
                                            sketch::HorizontalDistance>) {
          auto *payload = result->mutable_horizontal_distance();
          writePointPair(constraint.first, constraint.second, payload);
          payload->set_value(constraint.value.si());
        } else if constexpr (std::is_same_v<Constraint,
                                            sketch::VerticalDistance>) {
          auto *payload = result->mutable_vertical_distance();
          writePointPair(constraint.first, constraint.second, payload);
          payload->set_value(constraint.value.si());
        } else if constexpr (std::is_same_v<Constraint, sketch::Radius>) {
          api::writeId(constraint.curve,
                       result->mutable_radius()->mutable_curve());
          result->mutable_radius()->set_value(constraint.value.si());
        } else if constexpr (std::is_same_v<Constraint, sketch::Diameter>) {
          api::writeId(constraint.curve,
                       result->mutable_diameter()->mutable_curve());
          result->mutable_diameter()->set_value(constraint.value.si());
        } else {
          static_assert(std::is_same_v<Constraint, sketch::AngleBetween>);
          writeEntityPair(constraint.first, constraint.second,
                          result->mutable_angle());
          result->mutable_angle()->set_value(constraint.value.si());
        }
      },
      value);
}

} // namespace

Result<sketch::Definition>
readSketchDefinition(const wire::SketchDefinition &value,
                     const sketch::NumericalProfile &profile) {
  if (const auto coverage = schemaCoverageError(); coverage)
    return std::unexpected(*coverage);
  if (containsUnknownFields(value))
    return std::unexpected(diagnostic(
        "sketch.wire.unknown-field",
        "sketch definition contains an unsupported executable field"));
  if (auto valid = api::validateWire(value); !valid)
    return std::unexpected(std::move(valid.error()));
  auto sourceDigest = api::readDigest<ContentDigest>(value.source_digest());
  if (!sourceDigest)
    return std::unexpected(std::move(sourceDigest.error()));
  sketch::Definition result{*sourceDigest, {}, {}};
  result.entities.reserve(static_cast<std::size_t>(value.entities_size()));
  for (const wire::SketchEntity &entity : value.entities()) {
    auto converted = readEntity(entity);
    if (!converted)
      return std::unexpected(std::move(converted.error()));
    result.entities.emplace_back(std::move(*converted));
  }
  result.constraints.reserve(
      static_cast<std::size_t>(value.constraints_size()));
  for (const wire::SketchConstraint &constraint : value.constraints()) {
    auto converted = readConstraint(constraint);
    if (!converted)
      return std::unexpected(std::move(converted.error()));
    result.constraints.emplace_back(std::move(*converted));
  }
  if (auto valid = sketch::validate(result, profile); !valid)
    return std::unexpected(std::move(valid.error()));
  return result;
}

Result<void> writeSketchDefinition(const sketch::Definition &definition,
                                   wire::SketchDefinition *result,
                                   const sketch::NumericalProfile &profile) {
  if (result == nullptr)
    return invalid("sketch.wire.null-output",
                   "sketch wire output pointer is null");
  if (const auto coverage = schemaCoverageError(); coverage)
    return std::unexpected(*coverage);
  if (definition.entities.size() > maximumSketchDefinitionEntities ||
      definition.constraints.size() > maximumSketchDefinitionConstraints)
    return invalid("sketch.wire.count-limit",
                   "sketch definition exceeds a wire count limit");
  if (auto valid = sketch::validate(definition, profile); !valid)
    return std::unexpected(std::move(valid.error()));
  result->Clear();
  api::writeDigest(definition.sourceDigest, result->mutable_source_digest());
  for (const sketch::Entity &entity : definition.entities)
    writeEntity(entity, result->add_entities());
  for (const sketch::Constraint &constraint : definition.constraints)
    writeConstraint(constraint, result->add_constraints());
  if (result->ByteSizeLong() > maximumSketchDefinitionWireBytes) {
    result->Clear();
    return invalid("sketch.wire.byte-limit",
                   "sketch definition exceeds the wire byte limit");
  }
  return {};
}

Result<sketch::Definition>
parseSketchDefinition(std::span<const std::byte> bytes,
                      const sketch::NumericalProfile &profile) {
  if (bytes.size() > maximumSketchDefinitionWireBytes ||
      bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    return std::unexpected(
        diagnostic("sketch.wire.byte-limit",
                   "serialized sketch definition exceeds the wire byte limit"));
  protobuf::io::CodedInputStream input(
      reinterpret_cast<const std::uint8_t *>(bytes.data()),
      static_cast<int>(bytes.size()));
  input.SetTotalBytesLimit(static_cast<int>(maximumSketchDefinitionWireBytes));
  input.SetRecursionLimit(maximumSketchDefinitionWireDepth);
  wire::SketchDefinition value;
  if (!value.ParseFromCodedStream(&input) || !input.ConsumedEntireMessage())
    return std::unexpected(diagnostic(
        "sketch.wire.parse", "serialized sketch definition is invalid"));
  return readSketchDefinition(value, profile);
}

Result<std::string>
serializeSketchDefinition(const sketch::Definition &definition,
                          const sketch::NumericalProfile &profile) {
  wire::SketchDefinition value;
  if (auto converted = writeSketchDefinition(definition, &value, profile);
      !converted)
    return std::unexpected(std::move(converted.error()));
  std::string bytes;
  if (!value.SerializeToString(&bytes) ||
      bytes.size() > maximumSketchDefinitionWireBytes)
    return std::unexpected(diagnostic(
        "sketch.wire.serialize", "sketch definition serialization failed"));
  return bytes;
}

} // namespace kearne::adapters
