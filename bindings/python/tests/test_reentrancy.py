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

    # #295 — neutralise the synthetic objects before they go out of scope.
    #
    # They were built with `object.__new__` and FAKE handles (`_handle =
    # object()`), and the assertions above deliberately leave them with
    # `_dead = False` / `_was_explicitly_closed = False`. That combination is
    # exactly what makes _Finalizable.__del__ (fixpp_oo.py:99-111) act: at some
    # arbitrary later garbage collection -- plausibly inside a DIFFERENT test
    # file -- it calls close(), which passes the fake `object()` handle to
    # `session_close`/`engine_destroy`. SWIG's pointer-conversion typemap
    # rejects that during argument conversion and raises `TypeError` before
    # any native function is entered; `__del__` then swallows the exception,
    # as designed.
    #
    # These objects violate the class invariant that a live (non-`_dead`)
    # instance carries a real native handle. Leaving the deferred, GC-timed
    # close attempt in place serves no purpose. Marking them closed makes
    # __del__ return at its first guard and removes it entirely.
    for _synthetic in (active, sibling, engine):
        _synthetic._dead = True
        _synthetic._was_explicitly_closed = True


def test_session_close_step0_backstop_leaves_state_unmodified(monkeypatch):
    native = {"closed": 0}

    class _App(fixpp.Application):
        def __init__(self):
            self.result = None
            self.done = threading.Event()

        def fromApp(self, session, msg):
            try:
                session.close()
            except Exception as exc:
                self.result = (exc, session._dead, session._application is self)
            finally:
                self.done.set()

    app = _App()
    dict_h, acc_engine, ini_engine, acc, ini = establish_pair(app)
    try:
        # #295 — count ONLY closes of the session under test, and install the
        # patch AFTER establish_pair rather than before it.
        #
        # The previous form patched the module-level `session_close` before the
        # pair existed and counted EVERY call in the process. That made the
        # `native["closed"] == 0` assertion below mean "no unrelated Engine or
        # Session ANYWHERE in this process had its __del__ fire during this
        # ~0.1s window" -- which is not what this test is named for and not
        # something it can control. The path is real: _Finalizable.__del__
        # (fixpp_oo.py:99-111) calls close() -> _close_impl -> session_close
        # (fixpp_oo.py:255), and Engine.close() cascades the same way once per
        # live session it owns. Any object left unclosed by an EARLIER test and
        # finalized inside this window incremented the counter and failed this
        # test for a reason unrelated to the reentrancy backstop.
        #
        # SWIG 4.5.0 exposed exactly that: it made the 3.12 leg fail
        # deterministically, and the fragility predates the bump. The
        # generated teardown surface this test exercises directly
        # (SwigPyObject_dealloc, the tp_* slots, _wrap_session_close,
        # _wrap_engine_destroy) is byte-identical between SWIG 4.4.1 and
        # 4.5.0 -- but that only shows the diff does not obviously change
        # teardown, not that SWIG 4.5.0 introduced nothing. Other generated
        # code reachable from this test DID change: the reentrant guard's
        # exception is built via `_make_error` -> `strerror`
        # (fixpp_oo.py:245, fixpp.i:287-295), whose 4.5.0 wrapper routes
        # through the new `SWIG_FromBinaryCharPtrAndSize` path.
        #
        # Keying on the handle makes the assertion say what the test name says:
        # the step-0 backstop performed no native close FOR THIS SESSION.
        # Non-target handles are delegated to the real function so that
        # finalizers firing during the window still do their actual work
        # instead of being silently swallowed.
        target_handle = acc._handle
        real_session_close = fixpp_oo.session_close

        def _counting_session_close(handle):
            if handle == target_handle:
                native["closed"] += 1
                return None
            return real_session_close(handle)

        monkeypatch.setattr(fixpp_oo, "session_close", _counting_session_close)

        send_app_message(ini)
        assert app.done.wait(timeout=RECV_TIMEOUT), "fromApp callback never completed"
        # A named failure beats the TypeError that unpacking None would raise.
        # `fromApp` records `result` only in its `except` branch, so if the
        # step-0 backstop ever stops raising, `result` stays None and every
        # assertion below becomes unreachable. Say that plainly instead.
        assert app.result is not None, (
            "session.close() inside fromApp did not raise -- the step-0 "
            "reentrant-close backstop is absent, so the assertions below "
            "cannot be evaluated")
        exc, dead, app_still_attached = app.result
        assert isinstance(exc, fixpp.CallbackReentrantClose)
        assert exc.code == 1204
        assert dead is False
        assert app_still_attached is True
        assert native["closed"] == 0
    finally:
        # Undo before teardown so close_pair() performs REAL closes. Under the
        # previous form the patch was still active here (pytest undoes it only
        # after the test returns), so every close in teardown was swallowed.
        monkeypatch.undo()
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
