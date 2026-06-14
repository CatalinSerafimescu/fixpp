# Internal Contract — Acceptor first-Logon SendingTime guard (038)

Not a public API contract (no surface change). This pins the behavioural contract the witnesses assert. Scope: `src/session/session.cpp` (Group 1), `src/session/reconnect_fsm.cpp` (Group 2), test-only (Group 3).

## C-1 — Acceptor first-Logon SendingTime disposition (Group 1)

**Precondition**: acceptor in `fsm_state::NotConnected`; inbound frame is a Logon (35=A) that passes `interpret_logon` (BeginString/CompID/HeartBtInt) and the FIXT `1137` validation; `effective_clock_` is set.

**Contract**: let `T = cfg_.sending_time_threshold ? duration_cast<seconds>(*…) : 120 s`, `st = scan_frame_header(frame).sending_time`.

1. `st` is **valid** iff non-empty AND `fix_string_to_utc_time(st)` succeeds AND `check_sending_time(parsed, effective_clock_->now(), T)` returns ok (i.e. `|parsed − now| ≤ T`).
2. If `st` is **not valid** (absent/empty, malformed, or stale in either direction) — scaffolded on the in-arm `1137` reject (`:2102-2136`):
   - emit `Reject(35=3)` with `RefSeqNum` = the Logon's `34`, `RefTagID(371)=52`, `RefMsgType(372)="A"`, `SessionRejectReason(373)=10`; `assign_outbound()`; pass it to `fire_to_admin_` before transmit; `store_then_emit` (store error → I-07 logged-then-proceed).
   - **No Logout** is emitted (pre-establishment shape; the established-Q3 Logout is a live-session teardown). `record_state_transition_(fsm_state::Disconnected)`; `co_return` the handled (non-error) outcome.
   - The session does **not** establish; **no** reply Logon is sent; `persist_inbound_advance_` is **not** called; the next-expected-inbound counter (and durable inbound counter, if persistent) are unchanged.
   - A throwing `toAdmin` on the Reject → `app_callback_threw` + `Disconnected` (same as the `1137` reject).
3. If `st` is **valid**: proceed to establishment exactly as today — output byte-identical to the pre-feature acceptor path.

**Non-goals**: does not alter the established-session Q3 guard or the initiator Logon-ack Guard-3; does not add a config knob; does not change inbound `52` handling for non-Logon messages.

## C-2 — Reconnect credentials-rotated containment (Group 2)

**Contract**: in `ReconnectFsm::drive_reconnect_attempt`, the invocation of `emit_credentials_rotated_(...)` is exception-contained: a throw from the user callback is caught at the call site and does NOT propagate out of the coroutine. After a caught throw, the rotation baseline (`last_active_source_`, `last_active_fp_`) is still updated and the attempt proceeds to `make()`/handshake per policy. A non-throwing callback is unaffected (the guard is inert).

## C-3 — FIXT 1137 reject observability (Group 3, witness-only)

**Contract (asserted, not changed)**: an acceptor first-Logon with `DefaultApplVerID(1137)` absent or non-conformant yields an on-wire `Reject(35=3, RefTagID(371)=1137)` (`373=1` absent / `373=5` non-conformant), observable through `Application::toAdmin`, followed by `fsm_state::Disconnected`; the session does not establish. Group 3 adds witnesses for this; it changes no production code.
