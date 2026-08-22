#include <kearne/sketch/tools.hpp>
#include <kearne/sketch/transform.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <type_traits>
#include <utility>
#include <vector>

namespace kearne::sketch {
namespace {

template <typename QuantityType>
Result<QuantityType> quantity(double value, const char *code,
                              const char *message) {
  auto result = QuantityType::fromSi(value);
  if (!result)
    return std::unexpected(diagnostic(code, message));
  return result;
}

Result<void> validateTransform(const SimilarityTransform2d &transform) {
  if (!std::isfinite(transform.rotationRadians) ||
      !std::isfinite(transform.scale) || transform.scale <= 0.0 ||
      !std::isfinite(transform.pivot.x.si()) ||
      !std::isfinite(transform.pivot.y.si()) ||
      !std::isfinite(transform.translation.x.si()) ||
      !std::isfinite(transform.translation.y.si()))
    return std::unexpected(
        diagnostic("sketch.transform.invalid", "Sketch transform is invalid"));
  return {};
}

Result<LengthValue> scaledLength(LengthValue value,
                                 const SimilarityTransform2d &transform,
                                 const NumericalProfile &profile) {
  const double scaled = value.si() * transform.scale;
  if (!std::isfinite(scaled) || scaled < profile.minimumLengthMeters ||
      scaled > profile.maximumCoordinateMeters)
    return std::unexpected(
        diagnostic("sketch.transform.length-range",
                   "Sketch transform produces an invalid length"));
  return quantity<LengthValue>(scaled, "sketch.transform.length-range",
                               "Sketch transform produces an invalid length");
}

Result<LengthValue> scaledParameter(LengthValue value,
                                    const SimilarityTransform2d &transform,
                                    const NumericalProfile &profile) {
  const double scaled =
      value.si() * transform.scale * (transform.reflected ? -1.0 : 1.0);
  if (!std::isfinite(scaled) ||
      std::abs(scaled) > profile.maximumCoordinateMeters)
    return std::unexpected(
        diagnostic("sketch.transform.parameter-range",
                   "Sketch transform produces an invalid curve parameter"));
  return quantity<LengthValue>(
      scaled, "sketch.transform.parameter-range",
      "Sketch transform produces an invalid curve parameter");
}

Result<AngleValue> transformedAngle(AngleValue value,
                                    const SimilarityTransform2d &transform) {
  const double angle = transform.rotationRadians +
                       (transform.reflected ? -value.si() : value.si());
  return quantity<AngleValue>(angle, "sketch.transform.angle-range",
                              "Sketch transform produces an invalid angle");
}

bool preservesCoordinateAxes(const SimilarityTransform2d &transform,
                             const NumericalProfile &profile) {
  return std::abs(std::sin(transform.rotationRadians)) <=
         profile.angleToleranceRadians;
}

Result<Constraint> transformConstraint(const Constraint &constraint,
                                       const SimilarityTransform2d &transform,
                                       const NumericalProfile &profile) {
  Constraint result = constraint;
  auto updated = std::visit(
      [&](auto &value) -> Result<void> {
        using Type = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::is_same_v<Type, Lock>) {
          auto position = transformPoint(value.position, transform, profile);
          if (!position)
            return std::unexpected(std::move(position.error()));
          value.position = *position;
        } else if constexpr (std::is_same_v<Type, Distance> ||
                             std::is_same_v<Type, Radius> ||
                             std::is_same_v<Type, Diameter>) {
          auto scaled = scaledLength(value.value, transform, profile);
          if (!scaled)
            return std::unexpected(std::move(scaled.error()));
          value.value = *scaled;
        } else if constexpr (std::is_same_v<Type, HorizontalDistance> ||
                             std::is_same_v<Type, VerticalDistance>) {
          const double direction =
              std::cos(transform.rotationRadians) *
              (std::is_same_v<Type, VerticalDistance> && transform.reflected
                   ? -1.0
                   : 1.0);
          auto scaled = quantity<LengthValue>(
              value.value.si() * transform.scale * direction,
              "sketch.transform.dimension-range",
              "Sketch transform produces an invalid dimension");
          if (!scaled)
            return std::unexpected(std::move(scaled.error()));
          value.value = *scaled;
        }
        return {};
      },
      result);
  if (!updated)
    return std::unexpected(std::move(updated.error()));
  return result;
}

template <typename Mapping, typename Id>
std::optional<Id> mappedId(const Mapping &mapping, Id source) {
  const auto found = std::ranges::find_if(
      mapping, [source](const auto &entry) { return entry.source == source; });
  return found == mapping.end() ? std::nullopt
                                : std::optional<Id>{found->target};
}

Result<Constraint> copyConstraint(const Constraint &constraint,
                                  const TransformCopy &copy,
                                  DimensionCopyPolicy dimensions,
                                  std::set<SketchEntityId> &equalized,
                                  const NumericalProfile &profile) {
  const auto copiedId = mappedId(copy.constraints, constraintId(constraint));
  if (!copiedId)
    return std::unexpected(diagnostic("sketch.transform.constraint-copy-id",
                                      "Constraint copy identity is missing"));
  const auto remapEntity = [&](SketchEntityId &id) -> Result<void> {
    auto target = mappedId(copy.entities, id);
    if (!target)
      return std::unexpected(diagnostic("sketch.transform.entity-copy-id",
                                        "Entity copy identity is missing"));
    id = *target;
    return {};
  };
  const auto remapPoint = [&](PointRef &point) {
    return remapEntity(point.entity);
  };

  if (dimensions == DimensionCopyPolicy::Equalize) {
    const auto source = std::visit(
        []<typename Value>(
            const Value &value) -> std::optional<SketchEntityId> {
          using Type = std::remove_cvref_t<Value>;
          if constexpr (std::is_same_v<Type, Radius> ||
                        std::is_same_v<Type, Diameter>)
            return value.curve;
          if constexpr (std::is_same_v<Type, Distance> ||
                        std::is_same_v<Type, HorizontalDistance> ||
                        std::is_same_v<Type, VerticalDistance>) {
            if (value.first.entity == value.second.entity)
              return value.first.entity;
          }
          return std::nullopt;
        },
        constraint);
    if (source) {
      auto target = mappedId(copy.entities, *source);
      if (!target)
        return std::unexpected(diagnostic("sketch.transform.entity-copy-id",
                                          "Entity copy identity is missing"));
      if (!equalized.insert(*target).second)
        return std::unexpected(diagnostic(
            "sketch.transform.duplicate-equalized-dimension",
            "Copied geometry has more than one equalized dimension"));
      return Constraint{Equal{*copiedId, *source, *target}};
    }
  }

  auto result = transformConstraint(constraint, copy.transform, profile);
  if (!result)
    return std::unexpected(std::move(result.error()));
  auto remapped = std::visit(
      [&](auto &value) -> Result<void> {
        using Type = std::remove_cvref_t<decltype(value)>;
        value.id = *copiedId;
        if constexpr (std::is_same_v<Type, Coincident> ||
                      std::is_same_v<Type, Distance> ||
                      std::is_same_v<Type, HorizontalDistance> ||
                      std::is_same_v<Type, VerticalDistance>) {
          if (auto mapped = remapPoint(value.first); !mapped)
            return mapped;
          return remapPoint(value.second);
        } else if constexpr (std::is_same_v<Type, Horizontal> ||
                             std::is_same_v<Type, Vertical>) {
          return remapEntity(value.line);
        } else if constexpr (std::is_same_v<Type, Parallel> ||
                             std::is_same_v<Type, Perpendicular> ||
                             std::is_same_v<Type, Tangent> ||
                             std::is_same_v<Type, Concentric> ||
                             std::is_same_v<Type, Equal> ||
                             std::is_same_v<Type, Collinear> ||
                             std::is_same_v<Type, AngleBetween>) {
          if (auto mapped = remapEntity(value.first); !mapped)
            return mapped;
          return remapEntity(value.second);
        } else if constexpr (std::is_same_v<Type, Midpoint>) {
          if (auto mapped = remapPoint(value.point); !mapped)
            return mapped;
          return remapEntity(value.line);
        } else if constexpr (std::is_same_v<Type, PointOnObject>) {
          if (auto mapped = remapPoint(value.point); !mapped)
            return mapped;
          return remapEntity(value.curve);
        } else if constexpr (std::is_same_v<Type, Snell>) {
          if (auto mapped = remapPoint(value.incident); !mapped)
            return mapped;
          if (auto mapped = remapPoint(value.refracted); !mapped)
            return mapped;
          return remapEntity(value.boundary);
        } else if constexpr (std::is_same_v<Type, Symmetric>) {
          if (auto mapped = remapPoint(value.first); !mapped)
            return mapped;
          if (auto mapped = remapPoint(value.second); !mapped)
            return mapped;
          return remapEntity(value.axis);
        } else if constexpr (std::is_same_v<Type, SymmetricAboutPoint>) {
          if (auto mapped = remapPoint(value.first); !mapped)
            return mapped;
          if (auto mapped = remapPoint(value.second); !mapped)
            return mapped;
          return remapPoint(value.center);
        } else if constexpr (std::is_same_v<Type, Lock>) {
          return remapPoint(value.point);
        } else if constexpr (std::is_same_v<Type, Block>) {
          return remapEntity(value.entity);
        } else if constexpr (std::is_same_v<Type, Group>) {
          for (SketchEntityId &member : value.entities) {
            if (auto mapped = remapEntity(member); !mapped)
              return mapped;
          }
        } else if constexpr (std::is_same_v<Type, Radius> ||
                             std::is_same_v<Type, Diameter>) {
          return remapEntity(value.curve);
        }
        return {};
      },
      *result);
  if (!remapped)
    return std::unexpected(std::move(remapped.error()));
  return result;
}

template <typename Value>
Result<void> transformCenterAndRotation(Value &value,
                                        const SimilarityTransform2d &transform,
                                        const NumericalProfile &profile) {
  auto center = transformPoint(value.center, transform, profile);
  auto rotation = transformedAngle(value.rotation, transform);
  if (!center)
    return std::unexpected(std::move(center.error()));
  if (!rotation)
    return std::unexpected(std::move(rotation.error()));
  value.center = *center;
  value.rotation = *rotation;
  return {};
}

} // namespace

Result<Point2> transformPoint(const Point2 &point,
                              const SimilarityTransform2d &transform,
                              const NumericalProfile &profile) {
  if (auto valid = validateTransform(transform); !valid)
    return std::unexpected(std::move(valid.error()));
  const double localX = point.x.si() - transform.pivot.x.si();
  const double localY = (point.y.si() - transform.pivot.y.si()) *
                        (transform.reflected ? -1.0 : 1.0);
  const double cosine = std::cos(transform.rotationRadians);
  const double sine = std::sin(transform.rotationRadians);
  const double x = transform.pivot.x.si() + transform.translation.x.si() +
                   transform.scale * (cosine * localX - sine * localY);
  const double y = transform.pivot.y.si() + transform.translation.y.si() +
                   transform.scale * (sine * localX + cosine * localY);
  if (!std::isfinite(x) || !std::isfinite(y) ||
      std::abs(x) > profile.maximumCoordinateMeters ||
      std::abs(y) > profile.maximumCoordinateMeters)
    return std::unexpected(
        diagnostic("sketch.transform.coordinate-range",
                   "Sketch transform produces an invalid point"));
  auto resultX =
      quantity<LengthValue>(x, "sketch.transform.coordinate-range",
                            "Sketch transform produces an invalid point");
  auto resultY =
      quantity<LengthValue>(y, "sketch.transform.coordinate-range",
                            "Sketch transform produces an invalid point");
  if (!resultX)
    return std::unexpected(std::move(resultX.error()));
  if (!resultY)
    return std::unexpected(std::move(resultY.error()));
  return Point2{*resultX, *resultY};
}

Result<Entity> transformEntity(const Entity &entity,
                               const SimilarityTransform2d &transform,
                               const NumericalProfile &profile) {
  if (auto valid = validateTransform(transform); !valid)
    return std::unexpected(std::move(valid.error()));
  Entity result = entity;
  auto transformed = std::visit(
      [&](auto &value) -> Result<void> {
        using Type = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::is_same_v<Type, PointEntity>) {
          auto point = transformPoint(value.point, transform, profile);
          if (!point)
            return std::unexpected(std::move(point.error()));
          value.point = *point;
        } else if constexpr (std::is_same_v<Type, LineEntity>) {
          auto start = transformPoint(value.start, transform, profile);
          auto end = transformPoint(value.end, transform, profile);
          if (!start)
            return std::unexpected(std::move(start.error()));
          if (!end)
            return std::unexpected(std::move(end.error()));
          value.start = *start;
          value.end = *end;
        } else if constexpr (std::is_same_v<Type, CircleEntity> ||
                             std::is_same_v<Type, ArcEntity>) {
          auto center = transformPoint(value.center, transform, profile);
          auto radius = scaledLength(value.radius, transform, profile);
          if (!center)
            return std::unexpected(std::move(center.error()));
          if (!radius)
            return std::unexpected(std::move(radius.error()));
          value.center = *center;
          value.radius = *radius;
          if constexpr (std::is_same_v<Type, ArcEntity>) {
            auto start = transformedAngle(value.startAngle, transform);
            auto end = transformedAngle(value.endAngle, transform);
            if (!start)
              return std::unexpected(std::move(start.error()));
            if (!end)
              return std::unexpected(std::move(end.error()));
            value.startAngle = *start;
            value.endAngle = *end;
          }
        } else if constexpr (std::is_same_v<Type, EllipseEntity> ||
                             std::is_same_v<Type, EllipticalArcEntity> ||
                             std::is_same_v<Type, HyperbolicArcEntity>) {
          if (auto updated =
                  transformCenterAndRotation(value, transform, profile);
              !updated)
            return updated;
          auto major = scaledLength(value.majorRadius, transform, profile);
          auto minor = scaledLength(value.minorRadius, transform, profile);
          if (!major)
            return std::unexpected(std::move(major.error()));
          if (!minor)
            return std::unexpected(std::move(minor.error()));
          value.majorRadius = *major;
          value.minorRadius = *minor;
          if constexpr (std::is_same_v<Type, EllipticalArcEntity>) {
            if (transform.reflected) {
              auto start = quantity<AngleValue>(
                  -value.startParameter.si(),
                  "sketch.transform.parameter-range",
                  "Sketch transform produces an invalid curve parameter");
              auto end = quantity<AngleValue>(
                  -value.endParameter.si(), "sketch.transform.parameter-range",
                  "Sketch transform produces an invalid curve parameter");
              if (!start)
                return std::unexpected(std::move(start.error()));
              if (!end)
                return std::unexpected(std::move(end.error()));
              value.startParameter = *start;
              value.endParameter = *end;
            }
          } else if constexpr (std::is_same_v<Type, HyperbolicArcEntity>) {
            if (transform.reflected) {
              auto start = quantity<DimensionlessValue>(
                  -value.startParameter.si(),
                  "sketch.transform.parameter-range",
                  "Sketch transform produces an invalid curve parameter");
              auto end = quantity<DimensionlessValue>(
                  -value.endParameter.si(), "sketch.transform.parameter-range",
                  "Sketch transform produces an invalid curve parameter");
              if (!start)
                return std::unexpected(std::move(start.error()));
              if (!end)
                return std::unexpected(std::move(end.error()));
              value.startParameter = *start;
              value.endParameter = *end;
            }
          }
        } else if constexpr (std::is_same_v<Type, ParabolicArcEntity>) {
          auto vertex = transformPoint(value.vertex, transform, profile);
          auto focal = scaledLength(value.focalLength, transform, profile);
          auto rotation = transformedAngle(value.rotation, transform);
          auto start =
              scaledParameter(value.startParameter, transform, profile);
          auto end = scaledParameter(value.endParameter, transform, profile);
          if (!vertex)
            return std::unexpected(std::move(vertex.error()));
          if (!focal)
            return std::unexpected(std::move(focal.error()));
          if (!rotation)
            return std::unexpected(std::move(rotation.error()));
          if (!start)
            return std::unexpected(std::move(start.error()));
          if (!end)
            return std::unexpected(std::move(end.error()));
          value.vertex = *vertex;
          value.focalLength = *focal;
          value.rotation = *rotation;
          value.startParameter = *start;
          value.endParameter = *end;
        } else if constexpr (std::is_same_v<Type, BSplineEntity>) {
          for (Point2 &point : value.controlPoints) {
            auto replacement = transformPoint(point, transform, profile);
            if (!replacement)
              return std::unexpected(std::move(replacement.error()));
            point = *replacement;
          }
        }
        return {};
      },
      result);
  if (!transformed)
    return std::unexpected(std::move(transformed.error()));
  return result;
}

Result<AppliedEdits>
transformSelection(const Definition &current,
                   std::span<const SketchEntityId> entities,
                   const SimilarityTransform2d &transform,
                   ExternalConstraintPolicy externalConstraints,
                   const NumericalProfile &profile) {
  if (entities.empty() || entities.size() > 1'024U)
    return std::unexpected(diagnostic("sketch.transform.selection-size",
                                      "Sketch transform selection is invalid"));
  if (externalConstraints != ExternalConstraintPolicy::Refuse &&
      externalConstraints != ExternalConstraintPolicy::Detach)
    return std::unexpected(
        diagnostic("sketch.transform.constraint-policy",
                   "Sketch transform constraint policy is invalid"));
  if (auto valid = validate(current, profile); !valid)
    return std::unexpected(std::move(valid.error()));
  if (auto valid = validateTransform(transform); !valid)
    return std::unexpected(std::move(valid.error()));

  const std::set<SketchEntityId> selected(entities.begin(), entities.end());
  if (selected.size() != entities.size())
    return std::unexpected(
        diagnostic("sketch.transform.duplicate-selection",
                   "Sketch transform selection is duplicated"));

  std::vector<Edit> edits;
  edits.reserve(entities.size() + current.objects.size() +
                current.constraints.size());
  for (const SketchEntityId id : entities) {
    const auto found = std::ranges::find(current.entities, id, entityId);
    if (found == current.entities.end())
      return std::unexpected(diagnostic("sketch.transform.entity-missing",
                                        "Sketch transform entity is missing"));
    auto replacement = transformEntity(*found, transform, profile);
    if (!replacement)
      return std::unexpected(std::move(replacement.error()));
    edits.emplace_back(ReplaceEntity{std::move(*replacement)});
  }

  for (const SketchObject &object : current.objects) {
    const std::size_t selectedMembers = std::ranges::count_if(
        object.members, [&](const SketchObjectMember &member) {
          return selected.contains(member.entity);
        });
    if (object.kind != SketchObjectKind::CurveGroup && selectedMembers != 0U &&
        selectedMembers != object.members.size())
      edits.emplace_back(ReplaceObject{curveGroupAfterPartialEdit(object)});
  }

  const bool axesPreserved = preservesCoordinateAxes(transform, profile);
  for (const Constraint &constraint : current.constraints) {
    const std::vector<SketchEntityId> references =
        constraintEntityIds(constraint);
    const std::size_t selectedReferences = std::ranges::count_if(
        references, [&](SketchEntityId id) { return selected.contains(id); });
    if (selectedReferences == 0U)
      continue;
    if (selectedReferences != references.size()) {
      if (externalConstraints == ExternalConstraintPolicy::Refuse)
        return std::unexpected(
            diagnostic("sketch.transform.external-constraint",
                       "Selection has constraints to unselected geometry"));
      edits.emplace_back(DeleteConstraint{constraintId(constraint)});
      continue;
    }
    if (!axesPreserved &&
        (std::holds_alternative<Horizontal>(constraint) ||
         std::holds_alternative<Vertical>(constraint) ||
         std::holds_alternative<HorizontalDistance>(constraint) ||
         std::holds_alternative<VerticalDistance>(constraint))) {
      edits.emplace_back(DeleteConstraint{constraintId(constraint)});
      continue;
    }
    auto replacement = transformConstraint(constraint, transform, profile);
    if (!replacement)
      return std::unexpected(std::move(replacement.error()));
    if (*replacement != constraint)
      edits.emplace_back(ReplaceConstraint{std::move(*replacement)});
  }
  return applyEdits(current, edits, profile);
}

Result<TransformCopyRequirements> copyRequirements(
    const Definition &current, std::span<const SketchEntityId> entities,
    const SimilarityTransform2d &transform, const NumericalProfile &profile) {
  if (entities.empty() || entities.size() > 1'024U)
    return std::unexpected(diagnostic("sketch.transform.selection-size",
                                      "Sketch transform selection is invalid"));
  if (auto valid = validate(current, profile); !valid)
    return std::unexpected(std::move(valid.error()));
  if (auto valid = validateTransform(transform); !valid)
    return std::unexpected(std::move(valid.error()));
  const std::set<SketchEntityId> selected(entities.begin(), entities.end());
  if (selected.size() != entities.size())
    return std::unexpected(
        diagnostic("sketch.transform.duplicate-selection",
                   "Sketch transform selection is duplicated"));
  TransformCopyRequirements result;
  result.entities.assign(entities.begin(), entities.end());
  for (SketchEntityId id : entities) {
    if (std::ranges::find(current.entities, id, entityId) ==
        current.entities.end())
      return std::unexpected(diagnostic("sketch.transform.entity-missing",
                                        "Sketch transform entity is missing"));
  }
  for (const SketchObject &object : current.objects) {
    if (!object.members.empty() &&
        std::ranges::all_of(object.members, [&](const auto &member) {
          return selected.contains(member.entity);
        }))
      result.objects.push_back(object.id);
  }
  for (const Constraint &constraint : current.constraints) {
    const auto references = constraintEntityIds(constraint);
    if (!references.empty() &&
        std::ranges::all_of(
            references,
            [&](SketchEntityId id) { return selected.contains(id); }) &&
        (preservesCoordinateAxes(transform, profile) ||
         (!std::holds_alternative<Horizontal>(constraint) &&
          !std::holds_alternative<Vertical>(constraint) &&
          !std::holds_alternative<HorizontalDistance>(constraint) &&
          !std::holds_alternative<VerticalDistance>(constraint))))
      result.constraints.push_back(constraintId(constraint));
  }
  return result;
}

Result<AppliedEdits> copySelection(const Definition &current,
                                   std::span<const SketchEntityId> entities,
                                   std::span<const TransformCopy> copies,
                                   DimensionCopyPolicy dimensions,
                                   const NumericalProfile &profile) {
  if (entities.empty() || copies.empty() || entities.size() > 1'024U ||
      copies.size() > 1'024U || entities.size() > 4'096U / copies.size())
    return std::unexpected(
        diagnostic("sketch.transform.copy-size",
                   "Sketch transform copy count is invalid"));
  if (dimensions != DimensionCopyPolicy::Preserve &&
      dimensions != DimensionCopyPolicy::Equalize)
    return std::unexpected(diagnostic("sketch.transform.dimension-policy",
                                      "Dimension copy policy is invalid"));
  if (auto valid = validate(current, profile); !valid)
    return std::unexpected(std::move(valid.error()));
  const std::set<SketchEntityId> selected(entities.begin(), entities.end());
  if (selected.size() != entities.size())
    return std::unexpected(
        diagnostic("sketch.transform.duplicate-selection",
                   "Sketch transform selection is duplicated"));
  for (SketchEntityId id : entities) {
    if (std::ranges::find(current.entities, id, entityId) ==
        current.entities.end())
      return std::unexpected(diagnostic("sketch.transform.entity-missing",
                                        "Sketch transform entity is missing"));
  }

  std::set<SketchEntityId> entityTargets;
  std::set<SketchConstraintId> constraintTargets;
  std::set<SketchObjectId> objectTargets;
  for (const Entity &entity : current.entities)
    entityTargets.insert(entityId(entity));
  for (const Constraint &constraint : current.constraints)
    constraintTargets.insert(constraintId(constraint));
  for (const SketchObject &object : current.objects)
    objectTargets.insert(object.id);

  std::vector<Edit> edits;
  for (const TransformCopy &copy : copies) {
    auto requirements =
        copyRequirements(current, entities, copy.transform, profile);
    if (!requirements)
      return std::unexpected(std::move(requirements.error()));
    std::set<SketchEntityId> mappedSources;
    for (const EntityCopyIdentity &identity : copy.entities) {
      if (!selected.contains(identity.source) ||
          !mappedSources.insert(identity.source).second ||
          !entityTargets.insert(identity.target).second)
        return std::unexpected(
            diagnostic("sketch.transform.entity-copy-id",
                       "Entity copy identities are invalid"));
    }
    if (mappedSources != selected)
      return std::unexpected(
          diagnostic("sketch.transform.entity-copy-id",
                     "Entity copy identities are incomplete"));

    std::vector<const SketchObject *> copiedObjects;
    for (SketchObjectId id : requirements->objects)
      copiedObjects.push_back(
          &*std::ranges::find(current.objects, id, &SketchObject::id));
    std::set<SketchObjectId> mappedObjects;
    for (const ObjectCopyIdentity &identity : copy.objects) {
      const bool expected =
          std::ranges::find(requirements->objects, identity.source) !=
          requirements->objects.end();
      if (!expected || identity.label.empty() ||
          !mappedObjects.insert(identity.source).second ||
          !objectTargets.insert(identity.target).second)
        return std::unexpected(
            diagnostic("sketch.transform.object-copy-id",
                       "Object copy identities are invalid"));
    }
    if (mappedObjects.size() != copiedObjects.size())
      return std::unexpected(
          diagnostic("sketch.transform.object-copy-id",
                     "Object copy identities are incomplete"));

    std::vector<const Constraint *> copiedConstraints;
    for (SketchConstraintId id : requirements->constraints)
      copiedConstraints.push_back(
          &*std::ranges::find(current.constraints, id, constraintId));
    std::set<SketchConstraintId> mappedConstraints;
    for (const ConstraintCopyIdentity &identity : copy.constraints) {
      const bool expected =
          std::ranges::find(requirements->constraints, identity.source) !=
          requirements->constraints.end();
      if (!expected || !mappedConstraints.insert(identity.source).second ||
          !constraintTargets.insert(identity.target).second)
        return std::unexpected(
            diagnostic("sketch.transform.constraint-copy-id",
                       "Constraint copy identities are invalid"));
    }
    if (mappedConstraints.size() != copiedConstraints.size())
      return std::unexpected(
          diagnostic("sketch.transform.constraint-copy-id",
                     "Constraint copy identities are incomplete"));

    for (SketchEntityId source : entities) {
      const auto found = std::ranges::find(current.entities, source, entityId);
      auto transformed = transformEntity(*found, copy.transform, profile);
      if (!transformed)
        return std::unexpected(std::move(transformed.error()));
      const SketchEntityId target = *mappedId(copy.entities, source);
      std::visit([target](auto &value) { value.id = target; }, *transformed);
      edits.emplace_back(AppendEntity{std::move(*transformed)});
    }
    for (const SketchObject *source : copiedObjects) {
      const auto identity = std::ranges::find(copy.objects, source->id,
                                              &ObjectCopyIdentity::source);
      SketchObject object = *source;
      object.id = identity->target;
      object.label = identity->label;
      for (SketchObjectMember &member : object.members)
        member.entity = *mappedId(copy.entities, member.entity);
      edits.emplace_back(AppendObject{std::move(object)});
    }
    std::set<SketchEntityId> equalized;
    for (const Constraint *source : copiedConstraints) {
      auto constraint =
          copyConstraint(*source, copy, dimensions, equalized, profile);
      if (!constraint)
        return std::unexpected(std::move(constraint.error()));
      edits.emplace_back(AppendConstraint{std::move(*constraint)});
    }
    if (edits.size() > 65'536U)
      return std::unexpected(
          diagnostic("sketch.transform.copy-size",
                     "Sketch transform creates too many values"));
  }
  return applyEdits(current, edits, profile);
}

} // namespace kearne::sketch
