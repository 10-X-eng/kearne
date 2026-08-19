import QtQuick
import QtQuick.Layouts
import Kearne.UI

ColumnLayout {
    id: root

    property string title: ""
    property string detail: ""

    spacing: Theme.space1

    Text {
        Layout.fillWidth: true
        text: root.title
        color: Theme.text
        font.pixelSize: 28
        font.weight: Font.DemiBold
    }

    Text {
        visible: root.detail.length > 0
        Layout.fillWidth: true
        text: root.detail
        color: Theme.textMuted
        font.pixelSize: Theme.fontBody
        wrapMode: Text.WordWrap
    }
}
