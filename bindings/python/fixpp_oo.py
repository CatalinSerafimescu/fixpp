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
)


class Engine:
    """OO wrapper over a native engine handle (filled in Phase 3 / US1)."""


class Session:
    """OO wrapper over a native session handle (filled in Phase 3 / US1)."""


class Message:
    """OO wrapper over a native message handle (filled in Phase 3 / US1)."""


class Application:
    """Inbound-callback base class (filled in Phase 3 / US1)."""


class Dictionary:
    """OO wrapper over a native dictionary handle (filled in Phase 3 / US1)."""
