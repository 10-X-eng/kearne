#!/usr/bin/env bash
set -euo pipefail

readonly formatter="clang-format-19"
readonly script_directory="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
readonly repository_root="$(cd -- "${script_directory}/.." && pwd -P)"

usage() {
    cat <<'EOF'
Usage: tools/format-cpp.sh [--apply] [FILE ...]

Check repository C/C++ formatting, or update it with --apply. With no FILE
arguments, tracked and unignored files are discovered from Git.
EOF
}

fail() {
    printf 'format-cpp: %s\n' "$1" >&2
    exit 2
}

is_cpp_path() {
    case "$1" in
        *.c | *.cc | *.cpp | *.cxx | *.h | *.hh | *.hpp | *.hxx) return 0 ;;
        *) return 1 ;;
    esac
}

is_excluded_path() {
    case "/$1/" in
        */.git/* | */build/* | */build-*/* | */_deps/* | */generated/* | */prototype/*) return 0 ;;
        *) return 1 ;;
    esac
}

declare -a files=()

append_file() {
    local candidate="$1"
    local canonical
    local relative

    canonical="$(realpath -e -- "${candidate}")" || fail "file does not exist: $(printf '%q' "${candidate}")"
    [[ -f "${canonical}" ]] || fail "not a regular file: $(printf '%q' "${candidate}")"
    [[ "${canonical}" == "${repository_root}/"* ]] ||
        fail "file is outside the repository: $(printf '%q' "${candidate}")"

    relative="${canonical#"${repository_root}/"}"
    is_cpp_path "${relative}" || fail "unsupported file extension: $(printf '%q' "${candidate}")"
    is_excluded_path "${relative}" && fail "excluded path: $(printf '%q' "${candidate}")"
    files+=("${canonical}")
}

apply=false
if [[ "${1-}" == "--apply" ]]; then
    apply=true
    shift
elif [[ "${1-}" == "--help" || "${1-}" == "-h" ]]; then
    usage
    exit 0
elif [[ "${1-}" == --* ]]; then
    fail "unknown option: $(printf '%q' "$1")"
fi

cd -- "${repository_root}"

if (( $# > 0 )); then
    for candidate in "$@"; do
        append_file "${candidate}"
    done
else
    command -v git >/dev/null 2>&1 || fail "git is required for repository discovery"
    git rev-parse --is-inside-work-tree >/dev/null 2>&1 || fail "not inside a Git work tree"

    while IFS= read -r -d '' candidate; do
        is_cpp_path "${candidate}" || continue
        is_excluded_path "${candidate}" && continue
        [[ -f "${candidate}" ]] || continue
        append_file "${candidate}"
    done < <(git ls-files --cached --others --exclude-standard -z | LC_ALL=C sort -z)
fi

(( ${#files[@]} > 0 )) || fail "no eligible C/C++ files"

declare -a sorted_files=()
while IFS= read -r -d '' candidate; do
    sorted_files+=("${candidate}")
done < <(printf '%s\0' "${files[@]}" | LC_ALL=C sort -zu)
files=("${sorted_files[@]}")

command -v "${formatter}" >/dev/null 2>&1 ||
    fail "${formatter} is required; run tools/install-dev-deps-debian.sh"

readonly batch_size=128
for ((offset = 0; offset < ${#files[@]}; offset += batch_size)); do
    if [[ "${apply}" == true ]]; then
        "${formatter}" -i -- "${files[@]:offset:batch_size}"
    else
        "${formatter}" --dry-run --Werror -- "${files[@]:offset:batch_size}"
    fi
done

if [[ "${apply}" == true ]]; then
    printf 'Formatted %d C/C++ file(s).\n' "${#files[@]}"
else
    printf 'Formatting verified for %d C/C++ file(s).\n' "${#files[@]}"
fi
