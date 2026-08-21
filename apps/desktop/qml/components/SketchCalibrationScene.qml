pragma ComponentBehavior: Bound

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
    required property real snapSpacingMillimeters

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

    function dimensionAnchor(anchor, origin, index) {
        const anchorScreen = screenPoint(anchor)
        const originScreen = screenPoint(origin)
        let outwardX = anchorScreen[0] - originScreen[0]
        let outwardY = anchorScreen[1] - originScreen[1]
        const magnitude = Math.hypot(outwardX, outwardY)
        if (magnitude > 0.001) {
            outwardX /= magnitude
            outwardY /= magnitude
        } else {
            outwardX = 0
            outwardY = -1
        }
        const distance = 22 + index * 25
        return Qt.point(anchorScreen[0] + outwardX * distance,
                        anchorScreen[1] + outwardY * distance)
    }

    function handleClick(x, y) {
        if (App.ui.sketchInputKind === "plane-point") {
            const point = planePoint(x, y)
            return App.ui.submitSketchPoint(point[0], point[1])
        }
        if (App.ui.submitSketchPointerClick(x, y))
            return true
        return false
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

    component PreviewDimension: Rectangle {
        required property point anchorPoint
        required property string label

        x: anchorPoint.x - width / 2
        y: anchorPoint.y - height / 2
        width: dimensionText.implicitWidth + 14
        height: 22
        radius: Theme.radiusSmall
        color: Theme.surface
        border.color: Theme.borderStrong
        border.width: Theme.separatorWidth
        visible: App.ui.sketchGesturePreviewVisible

        Text {
            id: dimensionText
            anchors.centerIn: parent
            text: parent.label
            color: Theme.text
            font.pixelSize: Theme.fontSmall
            font.weight: Theme.fontWeightStrong
        }
    }

    Canvas {
        id: liveGeometry

        anchors.fill: parent
        visible: App.ui.sketchGesturePreviewVisible
        antialiasing: true
        readonly property var primitives: App.ui.sketchPreviewPrimitives

        function drawPolyline(context, points, closed) {
            if (points.length === 0)
                return
            const first = root.screenPoint(points[0])
            context.moveTo(first[0], first[1])
            for (let index = 1; index < points.length; ++index) {
                const point = root.screenPoint(points[index])
                context.lineTo(point[0], point[1])
            }
            if (closed)
                context.closePath()
        }

        function analyticPoints(primitive) {
            const center = primitive.points[0]
            const kind = primitive.kind
            const full = kind === "circle" || kind === "ellipse"
            const start = full ? 0 : primitive.startAngleRadians
            const sweep = full ? Math.PI * 2 : primitive.sweepAngleRadians
            const maximumRadius = Math.max(primitive.radius,
                                           primitive.secondaryRadius)
            const radiusPixels = maximumRadius / 1000
                               * App.sketchCamera.pixelsPerMetre
            const segments = Math.max(12, Math.min(256,
                Math.ceil(Math.abs(sweep) * Math.max(1, radiusPixels) / 8)))
            const cosine = Math.cos(primitive.rotationAngleRadians)
            const sine = Math.sin(primitive.rotationAngleRadians)
            const points = []
            for (let index = 0; index <= segments; ++index) {
                const parameter = start + sweep * index / segments
                let localX
                let localY
                if (kind === "hyperbolic-arc") {
                    localX = primitive.radius * Math.cosh(parameter)
                    localY = primitive.secondaryRadius * Math.sinh(parameter)
                } else if (kind === "parabolic-arc") {
                    localX = parameter * parameter / (4 * primitive.radius)
                    localY = parameter
                } else {
                    localX = primitive.radius * Math.cos(parameter)
                    localY = (kind === "ellipse" || kind === "elliptical-arc")
                             ? primitive.secondaryRadius * Math.sin(parameter)
                             : primitive.radius * Math.sin(parameter)
                }
                points.push(Qt.point(center.x + cosine * localX - sine * localY,
                                     center.y + sine * localX + cosine * localY))
            }
            return points
        }

        onPrimitivesChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()

        Connections {
            target: App.sketchCamera
            function onCameraChanged() { liveGeometry.requestPaint() }
        }

        onPaint: {
            const context = getContext("2d")
            context.clearRect(0, 0, width, height)
            context.lineWidth = 1.75
            context.lineCap = "round"
            context.lineJoin = "round"
            context.strokeStyle = Theme.accent
            context.fillStyle = Theme.accent
            for (const primitive of primitives) {
                context.setLineDash(primitive.construction ? [7, 5] : [])
                context.beginPath()
                if (primitive.kind === "point") {
                    const point = root.screenPoint(primitive.points[0])
                    context.arc(point[0], point[1], 3, 0, Math.PI * 2)
                    context.fill()
                } else if (primitive.kind === "line") {
                    drawPolyline(context, primitive.points, false)
                    context.stroke()
                } else {
                    drawPolyline(context, analyticPoints(primitive), false)
                    context.stroke()
                }
            }
        }
    }

    Repeater {
        model: App.ui.sketchPreviewMeasurements

        delegate: PreviewDimension {
            required property var modelData
            required property int index
            property string semanticId: visible
                                        ? "sketch.preview.measurement." + index
                                        : ""
            property string semanticName: "Live sketch measurement"
            property string semanticRole: "label"
            property var semanticActions: []
            property string semanticValue: label
            anchorPoint: root.dimensionAnchor(modelData.anchor,
                                              modelData.origin, index)
            label: modelData.label
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
