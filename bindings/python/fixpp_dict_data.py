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
from pathlib import Path
from typing import Iterator

# LOC-1: the exact bundled set (set-equality, not subset).
BUNDLED_DICTIONARIES: frozenset[str] = frozenset(
    {"FIX42", "FIX44", "FIX50SP2", "FIXT11"}
)


def _resource(name: str):
    """Return the traversable for <name>.xml, or raise KeyError (LOC-4: single
    decided type) naming the sorted valid set.

    Two install layouts resolve here, and the WHEEL one is unchanged:

    * **wheel** — the four XMLs sit inside the ``_fixpp_data`` package, so the
      first branch returns and nothing below runs.
    * **C++ package** (#257) — the ``-release`` packages already ship
      ``dictionaries/`` once under ``share/fixpp/dictionaries``, so the payload
      does NOT carry a second 1.9 MB copy. That install instead generates an
      ``_fixpp_data/__init__.py`` carrying ``DICTIONARY_DIR``, computed relative
      to itself so the package stays relocatable (LOC-5: never a build-host or
      repo-relative path).

    ⚠️ The fallback is deliberately NOT reached by the wheel, which is exactly
    why it needs its own witness — see ``FIXPP_PY_WITNESS_MODE=package`` in
    ``bindings/python/run_python_install_witness.cmake``, which resolves a real
    dictionary through this function against a staged package tree. A branch the
    shipped artifact never executes is untested by construction otherwise.
    """
    if name not in BUNDLED_DICTIONARIES:
        raise KeyError(
            f"unknown bundled dictionary {name!r}; "
            f"valid names are {sorted(BUNDLED_DICTIONARIES)}"
        )
    resource = files("_fixpp_data").joinpath(f"{name}.xml")
    if resource.is_file():
        return resource

    import _fixpp_data  # already imported by files() above; cheap

    directory = getattr(_fixpp_data, "DICTIONARY_DIR", None)
    if directory is not None:
        alternate = Path(directory) / f"{name}.xml"
        if alternate.is_file():
            return alternate

    # Neither layout has it. Return the bundled traversable regardless so the
    # caller's read_bytes()/as_file() raises the real FileNotFoundError naming
    # the expected path — the broken-wheel gate (SC-006 / FR-009) asserts that
    # a stripped wheel still fails here, and a KeyError would mis-describe it.
    return resource


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
