#include "sketch_gesture_preview.hpp"
#include "sketch_tool_gesture.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace kearne::ui {
namespace {

constexpr double maximumCoordinateMillimeters = 1.0e9;

bool validCoordinate(double value) {
  return std::isfinite(value) &&
         std::abs(value) <= maximumCoordinateMillimeters;
}

QPointF millimeters(PlanePoint point) {
  return {millimetersFromMetres(point.xMetres),
          millimetersFromMetres(point.yMetres)};
}

PlanePoint midpoint(PlanePoint first, PlanePoint second) {
  return {(first.xMetres + second.xMetres) * 0.5,
          (first.yMetres + second.yMetres) * 0.5};
}

PlanePoint normalOrigin(PlanePoint first, PlanePoint second) {
  const PlanePoint anchor = midpoint(first, second);
  double normalX = first.yMetres - second.yMetres;
  double normalY = second.xMetres - first.xMetres;
  if (normalY < 0.0 || (normalY == 0.0 && normalX < 0.0)) {
    normalX = -normalX;
    normalY = -normalY;
  }
  return {anchor.xMetres - normalX, anchor.yMetres - normalY};
}

double distance(PlanePoint first, PlanePoint second) {
  return std::hypot(second.xMetres - first.xMetres,
                    second.yMetres - first.yMetres);
}

void length(std::vector<SketchPreviewMeasurement> &result, QString prefix,
            double valueMetres, PlanePoint anchor, PlanePoint origin) {
  if (std::isfinite(valueMetres) && valueMetres > 0.0)
    result.push_back({std::move(prefix), SketchPreviewQuantity::Length,
                      valueMetres, millimeters(anchor), millimeters(origin)});
}

void angle(std::vector<SketchPreviewMeasurement> &result, QString prefix,
           double valueRadians, PlanePoint anchor, PlanePoint origin) {
  if (std::isfinite(valueRadians))
    result.push_back({std::move(prefix), SketchPreviewQuantity::Angle,
                      std::abs(valueRadians), millimeters(anchor),
                      millimeters(origin)});
}

PlanePoint radialPoint(const SketchPrimitiveProjection &primitive,
                       double angleRadians, double radius) {
  const PlanePoint center = primitive.points.front();
  return {center.xMetres + radius * std::cos(angleRadians),
          center.yMetres + radius * std::sin(angleRadians)};
}

std::vector<SketchPreviewMeasurement>
measurementsFor(LocalSketchToolKind kind, std::span<const PlanePoint> points,
                std::span<const SketchPrimitiveProjection> primitives) {
  std::vector<SketchPreviewMeasurement> result;
  if (points.size() < 2U)
    return result;

  const auto lastSegment = [&] {
    const PlanePoint first = points[points.size() - 2U];
    const PlanePoint second = points.back();
    length(result, {}, distance(first, second), midpoint(first, second),
           normalOrigin(first, second));
  };
  const auto onlyCurvedPrimitive = [&]() -> const SketchPrimitiveProjection * {
    const auto found = std::ranges::find_if(primitives, [](const auto &item) {
      return item.kind != SketchPrimitiveKind::Point &&
             item.kind != SketchPrimitiveKind::Line;
    });
    return found == primitives.end() ? nullptr : &*found;
  };

  if (kind == LocalSketchToolKind::Line ||
      kind == LocalSketchToolKind::Polyline || isLocalSketchBSpline(kind)) {
    lastSegment();
    return result;
  }

  if (kind == LocalSketchToolKind::Rectangle ||
      kind == LocalSketchToolKind::CenterRectangle) {
    if (primitives.size() >= 2U &&
        primitives[0].kind == SketchPrimitiveKind::Line &&
        primitives[1].kind == SketchPrimitiveKind::Line) {
      const PlanePoint center =
          primitives.size() >= 3U && !primitives[2].points.empty()
              ? midpoint(primitives[0].points.front(),
                         primitives[2].points.front())
              : midpoint(points[0], points[1]);
      for (std::size_t index = 0U; index < 2U; ++index) {
        const auto &line = primitives[index];
        if (line.points.size() == 2U)
          length(result, {}, distance(line.points[0], line.points[1]),
                 midpoint(line.points[0], line.points[1]), center);
      }
    }
    return result;
  }

  if (isLocalSketchPolygon(kind)) {
    length(result, QStringLiteral("R "), distance(points[0], points[1]),
           points[1], points[0]);
    if (!primitives.empty() &&
        primitives.front().kind == SketchPrimitiveKind::Line &&
        primitives.front().points.size() == 2U)
      length(
          result, QStringLiteral("Side "),
          distance(primitives.front().points[0], primitives.front().points[1]),
          midpoint(primitives.front().points[0], primitives.front().points[1]),
          points[0]);
    return result;
  }

  if (kind == LocalSketchToolKind::Circle) {
    length(result, QStringLiteral("R "), distance(points[0], points[1]),
           points[1], points[0]);
    return result;
  }

  if (kind == LocalSketchToolKind::ThreePointCircle) {
    if (const auto *circle = onlyCurvedPrimitive();
        circle && circle->kind == SketchPrimitiveKind::Circle) {
      length(result, QStringLiteral("R "), circle->radiusMetres,
             radialPoint(*circle, 0.0, circle->radiusMetres),
             circle->points.front());
    } else {
      lastSegment();
    }
    return result;
  }

  if (kind == LocalSketchToolKind::Arc ||
      kind == LocalSketchToolKind::ThreePointArc) {
    if (kind == LocalSketchToolKind::Arc)
      length(result, QStringLiteral("R "), distance(points[0], points[1]),
             points[1], points[0]);
    if (const auto *arc = onlyCurvedPrimitive();
        arc && arc->kind == SketchPrimitiveKind::Arc) {
      if (kind == LocalSketchToolKind::ThreePointArc)
        length(result, QStringLiteral("R "), arc->radiusMetres,
               radialPoint(*arc, arc->startAngleRadians, arc->radiusMetres),
               arc->points.front());
      angle(result, QStringLiteral("∠ "), arc->sweepAngleRadians,
            radialPoint(*arc,
                        arc->startAngleRadians + arc->sweepAngleRadians * 0.5,
                        arc->radiusMetres),
            arc->points.front());
    } else if (kind == LocalSketchToolKind::ThreePointArc) {
      lastSegment();
    }
    return result;
  }

  if (kind == LocalSketchToolKind::Ellipse ||
      kind == LocalSketchToolKind::ThreePointEllipse ||
      kind == LocalSketchToolKind::EllipticalArc) {
    if (const auto *ellipse = onlyCurvedPrimitive();
        ellipse && (ellipse->kind == SketchPrimitiveKind::Ellipse ||
                    ellipse->kind == SketchPrimitiveKind::EllipticalArc)) {
      const PlanePoint center = ellipse->points.front();
      length(result, QStringLiteral("A "), ellipse->radiusMetres,
             radialPoint(*ellipse, ellipse->rotationAngleRadians,
                         ellipse->radiusMetres),
             center);
      length(result, QStringLiteral("B "), ellipse->secondaryRadiusMetres,
             radialPoint(*ellipse,
                         ellipse->rotationAngleRadians + std::numbers::pi / 2.0,
                         ellipse->secondaryRadiusMetres),
             center);
      if (kind == LocalSketchToolKind::EllipticalArc && points.size() >= 4U) {
        const PlanePoint rim = points[3];
        const double offsetX = rim.xMetres - center.xMetres;
        const double offsetY = rim.yMetres - center.yMetres;
        const double cosine = std::cos(ellipse->rotationAngleRadians);
        const double sine = std::sin(ellipse->rotationAngleRadians);
        const double parameter = std::atan2(
            (-sine * offsetX + cosine * offsetY) /
                ellipse->secondaryRadiusMetres,
            (cosine * offsetX + sine * offsetY) / ellipse->radiusMetres);
        angle(result, QStringLiteral("Start "), parameter, rim, center);
      }
      if (ellipse->kind == SketchPrimitiveKind::EllipticalArc)
        angle(result, QStringLiteral("Sweep "), ellipse->sweepAngleRadians,
              radialPoint(*ellipse, ellipse->rotationAngleRadians,
                          ellipse->radiusMetres),
              center);
    } else {
      lastSegment();
    }
    return result;
  }

  if (kind == LocalSketchToolKind::HyperbolicArc ||
      kind == LocalSketchToolKind::ParabolicArc) {
    if (const auto *conic = onlyCurvedPrimitive()) {
      const PlanePoint origin = conic->points.front();
      length(
          result,
          kind == LocalSketchToolKind::ParabolicArc ? QStringLiteral("F ")
                                                    : QStringLiteral("A "),
          conic->radiusMetres,
          radialPoint(*conic, conic->rotationAngleRadians, conic->radiusMetres),
          origin);
      if (kind == LocalSketchToolKind::HyperbolicArc)
        length(result, QStringLiteral("B "), conic->secondaryRadiusMetres,
               radialPoint(*conic,
                           conic->rotationAngleRadians + std::numbers::pi / 2.0,
                           conic->secondaryRadiusMetres),
               origin);
    } else {
      lastSegment();
    }
    return result;
  }

  if (kind == LocalSketchToolKind::Slot ||
      kind == LocalSketchToolKind::Oblong) {
    length(result, QStringLiteral("Centers "), distance(points[0], points[1]),
           midpoint(points[0], points[1]), normalOrigin(points[0], points[1]));
    if (points.size() >= 3U && !primitives.empty() &&
        primitives.front().kind == SketchPrimitiveKind::Arc)
      length(result, QStringLiteral("Width "),
             2.0 * primitives.front().radiusMetres, points.back(), points[0]);
    return result;
  }

  if (kind == LocalSketchToolKind::ArcSlot) {
    length(result, QStringLiteral("R "), distance(points[0], points[1]),
           points[1], points[0]);
    if (points.size() >= 3U) {
      const double start = std::atan2(points[1].yMetres - points[0].yMetres,
                                      points[1].xMetres - points[0].xMetres);
      const double finish = std::atan2(points[2].yMetres - points[0].yMetres,
                                       points[2].xMetres - points[0].xMetres);
      angle(result, QStringLiteral("∠ "),
            std::remainder(finish - start, 2.0 * std::numbers::pi), points[2],
            points[0]);
    }
    if (points.size() >= 4U && primitives.size() >= 3U)
      length(result, QStringLiteral("Width "),
             primitives[0].radiusMetres - primitives[2].radiusMetres,
             points.back(), points[0]);
    return result;
  }

  lastSegment();
  return result;
}

} // namespace

SketchGesturePreview::SketchGesturePreview(QObject *parent) : QObject(parent) {}

bool SketchGesturePreview::updateGesture(
    const QString &commandId, std::span<const QPointF> pointsMillimeters,
    bool construction, const QString &methodId, bool closed,
    std::size_t sideCount, std::uint32_t degree) {
  const auto refuse = [this] {
    clear();
    return false;
  };
  if (pointsMillimeters.empty() ||
      std::ranges::any_of(pointsMillimeters, [](QPointF point) {
        return !validCoordinate(point.x()) || !validCoordinate(point.y());
      }))
    return refuse();
  const LocalSketchToolDefinition *definition =
      localSketchToolDefinition(QStringView{commandId}, QStringView{methodId});
  if (!definition ||
      (definition->maximumInputPointCount > 0U &&
       pointsMillimeters.size() > definition->maximumInputPointCount))
    return refuse();

  LocalSketchToolGesture gesture{definition->kind, {},        construction,
                                 closed,           sideCount, degree};
  gesture.points.reserve(pointsMillimeters.size());
  std::vector<PlanePoint> canonical;
  canonical.reserve(pointsMillimeters.size());
  for (QPointF point : pointsMillimeters) {
    const PlanePoint value{metresFromMillimeters(point.x()),
                           metresFromMillimeters(point.y())};
    canonical.push_back(value);
    gesture.points.push_back({value.xMetres, value.yMetres});
  }
  auto projected = projectLocalSketchToolGesture(gesture, false);
  if (!projected)
    return refuse();
  auto measurements = measurementsFor(definition->kind, canonical, *projected);
  std::vector<QPointF> inputPoints{pointsMillimeters.begin(),
                                   pointsMillimeters.end()};
  if (visible_ && inputPoints_ == inputPoints &&
      construction_ == construction && primitives_ == *projected &&
      measurements_ == measurements)
    return true;
  inputPoints_ = std::move(inputPoints);
  primitives_ = std::move(*projected);
  measurements_ = std::move(measurements);
  construction_ = construction;
  visible_ = true;
  ++generation_;
  emit previewChanged();
  return true;
}

void SketchGesturePreview::clear() {
  if (!visible_)
    return;
  visible_ = false;
  inputPoints_.clear();
  primitives_.clear();
  measurements_.clear();
  ++generation_;
  emit previewChanged();
}

} // namespace kearne::ui
