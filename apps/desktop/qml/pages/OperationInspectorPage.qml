pragma ComponentBehavior: Bound

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
    property var selectedOperation: null

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
                    font.weight: Theme.fontWeightStrong
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
                    onClicked: App.ui.navigateTo("editor")
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
                            model: App.ui.operations

                            ItemDelegate {
                                id: operationRow
                                required property var modelData
                                property string semanticId: "operation." + operationRow.modelData.id
                                property string semanticName: operationRow.modelData.name
                                property string semanticRole: "listitem"
                                property var semanticActions: ["inspect"]
                                property string semanticValue: operationRow.modelData.state

                                function inspectOperation() {
                                    root.selectedOperation = operationRow.modelData
                                    return true
                                }

                                function performSemanticAction(action, value) {
                                    return action === "inspect" && inspectOperation()
                                }

                                Layout.fillWidth: true
                                hoverEnabled: true
                                Accessible.name: semanticName
                                Accessible.id: semanticId
                                Accessible.role: Accessible.ListItem
                                Accessible.focusable: enabled && visible
                                onClicked: inspectOperation()

                                contentItem: RowLayout {
                                    spacing: Theme.space3

                                    KIcon {
                                        name: operationRow.modelData.kind === "service"
                                              ? "operations" : "view"
                                        color: operationRow.modelData.state === "current"
                                               ? Theme.success : Theme.warning
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: operationRow.modelData.name
                                        color: Theme.text
                                        font.pixelSize: Theme.fontBody
                                        font.weight: Theme.fontWeightStrong
                                    }

                                    StateBadge { status: operationRow.modelData.state }
                                }

                                background: Rectangle {
                                    radius: Theme.radiusSmall
                                    color: operationRow.hovered || operationRow.visualFocus
                                           ? Theme.surfaceMuted : Theme.transparent
                                    border.width: operationRow.visualFocus
                                                  ? Theme.focusRingWidth : 0
                                    border.color: Theme.focus
                                }
                            }
                        }

                        KSeparator { visible: root.selectedOperation !== null }

                        ColumnLayout {
                            visible: root.selectedOperation !== null
                            property string semanticId: visible
                                                        ? "operation.detail" : ""
                            property string semanticName: "Selected operation detail"
                            property string semanticRole: "group"
                            property var semanticActions: []
                            property string semanticValue: visible
                                                           ? root.selectedOperation.id : ""
                            Layout.fillWidth: true
                            spacing: Theme.space2

                            RowLayout {
                                Layout.fillWidth: true

                                Text {
                                    Layout.fillWidth: true
                                    text: root.selectedOperation !== null
                                          ? root.selectedOperation.name : ""
                                    color: Theme.text
                                    font.pixelSize: Theme.fontTitle
                                    font.weight: Theme.fontWeightStrong
                                }

                                StateBadge {
                                    status: root.selectedOperation !== null
                                            ? root.selectedOperation.state
                                            : "unavailable"
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                text: root.selectedOperation !== null
                                      ? root.selectedOperation.detail : ""
                                color: Theme.textMuted
                                font.pixelSize: Theme.fontBody
                                wrapMode: Text.WordWrap
                            }

                            Text {
                                Layout.fillWidth: true
                                text: root.selectedOperation !== null
                                      ? root.selectedOperation.id
                                        + (root.selectedOperation.progress >= 0
                                           ? " · " + root.selectedOperation.progress + "%"
                                           : " · progress unavailable")
                                      : ""
                                color: Theme.textFaint
                                font.pixelSize: Theme.fontSmall
                                font.family: Theme.fontDataFamily
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
                            model: App.ui.jobs
                            RowLayout {
                                id: jobRow
                                required property var modelData
                                property string semanticId: "job." + jobRow.modelData.id
                                property string semanticName: jobRow.modelData.label
                                property string semanticRole: "listitem"
                                property var semanticActions: []
                                property string semanticValue: jobRow.modelData.state
                                Layout.fillWidth: true
                                KIcon { name: "clock"; color: Theme.textMuted }
                                Text { Layout.fillWidth: true; text: jobRow.modelData.label; color: Theme.text; font.pixelSize: Theme.fontBody }
                                StateBadge { status: jobRow.modelData.state.toLowerCase() }
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
                            model: App.ui.diagnostics
                            RowLayout {
                                id: diagnosticRow
                                required property var modelData
                                property string semanticId: "diagnostic." + diagnosticRow.modelData.id
                                property string semanticName: diagnosticRow.modelData.summary
                                property string semanticRole: "listitem"
                                property var semanticActions: []
                                property string semanticValue: diagnosticRow.modelData.severity
                                Layout.fillWidth: true
                                KIcon { name: "inspect"; color: Theme.accent }
                                Text {
                                    Layout.fillWidth: true
                                    text: diagnosticRow.modelData.summary
                                    color: Theme.text
                                    font.pixelSize: Theme.fontBody
                                    wrapMode: Text.WordWrap
                                }
                                StateBadge {
                                    status: diagnosticRow.modelData.severity
                                    label: diagnosticRow.modelData.severity
                                }
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
                            model: App.ui.interfaceStates

                            KButton {
                                required property var modelData
                                semanticId: "development.state." + modelData.id
                                semanticName: "Show " + modelData.label + " state"
                                semanticValue: modelData.id
                                iconName: modelData.icon
                                text: modelData.label
                                primary: App.ui.viewportState === modelData.id
                                Layout.fillWidth: true
                                Layout.preferredHeight: 48
                                onClicked: {
                                    App.ui.requestCommand("development.state." + modelData.id)
                                    App.ui.navigateTo("editor")
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
