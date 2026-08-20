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

    function performSemanticAction(action, value) {
        if (action === "focus") {
            forceActiveFocus()
            return true
        }
        if (action !== "setValue" || readOnly)
            return false
        text = String(value)
        editingFinished()
        return true
    }

    implicitHeight: Theme.controlHeight
    color: Theme.text
    placeholderTextColor: Theme.textFaint
    font.pixelSize: Theme.fontBody
    selectByMouse: true
    Accessible.name: semanticName
    Accessible.id: semanticId
    Accessible.role: Accessible.EditableText
    Accessible.editable: !readOnly
    Accessible.focusable: enabled && visible

    background: Rectangle {
        radius: Theme.radiusSmall
        color: Theme.surface
        border.color: control.activeFocus ? Theme.focus : Theme.border
        border.width: control.activeFocus ? Theme.focusRingWidth
                                          : Theme.separatorWidth
    }
}
