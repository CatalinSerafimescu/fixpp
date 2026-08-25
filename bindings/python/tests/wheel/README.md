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

Ports of the in-tree `bindings/python/tests/` suite — the **round-trip, smoke,
exception, lifetime, OO-behaviour, and sub-interpreter** tests — adapted to the
locator. For every file below **except `test_subinterpreter.py`**, the *only*
divergence from its in-tree source is its dict helper (`_dict_path` /
`oo_test_support.dict_path` / `_gil_staging._dict_path`), now delegating to
`_wheeldict.resolve("FIX44")`; everything else is a faithful copy.
`test_subinterpreter.py` is the one behavioural exception — see its table row
and issue #298.

| File | Source | Locator swap |
|---|---|---|
| `test_locator.py` | (suite-native) | n/a — LOC-0..6 / LAY-4 witnesses |
| `test_installed_only.py` | (suite-native) | n/a — install-prefix guard |
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

⚠️ **`test_subinterpreter.py` is not a faithful port below Python 3.12.** The
in-tree source skips the whole test when `_xxsubinterpreters` is absent and,
on every Python version, tolerates a `RunFailedError` whose message doesn't
match the CPython import-barrier text (`bindings/python/tests/test_subinterpreter.py`).
The wheel twin instead makes `_xxsubinterpreters` **mandatory** below 3.12
(treating an absent module as a broken runner, not a skip) and, below 3.12,
**rejects** that tolerance — any `RunFailedError` fails the test. On 3.12+ the
two files behave identically (both `importorskip` and both check the barrier
text). This is a known, tracked discrepancy pending reconciliation — see
issue #298 — not an intended design; do not treat the wheel twin's stricter
3.10/3.11 behaviour as the documented contract.

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
