# Implementation Plan: Python GIL Discipline & Typed Exception Translation (PY-002 + PY-003)

**Branch**: `054-python-gil-exceptions` | **Date**: 2026-06-26 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/054-python-gil-exceptions/spec.md`

## Summary

Harden the merged PY-001 SWIG binding (`bindings/python/`) along two co-located SWIG-layer axes, **without touching the C-ABI** (the `0→1` freeze stays held):

1. **PY-002 — GIL discipline.** Turn PY-001's reactive 3-wrapper GIL release into a *systematic, audited, witnessed* discipline: a documented audit table classifying every wrapped C-ABI function as GIL-releasing (blocking) or GIL-holding; a **discriminating, proven-RED** GIL-release canary (the 053 teardown-vs-worker-callback deadlock shape) — distinct from the GIL-reacquire canary 053 already proved; and a **census amendment** to the normative `[2m]` design (every "send-from-callback is legal" site) recording the as-built-050 blocking-send deadlock as a current limitation.

2. **PY-003 — typed exceptions.** Realize the ratified `[2m §4.6]` exception hierarchy verbatim (root `FixppError`, `Error = FixppError` alias, one subclass per `fixpp_error_t` block + the five `BindingError` subclasses), **extended** with `AppError` for the post-2m `[1400,1499]` block; a single exposed translator (`_map_to_class`) the SWIG out-typemap routes through; an exact-mapping coverage test; and the subprocess-watchdog regression test deferred from 053 (a raising callback never deadlocks the engine).

Technical approach: all new behavior lives in `bindings/python/fixpp.i` (`%init` class creation + `%pythoncode` errors module + the translator; the out-typemap rewired to route through it; a `FIXPP_PY_GIL_RELEASE_CANARY` compile macro) and new pytests under `bindings/python/tests/`. The `.specify/2m-pybind.md` design doc is amended (Article XX). No `include/fix/c_api/*.h` change.

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
| **IX §2** Sanitizers Tier-1 (ASan/UBSan/TSan) | The new typed-exception + watchdog tests run under the existing `none`/`asan`/`tsan` `python-bindings` matrix; the trampoline worker path stays sanitized every PR. | **PASS** |
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
    ├── test_exceptions.py                  # NEW: typed hierarchy, exact-mapping coverage, fallback, attrs
    ├── test_gil_release_canary.py          # NEW: the discriminating release witness (skipped unless canary build)
    └── test_callback_raise_watchdog.py     # NEW: subprocess-watchdog raising-callback no-deadlock

.specify/2m-pybind.md     # AMENDED (Article XX): send-from-callback census (§1.3 rule 2, §3.12, §6.5 table,
                          #   §4.6 CallbackReentrantClose docstring) + AppError addition to §4.6 / §6.7
include/fix/c_api/        # UNCHANGED — consumed, not modified (freeze held)
```

**Structure Decision**: Single-project additive binding. New code lives in `bindings/python/` (`fixpp.i`, `CMakeLists.txt`, three new test files). The exception classes + translator live in `%pythoncode` inside `fixpp.i` (single `fixpp` module — a `fixpp.errors` *submodule* per `[2m §4.6]`'s `fixpp.errors._map_to_class` naming would require a package restructure and is deferred; the as-built exposes `fixpp._map_to_class` / `fixpp.exception_for_code` in the module namespace, noted in research D-3). The only non-`bindings/` edit is the `.specify/2m-pybind.md` design-doc amendment (Article XX, Gate-A-reviewed).

## Complexity Tracking

> No Constitution Check violations — section intentionally empty. (The Article XX `[2m]` amendment is a process checkpoint, not a complexity violation.)

## Phase Notes

- **Phase 0 (`research.md`)** resolves: the GIL audit-table census; the discriminating release-canary construction + why it differs from 053's reacquire canary; the as-built-050 blocking-send deadlock mechanism (`session.h:256-260`) and the `[2m]` census scope; the exception-hierarchy realization mechanism (`%init` class chain + `%pythoncode` translator routed-through by the out-typemap = single source of truth); the `_CODE_TO_NAME`-from-exposed-constants approach and the exact-mapping coverage assertion (non-fallback, not circular); the `AppError`/`[1400,1499]` extension + the `Unknown`/fallback collision resolution; and the subprocess-watchdog harness.
- **Phase 1** emits `data-model.md` (the class/translator/audit/canary/watchdog entities + the code→class map), `contracts/` (the Python exception surface + the GIL-discipline contract), and `quickstart.md`.
- Command stops after Phase 1 design. **Gate A runs next** (per the pipeline: `/plan` → Gate A → `/tasks`), and is the designated reviewer of the Article XX `[2m]` amendments + the new `AppError` public name + the spec-vs-`[2m §4.6]` reconciliation.

## Gate A

- (pending — runs after this plan per the pipeline)
