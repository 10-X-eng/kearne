pragma ComponentBehavior: Bound

import QtQuick
import Kearne.UI

Rectangle {
    id: root

    property string semanticId: "viewport.primary"
    property string semanticName: "Primary model viewport"
    property string semanticRole: "canvas"
    property var semanticActions: ["orbit", "pan", "zoom", "fit", "select",
                                   "pointerDrag", "pointerClick",
                                   "toggleConstruction"]
    property string semanticValue: App.camera.state + ":" + App.ui.selectionSummary
    property bool structureAvailable: true
    property bool inspectorAvailable: true
    property real modelGridSpacingMillimeters: App.ui.gridSpacingMillimeters
    readonly property real displayedGridSpacingMillimeters:
        App.ui.sketchEditing
        ? engineeringGrid.displayedSpacingMillimeters
        : modelGridSpacingMillimeters
    signal requestStructure()
    signal requestInspector()

    function displayModeIndex() {
        return ["shaded-edges", "shaded", "wireframe"]
                .indexOf(App.workspace.displayMode)
    }

    function performSemanticAction(action, value) {
        if (action === "orbit") {
            App.camera.orbit(24, -12)
            return true
        }
        if (action === "pan") {
            if (App.ui.sketchEditing)
                App.sketchCamera.pan(18, 12)
            else
                App.camera.pan(18, 12)
            return true
        }
        if (action === "zoom") {
            const amount = value === null || value === undefined ? 1
                                                                  : Number(value)
            if (App.ui.sketchEditing)
                App.sketchCamera.zoomAt(amount, width / 2, height / 2,
                                        width, height)
            else
                App.camera.zoom(amount)
            return true
        }
        if (action === "fit") {
            if (App.ui.sketchEditing)
                App.sketchCamera.reset()
            else
                App.camera.fit()
            return true
        }
        if (action === "toggleConstruction")
            return App.ui.toggleSketchConstruction()
        if (action !== "select" || value === null || value === undefined
                || String(value).length === 0)
            return false
        App.ui.selectEntity(String(value))
        return true
    }

    color: Theme.canvas
    clip: true
    activeFocusOnTab: true
    Accessible.name: semanticName
    Accessible.id: semanticId
    Accessible.role: Accessible.Canvas
    Accessible.focusable: enabled && visible

    Keys.onPressed: event => {
        const key = event.text.toUpperCase()
        if (event.key === Qt.Key_X
                && App.ui.sketchEditing) {
            event.accepted = App.ui.toggleSketchConstruction()
        } else if (event.key === Qt.Key_Home) {
            if (App.ui.sketchEditing)
                App.sketchCamera.reset()
            else
                App.camera.fit()
            event.accepted = true
        } else if (key === "0") {
            App.camera.setView("isometric")
            event.accepted = true
        } else if (event.key === Qt.Key_Left || event.key === Qt.Key_Right
                   || event.key === Qt.Key_Up || event.key === Qt.Key_Down) {
            App.camera.turn(event.key === Qt.Key_Left ? -15
                            : (event.key === Qt.Key_Right ? 15 : 0),
                            event.key === Qt.Key_Up ? 15
                            : (event.key === Qt.Key_Down ? -15 : 0))
            event.accepted = true
        }
    }

    EngineeringGrid {
        id: engineeringGrid
        anchors.fill: parent
        visible: App.workspace.gridVisible
                 && App.ui.sketchEditing
    }

    NavigationCalibrationScene {
        anchors.fill: parent
        visible: (App.ui.activeWorkspaceId === "model"
                  || (App.ui.activeWorkspaceId === "sketch"
                      && !App.ui.sketchEditing))
                 && App.ui.activeCommandId !== "model.sketch.create"
        opacity: App.ui.viewportState === "current" ? 1 : 0.38
        displayMode: App.workspace.displayMode
        gridVisible: App.workspace.gridVisible
        gridSpacingMillimeters: root.modelGridSpacingMillimeters
    }

    Item {
        objectName: "nativeSketchSceneHost"
        anchors.fill: parent
        visible: App.ui.sketchEditing
    }

    SketchCalibrationScene {
        id: sketchScene
        anchors.fill: parent
        visible: App.ui.sketchEditing
        snapSpacingMillimeters: engineeringGrid.displayedSpacingMillimeters
    }

    MouseArea {
        id: navigationArea
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
        hoverEnabled: true
        cursorShape: App.ui.sketchEditing
                     && App.ui.sketchInputKind !== "none"
                     ? Qt.CrossCursor : Qt.ArrowCursor
        property real previousX: 0
        property real previousY: 0
        property real pressedX: 0
        property real pressedY: 0
        property bool dragged: false

        onPressed: mouse => {
            previousX = mouse.x
            previousY = mouse.y
            pressedX = mouse.x
            pressedY = mouse.y
            dragged = false
            root.forceActiveFocus()
        }
        onPositionChanged: mouse => {
            const dx = mouse.x - previousX
            const dy = mouse.y - previousY
            previousX = mouse.x
            previousY = mouse.y
            if (App.ui.sketchEditing)
                sketchScene.updateCursor(mouse.x, mouse.y, true)
            if (Math.abs(dx) > 0 || Math.abs(dy) > 0) {
                if (App.ui.sketchEditing
                        && (mouse.buttons & Qt.MiddleButton)) {
                    App.sketchCamera.pan(dx, dy)
                    dragged = true
                } else if (App.ui.sketchEditing
                           && (mouse.buttons & Qt.LeftButton)
                           && Math.hypot(mouse.x - pressedX,
                                         mouse.y - pressedY) >= 4) {
                    dragged = true
                    const first = sketchScene.planePoint(pressedX, pressedY)
                    const opposite = sketchScene.planePoint(mouse.x, mouse.y)
                    App.ui.previewSketchDrag(first[0], first[1],
                                             opposite[0], opposite[1])
                } else if (!App.ui.sketchEditing
                           && App.camera.applyPointerDrag(mouse.buttons,
                                                          mouse.modifiers,
                                                          dx, dy)) {
                    dragged = true
                }
            }
        }
        onReleased: mouse => {
            App.ui.clearSketchDragPreview()
            if (mouse.button === Qt.LeftButton
                    && App.ui.sketchEditing) {
                if (dragged)
                    sketchScene.handleDrag(pressedX, pressedY,
                                           mouse.x, mouse.y)
                else
                    sketchScene.handleClick(mouse.x, mouse.y)
            } else if (!dragged && mouse.button === Qt.LeftButton) {
                App.ui.selectEntity("component.navigation-fixture")
            }
        }
        onCanceled: App.ui.clearSketchDragPreview()
        onExited: sketchScene.updateCursor(0, 0, false)
        onWheel: wheel => {
            if (App.ui.sketchEditing)
                App.sketchCamera.zoomAt(wheel.angleDelta.y / 120,
                                        wheel.x, wheel.y, width, height)
            else
                App.camera.applyWheel(wheel.angleDelta.y)
            wheel.accepted = true
        }
    }

    DatumPlaneSelector {
        anchors.fill: parent
        visible: App.ui.activeCommandId === "model.sketch.create"
                 && App.ui.commandDraftState !== "pending"
    }

    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.margins: Theme.space4
        width: breadcrumb.implicitWidth + Theme.space4
        height: 28
        radius: Theme.radiusSmall
        color: Theme.surface
        border.color: Theme.border

        Text {
            id: breadcrumb
            anchors.centerIn: parent
            text: App.ui.projectName + "  ›  " + App.ui.activeWorkspaceId
            color: Theme.textMuted
            font.pixelSize: Theme.fontSmall
        }
    }

    Row {
        id: panelToggles

        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: Theme.space4
        anchors.rightMargin: viewCube.visible
                             ? Theme.space4 + viewCube.width + Theme.space2
                             : Theme.space4
        spacing: Theme.space2

        KButton {
            semanticId: "viewport.structure.toggle"
            semanticName: "Show structure panel"
            iconName: "structure"
            visible: !root.structureAvailable
            text: "Structure"
            onClicked: root.requestStructure()
        }

        KButton {
            semanticId: "viewport.inspector.toggle"
            semanticName: "Show inspector panel"
            iconName: "inspect"
            visible: !root.inspectorAvailable
            text: "Inspector"
            onClicked: root.requestInspector()
        }
    }

    OrientationCube {
        id: viewCube

        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: Theme.space4
        visible: App.ui.activeWorkspaceId !== "drawing"
                 && App.ui.activeWorkspaceId !== "bom"
                 && !App.ui.sketchEditing
    }

    SurfaceState {
        id: surfaceState
        anchors.centerIn: parent
        semanticId: "viewport.state"
        status: App.ui.viewportState
        visible: status !== "current"
        title: App.ui.viewportHeadline
        detail: App.ui.viewportDetail
        onActionRequested: {
            if (surfaceState.status === "empty")
                App.ui.requestCommand("model.sketch.create")
            else if (surfaceState.status === "permission-denied")
                App.ui.navigateTo("settings")
            else
                App.ui.navigateTo("operations")
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: Theme.space4
        width: selectionText.implicitWidth + Theme.space4
        height: 28
        radius: Theme.radiusSmall
        color: Theme.surface
        border.color: Theme.border

        Text {
            id: selectionText
            anchors.centerIn: parent
            text: App.ui.selectionSummary
            color: Theme.textMuted
            font.pixelSize: Theme.fontSmall
        }
    }

    Rectangle {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.space4
        width: gridControls.width + Theme.space2
        height: 38
        radius: Theme.radius
        color: Theme.surface
        border.color: Theme.border

        Row {
            id: gridControls
            anchors.centerIn: parent
            spacing: 0

            KChoice {
                semanticId: "viewport.display_mode"
                semanticName: "Viewport display mode"
                semanticOptions: ["shaded-edges", "shaded", "wireframe"]
                model: ["Shaded with edges", "Shaded", "Wireframe"]
                currentIndex: root.displayModeIndex()
                iconName: "view"
                width: 176
                visible: App.ui.activeWorkspaceId === "model"
                onActivated: index => App.workspace.displayMode =
                             semanticOptions[index]
            }

            KButton {
                semanticId: "viewport.grid.toggle"
                semanticName: App.workspace.gridVisible ? "Hide modeling grid" : "Show modeling grid"
                semanticValue: App.workspace.gridVisible ? "visible" : "hidden"
                iconName: "grid"
                text: root.width >= 760 ? "Grid" : ""
                quiet: true
                checkable: true
                checked: App.workspace.gridVisible
                onClicked: App.workspace.gridVisible = checked
            }

            KButton {
                semanticId: "viewport.grid_snap.toggle"
                semanticName: App.workspace.gridSnapEnabled ? "Disable grid snap" : "Enable grid snap"
                semanticValue: App.workspace.gridSnapEnabled ? "enabled" : "disabled"
                iconName: "target"
                text: root.width >= 760 ? "Snap" : ""
                quiet: true
                checkable: true
                checked: App.workspace.gridSnapEnabled
                onClicked: App.workspace.gridSnapEnabled = checked
            }
        }
    }
}
