# API Surface Checklist: Typed Exception Hierarchy (PY-003)

**Purpose**: Requirements-quality gate for the `fixpp.*` typed exception surface (hierarchy, root alias, attributes, single-source translator, coverage invariant). Audience: Gate B reviewer.
**Created**: 2026-06-26
**Feature**: [spec.md](../spec.md) · contracts: [python-exception-surface.md](../contracts/python-exception-surface.md) · data-model: [data-model.md](../data-model.md) E-2/E-3

## Requirement Completeness

- [ ] CHK001 Is the full exception hierarchy enumerated as a closed tree — root `FixppError`, one subclass per `fixpp_error_t` block, the `CapiError` `Cancelled`/`Unknown` children, the five `BindingError` children, and `AppError` — with no "etc."? [Completeness, Spec §FR-006, contract T-1, data-model E-2]
- [ ] CHK002 Is `Error = FixppError` specified as an alias (not a separate class) so the shipped 053 `fixpp.Error` / `pytest.raises(fixpp.Error)` surface survives? [Completeness, Spec §FR-006, contract T-1]
- [ ] CHK003 Is the two-tier attribute contract fully specified — (a) translated `fixpp_error_t` instances carry `.code`/`.name`/`.message`; (b) in-typemap conversion failures are `FixppError`-rooted with `.message` only (no `.code`/`.name`) — with both tiers stated as non-contradictory? [Completeness, Spec §FR-007/FR-010, contract T-2]
- [ ] CHK004 Is the single-source translator surface named precisely — `fixpp._map_to_class(code)` + public alias `fixpp.exception_for_code(code)` + `fixpp.strerror(code)` — and is the out-typemap required to route through it (no parallel C mapping)? [Completeness, Spec §FR-008, contract T-4]
- [ ] CHK005 Is the deferred surface (`fixpp.errors` submodule package form; active raising of `ObjectLifetime`/1202 and `CallbackReentrantClose`/1204; engine-side 1200 translation) explicitly named as out of scope → PY-004/PY-005, so the wrap boundary is unambiguous? [Coverage, contract T-6]

## Requirement Clarity

- [ ] CHK006 Is the fallback rule unambiguous — a code in a known populated block → that block's class (e.g. future 405 → `StoreError`); a code in a wholly unmapped/future block → root `FixppError`; and NO `UnknownError` class (would collide with `Unknown`/2)? [Clarity, Spec §FR-009, contract T-3, data-model E-2]
- [ ] CHK007 Is the `.name` totality rule stated — for a code absent from `_CODE_TO_NAME` (synthetic SC-006 / future FR-009 code) the fallback is `f"FIXPP_ERR_{code}"` so `.name` is never `KeyError`/`None`? [Clarity, contract T-2, data-model E-3]
- [ ] CHK008 Is the cross-module routing mechanism clear — the out-typemap (wrapper in `_fixpp`) reaches the translator (in `fixpp.py`) via a cached lazy `PyImport_ImportModule("fixpp")` → `_raise_for_code`, so runtime and tests share the same `_map_to_class`/`_CODE_TO_NAME`? [Clarity, contract T-4, data-model E-3]

## Requirement Consistency

- [ ] CHK009 Do the block→class ranges in data-model E-2/E-3, the spec FR-006 enumeration, and contract T-1 agree exactly (same blocks, same boundaries, `AppError` = `[1400,1499]`)? [Consistency, Spec §FR-006, data-model E-2/E-3, contract T-1]
- [ ] CHK010 Is the `AppError` block (`[1400,1499]`) consistent with the 051 D-6 / `[2i §4.3]` error-block mint (codes 1400–1405) it maps, and distinct from `SessionError` (`[300,399]`) so the block cannot reuse an existing name? [Consistency, data-model E-2, research D-5]

## Acceptance Criteria Quality

- [ ] CHK011 Is SC-001 objectively verifiable — catch a specific category by type AND read the exact numeric code + message off the caught exception (not merely "an exception was raised")? [Measurability, Spec §SC-001]
- [ ] CHK012 Is the coverage invariant (SC-002) stated as set-EQUALITY both directions (`set(_CODE_TO_NAME) == set(header codes)` + every code maps non-fallback), count-pinned (`len==47`) so it cannot pass vacuously and fails on a code added OR removed from `error.h`? [Measurability, Spec §SC-002/FR-008, contract T-5]

## Edge Case Coverage

- [ ] CHK013 Is the synthetic out-of-range fallback (SC-006) testable directly via the exposed translator (`exception_for_code(99999) is FixppError`), independent of any exercised C-ABI path? [Edge Case, Spec §SC-006, contract T-3]
- [ ] CHK014 Is the in-typemap conversion-failure carve-out (embedded-NUL / invalid-UTF-8 / non-bytes → root `FixppError`, message-only, no fabricated code) specified so it stays `fixpp.Error`-rooted but is NOT claimed to carry the numeric attributes? [Edge Case, Spec §FR-010, contract T-2/T-3]

## Ambiguities & Assumptions

- [ ] CHK015 Is the assumption that the `FIXPP_ERR_*` constants are NOT SWIG-exposed (forcing the hand-written `_CODE_TO_NAME` + header-parsed coverage test) verified against the real module, not assumed? [Ambiguity, research D-3, contract T-5]
