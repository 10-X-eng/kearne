#pragma once

#include "frontend_contract.hpp"

#include <memory>
#include <vector>

namespace kearne::ui {

[[nodiscard]] std::unique_ptr<FrontendPort> makeDevelopmentFrontendPort(
    std::vector<UiOption> themeOptions, const QString &themeId,
    const QString &defaultLengthUnitId, const QString &interfaceDensityId,
    const QString &navigationProfileId, const QString &zoomDirectionId);

} // namespace kearne::ui
