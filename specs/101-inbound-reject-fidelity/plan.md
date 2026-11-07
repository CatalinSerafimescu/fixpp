# Implementation Plan: Inbound Reject fidelity

**Branch**: `101-inbound-reject-fidelity` | **Date**: 2026-11-07 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/101-inbound-reject-fidelity/spec.md`

## Summary

A validating session answers an undefined MsgType with 373=11 and an undefined tag with 373=0, and every
validation-gate Reject carries QuickFIX C++'s reason text (B85, owner-ruled). A frame has one MsgType, its first 35,
for every reader; a frame with any later differing 35 is refused on every validating session (Reject with QuickFIX C++'s
13/14 precedence after establishment, a disconnect in it), disconnected in establishment on any session, and handled as
its first 35 otherwise (B88). Every inbound-fed Reject and BusinessMessageReject echoes the MsgType verbatim whenever
it fits the engine's outbound ceiling `kSendFrameBytes`, rebuilt into a session-owned slot (no allocation) when the
site's own buffer overflows;
above the ceiling the Reject is sent without 372 and the BMR is not sent, with a session log record. 092's echo bound
retires (B82, QuickFIX parity up to the ceiling). Decisions D1–D10 and the merged
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
every caller D7's census recipe finds, the slot-rebuild helper, `dispose_unparseable_`), `src/log/format_registry.cpp` (D7's
record), `include/fixpp/session/session.hpp` (`inbound_reply_slot_`, `inbound_reply_slot_held_`, the record's rate state, and
`on_inbound_frame`'s serialized-caller sentence),
`include/fixpp/wire/validator.hpp`, `include/fixpp/wire/reject_reason_map.hpp`,
`include/fixpp/dict/table_view.hpp` (+ its builder), `src/dictionary/dictionary.cpp` (`Dictionary::build_table_view`),
`include/fixpp/core/error.hpp`, `src/capi/error.cpp`. Tests in `tests/session/`, `tests/wire/`, `tests/core/`,
`tests/capi/`, `tests/fuzz/`, `tests/interop/happy/`. Bench `bench/dictionary/table_view_footprint_bench.cpp`.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Article | Gate | Disposition |
|---|---|---|
| I / VI | core tag-value FIX, spec coverage | PASS: spec `## Normative References` lists the coverage-index entries (`[const §VI.5]`); FIX-TC 2q, 14a, 14c endings; QuickFIX parity per the owner's rule, deviations disclosed (FR-009). |
| VII | RED first, mutation-proven | PASS planned (research D9 and tasks). |
| VII.7 | fuzzing on parser-touching code | PASS planned: tasks T015 (seeds), T050 (the scan and D5-walk oracle) and T071 (ten minutes each of `fuzz_wire_validator` and `fuzz_session_recovery_admin_parse`). |
| VII.8 | grouped tests, selected by `ctest -L <label>` | PASS with the 099-precedent exception: the new Session cells go in a new grouped entry `session_101_tests` (`session_pure_tests` carries `TIMEOUT 120`); `error_101_completeness` (an exact-set completeness gate) and `session_101_reply_slot_death_test` (an `abort()` path) are standalone; feature tests are selected by `-L 101`. |
| XI.7 / Appendix A | four mandatory controls | PASS: the triggers are a validator change, session FSM endings and the serialized-caller precondition; the controls are the clarify sessions (spec `## Clarifications`), this `/speckit-analyze`, Codex Gate A (`## Gate A`) and the owner's sign-off (`decisions/speckit/101-inbound-reject-fidelity-gatea.md`, parent repo). |
| VIII | no regression | D8: Article VIII §2's protocol (A-B-A-B, min-per-tree, ≤ +5%, base = merge-base at measurement time) for `scan_frame_header_bench`, `on_inbound_frame_bench`, `validator_bench` and `table_view_footprint_bench`'s timed Build/Copy rows (D1 changes what they time); CI pairing supplementary; the footprint binary stays unpaired. |
| IX | coverage, sanitizers, static | `/speckit-verify`. |
| X | C ABI | PASS: two `core::error` slots map to the existing `FIXPP_ERR_WIRE_CONFORMANCE`; no code, symbol or struct added, so no version step (`[const §X.1]`, `[const §X.4]`; Codex and Opus agree). `expected_error_map.csv` and the completeness pins grow (D9). |
| XV.1 | no per-message heap | PASS: the defined-tag store is built and sealed with the table; D7's overflowed replies rebuild into a session-owned `kSendFrameBytes` slot (allocated with the Session) with a non-coroutine helper and run the site's existing tail, so no reply allocates. The slot's exclusivity rests on `on_inbound_frame`'s serialized-caller precondition, which the Engine already honours, stated on the header and asserted in debug builds (research D7); witnessed by run-thread allocation cells with a RED arm (D9). |
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
src/session/session.cpp                 # D3 texts; D5/D6 refusal conditions; LogoutSent; D7 helper + slot rebuild at every inbound-fed builder; FIXPP_ASSERT_INBOUND_REPLY_SLOT_FREE
include/fixpp/session/session.hpp      # D7 inbound reply slot + held flag; record rate state; on_inbound_frame precondition
src/log/format_registry.cpp             # D7's outbound-ceiling record (its CRC-collision static_assert stays green)
bench/dictionary/                       # D8 footprint rows
tests/session/                          # US1-US4 through a real Session; rewritten 092/098/099 cells
tests/wire/                             # validator cells per slot; D1 population mutations
tests/core/                             # sentinels; test_101_error_completeness.cpp
tests/capi/                             # expected_error_map.csv, error_surface_test, abi_symbol_golden_test
tests/fuzz/                             # is_valid_wire_error
tests/interop/happy/golden/             # regenerated reject-invalid-admin goldens
tests/alloc_guard/                      # D9 run-thread allocation cells (W101-45)
tests/dictionary/                       # D1 tag_defined cells
tests/support/                          # session_test_access reads; run_thread_engine_rig extensions
ci/expected-*.txt                       # eligible-tests and preset-conditional re-records
spec/                                   # B&L rows, feature catalogue, coverage index (FR-009)
brain/components/                       # SecondBrain flags of the Supersessions list
```

**Structure Decision**: existing layout; new test files join existing binaries, except Session-level cells: a new
`session_101_tests` entry (099 precedent; `session_pure_tests` carries `TIMEOUT 120`).

## Phases (for /speckit-tasks)

1. **Census + RED** — run research D9's recipes at the branch head and write the manifest into `tasks.md`; one
   Session-level cell per acceptance scenario on today's tree; validator cells for D2.
2. **Dictionary + validator** — D1, D2; slots and C mapping; completeness pins (D9).
3. **Texts** — D3 at the three gate sites; rewrite the cells and goldens recipe 1 finds.
4. **One MsgType** — D4, D5, D6, LogoutSent; rewrite the 092 and 099 cells recipes 2–3 find, each naming its
   superseding FR and the superseded clause; header comments naming the supersessions (D10).
5. **Echo** — D7's helper, slot rebuild, slot assertion and outbound-ceiling record at every inbound-fed builder its census recipe
   finds; `on_inbound_frame`'s precondition sentence; retire 092's bound; D9's D7 cells (thresholds derived by
   building; the run-thread allocation cells and their RED arm; the slot death test).
6. **Mutations, benches** — each change reverted alone, its cells RED; D8's paired runs (Build/Copy rows included) and
   footprint rows.
7. **Records** — B&L per FR-009 and research's Supersessions list (L-452-2 closed; B-092-3's 372 bullet; B-099-3,
   B-099-4's headline and bullets, B-099-5, B-099-11 rewritten; B-092-1, B-075-6 and the 075 intro, B-041-1, B-099-2,
   B-099-8, B-099-25 and L-099-1 amended by research's planned rewrites; the new B-rows), catalogue/coverage index;
   false comments (D10) fixed, `check-comment-claims.py` on the diff. The SecondBrain edit of close-out row 3
   (`brain/components/session.md`, `brain/components/inbound-message-path.md`, `brain/components/wire.md`) flags in
   place every clause in research's Supersessions list, re-derived by its recipe lines. File quickstart's two issues.
8. **Verify** — re-derive the census after the last fix commit; targeted ctest (`-L 101`), sanitizers on
   `session_101_tests` and `session_pure_tests`,
   MSVC build of touched targets, `/speckit-verify`.

## Complexity Tracking

| Item | Why | Simpler alternative rejected because |
|---|---|---|
| D7's slot rebuild into a session-owned `kSendFrameBytes` buffer | a long MsgType made the Reject and the BMR vanish; QuickFIX echoes 372 in both | a fixed bound needs a number derived from the other fields' lengths, and 092's bound already went stale at 098; enlarging each site's buffer grows every reply frame; a coroutine owning the buffer allocates per overflow (`[const §XV.1]`); full parity at the inbound limit breaks the replay's capture ceiling (follow-up) |
| D4 widens 099's `last ≠ third` to "any later ≠ first" | one MsgType per frame | keeping the last-value comparison leaves `D…A…D` handled as D with a hidden A |
| D5 walks past the header stop on the cold path | QuickFIX C++'s 14-before-13 precedence with three or more 35s | reusing the stop-at-header walk answers 13 where C++ answers 14 |

## Open for Gate A — resolved (rounds 1–3)

No open item remains. Each item's last verdict is Codex round 3's, accepted by the owner's ruling (§Gate A, round 3).

1. D6's validation-off ending after Logon: approved by both reviewers; "handled as X" is now defined by D6's
   differential oracle. **Round 3 (Codex): sound.**
2. D2's 373=0 inside a group: approved, and traced to QuickFIX C++ on 098's AS-2 layout; the other layouts' divergence
   is a B-row. **Round 3 (Codex): sound** (373=0 for the group layout); the hand-built MsgType-definition contradiction
   it raised alongside is Finding 1, applied at round 3.
3. D7's echo: round 1 approved omit-372 for the session Reject; round 2 (Codex) blocked it for the BusinessMessageReject,
   where 372 is required. Resolved at round 2 by orchestrator ruling: one rule for both, 372 verbatim up to
   `kSendFrameBytes` through a rebuild into a session-owned slot; above it the Reject omits 372 and the BMR is not sent (research D7). The
   site census is D7's recipe, the trigger is `wire_field_value_truncated`, and 092's bound retires.
   **Round 3 (Codex): sound.**
4. Article X: no C-ABI version step is owed (both reviewers). **Round 3 (Codex): sound**; the new slots map to the
   existing `FIXPP_ERR_WIRE_CONFORMANCE`, and no public C enumerator or ABI shape changes.

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
  vendored dictionaries. The BMR ending was left open for a ruling at round 1 and decided at round 2 (below).
- **Root cause #4, the shared lookup.** Implemented as `valid_tag_set_view::found()` on the view `valid_tags_for`
  already returns, instead of a public `has_msg_type`, so no second query is added that nothing on the hot path would
  call (research D2).

### Round 2 → rewrite 2 (2026-11-07)

Codex P1=1 P2=3 P3=0; Opus post-judging P1=1 P2=3 P3=5. Reviews:
research/reviews/codex_101-inbound-reject-fidelity_gate_a_2_review.md,
research/reviews/opus_101-inbound-reject-fidelity_gate_a_2_adversarial_review.md (parent repo).

- **Codex 1 (P1), the BusinessMessageReject sent without its required 372.** Decided by orchestrator ruling, on the
  judge's (b-C) and unified across both messages, so 372 has one rule:
  - 372 verbatim whenever the frame fits `Session::kSendFrameBytes`;
  - an overflow of the site's buffer is rebuilt into a session-owned `kSendFrameBytes` inbound reply slot, before any
    callback and before `assign_outbound` (the second ruling, below);
  - above the ceiling, the Reject omits 372 and the BMR is not sent, with a rate-bounded session log record.

  Changed: spec Clarifications (a round-2 session; the round-1 BMR entry and the 2026-11-07 #646 answer marked as
  narrowed, not rewritten), US4 (six scenarios), FR-007 (a common clause, then (a) Reject and (b) BMR), FR-008,
  FR-009; research D7 (rule, steps, tail, placement, ceiling ending, capture invariant, allocation, residual,
  follow-up (b-L), alternatives), D9 (D7's witnesses), D10; the contract's echo table; data-model's helper; quickstart's
  issues to file; this plan's Summary, Constitution Check, Scale/Scope, Phases 5–7, Complexity and open item 3. No
  ending is left open for a ruling.

  **Round-1 text the unified rule changed:**
  - D7's "a bigger buffer moves the threshold" alternative now names the rejected form, enlarging each site's buffer.
  - The two 092 `RefMsgTypeBound_LongMsgType_RejectWithout372_*` cells flip to 372 present: their MsgType fits the
    ceiling. They move into D9's known cells, and an above-ceiling Reject cell keeps the omit-372 witness.
  - D9's "long undefined MsgType through the gate, no 372" splits into a fits-the-ceiling cell and an above-it cell.
  - The long-CompID residual shrinks to "above the ceiling", because the slot rebuild is triggered by any overflow.

  **Source contradicted the judge in two places:**
  1. The judge's cold leaf allocates. Its frame is over asio's recycler limit, as `send_fallback_leaf_`'s own comment
     says of that leaf. The first pass of this rewrite disclosed that allocation under #544 R-10.
     **Second orchestrator ruling** (still rewrite 2): no peer-triggerable allocation. `[const §XV.1]`'s hot path
     includes a reply to an inbound frame, and R-10's engine-caused fallback does not carry over.
     - D7 now rebuilds into a session-owned slot, with a non-coroutine helper and the site's existing tail.
     - The slot's exclusivity rests on `on_inbound_frame`'s serialized-caller precondition. Source shows overlap is
       already unsupported: `run_read_pump`, `run_accept_loop` and `run_connect_loop` await each frame;
       `on_inbound_cold_` reads `fsm_state_` at entry, and `on_inbound_not_connected_` writes Active only after its
       `co_await`s. The header's reentrancy contract states none of it.
     - The implementation adds the sentence and a B-row, and asserts it the way the re-entry rule is asserted.
     - #544 §2.2 had rejected a `Session`-member reply buffer shared with the strand's other chains. This slot is
       limited to the inbound chain.
     - FR-008, FR-009 (the allocation B-row dropped, the precondition B-row added), D7, D9 (run-thread allocation cells
       with a RED arm, the slot death test, release on every exit), the contract, the data-model and the XV.1 row
       (plain PASS) follow.
  2. The `static_assert` cannot sit at namespace scope: `kSendFrameBytes` is a private member of `Session`, and
     `CaptureVisitor` is in `session.cpp`'s anonymous namespace. It goes in the rebuild helper's body.

  `tests/alloc_guard` had no reply-chain cell (#544's windows take no reply). D9's new cells are the first.
- **Codex 2 (P2), D5's hook transition at the boundary field.**
  - D5 is now a per-field algorithm: one carry; S's hooks before the stop; the route's hooks for the stopping field and
    every later field.
  - It states why: `read_value` arms the next field from the current call's hooks.
  - It gives the chosen fault rule: a faulting walk answers 14, and the 35s it saw are discarded.
  - `peek_fixt_route`'s walk and its stop are kept.
  - D9 gains three boundary witnesses, each with the mutant that must go RED.
- **Codex 3 (P2), D8's timed rows.** The existing `BM_TableView_Build*`/`Copy*` rows join the A-B-A-B run with the
  +5% rule (D8, SC-004, quickstart, plan VIII row). The binary stays unpaired in `bench/ci-suite.txt`. Codex's new
  timed rows (judged P3) are not added: the existing rows cover the changed functions.
- **Codex 4 (P2), Supersessions incomplete.** The table is now a recipe plus its classified hits.
  - The patterns were widened until every clause both reviewers named is a hit.
  - Added: 092 FR-003's 372 sub-bullet and the contract's "Reject contents" 372 bullet; 099's Clarifications, US3
    scenarios 7–8, the Edge Case, E-7's non-validating paragraph, "leaves it false", "gains no step", the
    decisive-cells list and the `LO` row; `surface.md`'s non-FIXT split bullet; W-6's establishes clause; W-9's named
    cells; B-092-3; B-099-3; B-099-4's headline and Repeated bullet.
  - B-099-3 and `surface.md`'s second bullet were found by the recipe, not named by either reviewer.
  - FR-009 names the B&L rows.
- **Judge P3s.**
  - P3-1 (FR-005 restated two clauses of D5's rule): FR-005, US3 scenario 7, the round-1 Clarification and the
    contract cite D5's three-clause rule.
  - P3-2 (D7 placement named `fire_to_admin_` for every site): placement names `toApp` for the BMR and covers the cold
    rebuild.
  - P3-3 (SecondBrain): Phase 7 points the close-out row 3 edit at the Supersessions list, and research says so.
  - The Codex 3 extension: not added (above).
  - The Codex 4 099 quickstart hit: classified as history in the recipe's kept classes.
- **Not taken from the judge:** QuickFIX C++'s 380=3 and 58 text for `UnsupportedMessageType`. They are not needed for
  the decision. The local QuickFIX read used here is the 372 echo (`2ce8a60`, `4969564`).

### Round 3 → owner ruling (2026-11-07): apply fixes, accept; Gate A converged by owner ruling

Codex P1=0 P2=2 P3=0; Opus post-judging P1=0 P2=2 P3=1. The owner ruled "apply fixes, accept": the judge's fixes
are applied below and no further review round runs. Gate A converged by owner ruling.

Reviews (parent repo, `research/reviews/`):
- `codex_101-inbound-reject-fidelity_gate_a_review.md`, `opus_101-inbound-reject-fidelity_gate_a_adversarial_review.md`
  (round 1);
- `codex_101-inbound-reject-fidelity_gate_a_2_review.md`, `opus_101-inbound-reject-fidelity_gate_a_2_adversarial_review.md`
  (round 2);
- `codex_101-inbound-reject-fidelity_gate_a_3_review.md`, `opus_101-inbound-reject-fidelity_gate_a_3_adversarial_review.md`
  (round 3).

- **Finding 1 (P2), `add_required_tag` already defines the MsgType.** `table_view::add_required_tag` writes both
  `required_` and `valid_`, so `found()` is true after it alone; round 2's sentence saying otherwise is replaced. The
  existing API's ending is kept: a MsgType is defined when `add_valid(_tag)` or `add_required(_tag)` named it, and a
  group-context operation does not define one. Changed: research D2's first bullet, data-model's `found()` row,
  FR-001's hand-built clause; D9 gains the W-014 guard (`conforming-heartbeat-*` stay accept, `unexpected-tag-42` →
  `wire_undefined_tag`, 371=9999), a required-only cell, a ctx-only cell and the `add_required_tag` mutant.
- **Finding 2 (P2), the supersession census misses live B&L rows.** Codex round 3 reopened round 2's
  "Supersessions closed" claim; this fix closes it. The round-2 recipe is keyed on D4/D7 vocabulary and could not see
  D2's 2 → 0 move or the universal 371 claim. Research's recipe is now one line per decision (D4–D7, D2/373 with the
  quote-agnostic dictionary check, D2/371), over 092, 098, 099, the live B&L and `brain/components`. Superseded rows
  added: B-092-1, B-075-6 (and the 075 intro sentence), B-041-1, B-099-2, B-099-8, B-099-25, L-099-1, and 099's D2-line
  clauses whose tag the judging table does not define; planned B&L rewrites for each; kept classes for every other
  hit of the new lines; FR-009 names the rows; Phase 7 and research's SecondBrain paragraph add
  `brain/components/wire.md`.
- **P3-1 (judge), B-041-1's reason set.** Its enumeration is replaced by a pointer to
  `wire_error_to_session_reject_reason`, with an amendment naming 101's 0 and 11 (research Supersessions).
- **Beyond the judge's text.** The 075 section's intro sentence and B-041-1 are hits of the new 371 and D2 lines, so
  they are table rows and FR-009 names them; the D4 line's new `first-wins` pattern and the widened paths gave hits
  (098 history, brain pages, B-099-22) that are classified as kept.
