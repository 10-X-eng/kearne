#include "sketch_prepared_products.hpp"

#include "sketch_projection_support.hpp"
#include "sketch_stroke_mesh_build.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace kearne::ui {
namespace {

constexpr std::array<render::SketchStyle, 2> provisionalStyles{{
    {render::SketchStyleRole::Preview, render::SketchLinePattern::Solid, 1.5F,
     7.0F, 6U},
    {render::SketchStyleRole::Construction, render::SketchLinePattern::Dashed,
     1.0F, 6.0F, 7U},
}};

constexpr std::array<render::SketchStyle, 4> overlayPointStyles{{
    {render::SketchStyleRole::Hovered, render::SketchLinePattern::Solid, 1.5F,
     9.0F, 20U},
    {render::SketchStyleRole::Selected, render::SketchLinePattern::Solid, 1.5F,
     9.0F, 21U},
    {render::SketchStyleRole::Preview, render::SketchLinePattern::Solid, 1.5F,
     9.0F, 22U},
    {render::SketchStyleRole::Diagnostic, render::SketchLinePattern::Solid,
     1.5F, 9.0F, 23U},
}};

constexpr std::array<render::SketchStyle, 5> markerStyles{{
    {render::SketchStyleRole::Construction, render::SketchLinePattern::Solid,
     1.5F, 12.0F, 30U},
    {render::SketchStyleRole::Preview, render::SketchLinePattern::Solid, 1.5F,
     12.0F, 31U},
    {render::SketchStyleRole::Hovered, render::SketchLinePattern::Solid, 1.5F,
     12.0F, 32U},
    {render::SketchStyleRole::Regular, render::SketchLinePattern::Solid, 1.5F,
     12.0F, 33U},
    {render::SketchStyleRole::Preview, render::SketchLinePattern::Solid, 1.5F,
     12.0F, 34U},
}};

struct OverlayPointMeshContext {
  const PreparedSketchOverlay *overlay = nullptr;
  std::array<std::size_t, 5> offsets{};
};

[[nodiscard]] SketchStrokeSourcePrimitive
overlayPointPrimitiveAt(const void *context, std::size_t index) noexcept {
  const auto &source = *static_cast<const OverlayPointMeshContext *>(context);
  std::size_t role = 0U;
  while (role + 1U < source.offsets.size() &&
         index >= source.offsets[role + 1U])
    ++role;
  const auto &point = source.overlay->roleSets()[role]
                          ->pointInstances()[index - source.offsets[role]];
  return {static_cast<std::uint32_t>(index + 1U),
          static_cast<std::uint16_t>(role),
          SketchStrokeSourceKind::Point,
          true,
          point.positionMetres,
          point.positionMetres,
          0.0,
          0.0,
          0.0,
          0U};
}

[[nodiscard]] render::Point2d
markerPosition(const PreparedSketchMarkers &markers,
               std::size_t index) noexcept {
  const SketchMarkerRenderRecord &marker = markers.markers()[index];
  const auto anchors =
      markers.anchors().subspan(marker.firstAnchor, marker.anchorCount);
  render::Point2d position;
  for (const SketchMarkerAnchorPoint &anchor : anchors) {
    position.x += anchor.positionMetres.x;
    position.y += anchor.positionMetres.y;
  }
  const double count = static_cast<double>(anchors.size());
  position.x /= count;
  position.y /= count;
  return position;
}

[[nodiscard]] SketchStrokeSourcePrimitive
markerPrimitiveAt(const void *context, std::size_t index) noexcept {
  const auto &markers = *static_cast<const PreparedSketchMarkers *>(context);
  const SketchMarkerRenderRecord &marker = markers.markers()[index];
  const render::Point2d position = markerPosition(markers, index);
  return {marker.handle.value(),
          static_cast<std::uint16_t>(
              static_cast<std::uint8_t>(marker.category) - 1U),
          SketchStrokeSourceKind::Glyph,
          true,
          position,
          position,
          0.0,
          0.0,
          0.0,
          static_cast<std::uint16_t>(marker.kind)};
}

template <typename Position>
[[nodiscard]] SketchStrokeSourceBounds
decorationBounds(std::size_t count, Position position) noexcept {
  SketchStrokeSourceBounds bounds;
  for (std::size_t index = 0U; index < count; ++index) {
    const render::Point2d point = position(index);
    if (bounds.empty) {
      bounds.minimum = point;
      bounds.maximum = point;
      bounds.empty = false;
      continue;
    }
    bounds.minimum.x = std::min(bounds.minimum.x, point.x);
    bounds.minimum.y = std::min(bounds.minimum.y, point.y);
    bounds.maximum.x = std::max(bounds.maximum.x, point.x);
    bounds.maximum.y = std::max(bounds.maximum.y, point.y);
  }
  return bounds;
}

[[nodiscard]] Result<std::shared_ptr<const SketchSceneMesh>>
prepareDecorationMesh(const SketchStrokeMeshSource &source, SketchCurveLod lod,
                      SketchTessellationOptions tessellation,
                      SketchUploadOptions upload,
                      std::shared_ptr<const SketchSceneMesh> reuse,
                      std::stop_token cancellation) {
  if (source.primitiveCount == 0U)
    return std::shared_ptr<const SketchSceneMesh>{};
  auto built = SketchStrokeMeshBuildAccess::build(
      source, lod, tessellation, upload, std::move(reuse), cancellation);
  if (!built)
    return std::unexpected(std::move(built.error()));
  try {
    return std::make_shared<const SketchSceneMesh>(std::move(built->mesh));
  } catch (const std::bad_alloc &) {
    return std::unexpected(
        diagnostic("desktop.sketch.decoration-allocation",
                   "prepared sketch decoration allocation failed"));
  }
}

[[nodiscard]] Diagnostic cancelled() {
  return diagnostic("desktop.sketch.preparation-cancelled",
                    "sketch product preparation was cancelled");
}

[[nodiscard]] SketchStrokeSourceKind
sourceKind(render::SketchPrimitiveKind kind) noexcept {
  switch (kind) {
  case render::SketchPrimitiveKind::Point:
    return SketchStrokeSourceKind::Point;
  case render::SketchPrimitiveKind::Line:
    return SketchStrokeSourceKind::Line;
  case render::SketchPrimitiveKind::Circle:
    return SketchStrokeSourceKind::Circle;
  case render::SketchPrimitiveKind::Arc:
    return SketchStrokeSourceKind::Arc;
  case render::SketchPrimitiveKind::Ellipse:
    return SketchStrokeSourceKind::Ellipse;
  case render::SketchPrimitiveKind::EllipticalArc:
    return SketchStrokeSourceKind::EllipticalArc;
  case render::SketchPrimitiveKind::HyperbolicArc:
    return SketchStrokeSourceKind::HyperbolicArc;
  case render::SketchPrimitiveKind::ParabolicArc:
    return SketchStrokeSourceKind::ParabolicArc;
  case render::SketchPrimitiveKind::BSpline:
    return SketchStrokeSourceKind::BSpline;
  }
  return SketchStrokeSourceKind::Point;
}

[[nodiscard]] SketchStrokeSourcePrimitive
provisionalPrimitiveAt(const void *context, std::size_t index) noexcept {
  const auto &primitive =
      static_cast<const render::SketchProvisionalGeometry *>(context)
          ->primitives()[index];
  return {primitive.handle.value(),
          primitive.classification ==
                  render::SketchProvisionalClassification::Regular
              ? std::uint16_t{0U}
              : std::uint16_t{1U},
          sourceKind(primitive.kind),
          true,
          primitive.points[0],
          primitive.points[1],
          primitive.radius,
          primitive.startAngleRadians,
          primitive.sweepAngleRadians,
          0U,
          primitive.secondaryRadius,
          primitive.rotationAngleRadians};
}

[[nodiscard]] SketchStrokeSourceBounds
provisionalBounds(const render::SketchProvisionalGeometry &source) {
  SketchStrokeSourceBounds bounds;
  for (std::size_t index = 0U; index < source.primitives().size(); ++index) {
    const SketchStrokeSourcePrimitive primitive =
        provisionalPrimitiveAt(&source, index);
    auto current = sketchStrokePrimitiveBounds(primitive);
    if (!current)
      return {};
    if (bounds.empty) {
      bounds = *current;
      continue;
    }
    bounds.minimum.x = std::min(bounds.minimum.x, current->minimum.x);
    bounds.minimum.y = std::min(bounds.minimum.y, current->minimum.y);
    bounds.maximum.x = std::max(bounds.maximum.x, current->maximum.x);
    bounds.maximum.y = std::max(bounds.maximum.y, current->maximum.y);
  }
  return bounds;
}

[[nodiscard]] Result<std::size_t>
sumRetained(std::initializer_list<std::size_t> values) {
  std::size_t total = 0U;
  for (const std::size_t value : values)
    if (!detail::checkedSizeAdd(total, value, total))
      return std::unexpected(
          diagnostic("desktop.sketch.products-byte-overflow",
                     "prepared sketch product byte accounting overflowed"));
  return total;
}

} // namespace

PreparedSketchProvisional::PreparedSketchProvisional(
    std::shared_ptr<const render::SketchProvisionalGeometry> source,
    std::shared_ptr<const SketchSceneMesh> mesh,
    std::vector<SketchStrokePrimitiveSpanRecord> provenance, SketchCurveLod lod,
    PreparedSketchProvisionalMetrics metrics)
    : source_(std::move(source)), mesh_(std::move(mesh)),
      provenance_(std::move(provenance)), lod_(lod), metrics_(metrics) {}

Result<std::shared_ptr<const PreparedSketchProvisional>>
prepareSketchProvisional(
    std::shared_ptr<const render::SketchProvisionalGeometry> source,
    SketchCurveLod lod, SketchTessellationOptions tessellation,
    SketchUploadOptions upload,
    std::shared_ptr<const PreparedSketchProvisional> reuse,
    std::stop_token cancellation) {
  if (!source)
    return std::unexpected(
        diagnostic("desktop.sketch.null-provisional",
                   "cannot prepare null provisional sketch geometry"));
  if (cancellation.stop_requested())
    return std::unexpected(cancelled());
  if (reuse && reuse->source() == source && reuse->lod() == lod)
    return reuse;

  const SketchStrokeMeshSource input{
      provisionalStyles,           source.get(),
      source->primitives().size(), provisionalPrimitiveAt,
      provisionalBounds(*source),
  };
  auto built = SketchStrokeMeshBuildAccess::build(
      input, lod, tessellation, upload, reuse ? reuse->mesh() : nullptr,
      cancellation);
  if (!built)
    return std::unexpected(std::move(built.error()));
  if (cancellation.stop_requested())
    return std::unexpected(cancelled());

  std::size_t provenanceBytes = 0U;
  if (!detail::checkedSizeMultiply(built->provenance.capacity(),
                                   sizeof(SketchStrokePrimitiveSpanRecord),
                                   provenanceBytes))
    return std::unexpected(
        diagnostic("desktop.sketch.products-byte-overflow",
                   "prepared sketch product byte accounting overflowed"));
  auto retained =
      sumRetained({sizeof(PreparedSketchProvisional),
                   built->mesh.metrics().retainedMeshBytes, provenanceBytes});
  if (!retained)
    return std::unexpected(std::move(retained.error()));
  PreparedSketchProvisionalMetrics metrics{
      built->provenance.size(), *retained, built->scratchBytes,
      std::max(*retained, built->peakBytes)};
  try {
    return std::shared_ptr<const PreparedSketchProvisional>(
        new PreparedSketchProvisional{
            std::move(source),
            std::make_shared<const SketchSceneMesh>(std::move(built->mesh)),
            std::move(built->provenance), lod, metrics});
  } catch (const std::bad_alloc &) {
    return std::unexpected(
        diagnostic("desktop.sketch.provisional-allocation",
                   "prepared provisional sketch allocation failed"));
  } catch (const std::length_error &) {
    return std::unexpected(
        diagnostic("desktop.sketch.provisional-budget",
                   "prepared provisional sketch exceeded container capacity"));
  }
}

PreparedSketchProducts::PreparedSketchProducts(
    std::shared_ptr<const SketchSceneProducts> source,
    std::shared_ptr<const PreparedSketchScene> base,
    std::shared_ptr<const PreparedSketchOverlay> overlay,
    std::shared_ptr<const PreparedSketchProvisional> provisional,
    std::shared_ptr<const PreparedSketchMarkers> markers,
    std::shared_ptr<const SketchSceneMesh> overlayPointMesh,
    std::shared_ptr<const SketchSceneMesh> markerMesh,
    PreparedSketchProductsMetrics metrics)
    : source_(std::move(source)), base_(std::move(base)),
      overlay_(std::move(overlay)), provisional_(std::move(provisional)),
      markers_(std::move(markers)),
      overlayPointMesh_(std::move(overlayPointMesh)),
      markerMesh_(std::move(markerMesh)), metrics_(metrics) {}

Result<std::shared_ptr<const PreparedSketchProducts>>
PreparedSketchProducts::create(
    std::shared_ptr<const SketchSceneProducts> source,
    std::shared_ptr<const PreparedSketchScene> base,
    std::shared_ptr<const PreparedSketchOverlay> overlay,
    std::shared_ptr<const PreparedSketchProvisional> provisional,
    std::shared_ptr<const PreparedSketchMarkers> markers) {
  return createPrepared(std::move(source), std::move(base), std::move(overlay),
                        std::move(provisional), std::move(markers), {}, {});
}

Result<std::shared_ptr<const PreparedSketchProducts>>
PreparedSketchProducts::createPrepared(
    std::shared_ptr<const SketchSceneProducts> source,
    std::shared_ptr<const PreparedSketchScene> base,
    std::shared_ptr<const PreparedSketchOverlay> overlay,
    std::shared_ptr<const PreparedSketchProvisional> provisional,
    std::shared_ptr<const PreparedSketchMarkers> markers,
    std::shared_ptr<const SketchSceneMesh> overlayPointMesh,
    std::shared_ptr<const SketchSceneMesh> markerMesh) {
  if (!source || !base)
    return std::unexpected(diagnostic(
        "desktop.sketch.products-null-component",
        "prepared sketch products require source and base components"));
  if (auto valid = validateSketchSceneProducts(*source); !valid)
    return std::unexpected(std::move(valid.error()));
  if (base->scene() != source->scene)
    return std::unexpected(diagnostic(
        "desktop.sketch.products-prepared-base",
        "prepared sketch base does not retain the exact source scene"));
  if (static_cast<bool>(overlay) != static_cast<bool>(source->overlay) ||
      (overlay &&
       (overlay->source() != source->overlay || overlay->base() != base)))
    return std::unexpected(diagnostic(
        "desktop.sketch.products-prepared-overlay",
        "prepared sketch overlay does not match its product source"));
  if (static_cast<bool>(provisional) !=
          static_cast<bool>(source->provisional) ||
      (provisional && (provisional->source() != source->provisional ||
                       provisional->lod() != base->lod())))
    return std::unexpected(diagnostic(
        "desktop.sketch.products-prepared-provisional",
        "prepared provisional geometry does not match its product source"));
  if (static_cast<bool>(markers) != static_cast<bool>(source->markers) ||
      (markers &&
       (markers->source() != source->markers || markers->base() != base)))
    return std::unexpected(diagnostic(
        "desktop.sketch.products-prepared-markers",
        "prepared sketch markers do not match their product source"));
  const bool requiresOverlayPointMesh =
      overlay && overlay->metrics().pointInstanceCount != 0U;
  if (static_cast<bool>(overlayPointMesh) != requiresOverlayPointMesh ||
      (overlayPointMesh && overlayPointMesh->lod() != base->lod()))
    return std::unexpected(
        diagnostic("desktop.sketch.products-prepared-overlay-point-mesh",
                   "prepared sketch overlay point mesh does not match its "
                   "product source"));
  const bool requiresMarkerMesh =
      markers && markers->metrics().markerCount != 0U;
  if (static_cast<bool>(markerMesh) != requiresMarkerMesh ||
      (markerMesh && markerMesh->lod() != base->lod()))
    return std::unexpected(diagnostic(
        "desktop.sketch.products-prepared-marker-mesh",
        "prepared sketch marker mesh does not match its product source"));

  PreparedSketchProductsMetrics metrics;
  metrics.baseRetainedBytes = base->metrics().totalRetainedBytes;
  metrics.overlayRetainedBytes =
      overlay ? overlay->metrics().retainedBytes : 0U;
  metrics.provisionalRetainedBytes =
      provisional ? provisional->metrics().retainedBytes : 0U;
  metrics.markerRetainedBytes = markers ? markers->metrics().retainedBytes : 0U;
  metrics.overlayPointMeshRetainedBytes =
      overlayPointMesh ? overlayPointMesh->metrics().retainedMeshBytes : 0U;
  metrics.markerMeshRetainedBytes =
      markerMesh ? markerMesh->metrics().retainedMeshBytes : 0U;
  auto retained = sumRetained(
      {sizeof(PreparedSketchProducts), metrics.baseRetainedBytes,
       metrics.overlayRetainedBytes, metrics.overlayPointMeshRetainedBytes,
       metrics.provisionalRetainedBytes, metrics.markerRetainedBytes,
       metrics.markerMeshRetainedBytes});
  if (!retained)
    return std::unexpected(std::move(retained.error()));
  metrics.totalRetainedBytes = *retained;
  try {
    return std::shared_ptr<const PreparedSketchProducts>(
        new PreparedSketchProducts{
            std::move(source), std::move(base), std::move(overlay),
            std::move(provisional), std::move(markers),
            std::move(overlayPointMesh), std::move(markerMesh), metrics});
  } catch (const std::bad_alloc &) {
    return std::unexpected(
        diagnostic("desktop.sketch.products-allocation",
                   "prepared sketch product packet allocation failed"));
  }
}

Result<std::shared_ptr<const PreparedSketchProducts>>
prepareSketchProducts(std::shared_ptr<const SketchSceneProducts> source,
                      SketchCurveLod lod,
                      SketchProductPreparationOptions options,
                      std::shared_ptr<const PreparedSketchProducts> reuse,
                      std::stop_token cancellation) {
  if (!source)
    return std::unexpected(diagnostic("desktop.sketch.null-products",
                                      "cannot prepare null sketch products"));
  if (auto valid = validateSketchSceneProducts(*source); !valid)
    return std::unexpected(std::move(valid.error()));
  if (cancellation.stop_requested())
    return std::unexpected(cancelled());

  std::shared_ptr<const PreparedSketchScene> base;
  if (reuse && reuse->source()->scene == source->scene && reuse->lod() == lod) {
    base = reuse->base();
  } else {
    auto prepared = prepareSketchScene(
        source->scene, lod, options.tessellation, options.picking,
        options.upload, reuse ? reuse->base() : nullptr, cancellation);
    if (!prepared)
      return std::unexpected(std::move(prepared.error()));
    base = std::move(*prepared);
  }

  std::shared_ptr<const PreparedSketchOverlay> overlay;
  if (source->overlay) {
    if (reuse && reuse->source()->overlay == source->overlay &&
        reuse->overlay() && reuse->overlay()->base() == base) {
      overlay = reuse->overlay();
    } else {
      auto prepared = prepareSketchOverlay(
          source->overlay, base, options.overlay,
          reuse ? reuse->overlay() : nullptr, cancellation);
      if (!prepared)
        return std::unexpected(std::move(prepared.error()));
      overlay = std::move(*prepared);
    }
  }

  std::shared_ptr<const SketchSceneMesh> overlayPointMesh;
  if (overlay && overlay->metrics().pointInstanceCount != 0U) {
    const bool exactReuse = reuse && reuse->overlay() == overlay &&
                            reuse->lod() == lod && reuse->overlayPointMesh();
    if (exactReuse) {
      overlayPointMesh = reuse->overlayPointMesh();
    } else {
      OverlayPointMeshContext context;
      context.overlay = overlay.get();
      for (std::size_t role = 0U; role < overlay->roleSets().size(); ++role) {
        const std::size_t count =
            overlay->roleSets()[role]->pointInstances().size();
        if (!detail::checkedSizeAdd(context.offsets[role], count,
                                    context.offsets[role + 1U]) ||
            context.offsets[role + 1U] >
                std::numeric_limits<std::uint32_t>::max())
          return std::unexpected(
              diagnostic("desktop.sketch.overlay-point-mesh-count-overflow",
                         "prepared sketch overlay point count exceeds packed "
                         "identity capacity"));
      }
      const SketchStrokeMeshSource pointSource{
          overlayPointStyles,
          &context,
          context.offsets.back(),
          overlayPointPrimitiveAt,
          decorationBounds(
              context.offsets.back(),
              [&](std::size_t index) {
                return overlayPointPrimitiveAt(&context, index).first;
              }),
      };
      auto prepared = prepareDecorationMesh(
          pointSource, lod, options.tessellation, options.upload,
          reuse ? reuse->overlayPointMesh() : nullptr, cancellation);
      if (!prepared)
        return std::unexpected(std::move(prepared.error()));
      overlayPointMesh = std::move(*prepared);
    }
  }

  std::shared_ptr<const PreparedSketchProvisional> provisional;
  if (source->provisional) {
    auto prepared = prepareSketchProvisional(
        source->provisional, lod, options.tessellation, options.upload,
        reuse ? reuse->provisional() : nullptr, cancellation);
    if (!prepared)
      return std::unexpected(std::move(prepared.error()));
    provisional = std::move(*prepared);
  }

  std::shared_ptr<const PreparedSketchMarkers> markers;
  if (source->markers) {
    if (reuse && reuse->source()->markers == source->markers &&
        reuse->markers() && reuse->markers()->base() == base) {
      markers = reuse->markers();
    } else {
      auto prepared = prepareSketchMarkers(
          source->markers, base, options.markers,
          reuse ? reuse->markers() : nullptr, cancellation);
      if (!prepared)
        return std::unexpected(std::move(prepared.error()));
      markers = std::move(*prepared);
    }
  }

  std::shared_ptr<const SketchSceneMesh> markerMesh;
  if (markers && markers->metrics().markerCount != 0U) {
    const bool exactReuse = reuse && reuse->markers() == markers &&
                            reuse->lod() == lod && reuse->markerMesh();
    if (exactReuse) {
      markerMesh = reuse->markerMesh();
    } else {
      const SketchStrokeMeshSource markerSource{
          markerStyles,
          markers.get(),
          markers->markers().size(),
          markerPrimitiveAt,
          decorationBounds(markers->markers().size(),
                           [&](std::size_t index) {
                             return markerPosition(*markers, index);
                           }),
      };
      auto prepared = prepareDecorationMesh(
          markerSource, lod, options.tessellation, options.upload,
          reuse ? reuse->markerMesh() : nullptr, cancellation);
      if (!prepared)
        return std::unexpected(std::move(prepared.error()));
      markerMesh = std::move(*prepared);
    }
  }
  if (cancellation.stop_requested())
    return std::unexpected(cancelled());

  return PreparedSketchProducts::createPrepared(
      std::move(source), std::move(base), std::move(overlay),
      std::move(provisional), std::move(markers), std::move(overlayPointMesh),
      std::move(markerMesh));
}

} // namespace kearne::ui
