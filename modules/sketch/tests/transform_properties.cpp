#include <kearne/sketch/tools.hpp>
#include <kearne/sketch/transform.hpp>
#include <kearne/testkit/property.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace {

using namespace kearne;

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

template <typename Id> Id id(std::uint64_t seed) {
  typename Id::RandomTail tail{};
  for (std::size_t index = 0; index < tail.size(); ++index)
    tail[index] = static_cast<std::uint8_t>(seed >> ((index % 8U) * 8U));
  auto result = Id::create(seed & ((std::uint64_t{1} << 48U) - 1U), tail);
  require(result.has_value(), "generated transform identity was invalid");
  return *result;
}

ContentDigest digest(std::uint64_t seed) {
  ContentDigest::Bytes bytes{};
  for (std::size_t index = 0; index < bytes.size(); ++index)
    bytes[index] = static_cast<std::uint8_t>(seed + index * 17U);
  auto result = ContentDigest::fromBytes("blake3", bytes);
  require(result.has_value(), "generated transform digest was invalid");
  return *result;
}

sketch::LengthValue length(double value) {
  auto result = sketch::LengthValue::fromSi(value);
  require(result.has_value(), "generated transform length was invalid");
  return *result;
}

sketch::AngleValue angle(double value) {
  auto result = sketch::AngleValue::fromSi(value);
  require(result.has_value(), "generated transform angle was invalid");
  return *result;
}

sketch::DimensionlessValue scalar(double value) {
  auto result = sketch::DimensionlessValue::fromSi(value);
  require(result.has_value(), "generated transform scalar was invalid");
  return *result;
}

sketch::Point2 point(double x, double y) { return {length(x), length(y)}; }

sketch::Entity entityFor(testkit::Random &random, std::uint64_t index) {
  const SketchEntityId entityId = id<SketchEntityId>(index + 1U);
  const double x = random.between(-100.0, 100.0);
  const double y = random.between(-100.0, 100.0);
  const double a = random.between(0.01, 10.0);
  const double b = random.between(0.005, a);
  const double rotation = random.between(-std::numbers::pi, std::numbers::pi);
  const bool construction = (index & 1U) != 0U;
  switch (index % 9U) {
  case 0U:
    return sketch::PointEntity{entityId, point(x, y), construction};
  case 1U:
    return sketch::LineEntity{entityId, point(x, y), point(x + a, y - b),
                              construction};
  case 2U:
    return sketch::CircleEntity{entityId, point(x, y), length(a), construction};
  case 3U:
    return sketch::ArcEntity{entityId,    point(x, y), length(a),
                             angle(-0.7), angle(1.9),  construction};
  case 4U:
    return sketch::EllipseEntity{entityId,  point(x, y),     length(a),
                                 length(b), angle(rotation), construction};
  case 5U:
    return sketch::EllipticalArcEntity{entityId,   point(x, y),     length(a),
                                       length(b),  angle(rotation), angle(-0.8),
                                       angle(1.4), construction};
  case 6U:
    return sketch::HyperbolicArcEntity{
        entityId,        point(x, y),  length(a),   length(b),
        angle(rotation), scalar(-0.7), scalar(0.9), construction};
  case 7U:
    return sketch::ParabolicArcEntity{entityId,        point(x, y), length(a),
                                      angle(rotation), length(-b),  length(a),
                                      construction};
  default:
    return sketch::BSplineEntity{
        entityId,
        {point(x, y), point(x + a * 0.3, y + b), point(x + a * 0.7, y - b),
         point(x + a, y)},
        {scalar(0.0), scalar(0.0), scalar(0.0), scalar(0.0), scalar(1.0),
         scalar(1.0), scalar(1.0), scalar(1.0)},
        {scalar(1.0), scalar(1.0), scalar(1.0), scalar(1.0)},
        3U,
        false,
        construction};
  }
}

std::vector<sketch::PointKey> pointKeys(const sketch::Entity &entity,
                                        bool reflected) {
  using Kind = sketch::PointKey;
  if (std::holds_alternative<sketch::PointEntity>(entity))
    return {Kind::Point};
  if (std::holds_alternative<sketch::LineEntity>(entity))
    return {Kind::Start, Kind::End};
  if (std::holds_alternative<sketch::CircleEntity>(entity))
    return {Kind::Center};
  if (std::holds_alternative<sketch::ArcEntity>(entity))
    return {Kind::Center, Kind::Start, Kind::End};
  if (std::holds_alternative<sketch::EllipseEntity>(entity))
    return reflected ? std::vector{Kind::Center, Kind::Major}
                     : std::vector{Kind::Center, Kind::Major, Kind::Minor};
  if (std::holds_alternative<sketch::EllipticalArcEntity>(entity) ||
      std::holds_alternative<sketch::HyperbolicArcEntity>(entity))
    return {Kind::Center, Kind::Start, Kind::End};
  if (std::holds_alternative<sketch::ParabolicArcEntity>(entity))
    return {Kind::Center, Kind::Focus, Kind::Start, Kind::End};
  return {Kind::Start, Kind::End};
}

void requireNear(const sketch::Point2 &first, const sketch::Point2 &second,
                 const char *message) {
  const double scale =
      std::max({1.0, std::abs(first.x.si()), std::abs(first.y.si()),
                std::abs(second.x.si()), std::abs(second.y.si())});
  require(std::hypot(first.x.si() - second.x.si(),
                     first.y.si() - second.y.si()) <= 2.0e-12 * scale,
          message);
}

sketch::SimilarityTransform2d
inverse(const sketch::SimilarityTransform2d &transform) {
  const double cosine = std::cos(transform.rotationRadians);
  const double sine = std::sin(transform.rotationRadians);
  const double rotatedX = cosine * transform.translation.x.si() +
                          sine * transform.translation.y.si();
  double rotatedY = -sine * transform.translation.x.si() +
                    cosine * transform.translation.y.si();
  if (transform.reflected)
    rotatedY = -rotatedY;
  return {transform.pivot,
          point(-rotatedX / transform.scale, -rotatedY / transform.scale),
          transform.reflected ? transform.rotationRadians
                              : -transform.rotationRadians,
          1.0 / transform.scale, transform.reflected};
}

void verifySimilarityTransforms(const testkit::PropertyProfile &profile) {
  testkit::checkProperty(
      "Sketch similarity transforms", profile,
      [](testkit::Random &random, std::uint64_t index) {
        const sketch::Entity source = entityFor(random, index);
        const sketch::SimilarityTransform2d transform{
            point(random.between(-10.0, 10.0), random.between(-10.0, 10.0)),
            point(random.between(-2.0, 2.0), random.between(-2.0, 2.0)),
            random.between(-std::numbers::pi, std::numbers::pi),
            random.between(0.01, 20.0), (random.next() & 1U) != 0U};
        auto transformed = sketch::transformEntity(source, transform);
        require(transformed.has_value(), "valid similarity transform failed");
        require(sketch::entityId(*transformed) == sketch::entityId(source) &&
                    transformed->index() == source.index(),
                "similarity transform changed stable entity identity or kind");
        require(
            std::visit([](const auto &value) { return value.construction; },
                       *transformed) ==
                std::visit([](const auto &value) { return value.construction; },
                           source),
            "similarity transform changed construction state");
        if (const auto *spline = std::get_if<sketch::BSplineEntity>(&source)) {
          const auto &result = std::get<sketch::BSplineEntity>(*transformed);
          require(result.knots == spline->knots &&
                      result.weights == spline->weights &&
                      result.degree == spline->degree &&
                      result.periodic == spline->periodic &&
                      result.controlPoints.size() ==
                          spline->controlPoints.size(),
                  "similarity transform changed B-spline parameterization");
          for (std::size_t pointIndex = 0;
               pointIndex < spline->controlPoints.size(); ++pointIndex) {
            auto expected = sketch::transformPoint(
                spline->controlPoints[pointIndex], transform);
            require(expected.has_value(), "B-spline pole transform failed");
            requireNear(result.controlPoints[pointIndex], *expected,
                        "B-spline pole disagrees with point transform");
          }
        }

        const sketch::Definition before{digest(index), {}, {source}, {}};
        const sketch::Definition after{digest(index), {}, {*transformed}, {}};
        for (const sketch::PointKey key :
             pointKeys(source, transform.reflected)) {
          auto original =
              sketch::resolvePoint(before, {sketch::entityId(source), key});
          auto actual = sketch::resolvePoint(
              after, {sketch::entityId(*transformed), key});
          require(original && actual,
                  "transformed semantic point was unresolved");
          auto expected = sketch::transformPoint(*original, transform);
          require(expected.has_value(), "semantic point transform failed");
          requireNear(*actual, *expected,
                      "transformed curve disagrees with point transform");
        }

        auto restored =
            sketch::transformEntity(*transformed, inverse(transform));
        require(restored.has_value(), "inverse similarity transform failed");
        const sketch::Definition restoredDefinition{
            digest(index), {}, {*restored}, {}};
        for (const sketch::PointKey key : pointKeys(source, false)) {
          auto original =
              sketch::resolvePoint(before, {sketch::entityId(source), key});
          auto actual = sketch::resolvePoint(
              restoredDefinition, {sketch::entityId(*restored), key});
          require(original && actual, "inverse semantic point was unresolved");
          requireNear(*actual, *original,
                      "inverse similarity transform did not restore geometry");
        }
      });
}

void verifyInvalidTransformRefusal() {
  const sketch::Entity source{
      sketch::PointEntity{id<SketchEntityId>(42U), point(0.0, 0.0)}};
  sketch::SimilarityTransform2d invalid{point(0.0, 0.0), point(0.0, 0.0)};
  invalid.scale = 0.0;
  require(!sketch::transformEntity(source, invalid),
          "zero-scale transform was accepted");
  invalid.scale = 1.0;
  invalid.rotationRadians = std::numeric_limits<double>::quiet_NaN();
  require(!sketch::transformEntity(source, invalid),
          "non-finite transform was accepted");
}

void verifySelectionTransforms(const testkit::PropertyProfile &profile) {
  testkit::checkProperty(
      "Sketch transform selection policies", profile,
      [](testkit::Random &random, std::uint64_t index) {
        const std::uint64_t seed = index * 32U + 10'000U;
        const std::array entityIds{
            id<SketchEntityId>(seed + 1U), id<SketchEntityId>(seed + 2U),
            id<SketchEntityId>(seed + 3U), id<SketchEntityId>(seed + 4U)};
        const std::array constraintIds{id<SketchConstraintId>(seed + 5U),
                                       id<SketchConstraintId>(seed + 6U),
                                       id<SketchConstraintId>(seed + 7U),
                                       id<SketchConstraintId>(seed + 8U),
                                       id<SketchConstraintId>(seed + 9U),
                                       id<SketchConstraintId>(seed + 10U),
                                       id<SketchConstraintId>(seed + 11U),
                                       id<SketchConstraintId>(seed + 12U)};
        const SketchObjectId objectId = id<SketchObjectId>(seed);
        const double x = random.between(-20.0, 20.0);
        const double y = random.between(-20.0, 20.0);
        const double width = random.between(0.01, 5.0);
        const double height = random.between(0.01, 5.0);
        sketch::Definition empty{digest(seed), {}, {}, {}};
        auto rectangle = sketch::applyTool(
            empty,
            sketch::RectangleToolInput{{objectId, entityIds, constraintIds},
                                       point(x, y),
                                       point(x + width, y + height),
                                       false});
        require(rectangle.has_value(), "transform rectangle fixture failed");

        const sketch::SimilarityTransform2d moved{
            point(0.0, 0.0),
            point(random.between(-2.0, 2.0), random.between(-2.0, 2.0))};
        auto all = sketch::transformSelection(
            rectangle->target, entityIds, moved,
            sketch::ExternalConstraintPolicy::Refuse);
        require(all && all->target.objects == rectangle->target.objects &&
                    all->target.constraints == rectangle->target.constraints,
                "whole-object translation changed intent or constraints");

        const std::array partial{entityIds.front()};
        auto refused = sketch::transformSelection(
            rectangle->target, partial, moved,
            sketch::ExternalConstraintPolicy::Refuse);
        require(!refused && refused.error().code ==
                                "sketch.transform.external-constraint",
                "partial constrained transform did not refuse safely");
        auto detached = sketch::transformSelection(
            rectangle->target, partial, moved,
            sketch::ExternalConstraintPolicy::Detach);
        require(detached && detached->target.objects.size() == 1U &&
                    detached->target.objects.front().id == objectId &&
                    detached->target.objects.front().kind ==
                        sketch::SketchObjectKind::CurveGroup &&
                    detached->target.objects.front().label ==
                        "Rectangle 1 (modified)" &&
                    detached->target.objects.front().members ==
                        rectangle->target.objects.front().members,
                "partial transform lost explicit source-object ancestry");
        const std::array anotherPartial{entityIds[1]};
        auto regrouped = sketch::transformSelection(
            detached->target, anotherPartial, moved,
            sketch::ExternalConstraintPolicy::Detach);
        require(
            regrouped &&
                regrouped->target.objects == detached->target.objects &&
                std::ranges::none_of(
                    regrouped->sourceEdits, [](const auto &intent) {
                      return intent.section == sketch::SourceSection::Objects;
                    }),
            "repeated partial transform rewrote stable source ancestry");

        const double rotation = random.between(0.1, 1.4);
        auto rotated = sketch::transformSelection(
            rectangle->target, entityIds,
            {point(x, y), point(0.0, 0.0), rotation, 1.0, false},
            sketch::ExternalConstraintPolicy::Refuse);
        require(rotated &&
                    std::ranges::none_of(
                        rotated->target.constraints,
                        [](const sketch::Constraint &constraint) {
                          return std::holds_alternative<sketch::Horizontal>(
                                     constraint) ||
                                 std::holds_alternative<sketch::Vertical>(
                                     constraint);
                        }),
                "rotation retained invalid axis-dependent constraints");

        const SketchEntityId lineId = id<SketchEntityId>(seed + 20U);
        const sketch::Point2 lineStart = point(x, y);
        sketch::Definition dimensioned{
            digest(seed + 20U),
            {},
            {sketch::LineEntity{lineId, lineStart, point(x + width, y)}},
            {sketch::Distance{id<SketchConstraintId>(seed + 21U),
                              {lineId, sketch::PointKey::Start},
                              {lineId, sketch::PointKey::End},
                              length(width)},
             sketch::HorizontalDistance{id<SketchConstraintId>(seed + 22U),
                                        {lineId, sketch::PointKey::Start},
                                        {lineId, sketch::PointKey::End},
                                        length(width)},
             sketch::Lock{id<SketchConstraintId>(seed + 23U),
                          {lineId, sketch::PointKey::Start},
                          lineStart}}};
        const double scale = random.between(0.1, 10.0);
        const sketch::SimilarityTransform2d scaled{
            point(x, y), point(0.25, -0.5), 0.0, scale, false};
        const std::array lineSelection{lineId};
        auto transformedDimensions = sketch::transformSelection(
            dimensioned, lineSelection, scaled,
            sketch::ExternalConstraintPolicy::Refuse);
        require(transformedDimensions &&
                    std::abs(std::get<sketch::Distance>(
                                 transformedDimensions->target.constraints[0])
                                 .value.si() -
                             width * scale) <= width * scale * 1.0e-13 &&
                    std::abs(std::get<sketch::HorizontalDistance>(
                                 transformedDimensions->target.constraints[1])
                                 .value.si() -
                             width * scale) <= width * scale * 1.0e-13,
                "scale did not update dimensional constraints");
        auto expectedLock = sketch::transformPoint(lineStart, scaled);
        require(expectedLock.has_value(), "lock transform fixture failed");
        requireNear(
            std::get<sketch::Lock>(transformedDimensions->target.constraints[2])
                .position,
            *expectedLock, "transform did not update absolute lock position");

        std::vector<sketch::TransformCopy> copies;
        copies.reserve(2U);
        for (std::size_t copyIndex = 0; copyIndex < 2U; ++copyIndex) {
          sketch::TransformCopy copy{
              {point(0.0, 0.0),
               point(width * static_cast<double>(copyIndex + 1U),
                     height * static_cast<double>(copyIndex + 1U))},
              {},
              {},
              {}};
          for (std::size_t entityIndex = 0; entityIndex < entityIds.size();
               ++entityIndex)
            copy.entities.push_back(
                {entityIds[entityIndex],
                 id<SketchEntityId>(seed + 100U + copyIndex * 32U +
                                    entityIndex)});
          for (std::size_t constraintIndex = 0;
               constraintIndex < constraintIds.size(); ++constraintIndex)
            copy.constraints.push_back(
                {constraintIds[constraintIndex],
                 id<SketchConstraintId>(seed + 200U + copyIndex * 32U +
                                        constraintIndex)});
          copy.objects.push_back(
              {objectId, id<SketchObjectId>(seed + 300U + copyIndex),
               "Rectangle " + std::to_string(copyIndex + 2U)});
          copies.push_back(std::move(copy));
        }
        auto array =
            sketch::copySelection(rectangle->target, entityIds, copies,
                                  sketch::DimensionCopyPolicy::Preserve);
        require(array && array->target.entities.size() == 12U &&
                    array->target.objects.size() == 3U &&
                    array->target.constraints.size() == 24U &&
                    array->target.objects[1].label == "Rectangle 2" &&
                    array->target.objects[2].label == "Rectangle 3",
                "rectangular copy did not preserve complete object intent");

        const SketchConstraintId distanceId =
            id<SketchConstraintId>(seed + 400U);
        const SketchConstraintId lockId = id<SketchConstraintId>(seed + 401U);
        sketch::Definition equalizedSource{
            digest(seed + 402U),
            {},
            {sketch::LineEntity{lineId, lineStart, point(x + width, y)}},
            {sketch::Distance{distanceId,
                              {lineId, sketch::PointKey::Start},
                              {lineId, sketch::PointKey::End},
                              length(width)},
             sketch::Lock{
                 lockId, {lineId, sketch::PointKey::Start}, lineStart}}};
        const SketchEntityId copiedLine = id<SketchEntityId>(seed + 403U);
        const SketchConstraintId equalId = id<SketchConstraintId>(seed + 404U);
        const SketchConstraintId copiedLockId =
            id<SketchConstraintId>(seed + 405U);
        const std::array equalizedCopy{sketch::TransformCopy{
            scaled,
            {{lineId, copiedLine}},
            {{distanceId, equalId}, {lockId, copiedLockId}},
            {}}};
        auto equalized =
            sketch::copySelection(equalizedSource, lineSelection, equalizedCopy,
                                  sketch::DimensionCopyPolicy::Equalize);
        require(
            equalized && equalized->target.entities.size() == 2U &&
                equalized->target.constraints.size() == 4U &&
                std::get<sketch::Equal>(equalized->target.constraints[2]) ==
                    sketch::Equal{equalId, lineId, copiedLine} &&
                std::get<sketch::Lock>(equalized->target.constraints[3]).id ==
                    copiedLockId,
            "equalized copy did not couple dimensions or preserve locks");
      });
}

} // namespace

int main() {
  try {
    verifyInvalidTransformRefusal();
    verifySimilarityTransforms(kearne::testkit::propertyProfile());
    verifySelectionTransforms(kearne::testkit::propertyProfile());
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
  }
}
