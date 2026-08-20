pragma ComponentBehavior: Bound

import QtQuick
import Kearne.UI

Item {
    id: root

    property string semanticId: "viewport.view_cube"
    property string semanticName: "Orientation cube"
    property string semanticRole: "group"
    property var semanticActions: ["choose", "home", "fit", "pointerClick",
                                   "pointerDrag"]
    property var semanticOptions: [
        "front", "back", "left", "right", "top", "bottom",
        "front-top", "front-bottom", "front-left", "front-right",
        "back-top", "back-bottom", "back-left", "back-right",
        "top-left", "top-right", "bottom-left", "bottom-right",
        "front-top-left", "front-top-right", "front-bottom-left",
        "front-bottom-right", "back-top-left", "back-top-right",
        "back-bottom-left", "back-bottom-right"
    ]
    property string semanticValue: App.camera.viewName
    property string hoveredView: ""
    property var pickRegions: []

    function performSemanticAction(action, value) {
        if (action === "home") {
            App.camera.setView("isometric")
            App.camera.fit()
            return true
        }
        if (action === "fit") {
            App.camera.fit()
            return true
        }
        if (action === "pointerDrag") {
            App.camera.orbit(24, -12)
            return true
        }
        return action === "choose"
               && semanticOptions.includes(String(value))
               && App.camera.setView(String(value))
    }

    function add(first, second) {
        return [first[0] + second[0], first[1] + second[1],
                first[2] + second[2]]
    }

    function scaled(vector, amount) {
        return [vector[0] * amount, vector[1] * amount,
                vector[2] * amount]
    }

    function combined(...vectors) {
        let result = [0, 0, 0]
        for (const vector of vectors)
            result = add(result, vector)
        return result
    }

    function face(view, normal, u, v) {
        const inset = 0.72
        return {
            view: view,
            kind: "face",
            normal: normal,
            points: [
                combined(normal, scaled(u, -inset), scaled(v, -inset)),
                combined(normal, scaled(u, inset), scaled(v, -inset)),
                combined(normal, scaled(u, inset), scaled(v, inset)),
                combined(normal, scaled(u, -inset), scaled(v, inset))
            ]
        }
    }

    function edge(view, first, second, along) {
        const inset = 0.72
        return {
            view: view,
            kind: "edge",
            normal: add(first, second),
            points: [
                combined(first, scaled(second, inset),
                         scaled(along, -inset)),
                combined(first, scaled(second, inset),
                         scaled(along, inset)),
                combined(scaled(first, inset), second,
                         scaled(along, inset)),
                combined(scaled(first, inset), second,
                         scaled(along, -inset))
            ]
        }
    }

    function corner(view, signs) {
        const inset = 0.72
        const x = [signs[0], 0, 0]
        const y = [0, signs[1], 0]
        const z = [0, 0, signs[2]]
        return {
            view: view,
            kind: "corner",
            normal: signs,
            points: [
                combined(x, scaled(y, inset), scaled(z, inset)),
                combined(scaled(x, inset), y, scaled(z, inset)),
                combined(scaled(x, inset), scaled(y, inset), z)
            ]
        }
    }

    function surfaces() {
        const x = [1, 0, 0]
        const left = [-1, 0, 0]
        const y = [0, 1, 0]
        const bottom = [0, -1, 0]
        const z = [0, 0, 1]
        const back = [0, 0, -1]
        return [
            face("front", z, x, y), face("back", back, left, y),
            face("left", left, z, y), face("right", x, back, y),
            face("top", y, x, back), face("bottom", bottom, x, z),
            edge("front-top", z, y, x),
            edge("front-bottom", z, bottom, x),
            edge("front-left", z, left, y),
            edge("front-right", z, x, y),
            edge("back-top", back, y, x),
            edge("back-bottom", back, bottom, x),
            edge("back-left", back, left, y),
            edge("back-right", back, x, y),
            edge("top-left", y, left, z),
            edge("top-right", y, x, z),
            edge("bottom-left", bottom, left, z),
            edge("bottom-right", bottom, x, z),
            corner("front-top-left", [-1, 1, 1]),
            corner("front-top-right", [1, 1, 1]),
            corner("front-bottom-left", [-1, -1, 1]),
            corner("front-bottom-right", [1, -1, 1]),
            corner("back-top-left", [-1, 1, -1]),
            corner("back-top-right", [1, 1, -1]),
            corner("back-bottom-left", [-1, -1, -1]),
            corner("back-bottom-right", [1, -1, -1])
        ]
    }

    function rotated(vector) {
        const xAngle = -App.camera.pitch * Math.PI / 180
        const yAngle = App.camera.yaw * Math.PI / 180
        const zAngle = App.camera.roll * Math.PI / 180
        const cosineX = Math.cos(xAngle)
        const sineX = Math.sin(xAngle)
        const cosineY = Math.cos(yAngle)
        const sineY = Math.sin(yAngle)
        const cosineZ = Math.cos(zAngle)
        const sineZ = Math.sin(zAngle)
        const afterX = [vector[0],
                        vector[1] * cosineX - vector[2] * sineX,
                        vector[1] * sineX + vector[2] * cosineX]
        const afterY = [afterX[0] * cosineY + afterX[2] * sineY,
                        afterX[1],
                        -afterX[0] * sineY + afterX[2] * cosineY]
        return [afterY[0] * cosineZ - afterY[1] * sineZ,
                afterY[0] * sineZ + afterY[1] * cosineZ,
                afterY[2]]
    }

    function projectedRegions() {
        const center = 66
        const scale = 35
        const result = []
        for (const surface of surfaces()) {
            if (rotated(surface.normal)[2] <= 0.015)
                continue
            let depth = 0
            const points = surface.points.map(point => {
                const transformed = rotated(point)
                depth += transformed[2]
                return [center + transformed[0] * scale,
                        center - transformed[1] * scale]
            })
            result.push({
                view: surface.view,
                kind: surface.kind,
                points: points,
                depth: depth / points.length
            })
        }
        result.sort((first, second) => first.depth - second.depth)
        return result
    }

    function contains(points, x, y) {
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

    function viewAt(x, y) {
        for (let index = pickRegions.length - 1; index >= 0; --index) {
            if (contains(pickRegions[index].points, x, y))
                return pickRegions[index].view
        }
        return ""
    }

    function displayName(view) {
        return String(view).split("-").join(" ").toUpperCase()
    }

    width: 132
    height: 132
    activeFocusOnTab: true
    Accessible.id: semanticId
    Accessible.name: semanticName
    Accessible.description: "Choose a face, edge, or corner; drag to orbit"
    Accessible.role: Accessible.Grouping
    Accessible.focusable: enabled && visible

    component NudgeButton: Rectangle {
        id: nudge

        required property string semanticId
        required property string semanticName
        property string semanticRole: "button"
        property var semanticActions: ["invoke"]
        property string iconName: "chevron-up"
        property real iconRotation: 0
        signal invoked()

        function performSemanticAction(action, value) {
            if (action !== "invoke")
                return false
            invoked()
            return true
        }

        width: 22
        height: 22
        radius: 11
        color: pointer.containsMouse ? Theme.surface : Theme.transparent
        border.width: pointer.containsMouse ? Theme.separatorWidth : 0
        border.color: Theme.accent
        Accessible.id: semanticId
        Accessible.name: semanticName
        Accessible.role: Accessible.Button
        Accessible.focusable: true
        Accessible.onPressAction: invoked()

        KIcon {
            anchors.centerIn: parent
            width: 13
            height: 13
            name: nudge.iconName
            color: pointer.containsMouse ? Theme.accent : Theme.textMuted
            transform: Rotation {
                origin.x: 6.5
                origin.y: 6.5
                angle: nudge.iconRotation
            }
        }

        MouseArea {
            id: pointer

            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: nudge.invoked()
        }
    }

    Canvas {
        id: cube

        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        width: 132
        height: 132
        antialiasing: true

        function polygon(context, region) {
            context.beginPath()
            context.moveTo(region.points[0][0], region.points[0][1])
            for (let index = 1; index < region.points.length; ++index)
                context.lineTo(region.points[index][0], region.points[index][1])
            context.closePath()
        }

        function centerOf(points) {
            let x = 0
            let y = 0
            for (const point of points) {
                x += point[0]
                y += point[1]
            }
            return [x / points.length, y / points.length]
        }

        function signedArea(points) {
            let area = 0
            for (let current = 0, previous = points.length - 1;
                 current < points.length; previous = current++) {
                area += points[previous][0] * points[current][1]
                        - points[current][0] * points[previous][1]
            }
            return area / 2
        }

        function faceLabel(context, region) {
            const area = Math.abs(signedArea(region.points))
            if (area < 140)
                return
            const center = centerOf(region.points)
            const edgeX = region.points[1][0] - region.points[0][0]
            const edgeY = region.points[1][1] - region.points[0][1]
            let angle = Math.atan2(edgeY, edgeX)
            if (angle > Math.PI / 2)
                angle -= Math.PI
            else if (angle < -Math.PI / 2)
                angle += Math.PI
            const edgeLength = Math.hypot(edgeX, edgeY)
            context.save()
            context.translate(center[0], center[1])
            context.rotate(angle)
            context.font = Theme.fontWeightStrong + " 7px sans-serif"
            context.fillStyle = Theme.textMuted
            context.textAlign = "center"
            context.textBaseline = "middle"
            context.fillText(root.displayName(region.view), 0, 0,
                             Math.max(16, edgeLength * 0.78))
            context.restore()
        }

        function axis(context, vector, color, label) {
            const direction = root.rotated(vector)
            const originX = 19
            const originY = 108
            const endX = originX + direction[0] * 14
            const endY = originY - direction[1] * 14
            context.beginPath()
            context.moveTo(originX, originY)
            context.lineTo(endX, endY)
            context.strokeStyle = color
            context.lineWidth = 1.6
            context.stroke()
            context.fillStyle = color
            context.font = Theme.fontWeightStrong + " 7px sans-serif"
            context.textAlign = "center"
            context.textBaseline = "middle"
            context.fillText(label, endX + direction[0] * 5,
                             endY - direction[1] * 5)
        }

        onPaint: {
            const context = getContext("2d")
            context.reset()
            context.clearRect(0, 0, width, height)
            const regions = root.projectedRegions()
            root.pickRegions = regions
            for (const region of regions) {
                polygon(context, region)
                context.fillStyle = region.view === root.hoveredView
                                    ? Theme.accentSoft
                                    : (region.kind === "face"
                                       ? Theme.surfaceRaised
                                       : (region.kind === "edge"
                                          ? Theme.surfaceMuted
                                          : Theme.border))
                context.fill()
                context.strokeStyle = region.view === root.hoveredView
                                      ? Theme.accent : Theme.borderStrong
                context.lineWidth = region.view === root.hoveredView ? 1.7 : 1
                context.stroke()
            }
            for (const region of regions) {
                if (region.kind === "face")
                    faceLabel(context, region)
            }
            axis(context, [1, 0, 0], Theme.axisX, "X")
            axis(context, [0, 0, 1], Theme.axisY, "Y")
            axis(context, [0, 1, 0], Theme.axisZ, "Z")
        }

        Connections {
            target: App.camera
            function onCameraChanged() { cube.requestPaint() }
        }

        Connections {
            target: App.themes
            function onTokensChanged() { cube.requestPaint() }
        }

        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
    }

    MouseArea {
        id: cubePointer

        anchors.fill: cube
        acceptedButtons: Qt.LeftButton
        hoverEnabled: true
        cursorShape: dragging ? Qt.ClosedHandCursor
                              : (root.hoveredView.length > 0
                                 ? Qt.PointingHandCursor : Qt.ArrowCursor)
        property real pressX: 0
        property real pressY: 0
        property real previousX: 0
        property real previousY: 0
        property string pressedView: ""
        property bool dragging: false

        onPressed: mouse => {
            pressX = mouse.x
            pressY = mouse.y
            previousX = mouse.x
            previousY = mouse.y
            pressedView = root.viewAt(mouse.x, mouse.y)
            dragging = false
            mouse.accepted = pressedView.length > 0
            if (mouse.accepted)
                root.forceActiveFocus()
        }
        onPositionChanged: mouse => {
            const dx = mouse.x - previousX
            const dy = mouse.y - previousY
            previousX = mouse.x
            previousY = mouse.y
            if (mouse.buttons & Qt.LeftButton) {
                if (!dragging && Math.hypot(mouse.x - pressX,
                                            mouse.y - pressY) >= 4)
                    dragging = true
                if (dragging) {
                    root.hoveredView = ""
                    App.camera.orbit(dx, dy)
                }
            } else {
                root.hoveredView = root.viewAt(mouse.x, mouse.y)
                cube.requestPaint()
            }
        }
        onReleased: mouse => {
            const releasedView = root.viewAt(mouse.x, mouse.y)
            if (!dragging && releasedView.length > 0
                    && releasedView === pressedView)
                App.camera.setView(releasedView)
            dragging = false
            pressedView = ""
            root.hoveredView = releasedView
            cube.requestPaint()
        }
        onDoubleClicked: mouse => {
            const view = root.viewAt(mouse.x, mouse.y)
            if (view.length > 0) {
                App.camera.setView(view)
                App.camera.fit()
            }
        }
        onCanceled: {
            dragging = false
            pressedView = ""
            root.hoveredView = ""
            cube.requestPaint()
        }
        onExited: {
            if (!dragging) {
                root.hoveredView = ""
                cube.requestPaint()
            }
        }
    }

    NudgeButton {
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        semanticId: "viewport.camera.turn_up"
        semanticName: "Turn view up"
        onInvoked: App.camera.turn(0, 45)
    }

    NudgeButton {
        anchors.top: parent.top
        anchors.topMargin: 110
        anchors.horizontalCenter: parent.horizontalCenter
        semanticId: "viewport.camera.turn_down"
        semanticName: "Turn view down"
        iconRotation: 180
        onInvoked: App.camera.turn(0, -45)
    }

    NudgeButton {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.topMargin: 55
        semanticId: "viewport.camera.turn_left"
        semanticName: "Turn view left"
        iconRotation: -90
        onInvoked: App.camera.turn(-45, 0)
    }

    NudgeButton {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: 55
        semanticId: "viewport.camera.turn_right"
        semanticName: "Turn view right"
        iconRotation: 90
        onInvoked: App.camera.turn(45, 0)
    }

    NudgeButton {
        anchors.top: parent.top
        anchors.left: parent.left
        semanticId: "viewport.camera.fit"
        semanticName: "Fit model in viewport"
        iconName: "fit"
        onInvoked: App.camera.fit()
    }

    NudgeButton {
        anchors.top: parent.top
        anchors.right: parent.right
        semanticId: "viewport.camera.home"
        semanticName: "Home isometric view"
        iconName: "home"
        onInvoked: {
            App.camera.setView("isometric")
            App.camera.fit()
        }
    }
}
