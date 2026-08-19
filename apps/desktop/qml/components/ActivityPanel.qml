import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kearne.UI

Rectangle {
    id: root

    property string semanticId: "panel.activity"
    property string semanticName: "Agent jobs and diagnostics"
    property string semanticRole: "panel"
    property var semanticActions: []
    property int activePage: 0
    readonly property var pages: [
        { id: "agent", label: "Agent", icon: "agent" },
        { id: "jobs", label: "Jobs", icon: "clock" },
        { id: "diagnostics", label: "Diagnostics", icon: "inspect" }
    ]

    color: Theme.surface
    border.color: Theme.border

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 36
            Layout.leftMargin: Theme.space3
            Layout.rightMargin: Theme.space3

            KIcon {
                name: root.pages[root.activePage].icon
                color: Theme.accent
                Layout.preferredWidth: 16
                Layout.preferredHeight: 16
            }

            Text {
                Layout.fillWidth: true
                text: root.pages[root.activePage].label
                color: Theme.text
                font.pixelSize: Theme.fontBody
                font.weight: Font.DemiBold
            }

            Text {
                visible: root.activePage === 0
                text: uiSession.agentStatus
                color: Theme.textFaint
                font.pixelSize: Theme.fontSmall
                elide: Text.ElideRight
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }

        ListView {
            id: activityList
            property string semanticId: "activity.items"
            property string semanticName: root.pages[root.activePage].label + " items"
            property string semanticRole: "list"
            property var semanticActions: []

            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: Theme.space2
            clip: true
            model: root.activePage === 0 ? uiSession.proposals
                                         : (root.activePage === 1 ? uiSession.jobs
                                                                  : uiSession.diagnostics)
            spacing: Theme.space2

            delegate: Rectangle {
                required property var modelData
                property string semanticId: "activity." + modelData.id
                property string semanticName: modelData.summary ?? modelData.label
                property string semanticRole: "listitem"
                property var semanticActions: []
                property string semanticValue: modelData.state ?? modelData.severity

                width: activityList.width
                height: Math.max(52, activityText.implicitHeight + Theme.space4)
                radius: Theme.radiusSmall
                color: Theme.surfaceRaised
                border.color: Theme.border

                Text {
                    id: activityText
                    anchors.fill: parent
                    anchors.margins: Theme.space2
                    text: modelData.summary ?? modelData.label
                    color: Theme.text
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }

        RowLayout {
            visible: root.activePage === 0
            Layout.fillWidth: true
            Layout.leftMargin: Theme.space2
            Layout.rightMargin: Theme.space2
            Layout.bottomMargin: Theme.space2
            spacing: Theme.space1

            KTextField {
                semanticId: "agent.prompt"
                semanticName: "Ask Kearne"
                placeholderText: "Ask Kearne"
                enabled: false
                Layout.fillWidth: true
            }

            KButton {
                semanticId: "agent.send"
                semanticName: "Send to Kearne"
                iconName: "agent"
                enabled: false
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 36
            Layout.leftMargin: Theme.space2
            Layout.rightMargin: Theme.space2
            spacing: 0

            Repeater {
                model: root.pages

                KButton {
                    required property var modelData
                    required property int index
                    semanticId: "activity.tab." + modelData.id
                    semanticName: modelData.label
                    semanticRole: "tab"
                    iconName: modelData.icon
                    text: modelData.label
                    compact: true
                    quiet: true
                    checkable: true
                    checked: root.activePage === index
                    Layout.fillWidth: true
                    onClicked: root.activePage = index
                }
            }
        }
    }
}
