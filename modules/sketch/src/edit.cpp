#include <kearne/sketch/edit.hpp>

#include <algorithm>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace kearne::sketch {
namespace {

constexpr std::size_t maximumSourceEditBatch = 32;

template <typename Values, typename Id, typename GetId>
auto findById(Values &values, const Id &id, GetId getId) {
  return std::ranges::find(values, id, getId);
}

std::string targetKey(const Edit &edit) {
  return std::visit(
      []<typename Value>(const Value &value) {
        using Type = std::remove_cvref_t<Value>;
        if constexpr (std::is_same_v<Type, AppendEntity> ||
                      std::is_same_v<Type, ReplaceEntity>) {
          return entityId(value.value).toString();
        } else if constexpr (std::is_same_v<Type, DeleteEntity>) {
          return value.id.toString();
        } else if constexpr (std::is_same_v<Type, AppendConstraint> ||
                             std::is_same_v<Type, ReplaceConstraint>) {
          return constraintId(value.value).toString();
        } else {
          return value.id.toString();
        }
      },
      edit);
}

template <typename Id>
SourceEditIntent intent(SourceEditAction action, SourceSection section, Id id) {
  return {action, section, id};
}

Result<SourceEditIntent> apply(Definition &target, const Edit &edit) {
  return std::visit(
      [&]<typename Value>(const Value &value) -> Result<SourceEditIntent> {
        using Type = std::remove_cvref_t<Value>;
        if constexpr (std::is_same_v<Type, AppendEntity>) {
          const SketchEntityId id = entityId(value.value);
          if (findById(target.entities, id, entityId) != target.entities.end())
            return std::unexpected(diagnostic("sketch.edit.entity-exists",
                                              "Sketch entity already exists"));
          target.entities.push_back(value.value);
          return intent(SourceEditAction::Append, SourceSection::Entities, id);
        } else if constexpr (std::is_same_v<Type, ReplaceEntity>) {
          const SketchEntityId id = entityId(value.value);
          auto found = findById(target.entities, id, entityId);
          if (found == target.entities.end())
            return std::unexpected(diagnostic("sketch.edit.entity-missing",
                                              "Sketch entity is missing"));
          if (found->index() != value.value.index())
            return std::unexpected(
                diagnostic("sketch.edit.entity-kind",
                           "Sketch entity replacement changes its kind"));
          *found = value.value;
          return intent(SourceEditAction::Replace, SourceSection::Entities, id);
        } else if constexpr (std::is_same_v<Type, DeleteEntity>) {
          auto found = findById(target.entities, value.id, entityId);
          if (found == target.entities.end())
            return std::unexpected(diagnostic("sketch.edit.entity-missing",
                                              "Sketch entity is missing"));
          target.entities.erase(found);
          return intent(SourceEditAction::Delete, SourceSection::Entities,
                        value.id);
        } else if constexpr (std::is_same_v<Type, AppendConstraint>) {
          const SketchConstraintId id = constraintId(value.value);
          if (findById(target.constraints, id, constraintId) !=
              target.constraints.end())
            return std::unexpected(
                diagnostic("sketch.edit.constraint-exists",
                           "Sketch constraint already exists"));
          target.constraints.push_back(value.value);
          return intent(SourceEditAction::Append, SourceSection::Constraints,
                        id);
        } else if constexpr (std::is_same_v<Type, ReplaceConstraint>) {
          const SketchConstraintId id = constraintId(value.value);
          auto found = findById(target.constraints, id, constraintId);
          if (found == target.constraints.end())
            return std::unexpected(diagnostic("sketch.edit.constraint-missing",
                                              "Sketch constraint is missing"));
          if (found->index() != value.value.index())
            return std::unexpected(
                diagnostic("sketch.edit.constraint-kind",
                           "Sketch constraint replacement changes its kind"));
          *found = value.value;
          return intent(SourceEditAction::Replace, SourceSection::Constraints,
                        id);
        } else {
          auto found = findById(target.constraints, value.id, constraintId);
          if (found == target.constraints.end())
            return std::unexpected(diagnostic("sketch.edit.constraint-missing",
                                              "Sketch constraint is missing"));
          target.constraints.erase(found);
          return intent(SourceEditAction::Delete, SourceSection::Constraints,
                        value.id);
        }
      },
      edit);
}

} // namespace

Result<AppliedEdits> applyEdits(const Definition &current,
                                std::span<const Edit> edits,
                                const NumericalProfile &profile) {
  if (edits.empty() || edits.size() > maximumSourceEditBatch)
    return std::unexpected(
        diagnostic("sketch.edit.batch-size", "Sketch edit batch is invalid"));
  if (auto valid = validate(current, profile); !valid)
    return std::unexpected(std::move(valid.error()));

  std::unordered_set<std::string> targets;
  targets.reserve(edits.size());
  AppliedEdits result{current, {}};
  result.sourceEdits.reserve(edits.size());
  for (const Edit &edit : edits) {
    if (!targets.insert(targetKey(edit)).second)
      return std::unexpected(
          diagnostic("sketch.edit.duplicate-target",
                     "Sketch edit target is duplicated in the batch"));
    auto applied = apply(result.target, edit);
    if (!applied)
      return std::unexpected(std::move(applied.error()));
    result.sourceEdits.push_back(std::move(*applied));
  }
  if (auto valid = validate(result.target, profile); !valid)
    return std::unexpected(std::move(valid.error()));
  return result;
}

} // namespace kearne::sketch
