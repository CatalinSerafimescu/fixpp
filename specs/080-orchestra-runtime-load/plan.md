# Implementation Plan: Orchestra runtime dictionary load for non-C++ consumers

**Branch**: `080-orchestra-runtime-load` | **Date**: 2026-07-19 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/080-orchestra-runtime-load/spec.md`

## Summary

Make the already-runtime-loadable FIX Latest / Orchestra dictionary (delivered by 074's `dict::OrchestraLoader`) reachable from the two non-C++ acquisition surfaces that are currently hard-wired to `dict::XmlLoader`: the C-API `fixpp_dict_load_from_xml` and the TOML `dictionary.path` resolver. A single new dictionary-layer helper `dict::load_any(path, mr)` sniffs the document's root element (`<fix>` → `XmlLoader`, `<fixr:repository>` → `OrchestraLoader`, any other root → fail-closed `dict::` error) and is called by both surfaces so the dispatch rule is defined once. Independently, the config-file path gains a pre-check that detects a FIX50SP2 + FIX-Latest dictionary pair (which today `std::abort`s at `version_registry` construction) and emits a **new distinct config-layer `reason_class` value** before the registry is built, turning process death into a diagnosable config error.

**No C-ABI change.** The C-API entry point's *behavior* widens (an Orchestra file that returned `FIXPP_ERR_CAPI_CONFIG_INVALID` now returns `FIXPP_ERR_OK`), but no symbol, signature, or `fixpp_error_t` value changes and no byte-frozen header is edited — the dual-dictionary collision is unreachable via the C-API (census below), so its distinct error lives in the C++ config layer, not `error.h`.

## Technical Context

**Language/Version**: C++23 (Article II §1; local/CI toolchain Clang 22 per Article XVII §7).

**Primary Dependencies**: pugixml (root-element sniff + both loaders), `std::pmr` (loader `memory_resource*` param), toml++ (config path; unchanged). No new third-party dependency.

**Storage**: N/A (reads dictionary XML files already shipped under `dictionaries/`).

**Testing**: gtest + ctest; local Tier-1 mirror `linux-clang-debug`; `/speckit-verify` serial preset matrix; Tier-2 MSVC + ABI golden in CI.

**Target Platform**: Linux x86_64 (Tier-1) + Windows MSVC (Tier-2).

**Project Type**: In-process C++23 library with an adjacent C ABI (Article I §2 / IV §2).

**Performance Goals**: None new. Dictionary acquisition is a **startup / one-time** path, not the per-message hot path — Article VIII §3's bench requirement (hot-path perf changes) does not apply. The root-element sniff adds one lightweight parse at load time only.

**Constraints**: C-ABI frozen at `1.5.0` — `tests/abi/golden/fixpp_capi_symbols.txt` (`nm` symbol set) and `tools/capi_freeze.sha256` (header byte-freeze incl. `error.h`/`version.h`) MUST pass without regeneration (SC-005). No codegen/golden/emitter change; existing read goldens byte-identical. 074 T022h loader-unit invariant (`XmlLoader` rejects `<fixr:repository>`) preserved.

**Scale/Scope**: Small, surgical. One new helper (`dict::load_any`, header + cpp), two call-site redirects (C-API thunk, TOML resolver), one appended `reason_class` value + a config-layer collision pre-check, and the test suite. No change to `src/session/`, `bindings/`, codegen, or the wire path.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-checked after Phase 1 design (below).*

| Article | Gate | Disposition |
|---|---|---|
| **I §1** (FIX version scope) | FIX Latest is already admitted at the read/dictionary tier (074, constitution v0.6). | **PASS, no amendment.** This feature adds *acquisition entry points* for an already-admitted runtime dictionary; it does not widen the FIX version set, add ApplExtID(1156)=303 differentiation, or add session negotiation (all still post-1.0). First feature since 074 that needs **no** constitution amendment. |
| **VI** (100% FIX rule) | New catalogue rows? | **N/A** — no new FIX message coverage. Close-out adds a `behaviors-and-limitations` L-row (the C-API/TOML contract-widening) + a `feature-catalogue` row per the standard close-out, not a coverage-index message row. |
| **VII** (testing) | Tests required. | **PASS** — RED-first tests for each FR (see quickstart). Whole-binary grouped test targets, `ctest -L` selection (Article VII §8). |
| **VIII §3** (perf bench) | Hot-path perf change? | **N/A** — startup-only load path; documented above. No bench required. |
| **IX §5** (abidiff / ABI hygiene) | ABI golden diff. | **PASS by no-op** — no exported C symbol added/changed; `nm` symbol golden + header byte-freeze unchanged (SC-005). `/speckit-verify` asserts both stay green. |
| **X** (ABI policy) | C-ABI contract review. | **PASS** — no `fixpp_error_t` addition, no signature/symbol change, no frozen-header edit. The C-API behavioral widening is documented (contract note + B&L row). §4 (error reporting) untouched. |
| **XVI §3 / Appendix A** (mandatory controls) | `/clarify`, `/analyze`, Gate A, `/plan` sign-off. | **TRIGGERED** via "Codegen layout — dictionary loader, multi-version coexistence" and a new public C++ `dict::` symbol. `/clarify` ✓ done. `/analyze` + Codex Gate A + user `/plan` sign-off REQUIRED before `/tasks`. |
| **XVII §1** (Gate A) | Touches public C++ API / dictionary loader. | **Gate A MANDATORY.** |
| **XV** (banned patterns) | Fail-closed disposition. | **PASS** — `load_any` propagates the loaders' existing fail-closed `dict::*_error` types; unrecognized/malformed root → `dict::` parse error (C-API `catch(...)`→`CONFIG_INVALID`; TOML `trap_throw_to_expected`→diagnostic). The collision pre-check is fail-closed (emits the new `reason_class`, does not proceed to registry build). `version_registry`'s existing `std::abort` retained as the direct-C++ fail-loud backstop. |

**No Complexity Tracking entries** — no constitution violation to justify.

## Project Structure

### Documentation (this feature)

```text
specs/080-orchestra-runtime-load/
├── plan.md              # This file
├── spec.md              # Feature spec (/speckit-specify + /speckit-clarify)
├── research.md          # Phase 0 — design decisions + the C-ABI-surface pivot
├── data-model.md        # Phase 1 — entities: load_any, reason_class value, collision predicate
├── quickstart.md        # Phase 1 — runnable validation scenarios (RED-first)
├── contracts/
│   ├── load_any.md      # dict::load_any signature + dispatch contract
│   └── surfaces.md      # widened C-API/TOML behavior + new reason_class value contract
├── checklists/
│   └── requirements.md  # spec quality checklist (from /speckit-specify)
└── tasks.md             # /speckit-tasks output — NOT created here
```

### Source Code (repository root = library submodule)

```text
include/fixpp/dict/
├── load_any.hpp                 # NEW — dict::load_any(path, mr) declaration
├── xml_loader.hpp               # unchanged
├── orchestra_loader.hpp         # unchanged
└── version_registry.hpp         # unchanged (abort backstop retained)

src/dictionary/
├── load_any.cpp                 # NEW — root-element sniff + dispatch, dict:: error types
├── xml_loader.cpp               # unchanged (T022h root reject intact)
└── orchestra_loader.cpp         # unchanged

include/fixpp/config/
└── load_diagnostic.hpp          # EDIT — append reason_class::conflicting_dictionaries

src/config/
└── selector_resolver.cpp        # EDIT — (a) call dict::load_any not XmlLoader (~L359);
                                 #        (b) collision pre-check over engine.dictionaries
                                 #            → emit conflicting_dictionaries diagnostic

src/capi/
└── dictionary.cpp               # EDIT — fixpp_dict_load_from_xml calls dict::load_any (~L48)

tests/dictionary/  tests/config/  tests/capi/
└── *                            # NEW — sniff dispatch, C-API/TOML Orchestra load,
                                 #        collision reason_class, single-dict ok,
                                 #        classic-fix regression, T022h invariant pin
```

**Structure Decision**: Single-project library layout (Option 1). The feature is a thin dispatch/entry-point addition inside the existing `dict::` and `config::` layers plus one C-API thunk redirect; it introduces one new translation unit (`load_any.cpp`) and edits three existing files. No new module, directory, or build target beyond the test binaries.

## Complexity Tracking

> No Constitution Check violations — section intentionally empty.
