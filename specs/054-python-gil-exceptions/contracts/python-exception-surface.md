# Contract — `fixpp.*` typed exception surface (PY-003)

Realizes `[2m §4.6]` verbatim (+ `AppError`). All names are module-level on `fixpp`.

## T-1 — Hierarchy & root alias

- Root: `fixpp.FixppError(Exception)`. **`fixpp.Error is fixpp.FixppError`** (alias — 053's `pytest.raises(fixpp.Error)` and the strerror-message assertion stay valid).
- One subclass per `fixpp_error_t` block (E-2 tree): `CapiError` (+ `Cancelled`/1, `Unknown`/2), `ParseError`, `ValidatorError`, `SessionError`, `StoreError`, `SyncError`, `TlsError`, `TransportError`, `DecimalError`, `ControlPlaneError`, `LogError`, `TapError`, `BindingError` (+ `PythonCallbackRaised`/1200, `SubInterpreterRejected`/1201, `ObjectLifetime`/1202, `WheelAbiMismatch`/1203, `CallbackReentrantClose`/1204), and **`AppError`** (`[1400,1499]`, new in 054).
- Every block subclass is `issubclass(_, FixppError)`; `Cancelled`/`Unknown` are `issubclass(_, CapiError)`; the five binding subclasses are `issubclass(_, BindingError)`.

## T-2 — Attributes

The attribute contract is **total but two-tier** (the two tiers do not contradict):

**(a) Typed `fixpp_error_t` instances** (every instance raised from a translated C-ABI status, via `_make_error` / the out-typemap) carry all three:

| attr | type | value |
|---|---|---|
| `.code` | `int` | the numeric `fixpp_error_t` |
| `.name` | `str` | symbolic, e.g. `"FIXPP_ERR_DICT_CONFIG"`; for a code absent from `_CODE_TO_NAME` (SC-006 synthetic / FR-009 future code) the fallback is `f"FIXPP_ERR_{code}"` so `.name` is **always present** (never `KeyError`/`None`) |
| `.message` | `str` | `fixpp_strerror(code)`; also `str(exc)` |

**(b) Non-`fixpp_error_t` in-typemap conversion failures** (non-str / embedded-NUL / invalid-UTF-8 / non-bytes — T-3) are **explicitly carved out of the `.code`/`.name` half**: they are raised as the root `fixpp.Error` (= `FixppError`, FR-010) with **`.message` only** and **no `.code`/`.name`** (the as-built `FIXPP_PY_RAISE` = message-only; no header code fits an argument-type error, so D-9's no-fabricated-code stance holds). They stay `fixpp.Error`-rooted (FR-010) but are NOT claimed to carry the numeric attributes.

## T-3 — Raising contract

- Every non-`OK` `fixpp_error_t` returned from a wrapped call raises the **block-matching** subclass (FR-006), via the single translator `_map_to_class` the out-typemap routes through (FR-008).
- A code in a known populated block with an unrecognized value → that block's class (e.g. future `405` → `StoreError`).
- A code in an unmapped/future block → root `FixppError` (fallback; FR-009). **No `UnknownError`.**
- In-typemap conversion failures (non-str / embedded-NUL / invalid-UTF-8 / non-bytes) → root `FixppError` (FR-010; not a built-in `TypeError`/`ValueError`, no fabricated code).
- `OK` (0) is never translated/raised.

## T-4 — Translator (single source of truth)

- `fixpp._map_to_class(code:int) -> type[FixppError]` and its public alias `fixpp.exception_for_code(code)`.
- `fixpp.strerror(code:int) -> str` (already exposed; `.message` source).
- The out-typemap routes through the Python `_raise_for_code` via a cached lazy `PyImport_ImportModule("fixpp")` (the wrapper is in `_fixpp`, the translator in `fixpp.py`) — the runtime path and the tests use the **same** `_map_to_class` / `_CODE_TO_NAME`. No parallel C mapping.

## T-5 — Coverage invariant (FR-008/SC-002) — header-sourced, non-vacuous

The `ERR_*` constants are **not** exposed as Python attributes (SWIG drops cast-to-typedef `#define`s — verified). The coverage test therefore parses `error.h` (the independent source) for the `FIXPP_ERR_*` codes and asserts:
1. `len(codes) == 47` (non-OK) — so it cannot pass vacuously.
2. every code: `_map_to_class(code)` is a `FixppError` subclass **and not the bare `FixppError` fallback**.
3. `set(_CODE_TO_NAME) == set(codes)` — the maintained name dict matches the header.

The `[1400,1499]` block is RED until `AppError` is added (the live drift proof). Non-circular (header is independent of the binding's map/dict), non-vacuous (count-pinned).

## T-6 — Out of scope (deferred)

- `fixpp.errors` submodule (`[2m §4.6]` `fixpp.errors._map_to_class`) — package restructure → PY-005-era (D-3).
- Active raising of `ObjectLifetime`/1202 (post-window view invalidation) and `CallbackReentrantClose`/1204 (reentrant-close detection) from binding logic — needs the SWIG director + markers → **PY-004**. The classes exist and the codes map here; only the binding-side runtime detection is deferred.
- `PythonCallbackRaised`/1200 engine-side translation — needs the director → PY-004 (D-8). Class exists, 1200 maps.
