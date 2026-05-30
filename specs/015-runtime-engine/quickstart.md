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

The connect loop reuses 014's `ReconnectFsm::drive_reconnect_attempt` (bounded reconnect to cap); on a live transport the engine wires `SessionConfig::transport_send` and spawns the read-pump (C2).

## Acceptor: listen, resolve by reversed CompID, authorize live

```cpp
fixpp::session::SessionConfig acc_cfg{ /* role=acceptor, begin_string, our CompIDs, mTLS profile, ... */ };
engine.register_session(std::move(acc_cfg));
engine.start();                            // spawns the accept loop on a listen endpoint
io.run();
```

On each inbound connection the accept loop (C1):
1. `co_await listener.async_accept()` → handshaken `Transport` (+ `handshake_result.peer_id`).
2. Reads the first frame (the Logon) via `wire::Framer` and resolves
   `SessionId::reversed_from_logon(begin_string, logon.Sender(49), logon.Target(56))`.
3. **Match** → binds the transport, `install_reconnected_transport(handshake_result)` (sets `live_peer_id_`), spawns the read-pump, delivers the Logon to `on_inbound_frame`.
4. **No match** → rejects (close transport, `session_unknown_acceptor_session`), no session created.

## T-041 closure: live acceptor authorization (fail-CLOSED)

```cpp
// mTLS acceptor, allow-list binding policy:
//   on-list  peer identity → admit → session reaches Active
//   off-list/absent under mTLS → FAIL CLOSED:
//     session_compid_unauthorized + session_event_compid_authorization_failed + Disconnected
```

Both gate sites (`session.cpp:1048`, `:1913`) now have the live-identity arm (C3, mirror of the 014 initiator arm at `:1864`). The `logon_peer_identity_override` seam is **removed** (C4) — the binding-logic tests drive a live handshake identity over the loopback-TLS fixture. **T-041 → `done`** for both roles.

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
