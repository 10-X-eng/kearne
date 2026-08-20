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
    required property real snapSpacingMillimeters
    readonly property real pixelsPerMillimeter: App.sketchCamera.pixelsPerMetre
                                                 / 1000
    readonly property point snappedCursorPosition: {
        const plane = planePoint(cursorX, cursorY)
        const screen = screenPoint({"x": plane[0], "y": plane[1]})
        return Qt.point(screen[0], screen[1])
    }

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
            const spacing = snapSpacingMillimeters
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
    }

    function handleClick(x, y) {
        if (App.ui.sketchInputKind === "plane-point") {
            const point = planePoint(x, y)
            return App.ui.submitSketchPoint(point[0], point[1])
        }
        if (App.ui.submitSketchPointerClick(x, y))
            return true
        const pick = hitTest(x, y)
        if (pick.id.length === 0)
            return false
        if (App.ui.sketchInputKind === "entity")
            return App.ui.submitSketchEntity(pick.id, pick.key)
        App.ui.selectEntity(pick.id)
        return true
    }

    function handleDrag(firstX, firstY, oppositeX, oppositeY) {
        const first = planePoint(firstX, firstY)
        const opposite = planePoint(oppositeX, oppositeY)
        if (App.ui.sketchInputKind === "plane-point")
            return App.ui.submitSketchDrag(first[0], first[1],
                                           opposite[0], opposite[1])
        if (App.ui.activeCommandId.length === 0)
            return App.ui.submitSketchCurveDrag(firstX, firstY,
                                                opposite[0], opposite[1])
        return false
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

        function drawGesturePreview(context) {
            if (!App.ui.sketchDragPreviewVisible)
                return
            const canonical = [App.ui.sketchDragPreviewFirst,
                               App.ui.sketchDragPreviewSecond,
                               App.ui.sketchDragPreviewThird,
                               App.ui.sketchDragPreviewFourth]
            const points = canonical
                           .map(point => root.screenPoint(point))
            context.setLineDash(App.ui.sketchDragPreviewConstruction
                                ? [7, 5] : [])
            context.strokeStyle = Theme.accentHover
            context.lineWidth = 2.4
            context.beginPath()
            context.moveTo(points[0][0], points[0][1])
            for (let index = 1; index < points.length; ++index)
                context.lineTo(points[index][0], points[index][1])
            context.closePath()
            context.stroke()
            context.setLineDash([])

            const center = [(points[0][0] + points[2][0]) / 2,
                            (points[0][1] + points[2][1]) / 2]
            drawDimensionLabel(
                        context,
                        App.ui.formatProjectLength(
                            Math.abs(canonical[1].x - canonical[0].x)),
                        points[0], points[1], center)
            drawDimensionLabel(
                        context,
                        App.ui.formatProjectLength(
                            Math.abs(canonical[2].y - canonical[1].y)),
                        points[1], points[2], center)
        }

        function drawDimensionLabel(context, label, start, end, center) {
            const midpointX = (start[0] + end[0]) / 2
            const midpointY = (start[1] + end[1]) / 2
            let outwardX = midpointX - center[0]
            let outwardY = midpointY - center[1]
            const magnitude = Math.hypot(outwardX, outwardY)
            if (magnitude > 0.001) {
                outwardX /= magnitude
                outwardY /= magnitude
            }
            const labelX = midpointX + outwardX * 18
            const labelY = midpointY + outwardY * 18
            context.font = Theme.fontWeightStrong + " " + Theme.fontSmall
                           + "px sans-serif"
            const labelWidth = context.measureText(label).width + 14
            const labelHeight = 22
            context.fillStyle = Theme.surface
            context.strokeStyle = Theme.borderStrong
            context.lineWidth = 1
            context.fillRect(labelX - labelWidth / 2,
                             labelY - labelHeight / 2,
                             labelWidth, labelHeight)
            context.strokeRect(labelX - labelWidth / 2,
                               labelY - labelHeight / 2,
                               labelWidth, labelHeight)
            context.fillStyle = Theme.text
            context.textAlign = "center"
            context.textBaseline = "middle"
            context.fillText(label, labelX, labelY)
        }

        onPaint: {
            const context = getContext("2d")
            context.reset()
            context.clearRect(0, 0, width, height)
            for (const primitive of App.ui.sketchPrimitives)
                drawPrimitive(context, primitive)
            drawGesturePreview(context)

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

        Connections {
            target: App.ui
            function onSketchDragPreviewChanged() { sketch.requestPaint() }
        }

        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
    }

    Item {
        width: 15
        height: 15
        x: root.snappedCursorPosition.x - width / 2
        y: root.snappedCursorPosition.y - height / 2
        visible: root.cursorVisible
                 && App.ui.sketchInputKind === "plane-point"

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width
            height: Theme.separatorWidth
            color: Theme.textMuted
        }

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: parent.verticalCenter
            width: Theme.separatorWidth
            height: parent.height
            color: Theme.textMuted
        }
    }

    StateBadge {
        visible: root.visible
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.leftMargin: Theme.space4
        anchors.topMargin: 84
        semanticId: root.visible ? "sketch.solve.state" : ""
        semanticName: "Sketch solve state"
        semanticValue: App.ui.sketchSolveStatus + ":"
                       + App.ui.sketchDegreesOfFreedom
        label: App.ui.sketchDegreesOfFreedom < 0
               ? "DOF NOT EVALUATED"
               : App.ui.sketchSolveStatus.toUpperCase() + " · "
                 + App.ui.sketchDegreesOfFreedom + " DOF"
        status: App.ui.sketchSolveStatus === "solved" ? "current"
                : (App.ui.sketchSolveStatus === "underconstrained" ? "preview"
                   : (App.ui.sketchDegreesOfFreedom < 0 ? "unavailable"
                      : "failed"))
    }
}
