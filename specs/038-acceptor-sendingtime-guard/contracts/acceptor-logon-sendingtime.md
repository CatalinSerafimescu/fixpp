# Internal Contract — Acceptor first-Logon SendingTime guard (038)

Not a public API contract (no surface change). This pins the behavioural contract the witnesses assert. Scope: `src/session/session.cpp` (Group 1), `src/session/reconnect_fsm.cpp` (Group 2), test-only (Group 3).

## C-1 — Acceptor first-Logon SendingTime disposition (Group 1)

**Precondition**: acceptor in `fsm_state::NotConnected`; inbound frame is a Logon (35=A) that passes `interpret_logon` (BeginString/CompID/HeartBtInt). The guard fires AFTER `ensure_hydrated_` (`:1925`, read-only outbound/inbound seed) but BEFORE `reset_on_logon` (`:1927`) and `check_inbound` (`:1936`) — so the Reject carries the hydrated durable outbound seq while inbound is seeded-not-advanced and no durable reset fires — and therefore before the FIXT `1137` validation and `authorize_logon(553/554)`; `effective_clock_` is set. (A bad `52` thus pre-empts the seqnum, `1137`, and credential gates — see data-model "simultaneous-bad ordering".) Order: `interpret_logon → ensure_hydrated_ → SendingTime(52) [NEW] → reset_on_logon → check_inbound → 1137 → authorize_logon → establish`.

**Contract**: let `T = cfg_.sending_time_threshold ? duration_cast<seconds>(*…) : 120 s`, `st = scan_frame_header(frame).sending_time`.

1. `st` is **valid** iff non-empty AND `fix_string_to_utc_time(st)` succeeds AND `check_sending_time(parsed, effective_clock_->now(), T)` returns ok (i.e. `|parsed − now| ≤ T`).
2. If `st` is **not valid** (absent/empty, malformed, or stale in either direction) — emit shape scaffolded on the in-arm `1137` reject (`:2102-2136`):
   - `build_reject` into a stack buffer **≥512B** (both siblings use `std::array<std::byte,512>`); on `build_reject` failure FAIL CLOSED — `record_state_transition_(fsm_state::Disconnected)`, never silent-success (`[[feedback_fixed_buffer_build_failure_silent_success]]`).
   - emit `Reject(35=3)` with `RefSeqNum` = the Logon's `34`, `RefTagID(371)=52`, `RefMsgType(372)="A"`, `SessionRejectReason(373)=10`; `assign_outbound()` — because the guard runs after `ensure_hydrated_`, this stamps the **hydrated durable outbound seq `N`** (not the construction counter 1), so on a persistent-store reconnect the Reject is in-sequence on the wire and `store(N, …)` is accepted (no `store_seqnum_out_of_order`); pass it to `fire_to_admin_` before transmit; `store_then_emit` (store error → I-07 logged-then-proceed).
   - **No Logout** is emitted (pre-establishment shape; the established-Q3 Logout is a live-session teardown). `record_state_transition_(fsm_state::Disconnected)`; `co_return` the handled (non-error) outcome.
   - The session does **not** establish; **no** reply Logon is sent; `persist_inbound_advance_` is **not** called; no durable seqnum reset fires (the guard precedes `reset_on_logon`); the in-memory next-expected-inbound is **seeded-not-advanced** (memory store → `seqnum_min`; persistent store → the seeded durable value `D`) and the durable inbound counter is unchanged.
   - A throwing `toAdmin` on the Reject → `app_callback_threw` + `Disconnected` (same as the `1137` reject).
3. If `st` is **valid**: proceed to establishment exactly as today — output byte-identical to the pre-feature acceptor path.

**Non-goals**: does not alter the established-session Q3 guard or the initiator Logon-ack Guard-3; does not add a config knob; does not change inbound `52` handling for non-Logon messages.

## C-2 — Reconnect credentials-rotated containment (Group 2)

**Contract**: in `ReconnectFsm::drive_reconnect_attempt`, the invocation of `emit_credentials_rotated_(...)` is exception-contained: a throw from the (injected FSM-seam) callback is caught at the call site and does NOT propagate out of the coroutine. After a caught throw, the rotation baseline (`last_active_source_`, `last_active_fp_`) is still updated and the attempt proceeds to `make()`/handshake per policy. A non-throwing callback is unaffected (the guard is inert). **Reachability**: the live `Session`-path callback is engine-owned and `noexcept` (→ `noexcept` `emit_event`, a ring-buffer write), so the throw is unreachable in production; this contract hardens the standalone-FSM injection seam, not a reachable operator-callback guarantee.

## C-3 — FIXT 1137 reject observability (Group 3, witness-only)

**Contract (asserted, not changed)**: an acceptor first-Logon with `DefaultApplVerID(1137)` absent or non-conformant yields an on-wire `Reject(35=3, RefTagID(371)=1137)` (`373=1` absent / `373=5` non-conformant), observable through `Application::toAdmin`, followed by `fsm_state::Disconnected`; the session does not establish. Group 3 adds witnesses for this; it changes no production code.
