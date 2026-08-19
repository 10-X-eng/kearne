import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kearne.UI

Rectangle {
    id: root

    property string semanticId: "panel.inspector"
    property string semanticName: "Command and properties inspector"
    property string semanticRole: "panel"
    property var semanticActions: []
    property int activePage: uiSession.inspectorPage

    color: Theme.surface
    border.color: Theme.border

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.space3
            Layout.rightMargin: Theme.space3
            Layout.topMargin: Theme.space3
            Layout.bottomMargin: Theme.space3
            spacing: Theme.space2

            RowLayout {
                Layout.fillWidth: true

                Rectangle {
                    Layout.preferredWidth: 14
                    Layout.preferredHeight: 14
                    color: Theme.transparent
                    border.color: Theme.accent
                }

                Text {
                    Layout.fillWidth: true
                    text: uiSession.inspectorTitle
                    color: Theme.text
                    font.pixelSize: Theme.fontTitle
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
            }

            Text {
                Layout.fillWidth: true
                text: uiSession.inspectorStatus
                color: uiSession.backendConnected ? Theme.success : Theme.warning
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.WordWrap
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 36
            Layout.leftMargin: Theme.space2
            Layout.rightMargin: Theme.space2
            spacing: 0

            KButton {
                semanticId: "inspector.tab.properties"
                semanticName: "Properties"
                semanticRole: "tab"
                text: "Properties"
                quiet: true
                checkable: true
                checked: root.activePage === 0
                Layout.fillWidth: true
                onClicked: {
                    root.activePage = 0
                    uiSession.selectInspectorPage("properties")
                }
            }

            KButton {
                semanticId: "inspector.tab.source"
                semanticName: "Native model source"
                semanticRole: "tab"
                text: "Source"
                iconName: "code"
                quiet: true
                checkable: true
                checked: root.activePage === 1
                Layout.fillWidth: true
                onClicked: {
                    root.activePage = 1
                    uiSession.selectInspectorPage("source")
                }
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }

        ScrollView {
            visible: root.activePage === 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            ColumnLayout {
                width: parent.width
                spacing: Theme.space3

                Text {
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.space3
                    Layout.rightMargin: Theme.space3
                    Layout.topMargin: Theme.space3
                    text: uiSession.fields.length > 0 ? "COMMAND" : "PROPERTIES"
                    color: Theme.textFaint
                    font.pixelSize: Theme.fontSmall
                    font.weight: Font.DemiBold
                    font.letterSpacing: 0.8
                }

                Repeater {
                    model: uiSession.fields

                    ColumnLayout {
                        required property var modelData
                        property string semanticId: "field." + modelData.id
                        property string semanticName: modelData.label
                        property string semanticRole: "group"
                        property var semanticActions: []
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.space3
                        Layout.rightMargin: Theme.space3
                        spacing: Theme.space1

                        Text {
                            text: parent.modelData.label
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontSmall
                        }

                        Loader {
                            Layout.fillWidth: true
                            sourceComponent: parent.modelData.kind === "choice" ? choiceEditor
                                                                                  : textEditor
                            property var field: parent.modelData
                        }
                    }
                }

                RowLayout {
                    visible: uiSession.fields.length > 0
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.space3
                    Layout.rightMargin: Theme.space3
                    spacing: Theme.space2

                    KButton {
                        semanticId: "inspector.preview"
                        semanticName: "Preview command"
                        Layout.fillWidth: true
                        text: "Preview"
                        onClicked: uiSession.requestCommand(uiSession.activeCommandId + ".preview")
                    }

                    KButton {
                        semanticId: "inspector.apply"
                        semanticName: "Apply command"
                        Layout.fillWidth: true
                        text: "Apply"
                        primary: true
                        onClicked: uiSession.requestCommand(uiSession.activeCommandId + ".apply")
                    }
                }

                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.space3
                    Layout.rightMargin: Theme.space3

                    Text {
                        Layout.fillWidth: true
                        text: "PARAMETERS"
                        color: Theme.textFaint
                        font.pixelSize: Theme.fontSmall
                        font.weight: Font.DemiBold
                        font.letterSpacing: 0.8
                    }

                    KButton {
                        semanticId: "parameters.manage"
                        semanticName: "Manage parameters"
                        text: "Manage"
                        quiet: true
                        onClicked: uiSession.requestCommand("parameters.manage")
                    }
                }

                Repeater {
                    model: uiSession.parameters

                    RowLayout {
                        required property var modelData
                        property string semanticId: "parameter." + modelData.name
                        property string semanticName: modelData.name
                        property string semanticRole: "row"
                        property var semanticActions: ["edit"]
                        property string semanticValue: modelData.expression
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.space3
                        Layout.rightMargin: Theme.space3

                        Text {
                            Layout.preferredWidth: 88
                            text: parent.modelData.name
                            color: Theme.text
                            font.pixelSize: Theme.fontSmall
                            font.family: "monospace"
                            elide: Text.ElideRight
                        }

                        Text {
                            Layout.fillWidth: true
                            text: parent.modelData.expression
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontSmall
                            font.family: "monospace"
                            elide: Text.ElideRight
                        }

                        Text {
                            text: parent.modelData.value
                            color: Theme.textFaint
                            font.pixelSize: Theme.fontSmall
                        }
                    }
                }

                Item { Layout.preferredHeight: Theme.space4 }
            }
        }

        ColumnLayout {
            visible: root.activePage === 1
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.space3
                Layout.rightMargin: Theme.space3
                Layout.topMargin: Theme.space2
                Layout.bottomMargin: Theme.space2

                Text {
                    Layout.fillWidth: true
                    text: "base_plate.py"
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSmall
                    font.family: "monospace"
                }

                StateBadge {
                    label: "Native build123d"
                    state: "current"
                }
            }

            TextArea {
                property string semanticId: "inspector.source.editor"
                property string semanticName: "Native build123d source"
                property string semanticRole: "textbox"
                property var semanticActions: readOnly ? ["focus"] : ["focus", "setValue"]
                property string semanticValue: text
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: Theme.space2
                Layout.rightMargin: Theme.space2
                text: uiSession.modelSource
                readOnly: !uiSession.backendConnected
                selectByMouse: true
                wrapMode: TextEdit.NoWrap
                color: Theme.text
                selectionColor: Theme.accentSoft
                selectedTextColor: Theme.text
                font.family: "monospace"
                font.pixelSize: Theme.fontSmall
                Accessible.name: semanticName
                background: Rectangle {
                    radius: Theme.radiusSmall
                    color: Theme.surfaceRaised
                    border.color: parent.activeFocus ? Theme.focus : Theme.border
                    border.width: parent.activeFocus ? 2 : 1
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.margins: Theme.space2
                spacing: Theme.space2

                KButton {
                    semanticId: "source.diff"
                    semanticName: "Review source changes"
                    text: "Diff"
                    iconName: "compare"
                    enabled: uiSession.backendConnected
                    Layout.fillWidth: true
                }

                KButton {
                    semanticId: "source.apply"
                    semanticName: "Apply source revision"
                    text: "Apply"
                    iconName: "check"
                    primary: true
                    enabled: uiSession.backendConnected
                    Layout.fillWidth: true
                }
            }
        }

        ActivityPanel {
            Layout.fillWidth: true
            Layout.preferredHeight: 240
        }
    }

    Component {
        id: textEditor

        TextField {
            property string semanticId: "input." + field.id
            property string semanticName: field.label
            property string semanticRole: "textbox"
            property var semanticActions: readOnly ? [] : ["focus", "setValue"]
            property string semanticValue: text
            text: field.value
            readOnly: field.readOnly ?? false
            color: Theme.text
            font.pixelSize: Theme.fontBody
            selectByMouse: true
            Accessible.name: semanticName
            background: Rectangle {
                radius: Theme.radiusSmall
                color: Theme.surface
                border.color: parent.activeFocus ? Theme.focus : Theme.border
                border.width: parent.activeFocus ? 2 : 1
            }
        }
    }

    Component {
        id: choiceEditor

        KChoice {
            semanticId: "input." + field.id
            semanticName: field.label
            model: field.options
            currentIndex: Math.max(0, field.options.indexOf(field.value))
        }
    }
}
