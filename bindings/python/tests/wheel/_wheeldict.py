"""Locator-based bundled-dictionary resolver for the dedicated wheel suite
(T012 / D-8 / E-6).

The dedicated ``tests/wheel/`` suite resolves every bundled dictionary through the
installed ``fixpp.dictionary_path(...)`` locator — never a repo-relative
``dictionaries/`` path — so it is valid against an installed wheel with the repo
absent (LOC-5). Each ported test's dict helper delegates here; this resolver is the
*only* divergence from the in-tree source of those ports.

``fixpp.dictionary_path`` is a context manager (zip-wheel-safe via
``importlib.resources.as_file``). The ported tests need a stable filesystem path for
the process lifetime — they call ``dict_load_from_xml(path)`` repeatedly — so each
name is entered once into a process-wide ``ExitStack`` closed at interpreter exit
(LOC-6: any temporary materialisation is cleaned up).
"""
import atexit
from contextlib import ExitStack

import fixpp

_stack = ExitStack()
atexit.register(_stack.close)
_resolved: dict = {}


def resolve(name="FIX44"):
    """Return a stable filesystem path to the bundled ``<name>.xml`` via the
    installed locator, materialised once and held for the process lifetime."""
    path = _resolved.get(name)
    if path is None:
        path = _stack.enter_context(fixpp.dictionary_path(name))
        _resolved[name] = path
    return path
