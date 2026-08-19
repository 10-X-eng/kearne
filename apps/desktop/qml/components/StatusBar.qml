import QtQuick
import QtQuick.Layouts
import Kearne.UI

Rectangle {
    id: root

    property string semanticId: "region.status_bar"
    property string semanticName: "Application status"
    property string semanticRole: "statusbar"
    property var semanticActions: []
    property string semanticValue: uiSession.modelHealth

    color: Theme.surface
    implicitHeight: Theme.statusBarHeight

    Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.border }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.space3
        anchors.rightMargin: Theme.space3
        spacing: Theme.space3

        Rectangle {
            Layout.preferredWidth: 7
            Layout.preferredHeight: 7
            radius: 4
            color: uiSession.backendConnected ? Theme.success : Theme.warning
        }

        Text {
            text: uiSession.modelHealth
            color: Theme.textMuted
            font.pixelSize: Theme.fontSmall
        }

        Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 14; color: Theme.border }

        Text {
            text: uiSession.selectionSummary
            color: Theme.textMuted
            font.pixelSize: Theme.fontSmall
        }

        Item { Layout.fillWidth: true }

        Text {
            text: uiSession.jobs.length + " operation" + (uiSession.jobs.length === 1 ? "" : "s")
            color: Theme.textMuted
            font.pixelSize: Theme.fontSmall
        }

        Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 14; color: Theme.border }

        Text {
            text: "Grid " + uiSession.gridSpacingLabel + "  ·  Snap: "
                  + (uiSession.gridSnapEnabled ? "grid, " : "") + "point, edge"
            color: Theme.textMuted
            font.pixelSize: Theme.fontSmall
        }
    }
}
