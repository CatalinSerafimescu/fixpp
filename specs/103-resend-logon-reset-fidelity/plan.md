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
left by the new gate), Article XI §3 (no coarse session mutex), one observable ending per condition
**Scale/Scope**: ~15 session functions changed (research Sites lists), two new private helpers plus one coroutine
(`note_inbound_advance_`, `answer_resend_request_`, `run_peer_logout_bound_`), one new internal primitive
(`detail::replay_gate`), one internal header field (`FrameHeader::begin_seqno_present`), C-ABI MINOR 16 → 17

## Constitution Check

*GATE: must pass before Phase 0 and be re-checked after Phase 1.* The full table is research.md "Constitution
check" as amended by "Pass-2 amendments". Summary:

| Article | Verdict |
|---|---|
| VI §5 normative references | PASS — spec "Normative References" added (FIX-SL §§, Table 3, §4.10; TC 1B, 1S, 2c, 14b, 20) |
| VII §3 test-first | PASS — a RED cell and a mutant per fix (research RED cells) |
| VIII §2/§5 performance | Measure — the gate's flag on send/liveness and two flag reads on the inbound path; SC-006 paired bench + the send-path allocation witness |
| IX §1/§2 coverage, sanitizers | PASS (planned) — every new branch has a cell; R-588-h under TSan/ASan |
| X §1/§6/§7 C ABI | PASS — MINOR 17, BREAKING marks on `fixpp_reset_seqnum_policy` and its setter, PR body, B&L; in-repo consumers updated (D7) |
| XI §2 cancellation | Justified — waits are woken by timer cancel (the session's wake idiom), not a cancellation slot, because `root_cancel_`'s one slot belongs to the liveness loop (D4.4) |
| XI §3 no session mutex | **Needs Gate A ruling** — the replay gate is strand-local (no thread exclusion, no `std::mutex`, no `async_mutex`); argued in D5′ |
| XI §7, Appendix A | All four controls apply: `/clarify` (done), `/analyze`, Codex Gate A, the owner's `/plan` sign-off |
| XV §1 allocation | PASS — hot path untouched; the bound's coroutine frame and contended waits are cold |
| XVI §3/§4 | `/clarify` done; `/analyze` mandatory |
| XVII §1 | Gate A required (C ABI, concurrency, session FSM and recovery, wire) |

**Owner `/plan` sign-off**: GIVEN 2026-11-14 ("plan signed off"), on the bundle at `278495fb` (rebased onto `da129e17` as `d165880b`; the squash tree equals 102's head tree, so no anchor moved). Appendix A control 4 discharged.

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
│   └── session-wire-endings.md # one observable ending per condition, per item
└── checklists/requirements.md
```

### Source Code (library root)

```text
include/fixpp/session/session_config.hpp   # default policy; doc of reset_seqnum_policy, refresh_on_logon, logout_disconnect_timeout_ms
include/fixpp/session/session.hpp          # private members: peer_logout_ mark, replay gate, bound join counter, wake
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

**Structure Decision**: library-internal session changes plus documentation surfaces; no new public symbol, type or
error code.

## Phases (for /speckit-tasks)

Order by dependency (research R17 as amended):

1. **Setup & census** — D8's FR-020 census (static half with its known positive; measured Pass P poison-default and
   Pass F flip in a scratch copy) and D9's Arm-B census; caller censuses for `drive_reconnect` (D1) and for
   `emit_initiator_logon_` vs a live inbound chain (D5′ R21). Results recorded in the evidence file, never in code
   comments.
2. **#578 (D1)** — RED reproduction first; if GREEN on the base, close as not-a-bug with evidence. Clear on the
   `Disconnected` edge; doc fix.
3. **#682 (D2, D7)** — RED cells (default, strict reset, strict acceptor Logout, AS5 matrix, C ABI and TOML
   defaults); the default flip; strict reset via the `reset_on_logon` path; the acceptor refusal moved before the
   sequence check; C-ABI 1.17; consumer updates; the census-found pins.
4. **#580 (D3)** — RED cells (7=0 byte-equal to 7=1; absent → Reject); presence flag; clamp in the walk; re-aim the
   538 slot-zero cell (R13).
5. **#577 ResendRequest half (D4.1)** — RED reproduction; `answer_resend_request_` from both too-high routes.
6. **#588 (D5′)** — the gate alone first (`replay_gate_test.cpp`, filter witness R-588-g); then the walk's exclusive
   side and each emitter's shared side (R-588-a…f); sanitizer repeat (R-588-h); bench (SC-006).
7. **#577 Logout half (D4.2–D4.7)** — the mark, `note_inbound_advance_`, `run_peer_logout_bound_` with its join and
   wake, the claim table, the send refusal; cells (a)…(i) and R-588-e.
8. **#581 (D6, D9)** — the deadline predicate; the Heartbeat's early return removed; Arm B's Logout; flipped pins
   (research Pins and pass-2 list).
9. **Records** — B&L delta (retire L-029-3, L-537-1; amend B-013-1, B-013-6, B-016-1, B-537-1, B-537-2, B-538-2,
   L-028-3, L-092-1/-2, L-093-7; add B-103-1…10, L-103-2…7), catalogue and coverage-index rows, brain supersession
   flags (R16, R23), the stale `logout_disconnect_timeout_ms` comment (R15), the QFJ 3.0.1 → 3.0.2 library doc
   mentions (`tests/interop/KNOWN-LIMITATIONS.md`, `brain/components/quickfix-compat.md`), a follow-up issue for
   L-103-2 (peer-spontaneous 141=Y acknowledgement).
10. **Polish** — `/simplify`, `/speckit-verify` (Tier-1 mirror incl. MSVC build of new targets, sanitizers, live
    interop SC-002 with no helper forcing a policy), Gate B.

## Complexity Tracking

| Addition | Why needed | Simpler alternative rejected because |
|---|---|---|
| `detail::replay_gate` (shared/exclusive, strand-local) | FR-014 widened by the owner: in-flight sends and liveness/close frames must be ordered around an answer | a flag + send-only wait (pass-1 D5) misses both widened cases; `async_mutex` leaves #583's terminal filter and serialises sends against each other |
| `run_peer_logout_bound_` detached coroutine | FR-010's bound must fire with no inbound frame | the liveness loop returns at HB 0 and has no per-session wake; a frame-driven check cannot fire without a frame |
| claim table across five Logout endings | exactly one Logout (US3, FR-017/018) | per-site guards were the #554 per-site-patching failure class |

## Gate A

Not yet run. Points for the reviewers (research Risks): the claim table D4.6 as a unit (R6); D5′'s lock order and
deadlock analysis; the XI §3 ruling on the gate; D2.4's move of the strict acceptor's refusal (R14); D6's reading of
"any MsgSeqNum" excluding faulty and garbled frames (R12); writer preference (R18) and the inbound stall bound (R19).
