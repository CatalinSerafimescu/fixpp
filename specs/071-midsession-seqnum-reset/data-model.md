# Phase 1 Data Model: Mid-Session Sequence-Number Reset Originator (071)

This feature adds no wire fields and no new persisted schema. The "data model" is the session-object state it introduces/reuses and the FSM edge it adds.

## New session state (added members)

| Member | Type | Location | Purpose | Lifecycle |
|---|---|---|---|---|
| `midsession_reset_in_progress_` | `bool` (default `false`) | `include/fixpp/session/session.hpp` (near `own_logon_sent_reset_flag_` :759) | The mid-session-reset marker. Drives the two-edge callback policy in `record_state_transition_` (suppress onLogout on Active→LogonSent; force onLogout on LogonSent→terminal — FR-015/R1) AND discriminates the ack arm's outbound-restore branch (FR-004/R2). | Set true synchronously at the trigger, **before** the `Active→LogonSent` transition and before the first `co_await` (FR-014). Cleared on **every** exit edge — success (→Active) and failure (→any terminal) — and the clear runs **before** the `application==nullptr` early return (`:233`) so app-less sessions do not leak it (Gate A P3). |
| `liveness_gen_` | `std::uint32_t` (default `0`) | `include/fixpp/session/session.hpp` (near the liveness spawn state) | Generation token so a superseded `run_liveness_loop` exits when the reset re-enters Active (R6). | Incremented at each Active-entry spawn; each loop captures its value and runs while `fsm_state_==Active && my_gen==liveness_gen_`. |

## New error value

| Value | Enum | Location | String |
|---|---|---|---|
| `session_invalid_state_for_reset` | `fixpp::core::error` | `include/fixpp/core/error.hpp` (next non-renumbering slot; `to_string` case ~:912) | `"session: invalid state for reset"` |

C++ `core::error` only — **not** a C-ABI `fixpp_error_t` value (no C-ABI change; FR-008).

## Reused machinery (mostly intact; ack arm + transition helper minimally extended)

| Symbol | Location | Role in 071 |
|---|---|---|
| `reset_seqnums_to_one_durable(disposition)` | `src/session/session.cpp:595` | Durable {1,1} reset at emit time (fatal-when-persistent). Value-idempotent; the ack arm calls it again safely. |
| `emit_initiator_logon_` (reference only) | `src/session/session.cpp:777-914` | Template for the new `emit_midsession_reset_logon_`; the `:818-865` core is mirrored, the hydrate/one-shot preamble is omitted. |
| `build_logon(..., reset_seqnum_flag=Y, ...)` | `src/session/session.cpp:877` | Builds the reset Logon at outbound seq 1. |
| `store_then_emit` | (admin emit path) | Transmits the reset Logon on the live transport. |
| `assign_outbound()` | `src/session/seqnum_manager.cpp:101` | Advances outbound 1→2 after the reset Logon. |
| `own_logon_sent_reset_flag_` | `session.hpp:759`, set `session.cpp:865` | Emit-time latch; gates the ack arm's outbound restore. |
| initiator ack arm (`peer_ack_sent_reset_flag`) | `src/session/session.cpp:3861-3975` | Consumes the peer's confirming Logon(141=Y): re-reset + restore to {2,2} + →Active. Reused intact **except** a minimal additive branch (FR-004/R2): when `midsession_reset_in_progress_`, restore outbound to 2 iff `peek_outbound()==seqnum_min+1`, else fail closed; connect-time predicate unchanged. |
| `record_state_transition_` | `src/session/session.cpp:217-258` | The single transition helper; gains the `midsession_reset_in_progress_`-gated two-edge callback policy (FR-015): suppress onLogout on Active→LogonSent, force onLogout on LogonSent→terminal, flag-clear before the `application==nullptr` early return. |
| `reconnect_fsm_.is_awaiting_resend()` | `src/session/reconnect_fsm.cpp:443` | Trigger guard conjunct (R4). |
| `cfg_.role` (`session_role::initiator`) | `include/fixpp/session/session_config.hpp` | Trigger guard conjunct — the feature is initiator-only (FR-007/R4). |
| LogonSent inbound handler | `src/session/session.cpp:3731-3778` | Marker-gated tolerance branch (FR-017/R11): when `midsession_reset_in_progress_`, a non-Logon inbound is drained (HB/TR) or dropped-as-abandoned (app) instead of `record_state_transition_(Disconnected)` (`:3776`); Logon→ack arm, Logout→terminal. Connect-time (marker unset) unchanged. |

## FSM edge added

```
    reset_sequence_numbers()  [guard: role==initiator && Active && !is_awaiting_resend()]
    (set flag; transition BEFORE first co_await — FR-014; suppress onLogout on this edge)
Active ───────────────────────────────────────────────► LogonSent
   ▲                                                         │
   │  peer Logon(141=Y)  [ack arm; restore→{2,2} or fail-closed; clear flag; NO onLogon]
   └─────────────────────────────────────────────────────────┘
   │  no confirm (transport EOF / store-reset fail / app close) — NO dedicated timeout (L-071-3)
   └────────────────────────► Disconnected  [FORCE onLogout once (FR-015); clear flag]
```

- New edge: **Active → LogonSent** (the only transition leaving Active for LogonSent; today Active only leaves to LogoutSent/Disconnected). onLogout suppressed on this edge (flag set).
- Return edge **LogonSent → Active** (success): existing ack-arm transition (`:4110`); onLogon NOT re-fired (still latched); flag cleared.
- Failure edge **LogonSent → terminal**: **onLogout forced once** while the flag is set (would otherwise be silently skipped since `was_active` is false — Gate A New-P1); flag cleared. No dedicated timeout drives this — it fires on transport EOF, store-reset failure, or application close (L-071-3).

## State/counter invariants (validated by witnesses)

- **INV-071-1**: after a successful reset handshake, `next_outbound == 2 && next_inbound == 2` (the next app send carries seq 2; no duplicate seq 1). (FR-005)
- **INV-071-2 (success callback)**: across a **successful** reset the application observes **neither** onLogon nor onLogout; the `onLogon_fired_`/`onLogout_fired_` latches are preserved so a later real teardown still fires onLogout exactly once. (FR-015/R1)
- **INV-071-2b (failure callback)**: across a **failed** reset (LogonSent→terminal while the reset is outstanding — non-confirmation EOF, store-reset failure, or app close) the application observes onLogout **exactly once** (no silent session death). (FR-015/R1)
- **INV-071-3**: exactly one `run_liveness_loop` is active after the handshake completes, and the generation guard is behavior-preserving for existing flows. (FR-016/R6)
- **INV-071-4**: with the trigger never invoked, all existing dispositions/bytes are unchanged (the three shipped reset suites pass unchanged). (FR-006)
- **INV-071-5**: the emit path performs no heap allocation (extends `ResetKnobs_NoHeapOnResetPath`). (Article XV §1)
- **INV-071-6 (dup-seq closed)**: if a non-reset outbound frame is emitted in the LogonSent window (e.g. a validation-Reject of a malformed confirming Logon), the ack arm never silently leaves `next_outbound==1` — it restores to 2 via the latched-fact/peek check or fails the handshake closed. (FR-004/R2)
- **INV-071-7 (connection preserved through peer liveness)**: a peer `Heartbeat`/`TestRequest` arriving in the LogonSent reset window does NOT close the transport; the reset completes on the subsequent confirming `Logon`. (FR-017/R11 — the SC-001 headline)
