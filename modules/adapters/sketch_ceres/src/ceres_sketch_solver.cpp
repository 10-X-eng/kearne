#include <kearne/adapters/ceres_sketch_solver.hpp>
#include <kearne/sketch/nurbs.hpp>

#include <Eigen/Dense>
#include <Eigen/Sparse>
#include <Eigen/SparseCholesky>
#include <Eigen/SparseQR>
#include <ceres/ceres.h>
#include <ceres/dynamic_numeric_diff_cost_function.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <numbers>
#include <numeric>
#include <ranges>
#include <span>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace kearne::adapters {
namespace {

namespace model = sketch;

enum class Kind {
  Point,
  Line,
  Circle,
  Arc,
  Ellipse,
  EllipticalArc,
  HyperbolicArc,
  ParabolicArc,
  BSpline,
};

struct WorkingEntity {
  model::Entity source;
  SketchEntityId id;
  Kind kind;
  std::vector<double> values;
  std::vector<double> knots;
  std::size_t controlPointCount = 0U;
  std::uint32_t degree = 0U;
  bool periodic = false;
  std::size_t periodicTailCount = 0U;

  WorkingEntity(model::Entity requestedSource, SketchEntityId requestedId,
                Kind requestedKind, std::vector<double> requestedValues,
                std::vector<double> requestedKnots = {},
                std::size_t requestedControlPointCount = 0U,
                std::uint32_t requestedDegree = 0U,
                bool requestedPeriodic = false,
                std::size_t requestedPeriodicTailCount = 0U)
      : source(std::move(requestedSource)), id(requestedId),
        kind(requestedKind), values(std::move(requestedValues)),
        knots(std::move(requestedKnots)),
        controlPointCount(requestedControlPointCount), degree(requestedDegree),
        periodic(requestedPeriodic),
        periodicTailCount(requestedPeriodicTailCount) {}
};

struct EntityShape {
  Kind kind;
  std::vector<double> knots;
  std::size_t controlPointCount = 0U;
  std::uint32_t degree = 0U;
  bool periodic = false;
  std::size_t periodicTailCount = 0U;
};

EntityShape shapeOf(const WorkingEntity &entity) {
  return {entity.kind,   entity.knots,    entity.controlPointCount,
          entity.degree, entity.periodic, entity.periodicTailCount};
}

model::NurbsView nurbsView(const EntityShape &shape,
                           std::span<const double> values) {
  return {values.first(shape.controlPointCount * 2U), shape.knots,
          values.subspan(shape.controlPointCount * 2U), shape.degree};
}

using WorkingIndex = std::unordered_map<SketchEntityId, std::size_t,
                                        TypedIdHash<SketchEntityIdTag>>;

WorkingEntity workingEntity(const model::Entity &entity) {
  return std::visit(
      [&entity]<typename Value>(const Value &value) -> WorkingEntity {
        using Type = std::decay_t<Value>;
        if constexpr (std::is_same_v<Type, model::PointEntity>) {
          return {entity,
                  value.id,
                  Kind::Point,
                  {value.point.x.si(), value.point.y.si()}};
        } else if constexpr (std::is_same_v<Type, model::LineEntity>) {
          return {entity,
                  value.id,
                  Kind::Line,
                  {value.start.x.si(), value.start.y.si(), value.end.x.si(),
                   value.end.y.si()}};
        } else if constexpr (std::is_same_v<Type, model::CircleEntity>) {
          return {
              entity,
              value.id,
              Kind::Circle,
              {value.center.x.si(), value.center.y.si(), value.radius.si()}};
        } else if constexpr (std::is_same_v<Type, model::ArcEntity>) {
          return {entity,
                  value.id,
                  Kind::Arc,
                  {value.center.x.si(), value.center.y.si(), value.radius.si(),
                   value.startAngle.si(), value.endAngle.si()}};
        } else if constexpr (std::is_same_v<Type, model::EllipseEntity>) {
          return {entity,
                  value.id,
                  Kind::Ellipse,
                  {value.center.x.si(), value.center.y.si(),
                   value.majorRadius.si(), value.minorRadius.si(),
                   value.rotation.si()}};
        } else if constexpr (std::is_same_v<Type, model::EllipticalArcEntity>) {
          return {entity,
                  value.id,
                  Kind::EllipticalArc,
                  {value.center.x.si(), value.center.y.si(),
                   value.majorRadius.si(), value.minorRadius.si(),
                   value.rotation.si(), value.startParameter.si(),
                   value.endParameter.si()}};
        } else if constexpr (std::is_same_v<Type, model::HyperbolicArcEntity>) {
          return {entity,
                  value.id,
                  Kind::HyperbolicArc,
                  {value.center.x.si(), value.center.y.si(),
                   value.majorRadius.si(), value.minorRadius.si(),
                   value.rotation.si(), value.startParameter.si(),
                   value.endParameter.si()}};
        } else if constexpr (std::is_same_v<Type, model::ParabolicArcEntity>) {
          return {entity,
                  value.id,
                  Kind::ParabolicArc,
                  {value.vertex.x.si(), value.vertex.y.si(),
                   value.focalLength.si(), value.rotation.si(),
                   value.startParameter.si(), value.endParameter.si()}};
        } else {
          std::vector<double> values;
          values.reserve(value.controlPoints.size() * 3U);
          for (const model::Point2 &point : value.controlPoints) {
            values.push_back(point.x.si());
            values.push_back(point.y.si());
          }
          for (const model::DimensionlessValue weight : value.weights)
            values.push_back(weight.si());
          std::vector<double> knots;
          knots.reserve(value.knots.size());
          for (const model::DimensionlessValue knot : value.knots)
            knots.push_back(knot.si());
          const std::size_t periodicTailCount =
              value.periodic
                  ? model::periodicNurbsTailCount(
                        {{values.data(), value.controlPoints.size() * 2U},
                         knots,
                         {values.data() + value.controlPoints.size() * 2U,
                          value.weights.size()},
                         value.degree},
                        0.0, 0.0)
                  : 0U;
          return {entity,           value.id,
                  Kind::BSpline,    std::move(values),
                  std::move(knots), value.controlPoints.size(),
                  value.degree,     value.periodic,
                  periodicTailCount};
        }
      },
      entity);
}

WorkingIndex indexOf(const std::vector<WorkingEntity> &entities) {
  WorkingIndex result;
  result.reserve(entities.size());
  for (std::size_t index = 0; index < entities.size(); ++index)
    result.emplace(entities[index].id, index);
  return result;
}

std::size_t coordinateCount(const WorkingEntity &entity) {
  if (entity.kind == Kind::Line)
    return 4U;
  if (entity.kind == Kind::BSpline)
    return entity.controlPointCount * 2U;
  return 2U;
}

double parameterScale(const EntityShape &shape, std::size_t index,
                      double lengthScale) {
  if (shape.kind == Kind::BSpline)
    return index < shape.controlPointCount * 2U ? lengthScale : 1.0;
  const bool angular = (shape.kind == Kind::Arc && index >= 3U) ||
                       (shape.kind == Kind::Ellipse && index == 4U) ||
                       (shape.kind == Kind::EllipticalArc && index >= 4U) ||
                       (shape.kind == Kind::HyperbolicArc && index >= 4U) ||
                       (shape.kind == Kind::ParabolicArc && index == 3U);
  return angular ? 1.0 : lengthScale;
}

Result<void> applyPrior(std::vector<WorkingEntity> &entities,
                        const std::vector<model::Entity> &prior,
                        const model::NumericalProfile &profile) {
  const WorkingIndex index = indexOf(entities);
  std::unordered_set<SketchEntityId, TypedIdHash<SketchEntityIdTag>> seen;
  seen.reserve(prior.size());
  for (const model::Entity &candidate : prior) {
    WorkingEntity seed = workingEntity(candidate);
    if (!seen.insert(seed.id).second)
      return std::unexpected(diagnostic("sketch.seed.duplicate-id",
                                        "prior solution has a duplicate ID"));
    const auto found = index.find(seed.id);
    if (found == index.end())
      continue;
    WorkingEntity &target = entities[found->second];
    if (seed.kind != target.kind ||
        seed.values.size() != target.values.size() ||
        seed.knots != target.knots ||
        seed.controlPointCount != target.controlPointCount ||
        seed.degree != target.degree || seed.periodic != target.periodic ||
        seed.periodicTailCount != target.periodicTailCount)
      return std::unexpected(diagnostic("sketch.seed.entity-kind-mismatch",
                                        "prior solution entity kind changed"));
    if (!std::ranges::all_of(seed.values,
                             [](double value) { return std::isfinite(value); }))
      return std::unexpected(
          diagnostic("sketch.seed.non-finite", "prior solution is not finite"));
    const std::size_t coordinates = coordinateCount(seed);
    for (std::size_t offset = 0; offset < coordinates; ++offset) {
      if (std::abs(seed.values[offset]) > profile.maximumCoordinateMeters)
        return std::unexpected(diagnostic("sketch.seed.coordinate-range",
                                          "prior solution is out of range"));
    }
    if ((seed.kind == Kind::Circle || seed.kind == Kind::Arc) &&
        (seed.values[2] < profile.minimumLengthMeters ||
         seed.values[2] > profile.maximumCoordinateMeters))
      return std::unexpected(diagnostic("sketch.seed.invalid-radius",
                                        "prior solution radius is invalid"));
    if ((seed.kind == Kind::Ellipse || seed.kind == Kind::EllipticalArc) &&
        (seed.values[2] < profile.minimumLengthMeters ||
         seed.values[2] > profile.maximumCoordinateMeters ||
         seed.values[3] < profile.minimumLengthMeters ||
         seed.values[3] > seed.values[2]))
      return std::unexpected(
          diagnostic("sketch.seed.invalid-ellipse-axis",
                     "prior solution ellipse axes are invalid"));
    if (seed.kind == Kind::HyperbolicArc &&
        (seed.values[2] < profile.minimumLengthMeters ||
         seed.values[2] > profile.maximumCoordinateMeters ||
         seed.values[3] < profile.minimumLengthMeters ||
         seed.values[3] > profile.maximumCoordinateMeters))
      return std::unexpected(
          diagnostic("sketch.seed.invalid-hyperbola-axis",
                     "prior solution hyperbola axes are invalid"));
    if (seed.kind == Kind::ParabolicArc &&
        (seed.values[2] < profile.minimumLengthMeters ||
         seed.values[2] > profile.maximumCoordinateMeters))
      return std::unexpected(
          diagnostic("sketch.seed.invalid-parabola-focus",
                     "prior solution parabola focal length is invalid"));
    if (seed.kind == Kind::BSpline)
      for (std::size_t offset = coordinates; offset < seed.values.size();
           ++offset)
        if (seed.values[offset] < 1.0e-12 || seed.values[offset] > 1.0e12)
          return std::unexpected(
              diagnostic("sketch.seed.invalid-bspline-weight",
                         "prior solution B-spline weight is invalid"));
    if (seed.kind == Kind::Line && std::hypot(seed.values[2] - seed.values[0],
                                              seed.values[3] - seed.values[1]) <
                                       profile.minimumLengthMeters)
      return std::unexpected(diagnostic("sketch.seed.degenerate-line",
                                        "prior solution line is degenerate"));
    if (seed.kind == Kind::Arc && std::abs(seed.values[4] - seed.values[3]) <
                                      profile.angleToleranceRadians)
      return std::unexpected(diagnostic("sketch.seed.degenerate-arc",
                                        "prior solution arc is degenerate"));
    target.values = std::move(seed.values);
  }
  return {};
}

template <typename Dimension>
Result<Quantity<Dimension>> quantity(double value) {
  return Quantity<Dimension>::fromSi(value);
}

Result<model::Point2> point(double x, double y) {
  auto xValue = quantity<Length>(x);
  auto yValue = quantity<Length>(y);
  if (!xValue || !yValue)
    return std::unexpected(
        diagnostic("sketch.solution.non-finite", "solved point is not finite"));
  return model::Point2{*xValue, *yValue};
}

Result<model::Entity> rebuild(const WorkingEntity &entity) {
  return std::visit(
      [&entity]<typename Value>(const Value &source) -> Result<model::Entity> {
        using Type = std::decay_t<Value>;
        if constexpr (std::is_same_v<Type, model::PointEntity>) {
          auto solved = point(entity.values[0], entity.values[1]);
          if (!solved)
            return std::unexpected(std::move(solved.error()));
          return model::Entity{
              model::PointEntity{source.id, *solved, source.construction}};
        } else if constexpr (std::is_same_v<Type, model::LineEntity>) {
          auto start = point(entity.values[0], entity.values[1]);
          auto end = point(entity.values[2], entity.values[3]);
          if (!start || !end)
            return std::unexpected(start ? std::move(end.error())
                                         : std::move(start.error()));
          return model::Entity{
              model::LineEntity{source.id, *start, *end, source.construction}};
        } else if constexpr (std::is_same_v<Type, model::CircleEntity>) {
          auto center = point(entity.values[0], entity.values[1]);
          auto radius = quantity<Length>(entity.values[2]);
          if (!center || !radius)
            return std::unexpected(center ? std::move(radius.error())
                                          : std::move(center.error()));
          return model::Entity{model::CircleEntity{source.id, *center, *radius,
                                                   source.construction}};
        } else if constexpr (std::is_same_v<Type, model::ArcEntity>) {
          auto center = point(entity.values[0], entity.values[1]);
          auto radius = quantity<Length>(entity.values[2]);
          auto start = quantity<Angle>(entity.values[3]);
          auto end = quantity<Angle>(entity.values[4]);
          if (!center || !radius || !start || !end)
            return std::unexpected(diagnostic("sketch.solution.non-finite",
                                              "solved arc is not finite"));
          return model::Entity{model::ArcEntity{
              source.id, *center, *radius, *start, *end, source.construction}};
        } else if constexpr (std::is_same_v<Type, model::EllipseEntity>) {
          auto center = point(entity.values[0], entity.values[1]);
          auto major = quantity<Length>(entity.values[2]);
          auto minor = quantity<Length>(entity.values[3]);
          auto rotation = quantity<Angle>(entity.values[4]);
          if (!center || !major || !minor || !rotation)
            return std::unexpected(diagnostic("sketch.solution.non-finite",
                                              "solved ellipse is not finite"));
          return model::Entity{model::EllipseEntity{source.id, *center, *major,
                                                    *minor, *rotation,
                                                    source.construction}};
        } else if constexpr (std::is_same_v<Type, model::EllipticalArcEntity>) {
          auto center = point(entity.values[0], entity.values[1]);
          auto major = quantity<Length>(entity.values[2]);
          auto minor = quantity<Length>(entity.values[3]);
          auto rotation = quantity<Angle>(entity.values[4]);
          auto start = quantity<Angle>(entity.values[5]);
          auto end = quantity<Angle>(entity.values[6]);
          if (!center || !major || !minor || !rotation || !start || !end)
            return std::unexpected(
                diagnostic("sketch.solution.non-finite",
                           "solved elliptical arc is not finite"));
          return model::Entity{model::EllipticalArcEntity{
              source.id, *center, *major, *minor, *rotation, *start, *end,
              source.construction}};
        } else if constexpr (std::is_same_v<Type, model::HyperbolicArcEntity>) {
          auto center = point(entity.values[0], entity.values[1]);
          auto major = quantity<Length>(entity.values[2]);
          auto minor = quantity<Length>(entity.values[3]);
          auto rotation = quantity<Angle>(entity.values[4]);
          auto start = quantity<Dimensionless>(entity.values[5]);
          auto end = quantity<Dimensionless>(entity.values[6]);
          if (!center || !major || !minor || !rotation || !start || !end)
            return std::unexpected(
                diagnostic("sketch.solution.non-finite",
                           "solved hyperbolic arc is not finite"));
          return model::Entity{model::HyperbolicArcEntity{
              source.id, *center, *major, *minor, *rotation, *start, *end,
              source.construction}};
        } else if constexpr (std::is_same_v<Type, model::ParabolicArcEntity>) {
          auto vertex = point(entity.values[0], entity.values[1]);
          auto focal = quantity<Length>(entity.values[2]);
          auto rotation = quantity<Angle>(entity.values[3]);
          auto start = quantity<Length>(entity.values[4]);
          auto end = quantity<Length>(entity.values[5]);
          if (!vertex || !focal || !rotation || !start || !end)
            return std::unexpected(
                diagnostic("sketch.solution.non-finite",
                           "solved parabolic arc is not finite"));
          return model::Entity{
              model::ParabolicArcEntity{source.id, *vertex, *focal, *rotation,
                                        *start, *end, source.construction}};
        } else {
          std::vector<model::Point2> controlPoints;
          controlPoints.reserve(source.controlPoints.size());
          for (std::size_t index = 0U; index < source.controlPoints.size();
               ++index) {
            auto solved = point(entity.values[index * 2U],
                                entity.values[index * 2U + 1U]);
            if (!solved)
              return std::unexpected(std::move(solved.error()));
            controlPoints.push_back(*solved);
          }
          std::vector<model::DimensionlessValue> weights;
          weights.reserve(source.weights.size());
          const std::size_t weightOffset = source.controlPoints.size() * 2U;
          for (std::size_t index = 0U; index < source.weights.size(); ++index) {
            auto solved =
                quantity<Dimensionless>(entity.values[weightOffset + index]);
            if (!solved)
              return std::unexpected(std::move(solved.error()));
            weights.push_back(*solved);
          }
          return model::Entity{model::BSplineEntity{
              source.id, std::move(controlPoints), source.knots,
              std::move(weights), source.degree, source.periodic,
              source.construction}};
        }
      },
      entity.source);
}

struct PlainPoint {
  double x;
  double y;
};

PlainPoint bsplinePoint(const EntityShape &shape, const double *values,
                        double parameter) {
  const model::NurbsPoint point = evaluateNurbs(
      nurbsView(shape, {values, shape.controlPointCount * 3U}), parameter);
  return {point.x, point.y};
}

PlainPoint rotatedPoint(const double *values, double localX, double localY,
                        std::size_t rotationIndex) {
  const double cosine = std::cos(values[rotationIndex]);
  const double sine = std::sin(values[rotationIndex]);
  return {values[0] + cosine * localX - sine * localY,
          values[1] + sine * localX + cosine * localY};
}

PlainPoint ellipsePoint(const double *values, double parameter) {
  return rotatedPoint(values, values[2] * std::cos(parameter),
                      values[3] * std::sin(parameter), 4U);
}

PlainPoint hyperbolaPoint(const double *values, double parameter) {
  return rotatedPoint(values, values[2] * std::cosh(parameter),
                      values[3] * std::sinh(parameter), 4U);
}

PlainPoint parabolaPoint(const double *values, double parameter) {
  return rotatedPoint(values, parameter * parameter / (4.0 * values[2]),
                      parameter, 3U);
}

std::vector<PlainPoint> rigidPoints(const EntityShape &shape,
                                    const double *values) {
  switch (shape.kind) {
  case Kind::Point:
    return {{values[0], values[1]}};
  case Kind::Line:
    return {{values[0], values[1]}, {values[2], values[3]}};
  case Kind::Circle:
    return {{values[0], values[1]}};
  case Kind::Arc:
    return {{values[0], values[1]},
            {values[0] + values[2] * std::cos(values[3]),
             values[1] + values[2] * std::sin(values[3])},
            {values[0] + values[2] * std::cos(values[4]),
             values[1] + values[2] * std::sin(values[4])}};
  case Kind::Ellipse:
    return {{values[0], values[1]},
            ellipsePoint(values, 0.0),
            ellipsePoint(values, std::numbers::pi / 2.0)};
  case Kind::EllipticalArc:
    return {{values[0], values[1]},
            ellipsePoint(values, 0.0),
            ellipsePoint(values, std::numbers::pi / 2.0),
            ellipsePoint(values, values[5]),
            ellipsePoint(values, values[6])};
  case Kind::HyperbolicArc:
    return {{values[0], values[1]},
            hyperbolaPoint(values, 0.0),
            rotatedPoint(values, values[2], values[3], 4U),
            hyperbolaPoint(values, values[5]),
            hyperbolaPoint(values, values[6])};
  case Kind::ParabolicArc:
    return {{values[0], values[1]},
            rotatedPoint(values, values[2], 0.0, 3U),
            parabolaPoint(values, values[4]),
            parabolaPoint(values, values[5])};
  case Kind::BSpline: {
    std::vector<PlainPoint> points;
    points.reserve(shape.controlPointCount);
    for (std::size_t index = 0U; index < shape.controlPointCount; ++index)
      points.push_back({values[index * 2U], values[index * 2U + 1U]});
    return points;
  }
  }
  std::unreachable();
}

std::optional<std::pair<std::size_t, std::size_t>>
rigidAxis(std::span<const PlainPoint> points, double minimumLength) {
  for (std::size_t first = 0U; first < points.size(); ++first)
    for (std::size_t second = first + 1U; second < points.size(); ++second)
      if (std::hypot(points[second].x - points[first].x,
                     points[second].y - points[first].y) >= minimumLength)
        return std::pair{first, second};
  return std::nullopt;
}

bool isLine(Kind kind) { return kind == Kind::Line; }
bool isRadial(Kind kind) { return kind == Kind::Circle || kind == Kind::Arc; }
bool isEllipse(Kind kind) {
  return kind == Kind::Ellipse || kind == Kind::EllipticalArc;
}
bool isHyperbola(Kind kind) { return kind == Kind::HyperbolicArc; }
bool isParabola(Kind kind) { return kind == Kind::ParabolicArc; }
bool isBSpline(Kind kind) { return kind == Kind::BSpline; }

bool validPointKey(Kind kind, model::PointKey key) {
  if (kind == Kind::Point)
    return key == model::PointKey::Point;
  if (kind == Kind::Line)
    return key == model::PointKey::Start || key == model::PointKey::End;
  if (kind == Kind::Arc)
    return key == model::PointKey::Center || key == model::PointKey::Start ||
           key == model::PointKey::End;
  if (kind == Kind::Ellipse)
    return key == model::PointKey::Center || key == model::PointKey::Major ||
           key == model::PointKey::Minor;
  if (kind == Kind::EllipticalArc)
    return key == model::PointKey::Center || key == model::PointKey::Major ||
           key == model::PointKey::Minor || key == model::PointKey::Start ||
           key == model::PointKey::End;
  if (kind == Kind::HyperbolicArc)
    return key == model::PointKey::Center || key == model::PointKey::Major ||
           key == model::PointKey::Minor || key == model::PointKey::Focus ||
           key == model::PointKey::Start || key == model::PointKey::End;
  if (kind == Kind::ParabolicArc)
    return key == model::PointKey::Center || key == model::PointKey::Focus ||
           key == model::PointKey::Start || key == model::PointKey::End;
  if (kind == Kind::BSpline)
    return key == model::PointKey::Start || key == model::PointKey::End;
  return key == model::PointKey::Center;
}

class ConstraintCost final {
public:
  ConstraintCost(model::Constraint constraint,
                 std::vector<SketchEntityId> entities,
                 std::vector<EntityShape> shapes,
                 std::vector<std::vector<double>> anchors,
                 const model::NumericalProfile &profile)
      : constraint_(std::move(constraint)), entities_(std::move(entities)),
        shapes_(std::move(shapes)), anchors_(std::move(anchors)),
        scale_(profile.typicalLengthMeters),
        minimum_(profile.minimumLengthMeters) {}

  bool operator()(double const *const *parameters, double *residuals) const {
    const auto block = [&](SketchEntityId id) {
      const auto found = std::ranges::find(entities_, id);
      const std::size_t index =
          static_cast<std::size_t>(found - entities_.begin());
      return std::pair{shapes_[index].kind, parameters[index]};
    };
    const auto shape = [&](SketchEntityId id) -> const EntityShape & {
      const auto found = std::ranges::find(entities_, id);
      return shapes_[static_cast<std::size_t>(found - entities_.begin())];
    };
    const auto selectedPoint = [&](const model::PointRef &reference) {
      const auto [kind, values] = block(reference.entity);
      if (kind == Kind::Point && reference.key == model::PointKey::Point)
        return PlainPoint{values[0], values[1]};
      if (kind == Kind::Line && reference.key == model::PointKey::Start)
        return PlainPoint{values[0], values[1]};
      if (kind == Kind::Line && reference.key == model::PointKey::End)
        return PlainPoint{values[2], values[3]};
      if (kind == Kind::Arc && reference.key != model::PointKey::Center) {
        const double angle =
            values[reference.key == model::PointKey::Start ? 3U : 4U];
        return PlainPoint{values[0] + values[2] * std::cos(angle),
                          values[1] + values[2] * std::sin(angle)};
      }
      if (isEllipse(kind) && reference.key != model::PointKey::Center) {
        if (reference.key == model::PointKey::Major)
          return ellipsePoint(values, 0.0);
        if (reference.key == model::PointKey::Minor)
          return ellipsePoint(values, std::numbers::pi / 2.0);
        return ellipsePoint(
            values, values[reference.key == model::PointKey::Start ? 5U : 6U]);
      }
      if (isHyperbola(kind) && reference.key != model::PointKey::Center) {
        if (reference.key == model::PointKey::Major)
          return hyperbolaPoint(values, 0.0);
        if (reference.key == model::PointKey::Minor)
          return rotatedPoint(values, values[2], values[3], 4U);
        if (reference.key == model::PointKey::Focus)
          return rotatedPoint(values, std::hypot(values[2], values[3]), 0.0,
                              4U);
        return hyperbolaPoint(
            values, values[reference.key == model::PointKey::Start ? 5U : 6U]);
      }
      if (isParabola(kind) && reference.key != model::PointKey::Center) {
        if (reference.key == model::PointKey::Focus)
          return rotatedPoint(values, values[2], 0.0, 3U);
        return parabolaPoint(
            values, values[reference.key == model::PointKey::Start ? 4U : 5U]);
      }
      if (isBSpline(kind)) {
        const EntityShape &spline = shape(reference.entity);
        const auto [first, last] = model::nurbsDomain(
            nurbsView(spline, {values, spline.controlPointCount * 3U}));
        return bsplinePoint(spline, values,
                            reference.key == model::PointKey::Start ? first
                                                                    : last);
      }
      return PlainPoint{values[0], values[1]};
    };
    const auto lineLength = [&](const double *line) {
      return std::max(std::hypot(line[2] - line[0], line[3] - line[1]),
                      minimum_);
    };
    const auto cross = [](double ax, double ay, double bx, double by) {
      return ax * by - ay * bx;
    };
    const auto dot = [](double ax, double ay, double bx, double by) {
      return ax * bx + ay * by;
    };

    std::size_t count = 0;
    std::visit(
        [&]<typename Value>(const Value &value) {
          using Type = std::decay_t<Value>;
          if constexpr (std::is_same_v<Type, model::Coincident> ||
                        std::is_same_v<Type, model::Distance> ||
                        std::is_same_v<Type, model::HorizontalDistance> ||
                        std::is_same_v<Type, model::VerticalDistance>) {
            const PlainPoint first = selectedPoint(value.first);
            const PlainPoint second = selectedPoint(value.second);
            const double deltaX = second.x - first.x;
            const double deltaY = second.y - first.y;
            if constexpr (std::is_same_v<Type, model::Coincident>) {
              residuals[count++] = deltaX / scale_;
              residuals[count++] = deltaY / scale_;
            }
            if constexpr (std::is_same_v<Type, model::Distance>)
              residuals[count++] =
                  (std::hypot(deltaX, deltaY) - value.value.si()) / scale_;
            if constexpr (std::is_same_v<Type, model::HorizontalDistance>)
              residuals[count++] = (deltaX - value.value.si()) / scale_;
            if constexpr (std::is_same_v<Type, model::VerticalDistance>)
              residuals[count++] = (deltaY - value.value.si()) / scale_;
          }
          if constexpr (std::is_same_v<Type, model::Horizontal> ||
                        std::is_same_v<Type, model::Vertical>) {
            const auto [kind, line] = block(value.line);
            static_cast<void>(kind);
            if constexpr (std::is_same_v<Type, model::Horizontal>)
              residuals[count++] = (line[3] - line[1]) / scale_;
            else
              residuals[count++] = (line[2] - line[0]) / scale_;
          }
          if constexpr (std::is_same_v<Type, model::Parallel> ||
                        std::is_same_v<Type, model::Perpendicular> ||
                        std::is_same_v<Type, model::Collinear> ||
                        std::is_same_v<Type, model::AngleBetween>) {
            const auto [firstKind, first] = block(value.first);
            const auto [secondKind, second] = block(value.second);
            static_cast<void>(firstKind);
            static_cast<void>(secondKind);
            const double firstX = first[2] - first[0];
            const double firstY = first[3] - first[1];
            const double secondX = second[2] - second[0];
            const double secondY = second[3] - second[1];
            const double denominator = lineLength(first) * lineLength(second);
            const double normalizedCross =
                cross(firstX, firstY, secondX, secondY) / denominator;
            const double normalizedDot =
                dot(firstX, firstY, secondX, secondY) / denominator;
            if constexpr (std::is_same_v<Type, model::Parallel>)
              residuals[count++] = normalizedCross;
            if constexpr (std::is_same_v<Type, model::Perpendicular>)
              residuals[count++] = normalizedDot;
            if constexpr (std::is_same_v<Type, model::AngleBetween>) {
              residuals[count++] = normalizedCross - std::sin(value.value.si());
              residuals[count++] = normalizedDot - std::cos(value.value.si());
            }
            if constexpr (std::is_same_v<Type, model::Collinear>) {
              residuals[count++] = normalizedCross;
              residuals[count++] = cross(firstX, firstY, second[0] - first[0],
                                         second[1] - first[1]) /
                                   (lineLength(first) * scale_);
            }
          }
          if constexpr (std::is_same_v<Type, model::Tangent>) {
            auto [firstKind, first] = block(value.first);
            auto [secondKind, second] = block(value.second);
            if (isRadial(firstKind) && isLine(secondKind)) {
              std::swap(firstKind, secondKind);
              std::swap(first, second);
            }
            if (isLine(firstKind)) {
              const double lineX = first[2] - first[0];
              const double lineY = first[3] - first[1];
              const double separation =
                  std::abs(cross(lineX, lineY, second[0] - first[0],
                                 second[1] - first[1])) /
                  lineLength(first);
              residuals[count++] = (separation - second[2]) / scale_;
            } else {
              const double separation =
                  std::hypot(second[0] - first[0], second[1] - first[1]);
              const double target = value.mode == model::Tangency::External
                                        ? first[2] + second[2]
                                        : std::abs(first[2] - second[2]);
              residuals[count++] = (separation - target) / scale_;
            }
          }
          if constexpr (std::is_same_v<Type, model::Concentric>) {
            const auto [firstKind, first] = block(value.first);
            const auto [secondKind, second] = block(value.second);
            static_cast<void>(firstKind);
            static_cast<void>(secondKind);
            residuals[count++] = (second[0] - first[0]) / scale_;
            residuals[count++] = (second[1] - first[1]) / scale_;
          }
          if constexpr (std::is_same_v<Type, model::Equal>) {
            const auto [firstKind, first] = block(value.first);
            const auto [secondKind, second] = block(value.second);
            static_cast<void>(secondKind);
            residuals[count++] =
                isLine(firstKind)
                    ? (lineLength(second) - lineLength(first)) / scale_
                    : (second[2] - first[2]) / scale_;
          }
          if constexpr (std::is_same_v<Type, model::Midpoint>) {
            const PlainPoint selected = selectedPoint(value.point);
            const auto [kind, line] = block(value.line);
            static_cast<void>(kind);
            residuals[count++] =
                (selected.x - (line[0] + line[2]) * 0.5) / scale_;
            residuals[count++] =
                (selected.y - (line[1] + line[3]) * 0.5) / scale_;
          }
          if constexpr (std::is_same_v<Type, model::PointOnObject>) {
            const PlainPoint selected = selectedPoint(value.point);
            const auto [kind, curve] = block(value.curve);
            if (isLine(kind)) {
              residuals[count++] =
                  cross(curve[2] - curve[0], curve[3] - curve[1],
                        selected.x - curve[0], selected.y - curve[1]) /
                  (lineLength(curve) * scale_);
            } else if (isRadial(kind)) {
              residuals[count++] =
                  (std::hypot(selected.x - curve[0], selected.y - curve[1]) -
                   curve[2]) /
                  scale_;
            } else if (isEllipse(kind)) {
              const double cosine = std::cos(curve[4]);
              const double sine = std::sin(curve[4]);
              const double offsetX = selected.x - curve[0];
              const double offsetY = selected.y - curve[1];
              const double localX = cosine * offsetX + sine * offsetY;
              const double localY = -sine * offsetX + cosine * offsetY;
              residuals[count++] = (std::pow(localX / curve[2], 2.0) +
                                    std::pow(localY / curve[3], 2.0) - 1.0) *
                                   std::min(curve[2], curve[3]) /
                                   (2.0 * scale_);
            } else if (isHyperbola(kind)) {
              const double cosine = std::cos(curve[4]);
              const double sine = std::sin(curve[4]);
              const double offsetX = selected.x - curve[0];
              const double offsetY = selected.y - curve[1];
              const double localX = cosine * offsetX + sine * offsetY;
              const double localY = -sine * offsetX + cosine * offsetY;
              residuals[count++] = (std::pow(localX / curve[2], 2.0) -
                                    std::pow(localY / curve[3], 2.0) - 1.0) *
                                   std::min(curve[2], curve[3]) /
                                   (2.0 * scale_);
            } else if (isParabola(kind)) {
              const double cosine = std::cos(curve[3]);
              const double sine = std::sin(curve[3]);
              const double offsetX = selected.x - curve[0];
              const double offsetY = selected.y - curve[1];
              const double localX = cosine * offsetX + sine * offsetY;
              const double localY = -sine * offsetX + cosine * offsetY;
              const double denominator =
                  std::max(2.0 * curve[2], std::abs(localY));
              residuals[count++] = (localY * localY - 4.0 * curve[2] * localX) /
                                   (denominator * scale_);
            } else {
              const EntityShape &spline = shape(value.curve);
              const model::NurbsView view =
                  nurbsView(spline, {curve, spline.controlPointCount * 3U});
              const model::NurbsProjection projection =
                  model::projectToNurbs(view, {selected.x, selected.y});
              const model::NurbsPoint tangent =
                  model::differentiateNurbs(view, projection.parameter);
              const double tangentLength = std::hypot(tangent.x, tangent.y);
              const double offsetX = selected.x - projection.point.x;
              const double offsetY = selected.y - projection.point.y;
              residuals[count++] =
                  tangentLength > std::numeric_limits<double>::epsilon()
                      ? (-tangent.y * offsetX + tangent.x * offsetY) /
                            (tangentLength * scale_)
                      : std::hypot(offsetX, offsetY) / scale_;
            }
          }
          if constexpr (std::is_same_v<Type, model::Symmetric>) {
            const PlainPoint first = selectedPoint(value.first);
            const PlainPoint second = selectedPoint(value.second);
            const auto [kind, axis] = block(value.axis);
            static_cast<void>(kind);
            const double axisX = axis[2] - axis[0];
            const double axisY = axis[3] - axis[1];
            const double length = lineLength(axis);
            const double midpointX = (first.x + second.x) * 0.5;
            const double midpointY = (first.y + second.y) * 0.5;
            residuals[count++] =
                cross(axisX, axisY, midpointX - axis[0], midpointY - axis[1]) /
                (length * scale_);
            residuals[count++] =
                dot(axisX, axisY, second.x - first.x, second.y - first.y) /
                (length * scale_);
          }
          if constexpr (std::is_same_v<Type, model::SymmetricAboutPoint>) {
            const PlainPoint first = selectedPoint(value.first);
            const PlainPoint second = selectedPoint(value.second);
            const PlainPoint center = selectedPoint(value.center);
            residuals[count++] =
                ((first.x + second.x) * 0.5 - center.x) / scale_;
            residuals[count++] =
                ((first.y + second.y) * 0.5 - center.y) / scale_;
          }
          if constexpr (std::is_same_v<Type, model::Lock>) {
            const PlainPoint selected = selectedPoint(value.point);
            residuals[count++] = (selected.x - value.position.x.si()) / scale_;
            residuals[count++] = (selected.y - value.position.y.si()) / scale_;
          }
          if constexpr (std::is_same_v<Type, model::Block>) {
            const auto [kind, selected] = block(value.entity);
            static_cast<void>(kind);
            const std::vector<double> &anchor = anchors_.front();
            for (std::size_t index = 0; index < anchor.size(); ++index) {
              residuals[count++] =
                  (selected[index] - anchor[index]) /
                  parameterScale(shapes_.front(), index, scale_);
            }
          }
          if constexpr (std::is_same_v<Type, model::Group>) {
            std::vector<PlainPoint> selectedPoints;
            std::vector<PlainPoint> anchorPoints;
            for (std::size_t index = 0U; index < entities_.size(); ++index) {
              const auto [kind, selected] = block(entities_[index]);
              auto current = rigidPoints(shapes_[index], selected);
              auto reference =
                  rigidPoints(shapes_[index], anchors_[index].data());
              selectedPoints.insert(selectedPoints.end(), current.begin(),
                                    current.end());
              anchorPoints.insert(anchorPoints.end(), reference.begin(),
                                  reference.end());
              if (kind == Kind::Circle)
                residuals[count++] =
                    (selected[2] - anchors_[index][2]) / scale_;
              if (kind == Kind::BSpline) {
                const std::size_t weightOffset =
                    shapes_[index].controlPointCount * 2U;
                for (std::size_t weight = weightOffset;
                     weight < anchors_[index].size(); ++weight)
                  residuals[count++] =
                      selected[weight] - anchors_[index][weight];
              }
            }
            const auto axis = rigidAxis(anchorPoints, minimum_);
            if (!axis) {
              for (std::size_t index = 1U; index < anchorPoints.size();
                   ++index) {
                residuals[count++] =
                    ((selectedPoints[index].x - selectedPoints[0].x) -
                     (anchorPoints[index].x - anchorPoints[0].x)) /
                    scale_;
                residuals[count++] =
                    ((selectedPoints[index].y - selectedPoints[0].y) -
                     (anchorPoints[index].y - anchorPoints[0].y)) /
                    scale_;
              }
            } else {
              const auto [firstIndex, secondIndex] = *axis;
              const PlainPoint anchorAxis{
                  anchorPoints[secondIndex].x - anchorPoints[firstIndex].x,
                  anchorPoints[secondIndex].y - anchorPoints[firstIndex].y};
              const PlainPoint selectedAxis{
                  selectedPoints[secondIndex].x - selectedPoints[firstIndex].x,
                  selectedPoints[secondIndex].y - selectedPoints[firstIndex].y};
              const double anchorLength =
                  std::hypot(anchorAxis.x, anchorAxis.y);
              const double selectedLength = std::max(
                  std::hypot(selectedAxis.x, selectedAxis.y), minimum_);
              residuals[count++] = (selectedLength - anchorLength) / scale_;
              for (std::size_t index = 0U; index < anchorPoints.size();
                   ++index) {
                if (index == firstIndex || index == secondIndex)
                  continue;
                const PlainPoint anchorOffset{
                    anchorPoints[index].x - anchorPoints[firstIndex].x,
                    anchorPoints[index].y - anchorPoints[firstIndex].y};
                const PlainPoint selectedOffset{
                    selectedPoints[index].x - selectedPoints[firstIndex].x,
                    selectedPoints[index].y - selectedPoints[firstIndex].y};
                residuals[count++] = (dot(selectedAxis.x, selectedAxis.y,
                                          selectedOffset.x, selectedOffset.y) /
                                          selectedLength -
                                      dot(anchorAxis.x, anchorAxis.y,
                                          anchorOffset.x, anchorOffset.y) /
                                          anchorLength) /
                                     scale_;
                residuals[count++] =
                    (cross(selectedAxis.x, selectedAxis.y, selectedOffset.x,
                           selectedOffset.y) /
                         selectedLength -
                     cross(anchorAxis.x, anchorAxis.y, anchorOffset.x,
                           anchorOffset.y) /
                         anchorLength) /
                    scale_;
              }
            }
          }
          if constexpr (std::is_same_v<Type, model::Radius> ||
                        std::is_same_v<Type, model::Diameter>) {
            const auto [kind, curve] = block(value.curve);
            static_cast<void>(kind);
            const double measured = std::is_same_v<Type, model::Diameter>
                                        ? curve[2] * 2.0
                                        : curve[2];
            residuals[count++] = (measured - value.value.si()) / scale_;
          }
        },
        constraint_);
    return std::ranges::all_of(std::span{residuals, count}, [](double value) {
      return std::isfinite(value);
    });
  }

private:
  model::Constraint constraint_;
  std::vector<SketchEntityId> entities_;
  std::vector<EntityShape> shapes_;
  std::vector<std::vector<double>> anchors_;
  double scale_;
  double minimum_;
};

std::vector<SketchEntityId> referencedEntities(const model::Constraint &value) {
  std::vector<SketchEntityId> result;
  const auto add = [&result](SketchEntityId id) {
    if (std::ranges::find(result, id) == result.end())
      result.push_back(id);
  };
  std::visit(
      [&]<typename Constraint>(const Constraint &constraint) {
        using Type = std::decay_t<Constraint>;
        if constexpr (std::is_same_v<Type, model::Coincident> ||
                      std::is_same_v<Type, model::Distance> ||
                      std::is_same_v<Type, model::HorizontalDistance> ||
                      std::is_same_v<Type, model::VerticalDistance>) {
          add(constraint.first.entity);
          add(constraint.second.entity);
        } else if constexpr (std::is_same_v<Type, model::Horizontal> ||
                             std::is_same_v<Type, model::Vertical>) {
          add(constraint.line);
        } else if constexpr (std::is_same_v<Type, model::Midpoint>) {
          add(constraint.point.entity);
          add(constraint.line);
        } else if constexpr (std::is_same_v<Type, model::PointOnObject>) {
          add(constraint.point.entity);
          add(constraint.curve);
        } else if constexpr (std::is_same_v<Type, model::Symmetric>) {
          add(constraint.first.entity);
          add(constraint.second.entity);
          add(constraint.axis);
        } else if constexpr (std::is_same_v<Type, model::SymmetricAboutPoint>) {
          add(constraint.first.entity);
          add(constraint.second.entity);
          add(constraint.center.entity);
        } else if constexpr (std::is_same_v<Type, model::Lock>) {
          add(constraint.point.entity);
        } else if constexpr (std::is_same_v<Type, model::Block>) {
          add(constraint.entity);
        } else if constexpr (std::is_same_v<Type, model::Group>) {
          for (const SketchEntityId entity : constraint.entities)
            add(entity);
        } else if constexpr (std::is_same_v<Type, model::Radius> ||
                             std::is_same_v<Type, model::Diameter>) {
          add(constraint.curve);
        } else {
          add(constraint.first);
          add(constraint.second);
        }
      },
      value);
  return result;
}

std::size_t residualCount(const model::Constraint &constraint,
                          const std::vector<WorkingEntity> &entities,
                          const WorkingIndex &index,
                          const model::NumericalProfile &profile) {
  return std::visit(
      [&]<typename Value>(const Value &value) -> std::size_t {
        using Type = std::decay_t<Value>;
        if constexpr (std::is_same_v<Type, model::Coincident> ||
                      std::is_same_v<Type, model::Concentric> ||
                      std::is_same_v<Type, model::Midpoint> ||
                      std::is_same_v<Type, model::Symmetric> ||
                      std::is_same_v<Type, model::SymmetricAboutPoint> ||
                      std::is_same_v<Type, model::Lock> ||
                      std::is_same_v<Type, model::Collinear> ||
                      std::is_same_v<Type, model::AngleBetween>)
          return 2;
        if constexpr (std::is_same_v<Type, model::Block>)
          return entities[index.at(value.entity)].values.size();
        if constexpr (std::is_same_v<Type, model::Group>) {
          std::vector<PlainPoint> points;
          std::size_t invariantSizes = 0U;
          for (const SketchEntityId id : value.entities) {
            const WorkingEntity &entity = entities[index.at(id)];
            const std::vector<double> anchor =
                workingEntity(entity.source).values;
            auto entityPoints = rigidPoints(shapeOf(entity), anchor.data());
            points.insert(points.end(), entityPoints.begin(),
                          entityPoints.end());
            invariantSizes += entity.kind == Kind::Circle ? 1U
                              : entity.kind == Kind::BSpline
                                  ? entity.controlPointCount
                                  : 0U;
          }
          return invariantSizes +
                 (rigidAxis(points, profile.minimumLengthMeters)
                      ? 1U + 2U * (points.size() - 2U)
                      : 2U * (points.size() - 1U));
        }
        return 1;
      },
      constraint);
}

class DragCost final {
public:
  DragCost(EntityShape shape, model::PointKey key, model::Point2 target,
           double scale)
      : shape_(std::move(shape)), key_(key),
        target_{target.x.si(), target.y.si()}, scale_(scale) {}

  bool operator()(double const *const *parameters, double *residuals) const {
    const double *values = parameters[0];
    PlainPoint selected{values[0], values[1]};
    if (shape_.kind == Kind::Line && key_ == model::PointKey::End)
      selected = {values[2], values[3]};
    else if (shape_.kind == Kind::Arc && key_ != model::PointKey::Center) {
      const double angle = values[key_ == model::PointKey::Start ? 3U : 4U];
      selected = {values[0] + values[2] * std::cos(angle),
                  values[1] + values[2] * std::sin(angle)};
    } else if (isEllipse(shape_.kind) && key_ != model::PointKey::Center) {
      if (key_ == model::PointKey::Major)
        selected = ellipsePoint(values, 0.0);
      else if (key_ == model::PointKey::Minor)
        selected = ellipsePoint(values, std::numbers::pi / 2.0);
      else
        selected = ellipsePoint(
            values, values[key_ == model::PointKey::Start ? 5U : 6U]);
    } else if (isHyperbola(shape_.kind) && key_ != model::PointKey::Center) {
      if (key_ == model::PointKey::Major)
        selected = hyperbolaPoint(values, 0.0);
      else if (key_ == model::PointKey::Minor)
        selected = rotatedPoint(values, values[2], values[3], 4U);
      else if (key_ == model::PointKey::Focus)
        selected =
            rotatedPoint(values, std::hypot(values[2], values[3]), 0.0, 4U);
      else
        selected = hyperbolaPoint(
            values, values[key_ == model::PointKey::Start ? 5U : 6U]);
    } else if (isParabola(shape_.kind) && key_ != model::PointKey::Center) {
      if (key_ == model::PointKey::Focus)
        selected = rotatedPoint(values, values[2], 0.0, 3U);
      else
        selected = parabolaPoint(
            values, values[key_ == model::PointKey::Start ? 4U : 5U]);
    } else if (isBSpline(shape_.kind)) {
      const auto [first, last] = model::nurbsDomain(
          nurbsView(shape_, {values, shape_.controlPointCount * 3U}));
      selected = bsplinePoint(shape_, values,
                              key_ == model::PointKey::Start ? first : last);
    }
    residuals[0] = (selected.x - target_.x) / scale_;
    residuals[1] = (selected.y - target_.y) / scale_;
    return std::isfinite(residuals[0]) && std::isfinite(residuals[1]);
  }

private:
  EntityShape shape_;
  model::PointKey key_;
  PlainPoint target_;
  double scale_;
};

class PeriodicBSplineCost final {
public:
  PeriodicBSplineCost(std::size_t controlPointCount, std::size_t tailCount,
                      double scale)
      : controlPointCount_(controlPointCount), tailCount_(tailCount),
        scale_(scale) {}

  bool operator()(double const *const *parameters, double *residuals) const {
    const double *values = parameters[0];
    const std::size_t tailStart = controlPointCount_ - tailCount_;
    const std::size_t weightOffset = controlPointCount_ * 2U;
    std::size_t residual = 0U;
    for (std::size_t index = 0U; index < tailCount_; ++index) {
      residuals[residual++] =
          (values[(tailStart + index) * 2U] - values[index * 2U]) / scale_;
      residuals[residual++] =
          (values[(tailStart + index) * 2U + 1U] - values[index * 2U + 1U]) /
          scale_;
      residuals[residual++] = values[weightOffset + tailStart + index] -
                              values[weightOffset + index];
    }
    return std::ranges::all_of(
        std::span{residuals, tailCount_ * 3U},
        [](double value) { return std::isfinite(value); });
  }

private:
  std::size_t controlPointCount_;
  std::size_t tailCount_;
  double scale_;
};

class CancellationCallback final : public ceres::IterationCallback {
public:
  explicit CancellationCallback(CancellationToken token) : token_(token) {}
  ceres::CallbackReturnType
  operator()(const ceres::IterationSummary &) override {
    return token_.stop_requested() ? ceres::SOLVER_ABORT
                                   : ceres::SOLVER_CONTINUE;
  }

private:
  CancellationToken token_;
};

struct JacobianAnalysis {
  std::size_t rank = 0;
  std::vector<model::FreedomMode> modes;
  std::vector<std::size_t> pivotRows;
};

Result<JacobianAnalysis>
analyzeJacobian(ceres::Problem &problem,
                const std::vector<ceres::ResidualBlockId> &residualBlocks,
                const std::vector<WorkingEntity> &entities,
                const model::NumericalProfile &profile, bool computeModes,
                bool computeRedundancy) {
  const std::size_t variableCount =
      std::accumulate(entities.begin(), entities.end(), std::size_t{0},
                      [](std::size_t total, const WorkingEntity &entity) {
                        return total + entity.values.size();
                      });
  JacobianAnalysis result;
  if (residualBlocks.empty()) {
    if (computeModes && variableCount <= profile.maximumModeVariables) {
      for (const WorkingEntity &entity : entities) {
        for (std::size_t parameter = 0; parameter < entity.values.size();
             ++parameter) {
          model::FreedomMode mode;
          mode.components.push_back(
              {entity.id, std::vector<double>(entity.values.size(), 0.0)});
          mode.components.back().parameterDirection[parameter] = 1.0;
          result.modes.push_back(std::move(mode));
        }
      }
    }
    return result;
  }

  ceres::Problem::EvaluateOptions options;
  options.apply_loss_function = false;
  options.residual_blocks = residualBlocks;
  options.parameter_blocks.reserve(entities.size());
  for (const WorkingEntity &entity : entities)
    options.parameter_blocks.push_back(
        const_cast<double *>(entity.values.data()));
  ceres::CRSMatrix crs;
  if (!problem.Evaluate(options, nullptr, nullptr, nullptr, &crs))
    return std::unexpected(
        diagnostic("sketch.solver.jacobian-failed",
                   "solver could not evaluate the Jacobian"));

  std::optional<Eigen::SparseMatrix<double>> sparseMatrix;
  const auto makeSparseMatrix = [&]() -> Eigen::SparseMatrix<double> & {
    if (!sparseMatrix) {
      std::vector<Eigen::Triplet<double>> entries;
      entries.reserve(crs.values.size());
      for (int row = 0; row < crs.num_rows; ++row) {
        for (int offset = crs.rows[static_cast<std::size_t>(row)];
             offset < crs.rows[static_cast<std::size_t>(row + 1)]; ++offset)
          entries.emplace_back(row, crs.cols[static_cast<std::size_t>(offset)],
                               crs.values[static_cast<std::size_t>(offset)]);
      }
      sparseMatrix.emplace(crs.num_rows, crs.num_cols);
      sparseMatrix->setFromTriplets(entries.begin(), entries.end());
    }
    return *sparseMatrix;
  };

  if (!computeRedundancy && variableCount > 64U &&
      crs.num_rows >= crs.num_cols) {
    Eigen::SparseMatrix<double> normal =
        makeSparseMatrix().transpose() * makeSparseMatrix();
    Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> factor;
    factor.compute(normal);
    if (factor.info() == Eigen::Success && factor.vectorD().size() > 0) {
      const double maximum = factor.vectorD().maxCoeff();
      const double relative = std::max(profile.rankRelativeTolerance *
                                           profile.rankRelativeTolerance,
                                       std::numeric_limits<double>::epsilon() *
                                           static_cast<double>(variableCount));
      if (maximum > 0.0 &&
          (factor.vectorD().array() > maximum * relative).all()) {
        result.rank = variableCount;
        return result;
      }
    }
  }

  if (variableCount <= profile.maximumModeVariables) {
    Eigen::MatrixXd matrix = Eigen::MatrixXd::Zero(crs.num_rows, crs.num_cols);
    for (int row = 0; row < crs.num_rows; ++row) {
      for (int offset = crs.rows[static_cast<std::size_t>(row)];
           offset < crs.rows[static_cast<std::size_t>(row + 1)]; ++offset)
        matrix(row, crs.cols[static_cast<std::size_t>(offset)]) =
            crs.values[static_cast<std::size_t>(offset)];
    }
    Eigen::JacobiSVD<Eigen::MatrixXd> svd(matrix);
    svd.setThreshold(profile.rankRelativeTolerance);
    result.rank = static_cast<std::size_t>(svd.rank());

    if (computeRedundancy) {
      Eigen::ColPivHouseholderQR<Eigen::MatrixXd> rowQr(matrix.transpose());
      rowQr.setThreshold(profile.rankRelativeTolerance);
      const auto pivots = rowQr.colsPermutation().indices();
      for (Eigen::Index index = 0; index < rowQr.rank(); ++index)
        result.pivotRows.push_back(static_cast<std::size_t>(pivots[index]));
    }

    if (computeModes && result.rank < variableCount) {
      Eigen::JacobiSVD<Eigen::MatrixXd> modeSvd(matrix, Eigen::ComputeFullV);
      modeSvd.setThreshold(profile.rankRelativeTolerance);
      result.rank = static_cast<std::size_t>(modeSvd.rank());
      std::vector<std::size_t> entityOffsets;
      entityOffsets.reserve(entities.size());
      std::size_t offset = 0;
      for (const WorkingEntity &entity : entities) {
        entityOffsets.push_back(offset);
        offset += entity.values.size();
      }
      for (Eigen::Index column = static_cast<Eigen::Index>(result.rank);
           column < modeSvd.matrixV().cols(); ++column) {
        model::FreedomMode mode;
        for (std::size_t entityIndex = 0; entityIndex < entities.size();
             ++entityIndex) {
          std::vector<double> direction(entities[entityIndex].values.size());
          double maximum = 0.0;
          for (std::size_t parameter = 0; parameter < direction.size();
               ++parameter) {
            direction[parameter] =
                modeSvd.matrixV()(static_cast<Eigen::Index>(
                                      entityOffsets[entityIndex] + parameter),
                                  column);
            maximum = std::max(maximum, std::abs(direction[parameter]));
          }
          if (maximum > profile.rankRelativeTolerance)
            mode.components.push_back(
                {entities[entityIndex].id, std::move(direction)});
        }
        result.modes.push_back(std::move(mode));
      }
    }
    return result;
  }

  Eigen::SparseMatrix<double> &matrix = makeSparseMatrix();
  Eigen::SparseQR<Eigen::SparseMatrix<double>, Eigen::COLAMDOrdering<int>> qr;
  qr.setPivotThreshold(profile.rankRelativeTolerance);
  qr.compute(matrix);
  if (qr.info() != Eigen::Success)
    return std::unexpected(diagnostic("sketch.solver.rank-failed",
                                      "solver could not rank the Jacobian"));
  result.rank = static_cast<std::size_t>(qr.rank());

  if (computeRedundancy) {
    Eigen::SparseMatrix<double> transpose = matrix.transpose();
    Eigen::SparseQR<Eigen::SparseMatrix<double>, Eigen::COLAMDOrdering<int>>
        rowQr;
    rowQr.setPivotThreshold(profile.rankRelativeTolerance);
    rowQr.compute(transpose);
    if (rowQr.info() == Eigen::Success) {
      const auto pivots = rowQr.colsPermutation().indices();
      for (Eigen::Index index = 0; index < rowQr.rank(); ++index)
        result.pivotRows.push_back(static_cast<std::size_t>(pivots[index]));
    }
  }
  return result;
}

struct SolveAttempt {
  model::SolveResult result;
  bool documentSatisfied = false;
};

Result<SolveAttempt> solveOnce(const model::SolveInput &input,
                               bool includeDrag) {
  std::vector<WorkingEntity> entities;
  entities.reserve(input.definition.entities.size());
  for (const model::Entity &entity : input.definition.entities)
    entities.push_back(workingEntity(entity));
  if (auto prior = applyPrior(entities, input.priorSolution, input.numerical);
      !prior)
    return std::unexpected(std::move(prior.error()));
  const WorkingIndex index = indexOf(entities);

  ceres::Problem problem;
  for (WorkingEntity &entity : entities) {
    const int size = static_cast<int>(entity.values.size());
    problem.AddParameterBlock(entity.values.data(), size);
    const std::size_t coordinates = coordinateCount(entity);
    for (std::size_t offset = 0; offset < coordinates; ++offset) {
      problem.SetParameterLowerBound(entity.values.data(),
                                     static_cast<int>(offset),
                                     -input.numerical.maximumCoordinateMeters);
      problem.SetParameterUpperBound(entity.values.data(),
                                     static_cast<int>(offset),
                                     input.numerical.maximumCoordinateMeters);
    }
    if (isRadial(entity.kind)) {
      problem.SetParameterLowerBound(entity.values.data(), 2,
                                     input.numerical.minimumLengthMeters);
      problem.SetParameterUpperBound(entity.values.data(), 2,
                                     input.numerical.maximumCoordinateMeters);
    }
    if (isEllipse(entity.kind)) {
      for (int axis = 2; axis <= 3; ++axis) {
        problem.SetParameterLowerBound(entity.values.data(), axis,
                                       input.numerical.minimumLengthMeters);
        problem.SetParameterUpperBound(entity.values.data(), axis,
                                       input.numerical.maximumCoordinateMeters);
      }
    }
    if (isHyperbola(entity.kind)) {
      for (int axis = 2; axis <= 3; ++axis) {
        problem.SetParameterLowerBound(entity.values.data(), axis,
                                       input.numerical.minimumLengthMeters);
        problem.SetParameterUpperBound(entity.values.data(), axis,
                                       input.numerical.maximumCoordinateMeters);
      }
      const double maximumParameter =
          std::acosh(input.numerical.maximumCoordinateMeters /
                     input.numerical.minimumLengthMeters);
      for (int parameter = 5; parameter <= 6; ++parameter) {
        problem.SetParameterLowerBound(entity.values.data(), parameter,
                                       -maximumParameter);
        problem.SetParameterUpperBound(entity.values.data(), parameter,
                                       maximumParameter);
      }
    }
    if (isParabola(entity.kind)) {
      problem.SetParameterLowerBound(entity.values.data(), 2,
                                     input.numerical.minimumLengthMeters);
      problem.SetParameterUpperBound(entity.values.data(), 2,
                                     input.numerical.maximumCoordinateMeters);
      for (int parameter = 4; parameter <= 5; ++parameter) {
        problem.SetParameterLowerBound(
            entity.values.data(), parameter,
            -input.numerical.maximumCoordinateMeters);
        problem.SetParameterUpperBound(entity.values.data(), parameter,
                                       input.numerical.maximumCoordinateMeters);
      }
    }
    if (isBSpline(entity.kind)) {
      for (std::size_t weight = coordinates; weight < entity.values.size();
           ++weight) {
        problem.SetParameterLowerBound(entity.values.data(),
                                       static_cast<int>(weight), 1.0e-12);
        problem.SetParameterUpperBound(entity.values.data(),
                                       static_cast<int>(weight), 1.0e12);
      }
    }
  }

  std::vector<ceres::ResidualBlockId> intrinsicBlocks;
  std::size_t intrinsicRows = 0U;
  for (WorkingEntity &entity : entities) {
    if (!entity.periodic || entity.periodicTailCount == 0U)
      continue;
    auto *cost = new ceres::DynamicNumericDiffCostFunction<PeriodicBSplineCost,
                                                           ceres::CENTRAL>(
        new PeriodicBSplineCost(entity.controlPointCount,
                                entity.periodicTailCount,
                                input.numerical.typicalLengthMeters));
    cost->AddParameterBlock(static_cast<int>(entity.values.size()));
    const std::size_t rows = entity.periodicTailCount * 3U;
    cost->SetNumResiduals(static_cast<int>(rows));
    intrinsicBlocks.push_back(
        problem.AddResidualBlock(cost, nullptr, entity.values.data()));
    intrinsicRows += rows;
  }

  std::vector<ceres::ResidualBlockId> documentBlocks;
  std::vector<std::size_t> rowCounts;
  documentBlocks.reserve(input.definition.constraints.size());
  rowCounts.reserve(input.definition.constraints.size());
  for (const model::Constraint &constraint : input.definition.constraints) {
    const std::vector<SketchEntityId> references =
        referencedEntities(constraint);
    std::vector<EntityShape> shapes;
    std::vector<std::vector<double>> anchors;
    std::vector<double *> parameterBlocks;
    shapes.reserve(references.size());
    anchors.reserve(references.size());
    parameterBlocks.reserve(references.size());
    for (const SketchEntityId id : references) {
      const WorkingEntity &entity = entities[index.at(id)];
      shapes.push_back(shapeOf(entity));
      anchors.push_back(workingEntity(entity.source).values);
      parameterBlocks.push_back(entities[index.at(id)].values.data());
    }
    auto *cost = new ceres::DynamicNumericDiffCostFunction<ConstraintCost,
                                                           ceres::CENTRAL>(
        new ConstraintCost(constraint, references, shapes, anchors,
                           input.numerical));
    for (const SketchEntityId id : references)
      cost->AddParameterBlock(
          static_cast<int>(entities[index.at(id)].values.size()));
    const std::size_t rows =
        residualCount(constraint, entities, index, input.numerical);
    cost->SetNumResiduals(static_cast<int>(rows));
    documentBlocks.push_back(
        problem.AddResidualBlock(cost, nullptr, parameterBlocks));
    rowCounts.push_back(rows);
  }

  if (includeDrag && input.drag) {
    const auto found = index.find(input.drag->point.entity);
    if (found == index.end())
      return std::unexpected(diagnostic("sketch.drag.missing-entity",
                                        "drag target entity is missing"));
    WorkingEntity &entity = entities[found->second];
    if (!validPointKey(entity.kind, input.drag->point.key))
      return std::unexpected(diagnostic("sketch.drag.invalid-point-key",
                                        "drag point key is invalid"));
    auto *cost =
        new ceres::DynamicNumericDiffCostFunction<DragCost, ceres::CENTRAL>(
            new DragCost(shapeOf(entity), input.drag->point.key,
                         input.drag->target,
                         input.numerical.typicalLengthMeters));
    cost->AddParameterBlock(static_cast<int>(entity.values.size()));
    cost->SetNumResiduals(2);
    problem.AddResidualBlock(cost, nullptr, entity.values.data());
  }

  model::SolveResult result;
  if (input.cancellation.stop_requested()) {
    result.status = model::SolveStatus::Cancelled;
    return SolveAttempt{std::move(result), false};
  }

  ceres::Solver::Summary summary;
  const bool ranSolver = !intrinsicBlocks.empty() || !documentBlocks.empty() ||
                         (includeDrag && input.drag.has_value());
  if (ranSolver) {
    CancellationCallback cancellation{input.cancellation};
    ceres::Solver::Options options;
    options.max_num_iterations =
        static_cast<int>(input.numerical.maximumIterations);
    options.linear_solver_type = ceres::SPARSE_NORMAL_CHOLESKY;
    options.sparse_linear_algebra_library_type = ceres::EIGEN_SPARSE;
    options.trust_region_strategy_type = ceres::DOGLEG;
    options.dogleg_type = ceres::SUBSPACE_DOGLEG;
    options.function_tolerance = 1.0e-14;
    options.gradient_tolerance = 1.0e-14;
    options.parameter_tolerance = 1.0e-14;
    options.logging_type = ceres::SILENT;
    options.minimizer_progress_to_stdout = false;
    options.num_threads = 1;
    options.callbacks.push_back(&cancellation);
    ceres::Solve(options, &problem, &summary);
    result.iterations = static_cast<std::uint32_t>(summary.iterations.size());
  }

  if (input.cancellation.stop_requested() ||
      summary.termination_type == ceres::USER_FAILURE) {
    result.status = model::SolveStatus::Cancelled;
    return SolveAttempt{std::move(result), false};
  }

  result.geometry.reserve(entities.size());
  for (const WorkingEntity &entity : entities) {
    auto solved = rebuild(entity);
    if (!solved)
      return std::unexpected(std::move(solved.error()));
    result.geometry.push_back(std::move(*solved));
  }
  auto residuals = model::evaluateResiduals(input.definition, result.geometry,
                                            input.numerical);
  if (!residuals)
    return std::unexpected(std::move(residuals.error()));
  result.residuals = std::move(*residuals);
  const bool satisfied = std::ranges::all_of(
      result.residuals, &model::ConstraintResidual::satisfied);

  const bool computeRedundancy = input.definition.constraints.size() <=
                                 input.numerical.maximumRedundancyConstraints;
  std::vector<ceres::ResidualBlockId> analysisBlocks = intrinsicBlocks;
  analysisBlocks.insert(analysisBlocks.end(), documentBlocks.begin(),
                        documentBlocks.end());
  auto jacobian = analyzeJacobian(problem, analysisBlocks, entities,
                                  input.numerical, true, computeRedundancy);
  if (!jacobian)
    return std::unexpected(std::move(jacobian.error()));
  const std::size_t variableCount =
      std::accumulate(entities.begin(), entities.end(), std::size_t{0},
                      [](std::size_t total, const WorkingEntity &entity) {
                        return total + entity.values.size();
                      });
  result.degreesOfFreedom =
      variableCount - std::min(variableCount, jacobian->rank);
  result.modes = std::move(jacobian->modes);

  if (computeRedundancy) {
    std::unordered_set<std::size_t> pivotRows(jacobian->pivotRows.begin(),
                                              jacobian->pivotRows.end());
    std::size_t row = intrinsicRows;
    for (std::size_t constraintIndex = 0; constraintIndex < rowCounts.size();
         ++constraintIndex) {
      bool contributes = false;
      for (std::size_t offset = 0; offset < rowCounts[constraintIndex];
           ++offset)
        contributes = contributes || pivotRows.contains(row + offset);
      if (!contributes)
        result.redundantConstraints.push_back(
            model::constraintId(input.definition.constraints[constraintIndex]));
      row += rowCounts[constraintIndex];
    }
  }

  if (!satisfied) {
    model::ConflictSet conflict;
    for (const model::ConstraintResidual &residual : result.residuals) {
      if (!residual.satisfied)
        conflict.constraints.push_back(residual.constraint);
    }
    if (!conflict.constraints.empty())
      result.conflicts.push_back(std::move(conflict));
  }

  if (ranSolver && summary.termination_type == ceres::FAILURE) {
    result.status = model::SolveStatus::Diverged;
    result.diagnostics.push_back(
        diagnostic("sketch.solver.failed", "sketch solver failed to converge"));
  } else if (!satisfied) {
    result.status = model::SolveStatus::Inconsistent;
  } else {
    result.status = result.degreesOfFreedom == 0
                        ? model::SolveStatus::Solved
                        : model::SolveStatus::Underconstrained;
  }
  return SolveAttempt{std::move(result), satisfied};
}

} // namespace

Result<sketch::SolveResult>
CeresSketchSolver::solve(const sketch::SolveInput &input) const {
  if (auto valid = sketch::validate(input.definition, input.numerical); !valid)
    return std::unexpected(std::move(valid.error()));
  auto attempt = solveOnce(input, input.drag.has_value());
  if (!attempt)
    return std::unexpected(std::move(attempt.error()));
  if (input.drag && !attempt->documentSatisfied &&
      attempt->result.status != sketch::SolveStatus::Cancelled) {
    auto constrained = solveOnce(input, false);
    if (!constrained)
      return std::unexpected(std::move(constrained.error()));
    constrained->result.diagnostics.push_back(
        diagnostic("sketch.drag.blocked",
                   "drag target conflicts with existing constraints",
                   Severity::Information));
    return std::move(constrained->result);
  }
  return std::move(attempt->result);
}

} // namespace kearne::adapters
