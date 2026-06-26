"""bindings/python/tests/test_roundtrip.py — PY-001 e2e loopback round-trip.

053-python-thin-binding, User Story 1 (the feature's definition of done). Drives
a full FIX 4.4 loopback round-trip through the PUBLIC C-ABI surface only:

    dict_load_from_xml -> two engines (acceptor port-0 + initiator) -> establish
    -> outbound builder (create_outbound/set_string/commit) -> session_send
    -> inbound Python callback -> msg_get_string -> assert field == sent.

This is the D-4 false-green guard: written FIRST / RED against the T005 skeleton
(selective wrap, no typemaps), it forces every typemap (T007-T012) to actually
work end-to-end. The Python translation of tests/capi/public_roundtrip_test.cpp,
with the outbound builder substituted for a hand-built payload and FIX44.xml.

Every wait uses a bounded, test-FAILING deadline (research D-2): a stuck
establishment / lost message FAILS the test, never hangs CI.

Run: PYTHONPATH=<build>/lib pytest bindings/python/tests/test_roundtrip.py -v
"""

import os
import threading
import time

import pytest

import fixpp

# ── Test parameters (research D-7 — verified against dictionaries/FIX44.xml) ──
HOST = "127.0.0.1"
MSG_TYPE = "D"          # NewOrderSingle (msgcat='app' -> routes to the callback)
TAG_CLORDID = 11        # ClOrdID, type STRING, required scalar field of "D"
SENT = "ORDER-PY-001"

# Bounded deadlines (seconds). Generous enough for a cold loopback under CI load,
# short enough that a real failure fails fast instead of hanging.
BIND_TIMEOUT = 5.0
ESTABLISH_TIMEOUT = 5.0
RECV_TIMEOUT = 5.0
POLL_INTERVAL = 0.02


def _dict_path():
    # The bundled dictionary lives at <repo>/dictionaries/FIX44.xml. Resolve it
    # relative to this file so the test is CWD-independent (CI runs the whole dir).
    here = os.path.dirname(os.path.abspath(__file__))
    repo_root = os.path.normpath(os.path.join(here, "..", "..", ".."))
    return os.path.join(repo_root, "dictionaries", "FIX44.xml")


def _wait_until(predicate, timeout, what):
    """Poll predicate() until truthy or the deadline elapses. Returns the truthy
    value; raises AssertionError (FAILS the test) on timeout — never hangs."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        v = predicate()
        if v:
            return v
        time.sleep(POLL_INTERVAL)
    raise AssertionError(f"timed out after {timeout}s waiting for: {what}")


def _make_session_config(dict_h, role, sender, target, port):
    """Build a plaintext session config mirroring the gold-reference recipe
    (research D-3 / quickstart). Per-role reset_on_logon asymmetry + the
    bilateral_lenient acceptance policy is the spec'd establishment recipe.

    (Establishment fallback, D-3: if a fresh pair does not log on under this
    recipe, public_roundtrip_test.cpp proves both-sides reset_on_logon=True +
    RESET_SEQNUM_BILATERAL_STRICT establishes 20/20 — swap the two lines below.)
    """
    sc = fixpp.session_config_create()
    fixpp.session_config_set_role(sc, role)
    fixpp.session_config_set_comp_ids(sc, sender, target)
    fixpp.session_config_set_begin_string(sc, "FIX.4.4")
    fixpp.session_config_set_dictionary(sc, dict_h)
    fixpp.session_config_set_security(
        sc, fixpp.SECURITY_INSECURE_PLAIN_TCP, None, None)  # explicit plaintext (XII §5)
    fixpp.session_config_set_heartbeat_seconds(sc, 30)
    fixpp.session_config_set_reset_on_logon(sc, role == fixpp.ROLE_INITIATOR)
    fixpp.session_config_set_reset_seqnum_policy(
        sc, fixpp.RESET_SEQNUM_BILATERAL_LENIENT)
    fixpp.session_config_set_tcp_endpoint(sc, HOST, port)
    return sc


def test_loopback_roundtrip():
    """Two-engine loopback round-trip: the field received equals the field sent
    (SC-001 / SC-003). RED against the T005 skeleton (no typemaps)."""
    dict_path = _dict_path()
    assert os.path.isfile(dict_path), f"missing bundled dictionary: {dict_path}"

    dict_h = fixpp.dict_load_from_xml(dict_path)

    received = {}
    got = threading.Event()

    def on_message(inbound):
        # Runs on an engine worker thread; the binding has reacquired the GIL.
        # Read INSIDE the callback — inbound is a borrowed, dispatch-window view.
        received["value"] = fixpp.msg_get_string(inbound, TAG_CLORDID)
        got.set()

    eng_a = eng_b = acc = ini = None
    try:
        # ── Acceptor engine (A): open before start, ephemeral port read after ──
        eca = fixpp.engine_config_create()
        fixpp.engine_config_set_realtime_clock(eca)
        eng_a = fixpp.engine_create(eca)  # 1-arg wrapper injects the version macros
        acc_sc = _make_session_config(
            dict_h, fixpp.ROLE_ACCEPTOR, "ACCEPTOR", "INITIATOR", 0)
        acc = fixpp.session_open(eng_a, acc_sc)
        fixpp.session_register_callback(acc, on_message)   # MUST be before start
        fixpp.engine_start(eng_a)

        port = _wait_until(
            lambda: fixpp.session_acceptor_bound_endpoint(acc) or None,
            BIND_TIMEOUT, "acceptor to bind an ephemeral port")

        # ── Initiator engine (B): now that the port is known ──
        ecb = fixpp.engine_config_create()
        fixpp.engine_config_set_realtime_clock(ecb)
        eng_b = fixpp.engine_create(ecb)
        ini_sc = _make_session_config(
            dict_h, fixpp.ROLE_INITIATOR, "INITIATOR", "ACCEPTOR", port)  # reversed ids
        ini = fixpp.session_open(eng_b, ini_sc)
        fixpp.engine_start(eng_b)

        _wait_until(lambda: fixpp.session_is_established(acc),
                    ESTABLISH_TIMEOUT, "acceptor to establish")
        _wait_until(lambda: fixpp.session_is_established(ini),
                    ESTABLISH_TIMEOUT, "initiator to establish")

        # ── Build + send one app message via the outbound C-ABI surface (D-8) ──
        m = fixpp.msg_create_outbound(ini, MSG_TYPE)
        fixpp.msg_set_string(m, TAG_CLORDID, SENT)
        payload = fixpp.msg_commit(m)            # -> bytes
        assert isinstance(payload, bytes), f"commit must return bytes, got {type(payload)}"
        fixpp.session_send(ini, payload)
        fixpp.msg_destroy(m)

        assert got.wait(timeout=RECV_TIMEOUT), "no message received within deadline"
        assert received.get("value") == SENT, \
            f"round-trip mismatch: sent {SENT!r}, received {received.get('value')!r}"
    finally:
        # Teardown (reverse of construction): close sessions, destroy engines, dict.
        if ini is not None:
            fixpp.session_close(ini)
        if acc is not None:
            fixpp.session_close(acc)
        if eng_b is not None:
            fixpp.engine_destroy(eng_b)
        if eng_a is not None:
            fixpp.engine_destroy(eng_a)
        fixpp.dict_destroy(dict_h)


def test_config_str_rejects_embedded_nul():
    """A config const char* with an embedded NUL raises fixpp.Error (T-3: the
    NUL-terminated C input would silently truncate; conversion failures route
    through the shared error bridge, FR-008). Witness for FR-003/FR-004a."""
    sc = fixpp.session_config_create()
    with pytest.raises(fixpp.Error):
        fixpp.session_config_set_comp_ids(sc, "SEND\x00ER", "TARGET")
    with pytest.raises(fixpp.Error):
        fixpp.session_config_set_begin_string(sc, "FIX.4\x00.4")
    fixpp.session_config_destroy(sc)


def test_config_str_rejects_wrong_type():
    """A non-str config const char* raises fixpp.Error (single binding exception
    type, FR-008 / T-3), not a bare Python TypeError."""
    sc = fixpp.session_config_create()
    with pytest.raises(fixpp.Error):
        fixpp.session_config_set_begin_string(sc, 1234)        # int, not str
    with pytest.raises(fixpp.Error):
        fixpp.session_config_set_comp_ids(sc, b"bytes", "TARGET")  # bytes, not str
    fixpp.session_config_destroy(sc)


def test_bad_dict_path_raises_fixpp_error():
    """A non-OK fixpp_error_t raises fixpp.Error carrying the fixpp_strerror text
    (FR-008 / SC-005). RED witness for the T012 error bridge: against the T005
    skeleton the bad path does not raise fixpp.Error."""
    with pytest.raises(fixpp.Error) as ei:
        fixpp.dict_load_from_xml("/nonexistent/path/FIX_DOES_NOT_EXIST.xml")
    msg = str(ei.value)
    assert msg, "fixpp.Error carried an empty message"
    # The message must be the human-readable strerror text, not a bare code int.
    assert not msg.strip().lstrip("-").isdigit(), \
        f"expected fixpp_strerror text, got a bare code: {msg!r}"


# ── Gate-B r1 counter-tests (RC-A) ───────────────────────────────────────────

def test_register_callback_after_start_no_leak():
    """A failed session_register_callback (post-start, native returns non-OK) must
    NOT hold a reference to the callable.

    Fix 1 / RC-A (Gate-B r1): the old %typemap(in) Py_INCREF'd before the call;
    on failure the T012 out-typemap raised without a matching Py_DECREF, leaking
    the callable.  The hand-wrapper INCREFs only after FIXPP_ERR_OK.

    Discrimination: revert the fix → weakref() is non-None after gc.collect()
    (the leaked C-side ref kept the object alive).  With the fix it IS None."""
    import gc
    import weakref

    dict_h = fixpp.dict_load_from_xml(_dict_path())
    eng = sess = None
    try:
        ec = fixpp.engine_config_create()
        fixpp.engine_config_set_realtime_clock(ec)
        eng = fixpp.engine_create(ec)
        # Initiator targeting a refused port — engine_start returns immediately
        # (the async connect attempt runs in the background; we don't need it).
        sc = _make_session_config(
            dict_h, fixpp.ROLE_INITIATOR, "A", "B", 9)  # port 9 always refused
        sess = fixpp.session_open(eng, sc)
        fixpp.engine_start(eng)   # post-start: register_callback MUST return non-OK

        class _TrackableCallback:
            """Minimal callable; class instances are weakreffable."""
            def __call__(self, msg):
                pass

        callback = _TrackableCallback()
        ref = weakref.ref(callback)

        with pytest.raises(fixpp.Error):
            fixpp.session_register_callback(sess, callback)

        del callback   # drop the only Python-side reference
        gc.collect()   # force cycle collection

        assert ref() is None, (
            "session_register_callback leaked a reference to the callable on "
            "the failure path — Py_DECREF is missing after non-OK native return")
    finally:
        if sess is not None:
            try:
                fixpp.session_close(sess)
            except fixpp.Error:
                pass  # non-established initiator (port 9) returns lifecycle error
        if eng is not None:
            fixpp.engine_destroy(eng)
        fixpp.dict_destroy(dict_h)


def test_codec_failure_routes_through_fixpp_error():
    """A str containing a lone surrogate raises fixpp.Error, not UnicodeEncodeError.

    Fix 2 / RC-A (Gate-B r1): the old code left a bare UnicodeEncodeError set by
    PyUnicode_AsUTF8AndSize in the exception state (the _err==0 SWIG_fail path).
    The fix calls PyErr_Clear() then FIXPP_PY_RAISE so all conversion failures
    route through fixpp.Error (T-3 / FR-008 single binding exception type).

    Discrimination: revert the fix → pytest.raises(fixpp.Error) does NOT catch a
    UnicodeEncodeError and the test fails with an unexpected exception type."""
    sc = fixpp.session_config_create()
    try:
        # '\ud800' is an unpaired high surrogate — not representable in UTF-8.
        # PyUnicode_AsUTF8AndSize raises UnicodeEncodeError on it; the old code
        # let that propagate as a bare Python exception; the fix normalises to
        # fixpp.Error.
        with pytest.raises(fixpp.Error):
            fixpp.session_config_set_begin_string(sc, "FIX.4\ud800.4")
        # T009 FIXPP_CONFIG_STR_OR_NONE path (cert / key parameters):
        with pytest.raises(fixpp.Error):
            fixpp.session_config_set_security(
                sc, fixpp.SECURITY_INSECURE_PLAIN_TCP,
                "cert\ud800bad", None)   # surrogate in cert path
    finally:
        fixpp.session_config_destroy(sc)

