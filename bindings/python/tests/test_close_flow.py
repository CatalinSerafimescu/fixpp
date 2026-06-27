import threading
import weakref

import pytest

import fixpp
import fixpp_oo


class _DummyMessage:
    def __init__(self):
        self._dead = False


def test_session_close_orders_message_invalidation_before_native_close(monkeypatch):
    native_seen = {}
    session = object.__new__(fixpp.Session)
    session._handle = object()
    session._dead = False
    session._was_explicitly_closed = False
    session._engine = None
    msg = _DummyMessage()
    session._messages = weakref.WeakSet([msg])
    session._application = object()
    session._in_callback = False
    session._application_registered = True

    def fake_close(handle):
        native_seen["msg_dead"] = msg._dead
        native_seen["session_dead"] = session._dead
        native_seen["handle"] = handle

    monkeypatch.setattr(fixpp_oo, "session_close", fake_close)
    monkeypatch.setattr(fixpp_oo, "_release_application_oo", lambda wrapper: None)

    session.close()

    assert native_seen == {
        "msg_dead": True,
        "session_dead": True,
        "handle": session._handle,
    }
    assert session._application is None
    assert session._was_explicitly_closed is True


def test_close_is_idempotent_and_does_not_double_free(monkeypatch):
    calls = {"session_close": 0, "release": 0}
    session = object.__new__(fixpp.Session)
    session._handle = object()
    session._dead = False
    session._was_explicitly_closed = False
    session._engine = None
    session._messages = weakref.WeakSet()
    session._application = object()
    session._in_callback = False
    session._application_registered = True

    monkeypatch.setattr(
        fixpp_oo, "session_close",
        lambda handle: calls.__setitem__("session_close", calls["session_close"] + 1),
    )
    monkeypatch.setattr(
        fixpp_oo, "_release_application_oo",
        lambda wrapper: calls.__setitem__("release", calls["release"] + 1),
    )

    session.close()
    session.close()

    assert calls == {"session_close": 1, "release": 1}


def test_engine_close_invalidates_child_sessions_weakly(monkeypatch):
    calls = {"destroy": 0}
    engine = object.__new__(fixpp.Engine)
    engine._handle = object()
    engine._dead = False
    engine._was_explicitly_closed = False
    engine._sessions = weakref.WeakSet()

    child = object.__new__(fixpp.Session)
    child._handle = object()
    child._dead = False
    child._was_explicitly_closed = False
    child._engine = engine
    child._messages = weakref.WeakSet()
    child._application = None
    child._application_registered = False
    child._in_callback = False
    engine._sessions.add(child)

    monkeypatch.setattr(
        fixpp_oo, "engine_destroy",
        lambda handle: calls.__setitem__("destroy", calls["destroy"] + 1),
    )
    monkeypatch.setattr(fixpp_oo, "session_close", lambda handle: None)
    monkeypatch.setattr(fixpp_oo, "_release_application_oo", lambda wrapper: None)

    engine.close()

    assert child._dead is True
    with pytest.raises(fixpp.ObjectLifetime) as ei:
        child.is_established()
    assert ei.value.code == 1202
    assert calls["destroy"] == 1


def test_concurrent_engine_entry_during_close_raises_without_cabi_entry(monkeypatch):
    engine = object.__new__(fixpp.Engine)
    engine._handle = object()
    engine._dead = False
    engine._was_explicitly_closed = False
    engine._sessions = weakref.WeakSet()

    parked = threading.Event()
    release = threading.Event()
    calls = {"session_open": 0, "engine_start": 0}

    monkeypatch.setattr(
        fixpp_oo, "engine_destroy",
        lambda handle: (parked.set(), release.wait(timeout=5.0)),
    )

    def bomb_session_open(*args, **kwargs):
        calls["session_open"] += 1
        raise AssertionError("session_open should not be reached")

    def bomb_engine_start(*args, **kwargs):
        calls["engine_start"] += 1
        raise AssertionError("engine_start should not be reached")

    monkeypatch.setattr(fixpp_oo, "session_open", bomb_session_open)
    monkeypatch.setattr(fixpp_oo, "engine_start", bomb_engine_start)

    close_thread = threading.Thread(target=engine.close)
    close_thread.start()
    assert parked.wait(timeout=1.0), "engine.close never reached destroy"

    with pytest.raises(fixpp.ObjectLifetime) as open_ei:
        engine.open_session(object())
    assert open_ei.value.code == 1202

    with pytest.raises(fixpp.ObjectLifetime) as start_ei:
        engine.start()
    assert start_ei.value.code == 1202

    release.set()
    close_thread.join(timeout=2.0)
    assert not close_thread.is_alive()
    assert calls == {"session_open": 0, "engine_start": 0}
