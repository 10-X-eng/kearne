import QtQuick
import QtQuick.Controls
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
            text: root.descriptor.detail ?? ""
            color: Theme.textMuted
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.WordWrap
        }
    }

    Loader {
        Layout.preferredWidth: root.descriptor.kind === "toggle" ? 52 : 190
        sourceComponent: root.descriptor.kind === "toggle" ? toggleEditor
                         : (root.descriptor.kind === "text" ? textEditor : choiceEditor)
        property var setting: root.descriptor
    }

    Component {
        id: toggleEditor
        KToggle {
            semanticId: root.semanticId + ".control"
            semanticName: root.semanticName
            checked: setting.value
            onToggled: root.valueEdited(setting.id, checked)
        }
    }

    Component {
        id: choiceEditor
        KChoice {
            semanticId: root.semanticId + ".control"
            semanticName: root.semanticName
            model: setting.options
            currentIndex: Math.max(0, setting.options.indexOf(setting.value))
            onActivated: index => root.valueEdited(
                             setting.id,
                             setting.optionIds ? setting.optionIds[index] : setting.options[index])
        }
    }

    Component {
        id: textEditor
        TextField {
            property string semanticId: root.semanticId + ".control"
            property string semanticName: root.semanticName
            property string semanticRole: "textbox"
            property var semanticActions: ["setValue", "focus"]
            property string semanticValue: text
            text: setting.value
            selectByMouse: true
            Accessible.name: semanticName
            onEditingFinished: root.valueEdited(setting.id, text)
        }
    }
}
