#pragma once

#include <kearne/base/value.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

namespace kearne::sketch {

using LengthValue = Quantity<Length>;
using AngleValue = Quantity<Angle>;

struct EvaluatedPlaneIdentity {
  ModelBindingId attachmentBinding;
  RevisionId revision;
  bool operator==(const EvaluatedPlaneIdentity &) const = default;
};

struct Point2 {
  LengthValue x;
  LengthValue y;
  auto operator<=>(const Point2 &) const = default;
};

struct PointEntity {
  SketchEntityId id;
  Point2 point;
  bool construction = false;
  auto operator<=>(const PointEntity &) const = default;
};

struct LineEntity {
  SketchEntityId id;
  Point2 start;
  Point2 end;
  bool construction = false;
  auto operator<=>(const LineEntity &) const = default;
};

struct CircleEntity {
  SketchEntityId id;
  Point2 center;
  LengthValue radius;
  bool construction = false;
  auto operator<=>(const CircleEntity &) const = default;
};

struct ArcEntity {
  SketchEntityId id;
  Point2 center;
  LengthValue radius;
  AngleValue startAngle;
  AngleValue endAngle;
  bool construction = false;
  auto operator<=>(const ArcEntity &) const = default;
};

using Entity = std::variant<PointEntity, LineEntity, CircleEntity, ArcEntity>;

enum class PointKey : std::uint8_t {
  Point = 1,
  Start = 2,
  End = 3,
  Center = 4
};

struct PointRef {
  SketchEntityId entity;
  PointKey key = PointKey::Point;
  auto operator<=>(const PointRef &) const = default;
};

struct Coincident {
  SketchConstraintId id;
  PointRef first;
  PointRef second;
  auto operator<=>(const Coincident &) const = default;
};

struct Horizontal {
  SketchConstraintId id;
  SketchEntityId line;
  auto operator<=>(const Horizontal &) const = default;
};

struct Vertical {
  SketchConstraintId id;
  SketchEntityId line;
  auto operator<=>(const Vertical &) const = default;
};

struct Parallel {
  SketchConstraintId id;
  SketchEntityId first;
  SketchEntityId second;
  auto operator<=>(const Parallel &) const = default;
};

struct Perpendicular {
  SketchConstraintId id;
  SketchEntityId first;
  SketchEntityId second;
  auto operator<=>(const Perpendicular &) const = default;
};

enum class Tangency : std::uint8_t { External = 1, Internal = 2 };

struct Tangent {
  SketchConstraintId id;
  SketchEntityId first;
  SketchEntityId second;
  Tangency mode = Tangency::External;
  auto operator<=>(const Tangent &) const = default;
};

struct Concentric {
  SketchConstraintId id;
  SketchEntityId first;
  SketchEntityId second;
  auto operator<=>(const Concentric &) const = default;
};

struct Equal {
  SketchConstraintId id;
  SketchEntityId first;
  SketchEntityId second;
  auto operator<=>(const Equal &) const = default;
};

struct Midpoint {
  SketchConstraintId id;
  PointRef point;
  SketchEntityId line;
  auto operator<=>(const Midpoint &) const = default;
};

struct Fixed {
  SketchConstraintId id;
  SketchEntityId entity;
  auto operator<=>(const Fixed &) const = default;
};

struct Collinear {
  SketchConstraintId id;
  SketchEntityId first;
  SketchEntityId second;
  auto operator<=>(const Collinear &) const = default;
};

struct Distance {
  SketchConstraintId id;
  PointRef first;
  PointRef second;
  LengthValue value;
  auto operator<=>(const Distance &) const = default;
};

struct HorizontalDistance {
  SketchConstraintId id;
  PointRef first;
  PointRef second;
  LengthValue value;
  auto operator<=>(const HorizontalDistance &) const = default;
};

struct VerticalDistance {
  SketchConstraintId id;
  PointRef first;
  PointRef second;
  LengthValue value;
  auto operator<=>(const VerticalDistance &) const = default;
};

struct Radius {
  SketchConstraintId id;
  SketchEntityId curve;
  LengthValue value;
  auto operator<=>(const Radius &) const = default;
};

struct Diameter {
  SketchConstraintId id;
  SketchEntityId curve;
  LengthValue value;
  auto operator<=>(const Diameter &) const = default;
};

struct AngleBetween {
  SketchConstraintId id;
  SketchEntityId first;
  SketchEntityId second;
  AngleValue value;
  auto operator<=>(const AngleBetween &) const = default;
};

using Constraint =
    std::variant<Coincident, Horizontal, Vertical, Parallel, Perpendicular,
                 Tangent, Concentric, Equal, Midpoint, Fixed, Collinear,
                 Distance, HorizontalDistance, VerticalDistance, Radius,
                 Diameter, AngleBetween>;

struct Definition {
  ContentDigest sourceDigest;
  std::vector<Entity> entities;
  std::vector<Constraint> constraints;
  bool operator==(const Definition &) const = default;
};

struct NumericalProfile {
  double typicalLengthMeters = 0.1;
  double minimumLengthMeters = 1.0e-9;
  double maximumCoordinateMeters = 1.0e6;
  double lengthToleranceMeters = 1.0e-8;
  double angleToleranceRadians = 1.0e-9;
  double rankRelativeTolerance = 1.0e-9;
  std::uint32_t maximumIterations = 80;
  std::size_t maximumModeVariables = 512;
  std::size_t maximumRedundancyConstraints = 128;
  bool operator==(const NumericalProfile &) const = default;
};

struct DragTarget {
  PointRef point;
  Point2 target;
  bool operator==(const DragTarget &) const = default;
};

struct SolveInput {
  Definition definition;
  std::vector<Entity> priorSolution;
  std::optional<DragTarget> drag;
  NumericalProfile numerical;
  CancellationToken cancellation;
};

enum class SolveStatus : std::uint8_t {
  Solved = 1,
  Underconstrained = 2,
  Inconsistent = 3,
  Diverged = 4,
  Cancelled = 5,
};

struct ConstraintResidual {
  SketchConstraintId constraint;
  double normalizedMaximum = 0.0;
  bool satisfied = false;
  bool operator==(const ConstraintResidual &) const = default;
};

struct ConflictSet {
  std::vector<SketchConstraintId> constraints;
  bool exact = false;
  bool operator==(const ConflictSet &) const = default;
};

struct ModeComponent {
  SketchEntityId entity;
  std::vector<double> parameterDirection;
  bool operator==(const ModeComponent &) const = default;
};

struct FreedomMode {
  std::vector<ModeComponent> components;
  bool operator==(const FreedomMode &) const = default;
};

struct SolveResult {
  SolveStatus status = SolveStatus::Diverged;
  std::vector<Entity> geometry;
  std::size_t degreesOfFreedom = 0;
  std::vector<FreedomMode> modes;
  std::vector<ConstraintResidual> residuals;
  std::vector<SketchConstraintId> redundantConstraints;
  std::vector<ConflictSet> conflicts;
  std::vector<Diagnostic> diagnostics;
  std::uint32_t iterations = 0;
};

[[nodiscard]] SketchEntityId entityId(const Entity &entity);
[[nodiscard]] SketchConstraintId constraintId(const Constraint &constraint);
[[nodiscard]] std::size_t closedProfileCount(const Definition &definition);
[[nodiscard]] Result<void> validate(const Definition &definition,
                                    const NumericalProfile &profile);
[[nodiscard]] Result<std::vector<ConstraintResidual>>
evaluateResiduals(const Definition &definition,
                  const std::vector<Entity> &geometry,
                  const NumericalProfile &profile);

class Solver {
public:
  virtual ~Solver() = default;
  [[nodiscard]] virtual Result<SolveResult>
  solve(const SolveInput &input) const = 0;
};

} // namespace kearne::sketch
