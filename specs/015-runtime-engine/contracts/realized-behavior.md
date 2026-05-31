# Contract — Realized Behaviour (015)

Behavioural contracts for the engine's runtime paths + the T-041 closure. Each maps to FRs/SCs and is the acceptance basis for Gate B. Anchors `file:line`-verified **branch-local** (the `015-runtime-engine` branch carries the FULL `session.cpp` — not a stub — confirmed first-hand at Gate A round 2; acceptor gate `session.cpp:1048`, initiator seam `session.cpp:1913`, 014 initiator live arm ~`session.cpp:1864`, `install_reconnected_transport` body `session.cpp:206`).

---

## C1 — Acceptor accept→handshake→bounded-read→resolve→attach→authorize (FR-005/006/007/014; US1; SC-001/002/011)
1. Accept loop `co_await listener.async_accept()` → a **TCP-connected, NOT-yet-TLS-handshaken** `Transport` with **no `handshake_result`/`peer_id`** (`listener.hpp:45-53`). Owned by the accept-scope cancellation domain (C5).
2. **Run the TLS handshake** — obtain the `TlsTransport`, `co_await async_handshake(...)`, harvest `handshake_result.peer_id` (Gate A Codex-1; symmetric to the initiator's `drive_reconnect_attempt`). Failure → close + reclaim slot.
3. **Bounded first-frame read** (FR-014: byte cap + Logon deadline) of the inbound Logon, parsed via the real `wire::Framer` surface (C2). Over-budget/timeout → close + reclaim slot (SC-011).
4. Resolve `SessionId::reversed_from_logon(begin_string, logon.SenderCompID(49), logon.TargetCompID(56))` against the registry (R4).
5. **Match** → attach via the **distinct acceptor attach primitive** (research.md R7; design-named `attach_accepted_transport`, exact spelling at implement): take ownership of the transport, **rebind** the outbound forwarding slot to the live `Transport::async_write`, set `live_peer_id_` from the harvested `handshake_result.peer_id`, and **do NOT transition the FSM** (leave the session on the acceptor `NotConnected → LogonReceived` path). **NOT `install_reconnected_transport`** — it re-enters `LogonSent` (initiator-only; `session.hpp:464`) and would mis-drive the acceptor (Gate A Codex-2). Then spawn the read-pump and deliver the first Logon to `on_inbound_frame`, where the acceptor gate (C3) authorizes against `live_peer_id_`.
6. **No match** (static-only — R2) → log `session_unknown_acceptor_session` (= 121) at the connection level (C7), close the transport, create NO session.

**Happens-before invariant (Gate A New-1)**: on the session strand, the attach primitive sets `live_peer_id_` **strictly-happens-before** the first `on_inbound_frame` reaching the acceptor gate. A delayed/absent identity falls to `else if (is_mtls) → fail-CLOSED` (safe). Regression: acceptor mTLS, identity delayed/absent → assert fail-CLOSED, never admit.

## C2 — Continuous read-pump on the real Framer surface (FR-004; US2; SC-003)
Per established session (either role), a coroutine on the session strand. The shipped `wire::Framer` API is `expected_t<span<frame_view>> feed(span<const std::byte> incoming, pmr_carry_buffer& carry, span<frame_view> out)` + `pending_bytes()` — **there is NO `feed(bytes)`/`next()`** (Gate A Codex-5):
```
fixpp::wire::Framer framer;                 // one per session
fixpp::wire::pmr_carry_buffer carry{cap, mr};// session-lifetime; cap from cfg
std::array<fixpp::wire::frame_view, K> out;  // bounded output buffer
co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation()); // R3
for (;;) {
    auto n = co_await transport.async_read_some(buf);   // honours total cancel
    if (!n) break;                                       // EOF / read error → stop pump
    auto frames = framer.feed({buf.data(), *n}, carry, out);
    if (!frames) { /* e.g. wire_frame_too_large → close */ break; }
    for (auto const& frame : *frames)
        co_await session.on_inbound_frame(frame.bytes()); // session.hpp:230, on strand
}
```
- Every inbound frame delivered to `on_inbound_frame` exactly once, in arrival order, on the session strand (FR-004). **Backpressure is natural** — the pump does not read the next chunk until `on_inbound_frame` completes; there is no inbound queue (Gate A P2-5).
- A frame exceeding the carry capacity → `wire_frame_too_large` (`error.hpp:60`) → `close(close_mode::terminal)` (no silent truncation). **Drain rule (T015 as-built)**: `Framer::feed` emits ≤ `out.size()` frames/call and retains the surplus in `carry`; the pump MUST re-feed (carry-only) until no complete frame remains BEFORE the next `async_read_some`, else a coalesced frame is dropped on EOF.
- EOF/read-error / `on_inbound_frame` error → pump stops and calls `session.close(close_mode::terminal)` = the existing disconnect path → `Disconnected` (no new disposition — FR-012). **Option A (Clarifications 2026-05-31)**: for an initiator this ends the connect loop — multi-cycle reconnect-respin is DEFERRED (`close(terminal)` is permanent; 014 has no tested multi-cycle reconnect), symmetric with the single-peer acceptor (C2i).
- **Scope** (Gate A New-2 / research.md R10): admin/session-layer flow only (Logon/Logout/Heartbeat/TestRequest/ResendRequest/SequenceReset). There is no user sink for inbound *application* messages in 015 (Application out of scope, FR-013); `Session` stores every frame + runs fromAdmin for admin types.

## C2i — Initiator connect loop: connect-then-Logon (FR-003; US2; SC-010 (7)/(8))
Symmetric to C1 (acceptor). `run_connect_loop` establishes in **connect-then-Logon** order — grounded in QuickFIX-cpp (`setResponder` on connect → `Session::next()`→`generateLogon`) and Fix8 (`Session::start`→`_connection->connect()` then `send(generate_logon)`): both emit the Logon strictly **after** the connection + outbound sink exist.
1. `co_await Session::open()` — session/FSM/executor/clock/arena setup; the engine-driven initiator does **NOT** emit the Logon at open (E-1a; the open()-time emit at `session.cpp:447-518` is valid only in the per-session-direct model).
2. `co_await session.drive_reconnect()` — public wrapper over `reconnect_fsm_.drive_reconnect_attempt()`; on success `install_reconnected_transport` rebinds `transport_send_` to the live transport (**symmetric initiator-attach — 015 amends 014**) + re-enters `LogonSent`. Exhaustion → `transport_reconnect_limit_exceeded` → loop ends.
3. Emit the initial Logon **POST-connect** over the now-live `transport_send_` (`build_logon` + `store_then_emit`, seq 1).
4. Read-pump (C2) on `session.live_transport()` delivers the peer Logon-ack + frames → `LogonSent → Active`; EOF → end (Option A — single connect+pump; respin deferred).
- **Public surface (SC-010)**: `Session::drive_reconnect()` + `Session::live_transport()`.
- **Regression watch** (`[[feedback_half_restructure_symmetric_api]]`): amending the shared `install_reconnected_transport` changes 014's reconnect-path outbound sink (config-bound → live `reconnected_transport_`) — re-run the 014 reconnect suite; confirm no test asserted outbound went to the config-time transport.

## C3 — Acceptor live-identity gate arm (FR-006/007/008; T-041; SC-001/002) — **ONE acceptor site**
Insert arm **1-live** ahead of the seam arm at the **single acceptor gate** `src/session/session.cpp:1048` (the `NotConnected → LogonReceived` branch), mirroring the initiator arm at `:1864`:
```
if (live_peer_id_.has_value() && is_mtls) {
    // (1-live) authorize against the REAL handshake peer_id
    auto r = compid_authorization_policy.authorize(*live_peer_id_, asserted_compid);
    // on-list → admit + emit session_event_peer_identity_bound
    // off-list/absent → FAIL CLOSED: session_compid_unauthorized (=117)
    //                   + session_event_compid_authorization_failed + Disconnected
} else if (is_mtls) {           // (2) inherited fail-CLOSED — UNCHANGED
    ...
} else { /* (3) non-mTLS permissive skip — UNCHANGED */ }
```
- Fail-closed/permissive semantics, canonical CN→SAN-DNS→SAN-URI→SHA-256 extraction order, and the `session_compid_unauthorized` / `session_event_compid_authorization_failed` shapes are **inherited unchanged** from 013/014 (FR-008). 015 changes only the acceptor identity **source** (seam → live `handshake_result.peer_id`).
- **`:1913` is NOT a second acceptor gate** (Gate A New-7): it is the initiator's seam arm inside `case fsm_state::LogonSent` (adjacent to 014's live arm at `:1864`). The live arm is added at `:1048` ONLY; the seam arm is *removed* from both `:1048` and `:1913` (C4).

## C4 — Seam removal (FR-009; SC-006)
Once C3 lands, `SessionConfig::logon_peer_identity_override` (`session_config.hpp:229`) has no remaining consumer:
- Remove the field. **There is NO field-count `static_assert` to update** — the only guard is `static_assert(std::is_copy_constructible_v<SessionConfig>, ...)` (`session_config.hpp:260`; the prior `:255` field-count claim was invented — Gate A Codex-6). Removing the optional field preserves copyability; verify the assert still holds (no edit expected).
- Remove the seam arm from **both** gate sites — `:1048` (acceptor) and `:1913` (initiator) — so each guard becomes: arm 1-live / `else if is_mtls` fail-CLOSED / non-mTLS skip.
- **Re-point binding-logic tests** (013/014 on-list/off-list/absent) to drive a **live** handshake identity over the loopback-TLS acceptor fixture (now feasible via C1). SC-006 = zero `logon_peer_identity_override` in `src/` + `include/`, and no test depends on it.

## C5 — Engine lifecycle, accept-scope & teardown (FR-001/003/011/014; US3; SC-004/005/011)
- `start()` non-blocking: `co_spawn`s a connect loop per initiator (reusing 014 `ReconnectFsm::drive_reconnect_attempt`) + an accept loop per acceptor; returns immediately (clarify-Q3). Each loop `co_await`s `Session::open()` itself (open() is awaitable; cannot run in the synchronous `void start()` — Gate A New-3).
- **Accept-scope cancellation domain** (per listener, distinct from per-session): owns the accept→handshake→bounded-first-read→attach window (C1). `stop()` mid-accept, handshake failure, first-frame timeout/over-budget, or construction failure → close the transport + reclaim the slot; other peers unaffected (FR-007/014, SC-011).
- **Destructor**: `~Engine()` asserts `stopped()` — NO synchronous best-effort path (a synchronous destructor can't run the caller-driven loop to drain coroutines holding raw `Session*` → UAF; Gate A Codex-9). Destruction requires a prior `co_await stop()`.
- `stop()` idempotent: total-cancels all loops/pumps/in-flight handshakes/accept-scope domains, closes transports, and **JOINS** outstanding session work **before clearing the registry** that owns the `Session` objects (join-before-clear — Gate A New-4); second stop is a no-op.
- **Teardown is the headline sanitizer target** (`[[feedback_gateb_full_sanitizer_before_signoff]]` + the 014 Gate-B UAF lesson): the full ASan/UBSan/TSan matrix must be green with zero leaks/UAF across construct→start→accept/connect→stop cycles (SC-005/SC-008). Run the **unfiltered** Tier-1 ctest (or `-L sync`) — never a name-scoped subset (SC-008; `[[feedback_awaitable_header_mutex_include_edge]]`).

## C6 — Registry & duplicate rejection (FR-002; SC-004)
- `register_session(cfg)` keys on `SessionId::from_config(cfg)`; a second config resolving to the same `SessionId` → `session_invalid_argument` (= 119; R5 #2, no new slot).
- `lookup(id)` returns the live `Session*`, or nullptr if `id` is not registered OR registered-but-not-yet-established (acceptor with no peer / loop not yet at open() — Gate A New-3).
- Registry mutation on the engine strand (E-5), not a mutex. The engine strand protects the map; the join-before-clear rule (C5) protects the pointee lifetime across strands (Gate A New-4).

## C7 — Unmatched-Logon error delivery site (FR-006; SC-006)
`session_unknown_acceptor_session` (= 121, the next free slot — verified no reusable code, `error.hpp:616-665`) surfaces at the **connection level only**: close the transport + log the diagnostic. It is **never delivered to a Session event** — at rejection time no Session exists for the unmatched identity (Gate A P2-6). Routing is static: an unmatched identity is ALWAYS rejected, no fail-open path (R2).

## C8 — Catalogue (FR-010; SC-007)
- **T-041 → `done`** in `feature-catalogue.md` (both roles bind a live identity + fail CLOSED in production; the seam is gone).
- `feature-catalogue.md` + `coverage-index.md` get the 015 row per `[[feedback_feature_completeness_gate]]`.
- Any catalogue row touching the runtime engine / acceptor live path updated.

---

## Out-of-scope guards (FR-013)
No config-file parsing, no `Application`-callback ecosystem (none exists to change — 015 consumes only what 005 ships internally; there is no inbound app-message user sink, research.md R10 / Gate A New-2), no store/log factory abstractions, no C-ABI / control-plane / observability / pybind surface. The dynamic-session-provider is deferred (R2). Non-mTLS permissive path unchanged.

## Error model (R5 — pinned first-hand at Gate A)
| Condition | Code | New slot |
|---|---|---|
| Unmatched acceptor Logon (connection-level only) | `session_unknown_acceptor_session = 121` | **yes, +1** (no reusable code, `error.hpp:616-665`; append-only `[const §X.4]`) |
| Duplicate registration | `session_invalid_argument` (= 119) | no |
| Acceptor authz failure | `session_compid_unauthorized` (= 117) | no |
