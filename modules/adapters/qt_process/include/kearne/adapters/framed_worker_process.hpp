#pragma once

#include <kearne/base/value.hpp>

#include <QByteArray>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stop_token>
#include <thread>

namespace kearne::adapters {

struct FramedWorkerProcessConfig {
  QString program;
  QStringList arguments;
  QProcessEnvironment environment;
  std::size_t maximumFrameBytes = 132'096U;
  std::size_t maximumStandardErrorBytes = 65'536U;
  int startTimeoutMilliseconds = 10'000;
  int jobTimeoutMilliseconds = 30'000;
};

// Thread-affine blocking adapter. Its owner calls exchange only from an
// engineering thread; the UI thread never constructs or waits on QProcess.
class FramedWorkerProcess final {
public:
  explicit FramedWorkerProcess(FramedWorkerProcessConfig config);
  ~FramedWorkerProcess();
  FramedWorkerProcess(const FramedWorkerProcess &) = delete;
  FramedWorkerProcess &operator=(const FramedWorkerProcess &) = delete;

  [[nodiscard]] Result<QByteArray> exchange(QByteArray payload,
                                            std::stop_token cancellation = {});
  void stop();
  [[nodiscard]] std::uint64_t processGeneration() const {
    return processGeneration_;
  }

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  std::uint64_t processGeneration_ = 0U;
};

} // namespace kearne::adapters
