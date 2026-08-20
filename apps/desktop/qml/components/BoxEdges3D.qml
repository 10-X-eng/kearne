pragma ComponentBehavior: Bound

import QtQuick
import QtQuick3D
import Kearne.UI

Node {
    id: root

    property vector3d dimensions: Qt.vector3d(100, 100, 100)
    property real thickness: 0.7
    property bool selected: false

    function edges() {
        const x = dimensions.x / 2
        const y = dimensions.y / 2
        const z = dimensions.z / 2
        const t = thickness / 100
        const alongX = Qt.vector3d(dimensions.x / 100, t, t)
        const alongY = Qt.vector3d(t, dimensions.y / 100, t)
        const alongZ = Qt.vector3d(t, t, dimensions.z / 100)
        return [
            { p: Qt.vector3d(0, -y, -z), s: alongX },
            { p: Qt.vector3d(0, -y, z), s: alongX },
            { p: Qt.vector3d(0, y, -z), s: alongX },
            { p: Qt.vector3d(0, y, z), s: alongX },
            { p: Qt.vector3d(-x, 0, -z), s: alongY },
            { p: Qt.vector3d(-x, 0, z), s: alongY },
            { p: Qt.vector3d(x, 0, -z), s: alongY },
            { p: Qt.vector3d(x, 0, z), s: alongY },
            { p: Qt.vector3d(-x, -y, 0), s: alongZ },
            { p: Qt.vector3d(-x, y, 0), s: alongZ },
            { p: Qt.vector3d(x, -y, 0), s: alongZ },
            { p: Qt.vector3d(x, y, 0), s: alongZ }
        ]
    }

    PrincipledMaterial {
        id: edgeMaterial

        lighting: PrincipledMaterial.NoLighting
        baseColor: root.selected ? Theme.accent : Theme.textMuted
    }

    Repeater3D {
        model: root.edges()

        Model {
            required property var modelData

            source: "#Cube"
            position: modelData.p
            scale: modelData.s
            materials: edgeMaterial
        }
    }
}
