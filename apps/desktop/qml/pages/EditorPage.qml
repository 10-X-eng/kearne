import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kearne.UI
import "../components"

ColumnLayout {
    id: root

    property string semanticId: "surface.editor"
    property string semanticName: "Design editor"
    property string semanticRole: "main"
    property var semanticActions: []

    spacing: 0

    WorkspaceBar { Layout.fillWidth: true }
    CommandStrip { Layout.fillWidth: true }

    RowLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        spacing: 0

        Loader {
            id: structure
            active: root.width >= 1040
            visible: active
            Layout.preferredWidth: Theme.leftPanelWidth
            Layout.minimumWidth: active ? 200 : 0
            Layout.maximumWidth: 420
            Layout.fillHeight: true
            sourceComponent: StructurePanel { }
        }

        DesignViewport {
            Layout.fillWidth: true
            Layout.fillHeight: true
            onRequestStructure: structureDrawer.open()
            onRequestInspector: inspectorDrawer.open()
        }

        Loader {
            id: inspector
            active: root.width >= 1240
            visible: active
            Layout.preferredWidth: Theme.rightPanelWidth
            Layout.minimumWidth: active ? 300 : 0
            Layout.maximumWidth: 480
            Layout.fillHeight: true
            sourceComponent: InspectorPanel { }
        }
    }

    StatusBar { Layout.fillWidth: true }

    Drawer {
        id: structureDrawer
        property string semanticId: enabled ? "drawer.structure" : ""
        property string semanticName: "Structure drawer"
        property string semanticRole: "drawer"
        property var semanticActions: ["dismiss"]
        edge: Qt.LeftEdge
        width: Math.min(Theme.leftPanelWidth, root.width * 0.8)
        height: root.height - Theme.statusBarHeight
        modal: true
        enabled: !structure.active
        contentItem: Loader {
            active: structureDrawer.enabled
            sourceComponent: StructurePanel { }
        }
    }

    Drawer {
        id: inspectorDrawer
        property string semanticId: enabled ? "drawer.inspector" : ""
        property string semanticName: "Inspector drawer"
        property string semanticRole: "drawer"
        property var semanticActions: ["dismiss"]
        edge: Qt.RightEdge
        width: Math.min(Theme.rightPanelWidth, root.width * 0.88)
        height: root.height - Theme.statusBarHeight
        modal: true
        enabled: !inspector.active
        contentItem: Loader {
            active: inspectorDrawer.enabled
            sourceComponent: InspectorPanel { }
        }
    }
}
