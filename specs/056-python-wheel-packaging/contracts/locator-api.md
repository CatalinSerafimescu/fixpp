# Contract: bundled-dictionary locator (FR-004a)

Pure-Python. No native code, no C-ABI change. Implemented in `fixpp_dict_data.py`
(an implementation module, mirroring `fixpp_oo.py`) and **re-exported through
`fixpp`** via the existing `%pythoncode` glue, so the **public** names are
`fixpp.dictionary_path` / `fixpp.dictionary_bytes` / `fixpp.BUNDLED_DICTIONARIES`
— not a new top-level import. Resolves the four bundled FIX dictionaries from the
`_fixpp_data` package.

## Public surface (via `import fixpp`)

```python
# fixpp_dict_data.py — re-exported into fixpp by the %pythoncode glue
from importlib.resources import files, as_file
from contextlib import contextmanager

BUNDLED_DICTIONARIES: frozenset[str]   # {"FIX42", "FIX44", "FIX50SP2", "FIXT11"}

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
| LOC-0 | The three names are reachable as `fixpp.dictionary_path` / `fixpp.dictionary_bytes` / `fixpp.BUNDLED_DICTIONARIES` from `import fixpp` (re-export glue), in addition to the `fixpp_dict_data` implementation module. |
| LOC-1 | `fixpp.BUNDLED_DICTIONARIES == {"FIX42","FIX44","FIX50SP2","FIXT11"}` exactly (set-equality, not subset). |
| LOC-2 | `fixpp.dictionary_path(n)` for `n ∈ BUNDLED_DICTIONARIES` yields a path `p` such that `os.path.isfile(p)` is true and `fixpp.dict_load_from_xml(p)` returns a valid handle. |
| LOC-3 | `fixpp.dictionary_bytes(n)` for `n ∈ BUNDLED_DICTIONARIES` returns non-empty bytes beginning with an XML prolog. |
| LOC-4 | `n ∉ BUNDLED_DICTIONARIES` → raises **`KeyError`** (single decided type — Gate A) whose message lists the **sorted** valid set. No silent default, no empty return. (data-model E-4 and the LOC-4 witness agree on `KeyError`.) |
| LOC-5 | Resolution works identically from an installed wheel and from the build tree (uses `importlib.resources.files("_fixpp_data")`, never a repo-relative or build-host path). |
| LOC-6 | The context manager cleans up any temporary materialisation on exit (zipped-wheel safe via `as_file`). |

## Witnesses

- LOC-0: `import fixpp; fixpp.dictionary_path` / `fixpp.dictionary_bytes` /
  `fixpp.BUNDLED_DICTIONARIES` all resolve (re-export reached).
- LOC-1: a set-equality assertion against the literal expected set (guards against
  a dropped/added XML).
- LOC-2: round-trip — `with fixpp.dictionary_path("FIX44") as p:
  fixpp.dict_load_from_xml(p)` then a FIX 4.4 send/recv (SC-002).
- LOC-4: `pytest.raises(KeyError)` on `fixpp.dictionary_path("FIX99")` asserting
  the sorted valid set is named in the message.
- LOC-5: the test runs against the **installed** wheel in CI (SC-003) via the
  dedicated `tests/wheel/` suite (importing only installed modules), i.e. with no
  repo present.
