#include <kearne/adapters/occ_curve_geometry.hpp>

#include <Geom2dAPI_InterCurveCurve.hxx>
#include <Geom2dAPI_ProjectPointOnCurve.hxx>
#include <Geom2d_BSplineCurve.hxx>
#include <Geom2d_BezierCurve.hxx>
#include <Geom2d_Circle.hxx>
#include <Geom2d_BoundedCurve.hxx>
#include <Geom2d_Curve.hxx>
#include <Geom2d_Ellipse.hxx>
#include <Geom2d_Hyperbola.hxx>
#include <Geom2d_Parabola.hxx>
#include <Geom2d_TrimmedCurve.hxx>
#include <Geom2dConvert.hxx>
#include <Geom2dConvert_CompCurveToBSplineCurve.hxx>
#include <IntRes2d_IntersectionPoint.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>
#include <TColStd_Array1OfInteger.hxx>
#include <TColStd_Array1OfReal.hxx>
#include <TColgp_Array1OfPnt2d.hxx>
#include <gp_Ax22d.hxx>
#include <gp_Circ2d.hxx>
#include <gp_Dir2d.hxx>
#include <gp_Elips2d.hxx>
#include <gp_Hypr2d.hxx>
#include <gp_Parab2d.hxx>
#include <gp_Pnt2d.hxx>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <limits>
#include <new>
#include <numeric>
#include <numbers>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace kearne::adapters {
namespace {

namespace model = sketch;
constexpr double millimetresPerMetre = 1'000.0;
constexpr std::size_t maximumIntersections = 4'096U;
constexpr std::size_t maximumTrimCurves = 65'536U;
constexpr std::size_t maximumTrimCuts = 65'536U;

gp_Pnt2d point(model::Point2 value) {
  return {value.x.si() * millimetresPerMetre,
          value.y.si() * millimetresPerMetre};
}

Result<model::Point2> point(const gp_Pnt2d &value) {
  auto x = model::LengthValue::fromSi(value.X() / millimetresPerMetre);
  auto y = model::LengthValue::fromSi(value.Y() / millimetresPerMetre);
  if (!x || !y)
    return std::unexpected(diagnostic("sketch.occ.curve-point",
                                      "curve operation produced an invalid point"));
  return model::Point2{*x, *y};
}

gp_Ax22d axes(model::Point2 center, double rotation) {
  return {point(center), gp_Dir2d{std::cos(rotation), std::sin(rotation)},
          Standard_True};
}

Handle(Geom2d_Curve) trimmed(const Handle(Geom2d_Curve) & basis,
                             double first, double last) {
  const auto [lower, upper] = std::minmax(first, last);
  return new Geom2d_TrimmedCurve(basis, lower, upper, Standard_True,
                                 Standard_False);
}

Result<Handle(Geom2d_Curve)> bspline(const model::BSplineEntity &entity) {
  const Standard_Integer poleCount =
      static_cast<Standard_Integer>(entity.controlPoints.size());
  TColgp_Array1OfPnt2d poles(1, poleCount);
  TColStd_Array1OfReal weights(1, poleCount);
  for (Standard_Integer index = 1; index <= poleCount; ++index) {
    const std::size_t source = static_cast<std::size_t>(index - 1);
    poles.SetValue(index, point(entity.controlPoints[source]));
    weights.SetValue(index, entity.weights[source].si());
  }

  std::vector<double> uniqueKnots;
  std::vector<Standard_Integer> multiplicities;
  uniqueKnots.reserve(entity.knots.size());
  multiplicities.reserve(entity.knots.size());
  for (const model::DimensionlessValue knot : entity.knots) {
    if (!uniqueKnots.empty() && knot.si() == uniqueKnots.back()) {
      ++multiplicities.back();
    } else {
      uniqueKnots.push_back(knot.si());
      multiplicities.push_back(1);
    }
  }
  const Standard_Integer knotCount =
      static_cast<Standard_Integer>(uniqueKnots.size());
  TColStd_Array1OfReal knots(1, knotCount);
  TColStd_Array1OfInteger mults(1, knotCount);
  for (Standard_Integer index = 1; index <= knotCount; ++index) {
    const std::size_t source = static_cast<std::size_t>(index - 1);
    knots.SetValue(index, uniqueKnots[source]);
    mults.SetValue(index, multiplicities[source]);
  }
  return Handle(Geom2d_Curve){new Geom2d_BSplineCurve(
      poles, weights, knots, mults, static_cast<Standard_Integer>(entity.degree),
      Standard_False)};
}

Result<Handle(Geom2d_Curve)> curve(const model::Entity &entity) {
  return std::visit(
      []<typename Value>(const Value &value) -> Result<Handle(Geom2d_Curve)> {
        using Type = std::decay_t<Value>;
        if constexpr (std::is_same_v<Type, model::PointEntity>) {
          return std::unexpected(diagnostic("sketch.occ.not-curve",
                                            "point geometry is not a curve"));
        } else if constexpr (std::is_same_v<Type, model::LineEntity>) {
          TColgp_Array1OfPnt2d poles(1, 2);
          poles.SetValue(1, point(value.start));
          poles.SetValue(2, point(value.end));
          return Handle(Geom2d_Curve){new Geom2d_BezierCurve(poles)};
        } else if constexpr (std::is_same_v<Type, model::CircleEntity>) {
          return Handle(Geom2d_Curve){new Geom2d_Circle(
              gp_Circ2d{axes(value.center, 0.0),
                        value.radius.si() * millimetresPerMetre})};
        } else if constexpr (std::is_same_v<Type, model::ArcEntity>) {
          Handle(Geom2d_Curve) basis = new Geom2d_Circle(
              gp_Circ2d{axes(value.center, 0.0),
                        value.radius.si() * millimetresPerMetre});
          return trimmed(basis, value.startAngle.si(), value.endAngle.si());
        } else if constexpr (std::is_same_v<Type, model::EllipseEntity>) {
          return Handle(Geom2d_Curve){new Geom2d_Ellipse(gp_Elips2d{
              axes(value.center, value.rotation.si()),
              value.majorRadius.si() * millimetresPerMetre,
              value.minorRadius.si() * millimetresPerMetre})};
        } else if constexpr (std::is_same_v<Type,
                                            model::EllipticalArcEntity>) {
          Handle(Geom2d_Curve) basis = new Geom2d_Ellipse(gp_Elips2d{
              axes(value.center, value.rotation.si()),
              value.majorRadius.si() * millimetresPerMetre,
              value.minorRadius.si() * millimetresPerMetre});
          return trimmed(basis, value.startParameter.si(),
                         value.endParameter.si());
        } else if constexpr (std::is_same_v<Type,
                                            model::HyperbolicArcEntity>) {
          Handle(Geom2d_Curve) basis = new Geom2d_Hyperbola(gp_Hypr2d{
              axes(value.center, value.rotation.si()),
              value.majorRadius.si() * millimetresPerMetre,
              value.minorRadius.si() * millimetresPerMetre});
          return trimmed(basis, value.startParameter.si(),
                         value.endParameter.si());
        } else if constexpr (std::is_same_v<Type,
                                            model::ParabolicArcEntity>) {
          Handle(Geom2d_Curve) basis = new Geom2d_Parabola(gp_Parab2d{
              axes(value.vertex, value.rotation.si()),
              value.focalLength.si() * millimetresPerMetre});
          return trimmed(basis,
                         value.startParameter.si() * millimetresPerMetre,
                         value.endParameter.si() * millimetresPerMetre);
        } else {
          return bspline(value);
        }
      },
      entity);
}

double normalizedAngle(double angle) {
  angle = std::fmod(angle, 2.0 * std::numbers::pi);
  return angle < 0.0 ? angle + 2.0 * std::numbers::pi : angle;
}

double boundedAngle(double angle, double first, double last) {
  const double middle = std::midpoint(first, last);
  return angle + std::round((middle - angle) / (2.0 * std::numbers::pi)) *
                     2.0 * std::numbers::pi;
}

Result<CurveParameter>
parameter(const model::Entity &entity, const gp_Pnt2d &location,
          std::optional<double> occParameter = std::nullopt) {
  return std::visit(
      [&](const auto &value) -> Result<CurveParameter> {
        using Type = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Type, model::PointEntity>) {
          return std::unexpected(diagnostic("sketch.occ.not-curve",
                                            "point geometry is not a curve"));
        } else if constexpr (std::is_same_v<Type, model::LineEntity>) {
          const double dx = value.end.x.si() - value.start.x.si();
          const double dy = value.end.y.si() - value.start.y.si();
          const double qx = location.X() / millimetresPerMetre -
                            value.start.x.si();
          const double qy = location.Y() / millimetresPerMetre -
                            value.start.y.si();
          auto result = model::DimensionlessValue::fromSi(
              std::clamp((qx * dx + qy * dy) / (dx * dx + dy * dy), 0.0,
                         1.0));
          if (result)
            return CurveParameter{*result};
        } else if constexpr (std::is_same_v<Type, model::CircleEntity> ||
                             std::is_same_v<Type, model::ArcEntity>) {
          const double angle = std::atan2(
              location.Y() / millimetresPerMetre - value.center.y.si(),
              location.X() / millimetresPerMetre - value.center.x.si());
          const double resolved =
              [&] {
                if constexpr (std::is_same_v<Type, model::CircleEntity>)
                  return normalizedAngle(angle);
                else
                  return boundedAngle(angle, value.startAngle.si(),
                                      value.endAngle.si());
              }();
          auto result = model::AngleValue::fromSi(resolved);
          if (result)
            return CurveParameter{*result};
        } else if constexpr (std::is_same_v<Type, model::EllipseEntity> ||
                             std::is_same_v<Type,
                                            model::EllipticalArcEntity>) {
          const double x = location.X() / millimetresPerMetre -
                           value.center.x.si();
          const double y = location.Y() / millimetresPerMetre -
                           value.center.y.si();
          const double cosine = std::cos(value.rotation.si());
          const double sine = std::sin(value.rotation.si());
          const double angle = std::atan2(
              (-sine * x + cosine * y) / value.minorRadius.si(),
              (cosine * x + sine * y) / value.majorRadius.si());
          const double resolved = [&] {
            if constexpr (std::is_same_v<Type, model::EllipseEntity>)
              return normalizedAngle(angle);
            else
              return boundedAngle(angle, value.startParameter.si(),
                                  value.endParameter.si());
          }();
          auto result = model::AngleValue::fromSi(resolved);
          if (result)
            return CurveParameter{*result};
        } else if constexpr (std::is_same_v<Type,
                                            model::HyperbolicArcEntity>) {
          const double x = location.X() / millimetresPerMetre -
                           value.center.x.si();
          const double y = location.Y() / millimetresPerMetre -
                           value.center.y.si();
          const double localY = -std::sin(value.rotation.si()) * x +
                                std::cos(value.rotation.si()) * y;
          auto result = model::DimensionlessValue::fromSi(
              std::asinh(localY / value.minorRadius.si()));
          if (result)
            return CurveParameter{*result};
        } else if constexpr (std::is_same_v<Type,
                                            model::ParabolicArcEntity>) {
          const double x = location.X() / millimetresPerMetre -
                           value.vertex.x.si();
          const double y = location.Y() / millimetresPerMetre -
                           value.vertex.y.si();
          auto result = model::LengthValue::fromSi(
              -std::sin(value.rotation.si()) * x +
              std::cos(value.rotation.si()) * y);
          if (result)
            return CurveParameter{*result};
        } else if constexpr (std::is_same_v<Type, model::BSplineEntity>) {
          if (occParameter) {
            auto result = model::DimensionlessValue::fromSi(*occParameter);
            if (result)
              return CurveParameter{*result};
          }
        }
        return std::unexpected(
            diagnostic("sketch.occ.curve-parameter",
                       "curve operation produced an invalid parameter"));
      },
      entity);
}

double scalar(const CurveParameter &parameter) {
  return std::visit([](const auto &value) { return value.si(); }, parameter);
}

struct ParameterDomain {
  double lower = 0.0;
  double upper = 0.0;
  bool closed = false;
};

Result<ParameterDomain> parameterDomain(const model::Entity &entity) {
  return std::visit(
      []<typename Value>(const Value &value) -> Result<ParameterDomain> {
        using Type = std::decay_t<Value>;
        if constexpr (std::is_same_v<Type, model::PointEntity>) {
          return std::unexpected(diagnostic("sketch.trim.not-curve",
                                            "Trim requires curve geometry"));
        } else if constexpr (std::is_same_v<Type, model::LineEntity>) {
          return ParameterDomain{0.0, 1.0, false};
        } else if constexpr (std::is_same_v<Type, model::CircleEntity> ||
                             std::is_same_v<Type, model::EllipseEntity>) {
          return ParameterDomain{0.0, 2.0 * std::numbers::pi, true};
        } else if constexpr (std::is_same_v<Type, model::ArcEntity>) {
          const double lower =
              std::min(value.startAngle.si(), value.endAngle.si());
          const double upper =
              std::max(value.startAngle.si(), value.endAngle.si());
          return ParameterDomain{lower, upper, false};
        } else if constexpr (std::is_same_v<Type,
                                            model::EllipticalArcEntity> ||
                             std::is_same_v<Type,
                                            model::HyperbolicArcEntity> ||
                             std::is_same_v<Type,
                                            model::ParabolicArcEntity>) {
          const double lower =
              std::min(value.startParameter.si(), value.endParameter.si());
          const double upper =
              std::max(value.startParameter.si(), value.endParameter.si());
          return ParameterDomain{lower, upper, false};
        } else {
          const double lower = value.knots[value.degree].si();
          const double upper = value.knots[value.controlPoints.size()].si();
          return ParameterDomain{lower, upper, value.periodic};
        }
      },
      entity);
}

bool positiveOrientation(const model::Entity &entity) {
  return std::visit(
      []<typename Value>(const Value &value) {
        using Type = std::decay_t<Value>;
        if constexpr (std::is_same_v<Type, model::ArcEntity>)
          return value.endAngle.si() > value.startAngle.si();
        if constexpr (std::is_same_v<Type, model::EllipticalArcEntity> ||
                      std::is_same_v<Type, model::HyperbolicArcEntity> ||
                      std::is_same_v<Type, model::ParabolicArcEntity>)
          return value.endParameter.si() > value.startParameter.si();
        return true;
      },
      entity);
}

bool construction(const model::Entity &entity) {
  return std::visit([](const auto &value) { return value.construction; },
                    entity);
}

double parameterTolerance(const model::Entity &entity,
                          const model::NumericalProfile &profile) {
  return std::visit(
      [&](const auto &value) {
        using Type = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Type, model::LineEntity>)
          return profile.lengthToleranceMeters /
                 std::hypot(value.end.x.si() - value.start.x.si(),
                            value.end.y.si() - value.start.y.si());
        if constexpr (std::is_same_v<Type, model::CircleEntity> ||
                      std::is_same_v<Type, model::ArcEntity>)
          return profile.lengthToleranceMeters / value.radius.si();
        if constexpr (std::is_same_v<Type, model::EllipseEntity> ||
                      std::is_same_v<Type, model::EllipticalArcEntity> ||
                      std::is_same_v<Type, model::HyperbolicArcEntity>)
          return profile.lengthToleranceMeters /
                 std::min(value.majorRadius.si(), value.minorRadius.si());
        if constexpr (std::is_same_v<Type, model::ParabolicArcEntity>)
          return profile.lengthToleranceMeters;
        if constexpr (std::is_same_v<Type, model::BSplineEntity>) {
          const double span = value.knots[value.controlPoints.size()].si() -
                              value.knots[value.degree].si();
          return std::max(profile.angleToleranceRadians,
                          std::abs(span) * 1.0e-12);
        }
        return profile.angleToleranceRadians;
      },
      entity);
}

double pointDistance(model::Point2 first, model::Point2 second) {
  return std::hypot(first.x.si() - second.x.si(),
                    first.y.si() - second.y.si());
}

Result<model::BSplineEntity>
exportBSpline(const Handle(Geom2d_BSplineCurve) &curve, SketchEntityId id,
              bool constructionValue) {
  if (curve.IsNull())
    return std::unexpected(diagnostic("sketch.occ.bspline-size",
                                      "B-spline result is unsupported"));
  const bool periodic = curve->IsPeriodic();
  Handle(Geom2d_BSplineCurve) canonical =
      Handle(Geom2d_BSplineCurve)::DownCast(curve->Copy());
  if (canonical.IsNull())
    return std::unexpected(diagnostic("sketch.occ.bspline-copy",
                                      "B-spline result copy failed"));
  if (periodic)
    canonical->SetNotPeriodic();
  if (canonical->NbPoles() < 2 || canonical->NbPoles() > 1'024 ||
      canonical->Degree() < 1 || canonical->Degree() > 25)
    return std::unexpected(diagnostic("sketch.occ.bspline-size",
                                      "B-spline result is unsupported"));
  std::vector<model::Point2> controlPoints;
  std::vector<model::DimensionlessValue> knots;
  std::vector<model::DimensionlessValue> weights;
  controlPoints.reserve(static_cast<std::size_t>(canonical->NbPoles()));
  weights.reserve(static_cast<std::size_t>(canonical->NbPoles()));
  for (Standard_Integer index = 1; index <= canonical->NbPoles(); ++index) {
    auto convertedPoint = point(canonical->Pole(index));
    auto weight =
        model::DimensionlessValue::fromSi(canonical->Weight(index));
    if (!convertedPoint || !weight)
      return std::unexpected(diagnostic("sketch.occ.bspline-result",
                                        "B-spline segment is invalid"));
    controlPoints.push_back(*convertedPoint);
    weights.push_back(*weight);
  }
  for (Standard_Integer index = 1; index <= canonical->NbKnots(); ++index) {
    auto knot = model::DimensionlessValue::fromSi(canonical->Knot(index));
    if (!knot)
      return std::unexpected(diagnostic("sketch.occ.bspline-result",
                                        "B-spline segment is invalid"));
    for (Standard_Integer repeat = 0; repeat < canonical->Multiplicity(index);
         ++repeat)
      knots.push_back(*knot);
  }
  return model::BSplineEntity{
      id,
      std::move(controlPoints),
      std::move(knots),
      std::move(weights),
      static_cast<std::uint32_t>(canonical->Degree()),
      periodic,
      constructionValue};
}

Result<model::BSplineEntity>
bsplineSegment(const model::BSplineEntity &source, SketchEntityId id,
               double lower, double upper) {
  auto converted = bspline(source);
  if (!converted)
    return std::unexpected(std::move(converted.error()));
  Handle(Geom2d_BSplineCurve) segment =
      Handle(Geom2d_BSplineCurve)::DownCast((*converted)->Copy());
  if (segment.IsNull())
    return std::unexpected(diagnostic("sketch.occ.bspline-copy",
                                      "B-spline segment copy failed"));
  if (source.periodic)
    segment->SetPeriodic();
  segment->Segment(lower, upper, Precision::PConfusion());
  return exportBSpline(segment, id, source.construction);
}

Result<model::Entity> curveSegment(const model::Entity &source,
                                   SketchEntityId id, double lower,
                                   double upper) {
  return std::visit(
      [&](const auto &value) -> Result<model::Entity> {
        using Type = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Type, model::PointEntity>) {
          return std::unexpected(diagnostic("sketch.trim.not-curve",
                                            "Trim requires curve geometry"));
        } else if constexpr (std::is_same_v<Type, model::LineEntity>) {
          const auto interpolate = [&](double parameter) {
            const double x = std::lerp(value.start.x.si(), value.end.x.si(),
                                       parameter);
            const double y = std::lerp(value.start.y.si(), value.end.y.si(),
                                       parameter);
            auto convertedX = model::LengthValue::fromSi(x);
            auto convertedY = model::LengthValue::fromSi(y);
            return model::Point2{*convertedX, *convertedY};
          };
          return model::LineEntity{id, interpolate(lower), interpolate(upper),
                                   value.construction};
        } else if constexpr (std::is_same_v<Type, model::CircleEntity>) {
          auto first = model::AngleValue::fromSi(lower);
          auto last = model::AngleValue::fromSi(upper);
          if (!first || !last)
            return std::unexpected(diagnostic("sketch.trim.parameter",
                                              "Trim parameter is invalid"));
          return model::ArcEntity{id, value.center, value.radius, *first,
                                  *last, value.construction};
        } else if constexpr (std::is_same_v<Type, model::ArcEntity>) {
          auto first = model::AngleValue::fromSi(
              positiveOrientation(source) ? lower : upper);
          auto last = model::AngleValue::fromSi(
              positiveOrientation(source) ? upper : lower);
          if (!first || !last)
            return std::unexpected(diagnostic("sketch.trim.parameter",
                                              "Trim parameter is invalid"));
          model::ArcEntity result = value;
          result.id = id;
          result.startAngle = *first;
          result.endAngle = *last;
          return result;
        } else if constexpr (std::is_same_v<Type, model::EllipseEntity>) {
          auto first = model::AngleValue::fromSi(lower);
          auto last = model::AngleValue::fromSi(upper);
          if (!first || !last)
            return std::unexpected(diagnostic("sketch.trim.parameter",
                                              "Trim parameter is invalid"));
          return model::EllipticalArcEntity{
              id,          value.center, value.majorRadius, value.minorRadius,
              value.rotation, *first,    *last,             value.construction};
        } else if constexpr (std::is_same_v<Type,
                                            model::EllipticalArcEntity>) {
          auto first = model::AngleValue::fromSi(
              positiveOrientation(source) ? lower : upper);
          auto last = model::AngleValue::fromSi(
              positiveOrientation(source) ? upper : lower);
          if (!first || !last)
            return std::unexpected(diagnostic("sketch.trim.parameter",
                                              "Trim parameter is invalid"));
          model::EllipticalArcEntity result = value;
          result.id = id;
          result.startParameter = *first;
          result.endParameter = *last;
          return result;
        } else if constexpr (std::is_same_v<Type,
                                            model::HyperbolicArcEntity>) {
          auto first = model::DimensionlessValue::fromSi(
              positiveOrientation(source) ? lower : upper);
          auto last = model::DimensionlessValue::fromSi(
              positiveOrientation(source) ? upper : lower);
          if (!first || !last)
            return std::unexpected(diagnostic("sketch.trim.parameter",
                                              "Trim parameter is invalid"));
          model::HyperbolicArcEntity result = value;
          result.id = id;
          result.startParameter = *first;
          result.endParameter = *last;
          return result;
        } else if constexpr (std::is_same_v<Type,
                                            model::ParabolicArcEntity>) {
          auto first = model::LengthValue::fromSi(
              positiveOrientation(source) ? lower : upper);
          auto last = model::LengthValue::fromSi(
              positiveOrientation(source) ? upper : lower);
          if (!first || !last)
            return std::unexpected(diagnostic("sketch.trim.parameter",
                                              "Trim parameter is invalid"));
          model::ParabolicArcEntity result = value;
          result.id = id;
          result.startParameter = *first;
          result.endParameter = *last;
          return result;
        } else {
          return bsplineSegment(value, id, lower, upper);
        }
      },
      source);
}

Result<void> validateTolerance(double toleranceMetres) {
  if (!std::isfinite(toleranceMetres) || toleranceMetres <= 0.0)
    return std::unexpected(diagnostic("sketch.occ.curve-tolerance",
                                      "curve tolerance must be positive"));
  return {};
}

} // namespace

Result<CurveProjection> projectToCurve(const model::Entity &entity,
                                       model::Point2 query,
                                       double toleranceMetres) {
  if (auto valid = validateTolerance(toleranceMetres); !valid)
    return std::unexpected(std::move(valid.error()));
  if (auto valid = model::validate(entity); !valid)
    return std::unexpected(std::move(valid.error()));
  try {
    auto source = curve(entity);
    if (!source)
      return std::unexpected(std::move(source.error()));
    Geom2dAPI_ProjectPointOnCurve projection{point(query), *source};
    if (projection.NbPoints() == 0)
      return std::unexpected(diagnostic("sketch.occ.curve-projection",
                                        "point projection found no curve location"));
    const gp_Pnt2d projected = projection.NearestPoint();
    auto converted = point(projected);
    auto native = parameter(entity, projected,
                            projection.LowerDistanceParameter());
    auto distance = model::LengthValue::fromSi(
        projection.LowerDistance() / millimetresPerMetre);
    if (!converted)
      return std::unexpected(std::move(converted.error()));
    if (!native)
      return std::unexpected(std::move(native.error()));
    if (!distance)
      return std::unexpected(std::move(distance.error()));
    return CurveProjection{{*converted, *native}, *distance};
  } catch (const Standard_Failure &failure) {
    return std::unexpected(diagnostic(
        "sketch.occ.curve-projection-failed",
        failure.GetMessageString() ? failure.GetMessageString()
                                   : "curve projection failed"));
  } catch (const std::bad_alloc &) {
    return std::unexpected(diagnostic("sketch.occ.allocation-failed",
                                      "curve projection allocation failed"));
  }
}

Result<CurveIntersectionSet>
intersectCurves(const model::Entity &first, const model::Entity &second,
                double toleranceMetres) {
  if (auto valid = validateTolerance(toleranceMetres); !valid)
    return std::unexpected(std::move(valid.error()));
  if (auto valid = model::validate(first); !valid)
    return std::unexpected(std::move(valid.error()));
  if (auto valid = model::validate(second); !valid)
    return std::unexpected(std::move(valid.error()));
  if (model::entityId(first) == model::entityId(second))
    return std::unexpected(diagnostic("sketch.occ.same-curve",
                                      "a curve cannot intersect itself here"));
  try {
    auto firstCurve = curve(first);
    auto secondCurve = curve(second);
    if (!firstCurve)
      return std::unexpected(std::move(firstCurve.error()));
    if (!secondCurve)
      return std::unexpected(std::move(secondCurve.error()));
    Geom2dAPI_InterCurveCurve intersection{
        *firstCurve, *secondCurve, toleranceMetres * millimetresPerMetre};
    const auto count = static_cast<std::size_t>(intersection.NbPoints());
    if (count > maximumIntersections)
      return std::unexpected(diagnostic("sketch.occ.too-many-intersections",
                                        "curve intersection result is too large"));
    CurveIntersectionSet result;
    result.overlapping = intersection.NbSegments() > 0;
    result.points.reserve(count);
    for (Standard_Integer index = 1; index <= intersection.NbPoints(); ++index) {
      const IntRes2d_IntersectionPoint &candidate =
          intersection.Intersector().Point(index);
      const gp_Pnt2d &location = candidate.Value();
      auto converted = point(location);
      auto firstParameter =
          parameter(first, location, candidate.ParamOnFirst());
      auto secondParameter =
          parameter(second, location, candidate.ParamOnSecond());
      if (!converted)
        return std::unexpected(std::move(converted.error()));
      if (!firstParameter)
        return std::unexpected(std::move(firstParameter.error()));
      if (!secondParameter)
        return std::unexpected(std::move(secondParameter.error()));
      result.points.push_back(
          {*converted, *firstParameter, *secondParameter});
    }
    std::ranges::sort(result.points, [](const auto &left, const auto &right) {
      if (const double firstOrder =
              scalar(left.firstParameter) - scalar(right.firstParameter);
          firstOrder != 0.0)
        return firstOrder < 0.0;
      return scalar(left.secondParameter) < scalar(right.secondParameter);
    });
    return result;
  } catch (const Standard_Failure &failure) {
    return std::unexpected(diagnostic(
        "sketch.occ.curve-intersection-failed",
        failure.GetMessageString() ? failure.GetMessageString()
                                   : "curve intersection failed"));
  } catch (const std::bad_alloc &) {
    return std::unexpected(diagnostic("sketch.occ.allocation-failed",
                                      "curve intersection allocation failed"));
  }
}

namespace {

struct TrimCut {
  double parameter = 0.0;
  model::Point2 point;
  SketchEntityId curve;
};

struct TrimSegment {
  double lower = 0.0;
  double upper = 0.0;
  std::optional<TrimCut> lowerBoundary;
  std::optional<TrimCut> upperBoundary;
};

struct TrimPlan {
  model::Entity source;
  std::vector<TrimSegment> retained;
  std::vector<TrimBoundary> boundaries;
};

struct SplitPlan {
  model::Entity source;
  model::Point2 point;
  std::vector<std::pair<double, double>> segments;
};

Result<SplitPlan> planSplit(const model::Definition &current,
                            model::CurvePick curve,
                            const model::NumericalProfile &profile) {
  if (auto valid = model::validate(current, profile); !valid)
    return std::unexpected(std::move(valid.error()));
  const auto found = std::ranges::find(current.entities, curve.entity,
                                       model::entityId);
  if (found == current.entities.end())
    return std::unexpected(
        diagnostic("sketch.split.curve-missing", "Split curve is missing"));
  if (std::holds_alternative<model::PointEntity>(*found))
    return std::unexpected(diagnostic("sketch.split.not-curve",
                                      "Split requires curve geometry"));
  auto projected =
      projectToCurve(*found, curve.reference, profile.lengthToleranceMeters);
  auto domain = parameterDomain(*found);
  if (!projected)
    return std::unexpected(std::move(projected.error()));
  if (!domain)
    return std::unexpected(std::move(domain.error()));
  const double split = scalar(projected->location.parameter);
  const double tolerance = parameterTolerance(*found, profile);
  if (!domain->closed &&
      (std::abs(split - domain->lower) <= tolerance ||
       std::abs(split - domain->upper) <= tolerance))
    return std::unexpected(diagnostic(
        "sketch.split.endpoint", "Choose a point away from the curve endpoints"));

  SplitPlan result{*found, projected->location.point, {}};
  if (domain->closed) {
    result.segments.push_back(
        {split, split + domain->upper - domain->lower});
  } else if (positiveOrientation(*found)) {
    result.segments.push_back({domain->lower, split});
    result.segments.push_back({split, domain->upper});
  } else {
    result.segments.push_back({split, domain->upper});
    result.segments.push_back({domain->lower, split});
  }
  return result;
}

Result<TrimPlan> planTrim(const model::Definition &current,
                          model::CurvePick curve,
                          const model::NumericalProfile &profile) {
  if (auto valid = model::validate(current, profile); !valid)
    return std::unexpected(std::move(valid.error()));
  if (current.entities.size() > maximumTrimCurves)
    return std::unexpected(diagnostic("sketch.trim.curve-limit",
                                      "Sketch has too many Trim candidates"));
  const auto found = std::ranges::find(current.entities, curve.entity,
                                       model::entityId);
  if (found == current.entities.end())
    return std::unexpected(
        diagnostic("sketch.trim.curve-missing", "Trim curve is missing"));
  if (std::holds_alternative<model::PointEntity>(*found))
    return std::unexpected(diagnostic("sketch.trim.not-curve",
                                      "Trim requires curve geometry"));

  auto projected =
      projectToCurve(*found, curve.reference, profile.lengthToleranceMeters);
  auto domain = parameterDomain(*found);
  if (!projected)
    return std::unexpected(std::move(projected.error()));
  if (!domain)
    return std::unexpected(std::move(domain.error()));
  const double tolerance = parameterTolerance(*found, profile);

  std::vector<TrimCut> cuts;
  for (const model::Entity &candidate : current.entities) {
    if (model::entityId(candidate) == curve.entity ||
        std::holds_alternative<model::PointEntity>(candidate))
      continue;
    auto intersections =
        intersectCurves(*found, candidate, profile.lengthToleranceMeters);
    if (!intersections)
      return std::unexpected(std::move(intersections.error()));
    if (intersections->overlapping)
      return std::unexpected(
          diagnostic("sketch.trim.overlapping-boundary",
                     "Trim curve overlaps another Sketch curve"));
    if (cuts.size() + intersections->points.size() > maximumTrimCuts)
      return std::unexpected(diagnostic(
          "sketch.trim.cut-limit", "Trim found too many curve boundaries"));
    for (const CurveIntersection &intersection : intersections->points) {
      const double native = scalar(intersection.firstParameter);
      if (!domain->closed &&
          (std::abs(native - domain->lower) <= tolerance ||
           std::abs(native - domain->upper) <= tolerance))
        continue;
      cuts.push_back(
          {native, intersection.point, model::entityId(candidate)});
    }
  }

  std::ranges::sort(cuts, [](const TrimCut &left, const TrimCut &right) {
    if (left.parameter != right.parameter)
      return left.parameter < right.parameter;
    return left.curve < right.curve;
  });
  std::vector<TrimCut> uniqueCuts;
  uniqueCuts.reserve(cuts.size());
  for (const TrimCut &cut : cuts)
    if (uniqueCuts.empty() ||
        (std::abs(cut.parameter - uniqueCuts.back().parameter) > tolerance &&
         pointDistance(cut.point, uniqueCuts.back().point) >
             profile.lengthToleranceMeters))
      uniqueCuts.push_back(cut);
  if (domain->closed && uniqueCuts.size() > 1U &&
      pointDistance(uniqueCuts.front().point, uniqueCuts.back().point) <=
          profile.lengthToleranceMeters)
    uniqueCuts.pop_back();
  if (std::ranges::any_of(uniqueCuts, [&](const TrimCut &cut) {
        return pointDistance(cut.point, projected->location.point) <=
               profile.lengthToleranceMeters;
      }))
    return std::unexpected(
        diagnostic("sketch.trim.ambiguous-pick",
                   "Choose a curve segment away from an intersection"));

  TrimPlan result{*found, {}, {}};
  double selected = scalar(projected->location.parameter);
  if (uniqueCuts.empty())
    return result;
  if (domain->closed) {
    if (uniqueCuts.size() < 2U)
      return std::unexpected(
          diagnostic("sketch.trim.insufficient-boundaries",
                     "A closed curve needs two distinct Trim boundaries"));
    const double period = domain->upper - domain->lower;
    while (selected < domain->lower)
      selected += period;
    while (selected >= domain->upper)
      selected -= period;
    const auto next = std::ranges::upper_bound(
        uniqueCuts, selected, {}, &TrimCut::parameter);
    const TrimCut &nextCut =
        next == uniqueCuts.end() ? uniqueCuts.front() : *next;
    const TrimCut &previousCut =
        next == uniqueCuts.begin() ? uniqueCuts.back() : *std::prev(next);
    const double lower = next == uniqueCuts.end()
                             ? nextCut.parameter + period
                             : nextCut.parameter;
    const double previous = next == uniqueCuts.begin()
                                ? previousCut.parameter - period
                                : previousCut.parameter;
    result.retained.push_back(
        {lower, previous + period, nextCut, previousCut});
  } else {
    const auto next = std::ranges::lower_bound(
        uniqueCuts, selected, {}, &TrimCut::parameter);
    if (next != uniqueCuts.end() &&
        std::abs(next->parameter - selected) <= tolerance)
      return std::unexpected(
          diagnostic("sketch.trim.ambiguous-pick",
                     "Choose a curve segment away from an intersection"));
    if (next != uniqueCuts.begin()) {
      const TrimCut &previous = *std::prev(next);
      result.retained.push_back(
          {domain->lower, previous.parameter, std::nullopt, previous});
    }
    if (next != uniqueCuts.end())
      result.retained.push_back(
          {next->parameter, domain->upper, *next, std::nullopt});
    if (!positiveOrientation(*found))
      std::ranges::reverse(result.retained);
  }
  for (const TrimSegment &segment : result.retained) {
    if (segment.lowerBoundary)
      result.boundaries.push_back(
          {segment.lowerBoundary->point, segment.lowerBoundary->curve});
    if (segment.upperBoundary)
      result.boundaries.push_back(
          {segment.upperBoundary->point, segment.upperBoundary->curve});
  }
  return result;
}

Diagnostic trimFailure(const Standard_Failure &failure) {
  return diagnostic("sketch.trim.occ-failure",
                    failure.GetMessageString()
                        ? failure.GetMessageString()
                        : "Trim geometry operation failed");
}

Diagnostic splitFailure(const Standard_Failure &failure) {
  return diagnostic("sketch.split.occ-failure",
                    failure.GetMessageString()
                        ? failure.GetMessageString()
                        : "Split geometry operation failed");
}

} // namespace

Result<TrimPreview> previewTrim(const model::Definition &current,
                               model::CurvePick curve,
                               const model::NumericalProfile &profile) {
  try {
    auto planned = planTrim(current, curve, profile);
    if (!planned)
      return std::unexpected(std::move(planned.error()));
    return TrimPreview{std::move(planned->boundaries),
                       planned->retained.empty()};
  } catch (const Standard_Failure &failure) {
    return std::unexpected(trimFailure(failure));
  } catch (const std::bad_alloc &) {
    return std::unexpected(diagnostic("sketch.trim.allocation-failed",
                                      "Trim geometry allocation failed"));
  }
}

Result<model::AppliedEdits>
trimCurve(const model::Definition &current, const TrimRequest &request,
          const model::NumericalProfile &profile) {
  try {
    auto planned = planTrim(current, request.curve, profile);
    if (!planned)
      return std::unexpected(std::move(planned.error()));
    std::vector<model::Entity> retained;
    std::vector<model::PointOnObject> boundaries;
    retained.reserve(planned->retained.size());
    boundaries.reserve(planned->boundaries.size());
    const bool forward = positiveOrientation(planned->source);
    std::size_t constraint = 0U;
    for (std::size_t index = 0U; index < planned->retained.size(); ++index) {
      const SketchEntityId output =
          index == 0U ? request.curve.entity
                      : request.identities.splitEntity;
      const TrimSegment &plannedSegment = planned->retained[index];
      auto segment = curveSegment(planned->source, output,
                                  plannedSegment.lower, plannedSegment.upper);
      if (!segment)
        return std::unexpected(std::move(segment.error()));
      retained.push_back(std::move(*segment));
      const auto appendBoundary = [&](const std::optional<TrimCut> &cut,
                                      model::PointKey key) {
        if (!cut)
          return;
        boundaries.push_back(
            {request.identities.boundaryConstraints[constraint++],
             {output, key}, cut->curve});
      };
      appendBoundary(plannedSegment.lowerBoundary,
                     forward ? model::PointKey::Start : model::PointKey::End);
      appendBoundary(plannedSegment.upperBoundary,
                     forward ? model::PointKey::End : model::PointKey::Start);
    }
    return model::trimCurve(
        current,
        {request.curve, std::move(retained), std::move(boundaries),
         request.constraints},
        profile);
  } catch (const Standard_Failure &failure) {
    return std::unexpected(trimFailure(failure));
  } catch (const std::bad_alloc &) {
    return std::unexpected(diagnostic("sketch.trim.allocation-failed",
                                      "Trim geometry allocation failed"));
  }
}

Result<SplitPreview> previewSplit(const model::Definition &current,
                                  model::CurvePick curve,
                                  const model::NumericalProfile &profile) {
  try {
    auto planned = planSplit(current, curve, profile);
    if (!planned)
      return std::unexpected(std::move(planned.error()));
    return SplitPreview{planned->point};
  } catch (const Standard_Failure &failure) {
    return std::unexpected(splitFailure(failure));
  } catch (const std::bad_alloc &) {
    return std::unexpected(diagnostic("sketch.split.allocation-failed",
                                      "Split geometry allocation failed"));
  }
}

Result<model::AppliedEdits>
splitCurve(const model::Definition &current, const SplitRequest &request,
           const model::NumericalProfile &profile) {
  try {
    auto planned = planSplit(current, request.curve, profile);
    if (!planned)
      return std::unexpected(std::move(planned.error()));
    std::vector<model::Entity> segments;
    segments.reserve(planned->segments.size());
    for (std::size_t index = 0U; index < planned->segments.size(); ++index) {
      const auto [lower, upper] = planned->segments[index];
      auto segment = curveSegment(
          planned->source,
          index == 0U ? request.curve.entity
                      : request.identities.secondSegment,
          lower, upper);
      if (!segment)
        return std::unexpected(std::move(segment.error()));
      segments.push_back(std::move(*segment));
    }
    const model::PointRef first{request.curve.entity, model::PointKey::End};
    const model::PointRef second{model::entityId(segments.back()),
                                 model::PointKey::Start};
    return model::splitCurve(
        current,
        {request.curve, std::move(segments),
         {request.identities.seamConstraint, first, second},
         request.constraints},
        profile);
  } catch (const Standard_Failure &failure) {
    return std::unexpected(splitFailure(failure));
  } catch (const std::bad_alloc &) {
    return std::unexpected(diagnostic("sketch.split.allocation-failed",
                                      "Split geometry allocation failed"));
  }
}

Result<model::AppliedEdits>
joinCurves(const model::Definition &current, const JoinRequest &request,
           const model::NumericalProfile &profile) {
  try {
    if (auto valid = model::validate(current, profile); !valid)
      return std::unexpected(std::move(valid.error()));
    const bool endpoints =
        (request.first.key == model::PointKey::Start ||
         request.first.key == model::PointKey::End) &&
        (request.second.key == model::PointKey::Start ||
         request.second.key == model::PointKey::End);
    const auto first = std::ranges::find(current.entities,
                                         request.first.entity,
                                         model::entityId);
    const auto second = std::ranges::find(current.entities,
                                          request.second.entity,
                                          model::entityId);
    if (!endpoints || request.first.entity == request.second.entity ||
        first == current.entities.end() || second == current.entities.end() ||
        std::holds_alternative<model::PointEntity>(*first) ||
        std::holds_alternative<model::PointEntity>(*second))
      return std::unexpected(
          diagnostic("sketch.join.input", "Join input is invalid"));
    auto firstDomain = parameterDomain(*first);
    auto secondDomain = parameterDomain(*second);
    auto firstPoint = model::resolvePoint(current, request.first);
    auto secondPoint = model::resolvePoint(current, request.second);
    if (!firstDomain || !secondDomain)
      return std::unexpected(!firstDomain ? std::move(firstDomain.error())
                                          : std::move(secondDomain.error()));
    if (firstDomain->closed || secondDomain->closed)
      return std::unexpected(diagnostic(
          "sketch.join.closed-curve", "Join requires two open curves"));
    if (!firstPoint || !secondPoint ||
        pointDistance(*firstPoint, *secondPoint) >
            profile.lengthToleranceMeters)
      return std::unexpected(diagnostic(
          "sketch.join.disconnected", "Join endpoints must be coincident"));
    if (construction(*first) != construction(*second))
      return std::unexpected(diagnostic(
          "sketch.join.construction", "Join curves must use the same type"));

    const auto oriented = [&](const model::Entity &entity, bool reverse)
        -> Result<Handle(Geom2d_BoundedCurve)> {
      auto converted = curve(entity);
      if (!converted)
        return std::unexpected(std::move(converted.error()));
      Handle(Geom2d_BoundedCurve) bounded =
          Handle(Geom2d_BoundedCurve)::DownCast((*converted)->Copy());
      if (bounded.IsNull())
        return std::unexpected(diagnostic(
            "sketch.join.unbounded-curve", "Join requires bounded curves"));
      if (reverse)
        bounded->Reverse();
      return bounded;
    };
    auto firstCurve =
        oriented(*first, request.first.key == model::PointKey::Start);
    auto secondCurve =
        oriented(*second, request.second.key == model::PointKey::End);
    if (!firstCurve || !secondCurve)
      return std::unexpected(!firstCurve ? std::move(firstCurve.error())
                                         : std::move(secondCurve.error()));
    Geom2dConvert_CompCurveToBSplineCurve joined{*firstCurve};
    if (!joined.Add(*secondCurve,
                    profile.lengthToleranceMeters * millimetresPerMetre))
      return std::unexpected(diagnostic(
          "sketch.join.continuity", "Curves do not form a continuous Join"));
    auto result = exportBSpline(joined.BSplineCurve(), request.first.entity,
                                construction(*first));
    if (!result)
      return std::unexpected(std::move(result.error()));
    return model::joinCurves(
        current,
        {request.first, request.second, std::move(*result), request.object,
         request.constraints},
        profile);
  } catch (const Standard_Failure &failure) {
    return std::unexpected(diagnostic(
        "sketch.join.occ-failure",
        failure.GetMessageString() ? failure.GetMessageString()
                                   : "Join geometry operation failed"));
  } catch (const std::bad_alloc &) {
    return std::unexpected(diagnostic("sketch.join.allocation-failed",
                                      "Join geometry allocation failed"));
  }
}

Result<model::AppliedEdits>
convertToNurbs(const model::Definition &current,
               const ConvertToNurbsRequest &request,
               const model::NumericalProfile &profile) {
  try {
    if (auto valid = model::validate(current, profile); !valid)
      return std::unexpected(std::move(valid.error()));
    const auto found = std::ranges::find(current.entities, request.curve,
                                         model::entityId);
    if (found == current.entities.end() ||
        std::holds_alternative<model::PointEntity>(*found) ||
        std::holds_alternative<model::BSplineEntity>(*found))
      return std::unexpected(diagnostic(
          "sketch.convert-to-nurbs.input",
          "Convert to NURBS requires one compatible analytic curve"));
    auto source = curve(*found);
    if (!source)
      return std::unexpected(std::move(source.error()));
    Handle(Geom2d_BSplineCurve) converted =
        Geom2dConvert::CurveToBSplineCurve(*source, Convert_TgtThetaOver2);
    auto result = exportBSpline(converted, request.curve, construction(*found));
    if (!result)
      return std::unexpected(std::move(result.error()));
    return model::convertToNurbs(
        current, {std::move(*result), request.constraints}, profile);
  } catch (const Standard_Failure &failure) {
    return std::unexpected(diagnostic(
        "sketch.convert-to-nurbs.occ-failure",
        failure.GetMessageString() ? failure.GetMessageString()
                                   : "NURBS conversion failed"));
  } catch (const std::bad_alloc &) {
    return std::unexpected(diagnostic(
        "sketch.convert-to-nurbs.allocation-failed",
        "NURBS conversion allocation failed"));
  }
}

} // namespace kearne::adapters
