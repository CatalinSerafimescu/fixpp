# Contract — Realized Behaviour (015)

Behavioural contracts for the engine's runtime paths + the T-041 closure. Each maps to FRs/SCs and is the acceptance basis for Gate B. Anchors `file:line`-verified vs post-PR-#87 `main`.

---

## C1 — Acceptor accept→resolve→bind→authorize (FR-005/006/007; US1; SC-001/002)
1. Accept loop `co_await listener.async_accept()` → fully-handshaken `Transport` (TLS issued inside; `listener.hpp:52`), carrying `handshake_result.peer_id`.
2. Read the first frame via `wire::Framer` (E-3): `framer.feed(bytes); auto logon = framer.next();`.
3. Resolve `SessionId::reversed_from_logon(begin_string, logon.SenderCompID(49), logon.TargetCompID(56))` against the registry (R4).
4. **Match** → set `SessionConfig::transport_send`, construct/`open()` the session if not already, call `install_reconnected_transport(handshake_result)` (sets `live_peer_id_`, `session.hpp:475`/`session.cpp:208`), spawn the read-pump, deliver the first frame to `on_inbound_frame`. The session's acceptor gate (C3) authorizes against the live identity.
5. **No match** (static-only — R2) → emit unmatched-Logon error (R5 #1), close the transport, create NO session.

## C2 — Continuous read-pump (FR-004; US2; SC-003)
Per established session (either role), a coroutine on the session strand:
```
fixpp::wire::Framer framer;          // one per session
for (;;) {
    auto n = co_await transport.async_read_some(buf);   // honours total cancel
    if (!n) break;                                       // EOF / read error → stop pump
    framer.feed({buf, *n});
    while (auto frame = framer.next())
        co_await session.on_inbound_frame(*frame);       // session.hpp:230, on strand
}
```
- Every inbound frame delivered to `on_inbound_frame` exactly once, in arrival order, on the session strand (FR-004).
- EOF/read-error → pump stops; the session goes through its existing disconnect handling (no new disposition — FR-012). For an initiator, the reconnect FSM (014) may re-drive a connect attempt; the engine respins the pump on the new transport.
- `reset_cancellation_state(enable_total_cancellation())` at entry (mandatory — R3).

## C3 — Acceptor live-identity gate arm (FR-006/007/008; T-041; SC-001/002) — **mirror of 014 initiator**
At BOTH acceptor gate sites (`src/session/session.cpp:1048` and `:1913`), insert arm **1-live** ahead of the seam arm, mirroring the initiator arm at `:1864`:
```
if (live_peer_id_.has_value() && is_mtls) {
    // (1-live) authorize against the REAL handshake peer_id
    auto r = compid_authorization_policy.authorize(*live_peer_id_, asserted_compid);
    // on-list → admit + emit session_event_peer_identity_bound
    // off-list/absent → FAIL CLOSED: session_compid_unauthorized
    //                   + session_event_compid_authorization_failed + Disconnected
} else if (is_mtls) {           // (2) inherited fail-CLOSED — UNCHANGED
    ...
} else { /* (3) non-mTLS permissive skip — UNCHANGED */ }
```
- Fail-closed/permissive semantics, canonical CN→SAN-DNS→SAN-URI→SHA-256 extraction order, and the `session_compid_unauthorized` / `session_event_compid_authorization_failed` shapes are **inherited unchanged** from 013/014 (FR-008). 015 changes only the acceptor identity **source** (seam → live `handshake_result.peer_id`).
- **Symmetric-fix obligation** (`[[feedback_half_restructure_symmetric_api]]`): the arm lands on BOTH `:1048` and `:1913`; a one-site fix is a defect. Grep every `logon_peer_identity_override.has_value()` consumer and convert each.

## C4 — Seam removal (FR-009; SC-006)
Once C3 lands on both sites, `SessionConfig::logon_peer_identity_override` has no remaining consumer:
- Remove the field (`session_config.hpp`) + update the field-count `static_assert` (`session_config.hpp:255`).
- Remove the seam arm from both gate sites → each guard becomes: arm 1-live / `else if is_mtls` fail-CLOSED / non-mTLS skip.
- **Re-point binding-logic tests** (013/014 on-list/off-list/absent) to drive a **live** handshake identity over the loopback-TLS acceptor fixture (now feasible via C1). SC-006 = zero `logon_peer_identity_override` in `src/` + `include/`, and no test depends on it.

## C5 — Engine lifecycle & teardown (FR-001/003/011; US3; SC-004/005)
- `start()` non-blocking: `co_spawn`s a connect loop per initiator (reusing 014 `ReconnectFsm::drive_reconnect_attempt`) + an accept loop per acceptor; returns immediately (clarify-Q3).
- `stop()` idempotent: total-cancels all loops/pumps/in-flight handshakes, closes transports, joins outstanding session work; second stop is a no-op.
- **Teardown is the headline sanitizer target** (`[[feedback_gateb_full_sanitizer_before_signoff]]` + the 014 Gate-B UAF lesson): the full ASan/UBSan/TSan matrix must be green with zero leaks/UAF across construct→start→accept/connect→stop cycles (SC-005/SC-008). Run the **unfiltered** Tier-1 ctest (or `-L sync`) — never a name-scoped subset (SC-008; `[[feedback_awaitable_header_mutex_include_edge]]`).

## C6 — Registry & duplicate rejection (FR-002; SC-004)
- `register_session(cfg)` keys on `SessionId::from_config(cfg)`; a second config resolving to the same `SessionId` → `session_invalid_argument` (R5 #2, no new slot).
- `lookup(id)` returns the live `Session*` or nullptr.
- Registry mutation on the engine strand (E-5), not a mutex.

## C7 — Catalogue (FR-010; SC-007)
- **T-041 → `done`** in `feature-catalogue.md` (both roles bind a live identity + fail CLOSED in production; the seam is gone).
- `feature-catalogue.md` + `coverage-index.md` get the 015 row per `[[feedback_feature_completeness_gate]]`.
- Any catalogue row touching the runtime engine / acceptor live path updated.

---

## Out-of-scope guards (FR-013; SC-009)
No config-file parsing, no `Application`-callback ecosystem changes beyond what 005 ships, no store/log factory abstractions, no C-ABI / control-plane / observability / pybind surface. The dynamic-session-provider is deferred (R2). Non-mTLS permissive path unchanged.

## Error model (R5)
| Condition | Code | New slot |
|---|---|---|
| Unmatched acceptor Logon | `session_unknown_acceptor_session` (≈121) | maybe +1 (confirm /tasks; append-only) |
| Duplicate registration | `session_invalid_argument` (119) | no |
| Acceptor authz failure | `session_compid_unauthorized` | no |
