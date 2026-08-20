#include "sketch_scene_publication.hpp"

#include <QEvent>
#include <QMetaObject>
#include <QThread>

#include <algorithm>
#include <array>
#include <condition_variable>
#include <limits>
#include <new>
#include <ranges>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace kearne::ui {
namespace {

[[nodiscard]] Diagnostic publicationDiagnostic(std::string code,
                                               std::string summary) {
  return diagnostic(std::move(code), std::move(summary));
}

[[nodiscard]] Result<void> requireThread(const QObject &owner,
                                         const QObject *peer = nullptr) {
  if (QThread::currentThread() != owner.thread() ||
      (peer && peer->thread() != owner.thread()))
    return std::unexpected(publicationDiagnostic(
        "desktop.sketch.publication-thread",
        "sketch publication must run on its owning UI thread"));
  return {};
}

template <typename Value> void updateMaximum(Value &maximum, Value value) {
  maximum = std::max(maximum, value);
}

struct SketchPublicationRetirementOwner {
  std::shared_ptr<const SketchSceneProducts> products;
  std::shared_ptr<const void> item;
};

[[nodiscard]] Result<std::shared_ptr<const void>>
retirementOwner(const std::shared_ptr<const SketchSceneProducts> &products,
                const SketchSceneItem &item) {
  auto retainedItem = item.retirementOwner();
  if (!retainedItem)
    return std::unexpected(std::move(retainedItem.error()));
  if (!products)
    return std::move(*retainedItem);
  if (!*retainedItem)
    return std::static_pointer_cast<const void>(products);
  try {
    return std::static_pointer_cast<const void>(
        std::make_shared<const SketchPublicationRetirementOwner>(
            SketchPublicationRetirementOwner{products,
                                             std::move(*retainedItem)}));
  } catch (const std::bad_alloc &) {
    return std::unexpected(publicationDiagnostic(
        "desktop.sketch.preparation-retirement-allocation",
        "sketch publication retirement allocation failed"));
  }
}

} // namespace

struct SketchPreparationExecutor::Impl {
  [[nodiscard]] static SketchPreparationLimits
  validateLimits(SketchPreparationLimits limits) {
    if (limits.maximumSubscriptions == 0U || limits.workerCount == 0U ||
        limits.maximumUiDeliveriesPerTurn == 0U)
      throw std::invalid_argument("sketch preparation limits must be positive");
    if (limits.maximumSubscriptions >
        static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
      throw std::invalid_argument("too many sketch preparation subscriptions");
    return limits;
  }

  using Request = SketchPreparationRequest;

  struct Completion {
    std::shared_ptr<const Request> request;
    Result<std::shared_ptr<const PreparedSketchProducts>> prepared;
  };

  struct Active {
    explicit Active(std::shared_ptr<const Request> activeRequest,
                    std::stop_source stop) noexcept
        : request(std::move(activeRequest)), cancellation(std::move(stop)) {}
    std::shared_ptr<const Request> request;
    std::stop_source cancellation;
  };

  struct Retirement {
    std::shared_ptr<const void> artifact;
    bool ready = false;
  };

  static_assert(std::is_nothrow_move_constructible_v<Completion>);
  static_assert(std::is_nothrow_move_constructible_v<Active>);

  enum class SlotState : std::uint8_t { Free, Leased, Retiring };

  struct Slot {
    SlotState state = SlotState::Free;
    std::uint64_t incarnation = 0;
    std::uint64_t nextEpoch = 0;
    std::uint64_t latestEpoch = 0;
    QObject *lifetime = nullptr;
    QMetaObject::Connection lifetimeConnection;
    SketchPreparationSink *sink = nullptr;
    std::shared_ptr<const Request> pending;
    std::optional<Active> active;
    std::optional<Completion> completion;
    std::optional<Completion> retiredDelivery;
    std::shared_ptr<const PreparedSketchProducts> reuse;
    std::array<Retirement,
               SketchPreparationLimits::maximumRetirementsPerSubscription>
        retirements;
    bool starting = false;
    bool workQueued = false;
    bool completionQueued = false;
    bool completionDelivering = false;
  };

  class Ring {
  public:
    explicit Ring(std::size_t capacity) : entries_(capacity) {}

    void push(std::uint32_t slot) {
      entries_[(head_ + size_) % entries_.size()] = slot;
      ++size_;
    }

    [[nodiscard]] std::uint32_t pop() {
      const std::uint32_t result = entries_[head_];
      head_ = (head_ + 1U) % entries_.size();
      --size_;
      return result;
    }

    [[nodiscard]] bool empty() const { return size_ == 0U; }
    [[nodiscard]] std::size_t size() const { return size_; }
    void clear() {
      head_ = 0;
      size_ = 0;
    }

  private:
    std::vector<std::uint32_t> entries_;
    std::size_t head_ = 0;
    std::size_t size_ = 0;
  };

  class IterationGuard {
  public:
    explicit IterationGuard(Impl &implementation)
        : implementation_(implementation) {}
    ~IterationGuard() {
      if (!active_)
        return;
      std::scoped_lock lock{implementation_.mutex};
      --implementation_.workerIterations;
      implementation_.drained.notify_all();
    }
    void activateLocked() {
      active_ = true;
      ++implementation_.workerIterations;
    }

  private:
    Impl &implementation_;
    bool active_ = false;
  };

  Impl(SketchPreparationExecutor &executorOwner,
       SketchPreparationLimits requested, PrepareFunction requestedPrepare,
       FaultFunction requestedFault)
      : executor(executorOwner), limits(validateLimits(requested)),
        entries(limits.maximumSubscriptions), work(limits.maximumSubscriptions),
        completions(limits.maximumSubscriptions),
        prepare(std::move(requestedPrepare)), fault(std::move(requestedFault)) {
    if (!prepare) {
      prepare = [](const SketchPreparationRequest &request,
                   std::shared_ptr<const PreparedSketchProducts> reuse,
                   std::stop_token stop) {
        return prepareSketchProducts(request.products, request.lod, {},
                                     std::move(reuse), stop);
      };
    }
    counters.workerCount = limits.workerCount;
    workers.reserve(limits.workerCount);
    for (std::size_t index = 0; index < limits.workerCount; ++index)
      workers.emplace_back([this](std::stop_token stop) { workerLoop(stop); });
  }

  [[nodiscard]] bool matches(SketchPreparationSubscription subscription,
                             const Slot &slot) const {
    return subscription.slot() < entries.size() &&
           slot.state == SlotState::Leased &&
           slot.incarnation == subscription.incarnation();
  }

  void enqueueWork(std::uint32_t index, Slot &slot) {
    if (slot.workQueued || slot.starting || slot.active ||
        slot.completionDelivering ||
        (slot.state == SlotState::Leased && slot.completion))
      return;
    slot.workQueued = true;
    work.push(index);
    changed.notify_one();
  }

  [[nodiscard]] std::optional<std::uint8_t>
  retireLocked(Slot &slot, std::shared_ptr<const void> artifact, bool ready) {
    if (!artifact)
      return std::uint8_t{0};
    for (std::size_t index = 0; index < slot.retirements.size(); ++index) {
      auto &entry = slot.retirements[index];
      if (entry.artifact)
        continue;
      entry.artifact = std::move(artifact);
      entry.ready = ready;
      ++counters.retirements;
      ++counters.pendingRetirements;
      updateMaximum(counters.maximumPendingRetirements,
                    counters.pendingRetirements);
      return static_cast<std::uint8_t>(index);
    }
    ++counters.backpressure;
    return std::nullopt;
  }

  void beginRetirementLocked(std::uint32_t index, Slot &slot) {
    if (slot.state != SlotState::Leased)
      return;
    slot.state = SlotState::Retiring;
    slot.sink = nullptr;
    slot.lifetime = nullptr;
    ++counters.unsubscriptions;
    if (slot.active)
      static_cast<void>(slot.active->cancellation.request_stop());
    if (!slot.starting && !slot.active)
      enqueueWork(index, slot);
  }

  void takeRetirementsLocked(
      Slot &slot,
      std::array<std::shared_ptr<const void>,
                 SketchPreparationLimits::maximumRetirementsPerSubscription>
          &destination) {
    for (std::size_t index = 0; index < slot.retirements.size(); ++index) {
      if (!slot.retirements[index].artifact ||
          (!slot.retirements[index].ready && slot.state != SlotState::Retiring))
        continue;
      destination[index] = std::move(slot.retirements[index].artifact);
      slot.retirements[index].ready = false;
      --counters.pendingRetirements;
    }
  }

  [[nodiscard]] bool queueCompletion(std::uint32_t index, Slot &slot) {
    if (!slot.completionQueued) {
      slot.completionQueued = true;
      completions.push(index);
    }
    if (uiDrainQueued)
      return false;
    uiDrainQueued = true;
    ++counters.uiDrainPosts;
    return true;
  }

  void postDrain() {
    try {
      if (QMetaObject::invokeMethod(
              &executor, [this] { executor.drainCompletions(); },
              Qt::QueuedConnection))
        return;
    } catch (...) {
    }

    std::scoped_lock lock{mutex};
    uiDrainQueued = false;
    stopping = true;
    retireAllLocked();
    changed.notify_all();
  }

  void retireAllLocked() {
    completions.clear();
    uiDrainQueued = false;
    for (std::uint32_t index = 0; index < entries.size(); ++index) {
      Slot &slot = entries[index];
      if (slot.state == SlotState::Free)
        continue;
      slot.state = SlotState::Retiring;
      slot.sink = nullptr;
      slot.lifetime = nullptr;
      QObject::disconnect(slot.lifetimeConnection);
      slot.lifetimeConnection = {};
      slot.completionQueued = false;
      if (slot.active)
        static_cast<void>(slot.active->cancellation.request_stop());
      if (!slot.starting && !slot.active)
        enqueueWork(index, slot);
    }
  }

  [[nodiscard]] bool drainedLocked() const {
    return counters.leasedSubscriptions == 0U &&
           counters.activePreparations == 0U &&
           counters.pendingPreparations == 0U &&
           counters.pendingCompletions == 0U &&
           counters.pendingRetirements == 0U && workerIterations == 0U &&
           uiDeliveriesActive == 0U && work.empty();
  }

  [[nodiscard]] Result<std::shared_ptr<const PreparedSketchProducts>>
  validatePrepared(
      const Request &request,
      Result<std::shared_ptr<const PreparedSketchProducts>> prepared) const {
    if (!prepared)
      return prepared;
    if (!*prepared)
      return std::unexpected(
          publicationDiagnostic("desktop.sketch.preparation-null-result",
                                "sketch preparation returned a null success"));
    if ((*prepared)->stamp() != request.products->stamp)
      return std::unexpected(publicationDiagnostic(
          "desktop.sketch.preparation-result-stamp",
          "prepared sketch product stamp does not match its request"));
    if ((*prepared)->source() != request.products)
      return std::unexpected(publicationDiagnostic(
          "desktop.sketch.preparation-result-instance",
          "prepared sketch products do not retain the exact request packet"));
    if ((*prepared)->lod() != request.lod)
      return std::unexpected(publicationDiagnostic(
          "desktop.sketch.preparation-result-lod",
          "prepared sketch product LOD does not match its request"));
    return prepared;
  }

  void workerLoop(std::stop_token stop) {
    for (;;) {
      IterationGuard iteration{*this};
      std::shared_ptr<const Request> request;
      std::stop_token cancellation;
      std::shared_ptr<const PreparedSketchProducts> reuse;
      std::optional<Completion> retiredCompletion;
      std::optional<Completion> retiredDelivery;
      std::shared_ptr<const Request> retiredRequest;
      std::shared_ptr<const PreparedSketchProducts> retiredReuse;
      std::array<std::shared_ptr<const void>,
                 SketchPreparationLimits::maximumRetirementsPerSubscription>
          retiredArtifacts;
      std::uint32_t index = 0;
      bool startPost = false;

      {
        std::unique_lock lock{mutex};
        changed.wait(lock, stop, [this] { return !work.empty(); });
        if (stop.stop_requested() && work.empty())
          return;
        if (work.empty())
          continue;

        index = work.pop();
        iteration.activateLocked();
        Slot &slot = entries[index];
        slot.workQueued = false;
        takeRetirementsLocked(slot, retiredArtifacts);
        if (slot.retiredDelivery) {
          retiredDelivery = std::move(slot.retiredDelivery);
          slot.retiredDelivery.reset();
        }

        if (slot.state == SlotState::Retiring) {
          if (slot.starting || slot.active)
            continue;
          if (slot.pending) {
            retiredRequest = std::move(slot.pending);
            --counters.pendingPreparations;
            ++counters.retiredPending;
          }
          if (slot.completion) {
            retiredCompletion = std::move(slot.completion);
            slot.completion.reset();
            --counters.pendingCompletions;
          }
          retiredReuse = std::move(slot.reuse);
          QObject::disconnect(slot.lifetimeConnection);
          slot.lifetimeConnection = {};
          if (slot.completionQueued) {
            drained.notify_all();
            continue;
          }
          slot.state = SlotState::Free;
          if (counters.leasedSubscriptions != 0U)
            --counters.leasedSubscriptions;
          drained.notify_all();
          continue;
        }

        if (!slot.pending) {
          drained.notify_all();
          continue;
        }

        if (slot.pending->epoch.value() != slot.latestEpoch) {
          retiredRequest = std::move(slot.pending);
          --counters.pendingPreparations;
          ++counters.retiredPending;
          drained.notify_all();
          continue;
        }

        request = std::move(slot.pending);
        --counters.pendingPreparations;
        reuse = slot.reuse;
        slot.starting = true;
      }

      std::optional<std::stop_source> cancellationSource;
      try {
        if (fault)
          fault(FaultSite::Start);
        cancellationSource.emplace();
        cancellation = cancellationSource->get_token();
      } catch (...) {
        std::scoped_lock lock{mutex};
        Slot &slot = entries[index];
        slot.starting = false;
        ++counters.startFailures;
        if (slot.state == SlotState::Leased &&
            slot.incarnation == request->subscription.incarnation() &&
            slot.latestEpoch == request->epoch.value() && !stopping) {
          if (slot.completion) {
            retiredCompletion = std::move(slot.completion);
            slot.completion.reset();
            ++counters.replacedCompletions;
          } else {
            ++counters.pendingCompletions;
            updateMaximum(counters.maximumPendingCompletions,
                          counters.pendingCompletions);
          }
          try {
            slot.completion.emplace(Completion{
                request, std::unexpected(publicationDiagnostic(
                             "desktop.sketch.preparation-start-failed",
                             "sketch preparation could not allocate "
                             "cancellation state"))});
            if (queueCompletion(index, slot))
              startPost = true;
          } catch (...) {
            if (!slot.completion)
              --counters.pendingCompletions;
          }
        }
        if (slot.state == SlotState::Retiring || slot.pending ||
            slot.retiredDelivery)
          enqueueWork(index, slot);
      }
      if (!cancellationSource) {
        if (startPost)
          postDrain();
        continue;
      }

      bool invoke = false;
      {
        std::scoped_lock lock{mutex};
        Slot &slot = entries[index];
        slot.starting = false;
        if (slot.state == SlotState::Leased &&
            slot.incarnation == request->subscription.incarnation() &&
            slot.latestEpoch == request->epoch.value() && !stopping) {
          slot.active.emplace(request, std::move(*cancellationSource));
          ++counters.activePreparations;
          ++counters.started;
          updateMaximum(counters.maximumActivePreparations,
                        counters.activePreparations);
          invoke = true;
        } else {
          ++counters.retiredPending;
          if (slot.state == SlotState::Retiring || slot.pending ||
              slot.retiredDelivery)
            enqueueWork(index, slot);
        }
      }
      if (!invoke)
        continue;

      std::optional<Result<std::shared_ptr<const PreparedSketchProducts>>>
          result;
      try {
        result.emplace(validatePrepared(
            *request, prepare(*request, std::move(reuse), cancellation)));
      } catch (const std::exception &error) {
        try {
          result.emplace(std::unexpected(publicationDiagnostic(
              "desktop.sketch.preparation-exception", error.what())));
        } catch (...) {
        }
      } catch (...) {
        try {
          result.emplace(std::unexpected(publicationDiagnostic(
              "desktop.sketch.preparation-exception",
              "sketch preparation threw an unknown exception")));
        } catch (...) {
        }
      }
      bool completionFault = false;
      try {
        if (fault)
          fault(FaultSite::Completion);
      } catch (...) {
        completionFault = true;
        result.reset();
        try {
          result.emplace(std::unexpected(publicationDiagnostic(
              "desktop.sketch.preparation-completion-failed",
              "sketch preparation could not publish its result")));
        } catch (...) {
        }
      }
      const bool cancelled =
          cancellation.stop_requested() && result && !result->has_value() &&
          result->error().code == "desktop.sketch.preparation-cancelled";
      const bool succeeded = result && result->has_value() && !completionFault;
      bool post = false;

      {
        std::scoped_lock lock{mutex};
        Slot &slot = entries[index];
        if (slot.active &&
            slot.active->request->subscription == request->subscription &&
            slot.active->request->epoch == request->epoch) {
          slot.active.reset();
          --counters.activePreparations;
        }

        const bool current =
            slot.state == SlotState::Leased &&
            slot.incarnation == request->subscription.incarnation() &&
            slot.latestEpoch == request->epoch.value() && !stopping;
        if (cancelled)
          ++counters.cancelled;
        else if (!current)
          ++counters.stale;
        else if (succeeded)
          ++counters.completed;
        else
          ++counters.failed;

        if (current && result) {
          if (slot.completion) {
            retiredCompletion = std::move(slot.completion);
            slot.completion.reset();
            ++counters.replacedCompletions;
          } else {
            ++counters.pendingCompletions;
            updateMaximum(counters.maximumPendingCompletions,
                          counters.pendingCompletions);
          }
          if (succeeded) {
            retiredReuse = std::move(slot.reuse);
            slot.reuse = **result;
          }
          slot.completion.emplace(Completion{request, std::move(*result)});
          post = queueCompletion(index, slot);
        }

        takeRetirementsLocked(slot, retiredArtifacts);

        if (slot.state == SlotState::Retiring || slot.pending ||
            slot.retiredDelivery)
          enqueueWork(index, slot);
        drained.notify_all();
      }

      if (post)
        postDrain();
    }
  }

  SketchPreparationExecutor &executor;
  SketchPreparationLimits limits;
  std::vector<Slot> entries;
  Ring work;
  Ring completions;
  PrepareFunction prepare;
  FaultFunction fault;
  mutable std::mutex mutex;
  std::condition_variable_any changed;
  std::condition_variable drained;
  std::vector<std::jthread> workers;
  SketchPreparationExecutorMetrics counters;
  bool uiDrainQueued = false;
  bool stopping = false;
  std::size_t workerIterations = 0;
  std::size_t uiDeliveriesActive = 0;
};

SketchPreparationExecutor::SketchPreparationExecutor(
    SketchPreparationLimits limits, PrepareFunction prepare,
    FaultFunction fault, QObject *parent)
    : QObject(parent),
      impl_(std::make_unique<Impl>(*this, limits, std::move(prepare),
                                   std::move(fault))) {}

SketchPreparationExecutor::~SketchPreparationExecutor() {
  requestShutdown();
  join();
}

Result<SketchPreparationSubscription>
SketchPreparationExecutor::subscribe(QObject &lifetime,
                                     SketchPreparationSink &sink) {
  if (auto ui = requireThread(*this, &lifetime); !ui)
    return std::unexpected(std::move(ui.error()));
  if (dynamic_cast<QObject *>(&sink) != &lifetime)
    return std::unexpected(publicationDiagnostic(
        "desktop.sketch.preparation-sink-lifetime",
        "sketch preparation sink must be its observed QObject lifetime"));

  std::scoped_lock lock{impl_->mutex};
  if (impl_->stopping)
    return std::unexpected(
        publicationDiagnostic("desktop.sketch.preparation-stopped",
                              "sketch preparation executor has stopped"));
  for (std::uint32_t index = 0; index < impl_->entries.size(); ++index) {
    Impl::Slot &slot = impl_->entries[index];
    if (slot.state != Impl::SlotState::Free)
      continue;
    if (slot.incarnation == std::numeric_limits<std::uint64_t>::max())
      continue;
    ++slot.incarnation;
    slot.state = Impl::SlotState::Leased;
    slot.lifetime = &lifetime;
    slot.sink = &sink;
    slot.nextEpoch = 0;
    slot.latestEpoch = 0;
    ++impl_->counters.subscriptions;
    ++impl_->counters.leasedSubscriptions;
    const SketchPreparationSubscription subscription{index, slot.incarnation};
    slot.lifetimeConnection = QObject::connect(
        &lifetime, &QObject::destroyed, this,
        [this, watched = &lifetime] { retireLifetime(watched); },
        Qt::DirectConnection);
    lifetime.installEventFilter(this);
    return subscription;
  }
  return std::unexpected(publicationDiagnostic(
      "desktop.sketch.preparation-capacity",
      "sketch preparation subscription capacity is exhausted"));
}

Result<SketchPreparationSubmission> SketchPreparationExecutor::submit(
    SketchPreparationSubscription subscription,
    std::shared_ptr<const SketchSceneProducts> products, SketchCurveLod lod) {
  if (auto ui = requireThread(*this); !ui)
    return std::unexpected(std::move(ui.error()));
  if (!products)
    return std::unexpected(
        publicationDiagnostic("desktop.sketch.preparation-null-products",
                              "sketch preparation requires a product packet"));
  if (auto valid = validateSketchSceneProducts(*products); !valid)
    return std::unexpected(std::move(valid.error()));

  std::uint64_t epoch = 0;
  {
    std::scoped_lock lock{impl_->mutex};
    if (subscription.slot() >= impl_->entries.size())
      return std::unexpected(
          publicationDiagnostic("desktop.sketch.preparation-subscription",
                                "sketch preparation subscription is invalid"));
    const Impl::Slot &slot = impl_->entries[subscription.slot()];
    if (!impl_->matches(subscription, slot))
      return std::unexpected(
          publicationDiagnostic("desktop.sketch.preparation-subscription",
                                "sketch preparation subscription is stale"));
    if (slot.nextEpoch == std::numeric_limits<std::uint64_t>::max())
      return std::unexpected(
          publicationDiagnostic("desktop.sketch.preparation-epoch-exhausted",
                                "sketch preparation epoch is exhausted"));
    epoch = slot.nextEpoch + 1U;
  }

  std::shared_ptr<const Impl::Request> request;
  try {
    if (impl_->fault)
      impl_->fault(FaultSite::Submission);
    request = std::make_shared<const Impl::Request>(Impl::Request{
        subscription, SketchPreparationEpoch{epoch}, std::move(products), lod});
  } catch (...) {
    return std::unexpected(
        publicationDiagnostic("desktop.sketch.preparation-request-allocation",
                              "sketch preparation request allocation failed"));
  }

  std::scoped_lock lock{impl_->mutex};
  Impl::Slot &slot = impl_->entries[subscription.slot()];
  if (!impl_->matches(subscription, slot))
    return std::unexpected(
        publicationDiagnostic("desktop.sketch.preparation-subscription",
                              "sketch preparation subscription is stale"));
  if (slot.nextEpoch + 1U != epoch)
    return std::unexpected(publicationDiagnostic(
        "desktop.sketch.preparation-concurrent-submit",
        "sketch preparation submission changed concurrently"));
  if (slot.pending) {
    std::shared_ptr<const void> retired = slot.pending;
    if (!impl_->retireLocked(slot, std::move(retired), true))
      return std::unexpected(publicationDiagnostic(
          "desktop.sketch.preparation-backpressure",
          "sketch preparation retirement capacity is saturated"));
    ++impl_->counters.retiredPending;
  }

  const bool superseded = slot.pending || slot.starting || slot.active ||
                          slot.completion.has_value();
  if (slot.active)
    static_cast<void>(slot.active->cancellation.request_stop());
  if (!slot.pending)
    ++impl_->counters.pendingPreparations;
  slot.pending = std::move(request);
  slot.nextEpoch = epoch;
  slot.latestEpoch = epoch;
  ++impl_->counters.submissions;
  updateMaximum(impl_->counters.maximumPendingPreparations,
                impl_->counters.pendingPreparations);
  if (superseded)
    ++impl_->counters.superseded;
  impl_->enqueueWork(subscription.slot(), slot);
  return SketchPreparationSubmission{SketchPreparationEpoch{epoch}, superseded};
}

Result<void> SketchPreparationExecutor::invalidate(
    SketchPreparationSubscription subscription) {
  if (auto ui = requireThread(*this); !ui)
    return ui;
  std::scoped_lock lock{impl_->mutex};
  if (subscription.slot() >= impl_->entries.size())
    return std::unexpected(
        publicationDiagnostic("desktop.sketch.preparation-subscription",
                              "sketch preparation subscription is invalid"));
  Impl::Slot &slot = impl_->entries[subscription.slot()];
  if (!impl_->matches(subscription, slot))
    return std::unexpected(
        publicationDiagnostic("desktop.sketch.preparation-subscription",
                              "sketch preparation subscription is stale"));
  if (slot.nextEpoch == std::numeric_limits<std::uint64_t>::max())
    return std::unexpected(
        publicationDiagnostic("desktop.sketch.preparation-epoch-exhausted",
                              "sketch preparation epoch is exhausted"));
  slot.latestEpoch = ++slot.nextEpoch;
  if (slot.active)
    static_cast<void>(slot.active->cancellation.request_stop());
  if (!slot.starting && !slot.active &&
      (slot.pending || slot.completion ||
       std::ranges::any_of(slot.retirements, [](const auto &entry) {
         return entry.artifact != nullptr && entry.ready;
       })))
    impl_->enqueueWork(subscription.slot(), slot);
  return {};
}

Result<void> SketchPreparationExecutor::unsubscribe(
    SketchPreparationSubscription subscription) {
  if (auto ui = requireThread(*this); !ui)
    return ui;
  QObject *lifetime = nullptr;
  QMetaObject::Connection connection;
  {
    std::scoped_lock lock{impl_->mutex};
    if (subscription.slot() >= impl_->entries.size())
      return std::unexpected(
          publicationDiagnostic("desktop.sketch.preparation-subscription",
                                "sketch preparation subscription is invalid"));
    Impl::Slot &slot = impl_->entries[subscription.slot()];
    if (!impl_->matches(subscription, slot))
      return std::unexpected(
          publicationDiagnostic("desktop.sketch.preparation-subscription",
                                "sketch preparation subscription is stale"));
    lifetime = slot.lifetime;
    connection = slot.lifetimeConnection;
    slot.lifetimeConnection = {};
    impl_->beginRetirementLocked(subscription.slot(), slot);
  }
  QObject::disconnect(connection);
  if (lifetime && lifetime->thread() == thread())
    lifetime->removeEventFilter(this);
  return {};
}

Result<SketchPreparationRetirement> SketchPreparationExecutor::retireArtifact(
    SketchPreparationSubscription subscription,
    std::shared_ptr<const void> artifact) {
  if (auto ui = requireThread(*this); !ui)
    return std::unexpected(std::move(ui.error()));
  if (!artifact)
    return std::unexpected(publicationDiagnostic(
        "desktop.sketch.preparation-null-retirement",
        "sketch artifact retirement requires an artifact"));
  std::scoped_lock lock{impl_->mutex};
  if (subscription.slot() >= impl_->entries.size())
    return std::unexpected(
        publicationDiagnostic("desktop.sketch.preparation-subscription",
                              "sketch preparation subscription is invalid"));
  Impl::Slot &slot = impl_->entries[subscription.slot()];
  if (!impl_->matches(subscription, slot))
    return std::unexpected(
        publicationDiagnostic("desktop.sketch.preparation-subscription",
                              "sketch preparation subscription is stale"));
  if (slot.pending && slot.pending->products.get() == artifact.get())
    return SketchPreparationRetirement{
        subscription, std::numeric_limits<std::uint8_t>::max()};
  auto entry = impl_->retireLocked(slot, std::move(artifact), false);
  if (!entry)
    return std::unexpected(publicationDiagnostic(
        "desktop.sketch.preparation-backpressure",
        "sketch preparation retirement capacity is saturated"));
  return SketchPreparationRetirement{subscription, *entry};
}

Result<void> SketchPreparationExecutor::releaseArtifact(
    SketchPreparationRetirement retirement) {
  if (auto ui = requireThread(*this); !ui)
    return ui;
  std::scoped_lock lock{impl_->mutex};
  const auto subscription = retirement.subscription_;
  if (subscription.slot() >= impl_->entries.size())
    return std::unexpected(
        publicationDiagnostic("desktop.sketch.preparation-retirement",
                              "sketch artifact retirement is invalid"));
  Impl::Slot &slot = impl_->entries[subscription.slot()];
  if (slot.incarnation != subscription.incarnation())
    return std::unexpected(
        publicationDiagnostic("desktop.sketch.preparation-retirement",
                              "sketch artifact retirement is stale"));
  if (retirement.entry_ == std::numeric_limits<std::uint8_t>::max())
    return {};
  if (retirement.entry_ >= slot.retirements.size() ||
      !slot.retirements[retirement.entry_].artifact ||
      slot.retirements[retirement.entry_].ready)
    return std::unexpected(
        publicationDiagnostic("desktop.sketch.preparation-retirement",
                              "sketch artifact retirement is stale"));
  slot.retirements[retirement.entry_].ready = true;
  impl_->enqueueWork(subscription.slot(), slot);
  return {};
}

SketchPreparationExecutorMetrics SketchPreparationExecutor::metrics() const {
  std::scoped_lock lock{impl_->mutex};
  auto result = impl_->counters;
  result.stopping = impl_->stopping;
  return result;
}

void SketchPreparationExecutor::requestShutdown() noexcept {
  if (QThread::currentThread() != thread()) {
    std::scoped_lock lock{impl_->mutex};
    ++impl_->counters.lifecycleRejections;
    return;
  }
  std::scoped_lock lock{impl_->mutex};
  if (impl_->stopping)
    return;
  impl_->stopping = true;
  impl_->retireAllLocked();
  impl_->changed.notify_all();
}

bool SketchPreparationExecutor::waitUntilDrained(
    std::chrono::milliseconds timeout) noexcept {
  if (QThread::currentThread() != thread()) {
    std::scoped_lock lock{impl_->mutex};
    ++impl_->counters.lifecycleRejections;
    return false;
  }
  std::unique_lock lock{impl_->mutex};
  if (impl_->uiDeliveriesActive != 0U) {
    ++impl_->counters.lifecycleRejections;
    return false;
  }
  return impl_->drained.wait_for(lock, timeout,
                                 [this] { return impl_->drainedLocked(); });
}

void SketchPreparationExecutor::join() noexcept {
  if (QThread::currentThread() != thread()) {
    std::scoped_lock lock{impl_->mutex};
    ++impl_->counters.lifecycleRejections;
    return;
  }
  {
    std::scoped_lock lock{impl_->mutex};
    if (!impl_->stopping || impl_->uiDeliveriesActive != 0U) {
      ++impl_->counters.lifecycleRejections;
      return;
    }
  }
  for (std::jthread &worker : impl_->workers) {
    worker.request_stop();
    impl_->changed.notify_all();
  }
  for (std::jthread &worker : impl_->workers) {
    if (worker.joinable())
      worker.join();
  }
  impl_->workers.clear();
}

bool SketchPreparationExecutor::isStopping() const noexcept {
  std::scoped_lock lock{impl_->mutex};
  return impl_->stopping;
}

bool SketchPreparationExecutor::eventFilter(QObject *watched, QEvent *event) {
  if (event && event->type() == QEvent::ThreadChange) {
    retireLifetime(watched);
    watched->removeEventFilter(this);
  }
  return QObject::eventFilter(watched, event);
}

void SketchPreparationExecutor::retireLifetime(QObject *lifetime) noexcept {
  std::scoped_lock lock{impl_->mutex};
  for (std::uint32_t index = 0; index < impl_->entries.size(); ++index) {
    Impl::Slot &slot = impl_->entries[index];
    if (slot.state != Impl::SlotState::Leased || slot.lifetime != lifetime)
      continue;
    impl_->beginRetirementLocked(index, slot);
  }
}

void SketchPreparationExecutor::drainCompletions() {
  if (!requireThread(*this))
    return;

  std::size_t processed = 0;
  while (processed < impl_->limits.maximumUiDeliveriesPerTurn) {
    std::optional<Impl::Completion> delivery;
    SketchPreparationSink *sink = nullptr;
    std::uint32_t index = 0;
    bool current = false;
    {
      std::scoped_lock lock{impl_->mutex};
      if (impl_->completions.empty())
        break;
      index = impl_->completions.pop();
      Impl::Slot &slot = impl_->entries[index];
      slot.completionQueued = false;
      if (slot.completion) {
        delivery = std::move(slot.completion);
        slot.completion.reset();
        --impl_->counters.pendingCompletions;
      }
      current =
          delivery && slot.state == Impl::SlotState::Leased &&
          slot.incarnation == delivery->request->subscription.incarnation() &&
          slot.latestEpoch == delivery->request->epoch.value() &&
          slot.lifetime && slot.lifetime->thread() == thread();
      slot.completionDelivering = delivery.has_value();
      if (current) {
        sink = slot.sink;
        ++impl_->uiDeliveriesActive;
      } else if (slot.state == Impl::SlotState::Leased && slot.lifetime &&
                 slot.lifetime->thread() != thread()) {
        impl_->beginRetirementLocked(index, slot);
      }
    }

    if (delivery && !current && impl_->fault) {
      try {
        impl_->fault(FaultSite::DeliveryBoundary);
      } catch (...) {
      }
    }

    bool delivered = false;
    if (current && sink) {
      try {
        sink->deliver(
            {delivery->request->subscription, delivery->request->epoch,
             delivery->request->products->stamp, delivery->request->products,
             delivery->request->lod, delivery->prepared});
        delivered = true;
      } catch (...) {
      }
    }

    {
      std::scoped_lock lock{impl_->mutex};
      Impl::Slot &slot = impl_->entries[index];
      if (current)
        --impl_->uiDeliveriesActive;
      slot.completionDelivering = false;
      if (delivery) {
        slot.retiredDelivery.emplace(std::move(*delivery));
        if (!slot.starting && !slot.active)
          impl_->enqueueWork(index, slot);
      } else if (slot.state == Impl::SlotState::Retiring && !slot.starting &&
                 !slot.active) {
        impl_->enqueueWork(index, slot);
      }
      if (delivered)
        ++impl_->counters.delivered;
      else if (current)
        ++impl_->counters.deliveryFailures;
      impl_->drained.notify_all();
    }
    ++processed;
  }

  bool repost = false;
  {
    std::scoped_lock lock{impl_->mutex};
    ++impl_->counters.uiDrainTurns;
    updateMaximum(impl_->counters.maximumUiDeliveriesInTurn, processed);
    if (impl_->completions.empty()) {
      impl_->uiDrainQueued = false;
    } else {
      ++impl_->counters.uiDrainPosts;
      repost = true;
    }
  }
  if (repost)
    impl_->postDrain();
}

SketchScenePublicationController::SketchScenePublicationController(
    SketchSceneItem &item, SketchPreparationExecutor &executor, QObject *parent)
    : QObject(parent), item_(&item), executor_(&executor) {
  if (auto ui = requireThread(executor, &item); !ui) {
    setLastDiagnostic(std::move(ui.error()));
  } else {
    auto subscribed = executor.subscribe(*this, *this);
    if (subscribed)
      subscription_ = *subscribed;
    else
      setLastDiagnostic(std::move(subscribed.error()));
  }
  subscribed_.store(subscription_.has_value(), std::memory_order_release);
  QObject::connect(
      &item, &QObject::destroyed, this,
      [this] {
        auto stopped = shutdown();
        if (!stopped)
          setLastDiagnostic(std::move(stopped.error()));
      },
      Qt::DirectConnection);
  QObject::connect(
      &executor, &QObject::destroyed, this,
      [this] {
        subscription_.reset();
        subscribed_.store(false, std::memory_order_release);
      },
      Qt::DirectConnection);
}

SketchScenePublicationController::~SketchScenePublicationController() {
  if (QThread::currentThread() != thread())
    qFatal("SketchScenePublicationController destroyed off its owning thread");
  const auto unsubscribeAfterFailure = [this] {
    if (!executor_ || !subscription_)
      return;
    if (auto unsubscribed = executor_->unsubscribe(*subscription_);
        !unsubscribed)
      qFatal("SketchScenePublicationController could not unsubscribe");
  };
  try {
    auto stopped = shutdown();
    if (!stopped)
      unsubscribeAfterFailure();
  } catch (...) {
    unsubscribeAfterFailure();
  }
}

Result<void> SketchScenePublicationController::requireUiThread() const {
  if (!item_)
    return std::unexpected(
        publicationDiagnostic("desktop.sketch.publication-item-destroyed",
                              "sketch scene item has been destroyed"));
  return requireThread(*this, item_);
}

Result<void>
SketchScenePublicationController::retarget(render::SceneTarget target) {
  if (auto ui = requireUiThread(); !ui)
    return ui;
  if (isShutdown())
    return std::unexpected(
        publicationDiagnostic("desktop.sketch.publication-stopped",
                              "sketch scene publication has stopped"));
  if (!executor_ || !subscription_)
    return std::unexpected(
        publicationDiagnostic("desktop.sketch.publication-executor-destroyed",
                              "sketch preparation executor is unavailable"));
  auto retained = retirementOwner(currentProducts_, *item_);
  if (!retained)
    return std::unexpected(std::move(retained.error()));
  std::optional<SketchPreparationRetirement> retirement;
  if (*retained) {
    auto retired =
        executor_->retireArtifact(*subscription_, std::move(*retained));
    if (!retired)
      return std::unexpected(std::move(retired.error()));
    retirement = *retired;
  }
  if (auto invalidated = executor_->invalidate(*subscription_); !invalidated) {
    if (retirement) {
      auto released = executor_->releaseArtifact(*retirement);
      if (!released)
        return released;
    }
    return invalidated;
  }

  desired_ = target;
  currentProducts_.reset();
  requestedProduct_.reset();
  requestedLod_.reset();
  markerView_.reset();
  markerViewWatermark_.reset();
  markerViewValue_.store(0U, std::memory_order_release);
  item_->retarget(std::move(target));
  if (retirement) {
    auto released = executor_->releaseArtifact(*retirement);
    if (!released)
      return released;
  }
  return {};
}

Result<bool> SketchScenePublicationController::validateAdvance(
    const SketchSceneProducts &products) const {
  if (!products.scene)
    return std::unexpected(publicationDiagnostic(
        "desktop.sketch.publication-null-scene",
        "sketch scene products require an evaluated scene"));
  if (!desired_)
    return std::unexpected(publicationDiagnostic(
        "desktop.sketch.publication-missing-target",
        "retarget sketch publication before publishing products"));
  if (products.stamp.target != *desired_ ||
      products.scene->stamp().target != *desired_)
    return std::unexpected(publicationDiagnostic(
        "desktop.sketch.publication-stale-target",
        "sketch products do not match the requested target"));
  if (auto valid = validateSketchSceneProducts(products); !valid)
    return std::unexpected(std::move(valid.error()));
  if (products.markers && products.markers->stamp().target.view &&
      (!markerView_ || products.markers->stamp().target.view != markerView_))
    return std::unexpected(publicationDiagnostic(
        "desktop.sketch.publication-marker-view",
        "view-dependent sketch markers do not match the current view"));

  if (!productClock_)
    return true;
  if (products.stamp.generation < *productClock_)
    return std::unexpected(publicationDiagnostic(
        "desktop.sketch.publication-stale-products",
        "sketch product generation is older than the current packet"));
  if (products.stamp.generation == *productClock_) {
    if (!currentProducts_ || products.stamp != currentProducts_->stamp)
      return std::unexpected(publicationDiagnostic(
          "desktop.sketch.publication-product-conflict",
          "one controller-global product generation has conflicting content"));
    if (!sameSketchSceneProductComponents(products, *currentProducts_))
      return std::unexpected(publicationDiagnostic(
          "desktop.sketch.publication-product-conflict",
          "one sketch product stamp has conflicting component identities"));
    return false;
  }

  if (currentProducts_) {
    const render::SceneStamp &current = currentProducts_->scene->stamp();
    const render::SceneStamp &next = products.scene->stamp();
    if (next.generation < current.generation)
      return std::unexpected(publicationDiagnostic(
          "desktop.sketch.publication-stale-scene",
          "sketch scene generation is older than the current packet"));
    if (next.generation == current.generation && next != current)
      return std::unexpected(publicationDiagnostic(
          "desktop.sketch.publication-scene-conflict",
          "one sketch scene generation has conflicting identity"));
    if (next == current && products.scene != currentProducts_->scene)
      return std::unexpected(publicationDiagnostic(
          "desktop.sketch.publication-scene-instance-conflict",
          "one sketch scene stamp was published by different instances"));
  }
  return true;
}

Result<SketchScenePublicationOffer>
SketchScenePublicationController::publishProducts(
    SketchSceneProducts products) {
  if (auto ui = requireUiThread(); !ui)
    return std::unexpected(std::move(ui.error()));
  if (isShutdown())
    return std::unexpected(
        publicationDiagnostic("desktop.sketch.publication-stopped",
                              "sketch scene publication has stopped"));
  auto changed = validateAdvance(products);
  if (!changed)
    return std::unexpected(std::move(changed.error()));
  if (publication_ == std::numeric_limits<std::uint64_t>::max())
    return std::unexpected(
        publicationDiagnostic("desktop.sketch.publication-sequence-exhausted",
                              "sketch publication sequence is exhausted"));

  if (!*changed)
    return scheduleCurrentProducts(false);

  if (!executor_ || !subscription_)
    return std::unexpected(
        publicationDiagnostic("desktop.sketch.publication-executor-destroyed",
                              "sketch preparation executor is unavailable"));
  std::shared_ptr<const SketchSceneProducts> next;
  try {
    next = std::make_shared<const SketchSceneProducts>(std::move(products));
  } catch (...) {
    return std::unexpected(
        publicationDiagnostic("desktop.sketch.publication-allocation",
                              "sketch product packet allocation failed"));
  }
  std::optional<SketchPreparationRetirement> retirement;
  if (currentProducts_) {
    auto retired = executor_->retireArtifact(*subscription_, currentProducts_);
    if (!retired)
      return std::unexpected(std::move(retired.error()));
    retirement = *retired;
  }

  ++publication_;
  productPublications_.fetch_add(1, std::memory_order_relaxed);
  productClock_ = next->stamp.generation;
  currentProducts_ = std::move(next);
  if (retirement) {
    auto released = executor_->releaseArtifact(*retirement);
    if (!released)
      return std::unexpected(std::move(released.error()));
  }
  return scheduleCurrentProducts(true);
}

Result<SketchScenePublicationOffer>
SketchScenePublicationController::scheduleCurrentProducts(bool changed) {
  if (!item_ || !executor_ || !currentProducts_ || !subscription_)
    return std::unexpected(publicationDiagnostic(
        "desktop.sketch.publication-scheduling-unavailable",
        "sketch preparation cannot be scheduled without live publication "
        "state"));
  const SketchCurveLod lod = item_->requestedLod();
  if (requestedProduct_ && requestedLod_ &&
      *requestedProduct_ == currentProducts_->stamp && *requestedLod_ == lod)
    return SketchScenePublicationOffer{publication_, changed, false, false};

  auto submitted = executor_->submit(*subscription_, currentProducts_, lod);
  if (!submitted) {
    Diagnostic failure = std::move(submitted.error());
    setLastDiagnostic(failure);
    return std::unexpected(std::move(failure));
  }
  requestedProduct_ = currentProducts_->stamp;
  requestedLod_ = lod;
  preparationRequests_.fetch_add(1, std::memory_order_relaxed);
  return SketchScenePublicationOffer{publication_, changed, true,
                                     submitted->superseded};
}

void SketchScenePublicationController::deliver(
    const SketchPreparationCompletionView &completion) {
  if (shutdown_.load(std::memory_order_acquire) || !subscription_ ||
      completion.subscription != *subscription_ || !item_ ||
      !currentProducts_ || completion.products != currentProducts_ ||
      item_->requestedLod() != completion.lod) {
    staleCompletions_.fetch_add(1, std::memory_order_relaxed);
    if (!shutdown_.load(std::memory_order_acquire) && item_ &&
        currentProducts_) {
      requestedProduct_.reset();
      requestedLod_.reset();
      auto scheduled = scheduleCurrentProducts(false);
      if (!scheduled)
        setLastDiagnostic(std::move(scheduled.error()));
    }
    return;
  }
  if (!completion.prepared) {
    requestedProduct_.reset();
    requestedLod_.reset();
    setLastDiagnostic(completion.prepared.error());
    return;
  }

  auto offered = item_->publishProducts(*completion.prepared);
  if (!offered) {
    itemRejections_.fetch_add(1, std::memory_order_relaxed);
    requestedProduct_.reset();
    requestedLod_.reset();
    setLastDiagnostic(std::move(offered.error()));
    return;
  }
  if (offered->decision != PreparedSketchSceneDecision::Accepted &&
      offered->decision != PreparedSketchSceneDecision::Duplicate) {
    itemRejections_.fetch_add(1, std::memory_order_relaxed);
    requestedProduct_.reset();
    requestedLod_.reset();
    return;
  }
  itemPublications_.fetch_add(1, std::memory_order_relaxed);
}

Result<SketchCameraDecision>
SketchScenePublicationController::publishCamera(SketchCamera2d camera) {
  if (auto ui = requireUiThread(); !ui)
    return std::unexpected(std::move(ui.error()));
  if (isShutdown())
    return std::unexpected(
        publicationDiagnostic("desktop.sketch.publication-stopped",
                              "sketch scene publication has stopped"));
  auto decision = item_->publishCamera(camera);
  if (!decision)
    return decision;
  if (*decision == SketchCameraDecision::Accepted) {
    invalidateViewMarkers();
    if (currentProducts_ && requestedLod_ &&
        *requestedLod_ != item_->requestedLod()) {
      requestedProduct_.reset();
      requestedLod_.reset();
      auto scheduled = scheduleCurrentProducts(false);
      if (!scheduled)
        return std::unexpected(std::move(scheduled.error()));
    }
  }
  return decision;
}

Result<void> SketchScenePublicationController::publishMarkerView(
    render::SketchMarkerViewGeneration generation) {
  if (auto ui = requireUiThread(); !ui)
    return ui;
  if (isShutdown())
    return std::unexpected(
        publicationDiagnostic("desktop.sketch.publication-stopped",
                              "sketch scene publication has stopped"));
  if (markerViewWatermark_ && generation < *markerViewWatermark_)
    return std::unexpected(publicationDiagnostic(
        "desktop.sketch.publication-stale-marker-view",
        "sketch marker view is older than the current view"));
  if (markerViewWatermark_ && generation == *markerViewWatermark_) {
    if (markerView_ && generation == *markerView_)
      return {};
    return std::unexpected(publicationDiagnostic(
        "desktop.sketch.publication-stale-marker-view",
        "invalidated marker view cannot be reintroduced"));
  }
  markerView_ = generation;
  markerViewWatermark_ = generation;
  markerViewValue_.store(generation.value(), std::memory_order_release);
  return {};
}

void SketchScenePublicationController::invalidateViewMarkers() {
  markerView_.reset();
  markerViewValue_.store(0U, std::memory_order_release);
}

Result<SketchSemanticPickEvidence> SketchScenePublicationController::pick(
    QPointF itemLogical, double toleranceLogicalPixels,
    render::SketchPickTargets targets) const {
  if (auto ui = requireUiThread(); !ui)
    return std::unexpected(std::move(ui.error()));
  if (isShutdown())
    return std::unexpected(
        publicationDiagnostic("desktop.sketch.publication-stopped",
                              "sketch scene publication has stopped"));
  auto picked = item_->pick(itemLogical, toleranceLogicalPixels, targets);
  if (!picked)
    return std::unexpected(std::move(picked.error()));
  if (!picked->products || picked->product != picked->products->stamp ||
      picked->scene != picked->products->scene->stamp())
    return std::unexpected(publicationDiagnostic(
        "desktop.sketch.publication-missing-products",
        "presented sketch frame has no exact product packet"));
  if (picked->products->markers) {
    const auto &view = picked->products->markers->stamp().target.view;
    if (view &&
        view->value() != markerViewValue_.load(std::memory_order_acquire))
      return std::unexpected(publicationDiagnostic(
          "desktop.sketch.publication-stale-marker-view",
          "presented sketch markers belong to an invalidated view"));
  }
  auto products = picked->products;
  return SketchSemanticPickEvidence{std::move(*picked), std::move(products)};
}

std::shared_ptr<const SketchSceneProducts>
SketchScenePublicationController::currentProducts() const {
  if (!requireUiThread() || isShutdown())
    return {};
  if (currentProducts_ && currentProducts_->markers) {
    const auto &view = currentProducts_->markers->stamp().target.view;
    if (view &&
        view->value() != markerViewValue_.load(std::memory_order_acquire))
      return {};
  }
  return currentProducts_;
}

SketchScenePublicationMetrics
SketchScenePublicationController::metrics() const {
  return {productPublications_.load(std::memory_order_relaxed),
          preparationRequests_.load(std::memory_order_relaxed),
          staleCompletions_.load(std::memory_order_relaxed),
          itemPublications_.load(std::memory_order_relaxed),
          itemRejections_.load(std::memory_order_relaxed),
          subscribed_.load(std::memory_order_acquire)};
}

Diagnostic SketchScenePublicationController::lastDiagnostic() const {
  std::scoped_lock lock{diagnosticMutex_};
  return lastDiagnostic_;
}

void SketchScenePublicationController::setLastDiagnostic(
    Diagnostic diagnostic) {
  std::scoped_lock lock{diagnosticMutex_};
  lastDiagnostic_ = std::move(diagnostic);
}

Result<void> SketchScenePublicationController::shutdown() {
  if (isShutdown())
    return {};
  if (QThread::currentThread() != thread())
    return std::unexpected(publicationDiagnostic(
        "desktop.sketch.publication-thread",
        "sketch publication shutdown must run on its owning UI thread"));

  std::optional<Diagnostic> releaseFailure;
  if (executor_ && subscription_) {
    auto retained =
        item_ ? retirementOwner(currentProducts_, *item_)
              : Result<std::shared_ptr<const void>>{
                    std::static_pointer_cast<const void>(currentProducts_)};
    if (!retained)
      return std::unexpected(std::move(retained.error()));
    if (*retained) {
      auto retired =
          executor_->retireArtifact(*subscription_, std::move(*retained));
      if (!retired)
        return std::unexpected(std::move(retired.error()));

      currentProducts_.reset();
      if (item_)
        item_->clearPresentation();
      auto released = executor_->releaseArtifact(*retired);
      if (!released)
        releaseFailure = std::move(released.error());
    } else if (item_) {
      item_->clearPresentation();
    }

    auto unsubscribed = executor_->unsubscribe(*subscription_);
    if (!unsubscribed) {
      if (releaseFailure)
        setLastDiagnostic(*releaseFailure);
      return std::unexpected(std::move(unsubscribed.error()));
    }
    subscription_.reset();
    subscribed_.store(false, std::memory_order_release);
  } else {
    currentProducts_.reset();
    if (item_)
      item_->clearPresentation();
  }

  requestedProduct_.reset();
  requestedLod_.reset();
  executor_.clear();
  item_.clear();
  shutdown_.store(true, std::memory_order_release);
  if (releaseFailure) {
    setLastDiagnostic(*releaseFailure);
    return std::unexpected(std::move(*releaseFailure));
  }
  return {};
}

bool SketchScenePublicationController::isShutdown() const noexcept {
  return shutdown_.load(std::memory_order_acquire);
}

} // namespace kearne::ui
