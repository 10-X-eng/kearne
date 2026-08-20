pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import Kearne.UI

ComboBox {
    id: control

    property string semanticId: ""
    property string semanticName: ""
    property string semanticRole: "combobox"
    property var semanticActions: ["choose", "focus"]
    property var semanticOptions: model
    property string iconName: ""
    property string semanticValue: currentIndex >= 0
                                   && currentIndex < semanticOptions.length
                                   ? String(semanticOptions[currentIndex]) : ""

    function performSemanticAction(action, value) {
        if (action === "focus") {
            forceActiveFocus()
            return true
        }
        if (action !== "choose")
            return false
        const index = semanticOptions.indexOf(value)
        if (index < 0)
            return false
        currentIndex = index
        activated(index)
        return true
    }

    implicitHeight: Theme.controlHeight
    leftPadding: Theme.space3
    rightPadding: Theme.space6
    font.pixelSize: Theme.fontBody
    hoverEnabled: true
    Accessible.name: semanticName
    Accessible.id: semanticId
    Accessible.role: Accessible.ComboBox
    Accessible.focusable: enabled && visible

    contentItem: Row {
        spacing: Theme.space2

        KIcon {
            visible: control.iconName.length > 0
            width: visible ? Theme.iconSize : 0
            height: Theme.iconSize
            anchors.verticalCenter: parent.verticalCenter
            name: control.iconName
            color: control.enabled ? Theme.textMuted : Theme.textFaint
        }

        Text {
            width: parent.width - x
            height: parent.height
            rightPadding: control.indicator.width + control.spacing
            text: control.displayText
            color: control.enabled ? Theme.text : Theme.textFaint
            font: control.font
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }

    indicator: KIcon {
        x: control.width - width - Theme.space2
        y: Math.round((control.height - height) / 2)
        width: Theme.iconSizeSmall
        height: Theme.iconSizeSmall
        name: "chevron"
        color: control.enabled ? Theme.textMuted : Theme.textFaint
    }

    background: Rectangle {
        radius: Theme.radiusSmall
        color: control.hovered ? Theme.surfaceMuted : Theme.surface
        border.width: control.visualFocus ? Theme.focusRingWidth
                                          : Theme.separatorWidth
        border.color: control.visualFocus ? Theme.focus : Theme.border
    }

    delegate: ItemDelegate {
        id: delegateControl
        required property var modelData
        required property int index
        width: control.width
        height: Theme.controlHeight
        text: String(delegateControl.modelData)
        highlighted: control.highlightedIndex === delegateControl.index
        font.pixelSize: Theme.fontBody
        contentItem: Text {
            text: delegateControl.text
            color: Theme.text
            font: delegateControl.font
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            color: delegateControl.highlighted ? Theme.selection : Theme.surface
        }
    }

    popup: Popup {
        y: control.height + Theme.space1
        width: control.width
        implicitHeight: Math.min(contentItem.implicitHeight + Theme.space1 * 2, 280)
        padding: Theme.space1

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator { }
        }

        background: Rectangle {
            radius: Theme.radiusSmall
            color: Theme.surface
            border.color: Theme.borderStrong
        }
    }
}
