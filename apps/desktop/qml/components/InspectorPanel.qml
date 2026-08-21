pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kearne.UI

Rectangle {
    id: root

    property string semanticId: "panel.inspector"
    property string semanticName: "Command and properties inspector"
    property string semanticRole: "panel"
    property var semanticActions: []
    property int activePage: App.ui.inspectorPage
    property bool closable: false
    signal closeRequested()

    function previewSourceDraft() {
        if (!sourceEditor.dirty || sourceEditor.conflict)
            return false
        if (!App.ui.submitSourceEdit(sourceEditor.text,
                                     sourceEditor.baselineRevision, true))
            return false
        sourceDiff.review(App.ui.selectedFunction.sourcePath,
                          sourceEditor.baselineText, sourceEditor.text)
        return true
    }

    function applySourceDraft() {
        if (!sourceEditor.dirty || sourceEditor.conflict)
            return false
        return App.ui.submitSourceEdit(sourceEditor.text,
                                       sourceEditor.baselineRevision, false)
    }

    color: Theme.surface
    border.color: Theme.border

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.space3
            Layout.rightMargin: Theme.space3
            Layout.topMargin: Theme.space3
            Layout.bottomMargin: Theme.space3
            spacing: Theme.space2

            RowLayout {
                Layout.fillWidth: true

                Rectangle {
                    Layout.preferredWidth: Theme.iconSizeSmall
                    Layout.preferredHeight: Theme.iconSizeSmall
                    color: Theme.transparent
                    border.color: Theme.accent
                }

                Text {
                    Layout.fillWidth: true
                    text: App.ui.inspectorTitle
                    color: Theme.text
                    font.pixelSize: Theme.fontTitle
                    font.weight: Theme.fontWeightStrong
                    elide: Text.ElideRight
                }

                KButton {
                    semanticId: visible ? "inspector.collapse" : ""
                    semanticName: "Collapse inspector panel"
                    iconName: "collapse-right"
                    quiet: true
                    compact: true
                    visible: root.closable
                    onClicked: root.closeRequested()
                }
            }

            Text {
                Layout.fillWidth: true
                text: App.ui.inspectorStatus
                color: App.ui.backendConnected ? Theme.success : Theme.warning
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.WordWrap
            }
        }

        KSeparator { }

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 36
            Layout.leftMargin: Theme.space2
            Layout.rightMargin: Theme.space2
            spacing: 0

            KButton {
                semanticId: "inspector.tab.properties"
                semanticName: "Properties"
                semanticRole: "tab"
                text: "Properties"
                quiet: true
                checkable: true
                checked: root.activePage === 0
                Layout.fillWidth: true
                onClicked: {
                    root.activePage = 0
                    App.ui.selectInspectorPage("properties")
                }
            }

            KButton {
                semanticId: "inspector.tab.source"
                semanticName: "Native model source"
                semanticRole: "tab"
                text: "Source"
                iconName: "code"
                quiet: true
                checkable: true
                checked: root.activePage === 1
                Layout.fillWidth: true
                onClicked: {
                    root.activePage = 1
                    App.ui.selectInspectorPage("source")
                }
            }
        }

        KSeparator { }

        ScrollView {
            visible: root.activePage === 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            ColumnLayout {
                width: parent.width
                spacing: Theme.space3

                Text {
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.space3
                    Layout.rightMargin: Theme.space3
                    Layout.topMargin: Theme.space3
                    text: App.ui.activeCommandId.length > 0 ? "COMMAND" : "PROPERTIES"
                    color: Theme.textFaint
                    font.pixelSize: Theme.fontSmall
                    font.weight: Theme.fontWeightStrong
                    font.letterSpacing: Theme.letterSpacing
                }

                Repeater {
                    model: App.ui.fields

                    ColumnLayout {
                        id: fieldGroup
                        required property var modelData
                        property string semanticId: "field." + fieldGroup.modelData.id
                        property string semanticName: fieldGroup.modelData.label
                        property string semanticRole: "group"
                        property var semanticActions: []
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.space3
                        Layout.rightMargin: Theme.space3
                        spacing: Theme.space1

                        Text {
                            text: fieldGroup.modelData.label
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontSmall
                        }

                        StackLayout {
                            Layout.fillWidth: true
                            currentIndex: fieldGroup.modelData.kind === "choice" ? 1
                                          : (fieldGroup.modelData.kind === "toggle" ? 2 : 0)

                            KTextField {
                                semanticId: fieldGroup.modelData.kind === "choice"
                                            || fieldGroup.modelData.kind === "toggle"
                                            ? "" : "input." + fieldGroup.modelData.id
                                semanticName: fieldGroup.modelData.label
                                text: fieldGroup.modelData.value
                                readOnly: fieldGroup.modelData.readOnly ?? false
                                onEditingFinished: App.ui.editField(
                                                       fieldGroup.modelData.id, text)
                            }

                            KChoice {
                                semanticId: fieldGroup.modelData.kind === "choice"
                                            ? "input." + fieldGroup.modelData.id : ""
                                semanticName: fieldGroup.modelData.label
                                model: fieldGroup.modelData.options
                                semanticOptions: fieldGroup.modelData.optionIds
                                currentIndex: Math.max(
                                                  0,
                                                  fieldGroup.modelData.optionIds.indexOf(
                                                      fieldGroup.modelData.value))
                                onActivated: index => App.ui.editField(
                                                 fieldGroup.modelData.id,
                                                 fieldGroup.modelData.optionIds[index])
                            }

                            KToggle {
                                semanticId: fieldGroup.modelData.kind === "toggle"
                                            ? "input." + fieldGroup.modelData.id : ""
                                semanticName: fieldGroup.modelData.label
                                checked: fieldGroup.modelData.value
                                enabled: !(fieldGroup.modelData.readOnly ?? false)
                                onToggled: App.ui.editField(fieldGroup.modelData.id,
                                                            checked)
                            }
                        }
                    }
                }

                RowLayout {
                    visible: App.ui.activeCommandId.length > 0
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.space3
                    Layout.rightMargin: Theme.space3
                    spacing: Theme.space2

                    KButton {
                        semanticId: "inspector.cancel"
                        semanticName: "Cancel command draft"
                        text: "Cancel"
                        quiet: true
                        Layout.fillWidth: true
                        onClicked: App.ui.cancelActiveCommand()
                    }

                    KButton {
                        semanticId: "inspector.preview"
                        semanticName: "Preview command"
                        visible: App.ui.commandPreviewSupported
                        enabled: App.ui.commandDraftState === "editing"
                        Layout.fillWidth: true
                        text: "Preview"
                        onClicked: App.ui.submitActiveCommand(true)
                    }

                    KButton {
                        semanticId: "inspector.apply"
                        semanticName: "Apply command"
                        visible: App.ui.commandApplySupported
                        enabled: App.ui.commandDraftState === "editing"
                                 || App.ui.commandDraftState === "preview"
                        Layout.fillWidth: true
                        text: "Apply"
                        primary: true
                        onClicked: App.ui.submitActiveCommand(false)
                    }
                }

                StateBadge {
                    visible: App.ui.activeCommandId.length > 0
                    Layout.leftMargin: Theme.space3
                    semanticId: visible ? "command.draft.state" : ""
                    semanticName: "Command draft state"
                    semanticValue: App.ui.commandDraftState
                    label: App.ui.commandDraftState
                    status: App.ui.commandDraftState === "rejected" ? "failed"
                            : (App.ui.commandDraftState === "editing" ? "current"
                               : App.ui.commandDraftState)
                }

                KSeparator { }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.space3
                    Layout.rightMargin: Theme.space3

                    Text {
                        Layout.fillWidth: true
                        text: "PARAMETERS"
                        color: Theme.textFaint
                        font.pixelSize: Theme.fontSmall
                        font.weight: Theme.fontWeightStrong
                        font.letterSpacing: Theme.letterSpacing
                    }

                    KButton {
                        semanticId: "parameters.manage"
                        semanticName: "Manage parameters"
                        text: "Manage"
                        quiet: true
                        onClicked: parameterManager.begin("")
                    }
                }

                Repeater {
                    model: App.ui.parameters

                    ItemDelegate {
                        id: parameterRow
                        required property var modelData
                        property string semanticId: "parameter." + parameterRow.modelData.id
                        property string semanticName: parameterRow.modelData.name
                        property string semanticRole: "row"
                        property var semanticActions: ["edit"]
                        property string semanticValue: parameterRow.modelData.expression

                        function editParameter() {
                            return parameterManager.begin(
                                        parameterRow.modelData.id)
                        }

                        function performSemanticAction(action, value) {
                            return action === "edit" && editParameter()
                        }

                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.space3
                        Layout.rightMargin: Theme.space3
                        hoverEnabled: true
                        Accessible.name: semanticName
                        Accessible.id: semanticId
                        Accessible.role: Accessible.ListItem
                        Accessible.focusable: enabled && visible
                        onClicked: editParameter()

                        contentItem: RowLayout {
                            Text {
                                Layout.preferredWidth: 88
                                text: parameterRow.modelData.name
                                color: Theme.text
                                font.pixelSize: Theme.fontSmall
                                font.family: Theme.fontDataFamily
                                elide: Text.ElideRight
                            }

                            Text {
                                Layout.fillWidth: true
                                text: parameterRow.modelData.expression
                                color: Theme.textMuted
                                font.pixelSize: Theme.fontSmall
                                font.family: Theme.fontDataFamily
                                elide: Text.ElideRight
                            }

                            Text {
                                text: parameterRow.modelData.value
                                color: Theme.textFaint
                                font.pixelSize: Theme.fontSmall
                            }
                        }

                        background: Rectangle {
                            radius: Theme.radiusSmall
                            color: parameterRow.hovered || parameterRow.visualFocus
                                   ? Theme.surfaceMuted : Theme.transparent
                            border.width: parameterRow.visualFocus
                                          ? Theme.focusRingWidth : 0
                            border.color: Theme.focus
                        }
                    }
                }

                Item { Layout.preferredHeight: Theme.space4 }
            }
        }

        ColumnLayout {
            visible: root.activePage === 1
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            FunctionContractPanel {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.space2
                Layout.rightMargin: Theme.space2
                Layout.topMargin: Theme.space2
                descriptor: App.ui.selectedFunction
            }

            TextArea {
                id: sourceEditor
                property string semanticId: "inspector.source.editor"
                property string semanticName: App.ui.selectedFunction.sourcePath
                                              + " native build123d source"
                property string semanticRole: "textbox"
                property var semanticActions: readOnly
                                              ? ["focus"]
                                              : ["focus", "setValue",
                                                 "prependText"]
                property string semanticValue: conflict ? "conflict"
                                                       : (dirty ? "modified" : "current")
                property string baselineText: ""
                property string baselineRevision: ""
                readonly property bool dirty: text !== baselineText
                readonly property bool conflict: dirty
                                                 && baselineRevision
                                                    !== App.ui.selectedFunction.revision

                function loadBaseline() {
                    baselineText = App.ui.modelSource
                    baselineRevision = App.ui.selectedFunction.revision
                    text = baselineText
                }

                function performSemanticAction(action, value) {
                    if (action === "focus") {
                        forceActiveFocus()
                        return true
                    }
                    if (readOnly)
                        return false
                    if (action === "setValue") {
                        text = String(value)
                        return true
                    }
                    if (action === "prependText") {
                        text = String(value) + text
                        return true
                    }
                    return false
                }

                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: Theme.space2
                Layout.rightMargin: Theme.space2
                readOnly: !App.ui.sourceEditingAvailable
                selectByMouse: true
                wrapMode: TextEdit.NoWrap
                color: Theme.text
                selectionColor: Theme.accentSoft
                selectedTextColor: Theme.text
                font.family: Theme.fontDataFamily
                font.pixelSize: Theme.fontSmall
                Accessible.name: semanticName
                Accessible.id: semanticId
                Accessible.role: Accessible.EditableText
                Accessible.editable: !readOnly
                Accessible.focusable: enabled && visible
                background: Rectangle {
                    radius: Theme.radiusSmall
                    color: Theme.surfaceRaised
                    border.color: parent.activeFocus ? Theme.focus : Theme.border
                    border.width: parent.activeFocus ? 2 : 1
                }

                Component.onCompleted: loadBaseline()

                Connections {
                    target: App.ui
                    function onProjectProjectionChanged() {
                        if (!sourceEditor.dirty
                                || sourceEditor.text === App.ui.modelSource)
                            sourceEditor.loadBaseline()
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.space2
                Layout.rightMargin: Theme.space2

                StateBadge {
                    semanticId: "source.draft.state"
                    semanticName: "Source draft state"
                    semanticValue: sourceEditor.conflict ? "conflict"
                                   : (sourceEditor.dirty ? "modified" : "current")
                    label: semanticValue
                    status: sourceEditor.conflict ? "failed"
                                                  : (sourceEditor.dirty ? "preview"
                                                                        : "current")
                }

                Text {
                    Layout.fillWidth: true
                    text: sourceEditor.conflict
                          ? "The source revision changed; preserve this draft and reload before applying."
                          : (sourceEditor.dirty ? "Uncommitted source revision"
                                                : App.ui.selectedFunction.revision)
                    color: sourceEditor.conflict ? Theme.error : Theme.textMuted
                    font.pixelSize: Theme.fontSmall
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignRight
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.margins: Theme.space2
                spacing: Theme.space2

                KButton {
                    semanticId: "source.diff"
                    semanticName: "Review source changes"
                    text: "Diff"
                    iconName: "compare"
                    enabled: App.ui.sourceEditingAvailable
                             && sourceEditor.dirty && !sourceEditor.conflict
                    Layout.fillWidth: true
                    onClicked: root.previewSourceDraft()
                }

                KButton {
                    semanticId: "source.apply"
                    semanticName: "Apply source revision"
                    text: "Apply"
                    iconName: "check"
                    primary: true
                    enabled: App.ui.sourceEditingAvailable
                             && sourceEditor.dirty && !sourceEditor.conflict
                    Layout.fillWidth: true
                    onClicked: root.applySourceDraft()
                }
            }
        }

        ActivityPanel {
            Layout.fillWidth: true
            Layout.preferredHeight: 240
        }
    }

    ParameterManagerDialog { id: parameterManager }

    SourceDiffDialog {
        id: sourceDiff
        onApplyRequested: root.applySourceDraft()
    }

}
