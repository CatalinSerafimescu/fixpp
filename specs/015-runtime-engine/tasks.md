---
description: "Task list — 015-runtime-engine"
---

# Tasks: Public Initiator/Acceptor Runtime Engine & Full T-041 Closure

**Input**: Design documents from `specs/015-runtime-engine/`
**Prerequisites**: plan.md, spec.md (US1–US4, all P1), research.md (baseline table + R1–R10), data-model.md (SessionId/Engine/SessionEntry + E-1..E-7), contracts/ (engine_api.md + realized-behavior.md C1–C8), quickstart.md

**Tests**: INCLUDED — TDD red-green-refactor is mandatory per `[const §VII.1/§VII.3]` and plan §Testing (GoogleTest/GoogleMock under ctest — NOT Catch2, Gate A Codex-7). Write each story's tests first and confirm they FAIL before implementing.

**Paths**: repo root = the library submodule `research/G19-fix-fpml-iso20022/library/`. All `src/`, `include/`, `tests/` paths below are relative to that root. All `file:line` anchors are **branch-local-verified** on `015-runtime-engine` (Gate A round 2 confirmed `src/session/session.cpp` is the FULL file, not a stub).

**Scope guard (FR-013)**: 015 ships the public engine + full T-041 closure and is **bounded below the Phase-5 service wrapper** — NO config-file parsing, NO `Application` user-callback ecosystem, NO store/log factory abstractions, NO C-ABI / control-plane / observability / pybind, and **no user sink for inbound *application* messages** (the read-pump delivers every frame to `Session::on_inbound_frame`, which stores it + runs the admin/session-layer handling — research R10 / Gate A New-2). The dynamic-session-provider hook is **deferred** (R2 — static reversed-CompID matching alone closes T-041). Non-mTLS permissive path unchanged.

**Public-surface delta (SC-010)**: exactly the documented runtime-engine additions + the seam removal — (1) NEW type `fixpp::session::Engine`; (2) NEW value type `fixpp::session::SessionId`; (3) NEW non-virtual `Session::attach_accepted_transport(...)`; (4) +1 error slot `session_unknown_acceptor_session = 121`; (5) REMOVED `SessionConfig::logon_peer_identity_override`; (6) NEW `Engine::acceptor_bound_endpoint(SessionId) const` accessor — exposes the OS-assigned bound port of an acceptor's listener for connection/discovery (the engine builds the acceptor `Listener` from the config's `reconnect_endpoint` repurposed as the bind endpoint; decided at implement 2026-05-30, user-approved); (7) NEW public `Session::drive_reconnect()` awaitable (engine connect-loop driver over the private `reconnect_fsm_.drive_reconnect_attempt()`); (8) NEW public `Session::live_transport()` accessor (read-pump's live-transport source) — both added per Clarifications 2026-05-31 (T016), mirroring the public `attach_accepted_transport`; (9) NEW `SessionConfig::engine_managed` bool (default `false`) — the engine-internal connect-then-Logon defer-emit discriminator (T016(d) as-built); the engine sets it for its initiator sessions, default `false` preserves the 013/014 per-session-direct emit-at-open model. No new module, no `check_layers.py` ALLOWED-map change, no `[const §XIV.2]` pluggable interface (R1/R2).

---

## Phase 1: Setup (Shared baseline verification)

**Purpose**: Re-confirm the shipped-reality anchors the design depends on before coding (the bundle was re-derived first-hand at Gate A after the original Phase-0/1 baseline was found partly fabricated — research baseline table).

- [X] T001 Re-verify the research.md shipped-reality baseline anchors on the branch base before coding — confirm each is still accurate: `Listener::async_accept()` returns a TCP-only (TLS-not-issued, no `peer_id`) `Transport` at `include/fixpp/transport/listener.hpp:45-53`; `Session::on_inbound_frame(std::span<const std::byte>)` at `include/fixpp/session/session.hpp:230`; the public synchronous ctor `Session(const EngineConfig&, const SessionConfig&)` at `session.hpp:95` + awaitable `open()` at `:114` (NO `make_session`, NO `Application&`); `transport_send_` captured-once-at-open at `session.hpp:530-534`; `live_peer_id_` at `session.hpp:552`; `install_reconnected_transport` (two-arg, re-enters `LogonSent`) decl `session.hpp:475-477` / body `src/session/session.cpp:206`; the **single** acceptor seam gate `session.cpp:1048` (NotConnected→LogonReceived) and the **initiator** seam arm `session.cpp:1913` (inside `case fsm_state::LogonSent`, opens `:1707`; 014 initiator live arm ~`:1864`); the seam field `session_config.hpp:229` + the only guard `static_assert(std::is_copy_constructible_v<SessionConfig>)` at `:260` (NO field-count assert); `Framer::feed(incoming, carry, out)` + `pending_bytes()` at `include/fixpp/wire/framer.hpp:131-136`, over-capacity → `wire_frame_too_large` (`error.hpp:60`); error-slot boundary `session_compid_unauthorized = 117` / `session_invalid_argument = 119` / max `session_seqnum_too_high = 120` with 121 next-free (`include/fixpp/core/error.hpp:616-665`); `EngineConfig` carries NO `Application` (`include/fixpp/core/engine_config.hpp:106-148`); `check_layers.py:29` has no `runtime` key and grants `session → {core, dictionary, wire, transport, log, otel}`. Record any drift as a bundle defect before proceeding.
- [X] T002 [P] Confirm the CodeGraph index is fresh (`codegraph sync` from the submodule) and the live-loopback TLS fixtures (`tests/tls/fixtures/leaf_rsa2048.pem` + `ca.pem`, addressable via `FIXPP_TLS_FIXTURE_DIR`) are present — the engine harness + the re-pointed binding tests reuse them (mirrors 014 T002; data-model E-6).

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: The shared engine substrate every story compiles and runs against — the new error slot, the `SessionId` key type, the `Engine` public header + lifecycle skeleton, and the shared live-loopback engine harness. The per-role loop **bodies** (accept loop = US1, read-pump/connect loop = US2) are stubbed here and fleshed out in their stories.

**⚠️ CRITICAL**: No user-story work can begin until this phase is complete — `engine.hpp`/`engine.cpp` and the harness are the substrate for all of US1–US4.

- [X] T003 [P] Append `error::session_unknown_acceptor_session = 121` in `include/fixpp/core/error.hpp` (next free after `session_seqnum_too_high = 120`; verified no reusable `unknown_session`/`no_such_session`/`unknown_acceptor` code, `error.hpp:616-665`). Append-only — slot 70 stays a permanent hole, never renumber (`[const §X.4]`; R5 #1). (FR-005/006; C7; data-model "Error model delta".)
- [X] T004 [P] Create the `SessionId` value type in `include/fixpp/session/engine.hpp` — `struct SessionId { std::string begin_string, sender_comp_id, target_comp_id; }` with defaulted `operator==`, a `std::hash<SessionId>` specialization over all three fields (for `unordered_map` keying), `static SessionId from_config(SessionConfig const&)`, and `static SessionId reversed_from_logon(std::string begin_string, std::string_view logon_sender, std::string_view logon_target)` → `{begin_string, sender = logon_target, target = logon_sender}`. **NO `qualifier` field** (Gate A New-5). Add focused unit test `tests/session/engine_session_id_test.cpp` (value equality, hash distribution, `from_config` round-trip, and the reversed-mapping for acceptor resolution). (R6/E-1; FR-002; data-model "SessionId"; contracts engine_api.md.)
- [X] T005 Author the `Engine` public declaration in `include/fixpp/session/engine.hpp` (after T004, same file): the class shell + `SessionEntry { std::unique_ptr<Session> session; enum role; SessionConfig config; asio::cancellation_signal session_cancel; }`, all public signatures with doc-comments per contracts/engine_api.md — `Engine(asio::any_io_executor, fixpp::core::EngineConfig)`, deleted copy/move, `~Engine()` (strict `assert(stopped())`, NO synchronous best-effort), `[[nodiscard]] expected_t<void> register_session(SessionConfig)` (NO `Application&`), `void start()` (non-blocking), `[[nodiscard]] asio::awaitable<void> stop()`, `[[nodiscard]] Session* lookup(SessionId const&) const`, `[[nodiscard]] bool stopped() const noexcept`; private members per data-model "Engine" (executor, held `EngineConfig`, `unordered_map<SessionId, SessionEntry>` registry, per-acceptor `Listener` + per-listener accept-scope `cancellation_signal`, the engine strand, the `stopped` flag, the rebindable outbound send-slot machinery). **No `std::mutex`** — registry sequencing is the engine strand only (`[const §XV.9]`; E-5). **T005 has TWO deliverables — neither may be skipped: (1)** the `engine.hpp` declaration above; **(2)** a **mandatory** doc amendment — add the `architecture.md §4.4 (session public types)` entries for `fixpp::session::Engine` + `fixpp::session::SessionId` (NO §2.2/§2.3 layer-graph change), required by `[const §VI.4]` bidirectional traceability before the PR lands. Then run `tools/check_layers.py` after the header lands (R1/R6; `[[feedback_gate_b_check_layers_post_fixer]]`). Depends T003, T004.
- [X] T006 Implement the engine lifecycle substrate in `src/session/engine.cpp` (NEW): ctor (hold the injected executor, derive the engine strand from it); `register_session` (key on `SessionId::from_config(cfg)`, duplicate → `session_invalid_argument = 119`, store config + role ONLY — does **not** construct a `Session`; R5 #2 / lazy-construction); `lookup` (returns null for a not-registered id OR a registered-but-not-yet-open session — Gate A New-3); `stopped()`; `start()` shell (non-blocking — `co_spawn` one per-role loop coroutine per registered session on `exec`; each loop FIRST does `co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation())` then `co_await Session::open()` before any work — open() cannot run in the synchronous `void start()`; loop bodies `run_accept_loop`/`run_connect_loop` declared here as minimal compiling stubs, fleshed out in US1/US2); `stop()` (idempotent — second call no-op via the stopped flag; **total-cancel** every accept loop, connect loop, read-pump, accept-scope domain + in-flight handshake; close transports; then **JOIN** all outstanding work **before clearing the registry** that owns the `Session` objects — join-before-clear, Gate A New-4); `~Engine()` (strict `assert(stopped())` — no sync drain, Gate A Codex-9). Capture the engine-owned **rebindable send-slot** as `cfg.transport_send` at open() so an acceptor `Session` is `lookup()`-addressable before any peer connects (E-1/R7 (b)). (FR-001/002/003/011; E-1/E-5/E-7; R1/R9; `[const §XI.2/§XI.4/§XV.9]`; `[[feedback_asio_cospawn_total_cancellation_default]]`.) Depends T005.
- [X] T007 Provide a shared live-loopback-TLS **engine** test harness/helper in `tests/session/` (an `Engine` bound to a caller-driven `asio::io_context`, a real `asio_tls_transport_factory`, loopback acceptor + initiator endpoints, fixtures via `FIXPP_TLS_FIXTURE_DIR`) reused by the US1/US2/US3 live tests so they do not depend on each other's test files (mirrors 014 T005; reuses `tests/session/loopback_tls_session_harness.hpp` where applicable). Depends T006.

**Checkpoint**: Error slot + `SessionId` + `Engine` header/skeleton compile, `check_layers.py` passes, the engine harness is available — user stories can begin.

---

## Phase 3: User Story 1 — Live acceptor production path (Priority: P1) 🎯 MVP

**Goal**: Realize the acceptor `accept → handshake → reversed-CompID resolve → attach → live-identity authorize` production path so the acceptor Logon gate binds the **real** `handshake_result.peer_id` (not the test seam) — the half of T-041 that 014 deferred. (FR-005/006/007/008/014; US1; SC-001/002/011; C1/C3/C7; E-2/E-4; R3/R4/R7.)

**Independent Test**: Stand up a live acceptor on the engine harness and connect a test initiator over real loopback-TLS; assert an on-list identity admits the session to its established state and an off-list/absent identity fails the acceptor Logon CLOSED with `session_compid_unauthorized` + `session_event_compid_authorization_failed`.

### Tests for User Story 1 (write FIRST, confirm FAIL) ⚠️

- [X] T008 [P] [US1] `tests/session/engine_acceptor_test.cpp` — acceptor harness: initiator connects over loopback-TLS with an **on-list** identity → accept loop runs the handshake, resolves by reversed CompID, attaches, direct-delivers the first Logon → acceptor gate admits → session reaches its established state (SC-001; US1 AC1; C1/C3).
- [X] T009 [P] [US1] `tests/session/engine_acceptor_failclosed_test.cpp` — under mTLS: (a) **off-list** identity → fail-CLOSED `session_compid_unauthorized` (=117) + `session_event_compid_authorization_failed`; (b) **absent/delayed** identity — the happens-before regression (Gate A New-1): deliberately delay/withhold `live_peer_id_` so the gate runs first → assert it falls to the `else if (is_mtls)` fail-CLOSED arm, **never admits** (SC-002; US1 AC2; C1 invariant/E-4).
- [X] T010 [P] [US1] `tests/session/engine_firstframe_test.cpp` — bounded pre-session window (FR-014): handshake stall / silent peer / partial first frame / more than the first-frame byte budget before a valid Logon → connection closed within the configured deadline, transport + accept slot reclaimed, **other peers unaffected**, ASan no-leak across N attempts (SC-011; C1 steps 2-3 / C5 accept-scope domain).

### Implementation for User Story 1

- [X] T011 [US1] Add the acceptor attach primitive `attach_accepted_transport(std::unique_ptr<Transport>, handshake_result)` (exact spelling locked here) — private/non-virtual decl in `include/fixpp/session/session.hpp` + body in `src/session/session.cpp`: take ownership of the live accepted transport, **rebind the rebindable send-slot** to the live `Transport::async_write`, set `live_peer_id_` from `handshake_result.peer_id`, and **do NOT transition the FSM** (leave the session on the acceptor `NotConnected → LogonReceived` path). **NOT `install_reconnected_transport`** — that re-enters `LogonSent` (initiator-only; Gate A Codex-2). (E-2/R7; C1 step 5.) Depends T006.
- [X] T012 [US1] Implement `run_accept_loop` in `src/session/engine.cpp` under the per-listener accept-scope cancellation domain: `co_await listener.async_accept()` (TCP-only) → obtain the `TlsTransport`, `co_await async_handshake(...)` itself + harvest `handshake_result.peer_id` (handshake failure → close + reclaim slot) → **bounded first-frame read** (byte cap + handshake/Logon deadline; over-budget/timeout → close + reclaim, FR-014) parsing the inbound Logon via `wire::Framer` → `SessionId::reversed_from_logon(begin_string, Logon.SenderCompID(49), Logon.TargetCompID(56))` registry resolve → **match**: lazily ctor+`open()` the `Session`, `attach_accepted_transport` (T011), **direct-deliver** the already-read first Logon to `on_inbound_frame` (DR-7 — NOT re-fed into the read-pump carry), then spawn the read-pump (US2); **no match** (static-only): close the transport + log `session_unknown_acceptor_session = 121` at the connection level, create NO session (R2/C7). (FR-005/006/014; C1; R3/R4/R9; `[const §XI.2]`.) Depends T011.
- [X] T013 [US1] Add the acceptor **live-identity gate arm** at the single site `src/session/session.cpp:1048` (the `NotConnected → LogonReceived` branch), inserted **ahead of** the seam arm, mirroring 014's initiator arm at `:1864`: `if (live_peer_id_.has_value() && is_mtls) { authorize(*live_peer_id_, asserted_compid); → admit on-list / fail-CLOSED off-list-or-absent (session_compid_unauthorized = 117 + session_event_compid_authorization_failed + Disconnected); }` — the inherited CN→SAN-DNS→SAN-URI→SHA-256 extraction order and the event/code shapes are UNCHANGED (FR-008); 015 changes only the identity **source** (seam → live `handshake_result.peer_id`). Pin the happens-before invariant in a code comment (`live_peer_id_` set on the session strand strictly-happens-before the first `on_inbound_frame` reaching this gate). (FR-006/007/008; T-041; C3/E-4.) Depends T012.

**Checkpoint**: A live acceptor admits an on-list live identity to its established state and fails CLOSED off-list/absent over loopback-TLS — the acceptor half of T-041 is operable in production. MVP complete and independently testable.

---

## Phase 4: User Story 2 — Continuous inbound read-pump for both roles (Priority: P1)

**Goal**: Turn the wired-up transport into a live running session — a continuous read-pump that feeds **every** inbound frame to `Session::on_inbound_frame`, in order, on the session strand, until close, for both roles (014 proved only single-frame on the reconnect path). (FR-003/004/012; US2; SC-003; C2/C5; E-3; R3/R8/R10.)

**Independent Test**: Drive a stream of N inbound frames into an established session's transport (initiator via the shipped 014 `install_reconnected_transport` path; acceptor via T011) and assert all N reach `on_inbound_frame` exactly once, in arrival order, on the session strand — no drops, duplicates, or off-strand dispatch.

### Tests for User Story 2 (write FIRST, confirm FAIL) ⚠️

- [X] T014 [P] [US2] `tests/session/engine_readpump_test.cpp` — established session: a sequence of N frames → each delivered to `on_inbound_frame` exactly once, in arrival order, on the session strand; a frame exceeding the carry capacity → `wire_frame_too_large` → pump closes (no silent truncation); transport EOF / read-error → pump stops and the session goes through its existing disconnect handling (no new disposition) (SC-003; US2 AC1/AC2; FR-004/012; C2). **DONE** (submodule `a997cbf`): 3 cases (in-order exactly-once via a concatenated single-write to force coalescing; over-capacity→Disconnected; EOF→Disconnected), observability via `seqnum_mgr_test_access().next_inbound_unsafe()` under `FIXPP_TEST_HOOKS`; cases 2/3 assert `state==Disconnected` (real witnesses).

### Implementation for User Story 2

- [X] T015 [US2] Implement the read-pump coroutine in `src/session/engine.cpp`: owns one `wire::Framer`, one session-lifetime `wire::pmr_carry_buffer`, and a bounded `frame_view out` buffer; FIRST `co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation())`; loop `co_await transport.async_read_some(buf)` → `Framer::feed({buf.data(), n}, carry, out)` → for each complete frame `co_await session.on_inbound_frame(frame.bytes())` (`session.hpp:230`, returns `asio::awaitable<expected_t<void>>`) — **if `on_inbound_frame` returns an `expected_t<void>` error, close the transport and stop the pump** (mirror the EOF/read-error arm; no new disposition — FR-012); over-capacity → `wire_frame_too_large` (`error.hpp:60`) → close; EOF/read-error → stop. **Natural backpressure** — no inbound queue; the next read waits on the current `on_inbound_frame`. **Scope = admin/session-layer flow only** — no user sink for inbound application messages (FR-013/R10). **`[const §VII.7 WAIVER]`**: the read-pump adds no parsing logic — `wire::Framer::feed` is already fuzzed by the 004 Framer corpus, and the new bounded-first-frame surface (deadline + byte-budget) is behaviorally tested by T010/T012 (SC-011 stall/silent/partial/over-budget). No new fuzz target required for this slice. (FR-004; C2/E-3; R8; `[const §XI.2/§XI.4]`; `[[feedback_asio_cospawn_total_cancellation_default]]`.) Depends T006. **AS-BUILT** (submodule `a997cbf`): capacity = named constant `kReadPumpCarryCapacity = 64 KiB` (`SessionConfig` has **no** `max_frame_bytes` field — task text corrected; arena = `cfg.framer_carry_arena ?: new_delete_resource()`); stop-arms call `session.close(close_mode::terminal)` (= existing disconnect path → Disconnected); pump is **`co_await`ed INLINE** from `run_accept_loop` (capture `Transport* raw` before the `attach_accepted_transport` move) so the accept loop's `counter_guard` joins it (no detached pump, no separate counter, no UAF). Added a **drain loop** (re-feed carry-only until `produced < out.size()`) — `Framer::feed` emits ≤ `out.size()` frames/call and retains the surplus, so coalesced frames must be drained before the next read or dropped on EOF (E-3). `engine_acceptor_test` client linger 2s→5s (new EOF→Disconnected behavior).
- [X] T016 [US2] Implement the **initiator live connect path** (Option A — single connect+pump; **multi-cycle reconnect-respin DEFERRED** per Clarifications 2026-05-31 / E-1a: `close(terminal)` is permanent + 014 has no tested multi-cycle reconnect; symmetric with US1's single-peer acceptor; documented Gate-B note). **TDD — write the RED test first.** **DONE** — (a) RED `fb47d14`; (c)+(d) `0663ab7` (regression gate 31/32, engine_connect stays RED); (b)+(e) GREEN: engine_connect PASSES (server got the Logon on the wire post-connect, session→Active, next_inbound==2+kN==4); full b/e regression gate GREEN (reconnect + 013/014 initiator + us1/us2/us3, ctest exit 0). As-built notes per sub-step below.
  - **(a) RED test** `tests/session/engine_connect_test.cpp` — register an initiator whose `reconnect_endpoint` targets a standalone TLS server (`LoopbackTlsFixture`'s listener side); drive the server: accept → handshake → receive the initiator's **post-connect** Logon → send Logon-ack + N heartbeats. Assert: the initiator session reaches `Active`, `next_inbound_unsafe()` advances per delivered frame (read-pump live for the initiator), and **no Logon reaches the wire before the transport is live** (connect-then-Logon, FR-003). Confirm FAIL first.
  - **(b)** Add **public** `Session::drive_reconnect()` — awaitable wrapper over the private `reconnect_fsm_.drive_reconnect_attempt()` — and **public** `Session::live_transport()` — returns the live `reconnected_transport_`/`accepted_transport_` (SC-010 (7)/(8)).
  - **(c)** Amend `install_reconnected_transport` (`session.cpp:206`) to **rebind `transport_send_`** to the live `Transport::async_write` (symmetric initiator-attach, E-1a). **REGRESSION WATCH** — re-run the 014 reconnect suite (`test_reconnect_live_happy_path.cpp` et al.); confirm no test asserted outbound went to the config-time transport (`[[feedback_half_restructure_symmetric_api]]`).
  - **(d)** Restructure `open()`'s initiator arm (`session.cpp:447-518`) so the **engine-driven initiator does NOT emit the Logon at open** (defer-emit mode or extract the emit into a connect-loop-called method — exact spelling at implement; REQUIRED: no Logon on the wire before the transport is live). Re-confirm the 013/014 per-session-direct initiator tests still pass (or are re-pointed). **AS-BUILT** (`0663ab7`): defer-emit mode via new `SessionConfig::engine_managed` (default `false`; SC-010 delta (9)); the whole initiator arm is gated on `!engine_managed` (no-op like the acceptor arm when set). Logon-emit **extracted** into private `Session::emit_initiator_logon_()` (behavior-preserving), called from `open()` (direct model) AND `drive_reconnect()` (engine model). 013/014 unchanged (default false).
  - **(e)** Implement `run_connect_loop` in `src/session/engine.cpp`: `open()` (no Logon) → `co_await session.drive_reconnect()` (connect+handshake+authorize+install+rebind) → **emit the initial Logon POST-connect** over the now-live `transport_send_` → `co_await run_read_pump(session.live_transport(), session, cfg)` until EOF → end. Grounded in QuickFIX-cpp/Fix8 connect-then-Logon ordering. **AS-BUILT**: the post-connect emit is **folded into `drive_reconnect()`** (calls `emit_initiator_logon_()` after a successful `drive_reconnect_attempt()`) to keep the SC-010 public surface at exactly the two methods; `run_connect_loop` sets `entry.config.engine_managed = true`, then `open()` → `drive_reconnect()` → `run_read_pump(live_transport())`. engine_connect server-driver teardown uses a bounded 5s timer-hold (mirror of engine_acceptor_test) — a drain-until-EOF variant hung because the initiator's `close(terminal)` does not surface a peer-visible EOF.
  (FR-003/004; C2/C5; E-1a; SC-010.) Depends T015.

**Checkpoint**: Both roles consume a continuous inbound stream on-strand, in order, exactly once — the shared running-session substrate is live.

---

## Phase 5: User Story 3 — Programmatic multi-session lifecycle via a SessionConfig-keyed registry (Priority: P1)

**Goal**: Prove the public engine surface — construct, register multiple sessions from their `SessionConfig`s, non-blocking start, clean idempotent stop, each session addressable in the `SessionId`-keyed registry, duplicate identities rejected. (FR-001/002/011; US3; SC-004/005; C5/C6; E-5/E-7; R9.)

**Independent Test**: Register two sessions (one initiator, one acceptor) from distinct `SessionConfig`s, start the engine, observe both run their loops + both retrievable by `SessionId`, then stop and assert clean teardown (no leak / no UAF / cancellation-safe) and that a second stop is a no-op; register a duplicate-identity config and assert rejection.

### Tests for User Story 3 (write FIRST, confirm FAIL) ⚠️

- [X] T017 [P] [US3] `tests/session/engine_lifecycle_test.cpp` — (a) register one initiator + one acceptor (distinct `SessionConfig`s) → `start()` → both loops run + both `lookup()`-retrievable by `SessionId` (and an acceptor with no peer yet `lookup()`s non-null-after-open / null-before-open per Gate A New-3); (b) `co_await stop()` → clean teardown across construct→start→accept/connect→stop, no leaked work, no UAF, cancellation-safe **under the sanitizer presets**; (c) a second `stop()` is a no-op; (d) a `SessionConfig` resolving to an already-registered `SessionId` → `register_session` returns `session_invalid_argument` (=119) (SC-004/005; US3 AC1/2/3; FR-002/011; C5/C6). **DONE** — two GoogleTest cases: `TwoSessionRegisterStartLookupStop` drives a **real in-engine loopback pair** (the initiator connects to the co-registered acceptor over loopback-TLS via a reserved free port; both reach `Active` — proving both loops run end-to-end) then exercises (a) null-before-open / non-null-after-open + lookup-by-id, (b) clean stop (unbounded `ioc.run()` returns → no hang), (c) second-stop no-op; `DuplicateIdentityRejected` covers (d) (no fixture; also witnesses stop-without-start is required + safe). The full-loopback choice avoids leaning on a doomed down-peer connect whose teardown latency depends on transport-layer `async_connect` cancellation (out of 015 scope — see T018 note). Surfaced + fixed the T018 gap below.

### Implementation for User Story 3

- [X] T018 [US3] Harden the engine lifecycle in `src/session/engine.cpp` against any gap T017 surfaces — idempotent `stop()` (stopped flag), join-before-clear ordering (no session-strand read-pump can dereference a freed `Session*` — Gate A New-4), `lookup()` null-pre-open semantics, `~Engine()` strict `assert(stopped())`, duplicate-rejection — and confirm there is **no `std::mutex`** in `engine.hpp` (engine-strand sequencing only; verify via the unfiltered / `-L sync` corpus ctest, `[const §XV.9]` / `[[feedback_awaitable_header_mutex_include_edge]]`). Most of this is realized in Foundational T006; this task closes the loop against the SC-004/005 witnesses. (FR-011; C5/C6; E-5/E-7.) Depends T017. **DONE** — T017 surfaced ONE real gap: `stop()` **never closed the transports** despite its own contract comment saying it does ("closes transports; then JOINS"). An *idle* established session's read-pump blocks in `async_read_some` with no peer EOF, and emitting `cancellation_type::total` alone does **not** break the in-flight SSL read (the prior live tests only ever stopped via peer-close EOF, never via cancel — so this was untested). Result: `stop()`'s join-before-clear hung forever. **Fix:** publish each session's live transport into the registry (`SessionEntry::live_transport`, a non-owning `transport::Transport*` set after `attach_accepted_transport` / after the initiator's successful `drive_reconnect`) and have `stop()` call `live_transport->close()` (synchronous, idempotent) on every entry **after** emitting the total-cancels and **before** the join — the closed socket fails the in-flight read → the pump unwinds → the counter decrements → join completes. No new public method (SC-010 unchanged; `SessionEntry::live_transport` is an internal registry field). engine_lifecycle stop now returns from an unbounded `ioc.run()` in ~ms; full 015 suite (8/8) + reconnect (4/4) GREEN. **Out of scope (documented limitation):** a connect loop pointed at a *down* peer busy-spins (default `ReconnectPolicy{}` has an empty schedule ⇒ 0 backoff) and its in-flight `async_connect` is not promptly cancelled by total-cancel — teardown of a *mid-connect, never-established* initiator is therefore not prompt. This is outside E-1a's single-connect happy-path scope and rooted in the 012 transport `async_connect` cancellation + the shared session reconnect default; flagged for a follow-up, NOT fixed in 015.

**Checkpoint**: The public multi-session lifecycle (register/start/stop/lookup, duplicate rejection, clean teardown) is proven under sanitizers — the minimum viable public runtime.

---

## Phase 6: User Story 4 — Test-seam removal and full T-041 closure (Priority: P1)

**Goal**: Remove the test-only `logon_peer_identity_override` seam from the production surface, re-point every binding-logic test onto the live handshake identity, and flip catalogue row **T-041** `implementing → done` for both roles. (FR-009/010; US4; SC-006/007; C4/C8; E-6.)

**Independent Test**: `grep -rn logon_peer_identity_override src/ include/` returns zero; the on-list/off-list/absent binding-logic tests drive `authorize()` from a live handshake identity (not the override); the feature-catalogue T-041 row reads `done`.

### Tests for User Story 4 (write FIRST, confirm FAIL) ⚠️

- [X] T019 [P] [US4] **DONE** — `engine_seam_removal_test.cpp`: `NoSeamReferenceInAnyTree` greps `src/`+`include/`+`tests/` (needle assembled from fragments so the file doesn't trip its own gate) → zero refs; `LiveIdentityDrivesAuthorization` drives an mTLS session's on-list identity through the production `live_peer_id_` path (`inject_live_identity`) → Active + `peer_identity_bound`. Both GREEN. `tests/session/engine_seam_removal_test.cpp` — the SC-006 grep gate (zero `logon_peer_identity_override` in `src/` + `include/` **+ `tests/`** — FR-009/SC-006 require that no production path *and no test* depends on the seam; T021 is the `tests/` re-point witness, T019's grep is the post-removal no-reference proof across all three trees) and an assertion that the binding-logic path under test authorizes from a **live** loopback-TLS handshake identity, not the override seam (SC-006; C4/E-6).

### Implementation for User Story 4

- [X] T020 [US4] **DONE** — removed the field + its `peer_identity.hpp` include from `session_config.hpp`; removed the seam arm at BOTH gate sites (acceptor + initiator) leaving the 2-arm guard (live / mTLS-fail-closed / non-mTLS-skip); updated the guard header comments + the obsolete acceptor-deferral notes (T-041 now CLOSED). `static_assert(is_copy_constructible_v<SessionConfig>)` still holds (peer_identity removal doesn't affect it). Production lib builds clean; zero seam refs in `src/`+`include/`. Remove `SessionConfig::logon_peer_identity_override` (`include/fixpp/session/session_config.hpp:229`) and its **seam arm at BOTH gate sites** — `src/session/session.cpp:1048` (acceptor) and `:1913` (initiator) — so each guard becomes exactly: arm-1-live / `else if (is_mtls)` fail-CLOSED / non-mTLS permissive skip. Verify the only `SessionConfig` guard `static_assert(std::is_copy_constructible_v<SessionConfig>)` (`session_config.hpp:260`) still holds after removing the optional field (no edit expected; there is NO field-count assert — Gate A Codex-6). Grep gate: zero `logon_peer_identity_override` occurrences in `src/` + `include/`. Symmetric one-pass fix per `[[feedback_half_restructure_symmetric_api]]`. (FR-009; C4/E-6.) Depends T013.
- [X] T021 [US4] **DONE** — added `tests/support/identity_injecting_transport.hpp` (`NullSinkTransport` + `inject_live_identity()` driving the production `attach_accepted_transport` → `live_peer_id_` path, role-symmetric). Re-pointed every seam-driving cell to mTLS + `inject_live_identity` after `open()`: `default_deny` (2 cells), `symmetric` (3), `principal_extraction` (1 integration cell; its A–D unit cells call `authorize()` directly, untouched), `invariant_counter_witness` (2). DELETED the two seam-only cells in `test_compid_binding_seam.cpp` (on-list/off-list — now covered by `test_live_identity_binding` + US1 acceptor tests), keeping its no-identity arm cells (mTLS-fail-closed / non-mTLS-permissive). Scrubbed all comment-only seam mentions across 4 more files + `minimal_security_profile.hpp` for the grep gate. All re-pointed targets + the full 015 suite GREEN (9/9). Re-point the 013/014 binding-logic tests that drove the seam (`tests/session/test_compid_binding_seam.cpp` + the on-list/off-list/absent cells in `test_compid_binding_mtls_fail_closed.cpp` / `test_compid_binding_symmetric.cpp` / `test_compid_binding_principal_extraction.cpp` / `test_compid_binding_default_deny.cpp`) to drive a **live handshake identity** over the loopback-TLS acceptor harness (T007) instead of `logon_peer_identity_override`; delete any test that exists only to exercise the seam. This is the SC-006 proof that no test depends on the seam (the seam existed only because `mock_transport` had no real identity — now obsolete). (FR-009; C4/E-6; SC-006.) Depends T020.

**Checkpoint**: The seam is gone from production + tests, both roles bind a live identity and fail CLOSED — T-041 is genuinely closed, not half-wired.

---

## Phase 7: Polish & Cross-Cutting Concerns

**Purpose**: Catalogue closure, the completeness gate, and the SC-008 unfiltered-suite-green discipline (pre-`/simplify` / `/speckit-verify`).

- [X] T022 **DONE** — flipped catalogue row **T-041 `implementing → done`** with the full 015 US4 closure note (both-role live binding via `attach_accepted_transport`/`install_reconnected_transport`; seam removed prod+tests; `engine_seam_removal` grep gate) + recorded the new `session_unknown_acceptor_session = 121` slot (FR-005/006 / C7) in the row. Update `feature-catalogue.md` + `coverage-index.md` with the 015 row and flip catalogue row **T-041 `implementing → done`** (both roles bind a live identity + fail CLOSED in production; the seam is gone); update any catalogue row touching the runtime engine / acceptor live path; note the new `session_unknown_acceptor_session = 121` slot in the error taxonomy. (FR-010; SC-007; C8; `[[feedback_feature_completeness_gate]]`.)
- [ ] T023 Run the feature-completeness audit (tasks ↔ FR-001..FR-014 ↔ SC-001..SC-011 ↔ catalogue, 100% or explicitly waived) — Gate-B precondition per `[const §XVII.8]` / `[[feedback_feature_completeness_gate]]`; confirm the out-of-scope guards (SC-009: no Phase-5 wrapper / C-ABI / control-plane / observability / pybind) and the public-surface delta bound (SC-010). Depends T022.
- [ ] T024 [P] Run the quickstart.md walkthroughs (initiator connect/run/stop; acceptor listen/resolve/authorize-live; T-041 fail-CLOSED) and confirm each command/behaviour matches.
- [ ] T025 Run the **unfiltered** Tier-1 ctest (and `-L sync`) — the suite-green claim must NOT come from a name-scoped `-R` subset, because `engine.hpp`/`session.hpp` are awaitable-corpus headers and 015 adds includes into them (SC-008; `[[feedback_awaitable_header_mutex_include_edge]]`); the tree must be clean first — ctest #132 asserts `git status --porcelain` is empty (`[[feedback_codegen_build_graph_cleanliness_gate]]`).
- [ ] T026 [P] Re-sync the CodeGraph index from the submodule (`codegraph sync`) after the code-changing phases so search/impact stay accurate (project CLAUDE.md).

---

## Dependencies & Execution Order

### Phase dependencies

- **Setup (P1)** → no deps.
- **Foundational (P2)** → after Setup. `T003 ‖ T004` → `T005` (engine.hpp) → `T006` (engine.cpp) → `T007` (harness). **BLOCKS all stories.**
- **US1 (P3)** → after Foundational. **MVP.** `T011 → T012 → T013` (T012's accept loop spawns the US2 read-pump — stubbed by T006 until US2 lands).
- **US2 (P4)** → after Foundational. `T015 → T016`. Independent of US1 (testable on the initiator path via the shipped 014 `install_reconnected_transport`); US1's accept loop (T012) calls the read-pump (T015) for steady-state, so full acceptor steady-state needs both.
- **US3 (P5)** → after Foundational. `T017 → T018`. Lifecycle guarantees largely realized in T006; this story proves SC-004/005 and hardens any gap.
- **US4 (P6)** → after US1 (the seam removal at `:1048` follows the live arm landing at `:1048` in T013; symmetric removal at `:1913` is one pass). `T020 → T021`.
- **Polish (P7)** → after all desired stories.

> **Honest cross-story coupling**: unlike a clean fan-out, 015's stories share `engine.cpp` and the `Session` gate. The engine substrate (T005/T006) is the genuine dependency root — US1's accept loop, US2's read-pump/connect loop, and US3's lifecycle tests all build on it. US4 (seam removal) depends on US1's live arm so removal does not strand a gate site. US2 is the only story testable in isolation of the others (initiator path). This is documented rather than papered over per the spec's all-P1 framing.

### Within a story

- Tests written and FAILING before implementation (`[const §VII.3]`).
- US1: the `Session` attach primitive (T011) before the accept loop that calls it (T012), before the gate arm it feeds (T013).
- US2: the read-pump body (T015) before the connect loop that spawns it (T016).
- US4: the field+arm removal (T020) before the test re-pointing that proves zero dependence (T021).

### Parallel opportunities

- Setup: `T002 ‖ T001`.
- Foundational: `T003 ‖ T004` (different files: `error.hpp` vs `engine.hpp`); then `T005 → T006 → T007` (T005/T006 share `engine.cpp`/`engine.hpp` — sequential).
- US1 tests: `T008 ‖ T009 ‖ T010` (different files).
- US2 test: `T014` standalone.
- US3 test: `T017` standalone.
- US4 test: `T019` standalone.
- Polish: `T024 ‖ T026`.

> Note: T011/T013 both edit `src/session/session.cpp`, and T012/T015/T016/T018 all edit `src/session/engine.cpp` — these are **sequential within their file**, not `[P]`, even across stories.

---

## Parallel Example: User Story 1 tests

```bash
# Write the three US1 test files together (different files, no deps):
Task: "tests/session/engine_acceptor_test.cpp — accept → handshake → resolve → attach → admit on-list live identity"
Task: "tests/session/engine_acceptor_failclosed_test.cpp — off-list + absent/delayed identity → fail-CLOSED (happens-before regression)"
Task: "tests/session/engine_firstframe_test.cpp — bounded first-frame window: stall/silent/partial/over-budget → close + reclaim"
```

## Parallel Example: Foundational independent files

```bash
Task: "append error::session_unknown_acceptor_session = 121 in include/fixpp/core/error.hpp"
Task: "create SessionId value type + std::hash + from_config + reversed_from_logon in include/fixpp/session/engine.hpp (+ unit test)"
```

---

## Implementation Strategy

### MVP first (User Story 1 only)

1. Phase 1 Setup → 2. Phase 2 Foundational (CRITICAL — error slot + `SessionId` + `Engine` skeleton + harness) → 3. Phase 3 US1 → **STOP & VALIDATE** the live acceptor admit-on-list / fail-CLOSED-off-list path over loopback-TLS → MVP (the acceptor half of T-041 operable in production).

### Incremental delivery

US1 (live acceptor authorize) → US2 (continuous read-pump for both roles) → US3 (multi-session lifecycle + registry) → US4 (seam removal + T-041 → done). Each adds value without breaking the prior; US4 closes the catalogue row only once US1's live arm is in place.

### Build/verify discipline (this box)

- Resource cap per `[[feedback_build_resource_cap_oom]]`: clang/build parallelism max `-j2`; the sanitizer presets + the 6-preset verify matrix run strictly ONE AT A TIME, sequentially.
- The headline correctness target is **engine `stop()` teardown under the full ASan/UBSan/TSan matrix** — clean total-cancellation of accept loops, connect loops, read-pumps, and in-flight handshakes with no UAF/leak (SC-005/SC-008; the 014 Gate-B UAF lesson + `[[feedback_gateb_full_sanitizer_before_signoff]]`). Every `co_spawn`ed loop/pump MUST `reset_cancellation_state(enable_total_cancellation())` or `stop()` hangs silently — the first thing to check on a stop-hang (`[[feedback_asio_cospawn_total_cancellation_default]]`).
- `/simplify` (step 11) → `/speckit-verify` (step 12, unfiltered ctest per SC-008) → Gate B (step 14) follow this `/tasks` step per `.specify/pipeline.md` (step 6 `/speckit-analyze` and step 9 `/speckit-checklist-audit` — MANDATORY, blocks `/speckit-implement` — come first).

---

## Notes

- `[P]` = different files, no dependency on an incomplete task.
- `[Story]` labels apply to US phases only (Setup/Foundational/Polish carry none).
- 015 adds the engine **inside the existing `session/` module** — NOT a new module; only an `architecture.md §4.4` public-type entry, no `check_layers.py` ALLOWED-map change (R1). Run `tools/check_layers.py` after `engine.hpp` lands.
- The dynamic-session-provider hook is **deferred** (R2) — static reversed-CompID matching alone closes T-041; do NOT build a speculative pluggable interface this slice (`[[karpathy-guidelines]]` Simplicity First).
- Commit after each task or logical group (the optional `before_tasks`/`after_tasks` git-commit hooks are available via `/speckit-git-commit`).
