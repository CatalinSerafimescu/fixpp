# Implementation Plan: Sequence-reset fidelity — QuickFIX's endings for a reset on Logout and for a Logon acknowledgement below NextNumIn

**Branch**: `104-sequence-reset-fidelity` | **Date**: 2026-11-17 | **Spec**: [spec.md](spec.md)
**Base**: `bf294f00` (fixpp `main`, PR #700's squash). The session code is 103's squash `b3df5d45`.
**Input**: [spec.md](spec.md) with its Clarifications sessions. The design decisions are in
[research.md](research.md): D1–D10, spec errata E-1 to E-5, the completed Departures list, OPEN O-1 to O-3, and
risks R1–R4.

## Summary

Three defects on the sequence-reset surface, each fixed to the owner's 2026-11-17 rulings, plus the #266
re-record.

1. **B53 (#595).** `reset_on_logout` resets when a peer Logout is **received**: a request, the response to
   fixpp's own Logout, or a too-high Logout at its acknowledgement.
   - The reset is the existing 141=Y reset unit `(1, 1)`. It runs after the reply and before `Disconnected`, as
     in QuickFIX C++, QuickFIX/J and QuickFIX/Go.
   - The emit-time trigger in `run_logout_phase1` is deleted, so an unanswered close keeps its numbers.
   - `logout_seen_` keeps its name and now means "a received Logout's reset is owed". Where an arm ends before
     its reset, the teardown or an in-place reconnect's Logon makes it (D1).
   - `close()` waits for an in-flight unit before its teardown decision.
   - 024 C5.1 is kept as written, per teardown (D2).
2. **B107 (#695).** A lenient or unilateral initiator treats an unasked 141=Y acknowledgement as a reset before
   any comparison with NextNumIn. At 34=1 the numbers become inbound next 2, outbound next 1, and the session goes
   Active with no second Logon (D3).
   - At 34 > 1 the acknowledgement is not consumed. The session goes Active, and the peer's next frame draws a
     ResendRequest from 1 (D4, provisional, O-2).
   - The peer's follow-up ResendRequest is answered as QuickFIX clamps it: an EndSeqNo at or above the next
     outbound number is read as through-current (D3.3a).
3. **#701.** A too-low frame in NotConnected or LogonSent draws QuickFIX's Logout "MsgSeqNum too low, expecting N
   but received M", whatever its 43, through the existing `refuse_logon_with_logout_`. The session then ends
   `Disconnected` (D5, D6).
4. **FR-017.** The #266 test-count basis is re-recorded from a CI run at 104's head, last (D9).

No C ABI, configuration-key, public-type or `sizeof(Session)` change.

## Technical Context

**Language/Version**: C++20 (library), C (ABI, unchanged).

**Primary Dependencies**:
- standalone asio (coroutines, strands, `co_spawn` on an empty cancellation slot for the reset unit);
- the existing fixpp session, store and transport layers.

No new dependency.

**Storage**: `MessageStore::reset_to` (093 E-9), through `Session::run_reset_unit_`. MemoryStore and FileStore
override it as one operation.

**Testing**:
- GoogleTest. New cells go in `tests/session/test_104_*.cpp`, under the ctest entry `session_104_tests`
  (label `104`).
- Re-pins in `session_reset_on_lifecycle` and `session_inbound_frame_dispositions`.
- Conformance `session_tc_seqnum`, unchanged.

**Target Platform**: Linux (clang, gcc, libc++), Windows MSVC — Tier 1–3 matrices.

**Project Type**: library (FIX engine).

**Performance Goals**: no change on the per-frame path. The work added runs only on a Logout that ends the
session, or on an establishment frame (Article VIII §3: no perf change, so no bench).

**Constraints**:
- Article XV §1: no allocation on parse → validate → dispatch. The unit's `co_spawn` frame is one per session
  ending.
- 093 contract C-6's unit guarantees.
- One observable ending per condition.
- Keep QuickFIX's observable behaviour.

**Scale/Scope**:
- Session functions changed: `on_inbound_active_logout_`, `on_inbound_active_too_high_logout_`,
  `on_inbound_logout_sent_`, `acknowledge_peer_logout_`, `run_logout_phase1`, `emit_initiator_logon_`, `close`.
- The two Logon arms, `on_inbound_logon_sent_` and `on_inbound_not_connected_`, plus one file-local text builder
  shared with 103's Arm B.
- `replay_outbound_range_`'s end model.
- One member's contract changed (`logout_seen_`); no member added or renamed.
- Doc comments in `session.hpp` (the member, `drive_reconnect`'s latch paragraph) and `session_config.hpp`
  (`reset_on_logout`, `reset_on_disconnect`).
- Two CI basis files.

## Constitution Check

*GATE: must pass before Phase 0 research and be re-checked after Phase 1 design.* Each clause is quoted from
`.specify/memory/constitution.md`.

| Article (quoted) | Verdict |
|---|---|
| VI §5: "Every `/specify` artifact must include a **Normative References** section listing the exact `[DocAbbrev §X.Y.Z] Title` entries from the coverage index that inform the spec." | **PASS.** The spec lists FIX-SL §4.3.11, §4.4.2, §4.4.3, §4.6, §4.7.1, §4.8.1, §4.10.2 and FIX-TC 1B, 2, 13, each a row of `spec/coverage-index.md` (re-derive: `grep -n -F '<§ or scenario>' spec/coverage-index.md`). No key is added. Phase 9 adds 104's witnesses to the notes column of the keys it witnesses. |
| VII §3: "**TDD is mandatory.** Every feature lands as red-green-refactor: failing test first, then implementation." | **PASS (planned).** Each behaviour has a cell that is RED on the base, by reading, before its fix, and a mutant (research D10). Phases 2, 4 and 6 open with their cells. |
| VII §4: "**No code without a test.**" | **PASS (planned).** D3.3a's clamp, the text builder's new callers and the moved `close()` wait each have a cell (U-7/U-8, T-1/T-2, R-1j). |
| VII §5: "`tests/conformance/` holds the official FIX session-layer test cases … Every PR must pass them in CI." | **PASS.** `TcSeqnumTest.Tc2c_MsgSeqNumTooLow` (Active) is unchanged. T-1/T-2 are TC 2c's and 1B(d)'s Logon-arm instances. |
| VII §8: "Author isolation-safe tests grouped; select by ctest label, not executable name" | **PASS (planned).** One bucket, `session_104_tests`, with label `104`. Selection is `-L 104 --no-tests=error` (quickstart §2). |
| IX §1: "**Per-PR:** **≥95% line, ≥85% branch** on **touched modules** … **Binding rule — no silent uncovered error/edge path.**" | **PASS (planned).** Each new branch has a cell: each receipt reset site (R-1c, R-1d, R-1e), its failure (R-1i), the owed-reset backstops (R-1f, R-1g), the unasked branch (U-1, U-4), the clamp (U-7, U-8), and each too-low Logout ending (T-4). `/speckit-verify` assesses anything left uncovered. |
| IX §2: "**Sanitizers — Tier 1 (every PR, Linux/Clang):** ASan, UBSan, TSan must all run and pass." | **PASS (planned).** `session_104_tests` and the re-pinned targets run under each (quickstart §4). |
| X §1 / §6: "Every change to it is reviewed against the contract; Codex Gate A is mandatory." / "ABI-affecting features trigger all four mandatory controls" | **N/A.** No C-ABI change: `reset_on_logout` has no C-ABI setter (`git grep -n reset_on_logout include/fix`), and no signature, code or default moves. The behaviour change reaches C-ABI users through the session, as B-104-* rows. |
| XI §2: "**Cancellation: ASIO native cancellation slots end-to-end.** No parallel `stop_token` abstraction." | **PASS.** The receipt reset reuses the unit's empty-slot `co_spawn` and the arm's restore. No new cancellation path. |
| XI §3: "`fixpp::sync::async_mutex` … is the only allowed mutex shape for coroutines. One exception is permitted … `detail::replay_gate` …" | **PASS.** No new exclusion primitive. The unit's manager set takes the existing seqnum `async_mutex` (risk R1 re-derives its uncontended grant). |
| XI §7: "Threading/concurrency-affecting features trigger all four mandatory controls (Appendix A)" | **Applies.** It moves `close()`'s wait and adds the unit's suspension to three inbound arms. Appendix A's "Session FSM: … recovery semantics" also applies. The controls: `/clarify` done 2026-11-17 (spec Clarifications); `/analyze` after `/speckit-tasks`; Codex Gate A on this bundle; owner `/plan` sign-off (with O-2). |
| XII §9: "Security-affecting features trigger all four mandatory controls (Appendix A)" | **Covered by XI §7's four controls.** 104 changes no TLS, cipher or cert-source code. L-104-3 discloses that a too-low Logout precedes the CompID-authorization gate, the position of the existing posture, strict and SendingTime refusals. Gate A is asked to judge it. |
| XV §1: "The "hot path" of this ban is the latency-critical **in-memory** path — parse → validate → dispatch and `MemoryStore`, which MUST stay zero-allocation per message." | **PASS.** Per frame: one flag test in the LogonSent arm and nothing in Active. The unit's frame is allocated once per session-ending Logout, which is not per message. The text is built in a fixed stack array. |
| XVI §3 / §4: "`/clarify` is MANDATORY before `/plan` …" / "`/analyze` is MANDATORY for the same trigger set" | `/clarify` is done. `/analyze` is scheduled after `/speckit-tasks`. |
| XVII §1: Gate A triggers "Touches concurrency / threading / cancellation / executor model", "Touches the session FSM, recovery, or message store contract" | **Gate A required** (spec Assumptions). |
| XVII §7: "Before opening any PR, the contributor MUST run a local … build + ctest cycle on at least the `linux-clang-debug` preset" | Planned (phase 10). Builds are run one at a time, with the owner's approval (§7 resource gate). |
| XVII §8: "`/speckit-verify` … required after every `/speckit-implement`." | Planned (phase 10), before any gate label. |

**Owner `/plan` sign-off**: delegated. The owner's instruction of 2026-11-17 was "run autonomously through all
steps … Stop if you hit a blocker". The open items (O-1 to O-3, errata E-1 to E-4, L-104-3, D2, D3.3a, D1 item 6)
are ruled in research.md "Orchestrator rulings (2026-11-17)". Each is listed in the PR body for the owner's review at
Gate B sign-off.

## Project Structure

### Documentation (this feature)

```text
specs/104-sequence-reset-fidelity/
├── spec.md          # rulings in its Clarifications sessions
├── plan.md          # this file
├── research.md      # D1–D10, errata E-1..E-5, Departures, OPEN O-1..O-3, risks
├── data-model.md    # the flag's new contract, the unit's callers, close()'s order, the 141 branch, the end model, B&L delta text
├── quickstart.md    # censuses, RED/GREEN/mutant procedure, scenarios, FR-017
├── contracts/
│   └── session-wire-endings-104.md  # RT-1..RT-6 (supersedes 024 C3.1) and endings W/U/T
└── checklists/
```

### Source Code (library root)

```text
src/session/session.cpp                    # D1 (the receipt sites, phase 1's deleted set, emit_initiator_logon_, close()'s wait),
                                           # D3 (LogonSent 141 branch), D3.3a (replay_outbound_range_ end model),
                                           # D5/D6 (both Logon arms' too-low Logout; the shared text builder, Arm B's caller)
include/fixpp/session/session.hpp          # logout_seen_'s contract comment (FR-006); drive_reconnect's latch paragraph
include/fixpp/session/session_config.hpp   # reset_on_logout / reset_on_disconnect doc comments (receipt rule, names 104)
tests/session/test_104_reset_on_logout.cpp       # R-1a … R-1j, R-2
tests/session/test_104_unasked_reset_ack.cpp     # U-1 … U-8
tests/session/test_104_logon_too_low.cpp         # T-1 … T-6
tests/session/CMakeLists.txt               # add_session_104_bucket(session_104_tests …), label 104
tests/session/test_reset_on_lifecycle.cpp  # FR-005 re-pins (research D8)
tests/session/inbound_frame_dispositions_test.cpp  # Od29 cell re-aimed to reset_on_disconnect
spec/behaviors-and-limitations.md, spec/behaviors-and-limitations-closed.md, spec/feature-catalogue.md,
spec/coverage-index.md, brain/components/session.md   # records (phase 9)
ci/expected-eligible-tests.txt, ci/ctest-parallelism-probe.md   # FR-017 (phase 11, last)
```

**Structure Decision**: library-internal session changes, test buckets and records. No new public symbol, type,
error code or configuration key.

## Phases (for /speckit-tasks)

Order by dependency. Each phase's RED cells land before its change (TDD). The evidence file holds every census
result and every RED/GREEN/mutant record, never code comments.

0. **Setup.**
   - `session_104_tests` (label `104`) is registered by the first cell task (phase 1) with its first file,
     through an `add_session_104_bucket` twin of `add_session_103_bucket`. No empty bucket is registered.
   - Run the censuses in quickstart §1:
     - the witness census, static and measured, each with its known positive;
     - the acceptor-twin reachability greps (D7);
     - the seqnum-lock takers (R1);
     - the cells that pin `requested_end + 1` (R3).
   - Record the results. The phases below take the measured rows as their re-pin list.
1. **US1 RED cells** (research D10 R-1a … R-1j, R-2), in `test_104_reset_on_logout.cpp`. Confirm each is RED on
   the base, or GREEN where D10 says so, with its reason.
2. **US1 change (D1, D2).**
   - `logout_seen_`'s header comment with the condition and the recipe; every hit of
     `git grep -n logout_seen -- tests src include bindings tools` re-read.
   - The three receipt resets, with the restore after each.
   - The deletion in `run_logout_phase1`.
   - `emit_initiator_logon_`'s `reset_before_logon` widening and its clear.
   - `close()`'s wait moved ahead of the predicate.
   - The comments that state the old rule, each naming 104 (FR-006): the member, the teardown block's
     "Predicate" paragraph, `run_logout_phase1`'s "site (a)" comment, `on_inbound_active_logout_`'s and
     `on_inbound_active_too_high_logout_`'s "024 T013" comments, `drive_reconnect`'s latch paragraph,
     `session_config.hpp`.
   - Then the FR-005 re-pins from phase 0's census: `ResetOnLogout_LocalInitiated_Resets` and
     `ResetOnLogout_BothRoles_Resets`' initiator arm re-pinned to an answered close; the comment corrections in
     `DoubleTrigger_OneStoreReset` and `PeerInitiated_Resets`; the Od29 cell re-aimed to `reset_on_disconnect`.
   - GREEN, then each mutant RED.
3. **US2 RED cells** (U-1 … U-8), in `test_104_unasked_reset_ack.cpp`.
4. **US2 change (D3, D4, D3.3a).**
   - The LogonSent 141 read moved, and the unasked branch.
   - `replay_outbound_range_`'s `end_is_through_current`, and its end-model comment naming 104.
   - The census-found pins from phase 0 (R3). GREEN, then the mutants.
   - D4 follows the owner's O-2 answer. If the owner picks QFC's outcome, U-4 changes to "inbound next 2, no
     ResendRequest" and the branch sets consumed for any M.
5. **US3 RED cells** (T-1 … T-6), in `test_104_logon_too_low.cpp`.
6. **US3 change (D5, D6).**
   - The file-local text builder; Arm B switched to it, byte-identical, with `TooLowLogout103.*` as the pin.
   - Both Logon arms' too-low branches calling `refuse_logon_with_logout_`.
   - The census-found pins. GREEN, then the mutants.
7. **Whole-feature test run.** The label-`104` run, the re-pinned registrations, then the full `ctest` once
   (Article XVII §7), outside any peer's MSVC window. TSan, ASan and UBSan of the new and re-pinned targets.
8. **MSVC.** Build `session_104_tests` and the re-pinned targets with `-k 0`, in the shared sandbox under its
   `.sandbox-lock`, before the first labelled push.
9. **Records.**
   - The B&L delta (data-model.md "B&L delta"): B-104-1 … B-104-7 and L-104-1 … L-104-6 added; B-538-7,
     B-103-9's disclosure and B-103-10 amended in place; L-103-2 and L-103-7 moved to the closed file with 104's
     resolution.
   - `spec/feature-catalogue.md` (a 104 row, and 104's witnesses on the rows of the Normative References' keys).
   - The `spec/coverage-index.md` notes.
   - `brain/components/session.md`: flag 024 C3.1, B-538-7's clause and 103's latch disclosure as superseded,
     naming 104.
   - Close follow-ups fixpp#595, #695 and #701 through the PR body, with no closing keyword inside a negation.
   - O-1 is filed as an issue if it is not settled before Gate B. Label it at creation.
   - Run `.claude/scripts/check-comment-claims.py --root <tree> --base origin/main`.
10. **Polish.** `/simplify`, then `/speckit-verify` (the Tier-1 mirror, including MSVC of the new targets), then
    Gate B.
11. **FR-017, last** (research D9; the sequence is the owner's choice, O-3, and (a) is proposed).
    - After the last commit that changes a ctest registration, and after Gate B's fixes, dispatch Tier 1, 2 and 3
      (`workflow_dispatch`) on the branch at that head. First check from each workflow's `if:` that the
      peak-memory report step runs on a dispatch event.
    - Read every lane's count off that run, and confirm its DIAGNOSTIC ONLY warning, the known positive.
    - Write `ci/expected-eligible-tests.txt` and the "RE-RECORD — 104" section in one commit that touches no CMake
      file.
    - Then the final sign-off order (body → labels → wait ~2 min → push last). The labelled run is SC-005's
      acceptance.
    - Under (b) or (c), research D9 gives the alternative order.

## Complexity Tracking

| Addition | Why needed | Simpler alternative rejected because |
|---|---|---|
| The receipt reset at three arm sites | FR-001's "at receipt … after the reply … before the disconnect" in every arm that accepts a Logout. A too-high Logout's acknowledgement is its own site | a single teardown reset contradicts clarification Q3, and `onLogout` and an in-place reconnect would see the old numbers |
| Owed-reset backstops (the teardown predicate and the reconnect Logon) | an arm that ends before its reset (a callback throw, a failed persist, an unacknowledged too-high Logout) must still reset, as QuickFIX does at receipt | without them, those receipts never reset. A teardown-only backstop resets the wrong connection after an in-place reconnect |
| `close()`'s unit wait moved ahead of its predicate | FR-001: `close()` waits for the receipt reset even when it makes no reset itself | keeping the wait inside the predicate leaves a `reset_on_logout`-only close unordered against its in-flight receipt reset |
| D3.3a's end-model clamp | 104's unasked reset is the route that makes "nothing sent at 1" reachable in Active. An explicit EndSeqNo then desyncs the peer | leaving it would make FR-008's reset break against FIX.4.0/4.1 and chunked QuickFIX/J peers |
