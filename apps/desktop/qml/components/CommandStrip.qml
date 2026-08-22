pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import Kearne.UI

Rectangle {
    id: root

    property string semanticId: "region.command_strip"
    property string semanticName: "Context commands"
    property string semanticRole: "toolbar"
    property var semanticActions: []
    readonly property var visibleCommands: App.ui.commands.filter(
                                               command => command.available)
    readonly property var families: commandFamilies(visibleCommands)
    readonly property int familyWidth: 84
    readonly property int overflowWidth: 52
    readonly property int horizontalMargins: Theme.space3 * 2
    readonly property bool needsOverflow:
        families.length * familyWidth + horizontalMargins > width
    readonly property int visibleFamilyCount: needsOverflow
        ? Math.max(1, Math.floor((width - horizontalMargins - overflowWidth)
                                / familyWidth))
        : families.length
    readonly property var visibleFamilies:
        families.slice(0, visibleFamilyCount)
    readonly property var overflowFamilies:
        families.slice(visibleFamilyCount)

    function commandFamilies(commands) {
        const records = []
        const byName = Object.create(null)
        for (const command of commands) {
            const name = command.menu.length > 0 ? command.menu : command.group
            let record = byName[name]
            if (!record) {
                record = {"label": name, "commands": []}
                byName[name] = record
                records.push(record)
            }
            record.commands.push(command)
        }
        return records
    }

    color: Theme.surface
    implicitHeight: Theme.commandBarHeight
    clip: true

    Row {
        id: commandRow

        x: Theme.space3
        height: parent.height
        spacing: 0

        Repeater {
            model: root.visibleFamilies

            CommandFamily {
                required property var modelData

                width: root.familyWidth
                height: root.height
                label: modelData.label
                commands: modelData.commands
            }
        }

        KButton {
            id: overflowButton

            width: root.overflowWidth
            height: root.height
            visible: root.overflowFamilies.length > 0
            semanticId: "command-menu.more"
            semanticName: "More tools"
            iconName: overflowMenu.opened ? "chevron-up" : "chevron"
            iconAbove: true
            iconSize: 16
            compact: true
            quiet: true
            text: "More"
            onClicked: overflowMenu.opened ? overflowMenu.close()
                                           : overflowMenu.open()
        }
    }

    Popup {
        id: overflowMenu

        parent: root
        x: Math.max(Theme.space3, root.width - width - Theme.space3)
        y: root.height
        width: 280
        height: Math.min(520, overflowColumn.implicitHeight + Theme.space4)
        padding: Theme.space2
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: Theme.surface
            border.color: Theme.borderStrong
            border.width: Theme.separatorWidth
            radius: Theme.radius
        }

        contentItem: Flickable {
            id: overflowScroll

            clip: true
            contentWidth: width
            contentHeight: overflowColumn.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            flickableDirection: Flickable.VerticalFlick

            Column {
                id: overflowColumn

                width: overflowScroll.width
                spacing: Theme.space2

                Repeater {
                    model: root.overflowFamilies

                    Column {
                        required property var modelData

                        width: overflowColumn.width
                        spacing: Theme.space1

                        Text {
                            width: parent.width
                            leftPadding: Theme.space2
                            rightPadding: Theme.space2
                            text: parent.modelData.label.toUpperCase()
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontSmall
                            font.weight: Theme.fontWeightStrong
                        }

                        Repeater {
                            model: parent.modelData.commands

                            KButton {
                                required property var modelData

                                width: overflowColumn.width
                                semanticId: "command." + modelData.id
                                semanticName: modelData.label
                                semanticValue: checked ? "selected" : modelData.shortcut
                                iconName: modelData.icon
                                shortcut: modelData.shortcut
                                quiet: true
                                checkable: true
                                checked: modelData.checked
                                         || App.ui.activeCommandId
                                            === modelData.id
                                text: modelData.label
                                onClicked: {
                                    overflowMenu.close()
                                    App.ui.requestCommand(modelData.id)
                                }
                            }
                        }
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
