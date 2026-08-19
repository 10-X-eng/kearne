import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kearne.UI

Rectangle {
    id: root

    function iconFor(record) {
        if (root.activePage === 1)
            return "checkpoint"
        switch (record.kind) {
        case "sketch": return "sketch"
        case "plane": return "plane"
        case "model-function": return "code"
        case "group": return "folder"
        default: return "model"
        }
    }

    property string semanticId: "panel.structure"
    property string semanticName: "Structure and history"
    property string semanticRole: "panel"
    property var semanticActions: []
    property int activePage: 0

    color: Theme.surface
    border.color: Theme.border

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            Layout.leftMargin: Theme.space2
            Layout.rightMargin: Theme.space2
            spacing: 0

            KButton {
                semanticId: "structure.tab.entities"
                semanticName: "Structure"
                semanticRole: "tab"
                text: "Structure"
                quiet: true
                checkable: true
                checked: root.activePage === 0
                onClicked: root.activePage = 0
            }

            KButton {
                semanticId: "structure.tab.history"
                semanticName: "History"
                semanticRole: "tab"
                text: "History"
                quiet: true
                checkable: true
                checked: root.activePage === 1
                onClicked: root.activePage = 1
            }

            Item { Layout.fillWidth: true }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.border
        }

        ListView {
            id: entityList
            property string semanticId: root.activePage === 0 ? "structure.entities" : "structure.history"
            property string semanticName: root.activePage === 0 ? "Document structure" : "Revision history"
            property string semanticRole: "list"
            property var semanticActions: []

            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.activePage === 0 ? uiSession.structure : uiSession.revisions

            delegate: Rectangle {
                id: row
                required property var modelData
                property string semanticId: root.activePage === 0
                                                ? "entity." + modelData.id
                                                : "revision." + modelData.id
                property string semanticName: modelData.label
                property string semanticRole: "listitem"
                property var semanticActions: ["select"]
                property string semanticValue: root.activePage === 0
                                                   ? modelData.kind
                                                   : modelData.detail

                width: entityList.width
                height: 32
                color: pointer.containsMouse ? Theme.surfaceMuted : Theme.surface

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.space2 + (modelData.depth ?? 0) * Theme.space4
                    anchors.rightMargin: Theme.space2
                    spacing: Theme.space2

                    KIcon {
                        Layout.preferredWidth: 14
                        Layout.preferredHeight: 14
                        name: root.iconFor(modelData)
                        color: root.activePage === 0 ? Theme.accent : Theme.textFaint
                    }

                    Text {
                        Layout.fillWidth: true
                        text: modelData.label
                        color: Theme.text
                        font.pixelSize: Theme.fontBody
                        elide: Text.ElideRight
                    }

                    Text {
                        visible: root.activePage === 1
                        text: modelData.detail ?? ""
                        color: Theme.textFaint
                        font.pixelSize: Theme.fontSmall
                    }
                }

                HoverHandler { id: pointer }
                TapHandler {
                    onTapped: {
                        if (root.activePage === 0)
                            uiSession.selectEntity(row.modelData.id)
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 42
            color: Theme.surfaceRaised
            border.color: Theme.border

            RowLayout {
                anchors.fill: parent
                anchors.margins: Theme.space2

                Text {
                    visible: root.activePage === 0
                    Layout.fillWidth: true
                    text: "Local workspace"
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSmall
                }

                Row {
                    visible: root.activePage === 1
                    spacing: Theme.space1

                    Repeater {
                        model: uiSession.historyCommands

                        KButton {
                            required property var modelData
                            semanticId: "history.command." + modelData.id
                            semanticName: modelData.label
                            semanticValue: modelData.shortcut
                            iconName: modelData.icon
                            shortcut: modelData.shortcut
                            compact: true
                            quiet: true
                            onClicked: uiSession.requestCommand(modelData.id)
                        }
                    }
                }
            }
        }
    }
}
