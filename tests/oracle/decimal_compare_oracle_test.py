# tests/oracle/decimal_compare_oracle_test.py
# Seam #8 — Python Decimal property oracle for fixpp_decimal_compare.
# Generates 10^4 canonical-domain (a,b) pairs and verifies that
# fixpp_decimal_compare(a,b) matches Python's decimal.Decimal ordering.

import ctypes
import decimal
import random

import pytest


def _py_sign(cmp: int) -> int:
    """Normalize comparison result to -1/0/+1."""
    if cmp < 0:  return -1
    if cmp > 0:  return  1
    return 0


def _canonical_pairs(n: int, seed: int = 42):
    """Generate n (mantissa_a, exp_a, mantissa_b, exp_b) pairs in canonical domain."""
    rng = random.Random(seed)
    MAX_M = (1 << 63) - 2  # INT64_MAX - 1
    pairs = []
    for _ in range(n):
        ma = rng.randint(-MAX_M, MAX_M)
        ea = rng.randint(-38, 0)
        mb = rng.randint(-MAX_M, MAX_M)
        eb = rng.randint(-38, 0)
        pairs.append((ma, ea, mb, eb))
    return pairs


def _cross_exponent_pairs():
    """Curated cross-exponent (mantissa_a, exp_a, mantissa_b, exp_b) pairs.

    `_canonical_pairs` draws both exponents uniformly at random, so
    value-equal-across-scales pairs (and the wide-multiply k-boundary /
    hi-limb-crossing-2^64 arms it triggers) are vanishingly unlikely to
    appear by chance over 10,000 draws. These pairs are hand-picked to
    force the different-exponent wide-multiply compare path with
    differing exponents on every row (research.md R4/R5) while staying
    strictly in-domain (mantissa in [INT64_MIN+1, INT64_MAX], exponent in
    [-38, 0]) so the bare fixpp_decimal_compare (which returns 0 for any
    out-of-domain operand, see DecimalCAPIErrorPaths.BareCompare*) never
    silently disagrees with the Python oracle for a reason unrelated to
    the compare algorithm itself. Deterministic by construction (literal
    list) — seed=42 above is untouched.
    """
    INT64_MAX = (1 << 63) - 1
    INT64_MIN_PLUS_1 = -INT64_MAX
    return [
        # -- canonicalization-equality across differing exponents (R5 row 1) --
        (100, -2, 1, 0),
        (-100, -2, -1, 0),
        (1000, -3, 10, -1),
        (-1000, -3, -10, -1),
        (9000, -3, 9, 0),
        (0, -38, 0, 0),
        (0, -20, 0, -5),
        (1, -1, 10, -2),
        (5, -2, 500, -4),
        # -- hi-limb crosses 2^64 in the wide-multiply scale-up (R5 row 2) --
        (99, 0, INT64_MAX, -18),
        (INT64_MAX, -18, 99, 0),
        (-99, 0, INT64_MIN_PLUS_1, -18),
        (INT64_MIN_PLUS_1, -18, -99, 0),
        # -- k-boundary: k=18 multiply arm / k=20 near-INT64_MAX guard arm /
        # k=38 full domain, both directions and both signs (R5 row 3) --
        (INT64_MAX, 0, 1, -18),
        (1, -18, INT64_MAX, 0),
        (INT64_MAX, 0, 1, -20),
        (1, -20, INT64_MAX, 0),
        (INT64_MAX, 0, 1, -38),
        (1, -38, INT64_MAX, 0),
        (INT64_MIN_PLUS_1, 0, -1, -38),
        (-1, -38, INT64_MIN_PLUS_1, 0),
        # -- general boundary-magnitude values at differing exponents --
        (3, -5, 3, -1),
        (123456789, -9, 1, 0),
        (-123456789, -9, -1, 0),
    ]


def _py_decimal(mantissa: int, exponent: int) -> decimal.Decimal:
    """Construct a Python Decimal from (mantissa, 10^exponent) representation."""
    return decimal.Decimal(mantissa) * decimal.Decimal(10) ** exponent


@pytest.mark.parametrize("ma,ea,mb,eb", _canonical_pairs(10_000) + _cross_exponent_pairs())
def test_compare_matches_python_oracle(libfixpp_capi, ma, ea, mb, eb):
    a = libfixpp_capi._make_decimal(ma, ea)
    b = libfixpp_capi._make_decimal(mb, eb)

    got = _py_sign(libfixpp_capi.fixpp_decimal_compare(a, b))

    py_a = _py_decimal(ma, ea)
    py_b = _py_decimal(mb, eb)
    expected = _py_sign((py_a > py_b) - (py_a < py_b))

    assert got == expected, (
        f"compare({ma}e{ea}, {mb}e{eb}): got {got}, expected {expected}"
    )
