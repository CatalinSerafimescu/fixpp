# Quickstart — 015 Runtime Engine

A walkthrough of the public engine for both roles + the T-041 closure. Illustrative — exact spellings lock at `/speckit-implement` against shipped headers (`engine_api.md` carries the anchored contract).

## Initiator: connect, run, stop

```cpp
#include <fixpp/session/engine.hpp>

asio::io_context io;                       // caller owns + drives the executor (Q3)
fixpp::core::EngineConfig ecfg{ /* clock, default_transport_factory, dictionaries */ };

fixpp::session::Engine engine(io.get_executor(), std::move(ecfg));

fixpp::session::SessionConfig init_cfg{ /* begin_string, sender/target CompID, mTLS profile, reconnect policy, ... */ };
auto reg = engine.register_session(std::move(init_cfg));   // FR-002
assert(reg);                                               // duplicate SessionId → error

engine.start();                            // non-blocking: spawns the connect loop, returns
io.run();                                  // caller drives; connect → handshake → Logon → Active
                                           //   then the read-pump feeds on_inbound_frame continuously
// … later, from within the executor:
co_await engine.stop();                    // idempotent: total-cancel all loops/pumps, no leak
```

The connect loop reuses 014's `ReconnectFsm::drive_reconnect_attempt` (bounded reconnect to cap) via the public `Session::drive_reconnect()`; on a live transport `install_reconnected_transport` rebinds `SessionConfig::transport_send` to the live sink, the initial Logon is emitted **POST-connect** (NOT at `open()` — connect-then-Logon, FR-003 / E-1a / Clarifications 2026-05-31), and the read-pump runs on `Session::live_transport()` (C2/C2i).

## Acceptor: listen, resolve by reversed CompID, authorize live

```cpp
fixpp::session::SessionConfig acc_cfg{ /* role=acceptor, begin_string, our CompIDs, mTLS profile, ... */ };
engine.register_session(std::move(acc_cfg));
engine.start();                            // spawns the accept loop on a listen endpoint
io.run();
```

On each inbound connection the accept loop (C1):
1. `co_await listener.async_accept()` → a **TCP-connected, NOT-yet-TLS-handshaken** `Transport` (no `peer_id` yet). The loop **runs the TLS handshake itself** (`async_handshake`) and harvests `handshake_result.peer_id`.
2. **Bounded** first-frame read (byte cap + Logon deadline, FR-014) of the Logon via the real `wire::Framer::feed(incoming, carry, out)` surface, then resolves
   `SessionId::reversed_from_logon(begin_string, logon.Sender(49), logon.Target(56))`.
3. **Match** → attaches via the **acceptor attach primitive** (`attach_accepted_transport`, design intent) — takes the transport, rebinds outbound, sets `live_peer_id_`, and does NOT transition the FSM (NOT `install_reconnected_transport`, which re-enters `LogonSent` — initiator-only). Spawns the read-pump, delivers the Logon to `on_inbound_frame`, where the acceptor gate authorizes against `live_peer_id_`.
4. **No match** → rejects at the connection level (close transport + log `session_unknown_acceptor_session = 121`), no session created.

## T-041 closure: live acceptor authorization (fail-CLOSED)

```cpp
// mTLS acceptor, allow-list binding policy:
//   on-list  peer identity → admit → session reaches Active
//   off-list/absent under mTLS → FAIL CLOSED:
//     session_compid_unauthorized + session_event_compid_authorization_failed + Disconnected
```

The live-identity arm `if (live_peer_id_.has_value() && is_mtls)` is present at the acceptor gate (as-built `session.cpp:1167`, the `NotConnected → LogonReceived` path; C3) and — symmetrically — at the initiator gate (as-built `session.cpp:1978`, the `LogonSent` case; 014's live arm). The `logon_peer_identity_override` seam arm that previously sat ahead of the `is_mtls` check at **both** sites is **removed** (T020 / C4) — each guard is now exactly the two-arm `live-identity / else-if-mTLS fail-CLOSED` plus the non-mTLS permissive skip. The binding-logic tests drive a live handshake identity over the loopback-TLS fixture (via `inject_live_identity` → the production `attach_accepted_transport` path). **T-041 → `done`** for both roles. *(Line numbers are as-built on `015-runtime-engine` and shift with edits; the structural anchors are the `live_peer_id_.has_value() && is_mtls` arm + the FSM-state case.)*

## What you can verify

| Check | Maps to |
|---|---|
| Acceptor admits on-list live identity to Active | SC-001 |
| Acceptor fails CLOSED off-list/absent (`session_compid_unauthorized`) | SC-002 |
| N inbound frames → all delivered in order, on strand, once | SC-003 |
| Register/start/stop multiple sessions; duplicate SessionId rejected | SC-004 |
| `stop()` teardown leak-free under ASan/UBSan/TSan; 2nd stop no-op | SC-005/SC-008 |
| Zero `logon_peer_identity_override` in `src/`+`include/`; no test depends on it | SC-006 |
| `feature-catalogue.md` T-041 reads `done` | SC-007 |

## Gotchas (carried lessons)
- Every `co_spawn`ed loop/pump must `reset_cancellation_state(enable_total_cancellation())` or `stop()` hangs silently (`[[feedback_asio_cospawn_total_cancellation_default]]`).
- Run the **unfiltered** Tier-1 ctest (or `-L sync`) for sign-off — the engine adds includes to awaitable-corpus headers (`[[feedback_awaitable_header_mutex_include_edge]]`, `[[feedback_gateb_full_sanitizer_before_signoff]]`).
- Fix BOTH acceptor gate sites + remove the seam in one pass — a half-fix repeats the 012 burn (`[[feedback_half_restructure_symmetric_api]]`).
