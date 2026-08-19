import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kearne.UI

ApplicationWindow {
    id: root

    property string semanticId: "window.main"
    property string semanticName: "Kearne — " + uiSession.projectName
    property string semanticRole: "window"
    property var semanticActions: ["focus", "resize"]
    property string semanticValue: uiSession.activeSurfaceId + ":" + uiSession.activeWorkspaceId

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
        onActivated: uiSession.navigateTo("settings")
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ProjectBar {
            Layout.fillWidth: true
            onOpenCommandPalette: palette.open()
        }

        Loader {
            Layout.fillWidth: true
            Layout.fillHeight: true
            sourceComponent: {
                switch (uiSession.activeSurfaceId) {
                case "projects": return projectsPage
                case "settings": return settingsPage
                case "recovery": return recoveryPage
                case "operations": return operationsPage
                default: return editorPage
                }
            }
        }
    }

    Component { id: editorPage; EditorPage { } }
    Component { id: projectsPage; ProjectHubPage { } }
    Component { id: settingsPage; SettingsPage { } }
    Component { id: recoveryPage; RecoveryPage { } }
    Component { id: operationsPage; OperationInspectorPage { } }

    CommandPalette { id: palette }
}
