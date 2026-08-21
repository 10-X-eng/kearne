#pragma once

#include <kearne/sketch/transform.hpp>

#include <array>
#include <vector>

namespace kearne::sketch {

enum class CornerEditKind : std::uint8_t { Fillet = 1, Chamfer = 2 };

struct CurvePick {
  SketchEntityId entity;
  Point2 reference;
};

struct CornerEditIds {
  SketchObjectId object;
  SketchEntityId curve;
  std::array<SketchConstraintId, 5> constraints;
};

struct CornerEdit {
  CornerEditKind kind = CornerEditKind::Fillet;
  CurvePick first;
  CurvePick second;
  LengthValue size;
  CornerEditIds output;
  ExternalConstraintPolicy constraints = ExternalConstraintPolicy::Refuse;
};

[[nodiscard]] Result<AppliedEdits>
editLineCorner(const Definition &current, const CornerEdit &edit,
               const NumericalProfile &profile = {});

enum class OffsetSourceMode : std::uint8_t { Keep = 1, Delete = 2 };

struct OffsetIdentity {
  SketchObjectId object;
  SketchEntityId curve;
};

struct OffsetEdit {
  std::vector<SketchEntityId> curves;
  LengthValue distance;
  OffsetSourceMode sourceMode = OffsetSourceMode::Keep;
  std::vector<OffsetIdentity> outputs;
  ExternalConstraintPolicy constraints = ExternalConstraintPolicy::Refuse;
};

[[nodiscard]] Result<AppliedEdits>
offsetCurves(const Definition &current, const OffsetEdit &edit,
             const NumericalProfile &profile = {});

struct ExtendEdit {
  CurvePick curve;
  Point2 target;
  ExternalConstraintPolicy constraints = ExternalConstraintPolicy::Refuse;
};

[[nodiscard]] Result<AppliedEdits>
extendCurve(const Definition &current, const ExtendEdit &edit,
            const NumericalProfile &profile = {});

} // namespace kearne::sketch
