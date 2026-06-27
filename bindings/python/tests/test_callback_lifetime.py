import gc
import weakref

import fixpp

from oo_test_support import close_pair, establish_pair


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
