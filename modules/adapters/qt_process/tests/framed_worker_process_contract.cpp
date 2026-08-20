#include <kearne/adapters/framed_worker_process.hpp>
#include <kearne/testkit/property.hpp>

#include <QCoreApplication>

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <stop_token>

namespace {

using kearne::adapters::FramedWorkerProcess;
using kearne::adapters::FramedWorkerProcessConfig;

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

FramedWorkerProcessConfig config() {
  return {QStringLiteral(KEARNE_TEST_PYTHON),
          {QStringLiteral(KEARNE_TEST_WORKER_SCRIPT)},
          QProcessEnvironment::systemEnvironment(),
          4'096U,
          4'096U,
          5'000,
          5'000};
}

} // namespace

int main(int argc, char **argv) {
  QCoreApplication application{argc, argv};
  FramedWorkerProcess worker{config()};
  const auto profile = kearne::testkit::propertyProfile();
  kearne::testkit::checkProperty(
      "warm framed exchange", profile,
      [&worker](kearne::testkit::Random &random, std::uint64_t index) {
        QByteArray request;
        request.resize(static_cast<qsizetype>(1U + index % 4'096U));
        for (char &byte : request)
          byte = static_cast<char>(random.next());
        auto response = worker.exchange(request);
        require(response.has_value(), "valid framed exchange failed");
        std::ranges::reverse(request);
        require(*response == request, "framed response was corrupted");
      });
  require(worker.processGeneration() == 1U,
          "warm exchanges did not reuse one worker process");

  std::stop_source cancelled;
  cancelled.request_stop();
  auto cancellation =
      worker.exchange(QByteArrayLiteral("cancelled"), cancelled.get_token());
  require(!cancellation &&
              cancellation.error().code == "worker.process.cancelled",
          "pre-dispatch cancellation was not rejected");

  auto exited = worker.exchange(QByteArrayLiteral("exit"));
  require(!exited && exited.error().code == "worker.process.exited",
          "early worker exit was not classified");
  auto restarted = worker.exchange(QByteArrayLiteral("restart"));
  require(restarted && *restarted == QByteArrayLiteral("tratser") &&
              worker.processGeneration() == 2U,
          "worker did not restart after process loss");

  auto oversized = worker.exchange(QByteArrayLiteral("oversize"));
  require(!oversized &&
              oversized.error().code == "worker.process.invalid-frame",
          "oversized result frame was not rejected");
  auto zero = worker.exchange(QByteArrayLiteral("zero"));
  require(!zero && zero.error().code == "worker.process.invalid-frame",
          "zero result frame was not rejected");

  auto logged = worker.exchange(QByteArrayLiteral("log"));
  require(!logged && logged.error().code == "worker.process.log-limit",
          "worker log flood was not bounded");
  worker.stop();
  return 0;
}
