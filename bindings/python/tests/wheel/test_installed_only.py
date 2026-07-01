"""Installed-only witness for the dedicated wheel suite (T012 / quickstart §4).

Asserts the suite is exercising the INSTALLED package and not a shadowing source
tree: ``fixpp`` must resolve from under ``sys.prefix`` (the install root / active
venv). The out-of-repo harness scrubs ``PYTHONPATH`` before invoking pytest; this
test is the in-suite guard that the scrub held (LOC-5 / D-8).
"""
import os
import sys

import fixpp


def test_fixpp_imported_from_install_prefix():
    real = os.path.realpath(fixpp.__file__)
    prefix = os.path.realpath(sys.prefix)
    assert real.startswith(prefix), (
        f"fixpp imported from {real}, not under sys.prefix {prefix} — a source "
        "tree is shadowing the installed wheel"
    )
