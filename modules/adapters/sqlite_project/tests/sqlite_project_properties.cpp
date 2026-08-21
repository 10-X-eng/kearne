#include <kearne/adapters/sqlite_project_store.hpp>

#include <kearne/document/content_store.hpp>
#include <kearne/document/project_state_access.hpp>
#include <kearne/testkit/property.hpp>

#include <sqlite3.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>

namespace {

using namespace kearne;
using namespace kearne::adapters;
using namespace kearne::document;
using namespace kearne::engineering;
using namespace kearne::persistence;

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

template <typename Value>
Value id(std::uint64_t timestamp, std::uint64_t randomValue) {
  typename Value::RandomTail tail{};
  for (std::size_t index = 0; index < tail.size(); ++index)
    tail[index] = static_cast<std::uint8_t>(randomValue >> ((index % 8U) * 8U));
  auto result = Value::create(timestamp, tail);
  require(result.has_value(), "test identifier could not be created");
  return std::move(*result);
}

template <typename Value> Value digest(std::uint8_t fill) {
  typename Value::Bytes bytes{};
  bytes.fill(fill);
  auto result = Value::fromBytes("blake3", bytes);
  require(result.has_value(), "test digest could not be created");
  return std::move(*result);
}

struct TemporaryProject {
  explicit TemporaryProject(std::uint64_t index, std::uint64_t salt)
      : path(std::filesystem::temp_directory_path() /
             ("kearne-store-" + std::to_string(index) + "-" +
              std::to_string(salt) + ".kearne")) {
    erase();
  }
  ~TemporaryProject() { erase(); }
  void erase() const {
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(path.string() + "-wal", ignored);
    std::filesystem::remove(path.string() + "-shm", ignored);
  }
  std::filesystem::path path;
};

struct ProjectFixture {
  ProjectId project;
  RecordId rootRecord;
  ActorId actor;
  SchemaSetDigest schema;
  DurableRevision genesis;
};

ProjectFixture projectFixture(std::uint64_t index, testkit::Random &random) {
  const ProjectId project = id<ProjectId>(index + 1'000U, random.next());
  const RecordId rootRecord = id<RecordId>(index + 2'000U, random.next());
  const ActorId actor = id<ActorId>(index + 3'000U, random.next());
  const SchemaSetDigest schema =
      digest<SchemaSetDigest>(static_cast<std::uint8_t>(random.next()));
  auto empty = ProjectState::create(project, schema);
  require(empty.has_value(), "test project could not initialize");
  EngineeringRecord root{rootRecord,
                         std::nullopt,
                         Lifecycle::Active,
                         {"kearne.project.root", 1, {'T', 'e', 's', 't'}},
                         {actor, Origin::System, std::nullopt, index + 1U}};
  MutationBatch mutations{CreateRecord{root}};
  auto state = internal::ProjectStateAccess::apply(*empty, mutations);
  require(state.has_value(), "test genesis state was rejected");
  RevisionEnvelope envelope{{},
                            id<TransactionId>(index + 4'000U, random.next()),
                            std::nullopt,
                            std::nullopt,
                            actor,
                            Origin::System,
                            std::nullopt,
                            std::nullopt,
                            schema,
                            index + 1U,
                            state->rootDigest(),
                            {"project.create"},
                            mutations};
  auto revision = revisionId(envelope);
  require(revision.has_value(), "test genesis revision was rejected");
  return {project,
          rootRecord,
          actor,
          schema,
          {{*revision, std::move(envelope)},
           ProjectSnapshot{std::move(*state), *revision},
           {}}};
}

DurableRevision sourceRevision(const ProjectFixture &fixture,
                               const DurableRevision &base, std::uint64_t index,
                               testkit::Random &random) {
  const std::string source = index % 4U == 0U
                                 ? std::string{}
                                 : "def generated():\n    return " +
                                       std::to_string(random.next()) + "\n";
  Bytes bytes(source.begin(), source.end());
  auto contentId = contentDigest(bytes);
  require(contentId.has_value(), "test source could not be hashed");
  auto path =
      ProjectPath::parse("models/generated_" + std::to_string(index) + ".py");
  require(path.has_value(), "test source path was rejected");
  const ContentEntry content{*contentId, bytes.size(),
                             "text/x-python; charset=utf-8"};
  MutationBatch mutations{PutContent{*path, std::nullopt, content}};
  auto state =
      internal::ProjectStateAccess::apply(base.snapshot.state(), mutations);
  require(state.has_value(), "test source revision was rejected");
  RevisionEnvelope envelope{
      {base.revision.id},
      id<TransactionId>(index + 5'000U, random.next()),
      id<RequestId>(index + 6'000U, random.next()),
      digest<ContentDigest>(static_cast<std::uint8_t>(random.next())),
      fixture.actor,
      Origin::Human,
      id<PermissionContextId>(index + 7'000U, random.next()),
      std::nullopt,
      fixture.schema,
      index + 2U,
      state->rootDigest(),
      {"source.module.create"},
      mutations};
  auto revision = revisionId(envelope);
  require(revision.has_value(), "test source revision did not hash");
  return {{*revision, std::move(envelope)},
          ProjectSnapshot{std::move(*state), *revision},
          {{*contentId, std::move(bytes)}}};
}

SqliteProjectStoreOptions options() {
  return {"sqlite-property-test", 64U * 1024U * 1024U, 16U * 1024U * 1024U,
          5'000U};
}

void verifyProjectStore(const testkit::PropertyProfile &profile) {
  testkit::PropertyProfile storageProfile = profile;
  if (!storageProfile.replay)
    storageProfile.iterations = std::max(
        (profile.iterations + 9'999U) / 10'000U, profile.shardCount * 16U);
  testkit::checkProperty(
      "SQLite project recovery", storageProfile,
      [](testkit::Random &random, std::uint64_t index) {
        TemporaryProject temporary{index, random.next()};
        ProjectFixture fixture = projectFixture(index, random);
        auto created = SqliteProjectStore::create(
            temporary.path, fixture.rootRecord, fixture.genesis, options());
        if (!created)
          throw std::runtime_error(
              "project database could not be created: " + created.error().code +
              " " + created.error().detail);
        auto initial = (*created)->loadHead();
        require(initial &&
                    initial->headRevision.id == fixture.genesis.revision.id &&
                    initial->headSnapshot.state().rootDigest() ==
                        fixture.genesis.snapshot.state().rootDigest() &&
                    initial->savedRevision == fixture.genesis.revision.id,
                "created project did not recover its genesis state");

        DurableRevision source =
            sourceRevision(fixture, fixture.genesis, index, random);
        DurableRevision corruptSource = source;
        if (corruptSource.content.front().bytes.empty())
          corruptSource.content.front().bytes.push_back(0xffU);
        else
          corruptSource.content.front().bytes.front() ^= 0xffU;
        auto corruptCommit =
            (*created)->commit(fixture.genesis.revision.id, corruptSource);
        auto afterCorruptCommit = (*created)->loadHead();
        require(!corruptCommit &&
                    corruptCommit.error().code ==
                        "persistence.content.digest-mismatch" &&
                    afterCorruptCommit &&
                    afterCorruptCommit->headRevision.id ==
                        fixture.genesis.revision.id,
                "invalid source bytes changed the durable head");
        require(
            (*created)->commit(fixture.genesis.revision.id, source).has_value(),
            "source revision was not committed");
        require(
            (*created)->commit(fixture.genesis.revision.id, source).has_value(),
            "ambiguous commit retry was not idempotent");
        auto committed = (*created)->loadHead();
        require(committed && committed->headRevision.id == source.revision.id &&
                    committed->headSnapshot.state().rootDigest() ==
                        source.snapshot.state().rootDigest() &&
                    committed->savedRevision == fixture.genesis.revision.id,
                "committed project head did not recover");
        auto children = (*created)->children(fixture.genesis.revision.id);
        require(children && *children == std::vector{source.revision.id},
                "revision child index is invalid");
        auto stored = (*created)->content(source.content.front().digest);
        require(stored && **stored == source.content.front().bytes,
                "source bytes did not round trip");
        DurableRevision conflict =
            sourceRevision(fixture, fixture.genesis, index + 100'001U, random);
        auto conflicted =
            (*created)->commit(fixture.genesis.revision.id, conflict);
        auto absent = (*created)->content(conflict.content.front().digest);
        auto afterConflict = (*created)->loadHead();
        require(!conflicted &&
                    conflicted.error().code == "persistence.head.conflict" &&
                    !absent &&
                    absent.error().code == "persistence.content.not-found" &&
                    afterConflict &&
                    afterConflict->headRevision.id == source.revision.id,
                "conflicted commit leaked content or changed the head");
        require((*created)
                    ->moveHead(source.revision.id, fixture.genesis.snapshot)
                    .has_value(),
                "durable undo failed");
        auto undone = (*created)->loadHead();
        const auto redone =
            (*created)->moveHead(fixture.genesis.revision.id, source.snapshot);
        auto restored = (*created)->loadHead();
        require(undone &&
                    undone->headRevision.id == fixture.genesis.revision.id &&
                    redone && restored &&
                    restored->headRevision.id == source.revision.id,
                "durable undo or redo changed canonical state");
        auto concurrentReader = SqliteProjectStore::open(
            temporary.path, ProjectOpenMode::ReadOnly, options());
        require(concurrentReader.has_value(),
                "read-only project could not open beside its writer");
        auto concurrentHead = (*concurrentReader)->loadHead();
        require(concurrentHead &&
                    concurrentHead->headRevision.id == source.revision.id,
                "read-only open did not observe the committed WAL head");
        concurrentReader->reset();
        require((*created)->savePoint(source.revision.id).has_value(),
                "project save point failed");
        created->reset();

        auto reopened = SqliteProjectStore::open(
            temporary.path, ProjectOpenMode::ReadOnly, options());
        require(reopened.has_value(), "saved project did not reopen");
        auto recovered = (*reopened)->loadHead();
        require(recovered && recovered->headRevision.id == source.revision.id &&
                    recovered->savedRevision == source.revision.id &&
                    recovered->headSnapshot.state().rootDigest() ==
                        source.snapshot.state().rootDigest(),
                "reopened project changed canonical state");
        auto denied = (*reopened)->commit(source.revision.id, source);
        require(!denied && denied.error().code == "persistence.read-only",
                "read-only project accepted a write");
      });
}

void verifyCorruptionRejected(const testkit::PropertyProfile &profile) {
  testkit::PropertyProfile corruptionProfile = profile;
  if (!corruptionProfile.replay)
    corruptionProfile.iterations = std::max(
        (profile.iterations + 9'999U) / 10'000U, profile.shardCount * 16U);
  testkit::checkProperty(
      "SQLite project corruption rejection", corruptionProfile,
      [shardCount = profile.shardCount](testkit::Random &random,
                                        std::uint64_t index) {
        const std::uint64_t fixtureIndex = index + 200'000U;
        TemporaryProject temporary{fixtureIndex, random.next()};
        ProjectFixture fixture = projectFixture(fixtureIndex, random);
        auto created = SqliteProjectStore::create(
            temporary.path, fixture.rootRecord, fixture.genesis, options());
        require(created.has_value(), "corruption fixture could not be created");
        DurableRevision source = sourceRevision(fixture, fixture.genesis,
                                                fixtureIndex * 4U + 1U, random);
        require((*created)
                        ->commit(fixture.genesis.revision.id, source)
                        .has_value() &&
                    (*created)->savePoint(source.revision.id).has_value(),
                "corruption fixture could not save");
        created->reset();

        sqlite3 *database = nullptr;
        const std::u8string encodedPath = temporary.path.u8string();
        const std::string path{
            reinterpret_cast<const char *>(encodedPath.data()),
            encodedPath.size()};
        require(sqlite3_open_v2(path.c_str(), &database, SQLITE_OPEN_READWRITE,
                                nullptr) == SQLITE_OK,
                "corruption fixture could not open raw database");
        const char *corruption =
            (index / shardCount) % 2U == 0U
                ? "UPDATE project_state SET "
                  "checkpoint=zeroblob(length(checkpoint))"
                : "UPDATE content SET bytes=zeroblob(length(bytes))";
        const int changed =
            sqlite3_exec(database, corruption, nullptr, nullptr, nullptr);
        sqlite3_close_v2(database);
        require(changed == SQLITE_OK,
                "corruption fixture could not alter project bytes");
        auto rejected = SqliteProjectStore::open(
            temporary.path, ProjectOpenMode::ReadOnly, options());
        require(!rejected, "corrupt project bytes were published");
      });
}

} // namespace

int main() {
  try {
    const auto profile = kearne::testkit::propertyProfile();
    verifyProjectStore(profile);
    verifyCorruptionRejected(profile);
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
