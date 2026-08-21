pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kearne.UI

Popup {
    id: root

    property string semanticId: "dialog.command_palette"
    property string semanticName: "Command palette"
    property string semanticRole: "dialog"
    property var semanticActions: ["dismiss"]
    property alias query: search.text

    function performSemanticAction(action, value) {
        if (action !== "dismiss")
            return false
        close()
        return true
    }

    parent: Overlay.overlay
    width: Math.min(620, parent.width - Theme.space6 * 2)
    height: Math.min(520, parent.height - Theme.space6 * 2)
    x: Math.round((parent.width - width) / 2)
    y: Math.max(Theme.space6, Math.round(parent.height * 0.12))
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    padding: 0

    onOpened: {
        search.text = ""
        search.forceActiveFocus()
    }

    background: Rectangle {
        radius: Theme.radius
        color: Theme.surface
        border.color: Theme.borderStrong
        border.width: Theme.separatorWidth
    }

    contentItem: ColumnLayout {
        spacing: 0

        KTextField {
            id: search
            semanticId: "command_palette.query"
            semanticName: "Search commands"
            semanticRole: "searchbox"
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            Layout.margins: Theme.space3
            placeholderText: "Search commands"
            font.pixelSize: Theme.fontTitle
            background: Rectangle {
                radius: Theme.radiusSmall
                color: Theme.surfaceRaised
                border.color: parent.activeFocus ? Theme.focus : Theme.border
                border.width: parent.activeFocus ? Theme.focusRingWidth
                                                 : Theme.separatorWidth
            }
        }

        KSeparator { }

        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentHeight: resultColumn.height + Theme.space4
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            Column {
                id: resultColumn
                width: parent.width
                topPadding: Theme.space2

                Repeater {
                    model: root.opened ? App.ui.commandCatalog : []

                    KButton {
                        id: commandButton
                        required property var modelData
                        semanticId: "palette.command." + commandButton.modelData.id
                        semanticName: commandButton.modelData.available
                                      ? commandButton.modelData.label
                                      : commandButton.modelData.label + ": "
                                        + commandButton.modelData.unavailableReason
                        semanticValue: commandButton.modelData.shortcut
                        iconName: commandButton.modelData.icon
                        shortcut: commandButton.modelData.shortcut
                        visible: root.query.length === 0
                                 || commandButton.modelData.label.toLowerCase().includes(root.query.toLowerCase())
                                 || commandButton.modelData.id.toLowerCase().includes(root.query.toLowerCase())
                        width: resultColumn.width
                        height: visible ? 42 : 0
                        quiet: true
                        enabled: commandButton.modelData.available
                        text: commandButton.modelData.label
                              + (commandButton.modelData.available ? ""
                                 : "  · unavailable")
                              + (commandButton.modelData.shortcut
                                 ? "     " + commandButton.modelData.shortcut : "")
                        onClicked: {
                            App.ui.requestCommand(commandButton.modelData.id)
                            root.close()
                        }
                    }
                }
            }
        }
    }
}
