# Implementation Plan: FIX Latest Typed Message Classes via Native Orchestra Codegen (`fixpp::vlatest` tier)

**Branch**: `076-fix-latest-typed-codegen` | **Date**: 2026-07-15 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/076-fix-latest-typed-codegen/spec.md`

## Summary

Generate the typed message layer (`build_<Msg>` / `validate_<Msg>` / typed args / readback / reify) for all 181 FIX Latest (EP303) messages into a new, distinct `fixpp::vlatest` namespace, using the existing 067/069 codegen emitter fed by 074's native `OrchestraLoader`. The typed-class/validator namespacing is free (the emitter partitions on `session_version`, kept distinct for `vlatest` by 074); the real work is (a) teaching the codegen `build_ir` path to load the FIX Latest dictionary via `OrchestraLoader` instead of the hardcoded `XmlLoader`, (b) registering a `kCodegenVersions` `vlatest` row + special-casing `app_version_enum`, (c) wiring a fifth per-version codegen invocation behind a build option that **defaults ON**, and (d) proving completeness with a **non-circular** raw-XML census (message + full field-set, all depths) since FIX Latest has no QuickFIX peer. Application-version auto-dispatch reachability (both `dispatch_application` and `reify_dispatch_application`) is out of scope — it forces the injective wire-ApplVerID break that is the deferred ApplExtID(1156)=303 feature's problem. **Verified (World A, code-read):** the typed validate/reify/round-trip path is INDEPENDENT of that deferred re-keying — the generated validator is `constexpr` tables, the runtime validator takes its `table_view` as a caller-supplied ctor argument, and reify is dict-free (`version_v = v50sp2` is inert). So this feature does not need `version_registry` re-keying; it stops cleanly at the caller-supplied-dict typed API.

## Technical Context

**Language/Version**: C++23 (library); the codegen tool is C++23 host code under `tools/codegen/fixpp-codegen/`.

**Primary Dependencies**: existing in-tree only — the 067/069 codegen emitter (`ir.cpp`, `emit_messages.cpp`, `emit_validator.cpp`, `emit_dispatch.cpp`, `gen_util.hpp`, `main.cpp`), 074's `dict::OrchestraLoader` (pugixml), the runtime `Dictionary`/`table_view`, the `wire::body_builder` / typed-args machinery. No new third-party dependency.

**Storage**: N/A. Input is the already-vendored, pinned `dictionaries/orchestra/OrchestraFIXLatest.xml` (EP303, 181 messages). Output is generated headers under the build tree (`${CMAKE_BINARY_DIR}/_codegen/include/fixpp/vlatest/`) plus a checked-in codegen golden.

**Testing**: gtest + ctest, whole-binary grouped per Article VII §8, selected via `ctest -L`. New surfaces: the non-circular completeness census, the 181-message typed round-trip, the build-option ON/OFF matrix behavior, and the codegen determinism golden for the `vlatest` tier.

**Target Platform**: Linux (clang/gcc) Tier-1/Tier-3 + Windows MSVC Tier-2, per the standard CI matrix. Codegen runs at CMake configure time (host), identical to the existing four-version invocation.

**Project Type**: C++23 FIX engine library + its offline codegen tool. Single project (Option 1).

**Performance Goals**: N/A for runtime hot path — this feature adds no runtime code to `parse`/`validate`/session send-recv beyond the generated per-message typed builders/validators, which mirror the existing legacy tiers. The **relevant cost is build-time / binary-size** (compile of 181 additional classes across the sanitizer/preset matrix). **Clarification disposition (build-cost measurement):** the spec's "`/plan` MUST measure the compile/binary delta" is satisfied here as an **ex-ante estimate** (≈ one `v50sp2`-sized tier; 181 vs 156 messages — research.md R6) with the **precise measurement deferred to implement task T1** — the code being measured does not exist until implementation, so a real number is unobtainable at `/plan`. The ON default holds unless T1's measurement is surprising, in which case it is re-raised with the user before merge. No new hot-path perf budget (Article VIII §3) is engaged.

**Constraints**: (1) **Zero C-ABI / Python / link-ABI change** — C-ABI GA-frozen at 1.5.0 (FR-008, SC-004). (2) **Strictly additive** — v42/v44/v50sp2/vt11 generated output byte-identical with the option ON or OFF (FR-004, SC-003). (3) **Build option defaults ON**, full matrix on the `vlatest` tier, both ON+OFF paths in CI (FR-003). (4) **Injective wire-ApplVerID map preserved** — no `vlatest` wiring into *either* application-version auto-dispatch surface (`dispatch_application` AND `reify_dispatch_application`), both of which collide `vlatest` onto v50sp2 (FR-009, SC-005). (5) **Non-circular exact-set census mandatory** (FR-006).

**Scale/Scope**: 181 generated message classes in one new namespace; ~5 codegen-tool touch points (`ir.cpp` load branch + `kCodegenVersions` row, `gen_util.hpp` `app_version_enum` special-case, `cmake/Codegen.cmake` fifth invocation + build option); 1 new census test, 1 round-trip test, determinism-golden extension.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

### Amendment required (rides this feature's Gate A)

- **Article I §1 — FIX Latest carve-out MUST be narrowed (MINOR bump v0.7 → v0.8).** The current text (v0.7) reads: *"FIX Latest (typed-codegen / session-negotiation tiers) … are post-1.0 milestones"* and *"only its typed-codegen, ApplExtID(1156)=303 differentiation, and session-negotiation tiers remain post-1.0."* This feature **delivers the FIX Latest typed-codegen tier for v1.0**, which contradicts that carve-out. The amendment reclassifies **"typed codegen"** from post-1.0 to **v1.0-delivered-by-076**, leaving only **ApplExtID(1156)=303 differentiation + session negotiation** as post-1.0.
  - **Bump class**: MINOR (additive version-tier reclassification; no banned-pattern / perf-budget / config change → not v-major per Article XX §4). Identical in kind to v0.6 (074) and v0.7 (075).
  - **§XVIII.5 disposition**: NO residual conflict — §5 bars early-shipping deferred **protocols** (SOFH/SBE/FIXP/FAST/JSON/GPB/MMT); typed codegen over an already-supported dictionary is not a protocol. The amendment is what makes the scope cease to be deferred, not a §5 waiver.
  - **§XVIII.6/§7 disposition**: §6 defers per-version codegen for the *runtime-XML-only* legacy versions (4.0/4.1/4.3/5.0/5.0SP1); FIX Latest is Orchestra-native, not runtime-XML-only, so §6 does not bar it. §7 (v0.5) governs the v44 app-message *family* set; FIX Latest is a distinct version namespace governed by Article I §1, not §7 — no §7 change needed.
  - **Process**: this feature is an Appendix-A codegen-trigger feature → **Codex Gate A required**. Per the established Gate-A-fold deviation from Article XX §2's standalone-PR letter (precedents 035/043/068/069/074/075), the mechanical amendment text + Sync Impact Report **rides this feature's branch/Gate A** — do NOT run `/speckit-constitution` standalone (constitution is symlink-single-sourced). User sign-off at Gate A. This is the amendment the 2026-07-15 v1.0 SCOPE DECISION pre-authorized for the first catalogue-closure feature.

### Compliant (no change)

- **Article I §3/§4 (Master Feature Catalogue / no silent omissions)** — PASS, advanced. This feature moves catalogue rows A-035..A-065 (FIX Latest typed) toward `done`; the exact-set census enforces "no silent omissions" at field granularity.
- **C-ABI freeze (1.5.0)** — PASS. FR-008/SC-004: zero C-ABI / Python / link-ABI change; verified by the existing ABI-golden gate.
- **Article VIII §3 (hot-path perf needs a bench)** — NOT ENGAGED. Additive codegen; no hot-path change. The build-cost measurement owed by the spec is a scope-cost input (research.md R6), not a runtime perf budget.
- **Article VII §8 (test grouping)** — PASS by construction: new tests are whole-binary grouped, `ctest -L`-selected, no `gtest_discover_tests` buckets.
- **Testing rigor (mutation-discriminating witnesses, exact-set completeness, no false-green)** — PASS: FR-006 mandates exact-set (not subset), the census is built from an *independent* source (raw XML), and the determinism golden guards non-deterministic emit.
- **Article XVIII §5 (no early-ship of deferred protocols)** — PASS: no protocol shipped; see §XVIII.5 disposition above.

**Gate result**: PASS with one required amendment (Article I §1), folded into Gate A per precedent. No unjustified complexity — see Complexity Tracking (empty).

## Project Structure

### Documentation (this feature)

```text
specs/076-fix-latest-typed-codegen/
├── plan.md              # This file
├── spec.md              # Feature spec (+ Clarifications 2026-07-15)
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/           # Phase 1 output (generated-API + build-option contracts)
├── checklists/
│   └── requirements.md  # Spec quality checklist (16/16)
└── tasks.md             # /speckit-tasks output (NOT created here)
```

### Source Code (repository root = library submodule)

```text
tools/codegen/fixpp-codegen/
├── ir.cpp               # build_ir: add OrchestraLoader load branch + kCodegenVersions vlatest row
├── gen_util.hpp         # app_version_enum: "vlatest" special-case (mirror vt11→Unknown)
├── emit_messages.cpp    # unchanged (keys on ir.ns → fixpp::vlatest for free)
├── emit_validator.cpp   # unchanged (keys on ir.ns)
├── emit_dispatch.cpp    # unchanged — vlatest deliberately excluded from dispatch_application (FR-009)
└── main.cpp             # unchanged driver (accepts the fifth --xml job)

cmake/
└── Codegen.cmake        # add build option (default ON) + fifth --xml OrchestraFIXLatest.xml invocation

dictionaries/orchestra/
└── OrchestraFIXLatest.xml   # existing pinned input (074) — unchanged

<build tree>/_codegen/include/fixpp/vlatest/   # generated: Fields/Messages/Validator/Reify/Builders.hpp
tools/codegen/golden/                            # extend the checked-in codegen determinism golden with vlatest

tests/
├── codegen/             # completeness census (raw-XML exact-set) + determinism assertion
└── wire/  (or tests/codegen/)  # 181-message typed round-trip + build-option ON/OFF behavior witness
```

**Structure Decision**: Single project. The change is confined to the offline codegen tool (`tools/codegen/fixpp-codegen/`), its CMake wiring (`cmake/Codegen.cmake`), and new tests. No `src/` runtime code, no `capi/`, no `bindings/python/` changes. Generated output lands in the existing per-version build-tree layout, adding a `vlatest/` sibling to `v42/v44/v50sp2/vt11/`.

## Complexity Tracking

> No Constitution Check violations requiring justification. The Article I §1 amendment is a governance reclassification handled through the Article XX process (folded into Gate A), not an architectural complexity deviation. Table intentionally empty.
