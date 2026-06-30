# Contract: `fixpp_dict_data` bundled-dictionary locator (FR-004a)

Pure-Python module shipped at the wheel top level. No native code, no C-ABI
change. Resolves the four bundled FIX dictionaries from the `_fixpp_data` package.

## Public surface

```python
# fixpp_dict_data.py
from importlib.resources import files, as_file
from contextlib import contextmanager

BUNDLED: frozenset[str]   # {"FIX42", "FIX44", "FIX50SP2", "FIXT11"}

@contextmanager
def dictionary_path(name: str) -> "Iterator[str]":
    """Yield a real filesystem path to the bundled <name>.xml.
    For feeding fixpp.dict_load_from_xml(path)."""

def dictionary_bytes(name: str) -> bytes:
    """Return the bytes of the bundled <name>.xml."""
```

## Behavioural contract

| ID | Rule |
|---|---|
| LOC-1 | `BUNDLED == {"FIX42","FIX44","FIX50SP2","FIXT11"}` exactly (set-equality, not subset). |
| LOC-2 | `dictionary_path(n)` for `n ∈ BUNDLED` yields a path `p` such that `os.path.isfile(p)` is true and `fixpp.dict_load_from_xml(p)` returns a valid handle. |
| LOC-3 | `dictionary_bytes(n)` for `n ∈ BUNDLED` returns non-empty bytes beginning with an XML prolog. |
| LOC-4 | `n ∉ BUNDLED` → raises `KeyError` (or `ValueError`) whose message lists the valid set. No silent default, no empty return. |
| LOC-5 | Resolution works identically from an installed wheel and from the build tree (uses `importlib.resources.files("_fixpp_data")`, never a repo-relative or build-host path). |
| LOC-6 | The context manager cleans up any temporary materialisation on exit (zipped-wheel safe via `as_file`). |

## Witnesses

- LOC-1: a set-equality assertion against the literal expected set (guards against
  a dropped/added XML).
- LOC-2: round-trip — `with dictionary_path("FIX44") as p: dict_load_from_xml(p)`
  then a FIX 4.4 send/recv (SC-002).
- LOC-4: `pytest.raises` on `dictionary_path("FIX99")` asserting the valid set is
  named.
- LOC-5: the test runs against the **installed** wheel in CI (SC-003), i.e. with
  no repo present.
