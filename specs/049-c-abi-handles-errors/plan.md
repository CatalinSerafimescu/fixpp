# Implementation Plan: C ABI engine surface — Feature A (handles, error surface, version negotiation)

**Branch**: `049-c-abi-handles-errors` | **Date**: 2026-06-23 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/049-c-abi-handles-errors/spec.md`

## Summary

Establish the foundation C-ABI surface (CA-001..004) that Features B and C build on: the opaque-handle catalogue + per-handle destroy/invalidation discipline, the full bounded `fixpp_error_t` master enum with `fixpp_strerror()`, the per-symbol reentrancy contract, and the runtime version accessors + compile-time macros. The technical core is a single `translate(fixpp::core::error) → fixpp_error_t` coalescing function plus a static `strerror` table. The map is an **audited coalescing decision** (data-model E-3), not a mechanical transcription: of the **116 `fixpp::core::error` enumerators** (highest assigned slot 131, holes at {2–9, 14–19, 70}) in `include/fixpp/core/error.hpp`, only ~72 carry an inline `→ FIXPP_ERR_*` arrow — `store_*`/`session_*`/`tls_*` use grouped `← { … }` prose — and the `log_*`/`otel_*`/`app_*`/`session_*` (and `out_of_memory`) groups are **deliberate overrides** to `UNKNOWN` because `[2i §4.3]` publishes no `#define` for them yet (L-049-2). The published numeric layout itself is fixed by `[2i §4.3]`. No new runtime dependency, no codegen, no wire/session behaviour. The C-ABI version stays **pre-1.0** (additive MINOR bump 0.1.0→0.2.0); the `0→1` freeze is a GA / Phase-8 task per `remaining-work/release-engineering.md` Task 2.

## Technical Context

**Language/Version**: C++23 (clang-22, `cppstd=23`); public headers must also compile as C (C11) — they include only `<stdint.h>`/`<stddef.h>`/`<stdbool.h>`.
**Primary Dependencies**: none new. Implementation TUs link the existing `fixpp_core` (for `fixpp::core::error`) inside `src/capi/`. No third-party additions.
**Storage**: N/A.
**Testing**: GoogleTest (`tests/capi/`), compiled as C++; plus a **pure-C** compile/link smoke (SC-001) to prove the headers are C-clean and no C++ headers leak. `tools/check_capi_occupancy.sh` + `error_codes_v1.txt` audit checked in CI (Tier 1).
**Target Platform**: Linux/Clang (Tier 1, the gating surface) + the per-PR `abi-golden` nm gate; Windows/MSVC Tier-2 consumes the same headers (export macro must be MSVC-correct).
**Project Type**: C-ABI layer of a C++ library — `include/fix/c_api/` (public C headers) + `src/capi/` (the only `extern "C"` TU set, the AGPL-isolation boundary).
**Performance Goals**: `fixpp_strerror` / `fixpp_version` / `fixpp_library_version` are O(1) and **zero global-heap allocation** (static table / value-typed PoD), per `[2i §4.4]` / `[2i §4.5]`.
**Constraints**: no C++ symbol leakage (`fixpp_capi.map` `fixpp_*; local: *`, verified by the per-PR nm gate); no exception crosses `extern "C"` (the `[2i §4.2.3]` thunk discipline); decimal boundary PoD `fixpp_decimal_t` frozen per Article X §3 (untouched — only its *error codes* renumber).
**Scale/Scope**: ~30 published C-ABI error codes coalescing the 116 `fixpp::core::error` enumerators (highest slot 131, holes {2–9, 14–19, 70}); 5 opaque handle typedefs; 3 new exported functions; 3 new tools (occupancy gate + reentrancy gate + audit file); 1 new export header. No handle *functions* (those are Features B/C).

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

- **Article X — ABI Policy (the governing article).**
  - §X.1 versioned contract + Codex Gate A mandatory → **Gate A is in the pipeline** (this is an ABI-surface change).
  - §X.2 no C++ symbol leakage → enforced by `fixpp_capi.map` + the per-PR `abi-golden` nm gate; golden symbol list updated for the 3 new functions (FR-019). **PASS by construction.**
  - §X.3 decimal boundary PoD frozen → `fixpp_decimal_t` layout untouched; only the *error-code numbers* move (provisional, pre-1.0). **PASS.**
  - §X.4 bounded enum + reserved ranges + stability + audit trail + occupancy gate → FR-006/012/013 implement exactly this. The audit trail records a `uint16_t` introducing-minor ordinal (current codes = 2) keying the downgrade per `[const §X.4]`; the occupancy gate is the `[2i §4.3]` two-check split (Check A header `#define` layout + Check B sibling variant-row counts, never compared to each other). Stability rule does not yet bind (still major 0). **PASS.**
  - §X.5 per-symbol reentrancy documented → FR-014, enforced by a discrete exactly-one-class-per-doc-block gate (`check_capi_reentrancy.sh`), separate from the occupancy gate. **PASS.**
  - §X.6 ABI-affecting → all four mandatory controls (`/clarify` ✔ done, `/analyze` pending, Codex Gate A pending, `/plan` sign-off pending). **On track.**
- **Article II — Language/Compilers.** C++23/clang-22; C-compatibility of public headers is an existing invariant (decimal.h already C-clean). **PASS.**
- **Article IX — Coverage/Sanitizers/Static analysis.** Per-PR ≥95% line / ≥85% branch on touched modules (`src/capi/`, `include/fix/c_api/`). The 116-arm `translate()` switch + the `strerror` table are the coverage risk → mitigated by an **enumerating test** that drives the exact 116-enumerator set through `translate()` (asserting each maps to its **specific expected code** from a checked-in table, mutation-tested — a correctness oracle, not a publishedness proxy; data-model E-3-test) and every published code through `fixpp_strerror` (Phase 1 / SC-002/SC-006). ASan/UBSan/TSan Tier-1 all apply. §IX.5 abidiff "from the first tagged C ABI release onward" — not yet tagged; the nm gate is the active drift check (the abidiff upgrade is noted in research as a deferred CA-feature item). **PASS.**
- **Article XV — no hot-path alloc.** `strerror`/`version` are zero-alloc by contract (FR-007). **PASS.**
- **No new dependency / no codegen / no new wire or `reason_class` surface.** **PASS** (matches the recent config-feature pattern).

**No violations. Complexity Tracking table not required.**

**Post-Phase-1 re-check**: still no constitution violation. Phase 1 surfaced one design observation (research D-8 / data-model E-3): `session_*` C++ variants reference `FIXPP_ERR_SESSION_*` codes absent from `[2i §4.3]`. **Resolved within Feature A** by mapping them — like the already-unpublished `log_*`/`otel_*`/`app_*` groups — to `FIXPP_ERR_UNKNOWN` (no Feature-A function produces a `session_*` error; the path is unobservable here). Publishing a real `FIXPP_ERR_SESSION_*` block + the `[2i]` amendment is a **Feature-B** item (L-049-2), not a Feature-A blocker. `translate()` is a total switch; no open mapping decision remains.

## Project Structure

### Documentation (this feature)

```text
specs/049-c-abi-handles-errors/
├── plan.md              # This file
├── spec.md              # /speckit-specify + /speckit-clarify output
├── research.md          # Phase 0 (this command)
├── data-model.md        # Phase 1 (this command)
├── quickstart.md        # Phase 1 (this command)
├── contracts/           # Phase 1 (this command)
│   ├── error-surface.md       # fixpp_error_t master enum + strerror + translate()
│   ├── version-surface.md     # fixpp_version()/fixpp_library_version() + macros + downgrade rule
│   ├── handle-catalogue.md     # 5 opaque handles + destroy discipline + null/invalid contract
│   └── tooling-and-abi.md      # error_codes_v1.txt + check_capi_occupancy.sh + abi-golden delta
└── checklists/
    └── requirements.md   # spec-quality checklist (done)
```

### Source Code (repository root)

```text
include/fix/c_api/
├── export.h        # NEW — FIXPP_API_EXPORT (declspec on MSVC / visibility on POSIX); required by [2i §4.3/§4.5]
├── error.h         # NEW — fixpp_error_t int32_t master enum (#define constants) + fixpp_strerror decl
├── version.h       # NEW — fixpp_version_t + fixpp_version()/fixpp_library_version() + FIXPP_C_ABI_VERSION_* macros
├── handles.h       # NEW — opaque forward structs: fixpp_engine_t/session_t/msg_t/dict_t/store_t + destroy-discipline doc
├── decimal.h       # EDIT — drop the provisional error #defines; include "fix/c_api/error.h" (decimal codes now from master)
├── log.h, otel.h   # (pre-existing reserved sub-headers — untouched)
include/fix/
└── c_api.h         # EDIT — umbrella: include error.h/version.h/handles.h/export.h; bump FIXPP_C_ABI_VERSION_MINOR (0.1.0→0.2.0)

src/capi/
├── error.cpp       # NEW — translate(error)→fixpp_error_t (116 enumerators→~30 coalescing) + fixpp_strerror table + downgrade translate(code, consumer_minor)
├── version.cpp     # NEW — fixpp_version()/fixpp_library_version() (PoD from version.hpp + the c_api.h macros)
├── decimal.cpp     # EDIT — replace local map_error() with the shared translate(); codes auto-renumber via macro names
├── capi.cpp        # (fixpp_version_string — untouched)
└── CMakeLists.txt  # EDIT — add error.cpp + version.cpp to fixpp_capi_objects

tools/
├── abi_history/
│   └── error_codes_v1.txt   # NEW — append-only audit: every numeric code → symbol + uint16_t introducing_minor (=2 now)
├── check_capi_occupancy.sh  # NEW — occupancy/drift gate (Tier 1): Check A (header #define layout == [2i §4.3]) + Check B (sibling variant-row counts == coverage table); the two are NEVER compared to each other
└── check_capi_reentrancy.sh # NEW — discrete reentrancy gate (Tier 1): exactly one class token per exported symbol's doc-block, 0 unannotated

tests/capi/
├── error_surface_test.cpp   # NEW — drive the exact 116-enumerator set→translate() asserting each variant's specific expected code (checked-in table, mutation-tested); all codes→strerror; out-of-range→"unknown"; downgrade
├── version_test.cpp         # NEW — fixpp_version()/library_version() match macros; downgrade rule unit tests
├── handles_compile_test.c   # NEW — pure-C TU: include c_api.h, exercise opaque-handle typedefs + null-handle codes
└── CMakeLists.txt           # EDIT — register the new tests + the pure-C compile/link target (SC-001/SC-003)

tests/abi/golden/
└── fixpp_capi_symbols.txt   # EDIT — add fixpp_strerror, fixpp_version, fixpp_library_version
```

**Structure Decision**: Reuse the existing C-ABI layer exactly as-is — public headers under `include/fix/c_api/`, implementation in the single `src/capi/` `extern "C"` boundary TU set (the AGPL-isolation boundary documented in `src/capi/CMakeLists.txt`). The `[2i §4.1]` per-domain header split (`error.h`/`version.h`/`message.h`/...) is adopted now for the headers this feature owns (`error.h`, `version.h`, `handles.h`, `export.h`); message/dict/session split headers are created by their owning features. `translate()` lives in `src/capi/error.cpp` (engine-internal, not exported) so the 116-enumerator→~30 mapping is testable and the consumer never sees the C++ enum.

## Complexity Tracking

> No Constitution Check violations — table intentionally omitted.

## Gate A

- Round 1 applied 2026-06-23: Codex P1=2 P2=6 P3=1; Opus post-judging P1=2 P2=6 P3=2; rewrite addresses root causes #1 (verbatim-framing/overrides + handle ownership), #2 (tooling mechanics + SC-004/006/007), #3 (116-enumerator census + correctness-oracle test). Reviews: research/reviews/codex_049-c-abi-handles-errors_gate_a_review.md, research/reviews/opus_049-c-abi-handles-errors_gate_a_adversarial_review.md.
- Round 2 applied 2026-06-23: Codex round-2 = all round-1 items CLOSED/PARTIAL, 0 new P1, residual = stale top-level spec.md FRs + quickstart; Opus post-judging P1=0 P2=1 P3=1; final rewrite syncs spec.md FR-002/012/013 + US3/AC/Key-Entities + quickstart.md :38/:52 to the corrected design docs (root cause: round-1 fixed the design appendix but not the top-level spec). Reviews: research/reviews/codex_049-c-abi-handles-errors_gate_a_2_review.md, research/reviews/opus_049-c-abi-handles-errors_gate_a_2_adversarial_review.md.
