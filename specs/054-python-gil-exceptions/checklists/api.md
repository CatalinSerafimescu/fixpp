# API Surface Checklist: Typed Exception Hierarchy (PY-003)

**Purpose**: Requirements-quality gate for the `fixpp.*` typed exception surface (hierarchy, root alias, attributes, single-source translator, coverage invariant). Audience: Gate B reviewer.
**Created**: 2026-06-26
**Feature**: [spec.md](../spec.md) · contracts: [python-exception-surface.md](../contracts/python-exception-surface.md) · data-model: [data-model.md](../data-model.md) E-2/E-3

## Requirement Completeness

- [x] CHK001 Is the full exception hierarchy enumerated as a closed tree — root `FixppError`, one subclass per `fixpp_error_t` block, the `CapiError` `Cancelled`/`Unknown` children, the five `BindingError` children, and `AppError` — with no "etc."? [Completeness, Spec §FR-006, contract T-1, data-model E-2] — PASS: data-model E-2 enumerates the complete closed tree (FixppError root, 12 pre-054 block classes, AppError for [1400,1499]); T-1 contract confirms; spec §FR-006 (line 91) names the same set with all BindingError children listed explicitly.
- [x] CHK002 Is `Error = FixppError` specified as an alias (not a separate class) so the shipped 053 `fixpp.Error` / `pytest.raises(fixpp.Error)` surface survives? [Completeness, Spec §FR-006, contract T-1] — PASS: spec §FR-006 states "`Error = FixppError` (the shipped 053 `fixpp.Error` becomes the alias, so `pytest.raises(fixpp.Error)` survives)"; E-2 line 41 shows `Error = FixppError`; T-1 specifies alias-not-separate-class.
- [x] CHK003 Is the two-tier attribute contract fully specified — (a) translated `fixpp_error_t` instances carry `.code`/`.name`/`.message`; (b) in-typemap conversion failures are `FixppError`-rooted with `.message` only (no `.code`/`.name`) — with both tiers stated as non-contradictory? [Completeness, Spec §FR-007/FR-010, contract T-2] — PASS: spec §FR-007 (translated attrs) + §FR-010 (in-typemap carve-out `.message` only) both stated; E-3 routing note + T-2 define both tiers as non-contradictory.
- [x] CHK004 Is the single-source translator surface named precisely — `fixpp._map_to_class(code)` + public alias `fixpp.exception_for_code(code)` + `fixpp.strerror(code)` — and is the out-typemap required to route through it (no parallel C mapping)? [Completeness, Spec §FR-008, contract T-4] — PASS: spec §FR-008 + E-3 name all three symbols; out-typemap routing via `fixpp_py_raise_for_code` → `_raise_for_code` specified in E-3; T-4 requires single-source routing with no parallel C mapping.
- [x] CHK005 Is the deferred surface (`fixpp.errors` submodule package form; active raising of `ObjectLifetime`/1202 and `CallbackReentrantClose`/1204; engine-side 1200 translation) explicitly named as out of scope → PY-004/PY-005, so the wrap boundary is unambiguous? [Coverage, contract T-6] — PASS: contract T-6 enumerates all three deferred surfaces with PY-004/PY-005 tags; E-3 footnote explicitly names the `fixpp.errors` package restructure as deferred (D-3/Gate-A-flagged); spec §FR-011 divergence note calls out 1200 translation deferred to PY-004.

## Requirement Clarity

- [x] CHK006 Is the fallback rule unambiguous — a code in a known populated block → that block's class (e.g. future 405 → `StoreError`); a code in a wholly unmapped/future block → root `FixppError`; and NO `UnknownError` class (would collide with `Unknown`/2)? [Clarity, Spec §FR-009, contract T-3, data-model E-2] — PASS: spec §FR-009 (line 94) + E-3 line 108 + T-3 all state: unmapped block → root FixppError; E-2 footnote at line 44 explicitly labels Unknown(2) as "NOT the unmapped-block fallback"; §FR-009 explains the collision risk.
- [x] CHK007 Is the `.name` totality rule stated — for a code absent from `_CODE_TO_NAME` (synthetic SC-006 / future FR-009 code) the fallback is `f"FIXPP_ERR_{code}"` so `.name` is never `KeyError`/`None`? [Clarity, contract T-2, data-model E-3] — PASS: E-3 line 109 defines `.name = _CODE_TO_NAME.get(code, f"FIXPP_ERR_{code}")` fallback; `_make_error` is stated as "total over its input domain"; T-2 confirms name is never None/KeyError.
- [x] CHK008 Is the cross-module routing mechanism clear — the out-typemap (wrapper in `_fixpp`) reaches the translator (in `fixpp.py`) via a cached lazy `PyImport_ImportModule("fixpp")` → `_raise_for_code`, so runtime and tests share the same `_map_to_class`/`_CODE_TO_NAME`? [Clarity, contract T-4, data-model E-3] — PASS: E-3 routing paragraph + T-4 both specify the lazy `PyImport_ImportModule("fixpp")` hop from `%typemap(out)` to `_raise_for_code`; "runtime path and the tests share `_map_to_class` (FR-008)" is explicit in E-3.

## Requirement Consistency

- [x] CHK009 Do the block→class ranges in data-model E-2/E-3, the spec FR-006 enumeration, and contract T-1 agree exactly (same blocks, same boundaries, `AppError` = `[1400,1499]`)? [Consistency, Spec §FR-006, data-model E-2/E-3, contract T-1] — PASS: E-2 hierarchy tree, E-3 `_map_to_class` description, spec §FR-006 enumeration (line 91), and T-1 all list the same 13 block classes (12 pre-054 + AppError) with identical boundaries; AppError=[1400,1499] consistent across all four artifacts.
- [x] CHK010 Is the `AppError` block (`[1400,1499]`) consistent with the 051 D-6 / `[2i §4.3]` error-block mint (codes 1400–1405) it maps, and distinct from `SessionError` (`[300,399]`) so the block cannot reuse an existing name? [Consistency, data-model E-2, research D-5] — PASS: error.h verified: codes 1400–1405 (6 codes; comment "1400-1404 map five reachable C++ ordinals; 1405 is pure C-ABI construction reject"); [2i §4.3] confirmed at 2i-capi.md line 474+/590+; `AppError` block name is distinct from `SessionError([300,399])`.

## Acceptance Criteria Quality

- [x] CHK011 Is SC-001 objectively verifiable — catch a specific category by type AND read the exact numeric code + message off the caught exception (not merely "an exception was raised")? [Measurability, Spec §SC-001] — PASS: spec §SC-001 (line 119) requires: catch by type, assert block-matching subclass, root catchability, AND recover the exact numeric code + message; not merely "an exception was raised."
- [x] CHK012 Is the coverage invariant (SC-002) stated as set-EQUALITY both directions (`set(_CODE_TO_NAME) == set(header codes)` + every code maps non-fallback), count-pinned (`len==47`) so it cannot pass vacuously and fails on a code added OR removed from `error.h`? [Measurability, Spec §SC-002/FR-008, contract T-5] — PASS: E-3 validation rule (line 164) + T-5 define the invariant as bidirectional set equality with `len==47` pin; spec §SC-002 (line 120) adds "a code in an unmapped block (the [1400,1499] block until AppError is added) fails it" — demonstrating the RED proof is live.

## Edge Case Coverage

- [x] CHK013 Is the synthetic out-of-range fallback (SC-006) testable directly via the exposed translator (`exception_for_code(99999) is FixppError`), independent of any exercised C-ABI path? [Edge Case, Spec §SC-006, contract T-3] — PASS: spec §SC-006 (line 124) + T-3 + E-3 line 111 (`exception_for_code` = public alias of `_map_to_class`) all confirm direct translator call path; no C-ABI exercise needed; §FR-009 notes "the fallback path is testable directly with a synthetic out-of-range code."
- [x] CHK014 Is the in-typemap conversion-failure carve-out (embedded-NUL / invalid-UTF-8 / non-bytes → root `FixppError`, message-only, no fabricated code) specified so it stays `fixpp.Error`-rooted but is NOT claimed to carry the numeric attributes? [Edge Case, Spec §FR-010, contract T-2/T-3] — PASS: spec §FR-010 (line 95) + E-3 routing note + T-2 carve-out all state: in-typemap failures → root FixppError, `.message` only, no `.code`/`.name`, no fabricated code; "stays fixpp.Error-rooted" explicit.

## Ambiguities & Assumptions

- [x] CHK015 Is the assumption that the `FIXPP_ERR_*` constants are NOT SWIG-exposed (forcing the hand-written `_CODE_TO_NAME` + header-parsed coverage test) verified against the real module, not assumed? [Ambiguity, research D-3, contract T-5] — PASS: research D-3 explicitly states verified against real `build/linux-clang-debug-py/lib/_fixpp.so` (2026-06-26); T-5 cites D-3 as the verification source; E-3 line 107 notes "SWIG drops cast-to-typedef `#define`s."

## Audit Result

| Disposition | Count |
|---|---|
| PASS | 15 |
| SPEC-FIXED | 0 |
| DD-DECIDED | 0 |
| WAIVED | 0 |
| **Total** | 15 |

### SPEC-FIXED items
*(none)*

### DD-DECIDED items
*(none)*

### WAIVED items
*(none)*

Anchors spot-verified: `[2m §4.6]` (line 815 mapping table + lines 792-812 CallbackReentrantClose docstring), `[2i §4.3]` (2i-capi.md line 474+/590+), `error.h` FIXPP_ERR_* count=48 total/47 non-OK, `fixpp.i:418-419` (%include engine.h/session.h), `fixpp.i:105-109` (%init fixpp.Error), research D-3 (verified against real _fixpp.so 2026-06-26) — all resolve in signed-off revision `.specify/2m-pybind.md` v0.3 (Gate A round 2 converged).
