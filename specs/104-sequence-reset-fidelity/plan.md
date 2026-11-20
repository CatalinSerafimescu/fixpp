# Implementation Plan: Sequence-reset fidelity — QuickFIX's endings for a reset on Logout and for a Logon acknowledgement below NextNumIn

**Branch**: `104-sequence-reset-fidelity` | **Date**: 2026-11-17 | **Spec**: [spec.md](spec.md)
**Base**: `bf294f00` (fixpp `main`, PR #700's squash). The session code is 103's squash `b3df5d45`.
**Input**: [spec.md](spec.md) with its Clarifications sessions. The design decisions are in
[research.md](research.md): D1–D10, spec errata E-1 to E-4 (applied), risks R1–R4, and the orchestrator rulings
O-1 to O-3. The observable rules are [contracts/session-wire-endings-104.md](contracts/session-wire-endings-104.md)
(Part 1: RT-1, RR-1 to RR-4 and the Outcomes table). The one list of departures is the spec's (FR-018).

## Summary

Three defects on the sequence-reset surface, each fixed to the owner's 2026-11-17 rulings, plus the #266
re-record.

1. **B53 (#595).** `reset_on_logout` resets when a Logout **received** from the peer is processed as far as its
   reset point (RU-1): a request in Active or LogonReceived, the response to fixpp's own Logout, a too-high Logout
   at its acknowledgement, or a Logout refusing fixpp's Logon in LogonSent (RU-2). Processing that ends before the
   reset point does not reset, and there is no teardown backstop under `reset_on_logout`.
   - At the reset point the arm records `Disconnected`, then runs the existing 141=Y reset unit `(1, 1)` (RU-3), so
     no emitter numbers in the reset space. Nothing on the wire differs from QuickFIX; `onLogout` fires before the
     reset (DQ-10).
   - The emit-time trigger in `run_logout_phase1` is deleted, so an unanswered close keeps its numbers.
   - `logout_seen_` keeps its name and becomes the receipt marker (`none` / `pending` / `reached`). `close()` and
     `Engine::stop()` wait for a `pending` receipt and its unit (stop through its role-loop join, with no bound:
     contract RR-2 item 4). Where `close()` cannot wait, it closes the receipt out to `reached` with no reset under `reset_on_logout`, and the arm, resuming, runs no unit: one owner
     (contract RR-2). `store_then_emit` and `persist_inbound_advance_` refuse every store call issued once the
     marker is `reached` (RR-4).
   - 024 C5.1 is kept as written, per teardown (D2).
2. **B107 (#695).** A lenient or unilateral initiator treats an unasked 141=Y acknowledgement as a reset before
   any comparison with NextNumIn. At 34=1 the numbers become inbound next 2, outbound next 1, and the session goes
   Active with no second Logon (D3).
   - At 34 > 1 the acknowledgement is not consumed. The session goes Active, and the peer's next frame draws a
     ResendRequest from 1 (D4, orchestrator ruling O-2).
   - On a direct mTLS initiator the unasked unit runs after the CompID-authorisation gate (clarification N-8).
   - The peer's follow-up ResendRequest is answered as QuickFIX clamps it: an EndSeqNo at or above the next
     outbound number is read as through-current (D3.3a).
3. **#701.** A too-low frame in NotConnected or LogonSent draws QuickFIX's Logout "MsgSeqNum too low, expecting N
   but received M", whatever its 43, through the existing `refuse_logon_with_logout_`, once the identity gates have
   passed (FIX-TC 1S(c), clarification N-8). The session then ends `Disconnected` (D5, D6).
4. **FR-017.** The #266 test-count basis is re-recorded from a CI run at 104's head, last, by sequence (a) (D9,
   ruling O-3).

No C ABI, configuration-key or public-type change. `logout_seen_` changes from `bool` to a one-byte enum, and
`reset_unit_wake_` is deleted (contract RR-2 item 4), and the private `bool` `wire_closed_by_reset_` is added (contract RR-6, ruling W). Whether `sizeof(Session)` moves is read once, base against head, at tasks.md T092; nothing
pins it (research D10). `Engine` gains one private member, R-1t's step-3 mark, so `sizeof(Engine)` changes, a layout
change in the installed `engine.hpp` with no C-ABI change; nothing pins it either (B-104-7).

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
- Re-pins and rewordings where the supersession census (research D8) keeps a hit: at least
  `session_reset_on_lifecycle`, `session_inbound_frame_dispositions`, the 027 walk cell, W8 and
  `test_logout_not_sent.cpp`'s comment.
- Conformance `session_tc_seqnum`, unchanged.

**Target Platform**: Linux (clang, gcc, libc++), Windows MSVC — Tier 1–3 matrices.

**Project Type**: library (FIX engine).

**Performance Goals**: one byte test per frame on each of two paths for RR-4, one more on each path for RR-5, and
nothing else per frame. Each mechanism is
accounted for on its own:
- **`store_then_emit`'s byte test** (RR-4), before the store call. It runs on every outbound frame that has a
  store call, every `Session::send` included. Article VIII §3 asks for a benchmark in the same PR, so phase 7 runs
  the existing `bench/session/session_send_bench.cpp`, whose sends pass through `store_then_emit`. The run is
  paired base-vs-candidate on one machine (Article VIII §2's method), and the results go in the evidence file.
- **`persist_inbound_advance_`'s byte test** (RR-4). It sits after the function's `store_is_persistent_` return, so
  it runs only on a persistent store's inbound persist and never on the in-memory inbound path (XV §1's hot path).
  `session_send_bench` does not cover it, and no benchmark is added for it.
- **The manager freeze's byte test** (contract RR-5). It sits in each refused `SeqnumManager` mutator, before its
  write, so it runs once per inbound frame (`check_inbound`) and once per outbound frame (`assign_outbound`), on the
  in-memory hot path too. Phase 7 runs `bench/session/seqnum_bench.cpp` (`assign_outbound`, a `bench/ci-suite.txt`
  row) and `bench/session/on_inbound_frame_bench.cpp` (`check_inbound` through `on_inbound_frame`) paired
  base-vs-candidate, with `session_send_bench`, by the same method.
- **`close()`'s poll** (contract RR-2 item 4). It runs in `close()` only, never per message.
  - Its worst case: one handler kept ready on the session's executor for up to `logon_timeout_ms`, while a receipt
    is held in a store call.
  - It is not a perf change under Article VIII: it changes none of the per-message quantities §1, §2, §4 and §5
    govern, it tests its condition before its first post (so a `close()` with nothing outstanding makes no pass),
    and it has the shape of the post-polls `close()` already runs: the joining close's result poll, 103's
    poll-join, the liveness join and 103's bound join (the last has the same worst case, unbounded). Its executor
    cost while a store call is held is a resource property, disclosed in B-104-2. Why 093's rejection of a poll
    does not apply here: research D1 item 4.
  - `engine_send_bench`, the only `paired` CI row on a send route (`bench/ci-suite.txt`), refuses its malformed
    payload before a number is read, so it never reaches `store_then_emit`; `session_send_bench` is not in
    `bench/ci-suite.txt`. RR-4's byte test is therefore measured only by the manual paired `session_send_bench`
    run above.
- The LogonSent arm gains one flag test.
- `Engine::stop()` gains R-1t's step-3 mark: one test of an unset member per stop, not per message, so not a perf
  change under Article VIII, as the LogonSent flag test is not.
- The rest runs only on a Logout that ends the session, on an establishment frame, or in `close()`.

**Constraints**:
- Article XV §1: no allocation on parse → validate → dispatch. Each new allocation site and its condition, none
  per message:
  - the reset unit's frame and its `co_spawn` at a receipt's reset point: at most one per connection, at a Logout
    that ends the session;
  - the same at the unasked 141=Y branch of `on_inbound_logon_sent_`: at most one per establishment;
  - each posted turn of `close()`'s poll: in `close()` only, while a receipt or unit is outstanding;
  - the step-3 mark's awaited hook in `Engine::stop()`: only when a test has set it.
- 093 contract C-6's unit guarantees.
- One observable ending per condition.
- Keep QuickFIX's observable behaviour.

**Scale/Scope**:
- Session functions changed: `on_inbound_active_logout_`, `on_inbound_active_too_high_logout_`,
  `on_inbound_logout_sent_`, `on_inbound_logon_sent_` (arm S, the unasked branch, the too-low branch),
  `acknowledge_peer_logout_` (B's reset point), `run_peer_logout_bound_` (no marker write before its reset point;
  the Debug assert there), `run_logout_phase1`, `close` (the
  poll), `run_reset_unit_` (its `reset_unit_wake_` call deleted), `store_then_emit` and `persist_inbound_advance_`
  (RR-4's boundaries), `install_reconnected_transport` (the thaw), and the failure branches of the
  `assign_outbound` callers (RR-5 item 6).
- `SeqnumManager` (`include/fixpp/session/seqnum_manager.hpp`, `src/session/seqnum_manager.cpp`): the freeze
  (contract RR-5; B-104-7).
- The two Logon arms, `on_inbound_logon_sent_` and `on_inbound_not_connected_`, plus one file-local text builder
  shared with 103's Arm B.
- `replay_outbound_range_`'s end model.
- One member's type and contract changed (`logout_seen_`, `bool` → a one-byte enum). One member deleted
  (`reset_unit_wake_`), with `run_logout_phase1`'s comment that names it as "the pattern close() uses" reworded. One
  private `Session` member added, `wire_closed_by_reset_`, and a defaulted exemption parameter on the private
  `store_then_emit` and `live_write_serialized_` (contract RR-6, ruling W). No member renamed.
- One private `Engine` member added: R-1t's step-3 mark, a test seam beside `test_hook_post_send_drain_`, set through
  `engine_test_access` and awaited by `Engine::stop()` between step 2's close join and step 3's join. So
  `sizeof(Engine)` changes: a layout change in an installed header, disclosed in B-104-7, with no C-ABI change.
- Doc comments in `session.hpp` (the member, `drive_reconnect`'s latch paragraph) and `session_config.hpp`
  (`reset_on_logout`, `reset_on_disconnect`).
- Two CI basis files.

## Constitution Check

*GATE: must pass before Phase 0 research and be re-checked after Phase 1 design.* Each clause is quoted from
`.specify/memory/constitution.md`.

| Article (quoted) | Verdict |
|---|---|
| VI §5: "Every `/specify` artifact must include a **Normative References** section listing the exact `[DocAbbrev §X.Y.Z] Title` entries from the coverage index that inform the spec." | **PASS.** The spec lists FIX-SL §4.3.11, §4.4.2, §4.4.3, §4.6, §4.7.1, §4.8.1, §4.10.2 and FIX-TC 1B, 1S, 2, 13, each a row of `spec/coverage-index.md` (re-derive: `grep -n -F '<§ or scenario>' spec/coverage-index.md`). No key is added. Phase 9 adds 104's witnesses to the notes column of the keys it witnesses. |
| VII §3: "**TDD is mandatory.** Every feature lands as red-green-refactor: failing test first, then implementation." | **PASS (planned).** Each behaviour has a cell that is RED on the base, by reading, before its fix, and a mutant (research D10). Phases 1, 3 and 5 are the RED-cell phases, each before its story's change. |
| VII §4: "**No code without a test.**" | **PASS (planned).** D3.3a's clamp, the text builder's new callers, the moved and widened `close()` wait and its poll, the close-out of a `pending` receipt with no reset, the reset point as one strand turn and its cancellation-state reset, the marker's owners (construction A: pump frames only, the bound's one write), RR-4's boundaries, arm S and the moved identity gates each have a cell (U-7/U-8, T-1/T-2, R-1j/R-1k/R-1s/R-1w, R-1l/R-1u, R-1x, R-1v/R-1f/R-1y/R-1z, R-1r, R-1o, T-6/U-9). |
| VII §5: "`tests/conformance/` holds the official FIX session-layer test cases … Every PR must pass them in CI." | **PASS.** `TcSeqnumTest.Tc2c_MsgSeqNumTooLow` (Active) is unchanged. T-1/T-2 are TC 2c's and 1B(d)'s Logon-arm instances. |
| VII §8: "Author isolation-safe tests grouped; select by ctest label, not executable name" | **PASS (planned).** One bucket, `session_104_tests`, with label `104`. Selection is `-L 104 --no-tests=error` (quickstart §2). |
| IX §1: "**Per-PR:** **≥95% line, ≥85% branch** on **touched modules** … **Binding rule — no silent uncovered error/edge path.**" | **PASS (planned).** Each new branch has a cell, and each contract ending has one: each receipt arm's reset point (R-1c, R-1d, R-1e, R-1o), each ending before it (R-1f, R-1g, R-1n), the races with `close()` and `Engine::stop()` (R-1j, R-1k, R-1l, R-1t, R-1u, R-1w, R-1x, R-1z), a duplicate too-high Logout (R-1y), the reply's cancelled store call (R-1v), the store failure (R-1i), the emitters at the unit (R-1p, R-1r), the unasked branch (U-1, U-4, U-9, U-10), the clamp (U-7, U-8), and each too-low Logout ending, one cell per ending per arm with its mutant (T-4a to T-4f, T-6). `/speckit-verify` assesses anything left uncovered. |
| VIII §3: "**No perf change merged without a benchmark in the same PR.**" | **PASS (planned).** Per mechanism ("Performance Goals"): `store_then_emit`'s byte test is the per-frame change, and `session_send_bench` runs paired base-vs-candidate in phase 7. RR-5's byte test in the manager's mutators is a per-frame change on both paths, measured by `seqnum_bench` and `on_inbound_frame_bench`, paired in the same run. `persist_inbound_advance_`'s byte test sits behind `store_is_persistent_`, so it is off the in-memory path and has no bench. `close()`'s poll is not a perf change (Performance Goals), and runs in `close()` only. Its worst case is up to `logon_timeout_ms` of posted turns while a store call is held, disclosed in B-104-2. Research D1 item 4 answers 093 OD-1's reason for rejecting a poll ("spins the strand for the whole bound"). `engine_send_bench`, the only `paired` CI row on a send route (`bench/ci-suite.txt`), refuses its malformed payload before a number is read and never reaches `store_then_emit`, and `session_send_bench` is not in `bench/ci-suite.txt`: RR-4's byte test is measured only by the plan's manual paired `session_send_bench` run (§2's method; §2 itself says automated coverage is partial). |
| IX §2: "**Sanitizers — Tier 1 (every PR, Linux/Clang):** ASan, UBSan, TSan must all run and pass." | **PASS (planned).** `session_104_tests` and the re-pinned targets run under each (quickstart §4). |
| X §1 / §6: "Every change to it is reviewed against the contract; Codex Gate A is mandatory." / "ABI-affecting features trigger all four mandatory controls" | **N/A.** No C-ABI change: `reset_on_logout` has no C-ABI setter (`git grep -n reset_on_logout include/fix`), and no signature, code or default moves. The behaviour change reaches C-ABI users through the session, as B-104-* rows. |
| XI §2: "**Cancellation: ASIO native cancellation slots end-to-end.** No parallel `stop_token` abstraction." | **PASS.** The receipt reset reuses the unit's empty-slot `co_spawn` and the arm's restore. No new cancellation path. |
| XI §3: "`fixpp::sync::async_mutex` … is the only allowed mutex shape for coroutines. One exception is permitted … `detail::replay_gate` …" | **PASS.** No new exclusion primitive. The unit's manager set takes the existing seqnum `async_mutex`, granted inline (risk R1). The receipt marker is a strand-confined byte, not a lock. |
| XI §7: "Threading/concurrency-affecting features trigger all four mandatory controls (Appendix A)" | **Applies.** It moves `close()`'s wait and adds the unit's suspension to three inbound arms. Appendix A's "Session FSM: … recovery semantics" also applies. The controls: `/clarify` done 2026-11-17 (spec Clarifications); `/analyze` after `/speckit-tasks`; Codex Gate A on this bundle; owner `/plan` sign-off (delegated, below). |
| XII §9: "Security-affecting features trigger all four mandatory controls (Appendix A)" | **Covered by XI §7's four controls.** 104 changes no TLS, cipher or cert-source code, but it changes two orderings against the mTLS CompID-authorisation gate, decided from FIX-TC 1S(c) (clarification N-8): the too-low Logout comes after the identity gates in both Logon arms (and after `authorize_logon` on the acceptor), and the initiator's unasked reset unit comes after its gate. Arm S's reset requires the same authorisation. What stays before the gates (posture, strict, SendingTime, 1137) is pre-existing and disclosed as L-104-3 (DQ-6). Cells: T-6's identity arms, U-9, R-1o. |
| XV §1: "The "hot path" of this ban is the latency-critical **in-memory** path — parse → validate → dispatch and `MemoryStore`, which MUST stay zero-allocation per message." | **PASS, by reading.** Per frame: one flag test in the LogonSent arm, RR-4's byte test in `store_then_emit` and `persist_inbound_advance_`, and RR-5's in `check_inbound` and `assign_outbound`; none allocates. The unit's frame is allocated once per session-ending Logout, which is not per message. `close()`'s poll is not on the per-message path. The text is built in a fixed stack array. The new allocation sites are listed under "Constraints". No allocation-counting cell is added: the existing `tests/alloc_guard/` entries, run under `tools/check_alloc.py`, are re-run at the head (T090), and whether any of them reaches `store_then_emit` or the LogonSent arm is read and recorded, so the record says which changed lines a measured witness covers and which rest on this reading. |
| XVI §3 / §4: "`/clarify` is MANDATORY before `/plan` …" / "`/analyze` is MANDATORY for the same trigger set" | `/clarify` is done. `/analyze` is scheduled after `/speckit-tasks`. |
| XVII §1: Gate A triggers "Touches concurrency / threading / cancellation / executor model", "Touches the session FSM, recovery, or message store contract" | **Gate A required** (spec Assumptions). |
| XVII §7: "Before opening any PR, the contributor MUST run a local … build + ctest cycle on at least the `linux-clang-debug` preset" | Planned (phase 7). §7's resource gate is discharged by the owner's standing grant of 2026-11-17 ("run autonomously through all steps … Stop if you hit a blocker"), recorded at tasks.md T001. Builds still run one at a time under `flock /tmp/fixpp-build.lock`, new trees go on `/mnt/wsl/fixppbuild`, and a build stops on any WSL memory pressure. |
| XVII §8: "`/speckit-verify` … required after every `/speckit-implement`." | Planned (phase 10), before any gate label. |

**Owner `/plan` sign-off**: delegated. The owner's instruction of 2026-11-17 was "run autonomously through all
steps … Stop if you hit a blocker". The items O-1 to O-3, errata E-1 to E-4, L-104-3, D2, D3.3a and the moved
`close()` wait are ruled in research.md "Orchestrator rulings (2026-11-17)"; RU-1 to RU-3 and the N-8 decision in
spec.md Clarifications, "Gate A round 1". None is open. Each is listed in the PR body for the owner's review at Gate
B sign-off.

## Project Structure

### Documentation (this feature)

```text
specs/104-sequence-reset-fidelity/
├── spec.md          # rulings in its Clarifications sessions
├── plan.md          # this file
├── research.md      # D1–D10, errata E-1..E-4, risks, the orchestrator rulings
├── data-model.md    # the receipt marker, the unit's callers, close()'s order, the 141 branch, the end model, B&L delta text
├── quickstart.md    # censuses, RED/GREEN/mutant procedure, scenarios, FR-017
├── contracts/
│   └── session-wire-endings-104.md  # RT-1, RR-1..RR-4, RT-4..RT-6, Outcomes O1..O20 (supersedes 024 C3.1, 027 C3, 093 C-6's close() bullet) and endings W/U/T
└── checklists/
```

### Source Code (library root)

```text
src/session/session.cpp                    # D1 (the receipt arms A/L/B/S and their reset points, phase 1's deleted set,
                                           # close()'s wait and the close-out of a pending receipt, RR-4's boundaries, the marker's clear at install),
                                           # D3 (LogonSent 141 branch, the unasked unit after the mTLS gate),
                                           # D3.3a (replay_outbound_range_ end model),
                                           # D5/D6 (both Logon arms' too-low Logout after the identity gates; the shared
                                           # text builder, Arm B's caller)
include/fixpp/session/session.hpp          # logout_seen_'s type and contract comment (FR-006); drive_reconnect's latch paragraph
include/fixpp/session/session_config.hpp   # reset_on_logout / reset_on_disconnect doc comments (receipt rule, names 104)
include/fixpp/session/engine.hpp           # R-1t's step-3 mark: a private test seam beside test_hook_post_send_drain_
src/session/engine.cpp                     # Engine::stop() awaits that seam, when set, between step 2's close join and step 3's join
tests/support/engine_test_access.hpp       # its setter
tests/session/test_104_reset_on_logout.cpp       # R-1a … R-1z, R-2, R-3 (conditional, research D10)
tests/session/test_104_unasked_reset_ack.cpp     # U-1 … U-10
tests/session/test_104_logon_too_low.cpp         # T-1 … T-6 (T-4a … T-4f)
tests/session/CMakeLists.txt               # add_session_104_bucket(session_104_tests …), label 104
tests/session/test_reset_on_lifecycle.cpp  # FR-005 re-pins (research D8)
tests/session/inbound_frame_dispositions_test.cpp  # Od29 cell re-aimed to reset_on_disconnect
tests/session/test_next_expected_msgseqnum.cpp     # the 027 C3 cell re-pinned to NewSeqNo=6 (D3.3a)
tests/session/test_persistent_seqnum_hydrate.cpp   # W8: an inbound-next assertion and its comments (D8)
tests/session/test_logout_not_sent.cpp             # the comment naming the superseded rule (D8)
spec/behaviors-and-limitations.md, spec/behaviors-and-limitations-closed.md, spec/feature-catalogue.md,
spec/coverage-index.md, brain/components/session.md, brain/components/graceful-logout.md   # records (phase 9)
ci/expected-eligible-tests.txt, ci/ctest-parallelism-probe.md   # FR-017 (phase 11, last)
```

**Structure Decision**: library-internal session changes, test buckets and records. No new public symbol, type,
error code or configuration key; R-1t's step-3 mark is a private `Engine` member, reached only through the existing
`engine_test_access` friend.

## Phases (for /speckit-tasks)

Order by dependency. Each phase's RED cells land before its change (TDD). The evidence file holds every census
result and every RED/GREEN/mutant record, never code comments.

0. **Setup.**
   - `session_104_tests` (label `104`) is registered by the first cell task (phase 1) with its first file,
     through an `add_session_104_bucket` twin of `add_session_103_bucket`. No empty bucket is registered.
   - Run the censuses in quickstart §1:
     - the supersession census (research D8): its static recipe over D8's pathspec (every other bundle's files
       included), then its measured half, with its known positives (B-024-1 seeded) each a hit;
     - the acceptor-twin reachability greps (D7);
     - the store-order condition and the manager's inline grant (research R1);
     - the Active sub-arms that write after a suspension following `check_inbound` (R-1r's route; contract RR-4);
     - RR-5's readings (research D11): the mutator callers by recipe, each refused return's caller handling, the
       emitters whose `toAdmin` precedes `assign_outbound`, `hydrate`'s reachability while frozen, and phase 1
       after a receipt reset (contract O22). A reading that contradicts RR-5 stops implementation.
   - Record the results. The phases below take the census's kept hits as their re-pin and reword list.
1. **The settling experiment comes first** (owner, 2026-11-18). Construction A rests on INV-T and INV-X (contract
   RR-2), which are read from source and not yet run. If either fails, the construction is wrong, not its
   implementation.
   - Add the Debug asserts `peer_logout_.phase != acking` at the entry of `on_inbound_active_logout_` and of
     `on_inbound_active_too_high_logout_`. They compile at the base.
   - Write R-1y and R-1z first (research D10), after HookedStore's per-call record.
   - In a Debug build, where asserts are on, run these together, by gtest filter in their binaries:
     - `*TooHighLogoutState103.*` in `session_103_tests`. The suite is a `TEST_P` instantiated as
       `BothRoles/…`, so a filter without the leading `*` selects nothing. That covers 103's
       `SecondTooHighLogoutWhilePending_*` and `H_LogoutResentInSequence_*` cells.
     - R-1y and R-1z, R-1z's `OneDelivery` arm included.

     Record each test count, which must not be 0.
   - **Only R-1z's `OneDelivery` arm is an input that reaches INV-T's window.** There the bound's wake has been
     posted and has not run yet when a 35=5 frame passes the drain test. 103's cells and R-1z's
     `CloseDuringReplyStore` arm feed one frame per pump, so the bound has run before the next frame. 103's
     `H_LogoutResentInSequence_*` also fills only to S, which leaves the bound unwoken.
   - **What a green `OneDelivery` run proves, and what it does not.** The bound needs at least two posted turns
     after its wake before it can claim (the derivation is the next bullet). A green run therefore proves that no
     suspension between the drain test and the entry assert lasts long enough for the bound to finish that race.
     It does not run INV-T as a whole: a suspension of one turn leaves the 103 mark still `pending` at the entry
     assert, so the assert stays silent, and that case is covered by the source reading of INV-T only.
   - Known positive, run in a scratch copy. The event it must cause: the bound reaches `acking` while the Logout
     frame is suspended between the drain test and the dispatch. The observable: the `OneDelivery` arm aborts on
     the entry assert.
     - The edit: between `on_inbound_active_`'s drain test and its sub-arm dispatch, insert a capped loop,
       `for (int i = 0; i < K && peer_logout_.phase == peer_logout_mark::phase_t::pending; ++i) co_await
       asio::post(…)`.
     - K is derived from the bound's wake path, read at the pinned asio
       (`grep -n "operator||" <asio>/experimental/awaitable_operators.hpp`; `grep -n "co_spawn_entry_point" -A 30
       <asio>/impl/co_spawn.hpp`; `grep -n "^void complete" -A 6 src/core/test/mock_clock.cpp`):
       1. `wake_peer_logout_bound_` calls `woken.cancel()`, which queues the aborted wait's handler (one posted
          turn).
       2. The bound waits in `woken.async_wait(…) || detail::await_deadline(…)`. `operator||` is a
          `make_parallel_group(…).async_wait(wait_for_one_success(), …)`, which completes only when every operand
          has completed. When the aborted wait runs, the group cancels the deadline operand.
       3. `mock_clock`'s cancellation handler completes the parked `sleep_until` through `complete()`, which
          calls `asio::post` (a second posted turn). Only then does `await_deadline` retire.
       4. Each operand's completion is dispatched (`co_spawn_entry_point`), which adds a posted turn only while
          the operand is still launching or on another executor. The group then completes, and the bound resumes
          to `live()` and its claim.

       So the race takes at least two posted turns, possibly more. K is eight times that minimum, derived at the
       pinned asio and recorded with its derivation in the evidence file (T022), never here. The loop exits as
       soon as the mark leaves `pending`, so a generous K costs nothing.
     - The known positive itself fails if the cap is hit with the mark still `pending`: the event did not happen,
       and the instrument is not accepted. The pass at which the mark left `pending` is recorded in the evidence
       file, not here.
     - The loop also records the phase it reads at exit. The bound sets `acking` and then awaits its
       acknowledgement (`run_peer_logout_bound_`), and sets `none` when that returns. So a value other than
       `acking` (for example `none`, if the acknowledgement ran to its end without suspending) means the event was
       not observable at the assert, and the known positive fails as it does at the cap. The remedy is in the
       scratch run, not the design: hold the bound's reply `store` in HookedStore, so the bound stays `acking`
       across the frame's next pass.
     - If the arm does not abort, the selection cannot report a failure of INV-T, and the experiment is not
       accepted: implementation STOPS, as it does for a fired assert (below; tasks T023, T044).
   - The marker's assert (`none` at the bound's reset point, RR-2 item 5) needs the enum and B's reset point. It
     lands with phase 2, which re-runs the same selection as soon as B's reset point exists. Only R-1y and R-1z set
     `reset_on_logout`, so only they can trip it: with rol off the marker is always `none`. Its known positive, in
     a scratch copy, seeds the violation where the assert reads it. The event: the bound reaches its reset point
     with the marker `pending`. The edit: set the marker to `pending` in the bound immediately before its
     reset-point assert. The observable: R-1y's bound trips the assert.
   - **If any assert fires, at either point, implementation STOPS** and the decision goes back to the owner, with
     the cell, the assert and the stack.
   - Then the rest of the US1 RED cells (research D10 R-1a … R-1x, R-2, and R-3 when phase 0 reads its input drivable), in `test_104_reset_on_logout.cpp`.
     Confirm each, R-1y and R-1z included, is RED on the base (an assertion failure, not an abort), or GREEN where
     D10 says so, with its reason.
2. **US1 change (D1, D2; contract Part 1).**
   - `logout_seen_`'s type (the three-valued marker) and its header comment with the condition and the recipe;
     every hit of `git grep -n logout_seen -- tests src include bindings tools` re-read (at the base the hits
     outside `session.cpp` are comments only, including `session.hpp`'s paragraph on the latches that stay set
     across a reconnect).
   - The receipt arms A, L, B, S, per contract RR-2 items 1, 2 and 5:
     - the marker set at receipt by the pump frames only, with B's spawn and ignored branches writing nothing;
     - each pump frame's owner, and its clear before an in-arm `close()`;
     - the bound's one write at its reset point, in `acking`;
     - the reset point in RR-3's order: reply, `Disconnected`, cancellation-state reset, marker `reached`, unit,
       restore;
     - the marker's Debug assert at each reset point (RR-2 item 5). As soon as B's reset point exists, re-run the
       settling experiment's selection (phase 1). An assert that fires stops implementation there too.
   - The deletion in `run_logout_phase1`.
   - `close()`: the wait moved ahead of the predicate, widened to a `pending` marker, and made a poll by post
     (contract RR-2 item 4), with `reset_unit_wake_` deleted; a marker still `pending` after the wait closed out to
     `reached` with no reset (design ruling 3); the predicate's reset under rod only.
   - The reset point as one strand turn: the cancellation-state reset, then the marker's move to `reached` (from
     the pump frame's `pending`, or from `none` in the bound; RR-2 item 5) written immediately before
     `run_reset_unit_`, with no suspension or callback between (contract RR-2 item 3).
   - RR-4's boundaries in `store_then_emit` and `persist_inbound_advance_`; the clear at
     `install_reconnected_transport`.
   - RR-5's manager freeze (research D11): in `include/fixpp/session/seqnum_manager.hpp` and
     `src/session/seqnum_manager.cpp`, the flag, the passkey-gated `freeze` / `thaw` and query `frozen`, and the test in the six refused
     mutators; in `src/session/session.cpp`, the freeze in `run_reset_unit_` and at the close-out, the thaw at
     `install_reconnected_transport`, T050's Debug asserts, and the conditional `Disconnected` writes on the paths
     a refused `assign_outbound` returns into (RR-5 item 6), and `inbound_resend_request_leaf_`'s entry test (RR-5
     item 6, the T014 STOP). Cells R-5a to R-5f, all but R-5d written RED with the other US1 cells, and R-5d with
     the mechanism.
   - The comments that state the old rule, each naming 104 (FR-006): the member, the teardown block's
     "Predicate" paragraph, `run_logout_phase1`'s "site (a)" comment, `on_inbound_active_logout_`'s and
     `on_inbound_active_too_high_logout_`'s "024 T013" comments, `drive_reconnect`'s latch paragraph,
     `session_config.hpp`.
   - Then the re-pins and rewordings the census kept: `ResetOnLogout_LocalInitiated_Resets` and
     `ResetOnLogout_BothRoles_Resets`' initiator arm re-pinned to an answered close; the comment corrections in
     `DoubleTrigger_OneStoreReset`, `PeerInitiated_Resets` and `test_logout_not_sent.cpp`; the Od29 cell re-aimed
     to `reset_on_disconnect`.
   - GREEN, then each mutant RED.
3. **US2 RED cells** (U-1 … U-10), in `test_104_unasked_reset_ack.cpp`.
4. **US2 change (D3, D4, D3.3a).**
   - The LogonSent 141 read moved, the unasked branch, and its unit after the mTLS gate (clarification N-8).
   - D4 as ruled (O-2): an unasked acknowledgement at M > 1 is not consumed.
   - `replay_outbound_range_`'s `end_is_through_current`, and its end-model comment naming 104.
   - The census's kept hits for D3 and D3.3a: W8's inbound-next assertion and comments; the 027 C3 cell
     (`WalkExtraction.TwoValueEnd_ExplicitEndBeyondStore`) re-pinned to `NewSeqNo=6`, with its file header's
     statement of 21 rewritten as the rule. GREEN, then the mutants.
5. **US3 RED cells** (T-1 … T-6, T-4a … T-4f), in `test_104_logon_too_low.cpp`.
6. **US3 change (D5, D6).**
   - The file-local text builder; Arm B switched to it, byte-identical, with `TooLowLogout103.*` as the pin.
   - Both Logon arms' too-low branches: the identity gates first (the mTLS CompID-authorisation gate; on the
     acceptor `authorize_logon`), through the helpers that make today's checks, then `refuse_logon_with_logout_`.
   - The census's kept hits. GREEN, then the mutants.
7. **Whole-feature test run.** The label-`104` run, the re-pinned registrations, then the full `ctest` once
   (Article XVII §7), outside any peer's MSVC window. TSan, ASan and UBSan of the new and re-pinned targets. The
   paired `session_send_bench` run (Article VIII §3), one build at a time.
8. **MSVC.** Build `session_104_tests` and the re-pinned targets with `-k 0`, in the shared sandbox under its
   `.sandbox-lock`, before the first labelled push.
9. **Records.**
   - The B&L delta (data-model.md "B&L delta"): B-104-1 … B-104-7 and L-104-1 … L-104-7 added (L-104-7 is ruling
     O-1's row, marked source-derived and not live-traced); B-024-1, B-538-7, B-563-5, B-093-11, L-093-12,
     B-103-9's disclosure and B-103-10 amended in place; L-103-2 and L-103-7 moved to the closed file with 104's resolution.
   - `spec/feature-catalogue.md` (a 104 row, and 104's witnesses on the rows of the Normative References' keys).
   - The `spec/coverage-index.md` notes.
   - `brain/components/session.md` and `brain/components/graceful-logout.md`: flag 024 C3.1, 027 C3, 093 C-6's
     `close()` bullet, B-538-7's clause and 103's latch disclosure as superseded, naming 104; reword
     graceful-logout.md's `logout_seen_` invariant row and its R-24 bullet; a pointer on session.md's
     rejected-alternative bullet (research D8).
   - Every other component page the issue #334 recipe finds (tasks T100): a page whose `refs` list a document 104
     supersedes, or whose `codegraph_entry` names a function 104 changes ("Scale/Scope"), is read, and each
     statement of a superseded rule on it is flagged in place. Known positive:
     `brain/components/message-store-quiescence.md`, whose 141=Y unit section says `close()` waits "when it is
     about to issue its teardown reset and a unit is in flight" (093 C-6's bullet).
   - File a labelled issue (bug, follow-up) for the refusals that still answer before the identity gates (L-104-3,
     FIX-TC 1S(c)); take the label names from `gh label list`.
   - Close follow-ups fixpp#595, #695 and #701 through the PR body, with no closing keyword inside a negation.
   - Run `.claude/scripts/check-comment-claims.py --root <tree> --base origin/main`.
10. **Polish.** `/simplify`, then `/speckit-verify` (the Tier-1 mirror, including MSVC of the new targets), then
    Gate B.
11. **FR-017, last** (research D9; sequence (a), ruling O-3).
    - After Gate B's last fix, which is the last commit that can change a ctest registration, push the branch and
      dispatch Tier 1, 2 and 3 (`workflow_dispatch`) at that head. First check from each workflow's `if:` that the
      peak-memory report step runs on a dispatch event.
    - Read every lane's count off that run, and confirm its DIAGNOSTIC ONLY warning, the known positive (research
      D9 defines its lane set).
    - Write `ci/expected-eligible-tests.txt` and the "RE-RECORD — 104" section. Before committing, run
      `bash ci/test-parallelism-verdict.sh`, as the basis file's header requires after any edit to it (not
      `ci/test-peak-rss.sh`, which the header names as the wrong harness). Record its result in the evidence file.
      Then commit both files in one commit that touches no CMake file, which D9's two `git diff` checks confirm.
    - **Its disposition** (`.specify/close-out.md` §1, "One line to settle in the Gate B record"):
      - The basis commit is CI data only. It lands deliberately outside the range that Gate B reviewed and that
        `/speckit-verify` recorded, and neither is re-run for it.
      - Why that is enough: it changes no code, test or registration, which D9's checks and the verdict harness
        show mechanically, and its only reader is the #266 report on the labelled run.
      - The Gate B record and the verify record each say so in one sentence that names the commit. That is the
        written disposition close-out.md asks for. It is what answers the parent rule that a gate result is scoped
        to the commits that existed when it ran.
    - Then the final sign-off order: BODY → LABELS → wait ~2 min → PUSH LAST, the last push being the basis
      commit. The labelled `pull_request` run that includes it is SC-005's acceptance.

## Complexity Tracking

| Addition | Why needed | Simpler alternative rejected because |
|---|---|---|
| The reset point in four receipt arms (A, L, B, S) | FR-001 and RU-1: a received Logout resets if and only if its processing reaches its reset point; a too-high Logout's acknowledgement and the LogonSent refusal are their own sites | a single teardown reset contradicts clarification Q3 and RU-1, and a teardown backstop resets Logouts QuickFIX did not process |
| The three-valued receipt marker, `close()`'s wait widened to `pending` as a poll, and the close-out of a `pending` receipt with no reset (design rulings 1–3) | RU-1's one-owner rule: 093's single-interleaving-point argument does not hold on routes A and B, where `close()` can overtake an arm suspended before its unit (Opus N-3) | waiting only on `reset_unit_in_flight_` cannot see an arm suspended in its reply or persist; a teardown take-over of a `pending` receipt resets a Logout that was not processed (rejected, design ruling 3); an event-driven wait is correct only if every exit from `pending` calls its waker (Opus round 2, N-1) |
| RR-4's refusal in `store_then_emit` and `persist_inbound_advance_` | an Active-arm frame in flight beside the detached bound can write after the reset, and an arm closed out by `close()` can write after the teardown (RU-3's check of every emitter) | a gate on each leaf is one test per site and misses the persist; one test at the two store boundaries covers every writer |
| D3.3a's end-model clamp | 104's unasked reset is the route that makes "nothing sent at 1" reachable in Active. An explicit EndSeqNo then desyncs the peer | leaving it would make FR-008's reset break against FIX.4.0/4.1 and chunked QuickFIX/J peers |

## Gate A

- Round 1 applied 2026-11-17: Codex P1=5 P2=2 P3=1; Opus post-judging P1=1 P2=7 P3=7; rewrite addresses root causes 1, 2, 3 (RU-1..RU-3). Reviews: research/reviews/codex_104-sequence-reset-fidelity_gate_a_review.md, research/reviews/opus_104-sequence-reset-fidelity_gate_a_adversarial_review.md.
- Round 2 applied 2026-11-18: Codex P1=4 P2=3 P3=2; Opus post-judging P1=2 P2=8 P3=7; rewrite addresses Codex #1, N-1 (P1s) and every P2/P3. Reviews: research/reviews/codex_104-sequence-reset-fidelity_gate_a_2_review.md, research/reviews/opus_104-sequence-reset-fidelity_gate_a_2_adversarial_review.md.
- Round 3 (loop 1 exhausted) 2026-11-18: Codex P1=2 P2=0 P3=0; Opus post-judging P1=2 P2=0 P3=1 (RC-1: one marker, several owners on route B). Fable consult 104-receipt-reset-marker-ownership: probable. Owner chose a targeted amendment with construction A; loop 2 follows. Reviews: research/reviews/codex_104-sequence-reset-fidelity_gate_a_3_review.md, research/reviews/opus_104-sequence-reset-fidelity_gate_a_3_adversarial_review.md.
- Loop 2 round 1 applied 2026-11-18: Codex P1=1 P2=0 P3=0; Opus post-judging P1=0 P2=1 P3=3; rewrite addresses N-1 (P2), N-2, N-3, Codex #1 (downgraded P3). Reviews: research/reviews/codex_104-sequence-reset-fidelity_gate_a_4_review.md, research/reviews/opus_104-sequence-reset-fidelity_gate_a_4_adversarial_review.md.
- Loop 2 round 2 applied 2026-11-18: Codex P1=1 P2=0 P3=0; Opus post-judging P1=0 P2=2 P3=0; rewrite corrects the Engine::stop() rows (role-loop join, L-563-4, L-093-12) and adds the stop cell. Reviews: research/reviews/codex_104-sequence-reset-fidelity_gate_a_5_review.md, research/reviews/opus_104-sequence-reset-fidelity_gate_a_5_adversarial_review.md.
- Loop 2 round 3 (loop 2 exhausted) 2026-11-18: Codex P1=0 P2=1 P3=0; Opus post-judging P1=0 P2=1 P3=0 (R-1t can stay GREEN under its reorder mutant). Owner chose: apply the judge's three verbatim edits, then a fresh narrow confirmation loop. Reviews: research/reviews/codex_104-sequence-reset-fidelity_gate_a_6_review.md, research/reviews/opus_104-sequence-reset-fidelity_gate_a_6_adversarial_review.md.
- Loop 3 round 1 (narrow confirmation) 2026-11-18: Codex P1=0 P2=0 P3=0; Opus post-judging P1=0 P2=0 P3=2 (New-1 pump budgets vs kHoldBound, New-2 wording), both applied. **Gate A CONVERGED.** Reviews: research/reviews/codex_104-sequence-reset-fidelity_gate_a_7_review.md, research/reviews/opus_104-sequence-reset-fidelity_gate_a_7_adversarial_review.md.
- /speckit-analyze 2026-11-18: 26 findings (1 escalated, 3 HIGH); all applied. F1 corrects three base readings in research D10 in place (RED on base: the base ends before the identity gate); no design change, so no Gate A round (orchestrator ruling). D1 ruled HIGH (the owner's standing grant discharges §XVII.7's approval; T001 records it). B3: gate-b-done, per 103's T163 precedent.
- Implementation STOP at T011, 2026-11-18: in-flight manager writers after the reset point. Fable consult 104-post-reset-inflight-manager-writers (probable). Owner ruled construction (Z), manager freeze; orchestrator sub-rulings: freeze on any reached marker, refused return per mutator, reset-writer mechanism. Narrow Gate A loop 4 follows.
- Loop 4 round 1 applied 2026-11-18: Codex P1=0 P2=1 P3=0; Opus post-judging P1=0 P2=2 P3=5; rewrite adds the post-set Debug assert (C-1) and narrows FR-019/B-104-8 (N-1); P3s applied. Reviews: research/reviews/codex_104-sequence-reset-fidelity_gate_a_8_review.md, research/reviews/opus_104-sequence-reset-fidelity_gate_a_8_adversarial_review.md.
- Loop 4 round 2 applied 2026-11-18: Codex P1=1 P2=1 P3=0; Opus post-judging P1=1 P2=2 P3=1; rewrite replaces T014 (C-1, N-1), adds the passkey-gated frozen(freeze_key) query (C-2), fixes T061's row (N-2). Reviews: research/reviews/codex_104-sequence-reset-fidelity_gate_a_9_review.md, research/reviews/opus_104-sequence-reset-fidelity_gate_a_9_adversarial_review.md.
- Loop 4 round 3 (narrow confirmation) 2026-11-18: Codex P1=0 P2=0 P3=1 (rejected by the judge); Opus post-judging P1=0 P2=0 P3=3 (N-1..N-3), applied with the optional D11 note. **Gate A loop 4 CONVERGED** (manager freeze, construction Z). Reviews: research/reviews/codex_104-sequence-reset-fidelity_gate_a_10_review.md, research/reviews/opus_104-sequence-reset-fidelity_gate_a_10_adversarial_review.md.
- T014 STOP 2026-11-18: inbound_resend_request_leaf_ reachable after a suspension (missing-7 Reject). Orchestrator ruling: by-construction entry state test (B41 pattern) plus the sending-time conditional write; no ordering argument. Reviewed by Gate A loop 5 (narrow), with ruling W; owner-visible at Gate B.
  - Ruling W, 2026-11-18. This is the third STOP of the class "work in flight across the write of `reached`", after the T011 manager writers and the T014 leaf. Once a reset point has run, nothing more is transmitted (contract RR-6). The test reads `wire_closed_by_reset_` after the `write_gate_` grant and before the sink. The close-out is exempt. Sub-ruling: our crossing phase-1 Logout is exempt from D-OUT-10 and from the test, by one parameter that only `run_logout_phase1` passes, because QuickFIX has already written our Logout before it processes the peer's. Cells R-5f, R-5g, R-5h, R-5i and R-1l's sink twin. A narrow Gate A loop 5 reviews the T014 delta (the leaf test, Arm D, W).
  - Loop 5 round 1 applied 2026-11-18. Codex P1=1 P2=1 P3=1; Opus post-judging P1=2 P2=1 P3=6.
    - The rewrite adds cell R-5i (Codex 1), the member's construction value (Codex 2), and the named carrier with a census by category (Codex 3).
    - It adds one ending for C1, C2 and C4 (N-1, orchestrator ruling; cells R-5h's C1 arms and R-5j), and moves the close-out witness to R-1l with O13-r narrowed (N-2).
    - N-3 departs from the judge on arms L and S: under N-1, D-OUT-10's ending depends on the member, so dropping either set is observable in C1. Each gets a witness (R-5h `StoreHeldAtArmL`, R-5j) instead of an equivalence reading.
    - It also applies N-3 to N-6. N-4: the crossing in LogonReceived is classified and handled.
    - Reviews: research/reviews/codex_104-sequence-reset-fidelity_gate_a_11_review.md, research/reviews/opus_104-sequence-reset-fidelity_gate_a_11_adversarial_review.md.
  - **Owner-visible items for Gate B sign-off (ruling W's one ending):**
    - `Session::send` returns success, with nothing transmitted, for a send in flight when a received Logout's reset point runs. Before the ruling the design returned `dispatch_aborted`; the base transmitted the send.
    - A direct initiator's `open()` returns success with the session Disconnected, and its Logon not sent, when the peer's Logout reached the LogonSent arm while that Logon was in its store call. On that path `open()` fires `onCreate` once, with the state already `Disconnected` (B-104-8; contract RR-6 items 3 and 6; cell R-5j).
    - The `send` result departs from QuickFIX/J (spec DQ-18).
  - Loop 5 round 2 applied 2026-11-18 (rewrite 2 of 2). Codex P1=1 P2=1 P3=0; Opus post-judging P1=0 P2=2 P3=4.
    - The rewrite adds DQ-18 and qualifies "No new departure" to the wire (Codex 1), and adds HookedStore's `store_applied` / `store_returned` events with every order pin restated on them (Codex 2).
    - It names `drive_reconnect()` as the second caller, with no conforming input (N-1), pins `onCreate` on `open()`'s new path (N-2), adds the one-ending sentence to FR-019 (N-3), and repairs T039 (N-4).
    - Reviews: research/reviews/codex_104-sequence-reset-fidelity_gate_a_12_review.md, research/reviews/opus_104-sequence-reset-fidelity_gate_a_12_adversarial_review.md.
  - Loop 5 round 3, 2026-11-18. Codex P1=1 P2=2 P3=0; Opus post-judging P1=0 P2=2 P3=2. Loop 5 exhausted at round 3; owner: apply the judge's bounded edits + confirmation round.
    - The edits applied: `drive_reconnect()` kept in RR-6's census with "no conforming input", and the success-while-Disconnected ending restricted to `open()` (Codex 1).
    - R-5g and R-5h `GateWait` select the frame's `store` with `release_when` already true (Codex 2a).
    - `blocking_write_state::starts` in T015, with its known positive, and the C2 pins restated on the start count with the rol-off rise (Codex 2b).
    - R-5j reads `state()` inside `onCreate` (N-1).
    - Reviews: research/reviews/codex_104-sequence-reset-fidelity_gate_a_13_review.md, research/reviews/opus_104-sequence-reset-fidelity_gate_a_13_adversarial_review.md.
  - Loop 5 converged at the confirmation round (codex/opus `_14`), P3 applied. Opus post-judging P1=0 P2=0 P3=1. The P3: the R-5f, R-5g `GateWait` and R-5h `GateWait` start-count pins name their two read sites, the frame's `release_when` call and `Hooks::on_reset` at the unit's `reset_to`, and require the readings to be equal. Reviews: research/reviews/codex_104-sequence-reset-fidelity_gate_a_14_review.md, research/reviews/opus_104-sequence-reset-fidelity_gate_a_14_adversarial_review.md.

### Round 1 — disagreements

- **Codex #2 (P1, "the reset claim is cleared before the cancellable manager phase").** Disagreed, as the Opus
  judge did. The race needs a contended seqnum lock, and none can exist before `close()`'s final
  `seqnum_mgr_.drain()`: `async_lock_op`'s fast path grants inline inside the initiation
  (`include/fixpp/core/sync/async_mutex.hpp`), no `SeqnumManager` method suspends while it holds the mutex
  (`src/session/seqnum_manager.cpp`), and all session work is serialised (the session strand, or an
  `already_serialized_executor`). After the first lock, `FIXPP_DETAIL_CO_AWAIT_LOCK` part 4 leaves the filter
  terminal-only, and `Engine::stop()` emits total. The forced manager-contention test Codex asks for cannot be
  built. Codex's valid concern, that `close()` cannot see a receipt before `reset_unit_in_flight_` is set, is met
  by RR-2: `close()` waits on the `pending` marker, which spans receipt → reset point → unit with no gap (research
  R1, contract RR-2 item 3).
- **Codex #8 (P3, "the Constitution Check cites a nonexistent path").** Disagreed, as the Opus judge did.
  `.specify/memory/constitution.md` is a tracked symlink to `../constitution.md` (check with
  `ls -la .specify/memory/` and `git ls-files .specify/memory`; `cmp` reports the two files identical), so the
  citation resolves and the quoted clauses are reproducible. The path is left as written.

### Round 2 — disagreements

- **Codex #1's counter-proposal (P1, confirmed).** Not taken: a transactional store boundary, or cancelling an
  issued store call without mutation. The Opus judge's narrowing was taken instead. "No store call is issued after
  the close-out" (contract RR-2 item 5), and O13 is split by what was already issued (O13-r, O13-p). A store call
  that was issued runs to its end (`B-598-1`), and the rulings do not need it undone. Codex's "persisted and
  transmitted" also overstates the wire half: on a live transport `close()` has already run `close_async()` and
  `write_gate_.cancel_and_drain()`, and only a config-time `transport_send` sink transmits (`B-563-5`). Opus's
  optional marker test after the store await was not added. It is outside the approved shape, and O13-r's wire
  ending is stated per transport.
- **Codex #2 (P1 → P2, judged).** One stale Summary sentence, not a design defect. Every operative passage
  already stated design ruling 3. The sentence is fixed.
- **Codex #3 (P1 → P2, judged).** Codex's seven-step order (basis commit, then verify and Gate B on that head,
  looping on every Gate B fix) is not taken. `.specify/close-out.md` §1 permits a commit outside Gate B's reviewed
  range when the record says so, and the parent's sign-off order expects one push after labelling. What was
  missing was the written disposition and a feasible quickstart order. Both are now in phase 11 and quickstart §5,
  together with the harness the basis file's header requires (N-5).
- **Codex #4 (P1 → P3, judged).** The owner's rule against recording results is about code comments (project
  `CLAUDE.md`). Its general form is the bundle's own D8 ("results go to the evidence file"). The spec's numbers
  were a dated observation of a named run, and FR-017 already forbade copying them. They are removed; the run and
  the recipe stay.
- **Codex #9's counter-proposal (P3, confirmed).** "Mock-clock advancement for both boundaries" cannot be built:
  `close_grace` is a real `asio::steady_timer`. R-1j takes the Opus correction instead.
- **Codex #6's thrown timing.** No second cell. `store_then_emit` gives both timings one block after its `try`
  (D-OUT-2), so R-1v's returned timing witnesses the shared ending. O9's "unreachable after the first lock" is
  deleted. The direct caller's `terminal` emission is R-1x at the reset point.
- **N-1's shape.** The shape is a poll by post, not a waker at every exit. The poll has no exit it can miss. A
  waker is correct only if every exit calls it, and N-1 is exactly a missed one. Cost: one posted turn per pass,
  in `close()` only, bounded by `logon_timeout_ms` (contract RR-2 item 4). The brief's mutant "delete the wake"
  has nothing to delete under a poll. It is re-aimed: restore 093's event wait → RED (R-1w).
- **N-7's note on RU-1.** RU-1's wording ("a receipt they could not wait for … has not reached its reset point")
  is a binding ruling and is not edited. Read for O14, where the unit is already in flight, it means a receipt
  still `pending`. Contract RR-2 item 4 states that reading, in design ruling 3's own words.

### Loop 2 round 1 — disagreements

- **Codex #1 (P1 → P3, judged).** Codex read the Article VIII §3 row as exempting `close()`'s poll from the
  benchmark requirement, and proposed a paired base-vs-candidate bench that holds a receipt pending during
  `close()`. The counter-proposal is not taken, for the judge's reasons:
  - §3 is met by its text: the PR carries a benchmark, and it measures the PR's only per-message change. No clause
    of Article VIII asks for a benchmark per mechanism, and §2 says §3's benchmark need not pass.
  - The poll is not a perf change in Article VIII's own terms. It moves none of §1, §2, §4 and §5's per-message
    quantities or benched rows: it runs in `close()` only, after the joins, tests its condition before its first
    post, and makes no pass in an ordinary `close()`.
  - Precedent under the same, unamended text: `close()` already runs four post-polls with no benchmark (the
    joining close's result poll, 103's poll-join, the liveness join and 103's bound join, the last unbounded with
    the same worst case).
  - No §2 reading is possible. The base's wait is event-driven, with zero posted turns, and the candidate's turn
    count over a held receipt is set by the hold the bench itself chooses.
  - The poll's shape is binding (round 2, "N-1's shape"), so no measurement could change a decision.

  What remained was the wording: "a cost to disclose, not a regression to bench" read as an author-accepted
  regression, which §2 forbids. "Performance Goals" and the VIII §3 row now classify the poll instead, contract RR-2
  item 4 states the test-before-first-post rule, and the row records that RR-4's byte test is measured only by the
  manual paired `session_send_bench` run. No benchmark is added.

### Loop 2 round 2 — disagreements

- **Codex #1 (P1 → P2, judged).** Codex's facts stand: `Engine::stop()` calls `close(terminal)` only at its step 4,
  after its step 3 role-loop join, and the pump awaits each frame (INV-P), so RR-2's bounded close-out never runs
  under stop. The bundle's stop rows are corrected to that order (contract RR-2 item 4, "Under `Engine::stop()`";
  O15; FR-001; B-104-2; research D1 item 4), citing `L-563-4` and `L-093-12`. Loop 2 round 1's owner table was
  right about the owner (A) and wrong about the reason: A owns its ending under stop through the role-loop join,
  not through RR-2's poll, and that table's "no clock: close-out" consequence does not apply to stop.
  Codex's counter-proposal, starting the session close (or a close-out on the session strand) after the transport
  closes and before the role-loop join, is not taken, for the judge's three reasons:
  - **It does not bound stop.** The role-loop join stays, and it still waits for the held store call, so stop's
    latency is unchanged. Stop is bounded only by the held store call, the class `L-093-12`, `L-563-7` (2) and
    `B-598-1` already disclose; 104 widens nothing there.
  - **It makes stop's ending depend on timing.** Its only effect is to change the ending: a hold longer than
    `logon_timeout_ms` would end with no reset instead of one. Stop's one deterministic ending (a reset if and only
    if the arm reaches its reset point, RU-1) would become a timing-dependent pair, which the "one condition, one
    observable ending" rule forbids.
  - **It reorders a load-bearing step 3 → 4.** INV-4b (step 4's `close()` drains the liveness loop after the role
    loops have exited), INV-6 (before `registry_.clear()`), step 4's `operation_aborted` analysis, and `L-563-4`,
    the shipped contract, all depend on that order.

  Cell R-1t pins the order: its mutant is exactly this reorder, and it must go RED.
- **New-1's no-clock twin (not added).** The judge asked for a no-clock twin of R-1t. Under the Engine it has no
  input: `Engine::start` refuses a null `EngineConfig::clock` (`validate_engine_config`, `clock_not_set`), and
  `Session::open` falls back from a null `clock_override` to that clock, so an Engine session always has one.
  R-1t's clock arm, advanced past `logon_timeout_ms`, is the discriminating cell. A "clock never advanced" twin is
  not a substitute: under the reorder mutant `close()` would wait out the hold and reset once, so it would stay
  GREEN against the mutant listed for it, which is New-1's defect again. (Superseded in part by loop 2 round 3: the
  decisive check is now the step-3 mark's order assertion; the clock advance stays as the bound's contrast.)

### Loop 4 round 1 — disagreements

- **Codex P2's second option (confirmed P2, judged).** Not taken: a test seam that runs `run_reset_unit_` with
  targets other than `(1, 1)` and the marker `reached`, so that a freeze armed ahead of the unit's setters would
  show as a refused `set_next_inbound` or `set_next_outbound`. Rejected for the judge's two reasons:
  - the seam is outside the Execution rules' set of seams ("A seam outside that set goes to the orchestrator");
  - it exercises an input that production never produces: every receipt unit's targets are `(1, 1)` (data-model
    "The reset unit: new callers").

  Codex's first option was taken instead: the post-set Debug assert in `run_reset_unit_` (contract RR-5 item 4;
  T050), read by its own message in the Debug build under T061's reorder mutant. In an asserts-off build the
  reorder is an equivalent mutant, recorded as such and claimed by no assertion (research D11, "Not relied on").

### Loop 4 round 2 — disagreements

- **C-2's alternatives (confirmed P2, judged).** `Session`'s Debug asserts need to read the manager's private flag.
  The judge took a passkey-gated `[[nodiscard]] bool frozen(freeze_key) const noexcept`, inline, with no lock, that
  no production branch tests (contract RR-5 item 4; T049). Three alternatives were rejected, for the judge's reasons:
  - **A Debug-only accessor (`#ifndef NDEBUG`).** `seqnum_manager.hpp` is installed, and its fixpp#511 comment states
    that a member gated behind a macro makes a TU's `SeqnumManager` a different class from the library's, an ODR
    violation (ill-formed, no diagnostic required). A consumer may set `NDEBUG` differently from the library
    (research D11).
  - **`freeze` returning the previous state.** It cannot express the entry assert or the "iff" asserts, and under
    T061's reorder mutant the check would move with the freeze, so the mutant would go undetected.
  - **`friend class Session`.** Research D11's own rejected alternative: it gives `Session` the counters too, so
    INV-Z would rest on reading `session.cpp`.
