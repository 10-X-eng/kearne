import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kearne.UI

Rectangle {
    id: root

    function unitIndex(unitId) {
        return Math.max(0, App.ui.lengthUnits.findIndex(unit => unit.id === unitId))
    }

    function unitSymbols() {
        return App.ui.lengthUnits.map(unit => unit.symbol)
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
            model: App.ui.workspaces

            KButton {
                required property var modelData
                semanticId: "workspace." + modelData.id
                semanticName: modelData.label + " workspace"
                semanticRole: "tab"
                iconName: modelData.icon
                text: modelData.label
                quiet: true
                checkable: true
                checked: App.ui.activeWorkspaceId === modelData.id
                Layout.preferredHeight: Theme.workspaceBarHeight - 1
                onClicked: App.ui.selectWorkspace(modelData.id)
            }
        }

        Item { Layout.fillWidth: true }

        KChoice {
            id: projectUnits
            semanticId: "project.length_unit"
            semanticName: "Current project length unit"
            model: root.unitSymbols()
            semanticOptions: App.ui.lengthUnits.map(unit => unit.id)
            currentIndex: root.unitIndex(App.ui.projectLengthUnitId)
            Layout.preferredWidth: 68
            Layout.rightMargin: Theme.space3
            onActivated: index => App.ui.setPreference(
                             "project-length-unit", App.ui.lengthUnits[index].id)

            ToolTip.visible: hovered
            ToolTip.delay: 700
            ToolTip.text: "Project units — display and input only"
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: Theme.separatorWidth
        color: Theme.border
    }
}
