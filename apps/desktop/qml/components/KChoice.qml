import QtQuick
import QtQuick.Controls
import Kearne.UI

ComboBox {
    id: control

    property string semanticId: ""
    property string semanticName: ""
    property string semanticRole: "combobox"
    property var semanticActions: ["choose", "focus"]
    property string semanticValue: currentText

    implicitHeight: Theme.controlHeight
    leftPadding: Theme.space3
    rightPadding: Theme.space6
    font.pixelSize: Theme.fontBody
    hoverEnabled: true
    Accessible.name: semanticName

    contentItem: Text {
        leftPadding: 0
        rightPadding: control.indicator.width + control.spacing
        text: control.displayText
        color: control.enabled ? Theme.text : Theme.textFaint
        font: control.font
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: KIcon {
        x: control.width - width - Theme.space2
        y: Math.round((control.height - height) / 2)
        width: 15
        height: 15
        name: "chevron"
        color: control.enabled ? Theme.textMuted : Theme.textFaint
    }

    background: Rectangle {
        radius: Theme.radiusSmall
        color: control.hovered ? Theme.surfaceMuted : Theme.surface
        border.width: control.visualFocus ? 2 : 1
        border.color: control.visualFocus ? Theme.focus : Theme.border
    }

    delegate: ItemDelegate {
        required property var modelData
        required property int index
        width: control.width
        height: Theme.controlHeight
        text: modelData
        highlighted: control.highlightedIndex === index
        font.pixelSize: Theme.fontBody
        contentItem: Text {
            text: parent.text
            color: Theme.text
            font: parent.font
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            color: parent.highlighted ? Theme.selection : Theme.surface
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
