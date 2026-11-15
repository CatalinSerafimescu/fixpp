# Implementation Plan: Resend and Logon-reset fidelity — QuickFIX's and FIX-SL's endings for sequence recovery

**Branch**: `103-resend-logon-reset-fidelity` | **Date**: 2026-11-14 | **Spec**: [spec.md](spec.md)
**Base**: `da129e17` (PR #680's squash of feature 102), rebased from `2c74af92` on 2026-11-14; the two trees are identical.
**Input**: [spec.md](spec.md) with three Clarifications sessions; design decisions in [research.md](research.md)
(D1–D9, RED cells, pins, B&L delta, constitution check, risks, and the normative "Pass-2 amendments").

## Summary

Six session defects on the sequence-recovery and Logon-reset paths, each fixed to the owner's 2026-11-14 rulings:
the default reset policy becomes `bilateral_lenient` and `bilateral_strict` resets before every Logon (#682,
C-ABI 1.17 BREAKING); resend state ends with the connection (#578); BeginSeqNo 0 is answered from 1 and an absent
tag 7 is Rejected (#580); a too-high ResendRequest is serviced and a too-high Logout is acknowledged after a
bounded recovery (#577); every outbound emitter outside the inbound chain is serialised against a resend answer by
a strand-local shared/exclusive gate (#588); the gap deadline acts on every frame that does not advance
NextNumIn, and every too-low frame without PossDupFlag draws QuickFIX's Logout (#581). Every item is RED-first;
#577 and #578 begin with a reproduction.

## Technical Context

**Language/Version**: C++20 (library), C (ABI), Python (binding tests, harness tools)
**Primary Dependencies**: standalone asio (coroutines, strands, `steady_timer`), existing fixpp session/store/
transport layers; no new dependency
**Storage**: `MessageStore` implementations (MemoryStore, FileStore) — behaviour of the strict reset uses the
existing `reset_seqnums_to_one_durable` path
**Testing**: GoogleTest (`tests/session`, `tests/capi`, `tests/config`, `tests/interop`), conformance cells,
pytest (`bindings/python/tests`), the parent interop matrix (live QuickFIX C++ v1.16.0 / QuickFIX/J 3.0.2)
**Target Platform**: Linux (clang, gcc, libc++), Windows MSVC — Tier 1–3 matrices
**Project Type**: library (FIX engine)
**Performance Goals**: no regression on the send path beyond the bench gate's tolerance (SC-006); the fast path of
the replay gate is a flag test and a counter, no await, no allocation
**Constraints**: Article XV §1 (no allocation on parse → validate → dispatch), #583 (no terminal cancellation filter
left by the new gate), Article XI §3 as amended by this feature (contracts/constitution-xi3-amendment.md), one
observable ending per condition
**Scale/Scope**: ~15 session functions changed (research Sites lists), two new private helpers plus one coroutine
(`note_inbound_advance_`, `answer_resend_request_`, `run_peer_logout_bound_`), one new internal primitive
(`detail::replay_gate`), one private counter (`conn_gen_`), one internal header field
(`FrameHeader::begin_seqno_present`), C-ABI MINOR 16 → 17, constitution v4.3 → v4.4 (Article XI §3)

## Constitution Check

*GATE: must pass before Phase 0 and be re-checked after Phase 1.* Redone at Gate A round 1 (RC5) with each clause
quoted from `.specify/memory/constitution.md` (v4.3); it replaces research.md's table.

| Article (quoted) | Verdict |
|---|---|
| VI §5: "Every `/specify` artifact must include a **Normative References** section listing the exact `[DocAbbrev §X.Y.Z] Title` entries from the coverage index that inform the spec." | **PASS (round 1)** — spec "Normative References" lists each FIX-SL and FIX-TC entry by its exact `spec/coverage-index.md` keys; Table 3 is cited under `[FIX-SL §4.8.8]` and precedence 14/15 under `[FIX-SL §4.10]`, with the detail in prose. Every needed key exists; no coverage-index row is added. |
| VII §3: "**TDD is mandatory.** Every feature lands as red-green-refactor: failing test first, then implementation." | PASS (planned) — a RED cell before each fix; research "FR ↔ cell ↔ mutant" maps every behavioural FR to a cell and a mutant. |
| VII §5: "`tests/conformance/` holds the official FIX session-layer test cases … Every PR must pass them in CI." | PASS (planned) — `TcSeqnumTest.Tc2c_MsgSeqNumTooLow` re-pinned to TC 2c's Logout (research Pins). |
| VII §6: "v1.0 includes at least one interop test against an independent FIX implementation (QuickFIX) …" | PASS — unchanged; SC-002's live Logon cells run the default policy once the helpers stop forcing it (owner ruling 2; the live restart-and-resume run stays fixpp#503). |
| VIII §2: "**Regression budget: a slowdown greater than +5%** against the **merge-base** … measured as a **paired base-vs-candidate run on one runner**" | Measure — on the send and liveness paths, the gate's flag test; on every inbound frame, `inbound_depth_`'s increment, decrement and compare (its holder is `on_inbound_active_`'s first statement, adding no coroutine frame) and two flag reads (`acking`, `note_inbound_advance_`), plus the `logout_in_flight_` test on the Logout paths only. SC-006's two paired rows: the send path, and the inbound-frame path (an Active Heartbeat and an Active application message), base worktree path padded to equal length. Article VIII §3 ("No perf change merged without a benchmark in the same PR") is met by those rows. |
| VIII §5: "zero `new`/`delete` between parse and `fromApp` callback." | PASS (planned) — nothing is added between parse and `fromApp`; the send-path fast path is witnessed by `alloc_guard_544_we_sends_interleaved_mallocnesia` and its MemoryStore twin, proven able to fire on a planted allocation (research D5′ "Allocation"). |
| IX §1: "**Per-PR:** **≥95% line, ≥85% branch** on **touched modules** … **Binding rule — no silent uncovered error/edge path.**" | PASS (planned) — each new branch has a cell: the bound's "live" re-tests (#578 E, F1, F2), the reconnect drain's branches (G1–G4), the `close()` join (R-588-e(3)), T=0, no clock, the cancelled wait (R-588-f(3)), the store-fault release, B-6's drained lock. |
| IX §2: "**Sanitizers — Tier 1 (every PR, Linux/Clang):** ASan, UBSan, TSan must all run and pass." | PASS (planned) — R-588-h on TSan and ASan with its derived repeat count (SC-005). |
| X §1/§6: "Every change to it is reviewed against the contract; Codex Gate A is mandatory." / "ABI-affecting features trigger all four mandatory controls" | PASS — Gate A running; the four controls in the XI §7 row. |
| X §7: a breaking change "is marked **BREAKING** in the documentation of each affected declaration (in the version comment of `version.h` where no declaration carries the change), in the PR description, and in the behaviors-and-limitations delta" and "updates every in-repository consumer … in the same PR" | **PASS (round 1, N3)** — MINOR 17; a 1.17 clause on every affected declaration, derived from the behaviour changes (research D7's table: the policy enum, its setter, `fixpp_session_config_create`, `fixpp_session_config_set_reset_on_logon`, and the five observers `fixpp_session_is_established`, `fixpp_session_close`, `fixpp_session_send`, `fixpp_session_register_callback`, `fixpp_session_register_send_callback`, with the 1.12 sentences 103 falsifies amended in place); the counterparty's side in `version.h`; PR body; B-103-1; consumers per D7. |
| XI §2: "**Cancellation: ASIO native cancellation slots end-to-end.** No parallel `stop_token` abstraction." | PASS, justified — waits end by the per-operation slot or by the session's wake idiom (a timer cancel, asio-native); no `stop_token`. The bound is not bound to `root_cancel_` because its slot holds one handler, the liveness loop's (D4.4). |
| XI §3: "`fixpp::sync::async_mutex` … is the only allowed mutex shape for coroutines." | **PASS once amended (owner ruling 1, Gate A round 1)** — `detail::replay_gate` is a coroutine shared/exclusive exclusion primitive, so it conflicts with §3 as written, and Article XX §1 requires "amending the article first". The amendment rides this PR (MINOR, v4.3 → v4.4): [contracts/constitution-xi3-amendment.md](contracts/constitution-xi3-amendment.md) holds the exact text and the Sync Impact Report entry, and phase 0 applies it before any D5′ code. |
| XI §7: "Threading/concurrency-affecting features trigger all four mandatory controls (Appendix A)" | Applies — `/clarify` done; `/analyze` pending; Codex Gate A running; owner `/plan` sign-off: see below. |
| XV §1: "The "hot path" of this ban is the latency-critical **in-memory** path — parse → validate → dispatch and `MemoryStore`, which MUST stay zero-allocation per message." | PASS — the per-frame additions are `inbound_depth_`'s holder (an increment, a decrement, a compare; no coroutine frame: it sits in `on_inbound_active_`, not in the non-coroutine `on_inbound_frame`, per #544's ruling a2), a byte compare and a flag read. The existing inbound windows `alloc_guard_544_wa_active_heartbeat_mallocnesia` and `alloc_guard_544_wb_active_app_message_mallocnesia` (cases that must read zero) witness it. The contended exclusive wait sits only at `replay_outbound_range_`'s entry (a ResendRequest or a 789 walk), off the per-frame path; it is measured as evidence (research D5′ "Allocation", Codex #9). The bound's coroutine frame is one per too-high Logout. |
| XV §3: "**Coarse global session lock.**" / XV §9: "**`std::mutex` in coroutine context.**" | PASS — the gate is per session, scoped to one resend answer, and uses no `std::mutex`. |
| XVI §3/§4: "`/clarify` is MANDATORY before `/plan` …" / "`/analyze` is MANDATORY for the same trigger set" | `/clarify` done; `/analyze` pending. |
| XVII §1: Gate A triggers "Touches the public C++ API or C ABI", "concurrency / threading / cancellation / executor model", "the wire format", "the session FSM, recovery, or message store contract" | Gate A required and running (all four triggers). |
| XX §1–§3: "Any conflict … must be resolved by amending the article first (with rationale committed in the same PR)"; "Codex Gate A review on every amendment. User signs off."; "every amendment is recorded in a Sync Impact Report" | Planned — the XI §3 amendment's text is reviewed at Gate A round 2 with this bundle, then a scoped Codex Gate A on the applied clause, Gate B, and owner sign-off; the PR is titled in Article XX §2 form (contract). |

**Owner `/plan` sign-off**: GIVEN 2026-11-14 ("plan signed off"), on the bundle at `278495fb` (rebased onto `da129e17` as `d165880b`), with Article XI §3 then open; **RE-CONFIRMED 2026-11-15 on the round-1 delta** (D1's clear at `install_reconnected_transport` with the connection generation, D4's bound protocol and `close()` join, the XI §3 amendment riding 103; spec Clarifications, Gate A round 1). Appendix A control 4 is discharged for the round-1 bundle. The same day the owner ruled "drain, then reconnect" for a `drive_reconnect` from Active (research D1), which round 2 reviews.

## Project Structure

### Documentation (this feature)

```text
specs/103-resend-logon-reset-fidelity/
├── spec.md          # rulings in three Clarifications sessions
├── plan.md          # this file
├── research.md      # D1–D9, RED cells, pins, B&L delta, constitution check, risks, pass-2 amendments
├── data-model.md    # session state added or changed
├── quickstart.md    # validation scenarios and commands
├── contracts/
│   ├── c-abi-1.17.md           # the default-policy change (BREAKING)
│   ├── session-wire-endings.md # one observable ending per condition, per item
│   └── constitution-xi3-amendment.md # Article XI §3 text + Sync Impact Report (owner ruling 1)
└── checklists/requirements.md
```

### Source Code (library root)

```text
include/fixpp/session/session_config.hpp   # default policy; doc of reset_seqnum_policy, refresh_on_logon, logout_disconnect_timeout_ms
include/fixpp/session/session.hpp          # private members: peer_logout_ mark, conn_gen_, replay gate, bound join counter, wake
.specify/memory/constitution.md            # Article XI §3 amendment + Sync Impact Report (phase 0)
include/fixpp/session/resend_state.hpp     # doc: when resend state is reset (names 103)
include/fix/c_api/session.h, version.h     # (default) marker, BREAKING marks, MINOR 17 paragraph
src/session/session.cpp                    # D1–D6, D9 sites (research Sites per decision)
src/session/scan_frame_header.hpp          # FrameHeader::begin_seqno_present
src/session/<replay_gate>.hpp              # detail::replay_gate (location decided at tasks)
tests/session/test_103_*.cpp               # new RED cells (reconnect, default policy, too-high RR, too-high Logout, too-low Logout)
tests/session/replay_gate_test.cpp         # the gate alone (filter witness)
tests/session/test_538_*.cpp, test_537_gap_deadline.cpp, test_reset_seqnum_policy_matrix.cpp, …  # extended / flipped pins
tests/capi/version_test.cpp, tests/config/*, bindings/python/tests/*, tests/interop/** # consumers (D7)
spec/behaviors-and-limitations*.md, spec/feature-catalogue.md, spec/coverage-index.md, brain/components/session.md
```

**Structure Decision**: library-internal session changes plus documentation surfaces and one constitution
amendment; no new public symbol, type or error code.

## Phases (for /speckit-tasks)

Order by dependency (research R17 as amended):

0. **Constitution amendment first (owner ruling 1; Article XX §1).** Apply
   [contracts/constitution-xi3-amendment.md](contracts/constitution-xi3-amendment.md) to
   `.specify/memory/constitution.md`: the Article XI §3 sentence and the PENDING Sync Impact Report entry. No D5′ code
   (phase 6, and the bound's gate wait in phase 7) is written before this lands on the branch.
1. **Setup & census** — D8's FR-020 census: the static half with the seeded sentinel as its known positive and
   VariantC as its negative control; Pass P (non-fatal `CENSUS-HIT`, attributed to the enclosing `[ RUN ]` line)
   and Pass F in a scratch copy, each proven non-zero on the sentinel. D9's Arm-B census. The reader censuses D1
   needs (no reader of the resend state or the mark acts outside Active), D4.2's LogonReceived reachability, and
   the acceptor's single `attach_accepted_transport` per Session. The send-path allocation witness's MemoryStore
   twin measured on the base, and its planted-allocation proof. Label 103 added to every test registration 103 touches, so `ctest -L 103 --no-tests=error` selects them (quickstart §1). Results recorded in the evidence file, never in
   code comments.
2. **#578 (D1)** — RED reproduction first (arms A, B, E; G1–G10, F1, F2 and the RS-1 census after phases 6 and 7;
   G10 is the Engine-managed reader fence, owner ruling 2026-11-15); if GREEN on the base,
   close as not-a-bug with evidence. The clear and `conn_gen_` at `install_reconnected_transport`; the wakes on the
   `Disconnected` edge; doc fix. The reconnect drain (`inbound_depth_`, the quiescence wait, its bound, its closing
   and cancellation endings) lands after phase 6, because it waits on the gate's shared count.
3. **#682 (D2, D7)** — RED cells (default, strict reset, strict acceptor Logout, AS5 matrix, C ABI and TOML
   defaults); the default flip; strict reset via the `reset_on_logon` path; the acceptor refusal moved before the
   sequence check; C-ABI 1.17 with a clause on every declaration of D7's table; consumer updates; the census-found
   pins.
4. **#580 (D3)** — RED cells (7=0 byte-equal to 7=1; absent → Reject); presence flag; clamp in the walk; re-aim the
   538 slot-zero cell (R13).
5. **#577 ResendRequest half (D4.1)** — RED reproduction; `answer_resend_request_` from both too-high routes.
6. **#588 (D5′)** — after phase 0. The gate alone first (`replay_gate_test.cpp`, filter witness R-588-g, the
   contended-wait allocation evidence); then the walk's exclusive side and each emitter's shared side
   (R-588-a…f), with the pre-check in the waiting frame or the split idiom (D5′.2); R-588-h's repeat count derived
   from its mutant's measured miss rate; bench (SC-006).
7. **#577 Logout half (D4.2–D4.7)** — the mark, `note_inbound_advance_`, `run_peer_logout_bound_` with its
   generation, publish-then-re-test, join and wake sources, the claim table, the `logout_in_flight_` owner held in
   `inbound_logout_leaf_` and `run_logout_phase1`, `close()`'s poll-join, the send refusal; cells (a)…(k), R-588-e,
   LO-1–LO-4, the LS-1 census, #578 F1, F2, G2.
8. **#581 (D6, D9)** — the deadline predicate; the Heartbeat's early return removed; Arm B's Logout; B-8's
   validate-gate carve-out; flipped pins (research Pins and pass-2 list).
9. **Records** — B&L delta (retire L-029-3, L-537-1; amend B-013-1, B-013-6, B-016-1, B-537-1, B-537-2, B-538-2,
   L-028-3, L-092-1/-2, L-093-7; add B-103-1…10, L-103-2…7), catalogue and coverage-index rows, brain supersession
   flags (R16, R23), the stale `logout_disconnect_timeout_ms` comment (R15), the QFJ 3.0.1 → 3.0.2 library doc
   mentions (`tests/interop/KNOWN-LIMITATIONS.md`, `brain/components/quickfix-compat.md`), a follow-up issue for
   L-103-2 (peer-spontaneous 141=Y acknowledgement). The three comments that advertise the root slot for
   transport I/O, mutex acquire and the parser→fromApp chain are corrected to the condition (the slot has a single
   client kind, the liveness loop) and the recipe (cell RS-1), with no count, naming 103: `session.hpp`'s comment on
   `root_cancellation_slot()`, `session.hpp`'s comment saying in-flight work bound to that slot unwinds, and
   `close()`'s comment at its `root_cancel_.emit` (research D1, P3-1; loop 2 round 1, P3-4). `drive_reconnect`'s header doc: the reader fence (acceptors, and `engine_managed` initiators with a transport, a standalone `engine_managed` initiator included), the
   standalone reader precondition, single flight, the 563 §2.1a supersession, the latches, and "not bound to
   `root_cancellation_slot()`".
10. **Polish** — `/simplify`, `/speckit-verify` (Tier-1 mirror incl. MSVC build of new targets, sanitizers, SC-002's
    live Logon cells with no helper forcing a policy, and the in-process FileStore restart cell in both roles),
    Gate B, the scoped Codex Gate A on the applied XI §3 clause, owner sign-off of the amendment.

## Complexity Tracking

| Addition | Why needed | Simpler alternative rejected because |
|---|---|---|
| `detail::replay_gate` (shared/exclusive, strand-local) | FR-014 widened by the owner: in-flight sends and liveness/close frames must be ordered around an answer | a flag + send-only wait (pass-1 D5) misses both widened cases; `async_mutex` leaves #583's terminal filter and serialises sends against each other |
| `run_peer_logout_bound_` detached coroutine | FR-010's bound must fire with no inbound frame | the liveness loop returns at HB 0 and has no per-session wake; a frame-driven check cannot fire without a frame |
| one session-level Logout owner (`logout_in_flight_`), held at the two Active Logout leaves, with `close()`'s poll-join | at most one Logout, exactly one except FR-010's enumerated cases (US3, FR-010, FR-017/018) | per-site guards were the #554 per-site-patching failure class; a claim alone cannot keep `close()` from closing the transport under a claimed acknowledgement |
| `conn_gen_`, `inbound_depth_`, `reconnect_in_flight_` and the reconnect drain's operation contract | FR-006 and the owner's "drain, then reconnect": no frame of the old connection on the new one, whatever `drive_reconnect` starts from | an FSM-edge clear misses that route (Codex #7); a write fence leaves the install racing live emits; refusing from Active is a new public error path the owner did not choose |
| Article XI §3 amendment | Article XX §1: the gate conflicts with §3 as written | `async_mutex`: exclusive only, #583's terminal filter, teardown drains |

## Gate A

- Round 1 applied 2026-11-14: Codex P1=13 P2=4 P3=0; Opus post-judging P1=3 P2=8 P3=13; rewrite addresses RC1–RC5 and the three owner rulings. Reviews: research/reviews/codex_103-resend-logon-reset-fidelity_gate_a_review.md, research/reviews/opus_103-resend-logon-reset-fidelity_gate_a_adversarial_review.md.

- Loop 2 round 2 applied 2026-11-15: Codex P1=0 P2=2 P3=1; Opus post-judging P1=0 P2=2 P3=4; rewrite adds the leaf's Active entry test, rewords the XI §3 confinement clause and moves the assertion, and applies the P3s. Reviews: research/reviews/codex_103-resend-logon-reset-fidelity_gate_a_5_review.md, research/reviews/opus_103-resend-logon-reset-fidelity_gate_a_5_adversarial_review.md.
- Loop 2 round 1 applied 2026-11-15: Codex P1=0 P2=2 P3=1; Opus post-judging P1=0 P2=2 P3=6; rewrite addresses the fence predicate/order (acceptor erratum), the Logout-owner clear site, and the P3s. Reviews: research/reviews/codex_103-resend-logon-reset-fidelity_gate_a_4_review.md, research/reviews/opus_103-resend-logon-reset-fidelity_gate_a_4_adversarial_review.md.
- Loop 1 exhausted at round 3 (2026-11-15): Codex P1=1 P2=1 P3=1; Opus post-judging P1=0 P2=2 P3=5; owner chose hand-edit + one targeted round and the Engine-managed fence; pre-loop-2 edit applies RC-α, RC-β, RC-γ and the P3s. Reviews: research/reviews/codex_103-resend-logon-reset-fidelity_gate_a_3_review.md, research/reviews/opus_103-resend-logon-reset-fidelity_gate_a_3_adversarial_review.md.
- Round 2 applied 2026-11-15: Codex P1=3 P2=3 P3=1; Opus post-judging P1=0 P2=8 P3=3; rewrite addresses root causes A–D. Reviews: research/reviews/codex_103-resend-logon-reset-fidelity_gate_a_2_review.md, research/reviews/opus_103-resend-logon-reset-fidelity_gate_a_2_adversarial_review.md.

Points for loop 2 (pre-loop-2 edit): D1's reader fence (step 1a since loop 2 round 1) and the standalone precondition (G10), the root
slot's single-client invariant (RS-1); D4.6's owner at the leaf, its derivation rule and LS-1, LO-1e/f, LO-2e/f/g,
LO-4; the amendment contract's confinement as a structural check.

Points for a scoped round 3 (as recorded before round 3; the Opus round-2 recommendation: D1 and D4.6 only): D1's operation contract
(single flight and its error code, the R-4 post and the 563 §2.1a supersession, the detached-coroutine wait and its
`root_cancel_` emit, the install's commit-point refusal, the #585 boundary, the latches); D4.6's `logout_in_flight_`
owner, its emitter list and `run_logout_phase1`'s `Disconnected` early exit; cells G5–G9 and LO-1–LO-3.

Points for round 2 (as recorded before round 2): the XI §3 amendment text (contract); D1's install-site clear and `conn_gen_`; D4.4's bound
protocol and D4.6's claim table as a unit, with `close()`'s `acking` join; D1's reconnect drain (owner ruling
2026-11-15) and Risk R24; FR-010's enumerated zero-Logout cases
against each Logout leaf's failure exits; D5′'s placement rule for the pre-check; the census sentinel and the
send-path witness; D7's per-declaration BREAKING table.

### Round 1 — disagreements

The Opus review marks no finding "Disagree"; every Codex finding is confirmed, downgraded or clustered. Where Opus
rejected Codex's proposed fix shape, the Codex shape is not applied:

- **Codex #1 (cancellation race in the contended wait)** — no race exists: every emitter of a per-operation or
  root slot runs on the strand, and neither `co_await` of the hand-applied pre-check suspends, so nothing runs
  between it and the wait's `await_transform`. Codex's non-throwing primitive and its "cancel between pre-check and
  initiation" seam are not built (the seam cannot be constructed). Applied instead: the placement rule (D5′.2).
- **Codex #2 (zero Logouts from `close()`)** — the four-state owned Logout operation is not adopted; `close()`'s
  join of an `acking` bound closes the avoidable case (D4.6). Codex's second trigger, a store failure after the
  claim, is not a defect: every Logout leaf fails closed on a store fault; it is listed as FR-010 case 3.
- **Codex #4 (builder overflow)** — prevalidating CompIDs or a guaranteed-capacity buffer is not adopted: ruling
  R-12 on #537 already chose the no-Logout overflow ending for this leaf, and C-ABI 1.12 documents it. Applied: the
  carve-out in FR-010, FR-017, FR-018, E9, E12, E14.
- **Codex #5 (dictionary-invalid too-low frames)** — moving the too-low disposition ahead of dictionary validation
  is not adopted (owner ruling 3: QuickFIX C++ validates first).
- **Codex #6 (faulty/garbled frames and the deadline)** — a header-scan-level trigger is not adopted: faulty frames
  do not refresh liveness, so the gap cannot stay open without end, and FIX-SL §4.5.2 treats a garbled message as
  never received. Applied: the exclusion and its reason in FR-017 and E12.
- **Codex #9 (Article XV §1)** — not a violation: §1's hot path is parse → validate → dispatch and `MemoryStore`,
  and the exclusive wait is not on the per-frame path. A preallocated waiter representation is not required;
  the contended-wait allocation witness is added as evidence.
- **Codex #10 (stale Assumption)** — the Assumption is deleted; the optional clock-driven negative test is not
  added, because the acknowledgement path arms no timer.
- **Codex #12 (SC-002)** — absorbing fixpp#503 is not adopted (owner ruling 2).
- **Codex #13 (Normative References)** — the over-reach is not applied: Article VI §5 asks only for coverage-index
  entries, not constitution, B&L or design-note keys. Its citation of `checklists/requirements.md` does not match
  that file's text.
- **Codex #11 (census control)** — the substance is applied; the test Codex named does not exist (VariantC's real
  name is `VariantC_BilateralStrictDefault_RegressionGuardNotClean`).
- **Codex #15 (initiator Logon outside the gate)** — enforcing a lifecycle/read-pump happens-before in the API is
  not adopted; the exclusion rests on causality (D5′.4's row), with the shared hold as the named fallback.
- **Codex #16 (traceability)** — removed-text mutants for documentation FRs are not adopted: FR-007 and FR-019 are
  documentation and records and need no behavioural mutant. The traceability table and the missing FR-011 cell
  are added.
- **Codex #17 (data model)** — a first-class owned state object is not adopted; the connection generation and the
  publish-then-re-test rule carry the bound's safety (data-model.md).
- **Codex #14 (`acking` drain vs FR-011)** — continuing to service administrative traffic until the
  acknowledgement is durably committed is not adopted; FR-011 is narrowed to the pending phase.

### Round 2 — disagreements

The Opus round-2 review marks no finding "Disagree" and downgrades Codex's three P1s to P2. Where it rejected
Codex's fix shape, the Codex shape is not applied:

- **Codex #2 (reconnect lifecycle)** — Codex's attempt token "rechecked after every suspension" carrying the
  caller's cancellation identity through backoff, connect and handshake is not adopted: cancellation there is
  fixpp#585's (B-538-5, brain R-22). Applied instead: the install as the atomic commit point for `close()` and stop,
  with the refused transport closed, and the `dispatch_aborted` clause scoped to the drain's wait.
- **Codex #3 (concurrent calls)** — "join the existing attempt" and a per-attempt generation checked at Logon
  emission are not adopted: single flight with a documented existing error (`session_already_open`) covers the
  whole operation, including the Logon.
- **Codex #4 (`inbound_depth_`'s holder)** — a move-only depth token passed into each arm, and a per-return-family
  table, are not adopted: the holder is `on_inbound_active_`'s first statement, which covers every return path, an
  exception and frame destruction with one RAII object and no new frame. Cell G9 covers destruction.
- **Codex #6 (`bound_done`)** — a modelled `bound_done_wake_` member with its own publish / re-test / clear protocol
  is not adopted: Opus's fix shape (a) removes `bound_done`, and every waiter polls the shared counter by post.
- **Codex #5 (inbound bench)** — "both callback and no-callback rows, MemoryStore where relevant" is narrowed to two
  rows (an Active Heartbeat, which reaches no application callback, and an Active application message, which reaches
  `fromApp`), the shapes the existing inbound allocation windows already drive.

### Loop 2 round 1 — disagreements

The Opus review confirms both Codex P2s and the P3 and marks nothing "Disagree". Two Codex fix shapes are not
applied, as the judge rejected or did not prefer them:

- **Codex P2-1 (a dedicated Engine-owned / reader-owned property, or setting `engine_managed` for acceptors too)**
  — not adopted. The judge's main fix is the role disjunct `cfg_.role == acceptor` before `engine_managed &&` a
  transport installed: it implements the owner's stated condition with no new mechanism and no change to
  `engine_managed`'s initiator-only contract. Its one false positive, a standalone initiator that sets
  `engine_managed` to defer its connect, is disclosed (B-103-9, header doc). The Engine-only marker
  (`session_engine_access::mark_pump_owned`) stays the named alternative if the owner prefers it at sign-off.
- **Codex P2-2 (a three-state owner, `none | inbound_ending | graceful_phase1`)** — not adopted. A boolean set at
  the two leaves and cleared only on the edge into `Disconnected` spans the whole ending; clearing phase 1's
  ownership early at an inbound ending's `Disconnected` loses nothing, because no Logout emitter can start after
  that edge (D4.6 "The clear").

### Loop 2 round 2 — disagreements

The Opus review downgrades Codex P2-1 to P3, confirms Codex P2-2 narrowed, and marks nothing "Disagree". Codex's
two fix directions are not applied, because the trigger they defend against has no supported path (research D1's
lifetime rule: a frame is destroyed mid-body only while its execution context is destroyed, after which no handler
of the session runs):

- **Codex P2-1: an abandonment-safe Logout owner** (an RAII guard that clears `logout_in_flight_` at frame
  destruction) **and its LO-6 "destroy the frame and verify a terminal disposition"** — not adopted. In the leaf it
  would clear at the leaf's return and reopen LO-5's window; at `on_inbound_active_` it never acts on a resumed path;
  at teardown nothing reads the owner again, so it would be a clear with no killing cell. The cell would build the
  unsupported continuation. Applied instead: the text no longer asserts the trigger (G9 rewritten as a teardown
  witness, D4.6's "every resumed path"). The LO-6 in research is a different cell (RC-θ's Trace A).
- **Codex P2-2: a destruction-safe gate** (making off-domain destructor releases race-free) — not adopted. No race is
  reachable: at teardown no handler of the domain runs, and a Session that owns the gate's timer is destroyed before
  its context. Applied instead: the XI §3 clause reworded to the true statement, the strand assertion moved to the
  acquire sites and `wake_all`'s acting branch, and G9 as the teardown witness.

