"""Run the pinned Kearne Python worker over inherited standard streams."""

from __future__ import annotations

import sys

from kearne._worker.protocol import ProtocolError, serve


def main() -> int:
    try:
        serve(sys.stdin.buffer, sys.stdout.buffer)
    except ProtocolError as error:
        print(f"worker.protocol: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
