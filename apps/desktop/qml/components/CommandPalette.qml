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
        border.width: 1
    }

    contentItem: ColumnLayout {
        spacing: 0

        TextField {
            id: search
            property string semanticId: "command_palette.query"
            property string semanticName: "Search commands"
            property string semanticRole: "searchbox"
            property var semanticActions: ["focus", "setValue"]
            property string semanticValue: text
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            Layout.margins: Theme.space3
            placeholderText: "Search commands"
            color: Theme.text
            font.pixelSize: Theme.fontTitle
            selectByMouse: true
            Accessible.name: semanticName
            background: Rectangle {
                radius: Theme.radiusSmall
                color: Theme.surfaceRaised
                border.color: parent.activeFocus ? Theme.focus : Theme.border
                border.width: parent.activeFocus ? 2 : 1
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }

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
                    model: uiSession.commandCatalog

                    KButton {
                        required property var modelData
                        semanticId: "palette.command." + modelData.id
                        semanticName: modelData.label
                        semanticValue: modelData.shortcut
                        iconName: modelData.icon
                        shortcut: modelData.shortcut
                        visible: root.query.length === 0
                                 || modelData.label.toLowerCase().includes(root.query.toLowerCase())
                                 || modelData.id.toLowerCase().includes(root.query.toLowerCase())
                        width: resultColumn.width
                        height: visible ? 42 : 0
                        quiet: true
                        text: modelData.label + (modelData.shortcut ? "     " + modelData.shortcut : "")
                        onClicked: {
                            uiSession.requestCommand(modelData.id)
                            root.close()
                        }
                    }
                }
            }
        }
    }
}
