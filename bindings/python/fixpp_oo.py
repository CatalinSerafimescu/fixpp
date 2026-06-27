"""Object-oriented lifetime/ownership layer over the flat SWIG substrate (PY-004 / 055).

Scope per ``[2m §6.2]`` / research D-1: a pure-Python wrapper layer
(``Engine`` / ``Session`` / ``Message`` / ``Application`` / ``Dictionary``)
carrying a liveness sentinel so a Python object can never outlive its native
handle — post-close / post-dispatch-window access raises
``fixpp.ObjectLifetime`` (1202) instead of a use-after-free. Closes L-053-1.

This module is ADDITIVE over the FROZEN C-ABI: it composes the flat substrate
functions (``engine_create`` / ``session_open`` / ``session_send`` / ...) and
the typed exception hierarchy that 053/054 already shipped in the generated
``fixpp`` module. No ``include/fix/c_api.h`` change. The ``[2m §4.5]`` 6-method
director and value-typed config/decimal classes are OUT of scope (D-1).
"""

# Flat substrate (prefix-stripped %rename names) + typed exceptions, from the
# generated `fixpp` module. fixpp.py imports THIS module last (re-export glue),
# so at import time every name below is already bound on the partially-init'd
# `fixpp` (the SWIG function wrappers and the %pythoncode exception block both
# precede the re-export glue).
import warnings
import weakref

from fixpp import (  # noqa: F401  (re-exported / used by later phases)
    engine_create,
    engine_start,
    engine_destroy,
    session_open,
    session_close,
    session_is_established,
    session_acceptor_bound_endpoint,
    session_send,
    msg_set_string,
    msg_commit,
    msg_destroy,
    msg_get_string,
    msg_create_outbound,
    dict_load_from_xml,
    dict_destroy,
    _register_application_oo,
    _release_application_oo,
    _is_main_interpreter,
    ObjectLifetime,
    CallbackReentrantClose,
    SubInterpreterRejected,
    CapiError,
    FixppError,
    _make_error,
)

# fixpp_error_t code for "Python object outlived its native handle" — already
# minted by PY-003; no new C-ABI code (FR-015, freeze held).
_OBJECT_LIFETIME = 1202
_SUBINTERPRETER_REJECTED = 1201
_CALLBACK_REENTRANT_CLOSE = 1204
_INVALID_HANDLE = 4


class _PickleBan:
    """Refuse to pickle a handle-bearing wrapper (T024 / FR-013; contracts C-5).

    A pickled native handle would deserialize in another process as a
    meaningless pointer that use-after-frees on first touch, so block it loudly
    at serialization time with a ``TypeError``. Scoped to the handle-bearing
    wrappers (and the callback base) only — no value-typed classes are
    introduced (FR-014 leg deferred).
    """

    def __reduce_ex__(self, protocol):
        raise TypeError(
            "fixpp.%s objects are not pickleable; native handles cannot cross "
            "process boundaries" % type(self).__name__)

    def __reduce__(self):
        raise TypeError(
            "fixpp.%s objects are not pickleable; native handles cannot cross "
            "process boundaries" % type(self).__name__)


class _LiveHandle(_PickleBan):
    """Shared liveness-sentinel base for every handle-bearing wrapper (T003).

    Carries the per-wrapper ``(_handle, _dead)`` state and the ``_ensure_live()``
    guard every C-ABI-calling method invokes FIRST. When ``_dead`` is True the
    guard raises ``fixpp.ObjectLifetime`` (numeric 1202) and returns WITHOUT
    touching the C-ABI — eliminating the use-after-free reachable from Python
    (FR-002/FR-003; data-model E-6; contracts C-2/C-8).

    The state is plain instance attributes mutated under the GIL — no
    ``threading.local``, no OS-thread-id assumptions (``[2m §1.3]`` rule (4) /
    FR-016): correct regardless of which worker thread runs a callback.
    """

    # Class-level defaults so the guard is total even before __init__ assigns
    # the real native handle (a half-constructed wrapper reads as not-yet-live).
    _handle = None
    _dead = False

    def _ensure_live(self):
        """Raise ``fixpp.ObjectLifetime`` (1202) if dead; else fall through.

        The raised instance is built via the same translator the C-ABI
        out-typemap uses, so an OO-raised 1202 carries the identical
        ``.code``/``.name``/``.message`` as a native-raised one.
        """
        if self._dead:
            raise _make_error(_OBJECT_LIFETIME)


class Engine(_LiveHandle):
    """OO wrapper over a native engine handle."""

    def __init__(self, config):
        self._handle = None
        self._dead = False
        self._was_explicitly_closed = False
        self._sessions = weakref.WeakSet()
        if not _is_main_interpreter():
            raise _make_error(_SUBINTERPRETER_REJECTED)
        self._handle = engine_create(config)

    def open_session(self, config):
        self._ensure_live()
        session = Session(self, session_open(self._handle, config))
        self._sessions.add(session)
        return session

    def start(self):
        self._ensure_live()
        engine_start(self._handle)

    def close(self):
        if self._dead:
            return
        for session in list(self._sessions):
            if session._in_callback:
                raise _make_error(_CALLBACK_REENTRANT_CLOSE)
        self._dead = True
        for session in list(self._sessions):
            session._close_from_engine()
        engine_destroy(self._handle)
        self._was_explicitly_closed = True

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()
        return None

    def __del__(self):
        if self._was_explicitly_closed or self._dead:
            return
        warnings.warn(
            "fixpp.Engine finalized without explicit close(); use close() or a with block",
            DeprecationWarning,
            stacklevel=2,
        )
        try:
            self.close()
        except Exception:
            pass


class Session(_LiveHandle):
    """OO wrapper over a native session handle."""

    def __init__(self, engine, handle):
        self._handle = handle
        self._dead = False
        self._was_explicitly_closed = False
        self._engine = engine
        self._messages = weakref.WeakSet()
        self._application = None
        self._application_registered = False
        self._in_callback = False

    def is_established(self):
        self._ensure_live()
        return session_is_established(self._handle)

    def acceptor_bound_endpoint(self):
        self._ensure_live()
        return session_acceptor_bound_endpoint(self._handle)

    def create_message(self, msg_type):
        self._ensure_live()
        return Message(msg_create_outbound(self._handle, msg_type), self, False)

    def send(self, msg):
        if self._in_callback:
            raise _make_error(_CALLBACK_REENTRANT_CLOSE)
        self._ensure_live()
        if not isinstance(msg, Message):
            raise TypeError("fixpp.Session.send expects a fixpp.Message")
        msg._ensure_sendable()
        session_send(self._handle, msg_commit(msg._handle))

    def register_application(self, app):
        self._ensure_live()
        previous = self._application
        self._application = app
        if self._application_registered:
            return
        try:
            _register_application_oo(self._handle, self)
            self._application_registered = True
        except Exception:
            self._application = previous
            raise

    def close(self):
        self._close_impl(allow_reentrant=False)

    def _close_from_engine(self):
        self._close_impl(allow_reentrant=True)

    def _close_impl(self, *, allow_reentrant):
        if self._dead:
            return
        if not allow_reentrant and self._in_callback:
            raise _make_error(_CALLBACK_REENTRANT_CLOSE)
        self._dead = True
        for msg in list(self._messages):
            msg._dead = True
        self._application = None
        session_close(self._handle)
        if self._application_registered:
            _release_application_oo(self)
            self._application_registered = False
        self._was_explicitly_closed = True

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()
        return None

    def __del__(self):
        if self._was_explicitly_closed or self._dead:
            return
        warnings.warn(
            "fixpp.Session finalized without explicit close(); use close() or a with block",
            DeprecationWarning,
            stacklevel=2,
        )
        try:
            self.close()
        except Exception:
            pass


class Message(_LiveHandle):
    """OO wrapper over a native message handle."""

    def __init__(self, handle, session, is_inbound):
        self._handle = handle
        self._dead = False
        self._is_inbound = is_inbound
        self._session = session
        self._session._messages.add(self)

    def get_string(self, tag):
        self._ensure_live()
        return msg_get_string(self._handle, tag)

    def set_string(self, tag, value):
        if self._is_inbound:
            raise _make_error(_INVALID_HANDLE)
        self._ensure_live()
        msg_set_string(self._handle, tag, value)

    def destroy(self):
        if self._dead:
            return
        if not self._is_inbound:
            msg_destroy(self._handle)
        self._dead = True

    def __del__(self):
        try:
            self.destroy()
        except Exception:
            pass

    def _ensure_sendable(self):
        if self._is_inbound:
            raise _make_error(_INVALID_HANDLE)
        self._ensure_live()


class Application(_PickleBan):
    """Inbound-callback base class — v1.0: inbound-only."""

    def fromApp(self, session, msg):
        raise NotImplementedError


class Dictionary(_LiveHandle):
    """OO wrapper over a native dictionary handle."""

    def __init__(self, handle):
        self._handle = handle
        self._dead = False

    @staticmethod
    def load_xml(path):
        return Dictionary(dict_load_from_xml(path))

    def destroy(self):
        if self._dead:
            return
        self._dead = True
        dict_destroy(self._handle)

    def native_handle(self):
        self._ensure_live()
        return self._handle
