import QtQuick
import QtQuick.Controls
import Kearne.UI

Switch {
    id: control

    property string semanticId: ""
    property string semanticName: ""
    property string semanticRole: "switch"
    property var semanticActions: ["toggle", "focus"]
    property bool semanticValue: checked

    function performSemanticAction(action, value) {
        if (action === "focus") {
            forceActiveFocus()
            return true
        }
        if (action !== "toggle")
            return false
        const requested = value === null || value === undefined ? !checked : Boolean(value)
        if (checked !== requested) {
            checked = requested
            toggled()
        }
        return true
    }

    implicitWidth: Theme.switchWidth + Theme.space1
    implicitHeight: 26
    hoverEnabled: true
    Accessible.name: semanticName
    Accessible.id: semanticId
    Accessible.role: Accessible.Switch
    Accessible.checkable: true
    Accessible.checked: checked
    Accessible.focusable: enabled && visible

    indicator: Rectangle {
        x: 0
        y: Math.round((control.height - height) / 2)
        width: Theme.switchWidth
        height: Theme.switchHeight
        radius: Theme.switchHeight / 2
        color: control.checked ? Theme.accent : Theme.surfaceMuted
        border.width: control.visualFocus ? Theme.focusRingWidth
                                          : Theme.separatorWidth
        border.color: control.visualFocus ? Theme.focus
                                           : (control.checked ? Theme.accent : Theme.borderStrong)

        Rectangle {
            x: control.checked ? parent.width - width - 3 : 3
            anchors.verticalCenter: parent.verticalCenter
            width: Theme.switchKnobSize
            height: Theme.switchKnobSize
            radius: Theme.switchKnobSize / 2
            color: Theme.surface
            border.color: Theme.border
        }
    }

    contentItem: Item { }
}
