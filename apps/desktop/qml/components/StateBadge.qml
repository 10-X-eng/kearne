import QtQuick
import QtQuick.Layouts
import Kearne.UI

Rectangle {
    id: root

    property string state: "current"
    property string label: state.replace("-", " ")
    property color stateColor: {
        if (state === "current") return Theme.success
        if (state === "failed" || state === "permission-denied") return Theme.error
        if (state === "stale" || state === "preview") return Theme.stale
        if (state === "pending" || state === "loading") return Theme.warning
        return Theme.textMuted
    }

    implicitWidth: row.implicitWidth + Theme.space3 * 2
    implicitHeight: 24
    radius: 12
    color: Theme.surfaceRaised
    border.color: Theme.border

    RowLayout {
        id: row
        anchors.centerIn: parent
        spacing: Theme.space1

        Rectangle {
            Layout.preferredWidth: 6
            Layout.preferredHeight: 6
            radius: 3
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
