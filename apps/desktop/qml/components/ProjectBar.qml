import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kearne.UI

Rectangle {
    id: root

    property string semanticId: "region.project_bar"
    property string semanticName: "Application navigation"
    property string semanticRole: "toolbar"
    property var semanticActions: []
    signal openCommandPalette()

    color: Theme.surface
    implicitHeight: Theme.projectBarHeight

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.space2
        anchors.rightMargin: Theme.space2
        spacing: Theme.space2

        KButton {
            semanticId: "navigation.projects"
            semanticName: "Projects"
            iconName: "brand"
            text: root.width >= 930 ? "KEARNE" : ""
            quiet: true
            font.capitalization: Font.AllUppercase
            onClicked: uiSession.navigateTo("projects")
        }

        Rectangle {
            Layout.preferredWidth: 1
            Layout.preferredHeight: 22
            color: Theme.border
        }

        KButton {
            semanticId: "navigation.editor"
            semanticName: "Open " + uiSession.projectName
            iconName: "folder"
            text: uiSession.projectName
            quiet: true
            checkable: true
            checked: uiSession.activeSurfaceId === "editor"
            onClicked: uiSession.navigateTo("editor")
        }

        Rectangle {
            visible: root.width >= 1120
            Layout.preferredHeight: 24
            Layout.preferredWidth: visible ? branchText.implicitWidth + Theme.space4 : 0
            radius: 12
            color: Theme.surfaceRaised
            border.color: Theme.border

            Text {
                id: branchText
                anchors.centerIn: parent
                text: "●  " + uiSession.branchLabel
                color: Theme.textMuted
                font.pixelSize: Theme.fontSmall
            }
        }

        Text {
            visible: root.width >= 1280
            text: uiSession.revisionLabel
            color: Theme.textFaint
            font.pixelSize: Theme.fontSmall
        }

        Item { Layout.fillWidth: true }

        KButton {
            semanticId: "command.palette.open"
            semanticName: "Search or run a command"
            iconName: "search"
            text: root.width >= 1080 ? "Search commands" : ""
            shortcut: "Ctrl+K"
            Layout.preferredWidth: root.width >= 1080 ? 200 : implicitWidth
            onClicked: root.openCommandPalette()
        }

        KButton {
            semanticId: "navigation.operations"
            semanticName: "Operations and diagnostics"
            iconName: "operations"
            text: root.width >= 1180 ? "Operations" : ""
            quiet: true
            checkable: true
            checked: uiSession.activeSurfaceId === "operations"
            onClicked: uiSession.navigateTo("operations")
        }

        KButton {
            semanticId: "navigation.settings"
            semanticName: "Settings"
            iconName: "settings"
            quiet: true
            checkable: true
            checked: uiSession.activeSurfaceId === "settings"
            onClicked: uiSession.navigateTo("settings")
        }

        KButton {
            semanticId: "project.save"
            semanticName: "Save project"
            iconName: "save"
            text: root.width >= 920 ? "Save" : ""
            primary: true
            visible: uiSession.activeSurfaceId === "editor"
            onClicked: uiSession.requestCommand("project.save")
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
