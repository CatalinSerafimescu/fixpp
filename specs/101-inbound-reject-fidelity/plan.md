# Implementation Plan: Inbound Reject fidelity

**Branch**: `101-inbound-reject-fidelity` | **Date**: 2026-11-07 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/101-inbound-reject-fidelity/spec.md`

## Summary

A validating session answers an undefined MsgType with 373=11 and an undefined tag with 373=0, and every
validation-gate Reject carries QuickFIX C++'s reason text (B85, owner-ruled). A frame has one MsgType, its first 35,
for every reader; a frame with any later differing 35 is refused on every validating session (Reject with QuickFIX C++'s
13/14 precedence after establishment, a disconnect in it), disconnected in establishment on any session, and handled as
its first 35 otherwise (B88). Every inbound-fed Reject echoes the MsgType verbatim and is sent without 372 instead of not
at all when the echo does not fit; 092's echo bound retires (B82, QuickFIX parity). Decisions D1–D10 and the merged
clauses this supersedes, by name: [research.md](research.md). Endings: [contracts/reject-frames.md](contracts/reject-frames.md).

## Technical Context

**Language/Version**: C++23 (clang, gcc, MSVC).

**Primary Dependencies**: none new.

**Storage**: N/A.

**Testing**: GoogleTest, through real `Session` objects for every ending (099's lesson: validator-level probes move a
defect up one layer); RED on `fa778a8e` per cell; a mutation per production change; the census by research D9's
recipes.

**Target Platform**: Linux and Windows.

**Project Type**: C++ library: session, wire validator, dictionary table view.

**Performance Goals**: no slowdown above `[const §VIII.2]`'s budget (D8): one comparison per later 35 in the scan; one hot
`valid_.find` per validated message, as today; one cold `tag_defined` lookup on a field that has already failed.

**Constraints**: no per-message allocation (`[const §XV.1]`); `noexcept` preserved; the C-ABI code set unchanged; 099's
two-occurrence FIXT endings in LogonReceived/Active, its validating FIXT establishment disconnect and its 1128 refusals
kept — the clauses that do change are named in research's Supersessions table.

**Scale/Scope**: production `src/session/scan_frame_header.hpp`, `src/session/peek_fixt_route.hpp`
(`fixt_header_repeats_msg_type`), `src/session/admin_messages.cpp` (`interpret_logon`), `src/session/session.cpp` (gate
sites, `refuse_active_frame_`, `establishment_frame_refused_`, `on_inbound_logout_sent_`, the Reject build helper and its
every caller D7's census recipe finds, `dispose_unparseable_`), `include/fixpp/wire/validator.hpp`, `include/fixpp/wire/reject_reason_map.hpp`,
`include/fixpp/dict/table_view.hpp` (+ its builder), `src/dictionary/dictionary.cpp` (`Dictionary::build_table_view`),
`include/fixpp/core/error.hpp`, `src/capi/error.cpp`. Tests in `tests/session/`, `tests/wire/`, `tests/core/`,
`tests/capi/`, `tests/fuzz/`, `tests/interop/happy/`. Bench `bench/dictionary/table_view_footprint_bench.cpp`.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Article | Gate | Disposition |
|---|---|---|
| I / VI | core tag-value FIX, spec coverage | PASS: spec `## Normative References` lists the coverage-index entries (`[const §VI.5]`); FIX-TC 2q, 14a, 14c endings; QuickFIX parity per the owner's rule, deviations disclosed (FR-009). |
| VII | RED first, mutation-proven | PASS planned (research D9 and tasks). |
| VIII | no regression | D8: Article VIII §2's protocol (A-B-A-B, min-per-tree, ≤ +5%, base = merge-base at measurement time) for `scan_frame_header_bench`, `on_inbound_frame_bench` and `validator_bench`; CI pairing supplementary. |
| IX | coverage, sanitizers, static | `/speckit-verify`. |
| X | C ABI | PASS: two `core::error` slots map to the existing `FIXPP_ERR_WIRE_CONFORMANCE`; no code, symbol or struct added, so no version step (`[const §X.1]`, `[const §X.4]`; Codex and Opus agree). `expected_error_map.csv` and the completeness pins grow (D9). |
| XV.1 | no per-message heap | PASS: the defined-tag store is built and sealed with the table; the D7 retry reuses the stack buffer. |
| XVII.1 | Gate A on public behaviour | Gate A required (session wire endings, public `core::error`, public `table_view`, public `interpret_logon`). |

**Initial gate: PASS. Post-design re-check: PASS.**

## Project Structure

### Documentation (this feature)

```text
specs/101-inbound-reject-fidelity/
├── plan.md
├── research.md
├── data-model.md
├── quickstart.md
├── contracts/
│   └── reject-frames.md
├── checklists/
│   └── requirements.md
└── tasks.md            # /speckit-tasks
```

### Source Code (library root)

```text
include/fixpp/dict/table_view.hpp       # tag_defined, valid_tag_set_view::found, mark_defined (+ builder)
src/dictionary/dictionary.cpp           # Dictionary::build_table_view fills the defined-tag store
include/fixpp/wire/validator.hpp        # D2 MsgType check on the shared lookup; undefined vs not-in-message split
include/fixpp/wire/reject_reason_map.hpp# 11 and 0 for the new slots
include/fixpp/core/error.hpp            # wire_invalid_msg_type, wire_undefined_tag
src/capi/error.cpp                      # both → FIXPP_ERR_WIRE_CONFORMANCE
src/session/scan_frame_header.hpp       # D4 first-wins + msg_type_conflict; kMaxShippedMsgTypeLength deleted
src/session/peek_fixt_route.hpp         # D5 walk past the header stop: in-header / past-header
src/session/admin_messages.cpp          # D4 interpret_logon first-wins
src/session/session.cpp                 # D3 texts; D5/D6 refusal conditions; LogoutSent; D7 helper at every inbound-fed builder
bench/dictionary/                       # D8 footprint rows
tests/session/                          # US1-US4 through a real Session; rewritten 092/098/099 cells
tests/wire/                             # validator cells per slot; D1 population mutations
tests/core/                             # sentinels; test_101_error_completeness.cpp
tests/capi/                             # expected_error_map.csv, error_surface_test, abi_symbol_golden_test
tests/fuzz/                             # is_valid_wire_error
tests/interop/happy/golden/             # regenerated reject-invalid-admin goldens
```

**Structure Decision**: existing layout; new test files join existing binaries.

## Phases (for /speckit-tasks)

1. **Census + RED** — run research D9's recipes at the branch head and write the manifest into `tasks.md`; one
   Session-level cell per acceptance scenario on today's tree; validator cells for D2.
2. **Dictionary + validator** — D1, D2; slots and C mapping; completeness pins (D9).
3. **Texts** — D3 at the three gate sites; rewrite the cells and goldens recipe 1 finds.
4. **One MsgType** — D4, D5, D6, LogoutSent; rewrite the 092 and 099 cells recipes 2–3 find, each naming its
   superseding FR and the superseded clause; header comments naming the supersessions (D10).
5. **Echo** — D7's helper at every inbound-fed builder its census recipe finds; retire 092's bound.
6. **Mutations, benches** — each change reverted alone, its cells RED; D8's paired runs and footprint rows.
7. **Records** — B&L (L-452-2 closed; B-099-4's "Not validating" bullet, B-099-5, B-099-11 rewritten; FR-009's new
   B-rows), catalogue/coverage index; false comments (D10) fixed, `check-comment-claims.py` on the diff.
8. **Verify** — re-derive the census after the last fix commit; targeted ctest, sanitizers on `session_pure_tests`,
   MSVC build of touched targets, `/speckit-verify`.

## Complexity Tracking

| Item | Why | Simpler alternative rejected because |
|---|---|---|
| D7's retry without 372 | a long MsgType made the Reject vanish | a fixed bound needs a number derived from the other fields' lengths, and 092's bound already went stale at 098 |
| D4 widens 099's `last ≠ third` to "any later ≠ first" | one MsgType per frame | keeping the last-value comparison leaves `D…A…D` handled as D with a hidden A |
| D5 walks past the header stop on the cold path | QuickFIX C++'s 14-before-13 precedence with three or more 35s | reusing the stop-at-header walk answers 13 where C++ answers 14 |

## Open for Gate A — resolved at round 1

1. D6's validation-off ending after Logon: approved by both reviewers; "handled as X" is now defined by D6's
   differential oracle.
2. D2's 373=0 inside a group: approved, and traced to QuickFIX C++ on 098's AS-2 layout; the other layouts' divergence
   is a B-row.
3. D7's omit-372-on-overflow: approved as policy; the site census is now D7's recipe (every inbound-fed builder), the retry condition is
   `wire_field_value_truncated`, and 092's bound retires.
4. Article X: no C-ABI version step is owed (both reviewers).

## Gate A

- Round 1 applied 2026-11-07: Codex P1=3 P2=5 P3=1; Opus post-judging P1=2 P2=9 P3=9; rewrite addresses root causes #1 (one RefMsgType echo owner at every inbound-fed builder, 092 bound retired), #2 (one MsgType as a contract naming every clause it supersedes, LogoutSent rows, 13/14 precedence, differential oracle), #3 (command-derived census and slot plan), #4 (Article VIII §2 protocol, one hot lookup), #5 (defined-tag store shape and population rule), #6 (records and spec hygiene: Normative References, Key Entities, FR-009 B-rows, Go not read for #646). Reviews: research/reviews/codex_101-inbound-reject-fidelity_gate_a_review.md, research/reviews/opus_101-inbound-reject-fidelity_gate_a_adversarial_review.md.

### Round 1 — disagreements

- **Codex 4 (P2), D5's header oracle on non-FIXT sessions — not applied.** Codex would make a non-FIXT session's header
  boundary the session dictionary's header only, so that `…52=…|1128=9|35=D` on FIX.4.4 answers 14. QuickFIX C++
  classifies header fields by `Message::isHeaderField`: its static list (1128, 1129, 627, 369, 370, the Sub/Location
  IDs, …) **or** the session dictionary's header, on every BeginString, so it keeps that 35 in the header and answers 13.
  `walk_fixt_header`'s union of `table_view::is_header_tag` and `dict::is_fixt_standard_header_tag` is the closer model;
  Codex's fix moves away from parity. The residual (tags `is_fixt_standard_header_tag` holds and QuickFIX C++'s static
  list lacks) is disclosed (research D5, FR-009). Opus judged this a disagreement; this rewrite follows Opus.

### Round 1 — refinements of Opus's fix shapes

- **Root cause #2, the 13/14 walk's hooks.** Opus's shape walks the whole frame with S's hooks. Past the header stop the
  rewrite reads with the route's hooks instead (identical on a non-FIXT session): on a FIXT application route, S's hooks
  would split a body Data value the route declares, and one embedding `SOH 35=` would show a phantom past-header 35
  (research D5).
- **Root cause #1, the BusinessMessageReject.** Opus's shape applies the omit-372 retry to the BMR twin, under the
  rationale that 372 is optional. That holds for Reject (35=3); in BusinessMessageReject (35=j) 372 is required in the
  vendored dictionaries. The BMR ending is kept as Opus shaped it but marked provisional and raised for a ruling (spec
  Clarifications, Gate A round 1; research D7).
- **Root cause #4, the shared lookup.** Implemented as `valid_tag_set_view::found()` on the view `valid_tags_for`
  already returns, instead of a public `has_msg_type`, so no second query is added that nothing on the hot path would
  call (research D2).
