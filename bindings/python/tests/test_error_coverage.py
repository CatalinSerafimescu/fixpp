"""bindings/python/tests/test_error_coverage.py — PY-003 FR-008 / SC-002 / T-5.

The exact-mapping drift guard. The FIXPP_ERR_* constants are NOT SWIG-exposed
(D-3, verified), so the expected set is derived from the INDEPENDENT source —
error.h is parsed for the {code: name} pairs — never a circular compare between
two lists both derived from the binding. Asserts, set-EQUALITY both directions:

  1. len(codes) == 47 (non-OK)            — count-pinned, cannot pass vacuously.
  2. every code maps to a non-fallback FixppError subclass (not the bare root).
  3. set(_CODE_TO_NAME) == set(header codes) AND the names match (full dict eq).

Adding OR removing a code in error.h without updating the binding fails this
test. The [1400,1499] block is the live proof: RED until AppError covers it
(AppError landed in 054, so this is GREEN; drop the AppError row from
_map_to_class and codes 1400-1405 fall to the fallback -> assertion 2 fails).
"""

import os
import pathlib
import re

import fixpp


def _error_h_path():
    here = os.path.dirname(os.path.abspath(__file__))
    repo_root = os.path.normpath(os.path.join(here, "..", "..", ".."))
    return os.path.join(repo_root, "include", "fix", "c_api", "error.h")


def _header_codes():
    text = pathlib.Path(_error_h_path()).read_text()
    return {
        int(value): name
        for name, value in re.findall(
            r"#define\s+(FIXPP_ERR_\w+)\s+\(\(fixpp_error_t\)(\d+)\)", text)
        if name != "FIXPP_ERR_OK"
    }


def test_header_parse_is_non_vacuous():
    codes = _header_codes()
    assert len(codes) == 47, f"expected 47 raisable codes, parsed {len(codes)}"


def test_every_header_code_maps_non_fallback():
    codes = _header_codes()
    fallbacks = []
    for code, name in codes.items():
        cls = fixpp.exception_for_code(code)
        assert issubclass(cls, fixpp.FixppError), f"{name} ({code}) -> non-FixppError"
        if cls is fixpp.FixppError:
            fallbacks.append((code, name))
    assert not fallbacks, f"these codes hit the bare-root fallback (unmapped block): {fallbacks}"


def test_name_dict_matches_header_set_equality():
    codes = _header_codes()
    assert set(fixpp._CODE_TO_NAME) == set(codes), (
        "code-set drift: "
        f"missing_from_dict={sorted(set(codes) - set(fixpp._CODE_TO_NAME))} "
        f"extra_in_dict={sorted(set(fixpp._CODE_TO_NAME) - set(codes))}")
    # Stronger: the symbolic names match too (full code->name equality).
    assert fixpp._CODE_TO_NAME == codes
