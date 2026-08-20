import QtQuick
import QtQuick.Layouts
import Kearne.UI

Rectangle {
    id: root

    property string semanticId: ""
    property string semanticName: label
    property string semanticRole: "status"
    property var semanticActions: []
    property string semanticValue: status
    property string status: "current"
    property string label: status.replace("-", " ")
    property color stateColor: {
        if (status === "current") return Theme.success
        if (status === "failed" || status === "permission-denied") return Theme.error
        if (status === "stale" || status === "preview") return Theme.stale
        if (status === "pending" || status === "loading") return Theme.warning
        return Theme.textMuted
    }

    implicitWidth: row.implicitWidth + Theme.space3 * 2
    implicitHeight: 24
    radius: Theme.badgeRadius
    color: Theme.surfaceRaised
    border.color: Theme.border
    Accessible.id: semanticId
    Accessible.name: semanticName
    Accessible.role: Accessible.StaticText

    RowLayout {
        id: row
        anchors.centerIn: parent
        spacing: Theme.space1

        Rectangle {
            Layout.preferredWidth: 6
            Layout.preferredHeight: 6
            radius: width / 2
            color: root.stateColor
        }

        Text {
            text: root.label
            color: Theme.textMuted
            font.pixelSize: Theme.fontSmall
            font.capitalization: Font.Capitalize
        }
    }
}
