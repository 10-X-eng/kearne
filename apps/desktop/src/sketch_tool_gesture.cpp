#include "sketch_tool_gesture.hpp"

#include <kearne/adapters/occ_bspline.hpp>
#include <kearne/sketch/nurbs.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>

namespace kearne::ui {
namespace {

constexpr std::array toolDefinitions{
    LocalSketchToolDefinition{LocalSketchToolKind::Point, "sketch.point", "",
                              "Point", "point", 1U, 1U},
    LocalSketchToolDefinition{LocalSketchToolKind::Line, "sketch.line", "",
                              "Line", "line", 2U, 2U},
    LocalSketchToolDefinition{LocalSketchToolKind::Polyline, "sketch.polyline",
                              "", "Polyline", "polyline", 2U, 0U},
    LocalSketchToolDefinition{LocalSketchToolKind::Circle, "sketch.circle",
                              "center-radius", "Circle", "circle", 2U, 2U},
    LocalSketchToolDefinition{LocalSketchToolKind::ThreePointCircle,
                              "sketch.circle", "three-point",
                              "Three-point Circle", "circle", 3U, 3U},
    LocalSketchToolDefinition{LocalSketchToolKind::Arc, "sketch.arc", "center",
                              "Arc", "arc", 3U, 3U},
    LocalSketchToolDefinition{LocalSketchToolKind::ThreePointArc, "sketch.arc",
                              "three-point", "Three-point Arc", "arc", 3U, 3U},
    LocalSketchToolDefinition{LocalSketchToolKind::Ellipse, "sketch.ellipse",
                              "center", "Ellipse", "ellipse", 3U, 3U},
    LocalSketchToolDefinition{LocalSketchToolKind::ThreePointEllipse,
                              "sketch.ellipse", "three-point",
                              "Three-point Ellipse", "ellipse", 3U, 3U},
    LocalSketchToolDefinition{LocalSketchToolKind::EllipticalArc,
                              "sketch.elliptical-arc", "", "Elliptical Arc",
                              "elliptical-arc", 5U, 5U},
    LocalSketchToolDefinition{LocalSketchToolKind::HyperbolicArc,
                              "sketch.hyperbolic-arc", "", "Hyperbolic Arc",
                              "hyperbolic-arc", 4U, 4U},
    LocalSketchToolDefinition{LocalSketchToolKind::ParabolicArc,
                              "sketch.parabolic-arc", "", "Parabolic Arc",
                              "parabolic-arc", 4U, 4U},
    LocalSketchToolDefinition{LocalSketchToolKind::Rectangle,
                              "sketch.rectangle", "corner", "Rectangle",
                              "rectangle", 2U, 2U},
    LocalSketchToolDefinition{LocalSketchToolKind::CenterRectangle,
                              "sketch.rectangle", "center", "Center Rectangle",
                              "rectangle", 2U, 2U},
    LocalSketchToolDefinition{LocalSketchToolKind::Slot, "sketch.slot", "",
                              "Slot", "slot", 3U, 3U},
    LocalSketchToolDefinition{LocalSketchToolKind::ArcSlot, "sketch.arc-slot",
                              "", "Arc Slot", "arc-slot", 4U, 4U},
    LocalSketchToolDefinition{LocalSketchToolKind::Oblong, "sketch.oblong", "",
                              "Oblong", "oblong", 3U, 3U},
    LocalSketchToolDefinition{LocalSketchToolKind::Triangle, "sketch.polygon",
                              "triangle", "Triangle", "triangle", 2U, 2U},
    LocalSketchToolDefinition{LocalSketchToolKind::Square, "sketch.polygon",
                              "square", "Square", "square", 2U, 2U},
    LocalSketchToolDefinition{LocalSketchToolKind::Pentagon, "sketch.polygon",
                              "pentagon", "Pentagon", "pentagon", 2U, 2U},
    LocalSketchToolDefinition{LocalSketchToolKind::Hexagon, "sketch.polygon",
                              "hexagon", "Hexagon", "hexagon", 2U, 2U},
    LocalSketchToolDefinition{LocalSketchToolKind::Heptagon, "sketch.polygon",
                              "heptagon", "Heptagon", "polygon", 2U, 2U},
    LocalSketchToolDefinition{LocalSketchToolKind::Octagon, "sketch.polygon",
                              "octagon", "Octagon", "octagon", 2U, 2U},
    LocalSketchToolDefinition{LocalSketchToolKind::RegularPolygon,
                              "sketch.polygon", "regular", "Polygon", "polygon",
                              2U, 2U},
    LocalSketchToolDefinition{LocalSketchToolKind::BSpline,
                              "sketch.bspline.control-points", "", "B-spline",
                              "bspline-control", 2U, 0U},
    LocalSketchToolDefinition{LocalSketchToolKind::PeriodicBSpline,
                              "sketch.bspline.periodic-control-points", "",
                              "Periodic B-spline", "bspline-periodic", 2U, 0U},
    LocalSketchToolDefinition{LocalSketchToolKind::InterpolatedBSpline,
                              "sketch.bspline.interpolation", "",
                              "Interpolated B-spline", "bspline-interpolate",
                              2U, 0U},
    LocalSketchToolDefinition{LocalSketchToolKind::PeriodicInterpolatedBSpline,
                              "sketch.bspline.periodic-interpolation", "",
                              "Periodic interpolated B-spline",
                              "bspline-periodic-interpolate", 2U, 0U},
};

constexpr std::array constraintDefinitions{
    LocalSketchConstraintDefinition{LocalSketchConstraintKind::Coincident,
                                    "sketch.coincident", "Coincident", 2U},
    LocalSketchConstraintDefinition{LocalSketchConstraintKind::Horizontal,
                                    "sketch.horizontal", "Horizontal", 1U},
    LocalSketchConstraintDefinition{LocalSketchConstraintKind::Vertical,
                                    "sketch.vertical", "Vertical", 1U},
    LocalSketchConstraintDefinition{
        LocalSketchConstraintKind::HorizontalVertical,
        "sketch.horizontal-vertical", "Horizontal / Vertical", 1U},
    LocalSketchConstraintDefinition{LocalSketchConstraintKind::Parallel,
                                    "sketch.parallel", "Parallel", 2U},
    LocalSketchConstraintDefinition{LocalSketchConstraintKind::Perpendicular,
                                    "sketch.perpendicular", "Perpendicular",
                                    2U},
    LocalSketchConstraintDefinition{LocalSketchConstraintKind::Tangent,
                                    "sketch.tangent", "Tangent", 2U},
    LocalSketchConstraintDefinition{LocalSketchConstraintKind::Equal,
                                    "sketch.equal", "Equal", 2U},
    LocalSketchConstraintDefinition{LocalSketchConstraintKind::Concentric,
                                    "sketch.concentric", "Concentric", 2U},
    LocalSketchConstraintDefinition{LocalSketchConstraintKind::Midpoint,
                                    "sketch.midpoint", "Midpoint", 2U},
    LocalSketchConstraintDefinition{LocalSketchConstraintKind::Block,
                                    "sketch.block", "Block", 1U},
    LocalSketchConstraintDefinition{LocalSketchConstraintKind::Group,
                                    "sketch.group", "Group", 2U, 16U},
    LocalSketchConstraintDefinition{
        LocalSketchConstraintKind::RemoveAxisAlignment,
        "sketch.remove-axis-alignment", "Remove axis alignment", 1U, 16U},
    LocalSketchConstraintDefinition{LocalSketchConstraintKind::Collinear,
                                    "sketch.collinear", "Collinear", 2U},
    LocalSketchConstraintDefinition{LocalSketchConstraintKind::PointOnObject,
                                    "sketch.point-on-object", "Point on object",
                                    2U},
    LocalSketchConstraintDefinition{LocalSketchConstraintKind::Symmetric,
                                    "sketch.symmetric", "Symmetric", 3U},
    LocalSketchConstraintDefinition{LocalSketchConstraintKind::Lock,
                                    "sketch.lock", "Lock", 1U},
    LocalSketchConstraintDefinition{LocalSketchConstraintKind::Distance,
                                    "sketch.distance", "Distance", 1U, 2U},
    LocalSketchConstraintDefinition{
        LocalSketchConstraintKind::HorizontalDistance,
        "sketch.horizontal-distance", "Horizontal distance", 2U},
    LocalSketchConstraintDefinition{LocalSketchConstraintKind::VerticalDistance,
                                    "sketch.vertical-distance",
                                    "Vertical distance", 2U},
    LocalSketchConstraintDefinition{LocalSketchConstraintKind::Radius,
                                    "sketch.radius", "Radius", 1U},
    LocalSketchConstraintDefinition{LocalSketchConstraintKind::Diameter,
                                    "sketch.diameter", "Diameter", 1U},
    LocalSketchConstraintDefinition{LocalSketchConstraintKind::Angle,
                                    "sketch.angle", "Angle", 2U},
    LocalSketchConstraintDefinition{LocalSketchConstraintKind::Snell,
                                    "sketch.snell", "Refraction", 3U},
};

SketchPrimitiveProjection pointPrimitive(const PlanePoint &point, QString id,
                                         bool construction) {
  return {std::move(id),
          SketchPrimitiveKind::Point,
          {point},
          {QStringLiteral("point")},
          {},
          0.0,
          construction,
          false,
          true};
}

SketchPrimitiveProjection linePrimitive(const PlanePoint &start,
                                        const PlanePoint &end, int index,
                                        bool construction) {
  return {QStringLiteral("draft.%1").arg(index),
          SketchPrimitiveKind::Line,
          {start, end},
          {QStringLiteral("start"), QStringLiteral("end")},
          {},
          0.0,
          construction,
          false,
          true};
}

SketchPrimitiveProjection circlePrimitive(const PlanePoint &center,
                                          double radius, QString id,
                                          bool construction) {
  return {std::move(id),
          SketchPrimitiveKind::Circle,
          {center},
          {QStringLiteral("center")},
          {},
          radius,
          construction,
          false,
          true};
}

SketchPrimitiveProjection arcPrimitive(const PlanePoint &center, double radius,
                                       double startAngle, double sweepAngle,
                                       int index, bool construction) {
  SketchPrimitiveProjection primitive;
  primitive.id = QStringLiteral("draft.%1").arg(index);
  primitive.kind = SketchPrimitiveKind::Arc;
  primitive.points = {center};
  primitive.pointKeys = {QStringLiteral("center")};
  primitive.radiusMetres = radius;
  primitive.construction = construction;
  primitive.draft = true;
  primitive.startAngleRadians = startAngle;
  primitive.sweepAngleRadians = sweepAngle;
  return primitive;
}

SketchPrimitiveProjection ellipsePrimitive(
    const PlanePoint &center, double majorRadius, double minorRadius,
    double rotation, QString id, bool construction,
    std::optional<std::pair<double, double>> parameterRange = std::nullopt) {
  SketchPrimitiveProjection primitive;
  primitive.id = std::move(id);
  primitive.kind = parameterRange ? SketchPrimitiveKind::EllipticalArc
                                  : SketchPrimitiveKind::Ellipse;
  primitive.points = {center};
  primitive.pointKeys = {QStringLiteral("center")};
  primitive.radiusMetres = majorRadius;
  primitive.secondaryRadiusMetres = minorRadius;
  primitive.rotationAngleRadians = rotation;
  primitive.construction = construction;
  primitive.draft = true;
  if (parameterRange) {
    primitive.startAngleRadians = parameterRange->first;
    primitive.sweepAngleRadians = parameterRange->second;
  }
  return primitive;
}

SketchPrimitiveProjection
conicArcPrimitive(SketchPrimitiveKind kind, const PlanePoint &anchor,
                  double radius, double secondaryRadius, double rotation,
                  double startParameter, double sweepParameter,
                  bool construction) {
  SketchPrimitiveProjection primitive;
  primitive.id = QStringLiteral("draft.0");
  primitive.kind = kind;
  primitive.points = {anchor};
  primitive.pointKeys = {QStringLiteral("center")};
  primitive.radiusMetres = radius;
  primitive.secondaryRadiusMetres = secondaryRadius;
  primitive.rotationAngleRadians = rotation;
  primitive.startAngleRadians = startParameter;
  primitive.sweepAngleRadians = sweepParameter;
  primitive.construction = construction;
  primitive.draft = true;
  return primitive;
}

struct HyperbolaGeometry {
  PlanePoint center;
  double majorRadius;
  double minorRadius;
  double rotation;
  double startParameter;
};

std::optional<HyperbolaGeometry>
hyperbolaFromGesture(std::span<const PlanePoint> points) {
  if (points.size() < 3U)
    return std::nullopt;
  const PlanePoint center = points[0];
  const double axisX = points[1].xMetres - center.xMetres;
  const double axisY = points[1].yMetres - center.yMetres;
  const double major = std::hypot(axisX, axisY);
  if (!std::isfinite(major) || major == 0.0)
    return std::nullopt;
  const double rotation = std::atan2(axisY, axisX);
  const double cosine = std::cos(rotation);
  const double sine = std::sin(rotation);
  const double offsetX = points[2].xMetres - center.xMetres;
  const double offsetY = points[2].yMetres - center.yMetres;
  const double localX = cosine * offsetX + sine * offsetY;
  const double localY = -sine * offsetX + cosine * offsetY;
  const double denominator = localX * localX / (major * major) - 1.0;
  if (!std::isfinite(denominator) || denominator <= 0.0 || localX <= 0.0 ||
      localY == 0.0)
    return std::nullopt;
  const double minor = std::abs(localY) / std::sqrt(denominator);
  const double start = std::asinh(localY / minor);
  if (!std::isfinite(minor) || minor == 0.0 || !std::isfinite(start))
    return std::nullopt;
  return HyperbolaGeometry{center, major, minor, rotation, start};
}

double hyperbolaParameter(const HyperbolaGeometry &hyperbola,
                          const PlanePoint &point) {
  const double offsetX = point.xMetres - hyperbola.center.xMetres;
  const double offsetY = point.yMetres - hyperbola.center.yMetres;
  const double localY = -std::sin(hyperbola.rotation) * offsetX +
                        std::cos(hyperbola.rotation) * offsetY;
  return std::asinh(localY / hyperbola.minorRadius);
}

struct ParabolaGeometry {
  PlanePoint vertex;
  double focalLength;
  double rotation;
  double startParameter;
};

std::optional<ParabolaGeometry>
parabolaFromGesture(std::span<const PlanePoint> points) {
  if (points.size() < 3U)
    return std::nullopt;
  const PlanePoint focus = points[0];
  const PlanePoint vertex = points[1];
  const double axisX = focus.xMetres - vertex.xMetres;
  const double axisY = focus.yMetres - vertex.yMetres;
  const double focal = std::hypot(axisX, axisY);
  if (!std::isfinite(focal) || focal == 0.0)
    return std::nullopt;
  const double rotation = std::atan2(axisY, axisX);
  const double offsetX = points[2].xMetres - vertex.xMetres;
  const double offsetY = points[2].yMetres - vertex.yMetres;
  const double start =
      -std::sin(rotation) * offsetX + std::cos(rotation) * offsetY;
  if (!std::isfinite(start) || start == 0.0)
    return std::nullopt;
  return ParabolaGeometry{vertex, focal, rotation, start};
}

double parabolaParameter(const ParabolaGeometry &parabola,
                         const PlanePoint &point) {
  const double offsetX = point.xMetres - parabola.vertex.xMetres;
  const double offsetY = point.yMetres - parabola.vertex.yMetres;
  return -std::sin(parabola.rotation) * offsetX +
         std::cos(parabola.rotation) * offsetY;
}

struct EllipseGeometry {
  PlanePoint center;
  double majorRadius;
  double minorRadius;
  double rotation;
};

std::optional<EllipseGeometry>
ellipseFromGesture(LocalSketchToolKind kind,
                   std::span<const PlanePoint> points) {
  if (points.size() < 3U)
    return std::nullopt;
  const bool threePoint = kind == LocalSketchToolKind::ThreePointEllipse;
  const PlanePoint center =
      threePoint ? PlanePoint{(points[0].xMetres + points[1].xMetres) * 0.5,
                              (points[0].yMetres + points[1].yMetres) * 0.5}
                 : points[0];
  const PlanePoint majorPoint = points[1];
  const double axisX = majorPoint.xMetres - center.xMetres;
  const double axisY = majorPoint.yMetres - center.yMetres;
  double major = std::hypot(axisX, axisY);
  if (major == 0.0)
    return std::nullopt;
  double rotation = std::atan2(axisY, axisX);
  const double rimX = points[2].xMetres - center.xMetres;
  const double rimY = points[2].yMetres - center.yMetres;
  double minor =
      std::abs(-std::sin(rotation) * rimX + std::cos(rotation) * rimY);
  if (minor == 0.0)
    return std::nullopt;
  if (minor > major) {
    std::swap(major, minor);
    rotation += std::numbers::pi / 2.0;
  }
  return EllipseGeometry{center, major, minor, rotation};
}

double ellipseParameter(const EllipseGeometry &ellipse,
                        const PlanePoint &point) {
  const double offsetX = point.xMetres - ellipse.center.xMetres;
  const double offsetY = point.yMetres - ellipse.center.yMetres;
  const double cosine = std::cos(ellipse.rotation);
  const double sine = std::sin(ellipse.rotation);
  return std::atan2((-sine * offsetX + cosine * offsetY) / ellipse.minorRadius,
                    (cosine * offsetX + sine * offsetY) / ellipse.majorRadius);
}

Result<std::vector<SketchPrimitiveProjection>> invalid(std::string code,
                                                       std::string message) {
  return std::unexpected(diagnostic(std::move(code), std::move(message)));
}

struct CircleGeometry {
  PlanePoint center;
  double radius;
};

std::optional<CircleGeometry> circleThrough(const PlanePoint &first,
                                            const PlanePoint &second,
                                            const PlanePoint &third) {
  const double bx = second.xMetres - first.xMetres;
  const double by = second.yMetres - first.yMetres;
  const double cx = third.xMetres - first.xMetres;
  const double cy = third.yMetres - first.yMetres;
  const double cross = bx * cy - by * cx;
  const double scale = std::max(
      {std::hypot(bx, by), std::hypot(cx, cy), std::hypot(cx - bx, cy - by)});
  if (!std::isfinite(cross) || !std::isfinite(scale) || scale == 0.0 ||
      std::abs(cross) <=
          64.0 * std::numeric_limits<double>::epsilon() * scale * scale)
    return std::nullopt;
  const double bSquared = bx * bx + by * by;
  const double cSquared = cx * cx + cy * cy;
  const PlanePoint center{
      first.xMetres + (cy * bSquared - by * cSquared) / (2.0 * cross),
      first.yMetres + (bx * cSquared - cx * bSquared) / (2.0 * cross)};
  const double radius = std::hypot(center.xMetres - first.xMetres,
                                   center.yMetres - first.yMetres);
  if (!std::isfinite(center.xMetres) || !std::isfinite(center.yMetres) ||
      !std::isfinite(radius) || radius == 0.0)
    return std::nullopt;
  return CircleGeometry{center, radius};
}

double positiveSweep(double start, double end) {
  double sweep = std::fmod(end - start, 2.0 * std::numbers::pi);
  if (sweep < 0.0)
    sweep += 2.0 * std::numbers::pi;
  return sweep;
}

} // namespace

bool isLocalSketchPolygon(LocalSketchToolKind kind) {
  return kind >= LocalSketchToolKind::Triangle &&
         kind <= LocalSketchToolKind::RegularPolygon;
}

bool isLocalSketchBSpline(LocalSketchToolKind kind) {
  return kind >= LocalSketchToolKind::BSpline &&
         kind <= LocalSketchToolKind::PeriodicInterpolatedBSpline;
}

bool isLocalSketchPeriodicBSpline(LocalSketchToolKind kind) {
  return kind == LocalSketchToolKind::PeriodicBSpline ||
         kind == LocalSketchToolKind::PeriodicInterpolatedBSpline;
}

bool isLocalSketchInterpolatedBSpline(LocalSketchToolKind kind) {
  return kind == LocalSketchToolKind::InterpolatedBSpline ||
         kind == LocalSketchToolKind::PeriodicInterpolatedBSpline;
}

std::size_t localSketchPolygonSideCount(LocalSketchToolKind kind,
                                        std::size_t requestedSideCount) {
  switch (kind) {
  case LocalSketchToolKind::Triangle:
    return 3U;
  case LocalSketchToolKind::Square:
    return 4U;
  case LocalSketchToolKind::Pentagon:
    return 5U;
  case LocalSketchToolKind::Hexagon:
    return 6U;
  case LocalSketchToolKind::Heptagon:
    return 7U;
  case LocalSketchToolKind::Octagon:
    return 8U;
  case LocalSketchToolKind::RegularPolygon:
    return requestedSideCount;
  default:
    return 0U;
  }
}

std::span<const LocalSketchToolDefinition> localSketchToolDefinitions() {
  return toolDefinitions;
}

const LocalSketchToolDefinition *
localSketchToolDefinition(LocalSketchToolKind kind) {
  const auto definition = std::ranges::find(toolDefinitions, kind,
                                            &LocalSketchToolDefinition::kind);
  return definition == toolDefinitions.end() ? nullptr : &*definition;
}

const LocalSketchToolDefinition *
localSketchToolDefinition(QStringView commandId, QStringView methodId) {
  const auto definition = std::ranges::find_if(
      toolDefinitions, [commandId, methodId](const auto &candidate) {
        const bool commandMatches =
            commandId == QLatin1StringView{candidate.commandId.data(),
                                           static_cast<qsizetype>(
                                               candidate.commandId.size())};
        const bool methodMatches =
            methodId.isEmpty() ||
            methodId == QLatin1StringView{
                            candidate.methodId.data(),
                            static_cast<qsizetype>(candidate.methodId.size())};
        return commandMatches && methodMatches;
      });
  return definition == toolDefinitions.end() ? nullptr : &*definition;
}

std::span<const LocalSketchConstraintDefinition>
localSketchConstraintDefinitions() {
  return constraintDefinitions;
}

const LocalSketchConstraintDefinition *
localSketchConstraintDefinition(LocalSketchConstraintKind kind) {
  const auto definition = std::ranges::find(
      constraintDefinitions, kind, &LocalSketchConstraintDefinition::kind);
  return definition == constraintDefinitions.end() ? nullptr : &*definition;
}

const LocalSketchConstraintDefinition *
localSketchConstraintDefinition(QStringView commandId) {
  const auto definition = std::ranges::find_if(
      constraintDefinitions, [commandId](const auto &candidate) {
        return commandId == QLatin1StringView{candidate.commandId.data(),
                                              static_cast<qsizetype>(
                                                  candidate.commandId.size())};
      });
  return definition == constraintDefinitions.end() ? nullptr : &*definition;
}

Result<std::vector<SketchPrimitiveProjection>>
projectLocalSketchToolGesture(const LocalSketchToolGesture &gesture,
                              bool requireComplete) {
  const LocalSketchToolDefinition *definition =
      localSketchToolDefinition(gesture.kind);
  if (!definition)
    return invalid("desktop.sketch.tool-kind",
                   "Sketch geometry tool kind is unsupported");
  if ((definition->maximumInputPointCount > 0U &&
       gesture.points.size() > definition->maximumInputPointCount) ||
      (requireComplete &&
       (gesture.points.size() < definition->minimumInputPointCount ||
        (definition->maximumInputPointCount > 0U &&
         gesture.points.size() != definition->maximumInputPointCount))))
    return invalid("desktop.sketch.tool-input-count",
                   "Sketch geometry tool received the wrong number of points");
  if (std::ranges::any_of(gesture.points, [](const auto &point) {
        return !std::isfinite(point.xMetres) || !std::isfinite(point.yMetres);
      }))
    return invalid("desktop.sketch.tool-non-finite",
                   "Sketch geometry tool received a non-finite point");
  if (gesture.points.empty())
    return std::vector<SketchPrimitiveProjection>{};

  std::vector<PlanePoint> points;
  points.reserve(gesture.points.size());
  for (const auto &point : gesture.points)
    points.push_back({point.xMetres, point.yMetres});

  if (gesture.kind == LocalSketchToolKind::Point)
    return std::vector{pointPrimitive(points[0], QStringLiteral("draft.0"),
                                      gesture.construction)};

  if (points.size() == 1U)
    return std::vector{pointPrimitive(points[0], QStringLiteral("draft.anchor"),
                                      gesture.construction)};

  if (isLocalSketchBSpline(gesture.kind)) {
    std::vector<sketch::Point2> input;
    input.reserve(points.size());
    for (const PlanePoint point : points) {
      auto x = sketch::LengthValue::fromSi(point.xMetres);
      auto y = sketch::LengthValue::fromSi(point.yMetres);
      if (!x || !y)
        return invalid("desktop.sketch.bspline-point",
                       "B-spline point is outside the supported range");
      input.push_back({*x, *y});
    }
    auto geometry = adapters::createBSplineGeometry(
        input,
        isLocalSketchInterpolatedBSpline(gesture.kind)
            ? adapters::BSplineCreation::Interpolation
            : adapters::BSplineCreation::ControlPoints,
        gesture.degree, isLocalSketchPeriodicBSpline(gesture.kind));
    if (!geometry)
      return std::unexpected(std::move(geometry.error()));
    std::vector<double> knots;
    std::vector<double> weights;
    knots.reserve(geometry->knots.size());
    weights.reserve(geometry->weights.size());
    for (const sketch::DimensionlessValue knot : geometry->knots)
      knots.push_back(knot.si());
    for (const sketch::DimensionlessValue weight : geometry->weights)
      weights.push_back(weight.si());
    std::vector<SketchPrimitiveProjection> result;
    result.reserve(1U + points.size() * 2U);
    SketchPrimitiveProjection curve;
    curve.id = QStringLiteral("draft.curve");
    curve.kind = SketchPrimitiveKind::BSpline;
    curve.points.reserve(geometry->controlPoints.size());
    curve.pointKeys.reserve(geometry->controlPoints.size());
    for (std::size_t index = 0U; index < geometry->controlPoints.size();
         ++index) {
      const sketch::Point2 &control = geometry->controlPoints[index];
      curve.points.push_back({control.x.si(), control.y.si()});
      curve.pointKeys.push_back(QStringLiteral("control.%1").arg(index + 1U));
    }
    curve.splineKnots = std::move(knots);
    curve.splineWeights = std::move(weights);
    curve.splineDegree = geometry->degree;
    curve.splinePeriodic = isLocalSketchPeriodicBSpline(gesture.kind);
    curve.construction = gesture.construction;
    curve.draft = true;
    result.push_back(std::move(curve));

    const bool interpolated = isLocalSketchInterpolatedBSpline(gesture.kind);
    if (!interpolated) {
      for (std::size_t index = 1U; index < points.size(); ++index) {
        auto control = linePrimitive(points[index - 1U], points[index],
                                     static_cast<int>(index - 1U), true);
        control.id = QStringLiteral("draft.control.%1").arg(index - 1U);
        result.push_back(std::move(control));
      }
      if (isLocalSketchPeriodicBSpline(gesture.kind)) {
        auto control =
            linePrimitive(points.back(), points.front(),
                          static_cast<int>(points.size() - 1U), true);
        control.id = QStringLiteral("draft.control.%1").arg(points.size() - 1U);
        result.push_back(std::move(control));
      }
    }
    for (std::size_t index = 0U; index < points.size(); ++index)
      result.push_back(pointPrimitive(
          points[index], QStringLiteral("draft.input.%1").arg(index), true));
    return result;
  }

  if (gesture.kind == LocalSketchToolKind::Line) {
    if (points[0] == points[1])
      return invalid("desktop.sketch.degenerate-line",
                     "Line endpoints must differ");
    return std::vector{
        linePrimitive(points[0], points[1], 0, gesture.construction)};
  }

  if (gesture.kind == LocalSketchToolKind::Polyline) {
    if (requireComplete && gesture.closed && points.size() < 3U)
      return invalid("desktop.sketch.polyline-point-count",
                     "Closed Polyline needs at least three points");
    std::vector<SketchPrimitiveProjection> result;
    const std::size_t segmentCount =
        points.size() - 1U + (gesture.closed && points.size() >= 3U ? 1U : 0U);
    result.reserve(segmentCount);
    for (std::size_t index = 1U; index < points.size(); ++index) {
      if (points[index - 1U] == points[index])
        return invalid("desktop.sketch.degenerate-polyline-segment",
                       "Polyline consecutive points must differ");
      result.push_back(linePrimitive(points[index - 1U], points[index],
                                     static_cast<int>(index - 1U),
                                     gesture.construction));
    }
    if (gesture.closed && points.size() >= 3U) {
      if (points.back() == points.front())
        return invalid(
            "desktop.sketch.duplicate-polyline-closure",
            "Use Close profile instead of repeating the first point");
      result.push_back(linePrimitive(points.back(), points.front(),
                                     static_cast<int>(result.size()),
                                     gesture.construction));
    }
    return result;
  }

  if (isLocalSketchPolygon(gesture.kind)) {
    const std::size_t sideCount =
        localSketchPolygonSideCount(gesture.kind, gesture.sideCount);
    if (sideCount < 3U || sideCount > 128U)
      return invalid("desktop.sketch.polygon-side-count",
                     "Regular Polygon needs between 3 and 128 sides");
    const double dx = points[1].xMetres - points[0].xMetres;
    const double dy = points[1].yMetres - points[0].yMetres;
    const double radius = std::hypot(dx, dy);
    if (radius == 0.0)
      return invalid("desktop.sketch.degenerate-polygon",
                     "Regular Polygon radius must be positive");
    const double start = std::atan2(dy, dx);
    const double turn = 2.0 * std::numbers::pi / static_cast<double>(sideCount);
    std::vector<PlanePoint> vertices;
    vertices.reserve(sideCount);
    for (std::size_t index = 0U; index < sideCount; ++index) {
      const double angle = start + turn * static_cast<double>(index);
      vertices.push_back({points[0].xMetres + radius * std::cos(angle),
                          points[0].yMetres + radius * std::sin(angle)});
    }
    std::vector<SketchPrimitiveProjection> result;
    result.reserve(sideCount);
    for (std::size_t index = 0U; index < sideCount; ++index)
      result.push_back(
          linePrimitive(vertices[index], vertices[(index + 1U) % sideCount],
                        static_cast<int>(index), gesture.construction));
    return result;
  }

  if (gesture.kind == LocalSketchToolKind::Circle) {
    const double radius = std::hypot(points[1].xMetres - points[0].xMetres,
                                     points[1].yMetres - points[0].yMetres);
    if (radius == 0.0)
      return invalid("desktop.sketch.degenerate-circle",
                     "Circle radius must be positive");
    return std::vector{circlePrimitive(
        points[0], radius, QStringLiteral("draft.0"), gesture.construction)};
  }

  if (gesture.kind == LocalSketchToolKind::ThreePointCircle) {
    if (points.size() == 2U)
      return std::vector{linePrimitive(points[0], points[1], 0, true)};
    const auto circle = circleThrough(points[0], points[1], points[2]);
    if (!circle)
      return invalid("desktop.sketch.degenerate-three-point-circle",
                     "Three-point Circle points must not be collinear");
    return std::vector{circlePrimitive(circle->center, circle->radius,
                                       QStringLiteral("draft.0"),
                                       gesture.construction)};
  }

  if (gesture.kind == LocalSketchToolKind::Rectangle ||
      gesture.kind == LocalSketchToolKind::CenterRectangle) {
    const PlanePoint first =
        gesture.kind == LocalSketchToolKind::CenterRectangle
            ? PlanePoint{2.0 * points[0].xMetres - points[1].xMetres,
                         2.0 * points[0].yMetres - points[1].yMetres}
            : points[0];
    if (first.xMetres == points[1].xMetres ||
        first.yMetres == points[1].yMetres)
      return invalid("desktop.sketch.degenerate-rectangle",
                     "Rectangle width and height must be positive");
    const std::array corners{
        first,
        PlanePoint{points[1].xMetres, first.yMetres},
        points[1],
        PlanePoint{first.xMetres, points[1].yMetres},
    };
    std::vector<SketchPrimitiveProjection> result;
    result.reserve(corners.size());
    for (std::size_t index = 0U; index < corners.size(); ++index)
      result.push_back(
          linePrimitive(corners[index], corners[(index + 1U) % corners.size()],
                        static_cast<int>(index), gesture.construction));
    return result;
  }

  if (gesture.kind == LocalSketchToolKind::Arc) {
    if (points.size() == 2U)
      return std::vector{linePrimitive(points[0], points[1], 0, true)};
    const double radius = std::hypot(points[1].xMetres - points[0].xMetres,
                                     points[1].yMetres - points[0].yMetres);
    const double start = std::atan2(points[1].yMetres - points[0].yMetres,
                                    points[1].xMetres - points[0].xMetres);
    const double end = std::atan2(points[2].yMetres - points[0].yMetres,
                                  points[2].xMetres - points[0].xMetres);
    const double sweep = positiveSweep(start, end);
    if (radius == 0.0 || sweep <= 1.0e-12)
      return invalid("desktop.sketch.degenerate-arc",
                     "Arc radius and sweep must be positive");
    return std::vector{
        arcPrimitive(points[0], radius, start, sweep, 0, gesture.construction)};
  }

  if (gesture.kind == LocalSketchToolKind::ThreePointArc) {
    if (points.size() == 2U)
      return std::vector{linePrimitive(points[0], points[1], 0, true)};
    const auto circle = circleThrough(points[0], points[1], points[2]);
    if (!circle)
      return invalid("desktop.sketch.degenerate-three-point-arc",
                     "Three-point Arc points must not be collinear");
    const double start = std::atan2(points[0].yMetres - circle->center.yMetres,
                                    points[0].xMetres - circle->center.xMetres);
    const double end = std::atan2(points[1].yMetres - circle->center.yMetres,
                                  points[1].xMetres - circle->center.xMetres);
    const double rim = std::atan2(points[2].yMetres - circle->center.yMetres,
                                  points[2].xMetres - circle->center.xMetres);
    const double counterclockwise = positiveSweep(start, end);
    const double sweep = positiveSweep(start, rim) <= counterclockwise
                             ? counterclockwise
                             : counterclockwise - 2.0 * std::numbers::pi;
    if (std::abs(sweep) <= 1.0e-12)
      return invalid("desktop.sketch.degenerate-three-point-arc",
                     "Three-point Arc endpoints must differ");
    return std::vector{arcPrimitive(circle->center, circle->radius, start,
                                    sweep, 0, gesture.construction)};
  }

  if (gesture.kind == LocalSketchToolKind::Ellipse ||
      gesture.kind == LocalSketchToolKind::ThreePointEllipse ||
      gesture.kind == LocalSketchToolKind::EllipticalArc) {
    if (points.size() == 2U)
      return std::vector{linePrimitive(points[0], points[1], 0, true)};
    const LocalSketchToolKind ellipseKind =
        gesture.kind == LocalSketchToolKind::ThreePointEllipse
            ? LocalSketchToolKind::ThreePointEllipse
            : LocalSketchToolKind::Ellipse;
    const auto ellipse = ellipseFromGesture(ellipseKind, points);
    if (!ellipse)
      return invalid("desktop.sketch.degenerate-ellipse",
                     "Ellipse axes must be positive");
    if (gesture.kind != LocalSketchToolKind::EllipticalArc)
      return std::vector{ellipsePrimitive(
          ellipse->center, ellipse->majorRadius, ellipse->minorRadius,
          ellipse->rotation, QStringLiteral("draft.0"), gesture.construction)};
    if (points.size() < 5U) {
      std::vector result{ellipsePrimitive(
          ellipse->center, ellipse->majorRadius, ellipse->minorRadius,
          ellipse->rotation, QStringLiteral("draft.0"), true)};
      if (points.size() == 4U)
        result.push_back(
            pointPrimitive(points[3], QStringLiteral("draft.start"), true));
      return result;
    }
    const double start = ellipseParameter(*ellipse, points[3]);
    const double end = ellipseParameter(*ellipse, points[4]);
    const double sweep = positiveSweep(start, end);
    if (sweep <= 1.0e-12)
      return invalid("desktop.sketch.degenerate-elliptical-arc",
                     "Elliptical Arc endpoints must differ");
    return std::vector{ellipsePrimitive(
        ellipse->center, ellipse->majorRadius, ellipse->minorRadius,
        ellipse->rotation, QStringLiteral("draft.0"), gesture.construction,
        std::pair{start, sweep})};
  }

  if (gesture.kind == LocalSketchToolKind::HyperbolicArc) {
    if (points.size() == 2U)
      return std::vector{linePrimitive(points[0], points[1], 0, true)};
    const auto hyperbola = hyperbolaFromGesture(points);
    if (!hyperbola)
      return invalid("desktop.sketch.degenerate-hyperbolic-arc",
                     "Hyperbolic Arc axes and start point are invalid");
    const double end =
        points.size() < 4U ? 0.0 : hyperbolaParameter(*hyperbola, points[3]);
    const double sweep = end - hyperbola->startParameter;
    if (!std::isfinite(sweep) || std::abs(sweep) <= 1.0e-12)
      return invalid("desktop.sketch.degenerate-hyperbolic-arc",
                     "Hyperbolic Arc endpoints must differ");
    return std::vector{
        conicArcPrimitive(SketchPrimitiveKind::HyperbolicArc, hyperbola->center,
                          hyperbola->majorRadius, hyperbola->minorRadius,
                          hyperbola->rotation, hyperbola->startParameter, sweep,
                          points.size() < 4U ? true : gesture.construction)};
  }

  if (gesture.kind == LocalSketchToolKind::ParabolicArc) {
    if (points.size() == 2U)
      return std::vector{linePrimitive(points[0], points[1], 0, true)};
    const auto parabola = parabolaFromGesture(points);
    if (!parabola)
      return invalid("desktop.sketch.degenerate-parabolic-arc",
                     "Parabolic Arc focus, vertex, and start are invalid");
    const double end =
        points.size() < 4U ? 0.0 : parabolaParameter(*parabola, points[3]);
    const double sweep = end - parabola->startParameter;
    if (!std::isfinite(sweep) || std::abs(sweep) <= 1.0e-12)
      return invalid("desktop.sketch.degenerate-parabolic-arc",
                     "Parabolic Arc endpoints must differ");
    return std::vector{
        conicArcPrimitive(SketchPrimitiveKind::ParabolicArc, parabola->vertex,
                          parabola->focalLength, 0.0, parabola->rotation,
                          parabola->startParameter, sweep,
                          points.size() < 4U ? true : gesture.construction)};
  }

  if (gesture.kind == LocalSketchToolKind::Slot ||
      gesture.kind == LocalSketchToolKind::Oblong) {
    if (points.size() == 2U) {
      if (points[0] == points[1])
        return invalid("desktop.sketch.degenerate-slot-axis",
                       "Slot centers must differ");
      return std::vector{linePrimitive(points[0], points[1], 0, true)};
    }
    const double dx = points[1].xMetres - points[0].xMetres;
    const double dy = points[1].yMetres - points[0].yMetres;
    const double lengthSquared = dx * dx + dy * dy;
    const double axisLength = std::sqrt(lengthSquared);
    if (axisLength == 0.0)
      return invalid("desktop.sketch.degenerate-slot-axis",
                     "Slot centers must differ");
    const double projection =
        std::clamp(((points[2].xMetres - points[0].xMetres) * dx +
                    (points[2].yMetres - points[0].yMetres) * dy) /
                       lengthSquared,
                   0.0, 1.0);
    const PlanePoint nearest{points[0].xMetres + projection * dx,
                             points[0].yMetres + projection * dy};
    const double radius = std::hypot(points[2].xMetres - nearest.xMetres,
                                     points[2].yMetres - nearest.yMetres);
    if (radius == 0.0)
      return invalid("desktop.sketch.degenerate-slot-width",
                     "Slot width must be positive");
    const double nx = -dy * radius / axisLength;
    const double ny = dx * radius / axisLength;
    const PlanePoint startTop{points[0].xMetres + nx, points[0].yMetres + ny};
    const PlanePoint startBottom{points[0].xMetres - nx,
                                 points[0].yMetres - ny};
    const PlanePoint endTop{points[1].xMetres + nx, points[1].yMetres + ny};
    const PlanePoint endBottom{points[1].xMetres - nx, points[1].yMetres - ny};
    const double axisAngle = std::atan2(dy, dx);
    return std::vector{
        arcPrimitive(points[0], radius, axisAngle + std::numbers::pi / 2.0,
                     std::numbers::pi, 0, gesture.construction),
        arcPrimitive(points[1], radius,
                     axisAngle + 3.0 * std::numbers::pi / 2.0, std::numbers::pi,
                     1, gesture.construction),
        linePrimitive(startTop, endTop, 2, gesture.construction),
        linePrimitive(startBottom, endBottom, 3, gesture.construction),
    };
  }

  if (gesture.kind == LocalSketchToolKind::ArcSlot) {
    const double radius = std::hypot(points[1].xMetres - points[0].xMetres,
                                     points[1].yMetres - points[0].yMetres);
    if (radius == 0.0)
      return invalid("desktop.sketch.degenerate-arc-slot-radius",
                     "Arc Slot centerline radius must be positive");
    if (points.size() == 2U)
      return std::vector{circlePrimitive(
          points[0], radius, QStringLiteral("draft.centerline"), true)};
    const double start = std::atan2(points[1].yMetres - points[0].yMetres,
                                    points[1].xMetres - points[0].xMetres);
    const double end = std::atan2(points[2].yMetres - points[0].yMetres,
                                  points[2].xMetres - points[0].xMetres);
    const double sweep = std::remainder(end - start, 2.0 * std::numbers::pi);
    if (sweep == 0.0)
      return invalid("desktop.sketch.degenerate-arc-slot-sweep",
                     "Arc Slot sweep must be nonzero");
    if (points.size() == 3U)
      return std::vector{
          arcPrimitive(points[0], radius, start, sweep, 0, true)};
    const double slotRadius =
        std::abs(std::hypot(points[3].xMetres - points[0].xMetres,
                            points[3].yMetres - points[0].yMetres) -
                 radius);
    if (slotRadius == 0.0 || slotRadius >= radius)
      return invalid("desktop.sketch.degenerate-arc-slot-width",
                     "Arc Slot width must be positive and smaller than its "
                     "centerline diameter");
    const double finish = start + sweep;
    const double direction = std::copysign(1.0, sweep);
    const PlanePoint startCenter{points[0].xMetres + radius * std::cos(start),
                                 points[0].yMetres + radius * std::sin(start)};
    const PlanePoint endCenter{points[0].xMetres + radius * std::cos(finish),
                               points[0].yMetres + radius * std::sin(finish)};
    return std::vector{
        arcPrimitive(points[0], radius + slotRadius, start, sweep, 0,
                     gesture.construction),
        arcPrimitive(endCenter, slotRadius, finish,
                     direction * std::numbers::pi, 1, gesture.construction),
        arcPrimitive(points[0], radius - slotRadius, finish, -sweep, 2,
                     gesture.construction),
        arcPrimitive(startCenter, slotRadius, start + std::numbers::pi,
                     direction * std::numbers::pi, 3, gesture.construction),
    };
  }

  return invalid("desktop.sketch.tool-kind",
                 "Sketch geometry tool kind is unsupported");
}

} // namespace kearne::ui
