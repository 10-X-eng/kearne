pragma Singleton

import QtQuick

QtObject {
    readonly property color surface: "#ffffff"
    readonly property color surfaceRaised: "#f8fafc"
    readonly property color surfaceMuted: "#f1f5f8"
    readonly property color canvas: "#e8eef3"
    readonly property color canvasGridMinor: "#dce5eb"
    readonly property color canvasGridMajor: "#c2d0d9"
    readonly property color axisX: "#c95a5a"
    readonly property color axisY: "#4c9769"
    readonly property color axisZ: "#397eaa"
    readonly property color border: "#d7e0e7"
    readonly property color borderStrong: "#b9c8d3"
    readonly property color text: "#17232c"
    readonly property color textMuted: "#687984"
    readonly property color textFaint: "#8c9aa4"
    readonly property color accent: "#187b99"
    readonly property color accentHover: "#126983"
    readonly property color accentSoft: "#e2f2f7"
    readonly property color focus: "#1688b0"
    readonly property color selection: "#dceff5"
    readonly property color success: "#31845b"
    readonly property color warning: "#a66a13"
    readonly property color error: "#b54d4d"
    readonly property color stale: "#7559a8"
    readonly property color agent: "#146d8a"
    readonly property color transparent: "transparent"

    readonly property int space1: 4
    readonly property int space2: 8
    readonly property int space3: 12
    readonly property int space4: 16
    readonly property int space6: 24
    readonly property int radiusSmall: 3
    readonly property int radius: 5
    readonly property int controlHeight: 32
    readonly property int projectBarHeight: 44
    readonly property int workspaceBarHeight: 36
    readonly property int commandBarHeight: 56
    readonly property int statusBarHeight: 28
    readonly property int leftPanelWidth: 256
    readonly property int rightPanelWidth: 336
    readonly property int fontSmall: 11
    readonly property int fontBody: 12
    readonly property int fontTitle: 14
    readonly property int fontDisplay: 18
}
