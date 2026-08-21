#pragma once

#include <kearne/render/sketch_scene.hpp>
#include <kearne/sketch/nurbs.hpp>
#include <kearne/testkit/property.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace kearne::render::test {

template <typename Id> Id id(std::uint64_t value) {
  typename Id::RandomTail random{};
  for (std::size_t index = 0; index < random.size(); ++index)
    random[index] = static_cast<std::uint8_t>(value >> ((index % 8U) * 8U));
  auto result = Id::create(value & ((std::uint64_t{1} << 48U) - 1U), random);
  if (!result)
    throw std::runtime_error("generated UUIDv7 was invalid");
  return *result;
}

template <typename Digest> Digest digest(std::uint64_t value) {
  typename Digest::Bytes bytes{};
  for (std::size_t index = 0; index < bytes.size(); ++index)
    bytes[index] = static_cast<std::uint8_t>(value >> ((index % 8U) * 8U));
  auto result = Digest::fromBytes("blake3-256", bytes);
  if (!result)
    throw std::runtime_error("generated digest was invalid");
  return *result;
}

inline SceneStamp stamp(std::uint64_t session, std::uint64_t generation,
                        std::uint64_t attachmentBinding,
                        std::uint64_t planeRevision, std::uint64_t evaluation,
                        std::uint64_t sceneDigest) {
  auto sessionHandle = RenderSessionHandle::create(session);
  auto sceneGeneration = SceneGeneration::create(generation);
  if (!sessionHandle || !sceneGeneration)
    throw std::runtime_error("generated scene stamp was invalid");
  return {{*sessionHandle,
           {id<ModelBindingId>(attachmentBinding),
            digest<RevisionId>(planeRevision)},
           digest<EvaluationKey>(evaluation)},
          *sceneGeneration,
          digest<SceneDigest>(sceneDigest)};
}

inline std::vector<SketchStyle> styles() {
  return {
      {SketchStyleRole::Regular, SketchLinePattern::Solid, 1.5F, 7.0F, 0},
      {SketchStyleRole::Construction, SketchLinePattern::Dashed, 1.0F, 6.0F, 1},
      {SketchStyleRole::Regular, SketchLinePattern::Solid, 2.0F, 8.0F, 4},
      {SketchStyleRole::Regular, SketchLinePattern::Dotted, 1.5F, 7.0F, 3},
      {SketchStyleRole::Regular, SketchLinePattern::Solid, 2.5F, 9.0F, 5},
  };
}

inline std::shared_ptr<const SketchSceneSnapshot>
scene(std::size_t count, std::uint64_t seed, SceneStamp sceneStamp) {
  constexpr std::size_t styleCount = 5;
  testkit::Random random{seed};
  std::vector<Point2d> points;
  std::vector<PackedSketchPrimitive> primitives;
  points.reserve(count * 2U);
  primitives.reserve(count);
  const std::size_t columns = std::max<std::size_t>(
      1, static_cast<std::size_t>(std::ceil(std::sqrt(count))));
  for (std::size_t index = 0; index < count; ++index) {
    const double column = static_cast<double>(index % columns);
    const double row = static_cast<double>(index / columns);
    const Point2d center{column * 0.04 + random.between(-0.003, 0.003),
                         row * 0.04 + random.between(-0.003, 0.003)};
    const auto handle =
        SketchPrimitiveHandle::create(static_cast<std::uint32_t>(index + 1U));
    if (!handle)
      throw std::runtime_error("generated primitive handle was invalid");
    const auto flags =
        index % 31U == 0U ? SketchPrimitiveFlags::Visible
        : index % 29U == 0U
            ? SketchPrimitiveFlags::Selectable
            : SketchPrimitiveFlags::Visible | SketchPrimitiveFlags::Selectable;
    PackedSketchPrimitive primitive{
        id<SketchEntityId>(seed + index + 1U),
        *handle,
        static_cast<std::uint32_t>(points.size()),
        static_cast<std::uint16_t>(index % styleCount),
        static_cast<SketchPrimitiveKind>(index % 4U + 1U),
        flags,
        0.0,
        0.0,
        0.0,
    };
    switch (primitive.kind) {
    case SketchPrimitiveKind::Point:
      points.push_back(center);
      break;
    case SketchPrimitiveKind::Line:
      points.push_back({center.x - random.between(0.004, 0.014),
                        center.y - random.between(0.004, 0.014)});
      points.push_back({center.x + random.between(0.004, 0.014),
                        center.y + random.between(0.004, 0.014)});
      break;
    case SketchPrimitiveKind::Circle:
      points.push_back(center);
      primitive.radius = random.between(0.004, 0.015);
      break;
    case SketchPrimitiveKind::Arc:
      points.push_back(center);
      primitive.radius = random.between(0.004, 0.015);
      primitive.startAngleRadians =
          random.between(-std::numbers::pi, std::numbers::pi);
      primitive.sweepAngleRadians =
          random.between(0.15, 5.5) * (index % 2U == 0U ? 1.0 : -1.0);
      break;
    case SketchPrimitiveKind::Ellipse:
    case SketchPrimitiveKind::EllipticalArc:
      points.push_back(center);
      primitive.radius = random.between(0.006, 0.014);
      primitive.secondaryRadius = random.between(0.002, primitive.radius);
      primitive.rotationAngleRadians =
          random.between(-std::numbers::pi, std::numbers::pi);
      if (primitive.kind == SketchPrimitiveKind::EllipticalArc) {
        primitive.startAngleRadians = random.between(-3.0, 3.0);
        primitive.sweepAngleRadians =
            random.between(0.15, 5.5) * (index % 2U == 0U ? 1.0 : -1.0);
      }
      break;
    case SketchPrimitiveKind::HyperbolicArc:
      points.push_back(center);
      primitive.radius = random.between(0.003, 0.008);
      primitive.secondaryRadius = random.between(0.002, 0.010);
      primitive.rotationAngleRadians =
          random.between(-std::numbers::pi, std::numbers::pi);
      primitive.startAngleRadians = random.between(-1.2, -0.2);
      primitive.sweepAngleRadians = random.between(0.4, 2.4);
      break;
    case SketchPrimitiveKind::ParabolicArc:
      points.push_back(center);
      primitive.radius = random.between(0.003, 0.008);
      primitive.rotationAngleRadians =
          random.between(-std::numbers::pi, std::numbers::pi);
      primitive.startAngleRadians = random.between(-0.012, -0.002);
      primitive.sweepAngleRadians = random.between(0.004, 0.024);
      break;
    case SketchPrimitiveKind::BSpline:
      throw std::runtime_error("generic scene generator selected B-spline");
    }
    primitives.push_back(primitive);
  }
  auto created =
      SketchSceneSnapshot::create(std::move(sceneStamp), styles(),
                                  std::move(points), std::move(primitives));
  if (!created)
    throw std::runtime_error(created.error().code);
  return std::make_shared<const SketchSceneSnapshot>(std::move(*created));
}

enum class PickSceneProfile : std::uint8_t {
  Sparse = 1,
  Coincident = 2,
  Concentric = 3,
  GlobalLines = 4,
  Outlier = 5,
};

inline std::shared_ptr<const SketchSceneSnapshot>
pickScene(std::size_t count, PickSceneProfile profile, std::uint64_t seed,
          SceneStamp sceneStamp) {
  if (profile == PickSceneProfile::Sparse)
    return scene(count, seed, std::move(sceneStamp));
  std::vector<Point2d> points;
  std::vector<PackedSketchPrimitive> primitives;
  points.reserve(count * 2U);
  primitives.reserve(count);
  const std::size_t columns = std::max<std::size_t>(
      1, static_cast<std::size_t>(std::ceil(std::sqrt(count))));
  for (std::size_t index = 0; index < count; ++index) {
    auto handle =
        SketchPrimitiveHandle::create(static_cast<std::uint32_t>(index + 1U));
    if (!handle)
      throw std::runtime_error("generated primitive handle was invalid");
    PackedSketchPrimitive primitive{
        id<SketchEntityId>(seed + index + 1U),
        *handle,
        static_cast<std::uint32_t>(points.size()),
        static_cast<std::uint16_t>(index % styles().size()),
        SketchPrimitiveKind::Point,
        SketchPrimitiveFlags::Visible | SketchPrimitiveFlags::Selectable,
        0.0,
        0.0,
        0.0};
    switch (profile) {
    case PickSceneProfile::Coincident:
      points.push_back({0.0, 0.0});
      break;
    case PickSceneProfile::Concentric:
      primitive.kind = SketchPrimitiveKind::Circle;
      primitive.radius = 1.0 + static_cast<double>(index) * 1.0e-6;
      points.push_back({0.0, 0.0});
      break;
    case PickSceneProfile::GlobalLines: {
      primitive.kind = SketchPrimitiveKind::Line;
      const double y = static_cast<double>(index) * 1.0e-6;
      points.push_back({-1.0e6, y});
      points.push_back({1.0e6, y});
      break;
    }
    case PickSceneProfile::Outlier:
      if (index + 1U == count) {
        points.push_back({1.0e9, -1.0e9});
      } else {
        const double x = static_cast<double>(index % columns) * 1.0e-4;
        const double y = static_cast<double>(index / columns) * 1.0e-4;
        points.push_back({x, y});
      }
      break;
    case PickSceneProfile::Sparse:
      break;
    }
    primitives.push_back(primitive);
  }
  auto created =
      SketchSceneSnapshot::create(std::move(sceneStamp), styles(),
                                  std::move(points), std::move(primitives));
  if (!created)
    throw std::runtime_error(created.error().code);
  return std::make_shared<const SketchSceneSnapshot>(std::move(*created));
}

inline Point2d radial(Point2d center, double radius, double angle) {
  return {center.x + radius * std::cos(angle),
          center.y + radius * std::sin(angle)};
}

inline Point2d ellipsePoint(Point2d center, double major, double minor,
                            double rotation, double parameter) {
  const double localX = major * std::cos(parameter);
  const double localY = minor * std::sin(parameter);
  const double cosine = std::cos(rotation);
  const double sine = std::sin(rotation);
  return {center.x + cosine * localX - sine * localY,
          center.y + sine * localX + cosine * localY};
}

inline Point2d hyperbolaPoint(Point2d center, double major, double minor,
                              double rotation, double parameter) {
  const double localX = major * std::cosh(parameter);
  const double localY = minor * std::sinh(parameter);
  const double cosine = std::cos(rotation);
  const double sine = std::sin(rotation);
  return {center.x + cosine * localX - sine * localY,
          center.y + sine * localX + cosine * localY};
}

inline Point2d parabolaPoint(Point2d vertex, double focalLength,
                             double rotation, double parameter) {
  const double localX = parameter * parameter / (4.0 * focalLength);
  const double cosine = std::cos(rotation);
  const double sine = std::sin(rotation);
  return {vertex.x + cosine * localX - sine * parameter,
          vertex.y + sine * localX + cosine * parameter};
}

inline std::vector<SketchPickQuery> queries(const SketchSceneSnapshot &scene,
                                            std::size_t count,
                                            std::uint64_t seed) {
  testkit::Random random{seed};
  std::vector<SketchPickQuery> result;
  result.reserve(count);
  if (scene.primitives().empty())
    return result;
  for (std::size_t index = 0; index < count; ++index) {
    const PackedSketchPrimitive &primitive =
        scene.primitives()[static_cast<std::size_t>(random.next() %
                                                    scene.primitives().size())];
    const Point2d first = scene.points()[primitive.firstPoint];
    Point2d target = first;
    if (primitive.kind == SketchPrimitiveKind::Line) {
      const Point2d end = scene.points()[primitive.firstPoint + 1];
      const double amount = random.between(0.0, 1.0);
      target = {first.x + (end.x - first.x) * amount,
                first.y + (end.y - first.y) * amount};
    } else if (primitive.kind == SketchPrimitiveKind::Circle) {
      target = radial(first, primitive.radius,
                      random.between(-std::numbers::pi, std::numbers::pi));
    } else if (primitive.kind == SketchPrimitiveKind::Arc) {
      target =
          radial(first, primitive.radius,
                 primitive.startAngleRadians +
                     primitive.sweepAngleRadians * random.between(0.0, 1.0));
    } else if (primitive.kind == SketchPrimitiveKind::Ellipse ||
               primitive.kind == SketchPrimitiveKind::EllipticalArc) {
      const double parameter =
          primitive.kind == SketchPrimitiveKind::Ellipse
              ? random.between(0.0, 2.0 * std::numbers::pi)
              : primitive.startAngleRadians +
                    primitive.sweepAngleRadians * random.between(0.0, 1.0);
      target = ellipsePoint(first, primitive.radius, primitive.secondaryRadius,
                            primitive.rotationAngleRadians, parameter);
    } else if (primitive.kind == SketchPrimitiveKind::HyperbolicArc) {
      const double parameter =
          primitive.startAngleRadians +
          primitive.sweepAngleRadians * random.between(0.0, 1.0);
      target =
          hyperbolaPoint(first, primitive.radius, primitive.secondaryRadius,
                         primitive.rotationAngleRadians, parameter);
    } else if (primitive.kind == SketchPrimitiveKind::ParabolicArc) {
      const double parameter =
          primitive.startAngleRadians +
          primitive.sweepAngleRadians * random.between(0.0, 1.0);
      target = parabolaPoint(first, primitive.radius,
                             primitive.rotationAngleRadians, parameter);
    }
    const double tolerance = random.between(0.0005, 0.004);
    target.x += random.between(-0.45, 0.45) * tolerance;
    target.y += random.between(-0.45, 0.45) * tolerance;
    const SketchPickTargets targets =
        index % 3U == 0U   ? SketchPickTargets::Points
        : index % 3U == 1U ? SketchPickTargets::Curves
                           : SketchPickTargets::All;
    result.push_back({target, tolerance, targets});
  }
  return result;
}

inline double positiveAngle(double angle) {
  constexpr double turn = 2.0 * std::numbers::pi;
  angle = std::fmod(angle, turn);
  return angle < 0.0 ? angle + turn : angle;
}

inline bool oracleAngleOnArc(double angle, double start, double sweep) {
  return sweep > 0.0 ? positiveAngle(angle - start) <= sweep
                     : positiveAngle(start - angle) <= -sweep;
}

inline double distance(Point2d first, Point2d second) {
  return std::hypot(first.x - second.x, first.y - second.y);
}

struct OraclePickRefinement {
  Point2d closestPoint;
  double distance = 0.0;
};

inline OraclePickRefinement oracleLineRefinement(Point2d query, Point2d start,
                                                 Point2d end) {
  const long double x = static_cast<long double>(end.x) - start.x;
  const long double y = static_cast<long double>(end.y) - start.y;
  const long double queryX = static_cast<long double>(query.x) - start.x;
  const long double queryY = static_cast<long double>(query.y) - start.y;
  const long double squaredLength = x * x + y * y;
  const long double parameter =
      squaredLength == 0.0L
          ? 0.0L
          : std::clamp((queryX * x + queryY * y) / squaredLength, 0.0L, 1.0L);
  return {
      {static_cast<double>(static_cast<long double>(start.x) + x * parameter),
       static_cast<double>(static_cast<long double>(start.y) + y * parameter)},
      static_cast<double>(
          std::hypot(queryX - x * parameter, queryY - y * parameter))};
}

inline std::optional<Point2d>
oracleSemanticPoint(const SketchSceneSnapshot &scene,
                    const PackedSketchPrimitive &primitive,
                    sketch::PointKey key) {
  if (primitive.kind == SketchPrimitiveKind::BSpline)
    return semanticPoint(scene, primitive, key);
  const Point2d center = scene.points()[primitive.firstPoint];
  if (primitive.kind == SketchPrimitiveKind::Point &&
      key == sketch::PointKey::Point)
    return center;
  if (primitive.kind == SketchPrimitiveKind::Line) {
    if (key == sketch::PointKey::Start)
      return center;
    if (key == sketch::PointKey::End)
      return scene.points()[primitive.firstPoint + 1];
  }
  if ((primitive.kind == SketchPrimitiveKind::Circle ||
       primitive.kind == SketchPrimitiveKind::Arc) &&
      key == sketch::PointKey::Center)
    return center;
  if (primitive.kind == SketchPrimitiveKind::Arc &&
      key == sketch::PointKey::Start)
    return radial(center, primitive.radius, primitive.startAngleRadians);
  if (primitive.kind == SketchPrimitiveKind::Arc &&
      key == sketch::PointKey::End)
    return radial(center, primitive.radius,
                  primitive.startAngleRadians + primitive.sweepAngleRadians);
  if ((primitive.kind == SketchPrimitiveKind::Ellipse ||
       primitive.kind == SketchPrimitiveKind::EllipticalArc) &&
      key == sketch::PointKey::Center)
    return center;
  if ((primitive.kind == SketchPrimitiveKind::Ellipse ||
       primitive.kind == SketchPrimitiveKind::EllipticalArc) &&
      key == sketch::PointKey::Major)
    return ellipsePoint(center, primitive.radius, primitive.secondaryRadius,
                        primitive.rotationAngleRadians, 0.0);
  if ((primitive.kind == SketchPrimitiveKind::Ellipse ||
       primitive.kind == SketchPrimitiveKind::EllipticalArc) &&
      key == sketch::PointKey::Minor)
    return ellipsePoint(center, primitive.radius, primitive.secondaryRadius,
                        primitive.rotationAngleRadians, std::numbers::pi / 2.0);
  if (primitive.kind == SketchPrimitiveKind::EllipticalArc &&
      key == sketch::PointKey::Start)
    return ellipsePoint(center, primitive.radius, primitive.secondaryRadius,
                        primitive.rotationAngleRadians,
                        primitive.startAngleRadians);
  if (primitive.kind == SketchPrimitiveKind::EllipticalArc &&
      key == sketch::PointKey::End)
    return ellipsePoint(center, primitive.radius, primitive.secondaryRadius,
                        primitive.rotationAngleRadians,
                        primitive.startAngleRadians +
                            primitive.sweepAngleRadians);
  if (primitive.kind == SketchPrimitiveKind::HyperbolicArc) {
    if (key == sketch::PointKey::Center)
      return center;
    if (key == sketch::PointKey::Major)
      return hyperbolaPoint(center, primitive.radius, primitive.secondaryRadius,
                            primitive.rotationAngleRadians, 0.0);
    if (key == sketch::PointKey::Minor) {
      const double cosine = std::cos(primitive.rotationAngleRadians);
      const double sine = std::sin(primitive.rotationAngleRadians);
      return Point2d{center.x + cosine * primitive.radius -
                         sine * primitive.secondaryRadius,
                     center.y + sine * primitive.radius +
                         cosine * primitive.secondaryRadius};
    }
    if (key == sketch::PointKey::Focus) {
      const double focus =
          std::hypot(primitive.radius, primitive.secondaryRadius);
      return Point2d{
          center.x + std::cos(primitive.rotationAngleRadians) * focus,
          center.y + std::sin(primitive.rotationAngleRadians) * focus};
    }
    if (key == sketch::PointKey::Start)
      return hyperbolaPoint(center, primitive.radius, primitive.secondaryRadius,
                            primitive.rotationAngleRadians,
                            primitive.startAngleRadians);
    if (key == sketch::PointKey::End)
      return hyperbolaPoint(center, primitive.radius, primitive.secondaryRadius,
                            primitive.rotationAngleRadians,
                            primitive.startAngleRadians +
                                primitive.sweepAngleRadians);
  }
  if (primitive.kind == SketchPrimitiveKind::ParabolicArc) {
    if (key == sketch::PointKey::Center)
      return center;
    if (key == sketch::PointKey::Focus)
      return Point2d{center.x + std::cos(primitive.rotationAngleRadians) *
                                    primitive.radius,
                     center.y + std::sin(primitive.rotationAngleRadians) *
                                    primitive.radius};
    if (key == sketch::PointKey::Start)
      return parabolaPoint(center, primitive.radius,
                           primitive.rotationAngleRadians,
                           primitive.startAngleRadians);
    if (key == sketch::PointKey::End)
      return parabolaPoint(
          center, primitive.radius, primitive.rotationAngleRadians,
          primitive.startAngleRadians + primitive.sweepAngleRadians);
  }
  return std::nullopt;
}

inline OraclePickRefinement
oracleCurveRefinement(const SketchSceneSnapshot &scene,
                      const PackedSketchPrimitive &primitive, Point2d query) {
  if (primitive.kind == SketchPrimitiveKind::BSpline) {
    const PackedSketchSpline &spline = scene.splines()[primitive.spline];
    const std::size_t count = spline.controlPointCount;
    const sketch::NurbsProjection projection = sketch::projectToNurbs(
        {scene.splineControlPointCoordinates().subspan(
             static_cast<std::size_t>(spline.firstControlPoint) * 2U,
             count * 2U),
         scene.splineKnots().subspan(spline.firstKnot,
                                     count + spline.degree + 1U),
         scene.splineWeights().subspan(spline.firstWeight, count),
         spline.degree},
        {query.x, query.y});
    return {{projection.point.x, projection.point.y},
            distance(query, {projection.point.x, projection.point.y})};
  }
  const Point2d center = scene.points()[primitive.firstPoint];
  if (primitive.kind == SketchPrimitiveKind::Line)
    return oracleLineRefinement(query, center,
                                scene.points()[primitive.firstPoint + 1]);
  const double radialDistance = distance(query, center);
  const double angle = radialDistance == 0.0
                           ? 0.0
                           : std::atan2(query.y - center.y, query.x - center.x);
  if (primitive.kind == SketchPrimitiveKind::Circle)
    return {radial(center, primitive.radius, angle),
            std::abs(radialDistance - primitive.radius)};
  if (primitive.kind == SketchPrimitiveKind::Arc) {
    if (oracleAngleOnArc(angle, primitive.startAngleRadians,
                         primitive.sweepAngleRadians))
      return {radial(center, primitive.radius, angle),
              std::abs(radialDistance - primitive.radius)};
    const Point2d start =
        radial(center, primitive.radius, primitive.startAngleRadians);
    const Point2d end =
        radial(center, primitive.radius,
               primitive.startAngleRadians + primitive.sweepAngleRadians);
    const double startDistance = distance(query, start);
    const double endDistance = distance(query, end);
    return startDistance <= endDistance
               ? OraclePickRefinement{start, startDistance}
               : OraclePickRefinement{end, endDistance};
  }
  return {{}, std::numeric_limits<double>::infinity()};
}

inline std::optional<SketchPickResult>
bruteForcePick(const SketchSceneSnapshot &scene, const SketchPickQuery &query) {
  struct Candidate {
    SketchPickResult result;
    std::uint16_t layer;
    std::uint32_t ordinal;
    std::uint8_t tieRank;
  };
  std::vector<Candidate> candidates;
  const auto consider = [&](Candidate candidate) {
    if (candidate.result.distance <= query.tolerance)
      candidates.push_back(std::move(candidate));
  };

  for (std::uint32_t ordinal = 0; ordinal < scene.primitives().size();
       ++ordinal) {
    const PackedSketchPrimitive &primitive = scene.primitives()[ordinal];
    const auto flags = static_cast<std::uint8_t>(primitive.flags);
    if ((flags & static_cast<std::uint8_t>(SketchPrimitiveFlags::Visible)) ==
            0 ||
        (flags & static_cast<std::uint8_t>(SketchPrimitiveFlags::Selectable)) ==
            0)
      continue;
    const std::uint16_t layer = scene.styles()[primitive.style].layer;
    if (hasTarget(query.targets, SketchPickTargets::Points)) {
      std::array<sketch::PointKey, 6> keys{};
      std::size_t keyCount = 0;
      switch (primitive.kind) {
      case SketchPrimitiveKind::Point:
        keys[0] = sketch::PointKey::Point;
        keyCount = 1;
        break;
      case SketchPrimitiveKind::Line:
        keys[0] = sketch::PointKey::Start;
        keys[1] = sketch::PointKey::End;
        keyCount = 2;
        break;
      case SketchPrimitiveKind::Circle:
        keys[0] = sketch::PointKey::Center;
        keyCount = 1;
        break;
      case SketchPrimitiveKind::Arc:
        keys[0] = sketch::PointKey::Center;
        keys[1] = sketch::PointKey::Start;
        keys[2] = sketch::PointKey::End;
        keyCount = 3;
        break;
      case SketchPrimitiveKind::Ellipse:
        keys[0] = sketch::PointKey::Center;
        keys[1] = sketch::PointKey::Major;
        keys[2] = sketch::PointKey::Minor;
        keyCount = 3;
        break;
      case SketchPrimitiveKind::EllipticalArc:
        keys[0] = sketch::PointKey::Center;
        keys[1] = sketch::PointKey::Major;
        keys[2] = sketch::PointKey::Minor;
        keys[3] = sketch::PointKey::Start;
        keys[4] = sketch::PointKey::End;
        keyCount = 5;
        break;
      case SketchPrimitiveKind::HyperbolicArc:
        keys[0] = sketch::PointKey::Center;
        keys[1] = sketch::PointKey::Major;
        keys[2] = sketch::PointKey::Minor;
        keys[3] = sketch::PointKey::Focus;
        keys[4] = sketch::PointKey::Start;
        keys[5] = sketch::PointKey::End;
        keyCount = 6;
        break;
      case SketchPrimitiveKind::ParabolicArc:
        keys[0] = sketch::PointKey::Center;
        keys[1] = sketch::PointKey::Focus;
        keys[2] = sketch::PointKey::Start;
        keys[3] = sketch::PointKey::End;
        keyCount = 4;
        break;
      case SketchPrimitiveKind::BSpline:
        keys[0] = sketch::PointKey::Start;
        keys[1] = sketch::PointKey::End;
        keyCount = 2;
        break;
      }
      for (std::size_t keyIndex = 0; keyIndex < keyCount; ++keyIndex) {
        const Point2d point =
            *oracleSemanticPoint(scene, primitive, keys[keyIndex]);
        consider({{scene.stamp(), primitive.entity, primitive.handle,
                   keys[keyIndex], point, distance(query.point, point)},
                  layer,
                  ordinal,
                  static_cast<std::uint8_t>(keyIndex)});
      }
    }
    if (hasTarget(query.targets, SketchPickTargets::Curves) &&
        primitive.kind != SketchPrimitiveKind::Point) {
      const OraclePickRefinement refinement =
          oracleCurveRefinement(scene, primitive, query.point);
      consider({{scene.stamp(), primitive.entity, primitive.handle,
                 std::nullopt, refinement.closestPoint, refinement.distance},
                layer,
                ordinal,
                0});
    }
  }
  if (candidates.empty())
    return std::nullopt;

  const double minimumDistance =
      std::ranges::min(candidates, {}, [](const Candidate &candidate) {
        return candidate.result.distance;
      }).result.distance;
  const auto equivalentDistance = [](double first, double second) {
    const auto firstBits = std::bit_cast<std::uint64_t>(first);
    const auto secondBits = std::bit_cast<std::uint64_t>(second);
    const std::uint64_t difference = firstBits > secondBits
                                         ? firstBits - secondBits
                                         : secondBits - firstBits;
    return difference <= 1024U;
  };
  const auto structurallyBetter = [](const Candidate &candidate,
                                     const Candidate &current) {
    const bool candidatePoint = candidate.result.pointKey.has_value();
    const bool currentPoint = current.result.pointKey.has_value();
    if (candidatePoint != currentPoint)
      return candidatePoint;
    if (candidate.layer != current.layer)
      return candidate.layer > current.layer;
    if (candidate.ordinal != current.ordinal)
      return candidate.ordinal > current.ordinal;
    return candidate.tieRank < current.tieRank;
  };
  const Candidate *best = nullptr;
  for (const Candidate &candidate : candidates) {
    if (!equivalentDistance(candidate.result.distance, minimumDistance))
      continue;
    if (!best || structurallyBetter(candidate, *best))
      best = &candidate;
  }
  return best->result;
}

inline void requireEquivalent(const std::optional<SketchPickResult> &actual,
                              const std::optional<SketchPickResult> &expected) {
  if (actual.has_value() != expected.has_value())
    throw std::runtime_error(
        "indexed pick disagrees with brute-force presence");
  if (!actual)
    return;
  const double distanceScale =
      std::max(std::abs(actual->distance), std::abs(expected->distance));
  const double pointScale = std::max(
      {1.0, std::abs(actual->closestPoint.x), std::abs(actual->closestPoint.y),
       std::abs(expected->closestPoint.x), std::abs(expected->closestPoint.y)});
  const double distanceTolerance =
      std::max(2048.0 * std::numeric_limits<double>::epsilon() * distanceScale,
               static_cast<double>(4096.0L *
                                   std::numeric_limits<long double>::epsilon() *
                                   static_cast<long double>(pointScale)));
  const double pointTolerance =
      4096.0 * std::numeric_limits<double>::epsilon() * pointScale;
  if (actual->scene != expected->scene || actual->entity != expected->entity ||
      actual->primitive != expected->primitive ||
      actual->pointKey != expected->pointKey)
    throw std::runtime_error(
        "indexed pick disagrees with brute-force identity");
  if (std::abs(actual->closestPoint.x - expected->closestPoint.x) >
          pointTolerance ||
      std::abs(actual->closestPoint.y - expected->closestPoint.y) >
          pointTolerance)
    throw std::runtime_error(
        "indexed pick disagrees with brute-force closest point");
  if (actual->distance != expected->distance &&
      std::abs(actual->distance - expected->distance) > distanceTolerance)
    throw std::runtime_error(
        "indexed pick disagrees with brute-force distance: indexed=" +
        std::to_string(actual->distance) +
        ", brute=" + std::to_string(expected->distance) +
        ", tolerance=" + std::to_string(distanceTolerance));
}

} // namespace kearne::render::test
