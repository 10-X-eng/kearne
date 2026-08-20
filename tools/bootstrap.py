#!/usr/bin/env python3
"""Create Kearne's pinned Python environment and configure a CMake preset."""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import venv
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ENVIRONMENT = ROOT / ".venv"
BOOTSTRAP_ENVIRONMENT = ROOT / ".bootstrap"
UV_VERSION = "0.11.23"


def require_program(name: str) -> str:
    path = shutil.which(name)
    if path is None:
        raise SystemExit(f"required program is missing: {name}")
    return path


def environment_python() -> Path:
    executable = "Scripts/python.exe" if sys.platform == "win32" else "bin/python"
    return ENVIRONMENT / executable


def environment_program(environment: Path, name: str) -> Path:
    suffix = ".exe" if sys.platform == "win32" else ""
    directory = "Scripts" if sys.platform == "win32" else "bin"
    return environment / directory / f"{name}{suffix}"


def run(command: list[str]) -> None:
    subprocess.run(command, cwd=ROOT, check=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--preset", default="dev")
    parser.add_argument("--configure-only", action="store_true")
    arguments = parser.parse_args()

    if sys.version_info < (3, 11):
        raise SystemExit("Kearne requires Python 3.11 or newer")
    cmake = require_program("cmake")
    require_program("ninja")

    uv = environment_program(BOOTSTRAP_ENVIRONMENT, "uv")
    if not uv.exists():
        venv.EnvBuilder(with_pip=True).create(BOOTSTRAP_ENVIRONMENT)
        bootstrap_python = environment_program(BOOTSTRAP_ENVIRONMENT, "python")
        run(
            [
                str(bootstrap_python),
                "-m",
                "pip",
                "install",
                "--disable-pip-version-check",
                f"uv=={UV_VERSION}",
            ]
        )
    run([str(uv), "sync", "--locked", "--python", sys.executable])
    python = environment_python()
    run(
        [
            cmake,
            "--preset",
            arguments.preset,
            f"-DPython3_EXECUTABLE={python}",
        ]
    )
    if not arguments.configure_only:
        run([cmake, "--build", "--preset", arguments.preset])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
