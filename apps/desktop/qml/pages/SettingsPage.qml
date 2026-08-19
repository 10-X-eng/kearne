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
    property string activeCategory: uiSession.settingsCategoryId
    property var categories: [
        { id: "appearance", label: "Appearance", icon: "view" },
        { id: "units", label: "Units", icon: "measure" },
        { id: "input", label: "Input", icon: "pan" },
        { id: "files", label: "Files", icon: "folder" },
        { id: "compute", label: "Compute", icon: "operations" },
        { id: "agent", label: "AI and privacy", icon: "agent" }
    ]

    function unitLabels() {
        return uiSession.lengthUnits.map(unit => unit.label)
    }

    function unitIds() {
        return uiSession.lengthUnits.map(unit => unit.id)
    }

    function unitLabel(unitId) {
        const unit = uiSession.lengthUnits.find(candidate => candidate.id === unitId)
        return unit ? unit.label : unitId
    }

    function editSetting(settingId, value) {
        if (settingId === "default-length-unit")
            uiSession.setDefaultLengthUnit(value)
        else if (settingId === "project-length-unit")
            uiSession.setProjectLengthUnit(value)
    }

    function settingsFor(category) {
        if (category === "appearance") return [
            { id: "theme", label: "Theme", detail: "Application color scheme", kind: "choice", value: "Light", options: ["System", "Light", "Dark"] },
            { id: "density", label: "Density", detail: "Control and panel spacing", kind: "choice", value: "Compact", options: ["Compact", "Comfortable"] },
            { id: "motion", label: "Reduced motion", detail: "Disable nonessential transitions", kind: "toggle", value: false }
        ]
        if (category === "units") return [
            { id: "default-length-unit", label: "New project length unit", detail: "Seeds new projects; existing projects keep their own unit", kind: "choice", value: unitLabel(uiSession.defaultLengthUnitId), options: unitLabels(), optionIds: unitIds() },
            { id: "project-length-unit", label: "Current project length unit", detail: "Controls display, input, and grid labels without rescaling geometry", kind: "choice", value: unitLabel(uiSession.projectLengthUnitId), options: unitLabels(), optionIds: unitIds() }
        ]
        if (category === "input") return [
            { id: "orbit", label: "Orbit behavior", detail: "Pointer mapping for 3D navigation", kind: "choice", value: "CAD", options: ["CAD", "Turntable", "Trackball"] },
            { id: "zoom", label: "Wheel direction", detail: "Scroll direction for viewport zoom", kind: "choice", value: "Natural", options: ["Natural", "Reversed"] },
            { id: "selection", label: "Selection cycling", detail: "Cycle overlapping selectable entities", kind: "toggle", value: true }
        ]
        if (category === "files") return [
            { id: "autosave", label: "Recovery interval", detail: "Minutes between recoverable journal checkpoints", kind: "text", value: "5" },
            { id: "backup", label: "Backup before migration", detail: "Keep the original project before format migration", kind: "toggle", value: true },
            { id: "cache", label: "Cache limit", detail: "Disposable geometry and mesh artifacts", kind: "text", value: "20 GB" }
        ]
        if (category === "compute") return [
            { id: "workers", label: "Worker limit", detail: "Maximum concurrent local workers", kind: "text", value: "Automatic" },
            { id: "gpu", label: "Graphics backend", detail: "Restart required after a persistent change", kind: "choice", value: "Automatic", options: ["Automatic", "Vulkan", "Direct3D 11", "OpenGL"] },
            { id: "background", label: "Background evaluation", detail: "Continue noninteractive work while editing", kind: "toggle", value: true }
        ]
        return [
            { id: "codex", label: "Codex harness", detail: "Supervised app-server access; currently disconnected", kind: "toggle", value: false },
            { id: "capture", label: "Application capture", detail: "Allow lossless capture of Kearne-owned surfaces", kind: "choice", value: "Ask each session", options: ["Disabled", "Ask each session", "Developer profile"] },
            { id: "network", label: "Provider network access", detail: "Separate from local engineering commands", kind: "toggle", value: false }
        ]
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
                    font.weight: Font.DemiBold
                    Layout.bottomMargin: Theme.space2
                }

                Repeater {
                    model: root.categories
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
                        onClicked: uiSession.selectSettingsCategory(modelData.id)
                    }
                }

                Item { Layout.fillHeight: true }

                Text {
                    Layout.fillWidth: true
                    text: "Frontend contract mode\nPreferences are session-only"
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
                        title: root.categories.find(category => category.id === root.activeCategory).label
                        detail: root.activeCategory === "units"
                                ? "Defaults seed new projects; display units never alter geometry."
                                : "User-level settings; project semantics are unchanged."
                    }

                    KPanel {
                        semanticId: "settings." + root.activeCategory
                        title: "Preferences"
                        iconName: root.categories.find(category => category.id === root.activeCategory).icon
                        Layout.fillWidth: true

                        Repeater {
                            model: root.settingsFor(root.activeCategory)
                            SettingRow {
                                required property var modelData
                                descriptor: modelData
                                onValueEdited: (settingId, value) => root.editSetting(settingId, value)
                            }
                        }
                    }
                }
            }
        }
    }
}
