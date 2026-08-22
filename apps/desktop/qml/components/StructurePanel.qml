pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kearne.UI

Rectangle {
    id: root

    function entitySemanticId(record, index) {
        const identity = String(record.id)
        if (!/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i
                .test(identity))
            return "entity." + identity
        let occurrence = 0
        const records = App.ui.structure
        for (let current = 0; current <= index; ++current) {
            if (records[current].kind === record.kind)
                ++occurrence
        }
        const kind = String(record.kind).replace(/^sketch-/, "")
                                      .replace(/[^a-z0-9]+/gi, "-")
                                      .toLowerCase()
        return "entity.sketch." + kind + "." + occurrence
    }

    function iconFor(record) {
        if (root.activePage === 1)
            return "checkpoint"
        switch (record.kind) {
        case "sketch": return "sketch"
        case "sketch-rectangle": return "rectangle"
        case "sketch-edge": return "line"
        case "sketch-point": return "point"
        case "sketch-line": return "line"
        case "sketch-polyline": return "polyline"
        case "sketch-polygon": return "polygon"
        case "sketch-circle": return "circle"
        case "sketch-arc": return "arc"
        case "sketch-ellipse": return "ellipse"
        case "sketch-elliptical-arc": return "elliptical-arc"
        case "sketch-hyperbolic-arc": return "hyperbolic-arc"
        case "sketch-parabolic-arc": return "parabolic-arc"
        case "sketch-bspline": return "bspline"
        case "sketch-fillet": return "round"
        case "sketch-chamfer": return "chamfer"
        case "sketch-offset": return "offset"
        case "sketch-joined-curve": return "bspline"
        case "sketch-slot": return "slot"
        case "sketch-oblong": return "oblong"
        case "sketch-arc-slot": return "slot"
        case "sketch-curve-group": return "folder"
        case "sketch-constraint": return "coincident"
        case "plane": return "plane"
        case "model-function": return "code"
        case "group": return "folder"
        default: return "model"
        }
    }

    property string semanticId: "panel.structure"
    property string semanticName: "Structure and history"
    property string semanticRole: "panel"
    property var semanticActions: []
    property int activePage: 0
    property bool closable: false
    signal closeRequested()

    color: Theme.surface
    border.color: Theme.border

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            Layout.leftMargin: Theme.space2
            Layout.rightMargin: Theme.space2
            spacing: 0

            KButton {
                semanticId: "structure.tab.entities"
                semanticName: "Structure"
                semanticRole: "tab"
                text: "Structure"
                quiet: true
                checkable: true
                checked: root.activePage === 0
                onClicked: root.activePage = 0
            }

            KButton {
                semanticId: "structure.tab.history"
                semanticName: "History"
                semanticRole: "tab"
                text: "History"
                quiet: true
                checkable: true
                checked: root.activePage === 1
                onClicked: root.activePage = 1
            }

            Item { Layout.fillWidth: true }

            KButton {
                semanticId: visible ? "structure.collapse" : ""
                semanticName: "Collapse structure panel"
                iconName: "collapse-left"
                quiet: true
                compact: true
                visible: root.closable
                onClicked: root.closeRequested()
            }
        }

        KSeparator { }

        ListView {
            id: entityList
            property string semanticId: root.activePage === 0 ? "structure.entities" : "structure.history"
            property string semanticName: root.activePage === 0 ? "Document structure" : "Revision history"
            property string semanticRole: "list"
            property var semanticActions: []

            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.activePage === 0 ? App.ui.structure : App.ui.revisions

            delegate: ItemDelegate {
                id: row
                required property var modelData
                required property int index
                property string semanticId: root.activePage === 0
                                                ? root.entitySemanticId(
                                                      row.modelData, row.index)
                                                : "revision." + row.modelData.id
                property string semanticName: row.modelData.label
                property string semanticRole: "listitem"
                property var semanticActions: root.activePage === 0 ? ["select"] : []
                property string semanticValue: root.activePage === 0
                                                   ? row.modelData.kind
                                                   : row.modelData.detail

                function selectEntity() {
                    if (root.activePage !== 0)
                        return false
                    App.ui.selectEntity(row.modelData.id)
                    return true
                }

                function performSemanticAction(action, value) {
                    return action === "select" && selectEntity()
                }

                width: entityList.width
                height: 32
                hoverEnabled: true
                Accessible.name: semanticName
                Accessible.id: semanticId
                Accessible.role: Accessible.ListItem
                Accessible.focusable: enabled && visible
                onClicked: selectEntity()

                contentItem: RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.space2 + (row.modelData.depth ?? 0) * Theme.space4
                    anchors.rightMargin: Theme.space2
                    spacing: Theme.space2

                    KIcon {
                        Layout.preferredWidth: Theme.iconSizeSmall
                        Layout.preferredHeight: Theme.iconSizeSmall
                        name: root.iconFor(row.modelData)
                        color: root.activePage === 0 ? Theme.accent : Theme.textFaint
                    }

                    Text {
                        Layout.fillWidth: true
                        text: row.modelData.label
                        color: Theme.text
                        font.pixelSize: Theme.fontBody
                        elide: Text.ElideRight
                    }

                    Text {
                        visible: root.activePage === 1
                        text: row.modelData.detail ?? ""
                        color: Theme.textFaint
                        font.pixelSize: Theme.fontSmall
                    }
                }

                background: Rectangle {
                    color: row.hovered || row.visualFocus
                           ? Theme.surfaceMuted : Theme.surface
                    border.width: row.visualFocus ? Theme.focusRingWidth : 0
                    border.color: Theme.focus
                }
            }
        }

        Rectangle {
            visible: root.activePage === 1
            Layout.fillWidth: true
            Layout.preferredHeight: 42
            color: Theme.surfaceRaised
            border.color: Theme.border

            RowLayout {
                anchors.fill: parent
                anchors.margins: Theme.space2

                Row {
                    spacing: Theme.space1

                    Repeater {
                        model: App.ui.historyCommands

                        KButton {
                            required property var modelData
                            semanticId: "history.command." + modelData.id
                            semanticName: modelData.label
                            semanticValue: modelData.shortcut
                            iconName: modelData.icon
                            shortcut: modelData.shortcut
                            enabled: modelData.available
                            compact: true
                            quiet: true
                            onClicked: App.ui.requestCommand(modelData.id)
                        }
                    }
                }
            }
        }
    }
}
