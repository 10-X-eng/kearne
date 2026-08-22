#include <kearne/adapters/occ_bspline.hpp>
#include <kearne/adapters/occ_curve_geometry.hpp>
#include <kearne/sketch/nurbs.hpp>
#include <kearne/testkit/property.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <numbers>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
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
  require(result.has_value(), "generated curve identity was invalid");
  return *result;
}

ContentDigest digest(std::uint64_t seed) {
  ContentDigest::Bytes bytes{};
  for (std::size_t index = 0U; index < bytes.size(); ++index)
    bytes[index] = static_cast<std::uint8_t>(seed + index * 17U);
  auto result = ContentDigest::fromBytes("blake3", bytes);
  require(result.has_value(), "generated curve digest was invalid");
  return *result;
}

sketch::LengthValue length(double value) {
  auto result = sketch::LengthValue::fromSi(value);
  require(result.has_value(), "generated curve length was invalid");
  return *result;
}

sketch::AngleValue angle(double value) {
  auto result = sketch::AngleValue::fromSi(value);
  require(result.has_value(), "generated curve angle was invalid");
  return *result;
}

sketch::DimensionlessValue scalar(double value) {
  auto result = sketch::DimensionlessValue::fromSi(value);
  require(result.has_value(), "generated curve scalar was invalid");
  return *result;
}

sketch::Point2 point(double x, double y) { return {length(x), length(y)}; }

adapters::TrimIdentities trimIdentities(SketchEntityId split,
                                        std::uint64_t seed) {
  return {split,
          {id<SketchConstraintId>(seed), id<SketchConstraintId>(seed + 1U)}};
}

double parameter(const adapters::CurveParameter &value) {
  return std::visit([](const auto &quantity) { return quantity.si(); }, value);
}

bool near(sketch::Point2 value, double x, double y, double tolerance = 1.0e-8) {
  return std::hypot(value.x.si() - x, value.y.si() - y) <= tolerance;
}

sketch::Point2 transformedPoint(double centerX, double centerY,
                                double rotation, double localX,
                                double localY) {
  const double cosine = std::cos(rotation);
  const double sine = std::sin(rotation);
  return point(centerX + cosine * localX - sine * localY,
               centerY + sine * localX + cosine * localY);
}

std::pair<double, double> transformedVector(double rotation, double localX,
                                            double localY) {
  const double cosine = std::cos(rotation);
  const double sine = std::sin(rotation);
  return {cosine * localX - sine * localY,
          sine * localX + cosine * localY};
}

double angleDifference(double first, double second) {
  return std::abs(std::remainder(first - second, 2.0 * std::numbers::pi));
}

double curveResidual(const sketch::Entity &curve, sketch::Point2 point) {
  return std::visit(
      [&](const auto &value) {
        using Type = std::decay_t<decltype(value)>;
        const double x = point.x.si();
        const double y = point.y.si();
        if constexpr (std::is_same_v<Type, sketch::LineEntity>) {
          const double dx = value.end.x.si() - value.start.x.si();
          const double dy = value.end.y.si() - value.start.y.si();
          return std::abs(dx * (y - value.start.y.si()) -
                          dy * (x - value.start.x.si())) /
                 std::hypot(dx, dy);
        } else if constexpr (std::is_same_v<Type, sketch::CircleEntity> ||
                             std::is_same_v<Type, sketch::ArcEntity>) {
          return std::abs(std::hypot(x - value.center.x.si(),
                                     y - value.center.y.si()) -
                          value.radius.si());
        } else if constexpr (std::is_same_v<Type, sketch::EllipseEntity> ||
                             std::is_same_v<Type,
                                            sketch::EllipticalArcEntity> ||
                             std::is_same_v<Type,
                                            sketch::HyperbolicArcEntity>) {
          const double cosine = std::cos(value.rotation.si());
          const double sine = std::sin(value.rotation.si());
          const double offsetX = x - value.center.x.si();
          const double offsetY = y - value.center.y.si();
          const double localX = cosine * offsetX + sine * offsetY;
          const double localY = -sine * offsetX + cosine * offsetY;
          const double first =
              std::pow(localX / value.majorRadius.si(), 2.0);
          const double second =
              std::pow(localY / value.minorRadius.si(), 2.0);
          const double implicit =
              std::is_same_v<Type, sketch::HyperbolicArcEntity>
                  ? first - second - 1.0
                  : first + second - 1.0;
          return std::abs(implicit) *
                 std::min(value.majorRadius.si(), value.minorRadius.si()) /
                 2.0;
        } else if constexpr (std::is_same_v<Type,
                                            sketch::ParabolicArcEntity>) {
          const double cosine = std::cos(value.rotation.si());
          const double sine = std::sin(value.rotation.si());
          const double offsetX = x - value.vertex.x.si();
          const double offsetY = y - value.vertex.y.si();
          const double localX = cosine * offsetX + sine * offsetY;
          const double localY = -sine * offsetX + cosine * offsetY;
          return std::abs(localY * localY -
                          4.0 * value.focalLength.si() * localX) /
                 std::max(2.0 * value.focalLength.si(), std::abs(localY));
        } else {
          return std::numeric_limits<double>::infinity();
        }
      },
      curve);
}

const sketch::Entity &entity(const sketch::Definition &definition,
                             SketchEntityId target) {
  const auto found =
      std::ranges::find(definition.entities, target, sketch::entityId);
  require(found != definition.entities.end(), "expected curve is missing");
  return *found;
}

void verifyProjectionCoverage() {
  const std::array<sketch::Entity, 8> curves{
      sketch::LineEntity{id<SketchEntityId>(1), point(0.0, 0.0),
                         point(2.0, 0.0)},
      sketch::CircleEntity{id<SketchEntityId>(2), point(0.0, 0.0), length(2.0)},
      sketch::ArcEntity{id<SketchEntityId>(3), point(0.0, 0.0), length(2.0),
                        angle(0.0), angle(std::numbers::pi / 2.0)},
      sketch::EllipseEntity{id<SketchEntityId>(4), point(0.0, 0.0),
                            length(3.0), length(1.0), angle(0.0)},
      sketch::EllipticalArcEntity{
          id<SketchEntityId>(5), point(0.0, 0.0), length(3.0), length(1.0),
          angle(0.0), angle(0.0), angle(std::numbers::pi / 2.0)},
      sketch::HyperbolicArcEntity{id<SketchEntityId>(6), point(0.0, 0.0),
                                  length(2.0), length(1.0), angle(0.0),
                                  scalar(-2.0), scalar(2.0)},
      sketch::ParabolicArcEntity{id<SketchEntityId>(7), point(0.0, 0.0),
                                 length(0.5), angle(0.0), length(-3.0),
                                 length(3.0)},
      sketch::BSplineEntity{id<SketchEntityId>(8),
                            {point(0.0, 0.0), point(2.0, 0.0)},
                            {scalar(0.0), scalar(0.0), scalar(1.0),
                             scalar(1.0)},
                            {scalar(1.0), scalar(1.0)},
                            1U}};
  const double hyperParameter = 0.5;
  const std::array queries{
      point(1.0, 1.0),
      point(3.0, 0.0),
      point(std::sqrt(2.0), std::sqrt(2.0)),
      point(4.0, 0.0),
      point(0.0, 1.0),
      point(2.0 * std::cosh(hyperParameter), std::sinh(hyperParameter)),
      point(0.5, 1.0),
      point(1.0, 1.0)};
  const std::array expectedParameters{
      0.5, 0.0, std::numbers::pi / 4.0, 0.0,
      std::numbers::pi / 2.0, hyperParameter, 1.0, 0.5};
  const std::array expectedDistances{1.0, 1.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
  for (std::size_t index = 0U; index < curves.size(); ++index) {
    auto projected = adapters::projectToCurve(curves[index], queries[index]);
    require(projected.has_value(), "valid curve projection was rejected");
    require(std::abs(parameter(projected->location.parameter) -
                     expectedParameters[index]) <= 1.0e-8 &&
                std::abs(projected->distance.si() - expectedDistances[index]) <=
                    1.0e-8,
            "curve projection lost its native parameter or distance");
  }
}

void verifyIntersectionCoverage() {
  const sketch::LineEntity vertical{id<SketchEntityId>(100), point(1.0, -5.0),
                                    point(1.0, 5.0)};
  const std::array<sketch::Entity, 6> curves{
      sketch::CircleEntity{id<SketchEntityId>(101), point(0.0, 0.0),
                           length(2.0)},
      sketch::ArcEntity{id<SketchEntityId>(102), point(0.0, 0.0), length(2.0),
                        angle(0.0), angle(std::numbers::pi)},
      sketch::EllipseEntity{id<SketchEntityId>(103), point(0.0, 0.0),
                            length(3.0), length(1.0), angle(0.0)},
      sketch::HyperbolicArcEntity{id<SketchEntityId>(104), point(0.0, 0.0),
                                  length(0.5), length(1.0), angle(0.0),
                                  scalar(-2.0), scalar(2.0)},
      sketch::ParabolicArcEntity{id<SketchEntityId>(105), point(0.0, 0.0),
                                 length(0.5), angle(0.0), length(-3.0),
                                 length(3.0)},
      sketch::BSplineEntity{id<SketchEntityId>(106),
                            {point(-2.0, 0.0), point(2.0, 0.0)},
                            {scalar(0.0), scalar(0.0), scalar(1.0),
                             scalar(1.0)},
                            {scalar(1.0), scalar(1.0)},
                            1U}};
  const std::array expectedCounts{2U, 1U, 2U, 2U, 2U, 1U};
  for (std::size_t index = 0U; index < curves.size(); ++index) {
    auto forward = adapters::intersectCurves(sketch::Entity{vertical},
                                             curves[index]);
    auto reverse = adapters::intersectCurves(curves[index],
                                             sketch::Entity{vertical});
    require(forward && reverse && !forward->overlapping &&
                !reverse->overlapping &&
                forward->points.size() == expectedCounts[index] &&
                reverse->points.size() == expectedCounts[index],
            "bounded curve intersection coverage diverged");
    for (const adapters::CurveIntersection &intersection : forward->points) {
      require(std::abs(intersection.point.x.si() - 1.0) <= 1.0e-8,
              "curve intersection point is not on the line");
      require(std::ranges::any_of(
                  reverse->points, [&](const auto &candidate) {
                    return near(candidate.point, intersection.point.x.si(),
                                intersection.point.y.si()) &&
                           std::abs(parameter(candidate.firstParameter) -
                                    parameter(intersection.secondParameter)) <=
                               1.0e-8 &&
                           std::abs(parameter(candidate.secondParameter) -
                                    parameter(intersection.firstParameter)) <=
                               1.0e-8;
                  }),
              "intersection input order changed geometric evidence");
    }
  }
}

void verifyNegativeSweepCoverage() {
  const sketch::Entity arc = sketch::ArcEntity{
      id<SketchEntityId>(200), point(0.0, 0.0), length(2.0),
      angle(std::numbers::pi / 2.0), angle(-std::numbers::pi / 2.0)};
  const sketch::Entity ellipticalArc = sketch::EllipticalArcEntity{
      id<SketchEntityId>(201), point(0.0, 0.0), length(3.0), length(1.0),
      angle(0.0), angle(std::numbers::pi / 2.0),
      angle(-std::numbers::pi / 2.0)};
  const sketch::Entity vertical = sketch::LineEntity{
      id<SketchEntityId>(202), point(1.0, -3.0), point(1.0, 3.0)};

  for (const auto &[curve, name] :
       {std::pair{arc, "clockwise arc lost an intersection"},
        std::pair{ellipticalArc,
                  "clockwise elliptical arc lost an intersection"}}) {
    auto result = adapters::intersectCurves(curve, vertical);
    require(result && !result->overlapping && result->points.size() == 2U,
            name);
    require(std::ranges::all_of(result->points, [](const auto &candidate) {
              const double native = parameter(candidate.firstParameter);
              return native >= -std::numbers::pi / 2.0 - 1.0e-8 &&
                     native <= std::numbers::pi / 2.0 + 1.0e-8;
            }),
            "clockwise curve sweep returned an out-of-span parameter");
  }
}

void verifyGeneratedLineIntersections(
    const testkit::PropertyProfile &profile) {
  testkit::checkProperty(
      "translated line intersections preserve native parameters", profile,
      [](testkit::Random &random, std::uint64_t index) {
        const double x = random.between(-1'000.0, 1'000.0);
        const double y = random.between(-1'000.0, 1'000.0);
        const double span = random.between(1.0e-4, 100.0);
        const sketch::Entity horizontal = sketch::LineEntity{
            id<SketchEntityId>(1'000U + index * 2U), point(x - span, y),
            point(x + span, y)};
        const sketch::Entity vertical = sketch::LineEntity{
            id<SketchEntityId>(1'001U + index * 2U), point(x, y - span),
            point(x, y + span)};
        auto result = adapters::intersectCurves(horizontal, vertical);
        require(result && !result->overlapping && result->points.size() == 1U,
                "valid line intersection was rejected");
        require(near(result->points.front().point, x, y, 1.0e-7) &&
                    std::abs(parameter(result->points.front().firstParameter) -
                             0.5) <= 1.0e-8 &&
                    std::abs(parameter(result->points.front().secondParameter) -
                             0.5) <= 1.0e-8,
                "line intersection changed under translation or scale");
      });
}

void verifyGeneratedConicOperations(const testkit::PropertyProfile &profile) {
  testkit::checkProperty(
      "conic operations preserve geometry under rigid transforms", profile,
      [](testkit::Random &random, std::uint64_t index) {
        const double centerX = random.between(-1'000.0, 1'000.0);
        const double centerY = random.between(-1'000.0, 1'000.0);
        const double rotation =
            random.between(-std::numbers::pi, std::numbers::pi);
        const double major = random.between(1.0e-3, 100.0);
        const double minor = major * random.between(0.3, 1.0);
        const double native = random.between(-1.25, 1.25);

        std::optional<sketch::Entity> conic;
        sketch::Point2 expected = point(0.0, 0.0);
        std::pair<double, double> tangent;
        bool angular = false;
        switch (index % 3U) {
        case 0U:
          conic = sketch::EllipseEntity{
              id<SketchEntityId>(20'000U + index * 2U),
              point(centerX, centerY), length(major), length(minor),
              angle(rotation)};
          expected = transformedPoint(centerX, centerY, rotation,
                                      major * std::cos(native),
                                      minor * std::sin(native));
          tangent = transformedVector(rotation, -major * std::sin(native),
                                      minor * std::cos(native));
          angular = true;
          break;
        case 1U:
          conic = sketch::HyperbolicArcEntity{
              id<SketchEntityId>(20'000U + index * 2U),
              point(centerX, centerY), length(major), length(minor),
              angle(rotation), scalar(-1.5), scalar(1.5)};
          expected = transformedPoint(centerX, centerY, rotation,
                                      major * std::cosh(native),
                                      minor * std::sinh(native));
          tangent = transformedVector(rotation, major * std::sinh(native),
                                      minor * std::cosh(native));
          break;
        default:
          conic = sketch::ParabolicArcEntity{
              id<SketchEntityId>(20'000U + index * 2U),
              point(centerX, centerY), length(major), angle(rotation),
              length(-2.0 * major), length(2.0 * major)};
          expected = transformedPoint(centerX, centerY, rotation,
                                      native * native * major / 4.0,
                                      native * major);
          tangent = transformedVector(rotation, native / 2.0, 1.0);
          break;
        }

        auto projected = adapters::projectToCurve(*conic, expected);
        require(projected &&
                    near(projected->location.point, expected.x.si(),
                         expected.y.si(), 1.0e-7) &&
                    projected->distance.si() <= 1.0e-7,
                "exact conic point did not project onto itself");
        const double expectedParameter =
            index % 3U == 2U ? native * major : native;
        const double projectedParameter =
            parameter(projected->location.parameter);
        require(angular ? angleDifference(projectedParameter,
                                          expectedParameter) <= 1.0e-7
                        : std::abs(projectedParameter - expectedParameter) <=
                              1.0e-7 * std::max(1.0, major),
                "conic projection changed its native parameter");

        const double tangentLength = std::hypot(tangent.first, tangent.second);
        const double normalX = -tangent.second / tangentLength;
        const double normalY = tangent.first / tangentLength;
        const double span = std::min(major, minor) * 0.02;
        const sketch::Entity crossing = sketch::LineEntity{
            id<SketchEntityId>(20'001U + index * 2U),
            point(expected.x.si() - normalX * span,
                  expected.y.si() - normalY * span),
            point(expected.x.si() + normalX * span,
                  expected.y.si() + normalY * span)};
        auto intersections = adapters::intersectCurves(*conic, crossing);
        require(intersections && !intersections->overlapping &&
                    std::ranges::any_of(
                        intersections->points, [&](const auto &candidate) {
                          return near(candidate.point, expected.x.si(),
                                      expected.y.si(), 1.0e-7) &&
                                 std::abs(parameter(candidate.secondParameter) -
                                          0.5) <= 1.0e-7;
                        }),
                "transformed conic lost a known crossing");
      });
}

void verifyTrimLifecycle() {
  const SketchEntityId target = id<SketchEntityId>(30'000);
  const SketchEntityId firstBoundary = id<SketchEntityId>(30'001);
  const SketchEntityId secondBoundary = id<SketchEntityId>(30'002);
  const SketchEntityId split = id<SketchEntityId>(30'003);
  const SketchObjectId object = id<SketchObjectId>(30'004);
  sketch::Definition definition{
      digest(30'000),
      {{object, "Line 1", sketch::SketchObjectKind::Line,
        {{"curve", target}}}},
      {sketch::LineEntity{target, point(-3.0, 0.0), point(3.0, 0.0)},
       sketch::LineEntity{firstBoundary, point(-1.0, -1.0),
                          point(-1.0, 1.0)},
       sketch::LineEntity{secondBoundary, point(1.0, -1.0),
                          point(1.0, 1.0)}},
      {}};
  auto preview =
      adapters::previewTrim(definition, {target, point(0.0, 0.0)});
  require(preview && !preview->deletesCurve &&
              preview->boundaries.size() == 2U &&
              near(preview->boundaries[0].point, -1.0, 0.0) &&
              near(preview->boundaries[1].point, 1.0, 0.0),
          "Trim preview did not identify the selected segment boundaries");
  auto trimmed = adapters::trimCurve(
      definition, {{target, point(0.0, 0.0)},
                   trimIdentities(split, 30'100),
                   sketch::ExternalConstraintPolicy::Refuse});
  require(trimmed && trimmed->target.entities.size() == 4U &&
              trimmed->target.objects.size() == 1U &&
              trimmed->target.objects.front().kind ==
                  sketch::SketchObjectKind::CurveGroup &&
              trimmed->target.objects.front().label == "Line 1 (modified)" &&
              trimmed->target.objects.front().members.size() == 2U &&
              trimmed->target.objects.front().members[0].role == "curve" &&
              trimmed->target.objects.front().members[1].role ==
                  "curve_part_2" &&
              trimmed->target.constraints.size() == 2U &&
              std::ranges::all_of(
                  trimmed->target.constraints,
                  [&](const sketch::Constraint &constraint) {
                    const auto *point =
                        std::get_if<sketch::PointOnObject>(&constraint);
                    return point &&
                           (point->curve == firstBoundary ||
                            point->curve == secondBoundary) &&
                           (point->point == sketch::PointRef{
                                                target, sketch::PointKey::End} ||
                            point->point == sketch::PointRef{
                                                split,
                                                sketch::PointKey::Start});
                  }),
          "line Trim lost source identity or human ownership");
  const auto &first = std::get<sketch::LineEntity>(entity(trimmed->target,
                                                          target));
  const auto &second =
      std::get<sketch::LineEntity>(entity(trimmed->target, split));
  require(near(first.start, -3.0, 0.0) && near(first.end, -1.0, 0.0) &&
              near(second.start, 1.0, 0.0) && near(second.end, 3.0, 0.0),
          "line Trim retained the selected segment");

  sketch::Definition constrained = definition;
  constrained.constraints.push_back(sketch::Horizontal{
      id<SketchConstraintId>(30'005), target});
  require(!adapters::trimCurve(
              constrained,
              {{target, point(0.0, 0.0)},
               trimIdentities(split, 30'110),
               sketch::ExternalConstraintPolicy::Refuse}),
          "Trim silently removed a source constraint");
  auto detached = adapters::trimCurve(
      constrained, {{target, point(0.0, 0.0)},
                    trimIdentities(split, 30'120),
                    sketch::ExternalConstraintPolicy::Detach});
  require(detached && detached->target.constraints.size() == 2U &&
              std::ranges::all_of(
                  detached->target.constraints,
                  [](const sketch::Constraint &constraint) {
                    return std::holds_alternative<sketch::PointOnObject>(
                        constraint);
                  }),
          "Trim did not replace detached constraints with boundary intent");

  sketch::Definition isolated{
      digest(30'010),
      {{id<SketchObjectId>(30'011), "Line 1", sketch::SketchObjectKind::Line,
        {{"curve", target}}}},
      {sketch::LineEntity{target, point(-3.0, 0.0), point(3.0, 0.0)}},
      {}};
  auto deletePreview =
      adapters::previewTrim(isolated, {target, point(0.0, 0.0)});
  require(deletePreview && deletePreview->deletesCurve &&
              deletePreview->boundaries.empty(),
          "unbounded Trim preview did not expose curve deletion");
  auto deleted = adapters::trimCurve(
      isolated, {{target, point(0.0, 0.0)},
                 trimIdentities(split, 30'130),
                 sketch::ExternalConstraintPolicy::Refuse});
  require(deleted && deleted->target.entities.empty() &&
              deleted->target.objects.empty(),
          "unbounded Trim did not delete the selected curve and owner");
}

void verifyClosedCurveTrim() {
  const SketchEntityId circle = id<SketchEntityId>(31'000);
  const SketchEntityId boundary = id<SketchEntityId>(31'001);
  const SketchObjectId object = id<SketchObjectId>(31'002);
  const sketch::Definition definition{
      digest(31'000),
      {{object, "Circle 1", sketch::SketchObjectKind::Circle,
        {{"curve", circle}}}},
      {sketch::CircleEntity{circle, point(0.0, 0.0), length(2.0)},
       sketch::LineEntity{boundary, point(0.0, -3.0), point(0.0, 3.0)}},
      {}};
  auto trimmed = adapters::trimCurve(
      definition, {{circle, point(2.0, 0.0)},
                   trimIdentities(id<SketchEntityId>(31'003), 31'100),
                   sketch::ExternalConstraintPolicy::Refuse});
  require(trimmed && trimmed->target.entities.size() == 2U &&
              trimmed->target.objects.front().kind ==
                  sketch::SketchObjectKind::CurveGroup &&
              trimmed->target.objects.front().members.size() == 1U,
          "closed Trim lost its surviving object ancestry");
  const auto &arc =
      std::get<sketch::ArcEntity>(entity(trimmed->target, circle));
  require(std::abs(arc.startAngle.si() - std::numbers::pi / 2.0) <=
                  1.0e-8 &&
              std::abs(arc.endAngle.si() -
                       3.0 * std::numbers::pi / 2.0) <= 1.0e-8,
          "closed Trim retained the selected circular segment");
  auto ambiguous = adapters::trimCurve(
      definition, {{circle, point(0.0, 2.0)},
                   trimIdentities(id<SketchEntityId>(31'004), 31'110),
                   sketch::ExternalConstraintPolicy::Refuse});
  require(!ambiguous &&
              ambiguous.error().code == "sketch.trim.ambiguous-pick",
          "Trim accepted a pick on an intersection");

  const SketchEntityId ellipse = id<SketchEntityId>(31'010);
  const sketch::Definition ellipseDefinition{
      digest(31'010),
      {{id<SketchObjectId>(31'011), "Ellipse 1",
        sketch::SketchObjectKind::Ellipse, {{"curve", ellipse}}}},
      {sketch::EllipseEntity{ellipse, point(4.0, -2.0), length(3.0),
                             length(1.0), angle(0.6)},
       sketch::LineEntity{id<SketchEntityId>(31'012), point(4.0, -6.0),
                          point(4.0, 2.0)}},
      {}};
  auto ellipseTrim = adapters::trimCurve(
      ellipseDefinition,
      {{ellipse, point(7.0, -2.0)},
       trimIdentities(id<SketchEntityId>(31'013), 31'120),
       sketch::ExternalConstraintPolicy::Refuse});
  require(ellipseTrim &&
              std::holds_alternative<sketch::EllipticalArcEntity>(
                  entity(ellipseTrim->target, ellipse)),
          "rotated ellipse Trim did not produce an elliptical arc");
}

void verifyPeriodicBSplineTrim() {
  const SketchEntityId spline = id<SketchEntityId>(32'000);
  auto created = adapters::createBSpline(
      {spline,
       {point(-2.0, 0.0), point(0.0, 2.0), point(2.0, 0.0),
        point(0.0, -2.0)},
       adapters::BSplineCreation::ControlPoints,
       3U,
       true});
  require(created.has_value(), "periodic Trim fixture creation failed");
  const sketch::Definition definition{
      digest(32'000),
      {{id<SketchObjectId>(32'001), "B-spline 1",
        sketch::SketchObjectKind::BSpline, {{"curve", spline}}}},
      {*created,
       sketch::LineEntity{id<SketchEntityId>(32'002), point(-3.0, 0.0),
                          point(3.0, 0.0)}},
      {}};
  auto trimmed = adapters::trimCurve(
      definition, {{spline, point(0.0, 2.0)},
                   trimIdentities(id<SketchEntityId>(32'003), 32'100),
                   sketch::ExternalConstraintPolicy::Refuse});
  require(trimmed && trimmed->target.entities.size() == 2U,
          "periodic B-spline Trim failed");
  const auto &retained =
      std::get<sketch::BSplineEntity>(entity(trimmed->target, spline));
  require(!retained.periodic && sketch::validate(trimmed->target, {}).has_value(),
          "periodic B-spline Trim did not produce one valid open segment");
}

void verifySplitLifecycle() {
  const SketchEntityId target = id<SketchEntityId>(35'000);
  const SketchEntityId second = id<SketchEntityId>(35'001);
  const SketchObjectId object = id<SketchObjectId>(35'002);
  const SketchConstraintId seam = id<SketchConstraintId>(35'003);
  sketch::Definition definition{
      digest(35'000),
      {{object, "Line 1", sketch::SketchObjectKind::Line,
        {{"curve", target}}}},
      {sketch::LineEntity{target, point(-4.0, 0.0), point(4.0, 0.0)}},
      {}};
  auto preview =
      adapters::previewSplit(definition, {target, point(1.0, 0.2)});
  require(preview && near(preview->point, 1.0, 0.0),
          "Split preview did not project to the exact curve location");
  auto split = adapters::splitCurve(
      definition,
      {{target, point(1.0, 0.2)}, {second, seam},
       sketch::ExternalConstraintPolicy::Refuse});
  require(split && split->target.entities.size() == 2U &&
              split->target.objects.size() == 1U &&
              split->target.objects.front().kind ==
                  sketch::SketchObjectKind::CurveGroup &&
              split->target.objects.front().label == "Line 1 (modified)" &&
              split->target.objects.front().members.size() == 2U &&
              split->target.objects.front().members[1].role ==
                  "curve_part_2" &&
              split->target.constraints.size() == 1U,
          "Split lost curve ancestry or human ownership");
  const auto *joined =
      std::get_if<sketch::Coincident>(&split->target.constraints.front());
  auto firstEnd =
      sketch::resolvePoint(split->target, {target, sketch::PointKey::End});
  auto secondStart =
      sketch::resolvePoint(split->target, {second, sketch::PointKey::Start});
  require(joined && joined->id == seam && firstEnd && secondStart &&
              near(*firstEnd, 1.0, 0.0) && *firstEnd == *secondStart,
          "Split did not preserve its exact seam intent");
  require(!adapters::previewSplit(definition, {target, point(-4.0, 0.0)}),
          "Split accepted an existing endpoint");

  definition.constraints.push_back(
      sketch::Horizontal{id<SketchConstraintId>(35'004), target});
  require(!adapters::splitCurve(
              definition,
              {{target, point(1.0, 0.0)}, {second, seam},
               sketch::ExternalConstraintPolicy::Refuse}),
          "Split silently removed a source constraint");
  auto detached = adapters::splitCurve(
      definition,
      {{target, point(1.0, 0.0)}, {second, seam},
       sketch::ExternalConstraintPolicy::Detach});
  require(detached && detached->target.constraints.size() == 1U &&
              std::holds_alternative<sketch::Coincident>(
                  detached->target.constraints.front()),
          "Split did not apply its explicit constraint policy");
}

void verifyClosedCurveSplit() {
  const SketchEntityId target = id<SketchEntityId>(36'000);
  const SketchObjectId object = id<SketchObjectId>(36'001);
  const SketchConstraintId seam = id<SketchConstraintId>(36'002);
  sketch::Definition definition{
      digest(36'000),
      {{object, "Circle 1", sketch::SketchObjectKind::Circle,
        {{"curve", target}}}},
      {sketch::CircleEntity{target, point(0.0, 0.0), length(2.0)}},
      {}};
  auto split = adapters::splitCurve(
      definition,
      {{target, point(0.0, 2.0)},
       {id<SketchEntityId>(36'003), seam},
       sketch::ExternalConstraintPolicy::Refuse});
  require(split && split->target.entities.size() == 1U &&
              split->target.objects.front().kind ==
                  sketch::SketchObjectKind::CurveGroup &&
              split->target.constraints.size() == 1U &&
              std::holds_alternative<sketch::ArcEntity>(
                  split->target.entities.front()),
          "closed Split did not create one open curve with a seam");
  const auto &arc = std::get<sketch::ArcEntity>(split->target.entities.front());
  require(std::abs(arc.startAngle.si() - std::numbers::pi / 2.0) <= 1.0e-8 &&
              std::abs(arc.endAngle.si() - arc.startAngle.si() -
                       2.0 * std::numbers::pi) <= 1.0e-8 &&
              sketch::validate(split->target, {}).has_value(),
          "closed Split changed its selected seam or produced invalid geometry");

  const SketchEntityId spline = id<SketchEntityId>(36'010);
  auto created = adapters::createBSpline(
      {spline,
       {point(-2.0, 0.0), point(0.0, 2.0), point(2.0, 0.0),
        point(0.0, -2.0)},
       adapters::BSplineCreation::ControlPoints,
       3U,
       true});
  require(created.has_value(), "periodic Split fixture creation failed");
  sketch::Definition periodic{
      digest(36'010),
      {{id<SketchObjectId>(36'011), "B-spline 1",
        sketch::SketchObjectKind::BSpline, {{"curve", spline}}}},
      {*created},
      {}};
  auto splineSplit = adapters::splitCurve(
      periodic,
      {{spline, point(0.0, 2.0)},
       {id<SketchEntityId>(36'012), id<SketchConstraintId>(36'013)},
       sketch::ExternalConstraintPolicy::Refuse});
  require(splineSplit && splineSplit->target.entities.size() == 1U &&
              !std::get<sketch::BSplineEntity>(
                   splineSplit->target.entities.front())
                   .periodic &&
              sketch::validate(splineSplit->target, {}).has_value(),
          "periodic B-spline Split did not create one valid open curve");
}

void verifyGeneratedOpenSplit(const testkit::PropertyProfile &profile) {
  testkit::checkProperty(
      "open Split preserves exact transformed seams", profile,
      [](testkit::Random &random, std::uint64_t index) {
        const double centerX = random.between(-1'000.0, 1'000.0);
        const double centerY = random.between(-1'000.0, 1'000.0);
        const double rotation =
            random.between(-std::numbers::pi, std::numbers::pi);
        const double major = random.between(0.01, 100.0);
        const double minor = major * random.between(0.3, 0.9);
        const bool reversed = (index & 1U) != 0U;
        const SketchEntityId target =
            id<SketchEntityId>(90'000U + index * 4U);
        const SketchEntityId second =
            id<SketchEntityId>(90'001U + index * 4U);
        const SketchObjectId object =
            id<SketchObjectId>(90'002U + index * 4U);
        const SketchConstraintId seam =
            id<SketchConstraintId>(90'003U + index * 4U);
        sketch::SketchObjectKind kind = sketch::SketchObjectKind::Arc;
        std::optional<sketch::Entity> curve;
        sketch::Point2 selected = point(centerX, centerY);
        switch (index % 5U) {
        case 0U:
          selected = transformedPoint(centerX, centerY, 0.0, major, 0.0);
          curve = sketch::ArcEntity{
              target, point(centerX, centerY), length(major),
              angle(reversed ? 1.2 : -1.2), angle(reversed ? -1.2 : 1.2)};
          break;
        case 1U:
          kind = sketch::SketchObjectKind::EllipticalArc;
          selected =
              transformedPoint(centerX, centerY, rotation, major, 0.0);
          curve = sketch::EllipticalArcEntity{
              target, point(centerX, centerY), length(major), length(minor),
              angle(rotation), angle(reversed ? 1.2 : -1.2),
              angle(reversed ? -1.2 : 1.2)};
          break;
        case 2U:
          kind = sketch::SketchObjectKind::HyperbolicArc;
          selected =
              transformedPoint(centerX, centerY, rotation, major, 0.0);
          curve = sketch::HyperbolicArcEntity{
              target, point(centerX, centerY), length(major), length(minor),
              angle(rotation), scalar(reversed ? 1.2 : -1.2),
              scalar(reversed ? -1.2 : 1.2)};
          break;
        case 3U:
          kind = sketch::SketchObjectKind::ParabolicArc;
          selected = point(centerX, centerY);
          curve = sketch::ParabolicArcEntity{
              target, point(centerX, centerY), length(major), angle(rotation),
              length(reversed ? major : -major),
              length(reversed ? -major : major)};
          break;
        default: {
          kind = sketch::SketchObjectKind::BSpline;
          const sketch::Point2 start =
              transformedPoint(centerX, centerY, rotation, -major, 0.0);
          const sketch::Point2 end =
              transformedPoint(centerX, centerY, rotation, major, 0.0);
          selected = point(std::midpoint(start.x.si(), end.x.si()),
                           std::midpoint(start.y.si(), end.y.si()));
          curve = sketch::BSplineEntity{
              target,
              {start, end},
              {scalar(0.0), scalar(0.0), scalar(1.0), scalar(1.0)},
              {scalar(1.0), scalar(1.0)},
              1U};
          break;
        }
        }
        sketch::Definition definition{
            digest(90'000U + index),
            {{object, "Curve 1", kind, {{"curve", target}}}},
            {*curve},
            {}};
        auto split = adapters::splitCurve(
            definition,
            {{target, selected}, {second, seam},
             sketch::ExternalConstraintPolicy::Refuse});
        require(split && split->target.entities.size() == 2U &&
                    split->target.constraints.size() == 1U &&
                    sketch::validate(split->target, {}).has_value(),
                "generated Split produced an invalid topology edit");
        auto firstEnd = sketch::resolvePoint(
            split->target, {target, sketch::PointKey::End});
        auto secondStart = sketch::resolvePoint(
            split->target, {second, sketch::PointKey::Start});
        require(firstEnd && secondStart &&
                    near(*firstEnd, selected.x.si(), selected.y.si(), 1.0e-7) &&
                    near(*secondStart, selected.x.si(), selected.y.si(),
                         1.0e-7) &&
                    near(*firstEnd, secondStart->x.si(), secondStart->y.si(),
                         1.0e-9),
                "generated Split changed its exact seam location");
      });
}

void verifyJoinLifecycle() {
  const SketchEntityId first = id<SketchEntityId>(37'000);
  const SketchEntityId second = id<SketchEntityId>(37'001);
  const SketchObjectId joinedObject = id<SketchObjectId>(37'004);
  const SketchConstraintId seam = id<SketchConstraintId>(37'005);
  const SketchConstraintId farLock = id<SketchConstraintId>(37'006);
  sketch::Definition definition{
      digest(37'000),
      {{id<SketchObjectId>(37'002), "Line 1", sketch::SketchObjectKind::Line,
        {{"curve", first}}},
       {id<SketchObjectId>(37'003), "Line 2", sketch::SketchObjectKind::Line,
        {{"curve", second}}}},
      {sketch::LineEntity{first, point(-2.0, 0.0), point(0.0, 0.0)},
       sketch::LineEntity{second, point(0.0, 0.0), point(0.0, 2.0)}},
      {sketch::Coincident{seam,
                          {first, sketch::PointKey::End},
                          {second, sketch::PointKey::Start}},
       sketch::Lock{farLock, {second, sketch::PointKey::End},
                    point(0.0, 2.0)}}};
  auto joined = adapters::joinCurves(
      definition,
      {{first, sketch::PointKey::End}, {second, sketch::PointKey::Start},
       joinedObject, sketch::ExternalConstraintPolicy::Refuse});
  require(joined && joined->target.entities.size() == 1U &&
              joined->target.objects.size() == 1U &&
              joined->target.objects.front().id == joinedObject &&
              joined->target.objects.front().label == "Joined curve 1" &&
              joined->target.objects.front().kind ==
                  sketch::SketchObjectKind::JoinedCurve &&
              joined->target.objects.front().members.front().entity == first &&
              joined->target.constraints.size() == 1U &&
              std::holds_alternative<sketch::BSplineEntity>(
                  joined->target.entities.front()),
          "Join lost stable identity or human ownership");
  const auto &curve =
      std::get<sketch::BSplineEntity>(joined->target.entities.front());
  const auto *lock =
      std::get_if<sketch::Lock>(&joined->target.constraints.front());
  require(near(curve.controlPoints.front(), -2.0, 0.0) &&
              near(curve.controlPoints.back(), 0.0, 2.0) && lock &&
              lock->id == farLock &&
              lock->point == sketch::PointRef{first, sketch::PointKey::End} &&
              sketch::validate(joined->target, {}).has_value(),
          "Join did not remap the far endpoint constraint");

  definition.constraints.push_back(
      sketch::Horizontal{id<SketchConstraintId>(37'007), first});
  require(!adapters::joinCurves(
              definition,
              {{first, sketch::PointKey::End},
               {second, sketch::PointKey::Start}, joinedObject,
               sketch::ExternalConstraintPolicy::Refuse}),
          "Join silently removed an incompatible constraint");
  auto detached = adapters::joinCurves(
      definition,
      {{first, sketch::PointKey::End}, {second, sketch::PointKey::Start},
       joinedObject, sketch::ExternalConstraintPolicy::Detach});
  require(detached && detached->target.constraints.size() == 1U &&
              std::holds_alternative<sketch::Lock>(
                  detached->target.constraints.front()),
          "Join did not apply its explicit constraint policy");
}

void verifyMixedCurveJoins() {
  for (std::size_t index = 0U; index < 5U; ++index) {
    const SketchEntityId first = id<SketchEntityId>(38'000U + index * 4U);
    const SketchEntityId second = id<SketchEntityId>(38'001U + index * 4U);
    sketch::SketchObjectKind secondKind = sketch::SketchObjectKind::Arc;
    std::optional<sketch::Entity> secondCurve;
    switch (index) {
    case 0U:
      secondCurve = sketch::ArcEntity{
          second, point(0.0, 1.0), length(1.0),
          angle(-std::numbers::pi / 2.0), angle(0.0)};
      break;
    case 1U:
      secondKind = sketch::SketchObjectKind::EllipticalArc;
      secondCurve = sketch::EllipticalArcEntity{
          second, point(0.0, 1.0), length(2.0), length(1.0), angle(0.0),
          angle(-std::numbers::pi / 2.0), angle(0.0)};
      break;
    case 2U:
      secondKind = sketch::SketchObjectKind::HyperbolicArc;
      secondCurve = sketch::HyperbolicArcEntity{
          second, point(-1.0, 0.0), length(1.0), length(0.5), angle(0.0),
          scalar(0.0), scalar(1.0)};
      break;
    case 3U:
      secondKind = sketch::SketchObjectKind::ParabolicArc;
      secondCurve = sketch::ParabolicArcEntity{
          second, point(0.0, 0.0), length(0.5), angle(0.0), length(0.0),
          length(1.0)};
      break;
    default:
      secondKind = sketch::SketchObjectKind::BSpline;
      secondCurve = sketch::BSplineEntity{
          second,
          {point(0.0, 0.0), point(1.0, 1.0)},
          {scalar(0.0), scalar(0.0), scalar(1.0), scalar(1.0)},
          {scalar(1.0), scalar(1.0)},
          1U};
      break;
    }
    sketch::Definition definition{
        digest(38'000U + index),
        {{id<SketchObjectId>(38'002U + index * 4U), "Line 1",
          sketch::SketchObjectKind::Line, {{"curve", first}}},
         {id<SketchObjectId>(38'003U + index * 4U), "Curve 1", secondKind,
          {{"curve", second}}}},
        {sketch::LineEntity{first, point(-1.0, 0.0), point(0.0, 0.0)},
         *secondCurve},
        {}};
    auto joined = adapters::joinCurves(
        definition,
        {{first, sketch::PointKey::End}, {second, sketch::PointKey::Start},
         id<SketchObjectId>(39'000U + index),
         sketch::ExternalConstraintPolicy::Refuse});
    require(joined && joined->target.entities.size() == 1U &&
                joined->target.objects.front().kind ==
                    sketch::SketchObjectKind::JoinedCurve &&
                sketch::validate(joined->target, {}).has_value(),
            "mixed analytic Join failed");
  }
}

void verifyGeneratedLineJoins(const testkit::PropertyProfile &profile) {
  testkit::checkProperty(
      "line Join preserves endpoint orientation", profile,
      [](testkit::Random &random, std::uint64_t index) {
        const sketch::Point2 seam =
            point(random.between(-1'000.0, 1'000.0),
                  random.between(-1'000.0, 1'000.0));
        const sketch::Point2 firstFar =
            point(seam.x.si() + random.between(0.01, 100.0),
                  seam.y.si() + random.between(-100.0, -0.01));
        const sketch::Point2 secondFar =
            point(seam.x.si() + random.between(-100.0, -0.01),
                  seam.y.si() + random.between(0.01, 100.0));
        const bool firstAtStart = (index & 1U) != 0U;
        const bool secondAtEnd = (index & 2U) != 0U;
        const SketchEntityId first =
            id<SketchEntityId>(100'000U + index * 4U);
        const SketchEntityId second =
            id<SketchEntityId>(100'001U + index * 4U);
        const sketch::PointKey firstKey = firstAtStart
                                              ? sketch::PointKey::Start
                                              : sketch::PointKey::End;
        const sketch::PointKey secondKey = secondAtEnd
                                               ? sketch::PointKey::End
                                               : sketch::PointKey::Start;
        sketch::Definition definition{
            digest(100'000U + index),
            {{id<SketchObjectId>(100'002U + index * 4U), "Line 1",
              sketch::SketchObjectKind::Line, {{"curve", first}}},
             {id<SketchObjectId>(100'003U + index * 4U), "Line 2",
              sketch::SketchObjectKind::Line, {{"curve", second}}}},
            {sketch::LineEntity{first,
                                firstAtStart ? seam : firstFar,
                                firstAtStart ? firstFar : seam},
             sketch::LineEntity{second,
                                secondAtEnd ? secondFar : seam,
                                secondAtEnd ? seam : secondFar}},
            {}};
        auto joined = adapters::joinCurves(
            definition,
            {{first, firstKey}, {second, secondKey},
             id<SketchObjectId>(110'000U + index),
             sketch::ExternalConstraintPolicy::Refuse});
        require(joined && joined->target.entities.size() == 1U &&
                    sketch::validate(joined->target, {}).has_value(),
                "generated line Join produced invalid geometry");
        const auto &curve =
            std::get<sketch::BSplineEntity>(joined->target.entities.front());
        require(near(curve.controlPoints.front(), firstFar.x.si(),
                     firstFar.y.si(), 1.0e-8) &&
                    near(curve.controlPoints.back(), secondFar.x.si(),
                         secondFar.y.si(), 1.0e-8),
                "generated line Join changed endpoint ancestry");
      });
}

void verifyGeneratedNurbsConversions(
    const testkit::PropertyProfile &profile) {
  testkit::checkProperty(
      "analytic curves convert to exact NURBS", profile,
      [](testkit::Random &random, std::uint64_t index) {
        const std::uint64_t seed = 120'000U + index * 3U;
        const SketchEntityId curveId = id<SketchEntityId>(seed);
        const SketchObjectId objectId = id<SketchObjectId>(seed + 1U);
        const double centerX = random.between(-100.0, 100.0);
        const double centerY = random.between(-100.0, 100.0);
        const double rotation =
            random.between(-std::numbers::pi, std::numbers::pi);
        const double major = random.between(0.1, 20.0);
        const double minor = random.between(0.05, major);
        const double first = random.between(-1.5, 0.5);
        const double span = random.between(0.2, 2.5);
        const bool construction = (random.next() & 1U) != 0U;
        sketch::SketchObjectKind kind = sketch::SketchObjectKind::Line;
        std::optional<sketch::Entity> curve;
        bool periodic = false;
        switch (index % 7U) {
        case 0U: {
          const sketch::Point2 start = point(centerX, centerY);
          const sketch::Point2 end =
              point(centerX + major * std::cos(rotation),
                    centerY + major * std::sin(rotation));
          curve = sketch::LineEntity{curveId, start, end, construction};
          break;
        }
        case 1U: {
          kind = sketch::SketchObjectKind::Circle;
          periodic = true;
          curve = sketch::CircleEntity{curveId, point(centerX, centerY),
                                       length(major), construction};
          break;
        }
        case 2U:
          kind = sketch::SketchObjectKind::Arc;
          curve = sketch::ArcEntity{curveId, point(centerX, centerY),
                                    length(major), angle(first),
                                    angle(first + span), construction};
          break;
        case 3U: {
          kind = sketch::SketchObjectKind::Ellipse;
          periodic = true;
          curve = sketch::EllipseEntity{curveId, point(centerX, centerY),
                                        length(major), length(minor),
                                        angle(rotation), construction};
          break;
        }
        case 4U:
          kind = sketch::SketchObjectKind::EllipticalArc;
          curve = sketch::EllipticalArcEntity{
              curveId, point(centerX, centerY), length(major), length(minor),
              angle(rotation), angle(first), angle(first + span), construction};
          break;
        case 5U:
          kind = sketch::SketchObjectKind::HyperbolicArc;
          curve = sketch::HyperbolicArcEntity{
              curveId, point(centerX, centerY), length(major), length(minor),
              angle(rotation), scalar(first), scalar(first + span),
              construction};
          break;
        default:
          kind = sketch::SketchObjectKind::ParabolicArc;
          curve = sketch::ParabolicArcEntity{
              curveId, point(centerX, centerY), length(major), angle(rotation),
              length(first), length(first + span), construction};
          break;
        }
        sketch::Definition definition{
            digest(seed),
            {{objectId, "Curve 1", kind, {{"curve", curveId}}}},
            {*curve},
            {}};
        auto converted = adapters::convertToNurbs(
            definition,
            {curveId, sketch::ExternalConstraintPolicy::Refuse});
        require(converted && converted->target.objects.size() == 1U &&
                    converted->target.objects.front().id == objectId &&
                    converted->target.objects.front().label == "B-spline 1" &&
                    converted->target.objects.front().kind ==
                        sketch::SketchObjectKind::BSpline &&
                    converted->target.entities.size() == 1U &&
                    sketch::entityId(converted->target.entities.front()) ==
                        curveId &&
                    std::holds_alternative<sketch::BSplineEntity>(
                        converted->target.entities.front()) &&
                    std::get<sketch::BSplineEntity>(
                        converted->target.entities.front())
                            .periodic == periodic &&
                    sketch::validate(converted->target, {}).has_value(),
                "exact NURBS conversion lost identity or curve state");
        const auto &spline = std::get<sketch::BSplineEntity>(
            converted->target.entities.front());
        std::vector<double> controlPoints;
        std::vector<double> knots;
        std::vector<double> weights;
        controlPoints.reserve(spline.controlPoints.size() * 2U);
        knots.reserve(spline.knots.size());
        weights.reserve(spline.weights.size());
        for (const sketch::Point2 pole : spline.controlPoints) {
          controlPoints.push_back(pole.x.si());
          controlPoints.push_back(pole.y.si());
        }
        for (const auto knot : spline.knots)
          knots.push_back(knot.si());
        for (const auto weight : spline.weights)
          weights.push_back(weight.si());
        const sketch::NurbsView view{controlPoints, knots, weights,
                                     spline.degree};
        const auto [lower, upper] = sketch::nurbsDomain(view);
        for (const double fraction :
             {0.0, 0.125, 0.25, 0.375, 0.5, 0.625, 0.75, 0.875, 1.0}) {
          const sketch::NurbsPoint evaluated = sketch::evaluateNurbs(
              view, std::lerp(lower, upper, fraction));
          require(curveResidual(*curve, point(evaluated.x, evaluated.y)) <=
                      1.0e-8,
                  "exact NURBS conversion changed the analytic locus");
        }
        if (!periodic) {
          auto sourceStart = sketch::resolvePoint(
              definition, {curveId, sketch::PointKey::Start});
          auto sourceEnd = sketch::resolvePoint(
              definition, {curveId, sketch::PointKey::End});
          auto convertedStart = sketch::resolvePoint(
              converted->target, {curveId, sketch::PointKey::Start});
          auto convertedEnd = sketch::resolvePoint(
              converted->target, {curveId, sketch::PointKey::End});
          require(sourceStart && sourceEnd && convertedStart && convertedEnd &&
                      near(*convertedStart, sourceStart->x.si(),
                           sourceStart->y.si()) &&
                      near(*convertedEnd, sourceEnd->x.si(),
                           sourceEnd->y.si()),
                  "exact NURBS conversion changed endpoint ancestry");
        }
      });
}

void verifyGeneratedOpenTrim(const testkit::PropertyProfile &profile) {
  testkit::checkProperty(
      "open Trim preserves transformed curve ancestry", profile,
      [](testkit::Random &random, std::uint64_t index) {
        const double centerX = random.between(-1'000.0, 1'000.0);
        const double centerY = random.between(-1'000.0, 1'000.0);
        const double rotation =
            random.between(-std::numbers::pi, std::numbers::pi);
        const double major = random.between(0.01, 100.0);
        const double minor = major * random.between(0.3, 0.9);
        const bool reversed = (index & 1U) != 0U;
        const SketchEntityId target =
            id<SketchEntityId>(40'000U + index * 5U);
        const SketchEntityId firstBoundary =
            id<SketchEntityId>(40'001U + index * 5U);
        const SketchEntityId secondBoundary =
            id<SketchEntityId>(40'002U + index * 5U);
        const SketchEntityId split =
            id<SketchEntityId>(40'003U + index * 5U);
        const SketchObjectId object =
            id<SketchObjectId>(40'004U + index * 5U);

        double firstParameter = -0.45;
        double secondParameter = 0.55;
        double selectedParameter = 0.0;
        sketch::SketchObjectKind objectKind = sketch::SketchObjectKind::Arc;
        std::optional<sketch::Entity> curve;
        const auto pointAndTangent =
            [&](double native) -> std::pair<sketch::Point2,
                                            std::pair<double, double>> {
          switch (index % 5U) {
          case 0U:
            return {transformedPoint(centerX, centerY, 0.0,
                                     major * std::cos(native),
                                     major * std::sin(native)),
                    transformedVector(0.0, -major * std::sin(native),
                                      major * std::cos(native))};
          case 1U:
            return {transformedPoint(centerX, centerY, rotation,
                                     major * std::cos(native),
                                     minor * std::sin(native)),
                    transformedVector(rotation, -major * std::sin(native),
                                      minor * std::cos(native))};
          case 2U:
            return {transformedPoint(centerX, centerY, rotation,
                                     major * std::cosh(native),
                                     minor * std::sinh(native)),
                    transformedVector(rotation, major * std::sinh(native),
                                      minor * std::cosh(native))};
          case 3U:
            return {transformedPoint(centerX, centerY, rotation,
                                     native * native / (4.0 * major), native),
                    transformedVector(rotation, native / (2.0 * major),
                                      1.0)};
          default:
            return {transformedPoint(centerX, centerY, rotation,
                                     std::lerp(-major, major, native), 0.0),
                    transformedVector(rotation, 1.0, 0.0)};
          }
        };

        switch (index % 5U) {
        case 0U:
          curve = sketch::ArcEntity{
              target, point(centerX, centerY), length(major),
              angle(reversed ? 1.3 : -1.2), angle(reversed ? -1.2 : 1.3)};
          break;
        case 1U:
          objectKind = sketch::SketchObjectKind::EllipticalArc;
          curve = sketch::EllipticalArcEntity{
              target, point(centerX, centerY), length(major), length(minor),
              angle(rotation), angle(reversed ? 1.3 : -1.2),
              angle(reversed ? -1.2 : 1.3)};
          break;
        case 2U:
          objectKind = sketch::SketchObjectKind::HyperbolicArc;
          curve = sketch::HyperbolicArcEntity{
              target, point(centerX, centerY), length(major), length(minor),
              angle(rotation), scalar(reversed ? 1.3 : -1.2),
              scalar(reversed ? -1.2 : 1.3)};
          break;
        case 3U:
          objectKind = sketch::SketchObjectKind::ParabolicArc;
          firstParameter *= major;
          secondParameter *= major;
          selectedParameter = 0.0;
          curve = sketch::ParabolicArcEntity{
              target, point(centerX, centerY), length(major), angle(rotation),
              length(reversed ? 1.3 * major : -1.2 * major),
              length(reversed ? -1.2 * major : 1.3 * major)};
          break;
        default: {
          objectKind = sketch::SketchObjectKind::BSpline;
          firstParameter = 0.35;
          secondParameter = 0.65;
          selectedParameter = 0.5;
          const sketch::Point2 start =
              transformedPoint(centerX, centerY, rotation, -major, 0.0);
          const sketch::Point2 end =
              transformedPoint(centerX, centerY, rotation, major, 0.0);
          curve = sketch::BSplineEntity{
              target,
              {start, end},
              {scalar(0.0), scalar(0.0), scalar(1.0), scalar(1.0)},
              {scalar(1.0), scalar(1.0)},
              1U};
          break;
        }
        }

        const auto first = pointAndTangent(firstParameter);
        const auto second = pointAndTangent(secondParameter);
        const auto selected = pointAndTangent(selectedParameter).first;
        const auto crossing = [&](SketchEntityId idValue, const auto &sample) {
          const double tangentLength =
              std::hypot(sample.second.first, sample.second.second);
          const double normalX = -sample.second.second / tangentLength;
          const double normalY = sample.second.first / tangentLength;
          const double span = std::min(major, minor) * 0.02;
          return sketch::LineEntity{
              idValue,
              point(sample.first.x.si() - normalX * span,
                    sample.first.y.si() - normalY * span),
              point(sample.first.x.si() + normalX * span,
                    sample.first.y.si() + normalY * span)};
        };
        sketch::Definition definition{
            digest(40'000U + index),
            {{object, "Curve 1", objectKind, {{"curve", target}}}},
            {*curve, crossing(firstBoundary, first),
             crossing(secondBoundary, second)},
            {}};
        auto trimmed = adapters::trimCurve(
            definition, {{target, selected},
                         trimIdentities(split, 80'000U + index * 2U),
                         sketch::ExternalConstraintPolicy::Refuse});
        require(trimmed && trimmed->target.objects.size() == 1U &&
                    trimmed->target.objects.front().kind ==
                        sketch::SketchObjectKind::CurveGroup &&
                    trimmed->target.objects.front().members.size() == 2U,
                "generated Trim lost a retained curve segment");
        auto firstEnd = sketch::resolvePoint(
            trimmed->target, {target, sketch::PointKey::End});
        auto secondStart = sketch::resolvePoint(
            trimmed->target, {split, sketch::PointKey::Start});
        const sketch::Point2 expectedFirst =
            reversed && index % 5U != 4U ? second.first : first.first;
        const sketch::Point2 expectedSecond =
            reversed && index % 5U != 4U ? first.first : second.first;
        require(firstEnd && secondStart &&
                    near(*firstEnd, expectedFirst.x.si(), expectedFirst.y.si(),
                         1.0e-7) &&
                    near(*secondStart, expectedSecond.x.si(),
                         expectedSecond.y.si(), 1.0e-7),
                "generated Trim changed retained endpoint ancestry");
      });
}

void verifyRefusals() {
  const sketch::Entity first = sketch::LineEntity{
      id<SketchEntityId>(10'000), point(0.0, 0.0), point(2.0, 0.0)};
  const sketch::Entity overlap = sketch::LineEntity{
      id<SketchEntityId>(10'001), point(1.0, 0.0), point(3.0, 0.0)};
  auto intersections = adapters::intersectCurves(first, overlap);
  require(intersections && intersections->overlapping,
          "overlapping curves were not reported explicitly");
  require(!adapters::intersectCurves(first, first) &&
              !adapters::intersectCurves(first, overlap, -1.0) &&
              !adapters::projectToCurve(first, point(0.0, 0.0), 0.0),
          "invalid curve operation input was accepted");

  const sketch::Entity degenerate = sketch::LineEntity{
      id<SketchEntityId>(10'002), point(1.0, 1.0), point(1.0, 1.0)};
  const sketch::Entity malformed = sketch::BSplineEntity{
      id<SketchEntityId>(10'003), {}, {}, {}, 3U};
  const auto invalidProjection =
      adapters::projectToCurve(degenerate, point(0.0, 0.0));
  const auto invalidIntersection = adapters::intersectCurves(first, malformed);
  require(!invalidProjection &&
              invalidProjection.error().code ==
                  "sketch.entity.degenerate-line" &&
              !invalidIntersection &&
              invalidIntersection.error().code ==
                  "sketch.entity.invalid-bspline-degree",
          "invalid domain geometry reached the OpenCASCADE boundary");
}

} // namespace

int main() {
  try {
    verifyProjectionCoverage();
    verifyIntersectionCoverage();
    verifyNegativeSweepCoverage();
    verifyGeneratedLineIntersections(kearne::testkit::propertyProfile());
    verifyGeneratedConicOperations(kearne::testkit::propertyProfile());
    verifyTrimLifecycle();
    verifyClosedCurveTrim();
    verifyPeriodicBSplineTrim();
    verifySplitLifecycle();
    verifyClosedCurveSplit();
    verifyGeneratedOpenSplit(kearne::testkit::propertyProfile());
    verifyJoinLifecycle();
    verifyMixedCurveJoins();
    verifyGeneratedLineJoins(kearne::testkit::propertyProfile());
    verifyGeneratedNurbsConversions(kearne::testkit::propertyProfile());
    verifyGeneratedOpenTrim(kearne::testkit::propertyProfile());
    verifyRefusals();
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
  }
}
