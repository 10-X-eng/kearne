#pragma once

#include <kearne/document/project_state.hpp>
#include <kearne/engineering/revision.hpp>

#include <memory>
#include <optional>
#include <vector>

namespace kearne::persistence {

struct StoredContent {
  ContentDigest digest;
  document::Bytes bytes;
};

struct DurableRevision {
  engineering::RevisionRecord revision;
  document::ProjectSnapshot snapshot;
  std::vector<StoredContent> content;
};

struct RecoveredProject {
  RecordId projectRootRecord;
  engineering::RevisionRecord headRevision;
  document::ProjectSnapshot headSnapshot;
  RevisionId savedRevision;
};

class ProjectStore {
public:
  virtual ~ProjectStore() = default;

  [[nodiscard]] virtual Result<void>
  commit(std::optional<RevisionId> expectedHead,
         const DurableRevision &revision) = 0;
  [[nodiscard]] virtual Result<RecoveredProject> loadHead() const = 0;
  [[nodiscard]] virtual Result<engineering::RevisionRecord>
  loadRevision(const RevisionId &revision) const = 0;
  [[nodiscard]] virtual Result<std::vector<RevisionId>>
  children(const RevisionId &revision) const = 0;
  [[nodiscard]] virtual Result<std::shared_ptr<const document::Bytes>>
  content(const ContentDigest &digest) const = 0;
  [[nodiscard]] virtual Result<void>
  moveHead(const RevisionId &expectedHead,
           const document::ProjectSnapshot &target) = 0;
  [[nodiscard]] virtual Result<void>
  savePoint(const RevisionId &expectedHead) = 0;
};

} // namespace kearne::persistence
