#!/usr/bin/env python3
"""Verify repository-local Markdown links without network access."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys
from urllib.parse import unquote, urlparse


LINK = re.compile(r"!?\[[^\]]*\]\(([^)]+)\)")


def markdown_links(path: Path) -> list[tuple[int, str]]:
    links: list[tuple[int, str]] = []
    fenced = False
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if line.lstrip().startswith("```"):
            fenced = not fenced
            continue
        if fenced:
            continue
        links.extend((line_number, match.group(1).strip("<>")) for match in LINK.finditer(line))
    return links


def local_target(document: Path, target: str) -> Path | None:
    target = target.split("#", 1)[0]
    if not target:
        return None
    parsed = urlparse(target)
    if parsed.scheme or parsed.netloc:
        return None
    return (document.parent / unquote(target)).resolve()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-root", required=True, type=Path)
    arguments = parser.parse_args()
    source_root = arguments.source_root.resolve()
    failures: list[str] = []
    def repository_document(path: Path) -> bool:
        parts = path.relative_to(source_root).parts
        return not any(
            part in {".git", ".cache", ".venv", "__pycache__"}
            or part == "build" or part.startswith("build-")
            for part in parts
        )

    documents = sorted(path for path in source_root.rglob("*.md")
                       if repository_document(path))
    checked = 0
    for document in documents:
        for line, link in markdown_links(document):
            target = local_target(document, link)
            if target is None:
                continue
            checked += 1
            if not target.exists():
                failures.append(
                    f"{document.relative_to(source_root)}:{line}: missing {link}")
    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    print(f"verified {checked} local links across {len(documents)} documents")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
