# Implementation Plan: Strict-Validation-Path Residual Closeout

**Branch**: `081-strict-validation-residuals` | **Date**: 2026-07-19 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/081-strict-validation-residuals/spec.md`

## Summary

Two correctness fixes on the opt-in runtime dictionary-driven strict inbound-validation path (`SessionConfig::validate_inbound_messages`; default off = byte-identical no-op), both residuals of feature 079 (fixpp#201):

- **Concern A (#203 / L-041-2)** — FIX50/FIX50SP1/FIX50SP2 application frames are rejected on the first standard-header tag because those dictionaries ship an empty `<header/>` (the FIXT.1.1 session layer owns 8/9/34/49/52/56 and trailer 10). Fix: in `Dictionary::as_table_view()` for the three empty-header versions, populate a **validator-private FIXT framing surface** (`fixt_framing_tags_` + a `fixt_framing_types_` tag→datatype map), consulted only by the validator's Step-1 accept and type-check arms, so `validate()` accepts the framing tags (accept-only — no new header-required enforcement) AND type-checks them (malformed numeric header rejected). The **shared `valid_` store / `field_valid_for` / `valid_tags_for` (which the inbound parser reads) stay byte-identical** — so **zero golden change and no parser behavior change**, whether strict validation is on or off. (Gate A round-1 moved the merge target off the shared store; see `## Gate A`.)
- **Concern B (#205)** — adopt QuickFIX group-gating: a `required='Y'` direct member of an **optional** group is not required per-instance. Fix: **thread the enclosing group's own `required=` down into member-record time** in both loaders (currently computed one frame too high and dropped) so optional-group members never enter the per-group required store; mirror the same gate in the codegen emitter and the census oracle. `consume_group` needs no change — it consumes whatever the store holds.

Both are pure C++ library changes: runtime validator view + two dictionary loaders + one codegen emitter + regenerated typed-validator goldens (v44/v50sp2/vlatest) + tests. No C-ABI change; no read/reify golden change; no wire/encode/parse change.

## Technical Context

**Language/Version**: C++23 (project standard, Article II).

**Primary Dependencies**: pugixml (XML/Orchestra loaders); GoogleTest/GoogleMock (tests, grouped by ctest label per Article VII §8); Google Benchmark (`bench/wire/validator_bench.cpp`, pre-existing); quickfix-cpp 1.16.0 (offline parity-golden generation only — not linked in CI).

**Storage**: N/A. Dictionaries are vendored XML (`dictionaries/*.xml`); `dictionaries/FIXT11.xml` is the source of truth for the FIXT.1.1 header/trailer field set (Concern A).

**Testing**: GoogleTest RED→GREEN behavior pins + a non-circular raw-XML census (QuickFIX immediate-enclosing group-gating required sets, computed independently from raw XML, exact-set-equal against loaded tables) + a quickfix-cpp 1.16.0 required-set parity golden that corroborates (not defines) the rule — the 079 tooling, updated. No Python change (bindings untouched).

**Target Platform**: Linux/Clang (Tier-1: Debug+Release, ASan/UBSan/TSan, coverage, static-analysis); Linux/GCC Release; Windows/MSVC (Tier-2).

**Project Type**: C++ FIX-engine library.

**Performance Goals**: No intended perf change. Concern A's framing-surface population runs at **validation-view construction (`as_table_view()`), at setup time — up to twice per session** (`session.cpp:992` unconditional for the parser's `inbound_tv_`, which computes-but-ignores the framing surface, and `session.cpp:1234` for the strict validator when enabled), never per `validate()`. It is a bounded setup-time addition at those two construction sites. Concern B **removes** per-instance required checks at 24 contexts (never adds work) and only re-shapes load-time accumulation. `validate()` hot-path cost is unchanged-or-lower; the existing `bench/wire/validator_bench.cpp` stays valid and its baseline is not expected to move beyond ±5%. (Article VIII §3 — no perf-baseline update needed because there is no intended `validate()` hot-path perf delta; the plan pins the setup-time placement that guarantees this.)

**Constraints**:
- **No C-ABI change** — frozen `1.5.0`; `abidiff` clean (0 diff), `nm` symbol golden byte-identical, no `capi/`/`error.h`/`version.h` edit (FR-008). Article X's four ABI controls are **not** triggered (this is not an ABI-affecting feature); the plan still *proves* no ABI change via the existing gate.
- **All read/reify goldens byte-identical** (v42/v44/v50sp2/vt11/vlatest) — FR-009. Guaranteed for Concern A by placing the merge in `as_table_view()` (no golden consumer reads it) and for Concern B by the read `group_view`/membership being independent of required-ness.
- **Opt-in only** — with `validate_inbound_messages` off (default), behavior is a byte-identical no-op (FR-010).
- **No false-accept** — Article VI / QuickFIX-parity: fixpp only relaxes to QuickFIX-exact (Concern B) and only *accepts* previously-rejected header tags (Concern A); neither weakens an existing check.

**Scale/Scope**: 1 runtime view builder (`dictionary.cpp as_table_view`), 2 loaders (`xml_loader.cpp`, `orchestra_loader.cpp`), 1 codegen emitter (`emit_builders.cpp`), 1 census oracle (`required_scope_oracle.hpp`), regenerated typed-validator goldens under `specs/078-precompiled-builder-libs/contracts/golden/{v44,v50sp2,vlatest}/`, and ~6 existing test files updated + new RED→GREEN pins.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Article | Gate | Status |
|---|---|---|
| **I — Identity & Mission** | No FIX version-set change; correctness of existing versions only. | ✅ PASS — no `session_version` additions. |
| **VI — 100% FIX / no silent omissions** | No new OFFICIAL rows; fixes bring the strict validator into QuickFIX/FIX conformance. Traceability via existing rows + B&L L-rows (L-041-2 resolved; #205 waiver W-204-1 superseded). No false-accept. | ✅ PASS — catalogue: no new rows; B&L updates only (see research.md D-6). |
| **VII — Testing (TDD, grouped tests, interop)** | RED→GREEN pins first; census + quickfix-cpp parity golden; isolation-safe tests grouped by ctest label; exact-set completeness gates stay standalone. | ✅ PASS — test plan in quickstart.md. |
| **VIII — Perf budgets** | Is this a hot-path perf change? | ✅ PASS — **not** a `validate()` hot-path perf change: Concern A populates the framing surface at setup-time view construction (up to twice/session, never per-`validate()`); Concern B removes checks. No baseline update; existing `validator_bench` remains valid (placement pinned in research.md D-1). |
| **IX — Coverage / sanitizers / ABI** | ≥95% line / ≥85% branch on touched modules; ASan/UBSan/TSan; abidiff clean. | ✅ PASS — touched modules `dictionary`/`wire` + codegen tool; **abidiff MUST be 0-diff** (asserted at verify). |
| **X — ABI Policy** | ABI-affecting? | ✅ PASS — **not** ABI-affecting; no `c_api.h`/`error.h`/`version.h` edit; symbol golden unchanged. The four ABI controls are not triggered. |
| **XVI/XVII — Spec-Kit + Gates** | /clarify done; /analyze + checklist-audit before /implement; Gate A after /plan; Gate B before merge. | ✅ ON TRACK. |

No violations → Complexity Tracking table omitted.

## Project Structure

### Documentation (this feature)

```text
specs/081-strict-validation-residuals/
├── plan.md              # This file
├── spec.md              # Feature spec (+ Clarifications)
├── research.md          # Phase 0 — design decisions (merge site/source, group-gating threading, codegen gating, golden blast radius)
├── data-model.md        # Phase 1 — the validation-view framing merge + per-group required store shape
├── quickstart.md        # Phase 1 — runnable RED→GREEN + census + parity validation scenarios
├── contracts/
│   ├── validation-acceptance.md   # Concern A observable contract (accept FIXT header/trailer; no new required enforcement)
│   └── census-and-parity.md       # Concern B census + quickfix-cpp parity contract (group-gating exact-set)
└── checklists/
    └── requirements.md  # Spec quality checklist (from /specify)
```

### Source Code (repository root = library submodule)

```text
include/fixpp/
├── wire/validator.hpp            # Step-1 valid_tags check (:170) + `is_fixt_framing_tag` accept; check_field_type/field_type_of (:183/:467) framing-type resolve — CONCERN A wiring; consume_group (:264) — READ (no change, Concern B)
└── dict/table_view.hpp           # valid_tags_for (:273) UNCHANGED; NEW validator-private fixt_framing_tags_ set + fixt_framing_types_ map + accessors — CONCERN A; per-group required store accessors — CONCERN B

src/dictionary/
├── dictionary.cpp                # as_table_view() (:358) — CONCERN A: populate fixt_framing_tags_/fixt_framing_types_ (~:376-382), leave valid_ store byte-identical;
│                                 #   per-group required store population (:416-418 bare, :475-522 ctx) — CONCERN B store
├── xml_loader.cpp                # expand_field_list (:522); group member record (:557-559); group greq at <group> branch;
│                                 #   version detection kVersionTable (:157-188) — CONCERN A gate + CONCERN B threading
└── orchestra_loader.cpp          # expand_field_list (:509); group member record (:545-547); greq (:592) — CONCERN B threading

tools/codegen/fixpp-codegen/
└── emit_builders.cpp             # compute_signature (:251-282) + intern (:648-649) — CONCERN B: fork group-plan identity by enclosing-group-required; emit_writer_traits_for_level (:723) then emits per-fork required_checks. item.group_required (:656) / item.required (:683)

tests/dictionary/
├── required_scope_oracle.hpp     # census oracle — rework group_scope_and -> group-gated (Concern B)
├── required_scope_test.cpp       # AP 732/733 pins flip (Concern B)
├── required_scope_census_test.cpp# per-context census + RC5 baseline recount (Concern B)
└── (new) fixt_header_merge_*     # Concern A: standalone FIX50SPx accept + FIXT framing-set == FIXT11.xml census

tests/wire/
├── required_scope_two_tier_test.cpp   # optional-group per-instance pins flip; required-group pins stay (Concern B)
├── required_scope_parity_test.cpp     # quickfix-cpp parity golden (Concern B) — regen
└── (new) fixt_header_validate_*       # Concern A: RED->GREEN standalone validate() accept

specs/078-precompiled-builder-libs/contracts/golden/{v44,v50sp2,vlatest}/   # regenerated typed-VALIDATOR goldens (Concern B)
```

**Structure Decision**: Single-project C++ library layout (existing). Concern A is isolated to the runtime validation view (`dictionary.cpp`) + a FIXT framing source; Concern B spans the two loaders + emitter + oracle in lockstep (the store, the typed emitter, and the oracle must move together, or the census/two-tier tests diverge — mirror-fix symmetric surfaces in one pass).

## Complexity Tracking

No constitution violations — table omitted.

## Gate A

- Round 1 applied 2026-07-19: Codex P1=4 P2=1 P3=2; Opus post-judging P1=4 P2=2 P3=4; rewrite addresses RC1 (validator-private framing surface, not shared valid_ store) + RC2 (fork group-plan identity by enclosing-required) + N1 (immediate-enclosing gating, non-circular oracle) + Normative References + perf-wording. Reviews: research/reviews/codex_081-strict-validation-residuals_gate_a_review.md, research/reviews/opus_081-strict-validation-residuals_gate_a_adversarial_review.md.
  - **Design refinement (not a `/clarify` reversal):** the `/clarify` decision "merge the FIXT.1.1 header/trailer into the validation view; single pass against the merged view" is preserved at the observable level. Only the merge **target** moved — from the parser-shared `valid_` store (which `table_view::field_valid_for` feeds into the inbound parser's `unknown_fields()` classification, `parser.hpp:582-584`, changing behavior even with strict validation off) to a **validator-private framing surface** (`fixt_framing_tags_` + `fixt_framing_types_`) read only by the validator. The validation view still merges the framing at load time; the parser-shared store is now provably untouched.
  - RC2: Concern B's typed tier forks the group-plan interning identity by effective enclosing-group-required-ness (the "thread a flag into the shared trait" design was structurally incoherent — the shared `writer_traits<G_X>::required_checks` cannot serve both a required and an optional usage); the "gate at the parent's `validate_entry_` call site" alternative is overruled (it drops nested-group recursion). No `builder_validate.hpp` change; read/reify goldens byte-identical.
  - N1: the group-gating semantic (immediate-enclosing group's own `required=`, not ancestor-AND) is decided at design time from QuickFIX `addXMLGroup`; the census oracle encodes it independently from raw XML, the parity golden corroborates (non-circular).
- Round 2 applied 2026-07-19: Codex P1=0 P2=1 P3=1; Opus post-judging P1=0 P2=2 P3=1; rewrite strengthens the parser-containment pin (direct field_valid_for invariant, not blind on/off compare), corrects the false 8/9/10 "framer-consumed" wording (tag 8 is the Step-1 reject site per validator_production_table_view_test.cpp:270; census mandates 8/9/10 in the framing set), and pins exact malformed-header error+ref_tag. All four round-1 P1 design fixes verified sound. Reviews: research/reviews/codex_081-strict-validation-residuals_gate_a_2_review.md, research/reviews/opus_081-strict-validation-residuals_gate_a_2_adversarial_review.md.
