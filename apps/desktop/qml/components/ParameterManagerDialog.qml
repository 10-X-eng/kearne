pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kearne.UI

Popup {
    id: root

    property string semanticId: "dialog.parameters"
    property string semanticName: "Manage parameters"
    property string semanticRole: "dialog"
    property var semanticActions: ["dismiss"]
    property string semanticValue: activeParameterId
    property string activeParameterId: ""

    function parameterById(parameterId) {
        for (const parameter of App.ui.parameters) {
            if (parameter.id === parameterId)
                return parameter
        }
        return null
    }

    function selectParameter(parameterId) {
        const parameter = parameterById(parameterId)
        if (parameter === null)
            return false
        activeParameterId = parameter.id
        expressionEditor.text = parameter.expression
        return true
    }

    function begin(parameterId) {
        const selected = parameterId.length > 0
                         ? parameterId
                         : (App.ui.parameters.length > 0
                            ? App.ui.parameters[0].id : "")
        if (!selectParameter(selected))
            return false
        open()
        expressionEditor.forceActiveFocus()
        return true
    }

    function applyEdit() {
        if (!App.ui.submitParameterEdit(activeParameterId,
                                        expressionEditor.text))
            return false
        close()
        return true
    }

    function performSemanticAction(action, value) {
        if (action !== "dismiss")
            return false
        close()
        return true
    }

    parent: Overlay.overlay
    width: Math.min(720, parent.width - Theme.space6 * 2)
    height: Math.min(470, parent.height - Theme.space6 * 2)
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    padding: Theme.space4

    background: Rectangle {
        radius: Theme.radius
        color: Theme.surface
        border.color: Theme.borderStrong
        border.width: Theme.separatorWidth
    }

    contentItem: ColumnLayout {
        spacing: Theme.space3

        PageHeading {
            Layout.fillWidth: true
            title: "Parameters"
            detail: "Named expressions consumed by the selected model function."
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.space3

            KPanel {
                semanticId: "parameter_manager.list"
                title: "Model inputs"
                iconName: "measure"
                Layout.preferredWidth: 250
                Layout.fillHeight: true

                Repeater {
                    model: App.ui.parameters

                    KButton {
                        required property var modelData
                        semanticId: "parameter_manager.item." + modelData.id
                        semanticName: "Edit " + modelData.name
                        semanticValue: modelData.expression
                        iconName: "measure"
                        text: modelData.name
                        quiet: true
                        checkable: true
                        checked: root.activeParameterId === modelData.id
                        Layout.fillWidth: true
                        onClicked: root.selectParameter(modelData.id)
                    }
                }
            }

            KPanel {
                semanticId: "parameter_manager.editor"
                title: root.parameterById(root.activeParameterId)?.name ?? "Parameter"
                detail: "Expression syntax and dimensional validity are owned by the engineering service."
                iconName: "code"
                Layout.fillWidth: true
                Layout.fillHeight: true

                Text {
                    text: "EXPRESSION"
                    color: Theme.textFaint
                    font.pixelSize: Theme.fontSmall
                    font.weight: Theme.fontWeightStrong
                    font.letterSpacing: Theme.letterSpacing
                }

                KTextField {
                    id: expressionEditor
                    semanticId: "parameter_manager.expression"
                    semanticName: "Parameter expression"
                    Layout.fillWidth: true
                }

                RowLayout {
                    Layout.fillWidth: true

                    Text {
                        Layout.fillWidth: true
                        text: "Effective value"
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSmall
                    }

                    Text {
                        text: root.parameterById(root.activeParameterId)?.value
                              ?? "unavailable"
                        color: Theme.textFaint
                        font.pixelSize: Theme.fontSmall
                    }
                }

                Item { Layout.fillHeight: true }

                StateBadge {
                    semanticId: "parameter_manager.state"
                    semanticName: "Parameter evaluation state"
                    label: "Evaluator unavailable"
                    status: "unavailable"
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true

            Item { Layout.fillWidth: true }

            KButton {
                semanticId: "parameter_manager.cancel"
                semanticName: "Cancel parameter edit"
                text: "Cancel"
                onClicked: root.close()
            }

            KButton {
                semanticId: "parameter_manager.apply"
                semanticName: "Apply parameter expression"
                iconName: "check"
                text: "Apply"
                primary: true
                enabled: expressionEditor.text.trim().length > 0
                onClicked: root.applyEdit()
            }
        }
    }
}
