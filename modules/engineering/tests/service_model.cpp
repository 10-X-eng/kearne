#include <kearne/api/strong_types.hpp>
#include <kearne/engineering/service.hpp>
#include <kearne/testkit/property.hpp>

#include <algorithm>
#include <array>
#include <barrier>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace kearne;
using namespace kearne::engineering;
namespace wire = kearne::api::v1;

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

template <typename Value>
Value id(std::uint64_t timestamp, std::uint64_t randomValue) {
  typename Value::RandomTail tail{};
  for (std::size_t index = 0; index < tail.size(); ++index)
    tail[index] = static_cast<std::uint8_t>(randomValue >> ((index % 8) * 8));
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

class TestPermissions final : public PermissionPolicy {
public:
  explicit TestPermissions(PermissionContextId denied) : denied_(denied) {}

  Result<void> authorize(const PermissionRequest &request) const override {
    if (request.context == denied_)
      return std::unexpected(
          diagnostic("permission.denied", "permission context is denied"));
    if (request.permission != "project.edit" &&
        request.permission != "project.read")
      return std::unexpected(
          diagnostic("permission.unknown", "permission is not registered"));
    return {};
  }

private:
  PermissionContextId denied_;
};

class TamperedContentStore final : public document::ContentStore {
public:
  explicit TamperedContentStore(document::Bytes bytes)
      : bytes_(std::make_shared<const document::Bytes>(std::move(bytes))) {}

  Result<void> put(ContentDigest, document::Bytes) override { return {}; }
  Result<std::shared_ptr<const document::Bytes>>
  get(const ContentDigest &) const override {
    return bytes_;
  }
  document::ContentStoreLimits limits() const override {
    return {16U * 1024U * 1024U, 64U * 1024U * 1024U};
  }

private:
  std::shared_ptr<const document::Bytes> bytes_;
};

class CoordinatedContentStore final : public document::ContentStore {
public:
  CoordinatedContentStore()
      : content_({16U * 1024U * 1024U, 64U * 1024U * 1024U}) {}

  Result<void> put(ContentDigest digest, document::Bytes bytes) override {
    return content_.put(std::move(digest), std::move(bytes));
  }

  Result<std::shared_ptr<const document::Bytes>>
  get(const ContentDigest &digest) const override {
    rendezvous_.arrive_and_wait();
    return content_.get(digest);
  }
  document::ContentStoreLimits limits() const override {
    return content_.limits();
  }

private:
  document::InMemoryContentStore content_;
  mutable std::barrier<> rendezvous_{2};
};

struct StoredSource {
  ContentDigest digest;
  std::uint64_t byteSize;
};

struct Fixture {
  ProjectId project = id<ProjectId>(1, 1);
  RecordId rootRecord = id<RecordId>(1, 2);
  ActorId actor = id<ActorId>(1, 3);
  PermissionContextId allowed = id<PermissionContextId>(1, 4);
  PermissionContextId denied = id<PermissionContextId>(1, 5);
  std::shared_ptr<TestPermissions> permissions =
      std::make_shared<TestPermissions>(denied);
  std::shared_ptr<document::InMemoryContentStore> contentStore =
      std::make_shared<document::InMemoryContentStore>(
          document::ContentStoreLimits{16U * 1024U * 1024U,
                                       64U * 1024U * 1024U});

  std::unique_ptr<InMemoryEngineeringService>
  service(std::shared_ptr<const document::ContentStore> store = {}) const {
    auto result = InMemoryEngineeringService::create(
        {project, rootRecord, id<TransactionId>(1, 6), actor,
         digest<SchemaSetDigest>(7), 1, "Kearne Project"},
        permissions, store ? std::move(store) : contentStore);
    require(result.has_value(), "engineering service could not initialize");
    return std::move(*result);
  }

  StoredSource store(std::string_view source) const {
    document::Bytes bytes(source.begin(), source.end());
    auto contentId = document::contentDigest(bytes);
    require(contentId.has_value(), "source content could not be hashed");
    require(contentStore->put(*contentId, std::move(bytes)).has_value(),
            "source content could not be staged");
    return {*contentId, static_cast<std::uint64_t>(source.size())};
  }

  static void writeSource(const StoredSource &source,
                          wire::ContentReference &reference) {
    api::writeDigest(source.digest, reference.mutable_digest());
    reference.set_byte_size(source.byteSize);
    reference.set_media_type("text/x-python; charset=utf-8");
  }

  wire::CommandEnvelope rename(RequestId request, RevisionId base,
                               PermissionContextId permission,
                               std::string name) const {
    wire::CommandEnvelope command;
    api::writeId(request, command.mutable_request_id());
    api::writeDigest(base, command.mutable_base_revision());
    api::writeId(actor, command.mutable_actor_id());
    command.set_origin(wire::ORIGIN_HUMAN);
    api::writeId(permission, command.mutable_permission_context_id());
    auto *rename = command.mutable_operation()->mutable_rename_project();
    api::writeId(project, rename->mutable_project_id());
    rename->set_display_name(std::move(name));
    return command;
  }

  wire::CommandEnvelope createSource(RequestId request, RevisionId base,
                                     PermissionContextId permission,
                                     std::string path,
                                     const StoredSource &source) const {
    wire::CommandEnvelope command;
    api::writeId(request, command.mutable_request_id());
    api::writeDigest(base, command.mutable_base_revision());
    api::writeId(actor, command.mutable_actor_id());
    command.set_origin(wire::ORIGIN_HUMAN);
    api::writeId(permission, command.mutable_permission_context_id());
    auto *create = command.mutable_operation()->mutable_create_source_module();
    api::writeId(project, create->mutable_project_id());
    create->set_path(std::move(path));
    writeSource(source, *create->mutable_content());
    return command;
  }

  wire::CommandEnvelope replaceSource(RequestId request, RevisionId base,
                                      PermissionContextId permission,
                                      std::string path, ContentDigest expected,
                                      const StoredSource &source) const {
    wire::CommandEnvelope command;
    api::writeId(request, command.mutable_request_id());
    api::writeDigest(base, command.mutable_base_revision());
    api::writeId(actor, command.mutable_actor_id());
    command.set_origin(wire::ORIGIN_HUMAN);
    api::writeId(permission, command.mutable_permission_context_id());
    auto *replace =
        command.mutable_operation()->mutable_replace_source_module();
    api::writeId(project, replace->mutable_project_id());
    replace->set_path(std::move(path));
    api::writeDigest(expected, replace->mutable_expected_prior());
    writeSource(source, *replace->mutable_content());
    return command;
  }

  wire::TransactionEnvelope
  transaction(RequestId request, RevisionId base,
              PermissionContextId permission,
              std::span<const wire::CommandEnvelope> commands) const {
    wire::TransactionEnvelope transaction;
    api::writeId(request, transaction.mutable_request_id());
    api::writeDigest(base, transaction.mutable_base_revision());
    api::writeId(actor, transaction.mutable_actor_id());
    transaction.set_origin(wire::ORIGIN_HUMAN);
    api::writeId(permission, transaction.mutable_permission_context_id());
    for (const wire::CommandEnvelope &command : commands)
      *transaction.add_operations() = command.operation();
    return transaction;
  }

  wire::QueryEnvelope metadata(RevisionId revision,
                               PermissionContextId permission) const {
    wire::QueryEnvelope query;
    api::writeDigest(revision, query.mutable_revision());
    query.set_limit(1);
    api::writeId(actor, query.mutable_actor_id());
    query.set_origin(wire::ORIGIN_HUMAN);
    api::writeId(permission, query.mutable_permission_context_id());
    api::writeId(project,
                 query.mutable_get_project_metadata()->mutable_project_id());
    return query;
  }

  wire::QueryEnvelope sourceMetadata(RevisionId revision,
                                     PermissionContextId permission,
                                     std::string path) const {
    wire::QueryEnvelope query;
    api::writeDigest(revision, query.mutable_revision());
    query.set_limit(1);
    api::writeId(actor, query.mutable_actor_id());
    query.set_origin(wire::ORIGIN_HUMAN);
    api::writeId(permission, query.mutable_permission_context_id());
    auto *metadata = query.mutable_get_source_module_metadata();
    api::writeId(project, metadata->mutable_project_id());
    metadata->set_path(std::move(path));
    return query;
  }
};

RevisionId committedRevision(const wire::CommandResult &result) {
  require(result.committed() && result.diagnostics().empty(),
          "command did not commit cleanly");
  auto revision = api::readDigest<RevisionId>(result.revision());
  require(revision.has_value(), "command returned an invalid revision");
  return std::move(*revision);
}

void requireDiagnostic(const wire::CommandResult &result,
                       std::string_view code) {
  require(!result.committed() && result.diagnostics_size() == 1 &&
              result.diagnostics(0).code() == code,
          "command returned the wrong diagnostic");
}

void requireDiagnostic(const wire::QueryResult &result, std::string_view code) {
  require(!result.has_project_metadata() &&
              !result.has_source_module_metadata() &&
              result.diagnostics_size() == 1 &&
              result.diagnostics(0).code() == code,
          "query returned the wrong diagnostic");
}

void verifyCriticalWorkflow() {
  Fixture fixture;
  auto service = fixture.service();
  require(InMemoryEngineeringService::registeredCommandCount() == 3 &&
              InMemoryEngineeringService::registeredQueryCount() == 2,
          "schema registry coverage is incomplete");
  const RevisionId genesis = service->headSnapshot()->revisionId();
  const RequestId request = id<RequestId>(2, 1);
  const wire::CommandEnvelope command =
      fixture.rename(request, genesis, fixture.allowed, "Mounting Plate");
  auto committed = service->submit(command, {id<TransactionId>(2, 2), 2});
  require(committed.has_value(), "valid command failed internally");
  const RevisionId renamed = committedRevision(*committed);
  require(service->revisionCount() == 2,
          "committed command did not create one revision");

  auto retried = service->submit(command, {id<TransactionId>(3, 3), 3});
  require(retried && committedRevision(*retried) == renamed &&
              service->revisionCount() == 2,
          "idempotent retry changed history");
  auto reused = command;
  reused.mutable_operation()->mutable_rename_project()->set_display_name(
      "Different");
  auto reuseResult = service->submit(reused, {id<TransactionId>(4, 4), 4});
  require(reuseResult.has_value(), "request reuse failed internally");
  requireDiagnostic(*reuseResult, "engineering.request.identity-reuse");
  const std::array transactionCommands{command};
  auto crossTypeReuse =
      service->submit(fixture.transaction(request, renamed, fixture.allowed,
                                          transactionCommands),
                      {id<TransactionId>(4, 40), 4});
  require(crossTypeReuse.has_value(),
          "cross-envelope request reuse failed internally");
  requireDiagnostic(*crossTypeReuse, "engineering.request.identity-reuse");
  auto transactionReuse = fixture.rename(id<RequestId>(4, 5), renamed,
                                         fixture.allowed, "Transaction Reuse");
  auto transactionReuseResult =
      service->submit(transactionReuse, {id<TransactionId>(2, 2), 4});
  require(transactionReuseResult.has_value(),
          "transaction reuse failed internally");
  requireDiagnostic(*transactionReuseResult,
                    "engineering.transaction.identity-reuse");

  auto stale = fixture.rename(id<RequestId>(5, 5), genesis, fixture.allowed,
                              "Stale Change");
  auto staleResult = service->submit(stale, {id<TransactionId>(5, 6), 5});
  require(staleResult.has_value(), "stale command failed internally");
  requireDiagnostic(*staleResult, "engineering.revision.conflict");
  auto denied = fixture.rename(id<RequestId>(6, 6), renamed, fixture.denied,
                               "Denied Change");
  auto deniedResult = service->submit(denied, {id<TransactionId>(6, 7), 6});
  require(deniedResult.has_value(), "denied command failed internally");
  requireDiagnostic(*deniedResult, "permission.denied");

  auto current = service->query(fixture.metadata(renamed, fixture.allowed));
  require(current && current->has_project_metadata() &&
              current->project_metadata().display_name() == "Mounting Plate",
          "current revision query returned wrong metadata");
  auto original = service->query(fixture.metadata(genesis, fixture.allowed));
  require(original && original->has_project_metadata() &&
              original->project_metadata().display_name() == "Kearne Project",
          "historical query did not observe its requested revision");

  require(service->undo().has_value() &&
              service->headSnapshot()->revisionId() == genesis,
          "undo did not move the workspace head");
  require(service->redoChoices() == std::vector{renamed},
          "redo choice was not retained");
  require(service->redo(renamed).has_value(), "redo failed");
  require(service->undo().has_value(), "second undo failed");
  auto branchCommand = fixture.rename(id<RequestId>(7, 7), genesis,
                                      fixture.allowed, "Alternate Plate");
  auto branch = service->submit(branchCommand, {id<TransactionId>(7, 8), 7});
  require(branch.has_value(), "branch command failed internally");
  const RevisionId branchRevision = committedRevision(*branch);
  const auto noChildren = service->redoChoices();
  require(noChildren.empty() && branchRevision != renamed,
          "editing after undo did not create a divergent revision");
  require(service->undo().has_value() && service->redoChoices().size() == 2,
          "divergent redo choices were lost");
}

void verifySourceWorkflow() {
  Fixture fixture;
  auto service = fixture.service();
  const RevisionId genesis = service->headSnapshot()->revisionId();
  const std::string firstSource = "from build123d import Box\n\n"
                                  "def mounting_plate(width: float):\n"
                                  "    return Box(width, 0.04, 0.006)\n";
  const StoredSource first = fixture.store(firstSource);
  const RequestId createRequest = id<RequestId>(20, 1);
  const auto create =
      fixture.createSource(createRequest, genesis, fixture.allowed,
                           "models/mounting_plate.py", first);
  require(create.ByteSizeLong() < 4096 &&
              create.SerializeAsString().find(firstSource) == std::string::npos,
          "source bytes leaked into the command envelope");
  auto created = service->submit(create, {id<TransactionId>(20, 2), 20});
  require(created.has_value(), "source creation failed internally");
  const RevisionId sourceRevision = committedRevision(*created);

  auto retried = service->submit(create, {id<TransactionId>(20, 3), 21});
  require(retried && committedRevision(*retried) == sourceRevision &&
              service->revisionCount() == 2,
          "source creation retry changed history");
  auto metadata = service->query(fixture.sourceMetadata(
      sourceRevision, fixture.allowed, "models/mounting_plate.py"));
  require(metadata && metadata->has_source_module_metadata(),
          "source metadata query failed");
  const auto &createdMetadata = metadata->source_module_metadata();
  auto observed =
      api::readDigest<RevisionId>(createdMetadata.observed_revision());
  auto returnedDigest =
      api::readDigest<ContentDigest>(createdMetadata.content().digest());
  require(observed && *observed == sourceRevision && returnedDigest &&
              *returnedDigest == first.digest &&
              createdMetadata.content().byte_size() == first.byteSize &&
              createdMetadata.path() == "models/mounting_plate.py",
          "source metadata does not describe its requested revision");
  auto absent = service->query(fixture.sourceMetadata(
      genesis, fixture.allowed, "models/mounting_plate.py"));
  require(absent.has_value(), "missing historical source failed internally");
  requireDiagnostic(*absent, "source.module.not-found");

  const StoredSource missing{digest<ContentDigest>(90), 7};
  auto missingCommand =
      fixture.createSource(id<RequestId>(21, 1), sourceRevision,
                           fixture.allowed, "models/missing.py", missing);
  auto missingResult =
      service->submit(missingCommand, {id<TransactionId>(21, 2), 21});
  require(missingResult.has_value(), "missing content failed internally");
  requireDiagnostic(*missingResult, "source.module.content-missing");

  auto wrongSize =
      fixture.createSource(id<RequestId>(22, 1), sourceRevision,
                           fixture.allowed, "models/wrong_size.py", first);
  wrongSize.mutable_operation()
      ->mutable_create_source_module()
      ->mutable_content()
      ->set_byte_size(first.byteSize + 1);
  auto wrongSizeResult =
      service->submit(wrongSize, {id<TransactionId>(22, 2), 22});
  require(wrongSizeResult.has_value(), "wrong content size failed internally");
  requireDiagnostic(*wrongSizeResult, "source.module.content-size-mismatch");

  auto wrongMedia =
      fixture.createSource(id<RequestId>(23, 1), sourceRevision,
                           fixture.allowed, "models/wrong_media.py", first);
  wrongMedia.mutable_operation()
      ->mutable_create_source_module()
      ->mutable_content()
      ->set_media_type("application/python");
  auto wrongMediaResult =
      service->submit(wrongMedia, {id<TransactionId>(23, 2), 23});
  require(wrongMediaResult.has_value(), "wrong media type failed internally");
  requireDiagnostic(*wrongMediaResult,
                    "source.module.invalid-content-reference");

  auto invalidPath =
      fixture.createSource(id<RequestId>(24, 1), sourceRevision,
                           fixture.allowed, "../escape.py", first);
  auto invalidPathResult =
      service->submit(invalidPath, {id<TransactionId>(24, 2), 24});
  require(invalidPathResult.has_value(), "invalid path failed internally");
  requireDiagnostic(*invalidPathResult, "document.path.invalid-segment");

  document::Bytes invalidBytes{0xffU};
  auto invalidDigest = document::contentDigest(invalidBytes);
  require(invalidDigest &&
              fixture.contentStore->put(*invalidDigest, invalidBytes),
          "invalid UTF-8 content could not be staged");
  const StoredSource invalidUtf8{*invalidDigest, 1};
  auto invalidUtf8Command = fixture.createSource(
      id<RequestId>(25, 1), sourceRevision, fixture.allowed,
      "models/invalid_utf8.py", invalidUtf8);
  auto invalidUtf8Result =
      service->submit(invalidUtf8Command, {id<TransactionId>(25, 2), 25});
  require(invalidUtf8Result.has_value(), "invalid UTF-8 failed internally");
  requireDiagnostic(*invalidUtf8Result, "source.module.invalid-utf8");

  auto denied =
      fixture.createSource(id<RequestId>(26, 1), sourceRevision, fixture.denied,
                           "models/denied.py", missing);
  auto deniedResult = service->submit(denied, {id<TransactionId>(26, 2), 26});
  require(deniedResult.has_value(), "denied source command failed internally");
  requireDiagnostic(*deniedResult, "permission.denied");

  const std::string replacementSource = "from build123d import Box\n\n"
                                        "def mounting_plate(width: float):\n"
                                        "    return Box(width, 0.05, 0.008)\n";
  const StoredSource replacement = fixture.store(replacementSource);
  auto replace = fixture.replaceSource(
      id<RequestId>(27, 1), sourceRevision, fixture.allowed,
      "models/mounting_plate.py", first.digest, replacement);
  auto replaced = service->submit(replace, {id<TransactionId>(27, 2), 27});
  require(replaced.has_value(), "source replacement failed internally");
  const RevisionId replacementRevision = committedRevision(*replaced);

  auto staleDigest = fixture.replaceSource(
      id<RequestId>(28, 1), replacementRevision, fixture.allowed,
      "models/mounting_plate.py", first.digest, replacement);
  auto staleDigestResult =
      service->submit(staleDigest, {id<TransactionId>(28, 2), 28});
  require(staleDigestResult.has_value(),
          "stale source digest failed internally");
  requireDiagnostic(*staleDigestResult, "document.mutation.stale");
  auto staleHead = fixture.replaceSource(
      id<RequestId>(29, 1), sourceRevision, fixture.allowed,
      "models/mounting_plate.py", first.digest, replacement);
  auto staleHeadResult =
      service->submit(staleHead, {id<TransactionId>(29, 2), 29});
  require(staleHeadResult.has_value(), "stale source head failed internally");
  requireDiagnostic(*staleHeadResult, "engineering.revision.conflict");

  auto previous = service->query(fixture.sourceMetadata(
      sourceRevision, fixture.allowed, "models/mounting_plate.py"));
  auto current = service->query(fixture.sourceMetadata(
      replacementRevision, fixture.allowed, "models/mounting_plate.py"));
  require(previous && current && previous->has_source_module_metadata() &&
              current->has_source_module_metadata() &&
              api::readDigest<ContentDigest>(
                  previous->source_module_metadata().content().digest()) ==
                  first.digest &&
              api::readDigest<ContentDigest>(
                  current->source_module_metadata().content().digest()) ==
                  replacement.digest,
          "source replacement changed an immutable ancestor");

  const std::string honest = "value = 1\n";
  const StoredSource honestReference = fixture.store(honest);
  auto tamperedStore = std::make_shared<TamperedContentStore>(
      document::Bytes{'v', 'a', 'l', 'u', 'e', ' ', '=', ' ', '2', '\n'});
  auto tamperedService = fixture.service(tamperedStore);
  auto tamperedCommand = fixture.createSource(
      id<RequestId>(30, 1), tamperedService->headSnapshot()->revisionId(),
      fixture.allowed, "models/tampered.py", honestReference);
  auto tampered =
      tamperedService->submit(tamperedCommand, {id<TransactionId>(30, 2), 30});
  require(tampered.has_value(), "tampered content failed internally");
  requireDiagnostic(*tampered, "source.module.content-digest-mismatch");

  auto missingStore = InMemoryEngineeringService::create(
      {fixture.project, fixture.rootRecord, id<TransactionId>(31, 1),
       fixture.actor, digest<SchemaSetDigest>(7), 31, "Kearne Project"},
      fixture.permissions, {});
  require(!missingStore &&
              missingStore.error().code == "engineering.content.missing-store",
          "engineering service accepted an ambient content store");
}

void verifyGeneratedStateMachine(const testkit::PropertyProfile &profile) {
  Fixture fixture;
  auto service = fixture.service();
  const RevisionId genesis = service->headSnapshot()->revisionId();
  RevisionId head = genesis;
  std::map<RevisionId, std::optional<RevisionId>> parents;
  std::map<RevisionId, std::vector<RevisionId>> children;
  std::map<RevisionId, std::string> names;
  parents.emplace(genesis, std::nullopt);
  names.emplace(genesis, "Kearne Project");
  std::vector<RevisionId> revisions{genesis};
  std::optional<wire::CommandEnvelope> lastCommand;
  std::optional<RevisionId> lastCommandRevision;

  testkit::checkProperty(
      "engineering revision state machine", profile,
      [&](testkit::Random &random, std::uint64_t index) {
        const std::uint64_t action = random.next() % 10;
        if (action <= 5) {
          const std::string name = "Generated " + std::to_string(random.next());
          const auto command =
              fixture.rename(id<RequestId>(index + 10, random.next()), head,
                             fixture.allowed, name);
          auto result = service->submit(
              command,
              {id<TransactionId>(index + 10, random.next()), index + 10});
          require(result.has_value(), "generated command failed internally");
          const RevisionId next = committedRevision(*result);
          parents.emplace(next, head);
          children[head].push_back(next);
          names.emplace(next, name);
          revisions.push_back(next);
          head = next;
          lastCommand = command;
          lastCommandRevision = next;
        } else if (action == 6) {
          const RevisionId revision = revisions[static_cast<std::size_t>(
              random.next() % revisions.size())];
          auto result =
              service->query(fixture.metadata(revision, fixture.allowed));
          require(result && result->has_project_metadata() &&
                      result->project_metadata().display_name() ==
                          names.at(revision),
                  "generated historical query disagrees with reference model");
        } else if (action == 7) {
          const auto parent = parents.at(head);
          auto result = service->undo();
          require(result.has_value() == parent.has_value(),
                  "undo availability disagrees with reference model");
          if (parent)
            head = *parent;
        } else if (action == 8) {
          auto expected = children[head];
          std::ranges::sort(expected);
          const auto actual = service->redoChoices();
          require(actual == expected,
                  "redo choices disagree with reference model");
          if (!expected.empty()) {
            const RevisionId choice = expected[static_cast<std::size_t>(
                random.next() % expected.size())];
            require(service->redo(choice).has_value(),
                    "valid redo was rejected");
            head = choice;
          }
        } else if (revisions.size() > 1) {
          RevisionId staleBase = revisions.front();
          if (staleBase == head)
            staleBase = revisions.back();
          auto command =
              fixture.rename(id<RequestId>(index + 10, random.next()),
                             staleBase, fixture.allowed, "Stale");
          auto result = service->submit(
              command,
              {id<TransactionId>(index + 10, random.next()), index + 10});
          require(result.has_value(),
                  "stale generated command failed internally");
          requireDiagnostic(*result, "engineering.revision.conflict");
        }

        if (index % 113 == 0 && lastCommand && lastCommandRevision) {
          const std::size_t before = service->revisionCount();
          auto retry = service->submit(
              *lastCommand,
              {id<TransactionId>(index + 20, random.next()), index + 20});
          require(retry && committedRevision(*retry) == *lastCommandRevision &&
                      service->revisionCount() == before,
                  "generated idempotent retry changed history");
        }
        require(
            service->headSnapshot()->revisionId() == head &&
                service->revisionCount() == revisions.size(),
            "service head or revision count disagrees with reference model");
      });
}

void verifyGeneratedSourceHistory(const testkit::PropertyProfile &profile) {
  Fixture fixture;
  auto service = fixture.service();
  const std::string path = "models/generated.py";
  StoredSource current = fixture.store("value = 0\n");
  auto initial = service->submit(
      fixture.createSource(id<RequestId>(100, 1),
                           service->headSnapshot()->revisionId(),
                           fixture.allowed, path, current),
      {id<TransactionId>(100, 2), 100});
  require(initial.has_value(), "generated source could not initialize");
  RevisionId head = committedRevision(*initial);
  std::vector<RevisionId> revisions{head};
  std::vector<ContentDigest> digests{current.digest};

  testkit::checkProperty(
      "source module revision history", profile,
      [&](testkit::Random &random, std::uint64_t index) {
        const StoredSource first =
            fixture.store("value = " + std::to_string(random.next()) + "\n");
        Result<wire::CommandResult> result;
        StoredSource next = first;
        if (index % 97 == 0) {
          next =
              fixture.store("value = " + std::to_string(random.next()) + "\n");
          const std::array commands{
              fixture.replaceSource(id<RequestId>(index + 200, 1), head,
                                    fixture.allowed, path, current.digest,
                                    first),
              fixture.replaceSource(id<RequestId>(index + 200, 2), head,
                                    fixture.allowed, path, first.digest, next)};
          result = service->submit(
              fixture.transaction(id<RequestId>(index + 200, random.next()),
                                  head, fixture.allowed, commands),
              {id<TransactionId>(index + 200, random.next()), index + 200});
        } else {
          auto command = fixture.replaceSource(
              id<RequestId>(index + 200, random.next()), head, fixture.allowed,
              path, current.digest, next);
          result = service->submit(
              command,
              {id<TransactionId>(index + 200, random.next()), index + 200});
        }
        require(result.has_value(), "generated source edit failed internally");
        head = committedRevision(*result);
        current = next;
        revisions.push_back(head);
        digests.push_back(current.digest);

        const std::size_t observedIndex = static_cast<std::size_t>(
            random.next() % static_cast<std::uint64_t>(revisions.size()));
        auto observed = service->query(fixture.sourceMetadata(
            revisions[observedIndex], fixture.allowed, path));
        require(
            observed && observed->has_source_module_metadata() &&
                api::readDigest<ContentDigest>(
                    observed->source_module_metadata().content().digest()) ==
                    digests[observedIndex],
            "generated source query changed historical content");

        if (index % 79 == 0) {
          ContentDigest::Bytes staleBytes = current.digest.bytes();
          staleBytes[0] ^= 0xffU;
          auto stalePrior = ContentDigest::fromBytes("blake3", staleBytes);
          require(stalePrior.has_value(),
                  "generated stale digest could not be created");
          const StoredSource staged =
              fixture.store("value = " + std::to_string(random.next()) + "\n");
          const StoredSource unreachable =
              fixture.store("value = " + std::to_string(random.next()) + "\n");
          const std::array commands{
              fixture.replaceSource(id<RequestId>(index + 10000, 1), head,
                                    fixture.allowed, path, current.digest,
                                    staged),
              fixture.replaceSource(id<RequestId>(index + 10000, 2), head,
                                    fixture.allowed, path, *stalePrior,
                                    unreachable)};
          const std::size_t revisionCount = service->revisionCount();
          auto rejected = service->submit(
              fixture.transaction(id<RequestId>(index + 10000, random.next()),
                                  head, fixture.allowed, commands),
              {id<TransactionId>(index + 10000, random.next()), index + 10000});
          require(rejected.has_value(),
                  "generated stale source edit failed internally");
          requireDiagnostic(*rejected, "document.mutation.stale");
          auto unchanged = service->query(
              fixture.sourceMetadata(head, fixture.allowed, path));
          require(
              unchanged && unchanged->has_source_module_metadata() &&
                  api::readDigest<ContentDigest>(
                      unchanged->source_module_metadata().content().digest()) ==
                      current.digest &&
                  service->revisionCount() == revisionCount,
              "failed generated transaction leaked staged state");
        }
      });
}

StoredSource stage(document::ContentStore &store, std::string_view source) {
  document::Bytes bytes(source.begin(), source.end());
  auto contentId = document::contentDigest(bytes);
  require(contentId.has_value(), "concurrent source could not be hashed");
  require(store.put(*contentId, std::move(bytes)).has_value(),
          "concurrent source could not be staged");
  return {*contentId, static_cast<std::uint64_t>(source.size())};
}

void verifyConcurrentPreparation() {
  {
    Fixture fixture;
    auto content = std::make_shared<CoordinatedContentStore>();
    const StoredSource source = stage(*content, "value = 1\n");
    auto service = fixture.service(content);
    const RevisionId base = service->headSnapshot()->revisionId();
    const std::array commands{
        fixture.createSource(id<RequestId>(2000, 1), base, fixture.allowed,
                             "models/left.py", source),
        fixture.createSource(id<RequestId>(2000, 2), base, fixture.allowed,
                             "models/right.py", source)};
    std::array<Result<wire::CommandResult>, 2> outcomes;
    std::vector<std::jthread> writers;
    for (std::size_t index = 0; index < outcomes.size(); ++index)
      writers.emplace_back([&, index] {
        outcomes[index] = service->submit(
            commands[index], {id<TransactionId>(2000, index + 10), 2000});
      });
    writers.clear();
    std::size_t committed = 0;
    std::size_t conflicted = 0;
    for (const auto &outcome : outcomes) {
      require(outcome.has_value(), "concurrent source edit failed internally");
      committed += outcome->committed();
      conflicted +=
          !outcome->committed() && outcome->diagnostics_size() == 1 &&
          outcome->diagnostics(0).code() == "engineering.revision.conflict";
    }
    require(committed == 1 && conflicted == 1 && service->revisionCount() == 2,
            "concurrent base edits were not atomically published");
  }

  {
    Fixture fixture;
    auto content = std::make_shared<CoordinatedContentStore>();
    const StoredSource source = stage(*content, "value = 2\n");
    auto service = fixture.service(content);
    const auto command = fixture.createSource(
        id<RequestId>(2100, 1), service->headSnapshot()->revisionId(),
        fixture.allowed, "models/idempotent.py", source);
    std::array<Result<wire::CommandResult>, 2> outcomes;
    std::vector<std::jthread> writers;
    for (std::size_t index = 0; index < outcomes.size(); ++index)
      writers.emplace_back([&, index] {
        outcomes[index] = service->submit(
            command, {id<TransactionId>(2100, index + 10), 2100});
      });
    writers.clear();
    require(outcomes[0] && outcomes[1] && outcomes[0]->committed() &&
                outcomes[1]->committed() &&
                outcomes[0]->revision().SerializeAsString() ==
                    outcomes[1]->revision().SerializeAsString() &&
                service->revisionCount() == 2,
            "concurrent idempotent delivery published twice");
  }
}

} // namespace

int main() {
  try {
    verifyCriticalWorkflow();
    verifySourceWorkflow();
    verifyGeneratedStateMachine(kearne::testkit::propertyProfile());
    verifyGeneratedSourceHistory(kearne::testkit::propertyProfile());
    verifyConcurrentPreparation();
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
