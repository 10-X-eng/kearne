import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kearne.UI

Rectangle {
    id: root

    property string semanticId: "region.command_strip"
    property string semanticName: "Context commands"
    property string semanticRole: "toolbar"
    property var semanticActions: []

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
                model: uiSession.commands

                Row {
                    required property var modelData
                    required property int index
                    height: root.height
                    spacing: Theme.space1

                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 1
                        height: 32
                        color: Theme.border
                        visible: index > 0
                                 && modelData.group !== uiSession.commands[index - 1].group
                    }

                    KButton {
                        semanticId: "command." + modelData.id
                        semanticName: modelData.label
                        semanticValue: modelData.shortcut
                        iconName: modelData.icon
                        iconAbove: true
                        iconSize: 20
                        shortcut: modelData.shortcut
                        compact: true
                        width: Math.max(52, implicitWidth)
                        height: root.height - 4
                        anchors.verticalCenter: parent.verticalCenter
                        quiet: true
                        checkable: true
                        checked: uiSession.activeCommandId === modelData.id
                        enabled: modelData.available
                        text: modelData.label
                        onClicked: uiSession.requestCommand(modelData.id)
                    }
                }
            }
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
