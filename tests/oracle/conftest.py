# tests/oracle/conftest.py — Seam #8 pytest fixture for the Python Decimal oracle.
import ctypes
import os
import pathlib
import pytest


class _FixppDecimalT(ctypes.Structure):
    _fields_ = [
        ("mantissa",  ctypes.c_int64),
        ("exponent",  ctypes.c_int8),
        ("_reserved", ctypes.c_int8 * 7),
    ]


def _make_decimal(mantissa: int, exponent: int) -> _FixppDecimalT:
    d = _FixppDecimalT()
    d.mantissa = mantissa
    d.exponent = exponent
    return d


@pytest.fixture(scope="session")
def libfixpp_capi() -> ctypes.CDLL:
    """Load libfixpp_capi.so and configure argtypes/restype for compare functions.

    Resolution order (Gate B P1 #4):
      1. FIXPP_CAPI_LIB — absolute path (set by tests/oracle/CMakeLists.txt
         from $<TARGET_FILE:fixpp_capi_shared> for ctest runs).
      2. FIXPP_BUILD_DIR/lib/libfixpp_capi.so — legacy convenience for direct
         pytest invocations.
    Missing-library is a hard failure, never a silent skip — the oracle is
    the Tier 1 correctness gate for §6.3, and skipping it on a misconfigured
    build would let regressions through.
    """
    explicit = os.environ.get("FIXPP_CAPI_LIB")
    if explicit:
        lib_path = explicit
    else:
        build_dir = os.environ.get(
            "FIXPP_BUILD_DIR",
            str(pathlib.Path(__file__).parent.parent.parent / "build" / "linux-clang-release"),
        )
        lib_path = os.path.join(build_dir, "lib", "libfixpp_capi.so")

    if not os.path.exists(lib_path):
        raise RuntimeError(
            f"libfixpp_capi shared object not found at {lib_path}. "
            f"Set FIXPP_CAPI_LIB or build the fixpp_capi_shared target."
        )

    lib = ctypes.CDLL(lib_path)

    # fixpp_decimal_compare(a, b) -> int
    lib.fixpp_decimal_compare.argtypes = [_FixppDecimalT, _FixppDecimalT]
    lib.fixpp_decimal_compare.restype  = ctypes.c_int

    # fixpp_decimal_compare_checked(a, b, int*) -> fixpp_error_t (int)
    lib.fixpp_decimal_compare_checked.argtypes = [
        _FixppDecimalT, _FixppDecimalT, ctypes.POINTER(ctypes.c_int)]
    lib.fixpp_decimal_compare_checked.restype = ctypes.c_int

    lib._make_decimal = staticmethod(_make_decimal)
    lib._FixppDecimalT = _FixppDecimalT
    return lib
