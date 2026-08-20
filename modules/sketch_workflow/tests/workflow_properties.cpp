#include <kearne/sketch_workflow/workflow.hpp>

#include <kearne/testkit/property.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

using namespace kearne;
namespace workflow = kearne::sketch_workflow;

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

template <typename Id> Id id(std::uint64_t value) {
  typename Id::RandomTail tail{};
  for (std::size_t index = 0; index < tail.size(); ++index)
    tail[index] = static_cast<std::uint8_t>(
        value >> static_cast<unsigned>((index % 8U) * 8U));
  auto result = Id::create(value & ((std::uint64_t{1} << 48U) - 1U), tail);
  require(result.has_value(), "generated workflow ID was invalid");
  return *result;
}

template <typename Digest> Digest digest(std::uint64_t value) {
  typename Digest::Bytes bytes{};
  for (std::size_t index = 0; index < bytes.size(); ++index)
    bytes[index] = static_cast<std::uint8_t>(value + index * 31U);
  auto result = Digest::fromBytes("blake3", bytes);
  require(result.has_value(), "generated workflow digest was invalid");
  return *result;
}

sketch::LengthValue length(double value) {
  auto result = sketch::LengthValue::fromSi(value);
  require(result.has_value(), "generated workflow length was invalid");
  return *result;
}

class AllowAll final : public engineering::PermissionPolicy {
public:
  Result<void>
  authorize(const engineering::PermissionRequest &) const override {
    return {};
  }
};

class MemorySourceEditor final : public workflow::SourceEditor {
public:
  Result<workflow::SourceRevision> create(JobId, std::string_view functionName,
                                          std::stop_token) override {
    ++creates;
    const std::string source =
        "def " + std::string{functionName} + "():\n    return None\n";
    return revision(source);
  }

  Result<workflow::SourceRevision>
  apply(JobId, std::span<const std::uint8_t> source, std::string_view,
        const sketch::AppliedEdits &edits, std::stop_token) override {
    ++applications;
    document::Bytes updated(source.begin(), source.end());
    updated.push_back(static_cast<std::uint8_t>(edits.sourceEdits.size()));
    return revision(std::move(updated));
  }

  std::size_t creates = 0;
  std::size_t applications = 0;

private:
  static Result<workflow::SourceRevision> revision(std::string_view source) {
    return revision(document::Bytes(source.begin(), source.end()));
  }

  static Result<workflow::SourceRevision> revision(document::Bytes source) {
    auto content = document::contentDigest(source);
    if (!content)
      return std::unexpected(std::move(content.error()));
    return workflow::SourceRevision{std::move(source), std::move(*content)};
  }
};

class IdentitySolver final : public sketch::Solver {
public:
  Result<sketch::SolveResult>
  solve(const sketch::SolveInput &input) const override {
    sketch::SolveResult result;
    result.status = input.definition.entities.empty()
                        ? sketch::SolveStatus::Solved
                        : sketch::SolveStatus::Underconstrained;
    result.geometry = input.definition.entities;
    result.degreesOfFreedom = input.definition.entities.empty() ? 0U : 1U;
    auto residuals = sketch::evaluateResiduals(
        input.definition, result.geometry, input.numerical);
    if (!residuals)
      return std::unexpected(std::move(residuals.error()));
    result.residuals = std::move(*residuals);
    return result;
  }
};

sketch::RectangleToolIds rectangleIds(std::uint64_t seed) {
  return {
      {id<SketchEntityId>(seed + 1U), id<SketchEntityId>(seed + 2U),
       id<SketchEntityId>(seed + 3U), id<SketchEntityId>(seed + 4U)},
      {id<SketchConstraintId>(seed + 10U), id<SketchConstraintId>(seed + 11U),
       id<SketchConstraintId>(seed + 12U), id<SketchConstraintId>(seed + 13U),
       id<SketchConstraintId>(seed + 14U), id<SketchConstraintId>(seed + 15U),
       id<SketchConstraintId>(seed + 16U), id<SketchConstraintId>(seed + 17U)}};
}

workflow::OperationContext operation(std::uint64_t seed, RevisionId base) {
  return {id<RequestId>(seed + 1U),
          id<TransactionId>(seed + 2U),
          id<JobId>(seed + 3U),
          std::move(base),
          Origin::Human,
          id<GestureId>(seed + 4U),
          seed + 5U};
}

workflow::EvaluationIdentity evaluation(std::uint64_t seed) {
  auto session = render::RenderSessionHandle::create(seed + 1U);
  auto generation = render::SceneGeneration::create(seed + 1U);
  require(session && generation, "generated render identity was invalid");
  return {*session, id<ModelBindingId>(seed + 2U),
          digest<EvaluationKey>(seed + 3U), *generation,
          digest<render::SceneDigest>(seed + 4U)};
}

void verifyWorkflow(const testkit::PropertyProfile &profile) {
  constexpr std::uint64_t fixtureSeed = 8'000'000U;
  const ProjectId project = id<ProjectId>(fixtureSeed + 1U);
  const ActorId actor = id<ActorId>(fixtureSeed + 2U);
  auto permissions = std::make_shared<AllowAll>();
  auto content = std::make_shared<document::InMemoryContentStore>(
      document::ContentStoreLimits{1U << 20U, 64U << 20U});
  auto service = engineering::InMemoryEngineeringService::create(
      {project, id<RecordId>(fixtureSeed + 3U),
       id<TransactionId>(fixtureSeed + 4U), actor,
       digest<SchemaSetDigest>(fixtureSeed + 5U), fixtureSeed + 6U,
       "Sketch Project"},
      permissions, content);
  require(service.has_value(), "workflow service creation failed");
  MemorySourceEditor sourceEditor;
  IdentitySolver solver;
  workflow::Workflow workflow{
      {project, actor, id<PermissionContextId>(fixtureSeed + 7U)},
      *content,
      **service,
      sourceEditor,
      solver};
  const RevisionId genesis = (*service)->headSnapshot()->revisionId();
  std::size_t completed = 0;

  testkit::checkProperty(
      "canonical Sketch workflow", profile,
      [&](testkit::Random &random, std::uint64_t index) {
        const std::uint64_t seed = random.next() ^ (index * 64U);
        const std::string pathText =
            "model/profile_" + std::to_string(index) + ".py";
        const RevisionId base = (*service)->headSnapshot()->revisionId();
        CancellationSource cancellation;
        cancellation.request_stop();
        auto cancelled = workflow.create(
            {"model/cancelled_" + std::to_string(index) + ".py", "profile"},
            operation(seed + 10U, base), evaluation(seed + 15U),
            cancellation.get_token());
        require(!cancelled &&
                    cancelled.error().code ==
                        "sketch.workflow.cancelled-before-commit" &&
                    (*service)->headSnapshot()->revisionId() == base,
                "cancelled Sketch creation changed history");
        auto created =
            workflow.create({pathText, "profile"}, operation(seed + 20U, base),
                            evaluation(seed + 30U));
        require(created && created->evaluation &&
                    created->evaluation->replacementScene &&
                    created->definition.entities.empty(),
                "Sketch creation did not commit and evaluate");

        const double x = random.between(-100.0, 100.0);
        const double y = random.between(-100.0, 100.0);
        const double width = random.between(0.001, 10.0);
        const double height = random.between(0.001, 10.0);
        auto edited = workflow.applyTool(
            *created, operation(seed + 40U, created->revision),
            sketch::RectangleToolInput{rectangleIds(seed + 50U),
                                       {length(x), length(y)},
                                       {length(x + width), length(y + height)}},
            evaluation(seed + 60U));
        require(edited && edited->evaluation &&
                    edited->evaluation->replacementScene &&
                    edited->evaluation->solve.solverResultValid &&
                    edited->definition.entities.size() == 4U &&
                    edited->definition.constraints.size() == 8U &&
                    edited->source.digest == edited->definition.sourceDigest &&
                    edited->revision ==
                        (*service)->headSnapshot()->revisionId(),
                "rectangle did not cross the canonical workflow");

        auto path = document::ProjectPath::parse(pathText);
        require(path.has_value(), "workflow path was invalid");
        auto stored = (*service)->headSnapshot()->state().content(*path);
        require(stored && stored->digest == edited->source.digest,
                "committed content does not match the evaluated source");

        auto stale = workflow.applyTool(
            *edited, operation(seed + 70U, genesis),
            sketch::PointToolInput{id<SketchEntityId>(seed + 80U),
                                   {length(x), length(y)}},
            evaluation(seed + 90U));
        ++completed;
        require(!stale && stale.error().code == "sketch.workflow.stale-base" &&
                    (*service)->revisionCount() == 1U + completed * 2U &&
                    sourceEditor.creates == completed &&
                    sourceEditor.applications == completed,
                "stale Sketch workflow input changed history");
      });
}

} // namespace

int main() {
  try {
    verifyWorkflow(kearne::testkit::propertyProfile());
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
