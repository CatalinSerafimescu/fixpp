"""NBC-3 (T018) — import-surface snapshot of the installed wheel.

Guards the additive `%pythoncode` re-export (T009 locator + the 055 OO block)
from silently DROPPING or ADDING a public name. The frozen set below is the
exact public surface of `import fixpp` as shipped by the cp310-abi3 wheel;
`==` (not subset) is deliberate — a subset check passes when a symbol is
deleted (it only catches additions) and a superset check passes when one is
added. Exact-set equality forces *any* surface change to be a reviewed edit to
this snapshot (cf. feedback_completeness_gate_exact_set_not_subset).

Runs in the dedicated wheel suite, so it asserts the surface of the *installed*
package (the harness scrubs PYTHONPATH and pins fixpp.__file__ under sys.prefix
— see test_installed_only.py). Imports only `fixpp`: NBC-3 is about the single
public proxy module re-exporting everything (quickstart §3).
"""

import fixpp

# The complete public surface (names not starting with "_") of `import fixpp`.
# Categories per NBC-3: flat C-ABI functions, the typed exception hierarchy
# (FixppError + Error alias + subclasses), the OO classes (Engine/Session/
# Message/Application/Dictionary), the enum-mirror int constants, and the
# new dictionary locator (dictionary_path/dictionary_bytes/BUNDLED_DICTIONARIES).
EXPECTED_PUBLIC_SURFACE = frozenset({
    # ── typed exception hierarchy (PY-003 / §4.6) ──────────────────────────
    "FixppError", "Error",
    "AppError", "BindingError", "CapiError", "Cancelled",
    "CallbackReentrantClose", "ControlPlaneError", "DecimalError",
    "LogError", "ObjectLifetime", "ParseError", "PythonCallbackRaised",
    "SessionError", "StoreError", "SubInterpreterRejected", "SyncError",
    "TapError", "TlsError", "TransportError", "Unknown", "ValidatorError",
    "WheelAbiMismatch", "exception_for_code",
    # ── OO layer (PY-004 / §6.2) ───────────────────────────────────────────
    "Engine", "Session", "Message", "Application", "Dictionary",
    # ── dictionary locator (PY-005 / LOC-*) ────────────────────────────────
    "BUNDLED_DICTIONARIES", "dictionary_path", "dictionary_bytes",
    # ── C-ABI version constants ────────────────────────────────────────────
    "C_ABI_VERSION", "C_ABI_VERSION_MAJOR", "C_ABI_VERSION_MINOR",
    "C_ABI_VERSION_PATCH",
    # ── enum-mirror int constants ──────────────────────────────────────────
    "RESET_SEQNUM_BILATERAL_LENIENT", "RESET_SEQNUM_BILATERAL_STRICT",
    "RESET_SEQNUM_UNILATERAL", "ROLE_ACCEPTOR", "ROLE_INITIATOR",
    "SECURITY_INSECURE_PLAIN_TCP", "SECURITY_TLS",
    "TOAPP_ERROR", "TOAPP_SEND", "TOAPP_VETO",
    # ── flat C-ABI functions ───────────────────────────────────────────────
    "dict_destroy", "dict_load_from_xml",
    "engine_config_create", "engine_config_destroy",
    "engine_config_set_realtime_clock", "engine_config_set_worker_threads",
    "engine_create", "engine_destroy", "engine_start",
    "msg_commit", "msg_create_outbound", "msg_destroy",
    "msg_get_string", "msg_set_string",
    "session_acceptor_bound_endpoint", "session_close",
    "session_config_create", "session_config_destroy",
    "session_config_set_begin_string", "session_config_set_comp_ids",
    "session_config_set_dictionary", "session_config_set_heartbeat_seconds",
    "session_config_set_reset_on_logon",
    "session_config_set_reset_seqnum_policy", "session_config_set_role",
    "session_config_set_security", "session_config_set_tcp_endpoint",
    "session_is_established", "session_open", "session_register_callback",
    "session_send",
    "strerror", "version_string",
})


def test_import_surface_exact_set():
    actual = frozenset(n for n in dir(fixpp) if not n.startswith("_"))
    missing = EXPECTED_PUBLIC_SURFACE - actual
    added = actual - EXPECTED_PUBLIC_SURFACE
    assert actual == EXPECTED_PUBLIC_SURFACE, (
        f"public surface drift — dropped: {sorted(missing)}; "
        f"unexpectedly added: {sorted(added)}. "
        "Update EXPECTED_PUBLIC_SURFACE only as a deliberate, reviewed change."
    )


def test_import_surface_named_categories_present():
    # Belt-and-suspenders for the NBC-3 named categories, so the intent of the
    # guard survives even if EXPECTED_PUBLIC_SURFACE is ever edited en masse.
    assert fixpp.Error is fixpp.FixppError
    for name in ("FixppError", "Engine", "Session", "Message", "Application",
                 "Dictionary", "dictionary_path", "dictionary_bytes",
                 "BUNDLED_DICTIONARIES", "strerror", "version_string"):
        assert hasattr(fixpp, name), name
