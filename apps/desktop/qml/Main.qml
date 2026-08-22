import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kearne.UI

ApplicationWindow {
    id: root

    objectName: semanticId
    property string semanticId: "window.main"
    property string semanticName: "Kearne — " + App.ui.projectName
    property string semanticRole: "window"
    property var semanticActions: ["focus", "resize"]
    property string semanticValue: App.ui.activeSurfaceId + ":" + App.ui.activeWorkspaceId

    function performSemanticAction(action, value) {
        if (action === "focus") {
            requestActivate()
            return true
        }
        if (action !== "resize" || value === null || value === undefined)
            return false
        const requestedWidth = Number(value.width)
        const requestedHeight = Number(value.height)
        if (!Number.isFinite(requestedWidth) || !Number.isFinite(requestedHeight)
                || requestedWidth < minimumWidth || requestedHeight < minimumHeight)
            return false
        width = Math.round(requestedWidth)
        height = Math.round(requestedHeight)
        return true
    }

    width: 1440
    height: 900
    minimumWidth: 800
    minimumHeight: 600
    visible: true
    title: semanticName
    color: Theme.surface

    Shortcut {
        sequence: "Ctrl+K"
        onActivated: palette.open()
    }

    Shortcut {
        sequence: "Ctrl+,"
        onActivated: App.ui.navigateTo("settings")
    }

    Shortcut {
        sequences: [StandardKey.Undo]
        enabled: App.ui.canUndo
        onActivated: App.ui.undo()
    }

    Shortcut {
        sequences: [StandardKey.Redo]
        enabled: App.ui.canRedo
        onActivated: App.ui.redo()
    }

    Shortcut {
        sequences: [StandardKey.Delete]
        enabled: App.ui.activeCommandId.length === 0
                 && App.ui.sketchConstraintSelected
        onActivated: App.ui.requestCommand("sketch.constraint.delete")
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ProjectBar {
            Layout.fillWidth: true
            onOpenCommandPalette: palette.open()
        }

        StackLayout {
            id: surfaces

            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: {
                switch (App.ui.activeSurfaceId) {
                case "projects": return 1
                case "settings": return 2
                case "recovery": return 3
                case "operations": return 4
                default: return 0
                }
            }

            EditorPage { }
            ProjectHubPage { }
            SettingsPage { }
            RecoveryPage { }
            OperationInspectorPage { }
        }
    }

    CommandPalette { id: palette }
}
