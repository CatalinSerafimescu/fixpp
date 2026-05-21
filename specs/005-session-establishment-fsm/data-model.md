# Phase 1 — Data Model — 005-session-establishment-fsm

Entities, the `[FIX-SL §4.10]` transition matrix, error-slot allocation, and the `[2d §4.5]` SessionConfig fields this feature consumes/owns-the-values-of. Anchors: 2e v0.4, 2d v0.4, 2f v1.5, `[FIX-SL]`. On conflict the anchor wins.

## E1 — `Session`

One FIX session with one counterparty (one `BeginString` + SenderCompID/TargetCompID identity). Owns: the FSM state (E2), the sequence-number state (E3), the timer set (heartbeat / test-request / graceful-close), the resolved `effective_clock` (D-5), and a `std::unique_ptr<MessageStore>` (the `[2e §4.1]` seam, consumed — E6). Lifecycle: `open()` (resolve effective_clock, validate config, start FSM), `close(graceful|terminal)` (two-phase per `[2d §6.5]`, idempotent), teardown. **No** multi-session registry / acceptor demux (one Session = one counterparty pair — D-11). Emits **no** `extern "C"` symbol (FR-015).

## E2 — Session FSM state (`session_fsm`)

`enum class fsm_state : std::uint8_t` — `NotConnected=0`, `LogonSent`, `LogonReceived`, `Active`, `LogoutSent`, `Disconnected`. **6 states — no `RecoveryPending`** (removed Session-2026-05-18 / D-2: the bound oracle requires an outbound `ResendRequest(35=2)` on a too-high gap, which belongs to the deferred session-recovery feature; a passive hold-state is not green-able in isolation and not what any surveyed engine does). A too-high `MsgSeqNum` is a session-fatal transition. Single-writer on the per-session strand.

### Transition matrix (`[FIX-SL §4.10]`; every cell defined — no UB; FR-001)

Event alphabet (FR-001): `open(initiator)` | inbound `Logon` valid | inbound `Logon` refused | inbound `Heartbeat` | inbound `TestRequest` | inbound `Reject` | inbound `Logout` | inbound out-of-scope admin (`ResendRequest`/`SequenceReset`) | seqnum in-seq | seqnum too-low (no PossDup) | seqnum too-high | dup-`Logon` while Active | simultaneous-logon resolution | invalid `MsgType` / type-invalid-for-state | heartbeat/TR/close timer tick | `initiate Logout` | `close(terminal)`/fatal. The matrix below collapses the per-message-content events into the seqnum/validation columns; the per-cell action (emitted message, counter effect, callback dispatch) is given in the cell. Guard precedence at message arrival: (1) parse/type recognized → else session `Reject`; (2) CompID/BeginString gate (logon states) → else refuse; (3) `SendingTime(52)` MaxLatency vs effective clock (Clarification Q3: `Reject` reason 10 → `Logout` → disconnect; Logon → logout-with-error); (4) seqnum class (too-low/too-high/in-seq); (5) message-type-for-state. **`(drained)` (LogoutSent row) is a defined transition, not a gap:** the inbound message is accepted off the wire and discarded — the FSM **remains in `LogoutSent`**, the next-expected-inbound seqnum is **not** advanced, the message is **not** surfaced via `fromAdmin`/`fromApp`, and **no** message is emitted (no `Reject`, no counter effect). Only the non-`(drained)` LogoutSent cells transition: inbound `Logout` → `Disconnected` (confirm) and the graceful-close timer (child slot) → `Disconnected`. This keeps seam #1's "every cell defined/testable" total.

| From \ Event | open (initiator) | inbound Logon (valid) | inbound Logon (refused: BeginString/CompID/not-1st) | inbound Heartbeat | inbound TestRequest | inbound Reject | inbound Logout | out-of-scope admin (RR/SeqReset) | seqnum in-seq | seqnum too-low (no PossDup) | seqnum too-high | invalid MsgType / type-invalid-for-state | heartbeat/TR timer | initiate Logout | close(terminal) / fatal |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| **NotConnected** | → LogonSent (emit Logon, seq=1, SendingTime) | → LogonReceived → Active (reply Logon, agreed HeartBtInt) | refuse, → Disconnected (no Active) | refuse, → Disconnected (1st msg ≠ Logon, `[FIX-SL §4.3]`) | refuse, → Disconnected | refuse, → Disconnected | → Disconnected (`[FIX-SL §4.6]`) | refuse, → Disconnected | n/a | n/a | n/a | refuse, → Disconnected | n/a | → Disconnected | → Disconnected |
| **LogonSent** | idem (idempotent) | → Active (validate HeartBtInt/CompID/BeginString) | refuse, → Disconnected | session-fatal Logout+disconnect (unexpected pre-Active) | session-fatal Logout+disconnect | session-fatal Logout+disconnect | → Disconnected (`[FIX-SL §4.6]`) | session-fatal Logout+disconnect | (validated post-Logon-ack) | **fatal** Logout(text)+disconnect | **fatal** Logout(text)+disconnect (recovery deferred) | session-fatal Logout+disconnect | Logon-timeout → Disconnected | → LogoutSent | → Disconnected |
| **LogonReceived** | n/a | (already acked) | refuse, → Disconnected | → Active (advance) | → emit Heartbeat(echo TestReqID), → Active | session `Reject` (no loop), → Active | → Disconnected | session `Reject` (RR/SeqReset deferred), → Active | → Active (advance) | **fatal** Logout(text)+disconnect | **fatal** Logout(text)+disconnect (recovery deferred) | session `Reject` | → Active | → LogoutSent | → Disconnected |
| **Active** | n/a | dup-Logon per `[FIX-SL §4.3]` (no silent re-establish; session `Reject`) | n/a | advance counter (liveness) | → emit Heartbeat(echo) | session-level log, no `Reject` of a `Reject` (I-5) | → emit Logout, → Disconnected | session `Reject` (RR/SeqReset deferred — `session_admin_not_supported`) | advance counter, dispatch fromAdmin/fromApp | **fatal** Logout(text)+disconnect (`[FIX-SL §4.1]`) | **fatal** Logout(text)+disconnect, surface `session_seqnum_gap_unrecoverable` (recovery deferred — Session-2026-05-18) | session `Reject`(`SessionRejectReason`) | emit Heartbeat / emit TestRequest / unanswered→unhealthy→disconnect | → emit Logout, → LogoutSent | → Disconnected |
| **LogoutSent** | n/a | (drained) | n/a | (drained) | (drained) | (drained) | → Disconnected (confirm) | (drained) | (drained) | (drained) | (drained) | (drained) | graceful-close timeout (child slot) → Disconnected | idem (idempotent) | → Disconnected |
| **Disconnected** | `session_already_closed` | ignored | ignored | ignored | ignored | ignored | ignored | ignored | ignored | ignored | ignored | ignored | none | `session_already_closed` | idem (idempotent) |

Receipt of `ResendRequest`/`SequenceReset` admin types in 005 is a **defined, bounded** transition (session-level `Reject` / state disposition above, surfaced as `session_admin_not_supported`), never undefined and never a silent no-op — the real recovery FSM (ResendRequest issuance, SequenceReset-GapFill) is the deferred session-recovery feature's, forward-referenced at the `[2e §3.1]`/`[2e §4 last bullet]` Phase-4 hand-off.

## E3 — Sequence-number state

Next-expected **inbound** `seqnum_t` and next **outbound** `seqnum_t` (E4). Increment-by-one on accepted/sent admin messages, advanced under the durable-before-transmit `MessageStore` ordering (E6). Detect: in-sequence (advance), too-low + no `PossDupFlag=Y` (**session-fatal** Logout+disconnect, `[FIX-SL §4.1]` ordered-sequence integrity, oracle `2c_MsgSeqNumTooLow`; PossDup S-010 out of scope → treated as the no-PossDup case), too-high (**session-fatal** Logout-with-text+disconnect, surface `session_seqnum_gap_unrecoverable` — recovery deferred, Session-2026-05-18; the bound oracle's `ResendRequest(35=2)` belongs to the deferred session-recovery feature). Overflow at `seqnum_max` ⇒ **session-fatal, no wrap**, surfaced via `[2e §6.7] store_seqnum_overflow` through a session-level error callback (`[2e §7.6]` N6). Counter bookkeeping serialized by `fixpp::sync::async_mutex` (D-7; defence-in-depth, structurally zero contention).

## E4 — `seqnum_t` (005-OWNED — D-1)

`include/fixpp/session/seqnum.hpp`, `namespace fixpp::session`: `using seqnum_t = std::uint32_t;`, `inline constexpr seqnum_t seqnum_min = 1;` (`[FIX-SL §4.1]`), `inline constexpr seqnum_t seqnum_max = std::numeric_limits<std::uint32_t>::max();`. Promotes the `[2e §4.7]` placeholder **in place** (same path; 2e includes resolve unchanged; `[2e §10 Q9]` handoff closed, `[const §VI.5]`). Consumed by `2e`'s `MessageStore` seam and downstream session work.

## E5 — Admin messages (interpreted over the merged wire/dict surfaces)

`Logon(35=A)` / `Logout(35=5)` / `Heartbeat(35=0)` / `TestRequest(35=1)` / `Reject(35=3)`. Built via `wire::Writer`, parsed/typed-accessed via the merged `dictionary/` surface (PR #66/#67) + `wire/` (PR #68) — 005 interprets **session semantics** only; it does not re-implement framing/parse/serialize. Fields owned semantically here: `MsgSeqNum(34)`, `HeartBtInt(108)`, `SenderCompID(49)`/`TargetCompID(56)`, `BeginString(8)`, `SendingTime(52)`, `TestReqID(112)`, `Reject` refs `RefSeqNum(45)`/`RefTagID(371)`/`RefMsgType(372)`/`SessionRejectReason(373)`.

## E6 — MessageStore seam (CONSUMED — `[2e §4.1]`, not redefined)

The 4-pure-virtual `store`/`retrieve`/`next_seqnum`/`reset` + `retrieve_visitor` at `include/fixpp/session/message_store.hpp` (`fixpp::session`). 005 exercises `store()` (durable-before-transmit) + `next_seqnum()` only; `retrieve()`/visitor walk = deferred recovery feature; `reset()` not called (S-017 deferred). A `tests/support/store_double.hpp` satisfies it (NOT S-012). Ordering/callsites per `[2e §root cause #1]`/`[2e §7.6]` (D-4).

## E7 — Effective clock (CONSUMED — `[2d §4.1]`)

`effective_clock = SessionConfig::clock_override ?: EngineConfig::clock`, resolved once at `Session::open` (`[2d §7.9]`, NFR-015). `steady_now()` = elapsed; `now()` = wire SendingTime + MaxLatency check; `sleep_until()` = timers. `mock_clock` (`[2d §4.3]`) for deterministic tests. Never a direct wall-clock call (FR-011).

## E8 — Time helper #4 (folded `core/` row — D-3)

`include/fixpp/core/fix_time.hpp`: reuse `fixpp::core::utc_time_point` (`[2d §4.1]`), add `fixpp::core::duration`. `utc_time_to_fix_string(utc_time_point, precision) → fixed buffer`, `fix_string_to_utc_time(span<const char>) → expected_t<utc_time_point>`. Grammar `YYYYMMDD-HH:MM:SS[.sss[sss]]`; ms default (FIX 4.x), µs where version permits, never coarser than seconds; round-trip lossless at emitted precision. Closes `core/` module-exit row #4.

## E9 — SessionConfig (CONSUMED — `[2d §4.5]`; values 005 owns per D-8)

Consumed fields: `executor_override`/`mode`/`locks` (threading), `clock_override` (E7), `sender_comp_id`/`target_comp_id`/`begin_string` (identity — owned by session-module spec = 005), `store_factory` (`unique_ptr<MessageStoreFactory>`), `dictionary`/`dialect_overlay`. **Values 005 owns** (D-8): `heartbeat_interval`=30 s, `test_request_threshold`=1×HeartBtInt, `sending_time_threshold`(MaxLatency)=120 s, `reject_policy`=`strict_reject_then_logout`. `std::nullopt` ⇒ engine substitutes the 005 fallback at `Session::open`. Additionally 005 owns the **graceful-close (Logout) timeout** as a fixed bound (**2 s**, QuickFIX `LogoutTimeout` default; *not* a `[2d §4.5]` optional, D-6/D-8) — the phase-1 `Clock::sleep_until` close window; on expiry the session force-disconnects → `Disconnected` and surfaces `session_logout_timeout` (slot 73, `[FIX-SL §4.6]`). 005 consumes the config shape; it does not redesign it (FR — Key Entities).

## Error mapping — `fixpp::core::error` slots 66..N (D-9; `[const §X.4]`)

Occupied before T005 (006/007/008 already pinned): 1, 10–13, 20–29, 30–42 (core/wire/dictionary), 43–46 (006 sync), 47–55 (007 threading incl. the `[2d §6.5]/[2d §6.7]` cross-doc set `clock_sleeps_cancelled=49`, `session_already_open=51`, `session_already_closed=52`, `invalid_session_config=53`, `dispatch_aborted=55`), 56–65 (008 store) → first free contiguous slot for 005's session_* set = **66**. Appended non-renumbering, per-doc-prefix `FIXPP_ERR_SESSION_*` (C-ABI coalescing owned by 2i):

| Variant | Slot | Source | Class |
|---|---|---|---|
| `session_invalid_logon` | 66 | FR-003/004, US1#3/#4, `[FIX-SL §4.2]`/`§4.3` | refusal — no Active |
| `session_compid_mismatch` | 67 | FR-004, `[FIX-SL §4.2.2]` | refusal |
| `session_begin_string_unsupported` | 68 | FR-003, `[FIX-SL §4.2.1]` | refusal |
| `session_seqnum_too_low` | 69 | FR-008, `[FIX-SL §4.1]` | session-fatal (no PossDup) |
| `session_seqnum_gap_unrecoverable` | 70 | FR-008/FR-001, Session-2026-05-18 (recovery deferred) | session-fatal (too-high; replaces the removed `session_recovery_pending`) |
| `session_sending_time_accuracy` | 71 | Clarification Q3, FR-013, `[FIX-SL §4.2.3]` | `SessionRejectReason=10` |
| `session_msg_type_invalid_for_state` | 72 | FR-007, `[FIX-SL §4.5.4]` | session-reject |
| `session_logout_timeout` | 73 | FR-005, `[FIX-SL §4.6.2]` | graceful-close force-disconnect |
| `session_test_request_unanswered` | 74 | FR-006, `[FIX-SL §4.5.5]` | liveness unhealthy → disconnect |
| `session_admin_not_supported` | 75 | FR-017, `[FIX-SL §4.10]` | deferred admin (RR/SeqReset) bounded reject |
| `session_invalid_config` | 76 | `[2d §4.5]` N-P2-3 / Session::open validation | configuration |

**Slot allocation pinned at T005** (Phase 2 /speckit-implement, 2026-05-21). Pre-implementation drafts of this table cited 43..53 + 54..N; that range was already occupied by 006/007/008 merges, so the 005 set landed at 66..76 per `[const §X.4]` non-renumbering. The cross-doc-coordinated `[2d §6.5]/[2d §6.7]` triple (`session_already_closed=52`, `dispatch_aborted=55`, `clock_sleeps_cancelled=49`) was already pinned by 007/2d — 005 reuses, does not duplicate. Overflow ⇒ **reuse** `[2e §6.7] store_seqnum_overflow` (no new variant). Pre-v1.0 the table remains revisable subject to `[const §X.4]`; on a tagged C-ABI release the numeric values freeze permanently.

## Invariants

- **I-1** Every `[FIX-SL §4.10]` state×event cell maps to a defined transition (no implicit/undefined) — seam #1.
- **I-2** `next-expected-inbound` advances by exactly 1 per accepted message; `next-outbound` by exactly 1 per sent; zero drift over a long run — seam #3.
- **I-3** Outbound: `store(seq, committed_span, outbound)` completes **before** `transport::async_write` is invoked; a cancelled transmit leaves **no** persisted-but-unsent inconsistency vs `[2e §root cause #1]` — seam #10.
- **I-4** A too-high `MsgSeqNum` is a session-fatal transition: the gap is surfaced (`session_seqnum_gap_unrecoverable`), an orderly Logout-with-text is emitted, the session disconnects, and **no** ResendRequest/SequenceReset is emitted by 005 (recovery deferred — Session-2026-05-18) — seam #4. There is no `RecoveryPending` state and no held-message buffer.
- **I-5** No `Reject`/`Logout` is itself rejected (no reject loop) — seam #7.
- **I-6** Every emitted message carries a grammar-exact `SendingTime(52)`; format→parse round-trips losslessly at emitted precision — seam #9.
- **I-7** Zero global `new`/`delete` on inbound-dispatch / timer-fire / seqnum paths; `noexcept` across that window; throwing user callback traps — seams #10/#12.
- **I-8** `seqnum_max` ⇒ session-fatal, no wrap — seam #3.
- **I-9** `Session::close()` idempotent; graceful Logout + close-timeout run under a child cancellation state; phase-2 root total only after phase-1 — seam #11.
- **I-10** `2e`'s `<fixpp/session/seqnum.hpp>` include resolves to the 005-owned `seqnum_t` post-merge (build/consumer check) — seam #13, SC-010.
