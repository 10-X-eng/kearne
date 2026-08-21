#pragma once

#include "frontend_contract.hpp"

#include <memory>
#include <vector>

namespace kearne::ui {

class LocalSketchSession;

[[nodiscard]] std::unique_ptr<FrontendController> makeCaptureDesktopController(
    std::vector<UiOption> themeOptions, const QString &themeId,
    const QString &defaultLengthUnitId, const QString &interfaceDensityId,
    const QString &navigationProfileId, const QString &zoomDirectionId);

[[nodiscard]] std::unique_ptr<FrontendController> makeDesktopController(
    std::unique_ptr<LocalSketchSession> sketchSession,
    std::vector<UiOption> themeOptions, const QString &themeId,
    const QString &defaultLengthUnitId, const QString &interfaceDensityId,
    const QString &navigationProfileId, const QString &zoomDirectionId);

} // namespace kearne::ui
