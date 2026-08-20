pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kearne.UI
import "../components"

Rectangle {
    id: root

    property string semanticId: "surface.projects"
    property string semanticName: "Projects"
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
                width: Math.min(1080, root.width - Theme.space6 * 2)
                anchors.top: parent.top
                anchors.topMargin: Theme.space6
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: Theme.space6

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.space4

                    PageHeading {
                        Layout.fillWidth: true
                        title: "Projects"
                        detail: "Open a local project or start from one semantic model."
                    }

                    KButton {
                        semanticId: "projects.open"
                        semanticName: "Open project"
                        iconName: "folder"
                        text: "Open"
                        enabled: App.ui.backendConnected
                        onClicked: App.ui.requestCommand("project.open")
                    }

                    KButton {
                        semanticId: "projects.new"
                        semanticName: "Create project"
                        iconName: "add"
                        text: "New design"
                        primary: true
                        onClicked: App.ui.openProject(
                                       "model", "project.create.part")
                    }
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: root.width >= 920 ? 2 : 1
                    columnSpacing: Theme.space4
                    rowSpacing: Theme.space4

                    KPanel {
                        semanticId: "projects.recent"
                        title: "Recent"
                        detail: "Local project workspaces"
                        iconName: "clock"
                        Layout.fillWidth: true
                        Layout.minimumHeight: 250

                        Repeater {
                            model: App.ui.recentProjects

                            KButton {
                                required property var modelData
                                semanticId: "project." + modelData.id
                                semanticName: "Open " + modelData.name
                                semanticValue: modelData.detail
                                iconName: modelData.icon
                                text: modelData.name + "  ·  " + modelData.modified
                                quiet: true
                                Layout.fillWidth: true
                                onClicked: App.ui.openProject(
                                               modelData.workspaceId,
                                               "project.open." + modelData.id)
                            }
                        }

                        Text {
                            visible: App.ui.recentProjects.length === 0
                            Layout.fillWidth: true
                            text: "No recent projects"
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontBody
                        }
                    }

                    KPanel {
                        semanticId: "projects.local_status"
                        title: "Local-first"
                        detail: "Project ownership and compute stay explicit."
                        iconName: "shield"
                        Layout.fillWidth: true
                        Layout.minimumHeight: 250

                        RowLayout {
                            Layout.fillWidth: true
                            KIcon { name: "check"; color: Theme.success }
                            Text {
                                Layout.fillWidth: true
                                text: "Desktop UI available offline"
                                color: Theme.text
                                font.pixelSize: Theme.fontBody
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            KIcon { name: "unavailable"; color: Theme.warning }
                            Text {
                                Layout.fillWidth: true
                                text: "Engineering backend not connected"
                                color: Theme.text
                                font.pixelSize: Theme.fontBody
                            }
                        }

                        KButton {
                            semanticId: "projects.recovery"
                            semanticName: "Open recovery"
                            iconName: "recovery"
                            text: "Recovery"
                            quiet: true
                            Layout.alignment: Qt.AlignLeft
                            onClicked: App.ui.navigateTo("recovery")
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.space3

                    Text {
                        text: "START"
                        color: Theme.textFaint
                        font.pixelSize: Theme.fontSmall
                        font.weight: Theme.fontWeightStrong
                        font.letterSpacing: Theme.letterSpacing
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: root.width >= 1120 ? 4 : (root.width >= 820 ? 2 : 1)
                        columnSpacing: Theme.space3
                        rowSpacing: Theme.space3

                        Repeater {
                            model: App.ui.projectTemplates

                            KPanel {
                                id: templateCard
                                required property var modelData
                                semanticId: "template." + modelData.id
                                semanticName: modelData.name
                                title: modelData.name
                                detail: modelData.detail
                                iconName: modelData.icon
                                Layout.fillWidth: true
                                Layout.minimumHeight: 130

                                KButton {
                                    semanticId: "template." + templateCard.modelData.id + ".create"
                                    semanticName: "Create " + templateCard.modelData.name
                                    iconName: "add"
                                    text: "Create"
                                    quiet: true
                                    Layout.alignment: Qt.AlignLeft
                                    onClicked: App.ui.openProject(
                                                   templateCard.modelData.workspaceId,
                                                   "project.create." + templateCard.modelData.id)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
