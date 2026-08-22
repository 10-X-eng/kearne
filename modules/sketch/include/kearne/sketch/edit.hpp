#pragma once

#include <kearne/sketch/model.hpp>

#include <span>
#include <variant>
#include <vector>

namespace kearne::sketch {

struct AppendObject {
  SketchObject value;
};

struct ReplaceObject {
  SketchObject value;
};

struct DeleteObject {
  SketchObjectId id;
};

struct AppendEntity {
  Entity value;
};

struct ReplaceEntity {
  Entity value;
};

// Explicitly changes a stable entity's geometry kind. Ordinary replacement
// remains kind-preserving so accidental topology changes are refused.
struct RetypeEntity {
  Entity value;
};

struct DeleteEntity {
  SketchEntityId id;
};

struct AppendConstraint {
  Constraint value;
};

struct ReplaceConstraint {
  Constraint value;
};

struct DeleteConstraint {
  SketchConstraintId id;
};

using Edit =
    std::variant<AppendObject, ReplaceObject, DeleteObject, AppendEntity,
                 ReplaceEntity, RetypeEntity, DeleteEntity, AppendConstraint,
                 ReplaceConstraint, DeleteConstraint>;

enum class SourceEditAction : std::uint8_t { Append, Replace, Delete };
enum class SourceSection : std::uint8_t { Objects, Entities, Constraints };

struct SourceEditIntent {
  SourceEditAction action;
  SourceSection section;
  std::variant<SketchObjectId, SketchEntityId, SketchConstraintId> target;
  bool operator==(const SourceEditIntent &) const = default;
};

struct AppliedEdits {
  Definition target;
  std::vector<SourceEditIntent> sourceEdits;
  bool operator==(const AppliedEdits &) const = default;
};

struct CurveDragEdit {
  SketchEntityId entity;
  Point2 first;
  Point2 current;
};

[[nodiscard]] Result<AppliedEdits>
applyEdits(const Definition &current, std::span<const Edit> edits,
           const NumericalProfile &profile = {});

[[nodiscard]] Result<AppliedEdits>
toggleConstruction(const Definition &current, SketchEntityId entity,
                   const NumericalProfile &profile = {});

[[nodiscard]] Result<AppliedEdits>
dragCurve(const Definition &current, const CurveDragEdit &drag,
          const NumericalProfile &profile = {});

[[nodiscard]] Result<AppliedEdits>
removeAxisAlignment(const Definition &current,
                    std::span<const SketchEntityId> entities,
                    const NumericalProfile &profile = {});

} // namespace kearne::sketch
