import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kearne.UI

Rectangle {
    id: root

    property string semanticId: "viewport.primary"
    property string semanticName: "Primary model viewport"
    property string semanticRole: "canvas"
    property var semanticActions: ["orbit", "pan", "zoom", "select"]
    property string semanticValue: uiSession.viewportDetail
    signal requestStructure()
    signal requestInspector()

    color: Theme.canvas
    clip: true

    EngineeringGrid {
        anchors.fill: parent
        visible: uiSession.gridVisible
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
            text: uiSession.projectName + "  ›  " + uiSession.activeWorkspaceId
            color: Theme.textMuted
            font.pixelSize: Theme.fontSmall
        }
    }

    Row {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: Theme.space4
        spacing: Theme.space2

        KButton {
            semanticId: "viewport.structure.toggle"
            semanticName: "Show structure panel"
            iconName: "structure"
            visible: root.width < 740
            text: "Structure"
            onClicked: root.requestStructure()
        }

        KButton {
            semanticId: "viewport.inspector.toggle"
            semanticName: "Show inspector panel"
            iconName: "inspect"
            visible: root.width < 860
            text: "Inspector"
            onClicked: root.requestInspector()
        }

        KButton {
            semanticId: "viewport.display_mode"
            semanticName: "Viewport display mode"
            iconName: "view"
            text: "Shaded with edges"
            onClicked: uiSession.requestCommand("viewport.display_mode")
        }
    }

    SurfaceState {
        anchors.centerIn: parent
        semanticId: "viewport.state"
        state: uiSession.viewportState
        title: uiSession.viewportHeadline
        detail: uiSession.viewportDetail
        onActionRequested: {
            if (state === "empty")
                uiSession.requestCommand("model.sketch.create")
            else if (state === "permission-denied")
                uiSession.navigateTo("settings")
            else
                uiSession.navigateTo("operations")
        }
    }

    Rectangle {
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: Theme.space4
        width: cameraRow.width + Theme.space4
        height: 38
        radius: Theme.radius
        color: Theme.surface
        border.color: Theme.border

        Row {
            id: cameraRow
            anchors.centerIn: parent
            spacing: 0

            Repeater {
                model: [
                    { label: "Orbit", icon: "revolve" },
                    { label: "Pan", icon: "pan" },
                    { label: "Zoom", icon: "search" },
                    { label: "Fit", icon: "fit" },
                    { label: "Front", icon: "view" }
                ]
                KButton {
                    required property var modelData
                    semanticId: "viewport.camera." + modelData.label.toLowerCase()
                    semanticName: modelData.label + " view"
                    iconName: modelData.icon
                    text: modelData.label
                    quiet: true
                    onClicked: uiSession.requestCommand("viewport.camera." + modelData.label.toLowerCase())
                }
            }
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
            text: uiSession.selectionSummary
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

            KButton {
                semanticId: "viewport.grid.toggle"
                semanticName: uiSession.gridVisible ? "Hide modeling grid" : "Show modeling grid"
                semanticValue: uiSession.gridVisible ? "visible" : "hidden"
                iconName: "grid"
                text: root.width >= 760 ? "Grid" : ""
                quiet: true
                checkable: true
                checked: uiSession.gridVisible
                onClicked: uiSession.setGridVisible(checked)
            }

            KButton {
                semanticId: "viewport.grid_snap.toggle"
                semanticName: uiSession.gridSnapEnabled ? "Disable grid snap" : "Enable grid snap"
                semanticValue: uiSession.gridSnapEnabled ? "enabled" : "disabled"
                iconName: "target"
                text: root.width >= 760 ? "Snap" : ""
                quiet: true
                checkable: true
                checked: uiSession.gridSnapEnabled
                onClicked: uiSession.setGridSnapEnabled(checked)
            }
        }
    }
}
