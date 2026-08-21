#include "sample_sketch_scene.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace kearne::ui {
namespace {

template <typename Id> Result<Id> fixtureId(std::uint64_t sequence) {
  typename Id::RandomTail tail{};
  for (std::size_t index = 0; index < tail.size(); ++index)
    tail[index] = static_cast<std::uint8_t>(sequence + index * 29U);
  return Id::create(1'700'000'000'000U + sequence, tail);
}

template <typename Digest> Result<Digest> fixtureDigest(std::uint8_t seed) {
  typename Digest::Bytes bytes{};
  for (std::size_t index = 0; index < bytes.size(); ++index)
    bytes[index] = static_cast<std::uint8_t>(seed + index * 17U);
  return Digest::fromBytes("sha256", bytes);
}

Result<sketch::Point2> point(double x, double y) {
  auto xLength = sketch::LengthValue::fromSi(x);
  auto yLength = sketch::LengthValue::fromSi(y);
  if (!xLength)
    return std::unexpected(std::move(xLength.error()));
  if (!yLength)
    return std::unexpected(std::move(yLength.error()));
  return sketch::Point2{*xLength, *yLength};
}

} // namespace

Result<std::shared_ptr<const render::SketchSceneSnapshot>>
makeSampleSketchScene() {
  auto session = render::RenderSessionHandle::create(1U);
  auto binding = fixtureId<ModelBindingId>(50U);
  auto revision = fixtureDigest<RevisionId>(51U);
  auto evaluation = fixtureDigest<EvaluationKey>(52U);
  auto generation = render::SceneGeneration::create(1U);
  auto sceneDigest = fixtureDigest<render::SceneDigest>(53U);
  if (!session)
    return std::unexpected(std::move(session.error()));
  if (!binding)
    return std::unexpected(std::move(binding.error()));
  if (!revision)
    return std::unexpected(std::move(revision.error()));
  if (!evaluation)
    return std::unexpected(std::move(evaluation.error()));
  if (!generation)
    return std::unexpected(std::move(generation.error()));
  if (!sceneDigest)
    return std::unexpected(std::move(sceneDigest.error()));
  return makeSampleSketchScene(
      render::SceneStamp{{*session, {*binding, *revision}, *evaluation},
                         *generation,
                         *sceneDigest});
}

Result<std::shared_ptr<const render::SketchSceneSnapshot>>
makeSampleSketchScene(render::SceneStamp stamp) {
  constexpr std::array edges{
      std::array{-0.05, -0.03, 0.05, -0.03},
      std::array{0.05, -0.03, 0.05, 0.03},
      std::array{0.05, 0.03, -0.05, 0.03},
      std::array{-0.05, 0.03, -0.05, -0.03},
  };
  constexpr std::array holes{
      std::array{-0.024, -0.018, 0.00325},
      std::array{0.024, -0.018, 0.00325},
      std::array{-0.024, 0.018, 0.00325},
      std::array{0.024, 0.018, 0.00325},
  };

  std::vector<sketch::Entity> geometry;
  geometry.reserve(edges.size() + holes.size());
  std::uint64_t sequence = 1U;
  for (const auto &edge : edges) {
    auto id = fixtureId<SketchEntityId>(sequence++);
    auto start = point(edge[0], edge[1]);
    auto end = point(edge[2], edge[3]);
    if (!id)
      return std::unexpected(std::move(id.error()));
    if (!start)
      return std::unexpected(std::move(start.error()));
    if (!end)
      return std::unexpected(std::move(end.error()));
    geometry.emplace_back(sketch::LineEntity{*id, *start, *end, false});
  }
  for (const auto &hole : holes) {
    auto id = fixtureId<SketchEntityId>(sequence++);
    auto center = point(hole[0], hole[1]);
    auto radius = sketch::LengthValue::fromSi(hole[2]);
    if (!id)
      return std::unexpected(std::move(id.error()));
    if (!center)
      return std::unexpected(std::move(center.error()));
    if (!radius)
      return std::unexpected(std::move(radius.error()));
    geometry.emplace_back(sketch::CircleEntity{*id, *center, *radius, false});
  }

  auto projected = render::projectSketchScene(std::move(stamp), geometry);
  if (!projected)
    return std::unexpected(std::move(projected.error()));
  return std::make_shared<const render::SketchSceneSnapshot>(
      std::move(*projected));
}

} // namespace kearne::ui
