import QtQuick
import QtQuick.Layouts
import Kearne.UI

Rectangle {
    id: root

    required property var descriptor
    required property string direction
    property string semanticId: "function.port." + descriptor.id
    property string semanticName: direction + " " + descriptor.label
    property string semanticRole: "listitem"
    property var semanticActions: []
    property string semanticValue: descriptor.type + ":" + descriptor.value + ":" + descriptor.state

    implicitHeight: 28
    radius: Theme.radiusSmall
    color: Theme.surfaceRaised
    Accessible.name: semanticName + ", " + descriptor.type + ", " + descriptor.value
    Accessible.id: semanticId
    Accessible.role: Accessible.ListItem

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.space2
        anchors.rightMargin: Theme.space2
        spacing: Theme.space2

        KIcon {
            name: root.direction === "Input" ? "chevron" : "target"
            color: root.descriptor.state === "current" ? Theme.success : Theme.textFaint
            Layout.preferredWidth: Theme.iconSizeSmall
            Layout.preferredHeight: Theme.iconSizeSmall
        }

        Text {
            Layout.preferredWidth: 72
            text: root.descriptor.label
            color: Theme.text
            font.pixelSize: Theme.fontSmall
            font.family: Theme.fontDataFamily
            elide: Text.ElideRight
        }

        Text {
            text: root.descriptor.type
            color: Theme.textMuted
            font.pixelSize: Theme.fontSmall
        }

        Item { Layout.fillWidth: true }

        Text {
            text: root.descriptor.value
            color: Theme.textFaint
            font.pixelSize: Theme.fontSmall
            elide: Text.ElideRight
        }
    }
}
