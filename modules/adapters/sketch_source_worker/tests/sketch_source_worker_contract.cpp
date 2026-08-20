#include <kearne/adapters/sketch_source_worker.hpp>

#include <kearne/adapters/ceres_sketch_solver.hpp>
#include <kearne/sketch/tools.hpp>
#include <kearne/sketch_workflow/workflow.hpp>

#include <QCoreApplication>
#include <QDir>

#include <array>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

using namespace kearne;

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

template <typename Id> Id id(std::uint64_t value) {
  typename Id::RandomTail tail{};
  for (std::size_t index = 0; index < tail.size(); ++index)
    tail[index] = static_cast<std::uint8_t>(
        value >> static_cast<unsigned>((index % 8U) * 8U));
  auto result = Id::create(1'700'000'000'000ULL + value, tail);
  if (!result)
    throw std::runtime_error("test UUIDv7 creation failed");
  return *result;
}

template <typename Digest> Digest digest(std::uint64_t value) {
  typename Digest::Bytes bytes{};
  for (std::size_t index = 0; index < bytes.size(); ++index)
    bytes[index] = static_cast<std::uint8_t>(value + index * 37U);
  auto result = Digest::fromBytes("blake3", bytes);
  require(result.has_value(), "test digest creation failed");
  return *result;
}

sketch::LengthValue length(double value) {
  auto result = sketch::LengthValue::fromSi(value);
  if (!result)
    throw std::runtime_error("test length creation failed");
  return *result;
}

sketch::RectangleToolIds rectangleIds(std::uint64_t seed) {
  return {
      {id<SketchEntityId>(seed + 1U), id<SketchEntityId>(seed + 2U),
       id<SketchEntityId>(seed + 3U), id<SketchEntityId>(seed + 4U)},
      {id<SketchConstraintId>(seed + 20U), id<SketchConstraintId>(seed + 21U),
       id<SketchConstraintId>(seed + 22U), id<SketchConstraintId>(seed + 23U),
       id<SketchConstraintId>(seed + 30U), id<SketchConstraintId>(seed + 31U),
       id<SketchConstraintId>(seed + 32U), id<SketchConstraintId>(seed + 33U)}};
}

kearne::adapters::FramedWorkerProcessConfig processConfig() {
  QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
  const QString paths = QStringLiteral(KEARNE_TEST_SDK_ROOT) +
                        QDir::listSeparator() +
                        QStringLiteral(KEARNE_TEST_GENERATED_PYTHON_ROOT);
  environment.insert(QStringLiteral("PYTHONPATH"), paths);
  return {QStringLiteral(KEARNE_TEST_PYTHON),
          {QStringLiteral("-m"), QStringLiteral("kearne._worker")},
          std::move(environment),
          132'096U,
          65'536U,
          10'000,
          30'000};
}

sketch::AppliedEdits rectangle(ContentDigest source, std::uint64_t seed,
                               double width, double height) {
  const sketch::Point2 first{length(0.0), length(0.0)};
  const sketch::Point2 opposite{length(width), length(height)};
  auto applied = sketch::applyTool(
      sketch::Definition{std::move(source), {}, {}},
      sketch::RectangleToolInput{rectangleIds(seed), first, opposite});
  require(applied.has_value(), "rectangle tool input was invalid");
  return std::move(*applied);
}

class AllowAll final : public engineering::PermissionPolicy {
public:
  Result<void>
  authorize(const engineering::PermissionRequest &) const override {
    return {};
  }
};

sketch_workflow::OperationContext operation(std::uint64_t seed,
                                            RevisionId base) {
  return {id<RequestId>(seed + 1U),
          id<TransactionId>(seed + 2U),
          id<JobId>(seed + 3U),
          std::move(base),
          Origin::Human,
          id<GestureId>(seed + 4U),
          1'700'000'000'000ULL + seed};
}

sketch_workflow::EvaluationIdentity evaluation(std::uint64_t seed) {
  auto session = render::RenderSessionHandle::create(seed + 1U);
  auto generation = render::SceneGeneration::create(seed + 1U);
  require(session && generation, "test render identity creation failed");
  return {*session, id<ModelBindingId>(seed + 2U),
          digest<EvaluationKey>(seed + 3U), *generation,
          digest<render::SceneDigest>(seed + 4U)};
}

void verifyCanonicalWorkflow(adapters::SketchSourceWorker &sourceEditor) {
  const ProjectId project = id<ProjectId>(200'000U);
  const ActorId actor = id<ActorId>(200'001U);
  auto permissions = std::make_shared<AllowAll>();
  auto content = std::make_shared<document::InMemoryContentStore>(
      document::ContentStoreLimits{1U << 20U, 4U << 20U});
  auto service = engineering::InMemoryEngineeringService::create(
      {project, id<RecordId>(200'002U), id<TransactionId>(200'003U), actor,
       digest<SchemaSetDigest>(200'004U), 1'700'000'200'004ULL,
       "Sketch Workflow"},
      permissions, content);
  require(service.has_value(), "canonical workflow service creation failed");
  adapters::CeresSketchSolver solver;
  sketch_workflow::Workflow workflow{
      {project, actor, id<PermissionContextId>(200'005U)},
      *content,
      **service,
      sourceEditor,
      solver};

  auto created = workflow.create(
      {"model/profile.py", "profile"},
      operation(210'000U, (*service)->headSnapshot()->revisionId()),
      evaluation(220'000U));
  require(created && created->evaluation &&
              created->evaluation->replacementScene,
          "real Sketch creation did not commit and evaluate");
  auto edited = workflow.applyTool(
      *created, operation(230'000U, created->revision),
      sketch::RectangleToolInput{rectangleIds(240'000U),
                                 {length(-0.04), length(-0.025)},
                                 {length(0.04), length(0.025)}},
      evaluation(250'000U));
  require(edited && edited->evaluation &&
              edited->evaluation->replacementScene &&
              edited->evaluation->solve.solverResultValid &&
              edited->evaluation->solve.status ==
                  sketch::SolveStatus::Underconstrained &&
              edited->evaluation->solve.degreesOfFreedom == 4U &&
              edited->evaluation->replacementScene->primitives().size() == 4U &&
              edited->definition.sourceDigest == edited->source.digest &&
              (*service)->revisionCount() == 3U,
          "real rectangle did not cross source, history, solve, and scene");
}

} // namespace

int main(int argc, char **argv) {
  QCoreApplication application{argc, argv};
  adapters::SketchSourceWorker worker{processConfig(),
                                      id<WorkerInstanceId>(1U)};
  auto created = worker.create(id<JobId>(2U), "profile");
  require(created.has_value(), "empty Sketch source creation failed");

  for (std::uint64_t index = 0; index < 128U; ++index) {
    const double width = 0.001 + static_cast<double>(index % 97U) * 0.001;
    const double height = 0.001 + static_cast<double>(index % 53U) * 0.001;
    const sketch::AppliedEdits edits =
        rectangle(created->digest, 1'000U + index * 100U, width, height);
    auto transformed = worker.apply(id<JobId>(10'000U + index), created->bytes,
                                    "profile", edits);
    require(transformed.has_value(), "rectangle source transform failed");
    require(transformed->digest != created->digest &&
                transformed->bytes.size() > created->bytes.size(),
            "rectangle transform did not publish new native source");
  }
  require(worker.processGeneration() == 1U,
          "source transforms did not reuse one warm worker");

  const auto otherDigest = document::contentDigest(document::Bytes{1U});
  require(otherDigest.has_value(), "stale digest creation failed");
  const sketch::AppliedEdits stale =
      rectangle(*otherDigest, 90'000U, 0.04, 0.03);
  auto rejected =
      worker.apply(id<JobId>(90'100U), created->bytes, "profile", stale);
  require(!rejected && rejected.error().code == "worker.source.stale-input",
          "stale source precondition was not returned structurally");

  const sketch::AppliedEdits graphical =
      rectangle(created->digest, 91'000U, 0.04, 0.03);
  auto graphicalSource =
      worker.apply(id<JobId>(91'100U), created->bytes, "profile", graphical);
  require(graphicalSource.has_value(), "source replacement setup failed");
  std::string replacement(graphicalSource->bytes.begin(),
                          graphicalSource->bytes.end());
  const std::size_t coordinate = replacement.find("m(0.04)");
  require(coordinate != std::string::npos,
          "generated source has no editable coordinate");
  replacement.replace(coordinate, std::string_view{"m(0.04)"}.size(),
                      "m(0.05)");
  document::Bytes replacementBytes(replacement.begin(), replacement.end());
  auto replaced = worker.replace(id<JobId>(91'200U), replacementBytes,
                                 "profile", graphicalSource->digest);
  require(replaced &&
              replaced->definition.sourceDigest == replaced->source.digest &&
              replaced->definition.entities.size() == 4U &&
              replaced->definition != graphical.target,
          "direct native source edit did not return its recognized definition");
  verifyCanonicalWorkflow(worker);
  require(worker.processGeneration() == 1U,
          "canonical workflow did not reuse the warm source worker");
  worker.stop();
  return 0;
}
