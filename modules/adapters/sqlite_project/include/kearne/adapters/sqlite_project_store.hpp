#pragma once

#include <kearne/persistence/project_store.hpp>

#include <filesystem>
#include <memory>
#include <string>

namespace kearne::adapters {

enum class ProjectOpenMode { ReadWrite, ReadOnly };

struct SqliteProjectStoreOptions {
  std::string applicationBuild;
  std::size_t maximumCheckpointBytes = 512U * 1024U * 1024U;
  std::size_t maximumContentBytes = 16U * 1024U * 1024U;
  std::uint32_t busyTimeoutMilliseconds = 5'000U;
};

class SqliteProjectStore final : public persistence::ProjectStore {
public:
  [[nodiscard]] static Result<std::unique_ptr<SqliteProjectStore>>
  create(const std::filesystem::path &path, RecordId projectRootRecord,
         const persistence::DurableRevision &genesis,
         SqliteProjectStoreOptions options);
  [[nodiscard]] static Result<std::unique_ptr<SqliteProjectStore>>
  open(const std::filesystem::path &path, ProjectOpenMode mode,
       SqliteProjectStoreOptions options);

  ~SqliteProjectStore() override;
  SqliteProjectStore(SqliteProjectStore &&) noexcept;
  SqliteProjectStore &operator=(SqliteProjectStore &&) noexcept;
  SqliteProjectStore(const SqliteProjectStore &) = delete;
  SqliteProjectStore &operator=(const SqliteProjectStore &) = delete;

  [[nodiscard]] Result<void>
  commit(std::optional<RevisionId> expectedHead,
         const persistence::DurableRevision &revision) override;
  [[nodiscard]] Result<persistence::RecoveredProject> loadHead() const override;
  [[nodiscard]] Result<engineering::RevisionRecord>
  loadRevision(const RevisionId &revision) const override;
  [[nodiscard]] Result<std::vector<RevisionId>>
  children(const RevisionId &revision) const override;
  [[nodiscard]] Result<std::shared_ptr<const document::Bytes>>
  content(const ContentDigest &digest) const override;
  [[nodiscard]] Result<void>
  moveHead(const RevisionId &expectedHead,
           const document::ProjectSnapshot &target) override;
  [[nodiscard]] Result<void> savePoint(const RevisionId &expectedHead) override;

private:
  struct Impl;
  explicit SqliteProjectStore(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

} // namespace kearne::adapters
