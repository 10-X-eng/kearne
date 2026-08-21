pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import Kearne.UI

Item {
    id: root

    required property string label
    required property var commands
    readonly property var primaryCommand: {
        const active = commands.find(
                         command => command.id === App.ui.activeCommandId)
        if (active)
            return active
        const preferred = commands.find(command => command.primary)
        return preferred ?? commands[0]
    }
    readonly property var alternateCommands:
        commands.filter(command => command.id !== primaryCommand.id)

    implicitWidth: 84
    implicitHeight: Theme.commandBarHeight

    KButton {
        id: primaryButton

        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: root.alternateCommands.length > 0
               ? parent.width - menuButton.width : parent.width
        semanticId: "command." + root.primaryCommand.id
        semanticName: root.primaryCommand.label + " · " + root.label
        semanticValue: root.primaryCommand.shortcut
        iconName: root.primaryCommand.icon
        iconAbove: true
        iconSize: 19
        shortcut: root.primaryCommand.shortcut
        compact: true
        quiet: true
        checkable: true
        checked: App.ui.activeCommandId === root.primaryCommand.id
        text: root.label
        onClicked: App.ui.requestCommand(root.primaryCommand.id)
    }

    KButton {
        id: menuButton

        anchors.top: parent.top
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        width: 20
        visible: root.alternateCommands.length > 0
        semanticId: "command-menu." + root.primaryCommand.workspaceId + "."
                    + root.label.toLowerCase().replace(/[^a-z0-9]+/g, "-")
        semanticName: root.label + " tools"
        iconName: familyMenu.opened ? "chevron-up" : "chevron"
        iconSize: 12
        compact: true
        quiet: true
        onClicked: familyMenu.opened ? familyMenu.close() : familyMenu.open()
    }

    Popup {
        id: familyMenu

        x: 0
        y: root.height
        width: 250
        padding: Theme.space2
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: Theme.surface
            border.color: Theme.borderStrong
            border.width: Theme.separatorWidth
            radius: Theme.radius
        }

        contentItem: Column {
            width: familyMenu.availableWidth
            spacing: Theme.space1

            Text {
                width: familyMenu.availableWidth
                leftPadding: Theme.space2
                rightPadding: Theme.space2
                bottomPadding: Theme.space1
                text: root.label.toUpperCase()
                color: Theme.textMuted
                font.pixelSize: Theme.fontSmall
                font.weight: Theme.fontWeightStrong
            }

            Repeater {
                model: root.alternateCommands

                KButton {
                    required property var modelData

                    width: familyMenu.availableWidth
                    semanticId: "command." + modelData.id
                    semanticName: modelData.label
                    semanticValue: modelData.shortcut
                    iconName: modelData.icon
                    shortcut: modelData.shortcut
                    quiet: true
                    checkable: true
                    checked: App.ui.activeCommandId === modelData.id
                    text: modelData.label
                    onClicked: {
                        familyMenu.close()
                        App.ui.requestCommand(modelData.id)
                    }
                }
            }
        }
    }
}
