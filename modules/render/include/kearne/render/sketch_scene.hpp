#pragma once

#include <kearne/base/value.hpp>
#include <kearne/sketch/model.hpp>

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stop_token>
#include <utility>
#include <variant>
#include <vector>

namespace kearne::render {

struct SceneDigestTag;

using ::kearne::EvaluationKey;
using SceneDigest = TypedDigest<SceneDigestTag>;

// Generations increase across every plane/evaluation target in one render
// session. A producer restart or counter reset allocates a new session handle.
class SceneGeneration final {
public:
  [[nodiscard]] static Result<SceneGeneration> create(std::uint64_t value);
  [[nodiscard]] std::uint64_t value() const { return value_; }
  auto operator<=>(const SceneGeneration &) const = default;

private:
  explicit SceneGeneration(std::uint64_t value) : value_(value) {}
  std::uint64_t value_;
};

// Presentation generations advance independently of evaluated scene content.
class SketchPresentationGeneration final {
public:
  [[nodiscard]] static Result<SketchPresentationGeneration>
  create(std::uint64_t value);
  [[nodiscard]] std::uint64_t value() const { return value_; }
  auto operator<=>(const SketchPresentationGeneration &) const = default;

private:
  explicit SketchPresentationGeneration(std::uint64_t value) : value_(value) {}
  std::uint64_t value_;
};

// Handles are meaningful only inside one render session. They are never
// semantic identity and must not enter project state or command payloads.
class RenderSessionHandle final {
public:
  [[nodiscard]] static Result<RenderSessionHandle> create(std::uint64_t value);
  [[nodiscard]] std::uint64_t value() const { return value_; }
  auto operator<=>(const RenderSessionHandle &) const = default;

private:
  explicit RenderSessionHandle(std::uint64_t value) : value_(value) {}
  std::uint64_t value_;
};

class SketchPrimitiveHandle final {
public:
  [[nodiscard]] static Result<SketchPrimitiveHandle>
  create(std::uint32_t value);
  [[nodiscard]] std::uint32_t value() const { return value_; }
  auto operator<=>(const SketchPrimitiveHandle &) const = default;

private:
  explicit SketchPrimitiveHandle(std::uint32_t value) : value_(value) {}
  std::uint32_t value_;
};

struct SceneTarget {
  RenderSessionHandle session;
  sketch::EvaluatedPlaneIdentity evaluatedPlane;
  EvaluationKey evaluation;
  bool operator==(const SceneTarget &) const = default;
};

struct SceneStamp {
  SceneTarget target;
  SceneGeneration generation;
  SceneDigest digest;
  bool operator==(const SceneStamp &) const = default;
};

struct Point2d {
  // Canonical sketch-plane coordinates in SI metres.
  double x = 0.0;
  double y = 0.0;
  auto operator<=>(const Point2d &) const = default;
};

struct Bounds2d {
  Point2d minimum;
  Point2d maximum;
  bool empty = true;
  bool operator==(const Bounds2d &) const = default;
};

enum class SketchPrimitiveKind : std::uint8_t {
  Point = 1,
  Line = 2,
  Circle = 3,
  Arc = 4,
};

enum class SketchStyleRole : std::uint8_t {
  Regular = 1,
  Construction = 2,
  Selected = 3,
  Preview = 4,
  Diagnostic = 5,
  Hovered = 6,
};

enum class SketchLinePattern : std::uint8_t {
  Solid = 1,
  Dashed = 2,
  Dotted = 3,
};

struct SketchStyle {
  SketchStyleRole role = SketchStyleRole::Regular;
  SketchLinePattern linePattern = SketchLinePattern::Solid;
  // Device-independent pixels; adapters apply device scale at presentation.
  float strokeWidthPixels = 1.0F;
  float pointDiameterPixels = 7.0F;
  std::uint16_t layer = 0;
  bool operator==(const SketchStyle &) const = default;
};

enum class SketchPrimitiveFlags : std::uint8_t {
  None = 0,
  Visible = 1U << 0U,
  Selectable = 1U << 1U,
  All = (1U << 0U) | (1U << 1U),
};

[[nodiscard]] constexpr SketchPrimitiveFlags
operator|(SketchPrimitiveFlags first, SketchPrimitiveFlags second) {
  return static_cast<SketchPrimitiveFlags>(static_cast<std::uint8_t>(first) |
                                           static_cast<std::uint8_t>(second));
}

[[nodiscard]] constexpr bool hasFlag(SketchPrimitiveFlags value,
                                     SketchPrimitiveFlags flag) {
  return (static_cast<std::uint8_t>(value) & static_cast<std::uint8_t>(flag)) !=
         0;
}

// Points occupy one contiguous array. Point uses one point; Line uses two;
// Circle and Arc use their center. Radius is in metres. Arc angles are radians
// with a signed sweep from start.
struct PackedSketchPrimitive {
  SketchEntityId entity;
  SketchPrimitiveHandle handle;
  std::uint32_t firstPoint = 0;
  std::uint16_t style = 0;
  SketchPrimitiveKind kind = SketchPrimitiveKind::Point;
  SketchPrimitiveFlags flags =
      SketchPrimitiveFlags::Visible | SketchPrimitiveFlags::Selectable;
  double radius = 0.0;
  double startAngleRadians = 0.0;
  double sweepAngleRadians = 0.0;
  bool operator==(const PackedSketchPrimitive &) const = default;
};

class SketchSceneSnapshot final {
public:
  [[nodiscard]] static Result<SketchSceneSnapshot>
  create(SceneStamp stamp, std::vector<SketchStyle> styles,
         std::vector<Point2d> points,
         std::vector<PackedSketchPrimitive> primitives);

  [[nodiscard]] const SceneStamp &stamp() const { return stamp_; }
  [[nodiscard]] const Bounds2d &bounds() const { return bounds_; }
  [[nodiscard]] std::span<const SketchStyle> styles() const { return styles_; }
  [[nodiscard]] std::span<const Point2d> points() const { return points_; }
  [[nodiscard]] std::span<const PackedSketchPrimitive> primitives() const {
    return primitives_;
  }
  [[nodiscard]] const PackedSketchPrimitive *
  findPrimitive(SketchEntityId entity) const;
  // Payload bytes for the immutable semantic lookup table. Allocator metadata
  // is excluded.
  [[nodiscard]] std::size_t semanticIndexBytes() const;

private:
  SketchSceneSnapshot(SceneStamp stamp, Bounds2d bounds,
                      std::vector<SketchStyle> styles,
                      std::vector<Point2d> points,
                      std::vector<PackedSketchPrimitive> primitives,
                      std::vector<std::uint32_t> semanticIndex);

  SceneStamp stamp_;
  Bounds2d bounds_;
  std::vector<SketchStyle> styles_;
  std::vector<Point2d> points_;
  std::vector<PackedSketchPrimitive> primitives_;
  std::vector<std::uint32_t> semanticIndex_;
};

struct SketchProjectionStyles {
  SketchStyle regular;
  SketchStyle construction;
  bool operator==(const SketchProjectionStyles &) const = default;
};

[[nodiscard]] SketchProjectionStyles defaultSketchProjectionStyles();

[[nodiscard]] Result<SketchSceneSnapshot> projectSketchScene(
    SceneStamp stamp, std::span<const sketch::Entity> geometry,
    SketchProjectionStyles styles = defaultSketchProjectionStyles());

[[nodiscard]] std::optional<Point2d>
semanticPoint(const SketchSceneSnapshot &scene,
              const PackedSketchPrimitive &primitive, sketch::PointKey key);

enum class SketchOverlayRole : std::uint8_t {
  Hovered = 1,
  Selected = 2,
  Preview = 3,
  Diagnostic = 4,
};

struct SketchOverlayScope {
  SketchEntityId entity;
  std::optional<sketch::PointKey> point;
  auto operator<=>(const SketchOverlayScope &) const = default;
};

struct SketchOverlayRoleSetDigest {
  std::array<std::uint8_t, 32> bytes{};
  auto operator<=>(const SketchOverlayRoleSetDigest &) const = default;
};

struct SketchOverlayDigest {
  std::array<std::uint8_t, 32> bytes{};
  auto operator<=>(const SketchOverlayDigest &) const = default;
};

struct SketchOverlayRoleSetLimits {
  // Payload limits exclude the caller-owned input span, stack values, and
  // allocator metadata.
  std::size_t maximumScopeCount = 1'000'000U;
  std::size_t maximumInputBytes = 64U * 1024U * 1024U;
  std::size_t maximumRetainedBytes = 64U * 1024U * 1024U;
  std::size_t maximumScratchBytes = 128U * 1024U * 1024U;
  std::size_t maximumPeakBuildBytes = 192U * 1024U * 1024U;
};

// One canonical immutable role payload. Independently retained sets let a
// one-entry hover replacement reuse a large stable selection without copying,
// sorting, hashing, or comparing it.
class SketchOverlayRoleSet final {
public:
  [[nodiscard]] static Result<std::shared_ptr<const SketchOverlayRoleSet>>
  create(std::shared_ptr<const SketchSceneSnapshot> base,
         SketchOverlayRole role, std::span<const SketchOverlayScope> scopes,
         SketchOverlayRoleSetLimits limits = {});
  [[nodiscard]] static Result<std::shared_ptr<const SketchOverlayRoleSet>>
  create(std::shared_ptr<const SketchSceneSnapshot> base,
         SketchOverlayRole role, std::span<const SketchOverlayScope> scopes,
         SketchOverlayRoleSetLimits limits, std::stop_token cancellation);

  [[nodiscard]] const std::shared_ptr<const SketchSceneSnapshot> &base() const {
    return base_;
  }
  [[nodiscard]] SketchOverlayRole role() const { return role_; }
  [[nodiscard]] const SketchOverlayRoleSetDigest &digest() const {
    return digest_;
  }
  [[nodiscard]] std::span<const SketchOverlayScope> scopes() const {
    return scopes_;
  }
  [[nodiscard]] bool contains(SketchOverlayScope scope) const;
  [[nodiscard]] std::size_t inputBytes() const { return inputBytes_; }
  [[nodiscard]] std::size_t retainedBytes() const { return retainedBytes_; }
  [[nodiscard]] std::size_t scratchBytes() const { return scratchBytes_; }
  [[nodiscard]] std::size_t peakBuildBytes() const { return peakBuildBytes_; }

private:
  SketchOverlayRoleSet(std::shared_ptr<const SketchSceneSnapshot> base,
                       SketchOverlayRole role,
                       SketchOverlayRoleSetDigest digest,
                       std::vector<SketchOverlayScope> scopes,
                       std::size_t inputBytes, std::size_t retainedBytes,
                       std::size_t scratchBytes, std::size_t peakBuildBytes);

  std::shared_ptr<const SketchSceneSnapshot> base_;
  SketchOverlayRole role_;
  SketchOverlayRoleSetDigest digest_;
  std::vector<SketchOverlayScope> scopes_;
  std::size_t inputBytes_ = 0;
  std::size_t retainedBytes_ = 0;
  std::size_t scratchBytes_ = 0;
  std::size_t peakBuildBytes_ = 0;
};

using SketchOverlayRoleSetPtr = std::shared_ptr<const SketchOverlayRoleSet>;

// An overlay retains its exact immutable base and exactly one immutable set
// for each role. It never owns geometry, pick data, or tessellation data.
class SketchPresentationOverlay final {
public:
  [[nodiscard]] static Result<std::shared_ptr<const SketchPresentationOverlay>>
  create(std::shared_ptr<const SketchSceneSnapshot> base,
         SketchPresentationGeneration generation,
         std::span<const SketchOverlayRoleSetPtr> roleSets);

  [[nodiscard]] const std::shared_ptr<const SketchSceneSnapshot> &base() const {
    return base_;
  }
  [[nodiscard]] SketchPresentationGeneration generation() const {
    return generation_;
  }
  [[nodiscard]] const SketchOverlayDigest &payloadDigest() const {
    return payloadDigest_;
  }
  [[nodiscard]] std::span<const SketchOverlayRoleSetPtr, 4> roleSets() const {
    return roleSets_;
  }
  [[nodiscard]] SketchOverlayRoleSetPtr roleSet(SketchOverlayRole role) const;
  [[nodiscard]] std::optional<SketchStyleRole>
  resolve(SketchOverlayScope scope) const;

private:
  SketchPresentationOverlay(std::shared_ptr<const SketchSceneSnapshot> base,
                            SketchPresentationGeneration generation,
                            SketchOverlayDigest payloadDigest,
                            std::array<SketchOverlayRoleSetPtr, 4> roleSets);

  std::shared_ptr<const SketchSceneSnapshot> base_;
  SketchPresentationGeneration generation_;
  SketchOverlayDigest payloadDigest_;
  std::array<SketchOverlayRoleSetPtr, 4> roleSets_;
};

enum class SketchOverlayDecision : std::uint8_t {
  Accepted = 1,
  Duplicate = 2,
  StaleScene = 3,
  StaleGeneration = 4,
  GenerationConflict = 5,
};

// Retains only the latest presentation state for one exact scene stamp.
class LatestSketchPresentation final {
public:
  explicit LatestSketchPresentation(SceneStamp scene);
  LatestSketchPresentation(const LatestSketchPresentation &) = delete;
  LatestSketchPresentation &
  operator=(const LatestSketchPresentation &) = delete;

  void retarget(SceneStamp scene);
  [[nodiscard]] Result<SketchOverlayDecision>
  publish(std::shared_ptr<const SketchPresentationOverlay> overlay);
  [[nodiscard]] std::shared_ptr<const SketchPresentationOverlay> latest() const;
  [[nodiscard]] std::size_t retainedCount() const;

private:
  mutable std::mutex mutex_;
  SceneStamp scene_;
  std::shared_ptr<const SketchPresentationOverlay> latest_;
};

struct SketchProvisionalDigestTag;
using SketchProvisionalDigest = TypedDigest<SketchProvisionalDigestTag>;

// These handles are process-local interaction identity. They never enter
// source, project state, evaluated geometry, or persistent selection.
class SketchEditSessionHandle final {
public:
  [[nodiscard]] static Result<SketchEditSessionHandle>
  create(std::uint64_t value);
  [[nodiscard]] std::uint64_t value() const { return value_; }
  auto operator<=>(const SketchEditSessionHandle &) const = default;

private:
  explicit SketchEditSessionHandle(std::uint64_t value) : value_(value) {}
  std::uint64_t value_;
};

class SketchToolInstanceHandle final {
public:
  [[nodiscard]] static Result<SketchToolInstanceHandle>
  create(std::uint64_t value);
  [[nodiscard]] std::uint64_t value() const { return value_; }
  auto operator<=>(const SketchToolInstanceHandle &) const = default;

private:
  explicit SketchToolInstanceHandle(std::uint64_t value) : value_(value) {}
  std::uint64_t value_;
};

class SketchProvisionalGeneration final {
public:
  [[nodiscard]] static Result<SketchProvisionalGeneration>
  create(std::uint64_t value);
  [[nodiscard]] std::uint64_t value() const { return value_; }
  auto operator<=>(const SketchProvisionalGeneration &) const = default;

private:
  explicit SketchProvisionalGeneration(std::uint64_t value) : value_(value) {}
  std::uint64_t value_;
};

class SketchProvisionalPrimitiveHandle final {
public:
  [[nodiscard]] static Result<SketchProvisionalPrimitiveHandle>
  create(std::uint32_t value);
  [[nodiscard]] std::uint32_t value() const { return value_; }
  auto operator<=>(const SketchProvisionalPrimitiveHandle &) const = default;

private:
  explicit SketchProvisionalPrimitiveHandle(std::uint32_t value)
      : value_(value) {}
  std::uint32_t value_;
};

struct SketchProvisionalTarget {
  SceneStamp base;
  SketchEditSessionHandle editSession;
  SketchToolInstanceHandle toolInstance;
  bool operator==(const SketchProvisionalTarget &) const = default;
};

struct SketchProvisionalStamp {
  SketchProvisionalTarget target;
  SketchProvisionalGeneration generation;
  SketchProvisionalDigest payload;
  bool operator==(const SketchProvisionalStamp &) const = default;
};

enum class SketchProvisionalClassification : std::uint8_t {
  Regular = 1,
  Construction = 2,
};

// Geometry is canonical sketch-plane SI metres. Point, circle, and arc use one
// point; line uses two. Circle/arc radius is metres and arc angles are radians.
// Unused point slots and unused curve parameters must be zero.
struct PackedSketchProvisionalPrimitive {
  SketchProvisionalPrimitiveHandle handle;
  std::array<Point2d, 2> points;
  std::uint8_t pointCount = 0;
  SketchPrimitiveKind kind = SketchPrimitiveKind::Point;
  SketchProvisionalClassification classification =
      SketchProvisionalClassification::Regular;
  double radius = 0.0;
  double startAngleRadians = 0.0;
  double sweepAngleRadians = 0.0;
  bool operator==(const PackedSketchProvisionalPrimitive &) const = default;
};

struct SketchProvisionalLimits {
  // Exact payload accounting excludes caller-owned input and allocator
  // metadata. Peak build bytes are retained plus temporary sort storage.
  std::size_t maximumInputBytes = 64U * 1024U * 1024U;
  std::size_t maximumRetainedBytes = 64U * 1024U * 1024U;
  std::size_t maximumScratchBytes = 64U * 1024U * 1024U;
  std::size_t maximumPeakBuildBytes = 128U * 1024U * 1024U;
};

// Provisional geometry is immutable, latest-only interaction state. It has no
// SketchEntityId, evaluated scene generation/digest, or persistent pick index.
class SketchProvisionalGeometry final {
public:
  [[nodiscard]] static Result<std::shared_ptr<const SketchProvisionalGeometry>>
  create(SketchProvisionalStamp stamp,
         std::span<const PackedSketchProvisionalPrimitive> primitives,
         SketchProvisionalLimits limits = {});
  [[nodiscard]] static Result<std::shared_ptr<const SketchProvisionalGeometry>>
  create(SketchProvisionalStamp stamp,
         std::span<const PackedSketchProvisionalPrimitive> primitives,
         SketchProvisionalLimits limits, std::stop_token cancellation);

  [[nodiscard]] const SketchProvisionalStamp &stamp() const { return stamp_; }
  [[nodiscard]] std::span<const PackedSketchProvisionalPrimitive>
  primitives() const {
    return primitives_;
  }
  [[nodiscard]] const PackedSketchProvisionalPrimitive *
  findPrimitive(SketchProvisionalPrimitiveHandle handle) const;
  [[nodiscard]] std::size_t inputBytes() const { return inputBytes_; }
  [[nodiscard]] std::size_t retainedBytes() const { return retainedBytes_; }
  [[nodiscard]] std::size_t scratchBytes() const { return scratchBytes_; }
  [[nodiscard]] std::size_t peakBuildBytes() const { return peakBuildBytes_; }

private:
  SketchProvisionalGeometry(
      SketchProvisionalStamp stamp,
      std::vector<PackedSketchProvisionalPrimitive> primitives,
      std::size_t inputBytes, std::size_t retainedBytes,
      std::size_t scratchBytes, std::size_t peakBuildBytes);

  SketchProvisionalStamp stamp_;
  std::vector<PackedSketchProvisionalPrimitive> primitives_;
  std::size_t inputBytes_ = 0;
  std::size_t retainedBytes_ = 0;
  std::size_t scratchBytes_ = 0;
  std::size_t peakBuildBytes_ = 0;
};

enum class SketchProvisionalDecision : std::uint8_t {
  Accepted = 1,
  Duplicate = 2,
  StaleTarget = 3,
  StaleGeneration = 4,
  GenerationConflict = 5,
};

class LatestSketchProvisionalGeometry final {
public:
  explicit LatestSketchProvisionalGeometry(SketchProvisionalTarget target);
  LatestSketchProvisionalGeometry(const LatestSketchProvisionalGeometry &) =
      delete;
  LatestSketchProvisionalGeometry &
  operator=(const LatestSketchProvisionalGeometry &) = delete;

  void retarget(SketchProvisionalTarget target);
  [[nodiscard]] Result<SketchProvisionalDecision>
  publish(std::shared_ptr<const SketchProvisionalGeometry> geometry);
  [[nodiscard]] std::shared_ptr<const SketchProvisionalGeometry> latest() const;
  [[nodiscard]] std::size_t retainedCount() const;

private:
  mutable std::mutex mutex_;
  SketchProvisionalTarget target_;
  std::shared_ptr<const SketchProvisionalGeometry> latest_;
};

struct SketchMarkerDigestTag;
using SketchMarkerDigest = TypedDigest<SketchMarkerDigestTag>;

class SketchMarkerGeneration final {
public:
  [[nodiscard]] static Result<SketchMarkerGeneration>
  create(std::uint64_t value);
  [[nodiscard]] std::uint64_t value() const { return value_; }
  auto operator<=>(const SketchMarkerGeneration &) const = default;

private:
  explicit SketchMarkerGeneration(std::uint64_t value) : value_(value) {}
  std::uint64_t value_;
};

// Producers advance this for every plane-to-screen transform, viewport, device
// scale, or screen-space snap tolerance change.
class SketchMarkerViewGeneration final {
public:
  [[nodiscard]] static Result<SketchMarkerViewGeneration>
  create(std::uint64_t value);
  [[nodiscard]] std::uint64_t value() const { return value_; }
  auto operator<=>(const SketchMarkerViewGeneration &) const = default;

private:
  explicit SketchMarkerViewGeneration(std::uint64_t value) : value_(value) {}
  std::uint64_t value_;
};

class SketchMarkerHandle final {
public:
  [[nodiscard]] static Result<SketchMarkerHandle> create(std::uint32_t value);
  [[nodiscard]] std::uint32_t value() const { return value_; }
  auto operator<=>(const SketchMarkerHandle &) const = default;

private:
  explicit SketchMarkerHandle(std::uint32_t value) : value_(value) {}
  std::uint32_t value_;
};

struct SketchProvisionalReference {
  SketchProvisionalGeneration generation;
  SketchProvisionalDigest payload;
  bool operator==(const SketchProvisionalReference &) const = default;
};

struct SketchMarkerInteraction {
  SketchEditSessionHandle editSession;
  SketchToolInstanceHandle toolInstance;
  bool operator==(const SketchMarkerInteraction &) const = default;
};

struct SketchMarkerTarget {
  SceneStamp base;
  // create() requires these dependencies exactly when used: interaction for
  // provisional anchors or inference/snap, provisional for provisional
  // anchors, and view for inference/snap.
  std::optional<SketchMarkerInteraction> interaction;
  std::optional<SketchProvisionalReference> provisional;
  std::optional<SketchMarkerViewGeneration> view;
  bool operator==(const SketchMarkerTarget &) const = default;
};

struct SketchMarkerStamp {
  SketchMarkerTarget target;
  SketchMarkerGeneration generation;
  SketchMarkerDigest payload;
  bool operator==(const SketchMarkerStamp &) const = default;
};

struct SketchMarkerPointLocation {
  sketch::PointKey point;
  bool operator==(const SketchMarkerPointLocation &) const = default;
};

struct SketchMarkerCurveLocation {
  // Zero and one are the curve's exact canonical endpoints. A circle uses one
  // full counterclockwise turn; an arc follows its signed sweep.
  double normalizedParameter = 0.0;
  bool operator==(const SketchMarkerCurveLocation &) const = default;
};

using SketchMarkerPrimitiveLocation =
    std::variant<SketchMarkerPointLocation, SketchMarkerCurveLocation>;

struct SketchBaseMarkerAnchor {
  SketchEntityId entity;
  SketchMarkerPrimitiveLocation location;
  bool operator==(const SketchBaseMarkerAnchor &) const = default;
};

struct SketchProvisionalMarkerAnchor {
  SketchProvisionalPrimitiveHandle primitive;
  SketchMarkerPrimitiveLocation location;
  bool operator==(const SketchProvisionalMarkerAnchor &) const = default;
};

struct SketchCanonicalMarkerAnchor {
  Point2d point;
  bool operator==(const SketchCanonicalMarkerAnchor &) const = default;
};

using SketchMarkerAnchor =
    std::variant<SketchBaseMarkerAnchor, SketchProvisionalMarkerAnchor,
                 SketchCanonicalMarkerAnchor>;

// Resolves one validated marker anchor to canonical sketch-plane SI metres.
// Transient provisional handles remain scoped to the supplied dependency.
[[nodiscard]] Result<Point2d> resolveSketchMarkerAnchor(
    const SketchMarkerAnchor &anchor, const SketchSceneSnapshot &base,
    const SketchProvisionalGeometry *provisional = nullptr);

// Marker kinds carry only render semantics. Constraint and inference truth
// remains in the sketch/solver state referenced by the exact marker stamp.
enum class SketchMarkerKind : std::uint8_t {
  CoincidentConstraint = 1,
  HorizontalConstraint,
  VerticalConstraint,
  ParallelConstraint,
  PerpendicularConstraint,
  TangentConstraint,
  EqualConstraint,
  ConcentricConstraint,
  MidpointConstraint,
  FixedConstraint,
  CollinearConstraint,
  HorizontalInference = 32,
  VerticalInference,
  ParallelInference,
  PerpendicularInference,
  TangentInference,
  CollinearInference,
  TranslationDegreeOfFreedom = 64,
  RotationDegreeOfFreedom,
  DistanceDimension = 96,
  HorizontalDistanceDimension,
  VerticalDistanceDimension,
  RadiusDimension,
  DiameterDimension,
  AngleDimension,
  EndpointSnap = 128,
  MidpointSnap,
  CenterSnap,
  IntersectionSnap,
  QuadrantSnap,
  GridSnap,
};

enum class SketchMarkerCategory : std::uint8_t {
  Constraint = 1,
  Inference = 2,
  DegreeOfFreedom = 3,
  Dimension = 4,
  SnapCursor = 5,
};

[[nodiscard]] std::optional<SketchMarkerCategory>
markerCategory(SketchMarkerKind kind);

// Anchors are packed in a separate contiguous array. Dimension values use SI
// metres or radians according to kind; every other kind requires zero.
struct PackedSketchMarker {
  SketchMarkerHandle handle;
  // Canonical edit/delete identity, distinct from the transient marker handle.
  std::optional<SketchConstraintId> constraint;
  std::uint32_t firstAnchor = 0;
  std::uint8_t anchorCount = 0;
  SketchMarkerKind kind = SketchMarkerKind::CoincidentConstraint;
  double valueSi = 0.0;
  bool operator==(const PackedSketchMarker &) const = default;
};

struct SketchMarkerLimits {
  // Payload accounting excludes caller-owned spans and allocator metadata.
  std::size_t maximumMarkerCount = 1'000'000U;
  std::size_t maximumAnchorCount = 3'000'000U;
  std::size_t maximumInputBytes = 128U * 1024U * 1024U;
  std::size_t maximumRetainedBytes = 128U * 1024U * 1024U;
  std::size_t maximumScratchBytes = 64U * 1024U * 1024U;
  std::size_t maximumPeakBuildBytes = 192U * 1024U * 1024U;
};

// This immutable packet owns annotations, never evaluated or provisional
// geometry. It retains the exact dependencies used to validate its anchors.
class SketchMarkerPacket final {
public:
  [[nodiscard]] static Result<std::shared_ptr<const SketchMarkerPacket>>
  create(SketchMarkerStamp stamp,
         std::shared_ptr<const SketchSceneSnapshot> base,
         std::shared_ptr<const SketchProvisionalGeometry> provisional,
         std::span<const SketchMarkerAnchor> anchors,
         std::span<const PackedSketchMarker> markers,
         SketchMarkerLimits limits = {});
  [[nodiscard]] static Result<std::shared_ptr<const SketchMarkerPacket>>
  create(SketchMarkerStamp stamp,
         std::shared_ptr<const SketchSceneSnapshot> base,
         std::shared_ptr<const SketchProvisionalGeometry> provisional,
         std::span<const SketchMarkerAnchor> anchors,
         std::span<const PackedSketchMarker> markers, SketchMarkerLimits limits,
         std::stop_token cancellation);

  [[nodiscard]] const SketchMarkerStamp &stamp() const { return stamp_; }
  [[nodiscard]] const std::shared_ptr<const SketchSceneSnapshot> &base() const {
    return base_;
  }
  [[nodiscard]] const std::shared_ptr<const SketchProvisionalGeometry> &
  provisional() const {
    return provisional_;
  }
  [[nodiscard]] std::span<const SketchMarkerAnchor> anchors() const {
    return anchors_;
  }
  [[nodiscard]] std::span<const PackedSketchMarker> markers() const {
    return markers_;
  }
  [[nodiscard]] const PackedSketchMarker *
  findMarker(SketchMarkerHandle handle) const;
  [[nodiscard]] const PackedSketchMarker *
  findConstraint(SketchConstraintId constraint) const;
  [[nodiscard]] std::span<const SketchMarkerAnchor>
  markerAnchors(SketchMarkerHandle marker) const;
  [[nodiscard]] std::size_t inputBytes() const { return inputBytes_; }
  [[nodiscard]] std::size_t retainedBytes() const { return retainedBytes_; }
  [[nodiscard]] std::size_t scratchBytes() const { return scratchBytes_; }
  [[nodiscard]] std::size_t peakBuildBytes() const { return peakBuildBytes_; }

private:
  SketchMarkerPacket(
      SketchMarkerStamp stamp, std::shared_ptr<const SketchSceneSnapshot> base,
      std::shared_ptr<const SketchProvisionalGeometry> provisional,
      std::vector<SketchMarkerAnchor> anchors,
      std::vector<PackedSketchMarker> markers,
      std::vector<std::uint32_t> constraintIndex, std::size_t inputBytes,
      std::size_t retainedBytes, std::size_t scratchBytes,
      std::size_t peakBuildBytes);

  SketchMarkerStamp stamp_;
  std::shared_ptr<const SketchSceneSnapshot> base_;
  std::shared_ptr<const SketchProvisionalGeometry> provisional_;
  std::vector<SketchMarkerAnchor> anchors_;
  std::vector<PackedSketchMarker> markers_;
  std::vector<std::uint32_t> constraintIndex_;
  std::size_t inputBytes_ = 0;
  std::size_t retainedBytes_ = 0;
  std::size_t scratchBytes_ = 0;
  std::size_t peakBuildBytes_ = 0;
};

enum class SketchMarkerDecision : std::uint8_t {
  Accepted = 1,
  Duplicate = 2,
  StaleTarget = 3,
  StaleGeneration = 4,
  GenerationConflict = 5,
};

class LatestSketchMarkerPacket final {
public:
  explicit LatestSketchMarkerPacket(SketchMarkerTarget target);
  LatestSketchMarkerPacket(const LatestSketchMarkerPacket &) = delete;
  LatestSketchMarkerPacket &
  operator=(const LatestSketchMarkerPacket &) = delete;

  void retarget(SketchMarkerTarget target);
  [[nodiscard]] Result<SketchMarkerDecision>
  publish(std::shared_ptr<const SketchMarkerPacket> packet);
  [[nodiscard]] std::shared_ptr<const SketchMarkerPacket> latest() const;
  [[nodiscard]] std::size_t retainedCount() const;

private:
  mutable std::mutex mutex_;
  SketchMarkerTarget target_;
  std::shared_ptr<const SketchMarkerPacket> latest_;
};

struct SketchPrimitiveBatch {
  std::vector<Point2d> points;
  std::vector<PackedSketchPrimitive> primitives;
};

// A delta is cumulative within one session and attachment binding relative to
// its exact base stamp. Upserts replace by session-local primitive handle.
class SketchSceneDelta final {
public:
  [[nodiscard]] static Result<SketchSceneDelta>
  create(SceneStamp base, SceneStamp target,
         std::optional<std::vector<SketchStyle>> replacementStyles,
         std::vector<SketchPrimitiveHandle> removed,
         SketchPrimitiveBatch upserts);

  [[nodiscard]] const SceneStamp &base() const { return base_; }
  [[nodiscard]] const SceneStamp &target() const { return target_; }
  [[nodiscard]] const std::optional<std::vector<SketchStyle>> &
  replacementStyles() const {
    return replacementStyles_;
  }
  [[nodiscard]] std::span<const SketchPrimitiveHandle> removed() const {
    return removed_;
  }
  [[nodiscard]] const SketchPrimitiveBatch &upserts() const { return upserts_; }

private:
  SketchSceneDelta(SceneStamp base, SceneStamp target,
                   std::optional<std::vector<SketchStyle>> replacementStyles,
                   std::vector<SketchPrimitiveHandle> removed,
                   SketchPrimitiveBatch upserts);

  SceneStamp base_;
  SceneStamp target_;
  std::optional<std::vector<SketchStyle>> replacementStyles_;
  std::vector<SketchPrimitiveHandle> removed_;
  SketchPrimitiveBatch upserts_;
};

[[nodiscard]] Result<std::shared_ptr<const SketchSceneSnapshot>>
applySceneDelta(const SketchSceneSnapshot &base, const SketchSceneDelta &delta);

class SketchSceneEnvelope final {
public:
  using Full = std::shared_ptr<const SketchSceneSnapshot>;
  using Delta = std::shared_ptr<const SketchSceneDelta>;

  [[nodiscard]] static Result<SketchSceneEnvelope> full(Full snapshot);
  [[nodiscard]] static Result<SketchSceneEnvelope> delta(Delta delta);

  [[nodiscard]] bool isFull() const {
    return std::holds_alternative<Full>(data_);
  }
  [[nodiscard]] const SceneStamp &target() const;
  [[nodiscard]] std::optional<SceneStamp> base() const;
  [[nodiscard]] const Full &snapshot() const;
  [[nodiscard]] const Delta &sceneDelta() const;

private:
  explicit SketchSceneEnvelope(std::variant<Full, Delta> data)
      : data_(std::move(data)) {}
  std::variant<Full, Delta> data_;
};

enum class SceneEnvelopeDecision : std::uint8_t {
  AcceptFull = 1,
  AcceptDelta = 2,
  Duplicate = 3,
  StaleTarget = 4,
  StaleGeneration = 5,
  GenerationConflict = 6,
  MissingBase = 7,
  BaseMismatch = 8,
  GenerationGap = 9,
};

[[nodiscard]] SceneEnvelopeDecision
assessSceneEnvelope(const SceneTarget &desired,
                    const std::optional<SceneStamp> &installed,
                    const SketchSceneEnvelope &envelope);

struct SceneOffer {
  SceneEnvelopeDecision decision;
  bool replacedPending = false;
};

// The mailbox retains at most one pending immutable snapshot. Accepted deltas
// are materialized against the installed or pending base, so intermediate
// generations never form an unbounded render-thread queue.
class LatestSketchSceneMailbox final {
public:
  explicit LatestSketchSceneMailbox(SceneTarget desired);
  LatestSketchSceneMailbox(const LatestSketchSceneMailbox &) = delete;
  LatestSketchSceneMailbox &
  operator=(const LatestSketchSceneMailbox &) = delete;

  void retarget(SceneTarget desired);
  [[nodiscard]] Result<SceneOffer> offer(const SketchSceneEnvelope &envelope);
  [[nodiscard]] std::shared_ptr<const SketchSceneSnapshot> takeLatest();
  [[nodiscard]] std::shared_ptr<const SketchSceneSnapshot> installed() const;
  [[nodiscard]] std::size_t pendingCount() const;

private:
  mutable std::mutex mutex_;
  SceneTarget desired_;
  std::shared_ptr<const SketchSceneSnapshot> installed_;
  std::shared_ptr<const SketchSceneSnapshot> pending_;
};

enum class SketchPickTargets : std::uint8_t {
  None = 0,
  Points = 1U << 0U,
  Curves = 1U << 1U,
  All = 3,
};

[[nodiscard]] constexpr SketchPickTargets operator|(SketchPickTargets first,
                                                    SketchPickTargets second) {
  return static_cast<SketchPickTargets>(static_cast<std::uint8_t>(first) |
                                        static_cast<std::uint8_t>(second));
}

[[nodiscard]] constexpr bool hasTarget(SketchPickTargets value,
                                       SketchPickTargets target) {
  return (static_cast<std::uint8_t>(value) &
          static_cast<std::uint8_t>(target)) != 0;
}

struct SketchPickQuery {
  Point2d point;
  double tolerance = 0.0;
  SketchPickTargets targets = SketchPickTargets::All;
};

struct SketchPickResult {
  SceneStamp scene;
  SketchEntityId entity;
  SketchPrimitiveHandle primitive;
  std::optional<sketch::PointKey> pointKey;
  Point2d closestPoint;
  double distance = 0.0;
  bool operator==(const SketchPickResult &) const = default;
};

struct SketchPickIndexOptions {
  // Payload limits cover Data and requested packed arrays. Allocator metadata
  // is implementation-owned and excluded.
  std::size_t maximumRetainedBytes = 64U * 1024U * 1024U;
  std::size_t maximumScratchBytes = 128U * 1024U * 1024U;
  std::size_t maximumPeakBuildBytes = 192U * 1024U * 1024U;
  std::uint32_t maximumLeafTargets = 8;
  std::uint32_t maximumVisitedNodesPerPass = 4096;
  std::uint32_t maximumRefinedTargetsPerPass = 1024;
};

struct SketchPickMetrics {
  std::uint32_t visitedNodes = 0;
  std::uint32_t refinedTargets = 0;
  std::uint8_t passes = 0;
  bool operator==(const SketchPickMetrics &) const = default;
};

enum class SketchPickStatus : std::uint8_t {
  Hit = 1,
  Miss = 2,
  WorkBudgetExceeded = 3,
  InvalidQuery = 4,
  NonFiniteArithmetic = 5,
};

struct SketchPickOutcome {
  SketchPickStatus status = SketchPickStatus::Miss;
  std::optional<SketchPickResult> result;
  SketchPickMetrics metrics;
  // Canonical metres used for winner ordering. Without an eligibility
  // evaluator this equals result.distance; analytic evidence remains intact.
  std::optional<double> rankingDistance;
};

struct SketchPickQueryWorkspace {
  std::span<std::uint32_t> nodeStack;
};

enum class SketchPickEligibilityDecision : std::uint8_t {
  Eligible = 1,
  Ineligible = 2,
  WorkBudgetExceeded = 3,
  NonFiniteArithmetic = 4,
};

struct SketchPickEligibility {
  struct Evaluation {
    SketchPickEligibilityDecision decision =
        SketchPickEligibilityDecision::Ineligible;
    double rankingDistance = 0.0;
  };
  using Evaluate = Evaluation (*)(void *context,
                                  const SketchPickResult &candidate) noexcept;

  void *context = nullptr;
  Evaluate evaluate = nullptr;
};

class SketchPickIndex final {
public:
  static constexpr std::size_t recommendedQueryStackCapacity = 128U;

  [[nodiscard]] static Result<SketchPickIndex>
  build(std::shared_ptr<const SketchSceneSnapshot> scene,
        SketchPickIndexOptions options = {});
  [[nodiscard]] static Result<SketchPickIndex>
  build(std::shared_ptr<const SketchSceneSnapshot> scene,
        SketchPickIndexOptions options, std::stop_token cancellation);
  [[nodiscard]] Result<std::optional<SketchPickResult>>
  pick(const SketchPickQuery &query) const;
  // The allocation-free query boundary reports refusal as data. `pick` is the
  // compatibility adapter that maps non-completion to a Diagnostic.
  [[nodiscard]] SketchPickOutcome query(const SketchPickQuery &query) const;
  [[nodiscard]] SketchPickOutcome
  query(const SketchPickQuery &query, SketchPickQueryWorkspace workspace,
        SketchPickEligibility eligibility = {}) const;
  [[nodiscard]] const SketchSceneSnapshot &scene() const;
  [[nodiscard]] std::size_t leafCount() const;
  [[nodiscard]] std::size_t nodeCount() const;
  [[nodiscard]] std::size_t targetCount() const;
  // These values report the same payload accounting enforced by the options.
  [[nodiscard]] std::size_t retainedBytes() const;
  [[nodiscard]] std::size_t scratchBytes() const;
  [[nodiscard]] std::size_t peakBuildBytes() const;
  [[nodiscard]] std::size_t indexedReferenceCount() const;

private:
  struct Data;
  explicit SketchPickIndex(std::shared_ptr<const Data> data)
      : data_(std::move(data)) {}
  std::shared_ptr<const Data> data_;
};

} // namespace kearne::render
