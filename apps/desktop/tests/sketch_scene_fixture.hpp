#pragma once

#include "sketch_prepared_products.hpp"

#include <kearne/testkit/property.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <utility>
#include <vector>

namespace kearne::ui::test {

template <typename Id> Id id(std::uint64_t value) {
  typename Id::RandomTail random{};
  for (std::size_t index = 0; index < random.size(); ++index)
    random[index] = static_cast<std::uint8_t>(value >> ((index % 8U) * 8U));
  auto created = Id::create(value & ((std::uint64_t{1} << 48U) - 1U), random);
  if (!created)
    throw std::runtime_error("generated UUIDv7 was invalid");
  return std::move(*created);
}

template <typename Digest> Digest digest(std::uint64_t value) {
  typename Digest::Bytes bytes{};
  for (std::size_t index = 0; index < bytes.size(); ++index)
    bytes[index] = static_cast<std::uint8_t>(value >> ((index % 8U) * 8U));
  auto created = Digest::fromBytes("blake3-256", bytes);
  if (!created)
    throw std::runtime_error("generated digest was invalid");
  return std::move(*created);
}

inline render::SceneStamp stamp(std::uint64_t session, std::uint64_t generation,
                                std::uint64_t attachmentBinding,
                                std::uint64_t planeRevision,
                                std::uint64_t evaluation,
                                std::uint64_t sceneDigest) {
  auto sessionHandle = render::RenderSessionHandle::create(session);
  auto sceneGeneration = render::SceneGeneration::create(generation);
  if (!sessionHandle || !sceneGeneration)
    throw std::runtime_error("generated scene stamp was invalid");
  return {{*sessionHandle,
           {id<ModelBindingId>(attachmentBinding),
            digest<RevisionId>(planeRevision)},
           digest<render::EvaluationKey>(evaluation)},
          *sceneGeneration,
          digest<render::SceneDigest>(sceneDigest)};
}

inline SketchProductStamp productStamp(const render::SceneTarget &target,
                                       std::uint64_t generation,
                                       std::uint64_t payload) {
  auto version = SketchProductGeneration::create(generation);
  if (!version)
    throw std::runtime_error("generated product generation was invalid");
  return {target, *version, digest<SketchProductDigest>(payload)};
}

inline std::shared_ptr<const SketchSceneProducts>
productPacket(std::shared_ptr<const render::SketchSceneSnapshot> base,
              std::uint64_t generation, std::uint64_t payload) {
  return std::make_shared<const SketchSceneProducts>(SketchSceneProducts{
      productStamp(base->stamp().target, generation, payload),
      std::move(base),
      {},
      {},
      {}});
}

inline std::shared_ptr<const PreparedSketchProducts>
preparedProductPacket(std::shared_ptr<const PreparedSketchScene> base,
                      std::uint64_t generation = 0U,
                      std::uint64_t payload = 0U) {
  if (!base)
    throw std::runtime_error("cannot wrap a null prepared sketch scene");
  if (generation == 0U)
    generation = base->stamp().generation.value();
  if (payload == 0U)
    payload = generation;
  auto source = productPacket(base->scene(), generation, payload);
  auto prepared = PreparedSketchProducts::create(std::move(source), base);
  if (!prepared)
    throw std::runtime_error("generated prepared product packet was invalid");
  return std::move(*prepared);
}

inline std::vector<render::SketchStyle> styles() {
  return {
      {render::SketchStyleRole::Regular, render::SketchLinePattern::Solid, 1.5F,
       7.0F, 0},
      {render::SketchStyleRole::Construction, render::SketchLinePattern::Dashed,
       1.0F, 6.0F, 1},
      {render::SketchStyleRole::Regular, render::SketchLinePattern::Solid, 2.0F,
       8.0F, 4},
      {render::SketchStyleRole::Regular, render::SketchLinePattern::Dotted,
       1.5F, 7.0F, 3},
      {render::SketchStyleRole::Regular, render::SketchLinePattern::Solid, 2.5F,
       9.0F, 5},
  };
}

inline std::shared_ptr<const render::SketchSceneSnapshot>
scene(std::size_t count, std::uint64_t seed, render::SceneStamp sceneStamp) {
  testkit::Random random{seed};
  std::vector<render::Point2d> points;
  std::vector<render::PackedSketchPrimitive> primitives;
  points.reserve(count * 2U);
  primitives.reserve(count);
  const std::size_t columns = std::max<std::size_t>(
      1U, static_cast<std::size_t>(std::ceil(std::sqrt(count))));
  for (std::size_t index = 0; index < count; ++index) {
    const render::Point2d center{
        (static_cast<double>(index % columns) -
         static_cast<double>(columns) * 0.5) *
                0.012 +
            random.between(-0.001, 0.001),
        (static_cast<double>(index / columns) -
         static_cast<double>((count + columns - 1U) / columns) * 0.5) *
                0.012 +
            random.between(-0.001, 0.001)};
    auto handle = render::SketchPrimitiveHandle::create(
        static_cast<std::uint32_t>(index + 1U));
    if (!handle)
      throw std::runtime_error("generated primitive handle was invalid");
    render::PackedSketchPrimitive primitive{
        id<SketchEntityId>(seed + index + 1U),
        *handle,
        static_cast<std::uint32_t>(points.size()),
        static_cast<std::uint16_t>(index % 5U),
        static_cast<render::SketchPrimitiveKind>(index % 8U + 1U),
        render::SketchPrimitiveFlags::Visible |
            render::SketchPrimitiveFlags::Selectable,
        0.0,
        0.0,
        0.0,
    };
    switch (primitive.kind) {
    case render::SketchPrimitiveKind::Point:
      points.push_back(center);
      break;
    case render::SketchPrimitiveKind::Line:
      points.push_back({center.x - random.between(0.002, 0.005),
                        center.y - random.between(0.002, 0.005)});
      points.push_back({center.x + random.between(0.002, 0.005),
                        center.y + random.between(0.002, 0.005)});
      break;
    case render::SketchPrimitiveKind::Circle:
      points.push_back(center);
      primitive.radius = random.between(0.002, 0.005);
      break;
    case render::SketchPrimitiveKind::Arc:
      points.push_back(center);
      primitive.radius = random.between(0.002, 0.005);
      primitive.startAngleRadians =
          random.between(-std::numbers::pi, std::numbers::pi);
      primitive.sweepAngleRadians =
          random.between(0.2, 5.5) * (index % 2U == 0U ? 1.0 : -1.0);
      break;
    case render::SketchPrimitiveKind::Ellipse:
    case render::SketchPrimitiveKind::EllipticalArc:
      points.push_back(center);
      primitive.radius = random.between(0.004, 0.008);
      primitive.secondaryRadius = random.between(0.001, primitive.radius);
      primitive.rotationAngleRadians =
          random.between(-std::numbers::pi, std::numbers::pi);
      if (primitive.kind == render::SketchPrimitiveKind::EllipticalArc) {
        primitive.startAngleRadians =
            random.between(-std::numbers::pi, std::numbers::pi);
        primitive.sweepAngleRadians =
            random.between(0.2, 5.5) * (index % 2U == 0U ? 1.0 : -1.0);
      }
      break;
    case render::SketchPrimitiveKind::HyperbolicArc:
      points.push_back(center);
      primitive.radius = random.between(0.003, 0.006);
      primitive.secondaryRadius = random.between(0.002, 0.008);
      primitive.rotationAngleRadians =
          random.between(-std::numbers::pi, std::numbers::pi);
      primitive.startAngleRadians = random.between(-1.2, -0.2);
      primitive.sweepAngleRadians = random.between(0.4, 2.4);
      break;
    case render::SketchPrimitiveKind::ParabolicArc:
      points.push_back(center);
      primitive.radius = random.between(0.003, 0.006);
      primitive.rotationAngleRadians =
          random.between(-std::numbers::pi, std::numbers::pi);
      primitive.startAngleRadians = random.between(-0.008, -0.001);
      primitive.sweepAngleRadians = random.between(0.002, 0.016);
      break;
    case render::SketchPrimitiveKind::BSpline:
      throw std::runtime_error("generic desktop fixture selected B-spline");
    }
    primitives.push_back(primitive);
  }
  auto created = render::SketchSceneSnapshot::create(
      std::move(sceneStamp), styles(), std::move(points),
      std::move(primitives));
  if (!created)
    throw std::runtime_error(created.error().code);
  return std::make_shared<const render::SketchSceneSnapshot>(
      std::move(*created));
}

inline std::shared_ptr<const PreparedSketchScene>
preparedScene(std::shared_ptr<const render::SketchSceneSnapshot> generated,
              SketchCurveLod lod) {
  auto prepared = prepareSketchScene(std::move(generated), lod);
  if (!prepared)
    throw std::runtime_error(prepared.error().code);
  return std::move(*prepared);
}

inline SketchCamera2d camera(std::uint64_t generation) {
  if (generation == 1U)
    return {};
  return {generation,
          {static_cast<double>(generation % 17U) * 0.001,
           -static_cast<double>(generation % 13U) * 0.001},
          0.0002 + static_cast<double>(generation % 11U) * 0.00001,
          static_cast<double>(generation % 19U) * 0.03};
}

} // namespace kearne::ui::test
