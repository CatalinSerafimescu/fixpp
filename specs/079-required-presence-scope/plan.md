# Implementation Plan: Runtime validator required-presence scoping

**Branch**: `079-required-presence-scope` | **Date**: 2026-07-18 | **Spec**: [spec.md](./spec.md) | **Issue**: fixpp#201

**Input**: Feature specification from `specs/079-required-presence-scope/spec.md`

## Summary

The runtime dictionary-driven validator's message-level required-field set is contaminated with fields that are `required='Y'` only inside an optional repeating `<group>`. The validator flat-probes that set against top-level fields, so a conforming message that legitimately omits the optional group is **false-rejected** (e.g. FIX44 PositionReport without NoUnderlyings → missing 732; FIX50SP2 TradeCaptureReport without NoSides → missing 54). The contamination originates in the two runtime loaders (`xml_loader.cpp` for the 9 QuickFIX dicts, `orchestra_loader.cpp` for vlatest). A second defect: repeating-group instances are not checked for their own required members, so a malformed instance is wrongly accepted.

**Phase-0 scope narrowing (recorded, evidence-backed)**: the issue also posited an optional-*component* over-require leg (its "fix item 3", ~6 codegen sites). A static raw-XML enumeration across all 10 dictionaries found **0 genuine optional-component over-require sites** (the 9 QuickFIX dicts have none; the 11 Orchestra hits are always-required StandardHeader/Trailer fields that must be preserved). The codegen tier also never over-required groups — `emit_builders` filters top-level items by `group_no_tag==0`. So the component/codegen leg is **vacuous (L-067-1 redux)** and dropped; the **scope** matches the candidate on `177a0535` (runtime-only + group-only). The implementation still **widens the candidate's `≤64` fail-open mask to dynamic width** per FR-004/RC5 (the candidate's `check_required = !req_members.empty() && req_members.size() <= 64` silently skips the per-instance check for groups with >64 required members — the candidate is a hypothesis, not ratified verbatim).

**Technical approach**: the fix is the candidate's group-scope loader change (an `in_group` flag stops group-member requireds entering the message-level `required_out`) + a per-group required-member store (additive `table_view`) + a per-instance required-member check in the validator's `consume_group`. This plan does **not** ratify the candidate — correctness is established independently by a non-circular census (raw-XML expected required set ≡ shipped runtime set, exact set-equality across all 10 dicts, also covering the codegen IR projection as a safety net) cross-checked against quickfix-cpp 1.16.0 (set-level oracle, 9 QuickFIX dicts), plus per-version real-frame regressions and a two-tier verdict-agreement test. The census is scope-agnostic (compares full required sets), so it would surface any component/codegen over-require as RED even though none is expected — the narrowed fix does not narrow the verification.

## Technical Context

**Language/Version**: C++23 (library); the codegen tool is C++23 host code (Article II §1 — C++23, no fallback).

**Primary Dependencies**: in-tree dictionary loaders (`XmlLoader`, `OrchestraLoader`/pugixml), the header-only wire validator (`include/fixpp/wire/validator.hpp`), the `table_view` dictionary projection, the codegen tool (`tools/codegen/fixpp-codegen/`, IR in `ir.cpp`). quickfix-cpp 1.16.0 (vendored under `reference-engines/`, gitignored) for the parity golden only — not linked in CI.

**Storage**: N/A (in-memory dictionary tables; generated headers on disk with checked-in goldens).

**Testing**: gtest + ctest. New: a non-circular census (independent raw-XML walker vs shipped required set, exact set-equality) proven RED on reintroduction; a QuickFIX parity leg (golden captured from quickfix-cpp 1.16.0, consumed offline per the 075 precedent); per-version real-frame accept/reject regressions (named + one-per-version); a two-tier verdict-agreement test.

**Target Platform**: Linux (clang/gcc) + Windows (MSVC) + libc++, per the 3-tier CI.

**Project Type**: C++ FIX protocol engine library.

**Performance Goals**: the runtime validator is a hot path (Article VIII §3; validate() measured 489–986 ns in 075). The required-set derivation and the per-group required-member store are built at dictionary-load / `as_table_view()` time; the only per-message addition is `consume_group`'s per-instance required-member scan (runs only when a group is present). Article VIII §3 is **absolute** — this hot-path change ships with a Google Benchmark in the **same PR**, unconditionally (not conditional on "if a delta is measurable", and never deferred to a MET-PARTIAL note — [[feedback_gateb_perf_change_needs_bench_not_a_metpartial_note]]). The bench MUST report before/after rows for at least: (a) a no-group message, (b) a shallow single group, (c) a nested / multi-instance group. The outcome may be "within noise", but the bench must exist and be reported in the PR.

**Constraints**: no C-ABI change (frozen 1.5.0); legacy read goldens (v44/v42/vt11) byte-identical; strict inbound validation stays opt-in/default-off; the change alters only what that path accepts/rejects.

**Scale/Scope**: 10 vendored dictionaries; group-scoped affected messages census-enumerated (PositionReport, TradeCaptureReport, Allocation, and the FIX50SP2 family the issue names). Codegen over-require sites: **0** (Phase-0 static enumeration) — no codegen change.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

- **Article VI (100% FIX Rule / spec coverage + §5 Normative References)**: PASS — with the `## Normative References` section now present in `spec.md` (Article VI §5) citing coverage-index §3 validator row (W-014, `spec/coverage-index.md:189`) + §3.2 repeating-groups row (`:184`) + the FIX required-field/componentRef AND-rule + L-067-1/L-077-1. (Article VI §5's operative reading is **spec.md-only** — the section informs *the spec*; this matches the merged precedent of 074/075/076/078, which carry it in spec.md only, and 077, which shipped with none. plan.md/research.md/data-model.md/contracts/quickstart do NOT carry a Normative References section in any merged bundle.) Corrects existing coverage (false-reject) without narrowing it; the census is the coverage evidence; feature-catalogue + coverage-index rows updated at close-out.
- **Article VII (Testing)**: PASS by construction — TDD RED→GREEN, non-circular census (exact-SET equality, not subset — [[feedback_completeness_gate_exact_set_not_subset]]), a direct regression per named invariant, tests grouped per 068.
- **Article VIII (Performance)**: GATE — validator is a hot path. Design keeps derivation at load/table-view-build time, but the per-message `consume_group` required-member scan is a hot-path change, so a Google Benchmark ships in **this PR unconditionally** (before/after rows for a no-group message, a shallow group, and a nested/multi-instance group; outcome may be "within noise"). Article VIII §3 is absolute — no "if measurable" conditionality, no MET-PARTIAL note ([[feedback_gateb_perf_change_needs_bench_not_a_metpartial_note]]).
- **Article IX (Sanitizers/Coverage)**: PASS — the per-instance required-member check (dynamic-width, not ≤64-bounded — FR-004/RC5) and any new allocation run under the ASan/UBSan/TSan matrix; coverage measured at /implement (lcov basis).
- **Article X (ABI Policy)**: PASS — no C-ABI surface touched (frozen 1.5.0); `table_view` additions are internal C++ (not the C ABI). No codegen change → all read goldens (v44/v42/vt11/v50sp2/vlatest) byte-identical.
- **Article XV (Banned patterns)**: PASS — no exceptions across parse→fromApp; the bitmask per-instance check is fail-closed (surfaces the offending tag), mirroring existing `wire_required_field_missing` disposition ([[feedback_mirror_existing_failclosed_disposition]]).
- **Article XVI/XVII (Spec-Kit / Gates)**: this feature runs the full pipeline; Gate A after this plan (before /tasks), Gate B on the PR.

**No unjustified violations** — Complexity Tracking left empty.

## Project Structure

### Documentation (this feature)

```text
specs/079-required-presence-scope/
├── plan.md              # This file
├── research.md          # Phase 0 — the group-scope loader design + the codegen-site enumeration (0 sites) that narrowed scope
├── data-model.md        # Phase 1 — required-set entities + the per-group required-member store
├── quickstart.md        # Phase 1 — how to run the census + parity + real-frame regressions
├── contracts/           # Phase 1 — the census exact-set contract + the two-tier agreement contract
└── tasks.md             # /speckit-tasks (NOT created here)
```

### Source Code (repository root)

```text
include/fixpp/
├── dict/table_view.hpp      # additive per-group required-member store (candidate) + accessors
└── wire/validator.hpp       # Step 2 top-level required probe; consume_group per-instance required check

src/dictionary/
├── xml_loader.cpp           # expand_field_list — in_group flag stops group-member requireds entering required_out (candidate)
├── orchestra_loader.cpp     # sibling projection for vlatest — same in_group flag (candidate)
└── dictionary.cpp           # as_table_view() populates the per-group required-member store

tools/codegen/fixpp-codegen/
└── ir.cpp                   # NO CHANGE (Phase-0: 0 over-require sites; top-level check already group_no_tag-filtered).
                             # Read-only in this feature — the census exercises the IR data-structure projection as a safety net.
                             # (v42 emits no builder/validator tier — main.cpp:132 `if (ir.ns != "v42")`; the IR MessageIR still exists for v42.)

tests/
├── dictionary/required_scope_test.cpp        # candidate pins (AP/AE group exclusions, D control)
├── dictionary/<census>.cpp                    # NEW non-circular census (raw-XML expected required set ≡ shipped set, all 10 dicts, incl. IR projection; RED on group reintroduction)
├── wire/validator_type_check_test.cpp         # per-instance group required reject (candidate) + real-frame accept/reject
├── wire/<parity>.cpp                           # NEW QuickFIX 1.16.0 required-set parity leg (offline golden, 9 QuickFIX dicts)
└── <two-tier agreement test>                   # runtime vs generated typed validator verdict match (guards the no-codegen-change conclusion)
```

**Structure Decision**: Single-project C++ library layout (the default). The fix spans only the dictionary tier (`src/dictionary/` + `include/fixpp/dict/`) and the wire validator (`include/fixpp/wire/`); the codegen tier is read-only (exercised by the census, not modified). Tests mirror the existing `tests/dictionary` + `tests/wire` split. No new top-level directories.

## Complexity Tracking

*No Constitution Check violations — section intentionally empty.*

## Gate A

- Round 1 applied 2026-07-18: Codex P1=3 P2=6 P3=3; Opus post-judging P1=2 P2=7 P3=6; rewrite addresses root causes RC1–RC6. No Codex findings disagreed (2 severity downgrades: P1-3→P2, P2-3→P3, already reflected in the Opus tally). Reviews: research/reviews/codex_079-required-presence-scope_gate_a_review.md, research/reviews/opus_079-required-presence-scope_gate_a_adversarial_review.md.
- Round 2 applied 2026-07-18: Codex P1=0 P2=4 P3=3; Opus post-judging P1=2 P2=1 P3=3; rewrite addresses root causes RC1–RC4 (both P1s = census-contract prose making a leg permanently RED on real dicts: the message-level actual-side prohibition, and the per-group bare-store over-correction). Reviews: research/reviews/codex_079-required-presence-scope_gate_a_2_review.md, research/reviews/opus_079-required-presence-scope_gate_a_2_adversarial_review.md.

### Round 2 — disagreements

- Codex R2 P2-3 (Normative References required in every /specify artifact) NOT applied — Opus adjudicated it a false positive: Article VI §5's operative reading is spec.md-only, matching merged precedent 074/075/076/078 (spec.md only) and 077 (none). plan.md Article VI PASS is honest; spec.md carries the section.
