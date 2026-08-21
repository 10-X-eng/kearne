#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

python3 tools/bootstrap.py --preset release
ctest --preset release

printf '\nKearne: %s\n' "$repo_root/build/release/apps/desktop/kearne"
