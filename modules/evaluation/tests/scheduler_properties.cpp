#include <kearne/evaluation/scheduler.hpp>
#include <kearne/testkit/property.hpp>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

using namespace kearne;
using namespace kearne::evaluation;

constexpr std::uint64_t priorityCount =
    static_cast<std::uint64_t>(Priority::Count);

void require(bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}

template <typename Id> Id id(std::uint64_t value) {
  typename Id::RandomTail random{};
  for (std::size_t byte = 0; byte < random.size(); ++byte)
    random[byte] =
        static_cast<std::uint8_t>((value >> ((byte % 8U) * 8U)) + byte * 17U);
  auto result = Id::create(value & ((std::uint64_t{1} << 48U) - 1U), random);
  require(result.has_value(), "generated ID is invalid");
  return *result;
}

EvaluationKey key(std::uint64_t value) {
  EvaluationKey::Bytes bytes{};
  for (std::size_t byte = 0; byte < bytes.size(); ++byte)
    bytes[byte] =
        static_cast<std::uint8_t>((value >> ((byte % 8U) * 8U)) + byte * 29U);
  auto result = EvaluationKey::fromBytes("blake3", bytes);
  require(result.has_value(), "generated evaluation key is invalid");
  return *result;
}

using KeyHash = TypedDigestHash<EvaluationKeyTag>;

Generation generation(std::uint64_t value) {
  auto result = Generation::create(value);
  require(result.has_value(), "generated evaluation generation is invalid");
  return *result;
}

using JobHash = TypedIdHash<JobIdTag>;
using SubscriberHash = TypedIdHash<SubscriberIdTag>;
using ProjectionHash = TypedIdHash<ProjectionIdTag>;

SchedulerLimits limits(std::size_t jobs, std::size_t concurrent = 4) {
  SchedulerLimits result;
  result.jobs = jobs;
  result.subscribers = jobs * 8U;
  result.projections = jobs * 2U;
  result.availableResults = jobs * 2U;
  result.dependenciesPerJob = 8;
  result.dependencyEdges = jobs * 8U;
  result.global = {jobs, concurrent, static_cast<std::uint32_t>(concurrent),
                   static_cast<std::uint64_t>(concurrent) * 4096U};
  for (ResourceLimit &resource : result.classes)
    resource = {jobs, concurrent, static_cast<std::uint32_t>(concurrent),
                static_cast<std::uint64_t>(concurrent) * 4096U};
  result.agingIntervalTicks = 10;
  result.maximumAgePromotions = static_cast<std::uint8_t>(Priority::Count) - 1U;
  result.fitSearchLimit = 64;
  return result;
}

template <typename ActionType>
std::vector<const ActionType *> actions(const StepResult &result) {
  std::vector<const ActionType *> matches;
  for (const Action &action : result.actions) {
    if (const auto *typed = std::get_if<ActionType>(&action))
      matches.push_back(typed);
  }
  return matches;
}

ResourceDemand demand(std::uint64_t number) {
  return {1, 64U + (number % 8U) * 16U,
          static_cast<ResourceClass>(
              number % static_cast<std::uint64_t>(ResourceClass::Count))};
}

std::vector<EvaluationKey> dependencies(std::uint64_t number) {
  if (number == 0 || number % 4U != 0)
    return {};
  return {key(number - 1U)};
}

struct ReferenceSubscriber final {
  ProjectionId projection;
  Generation generation;
  EvaluationKey key;
  bool active = true;
};

struct ReferenceProjection final {
  Generation generation;
  EvaluationKey key;
};

struct ReferenceJob final {
  EvaluationKey key;
  ResourceDemand demand;
  std::vector<EvaluationKey> dependencies;
  std::optional<WorkerInstanceId> worker;
  std::uint64_t sequence = 0;
  bool running = false;
  bool terminal = false;
};

class ReferenceModel final {
public:
  explicit ReferenceModel(SchedulerLimits configured)
      : limits_(std::move(configured)) {}

  void before(const Event &event, const StepResult &result) {
    const auto *submission = std::get_if<Submit>(&event);
    if (submission && !actions<Admitted>(result).empty()) {
      const auto projection = projections_.find(submission->projection);
      if (projection == projections_.end() ||
          submission->generation > projection->second.generation) {
        for (auto &[id, subscriber] : subscribers_) {
          static_cast<void>(id);
          if (subscriber.projection == submission->projection)
            subscriber.active = false;
        }
        projections_.insert_or_assign(
            submission->projection,
            ReferenceProjection{submission->generation, submission->key});
      }
      subscribers_.insert_or_assign(submission->subscriber,
                                    ReferenceSubscriber{submission->projection,
                                                        submission->generation,
                                                        submission->key});
    }
    if (const auto *cancel = std::get_if<CancelSubscription>(&event)) {
      const auto found = subscribers_.find(cancel->subscriber);
      if (found != subscribers_.end() &&
          result.status != EventStatus::IgnoredStale)
        found->second.active = false;
    }
  }

  void observe(const Event &event, const StepResult &result,
               Scheduler &scheduler) {
    before(event, result);
    const Submit *submission = std::get_if<Submit>(&event);
    for (const Action &action : result.actions) {
      if (const auto *admitted = std::get_if<Admitted>(&action)) {
        const auto existing = keyToJob_.find(admitted->key);
        if (existing != keyToJob_.end() && admitted->sharedExecution) {
          require(existing->second == admitted->job,
                  "equal evaluation keys did not deduplicate");
        } else {
          require(submission != nullptr,
                  "admission was emitted for a non-submit event");
          if (existing != keyToJob_.end()) {
            require(jobs_.at(existing->second).terminal,
                    "equal key started twice concurrently");
            existing->second = admitted->job;
          } else {
            keyToJob_.emplace(admitted->key, admitted->job);
          }
          jobs_.emplace(admitted->job,
                        ReferenceJob{admitted->key, submission->demand,
                                     submission->dependencies, std::nullopt, 0,
                                     false, false});
        }
      } else if (const auto *dispatch = std::get_if<Dispatch>(&action)) {
        auto found = jobs_.find(dispatch->job);
        require(found != jobs_.end(), "unknown job was dispatched");
        require(!found->second.running && !found->second.terminal,
                "job dispatched more than once concurrently");
        for (const EvaluationKey &dependency : found->second.dependencies)
          require(succeeded_.contains(dependency),
                  "job ran before an input succeeded");
        found->second.running = true;
        running_.push_back(dispatch->job);
      } else if (const auto *progress = std::get_if<ForwardProgress>(&action)) {
        const auto found = subscribers_.find(progress->subscriber);
        require(found != subscribers_.end() && found->second.active,
                "progress reached an inactive subscriber");
        require(isCurrent(found->second),
                "progress reached a stale projection");
        require(progress->key == found->second.key &&
                    progress->projection == found->second.projection &&
                    progress->generation == found->second.generation,
                "progress lost subscriber evaluation identity");
      } else if (const auto *publication =
                     std::get_if<PublicationDecision>(&action)) {
        const auto found = subscribers_.find(publication->subscriber);
        require(found != subscribers_.end(),
                "publication references an unknown subscriber");
        const bool expectedCurrent = isCurrent(found->second);
        require((publication->disposition == PublicationDisposition::Current) ==
                    expectedCurrent,
                "stale/current publication decision disagrees with model");
        if (!expectedCurrent &&
            publication->terminal == TerminalStatus::Succeeded)
          require(publication->disposition ==
                      (isStale(found->second)
                           ? PublicationDisposition::StaleCacheOnly
                           : PublicationDisposition::UnobservedCacheOnly),
                  "stale success was neither current nor cache-only");
        if (!expectedCurrent &&
            publication->terminal != TerminalStatus::Succeeded)
          require(publication->disposition ==
                      (isStale(found->second)
                           ? PublicationDisposition::StaleNoArtifact
                           : PublicationDisposition::UnobservedNoArtifact),
                  "non-current terminal state lost stale/unobserved identity");
        found->second.active = false;
      } else if (const auto *terminal = std::get_if<JobTerminal>(&action)) {
        auto found = jobs_.find(terminal->job);
        require(found != jobs_.end(), "unknown job reached terminal state");
        found->second.running = false;
        found->second.terminal = true;
        terminal_.push_back(terminal->job);
        if (terminal->state == JobState::Succeeded)
          succeeded_.insert(terminal->key);
      }
    }
    const SchedulerStats actual = scheduler.stats();
    require(actual.running <= limits_.global.concurrentJobs,
            "global concurrency limit exceeded");
    require(actual.usedCpuSlots <= limits_.global.cpuSlots,
            "global CPU limit exceeded");
    require(actual.usedMemoryBytes <= limits_.global.memoryBytes,
            "global memory limit exceeded");
  }

  void bindDispatches(const StepResult &result, Scheduler &scheduler,
                      std::uint64_t &eventCounter) {
    for (const Dispatch *dispatch : actions<Dispatch>(result)) {
      auto found = jobs_.find(dispatch->job);
      require(found != jobs_.end(), "cannot bind an unknown dispatch");
      const WorkerInstanceId worker =
          id<WorkerInstanceId>(5'000'000U + eventCounter++);
      found->second.worker = worker;
      StepResult started =
          scheduler.apply(ExecutionStarted{dispatch->job, worker});
      observe(Event{ExecutionStarted{dispatch->job, worker}}, started,
              scheduler);
    }
  }

  std::optional<JobId> running(std::uint64_t selector) const {
    if (running_.empty())
      return std::nullopt;
    for (std::size_t offset = 0; offset < running_.size(); ++offset) {
      const JobId candidate =
          running_[(static_cast<std::size_t>(selector) + offset) %
                   running_.size()];
      const auto found = jobs_.find(candidate);
      if (found != jobs_.end() && found->second.running)
        return candidate;
    }
    return std::nullopt;
  }

  std::optional<JobId> completed(std::uint64_t selector) const {
    if (terminal_.empty())
      return std::nullopt;
    for (std::size_t offset = 0; offset < terminal_.size(); ++offset) {
      const JobId candidate =
          terminal_[(static_cast<std::size_t>(selector) + offset) %
                    terminal_.size()];
      const auto found = jobs_.find(candidate);
      if (found != jobs_.end() && found->second.terminal &&
          found->second.worker)
        return candidate;
    }
    return std::nullopt;
  }

  ReferenceJob &job(JobId id) { return jobs_.at(id); }

private:
  bool isCurrent(const ReferenceSubscriber &subscriber) const {
    const auto projection = projections_.find(subscriber.projection);
    return subscriber.active && projection != projections_.end() &&
           projection->second.generation == subscriber.generation &&
           projection->second.key == subscriber.key;
  }

  bool isStale(const ReferenceSubscriber &subscriber) const {
    const auto projection = projections_.find(subscriber.projection);
    return projection != projections_.end() &&
           (projection->second.generation != subscriber.generation ||
            projection->second.key != subscriber.key);
  }

  SchedulerLimits limits_;
  std::unordered_map<EvaluationKey, JobId, KeyHash> keyToJob_;
  std::unordered_map<JobId, ReferenceJob, JobHash> jobs_;
  std::unordered_map<SubscriberId, ReferenceSubscriber, SubscriberHash>
      subscribers_;
  std::unordered_map<ProjectionId, ReferenceProjection, ProjectionHash>
      projections_;
  std::unordered_set<EvaluationKey, KeyHash> succeeded_;
  std::vector<JobId> running_;
  std::vector<JobId> terminal_;
};

void applyObserved(Scheduler &scheduler, ReferenceModel &model,
                   const Event &event, std::uint64_t &eventCounter) {
  StepResult result = scheduler.apply(event);
  model.observe(event, result, scheduler);
  model.bindDispatches(result, scheduler, eventCounter);
}

void verifyMixedStateMachine() {
  const testkit::PropertyProfile profile = testkit::propertyProfile();
  const std::size_t eventCount = std::max<std::size_t>(
      12'000,
      static_cast<std::size_t>((profile.iterations + profile.shardCount - 1U) /
                               profile.shardCount));
  const SchedulerLimits configured = limits(1'024, 4);
  Scheduler scheduler{configured};
  require(scheduler.valid(), "generated scheduler limits are invalid");
  ReferenceModel model{configured};
  testkit::Random random{profile.seed ^
                         (profile.shardIndex * 0x9e3779b97f4a7c15ULL)};
  std::vector<SubscriberId> subscriberIds;
  std::uint64_t nextSubscriber = 1;
  std::uint64_t nextJob = 1;
  std::uint64_t eventCounter = 1;
  std::uint64_t clock = 0;
  std::array<std::uint64_t, 64> generations{};
  generations.fill(1);

  for (std::size_t iteration = 0; iteration < eventCount; ++iteration) {
    const std::uint64_t choice = random.next() % 100U;
    if (choice < 47U) {
      const std::uint64_t number = random.next() % 400U;
      const std::size_t projectionIndex =
          static_cast<std::size_t>(random.next() % generations.size());
      if ((random.next() % 5U) != 0)
        ++generations[projectionIndex];
      const Generation requestGeneration =
          generation(generations[projectionIndex]);
      const SubscriberId subscriber =
          id<SubscriberId>(1'000'000U + nextSubscriber++);
      subscriberIds.push_back(subscriber);
      Submit submit{id<JobId>(2'000'000U + nextJob++),
                    key(number),
                    subscriber,
                    id<ProjectionId>(3'000'000U + projectionIndex),
                    requestGeneration,
                    static_cast<Priority>(random.next() % priorityCount),
                    demand(number),
                    dependencies(number),
                    random.next() % 7U == 0 ? Retention::CompleteForCache
                                            : Retention::CancelWhenUnobserved};
      applyObserved(scheduler, model, Event{std::move(submit)}, eventCounter);
    } else if (choice < 61U && !subscriberIds.empty()) {
      const SubscriberId subscriber = subscriberIds[static_cast<std::size_t>(
          random.next() % subscriberIds.size())];
      applyObserved(scheduler, model, Event{CancelSubscription{subscriber}},
                    eventCounter);
    } else if (choice < 74U) {
      const auto running = model.running(random.next());
      if (!running) {
        clock += 1U + random.next() % 20U;
        applyObserved(scheduler, model, Event{AdvanceTime{{clock}}},
                      eventCounter);
        continue;
      }
      ReferenceJob &job = model.job(*running);
      ++job.sequence;
      applyObserved(
          scheduler, model,
          Event{Progress{*running, *job.worker, job.sequence,
                         static_cast<std::uint32_t>(random.next() % 16U),
                         random.next() % 100U, 100U}},
          eventCounter);
    } else if (choice < 86U) {
      const auto running = model.running(random.next());
      if (!running)
        continue;
      ReferenceJob &job = model.job(*running);
      ++job.sequence;
      const TerminalStatus outcome =
          random.next() % 8U == 0
              ? static_cast<TerminalStatus>(1U + random.next() % 3U)
              : TerminalStatus::Succeeded;
      applyObserved(scheduler, model,
                    Event{ExecutionFinished{*running, *job.worker, job.sequence,
                                            outcome}},
                    eventCounter);
      if (outcome == TerminalStatus::Succeeded)
        applyObserved(scheduler, model, Event{ArtifactAvailable{job.key}},
                      eventCounter);
    } else if (choice < 94U) {
      clock += 1U + random.next() % 20U;
      applyObserved(scheduler, model, Event{AdvanceTime{{clock}}},
                    eventCounter);
    } else {
      if ((random.next() & 1U) != 0) {
        const auto completed = model.completed(random.next());
        if (completed) {
          ReferenceJob &job = model.job(*completed);
          StepResult stale = scheduler.apply(
              ExecutionFinished{*completed, *job.worker, job.sequence + 100U,
                                TerminalStatus::Succeeded});
          require(stale.status == EventStatus::IgnoredStale,
                  "stale completion was accepted");
          continue;
        }
      }
      const auto running = model.running(random.next());
      if (!running)
        continue;
      ReferenceJob &job = model.job(*running);
      const WorkerInstanceId staleWorker =
          id<WorkerInstanceId>(9'000'000U + eventCounter++);
      StepResult stale = scheduler.apply(
          Progress{*running, staleWorker, job.sequence + 100U, 0, 0, {}});
      require(stale.status == EventStatus::RejectedIdentity,
              "stale worker instance was accepted");
      StepResult reordered = scheduler.apply(
          Progress{*running, *job.worker, job.sequence, 0, 0, {}});
      require(reordered.status == EventStatus::IgnoredStale,
              "reordered progress was accepted");
    }
  }
}

void verifyAgingAndCancellationRace() {
  SchedulerLimits configured = limits(16, 1);
  configured.agingIntervalTicks = 10;
  Scheduler scheduler{configured};
  const ResourceDemand resource = demand(0);
  const ProjectionId projection = id<ProjectionId>(100);

  auto blocker = scheduler.apply(Submit{id<JobId>(1),
                                        key(1),
                                        id<SubscriberId>(1),
                                        projection,
                                        generation(1),
                                        Priority::InteractivePreview,
                                        resource,
                                        {},
                                        Retention::CompleteForCache});
  const Dispatch *blockerDispatch = actions<Dispatch>(blocker).front();
  const WorkerInstanceId blockerWorker = id<WorkerInstanceId>(1);
  require(scheduler.apply(ExecutionStarted{blockerDispatch->job, blockerWorker})
                  .status == EventStatus::Applied,
          "blocker did not start");

  const ProjectionId backgroundProjection = id<ProjectionId>(101);
  auto background = scheduler.apply(Submit{id<JobId>(2),
                                           key(2),
                                           id<SubscriberId>(2),
                                           backgroundProjection,
                                           generation(1),
                                           Priority::Idle,
                                           resource,
                                           {},
                                           Retention::CancelWhenUnobserved});
  require(actions<Dispatch>(background).empty(),
          "queued background work bypassed resource limit");
  require(scheduler.apply(AdvanceTime{{50}}).status == EventStatus::Applied,
          "logical clock did not advance");
  auto interactive = scheduler.apply(Submit{id<JobId>(3),
                                            key(3),
                                            id<SubscriberId>(3),
                                            id<ProjectionId>(102),
                                            generation(1),
                                            Priority::InteractivePreview,
                                            resource,
                                            {},
                                            Retention::CancelWhenUnobserved});
  require(actions<Dispatch>(interactive).empty(),
          "interactive work bypassed resource limit");
  auto released = scheduler.apply(ExecutionFinished{
      blockerDispatch->job, blockerWorker, 1, TerminalStatus::Succeeded});
  const auto dispatched = actions<Dispatch>(released);
  require(dispatched.size() == 1 && dispatched.front()->key == key(2),
          "bounded aging did not prevent idle-work starvation");

  const WorkerInstanceId worker = id<WorkerInstanceId>(2);
  require(scheduler.apply(ExecutionStarted{dispatched.front()->job, worker})
                  .status == EventStatus::Applied,
          "aged job did not start");
  auto cancelled = scheduler.apply(CancelSubscription{id<SubscriberId>(2)});
  require(actions<CancelExecution>(cancelled).size() == 1,
          "last-subscriber cancellation did not reach the executor");
  auto raced = scheduler.apply(ExecutionFinished{
      dispatched.front()->job, worker, 1, TerminalStatus::Succeeded});
  const auto publications = actions<PublicationDecision>(raced);
  require(publications.size() == 1 &&
              publications.front()->disposition ==
                  PublicationDisposition::UnobservedCacheOnly,
          "cancelled completion became the current projection");
  require(scheduler
                  .apply(ExecutionFinished{dispatched.front()->job, worker, 2,
                                           TerminalStatus::Succeeded})
                  .status == EventStatus::IgnoredStale,
          "a second terminal event was accepted");
}

void verifyGeneratedFitAwareSelection() {
  const testkit::PropertyProfile profile = testkit::propertyProfile();
  testkit::checkProperty(
      "bounded fit-aware ready selection", profile,
      [](testkit::Random &random, std::uint64_t iteration) {
        const std::uint16_t capacity =
            static_cast<std::uint16_t>(3U + random.next() % 30U);
        const std::uint64_t memoryCapacity =
            1'024U + (random.next() % 32U) * 128U;
        SchedulerLimits configured = limits(4, 2);
        configured.fitSearchLimit = 2;
        configured.global.cpuSlots = capacity;
        configured.global.memoryBytes = memoryCapacity;
        configured.classes[static_cast<std::size_t>(ResourceClass::General)] = {
            4, 2, capacity, memoryCapacity};
        Scheduler scheduler{configured};
        const ResourceDemand large{static_cast<std::uint16_t>(capacity - 1U),
                                   memoryCapacity - 1U, ResourceClass::General};
        constexpr ResourceDemand small{1, 1, ResourceClass::General};

        auto blocker = scheduler.apply(
            Submit{id<JobId>(50'000'000U + iteration * 4U),
                   key(iteration * 4U),
                   id<SubscriberId>(51'000'000U + iteration * 4U),
                   id<ProjectionId>(52'000'000U + iteration * 4U),
                   generation(1),
                   Priority::VisibleResult,
                   large,
                   {},
                   Retention::CompleteForCache});
        const Dispatch *blockerDispatch = actions<Dispatch>(blocker).front();
        const WorkerInstanceId blockerWorker =
            id<WorkerInstanceId>(53'000'000U + iteration * 2U);
        require(scheduler
                        .apply(ExecutionStarted{blockerDispatch->job,
                                                blockerWorker})
                        .status == EventStatus::Applied,
                "fit-aware blocker did not start");

        const EvaluationKey largeKey = key(iteration * 4U + 1U);
        auto queuedLarge = scheduler.apply(
            Submit{id<JobId>(50'000'000U + iteration * 4U + 1U),
                   largeKey,
                   id<SubscriberId>(51'000'000U + iteration * 4U + 1U),
                   id<ProjectionId>(52'000'000U + iteration * 4U + 1U),
                   generation(1),
                   Priority::VisibleResult,
                   large,
                   {},
                   Retention::CancelWhenUnobserved});
        require(actions<Dispatch>(queuedLarge).empty(),
                "oversized head job unexpectedly fit remaining resources");
        const EvaluationKey smallKey = key(iteration * 4U + 2U);
        auto queuedSmall = scheduler.apply(
            Submit{id<JobId>(50'000'000U + iteration * 4U + 2U),
                   smallKey,
                   id<SubscriberId>(51'000'000U + iteration * 4U + 2U),
                   id<ProjectionId>(52'000'000U + iteration * 4U + 2U),
                   generation(1),
                   Priority::VisibleResult,
                   small,
                   {},
                   Retention::CancelWhenUnobserved});
        const auto smallDispatch = actions<Dispatch>(queuedSmall);
        require(smallDispatch.size() == 1 &&
                    smallDispatch.front()->key == smallKey,
                "ready head-of-line prevented a later fitting job");

        auto released = scheduler.apply(ExecutionFinished{
            blockerDispatch->job, blockerWorker, 1, TerminalStatus::Succeeded});
        const auto largeDispatch = actions<Dispatch>(released);
        require(largeDispatch.size() == 1 &&
                    largeDispatch.front()->key == largeKey,
                "fit-aware bypass permanently starved the older job");
      });
}

void verifyBoundedGangReservation() {
  SchedulerLimits configured = limits(32, 4);
  configured.maximumFitBypasses = 2;
  configured.global.cpuSlots = 4;
  configured.classes[static_cast<std::size_t>(ResourceClass::General)] = {
      32, 4, 4, configured.global.memoryBytes};
  Scheduler scheduler{configured};
  constexpr ResourceDemand oneSlot{1, 1, ResourceClass::General};
  constexpr ResourceDemand allSlots{4, 1, ResourceClass::General};

  struct Running final {
    JobId job;
    WorkerInstanceId worker;
  };
  std::vector<Running> running;
  auto submitAndStart = [&](std::uint64_t number) {
    StepResult result =
        scheduler.apply(Submit{id<JobId>(60'000'000U + number),
                               key(60'000'000U + number),
                               id<SubscriberId>(61'000'000U + number),
                               id<ProjectionId>(62'000'000U + number),
                               generation(1),
                               Priority::VisibleResult,
                               oneSlot,
                               {},
                               Retention::CompleteForCache});
    const auto dispatched = actions<Dispatch>(result);
    require(dispatched.size() == 1,
            "one-slot reservation test job did not dispatch");
    Running execution{dispatched.front()->job,
                      id<WorkerInstanceId>(63'000'000U + number)};
    require(scheduler.apply(ExecutionStarted{execution.job, execution.worker})
                    .status == EventStatus::Applied,
            "one-slot reservation test job did not start");
    running.push_back(execution);
  };
  for (std::uint64_t number = 0; number < 4; ++number)
    submitAndStart(number);

  const EvaluationKey gangKey = key(64'000'000);
  StepResult gang = scheduler.apply(Submit{id<JobId>(64'000'000),
                                           gangKey,
                                           id<SubscriberId>(64'000'000),
                                           id<ProjectionId>(64'000'000),
                                           generation(1),
                                           Priority::VisibleResult,
                                           allSlots,
                                           {},
                                           Retention::CompleteForCache});
  require(actions<Dispatch>(gang).empty(),
          "gang job exceeded saturated resources");

  auto finish = [&](const Running &execution) {
    return scheduler.apply(ExecutionFinished{execution.job, execution.worker, 1,
                                             TerminalStatus::Succeeded});
  };
  require(actions<Dispatch>(finish(running[0])).empty(),
          "gang job dispatched before all slots were free");
  submitAndStart(10);
  require(actions<Dispatch>(finish(running[1])).empty(),
          "gang job dispatched before all slots were free");
  submitAndStart(11);
  require(scheduler.stats().resourceDrainReserved,
          "bounded fit bypasses did not reserve resources for a gang job");

  require(actions<Dispatch>(finish(running[2])).empty(),
          "reservation allowed a refill while draining resources");
  StepResult refill = scheduler.apply(Submit{id<JobId>(60'000'020),
                                             key(60'000'020),
                                             id<SubscriberId>(61'000'020),
                                             id<ProjectionId>(62'000'020),
                                             generation(1),
                                             Priority::VisibleResult,
                                             oneSlot,
                                             {},
                                             Retention::CompleteForCache});
  require(actions<Dispatch>(refill).empty(),
          "reservation dispatched newly admitted refill work");
  require(actions<Dispatch>(finish(running[3])).empty(),
          "gang job dispatched before all refill work completed");
  require(actions<Dispatch>(finish(running[4])).empty(),
          "gang job dispatched with one refill still running");
  StepResult drained = finish(running[5]);
  const auto dispatched = actions<Dispatch>(drained);
  require(dispatched.size() == 1 && dispatched.front()->key == gangKey &&
              !scheduler.stats().resourceDrainReserved,
          "resource drain did not dispatch the reserved gang job first");
}

void verifyClassLocalReservationUtilization() {
  const testkit::PropertyProfile profile = testkit::propertyProfile();
  testkit::checkProperty(
      "class-local reservation utilization", profile,
      [](testkit::Random &random, std::uint64_t iteration) {
        const std::size_t slots = 4U + random.next() % 13U;
        SchedulerLimits configured = limits(slots + 4U, slots);
        configured.maximumFitBypasses = 1;
        configured.global.cpuSlots = static_cast<std::uint32_t>(slots);
        const std::size_t geometry =
            static_cast<std::size_t>(ResourceClass::Geometry);
        const std::size_t python =
            static_cast<std::size_t>(ResourceClass::Python);
        configured.classes[geometry] = {slots + 4U, 1,
                                        static_cast<std::uint32_t>(slots),
                                        configured.global.memoryBytes};
        configured.classes[python] = {slots + 4U, slots,
                                      static_cast<std::uint32_t>(slots),
                                      configured.global.memoryBytes};
        Scheduler scheduler{configured};
        constexpr ResourceDemand geometryDemand{1, 1, ResourceClass::Geometry};
        constexpr ResourceDemand pythonDemand{1, 1, ResourceClass::Python};
        const std::uint64_t base = 70'000'000U + iteration * 32U;

        auto submit = [&](std::uint64_t offset, ResourceDemand resource) {
          return scheduler.apply(Submit{id<JobId>(base + offset),
                                        key(base + offset),
                                        id<SubscriberId>(base + offset),
                                        id<ProjectionId>(base + offset),
                                        generation(1),
                                        Priority::VisibleResult,
                                        resource,
                                        {},
                                        Retention::CompleteForCache});
        };
        StepResult blocker = submit(0, geometryDemand);
        const Dispatch *blockerDispatch = actions<Dispatch>(blocker).front();
        const WorkerInstanceId worker = id<WorkerInstanceId>(base);
        require(scheduler.apply(ExecutionStarted{blockerDispatch->job, worker})
                        .status == EventStatus::Applied,
                "class-local blocker did not start");

        const EvaluationKey reservedKey = key(base + 1U);
        require(actions<Dispatch>(submit(1, geometryDemand)).empty(),
                "class-local limit did not queue the reserved job");
        require(actions<Dispatch>(submit(2, pythonDemand)).size() == 1 &&
                    scheduler.stats().resourceDrainReserved,
                "fit bypass did not establish a class-local reservation");
        for (std::size_t offset = 1; offset < slots - 2U; ++offset) {
          require(actions<Dispatch>(submit(2U + offset, pythonDemand)).size() ==
                      1,
                  "reservation idled an unrelated resource class");
        }

        const std::uint64_t overflowOffset = slots;
        const EvaluationKey overflowKey = key(base + overflowOffset);
        require(actions<Dispatch>(submit(overflowOffset, pythonDemand)).empty(),
                "unrelated work consumed reserved global headroom");
        const SchedulerStats draining = scheduler.stats();
        require(draining.running == slots - 1U &&
                    draining.resourceDrainReserved,
                "class-local drain did not preserve exact global headroom");

        StepResult released = scheduler.apply(ExecutionFinished{
            blockerDispatch->job, worker, 1, TerminalStatus::Succeeded});
        const auto dispatched = actions<Dispatch>(released);
        require(dispatched.size() == 2 && dispatched[0]->key == reservedKey &&
                    dispatched[1]->key == overflowKey &&
                    !scheduler.stats().resourceDrainReserved,
                "class-local drain did not dispatch the reservation first");
      });
}

void verifyIndependentSubscribers() {
  Scheduler scheduler{limits(8, 1)};
  constexpr ResourceDemand resource{1, 64, ResourceClass::General};
  const EvaluationKey sharedKey = key(500);
  const ProjectionId projection = id<ProjectionId>(500);
  auto first = scheduler.apply(Submit{id<JobId>(500),
                                      sharedKey,
                                      id<SubscriberId>(500),
                                      projection,
                                      generation(1),
                                      Priority::VisibleResult,
                                      resource,
                                      {},
                                      Retention::CancelWhenUnobserved});
  const Dispatch *execution = actions<Dispatch>(first).front();
  const WorkerInstanceId worker = id<WorkerInstanceId>(500);
  require(scheduler.apply(ExecutionStarted{execution->job, worker}).status ==
              EventStatus::Applied,
          "shared execution did not start");
  auto second = scheduler.apply(Submit{id<JobId>(501),
                                       sharedKey,
                                       id<SubscriberId>(501),
                                       projection,
                                       generation(1),
                                       Priority::UserRequested,
                                       resource,
                                       {},
                                       Retention::CancelWhenUnobserved});
  const auto admissions = actions<Admitted>(second);
  require(admissions.size() == 1 && admissions.front()->sharedExecution &&
              admissions.front()->job == execution->job,
          "equal keys did not share the running execution");
  auto firstCancel = scheduler.apply(CancelSubscription{id<SubscriberId>(500)});
  require(actions<CancelExecution>(firstCancel).empty(),
          "one subscriber cancelled another subscriber's work");
  auto secondCancel =
      scheduler.apply(CancelSubscription{id<SubscriberId>(501)});
  require(actions<CancelExecution>(secondCancel).size() == 1,
          "unobserved shared work was not cancelled");
  auto cancellationRace =
      scheduler.apply(Submit{id<JobId>(502),
                             sharedKey,
                             id<SubscriberId>(502),
                             projection,
                             generation(1),
                             Priority::InteractivePreview,
                             resource,
                             {},
                             Retention::CancelWhenUnobserved});
  require(cancellationRace.status == EventStatus::RejectedInvalid &&
              actions<AdmissionRejected>(cancellationRace).front()->reason ==
                  AdmissionRejection::CancellationInFlight,
          "new subscriber attached to cancellation already in flight");
  require(scheduler
                  .apply(ExecutionFinished{execution->job, worker, 1,
                                           TerminalStatus::Cancelled})
                  .status == EventStatus::Applied,
          "shared cancellation did not terminate");
  auto unretiredRetry =
      scheduler.apply(Submit{id<JobId>(503),
                             sharedKey,
                             id<SubscriberId>(503),
                             projection,
                             generation(1),
                             Priority::InteractivePreview,
                             resource,
                             {},
                             Retention::CancelWhenUnobserved});
  require(unretiredRetry.status == EventStatus::RejectedInvalid &&
              actions<AdmissionRejected>(unretiredRetry).front()->reason ==
                  AdmissionRejection::RetirementRequired,
          "retry replaced a terminal attempt without explicit retirement");
  require(scheduler.apply(RetireJob{execution->job, sharedKey}).status ==
              EventStatus::Applied,
          "cancelled execution was not retired before retry");
  auto retry = scheduler.apply(Submit{id<JobId>(504),
                                      sharedKey,
                                      id<SubscriberId>(504),
                                      projection,
                                      generation(1),
                                      Priority::InteractivePreview,
                                      resource,
                                      {},
                                      Retention::CancelWhenUnobserved});
  const auto retryAdmission = actions<Admitted>(retry);
  require(retryAdmission.size() == 1 &&
              !retryAdmission.front()->sharedExecution &&
              actions<Dispatch>(retry).size() == 1,
          "cancelled key could not start a fresh execution");
}

void verifySupersededDependencyCannotResurrect() {
  Scheduler scheduler{limits(8, 1)};
  constexpr ResourceDemand resource{1, 64, ResourceClass::General};
  auto blocker = scheduler.apply(Submit{id<JobId>(600),
                                        key(600),
                                        id<SubscriberId>(600),
                                        id<ProjectionId>(600),
                                        generation(1),
                                        Priority::InteractivePreview,
                                        resource,
                                        {},
                                        Retention::CompleteForCache});
  require(!actions<Dispatch>(blocker).empty(),
          "race blocker was not dispatched");

  const EvaluationKey oldKey = key(601);
  const ProjectionId projection = id<ProjectionId>(601);
  auto old = scheduler.apply(Submit{id<JobId>(601),
                                    oldKey,
                                    id<SubscriberId>(601),
                                    projection,
                                    generation(1),
                                    Priority::Normal,
                                    resource,
                                    {},
                                    Retention::CancelWhenUnobserved});
  require(actions<Dispatch>(old).empty() &&
              scheduler.state(oldKey) == JobState::Queued,
          "superseded dependency setup was not queued");

  const EvaluationKey replacementKey = key(602);
  auto replacement = scheduler.apply(Submit{id<JobId>(602),
                                            replacementKey,
                                            id<SubscriberId>(602),
                                            projection,
                                            generation(2),
                                            Priority::Normal,
                                            resource,
                                            {oldKey},
                                            Retention::CancelWhenUnobserved});
  require(scheduler.state(oldKey) == JobState::Superseded &&
              scheduler.state(replacementKey) == JobState::DependencyFailed &&
              actions<Dispatch>(replacement).empty(),
          "supersession resurrected a dependency-failed replacement");
}

std::vector<Dispatch> collectDispatches(const StepResult &result) {
  std::vector<Dispatch> collected;
  for (const Dispatch *dispatch : actions<Dispatch>(result))
    collected.push_back(*dispatch);
  return collected;
}

std::unordered_set<EvaluationKey, KeyHash>
runDAG(const std::vector<std::uint64_t> &order) {
  Scheduler scheduler{limits(order.size() + 8U, 4)};
  std::vector<Dispatch> pending;
  for (const std::uint64_t number : order) {
    std::vector<EvaluationKey> inputs;
    if (number > 0)
      inputs.push_back(key(number - 1U));
    if (number > 2)
      inputs.push_back(key(number - 3U));
    StepResult submitted = scheduler.apply(Submit{
        id<JobId>(10'000U + number), key(number),
        id<SubscriberId>(20'000U + number), id<ProjectionId>(30'000U + number),
        generation(1), static_cast<Priority>(number % priorityCount),
        demand(number), std::move(inputs), Retention::CompleteForCache});
    auto dispatched = collectDispatches(submitted);
    pending.insert(pending.end(), dispatched.begin(), dispatched.end());
  }

  std::unordered_set<EvaluationKey, KeyHash> succeeded;
  std::uint64_t workerNumber = 40'000;
  while (!pending.empty()) {
    Dispatch dispatch = pending.back();
    pending.pop_back();
    const WorkerInstanceId worker = id<WorkerInstanceId>(workerNumber++);
    require(scheduler.apply(ExecutionStarted{dispatch.job, worker}).status ==
                EventStatus::Applied,
            "convergence worker did not start");
    StepResult finished = scheduler.apply(
        ExecutionFinished{dispatch.job, worker, 1, TerminalStatus::Succeeded});
    succeeded.insert(dispatch.key);
    auto dispatched = collectDispatches(finished);
    pending.insert(pending.end(), dispatched.begin(), dispatched.end());
    StepResult available = scheduler.apply(ArtifactAvailable{dispatch.key});
    dispatched = collectDispatches(available);
    pending.insert(pending.end(), dispatched.begin(), dispatched.end());
  }
  require(succeeded.size() == order.size(),
          "valid dependency DAG did not converge");
  return succeeded;
}

void verifyInterleavingConvergence() {
  constexpr std::uint64_t count = 256;
  std::vector<std::uint64_t> forward;
  forward.reserve(count);
  for (std::uint64_t number = 0; number < count; ++number)
    forward.push_back(number);
  std::vector<std::uint64_t> interleaved;
  interleaved.reserve(count);
  for (std::uint64_t number = 1; number < count; number += 2)
    interleaved.push_back(count - number);
  for (std::uint64_t number = 0; number < count; number += 2)
    interleaved.push_back(number);
  require(runDAG(forward) == runDAG(interleaved),
          "valid event interleavings produced different artifacts");
}

void verifyCycleAndBounds() {
  SchedulerLimits configured = limits(4, 1);
  configured.subscribers = 4;
  configured.dependenciesPerJob = 1;
  configured.dependencyEdges = 2;
  Scheduler scheduler{configured};
  const auto first = scheduler.apply(Submit{id<JobId>(100),
                                            key(100),
                                            id<SubscriberId>(100),
                                            id<ProjectionId>(100),
                                            generation(1),
                                            Priority::Normal,
                                            demand(100),
                                            {key(101)},
                                            Retention::CancelWhenUnobserved});
  require(first.status == EventStatus::Applied,
          "forward dependency was not admitted");
  const auto cycle = scheduler.apply(Submit{id<JobId>(101),
                                            key(101),
                                            id<SubscriberId>(101),
                                            id<ProjectionId>(101),
                                            generation(1),
                                            Priority::Normal,
                                            demand(101),
                                            {key(100)},
                                            Retention::CancelWhenUnobserved});
  require(cycle.status == EventStatus::RejectedInvalid &&
              actions<AdmissionRejected>(cycle).front()->reason ==
                  AdmissionRejection::DependencyCycle,
          "dependency cycle was admitted");
}

void verifyProjectionAndJobChurn() {
  constexpr std::size_t cycles = 10'000;
  SchedulerLimits configured = limits(3, 1);
  configured.subscribers = 3;
  configured.projections = 2;
  Scheduler scheduler{configured};
  constexpr ResourceDemand resource{1, 64, ResourceClass::General};
  const Generation firstGeneration = generation(1);
  auto blocker = scheduler.apply(Submit{id<JobId>(700'000),
                                        key(700'000),
                                        id<SubscriberId>(700'000),
                                        id<ProjectionId>(700'000),
                                        firstGeneration,
                                        Priority::InteractivePreview,
                                        resource,
                                        {},
                                        Retention::CompleteForCache});
  const Dispatch *running = actions<Dispatch>(blocker).front();
  require(scheduler
                  .apply(ExecutionStarted{running->job,
                                          id<WorkerInstanceId>(700'000)})
                  .status == EventStatus::Applied,
          "churn blocker did not start");

  for (std::size_t cycle = 0; cycle < cycles; ++cycle) {
    const EvaluationKey currentKey = key(710'000U + cycle);
    const SubscriberId subscriber = id<SubscriberId>(720'000U + cycle);
    const ProjectionId projection = id<ProjectionId>(730'000U + cycle);
    auto submitted = scheduler.apply(Submit{id<JobId>(740'000U + cycle),
                                            currentKey,
                                            subscriber,
                                            projection,
                                            firstGeneration,
                                            Priority::Normal,
                                            resource,
                                            {},
                                            Retention::CancelWhenUnobserved});
    require(submitted.status == EventStatus::Applied &&
                actions<Dispatch>(submitted).empty(),
            "churn job was not queued");
    require(
        scheduler.apply(RetireProjection{projection, generation(2), currentKey})
                .status == EventStatus::IgnoredStale,
        "stale projection retirement changed currentness");
    require(scheduler
                    .apply(RetireProjection{projection, firstGeneration,
                                            currentKey})
                    .status == EventStatus::RejectedInvalid,
            "active projection was retired");
    require(scheduler.apply(CancelSubscription{subscriber}).status ==
                EventStatus::Applied,
            "queued churn job did not cancel");
    require(scheduler
                    .apply(RetireProjection{projection, firstGeneration,
                                            currentKey})
                    .status == EventStatus::Applied,
            "inactive matching projection was not retired");
    require(scheduler
                    .apply(RetireProjection{projection, firstGeneration,
                                            currentKey})
                    .status == EventStatus::Idempotent,
            "replayed projection retirement was not idempotent");
    require(scheduler.apply(RetireJob{id<JobId>(740'000U + cycle), currentKey})
                    .status == EventStatus::Applied,
            "terminal churn job was not retired");
    const SchedulerStats state = scheduler.stats();
    require(state.jobs == 1 && state.subscribers == 1 &&
                state.projections == 1 && state.running == 1,
            "bounded churn did not reclaim scheduler identities");
  }
}

void verifyArtifactHandoffAndSubscriberOwnership() {
  SchedulerLimits configured = limits(8, 2);
  configured.availableResults = 1;
  Scheduler scheduler{configured};
  constexpr ResourceDemand resource{1, 64, ResourceClass::General};
  const EvaluationKey sourceKey = key(750'000);
  const JobId sourceJob = id<JobId>(750'000);
  const ProjectionId sourceProjection = id<ProjectionId>(750'000);
  auto source = scheduler.apply(Submit{sourceJob,
                                       sourceKey,
                                       id<SubscriberId>(750'000),
                                       sourceProjection,
                                       generation(1),
                                       Priority::Normal,
                                       resource,
                                       {},
                                       Retention::CompleteForCache});
  const Dispatch *dispatch = actions<Dispatch>(source).front();
  const WorkerInstanceId worker = id<WorkerInstanceId>(750'000);
  require(scheduler.apply(ExecutionStarted{dispatch->job, worker}).status ==
                  EventStatus::Applied &&
              scheduler
                      .apply(ExecutionFinished{dispatch->job, worker, 1,
                                               TerminalStatus::Succeeded})
                      .status == EventStatus::Applied,
          "artifact handoff source did not succeed");
  require(scheduler.apply(RetireProjection{sourceProjection, generation(1),
                                           sourceKey})
                      .status == EventStatus::Applied &&
              scheduler.stats().subscribers == 1,
          "projection retirement reclaimed subscriber lifecycle state");
  require(scheduler.apply(RetireJob{sourceJob, sourceKey}).status ==
              EventStatus::RejectedInvalid,
          "successful job retired before external artifact handoff");
  const EvaluationKey firstDependentKey = key(750'001);
  auto beforeHandoff = scheduler.apply(Submit{id<JobId>(750'001),
                                              firstDependentKey,
                                              id<SubscriberId>(750'001),
                                              id<ProjectionId>(750'001),
                                              generation(1),
                                              Priority::Normal,
                                              resource,
                                              {sourceKey},
                                              Retention::CancelWhenUnobserved});
  require(actions<Dispatch>(beforeHandoff).empty() &&
              scheduler.state(firstDependentKey) == JobState::WaitingForInputs,
          "successful finish bypassed artifact reachability handoff");
  auto firstHandoff = scheduler.apply(ArtifactAvailable{sourceKey});
  require(actions<Dispatch>(firstHandoff).size() == 1,
          "artifact handoff did not release an existing dependent");
  require(scheduler.apply(ForgetArtifactAvailability{sourceKey}).status ==
                  EventStatus::Applied &&
              scheduler.stats().availableResults == 0,
          "artifact-cache eviction did not reclaim availability state");

  const EvaluationKey secondDependentKey = key(750'002);
  auto afterEviction = scheduler.apply(Submit{id<JobId>(750'002),
                                              secondDependentKey,
                                              id<SubscriberId>(750'002),
                                              id<ProjectionId>(750'002),
                                              generation(1),
                                              Priority::Normal,
                                              resource,
                                              {sourceKey},
                                              Retention::CancelWhenUnobserved});
  require(actions<Dispatch>(afterEviction).empty() &&
              scheduler.state(secondDependentKey) == JobState::WaitingForInputs,
          "retained success bypassed cache eviction");
  auto secondHandoff = scheduler.apply(ArtifactAvailable{sourceKey});
  require(actions<Dispatch>(secondHandoff).size() == 1 &&
              scheduler.apply(RetireJob{sourceJob, sourceKey}).status ==
                  EventStatus::Applied &&
              scheduler.stats().subscribers == 2,
          "re-handoff did not resume dependency or reclaim source subscriber");
  require(scheduler.apply(ForgetArtifactAvailability{sourceKey}).status ==
                  EventStatus::Applied &&
              scheduler.stats().availableResults == 0,
          "re-handoff availability did not evict");

  for (std::uint64_t cycle = 0; cycle < 10'000; ++cycle) {
    const EvaluationKey availableKey = key(760'000U + cycle);
    require(
        scheduler.apply(ArtifactAvailable{availableKey}).status ==
                EventStatus::Applied &&
            scheduler.apply(ArtifactAvailable{availableKey}).status ==
                EventStatus::Idempotent &&
            scheduler.apply(ForgetArtifactAvailability{availableKey}).status ==
                EventStatus::Applied &&
            scheduler.stats().availableResults == 0,
        "artifact availability churn exceeded its bounded ownership");
  }
}

void verifyLinearSharedSubscriberRetirement() {
  constexpr std::size_t sharedSubscribers = 100'000;
  SchedulerLimits configured = limits(4, 1);
  configured.subscribers = sharedSubscribers + 2U;
  configured.projections = 4;
  Scheduler scheduler{configured};
  constexpr ResourceDemand resource{1, 1, ResourceClass::General};
  const EvaluationKey sharedKey = key(80'000'000);
  const ProjectionId sharedProjection = id<ProjectionId>(80'000'000);
  const JobId sharedJob = id<JobId>(80'000'000);

  StepResult admitted = scheduler.apply(Submit{sharedJob,
                                               sharedKey,
                                               id<SubscriberId>(80'000'000),
                                               sharedProjection,
                                               generation(1),
                                               Priority::VisibleResult,
                                               resource,
                                               {},
                                               Retention::CompleteForCache});
  const Dispatch *dispatch = actions<Dispatch>(admitted).front();
  const WorkerInstanceId worker = id<WorkerInstanceId>(80'000'000);
  require(scheduler.apply(ExecutionStarted{dispatch->job, worker}).status ==
                  EventStatus::Applied &&
              scheduler
                      .apply(ExecutionFinished{dispatch->job, worker, 1,
                                               TerminalStatus::Succeeded})
                      .status == EventStatus::Applied,
          "shared-subscriber source did not complete");

  for (std::size_t number = 1; number < sharedSubscribers; ++number) {
    StepResult shared =
        scheduler.apply(Submit{id<JobId>(80'000'000U + number),
                               sharedKey,
                               id<SubscriberId>(80'000'000U + number),
                               sharedProjection,
                               generation(1),
                               Priority::Normal,
                               resource,
                               {},
                               Retention::CancelWhenUnobserved});
    require(shared.status == EventStatus::Applied &&
                actions<PublicationDecision>(shared).size() == 1,
            "terminal execution did not serve a shared subscriber");
  }

  const ProjectionId advancedProjection = id<ProjectionId>(81'000'000);
  require(scheduler
                  .apply(Submit{id<JobId>(81'000'000),
                                sharedKey,
                                id<SubscriberId>(81'000'000),
                                advancedProjection,
                                generation(1),
                                Priority::Normal,
                                resource,
                                {},
                                Retention::CancelWhenUnobserved})
                  .status == EventStatus::Applied,
          "shared job did not attach the projection later advanced");
  const EvaluationKey advancedKey = key(81'000'001);
  require(scheduler
                  .apply(Submit{id<JobId>(81'000'001),
                                advancedKey,
                                id<SubscriberId>(81'000'001),
                                advancedProjection,
                                generation(2),
                                Priority::VisibleResult,
                                resource,
                                {},
                                Retention::CancelWhenUnobserved})
                  .status == EventStatus::Applied,
          "newer projection was not admitted");
  require(scheduler.apply(ArtifactAvailable{sharedKey}).status ==
                  EventStatus::Applied &&
              scheduler.apply(RetireJob{sharedJob, sharedKey}).status ==
                  EventStatus::Applied,
          "shared terminal job did not retire");
  const SchedulerStats retired = scheduler.stats();
  require(
      retired.jobs == 1 && retired.subscribers == 1 &&
          scheduler.isCurrent(advancedProjection, generation(2), advancedKey) &&
          scheduler
                  .apply(RetireProjection{sharedProjection, generation(1),
                                          sharedKey})
                  .status == EventStatus::Applied,
      "linear retirement damaged or retained projection state");
}

std::vector<JobId> dependentFailureOrder(std::size_t capacity) {
  Scheduler scheduler{limits(capacity, 1)};
  constexpr ResourceDemand resource{1, 64, ResourceClass::General};
  const EvaluationKey rootKey = key(800'000);
  auto root = scheduler.apply(Submit{id<JobId>(800'000),
                                     rootKey,
                                     id<SubscriberId>(800'000),
                                     id<ProjectionId>(800'000),
                                     generation(1),
                                     Priority::Normal,
                                     resource,
                                     {},
                                     Retention::CompleteForCache});
  const Dispatch *running = actions<Dispatch>(root).front();
  const WorkerInstanceId worker = id<WorkerInstanceId>(800'000);
  require(scheduler.apply(ExecutionStarted{running->job, worker}).status ==
              EventStatus::Applied,
          "determinism root did not start");
  for (std::uint64_t number = 0; number < 32; ++number) {
    require(scheduler
                    .apply(Submit{id<JobId>(810'000U + number),
                                  key(810'000U + number),
                                  id<SubscriberId>(810'000U + number),
                                  id<ProjectionId>(810'000U + number),
                                  generation(1),
                                  Priority::Normal,
                                  resource,
                                  {rootKey},
                                  Retention::CancelWhenUnobserved})
                    .status == EventStatus::Applied,
            "determinism dependent was not admitted");
  }
  StepResult failed = scheduler.apply(
      ExecutionFinished{running->job, worker, 1, TerminalStatus::Failed});
  std::vector<JobId> order;
  for (const JobTerminal *terminal : actions<JobTerminal>(failed)) {
    if (terminal->state == JobState::DependencyFailed)
      order.push_back(terminal->job);
  }
  return order;
}

void verifyDeterministicDependentOrder() {
  const std::vector<JobId> compact = dependentFailureOrder(64);
  const std::vector<JobId> sparse = dependentFailureOrder(4'096);
  require(compact == sparse && compact.size() == 32,
          "dependent action order changed with hash-table capacity");
  require(std::ranges::is_sorted(compact),
          "dependents did not follow stable admission order");
}

void verifyDeepFailureCascade() {
  constexpr std::uint64_t depth = 100'000;
  Scheduler scheduler{limits(static_cast<std::size_t>(depth + 2U), 1)};
  constexpr ResourceDemand resource{1, 0, ResourceClass::General};
  for (std::uint64_t number = 0; number < depth; ++number) {
    require(scheduler
                    .apply(Submit{id<JobId>(1'000'000U + number),
                                  key(number),
                                  id<SubscriberId>(2'000'000U + number),
                                  id<ProjectionId>(3'000'000U + number),
                                  generation(1),
                                  Priority::Normal,
                                  resource,
                                  {key(number + 1U)},
                                  Retention::CancelWhenUnobserved})
                    .status == EventStatus::Applied,
            "deep dependency node was not admitted");
  }
  auto root = scheduler.apply(Submit{id<JobId>(1'000'000U + depth),
                                     key(depth),
                                     id<SubscriberId>(2'000'000U + depth),
                                     id<ProjectionId>(3'000'000U + depth),
                                     generation(1),
                                     Priority::Normal,
                                     resource,
                                     {},
                                     Retention::CancelWhenUnobserved});
  const Dispatch *dispatch = actions<Dispatch>(root).front();
  const WorkerInstanceId worker = id<WorkerInstanceId>(4'000'000);
  require(scheduler.apply(ExecutionStarted{dispatch->job, worker}).status ==
              EventStatus::Applied,
          "deep dependency root did not start");
  StepResult failed = scheduler.apply(
      ExecutionFinished{dispatch->job, worker, 1, TerminalStatus::Failed});
  const SchedulerStats state = scheduler.stats();
  require(state.terminal == depth + 1U && state.dependencyEdges == 0 &&
              state.running == 0 &&
              actions<JobTerminal>(failed).size() == depth + 1U,
          "deep dependency failure did not converge iteratively");
}

void verifyGeneratedInvalidBoundaries() {
  require(!Generation::create(0), "zero evaluation generation was accepted");
  Scheduler scheduler{limits(4, 1)};
  testkit::Random random{0x696e76616c6964ULL};
  for (std::uint64_t iteration = 0; iteration < 10'000; ++iteration) {
    const auto invalidPriority = static_cast<Priority>(
        priorityCount + random.next() % (256U - priorityCount));
    const auto invalidRetention =
        static_cast<Retention>(2U + random.next() % 254U);
    auto priorityResult =
        scheduler.apply(Submit{id<JobId>(5'000'000U + iteration),
                               key(5'000'000U + iteration),
                               id<SubscriberId>(6'000'000U + iteration),
                               id<ProjectionId>(7'000'000U + iteration),
                               generation(1),
                               invalidPriority,
                               demand(0),
                               {},
                               Retention::CancelWhenUnobserved});
    require(priorityResult.status == EventStatus::RejectedInvalid,
            "invalid priority reached a scheduling index");
    auto retentionResult =
        scheduler.apply(Submit{id<JobId>(8'000'000U + iteration),
                               key(8'000'000U + iteration),
                               id<SubscriberId>(9'000'000U + iteration),
                               id<ProjectionId>(10'000'000U + iteration),
                               generation(1),
                               Priority::Normal,
                               demand(0),
                               {},
                               invalidRetention});
    require(retentionResult.status == EventStatus::RejectedInvalid,
            "invalid retention policy was accepted");
  }
  require(scheduler.apply(AdvanceTime{{10}}).status == EventStatus::Applied &&
              scheduler.apply(AdvanceTime{{9}}).status ==
                  EventStatus::IgnoredStale,
          "logical time regressed");

  SchedulerLimits extreme = limits(65'540, 65'539);
  extreme.global.cpuSlots = std::numeric_limits<std::uint32_t>::max();
  extreme.global.concurrentJobs = 65'539;
  extreme.classes[static_cast<std::size_t>(ResourceClass::General)].cpuSlots =
      std::numeric_limits<std::uint32_t>::max();
  extreme.classes[static_cast<std::size_t>(ResourceClass::General)]
      .concurrentJobs = 65'539;
  Scheduler saturated{extreme};
  constexpr ResourceDemand maximumDemand{
      std::numeric_limits<std::uint16_t>::max(), 0, ResourceClass::General};
  for (std::uint64_t number = 0; number < 65'538; ++number) {
    require(saturated
                    .apply(Submit{id<JobId>(20'000'000U + number),
                                  key(20'000'000U + number),
                                  id<SubscriberId>(30'000'000U + number),
                                  id<ProjectionId>(40'000'000U + number),
                                  generation(1),
                                  Priority::Normal,
                                  maximumDemand,
                                  {},
                                  Retention::CompleteForCache})
                    .status == EventStatus::Applied,
            "extreme resource job was not admitted");
  }
  const SchedulerStats saturatedState = saturated.stats();
  require(saturatedState.running == 65'537 && saturatedState.queued == 1 &&
              saturatedState.usedCpuSlots ==
                  std::numeric_limits<std::uint32_t>::max(),
          "CPU resource arithmetic overflowed at its maximum envelope");
  const JobId running = id<JobId>(20'000'000);
  const WorkerInstanceId worker = id<WorkerInstanceId>(20'000'000);
  require(saturated.apply(ExecutionStarted{running, worker}).status ==
              EventStatus::Applied,
          "extreme resource worker did not start");
  require(saturated
                  .apply(ExecutionFinished{running, worker, 1,
                                           static_cast<TerminalStatus>(255)})
                  .status == EventStatus::RejectedInvalid,
          "invalid terminal status mutated scheduler state");
}

} // namespace

int main() {
  try {
    verifyMixedStateMachine();
    verifyAgingAndCancellationRace();
    verifyGeneratedFitAwareSelection();
    verifyBoundedGangReservation();
    verifyClassLocalReservationUtilization();
    verifyIndependentSubscribers();
    verifySupersededDependencyCannotResurrect();
    verifyInterleavingConvergence();
    verifyCycleAndBounds();
    verifyProjectionAndJobChurn();
    verifyArtifactHandoffAndSubscriberOwnership();
    verifyLinearSharedSubscriberRetirement();
    verifyDeterministicDependentOrder();
    verifyDeepFailureCascade();
    verifyGeneratedInvalidBoundaries();
    std::cout << "evaluation scheduler properties passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
