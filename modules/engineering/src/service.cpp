#include <kearne/engineering/service.hpp>

#include <kearne/engineering/command_intent.hpp>

#include <kearne/api/strong_types.hpp>
#include <kearne/api/v1/options.pb.h>
#include <kearne/api/wire_validation.hpp>
#include <kearne/document/canonical.hpp>
#include <kearne/document/project_state_access.hpp>

#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>

#include <algorithm>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>

namespace kearne::engineering {
namespace {

namespace protobuf = google::protobuf;
namespace wire = api::v1;
using document::EngineeringRecord;
using document::MutationBatch;
using document::ProjectSnapshot;
using document::ProjectState;

template <typename> inline constexpr bool unsupportedIntent = false;

constexpr std::string_view projectRootKind = "kearne.project.root";
constexpr std::string_view pythonMediaType = "text/x-python; charset=utf-8";
constexpr std::uint64_t maxSourceModuleBytes = 16U * 1024U * 1024U;

wire::CommandResult commandFailure(Diagnostic value) {
  wire::CommandResult result;
  api::writeDiagnostic(value, result.add_diagnostics());
  return result;
}

wire::QueryResult queryFailure(Diagnostic value) {
  wire::QueryResult result;
  api::writeDiagnostic(value, result.add_diagnostics());
  return result;
}

Result<EngineeringRecord> projectRoot(const ProjectState &state, RecordId id) {
  auto result = state.record(id);
  if (!result || result->value.kind != projectRootKind)
    return std::unexpected(diagnostic("engineering.project.missing-root",
                                      "project root is missing"));
  return std::move(*result);
}

Result<void> validateDisplayName(std::string_view value) {
  if (value.empty() || value.size() > 80 || !document::isValidUtf8(value))
    return std::unexpected(
        diagnostic("project.name.invalid", "project display name is invalid"));
  return {};
}

Result<void> requireProject(const ProjectState &state,
                            const ProjectId &project) {
  if (project != state.projectId())
    return std::unexpected(
        diagnostic("project.id.mismatch", "request targets another project"));
  return {};
}

Result<void> requireProject(const ProjectState &state,
                            const wire::UuidV7 &value) {
  auto project = api::readId<ProjectId>(value);
  if (!project)
    return std::unexpected(std::move(project.error()));
  return requireProject(state, *project);
}

Result<void> validateSourceContent(const document::ContentEntry &reference,
                                   const document::ContentStore &store) {
  if (reference.byteSize > maxSourceModuleBytes ||
      reference.mediaType != pythonMediaType)
    return std::unexpected(diagnostic("source.module.invalid-content-reference",
                                      "source content metadata is invalid"));
  auto bytes = store.get(reference.digest);
  if (!bytes && bytes.error().code == "document.content.not-found")
    return std::unexpected(diagnostic("source.module.content-missing",
                                      "source content is not available"));
  if (!bytes)
    return std::unexpected(std::move(bytes.error()));
  if (!*bytes || (*bytes)->size() != reference.byteSize)
    return std::unexpected(diagnostic("source.module.content-size-mismatch",
                                      "source content size does not match"));
  auto actual = document::contentDigest(**bytes);
  if (!actual)
    return std::unexpected(std::move(actual.error()));
  if (*actual != reference.digest)
    return std::unexpected(diagnostic("source.module.content-digest-mismatch",
                                      "source content digest does not match",
                                      Severity::Fatal));
  const char *sourceData =
      (*bytes)->empty() ? "" : reinterpret_cast<const char *>((*bytes)->data());
  const std::string_view source{sourceData, (*bytes)->size()};
  if (!document::isValidUtf8(source))
    return std::unexpected(diagnostic("source.module.invalid-utf8",
                                      "source content is not UTF-8"));
  return {};
}

void writeContentReference(const document::ContentEntry &entry,
                           wire::ContentReference &reference) {
  api::writeDigest(entry.digest, reference.mutable_digest());
  reference.set_byte_size(entry.byteSize);
  reference.set_media_type(entry.mediaType);
}

Result<MutationBatch> normalizeRename(const ProjectState &state,
                                      RecordId projectRootRecord,
                                      internal::RenameProjectIntent command) {
  if (command.project != state.projectId())
    return std::unexpected(
        diagnostic("project.id.mismatch", "command targets another project"));
  auto root = projectRoot(state, projectRootRecord);
  if (!root)
    return std::unexpected(std::move(root.error()));
  auto expected = document::digestOf(*root);
  if (!expected)
    return std::unexpected(std::move(expected.error()));
  root->value.bytes.assign(command.displayName.begin(),
                           command.displayName.end());
  return MutationBatch{document::ReplaceRecord{root->id, *expected, *root}};
}

Result<MutationBatch>
normalizeSource(const ProjectState &state,
                const document::ContentStore &contentStore,
                const ProjectId &project, document::ProjectPath path,
                document::ContentEntry content,
                std::optional<ContentDigest> expectedPrior) {
  if (auto result = requireProject(state, project); !result)
    return std::unexpected(std::move(result.error()));
  if (auto valid = validateSourceContent(content, contentStore); !valid)
    return std::unexpected(std::move(valid.error()));
  return MutationBatch{document::PutContent{
      std::move(path), std::move(expectedPrior), std::move(content)}};
}

Result<MutationBatch>
normalizeCommand(const ProjectState &state, RecordId projectRootRecord,
                 const document::ContentStore &contentStore,
                 internal::CommandIntent command) {
  return std::visit(
      [&](auto &&value) -> Result<MutationBatch> {
        using Intent = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::is_same_v<Intent, internal::RenameProjectIntent>) {
          return normalizeRename(state, projectRootRecord, std::move(value));
        } else if constexpr (std::is_same_v<
                                 Intent, internal::CreateSourceModuleIntent>) {
          return normalizeSource(state, contentStore, value.project,
                                 std::move(value.path),
                                 std::move(value.content), std::nullopt);
        } else if constexpr (std::is_same_v<
                                 Intent, internal::ReplaceSourceModuleIntent>) {
          return normalizeSource(state, contentStore, value.project,
                                 std::move(value.path),
                                 std::move(value.content), value.expectedPrior);
        } else {
          static_assert(unsupportedIntent<Intent>,
                        "command intent has no mutation normalizer");
        }
      },
      std::move(command));
}

Result<void> queryProjectMetadata(const ProjectSnapshot &snapshot,
                                  RecordId projectRootRecord,
                                  const protobuf::Message &payload,
                                  wire::QueryResult &result) {
  const auto &query =
      static_cast<const wire::GetProjectMetadataQuery &>(payload);
  auto project = api::readId<ProjectId>(query.project_id());
  if (!project)
    return std::unexpected(std::move(project.error()));
  if (*project != snapshot.state().projectId())
    return std::unexpected(
        diagnostic("project.id.mismatch", "query targets another project"));
  auto root = projectRoot(snapshot.state(), projectRootRecord);
  if (!root)
    return std::unexpected(std::move(root.error()));
  const std::string displayName(root->value.bytes.begin(),
                                root->value.bytes.end());
  if (auto validation = validateDisplayName(displayName); !validation)
    return std::unexpected(std::move(validation.error()));
  wire::ProjectMetadataResult *metadata = result.mutable_project_metadata();
  api::writeDigest(snapshot.revisionId(),
                   metadata->mutable_observed_revision());
  api::writeId(snapshot.state().projectId(), metadata->mutable_project_id());
  metadata->set_display_name(displayName);
  return {};
}

Result<void> querySourceModuleMetadata(const ProjectSnapshot &snapshot,
                                       RecordId,
                                       const protobuf::Message &payload,
                                       wire::QueryResult &result) {
  const auto &query =
      static_cast<const wire::GetSourceModuleMetadataQuery &>(payload);
  if (auto project = requireProject(snapshot.state(), query.project_id());
      !project)
    return std::unexpected(std::move(project.error()));
  auto path = document::ProjectPath::parse(query.path());
  if (!path)
    return std::unexpected(std::move(path.error()));
  auto entry = snapshot.state().content(*path);
  if (!entry)
    return std::unexpected(diagnostic("source.module.not-found",
                                      "source module is not available"));
  wire::SourceModuleMetadataResult *metadata =
      result.mutable_source_module_metadata();
  api::writeDigest(snapshot.revisionId(),
                   metadata->mutable_observed_revision());
  api::writeId(snapshot.state().projectId(), metadata->mutable_project_id());
  metadata->set_path(path->value());
  writeContentReference(*entry, *metadata->mutable_content());
  return {};
}

using QueryHandler = Result<void> (*)(const ProjectSnapshot &, RecordId,
                                      const protobuf::Message &,
                                      wire::QueryResult &);
struct QueryRegistration {
  const protobuf::Descriptor *descriptor;
  QueryHandler query;
};

const std::array queryRegistry{
    QueryRegistration{wire::GetProjectMetadataQuery::descriptor(),
                      queryProjectMetadata},
    QueryRegistration{wire::GetSourceModuleMetadataQuery::descriptor(),
                      querySourceModuleMetadata},
};

const QueryRegistration *
queryRegistration(const protobuf::Descriptor *descriptor) {
  const auto found = std::ranges::find(queryRegistry, descriptor,
                                       &QueryRegistration::descriptor);
  return found == queryRegistry.end() ? nullptr : &*found;
}

Result<void> verifyRegistryCoverage() {
  if (auto commands = internal::verifyCommandIntentRegistry(); !commands)
    return commands;
  std::size_t queries = 0;
  for (const protobuf::Descriptor *descriptor : api::registeredWireTypes()) {
    const wire::MessageRules &rules =
        descriptor->options().GetExtension(wire::message_rules);
    if (rules.surface() == wire::SURFACE_KIND_QUERY) {
      ++queries;
      if (!queryRegistration(descriptor))
        return std::unexpected(diagnostic("engineering.query.unhandled",
                                          "registered query has no handler"));
    }
  }
  if (queries != queryRegistry.size())
    return std::unexpected(diagnostic("engineering.registry.extra-handler",
                                      "handler has no registered schema"));
  return {};
}

const protobuf::Message *payload(const protobuf::Message &envelope) {
  const protobuf::Descriptor *descriptor = envelope.GetDescriptor();
  const protobuf::OneofDescriptor *oneof =
      descriptor->FindOneofByName("payload");
  const protobuf::Reflection *reflection = envelope.GetReflection();
  if (!oneof)
    return nullptr;
  const protobuf::FieldDescriptor *field =
      reflection->GetOneofFieldDescriptor(envelope, oneof);
  return field ? &reflection->GetMessage(envelope, field) : nullptr;
}

std::string_view permission(const protobuf::Message &message) {
  const wire::MessageRules &rules =
      message.GetDescriptor()->options().GetExtension(wire::message_rules);
  return rules.permission();
}

struct Submission {
  internal::CommandRequestIntent intent;
  ContentDigest inputDigest;
};

template <typename Envelope>
Result<Submission> prepareSubmission(const Envelope &envelope,
                                     const PermissionPolicy &permissions) {
  auto intent = internal::parseCommandRequest(envelope);
  if (!intent)
    return std::unexpected(std::move(intent.error()));
  auto inputDigest = internal::semanticRequestDigest(*intent);
  if (!inputDigest)
    return std::unexpected(std::move(inputDigest.error()));
  for (const internal::CommandIntent &operation : intent->operations) {
    if (auto authorized = permissions.authorize(
            {intent->actor, intent->origin, intent->permissionContext,
             internal::commandIntentPermission(operation)});
        !authorized)
      return std::unexpected(std::move(authorized.error()));
  }
  return Submission{std::move(*intent), *inputDigest};
}

} // namespace

struct InMemoryEngineeringService::Impl {
  struct Node {
    RevisionRecord revision;
    document::Bytes canonical;
    std::shared_ptr<const ProjectSnapshot> snapshot;
  };

  struct RequestOutcome {
    ContentDigest digest;
    wire::CommandResult result;
  };

  using RevisionMap =
      std::unordered_map<RevisionId, std::shared_ptr<const Node>,
                         TypedDigestHash<RevisionIdTag>>;
  using ChildMap = std::unordered_map<RevisionId, std::vector<RevisionId>,
                                      TypedDigestHash<RevisionIdTag>>;
  using RequestMap =
      std::unordered_map<RequestId, RequestOutcome, TypedIdHash<RequestIdTag>>;
  using TransactionMap = std::unordered_map<TransactionId, RevisionId,
                                            TypedIdHash<TransactionIdTag>>;

  Impl(std::shared_ptr<const PermissionPolicy> permissionPolicy,
       std::shared_ptr<const document::ContentStore> content,
       RecordId rootRecord, std::shared_ptr<const Node> genesis)
      : permissions(std::move(permissionPolicy)),
        contentStore(std::move(content)), projectRoot(rootRecord),
        head(genesis->revision.id) {
    transactions.emplace(genesis->revision.envelope.transaction,
                         genesis->revision.id);
    revisions.emplace(genesis->revision.id, std::move(genesis));
  }

  std::shared_ptr<const PermissionPolicy> permissions;
  std::shared_ptr<const document::ContentStore> contentStore;
  RecordId projectRoot;
  mutable std::mutex mutex;
  RevisionMap revisions;
  ChildMap children;
  RequestMap requests;
  TransactionMap transactions;
  RevisionId head;

  [[nodiscard]] Result<wire::CommandResult> submit(Submission submission,
                                                   TransactionContext context);
};

Result<std::unique_ptr<InMemoryEngineeringService>>
InMemoryEngineeringService::create(
    GenesisContext context, std::shared_ptr<const PermissionPolicy> permissions,
    std::shared_ptr<const document::ContentStore> contentStore) {
  if (!permissions)
    return std::unexpected(diagnostic("engineering.permission.missing-policy",
                                      "permission policy is required"));
  if (!contentStore)
    return std::unexpected(diagnostic("engineering.content.missing-store",
                                      "content store is required"));
  if (auto registry = verifyRegistryCoverage(); !registry)
    return std::unexpected(std::move(registry.error()));
  if (auto name = validateDisplayName(context.displayName); !name)
    return std::unexpected(std::move(name.error()));
  auto state = ProjectState::create(context.project, context.schemaSet);
  if (!state)
    return std::unexpected(std::move(state.error()));
  EngineeringRecord root{
      context.projectRootRecord,
      std::nullopt,
      document::Lifecycle::Active,
      {std::string{projectRootKind},
       1,
       {context.displayName.begin(), context.displayName.end()}},
      {context.actor, Origin::System, std::nullopt,
       context.committedAtUnixMilliseconds}};
  MutationBatch mutations{document::CreateRecord{root}};
  auto initialized =
      document::internal::ProjectStateAccess::apply(*state, mutations);
  if (!initialized)
    return std::unexpected(std::move(initialized.error()));
  RevisionEnvelope envelope{{},
                            context.transaction,
                            std::nullopt,
                            std::nullopt,
                            context.actor,
                            Origin::System,
                            std::nullopt,
                            std::nullopt,
                            context.schemaSet,
                            context.committedAtUnixMilliseconds,
                            initialized->rootDigest(),
                            {"project.create"},
                            std::move(mutations)};
  auto id = revisionId(envelope);
  auto encoded = canonicalBytes(envelope);
  if (!id || !encoded)
    return std::unexpected(id ? std::move(encoded.error())
                              : std::move(id.error()));
  auto snapshot = std::make_shared<const ProjectSnapshot>(*initialized, *id);
  auto node = std::make_shared<const Impl::Node>(Impl::Node{
      {*id, std::move(envelope)}, std::move(*encoded), std::move(snapshot)});
  auto impl =
      std::make_unique<Impl>(std::move(permissions), std::move(contentStore),
                             context.projectRootRecord, std::move(node));
  return std::unique_ptr<InMemoryEngineeringService>(
      new InMemoryEngineeringService(std::move(impl)));
}

InMemoryEngineeringService::InMemoryEngineeringService(
    std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}
InMemoryEngineeringService::~InMemoryEngineeringService() = default;
InMemoryEngineeringService::InMemoryEngineeringService(
    InMemoryEngineeringService &&) noexcept = default;
InMemoryEngineeringService &InMemoryEngineeringService::operator=(
    InMemoryEngineeringService &&) noexcept = default;

Result<wire::CommandResult>
InMemoryEngineeringService::submit(const wire::CommandEnvelope &command,
                                   TransactionContext context) {
  auto submission = prepareSubmission(command, *impl_->permissions);
  if (!submission)
    return commandFailure(std::move(submission.error()));
  return impl_->submit(std::move(*submission), context);
}

Result<wire::CommandResult>
InMemoryEngineeringService::submit(const wire::TransactionEnvelope &transaction,
                                   TransactionContext context) {
  auto submission = prepareSubmission(transaction, *impl_->permissions);
  if (!submission)
    return commandFailure(std::move(submission.error()));
  return impl_->submit(std::move(*submission), context);
}

Result<wire::CommandResult>
InMemoryEngineeringService::Impl::submit(Submission submission,
                                         TransactionContext context) {
  const RequestId request = submission.intent.request;
  const RevisionId base = submission.intent.base;

  const auto requestOutcome = [&](const Impl::RequestOutcome &outcome) {
    if (outcome.digest != submission.inputDigest)
      return commandFailure(
          diagnostic("engineering.request.identity-reuse",
                     "request identifier was reused with different content",
                     Severity::Fatal));
    return outcome.result;
  };

  std::shared_ptr<const ProjectSnapshot> baseSnapshot;
  {
    std::scoped_lock lock{mutex};
    if (const auto found = requests.find(request); found != requests.end())
      return requestOutcome(found->second);
    if (transactions.contains(context.transaction))
      return commandFailure(diagnostic("engineering.transaction.identity-reuse",
                                       "transaction identifier was reused",
                                       Severity::Fatal));
    if (base != head)
      return commandFailure(diagnostic("engineering.revision.conflict",
                                       "workspace head has moved"));
    const auto baseNode = revisions.find(base);
    if (baseNode == revisions.end())
      return std::unexpected(diagnostic("engineering.head.missing",
                                        "workspace head is unavailable",
                                        Severity::Fatal));
    baseSnapshot = baseNode->second->snapshot;
  }

  const auto preparationFailure = [&](Diagnostic error) {
    std::scoped_lock lock{mutex};
    if (const auto found = requests.find(request); found != requests.end())
      return requestOutcome(found->second);
    return commandFailure(std::move(error));
  };

  ProjectState state = baseSnapshot->state();
  MutationBatch mutations;
  std::vector<std::string> commandTypes;
  commandTypes.reserve(submission.intent.operations.size());
  for (internal::CommandIntent &operation : submission.intent.operations) {
    commandTypes.emplace_back(internal::commandIntentStableName(operation));
    auto normalized = normalizeCommand(state, projectRoot, *contentStore,
                                       std::move(operation));
    if (!normalized)
      return preparationFailure(std::move(normalized.error()));
    auto next =
        document::internal::ProjectStateAccess::apply(state, *normalized);
    if (!next)
      return preparationFailure(std::move(next.error()));
    mutations.insert(mutations.end(),
                     std::make_move_iterator(normalized->begin()),
                     std::make_move_iterator(normalized->end()));
    state = std::move(*next);
  }
  RevisionEnvelope envelope{{base},
                            context.transaction,
                            request,
                            submission.inputDigest,
                            submission.intent.actor,
                            submission.intent.origin,
                            submission.intent.permissionContext,
                            std::move(submission.intent.gesture),
                            state.schemaSet(),
                            context.committedAtUnixMilliseconds,
                            state.rootDigest(),
                            std::move(commandTypes),
                            std::move(mutations)};
  auto id = revisionId(envelope);
  auto encoded = canonicalBytes(envelope);
  if (!id || !encoded)
    return std::unexpected(id ? std::move(encoded.error())
                              : std::move(id.error()));
  auto snapshot = std::make_shared<const ProjectSnapshot>(state, *id);
  auto node = std::make_shared<const Impl::Node>(Impl::Node{
      {*id, std::move(envelope)}, std::move(*encoded), std::move(snapshot)});

  std::scoped_lock lock{mutex};
  if (const auto found = requests.find(request); found != requests.end())
    return requestOutcome(found->second);
  if (transactions.contains(context.transaction))
    return commandFailure(diagnostic("engineering.transaction.identity-reuse",
                                     "transaction identifier was reused",
                                     Severity::Fatal));
  if (head != base)
    return commandFailure(diagnostic("engineering.revision.conflict",
                                     "workspace head has moved"));
  if (const auto collision = revisions.find(*id);
      collision != revisions.end()) {
    if (collision->second->canonical != node->canonical)
      return std::unexpected(
          diagnostic("engineering.revision.collision",
                     "revision digest collision or corruption was detected",
                     Severity::Fatal));
    return std::unexpected(
        diagnostic("engineering.revision.duplicate",
                   "revision already exists without its request outcome",
                   Severity::Fatal));
  }

  revisions.emplace(*id, std::move(node));
  children[base].push_back(*id);
  transactions.emplace(context.transaction, *id);
  head = *id;
  wire::CommandResult result;
  result.set_committed(true);
  api::writeDigest(*id, result.mutable_revision());
  requests.emplace(request,
                   Impl::RequestOutcome{submission.inputDigest, result});
  return result;
}

Result<wire::QueryResult>
InMemoryEngineeringService::query(const wire::QueryEnvelope &query) const {
  if (auto validation = api::validateWire(query); !validation)
    return queryFailure(std::move(validation.error()));
  const protobuf::Message *queryPayload = payload(query);
  const QueryRegistration *registration =
      queryPayload ? queryRegistration(queryPayload->GetDescriptor()) : nullptr;
  if (!registration)
    return queryFailure(
        diagnostic("engineering.query.unsupported", "query is not supported"));
  auto revision = api::readDigest<RevisionId>(query.revision());
  auto actor = api::readId<ActorId>(query.actor_id());
  auto origin = api::readOrigin(query.origin());
  auto permissionContext =
      api::readId<PermissionContextId>(query.permission_context_id());
  if (!revision || !actor || !origin || !permissionContext)
    return queryFailure(diagnostic("engineering.query.invalid-envelope",
                                   "query envelope conversion failed"));
  if (auto authorized = impl_->permissions->authorize(
          {*actor, *origin, *permissionContext, permission(*queryPayload)});
      !authorized)
    return queryFailure(std::move(authorized.error()));

  std::shared_ptr<const ProjectSnapshot> snapshot;
  {
    std::scoped_lock lock{impl_->mutex};
    const auto found = impl_->revisions.find(*revision);
    if (found == impl_->revisions.end())
      return queryFailure(diagnostic("engineering.revision.not-found",
                                     "requested revision is unavailable"));
    snapshot = found->second->snapshot;
  }
  wire::QueryResult result;
  if (auto handled = registration->query(*snapshot, impl_->projectRoot,
                                         *queryPayload, result);
      !handled)
    return queryFailure(std::move(handled.error()));
  return result;
}

std::shared_ptr<const ProjectSnapshot>
InMemoryEngineeringService::headSnapshot() const {
  std::scoped_lock lock{impl_->mutex};
  return impl_->revisions.at(impl_->head)->snapshot;
}

Result<void> InMemoryEngineeringService::undo() {
  std::scoped_lock lock{impl_->mutex};
  const auto node = impl_->revisions.find(impl_->head);
  if (node == impl_->revisions.end() ||
      node->second->revision.envelope.parents.empty())
    return std::unexpected(diagnostic("engineering.history.no-undo",
                                      "workspace has no parent revision"));
  impl_->head = node->second->revision.envelope.parents.front();
  return {};
}

std::vector<RevisionId> InMemoryEngineeringService::redoChoices() const {
  std::scoped_lock lock{impl_->mutex};
  const auto found = impl_->children.find(impl_->head);
  if (found == impl_->children.end())
    return {};
  std::vector<RevisionId> result = found->second;
  std::ranges::sort(result);
  return result;
}

Result<void> InMemoryEngineeringService::redo(const RevisionId &revision) {
  std::scoped_lock lock{impl_->mutex};
  const auto found = impl_->children.find(impl_->head);
  if (found == impl_->children.end() ||
      std::ranges::find(found->second, revision) == found->second.end())
    return std::unexpected(diagnostic("engineering.history.invalid-redo",
                                      "revision is not a redo choice"));
  impl_->head = revision;
  return {};
}

std::size_t InMemoryEngineeringService::revisionCount() const {
  std::scoped_lock lock{impl_->mutex};
  return impl_->revisions.size();
}

std::size_t InMemoryEngineeringService::registeredCommandCount() {
  return internal::commandIntentRegistry().size();
}

std::size_t InMemoryEngineeringService::registeredQueryCount() {
  return queryRegistry.size();
}

} // namespace kearne::engineering
