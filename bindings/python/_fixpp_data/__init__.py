"""Bundled FIX dictionary data package (PY-005 / LAY-2 / data-model E-3).

Importable marker only. The four FIX dictionary XMLs (FIX42/FIX44/FIX50SP2/FIXT11)
are staged into this package at build time from the submodule ``dictionaries/``
directory (single source of truth — never hand-duplicated into source control);
the wheel ships them alongside this ``__init__.py``. Resolved by the
``fixpp_dict_data`` locator via ``importlib.resources.files("_fixpp_data")``.
"""
