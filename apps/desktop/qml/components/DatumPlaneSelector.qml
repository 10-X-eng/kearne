pragma ComponentBehavior: Bound

import QtQuick
import Kearne.UI

Item {
    id: root

    property string semanticId: "viewport.datum_planes"
    property string semanticName: "Sketch attachment planes"
    property string semanticRole: "group"
    property var semanticActions: ["choose"]
    property string semanticValue: selectedPlaneId()
    property string hoveredPlaneId: ""

    function selectedPlaneId() {
        const field = App.ui.fields.find(
                        candidate => candidate.id
                        === "model.sketch.create.attachment")
        return field === undefined ? "" : String(field.value)
    }

    function planeRecords() {
        const centerX = width / 2
        const centerY = height * 0.53
        return [
            { id: "reference.plane.xy", label: "XY",
              color: Theme.axisZ,
              points: [[centerX - 150, centerY],
                       [centerX, centerY - 70],
                       [centerX + 150, centerY],
                       [centerX, centerY + 70]],
              labelX: centerX, labelY: centerY + 28 },
            { id: "reference.plane.xz", label: "XZ",
              color: Theme.axisY,
              points: [[centerX - 150, centerY],
                       [centerX, centerY - 70],
                       [centerX, centerY - 230],
                       [centerX - 150, centerY - 160]],
              labelX: centerX - 82, labelY: centerY - 112 },
            { id: "reference.plane.yz", label: "YZ",
              color: Theme.axisX,
              points: [[centerX, centerY - 70],
                       [centerX + 150, centerY],
                       [centerX + 150, centerY - 160],
                       [centerX, centerY - 230]],
              labelX: centerX + 82, labelY: centerY - 112 }
        ]
    }

    function containsPoint(points, x, y) {
        let inside = false
        for (let current = 0, previous = points.length - 1;
             current < points.length; previous = current++) {
            const first = points[current]
            const second = points[previous]
            if ((first[1] > y) !== (second[1] > y)
                    && x < (second[0] - first[0]) * (y - first[1])
                         / (second[1] - first[1]) + first[0])
                inside = !inside
        }
        return inside
    }

    function planeAt(x, y) {
        const planes = planeRecords()
        for (let index = planes.length - 1; index >= 0; --index) {
            if (containsPoint(planes[index].points, x, y))
                return planes[index].id
        }
        return ""
    }

    function performSemanticAction(action, value) {
        if (action !== "choose")
            return false
        const id = String(value)
        if (!planeRecords().some(plane => plane.id === id))
            return false
        App.ui.selectEntity(id)
        return true
    }

    Accessible.id: semanticId
    Accessible.name: semanticName
    Accessible.description: "Choose XY, XZ, or YZ for the new Sketch"
    Accessible.role: Accessible.Grouping

    Canvas {
        id: planes
        anchors.fill: parent
        antialiasing: true

        function drawPlane(context, plane, selected, hovered) {
            context.beginPath()
            context.moveTo(plane.points[0][0], plane.points[0][1])
            for (let index = 1; index < plane.points.length; ++index)
                context.lineTo(plane.points[index][0], plane.points[index][1])
            context.closePath()
            context.globalAlpha = selected ? 0.34 : (hovered ? 0.26 : 0.16)
            context.fillStyle = plane.color
            context.fill()
            context.globalAlpha = 1
            context.strokeStyle = selected || hovered ? plane.color
                                                       : Theme.borderStrong
            context.lineWidth = selected ? 2.4 : 1.5
            context.stroke()

            context.font = Theme.fontWeightStrong + " " + Theme.fontBody
                           + "px sans-serif"
            const labelWidth = context.measureText(plane.label).width + 18
            context.fillStyle = Theme.surface
            context.fillRect(plane.labelX - labelWidth / 2,
                             plane.labelY - 12, labelWidth, 24)
            context.strokeStyle = selected || hovered ? plane.color
                                                       : Theme.borderStrong
            context.lineWidth = 1
            context.strokeRect(plane.labelX - labelWidth / 2,
                               plane.labelY - 12, labelWidth, 24)
            context.fillStyle = selected || hovered ? plane.color : Theme.text
            context.textAlign = "center"
            context.textBaseline = "middle"
            context.fillText(plane.label, plane.labelX, plane.labelY)
        }

        onPaint: {
            const context = getContext("2d")
            context.reset()
            context.clearRect(0, 0, width, height)
            const selected = root.selectedPlaneId()
            for (const plane of root.planeRecords())
                drawPlane(context, plane, plane.id === selected,
                          plane.id === root.hoveredPlaneId)
        }

        Connections {
            target: App.ui
            function onProjectionChanged() { planes.requestPaint() }
        }

        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: root.hoveredPlaneId.length > 0
                     ? Qt.PointingHandCursor : Qt.ArrowCursor
        onPositionChanged: mouse => {
            const next = root.planeAt(mouse.x, mouse.y)
            if (next !== root.hoveredPlaneId) {
                root.hoveredPlaneId = next
                planes.requestPaint()
            }
        }
        onExited: {
            root.hoveredPlaneId = ""
            planes.requestPaint()
        }
        onPressed: mouse => mouse.accepted = root.planeAt(mouse.x, mouse.y)
                                          .length > 0
        onClicked: mouse => {
            const plane = root.planeAt(mouse.x, mouse.y)
            if (plane.length > 0)
                App.ui.selectEntity(plane)
        }
    }

    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 48
        width: prompt.implicitWidth + 28
        height: 34
        radius: Theme.badgeRadius
        color: Theme.surface
        border.color: Theme.borderStrong

        Text {
            id: prompt
            anchors.centerIn: parent
            text: "Select a datum plane or planar face"
            color: Theme.text
            font.pixelSize: Theme.fontBody
            font.weight: Theme.fontWeightStrong
        }
    }
}
