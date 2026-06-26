# Implementation Plan: Python GIL Discipline & Typed Exception Translation (PY-002 + PY-003)

**Branch**: `054-python-gil-exceptions` | **Date**: 2026-06-26 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/054-python-gil-exceptions/spec.md`

## Summary

Harden the merged PY-001 SWIG binding (`bindings/python/`) along two co-located SWIG-layer axes, **without touching the C-ABI** (the `0→1` freeze stays held):

1. **PY-002 — GIL discipline.** Turn PY-001's reactive 3-wrapper GIL release into a *systematic, audited, witnessed* discipline: a documented audit table classifying every wrapped C-ABI function as GIL-releasing (blocking) or GIL-holding; a **discriminating, proven-RED** GIL-release canary (the 053 teardown-vs-worker-callback deadlock shape) — distinct from the GIL-reacquire canary 053 already proved; and a **census amendment** to the normative `[2m]` design (every "send-from-callback is legal" site) recording the as-built-050 blocking-send deadlock as a current limitation.

2. **PY-003 — typed exceptions.** Realize the ratified `[2m §4.6]` exception hierarchy verbatim (root `FixppError`, `Error = FixppError` alias, one subclass per `fixpp_error_t` block + the five `BindingError` subclasses), **extended** with `AppError` for the post-2m `[1400,1499]` block; a single exposed translator (`_map_to_class`) the SWIG out-typemap routes through; an exact-mapping coverage test; and the subprocess-watchdog regression test deferred from 053 (a raising callback never deadlocks the engine).

Technical approach: all new behavior lives in `bindings/python/fixpp.i` (`%init` class creation + `%pythoncode` errors module + the translator; the out-typemap rewired to route through it; a `FIXPP_PY_GIL_RELEASE_CANARY` compile macro) and new pytests under `bindings/python/tests/`. The `.specify/2m-pybind.md` design doc will be amended as an implement-phase Article XX deliverable whose shape Gate A ratifies; the edit lands in the feature PR per the 043/051 precedent (Article XX §1 requires the amendment committed in the same PR — Gate A has no PR yet). No `include/fix/c_api/*.h` change.

## Technical Context

**Language/Version**: SWIG 4.x interface + C/C++ trampoline (C++23 toolchain), CPython 3.12 reference interpreter (targets 3.10–3.13 per `[2m]`); Python test code (pytest).
**Primary Dependencies**: SWIG ≥4.0, `Python3::Module`, the static `fixpp_capi` archive (049/050/051/052), the bundled `dictionaries/FIX44.xml` (reused by the GIL/watchdog loopback tests).
**Storage**: N/A (in-memory FIX session over loopback TCP).
**Testing**: pytest (`bindings/python/tests/`) — typed-exception tests, the GIL-release canary witness (local), and the subprocess-watchdog raising-callback test; the non-canary suite runs in the Tier-1 `python-bindings` matrix (`none`/`asan`/`tsan`).
**Target Platform**: Linux x86_64 (in-tree `-DFIXPP_BUILD_PYTHON=ON`). pip/wheel/abi3/macOS/Windows are PY-005 / deferred.
**Project Type**: Language binding (SWIG) over an existing C ABI — single project, additive consumer + one Phase-2 design-doc amendment.
**Performance Goals**: None (correctness slice; Article VIII N/A).
**Constraints**: No `include/fix/c_api/*.h` modification (`0→1` freeze held). No C++ type/exception/symbol crosses `extern "C"`. Live-I/O and the deliberate-deadlock canary/watchdog MUST use bounded, test-failing deadlines / a subprocess hard timeout — never an unbounded wait in the in-matrix suite (CI-hang risk). The deliberate-hang canary build is **local-only** (FR-013).
**Scale/Scope**: 0 wrapped-function additions; ~12 exception classes + 6 binding/app subclasses + 1 translator; the out-typemap rewire; 1 GIL audit table; 1 release-canary macro; ~3 new pytest files; 1 design-doc census amendment. 3 user stories (P1 GIL, P2 exceptions, P3 watchdog).

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Article | Gate | Verdict |
|---|---|---|
| **IV §3** Distribution — Python via SWIG over the C ABI, Linux x86_64 | All work in `fixpp.i` + pytest; no new dependency. | **PASS** |
| **VI §5** Normative References in the `/specify` artifact | Added to `spec.md`; PY-002/PY-003 are existing OFFICIAL rows (no new coverage-index entry). | **PASS** |
| **VII §2/§3/§4** pytest, TDD mandatory, no untested code | Each axis authored test-first (RED): exact-mapping test RED until `AppError` added; canary test RED under the canary; watchdog RED if it hangs. | **PASS** (TDD ordering in tasks) |
| **VIII** Perf budgets / bench-in-PR | No perf-sensitive module; no bench. | **N/A** |
| **IX §1** Coverage ≥95/85 on touched modules | **No `include/`–`src/` library module modified** (additive binding consumer); `bindings/` is outside the `include src` lcov scope and is exercised by the new pytests. | **N/A** (stated, reviewer-checkable) |
| **IX §2** Sanitizers Tier-1 (ASan/UBSan/TSan) | The new typed-exception + watchdog tests run under the existing `none`/`asan`/`tsan` `python-bindings` matrix; the trampoline worker path stays sanitized every PR. **The Tier-1 UBSan leg (`constitution.md:134`) has no `python-bindings` lane** — 054 **carries forward the 053 D-9 UBSan waiver** (the binding UBSan lane was waived at 053; standing up one is a PY-005-era CI item). Recorded here as a verify-risk note, NOT silently dropped. | **PASS** (ASan/TSan); **UBSan leg WAIVED** (053 D-9 carry-forward) |
| **X §1/§5/§6** ABI Policy — C-ABI is a versioned contract | **C-ABI consumed unchanged** (no `c_api.h` edit) → X§6 ABI-change controls not triggered. Reentrancy contract honored (no blocking call from inside the callback documented). | **PASS** |
| **XX** Design-doc amendment process | The `[2m]` census amendment (FR-005: §1.3/§3.12/§6.5/§4.6 send-from-callback) **and** the `[2m §4.6]` `AppError` extension (FR-006) are amendments to a signed-off Phase-2 design doc → **Gate A reviews them per Article XX**. Both are recorded as additive/limitation amendments, not silent edits. | **PASS** (Gate-A-gated) |
| **XI §3** No `std::mutex` in awaitable headers | Trampoline + typemap are flat C/C++; no `asio::awaitable<...>`. | **N/A** |
| **XII §5** Transport security — explicit profile, `unset` rejected | The GIL/watchdog loopback reuses 053's explicit `insecure_plain_tcp` establishment recipe; no implicit default. | **PASS** (inherited) |
| **XV** Banned patterns | No global new/delete witness, no banned idioms. | **PASS** |

No violations requiring Complexity Tracking. One Article XX checkpoint (the `[2m]` amendments) is explicitly routed to Gate A.

## Project Structure

### Documentation (this feature)

```text
specs/054-python-gil-exceptions/
├── plan.md              # This file
├── research.md          # Phase 0 — decisions D-1..D-9
├── data-model.md        # Phase 1 — exception hierarchy / translator / GIL audit / canary / watchdog entities
├── quickstart.md        # Phase 1 — typed-except + GIL-release demonstration
├── contracts/
│   ├── python-exception-surface.md  # the fixpp.* exception hierarchy + translator + attrs contract
│   └── gil-discipline-contract.md   # the GIL audit table + release/reacquire + canary + [2m] amendment
└── checklists/
    └── requirements.md  # spec quality checklist (from /speckit-specify)
```

### Source Code (repository root = the library submodule)

```text
bindings/python/
├── fixpp.i              # EXPANDED: %init creates the FixppError hierarchy (parented chain) +
│                        #   %pythoncode errors block (_CODE_TO_NAME from exposed constants,
│                        #   _map_to_class, _make_error, _raise_for_code); out-typemap fixpp_error_t
│                        #   rewired to route through _raise_for_code (single source of truth);
│                        #   fixpp_strerror exposed; GIL-discipline audit comment-table;
│                        #   FIXPP_PY_GIL_RELEASE_CANARY macro guarding the %exception release bands
├── CMakeLists.txt       # EXPANDED: -DFIXPP_PY_GIL_RELEASE_CANARY option (local-only, mirrors the
│                        #   existing FIXPP_PY_GIL_CANARY reacquire-canary option)
└── tests/
    ├── test_smoke.py                       # KEPT
    ├── test_roundtrip.py                   # KEPT (updates fixpp.Error→FixppError-alias assertions stay green)
    ├── test_exceptions.py                  # NEW: typed hierarchy, fallback, attrs
    ├── test_error_coverage.py              # NEW: header-sourced set-equality coverage (error.h ↔ _CODE_TO_NAME / _map_to_class)
    ├── test_gil_release_canary.py          # NEW: two-mode release witness — GREEN in a normal build (in-matrix); RED in a FIXPP_PY_GIL_RELEASE_CANARY build (local-only)
    └── test_callback_raise_watchdog.py     # NEW: subprocess-watchdog raising-callback no-deadlock

.specify/2m-pybind.md     # TO BE AMENDED at /implement (Article XX deliverable; Gate A ratifies the shape):
                          #   send-from-callback census (§1.3 rule 2, §3.12, §6.5 table,
                          #   §4.6 CallbackReentrantClose docstring) + AppError addition to §4.6 / §6.7
include/fix/c_api/        # UNCHANGED — consumed, not modified (freeze held)
```

**Structure Decision**: Single-project additive binding. New code lives in `bindings/python/` (`fixpp.i`, `CMakeLists.txt`, three new test files). The exception classes + translator live in `%pythoncode` inside `fixpp.i` (single `fixpp` module — a `fixpp.errors` *submodule* per `[2m §4.6]`'s `fixpp.errors._map_to_class` naming would require a package restructure and is deferred; the as-built exposes `fixpp._map_to_class` / `fixpp.exception_for_code` in the module namespace, noted in research D-3). The only non-`bindings/` edit is the `.specify/2m-pybind.md` design-doc amendment (Article XX, Gate-A-reviewed).

## Complexity Tracking

> No Constitution Check violations — section intentionally empty. (The Article XX `[2m]` amendment is a process checkpoint, not a complexity violation.)

## Phase Notes

- **Phase 0 (`research.md`)** resolves: the GIL audit-table census; the discriminating release-canary construction + why it differs from 053's reacquire canary; the as-built-050 blocking-send deadlock mechanism (`src/capi/session.cpp:284-286` (send) / `:202-205` (close); rule `session.h:255-258`) and the `[2m]` census scope; the exception-hierarchy realization mechanism (`%init` class chain + `%pythoncode` translator routed-through by the out-typemap = single source of truth); the `_CODE_TO_NAME`-from-exposed-constants approach and the exact-mapping coverage assertion (non-fallback, not circular); the `AppError`/`[1400,1499]` extension + the `Unknown`/fallback collision resolution; and the subprocess-watchdog harness.
- **Phase 1** emits `data-model.md` (the class/translator/audit/canary/watchdog entities + the code→class map), `contracts/` (the Python exception surface + the GIL-discipline contract), and `quickstart.md`.
- Command stops after Phase 1 design. **Gate A runs next** (per the pipeline: `/plan` → Gate A → `/tasks`), and is the designated reviewer of the Article XX `[2m]` amendments + the new `AppError` public name + the spec-vs-`[2m §4.6]` reconciliation.

## Gate A

### Ratified Article XX reconciliations

- **Translator surface (RC-1).** The spec previously quoted `[2m §4.6]`'s package form `fixpp.errors._map_to_class` verbatim at four sites while the plan/research/data-model/contracts exposed the module-level surface. **Gate A round 1 ratifies the module-level `fixpp._map_to_class(code)` + public `fixpp.exception_for_code(code)` as the Article XX decision for 054**; the `fixpp.errors.*` package form is the **deferred package alias → PY-005** (a package restructure out of scope for this hardening slice). The spec sites (`spec.md` clarify Q2 / FR-008 / Key Entities / Normative Refs) are reconciled to the module-level surface so the contract `/tasks` consumes is single-valued. (Recorded in `spec.md` Clarifications, Session 2026-06-26 Gate A round 1.)
- The `[2m]` send-from-callback census amendment (FR-005, L-054-1) + the `AppError` `[1400,1499]` extension (FR-006) remain the Article XX amendments Gate A reviews (additive/limitation, not silent edits).

### Implement-phase task pin — `[2m]` Article XX amendment (committed deliverable)

Gate A ratifies the *shape* of the `[2m]` amendment; the live edit to `.specify/2m-pybind.md` lands at `/implement` and is **committed in the feature PR** (Article XX §1's same-PR clause; the 043/051 precedent — Gate A has no PR yet). **`/tasks` MUST emit an explicit implement-phase task** for this edit so it cannot be dropped (the 051 drop-risk is real). The committed deliverable covers, in one edit:

- the **four census sites** (FR-005 / E-6): §1.3 rule (2) (send-from-callback no longer unqualified-legal → L-054-1), §3.12 (as-built `use_future` blocking shape), §6.5 carve-out table (**add `session_send`**), and the §4.6 `CallbackReentrantClose` docstring (corrected to the L-054-1 limitation);
- the **`AppError`** `[1400,1499]` block addition to the §4.6 / §6.7 hierarchy (FR-006 / D-5);
- the **module-level translator-surface** Article XX decision (RC-1: `fixpp._map_to_class` / `fixpp.exception_for_code` as the 054 surface; `fixpp.errors.*` package form deferred → PY-005).

The `[2m]` doc is intentionally **NOT pre-applied** at Gate A.

### Round 1 — applied

- Round 1 applied 2026-06-26: Codex P1=1 P2=5 P3=1; Opus post-judging P1=0 P2=6 P3=3; rewrite addresses root causes RC-1 (translator surface reconciliation), RC-2 (audit-census + two-mode canary + watchdog re-staging), RC-3 (attribute/forward-compat totality), RC-4 (UBSan waiver-carry + blocking-send dual-cite). Reviews: research/reviews/codex_054-python-gil-exceptions_gate_a_review.md, research/reviews/opus_054-python-gil-exceptions_gate_a_adversarial_review.md.
- Round 2 applied 2026-06-26: Codex P1=0 P2=2 P3=0; Opus post-judging P1=0 P2=2 P3=0; rewrite addresses RC-A (reword the three present-tense [2m]-amendment overclaims to implement-phase Article-XX-deliverable framing + pin the [2m] edit as a committed implement task; the live [2m] is intentionally NOT pre-applied per the 043/051 precedent and Article XX §1 same-PR clause) and RC-B (reword data-model.md:67/:165 to the two-tier T-2 attribute contract). Reviews: research/reviews/codex_054-python-gil-exceptions_gate_a_2_review.md, research/reviews/opus_054-python-gil-exceptions_gate_a_2_adversarial_review.md.

### Round 1 — disagreements

- **Codex #6, Article IX §1 leg — DISAGREE (verdict unchanged).** Codex read `constitution.md:130`'s "Binding rule — no silent uncovered error/edge path" as a *Python-binding* coverage rule and argued `plan.md:38`'s IX §1 "N/A" is wrong. It is not a binding-the-Python-layer rule: "Binding" there means a *binding/mandatory* constraint, scoped explicitly to touched `include/fixpp/<mod>/*` + `src/<mod>/*` modules (`constitution.md:129`). 054 touches **no** `include`/`src` module (all work is in `bindings/python/`), which is outside the lcov scope, so `plan.md:38`'s IX §1 "N/A" is **correct as written**. The new SWIG error paths (translator, typemap failures) are covered by Article VII / pytest (FR-008/FR-010 tests, SC-002/SC-006), which is their correct home — not IX §1. Verdict unchanged; the UBSan §2 leg (a separate Codex #6 sub-finding) IS applied above.
