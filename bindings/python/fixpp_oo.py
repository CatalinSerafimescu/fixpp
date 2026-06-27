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
from fixpp import (  # noqa: F401  (re-exported / used by later phases)
    engine_create,
    session_open,
    session_send,
    msg_create_outbound,
    dict_load_from_xml,
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


class _LiveHandle:
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
    """OO wrapper over a native engine handle (filled in Phase 3 / US1)."""


class Session(_LiveHandle):
    """OO wrapper over a native session handle (filled in Phase 3 / US1)."""


class Message(_LiveHandle):
    """OO wrapper over a native message handle (filled in Phase 3 / US1)."""


class Application:
    """Inbound-callback base class — NOT handle-bearing (filled in Phase 3 / US1)."""


class Dictionary(_LiveHandle):
    """OO wrapper over a native dictionary handle (filled in Phase 3 / US1)."""
