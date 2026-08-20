#include <kearne/adapters/framed_worker_process.hpp>

#include <QDeadlineTimer>
#include <QProcess>

#include <algorithm>
#include <limits>
#include <utility>

namespace kearne::adapters {
namespace {

constexpr int cancellationPollMilliseconds = 25;

Diagnostic invalidConfig(std::string summary) {
  return diagnostic("worker.process.invalid-config", std::move(summary));
}

} // namespace

struct FramedWorkerProcess::Impl final {
  explicit Impl(FramedWorkerProcessConfig configured)
      : config(std::move(configured)) {}

  FramedWorkerProcessConfig config;
  std::unique_ptr<QProcess> process;
  QByteArray received;
  std::size_t standardErrorBytes = 0U;
  std::thread::id owner;

  [[nodiscard]] Result<void> claimThread() {
    const std::thread::id current = std::this_thread::get_id();
    if (owner == std::thread::id{}) {
      owner = current;
      return {};
    }
    if (owner != current)
      return std::unexpected(diagnostic(
          "worker.process.wrong-thread",
          "worker process was accessed outside its engineering thread"));
    return {};
  }

  void terminate() {
    received.clear();
    standardErrorBytes = 0U;
    if (!process)
      return;
    if (process->state() != QProcess::NotRunning) {
      process->terminate();
      if (!process->waitForFinished(250)) {
        process->kill();
        process->waitForFinished(1'000);
      }
    }
    process.reset();
  }

  [[nodiscard]] Result<void> drainStandardError() {
    if (!process)
      return {};
    process->setReadChannel(QProcess::StandardError);
    const qint64 available = process->bytesAvailable();
    if (available > 0) {
      const std::size_t bytes = static_cast<std::size_t>(available);
      if (bytes >
          config.maximumStandardErrorBytes -
              std::min(standardErrorBytes, config.maximumStandardErrorBytes)) {
        terminate();
        return std::unexpected(
            diagnostic("worker.process.log-limit",
                       "worker standard error exceeded its limit"));
      }
      standardErrorBytes += bytes;
      static_cast<void>(process->read(available));
    }
    process->setReadChannel(QProcess::StandardOutput);
    return {};
  }

  [[nodiscard]] Result<void> waitForOutput(QDeadlineTimer &deadline,
                                           std::stop_token cancellation) {
    if (cancellation.stop_requested()) {
      terminate();
      return std::unexpected(
          diagnostic("worker.process.cancelled", "worker job was cancelled"));
    }
    const qint64 remaining = deadline.remainingTime();
    if (remaining <= 0) {
      terminate();
      return std::unexpected(
          diagnostic("worker.process.timeout", "worker job timed out"));
    }
    const int wait = static_cast<int>(
        std::min<qint64>(remaining, cancellationPollMilliseconds));
    static_cast<void>(process->waitForReadyRead(wait));
    if (auto drained = drainStandardError(); !drained)
      return drained;
    if (process->state() == QProcess::NotRunning) {
      terminate();
      return std::unexpected(diagnostic(
          "worker.process.exited", "worker exited before returning a result"));
    }
    return {};
  }

  [[nodiscard]] Result<void> ensureStarted(std::uint64_t &generation) {
    if (process && process->state() != QProcess::NotRunning)
      return {};
    terminate();
    process = std::make_unique<QProcess>();
    process->setProcessChannelMode(QProcess::SeparateChannels);
    process->setProcessEnvironment(config.environment);
    process->setProgram(config.program);
    process->setArguments(config.arguments);
    process->setReadChannel(QProcess::StandardOutput);
    process->start(QIODevice::ReadWrite);
    if (!process->waitForStarted(config.startTimeoutMilliseconds)) {
      terminate();
      return std::unexpected(
          diagnostic("worker.process.start", "worker process did not start"));
    }
    if (generation == std::numeric_limits<std::uint64_t>::max()) {
      terminate();
      return std::unexpected(diagnostic(
          "worker.process.generation-exhausted",
          "worker process generation is exhausted", Severity::Fatal));
    }
    ++generation;
    return {};
  }

  [[nodiscard]] Result<void> writeFrame(const QByteArray &payload,
                                        QDeadlineTimer &deadline,
                                        std::stop_token cancellation) {
    QByteArray frame;
    frame.resize(4);
    const auto size = static_cast<std::uint32_t>(payload.size());
    frame[0] = static_cast<char>(size >> 24U);
    frame[1] = static_cast<char>(size >> 16U);
    frame[2] = static_cast<char>(size >> 8U);
    frame[3] = static_cast<char>(size);
    frame.append(payload);
    qsizetype offset = 0;
    while (offset < frame.size()) {
      const qint64 written =
          process->write(frame.constData() + offset, frame.size() - offset);
      if (written <= 0) {
        terminate();
        return std::unexpected(diagnostic(
            "worker.process.write", "worker request could not be written"));
      }
      offset += written;
    }
    while (process->bytesToWrite() > 0) {
      if (cancellation.stop_requested() || deadline.hasExpired()) {
        terminate();
        return std::unexpected(diagnostic(
            cancellation.stop_requested() ? "worker.process.cancelled"
                                          : "worker.process.timeout",
            cancellation.stop_requested() ? "worker job was cancelled"
                                          : "worker job timed out"));
      }
      const qint64 remaining = deadline.remainingTime();
      const int wait = static_cast<int>(
          std::min<qint64>(remaining, cancellationPollMilliseconds));
      if (!process->waitForBytesWritten(wait) &&
          process->state() == QProcess::NotRunning) {
        terminate();
        return std::unexpected(diagnostic(
            "worker.process.exited", "worker exited while receiving a job"));
      }
      if (auto drained = drainStandardError(); !drained)
        return drained;
    }
    return {};
  }

  [[nodiscard]] Result<QByteArray> readFrame(QDeadlineTimer &deadline,
                                             std::stop_token cancellation) {
    const auto fill = [&](qsizetype required) -> Result<void> {
      while (received.size() < required) {
        process->setReadChannel(QProcess::StandardOutput);
        const qint64 available = process->bytesAvailable();
        if (available > 0) {
          const qsizetype permitted =
              static_cast<qsizetype>(config.maximumFrameBytes + 4U) -
              received.size();
          if (available > permitted) {
            terminate();
            return std::unexpected(
                diagnostic("worker.process.output-limit",
                           "worker output exceeded its frame limit"));
          }
          received.append(process->read(available));
          continue;
        }
        if (auto waited = waitForOutput(deadline, cancellation); !waited)
          return waited;
      }
      return {};
    };
    if (auto header = fill(4); !header)
      return std::unexpected(std::move(header.error()));
    const auto byte = [this](qsizetype index) {
      return static_cast<std::uint32_t>(
          static_cast<unsigned char>(received[index]));
    };
    const std::uint32_t size =
        (byte(0) << 24U) | (byte(1) << 16U) | (byte(2) << 8U) | byte(3);
    if (size == 0U || size > config.maximumFrameBytes) {
      terminate();
      return std::unexpected(diagnostic("worker.process.invalid-frame",
                                        "worker result frame is invalid"));
    }
    const qsizetype required = 4 + static_cast<qsizetype>(size);
    if (auto body = fill(required); !body)
      return std::unexpected(std::move(body.error()));
    QByteArray result = received.mid(4, static_cast<qsizetype>(size));
    received.remove(0, required);
    standardErrorBytes = 0U;
    return result;
  }
};

FramedWorkerProcess::FramedWorkerProcess(FramedWorkerProcessConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}

FramedWorkerProcess::~FramedWorkerProcess() { impl_->terminate(); }

Result<QByteArray> FramedWorkerProcess::exchange(QByteArray payload,
                                                 std::stop_token cancellation) {
  if (auto thread = impl_->claimThread(); !thread)
    return std::unexpected(std::move(thread.error()));
  if (impl_->config.program.isEmpty())
    return std::unexpected(
        invalidConfig("worker executable is not configured"));
  if (impl_->config.maximumFrameBytes == 0U ||
      impl_->config.maximumFrameBytes >
          static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) ||
      impl_->config.maximumStandardErrorBytes == 0U ||
      impl_->config.startTimeoutMilliseconds <= 0 ||
      impl_->config.jobTimeoutMilliseconds <= 0)
    return std::unexpected(invalidConfig("worker limits are invalid"));
  if (payload.isEmpty() || static_cast<std::size_t>(payload.size()) >
                               impl_->config.maximumFrameBytes)
    return std::unexpected(diagnostic("worker.process.input-limit",
                                      "worker request exceeds its frame"));
  if (cancellation.stop_requested())
    return std::unexpected(
        diagnostic("worker.process.cancelled", "worker job was cancelled"));
  if (auto started = impl_->ensureStarted(processGeneration_); !started)
    return std::unexpected(std::move(started.error()));
  QDeadlineTimer deadline{impl_->config.jobTimeoutMilliseconds};
  if (auto written = impl_->writeFrame(payload, deadline, cancellation);
      !written)
    return std::unexpected(std::move(written.error()));
  return impl_->readFrame(deadline, cancellation);
}

void FramedWorkerProcess::stop() {
  if (impl_->claimThread())
    impl_->terminate();
}

} // namespace kearne::adapters
