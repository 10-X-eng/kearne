#pragma once

#include <cstddef>
#include <limits>
#include <stop_token>

namespace kearne::ui::detail {

struct SketchProjectionCancelled {};

class CancellationPoller final {
public:
  explicit CancellationPoller(std::stop_token token) : token_(token) {}

  void checkpoint(std::size_t work = 1U) {
    if (!token_.stop_possible())
      return;
    constexpr std::size_t interval = 256U;
    if (work < interval - work_) {
      work_ += work;
      return;
    }
    work_ = work - (interval - work_);
    work_ %= interval;
    checkpointNow();
  }

  void checkpointNow() const {
    if (token_.stop_requested())
      throw SketchProjectionCancelled{};
  }

private:
  std::stop_token token_;
  std::size_t work_ = 0U;
};

[[nodiscard]] constexpr bool
checkedSizeAdd(std::size_t first, std::size_t second, std::size_t &result) {
  if (second > std::numeric_limits<std::size_t>::max() - first)
    return false;
  result = first + second;
  return true;
}

[[nodiscard]] constexpr bool checkedSizeMultiply(std::size_t first,
                                                 std::size_t second,
                                                 std::size_t &result) {
  if (first != 0U && second > std::numeric_limits<std::size_t>::max() / first)
    return false;
  result = first * second;
  return true;
}

} // namespace kearne::ui::detail
