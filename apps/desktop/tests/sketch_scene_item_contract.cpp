#include "sketch_scene_fixture.hpp"
#include "sketch_scene_item.hpp"
#include "sketch_vector_packet.hpp"

#include <kearne/testkit/property.hpp>

#include <QGuiApplication>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <numbers>
#include <stdexcept>
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

void requirePacket(const SketchSceneSnapshot &source,
                   const PreparedSketchScene &prepared) {
  require(prepared.scene().get() == &source && prepared.packet() &&
              prepared.primitiveVectorIndex() && prepared.pickIndex(),
          "prepared scene lost an immutable dependency");
  const auto chunks = prepared.packet()->chunks();
  std::size_t records = 0U;
  std::size_t dataRecords = 0U;
  bool containsLowDegreeNurbs = false;
  bool containsGeneralNurbs = false;
  for (const auto &chunk : chunks) {
    require(chunk && !chunk->records().empty() &&
                chunk->payloadBytes() ==
                    chunk->records().size() * sizeof(SketchVectorRecord) +
                        chunk->data().size() * sizeof(SketchVectorData),
            "vector chunk payload is inconsistent");
    require(chunk->style() < prepared.packet()->styles().size(),
            "vector chunk style is out of range");
    for (const SketchVectorRecord &record : chunk->records()) {
      const bool isNurbs = record.meta[0] == static_cast<std::uint32_t>(
                                                 SketchVectorKind::BSpline);
      const SketchVectorShaderFamily expectedFamily =
          !isNurbs               ? SketchVectorShaderFamily::Basic
          : record.meta[2] <= 3U ? SketchVectorShaderFamily::NurbsLowDegree
                                 : SketchVectorShaderFamily::NurbsGeneral;
      require(record.meta[0] >=
                      static_cast<std::uint32_t>(SketchVectorKind::Point) &&
                  record.meta[0] <=
                      static_cast<std::uint32_t>(SketchVectorKind::Text) &&
                  record.meta[3] != 0U && record.appearance[0] > 0.0F &&
                  record.appearance[1] > 0.0F,
              "vector record is not self-contained");
      require(chunk->shaderFamily() == expectedFamily,
              "vector chunk mixed analytic and NURBS records");
      containsLowDegreeNurbs =
          containsLowDegreeNurbs ||
          expectedFamily == SketchVectorShaderFamily::NurbsLowDegree;
      containsGeneralNurbs =
          containsGeneralNurbs ||
          expectedFamily == SketchVectorShaderFamily::NurbsGeneral;
    }
    records += chunk->records().size();
    dataRecords += chunk->data().size();
  }
  require(records == prepared.packet()->metrics().records &&
              dataRecords == prepared.packet()->metrics().dataRecords &&
              chunks.size() == prepared.packet()->metrics().chunks,
          "vector packet metrics are inconsistent");
  require(
      prepared.packet()->requiresShaderFamily(
          SketchVectorShaderFamily::NurbsLowDegree) == containsLowDegreeNurbs &&
          prepared.packet()->requiresShaderFamily(
              SketchVectorShaderFamily::NurbsGeneral) == containsGeneralNurbs,
      "vector packet declared the wrong native shader families");

  std::size_t indexed = 0U;
  for (const PackedSketchPrimitive &primitive : source.primitives()) {
    const SketchPrimitiveVectorEntry *entry =
        prepared.primitiveVectorIndex()->find(primitive.handle);
    const bool visible =
        hasFlag(primitive.flags, SketchPrimitiveFlags::Visible);
    require(visible == (entry != nullptr),
            "vector provenance disagrees with visibility");
    if (!entry)
      continue;
    ++indexed;
    std::size_t count = 0U;
    for (const SketchPrimitiveChunkSpan span :
         prepared.primitiveVectorIndex()->spans(primitive.handle)) {
      require(span.chunk < chunks.size() && span.recordCount != 0U &&
                  span.firstRecord <= chunks[span.chunk]->records().size() &&
                  span.recordCount <=
                      chunks[span.chunk]->records().size() - span.firstRecord,
              "vector provenance range is invalid");
      count += span.recordCount;
    }
    require(count == entry->recordCount,
            "vector provenance count is inconsistent");
  }
  require(indexed == prepared.primitiveVectorIndex()->entries().size(),
          "vector provenance has foreign entries");
}

std::shared_ptr<const SketchSceneSnapshot>
rationalSplineScene(std::uint32_t degree) {
  auto handle = SketchPrimitiveHandle::create(1U);
  require(handle.has_value(), "spline handle fixture failed");
  PackedSketchPrimitive primitive{id<SketchEntityId>(901U),
                                  *handle,
                                  0U,
                                  0U,
                                  SketchPrimitiveKind::BSpline,
                                  SketchPrimitiveFlags::Visible |
                                      SketchPrimitiveFlags::Selectable};
  primitive.spline = 0U;
  SketchPrimitiveBatch batch;
  batch.points = {};
  batch.primitives = {primitive};
  const std::uint32_t count = degree + 1U;
  batch.splineControlPointCoordinates.reserve(count * 2U);
  batch.splineWeights.reserve(count);
  for (std::uint32_t index = 0U; index < count; ++index) {
    const double amount = static_cast<double>(index) / degree;
    batch.splineControlPointCoordinates.push_back(
        std::lerp(-0.03, 0.03, amount));
    batch.splineControlPointCoordinates.push_back(
        std::sin(amount * std::numbers::pi * 2.0) * 0.025);
    batch.splineWeights.push_back(0.35 + static_cast<double>(index % 5U) * 0.4);
  }
  batch.splineKnots.insert(batch.splineKnots.end(), count, 0.0);
  batch.splineKnots.insert(batch.splineKnots.end(), count, 1.0);
  batch.splines = {{0U, count, 0U, 0U, degree, false}};
  auto created = SketchSceneSnapshot::create(stamp(9, 1, 9, 9, 9, 9), styles(),
                                             std::move(batch));
  require(created.has_value(), "rational spline fixture failed");
  return std::make_shared<const SketchSceneSnapshot>(std::move(*created));
}

void verifyNativeSplinePackets() {
  for (std::uint32_t degree = 1U; degree <= 25U; ++degree) {
    const auto source = rationalSplineScene(degree);
    const auto prepared = preparedScene(source);
    requirePacket(*source, *prepared);
    require(prepared->packet()->metrics().records == 1U,
            "single-span spline did not produce one native record");
    const auto &chunk = prepared->packet()->chunks().front();
    const SketchVectorRecord &record = chunk->records().front();
    const SketchVectorShaderFamily expected =
        degree <= 3U ? SketchVectorShaderFamily::NurbsLowDegree
                     : SketchVectorShaderFamily::NurbsGeneral;
    require(record.meta[0] ==
                    static_cast<std::uint32_t>(SketchVectorKind::BSpline) &&
                record.meta[2] == degree &&
                chunk->data().size() == (degree + 1U) * 4U &&
                chunk->shaderFamily() == expected &&
                prepared->packet()->requiresShaderFamily(expected),
            "spline record did not preserve degree, poles, weights, and knots");
    require(record.domain[1] == 0.0F && record.domain[2] == 1.0F,
            "spline record lost its native parameter domain");
  }
}

void verifyTransforms(const testkit::PropertyProfile &profile) {
  testkit::checkProperty(
      "vector view inverse", profile,
      [](testkit::Random &random, std::uint64_t index) {
        const SketchCamera2d view{
            index + 1U,
            {random.between(-1.0e6, 1.0e6), random.between(-1.0e6, 1.0e6)},
            std::pow(10.0, random.between(-9.0, 3.0)),
            random.between(-std::numbers::pi, std::numbers::pi)};
        auto transform = SketchViewTransform::create(
            view, {random.between(64.0, 7680.0), random.between(64.0, 4320.0)});
        require(transform.has_value(), "valid vector view was rejected");
        const Point2d point{
            view.centerMetres.x +
                random.between(-1000.0, 1000.0) * view.metresPerLogicalPixel,
            view.centerMetres.y +
                random.between(-1000.0, 1000.0) * view.metresPerLogicalPixel};
        const Point2d restored =
            transform->toCanonical(transform->toItem(point));
        const double tolerance =
            std::max(1.0e-12, view.metresPerLogicalPixel * 1.0e-7);
        require(std::abs(restored.x - point.x) <= tolerance &&
                    std::abs(restored.y - point.y) <= tolerance,
                "vector view transform is not reversible");
      });
}

void verifyGeneratedPackets(const testkit::PropertyProfile &profile) {
  testkit::PropertyProfile bounded = profile;
  bounded.iterations = std::min<std::uint64_t>(profile.iterations, 2'000U);
  testkit::checkProperty(
      "native vector packet", bounded,
      [](testkit::Random &random, std::uint64_t index) {
        const std::size_t count =
            1U + static_cast<std::size_t>(random.next() % 256U);
        const auto source =
            scene(count, random.next(),
                  stamp(20, index + 1U, 20, 20, 20, random.next()));
        SketchVectorUploadOptions options;
        options.maximumChunkBytes = std::max<std::size_t>(
            sizeof(SketchVectorRecord), 256U + random.next() % 16'384U);
        auto prepared = prepareSketchScene(source, {}, options);
        require(prepared.has_value(), "generated vector scene failed");
        requirePacket(*source, **prepared);

        auto transform =
            SketchViewTransform::create(camera(index + 1U), {1920.0, 1080.0});
        require(transform.has_value(), "generated view failed");
        auto visibility = ProgressiveSketchVisibility::create(
            (*prepared)->packet(), *transform);
        require(visibility.has_value(), "vector visibility failed");
        while (!visibility->complete()) {
          auto slice = visibility->takeNextSlice(7U, 5U);
          require(slice.has_value() && slice->chunks.size() <= 5U,
                  "vector visibility exceeded its slice bound");
        }
        auto upload = ProgressiveSketchUpload::create(
            (*prepared)->packet(), visibility->releaseSelectedChunks(), {});
        require(upload.has_value(), "vector upload plan failed");
        while (!upload->complete()) {
          auto slice = upload->takeNextSlice(8U * 1024U, 3U);
          require(slice.has_value() && slice->entries.size() <= 3U &&
                      slice->bytes <= 8U * 1024U,
                  "vector upload exceeded its slice bound");
        }
      });
}

void verifyPresenterUsesCameraIndependentPacket() {
  const auto source = scene(48U, 81U, stamp(30, 1, 30, 30, 30, 1));
  const auto prepared = preparedScene(source);
  const auto products = preparedProductPacket(prepared);
  SketchScenePresenter presenter;
  presenter.retarget(source->stamp().target);
  auto offered = presenter.publish(products);
  require(offered && offered->decision == PreparedSketchSceneDecision::Accepted,
          "presenter rejected vector products");
  auto first = presenter.synchronize({1280.0, 720.0});
  require(first && (*first)->packet() == prepared->packet(),
          "presenter replaced the prepared vector packet");
  auto changed = presenter.publishCamera(
      {2U, {0.01, -0.02}, 0.00008, std::numbers::pi / 5.0});
  require(changed && *changed == SketchCameraDecision::Accepted,
          "presenter rejected a newer camera");
  auto second = presenter.synchronize({1280.0, 720.0});
  require(second && (*second)->packet() == prepared->packet() &&
              (*second)->transform().camera().generation == 2U,
          "camera change rebuilt or replaced native vector geometry");
}

void verifyBoundsRejectInvalidInput() {
  auto invalidView = SketchViewTransform::create({}, {0.0, 1080.0});
  require(!invalidView &&
              invalidView.error().code == "desktop.sketch.invalid-vector-view",
          "invalid vector view was accepted");
  SketchVectorUploadOptions invalidOptions;
  invalidOptions.maximumChunkBytes = sizeof(SketchVectorRecord) - 1U;
  const auto source = scene(1U, 8U, stamp(40, 1, 40, 40, 40, 1));
  auto invalidPacket = buildSketchVectorPacket(*source, invalidOptions);
  require(!invalidPacket && invalidPacket.error().code ==
                                "desktop.sketch.invalid-vector-options",
          "invalid vector packet limits were accepted");
}

} // namespace

int main(int argc, char *argv[]) {
  try {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication application(argc, argv);
    static_assert(sizeof(SketchVectorRecord) == 128U);
    static_assert(alignof(SketchVectorRecord) == 16U);
    static_assert(sizeof(SketchVectorData) == 16U);
    SketchSceneItem item;
    require(item.flags().testFlag(QQuickItem::ItemHasContents),
            "sketch item does not own native scene-graph content");
    const auto profile = testkit::propertyProfile();
    verifyTransforms(profile);
    verifyGeneratedPackets(profile);
    verifyNativeSplinePackets();
    verifyPresenterUsesCameraIndependentPacket();
    verifyBoundsRejectInvalidInput();
    std::cout << "verified " << profile.iterations
              << " generated native-vector view cases\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
