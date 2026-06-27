"""Shared helpers for PY-004 OO-layer tests."""

import os
import time

import fixpp

HOST = "127.0.0.1"
MSG_TYPE = "D"
TAG_CLORDID = 11
SENT = "ORDER-PY-004-US2"

BIND_TIMEOUT = 5.0
ESTABLISH_TIMEOUT = 5.0
RECV_TIMEOUT = 5.0
POLL_INTERVAL = 0.02


def dict_path():
    here = os.path.dirname(os.path.abspath(__file__))
    repo_root = os.path.normpath(os.path.join(here, "..", "..", ".."))
    return os.path.join(repo_root, "dictionaries", "FIX44.xml")


def wait_until(predicate, timeout, what):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(POLL_INTERVAL)
    raise AssertionError(f"timed out after {timeout}s waiting for: {what}")


def make_session_config(dict_h, role, sender, target, port):
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


def make_engine(*, worker_threads=None):
    cfg = fixpp.engine_config_create()
    fixpp.engine_config_set_realtime_clock(cfg)
    if worker_threads is not None:
        fixpp.engine_config_set_worker_threads(cfg, worker_threads)
    return fixpp.Engine(cfg)


def establish_pair(application, *, acceptor_workers=None):
    dict_h = fixpp.dict_load_from_xml(dict_path())
    acc_engine = ini_engine = acc = ini = None
    try:
        acc_engine = make_engine(worker_threads=acceptor_workers)
        acc_sc = make_session_config(
            dict_h, fixpp.ROLE_ACCEPTOR, "OO2_ACC", "OO2_INI", 0)
        acc = acc_engine.open_session(acc_sc)
        acc.register_application(application)
        acc_engine.start()

        port = wait_until(
            lambda: acc.acceptor_bound_endpoint() or None,
            BIND_TIMEOUT, "acceptor to bind an ephemeral port")

        ini_engine = make_engine()
        ini_sc = make_session_config(
            dict_h, fixpp.ROLE_INITIATOR, "OO2_INI", "OO2_ACC", port)
        ini = ini_engine.open_session(ini_sc)
        ini_engine.start()

        wait_until(lambda: acc.is_established(),
                   ESTABLISH_TIMEOUT, "acceptor to establish")
        wait_until(lambda: ini.is_established(),
                   ESTABLISH_TIMEOUT, "initiator to establish")
        return dict_h, acc_engine, ini_engine, acc, ini
    except Exception:
        close_pair(ini_engine, acc_engine, dict_h)
        raise


def send_app_message(session, value=SENT):
    outbound = session.create_message(MSG_TYPE)
    outbound.set_string(TAG_CLORDID, value)
    session.send(outbound)
    return outbound


def close_pair(ini_engine, acc_engine, dict_h):
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
