#pragma once

#include <kearne/document/mutation.hpp>
#include <kearne/document/project_state.hpp>

#include <span>

namespace kearne::document::internal {

class ProjectStateAccess final {
public:
  [[nodiscard]] static Result<ProjectState>
  apply(const ProjectState &base, std::span<const Mutation> mutations);
  [[nodiscard]] static Result<void> validate(const ProjectState &state);
};

} // namespace kearne::document::internal
