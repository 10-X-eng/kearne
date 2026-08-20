#include <kearne/adapters/ceres_sketch_solver.hpp>

#include <Eigen/Dense>
#include <Eigen/Sparse>
#include <Eigen/SparseCholesky>
#include <Eigen/SparseQR>
#include <ceres/ceres.h>
#include <ceres/dynamic_numeric_diff_cost_function.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <numeric>
#include <ranges>
#include <span>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace kearne::adapters {
namespace {

namespace model = sketch;

enum class Kind { Point, Line, Circle, Arc };

struct WorkingEntity {
  model::Entity source;
  SketchEntityId id;
  Kind kind;
  std::vector<double> values;
};

using WorkingIndex = std::unordered_map<SketchEntityId, std::size_t,
                                        TypedIdHash<SketchEntityIdTag>>;

WorkingEntity workingEntity(const model::Entity &entity) {
  return std::visit(
      [&entity]<typename Value>(const Value &value) -> WorkingEntity {
        using Type = std::decay_t<Value>;
        if constexpr (std::is_same_v<Type, model::PointEntity>) {
          return {entity,
                  value.id,
                  Kind::Point,
                  {value.point.x.si(), value.point.y.si()}};
        } else if constexpr (std::is_same_v<Type, model::LineEntity>) {
          return {entity,
                  value.id,
                  Kind::Line,
                  {value.start.x.si(), value.start.y.si(), value.end.x.si(),
                   value.end.y.si()}};
        } else if constexpr (std::is_same_v<Type, model::CircleEntity>) {
          return {
              entity,
              value.id,
              Kind::Circle,
              {value.center.x.si(), value.center.y.si(), value.radius.si()}};
        } else {
          return {entity,
                  value.id,
                  Kind::Arc,
                  {value.center.x.si(), value.center.y.si(), value.radius.si(),
                   value.startAngle.si(), value.endAngle.si()}};
        }
      },
      entity);
}

WorkingIndex indexOf(const std::vector<WorkingEntity> &entities) {
  WorkingIndex result;
  result.reserve(entities.size());
  for (std::size_t index = 0; index < entities.size(); ++index)
    result.emplace(entities[index].id, index);
  return result;
}

Result<void> applyPrior(std::vector<WorkingEntity> &entities,
                        const std::vector<model::Entity> &prior,
                        const model::NumericalProfile &profile) {
  const WorkingIndex index = indexOf(entities);
  std::unordered_set<SketchEntityId, TypedIdHash<SketchEntityIdTag>> seen;
  seen.reserve(prior.size());
  for (const model::Entity &candidate : prior) {
    WorkingEntity seed = workingEntity(candidate);
    if (!seen.insert(seed.id).second)
      return std::unexpected(diagnostic("sketch.seed.duplicate-id",
                                        "prior solution has a duplicate ID"));
    const auto found = index.find(seed.id);
    if (found == index.end())
      continue;
    WorkingEntity &target = entities[found->second];
    if (seed.kind != target.kind || seed.values.size() != target.values.size())
      return std::unexpected(diagnostic("sketch.seed.entity-kind-mismatch",
                                        "prior solution entity kind changed"));
    if (!std::ranges::all_of(seed.values,
                             [](double value) { return std::isfinite(value); }))
      return std::unexpected(
          diagnostic("sketch.seed.non-finite", "prior solution is not finite"));
    const std::size_t coordinateCount = seed.kind == Kind::Line ? 4U : 2U;
    for (std::size_t offset = 0; offset < coordinateCount; ++offset) {
      if (std::abs(seed.values[offset]) > profile.maximumCoordinateMeters)
        return std::unexpected(diagnostic("sketch.seed.coordinate-range",
                                          "prior solution is out of range"));
    }
    if ((seed.kind == Kind::Circle || seed.kind == Kind::Arc) &&
        (seed.values[2] < profile.minimumLengthMeters ||
         seed.values[2] > profile.maximumCoordinateMeters))
      return std::unexpected(diagnostic("sketch.seed.invalid-radius",
                                        "prior solution radius is invalid"));
    if (seed.kind == Kind::Line && std::hypot(seed.values[2] - seed.values[0],
                                              seed.values[3] - seed.values[1]) <
                                       profile.minimumLengthMeters)
      return std::unexpected(diagnostic("sketch.seed.degenerate-line",
                                        "prior solution line is degenerate"));
    if (seed.kind == Kind::Arc && std::abs(seed.values[4] - seed.values[3]) <
                                      profile.angleToleranceRadians)
      return std::unexpected(diagnostic("sketch.seed.degenerate-arc",
                                        "prior solution arc is degenerate"));
    target.values = std::move(seed.values);
  }
  return {};
}

template <typename Dimension>
Result<Quantity<Dimension>> quantity(double value) {
  return Quantity<Dimension>::fromSi(value);
}

Result<model::Point2> point(double x, double y) {
  auto xValue = quantity<Length>(x);
  auto yValue = quantity<Length>(y);
  if (!xValue || !yValue)
    return std::unexpected(
        diagnostic("sketch.solution.non-finite", "solved point is not finite"));
  return model::Point2{*xValue, *yValue};
}

Result<model::Entity> rebuild(const WorkingEntity &entity) {
  return std::visit(
      [&entity]<typename Value>(const Value &source) -> Result<model::Entity> {
        using Type = std::decay_t<Value>;
        if constexpr (std::is_same_v<Type, model::PointEntity>) {
          auto solved = point(entity.values[0], entity.values[1]);
          if (!solved)
            return std::unexpected(std::move(solved.error()));
          return model::Entity{
              model::PointEntity{source.id, *solved, source.construction}};
        } else if constexpr (std::is_same_v<Type, model::LineEntity>) {
          auto start = point(entity.values[0], entity.values[1]);
          auto end = point(entity.values[2], entity.values[3]);
          if (!start || !end)
            return std::unexpected(start ? std::move(end.error())
                                         : std::move(start.error()));
          return model::Entity{
              model::LineEntity{source.id, *start, *end, source.construction}};
        } else if constexpr (std::is_same_v<Type, model::CircleEntity>) {
          auto center = point(entity.values[0], entity.values[1]);
          auto radius = quantity<Length>(entity.values[2]);
          if (!center || !radius)
            return std::unexpected(center ? std::move(radius.error())
                                          : std::move(center.error()));
          return model::Entity{model::CircleEntity{source.id, *center, *radius,
                                                   source.construction}};
        } else {
          auto center = point(entity.values[0], entity.values[1]);
          auto radius = quantity<Length>(entity.values[2]);
          auto start = quantity<Angle>(entity.values[3]);
          auto end = quantity<Angle>(entity.values[4]);
          if (!center || !radius || !start || !end)
            return std::unexpected(diagnostic("sketch.solution.non-finite",
                                              "solved arc is not finite"));
          return model::Entity{model::ArcEntity{
              source.id, *center, *radius, *start, *end, source.construction}};
        }
      },
      entity.source);
}

struct PlainPoint {
  double x;
  double y;
};

bool isLine(Kind kind) { return kind == Kind::Line; }
bool isRadial(Kind kind) { return kind == Kind::Circle || kind == Kind::Arc; }

bool validPointKey(Kind kind, model::PointKey key) {
  if (kind == Kind::Point)
    return key == model::PointKey::Point;
  if (kind == Kind::Line)
    return key == model::PointKey::Start || key == model::PointKey::End;
  return key == model::PointKey::Center;
}

class ConstraintCost final {
public:
  ConstraintCost(model::Constraint constraint,
                 std::vector<SketchEntityId> entities, std::vector<Kind> kinds,
                 std::vector<std::vector<double>> anchors,
                 const model::NumericalProfile &profile)
      : constraint_(std::move(constraint)), entities_(std::move(entities)),
        kinds_(std::move(kinds)), anchors_(std::move(anchors)),
        scale_(profile.typicalLengthMeters),
        minimum_(profile.minimumLengthMeters) {}

  bool operator()(double const *const *parameters, double *residuals) const {
    const auto block = [&](SketchEntityId id) {
      const auto found = std::ranges::find(entities_, id);
      const std::size_t index =
          static_cast<std::size_t>(found - entities_.begin());
      return std::pair{kinds_[index], parameters[index]};
    };
    const auto selectedPoint = [&](const model::PointRef &reference) {
      const auto [kind, values] = block(reference.entity);
      if (kind == Kind::Point && reference.key == model::PointKey::Point)
        return PlainPoint{values[0], values[1]};
      if (kind == Kind::Line && reference.key == model::PointKey::Start)
        return PlainPoint{values[0], values[1]};
      if (kind == Kind::Line && reference.key == model::PointKey::End)
        return PlainPoint{values[2], values[3]};
      return PlainPoint{values[0], values[1]};
    };
    const auto lineLength = [&](const double *line) {
      return std::max(std::hypot(line[2] - line[0], line[3] - line[1]),
                      minimum_);
    };
    const auto cross = [](double ax, double ay, double bx, double by) {
      return ax * by - ay * bx;
    };
    const auto dot = [](double ax, double ay, double bx, double by) {
      return ax * bx + ay * by;
    };

    std::size_t count = 0;
    std::visit(
        [&]<typename Value>(const Value &value) {
          using Type = std::decay_t<Value>;
          if constexpr (std::is_same_v<Type, model::Coincident> ||
                        std::is_same_v<Type, model::Distance> ||
                        std::is_same_v<Type, model::HorizontalDistance> ||
                        std::is_same_v<Type, model::VerticalDistance>) {
            const PlainPoint first = selectedPoint(value.first);
            const PlainPoint second = selectedPoint(value.second);
            const double deltaX = second.x - first.x;
            const double deltaY = second.y - first.y;
            if constexpr (std::is_same_v<Type, model::Coincident>) {
              residuals[count++] = deltaX / scale_;
              residuals[count++] = deltaY / scale_;
            }
            if constexpr (std::is_same_v<Type, model::Distance>)
              residuals[count++] =
                  (std::hypot(deltaX, deltaY) - value.value.si()) / scale_;
            if constexpr (std::is_same_v<Type, model::HorizontalDistance>)
              residuals[count++] = (deltaX - value.value.si()) / scale_;
            if constexpr (std::is_same_v<Type, model::VerticalDistance>)
              residuals[count++] = (deltaY - value.value.si()) / scale_;
          }
          if constexpr (std::is_same_v<Type, model::Horizontal> ||
                        std::is_same_v<Type, model::Vertical>) {
            const auto [kind, line] = block(value.line);
            static_cast<void>(kind);
            if constexpr (std::is_same_v<Type, model::Horizontal>)
              residuals[count++] = (line[3] - line[1]) / scale_;
            else
              residuals[count++] = (line[2] - line[0]) / scale_;
          }
          if constexpr (std::is_same_v<Type, model::Parallel> ||
                        std::is_same_v<Type, model::Perpendicular> ||
                        std::is_same_v<Type, model::Collinear> ||
                        std::is_same_v<Type, model::AngleBetween>) {
            const auto [firstKind, first] = block(value.first);
            const auto [secondKind, second] = block(value.second);
            static_cast<void>(firstKind);
            static_cast<void>(secondKind);
            const double firstX = first[2] - first[0];
            const double firstY = first[3] - first[1];
            const double secondX = second[2] - second[0];
            const double secondY = second[3] - second[1];
            const double denominator = lineLength(first) * lineLength(second);
            const double normalizedCross =
                cross(firstX, firstY, secondX, secondY) / denominator;
            const double normalizedDot =
                dot(firstX, firstY, secondX, secondY) / denominator;
            if constexpr (std::is_same_v<Type, model::Parallel>)
              residuals[count++] = normalizedCross;
            if constexpr (std::is_same_v<Type, model::Perpendicular>)
              residuals[count++] = normalizedDot;
            if constexpr (std::is_same_v<Type, model::AngleBetween>) {
              residuals[count++] = normalizedCross - std::sin(value.value.si());
              residuals[count++] = normalizedDot - std::cos(value.value.si());
            }
            if constexpr (std::is_same_v<Type, model::Collinear>) {
              residuals[count++] = normalizedCross;
              residuals[count++] = cross(firstX, firstY, second[0] - first[0],
                                         second[1] - first[1]) /
                                   (lineLength(first) * scale_);
            }
          }
          if constexpr (std::is_same_v<Type, model::Tangent>) {
            auto [firstKind, first] = block(value.first);
            auto [secondKind, second] = block(value.second);
            if (isRadial(firstKind) && isLine(secondKind)) {
              std::swap(firstKind, secondKind);
              std::swap(first, second);
            }
            if (isLine(firstKind)) {
              const double lineX = first[2] - first[0];
              const double lineY = first[3] - first[1];
              const double separation =
                  std::abs(cross(lineX, lineY, second[0] - first[0],
                                 second[1] - first[1])) /
                  lineLength(first);
              residuals[count++] = (separation - second[2]) / scale_;
            } else {
              const double separation =
                  std::hypot(second[0] - first[0], second[1] - first[1]);
              const double target = value.mode == model::Tangency::External
                                        ? first[2] + second[2]
                                        : std::abs(first[2] - second[2]);
              residuals[count++] = (separation - target) / scale_;
            }
          }
          if constexpr (std::is_same_v<Type, model::Concentric>) {
            const auto [firstKind, first] = block(value.first);
            const auto [secondKind, second] = block(value.second);
            static_cast<void>(firstKind);
            static_cast<void>(secondKind);
            residuals[count++] = (second[0] - first[0]) / scale_;
            residuals[count++] = (second[1] - first[1]) / scale_;
          }
          if constexpr (std::is_same_v<Type, model::Equal>) {
            const auto [firstKind, first] = block(value.first);
            const auto [secondKind, second] = block(value.second);
            static_cast<void>(secondKind);
            residuals[count++] =
                isLine(firstKind)
                    ? (lineLength(second) - lineLength(first)) / scale_
                    : (second[2] - first[2]) / scale_;
          }
          if constexpr (std::is_same_v<Type, model::Midpoint>) {
            const PlainPoint selected = selectedPoint(value.point);
            const auto [kind, line] = block(value.line);
            static_cast<void>(kind);
            residuals[count++] =
                (selected.x - (line[0] + line[2]) * 0.5) / scale_;
            residuals[count++] =
                (selected.y - (line[1] + line[3]) * 0.5) / scale_;
          }
          if constexpr (std::is_same_v<Type, model::Fixed>) {
            const auto [kind, selected] = block(value.entity);
            static_cast<void>(kind);
            const std::vector<double> &anchor = anchors_.front();
            for (std::size_t index = 0; index < anchor.size(); ++index) {
              const bool angular = kinds_.front() == Kind::Arc && index >= 3;
              residuals[count++] =
                  (selected[index] - anchor[index]) / (angular ? 1.0 : scale_);
            }
          }
          if constexpr (std::is_same_v<Type, model::Radius> ||
                        std::is_same_v<Type, model::Diameter>) {
            const auto [kind, curve] = block(value.curve);
            static_cast<void>(kind);
            const double measured = std::is_same_v<Type, model::Diameter>
                                        ? curve[2] * 2.0
                                        : curve[2];
            residuals[count++] = (measured - value.value.si()) / scale_;
          }
        },
        constraint_);
    return std::ranges::all_of(std::span{residuals, count}, [](double value) {
      return std::isfinite(value);
    });
  }

private:
  model::Constraint constraint_;
  std::vector<SketchEntityId> entities_;
  std::vector<Kind> kinds_;
  std::vector<std::vector<double>> anchors_;
  double scale_;
  double minimum_;
};

std::vector<SketchEntityId> referencedEntities(const model::Constraint &value) {
  std::vector<SketchEntityId> result;
  const auto add = [&result](SketchEntityId id) {
    if (std::ranges::find(result, id) == result.end())
      result.push_back(id);
  };
  std::visit(
      [&]<typename Constraint>(const Constraint &constraint) {
        using Type = std::decay_t<Constraint>;
        if constexpr (std::is_same_v<Type, model::Coincident> ||
                      std::is_same_v<Type, model::Distance> ||
                      std::is_same_v<Type, model::HorizontalDistance> ||
                      std::is_same_v<Type, model::VerticalDistance>) {
          add(constraint.first.entity);
          add(constraint.second.entity);
        } else if constexpr (std::is_same_v<Type, model::Horizontal> ||
                             std::is_same_v<Type, model::Vertical>) {
          add(constraint.line);
        } else if constexpr (std::is_same_v<Type, model::Midpoint>) {
          add(constraint.point.entity);
          add(constraint.line);
        } else if constexpr (std::is_same_v<Type, model::Fixed>) {
          add(constraint.entity);
        } else if constexpr (std::is_same_v<Type, model::Radius> ||
                             std::is_same_v<Type, model::Diameter>) {
          add(constraint.curve);
        } else {
          add(constraint.first);
          add(constraint.second);
        }
      },
      value);
  return result;
}

std::size_t residualCount(const model::Constraint &constraint,
                          const std::vector<WorkingEntity> &entities,
                          const WorkingIndex &index) {
  return std::visit(
      [&]<typename Value>(const Value &value) -> std::size_t {
        using Type = std::decay_t<Value>;
        if constexpr (std::is_same_v<Type, model::Coincident> ||
                      std::is_same_v<Type, model::Concentric> ||
                      std::is_same_v<Type, model::Midpoint> ||
                      std::is_same_v<Type, model::Collinear> ||
                      std::is_same_v<Type, model::AngleBetween>)
          return 2;
        if constexpr (std::is_same_v<Type, model::Fixed>)
          return entities[index.at(value.entity)].values.size();
        return 1;
      },
      constraint);
}

class DragCost final {
public:
  DragCost(Kind kind, model::PointKey key, model::Point2 target, double scale)
      : kind_(kind), key_(key), target_{target.x.si(), target.y.si()},
        scale_(scale) {}

  bool operator()(double const *const *parameters, double *residuals) const {
    const double *values = parameters[0];
    PlainPoint selected{values[0], values[1]};
    if (kind_ == Kind::Line && key_ == model::PointKey::End)
      selected = {values[2], values[3]};
    residuals[0] = (selected.x - target_.x) / scale_;
    residuals[1] = (selected.y - target_.y) / scale_;
    return std::isfinite(residuals[0]) && std::isfinite(residuals[1]);
  }

private:
  Kind kind_;
  model::PointKey key_;
  PlainPoint target_;
  double scale_;
};

class CancellationCallback final : public ceres::IterationCallback {
public:
  explicit CancellationCallback(CancellationToken token) : token_(token) {}
  ceres::CallbackReturnType
  operator()(const ceres::IterationSummary &) override {
    return token_.stop_requested() ? ceres::SOLVER_ABORT
                                   : ceres::SOLVER_CONTINUE;
  }

private:
  CancellationToken token_;
};

struct JacobianAnalysis {
  std::size_t rank = 0;
  std::vector<model::FreedomMode> modes;
  std::vector<std::size_t> pivotRows;
};

Result<JacobianAnalysis>
analyzeJacobian(ceres::Problem &problem,
                const std::vector<ceres::ResidualBlockId> &residualBlocks,
                const std::vector<WorkingEntity> &entities,
                const model::NumericalProfile &profile, bool computeModes,
                bool computeRedundancy) {
  const std::size_t variableCount =
      std::accumulate(entities.begin(), entities.end(), std::size_t{0},
                      [](std::size_t total, const WorkingEntity &entity) {
                        return total + entity.values.size();
                      });
  JacobianAnalysis result;
  if (residualBlocks.empty()) {
    if (computeModes && variableCount <= profile.maximumModeVariables) {
      for (const WorkingEntity &entity : entities) {
        for (std::size_t parameter = 0; parameter < entity.values.size();
             ++parameter) {
          model::FreedomMode mode;
          mode.components.push_back(
              {entity.id, std::vector<double>(entity.values.size(), 0.0)});
          mode.components.back().parameterDirection[parameter] = 1.0;
          result.modes.push_back(std::move(mode));
        }
      }
    }
    return result;
  }

  ceres::Problem::EvaluateOptions options;
  options.apply_loss_function = false;
  options.residual_blocks = residualBlocks;
  options.parameter_blocks.reserve(entities.size());
  for (const WorkingEntity &entity : entities)
    options.parameter_blocks.push_back(
        const_cast<double *>(entity.values.data()));
  ceres::CRSMatrix crs;
  if (!problem.Evaluate(options, nullptr, nullptr, nullptr, &crs))
    return std::unexpected(
        diagnostic("sketch.solver.jacobian-failed",
                   "solver could not evaluate the Jacobian"));

  std::optional<Eigen::SparseMatrix<double>> sparseMatrix;
  const auto makeSparseMatrix = [&]() -> Eigen::SparseMatrix<double> & {
    if (!sparseMatrix) {
      std::vector<Eigen::Triplet<double>> entries;
      entries.reserve(crs.values.size());
      for (int row = 0; row < crs.num_rows; ++row) {
        for (int offset = crs.rows[static_cast<std::size_t>(row)];
             offset < crs.rows[static_cast<std::size_t>(row + 1)]; ++offset)
          entries.emplace_back(row, crs.cols[static_cast<std::size_t>(offset)],
                               crs.values[static_cast<std::size_t>(offset)]);
      }
      sparseMatrix.emplace(crs.num_rows, crs.num_cols);
      sparseMatrix->setFromTriplets(entries.begin(), entries.end());
    }
    return *sparseMatrix;
  };

  if (!computeRedundancy && variableCount > 64U &&
      crs.num_rows >= crs.num_cols) {
    Eigen::SparseMatrix<double> normal =
        makeSparseMatrix().transpose() * makeSparseMatrix();
    Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> factor;
    factor.compute(normal);
    if (factor.info() == Eigen::Success && factor.vectorD().size() > 0) {
      const double maximum = factor.vectorD().maxCoeff();
      const double relative = std::max(profile.rankRelativeTolerance *
                                           profile.rankRelativeTolerance,
                                       std::numeric_limits<double>::epsilon() *
                                           static_cast<double>(variableCount));
      if (maximum > 0.0 &&
          (factor.vectorD().array() > maximum * relative).all()) {
        result.rank = variableCount;
        return result;
      }
    }
  }

  if (variableCount <= profile.maximumModeVariables) {
    Eigen::MatrixXd matrix = Eigen::MatrixXd::Zero(crs.num_rows, crs.num_cols);
    for (int row = 0; row < crs.num_rows; ++row) {
      for (int offset = crs.rows[static_cast<std::size_t>(row)];
           offset < crs.rows[static_cast<std::size_t>(row + 1)]; ++offset)
        matrix(row, crs.cols[static_cast<std::size_t>(offset)]) =
            crs.values[static_cast<std::size_t>(offset)];
    }
    Eigen::JacobiSVD<Eigen::MatrixXd> svd(matrix);
    svd.setThreshold(profile.rankRelativeTolerance);
    result.rank = static_cast<std::size_t>(svd.rank());

    if (computeRedundancy) {
      Eigen::ColPivHouseholderQR<Eigen::MatrixXd> rowQr(matrix.transpose());
      rowQr.setThreshold(profile.rankRelativeTolerance);
      const auto pivots = rowQr.colsPermutation().indices();
      for (Eigen::Index index = 0; index < rowQr.rank(); ++index)
        result.pivotRows.push_back(static_cast<std::size_t>(pivots[index]));
    }

    if (computeModes && result.rank < variableCount) {
      Eigen::JacobiSVD<Eigen::MatrixXd> modeSvd(matrix, Eigen::ComputeFullV);
      modeSvd.setThreshold(profile.rankRelativeTolerance);
      result.rank = static_cast<std::size_t>(modeSvd.rank());
      std::vector<std::size_t> entityOffsets;
      entityOffsets.reserve(entities.size());
      std::size_t offset = 0;
      for (const WorkingEntity &entity : entities) {
        entityOffsets.push_back(offset);
        offset += entity.values.size();
      }
      for (Eigen::Index column = static_cast<Eigen::Index>(result.rank);
           column < modeSvd.matrixV().cols(); ++column) {
        model::FreedomMode mode;
        for (std::size_t entityIndex = 0; entityIndex < entities.size();
             ++entityIndex) {
          std::vector<double> direction(entities[entityIndex].values.size());
          double maximum = 0.0;
          for (std::size_t parameter = 0; parameter < direction.size();
               ++parameter) {
            direction[parameter] =
                modeSvd.matrixV()(static_cast<Eigen::Index>(
                                      entityOffsets[entityIndex] + parameter),
                                  column);
            maximum = std::max(maximum, std::abs(direction[parameter]));
          }
          if (maximum > profile.rankRelativeTolerance)
            mode.components.push_back(
                {entities[entityIndex].id, std::move(direction)});
        }
        result.modes.push_back(std::move(mode));
      }
    }
    return result;
  }

  Eigen::SparseMatrix<double> &matrix = makeSparseMatrix();
  Eigen::SparseQR<Eigen::SparseMatrix<double>, Eigen::COLAMDOrdering<int>> qr;
  qr.setPivotThreshold(profile.rankRelativeTolerance);
  qr.compute(matrix);
  if (qr.info() != Eigen::Success)
    return std::unexpected(diagnostic("sketch.solver.rank-failed",
                                      "solver could not rank the Jacobian"));
  result.rank = static_cast<std::size_t>(qr.rank());

  if (computeRedundancy) {
    Eigen::SparseMatrix<double> transpose = matrix.transpose();
    Eigen::SparseQR<Eigen::SparseMatrix<double>, Eigen::COLAMDOrdering<int>>
        rowQr;
    rowQr.setPivotThreshold(profile.rankRelativeTolerance);
    rowQr.compute(transpose);
    if (rowQr.info() == Eigen::Success) {
      const auto pivots = rowQr.colsPermutation().indices();
      for (Eigen::Index index = 0; index < rowQr.rank(); ++index)
        result.pivotRows.push_back(static_cast<std::size_t>(pivots[index]));
    }
  }
  return result;
}

struct SolveAttempt {
  model::SolveResult result;
  bool documentSatisfied = false;
};

Result<SolveAttempt> solveOnce(const model::SolveInput &input,
                               bool includeDrag) {
  std::vector<WorkingEntity> entities;
  entities.reserve(input.definition.entities.size());
  for (const model::Entity &entity : input.definition.entities)
    entities.push_back(workingEntity(entity));
  if (auto prior = applyPrior(entities, input.priorSolution, input.numerical);
      !prior)
    return std::unexpected(std::move(prior.error()));
  const WorkingIndex index = indexOf(entities);

  ceres::Problem problem;
  for (WorkingEntity &entity : entities) {
    const int size = static_cast<int>(entity.values.size());
    problem.AddParameterBlock(entity.values.data(), size);
    const std::size_t coordinateCount = entity.kind == Kind::Line ? 4U : 2U;
    for (std::size_t offset = 0; offset < coordinateCount; ++offset) {
      problem.SetParameterLowerBound(entity.values.data(),
                                     static_cast<int>(offset),
                                     -input.numerical.maximumCoordinateMeters);
      problem.SetParameterUpperBound(entity.values.data(),
                                     static_cast<int>(offset),
                                     input.numerical.maximumCoordinateMeters);
    }
    if (isRadial(entity.kind)) {
      problem.SetParameterLowerBound(entity.values.data(), 2,
                                     input.numerical.minimumLengthMeters);
      problem.SetParameterUpperBound(entity.values.data(), 2,
                                     input.numerical.maximumCoordinateMeters);
    }
  }

  std::vector<ceres::ResidualBlockId> documentBlocks;
  std::vector<std::size_t> rowCounts;
  documentBlocks.reserve(input.definition.constraints.size());
  rowCounts.reserve(input.definition.constraints.size());
  for (const model::Constraint &constraint : input.definition.constraints) {
    const std::vector<SketchEntityId> references =
        referencedEntities(constraint);
    std::vector<Kind> kinds;
    std::vector<std::vector<double>> anchors;
    std::vector<double *> parameterBlocks;
    kinds.reserve(references.size());
    anchors.reserve(references.size());
    parameterBlocks.reserve(references.size());
    for (const SketchEntityId id : references) {
      const WorkingEntity &entity = entities[index.at(id)];
      kinds.push_back(entity.kind);
      anchors.push_back(workingEntity(entity.source).values);
      parameterBlocks.push_back(entities[index.at(id)].values.data());
    }
    auto *cost = new ceres::DynamicNumericDiffCostFunction<ConstraintCost,
                                                           ceres::CENTRAL>(
        new ConstraintCost(constraint, references, kinds, anchors,
                           input.numerical));
    for (const SketchEntityId id : references)
      cost->AddParameterBlock(
          static_cast<int>(entities[index.at(id)].values.size()));
    const std::size_t rows = residualCount(constraint, entities, index);
    cost->SetNumResiduals(static_cast<int>(rows));
    documentBlocks.push_back(
        problem.AddResidualBlock(cost, nullptr, parameterBlocks));
    rowCounts.push_back(rows);
  }

  if (includeDrag && input.drag) {
    const auto found = index.find(input.drag->point.entity);
    if (found == index.end())
      return std::unexpected(diagnostic("sketch.drag.missing-entity",
                                        "drag target entity is missing"));
    WorkingEntity &entity = entities[found->second];
    if (!validPointKey(entity.kind, input.drag->point.key))
      return std::unexpected(diagnostic("sketch.drag.invalid-point-key",
                                        "drag point key is invalid"));
    auto *cost =
        new ceres::DynamicNumericDiffCostFunction<DragCost, ceres::CENTRAL>(
            new DragCost(entity.kind, input.drag->point.key, input.drag->target,
                         input.numerical.typicalLengthMeters));
    cost->AddParameterBlock(static_cast<int>(entity.values.size()));
    cost->SetNumResiduals(2);
    problem.AddResidualBlock(cost, nullptr, entity.values.data());
  }

  model::SolveResult result;
  if (input.cancellation.stop_requested()) {
    result.status = model::SolveStatus::Cancelled;
    return SolveAttempt{std::move(result), false};
  }

  ceres::Solver::Summary summary;
  const bool ranSolver =
      !documentBlocks.empty() || (includeDrag && input.drag.has_value());
  if (ranSolver) {
    CancellationCallback cancellation{input.cancellation};
    ceres::Solver::Options options;
    options.max_num_iterations =
        static_cast<int>(input.numerical.maximumIterations);
    options.linear_solver_type = ceres::SPARSE_NORMAL_CHOLESKY;
    options.sparse_linear_algebra_library_type = ceres::EIGEN_SPARSE;
    options.trust_region_strategy_type = ceres::DOGLEG;
    options.dogleg_type = ceres::SUBSPACE_DOGLEG;
    options.function_tolerance = 1.0e-14;
    options.gradient_tolerance = 1.0e-14;
    options.parameter_tolerance = 1.0e-14;
    options.logging_type = ceres::SILENT;
    options.minimizer_progress_to_stdout = false;
    options.num_threads = 1;
    options.callbacks.push_back(&cancellation);
    ceres::Solve(options, &problem, &summary);
    result.iterations = static_cast<std::uint32_t>(summary.iterations.size());
  }

  if (input.cancellation.stop_requested() ||
      summary.termination_type == ceres::USER_FAILURE) {
    result.status = model::SolveStatus::Cancelled;
    return SolveAttempt{std::move(result), false};
  }

  result.geometry.reserve(entities.size());
  for (const WorkingEntity &entity : entities) {
    auto solved = rebuild(entity);
    if (!solved)
      return std::unexpected(std::move(solved.error()));
    result.geometry.push_back(std::move(*solved));
  }
  auto residuals = model::evaluateResiduals(input.definition, result.geometry,
                                            input.numerical);
  if (!residuals)
    return std::unexpected(std::move(residuals.error()));
  result.residuals = std::move(*residuals);
  const bool satisfied = std::ranges::all_of(
      result.residuals, &model::ConstraintResidual::satisfied);

  const bool computeRedundancy = input.definition.constraints.size() <=
                                 input.numerical.maximumRedundancyConstraints;
  auto jacobian = analyzeJacobian(problem, documentBlocks, entities,
                                  input.numerical, true, computeRedundancy);
  if (!jacobian)
    return std::unexpected(std::move(jacobian.error()));
  const std::size_t variableCount =
      std::accumulate(entities.begin(), entities.end(), std::size_t{0},
                      [](std::size_t total, const WorkingEntity &entity) {
                        return total + entity.values.size();
                      });
  result.degreesOfFreedom =
      variableCount - std::min(variableCount, jacobian->rank);
  result.modes = std::move(jacobian->modes);

  if (computeRedundancy) {
    std::unordered_set<std::size_t> pivotRows(jacobian->pivotRows.begin(),
                                              jacobian->pivotRows.end());
    std::size_t row = 0;
    for (std::size_t constraintIndex = 0; constraintIndex < rowCounts.size();
         ++constraintIndex) {
      bool contributes = false;
      for (std::size_t offset = 0; offset < rowCounts[constraintIndex];
           ++offset)
        contributes = contributes || pivotRows.contains(row + offset);
      if (!contributes)
        result.redundantConstraints.push_back(
            model::constraintId(input.definition.constraints[constraintIndex]));
      row += rowCounts[constraintIndex];
    }
  }

  if (!satisfied) {
    model::ConflictSet conflict;
    for (const model::ConstraintResidual &residual : result.residuals) {
      if (!residual.satisfied)
        conflict.constraints.push_back(residual.constraint);
    }
    if (!conflict.constraints.empty())
      result.conflicts.push_back(std::move(conflict));
  }

  if (ranSolver && summary.termination_type == ceres::FAILURE) {
    result.status = model::SolveStatus::Diverged;
    result.diagnostics.push_back(
        diagnostic("sketch.solver.failed", "sketch solver failed to converge"));
  } else if (!satisfied) {
    result.status = model::SolveStatus::Inconsistent;
  } else {
    result.status = result.degreesOfFreedom == 0
                        ? model::SolveStatus::Solved
                        : model::SolveStatus::Underconstrained;
  }
  return SolveAttempt{std::move(result), satisfied};
}

} // namespace

Result<sketch::SolveResult>
CeresSketchSolver::solve(const sketch::SolveInput &input) const {
  if (auto valid = sketch::validate(input.definition, input.numerical); !valid)
    return std::unexpected(std::move(valid.error()));
  auto attempt = solveOnce(input, input.drag.has_value());
  if (!attempt)
    return std::unexpected(std::move(attempt.error()));
  if (input.drag && !attempt->documentSatisfied &&
      attempt->result.status != sketch::SolveStatus::Cancelled) {
    auto constrained = solveOnce(input, false);
    if (!constrained)
      return std::unexpected(std::move(constrained.error()));
    constrained->result.diagnostics.push_back(
        diagnostic("sketch.drag.blocked",
                   "drag target conflicts with existing constraints",
                   Severity::Information));
    return std::move(constrained->result);
  }
  return std::move(attempt->result);
}

} // namespace kearne::adapters
