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

sketch::DimensionlessValue dimensionless(double value) {
  auto result = sketch::DimensionlessValue::fromSi(value);
  require(result.has_value(), "generated dimensionless value was invalid");
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
        sketch::Definition current{digest<ContentDigest>(seed), {}, {}, {}};
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
              sketch::Block{constraintIds[position], entityIds[position]}});
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
        const double xDirection = random.next() % 2U == 0U ? -1.0 : 1.0;
        const double yDirection = random.next() % 2U == 0U ? -1.0 : 1.0;
        const sketch::Point2 opposite{length(x + xDirection * size),
                                      length(y + yDirection * size * 0.75)};
        sketch::Definition current{digest<ContentDigest>(seed), {}, {}, {}};
        const std::array<sketch::ToolInput, 4> primitives{
            sketch::PointToolInput{{id<SketchObjectId>(seed + 101U),
                                    id<SketchEntityId>(seed + 1U)},
                                   first},
            sketch::LineToolInput{{id<SketchObjectId>(seed + 102U),
                                   id<SketchEntityId>(seed + 2U)},
                                  first,
                                  opposite},
            sketch::CircleToolInput{{id<SketchObjectId>(seed + 103U),
                                     id<SketchEntityId>(seed + 3U)},
                                    first,
                                    length(size)},
            sketch::ArcToolInput{{id<SketchObjectId>(seed + 104U),
                                  id<SketchEntityId>(seed + 4U)},
                                 first,
                                 length(size),
                                 angle(0.2),
                                 angle(std::numbers::pi)}};
        for (const sketch::ToolInput &tool : primitives) {
          auto applied = sketch::applyTool(current, tool);
          require(applied.has_value(), "primitive tool failed");
          current = std::move(applied->target);
        }

        sketch::RectangleToolIds rectangleIds{
            id<SketchObjectId>(seed + 9U),
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
        require(rectangle.has_value() && rectangle->sourceEdits.size() == 13U &&
                    rectangle->target.objects.size() == 5U &&
                    rectangle->target.entities.size() == 8U &&
                    rectangle->target.constraints.size() == 8U,
                "rectangle did not compose through generic edits");
        const std::array expectedPrimitiveLabels{
            std::string_view{"Point 1"}, std::string_view{"Line 1"},
            std::string_view{"Circle 1"}, std::string_view{"Arc 1"}};
        for (std::size_t primitiveIndex = 0;
             primitiveIndex < expectedPrimitiveLabels.size(); ++primitiveIndex)
          require(
              rectangle->target.objects[primitiveIndex].label ==
                      expectedPrimitiveLabels[primitiveIndex] &&
                  rectangle->target.objects[primitiveIndex].members.size() ==
                      1U,
              "primitive tool lost its human identity");
        const sketch::SketchObject &object = rectangle->target.objects.back();
        require(object.label == "Rectangle 1" && object.members.size() == 4U,
                "rectangle lost its human identity");
        const double minimumX = std::min(first.x.si(), opposite.x.si());
        const double maximumX = std::max(first.x.si(), opposite.x.si());
        const double minimumY = std::min(first.y.si(), opposite.y.si());
        const double maximumY = std::max(first.y.si(), opposite.y.si());
        for (const sketch::SketchObjectMember &member : object.members) {
          const auto found = std::ranges::find(rectangle->target.entities,
                                               member.entity, sketch::entityId);
          require(found != rectangle->target.entities.end() &&
                      std::holds_alternative<sketch::LineEntity>(*found),
                  "rectangle member does not resolve to a line");
          const auto &line = std::get<sketch::LineEntity>(*found);
          const bool correct =
              member.role == "bottom"
                  ? line.start.y.si() == minimumY && line.end.y.si() == minimumY
              : member.role == "right"
                  ? line.start.x.si() == maximumX && line.end.x.si() == maximumX
              : member.role == "top"
                  ? line.start.y.si() == maximumY && line.end.y.si() == maximumY
              : member.role == "left"
                  ? line.start.x.si() == minimumX && line.end.x.si() == minimumX
                  : false;
          require(correct, "rectangle member role changed with drag direction");
        }
        auto residuals = sketch::evaluateResiduals(
            rectangle->target, rectangle->target.entities, {});
        require(residuals.has_value() &&
                    std::ranges::all_of(*residuals,
                                        &sketch::ConstraintResidual::satisfied),
                "rectangle tool emitted unsatisfied constraints");
        sketch::RectangleToolIds secondIds{
            id<SketchObjectId>(seed + 1'000U),
            {id<SketchEntityId>(seed + 1'001U),
             id<SketchEntityId>(seed + 1'002U),
             id<SketchEntityId>(seed + 1'003U),
             id<SketchEntityId>(seed + 1'004U)},
            {id<SketchConstraintId>(seed + 1'010U),
             id<SketchConstraintId>(seed + 1'011U),
             id<SketchConstraintId>(seed + 1'012U),
             id<SketchConstraintId>(seed + 1'013U),
             id<SketchConstraintId>(seed + 1'014U),
             id<SketchConstraintId>(seed + 1'015U),
             id<SketchConstraintId>(seed + 1'016U),
             id<SketchConstraintId>(seed + 1'017U)}};
        auto secondRectangle = sketch::applyTool(
            rectangle->target,
            sketch::RectangleToolInput{
                secondIds,
                {length(x + size * 2.0), length(y + size * 2.0)},
                {length(x + size * 3.0), length(y + size * 3.0)},
                false});
        require(
            secondRectangle && secondRectangle->target.objects.size() == 6U &&
                secondRectangle->target.objects.back().label == "Rectangle 2",
            "rectangle automatic names are not unique and stable");
        sketch::SlotToolIds slotIds{id<SketchObjectId>(seed + 2'000U),
                                    {id<SketchEntityId>(seed + 2'001U),
                                     id<SketchEntityId>(seed + 2'002U),
                                     id<SketchEntityId>(seed + 2'003U),
                                     id<SketchEntityId>(seed + 2'004U)},
                                    {id<SketchConstraintId>(seed + 2'010U),
                                     id<SketchConstraintId>(seed + 2'011U),
                                     id<SketchConstraintId>(seed + 2'012U),
                                     id<SketchConstraintId>(seed + 2'013U),
                                     id<SketchConstraintId>(seed + 2'014U),
                                     id<SketchConstraintId>(seed + 2'015U),
                                     id<SketchConstraintId>(seed + 2'016U),
                                     id<SketchConstraintId>(seed + 2'017U),
                                     id<SketchConstraintId>(seed + 2'018U)}};
        const bool slotConstruction = random.next() % 2U != 0U;
        const bool oblong = random.next() % 2U != 0U;
        auto slot = sketch::applyTool(
            secondRectangle->target,
            sketch::SlotToolInput{slotIds,
                                  {length(x + size * 4.0), length(y)},
                                  {length(x + size * 6.0), length(y + size)},
                                  length(size * 0.25),
                                  slotConstruction,
                                  oblong ? sketch::SketchObjectKind::Oblong
                                         : sketch::SketchObjectKind::Slot});
        require(slot.has_value(), "slot tool rejected valid geometry");
        require(slot->sourceEdits.size() == 14U,
                "slot emitted the wrong source edit batch");
        require(slot->target.objects.size() == 7U &&
                    slot->target.objects.back().label ==
                        (oblong ? "Oblong 1" : "Slot 1") &&
                    slot->target.objects.back().kind ==
                        (oblong ? sketch::SketchObjectKind::Oblong
                                : sketch::SketchObjectKind::Slot) &&
                    slot->target.objects.back().members.size() == 4U,
                "slot lost its human object identity");
        const std::size_t profileCount =
            sketch::closedProfileCount(slot->target);
        const std::size_t priorProfileCount =
            sketch::closedProfileCount(secondRectangle->target);
        if (profileCount != priorProfileCount + (slotConstruction ? 0U : 1U))
          throw std::runtime_error("slot profile count was " +
                                   std::to_string(profileCount));
        auto slotResiduals =
            sketch::evaluateResiduals(slot->target, slot->target.entities, {});
        require(slotResiduals &&
                    std::ranges::all_of(*slotResiduals,
                                        &sketch::ConstraintResidual::satisfied),
                "slot tool emitted unsatisfied topology constraints");
        sketch::ArcSlotToolIds arcSlotIds{
            id<SketchObjectId>(seed + 3'000U),
            {id<SketchEntityId>(seed + 3'001U),
             id<SketchEntityId>(seed + 3'002U),
             id<SketchEntityId>(seed + 3'003U),
             id<SketchEntityId>(seed + 3'004U)},
            {id<SketchConstraintId>(seed + 3'010U),
             id<SketchConstraintId>(seed + 3'011U),
             id<SketchConstraintId>(seed + 3'012U),
             id<SketchConstraintId>(seed + 3'013U),
             id<SketchConstraintId>(seed + 3'014U),
             id<SketchConstraintId>(seed + 3'015U),
             id<SketchConstraintId>(seed + 3'016U),
             id<SketchConstraintId>(seed + 3'017U),
             id<SketchConstraintId>(seed + 3'018U),
             id<SketchConstraintId>(seed + 3'019U)}};
        const bool arcSlotConstruction = random.next() % 2U != 0U;
        auto arcSlot = sketch::applyTool(
            slot->target, sketch::ArcSlotToolInput{
                              arcSlotIds,
                              {length(x + size * 8.0), length(y + size * 3.0)},
                              length(size),
                              angle(0.2),
                              angle(random.next() % 2U == 0U ? 1.4 : -1.4),
                              length(size * 0.2),
                              arcSlotConstruction});
        require(arcSlot && arcSlot->sourceEdits.size() == 15U &&
                    arcSlot->target.objects.size() == 8U &&
                    arcSlot->target.objects.back().label == "Arc Slot 1" &&
                    arcSlot->target.objects.back().kind ==
                        sketch::SketchObjectKind::ArcSlot &&
                    arcSlot->target.objects.back().members.size() == 4U,
                "arc slot lost its canonical human object");
        const std::size_t arcSlotProfiles =
            sketch::closedProfileCount(arcSlot->target);
        const std::size_t slotProfiles =
            sketch::closedProfileCount(slot->target);
        require(arcSlotProfiles ==
                    slotProfiles + (arcSlotConstruction ? 0U : 1U),
                "arc slot did not form one closed profile");
        auto arcSlotResiduals = sketch::evaluateResiduals(
            arcSlot->target, arcSlot->target.entities, {});
        require(arcSlotResiduals &&
                    std::ranges::all_of(*arcSlotResiduals,
                                        &sketch::ConstraintResidual::satisfied),
                "arc slot emitted unsatisfied topology constraints");
        const bool closed = random.next() % 2U != 0U;
        const bool polylineConstruction = random.next() % 2U != 0U;
        const std::size_t segmentCount = closed ? 4U : 3U;
        const std::size_t joinCount = closed ? 4U : 2U;
        std::vector<SketchEntityId> segments;
        std::vector<SketchConstraintId> joins;
        for (std::size_t segment = 0U; segment < segmentCount; ++segment)
          segments.push_back(id<SketchEntityId>(seed + 4'001U + segment));
        for (std::size_t join = 0U; join < joinCount; ++join)
          joins.push_back(id<SketchConstraintId>(seed + 4'010U + join));
        auto polyline = sketch::applyTool(
            arcSlot->target,
            sketch::PolylineToolInput{
                {id<SketchObjectId>(seed + 4'000U), std::move(segments),
                 std::move(joins)},
                {{length(x), length(y + size * 8.0)},
                 {length(x + size), length(y + size * 9.0)},
                 {length(x + size * 2.0), length(y + size * 8.0)},
                 {length(x + size * 3.0), length(y + size * 9.0)}},
                closed,
                polylineConstruction});
        require(
            polyline &&
                polyline->sourceEdits.size() == segmentCount + joinCount + 1U &&
                polyline->target.objects.size() == 9U &&
                polyline->target.objects.back().label == "Polyline 1" &&
                polyline->target.objects.back().kind ==
                    sketch::SketchObjectKind::Polyline &&
                polyline->target.objects.back().members.size() == segmentCount,
            "polyline lost its canonical human object or atomic edits");
        require(sketch::closedProfileCount(polyline->target) ==
                    arcSlotProfiles +
                        (closed && !polylineConstruction ? 1U : 0U),
                "polyline profile topology is incorrect");
        auto polylineResiduals = sketch::evaluateResiduals(
            polyline->target, polyline->target.entities, {});
        require(polylineResiduals &&
                    std::ranges::all_of(*polylineResiduals,
                                        &sketch::ConstraintResidual::satisfied),
                "polyline emitted unsatisfied topology constraints");
        const std::size_t sideCount =
            static_cast<std::size_t>(random.next() % 30U + 3U);
        const bool polygonConstruction = random.next() % 2U != 0U;
        std::vector<SketchEntityId> sides;
        std::vector<SketchConstraintId> polygonConstraints;
        sides.reserve(sideCount);
        polygonConstraints.reserve(3U * sideCount - 2U);
        for (std::size_t side = 0U; side < sideCount; ++side)
          sides.push_back(id<SketchEntityId>(seed + 5'001U + side));
        for (std::size_t constraint = 0U; constraint < 3U * sideCount - 2U;
             ++constraint)
          polygonConstraints.push_back(
              id<SketchConstraintId>(seed + 5'200U + constraint));
        auto polygon = sketch::applyTool(
            polyline->target,
            sketch::RegularPolygonToolInput{
                {id<SketchObjectId>(seed + 5'000U), std::move(sides),
                 std::move(polygonConstraints)},
                {length(x + size * 8.0), length(y + size * 8.0)},
                {length(x + size * 9.0), length(y + size * 8.0)},
                sideCount,
                polygonConstruction});
        if (!polygon)
          throw std::runtime_error(polygon.error().code + ": " +
                                   polygon.error().summary);
        require(polygon->sourceEdits.size() == 4U * sideCount - 1U,
                "regular polygon emitted the wrong atomic edit count");
        require(polygon->target.objects.size() == 10U &&
                    polygon->target.objects.back().kind ==
                        sketch::SketchObjectKind::RegularPolygon &&
                    polygon->target.objects.back().members.size() == sideCount,
                "regular polygon lost its canonical human object");
        require(sketch::closedProfileCount(polygon->target) ==
                    sketch::closedProfileCount(polyline->target) +
                        (polygonConstruction ? 0U : 1U),
                "regular polygon profile topology is incorrect");
        auto polygonResiduals = sketch::evaluateResiduals(
            polygon->target, polygon->target.entities, {});
        require(polygonResiduals &&
                    std::ranges::all_of(*polygonResiduals,
                                        &sketch::ConstraintResidual::satisfied),
                "regular polygon emitted unsatisfied regularity constraints");
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
            id<SketchObjectId>(seed),
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
            {digest<ContentDigest>(seed), {}, {}, {}},
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

void verifyConicTools(const testkit::PropertyProfile &profile) {
  testkit::checkProperty(
      "exact ellipse and elliptical arc tools", profile,
      [](testkit::Random &random, std::uint64_t index) {
        const std::uint64_t seed = random.next() ^ (index * 128U);
        const double x = random.between(-100.0, 100.0);
        const double y = random.between(-100.0, 100.0);
        const double major = random.between(0.01, 10.0);
        const double minor = major * random.between(0.05, 1.0);
        const double rotation = random.between(-4.0 * std::numbers::pi,
                                               4.0 * std::numbers::pi);
        const double start = random.between(-std::numbers::pi,
                                            std::numbers::pi);
        const double sweep = random.between(0.01, 2.0 * std::numbers::pi);
        const sketch::Point2 center{length(x), length(y)};
        const SketchEntityId ellipseEntity = id<SketchEntityId>(seed + 1U);
        const SketchEntityId arcEntity = id<SketchEntityId>(seed + 2U);
        sketch::Definition definition{digest<ContentDigest>(seed), {}, {}, {}};
        auto ellipse = sketch::applyTool(
            definition,
            sketch::EllipseToolInput{{id<SketchObjectId>(seed + 3U),
                                      ellipseEntity},
                                     center,
                                     length(major),
                                     length(minor),
                                     angle(rotation),
                                     false});
        require(ellipse && ellipse->sourceEdits.size() == 2U &&
                    ellipse->target.objects.back().label == "Ellipse 1" &&
                    sketch::closedProfileCount(ellipse->target) == 1U,
                "ellipse tool lost exact geometry or human identity");
        auto arc = sketch::applyTool(
            ellipse->target,
            sketch::EllipticalArcToolInput{
                {id<SketchObjectId>(seed + 4U), arcEntity}, center,
                length(major), length(minor), angle(rotation), angle(start),
                angle(start + sweep), true});
        require(arc && arc->sourceEdits.size() == 2U &&
                    arc->target.objects.back().label == "Elliptical Arc 1" &&
                    sketch::closedProfileCount(arc->target) == 1U &&
                    sketch::validate(arc->target, {}).has_value(),
                "elliptical arc tool lost exact geometry or human identity");
        auto majorPoint = sketch::resolvePoint(
            arc->target, {ellipseEntity, sketch::PointKey::Major});
        auto minorPoint = sketch::resolvePoint(
            arc->target, {ellipseEntity, sketch::PointKey::Minor});
        require(majorPoint && minorPoint &&
                    std::abs(majorPoint->x.si() -
                             (x + major * std::cos(rotation))) < 1.0e-10 &&
                    std::abs(majorPoint->y.si() -
                             (y + major * std::sin(rotation))) < 1.0e-10 &&
                    std::abs(minorPoint->x.si() -
                             (x - minor * std::sin(rotation))) < 1.0e-10 &&
                    std::abs(minorPoint->y.si() -
                             (y + minor * std::cos(rotation))) < 1.0e-10,
                "ellipse semantic handles do not resolve exact axes");
        auto resized = sketch::dragCurve(
            arc->target,
            {ellipseEntity,
             *majorPoint,
             {length(x + 1.5 * major * std::cos(rotation)),
              length(y + 1.5 * major * std::sin(rotation))}});
        require(resized &&
                    std::abs(std::get<sketch::EllipseEntity>(
                                 resized->target.entities.front())
                                     .minorRadius.si() -
                             1.5 * minor) < 1.0e-10,
                "ellipse curve drag did not preserve its aspect ratio");
      });
}

void verifyUnboundedConicTools(const testkit::PropertyProfile &profile) {
  testkit::checkProperty(
      "exact hyperbolic and parabolic arc tools", profile,
      [](testkit::Random &random, std::uint64_t index) {
        const std::uint64_t seed = random.next() ^ (index * 256U);
        const double x = random.between(-100.0, 100.0);
        const double y = random.between(-100.0, 100.0);
        const double major = random.between(0.01, 10.0);
        const double minor = random.between(0.01, 10.0);
        const double focal = random.between(0.01, 10.0);
        const double rotation = random.between(-4.0 * std::numbers::pi,
                                               4.0 * std::numbers::pi);
        const double hyperStart = random.between(-1.5, -0.05);
        const double hyperEnd = random.between(0.05, 1.5);
        const double parabolaStart = random.between(-5.0, -0.05);
        const double parabolaEnd = random.between(0.05, 5.0);
        const sketch::Point2 anchor{length(x), length(y)};
        const SketchEntityId hyperbolaEntity =
            id<SketchEntityId>(seed + 1U);
        const SketchEntityId parabolaEntity =
            id<SketchEntityId>(seed + 2U);
        sketch::Definition definition{digest<ContentDigest>(seed), {}, {}, {}};
        auto hyperbola = sketch::applyTool(
            definition,
            sketch::HyperbolicArcToolInput{
                {id<SketchObjectId>(seed + 3U), hyperbolaEntity}, anchor,
                length(major), length(minor), angle(rotation),
                dimensionless(hyperStart), dimensionless(hyperEnd), false});
        require(hyperbola && hyperbola->sourceEdits.size() == 2U &&
                    hyperbola->target.objects.back().label ==
                        "Hyperbolic Arc 1" &&
                    sketch::closedProfileCount(hyperbola->target) == 0U &&
                    sketch::validate(hyperbola->target, {}).has_value(),
                "hyperbolic arc tool lost exact geometry or human identity");
        auto parabola = sketch::applyTool(
            hyperbola->target,
            sketch::ParabolicArcToolInput{
                {id<SketchObjectId>(seed + 4U), parabolaEntity}, anchor,
                length(focal), angle(rotation), length(parabolaStart),
                length(parabolaEnd), true});
        require(parabola && parabola->sourceEdits.size() == 2U &&
                    parabola->target.objects.back().label ==
                        "Parabolic Arc 1" &&
                    sketch::closedProfileCount(parabola->target) == 0U &&
                    sketch::validate(parabola->target, {}).has_value(),
                "parabolic arc tool lost exact geometry or human identity");

        const auto hyperFocus = sketch::resolvePoint(
            parabola->target, {hyperbolaEntity, sketch::PointKey::Focus});
        const auto hyperStartPoint = sketch::resolvePoint(
            parabola->target, {hyperbolaEntity, sketch::PointKey::Start});
        const auto parabolaFocus = sketch::resolvePoint(
            parabola->target, {parabolaEntity, sketch::PointKey::Focus});
        const auto parabolaEndPoint = sketch::resolvePoint(
            parabola->target, {parabolaEntity, sketch::PointKey::End});
        const double cosine = std::cos(rotation);
        const double sine = std::sin(rotation);
        const double hyperFocusDistance = std::hypot(major, minor);
        const double hyperLocalX = major * std::cosh(hyperStart);
        const double hyperLocalY = minor * std::sinh(hyperStart);
        const double parabolaLocalX =
            parabolaEnd * parabolaEnd / (4.0 * focal);
        require(
            hyperFocus && hyperStartPoint && parabolaFocus &&
                parabolaEndPoint &&
                std::abs(hyperFocus->x.si() -
                         (x + cosine * hyperFocusDistance)) < 1.0e-10 &&
                std::abs(hyperFocus->y.si() -
                         (y + sine * hyperFocusDistance)) < 1.0e-10 &&
                std::abs(hyperStartPoint->x.si() -
                         (x + cosine * hyperLocalX - sine * hyperLocalY)) <
                    1.0e-10 &&
                std::abs(parabolaFocus->x.si() - (x + cosine * focal)) <
                    1.0e-10 &&
                std::abs(parabolaEndPoint->x.si() -
                         (x + cosine * parabolaLocalX - sine * parabolaEnd)) <
                    1.0e-10,
            "unbounded conic semantic points lost exact geometry");

        const sketch::Point2 majorPoint{length(x + cosine * major),
                                        length(y + sine * major)};
        const double scale = 1.25;
        auto resized = sketch::dragCurve(
            parabola->target,
            {hyperbolaEntity,
             majorPoint,
             {length(x + cosine * major * scale),
              length(y + sine * major * scale)}});
        const auto resizedEntity =
            resized ? std::ranges::find(resized->target.entities,
                                        hyperbolaEntity, sketch::entityId)
                    : parabola->target.entities.end();
        require(resized && resizedEntity != resized->target.entities.end() &&
                    std::abs(std::get<sketch::HyperbolicArcEntity>(
                                 *resizedEntity)
                                 .minorRadius.si() -
                             scale * minor) < 1.0e-10,
                "hyperbolic arc drag did not preserve its axis ratio");
      });
}

void verifyBSplineTool(const testkit::PropertyProfile &profile) {
  testkit::checkProperty(
      "exact rational B-spline tool and incidence", profile,
      [](testkit::Random &random, std::uint64_t index) {
        const std::uint64_t seed = random.next() ^ (index * 64U);
        const double x = random.between(-100.0, 100.0);
        const double y = random.between(-100.0, 100.0);
        const double extent = random.between(0.01, 10.0);
        const double weight1 = random.between(0.25, 4.0);
        const double weight2 = random.between(0.25, 4.0);
        const std::vector<sketch::Point2> poles{
            {length(x), length(y)},
            {length(x + extent), length(y + extent * 1.5)},
            {length(x + extent * 2.0), length(y - extent)},
            {length(x + extent * 3.0), length(y + extent * 0.25)}};
        const std::vector<sketch::DimensionlessValue> knots{
            dimensionless(0.0), dimensionless(0.0), dimensionless(0.0),
            dimensionless(0.0), dimensionless(1.0), dimensionless(1.0),
            dimensionless(1.0), dimensionless(1.0)};
        const std::vector<sketch::DimensionlessValue> weights{
            dimensionless(1.0), dimensionless(weight1),
            dimensionless(weight2), dimensionless(1.0)};
        const SketchEntityId splineId = id<SketchEntityId>(seed + 1U);
        sketch::Definition definition{digest<ContentDigest>(seed), {}, {}, {}};
        auto applied = sketch::applyTool(
            definition,
            sketch::BSplineToolInput{{id<SketchObjectId>(seed + 2U), splineId},
                                     poles, knots, weights, 3U, false, false});
        require(applied && applied->sourceEdits.size() == 2U &&
                    applied->target.objects.front().label == "B-spline 1" &&
                    sketch::validate(applied->target, {}).has_value() &&
                    sketch::closedProfileCount(applied->target) == 0U,
                "B-spline tool lost its exact geometry or human identity");
        const auto start = sketch::resolvePoint(
            applied->target, {splineId, sketch::PointKey::Start});
        const auto end = sketch::resolvePoint(
            applied->target, {splineId, sketch::PointKey::End});
        require(start && end && *start == poles.front() && *end == poles.back(),
                "clamped B-spline endpoints are not exact");

        const double parameter = random.between(0.05, 0.95);
        const double inverse = 1.0 - parameter;
        const std::array basis{inverse * inverse * inverse,
                               3.0 * inverse * inverse * parameter,
                               3.0 * inverse * parameter * parameter,
                               parameter * parameter * parameter};
        const std::array rationalWeights{1.0, weight1, weight2, 1.0};
        double denominator = 0.0;
        double pointX = 0.0;
        double pointY = 0.0;
        for (std::size_t pole = 0U; pole < poles.size(); ++pole) {
          const double contribution = basis[pole] * rationalWeights[pole];
          denominator += contribution;
          pointX += contribution * poles[pole].x.si();
          pointY += contribution * poles[pole].y.si();
        }
        pointX /= denominator;
        pointY /= denominator;
        const SketchEntityId pointId = id<SketchEntityId>(seed + 3U);
        sketch::Definition incidence = applied->target;
        incidence.entities.push_back(
            sketch::PointEntity{pointId, {length(pointX), length(pointY)}});
        incidence.constraints.push_back(sketch::PointOnObject{
            id<SketchConstraintId>(seed + 4U),
            {pointId, sketch::PointKey::Point}, splineId});
        auto residuals =
            sketch::evaluateResiduals(incidence, incidence.entities, {});
        require(residuals && residuals->size() == 1U &&
                    residuals->front().satisfied,
                "exact point on rational B-spline was not recognized");
      });
}

void verifyAxisAlignmentRemoval(const testkit::PropertyProfile &profile) {
  testkit::checkProperty(
      "axis alignment removal deletes only selected axis constraints", profile,
      [](testkit::Random &random, std::uint64_t index) {
        const std::uint64_t seed = random.next() ^ (index * 16U);
        const SketchEntityId horizontal = id<SketchEntityId>(seed + 1U);
        const SketchEntityId vertical = id<SketchEntityId>(seed + 2U);
        const SketchEntityId untouched = id<SketchEntityId>(seed + 3U);
        const std::array selected{horizontal, vertical};
        sketch::Definition definition{
            digest<ContentDigest>(seed),
            {},
            {sketch::LineEntity{horizontal,
                                {length(0.0), length(0.0)},
                                {length(1.0), length(0.0)}},
             sketch::LineEntity{vertical,
                                {length(0.0), length(0.0)},
                                {length(0.0), length(1.0)}},
             sketch::LineEntity{untouched,
                                {length(2.0), length(0.0)},
                                {length(3.0), length(0.0)}}},
            {sketch::Horizontal{id<SketchConstraintId>(seed + 4U), horizontal},
             sketch::Vertical{id<SketchConstraintId>(seed + 5U), vertical},
             sketch::Horizontal{id<SketchConstraintId>(seed + 6U), untouched}}};
        auto removed = sketch::removeAxisAlignment(definition, selected);
        require(removed && removed->target.constraints.size() == 1U &&
                    removed->sourceEdits.size() == 2U &&
                    std::get<sketch::Horizontal>(
                        removed->target.constraints.front())
                            .line == untouched &&
                    std::ranges::all_of(
                        removed->sourceEdits,
                        [](const auto &intent) {
                          return intent.action ==
                                     sketch::SourceEditAction::Delete &&
                                 intent.section ==
                                     sketch::SourceSection::Constraints;
                        }),
                "axis alignment removal changed unrelated Sketch state");
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
    verifyConicTools(profile);
    verifyUnboundedConicTools(profile);
    verifyBSplineTool(profile);
    verifyAxisAlignmentRemoval(profile);
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
