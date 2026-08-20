pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kearne.UI
import "../components"

Rectangle {
    id: root

    property string semanticId: "surface.settings"
    property string semanticName: "Settings"
    property string semanticRole: "main"
    property var semanticActions: []
    property string activeCategory: App.ui.settingsCategoryId

    function activeCategoryDescriptor() {
        return App.ui.preferenceCategories.find(
                    category => category.id === root.activeCategory)
    }

    function navigationProfile() {
        const descriptor = App.ui.preferences.find(
                    preference => preference.id === "navigation-profile")
        return descriptor ? descriptor.value : "solidworks"
    }

    ThemeImportDialog {
        id: themeFileDialog
    }

    color: Theme.surfaceMuted

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.preferredWidth: root.width >= 980 ? 240 : 176
            Layout.fillHeight: true
            color: Theme.surface
            border.color: Theme.border

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.space3
                spacing: Theme.space1

                Text {
                    text: "SETTINGS"
                    color: Theme.textFaint
                    font.pixelSize: Theme.fontSmall
                    font.weight: Theme.fontWeightStrong
                    Layout.bottomMargin: Theme.space2
                }

                Repeater {
                    model: App.ui.preferenceCategories
                    KButton {
                        required property var modelData
                        semanticId: "settings.category." + modelData.id
                        semanticName: modelData.label
                        semanticRole: "tab"
                        iconName: modelData.icon
                        text: modelData.label
                        quiet: true
                        checkable: true
                        checked: root.activeCategory === modelData.id
                        Layout.fillWidth: true
                        onClicked: App.ui.selectSettingsCategory(modelData.id)
                    }
                }

                Item { Layout.fillHeight: true }

                Text {
                    Layout.fillWidth: true
                    text: "User defaults are stored locally\nProject settings stay with the project"
                    color: Theme.textFaint
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                }
            }
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            Item {
                width: Math.max(parent.width, 560)
                implicitHeight: settingsContent.implicitHeight + Theme.space6 * 2

                ColumnLayout {
                    id: settingsContent
                    width: Math.min(760, parent.width - Theme.space6 * 2)
                    anchors.top: parent.top
                    anchors.topMargin: Theme.space6
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: Theme.space6

                    PageHeading {
                        Layout.fillWidth: true
                        title: root.activeCategoryDescriptor().label
                        detail: root.activeCategory === "appearance"
                                ? "Built-in and imported YAML themes use one validated token contract."
                                : (root.activeCategory === "units"
                                ? "Defaults seed new projects; display units never alter geometry."
                                : "User-level settings; project semantics are unchanged.")
                    }

                    KPanel {
                        semanticId: "settings." + root.activeCategory
                        title: "Preferences"
                        iconName: root.activeCategoryDescriptor().icon
                        Layout.fillWidth: true

                        Repeater {
                            model: App.ui.preferences.filter(
                                       setting => setting.categoryId === root.activeCategory)
                            SettingRow {
                                required property var modelData
                                descriptor: modelData
                                onValueEdited: (settingId, value) =>
                                                   App.ui.setPreference(settingId, value)
                            }
                        }

                        RowLayout {
                            visible: root.activeCategory === "input"
                            Layout.fillWidth: true
                            Layout.topMargin: Theme.space2

                            Text {
                                Layout.fillWidth: true
                                text: "3D controller"
                                color: Theme.text
                                font.pixelSize: Theme.fontBody
                            }

                            StateBadge {
                                semanticId: "settings.input.space_mouse"
                                semanticName: "3D controller status"
                                status: App.navigationDevice.connected ? "current"
                                                                        : "unavailable"
                                label: App.navigationDevice.connected ? "Connected"
                                                                      : "Not detected"
                            }
                        }

                        Text {
                            visible: root.activeCategory === "input"
                            Layout.fillWidth: true
                            text: App.navigationDevice.status
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontSmall
                            wrapMode: Text.WordWrap
                        }

                        Text {
                            visible: root.activeCategory === "input"
                            Layout.fillWidth: true
                            text: root.navigationProfile() === "solidworks"
                                  ? "Middle drag orbit · Ctrl + middle drag pan · Wheel zoom"
                                  : (root.navigationProfile() === "onshape"
                                     ? "Right drag orbit · Middle drag pan · Wheel zoom"
                                     : "Shift + middle drag orbit · Middle drag pan · Wheel zoom")
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontSmall
                            wrapMode: Text.WordWrap
                        }


                        RowLayout {
                            visible: root.activeCategory === "appearance"
                            Layout.fillWidth: true

                            Text {
                                Layout.fillWidth: true
                                text: App.themes.activeThemeName
                                      + " · " + App.themes.appearance
                                color: Theme.textMuted
                                font.pixelSize: Theme.fontSmall
                            }

                            KButton {
                                semanticId: "settings.theme.import"
                                semanticName: "Import YAML theme"
                                iconName: "folder"
                                text: "Import YAML"
                                onClicked: themeFileDialog.begin()
                            }
                        }

                        Text {
                            visible: root.activeCategory === "appearance"
                                     && App.themes.lastError.length > 0
                            Layout.fillWidth: true
                            text: App.themes.lastError
                            color: Theme.error
                            font.pixelSize: Theme.fontSmall
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }
        }
    }
}
