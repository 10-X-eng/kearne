import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kearne.UI
import "../components"

Item {
    id: root

    property string semanticId: "surface.editor"
    property string semanticName: "Design editor"
    property string semanticRole: "main"
    property var semanticActions: []

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        WorkspaceBar { Layout.fillWidth: true }
        CommandStrip { Layout.fillWidth: true }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Loader {
                id: structure
                active: root.width >= 1040 && App.workspace.structureVisible
                visible: active
                Layout.preferredWidth: App.workspace.structureWidth
                Layout.minimumWidth: active ? 200 : 0
                Layout.maximumWidth: 420
                Layout.fillHeight: true
                sourceComponent: StructurePanel {
                    closable: true
                    onCloseRequested: App.workspace.structureVisible = false
                }
            }

            KSplitHandle {
                semanticId: visible ? "layout.structure.resize" : ""
                semanticName: "Resize structure panel"
                currentSize: structure.width
                minimumSize: 200
                maximumSize: 420
                visible: structure.active
                Layout.fillHeight: true
                onResizeRequested: size => App.workspace.structureWidth = size
            }

            DesignViewport {
                Layout.fillWidth: true
                Layout.fillHeight: true
                structureAvailable: structure.active
                inspectorAvailable: inspector.active
                onRequestStructure: {
                    if (root.width >= 1040)
                        App.workspace.structureVisible = true
                    else
                        structureDrawer.open()
                }
                onRequestInspector: {
                    if (root.width >= 1240)
                        App.workspace.inspectorVisible = true
                    else
                        inspectorDrawer.open()
                }
            }

            KSplitHandle {
                semanticId: visible ? "layout.inspector.resize" : ""
                semanticName: "Resize inspector panel"
                currentSize: inspector.width
                minimumSize: 300
                maximumSize: 480
                direction: -1
                visible: inspector.active
                Layout.fillHeight: true
                onResizeRequested: size => App.workspace.inspectorWidth = size
            }

            Loader {
                id: inspector
                active: root.width >= 1240 && App.workspace.inspectorVisible
                visible: active
                Layout.preferredWidth: App.workspace.inspectorWidth
                Layout.minimumWidth: active ? 300 : 0
                Layout.maximumWidth: 480
                Layout.fillHeight: true
                sourceComponent: InspectorPanel {
                    closable: true
                    onCloseRequested: App.workspace.inspectorVisible = false
                }
            }
        }

        StatusBar { Layout.fillWidth: true }
    }

    Drawer {
        id: structureDrawer
        property string semanticId: enabled ? "drawer.structure" : ""
        property string semanticName: "Structure drawer"
        property string semanticRole: "drawer"
        property var semanticActions: ["dismiss"]
        property real semanticValue: position

        function performSemanticAction(action, value) {
            if (action !== "dismiss")
                return false
            close()
            return true
        }

        edge: Qt.LeftEdge
        width: Math.min(Theme.leftPanelWidth, root.width * 0.8)
        height: root.height - Theme.statusBarHeight
        modal: true
        enabled: !structure.active
        contentItem: Loader {
            active: structureDrawer.visible
            sourceComponent: StructurePanel { }
        }
    }

    Drawer {
        id: inspectorDrawer
        property string semanticId: enabled ? "drawer.inspector" : ""
        property string semanticName: "Inspector drawer"
        property string semanticRole: "drawer"
        property var semanticActions: ["dismiss"]
        property real semanticValue: position

        function performSemanticAction(action, value) {
            if (action !== "dismiss")
                return false
            close()
            return true
        }

        edge: Qt.RightEdge
        width: Math.min(Theme.rightPanelWidth, root.width * 0.88)
        height: root.height - Theme.statusBarHeight
        modal: true
        enabled: !inspector.active
        contentItem: Loader {
            active: inspectorDrawer.visible
            sourceComponent: InspectorPanel { }
        }
    }
}
