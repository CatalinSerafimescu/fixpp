# Implementation Plan: 072-nested-group-hardening

**Branch**: `072-nested-group-hardening` | **Date**: 2026-07-12 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/072-nested-group-hardening/spec.md`

## Summary

Bundle two latent doubly-nested-group correctness defects, both inert on shipped dicts:

- **Part A (#180, L-063-4/L-062-3)** — pin the two structural dictionary conventions the nested-group read relies on as permanent census assertions, and add a **load-time reject** at `XmlLoader::load_*` for the nested==parent delimiter collision (new typed `dict::` error deriving from `xml_parse_error`). The census uses the **structural** loader group table so it covers all 9 runtime dicts non-vacuously (FIX40/41/42 register no `NumInGroup` groups).
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
| X §1-4 | C-ABI | **No C-ABI change.** New error is a C++ `dict::` type + appended `fixpp::core::error` variant (not the frozen C-ABI `fixpp_error_t`). ABI hygiene gate must show no delta (SC-006) |
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
include/fixpp/dict/error.hpp            # + new typed group-delimiter-collision error (derives xml_parse_error)
include/fixpp/core/error.hpp            # + appended error enum variant for its code()
src/dictionary/xml_loader.cpp           # LoaderState::finalize(): parent-chain walk + throw on nested==parent delim
tests/dictionary/reused_tag_census_test.cpp  # + FR-001 (structural, non-vacuous) & FR-002 assertions
tests/dictionary/…                      # + delimiter-reject inline-XML witness (load_from_string should throw)

# Part B — typed pushed context + validator + witness
tools/codegen/fixpp-codegen/emit_messages.cpp   # :270-271 view-mint: push ctx_.group_ctx.pushed(nested_no_tag)
include/fixpp/wire/group_view.hpp        # operator[] stays verbatim (NO change) — documented invariant
include/fixpp/wire/validator.hpp         # Step-3 group walk: nesting-aware pushed context (L-063-3)
tests/codegen/nested_group_read_test.cpp # + depth-3 discrimination witness (v44::MassQuote + hand-built table_view), mutation-proven
tests/wire/… (validator)                 # + validator depth-≥2 membership witness

# Catalogue / bookkeeping
spec/behaviors-and-limitations.md        # L-063-4/L-062-3 → pinned(+guard); L-065-1/L-063-3 → fixed; residuals recorded
spec/feature-catalogue.md, coverage-index.md  # close-out at MARK DONE
```

**Note**: `emit_messages.cpp` is the ONLY generated-code change; it propagates to all 4 codegen-input dicts via configure-time regen (no checked-in golden — SC-005 verifies via clean reconfigure).

## Phase 0 — Outline & Research

DONE → [research.md](./research.md). All NEEDS CLARIFICATION resolved (the one clarify question answered; all other unknowns closed by source exploration). Key issue-text divergences captured (fix site, depth-3, no-golden, census false-green, witness codegen wall).

## Phase 1 — Design & Contracts

DONE → [data-model.md](./data-model.md), [contracts/](./contracts/), [quickstart.md](./quickstart.md).

## Post-Design Constitution Re-Check

Re-evaluated after Phase 1: no new violations. The design adds one C++ error type + one appended enum variant (no C-ABI delta), one load-time check, one emitter push, one validator walk, and test-only census/witness code. Ready for **Gate A** (mandatory), then `/analyze` → `/tasks`.
