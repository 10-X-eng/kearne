import QtQuick
import QtQuick.Controls
import Kearne.UI

Switch {
    id: control

    property string semanticId: ""
    property string semanticName: ""
    property string semanticRole: "switch"
    property var semanticActions: ["toggle", "focus"]
    property string semanticValue: checked ? "enabled" : "disabled"

    implicitWidth: 44
    implicitHeight: 26
    hoverEnabled: true
    Accessible.name: semanticName

    indicator: Rectangle {
        x: 0
        y: Math.round((control.height - height) / 2)
        width: 40
        height: 22
        radius: 11
        color: control.checked ? Theme.accent : Theme.surfaceMuted
        border.width: control.visualFocus ? 2 : 1
        border.color: control.visualFocus ? Theme.focus
                                           : (control.checked ? Theme.accent : Theme.borderStrong)

        Rectangle {
            x: control.checked ? parent.width - width - 3 : 3
            anchors.verticalCenter: parent.verticalCenter
            width: 16
            height: 16
            radius: 8
            color: Theme.surface
            border.color: Theme.border
        }
    }

    contentItem: Item { }
}
