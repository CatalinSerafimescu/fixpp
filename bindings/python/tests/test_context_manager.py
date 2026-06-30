import gc
import warnings
import weakref

import fixpp
import fixpp_oo


def test_engine_context_manager_closes_on_exit(monkeypatch):
    calls = []
    engine = object.__new__(fixpp.Engine)
    engine._handle = object()
    engine._dead = False
    engine._was_explicitly_closed = False
    engine._sessions = weakref.WeakSet()

    monkeypatch.setattr(
        fixpp.Engine, "close",
        lambda self: (calls.append("engine-close"),
                      setattr(self, "_dead", True),
                      setattr(self, "_was_explicitly_closed", True)))

    with engine as entered:
        assert entered is engine

    assert calls == ["engine-close"]


def test_session_context_manager_closes_on_exit(monkeypatch):
    calls = []
    session = object.__new__(fixpp.Session)
    session._handle = object()
    session._dead = False
    session._was_explicitly_closed = False
    session._engine = None
    session._messages = weakref.WeakSet()
    session._application = None
    session._application_registered = False
    session._in_callback = False

    monkeypatch.setattr(
        fixpp.Session, "close",
        lambda self: (calls.append("session-close"),
                      setattr(self, "_dead", True),
                      setattr(self, "_was_explicitly_closed", True)))

    with session as entered:
        assert entered is session

    assert calls == ["session-close"]


def test_gc_only_teardown_warns_and_attempts_best_effort_close(monkeypatch):
    calls = []

    def fake_close(self):
        calls.append(type(self).__name__)
        self._dead = True
        self._was_explicitly_closed = True

    monkeypatch.setattr(fixpp.Engine, "close", fake_close)
    monkeypatch.setattr(fixpp.Session, "close", fake_close)

    with warnings.catch_warnings(record=True) as caught:
        warnings.simplefilter("always")

        engine = object.__new__(fixpp.Engine)
        engine._handle = object()
        engine._dead = False
        engine._was_explicitly_closed = False
        engine._sessions = weakref.WeakSet()
        del engine

        session = object.__new__(fixpp.Session)
        session._handle = object()
        session._dead = False
        session._was_explicitly_closed = False
        session._engine = None
        session._messages = weakref.WeakSet()
        session._application = None
        session._application_registered = False
        session._in_callback = False
        del session

        gc.collect()

    messages = [str(w.message) for w in caught if w.category is DeprecationWarning]
    assert any("fixpp.Engine" in message for message in messages)
    assert any("fixpp.Session" in message for message in messages)
    assert calls == ["Engine", "Session"]
