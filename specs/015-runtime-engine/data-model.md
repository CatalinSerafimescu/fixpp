# Phase 1 — Data Model & Design Records: 015-runtime-engine

**Feature**: Public Initiator/Acceptor Runtime Engine & Full T-041 Closure
**Date**: 2026-05-30 | **Plan**: [`plan.md`](./plan.md) | **Research**: [`research.md`](./research.md)

Entities + design records (E-1..E-7). Every shipped anchor is `file:line`-verified **branch-local** (the `015-runtime-engine` branch carries the FULL `session.cpp`/`session_config.hpp`, not stubs — confirmed first-hand at Gate A round 2; see research.md baseline). The engine is a new concrete type in the **existing `session/` module** (R1).

---

## Entities

### SessionId (new value type — R6)
The registry key. A **regular value type** in `fixpp::session` — copyable, equality-comparable, hashable (so it can key `std::unordered_map`), with ordinary value semantics.

| Field | Type | Source |
|---|---|---|
| `begin_string` | `std::string` | `SessionConfig::begin_string` |
| `sender_comp_id` | `std::string` | `SessionConfig::sender_comp_id` |
| `target_comp_id` | `std::string` | `SessionConfig::target_comp_id` |

- **No `qualifier` field** — the previously-reserved always-empty field is dropped (Gate A New-5; re-add only when a real `SessionConfig` qualifier lands).
- **Value equality** + a hash (for `unordered_map`) and/or `operator<` (for `map`). All three fields participate.
- **Construction**: `SessionId::from_config(SessionConfig const&)` (own role) and `SessionId::reversed_from_logon(begin_string, logon_sender, logon_target)` → `{begin_string, sender = logon_target, target = logon_sender}` for acceptor resolution (R4).
- **Validation**: empty `begin_string`/`sender`/`target` is invalid (a `SessionConfig` with them empty is already rejected upstream by 005/010 open() validation; the engine relies on that, does not re-validate).

### Engine (new concrete type — R1)
The public runtime engine. Lives in `fixpp::session`. Owns the registry + per-role loops; bound to a caller-supplied executor (clarify-Q3).

| Member (conceptual) | Type | Role |
|---|---|---|
| executor | `asio::any_io_executor` | caller-supplied (Q3); all loops `co_spawn` on it |
| engine_cfg | `fixpp::core::EngineConfig` (borrowed/held) | the shared engine-level resources (`clock`, `default_transport_factory`, dictionaries); the public `Session(const EngineConfig&, const SessionConfig&)` ctor takes it by const-ref (`session.hpp:95`). `EngineConfig` carries **no `Application`** (`engine_config.hpp:106-148`). |
| registry | `std::unordered_map<SessionId, SessionEntry>` | FR-002 store; confined to the single injected executor (E-5 — single-executor confinement, not a strand) |
| listeners | per acceptor-config `Listener` (012) | accept-loop substrate |
| accept-scope domains | per-listener `cancellation_signal` (≠ per-session) | owns the accept→handshake→bounded-first-read→attach window (E-7 / research.md R9) |
| stopped flag | `bool`/`atomic` | `stop()` idempotence (FR-011) |

**Construction model (re-derived first-hand, Gate A round 1; lazy-construction clarified round 2):** construction is **LAZY** and consistent with engine_api.md `register_session`. `register_session` stores **config + role ONLY — it does NOT construct a `Session`.** **BOTH** the public synchronous ctor `Session(const EngineConfig&, const SessionConfig&)` (`session.hpp:95`) **AND** the awaitable `open()` (`session.hpp:114`) run **lazily INSIDE each spawned accept/connect loop** — never at register time and never in the synchronous `void start()` (`open()` is awaitable and cannot run there). **There is NO `make_session` factory and NO `Application&` parameter** (the prior research.md row was fabricated — Gate A Codex-3/New-2). Until its loop reaches the ctor, a registered session has **no `Session` object**; `lookup()` returns null for it (E-7 / research.md R9; see SessionEntry below).

**Public surface** (minimal — see contracts/engine_api.md):
- `Engine(asio::any_io_executor exec, fixpp::core::EngineConfig cfg)`
- `expected_t<void> register_session(SessionConfig cfg)` — FR-002; duplicate `SessionId` → error (R5 #2: reuse `session_invalid_argument = 119`). **No `Application&` parameter.**
- `void start()` — non-blocking (Q3); `co_spawn`s a connect loop per initiator + an accept loop per acceptor; each loop `co_await`s `open()` itself
- `asio::awaitable<void> stop()` — idempotent; total-cancellation teardown that **joins** all loops/pumps/in-flight handshakes **before clearing the registry** (FR-011; E-7)
- `Session* lookup(SessionId const&) const` — registry addressing; may return a constructed-but-not-yet-open session, or null for a registered acceptor with no connected peer (research.md R9)

### SessionEntry (registry value)
| Field | Type | Role |
|---|---|---|
| session | `std::unique_ptr<Session>` | the owned session — **null until the loop constructs it** (ctor + `open()` run lazily inside the accept/connect loop, not at register/start time). `lookup()` returns null while this is null. |
| role | `enum { initiator, acceptor }` | which loop drives it |
| config | `SessionConfig` | retained for re-accept/reconnect + identity |
| session_cancel | `asio::cancellation_signal` | the **per-session** teardown handle (read-pump + session work) for `stop()` |
| live_transport | `transport::Transport*` (non-owning) | the live transport once attached/connected — **null until the loop attaches it**; owned by the `Session`. `stop()` calls `live_transport->close()` to unblock the idle read-pump's in-flight `async_read_some` (T018 / E-7 "closes transports"). |

> The per-session `session_cancel` is **distinct** from the per-listener accept-scope domain (E-7): a listener outlives any one peer connection, so they have different lifetimes and MUST NOT be the same signal (Gate A P3-3 / New-4).

### Accept loop (per acceptor session/listener)
A coroutine `co_spawn`ed once per acceptor session. ⚠️ **Two clauses that stood here — which executor the loop is spawned on, and whether the read-pump is a separately-spawned coroutine — are DELETED, not corrected** (2026-08-31, issue #334). Both were falsified by feature 023 (T010); their siblings in `research.md` R3 were amended 2026-08-29 and **this occurrence was missed**, which is the whole reason it is called out rather than quietly fixed. No replacement topology is written here: restating one would rot on the next threading change with nothing to notice it. Derive both from the spawn site, which cannot go stale silently because it is the thing being described — `grep -n "co_spawn" src/session/engine.cpp`. What is structural, and so safe to keep, is the loop's step sequence: `while (!stopped) { auto t = co_await listener.async_accept(); … run TLS handshake … bounded first-frame read … resolve … attach … read-pump }`. **`async_accept()` returns a TCP-connected `Transport` with TLS NOT yet issued and no `handshake_result`/`peer_id`** (`listener.hpp:45-53`) — the loop itself drives `async_handshake` and harvests `handshake_result.peer_id` (E-2). The accept→attach window is owned by the accept-scope cancellation domain (E-7).

**Listener acquisition** (decided at implement 2026-05-30, user-approved; SC-010 delta #6): the engine builds each acceptor's `asio_listener` from `asio_listener::Config{ bind_endpoint = cfg.reconnect_endpoint, ssl_cfg from the acceptor's transport factory }` — `reconnect_endpoint` is repurposed as the acceptor's bind/listen endpoint (no new `SessionConfig` field; `TransportFactory` has no listener method by design). A `bind_endpoint` port of `0` yields an OS-assigned port, discoverable via the new `Engine::acceptor_bound_endpoint(SessionId) const` accessor (used by the standalone test-initiator and by ops). The listener is held per-acceptor in the engine and torn down by `stop()` alongside the accept-scope domain.

### Read-pump (per established session, both roles)
A coroutine `co_spawn`ed on the **session strand**: owns one `wire::Framer` + one session-lifetime `pmr_carry_buffer` + a bounded `frame_view out` buffer (E-3), passes each `async_read_some` chunk to `Framer::feed(incoming, carry, out)`, and `co_await`s `session.on_inbound_frame(frame)` (`session.hpp:230`) per complete frame until EOF/read-error/cancellation. Backpressure is natural (no inbound queue — the next read waits on the current `on_inbound_frame`). Scope is admin/session-layer flow; no app-message user sink (research.md R10).

### Live peer identity (T-041 closure input)
`Session::live_peer_id_` (`session.hpp:552`, `std::optional<peer_identity>`). On the **initiator** path 014 sets it via `install_reconnected_transport(unique_ptr<Transport>, handshake_result)` (`session.hpp:475-477`, body `session.cpp:206`) — which also **re-enters `LogonSent`** (initiator-only). The **acceptor** path does NOT use that primitive (it would force the wrong FSM branch — Gate A Codex-2); it uses a distinct acceptor attach primitive (E-2) that sets `live_peer_id_` **without** an FSM transition, strictly-happens-before the acceptor gate runs. 015 makes the acceptor gate at `session.cpp:1048` consume `live_peer_id_` (E-4).

---

## Design records

### E-1 — Engine ↔ Session ownership & wiring
The engine constructs each `Session` with the **public, synchronous** ctor `Session(const EngineConfig&, const SessionConfig&)` (`session.hpp:95`) and `co_await`s `open()` (`:114`, awaitable) to bind the session executor (strand) + clock + arena. **There is no `make_session` factory and no `Application&` parameter** (Gate A Codex-3/New-2). Outbound is wired via **`SessionConfig::transport_send`** — the `std::function<void(span<const std::byte>)>` captured ONCE at `open()` into `transport_send_` and immutable thereafter (`session.hpp:530-534`). Because under the engine's **lazy-connect model the live transport does not exist until a peer connects** (after register/open) for **either** role, the engine captures a **rebindable forwarding slot** as `cfg.transport_send` at open (initially a no-op) and repoints it at the live `Transport::async_write` when the transport becomes live: **acceptor** → during `attach_accepted_transport` (E-2 / research.md R7); **initiator** → during `install_reconnected_transport` (E-1a, Clarifications 2026-05-31). **This AMENDS the 014 behavior** where `install_reconnected_transport` left `transport_send_` untouched (valid only in 013/014's per-session-direct model, where `transport_send` was bound to a real transport at config time — an assumption that does not hold under lazy-connect). Inbound is driven by the read-pump (E-3). The engine is the lifetime owner (`unique_ptr<Session>` in the registry); sessions never outlive the engine (E-7 join-before-clear).

### E-1a — Initiator connect, outbound wiring & Logon emission (Clarifications 2026-05-31)
Symmetric to E-2 (acceptor). The engine's `run_connect_loop` (FR-003), under the per-session cancellation scope, must establish in **connect-then-Logon** order — grounded in **QuickFIX-cpp** (`Initiator::getSession`→`setResponder` on connect, then `Session::next()`→`generateLogon`, Session.cpp:139) and **Fix8** (`Session::start`→`_connection->connect()` then `send(generate_logon)`, runtime/session.cpp:180/201): both emit the Logon strictly **after** the connection + outbound sink exist.
1. Construct the `Session` + `co_await open()` — `open()` performs session/FSM/executor/clock/arena setup but, for an **engine-driven initiator**, **does NOT emit the initial Logon at open time** (the open()-time Logon-emit at `session.cpp:447-518` is valid only under the per-session-direct model). **As-built (T016(d))**: a **defer-emit mode** via the new `SessionConfig::engine_managed` flag (default `false`). `run_connect_loop` sets `entry.config.engine_managed = true` before constructing the Session; `open()`'s initiator arm is then a **no-op** (no `LogonSent` transition, no Logon emit — exactly like the acceptor arm). The Logon emission was **extracted** from `open()` into a private `Session::emit_initiator_logon_()` (behavior-preserving), called from `open()` for the per-session-direct model (`engine_managed=false`) AND from `drive_reconnect()` for the engine path (post-connect). Default `false` ⇒ 013/014 unchanged. The REQUIRED behavior holds: no Logon on the wire before the transport is live.
2. `co_await session.drive_reconnect()` — **new public** awaitable wrapper over the private `reconnect_fsm_.drive_reconnect_attempt()` (014's bounded reconnect/connect/handshake/authorize loop). On success it calls `install_reconnected_transport(live_transport, handshake_result)` which (015) sets `live_peer_id_`, takes ownership of `reconnected_transport_`, **rebinds `transport_send_`** to the live `Transport::async_write` (the **symmetric initiator-attach**), and re-enters `LogonSent`. Loop exhaustion → `transport_reconnect_limit_exceeded` → connect loop ends.
3. **Emit the initial Logon POST-connect** over the now-live `transport_send_` (`build_logon` + `store_then_emit`, seq 1). **As-built**: this is **folded into `drive_reconnect()`** (it calls `emit_initiator_logon_()` after a successful `drive_reconnect_attempt()`), NOT a separate connect-loop step — keeping the SC-010 public surface at exactly the two documented methods rather than exposing a third (private) emit entry point.
4. Read-pump (E-3) on `session.live_transport()` (**new public** accessor returning the live `reconnected_transport_`/`accepted_transport_`) delivers the peer's Logon-ack + subsequent frames to `on_inbound_frame` (`LogonSent → Active`).
5. EOF/read-error → the read-pump's existing disconnect handling (`close(close_mode::terminal)` → `Disconnected`); the connect loop ends.
- **Scope (Option A, user-approved 2026-05-31)**: single connect+pump; **multi-cycle reconnect-respin DEFERRED** — `close(terminal)` is permanent (`lifecycle::closed_drained`) and 014 has no tested multi-cycle reconnect-on-one-Session; symmetric with US1's single-peer acceptor (re-accept deferred). Tracked as a follow-up; documented Gate-B completeness note.
- **Public surface (SC-010)**: `Session::drive_reconnect()` + `Session::live_transport()` (mirroring the public `attach_accepted_transport`). **As-built also adds SC-010 delta (9)**: the `SessionConfig::engine_managed` bool (default `false`) — the engine-internal defer-emit discriminator (T016(d)). It is a config field rather than a public method/friend to stay consistent with this feature's engine↔Session coupling pattern (engine-set state via documented surface, default preserves the per-session-direct model).
- **REGRESSION WATCH (half-restructure, `[[feedback_half_restructure_symmetric_api]]`)**: amending the shared `install_reconnected_transport` to rebind `transport_send_` changes 014's reconnect-path outbound sink (config-bound → live `reconnected_transport_`). Re-run the 014 reconnect suite (`test_reconnect_live_happy_path.cpp` et al.) — confirm no test asserted outbound went to the config-time transport.

### E-2 — Acceptor handshake, bounded first-frame read, resolution & attach (R4/R7/R9)
`async_accept()` yields a **TCP-connected, NOT-yet-TLS-handshaken** `Transport` with no `handshake_result`/`peer_id` (`listener.hpp:45-53`). The accept loop, under the accept-scope cancellation domain (E-7), must:
1. **Run the TLS handshake itself** — obtain the `TlsTransport`, `co_await async_handshake(...)`, harvest `handshake_result.peer_id` (symmetric to the initiator's `drive_reconnect_attempt`; Gate A Codex-1). Handshake failure → close + reclaim slot.
2. **Bounded first-frame read** (FR-014: byte cap + handshake/Logon deadline) of the inbound Logon to learn the peer's CompIDs; over-budget / timeout → close + reclaim slot (Gate A Codex-10).
3. Resolve `SessionId::reversed_from_logon(begin_string, logon.SenderCompID(49), logon.TargetCompID(56))` against the registry.
- **Match** → attach via the **distinct acceptor attach primitive** (research.md R7; design-named `attach_accepted_transport`, exact spelling locked at implement): take ownership of the live transport, rebind `transport_send_`, set `live_peer_id_` from the harvested `handshake_result.peer_id`, and **DO NOT transition the FSM** (leave the session on the acceptor `NotConnected → LogonReceived` path). **NOT `install_reconnected_transport`** — that re-enters `LogonSent` (initiator-only) and would mis-drive the acceptor (Gate A Codex-2). Then spawn the read-pump and deliver the first Logon to `on_inbound_frame`, where the acceptor gate (E-4) authorizes against `live_peer_id_`.
- **No match** (static-only, R2) → reject: close the transport, log `session_unknown_acceptor_session` (= 121) at the connection level, create no session.
- **Happens-before invariant (Gate A New-1)**: on the session strand, `live_peer_id_` is set by the attach primitive **strictly-happens-before** the first `on_inbound_frame` reaching the acceptor gate. A delayed/absent identity falls to `else if (is_mtls) → fail-CLOSED` (safe). Regression test: acceptor mTLS, identity delayed → assert fail-CLOSED, never admit.
- **First-frame handoff (DIRECT delivery — DR-7, round 2)**: the accept loop delivers the already-read first Logon **directly to `on_inbound_frame`** (the same frame it parsed to resolve the SessionId), then the read-pump takes over for all **subsequent** bytes. The first frame is NOT re-fed into the read-pump's framer carry — there is exactly one model (direct delivery), consistent with realized-behavior C1.
- This mirrors QFC `Session::lookupSession(message, true)` + QFJ `getReverseSessionID`.

### E-3 — Framing: the real `wire::Framer::feed` surface (R8)
`Transport::async_read_some` yields bytes, not frames; `on_inbound_frame` expects a verified frame (`session.hpp:216-218` "after parse/frame-validate"). **Decision: use the shipped `wire::Framer`** (`framer.hpp`). The real surface (verified first-hand, `framer.hpp:131-136`) is `expected_t<span<frame_view>> Framer::feed(span<const std::byte> incoming, pmr_carry_buffer& carry, span<frame_view> out) noexcept` + `pending_bytes()` — **there is NO `feed(bytes)`/`next()`** (the prior contract pseudo-code was fabricated, Gate A Codex-5). `session` already depends on `wire` (`check_layers.py:29`), so no layer edge. The read-pump owns one `Framer`, one session-lifetime `pmr_carry_buffer`, and a bounded `frame_view out` buffer; a frame exceeding the carry capacity → `wire_frame_too_large` (`error.hpp:60`) → close (no silent truncation). **Capacity** is a named constant (`kReadPumpCarryCapacity = 64 KiB`; `SessionConfig` has **no** `max_frame_bytes` field — verified at T015 implement); the **arena** is `cfg.framer_carry_arena ?: std::pmr::new_delete_resource()`. NOTE the read-pump must **drain all complete frames** the framer can emit from each read before issuing the next `async_read_some`: `Framer::feed` emits at most `out.size()` frames per call and retains the surplus in `carry`, so multiple frames coalesced into one read must be re-fed (carry-only) until none remain, else a coalesced frame is dropped on EOF (T015 drain-loop fix).
- **Rationale**: avoids duplicating frame-delimiting logic already shipped + fuzzed in 004; keeps `on_inbound_frame`'s contract (verified frame in) intact.

### E-4 — Acceptor live-identity gate arm (T-041 closure) — ONE acceptor site (Gate A New-7)
014 added the live-identity arm to the **initiator** gate at `session.cpp:1864` (inside `case fsm_state::LogonSent`). The **acceptor** gate is the **single** site `session.cpp:1048` (the `NotConnected → LogonReceived` branch). **`session.cpp:1913` is NOT a second acceptor gate** — it is the initiator's leftover seam arm adjacent to `:1864` in the same `LogonSent` case (the case opens at `:1707`). 015:
- **Adds new arm 1-live at `:1048` ONLY** (ahead of the seam arm): `if (live_peer_id_.has_value() && is_mtls) → authorize against live_peer_id_` → admit on-list / fail-CLOSED (`session_compid_unauthorized = 117` + `session_event_compid_authorization_failed`) off-list/absent.
- The accept loop sets `live_peer_id_` via the acceptor attach primitive (E-2) before the Logon is processed.
- **Symmetric-fix discipline** (Gate A New-7, correcting the inverted geometry): *add* the arm at `:1048` (one site); *remove* the seam arm at **both** `:1048` (acceptor) and `:1913` (initiator) once the seam field is deleted (E-6). Audit via grep for every `logon_peer_identity_override.has_value()` consumer — there are two (one per site).

### E-5 — Registry concurrency: single-executor confinement (§XV.9) [gate-b/r1: strand removed]
The registry (`unordered_map<SessionId, SessionEntry>`) is mutated by `register_session` (pre-start, single-threaded by contract) and read/iterated by `start()`, `stop()`, and the accept/connect loops — all on the same injected executor. **Decision (015): single-executor confinement** — `register_session` is called before `start()` (single-threaded); all subsequent access (lookup, loop entry, stop) runs on the same executor. NO strand, NO `std::mutex`. Honours `[const §XV.9]` (no `std::mutex` in awaitable-corpus headers). Multi-threaded `io_context` is NOT supported in 015 — a future strand-hardening pass may add it. `engine_strand_` was removed (`gate-b/r1`; it was constructed but never used — a misleading dead member). **Note (Gate A New-4)**: confinement protects the *map*, NOT the *pointee lifetime* — see E-7 for the join-before-clear rule that prevents a session-strand read-pump from holding a dangling `Session*`.

### E-6 — Seam removal (FR-009) & test re-pointing
`SessionConfig::logon_peer_identity_override` (`session_config.hpp:229`; the `:1048`/`:1913` consumers) is removed once E-4 lands. Consequences (all in-slice):
- Delete the field + its guard arm at **both** sites (`session.cpp:1048` acceptor, `:1913` initiator); each guard becomes two-arm (live-identity / `else if is_mtls` fail-CLOSED / non-mTLS skip).
- **Re-point the binding-logic tests**: 013/014's on-list/off-list/absent tests drove the seam; they must now drive a **live handshake identity** over the loopback-TLS acceptor fixture (the live acceptor path E-2 makes this feasible — the seam existed only because `mock_transport` had no real identity). This is the SC-006 proof that no test depends on the seam.
- **No field-count `static_assert` to update** — the only `SessionConfig` guard is `static_assert(std::is_copy_constructible_v<SessionConfig>, ...)` (`session_config.hpp:260`); the prior "field-count static_assert at `:255`" was invented (Gate A Codex-6). Removing the optional field preserves copyability, so the existing assert still holds; verify it after removal (no edit expected).
- Grep gate: zero `logon_peer_identity_override` occurrences in `src/` and `include/` after removal; tests reference only the live path.

### E-7 — Accept-scope lifecycle + teardown ownership (Gate A Root cause #4)
A per-listener **accept-scope cancellation domain** (distinct from each per-session `session_cancel`) owns the window between `async_accept` returning a transport and the session being attached: (a) the accepted transport, (b) the TLS handshake, (c) the bounded first-frame read (FR-014), (d) `open()` if done lazily. On handshake failure, first-frame timeout/over-budget, construction failure, or `stop()` mid-accept → close the transport, reclaim the slot; other peers unaffected.
- **Destructor**: `~Engine()` is a strict `assert(stopped())` precondition — **no synchronous best-effort teardown** (it cannot run the caller-driven loop to drain coroutines holding raw `Session*` → UAF, the 014 Gate-B burn; Gate A Codex-9). Destruction requires a prior `co_await stop()`.
- **Join-before-clear**: `stop()` total-cancels AND **joins** every accept loop, connect loop, and read-pump **before** clearing the registry that owns the `Session` (Gate A New-4).
- **Closes transports (T018 as-built, US3)**: total-cancel alone is **insufficient** to unwind an *idle established* read-pump — it blocks in `Transport::async_read_some` with no peer EOF, and `cancellation_type::total` does **not** break the in-flight SSL read (the US1/US2 live tests only ever stopped via peer-close EOF, so this was never exercised until US3's two idle in-engine sessions). `stop()` therefore **closes each live transport** before the join: each role loop publishes its live `transport::Transport*` into `SessionEntry::live_transport` (non-owning; set after `attach_accepted_transport` / after the initiator's successful `drive_reconnect`), and `stop()` calls `live_transport->close()` (synchronous, idempotent) on every entry after emitting the cancels and before joining — the closed socket fails the in-flight read, the pump unwinds, the counter decrements, and the join completes. This realizes `stop()`'s own "closes transports; then JOINS" contract (previously only total-cancel was emitted). **No new public method** — `live_transport` is an internal registry field (SC-010 surface unchanged). **Known limitation (out of 015 scope)**: a *mid-connect, never-established* initiator (peer down) is not promptly torn down — the default `ReconnectPolicy{}` empty schedule busy-spins at 0 backoff and its in-flight `async_connect` is not promptly cancelled by total-cancel (a 012-transport `async_connect`-cancellation concern, outside E-1a's single-connect happy path); flagged for follow-up.
- **`open()` sequencing**: `open()` (awaitable) is `co_await`ed as the first step inside each spawned loop, never in the synchronous `void start()`; `lookup()` may therefore return a constructed-but-not-yet-open session, or null for a registered acceptor with no peer (Gate A New-3).

---

## Error model delta (R5 — pinned first-hand at Gate A)
| Condition | Code | New slot? |
|---|---|---|
| Unmatched acceptor Logon (no registry entry) | `session_unknown_acceptor_session = 121` (next free; surfaces at connection level only — close + log, never a Session event) | **Yes, +1** — verified no reusable `unknown_session`/`no_such_session` code (`error.hpp:616-665`); append-only `[const §X.4]` |
| Duplicate `register_session` (same SessionId) | reuse `session_invalid_argument` (= 119) | No |
| Acceptor authorization failure | reuse `session_compid_unauthorized` (= 117) | No |

Slot 70 stays a permanent hole; 120 is the current max; 121 is the next free; never renumber.

## State / lifecycle
`Engine`: `constructed → started → stopping → stopped`. `start()` legal once from `constructed`; `stop()` idempotent from any state (no-op if already `stopped`). Per-session: the existing `Session` lifecycle (`open → … → close`) is unchanged; the engine owns construction/`open()`/teardown ordering (E-7).

## Traceability
FR-001→Engine; FR-002→SessionId+register_session+E-5; FR-003→accept/connect loops; FR-004→read-pump+E-3; FR-005→E-2; FR-006/007/008→E-4; FR-009→E-6; FR-010→E-4 catalogue flip; FR-011→stop()+total-cancellation+E-7; FR-012→Out-of-scope guard; FR-013→structure+R10; FR-014→E-2/E-7 (bounded first-frame read).
