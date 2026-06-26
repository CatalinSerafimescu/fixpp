"""bindings/python/tests/test_exceptions.py — PY-003 typed exception surface.

Covers FR-006 (hierarchy + alias), FR-007 (attrs), FR-009 (block/fallback
mapping), FR-010 (in-typemap carve-out), SC-001 (catch by category + recover
code), SC-006 (synthetic unmapped fallback). The set-equality coverage invariant
(SC-002) lives in test_error_coverage.py.
"""

import pytest

import fixpp

# Every block subclass that must descend directly from FixppError.
_BLOCK_CLASSES = [
    "CapiError", "ParseError", "ValidatorError", "SessionError", "StoreError",
    "SyncError", "TlsError", "TransportError", "DecimalError",
    "ControlPlaneError", "LogError", "TapError", "BindingError", "AppError",
]
_BINDING_CHILDREN = [
    "PythonCallbackRaised", "SubInterpreterRejected", "ObjectLifetime",
    "WheelAbiMismatch", "CallbackReentrantClose",
]


def test_root_alias_survives_053_surface():
    # FR-006: Error is an alias of FixppError (pytest.raises(fixpp.Error) survives).
    assert fixpp.Error is fixpp.FixppError
    assert issubclass(fixpp.FixppError, Exception)


def test_hierarchy_structure():
    F = fixpp.FixppError
    for name in _BLOCK_CLASSES:
        assert issubclass(getattr(fixpp, name), F), name
    # CapiError children.
    assert issubclass(fixpp.Cancelled, fixpp.CapiError)
    assert issubclass(fixpp.Unknown, fixpp.CapiError)
    # BindingError children.
    for name in _BINDING_CHILDREN:
        assert issubclass(getattr(fixpp, name), fixpp.BindingError), name


def test_no_unknownerror_class():
    # FR-009: no fixpp.UnknownError (would collide with Unknown/2).
    assert not hasattr(fixpp, "UnknownError")


def test_block_mapping_introspection():
    # FR-008/FR-009: the exposed translator maps codes to the block-matching class.
    cases = {
        1: fixpp.Cancelled,
        2: fixpp.Unknown,
        50: fixpp.CapiError,
        100: fixpp.ParseError,
        200: fixpp.ValidatorError,
        300: fixpp.SessionError,
        405: fixpp.StoreError,          # future-in-known-block -> block class
        701: fixpp.TransportError,
        1200: fixpp.PythonCallbackRaised,
        1204: fixpp.CallbackReentrantClose,
        1250: fixpp.BindingError,       # unrecognised binding growth -> parent
        1402: fixpp.AppError,
    }
    for code, cls in cases.items():
        assert fixpp.exception_for_code(code) is cls, (code, cls)


def test_unmapped_fallback_is_root():
    # SC-006: a wholly unmapped/future block -> root FixppError (forward-compat).
    assert fixpp.exception_for_code(99999) is fixpp.FixppError
    assert fixpp.exception_for_code(50000) is fixpp.FixppError


def test_attributes_on_typed_instances():
    # FR-007: .code (int) + .name (symbolic) + .message (strerror == str(exc)).
    for code in (1, 2, 200, 701, 1204, 1402):
        e = fixpp._make_error(code)
        assert isinstance(e, fixpp.FixppError)
        assert e.code == code
        assert e.name == fixpp._CODE_TO_NAME[code]
        assert e.message == fixpp.strerror(code)
        assert str(e) == fixpp.strerror(code)
        # block-matching subclass
        assert type(e) is fixpp.exception_for_code(code)


def test_name_totality_for_synthetic_code():
    # T-2 / SC-006: a code absent from _CODE_TO_NAME still gets a .name (no KeyError).
    e = fixpp._make_error(99999)
    assert e.name == "FIXPP_ERR_99999"
    assert isinstance(e, fixpp.FixppError) and type(e) is fixpp.FixppError


def test_live_typed_raise_carries_code_and_message():
    # SC-001: a real non-OK C-ABI path raises the block-matching subclass carrying
    # the recoverable numeric code + message, catchable as the root.
    with pytest.raises(fixpp.FixppError) as ei:
        fixpp.dict_load_from_xml("/nonexistent/path/NOPE.xml")
    e = ei.value
    assert e.code in fixpp._CODE_TO_NAME
    assert e.name == fixpp._CODE_TO_NAME[e.code]
    assert e.message == str(e) == fixpp.strerror(e.code)
    assert type(e) is fixpp.exception_for_code(e.code)
    # Also catchable via the 053 alias.
    with pytest.raises(fixpp.Error):
        fixpp.dict_load_from_xml("/nonexistent/path/NOPE.xml")


def test_in_typemap_conversion_failures_are_root_message_only():
    # FR-010 / T-2 tier b: non-str / embedded-NUL / non-bytes in-typemap failures
    # -> root FixppError, .message ONLY (no .code/.name, no fabricated code).
    sc = fixpp.session_config_create()
    try:
        for bad in ("has\x00nul", 1234, b"bytes"):
            with pytest.raises(fixpp.FixppError) as ei:
                fixpp.session_config_set_begin_string(sc, bad)
            e = ei.value
            assert type(e) is fixpp.FixppError, f"expected root, got {type(e).__name__}"
            assert not hasattr(e, "code")
            assert not hasattr(e, "name")
            assert str(e), "root conversion-failure carried no message"
    finally:
        fixpp.session_config_destroy(sc)
