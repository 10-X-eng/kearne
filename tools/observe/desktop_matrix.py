#!/usr/bin/env python3
"""Generate and verify Kearne desktop observation scenarios."""

from __future__ import annotations

import argparse
from collections import Counter
from concurrent.futures import ThreadPoolExecutor, as_completed
from fnmatch import fnmatchcase
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import stat
import struct
import subprocess
import sys
import tempfile
import zlib


SURFACES = ("editor", "projects", "settings", "recovery", "operations")
WORKSPACES = (
    "model", "sketch", "assemble", "sheet-metal", "simulate", "cam", "drawing", "bom",
)
STATES = (
    "empty", "loading", "current", "preview", "pending", "stale", "failed",
    "unavailable", "read-only", "permission-denied",
)
SETTINGS_CATEGORIES = ("appearance", "units", "input", "files", "compute", "agent")
ACTIONABLE_ROLES = {"button", "tab", "searchbox", "textbox", "combobox", "switch"}
ACCESSIBLE_ROLES = {
    "button": "Button",
    "canvas": "Canvas",
    "combobox": "ComboBox",
    "listitem": "ListItem",
    "row": "ListItem",
    "searchbox": "EditableText",
    "splitter": "Splitter",
    "status": "Button",
    "switch": "Switch",
    "tab": "PageTab",
    "textbox": "EditableText",
}
ROUTE_ACTIONS = {
    "editor": ("projects", "navigation.editor"),
    "projects": ("editor", "navigation.projects"),
    "settings": ("editor", "navigation.settings"),
    "recovery": ("projects", "projects.recovery"),
    "operations": ("editor", "navigation.operations"),
}
PROJECT_ROUTES = {
    "projects.new": "model",
    "project.motor-bracket": "model",
    "template.part.create": "model",
    "template.assembly.create": "assemble",
    "template.drawing.create": "drawing",
    "template.sheet-metal.create": "sheet-metal",
    "template.cam.create": "cam",
}


def protocol_schemas() -> tuple[str, str]:
    path = Path(__file__).with_name("observation.schema.json")
    document = json.loads(path.read_text(encoding="utf-8"))
    definitions = document.get("$defs", {})
    try:
        semantic = definitions["semanticUi"]["properties"]["schema"]["const"]
        capture = definitions["applicationSessionCapture"]["properties"]["schema"]["const"]
    except (KeyError, TypeError) as error:
        raise RuntimeError(f"invalid observation protocol schema: {path}") from error
    if not isinstance(semantic, str) or not isinstance(capture, str):
        raise RuntimeError(f"invalid observation protocol schema identifiers: {path}")
    return semantic, capture


def operations_for(scenario: dict[str, object]) -> list[dict[str, object]]:
    operations = [
        {"action": "invoke", "semantic_id": semantic_id}
        for semantic_id in scenario.get("actions", [])
    ]
    operations.extend(scenario.get("operations", []))
    return operations


def profile_for(output: Path, scenario: dict[str, object]) -> Path:
    return output.parent / "profiles" / str(scenario.get("profile", scenario["name"]))


def scenarios(mode: str) -> list[dict[str, object]]:
    if mode == "proof":
        create = [
            {"action": "invoke", "semantic_id": "command.model.sketch.create"},
            {"action": "invoke", "semantic_id": "inspector.apply"},
        ]
        return [
            {"name": "proof-sketch-empty", "surface": "editor",
             "workspace": "sketch", "initial_workspace": "model",
             "local_engineering": True,
             "operations": create,
             "required": ["sketch.solve.state"],
             "expected_values": {"sketch.solve.state": "solved:0",
                                 "viewport.state": "current"},
             "width": 1440, "height": 900},
            {"name": "proof-sketch-rectangle", "surface": "editor",
             "workspace": "sketch", "initial_workspace": "model",
             "local_engineering": True,
             "operations": [
                 *create,
                 {"action": "invoke",
                  "semantic_id": "command.sketch.rectangle"},
                 {"action": "pointerDrag", "semantic_id": "viewport.primary",
                  "value": {"button": "left", "from": [0.42, 0.43],
                            "to": [0.58, 0.57]}},
             ],
             "required": ["sketch.solve.state", "command.draft.state"],
             "expected_values": {
                 "sketch.solve.state": "underconstrained:4",
                 "command.draft.state": "editing",
                 "viewport.state": "current",
             },
             "width": 1440, "height": 900},
        ]
    result = [
        {"name": f"route-{surface}", "initial_surface": initial, "surface": surface,
         "actions": [action], "width": 1440, "height": 900}
        for surface, (initial, action) in ROUTE_ACTIONS.items()
    ]
    result.extend(
        {"name": f"workspace-{workspace}", "surface": "editor", "workspace": workspace,
         "initial_workspace": "model", "actions": [f"workspace.{workspace}"],
         "viewport_visual": workspace == "model",
         "expected_values": {
             "viewport.state": "current" if workspace in ("model", "sketch")
             else "unavailable"},
         "width": 1280, "height": 800}
        for workspace in WORKSPACES
    )
    result.extend(
        {"name": f"state-{state}", "surface": "editor", "state": state,
         "width": 1024, "height": 700}
        for state in STATES
    )
    result.extend(
        {"name": f"narrow-{surface}", "surface": surface, "width": 800, "height": 600}
        for surface in SURFACES
    )
    result.extend(
        {"name": f"settings-{category}", "surface": "settings",
         "settings_category": category, "initial_settings_category": "appearance",
         "actions": [f"settings.category.{category}"], "width": 1280, "height": 800}
        for category in SETTINGS_CATEGORIES
    )
    result.append(
        {"name": "inspector-source", "surface": "editor",
         "actions": ["inspector.tab.source"], "required": ["inspector.source.editor"],
         "width": 1440, "height": 900}
    )
    result.append(
        {"name": "inspector-function-ports", "surface": "editor",
         "actions": ["inspector.tab.source", "function.contract.toggle"],
         "required": ["function.port.input.width", "function.port.input.depth",
                      "function.port.input.thickness", "function.port.output.body"],
         "width": 1440, "height": 900}
    )
    result.append(
        {"name": "sketch-source-function", "surface": "editor",
         "initial_workspace": "model", "workspace": "sketch",
         "operations": [
             {"action": "invoke", "semantic_id": "workspace.sketch"},
             {"action": "invoke", "semantic_id": "inspector.tab.source"},
             {"action": "invoke", "semantic_id": "function.contract.toggle"},
         ],
         "required": ["function.port.input.width",
                      "function.port.input.depth",
                      "function.port.input.hole-spacing",
                      "function.port.input.hole-diameter",
                      "function.port.output.profile"],
         "expected_absent": ["function.port.input.thickness",
                             "function.port.output.body"],
         "width": 1440, "height": 900}
    )
    result.extend((
        {"name": "source-revision-review", "surface": "editor",
         "operations": [
             {"action": "invoke", "semantic_id": "inspector.tab.source"},
             {"action": "setValue", "semantic_id": "inspector.source.editor",
              "value": "from build123d import Box\n\ndef base_plate():\n    return Box(120, 60, 8)\n"},
             {"action": "invoke", "semantic_id": "source.diff"},
         ],
         "required": ["dialog.source_diff", "source_diff.current",
                      "source_diff.proposed", "source_diff.apply",
                      "source.draft.state"],
         "expected_visible": ["dialog.source_diff"],
         "expected_values": {"inspector.source.editor": "modified",
                             "source.draft.state": "modified"},
         "width": 1440, "height": 900},
        {"name": "source-revision-apply", "surface": "editor",
         "operations": [
             {"action": "invoke", "semantic_id": "inspector.tab.source"},
             {"action": "setValue", "semantic_id": "inspector.source.editor",
              "value": "from build123d import Box\n\ndef base_plate():\n    return Box(120, 60, 8)\n"},
             {"action": "invoke", "semantic_id": "source.diff"},
             {"action": "invoke", "semantic_id": "source_diff.apply"},
         ],
         "required": ["dialog.source_diff", "source.draft.state"],
         "expected_hidden": ["dialog.source_diff"],
         "expected_values": {"inspector.source.editor": "current",
                             "source.draft.state": "current"},
         "width": 1440, "height": 900},
        {"name": "transient-command-palette", "surface": "editor",
         "actions": ["command.palette.open"],
         "required": ["dialog.command_palette", "command_palette.query",
                      "palette.command.model.sketch.create"],
         "expected_visible": ["dialog.command_palette", "command_palette.query"],
         "width": 1280, "height": 800},
        {"name": "transient-command-palette-dismiss", "surface": "editor",
         "operations": [
             {"action": "invoke", "semantic_id": "command.palette.open"},
             {"action": "dismiss", "semantic_id": "dialog.command_palette"},
         ],
         "expected_hidden": ["dialog.command_palette"],
         "width": 1280, "height": 800},
        {"name": "command-palette-context-route", "surface": "editor",
         "workspace": "model", "initial_workspace": "assemble",
         "operations": [
             {"action": "invoke", "semantic_id": "command.palette.open"},
             {"action": "invoke",
              "semantic_id": "palette.command.model.extrude"},
         ],
         "required": ["field.model.extrude.profile",
                      "command.draft.state"],
         "expected_values": {"command.draft.state": "editing",
                             "viewport.state": "current"},
         "expected_hidden": ["dialog.command_palette"],
         "width": 1440, "height": 900},
        {"name": "transient-theme-import", "surface": "settings",
         "settings_category": "appearance",
         "actions": ["settings.theme.import"],
         "required": ["dialog.theme_import", "theme_import.path",
                      "theme_import.cancel", "theme_import.apply"],
         "expected_visible": ["dialog.theme_import", "theme_import.path"],
         "width": 1280, "height": 800},
        {"name": "transient-theme-import-dismiss", "surface": "settings",
         "settings_category": "appearance",
         "operations": [
             {"action": "invoke", "semantic_id": "settings.theme.import"},
             {"action": "dismiss", "semantic_id": "dialog.theme_import"},
         ],
         "expected_hidden": ["dialog.theme_import"],
         "width": 1280, "height": 800},
        {"name": "transient-structure-drawer", "surface": "editor",
         "actions": ["viewport.structure.toggle"],
         "required": ["drawer.structure", "panel.structure"],
         "expected_visible": ["drawer.structure", "panel.structure"],
         "width": 800, "height": 600},
        {"name": "transient-structure-drawer-dismiss", "surface": "editor",
         "operations": [
             {"action": "invoke", "semantic_id": "viewport.structure.toggle"},
             {"action": "dismiss", "semantic_id": "drawer.structure"},
         ],
         "expected_hidden": ["drawer.structure"],
         "expected_absent": ["panel.structure"],
         "width": 800, "height": 600},
        {"name": "transient-inspector-drawer", "surface": "editor",
         "actions": ["viewport.inspector.toggle"],
         "required": ["drawer.inspector", "panel.inspector"],
         "expected_visible": ["drawer.inspector", "panel.inspector"],
         "width": 800, "height": 600},
        {"name": "transient-inspector-drawer-dismiss", "surface": "editor",
         "operations": [
             {"action": "invoke", "semantic_id": "viewport.inspector.toggle"},
             {"action": "dismiss", "semantic_id": "drawer.inspector"},
         ],
         "expected_hidden": ["drawer.inspector"],
         "expected_absent": ["panel.inspector"],
         "width": 800, "height": 600},
    ))
    result.extend((
        {"name": "command-plane", "surface": "editor",
         "actions": ["command.model.plane.create"],
         "required": ["field.model.plane.create.method",
                      "field.model.plane.create.reference",
                      "field.model.plane.create.offset"],
         "width": 1440, "height": 900},
        {"name": "command-new-sketch", "surface": "editor",
         "actions": ["command.model.sketch.create"],
         "required": ["field.model.sketch.create.attachment",
                      "field.model.sketch.create.orientation"],
         "width": 1440, "height": 900},
        {"name": "command-extrude", "surface": "editor",
         "actions": ["command.model.extrude"],
         "required": ["field.model.extrude.profile",
                      "field.model.extrude.distance",
                      "field.model.extrude.extent",
                      "field.model.extrude.operation", "command.draft.state",
                      "inspector.cancel", "inspector.preview", "inspector.apply"],
         "expected_values": {"command.draft.state": "editing"},
         "width": 1440, "height": 900},
        {"name": "command-extrude-preview", "surface": "editor",
         "operations": [
             {"action": "invoke", "semantic_id": "command.model.extrude"},
             {"action": "setValue",
              "semantic_id": "input.model.extrude.distance", "value": "25 mm"},
             {"action": "invoke", "semantic_id": "inspector.preview"},
         ],
         "required": ["command.draft.state", "viewport.state"],
         "expected_values": {"command.draft.state": "preview",
                             "viewport.state": "preview"},
         "width": 1440, "height": 900},
        {"name": "command-extrude-apply", "surface": "editor",
         "operations": [
             {"action": "invoke", "semantic_id": "command.model.extrude"},
             {"action": "invoke", "semantic_id": "inspector.preview"},
             {"action": "invoke", "semantic_id": "inspector.apply"},
         ],
         "required": ["command.draft.state", "viewport.state"],
         "expected_values": {"command.draft.state": "unavailable",
                             "viewport.state": "current"},
         "width": 1440, "height": 900},
        {"name": "command-extrude-cancel", "surface": "editor",
         "operations": [
             {"action": "invoke", "semantic_id": "command.model.extrude"},
             {"action": "invoke", "semantic_id": "inspector.cancel"},
         ],
         "expected_absent": ["field.model.extrude.profile",
                             "field.model.extrude.distance",
                             "command.draft.state"],
         "expected_hidden": ["inspector.cancel", "inspector.preview",
                             "inspector.apply"],
         "width": 1440, "height": 900},
        {"name": "command-sketch-line", "surface": "editor",
         "workspace": "sketch", "initial_workspace": "model",
         "operations": [
             {"action": "invoke", "semantic_id": "workspace.sketch"},
             {"action": "invoke", "semantic_id": "command.sketch.line"},
             {"action": "toggle",
              "semantic_id": "input.sketch.line.construction"},
         ],
         "required": ["field.sketch.line.construction",
                      "input.sketch.line.construction", "command.draft.state",
                      "inspector.cancel"],
         "expected_values": {"command.draft.state": "editing",
                             "viewport.state": "current"},
         "expected_hidden": ["inspector.preview", "inspector.apply"],
         "width": 1440, "height": 900},
        {"name": "sketch-pointer-line-ready", "surface": "editor",
         "workspace": "sketch", "initial_workspace": "model",
         "operations": [
             {"action": "invoke", "semantic_id": "workspace.sketch"},
             {"action": "invoke", "semantic_id": "command.sketch.line"},
             {"action": "pointerClick", "semantic_id": "viewport.primary",
              "value": {"button": "left", "modifiers": [],
                        "position": [0.56, 0.34]}},
             {"action": "pointerClick", "semantic_id": "viewport.primary",
              "value": {"button": "left", "modifiers": [],
                        "position": [0.68, 0.44]}},
         ],
         "required": ["sketch.canvas", "command.draft.state",
                      "inspector.preview", "inspector.apply"],
         "expected_values": {"sketch.canvas": "sketch.line:2",
                             "command.draft.state": "editing"},
         "expected_visible": ["inspector.preview", "inspector.apply"],
         "width": 1440, "height": 900},
        {"name": "sketch-pointer-constraint-ready", "surface": "editor",
         "workspace": "sketch", "initial_workspace": "model",
         "operations": [
             {"action": "invoke", "semantic_id": "workspace.sketch"},
             {"action": "invoke", "semantic_id": "command.sketch.coincident"},
             {"action": "pointerClick", "semantic_id": "viewport.primary",
              "value": {"button": "left", "modifiers": [],
                        "position": [0.380, 0.582]}},
             {"action": "pointerClick", "semantic_id": "viewport.primary",
              "value": {"button": "left", "modifiers": [],
                        "position": [0.620, 0.582]}},
         ],
         "required": ["sketch.canvas", "command.draft.state",
                      "inspector.preview", "inspector.apply"],
         "expected_values": {"sketch.canvas": "sketch.coincident:2",
                             "command.draft.state": "editing"},
         "expected_visible": ["inspector.preview", "inspector.apply"],
         "width": 1440, "height": 900},
        {"name": "command-unavailable", "surface": "editor",
         "actions": ["command.palette.open"],
         "required": ["palette.command.model.revolve"],
         "expected_absent": ["command.model.revolve"],
         "expected_disabled": ["palette.command.model.revolve"],
         "width": 1440, "height": 900},
        {"name": "parameter-manager", "surface": "editor",
         "actions": ["parameters.manage"],
         "required": ["dialog.parameters", "parameter_manager.list",
                      "parameter_manager.editor",
                      "parameter_manager.expression",
                      "parameter_manager.apply"],
         "expected_visible": ["dialog.parameters"],
         "expected_values": {"dialog.parameters": "width"},
         "width": 1440, "height": 900},
        {"name": "parameter-edit-apply", "surface": "editor",
         "operations": [
             {"action": "invoke", "semantic_id": "parameters.manage"},
             {"action": "setValue",
              "semantic_id": "parameter_manager.expression",
              "value": "120 mm"},
             {"action": "invoke", "semantic_id": "parameter_manager.apply"},
         ],
         "required": ["dialog.parameters", "parameter.width"],
         "expected_hidden": ["dialog.parameters"],
         "expected_values": {"parameter.width": "120 mm"},
         "width": 1440, "height": 900},
        {"name": "operation-inspect", "surface": "operations",
         "operations": [
             {"action": "inspect",
              "semantic_id": "operation.operation.frontend"},
         ],
         "required": ["operation.detail"],
         "expected_values": {"operation.detail": "operation.frontend"},
         "width": 1280, "height": 800},
    ))
    result.extend(
        {"name": f"project-route-{workspace}-{index}", "initial_surface": "projects",
         "surface": "editor", "workspace": workspace, "actions": [semantic_id],
         "expected_values": {
             "viewport.state": "current" if workspace == "model" else "unavailable"},
         "width": 1440, "height": 900}
        for index, (semantic_id, workspace) in enumerate(PROJECT_ROUTES.items())
    )
    result.extend((
        {"name": "viewport-orbit", "surface": "editor",
         "operations": [
             {"action": "orbit", "semantic_id": "viewport.primary"},
         ],
         "required": ["viewport.view_cube", "viewport.camera.fit"],
         "expected_contains": {"viewport.primary": "solidworks:custom"},
         "width": 1440, "height": 900},
        {"name": "viewport-pointer-orbit", "surface": "editor",
         "operations": [
             {"action": "pointerDrag", "semantic_id": "viewport.primary",
              "value": {"button": "middle", "modifiers": [],
                        "from": [0.42, 0.44], "to": [0.58, 0.54]}},
         ],
         "required": ["viewport.view_cube", "viewport.camera.fit"],
         "expected_contains": {"viewport.primary": "solidworks:custom"},
         "width": 1440, "height": 900},
        {"name": "viewport-adaptive-grid-zoom", "surface": "editor",
         "operations": [
             {"action": "zoom", "semantic_id": "viewport.primary",
              "value": 4},
         ],
         "required": ["region.status_bar"],
         "expected_contains": {"region.status_bar": ":5 mm"},
         "width": 1440, "height": 900},
        {"name": "viewport-view-cube-front", "surface": "editor",
         "operations": [
             {"action": "choose", "semantic_id": "viewport.view_cube",
              "value": "front"},
         ],
         "required": ["viewport.view_cube", "viewport.camera.fit"],
         "expected_values": {"viewport.view_cube": "front"},
         "expected_contains": {"viewport.primary": "solidworks:front"},
         "width": 1440, "height": 900},
        {"name": "viewport-pointer-cube-left", "surface": "editor",
         "operations": [
             {"action": "pointerClick", "semantic_id": "viewport.view_cube",
              "value": {"button": "left", "modifiers": [],
                        "position": [0.33, 0.48]}},
         ],
         "required": ["viewport.view_cube", "viewport.camera.fit"],
         "expected_values": {"viewport.view_cube": "left"},
         "expected_contains": {"viewport.primary": "solidworks:left"},
         "width": 1440, "height": 900},
        {"name": "viewport-display-mode-select", "surface": "editor",
         "profile": "display-mode-persistence",
         "operations": [
             {"action": "choose", "semantic_id": "viewport.display_mode",
              "value": "wireframe"},
         ],
         "expected_values": {"viewport.display_mode": "wireframe"},
         "expected_workspace": {"display_mode": "wireframe"},
         "width": 1440, "height": 900},
        {"name": "viewport-display-mode-restart", "surface": "editor",
         "profile": "display-mode-persistence",
         "expected_values": {"viewport.display_mode": "wireframe"},
         "expected_workspace": {"display_mode": "wireframe"},
         "width": 1440, "height": 900},
        {"name": "theme-dark-select", "surface": "settings",
         "settings_category": "appearance", "theme": "light",
         "profile": "theme-persistence", "expected_theme": "dark",
         "operations": [
             {"action": "choose", "semantic_id": "setting.theme.control",
              "value": "dark"},
             {"action": "invoke", "semantic_id": "navigation.editor"},
             {"action": "invoke", "semantic_id": "navigation.settings"},
         ],
         "expected_values": {"setting.theme.control": "dark"},
         "expected_preferences": {"theme": "dark"},
         "width": 1280, "height": 800},
        {"name": "theme-dark-restart", "surface": "settings",
         "settings_category": "appearance", "theme": None,
         "profile": "theme-persistence", "expected_theme": "dark",
         "expected_values": {"setting.theme.control": "dark"},
         "expected_preferences": {"theme": "dark"},
         "width": 1280, "height": 800},
        {"name": "density-comfortable-select", "surface": "settings",
         "settings_category": "appearance",
         "profile": "density-persistence",
         "operations": [
             {"action": "choose",
              "semantic_id": "setting.interface-density.control",
              "value": "comfortable"},
         ],
         "expected_values": {
             "setting.interface-density.control": "comfortable"},
         "expected_bounds": {"region.project_bar": [None, None, None, 51]},
         "expected_preferences": {"interface-density": "comfortable"},
         "width": 1280, "height": 800},
        {"name": "density-comfortable-restart", "surface": "settings",
         "settings_category": "appearance",
         "profile": "density-persistence",
         "expected_values": {
             "setting.interface-density.control": "comfortable"},
         "expected_bounds": {"region.project_bar": [None, None, None, 51]},
         "expected_preferences": {"interface-density": "comfortable"},
         "width": 1280, "height": 800},
        {"name": "default-unit-select", "surface": "settings",
         "settings_category": "units", "profile": "unit-persistence",
         "operations": [
             {"action": "choose",
              "semantic_id": "setting.default-length-unit.control",
              "value": "in"},
             {"action": "invoke", "semantic_id": "navigation.editor"},
             {"action": "invoke", "semantic_id": "navigation.settings"},
         ],
         "expected_values": {"setting.default-length-unit.control": "in"},
         "expected_preferences": {"default-length-unit": "in"},
         "width": 1280, "height": 800},
        {"name": "default-unit-new-project", "initial_surface": "projects",
         "surface": "editor", "workspace": "model",
         "profile": "unit-persistence", "actions": ["projects.new"],
         "expected_values": {"project.length_unit": "in",
                             "viewport.grid": "XY:0.5 in"},
         "expected_preferences": {"default-length-unit": "in"},
         "width": 1440, "height": 900},
        {"name": "navigation-profile-select", "surface": "settings",
         "settings_category": "input", "profile": "navigation-persistence",
         "operations": [
             {"action": "choose",
              "semantic_id": "setting.navigation-profile.control",
              "value": "fusion"},
         ],
         "required": ["settings.input.space_mouse"],
         "expected_values": {
             "setting.navigation-profile.control": "fusion"},
         "expected_preferences": {"navigation-profile": "fusion"},
         "width": 1280, "height": 800},
        {"name": "navigation-profile-restart", "surface": "settings",
         "settings_category": "input", "profile": "navigation-persistence",
         "required": ["settings.input.space_mouse"],
         "expected_values": {
             "setting.navigation-profile.control": "fusion"},
         "expected_preferences": {"navigation-profile": "fusion"},
         "width": 1280, "height": 800},
        {"name": "workspace-layout-customize", "surface": "editor",
         "profile": "workspace-layout-persistence",
         "operations": [
             {"action": "resize", "semantic_id": "layout.structure.resize",
              "value": 360},
             {"action": "resize", "semantic_id": "layout.inspector.resize",
              "value": 420},
             {"action": "invoke", "semantic_id": "viewport.grid.toggle"},
             {"action": "invoke", "semantic_id": "viewport.grid_snap.toggle"},
             {"action": "invoke", "semantic_id": "structure.collapse"},
             {"action": "invoke", "semantic_id": "inspector.collapse"},
         ],
         "required": ["viewport.structure.toggle", "viewport.inspector.toggle"],
         "expected_absent": ["panel.structure", "panel.inspector",
                             "layout.structure.resize", "layout.inspector.resize"],
         "expected_values": {"viewport.grid.toggle": "hidden",
                             "viewport.grid_snap.toggle": "disabled"},
         "expected_workspace": {
             "structure_width": 360, "inspector_width": 420,
             "structure_visible": False, "inspector_visible": False,
             "grid_visible": False, "grid_snap_enabled": False,
         },
         "width": 1440, "height": 900},
        {"name": "workspace-layout-restart", "surface": "editor",
         "profile": "workspace-layout-persistence",
         "required": ["viewport.structure.toggle", "viewport.inspector.toggle"],
         "expected_absent": ["panel.structure", "panel.inspector",
                             "layout.structure.resize", "layout.inspector.resize"],
         "expected_values": {"viewport.grid.toggle": "hidden",
                             "viewport.grid_snap.toggle": "disabled"},
         "expected_workspace": {
             "structure_width": 360, "inspector_width": 420,
             "structure_visible": False, "inspector_visible": False,
             "grid_visible": False, "grid_snap_enabled": False,
         },
         "width": 1440, "height": 900},
        {"name": "workspace-layout-restore", "surface": "editor",
         "profile": "workspace-layout-persistence",
         "operations": [
             {"action": "invoke", "semantic_id": "viewport.structure.toggle"},
             {"action": "invoke", "semantic_id": "viewport.inspector.toggle"},
             {"action": "invoke", "semantic_id": "viewport.grid.toggle"},
             {"action": "invoke", "semantic_id": "viewport.grid_snap.toggle"},
         ],
         "required": ["panel.structure", "panel.inspector",
                      "layout.structure.resize", "layout.inspector.resize"],
         "expected_values": {"viewport.grid.toggle": "visible",
                             "viewport.grid_snap.toggle": "enabled"},
         "expected_bounds": {"panel.structure": [None, None, 360, None],
                             "panel.inspector": [None, None, 420, None]},
         "expected_workspace": {
             "structure_width": 360, "inspector_width": 420,
             "structure_visible": True, "inspector_visible": True,
             "grid_visible": True, "grid_snap_enabled": True,
         },
         "width": 1440, "height": 900},
    ))
    if mode == "full":
        result.extend(
            {"name": f"full-{surface}-{width}", "surface": surface, "width": width,
             "height": round(width * 0.625)}
            for surface in SURFACES for width in (960, 1200, 1920)
        )
        result.extend(
            {"name": f"full-{workspace}-{state}", "surface": "editor",
             "workspace": workspace, "state": state, "width": 1440, "height": 900}
            for workspace in WORKSPACES for state in STATES
        )
    names = [str(item["name"]) for item in result]
    if len(names) != len(set(names)):
        raise RuntimeError("scenario generator produced duplicate names")
    return result


class VirtualDisplay:
    def __init__(self) -> None:
        self.process: subprocess.Popen[str] | None = None
        self.display: str | None = None

    def __enter__(self) -> str | None:
        xvfb = shutil.which("Xvfb") if sys.platform.startswith("linux") else None
        if not xvfb:
            return None
        self.process = subprocess.Popen(
            [xvfb, "-displayfd", "1", "-screen", "0", "2200x1400x24", "-nolisten", "tcp"],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        )
        assert self.process.stdout is not None
        display_number = self.process.stdout.readline().strip()
        if not display_number or self.process.poll() is not None:
            assert self.process.stderr is not None
            raise RuntimeError(f"Xvfb failed to start: {self.process.stderr.read()}")
        self.display = f":{display_number}"
        return self.display

    def __exit__(self, *_: object) -> None:
        if not self.process:
            return
        self.process.terminate()
        try:
            self.process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait(timeout=5)


def launch(executable: Path, scenario: dict[str, object], output: Path,
           display: str | None) -> None:
    width = int(scenario["width"])
    height = int(scenario["height"])
    command = [
        str(executable), "--capture-dir", str(output), "--surface",
        str(scenario.get("initial_surface", scenario["surface"])), "--workspace",
        str(scenario.get("initial_workspace", scenario.get("workspace", "model"))),
        "--inspector-page",
        "properties", "--settings-category",
        str(scenario.get("initial_settings_category",
                         scenario.get("settings_category", "appearance"))), "--width", str(width),
        "--height", str(height),
    ]
    if "state" in scenario:
        command.extend(("--ui-state", str(scenario["state"])))
    if scenario.get("local_engineering"):
        command.append("--local-engineering")
    if scenario.get("theme", "light") is not None:
        command.extend(("--theme", str(scenario.get("theme", "light"))))
    for operation in operations_for(scenario):
        command.extend(("--ui-operation", json.dumps(operation, separators=(",", ":"))))
    environment = os.environ.copy()
    profile = profile_for(output, scenario)
    profile.mkdir(parents=True, exist_ok=True, mode=0o700)
    if os.name == "posix":
        profile.chmod(0o700)
    environment["XDG_CONFIG_HOME"] = str(profile / "config")
    environment["XDG_DATA_HOME"] = str(profile / "data")
    environment["XDG_CACHE_HOME"] = str(profile / "cache")
    environment.setdefault("QT_QUICK_BACKEND", "rhi")
    if sys.platform.startswith("linux"):
        environment.setdefault("QSG_RHI_BACKEND", "opengl")
        environment.setdefault("LIBGL_ALWAYS_SOFTWARE", "1")
    if display:
        environment["DISPLAY"] = display
    elif sys.platform.startswith("linux") and "DISPLAY" not in environment:
        environment["QT_QPA_PLATFORM"] = "offscreen"
    completed = subprocess.run(
        command, capture_output=True, text=True, env=environment,
        timeout=45 if scenario.get("local_engineering") else 20,
    )
    if completed.returncode:
        raise RuntimeError(
            f"application exited {completed.returncode}\nstdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
        )
    if completed.stderr.strip():
        raise RuntimeError(f"application emitted stderr:\n{completed.stderr}")


def png_pixels(path: Path) -> tuple[int, int, int, int, list[bytes]]:
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("invalid PNG signature")
    position = 8
    compressed = bytearray()
    width = height = bit_depth = color_type = 0
    while position < len(data):
        length = struct.unpack(">I", data[position:position + 4])[0]
        kind = data[position + 4:position + 8]
        payload = data[position + 8:position + 8 + length]
        if kind == b"IHDR":
            width, height, bit_depth, color_type = struct.unpack(">IIBB", payload[:10])
        elif kind == b"IDAT":
            compressed.extend(payload)
        elif kind == b"IEND":
            break
        position += length + 12
    channels = {0: 1, 2: 3, 4: 2, 6: 4}.get(color_type)
    if bit_depth != 8 or channels is None:
        raise ValueError(f"unsupported PNG format depth={bit_depth} color={color_type}")
    raw = zlib.decompress(compressed)
    stride = width * channels
    expected = (stride + 1) * height
    if len(raw) != expected:
        raise ValueError(f"PNG scanline size {len(raw)} does not match {expected}")
    rows: list[bytes] = []
    previous = bytearray(stride)
    for y in range(height):
        offset = y * (stride + 1)
        filter_kind = raw[offset]
        encoded = raw[offset + 1:offset + stride + 1]
        row = bytearray(stride)
        for index, value in enumerate(encoded):
            left = row[index - channels] if index >= channels else 0
            above = previous[index]
            upper_left = previous[index - channels] if index >= channels else 0
            if filter_kind == 0:
                predictor = 0
            elif filter_kind == 1:
                predictor = left
            elif filter_kind == 2:
                predictor = above
            elif filter_kind == 3:
                predictor = (left + above) // 2
            elif filter_kind == 4:
                estimate = left + above - upper_left
                distances = (abs(estimate - left), abs(estimate - above),
                             abs(estimate - upper_left))
                predictor = (left, above, upper_left)[distances.index(min(distances))]
            else:
                raise ValueError(f"unsupported PNG filter {filter_kind}")
            row[index] = (value + predictor) & 0xff
        rows.append(bytes(row))
        previous = row
    step = max(1, int(math.sqrt(width * height / 16_384)))
    colors = {
        rows[y][x * channels:(x + 1) * channels]
        for y in range(0, height, step)
        for x in range(0, width, step)
    }
    return width, height, len(colors), channels, rows


def region_color_count(rows: list[bytes], channels: int,
                       bounds: list[object]) -> int:
    image_height = len(rows)
    image_width = len(rows[0]) // channels
    x, y, width, height = (float(value) for value in bounds)
    x0 = max(0, int(x + width * 0.18))
    x1 = min(image_width, int(x + width * 0.82))
    y0 = max(0, int(y + height * 0.18))
    y1 = min(image_height, int(y + height * 0.74))
    area = max(1, (x1 - x0) * (y1 - y0))
    step = max(1, int(math.sqrt(area / 16_384)))
    return len({
        rows[row][column * channels:(column + 1) * channels]
        for row in range(y0, y1, step)
        for column in range(x0, x1, step)
    })


def png_difference(left: Path, right: Path) -> float:
    left_width, left_height, _, left_channels, left_rows = png_pixels(left)
    right_width, right_height, _, right_channels, right_rows = png_pixels(right)
    if ((left_width, left_height, left_channels)
            != (right_width, right_height, right_channels)):
        return 1.0
    step = max(1, int(math.sqrt(left_width * left_height / 65_536)))
    components = min(3, left_channels)
    difference = samples = 0
    for y in range(0, left_height, step):
        for x in range(0, left_width, step):
            offset = x * left_channels
            difference += sum(
                abs(left_rows[y][offset + component]
                    - right_rows[y][offset + component])
                for component in range(components)
            )
            samples += components
    return difference / (samples * 255)


def validate_bounds(node: dict[str, object], image_width: int, image_height: int) -> list[str]:
    failures: list[str] = []
    for key in ("logical_bounds", "physical_bounds", "screen_bounds"):
        bounds = node.get(key)
        if not isinstance(bounds, list) or len(bounds) != 4 or not all(
            isinstance(value, (int, float)) and math.isfinite(value) for value in bounds
        ):
            failures.append(f"{node['id']}: invalid {key}")
    if failures or not node.get("visible"):
        return failures
    x, y, width, height = node["physical_bounds"]
    if width <= 0 or height <= 0:
        failures.append(f"{node['id']}: visible node has non-positive bounds")
    tolerance = 2.0
    if x + width <= -tolerance or y + height <= -tolerance or x >= image_width + tolerance or y >= image_height + tolerance:
        failures.append(f"{node['id']}: visible bounds do not intersect captured image")
    return failures


def validate(output: Path, scenario: dict[str, object]) -> list[str]:
    failures: list[str] = []
    image_path = output / "application-session.png"
    metadata_path = output / "capture.json"
    semantic_path = output / "semantic-ui.json"
    for path in (image_path, metadata_path, semantic_path):
        if not path.is_file():
            return [f"missing {path.name}"]
    if os.name == "posix":
        if stat.S_IMODE(output.stat().st_mode) & 0o077:
            failures.append("capture directory is accessible outside its owner")
        for path in (image_path, metadata_path, semantic_path):
            if stat.S_IMODE(path.stat().st_mode) & 0o077:
                failures.append(f"{path.name} is accessible outside its owner")
    try:
        image_width, image_height, color_count, channels, rows = png_pixels(
            image_path)
    except (OSError, ValueError, zlib.error) as error:
        return [str(error)]
    if (image_width, image_height) != (int(scenario["width"]), int(scenario["height"])):
        failures.append(f"image size {(image_width, image_height)} does not match scenario")
    if color_count < 8:
        failures.append(f"capture has only {color_count} sampled colors")
    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    semantic = json.loads(semantic_path.read_text(encoding="utf-8"))
    semantic_schema, capture_schema = protocol_schemas()
    if metadata.get("schema") != capture_schema:
        failures.append("capture schema mismatch")
    if semantic.get("schema") != semantic_schema:
        failures.append("semantic schema mismatch")
    if metadata.get("target") != "ApplicationSession" or metadata.get("surface_count", 0) < 1:
        failures.append("capture target is not a non-empty application session")
    image = metadata.get("image", {})
    if (image.get("pixel_width"), image.get("pixel_height")) != (image_width, image_height):
        failures.append("capture metadata dimensions mismatch PNG")
    if image.get("sha256") != hashlib.sha256(image_path.read_bytes()).hexdigest():
        failures.append("capture digest mismatch")
    statistics = image.get("statistics", {})
    if statistics.get("sampled_unique_colors", 0) < 8:
        failures.append("capture statistics report fewer than eight colors")
    if statistics.get("sampled_alpha_max", 0) <= 0:
        failures.append("capture statistics report no visible pixels")
    if statistics.get("sampled_luminance_max", 0) - statistics.get("sampled_luminance_min", 0) < 16:
        failures.append("capture statistics report insufficient luminance range")
    if metadata.get("ui_generation") != semantic.get("ui_generation"):
        failures.append("semantic and capture generations differ")
    if (not isinstance(metadata.get("project_revision"), str)
            or not metadata["project_revision"]
            or metadata.get("project_revision") != semantic.get("project_revision")):
        failures.append("semantic and capture project revisions differ")
    expected_operations = operations_for(scenario)
    action_receipts = metadata.get("actions", [])
    semantic_actions = semantic.get("actions", [])
    if action_receipts != semantic_actions:
        failures.append("semantic and capture action receipts differ")
    if len(action_receipts) != len(expected_operations):
        failures.append("executed semantic operation count differs from scenario")
    for receipt, expected in zip(action_receipts, expected_operations, strict=False):
        for key in ("action", "semantic_id", "value"):
            if key in expected and receipt.get(key) != expected[key]:
                failures.append(f"semantic operation receipt differs for {key}")
        if receipt.get("generation_after", -1) < receipt.get("generation_before", 0):
            failures.append("semantic operation receipt moved generation backward")
    expected_theme = scenario.get("expected_theme", scenario.get("theme", "light"))
    if metadata.get("theme_id") != expected_theme or semantic.get("theme_id") != expected_theme:
        failures.append("captured theme differs from scenario")
    if semantic.get("surface_id") != scenario["surface"]:
        failures.append("captured surface differs from scenario")
    if semantic.get("workspace_id") != scenario.get("workspace", "model"):
        failures.append("captured workspace differs from scenario")
    nodes = semantic.get("nodes")
    if not isinstance(nodes, list) or not nodes:
        return [*failures, "semantic snapshot has no nodes"]
    ids = [node.get("id") for node in nodes]
    duplicates = sorted(node_id for node_id, count in Counter(ids).items() if count > 1)
    if duplicates:
        failures.append(f"duplicate semantic IDs: {duplicates}")
    id_set = set(ids)
    required = {"window.main", f"surface.{scenario['surface']}"}
    required.update(scenario.get("required", []))
    required.update(scenario.get("expected_values", {}))
    required.update(scenario.get("expected_contains", {}))
    required.update(scenario.get("expected_visible", []))
    required.update(scenario.get("expected_hidden", []))
    required.update(scenario.get("expected_disabled", []))
    if scenario.get("surface") == "editor":
        required.update({"project.length_unit", "viewport.grid",
                         "viewport.grid.toggle", "viewport.grid_snap.toggle",
                         "project.save"})
    if scenario.get("surface") == "projects":
        required.update(PROJECT_ROUTES)
        required.update({"projects.open", "projects.recovery"})
    if scenario.get("settings_category"):
        required.add(f"settings.{scenario['settings_category']}")
    missing = sorted(required - id_set)
    if missing:
        failures.append(f"missing required semantic nodes: {missing}")
    unexpected = sorted(set(scenario.get("expected_absent", [])) & id_set)
    if unexpected:
        failures.append(f"unexpected semantic nodes are present: {unexpected}")
    nodes_by_id = {node.get("id"): node for node in nodes}
    if scenario.get("viewport_visual"):
        viewport = nodes_by_id.get("viewport.primary", {})
        bounds = viewport.get("physical_bounds")
        if (not isinstance(bounds, list) or len(bounds) != 4
                or region_color_count(rows, channels, bounds) < 12):
            failures.append("viewport has no rendered visual content")
    for semantic_id in scenario.get("expected_visible", []):
        node = nodes_by_id.get(semantic_id)
        if node and not node.get("visible"):
            failures.append(f"{semantic_id}: expected visible node is hidden")
    for semantic_id in scenario.get("expected_hidden", []):
        node = nodes_by_id.get(semantic_id)
        if node and node.get("visible"):
            failures.append(f"{semantic_id}: expected hidden node is visible")
    for semantic_id in scenario.get("expected_disabled", []):
        node = nodes_by_id.get(semantic_id)
        if node and node.get("enabled"):
            failures.append(f"{semantic_id}: expected disabled node is enabled")
    if (scenario.get("surface") == "projects"
            and nodes_by_id.get("projects.open", {}).get("enabled")):
        failures.append("projects.open is enabled without a file-opening adapter")
    if (scenario.get("surface") == "editor"
            and nodes_by_id.get("project.save", {}).get("enabled")):
        failures.append("project.save is enabled without a persistence adapter")
    for semantic_id, expected in scenario.get("expected_values", {}).items():
        node = nodes_by_id.get(semantic_id)
        if node and node.get("value") != expected:
            failures.append(f"{semantic_id}: value differs from scenario")
    for semantic_id, expected in scenario.get("expected_contains", {}).items():
        node = nodes_by_id.get(semantic_id)
        if node and expected not in str(node.get("value", "")):
            failures.append(f"{semantic_id}: value does not contain {expected}")
    for semantic_id, expected in scenario.get("expected_bounds", {}).items():
        node = nodes_by_id.get(semantic_id)
        if not node:
            continue
        actual = node.get("logical_bounds")
        if not isinstance(actual, list) or len(actual) != 4:
            failures.append(f"{semantic_id}: logical bounds are missing")
            continue
        for index, expected_value in enumerate(expected):
            if expected_value is not None and not math.isclose(
                    actual[index], expected_value, abs_tol=1.0):
                failures.append(f"{semantic_id}: logical bounds differ from scenario")
                break
    if scenario.get("expected_preferences"):
        preference_files = list(profile_for(output, scenario).rglob("preferences.json"))
        if len(preference_files) != 1:
            failures.append("expected one persisted user-preference document")
        else:
            preference_file = preference_files[0]
            persisted = json.loads(preference_file.read_text(encoding="utf-8"))
            if persisted.get("schema") != "kearne.user-preferences/v1":
                failures.append("user-preference schema mismatch")
            values = persisted.get("values", {})
            for preference_id, expected in scenario["expected_preferences"].items():
                if values.get(preference_id) != expected:
                    failures.append(f"{preference_id}: persisted value differs")
            if os.name == "posix" and stat.S_IMODE(preference_file.stat().st_mode) & 0o077:
                failures.append("user preferences are accessible outside their owner")
    if scenario.get("expected_workspace"):
        workspace_files = list(profile_for(output, scenario).rglob("workspace-state.json"))
        if len(workspace_files) != 1:
            failures.append("expected one persisted workspace-state document")
        else:
            workspace_file = workspace_files[0]
            persisted = json.loads(workspace_file.read_text(encoding="utf-8"))
            if persisted.get("schema") != "kearne.workspace-state/v2":
                failures.append("workspace-state schema mismatch")
            layout = persisted.get("workspaces", {}).get("model", {})
            for field, expected in scenario["expected_workspace"].items():
                if layout.get(field) != expected:
                    failures.append(f"workspace {field}: persisted value differs")
            if os.name == "posix" and stat.S_IMODE(workspace_file.stat().st_mode) & 0o077:
                failures.append("workspace state is accessible outside its owner")
    for node in nodes:
        node_id = node.get("id", "<missing>")
        if not isinstance(node_id, str) or not node_id:
            failures.append("semantic node has no ID")
            continue
        if not isinstance(node.get("name"), str) or not node["name"].strip():
            failures.append(f"{node_id}: empty accessible name")
        if not isinstance(node.get("role"), str) or not node["role"].strip():
            failures.append(f"{node_id}: empty role")
        if not isinstance(node.get("actions"), list):
            failures.append(f"{node_id}: actions are not a list")
        elif node.get("visible") and node.get("enabled") and node.get("role") in ACTIONABLE_ROLES and not node["actions"]:
            failures.append(f"{node_id}: actionable control declares no action")
        elif node["actions"] and node.get("action_handler") is not True:
            failures.append(f"{node_id}: declared actions have no handler")
        accessibility = node.get("accessibility")
        expected_role = ACCESSIBLE_ROLES.get(node.get("role"))
        needs_accessibility = bool(
            node.get("visible")
            and expected_role
            and (node.get("role") in ACTIONABLE_ROLES
                 or node.get("role") == "canvas"
                 or node.get("actions"))
        )
        if needs_accessibility:
            if not isinstance(accessibility, dict) or not accessibility.get("present"):
                failures.append(f"{node_id}: missing native accessibility interface")
            else:
                if accessibility.get("identifier") != node_id:
                    failures.append(f"{node_id}: native accessibility ID differs")
                if accessibility.get("name") != node.get("name"):
                    failures.append(f"{node_id}: native accessible name differs")
                if accessibility.get("role") != expected_role:
                    failures.append(
                        f"{node_id}: native role {accessibility.get('role')} is not {expected_role}"
                    )
                state = accessibility.get("state")
                if not isinstance(state, dict):
                    failures.append(f"{node_id}: native accessibility state is missing")
                elif node.get("enabled") and not state.get("focusable"):
                    failures.append(f"{node_id}: enabled control is not natively focusable")
                if node.get("enabled") and state.get("disabled"):
                    failures.append(f"{node_id}: enabled control is natively disabled")
                if node.get("visible") and state.get("invisible"):
                    failures.append(f"{node_id}: visible control is natively invisible")
        parent = node.get("parent_id")
        if parent and parent not in id_set:
            failures.append(f"{node_id}: unknown semantic parent {parent}")
        failures.extend(validate_bounds(node, image_width, image_height))
    return failures


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument(
        "--mode", choices=("smoke", "full", "proof"), default="smoke")
    parser.add_argument("--output", type=Path)
    parser.add_argument(
        "--scenario", action="append", metavar="GLOB",
        help="Run only scenario names matching this repeatable glob.",
    )
    arguments = parser.parse_args()
    executable = arguments.executable.resolve()
    if not executable.is_file():
        parser.error(f"executable does not exist: {executable}")
    temporary = None
    if arguments.output:
        root = arguments.output.resolve()
        root.mkdir(parents=True, exist_ok=True)
    else:
        temporary = tempfile.TemporaryDirectory(prefix="kearne-observation-")
        root = Path(temporary.name)
    all_failures: list[str] = []
    matrix = scenarios(arguments.mode)
    if arguments.scenario:
        matrix = [
            scenario for scenario in matrix
            if any(fnmatchcase(str(scenario["name"]), pattern)
                   for pattern in arguments.scenario)
        ]
        if not matrix:
            parser.error("--scenario patterns matched no scenarios")
    groups: dict[str, list[tuple[int, dict[str, object]]]] = {}
    for index, scenario in enumerate(matrix, 1):
        profile = str(scenario.get("profile", scenario["name"]))
        groups.setdefault(profile, []).append((index, scenario))

    def run_group(entries: list[tuple[int, dict[str, object]]],
                  display: str | None) -> list[tuple[int, str, list[str]]]:
        outcomes = []
        for index, scenario in entries:
            name = str(scenario["name"])
            output = root / name
            output.mkdir()
            try:
                launch(executable, scenario, output, display)
                failures = validate(output, scenario)
            except (OSError, subprocess.SubprocessError, RuntimeError) as error:
                failures = [str(error)]
            outcomes.append((index, name, failures))
        return outcomes

    workers = max(1, int(os.environ.get("KEARNE_OBSERVE_WORKERS", "4")))
    outcomes: list[tuple[int, str, list[str]]] = []
    with VirtualDisplay() as display:
        with ThreadPoolExecutor(max_workers=min(workers, len(groups))) as executor:
            futures = [executor.submit(run_group, group, display)
                       for group in groups.values()]
            for future in as_completed(futures):
                outcomes.extend(future.result())
    for index, name, failures in sorted(outcomes):
        if failures:
            all_failures.extend(f"{name}: {failure}" for failure in failures)
            print(f"[{index:03}/{len(matrix):03}] FAIL {name}")
        else:
            print(f"[{index:03}/{len(matrix):03}] PASS {name}")
    light = root / "settings-appearance" / "application-session.png"
    selected_dark = root / "theme-dark-select" / "application-session.png"
    restarted_dark = root / "theme-dark-restart" / "application-session.png"
    if all(path.is_file() for path in (light, selected_dark, restarted_dark)):
        if png_difference(light, selected_dark) < 0.04:
            all_failures.append("theme metamorphic check: light and dark renders match")
        if png_difference(selected_dark, restarted_dark) > 0.01:
            all_failures.append("theme metamorphic check: persisted dark render changed after restart")
    empty_sketch = root / "proof-sketch-empty" / "application-session.png"
    rectangle_sketch = root / "proof-sketch-rectangle" / "application-session.png"
    if empty_sketch.is_file() and rectangle_sketch.is_file():
        if png_difference(empty_sketch, rectangle_sketch) < 0.0001:
            all_failures.append(
                "sketch proof metamorphic check: evaluated rectangle did not change the render")
    if all_failures:
        print("\n".join(all_failures), file=sys.stderr)
        if temporary:
            print(f"artifacts retained only for this process: {root}", file=sys.stderr)
        return 1
    print(f"verified {len(matrix)} generated observation scenarios")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
