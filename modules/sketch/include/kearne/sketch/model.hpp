#pragma once

#include <kearne/base/value.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace kearne::sketch {

using LengthValue = Quantity<Length>;
using AngleValue = Quantity<Angle>;
using DimensionlessValue = Quantity<Dimensionless>;

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

struct EllipseEntity {
  SketchEntityId id;
  Point2 center;
  LengthValue majorRadius;
  LengthValue minorRadius;
  AngleValue rotation;
  bool construction = false;
  auto operator<=>(const EllipseEntity &) const = default;
};

struct EllipticalArcEntity {
  SketchEntityId id;
  Point2 center;
  LengthValue majorRadius;
  LengthValue minorRadius;
  AngleValue rotation;
  AngleValue startParameter;
  AngleValue endParameter;
  bool construction = false;
  auto operator<=>(const EllipticalArcEntity &) const = default;
};

struct HyperbolicArcEntity {
  SketchEntityId id;
  Point2 center;
  LengthValue majorRadius;
  LengthValue minorRadius;
  AngleValue rotation;
  DimensionlessValue startParameter;
  DimensionlessValue endParameter;
  bool construction = false;
  auto operator<=>(const HyperbolicArcEntity &) const = default;
};

struct ParabolicArcEntity {
  SketchEntityId id;
  Point2 vertex;
  LengthValue focalLength;
  AngleValue rotation;
  LengthValue startParameter;
  LengthValue endParameter;
  bool construction = false;
  auto operator<=>(const ParabolicArcEntity &) const = default;
};

// Canonical finite NURBS representation. Knots are the full nondecreasing
// sequence, including repetitions, so every runtime evaluates the same curve.
struct BSplineEntity {
  SketchEntityId id;
  std::vector<Point2> controlPoints;
  std::vector<DimensionlessValue> knots;
  std::vector<DimensionlessValue> weights;
  std::uint32_t degree = 3U;
  bool periodic = false;
  bool construction = false;
  auto operator<=>(const BSplineEntity &) const = default;
};

using Entity =
    std::variant<PointEntity, LineEntity, CircleEntity, ArcEntity,
                 EllipseEntity, EllipticalArcEntity, HyperbolicArcEntity,
                 ParabolicArcEntity, BSplineEntity>;

enum class PointKey : std::uint8_t {
  Point = 1,
  Start = 2,
  End = 3,
  Center = 4,
  Major = 5,
  Minor = 6,
  Focus = 7,
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

struct PointOnObject {
  SketchConstraintId id;
  PointRef point;
  SketchEntityId curve;
  auto operator<=>(const PointOnObject &) const = default;
};

struct Symmetric {
  SketchConstraintId id;
  PointRef first;
  PointRef second;
  SketchEntityId axis;
  auto operator<=>(const Symmetric &) const = default;
};

struct SymmetricAboutPoint {
  SketchConstraintId id;
  PointRef first;
  PointRef second;
  PointRef center;
  auto operator<=>(const SymmetricAboutPoint &) const = default;
};

struct Lock {
  SketchConstraintId id;
  PointRef point;
  Point2 position;
  auto operator<=>(const Lock &) const = default;
};

struct Block {
  SketchConstraintId id;
  SketchEntityId entity;
  auto operator<=>(const Block &) const = default;
};

struct Group {
  SketchConstraintId id;
  std::vector<SketchEntityId> entities;
  auto operator<=>(const Group &) const = default;
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
                 Tangent, Concentric, Equal, Midpoint, Block, Group, Collinear,
                 PointOnObject, Symmetric, SymmetricAboutPoint, Lock, Distance,
                 HorizontalDistance, VerticalDistance, Radius, Diameter,
                 AngleBetween>;

enum class SketchObjectKind : std::uint8_t {
  Rectangle = 1,
  Point = 2,
  Line = 3,
  Circle = 4,
  Arc = 5,
  Slot = 6,
  ArcSlot = 7,
  Polyline = 8,
  RegularPolygon = 9,
  Oblong = 10,
  Ellipse = 11,
  EllipticalArc = 12,
  HyperbolicArc = 13,
  ParabolicArc = 14,
  BSpline = 15,
  Fillet = 16,
  Chamfer = 17,
  Offset = 18,
  JoinedCurve = 19,
};

struct SketchObjectMember {
  std::string role;
  SketchEntityId entity;
  auto operator<=>(const SketchObjectMember &) const = default;
};

struct SketchObject {
  SketchObjectId id;
  std::string label;
  SketchObjectKind kind = SketchObjectKind::Rectangle;
  std::vector<SketchObjectMember> members;
  bool operator==(const SketchObject &) const = default;
};

struct Definition {
  ContentDigest sourceDigest;
  std::vector<SketchObject> objects;
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
[[nodiscard]] std::vector<SketchEntityId>
constraintEntityIds(const Constraint &constraint);
[[nodiscard]] Result<Point2> resolvePoint(const Definition &definition,
                                          PointRef reference);
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
