#pragma once

#include "sketch_prepared_products.hpp"
#include "sketch_scene_item.hpp"
#include "sketch_scene_products.hpp"

#include <QObject>
#include <QPointer>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>

namespace kearne::ui {

class SketchPreparationSubscription final {
public:
  [[nodiscard]] std::uint32_t slot() const { return slot_; }
  [[nodiscard]] std::uint64_t incarnation() const { return incarnation_; }
  bool operator==(const SketchPreparationSubscription &) const = default;

private:
  SketchPreparationSubscription(std::uint32_t slot, std::uint64_t incarnation)
      : slot_(slot), incarnation_(incarnation) {}

  std::uint32_t slot_ = 0;
  std::uint64_t incarnation_ = 0;

  friend class SketchPreparationExecutor;
};

class SketchPreparationEpoch final {
public:
  [[nodiscard]] std::uint64_t value() const { return value_; }
  bool operator==(const SketchPreparationEpoch &) const = default;

private:
  explicit SketchPreparationEpoch(std::uint64_t value) : value_(value) {}
  std::uint64_t value_ = 0;

  friend class SketchPreparationExecutor;
};

class SketchPreparationRetirement final {
public:
  bool operator==(const SketchPreparationRetirement &) const = default;

private:
  SketchPreparationRetirement(SketchPreparationSubscription subscription,
                              std::uint8_t entry)
      : subscription_(subscription), entry_(entry) {}
  SketchPreparationSubscription subscription_;
  std::uint8_t entry_ = 0;
  friend class SketchPreparationExecutor;
};

struct SketchPreparationRequest {
  SketchPreparationSubscription subscription;
  SketchPreparationEpoch epoch;
  std::shared_ptr<const SketchSceneProducts> products;
  SketchCurveLod lod;
};

struct SketchPreparationCompletionView {
  SketchPreparationSubscription subscription;
  SketchPreparationEpoch epoch;
  const SketchProductStamp &product;
  const std::shared_ptr<const SketchSceneProducts> &products;
  SketchCurveLod lod;
  const Result<std::shared_ptr<const PreparedSketchProducts>> &prepared;
};

class SketchPreparationSink {
public:
  virtual ~SketchPreparationSink() = default;
  virtual void deliver(const SketchPreparationCompletionView &completion) = 0;
};

struct SketchPreparationLimits {
  static constexpr std::size_t maximumRetirementsPerSubscription = 2;
  std::size_t maximumSubscriptions = 64;
  std::size_t workerCount = 2;
  std::size_t maximumUiDeliveriesPerTurn = 8;
};

struct SketchPreparationSubmission {
  SketchPreparationEpoch epoch;
  bool superseded = false;
  bool operator==(const SketchPreparationSubmission &) const = default;
};

struct SketchPreparationExecutorMetrics {
  std::uint64_t subscriptions = 0;
  std::uint64_t unsubscriptions = 0;
  std::uint64_t submissions = 0;
  std::uint64_t superseded = 0;
  std::uint64_t startFailures = 0;
  std::uint64_t started = 0;
  std::uint64_t completed = 0;
  std::uint64_t failed = 0;
  std::uint64_t cancelled = 0;
  std::uint64_t stale = 0;
  std::uint64_t retiredPending = 0;
  std::uint64_t replacedCompletions = 0;
  std::uint64_t delivered = 0;
  std::uint64_t deliveryFailures = 0;
  std::uint64_t lifecycleRejections = 0;
  std::uint64_t backpressure = 0;
  std::uint64_t retirements = 0;
  std::uint64_t uiDrainPosts = 0;
  std::uint64_t uiDrainTurns = 0;
  std::size_t workerCount = 0;
  std::size_t leasedSubscriptions = 0;
  std::size_t activePreparations = 0;
  std::size_t pendingPreparations = 0;
  std::size_t pendingCompletions = 0;
  std::size_t pendingRetirements = 0;
  std::size_t maximumActivePreparations = 0;
  std::size_t maximumPendingPreparations = 0;
  std::size_t maximumPendingCompletions = 0;
  std::size_t maximumPendingRetirements = 0;
  std::size_t maximumUiDeliveriesInTurn = 0;
  bool stopping = false;
  bool operator==(const SketchPreparationExecutorMetrics &) const = default;
};

class SketchPreparationExecutor final : public QObject {
public:
  enum class FaultSite : std::uint8_t {
    Start = 1,
    Completion = 2,
    Submission = 3,
    DeliveryBoundary = 4
  };
  using PrepareFunction =
      std::function<Result<std::shared_ptr<const PreparedSketchProducts>>(
          const SketchPreparationRequest &,
          std::shared_ptr<const PreparedSketchProducts>, std::stop_token)>;
  using FaultFunction = std::function<void(FaultSite)>;

  explicit SketchPreparationExecutor(SketchPreparationLimits limits = {},
                                     PrepareFunction prepare = {},
                                     FaultFunction fault = {},
                                     QObject *parent = nullptr);
  ~SketchPreparationExecutor() override;

  SketchPreparationExecutor(const SketchPreparationExecutor &) = delete;
  SketchPreparationExecutor &
  operator=(const SketchPreparationExecutor &) = delete;

  [[nodiscard]] Result<SketchPreparationSubscription>
  subscribe(QObject &lifetime, SketchPreparationSink &sink);
  [[nodiscard]] Result<SketchPreparationSubmission>
  submit(SketchPreparationSubscription subscription,
         std::shared_ptr<const SketchSceneProducts> products,
         SketchCurveLod lod);
  [[nodiscard]] Result<void>
  invalidate(SketchPreparationSubscription subscription);
  [[nodiscard]] Result<void>
  unsubscribe(SketchPreparationSubscription subscription);
  // Stage an owner, release the caller's owner, then release this token. The
  // worker cannot destroy the artifact before releaseArtifact().
  [[nodiscard]] Result<SketchPreparationRetirement>
  retireArtifact(SketchPreparationSubscription subscription,
                 std::shared_ptr<const void> artifact);
  [[nodiscard]] Result<void>
  releaseArtifact(SketchPreparationRetirement retirement);

  [[nodiscard]] SketchPreparationExecutorMetrics metrics() const;
  // Terminal order is requestShutdown(), waitUntilDrained(), then join().
  // join() requires shutdown to have been requested.
  void requestShutdown() noexcept;
  [[nodiscard]] bool
  waitUntilDrained(std::chrono::milliseconds timeout) noexcept;
  void join() noexcept;
  [[nodiscard]] bool isStopping() const noexcept;

private:
  struct Impl;
  bool eventFilter(QObject *watched, QEvent *event) override;
  void drainCompletions();
  void retireLifetime(QObject *lifetime) noexcept;
  std::unique_ptr<Impl> impl_;
};

struct SketchScenePublicationOffer {
  std::uint64_t publication = 0;
  bool productsChanged = false;
  bool preparationScheduled = false;
  bool supersededPreparation = false;
  bool operator==(const SketchScenePublicationOffer &) const = default;
};

struct SketchScenePublicationMetrics {
  std::uint64_t productPublications = 0;
  std::uint64_t preparationRequests = 0;
  std::uint64_t staleCompletions = 0;
  std::uint64_t itemPublications = 0;
  std::uint64_t itemRejections = 0;
  bool subscribed = false;
  bool operator==(const SketchScenePublicationMetrics &) const = default;
};

struct SketchSemanticPickEvidence {
  SketchItemPickEvidence item;
  std::shared_ptr<const SketchSceneProducts> products;
};

// The controller owns no thread. It validates product packets and adapts one
// executor subscription to one item on the item's UI thread.
class SketchScenePublicationController final : public QObject,
                                               public SketchPreparationSink {
public:
  SketchScenePublicationController(SketchSceneItem &item,
                                   SketchPreparationExecutor &executor,
                                   QObject *parent = nullptr);
  ~SketchScenePublicationController() override;

  SketchScenePublicationController(const SketchScenePublicationController &) =
      delete;
  SketchScenePublicationController &
  operator=(const SketchScenePublicationController &) = delete;

  [[nodiscard]] Result<void> retarget(render::SceneTarget target);
  [[nodiscard]] Result<SketchScenePublicationOffer>
  publishProducts(SketchSceneProducts products);
  [[nodiscard]] Result<SketchCameraDecision>
  publishCamera(SketchCamera2d camera);
  [[nodiscard]] Result<void>
  publishMarkerView(render::SketchMarkerViewGeneration generation);
  [[nodiscard]] Result<SketchSemanticPickEvidence> pick(
      QPointF itemLogical, double toleranceLogicalPixels,
      render::SketchPickTargets targets = render::SketchPickTargets::All) const;

  [[nodiscard]] std::shared_ptr<const SketchSceneProducts>
  currentProducts() const;
  [[nodiscard]] SketchScenePublicationMetrics metrics() const;
  [[nodiscard]] Diagnostic lastDiagnostic() const;

  [[nodiscard]] Result<void> shutdown();
  [[nodiscard]] bool isShutdown() const noexcept;

private:
  [[nodiscard]] Result<void> requireUiThread() const;
  [[nodiscard]] Result<bool>
  validateAdvance(const SketchSceneProducts &products) const;
  [[nodiscard]] Result<SketchScenePublicationOffer>
  scheduleCurrentProducts(bool productsChanged);
  void deliver(const SketchPreparationCompletionView &completion) override;
  void invalidateViewMarkers();
  void setLastDiagnostic(Diagnostic diagnostic);

  QPointer<SketchSceneItem> item_;
  QPointer<SketchPreparationExecutor> executor_;
  std::optional<SketchPreparationSubscription> subscription_;
  std::optional<render::SceneTarget> desired_;
  std::shared_ptr<const SketchSceneProducts> currentProducts_;
  std::optional<SketchProductGeneration> productClock_;
  std::optional<SketchProductStamp> requestedProduct_;
  std::optional<SketchCurveLod> requestedLod_;
  std::optional<render::SketchMarkerViewGeneration> markerView_;
  std::optional<render::SketchMarkerViewGeneration> markerViewWatermark_;
  std::atomic_uint64_t markerViewValue_ = 0;
  mutable std::mutex diagnosticMutex_;
  Diagnostic lastDiagnostic_;
  std::uint64_t publication_ = 0;
  std::atomic_bool subscribed_ = false;
  std::atomic_bool shutdown_ = false;

  std::atomic_uint64_t productPublications_ = 0;
  std::atomic_uint64_t preparationRequests_ = 0;
  std::atomic_uint64_t staleCompletions_ = 0;
  std::atomic_uint64_t itemPublications_ = 0;
  std::atomic_uint64_t itemRejections_ = 0;
};

} // namespace kearne::ui
