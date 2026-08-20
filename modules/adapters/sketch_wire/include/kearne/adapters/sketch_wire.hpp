#pragma once

#include <kearne/api/v1/sketch.pb.h>
#include <kearne/sketch/model.hpp>

#include <cstddef>
#include <span>
#include <string>

namespace kearne::adapters {

// Coordinates are local to the trusted SketchPlane supplied by evaluation.
inline constexpr std::size_t maximumSketchDefinitionWireBytes = 16'777'216;
inline constexpr std::size_t maximumSketchDefinitionEntities = 65'536;
inline constexpr std::size_t maximumSketchDefinitionConstraints = 65'536;
inline constexpr int maximumSketchDefinitionWireDepth = 16;

[[nodiscard]] Result<sketch::Definition>
readSketchDefinition(const api::v1::SketchDefinition &wire,
                     const sketch::NumericalProfile &profile = {});

[[nodiscard]] Result<void>
writeSketchDefinition(const sketch::Definition &definition,
                      api::v1::SketchDefinition *wire,
                      const sketch::NumericalProfile &profile = {});

[[nodiscard]] Result<sketch::Definition>
parseSketchDefinition(std::span<const std::byte> bytes,
                      const sketch::NumericalProfile &profile = {});

[[nodiscard]] Result<std::string>
serializeSketchDefinition(const sketch::Definition &definition,
                          const sketch::NumericalProfile &profile = {});

} // namespace kearne::adapters
