"""OO lifetime/ownership witnesses for PY-004 Phase 3 (055 / US1)."""

import os
import threading
import time

import pytest

import fixpp

HOST = "127.0.0.1"
MSG_TYPE = "D"
TAG_CLORDID = 11
SENT = "ORDER-PY-004"

BIND_TIMEOUT = 5.0
ESTABLISH_TIMEOUT = 5.0
RECV_TIMEOUT = 5.0
POLL_INTERVAL = 0.02


def _dict_path():
    here = os.path.dirname(os.path.abspath(__file__))
    repo_root = os.path.normpath(os.path.join(here, "..", "..", ".."))
    return os.path.join(repo_root, "dictionaries", "FIX44.xml")


def _wait_until(predicate, timeout, what):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(POLL_INTERVAL)
    raise AssertionError(f"timed out after {timeout}s waiting for: {what}")


def _make_session_config(dict_h, role, sender, target, port):
    sc = fixpp.session_config_create()
    fixpp.session_config_set_role(sc, role)
    fixpp.session_config_set_comp_ids(sc, sender, target)
    fixpp.session_config_set_begin_string(sc, "FIX.4.4")
    fixpp.session_config_set_dictionary(sc, dict_h)
    fixpp.session_config_set_security(
        sc, fixpp.SECURITY_INSECURE_PLAIN_TCP, None, None)
    fixpp.session_config_set_heartbeat_seconds(sc, 30)
    fixpp.session_config_set_reset_on_logon(sc, role == fixpp.ROLE_INITIATOR)
    fixpp.session_config_set_reset_seqnum_policy(
        sc, fixpp.RESET_SEQNUM_BILATERAL_LENIENT)
    fixpp.session_config_set_tcp_endpoint(sc, HOST, port)
    return sc


def _make_engine():
    cfg = fixpp.engine_config_create()
    fixpp.engine_config_set_realtime_clock(cfg)
    return fixpp.Engine(cfg)


def _establish_pair(application):
    dict_h = fixpp.dict_load_from_xml(_dict_path())
    acc_engine = ini_engine = acc = ini = None
    try:
        acc_engine = _make_engine()
        acc_sc = _make_session_config(
            dict_h, fixpp.ROLE_ACCEPTOR, "OO_ACC", "OO_INI", 0)
        acc = acc_engine.open_session(acc_sc)
        acc.register_application(application)
        acc_engine.start()

        port = _wait_until(
            lambda: acc.acceptor_bound_endpoint() or None,
            BIND_TIMEOUT, "acceptor to bind an ephemeral port")

        ini_engine = _make_engine()
        ini_sc = _make_session_config(
            dict_h, fixpp.ROLE_INITIATOR, "OO_INI", "OO_ACC", port)
        ini = ini_engine.open_session(ini_sc)
        ini_engine.start()

        _wait_until(
            lambda: acc.is_established(),
            ESTABLISH_TIMEOUT,
            "acceptor to establish")
        _wait_until(
            lambda: ini.is_established(),
            ESTABLISH_TIMEOUT,
            "initiator to establish")

        return dict_h, acc_engine, ini_engine, acc, ini
    except Exception:
        if ini_engine is not None:
            try:
                ini_engine.close()
            except Exception:
                pass
        if acc_engine is not None:
            try:
                acc_engine.close()
            except Exception:
                pass
        fixpp.dict_destroy(dict_h)
        raise


class _StashingApp(fixpp.Application):
    def __init__(self, store, event, should_raise=False):
        self._store = store
        self._event = event
        self._should_raise = should_raise

    def fromApp(self, session, msg):
        self._store["session"] = session
        self._store["msg"] = msg
        self._event.set()
        if self._should_raise:
            raise RuntimeError("intentional test callback failure")


def _close_pair(ini_engine, acc_engine, dict_h):
    if ini_engine is not None:
        try:
            ini_engine.close()
        except fixpp.FixppError:
            pass
    if acc_engine is not None:
        try:
            acc_engine.close()
        except fixpp.FixppError:
            pass
    if dict_h is not None:
        fixpp.dict_destroy(dict_h)


def test_session_send_after_engine_close_raises_object_lifetime():
    app = _StashingApp({}, threading.Event())
    dict_h, acc_engine, ini_engine, _acc, ini = _establish_pair(app)
    try:
        outbound = ini.create_message(MSG_TYPE)
        outbound.set_string(TAG_CLORDID, SENT)

        ini_engine.close()

        with pytest.raises(fixpp.ObjectLifetime) as ei:
            ini.send(outbound)
        assert ei.value.code == 1202
    finally:
        _close_pair(None, acc_engine, dict_h)


def test_inbound_message_stashed_after_callback_and_close_raises_object_lifetime():
    store = {}
    received = threading.Event()
    app = _StashingApp(store, received)
    dict_h, acc_engine, ini_engine, _acc, ini = _establish_pair(app)
    try:
        outbound = ini.create_message(MSG_TYPE)
        outbound.set_string(TAG_CLORDID, SENT)
        ini.send(outbound)

        assert received.wait(timeout=RECV_TIMEOUT), "callback never fired"

        stashed = store["msg"]

        with pytest.raises(fixpp.ObjectLifetime) as ei:
            stashed.get_string(TAG_CLORDID)
        assert ei.value.code == 1202

        store["session"].close()

        with pytest.raises(fixpp.ObjectLifetime) as ei:
            stashed.get_string(TAG_CLORDID)
        assert ei.value.code == 1202
    finally:
        _close_pair(ini_engine, acc_engine, dict_h)


def test_raising_callback_stashed_inbound_message_is_dead_after_exception_exit():
    store = {}
    received = threading.Event()
    app = _StashingApp(store, received, should_raise=True)
    dict_h, acc_engine, ini_engine, _acc, ini = _establish_pair(app)
    try:
        outbound = ini.create_message(MSG_TYPE)
        outbound.set_string(TAG_CLORDID, SENT)
        ini.send(outbound)

        assert received.wait(timeout=RECV_TIMEOUT), "raising callback never fired"

        with pytest.raises(fixpp.ObjectLifetime) as ei:
            store["msg"].get_string(TAG_CLORDID)
        assert ei.value.code == 1202
    finally:
        _close_pair(ini_engine, acc_engine, dict_h)


def test_dictionary_destroy_invalidates_accessors():
    dictionary = fixpp.Dictionary.load_xml(_dict_path())
    dictionary.destroy()

    with pytest.raises(fixpp.ObjectLifetime) as ei:
        dictionary.native_handle()
    assert ei.value.code == 1202
