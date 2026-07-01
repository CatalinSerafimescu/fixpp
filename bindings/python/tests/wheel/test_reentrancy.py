import threading
import weakref

import pytest

import fixpp
import fixpp_oo

from _oo_reentrancy_staging import run_staging
from oo_test_support import (
    RECV_TIMEOUT,
    close_pair,
    establish_pair,
    send_app_message,
    wait_until,
)


def test_send_close_and_engine_close_reentrant_watchdog():
    for op in ("send", "session_close", "engine_close"):
        result = run_staging(op, timeout=20)
        assert result.returncode == 0, result.stderr or result.stdout


def test_reentrancy_guard_survives_rotating_pool():
    result = run_staging("send", worker_threads=4, timeout=20)
    assert result.returncode == 0, result.stderr or result.stdout


def test_engine_close_preflight_raises_without_closing_sibling(monkeypatch):
    engine = object.__new__(fixpp.Engine)
    engine._handle = object()
    engine._dead = False
    engine._was_explicitly_closed = False
    engine._sessions = weakref.WeakSet()

    active = object.__new__(fixpp.Session)
    active._handle = object()
    active._dead = False
    active._was_explicitly_closed = False
    active._engine = engine
    active._messages = weakref.WeakSet()
    active._application = object()
    active._application_registered = True
    active._in_callback = True

    sibling = object.__new__(fixpp.Session)
    sibling._handle = object()
    sibling._dead = False
    sibling._was_explicitly_closed = False
    sibling._engine = engine
    sibling._messages = weakref.WeakSet()
    sibling._application = object()
    sibling._application_registered = True
    sibling._in_callback = False

    engine._sessions.add(active)
    engine._sessions.add(sibling)

    touched = {"destroy": 0}
    monkeypatch.setattr(
        fixpp.Session, "_close_from_engine",
        lambda self: (_ for _ in ()).throw(AssertionError("child close should not run")),
    )
    monkeypatch.setattr(
        fixpp_oo, "engine_destroy",
        lambda handle: touched.__setitem__("destroy", touched["destroy"] + 1),
    )

    with pytest.raises(fixpp.CallbackReentrantClose) as ei:
        engine.close()

    assert ei.value.code == 1204
    assert sibling._dead is False
    assert active._dead is False
    assert touched["destroy"] == 0


def test_session_close_step0_backstop_leaves_state_unmodified(monkeypatch):
    native = {"closed": 0}

    class _App(fixpp.Application):
        def __init__(self):
            self.result = None

        def fromApp(self, session, msg):
            try:
                session.close()
            except Exception as exc:
                self.result = (exc, session._dead, session._application is self)

    monkeypatch.setattr(
        fixpp_oo, "session_close",
        lambda handle: native.__setitem__("closed", native["closed"] + 1),
    )

    app = _App()
    dict_h, acc_engine, ini_engine, acc, ini = establish_pair(app)
    try:
        send_app_message(ini)
        deadline = threading.Event()
        assert deadline.wait(0.1) is False
        exc, dead, app_still_attached = app.result
        assert isinstance(exc, fixpp.CallbackReentrantClose)
        assert exc.code == 1204
        assert dead is False
        assert app_still_attached is True
        assert native["closed"] == 0
    finally:
        close_pair(ini_engine, acc_engine, dict_h)


def test_callback_exception_exit_clears_in_callback_marker():
    fired = threading.Event()

    class _RaisingApp(fixpp.Application):
        def fromApp(self, session, msg):
            fired.set()
            raise RuntimeError("intentional callback failure")

    app = _RaisingApp()
    dict_h, acc_engine, ini_engine, acc, ini = establish_pair(app)
    try:
        outbound = send_app_message(ini)
        assert fired.wait(timeout=RECV_TIMEOUT), "callback never fired"
        # The raising callback ran on the ACCEPTOR session. The trampoline must
        # clear acc._in_callback on the exception-exit path; if it does not, the
        # marker stays True forever — this wait_until times out (the regression
        # signal) and the follow-up acc.send() below would falsely raise 1204.
        wait_until(lambda: acc._in_callback is False, RECV_TIMEOUT,
                   "acceptor _in_callback to clear after the raising callback")
        # A legitimate send on the SAME session whose callback raised must NOT
        # falsely raise CallbackReentrantClose (FR-017 exit discipline).
        follow_up = acc.create_message("D")
        follow_up.set_string(11, "FOLLOWUP")
        acc.send(follow_up)
        outbound.destroy()
        follow_up.destroy()
    finally:
        close_pair(ini_engine, acc_engine, dict_h)
