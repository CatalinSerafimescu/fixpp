"""bindings/python/tests/test_smoke.py
Smoke test for the SWIG Python binding.
Run via: pytest bindings/python/tests/ -v
with PYTHONPATH pointing to the build's lib directory.
"""

import fixpp


def test_import():
    """The fixpp module must be importable."""
    assert fixpp is not None


def test_version_string_non_empty():
    """fixpp.version_string() must return a non-empty string."""
    v = fixpp.version_string()  # renamed from fixpp_version_string (053 prefix strip)
    assert v, "fixpp.version_string() returned None or empty string"
    assert len(v) > 0
