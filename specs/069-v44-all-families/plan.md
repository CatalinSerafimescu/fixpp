# Implementation Plan: v44 all-families typed codegen coverage

**Branch**: `069-v44-all-families` | **Date**: 2026-07-11 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/069-v44-all-families/spec.md`

## Summary

Widen the existing `067` codegen writer-emitter so it generates typed `build_<Msg>` / `validate_<Msg>` / `<Msg>Args` for **all in-scope FIX44 application messages** (**83 in-scope = 85 `msgcat='app'` minus {BE, BF}**; 33 existing OFFICIAL + **50 new**), not just the 33 OFFICIAL MsgTypes. Selection keys on `msgcat` — the 8 `msgcat='admin'` messages (7 session types + XMLnonFIX `35=n`) are auto-excluded; of the N-002/N-003 set only BE/BF exist in FIX44 (BW/BX/BY absent). The spike's earlier "86" was a **msgtype-based** measurement (retained as the compile-cost basis only). Emission is mechanical from the existing `VersionIR` (spike-verified); the read side (`Messages.hpp` + `dict::reify`) is **already** generated for every message, so this feature is **write-side + verification only**. Coverage is selected by a new CMake `CACHE STRING` `FIXPP_CODEGEN_V44_FAMILIES=all|official` (**default `all`**; `STRINGS all official` + configure-time fatal error on any other value); OFFICIAL-only regenerates today's 33 byte-identically. Every generated builder is verified by a **differential round-trip** against the independent runtime-XML parse path, anchored by a small exemplar-per-family external-golden set. No runtime / C-ABI / Python surface change. v44 only.

## Technical Context

**Language/Version**: C++23 (Clang 22 local == CI, per Article II/III). The codegen host tool `fixpp-codegen` and the generated headers are C++23.
**Primary Dependencies**: existing `fixpp-codegen` emitter (`tools/codegen/fixpp-codegen/`), `wire::body_builder` (061), `dict::reify` (057), the runtime-XML parse path (`dict::Dictionary::as_table_view`), CMake codegen driver (`cmake/Codegen.cmake`). No new third-party dependency.
**Storage**: N/A (generated headers under `build/<preset>/_codegen/include/fixpp/v44/`; source-tree unaffected).
**Testing**: GoogleTest + ctest; a new table-driven differential round-trip harness over the emitted set; the 067 shape-oracle/roundtrip/validate/failclosed/completeness suite extended; Tier-1 sanitizer matrix via `/speckit-verify`.
**Target Platform**: Linux (clang) primary; MSVC + gcc-release exercised in CI (codegen is host-tool + headers, platform-portable).
**Project Type**: C++ library — codegen tool + generated headers + tests. No application/service surface.
**Performance Goals**: No runtime performance surface (FR-012, no runtime change). Build-cost is the governed axis: measured +19 s/TU (2.57×) for the full header, bounded by the CMake `CACHE STRING` (default `all`; opt-down `official` restores baseline) and the per-preset CI policy (research R9).
**Constraints**: 33 OFFICIAL messages byte-identical under either mode (FR-005/SC-003); no runtime/C-ABI/Python change (FR-012); v44 namespace only (FR-004); enum-domain validation out of scope (FR-013); differential verification must be non-tautological (FR-009/FR-010).
**Scale/Scope**: **83 in-scope** v44 application builders (33 existing + **50 new**), from **85 `msgcat='app'`** minus {BE, BF}. The spike's msgtype-based "86" counted XMLnonFIX + BE/BF; the authoritative msgcat-based in-scope is 83, so real cost ≤ the spike's measured cost (83 ≤ 86). Blast radius = the ~5 test TUs that include `v44/Builders.hpp` (no public header pulls it).

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-checked after Phase 1 design.*

**Appendix A trigger: Codegen layout** → this feature triggers **all four mandatory controls**: `/clarify` (DONE — Session 2026-07-11, 2 Qs), `/analyze` (pending, after `/tasks`), **Codex Gate A** (pending, after this plan), **user `/plan` sign-off** (pending). Also Article XVI §3 (`/clarify` mandatory before `/plan` for codegen) — satisfied.

| Gate | Article | Status |
|---|---|---|
| TDD red-green (VII §3/§4) | VII | PLANNED — differential harness + new-family witnesses written failing-first; completeness pin moved with the set. |
| Forced-regen + golden discipline (codegen) | VI/XVII | PLANNED — regenerate all 4 namespaces; `codegen-build-graph-check` git-cleanliness gate; 33-OFFICIAL byte-identical diff asserted. |
| Sanitizer matrix (IX) | IX | PLANNED — `/speckit-verify` Tier-1 (ASan/UBSan/TSan + debug) on the codegen consumers + differential harness. |
| No runtime/ABI/Python surface (X/FR-012) | X | PASS by construction — codegen + tests only; C-ABI stays GA-frozen 1.5.0; no `c_api.h` touch. Confirm via `capi_freeze.sha256` unchanged. |
| Fuzzing (VII §8) | VII | N/A — no new parser-touching runtime code (write emitter is a build-time host tool; the runtime parse path is unchanged). |
| Coverage (IX) | IX | PLANNED — generated builders exercised by the differential harness; lcov measured at `/implement`, not waived ex-ante. |

**⚠️ Constitution conflict — RESOLVED by folded amendment (user-approved 2026-07-11).**
Article XVIII §7 defers application-message rows **A-014..A-034 to v1.x**, and §5 forbids early-shipping post-1.0 scope into v1.0. Feature 069's new messages are the **FIX44-present `msgcat='app'` subset** of that deferred set (A-014/015/019/025 + A-016/017/020/022/026 + C-001/002 + R-/P- families); the FIX50-only rows in that range (A-018/023/027/028/029/030/031/032/033 + C-003, plus the mixed-row siblings BO/BR/BL) have **no FIX44 message** and stay **deferred to `fixpp::v50sp2`/all-version work** (NOT delivered by 069). **A-034/XMLnonFIX is `msgcat='admin'` and is NOT delivered** (auto-excluded), and A-024 stays dropped-as-duplicate. Since 067 already shipped the v1.0 OFFICIAL set, the widening inherently lands v1.x-scoped codegen, and the v1.0 tag is not yet cut. **Resolution (user decision 2026-07-11): proceed now, folding an Article XVIII §7 amendment into this feature** (Article XX §1 amend-then-proceed; precedent: Gate-A-folded amendments in features 035/043). The amendment reclassifies the v44 **application** typed codegen as delivered here, absorbing the already-pending D7 §XVIII.7 staleness reconciliation, and **explicitly dispositions §5**: reclassifying this scope as *delivered-now* removes it from the "deferred post-1.0 scope being early-shipped" category, so §5 needs no separate amendment (stated in the payload). The exact §7 replacement text, Sync Impact Report, version bump (v0.4 → v0.5), and per-catalogue-row reconciliation are in **`## Constitution Amendment Payload`** below. Gate A reviews the amendment; user signs off. Tracked in Complexity Tracking + `research.md` R7. **This is the one Constitution Check item requiring justification; all other gates pass or are planned.**

## Project Structure

### Documentation (this feature)

```text
specs/069-v44-all-families/
├── plan.md              # This file
├── research.md          # Phase 0 — R1..R8 decisions
├── data-model.md        # Phase 1 — IR msgcat field, coverage-mode, emitted-set
├── quickstart.md        # Phase 1 — build with families=all|official; run the harness
├── contracts/
│   └── coverage-and-completeness.md   # coverage-mode + emitted-set completeness contract
├── checklists/
│   └── requirements.md  # spec quality (from /specify)
└── tasks.md             # Phase 2 (/speckit-tasks — NOT created here)
```

### Source Code (repository root of the library submodule)

```text
tools/codegen/fixpp-codegen/
├── ir.hpp                # + MessageIR::is_application (or msgcat enum)
├── ir.cpp                # parse msgcat='app'|'admin' from <message> into the IR
├── emit_builders.cpp     # replace kOfficial33-only gate with coverage-mode predicate
│                         #   (official = kOfficial33 allowlist [unchanged output];
│                         #    all = is_application && !is_n002_n003_excluded)
└── main.cpp              # thread the coverage mode (CLI flag --families all|official)

cmake/
└── Codegen.cmake         # + FIXPP_CODEGEN_V44_FAMILIES CACHE STRING (default all;
                          #   STRINGS all official + FATAL_ERROR on other) → --families

tests/
├── session/
│   ├── test_067_completeness.cpp             # ← THE FR-011 emitted-set pin (touched). Today it
│   │                                         #   hardcodes `builder_registry`=={kExpectedOfficial33}
│   │                                         #   + `== 33U`; GENERALIZE to the mode's intended set
│   │                                         #   (official→33, all→83). all-mode expected set is an
│   │                                         #   INDEPENDENT raw FIX44.xml census (pugixml/grep of
│   │                                         #   `<message msgcat='app'>` minus present {BE,BF}),
│   │                                         #   asserting cardinality 83 — NOT re-derived from the
│   │                                         #   VersionIR the emitter consumes. STANDALONE (§VII.8
│   │                                         #   keeps exact-set completeness gates standalone).
│   ├── test_069_all_families_roundtrip.cpp   # NEW — differential round-trip over the full set
│   ├── test_069_family_exemplar_golden.cpp   # NEW — external-golden anchor, exemplar-per-family
│   ├── test_069_mode_count.cpp               # NEW (SC-004) — build-graph/count assertion per mode:
│   │                                         #   `all`→83 builders, `official`→33
│   └── test_067_*                            # extended where the set widens
└── codegen/
    └── test_067_emit_builders_unit.cpp       # NOT the emitted-set pin — its kOfficial33 use is the
                                              #   N3-census `visited==kOfficial33.size()` vacuous-pass
                                              #   guard (raw-pugixml walk, the census precedent reused
                                              #   above). ADD here the msgcat fail-closed witness:
                                              #   a synthetic `<message>` LACKING `msgcat` must make
                                              #   `build_ir` fail-closed (loader error, no default-
                                              #   guess), mirroring the existing synthetic-XML
                                              #   discriminating-witness pattern in this file.

spec/
├── coverage-index.md         # flip the newly-covered app-message rows (write column)
├── feature-catalogue.md      # 069 close-out rows
└── behaviors-and-limitations.md  # L-069-* (enum-domain unbacked; any nested-family limitation)

.specify/constitution.md      # Article XVIII §7 amendment (folded; Gate A + user sign-off)
```

**Structure Decision**: Reuse the 067 emitter and its test module verbatim; the only emitter change is the **selection predicate** (allowlist → msgcat-driven coverage mode) plus a **one-field IR addition** (msgcat) and a **CMake `CACHE STRING`**. No new module, no new public header, no new runtime code. Verification adds three test TUs (differential harness, exemplar golden, mode-count) alongside the existing 067 builder suite.

**Test grouping & selection (Article VII §8).** The new isolation-safe TUs — `test_069_all_families_roundtrip`, `test_069_family_exemplar_golden`, `test_069_mode_count` — **join an existing whole-binary grouped executable** in their module (per §8: new isolation-safe `.cpp` files default to a grouped executable, `gtest_discover_tests` prohibited), carrying labels `069;all_families;roundtrip`, `069;family_golden`, `069;mode_count` respectively. `test_067_completeness.cpp` (the exact-set completeness gate) **stays standalone** — §8 explicitly exempts exact-set/completeness gates from grouping. Selection is by `ctest -L <label>`, never `-R <exe-name>` (§8). The msgcat fail-closed witness added to `test_067_emit_builders_unit.cpp` inherits that TU's existing grouping/label.

## Complexity Tracking

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| Article XVIII §7 amendment (folded into 069) | 069's coverage goal lands the §7-deferred **`msgcat='app'`** subset of A-014..A-034 (A-034/XMLnonFIX is admin, NOT delivered) + C/R/P families as v1.x-scoped codegen before the v1.0 tag; the constitution must be amended before proceeding (Article XX §1). User-approved 2026-07-11. | Deferring 069 until after the v1.0 tag was offered and declined — it blocks "close legacy before Orchestra" behind the whole (not-imminent) v1.0-tag gate. Narrowing scope to avoid the deferred set was offered and declined — 067 already shipped the non-deferred set, so nothing coherent remains to widen. |
| New IR field (`is_application`/msgcat) | The emitter currently selects via a hardcoded 33-allowlist; "all app-messages minus N-002/N-003" needs the app/admin category, which the IR does not yet carry. | Hardcoding a second ~83-element allowlist was rejected — it duplicates dictionary knowledge, goes stale on dictionary revisions, and re-creates the exact drift the completeness pin exists to prevent. msgcat is authoritative and already in the XML. |

## Constitution Amendment Payload

The folded Article XVIII §7 amendment this feature carries (Article XX §1 amend-then-proceed; Gate A reviews it, user signs off). This is the concrete, copy-ready payload — the amendment is NOT delivered until this text lands in `.specify/constitution.md` on the 069 branch.

**Version bump:** **v0.4 → v0.5** — MINOR per Article XX §4 (roadmap reclassification: reclassifies already-scoped v44 application codegen from v1.x-deferred to v1.0-delivered; purely additive — no banned-pattern addition, no perf-budget tightening, no config break → not v-major).

**Sync Impact Report line** (prepend to the `<!-- Sync Impact Report … -->` block at the top of `.specify/constitution.md`):

```
Sync Impact Report — v0.4 → v0.5 (2026-07-11) — RATIFIED
  Bump: MINOR (roadmap reclassification; additive — reclassifies v44 msgcat='app' typed codegen from §XVIII.7 v1.x-deferred to v1.0-delivered-by-069; no banned-pattern/perf/config change → not v-major per Article XX §4).
  Modified principles:
    - Article XVIII §7 (Application-message codegen scope for v1.0) — the FIX44-present v44 msgcat='app' subset of A-014..A-034 (BusinessMessageReject A-014, DontKnowTrade A-015, the List family A-019, the SecurityList family A-025, plus A-016/017/020/022/026 and the C-001/002, R-, P- families) is reclassified as DELIVERED by feature 069 under fixpp::v44 — claimed message-precise: the mixed rows A-022/A-026/C-002 deliver only their FIX44 MsgTypes (AW / z,AA / AL,AM,AN,AO,AP). The FIX50-only rows A-018/023/027/028/029/030/031/032/033 + C-003 (whole rows absent from FIX44) and the mixed-row siblings BO (A-022) / BR (A-026) / BL (C-002) carry no FIX44 message and stay deferred to fixpp::v50sp2/all-version widening (NOT delivered by 069). XMLnonFIX (A-034, 35=n) stays deferred — it is msgcat='admin', outside the application-writer emitter, runtime-XML only. A-024 stays dropped-as-duplicate ([SYN §4.4]). fixpp::v42 / fixpp::v50sp2 app-message widening remains v1.x-deferred (069 is v44-only). Absorbs the pending D7 §XVIII.7 staleness (which listed the C/R families inconsistently).
  Added sections: none. Removed sections: none.
  §XVIII.5 disposition: NO amendment required — reclassifying this scope as delivered-now removes it from the "deferred post-1.0 scope being early-shipped" category, so §5's no-early-ship bar has no residual conflict once §7 is rewritten.
  Rationale: 067 shipped the v1.0 OFFICIAL v44 set; nothing coherent remains to widen without landing the §7-deferred app families, and the v1.0 tag is not yet cut. User elected proceed-now (2026-07-11).
  Templates / dependents reviewed: plan/spec/tasks templates — no change. Affected catalogue rows: coverage-index.md write-column flips for A-014/015/019/025 + A-016/017/020/022/026 + C-001/002 + R-001..005 + P-004..008 (v44-present MsgTypes only — the mixed rows A-022/A-026/C-002 flip only AW / z,AA / AL,AM,AN,AO,AP; the FIX50-only rows A-018/023/027/028/029/030/031/032/033 + C-003 and siblings BO/BR/BL are NOT flipped, no v44 row exists to flip); feature-catalogue.md 069 close-out rows.
  Process: Appendix-A codegen-trigger feature → Codex Gate A required (satisfied on the 069 branch). Rides feature 069's branch rather than a standalone `Constitution: amend §XVIII.7 — …` PR (Article XX §2 PR-title form) — a deviation from the letter of §2, recorded here (precedent: 035/043 Gate-A-folded amendments). Ratified: 2026-07-11 pending user sign-off at Gate A.
```

**Exact §XVIII.7 replacement text** (replaces the current §7 verbatim):

> 7. **Application-message codegen scope for v1.0.** v1.0's typed-message scope under `fixpp::v42`, `fixpp::v44`, `fixpp::v50sp2` is A-001..A-013 plus the M-/P-/C-/R-/N- families per the catalogue. **Under `fixpp::v44`, the full `msgcat='app'` set (83 in-scope messages = 85 app minus the N-002/N-003 pair BE/BF) is DELIVERED by feature 069** — the previously-deferred FIX44 application rows A-014, A-015, A-016/017/020, A-019, A-025, the FIX44 members of A-022 (`AW`) and A-026 (`z`, `AA`), the Collateral (C-001), Position (C-002: `AL`/`AM`/`AN`/`AO`/`AP`), Registration (R-), and post-trade (P-) families now carry typed `build_/validate_/Args` builders. **The FIX50-only rows whose only MsgTypes are absent from FIX44 — A-018 (BN), A-023 (BZ/CA), A-027 (BI/BJ/BS), A-028 (BT/BU/BV), A-029 (BK/BP), A-030 (BQ), A-031 (BM), A-032 (CB), A-033 (CC/CD/CE), C-003 (CQ) — plus the mixed-row FIX50-only siblings BO (A-022), BR (A-026), BL (C-002) are NOT delivered by 069**; they carry no FIX44 message and remain deferred to future `fixpp::v50sp2`/all-version widening. **XMLnonFIX (A-034, 35=n) is NOT delivered** — it is `msgcat='admin'`, outside the application-writer emitter; runtime-XML access via `view.get(uint16_t tag)` remains its only path. A-024 stays dropped as a duplicate per `[SYN §4.4]`. The N-002/N-003 session-FSM pair (BE UserRequest / BF UserResponse) remains deferred to the separate v1.0-tagging gate. **`fixpp::v42` and `fixpp::v50sp2` application-message widening (A-014..A-033) remains v1.x-deferred** (069 is v44-only). Runtime-XML access to any not-yet-typed application message via `view.get(uint16_t tag)` continues to ship across all 9 supported FIX versions.

**D7-staleness + C/R/P reconciliation:** the pending D7 §XVIII.7 staleness (which listed the C/R families inconsistently against the catalogue) is absorbed here — the replacement text above enumerates the C-001/002, R-001..005, P-004..008 v44 rows as delivered (C-003's only MsgType `CQ` is FIX50-only and stays deferred), superseding the stale D7 listing.

**Article XX §2 evidence path:** folded into the 069 feature branch (not a standalone amendment PR) per the 035/043 precedent; Codex Gate A on the amendment + user `/plan` sign-off are the ratification basis; the PR-title deviation from §2's `Constitution: amend §XVIII.7 — …` form is recorded in the Sync Impact Report above (not silently taken).

## Gate A

- Round 1 applied 2026-07-11: Codex P1=2 P2=6 P3=1; Opus post-judging P1=4 P2=9 P3=2; rewrite addresses root causes RC1 (mislocated completeness pin), RC2 (stale count → 83), RC3 (skipped /specify codegen obligations). Reviews: research/reviews/codex_069-v44-all-families_gate_a_review.md, research/reviews/opus_069-v44-all-families_gate_a_adversarial_review.md.
- Round 2 applied 2026-07-11: Codex P1=1 P2=0 P3=0; Opus post-judging P1=2 P2=0 P3=1; rewrite makes the amendment + Normative References message-instance precise (claim only the 50 FIX44-present new MsgTypes; defer FIX50-only rows A-018/023/027/028/029/030/031/032/033 + C-003 and mixed-row siblings BR/BO/BL to v50sp2/all-version). Reviews: research/reviews/codex_069-v44-all-families_gate_a_2_review.md, research/reviews/opus_069-v44-all-families_gate_a_2_adversarial_review.md.
