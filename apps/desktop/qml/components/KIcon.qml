import QtQuick
import Kearne.UI

Canvas {
    id: root

    property string name: "command"
    property color color: Theme.text
    property color accentColor: Theme.accent
    property color softColor: Theme.accentSoft
    property real strokeWidth: Theme.iconStrokeWidth

    implicitWidth: Theme.iconSize
    implicitHeight: Theme.iconSize
    antialiasing: true

    onNameChanged: requestPaint()
    onColorChanged: requestPaint()
    onAccentColorChanged: requestPaint()
    onSoftColorChanged: requestPaint()
    onStrokeWidthChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    function paintPath(context, points, closed) {
        context.beginPath()
        context.moveTo(points[0], points[1])
        for (let index = 2; index < points.length; index += 2)
            context.lineTo(points[index], points[index + 1])
        if (closed)
            context.closePath()
        context.stroke()
    }

    function line(context, x1, y1, x2, y2) {
        paintPath(context, [x1, y1, x2, y2], false)
    }

    function rect(context, x, y, width, height, radius) {
        const r = Math.min(radius ?? 0, width / 2, height / 2)
        context.beginPath()
        context.moveTo(x + r, y)
        context.lineTo(x + width - r, y)
        context.quadraticCurveTo(x + width, y, x + width, y + r)
        context.lineTo(x + width, y + height - r)
        context.quadraticCurveTo(x + width, y + height, x + width - r, y + height)
        context.lineTo(x + r, y + height)
        context.quadraticCurveTo(x, y + height, x, y + height - r)
        context.lineTo(x, y + r)
        context.quadraticCurveTo(x, y, x + r, y)
        context.closePath()
        context.stroke()
    }

    function circle(context, x, y, radius) {
        context.beginPath()
        context.arc(x, y, radius, 0, Math.PI * 2)
        context.stroke()
    }

    function dot(context, x, y, radius) {
        context.beginPath()
        context.arc(x, y, radius, 0, Math.PI * 2)
        context.fill()
    }

    function fillPath(context, points) {
        context.beginPath()
        context.moveTo(points[0], points[1])
        for (let index = 2; index < points.length; index += 2)
            context.lineTo(points[index], points[index + 1])
        context.closePath()
        context.fill()
    }

    function useBase(context, multiplier) {
        context.strokeStyle = root.color
        context.fillStyle = root.color
        context.lineWidth = root.strokeWidth * (multiplier ?? 1) * 24
                            / Math.max(root.width, root.height)
    }

    function useAccent(context, multiplier) {
        context.strokeStyle = root.accentColor
        context.fillStyle = root.accentColor
        context.lineWidth = root.strokeWidth * (multiplier ?? 1) * 24
                            / Math.max(root.width, root.height)
    }

    function useSoft(context) {
        context.strokeStyle = root.softColor
        context.fillStyle = root.softColor
    }

    function cube(context) {
        paintPath(context, [4, 8, 12, 3.5, 20, 8, 12, 12.5, 4, 8, 4, 16, 12, 20.5,
                            20, 16, 20, 8], false)
        line(context, 12, 12.5, 12, 20.5)
    }

    function draw(context, icon) {
        switch (icon) {
        case "brand":
            paintPath(context, [12, 2.5, 21.5, 12, 12, 21.5, 2.5, 12], true)
            circle(context, 12, 12, 2.2)
            break
        case "home":
            paintPath(context, [3.5, 11, 12, 4, 20.5, 11], false)
            paintPath(context, [6, 9, 6, 20, 18, 20, 18, 9], false)
            break
        case "folder":
            paintPath(context, [3, 7, 9, 7, 11, 9, 21, 9, 20, 19, 4, 19, 3, 7], true)
            break
        case "file":
        case "sheet":
        case "drawing":
            paintPath(context, [6, 3, 15, 3, 19, 7, 19, 21, 6, 21, 6, 3], true)
            paintPath(context, [15, 3, 15, 7, 19, 7], false)
            if (icon !== "file") {
                line(context, 9, 12, 16, 12)
                line(context, 9, 16, 16, 16)
            }
            break
        case "add":
            circle(context, 12, 12, 8.5)
            line(context, 12, 8, 12, 16)
            line(context, 8, 12, 16, 12)
            break
        case "settings":
            circle(context, 12, 12, 3.2)
            circle(context, 12, 12, 7.5)
            line(context, 12, 2, 12, 4.5)
            line(context, 12, 19.5, 12, 22)
            line(context, 2, 12, 4.5, 12)
            line(context, 19.5, 12, 22, 12)
            break
        case "search":
            circle(context, 10.5, 10.5, 6.5)
            line(context, 15.5, 15.5, 21, 21)
            break
        case "save":
            rect(context, 4, 3, 16, 18, 1.5)
            rect(context, 8, 4, 8, 6, 0.5)
            rect(context, 8, 14, 8, 7, 0.5)
            break
        case "operations":
            line(context, 5, 5, 19, 5)
            line(context, 5, 12, 19, 12)
            line(context, 5, 19, 19, 19)
            dot(context, 9, 5, 1.8)
            dot(context, 15, 12, 1.8)
            dot(context, 11, 19, 1.8)
            break
        case "structure":
            line(context, 7, 6, 7, 18)
            line(context, 7, 8, 11, 8)
            line(context, 7, 16, 11, 16)
            rect(context, 3, 3, 4, 4, 0.5)
            rect(context, 11, 6, 9, 4, 0.5)
            rect(context, 11, 14, 9, 4, 0.5)
            break
        case "pan":
            paintPath(context, [8, 12, 8, 7, 10, 7, 10, 11, 10, 5, 12, 5, 12, 11,
                                12, 4, 14, 4, 14, 11, 14, 6, 16, 6, 16, 13], false)
            context.beginPath()
            context.moveTo(8, 11)
            context.bezierCurveTo(4, 8, 3, 11, 6, 15)
            context.bezierCurveTo(8, 19, 11, 20, 16, 19)
            context.bezierCurveTo(19, 18, 20, 15, 16, 13)
            context.stroke()
            break
        case "fit":
            paintPath(context, [9, 4, 4, 4, 4, 9], false)
            paintPath(context, [15, 4, 20, 4, 20, 9], false)
            paintPath(context, [4, 15, 4, 20, 9, 20], false)
            paintPath(context, [20, 15, 20, 20, 15, 20], false)
            break
        case "chevron":
            paintPath(context, [6, 9, 12, 15, 18, 9], false)
            break
        case "chevron-up":
            paintPath(context, [6, 15, 12, 9, 18, 15], false)
            break
        case "collapse-left":
            line(context, 19, 4, 19, 20)
            paintPath(context, [14, 7, 9, 12, 14, 17], false)
            break
        case "collapse-right":
            line(context, 5, 4, 5, 20)
            paintPath(context, [10, 7, 15, 12, 10, 17], false)
            break
        case "recovery":
        case "refresh":
        case "stale":
            context.beginPath()
            context.arc(12, 12, 8, -Math.PI * 0.2, Math.PI * 1.45)
            context.stroke()
            paintPath(context, [4, 8, 4, 3, 9, 4], false)
            break
        case "model":
        case "assemble":
            cube(context)
            if (icon === "assemble") {
                circle(context, 18.5, 17.5, 2.5)
                line(context, 16, 17.5, 21, 17.5)
            }
            break
        case "sheet-metal":
            paintPath(context, [3, 17, 9, 17, 9, 8, 15, 5, 21, 5], false)
            line(context, 9, 8, 15, 11)
            line(context, 15, 5, 15, 11)
            break
        case "bend":
            paintPath(context, [4, 18, 10, 18, 10, 10, 14, 6, 20, 6], false)
            context.beginPath()
            context.arc(14, 10, 4, Math.PI, Math.PI * 1.5)
            context.stroke()
            break
        case "unfold":
            paintPath(context, [3, 17, 8, 12, 13, 17, 21, 9], false)
            line(context, 8, 12, 8, 6)
            line(context, 13, 17, 13, 11)
            break
        case "cam":
            rect(context, 7, 3, 10, 5, 1)
            line(context, 9, 8, 9, 17)
            line(context, 15, 8, 15, 17)
            paintPath(context, [7, 17, 12, 21, 17, 17], false)
            break
        case "fastener":
            paintPath(context, [8, 3, 16, 3, 19, 6, 16, 9, 14, 9, 14, 20,
                                10, 20, 10, 9, 8, 9, 5, 6, 8, 3], true)
            line(context, 10, 12, 14, 12)
            line(context, 10, 16, 14, 16)
            break
        case "stock":
            cube(context)
            rect(context, 7, 9, 10, 7, 0.5)
            break
        case "face":
            line(context, 4, 18, 20, 18)
            rect(context, 8, 4, 8, 6, 1)
            line(context, 12, 10, 12, 15)
            line(context, 8, 15, 16, 15)
            break
        case "contour":
            rect(context, 5, 5, 14, 14, 2)
            paintPath(context, [3, 8, 3, 3, 8, 3], false)
            break
        case "pocket":
            rect(context, 4, 5, 16, 14, 2)
            rect(context, 8, 9, 8, 6, 1)
            break
        case "drill":
            paintPath(context, [9, 3, 15, 3, 15, 9, 12, 20, 9, 9, 9, 3], true)
            line(context, 9, 7, 15, 5)
            line(context, 9, 11, 14, 9)
            break
        case "toolpath":
            context.beginPath()
            context.moveTo(3, 18)
            context.bezierCurveTo(6, 5, 10, 19, 14, 8)
            context.bezierCurveTo(16, 3, 19, 7, 21, 4)
            context.stroke()
            dot(context, 3, 18, 1.4)
            paintPath(context, [18, 4, 21, 4, 21, 7], false)
            break
        case "sketch":
            rect(context, 4, 4, 16, 16, 1)
            paintPath(context, [7, 16, 10, 9, 17, 7], false)
            dot(context, 7, 16, 1.3)
            dot(context, 10, 9, 1.3)
            dot(context, 17, 7, 1.3)
            break
        case "simulate":
        case "chart":
            paintPath(context, [4, 19, 8, 14, 11, 16, 15, 8, 20, 5], false)
            line(context, 4, 4, 4, 20)
            line(context, 4, 20, 21, 20)
            break
        case "bom":
        case "columns":
            rect(context, 4, 4, 16, 16, 1)
            line(context, 9, 4, 9, 20)
            line(context, 15, 4, 15, 20)
            line(context, 4, 9, 20, 9)
            line(context, 4, 14.5, 20, 14.5)
            break
        case "versions":
        case "branch":
            line(context, 8, 5, 8, 19)
            context.beginPath()
            context.moveTo(8, 9)
            context.bezierCurveTo(8, 12, 16, 10, 16, 15)
            context.stroke()
            circle(context, 8, 5, 2)
            circle(context, 8, 19, 2)
            circle(context, 16, 16, 2)
            break
        case "agent":
            rect(context, 4, 6, 16, 14, 3)
            line(context, 12, 3, 12, 6)
            circle(context, 9, 12, 1)
            circle(context, 15, 12, 1)
            line(context, 8, 16, 16, 16)
            break
        case "code":
            paintPath(context, [9, 6, 3, 12, 9, 18], false)
            paintPath(context, [15, 6, 21, 12, 15, 18], false)
            line(context, 14, 4, 10, 20)
            break
        case "extrude":
            useSoft(context)
            fillPath(context, [6, 9, 9, 5, 21, 5, 18, 9])
            useBase(context)
            rect(context, 6, 9, 12, 10, 1)
            paintPath(context, [6, 9, 9, 5, 21, 5, 18, 9], false)
            line(context, 18, 9, 21, 5)
            useAccent(context)
            line(context, 21, 5, 21, 15)
            fillPath(context, [18.5, 13, 21, 16.5, 23.5, 13])
            break
        case "revolve":
            useSoft(context)
            context.beginPath()
            context.arc(11, 12, 5, -Math.PI * 0.5, Math.PI * 0.5)
            context.fill()
            useAccent(context)
            context.beginPath()
            context.arc(11, 12, 7, -Math.PI * 0.65, Math.PI * 0.65)
            context.stroke()
            fillPath(context, [16, 4.5, 21, 5.8, 18.8, 10])
            useBase(context)
            line(context, 8, 6, 8, 18)
            break
        case "path":
            context.beginPath()
            context.moveTo(4, 18)
            context.bezierCurveTo(7, 5, 16, 20, 20, 6)
            context.stroke()
            dot(context, 4, 18, 1.5)
            dot(context, 20, 6, 1.5)
            break
        case "layers":
            paintPath(context, [3, 9, 12, 4, 21, 9, 12, 14, 3, 9], true)
            paintPath(context, [4, 13, 12, 18, 20, 13], false)
            paintPath(context, [5, 17, 12, 21, 19, 17], false)
            break
        case "round":
            paintPath(context, [5, 20, 5, 9, 9, 5, 20, 5], false)
            context.beginPath()
            context.arc(10, 10, 5, Math.PI, Math.PI * 1.5)
            context.stroke()
            break
        case "chamfer":
            paintPath(context, [4, 20, 4, 9, 9, 4, 20, 4], false)
            line(context, 4, 9, 9, 4)
            break
        case "shell":
            cube(context)
            paintPath(context, [8, 10, 12, 8, 16, 10, 12, 12], true)
            break
        case "hole":
            useSoft(context)
            dot(context, 12, 12, 8)
            useBase(context)
            circle(context, 12, 12, 8)
            useAccent(context)
            circle(context, 12, 12, 3)
            dot(context, 12, 12, 1)
            break
        case "grid":
            for (let x = 7; x <= 17; x += 5)
                for (let y = 7; y <= 17; y += 5) {
                    useSoft(context)
                    dot(context, x, y, 2.2)
                    useAccent(context)
                    circle(context, x, y, 1.4)
                }
            break
        case "plane":
            useSoft(context)
            fillPath(context, [3.5, 14, 9, 5, 20.5, 9, 15, 18])
            useBase(context)
            paintPath(context, [3.5, 14, 9, 5, 20.5, 9, 15, 18, 3.5, 14], false)
            useAccent(context)
            line(context, 12, 11.5, 17, 3.5)
            fillPath(context, [17, 3.5, 16.8, 8, 13.5, 5.9])
            dot(context, 12, 11.5, 1.3)
            break
        case "mirror":
            useAccent(context)
            line(context, 12, 3, 12, 21)
            useBase(context)
            paintPath(context, [9, 6, 4, 9, 4, 17, 9, 19], false)
            useSoft(context)
            fillPath(context, [15, 6, 20, 9, 20, 17, 15, 19])
            useBase(context)
            paintPath(context, [15, 6, 20, 9, 20, 17, 15, 19], false)
            break
        case "measure":
            paintPath(context, [4, 17, 17, 4, 21, 8, 8, 21, 4, 17], true)
            useAccent(context)
            line(context, 9, 15, 11, 17)
            line(context, 12, 12, 14, 14)
            line(context, 15, 9, 17, 11)
            break
        case "dimension":
            useBase(context, 0.8)
            line(context, 5, 5, 5, 20)
            line(context, 19, 5, 19, 20)
            line(context, 4, 19, 20, 19)
            useAccent(context)
            line(context, 5, 9, 19, 9)
            fillPath(context, [5, 9, 8.5, 6.8, 8.5, 11.2])
            fillPath(context, [19, 9, 15.5, 6.8, 15.5, 11.2])
            break
        case "section":
            paintPath(context, [4, 19, 4, 5, 20, 5], false)
            for (let index = 0; index < 4; ++index)
                line(context, 6 + index * 4, 6, 4 + index * 4, 10)
            break
        case "point":
            useBase(context, 0.7)
            line(context, 12, 4, 12, 20)
            line(context, 4, 12, 20, 12)
            useSoft(context)
            dot(context, 12, 12, 3.8)
            useAccent(context)
            circle(context, 12, 12, 2.2)
            dot(context, 12, 12, 1)
            break
        case "line":
            useAccent(context)
            line(context, 5, 19, 19, 5)
            useBase(context)
            dot(context, 5, 19, 1.7)
            dot(context, 19, 5, 1.7)
            break
        case "polyline":
            useAccent(context)
            paintPath(context, [3.5, 18.5, 9, 8, 14, 15, 20.5, 5], false)
            useBase(context)
            dot(context, 3.5, 18.5, 1.6)
            dot(context, 9, 8, 1.6)
            dot(context, 14, 15, 1.6)
            dot(context, 20.5, 5, 1.6)
            break
        case "rectangle":
            useSoft(context)
            context.fillRect(4, 6, 16, 12)
            useBase(context)
            rect(context, 4, 6, 16, 12, 0.5)
            useAccent(context)
            dot(context, 4, 6, 1.2)
            dot(context, 20, 18, 1.2)
            break
        case "circle":
            useSoft(context)
            dot(context, 12, 12, 8)
            useBase(context)
            circle(context, 12, 12, 8)
            useAccent(context)
            dot(context, 12, 12, 1.4)
            break
        case "arc":
            useBase(context, 0.65)
            line(context, 12, 14, 5, 10.5)
            line(context, 12, 14, 19, 10.5)
            useAccent(context)
            context.beginPath()
            context.arc(12, 14, 8, Math.PI * 1.15, Math.PI * 1.85)
            context.stroke()
            useBase(context)
            dot(context, 4.9, 10.4, 1.4)
            dot(context, 19.1, 10.4, 1.4)
            useAccent(context)
            dot(context, 12, 14, 1.1)
            break
        case "trim":
            useSoft(context)
            dot(context, 7, 17, 3)
            dot(context, 17, 17, 3)
            useBase(context)
            circle(context, 7, 17, 3)
            circle(context, 17, 17, 3)
            line(context, 9, 15, 17, 5)
            line(context, 15, 15, 7, 5)
            useAccent(context)
            line(context, 10.5, 11.5, 13.5, 8.5)
            break
        case "coincident":
            useBase(context)
            line(context, 4, 19, 12, 11)
            line(context, 12, 11, 20, 17)
            useSoft(context)
            dot(context, 12, 11, 4)
            useAccent(context)
            circle(context, 12, 11, 2.4)
            dot(context, 12, 11, 1)
            break
        case "horizontal":
            useBase(context, 0.75)
            line(context, 5, 6, 5, 18)
            line(context, 19, 6, 19, 18)
            useAccent(context)
            line(context, 4, 12, 20, 12)
            fillPath(context, [4, 12, 7.5, 9.8, 7.5, 14.2])
            fillPath(context, [20, 12, 16.5, 9.8, 16.5, 14.2])
            break
        case "vertical":
            useBase(context, 0.75)
            line(context, 6, 5, 18, 5)
            line(context, 6, 19, 18, 19)
            useAccent(context)
            line(context, 12, 4, 12, 20)
            fillPath(context, [12, 4, 9.8, 7.5, 14.2, 7.5])
            fillPath(context, [12, 20, 9.8, 16.5, 14.2, 16.5])
            break
        case "equal":
            useBase(context)
            line(context, 4, 18, 10, 6)
            line(context, 14, 18, 20, 6)
            useAccent(context)
            line(context, 5.5, 11.5, 9, 13.2)
            line(context, 15.5, 11.5, 19, 13.2)
            break
        case "parallel":
            useBase(context)
            line(context, 5, 17, 13, 5)
            line(context, 11, 19, 19, 7)
            useAccent(context)
            paintPath(context, [6.5, 10.5, 9, 10, 8.5, 12.5], false)
            paintPath(context, [14.5, 12.5, 17, 12, 16.5, 14.5], false)
            break
        case "perpendicular":
            useBase(context)
            line(context, 5, 5, 5, 19)
            line(context, 5, 19, 20, 19)
            useAccent(context)
            paintPath(context, [5, 14, 10, 14, 10, 19], false)
            break
        case "tangent":
            useBase(context)
            circle(context, 10, 13, 6)
            line(context, 4, 5, 20, 5)
            useAccent(context)
            dot(context, 10, 7, 1.8)
            break
        case "concentric":
            useBase(context)
            circle(context, 12, 12, 8)
            useAccent(context)
            circle(context, 12, 12, 4)
            dot(context, 12, 12, 1.2)
            break
        case "midpoint":
            useBase(context)
            line(context, 4, 16, 20, 8)
            dot(context, 4, 16, 1.4)
            dot(context, 20, 8, 1.4)
            useAccent(context)
            fillPath(context, [10, 11.5, 13.5, 9.7, 13.5, 13.3])
            break
        case "fixed":
            useBase(context)
            line(context, 5, 18, 19, 7)
            dot(context, 5, 18, 1.4)
            dot(context, 19, 7, 1.4)
            useAccent(context)
            rect(context, 9, 11, 7, 7, 1)
            context.beginPath()
            context.arc(12.5, 11, 2.5, Math.PI, Math.PI * 2)
            context.stroke()
            break
        case "collinear":
            useBase(context)
            line(context, 4, 18, 20, 6)
            useAccent(context)
            dot(context, 5, 17.2, 1.7)
            dot(context, 12, 12, 1.7)
            dot(context, 19, 6.8, 1.7)
            break
        case "joint":
            circle(context, 7, 12, 3)
            circle(context, 17, 12, 3)
            line(context, 10, 12, 14, 12)
            break
        case "play":
            paintPath(context, [8, 5, 19, 12, 8, 19], true)
            break
        case "interference":
            rect(context, 3, 6, 11, 11, 1)
            rect(context, 10, 10, 11, 11, 1)
            break
        case "material":
            circle(context, 12, 12, 8)
            context.beginPath()
            context.arc(12, 12, 8, -Math.PI / 2, Math.PI / 2)
            context.fill()
            break
        case "mesh":
            paintPath(context, [12, 3, 21, 20, 3, 20, 12, 3], true)
            line(context, 7.5, 12, 16.5, 12)
            line(context, 7.5, 12, 12, 20)
            line(context, 16.5, 12, 12, 20)
            break
        case "solve":
        case "check":
            paintPath(context, [4, 12, 9, 17, 20, 6], false)
            break
        case "view":
        case "preview":
        case "inspect":
            context.beginPath()
            context.moveTo(3, 12)
            context.bezierCurveTo(7, 5, 17, 5, 21, 12)
            context.bezierCurveTo(17, 19, 7, 19, 3, 12)
            context.closePath()
            context.stroke()
            circle(context, 12, 12, 2.5)
            break
        case "target":
            circle(context, 12, 12, 8)
            circle(context, 12, 12, 4)
            dot(context, 12, 12, 1.5)
            break
        case "export":
            rect(context, 4, 11, 16, 10, 1)
            line(context, 12, 3, 12, 15)
            paintPath(context, [8, 7, 12, 3, 16, 7], false)
            break
        case "checkpoint":
            circle(context, 12, 12, 8)
            circle(context, 12, 12, 2)
            break
        case "compare":
            paintPath(context, [8, 5, 4, 9, 8, 13], false)
            line(context, 4, 9, 18, 9)
            paintPath(context, [16, 11, 20, 15, 16, 19], false)
            line(context, 6, 15, 20, 15)
            break
        case "merge":
            line(context, 6, 4, 6, 9)
            context.beginPath()
            context.moveTo(6, 9)
            context.bezierCurveTo(6, 14, 12, 12, 12, 18)
            context.stroke()
            line(context, 18, 4, 18, 9)
            context.beginPath()
            context.moveTo(18, 9)
            context.bezierCurveTo(18, 14, 12, 12, 12, 18)
            context.stroke()
            break
        case "plan":
            rect(context, 5, 4, 14, 17, 1)
            line(context, 8, 9, 10, 11)
            line(context, 10, 11, 14, 7)
            line(context, 8, 16, 16, 16)
            break
        case "review":
            rect(context, 4, 4, 16, 16, 1)
            paintPath(context, [7, 12, 10, 15, 17, 8], false)
            break
        case "clock":
        case "loading":
            circle(context, 12, 12, 8)
            line(context, 12, 7, 12, 12)
            line(context, 12, 12, 16, 14)
            break
        case "error":
        case "unavailable":
            circle(context, 12, 12, 8)
            line(context, 7, 7, 17, 17)
            line(context, 17, 7, 7, 17)
            break
        case "lock":
            rect(context, 5, 10, 14, 11, 2)
            context.beginPath()
            context.arc(12, 10, 5, Math.PI, Math.PI * 2)
            context.stroke()
            break
        case "shield":
            paintPath(context, [12, 3, 20, 6, 19, 14, 12, 21, 5, 14, 4, 6, 12, 3], true)
            line(context, 8, 8, 16, 16)
            break
        case "empty":
            rect(context, 4, 5, 16, 14, 2)
            line(context, 8, 9, 16, 9)
            break
        default:
            rect(context, 4, 4, 16, 16, 2)
            line(context, 8, 12, 16, 12)
            line(context, 12, 8, 12, 16)
        }
    }

    onPaint: {
        const context = getContext("2d")
        context.reset()
        context.scale(width / 24, height / 24)
        context.lineCap = "round"
        context.lineJoin = "round"
        useBase(context)
        draw(context, root.name)
    }
}
