#!/usr/bin/env bash
set -euo pipefail

script_directory="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repository_root="$(cd -- "${script_directory}/.." && pwd)"
cd "${repository_root}"

if ! command -v apt-get >/dev/null 2>&1; then
    echo "This installer supports Debian-family development hosts." >&2
    exit 2
fi

if (( EUID == 0 )); then
    privilege=()
elif command -v sudo >/dev/null 2>&1; then
    privilege=(sudo)
else
    echo "Run as root or install sudo." >&2
    exit 2
fi

"${privilege[@]}" apt-get update
"${privilege[@]}" env DEBIAN_FRONTEND=noninteractive apt-get install -y \
    --no-install-recommends \
    build-essential \
    clang-format-19 \
    clang-tidy-19 \
    cmake \
    libspnav-dev \
    libyaml-cpp-dev \
    ninja-build \
    pkg-config \
    python3 \
    python3-venv \
    qt6-base-dev \
    qt6-base-private-dev \
    qt6-declarative-dev

if ! pkg-config --atleast-version=6.8 Qt6Core; then
    echo "Kearne requires Qt 6.8 or newer; this distribution supplied an older Qt." >&2
    exit 1
fi

python3 tools/bootstrap.py "$@"
