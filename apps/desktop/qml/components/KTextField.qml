import QtQuick
import QtQuick.Controls
import Kearne.UI

TextField {
    id: control

    property string semanticId: ""
    property string semanticName: placeholderText
    property string semanticRole: "textbox"
    property var semanticActions: readOnly ? ["focus"] : ["focus", "setValue"]
    property string semanticValue: readOnly ? "" : text

    implicitHeight: Theme.controlHeight
    color: Theme.text
    placeholderTextColor: Theme.textFaint
    font.pixelSize: Theme.fontBody
    selectByMouse: true
    Accessible.name: semanticName

    background: Rectangle {
        radius: Theme.radiusSmall
        color: Theme.surface
        border.color: control.activeFocus ? Theme.focus : Theme.border
        border.width: control.activeFocus ? 2 : 1
    }
}
