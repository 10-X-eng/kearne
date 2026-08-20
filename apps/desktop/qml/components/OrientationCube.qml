pragma ComponentBehavior: Bound

import QtQuick
import QtQuick3D
import Kearne.UI

Item {
    id: root

    property string semanticId: "viewport.view_cube"
    property string semanticName: "Orientation cube"
    property string semanticRole: "group"
    property var semanticActions: ["choose", "fit", "pointerClick"]
    property string semanticValue: App.camera.viewName
    property string hoveredView: ""
    property string cameraState: App.camera.state

    function performSemanticAction(action, value) {
        if (action === "fit") {
            App.camera.fit()
            return true
        }
        return action === "choose" && App.camera.setView(String(value))
    }

    function viewAt(x, y) {
        const hit = cubeView.pick(x, y)
        return hit.objectHit === null ? "" : hit.objectHit.objectName
    }

    function faceCenter(face) {
        cameraState
        return cubeView.mapFrom3DScene(
                    face.mapPositionToScene(Qt.vector3d(0, 0, 0)))
    }

    function faceIsVisible(face, normal) {
        cameraState
        return face.mapDirectionToScene(normal).z > 0.05
    }

    width: 104
    height: 132
    activeFocusOnTab: true
    Accessible.id: semanticId
    Accessible.name: semanticName
    Accessible.description: "Click a face for a standard view"
    Accessible.role: Accessible.Grouping
    Accessible.focusable: enabled && visible

    Rectangle {
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        width: 104
        height: 104
        radius: Theme.radius
        color: Theme.surface
        border.color: Theme.border

        View3D {
            id: cubeView

            anchors.fill: parent
            anchors.margins: 4
            camera: cubeCamera
            environment: SceneEnvironment {
                backgroundMode: SceneEnvironment.Transparent
                antialiasingMode: SceneEnvironment.MSAA
                antialiasingQuality: SceneEnvironment.High
            }

            OrthographicCamera {
                id: cubeCamera

                z: 300
                clipNear: 1
                clipFar: 600
                horizontalMagnification: 1.05
                verticalMagnification: 1.05
            }

            DirectionalLight {
                eulerRotation: Qt.vector3d(-35, -35, 0)
                brightness: 1.2
                ambientColor: "#758097"
            }

            PrincipledMaterial {
                id: edgeMaterial

                baseColor: Theme.borderStrong
                roughness: 0.58
            }

            PrincipledMaterial {
                id: frontMaterial

                baseColor: Theme.surfaceRaised
                roughness: 0.52
            }

            PrincipledMaterial {
                id: sideMaterial

                baseColor: Theme.surfaceMuted
                roughness: 0.52
            }

            PrincipledMaterial {
                id: topMaterial

                baseColor: Theme.surface
                roughness: 0.52
            }

            PrincipledMaterial {
                id: hoveredMaterial

                baseColor: Theme.accentSoft
                roughness: 0.45
            }

            Node {
                eulerRotation: Qt.vector3d(-App.camera.pitch,
                                           App.camera.yaw,
                                           App.camera.roll)

                Model {
                    source: "#Cube"
                    scale: Qt.vector3d(0.64, 0.64, 0.64)
                    materials: edgeMaterial
                }

                Model {
                    id: frontFace
                    objectName: "front"
                    source: "#Cube"
                    position: Qt.vector3d(0, 0, 32.5)
                    scale: Qt.vector3d(0.6, 0.6, 0.01)
                    pickable: true
                    materials: root.hoveredView === objectName
                               ? hoveredMaterial : frontMaterial
                }

                Model {
                    id: backFace
                    objectName: "back"
                    source: "#Cube"
                    position: Qt.vector3d(0, 0, -32.5)
                    scale: Qt.vector3d(0.6, 0.6, 0.01)
                    pickable: true
                    materials: root.hoveredView === objectName
                               ? hoveredMaterial : sideMaterial
                }

                Model {
                    id: topFace
                    objectName: "top"
                    source: "#Cube"
                    position: Qt.vector3d(0, 32.5, 0)
                    scale: Qt.vector3d(0.6, 0.01, 0.6)
                    pickable: true
                    materials: root.hoveredView === objectName
                               ? hoveredMaterial : topMaterial
                }

                Model {
                    id: bottomFace
                    objectName: "bottom"
                    source: "#Cube"
                    position: Qt.vector3d(0, -32.5, 0)
                    scale: Qt.vector3d(0.6, 0.01, 0.6)
                    pickable: true
                    materials: root.hoveredView === objectName
                               ? hoveredMaterial : sideMaterial
                }

                Model {
                    id: rightFace
                    objectName: "right"
                    source: "#Cube"
                    position: Qt.vector3d(32.5, 0, 0)
                    scale: Qt.vector3d(0.01, 0.6, 0.6)
                    pickable: true
                    materials: root.hoveredView === objectName
                               ? hoveredMaterial : sideMaterial
                }

                Model {
                    id: leftFace
                    objectName: "left"
                    source: "#Cube"
                    position: Qt.vector3d(-32.5, 0, 0)
                    scale: Qt.vector3d(0.01, 0.6, 0.6)
                    pickable: true
                    materials: root.hoveredView === objectName
                               ? hoveredMaterial : sideMaterial
                }
            }
        }

        MouseArea {
            anchors.fill: parent
            anchors.margins: 4
            hoverEnabled: true
            cursorShape: root.hoveredView.length > 0
                         ? Qt.PointingHandCursor : Qt.ArrowCursor
            onPositionChanged: mouse => root.hoveredView = root.viewAt(mouse.x,
                                                                       mouse.y)
            onExited: root.hoveredView = ""
            onClicked: mouse => {
                const view = root.viewAt(mouse.x, mouse.y)
                if (view.length > 0)
                    App.camera.setView(view)
            }
        }

        Repeater {
            model: [
                { label: "FRONT", face: frontFace,
                  normal: Qt.vector3d(0, 0, 1) },
                { label: "BACK", face: backFace,
                  normal: Qt.vector3d(0, 0, -1) },
                { label: "TOP", face: topFace,
                  normal: Qt.vector3d(0, 1, 0) },
                { label: "BOTTOM", face: bottomFace,
                  normal: Qt.vector3d(0, -1, 0) },
                { label: "RIGHT", face: rightFace,
                  normal: Qt.vector3d(1, 0, 0) },
                { label: "LEFT", face: leftFace,
                  normal: Qt.vector3d(-1, 0, 0) }
            ]

            Text {
                required property var modelData
                property vector3d center: root.faceCenter(modelData.face)

                x: 4 + center.x - width / 2
                y: 4 + center.y - height / 2
                visible: root.faceIsVisible(modelData.face, modelData.normal)
                text: modelData.label
                color: Theme.textMuted
                font.pixelSize: 7
                font.weight: Font.DemiBold
            }
        }

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 7
            visible: root.hoveredView.length > 0
            width: faceName.implicitWidth + 12
            height: 19
            radius: 9
            color: Theme.surface
            border.color: Theme.accent

            Text {
                id: faceName

                anchors.centerIn: parent
                text: root.hoveredView.toUpperCase()
                color: Theme.text
                font.pixelSize: Theme.fontSmall
                font.weight: Font.DemiBold
            }
        }
    }

    KButton {
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        semanticId: "viewport.camera.fit"
        semanticName: "Fit model in viewport"
        iconName: "fit"
        quiet: true
        onClicked: App.camera.fit()
    }
}
