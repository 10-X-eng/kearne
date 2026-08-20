"""Finite SI quantities used at Kearne's Python boundary."""

from __future__ import annotations

from dataclasses import dataclass
from math import isfinite, pi
from typing import overload


class QuantityError(ValueError):
    """A quantity value or unit conversion is invalid."""


def _finite(value: float) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise QuantityError("quantity is not numeric")
    result = float(value)
    if not isfinite(result):
        raise QuantityError("quantity is not finite")
    return 0.0 if result == 0.0 else result


def _scaled(value: float, scale: float) -> float:
    finite_scale = _finite(scale)
    result = _finite(value) * finite_scale
    if finite_scale <= 0.0 or not isfinite(result):
        raise QuantityError("unit conversion is invalid")
    return 0.0 if result == 0.0 else result


@dataclass(frozen=True, slots=True)
class Length:
    """Length stored in canonical metres."""

    metres: float

    def __post_init__(self) -> None:
        object.__setattr__(self, "metres", _finite(self.metres))

    def __add__(self, other: Length) -> Length:
        if not isinstance(other, Length):
            return NotImplemented
        return Length(self.metres + other.metres)

    def __sub__(self, other: Length) -> Length:
        if not isinstance(other, Length):
            return NotImplemented
        return Length(self.metres - other.metres)

    def __neg__(self) -> Length:
        return Length(-self.metres)

    def __mul__(self, scalar: float) -> Length:
        return Length(self.metres * _finite(scalar))

    def __rmul__(self, scalar: float) -> Length:
        return self * scalar

    @overload
    def __truediv__(self, divisor: float) -> Length: ...

    @overload
    def __truediv__(self, divisor: Length) -> float: ...

    def __truediv__(self, divisor: float | Length) -> Length | float:
        value = divisor.metres if isinstance(divisor, Length) else _finite(divisor)
        if value == 0.0:
            raise QuantityError("division by zero")
        result = self.metres / value
        return _finite(result) if isinstance(divisor, Length) else Length(result)

    def in_unit(self, metres_per_unit: float) -> float:
        return _finite(self.metres / _scaled(1.0, metres_per_unit))


@dataclass(frozen=True, slots=True)
class Angle:
    """Angle stored in canonical radians."""

    radians: float

    def __post_init__(self) -> None:
        object.__setattr__(self, "radians", _finite(self.radians))

    def __add__(self, other: Angle) -> Angle:
        if not isinstance(other, Angle):
            return NotImplemented
        return Angle(self.radians + other.radians)

    def __sub__(self, other: Angle) -> Angle:
        if not isinstance(other, Angle):
            return NotImplemented
        return Angle(self.radians - other.radians)

    def __neg__(self) -> Angle:
        return Angle(-self.radians)

    def __mul__(self, scalar: float) -> Angle:
        return Angle(self.radians * _finite(scalar))

    def __rmul__(self, scalar: float) -> Angle:
        return self * scalar

    @overload
    def __truediv__(self, divisor: float) -> Angle: ...

    @overload
    def __truediv__(self, divisor: Angle) -> float: ...

    def __truediv__(self, divisor: float | Angle) -> Angle | float:
        value = divisor.radians if isinstance(divisor, Angle) else _finite(divisor)
        if value == 0.0:
            raise QuantityError("division by zero")
        result = self.radians / value
        return _finite(result) if isinstance(divisor, Angle) else Angle(result)

    def in_unit(self, radians_per_unit: float) -> float:
        return _finite(self.radians / _scaled(1.0, radians_per_unit))


def m(value: float) -> Length:
    return Length(_finite(value))


def mm(value: float) -> Length:
    return Length(_scaled(value, 0.001))


def inch(value: float) -> Length:
    return Length(_scaled(value, 0.0254))


def rad(value: float) -> Angle:
    return Angle(_finite(value))


def deg(value: float) -> Angle:
    return Angle(_scaled(value, pi / 180.0))


__all__ = ["Angle", "Length", "QuantityError", "deg", "inch", "m", "mm", "rad"]
