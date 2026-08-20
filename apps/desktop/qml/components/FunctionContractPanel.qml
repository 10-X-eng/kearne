pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Kearne.UI

KPanel {
    id: root

    required property var descriptor
    property bool expanded: false

    semanticId: "function.contract"
    semanticName: descriptor.name + " function contract"
    title: descriptor.name
    detail: descriptor.sourcePath + " · " + descriptor.revision
    iconName: "code"

    Text {
        Layout.fillWidth: true
        text: root.descriptor.signature
        color: Theme.text
        font.pixelSize: Theme.fontSmall
        font.family: Theme.fontDataFamily
        wrapMode: Text.WrapAnywhere
    }

    RowLayout {
        Layout.fillWidth: true

        StateBadge {
            label: root.descriptor.recognition
            status: root.descriptor.recognition
        }

        Text {
            Layout.fillWidth: true
            text: root.descriptor.inputs.length + " inputs · "
                  + root.descriptor.outputs.length + " outputs"
            color: Theme.textMuted
            font.pixelSize: Theme.fontSmall
            horizontalAlignment: Text.AlignRight
        }

        KButton {
            semanticId: "function.contract.toggle"
            semanticName: root.expanded ? "Hide function ports" : "Show function ports"
            semanticValue: root.expanded ? "expanded" : "collapsed"
            iconName: root.expanded ? "chevron-up" : "add"
            text: root.expanded ? "Hide" : "Ports"
            quiet: true
            compact: true
            onClicked: root.expanded = !root.expanded
        }
    }

    ColumnLayout {
        visible: root.expanded
        Layout.fillWidth: true
        spacing: Theme.space1

        Text {
            text: "INPUTS"
            color: Theme.textFaint
            font.pixelSize: Theme.fontSmall
            font.weight: Theme.fontWeightStrong
            font.letterSpacing: Theme.letterSpacing
        }

        Repeater {
            model: root.descriptor.inputs
            FunctionPortRow {
                required property var modelData
                Layout.fillWidth: true
                descriptor: modelData
                direction: "Input"
            }
        }

        Text {
            text: "OUTPUTS"
            color: Theme.textFaint
            font.pixelSize: Theme.fontSmall
            font.weight: Theme.fontWeightStrong
            font.letterSpacing: Theme.letterSpacing
            Layout.topMargin: Theme.space1
        }

        Repeater {
            model: root.descriptor.outputs
            FunctionPortRow {
                required property var modelData
                Layout.fillWidth: true
                descriptor: modelData
                direction: "Output"
            }
        }
    }
}
