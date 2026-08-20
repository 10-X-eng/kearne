from __future__ import annotations

import unittest
from math import isclose, nextafter
from sys import float_info

from hypothesis import given, settings
from hypothesis import strategies as st
from kearne.units import Angle, Length, QuantityError, deg, inch, m, mm, rad

FINITE = st.floats(
    min_value=-1_000_000.0,
    max_value=1_000_000.0,
    allow_nan=False,
    allow_infinity=False,
)


class QuantityProperties(unittest.TestCase):
    @settings(max_examples=500, deadline=None)
    @given(FINITE)
    def test_length_conversions_share_canonical_metres(self, value: float) -> None:
        self.assertTrue(
            isclose(mm(value * 25.4).metres, inch(value).metres, rel_tol=1e-12)
        )
        self.assertTrue(
            isclose(m(value).in_unit(0.001), value * 1_000.0, rel_tol=1e-12)
        )

    @settings(max_examples=500, deadline=None)
    @given(FINITE)
    def test_angle_conversions_share_canonical_radians(self, value: float) -> None:
        self.assertTrue(
            isclose(
                deg(value).radians,
                rad(value * 3.141592653589793 / 180).radians,
                rel_tol=1e-12,
            )
        )

    @settings(max_examples=500, deadline=None)
    @given(FINITE, st.floats(min_value=0.01, max_value=100.0, allow_nan=False))
    def test_quantity_arithmetic_preserves_type(
        self, value: float, scale: float
    ) -> None:
        length = mm(value)
        angle = deg(value)
        self.assertIsInstance(length * scale / scale, Length)
        self.assertIsInstance(angle * scale / scale, Angle)
        self.assertAlmostEqual((length * scale / scale).metres, length.metres)
        self.assertAlmostEqual((angle * scale / scale).radians, angle.radians)

    def test_non_finite_values_and_invalid_scales_are_rejected(self) -> None:
        for value in (float("nan"), float("inf"), -float("inf")):
            with self.subTest(value=value), self.assertRaises(QuantityError):
                Length(value)
        with self.assertRaises(QuantityError):
            mm(1).in_unit(0)
        with self.assertRaises(QuantityError):
            mm(1) / 0

    @settings(max_examples=200, deadline=None)
    @given(st.sampled_from((True, False, "1", b"1", object())))
    def test_non_numeric_runtime_values_are_rejected(self, value: object) -> None:
        with self.assertRaises(QuantityError):
            Length(value)  # type: ignore[arg-type]

    def test_zero_subnormal_and_overflow_boundaries_are_explicit(self) -> None:
        self.assertEqual(mm(-0.0).metres, 0.0)
        self.assertGreater(m(nextafter(0.0, 1.0)).metres, 0.0)
        with self.assertRaises(QuantityError):
            m(float_info.max) * 2.0
        with self.assertRaises(QuantityError):
            m(float_info.max).in_unit(0.001)

    def test_dimensions_cannot_be_mixed(self) -> None:
        with self.assertRaises(TypeError):
            _ = mm(1) + deg(1)  # type: ignore[operator]
        with self.assertRaises(TypeError):
            _ = deg(1) + mm(1)  # type: ignore[operator]


if __name__ == "__main__":
    unittest.main()
