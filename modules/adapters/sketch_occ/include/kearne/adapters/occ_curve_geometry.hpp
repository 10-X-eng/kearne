#pragma once

#include <kearne/sketch/modify.hpp>

#include <array>
#include <optional>
#include <variant>
#include <vector>

namespace kearne::adapters {

using CurveParameter =
    std::variant<sketch::DimensionlessValue, sketch::AngleValue,
                 sketch::LengthValue>;

struct CurveLocation {
  sketch::Point2 point;
  CurveParameter parameter;
  bool operator==(const CurveLocation &) const = default;
};

struct CurveProjection {
  CurveLocation location;
  sketch::LengthValue distance;
  bool operator==(const CurveProjection &) const = default;
};

struct CurveIntersection {
  sketch::Point2 point;
  CurveParameter firstParameter;
  CurveParameter secondParameter;
  bool operator==(const CurveIntersection &) const = default;
};

struct CurveIntersectionSet {
  std::vector<CurveIntersection> points;
  bool overlapping = false;
  bool operator==(const CurveIntersectionSet &) const = default;
};

struct TrimBoundary {
  sketch::Point2 point;
  SketchEntityId curve;
  bool operator==(const TrimBoundary &) const = default;
};

struct TrimPreview {
  std::vector<TrimBoundary> boundaries;
  bool deletesCurve = false;
  bool operator==(const TrimPreview &) const = default;
};

struct TrimIdentities {
  SketchEntityId splitEntity;
  std::array<SketchConstraintId, 2> boundaryConstraints;
};

struct TrimRequest {
  sketch::CurvePick curve;
  TrimIdentities identities;
  sketch::ExternalConstraintPolicy constraints =
      sketch::ExternalConstraintPolicy::Refuse;
};

struct SplitPreview {
  sketch::Point2 point;
  bool operator==(const SplitPreview &) const = default;
};

struct SplitIdentities {
  SketchEntityId secondSegment;
  SketchConstraintId seamConstraint;
};

struct SplitRequest {
  sketch::CurvePick curve;
  SplitIdentities identities;
  sketch::ExternalConstraintPolicy constraints =
      sketch::ExternalConstraintPolicy::Refuse;
};

struct JoinRequest {
  sketch::PointRef first;
  sketch::PointRef second;
  SketchObjectId object;
  sketch::ExternalConstraintPolicy constraints =
      sketch::ExternalConstraintPolicy::Refuse;
};

struct ConvertToNurbsRequest {
  SketchEntityId curve;
  sketch::ExternalConstraintPolicy constraints =
      sketch::ExternalConstraintPolicy::Refuse;
};

[[nodiscard]] Result<CurveProjection>
projectToCurve(const sketch::Entity &curve, sketch::Point2 query,
               double toleranceMetres = 1.0e-8);

[[nodiscard]] Result<CurveIntersectionSet>
intersectCurves(const sketch::Entity &first, const sketch::Entity &second,
                double toleranceMetres = 1.0e-8);

[[nodiscard]] Result<TrimPreview>
previewTrim(const sketch::Definition &current, sketch::CurvePick curve,
            const sketch::NumericalProfile &profile = {});

[[nodiscard]] Result<sketch::AppliedEdits>
trimCurve(const sketch::Definition &current, const TrimRequest &request,
          const sketch::NumericalProfile &profile = {});

[[nodiscard]] Result<SplitPreview>
previewSplit(const sketch::Definition &current, sketch::CurvePick curve,
             const sketch::NumericalProfile &profile = {});

[[nodiscard]] Result<sketch::AppliedEdits>
splitCurve(const sketch::Definition &current, const SplitRequest &request,
           const sketch::NumericalProfile &profile = {});

[[nodiscard]] Result<sketch::AppliedEdits>
joinCurves(const sketch::Definition &current, const JoinRequest &request,
           const sketch::NumericalProfile &profile = {});

[[nodiscard]] Result<sketch::AppliedEdits>
convertToNurbs(const sketch::Definition &current,
               const ConvertToNurbsRequest &request,
               const sketch::NumericalProfile &profile = {});

} // namespace kearne::adapters
