"""Bundled FIX dictionary locator (PY-005 / FR-004a).

Pure-Python, no native code, no C-ABI change. Re-exported through ``import fixpp``
by the ``%pythoncode`` glue in ``fixpp.i``, so the *public* names are
``fixpp.dictionary_path`` / ``fixpp.dictionary_bytes`` / ``fixpp.BUNDLED_DICTIONARIES``
(this is an implementation module, mirroring ``fixpp_oo``). Resolves the four
bundled FIX dictionaries from the ``_fixpp_data`` package via
``importlib.resources``, so resolution works identically from an installed wheel
(zipped or unpacked) and from the build tree — never a repo-relative or build-host
path (LOC-5).
"""
from __future__ import annotations

from contextlib import contextmanager
from importlib.resources import as_file, files
from typing import Iterator

# LOC-1: the exact bundled set (set-equality, not subset).
BUNDLED_DICTIONARIES: frozenset[str] = frozenset(
    {"FIX42", "FIX44", "FIX50SP2", "FIXT11"}
)


def _resource(name: str):
    """Return the importlib.resources traversable for <name>.xml, or raise
    KeyError (LOC-4: single decided type) naming the sorted valid set."""
    if name not in BUNDLED_DICTIONARIES:
        raise KeyError(
            f"unknown bundled dictionary {name!r}; "
            f"valid names are {sorted(BUNDLED_DICTIONARIES)}"
        )
    return files("_fixpp_data").joinpath(f"{name}.xml")


@contextmanager
def dictionary_path(name: str) -> Iterator[str]:
    """Yield a real filesystem path to the bundled ``<name>.xml`` — for feeding
    ``fixpp.dict_load_from_xml(path)`` (LOC-2). Zipped-wheel safe via
    ``importlib.resources.as_file``; any temporary materialisation is cleaned up
    on exit (LOC-6). Raises ``KeyError`` on an unknown name (LOC-4)."""
    resource = _resource(name)
    with as_file(resource) as p:
        yield str(p)


def dictionary_bytes(name: str) -> bytes:
    """Return the bytes of the bundled ``<name>.xml`` (LOC-3). Raises
    ``KeyError`` on an unknown name (LOC-4)."""
    return _resource(name).read_bytes()
