import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kearne.UI

Popup {
    id: root

    property string semanticId: "dialog.source_diff"
    property string semanticName: "Review source revision"
    property string semanticRole: "dialog"
    property var semanticActions: ["dismiss"]
    property string sourcePath: ""
    property string baseline: ""
    property string proposed: ""
    signal applyRequested()

    function review(path, currentSource, proposedSource) {
        sourcePath = path
        baseline = currentSource
        proposed = proposedSource
        open()
    }

    function performSemanticAction(action, value) {
        if (action !== "dismiss")
            return false
        close()
        return true
    }

    parent: Overlay.overlay
    width: Math.min(920, parent.width - Theme.space6 * 2)
    height: Math.min(620, parent.height - Theme.space6 * 2)
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
            title: "Review source revision"
            detail: root.sourcePath + " · expected revision remains unchanged until Apply"
        }

        GridLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            columns: 2
            columnSpacing: Theme.space3
            rowSpacing: Theme.space2

            Text {
                text: "CURRENT"
                color: Theme.textFaint
                font.pixelSize: Theme.fontSmall
                font.weight: Theme.fontWeightStrong
                font.letterSpacing: Theme.letterSpacing
            }

            Text {
                text: "PROPOSED"
                color: Theme.textFaint
                font.pixelSize: Theme.fontSmall
                font.weight: Theme.fontWeightStrong
                font.letterSpacing: Theme.letterSpacing
            }

            TextArea {
                property string semanticId: "source_diff.current"
                property string semanticName: "Current source"
                property string semanticRole: "textbox"
                property var semanticActions: ["focus"]
                property string semanticValue: root.sourcePath

                function performSemanticAction(action, value) {
                    if (action !== "focus")
                        return false
                    forceActiveFocus()
                    return true
                }

                Layout.fillWidth: true
                Layout.fillHeight: true
                text: root.baseline
                readOnly: true
                wrapMode: TextEdit.NoWrap
                color: Theme.textMuted
                font.family: Theme.fontDataFamily
                font.pixelSize: Theme.fontSmall
                Accessible.name: semanticName
                Accessible.id: semanticId
                Accessible.role: Accessible.EditableText
                Accessible.readOnly: true
                Accessible.focusable: enabled && visible
                background: Rectangle {
                    radius: Theme.radiusSmall
                    color: Theme.surfaceMuted
                    border.color: parent.activeFocus ? Theme.focus : Theme.border
                }
            }

            TextArea {
                property string semanticId: "source_diff.proposed"
                property string semanticName: "Proposed source"
                property string semanticRole: "textbox"
                property var semanticActions: ["focus"]
                property string semanticValue: root.sourcePath

                function performSemanticAction(action, value) {
                    if (action !== "focus")
                        return false
                    forceActiveFocus()
                    return true
                }

                Layout.fillWidth: true
                Layout.fillHeight: true
                text: root.proposed
                readOnly: true
                wrapMode: TextEdit.NoWrap
                color: Theme.text
                font.family: Theme.fontDataFamily
                font.pixelSize: Theme.fontSmall
                Accessible.name: semanticName
                Accessible.id: semanticId
                Accessible.role: Accessible.EditableText
                Accessible.readOnly: true
                Accessible.focusable: enabled && visible
                background: Rectangle {
                    radius: Theme.radiusSmall
                    color: Theme.surfaceRaised
                    border.color: parent.activeFocus ? Theme.focus : Theme.border
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true

            StateBadge {
                label: "Uncommitted"
                status: "preview"
            }

            Item { Layout.fillWidth: true }

            KButton {
                semanticId: "source_diff.cancel"
                semanticName: "Cancel source review"
                text: "Cancel"
                onClicked: root.close()
            }

            KButton {
                semanticId: "source_diff.apply"
                semanticName: "Apply proposed source"
                iconName: "check"
                text: "Apply"
                primary: true
                onClicked: {
                    root.applyRequested()
                    root.close()
                }
            }
        }
    }
}
