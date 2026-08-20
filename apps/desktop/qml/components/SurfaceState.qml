import QtQuick
import QtQuick.Layouts
import Kearne.UI

Rectangle {
    id: root

    property string semanticId: "state.surface"
    property string semanticName: title
    property string semanticRole: "status"
    property var semanticActions: actionLabel.length > 0 ? ["invoke"] : []
    property string semanticValue: status
    property string status: "empty"
    property string title: ""
    property string detail: ""
    property string actionLabel: {
        if (status === "empty") return "Create sketch"
        if (status === "failed" || status === "stale") return "Inspect"
        if (status === "permission-denied") return "Permissions"
        if (status === "unavailable") return "Operations"
        return ""
    }
    property string iconName: {
        if (status === "current") return "check"
        if (status === "preview") return "preview"
        if (status === "pending") return "clock"
        if (status === "loading") return "loading"
        if (status === "stale") return "stale"
        if (status === "failed") return "error"
        if (status === "unavailable") return "unavailable"
        if (status === "read-only") return "lock"
        if (status === "permission-denied") return "shield"
        return "empty"
    }
    signal actionRequested()

    function performSemanticAction(action, value) {
        if (action !== "invoke" || actionLabel.length === 0)
            return false
        actionRequested()
        return true
    }

    width: Math.min(440, parent.width - Theme.space6 * 2)
    implicitHeight: content.implicitHeight + Theme.space6 * 2
    height: implicitHeight
    radius: Theme.radius
    color: Theme.surfaceRaised
    border.width: Theme.separatorWidth
    border.color: Theme.borderStrong
    activeFocusOnTab: actionLabel.length > 0
    Accessible.name: semanticName
    Accessible.description: detail
    Accessible.id: semanticId
    Accessible.role: actionLabel.length > 0 ? Accessible.Button : Accessible.StaticText
    Accessible.focusable: actionLabel.length > 0 && enabled && visible
    Accessible.onPressAction: performSemanticAction("invoke", null)

    Keys.onEnterPressed: performSemanticAction("invoke", null)
    Keys.onReturnPressed: performSemanticAction("invoke", null)
    Keys.onSpacePressed: performSemanticAction("invoke", null)

    ColumnLayout {
        id: content
        anchors.centerIn: parent
        width: parent.width - Theme.space6 * 2
        spacing: Theme.space3

        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 42
            Layout.preferredHeight: 42
            radius: height / 2
            color: Theme.accentSoft

            KIcon {
                anchors.centerIn: parent
                width: 22
                height: 22
                name: root.iconName
                color: root.status === "failed" || root.status === "permission-denied"
                       ? Theme.error : Theme.accent
            }
        }

        Text {
            Layout.fillWidth: true
            text: root.title
            color: Theme.text
            font.pixelSize: Theme.fontDisplay
            font.weight: Theme.fontWeightStrong
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
            status: root.status
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
