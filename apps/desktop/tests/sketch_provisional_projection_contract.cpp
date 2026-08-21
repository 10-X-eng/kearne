#include "display_units.hpp"
#include "sketch_gesture_preview.hpp"
#include "sketch_provisional_projection.hpp"
#include "sketch_scene_fixture.hpp"
#include "sketch_tool_fixture.hpp"
#include "sketch_tool_gesture.hpp"

#include <kearne/testkit/property.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <numbers>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace kearne;
using namespace kearne::render;
using namespace kearne::ui;
using namespace kearne::ui::test;

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

SketchPrimitiveProjection generatedPrimitive(testkit::Random &random,
                                             std::uint64_t index) {
  SketchPrimitiveProjection result;
  result.id = QStringLiteral("draft.%1").arg(index);
  result.kind = static_cast<ui::SketchPrimitiveKind>(index % 8U);
  result.construction = index % 3U == 0U;
  result.draft = true;
  const PlanePoint first{random.between(-100.0, 100.0),
                         random.between(-100.0, 100.0)};
  switch (result.kind) {
  case ui::SketchPrimitiveKind::Point:
    result.points = {first};
    break;
  case ui::SketchPrimitiveKind::Line:
    result.points = {
        first,
        {first.xMetres + random.between(0.001, 10.0),
         first.yMetres + random.between(0.001, 10.0)},
    };
    break;
  case ui::SketchPrimitiveKind::Circle:
    result.points = {first};
    result.radiusMetres = random.between(0.001, 10.0);
    break;
  case ui::SketchPrimitiveKind::Arc:
    result.points = {first};
    result.radiusMetres = random.between(0.001, 10.0);
    result.startAngleRadians = random.between(-3.0, 3.0);
    result.sweepAngleRadians = random.between(0.001, 6.0);
    break;
  case ui::SketchPrimitiveKind::Ellipse:
  case ui::SketchPrimitiveKind::EllipticalArc:
    result.points = {first};
    result.radiusMetres = random.between(0.002, 10.0);
    result.secondaryRadiusMetres = random.between(0.001, result.radiusMetres);
    result.rotationAngleRadians = random.between(-3.0, 3.0);
    if (result.kind == ui::SketchPrimitiveKind::EllipticalArc) {
      result.startAngleRadians = random.between(-3.0, 3.0);
      result.sweepAngleRadians = random.between(0.001, 6.0);
    }
    break;
  case ui::SketchPrimitiveKind::HyperbolicArc:
    result.points = {first};
    result.radiusMetres = random.between(0.002, 2.0);
    result.secondaryRadiusMetres = random.between(0.002, 2.0);
    result.rotationAngleRadians = random.between(-3.0, 3.0);
    result.startAngleRadians = random.between(-1.5, -0.1);
    result.sweepAngleRadians = random.between(0.2, 3.0);
    break;
  case ui::SketchPrimitiveKind::ParabolicArc:
    result.points = {first};
    result.radiusMetres = random.between(0.002, 2.0);
    result.rotationAngleRadians = random.between(-3.0, 3.0);
    result.startAngleRadians = random.between(-2.0, -0.1);
    result.sweepAngleRadians = random.between(0.2, 4.0);
    break;
  case ui::SketchPrimitiveKind::BSpline:
    throw std::runtime_error("packed provisional generator selected B-spline");
  }
  return result;
}

SketchProvisionalProjectionIdentity identity(const SceneStamp &base,
                                             std::uint64_t generation) {
  auto edit = SketchEditSessionHandle::create(1U);
  auto tool = SketchToolInstanceHandle::create(1U);
  auto version = SketchProvisionalGeneration::create(generation);
  require(edit && tool && version, "provisional identity was invalid");
  return {base, *edit, *tool, *version};
}

void verifyGeneratedProjection(const testkit::PropertyProfile &profile) {
  testkit::checkProperty(
      "frontend draft projects to native provisional geometry", profile,
      [](testkit::Random &random, std::uint64_t index) {
        const std::size_t count =
            static_cast<std::size_t>(random.next() % 24U + 1U);
        std::vector<SketchPrimitiveProjection> input;
        input.reserve(count + 1U);
        SketchPrimitiveProjection ignored;
        ignored.kind = ui::SketchPrimitiveKind::Line;
        ignored.points = {{0.0, 0.0}};
        input.push_back(ignored);
        for (std::size_t primitive = 0U; primitive < count; ++primitive)
          input.push_back(generatedPrimitive(random, index + primitive));
        const SceneStamp base = stamp(90, index + 1U, 90, 90, 90, index + 1U);
        auto first = projectSketchProvisional(identity(base, 1U), input);
        auto repeated = projectSketchProvisional(identity(base, 2U), input);
        if (!first)
          throw std::runtime_error(first.error().code + ": " +
                                   first.error().summary);
        if (!repeated)
          throw std::runtime_error(repeated.error().code + ": " +
                                   repeated.error().summary);
        require(first && repeated && *first && *repeated,
                "valid frontend draft projection failed");
        require((*first)->primitives().size() == count &&
                    (*first)->stamp().payload == (*repeated)->stamp().payload &&
                    std::ranges::equal((*first)->primitives(),
                                       (*repeated)->primitives()),
                "provisional projection lost count, payload, or geometry");
        for (std::size_t primitive = 0U; primitive < count; ++primitive) {
          const auto &source = input[primitive + 1U];
          const auto &projected = (*first)->primitives()[primitive];
          require(projected.handle.value() == primitive + 1U &&
                      projected.classification ==
                          (source.construction
                               ? SketchProvisionalClassification::Construction
                               : SketchProvisionalClassification::Regular),
                  "provisional identity or classification changed");
          require(
              projected.points[0] == Point2d{source.points[0].xMetres,
                                             source.points[0].yMetres} &&
                  projected.radius == source.radiusMetres &&
                  projected.startAngleRadians == source.startAngleRadians &&
                  projected.sweepAngleRadians == source.sweepAngleRadians &&
                  projected.secondaryRadius == source.secondaryRadiusMetres &&
                  projected.rotationAngleRadians == source.rotationAngleRadians,
              "provisional canonical geometry changed");
        }
      });
}

void verifyRefusals() {
  const SceneStamp base = stamp(91, 1, 91, 91, 91, 1);
  SketchPrimitiveProjection ignored;
  ignored.kind = ui::SketchPrimitiveKind::Line;
  ignored.points = {{0.0, 0.0}};
  auto empty = projectSketchProvisional(identity(base, 1U), {&ignored, 1U});
  require(empty && !*empty, "non-draft geometry produced a preview packet");

  ignored.draft = true;
  auto invalid = projectSketchProvisional(identity(base, 2U), {&ignored, 1U});
  require(!invalid &&
              invalid.error().code == "desktop.sketch.provisional-line-shape",
          "malformed line preview was not refused at the adapter boundary");
}

std::size_t expectedPrimitiveCount(LocalSketchToolKind kind, bool closed,
                                   std::size_t sideCount) {
  switch (kind) {
  case LocalSketchToolKind::Rectangle:
  case LocalSketchToolKind::CenterRectangle:
  case LocalSketchToolKind::Slot:
  case LocalSketchToolKind::Oblong:
  case LocalSketchToolKind::ArcSlot:
    return 4U;
  case LocalSketchToolKind::Polyline:
    return closed ? 3U : 2U;
  case LocalSketchToolKind::Point:
  case LocalSketchToolKind::Line:
  case LocalSketchToolKind::Circle:
  case LocalSketchToolKind::Arc:
  case LocalSketchToolKind::ThreePointArc:
  case LocalSketchToolKind::ThreePointCircle:
  case LocalSketchToolKind::Ellipse:
  case LocalSketchToolKind::ThreePointEllipse:
  case LocalSketchToolKind::EllipticalArc:
  case LocalSketchToolKind::HyperbolicArc:
  case LocalSketchToolKind::ParabolicArc:
    return 1U;
  case LocalSketchToolKind::Triangle:
  case LocalSketchToolKind::Square:
  case LocalSketchToolKind::Pentagon:
  case LocalSketchToolKind::Hexagon:
  case LocalSketchToolKind::Heptagon:
  case LocalSketchToolKind::Octagon:
  case LocalSketchToolKind::RegularPolygon:
    return localSketchPolygonSideCount(kind, sideCount);
  case LocalSketchToolKind::BSpline:
  case LocalSketchToolKind::PeriodicBSpline:
  case LocalSketchToolKind::InterpolatedBSpline:
  case LocalSketchToolKind::PeriodicInterpolatedBSpline:
    return 0U;
  }
  return 0U;
}

std::size_t expectedMeasurementCount(LocalSketchToolKind kind) {
  if (kind == LocalSketchToolKind::Rectangle ||
      kind == LocalSketchToolKind::CenterRectangle ||
      isLocalSketchPolygon(kind) || kind == LocalSketchToolKind::Arc ||
      kind == LocalSketchToolKind::ThreePointArc ||
      kind == LocalSketchToolKind::Ellipse ||
      kind == LocalSketchToolKind::ThreePointEllipse ||
      kind == LocalSketchToolKind::HyperbolicArc ||
      kind == LocalSketchToolKind::Slot || kind == LocalSketchToolKind::Oblong)
    return 2U;
  if (kind == LocalSketchToolKind::EllipticalArc)
    return 4U;
  if (kind == LocalSketchToolKind::ArcSlot)
    return 3U;
  return kind == LocalSketchToolKind::Point ? 0U : 1U;
}

void verifyToolCatalogAndGeneratedGestures(
    const testkit::PropertyProfile &profile) {
  const auto definitions = localSketchToolDefinitions();
  require(!definitions.empty(), "local Sketch tool catalog is empty");
  std::set<std::string> methods;
  for (const auto &definition : definitions) {
    const std::string methodKey = std::string{definition.commandId} + "\n" +
                                  std::string{definition.methodId};
    require(!definition.commandId.empty() && !definition.label.empty() &&
                !definition.icon.empty() &&
                definition.minimumInputPointCount > 0U &&
                (definition.maximumInputPointCount == 0U ||
                 definition.maximumInputPointCount >=
                     definition.minimumInputPointCount) &&
                methods.insert(methodKey).second &&
                localSketchToolDefinition(definition.kind) == &definition &&
                localSketchToolDefinition(
                    QString::fromLatin1(
                        definition.commandId.data(),
                        static_cast<qsizetype>(definition.commandId.size())),
                    QString::fromLatin1(
                        definition.methodId.data(),
                        static_cast<qsizetype>(definition.methodId.size()))) ==
                    &definition,
            "Sketch tool definition is incomplete, duplicated, or unfindable");
  }

  testkit::checkProperty(
      "catalog gestures share preview and native projection", profile,
      [definitions](testkit::Random &random, std::uint64_t index) {
        const auto &definition = definitions[index % definitions.size()];
        auto points = definingSketchToolPoints(definition.kind);
        require(points.size() >= definition.minimumInputPointCount &&
                    (definition.maximumInputPointCount == 0U ||
                     points.size() == definition.maximumInputPointCount),
                "Sketch tool point contract drifted from its catalog");
        const double scale = random.between(0.001, 100.0);
        const double angle =
            random.between(-std::numbers::pi, std::numbers::pi);
        const double cosine = std::cos(angle);
        const double sine = std::sin(angle);
        const double translateX = random.between(-1'000.0, 1'000.0);
        const double translateY = random.between(-1'000.0, 1'000.0);
        for (auto &point : points) {
          const double x = point.xMetres * scale;
          const double y = point.yMetres * scale;
          point = {translateX + x * cosine - y * sine,
                   translateY + x * sine + y * cosine};
        }
        LocalSketchToolGesture gesture{
            definition.kind, points, index % 2U == 0U,
            definition.kind == LocalSketchToolKind::Polyline &&
                index % 3U == 0U};
        gesture.sideCount =
            definition.kind == LocalSketchToolKind::RegularPolygon
                ? static_cast<std::size_t>(index % 126U + 3U)
                : 0U;
        for (std::size_t count = 1U; count <= points.size(); ++count) {
          LocalSketchToolGesture partial = gesture;
          partial.points.resize(count);
          auto preview = projectLocalSketchToolGesture(partial);
          if (!preview)
            throw std::runtime_error(preview.error().code + ": " +
                                     preview.error().summary);
          require(!preview->empty(),
                  "valid partial Sketch gesture produced no preview");
          std::vector<QPointF> pointerPoints;
          pointerPoints.reserve(partial.points.size());
          for (const LocalSketchToolPoint point : partial.points)
            pointerPoints.push_back({millimetersFromMetres(point.xMetres),
                                     millimetersFromMetres(point.yMetres)});
          LocalSketchToolGesture pointerGesture = partial;
          for (std::size_t point = 0U; point < pointerPoints.size(); ++point) {
            pointerGesture.points[point] = {
                metresFromMillimeters(pointerPoints[point].x()),
                metresFromMillimeters(pointerPoints[point].y())};
          }
          auto pointerProjection =
              projectLocalSketchToolGesture(pointerGesture, false);
          SketchGesturePreview interaction;
          require(
              interaction.updateGesture(
                  QString::fromLatin1(
                      definition.commandId.data(),
                      static_cast<qsizetype>(definition.commandId.size())),
                  pointerPoints, partial.construction,
                  QString::fromLatin1(
                      definition.methodId.data(),
                      static_cast<qsizetype>(definition.methodId.size())),
                  partial.closed, partial.sideCount, partial.degree) &&
                  pointerProjection &&
                  std::ranges::equal(interaction.primitives(),
                                     *pointerProjection),
              "pointer preview diverged from the shared gesture projection");
          if (count >= 2U)
            require(!interaction.measurements().empty(),
                    "multi-point Sketch stage has no live measurement");
          if (count == points.size())
            require(interaction.measurements().size() ==
                        expectedMeasurementCount(definition.kind),
                    "complete Sketch gesture has the wrong live measurements");
        }
        auto complete = projectLocalSketchToolGesture(gesture, true);
        if (!complete)
          throw std::runtime_error(complete.error().code + ": " +
                                   complete.error().summary);
        const std::size_t expected = expectedPrimitiveCount(
            definition.kind, gesture.closed, gesture.sideCount);
        require(isLocalSketchBSpline(definition.kind)
                    ? complete->size() > gesture.points.size()
                    : complete->size() == expected,
                "complete Sketch gesture produced the wrong primitive count");
        const SceneStamp base = stamp(92, index + 1U, 92, 92, 92, index + 1U);
        auto native = projectSketchProvisional(identity(base, 1U), *complete);
        require(native && *native &&
                    (*native)->primitives().size() == complete->size(),
                "catalog gesture did not reach native provisional geometry");
      });
}

void verifyConstraintCatalog() {
  const auto definitions = localSketchConstraintDefinitions();
  require(!definitions.empty(), "local Sketch constraint catalog is empty");
  std::set<std::string> commands;
  std::set<LocalSketchConstraintKind> kinds;
  for (const auto &definition : definitions) {
    require(!definition.commandId.empty() && !definition.label.empty() &&
                definition.minimumSelectionCount >= 1U &&
                definition.minimumSelectionCount <= 16U &&
                (definition.maximumSelectionCount == 0U ||
                 (definition.maximumSelectionCount >=
                      definition.minimumSelectionCount &&
                  definition.maximumSelectionCount <= 16U)) &&
                commands.insert(std::string{definition.commandId}).second &&
                kinds.insert(definition.kind).second &&
                localSketchConstraintDefinition(definition.kind) ==
                    &definition &&
                localSketchConstraintDefinition(QString::fromLatin1(
                    definition.commandId.data(),
                    static_cast<qsizetype>(definition.commandId.size()))) ==
                    &definition,
            "Sketch constraint definition is incomplete, duplicated, or "
            "unfindable");
  }
}

void verifyDimensionValueParsing(const testkit::PropertyProfile &profile) {
  testkit::checkProperty(
      "displayed dimension values preserve SI meaning", profile,
      [](testkit::Random &random, std::uint64_t) {
        const double magnitude = random.between(-1.0e5, 1.0e5);
        const std::array units{
            std::pair{QStringLiteral("mm"), 0.001},
            std::pair{QStringLiteral("cm"), 0.01},
            std::pair{QStringLiteral("m"), 1.0},
            std::pair{QStringLiteral("in"), 0.0254},
        };
        for (const auto &[unit, scale] : units) {
          const QString text =
              QString::number(magnitude, 'g', 17) + QLatin1Char(' ') + unit;
          const auto parsed = parseDisplayedLengthMetres(text, unit);
          require(parsed && std::abs(*parsed - magnitude * scale) <=
                                std::max(1.0e-12,
                                         std::abs(magnitude * scale) * 1.0e-14),
                  "length dimension parser changed SI meaning");
        }
        const auto degrees = parseDisplayedAngleRadians(
            QString::number(magnitude, 'g', 17) + QStringLiteral(" deg"));
        const auto radians = parseDisplayedAngleRadians(
            QString::number(magnitude, 'g', 17) + QStringLiteral(" rad"));
        require(degrees && radians &&
                    std::abs(*degrees - magnitude * std::numbers::pi / 180.0) <=
                        std::max(1.0e-12, std::abs(*degrees) * 1.0e-14) &&
                    std::abs(*radians - magnitude) <=
                        std::max(1.0e-12, std::abs(magnitude) * 1.0e-14),
                "angle dimension parser changed SI meaning");
      });
}

} // namespace

int main() {
  try {
    const auto profile = kearne::testkit::propertyProfile();
    verifyGeneratedProjection(profile);
    verifyToolCatalogAndGeneratedGestures(profile);
    verifyConstraintCatalog();
    verifyDimensionValueParsing(profile);
    verifyRefusals();
    std::cout << "verified " << profile.iterations
              << " frontend-to-native provisional projections\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
