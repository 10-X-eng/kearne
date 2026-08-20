pragma ComponentBehavior: Bound

import QtQuick
import QtQuick3D
import Kearne.UI

Item {
    id: root

    property bool selected: App.ui.selectionSummary
                            === "component.navigation-fixture"
    property string displayMode: "shaded-edges"
    property bool gridVisible: true
    property real gridSpacingMillimeters: 10
    readonly property real gridSpanScale: gridSpacingMillimeters * 0.6
    readonly property real gridLineScale: 0.004 * App.camera.distance / 220

    View3D {
        anchors.fill: parent
        camera: viewportCamera
        environment: SceneEnvironment {
            backgroundMode: SceneEnvironment.Transparent
            antialiasingMode: SceneEnvironment.MSAA
            antialiasingQuality: SceneEnvironment.High
            depthTestEnabled: true
            temporalAAEnabled: false
        }

        OrthographicCamera {
            id: viewportCamera

            z: 500
            clipNear: 1
            clipFar: 2000
            horizontalMagnification: 2.6 * 220 / App.camera.distance
            verticalMagnification: horizontalMagnification
        }

        DirectionalLight {
            eulerRotation: Qt.vector3d(-42, -36, 0)
            brightness: 1.15
            ambientColor: "#667085"
            castsShadow: true
            shadowFactor: 28
        }

        DirectionalLight {
            eulerRotation: Qt.vector3d(36, 145, 0)
            brightness: 0.45
        }

        Node {
            id: world

            x: App.camera.panX / viewportCamera.horizontalMagnification
            y: -App.camera.panY / viewportCamera.verticalMagnification
            eulerRotation: Qt.vector3d(-App.camera.pitch,
                                       App.camera.yaw,
                                       App.camera.roll)

            PrincipledMaterial {
                id: fixtureMaterial

                baseColor: root.selected ? Theme.accentSoft : "#a8b3c4"
                metalness: 0.08
                roughness: 0.38
            }

            PrincipledMaterial {
                id: cutMaterial

                baseColor: "#293445"
                metalness: 0.16
                roughness: 0.5
            }

            PrincipledMaterial {
                id: gridMinorMaterial

                lighting: PrincipledMaterial.NoLighting
                baseColor: Theme.canvasGridMinor
            }

            PrincipledMaterial {
                id: gridMajorMaterial

                lighting: PrincipledMaterial.NoLighting
                baseColor: Theme.canvasGridMajor
            }

            PrincipledMaterial {
                id: axisXMaterial

                lighting: PrincipledMaterial.NoLighting
                baseColor: Theme.axisX
            }

            PrincipledMaterial {
                id: axisYMaterial

                lighting: PrincipledMaterial.NoLighting
                baseColor: Theme.axisY
            }

            PrincipledMaterial {
                id: axisZMaterial

                lighting: PrincipledMaterial.NoLighting
                baseColor: Theme.axisZ
            }

            Node {
                visible: root.gridVisible

                Repeater3D {
                    model: 61

                    Model {
                        required property int index

                        visible: index !== 30
                        source: "#Cube"
                        position: Qt.vector3d(
                                      0, -0.7,
                                      (index - 30) * root.gridSpacingMillimeters)
                        scale: Qt.vector3d(root.gridSpanScale,
                                           root.gridLineScale * 0.75,
                                           root.gridLineScale)
                        materials: index % 5 === 0 ? gridMajorMaterial
                                                   : gridMinorMaterial
                    }
                }

                Repeater3D {
                    model: 61

                    Model {
                        required property int index

                        visible: index !== 30
                        source: "#Cube"
                        position: Qt.vector3d(
                                      (index - 30) * root.gridSpacingMillimeters,
                                      -0.7, 0)
                        scale: Qt.vector3d(root.gridLineScale,
                                           root.gridLineScale * 0.75,
                                           root.gridSpanScale)
                        materials: index % 5 === 0 ? gridMajorMaterial
                                                   : gridMinorMaterial
                    }
                }

                Model {
                    source: "#Cube"
                    position: Qt.vector3d(0, -0.45, 0)
                    scale: Qt.vector3d(root.gridSpanScale,
                                       root.gridLineScale * 1.5,
                                       root.gridLineScale * 2)
                    materials: axisXMaterial
                }

                Model {
                    source: "#Cube"
                    position: Qt.vector3d(0, -0.45, 0)
                    scale: Qt.vector3d(root.gridLineScale * 2,
                                       root.gridLineScale * 1.5,
                                       root.gridSpanScale)
                    materials: axisYMaterial
                }

                Model {
                    source: "#Cube"
                    position: Qt.vector3d(0,
                                          root.gridSpacingMillimeters * 6.5,
                                          0)
                    scale: Qt.vector3d(root.gridLineScale * 2,
                                       root.gridSpacingMillimeters * 0.13,
                                       root.gridLineScale * 2)
                    materials: axisZMaterial
                }
            }

            Node {
                id: fixture

                Model {
                    visible: root.displayMode !== "wireframe"
                    source: "#Cube"
                    position: Qt.vector3d(0, 4, 0)
                    scale: Qt.vector3d(1, 0.08, 0.6)
                    materials: fixtureMaterial
                }

                Model {
                    visible: root.displayMode !== "wireframe"
                    source: "#Cube"
                    position: Qt.vector3d(-40, 26, 0)
                    scale: Qt.vector3d(0.12, 0.36, 0.4)
                    materials: fixtureMaterial
                }

                Model {
                    visible: root.displayMode !== "wireframe"
                    source: "#Cube"
                    position: Qt.vector3d(40, 26, 0)
                    scale: Qt.vector3d(0.12, 0.36, 0.4)
                    materials: fixtureMaterial
                }

                Repeater3D {
                    model: [
                        { x: -23, z: -18 }, { x: 23, z: -18 },
                        { x: -23, z: 18 }, { x: 23, z: 18 }
                    ]

                    Model {
                        required property var modelData

                        visible: root.displayMode !== "wireframe"
                        source: "#Cylinder"
                        position: Qt.vector3d(modelData.x, 8.2, modelData.z)
                        scale: Qt.vector3d(0.075, 0.006, 0.075)
                        materials: cutMaterial
                    }
                }

                BoxEdges3D {
                    visible: root.displayMode !== "shaded"
                    position: Qt.vector3d(0, 4, 0)
                    dimensions: Qt.vector3d(100, 8, 60)
                    selected: root.selected
                }

                BoxEdges3D {
                    visible: root.displayMode !== "shaded"
                    position: Qt.vector3d(-40, 26, 0)
                    dimensions: Qt.vector3d(12, 36, 40)
                    selected: root.selected
                }

                BoxEdges3D {
                    visible: root.displayMode !== "shaded"
                    position: Qt.vector3d(40, 26, 0)
                    dimensions: Qt.vector3d(12, 36, 40)
                    selected: root.selected
                }
            }
        }
    }

    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 58
        width: calibrationLabel.implicitWidth + Theme.space4
        height: 24
        radius: Theme.badgeRadius
        color: Theme.surface
        border.color: Theme.border

        Text {
            id: calibrationLabel

            anchors.centerIn: parent
            text: "NAVIGATION CALIBRATION  ·  100 × 60 × 44 mm"
            color: Theme.textFaint
            font.pixelSize: Theme.fontSmall
            font.letterSpacing: Theme.letterSpacing
        }
    }
}
