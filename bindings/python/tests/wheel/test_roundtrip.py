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

import _wheeldict

# ── Test parameters (research D-7 — verified against dictionaries/FIX44.xml) ──
HOST = "127.0.0.1"
MSG_TYPE = "D"          # NewOrderSingle (msgcat='app' -> routes to the callback)
TAG_CLORDID = 11        # ClOrdID, type STRING, required scalar field of "D"
SENT = "ORDER-PY-001"
TAG_TEXT = 58           # Text, a free-form STRING field carried verbatim on the wire
NON_UTF8 = b"\xff\xfe"  # invalid UTF-8 (0xFF/0xFE are never valid lead bytes); no
                        # SOH (0x01) / '=' (0x3D), so it survives FIX framing intact

# Bounded deadlines (seconds). Generous enough for a cold loopback under CI load,
# short enough that a real failure fails fast instead of hanging.
BIND_TIMEOUT = 5.0
ESTABLISH_TIMEOUT = 5.0
RECV_TIMEOUT = 5.0
POLL_INTERVAL = 0.02


def _dict_path():
    # Wheel-suite port (T012): resolve the bundled FIX44 dict through the installed
    # locator (never a repo-relative path), so this runs against an installed wheel.
    return _wheeldict.resolve("FIX44")


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
    dict_h = fixpp.dict_load_from_xml(_dict_path())
    ec = fixpp.engine_config_create()
    fixpp.engine_config_set_realtime_clock(ec)
    eng = fixpp.engine_create(ec)
    sc = fixpp.session_config_create()
    sess = None
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
        sc_msg = _make_session_config(
            dict_h, fixpp.ROLE_INITIATOR, "SENDER", "TARGET", 9)
        sess = fixpp.session_open(eng, sc_msg)
        with pytest.raises(fixpp.Error):
            fixpp.msg_create_outbound(sess, "\ud800")
    finally:
        fixpp.engine_destroy(eng)
        fixpp.session_config_destroy(sc)
        fixpp.dict_destroy(dict_h)


def test_msg_get_string_non_utf8_routes_through_fixpp_error():
    """DECODE side: msg_get_string on non-UTF-8 wire bytes raises fixpp.Error.

    Companion to test_codec_failure_routes_through_fixpp_error (the ENCODE side:
    a Python str with a surrogate → set_string/config). This is the OUTPUT
    codec path in fixpp.i: the msg_get_string argout typemap decodes the aliased
    wire bytes via PyUnicode_FromStringAndSize; a non-UTF-8 value returns NULL,
    which must route through fixpp.Error (T-3 / FR-008), not surface a bare
    UnicodeDecodeError or hand back a NULL object. Retires the standing 053
    waiver / PY-003 residue (the `_str == NULL` branch was never witnessed).

    The thin binding wraps no raw-bytes field setter (msg_set_string validates
    UTF-8 on input), so the sender hand-builds the app payload in the msg_commit
    wire format ("35=<type>\\x01<tag>=<value>\\x01..."; message_write.cpp:673) and
    injects the invalid bytes directly; Engine::send stamps the session envelope
    around this body. They round-trip through the wire and inbound parse intact
    (no SOH/'='), so the acceptor's callback reads them back and the decode fails
    on genuinely non-UTF-8 wire data — not a synthesised handle.

    Discrimination: drop the `_str == NULL` guard in the argout typemap →
    fixpp.Error is not raised (a NULL is appended / a bare exception leaks) and
    this test fails, so it pins that exact branch."""
    dict_h = fixpp.dict_load_from_xml(_dict_path())

    captured = {}
    got = threading.Event()

    def on_message(inbound):
        # Runs on an engine worker thread (GIL reacquired). Read the non-UTF-8
        # field INSIDE the dispatch window and record what msg_get_string does.
        try:
            fixpp.msg_get_string(inbound, TAG_TEXT)
            captured["exc"] = None          # no raise → the guard is missing (BUG)
        except Exception as e:              # noqa: BLE001 — record the exact type
            captured["exc"] = e
        finally:
            got.set()

    eng_a = eng_b = acc = ini = None
    try:
        eca = fixpp.engine_config_create()
        fixpp.engine_config_set_realtime_clock(eca)
        eng_a = fixpp.engine_create(eca)
        acc_sc = _make_session_config(
            dict_h, fixpp.ROLE_ACCEPTOR, "ACCEPTOR", "INITIATOR", 0)
        acc = fixpp.session_open(eng_a, acc_sc)
        fixpp.session_register_callback(acc, on_message)
        fixpp.engine_start(eng_a)

        port = _wait_until(
            lambda: fixpp.session_acceptor_bound_endpoint(acc) or None,
            BIND_TIMEOUT, "acceptor to bind an ephemeral port")

        ecb = fixpp.engine_config_create()
        fixpp.engine_config_set_realtime_clock(ecb)
        eng_b = fixpp.engine_create(ecb)
        ini_sc = _make_session_config(
            dict_h, fixpp.ROLE_INITIATOR, "INITIATOR", "ACCEPTOR", port)
        ini = fixpp.session_open(eng_b, ini_sc)
        fixpp.engine_start(eng_b)

        _wait_until(lambda: fixpp.session_is_established(acc),
                    ESTABLISH_TIMEOUT, "acceptor to establish")
        _wait_until(lambda: fixpp.session_is_established(ini),
                    ESTABLISH_TIMEOUT, "initiator to establish")

        # Hand-built app payload in the msg_commit wire format so the non-UTF-8
        # value can be injected directly (the binding wraps no raw-bytes setter).
        payload = (
            b"35=" + MSG_TYPE.encode() + b"\x01"
            + str(TAG_CLORDID).encode() + b"=" + SENT.encode() + b"\x01"
            + str(TAG_TEXT).encode() + b"=" + NON_UTF8 + b"\x01"
        )
        fixpp.session_send(ini, payload)

        assert got.wait(timeout=RECV_TIMEOUT), "no message received within deadline"
        exc = captured.get("exc")
        assert exc is not None, \
            "msg_get_string returned instead of raising on non-UTF-8 wire bytes"
        assert isinstance(exc, fixpp.Error), \
            f"expected fixpp.Error, got {type(exc).__name__}: {exc}"
        assert "not valid UTF-8" in str(exc), \
            f"unexpected fixpp.Error message: {exc!r}"
    finally:
        if ini is not None:
            fixpp.session_close(ini)
        if acc is not None:
            fixpp.session_close(acc)
        if eng_b is not None:
            fixpp.engine_destroy(eng_b)
        if eng_a is not None:
            fixpp.engine_destroy(eng_a)
        fixpp.dict_destroy(dict_h)


# ── Gate-B r2 counter-tests (Fix 1: GIL release) ─────────────────────────────

# Generous teardown deadline for session_close + engine_destroy on CI (both run
# asynchronously via fut.get(); under load, a 5 s RECV_TIMEOUT + teardown must
# comfortably fit within 10 s).
TEARDOWN_TIMEOUT = 10.0


def test_raising_callback_no_hang():
    """A raising callback + concurrent session_close must NOT deadlock (SC-004).

    Root cause (Gate-B r1 escalation): the blocking SWIG wrappers held the GIL
    while waiting on the io_context worker (fut.get / join). When the recv
    trampoline fired a raising callback, PyErr_Print / raise needed the GIL;
    session_close simultaneously held the GIL in fut.get -> deadlock.

    Fix 1 (Gate-B r2): %exception blocks release the GIL around $action for
    fixpp_engine_destroy / fixpp_session_close / fixpp_session_send. This test
    is the in-process positive witness: teardown must complete within
    TEARDOWN_TIMEOUT.

    Regression truth: revert the %exception blocks in fixpp.i and the teardown
    thread can block in C while holding the GIL; the main thread's
    td.join(timeout) then cannot return to run the assert because the GIL is
    held hostage in C. The regression manifests as a process hang caught by the
    CI job-level timeout, not a clean in-test assertion failure.
    """
    dict_path = _dict_path()
    assert os.path.isfile(dict_path), f"missing bundled dictionary: {dict_path}"
    dict_h = fixpp.dict_load_from_xml(dict_path)

    callback_fired = threading.Event()

    def raising_callback(inbound):
        # Touch the borrowed proxy (FR-014 path) — exercises the msg_get_string
        # trampoline inside the dispatch window.
        try:
            fixpp.msg_get_string(inbound, TAG_CLORDID)
        except fixpp.Error:
            pass
        callback_fired.set()
        # time.sleep() releases the GIL for 150ms, giving the main/_teardown
        # thread a chance to acquire the GIL and enter session_close.  After
        # waking, the worker needs the GIL again to execute `raise` — without
        # Fix 1 this deadlocks (worker needs GIL; _teardown holds GIL in
        # fut.get).  With Fix 1 the worker re-acquires normally and raises
        # (PyErr_Print logs it; the trampoline continues safely).
        time.sleep(0.15)
        raise RuntimeError("intentional: test SC-004 raising callback")

    eng_a = eng_b = acc = ini = None
    try:
        eca = fixpp.engine_config_create()
        fixpp.engine_config_set_realtime_clock(eca)
        eng_a = fixpp.engine_create(eca)
        acc_sc = _make_session_config(
            dict_h, fixpp.ROLE_ACCEPTOR, "RC_ACCEP", "RC_INITI", 0)
        acc = fixpp.session_open(eng_a, acc_sc)
        fixpp.session_register_callback(acc, raising_callback)
        fixpp.engine_start(eng_a)

        port = _wait_until(
            lambda: fixpp.session_acceptor_bound_endpoint(acc) or None,
            BIND_TIMEOUT, "raising-callback acceptor to bind")

        ecb = fixpp.engine_config_create()
        fixpp.engine_config_set_realtime_clock(ecb)
        eng_b = fixpp.engine_create(ecb)
        ini_sc = _make_session_config(
            dict_h, fixpp.ROLE_INITIATOR, "RC_INITI", "RC_ACCEP", port)
        ini = fixpp.session_open(eng_b, ini_sc)
        fixpp.engine_start(eng_b)

        _wait_until(lambda: fixpp.session_is_established(acc),
                    ESTABLISH_TIMEOUT, "raising-callback acceptor to establish")
        _wait_until(lambda: fixpp.session_is_established(ini),
                    ESTABLISH_TIMEOUT, "raising-callback initiator to establish")

        m = fixpp.msg_create_outbound(ini, MSG_TYPE)
        fixpp.msg_set_string(m, TAG_CLORDID, SENT)
        payload = fixpp.msg_commit(m)
        fixpp.session_send(ini, payload)
        fixpp.msg_destroy(m)

        fired = callback_fired.wait(timeout=RECV_TIMEOUT)
        assert fired, "raising callback never fired within deadline"

    finally:
        # Teardown in a daemon thread with a bounded join deadline (research D-2).
        _ini, _acc, _eng_b, _eng_a, _dh = ini, acc, eng_b, eng_a, dict_h

        def _teardown():
            if _ini is not None:
                try:
                    fixpp.session_close(_ini)
                except fixpp.Error:
                    pass
            if _acc is not None:
                try:
                    fixpp.session_close(_acc)
                except fixpp.Error:
                    pass
            if _eng_b is not None:
                fixpp.engine_destroy(_eng_b)
            if _eng_a is not None:
                fixpp.engine_destroy(_eng_a)
            fixpp.dict_destroy(_dh)

        td = threading.Thread(target=_teardown, daemon=True)
        td.start()
        td.join(timeout=TEARDOWN_TIMEOUT)
        assert not td.is_alive(), (
            f"teardown deadlocked (SC-004): session_close or engine_destroy did "
            f"not complete within {TEARDOWN_TIMEOUT}s -- blocking wrappers must "
            "release the GIL around fut.get() to avoid deadlock with the recv "
            "trampoline (Gate-B r2 Fix 1: %exception Py_BEGIN_ALLOW_THREADS).")

    # Phase 2: verify interpreter is not corrupted after the raising callback.
    # A fresh two-engine loopback with a normal callback must work normally.
    dict_h2 = fixpp.dict_load_from_xml(dict_path)
    received2 = {}
    got2 = threading.Event()

    def normal_callback(inbound):
        received2["value"] = fixpp.msg_get_string(inbound, TAG_CLORDID)
        got2.set()

    eng_a2 = eng_b2 = acc2 = ini2 = None
    try:
        eca2 = fixpp.engine_config_create()
        fixpp.engine_config_set_realtime_clock(eca2)
        eng_a2 = fixpp.engine_create(eca2)
        acc_sc2 = _make_session_config(
            dict_h2, fixpp.ROLE_ACCEPTOR, "FRESH_A", "FRESH_B", 0)
        acc2 = fixpp.session_open(eng_a2, acc_sc2)
        fixpp.session_register_callback(acc2, normal_callback)
        fixpp.engine_start(eng_a2)

        port2 = _wait_until(
            lambda: fixpp.session_acceptor_bound_endpoint(acc2) or None,
            BIND_TIMEOUT, "fresh acceptor to bind")

        ecb2 = fixpp.engine_config_create()
        fixpp.engine_config_set_realtime_clock(ecb2)
        eng_b2 = fixpp.engine_create(ecb2)
        ini_sc2 = _make_session_config(
            dict_h2, fixpp.ROLE_INITIATOR, "FRESH_B", "FRESH_A", port2)
        ini2 = fixpp.session_open(eng_b2, ini_sc2)
        fixpp.engine_start(eng_b2)

        _wait_until(lambda: fixpp.session_is_established(acc2),
                    ESTABLISH_TIMEOUT, "fresh acceptor to establish")
        _wait_until(lambda: fixpp.session_is_established(ini2),
                    ESTABLISH_TIMEOUT, "fresh initiator to establish")

        m2 = fixpp.msg_create_outbound(ini2, MSG_TYPE)
        fixpp.msg_set_string(m2, TAG_CLORDID, SENT)
        payload2 = fixpp.msg_commit(m2)
        fixpp.session_send(ini2, payload2)
        fixpp.msg_destroy(m2)

        assert got2.wait(timeout=RECV_TIMEOUT), \
            "fresh session: no message received -- interpreter may be corrupted"
        assert received2.get("value") == SENT, \
            (f"fresh session: field mismatch: sent {SENT!r}, "
             f"got {received2.get('value')!r}")
    finally:
        if ini2 is not None:
            try:
                fixpp.session_close(ini2)
            except fixpp.Error:
                pass
        if acc2 is not None:
            try:
                fixpp.session_close(acc2)
            except fixpp.Error:
                pass
        if eng_b2 is not None:
            fixpp.engine_destroy(eng_b2)
        if eng_a2 is not None:
            fixpp.engine_destroy(eng_a2)
        fixpp.dict_destroy(dict_h2)
