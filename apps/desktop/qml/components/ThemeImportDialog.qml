import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kearne.UI

Popup {
    id: root

    property string semanticId: "dialog.theme_import"
    property string semanticName: "Import Kearne theme"
    property string semanticRole: "dialog"
    property var semanticActions: ["dismiss"]

    function begin() {
        sourcePath.text = ""
        App.themes.clearError()
        open()
        sourcePath.forceActiveFocus()
    }

    function importSelected() {
        if (!App.themes.importThemePath(sourcePath.text))
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
    width: Math.min(560, parent.width - Theme.space6 * 2)
    implicitHeight: content.implicitHeight + Theme.space6 * 2
    height: implicitHeight
    x: Math.round((parent.width - width) / 2)
    y: Math.max(Theme.space6, Math.round((parent.height - height) / 2))
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    padding: Theme.space6

    background: Rectangle {
        radius: Theme.radius
        color: Theme.surface
        border.color: Theme.borderStrong
        border.width: Theme.separatorWidth
    }

    contentItem: ColumnLayout {
        id: content
        spacing: Theme.space4

        PageHeading {
            Layout.fillWidth: true
            title: "Import YAML theme"
            detail: "Enter an absolute local path. The file is validated and copied into your Kearne profile."
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.space1

            Text {
                text: "THEME FILE"
                color: Theme.textFaint
                font.pixelSize: Theme.fontSmall
                font.weight: Theme.fontWeightStrong
                font.letterSpacing: Theme.letterSpacing
            }

            KTextField {
                id: sourcePath
                semanticId: "theme_import.path"
                semanticName: "Theme file path"
                placeholderText: "/absolute/path/theme.yml"
                Layout.fillWidth: true
                onAccepted: root.importSelected()
            }
        }

        Text {
            visible: App.themes.lastError.length > 0
            Layout.fillWidth: true
            text: App.themes.lastError
            color: Theme.error
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.WordWrap
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space2

            Item { Layout.fillWidth: true }

            KButton {
                semanticId: "theme_import.cancel"
                semanticName: "Cancel theme import"
                text: "Cancel"
                onClicked: root.close()
            }

            KButton {
                semanticId: "theme_import.apply"
                semanticName: "Import selected theme"
                iconName: "add"
                text: "Import"
                primary: true
                enabled: sourcePath.text.trim().length > 0
                onClicked: root.importSelected()
            }
        }
    }
}
