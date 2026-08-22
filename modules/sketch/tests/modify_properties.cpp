#include <kearne/sketch/modify.hpp>
#include <kearne/testkit/property.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
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
  for (std::size_t index = 0U; index < tail.size(); ++index)
    tail[index] = static_cast<std::uint8_t>(seed >> ((index % 8U) * 8U));
  auto result = Id::create(seed & ((std::uint64_t{1} << 48U) - 1U), tail);
  require(result.has_value(), "generated modify identity was invalid");
  return *result;
}

ContentDigest digest(std::uint64_t seed) {
  ContentDigest::Bytes bytes{};
  for (std::size_t index = 0U; index < bytes.size(); ++index)
    bytes[index] = static_cast<std::uint8_t>(seed + index * 19U);
  auto result = ContentDigest::fromBytes("blake3", bytes);
  require(result.has_value(), "generated modify digest was invalid");
  return *result;
}

sketch::LengthValue length(double value) {
  auto result = sketch::LengthValue::fromSi(value);
  require(result.has_value(), "generated modify length was invalid");
  return *result;
}

sketch::AngleValue angle(double value) {
  auto result = sketch::AngleValue::fromSi(value);
  require(result.has_value(), "generated modify angle was invalid");
  return *result;
}

sketch::DimensionlessValue scalar(double value) {
  auto result = sketch::DimensionlessValue::fromSi(value);
  require(result.has_value(), "generated modify scalar was invalid");
  return *result;
}

sketch::Point2 point(double x, double y) { return {length(x), length(y)}; }

sketch::CornerEditIds cornerIds(std::uint64_t seed) {
  return {id<SketchObjectId>(seed),
          id<SketchEntityId>(seed + 1U),
          {id<SketchConstraintId>(seed + 2U), id<SketchConstraintId>(seed + 3U),
           id<SketchConstraintId>(seed + 4U), id<SketchConstraintId>(seed + 5U),
           id<SketchConstraintId>(seed + 6U)}};
}

sketch::Definition cornerDefinition(std::uint64_t seed, double extent,
                                    double direction,
                                    bool constrained = false) {
  const SketchEntityId first = id<SketchEntityId>(seed + 1U);
  const SketchEntityId second = id<SketchEntityId>(seed + 2U);
  const std::vector<sketch::Constraint> constraints =
      constrained ? std::vector<sketch::Constraint>{sketch::Horizontal{
                        id<SketchConstraintId>(seed + 5U), first}}
                  : std::vector<sketch::Constraint>{};
  return {digest(seed),
          {sketch::SketchObject{id<SketchObjectId>(seed + 3U),
                                "Line 1",
                                sketch::SketchObjectKind::Line,
                                {{"curve", first}}},
           sketch::SketchObject{id<SketchObjectId>(seed + 4U),
                                "Line 2",
                                sketch::SketchObjectKind::Line,
                                {{"curve", second}}}},
          {sketch::LineEntity{first, point(extent, 0.0), point(0.0, 0.0)},
           sketch::LineEntity{second, point(0.0, 0.0),
                              point(extent * std::cos(direction),
                                    extent * std::sin(direction))}},
          constraints};
}

void verifyCornerEdits(const testkit::PropertyProfile &profile) {
  testkit::checkProperty(
      "line fillet and chamfer geometry", profile,
      [](testkit::Random &random, std::uint64_t index) {
        const std::uint64_t seed = index * 100U + 10'000U;
        const double direction = random.between(0.35, 2.75);
        const double extent = random.between(0.05, 20.0);
        const double size = random.between(0.001, extent * 0.1);
        const double referenceFraction = random.between(1.0e-6, 1.0);
        const auto current = cornerDefinition(seed, extent, direction);
        const auto &first = std::get<sketch::LineEntity>(current.entities[0]);
        const auto &second = std::get<sketch::LineEntity>(current.entities[1]);
        const sketch::Point2 firstReference =
            point(extent * referenceFraction, 0.0);
        const sketch::Point2 secondReference =
            point(extent * referenceFraction * std::cos(direction),
                  extent * referenceFraction * std::sin(direction));
        const std::array kinds{sketch::CornerEditKind::Fillet,
                               sketch::CornerEditKind::Chamfer};
        for (std::size_t kindIndex = 0U; kindIndex < kinds.size();
             ++kindIndex) {
          const auto ids = cornerIds(seed + 20U + kindIndex * 10U);
          auto edited = sketch::editLineCorner(
              current, {kinds[kindIndex],
                        {first.id, firstReference},
                        {second.id, secondReference},
                        length(size),
                        ids,
                        sketch::ExternalConstraintPolicy::Detach});
          require(edited.has_value(), "valid corner edit was rejected");
          require(sketch::validate(edited->target, {}).has_value(),
                  "corner edit produced an invalid definition");
          require(edited->target.entities.size() == 3U &&
                      edited->target.objects.size() == 3U,
                  "corner edit did not preserve two lines and one result");
          const auto &trimmedFirst =
              std::get<sketch::LineEntity>(edited->target.entities[0]);
          const auto &trimmedSecond =
              std::get<sketch::LineEntity>(edited->target.entities[1]);
          const std::string expectedLabel =
              kindIndex == 0U ? "Fillet 1" : "Chamfer 1";
          require(edited->target.objects.back().label == expectedLabel,
                  "corner result lost its human object identity");
          if (kinds[kindIndex] == sketch::CornerEditKind::Fillet) {
            const auto &arc =
                std::get<sketch::ArcEntity>(edited->target.entities[2]);
            const sketch::Point2 arcStart = *sketch::resolvePoint(
                edited->target, {arc.id, sketch::PointKey::Start});
            const sketch::Point2 arcEnd = *sketch::resolvePoint(
                edited->target, {arc.id, sketch::PointKey::End});
            require(std::hypot(arcStart.x.si() - trimmedFirst.end.x.si(),
                               arcStart.y.si() - trimmedFirst.end.y.si()) <=
                            1.0e-9 &&
                        std::hypot(arcEnd.x.si() - trimmedSecond.start.x.si(),
                                   arcEnd.y.si() -
                                       trimmedSecond.start.y.si()) <= 1.0e-9 &&
                        std::abs(arc.radius.si() - size) <= 1.0e-12 &&
                        edited->target.constraints.size() == 5U,
                    "fillet lost its tangent arc topology");
          } else {
            const auto &chamfer =
                std::get<sketch::LineEntity>(edited->target.entities[2]);
            require(chamfer.start == trimmedFirst.end &&
                        chamfer.end == trimmedSecond.start &&
                        edited->target.constraints.size() == 2U,
                    "chamfer lost its trimmed line topology");
          }
        }
      });
}

void verifyConstraintPolicy() {
  constexpr std::uint64_t seed = 900'000U;
  const auto current =
      cornerDefinition(seed, 0.1, std::numbers::pi / 2.0, true);
  const auto &first = std::get<sketch::LineEntity>(current.entities[0]);
  const auto &second = std::get<sketch::LineEntity>(current.entities[1]);
  sketch::CornerEdit edit{
      sketch::CornerEditKind::Fillet, {first.id, first.start},
      {second.id, second.end},        length(0.01),
      cornerIds(seed + 20U),          sketch::ExternalConstraintPolicy::Refuse};
  require(!sketch::editLineCorner(current, edit),
          "corner edit ignored its refusal policy");
  edit.constraints = sketch::ExternalConstraintPolicy::Detach;
  auto detached = sketch::editLineCorner(current, edit);
  require(detached && detached->target.constraints.size() == 5U,
          "corner edit did not explicitly replace affected constraints");
}

void verifyCompoundCornerIntent(const testkit::PropertyProfile &profile) {
  testkit::checkProperty(
      "partial corner edits preserve human source ancestry", profile,
      [](testkit::Random &random, std::uint64_t index) {
        const std::uint64_t seed = 1'000'000U + index * 80U;
        const double width = random.between(0.05, 20.0);
        const double height = random.between(0.05, 20.0);
        const double size = random.between(
            0.001, std::min(width, height) * 0.2);
        const std::array entities{
            id<SketchEntityId>(seed + 1U), id<SketchEntityId>(seed + 2U),
            id<SketchEntityId>(seed + 3U), id<SketchEntityId>(seed + 4U)};
        const SketchObjectId rectangle = id<SketchObjectId>(seed + 5U);
        const sketch::Definition current{
            digest(seed),
            {{rectangle,
              "Rectangle 1",
              sketch::SketchObjectKind::Rectangle,
              {{"bottom", entities[0]},
               {"right", entities[1]},
               {"top", entities[2]},
               {"left", entities[3]}}}},
            {sketch::LineEntity{entities[0], point(0.0, 0.0),
                                point(width, 0.0)},
             sketch::LineEntity{entities[1], point(width, 0.0),
                                point(width, height)},
             sketch::LineEntity{entities[2], point(width, height),
                                point(0.0, height)},
             sketch::LineEntity{entities[3], point(0.0, height),
                                point(0.0, 0.0)}},
            {}};
        const std::array kinds{sketch::CornerEditKind::Fillet,
                               sketch::CornerEditKind::Chamfer};
        for (std::size_t kind = 0U; kind < kinds.size(); ++kind) {
          auto edited = sketch::editLineCorner(
              current,
              {kinds[kind],
               {entities[0], point(0.0, 0.0)},
               {entities[1], point(width, height)},
               length(size),
               cornerIds(seed + 20U + kind * 10U),
               sketch::ExternalConstraintPolicy::Detach});
          require(edited && edited->target.objects.size() == 2U,
                  "corner edit lost its source objects");
          const auto &group = edited->target.objects.front();
          require(group.id == rectangle &&
                      group.kind == sketch::SketchObjectKind::CurveGroup &&
                      group.label == "Rectangle 1 (modified)" &&
                      group.members == current.objects.front().members &&
                      sketch::validate(edited->target, {}).has_value(),
                  "corner edit exposed anonymous compound members");
          require(std::ranges::any_of(
                      edited->sourceEdits, [](const auto &intent) {
                        return intent.action == sketch::SourceEditAction::Replace &&
                               intent.section == sketch::SourceSection::Objects;
                      }),
                  "corner edit did not reclassify source intent explicitly");
        }
      });
}

void verifyOffsets(const testkit::PropertyProfile &profile) {
  testkit::checkProperty(
      "line circle and arc offsets", profile,
      [](testkit::Random &random, std::uint64_t index) {
        const std::uint64_t seed = 2'000'000U + index * 30U;
        const double direction =
            random.between(-std::numbers::pi, std::numbers::pi);
        const double distance = random.between(-0.02, 0.02);
        const double safeDistance =
            std::abs(distance) < 1.0e-4 ? 1.0e-4 : distance;
        const double radius = random.between(0.05, 10.0);
        const SketchEntityId lineId = id<SketchEntityId>(seed + 1U);
        const SketchEntityId circleId = id<SketchEntityId>(seed + 2U);
        const SketchEntityId arcId = id<SketchEntityId>(seed + 3U);
        sketch::Definition current{
            digest(seed),
            {},
            {sketch::LineEntity{
                 lineId, point(0.0, 0.0),
                 point(std::cos(direction), std::sin(direction))},
             sketch::CircleEntity{circleId, point(2.0, 1.0), length(radius)},
             sketch::ArcEntity{arcId, point(-2.0, 1.0), length(radius),
                               angle(-0.7), angle(1.4)}},
            {}};
        const std::array source{lineId, circleId, arcId};
        sketch::OffsetEdit request{{source.begin(), source.end()},
                                   length(safeDistance),
                                   sketch::OffsetSourceMode::Keep,
                                   {},
                                   sketch::ExternalConstraintPolicy::Refuse};
        for (std::size_t output = 0U; output < source.size(); ++output)
          request.outputs.push_back(
              {id<SketchObjectId>(seed + 10U + output * 2U),
               id<SketchEntityId>(seed + 11U + output * 2U)});
        auto offset = sketch::offsetCurves(current, request);
        require(offset && offset->target.entities.size() == 6U &&
                    offset->target.objects.size() == 3U &&
                    offset->target.objects[0].label == "Offset 1" &&
                    offset->target.objects[2].label == "Offset 3" &&
                    sketch::validate(offset->target, {}).has_value(),
                "valid offset family was not one canonical transaction");
        const auto &offsetLine =
            std::get<sketch::LineEntity>(offset->target.entities[3]);
        const auto &offsetCircle =
            std::get<sketch::CircleEntity>(offset->target.entities[4]);
        const auto &offsetArc =
            std::get<sketch::ArcEntity>(offset->target.entities[5]);
        const double signedSeparation =
            -std::sin(direction) * offsetLine.start.x.si() +
            std::cos(direction) * offsetLine.start.y.si();
        require(std::abs(signedSeparation - safeDistance) <= 1.0e-10 &&
                    std::abs(offsetCircle.radius.si() - radius -
                             safeDistance) <= 1.0e-10 &&
                    std::abs(offsetArc.radius.si() - radius - safeDistance) <=
                        1.0e-10,
                "offset geometry disagrees with its signed distance");

        request.sourceMode = sketch::OffsetSourceMode::Delete;
        auto replaced = sketch::offsetCurves(current, request);
        require(replaced && replaced->target.entities.size() == 3U &&
                    replaced->target.objects.size() == 3U,
                "delete-source offset did not replace its sources atomically");
      });
}

void verifyExtends(const testkit::PropertyProfile &profile) {
  testkit::checkProperty(
      "line and circular arc extension", profile,
      [](testkit::Random &random, std::uint64_t index) {
        const std::uint64_t seed = 4'000'000U + index * 20U;
        const double direction =
            random.between(-std::numbers::pi, std::numbers::pi);
        const double extent = random.between(0.05, 20.0);
        const bool moveStart = (random.next() & 1U) != 0U;
        const double parameter =
            moveStart ? random.between(-2.0, 0.9) : random.between(0.1, 3.0);
        const sketch::Point2 start = point(-0.3, 0.2);
        const sketch::Point2 end =
            point(start.x.si() + extent * std::cos(direction),
                  start.y.si() + extent * std::sin(direction));
        const SketchEntityId lineId = id<SketchEntityId>(seed + 1U);
        sketch::Definition lineDefinition{
            digest(seed), {}, {sketch::LineEntity{lineId, start, end}}, {}};
        const sketch::Point2 lineTarget =
            point(std::lerp(start.x.si(), end.x.si(), parameter),
                  std::lerp(start.y.si(), end.y.si(), parameter));
        auto lineResult = sketch::extendCurve(
            lineDefinition, {{lineId, moveStart ? start : end},
                             lineTarget,
                             sketch::ExternalConstraintPolicy::Refuse});
        require(lineResult && lineResult->target.entities.size() == 1U,
                "valid line extension was rejected");
        const auto &line =
            std::get<sketch::LineEntity>(lineResult->target.entities[0]);
        const sketch::Point2 moved = moveStart ? line.start : line.end;
        require(std::hypot(moved.x.si() - lineTarget.x.si(),
                           moved.y.si() - lineTarget.y.si()) <= 1.0e-10 &&
                    (moveStart ? line.end == end : line.start == start),
                "line extension moved the wrong endpoint");

        const double radius = random.between(0.01, 10.0);
        const double first = random.between(-2.0, 1.0);
        const double span = (random.next() & 1U) != 0U
                                ? random.between(0.3, 2.5)
                                : random.between(-2.5, -0.3);
        const double last = first + span;
        const double change = (random.next() & 1U) != 0U
                                  ? random.between(0.1, 0.25)
                                  : random.between(-0.25, -0.1);
        const double movedAngle = (moveStart ? first : last) + change;
        const SketchEntityId arcId = id<SketchEntityId>(seed + 2U);
        const sketch::Point2 center = point(1.0, -2.0);
        const auto arcPoint = [&](double value) {
          return point(center.x.si() + radius * std::cos(value),
                       center.y.si() + radius * std::sin(value));
        };
        sketch::Definition arcDefinition{
            digest(seed + 1U),
            {},
            {sketch::ArcEntity{arcId, center, length(radius), angle(first),
                               angle(last)}},
            {}};
        auto arcResult = sketch::extendCurve(
            arcDefinition, {{arcId, arcPoint(moveStart ? first : last)},
                            arcPoint(movedAngle),
                            sketch::ExternalConstraintPolicy::Refuse});
        require(arcResult && arcResult->target.entities.size() == 1U,
                "valid arc extension was rejected");
        const auto &arc =
            std::get<sketch::ArcEntity>(arcResult->target.entities[0]);
        require(
            std::abs((moveStart ? arc.startAngle.si() : arc.endAngle.si()) -
                     movedAngle) <= 1.0e-10 &&
                std::abs((moveStart ? arc.endAngle.si() : arc.startAngle.si()) -
                         (moveStart ? last : first)) <= 1.0e-12,
            "arc extension moved the wrong endpoint");
      });

  const SketchEntityId lineId = id<SketchEntityId>(8'000'001U);
  sketch::Definition constrained{
      digest(8'000'000U),
      {},
      {sketch::LineEntity{lineId, point(0.0, 0.0), point(1.0, 0.0)}},
      {sketch::Horizontal{id<SketchConstraintId>(8'000'002U), lineId}}};
  sketch::ExtendEdit edit{{lineId, point(1.0, 0.0)},
                          point(2.0, 0.0),
                          sketch::ExternalConstraintPolicy::Refuse};
  require(!sketch::extendCurve(constrained, edit),
          "Extend ignored its refusal policy");
  edit.constraints = sketch::ExternalConstraintPolicy::Detach;
  auto detached = sketch::extendCurve(constrained, edit);
  require(detached && detached->target.constraints.empty(),
          "Extend did not detach affected constraints explicitly");
}

void verifyNurbsConversion(const testkit::PropertyProfile &profile) {
  testkit::checkProperty(
      "NURBS conversion preserves compatible intent", profile,
      [](testkit::Random &random, std::uint64_t index) {
        const std::uint64_t seed = 9'000'000U + index * 8U;
        const SketchEntityId curve = id<SketchEntityId>(seed + 1U);
        const SketchObjectId object = id<SketchObjectId>(seed + 2U);
        const SketchConstraintId lock = id<SketchConstraintId>(seed + 3U);
        const SketchConstraintId horizontal =
            id<SketchConstraintId>(seed + 4U);
        const sketch::Point2 start =
            point(random.between(-100.0, 100.0),
                  random.between(-100.0, 100.0));
        const sketch::Point2 end =
            point(start.x.si() + random.between(0.01, 100.0), start.y.si());
        sketch::Definition definition{
            digest(seed),
            {{object, "Line 1", sketch::SketchObjectKind::Line,
              {{"curve", curve}}}},
            {sketch::LineEntity{curve, start, end}},
            {sketch::Lock{lock, {curve, sketch::PointKey::Start}, start},
             sketch::Horizontal{horizontal, curve}}};
        sketch::ConvertToNurbsEdit edit{
            {curve,
             {start, end},
             {scalar(0.0), scalar(0.0), scalar(1.0), scalar(1.0)},
             {scalar(1.0), scalar(1.0)},
             1U},
            sketch::ExternalConstraintPolicy::Refuse};
        require(!sketch::convertToNurbs(definition, edit),
                "NURBS conversion silently removed an incompatible constraint");
        edit.constraints = sketch::ExternalConstraintPolicy::Detach;
        auto converted = sketch::convertToNurbs(definition, edit);
        require(converted && converted->target.entities.size() == 1U &&
                    converted->target.objects.size() == 1U &&
                    converted->target.objects.front().id == object &&
                    converted->target.objects.front().label == "B-spline 1" &&
                    converted->target.objects.front().kind ==
                        sketch::SketchObjectKind::BSpline &&
                    std::holds_alternative<sketch::BSplineEntity>(
                        converted->target.entities.front()) &&
                    sketch::entityId(converted->target.entities.front()) == curve &&
                    converted->target.constraints.size() == 1U &&
                    sketch::constraintId(converted->target.constraints.front()) ==
                        lock &&
                    sketch::validate(converted->target, {}).has_value(),
                "NURBS conversion lost identity or compatible constraints");
      });
}

} // namespace

int main() {
  try {
    const auto profile = kearne::testkit::propertyProfile();
    verifyCornerEdits(profile);
    verifyConstraintPolicy();
    verifyCompoundCornerIntent(profile);
    verifyOffsets(profile);
    verifyExtends(profile);
    verifyNurbsConversion(profile);
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
  }
}
