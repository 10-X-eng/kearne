import QtQuick
import Kearne.UI

Rectangle {
    id: root

    required property string semanticId
    required property string semanticName
    required property real currentSize
    required property real minimumSize
    required property real maximumSize
    property int direction: 1
    property string semanticRole: "splitter"
    property var semanticActions: ["resize", "increase", "decrease", "focus"]
    property string semanticValue: String(Math.round(currentSize))
    property real dragOrigin: currentSize
    signal resizeRequested(real size)

    function bounded(size) {
        return Math.max(minimumSize, Math.min(maximumSize, Math.round(size)))
    }

    function moveDivider(delta) {
        resizeRequested(bounded(currentSize + direction * delta))
        return true
    }

    function performSemanticAction(action, value) {
        if (action === "focus") {
            forceActiveFocus()
            return true
        }
        if (action === "increase")
            return moveDivider(8)
        if (action === "decrease")
            return moveDivider(-8)
        if (action !== "resize")
            return false
        const requested = Number(value)
        if (!Number.isFinite(requested))
            return false
        resizeRequested(bounded(requested))
        return true
    }

    implicitWidth: 7
    color: Theme.transparent
    activeFocusOnTab: true
    Accessible.name: semanticName
    Accessible.description: "Resize with Left and Right Arrow keys"
    Accessible.id: semanticId
    Accessible.role: Accessible.Splitter
    Accessible.focusable: enabled && visible
    Accessible.onIncreaseAction: moveDivider(8)
    Accessible.onDecreaseAction: moveDivider(-8)

    Keys.onLeftPressed: moveDivider(-8)
    Keys.onRightPressed: moveDivider(8)

    Rectangle {
        anchors.centerIn: parent
        width: root.activeFocus || drag.active || hover.hovered ? 3 : Theme.separatorWidth
        height: parent.height
        color: root.activeFocus || drag.active ? Theme.focus
              : (hover.hovered ? Theme.accent : Theme.border)
    }

    HoverHandler { id: hover }

    DragHandler {
        id: drag
        target: null
        xAxis.enabled: true
        yAxis.enabled: false
        onActiveChanged: {
            if (active)
                root.dragOrigin = root.currentSize
        }
        onTranslationChanged: {
            if (active)
                root.resizeRequested(root.bounded(
                                         root.dragOrigin
                                         + root.direction * translation.x))
        }
    }
}
