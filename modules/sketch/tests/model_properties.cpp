#include <kearne/sketch/edit.hpp>
#include <kearne/sketch/model.hpp>
#include <kearne/sketch/tools.hpp>
#include <kearne/testkit/property.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <numbers>
#include <ranges>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

using namespace kearne;

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

template <typename Id> Id id(std::uint64_t seed) {
  typename Id::RandomTail tail{};
  for (std::size_t index = 0; index < tail.size(); ++index)
    tail[index] = static_cast<std::uint8_t>(seed >> ((index % 8U) * 8U));
  auto result = Id::create(seed & ((std::uint64_t{1} << 48U) - 1U), tail);
  require(result.has_value(), "generated plane binding ID was invalid");
  return std::move(*result);
}

template <typename Digest> Digest digest(std::uint64_t seed) {
  typename Digest::Bytes bytes{};
  for (std::size_t index = 0; index < bytes.size(); ++index)
    bytes[index] = static_cast<std::uint8_t>(seed + index * 29U);
  auto result = Digest::fromBytes("blake3", bytes);
  require(result.has_value(), "generated revision digest was invalid");
  return std::move(*result);
}

sketch::LengthValue length(double value) {
  auto result = sketch::LengthValue::fromSi(value);
  require(result.has_value(), "generated Sketch length was invalid");
  return *result;
}

sketch::AngleValue angle(double value) {
  auto result = sketch::AngleValue::fromSi(value);
  require(result.has_value(), "generated Sketch angle was invalid");
  return *result;
}

void verifyEvaluatedPlaneIdentity(const testkit::PropertyProfile &profile) {
  testkit::checkProperty(
      "evaluated plane identity", profile,
      [](testkit::Random &random, std::uint64_t index) {
        const std::uint64_t seed = random.next() ^ (index * 4U);
        const ModelBindingId binding = id<ModelBindingId>(seed + 1U);
        const ModelBindingId otherBinding = id<ModelBindingId>(seed + 2U);
        const RevisionId revision = digest<RevisionId>(seed + 3U);
        const RevisionId otherRevision = digest<RevisionId>(seed + 4U);
        const sketch::EvaluatedPlaneIdentity identity{binding, revision};
        require(identity == sketch::EvaluatedPlaneIdentity{binding, revision},
                "equal plane identity compared unequal");
        require(identity !=
                    sketch::EvaluatedPlaneIdentity{otherBinding, revision},
                "plane identity ignored its attachment binding");
        require(identity !=
                    sketch::EvaluatedPlaneIdentity{binding, otherRevision},
                "plane identity ignored its revision");
        const sketch::EvaluatedPlaneIdentity different{otherBinding,
                                                       otherRevision};
        require((identity == different) == (different == identity),
                "plane identity equality is not symmetric");
      });
}

void verifyEditLifecycle(const testkit::PropertyProfile &profile) {
  testkit::checkProperty(
      "Sketch edit lifecycle", profile,
      [](testkit::Random &random, std::uint64_t index) {
        const std::uint64_t seed = random.next() ^ (index * 100U);
        const double x = random.between(-100.0, 100.0);
        const double y = random.between(-100.0, 100.0);
        const double size = random.between(0.001, 10.0);
        const std::array entityIds{
            id<SketchEntityId>(seed + 1U), id<SketchEntityId>(seed + 2U),
            id<SketchEntityId>(seed + 3U), id<SketchEntityId>(seed + 4U)};
        const std::array constraintIds{id<SketchConstraintId>(seed + 10U),
                                       id<SketchConstraintId>(seed + 11U),
                                       id<SketchConstraintId>(seed + 12U),
                                       id<SketchConstraintId>(seed + 13U)};
        const sketch::Point2 origin{length(x), length(y)};
        const std::array<sketch::Entity, 4> entities{
            sketch::PointEntity{entityIds[0], origin},
            sketch::LineEntity{
                entityIds[1], origin, {length(x + size), length(y + size)}},
            sketch::CircleEntity{entityIds[2], origin, length(size)},
            sketch::ArcEntity{entityIds[3], origin, length(size), angle(0.1),
                              angle(std::numbers::pi)}};
        sketch::Definition current{digest<ContentDigest>(seed), {}, {}};
        std::vector<sketch::Edit> additions;
        additions.reserve(entities.size());
        for (const sketch::Entity &entity : entities)
          additions.push_back(sketch::AppendEntity{entity});
        auto added = sketch::applyEdits(current, additions);
        require(added.has_value() && added->target.entities.size() == 4U,
                "generic entity additions failed");
        require(std::ranges::all_of(
                    added->sourceEdits,
                    [](const auto &edit) {
                      return edit.action == sketch::SourceEditAction::Append &&
                             edit.section == sketch::SourceSection::Entities;
                    }),
                "entity additions produced incorrect source intents");

        std::vector<sketch::Edit> constraints;
        constraints.reserve(constraintIds.size());
        for (std::size_t position = 0; position < constraintIds.size();
             ++position)
          constraints.push_back(sketch::AppendConstraint{
              sketch::Fixed{constraintIds[position], entityIds[position]}});
        auto constrained = sketch::applyEdits(added->target, constraints);
        require(constrained.has_value() &&
                    constrained->target.constraints.size() == 4U,
                "generic constraint additions failed");

        std::vector<sketch::Edit> replacements{
            sketch::ReplaceEntity{sketch::PointEntity{
                entityIds[0], {length(x + size), length(y)}}},
            sketch::ReplaceEntity{
                sketch::LineEntity{entityIds[1],
                                   origin,
                                   {length(x + size * 2.0), length(y + size)}}},
            sketch::ReplaceEntity{
                sketch::CircleEntity{entityIds[2], origin, length(size * 2.0)}},
            sketch::ReplaceEntity{
                sketch::ArcEntity{entityIds[3], origin, length(size * 2.0),
                                  angle(0.2), angle(std::numbers::pi + 0.1)}}};
        auto replaced = sketch::applyEdits(constrained->target, replacements);
        require(replaced.has_value() &&
                    replaced->target.sourceDigest == current.sourceDigest,
                "generic entity replacements failed");

        std::vector<sketch::Edit> removals;
        removals.reserve(constraintIds.size() + entityIds.size());
        for (const SketchConstraintId value : constraintIds)
          removals.push_back(sketch::DeleteConstraint{value});
        for (const SketchEntityId value : entityIds)
          removals.push_back(sketch::DeleteEntity{value});
        std::ranges::rotate(removals, removals.begin() +
                                          static_cast<std::ptrdiff_t>(
                                              random.next() % removals.size()));
        auto removed = sketch::applyEdits(replaced->target, removals);
        require(removed.has_value() && removed->target.entities.empty() &&
                    removed->target.constraints.empty(),
                "mixed-section deletion batch failed");

        const std::array duplicate{
            sketch::Edit{sketch::AppendEntity{entities[0]}},
            sketch::Edit{sketch::AppendEntity{entities[0]}}};
        auto rejected = sketch::applyEdits(current, duplicate);
        require(!rejected &&
                    rejected.error().code == "sketch.edit.duplicate-target",
                "duplicate edit target was not rejected");
      });
}

void verifyToolComposition(const testkit::PropertyProfile &profile) {
  testkit::checkProperty(
      "Sketch tool composition", profile,
      [](testkit::Random &random, std::uint64_t index) {
        const std::uint64_t seed = random.next() ^ (index * 32U);
        const double x = random.between(-1'000.0, 1'000.0);
        const double y = random.between(-1'000.0, 1'000.0);
        const double size = random.between(0.001, 100.0);
        const sketch::Point2 first{length(x), length(y)};
        const sketch::Point2 opposite{length(x + size),
                                      length(y + size * 0.75)};
        sketch::Definition current{digest<ContentDigest>(seed), {}, {}};
        const std::array<sketch::ToolInput, 4> primitives{
            sketch::PointToolInput{id<SketchEntityId>(seed + 1U), first},
            sketch::LineToolInput{id<SketchEntityId>(seed + 2U), first,
                                  opposite},
            sketch::CircleToolInput{id<SketchEntityId>(seed + 3U), first,
                                    length(size)},
            sketch::ArcToolInput{id<SketchEntityId>(seed + 4U), first,
                                 length(size), angle(0.2),
                                 angle(std::numbers::pi)}};
        for (const sketch::ToolInput &tool : primitives) {
          auto applied = sketch::applyTool(current, tool);
          require(applied.has_value(), "primitive tool failed");
          current = std::move(applied->target);
        }

        sketch::RectangleToolIds rectangleIds{
            {id<SketchEntityId>(seed + 10U), id<SketchEntityId>(seed + 11U),
             id<SketchEntityId>(seed + 12U), id<SketchEntityId>(seed + 13U)},
            {id<SketchConstraintId>(seed + 20U),
             id<SketchConstraintId>(seed + 21U),
             id<SketchConstraintId>(seed + 22U),
             id<SketchConstraintId>(seed + 23U),
             id<SketchConstraintId>(seed + 24U),
             id<SketchConstraintId>(seed + 25U),
             id<SketchConstraintId>(seed + 26U),
             id<SketchConstraintId>(seed + 27U)}};
        auto rectangle = sketch::applyTool(
            current, sketch::RectangleToolInput{rectangleIds, first, opposite,
                                                random.next() % 2U != 0U});
        require(rectangle.has_value() && rectangle->sourceEdits.size() == 12U &&
                    rectangle->target.entities.size() == 8U &&
                    rectangle->target.constraints.size() == 8U,
                "rectangle did not compose through generic edits");
        auto residuals = sketch::evaluateResiduals(
            rectangle->target, rectangle->target.entities, {});
        require(residuals.has_value() &&
                    std::ranges::all_of(*residuals,
                                        &sketch::ConstraintResidual::satisfied),
                "rectangle tool emitted unsatisfied constraints");
      });
}

void verifyDirectManipulation(const testkit::PropertyProfile &profile) {
  testkit::checkProperty(
      "Sketch direct manipulation", profile,
      [](testkit::Random &random, std::uint64_t index) {
        const std::uint64_t seed = random.next() ^ (index * 64U);
        const double x = random.between(-100.0, 100.0);
        const double y = random.between(-100.0, 100.0);
        const double width = random.between(0.01, 10.0);
        const double height = random.between(0.01, 10.0);
        const double dragX = random.between(-width * 0.4, width * 0.4);
        const double dragY = random.between(-height * 0.4, height * 0.4);
        sketch::RectangleToolIds ids{
            {id<SketchEntityId>(seed + 1U), id<SketchEntityId>(seed + 2U),
             id<SketchEntityId>(seed + 3U), id<SketchEntityId>(seed + 4U)},
            {id<SketchConstraintId>(seed + 10U),
             id<SketchConstraintId>(seed + 11U),
             id<SketchConstraintId>(seed + 12U),
             id<SketchConstraintId>(seed + 13U),
             id<SketchConstraintId>(seed + 14U),
             id<SketchConstraintId>(seed + 15U),
             id<SketchConstraintId>(seed + 16U),
             id<SketchConstraintId>(seed + 17U)}};
        const sketch::Point2 first{length(x), length(y)};
        auto rectangle = sketch::applyTool(
            {digest<ContentDigest>(seed), {}, {}},
            sketch::RectangleToolInput{
                ids, first, {length(x + width), length(y + height)}, false});
        require(rectangle.has_value() &&
                    sketch::closedProfileCount(rectangle->target) == 1U,
                "direct manipulation rectangle was not created");

        const std::size_t edgeIndex = random.next() % ids.edges.size();
        auto construction =
            sketch::toggleConstruction(rectangle->target, ids.edges[edgeIndex]);
        require(construction && construction->sourceEdits.size() == 1U &&
                    sketch::closedProfileCount(construction->target) == 0U,
                "construction toggle was not one reusable entity edit");
        auto restored = sketch::toggleConstruction(construction->target,
                                                   ids.edges[edgeIndex]);
        require(restored && restored->target == rectangle->target &&
                    sketch::closedProfileCount(restored->target) == 1U,
                "construction toggle was not reversible");

        const auto selected = std::ranges::find(
            rectangle->target.entities, ids.edges[edgeIndex], sketch::entityId);
        require(selected != rectangle->target.entities.end(),
                "rectangle edge is missing");
        const auto &line = std::get<sketch::LineEntity>(*selected);
        const sketch::Point2 midpoint{
            length((line.start.x.si() + line.end.x.si()) * 0.5),
            length((line.start.y.si() + line.end.y.si()) * 0.5)};
        auto dragged = sketch::dragCurve(rectangle->target,
                                         {ids.edges[edgeIndex],
                                          midpoint,
                                          {length(midpoint.x.si() + dragX),
                                           length(midpoint.y.si() + dragY)}});
        require(dragged && dragged->sourceEdits.size() == 3U,
                "rectangle edge drag did not compose three connected edits");
        auto residuals = sketch::evaluateResiduals(
            dragged->target, dragged->target.entities, {});
        require(residuals &&
                    std::ranges::all_of(*residuals,
                                        &sketch::ConstraintResidual::satisfied),
                "rectangle edge drag broke its reusable constraints");
      });
}

} // namespace

int main() {
  try {
    const auto profile = kearne::testkit::propertyProfile();
    verifyEvaluatedPlaneIdentity(profile);
    verifyEditLifecycle(profile);
    verifyToolComposition(profile);
    verifyDirectManipulation(profile);
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
