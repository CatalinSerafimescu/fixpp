# `tests/wheel/` — functional install-verification subset (T012 / D-8 / E-6)

The dedicated, **locator-using** test suite run against the *installed* abi3 wheel
on each of CPython 3.10/3.11/3.12/3.13 (T013 / CI-4). It imports **only installed
modules** and resolves every dictionary through `fixpp.dictionary_path(...)` via the
shared `_wheeldict.resolve(...)` helper — never a repo-relative `dictionaries/`
path — so it is valid with the repo absent (LOC-5).

Run it (out-of-repo harness scrubs `PYTHONPATH` so no source tree shadows the
installed package — `test_installed_only.py` is the in-suite guard):

```bash
python -m venv /tmp/wheel-venv && /tmp/wheel-venv/bin/pip install <the.whl> pytest
( cd bindings/python/tests/wheel && env -u PYTHONPATH /tmp/wheel-venv/bin/python -m pytest -q )
```

## Membership (the enumerated set, T012)

> ⚠️ **THIS SECTION IS THE ALLOWLIST, AND IT IS ENFORCED (#298).**
> `ci/assert-wheel-test-parity.py`, pinned by `ci/test-wheel-parity.sh` in the
> `ci-script-pins` job, reads this file and fails the build on: a wheel file
> named nowhere here; a row naming a file that does not exist; a twin whose
> `def test_*` name set differs from its in-tree source; and a row marked
> `as-is` whose two copies are not byte-identical. **Edit the table in the same
> commit as the file** — a divergence introduced without its row is a build
> failure, by design. Rows marked with a locator swap, `(suite-native)` or
> `diverges` are exempt from byte-identity only; the other three still apply.

Ports of the in-tree `bindings/python/tests/` suite — the **round-trip, smoke,
exception, lifetime, OO-behaviour, and sub-interpreter** tests — adapted to the
locator. For a `swap `_dict_path`` row, the intended BEHAVIOURAL divergence from
its in-tree source is its dict helper (`_dict_path` /
`oo_test_support.dict_path` / `_gil_staging._dict_path`) delegating to
`_wheeldict.resolve("FIX44")`; those files also carry a short port note.
`test_subinterpreter.py` is the one behavioural exception — see its table row.

> ⚠️ **WHAT THE GATE ACTUALLY ENFORCES FOR THESE ROWS IS TEST-NAME PARITY, NOT
> BYTE IDENTITY**, and this paragraph says so rather than asserting the stronger
> property. It previously read *"everything else is a faithful copy"* — an
> unenforced claim stated as fact, in the very file the gate treats as the
> contract. A review demonstrated the gap by replacing all eight test BODIES in
> `test_roundtrip.py` with `pass`: names unchanged, gate green. Byte identity is
> enforced only for `as-is` rows, which is why moving a file to `as-is` is the
> way to strengthen it, and why the claim here is now scoped to what is checked.

| File | Source | Locator swap |
|---|---|---|
| `test_locator.py` | (suite-native) | n/a — LOC-0..6 / LAY-4 witnesses |
| `test_installed_only.py` | (suite-native) | n/a — install-prefix guard |
| `test_import_surface.py` | (suite-native) | n/a — NBC-3 exact public-surface witness |
| `test_smoke.py` | as-is | — |
| `test_roundtrip.py` | swap `_dict_path` | ✓ |
| `test_exceptions.py` | as-is | — (uses only non-existent paths) |
| `test_lifetime.py` | swap `_dict_path` | ✓ |
| `test_callback_lifetime.py` | as-is | via `oo_test_support` |
| `test_close_flow.py` | as-is | — |
| `test_context_manager.py` | as-is | — |
| `test_pickle_ban.py` | as-is | — |
| `test_reentrancy.py` | as-is | via `_oo_reentrancy_staging` / `oo_test_support` |
| `test_callback_raise_watchdog.py` | as-is | via `_gil_staging` |
| `test_subinterpreter.py` | **diverges** | n/a — locator-independent, but NOT as-is (see below) |
| `oo_test_support.py` | swap `_dict_path` | ✓ — `dict_path()` delegates to `_wheeldict.resolve` |
| `_gil_staging.py` | swap `_dict_path` | ✓ — `_gil_staging._dict_path` |
| `_oo_reentrancy_staging.py` | as-is | — |

⚠️ **`test_subinterpreter.py` is not a faithful port below Python 3.12.** The
in-tree source uses `importorskip("_xxsubinterpreters")` on every Python
version, so an absent module skips the whole test, and it tolerates a
`RunFailedError` only when the message contains the CPython import-barrier text
(`bindings/python/tests/test_subinterpreter.py`). The wheel twin instead makes
`_xxsubinterpreters` **mandatory** below 3.12 (treating an absent module as a
broken runner, not a skip) and, below 3.12, **rejects every**
`RunFailedError`. On 3.12+ the two files behave identically: both
`importorskip`, and both tolerate only the import-barrier message. This is a
known discrepancy, not an intended design; do not treat the wheel twin's
stricter 3.10/3.11 behaviour as the documented contract.

> ⛔ **THIS ROW IS THE ONE THING THE PARITY GATE DOES NOT ENFORCE, AND THAT IS
> STATED HERE SO THE EXEMPTION IS NOT MISTAKEN FOR A SANCTION.**
> `ci/assert-wheel-test-parity.py` exempts `diverges` rows from byte-identity
> (they still carry enumeration, no-dangling and test-name parity). So the
> behavioural divergence above survives a green gate. **`diverges` is not a
> general escape hatch — this is its only current member, and a second one
> should be argued for rather than added.** Reconciling the 3.10/3.11 behaviour
> is real Python-version semantics work and was deliberately NOT folded into
> the #298 parity pass; it remains open on its own merits. Adding a `diverges`
> row to dodge a gate failure would be the exact substitution this gate exists
> to prevent: an unenforced convention replaced by a falsely enforced one.

Support modules: `_wheeldict.py` (locator resolver), `oo_test_support.py`,
`_gil_staging.py`, `_oo_reentrancy_staging.py`. The two staging modules run as
subprocesses and re-inject their own `PYTHONPATH` (their dir + the installed `fixpp`
dir), so they survive the parent harness's `PYTHONPATH` scrub.

## Deliberate exclusions

- **`test_gil_release_canary.py`** — spec exclusion (FR-007): it requires the
  deliberate-hang `FIXPP_PY_GIL_RELEASE_CANARY` build, which the shipped wheel is
  not. Stays in the in-tree sanitizer matrix. (The *watchdog* `_gil_staging` flow
  is ported — it runs against a normal build, FR-011.)
- **`test_error_coverage.py`** — not install-verifiable: it reads the repo source
  header `include/fix/c_api/error.h` to assert the typed-exception code set matches
  the C ABI. The header is absent from a wheel, so this source-consistency check
  stays in-tree. (The additive import-surface guard is the separate NBC-3 snapshot,
  T018.)
