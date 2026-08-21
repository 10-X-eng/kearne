#include <kearne/adapters/sqlite_project_store.hpp>

#include <kearne/document/checkpoint.hpp>
#include <kearne/document/content_store.hpp>

#include <sqlite3.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <mutex>
#include <new>
#include <ranges>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace kearne::adapters {
namespace {

constexpr int applicationId = 0x4b454152;
constexpr int containerVersion = 1;

class Failure final {
public:
  explicit Failure(Diagnostic diagnostic)
      : diagnostic_(std::move(diagnostic)) {}
  Diagnostic take() { return std::move(diagnostic_); }

private:
  Diagnostic diagnostic_;
};

[[noreturn]] void fail(std::string code, std::string summary,
                       Severity severity = Severity::Error) {
  throw Failure{diagnostic(std::move(code), std::move(summary), severity)};
}

[[noreturn]] void failSqlite(sqlite3 *database, std::string code,
                             std::string summary) {
  Diagnostic value = diagnostic(std::move(code), std::move(summary));
  if (database) {
    value.parameters.push_back(
        std::to_string(sqlite3_extended_errcode(database)));
    value.detail = sqlite3_errmsg(database);
  }
  throw Failure{std::move(value)};
}

void requireSqlite(int result, sqlite3 *database, std::string_view operation) {
  if (result != SQLITE_OK)
    failSqlite(database, "persistence.sqlite." + std::string{operation},
               "project storage operation failed");
}

template <typename Value> Value require(Result<Value> result) {
  if (!result)
    throw Failure{std::move(result.error())};
  return std::move(*result);
}

template <typename Value, typename Action>
Result<Value> guarded(Action &&action) {
  try {
    if constexpr (std::is_void_v<Value>) {
      std::forward<Action>(action)();
      return {};
    } else {
      return std::forward<Action>(action)();
    }
  } catch (Failure &failure) {
    return std::unexpected(failure.take());
  } catch (const std::bad_alloc &) {
    return std::unexpected(diagnostic("persistence.memory.exhausted",
                                      "project storage ran out of memory"));
  }
}

class Statement final {
public:
  Statement(sqlite3 *database, std::string_view sql) : database_(database) {
    requireSqlite(
        sqlite3_prepare_v3(database, sql.data(), static_cast<int>(sql.size()),
                           SQLITE_PREPARE_PERSISTENT, &statement_, nullptr),
        database, "prepare");
  }
  ~Statement() { sqlite3_finalize(statement_); }
  Statement(const Statement &) = delete;
  Statement &operator=(const Statement &) = delete;

  void text(int index, std::string_view value) {
    requireSqlite(sqlite3_bind_text64(statement_, index, value.data(),
                                      value.size(), SQLITE_TRANSIENT,
                                      SQLITE_UTF8),
                  database_, "bind-text");
  }
  void blob(int index, std::span<const std::uint8_t> value) {
    const int result =
        value.empty() ? sqlite3_bind_zeroblob64(statement_, index, 0)
                      : sqlite3_bind_blob64(statement_, index, value.data(),
                                            value.size(), SQLITE_TRANSIENT);
    requireSqlite(result, database_, "bind-blob");
  }
  void integer(int index, std::uint64_t value) {
    if (value >
        static_cast<std::uint64_t>(std::numeric_limits<sqlite3_int64>::max()))
      fail("persistence.sqlite.integer-range",
           "project storage integer exceeds SQLite range");
    requireSqlite(sqlite3_bind_int64(statement_, index,
                                     static_cast<sqlite3_int64>(value)),
                  database_, "bind-integer");
  }
  void null(int index) {
    requireSqlite(sqlite3_bind_null(statement_, index), database_, "bind-null");
  }
  void optionalText(int index, const std::optional<std::string> &value) {
    if (value)
      text(index, *value);
    else
      null(index);
  }
  bool row() {
    const int result = sqlite3_step(statement_);
    if (result == SQLITE_ROW)
      return true;
    if (result == SQLITE_DONE)
      return false;
    failSqlite(database_, "persistence.sqlite.step",
               "project storage statement failed");
  }
  void execute() {
    if (row())
      fail("persistence.sqlite.unexpected-row",
           "project storage write returned a row");
  }
  std::string text(int column) const {
    if (sqlite3_column_type(statement_, column) != SQLITE_TEXT)
      fail("persistence.sqlite.column-type",
           "project storage text column is invalid", Severity::Fatal);
    const auto *value = sqlite3_column_text(statement_, column);
    const int size = sqlite3_column_bytes(statement_, column);
    return {reinterpret_cast<const char *>(value),
            static_cast<std::size_t>(size)};
  }
  std::optional<std::string> optionalText(int column) const {
    if (sqlite3_column_type(statement_, column) == SQLITE_NULL)
      return std::nullopt;
    return text(column);
  }
  std::uint64_t integer(int column) const {
    if (sqlite3_column_type(statement_, column) != SQLITE_INTEGER)
      fail("persistence.sqlite.column-type",
           "project storage integer column is invalid", Severity::Fatal);
    const sqlite3_int64 value = sqlite3_column_int64(statement_, column);
    if (value < 0)
      fail("persistence.sqlite.column-range",
           "project storage integer column is negative", Severity::Fatal);
    return static_cast<std::uint64_t>(value);
  }
  document::Bytes blob(int column, std::size_t maximumBytes) const {
    if (sqlite3_column_type(statement_, column) != SQLITE_BLOB)
      fail("persistence.sqlite.column-type",
           "project storage binary column is invalid", Severity::Fatal);
    const int encodedSize = sqlite3_column_bytes(statement_, column);
    if (encodedSize < 0 || static_cast<std::size_t>(encodedSize) > maximumBytes)
      fail("persistence.sqlite.column-limit",
           "project storage binary column exceeds its limit", Severity::Fatal);
    const auto *data = static_cast<const std::uint8_t *>(
        sqlite3_column_blob(statement_, column));
    if (encodedSize == 0)
      return {};
    if (!data)
      fail("persistence.sqlite.column-null",
           "project storage binary column is unavailable", Severity::Fatal);
    return {data, data + encodedSize};
  }

private:
  sqlite3 *database_;
  sqlite3_stmt *statement_ = nullptr;
};

void execute(sqlite3 *database, std::string_view sql) {
  char *message = nullptr;
  const int result = sqlite3_exec(database, std::string{sql}.c_str(), nullptr,
                                  nullptr, &message);
  if (result == SQLITE_OK)
    return;
  Diagnostic value = diagnostic("persistence.sqlite.execute",
                                "project storage statement failed");
  value.parameters.push_back(std::to_string(result));
  if (message)
    value.detail = message;
  sqlite3_free(message);
  throw Failure{std::move(value)};
}

class Transaction final {
public:
  explicit Transaction(sqlite3 *database) : database_(database) {
    execute(database_, "BEGIN IMMEDIATE");
  }
  ~Transaction() {
    if (!committed_)
      sqlite3_exec(database_, "ROLLBACK", nullptr, nullptr, nullptr);
  }
  void commit() {
    execute(database_, "COMMIT");
    committed_ = true;
  }

private:
  sqlite3 *database_;
  bool committed_ = false;
};

std::string pathUtf8(const std::filesystem::path &path) {
  const std::u8string encoded = path.u8string();
  return {reinterpret_cast<const char *>(encoded.data()), encoded.size()};
}

void validateOptions(const SqliteProjectStoreOptions &options) {
  constexpr std::size_t maximumSqliteValue = 1'000'000'000U;
  if (options.applicationBuild.empty() ||
      options.applicationBuild.size() > 128U ||
      !document::isValidUtf8(options.applicationBuild) ||
      options.maximumCheckpointBytes == 0 ||
      options.maximumCheckpointBytes > maximumSqliteValue ||
      options.maximumContentBytes == 0 ||
      options.maximumContentBytes > options.maximumCheckpointBytes ||
      options.busyTimeoutMilliseconds == 0 ||
      options.busyTimeoutMilliseconds > 60'000U)
    fail("persistence.options.invalid", "project storage options are invalid");
}

template <typename Digest>
Digest parseDigest(std::string_view value, std::string_view field) {
  auto decoded = Digest::parse(value);
  if (!decoded)
    fail("persistence." + std::string{field} + ".invalid",
         "project storage contains an invalid digest", Severity::Fatal);
  return std::move(*decoded);
}

template <typename Id>
Id parseId(std::string_view value, std::string_view field) {
  auto decoded = Id::parse(value);
  if (!decoded)
    fail("persistence." + std::string{field} + ".invalid",
         "project storage contains an invalid identifier", Severity::Fatal);
  return std::move(*decoded);
}

std::optional<std::string> text(const std::optional<RevisionId> &value) {
  return value ? std::optional{value->toString()} : std::nullopt;
}

struct EncodedRevision {
  document::Bytes envelope;
  document::Bytes checkpoint;
};

EncodedRevision encodeRevision(const persistence::DurableRevision &value,
                               const SqliteProjectStoreOptions &options,
                               const std::optional<RevisionId> &expectedHead) {
  if (value.revision.id != value.snapshot.revisionId() ||
      value.revision.envelope.projectRootDigest !=
          value.snapshot.state().rootDigest() ||
      value.revision.envelope.schemaSet != value.snapshot.state().schemaSet())
    fail("persistence.revision.snapshot-mismatch",
         "revision and project checkpoint disagree");
  if (expectedHead) {
    if (value.revision.envelope.parents.empty() ||
        value.revision.envelope.parents.front() != *expectedHead)
      fail("persistence.revision.parent-mismatch",
           "revision does not descend from the expected head");
  } else if (!value.revision.envelope.parents.empty()) {
    fail("persistence.revision.genesis-parent",
         "genesis revision cannot have a parent");
  }
  auto envelope = require(engineering::canonicalBytes(value.revision.envelope));
  auto actualId = require(engineering::revisionId(value.revision.envelope));
  if (actualId != value.revision.id)
    fail("persistence.revision.identity-mismatch",
         "revision bytes do not match their identity", Severity::Fatal);
  engineering::RevisionDecodeLimits revisionLimits;
  revisionLimits.maximumEncodedBytes = options.maximumCheckpointBytes;
  const auto decoded =
      require(engineering::decodeRevisionRecord(envelope, revisionLimits));
  if (decoded.id != value.revision.id)
    fail("persistence.revision.identity-mismatch",
         "revision decoder changed its identity", Severity::Fatal);
  document::ProjectCheckpointLimits checkpointLimits;
  checkpointLimits.maximumEncodedBytes = options.maximumCheckpointBytes;
  auto checkpoint =
      require(document::canonicalBytes(value.snapshot, checkpointLimits));
  return {std::move(envelope), std::move(checkpoint)};
}

void validateContent(const persistence::StoredContent &content,
                     std::size_t maximumBytes) {
  if (content.bytes.size() > maximumBytes)
    fail("persistence.content.size-limit",
         "project content exceeds its byte limit");
  const ContentDigest actual = require(document::contentDigest(content.bytes));
  if (actual != content.digest)
    fail("persistence.content.digest-mismatch",
         "project content bytes do not match their digest", Severity::Fatal);
}

void configureConnection(sqlite3 *database, ProjectOpenMode mode,
                         const SqliteProjectStoreOptions &options) {
  requireSqlite(sqlite3_extended_result_codes(database, 1), database,
                "extended-results");
  requireSqlite(
      sqlite3_busy_timeout(database,
                           static_cast<int>(options.busyTimeoutMilliseconds)),
      database, "busy-timeout");
  requireSqlite(
      sqlite3_db_config(database, SQLITE_DBCONFIG_DEFENSIVE, 1, nullptr),
      database, "defensive-mode");
  sqlite3_limit(database, SQLITE_LIMIT_LENGTH,
                static_cast<int>(options.maximumCheckpointBytes));
  execute(database, "PRAGMA foreign_keys=ON");
  execute(database, "PRAGMA trusted_schema=OFF");
  if (mode == ProjectOpenMode::ReadOnly) {
    execute(database, "PRAGMA query_only=ON");
  }
}

void configureWriter(sqlite3 *database) {
  Statement journal(database, "PRAGMA journal_mode=WAL");
  if (!journal.row() || journal.text(0) != "wal" || journal.row())
    fail("persistence.sqlite.wal-unavailable",
         "project storage could not enable its journal");
  execute(database, "PRAGMA synchronous=FULL");
  execute(database, "PRAGMA wal_autocheckpoint=1000");
}

void verifyFormat(sqlite3 *database) {
  Statement application(database, "PRAGMA application_id");
  if (!application.row() || application.integer(0) != applicationId ||
      application.row())
    fail("persistence.format.application", "file is not a Kearne project",
         Severity::Fatal);
  Statement version(database, "PRAGMA user_version");
  if (!version.row() || version.integer(0) != containerVersion || version.row())
    fail("persistence.format.version",
         "project container version is unsupported", Severity::Fatal);
}

void quickCheck(sqlite3 *database) {
  Statement check(database, "PRAGMA quick_check(1)");
  if (!check.row() || check.text(0) != "ok" || check.row())
    fail("persistence.sqlite.integrity",
         "project database failed its integrity check", Severity::Fatal);
  Statement foreignKeys(database, "PRAGMA foreign_key_check");
  if (foreignKeys.row())
    fail("persistence.sqlite.references",
         "project database contains broken references", Severity::Fatal);
}

constexpr std::string_view schema = R"sql(
PRAGMA application_id=1262829906;
PRAGMA user_version=1;
CREATE TABLE revision (
  id TEXT PRIMARY KEY,
  parent0 TEXT REFERENCES revision(id),
  parent1 TEXT REFERENCES revision(id),
  root_digest TEXT NOT NULL,
  transaction_id TEXT NOT NULL UNIQUE,
  request_id TEXT UNIQUE,
  request_digest TEXT,
  envelope BLOB NOT NULL,
  CHECK ((request_id IS NULL) = (request_digest IS NULL))
) STRICT;
CREATE INDEX revision_parent0 ON revision(parent0);
CREATE INDEX revision_parent1 ON revision(parent1);
CREATE TABLE content (
  digest TEXT PRIMARY KEY,
  byte_size INTEGER NOT NULL CHECK (byte_size >= 0),
  bytes BLOB NOT NULL,
  CHECK (length(bytes) = byte_size)
) STRICT;
CREATE TABLE project_state (
  singleton INTEGER PRIMARY KEY CHECK (singleton = 1),
  container_version INTEGER NOT NULL CHECK (container_version = 1),
  minimum_reader INTEGER NOT NULL CHECK (minimum_reader = 1),
  project_id TEXT NOT NULL,
  project_root_record TEXT NOT NULL,
  schema_set TEXT NOT NULL,
  head_revision TEXT NOT NULL REFERENCES revision(id),
  saved_revision TEXT NOT NULL REFERENCES revision(id),
  checkpoint BLOB NOT NULL,
  created_build TEXT NOT NULL,
  last_write_build TEXT NOT NULL
) STRICT;
)sql";

} // namespace

struct SqliteProjectStore::Impl {
  sqlite3 *database = nullptr;
  ProjectOpenMode mode;
  SqliteProjectStoreOptions options;
  mutable std::mutex mutex;

  ~Impl() {
    if (database)
      sqlite3_close_v2(database);
  }
};

namespace {

sqlite3 *openDatabase(const std::filesystem::path &path, ProjectOpenMode mode,
                      const SqliteProjectStoreOptions &options, bool create) {
  validateOptions(options);
  if (path.empty())
    fail("persistence.path.empty", "project path is empty");
  sqlite3 *database = nullptr;
  const int flags =
      (mode == ProjectOpenMode::ReadOnly ? SQLITE_OPEN_READONLY
                                         : SQLITE_OPEN_READWRITE) |
      (create ? SQLITE_OPEN_CREATE : 0) | SQLITE_OPEN_FULLMUTEX;
  const std::string encodedPath = pathUtf8(path);
  const int opened =
      sqlite3_open_v2(encodedPath.c_str(), &database, flags, nullptr);
  if (opened != SQLITE_OK) {
    Diagnostic error = diagnostic("persistence.sqlite.open",
                                  "project file could not be opened");
    if (database) {
      error.parameters.push_back(
          std::to_string(sqlite3_extended_errcode(database)));
      error.detail = sqlite3_errmsg(database);
      sqlite3_close_v2(database);
    }
    throw Failure{std::move(error)};
  }
  try {
    configureConnection(database, mode, options);
  } catch (...) {
    sqlite3_close_v2(database);
    throw;
  }
  return database;
}

std::optional<std::string> currentHead(sqlite3 *database) {
  Statement statement(
      database, "SELECT head_revision FROM project_state WHERE singleton=1");
  if (!statement.row())
    return std::nullopt;
  const std::string result = statement.text(0);
  if (statement.row())
    fail("persistence.project.duplicate", "project contains duplicate state",
         Severity::Fatal);
  return result;
}

bool revisionExists(sqlite3 *database, const RevisionId &revision,
                    std::span<const std::uint8_t> envelope) {
  Statement statement(database, "SELECT envelope FROM revision WHERE id=?1");
  statement.text(1, revision.toString());
  if (!statement.row())
    return false;
  const document::Bytes stored =
      statement.blob(0, std::numeric_limits<std::size_t>::max());
  if (statement.row())
    fail("persistence.revision.duplicate",
         "project contains duplicate revisions", Severity::Fatal);
  if (!std::ranges::equal(stored, envelope))
    fail("persistence.revision.collision",
         "revision identity collision or corruption was detected",
         Severity::Fatal);
  return true;
}

void insertContent(sqlite3 *database, const persistence::StoredContent &content,
                   std::size_t maximumBytes) {
  validateContent(content, maximumBytes);
  Statement insert(
      database, "INSERT INTO content(digest,byte_size,bytes) VALUES(?1,?2,?3) "
                "ON CONFLICT(digest) DO NOTHING");
  insert.text(1, content.digest.toString());
  insert.integer(2, content.bytes.size());
  insert.blob(3, content.bytes);
  insert.execute();
  Statement verify(database,
                   "SELECT byte_size,bytes FROM content WHERE digest=?1");
  verify.text(1, content.digest.toString());
  if (!verify.row() || verify.integer(0) != content.bytes.size() ||
      !std::ranges::equal(verify.blob(1, maximumBytes), content.bytes) ||
      verify.row())
    fail("persistence.content.collision",
         "content identity collision or corruption was detected",
         Severity::Fatal);
}

void requireMutationContent(sqlite3 *database,
                            const engineering::RevisionEnvelope &revision,
                            std::size_t maximumBytes) {
  for (const document::Mutation &mutation : revision.mutations) {
    const auto *put = std::get_if<document::PutContent>(&mutation);
    if (!put)
      continue;
    if (put->value.byteSize > maximumBytes)
      fail("persistence.content.size-limit",
           "source content exceeds its byte limit");
    Statement statement(database,
                        "SELECT byte_size FROM content WHERE digest=?1");
    statement.text(1, put->value.digest.toString());
    if (!statement.row() || statement.integer(0) != put->value.byteSize ||
        statement.row())
      fail("persistence.content.missing",
           "revision source content is unavailable");
  }
}

void requireSnapshotContent(sqlite3 *database,
                            const document::ProjectSnapshot &snapshot,
                            std::size_t maximumBytes) {
  for (const auto &[path, entry] : snapshot.state().content()) {
    static_cast<void>(path);
    if (entry.byteSize > maximumBytes)
      fail("persistence.content.size-limit",
           "source content exceeds its byte limit");
    Statement statement(database,
                        "SELECT byte_size FROM content WHERE digest=?1");
    statement.text(1, entry.digest.toString());
    if (!statement.row() || statement.integer(0) != entry.byteSize ||
        statement.row())
      fail("persistence.content.missing",
           "project source content is unavailable");
  }
}

void insertRevision(sqlite3 *database,
                    const persistence::DurableRevision &value,
                    std::span<const std::uint8_t> envelope) {
  Statement statement(
      database,
      "INSERT INTO revision(id,parent0,parent1,root_digest,transaction_id,"
      "request_id,request_digest,envelope) VALUES(?1,?2,?3,?4,?5,?6,?7,?8)");
  statement.text(1, value.revision.id.toString());
  statement.optionalText(
      2, value.revision.envelope.parents.empty()
             ? std::nullopt
             : std::optional{value.revision.envelope.parents[0].toString()});
  statement.optionalText(
      3, value.revision.envelope.parents.size() < 2U
             ? std::nullopt
             : std::optional{value.revision.envelope.parents[1].toString()});
  statement.text(4, value.revision.envelope.projectRootDigest.toString());
  statement.text(5, value.revision.envelope.transaction.toString());
  statement.optionalText(
      6, value.revision.envelope.request
             ? std::optional{value.revision.envelope.request->toString()}
             : std::nullopt);
  statement.optionalText(
      7, value.revision.envelope.requestDigest
             ? std::optional{value.revision.envelope.requestDigest->toString()}
             : std::nullopt);
  statement.blob(8, envelope);
  statement.execute();
}

void publish(sqlite3 *database, const SqliteProjectStoreOptions &options,
             const std::optional<RevisionId> &expectedHead,
             const persistence::DurableRevision &value,
             const EncodedRevision &encoded,
             std::optional<RecordId> genesisRoot) {
  const auto head = currentHead(database);
  if (revisionExists(database, value.revision.id, encoded.envelope)) {
    if (head && *head == value.revision.id.toString())
      return;
    fail("persistence.revision.duplicate",
         "revision already exists away from the project head");
  }
  if (text(expectedHead) != head)
    fail("persistence.head.conflict", "project head has moved");
  if (head) {
    Statement project(database,
                      "SELECT project_id FROM project_state WHERE singleton=1");
    if (!project.row() ||
        project.text(0) != value.snapshot.state().projectId().toString() ||
        project.row())
      fail("persistence.project.identity-mismatch",
           "revision targets another project");
  }
  for (const persistence::StoredContent &content : value.content)
    insertContent(database, content, options.maximumContentBytes);
  requireMutationContent(database, value.revision.envelope,
                         options.maximumContentBytes);
  requireSnapshotContent(database, value.snapshot, options.maximumContentBytes);
  insertRevision(database, value, encoded.envelope);

  if (genesisRoot) {
    Statement insert(
        database,
        "INSERT INTO project_state(singleton,container_version,minimum_reader,"
        "project_id,project_root_record,schema_set,head_revision,saved_"
        "revision,"
        "checkpoint,created_build,last_write_build) "
        "VALUES(1,1,1,?1,?2,?3,?4,?4,?5,?6,?6)");
    insert.text(1, value.snapshot.state().projectId().toString());
    insert.text(2, genesisRoot->toString());
    insert.text(3, value.snapshot.state().schemaSet().toString());
    insert.text(4, value.revision.id.toString());
    insert.blob(5, encoded.checkpoint);
    insert.text(6, options.applicationBuild);
    insert.execute();
    return;
  }
  Statement update(
      database,
      "UPDATE project_state SET schema_set=?1,head_revision=?2,checkpoint=?3,"
      "last_write_build=?4 WHERE singleton=1 AND head_revision=?5");
  update.text(1, value.snapshot.state().schemaSet().toString());
  update.text(2, value.revision.id.toString());
  update.blob(3, encoded.checkpoint);
  update.text(4, options.applicationBuild);
  update.text(5, expectedHead->toString());
  update.execute();
  if (sqlite3_changes(database) != 1)
    fail("persistence.head.conflict", "project head has moved");
}

engineering::RevisionRecord
readRevision(sqlite3 *database, const RevisionId &revision,
             const SqliteProjectStoreOptions &options) {
  Statement statement(
      database,
      "SELECT parent0,parent1,root_digest,envelope FROM revision WHERE id=?1");
  statement.text(1, revision.toString());
  if (!statement.row())
    fail("persistence.revision.not-found", "project revision is unavailable");
  const auto parent0 = statement.optionalText(0);
  const auto parent1 = statement.optionalText(1);
  const auto root = statement.text(2);
  const auto envelope = statement.blob(3, options.maximumCheckpointBytes);
  if (statement.row())
    fail("persistence.revision.duplicate",
         "project contains duplicate revisions", Severity::Fatal);
  engineering::RevisionDecodeLimits limits;
  limits.maximumEncodedBytes = options.maximumCheckpointBytes;
  auto decoded = require(engineering::decodeRevisionRecord(envelope, limits));
  if (decoded.id != revision ||
      decoded.envelope.projectRootDigest.toString() != root ||
      text(decoded.envelope.parents.empty()
               ? std::optional<RevisionId>{}
               : std::optional{decoded.envelope.parents[0]}) != parent0 ||
      text(decoded.envelope.parents.size() < 2U
               ? std::optional<RevisionId>{}
               : std::optional{decoded.envelope.parents[1]}) != parent1)
    fail("persistence.revision.metadata-mismatch",
         "revision index and canonical bytes disagree", Severity::Fatal);
  return decoded;
}

std::shared_ptr<const document::Bytes> loadContent(sqlite3 *database,
                                                   const ContentDigest &digest,
                                                   std::size_t maximumBytes) {
  Statement statement(database,
                      "SELECT byte_size,bytes FROM content WHERE digest=?1");
  statement.text(1, digest.toString());
  if (!statement.row())
    fail("persistence.content.not-found", "project content is unavailable");
  const std::uint64_t declaredSize = statement.integer(0);
  document::Bytes bytes = statement.blob(1, maximumBytes);
  if (statement.row() || declaredSize != bytes.size())
    fail("persistence.content.size-mismatch",
         "project content length is invalid", Severity::Fatal);
  const ContentDigest actual = require(document::contentDigest(bytes));
  if (actual != digest)
    fail("persistence.content.digest-mismatch",
         "project content failed its integrity check", Severity::Fatal);
  return std::make_shared<const document::Bytes>(std::move(bytes));
}

persistence::RecoveredProject
recoverHead(sqlite3 *database, const SqliteProjectStoreOptions &options) {
  quickCheck(database);
  Statement statement(
      database,
      "SELECT container_version,minimum_reader,project_id,project_root_record,"
      "schema_set,head_revision,saved_revision,checkpoint FROM project_state "
      "WHERE singleton=1");
  if (!statement.row())
    fail("persistence.project.missing", "project state is unavailable",
         Severity::Fatal);
  if (statement.integer(0) != containerVersion ||
      statement.integer(1) != containerVersion)
    fail("persistence.format.version",
         "project container version is unsupported", Severity::Fatal);
  const ProjectId project = parseId<ProjectId>(statement.text(2), "project-id");
  const RecordId rootRecord =
      parseId<RecordId>(statement.text(3), "project-root-record");
  const SchemaSetDigest schemaSet =
      parseDigest<SchemaSetDigest>(statement.text(4), "schema-set");
  const RevisionId head =
      parseDigest<RevisionId>(statement.text(5), "head-revision");
  const RevisionId saved =
      parseDigest<RevisionId>(statement.text(6), "saved-revision");
  const document::Bytes checkpoint =
      statement.blob(7, options.maximumCheckpointBytes);
  if (statement.row())
    fail("persistence.project.duplicate", "project contains duplicate state",
         Severity::Fatal);
  document::ProjectCheckpointLimits limits;
  limits.maximumEncodedBytes = options.maximumCheckpointBytes;
  auto snapshot =
      require(document::decodeProjectCheckpoint(checkpoint, limits));
  auto revision = readRevision(database, head, options);
  if (snapshot.state().projectId() != project ||
      snapshot.state().schemaSet() != schemaSet ||
      snapshot.revisionId() != head ||
      snapshot.state().rootDigest() != revision.envelope.projectRootDigest ||
      !snapshot.state().record(rootRecord))
    fail("persistence.project.checkpoint-mismatch",
         "project metadata and checkpoint disagree", Severity::Fatal);
  for (const auto &[path, entry] : snapshot.state().content()) {
    static_cast<void>(path);
    const auto bytes =
        loadContent(database, entry.digest, options.maximumContentBytes);
    if (bytes->size() != entry.byteSize)
      fail("persistence.content.size-mismatch",
           "project source length disagrees with its checkpoint",
           Severity::Fatal);
  }
  return {rootRecord, std::move(revision), std::move(snapshot), saved};
}

} // namespace

SqliteProjectStore::SqliteProjectStore(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}
SqliteProjectStore::~SqliteProjectStore() = default;
SqliteProjectStore::SqliteProjectStore(SqliteProjectStore &&) noexcept =
    default;
SqliteProjectStore &
SqliteProjectStore::operator=(SqliteProjectStore &&) noexcept = default;

Result<std::unique_ptr<SqliteProjectStore>>
SqliteProjectStore::create(const std::filesystem::path &path,
                           RecordId projectRootRecord,
                           const persistence::DurableRevision &genesis,
                           SqliteProjectStoreOptions options) {
  return guarded<std::unique_ptr<SqliteProjectStore>>([&] {
    auto impl = std::make_unique<Impl>();
    impl->mode = ProjectOpenMode::ReadWrite;
    impl->options = std::move(options);
    impl->database = openDatabase(path, impl->mode, impl->options, true);
    Statement existing(impl->database,
                       "SELECT count(*) FROM sqlite_schema WHERE type='table'");
    if (!existing.row() || existing.integer(0) != 0 || existing.row())
      fail("persistence.create.exists",
           "project file already contains a database");
    configureWriter(impl->database);
    const EncodedRevision encoded =
        encodeRevision(genesis, impl->options, std::nullopt);
    if (!genesis.snapshot.state().record(projectRootRecord))
      fail("persistence.project.missing-root",
           "genesis checkpoint does not contain its project root");
    Transaction transaction{impl->database};
    execute(impl->database, schema);
    publish(impl->database, impl->options, std::nullopt, genesis, encoded,
            projectRootRecord);
    transaction.commit();
    verifyFormat(impl->database);
    static_cast<void>(recoverHead(impl->database, impl->options));
    return std::unique_ptr<SqliteProjectStore>(
        new SqliteProjectStore(std::move(impl)));
  });
}

Result<std::unique_ptr<SqliteProjectStore>>
SqliteProjectStore::open(const std::filesystem::path &path,
                         ProjectOpenMode mode,
                         SqliteProjectStoreOptions options) {
  return guarded<std::unique_ptr<SqliteProjectStore>>([&] {
    auto impl = std::make_unique<Impl>();
    impl->mode = mode;
    impl->options = std::move(options);
    impl->database = openDatabase(path, mode, impl->options, false);
    verifyFormat(impl->database);
    if (mode == ProjectOpenMode::ReadWrite)
      configureWriter(impl->database);
    static_cast<void>(recoverHead(impl->database, impl->options));
    return std::unique_ptr<SqliteProjectStore>(
        new SqliteProjectStore(std::move(impl)));
  });
}

Result<void>
SqliteProjectStore::commit(std::optional<RevisionId> expectedHead,
                           const persistence::DurableRevision &revision) {
  return guarded<void>([&] {
    if (impl_->mode == ProjectOpenMode::ReadOnly)
      fail("persistence.read-only", "project is open read-only");
    const EncodedRevision encoded =
        encodeRevision(revision, impl_->options, expectedHead);
    std::scoped_lock lock{impl_->mutex};
    Transaction transaction{impl_->database};
    publish(impl_->database, impl_->options, expectedHead, revision, encoded,
            std::nullopt);
    transaction.commit();
  });
}

Result<persistence::RecoveredProject> SqliteProjectStore::loadHead() const {
  return guarded<persistence::RecoveredProject>([&] {
    std::scoped_lock lock{impl_->mutex};
    return recoverHead(impl_->database, impl_->options);
  });
}

Result<engineering::RevisionRecord>
SqliteProjectStore::loadRevision(const RevisionId &revision) const {
  return guarded<engineering::RevisionRecord>([&] {
    std::scoped_lock lock{impl_->mutex};
    return readRevision(impl_->database, revision, impl_->options);
  });
}

Result<std::vector<RevisionId>>
SqliteProjectStore::children(const RevisionId &revision) const {
  return guarded<std::vector<RevisionId>>([&] {
    std::scoped_lock lock{impl_->mutex};
    Statement statement(
        impl_->database,
        "SELECT id FROM revision WHERE parent0=?1 OR parent1=?1 ORDER BY id");
    statement.text(1, revision.toString());
    std::vector<RevisionId> result;
    while (statement.row())
      result.push_back(
          parseDigest<RevisionId>(statement.text(0), "child-revision"));
    return result;
  });
}

Result<std::shared_ptr<const document::Bytes>>
SqliteProjectStore::content(const ContentDigest &digest) const {
  return guarded<std::shared_ptr<const document::Bytes>>([&] {
    std::scoped_lock lock{impl_->mutex};
    return loadContent(impl_->database, digest,
                       impl_->options.maximumContentBytes);
  });
}

Result<void>
SqliteProjectStore::moveHead(const RevisionId &expectedHead,
                             const document::ProjectSnapshot &target) {
  return guarded<void>([&] {
    if (impl_->mode == ProjectOpenMode::ReadOnly)
      fail("persistence.read-only", "project is open read-only");
    document::ProjectCheckpointLimits limits;
    limits.maximumEncodedBytes = impl_->options.maximumCheckpointBytes;
    const document::Bytes checkpoint =
        require(document::canonicalBytes(target, limits));
    std::scoped_lock lock{impl_->mutex};
    Transaction transaction{impl_->database};
    const auto head = currentHead(impl_->database);
    if (!head || *head != expectedHead.toString())
      fail("persistence.head.conflict", "project head has moved");
    const auto revision =
        readRevision(impl_->database, target.revisionId(), impl_->options);
    if (revision.envelope.projectRootDigest != target.state().rootDigest() ||
        revision.envelope.schemaSet != target.state().schemaSet())
      fail("persistence.project.checkpoint-mismatch",
           "target checkpoint does not match its revision");
    Statement update(
        impl_->database,
        "UPDATE project_state SET schema_set=?1,head_revision=?2,checkpoint=?3,"
        "last_write_build=?4 WHERE singleton=1 AND head_revision=?5");
    update.text(1, target.state().schemaSet().toString());
    update.text(2, target.revisionId().toString());
    update.blob(3, checkpoint);
    update.text(4, impl_->options.applicationBuild);
    update.text(5, expectedHead.toString());
    update.execute();
    if (sqlite3_changes(impl_->database) != 1)
      fail("persistence.head.conflict", "project head has moved");
    transaction.commit();
  });
}

Result<void> SqliteProjectStore::savePoint(const RevisionId &expectedHead) {
  return guarded<void>([&] {
    if (impl_->mode == ProjectOpenMode::ReadOnly)
      fail("persistence.read-only", "project is open read-only");
    std::scoped_lock lock{impl_->mutex};
    Transaction transaction{impl_->database};
    Statement update(
        impl_->database,
        "UPDATE project_state SET saved_revision=head_revision,"
        "last_write_build=?1 WHERE singleton=1 AND head_revision=?2");
    update.text(1, impl_->options.applicationBuild);
    update.text(2, expectedHead.toString());
    update.execute();
    if (sqlite3_changes(impl_->database) != 1)
      fail("persistence.head.conflict", "project head has moved");
    transaction.commit();
    int logFrames = 0;
    int checkpointedFrames = 0;
    const int result = sqlite3_wal_checkpoint_v2(
        impl_->database, nullptr, SQLITE_CHECKPOINT_FULL, &logFrames,
        &checkpointedFrames);
    if (result != SQLITE_OK || checkpointedFrames != logFrames)
      failSqlite(impl_->database, "persistence.sqlite.checkpoint",
                 "project save point could not checkpoint its journal");
  });
}

} // namespace kearne::adapters
