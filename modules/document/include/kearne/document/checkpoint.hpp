#pragma once

#include <kearne/document/mutation.hpp>
#include <kearne/document/project_state.hpp>

#include <span>

namespace kearne::document {

using ProjectCheckpointLimits = MutationDecodeLimits;

[[nodiscard]] Result<Bytes> canonicalBytes(const ProjectSnapshot &snapshot,
                                           ProjectCheckpointLimits limits = {});
[[nodiscard]] Result<ProjectSnapshot>
decodeProjectCheckpoint(std::span<const std::uint8_t> bytes,
                        ProjectCheckpointLimits limits = {});

} // namespace kearne::document
