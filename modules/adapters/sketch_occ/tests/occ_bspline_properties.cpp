#include <kearne/adapters/occ_bspline.hpp>
#include <kearne/sketch/nurbs.hpp>
#include <kearne/testkit/property.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace model = kearne::sketch;
using kearne::ContentDigest;
using kearne::Length;
using kearne::Quantity;
using kearne::SketchEntityId;
using kearne::adapters::BSplineCreation;
using kearne::adapters::BSplineEdit;
using kearne::adapters::BSplineEditRequest;
using kearne::adapters::BSplineRequest;
using kearne::adapters::createBSpline;
using kearne::adapters::editBSpline;
using kearne::testkit::checkProperty;
using kearne::testkit::PropertyProfile;
using kearne::testkit::Random;

void require(bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}

template <typename Id> Id id(std::uint64_t value) {
  typename Id::RandomTail random{};
  for (std::size_t index = 0U; index < random.size(); ++index)
    random[index] = static_cast<std::uint8_t>(value >> ((index % 8U) * 8U));
  auto result = Id::create(value & ((std::uint64_t{1} << 48U) - 1U), random);
  if (!result)
    throw std::runtime_error("could not create test ID");
  return *result;
}

ContentDigest digest(std::uint64_t value) {
  ContentDigest::Bytes bytes{};
  for (std::size_t index = 0U; index < bytes.size(); ++index)
    bytes[index] = static_cast<std::uint8_t>(value + index * 17U);
  auto result = ContentDigest::fromBytes("blake3-256", bytes);
  if (!result)
    throw std::runtime_error("could not create test digest");
  return *result;
}

model::LengthValue length(double value) {
  auto result = Quantity<Length>::fromSi(value);
  if (!result)
    throw std::runtime_error("invalid test length");
  return *result;
}

model::Point2 point(double x, double y) { return {length(x), length(y)}; }

model::NurbsView view(const model::BSplineEntity &spline,
                      std::vector<double> &coordinates,
                      std::vector<double> &knots,
                      std::vector<double> &weights) {
  coordinates.clear();
  knots.clear();
  weights.clear();
  for (const model::Point2 &pole : spline.controlPoints) {
    coordinates.push_back(pole.x.si());
    coordinates.push_back(pole.y.si());
  }
  for (const model::DimensionlessValue knot : spline.knots)
    knots.push_back(knot.si());
  for (const model::DimensionlessValue weight : spline.weights)
    weights.push_back(weight.si());
  return {coordinates, knots, weights, spline.degree};
}

void verifyAllCreationModes(const PropertyProfile &profile) {
  checkProperty(
      "all four exact B-spline creation modes", profile,
      [](Random &random, std::uint64_t index) {
        const double x = random.between(-100.0, 100.0);
        const double y = random.between(-100.0, 100.0);
        const double extent = random.between(0.01, 10.0);
        const std::vector<model::Point2> input{
            point(x, y), point(x + extent, y + extent * 0.7),
            point(x + extent * 2.0, y - extent * 0.4),
            point(x + extent * 3.0, y + extent),
            point(x + extent * 4.0, y + extent * 0.1)};
        for (const BSplineCreation creation :
             {BSplineCreation::ControlPoints, BSplineCreation::Interpolation}) {
          for (const bool periodic : {false, true}) {
            BSplineRequest request{
                id<SketchEntityId>(10'000U + index * 4U +
                                   static_cast<std::uint64_t>(creation) * 2U +
                                   (periodic ? 1U : 0U)),
                input,
                creation,
                3U,
                periodic,
                false,
                1.0e-8};
            auto created = createBSpline(request);
            require(
                created.has_value(),
                "valid B-spline creation mode failed" +
                    (created ? std::string{} : ": " + created.error().code));
            model::Definition definition{
                digest(index + 1U), {}, {*created}, {}};
            require(model::validate(definition, {}).has_value(),
                    "created B-spline failed the canonical model contract");
            std::vector<double> coordinates;
            std::vector<double> knots;
            std::vector<double> weights;
            const model::NurbsView curve =
                view(*created, coordinates, knots, weights);
            const auto [first, last] = model::nurbsDomain(curve);
            const model::NurbsPoint start = model::evaluateNurbs(curve, first);
            const model::NurbsPoint end = model::evaluateNurbs(curve, last);
            if (periodic)
              require(std::hypot(end.x - start.x, end.y - start.y) <= 1.0e-8,
                      "periodic B-spline is not closed");
            else
              require(std::hypot(start.x - input.front().x.si(),
                                 start.y - input.front().y.si()) <= 1.0e-10 &&
                          std::hypot(end.x - input.back().x.si(),
                                     end.y - input.back().y.si()) <= 1.0e-10,
                      "open B-spline lost its endpoint contract");
            if (creation == BSplineCreation::Interpolation)
              for (const model::Point2 &interpolated : input) {
                const model::NurbsProjection projection = model::projectToNurbs(
                    curve, {interpolated.x.si(), interpolated.y.si()});
                require(std::sqrt(projection.squaredDistance) <= 1.0e-7,
                        "interpolated B-spline missed an input point");
              }
          }
        }
      });
}

void verifyPeriodicTwoPointAndDuplicateContracts() {
  const std::vector<model::Point2> twoPoints{point(-0.02, 0.0),
                                             point(0.02, 0.0)};
  auto periodic =
      createBSpline({id<SketchEntityId>(900'001U), twoPoints,
                     BSplineCreation::Interpolation, 3U, true, false, 1.0e-8});
  require(periodic.has_value(),
          "two-point periodic interpolation was rejected");
  model::Definition definition{digest(900'001U), {}, {*periodic}, {}};
  require(model::validate(definition, {}).has_value(),
          "two-point periodic interpolation is not canonical");

  const std::vector<model::Point2> adjacentDuplicate{
      point(0.0, 0.0), point(0.0, 0.0), point(0.01, 0.02)};
  require(!createBSpline({id<SketchEntityId>(900'002U), adjacentDuplicate,
                          BSplineCreation::ControlPoints, 2U, false})
               .has_value(),
          "adjacent duplicate B-spline points were accepted");
  const std::vector<model::Point2> duplicateClosure{
      point(0.0, 0.0), point(0.01, 0.02), point(0.0, 0.0)};
  require(!createBSpline({id<SketchEntityId>(900'003U), duplicateClosure,
                          BSplineCreation::Interpolation, 3U, true})
               .has_value(),
          "explicit periodic B-spline closure was accepted");
}

double maximumCurveDifference(const model::BSplineEntity &first,
                              const model::BSplineEntity &second) {
  std::vector<double> firstCoordinates;
  std::vector<double> firstKnots;
  std::vector<double> firstWeights;
  std::vector<double> secondCoordinates;
  std::vector<double> secondKnots;
  std::vector<double> secondWeights;
  const model::NurbsView firstView =
      view(first, firstCoordinates, firstKnots, firstWeights);
  const model::NurbsView secondView =
      view(second, secondCoordinates, secondKnots, secondWeights);
  const auto firstDomain = model::nurbsDomain(firstView);
  const auto secondDomain = model::nurbsDomain(secondView);
  double maximum = 0.0;
  for (std::size_t sample = 0U; sample <= 64U; ++sample) {
    const double fraction = static_cast<double>(sample) / 64.0;
    const model::NurbsPoint firstPoint = model::evaluateNurbs(
        firstView, std::lerp(firstDomain.first, firstDomain.second, fraction));
    const model::NurbsPoint secondPoint = model::evaluateNurbs(
        secondView,
        std::lerp(secondDomain.first, secondDomain.second, fraction));
    maximum = std::max(maximum,
                       std::hypot(firstPoint.x - secondPoint.x,
                                  firstPoint.y - secondPoint.y));
  }
  return maximum;
}

void verifyExactEditing(const PropertyProfile &profile) {
  checkProperty(
      "exact B-spline editing", profile,
      [](Random &random, std::uint64_t index) {
        const double x = random.between(-10.0, 10.0);
        const double y = random.between(-10.0, 10.0);
        const double extent = random.between(0.01, 2.0);
        const std::vector<model::Point2> input{
            point(x, y), point(x + extent, y + extent * 0.7),
            point(x + extent * 2.0, y - extent * 0.4),
            point(x + extent * 3.0, y + extent),
            point(x + extent * 4.0, y + extent * 0.1),
            point(x + extent * 5.0, y + extent * 0.6)};
        auto created = createBSpline(
            {id<SketchEntityId>(2'000'000U + index), input,
             BSplineCreation::ControlPoints, 3U, false, false, 1.0e-8});
        require(created.has_value(), "editable B-spline creation failed");

        auto elevated = editBSpline(
            {*created, BSplineEdit::IncreaseDegree, 0U, 0.0, 0.0});
        require(elevated && elevated->degree == created->degree + 1U &&
                    maximumCurveDifference(*created, *elevated) <= 1.0e-10,
                "degree elevation changed the B-spline shape");
        auto reduced = editBSpline(
            {*elevated, BSplineEdit::DecreaseDegree, 0U, 0.0, 1.0e-8});
        require(reduced && reduced->degree <= created->degree &&
                    maximumCurveDifference(*created, *reduced) <= 1.0e-8,
                "degree reduction exceeded its requested deviation");

        const double insertedParameter =
            std::midpoint(created->knots[created->degree].si(),
                          created->knots[created->controlPoints.size()].si());
        auto inserted = editBSpline(
            {*created, BSplineEdit::InsertKnot, 0U, insertedParameter, 0.0});
        require(inserted &&
                    inserted->knots.size() == created->knots.size() + 1U &&
                    maximumCurveDifference(*created, *inserted) <= 1.0e-10,
                "knot insertion changed the B-spline shape");

        auto increased = editBSpline(
            {*created, BSplineEdit::IncreaseKnotMultiplicity, 1U, 0.0, 0.0});
        require(increased &&
                    increased->knots.size() == created->knots.size() + 1U &&
                    maximumCurveDifference(*created, *increased) <= 1.0e-10,
                "knot multiplicity increase changed the B-spline shape");
        auto decreased = editBSpline(
            {*increased, BSplineEdit::DecreaseKnotMultiplicity, 1U, 0.0,
             1.0e-10});
        require(decreased &&
                    decreased->knots.size() == created->knots.size() &&
                    maximumCurveDifference(*created, *decreased) <= 1.0e-10,
                "knot multiplicity decrease exceeded its deviation");

        const std::size_t pole = created->weights.size() / 2U;
        const double changedWeight = created->weights[pole].si() * 1.5;
        auto weighted = editBSpline(
            {*created, BSplineEdit::SetPoleWeight, pole, changedWeight, 0.0});
        require(weighted &&
                    std::abs(weighted->weights[pole].si() - changedWeight) <=
                        std::abs(changedWeight) * 1.0e-14 &&
                    maximumCurveDifference(*created, *weighted) > 1.0e-12,
                "pole weight edit did not change the rational curve");
      });
}

void verifyInvalidEditRefusal() {
  const std::vector<model::Point2> input{
      point(0.0, 0.0), point(0.25, 0.5), point(0.75, -0.25),
      point(1.0, 0.0)};
  auto created = createBSpline({id<SketchEntityId>(3'000'000U), input,
                                BSplineCreation::ControlPoints, 3U, false,
                                false, 1.0e-8});
  require(created.has_value(), "invalid-edit fixture creation failed");
  const auto rejected = editBSpline(
      {*created, static_cast<BSplineEdit>(255U), 0U, 0.0, 0.0});
  require(!rejected && rejected.error().code == "sketch.bspline.invalid-edit",
          "unknown B-spline edit kind was not rejected deterministically");
}

} // namespace

int main() {
  try {
    verifyPeriodicTwoPointAndDuplicateContracts();
    verifyInvalidEditRefusal();
    verifyAllCreationModes(kearne::testkit::propertyProfile());
    verifyExactEditing(kearne::testkit::propertyProfile());
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
  }
}
