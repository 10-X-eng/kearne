#pragma once

#include <kearne/base/value.hpp>

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace kearne::evaluation {

struct SubscriberIdTag;
struct ProjectionIdTag;
using SubscriberId = TypedId<SubscriberIdTag>;
using ProjectionId = TypedId<ProjectionIdTag>;

struct LogicalTime final {
  std::uint64_t ticks = 0;
  auto operator<=>(const LogicalTime &) const = default;
};

class Generation final {
public:
  [[nodiscard]] static Result<Generation> create(std::uint64_t value);
  [[nodiscard]] std::uint64_t value() const { return value_; }
  auto operator<=>(const Generation &) const = default;

private:
  explicit Generation(std::uint64_t value) : value_(value) {}
  std::uint64_t value_;
};

enum class Priority : std::uint8_t {
  InteractivePreview,
  VisibleResult,
  UserRequested,
  Normal,
  Background,
  Idle,
  Count,
};

enum class ResourceClass : std::uint8_t {
  General,
  Geometry,
  Python,
  Solver,
  Import,
  Count,
};

struct ResourceDemand final {
  std::uint16_t cpuSlots = 1;
  std::uint64_t memoryBytes = 0;
  ResourceClass resourceClass = ResourceClass::General;
  bool operator==(const ResourceDemand &) const = default;
};

struct ResourceLimit final {
  std::size_t queuedJobs = 0;
  std::size_t concurrentJobs = 0;
  std::uint32_t cpuSlots = 0;
  std::uint64_t memoryBytes = 0;
  bool operator==(const ResourceLimit &) const = default;
};

struct SchedulerLimits final {
  std::size_t jobs = 0;
  std::size_t subscribers = 0;
  std::size_t projections = 0;
  std::size_t availableResults = 0;
  std::size_t dependenciesPerJob = 0;
  std::size_t dependencyEdges = 0;
  ResourceLimit global;
  std::array<ResourceLimit, static_cast<std::size_t>(ResourceClass::Count)>
      classes{};
  std::uint64_t agingIntervalTicks = 1;
  std::uint8_t maximumAgePromotions =
      static_cast<std::uint8_t>(Priority::Count) - 1U;
  std::size_t fitSearchLimit = 64;
  std::size_t maximumFitBypasses = 8;
};

enum class Retention : std::uint8_t {
  CancelWhenUnobserved,
  CompleteForCache,
  Count,
};

struct Submit final {
  JobId proposedJob;
  EvaluationKey key;
  SubscriberId subscriber;
  ProjectionId projection;
  Generation generation;
  Priority priority = Priority::Normal;
  ResourceDemand demand;
  std::vector<EvaluationKey> dependencies;
  Retention retention = Retention::CancelWhenUnobserved;
};

struct CancelSubscription final {
  SubscriberId subscriber;
};

struct AdvanceTime final {
  LogicalTime now;
};

struct ExecutionStarted final {
  JobId job;
  WorkerInstanceId worker;
};

struct Progress final {
  JobId job;
  WorkerInstanceId worker;
  std::uint64_t sequence = 0;
  std::uint32_t stage = 0;
  std::uint64_t completedUnits = 0;
  std::optional<std::uint64_t> totalUnits;
};

enum class TerminalStatus : std::uint8_t {
  Succeeded,
  Failed,
  Cancelled,
  WorkerLost,
  Count,
};

struct ExecutionFinished final {
  JobId job;
  WorkerInstanceId worker;
  std::uint64_t sequence = 0;
  TerminalStatus status = TerminalStatus::Failed;
};

struct RetireJob final {
  JobId job;
  EvaluationKey key;
};

// Emitted by the coordinator only after a successful immutable artifact is
// externally reachable. A succeeded job cannot retire before this handoff;
// retirement then reclaims its subscribers while dependency readiness remains
// in the bounded availability set.
struct ArtifactAvailable final {
  EvaluationKey key;
};

struct ForgetArtifactAvailability final {
  EvaluationKey key;
};

struct RetireProjection final {
  ProjectionId projection;
  Generation observedGeneration;
  EvaluationKey observedKey;
};

using Event =
    std::variant<Submit, CancelSubscription, AdvanceTime, ExecutionStarted,
                 Progress, ExecutionFinished, RetireJob, ArtifactAvailable,
                 ForgetArtifactAvailability, RetireProjection>;

enum class EventStatus : std::uint8_t {
  Applied,
  Idempotent,
  IgnoredStale,
  RejectedInvalid,
  RejectedCapacity,
  RejectedIdentity,
};

enum class JobState : std::uint8_t {
  WaitingForInputs,
  Queued,
  Running,
  CancellationRequested,
  Succeeded,
  Failed,
  Cancelled,
  WorkerLost,
  Superseded,
  DependencyFailed,
};

enum class AdmissionRejection : std::uint8_t {
  InvalidLimits,
  InvalidDemand,
  TooManyJobs,
  TooManySubscribers,
  TooManyProjections,
  TooManyDependencies,
  TooManyDependencyEdges,
  ClassQueueFull,
  IdentityCollision,
  GenerationConflict,
  DependencyCycle,
  CancellationInFlight,
  RetirementRequired,
  OrdinalExhausted,
};

struct Admitted final {
  JobId job;
  EvaluationKey key;
  SubscriberId subscriber;
  bool sharedExecution = false;
};

struct AdmissionRejected final {
  SubscriberId subscriber;
  AdmissionRejection reason = AdmissionRejection::InvalidLimits;
};

struct Dispatch final {
  JobId job;
  EvaluationKey key;
  ResourceDemand demand;
};

struct CancelExecution final {
  JobId job;
  std::optional<WorkerInstanceId> worker;
};

struct ForwardProgress final {
  JobId job;
  EvaluationKey key;
  SubscriberId subscriber;
  ProjectionId projection;
  Generation generation;
  std::uint64_t sequence = 0;
  std::uint32_t stage = 0;
  std::uint64_t completedUnits = 0;
  std::optional<std::uint64_t> totalUnits;
};

enum class PublicationDisposition : std::uint8_t {
  Current,
  StaleCacheOnly,
  UnobservedCacheOnly,
  StaleNoArtifact,
  UnobservedNoArtifact,
};

struct PublicationDecision final {
  JobId job;
  EvaluationKey key;
  SubscriberId subscriber;
  ProjectionId projection;
  Generation generation;
  TerminalStatus terminal = TerminalStatus::Failed;
  PublicationDisposition disposition =
      PublicationDisposition::UnobservedNoArtifact;
};

struct JobTerminal final {
  JobId job;
  EvaluationKey key;
  JobState state = JobState::Failed;
};

using Action =
    std::variant<Admitted, AdmissionRejected, Dispatch, CancelExecution,
                 ForwardProgress, PublicationDecision, JobTerminal>;

struct StepResult final {
  EventStatus status = EventStatus::Applied;
  std::vector<Action> actions;
};

struct SchedulerStats final {
  std::size_t jobs = 0;
  std::size_t waiting = 0;
  std::size_t queued = 0;
  std::size_t running = 0;
  std::size_t terminal = 0;
  std::size_t subscribers = 0;
  std::size_t activeSubscribers = 0;
  std::size_t projections = 0;
  std::size_t availableResults = 0;
  std::size_t dependencyEdges = 0;
  std::uint32_t usedCpuSlots = 0;
  std::uint64_t usedMemoryBytes = 0;
  bool resourceDrainReserved = false;
  bool operator==(const SchedulerStats &) const = default;
};

class Scheduler final {
public:
  explicit Scheduler(SchedulerLimits limits);
  Scheduler(Scheduler &&) noexcept;
  Scheduler &operator=(Scheduler &&) noexcept;
  ~Scheduler();

  Scheduler(const Scheduler &) = delete;
  Scheduler &operator=(const Scheduler &) = delete;

  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] StepResult apply(const Event &event);
  [[nodiscard]] SchedulerStats stats() const;
  [[nodiscard]] std::optional<JobState> state(const EvaluationKey &key) const;
  [[nodiscard]] bool isCurrent(ProjectionId projection, Generation generation,
                               const EvaluationKey &key) const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace kearne::evaluation
