import QtQuick
import QtQuick.Layouts
import Kearne.UI

Rectangle {
    id: root

    property string semanticId: "region.status_bar"
    property string semanticName: "Application status"
    property string semanticRole: "statusbar"
    property var semanticActions: []
    property string gridSpacingLabel: App.ui.gridSpacingLabel
    property string semanticValue: App.ui.modelHealth + ":" + gridSpacingLabel

    color: Theme.surface
    implicitHeight: Theme.statusBarHeight

    Rectangle {
        anchors.top: parent.top
        width: parent.width
        height: Theme.separatorWidth
        color: Theme.border
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.space3
        anchors.rightMargin: Theme.space3
        spacing: Theme.space3

        Rectangle {
            Layout.preferredWidth: 7
            Layout.preferredHeight: 7
            radius: width / 2
            color: App.ui.backendConnected ? Theme.success : Theme.warning
        }

        Text {
            text: App.ui.modelHealth
            color: Theme.textMuted
            font.pixelSize: Theme.fontSmall
        }

        KSeparator {
            orientation: Qt.Vertical
            fillAvailable: false
            Layout.preferredHeight: Theme.iconSizeSmall
        }

        Text {
            text: App.ui.selectionSummary
            color: Theme.textMuted
            font.pixelSize: Theme.fontSmall
        }

        Item { Layout.fillWidth: true }

        Text {
            text: App.ui.jobs.length + " operation" + (App.ui.jobs.length === 1 ? "" : "s")
            color: Theme.textMuted
            font.pixelSize: Theme.fontSmall
        }

        KSeparator {
            orientation: Qt.Vertical
            fillAvailable: false
            Layout.preferredHeight: Theme.iconSizeSmall
        }

        Text {
            text: "Grid " + root.gridSpacingLabel + "  ·  Snap: "
                  + (App.workspace.gridSnapEnabled ? "grid, " : "") + "point, edge"
            color: Theme.textMuted
            font.pixelSize: Theme.fontSmall
        }
    }
}
