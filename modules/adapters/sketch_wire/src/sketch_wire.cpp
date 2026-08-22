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
        hasExactFields(*wire::SketchEllipseGeometry::descriptor(),
                       std::array{1, 2, 3, 4}) &&
        hasExactFields(*wire::SketchEllipticalArcGeometry::descriptor(),
                       std::array{1, 2, 3, 4, 5, 6}) &&
        hasExactFields(*wire::SketchHyperbolicArcGeometry::descriptor(),
                       std::array{1, 2, 3, 4, 5, 6}) &&
        hasExactFields(*wire::SketchParabolicArcGeometry::descriptor(),
                       std::array{1, 2, 3, 4, 5}) &&
        hasExactFields(*wire::SketchBSplineGeometry::descriptor(),
                       std::array{1, 2, 3, 4, 5}) &&
        hasExactFields(*wire::SketchEntity::descriptor(),
                       std::array{1, 2, 20, 21, 22, 23, 24, 25, 26, 27, 28}) &&
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
        hasExactFields(*wire::SketchPointCurveConstraint::descriptor(),
                       std::array{1, 2}) &&
        hasExactFields(*wire::SketchSymmetricConstraint::descriptor(),
                       std::array{1, 2, 3}) &&
        hasExactFields(*wire::SketchPointPositionConstraint::descriptor(),
                       std::array{1, 2}) &&
        hasExactFields(*wire::SketchPointTripleConstraint::descriptor(),
                       std::array{1, 2, 3}) &&
        hasExactFields(*wire::SketchEntityConstraint::descriptor(),
                       std::array{1}) &&
        hasExactFields(*wire::SketchEntitySetConstraint::descriptor(),
                       std::array{1}) &&
        hasExactFields(*wire::SketchPointPairLengthConstraint::descriptor(),
                       std::array{1, 2, 3}) &&
        hasExactFields(*wire::SketchCurveLengthConstraint::descriptor(),
                       std::array{1, 2}) &&
        hasExactFields(*wire::SketchEntityPairAngleConstraint::descriptor(),
                       std::array{1, 2, 3}) &&
        hasExactFields(*wire::SketchConstraint::descriptor(),
                       std::array{1,  20, 21, 22, 23, 24, 25, 26,
                                  27, 28, 29, 30, 31, 32, 33, 34,
                                  35, 36, 37, 38, 39, 40, 41}) &&
        hasExactFields(*wire::SketchObjectMember::descriptor(),
                       std::array{1, 2}) &&
        hasExactFields(*wire::SketchObject::descriptor(),
                       std::array{1, 2, 3, 4}) &&
        hasExactFields(*wire::SketchDefinition::descriptor(),
                       std::array{1, 2, 3, 4});
    const protobuf::OneofDescriptor *geometry =
        wire::SketchEntity::descriptor()->FindOneofByName("geometry");
    const protobuf::OneofDescriptor *relation =
        wire::SketchConstraint::descriptor()->FindOneofByName("relation");
    if (complete && geometry != nullptr &&
        hasExactFields(*geometry,
                       std::array{20, 21, 22, 23, 24, 25, 26, 27, 28}) &&
        relation != nullptr &&
        hasExactFields(*relation,
                       std::array{20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
                                  31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41}))
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

Result<sketch::DimensionlessValue> readDimensionless(double value) {
  return sketch::DimensionlessValue::fromSi(value);
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
  case wire::SKETCH_POINT_KEY_MAJOR:
    return sketch::PointKey::Major;
  case wire::SKETCH_POINT_KEY_MINOR:
    return sketch::PointKey::Minor;
  case wire::SKETCH_POINT_KEY_FOCUS:
    return sketch::PointKey::Focus;
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
  case sketch::PointKey::Major:
    return wire::SKETCH_POINT_KEY_MAJOR;
  case sketch::PointKey::Minor:
    return wire::SKETCH_POINT_KEY_MINOR;
  case sketch::PointKey::Focus:
    return wire::SKETCH_POINT_KEY_FOCUS;
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

Result<sketch::SketchObjectKind> readObjectKind(wire::SketchObjectKind value) {
  switch (value) {
  case wire::SKETCH_OBJECT_KIND_RECTANGLE:
    return sketch::SketchObjectKind::Rectangle;
  case wire::SKETCH_OBJECT_KIND_POINT:
    return sketch::SketchObjectKind::Point;
  case wire::SKETCH_OBJECT_KIND_LINE:
    return sketch::SketchObjectKind::Line;
  case wire::SKETCH_OBJECT_KIND_CIRCLE:
    return sketch::SketchObjectKind::Circle;
  case wire::SKETCH_OBJECT_KIND_ARC:
    return sketch::SketchObjectKind::Arc;
  case wire::SKETCH_OBJECT_KIND_SLOT:
    return sketch::SketchObjectKind::Slot;
  case wire::SKETCH_OBJECT_KIND_ARC_SLOT:
    return sketch::SketchObjectKind::ArcSlot;
  case wire::SKETCH_OBJECT_KIND_POLYLINE:
    return sketch::SketchObjectKind::Polyline;
  case wire::SKETCH_OBJECT_KIND_REGULAR_POLYGON:
    return sketch::SketchObjectKind::RegularPolygon;
  case wire::SKETCH_OBJECT_KIND_OBLONG:
    return sketch::SketchObjectKind::Oblong;
  case wire::SKETCH_OBJECT_KIND_ELLIPSE:
    return sketch::SketchObjectKind::Ellipse;
  case wire::SKETCH_OBJECT_KIND_ELLIPTICAL_ARC:
    return sketch::SketchObjectKind::EllipticalArc;
  case wire::SKETCH_OBJECT_KIND_HYPERBOLIC_ARC:
    return sketch::SketchObjectKind::HyperbolicArc;
  case wire::SKETCH_OBJECT_KIND_PARABOLIC_ARC:
    return sketch::SketchObjectKind::ParabolicArc;
  case wire::SKETCH_OBJECT_KIND_BSPLINE:
    return sketch::SketchObjectKind::BSpline;
  case wire::SKETCH_OBJECT_KIND_FILLET:
    return sketch::SketchObjectKind::Fillet;
  case wire::SKETCH_OBJECT_KIND_CHAMFER:
    return sketch::SketchObjectKind::Chamfer;
  case wire::SKETCH_OBJECT_KIND_OFFSET:
    return sketch::SketchObjectKind::Offset;
  case wire::SKETCH_OBJECT_KIND_JOINED_CURVE:
    return sketch::SketchObjectKind::JoinedCurve;
  case wire::SKETCH_OBJECT_KIND_CURVE_GROUP:
    return sketch::SketchObjectKind::CurveGroup;
  default:
    break;
  }
  return std::unexpected(diagnostic("sketch.wire.invalid-object-kind",
                                    "wire Sketch object kind is unsupported"));
}

wire::SketchObjectKind writeObjectKind(sketch::SketchObjectKind value) {
  switch (value) {
  case sketch::SketchObjectKind::Rectangle:
    return wire::SKETCH_OBJECT_KIND_RECTANGLE;
  case sketch::SketchObjectKind::Point:
    return wire::SKETCH_OBJECT_KIND_POINT;
  case sketch::SketchObjectKind::Line:
    return wire::SKETCH_OBJECT_KIND_LINE;
  case sketch::SketchObjectKind::Circle:
    return wire::SKETCH_OBJECT_KIND_CIRCLE;
  case sketch::SketchObjectKind::Arc:
    return wire::SKETCH_OBJECT_KIND_ARC;
  case sketch::SketchObjectKind::Slot:
    return wire::SKETCH_OBJECT_KIND_SLOT;
  case sketch::SketchObjectKind::ArcSlot:
    return wire::SKETCH_OBJECT_KIND_ARC_SLOT;
  case sketch::SketchObjectKind::Polyline:
    return wire::SKETCH_OBJECT_KIND_POLYLINE;
  case sketch::SketchObjectKind::RegularPolygon:
    return wire::SKETCH_OBJECT_KIND_REGULAR_POLYGON;
  case sketch::SketchObjectKind::Oblong:
    return wire::SKETCH_OBJECT_KIND_OBLONG;
  case sketch::SketchObjectKind::Ellipse:
    return wire::SKETCH_OBJECT_KIND_ELLIPSE;
  case sketch::SketchObjectKind::EllipticalArc:
    return wire::SKETCH_OBJECT_KIND_ELLIPTICAL_ARC;
  case sketch::SketchObjectKind::HyperbolicArc:
    return wire::SKETCH_OBJECT_KIND_HYPERBOLIC_ARC;
  case sketch::SketchObjectKind::ParabolicArc:
    return wire::SKETCH_OBJECT_KIND_PARABOLIC_ARC;
  case sketch::SketchObjectKind::BSpline:
    return wire::SKETCH_OBJECT_KIND_BSPLINE;
  case sketch::SketchObjectKind::Fillet:
    return wire::SKETCH_OBJECT_KIND_FILLET;
  case sketch::SketchObjectKind::Chamfer:
    return wire::SKETCH_OBJECT_KIND_CHAMFER;
  case sketch::SketchObjectKind::Offset:
    return wire::SKETCH_OBJECT_KIND_OFFSET;
  case sketch::SketchObjectKind::JoinedCurve:
    return wire::SKETCH_OBJECT_KIND_JOINED_CURVE;
  case sketch::SketchObjectKind::CurveGroup:
    return wire::SKETCH_OBJECT_KIND_CURVE_GROUP;
  }
  std::terminate();
}

Result<sketch::SketchObject> readObject(const wire::SketchObject &value) {
  auto id = api::readId<SketchObjectId>(value.id());
  auto kind = readObjectKind(value.kind());
  if (!id)
    return std::unexpected(std::move(id.error()));
  if (!kind)
    return std::unexpected(std::move(kind.error()));
  sketch::SketchObject result{
      *id, std::string{value.label().data(), value.label().size()}, *kind, {}};
  result.members.reserve(static_cast<std::size_t>(value.members_size()));
  for (const wire::SketchObjectMember &member : value.members()) {
    auto entity = readEntityId(member.entity());
    if (!entity)
      return std::unexpected(std::move(entity.error()));
    result.members.push_back(sketch::SketchObjectMember{
        std::string{member.role().data(), member.role().size()}, *entity});
  }
  return result;
}

void writeObject(const sketch::SketchObject &value,
                 wire::SketchObject *result) {
  api::writeId(value.id, result->mutable_id());
  result->set_label(value.label);
  result->set_kind(writeObjectKind(value.kind));
  for (const sketch::SketchObjectMember &member : value.members) {
    wire::SketchObjectMember *wireMember = result->add_members();
    wireMember->set_role(member.role);
    api::writeId(member.entity, wireMember->mutable_entity());
  }
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
  case wire::SketchEntity::kEllipse: {
    auto center = readPoint(value.ellipse().center());
    auto major = readLength(value.ellipse().major_radius());
    auto minor = readLength(value.ellipse().minor_radius());
    auto rotation = readAngle(value.ellipse().rotation());
    if (!center || !major || !minor || !rotation)
      return std::unexpected(!center  ? std::move(center.error())
                             : !major ? std::move(major.error())
                             : !minor ? std::move(minor.error())
                                      : std::move(rotation.error()));
    return sketch::EllipseEntity{*id,    *center,   *major,
                                 *minor, *rotation, value.construction()};
  }
  case wire::SketchEntity::kEllipticalArc: {
    auto center = readPoint(value.elliptical_arc().center());
    auto major = readLength(value.elliptical_arc().major_radius());
    auto minor = readLength(value.elliptical_arc().minor_radius());
    auto rotation = readAngle(value.elliptical_arc().rotation());
    auto start = readAngle(value.elliptical_arc().start_parameter());
    auto end = readAngle(value.elliptical_arc().end_parameter());
    if (!center || !major || !minor || !rotation || !start || !end) {
      if (!center)
        return std::unexpected(std::move(center.error()));
      if (!major)
        return std::unexpected(std::move(major.error()));
      if (!minor)
        return std::unexpected(std::move(minor.error()));
      if (!rotation)
        return std::unexpected(std::move(rotation.error()));
      if (!start)
        return std::unexpected(std::move(start.error()));
      return std::unexpected(std::move(end.error()));
    }
    return sketch::EllipticalArcEntity{
        *id,       *center, *major, *minor,
        *rotation, *start,  *end,   value.construction()};
  }
  case wire::SketchEntity::kHyperbolicArc: {
    const auto &payload = value.hyperbolic_arc();
    auto center = readPoint(payload.center());
    auto major = readLength(payload.major_radius());
    auto minor = readLength(payload.minor_radius());
    auto rotation = readAngle(payload.rotation());
    auto start = readDimensionless(payload.start_parameter());
    auto end = readDimensionless(payload.end_parameter());
    if (!center || !major || !minor || !rotation || !start || !end) {
      if (!center)
        return std::unexpected(std::move(center.error()));
      if (!major)
        return std::unexpected(std::move(major.error()));
      if (!minor)
        return std::unexpected(std::move(minor.error()));
      if (!rotation)
        return std::unexpected(std::move(rotation.error()));
      if (!start)
        return std::unexpected(std::move(start.error()));
      return std::unexpected(std::move(end.error()));
    }
    return sketch::HyperbolicArcEntity{
        *id,       *center, *major, *minor,
        *rotation, *start,  *end,   value.construction()};
  }
  case wire::SketchEntity::kParabolicArc: {
    const auto &payload = value.parabolic_arc();
    auto vertex = readPoint(payload.vertex());
    auto focal = readLength(payload.focal_length());
    auto rotation = readAngle(payload.rotation());
    auto start = readLength(payload.start_parameter());
    auto end = readLength(payload.end_parameter());
    if (!vertex || !focal || !rotation || !start || !end) {
      if (!vertex)
        return std::unexpected(std::move(vertex.error()));
      if (!focal)
        return std::unexpected(std::move(focal.error()));
      if (!rotation)
        return std::unexpected(std::move(rotation.error()));
      if (!start)
        return std::unexpected(std::move(start.error()));
      return std::unexpected(std::move(end.error()));
    }
    return sketch::ParabolicArcEntity{
        *id, *vertex, *focal, *rotation, *start, *end, value.construction()};
  }
  case wire::SketchEntity::kBspline: {
    const auto &payload = value.bspline();
    std::vector<sketch::Point2> controlPoints;
    std::vector<sketch::DimensionlessValue> knots;
    std::vector<sketch::DimensionlessValue> weights;
    controlPoints.reserve(
        static_cast<std::size_t>(payload.control_points_size()));
    knots.reserve(static_cast<std::size_t>(payload.knots_size()));
    weights.reserve(static_cast<std::size_t>(payload.weights_size()));
    for (const wire::SketchPoint2 &point : payload.control_points()) {
      auto decoded = readPoint(point);
      if (!decoded)
        return std::unexpected(std::move(decoded.error()));
      controlPoints.push_back(*decoded);
    }
    for (const double knot : payload.knots()) {
      auto decoded = readDimensionless(knot);
      if (!decoded)
        return std::unexpected(std::move(decoded.error()));
      knots.push_back(*decoded);
    }
    for (const double weight : payload.weights()) {
      auto decoded = readDimensionless(weight);
      if (!decoded)
        return std::unexpected(std::move(decoded.error()));
      weights.push_back(*decoded);
    }
    return sketch::BSplineEntity{*id,
                                 std::move(controlPoints),
                                 std::move(knots),
                                 std::move(weights),
                                 payload.degree(),
                                 payload.periodic(),
                                 value.construction()};
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
        } else if constexpr (std::is_same_v<Entity, sketch::ArcEntity>) {
          writePoint(entity.center, result->mutable_arc()->mutable_center());
          result->mutable_arc()->set_radius(entity.radius.si());
          result->mutable_arc()->set_start_angle(entity.startAngle.si());
          result->mutable_arc()->set_end_angle(entity.endAngle.si());
        } else if constexpr (std::is_same_v<Entity, sketch::EllipseEntity>) {
          auto *ellipse = result->mutable_ellipse();
          writePoint(entity.center, ellipse->mutable_center());
          ellipse->set_major_radius(entity.majorRadius.si());
          ellipse->set_minor_radius(entity.minorRadius.si());
          ellipse->set_rotation(entity.rotation.si());
        } else if constexpr (std::is_same_v<Entity,
                                            sketch::EllipticalArcEntity>) {
          auto *arc = result->mutable_elliptical_arc();
          writePoint(entity.center, arc->mutable_center());
          arc->set_major_radius(entity.majorRadius.si());
          arc->set_minor_radius(entity.minorRadius.si());
          arc->set_rotation(entity.rotation.si());
          arc->set_start_parameter(entity.startParameter.si());
          arc->set_end_parameter(entity.endParameter.si());
        } else if constexpr (std::is_same_v<Entity,
                                            sketch::HyperbolicArcEntity>) {
          auto *arc = result->mutable_hyperbolic_arc();
          writePoint(entity.center, arc->mutable_center());
          arc->set_major_radius(entity.majorRadius.si());
          arc->set_minor_radius(entity.minorRadius.si());
          arc->set_rotation(entity.rotation.si());
          arc->set_start_parameter(entity.startParameter.si());
          arc->set_end_parameter(entity.endParameter.si());
        } else if constexpr (std::is_same_v<Entity,
                                            sketch::ParabolicArcEntity>) {
          auto *arc = result->mutable_parabolic_arc();
          writePoint(entity.vertex, arc->mutable_vertex());
          arc->set_focal_length(entity.focalLength.si());
          arc->set_rotation(entity.rotation.si());
          arc->set_start_parameter(entity.startParameter.si());
          arc->set_end_parameter(entity.endParameter.si());
        } else {
          static_assert(std::is_same_v<Entity, sketch::BSplineEntity>);
          auto *spline = result->mutable_bspline();
          for (const sketch::Point2 &point : entity.controlPoints)
            writePoint(point, spline->add_control_points());
          for (const sketch::DimensionlessValue knot : entity.knots)
            spline->add_knots(knot.si());
          for (const sketch::DimensionlessValue weight : entity.weights)
            spline->add_weights(weight.si());
          spline->set_degree(entity.degree);
          spline->set_periodic(entity.periodic);
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
  case wire::SketchConstraint::kPointOnObject: {
    auto point = readPointReference(value.point_on_object().point());
    auto curve = readEntityId(value.point_on_object().curve());
    if (!point)
      return std::unexpected(std::move(point.error()));
    if (!curve)
      return std::unexpected(std::move(curve.error()));
    return sketch::PointOnObject{*id, *point, *curve};
  }
  case wire::SketchConstraint::kSymmetric: {
    auto first = readPointReference(value.symmetric().first());
    auto second = readPointReference(value.symmetric().second());
    auto axis = readEntityId(value.symmetric().axis());
    if (!first)
      return std::unexpected(std::move(first.error()));
    if (!second)
      return std::unexpected(std::move(second.error()));
    if (!axis)
      return std::unexpected(std::move(axis.error()));
    return sketch::Symmetric{*id, *first, *second, *axis};
  }
  case wire::SketchConstraint::kLock: {
    auto point = readPointReference(value.lock().point());
    auto position = readPoint(value.lock().position());
    if (!point)
      return std::unexpected(std::move(point.error()));
    if (!position)
      return std::unexpected(std::move(position.error()));
    return sketch::Lock{*id, *point, *position};
  }
  case wire::SketchConstraint::kSymmetricAboutPoint: {
    auto first = readPointReference(value.symmetric_about_point().first());
    auto second = readPointReference(value.symmetric_about_point().second());
    auto center = readPointReference(value.symmetric_about_point().center());
    if (!first)
      return std::unexpected(std::move(first.error()));
    if (!second)
      return std::unexpected(std::move(second.error()));
    if (!center)
      return std::unexpected(std::move(center.error()));
    return sketch::SymmetricAboutPoint{*id, *first, *second, *center};
  }
  case wire::SketchConstraint::kBlock: {
    auto entity = readEntityId(value.block().entity());
    if (!entity)
      return std::unexpected(std::move(entity.error()));
    return sketch::Block{*id, *entity};
  }
  case wire::SketchConstraint::kGroup: {
    std::vector<SketchEntityId> entities;
    entities.reserve(static_cast<std::size_t>(value.group().entities_size()));
    for (const wire::UuidV7 &wireEntity : value.group().entities()) {
      auto entity = readEntityId(wireEntity);
      if (!entity)
        return std::unexpected(std::move(entity.error()));
      entities.push_back(*entity);
    }
    return sketch::Group{*id, std::move(entities)};
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
        } else if constexpr (std::is_same_v<Constraint,
                                            sketch::PointOnObject>) {
          writePointReference(
              constraint.point,
              result->mutable_point_on_object()->mutable_point());
          api::writeId(constraint.curve,
                       result->mutable_point_on_object()->mutable_curve());
        } else if constexpr (std::is_same_v<Constraint, sketch::Symmetric>) {
          writePointReference(constraint.first,
                              result->mutable_symmetric()->mutable_first());
          writePointReference(constraint.second,
                              result->mutable_symmetric()->mutable_second());
          api::writeId(constraint.axis,
                       result->mutable_symmetric()->mutable_axis());
        } else if constexpr (std::is_same_v<Constraint, sketch::Lock>) {
          auto *payload = result->mutable_lock();
          writePointReference(constraint.point, payload->mutable_point());
          writePoint(constraint.position, payload->mutable_position());
        } else if constexpr (std::is_same_v<Constraint,
                                            sketch::SymmetricAboutPoint>) {
          auto *payload = result->mutable_symmetric_about_point();
          writePointReference(constraint.first, payload->mutable_first());
          writePointReference(constraint.second, payload->mutable_second());
          writePointReference(constraint.center, payload->mutable_center());
        } else if constexpr (std::is_same_v<Constraint, sketch::Block>) {
          api::writeId(constraint.entity,
                       result->mutable_block()->mutable_entity());
        } else if constexpr (std::is_same_v<Constraint, sketch::Group>) {
          for (const SketchEntityId entity : constraint.entities)
            api::writeId(entity, result->mutable_group()->add_entities());
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
  sketch::Definition result{*sourceDigest, {}, {}, {}};
  result.objects.reserve(static_cast<std::size_t>(value.objects_size()));
  for (const wire::SketchObject &object : value.objects()) {
    auto converted = readObject(object);
    if (!converted)
      return std::unexpected(std::move(converted.error()));
    result.objects.emplace_back(std::move(*converted));
  }
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
  if (definition.objects.size() > maximumSketchDefinitionObjects ||
      definition.entities.size() > maximumSketchDefinitionEntities ||
      definition.constraints.size() > maximumSketchDefinitionConstraints)
    return invalid("sketch.wire.count-limit",
                   "sketch definition exceeds a wire count limit");
  if (auto valid = sketch::validate(definition, profile); !valid)
    return std::unexpected(std::move(valid.error()));
  result->Clear();
  api::writeDigest(definition.sourceDigest, result->mutable_source_digest());
  for (const sketch::SketchObject &object : definition.objects)
    writeObject(object, result->add_objects());
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
