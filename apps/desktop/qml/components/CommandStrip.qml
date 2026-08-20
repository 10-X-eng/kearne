pragma ComponentBehavior: Bound

import QtQuick
import Kearne.UI

Rectangle {
    id: root

    property string semanticId: "region.command_strip"
    property string semanticName: "Context commands"
    property string semanticRole: "toolbar"
    property var semanticActions: []
    readonly property var visibleCommands: App.ui.commands.filter(
                                               command => command.available)

    color: Theme.surface
    implicitHeight: Theme.commandBarHeight
    clip: true

    Flickable {
        anchors.fill: parent
        contentWidth: commandRow.width + Theme.space6
        contentHeight: height
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.HorizontalFlick

        Row {
            id: commandRow
            x: Theme.space3
            height: parent.height
            spacing: Theme.space1

            Repeater {
                model: root.visibleCommands

                Row {
                    id: commandGroup
                    required property var modelData
                    required property int index
                    height: root.height
                    spacing: Theme.space1

                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: Theme.separatorWidth
                        height: Theme.controlHeight
                        color: Theme.border
                        visible: commandGroup.index > 0
                                 && commandGroup.modelData.group
                                    !== root.visibleCommands[commandGroup.index - 1].group
                    }

                    KButton {
                        semanticId: "command." + commandGroup.modelData.id
                        semanticName: commandGroup.modelData.available
                                      ? commandGroup.modelData.label
                                      : commandGroup.modelData.label + ": "
                                        + commandGroup.modelData.unavailableReason
                        semanticValue: commandGroup.modelData.shortcut
                        iconName: commandGroup.modelData.icon
                        iconAbove: true
                        iconSize: 20
                        shortcut: commandGroup.modelData.shortcut
                        compact: true
                        width: Math.max(52, implicitWidth)
                        height: root.height - 4
                        anchors.verticalCenter: parent.verticalCenter
                        quiet: true
                        checkable: true
                        checked: App.ui.activeCommandId === commandGroup.modelData.id
                        enabled: commandGroup.modelData.available
                        text: commandGroup.modelData.label
                        onClicked: App.ui.requestCommand(commandGroup.modelData.id)
                    }
                }
            }
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
