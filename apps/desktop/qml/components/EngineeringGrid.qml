import QtQuick
import Kearne.UI

Item {
    id: root

    property string semanticId: "viewport.grid"
    property string semanticName: "Modeling grid"
    property string semanticRole: "grid"
    property var semanticActions: []
    property string semanticValue: App.ui.gridPlaneLabel + ":" + App.ui.gridSpacingLabel
    property bool sketchView: App.ui.activeWorkspaceId === "sketch"
    property real minorPixelSpacing: 28
    property int majorInterval: 5

    EngineeringGridItem {
        anchors.fill: parent
        visible: root.sketchView
        viewCenterMetres: App.sketchCamera.centerMetres
        gridOriginMetres: Qt.point(0, 0)
        pixelsPerMetre: App.sketchCamera.pixelsPerMetre
        rotationRadians: App.sketchCamera.rotationRadians
        minorSpacingMetres: App.ui.gridSpacingMillimeters / 1000
        majorInterval: root.majorInterval
        minorLineWidthPixels: Theme.gridLineWidthMinor
        majorLineWidthPixels: Theme.gridLineWidthMajor
        axisLineWidthPixels: Theme.axisLineWidth
        minorColor: Theme.canvasGridMinor
        majorColor: Theme.canvasGridMajor
        axisXColor: Theme.axisX
        axisYColor: Theme.axisY
    }

    Canvas {
        id: gridCanvas
        anchors.fill: parent
        visible: !root.sketchView
        antialiasing: true

        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()

        function stroke(context, color, width, x1, y1, x2, y2) {
            context.beginPath()
            context.strokeStyle = color
            context.lineWidth = width
            context.moveTo(x1, y1)
            context.lineTo(x2, y2)
            context.stroke()
        }

        function paintModelGrid(context) {
            const originX = width / 2
            const originY = height * 0.62
            const step = root.minorPixelSpacing
            const extent = Math.ceil((width + height) / step)
            const ux = step
            const uy = -step * 0.28
            const vx = -step
            const vy = -step * 0.28

            for (let index = -extent; index <= extent; ++index) {
                if (index === 0)
                    continue
                const major = index % root.majorInterval === 0
                const color = major ? Theme.canvasGridMajor : Theme.canvasGridMinor
                const lineWidth = major ? Theme.gridLineWidthMajor
                                        : Theme.gridLineWidthMinor
                stroke(context, color, lineWidth,
                       originX + index * ux - extent * vx,
                       originY + index * uy - extent * vy,
                       originX + index * ux + extent * vx,
                       originY + index * uy + extent * vy)
                stroke(context, color, lineWidth,
                       originX + index * vx - extent * ux,
                       originY + index * vy - extent * uy,
                       originX + index * vx + extent * ux,
                       originY + index * vy + extent * uy)
            }
            stroke(context, Theme.axisX, Theme.axisLineWidth, originX - extent * ux,
                   originY - extent * uy, originX + extent * ux, originY + extent * uy)
            stroke(context, Theme.axisY, Theme.axisLineWidth, originX - extent * vx,
                   originY - extent * vy, originX + extent * vx, originY + extent * vy)
            stroke(context, Theme.axisZ, Theme.axisLineWidth,
                   originX, originY, originX, originY - step * 7)
        }

        onPaint: {
            const context = getContext("2d")
            context.reset()
            paintModelGrid(context)
        }

        Connections {
            target: App.camera
            function onCameraChanged() { gridCanvas.requestPaint() }
        }

    }

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.leftMargin: Theme.space4
        anchors.topMargin: 52
        width: gridReadout.implicitWidth + Theme.space4
        height: 26
        radius: Theme.radiusSmall
        color: Theme.surface
        border.color: Theme.border

        Text {
            id: gridReadout
            anchors.centerIn: parent
            text: App.ui.gridPlaneLabel + " plane  ·  Grid " + App.ui.gridSpacingLabel
            color: Theme.textMuted
            font.pixelSize: Theme.fontSmall
        }
    }
}
