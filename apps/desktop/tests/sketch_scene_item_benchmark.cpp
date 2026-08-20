#include "sketch_scene_fixture.hpp"
#include "sketch_scene_item.hpp"
#include "sketch_stroke_pattern.hpp"

#include <kearne/testkit/distribution.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numbers>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace kearne;
using namespace kearne::render;
using namespace kearne::ui;
using namespace kearne::ui::test;

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

void validateMesh(const SketchSceneMesh &mesh) {
  std::size_t vertices = 0;
  std::size_t indices = 0;
  for (const auto &chunk : mesh.chunks()) {
    require(chunk->indices().size() % 3U == 0U,
            "benchmark mesh contains incomplete triangles");
    for (const std::uint32_t index : chunk->indices())
      require(index < chunk->vertices().size(),
              "benchmark mesh index is out of bounds");
    vertices += chunk->vertices().size();
    indices += chunk->indices().size();
  }
  require(vertices == mesh.metrics().vertices &&
              indices == mesh.metrics().indices,
          "benchmark mesh accounting is inconsistent");
}

struct DisplayedPickMeasurements {
  std::vector<double> microseconds;
  std::uint64_t queries = 0U;
  std::uint64_t hits = 0U;
  std::uint64_t misses = 0U;
  std::uint64_t refusals = 0U;
  std::uint64_t visitedNodes = 0U;
  std::uint64_t refinedTargets = 0U;
  std::uint64_t renderedSpanProbes = 0U;
  std::uint64_t renderedTriangleTests = 0U;
  std::uint64_t renderedPatternIntervals = 0U;
};

std::shared_ptr<const SketchSceneSnapshot>
fixedScene(SceneStamp sceneStamp, std::vector<SketchStyle> sceneStyles,
           std::vector<Point2d> points,
           std::vector<PackedSketchPrimitive> primitives) {
  auto created =
      SketchSceneSnapshot::create(std::move(sceneStamp), std::move(sceneStyles),
                                  std::move(points), std::move(primitives));
  require(created.has_value(), "displayed-pick benchmark scene was invalid");
  return std::make_shared<const SketchSceneSnapshot>(std::move(*created));
}

std::shared_ptr<const SynchronizedSketchScene>
displayedFrame(SketchScenePresenter &presenter,
               const std::shared_ptr<const PreparedSketchScene> &prepared,
               SketchCamera2d camera, QSizeF viewport,
               SketchPickCoveragePolicy pickCoverage = {}) {
  presenter.retarget(prepared->stamp().target);
  auto cameraDecision = presenter.publishCamera(camera);
  require(cameraDecision.has_value() &&
              *cameraDecision == SketchCameraDecision::Accepted,
          "displayed-pick benchmark camera was rejected");
  auto products = preparedProductPacket(prepared);
  auto offered = presenter.publish(products);
  require(offered.has_value() &&
              offered->decision == PreparedSketchSceneDecision::Accepted,
          "displayed-pick benchmark scene was rejected");
  auto transform = SketchViewTransform::create(camera, viewport);
  require(transform.has_value(),
          "displayed-pick benchmark transform was rejected");
  auto visible = prepared->mesh()->visibleChunks(*transform, pickCoverage);
  require(visible.has_value() && !visible->chunks.empty(),
          "displayed-pick benchmark produced no visible coverage");
  auto coverage = SketchPresentedChunkCoverage::create(
      *prepared->mesh(), std::move(visible->chunks));
  require(coverage.has_value() && (*coverage)->size() > 0U,
          "displayed-pick benchmark coverage was rejected");
  return std::make_shared<const SynchronizedSketchScene>(
      std::move(products), *transform, pickCoverage, std::move(*coverage));
}

void recordDisplayedPick(
    DisplayedPickMeasurements &measurements,
    const std::shared_ptr<const SynchronizedSketchScene> &frame,
    Result<SketchItemPickEvidence> result, double microseconds) {
  measurements.microseconds.push_back(microseconds);
  ++measurements.queries;
  if (!result) {
    if (result.error().code == "desktop.sketch.rendered-pick-budget") {
      ++measurements.refusals;
      return;
    }
    throw std::runtime_error(result.error().code);
  }

  const SketchItemPickEvidence &evidence = *result;
  require(
      evidence.scene == frame->scene()->stamp() &&
          evidence.latestAcceptedScene == frame->scene()->stamp() &&
          evidence.matchesLatestAcceptedScene &&
          evidence.cameraGeneration == frame->transform().camera().generation &&
          evidence.viewportLogical == frame->transform().viewportLogical() &&
          evidence.pickCoverage == frame->pickCoverage() &&
          evidence.renderedSpanProbes <=
              frame->pickCoverage().maximumRenderedSpanProbes &&
          evidence.renderedTriangleTests <=
              frame->pickCoverage().maximumRenderedTriangleTests &&
          evidence.renderedPatternIntervals <=
              frame->pickCoverage().maximumPatternIntervals,
      "displayed-pick evidence was not from the presented frame");
  measurements.visitedNodes += evidence.analyticMetrics.visitedNodes;
  measurements.refinedTargets += evidence.analyticMetrics.refinedTargets;
  measurements.renderedSpanProbes += evidence.renderedSpanProbes;
  measurements.renderedTriangleTests += evidence.renderedTriangleTests;
  measurements.renderedPatternIntervals += evidence.renderedPatternIntervals;
  if (!evidence.hit) {
    require(!evidence.displayedDistanceLogicalPixels,
            "displayed-pick miss retained a winner distance");
    ++measurements.misses;
    return;
  }

  require(evidence.displayedDistanceLogicalPixels.has_value() &&
              std::isfinite(*evidence.displayedDistanceLogicalPixels) &&
              *evidence.displayedDistanceLogicalPixels >= 0.0 &&
              evidence.renderedSpanProbes > 0U &&
              evidence.renderedTriangleTests > 0U,
          "displayed-pick hit omitted rendered coverage evidence");
  const auto spans = frame->prepared()->primitiveTessellationIndex()->spans(
      evidence.hit->primitive);
  require(std::ranges::any_of(spans,
                              [&](SketchPrimitiveChunkSpan span) {
                                return frame->presentedChunks()->contains(
                                    span.chunk);
                              }),
          "displayed-pick hit did not reference a resident chunk");
  ++measurements.hits;
}

Result<SketchItemPickEvidence>
timedDisplayedPick(SketchScenePresenter &presenter,
                   const std::shared_ptr<const SynchronizedSketchScene> &frame,
                   QPointF item, double toleranceLogicalPixels,
                   SketchPickTargets targets, double &microseconds) {
  using Clock = std::chrono::steady_clock;
  const auto start = Clock::now();
  auto result = presenter.pick(frame, item, toleranceLogicalPixels, targets);
  const auto finish = Clock::now();
  microseconds =
      std::chrono::duration<double, std::micro>(finish - start).count();
  return result;
}

void exerciseAdversarialDisplayedPicks(DisplayedPickMeasurements &measurements,
                                       std::uint64_t seed) {
  constexpr SketchPrimitiveFlags selectable =
      SketchPrimitiveFlags::Visible | SketchPrimitiveFlags::Selectable;
  const SketchCamera2d camera{2U, {}, 0.001, 0.0};
  const QSizeF viewport{1'000.0, 240.0};
  SketchUploadOptions narrowTiles;
  narrowTiles.spatialTileLogicalPixels = 8.0;

  const std::vector<SketchStyle> dashedStyle{{SketchStyleRole::Construction,
                                              SketchLinePattern::Dashed, 2.0F,
                                              8.0F, 0U}};
  auto patternedScene = fixedScene(
      stamp(seed, 1U, seed, seed, seed, 1U), dashedStyle,
      {{-0.05, 0.0}, {0.05, 0.0}},
      {{id<SketchEntityId>(seed + 1U), *SketchPrimitiveHandle::create(1U), 0U,
        0U, SketchPrimitiveKind::Line, selectable, 0.0, 0.0, 0.0}});
  auto patternedPrepared = prepareSketchScene(
      patternedScene,
      SketchCurveLod::forMetresPerLogicalPixel(camera.metresPerLogicalPixel),
      {}, {}, narrowTiles);
  require(patternedPrepared.has_value(),
          "patterned displayed-pick preparation failed");
  SketchScenePresenter patternedPresenter;
  auto patternedFrame =
      displayedFrame(patternedPresenter, *patternedPrepared, camera, viewport);
  const SketchStrokePattern pattern = strokePattern(dashedStyle.front());
  require(pattern.onLogicalPixels > 0.0F &&
              pattern.periodLogicalPixels > pattern.onLogicalPixels,
          "patterned displayed-pick style had no gap");
  const QPointF patternStart = patternedFrame->transform().toItem({-0.05, 0.0});
  const QPointF onItem{patternStart.x() +
                           static_cast<double>(pattern.onLogicalPixels) * 0.5,
                       patternStart.y()};
  const QPointF gapItem{patternStart.x() +
                            static_cast<double>(pattern.onLogicalPixels +
                                                pattern.periodLogicalPixels) *
                                0.5,
                        patternStart.y()};
  for (std::size_t index = 0U; index < 32U; ++index) {
    const bool onStroke = index % 2U == 0U;
    const QPointF item = onStroke ? onItem : gapItem;
    double elapsed = 0.0;
    auto picked = timedDisplayedPick(patternedPresenter, patternedFrame, item,
                                     0.0, SketchPickTargets::Curves, elapsed);
    require(picked.has_value() && picked->hit.has_value() == onStroke &&
                picked->renderedPatternIntervals > 0U,
            "patterned displayed-pick did not follow rendered dash coverage");
    recordDisplayedPick(measurements, patternedFrame, std::move(picked),
                        elapsed);
  }

  const std::vector<SketchStyle> solidStyle{
      {SketchStyleRole::Regular, SketchLinePattern::Solid, 2.0F, 8.0F, 0U}};
  auto longScene = fixedScene(
      stamp(seed + 1U, 1U, seed + 1U, seed + 1U, seed + 1U, 1U), solidStyle,
      {{0.0, 0.0}},
      {{id<SketchEntityId>(seed + 2U), *SketchPrimitiveHandle::create(1U), 0U,
        0U, SketchPrimitiveKind::Circle, selectable, 0.4, 0.0, 0.0}});
  auto longPrepared = prepareSketchScene(
      longScene,
      SketchCurveLod::forMetresPerLogicalPixel(camera.metresPerLogicalPixel),
      {}, {}, narrowTiles);
  require(longPrepared.has_value(),
          "long-span displayed-pick preparation failed");
  const auto longSpans = (*longPrepared)
                             ->primitiveTessellationIndex()
                             ->spans(*SketchPrimitiveHandle::create(1U));
  const auto patternedSpans = (*patternedPrepared)
                                  ->primitiveTessellationIndex()
                                  ->spans(*SketchPrimitiveHandle::create(1U));
  require(longSpans.size() > patternedSpans.size(),
          "long-span displayed-pick fixture did not cross enough chunks");
  SketchScenePresenter longPresenter;
  auto longFrame =
      displayedFrame(longPresenter, *longPrepared, camera, viewport);
  constexpr double longPickToleranceLogicalPixels = 2.0;
  const double outsideStrokeLogicalPixels =
      static_cast<double>(solidStyle.front().strokeWidthPixels) * 0.5 +
      longPickToleranceLogicalPixels * 0.75;
  const QPointF longPickItem = longFrame->transform().toItem(
      {0.4 + outsideStrokeLogicalPixels * camera.metresPerLogicalPixel, 0.0});
  for (std::size_t index = 0U; index < 16U; ++index) {
    double elapsed = 0.0;
    auto picked = timedDisplayedPick(longPresenter, longFrame, longPickItem,
                                     longPickToleranceLogicalPixels,
                                     SketchPickTargets::Curves, elapsed);
    if (!picked)
      throw std::runtime_error(picked.error().code);
    if (!picked->hit)
      throw std::runtime_error("long-span displayed-pick missed the stroke");
    if (picked->renderedSpanProbes != longSpans.size())
      throw std::runtime_error("long-span displayed-pick span mismatch: " +
                               std::to_string(picked->renderedSpanProbes) +
                               "/" + std::to_string(longSpans.size()));
    recordDisplayedPick(measurements, longFrame, std::move(picked), elapsed);
  }

  SketchPickCoveragePolicy limitedSpans;
  limitedSpans.maximumRenderedSpanProbes = 1U;
  SketchScenePresenter limitedPresenter;
  auto limitedFrame = displayedFrame(limitedPresenter, *longPrepared, camera,
                                     viewport, limitedSpans);
  double limitedElapsed = 0.0;
  auto limited =
      timedDisplayedPick(limitedPresenter, limitedFrame, longPickItem,
                         longPickToleranceLogicalPixels,
                         SketchPickTargets::Curves, limitedElapsed);
  require(!limited &&
              limited.error().code == "desktop.sketch.rendered-pick-budget",
          "long-span displayed-pick returned a partial bounded result");
  recordDisplayedPick(measurements, limitedFrame, std::move(limited),
                      limitedElapsed);

  const std::size_t denseCount =
      static_cast<std::size_t>(
          SketchPickIndexOptions{}.maximumRefinedTargetsPerPass) *
      2U;
  std::vector<Point2d> densePoints(denseCount, Point2d{});
  std::vector<PackedSketchPrimitive> densePrimitives;
  densePrimitives.reserve(denseCount);
  for (std::size_t index = 0U; index < denseCount; ++index)
    densePrimitives.push_back(
        {id<SketchEntityId>(seed + 3U + index),
         *SketchPrimitiveHandle::create(static_cast<std::uint32_t>(index + 1U)),
         static_cast<std::uint32_t>(index), 0U, SketchPrimitiveKind::Point,
         selectable, 0.0, 0.0, 0.0});
  auto denseScene = fixedScene(
      stamp(seed + 2U, 1U, seed + 2U, seed + 2U, seed + 2U, 1U), solidStyle,
      std::move(densePoints), std::move(densePrimitives));
  auto densePrepared = prepareSketchScene(
      denseScene,
      SketchCurveLod::forMetresPerLogicalPixel(camera.metresPerLogicalPixel));
  require(densePrepared.has_value(), "dense displayed-pick preparation failed");
  SketchScenePresenter densePresenter;
  auto denseFrame =
      displayedFrame(densePresenter, *densePrepared, camera, viewport);
  for (std::size_t index = 0U; index < 16U; ++index) {
    double elapsed = 0.0;
    auto picked = timedDisplayedPick(densePresenter, denseFrame, {500.0, 120.0},
                                     0.0, SketchPickTargets::Points, elapsed);
    require(!picked &&
                picked.error().code == "desktop.sketch.rendered-pick-budget",
            "dense displayed-pick escaped its bounded refusal");
    recordDisplayedPick(measurements, denseFrame, std::move(picked), elapsed);
  }
}

void benchmark(std::size_t primitiveCount) {
  using Clock = std::chrono::steady_clock;
  const SceneStamp sceneStamp = stamp(8, primitiveCount, primitiveCount,
                                      primitiveCount, 8, primitiveCount);
  auto generated = scene(primitiveCount, primitiveCount, sceneStamp);
  constexpr double metresPerPixel = 0.0002;
  const SketchCurveLod lod =
      SketchCurveLod::forMetresPerLogicalPixel(metresPerPixel);

  auto validatedPrepared = prepareSketchScene(generated, lod);
  require(validatedPrepared.has_value(),
          "benchmark validation preparation failed");
  validateMesh(*(*validatedPrepared)->mesh());

  SketchScenePresenter validatedPresenter;
  validatedPresenter.retarget(sceneStamp.target);
  require(validatedPresenter.publishCamera({2, {}, metresPerPixel, 0.0}) ==
              SketchCameraDecision::Accepted,
          "benchmark validation camera failed");
  require(validatedPresenter.publish(preparedProductPacket(*validatedPrepared))
              .has_value(),
          "benchmark validation publication failed");
  auto validatedFrame = validatedPresenter.synchronize({1600.0, 900.0});
  require(validatedFrame.has_value(),
          "benchmark validation synchronization failed");
  auto visible = (*validatedPrepared)
                     ->mesh()
                     ->visibleChunks((*validatedFrame)->transform());
  require(visible.has_value(), "benchmark visible chunk selection failed");
  auto displayedCoverage = SketchPresentedChunkCoverage::create(
      *(*validatedPrepared)->mesh(), visible->chunks);
  require(displayedCoverage.has_value(),
          "benchmark displayed coverage construction failed");
  auto displayed = std::make_shared<const SynchronizedSketchScene>(
      (*validatedFrame)->products(), (*validatedFrame)->transform(),
      SketchPickCoveragePolicy{}, std::move(*displayedCoverage));

  std::vector<QPointF> visiblePointQueries;
  for (const PackedSketchPrimitive &primitive : generated->primitives()) {
    if (primitive.kind != SketchPrimitiveKind::Point)
      continue;
    const QPointF item = displayed->transform().toItem(
        generated->points()[primitive.firstPoint]);
    if (item.x() >= 0.0 &&
        item.x() <= displayed->transform().viewportLogical().width() &&
        item.y() >= 0.0 &&
        item.y() <= displayed->transform().viewportLogical().height())
      visiblePointQueries.push_back(item);
  }
  require(!visiblePointQueries.empty(),
          "benchmark scene had no visible exact-pick points");
  DisplayedPickMeasurements displayedPicks;
  displayedPicks.microseconds.reserve(2'065U);
  for (std::size_t index = 0U; index < 2'000U; ++index) {
    double elapsed = 0.0;
    auto picked = timedDisplayedPick(
        validatedPresenter, displayed,
        visiblePointQueries[index % visiblePointQueries.size()], 0.0,
        SketchPickTargets::Points, elapsed);
    require(picked.has_value() && picked->hit,
            "benchmark visible point was absent from displayed coverage");
    recordDisplayedPick(displayedPicks, displayed, std::move(picked), elapsed);
  }
  exerciseAdversarialDisplayedPicks(displayedPicks,
                                    primitiveCount * 4U + 10'000U);

  std::vector<double> preparationMilliseconds;
  std::uint64_t checksum = 0;
  for (std::size_t trial = 0; trial < 7U; ++trial) {
    const auto start = Clock::now();
    auto prepared = prepareSketchScene(generated, lod);
    const auto finish = Clock::now();
    require(prepared.has_value(), "timed scene preparation failed");
    preparationMilliseconds.push_back(
        std::chrono::duration<double, std::milli>(finish - start).count());
    checksum += (*prepared)->mesh()->metrics().indices;
  }

  std::vector<double> synchronizeMilliseconds;
  for (std::size_t trial = 0; trial < 7U; ++trial) {
    SketchScenePresenter presenter;
    presenter.retarget(sceneStamp.target);
    require(presenter.publishCamera({2, {}, metresPerPixel, 0.0}) ==
                SketchCameraDecision::Accepted,
            "timed synchronization camera failed");
    require(presenter.publish(preparedProductPacket(*validatedPrepared))
                .has_value(),
            "timed synchronization publication failed");
    const auto start = Clock::now();
    auto frame = presenter.synchronize({1600.0, 900.0});
    const auto finish = Clock::now();
    require(frame.has_value(), "timed synchronization failed");
    synchronizeMilliseconds.push_back(
        std::chrono::duration<double, std::milli>(finish - start).count());
    checksum += (*frame)->pickIndex()->indexedReferenceCount();
    require(presenter.synchronizationMetrics().scalablePreparations == 0U,
            "timed synchronization performed scalable preparation");
  }

  SketchScenePresenter cameraPresenter;
  cameraPresenter.retarget(sceneStamp.target);
  require(cameraPresenter.publishCamera({2, {}, metresPerPixel, 0.0}) ==
              SketchCameraDecision::Accepted,
          "camera stress initial camera failed");
  require(cameraPresenter.publish(preparedProductPacket(*validatedPrepared))
              .has_value(),
          "camera stress publication failed");
  auto cameraFrame = cameraPresenter.synchronize({1600.0, 900.0});
  require(cameraFrame.has_value(), "camera stress initial frame failed");
  const auto retainedMesh = (*cameraFrame)->mesh();
  std::vector<double> cameraMicroseconds;
  cameraMicroseconds.reserve(2'000U);
  for (std::uint64_t generation = 3; generation < 2'003; ++generation) {
    const SketchCamera2d moving{generation,
                                {static_cast<double>(generation % 127U) * 0.1,
                                 -static_cast<double>(generation % 113U) * 0.1},
                                metresPerPixel,
                                static_cast<double>(generation % 360U) *
                                    std::numbers::pi / 180.0};
    const QSizeF viewport{640.0 + static_cast<double>(generation % 1'600U),
                          480.0 + static_cast<double>(generation % 900U)};
    const auto start = Clock::now();
    auto cameraDecision = cameraPresenter.publishCamera(moving);
    cameraFrame = cameraPresenter.synchronize(viewport);
    require(cameraFrame.has_value(), "camera stress frame failed");
    const auto finish = Clock::now();
    require(cameraDecision == SketchCameraDecision::Accepted &&
                (*cameraFrame)->mesh() == retainedMesh,
            "camera stress update failed");
    cameraMicroseconds.push_back(
        std::chrono::duration<double, std::micro>(finish - start).count());
    checksum += retainedMesh->metrics().batches;
  }
  require(cameraPresenter.synchronizationMetrics().scalablePreparations == 0U,
          "camera stress performed scalable preparation");

  std::vector<double> visibilitySliceMicroseconds;
  std::size_t maximumVisibilityNodes = 0;
  std::size_t maximumVisibilityChunks = 0;
  for (std::size_t trial = 0; trial < 7U; ++trial) {
    auto progressiveVisibility = ProgressiveSketchVisibility::create(
        (*validatedPrepared)->mesh(), (*validatedFrame)->transform());
    require(progressiveVisibility.has_value(),
            "benchmark progressive visibility failed");
    std::size_t selectedCount = 0;
    while (!progressiveVisibility->complete()) {
      const auto start = Clock::now();
      auto slice = progressiveVisibility->takeNextSlice(
          SketchGpuUploadPolicy::maximumSpatialNodesPerFrame,
          SketchGpuUploadPolicy::maximumVisibleChunksPerFrame);
      const auto finish = Clock::now();
      require(slice && slice->spatialNodesVisited > 0U,
              "benchmark visibility slice did not progress");
      visibilitySliceMicroseconds.push_back(
          std::chrono::duration<double, std::micro>(finish - start).count());
      maximumVisibilityNodes =
          std::max(maximumVisibilityNodes, slice->spatialNodesVisited);
      maximumVisibilityChunks =
          std::max(maximumVisibilityChunks, slice->chunks.size());
      selectedCount += slice->chunks.size();
    }
    require(selectedCount == visible->chunks.size() &&
                progressiveVisibility->spatialNodesVisited() ==
                    visible->spatialNodesVisited,
            "benchmark progressive visibility disagreed with full query");
    checksum += selectedCount;
  }
  std::vector<double> uploadSliceMicroseconds;
  std::size_t maximumSliceBytes = 0;
  std::size_t maximumSliceChunks = 0;
  for (std::size_t trial = 0; trial < 7U; ++trial) {
    auto progressive = ProgressiveSketchUpload::create(
        *validatedPrepared, visible->chunks,
        std::span<const std::shared_ptr<const SketchUploadChunk>>{});
    require(progressive.has_value(), "benchmark upload schedule failed");
    while (!progressive->complete()) {
      const auto start = Clock::now();
      auto slice = progressive->takeNextSlice(
          SketchGpuUploadPolicy::maximumBytesPerFrame,
          SketchGpuUploadPolicy::maximumChunksPerFrame);
      require(slice && !slice->entries.empty(),
              "benchmark upload slice did not progress");
      std::vector<std::byte> copied(slice->bytes);
      std::size_t offset = 0;
      for (const SketchUploadSliceEntry entry : slice->entries) {
        const std::uint32_t index = entry.chunk;
        const auto chunk = (*validatedPrepared)->mesh()->chunks()[index];
        const auto vertices = chunk->vertices();
        const std::size_t vertexBytes =
            vertices.size() * sizeof(SketchMeshVertex);
        std::memcpy(copied.data() + offset, vertices.data(), vertexBytes);
        offset += vertexBytes;
        const auto indices = chunk->indices();
        const std::size_t indexBytes = indices.size() * sizeof(std::uint32_t);
        std::memcpy(copied.data() + offset, indices.data(), indexBytes);
        offset += indexBytes;
      }
      const auto finish = Clock::now();
      require(offset == copied.size(), "benchmark upload copy was incomplete");
      uploadSliceMicroseconds.push_back(
          std::chrono::duration<double, std::micro>(finish - start).count());
      maximumSliceBytes = std::max(maximumSliceBytes, slice->bytes);
      maximumSliceChunks = std::max(maximumSliceChunks, slice->entries.size());
      checksum += slice->bytes + slice->entries.size();
    }
  }

  const auto preparationDistribution =
      testkit::summarizeDistribution(preparationMilliseconds);
  const auto synchronizeDistribution =
      testkit::summarizeDistribution(synchronizeMilliseconds);
  const auto cameraDistribution =
      testkit::summarizeDistribution(cameraMicroseconds);
  const auto uploadSliceDistribution =
      testkit::summarizeDistribution(uploadSliceMicroseconds);
  const auto visibilitySliceDistribution =
      testkit::summarizeDistribution(visibilitySliceMicroseconds);
  const auto displayedPickDistribution =
      testkit::summarizeDistribution(displayedPicks.microseconds);
  require(preparationDistribution && synchronizeDistribution &&
              cameraDistribution && visibilitySliceDistribution &&
              uploadSliceDistribution && displayedPickDistribution,
          "benchmark distribution summary failed");
  require(displayedPicks.queries == displayedPicks.microseconds.size() &&
              displayedPicks.queries == displayedPicks.hits +
                                            displayedPicks.misses +
                                            displayedPicks.refusals &&
              displayedPicks.hits > 0U && displayedPicks.misses > 0U &&
              displayedPicks.refusals > 0U &&
              displayedPicks.renderedSpanProbes > 0U &&
              displayedPicks.renderedTriangleTests > 0U &&
              displayedPicks.renderedPatternIntervals > 0U,
          "displayed-pick outcome accounting is inconsistent");
  const SketchMeshMetrics resources = (*validatedPrepared)->mesh()->metrics();
  std::cout
      << primitiveCount << ',' << resources.vertices << ',' << resources.indices
      << ',' << resources.bytes << ',' << resources.retainedMeshBytes << ','
      << resources.peakPreparationMeshBytes << ','
      << (*validatedPrepared)->mesh()->chunks().size() << ','
      << visible->chunks.size() << ',' << visible->spatialNodesVisited << ','
      << maximumVisibilityNodes << ',' << maximumVisibilityChunks << ','
      << maximumSliceBytes << ',' << maximumSliceChunks << ',' << std::fixed
      << std::setprecision(3) << preparationDistribution->p50 << ','
      << preparationDistribution->p95 << ',' << preparationDistribution->p99
      << ',' << preparationDistribution->maximum << ','
      << synchronizeDistribution->p50 << ',' << synchronizeDistribution->p95
      << ',' << synchronizeDistribution->p99 << ','
      << synchronizeDistribution->maximum << ',' << cameraDistribution->p50
      << ',' << cameraDistribution->p95 << ',' << cameraDistribution->p99 << ','
      << cameraDistribution->maximum << ',' << visibilitySliceDistribution->p50
      << ',' << visibilitySliceDistribution->p95 << ','
      << visibilitySliceDistribution->p99 << ','
      << visibilitySliceDistribution->maximum << ','
      << uploadSliceDistribution->p50 << ',' << uploadSliceDistribution->p95
      << ',' << uploadSliceDistribution->p99 << ','
      << uploadSliceDistribution->maximum << ','
      << displayedPickDistribution->p50 << ',' << displayedPickDistribution->p95
      << ',' << displayedPickDistribution->p99 << ','
      << displayedPickDistribution->maximum << ',' << displayedPicks.queries
      << ',' << displayedPicks.hits << ',' << displayedPicks.misses << ','
      << displayedPicks.refusals << ',' << displayedPicks.visitedNodes << ','
      << displayedPicks.refinedTargets << ','
      << displayedPicks.renderedSpanProbes << ','
      << displayedPicks.renderedTriangleTests << ','
      << displayedPicks.renderedPatternIntervals << ',' << checksum << '\n';
}

} // namespace

int main() {
  try {
    std::cout << "primitives,vertices,indices,bytes,retained_mesh_bytes,"
                 "peak_prepare_mesh_bytes,chunks,visible_chunks,"
                 "spatial_nodes_visited,max_visibility_nodes,"
                 "max_visibility_chunks,max_slice_bytes,max_slice_chunks,"
                 "prepare_p50_ms,prepare_p95_ms,prepare_p99_ms,prepare_max_ms,"
                 "sync_p50_ms,sync_p95_ms,sync_p99_ms,sync_max_ms,"
                 "camera_p50_us,camera_p95_us,camera_p99_us,camera_max_us,"
                 "visibility_slice_p50_us,visibility_slice_p95_us,"
                 "visibility_slice_p99_us,visibility_slice_max_us,"
                 "upload_slice_p50_us,upload_slice_p95_us,"
                 "upload_slice_p99_us,upload_slice_max_us,"
                 "displayed_pick_p50_us,displayed_pick_p95_us,"
                 "displayed_pick_p99_us,displayed_pick_max_us,pick_queries,"
                 "pick_hits,pick_misses,pick_refusals,pick_visited_nodes,"
                 "pick_refined_targets,rendered_span_probes,"
                 "rendered_triangle_tests,rendered_pattern_intervals,"
                 "checksum\n";
    for (const std::size_t size : std::array{1'000U, 10'000U, 100'000U})
      benchmark(size);
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
