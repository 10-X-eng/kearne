pragma Singleton

import QtQuick

QtObject {
    readonly property var values: App.themes.tokens
    readonly property real densityScale: App.ui.interfaceDensityId === "comfortable"
                                         ? 1.15 : 1.0

    function scaled(value) {
        return Math.round(Number(value) * densityScale)
    }

    readonly property color surface: values.surface
    readonly property color surfaceRaised: values.surfaceRaised
    readonly property color surfaceMuted: values.surfaceMuted
    readonly property color canvas: values.canvas
    readonly property color canvasGridMinor: values.canvasGridMinor
    readonly property color canvasGridMajor: values.canvasGridMajor
    readonly property color axisX: values.axisX
    readonly property color axisY: values.axisY
    readonly property color axisZ: values.axisZ
    readonly property color border: values.border
    readonly property color borderStrong: values.borderStrong
    readonly property color text: values.text
    readonly property color textMuted: values.textMuted
    readonly property color textFaint: values.textFaint
    readonly property color accent: values.accent
    readonly property color accentHover: values.accentHover
    readonly property color accentSoft: values.accentSoft
    readonly property color focus: values.focus
    readonly property color selection: values.selection
    readonly property color success: values.success
    readonly property color warning: values.warning
    readonly property color error: values.error
    readonly property color stale: values.stale
    readonly property color agent: values.agent
    readonly property color transparent: values.transparent

    readonly property int space1: scaled(values.space1)
    readonly property int space2: scaled(values.space2)
    readonly property int space3: scaled(values.space3)
    readonly property int space4: scaled(values.space4)
    readonly property int space6: scaled(values.space6)
    readonly property int radiusSmall: values.radiusSmall
    readonly property int radius: values.radius
    readonly property int badgeRadius: values.badgeRadius
    readonly property int separatorWidth: values.separatorWidth
    readonly property int focusRingWidth: values.focusRingWidth
    readonly property int iconSizeSmall: scaled(values.iconSizeSmall)
    readonly property int iconSize: scaled(values.iconSize)
    readonly property int iconSizeLarge: scaled(values.iconSizeLarge)
    readonly property real iconStrokeWidth: values.iconStrokeWidth
    readonly property real gridLineWidthMinor: values.gridLineWidthMinor
    readonly property real gridLineWidthMajor: values.gridLineWidthMajor
    readonly property real axisLineWidth: values.axisLineWidth
    readonly property real disabledOpacity: values.disabledOpacity
    readonly property int controlHeightCompact: scaled(values.controlHeightCompact)
    readonly property int controlHeight: scaled(values.controlHeight)
    readonly property int viewportControlHeight: scaled(values.viewportControlHeight)
    readonly property int projectBarHeight: scaled(values.projectBarHeight)
    readonly property int workspaceBarHeight: scaled(values.workspaceBarHeight)
    readonly property int commandBarHeight: scaled(values.commandBarHeight)
    readonly property int statusBarHeight: scaled(values.statusBarHeight)
    readonly property int leftPanelWidth: scaled(values.leftPanelWidth)
    readonly property int rightPanelWidth: scaled(values.rightPanelWidth)
    readonly property int switchWidth: scaled(values.switchWidth)
    readonly property int switchHeight: scaled(values.switchHeight)
    readonly property int switchKnobSize: scaled(values.switchKnobSize)
    readonly property string fontUiFamily: values.fontUiFamily
    readonly property string fontDataFamily: values.fontDataFamily
    readonly property int fontSmall: scaled(values.fontSmall)
    readonly property int fontBody: scaled(values.fontBody)
    readonly property int fontTitle: scaled(values.fontTitle)
    readonly property int fontDisplay: scaled(values.fontDisplay)
    readonly property int fontHeading: scaled(values.fontHeading)
    readonly property int fontWeightNormal: values.fontWeightNormal
    readonly property int fontWeightStrong: values.fontWeightStrong
    readonly property real letterSpacing: values.letterSpacing
}
