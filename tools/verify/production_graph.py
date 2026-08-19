#!/usr/bin/env python3
"""Reject prototype inputs in Kearne production build and install graphs."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys


FORBIDDEN_TREES = ("prototype", "spikes")
GRAPH_FILES = ("build.ninja", "compile_commands.json")


def forbidden_markers(source_root: Path) -> tuple[str, ...]:
    markers: list[str] = []
    for name in FORBIDDEN_TREES:
        path = source_root / name
        markers.extend((path.as_posix() + "/", str(path).replace("/", "\\") + "\\"))
    return tuple(markers)


def generated_graph_files(build_root: Path) -> list[Path]:
    files = [build_root / name for name in GRAPH_FILES]
    files.extend(build_root.rglob("cmake_install.cmake"))
    return sorted(set(files))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-root", required=True, type=Path)
    parser.add_argument("--build-root", required=True, type=Path)
    arguments = parser.parse_args()
    source_root = arguments.source_root.resolve()
    build_root = arguments.build_root.resolve()
    markers = forbidden_markers(source_root)
    failures: list[str] = []

    compile_commands = build_root / "compile_commands.json"
    if not compile_commands.is_file():
        failures.append(f"missing generated graph: {compile_commands}")
    else:
        try:
            entries = json.loads(compile_commands.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as error:
            failures.append(f"invalid compile database: {error}")
        else:
            if not isinstance(entries, list) or not entries:
                failures.append("compile database has no production translation units")

    for path in generated_graph_files(build_root):
        if not path.is_file():
            failures.append(f"missing generated graph: {path}")
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        for marker in markers:
            if marker in text:
                failures.append(f"{path.relative_to(build_root)} references {marker}")

    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    print(f"verified {len(generated_graph_files(build_root))} generated production graphs")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
