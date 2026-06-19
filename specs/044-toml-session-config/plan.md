# Implementation Plan: Native TOML config-file loading (session establishment)

**Branch**: `044-toml-session-config` | **Date**: 2026-06-19 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/044-toml-session-config/spec.md`

## Summary

Deliver a **native TOML config-file loader** that parses a human-authored `.toml` file into a fully-validated **configuration bundle** — the file-expressible engine-level establishment settings plus one or more per-session `SessionConfig` definitions — and **fails closed before `Session::open`**. The loader is *pure config translation* (PATH-B): it hydrates the existing public config value-types (`fixpp::session::SessionConfig` + the establishment slice of `fixpp::core::EngineConfig`) and resolves the in-scope object selectors (`store` / `cert_source` / `dictionary` / `transport` / `clock`) from `{kind, params}` into the existing built-in factories. It adds **no** runtime adapter, **no** new wire/codegen/C-ABI surface, and **no** new `fixpp_error_t` value.

**Technical approach.** A new isolated component `fixpp_config_toml` (own CMake target) PRIVATE-includes a header-only TOML parser (`tomlplusplus/3.4.0`) in its `.cpp` only, so a host that does not use file config takes on no dependency (FR-004 / SC-006 satisfied *structurally* — the parser is never a link edge of `fixpp_core`/`fixpp_session`). The entry point is `load_toml_config(path, LoadOptions{engine_executor, resource})`: the engine executor builds the clock + TLS factory at load (DECISION-1), and `resource` is the cold-path load-time arena for the dictionary/cert load-time allocation (`XmlLoader::load`/`make_file_cert_source` — Codex Gate A round 2 #1). It returns a C++-only `LoadResult` carrying either the bundle or a **collected vector** of per-key `LoadDiagnostic`s (replacing 008's single collapsed `store_factory_failed`; FR-017/FR-018). It preserves every text-boundary invariant — no-implicit-default, closed-enum canonical spellings, frozen-at-open, fail-closed, credential redaction — and leaves the host-supplied values (`Application` callbacks, the executor instance, the runtime arenas) for the host to inject before open.

## Technical Context

**Language/Version**: C++23 (Clang 22 primary; GCC sanity; MSVC) — `[const §II.1]`
**Primary Dependencies**: `tomlplusplus/3.4.0` (header-only, MIT, AGPL-compatible — **verified latest on Conan Center 2026-06-19**); reuses existing `fixpp_session` / `fixpp_core` / `fixpp_tls` / `fixpp_dict` / `fixpp_transport` value-types + factories. No new transitive runtime deps into core.
**Storage**: N/A (the loader *configures* the message store; it has none of its own).
**Testing**: GoogleTest (`[const §VII.1]`); TDD red-green-refactor (`[const §VII.3]`). Negative-file battery (one per error class) + a field-for-field equivalence test vs. a hand-built config + a multi-session-defaults test + a QuickFIX-parity-table test.
**Target Platform**: Linux primary; the loader is platform-neutral host code (no OS-specific surface beyond `std::filesystem`).
**Project Type**: In-process C++23 library component (an *opt-in adjacent* target, like `quickfix_compat` but properly isolated).
**Performance Goals**: None constitutional — the loader is a **cold, one-shot, pre-open path**. `[const §XV.1]` hot-path allocation ban does not apply (explicitly noted in Constitution Check). No benchmark required (`[const §VIII.3]` is about perf-sensitive modules).
**Constraints**: Fail-closed before any `Session::open`; no implicit defaults; closed-enum canonical spellings only; frozen-at-open; redact credentials in all diagnostics; parser dependency MUST NOT enter the embeddable core.
**Scale/Scope**: One file → N session definitions + shared defaults. ~28 Bucket-A scalars + 4 structured members + 5 object selectors. Sizing **MEDIUM** — **not** because of any new core artifact (there are **zero**: `MemoryStoreFactory` already exists — see research D-5/D-5a), but because of the breadth of the validation/mapping surface: the collect-all per-key diagnostic taxonomy, ~28 scalar + enum canonical-spelling maps, 5 selector resolvers over existing factories, the noexcept-boundary throwing-site conversions (D-3), the multi-session `[default]`-merge model, and the negative-file battery. The feature is pure selector-mapping + validation + tests in the new `fixpp_config_toml` component.

## Constitution Check

*GATE: evaluated article-by-article against `.specify/constitution.md` v0.3. Re-checked after Phase 1.*

| Article | Verdict | Notes |
|---|---|---|
| **I — Identity/Mission** | PASS | No FIX-version-coverage impact; config supply mechanism only. |
| **VI — Spec Coverage** | PASS (with action) | Design-blessed feature → **design catalogue row `[const §XV.16]`**, not an `OFFICIAL` row (§VI.3). **Action (must be a NAMED pre-merge task in `tasks.md`, naming BOTH files):** add the `[const §XV.16]` design row to `spec/feature-catalogue.md` + a design-choice note to `spec/coverage-index.md` **before merge** (§VI.4). **Normative References** section now present in spec (§VI.5) — was absent at first draft (the 042 false-PASS class; corrected). |
| **II — Language/Compilers** | PASS | C++23, no new compiler constraints. |
| **III — Build/Deps** | PASS (with action) | New Conan dep `tomlplusplus/3.4.0` — pinned, declared in `conanfile.py`, AGPL-compatible MIT. Header-only ⇒ `tool_requires`-free, no shared/static option. **Action:** add to `requires`; user signs off the dep at `/plan`; Codex Gate A reviews it (§III.2). |
| **IV/V — Distribution/License** | PASS | tomlplusplus MIT (no LGPL, `[const §V.3]`/§XV.12). Loader is library code, AGPL. |
| **VII — Testing/TDD** | PASS | TDD mandatory; negative-file battery + equivalence + parity tests planned (Phase 1 contracts). |
| **VIII — Perf** | N/A | Cold load-time path; not a perf-sensitive module. No bench needed; no hot-path budget touched. |
| **IX — Coverage/Sanitizers** | PASS | 95/85 on touched modules; ASan/UBSan/TSan via `/speckit-verify` matrix. Loader is single-threaded synchronous code (no new concurrency). |
| **X — ABI** | **PASS — pinned** | **No C-ABI change. No new `fixpp_error_t` value.** Diagnostics are a C++-only loader-local `LoadDiagnostic`/`LoadResult` type that never crosses `include/fix/c_api.h`. This is the explicit replacement for 008's `store_factory_failed` collapse — done in C++ space only. |
| **XI — Concurrency** | N/A | No coroutine/strand/executor change. The loader is synchronous, pre-open. It does not select an executor *instance* (host-supplied, FR-010); it may select threading *policy* (`mode`/`locks`) and MUST fail closed on `direct_executor` without `already_serialized_executor` (FR-011) **and** on `direct_executor`+`spin` regardless of attestation (FR-011a / research D-6b — `session.cpp:904`). |
| **XII — Security** | **PASS — constrained (Gate-A trigger)** | Configures `SecurityProfile` + cert paths ⇒ **Gate A mandatory** (Appendix A: TLS config, cert-source plug-in). Loader MUST preserve §XII.5 no-implicit-default (`kind::unset` rejected at open), never default `insecure_plain_tcp`, and keep §XII.7 `EncryptMethod(98)` ban intact. **Step-1 accepted profiles: `{mtls_ca, one_way_ca, insecure_plain_tcp}`; `mtls_pinned` is recognized-but-deferred** (no file channel for pin material — `[const §XII.6]` pinset rotation is its own v1.0 feature; research D-9a / FR-006b). **Step 1 is plaintext-key-only** (no `password_cb` file channel; an encrypted key fails closed as an invalid cert selector, FR-006b). Profile↔transport-factory consistency is enforced by the **existing 043 `Session::open()` check** (`session.cpp:946-966`), NOT a divergent loader check (see research D-6). |
| **XIII — Observability** | OUT OF SCOPE | Logger/tracer/meter/tap/arena config is deferred to step 2 (FR-009). The loader must reject those keys under the distinct "recognized-but-not-yet-supported (step 2)" reason (FR-018a), never silently ignore. |
| **XIV — Pluggable Interfaces** | PASS | Resolves the **existing** pluggable defaults; adds no new pluggable interface, no new pure-virtual, and **no new type**. `MemoryStoreFactory` already exists (`memory_store_factory.hpp:41`, `final : MessageStoreFactory`); the loader reuses it (research D-5/D-5a). The earlier "one new type" claim was a stale Explore-pass error, corrected at Gate A. |
| **XV — Banned Patterns** | PASS | §16 explicitly blesses TOML ("TOML is also accepted"). §1 hot-path-alloc N/A (cold path). No banned crypto/format/dep. |
| **XVI — Spec-Kit Workflow** | PASS | `/clarify` DONE (4 decisions). `/analyze` MANDATORY before `/implement` (Security trigger). Clean-context phases. |
| **XVII — Codex Gates** | PASS (gates pending) | **Gate A MANDATORY** (Security trigger) before `/tasks`. Gate B mandatory pre-merge. `/speckit-verify` after `/implement`. User `/plan` sign-off MANDATORY (Appendix A). |
| **XVIII — Roadmap** | PASS | No protocol shipping; v1.0 in-scope. |

**No violations requiring Complexity Tracking.** There are **zero new core artifacts**: `MemoryStoreFactory` already exists and is reused (research D-5/D-5a). The only new build unit is the isolated `fixpp_config_toml` component (an opt-in adjacent target, not a core type or interface).

**New-module grounding action (Gate A round 1 — Codex #3).** The new `config` module / `fixpp::config` namespace is **ungrounded** in the layer authority: `.specify/architecture.md` has no `config` module row or `fixpp::config` namespace row, and `tools/check_layers.py` has no `config` whitelist entry — its `module_of_path` returns `config` for `src/config/*.cpp` while `check_file` defaults an unknown module to an **empty** allowed-set (`check_layers.py:~123`), so every `#include <fixpp/{session,core,tls,dict,transport}/…>` in `src/config/` would be flagged a layer violation at the build-time layer gate. Two implementation tasks (scoped here; the files themselves are outside this bundle — do NOT edit them as part of the bundle rewrite):
- **Task (architecture.md):** add the `config` module row + the `fixpp::config` namespace row to `.specify/architecture.md` (architecture.md is the binding module-placement authority — `[[feedback_gate_b_check_layers_post_fixer]]`).
- **Task (check_layers.py):** add a `config` whitelist entry — `config` MAY include `core`/`session`/`dictionary`/`tls`/`transport`; assert the negative edge that NO `core`/`session`/`dict`/`tls`/`transport` module includes or links `config`.

**Mandatory controls (Appendix A) for this feature:** `/clarify` ✅done · `/analyze` ⏳required · **Codex Gate A** ⏳required (Security) · user `/plan` sign-off ⏳required.

## Project Structure

### Documentation (this feature)

```text
specs/044-toml-session-config/
├── plan.md              # this file
├── research.md          # Phase 0 — decisions D-1..D-10 (parser, isolation, diagnostics, selectors, parity)
├── data-model.md        # Phase 1 — entities + field-population map + selector→factory param table
├── quickstart.md        # Phase 1 — example TOML + host stub
├── contracts/
│   └── toml_config_loader.hpp   # Phase 1 — public loader API + LoadResult/LoadDiagnostic shape
├── checklists/
│   └── requirements.md  # spec-quality checklist (from /specify)
└── tasks.md             # Phase 2 — created by /speckit-tasks (NOT here)
```

### Source Code (repository root = the library submodule)

```text
include/fixpp/config/                         # NEW public surface for the loader component
├── toml_config_loader.hpp                    #   load_toml_config(path, LoadOptions{engine_exec, resource}) -> LoadResult
├── config_bundle.hpp                         #   ConfigBundle / SessionDefinition value types
└── load_diagnostic.hpp                       #   LoadDiagnostic + reason-class enum + LoadResult

src/config/                                   # NEW isolated component (own CMake target fixpp_config_toml)
├── CMakeLists.txt                            #   PRIVATE tomlplusplus; links fixpp_session/_core/_tls/_dict/_transport
├── toml_config_loader.cpp                    #   parse + collect-all-errors + bundle assembly
├── selector_resolver.cpp                     #   {kind,params} -> built-in factories (store/cert/dict/transport/clock)
└── scalar_mappers.cpp                        #   ~28 Bucket-A scalar + enum-token canonical-spelling maps

tests/config/                                 # NEW test dir for the component
├── CMakeLists.txt
├── test_load_happy_path.cpp                  #   US1 — field-for-field equivalence vs hand-built config
├── test_load_negative_battery.cpp            #   US2 — one cell per error class; collect-ALL assertion
│                                             #     incl. direct_executor+spin (D-6b), security_profile.kind
│                                             #     missing/empty (FR-006a), malformed cert + malformed/unknown
│                                             #     dictionary XML → diagnostic NOT terminate (D-3 noexcept);
│                                             #     required-at-load per-key cells (data-model E-3 bucket B):
│                                             #     clock.kind, sender/target_comp_id, begin_string,
│                                             #     store/dictionary/transport selectors each missing→missing_required
│                                             #     + empty→empty_required; transport.host/port required-if-initiator;
│                                             #     encrypted PEM key → invalid_or_contradictory_selector NOT terminate (D-9a)
├── test_load_selectors.cpp                   #   US3 — each selectable kind + unknown-kind reject;
│                                             #     dictionary by-path OK; kind="version" + dialect_overlay +
│                                             #     security_profile.kind="mtls_pinned" →
│                                             #     recognized_not_yet_supported_step2 (FR-007a / FR-006b / D-9a)
├── test_load_multisession_defaults.cpp       #   US4 — shared defaults + per-session override;
│                                             #     per-session transport_factory_override use_count()==1 (D-6a)
├── test_quickfix_parity_table.cpp            #   SC-004 — every QF establishment key has an equivalent
└── fixtures/                                 #   .toml fixtures (good + one-error-per-class)
```

> **No new core file.** An earlier draft listed `include/fixpp/session/memory_store_factory.hpp` + `src/session/memory_store_factory.cpp` as NEW — that was a stale Explore-pass error: `MemoryStoreFactory` already exists in this checkout (`memory_store_factory.hpp:41`). The loader reuses it (research D-5/D-5a); no new core file is created.

**Structure Decision**: a **new `fixpp_config_toml` component** under `src/config/` + `include/fixpp/config/`, mirroring the *intent* of `quickfix_compat` (opt-in, adjacent) but **correctly isolated** — `quickfix_compat` compiles into `fixpp_session` (`src/session/CMakeLists.txt:69`), which would drag the parser into core; this component is its own target that nothing in core links. The header-only parser keeps even the link graph clean. The `config` module/namespace it introduces must be grounded in `.specify/architecture.md` + `tools/check_layers.py` (the new-module grounding action above), or the layer gate flags every cross-module include as a violation. There is **no new core artifact** — `store=memory` reuses the existing `MemoryStoreFactory` (research D-5/D-5a).

## Complexity Tracking

> No Constitution Check violations. Table intentionally empty.

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| — | — | — |

## Gate A

- Round 1 applied 2026-06-19: Codex P1=0 P2=5 P3=1; Opus post-judging P1=1 P2=7 P3=2; rewrite addresses root cause (stale Explore claims) + the P1 executor-at-load fork (DECISION-1) + by-path-only dictionary (DECISION-2). Reviews: research/reviews/codex_044-toml-session-config_gate_a_review.md, research/reviews/opus_044-toml-session-config_gate_a_adversarial_review.md.
- Round 2 applied 2026-06-19: Codex P1=0 P2=2 P3=0; Opus post-judging P1=0 P2=4 P3=1; final rewrite closes the load-boundary-completeness class — LoadOptions{executor,resource} (EDIT-1), mtls_pinned + encrypted-key plaintext-only fail-closed boundary (EDIT-2), full required-at-load key enumeration (EDIT-3). Reviews: research/reviews/codex_044-toml-session-config_gate_a_2_review.md, research/reviews/opus_044-toml-session-config_gate_a_2_adversarial_review.md.
- Round 3 applied 2026-06-19: Codex 0P1/2P2/0P3; Opus post-judging 0P1/2P2/1P3; final mechanical pass — FIXT default_appl_ver_id (FIX-1) + cert_source-on-TLS-profile (FIX-2) conditional-required rows in E-3, encrypted-PEM deferred-set contradiction removed (FIX-3). All fork-free; conditional-required enumeration now closed at 3. Reviews: research/reviews/codex_044-toml-session-config_gate_a_3_review.md, research/reviews/opus_044-toml-session-config_gate_a_3_adversarial_review.md.
