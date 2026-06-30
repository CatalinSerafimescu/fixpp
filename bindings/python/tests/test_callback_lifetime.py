import gc
import weakref

import pytest

import fixpp

from oo_test_support import (
    close_pair,
    dict_path,
    establish_pair,
    make_engine,
    make_session_config,
)


class _TrackableApp(fixpp.Application):
    def fromApp(self, session, msg):
        pass


def test_callback_ref_drops_after_session_close():
    app = _TrackableApp()
    ref = weakref.ref(app)
    dict_h, acc_engine, ini_engine, acc, _ini = establish_pair(app)
    del app
    try:
        acc.close()
    finally:
        close_pair(ini_engine, acc_engine, dict_h)
    gc.collect()
    assert ref() is None


def test_reregister_releases_prior_callable():
    first = _TrackableApp()
    first_ref = weakref.ref(first)
    dict_h, acc_engine, ini_engine, acc, _ini = establish_pair(first)
    try:
        second = _TrackableApp()
        acc.register_application(second)
        del first
        gc.collect()
        assert first_ref() is None
    finally:
        close_pair(ini_engine, acc_engine, dict_h)


def test_session_wrapper_weakref_dead_after_close():
    """After a successful close() the C-held Py_INCREF on the Session wrapper is
    released — the wrapper must be collectable (P2, gate-b/r1)."""
    app = _TrackableApp()
    dict_h, acc_engine, ini_engine, acc, _ini = establish_pair(app)
    session_ref = weakref.ref(acc)
    try:
        acc.close()
    finally:
        close_pair(ini_engine, acc_engine, dict_h)
    del acc
    gc.collect()
    assert session_ref() is None, "C-held Py_INCREF on Session wrapper was not released"


def test_session_wrapper_weakref_dead_after_raising_close():
    """C-held Py_INCREF on the Session wrapper is released even when session_close raises
    (never-established path → FIXPP_ERR_THREAD_SESSION_LIFECYCLE = 301) — discriminating
    witness for the try/finally fix (P2 + P1-a, gate-b/r1)."""
    app = _TrackableApp()
    dict_h = fixpp.dict_load_from_xml(dict_path())
    ini_engine = make_engine()
    sc = make_session_config(dict_h, fixpp.ROLE_INITIATOR, "WRT", "WRP", 1)
    acc = ini_engine.open_session(sc)
    acc.register_application(app)  # real Py_INCREF on acc
    assert acc._application_registered is True
    session_ref = weakref.ref(acc)
    try:
        with pytest.raises(fixpp.FixppError):
            acc.close()  # raises THREAD_SESSION_LIFECYCLE (code 301) — never started
    finally:
        del acc  # remove our Python ref; C INCREF (if leaked) would keep wrapper alive
        try:
            ini_engine.close()
        except fixpp.FixppError:
            pass
        fixpp.dict_destroy(dict_h)
    gc.collect()
    assert session_ref() is None, (
        "C-held Py_INCREF on Session wrapper was not released after raising close"
    )
