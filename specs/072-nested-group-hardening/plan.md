# Implementation Plan: 072-nested-group-hardening

**Branch**: `072-nested-group-hardening` | **Date**: 2026-07-12 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/072-nested-group-hardening/spec.md`

## Summary

Bundle two latent doubly-nested-group correctness defects, both inert on shipped dicts:

- **Part A (#180, L-063-4/L-062-3)** — pin the two structural dictionary conventions the nested-group read relies on as permanent census assertions, and add a **load-time reject** at `XmlLoader::load_*` for the nested==parent delimiter collision (new typed `dict::` error deriving from `xml_parse_error`, **reusing the inherited `code()` — NO new `core::error` variant**, discrimination by catch type). The census uses a **raw per-`<group>` XML walk (parent-delimiter-threaded, `<component>`-expanded, delimiter resolved post-component-expansion)** — strictly stronger than the guard's first-seen `groups_` seam — so it covers all membership contexts across all 9 runtime dicts non-vacuously (FIX40/41/42 register no `NumInGroup` groups). Note: with component expansion for both parent-linkage and delimiter resolution, this census is real recursive work, not a threading tweak on the existing scaffold (bounded — no split-trigger; see research D-A3).
- **Part B (#183, L-065-1)** — the generated typed nested accessor re-wraps the parent membership context **unpushed** at the emitter view-mint, so a **depth-3 grandchild-group** slice queries membership one level too short → bare-fallback wrong member. Push the nested `no_tag` once, at the emitter mint (`emit_messages.cpp:270-271`), reconciling the typed path *up* to 065's already-correct C-ABI cursor; fix the validator's worse flat-context residual (L-063-3); verify by clean codegen reconfigure (no checked-in golden).

Technical approach and all anchors: [research.md](./research.md). Out of scope: L-065-2 arena fail-loud (#184, separate round).

## Technical Context

**Language/Version**: C++20/23 (project standard, Article II).
**Primary Dependencies**: pugixml (dict XML load), GoogleTest/GoogleMock (Art. VII §1); the codegen emitter (`tools/codegen/fixpp-codegen`).
**Storage**: N/A (dictionary + wire, no persistence).
**Testing**: GoogleTest; TDD red-green mandatory (Art. VII §3); isolation-safe new cases grouped, selected by `ctest -L` (Art. VII §8).
**Target Platform**: Linux/Clang (Tier 1 gate) + GCC-release sanity + MSVC (Tier 2). Coverage Linux/Clang only.
**Project Type**: C++ wire-protocol library + codegen.
**Performance Goals**: no hot-path change. The load-time guard runs once per dictionary load (not per message); no parse/dispatch allocation added (Art. XV §1).
**Constraints**: C-ABI GA-frozen at `1.5.0` — zero exported-symbol/header/enum/version delta (FR-012/SC-006). Coverage ≥95% line / ≥85% branch on touched modules (Art. IX §1).
**Scale/Scope**: 9 runtime dictionaries; the emitter change touches every depth-≥2 descent site across the 4 codegen-input dicts (build-tree output only).

## Constitution Check

*GATE: must pass before Phase 0 (passed) and re-checked post-design (below).*

| Article | Trigger | Disposition |
|---|---|---|
| XVI §3-4 | codegen / wire / error-semantics touched | `/clarify` DONE (Session 2026-07-12); `/analyze` MANDATORY before `/implement` |
| XVII §1 | wire/parser/codegen + error semantics | **Gate A MANDATORY** (this bundle) — runs after this plan, before `/tasks` |
| VII §3 | all code | TDD red-green; each fix lands behind a failing test (delimiter-reject witness, census assertions proven to trip on a mutated dict, depth-3 discrimination witness mutation-proven RED, validator witness) |
| VII §8 | new test files | delimiter-reject + census + depth-3 typed witness are isolation-safe → grouped, `ctest -L`. Any own-`main`/global-state case stays standalone |
| IX §1 | touched modules (`dict/`, `wire/`, codegen, `capi/` read-only) | ≥95/85; every uncovered error/edge path gets a recorded assessment in `.specify/decisions/072-nested-group-hardening-verify.md` |
| IX §2 | all | ASan/UBSan/TSan Tier 1 pass |
| X §1-4 | C-ABI | **No C-ABI change.** New error is a C++ `dict::` type deriving from `xml_parse_error`, **reusing the inherited `code()` — NO appended `fixpp::core::error` variant** (avoids the `error_message()` `-Wswitch`/`-Werror` break + `test_020` slot-132 flip; discrimination by catch type). Neither the frozen C-ABI `fixpp_error_t` nor `core::error` changes. ABI hygiene gate must show no delta (SC-006) |
| XV §1 | hot path | Guard is load-time, not per-message; no hot-path alloc added |
| XV §6/§13 | codegen | Fix keeps the hybrid codegen+runtime model; typed accessors still generated, still correct |

**No constitution violations. No complexity deviations to track.**

## Project Structure

### Documentation (this feature)

```text
specs/072-nested-group-hardening/
├── spec.md              # corrected post-research (depth-3, hand-built table_view, structural census)
├── plan.md              # this file
├── research.md          # Phase 0 — decisions D-A1..A5, D-B1..B6 + issue-text divergences
├── data-model.md        # Phase 1 — entities/contracts touched
├── quickstart.md        # Phase 1 — validation guide
├── contracts/
│   ├── load-guard.md     # the new load-time reject contract
│   └── typed-context.md  # the emitter push + validator contract
└── checklists/requirements.md
```

### Source Code (files this feature touches)

```text
# Part A — census + load guard
include/fixpp/dict/error.hpp            # + new typed group-delimiter-collision error (derives xml_parse_error, reuses inherited code() — NO core::error append)
src/dictionary/xml_loader.cpp           # LoaderState::finalize(): parent-chain walk + throw on nested==parent delim
tests/dictionary/reused_tag_census_test.cpp  # + FR-001 (raw walk, parent-delim-threaded + component-expanded, non-vacuous) & FR-002 assertions
# NOT touched: include/fixpp/core/error.hpp, tests/core/test_020_error_completeness.cpp (no enum append — NEW-1)
tests/dictionary/…                      # + delimiter-reject inline-XML witness (load_from_string should throw)

# Part B — typed pushed context + validator + witness
tools/codegen/fixpp-codegen/emit_messages.cpp   # :270-271 view-mint: push ctx_.group_ctx.pushed(nested_no_tag)
include/fixpp/wire/group_view.hpp        # operator[] stays verbatim (NO change) — documented invariant
include/fixpp/wire/validator.hpp         # Step-3 group walk: FROM-SCRATCH recursive nesting-aware rewrite (L-063-3, FR-010) — SPLIT-TRIGGER if unbounded
tests/codegen/nested_group_read_test.cpp # + depth-3 discrimination witness (v44::MassQuote + hand-built table_view), mutation-proven
tests/wire/… (validator)                 # + validator depth-≥2 membership witness

# Catalogue / bookkeeping
spec/behaviors-and-limitations.md        # L-063-4/L-062-3 → pinned(+guard); L-065-1/L-063-3 → fixed; residuals recorded
spec/feature-catalogue.md, coverage-index.md  # close-out at MARK DONE
```

**Note**: `emit_messages.cpp` is the ONLY generated-code change; it propagates to all 4 codegen-input dicts via configure-time regen (no checked-in golden — SC-005 verifies via clean reconfigure).

**Risk — FR-010 validator rewrite (bundling soundness, Gate-A hard gate)**: `validator.hpp` Step-3 is fundamentally flat/non-recursive (hardcoded `root_path={}` at `:204`; `seen_in_instance` heuristic at `:258-267`, not `consume_group_extent`). Making it nesting-aware is a **from-scratch recursive rewrite of unknown size**, not a push tweak — unlike the one-line FR-007 accessor fix. FR-010 now carries a concrete algorithm (recurse threading a path stack, **query-before-push**: query each candidate group's membership under the current parent path — excluding its own `no_tag` — before pushing it to descend into children) + a named mutation-proven witness (`ValidatorNestedMembership_Depth2ContextMissUnderFlatWalk`) + an explicit **SPLIT-TRIGGER**: if that rewrite proves unbounded, FR-010 splits to its own follow-up feature — 072 ships Part A + the accessor half of Part B (FR-007/FR-008/FR-011), and the L-063-3 validator residual stays "open/tracked" (NOT promoted to "fixed", FR-013). This split decision MUST be recorded here (in `## Gate A` / this section) before `/tasks`. See spec FR-010, research D-B4, contracts/typed-context.md.

## Phase 0 — Outline & Research

DONE → [research.md](./research.md). All NEEDS CLARIFICATION resolved (the one clarify question answered; all other unknowns closed by source exploration). Key issue-text divergences captured (fix site, depth-3, no-golden, census false-green, witness codegen wall).

## Phase 1 — Design & Contracts

DONE → [data-model.md](./data-model.md), [contracts/](./contracts/), [quickstart.md](./quickstart.md).

## Post-Design Constitution Re-Check

Re-evaluated after Phase 1: no new violations. The design adds one C++ error type (deriving from `xml_parse_error`, **reusing the inherited `code()` — no enum append**, no C-ABI/`core::error` delta), one load-time check, one emitter push, one nesting-aware validator rewrite (FR-010, split-trigger), and test-only census/witness code. Ready for **Gate A** (mandatory), then `/analyze` → `/tasks`.

## Gate A

- Round 1 applied 2026-07-12: Codex P1=1 P2=2 P3=1; Opus post-judging P1=1 P2=4 P3=3; rewrite addresses root causes [census-seam, lookup-key-vs-stored-ctx, enum-append-blast-radius, FR-010-incompleteness]. Reviews: research/reviews/codex_072-nested-group-hardening_gate_a_review.md, research/reviews/opus_072-nested-group-hardening_gate_a_adversarial_review.md.
- Round 2 applied 2026-07-12: Codex P1=1 P2=0 P3=1; Opus post-judging P1=1 P2=0 P3=2; rewrite fixes FR-010 query-before-push (spec/research/contract), phantom load_from_file→load, research D-A3 delimiter example. Reviews: research/reviews/codex_072-nested-group-hardening_gate_a_2_review.md, research/reviews/opus_072-nested-group-hardening_gate_a_2_adversarial_review.md.
- Round 3 applied 2026-07-12: Codex P1=P2=P3=0; Opus post-judging P1=0 P2=1 P3=0 — design CONVERGED, sole P2 was the unrecorded FR-010 SPLIT-TRIGGER decision (a one-line bookkeeping completion, not a redesign). Recorded below → the hard-gate condition is now DISCHARGED. Reviews: research/reviews/codex_072-nested-group-hardening_gate_a_3_review.md, research/reviews/opus_072-nested-group-hardening_gate_a_3_adversarial_review.md.

### Round 1 — hard-gate condition (FR-010)

FR-010 now carries a concrete algorithm + named mutation-proven witness + SPLIT-TRIGGER (see the Project Structure FR-010 risk note, spec FR-010, research D-B4). The split decision (does the validator rewrite ride 072 or move to its own feature?) MUST be recorded here before `/tasks` — **DISCHARGED by the round-3 decision below**.

- **SPLIT-TRIGGER decision (round 3, user-signed-off 2026-07-12): FR-010 RIDES 072.** The split-trigger is the **`/implement`-time escape** — if, during `/implement`, the nesting-aware validator rewrite proves unbounded vs the Part B accessor fix (FR-007), FR-010 splits to its own follow-up feature **at that point** (072 then ships Part A + FR-007/FR-008/FR-011), and L-063-3's "fixed" promotion (FR-013) is **withheld** until it actually ships. Provisional-then-escape, not split-now.

### Round 1 — disagreements

None — all Codex + Opus findings accepted; every anchor re-verified against on-disk source at branch `072-nested-group-hardening` (error hierarchy non-virtual `code()`, `test_020` slot-132 pin, `groups_` first-seen dedup, `walk_groups` no parent-linkage, `group_member_tags` separate `no_tag`, validator `root_path={}`, FIX44 MassQuote `msgtype='i'`, `295→555` via component ref).
