# Quickstart: validating 104

Run from the library root of the tree that holds the branch. Only one build or test process runs at a time, by
machine rule. The contract is [contracts/session-wire-endings-104.md](contracts/session-wire-endings-104.md) (RT-1,
RR-1 to RR-4, RT-4 to RT-6, the Outcomes table, rows W, U and T). The cells and mutants are named in [research.md](research.md) D10.

## 1. Census first (phase 1, before any fix)

Run each census in a scratch copy, never committed. Record the results in the evidence file, never in code
comments.

**Supersession census (research D8).** It finds surfaces that stay GREEN as well as cells that fail.
- Static half: research D8 steps 1 to 3, with its pathspec and key table, are the recipe; this file does not keep a
  copy of the command. Give each kept hit a disposition (re-pin, reword, supersede by name, unaffected) in the
  evidence file.
- Measured half: apply D1 alone, then D3 + D3.3a, then D5, each in its own scratch copy, and run:
  ```bash
  ctest --preset linux-clang-debug -L session -V
  ctest --preset linux-clang-debug -L capi -V
  ctest --preset linux-clang-debug -L config -V
  ctest --preset linux-clang-debug -R '^session_tc_' -V   # tests/session/conformance's standalone registrations
  ```
  Also run the standalone interop labels. Every failing cell must already be a kept hit of the static half;
  one that is not means a key is missing.
- Known positives, which must be hits (static) or fail (measured), or the census is broken: the list in research
  D8 step 5, with B-024-1 as the seeded static positive, and for the measured half:
  - D1: `ResetOnLifecycleTest.ResetOnLogout_LocalInitiated_Resets`;
  - D5: a scratch cell feeding a too-low acknowledgement;
  - D3.3a: a scratch cell sending RR(7=5, 16=10) at next outbound 5, and
    `WalkExtraction.TwoValueEnd_ExplicitEndBeyondStore`.

**Reachability censuses.**
- Acceptor twin (D7):
  ```bash
  git grep -n "record_state_transition_(fsm_state::NotConnected" src
  git grep -n "ensure_hydrated_(" src
  git grep -n -e set_next -e set_counters include
  ```
  The acceptor twin is in scope if any of these hits a production path.
- The manager's inline grant and the store order (research R1):
  ```bash
  git grep -n "seqnum_mgr_\.\(assign_outbound\|check_inbound\|set_next\|reset_to_one\)" src/session
  git grep -n "async_lock\|asio::post\|offload" -- include/fixpp/session/*store*.hpp src/session/*store*.cpp
  ```
  Read each hit. If a store can apply a later `reset_to` before an earlier `store`, stop and take R1's fallback
  to the orchestrator.
- The emitters at the reset point (contract RR-4): every Active sub-arm that writes (a leaf or
  `persist_inbound_advance_`) after a suspension that follows its `check_inbound`, and every emitter's state test
  before its `assign_outbound`. Read `run_liveness_loop`, `Session::send_body_`, `replay_outbound_range_` and the
  `inbound_*_leaf_` family. R-1r's route is the first such sub-arm found.

## 2. RED on the base, then GREEN, then the mutant

The new cells are registered in `session_104_tests`, which carries label `104`. Article VII §8 grouping means a
GTest name given to `ctest -R` selects nothing and exits 0, so select like this:

```bash
cmake --build --preset linux-clang-debug --target session_104_tests
# one cell: run the binary; record the reported test count (0 fails the step)
<build>/tests/session/session_104_tests --gtest_filter='ResetOnLogout104.Unanswered_Timeout_NoReset'
# the constitutional run: by label, refusing an empty selection
ctest --preset linux-clang-debug -L 104 --no-tests=error --output-on-failure
```

The settling experiment (plan phase 1) runs before the other cells. Run it in a Debug build. 103's suite is a
`TEST_P` instantiated as `BothRoles/…`, so its filter needs the leading `*`. Record each count, which must not be 0:

```bash
<build>/tests/session/session_103_tests --gtest_filter='*TooHighLogoutState103.*'
<build>/tests/session/session_104_tests --gtest_filter='ResetOnLogout104.DuplicateTooHighLogoutWhilePending_*:ResetOnLogout104.ClaimsAPendingBound_*'
```

For each cell in research D10:
1. Build at the commit that adds the cell, before its fix, and record the failing assertion.
2. Build after the fix and record GREEN, with the same non-zero test count.
3. In a scratch copy, apply the cell's mutant. `strings`-check that the binary is fresh, re-run, and record RED.

The re-pinned cells (D8) are run in their own registrations, `session_reset_on_lifecycle` and
`session_inbound_frame_dispositions` (standalone, so `ctest -R '^<name>$'` selects them), before and after.

Cells with a forced-hit or a competing-timer element:
- **R-1c / R-1d / R-1e / R-1o.** Each reads at the store's `on_reset` hook: the store call it saw (`reset_to`,
  the unit, not `reset`, the teardown), the state (`Disconnected`, RU-3) and that the transport is still open.
  R-1d and R-1e also read the numbers inside `onLogout`, which must be the pre-reset ones (DQ-10).
- **R-1k / R-1l / R-1u.** A final-state assertion cannot tell the arm's reset from a teardown reset; each cell
  asserts which store call ran (R-1l: none). R-1k and R-1u assert that the reset finished before `close()`
  returned. R-1u's mutant splits the reset point's strand turn and must go RED.
- **R-1b.** The drop's close must complete before `logout_disconnect_timeout_ms`. Assert the completion instant
  against the grace deadline, so the timer cannot be what ended the wait.
- **R-1j.** Assert that the store hold is reached in the LogoutSent arm (state `Disconnected`, transport open,
  phase 1 not woken). A hold reached in the teardown would also block `close()`. The grace timer is a real
  `asio::steady_timer`: a small real-time `logout_disconnect_timeout_ms` expires during the hold, while the mock
  clock is never advanced to `logon_timeout_ms`. Assert the transport closed while the `close()` future is pending.
- **R-1l.** Name each number on its own: the manager's inbound moved at `check_inbound`, before the arm; the store's
  inbound moves only if the persist lands (`HeldInPersist`).
- **R-1w, R-1f(1), R-1v.** `close()`'s wait is a poll by post, so a cell pumps it with a bounded run (`run_for`, a
  slice loop), never an unbounded `poll()`. "Not held" is read with the mock clock below `logon_timeout_ms`;
  `StuckPending` is the forced hit of the expiry event.
- **Every "no reset" cell** carries its forced-hit arm (research D10, "Rules carried into the cells").
- **R-1p.** The mock clock passes one and then two HeartBtInt during the hold, so the loop's Heartbeat and
  TestRequest deadlines both fall inside it.
- **T-4a.** The overflow cell is a pair: the longest CompIDs that still emit the Logout (the forced hit), and one
  character more.

## 3. Scenarios, by user story

| Story | Scenario | Cells |
|---|---|---|
| US1 AS1 | `rol`, unanswered Logout, timeout, so no reset | R-1a |
| US1 AS2 | `rol`, unanswered Logout, transport drop, so no reset | R-1b |
| US1 AS3 | peer answers, so the reset is at the reset point | R-1c, R-1j |
| US1 AS4 | peer request, so the reset is at the reset point | R-1d |
| US1 AS5 | `rod`, with and without `rol`: one reset per teardown, `(1, 1)` | existing `ResetOnDisconnect_*`, R-2 |
| US1 AS6 | Logout in LogonSent resets; in NotConnected does not | R-1o |
| US1 AS7 | processing ends before the reset point: no reset | R-1f, R-1g, R-1n |
| US1 edges | too-high Logout (a duplicate included); races with `close()` and `Engine::stop()` (A beside a claimed bound included); a cancelled reply store; store failure; reconnect; emitters at the unit; a direct caller's cancellation at the reset point | R-1e, R-1h to R-1l, R-1p to R-1s, R-1u to R-1z |
| US2 AS1/AS2 | unasked 141=Y at 34=1, lenient and unilateral | U-1, U-2 |
| US2 AS3 | strict unchanged | U-3, existing `Initiator_Received141Ack_*` |
| US2 AS4 | 141=Y at M > 1 | U-4, W8 re-pin |
| US2 follow-up | 789 after the reset; RR(1..0); explicit EndSeqNo | U-5 to U-8 |
| US2 mTLS | the unasked unit after the gate | U-9 |
| US3 AS1/AS2 | too-low acknowledgement and too-low Logon draw the Logout | T-1, T-2, T-3 |
| US3 AS3 | the Logout cannot be sent: one cell per ending per arm | T-4a to T-4e |
| FR-015 | the too-low Logout does not reset | T-5 |
| Precedence | SendingTime, strict, the identity gates (FIX-TC 1S(c)) | T-6 |

## 4. Whole-feature checks

- The full `ctest` once, for Article XVII §7, outside any peer's MSVC window.
- TSan, ASan and UBSan runs of `session_104_tests` and of the re-pinned targets.
- MSVC: build `session_104_tests` and the re-pinned targets with `-k 0` before the first labelled push.
- `.claude/scripts/check-comment-claims.py --root <tree> --base origin/main` over the new and changed comments.
  The `logout_seen_` comment and the end-model comment must state a condition and a recipe, not a
  count.
- The paired `session_send_bench` run, base against candidate on one machine (plan "Performance Goals", Article
  VIII §3): RR-4 adds a byte test to every outbound frame's store path. Its other test, in
  `persist_inbound_advance_`, sits behind `store_is_persistent_` and is not benched. Results go to the
  evidence file.

## 5. Records, verification, then FR-017 last

The order is plan phases 9 to 11.

1. Records (phase 9): the B&L delta (data-model.md), L-104-7 and the amendments to B-024-1, B-563-5 and B-093-11
   included; `spec/feature-catalogue.md` rows, with 104's witnesses added to the rows whose Spec ref is a key in
   the spec's Normative References (re-derive with `grep -n -F '§4.3.11' spec/coverage-index.md` and the like);
   the `spec/coverage-index.md` notes column for those keys; the supersession flags on
   `brain/components/session.md` and `brain/components/graceful-logout.md` (024 C3.1, 027 C3, 093 C-6's
   `close()` bullet, B-538-7's clause, 103's latch disclosure, and each merged-bundle document the census flags);
   the L-104-3 follow-up issue, labelled.
2. `/speckit-verify`, then Gate B (phase 10).
3. FR-017 (phase 11; research D9, ruling O-3), after Gate B's last fix:
   - dispatch the tiers on the branch at that head, before labelling;
   - read each lane's count off that run (`ran:` on Linux, `out of <N>` on Windows);
   - confirm the same run printed the DIAGNOSTIC ONLY warning, the known positive;
   - write `ci/expected-eligible-tests.txt` and a "RE-RECORD — 104" section of `ci/ctest-parallelism-probe.md`,
     run `bash ci/test-parallelism-verdict.sh`, and commit both files in one commit that changes no CMake file;
   - record the commit's disposition in the Gate B and verify records (plan phase 11);
   - BODY → LABELS → wait ~2 min → PUSH LAST;
   - accept on the labelled run that includes it: no DIAGNOSTIC ONLY for a count mismatch.
