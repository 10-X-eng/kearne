pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kearne.UI
import "../components"

Rectangle {
    id: root

    property string semanticId: "surface.recovery"
    property string semanticName: "Recovery"
    property string semanticRole: "main"
    property var semanticActions: []

    color: Theme.surfaceMuted

    ScrollView {
        anchors.fill: parent
        clip: true

        Item {
            width: root.width
            implicitHeight: content.implicitHeight + Theme.space6 * 2

            ColumnLayout {
                id: content
                width: Math.min(920, root.width - Theme.space6 * 2)
                anchors.top: parent.top
                anchors.topMargin: Theme.space6
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: Theme.space6

                RowLayout {
                    Layout.fillWidth: true

                    PageHeading {
                        Layout.fillWidth: true
                        title: "Recovery"
                        detail: "Interrupted writes are inspected without replacing the original project."
                    }

                    KButton {
                        semanticId: "recovery.back"
                        semanticName: "Back to projects"
                        iconName: "home"
                        text: "Projects"
                        onClicked: App.ui.navigateTo("projects")
                    }
                }

                KPanel {
                    semanticId: "recovery.candidates"
                    title: "Recovery candidates"
                    detail: "Journal and project evidence"
                    iconName: "recovery"
                    Layout.fillWidth: true

                    Repeater {
                        model: App.ui.recoveryItems

                        RowLayout {
                            id: recoveryRow
                            required property var modelData
                            property string semanticId: "recovery." + recoveryRow.modelData.id
                            property string semanticName: recoveryRow.modelData.name
                            property string semanticRole: "listitem"
                            property var semanticActions: recoveryRow.modelData.available
                                                          ? ["inspect"] : []
                            property string semanticValue: recoveryRow.modelData.state

                            Accessible.name: semanticName
                            Accessible.id: semanticId
                            Accessible.role: Accessible.ListItem

                            function inspectRecovery() {
                                if (!recoveryRow.modelData.available)
                                    return false
                                App.ui.requestCommand(
                                            "recovery.inspect." + recoveryRow.modelData.id)
                                return true
                            }

                            function performSemanticAction(action, value) {
                                return action === "inspect" && inspectRecovery()
                            }
                            Layout.fillWidth: true
                            spacing: Theme.space3

                            KIcon {
                                name: recoveryRow.modelData.available ? "recovery" : "check"
                                color: recoveryRow.modelData.available ? Theme.warning : Theme.success
                                Layout.preferredWidth: 24
                                Layout.preferredHeight: 24
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: Theme.space1

                                Text {
                                    Layout.fillWidth: true
                                    text: recoveryRow.modelData.name
                                    color: Theme.text
                                    font.pixelSize: Theme.fontBody
                                    font.weight: Theme.fontWeightStrong
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: recoveryRow.modelData.detail
                                    color: Theme.textMuted
                                    font.pixelSize: Theme.fontSmall
                                    wrapMode: Text.WordWrap
                                }
                            }

                            StateBadge { status: recoveryRow.modelData.state }

                            KButton {
                                semanticId: "recovery." + recoveryRow.modelData.id + ".inspect"
                                semanticName: "Inspect " + recoveryRow.modelData.name
                                iconName: "inspect"
                                text: "Inspect"
                                visible: recoveryRow.modelData.available
                                enabled: recoveryRow.modelData.available
                                onClicked: recoveryRow.inspectRecovery()
                            }
                        }
                    }
                }

                KPanel {
                    semanticId: "recovery.policy"
                    title: "Safe recovery"
                    detail: "Kearne preserves source evidence before repair."
                    iconName: "shield"
                    Layout.fillWidth: true

                    Repeater {
                        model: [
                            "Validate the durable project and journal independently",
                            "Open a recovered copy; never overwrite the only source",
                            "Keep unsupported records opaque for later inspection"
                        ]

                        RowLayout {
                            id: policyRow
                            required property string modelData
                            Layout.fillWidth: true
                            KIcon { name: "check"; color: Theme.success }
                            Text {
                                Layout.fillWidth: true
                                text: policyRow.modelData
                                color: Theme.text
                                font.pixelSize: Theme.fontBody
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                }
            }
        }
    }
}
