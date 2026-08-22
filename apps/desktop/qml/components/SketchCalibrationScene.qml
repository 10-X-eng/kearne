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
