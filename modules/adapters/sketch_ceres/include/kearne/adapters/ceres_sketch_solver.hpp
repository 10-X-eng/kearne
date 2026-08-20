#pragma once

#include <kearne/sketch/model.hpp>

namespace kearne::adapters {

class CeresSketchSolver final : public sketch::Solver {
public:
  [[nodiscard]] Result<sketch::SolveResult>
  solve(const sketch::SolveInput &input) const override;
};

} // namespace kearne::adapters
