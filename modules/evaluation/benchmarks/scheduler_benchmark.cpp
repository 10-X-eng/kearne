#include <kearne/evaluation/scheduler.hpp>
#include <kearne/testkit/distribution.hpp>

#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace kearne;
using namespace kearne::evaluation;

constexpr std::size_t priorityCount = static_cast<std::size_t>(Priority::Count);

void require(bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}

template <typename Id> Id id(std::uint64_t value) {
  typename Id::RandomTail random{};
  for (std::size_t byte = 0; byte < random.size(); ++byte)
    random[byte] =
        static_cast<std::uint8_t>((value >> ((byte % 8U) * 8U)) + byte * 13U);
  auto result = Id::create(value & ((std::uint64_t{1} << 48U) - 1U), random);
  require(result.has_value(), "benchmark ID is invalid");
  return *result;
}

EvaluationKey key(std::uint64_t value) {
  EvaluationKey::Bytes bytes{};
  for (std::size_t byte = 0; byte < bytes.size(); ++byte)
    bytes[byte] =
        static_cast<std::uint8_t>((value >> ((byte % 8U) * 8U)) + byte * 31U);
  auto result = EvaluationKey::fromBytes("blake3", bytes);
  require(result.has_value(), "benchmark evaluation key is invalid");
  return *result;
}

Generation generation(std::uint64_t value) {
  auto result = Generation::create(value);
  require(result.has_value(), "benchmark generation is invalid");
  return *result;
}

SchedulerLimits limits(std::size_t queued, std::size_t samples) {
  SchedulerLimits result;
  result.jobs = queued + 1U;
  result.subscribers = queued + samples + 1U;
  result.projections = queued + samples + 1U;
  result.availableResults = queued + 1U;
  result.dependenciesPerJob = 2;
  result.dependencyEdges = queued * 2U;
  result.global = {queued + 1U, 1, 1, 4096};
  for (ResourceLimit &resource : result.classes)
    resource = {queued + 1U, 1, 1, 4096};
  result.agingIntervalTicks = 100;
  result.maximumAgePromotions = static_cast<std::uint8_t>(Priority::Count) - 1U;
  result.fitSearchLimit = 64;
  return result;
}

bool hasDispatch(const StepResult &result) {
  for (const Action &action : result.actions) {
    if (std::holds_alternative<Dispatch>(action))
      return true;
  }
  return false;
}

const Dispatch &dispatch(const StepResult &result) {
  for (const Action &action : result.actions) {
    if (const auto *value = std::get_if<Dispatch>(&action))
      return *value;
  }
  throw std::runtime_error("benchmark blocker was not dispatched");
}

void report(std::size_t scale, const char *boundary,
            const testkit::Distribution &distribution) {
  std::cout << scale << ' ' << boundary << ": samples=" << distribution.samples
            << " p50=" << std::fixed << std::setprecision(3) << distribution.p50
            << " us p95=" << distribution.p95 << " us p99=" << distribution.p99
            << " us max=" << distribution.maximum
            << " us sigma=" << distribution.populationStandardDeviation
            << " us\n";
}

void benchmark(std::size_t queued) {
  constexpr std::size_t samples = 2'001;
  constexpr ResourceDemand resource{1, 64, ResourceClass::General};
  Scheduler scheduler{limits(queued, samples)};
  require(scheduler.valid(), "benchmark limits are invalid");

  StepResult blocker = scheduler.apply(Submit{id<JobId>(1),
                                              key(0),
                                              id<SubscriberId>(1),
                                              id<ProjectionId>(1),
                                              generation(1),
                                              Priority::InteractivePreview,
                                              resource,
                                              {},
                                              Retention::CompleteForCache});
  const Dispatch running = dispatch(blocker);
  require(
      scheduler.apply(ExecutionStarted{running.job, id<WorkerInstanceId>(1)})
              .status == EventStatus::Applied,
      "benchmark blocker did not start");

  std::vector<double> admissionMicroseconds;
  admissionMicroseconds.reserve(queued);
  std::vector<EvaluationKey> currentKeys;
  std::vector<JobId> currentJobs;
  std::vector<SubscriberId> currentSubscribers;
  std::vector<ProjectionId> currentProjections;
  currentKeys.reserve(queued);
  currentJobs.reserve(queued);
  currentSubscribers.reserve(queued);
  currentProjections.reserve(queued);
  for (std::size_t index = 0; index < queued; ++index) {
    const EvaluationKey submittedKey = key(index + 1U);
    const JobId submittedJob = id<JobId>(10U + index);
    const SubscriberId submittedSubscriber = id<SubscriberId>(100'000U + index);
    const ProjectionId submittedProjection = id<ProjectionId>(200'000U + index);
    const auto start = std::chrono::steady_clock::now();
    StepResult result =
        scheduler.apply(Submit{submittedJob,
                               submittedKey,
                               submittedSubscriber,
                               submittedProjection,
                               generation(1),
                               static_cast<Priority>(index % priorityCount),
                               resource,
                               {},
                               Retention::CancelWhenUnobserved});
    const auto end = std::chrono::steady_clock::now();
    require(result.status == EventStatus::Applied && !hasDispatch(result),
            "queued admission violated the blocker/resource contract");
    admissionMicroseconds.push_back(
        std::chrono::duration<double, std::micro>(end - start).count());
    currentKeys.push_back(submittedKey);
    currentJobs.push_back(submittedJob);
    currentSubscribers.push_back(submittedSubscriber);
    currentProjections.push_back(submittedProjection);
  }
  SchedulerStats state = scheduler.stats();
  require(state.queued == queued && state.running == 1U &&
              state.usedCpuSlots == 1U && state.usedMemoryBytes == 64U,
          "prefilled queue failed correctness checks");

  std::vector<double> churnMicroseconds;
  churnMicroseconds.reserve(samples);
  for (std::size_t sample = 0; sample < samples; ++sample) {
    const std::size_t target = sample % queued;
    const std::uint64_t unique = queued * samples + sample;
    const EvaluationKey replacementKey = key(10'000'000U + unique);
    const SubscriberId replacementSubscriber =
        id<SubscriberId>(20'000'000U + unique);
    const ProjectionId replacementProjection =
        id<ProjectionId>(30'000'000U + unique);
    const auto start = std::chrono::steady_clock::now();
    StepResult cancelled =
        scheduler.apply(CancelSubscription{currentSubscribers[target]});
    StepResult retiredJob =
        scheduler.apply(RetireJob{currentJobs[target], currentKeys[target]});
    StepResult retiredProjection = scheduler.apply(RetireProjection{
        currentProjections[target], generation(1), currentKeys[target]});
    StepResult replacement = scheduler.apply(
        Submit{id<JobId>(40'000'000U + unique),
               replacementKey,
               replacementSubscriber,
               replacementProjection,
               generation(1),
               static_cast<Priority>((sample * 5U) % priorityCount),
               resource,
               {},
               Retention::CancelWhenUnobserved});
    const auto end = std::chrono::steady_clock::now();
    require(cancelled.status == EventStatus::Applied &&
                retiredJob.status == EventStatus::Applied &&
                retiredProjection.status == EventStatus::Applied &&
                replacement.status == EventStatus::Applied &&
                !hasDispatch(replacement),
            "full-queue churn failed cancellation/reclamation");
    currentKeys[target] = replacementKey;
    currentJobs[target] = id<JobId>(40'000'000U + unique);
    currentSubscribers[target] = replacementSubscriber;
    currentProjections[target] = replacementProjection;
    churnMicroseconds.push_back(
        std::chrono::duration<double, std::micro>(end - start).count());
  }
  state = scheduler.stats();
  require(state.jobs == queued + 1U && state.queued == queued &&
              state.subscribers == queued + 1U &&
              state.projections == queued + 1U,
          "churn did not reclaim bounded scheduler state");

  std::vector<double> eventMicroseconds;
  eventMicroseconds.reserve(samples);
  for (std::size_t sample = 0; sample < samples; ++sample) {
    const std::size_t target = (sample * 0x9e3779b9ULL + queued / 2U) % queued;
    Submit shared{id<JobId>(1'000'000U + sample),
                  currentKeys[target],
                  id<SubscriberId>(2'000'000U + queued + sample),
                  id<ProjectionId>(3'000'000U + queued + sample),
                  generation(1),
                  static_cast<Priority>((sample * 5U) % priorityCount),
                  resource,
                  {},
                  Retention::CancelWhenUnobserved};
    const auto start = std::chrono::steady_clock::now();
    StepResult result = scheduler.apply(shared);
    const auto end = std::chrono::steady_clock::now();
    require(result.status == EventStatus::Applied && !hasDispatch(result),
            "full-queue event changed resource occupancy");
    bool sharedExecution = false;
    for (const Action &action : result.actions) {
      if (const auto *admitted = std::get_if<Admitted>(&action))
        sharedExecution = admitted->sharedExecution;
    }
    require(sharedExecution &&
                scheduler.isCurrent(shared.projection, shared.generation,
                                    shared.key),
            "full-queue event failed deduplication/currentness checks");
    eventMicroseconds.push_back(
        std::chrono::duration<double, std::micro>(end - start).count());
  }

  state = scheduler.stats();
  require(state.jobs == queued + 1U && state.queued == queued &&
              state.running == 1U,
          "timed events changed the queued population");
  const auto admission = testkit::summarizeDistribution(admissionMicroseconds);
  const auto churn = testkit::summarizeDistribution(churnMicroseconds);
  const auto events = testkit::summarizeDistribution(eventMicroseconds);
  require(admission && churn && events, "benchmark distribution is invalid");
  report(queued, "queued admission", *admission);
  report(queued, "queued cancel/retire/replace churn", *churn);
  report(queued, "queued deduplicated priority event", *events);
}

void benchmarkSharedRetirement(std::size_t subscriberCount) {
  SchedulerLimits configured = limits(1, subscriberCount);
  configured.subscribers = subscriberCount;
  configured.projections = 1;
  Scheduler scheduler{configured};
  constexpr ResourceDemand resource{1, 1, ResourceClass::General};
  const EvaluationKey sharedKey = key(90'000'000);
  const JobId sharedJob = id<JobId>(90'000'000);
  const ProjectionId projection = id<ProjectionId>(90'000'000);
  StepResult admitted = scheduler.apply(Submit{sharedJob,
                                               sharedKey,
                                               id<SubscriberId>(90'000'000),
                                               projection,
                                               generation(1),
                                               Priority::Normal,
                                               resource,
                                               {},
                                               Retention::CompleteForCache});
  const Dispatch running = dispatch(admitted);
  const WorkerInstanceId worker = id<WorkerInstanceId>(90'000'000);
  require(scheduler.apply(ExecutionStarted{running.job, worker}).status ==
                  EventStatus::Applied &&
              scheduler
                      .apply(ExecutionFinished{running.job, worker, 1,
                                               TerminalStatus::Succeeded})
                      .status == EventStatus::Applied &&
              scheduler.apply(ArtifactAvailable{sharedKey}).status ==
                  EventStatus::Applied,
          "shared-retirement source did not complete");
  for (std::size_t index = 1; index < subscriberCount; ++index) {
    require(scheduler
                    .apply(Submit{id<JobId>(90'000'000U + index),
                                  sharedKey,
                                  id<SubscriberId>(90'000'000U + index),
                                  projection,
                                  generation(1),
                                  Priority::Normal,
                                  resource,
                                  {},
                                  Retention::CancelWhenUnobserved})
                    .status == EventStatus::Applied,
            "shared-retirement subscriber was not admitted");
  }
  require(scheduler.stats().subscribers == subscriberCount,
          "shared-retirement population is incomplete");
  const auto start = std::chrono::steady_clock::now();
  StepResult retired = scheduler.apply(RetireJob{sharedJob, sharedKey});
  const auto end = std::chrono::steady_clock::now();
  require(
      retired.status == EventStatus::Applied && scheduler.stats().jobs == 0 &&
          scheduler.stats().subscribers == 0 &&
          scheduler
                  .apply(RetireProjection{projection, generation(1), sharedKey})
                  .status == EventStatus::Applied,
      "shared-retirement did not reclaim scheduler state");
  const std::vector<double> sample{
      std::chrono::duration<double, std::micro>(end - start).count()};
  const auto distribution = testkit::summarizeDistribution(sample);
  require(distribution.has_value(), "shared-retirement timing is invalid");
  report(subscriberCount, "shared-subscriber retirement", *distribution);
}

} // namespace

int main() {
  try {
    for (const std::size_t queued : {1'000U, 10'000U, 100'000U})
      benchmark(queued);
    for (const std::size_t subscribers : {1'000U, 10'000U, 100'000U})
      benchmarkSharedRetirement(subscribers);
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
