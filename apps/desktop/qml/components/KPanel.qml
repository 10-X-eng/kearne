import QtQuick
import QtQuick.Layouts
import Kearne.UI

Rectangle {
    id: root

    default property alias contentData: content.data
    property string semanticId: ""
    property string semanticName: title
    property string semanticRole: "group"
    property var semanticActions: []
    property string title: ""
    property string detail: ""
    property string iconName: ""

    implicitHeight: layout.implicitHeight + Theme.space4 * 2
    radius: Theme.radius
    color: Theme.surface
    border.color: Theme.border

    ColumnLayout {
        id: layout
        anchors.fill: parent
        anchors.margins: Theme.space4
        spacing: Theme.space3

        RowLayout {
            visible: root.title.length > 0 || root.iconName.length > 0
            Layout.fillWidth: true
            spacing: Theme.space3

            KIcon {
                visible: root.iconName.length > 0
                name: root.iconName
                color: Theme.accent
                Layout.preferredWidth: Theme.iconSizeLarge
                Layout.preferredHeight: Theme.iconSizeLarge
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.space1

                Text {
                    Layout.fillWidth: true
                    text: root.title
                    color: Theme.text
                    font.pixelSize: Theme.fontTitle
                    font.weight: Theme.fontWeightStrong
                    elide: Text.ElideRight
                }

                Text {
                    visible: root.detail.length > 0
                    Layout.fillWidth: true
                    text: root.detail
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                }
            }
        }

        ColumnLayout {
            id: content
            Layout.fillWidth: true
            spacing: Theme.space2
        }
    }
}
