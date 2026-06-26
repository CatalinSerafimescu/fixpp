# ABI Consumption Checklist: Thin End-to-End Python Binding (PY-001)

**Purpose**: Requirements-quality gate for correct consumption of the frozen C-ABI by the first real C-ABI consumer / `0→1` freeze validator (no `c_api.h` change, version injection, static-link, freeze held). Audience: Gate B reviewer.
**Created**: 2026-06-26
**Feature**: [spec.md](../spec.md) · [plan.md](../plan.md) · [research.md](../research.md) (D-4, D-6)

## Requirement Completeness

- [x] CHK019 Is "MUST NOT require a C-ABI source change" stated as a hard requirement, with the failure path defined (close any surfaced gap by an additive MINOR C-ABI change before merge, freeze held)? [Completeness, Spec §FR-012] — PASS: FR-012 states "This feature MUST NOT require a C-ABI source change … If implementation surfaces a C-ABI gap, it MUST be closed by an additive (MINOR) C-ABI change before this feature merges, preserving the held `0→1` freeze"; `tasks.md §Notes` repeats the STOP-and-raise instruction.
- [x] CHK020 Is the static-link requirement specified completely — `fixpp_capi` archive (PIC) **and** `-static-libstdc++ -static-libgcc` on Linux — with the runtime acceptance (no `libfixpp_capi.so` dependency) stated? [Completeness, Spec §FR-010, research D-6] — PASS: FR-010 mandates static linking of both the C-ABI archive and the C++ standard library; `research.md D-6` gives the exact link flags (`-fPIC` archive + `-static-libstdc++ -static-libgcc`); `swig-typemap-contract.md T-6` repeats all three: PIC archive + static-stdlib + `Python3::Module`; runtime no-`.so`-dependency is the observable outcome of SC-001.
- [x] CHK021 Is the consumed C-ABI surface attributed to its source slices (049/050/051/052) so the reviewer can confirm every wrapped symbol is a shipped, frozen symbol? [Completeness, Spec §Normative References] — PASS: `spec.md §Normative References` cites "C-ABI contract headers exercised: `fix/c_api/{dict,engine,session,message,version,error}.h` (the 049/050/051/052 surface)"; `research.md` preamble confirms all signatures re-verified against shipped headers at Gate A r1.
- [x] CHK022 Is the version-injection requirement specified — which macros (`FIXPP_C_ABI_VERSION_MAJOR/_MINOR`) and where they are injected (the `engine_create` 4-arg call)? [Completeness, contract T-1, research D-4] — PASS: `swig-typemap-contract.md T-1` specifies: expose macros via `%constant`/`%inline` over `version.h:32-33`; the `engine_create` hand-wrapper calls the real 4-arg `fixpp_engine_create(cfg, FIXPP_C_ABI_VERSION_MAJOR, FIXPP_C_ABI_VERSION_MINOR, &out)` (`engine.h:81-84`); `research.md D-4` confirms MAJOR=0, MINOR=5.

## Requirement Clarity

- [x] CHK023 Is the `0→1` freeze condition stated unambiguously (the freeze stays HELD by this feature; PY-001..004 validate before any `0→1` flip)? [Clarity, Spec §FR-012, §Normative References] — PASS: FR-012 states "preserving the held `0→1` freeze"; `spec.md §Normative References` states "the C ABI is a versioned, reentrancy-documented contract; consumed unchanged by this feature (no `include/fix/c_api.h` modification; the `0→1` freeze stays held)"; `plan.md §Constraints` repeats it; language is unambiguous.
- [x] CHK024 Is the exact `fixpp_engine_create` arity (4-arg `(cfg, major, minor, &out)`) pinned against the shipped header, given a stale 2-arg/name assumption was corrected at Gate A? [Clarity, research.md preamble + D-4] — PASS: `research.md` preamble records the Gate A r1 correction ("the re-verification found two signatures wrong and corrected them: `fixpp_engine_create` is the 4-arg form"); D-4 pins `fixpp_engine_create(cfg, uint16_t consumer_major, uint16_t consumer_minor, fixpp_engine_t** out)` citing `engine.h:81-84`; confirmed against shipped `engine.h:81-84`.

## Requirement Consistency

- [x] CHK025 Are the version-macro values the binding injects (`MAJOR=0`, `MINOR=5`) consistent with the shipped `version.h` and the 052 MINOR bump? [Consistency, research D-4, contract T-1] — PASS: `version.h:32-33` defines `FIXPP_C_ABI_VERSION_MAJOR 0` and `FIXPP_C_ABI_VERSION_MINOR 5`; `research.md D-4` cites `version.h:32-33` and records MAJOR=0, MINOR=5; `swig-typemap-contract.md T-1` repeats these values; the 052 MINOR 4→5 bump is reflected consistently.
- [x] CHK026 Is the "consumed unchanged, no `include/fix/c_api.h` modification" claim consistent across spec, plan Structure Decision, and the tasks scope guard? [Consistency, Spec §FR-012, plan.md §Structure, tasks.md §Scope guard] — PASS: FR-012 states the hard requirement; `plan.md §Structure Decision` states "The C-ABI headers/sources and the engine are untouched"; `tasks.md §Scope guard` states "This feature is an additive consumer — it does NOT modify `include/fix/c_api.h` (FR-012)"; all three are consistent.

## Exception / Recovery Coverage

- [x] CHK027 Is the recovery path defined for the case where implementation surfaces a real C-ABI gap (STOP; raise a separate additive MINOR C-ABI feature before this merges)? [Coverage, Exception Flow, Spec §FR-012, tasks.md T020/Notes] — PASS: FR-012 defines the recovery path ("MUST be closed by an additive (MINOR) C-ABI change before this feature merges, preserving the held `0→1` freeze"); `tasks.md §Notes` repeats "if a real C-ABI gap surfaces, STOP and raise it as a separate additive (MINOR) C-ABI feature before this merges (freeze held)"; the recovery sequence is unambiguous.

## Dependencies & Assumptions

- [x] CHK028 Is the assumption that **no C++ type, exception, or symbol crosses the `extern "C"` boundary** (the property that makes `-static-libstdc++` safe) documented? [Assumption, plan.md §Constraints] — PASS: `plan.md §Constraints` states "No C++ type, exception, or symbol crosses the `extern "C"` boundary (the C-ABI thunks catch-all), which is what makes static-libstdc++ safe"; `research.md D-6` also records "static-libstdc++ is safe here precisely because no C++ type or exception crosses the `extern "C"` boundary".
- [x] CHK029 Are the C-ABI reentrancy/lifetime contracts the consumer must honor referenced as a dependency (e.g., register-callback before `engine_start`; per-op msg-handle destroy; no blocking call from the callback)? [Dependency, plan.md IX/X rows, contracts/python-module-surface.md §Callback] — PASS: `python-module-surface.md §Callback` specifies register-before-start, no blocking calls from callback, read-within-window; `data-model.md E-4` states register-before-engine_start (citing `session.h:268`); `plan.md` Constitution Check X rows document reentrancy/lifetime consumer obligations.
- [x] CHK030 Is the bundled `dictionaries/FIX44.xml` dependency documented as present and loadable (the artifact the round-trip consumes)? [Dependency, Spec §Assumptions] — PASS: `spec.md §Assumptions` states "The round-trip uses the FIX 4.4 dictionary (`FIX44.xml`, bundled under `dictionaries/`)"; `tasks.md T001` lists "bundled `dictionaries/FIX44.xml` is present + readable" as a precondition check; `quickstart.md` uses `DICT = "dictionaries/FIX44.xml"`.

## Audit Result

| Disposition | Count |
|---|---|
| PASS | 12 |
| SPEC-FIXED | 0 |
| DD-DECIDED | 0 |
| WAIVED | 0 |
| **Total** | **12** |

### SPEC-FIXED items
None.

### DD-DECIDED items
None.

### WAIVED items
None.

Anchors spot-verified: `[2m §4.2]`, `[2m §4.4]`, `[2m §5]`, `[2m §6.1]`, `[2m §6.7]`, `[const §X.1]`, `[const §X.5]`, `[const §X.6]` — all resolve in signed-off revision `.specify/2m-pybind.md` (Draft v0.3 Gate A r2) and `.specify/constitution.md`.
