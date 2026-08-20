#include <kearne/evaluation/scheduler.hpp>

#include <algorithm>
#include <array>
#include <limits>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace kearne::evaluation {
namespace {

constexpr std::size_t priorityCount = static_cast<std::size_t>(Priority::Count);
constexpr std::size_t resourceClassCount =
    static_cast<std::size_t>(ResourceClass::Count);

using EvaluationKeyHash = TypedDigestHash<EvaluationKeyTag>;

template <typename Value>
bool preservesCapacity(Value used, Value limit, Value consumed,
                       Value required) {
  if (required > limit || used > limit - required)
    return consumed == 0;
  return consumed <= limit - required - used;
}

std::size_t index(ResourceClass value) {
  return static_cast<std::size_t>(value);
}

std::size_t index(Priority value) { return static_cast<std::size_t>(value); }

bool validEnum(Priority value) { return index(value) < priorityCount; }

bool validEnum(Retention value) {
  return static_cast<std::size_t>(value) <
         static_cast<std::size_t>(Retention::Count);
}

bool validEnum(TerminalStatus value) {
  return static_cast<std::size_t>(value) <
         static_cast<std::size_t>(TerminalStatus::Count);
}

bool terminal(JobState state) {
  return state == JobState::Succeeded || state == JobState::Failed ||
         state == JobState::Cancelled || state == JobState::WorkerLost ||
         state == JobState::Superseded || state == JobState::DependencyFailed;
}

JobState terminalState(TerminalStatus status) {
  switch (status) {
  case TerminalStatus::Succeeded:
    return JobState::Succeeded;
  case TerminalStatus::Failed:
    return JobState::Failed;
  case TerminalStatus::Cancelled:
    return JobState::Cancelled;
  case TerminalStatus::WorkerLost:
    return JobState::WorkerLost;
  case TerminalStatus::Count:
    break;
  }
  return JobState::Failed;
}

TerminalStatus terminalStatus(JobState state) {
  switch (state) {
  case JobState::Succeeded:
    return TerminalStatus::Succeeded;
  case JobState::Cancelled:
  case JobState::Superseded:
    return TerminalStatus::Cancelled;
  case JobState::WorkerLost:
    return TerminalStatus::WorkerLost;
  default:
    return TerminalStatus::Failed;
  }
}

struct QueueToken final {
  std::uint64_t admittedAt = 0;
  std::uint64_t ordinal = 0;
  JobId job;
};

struct QueueTokenLess final {
  bool operator()(const QueueToken &left,
                  const QueueToken &right) const noexcept {
    if (left.admittedAt != right.admittedAt)
      return left.admittedAt < right.admittedAt;
    if (left.ordinal != right.ordinal)
      return left.ordinal < right.ordinal;
    return left.job < right.job;
  }
};

struct Subscriber final {
  JobId job;
  EvaluationKey key;
  ProjectionId projection;
  Generation generation;
  Priority priority;
  bool active = true;
};

struct Projection final {
  Generation generation;
  EvaluationKey key;
  std::vector<SubscriberId> subscribers;
};

struct Job final {
  Job(JobId jobId, EvaluationKey evaluationKey, ResourceDemand resources,
      std::vector<EvaluationKey> inputs)
      : id(std::move(jobId)), key(std::move(evaluationKey)), demand(resources),
        dependencies(std::move(inputs)) {}

  JobId id;
  EvaluationKey key;
  ResourceDemand demand;
  std::vector<EvaluationKey> dependencies;
  std::unordered_set<EvaluationKey, EvaluationKeyHash> waitingOn;
  std::vector<SubscriberId> subscribers;
  std::array<std::size_t, priorityCount> activePriorities{};
  JobState state = JobState::WaitingForInputs;
  bool keepForCache = false;
  std::optional<WorkerInstanceId> worker;
  std::uint64_t lastSequence = 0;
  std::uint64_t admittedAt = 0;
  std::uint64_t ordinal = 0;
  Priority queuedPriority = Priority::Normal;
  bool inReadyQueue = false;
  std::size_t fitBypasses = 0;
};

struct Usage final {
  std::size_t pending = 0;
  std::size_t running = 0;
  std::uint32_t cpuSlots = 0;
  std::uint64_t memoryBytes = 0;
};

using ReadyQueue = std::set<QueueToken, QueueTokenLess>;

} // namespace

struct Scheduler::Impl final {
  explicit Impl(SchedulerLimits configured) : limits(std::move(configured)) {
    validLimits = validateLimits();
    if (validLimits) {
      constexpr std::size_t initialReserveLimit = 131'072;
      jobsByKey.reserve(std::min(limits.jobs, initialReserveLimit));
      jobsById.reserve(std::min(limits.jobs, initialReserveLimit));
      subscribers.reserve(std::min(limits.subscribers, initialReserveLimit));
      projections.reserve(std::min(limits.projections, initialReserveLimit));
      availableResults.reserve(
          std::min(limits.availableResults, initialReserveLimit));
      waiters.reserve(std::min(limits.jobs, initialReserveLimit));
    }
  }

  SchedulerLimits limits;
  bool validLimits = false;
  LogicalTime now{};
  std::uint64_t nextOrdinal = 0;
  std::unordered_map<EvaluationKey, Job, EvaluationKeyHash> jobsByKey;
  std::unordered_map<JobId, EvaluationKey, TypedIdHash<JobIdTag>> jobsById;
  std::unordered_map<SubscriberId, Subscriber, TypedIdHash<SubscriberIdTag>>
      subscribers;
  std::unordered_map<ProjectionId, Projection, TypedIdHash<ProjectionIdTag>>
      projections;
  std::unordered_set<EvaluationKey, EvaluationKeyHash> availableResults;
  std::unordered_map<EvaluationKey,
                     std::unordered_set<JobId, TypedIdHash<JobIdTag>>,
                     EvaluationKeyHash>
      waiters;
  std::array<std::array<ReadyQueue, priorityCount>, resourceClassCount> ready;
  Usage globalUsage;
  std::array<Usage, resourceClassCount> classUsage{};
  std::size_t dependencyEdges = 0;
  std::optional<JobId> reservedJob;

  bool validateLimits() const {
    if (limits.jobs == 0 || limits.subscribers == 0 ||
        limits.projections == 0 || limits.availableResults == 0 ||
        limits.global.queuedJobs == 0 || limits.global.concurrentJobs == 0 ||
        limits.global.cpuSlots == 0 || limits.agingIntervalTicks == 0 ||
        limits.fitSearchLimit == 0 || limits.maximumFitBypasses == 0 ||
        limits.maximumAgePromotions >= priorityCount)
      return false;
    for (const ResourceLimit &resource : limits.classes) {
      if (resource.concurrentJobs > 0 && resource.cpuSlots == 0)
        return false;
    }
    return true;
  }

  Job *findJob(JobId id) {
    const auto indexed = jobsById.find(id);
    if (indexed == jobsById.end())
      return nullptr;
    const auto found = jobsByKey.find(indexed->second);
    return found == jobsByKey.end() ? nullptr : &found->second;
  }

  const Job *findJob(JobId id) const {
    const auto indexed = jobsById.find(id);
    if (indexed == jobsById.end())
      return nullptr;
    const auto found = jobsByKey.find(indexed->second);
    return found == jobsByKey.end() ? nullptr : &found->second;
  }

  Priority activePriority(const Job &job) const {
    for (std::size_t candidate = 0; candidate < priorityCount; ++candidate) {
      if (job.activePriorities[candidate] != 0)
        return static_cast<Priority>(candidate);
    }
    return job.queuedPriority;
  }

  void removeReady(Job &job) {
    if (!job.inReadyQueue)
      return;
    ready[index(job.demand.resourceClass)][index(job.queuedPriority)].erase(
        QueueToken{job.admittedAt, job.ordinal, job.id});
    job.inReadyQueue = false;
  }

  void enqueueReady(Job &job) {
    removeReady(job);
    job.queuedPriority = activePriority(job);
    ready[index(job.demand.resourceClass)][index(job.queuedPriority)].insert(
        QueueToken{job.admittedAt, job.ordinal, job.id});
    job.inReadyQueue = true;
    job.state = JobState::Queued;
  }

  void refreshPriority(Job &job) {
    if (job.state == JobState::Queued)
      enqueueReady(job);
  }

  bool fits(const Job &job) const {
    const std::size_t resource = index(job.demand.resourceClass);
    const ResourceLimit &classLimit = limits.classes[resource];
    const Usage &used = classUsage[resource];
    return globalUsage.running < limits.global.concurrentJobs &&
           classUsage[resource].running < classLimit.concurrentJobs &&
           globalUsage.cpuSlots <=
               limits.global.cpuSlots - job.demand.cpuSlots &&
           used.cpuSlots <= classLimit.cpuSlots - job.demand.cpuSlots &&
           globalUsage.memoryBytes <=
               limits.global.memoryBytes - job.demand.memoryBytes &&
           used.memoryBytes <= classLimit.memoryBytes - job.demand.memoryBytes;
  }

  std::size_t effectivePriority(const Job &job, Priority priority) const {
    const std::uint64_t waited = now.ticks - job.admittedAt;
    const std::uint64_t promotions = std::min<std::uint64_t>(
        waited / limits.agingIntervalTicks, limits.maximumAgePromotions);
    const std::size_t base = index(priority);
    return promotions >= base ? 0 : base - static_cast<std::size_t>(promotions);
  }

  bool preferred(const Job &candidate, std::size_t candidatePriority,
                 const Job &selected, std::size_t selectedPriority) const {
    return candidatePriority < selectedPriority ||
           (candidatePriority == selectedPriority &&
            (candidate.admittedAt < selected.admittedAt ||
             (candidate.admittedAt == selected.admittedAt &&
              (candidate.ordinal < selected.ordinal ||
               (candidate.ordinal == selected.ordinal &&
                candidate.id < selected.id)))));
  }

  bool preservesReservedHeadroom(const Job &candidate,
                                 const Job &reserved) const {
    if (!preservesCapacity(globalUsage.running, limits.global.concurrentJobs,
                           std::size_t{1}, std::size_t{1}) ||
        !preservesCapacity(
            globalUsage.cpuSlots, limits.global.cpuSlots,
            static_cast<std::uint32_t>(candidate.demand.cpuSlots),
            static_cast<std::uint32_t>(reserved.demand.cpuSlots)) ||
        !preservesCapacity(globalUsage.memoryBytes, limits.global.memoryBytes,
                           candidate.demand.memoryBytes,
                           reserved.demand.memoryBytes))
      return false;
    if (candidate.demand.resourceClass != reserved.demand.resourceClass)
      return true;
    const std::size_t resource = index(reserved.demand.resourceClass);
    const Usage &used = classUsage[resource];
    const ResourceLimit &limit = limits.classes[resource];
    return preservesCapacity(used.running, limit.concurrentJobs, std::size_t{1},
                             std::size_t{1}) &&
           preservesCapacity(
               used.cpuSlots, limit.cpuSlots,
               static_cast<std::uint32_t>(candidate.demand.cpuSlots),
               static_cast<std::uint32_t>(reserved.demand.cpuSlots)) &&
           preservesCapacity(used.memoryBytes, limit.memoryBytes,
                             candidate.demand.memoryBytes,
                             reserved.demand.memoryBytes);
  }

  Job *selectRunnable(const Job *reserved) {
    if (globalUsage.running >= limits.global.concurrentJobs)
      return nullptr;
    Job *selected = nullptr;
    std::size_t selectedEffective = std::numeric_limits<std::size_t>::max();
    for (std::size_t resource = 0; resource < resourceClassCount; ++resource) {
      for (std::size_t priority = 0; priority < priorityCount; ++priority) {
        ReadyQueue &queue = ready[resource][priority];
        if (queue.empty())
          continue;
        Job *candidate = nullptr;
        std::size_t inspected = 0;
        for (auto queued = queue.begin();
             queued != queue.end() && inspected < limits.fitSearchLimit;
             ++queued, ++inspected) {
          Job *possible = findJob(queued->job);
          if (possible && possible->state == JobState::Queued &&
              possible->inReadyQueue && fits(*possible) &&
              (!reserved || preservesReservedHeadroom(*possible, *reserved))) {
            candidate = possible;
            break;
          }
        }
        if (!candidate)
          continue;
        const std::size_t effective =
            effectivePriority(*candidate, static_cast<Priority>(priority));
        if (!selected ||
            preferred(*candidate, effective, *selected, selectedEffective)) {
          selected = candidate;
          selectedEffective = effective;
        }
      }
    }
    return selected;
  }

  Job *nextRunnable() {
    Job *reserved = nullptr;
    if (reservedJob) {
      reserved = findJob(*reservedJob);
      if (!reserved || reserved->state != JobState::Queued) {
        reservedJob.reset();
        reserved = nullptr;
      } else if (fits(*reserved)) {
        return reserved;
      }
    }
    return selectRunnable(reserved);
  }

  void recordFitBypasses(const Job &selected) {
    const std::size_t selectedPriority =
        effectivePriority(selected, selected.queuedPriority);
    Job *reservation = nullptr;
    std::size_t reservationPriority = std::numeric_limits<std::size_t>::max();
    for (std::size_t resource = 0; resource < resourceClassCount; ++resource) {
      for (std::size_t priority = 0; priority < priorityCount; ++priority) {
        std::size_t inspected = 0;
        for (auto queued = ready[resource][priority].begin();
             queued != ready[resource][priority].end() &&
             inspected < limits.fitSearchLimit;
             ++queued, ++inspected) {
          Job *bypassed = findJob(queued->job);
          if (!bypassed || bypassed == &selected ||
              bypassed->state != JobState::Queued)
            continue;
          if (fits(*bypassed))
            break;
          const std::size_t effective =
              effectivePriority(*bypassed, static_cast<Priority>(priority));
          if (!preferred(*bypassed, effective, selected, selectedPriority))
            continue;
          if (bypassed->fitBypasses < limits.maximumFitBypasses)
            ++bypassed->fitBypasses;
          if (bypassed->fitBypasses == limits.maximumFitBypasses &&
              (!reservation || preferred(*bypassed, effective, *reservation,
                                         reservationPriority))) {
            reservation = bypassed;
            reservationPriority = effective;
          }
        }
      }
    }
    if (reservation)
      reservedJob = reservation->id;
  }

  void schedule(std::vector<Action> &actions) {
    while (Job *job = nextRunnable()) {
      const bool wasReserved = reservedJob && *reservedJob == job->id;
      if (wasReserved)
        reservedJob.reset();
      else if (!reservedJob)
        recordFitBypasses(*job);
      removeReady(*job);
      const std::size_t resource = index(job->demand.resourceClass);
      --globalUsage.pending;
      --classUsage[resource].pending;
      ++globalUsage.running;
      ++classUsage[resource].running;
      globalUsage.cpuSlots += job->demand.cpuSlots;
      classUsage[resource].cpuSlots += job->demand.cpuSlots;
      globalUsage.memoryBytes += job->demand.memoryBytes;
      classUsage[resource].memoryBytes += job->demand.memoryBytes;
      job->state = JobState::Running;
      actions.emplace_back(Dispatch{job->id, job->key, job->demand});
    }
  }

  void release(Job &job) {
    const std::size_t resource = index(job.demand.resourceClass);
    --globalUsage.running;
    --classUsage[resource].running;
    globalUsage.cpuSlots -= job.demand.cpuSlots;
    classUsage[resource].cpuSlots -= job.demand.cpuSlots;
    globalUsage.memoryBytes -= job.demand.memoryBytes;
    classUsage[resource].memoryBytes -= job.demand.memoryBytes;
  }

  bool current(const Subscriber &subscriber) const {
    const auto found = projections.find(subscriber.projection);
    return subscriber.active && found != projections.end() &&
           found->second.generation == subscriber.generation &&
           found->second.key == subscriber.key;
  }

  bool stale(const Subscriber &subscriber) const {
    const auto found = projections.find(subscriber.projection);
    return found != projections.end() &&
           (found->second.generation != subscriber.generation ||
            found->second.key != subscriber.key);
  }

  PublicationDisposition publicationDisposition(const Subscriber &subscriber,
                                                TerminalStatus status) const {
    if (current(subscriber))
      return PublicationDisposition::Current;
    if (status == TerminalStatus::Succeeded)
      return stale(subscriber) ? PublicationDisposition::StaleCacheOnly
                               : PublicationDisposition::UnobservedCacheOnly;
    return stale(subscriber) ? PublicationDisposition::StaleNoArtifact
                             : PublicationDisposition::UnobservedNoArtifact;
  }

  void publishDecisions(Job &job, TerminalStatus status,
                        std::vector<Action> &actions) {
    for (const SubscriberId &id : job.subscribers) {
      auto found = subscribers.find(id);
      if (found == subscribers.end())
        continue;
      Subscriber &subscriber = found->second;
      actions.emplace_back(PublicationDecision{
          job.id, job.key, id, subscriber.projection, subscriber.generation,
          status, publicationDisposition(subscriber, status)});
      subscriber.active = false;
    }
    job.activePriorities.fill(0);
  }

  void detachPendingDependencies(Job &job) {
    for (const EvaluationKey &dependency : job.waitingOn) {
      const auto found = waiters.find(dependency);
      if (found != waiters.end()) {
        found->second.erase(job.id);
        if (found->second.empty())
          waiters.erase(found);
      }
      --dependencyEdges;
    }
    job.waitingOn.clear();
  }

  std::vector<JobId> takeDependents(const EvaluationKey &key) {
    const auto waiting = waiters.find(key);
    if (waiting == waiters.end())
      return {};
    std::vector<JobId> dependents(waiting->second.begin(),
                                  waiting->second.end());
    waiters.erase(waiting);
    std::ranges::sort(
        dependents, [this](const JobId &left, const JobId &right) {
          const Job *leftJob = findJob(left);
          const Job *rightJob = findJob(right);
          if (leftJob && rightJob && leftJob->ordinal != rightJob->ordinal)
            return leftJob->ordinal < rightJob->ordinal;
          return left < right;
        });
    return dependents;
  }

  void failDependents(const EvaluationKey &key, std::vector<Action> &actions) {
    std::vector<EvaluationKey> failed{key};
    failed.reserve(std::min<std::size_t>(limits.jobs, 256));
    std::size_t nextFailure = 0;
    while (nextFailure < failed.size()) {
      const EvaluationKey failedKey = failed[nextFailure++];
      for (const JobId &dependentId : takeDependents(failedKey)) {
        Job *dependent = findJob(dependentId);
        if (!dependent || terminal(dependent->state) ||
            dependent->waitingOn.erase(failedKey) == 0)
          continue;
        --dependencyEdges;
        if (dependent->state == JobState::Running ||
            dependent->state == JobState::CancellationRequested)
          continue;
        removeReady(*dependent);
        --globalUsage.pending;
        --classUsage[index(dependent->demand.resourceClass)].pending;
        detachPendingDependencies(*dependent);
        dependent->state = JobState::DependencyFailed;
        actions.emplace_back(
            JobTerminal{dependent->id, dependent->key, dependent->state});
        publishDecisions(*dependent, TerminalStatus::Failed, actions);
        failed.push_back(dependent->key);
      }
    }
  }

  void satisfyDependents(const EvaluationKey &key,
                         std::vector<Action> &actions) {
    for (const JobId &dependentId : takeDependents(key)) {
      Job *dependent = findJob(dependentId);
      if (!dependent || terminal(dependent->state) ||
          dependent->waitingOn.erase(key) == 0)
        continue;
      --dependencyEdges;
      if (dependent->waitingOn.empty() &&
          dependent->state == JobState::WaitingForInputs)
        enqueueReady(*dependent);
    }
    schedule(actions);
  }

  void terminatePending(Job &job, JobState state,
                        std::vector<Action> &actions) {
    if (reservedJob && *reservedJob == job.id)
      reservedJob.reset();
    removeReady(job);
    --globalUsage.pending;
    --classUsage[index(job.demand.resourceClass)].pending;
    detachPendingDependencies(job);
    job.state = state;
    actions.emplace_back(JobTerminal{job.id, job.key, state});
    publishDecisions(job, terminalStatus(state), actions);
    failDependents(job.key, actions);
  }

  void cancelSubscriber(Subscriber &subscriber, bool superseded,
                        std::vector<Action> &actions) {
    if (!subscriber.active)
      return;
    subscriber.active = false;
    Job *job = findJob(subscriber.job);
    if (!job || terminal(job->state))
      return;
    --job->activePriorities[index(subscriber.priority)];
    const bool observed = std::ranges::any_of(
        job->activePriorities, [](std::size_t count) { return count != 0; });
    if (observed) {
      refreshPriority(*job);
      return;
    }
    if (job->keepForCache)
      return;
    if (job->state == JobState::WaitingForInputs ||
        job->state == JobState::Queued) {
      terminatePending(*job,
                       superseded ? JobState::Superseded : JobState::Cancelled,
                       actions);
      return;
    }
    if (job->state == JobState::Running) {
      job->state = JobState::CancellationRequested;
      actions.emplace_back(CancelExecution{job->id, job->worker});
    }
  }

  void updateProjection(const Submit &event, std::vector<Action> &actions) {
    auto found = projections.find(event.projection);
    if (found == projections.end()) {
      projections.emplace(
          event.projection,
          Projection{event.generation, event.key,
                     std::vector<SubscriberId>{event.subscriber}});
      return;
    }
    Projection &projection = found->second;
    if (event.generation > projection.generation) {
      const std::vector<SubscriberId> previous =
          std::move(projection.subscribers);
      projection.generation = event.generation;
      projection.key = event.key;
      projection.subscribers.clear();
      projection.subscribers.push_back(event.subscriber);
      for (const SubscriberId &id : previous) {
        auto subscriber = subscribers.find(id);
        if (subscriber != subscribers.end())
          cancelSubscriber(subscriber->second, true, actions);
      }
      return;
    }
    projection.subscribers.push_back(event.subscriber);
  }

  void eraseTerminalJob(const EvaluationKey &key) {
    auto found = jobsByKey.find(key);
    if (found == jobsByKey.end())
      return;
    Job &job = found->second;
    std::unordered_set<ProjectionId, TypedIdHash<ProjectionIdTag>> affected;
    affected.reserve(std::min(job.subscribers.size(), projections.size()));
    for (const SubscriberId &id : job.subscribers) {
      const auto subscriber = subscribers.find(id);
      if (subscriber == subscribers.end())
        continue;
      const auto projection = projections.find(subscriber->second.projection);
      if (projection != projections.end() && projection->second.key == job.key)
        affected.insert(subscriber->second.projection);
      subscribers.erase(subscriber);
    }
    for (const ProjectionId &id : affected) {
      const auto projection = projections.find(id);
      if (projection != projections.end() && projection->second.key == job.key)
        projection->second.subscribers.clear();
    }
    jobsById.erase(job.id);
    jobsByKey.erase(found);
  }

  bool createsCycle(const EvaluationKey &key,
                    std::span<const EvaluationKey> dependencies) const {
    std::vector<EvaluationKey> pending(dependencies.begin(),
                                       dependencies.end());
    std::unordered_set<EvaluationKey, EvaluationKeyHash> visited;
    visited.reserve(std::min(limits.jobs, dependencies.size() + 8));
    while (!pending.empty()) {
      EvaluationKey candidate = pending.back();
      pending.pop_back();
      if (candidate == key)
        return true;
      if (!visited.insert(candidate).second)
        continue;
      const auto found = jobsByKey.find(candidate);
      if (found != jobsByKey.end())
        pending.insert(pending.end(), found->second.dependencies.begin(),
                       found->second.dependencies.end());
    }
    return false;
  }

  AdmissionRejection validateSubmit(const Submit &event, bool &accepted) const {
    accepted = false;
    const std::size_t resource = index(event.demand.resourceClass);
    if (!validLimits)
      return AdmissionRejection::InvalidLimits;
    if (resource >= resourceClassCount || !validEnum(event.priority) ||
        !validEnum(event.retention) || event.demand.cpuSlots == 0)
      return AdmissionRejection::InvalidDemand;
    const ResourceLimit &classLimit = limits.classes[resource];
    if (classLimit.concurrentJobs == 0 ||
        event.demand.cpuSlots > limits.global.cpuSlots ||
        event.demand.cpuSlots > classLimit.cpuSlots ||
        event.demand.memoryBytes > limits.global.memoryBytes ||
        event.demand.memoryBytes > classLimit.memoryBytes)
      return AdmissionRejection::InvalidDemand;
    if (event.dependencies.size() > limits.dependenciesPerJob)
      return AdmissionRejection::TooManyDependencies;
    if (subscribers.size() >= limits.subscribers)
      return AdmissionRejection::TooManySubscribers;
    const auto projection = projections.find(event.projection);
    if (projection == projections.end() &&
        projections.size() >= limits.projections)
      return AdmissionRejection::TooManyProjections;
    if (projection != projections.end() &&
        (event.generation < projection->second.generation ||
         (event.generation == projection->second.generation &&
          event.key != projection->second.key)))
      return AdmissionRejection::GenerationConflict;
    accepted = true;
    return AdmissionRejection::InvalidLimits;
  }

  StepResult submit(const Submit &source) {
    StepResult result;
    if (const auto prior = subscribers.find(source.subscriber);
        prior != subscribers.end()) {
      result.status = prior->second.key == source.key &&
                              prior->second.projection == source.projection &&
                              prior->second.generation == source.generation
                          ? EventStatus::Idempotent
                          : EventStatus::RejectedIdentity;
      return result;
    }

    bool accepted = false;
    const AdmissionRejection preliminary = validateSubmit(source, accepted);
    if (!accepted) {
      result.status =
          preliminary == AdmissionRejection::TooManySubscribers ||
                  preliminary == AdmissionRejection::TooManyProjections ||
                  preliminary == AdmissionRejection::TooManyDependencies
              ? EventStatus::RejectedCapacity
              : EventStatus::RejectedInvalid;
      result.actions.emplace_back(
          AdmissionRejected{source.subscriber, preliminary});
      return result;
    }

    std::vector<EvaluationKey> dependencies = source.dependencies;
    std::ranges::sort(dependencies);
    dependencies.erase(std::unique(dependencies.begin(), dependencies.end()),
                       dependencies.end());
    if (dependencies.size() > limits.dependenciesPerJob) {
      result.status = EventStatus::RejectedCapacity;
      result.actions.emplace_back(AdmissionRejected{
          source.subscriber, AdmissionRejection::TooManyDependencies});
      return result;
    }

    auto existing = jobsByKey.find(source.key);
    if (existing != jobsByKey.end() &&
        (existing->second.state == JobState::Cancelled ||
         existing->second.state == JobState::Superseded ||
         existing->second.state == JobState::WorkerLost ||
         existing->second.state == JobState::DependencyFailed)) {
      result.status = EventStatus::RejectedInvalid;
      result.actions.emplace_back(AdmissionRejected{
          source.subscriber, AdmissionRejection::RetirementRequired});
      return result;
    }
    if (existing != jobsByKey.end()) {
      Job &job = existing->second;
      if (job.state == JobState::CancellationRequested) {
        result.status = EventStatus::RejectedInvalid;
        result.actions.emplace_back(AdmissionRejected{
            source.subscriber, AdmissionRejection::CancellationInFlight});
        return result;
      }
      if (job.demand != source.demand || job.dependencies != dependencies) {
        result.status = EventStatus::RejectedIdentity;
        result.actions.emplace_back(AdmissionRejected{
            source.subscriber, AdmissionRejection::IdentityCollision});
        return result;
      }
      subscribers.emplace(source.subscriber,
                          Subscriber{job.id, source.key, source.projection,
                                     source.generation, source.priority});
      job.subscribers.push_back(source.subscriber);
      ++job.activePriorities[index(source.priority)];
      job.keepForCache =
          job.keepForCache || source.retention == Retention::CompleteForCache;
      refreshPriority(job);
      result.actions.emplace_back(
          Admitted{job.id, job.key, source.subscriber, true});
      updateProjection(source, result.actions);
      if (terminal(job.state)) {
        const TerminalStatus status = terminalStatus(job.state);
        Subscriber &subscriber = subscribers.find(source.subscriber)->second;
        if (subscriber.active) {
          result.actions.emplace_back(
              PublicationDecision{job.id, job.key, source.subscriber,
                                  source.projection, source.generation, status,
                                  publicationDisposition(subscriber, status)});
          subscriber.active = false;
          --job.activePriorities[index(source.priority)];
        }
      }
      schedule(result.actions);
      return result;
    }

    if (jobsByKey.size() >= limits.jobs) {
      result.status = EventStatus::RejectedCapacity;
      result.actions.emplace_back(AdmissionRejected{
          source.subscriber, AdmissionRejection::TooManyJobs});
      return result;
    }
    if (jobsById.contains(source.proposedJob)) {
      result.status = EventStatus::RejectedIdentity;
      result.actions.emplace_back(AdmissionRejected{
          source.subscriber, AdmissionRejection::IdentityCollision});
      return result;
    }
    if (nextOrdinal == std::numeric_limits<std::uint64_t>::max()) {
      result.status = EventStatus::RejectedCapacity;
      result.actions.emplace_back(AdmissionRejected{
          source.subscriber, AdmissionRejection::OrdinalExhausted});
      return result;
    }
    const std::size_t resource = index(source.demand.resourceClass);
    if (globalUsage.pending >= limits.global.queuedJobs ||
        classUsage[resource].pending >= limits.classes[resource].queuedJobs) {
      result.status = EventStatus::RejectedCapacity;
      result.actions.emplace_back(AdmissionRejected{
          source.subscriber, AdmissionRejection::ClassQueueFull});
      return result;
    }
    if (dependencies.size() > limits.dependencyEdges - dependencyEdges) {
      result.status = EventStatus::RejectedCapacity;
      result.actions.emplace_back(AdmissionRejected{
          source.subscriber, AdmissionRejection::TooManyDependencyEdges});
      return result;
    }
    if (createsCycle(source.key, dependencies)) {
      result.status = EventStatus::RejectedInvalid;
      result.actions.emplace_back(AdmissionRejected{
          source.subscriber, AdmissionRejection::DependencyCycle});
      return result;
    }

    Job job{source.proposedJob, source.key, source.demand,
            std::move(dependencies)};
    job.keepForCache = source.retention == Retention::CompleteForCache;
    job.admittedAt = now.ticks;
    job.ordinal = nextOrdinal++;
    job.queuedPriority = source.priority;
    job.subscribers.push_back(source.subscriber);
    ++job.activePriorities[index(source.priority)];
    bool dependencyFailed = false;
    for (const EvaluationKey &dependency : job.dependencies) {
      if (availableResults.contains(dependency))
        continue;
      const auto found = jobsByKey.find(dependency);
      if (found != jobsByKey.end() && terminal(found->second.state) &&
          found->second.state != JobState::Succeeded) {
        dependencyFailed = true;
        break;
      }
      job.waitingOn.insert(dependency);
    }

    const JobId jobId = job.id;
    jobsById.emplace(job.id, job.key);
    auto inserted = jobsByKey.emplace(job.key, std::move(job));
    Job &stored = inserted.first->second;
    subscribers.emplace(source.subscriber,
                        Subscriber{stored.id, source.key, source.projection,
                                   source.generation, source.priority});
    ++globalUsage.pending;
    ++classUsage[resource].pending;
    if (!dependencyFailed) {
      for (const EvaluationKey &dependency : stored.waitingOn) {
        waiters[dependency].insert(stored.id);
        ++dependencyEdges;
      }
    } else {
      stored.waitingOn.clear();
    }
    result.actions.emplace_back(
        Admitted{jobId, source.key, source.subscriber, false});
    updateProjection(source, result.actions);
    if (terminal(stored.state)) {
      schedule(result.actions);
      return result;
    }
    if (dependencyFailed)
      terminatePending(stored, JobState::DependencyFailed, result.actions);
    else if (stored.waitingOn.empty())
      enqueueReady(stored);
    else
      stored.state = JobState::WaitingForInputs;
    schedule(result.actions);
    return result;
  }

  StepResult cancel(const CancelSubscription &event) {
    StepResult result;
    auto found = subscribers.find(event.subscriber);
    if (found == subscribers.end()) {
      result.status = EventStatus::IgnoredStale;
      return result;
    }
    if (!found->second.active) {
      result.status = EventStatus::Idempotent;
      return result;
    }
    cancelSubscriber(found->second, false, result.actions);
    schedule(result.actions);
    return result;
  }

  StepResult advance(const AdvanceTime &event) {
    StepResult result;
    if (event.now < now) {
      result.status = EventStatus::IgnoredStale;
      return result;
    }
    if (event.now == now) {
      result.status = EventStatus::Idempotent;
      return result;
    }
    now = event.now;
    schedule(result.actions);
    return result;
  }

  StepResult started(const ExecutionStarted &event) {
    StepResult result;
    Job *job = findJob(event.job);
    if (!job || terminal(job->state)) {
      result.status = EventStatus::IgnoredStale;
      return result;
    }
    if (job->state != JobState::Running &&
        job->state != JobState::CancellationRequested) {
      result.status = EventStatus::RejectedInvalid;
      return result;
    }
    if (job->worker) {
      result.status = *job->worker == event.worker
                          ? EventStatus::Idempotent
                          : EventStatus::RejectedIdentity;
      return result;
    }
    job->worker = event.worker;
    if (job->state == JobState::CancellationRequested)
      result.actions.emplace_back(CancelExecution{job->id, job->worker});
    return result;
  }

  StepResult progress(const Progress &event) {
    StepResult result;
    Job *job = findJob(event.job);
    if (!job || terminal(job->state)) {
      result.status = EventStatus::IgnoredStale;
      return result;
    }
    if ((job->state != JobState::Running &&
         job->state != JobState::CancellationRequested) ||
        !job->worker || *job->worker != event.worker) {
      result.status = EventStatus::RejectedIdentity;
      return result;
    }
    if (event.sequence <= job->lastSequence) {
      result.status = EventStatus::IgnoredStale;
      return result;
    }
    if (event.totalUnits && event.completedUnits > *event.totalUnits) {
      result.status = EventStatus::RejectedInvalid;
      return result;
    }
    job->lastSequence = event.sequence;
    for (const SubscriberId &id : job->subscribers) {
      const auto subscriber = subscribers.find(id);
      if (subscriber != subscribers.end() && current(subscriber->second))
        result.actions.emplace_back(ForwardProgress{
            job->id, job->key, id, subscriber->second.projection,
            subscriber->second.generation, event.sequence, event.stage,
            event.completedUnits, event.totalUnits});
    }
    return result;
  }

  StepResult finish(const ExecutionFinished &event) {
    StepResult result;
    if (!validEnum(event.status)) {
      result.status = EventStatus::RejectedInvalid;
      return result;
    }
    Job *job = findJob(event.job);
    if (!job || terminal(job->state)) {
      result.status = EventStatus::IgnoredStale;
      return result;
    }
    if ((job->state != JobState::Running &&
         job->state != JobState::CancellationRequested) ||
        !job->worker || *job->worker != event.worker) {
      result.status = EventStatus::RejectedIdentity;
      return result;
    }
    if (event.sequence <= job->lastSequence) {
      result.status = EventStatus::IgnoredStale;
      return result;
    }
    job->lastSequence = event.sequence;
    release(*job);
    job->state = terminalState(event.status);
    result.actions.emplace_back(JobTerminal{job->id, job->key, job->state});
    publishDecisions(*job, event.status, result.actions);
    if (event.status != TerminalStatus::Succeeded)
      failDependents(job->key, result.actions);
    schedule(result.actions);
    return result;
  }

  StepResult retire(const RetireJob &event) {
    StepResult result;
    auto found = jobsByKey.find(event.key);
    if (found == jobsByKey.end()) {
      result.status = EventStatus::Idempotent;
      return result;
    }
    Job &job = found->second;
    if (job.id != event.job) {
      result.status = EventStatus::IgnoredStale;
      return result;
    }
    if (!terminal(job.state) || waiters.contains(job.key) ||
        (job.state == JobState::Succeeded &&
         !availableResults.contains(job.key))) {
      result.status = EventStatus::RejectedInvalid;
      return result;
    }
    eraseTerminalJob(event.key);
    return result;
  }

  StepResult artifactAvailable(const ArtifactAvailable &event) {
    StepResult result;
    if (availableResults.contains(event.key)) {
      result.status = EventStatus::Idempotent;
      return result;
    }
    if (availableResults.size() >= limits.availableResults) {
      result.status = EventStatus::RejectedCapacity;
      return result;
    }
    availableResults.insert(event.key);
    satisfyDependents(event.key, result.actions);
    return result;
  }

  StepResult forgetAvailability(const ForgetArtifactAvailability &event) {
    StepResult result;
    if (availableResults.erase(event.key) == 0)
      result.status = EventStatus::Idempotent;
    return result;
  }

  StepResult retireProjection(const RetireProjection &event) {
    StepResult result;
    const auto found = projections.find(event.projection);
    if (found == projections.end()) {
      result.status = EventStatus::Idempotent;
      return result;
    }
    if (found->second.generation != event.observedGeneration ||
        found->second.key != event.observedKey) {
      result.status = EventStatus::IgnoredStale;
      return result;
    }
    const bool active = std::ranges::any_of(
        found->second.subscribers, [this](const SubscriberId &id) {
          const auto subscriber = subscribers.find(id);
          return subscriber != subscribers.end() && subscriber->second.active;
        });
    if (active) {
      result.status = EventStatus::RejectedInvalid;
      return result;
    }
    projections.erase(found);
    return result;
  }

  StepResult apply(const Event &event) {
    if (!validLimits) {
      StepResult result;
      result.status = EventStatus::RejectedInvalid;
      if (const auto *submission = std::get_if<Submit>(&event))
        result.actions.emplace_back(AdmissionRejected{
            submission->subscriber, AdmissionRejection::InvalidLimits});
      return result;
    }
    return std::visit(
        [this](const auto &typed) -> StepResult {
          using Type = std::decay_t<decltype(typed)>;
          if constexpr (std::is_same_v<Type, Submit>)
            return submit(typed);
          else if constexpr (std::is_same_v<Type, CancelSubscription>)
            return cancel(typed);
          else if constexpr (std::is_same_v<Type, AdvanceTime>)
            return advance(typed);
          else if constexpr (std::is_same_v<Type, ExecutionStarted>)
            return started(typed);
          else if constexpr (std::is_same_v<Type, Progress>)
            return progress(typed);
          else if constexpr (std::is_same_v<Type, ExecutionFinished>)
            return finish(typed);
          else if constexpr (std::is_same_v<Type, RetireJob>)
            return retire(typed);
          else if constexpr (std::is_same_v<Type, ArtifactAvailable>)
            return artifactAvailable(typed);
          else if constexpr (std::is_same_v<Type, ForgetArtifactAvailability>)
            return forgetAvailability(typed);
          else
            return retireProjection(typed);
        },
        event);
  }

  SchedulerStats stats() const {
    SchedulerStats result;
    result.jobs = jobsByKey.size();
    result.subscribers = subscribers.size();
    result.projections = projections.size();
    result.availableResults = availableResults.size();
    result.dependencyEdges = dependencyEdges;
    result.usedCpuSlots = globalUsage.cpuSlots;
    result.usedMemoryBytes = globalUsage.memoryBytes;
    result.resourceDrainReserved = reservedJob.has_value();
    for (const auto &[key, job] : jobsByKey) {
      static_cast<void>(key);
      switch (job.state) {
      case JobState::WaitingForInputs:
        ++result.waiting;
        break;
      case JobState::Queued:
        ++result.queued;
        break;
      case JobState::Running:
      case JobState::CancellationRequested:
        ++result.running;
        break;
      default:
        ++result.terminal;
        break;
      }
    }
    for (const auto &[id, subscriber] : subscribers) {
      static_cast<void>(id);
      if (subscriber.active)
        ++result.activeSubscribers;
    }
    return result;
  }
};

Scheduler::Scheduler(SchedulerLimits limits)
    : impl_(std::make_unique<Impl>(std::move(limits))) {}
Scheduler::Scheduler(Scheduler &&) noexcept = default;
Scheduler &Scheduler::operator=(Scheduler &&) noexcept = default;
Scheduler::~Scheduler() = default;

Result<Generation> Generation::create(std::uint64_t value) {
  if (value == 0)
    return std::unexpected(diagnostic("evaluation.generation.zero",
                                      "evaluation generation must be nonzero"));
  return Generation{value};
}

bool Scheduler::valid() const noexcept { return impl_->validLimits; }

StepResult Scheduler::apply(const Event &event) { return impl_->apply(event); }

SchedulerStats Scheduler::stats() const { return impl_->stats(); }

std::optional<JobState> Scheduler::state(const EvaluationKey &key) const {
  const auto found = impl_->jobsByKey.find(key);
  return found == impl_->jobsByKey.end()
             ? std::nullopt
             : std::optional<JobState>{found->second.state};
}

bool Scheduler::isCurrent(ProjectionId projection, Generation generation,
                          const EvaluationKey &key) const {
  const auto found = impl_->projections.find(projection);
  return found != impl_->projections.end() &&
         found->second.generation == generation && found->second.key == key;
}

} // namespace kearne::evaluation
