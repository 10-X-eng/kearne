"""Public Python surface for Kearne model functions and automation."""

from __future__ import annotations

from pkgutil import extend_path
from typing import TYPE_CHECKING, Any

__path__ = extend_path(__path__, __name__)

if TYPE_CHECKING:
    from kearne.sketch import SketchDefinition

__all__ = ["SketchDefinition"]


def __getattr__(name: str) -> Any:
    if name == "SketchDefinition":
        from kearne.sketch import SketchDefinition

        return SketchDefinition
    raise AttributeError(name)
