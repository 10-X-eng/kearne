#pragma once

#include "frontend_contract.hpp"
#include "local_sketch_session.hpp"

#include <span>
#include <string_view>
#include <vector>

namespace kearne::ui {

struct LocalSketchToolDefinition {
  LocalSketchToolKind kind;
  std::string_view commandId;
  std::string_view methodId;
  std::string_view label;
  std::string_view icon;
  std::size_t minimumInputPointCount;
  std::size_t maximumInputPointCount;
};

struct LocalSketchConstraintDefinition {
  LocalSketchConstraintKind kind;
  std::string_view commandId;
  std::string_view label;
  std::size_t minimumSelectionCount;
  std::size_t maximumSelectionCount = 0U;
};

[[nodiscard]] std::span<const LocalSketchToolDefinition>
localSketchToolDefinitions();
[[nodiscard]] const LocalSketchToolDefinition *
localSketchToolDefinition(LocalSketchToolKind kind);
[[nodiscard]] const LocalSketchToolDefinition *
localSketchToolDefinition(QStringView commandId, QStringView methodId = {});
[[nodiscard]] std::span<const LocalSketchConstraintDefinition>
localSketchConstraintDefinitions();
[[nodiscard]] const LocalSketchConstraintDefinition *
localSketchConstraintDefinition(LocalSketchConstraintKind kind);
[[nodiscard]] const LocalSketchConstraintDefinition *
localSketchConstraintDefinition(QStringView commandId);
[[nodiscard]] bool isLocalSketchPolygon(LocalSketchToolKind kind);
[[nodiscard]] bool isLocalSketchBSpline(LocalSketchToolKind kind);
[[nodiscard]] bool isLocalSketchPeriodicBSpline(LocalSketchToolKind kind);
[[nodiscard]] bool isLocalSketchInterpolatedBSpline(LocalSketchToolKind kind);
[[nodiscard]] std::size_t
localSketchPolygonSideCount(LocalSketchToolKind kind,
                            std::size_t requestedSideCount = 0U);

// Produces the same canonical SI geometry used for live preview and commit.
// Incomplete gestures return the useful construction available so far.
[[nodiscard]] Result<std::vector<SketchPrimitiveProjection>>
projectLocalSketchToolGesture(const LocalSketchToolGesture &gesture,
                              bool requireComplete = false);

} // namespace kearne::ui
