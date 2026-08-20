pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Kearne.UI

RowLayout {
    id: root

    required property var descriptor
    property string semanticId: "setting." + descriptor.id
    property string semanticName: descriptor.label
    property string semanticRole: "group"
    property var semanticActions: []
    signal valueEdited(string settingId, var value)

    enabled: descriptor.enabled
    opacity: enabled ? 1 : Theme.disabledOpacity
    Layout.fillWidth: true
    Layout.minimumHeight: 52
    spacing: Theme.space4

    ColumnLayout {
        Layout.fillWidth: true
        spacing: Theme.space1

        Text {
            Layout.fillWidth: true
            text: root.descriptor.label
            color: Theme.text
            font.pixelSize: Theme.fontBody
        }

        Text {
            visible: (root.descriptor.detail ?? "").length > 0
            Layout.fillWidth: true
            text: (root.descriptor.detail ?? "")
                  + (root.enabled ? "" : " · Unavailable")
            color: Theme.textMuted
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.WordWrap
        }
    }

    Loader {
        Layout.preferredWidth: root.descriptor.kind === "toggle" ? 52 : 190
        sourceComponent: root.descriptor.kind === "toggle" ? toggleEditor
                         : (root.descriptor.kind === "text" ? textEditor : choiceEditor)
    }

    Component {
        id: toggleEditor
        KToggle {
            semanticId: root.semanticId + ".control"
            semanticName: root.semanticName
            checked: root.descriptor.value
            onToggled: root.valueEdited(root.descriptor.id, checked)
        }
    }

    Component {
        id: choiceEditor
        KChoice {
            semanticId: root.semanticId + ".control"
            semanticName: root.semanticName
            model: root.descriptor.options
            semanticOptions: root.descriptor.optionIds
            currentIndex: Math.max(0, root.descriptor.optionIds.indexOf(root.descriptor.value))
            onActivated: index => root.valueEdited(
                             root.descriptor.id,
                             root.descriptor.optionIds ? root.descriptor.optionIds[index]
                                                       : root.descriptor.options[index])
        }
    }

    Component {
        id: textEditor
        KTextField {
            semanticId: root.semanticId + ".control"
            semanticName: root.semanticName
            text: root.descriptor.value
            onEditingFinished: root.valueEdited(root.descriptor.id, text)
        }
    }
}
