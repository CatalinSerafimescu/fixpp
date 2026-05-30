# Phase 1 — Data Model & Design Records: 015-runtime-engine

**Feature**: Public Initiator/Acceptor Runtime Engine & Full T-041 Closure
**Date**: 2026-05-30 | **Plan**: [`plan.md`](./plan.md) | **Research**: [`research.md`](./research.md)

Entities + design records (E-1..E-6). Every shipped anchor is `file:line`-verified against post-PR-#87 `main` (2026-05-30). The engine is a new concrete type in the **existing `session/` module** (R1).

---

## Entities

### SessionId (new value type — R6)
The registry key. Value type in `fixpp::session`.

| Field | Type | Source |
|---|---|---|
| `begin_string` | `std::string` | `SessionConfig::begin_string` (`session_config.hpp:153`) |
| `sender_comp_id` | `std::string` | `SessionConfig::sender_comp_id` (`:151`) |
| `target_comp_id` | `std::string` | `SessionConfig::target_comp_id` (`:152`) |
| `qualifier` | `std::string` (default empty) | reserved for forward-compat (clarify-Q2 "optional qualifier"); **no `SessionConfig` field today** — always empty in 015 |

- **Value equality** + a hash (for `unordered_map`) and/or `operator<` (for `map`). All four fields participate (qualifier empty ⇒ no behavioural change in 015).
- **Construction**: `SessionId::from_config(SessionConfig const&)` (own role) and `SessionId::reversed_from_logon(begin_string, logon_sender, logon_target)` → `{begin_string, sender = logon_target, target = logon_sender}` for acceptor resolution (R4).
- **Validation**: empty `begin_string`/`sender`/`target` is invalid (a `SessionConfig` with them empty is already rejected upstream by 005/010 open() validation; the engine relies on that, does not re-validate).

### Engine (new concrete type — R1)
The public runtime engine. Lives in `fixpp::session`. Owns the registry + per-role loops; bound to a caller-supplied executor (clarify-Q3).

| Member (conceptual) | Type | Role |
|---|---|---|
| executor | `asio::any_io_executor` | caller-supplied (Q3); all loops `co_spawn` on it |
| engine_cfg | `fixpp::core::EngineConfig` (borrowed/held) | the shared engine-level resources (`clock`, `default_transport_factory`, dictionaries); `Session` ctor already takes `EngineConfig&` (`session.hpp:95`) |
| registry | `std::unordered_map<SessionId, SessionEntry>` | FR-002 store; mutated only on the engine strand (E-5) |
| listeners | per acceptor-config `Listener` (012) | accept-loop substrate |
| stopped flag | `bool`/`atomic` | `stop()` idempotence (FR-011) |

**Public surface** (minimal — see contracts/engine_api.md):
- `explicit Engine(asio::any_io_executor exec, fixpp::core::EngineConfig cfg)`
- `expected_t<void> register_session(SessionConfig cfg)` — FR-002; duplicate `SessionId` → error (R5 #2: reuse `session_invalid_argument`, no new slot)
- `void start()` — non-blocking (Q3); `co_spawn`s a connect loop per initiator + an accept loop per acceptor
- `asio::awaitable<void> stop()` — idempotent; total-cancellation teardown of all loops/pumps/in-flight handshakes (FR-011)
- `Session* lookup(SessionId const&) const` — registry addressing

### SessionEntry (registry value)
| Field | Type | Role |
|---|---|---|
| session | `std::unique_ptr<Session>` | the owned session (created via the `Session(EngineConfig&, SessionConfig&)` ctor + `open()`) |
| role | `enum { initiator, acceptor }` | which loop drives it |
| config | `SessionConfig` | retained for re-accept/reconnect + identity |
| read_pump_cs / loop_cs | `asio::cancellation_signal` | per-session teardown handles for `stop()` |

### Accept loop (per acceptor session/listener)
A coroutine `co_spawn`ed on the engine executor: `while (!stopped) { auto t = co_await listener.async_accept(); … resolve … spawn read-pump }`. `async_accept()` returns a fully-handshaken `Transport` (`listener.hpp:52`).

### Read-pump (per established session, both roles)
A coroutine `co_spawn`ed on the **session strand**: accumulates bytes via `wire::Framer` (E-3), and for each complete frame `co_await session.on_inbound_frame(frame)` (`session.hpp:230`) until EOF/read-error/cancellation.

### Live peer identity (T-041 closure input)
Already plumbed by 014: `Session::live_peer_id_` (`session.hpp:552`), set by `install_reconnected_transport(handshake_result)` (`session.hpp:475`, `session.cpp:208`). 015 makes the **acceptor** gate consume it (E-4).

---

## Design records

### E-1 — Engine ↔ Session ownership & wiring
The engine constructs each `Session` with the existing `Session(EngineConfig const&, SessionConfig const&)` ctor (`session.hpp:95`) and calls `open()` (`:114`) to bind the session executor (strand) + clock + arena. Outbound is wired via **`SessionConfig::transport_send`** — the `std::function<void(span<const std::byte>)>` captured at open() into `transport_send_` (`session.hpp:530-534`); the engine sets `cfg.transport_send` to a closure writing to the live `Transport::async_write` before constructing the session. Inbound is driven by the read-pump (E-3). The engine is the lifetime owner (`unique_ptr<Session>` in the registry); sessions never outlive the engine.

### E-2 — Acceptor first-frame peek & session resolution (R4)
`async_accept()` yields a handshaken `Transport` but not yet a session. The accept loop must read the **first inbound frame** (the Logon) to learn the peer's CompIDs, then resolve `SessionId::reversed_from_logon(begin_string, logon.SenderCompID(49), logon.TargetCompID(56))` against the registry.
- **Match** → hand the live transport + that first frame to the resolved session: wire `transport_send`, call `install_reconnected_transport(handshake_result)` so `live_peer_id_` is set (E-4), spawn the read-pump, and deliver the already-read first frame to `on_inbound_frame` (so the Logon is processed by the session's gate, where E-4 authorizes).
- **No match** (static-only, R2) → reject: close the transport, emit the unmatched-Logon error (R5 #1), create no session.
- **Framing of the first frame** uses the same `wire::Framer` accumulator as the read-pump (E-3) — the peek is just "run the framer until the first complete frame, then resolve."
- This mirrors QFC `Session::lookupSession(message, true)` (`quickfix-cpp/.../SocketConnection.cpp:162`) + QFJ `getReverseSessionID` (`AcceptorIoHandler.java:70`).

**Open sub-decision for /tasks**: whether the first-frame bytes are replayed into `on_inbound_frame` or the framer buffer is transferred to the session intact. Default: deliver the parsed first frame to `on_inbound_frame` directly (no double-parse), keep remaining buffered bytes in the read-pump's framer. Lock at implement after reading `on_inbound_frame`'s framing expectations.

### E-3 — Framing: reuse `wire::Framer`
`Transport::async_read_some` yields bytes, not frames; `on_inbound_frame` expects a verified frame (`session.hpp:216` "after parse/frame-validate"). **Decision: reuse the existing `wire::Framer` (`include/fixpp/wire/framer.hpp`)** as the read-pump's accumulator — do NOT hand-roll SOH/BodyLength/checksum delimiting. `session` already depends on `wire` (`check_layers.py:29`), so this adds no layer edge. The read-pump owns one `Framer` instance per session, feeds it `async_read_some` output, and drains complete frames to `on_inbound_frame`.
- **Rationale**: avoids duplicating frame-delimiting logic already shipped + fuzzed in 004; keeps `on_inbound_frame`'s contract (verified frame in) intact.
- **Confirm at /tasks**: the exact `wire::Framer` API (push bytes / pull frame) against `framer.hpp`; do not assume method names here.

### E-4 — Acceptor live-identity gate arm (T-041 closure; mirror of 014 initiator)
014 added the live-identity arm to the **initiator** gate only (`session.cpp:1864`: `if (live_peer_id_.has_value() && is_mtls) → authorize(real peer_id)`). The **acceptor** gate (`session.cpp:1048`, and the second site `:1913`) is still the seam-only three-way guard. 015 adds the symmetric arm at the acceptor site:
- **New arm 1-live** (ahead of the seam arm): `if (live_peer_id_.has_value() && is_mtls) → authorize against live_peer_id_` → admit on-list / fail-CLOSED (`session_compid_unauthorized` + `session_event_compid_authorization_failed`) off-list/absent.
- The accept loop sets `live_peer_id_` via `install_reconnected_transport(handshake_result)` (E-2) before the Logon is processed.
- After this lands on BOTH gate sites, the `logon_peer_identity_override` seam arm has **no remaining production or test consumer** → removed (E-6).
- **Symmetric-fix discipline** per `[[feedback_half_restructure_symmetric_api]]`: 014 did the initiator half + documented the acceptor deferral; 015 does the acceptor half. BOTH gate sites (`:1048` and `:1913`) must get the arm — a one-site fix would repeat the 012 half-restructure burn. Audit via grep for every `logon_peer_identity_override.has_value()` consumer.

### E-5 — Registry concurrency: engine strand, not a mutex (§XV.9)
The registry (`unordered_map<SessionId, SessionEntry>`) is mutated by `register_session` (pre-start, single-threaded by contract) and read by accept-loop resolution (concurrent with the executor). **Decision: sequence all registry mutation/iteration on a dedicated engine strand** (derived from the injected executor), NOT a `std::mutex`. This honours `[const §XV.9]` (no `std::mutex` in awaitable-corpus headers — `engine.hpp` will be in the awaitable corpus since it holds coroutines) and `[[feedback_awaitable_header_mutex_include_edge]]`. `register_session` before `start()` is the common path (no contention); post-start dynamic registration (if ever) posts to the engine strand. The unfiltered Tier-1 / `-L sync` ctest is the witness.

### E-6 — Seam removal (FR-009) & test re-pointing
`SessionConfig::logon_peer_identity_override` (`session_config.hpp`, the `:1048`/`:1913` consumers) is removed once E-4 lands on both gate sites. Consequences (all in-slice):
- Delete the field + its three-way-guard arm at both sites (`session.cpp:1048`, `:1913`); the guard becomes two-arm (live-identity / `else if is_mtls` fail-CLOSED / non-mTLS skip).
- **Re-point the binding-logic tests**: 013/014's on-list/off-list/absent tests drove the seam; they must now drive a **live handshake identity** over the loopback-TLS acceptor fixture (the live acceptor path E-2 makes this feasible — the seam existed only because `mock_transport` had no real identity). This is the SC-006 proof that no test depends on the seam.
- `SessionConfig`'s `static_assert` field-count guard (`session_config.hpp:255`) is updated for the removed field.
- Grep gate: zero `logon_peer_identity_override` occurrences in `src/` and `include/` after removal; tests reference only the live path.

---

## Error model delta (R5)
| Condition | Code | New slot? |
|---|---|---|
| Unmatched acceptor Logon (no registry entry) | `session_unknown_acceptor_session` (≈121, next free after 120) | **Maybe +1** — confirm no reusable code at /tasks; append-only `[const §X.4]` |
| Duplicate `register_session` (same SessionId) | reuse `session_invalid_argument` (=119) | No |
| Acceptor authorization failure | reuse `session_compid_unauthorized` (013) | No |

Slot 70 stays a permanent hole; 120 is the current max; never renumber.

## State / lifecycle
`Engine`: `constructed → started → stopping → stopped`. `start()` legal once from `constructed`; `stop()` idempotent from any state (no-op if already `stopped`). Per-session: the existing `Session` lifecycle (`open → … → close`) is unchanged; the engine owns construction/`open()`/teardown ordering.

## Traceability
FR-001→Engine; FR-002→SessionId+register_session+E-5; FR-003→accept/connect loops; FR-004→read-pump+E-3; FR-005→E-2; FR-006/007/008→E-4; FR-009→E-6; FR-010→E-4 catalogue flip; FR-011→stop()+total-cancellation (R3); FR-012→Out-of-scope guard; FR-013→structure.
