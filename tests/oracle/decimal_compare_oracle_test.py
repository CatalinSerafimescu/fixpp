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


def _py_decimal(mantissa: int, exponent: int) -> decimal.Decimal:
    """Construct a Python Decimal from (mantissa, 10^exponent) representation."""
    return decimal.Decimal(mantissa) * decimal.Decimal(10) ** exponent


@pytest.mark.parametrize("ma,ea,mb,eb", _canonical_pairs(10_000))
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
