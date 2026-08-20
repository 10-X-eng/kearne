#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>

namespace kearne::ui {

struct ArtifactReclaimerMetrics {
  std::uint64_t accepted = 0;
  std::uint64_t released = 0;
  std::uint64_t saturated = 0;
  std::uint64_t contended = 0;
  std::uint64_t closed = 0;
  std::size_t outstanding = 0;
  std::size_t maximumQueued = 0;
  bool operator==(const ArtifactReclaimerMetrics &) const = default;
};

template <typename Artifact, std::size_t Capacity>
class BoundedArtifactReclaimer final {
  static_assert(Capacity > 0U);
  static_assert(std::is_nothrow_move_constructible_v<Artifact>);
  static_assert(std::is_nothrow_destructible_v<Artifact>);

public:
  BoundedArtifactReclaimer() : consumer_([this] { consume(); }) {}
  BoundedArtifactReclaimer(const BoundedArtifactReclaimer &) = delete;
  BoundedArtifactReclaimer &
  operator=(const BoundedArtifactReclaimer &) = delete;

  ~BoundedArtifactReclaimer() { shutdown(); }

  template <typename Factory>
  [[nodiscard]] bool tryReclaim(Factory &&factory) noexcept {
    static_assert(std::is_nothrow_invocable_r_v<Artifact, Factory &&>);
    std::unique_lock lock{mutex_, std::try_to_lock};
    if (!lock.owns_lock()) {
      contended_.fetch_add(1U, std::memory_order_relaxed);
      return false;
    }
    if (!accepting_) {
      closed_.fetch_add(1U, std::memory_order_relaxed);
      return false;
    }
    if (queued_ == Capacity) {
      saturated_.fetch_add(1U, std::memory_order_relaxed);
      return false;
    }
    slots_[write_].emplace(std::forward<Factory>(factory)());
    write_ = (write_ + 1U) % Capacity;
    ++queued_;
    outstanding_.fetch_add(1U, std::memory_order_relaxed);
    accepted_.fetch_add(1U, std::memory_order_relaxed);
    std::size_t maximum = maximumQueued_.load(std::memory_order_relaxed);
    while (maximum < queued_ &&
           !maximumQueued_.compare_exchange_weak(maximum, queued_,
                                                 std::memory_order_relaxed)) {
    }
    lock.unlock();
    ready_.notify_one();
    return true;
  }

  [[nodiscard]] ArtifactReclaimerMetrics metrics() const noexcept {
    return {accepted_.load(std::memory_order_relaxed),
            released_.load(std::memory_order_relaxed),
            saturated_.load(std::memory_order_relaxed),
            contended_.load(std::memory_order_relaxed),
            closed_.load(std::memory_order_relaxed),
            outstanding_.load(std::memory_order_relaxed),
            maximumQueued_.load(std::memory_order_relaxed)};
  }

  [[nodiscard]] bool
  waitUntilEmpty(std::chrono::milliseconds timeout) noexcept {
    std::unique_lock lock{mutex_};
    return drained_.wait_for(lock, timeout, [this] {
      return outstanding_.load(std::memory_order_acquire) == 0U;
    });
  }

  void shutdown() noexcept {
    {
      std::scoped_lock lock{mutex_};
      accepting_ = false;
      stopping_ = true;
    }
    ready_.notify_one();
    if (consumer_.joinable())
      consumer_.join();
  }

private:
  void consume() noexcept {
    while (true) {
      std::optional<Artifact> artifact;
      {
        std::unique_lock lock{mutex_};
        ready_.wait(lock, [this] { return queued_ != 0U || stopping_; });
        if (queued_ == 0U && stopping_)
          break;
        artifact.emplace(std::move(*slots_[read_]));
        slots_[read_].reset();
        read_ = (read_ + 1U) % Capacity;
        --queued_;
      }
      artifact.reset();
      released_.fetch_add(1U, std::memory_order_relaxed);
      if (outstanding_.fetch_sub(1U, std::memory_order_acq_rel) == 1U)
        drained_.notify_all();
    }
  }

  mutable std::mutex mutex_;
  std::condition_variable ready_;
  std::condition_variable drained_;
  std::array<std::optional<Artifact>, Capacity> slots_;
  std::size_t read_ = 0;
  std::size_t write_ = 0;
  std::size_t queued_ = 0;
  bool accepting_ = true;
  bool stopping_ = false;
  std::thread consumer_;
  std::atomic<std::uint64_t> accepted_ = 0;
  std::atomic<std::uint64_t> released_ = 0;
  std::atomic<std::uint64_t> saturated_ = 0;
  std::atomic<std::uint64_t> contended_ = 0;
  std::atomic<std::uint64_t> closed_ = 0;
  std::atomic<std::size_t> outstanding_ = 0;
  std::atomic<std::size_t> maximumQueued_ = 0;
};

} // namespace kearne::ui
