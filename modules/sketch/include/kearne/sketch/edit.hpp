#pragma once

#include <kearne/sketch/model.hpp>

#include <span>
#include <variant>
#include <vector>

namespace kearne::sketch {

struct AppendEntity {
  Entity value;
};

struct ReplaceEntity {
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
    std::variant<AppendEntity, ReplaceEntity, DeleteEntity, AppendConstraint,
                 ReplaceConstraint, DeleteConstraint>;

enum class SourceEditAction : std::uint8_t { Append, Replace, Delete };
enum class SourceSection : std::uint8_t { Entities, Constraints };

struct SourceEditIntent {
  SourceEditAction action;
  SourceSection section;
  std::variant<SketchEntityId, SketchConstraintId> target;
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

} // namespace kearne::sketch
