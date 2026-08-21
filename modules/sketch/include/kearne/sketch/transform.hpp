#pragma once

#include <kearne/sketch/edit.hpp>

#include <span>

namespace kearne::sketch {

// A similarity about pivot followed by translation. Reflection is across the
// local x-axis before rotation; an arbitrary mirror axis at angle a therefore
// uses rotationRadians = 2a.
struct SimilarityTransform2d {
  Point2 pivot;
  Point2 translation;
  double rotationRadians = 0.0;
  double scale = 1.0;
  bool reflected = false;
  bool operator==(const SimilarityTransform2d &) const = default;
};

enum class ExternalConstraintPolicy : std::uint8_t {
  Refuse = 1,
  Detach = 2,
};

enum class DimensionCopyPolicy : std::uint8_t { Preserve = 1, Equalize = 2 };

struct EntityCopyIdentity {
  SketchEntityId source;
  SketchEntityId target;
  bool operator==(const EntityCopyIdentity &) const = default;
};

struct ConstraintCopyIdentity {
  SketchConstraintId source;
  SketchConstraintId target;
  bool operator==(const ConstraintCopyIdentity &) const = default;
};

struct ObjectCopyIdentity {
  SketchObjectId source;
  SketchObjectId target;
  std::string label;
  bool operator==(const ObjectCopyIdentity &) const = default;
};

struct TransformCopy {
  SimilarityTransform2d transform;
  std::vector<EntityCopyIdentity> entities;
  std::vector<ConstraintCopyIdentity> constraints;
  std::vector<ObjectCopyIdentity> objects;
  bool operator==(const TransformCopy &) const = default;
};

struct TransformCopyRequirements {
  std::vector<SketchEntityId> entities;
  std::vector<SketchConstraintId> constraints;
  std::vector<SketchObjectId> objects;
  bool operator==(const TransformCopyRequirements &) const = default;
};

[[nodiscard]] Result<Point2>
transformPoint(const Point2 &point, const SimilarityTransform2d &transform,
               const NumericalProfile &profile = {});

[[nodiscard]] Result<Entity>
transformEntity(const Entity &entity, const SimilarityTransform2d &transform,
                const NumericalProfile &profile = {});

[[nodiscard]] Result<AppliedEdits>
transformSelection(const Definition &current,
                   std::span<const SketchEntityId> entities,
                   const SimilarityTransform2d &transform,
                   ExternalConstraintPolicy externalConstraints,
                   const NumericalProfile &profile = {});

[[nodiscard]] Result<AppliedEdits> copySelection(
    const Definition &current, std::span<const SketchEntityId> entities,
    std::span<const TransformCopy> copies, DimensionCopyPolicy dimensions,
    const NumericalProfile &profile = {});

[[nodiscard]] Result<TransformCopyRequirements>
copyRequirements(const Definition &current,
                 std::span<const SketchEntityId> entities,
                 const SimilarityTransform2d &transform,
                 const NumericalProfile &profile = {});

} // namespace kearne::sketch
