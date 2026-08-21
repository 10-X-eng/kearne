#include <kearne/adapters/occ_bspline.hpp>

#include <GeomAPI_Interpolate.hxx>
#include <Geom_BSplineCurve.hxx>
#include <GeomConvert_ApproxCurve.hxx>
#include <GeomAbs_Shape.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>
#include <TColStd_Array1OfInteger.hxx>
#include <TColStd_Array1OfReal.hxx>
#include <TColgp_Array1OfPnt.hxx>
#include <TColgp_HArray1OfPnt.hxx>
#include <gp_Pnt.hxx>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <utility>
#include <vector>

namespace kearne::adapters {
namespace {

namespace model = sketch;
constexpr double millimetresPerMetre = 1'000.0;

Result<void> validateRequest(std::span<const model::Point2> points,
                             BSplineCreation creation, std::uint32_t degree,
                             bool periodic, double tolerance) {
  if (points.size() < 2U || points.size() > 1'024U)
    return std::unexpected(diagnostic("sketch.bspline.invalid-point-count",
                                      "B-spline point count is invalid"));
  if (degree == 0U || degree > 25U)
    return std::unexpected(diagnostic("sketch.bspline.invalid-degree",
                                      "B-spline degree is invalid"));
  const auto creationValue = static_cast<std::uint8_t>(creation);
  if (creationValue <
          static_cast<std::uint8_t>(BSplineCreation::ControlPoints) ||
      creationValue > static_cast<std::uint8_t>(BSplineCreation::Interpolation))
    return std::unexpected(diagnostic("sketch.bspline.invalid-creation",
                                      "B-spline creation mode is invalid"));
  if (!std::isfinite(tolerance) || tolerance <= 0.0)
    return std::unexpected(
        diagnostic("sketch.bspline.invalid-tolerance",
                   "B-spline interpolation tolerance is invalid"));
  for (std::size_t index = 1U; index < points.size(); ++index)
    if (points[index] == points[index - 1U])
      return std::unexpected(
          diagnostic("sketch.bspline.duplicate-adjacent-point",
                     "adjacent B-spline points must differ"));
  if (periodic && points.front() == points.back())
    return std::unexpected(
        diagnostic("sketch.bspline.duplicate-periodic-closure",
                   "periodic B-spline input must not repeat its first point"));
  return {};
}

Handle(Geom_BSplineCurve)
    controlPointCurve(std::span<const model::Point2> points,
                      std::uint32_t requestedDegree, bool periodic) {
  const Standard_Integer count = static_cast<Standard_Integer>(points.size());
  const auto maximumDegree = static_cast<std::uint32_t>(points.size() - 1U);
  const Standard_Integer degree =
      static_cast<Standard_Integer>(std::min(requestedDegree, maximumDegree));
  TColgp_Array1OfPnt poles(1, count);
  for (Standard_Integer index = 1; index <= count; ++index) {
    const model::Point2 &point = points[static_cast<std::size_t>(index - 1)];
    poles.SetValue(index, gp_Pnt(point.x.si() * millimetresPerMetre,
                                 point.y.si() * millimetresPerMetre, 0.0));
  }
  if (periodic) {
    TColStd_Array1OfReal knots(1, count + 1);
    TColStd_Array1OfInteger multiplicities(1, count + 1);
    for (Standard_Integer index = 1; index <= count + 1; ++index) {
      knots.SetValue(index, static_cast<double>(index - 1));
      multiplicities.SetValue(index, 1);
    }
    return new Geom_BSplineCurve(poles, knots, multiplicities, degree,
                                 Standard_True);
  }

  const Standard_Integer knotCount = count - degree + 1;
  TColStd_Array1OfReal knots(1, knotCount);
  TColStd_Array1OfInteger multiplicities(1, knotCount);
  for (Standard_Integer index = 1; index <= knotCount; ++index) {
    knots.SetValue(index, static_cast<double>(index - 1));
    multiplicities.SetValue(index,
                            index == 1 || index == knotCount ? degree + 1 : 1);
  }
  return new Geom_BSplineCurve(poles, knots, multiplicities, degree,
                               Standard_False);
}

Handle(Geom_BSplineCurve)
    interpolatedCurve(std::span<const model::Point2> input, bool periodic,
                      double tolerance) {
  const Standard_Integer count = static_cast<Standard_Integer>(input.size());
  Handle(TColgp_HArray1OfPnt) points = new TColgp_HArray1OfPnt(1, count);
  for (Standard_Integer index = 1; index <= count; ++index) {
    const model::Point2 &point = input[static_cast<std::size_t>(index - 1)];
    points->SetValue(index, gp_Pnt(point.x.si() * millimetresPerMetre,
                                   point.y.si() * millimetresPerMetre, 0.0));
  }
  GeomAPI_Interpolate interpolation(points, periodic,
                                    tolerance * millimetresPerMetre);
  interpolation.Perform();
  return interpolation.Curve();
}

Result<CanonicalBSpline>
canonicalGeometry(bool periodic, const Handle(Geom_BSplineCurve) & curve) {
  if (curve.IsNull())
    return std::unexpected(diagnostic("sketch.bspline.construction-failed",
                                      "B-spline construction failed"));
  Handle(Geom_BSplineCurve) canonical =
      Handle(Geom_BSplineCurve)::DownCast(curve->Copy());
  if (canonical.IsNull())
    return std::unexpected(diagnostic("sketch.bspline.copy-failed",
                                      "B-spline canonicalization failed"));
  if (canonical->IsPeriodic())
    canonical->SetNotPeriodic();

  std::vector<model::Point2> controlPoints;
  std::vector<model::DimensionlessValue> knots;
  std::vector<model::DimensionlessValue> weights;
  controlPoints.reserve(static_cast<std::size_t>(canonical->NbPoles()));
  weights.reserve(static_cast<std::size_t>(canonical->NbPoles()));
  for (Standard_Integer index = 1; index <= canonical->NbPoles(); ++index) {
    const gp_Pnt point = canonical->Pole(index);
    auto x = model::LengthValue::fromSi(point.X() / millimetresPerMetre);
    auto y = model::LengthValue::fromSi(point.Y() / millimetresPerMetre);
    auto weight = model::DimensionlessValue::fromSi(canonical->Weight(index));
    if (!x || !y || !weight || std::abs(point.Z()) > 1.0e-9)
      return std::unexpected(
          diagnostic("sketch.bspline.non-planar-result",
                     "B-spline construction produced invalid plane geometry"));
    controlPoints.push_back({*x, *y});
    weights.push_back(*weight);
  }
  for (Standard_Integer index = 1; index <= canonical->NbKnots(); ++index) {
    auto knot = model::DimensionlessValue::fromSi(canonical->Knot(index));
    if (!knot)
      return std::unexpected(
          diagnostic("sketch.bspline.invalid-result-knot",
                     "B-spline construction produced an invalid knot"));
    for (Standard_Integer repeat = 0; repeat < canonical->Multiplicity(index);
         ++repeat)
      knots.push_back(*knot);
  }
  return CanonicalBSpline{
      std::move(controlPoints), std::move(knots), std::move(weights),
      static_cast<std::uint32_t>(canonical->Degree()), periodic};
}

Result<Handle(Geom_BSplineCurve)>
occCurve(const model::BSplineEntity &entity) {
  if (entity.controlPoints.size() < 2U ||
      entity.controlPoints.size() != entity.weights.size() ||
      entity.knots.size() !=
          entity.controlPoints.size() + entity.degree + 1U ||
      entity.degree == 0U || entity.degree > 25U ||
      entity.controlPoints.size() >
          static_cast<std::size_t>(std::numeric_limits<Standard_Integer>::max()))
    return std::unexpected(diagnostic("sketch.bspline.invalid-edit-curve",
                                      "B-spline edit target is invalid"));

  const Standard_Integer poleCount =
      static_cast<Standard_Integer>(entity.controlPoints.size());
  TColgp_Array1OfPnt poles(1, poleCount);
  TColStd_Array1OfReal weights(1, poleCount);
  for (Standard_Integer index = 1; index <= poleCount; ++index) {
    const std::size_t source = static_cast<std::size_t>(index - 1);
    const model::Point2 &point = entity.controlPoints[source];
    const double weight = entity.weights[source].si();
    if (!std::isfinite(point.x.si()) || !std::isfinite(point.y.si()) ||
        !std::isfinite(weight) || weight <= 0.0)
      return std::unexpected(diagnostic("sketch.bspline.invalid-edit-curve",
                                        "B-spline edit target is invalid"));
    poles.SetValue(index, gp_Pnt(point.x.si() * millimetresPerMetre,
                                 point.y.si() * millimetresPerMetre, 0.0));
    weights.SetValue(index, weight);
  }

  std::vector<double> uniqueKnots;
  std::vector<Standard_Integer> multiplicities;
  uniqueKnots.reserve(entity.knots.size());
  multiplicities.reserve(entity.knots.size());
  for (const model::DimensionlessValue knot : entity.knots) {
    const double value = knot.si();
    if (!std::isfinite(value) ||
        (!uniqueKnots.empty() && value < uniqueKnots.back()))
      return std::unexpected(diagnostic("sketch.bspline.invalid-edit-curve",
                                        "B-spline edit target is invalid"));
    if (!uniqueKnots.empty() && value == uniqueKnots.back()) {
      ++multiplicities.back();
    } else {
      uniqueKnots.push_back(value);
      multiplicities.push_back(1);
    }
  }
  if (uniqueKnots.size() < 2U ||
      uniqueKnots.size() >
          static_cast<std::size_t>(std::numeric_limits<Standard_Integer>::max()))
    return std::unexpected(diagnostic("sketch.bspline.invalid-edit-curve",
                                      "B-spline edit target is invalid"));
  const Standard_Integer knotCount =
      static_cast<Standard_Integer>(uniqueKnots.size());
  TColStd_Array1OfReal knots(1, knotCount);
  TColStd_Array1OfInteger mults(1, knotCount);
  for (Standard_Integer index = 1; index <= knotCount; ++index) {
    const std::size_t source = static_cast<std::size_t>(index - 1);
    knots.SetValue(index, uniqueKnots[source]);
    mults.SetValue(index, multiplicities[source]);
  }
  return Handle(Geom_BSplineCurve){new Geom_BSplineCurve(
      poles, weights, knots, mults, static_cast<Standard_Integer>(entity.degree),
      Standard_False)};
}

Result<model::BSplineEntity>
editedEntity(const model::BSplineEntity &source,
             const Handle(Geom_BSplineCurve) & curve) {
  auto geometry = canonicalGeometry(source.periodic, curve);
  if (!geometry)
    return std::unexpected(std::move(geometry.error()));
  return model::BSplineEntity{source.id,
                              std::move(geometry->controlPoints),
                              std::move(geometry->knots),
                              std::move(geometry->weights),
                              geometry->degree,
                              geometry->periodic,
                              source.construction};
}

} // namespace

Result<CanonicalBSpline>
createBSplineGeometry(std::span<const model::Point2> points,
                      BSplineCreation creation, std::uint32_t degree,
                      bool periodic, double interpolationToleranceMetres) {
  if (auto valid = validateRequest(points, creation, degree, periodic,
                                   interpolationToleranceMetres);
      !valid)
    return std::unexpected(std::move(valid.error()));
  try {
    const Handle(Geom_BSplineCurve) curve =
        creation == BSplineCreation::ControlPoints
            ? controlPointCurve(points, degree, periodic)
            : interpolatedCurve(points, periodic, interpolationToleranceMetres);
    return canonicalGeometry(periodic, curve);
  } catch (const Standard_Failure &failure) {
    return std::unexpected(
        diagnostic("sketch.bspline.occ-failure",
                   failure.GetMessageString()
                       ? failure.GetMessageString()
                       : "OpenCASCADE B-spline construction failed"));
  } catch (const std::bad_alloc &) {
    return std::unexpected(
        diagnostic("sketch.bspline.allocation-failed",
                   "B-spline construction allocation failed"));
  }
}

Result<model::BSplineEntity> createBSpline(const BSplineRequest &request) {
  auto geometry = createBSplineGeometry(request.points, request.creation,
                                        request.degree, request.periodic,
                                        request.interpolationToleranceMetres);
  if (!geometry)
    return std::unexpected(std::move(geometry.error()));
  return model::BSplineEntity{request.id,
                              std::move(geometry->controlPoints),
                              std::move(geometry->knots),
                              std::move(geometry->weights),
                              geometry->degree,
                              geometry->periodic,
                              request.construction};
}

Result<model::BSplineEntity> editBSpline(const BSplineEditRequest &request) {
  try {
    const auto editValue = static_cast<std::uint8_t>(request.edit);
    if (editValue < static_cast<std::uint8_t>(BSplineEdit::IncreaseDegree) ||
        editValue > static_cast<std::uint8_t>(BSplineEdit::SetPoleWeight))
      return std::unexpected(diagnostic("sketch.bspline.invalid-edit",
                                        "B-spline edit kind is invalid"));
    auto converted = occCurve(request.entity);
    if (!converted)
      return std::unexpected(std::move(converted.error()));
    Handle(Geom_BSplineCurve) curve = std::move(*converted);
    switch (request.edit) {
    case BSplineEdit::IncreaseDegree:
      if (curve->Degree() >= 25)
        return std::unexpected(
            diagnostic("sketch.bspline.maximum-degree",
                       "B-spline degree is already at its maximum"));
      curve->IncreaseDegree(curve->Degree() + 1);
      break;
    case BSplineEdit::DecreaseDegree: {
      if (curve->Degree() <= 1)
        return std::unexpected(
            diagnostic("sketch.bspline.minimum-degree",
                       "B-spline degree is already at its minimum"));
      if (!std::isfinite(request.maximumDeviationMetres) ||
          request.maximumDeviationMetres < 0.0)
        return std::unexpected(
            diagnostic("sketch.bspline.invalid-deviation",
                       "B-spline maximum deviation is invalid"));
      const double tolerance =
          std::max(request.maximumDeviationMetres * millimetresPerMetre,
                   Precision::Confusion());
      GeomConvert_ApproxCurve approximation(curve, tolerance, GeomAbs_C0,
                                            1'024, curve->Degree() - 1);
      if (!approximation.IsDone() || approximation.Curve().IsNull() ||
          approximation.MaxError() > tolerance)
        return std::unexpected(
            diagnostic("sketch.bspline.degree-reduction-failed",
                       "B-spline cannot be reduced within the requested deviation"));
      curve = approximation.Curve();
      break;
    }
    case BSplineEdit::IncreaseKnotMultiplicity: {
      if (request.index >= static_cast<std::size_t>(curve->NbKnots()))
        return std::unexpected(diagnostic("sketch.bspline.invalid-knot-index",
                                          "B-spline knot index is invalid"));
      const Standard_Integer index =
          static_cast<Standard_Integer>(request.index + 1U);
      const Standard_Integer multiplicity = curve->Multiplicity(index);
      if (multiplicity >= curve->Degree() || index == 1 ||
          index == curve->NbKnots())
        return std::unexpected(
            diagnostic("sketch.bspline.maximum-knot-multiplicity",
                       "B-spline knot multiplicity cannot be increased"));
      curve->IncreaseMultiplicity(index, multiplicity + 1);
      break;
    }
    case BSplineEdit::DecreaseKnotMultiplicity: {
      if (request.index >= static_cast<std::size_t>(curve->NbKnots()))
        return std::unexpected(diagnostic("sketch.bspline.invalid-knot-index",
                                          "B-spline knot index is invalid"));
      if (!std::isfinite(request.maximumDeviationMetres) ||
          request.maximumDeviationMetres < 0.0)
        return std::unexpected(
            diagnostic("sketch.bspline.invalid-deviation",
                       "B-spline maximum deviation is invalid"));
      const Standard_Integer index =
          static_cast<Standard_Integer>(request.index + 1U);
      const Standard_Integer multiplicity = curve->Multiplicity(index);
      if (multiplicity <= 1 || index == 1 || index == curve->NbKnots() ||
          !curve->RemoveKnot(
              index, multiplicity - 1,
              request.maximumDeviationMetres * millimetresPerMetre))
        return std::unexpected(
            diagnostic("sketch.bspline.knot-removal-failed",
                       "B-spline knot cannot be removed within the requested deviation"));
      break;
    }
    case BSplineEdit::InsertKnot:
      if (!std::isfinite(request.value) ||
          request.value <= curve->FirstParameter() ||
          request.value >= curve->LastParameter())
        return std::unexpected(diagnostic("sketch.bspline.invalid-knot",
                                          "B-spline knot parameter is invalid"));
      curve->InsertKnot(request.value, 1, Precision::PConfusion(),
                        Standard_True);
      break;
    case BSplineEdit::SetPoleWeight:
      if (request.index >= static_cast<std::size_t>(curve->NbPoles()) ||
          !std::isfinite(request.value) || request.value <= 0.0)
        return std::unexpected(diagnostic("sketch.bspline.invalid-pole-weight",
                                          "B-spline pole weight is invalid"));
      curve->SetWeight(static_cast<Standard_Integer>(request.index + 1U),
                       request.value);
      break;
    }
    return editedEntity(request.entity, curve);
  } catch (const Standard_Failure &failure) {
    return std::unexpected(
        diagnostic("sketch.bspline.occ-edit-failure",
                   failure.GetMessageString()
                       ? failure.GetMessageString()
                       : "OpenCASCADE B-spline edit failed"));
  } catch (const std::bad_alloc &) {
    return std::unexpected(
        diagnostic("sketch.bspline.allocation-failed",
                   "B-spline editing allocation failed"));
  }
}

} // namespace kearne::adapters
