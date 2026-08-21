#include <kearne/adapters/ceres_sketch_solver.hpp>
#include <kearne/sketch/nurbs.hpp>
#include <kearne/testkit/property.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <future>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace {

namespace model = kearne::sketch;
using kearne::Angle;
using kearne::ContentDigest;
using kearne::Dimensionless;
using kearne::Length;
using kearne::Quantity;
using kearne::SketchConstraintId;
using kearne::SketchEntityId;
using kearne::TypedId;
using kearne::adapters::CeresSketchSolver;
using kearne::testkit::checkProperty;
using kearne::testkit::PropertyProfile;
using kearne::testkit::Random;

void require(bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}

template <typename Id> Id id(std::uint64_t value) {
  typename Id::RandomTail random{};
  for (std::size_t index = 0; index < random.size(); ++index)
    random[index] = static_cast<std::uint8_t>(value >> ((index % 8U) * 8U));
  auto result = Id::create(value & ((std::uint64_t{1} << 48U) - 1U), random);
  if (!result)
    throw std::runtime_error("could not create test ID");
  return *result;
}

ContentDigest digest(std::uint64_t value) {
  ContentDigest::Bytes bytes{};
  for (std::size_t index = 0; index < bytes.size(); ++index)
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

model::AngleValue angle(double value) {
  auto result = Quantity<Angle>::fromSi(value);
  if (!result)
    throw std::runtime_error("invalid test angle");
  return *result;
}

model::DimensionlessValue dimensionless(double value) {
  auto result = Quantity<Dimensionless>::fromSi(value);
  if (!result)
    throw std::runtime_error("invalid test scalar");
  return *result;
}

model::Point2 point(double x, double y) { return {length(x), length(y)}; }

model::SolveInput solveInput(model::Definition definition) {
  return {std::move(definition), {}, std::nullopt, {}, {}};
}

const model::PointEntity &
pointEntity(const std::vector<model::Entity> &entities,
            SketchEntityId selected) {
  const auto found = std::ranges::find(entities, selected, model::entityId);
  if (found == entities.end() ||
      !std::holds_alternative<model::PointEntity>(*found))
    throw std::runtime_error("point result is missing");
  return std::get<model::PointEntity>(*found);
}

void requireError(const auto &result, std::string_view code) {
  require(!result.has_value(), "operation unexpectedly succeeded");
  require(result.error().code == code,
          "unexpected diagnostic: " + result.error().code);
}

model::Definition fullyConstrainedPoints(std::uint64_t seed, double originX,
                                         double originY, double deltaX,
                                         double deltaY) {
  const SketchEntityId anchor = id<SketchEntityId>(seed + 1);
  const SketchEntityId moving = id<SketchEntityId>(seed + 2);
  return {digest(seed),
          {},
          {model::PointEntity{anchor, point(originX, originY)},
           model::PointEntity{
               moving, point(originX + deltaX * 0.2, originY - deltaY * 0.3)}},
          {model::Block{id<SketchConstraintId>(seed + 11), anchor},
           model::HorizontalDistance{id<SketchConstraintId>(seed + 12),
                                     {anchor, model::PointKey::Point},
                                     {moving, model::PointKey::Point},
                                     length(deltaX)},
           model::VerticalDistance{id<SketchConstraintId>(seed + 13),
                                   {anchor, model::PointKey::Point},
                                   {moving, model::PointKey::Point},
                                   length(deltaY)}}};
}

model::Definition completeConstraintScene(std::uint64_t seed, double x,
                                          double y, double extent) {
  const double radius = extent * 0.2;
  const auto entity = [seed](std::uint64_t offset) {
    return id<SketchEntityId>(seed + offset);
  };
  const auto constraint = [seed](std::uint64_t offset) {
    return id<SketchConstraintId>(seed + 100 + offset);
  };
  const SketchEntityId origin = entity(1);
  const SketchEntityId midpoint = entity(2);
  const SketchEntityId horizontal = entity(3);
  const SketchEntityId parallel = entity(4);
  const SketchEntityId vertical = entity(5);
  const SketchEntityId collinear = entity(6);
  const SketchEntityId firstCircle = entity(7);
  const SketchEntityId equalCircle = entity(8);
  const SketchEntityId tangentCircle = entity(9);
  const SketchEntityId lineTangentCircle = entity(10);
  const SketchEntityId arc = entity(11);
  const SketchEntityId mirrored = entity(12);

  model::Definition definition{
      digest(seed),
      {},
      {model::PointEntity{origin, point(x, y)},
       model::PointEntity{midpoint, point(x + extent * 0.5, y)},
       model::LineEntity{horizontal, point(x, y), point(x + extent, y)},
       model::LineEntity{parallel, point(x, y + extent),
                         point(x + extent, y + extent)},
       model::LineEntity{vertical, point(x, y), point(x, y + extent)},
       model::LineEntity{collinear, point(x + extent * 2.0, y),
                         point(x + extent * 3.0, y)},
       model::CircleEntity{firstCircle, point(x + extent * 4.0, y),
                           length(radius)},
       model::CircleEntity{equalCircle, point(x + extent * 4.0, y),
                           length(radius)},
       model::CircleEntity{tangentCircle,
                           point(x + extent * 4.0 + radius * 2.0, y),
                           length(radius)},
       model::CircleEntity{lineTangentCircle,
                           point(x + extent * 0.7, y + radius), length(radius)},
       model::ArcEntity{arc, point(x + extent * 6.0, y), length(radius),
                        angle(0.0), angle(std::numbers::pi / 2.0)},
       model::PointEntity{mirrored, point(x - extent * 0.5, y)}},
      {}};
  auto &constraints = definition.constraints;
  constraints = {
      model::Coincident{constraint(1),
                        {origin, model::PointKey::Point},
                        {horizontal, model::PointKey::Start}},
      model::Horizontal{constraint(2), horizontal},
      model::Vertical{constraint(3), vertical},
      model::Parallel{constraint(4), horizontal, parallel},
      model::Perpendicular{constraint(5), horizontal, vertical},
      model::Tangent{constraint(6), firstCircle, tangentCircle,
                     model::Tangency::External},
      model::Tangent{constraint(7), horizontal, lineTangentCircle,
                     model::Tangency::External},
      model::Concentric{constraint(8), firstCircle, equalCircle},
      model::Equal{constraint(9), firstCircle, equalCircle},
      model::Midpoint{
          constraint(10), {midpoint, model::PointKey::Point}, horizontal},
      model::Block{constraint(11), origin},
      model::Collinear{constraint(12), horizontal, collinear},
      model::Distance{constraint(13),
                      {origin, model::PointKey::Point},
                      {midpoint, model::PointKey::Point},
                      length(extent * 0.5)},
      model::HorizontalDistance{constraint(14),
                                {origin, model::PointKey::Point},
                                {midpoint, model::PointKey::Point},
                                length(extent * 0.5)},
      model::VerticalDistance{constraint(15),
                              {origin, model::PointKey::Point},
                              {midpoint, model::PointKey::Point},
                              length(0.0)},
      model::Radius{constraint(16), firstCircle, length(radius)},
      model::Diameter{constraint(17), tangentCircle, length(radius * 2.0)},
      model::AngleBetween{constraint(18), horizontal, vertical,
                          angle(std::numbers::pi / 2.0)},
      model::Radius{constraint(19), arc, length(radius)},
      model::PointOnObject{
          constraint(20), {origin, model::PointKey::Point}, horizontal},
      model::Symmetric{constraint(21),
                       {mirrored, model::PointKey::Point},
                       {midpoint, model::PointKey::Point},
                       vertical},
      model::Lock{
          constraint(22), {origin, model::PointKey::Point}, point(x, y)},
      model::SymmetricAboutPoint{constraint(23),
                                 {mirrored, model::PointKey::Point},
                                 {midpoint, model::PointKey::Point},
                                 {origin, model::PointKey::Point}}};
  return definition;
}

model::Definition fixedPointScene(std::uint64_t seed, std::size_t count) {
  model::Definition definition{digest(seed), {}, {}, {}};
  definition.entities.reserve(count);
  definition.constraints.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    const auto offset = static_cast<std::uint64_t>(index);
    const SketchEntityId entity = id<SketchEntityId>(seed + offset + 1);
    definition.entities.push_back(model::PointEntity{
        entity, point(static_cast<double>(index % 32U) * 0.01,
                      static_cast<double>(index / 32U) * 0.01)});
    definition.constraints.push_back(model::Block{
        id<SketchConstraintId>(seed + count + offset + 1), entity});
  }
  return definition;
}

model::Definition tangentCircleScene(std::uint64_t seed, double x, double y,
                                     double radius) {
  const SketchEntityId anchor = id<SketchEntityId>(seed + 1);
  const SketchEntityId moving = id<SketchEntityId>(seed + 2);
  return {
      digest(seed),
      {},
      {model::CircleEntity{anchor, point(0.0, 0.0), length(1.0)},
       model::CircleEntity{moving, point(x, y), length(radius)}},
      {model::Block{id<SketchConstraintId>(seed + 11), anchor},
       model::Radius{id<SketchConstraintId>(seed + 12), moving, length(1.0)},
       model::VerticalDistance{id<SketchConstraintId>(seed + 13),
                               {anchor, model::PointKey::Center},
                               {moving, model::PointKey::Center},
                               length(0.0)},
       model::Tangent{id<SketchConstraintId>(seed + 14), anchor, moving,
                      model::Tangency::External}}};
}

void residualContract(const PropertyProfile &profile) {
  checkProperty(
      "all sketch constraint residuals", profile,
      [](Random &random, std::uint64_t index) {
        const double extent = random.between(0.01, 10.0);
        const double x = random.between(-100.0, 100.0);
        const double y = random.between(-100.0, 100.0);
        model::Definition definition =
            completeConstraintScene(10'000 + index * 256, x, y, extent);
        auto residuals =
            model::evaluateResiduals(definition, definition.entities, {});
        require(residuals.has_value(), residuals ? "" : residuals.error().code);
        require(std::ranges::all_of(*residuals,
                                    &model::ConstraintResidual::satisfied),
                "an exact constraint scene has a residual");
      });
}

void solverContract(const PropertyProfile &baseProfile) {
  PropertyProfile profile = baseProfile;
  profile.iterations =
      std::max<std::uint64_t>(100, baseProfile.iterations / 100);
  CeresSketchSolver solver;
  checkProperty(
      "fully constrained sketch solve", profile,
      [&solver](Random &random, std::uint64_t index) {
        const double originX = random.between(-100.0, 100.0);
        const double originY = random.between(-100.0, 100.0);
        const double deltaX = random.between(-10.0, 10.0);
        const double deltaY = random.between(-10.0, 10.0);
        model::Definition definition = fullyConstrainedPoints(
            1'000'000 + index * 32, originX, originY, deltaX, deltaY);
        const SketchEntityId moving = model::entityId(definition.entities[1]);
        model::SolveInput input = solveInput(definition);
        auto solved = solver.solve(input);
        require(solved.has_value(), solved ? "" : solved.error().code);
        require(solved->status == model::SolveStatus::Solved,
                "fully constrained system did not solve");
        require(solved->degreesOfFreedom == 0,
                "fully constrained system has freedom");
        const model::PointEntity &result =
            pointEntity(solved->geometry, moving);
        require(std::abs(result.point.x.si() - (originX + deltaX)) <= 1.0e-8,
                "horizontal dimension was not solved");
        require(std::abs(result.point.y.si() - (originY + deltaY)) <= 1.0e-8,
                "vertical dimension was not solved");

        std::ranges::reverse(input.definition.entities);
        std::ranges::reverse(input.definition.constraints);
        auto reordered = solver.solve(input);
        require(reordered.has_value(), reordered ? "" : reordered.error().code);
        const model::PointEntity &reorderedPoint =
            pointEntity(reordered->geometry, moving);
        require(std::abs(reorderedPoint.point.x.si() - result.point.x.si()) <=
                        1.0e-8 &&
                    std::abs(reorderedPoint.point.y.si() -
                             result.point.y.si()) <= 1.0e-8,
                "solve depends on declaration order");
      });
}

void generatedScaleAndOrderStress(const PropertyProfile &baseProfile) {
  PropertyProfile profile = baseProfile;
  profile.iterations =
      std::max<std::uint64_t>(100, baseProfile.iterations / 100);
  CeresSketchSolver solver;
  checkProperty(
      "solver scale metamorphism", profile,
      [&solver](Random &random, std::uint64_t index) {
        const double originX = random.between(-10.0, 10.0);
        const double originY = random.between(-10.0, 10.0);
        const double deltaX = random.between(-1.0, 1.0);
        const double deltaY = random.between(-1.0, 1.0);
        const double scale = random.between(0.01, 100.0);
        model::Definition base = fullyConstrainedPoints(
            3'000'000 + index * 64, originX, originY, deltaX, deltaY);
        model::Definition transformed = fullyConstrainedPoints(
            4'000'000 + index * 64, originX * scale, originY * scale,
            deltaX * scale, deltaY * scale);
        auto first = solver.solve(solveInput(std::move(base)));
        auto second = solver.solve(solveInput(std::move(transformed)));
        require(first.has_value() && second.has_value(),
                "metamorphic solve failed");
        const auto &firstPoint =
            std::get<model::PointEntity>(first->geometry[1]);
        const auto &secondPoint =
            std::get<model::PointEntity>(second->geometry[1]);
        require(std::abs(secondPoint.point.x.si() -
                         firstPoint.point.x.si() * scale) <= 2.0e-8 &&
                    std::abs(secondPoint.point.y.si() -
                             firstPoint.point.y.si() * scale) <= 2.0e-8,
                "uniform scale changed the solved geometry");
      });

  model::Definition ordered = fixedPointScene(5'000'000, 129);
  auto first = solver.solve(solveInput(ordered));
  std::ranges::reverse(ordered.entities);
  std::ranges::reverse(ordered.constraints);
  auto second = solver.solve(solveInput(ordered));
  require(first.has_value() && second.has_value(),
          "generated order stress solve failed");
  require(first->status == model::SolveStatus::Solved &&
              second->status == model::SolveStatus::Solved &&
              first->degreesOfFreedom == 0 && second->degreesOfFreedom == 0,
          "generated order stress has incorrect solver state");
  for (const model::Entity &entity : first->geometry) {
    const auto selected = model::entityId(entity);
    const auto found =
        std::ranges::find(second->geometry, selected, model::entityId);
    require(found != second->geometry.end() && *found == entity,
            "fixed solution depends on declaration order");
  }
}

void nonlinearSolverContract(const PropertyProfile &baseProfile) {
  PropertyProfile profile = baseProfile;
  profile.iterations =
      std::max<std::uint64_t>(100, baseProfile.iterations / 100);
  CeresSketchSolver solver;
  checkProperty(
      "nonlinear circle tangency", profile,
      [&solver](Random &random, std::uint64_t index) {
        model::Definition definition = tangentCircleScene(
            5'500'000 + index * 32, random.between(0.2, 4.0),
            random.between(-1.0, 1.0), random.between(0.2, 2.0));
        const SketchEntityId moving = model::entityId(definition.entities[1]);
        auto solved = solver.solve(solveInput(std::move(definition)));
        require(solved.has_value(), solved ? "" : solved.error().code);
        require(solved->status == model::SolveStatus::Solved &&
                    solved->degreesOfFreedom == 0,
                "nonlinear tangency did not fully solve");
        const auto found =
            std::ranges::find(solved->geometry, moving, model::entityId);
        require(found != solved->geometry.end() &&
                    std::holds_alternative<model::CircleEntity>(*found),
                "solved tangent circle is missing");
        const auto &circle = std::get<model::CircleEntity>(*found);
        require(std::abs(circle.center.y.si()) <= 1.0e-8 &&
                    std::abs(circle.radius.si() - 1.0) <= 1.0e-8 &&
                    std::abs(std::abs(circle.center.x.si()) - 2.0) <= 1.0e-8,
                "nonlinear tangency geometry is incorrect");
      });
}

void rigidGroupContract(const PropertyProfile &baseProfile) {
  PropertyProfile profile = baseProfile;
  profile.iterations =
      std::max<std::uint64_t>(100, baseProfile.iterations / 100);
  CeresSketchSolver solver;
  checkProperty(
      "rigid group preserves shape with three planar freedoms", profile,
      [&solver](Random &random, std::uint64_t index) {
        const double extent = random.between(0.01, 10.0);
        const double offset = random.between(0.01, 10.0);
        const std::uint64_t seed = 5'800'000 + index * 16U;
        const SketchEntityId first = id<SketchEntityId>(seed + 1U);
        const SketchEntityId second = id<SketchEntityId>(seed + 2U);
        model::Definition definition{
            digest(seed),
            {},
            {model::LineEntity{first, point(0.0, 0.0), point(extent, 0.0)},
             model::LineEntity{second, point(0.0, offset),
                               point(extent, offset)}},
            {model::Group{id<SketchConstraintId>(seed + 3U), {first, second}}}};
        auto solved = solver.solve(solveInput(definition));
        require(solved.has_value(), solved ? "" : solved.error().code);
        require(solved->status == model::SolveStatus::Underconstrained &&
                    solved->degreesOfFreedom == 3 &&
                    std::ranges::all_of(solved->residuals,
                                        &model::ConstraintResidual::satisfied),
                "rigid group did not preserve exactly three planar freedoms");

        std::ranges::reverse(definition.entities);
        std::get<model::Group>(definition.constraints.front()).entities = {
            second, first};
        auto reordered = solver.solve(solveInput(std::move(definition)));
        require(reordered.has_value() &&
                    reordered->status == model::SolveStatus::Underconstrained &&
                    reordered->degreesOfFreedom == 3,
                "rigid group depends on entity declaration order");
      });
}

void validationAndDegeneracy() {
  const SketchEntityId entity = id<SketchEntityId>(6'000'001);
  const SketchEntityId line = id<SketchEntityId>(6'000'002);
  const SketchEntityId arc = id<SketchEntityId>(6'000'003);
  model::Definition valid{
      digest(6'000'000),
      {},
      {model::PointEntity{entity, point(0.0, 0.0)},
       model::LineEntity{line, point(0.0, 0.0), point(1.0, 0.0)},
       model::ArcEntity{arc, point(0.0, 0.0), length(1.0), angle(0.0),
                        angle(1.0)}},
      {}};

  const auto invalidProfile = [&](auto mutate) {
    model::NumericalProfile profile;
    mutate(profile);
    requireError(model::validate(valid, profile),
                 "sketch.numerical.invalid-profile");
  };
  invalidProfile([](auto &value) {
    value.maximumCoordinateMeters = value.typicalLengthMeters;
  });
  invalidProfile([](auto &value) { value.angleToleranceRadians = 1.0; });
  invalidProfile([](auto &value) { value.rankRelativeTolerance = 1.0; });
  invalidProfile([](auto &value) {
    value.maximumIterations = std::numeric_limits<std::uint32_t>::max();
  });

  model::Definition degenerate = valid;
  degenerate.entities[1] =
      model::LineEntity{line, point(0.0, 0.0), point(0.0, 0.0)};
  requireError(model::validate(degenerate, {}),
               "sketch.entity.degenerate-line");
  degenerate = valid;
  degenerate.entities[2] = model::ArcEntity{arc, point(0.0, 0.0), length(1.0),
                                            angle(1.0), angle(1.0)};
  requireError(model::validate(degenerate, {}), "sketch.entity.degenerate-arc");
  degenerate = valid;
  degenerate.entities[2] =
      model::ArcEntity{arc, point(0.0, 0.0), length(1.0), angle(0.0),
                       angle(2.0 * std::numbers::pi + 1.0e-6)};
  requireError(model::validate(degenerate, {}),
               "sketch.entity.unsupported-arc-span");
  degenerate = valid;
  degenerate.entities[2] = model::ArcEntity{
      arc, point(0.0, 0.0), length(1.0e6 + 1.0), angle(0.0), angle(1.0)};
  requireError(model::validate(degenerate, {}), "sketch.entity.invalid-radius");

  std::vector<model::Entity> invalidGeometry = valid.entities;
  invalidGeometry[1] =
      model::LineEntity{line, point(0.0, 0.0), point(0.0, 0.0)};
  requireError(model::evaluateResiduals(valid, invalidGeometry, {}),
               "sketch.solution.degenerate-line");
  invalidGeometry = valid.entities;
  invalidGeometry[2] = model::ArcEntity{arc, point(0.0, 0.0), length(1.0),
                                        angle(1.0), angle(1.0)};
  requireError(model::evaluateResiduals(valid, invalidGeometry, {}),
               "sketch.solution.degenerate-arc");

  model::Definition invalidGroup = valid;
  invalidGroup.constraints = {
      model::Group{id<SketchConstraintId>(6'000'010), {entity}}};
  requireError(model::validate(invalidGroup, {}),
               "sketch.constraint.invalid-group");
  invalidGroup.constraints = {
      model::Group{id<SketchConstraintId>(6'000'011), {entity, entity}}};
  requireError(model::validate(invalidGroup, {}),
               "sketch.constraint.invalid-group");

  CeresSketchSolver solver;
  model::SolveInput withPrior = solveInput(valid);
  withPrior.priorSolution = {
      model::LineEntity{line, point(0.0, 0.0), point(0.0, 0.0)}};
  requireError(solver.solve(withPrior), "sketch.seed.degenerate-line");
  withPrior.priorSolution = {model::ArcEntity{arc, point(0.0, 0.0), length(1.0),
                                              angle(1.0), angle(1.0)}};
  requireError(solver.solve(withPrior), "sketch.seed.degenerate-arc");
}

void rankAndCancellationStress() {
  CeresSketchSolver solver;
  model::Definition fixed = fixedPointScene(7'000'000, 1);
  model::SolveInput scaled = solveInput(fixed);
  scaled.numerical.typicalLengthMeters = 1.0e-12;
  scaled.numerical.minimumLengthMeters = 1.0e-18;
  scaled.numerical.lengthToleranceMeters = 1.0e-15;
  auto ranked = solver.solve(scaled);
  require(ranked.has_value(), ranked ? "" : ranked.error().code);
  require(ranked->status == model::SolveStatus::Solved &&
              ranked->degreesOfFreedom == 0,
          "rank changed under valid numerical scaling");

  model::Definition deficient = fixedPointScene(7'100'000, 100);
  deficient.constraints.pop_back();
  const std::vector<model::Constraint> independent = deficient.constraints;
  for (std::size_t index = 0; index < independent.size(); ++index) {
    const SketchEntityId selected =
        std::get<model::Block>(independent[index]).entity;
    deficient.constraints.push_back(model::Block{
        id<SketchConstraintId>(7'200'000 + static_cast<std::uint64_t>(index)),
        selected});
  }
  auto underconstrained = solver.solve(solveInput(std::move(deficient)));
  require(underconstrained.has_value(),
          underconstrained ? "" : underconstrained.error().code);
  require(underconstrained->status == model::SolveStatus::Underconstrained &&
              underconstrained->degreesOfFreedom == 2 &&
              underconstrained->modes.size() == 2,
          "large rank-deficient normal system was misclassified");

  kearne::CancellationSource cancellation;
  model::SolveInput cancellable = solveInput(fixedPointScene(8'000'000, 1'000));
  cancellable.cancellation = cancellation.get_token();
  std::promise<void> entered;
  std::future<kearne::Result<model::SolveResult>> future =
      std::async(std::launch::async, [&] {
        entered.set_value();
        return solver.solve(cancellable);
      });
  entered.get_future().wait();
  cancellation.request_stop();
  auto cancelled = future.get();
  require(cancelled.has_value() &&
              cancelled->status == model::SolveStatus::Cancelled,
          "concurrent cancellation did not stop a generated solve");
}

void stateSemantics() {
  CeresSketchSolver solver;
  const SketchEntityId pointId = id<SketchEntityId>(90'001);
  model::Definition free{
      digest(90'000), {}, {model::PointEntity{pointId, point(1.0, 2.0)}}, {}};
  auto underconstrained = solver.solve(solveInput(free));
  require(underconstrained.has_value(),
          underconstrained ? "" : underconstrained.error().code);
  require(underconstrained->status == model::SolveStatus::Underconstrained &&
              underconstrained->degreesOfFreedom == 2,
          "free point state is incorrect");

  model::SolveInput dragged = solveInput(free);
  dragged.drag =
      model::DragTarget{{pointId, model::PointKey::Point}, point(3.0, -4.0)};
  auto moved = solver.solve(dragged);
  require(moved.has_value(), moved ? "" : moved.error().code);
  const auto &movedPoint = pointEntity(moved->geometry, pointId);
  require(std::abs(movedPoint.point.x.si() - 3.0) <= 1.0e-8 &&
              std::abs(movedPoint.point.y.si() + 4.0) <= 1.0e-8,
          "drag target was not solved");

  free.constraints.push_back(
      model::Block{id<SketchConstraintId>(90'002), pointId});
  dragged.definition = free;
  auto blocked = solver.solve(dragged);
  require(blocked.has_value(), blocked ? "" : blocked.error().code);
  const auto &blockedPoint = pointEntity(blocked->geometry, pointId);
  require(std::abs(blockedPoint.point.x.si() - 1.0) <= 1.0e-8 &&
              std::abs(blockedPoint.point.y.si() - 2.0) <= 1.0e-8,
          "blocked drag weakened a fixed constraint");
  require(std::ranges::any_of(blocked->diagnostics,
                              [](const auto &value) {
                                return value.code == "sketch.drag.blocked";
                              }),
          "blocked drag has no diagnostic");

  kearne::CancellationSource cancellation;
  cancellation.request_stop();
  model::SolveInput cancelled = solveInput(free);
  cancelled.cancellation = cancellation.get_token();
  auto cancelledResult = solver.solve(cancelled);
  require(cancelledResult.has_value() &&
              cancelledResult->status == model::SolveStatus::Cancelled,
          "pre-cancelled solve did not cancel");

  model::Definition contradictory =
      fullyConstrainedPoints(100'000, 0.0, 0.0, 1.0, 0.0);
  const SketchEntityId first = model::entityId(contradictory.entities[0]);
  const SketchEntityId second = model::entityId(contradictory.entities[1]);
  contradictory.constraints.push_back(
      model::HorizontalDistance{id<SketchConstraintId>(100'020),
                                {first, model::PointKey::Point},
                                {second, model::PointKey::Point},
                                length(2.0)});
  auto inconsistent = solver.solve(solveInput(contradictory));
  require(inconsistent.has_value(),
          inconsistent ? "" : inconsistent.error().code);
  require(inconsistent->status == model::SolveStatus::Inconsistent &&
              !inconsistent->conflicts.empty() &&
              !inconsistent->conflicts.front().exact,
          "contradiction was not reported as an approximate conflict");
}

void exactConicSolverContract() {
  CeresSketchSolver solver;
  const SketchEntityId ellipseId = id<SketchEntityId>(110'001);
  const SketchEntityId arcId = id<SketchEntityId>(110'002);
  const SketchEntityId hyperbolaId = id<SketchEntityId>(110'003);
  const SketchEntityId parabolaId = id<SketchEntityId>(110'004);
  model::Definition free{
      digest(110'000),
      {},
      {model::EllipseEntity{ellipseId, point(0.0, 0.0), length(2.0),
                            length(0.75), angle(0.25)},
       model::EllipticalArcEntity{arcId, point(4.0, 1.0), length(1.5),
                                  length(0.5), angle(-0.4), angle(0.2),
                                  angle(2.6)},
       model::HyperbolicArcEntity{hyperbolaId, point(-3.0, 2.0), length(0.8),
                                  length(1.2), angle(0.6), dimensionless(-0.7),
                                  dimensionless(1.1)},
       model::ParabolicArcEntity{parabolaId, point(7.0, -2.0), length(0.65),
                                 angle(-0.35), length(-1.2), length(1.8)}},
      {}};
  auto unconstrained = solver.solve(solveInput(free));
  require(unconstrained &&
              unconstrained->status == model::SolveStatus::Underconstrained &&
              unconstrained->degreesOfFreedom == 25U,
          "free conic parameter count is incorrect");

  model::Definition fixed = free;
  fixed.constraints = {
      model::Block{id<SketchConstraintId>(110'010), ellipseId},
      model::Block{id<SketchConstraintId>(110'011), arcId},
      model::Block{id<SketchConstraintId>(110'012), hyperbolaId},
      model::Block{id<SketchConstraintId>(110'013), parabolaId}};
  auto blocked = solver.solve(solveInput(fixed));
  require(blocked && blocked->status == model::SolveStatus::Solved &&
              blocked->degreesOfFreedom == 0U &&
              blocked->geometry == free.entities,
          "blocked conics did not preserve every exact parameter");

  model::SolveInput dragged = solveInput(free);
  dragged.drag =
      model::DragTarget{{ellipseId, model::PointKey::Major}, point(2.5, 1.25)};
  auto moved = solver.solve(dragged);
  require(moved && moved->status == model::SolveStatus::Underconstrained,
          "ellipse major-axis drag did not solve");
  model::Definition movedDefinition = free;
  movedDefinition.entities = moved->geometry;
  auto major =
      model::resolvePoint(movedDefinition, {ellipseId, model::PointKey::Major});
  require(major && std::abs(major->x.si() - 2.5) <= 1.0e-7 &&
              std::abs(major->y.si() - 1.25) <= 1.0e-7,
          "ellipse semantic axis did not follow the drag target");

  model::SolveInput hyperbolaDrag = solveInput(free);
  hyperbolaDrag.drag = model::DragTarget{{hyperbolaId, model::PointKey::Start},
                                         point(-2.1, 1.4)};
  auto movedHyperbola = solver.solve(hyperbolaDrag);
  require(movedHyperbola &&
              movedHyperbola->status == model::SolveStatus::Underconstrained,
          "hyperbolic endpoint drag did not solve");
  model::Definition movedHyperbolaDefinition = free;
  movedHyperbolaDefinition.entities = movedHyperbola->geometry;
  auto hyperbolaStart = model::resolvePoint(
      movedHyperbolaDefinition, {hyperbolaId, model::PointKey::Start});
  require(hyperbolaStart && std::hypot(hyperbolaStart->x.si() + 2.1,
                                       hyperbolaStart->y.si() - 1.4) <= 1.0e-7,
          "hyperbolic endpoint did not follow the drag target");

  model::SolveInput parabolaDrag = solveInput(free);
  parabolaDrag.drag = model::DragTarget{{parabolaId, model::PointKey::Focus},
                                        point(8.0, -1.25)};
  auto movedParabola = solver.solve(parabolaDrag);
  require(movedParabola &&
              movedParabola->status == model::SolveStatus::Underconstrained,
          "parabolic focus drag did not solve");
  model::Definition movedParabolaDefinition = free;
  movedParabolaDefinition.entities = movedParabola->geometry;
  auto parabolaFocus = model::resolvePoint(
      movedParabolaDefinition, {parabolaId, model::PointKey::Focus});
  require(parabolaFocus && std::hypot(parabolaFocus->x.si() - 8.0,
                                      parabolaFocus->y.si() + 1.25) <= 1.0e-7,
          "parabolic focus did not follow the drag target");

  const SketchEntityId hyperbolaPoint = id<SketchEntityId>(110'020);
  const SketchEntityId parabolaPoint = id<SketchEntityId>(110'021);
  const double hyperbolaParameter = 0.45;
  const double parabolaParameter = -0.7;
  model::Definition incidence{
      digest(110'019),
      {},
      {model::HyperbolicArcEntity{hyperbolaId, point(0.0, 0.0), length(2.0),
                                  length(0.75), angle(0.0), dimensionless(-1.0),
                                  dimensionless(1.0)},
       model::ParabolicArcEntity{parabolaId, point(5.0, 0.0), length(0.5),
                                 angle(0.0), length(-1.0), length(1.0)},
       model::PointEntity{hyperbolaPoint,
                          point(2.0 * std::cosh(hyperbolaParameter),
                                0.75 * std::sinh(hyperbolaParameter))},
       model::PointEntity{
           parabolaPoint,
           point(5.0 + parabolaParameter * parabolaParameter / 2.0,
                 parabolaParameter)}},
      {model::Block{id<SketchConstraintId>(110'030), hyperbolaId},
       model::Block{id<SketchConstraintId>(110'031), parabolaId},
       model::PointOnObject{id<SketchConstraintId>(110'032),
                            {hyperbolaPoint, model::PointKey::Point},
                            hyperbolaId},
       model::PointOnObject{id<SketchConstraintId>(110'033),
                            {parabolaPoint, model::PointKey::Point},
                            parabolaId}}};
  auto incident = solver.solve(solveInput(incidence));
  require(incident &&
              incident->status == model::SolveStatus::Underconstrained &&
              incident->degreesOfFreedom == 2U &&
              std::ranges::all_of(incident->residuals,
                                  &model::ConstraintResidual::satisfied),
          "exact conic point-on-object constraints are not solved");
}

void exactBSplineSolverContract() {
  CeresSketchSolver solver;
  const SketchEntityId splineId = id<SketchEntityId>(120'001);
  const SketchEntityId pointId = id<SketchEntityId>(120'002);
  const std::vector<model::DimensionlessValue> knots{
      dimensionless(0.0), dimensionless(0.0), dimensionless(0.0),
      dimensionless(1.0), dimensionless(1.0), dimensionless(1.0)};
  const std::vector<model::DimensionlessValue> weights{
      dimensionless(1.0), dimensionless(0.75), dimensionless(1.0)};
  const model::BSplineEntity spline{
      splineId,
      {point(0.0, 0.0), point(1.0, 2.0), point(2.0, 0.0)},
      knots,
      weights,
      2U};
  model::Definition free{digest(120'000), {}, {spline}, {}};

  auto unconstrained = solver.solve(solveInput(free));
  require(unconstrained &&
              unconstrained->status == model::SolveStatus::Underconstrained &&
              unconstrained->degreesOfFreedom == 9U,
          "free rational B-spline parameter count is incorrect");

  const SketchEntityId periodicId = id<SketchEntityId>(120'003);
  const model::BSplineEntity periodic{
      periodicId,
      {point(-0.5, 0.38888888888888884), point(0.5, -0.38888888888888884),
       point(1.142857142857143, -0.19047619047619047), point(1.25, 1.25),
       point(-0.14285714285714296, 1.190476190476191),
       point(-0.5, 0.38888888888888884), point(0.5, -0.38888888888888884)},
      {dimensionless(-2.0), dimensionless(-1.0), dimensionless(0.0),
       dimensionless(0.0), dimensionless(1.0), dimensionless(2.0),
       dimensionless(3.0), dimensionless(4.0), dimensionless(4.0),
       dimensionless(5.0), dimensionless(6.0)},
      {dimensionless(1.0), dimensionless(1.0), dimensionless(1.0),
       dimensionless(1.0), dimensionless(1.0), dimensionless(1.0),
       dimensionless(1.0)},
      3U,
      true};
  model::Definition periodicFree{digest(120'004), {}, {periodic}, {}};
  auto periodicResult = solver.solve(solveInput(periodicFree));
  require(periodicResult &&
              periodicResult->status == model::SolveStatus::Underconstrained &&
              periodicResult->degreesOfFreedom == 15U &&
              periodicResult->geometry == periodicFree.entities,
          "periodic B-spline seam is not an exact intrinsic constraint");

  model::Definition fixed = free;
  fixed.constraints = {model::Block{id<SketchConstraintId>(120'010), splineId}};
  auto blocked = solver.solve(solveInput(fixed));
  require(blocked && blocked->status == model::SolveStatus::Solved &&
              blocked->degreesOfFreedom == 0U &&
              blocked->geometry == free.entities,
          "blocked rational B-spline did not preserve its exact parameters");

  model::SolveInput drag = solveInput(free);
  drag.drag =
      model::DragTarget{{splineId, model::PointKey::Start}, point(-0.5, 0.25)};
  auto moved = solver.solve(drag);
  require(moved && moved->status == model::SolveStatus::Underconstrained,
          "B-spline endpoint drag did not solve");
  model::Definition movedDefinition = free;
  movedDefinition.entities = moved->geometry;
  auto start =
      model::resolvePoint(movedDefinition, {splineId, model::PointKey::Start});
  require(start &&
              std::hypot(start->x.si() + 0.5, start->y.si() - 0.25) <= 1.0e-7,
          "B-spline endpoint did not follow the drag target");

  const std::array<double, 6U> coordinates{0.0, 0.0, 1.0, 2.0, 2.0, 0.0};
  const std::array<double, 6U> plainKnots{0.0, 0.0, 0.0, 1.0, 1.0, 1.0};
  const std::array<double, 3U> plainWeights{1.0, 0.75, 1.0};
  const model::NurbsPoint incidentPoint =
      model::evaluateNurbs({coordinates, plainKnots, plainWeights, 2U}, 0.37);
  model::Definition incidence{
      digest(120'020),
      {},
      {spline, model::PointEntity{pointId, point(incidentPoint.x + 0.1,
                                                 incidentPoint.y + 0.15)}},
      {model::Block{id<SketchConstraintId>(120'021), splineId},
       model::PointOnObject{id<SketchConstraintId>(120'022),
                            {pointId, model::PointKey::Point},
                            splineId}}};
  auto incident = solver.solve(solveInput(incidence));
  const std::string incidenceState =
      !incident
          ? incident.error().code
          : "status=" + std::to_string(static_cast<int>(incident->status)) +
                ", dof=" + std::to_string(incident->degreesOfFreedom) +
                ", residual=" +
                (incident->residuals.empty()
                     ? std::string{"missing"}
                     : std::to_string(
                           incident->residuals.back().normalizedMaximum));
  require(incident &&
              incident->status == model::SolveStatus::Underconstrained &&
              incident->degreesOfFreedom == 1U &&
              std::ranges::all_of(incident->residuals,
                                  &model::ConstraintResidual::satisfied),
          "exact rational B-spline point-on-object constraint did not solve: " +
              incidenceState);
}

} // namespace

int main() {
  try {
    const PropertyProfile profile = kearne::testkit::propertyProfile();
    residualContract(profile);
    solverContract(profile);
    generatedScaleAndOrderStress(profile);
    nonlinearSolverContract(profile);
    rigidGroupContract(profile);
    validationAndDegeneracy();
    rankAndCancellationStress();
    stateSemantics();
    exactConicSolverContract();
    exactBSplineSolverContract();
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
  }
}
