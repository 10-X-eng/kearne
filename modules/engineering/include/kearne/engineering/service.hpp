#pragma once

#include <kearne/api/v1/engineering.pb.h>
#include <kearne/document/content_store.hpp>
#include <kearne/document/project_state.hpp>
#include <kearne/engineering/revision.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace kearne::engineering {

struct PermissionRequest {
  ActorId actor;
  Origin origin;
  PermissionContextId context;
  std::string_view permission;
};

class PermissionPolicy {
public:
  virtual ~PermissionPolicy() = default;
  [[nodiscard]] virtual Result<void>
  authorize(const PermissionRequest &request) const = 0;
};

struct GenesisContext {
  ProjectId project;
  RecordId projectRootRecord;
  TransactionId transaction;
  ActorId actor;
  SchemaSetDigest schemaSet;
  std::uint64_t committedAtUnixMilliseconds;
  std::string displayName;
};

struct TransactionContext {
  TransactionId transaction;
  std::uint64_t committedAtUnixMilliseconds;
};

class EngineeringService {
public:
  virtual ~EngineeringService() = default;
  [[nodiscard]] virtual Result<api::v1::CommandResult>
  submit(const api::v1::CommandEnvelope &command,
         TransactionContext context) = 0;
  [[nodiscard]] virtual Result<api::v1::CommandResult>
  submit(const api::v1::TransactionEnvelope &transaction,
         TransactionContext context) = 0;
  [[nodiscard]] virtual Result<api::v1::QueryResult>
  query(const api::v1::QueryEnvelope &query) const = 0;
  [[nodiscard]] virtual std::shared_ptr<const document::ProjectSnapshot>
  headSnapshot() const = 0;
  [[nodiscard]] virtual Result<void> undo() = 0;
  [[nodiscard]] virtual std::vector<RevisionId> redoChoices() const = 0;
  [[nodiscard]] virtual Result<void> redo(const RevisionId &revision) = 0;
  [[nodiscard]] virtual std::size_t revisionCount() const = 0;
};

class InMemoryEngineeringService final : public EngineeringService {
public:
  [[nodiscard]] static Result<std::unique_ptr<InMemoryEngineeringService>>
  create(GenesisContext context,
         std::shared_ptr<const PermissionPolicy> permissions,
         std::shared_ptr<const document::ContentStore> contentStore);
  ~InMemoryEngineeringService() override;
  InMemoryEngineeringService(InMemoryEngineeringService &&) noexcept;
  InMemoryEngineeringService &operator=(InMemoryEngineeringService &&) noexcept;
  InMemoryEngineeringService(const InMemoryEngineeringService &) = delete;
  InMemoryEngineeringService &
  operator=(const InMemoryEngineeringService &) = delete;

  [[nodiscard]] Result<api::v1::CommandResult>
  submit(const api::v1::CommandEnvelope &command,
         TransactionContext context) override;
  [[nodiscard]] Result<api::v1::CommandResult>
  submit(const api::v1::TransactionEnvelope &transaction,
         TransactionContext context) override;
  [[nodiscard]] Result<api::v1::QueryResult>
  query(const api::v1::QueryEnvelope &query) const override;

  [[nodiscard]] std::shared_ptr<const document::ProjectSnapshot>
  headSnapshot() const override;
  [[nodiscard]] Result<void> undo() override;
  [[nodiscard]] std::vector<RevisionId> redoChoices() const override;
  [[nodiscard]] Result<void> redo(const RevisionId &revision) override;
  [[nodiscard]] std::size_t revisionCount() const override;

  [[nodiscard]] static std::size_t registeredCommandCount();
  [[nodiscard]] static std::size_t registeredQueryCount();

private:
  struct Impl;
  explicit InMemoryEngineeringService(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

} // namespace kearne::engineering
