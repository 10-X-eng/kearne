import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kearne.UI

Button {
    id: control

    property string semanticId: ""
    property string semanticName: text
    property string semanticRole: "button"
    property var semanticActions: ["invoke"]
    property string semanticValue: checked ? "selected" : ""
    property bool primary: false
    property bool quiet: false
    property bool selected: checked
    property string iconName: ""
    property bool iconAbove: false
    property int iconSize: iconAbove ? Theme.iconSizeLarge : Theme.iconSize
    property string shortcut: ""
    property bool compact: false
    property int textPixelSize: compact ? Theme.fontSmall : Theme.fontBody

    function performSemanticAction(action, value) {
        if (action !== "invoke")
            return false
        click()
        return true
    }

    implicitHeight: compact ? Theme.controlHeightCompact : Theme.controlHeight
    implicitWidth: Math.max(iconAbove ? 52 : 36,
                            contentItem.implicitWidth + leftPadding + rightPadding)
    hoverEnabled: true
    padding: compact ? Theme.space1 : Theme.space2
    leftPadding: compact ? Theme.space2 : Theme.space3
    rightPadding: compact ? Theme.space2 : Theme.space3

    Accessible.name: semanticName
    Accessible.id: semanticId
    Accessible.role: semanticRole === "tab" ? Accessible.PageTab : Accessible.Button
    Accessible.checkable: checkable
    Accessible.checked: checked
    Accessible.focusable: enabled && visible

    contentItem: GridLayout {
        columns: control.iconAbove ? 1 : 2
        columnSpacing: Theme.space2
        rowSpacing: Theme.space1

        KIcon {
            visible: control.iconName.length > 0
            Layout.alignment: Qt.AlignCenter
            Layout.preferredWidth: control.iconSize
            Layout.preferredHeight: control.iconSize
            name: control.iconName
            color: control.primary ? Theme.surface
                                   : (control.enabled
                                      ? (control.selected ? Theme.accent : Theme.textMuted)
                                      : Theme.textFaint)
            accentColor: control.primary || control.selected ? Theme.surface : Theme.accent
            softColor: control.primary ? Theme.accentHover
                                       : (control.selected ? Theme.accent : Theme.accentSoft)
        }

        Text {
            visible: control.text.length > 0
            Layout.alignment: Qt.AlignCenter
            text: control.text
            color: control.primary ? Theme.surface : Theme.text
            font.pixelSize: control.textPixelSize
            font.weight: control.primary || control.selected
                         ? Theme.fontWeightStrong : Theme.fontWeightNormal
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }

    ToolTip.visible: hovered && semanticName.length > 0
    ToolTip.delay: 700
    ToolTip.text: semanticName + (shortcut.length > 0 ? "  " + shortcut : "")

    background: Rectangle {
        radius: Theme.radiusSmall
        color: {
            if (!control.enabled)
                return Theme.surfaceMuted
            if (control.primary)
                return control.down ? Theme.accentHover : Theme.accent
            if (control.selected)
                return Theme.selection
            if (control.hovered || control.visualFocus)
                return Theme.surfaceMuted
            return control.quiet ? Theme.transparent : Theme.surface
        }
        border.width: control.visualFocus ? Theme.focusRingWidth
                                         : (control.quiet && !control.selected
                                            ? 0 : Theme.separatorWidth)
        border.color: control.visualFocus ? Theme.focus : Theme.border
    }
}
