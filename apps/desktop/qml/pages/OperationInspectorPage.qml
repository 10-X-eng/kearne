import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kearne.UI
import "../components"

Rectangle {
    id: root

    property string semanticId: "surface.operations"
    property string semanticName: "Operation inspector"
    property string semanticRole: "main"
    property var semanticActions: []
    property string activePage: "operations"

    color: Theme.surfaceMuted

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.preferredWidth: root.width >= 980 ? 240 : 176
            Layout.fillHeight: true
            color: Theme.surface
            border.color: Theme.border

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.space3
                spacing: Theme.space1

                Text {
                    text: "INSPECT"
                    color: Theme.textFaint
                    font.pixelSize: Theme.fontSmall
                    font.weight: Font.DemiBold
                    Layout.bottomMargin: Theme.space2
                }

                Repeater {
                    model: [
                        { id: "operations", label: "Operations", icon: "operations" },
                        { id: "jobs", label: "Jobs", icon: "clock" },
                        { id: "diagnostics", label: "Diagnostics", icon: "inspect" },
                        { id: "states", label: "UI states", icon: "view" }
                    ]

                    KButton {
                        required property var modelData
                        semanticId: "operations.page." + modelData.id
                        semanticName: modelData.label
                        semanticRole: "tab"
                        iconName: modelData.icon
                        text: modelData.label
                        quiet: true
                        checkable: true
                        checked: root.activePage === modelData.id
                        Layout.fillWidth: true
                        onClicked: root.activePage = modelData.id
                    }
                }

                Item { Layout.fillHeight: true }

                KButton {
                    semanticId: "operations.return"
                    semanticName: "Return to editor"
                    iconName: "model"
                    text: "Editor"
                    quiet: true
                    Layout.fillWidth: true
                    onClicked: uiSession.navigateTo("editor")
                }
            }
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            Item {
                width: Math.max(parent.width, 560)
                implicitHeight: operationContent.implicitHeight + Theme.space6 * 2

                ColumnLayout {
                    id: operationContent
                    width: Math.min(900, parent.width - Theme.space6 * 2)
                    anchors.top: parent.top
                    anchors.topMargin: Theme.space6
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: Theme.space6

                    PageHeading {
                        Layout.fillWidth: true
                        title: root.activePage === "states" ? "Interface states"
                               : (root.activePage.charAt(0).toUpperCase() + root.activePage.slice(1))
                        detail: root.activePage === "states"
                                ? "Exercise production state components through the public command path."
                                : "Persistent operational evidence; failures do not disappear as notifications."
                    }

                    KPanel {
                        visible: root.activePage === "operations"
                        semanticId: visible ? "operations.list" : ""
                        title: "Current session"
                        iconName: "operations"
                        Layout.fillWidth: true

                        Repeater {
                            model: uiSession.operations

                            RowLayout {
                                required property var modelData
                                property string semanticId: "operation." + modelData.id
                                property string semanticName: modelData.name
                                property string semanticRole: "listitem"
                                property var semanticActions: ["inspect"]
                                property string semanticValue: modelData.state
                                Layout.fillWidth: true
                                spacing: Theme.space3

                                KIcon {
                                    name: modelData.kind === "service" ? "operations" : "view"
                                    color: modelData.state === "current" ? Theme.success : Theme.warning
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: Theme.space1
                                    Text {
                                        text: modelData.name
                                        color: Theme.text
                                        font.pixelSize: Theme.fontBody
                                        font.weight: Font.DemiBold
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData.detail
                                        color: Theme.textMuted
                                        font.pixelSize: Theme.fontSmall
                                        wrapMode: Text.WordWrap
                                    }
                                }
                                StateBadge { state: modelData.state }
                            }
                        }
                    }

                    KPanel {
                        visible: root.activePage === "jobs"
                        semanticId: visible ? "operations.jobs" : ""
                        title: "Jobs"
                        iconName: "clock"
                        Layout.fillWidth: true

                        Repeater {
                            model: uiSession.jobs
                            RowLayout {
                                required property var modelData
                                property string semanticId: "job." + modelData.id
                                property string semanticName: modelData.label
                                property string semanticRole: "listitem"
                                property var semanticActions: []
                                property string semanticValue: modelData.state
                                Layout.fillWidth: true
                                KIcon { name: "clock"; color: Theme.textMuted }
                                Text { Layout.fillWidth: true; text: modelData.label; color: Theme.text; font.pixelSize: Theme.fontBody }
                                StateBadge { state: modelData.state.toLowerCase() }
                            }
                        }
                    }

                    KPanel {
                        visible: root.activePage === "diagnostics"
                        semanticId: visible ? "operations.diagnostics" : ""
                        title: "Diagnostics"
                        iconName: "inspect"
                        Layout.fillWidth: true

                        Repeater {
                            model: uiSession.diagnostics
                            RowLayout {
                                required property var modelData
                                property string semanticId: "diagnostic." + modelData.id
                                property string semanticName: modelData.summary
                                property string semanticRole: "listitem"
                                property var semanticActions: []
                                property string semanticValue: modelData.severity
                                Layout.fillWidth: true
                                KIcon { name: "inspect"; color: Theme.accent }
                                Text {
                                    Layout.fillWidth: true
                                    text: modelData.summary
                                    color: Theme.text
                                    font.pixelSize: Theme.fontBody
                                    wrapMode: Text.WordWrap
                                }
                                StateBadge { state: modelData.severity; label: modelData.severity }
                            }
                        }
                    }

                    GridLayout {
                        visible: root.activePage === "states"
                        Layout.fillWidth: true
                        columns: root.width >= 1050 ? 3 : 2
                        columnSpacing: Theme.space3
                        rowSpacing: Theme.space3

                        Repeater {
                            model: uiSession.interfaceStates

                            KButton {
                                required property var modelData
                                semanticId: "development.state." + modelData.id
                                semanticName: "Show " + modelData.label + " state"
                                semanticValue: modelData.id
                                iconName: modelData.icon
                                text: modelData.label
                                primary: uiSession.viewportState === modelData.id
                                Layout.fillWidth: true
                                Layout.preferredHeight: 48
                                onClicked: {
                                    uiSession.requestCommand("development.state." + modelData.id)
                                    uiSession.navigateTo("editor")
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
