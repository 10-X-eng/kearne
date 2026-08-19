import QtQuick
import Kearne.UI

Item {
    id: root

    property string semanticId: "viewport.grid"
    property string semanticName: "Modeling grid"
    property string semanticRole: "grid"
    property var semanticActions: []
    property string semanticValue: uiSession.gridPlaneLabel + ":" + uiSession.gridSpacingLabel
    property bool sketchView: uiSession.activeWorkspaceId === "sketch"
    property real minorPixelSpacing: sketchView ? 24 : 28
    property int majorInterval: 5

    Canvas {
        id: gridCanvas
        anchors.fill: parent
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

        function paintSketchGrid(context) {
            const originX = Math.round(width / 2) + 0.5
            const originY = Math.round(height / 2) + 0.5
            const step = root.minorPixelSpacing
            const xCount = Math.ceil(width / step / 2) + 1
            const yCount = Math.ceil(height / step / 2) + 1

            for (let index = -xCount; index <= xCount; ++index) {
                if (index === 0)
                    continue
                const x = originX + index * step
                stroke(context,
                       index % root.majorInterval === 0 ? Theme.canvasGridMajor
                                                        : Theme.canvasGridMinor,
                       index % root.majorInterval === 0 ? 1.15 : 0.65,
                       x, 0, x, height)
            }
            for (let index = -yCount; index <= yCount; ++index) {
                if (index === 0)
                    continue
                const y = originY + index * step
                stroke(context,
                       index % root.majorInterval === 0 ? Theme.canvasGridMajor
                                                        : Theme.canvasGridMinor,
                       index % root.majorInterval === 0 ? 1.15 : 0.65,
                       0, y, width, y)
            }
            stroke(context, Theme.axisX, 1.35, 0, originY, width, originY)
            stroke(context, Theme.axisY, 1.35, originX, 0, originX, height)
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
                const lineWidth = major ? 1.05 : 0.6
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
            stroke(context, Theme.axisX, 1.4, originX - extent * ux,
                   originY - extent * uy, originX + extent * ux, originY + extent * uy)
            stroke(context, Theme.axisY, 1.4, originX - extent * vx,
                   originY - extent * vy, originX + extent * vx, originY + extent * vy)
            stroke(context, Theme.axisZ, 1.4, originX, originY, originX, originY - step * 7)
        }

        onPaint: {
            const context = getContext("2d")
            context.reset()
            if (root.sketchView)
                paintSketchGrid(context)
            else
                paintModelGrid(context)
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
            text: uiSession.gridPlaneLabel + " plane  ·  Grid " + uiSession.gridSpacingLabel
            color: Theme.textMuted
            font.pixelSize: Theme.fontSmall
        }
    }
}
