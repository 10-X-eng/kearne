import QtQuick
import QtQuick.Layouts
import Kearne.UI

Rectangle {
    id: root

    property string semanticId: "state.surface"
    property string semanticName: title
    property string semanticRole: "status"
    property var semanticActions: actionLabel.length > 0 ? ["invoke"] : []
    property string semanticValue: state
    property string state: "empty"
    property string title: ""
    property string detail: ""
    property string actionLabel: {
        if (state === "empty") return "Create sketch"
        if (state === "failed" || state === "stale") return "Inspect"
        if (state === "permission-denied") return "Permissions"
        if (state === "unavailable") return "Operations"
        return ""
    }
    property string iconName: {
        if (state === "current") return "check"
        if (state === "preview") return "preview"
        if (state === "pending") return "clock"
        if (state === "loading") return "loading"
        if (state === "stale") return "stale"
        if (state === "failed") return "error"
        if (state === "unavailable") return "unavailable"
        if (state === "read-only") return "lock"
        if (state === "permission-denied") return "shield"
        return "empty"
    }
    signal actionRequested()

    width: Math.min(440, parent.width - Theme.space6 * 2)
    implicitHeight: content.implicitHeight + Theme.space6 * 2
    height: implicitHeight
    radius: Theme.radius
    color: Theme.surfaceRaised
    border.width: 1
    border.color: Theme.borderStrong

    ColumnLayout {
        id: content
        anchors.centerIn: parent
        width: parent.width - Theme.space6 * 2
        spacing: Theme.space3

        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 42
            Layout.preferredHeight: 42
            radius: 21
            color: Theme.accentSoft

            KIcon {
                anchors.centerIn: parent
                width: 22
                height: 22
                name: root.iconName
                color: root.state === "failed" || root.state === "permission-denied"
                       ? Theme.error : Theme.accent
            }
        }

        Text {
            Layout.fillWidth: true
            text: root.title
            color: Theme.text
            font.pixelSize: Theme.fontDisplay
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
        }

        Text {
            Layout.fillWidth: true
            text: root.detail
            color: Theme.textMuted
            font.pixelSize: Theme.fontBody
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        StateBadge {
            Layout.alignment: Qt.AlignHCenter
            state: root.state
        }

        KButton {
            semanticId: visible ? root.semanticId + ".action" : ""
            semanticName: root.actionLabel
            iconName: root.iconName
            text: root.actionLabel
            primary: true
            visible: root.actionLabel.length > 0
            Layout.alignment: Qt.AlignHCenter
            onClicked: root.actionRequested()
        }
    }
}
