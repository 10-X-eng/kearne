#include "sketch_viewport_bridge.hpp"

#include "sketch_provisional_projection.hpp"
#include "ui_session.hpp"

#include <kearne/sketch/nurbs.hpp>

#include <QByteArrayView>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QEventLoop>
#include <QQuickItem>
#include <QQuickWindow>
#include <QThread>
#include <QtEndian>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <new>
#include <span>
#include <utility>

namespace kearne::ui {
namespace {

QString pointKeyName(sketch::PointKey key) {
  switch (key) {
  case sketch::PointKey::Point:
    return QStringLiteral("point");
  case sketch::PointKey::Start:
    return QStringLiteral("start");
  case sketch::PointKey::End:
    return QStringLiteral("end");
  case sketch::PointKey::Center:
    return QStringLiteral("center");
  case sketch::PointKey::Major:
    return QStringLiteral("major");
  case sketch::PointKey::Minor:
    return QStringLiteral("minor");
  case sketch::PointKey::Focus:
    return QStringLiteral("focus");
  }
  return {};
}

std::optional<sketch::PointKey> pointKey(QStringView value) {
  if (value == QStringLiteral("point"))
    return sketch::PointKey::Point;
  if (value == QStringLiteral("start"))
    return sketch::PointKey::Start;
  if (value == QStringLiteral("end"))
    return sketch::PointKey::End;
  if (value == QStringLiteral("center"))
    return sketch::PointKey::Center;
  if (value == QStringLiteral("major"))
    return sketch::PointKey::Major;
  if (value == QStringLiteral("minor"))
    return sketch::PointKey::Minor;
  if (value == QStringLiteral("focus"))
    return sketch::PointKey::Focus;
  return std::nullopt;
}

constexpr std::array overlayRoles{
    render::SketchOverlayRole::Hovered,
    render::SketchOverlayRole::Selected,
    render::SketchOverlayRole::Preview,
    render::SketchOverlayRole::Diagnostic,
};

constexpr std::uint8_t controlPolygonAnnotation = 1U << 0U;
constexpr std::uint8_t curvatureCombAnnotation = 1U << 1U;
constexpr std::uint8_t degreeLabelAnnotation = 1U << 2U;
constexpr std::uint8_t knotLabelAnnotation = 1U << 3U;
constexpr std::uint8_t weightLabelAnnotation = 1U << 4U;

bool hasAnnotation(std::uint8_t annotations, std::uint8_t annotation) {
  return (annotations & annotation) != 0U;
}

render::SketchPickTargets pickTargets(SketchSelectionKind selection) {
  switch (selection) {
  case SketchSelectionKind::Point:
    return render::SketchPickTargets::Points;
  case SketchSelectionKind::Curve:
    return render::SketchPickTargets::Curves;
  case SketchSelectionKind::Any:
    return render::SketchPickTargets::All;
  }
  return render::SketchPickTargets::All;
}

double pointDistance(QPointF first, QPointF second) {
  return std::hypot(first.x() - second.x(), first.y() - second.y());
}

double segmentDistance(QPointF query, QPointF first, QPointF second) {
  const QPointF delta = second - first;
  const double denominator = delta.x() * delta.x() + delta.y() * delta.y();
  if (!(denominator > 0.0))
    return pointDistance(query, first);
  const QPointF relative = query - first;
  const double parameter = std::clamp(
      (relative.x() * delta.x() + relative.y() * delta.y()) / denominator, 0.0,
      1.0);
  return pointDistance(query, first + delta * parameter);
}

double rectangleDistance(QPointF query, QPointF center, double width,
                         double height) {
  const double x =
      std::max(0.0, std::abs(query.x() - center.x()) - width * 0.5);
  const double y =
      std::max(0.0, std::abs(query.y() - center.y()) - height * 0.5);
  return std::hypot(x, y);
}

double markerHitDistance(const SketchMarkerRenderRecord &marker,
                         std::span<const SketchMarkerAnchorPoint> anchors,
                         const SketchVectorRecord &record,
                         const SketchViewTransform &transform, QPointF query) {
  const auto point = [&transform](const SketchMarkerAnchorPoint &anchor) {
    return transform.toItem(anchor.positionMetres);
  };
  if (marker.category != render::SketchMarkerCategory::Dimension) {
    QPointF center;
    for (const SketchMarkerAnchorPoint &anchor : anchors)
      center += point(anchor);
    center /= static_cast<qreal>(anchors.size());
    center += QPointF{record.shape[2], record.shape[3]};
    return pointDistance(query, center);
  }

  const double textWidth = record.shape[0];
  const double textHeight = record.shape[1];
  if (marker.kind == render::SketchMarkerKind::DistanceDimension ||
      marker.kind == render::SketchMarkerKind::HorizontalDistanceDimension ||
      marker.kind == render::SketchMarkerKind::VerticalDistanceDimension) {
    const QPointF first = point(anchors[0]);
    const QPointF second = point(anchors[1]);
    QPointF firstDimension;
    QPointF secondDimension;
    if (marker.kind == render::SketchMarkerKind::HorizontalDistanceDimension) {
      firstDimension = first + QPointF{0.0, -24.0};
      secondDimension = {second.x(), first.y() - 24.0};
    } else if (marker.kind ==
               render::SketchMarkerKind::VerticalDistanceDimension) {
      firstDimension = first + QPointF{24.0, 0.0};
      secondDimension = {first.x() + 24.0, second.y()};
    } else {
      const QPointF delta = second - first;
      const double length = std::hypot(delta.x(), delta.y());
      if (!(length > 0.0))
        return pointDistance(query, first);
      QPointF normal{-delta.y() / length, delta.x() / length};
      if (normal.y() > 0.0)
        normal = -normal;
      firstDimension = first + normal * 24.0;
      secondDimension = second + normal * 24.0;
    }
    const QPointF layoutOffset{record.shape[2], record.shape[3]};
    firstDimension += layoutOffset;
    secondDimension += layoutOffset;
    const QPointF center = (firstDimension + secondDimension) * 0.5;
    return std::min({segmentDistance(query, first, firstDimension),
                     segmentDistance(query, second, secondDimension),
                     segmentDistance(query, firstDimension, secondDimension),
                     rectangleDistance(query, center, textWidth, textHeight)});
  }
  if (marker.kind == render::SketchMarkerKind::RadiusDimension ||
      marker.kind == render::SketchMarkerKind::DiameterDimension) {
    const QPointF center = point(anchors[0]);
    const QPointF rim = point(anchors[1]);
    const QPointF delta = rim - center;
    const double length = std::hypot(delta.x(), delta.y());
    if (!(length > 0.0))
      return pointDistance(query, center);
    const QPointF direction = delta / length;
    const QPointF first =
        marker.kind == render::SketchMarkerKind::DiameterDimension
            ? center - delta
            : center;
    const QPointF finish =
        rim + direction * 24.0 + QPointF{record.shape[2], record.shape[3]};
    const QPointF textCenter = finish + direction * (textWidth * 0.5 + 3.0);
    return std::min(
        segmentDistance(query, first, finish),
        rectangleDistance(query, textCenter, textWidth, textHeight));
  }
  const QPointF center = point(anchors[2]);
  const QPointF firstRay = point(anchors[0]);
  const QPointF secondRay = point(anchors[1]);
  const QPointF firstDelta = firstRay - center;
  const QPointF secondDelta = secondRay - center;
  const double firstLength = std::hypot(firstDelta.x(), firstDelta.y());
  const double secondLength = std::hypot(secondDelta.x(), secondDelta.y());
  if (!(firstLength > 0.0) || !(secondLength > 0.0))
    return pointDistance(query, center);
  const QPointF firstDirection = firstDelta / firstLength;
  const QPointF secondDirection = secondDelta / secondLength;
  const double radialDistance =
      std::hypot(query.x() - center.x(), query.y() - center.y());
  const double radius = 30.0 + std::hypot(record.shape[2], record.shape[3]);
  return std::min(
      {std::abs(radialDistance - radius),
       segmentDistance(query, center, center + firstDirection * (radius + 5.0)),
       segmentDistance(query, center,
                       center + secondDirection * (radius + 5.0))});
}

std::optional<SketchPickSelection>
pickConstraintMarker(const SketchSceneItem &item, QPointF query,
                     double tolerance) {
  const auto presented = item.presentedFrame();
  if (!presented || !presented->synchronized() || !presented->markerChunks())
    return std::nullopt;
  const auto &synchronized = *presented->synchronized();
  const auto &products = synchronized.products();
  if (!products || !products->markers() || !products->markerPacket())
    return std::nullopt;
  SketchCamera2d localCamera = synchronized.transform().camera();
  localCamera.centerMetres = synchronized.transform().toCanonical(query);
  const double reach = tolerance + 96.0;
  auto localTransform = SketchViewTransform::create(
      localCamera, QSizeF{reach * 2.0, reach * 2.0});
  if (!localTransform)
    return std::nullopt;
  auto chunks = products->markerPacket()->visibleChunks(*localTransform);
  if (!chunks)
    return std::nullopt;
  const PreparedSketchMarkers &markers = *products->markers();
  double bestDistance = std::numeric_limits<double>::infinity();
  std::optional<SketchConstraintId> best;
  std::size_t probes = 0U;
  constexpr std::size_t maximumProbes = 16'384U;
  for (std::uint32_t chunkIndex : *chunks) {
    if (!presented->markerChunks()->contains(chunkIndex) ||
        chunkIndex >= products->markerPacket()->chunks().size())
      continue;
    const auto &chunk = products->markerPacket()->chunks()[chunkIndex];
    for (const SketchVectorRecord &record : chunk->records()) {
      if (++probes > maximumProbes)
        return std::nullopt;
      auto handle = render::SketchMarkerHandle::create(record.meta[3]);
      const SketchMarkerRenderRecord *marker =
          handle ? markers.findMarker(*handle) : nullptr;
      if (!marker || !marker->constraint)
        continue;
      if ((marker->category == render::SketchMarkerCategory::Constraint &&
           !markers.display().constraintsVisible) ||
          (marker->category == render::SketchMarkerCategory::Dimension &&
           (!markers.display().dimensionsVisible ||
            (marker->visual == render::SketchMarkerVisualState::Reference &&
             !markers.display().referenceDimensionsVisible))))
        continue;
      const auto anchors = markers.markerAnchors(marker->handle);
      const double distance = markerHitDistance(
          *marker, anchors, record, synchronized.transform(), query);
      const double hitRadius =
          marker->category == render::SketchMarkerCategory::Dimension
              ? tolerance + 3.0
              : tolerance + 6.0;
      if (distance <= hitRadius && distance < bestDistance) {
        bestDistance = distance;
        best = marker->constraint;
      }
    }
  }
  if (!best)
    return std::nullopt;
  SketchPickSelection selection;
  selection.constraintId = QString::fromStdString(best->toString());
  return selection;
}

SketchConstraintDisplay constraintDisplay(QStringView unitId,
                                          bool constraintsVisible,
                                          bool dimensionsVisible,
                                          bool referenceDimensionsVisible) {
  SketchConstraintDisplay result;
  result.constraintsVisible = constraintsVisible;
  result.dimensionsVisible = dimensionsVisible;
  result.referenceDimensionsVisible = referenceDimensionsVisible;
  if (unitId == QStringLiteral("cm"))
    result.lengthUnit = SketchLengthDisplayUnit::Centimeter;
  else if (unitId == QStringLiteral("m"))
    result.lengthUnit = SketchLengthDisplayUnit::Meter;
  else if (unitId == QStringLiteral("in"))
    result.lengthUnit = SketchLengthDisplayUnit::Inch;
  return result;
}

template <typename Value>
void appendDigestValue(QCryptographicHash &hash, Value value) {
  const Value encoded = qToBigEndian(value);
  hash.addData(QByteArrayView{reinterpret_cast<const char *>(&encoded),
                              static_cast<qsizetype>(sizeof(encoded))});
}

Result<SketchProductDigest> productDigest(
    const render::SketchSceneSnapshot &scene,
    const std::shared_ptr<const render::SketchPresentationOverlay> &overlay,
    const std::shared_ptr<const render::SketchProvisionalGeometry> &provisional,
    const std::shared_ptr<const render::SketchMarkerPacket> &markers,
    SketchConstraintDisplay display, SketchMarkerEmphasis emphasis) {
  QCryptographicHash hash{QCryptographicHash::Sha256};
  const auto append = [&hash](bool present,
                              std::span<const std::uint8_t> bytes) {
    const char marker = present ? 1 : 0;
    hash.addData(QByteArrayView{&marker, 1});
    if (present)
      hash.addData(QByteArrayView{reinterpret_cast<const char *>(bytes.data()),
                                  static_cast<qsizetype>(bytes.size())});
  };
  append(true, scene.stamp().digest.bytes());
  append(static_cast<bool>(overlay),
         overlay ? std::span<const std::uint8_t>{overlay->payloadDigest().bytes}
                 : std::span<const std::uint8_t>{});
  append(static_cast<bool>(provisional),
         provisional ? provisional->stamp().payload.bytes()
                     : std::span<const std::uint8_t>{});
  append(static_cast<bool>(markers), markers ? markers->stamp().payload.bytes()
                                             : std::span<const std::uint8_t>{});
  const std::array displayBytes{
      static_cast<char>(display.lengthUnit),
      static_cast<char>(display.constraintsVisible),
      static_cast<char>(display.dimensionsVisible),
      static_cast<char>(display.referenceDimensionsVisible)};
  hash.addData(QByteArrayView{displayBytes.data(),
                              static_cast<qsizetype>(displayBytes.size())});
  appendDigestValue(hash, emphasis.selectedMarker);
  appendDigestValue(hash, emphasis.hoveredMarker);
  const QByteArray hashed = hash.result();
  SketchProductDigest::Bytes bytes{};
  if (hashed.size() != static_cast<qsizetype>(bytes.size()))
    return std::unexpected(
        diagnostic("desktop.sketch.product-digest",
                   "native Sketch product digest has an unexpected size",
                   Severity::Fatal));
  std::transform(hashed.cbegin(), hashed.cend(), bytes.begin(),
                 [](char value) { return static_cast<std::uint8_t>(value); });
  return SketchProductDigest::fromBytes("sha256", bytes);
}

Result<render::SketchMarkerDigest>
splineAnnotationDigest(const render::SketchSceneSnapshot &scene,
                       std::span<const render::SketchMarkerAnchor> anchors,
                       std::span<const render::PackedSketchMarker> markers) {
  QCryptographicHash hash{QCryptographicHash::Sha256};
  hash.addData(QByteArrayView{
      reinterpret_cast<const char *>(scene.stamp().digest.bytes().data()),
      static_cast<qsizetype>(scene.stamp().digest.bytes().size())});
  for (const render::PackedSketchMarker &marker : markers) {
    appendDigestValue(hash, marker.handle.value());
    appendDigestValue(hash, static_cast<std::uint8_t>(marker.kind));
    appendDigestValue(hash, marker.firstAnchor);
    appendDigestValue(hash, marker.anchorCount);
    appendDigestValue(hash, std::bit_cast<std::uint64_t>(marker.valueSi));
    appendDigestValue(hash, static_cast<std::uint8_t>(marker.visual));
  }
  for (const render::SketchMarkerAnchor &anchor : anchors) {
    const auto *canonical =
        std::get_if<render::SketchCanonicalMarkerAnchor>(&anchor);
    if (!canonical)
      return std::unexpected(
          diagnostic("desktop.sketch.annotation-anchor",
                     "spline annotations require canonical anchors"));
    appendDigestValue(hash, std::bit_cast<std::uint64_t>(canonical->point.x));
    appendDigestValue(hash, std::bit_cast<std::uint64_t>(canonical->point.y));
  }
  const QByteArray hashed = hash.result();
  render::SketchMarkerDigest::Bytes bytes{};
  if (hashed.size() != static_cast<qsizetype>(bytes.size()))
    return std::unexpected(diagnostic(
        "desktop.sketch.annotation-digest",
        "spline annotation digest has an unexpected size", Severity::Fatal));
  std::transform(hashed.cbegin(), hashed.cend(), bytes.begin(),
                 [](char value) { return static_cast<std::uint8_t>(value); });
  return render::SketchMarkerDigest::fromBytes("sha256", bytes);
}

Result<std::shared_ptr<const render::SketchMarkerPacket>>
splineAnnotationMarkers(
    const std::shared_ptr<const render::SketchSceneSnapshot> &scene,
    QStringView selectedEntity, std::uint8_t annotations,
    render::SketchMarkerGeneration generation) {
  const bool controlPolygon =
      hasAnnotation(annotations, controlPolygonAnnotation);
  const bool curvatureComb =
      hasAnnotation(annotations, curvatureCombAnnotation);
  const bool degreeLabel = hasAnnotation(annotations, degreeLabelAnnotation);
  const bool knotLabels = hasAnnotation(annotations, knotLabelAnnotation);
  const bool weightLabels = hasAnnotation(annotations, weightLabelAnnotation);
  const auto entity =
      SketchEntityId::parse(selectedEntity.toString().toStdString());
  const render::PackedSketchPrimitive *primitive =
      entity ? scene->findPrimitive(*entity) : nullptr;
  if (!primitive || primitive->kind != render::SketchPrimitiveKind::BSpline ||
      primitive->spline >= scene->splines().size())
    return std::unexpected(
        diagnostic("desktop.sketch.annotation-target",
                   "spline annotations require one evaluated B-spline"));
  const render::PackedSketchSpline spline = scene->splines()[primitive->spline];
  const std::size_t count = spline.controlPointCount;
  const std::size_t first = spline.firstControlPoint;
  const auto coordinates = scene->splineControlPointCoordinates();
  if (count < 2U || first > coordinates.size() / 2U ||
      count > coordinates.size() / 2U - first)
    return std::unexpected(
        diagnostic("desktop.sketch.annotation-control-range",
                   "spline annotations have an invalid control-point range"));
  const std::size_t knotCount = count + spline.degree + 1U;
  const auto knots = scene->splineKnots();
  const auto weights = scene->splineWeights();
  if (spline.firstKnot > knots.size() ||
      knotCount > knots.size() - spline.firstKnot ||
      spline.firstWeight > weights.size() ||
      count > weights.size() - spline.firstWeight)
    return std::unexpected(
        diagnostic("desktop.sketch.annotation-spline-range",
                   "spline annotations have invalid knot or weight ranges"));
  const sketch::NurbsView curve{coordinates.subspan(first * 2U, count * 2U),
                                knots.subspan(spline.firstKnot, knotCount),
                                weights.subspan(spline.firstWeight, count),
                                spline.degree};

  std::vector<render::SketchMarkerAnchor> anchors;
  std::vector<render::PackedSketchMarker> markers;
  constexpr std::size_t maximumCombSamples = 65U;
  anchors.reserve((controlPolygon ? count * 3U - 2U : 0U) +
                  (curvatureComb ? maximumCombSamples * 4U - 2U : 0U) +
                  (degreeLabel ? 1U : 0U) + (knotLabels ? knotCount : 0U) +
                  (weightLabels ? count : 0U));
  markers.reserve((controlPolygon ? count * 2U - 1U : 0U) +
                  (curvatureComb ? maximumCombSamples * 2U - 1U : 0U) +
                  (degreeLabel ? 1U : 0U) + (knotLabels ? knotCount : 0U) +
                  (weightLabels ? count : 0U));
  const auto point = [&](std::size_t index) {
    const std::size_t coordinate = (first + index) * 2U;
    return render::Point2d{coordinates[coordinate],
                           coordinates[coordinate + 1U]};
  };
  const auto appendMarker = [&](render::SketchMarkerKind kind,
                                std::span<const render::Point2d> points,
                                double markerValue = 0.0) -> Result<void> {
    if (markers.size() >= std::numeric_limits<std::uint32_t>::max() ||
        anchors.size() >
            std::numeric_limits<std::uint32_t>::max() - points.size())
      return std::unexpected(
          diagnostic("desktop.sketch.annotation-count",
                     "spline annotations exceed packed marker limits"));
    auto handle = render::SketchMarkerHandle::create(
        static_cast<std::uint32_t>(markers.size() + 1U));
    if (!handle)
      return std::unexpected(std::move(handle.error()));
    const std::uint32_t firstAnchor =
        static_cast<std::uint32_t>(anchors.size());
    for (const render::Point2d value : points)
      anchors.emplace_back(render::SketchCanonicalMarkerAnchor{value});
    markers.push_back({*handle, std::nullopt, firstAnchor,
                       static_cast<std::uint8_t>(points.size()), kind,
                       markerValue});
    return {};
  };
  if (controlPolygon) {
    for (std::size_t index = 1U; index < count; ++index) {
      const std::array segment{point(index - 1U), point(index)};
      if (auto appended = appendMarker(
              render::SketchMarkerKind::SplineControlSegment, segment);
          !appended)
        return std::unexpected(std::move(appended.error()));
    }
    for (std::size_t index = 0U; index < count; ++index) {
      const std::array pole{point(index)};
      if (auto appended =
              appendMarker(render::SketchMarkerKind::SplineControlPole, pole);
          !appended)
        return std::unexpected(std::move(appended.error()));
    }
  }

  if (curvatureComb) {
    const auto [firstParameter, lastParameter] = sketch::nurbsDomain(curve);
    const std::size_t sampleCount =
        std::clamp(count * 8U, std::size_t{17U}, maximumCombSamples);
    struct CurvatureSample {
      render::Point2d point;
      render::Point2d normal;
      double curvature = 0.0;
    };
    std::vector<CurvatureSample> samples;
    samples.reserve(sampleCount);
    double maximumCurvature = 0.0;
    double minimumX = std::numeric_limits<double>::infinity();
    double minimumY = std::numeric_limits<double>::infinity();
    double maximumX = -std::numeric_limits<double>::infinity();
    double maximumY = -std::numeric_limits<double>::infinity();
    for (std::size_t index = 0U; index < count; ++index) {
      const render::Point2d value = point(index);
      minimumX = std::min(minimumX, value.x);
      minimumY = std::min(minimumY, value.y);
      maximumX = std::max(maximumX, value.x);
      maximumY = std::max(maximumY, value.y);
    }
    for (std::size_t index = 0U; index < sampleCount; ++index) {
      const double parameter = std::lerp(
          firstParameter, lastParameter,
          static_cast<double>(index) / static_cast<double>(sampleCount - 1U));
      const sketch::NurbsPoint value = sketch::evaluateNurbs(curve, parameter);
      const sketch::NurbsPoint firstDerivative =
          sketch::differentiateNurbs(curve, parameter);
      const sketch::NurbsPoint secondDerivative =
          sketch::differentiateNurbsSecond(curve, parameter);
      const double speed = std::hypot(firstDerivative.x, firstDerivative.y);
      if (!std::isfinite(value.x) || !std::isfinite(value.y) ||
          !std::isfinite(speed) || speed <= 0.0)
        continue;
      const double curvature = (firstDerivative.x * secondDerivative.y -
                                firstDerivative.y * secondDerivative.x) /
                               (speed * speed * speed);
      if (!std::isfinite(curvature))
        continue;
      samples.push_back(
          {{value.x, value.y},
           {-firstDerivative.y / speed, firstDerivative.x / speed},
           curvature});
      maximumCurvature = std::max(maximumCurvature, std::abs(curvature));
    }
    const double span = std::max(maximumX - minimumX, maximumY - minimumY);
    if (maximumCurvature > 0.0 && std::isfinite(span) && span > 0.0) {
      const double scale = span * 0.22 / maximumCurvature;
      std::vector<render::Point2d> tips;
      tips.reserve(samples.size());
      for (const CurvatureSample &sample : samples) {
        const render::Point2d tip{
            sample.point.x + sample.normal.x * sample.curvature * scale,
            sample.point.y + sample.normal.y * sample.curvature * scale};
        tips.push_back(tip);
        if (std::abs(sample.curvature) <= maximumCurvature * 1.0e-12)
          continue;
        const std::array segment{sample.point, tip};
        if (auto appended = appendMarker(
                render::SketchMarkerKind::SplineCurvatureSegment, segment);
            !appended)
          return std::unexpected(std::move(appended.error()));
      }
      for (std::size_t index = 1U; index < tips.size(); ++index) {
        if (tips[index] == tips[index - 1U])
          continue;
        const std::array segment{tips[index - 1U], tips[index]};
        if (auto appended = appendMarker(
                render::SketchMarkerKind::SplineCurvatureSegment, segment);
            !appended)
          return std::unexpected(std::move(appended.error()));
      }
    }
  }
  if (degreeLabel) {
    render::Point2d centroid;
    for (std::size_t index = 0U; index < count; ++index) {
      centroid.x += point(index).x;
      centroid.y += point(index).y;
    }
    centroid.x /= static_cast<double>(count);
    centroid.y /= static_cast<double>(count);
    const std::array position{centroid};
    if (auto appended =
            appendMarker(render::SketchMarkerKind::SplineDegreeLabel, position,
                         static_cast<double>(spline.degree));
        !appended)
      return std::unexpected(std::move(appended.error()));
  }
  if (knotLabels) {
    const auto [firstParameter, lastParameter] = sketch::nurbsDomain(curve);
    std::size_t index = 0U;
    while (index < curve.knots.size()) {
      std::size_t finish = index + 1U;
      while (finish < curve.knots.size() &&
             curve.knots[finish] == curve.knots[index])
        ++finish;
      const double parameter = curve.knots[index];
      if (parameter >= firstParameter && parameter <= lastParameter) {
        const sketch::NurbsPoint evaluated =
            sketch::evaluateNurbs(curve, parameter);
        const std::array position{render::Point2d{evaluated.x, evaluated.y}};
        if (auto appended = appendMarker(
                render::SketchMarkerKind::SplineKnotMultiplicityLabel, position,
                static_cast<double>(finish - index));
            !appended)
          return std::unexpected(std::move(appended.error()));
      }
      index = finish;
    }
  }
  if (weightLabels) {
    for (std::size_t index = 0U; index < count; ++index) {
      const std::array position{point(index)};
      if (auto appended =
              appendMarker(render::SketchMarkerKind::SplinePoleWeightLabel,
                           position, curve.weights[index]);
          !appended)
        return std::unexpected(std::move(appended.error()));
    }
  }
  auto digest = splineAnnotationDigest(*scene, anchors, markers);
  if (!digest)
    return std::unexpected(std::move(digest.error()));
  return render::SketchMarkerPacket::create(
      {{scene->stamp(), std::nullopt, std::nullopt, std::nullopt},
       generation,
       *digest},
      scene, nullptr, anchors, markers);
}

} // namespace

Result<std::unique_ptr<SketchViewportBridge>>
SketchViewportBridge::create(QQuickItem &host, UiSession &session,
                             SketchCameraController &camera) {
  try {
    auto bridge = std::unique_ptr<SketchViewportBridge>(
        new SketchViewportBridge{host, session, camera});
    if (auto initialized = bridge->initialize(); !initialized)
      return std::unexpected(std::move(initialized.error()));
    return bridge;
  } catch (const std::bad_alloc &) {
    return std::unexpected(
        diagnostic("desktop.sketch.viewport-allocation",
                   "native Sketch viewport allocation failed"));
  }
}

SketchViewportBridge::SketchViewportBridge(QQuickItem &host, UiSession &session,
                                           SketchCameraController &camera)
    : host_(host), session_(session), camera_(camera),
      item_(std::make_unique<SketchSceneItem>(&host_)),
      publication_(std::make_unique<SketchScenePublicationController>(
          *item_, executor_)) {}

SketchViewportBridge::~SketchViewportBridge() {
  if (!stopped_)
    static_cast<void>(shutdown());
}

Result<void> SketchViewportBridge::initialize() {
  if (QThread::currentThread() != host_.thread() ||
      QThread::currentThread() != session_.thread() ||
      QThread::currentThread() != camera_.thread())
    return std::unexpected(
        diagnostic("desktop.sketch.viewport-thread",
                   "native Sketch viewport must be created on the UI thread"));
  if (!publication_->metrics().subscribed)
    return std::unexpected(publication_->lastDiagnostic());

  item_->setObjectName(QStringLiteral("nativeSketchScene"));
  item_->setParentItem(&host_);
  synchronizeGeometry();
  QObject::connect(&host_, &QQuickItem::widthChanged, this,
                   [this] { synchronizeGeometry(); });
  QObject::connect(&host_, &QQuickItem::heightChanged, this,
                   [this] { synchronizeGeometry(); });
  QObject::connect(&host_, &QQuickItem::windowChanged, this,
                   [this](QQuickWindow *window) { subscribeToWindow(window); });
  subscribeToWindow(host_.window());
  QObject::connect(&session_, &UiSession::projectionChanged, this,
                   [this] { record(publishProjection()); });
  QObject::connect(&session_, &UiSession::sketchGesturePreviewChanged, this,
                   [this] { record(publishProjection()); });
  QObject::connect(&camera_, &SketchCameraController::cameraChanged, this,
                   [this] { record(publishCamera()); });
  session_.setSketchPickHandler([this](QPointF point, double tolerance,
                                       SketchSelectionKind selection)
                                    -> std::optional<SketchPickSelection> {
    lastPickItemPoint_ = point;
    if (selection == SketchSelectionKind::Any &&
        session_.activeCommandId().isEmpty())
      if (auto marker = pickConstraintMarker(*item_, point, tolerance))
        return marker;
    auto evidence =
        publication_->pick(point, tolerance, pickTargets(selection));
    if (!evidence || !evidence->item.hit)
      return std::nullopt;
    const auto &hit = *evidence->item.hit;
    return SketchPickSelection{QString::fromStdString(hit.entity.toString()),
                               hit.pointKey ? pointKeyName(*hit.pointKey)
                                            : QString{},
                               {millimetersFromMetres(hit.closestPoint.x),
                                millimetersFromMetres(hit.closestPoint.y)}};
  });
  session_.setSketchHoverHandler(
      [this](std::optional<SketchPickSelection> selection) {
        if (selection)
          lastHoverItemPoint_ = lastPickItemPoint_;
        else {
          lastHoverItemPoint_.reset();
          hoverRepickPending_ = false;
        }
        std::vector<SketchSelectionScope> scopes;
        hoveredConstraintId_ = selection ? selection->constraintId : QString{};
        if (selection) {
          if (!selection->constraintId.isEmpty())
            scopes = session_.sketchConstraintScopes(selection->constraintId);
          else if (!selection->entityId.isEmpty())
            scopes.push_back({selection->entityId, selection->pointKey});
        }
        record(publishOverlay(scopes, selected_));
      });
  if (auto cameraResult = publishCamera(); !cameraResult)
    return cameraResult;
  return publishProjection();
}

void SketchViewportBridge::synchronizeGeometry() {
  item_->setPosition({0.0, 0.0});
  item_->setSize({host_.width(), host_.height()});
}

Result<void> SketchViewportBridge::publishCamera() {
  auto result = publication_->publishCamera(camera_.camera());
  if (!result)
    return std::unexpected(std::move(result.error()));
  switch (*result) {
  case SketchCameraDecision::Accepted:
  case SketchCameraDecision::Duplicate:
    return {};
  case SketchCameraDecision::StaleGeneration:
    return std::unexpected(diagnostic(
        "desktop.sketch.camera-stale",
        "native Sketch viewport rejected a stale camera generation"));
  case SketchCameraDecision::GenerationConflict:
    return std::unexpected(
        diagnostic("desktop.sketch.camera-generation-conflict",
                   "native Sketch viewport rejected conflicting camera state"));
  }
  return std::unexpected(
      diagnostic("desktop.sketch.camera-decision",
                 "native Sketch viewport returned an unknown camera decision"));
}

Result<void> SketchViewportBridge::publishProjection() {
  item_->setPipelineWarmup(
      {.lowDegreeNurbs =
           session_.activeCommandId() == QStringLiteral("sketch.join") ||
           session_.activeCommandId() ==
               QStringLiteral("sketch.bspline.convert-to-nurbs"),
       .generalNurbs = false});
  const auto nextScene = session_.sketchScene();
  const bool sceneChanged = nextScene != publishedScene_;
  const bool sameSemanticTarget =
      nextScene && publishedScene_ &&
      nextScene->stamp().target == publishedScene_->stamp().target;
  if (sceneChanged && !sameSemanticTarget)
    hoverRepickPending_ = !hovered_.empty() && lastHoverItemPoint_.has_value();
  const std::vector<SketchSelectionScope> retainedHover =
      !sceneChanged || sameSemanticTarget ? hovered_
                                          : std::vector<SketchSelectionScope>{};
  if (auto scene = publishScene(); !scene)
    return scene;
  if (auto provisional = publishProvisional(); !provisional)
    return provisional;
  return publishOverlay(retainedHover, session_.selectedSketchScopes());
}

Result<void> SketchViewportBridge::publishScene() {
  const auto scene = session_.sketchScene();
  if (scene == publishedScene_)
    return {};
  if (!scene) {
    publishedScene_.reset();
    overlay_.reset();
    provisional_.reset();
    markers_.reset();
    overlayRoleSets_ = {};
    hovered_.clear();
    selected_.clear();
    publishedDraft_.clear();
    provisionalCommand_.clear();
    requestedProducts_.reset();
    hoverRepickPending_ = false;
    presentationPublished_ = false;
    splineAnnotationsPublished_ = 0U;
    constraintDisplayPublished_ = {};
    markerEmphasisPublished_ = {};
    hoveredConstraintId_.clear();
    item_->clearPresentation();
    return {};
  }
  if (!publishedScene_ ||
      publishedScene_->stamp().target != scene->stamp().target) {
    if (auto targeted = publication_->retarget(scene->stamp().target);
        !targeted)
      return targeted;
  }
  publishedScene_ = scene;
  overlay_.reset();
  provisional_.reset();
  markers_.reset();
  overlayRoleSets_ = {};
  hovered_.clear();
  selected_.clear();
  publishedDraft_.clear();
  presentationPublished_ = false;
  splineAnnotationsPublished_ = 0U;
  return {};
}

Result<void> SketchViewportBridge::publishProvisional() {
  if (!publishedScene_)
    return {};
  std::vector<SketchPrimitiveProjection> draft;
  for (const SketchPrimitiveProjection &primitive :
       session_.sketchPrimitiveProjections()) {
    if (primitive.draft)
      draft.push_back(primitive);
  }
  const auto gesture = session_.sketchGesturePreviewPrimitives();
  draft.insert(draft.end(), gesture.begin(), gesture.end());
  const QString command = session_.activeCommandId();
  if (command != provisionalCommand_) {
    if (toolInstanceGeneration_ == std::numeric_limits<std::uint64_t>::max())
      return std::unexpected(
          diagnostic("desktop.sketch.tool-instance-generation-exhausted",
                     "native Sketch tool instance generation is exhausted",
                     Severity::Fatal));
    ++toolInstanceGeneration_;
    provisionalCommand_ = command;
    publishedDraft_.clear();
    provisional_.reset();
    presentationPublished_ = false;
  }
  if (draft == publishedDraft_)
    return {};
  if (draft.empty()) {
    publishedDraft_.clear();
    provisional_.reset();
    presentationPublished_ = false;
    return {};
  }
  if (provisionalCommand_.isEmpty())
    return std::unexpected(
        diagnostic("desktop.sketch.provisional-without-tool",
                   "native Sketch preview has no active tool instance"));
  if (provisionalGeneration_ == std::numeric_limits<std::uint64_t>::max())
    return std::unexpected(diagnostic(
        "desktop.sketch.provisional-generation-exhausted",
        "native Sketch preview generation is exhausted", Severity::Fatal));
  auto editSession = render::SketchEditSessionHandle::create(1U);
  auto toolInstance =
      render::SketchToolInstanceHandle::create(toolInstanceGeneration_);
  auto generation =
      render::SketchProvisionalGeneration::create(++provisionalGeneration_);
  if (!editSession)
    return std::unexpected(std::move(editSession.error()));
  if (!toolInstance)
    return std::unexpected(std::move(toolInstance.error()));
  if (!generation)
    return std::unexpected(std::move(generation.error()));
  auto projected = projectSketchProvisional(
      {publishedScene_->stamp(), *editSession, *toolInstance, *generation},
      draft);
  if (!projected)
    return std::unexpected(std::move(projected.error()));
  provisional_ = std::move(*projected);
  publishedDraft_ = std::move(draft);
  presentationPublished_ = false;
  return {};
}

Result<void> SketchViewportBridge::publishOverlay(
    std::span<const SketchSelectionScope> hover,
    std::span<const SketchSelectionScope> selected) {
  const std::vector<SketchSelectionScope> hoveredSelections{hover.begin(),
                                                            hover.end()};
  const std::vector<SketchSelectionScope> selectedSelections{selected.begin(),
                                                             selected.end()};
  if (!publishedScene_)
    return {};
  const bool selectedBSpline =
      selectedSelections.size() == 1U &&
      selectedSelections.front().pointKey.isEmpty() && [&] {
        const auto entity = SketchEntityId::parse(
            selectedSelections.front().entityId.toStdString());
        const render::PackedSketchPrimitive *primitive =
            entity ? publishedScene_->findPrimitive(*entity) : nullptr;
        return primitive &&
               primitive->kind == render::SketchPrimitiveKind::BSpline;
      }();
  std::uint8_t requestedAnnotations = 0U;
  if (selectedBSpline) {
    if (session_.sketchControlPolygonVisible())
      requestedAnnotations |= controlPolygonAnnotation;
    if (session_.sketchCurvatureCombVisible())
      requestedAnnotations |= curvatureCombAnnotation;
    if (session_.sketchDegreeLabelsVisible())
      requestedAnnotations |= degreeLabelAnnotation;
    if (session_.sketchKnotLabelsVisible())
      requestedAnnotations |= knotLabelAnnotation;
    if (session_.sketchWeightLabelsVisible())
      requestedAnnotations |= weightLabelAnnotation;
  }
  std::shared_ptr<const render::SketchMarkerPacket> semanticMarkers =
      session_.sketchConstraintMarkers();
  // A transient curve-drag scene intentionally withholds markers based on the
  // committed scene until the matching evaluated revision arrives.
  if (semanticMarkers && semanticMarkers->base() != publishedScene_)
    semanticMarkers.reset();
  const bool markerPresentationCurrent = requestedAnnotations != 0U
                                             ? static_cast<bool>(markers_)
                                             : markers_ == semanticMarkers;
  const SketchConstraintDisplay display = constraintDisplay(
      session_.projectLengthUnitId(), session_.sketchConstraintsVisible(),
      session_.sketchDimensionsVisible(),
      session_.sketchReferenceDimensionsVisible());
  SketchMarkerEmphasis emphasis;
  if (requestedAnnotations == 0U && semanticMarkers) {
    const auto markerHandle = [&semanticMarkers](const QString &constraintId) {
      const auto id = SketchConstraintId::parse(constraintId.toStdString());
      const render::PackedSketchMarker *marker =
          id ? semanticMarkers->findConstraint(*id) : nullptr;
      return marker ? marker->handle.value() : 0U;
    };
    emphasis.selectedMarker = markerHandle(session_.selectedEntityId());
    emphasis.hoveredMarker = markerHandle(hoveredConstraintId_);
  }
  if (hoveredSelections == hovered_ && selectedSelections == selected_ &&
      requestedAnnotations == splineAnnotationsPublished_ &&
      markerPresentationCurrent && display == constraintDisplayPublished_ &&
      emphasis == markerEmphasisPublished_ && presentationPublished_)
    return {};
  const bool wantsOverlay = !hover.empty() || !selected.empty();
  const bool overlayChanged =
      hoveredSelections != hovered_ || selectedSelections != selected_ ||
      (wantsOverlay && !overlay_) || (!wantsOverlay && overlay_);
  if (productGeneration_ == std::numeric_limits<std::uint64_t>::max() ||
      (wantsOverlay && overlayChanged &&
       presentationGeneration_ == std::numeric_limits<std::uint64_t>::max()))
    return std::unexpected(diagnostic(
        "desktop.sketch.presentation-generation-exhausted",
        "native Sketch presentation generation is exhausted", Severity::Fatal));

  std::shared_ptr<const render::SketchPresentationOverlay> overlay;
  std::shared_ptr<const render::SketchMarkerPacket> markers;
  std::vector<render::SketchOverlayScope> hoveredOverlayScopes;
  hoveredOverlayScopes.reserve(hover.size());
  for (const SketchSelectionScope &selection : hover) {
    auto entity = SketchEntityId::parse(selection.entityId.toStdString());
    if (!entity || !publishedScene_->findPrimitive(*entity))
      return std::unexpected(
          diagnostic("desktop.sketch.hover-entity",
                     "native Sketch hover references missing geometry"));
    std::optional<sketch::PointKey> point;
    if (!selection.pointKey.isEmpty()) {
      point = pointKey(selection.pointKey);
      if (!point)
        return std::unexpected(
            diagnostic("desktop.sketch.hover-point",
                       "native Sketch hover references an invalid point"));
    }
    hoveredOverlayScopes.push_back({*entity, point});
  }
  std::vector<render::SketchOverlayScope> selectedOverlayScopes;
  selectedOverlayScopes.reserve(selected.size());
  for (const SketchSelectionScope &selection : selected) {
    auto entity = SketchEntityId::parse(selection.entityId.toStdString());
    if (!entity || !publishedScene_->findPrimitive(*entity))
      return std::unexpected(
          diagnostic("desktop.sketch.selection-entity",
                     "native Sketch selection references missing geometry"));
    std::optional<sketch::PointKey> point;
    if (!selection.pointKey.isEmpty()) {
      point = pointKey(selection.pointKey);
      if (!point)
        return std::unexpected(
            diagnostic("desktop.sketch.selection-point",
                       "native Sketch selection references an invalid point"));
    }
    selectedOverlayScopes.push_back({*entity, point});
  }
  if (wantsOverlay && !overlayChanged) {
    overlay = overlay_;
  } else if (!hoveredOverlayScopes.empty() || !selectedOverlayScopes.empty()) {
    for (std::size_t index = 0U; index < overlayRoles.size(); ++index) {
      const bool unchanged = (index == 0U && hoveredSelections == hovered_) ||
                             (index == 1U && selectedSelections == selected_) ||
                             index > 1U;
      if (unchanged && overlayRoleSets_[index] &&
          overlayRoleSets_[index]->base() == publishedScene_)
        continue;
      std::span<const render::SketchOverlayScope> scopes;
      if (index == 0U)
        scopes = hoveredOverlayScopes;
      else if (index == 1U)
        scopes = selectedOverlayScopes;
      auto created = render::SketchOverlayRoleSet::create(
          publishedScene_, overlayRoles[index], scopes);
      if (!created)
        return std::unexpected(std::move(created.error()));
      overlayRoleSets_[index] = std::move(*created);
    }
    auto generation =
        render::SketchPresentationGeneration::create(++presentationGeneration_);
    if (!generation)
      return std::unexpected(std::move(generation.error()));
    auto created = render::SketchPresentationOverlay::create(
        publishedScene_, *generation, overlayRoleSets_);
    if (!created)
      return std::unexpected(std::move(created.error()));
    overlay = std::move(*created);
  }

  if (requestedAnnotations == 0U) {
    markers = std::move(semanticMarkers);
  } else if (requestedAnnotations == splineAnnotationsPublished_ &&
             selectedSelections == selected_ && markers_) {
    markers = markers_;
  } else if (requestedAnnotations != 0U) {
    if (markerGeneration_ == std::numeric_limits<std::uint64_t>::max())
      return std::unexpected(diagnostic(
          "desktop.sketch.marker-generation-exhausted",
          "native Sketch marker generation is exhausted", Severity::Fatal));
    auto generation =
        render::SketchMarkerGeneration::create(++markerGeneration_);
    if (!generation)
      return std::unexpected(std::move(generation.error()));
    auto created = splineAnnotationMarkers(publishedScene_,
                                           selectedSelections.front().entityId,
                                           requestedAnnotations, *generation);
    if (!created)
      return std::unexpected(std::move(created.error()));
    markers = std::move(*created);
  }

  auto generation = SketchProductGeneration::create(++productGeneration_);
  if (!generation)
    return std::unexpected(std::move(generation.error()));
  auto digest = productDigest(*publishedScene_, overlay, provisional_, markers,
                              display, emphasis);
  if (!digest)
    return std::unexpected(std::move(digest.error()));
  const SketchProductStamp stamp{publishedScene_->stamp().target, *generation,
                                 *digest};
  auto offered =
      publication_->publishProducts({stamp, publishedScene_, overlay,
                                     provisional_, markers, display, emphasis});
  if (!offered)
    return std::unexpected(std::move(offered.error()));
  requestedProducts_ = stamp;
  hovered_ = std::move(hoveredSelections);
  selected_ = std::move(selectedSelections);
  overlay_ = std::move(overlay);
  markers_ = std::move(markers);
  splineAnnotationsPublished_ = requestedAnnotations;
  constraintDisplayPublished_ = display;
  markerEmphasisPublished_ = emphasis;
  presentationPublished_ = true;
  return {};
}

void SketchViewportBridge::subscribeToWindow(QQuickWindow *window) {
  QObject::disconnect(frameSwappedConnection_);
  frameSwappedConnection_ = {};
  if (!window)
    return;
  frameSwappedConnection_ =
      QObject::connect(window, &QQuickWindow::frameSwapped, this,
                       [this] { repickHoverAfterPresentedFrame(); });
}

void SketchViewportBridge::repickHoverAfterPresentedFrame() {
  if (!hoverRepickPending_ || !lastHoverItemPoint_ || stopped_ ||
      !presentationCurrent())
    return;
  const QPointF point = *lastHoverItemPoint_;
  hoverRepickPending_ = false;
  static_cast<void>(session_.updateSketchPointerHover(point.x(), point.y()));
}

void SketchViewportBridge::record(Result<void> result) {
  if (!result)
    lastDiagnostic_ = std::move(result.error());
}

Result<void> SketchViewportBridge::shutdown(std::chrono::milliseconds timeout) {
  if (stopped_)
    return {};
  if (QThread::currentThread() != thread())
    return std::unexpected(
        diagnostic("desktop.sketch.viewport-shutdown-thread",
                   "native Sketch viewport must stop on the UI thread"));

  QObject::disconnect(frameSwappedConnection_);
  frameSwappedConnection_ = {};
  hoverRepickPending_ = false;
  session_.clearSketchPickHandler();
  session_.clearSketchHoverHandler();

  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (publication_) {
    auto stopped = publication_->shutdown();
    if (stopped) {
      publication_.reset();
      break;
    }
    if (stopped.error().code != "desktop.sketch.preparation-backpressure" ||
        std::chrono::steady_clock::now() >= deadline)
      return std::unexpected(std::move(stopped.error()));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
  }

  executor_.requestShutdown();
  while (!executor_.waitUntilDrained(std::chrono::milliseconds{0})) {
    if (std::chrono::steady_clock::now() >= deadline)
      return std::unexpected(
          diagnostic("desktop.sketch.viewport-shutdown-timeout",
                     "native Sketch viewport did not drain before shutdown"));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    QThread::yieldCurrentThread();
  }
  executor_.join();
  item_.reset();
  stopped_ = true;
  return {};
}

bool SketchViewportBridge::presentationCurrent() const {
  const auto expected = session_.sketchScene();
  if (!expected)
    return true;
  const auto frame = item_->presentedFrame();
  return frame && frame->synchronized() && requestedProducts_ &&
         frame->synchronized()->scene() == expected &&
         frame->synchronized()->products()->stamp() == *requestedProducts_ &&
         item_->requestedPipelinesReady();
}

QString SketchViewportBridge::presentationStatus() const {
  const auto expected = session_.sketchScene();
  const auto frame = item_->presentedFrame();
  const auto synchronized = frame ? frame->synchronized() : nullptr;
  const bool sceneCurrent = synchronized && synchronized->scene() == expected;
  const bool productsCurrent =
      synchronized && requestedProducts_ &&
      synchronized->products()->stamp() == *requestedProducts_;
  const bool pipelinesReady = item_->requestedPipelinesReady();
  const Diagnostic publicationDiagnostic =
      publication_ ? publication_->lastDiagnostic() : Diagnostic{};
  return QStringLiteral("expected=%1 frame=%2 requested=%3 scene=%4 "
                        "products=%5 pipelines=%6 bridge=%7 publication=%8")
      .arg(expected ? 1 : 0)
      .arg(synchronized ? 1 : 0)
      .arg(requestedProducts_ ? 1 : 0)
      .arg(sceneCurrent ? 1 : 0)
      .arg(productsCurrent ? 1 : 0)
      .arg(pipelinesReady ? 1 : 0)
      .arg(QString::fromStdString(lastDiagnostic_.code))
      .arg(QString::fromStdString(publicationDiagnostic.code));
}

} // namespace kearne::ui
