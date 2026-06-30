"""Bundled-dictionary locator witnesses (LOC-0..6) — part of the dedicated
``tests/wheel/`` functional install-verification subset (T012 / D-8).

Imports ONLY installed/built modules and resolves dictionaries through the public
``fixpp.*`` re-export (never a repo-relative ``dictionaries/`` path), so it is valid
against an installed wheel with the repo absent (LOC-5). In-tree it runs against
the build output (PYTHONPATH=<build>/lib, where the locator + _fixpp_data + the
four XMLs are staged).
"""
import os

import pytest

import fixpp


def test_loc0_public_reexport_reached():
    # LOC-0: the three names resolve through `import fixpp` (re-export glue),
    # not only the fixpp_dict_data implementation module.
    assert callable(fixpp.dictionary_path)
    assert callable(fixpp.dictionary_bytes)
    assert isinstance(fixpp.BUNDLED_DICTIONARIES, frozenset)


def test_loc1_bundled_set_exact():
    # LOC-1: set-equality (guards against a dropped/added XML), not subset.
    assert fixpp.BUNDLED_DICTIONARIES == {"FIX42", "FIX44", "FIX50SP2", "FIXT11"}


@pytest.mark.parametrize("name", sorted({"FIX42", "FIX44", "FIX50SP2", "FIXT11"}))
def test_loc2_path_resolves_and_loads(name):
    # LOC-2: yields a real filesystem path; dict_load_from_xml returns a valid handle.
    with fixpp.dictionary_path(name) as p:
        assert os.path.isfile(p), p
        handle = fixpp.dict_load_from_xml(p)
        assert handle is not None


@pytest.mark.parametrize("name", sorted({"FIX42", "FIX44", "FIX50SP2", "FIXT11"}))
def test_loc3_bytes_xml_prolog(name):
    # LOC-3: non-empty bytes beginning with an XML prolog.
    data = fixpp.dictionary_bytes(name)
    assert data, "empty dictionary bytes"
    assert data.lstrip().startswith(b"<?xml"), data[:32]


def test_loc4_unknown_name_keyerror_names_sorted_set():
    # LOC-4: KeyError (single decided type) whose message names the SORTED valid set.
    with pytest.raises(KeyError) as ei:
        with fixpp.dictionary_path("FIX99"):
            pass
    msg = str(ei.value)
    assert "FIX99" in msg
    assert str(sorted(fixpp.BUNDLED_DICTIONARIES)) in msg

    with pytest.raises(KeyError):
        fixpp.dictionary_bytes("NOPE")


def test_lay4_flat_module_imports():
    # LAY-4: the implementation modules import as flat top-level modules.
    import fixpp_dict_data  # noqa: F401
    import fixpp_oo  # noqa: F401
