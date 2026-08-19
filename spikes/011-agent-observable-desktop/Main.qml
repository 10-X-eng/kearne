import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root
    width: 1100
    height: 700
    x: 24
    y: 40
    visible: true
    title: "Kearne — Mounting Plate"
    color: "#10141b"
    palette.window: "#151a22"
    palette.windowText: "#e7ebf2"
    palette.base: "#0f141b"
    palette.alternateBase: "#17202b"
    palette.text: "#e7ebf2"
    palette.button: "#202938"
    palette.buttonText: "#d7dde7"
    palette.highlight: "#3d5a80"
    palette.highlightedText: "#f4f7fb"
    property string kearneSurfaceId: "surface.main"
    property string semanticId: "window.main"

    component KButton: Button {
        id: control
        property string semanticId
        property bool selected: false
        property bool emphasized: selected
        Accessible.id: semanticId
        Accessible.name: text
        Accessible.selected: selected
        implicitHeight: 34
        leftPadding: 13
        rightPadding: 13
        font.pixelSize: 12
        font.weight: emphasized ? Font.DemiBold : Font.Medium
        palette.buttonText: emphasized ? "#f4f7fb" : "#aeb8c6"
        background: Rectangle {
            radius: 7
            color: control.down ? "#334258"
                                : control.emphasized ? "#26354a"
                                                   : control.hovered ? "#202938" : "transparent"
            border.color: control.emphasized ? "#4d709f" : "transparent"
        }
    }

    component PanelLabel: Label {
        color: "#e7ebf2"
        font.pixelSize: 11
        font.weight: Font.DemiBold
    }

    header: Rectangle {
        height: 96
        color: "#151a22"
        border.color: "#252c37"
        property string semanticId: "region.command_bar"
        Accessible.id: semanticId
        Accessible.name: "Command bar"
        Accessible.role: Accessible.ToolBar

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 48
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                spacing: 8

                Label {
                    text: "KEARNE"
                    color: "#f4f7fb"
                    font.pixelSize: 15
                    font.weight: Font.Bold
                    font.letterSpacing: 1.2
                }
                Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 20; color: "#303845" }
                Label {
                    text: "Mounting Plate"
                    color: "#d7dde7"
                    font.pixelSize: 13
                    font.weight: Font.Medium
                }
                Label { text: "main  ·  r18"; color: "#748196"; font.pixelSize: 11 }
                Item { Layout.fillWidth: true }
                KButton { semanticId: "command.search"; text: "Search commands" }
                KButton { semanticId: "command.agent"; text: "Agent"; emphasized: true }
            }

            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#252c37" }

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 47
                Layout.leftMargin: 12
                spacing: 2

                KButton { semanticId: "workspace.design"; text: "Design"; selected: true }
                KButton { semanticId: "workspace.assembly"; text: "Assembly" }
                KButton { semanticId: "workspace.simulation"; text: "Simulation" }
                Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 22; color: "#303845"; Layout.leftMargin: 6; Layout.rightMargin: 6 }
                KButton { semanticId: "command.sketch"; text: "Sketch" }
                KButton { semanticId: "command.extrude"; text: "Extrude"; selected: true }
                KButton { semanticId: "command.fillet"; text: "Fillet" }
                KButton { semanticId: "command.measure"; text: "Measure" }
                Item { Layout.fillWidth: true }
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 1

        Rectangle {
            Layout.preferredWidth: 220
            Layout.fillHeight: true
            color: "#141922"
            property string semanticId: "panel.structure"
            Accessible.id: semanticId
            Accessible.name: "Model structure"
            Accessible.role: Accessible.Pane

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 7
                PanelLabel { text: "MODEL" }
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#272e39" }
                Label { text: "⌄  Mounting Plate"; color: "#dce2eb"; font.pixelSize: 12 }
                Label { text: "    Origin"; color: "#7f8a9b"; font.pixelSize: 12 }
                Label { text: "    Sketch 01"; color: "#9aa6b6"; font.pixelSize: 12 }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 32
                    radius: 6
                    color: "#223148"
                    border.color: "#3d5a80"
                    property string semanticId: "tree.feature.extrude_01"
                    Accessible.id: semanticId
                    Accessible.name: "Extrude 01, selected"
                    Accessible.role: Accessible.TreeItem
                    Accessible.selected: true
                    Label { anchors.verticalCenter: parent.verticalCenter; x: 18; text: "Extrude 01"; color: "#edf3fc"; font.pixelSize: 12 }
                }
                Label { text: "    Fillet 01"; color: "#9aa6b6"; font.pixelSize: 12 }
                Item { Layout.fillHeight: true }
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: "History"; color: "#7f8a9b"; font.pixelSize: 11 }
                    Item { Layout.fillWidth: true }
                    Label { text: "18 revisions"; color: "#59677b"; font.pixelSize: 10 }
                }
            }
        }

        Rectangle {
            id: viewport
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#0d1117"
            property string semanticId: "viewport.primary"
            Accessible.id: semanticId
            Accessible.name: "Primary 3D viewport"
            Accessible.role: Accessible.Canvas

            Canvas {
                id: scene
                anchors.fill: parent
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    ctx.fillStyle = "#0d1117"
                    ctx.fillRect(0, 0, width, height)
                    ctx.strokeStyle = "#18202a"
                    ctx.lineWidth = 1
                    const step = 32
                    for (let x = width / 2 % step; x < width; x += step) {
                        ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, height); ctx.stroke()
                    }
                    for (let y = height / 2 % step; y < height; y += step) {
                        ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(width, y); ctx.stroke()
                    }

                    const cx = width * 0.5
                    const cy = height * 0.48
                    ctx.fillStyle = "#40566d"
                    ctx.strokeStyle = "#8ba4bc"
                    ctx.lineWidth = 1.5
                    ctx.beginPath()
                    ctx.moveTo(cx - 170, cy - 30)
                    ctx.lineTo(cx + 45, cy - 145)
                    ctx.lineTo(cx + 185, cy - 68)
                    ctx.lineTo(cx - 30, cy + 48)
                    ctx.closePath(); ctx.fill(); ctx.stroke()

                    ctx.fillStyle = "#273848"
                    ctx.beginPath()
                    ctx.moveTo(cx - 170, cy - 30)
                    ctx.lineTo(cx - 30, cy + 48)
                    ctx.lineTo(cx - 30, cy + 104)
                    ctx.lineTo(cx - 170, cy + 27)
                    ctx.closePath(); ctx.fill(); ctx.stroke()

                    ctx.fillStyle = "#31475a"
                    ctx.beginPath()
                    ctx.moveTo(cx - 30, cy + 48)
                    ctx.lineTo(cx + 185, cy - 68)
                    ctx.lineTo(cx + 185, cy - 12)
                    ctx.lineTo(cx - 30, cy + 104)
                    ctx.closePath(); ctx.fill(); ctx.stroke()

                    ctx.fillStyle = "#111820"
                    ctx.strokeStyle = "#90aac1"
                    for (const p of [[-92, -7], [52, -83], [101, -49], [-40, 26]]) {
                        ctx.beginPath(); ctx.ellipse(cx + p[0], cy + p[1], 17, 10, -0.49, 0, Math.PI * 2); ctx.fill(); ctx.stroke()
                    }
                }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.margins: 14
                width: 152
                height: 30
                radius: 7
                color: "#17202bd9"
                border.color: "#2c3847"
                Label { anchors.centerIn: parent; text: "PERSPECTIVE  ·  FIT"; color: "#9ba9ba"; font.pixelSize: 10 }
            }

            Row {
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.margins: 15
                spacing: 8
                Label { text: "X"; color: "#e66d6d"; font.pixelSize: 11; font.weight: Font.Bold }
                Label { text: "Y"; color: "#69c68d"; font.pixelSize: 11; font.weight: Font.Bold }
                Label { text: "Z"; color: "#6b9fe8"; font.pixelSize: 11; font.weight: Font.Bold }
            }
        }

        Rectangle {
            Layout.preferredWidth: 252
            Layout.fillHeight: true
            color: "#141922"
            property string semanticId: "panel.properties"
            Accessible.id: semanticId
            Accessible.name: "Properties"
            Accessible.role: Accessible.Pane

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 10
                PanelLabel { text: "EXTRUDE 01" }
                Label { text: "Feature is up to date"; color: "#72c594"; font.pixelSize: 11 }
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#272e39" }
                Label { text: "DISTANCE"; color: "#707d8f"; font.pixelSize: 9; font.weight: Font.DemiBold }
                TextField {
                    id: distance
                    Layout.fillWidth: true
                    text: "12.00 mm"
                    color: "#edf1f7"
                    selectByMouse: true
                    property string semanticId: "parameter.extrude.distance"
                    Accessible.id: semanticId
                    Accessible.name: "Extrude distance"
                    background: Rectangle { radius: 6; color: "#0f141b"; border.color: distance.activeFocus ? "#527daf" : "#303946" }
                }
                Label { text: "OPERATION"; color: "#707d8f"; font.pixelSize: 9; font.weight: Font.DemiBold }
                ComboBox {
                    Layout.fillWidth: true
                    model: ["New body", "Join", "Cut", "Intersect"]
                    currentIndex: 0
                    property string semanticId: "parameter.extrude.operation"
                    Accessible.id: semanticId
                    Accessible.name: "Boolean operation"
                }
                Label { text: "DIRECTION"; color: "#707d8f"; font.pixelSize: 9; font.weight: Font.DemiBold }
                RowLayout {
                    KButton { semanticId: "parameter.direction.normal"; text: "Normal"; selected: true }
                    KButton { semanticId: "parameter.direction.reverse"; text: "Reverse" }
                }
                Item { Layout.fillHeight: true }
                Label { text: "Revision r18  ·  saved"; color: "#5f6b7c"; font.pixelSize: 10 }
            }
        }
    }

    footer: Rectangle {
        height: 28
        color: "#11161e"
        border.color: "#252c37"
        property string semanticId: "region.status"
        Accessible.id: semanticId
        Accessible.name: "Ready, one body, no active jobs"
        Accessible.role: Accessible.StatusBar
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            Label { text: "READY"; color: "#75c996"; font.pixelSize: 9; font.weight: Font.DemiBold }
            Label { text: "1 body  ·  4 features"; color: "#788496"; font.pixelSize: 10 }
            Item { Layout.fillWidth: true }
            Label { text: "0 jobs"; color: "#788496"; font.pixelSize: 10 }
            Label { text: "60 fps"; color: "#788496"; font.pixelSize: 10 }
        }
    }

    Window {
        id: operationWindow
        width: 292
        height: 360
        x: root.x + root.width + 16
        y: root.y + 82
        visible: true
        title: "Operation"
        color: "#151a22"
        flags: Qt.Tool
        property string kearneSurfaceId: "surface.operation"
        property string semanticId: "window.operation"

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 18
            spacing: 10
            Label { text: "EXTRUDE"; color: "#f0f4fa"; font.pixelSize: 14; font.weight: Font.DemiBold }
            Label {
                Layout.fillWidth: true
                text: "A non-destructive feature preview."
                wrapMode: Text.WordWrap
                color: "#8794a6"
                font.pixelSize: 11
            }
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#2a323e" }
            Label { text: "Profile"; color: "#788598"; font.pixelSize: 10 }
            Label { text: "Sketch 01 · closed loop"; color: "#d8dee8"; font.pixelSize: 12 }
            Label { text: "Result"; color: "#788598"; font.pixelSize: 10 }
            Label { text: "1 valid solid"; color: "#72c594"; font.pixelSize: 12 }
            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                KButton { semanticId: "operation.cancel"; text: "Cancel" }
                KButton { semanticId: "operation.apply"; text: "Apply"; emphasized: true }
            }
        }
    }
}
