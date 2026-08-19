import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kearne.UI

Rectangle {
    id: root

    function unitIndex(unitId) {
        return Math.max(0, uiSession.lengthUnits.findIndex(unit => unit.id === unitId))
    }

    function unitSymbols() {
        return uiSession.lengthUnits.map(unit => unit.symbol)
    }

    property string semanticId: "region.workspace_bar"
    property string semanticName: "Workspaces"
    property string semanticRole: "tablist"
    property var semanticActions: []

    color: Theme.surface
    implicitHeight: Theme.workspaceBarHeight

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.space3
        spacing: 0

        Repeater {
            model: uiSession.workspaces

            KButton {
                required property var modelData
                semanticId: "workspace." + modelData.id
                semanticName: modelData.label + " workspace"
                semanticRole: "tab"
                iconName: modelData.icon
                text: modelData.label
                quiet: true
                checkable: true
                checked: uiSession.activeWorkspaceId === modelData.id
                Layout.preferredHeight: Theme.workspaceBarHeight - 1
                onClicked: uiSession.selectWorkspace(modelData.id)
            }
        }

        Item { Layout.fillWidth: true }

        KChoice {
            id: projectUnits
            semanticId: "project.length_unit"
            semanticName: "Current project length unit"
            model: root.unitSymbols()
            currentIndex: root.unitIndex(uiSession.projectLengthUnitId)
            Layout.preferredWidth: 68
            Layout.rightMargin: Theme.space3
            onActivated: index => uiSession.setProjectLengthUnit(uiSession.lengthUnits[index].id)

            ToolTip.visible: hovered
            ToolTip.delay: 700
            ToolTip.text: "Project units — display and input only"
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.border
    }
}
