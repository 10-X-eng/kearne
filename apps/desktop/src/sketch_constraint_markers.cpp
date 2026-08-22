#include "sketch_constraint_markers.hpp"

#include <QByteArrayView>
#include <QCryptographicHash>
#include <QtEndian>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <new>
#include <numbers>
#include <numeric>
#include <utility>
#include <variant>
#include <vector>

namespace kearne::ui {
namespace {

template <typename Value>
void appendDigestValue(QCryptographicHash &hash, Value value) {
  const Value encoded = qToBigEndian(value);
  hash.addData(QByteArrayView{reinterpret_cast<const char *>(&encoded),
                              static_cast<qsizetype>(sizeof(encoded))});
}

template <typename Id> void appendId(QCryptographicHash &hash, const Id &id) {
  hash.addData(QByteArrayView{reinterpret_cast<const char *>(id.bytes().data()),
                              static_cast<qsizetype>(id.bytes().size())});
}

render::SketchMarkerVisualState
markerVisualState(sketch::ConstraintState state) {
  switch (state) {
  case sketch::ConstraintState::Driving:
    return render::SketchMarkerVisualState::Active;
  case sketch::ConstraintState::Reference:
    return render::SketchMarkerVisualState::Reference;
  case sketch::ConstraintState::Suppressed:
    return render::SketchMarkerVisualState::Suppressed;
  case sketch::ConstraintState::Redundant:
    return render::SketchMarkerVisualState::Redundant;
  case sketch::ConstraintState::Conflicting:
    return render::SketchMarkerVisualState::Conflicting;
  }
  return render::SketchMarkerVisualState::Conflicting;
}

render::SketchMarkerAnchor pointAnchor(sketch::PointRef point) {
  return render::SketchBaseMarkerAnchor{
      point.entity, render::SketchMarkerPointLocation{point.key}};
}

render::SketchMarkerAnchor curveAnchor(SketchEntityId entity,
                                       double parameter = 0.5) {
  return render::SketchBaseMarkerAnchor{
      entity, render::SketchMarkerCurveLocation{parameter}};
}

Result<render::Point2d>
entityPresentationPoint(const render::SketchSceneSnapshot &scene,
                        SketchEntityId entity) {
  const render::PackedSketchPrimitive *primitive = scene.findPrimitive(entity);
  if (!primitive)
    return std::unexpected(
        diagnostic("desktop.sketch.constraint-marker-entity",
                   "constraint marker references missing evaluated geometry"));
  if (primitive->kind == render::SketchPrimitiveKind::Point) {
    const auto point =
        render::semanticPoint(scene, *primitive, sketch::PointKey::Point);
    if (point)
      return *point;
  }
  if (primitive->kind != render::SketchPrimitiveKind::BSpline) {
    return render::resolveSketchMarkerAnchor(curveAnchor(entity), scene);
  }
  if (primitive->spline >= scene.splines().size())
    return std::unexpected(diagnostic(
        "desktop.sketch.constraint-marker-spline",
        "constraint marker references invalid evaluated spline geometry"));
  const render::PackedSketchSpline spline = scene.splines()[primitive->spline];
  if (spline.controlPointCount == 0U ||
      spline.firstControlPoint >
          scene.splineControlPointCoordinates().size() / 2U ||
      spline.controlPointCount >
          scene.splineControlPointCoordinates().size() / 2U -
              spline.firstControlPoint)
    return std::unexpected(diagnostic(
        "desktop.sketch.constraint-marker-spline",
        "constraint marker references invalid evaluated spline geometry"));
  render::Point2d result;
  const auto coordinates = scene.splineControlPointCoordinates();
  for (std::size_t index = 0U; index < spline.controlPointCount; ++index) {
    const std::size_t coordinate =
        (static_cast<std::size_t>(spline.firstControlPoint) + index) * 2U;
    result.x += coordinates[coordinate];
    result.y += coordinates[coordinate + 1U];
  }
  const double count = static_cast<double>(spline.controlPointCount);
  return render::Point2d{result.x / count, result.y / count};
}

Result<render::SketchMarkerAnchor>
entityAnchor(const render::SketchSceneSnapshot &scene, SketchEntityId entity) {
  const render::PackedSketchPrimitive *primitive = scene.findPrimitive(entity);
  if (!primitive)
    return std::unexpected(
        diagnostic("desktop.sketch.constraint-marker-entity",
                   "constraint marker references missing evaluated geometry"));
  if (primitive->kind == render::SketchPrimitiveKind::Point)
    return pointAnchor({entity, sketch::PointKey::Point});
  if (primitive->kind != render::SketchPrimitiveKind::BSpline)
    return curveAnchor(entity);
  auto point = entityPresentationPoint(scene, entity);
  if (!point)
    return std::unexpected(std::move(point.error()));
  return render::SketchCanonicalMarkerAnchor{*point};
}

Result<double> pointDistance(const render::SketchSceneSnapshot &scene,
                             sketch::PointRef first, sketch::PointRef second,
                             bool horizontal, bool vertical) {
  auto firstPoint =
      render::resolveSketchMarkerAnchor(pointAnchor(first), scene);
  auto secondPoint =
      render::resolveSketchMarkerAnchor(pointAnchor(second), scene);
  if (!firstPoint)
    return std::unexpected(std::move(firstPoint.error()));
  if (!secondPoint)
    return std::unexpected(std::move(secondPoint.error()));
  const double x = secondPoint->x - firstPoint->x;
  const double y = secondPoint->y - firstPoint->y;
  if (horizontal)
    return std::abs(x);
  if (vertical)
    return std::abs(y);
  return std::hypot(x, y);
}

Result<double> curveRadius(const render::SketchSceneSnapshot &scene,
                           SketchEntityId entity, bool diameter) {
  const render::PackedSketchPrimitive *primitive = scene.findPrimitive(entity);
  if (!primitive || (primitive->kind != render::SketchPrimitiveKind::Circle &&
                     primitive->kind != render::SketchPrimitiveKind::Arc))
    return std::unexpected(
        diagnostic("desktop.sketch.constraint-marker-radius",
                   "radius dimension references invalid evaluated geometry"));
  return primitive->radius * (diameter ? 2.0 : 1.0);
}

Result<double> angleBetween(const render::SketchSceneSnapshot &scene,
                            SketchEntityId first, SketchEntityId second) {
  const auto direction =
      [&scene](SketchEntityId entity) -> Result<render::Point2d> {
    const render::PackedSketchPrimitive *primitive =
        scene.findPrimitive(entity);
    if (!primitive || primitive->kind != render::SketchPrimitiveKind::Line)
      return std::unexpected(diagnostic(
          "desktop.sketch.constraint-marker-angle",
          "angle dimension references invalid evaluated line geometry"));
    const auto start =
        render::semanticPoint(scene, *primitive, sketch::PointKey::Start);
    const auto end =
        render::semanticPoint(scene, *primitive, sketch::PointKey::End);
    if (!start || !end)
      return std::unexpected(diagnostic(
          "desktop.sketch.constraint-marker-angle",
          "angle dimension cannot resolve evaluated line endpoints"));
    const render::Point2d result{end->x - start->x, end->y - start->y};
    if (!(std::hypot(result.x, result.y) > 0.0))
      return std::unexpected(
          diagnostic("desktop.sketch.constraint-marker-angle",
                     "angle dimension references a degenerate evaluated line"));
    return result;
  };
  auto firstDirection = direction(first);
  auto secondDirection = direction(second);
  if (!firstDirection)
    return std::unexpected(std::move(firstDirection.error()));
  if (!secondDirection)
    return std::unexpected(std::move(secondDirection.error()));
  const double denominator = std::hypot(firstDirection->x, firstDirection->y) *
                             std::hypot(secondDirection->x, secondDirection->y);
  const double cosine = std::clamp((firstDirection->x * secondDirection->x +
                                    firstDirection->y * secondDirection->y) /
                                       denominator,
                                   -1.0, 1.0);
  return std::acos(cosine);
}

Result<std::array<render::Point2d, 3>>
angleDimensionAnchors(const render::SketchSceneSnapshot &scene,
                      SketchEntityId first, SketchEntityId second) {
  const auto line =
      [&scene](
          SketchEntityId entity) -> Result<std::array<render::Point2d, 2>> {
    const render::PackedSketchPrimitive *primitive =
        scene.findPrimitive(entity);
    if (!primitive || primitive->kind != render::SketchPrimitiveKind::Line)
      return std::unexpected(diagnostic(
          "desktop.sketch.constraint-marker-angle",
          "angle dimension references invalid evaluated line geometry"));
    const auto start =
        render::semanticPoint(scene, *primitive, sketch::PointKey::Start);
    const auto end =
        render::semanticPoint(scene, *primitive, sketch::PointKey::End);
    if (!start || !end)
      return std::unexpected(diagnostic(
          "desktop.sketch.constraint-marker-angle",
          "angle dimension cannot resolve evaluated line endpoints"));
    return std::array{*start, *end};
  };
  auto firstLine = line(first);
  auto secondLine = line(second);
  if (!firstLine)
    return std::unexpected(std::move(firstLine.error()));
  if (!secondLine)
    return std::unexpected(std::move(secondLine.error()));
  const render::Point2d firstDirection{(*firstLine)[1].x - (*firstLine)[0].x,
                                       (*firstLine)[1].y - (*firstLine)[0].y};
  const render::Point2d secondDirection{(*secondLine)[1].x - (*secondLine)[0].x,
                                        (*secondLine)[1].y -
                                            (*secondLine)[0].y};
  const double firstLength = std::hypot(firstDirection.x, firstDirection.y);
  const double secondLength = std::hypot(secondDirection.x, secondDirection.y);
  if (!(firstLength > 0.0) || !(secondLength > 0.0))
    return std::unexpected(diagnostic(
        "desktop.sketch.constraint-marker-angle",
        "angle dimension references degenerate evaluated line geometry"));
  const double cross = firstDirection.x * secondDirection.y -
                       firstDirection.y * secondDirection.x;
  render::Point2d center{std::midpoint((*firstLine)[0].x, (*firstLine)[1].x),
                         std::midpoint((*firstLine)[0].y, (*firstLine)[1].y)};
  if (std::abs(cross) > firstLength * secondLength * 1.0e-12) {
    const render::Point2d separation{(*secondLine)[0].x - (*firstLine)[0].x,
                                     (*secondLine)[0].y - (*firstLine)[0].y};
    const double parameter =
        (separation.x * secondDirection.y - separation.y * secondDirection.x) /
        cross;
    center = {(*firstLine)[0].x + parameter * firstDirection.x,
              (*firstLine)[0].y + parameter * firstDirection.y};
  }
  const double reach = std::min(firstLength, secondLength) * 0.5;
  return std::array{
      center,
      render::Point2d{center.x + firstDirection.x / firstLength * reach,
                      center.y + firstDirection.y / firstLength * reach},
      render::Point2d{center.x + secondDirection.x / secondLength * reach,
                      center.y + secondDirection.y / secondLength * reach}};
}

struct MarkerDefinition {
  render::SketchMarkerKind kind =
      render::SketchMarkerKind::CoincidentConstraint;
  std::array<std::optional<render::SketchMarkerAnchor>, 3> anchors;
  std::uint8_t anchorCount = 0U;
  double valueSi = 0.0;
};

Result<MarkerDefinition>
markerDefinition(const sketch::Constraint &constraint,
                 const render::SketchSceneSnapshot &scene) {
  MarkerDefinition result;
  const auto add = [&result](render::SketchMarkerAnchor anchor) {
    result.anchors[result.anchorCount++].emplace(std::move(anchor));
  };
  const auto addEntity = [&](SketchEntityId entity) -> Result<void> {
    auto anchor = entityAnchor(scene, entity);
    if (!anchor)
      return std::unexpected(std::move(anchor.error()));
    add(std::move(*anchor));
    return {};
  };
  auto projected = std::visit(
      [&](const auto &value) -> Result<void> {
        using Value = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Value, sketch::Coincident>) {
          result.kind = render::SketchMarkerKind::CoincidentConstraint;
          add(pointAnchor(value.first));
          add(pointAnchor(value.second));
        } else if constexpr (std::is_same_v<Value, sketch::Horizontal>) {
          result.kind = render::SketchMarkerKind::HorizontalConstraint;
          add(curveAnchor(value.line));
        } else if constexpr (std::is_same_v<Value, sketch::Vertical>) {
          result.kind = render::SketchMarkerKind::VerticalConstraint;
          add(curveAnchor(value.line));
        } else if constexpr (std::is_same_v<Value, sketch::Parallel> ||
                             std::is_same_v<Value, sketch::Perpendicular> ||
                             std::is_same_v<Value, sketch::Tangent> ||
                             std::is_same_v<Value, sketch::Equal> ||
                             std::is_same_v<Value, sketch::Collinear>) {
          if constexpr (std::is_same_v<Value, sketch::Parallel>)
            result.kind = render::SketchMarkerKind::ParallelConstraint;
          else if constexpr (std::is_same_v<Value, sketch::Perpendicular>)
            result.kind = render::SketchMarkerKind::PerpendicularConstraint;
          else if constexpr (std::is_same_v<Value, sketch::Tangent>)
            result.kind = render::SketchMarkerKind::TangentConstraint;
          else if constexpr (std::is_same_v<Value, sketch::Equal>)
            result.kind = render::SketchMarkerKind::EqualConstraint;
          else
            result.kind = render::SketchMarkerKind::CollinearConstraint;
          add(curveAnchor(value.first));
          add(curveAnchor(value.second));
        } else if constexpr (std::is_same_v<Value, sketch::Concentric>) {
          result.kind = render::SketchMarkerKind::ConcentricConstraint;
          add(pointAnchor({value.first, sketch::PointKey::Center}));
          add(pointAnchor({value.second, sketch::PointKey::Center}));
        } else if constexpr (std::is_same_v<Value, sketch::Midpoint>) {
          result.kind = render::SketchMarkerKind::MidpointConstraint;
          add(pointAnchor(value.point));
        } else if constexpr (std::is_same_v<Value, sketch::PointOnObject>) {
          result.kind = render::SketchMarkerKind::PointOnObjectConstraint;
          add(pointAnchor(value.point));
        } else if constexpr (std::is_same_v<Value, sketch::Symmetric>) {
          result.kind = render::SketchMarkerKind::SymmetricConstraint;
          add(pointAnchor(value.first));
          add(pointAnchor(value.second));
          add(curveAnchor(value.axis));
        } else if constexpr (std::is_same_v<Value,
                                            sketch::SymmetricAboutPoint>) {
          result.kind = render::SketchMarkerKind::SymmetricAboutPointConstraint;
          add(pointAnchor(value.first));
          add(pointAnchor(value.second));
          add(pointAnchor(value.center));
        } else if constexpr (std::is_same_v<Value, sketch::Lock>) {
          result.kind = render::SketchMarkerKind::FixedConstraint;
          add(pointAnchor(value.point));
        } else if constexpr (std::is_same_v<Value, sketch::Block>) {
          result.kind = render::SketchMarkerKind::FixedConstraint;
          return addEntity(value.entity);
        } else if constexpr (std::is_same_v<Value, sketch::Group>) {
          result.kind = render::SketchMarkerKind::GroupConstraint;
          if (value.entities.empty())
            return std::unexpected(
                diagnostic("desktop.sketch.constraint-marker-group",
                           "group constraint has no evaluated geometry"));
          render::Point2d centroid;
          for (SketchEntityId entity : value.entities) {
            auto point = entityPresentationPoint(scene, entity);
            if (!point)
              return std::unexpected(std::move(point.error()));
            centroid.x += point->x;
            centroid.y += point->y;
          }
          const double count = static_cast<double>(value.entities.size());
          add(render::SketchCanonicalMarkerAnchor{
              {centroid.x / count, centroid.y / count}});
        } else if constexpr (std::is_same_v<Value, sketch::Snell>) {
          result.kind = render::SketchMarkerKind::RefractionConstraint;
          add(pointAnchor(value.incident));
          add(pointAnchor(value.refracted));
          add(curveAnchor(value.boundary));
        } else if constexpr (std::is_same_v<Value, sketch::Distance> ||
                             std::is_same_v<Value,
                                            sketch::HorizontalDistance> ||
                             std::is_same_v<Value, sketch::VerticalDistance>) {
          constexpr bool horizontal =
              std::is_same_v<Value, sketch::HorizontalDistance>;
          constexpr bool vertical =
              std::is_same_v<Value, sketch::VerticalDistance>;
          result.kind =
              horizontal ? render::SketchMarkerKind::HorizontalDistanceDimension
              : vertical ? render::SketchMarkerKind::VerticalDistanceDimension
                         : render::SketchMarkerKind::DistanceDimension;
          add(pointAnchor(value.first));
          add(pointAnchor(value.second));
          if (value.properties.dimensionMode == sketch::DimensionMode::Driving)
            result.valueSi = value.value.si();
          else {
            auto measured = pointDistance(scene, value.first, value.second,
                                          horizontal, vertical);
            if (!measured)
              return std::unexpected(std::move(measured.error()));
            result.valueSi = *measured;
          }
        } else if constexpr (std::is_same_v<Value, sketch::Radius> ||
                             std::is_same_v<Value, sketch::Diameter>) {
          constexpr bool diameter = std::is_same_v<Value, sketch::Diameter>;
          result.kind = diameter ? render::SketchMarkerKind::DiameterDimension
                                 : render::SketchMarkerKind::RadiusDimension;
          add(pointAnchor({value.curve, sketch::PointKey::Center}));
          add(curveAnchor(value.curve, 0.125));
          if (value.properties.dimensionMode == sketch::DimensionMode::Driving)
            result.valueSi = value.value.si();
          else {
            auto measured = curveRadius(scene, value.curve, diameter);
            if (!measured)
              return std::unexpected(std::move(measured.error()));
            result.valueSi = *measured;
          }
        } else if constexpr (std::is_same_v<Value, sketch::AngleBetween>) {
          result.kind = render::SketchMarkerKind::AngleDimension;
          auto dimensionAnchors =
              angleDimensionAnchors(scene, value.first, value.second);
          if (!dimensionAnchors)
            return std::unexpected(std::move(dimensionAnchors.error()));
          add(curveAnchor(value.first, 0.25));
          add(curveAnchor(value.second, 0.25));
          add(render::SketchCanonicalMarkerAnchor{(*dimensionAnchors)[0]});
          if (value.properties.dimensionMode == sketch::DimensionMode::Driving)
            result.valueSi = value.value.si();
          else {
            auto measured = angleBetween(scene, value.first, value.second);
            if (!measured)
              return std::unexpected(std::move(measured.error()));
            result.valueSi = *measured;
          }
        }
        return {};
      },
      constraint);
  if (!projected)
    return std::unexpected(std::move(projected.error()));
  return result;
}

Result<render::SketchMarkerDigest>
markerDigest(const render::SketchSceneSnapshot &scene,
             std::span<const render::SketchMarkerAnchor> anchors,
             std::span<const render::PackedSketchMarker> markers) {
  QCryptographicHash hash{QCryptographicHash::Sha256};
  hash.addData(QByteArrayView{
      reinterpret_cast<const char *>(scene.stamp().digest.bytes().data()),
      static_cast<qsizetype>(scene.stamp().digest.bytes().size())});
  for (const render::PackedSketchMarker &marker : markers) {
    appendDigestValue(hash, marker.handle.value());
    appendId(hash, *marker.constraint);
    appendDigestValue(hash, marker.firstAnchor);
    appendDigestValue(hash, marker.anchorCount);
    appendDigestValue(hash, static_cast<std::uint8_t>(marker.kind));
    appendDigestValue(hash, std::bit_cast<std::uint64_t>(marker.valueSi));
    appendDigestValue(hash, static_cast<std::uint8_t>(marker.visual));
  }
  for (const render::SketchMarkerAnchor &anchor : anchors) {
    std::visit(
        [&hash](const auto &value) {
          using Value = std::decay_t<decltype(value)>;
          if constexpr (std::is_same_v<Value, render::SketchBaseMarkerAnchor>) {
            appendDigestValue(hash, std::uint8_t{1U});
            appendId(hash, value.entity);
            std::visit(
                [&hash](const auto &location) {
                  using Location = std::decay_t<decltype(location)>;
                  if constexpr (std::is_same_v<
                                    Location,
                                    render::SketchMarkerPointLocation>) {
                    appendDigestValue(hash, std::uint8_t{1U});
                    appendDigestValue(
                        hash, static_cast<std::uint8_t>(location.point));
                  } else {
                    appendDigestValue(hash, std::uint8_t{2U});
                    appendDigestValue(hash, std::bit_cast<std::uint64_t>(
                                                location.normalizedParameter));
                  }
                },
                value.location);
          } else if constexpr (std::is_same_v<
                                   Value,
                                   render::SketchCanonicalMarkerAnchor>) {
            appendDigestValue(hash, std::uint8_t{2U});
            appendDigestValue(hash,
                              std::bit_cast<std::uint64_t>(value.point.x));
            appendDigestValue(hash,
                              std::bit_cast<std::uint64_t>(value.point.y));
          } else {
            appendDigestValue(hash, std::uint8_t{3U});
          }
        },
        anchor);
  }
  const QByteArray hashed = hash.result();
  render::SketchMarkerDigest::Bytes bytes{};
  if (hashed.size() != static_cast<qsizetype>(bytes.size()))
    return std::unexpected(diagnostic(
        "desktop.sketch.constraint-marker-digest",
        "constraint marker digest has an unexpected size", Severity::Fatal));
  std::transform(hashed.cbegin(), hashed.cend(), bytes.begin(),
                 [](char value) { return static_cast<std::uint8_t>(value); });
  return render::SketchMarkerDigest::fromBytes("sha256", bytes);
}

} // namespace

Result<std::shared_ptr<const render::SketchMarkerPacket>>
projectSketchConstraintMarkers(
    std::span<const sketch::Constraint> constraints,
    std::span<const sketch::ConstraintHealth> health,
    std::shared_ptr<const render::SketchSceneSnapshot> scene,
    render::SketchMarkerGeneration generation,
    render::SketchMarkerLimits limits, std::stop_token cancellation) {
  if (!scene)
    return std::unexpected(
        diagnostic("desktop.sketch.constraint-marker-scene",
                   "constraint markers require an evaluated scene"));
  if (constraints.size() != health.size())
    return std::unexpected(diagnostic(
        "desktop.sketch.constraint-marker-health",
        "constraint marker health does not match constraint declarations"));
  if (constraints.size() > std::numeric_limits<std::uint32_t>::max() ||
      constraints.size() > limits.maximumMarkerCount ||
      constraints.size() > limits.maximumAnchorCount / 3U)
    return std::unexpected(
        diagnostic("desktop.sketch.constraint-marker-limit",
                   "constraint marker projection exceeds its bounded count"));
  try {
    std::vector<render::SketchMarkerAnchor> anchors;
    std::vector<render::PackedSketchMarker> markers;
    anchors.reserve(constraints.size() * 3U);
    markers.reserve(constraints.size());
    for (std::size_t index = 0U; index < constraints.size(); ++index) {
      if (cancellation.stop_requested())
        return std::unexpected(
            diagnostic("desktop.sketch.constraint-marker-cancelled",
                       "constraint marker projection was cancelled"));
      const sketch::Constraint &constraint = constraints[index];
      if (health[index].constraint != sketch::constraintId(constraint))
        return std::unexpected(
            diagnostic("desktop.sketch.constraint-marker-health",
                       "constraint marker health identity is out of order"));
      auto definition = markerDefinition(constraint, *scene);
      if (!definition)
        return std::unexpected(std::move(definition.error()));
      auto handle = render::SketchMarkerHandle::create(
          static_cast<std::uint32_t>(index + 1U));
      if (!handle)
        return std::unexpected(std::move(handle.error()));
      const auto firstAnchor = static_cast<std::uint32_t>(anchors.size());
      for (std::uint8_t anchor = 0U; anchor < definition->anchorCount; ++anchor)
        anchors.push_back(std::move(*definition->anchors[anchor]));
      markers.push_back({*handle, sketch::constraintId(constraint), firstAnchor,
                         definition->anchorCount, definition->kind,
                         definition->valueSi,
                         markerVisualState(health[index].state)});
    }
    auto digest = markerDigest(*scene, anchors, markers);
    if (!digest)
      return std::unexpected(std::move(digest.error()));
    const render::SceneStamp sceneStamp = scene->stamp();
    return render::SketchMarkerPacket::create(
        {{sceneStamp, std::nullopt, std::nullopt, std::nullopt},
         generation,
         *digest},
        std::move(scene), nullptr, anchors, markers, limits, cancellation);
  } catch (const std::bad_alloc &) {
    return std::unexpected(diagnostic(
        "desktop.sketch.constraint-marker-allocation",
        "constraint marker projection could not allocate bounded storage"));
  } catch (const std::length_error &) {
    return std::unexpected(
        diagnostic("desktop.sketch.constraint-marker-limit",
                   "constraint marker projection exceeds vector capacity"));
  }
}

} // namespace kearne::ui
