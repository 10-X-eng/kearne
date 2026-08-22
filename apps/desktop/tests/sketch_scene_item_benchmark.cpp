#include "sketch_scene_fixture.hpp"
#include "sketch_scene_item.hpp"

#include <kearne/testkit/distribution.hpp>

#include <QGuiApplication>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>

namespace {

using namespace kearne;
using namespace kearne::render;
using namespace kearne::ui;
using namespace kearne::ui::test;

using Clock = std::chrono::steady_clock;

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

double milliseconds(Clock::time_point start, Clock::time_point finish) {
  return std::chrono::duration<double, std::milli>(finish - start).count();
}

void benchmark(std::size_t primitiveCount) {
  const auto source = scene(
      primitiveCount, primitiveCount,
      stamp(8U, primitiveCount, primitiveCount, primitiveCount, 8U,
            primitiveCount));
  SketchVectorUploadOptions options;
  options.maximumChunkBytes = 256U * 1024U;

  std::vector<double> preparationMilliseconds;
  std::shared_ptr<const PreparedSketchScene> prepared;
  for (std::size_t trial = 0U; trial < 7U; ++trial) {
    const auto start = Clock::now();
    auto result = prepareSketchScene(source, {}, options);
    const auto finish = Clock::now();
    require(result.has_value(), "native vector preparation failed");
    prepared = std::move(*result);
    preparationMilliseconds.push_back(milliseconds(start, finish));
  }
  require(prepared && prepared->packet() &&
              prepared->packet()->metrics().inputPrimitives == primitiveCount &&
              prepared->packet()->metrics().records == primitiveCount,
          "native vector packet accounting is inconsistent");

  auto transform = SketchViewTransform::create(
      {1U, {}, 0.0002, 0.0}, {1600.0, 900.0});
  require(transform.has_value(), "benchmark view was rejected");

  std::vector<double> visibilityMicroseconds;
  std::size_t visibleChunks = 0U;
  std::size_t visitedNodes = 0U;
  SketchChunkSequence required;
  for (std::size_t trial = 0U; trial < 7U; ++trial) {
    const auto start = Clock::now();
    auto visibility =
        ProgressiveSketchVisibility::create(prepared->packet(), *transform);
    require(visibility.has_value(), "native vector visibility failed");
    while (!visibility->complete()) {
      auto slice = visibility->takeNextSlice(256U, 64U);
      require(slice && slice->spatialNodesVisited <= 256U &&
                  slice->chunks.size() <= 64U,
              "native vector visibility exceeded its work bound");
    }
    const auto finish = Clock::now();
    visibleChunks = visibility->selectedChunks().size();
    visitedNodes = visibility->spatialNodesVisited();
    if (trial == 6U)
      required = visibility->releaseSelectedChunks();
    visibilityMicroseconds.push_back(
        std::chrono::duration<double, std::micro>(finish - start).count());
  }

  auto upload = ProgressiveSketchUpload::create(
      prepared->packet(), std::move(required),
      std::span<const std::shared_ptr<const SketchVectorChunk>>{});
  require(upload.has_value(), "native vector upload plan failed");
  std::vector<double> uploadSliceMicroseconds;
  std::size_t uploadBytes = 0U;
  std::size_t uploadChunks = 0U;
  while (!upload->complete()) {
    const auto start = Clock::now();
    auto slice = upload->takeNextSlice(512U * 1024U, 8U);
    const auto finish = Clock::now();
    require(slice && !slice->entries.empty() &&
                slice->bytes <= 512U * 1024U &&
                slice->entries.size() <= 8U,
            "native vector upload exceeded its work bound");
    uploadBytes += slice->bytes;
    uploadChunks += slice->entries.size();
    uploadSliceMicroseconds.push_back(
        std::chrono::duration<double, std::micro>(finish - start).count());
  }

  const auto preparation =
      testkit::summarizeDistribution(preparationMilliseconds);
  const auto visibility = testkit::summarizeDistribution(visibilityMicroseconds);
  const auto uploadSlices =
      testkit::summarizeDistribution(uploadSliceMicroseconds);
  require(preparation && visibility && uploadSlices,
          "native vector benchmark distribution failed");

  const SketchVectorPacketMetrics metrics = prepared->packet()->metrics();
  std::cout << primitiveCount << ',' << metrics.records << ','
            << metrics.dataRecords << ',' << metrics.chunks << ','
            << metrics.retainedBytes << ',' << metrics.peakBytes << ','
            << visibleChunks << ',' << visitedNodes << ',' << uploadChunks << ','
            << uploadBytes << ',' << std::fixed << std::setprecision(3)
            << preparation->p50 << ',' << preparation->p95 << ','
            << preparation->maximum << ',' << visibility->p50 << ','
            << visibility->p95 << ',' << visibility->maximum << ','
            << uploadSlices->p50 << ',' << uploadSlices->p95 << ','
            << uploadSlices->maximum << '\n';
}

} // namespace

int main(int argc, char *argv[]) {
  try {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication application(argc, argv);
    std::cout << "primitives,records,data_records,chunks,retained_bytes,"
                 "peak_bytes,visible_chunks,visited_nodes,upload_chunks,"
                 "upload_bytes,prepare_p50_ms,prepare_p95_ms,prepare_max_ms,"
                 "visibility_p50_us,visibility_p95_us,visibility_max_us,"
                 "upload_slice_p50_us,upload_slice_p95_us,upload_slice_max_us\n";
    for (std::size_t count : std::array{1'000U, 10'000U, 100'000U})
      benchmark(count);
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
