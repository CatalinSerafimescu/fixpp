---
description: "Task list — 049-c-abi-handles-errors (C ABI Feature A)"
---

# Tasks: C ABI engine surface — Feature A (handles, error surface, version negotiation)

**Input**: Design documents from `specs/049-c-abi-handles-errors/`
**Prerequisites**: plan.md ✔, spec.md ✔, research.md ✔, data-model.md ✔, contracts/ ✔, quickstart.md ✔
**Branch**: `049-c-abi-handles-errors` · **Gate A**: converged round 3 (2 rewrites), user-signed-off 2026-06-23.

**Tests**: REQUIRED. The spec mandates an enumerating correctness-oracle test (SC-002/006), pure-C compile/link smokes (SC-001/003), and negative gate checks (SC-004/005). TDD ordering: write the test RED before the implementation TU.

**Organization**: Tasks grouped by user story. US1 (version) + US2 (error surface) are both P1 and form the foundation; US3 (handles) depends on US2's published null/invalid codes; US4 (reentrancy) annotates the US1+US2 symbols.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no incomplete dependencies)
- **[Story]**: US1/US2/US3/US4 (Setup/Foundational/Polish carry no story label)
- All paths are repo-root-relative within the library submodule.

> **Shared-file note**: `src/capi/CMakeLists.txt` and `tests/capi/CMakeLists.txt` are each edited by multiple stories — those edits are **sequential, never `[P]`**. Build the Tier-1 `linux-clang` preset at `-j2` max (WSL2 cap); run sanitizer presets ONE AT A TIME.

---

## Phase 1: Setup (grounding + baseline)

**Purpose**: Establish the build baseline and the source census the renumber + expected-code table depend on.

- [X] T001 Build baseline + grounding (no source change): configure the Tier-1 `linux-clang` preset and confirm the existing `fixpp_capi` target + `src/capi/` `extern "C"` boundary + `fixpp_capi.map` + `.github/workflows/abi-golden.yml` build/pass pre-change; record the current exported-symbol set from the nm gate as the SC-007 baseline-delta reference.
- [X] T002 [P] Census the C-ABI surface against source: → see `research/census-ground-truth.md` (116-set, name-transparent renumber, +3 nm delta, full variant→code oracle). enumerate the **116** `fixpp::core::error` enumerators (holes {2–9,14–19,70}, highest slot 131) in `include/fixpp/core/error.hpp` and verify the data-model E-3 group mapping against the inline `→`/grouped-`← {…}` annotations; grep the repo for the literal provisional decimal numbers `3`/`10`/`11` (FR-011 / research D-4 census — per [[feedback_census_all_handrolled_scanners_before_scoping_parse_fix]] hunt EVERY occurrence) and record the hit-list to drive T009 (expected-code table) and T013 (renumber).

---

## Phase 2: Foundational (blocking prerequisite for ALL stories)

**Purpose**: The shared export macro every public header includes.

**⚠️ CRITICAL**: No user story can compile its headers until this is complete.

- [X] T003 Create `include/fix/c_api/export.h` (NEW) — `FIXPP_API_EXPORT`: `__declspec(dllexport)` under `_WIN32`+`FIXPP_CAPI_BUILD` / `__declspec(dllimport)` when `_WIN32` consuming / `__attribute__((visibility("default")))` on POSIX / empty for the static-archive consumer; C-clean (no C++), includes only what `<stdint.h>`-class headers need. Per research D-5; included by `error.h` + `version.h`.

**Checkpoint**: Foundation ready — US1 and US2 can begin.

---

## Phase 3: User Story 1 - Version detection + ABI compatibility (Priority: P1) 🎯 MVP

**Goal**: A pure-C consumer can query the C-ABI surface version and the (decoupled) library version and decide major-compatibility against compile-time macros.

**Independent Test**: Compile a pure-C program against `version.h`, call both accessors, compare major to the macro — matching major = compatible, mismatching = hard incompatibility.

### Tests for User Story 1 (write FIRST, ensure they FAIL) ⚠️

- [X] T004 [P] [US1] Write `tests/capi/version_test.cpp` (GTest, RED): `fixpp_version()` returns `{major=0,minor=2,patch=0}` and equals `FIXPP_C_ABI_VERSION_{MAJOR,MINOR,PATCH}`; composite `FIXPP_C_ABI_VERSION == (0<<16)|(2<<8)|0`; `fixpp_library_version()` returns `fixpp::core::FIXPP_VERSION` (0,0,1) and is **decoupled** from the C-ABI track (AC-2). Register the target in `tests/capi/CMakeLists.txt`.
- [X] T005 [P] [US1] Write `tests/capi/capi_version_smoke.c` (pure-C, SC-001, compile-gated): `#include <fix/c_api/version.h>` ONLY (no umbrella, no C++), call `fixpp_version()`/`fixpp_library_version()`, compare `.major` to `FIXPP_C_ABI_VERSION_MAJOR`. Register a **C** compile+link target in `tests/capi/CMakeLists.txt` linking the static `fixpp_capi` target. NOTE (analyze C3): do **NOT** define `FIXPP_CAPI_BUILD` — this TU is a *consumer*, not a builder of the shared lib (that macro is the dllexport/build side; consumers want dllimport / the static-archive no-op).

### Implementation for User Story 1

- [X] T006 [US1] Create `include/fix/c_api/version.h` (NEW): `typedef struct fixpp_version { uint16_t major, minor, patch, _reserved; } fixpp_version_t;` (PoD, E-4); macros `FIXPP_C_ABI_VERSION_MAJOR 0` / `_MINOR 2` / `_PATCH 0` + composite `FIXPP_C_ABI_VERSION`; `FIXPP_API_EXPORT fixpp_version_t fixpp_version(void);` + `fixpp_library_version(void);` decls; `#include <fix/c_api/export.h>`; C-clean. (FR-015/016/018)
- [X] T007 [US1] Create `src/capi/version.cpp` (NEW): `fixpp_version()` returns the `version.h` C-ABI macro values; `fixpp_library_version()` returns the **numeric** library version. NOTE (analyze C1): `fixpp::core::FIXPP_VERSION` is a `constexpr` **string** `"0.0.1"`, not a struct — do NOT parse it at runtime. Source the numeric components from the existing `FIXPP_VERSION_{MAJOR,MINOR,PATCH}` macros in `include/fix/c_api.h` (currently `0/0/1`; these move to a header per T022) → return `fixpp_version_t{0,0,1}`. Zero-alloc, value-typed. Add `version.cpp` to `src/capi/CMakeLists.txt` `fixpp_capi_objects`. Make T004/T005 GREEN. (FR-015, E-4)

**Checkpoint**: US1 independently testable — pure-C version query + macro compare works (MVP).

---

## Phase 4: User Story 2 - Stable bounded error surface + downgrade (Priority: P1)

**Goal**: Every published `fixpp_error_t` code maps from its `fixpp::core::error` source via an audited `translate()`, has a zero-alloc `fixpp_strerror`, the forward-compat downgrade is implemented, the provisional decimal codes are renumbered, and the audit + occupancy gates exist.

**Independent Test**: Drive the exact 116-enumerator set through `translate()` asserting each variant's specific expected code; every published code → non-null `strerror`; out-of-range → "unknown error"; future-minor code downgrades to UNKNOWN. The occupancy/audit gate fails deterministically on a redefined slot.

### Setup artifact (the correctness-oracle source of truth)

- [X] T008 [US2] Create the checked-in expected (variant → exact C code) table `tests/capi/expected_error_map.csv` from the T002 census: all **116** enumerators → their specific `FIXPP_ERR_*` code per data-model E-3 (override groups `session_*`/`log_*`/`otel_*`/`app_*`/`out_of_memory` → `FIXPP_ERR_UNKNOWN`). This single artifact seeds BOTH `error_codes_v1.txt` (T014) and the enumerating test (T009). (E-3 / E-3-test)

### Tests for User Story 2 (write FIRST, ensure they FAIL) ⚠️

- [X] T009 [US2] Write `tests/capi/error_surface_test.cpp` (GTest, RED): (a) drive the EXACT 116-enumerator set through `translate()` asserting each maps to its **specific** expected code from `expected_error_map.csv` — a correctness oracle, NOT "returns a published code"; **mutation note**: flipping one arm (e.g. `tls_handshake_failed → STORE_RUNTIME`) MUST be RED; (b) override arms asserted `== FIXPP_ERR_UNKNOWN` explicitly; (c) every published code → `fixpp_strerror` non-null + non-empty, out-of-range/undefined → "unknown error", asserted zero-alloc; (d) `translate_for_consumer(<minor-3 synthetic code>, consumer_minor=2) == UNKNOWN` while current minor-2 codes pass through (SC-006). Register the target in `tests/capi/CMakeLists.txt`. (SC-002/006, data-model E-3-test, per [[feedback_completeness_gate_exact_set_not_subset]] + [[feedback_coverage_push_enshrines_bugs]])

### Implementation for User Story 2

- [X] T010 [US2] Create `include/fix/c_api/error.h` (NEW): `typedef int32_t fixpp_error_t;` (never a C `enum`) + the full `[2i §4.3]` `#define` layout per data-model E-2 (cross-cutting `OK`=0/`CANCELLED`=1/`UNKNOWN`=2/`NULL_HANDLE`=3/`INVALID_HANDLE`=4/`VERSION_MISMATCH`=5/`BUFFER_TOO_SMALL`=6/`TYPE_MISMATCH`=7/`TAG_NOT_FOUND`=8/`INDEX_OUT_OF_RANGE`=9/`CAPI_CONFIG_INVALID`=10; wire 100–102; dict 200–202; threading 300–302; store 400–403; sync 500; tls 600–603; transport 700–703; decimal 800–801; control-plane 900–901; bindings 1200–1204; reserved blocks as comments incl. log+otel 1000–1099, tap 1100–1199, post-v1.x 1300–1399, future 1400+); `FIXPP_API_EXPORT const char* fixpp_strerror(fixpp_error_t code);` decl; `#include <fix/c_api/export.h>`; C-clean. (FR-006/007/010, E-2)
- [X] T011 [US2] Create `src/capi/error.cpp` (NEW): `fixpp_capi::detail::translate(fixpp::core::error) noexcept` — a **total** `switch`, **no `default`** (`-Wswitch`), 116 arms per data-model E-3, with override groups (`session_*`/`log_*`/`otel_*`/`app_*`/`out_of_memory`) → `FIXPP_ERR_UNKNOWN` each carrying a `// publish FIXPP_ERR_SESSION_* in Feature B` / deferral comment (L-049-2); `fixpp_capi::detail::translate_for_consumer(fixpp_error_t, uint16_t consumer_minor) noexcept` downgrade (`introducing_minor > consumer_minor → UNKNOWN`, E-5); `fixpp_strerror` static `const char* const` table, out-of-range/undefined → "unknown error", zero global-heap alloc. Add `error.cpp` to `src/capi/CMakeLists.txt`. Make T009 GREEN. (FR-007/008/009/017, research D-1/D-2/D-3)
- [X] T012 [US2] Renumber the provisional decimal codes in lockstep (FR-011, from the T002 census hit-list): remove the provisional `#define`s in `include/fix/c_api/decimal.h` and have it `#include <fix/c_api/error.h>`; replace `src/capi/decimal.cpp`'s local `map_error()` with the shared `translate()` (preserve `decimal_overflow` slot-11 → `DECIMAL_INVALID`); reconcile/annotate the `contracts/c_api_decimal.h` byte-for-byte copy referenced in `decimal.h`; update EVERY C-ABI test asserting literal `3`/`10`/`11` (tests/capi, tests/oracle, the Python decimal ctypes oracle) to the master names/values. **Stay-green verification scope (analyze C2) MUST include `tests/core/decimal_capi_error_test.cpp`, `tests/core/decimal_capi_layout_test.cpp`, `tests/core/decimal_reserved_tolerance_test.cpp`** — these assert by macro NAME (so they auto-renumber) but exercise `FIXPP_ERR_DECIMAL_*`/`FIXPP_ERR_BUFFER_TOO_SMALL` and MUST be built+run green post-renumber. Existing decimal C tests stay green.
- [X] T013 [US2] **RECONCILE/REPLACE** `tools/abi_history/error_codes_v1.txt` (analyze F1 — the file **already exists**; it is NOT new): the current file is a 4-field log/otel *forward-reservation* (`<int> <status> <enumerator> <notes>`, marking 1000–1012 `used`) that **contradicts L-049-2** (the 1000–1099 block has no `#define` and maps to `UNKNOWN` in Feature A — it must NOT be listed as `used`). Rewrite it in the FR-012/E-6 format: one line per **published** code `<numeric>\t<SYMBOL>\t<introducing_minor>` with `introducing_minor = 2` (uint16_t ABI-minor ordinal) for every current E-2 code, seeded from T008's `expected_error_map.csv`. Preserve the forward-reservation *knowledge* as **reserved-block comments** only (e.g. `# 1000–1099 log/otel — reserved, no #define in v1.0 (L-049-2); published in Feature B`), consistent with how data-model E-2 documents reserved blocks — do NOT carry forward any `used` data line for an unpublished code. (FR-012, E-6)
- [X] T014 [US2] Create `tools/check_capi_occupancy.sh` (NEW, Tier-1): **Check A** — header `#define` layout equals the `[2i §4.3]` published values; **Check B** — sibling `[2X §6.X]` variant-row counts equal the expected coalescing-coverage table (decimal 4 / wire 13 / dict 20 / threading 9 / store 10 / sync 4 / TLS 15 / transport 22); the two quantities are **NEVER** compared to each other; also assert `error_codes_v1.txt` has no slot redefinition vs the header; non-zero exit fails. (FR-013, E-6, mirrors existing `tools/*.sh` gate style)
- [X] T015 [US2] Negative check for the occupancy/audit gate (SC-004): a fixture that re-defines/duplicates a published slot makes `check_capi_occupancy.sh` exit non-zero **deterministically**; assert the clean tree passes and the mutated tree fails.

**Checkpoint**: US1 + US2 = the P1 core. Error surface, strerror, downgrade, decimal renumber, and the audit/occupancy gate all work.

---

## Phase 5: User Story 3 - Opaque handle catalogue (Priority: P2)

**Goal**: The five opaque handle typedefs exist with a per-handle destroy/invalidation discipline (per `[2i §4.2.1]`) and a documented null-vs-invalid code contract; the surface is pure-C-clean.

**Independent Test**: A pure-C TU includes the umbrella, declares a pointer of each handle type, references the null/invalid codes, and compiles+links with no C++ headers pulled.

**Depends on**: US2 T010 (publishes `FIXPP_ERR_NULL_HANDLE`/`FIXPP_ERR_INVALID_HANDLE`).

### Tests for User Story 3 (write FIRST, ensure they FAIL/compile-fail) ⚠️

- [X] T016 [US3] Write `tests/capi/handles_compile_test.c` (pure-C, SC-003, compile-gated, RED): `#include <fix/c_api.h>` (umbrella), declare a pointer of each of the 5 handle typedefs, reference `FIXPP_ERR_NULL_HANDLE`/`FIXPP_ERR_INVALID_HANDLE`; MUST compile+link as **C** with no C++ headers pulled. Register the C target in `tests/capi/CMakeLists.txt`.

### Implementation for User Story 3

- [X] T017 [US3] Create `include/fix/c_api/handles.h` (NEW): five incomplete forward typedefs (`fixpp_engine_t`/`fixpp_session_t`/`fixpp_msg_t`/`fixpp_dict_t`/`fixpp_store_t`); the **per-handle** destroy/invalidation discipline doc copied from `[2i §4.2.1]` (engine/dict/outbound-msg → idempotent NULL-safe never-throwing `*_destroy`; `session` → closed via `fixpp_session_close`, no `*_destroy`, invalidates on close; `store` → no destroy, invalidates when its session closes per `[2e §6.7]` N1; inbound `msg` → engine-destroyed at parse-window close); null-first → `FIXPP_ERR_NULL_HANDLE` / destroyed-or-corrupted → `FIXPP_ERR_INVALID_HANDLE` contract referencing `error.h`; C-clean, no `fixpp::` symbols. (FR-001/002/003, E-1)
- [X] T018 [US3] Make T016 GREEN + verify FR-004 compile leg: the pure-C umbrella TU compiles+links; handles add no exported symbols (typedefs are compile-time only). (Full nm-leak verification is T023 in Polish.)

**Checkpoint**: US3 — pure-C handle catalogue compiles clean.

---

## Phase 6: User Story 4 - Per-symbol reentrancy contract (Priority: P2)

**Goal**: Every exported C-ABI symbol carries exactly one reentrancy class, enforced by a discrete gate.

**Independent Test**: Each exported `fixpp_*` symbol's doc-block has exactly one of {thread-safe, single-thread, requires-session-lock}; the gate fails on a 0- or 2-class symbol.

**Depends on**: US1 T006 + US2 T010 (the symbols to annotate).

### Implementation for User Story 4

- [X] T019 [US4] Annotate every exported symbol's doc-block with exactly one reentrancy class per `[2i §4.10]` — `thread-safe` for `fixpp_strerror` (in `include/fix/c_api/error.h`) and `fixpp_version` + `fixpp_library_version` (in `include/fix/c_api/version.h`). (FR-014, E-5)
- [X] T020 [US4] Create `tools/check_capi_reentrancy.sh` (NEW, Tier-1, **discrete** — NOT folded into `check_capi_occupancy.sh`): for every exported `fixpp_*` declaration in `include/fix/c_api/*.h`, assert its **doc-block** (the contiguous comment immediately preceding the decl) contains **exactly one** of `thread-safe`/`single-thread`/`requires-session-lock` — 0 unannotated, 0 double-classed; "token anywhere in the file" is insufficient; non-zero exit fails. (FR-014, E-5 / research D-7)
- [X] T021 [US4] Positive/negative test for the reentrancy gate (SC-005): the real 3-symbol surface passes; a fixture symbol with 0 or 2 class tokens makes the gate fail deterministically.

**Checkpoint**: US4 — reentrancy annotated + mechanically enforced.

---

## Phase 7: Polish & Cross-Cutting Concerns

- [X] T022 Edit the umbrella `include/fix/c_api.h`: aggregate `#include` of `export.h` / `error.h` / `version.h` / `handles.h`; keep the umbrella C-clean. **Remove the existing inline macro blocks (analyze F2): `c_api.h` currently `#define`s both `FIXPP_C_ABI_VERSION_{MAJOR=0,MINOR=1,PATCH=0}` and the library-track `FIXPP_VERSION_{MAJOR=0,MINOR=0,PATCH=1}`.** The C-ABI macros move to `version.h` (now at MINOR=2) — DELETE the stale inline `FIXPP_C_ABI_VERSION_*` block from `c_api.h` to avoid a redefinition/stale-0.1.0 conflict with T006's `version.h`. Decide the library-track `FIXPP_VERSION_*` macros' home (keep in the umbrella or move beside the C-ABI macros) and ensure T007's `fixpp_library_version()` references whichever survives; no duplicate definition may remain.
- [X] T023 Export-map + ABI-golden (FR-019 / SC-007 / SC-003): ensure `fixpp_strerror`, `fixpp_version`, `fixpp_library_version` are exported by `fixpp_capi.map` (pattern `fixpp_*` already covers them — verify) and add them (sorted) to `tests/abi/golden/fixpp_capi_symbols.txt`; **re-capture the abi-golden baseline** in the same change; confirm the nm gate shows ONLY `fixpp_*` exports (0 C++ leak) and that the decimal renumber is invisible to it (its safety net is T013/T009, per research D-4/D-6 — abidiff upgrade explicitly deferred).
- [X] T024 [P] Wire `tools/check_capi_occupancy.sh` + `tools/check_capi_reentrancy.sh` into the Tier-1 CI job (same place `tools/check_layers.py` / corpus gates run); confirm `/speckit-verify` fires both.
- [X] T025 [P] Layer/architecture confirm: run `tools/check_layers.py` — the `capi → core` edge already exists (decimal.cpp links `fixpp_core`); update `.specify/architecture.md` ONLY if a new edge is introduced (none expected). (per [[feedback_gate_b_check_layers_post_fixer]])
- [X] T026 [P] Record named limitations + catalogue: add **L-049-1** (downgrade not wired to a live `consumer_minor` until Feature B's `fixpp_engine_create`) and **L-049-2** (`session_*`/`log_*`/`otel_*`/`app_*` → `UNKNOWN` in v1.0) to `spec/behaviors-and-limitations.md`; add the CA-001..004 feature-catalogue row(s) + the coverage-index note.
- [X] T027 Run quickstart.md validation end-to-end: build the pure-C smokes + run the `capi`/`error_surface`/`version`/`handles` ctest subset + execute both new `tools/*.sh` gates; confirm the documented commands work.

---

## Dependencies & Execution Order

### Phase dependencies
- **Setup (P1: T001–T002)** → no deps; T002 `[P]` with T001.
- **Foundational (P2: T003 export.h)** → after Setup; BLOCKS all stories (every header includes `export.h`).
- **US1 (P3) + US2 (P4)** → after Foundational; both P1, independently testable; can interleave.
- **US3 (P5)** → after **US2 T010** (needs `FIXPP_ERR_NULL_HANDLE`/`INVALID_HANDLE`).
- **US4 (P6)** → after **US1 T006 + US2 T010** (annotates those symbols).
- **Polish (P7)** → after all stories (T023 needs all 3 functions to exist; T022 umbrella needs all 4 headers).

### Within each story
- Tests written and FAILING before the implementation TU (TDD).
- Header (the contract the test compiles against) before its `.cpp`.
- `expected_error_map.csv` (T008) before the enumerating test (T009) and the audit file (T013).

### Shared-file sequencing (NOT parallel)
- `src/capi/CMakeLists.txt`: edited by T007 (version.cpp) then T011 (error.cpp).
- `tests/capi/CMakeLists.txt`: edited by T004/T005 (US1), T009 (US2), T016 (US3) — sequential.

### Parallel opportunities
- T002 `[P]` with T001.
- US1 tests T004/T005 `[P]` (different files).
- Polish T024/T025/T026 `[P]` (different files/tools).
- US1 and US2 can be worked concurrently by two developers after T003 (distinct files except the two CMakeLists, which serialize).

---

## Implementation Strategy

### MVP (🎯 US1 only)
1. T001–T002 (Setup) → T003 (Foundational) → T004–T007 (US1).
2. **STOP & VALIDATE**: pure-C version smoke + macro compare green.

### Incremental delivery (recommended — US1+US2 are the real P1 core)
1. Setup + Foundational → US1 (version) → US2 (error surface) = the P1 deliverable (validate enumerating oracle + occupancy gate).
2. Add US3 (handles) → pure-C umbrella compiles clean.
3. Add US4 (reentrancy) → discrete gate green.
4. Polish → abi-golden re-capture + CI wiring + B&L/catalogue + quickstart.

### Notes
- ABI-affecting feature: all four Article X §6 controls — `/clarify` ✔, Codex Gate A ✔ (converged r3), `/analyze` (next, MANDATORY), `/plan` sign-off ✔.
- Build `-j2` max; sanitizer presets ONE AT A TIME (WSL2 OOM cap).
- `translate()` is engine-internal (`fixpp_capi::detail`, not exported) — only `fixpp_strerror`/`fixpp_version`/`fixpp_library_version` cross the boundary.
- Pipeline next after `/tasks`: `/speckit-analyze` (MANDATORY) → `/speckit-checklist` → `/checklist-audit` → `/speckit-implement` → `/simplify` → `/speckit-verify` → Gate B.
