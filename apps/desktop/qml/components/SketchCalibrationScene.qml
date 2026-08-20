import QtQuick
import Kearne.UI

Item {
    id: root

    property string semanticId: "sketch.canvas"
    property string semanticName: "Sketch plane"
    property string semanticRole: "canvas"
    property var semanticActions: []
    property string semanticValue: App.ui.activeCommandId + ":"
                                   + App.ui.sketchInputCount
    property real cursorX: 0
    property real cursorY: 0
    property bool cursorVisible: false
    readonly property real pixelsPerMillimeter: App.sketchCamera.pixelsPerMetre
                                                 / 1000

    activeFocusOnTab: true
    Accessible.name: semanticName
    Accessible.id: semanticId
    Accessible.role: Accessible.Canvas
    Accessible.focusable: enabled && visible

    function screenPoint(point) {
        const dx = point.x / 1000 - App.sketchCamera.centerMetres.x
        const dy = point.y / 1000 - App.sketchCamera.centerMetres.y
        const cosine = Math.cos(App.sketchCamera.rotationRadians)
        const sine = Math.sin(App.sketchCamera.rotationRadians)
        return [width / 2 + (cosine * dx - sine * dy)
                * App.sketchCamera.pixelsPerMetre,
                height / 2 - (sine * dx + cosine * dy)
                * App.sketchCamera.pixelsPerMetre]
    }

    function planePoint(x, y) {
        const projectedX = (x - width / 2) / App.sketchCamera.pixelsPerMetre
        const projectedY = -(y - height / 2) / App.sketchCamera.pixelsPerMetre
        const cosine = Math.cos(App.sketchCamera.rotationRadians)
        const sine = Math.sin(App.sketchCamera.rotationRadians)
        let planeX = (App.sketchCamera.centerMetres.x
                      + cosine * projectedX + sine * projectedY) * 1000
        let planeY = (App.sketchCamera.centerMetres.y
                      - sine * projectedX + cosine * projectedY) * 1000
        if (App.workspace.gridSnapEnabled) {
            const spacing = App.ui.gridSpacingMillimeters
            planeX = Math.round(planeX / spacing) * spacing
            planeY = Math.round(planeY / spacing) * spacing
        }
        return [planeX, planeY]
    }

    function segmentDistance(x, y, start, end) {
        const dx = end[0] - start[0]
        const dy = end[1] - start[1]
        const lengthSquared = dx * dx + dy * dy
        const amount = lengthSquared < 0.001 ? 0
                       : Math.max(0, Math.min(1,
                           ((x - start[0]) * dx + (y - start[1]) * dy)
                           / lengthSquared))
        return Math.hypot(x - start[0] - amount * dx,
                          y - start[1] - amount * dy)
    }

    function hitTest(x, y) {
        const primitives = App.ui.sketchPrimitives
        const selection = App.ui.sketchSelectionKind
        for (let index = primitives.length - 1; index >= 0; --index) {
            const primitive = primitives[index]
            if (primitive.draft || primitive.points.length === 0)
                continue
            if (selection !== "curve") {
                for (const projectedPoint of primitive.points) {
                    const point = screenPoint(projectedPoint)
                    if (Math.hypot(x - point[0], y - point[1]) <= 8)
                        return {"id": primitive.id,
                                "key": projectedPoint.key}
                }
            }
            if (selection === "point")
                continue
            if (primitive.kind === "line") {
                const start = screenPoint(primitive.points[0])
                const end = screenPoint(primitive.points[1])
                if (segmentDistance(x, y, start, end) <= 7)
                    return {"id": primitive.id, "key": ""}
            } else if (primitive.kind === "circle") {
                const center = screenPoint(primitive.points[0])
                const radius = primitive.radius * pixelsPerMillimeter
                if (Math.abs(Math.hypot(x - center[0], y - center[1])
                             - radius) <= 7)
                    return {"id": primitive.id, "key": ""}
            }
        }
        return {"id": "", "key": ""}
    }

    function updateCursor(x, y, visible) {
        cursorX = x
        cursorY = y
        cursorVisible = visible
        sketch.requestPaint()
    }

    function handleClick(x, y) {
        if (App.ui.sketchInputKind === "plane-point") {
            const point = planePoint(x, y)
            return App.ui.submitSketchPoint(point[0], point[1])
        }
        const pick = hitTest(x, y)
        if (pick.id.length === 0)
            return false
        if (App.ui.sketchInputKind === "entity")
            return App.ui.submitSketchEntity(pick.id, pick.key)
        App.ui.selectEntity(pick.id)
        return true
    }

    Canvas {
        id: sketch
        anchors.fill: parent
        renderStrategy: Canvas.Threaded
        antialiasing: true

        function drawPrimitive(context, primitive) {
            const points = primitive.points.map(point => root.screenPoint(point))
            if (points.length === 0)
                return
            context.setLineDash(primitive.construction ? [7, 5] : [])
            context.strokeStyle = primitive.selected ? Theme.warning
                                  : (primitive.draft ? Theme.accentHover
                                                     : Theme.accent)
            context.fillStyle = context.strokeStyle
            context.lineWidth = primitive.selected || primitive.draft ? 2.4 : 1.7
            if (primitive.kind === "point") {
                context.beginPath()
                context.arc(points[0][0], points[0][1], 3.5, 0, Math.PI * 2)
                context.fill()
            } else if (primitive.kind === "line" && points.length >= 2) {
                context.beginPath()
                context.moveTo(points[0][0], points[0][1])
                context.lineTo(points[1][0], points[1][1])
                context.stroke()
            } else if (primitive.kind === "circle") {
                context.beginPath()
                context.arc(points[0][0], points[0][1],
                            Math.abs(primitive.radius * root.pixelsPerMillimeter),
                            0, Math.PI * 2)
                context.stroke()
            } else if (primitive.kind === "arc" && points.length >= 3) {
                context.beginPath()
                context.moveTo(points[0][0], points[0][1])
                context.quadraticCurveTo(points[1][0], points[1][1],
                                         points[2][0], points[2][1])
                context.stroke()
            }
            for (let index = 0; index < points.length; ++index) {
                if (!primitive.points[index].selected)
                    continue
                context.fillStyle = Theme.warning
                context.beginPath()
                context.arc(points[index][0], points[index][1], 4, 0,
                            Math.PI * 2)
                context.fill()
            }
            context.setLineDash([])
        }

        onPaint: {
            const context = getContext("2d")
            context.reset()
            context.clearRect(0, 0, width, height)
            for (const primitive of App.ui.sketchPrimitives)
                drawPrimitive(context, primitive)

            if (root.cursorVisible && App.ui.sketchInputKind === "plane-point") {
                const point = root.planePoint(root.cursorX, root.cursorY)
                const snapped = root.screenPoint({"x": point[0], "y": point[1]})
                context.strokeStyle = Theme.textMuted
                context.lineWidth = 1
                context.beginPath()
                context.moveTo(snapped[0] - 7, snapped[1])
                context.lineTo(snapped[0] + 7, snapped[1])
                context.moveTo(snapped[0], snapped[1] - 7)
                context.lineTo(snapped[0], snapped[1] + 7)
                context.stroke()
            }
        }

        Connections {
            target: App.sketchCamera
            function onCameraChanged() { sketch.requestPaint() }
        }

        Connections {
            target: App.themes
            function onProjectionChanged() { sketch.requestPaint() }
        }

        Connections {
            target: App.ui
            function onProjectionChanged() { sketch.requestPaint() }
        }

        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
    }

    StateBadge {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.leftMargin: Theme.space4
        anchors.topMargin: 84
        label: App.ui.sketchInputKind === "none"
               ? "DOF NOT EVALUATED"
               : App.ui.sketchInputCount + " / "
                 + App.ui.sketchMinimumInputCount + " INPUTS"
        status: App.ui.sketchInputKind === "none" ? "unavailable" : "preview"
    }
}
