# Phase 0 — Research: 015-runtime-engine

**Feature**: Public Initiator/Acceptor Runtime Engine & Full T-041 Closure
**Date**: 2026-05-30 | **Plan**: [`plan.md`](./plan.md)

All "NEEDS CLARIFICATION" items from the plan's Technical Context are resolved here. Every load-bearing claim is grounded against the shipped source on this branch (`015-runtime-engine`) (file:line verified 2026-05-30) and the QFC/QFJ reference engines (`reference-engines/`, gitignored).

> **Gate A re-verification note (round 1 re-derive; round 2 anchors confirmed branch-local).** This research.md was RE-DERIVED from a fresh first-hand source read after round 1 found the original Phase-0/1 baseline partly fabricated (Root cause #2). At round 2 the orchestrator verified **first-hand on THIS branch** that `src/session/session.cpp` is the FULL file (not a stub) and every cited line is **branch-local accurate**: acceptor gate seam consumer `session.cpp:1048` (`if (cfg_.logon_peer_identity_override.has_value())`, NotConnected->LogonReceived), initiator seam consumer `session.cpp:1913` (inside `case fsm_state::LogonSent`), 014 initiator live arm ~`session.cpp:1864`, `install_reconnected_transport` body `session.cpp:206`, seam field `session_config.hpp:229`; `session.hpp`/`framer.hpp`/`listener.hpp`/`error.hpp`/`engine_config.hpp` are likewise the real shipped files. All `file:line` anchors in this bundle are branch-local-verified.

---

## Shipped-reality baseline (verified branch-local on `015-runtime-engine`)

| Claim | Shipped reality | `file:line` |
|---|---|---|
| Accept primitive | `Listener` abstract = **exactly 1 pure-virtual**: `async_accept() -> awaitable<expected_t<unique_ptr<Transport>>>`. **TLS is NOT issued inside** — the returned `Transport` is in the *connected* (TCP-only) state and "the FSM issues `async_handshake` (TLS) immediately" (header comment). `async_accept` returns **no `handshake_result` and no `peer_id`**. `cancellation_type::total -> transport_accept_cancelled` | `include/fixpp/transport/listener.hpp:45-53` |
| Read-pump target | `Session::on_inbound_frame(std::span<const std::byte>) noexcept -> awaitable<expected_t<void>>` — session-strand; store(inbound) before fromAdmin/fromApp | `include/fixpp/session/session.hpp:230` |
| Session construction | **PUBLIC, synchronous ctor** `Session(const fixpp::core::EngineConfig&, const SessionConfig&)` (NOT noexcept) + awaitable `open() noexcept -> awaitable<expected_t<void>>`. **There is NO `make_session` factory, NO private ctor, and NO `Application&` parameter** anywhere. The prior "`static make_session(SessionConfig, Application&, executor)`" row was FABRICATED (Gate A Codex-3 / New-2) and is deleted. | ctor `session.hpp:95`; `open()` `session.hpp:114` |
| Identity fields | `SessionConfig.{sender_comp_id, target_comp_id, begin_string}` (`std::string`); **NO `session_qualifier` field today** | `include/fixpp/session/session_config.hpp` |
| Outbound send wiring | `transport_send_` is `std::function<void(span<const std::byte>)>`, **captured once from `cfg_.transport_send` at `open()`** and immutable thereafter | `session.hpp:530-534` |
| Initiator live-arm (014) | `live_peer_id_.has_value() && is_mtls` -> authorize w/ real handshake peer_id (arm 1-live, ahead of seam), in the **`case fsm_state::LogonSent`** initiator ack path | `session.cpp:1864` |
| Acceptor gate (still seam-only) — **ONE site** | three-way guard on the **acceptor `NotConnected -> LogonReceived`** branch: (1) `logon_peer_identity_override` -> authorize; (2) `else if (is_mtls)` -> fail-CLOSED; (3) non-mTLS -> skip. **NO live-identity arm.** | `session.cpp:1048` — the single acceptor gate |
| `:1913` is NOT a second acceptor gate | `session.cpp:1913` is the **initiator** seam arm inside the same `case fsm_state::LogonSent` as `:1864` (the case opens at `session.cpp:1707`). It is the initiator's leftover override arm, not an acceptor site (Gate A New-7). | `session.cpp:1707/1913` |
| Live identity plumbing (014) — **initiator-only primitive** | `live_peer_id_ : std::optional<peer_identity>` member; `install_reconnected_transport(unique_ptr<Transport>, handshake_result) noexcept` — **two args** — stores the transport + `live_peer_id_` AND **re-enters `LogonSent`** (forces the initiator path). NOT a neutral attach. | `session.hpp:552`, `:475-477` (decl + doc `:455-464`); body `session.cpp:208-223` |
| Seam field | `std::optional<fixpp::tls::peer_identity> logon_peer_identity_override{}` — test seam to remove | `session_config.hpp:229` |
| Seam-removal guard | the only `static_assert` is `static_assert(std::is_copy_constructible_v<SessionConfig>, ...)` (the W-5 by-value-copy guard). **There is NO field-count `static_assert`** — the prior `:255` field-count claim was invented (Gate A Codex-6). | `session_config.hpp:260` |
| Framer surface | `expected_t<span<frame_view>> Framer::feed(span<const std::byte> incoming, pmr_carry_buffer& carry, span<frame_view> out) noexcept` + `pending_bytes()`. **There is NO `next()` and NO single-argument `feed`.** Over-capacity append -> `pmr_carry_buffer::append` returns false -> Framer reports `wire_frame_too_large`. | `include/fixpp/wire/framer.hpp:131-136`; `pmr_carry_buffer` `:28-68`; `wire_frame_too_large` `error.hpp:60` |
| `EngineConfig` | value type; carries `executor`, `clock`, `dictionaries`, default resources, `default_transport_factory`, etc. **Carries NO `Application`.** | `include/fixpp/core/engine_config.hpp:106-148` |
| Layer map | `check_layers.py` ALLOWED: **no `runtime` key**; `session` edges = `{core, dictionary, wire, transport, log, otel}` | `tools/check_layers.py:29` |
| Error-slot boundary | `session_compid_unauthorized = 117`, `session_testreqid_mismatch = 118`, `session_invalid_argument = 119`, max = `session_seqnum_too_high = 120` (014); slot 70 permanent hole. **No reusable `unknown_session`/`no_such_session`/`unknown_acceptor` code exists** (grep-clean) -> 121 is the next free slot. | `include/fixpp/core/error.hpp:616-665` |

---

## R1 — Engine module placement & public-type shape

**Decision**: The runtime engine is a **new public concrete type in the existing `session/` module** — `include/fixpp/session/engine.hpp` + `src/session/engine.cpp`. NOT a new `runtime/` module.

**Rationale**:
- `check_layers.py:29` already grants `session -> {core, dictionary, wire, transport, log, otel}` — every edge the engine needs (it composes `transport::Listener`/`TransportFactory` + `session::Session`/`ReconnectFsm`). No `ALLOWED`-map change, no architecture.md §2.2 amendment.
- architecture.md §2.2 row 4 already scopes `session/` as owning the `Session`, FSM, and session lifecycle; the multi-session engine is the orchestration concern of that same module.
- A new `runtime/` module would force a `check_layers.py` ALLOWED key **and** an arch §2.2 amendment — a Gate-A-heavy change per `[[feedback_gate_b_check_layers_post_fixer]]` (007 RC#1 burned a PR hotfix on exactly this — wrong-module placement).
- The 005 `Session` already exposes the surface the engine drives: the public ctor (`session.hpp:95`), `open()` (`:114`), and `on_inbound_frame` (`:230`). The engine is the long-anticipated multi-session owner.

**Public-type shape** (minimal, clarify-Q3-driven):
```
class Engine {
  Engine(asio::any_io_executor exec, fixpp::core::EngineConfig cfg);  // injected executor (Q3)
  expected_t<void> register_session(SessionConfig cfg);  // FR-002; dup SessionId -> error; NO Application&
  void start();                                          // non-blocking; launch loops (Q3)
  asio::awaitable<void> stop();                          // idempotent; total-cancel + join-before-clear
  Session* lookup(SessionId const&) const;              // registry addressing (may be null pre-connect)
};
```
**Alternatives considered**: (a) new `runtime/` module — rejected (layer+arch churn, above). (b) free functions over a bag of sessions — rejected (no lifecycle owner for start/stop/teardown; FR-011 needs a single cancellation root). (c) put it in `capi/`/`service/` — rejected (those are Phase-5+ and downstream of the AGPL boundary; the engine is pre-Phase-5 per scope).

**Gate-A obligation**: add one architecture.md §4.4 public-type entry for `fixpp::session::Engine` (no layer-graph change). Per `[[feedback_gate_b_check_layers_post_fixer]]`, run `tools/check_layers.py` after the new header lands.

---

## R2 — Optional dynamic-session-provider: build or defer?

**Decision**: **DEFER the dynamic provider** to a later feature. 015 ships **static pre-configured matching only** (reversed-CompID SessionId match against the registry). The clarify-Q1 "optional dynamic hook" is recorded as an explicit, named future extension point — NOT built in this slice.

**Rationale**:
- Static matching alone fully closes **T-041** (the live acceptor identity -> `authorize()` fail-CLOSED binding is proven on the static path); the dynamic provider adds no T-041 coverage.
- Per `[[karpathy-guidelines]]` Simplicity First: a pluggable `AcceptorSessionProvider` interface with no in-tree second impl is speculative configurability. QFJ ships both, but QFJ is a mature multi-release engine; fixpp's pre-1.0 MVP does not need accept-any-then-template-mint yet.
- Building it now would add a new `[const §XIV.2]` pluggable interface (needs a one-paragraph Gate-A justification) for a capability with zero current consumer — the exact "flexibility that wasn't requested" anti-pattern.
- The static path is also the **safer** default: an unmatched Logon is rejected with no session created (no accept-any surface), tightening the security posture for the T-041 review.

**Spec reconciliation**: spec FR-005 / Clarifications Q1 say the engine **MAY** support the dynamic hook ("MAY", not "MUST"). Deferring is spec-conformant — and the spec is tightened at Gate A round 1 to make routing explicitly static (unmatched Logon ALWAYS rejected, no fail-open).

**Alternatives considered**: (a) build the full QFJ-style `DynamicAcceptorSessionProvider` now — rejected (speculative, +1 interface, no consumer). (b) build a stub interface with no impl — rejected (a pure-virtual with no second impl is worse than a named extension point in prose). **Re-open if**: a concrete multi-tenant/accept-any requirement lands, or Gate A judges static-only insufficient for the carve-out.

---

## R3 — Accept loop & continuous inbound read-pump pattern

> ## ⚠️ SUPERSEDED IN PART — amended 2026-08-29, do not re-seed a design from the Decision below
>
> Feature **023 (T010)** changed the executor and the coroutine shape. **Two clauses of the R3
> Decision are FALSE against the shipped engine**, and this note deletes them rather than
> restating a corrected design — a restated design rots on the next threading change with nothing
> to notice it, which is how this document reached today's state.
>
> | Clause as written | Shipped |
> |---|---|
> | *"`co_spawn` an accept loop **on the engine executor**"* | `co_spawn(*entry.session_strand, run_accept_loop, …)` — the **per-session strand** (itself `make_strand(exec_)`, so still over the engine executor at the *pool* level; what changed is the **serialisation domain**) |
> | *"`co_spawn` a **read-pump coroutine** on that session's strand"* | the pump is **`co_await`ed inline** inside the accept loop, not spawned as a separate coroutine — the whole role already runs on that strand |
>
> *"Repeatedly `co_await listener.async_accept()`"* is **literally** true (`while (!engine.stopped())`)
> but materially misleading: the loop only re-accepts **after** the previous connection's pump
> returns. It is serialized, not concurrent.
>
> **Derive it from the spawn site**, which cannot go stale silently because it *is* the thing
> described:
> ```bash
> grep -n "co_spawn" src/session/engine.cpp
> grep -n "session_strand.emplace" src/session/engine.cpp
> ```
>
> **What still holds:** the whole **Rationale** block below — the acceptor loop owns the TLS
> handshake because `Listener::async_accept()` returns a TCP-connected transport with no
> `handshake_result`/`peer_id`; one pump per session in-order on one strand; and the
> **Cancellation** paragraph, which is if anything more load-bearing now. Those are unaffected.
> The **Alternatives considered** are historical and do not rot.
>
> Governing record: `specs/023-engine-session-strand/research.md` (D0–D6). Same supersession is
> noted in `.specify/2j-controlplane.md` §6.5 and `.specify/2d-threading.md`.

**Decision** *(as written at 015 sign-off — see the note above for the two superseded clauses)*: Per registered acceptor session, `co_spawn` an **accept loop** that repeatedly `co_await listener.async_accept()`; on each accepted `Transport` (TCP-connected, **not yet TLS-handshaken** — see baseline), the loop must itself **drive the TLS handshake** (obtain the `TlsTransport`, `co_await async_handshake(...)`, harvest `handshake_result` / `peer_id`), then resolve the target session by reversed-CompID (R4), attach the transport (R7 acceptor attach primitive), and run a **read-pump** for that session. The read-pump loops `transport.async_read_some(...)` -> `Framer::feed(...)` -> `co_await session.on_inbound_frame(frame)` until EOF/read-error/cancellation. The **initiator** side reuses 014's `ReconnectFsm::drive_reconnect_attempt` for connect+handshake (which already produces the `handshake_result`), then spawns the same read-pump.

**Rationale**:
- `Listener::async_accept()` (`listener.hpp:45-53`) returns a TCP-connected `Transport` with **TLS NOT yet issued** ("the FSM issues `async_handshake` (TLS) immediately") and **no `handshake_result`/`peer_id`** — so the acceptor loop is responsible for running the handshake and harvesting the identity, symmetric to the initiator's `drive_reconnect_attempt`. (Gate A Codex-1 / New-1: this is the single source of `handshake_result.peer_id` on the acceptor path; T-041 closure depends on it.)
- `on_inbound_frame` (`session.hpp:230`) is explicitly "the engine's transport feeds it after parse/frame-validate" and is documented session-strand — the read-pump is its only production driver (014 fed it only via tests).
- One read-pump coroutine per session, pinned to that session's strand, gives FR-004's in-order, on-strand, exactly-once delivery for free (single consumer, no cross-session sharing).

**Cancellation** (critical — `[const §XI.2]`, `[[feedback_asio_cospawn_total_cancellation_default]]`): every `co_spawn` (accept loop, read-pump, connect loop) defaults to **terminal-only** cancellation; each MUST `co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation())` or `stop()`'s total-cancel will silently hang. This is the headline teardown correctness target (FR-011/SC-005) and the first thing to check on any "stop() hangs" symptom.

**Framing note**: `Transport::async_read_some` yields bytes, not frames; the read-pump uses the shipped `wire::Framer::feed(incoming, carry, out)` surface (R8) to delimit FIX frames before calling `on_inbound_frame`. Backpressure is natural — the pump `co_await`s `on_inbound_frame` synchronously and does not read the next chunk until the session has consumed the current frame, so there is no unbounded inbound queue.

**Alternatives considered**: (a) one shared read-pump multiplexing all sessions — rejected (breaks per-session strand isolation; serializes unrelated sessions). (b) callback-style `async_read_some` with completion handlers — rejected (the tree is coroutine-native; `[const §XI.1]`).

---

## R4 — Acceptor connection -> session resolution (reversed-CompID)

**Decision**: On an accepted connection, read the inbound **Logon**, extract its SenderCompID(49)/TargetCompID(56), and look up the registry by the **reversed** tuple: `SessionId{begin_string, sender = inbound.TargetCompID, target = inbound.SenderCompID}`. Match -> attach + feed bytes to that session. No match (static-only) -> reject at the connection level, close the transport, create no session.

**Rationale** (reference-engine grounded):
- **QFC** `Acceptor::getSession(msg, responder)` builds `SessionID(beginString, SenderCompID(clTargetCompID), TargetCompID(clSenderCompID))` — reversed — and looks it up in `std::map<SessionID, Session*>` (`quickfix-cpp/src/C++/Acceptor.cpp:101-129`).
- **QFJ** `AcceptorIoHandler` does `MessageUtils.getReverseSessionID(message)` then `sessionProvider.getSession(sessionID)` (`quickfixj-core/.../mina/acceptor/AcceptorIoHandler.java:70-71`).
- Both engines key the session table by the FIX **SessionID tuple** — matching clarify-Q2.

**The first-frame problem**: resolution needs the inbound Logon's CompIDs, so the engine must read the **first** frame before it knows which `Session` owns the connection. **Decision (DR-7, DIRECT delivery — pinned round 2)**: a **bounded** pre-session read step in the accept loop (R9: byte cap + handshake/Logon deadline) reads exactly the first frame, resolves the session, attaches the live transport (R7), then delivers that **already-read first Logon DIRECTLY to `on_inbound_frame`** (the R5/E-4 acceptor live-identity arm authorizes against the already-set `live_peer_id_`); the read-pump then handles all **subsequent** bytes. There is exactly one handoff model — the first frame is NOT re-fed into the read-pump's framer carry (no carry-transfer). This is the QFC/QFJ "lookupSession(message)" shape. Detailed in data-model E-2.

**Alternatives considered**: (a) one listener+session statically bound 1:1 (no Logon peek) — rejected (a single listen port serves multiple acceptor sessions in both reference engines; CompID demux is required). (b) resolve by listen-port->session config — rejected (doesn't match the multi-session-per-port FIX convention; QFC/QFJ both demux by CompID).

---

## R5 — Error slots: does the engine need new `error::` variants?

**Decision (pinned first-hand, Gate A round 1; slot value corrected round 2)**: **Reuse existing codes where they fit; append exactly ONE genuinely-new slot.** Verified against `error.hpp`: `session_compid_unauthorized = 117` (`error.hpp:616`), `session_testreqid_mismatch = 118` (`error.hpp:622`), `session_invalid_argument = 119` (`error.hpp:627`), max = `session_seqnum_too_high = 120` (`error.hpp:653`); a grep for `unknown_session`/`no_such_session`/`unknown_acceptor`/`session_not_found` is clean — **no reusable "no configured session" code exists**. So:
1. **Unmatched acceptor Logon** (no registry entry for the reversed CompIDs) -> **append `session_unknown_acceptor_session = 121`** (the next free slot; not redundant — verified no reusable code, Gate A New-6). Surfaces at the connection level only (close + log), never delivered to a Session event — at rejection time no Session exists (data-model "Error model delta"; realized-behavior C7).
2. **Duplicate registration** (FR-002, two configs -> same SessionId) — a **programmatic API error at register time**; **reuse `session_invalid_argument = 119`**, do not append.
3. **Acceptor authorization failure** — **reuse `session_compid_unauthorized = 117`** unchanged (FR-008 inherits 013/014 shapes; no new code).

**Rationale**: Minimize ABI surface. Only #1 is genuinely new; #2/#3 reuse verified slots. Slot 70 stays a permanent hole; never renumber.

**Alternatives considered**: minting fresh slots for all three — rejected (ABI bloat; #2/#3 have clean existing semantics). Coalescing #1 into `session_compid_unauthorized` — rejected (semantically distinct: "no such session" != "session exists but peer unauthorized"; the security-review axis wants them separable).

---

## R6 — SessionId key type (registry) — new type or reuse?

**Decision**: Introduce a small value type `fixpp::session::SessionId { begin_string, sender_comp_id, target_comp_id }` — a **regular value type** (copyable, equality-comparable, hashable) used as the registry key. **No `qualifier` field** — drop it. **There is no existing `SessionId` type today** (`SessionConfig` carries the three fields loose on `main`; grep confirms no `SessionId`/`session_id` type).

**Rationale**:
- clarify-Q2 fixed the key as the FIX SessionID tuple; a named value type (vs keying on raw `SessionConfig`) gives clean equality for FR-002 duplicate rejection and O(1)/O(log n) lookup, and matches QFC/QFJ `SessionID` precedent.
- The previously-proposed reserved-but-empty `qualifier` field is **dropped** (Gate A New-5): a pre-1.0 value type has no ABI-stability obligation, and an always-empty field "for forward-compat" is exactly the speculative-configurability `[[karpathy-guidelines]]` Simplicity First forbids (the same rationale the bundle uses to defer the dynamic provider in R2). Re-add a `qualifier` only when a real `SessionConfig` qualifier field lands.
- Lives in `session/` (R1); no layer impact.

**Alternatives considered**: (a) key the `std::map`/`unordered_map` on `std::tuple<string,string,string>` — rejected (no semantic name; FR-002/lookup read worse). (b) reuse `SessionConfig` as the key — rejected (heavy value type; identity != whole config; two different configs can share an identity, which is exactly the FR-002 duplicate case). **Gate-A obligation**: this is a new (small) public type — add to architecture.md §4.4 alongside `Engine` (R1).

---

## R7 — Acceptor first-attach primitive + the live-identity-before-gate invariant (Gate A round 1 — Root cause #1)

**Decision**: Do **NOT** reuse `install_reconnected_transport` on the acceptor path. That primitive (`session.hpp:475-477`, body `session.cpp:208-223` on `main`) is an **initiator** re-install: it stores the transport + `live_peer_id_` AND **unconditionally re-enters `LogonSent`** (forcing the initiator ack path). Driving an acceptor through it would put the session in the initiator's wait-for-Logon-ack state and then feed it an *inbound* Logon (not an ack) — structurally wrong (Gate A Codex-2). The acceptor needs a **distinct attach primitive** (named here `attach_accepted_transport(...)` as the design intent; exact spelling locked at `/speckit-implement` against `main`'s `Session`) that:
1. takes ownership of the live accepted transport;
2. sets `live_peer_id_` from the harvested `handshake_result.peer_id` (R3 — the acceptor loop ran the handshake);
3. rebinds outbound so `transport_send_` writes to the live accepted transport (see the ordering hazard below). (At Gate A, `install_reconnected_transport` did NOT touch `transport_send_` — 014 behavior. **NOTE: 015 later AMENDS `install_reconnected_transport` to ALSO rebind `transport_send_` for the initiator path** — Clarifications 2026-05-31 / data-model E-1a. The acceptor still needs its *own* `attach_accepted_transport` because install re-enters `LogonSent` — the FSM reason, NOT the send-slot.);
4. does **NOT** transition the FSM — it leaves the session on the acceptor `NotConnected -> LogonReceived` path so the inbound Logon is processed by the acceptor gate (`session.cpp:1048`).

**Outbound-wiring ordering hazard (Gate A Codex-4)**: `transport_send_` is captured once at `open()` from `cfg_.transport_send` and is immutable thereafter, but for an acceptor the live transport does not exist until a peer connects (after register/open). The design therefore needs a **stable indirection**: either (a) construct/`open()` the acceptor `Session` lazily at accept time (after the transport exists), or (b) capture a rebindable forwarding slot at `open()` whose target the attach primitive updates. **Decision: (b)** — capture an engine-owned rebindable send-slot as `cfg_.transport_send` so `lookup()` can return a registered acceptor `Session` before any connection (SC-004), and `attach_accepted_transport` repoints the slot at attach. (a) is rejected because SC-004 requires the session be retrievable by key after `start()` and before any connection.

**The happens-before invariant (Gate A New-1) — write it down + test it**: `live_peer_id_` MUST be set on the session strand **strictly-happens-before** the first `on_inbound_frame` that can reach the acceptor Logon gate. Sequence on the session strand: `attach_accepted_transport` (sets `live_peer_id_`) -> deliver first Logon to `on_inbound_frame` -> acceptor gate at `:1048` authorizes against `live_peer_id_`. If the identity is absent when the gate runs, the guard falls to `else if (is_mtls) -> fail-CLOSED` (safe), but the design pins the invariant so an implementer chasing "on-list peers wrongly rejected" does NOT re-open the hole by setting `live_peer_id_` unconditionally or routing through the non-mTLS skip. **Regression test**: acceptor mTLS, `live_peer_id_` deliberately delayed/absent -> assert fail-CLOSED, never admit.

**Gate-site geometry (Gate A New-7)**: the live arm is added at the acceptor gate `session.cpp:1048` **only** (the single `NotConnected -> LogonReceived` acceptor site). `session.cpp:1913` is the **initiator** seam arm inside `case fsm_state::LogonSent` (adjacent to 014's live arm at `:1864`), NOT a second acceptor gate. The symmetric-fix obligation is therefore: **add** the live arm at `:1048` (one site); **remove** the seam arm at **both** `:1048` (acceptor) and `:1913` (initiator) once the seam field is deleted (E-6).

---

## R8 — Read-pump against the real Framer surface (Gate A round 1 — Root cause #2)

**Decision**: The read-pump uses the shipped `wire::Framer` surface, NOT a fictional `feed()/next()`. The real API (`framer.hpp:131-136`) is `expected_t<span<frame_view>> Framer::feed(span<const std::byte> incoming, pmr_carry_buffer& carry, span<frame_view> out) noexcept` + `pending_bytes()`. The pump owns one `Framer`, one session-lifetime `pmr_carry_buffer` (capacity = a named constant `kReadPumpCarryCapacity = 64 KiB`; `SessionConfig` has **no** `max_frame_bytes` field — verified at T015; arena from `framer_carry_arena ?: new_delete_resource()`), and a bounded `frame_view out` buffer; each `async_read_some` chunk is passed to `feed(...)`, which returns the span of complete frames and carries the trailing partial in `carry`. A frame exceeding the carry capacity is reported by the Framer as `wire_frame_too_large` (`error.hpp:60`) — the pump closes the connection (no silent truncation). **Rationale**: the contracts must not pre-commit to an API that does not exist; the real append/`feed`/`consume_front` model is what the engine drives (the earlier "do not assume method names" caveat is now resolved against shipped source).

---

## R9 — Pre-session lifecycle + teardown ownership (Gate A round 1 — Root cause #4)

**Decision**: Introduce an engine-level **accept-scope cancellation domain**, distinct from each per-session domain. For the window between `async_accept` returning a transport and the session being attached, it owns (a) the accepted transport, (b) the TLS handshake, (c) the **bounded** first-frame read — a maximum byte budget AND a handshake/Logon deadline (FR-014) — and (d) construction/`open()` if done lazily. On handshake failure, first-frame timeout/over-budget, construction failure, or `stop()` arriving mid-accept, this domain closes the transport and reclaims the slot. A peer that dribbles pre-Logon bytes is closed at the deadline; other peers are unaffected (Gate A Codex-10).

**Teardown ownership (Gate A Codex-9 + New-3 + New-4)**:
- **Destructor**: `~Engine()` is a strict `assert(stopped())` precondition — **no synchronous best-effort path** (a synchronous destructor cannot run the caller-driven event loop to drain in-flight coroutines that hold raw `Session*`, which is a UAF — the 014 Gate-B burn). Destruction requires a prior `co_await stop()`.
- **Join-before-clear**: `stop()` cancels (total) AND **joins** every accept loop, connect loop, and per-session read-pump **before** the registry (which owns the `Session` via `unique_ptr`) is cleared, so no coroutine on a session strand can hold a dangling `Session*` (Gate A New-4 — the registry strand E-5 protects the *map*, not the *pointee lifetime across strands*).
- **`open()` sequencing**: `open()` is awaitable, so it is `co_await`ed as the **first step inside each spawned loop**, not in the synchronous `void start()`. Consequence: a registered session may be **constructed-but-not-yet-open** when `start()` returns; `lookup()` may return such a session (or null for a registered acceptor with no peer) — pin this in the contract (Gate A New-3).

---

## R10 — Read-pump inbound message scope (Gate A round 1 — Root cause #5)

**Decision**: 015 is scoped to **admin / session-layer** inbound message flow (Logon, Logout, Heartbeat, TestRequest, ResendRequest, SequenceReset) — the messages `Session`'s own FSM consumes internally. With the `Application` callback ecosystem out of scope (FR-013), there is **no user sink for inbound *application* messages** in this slice. FR-004's "deliver every inbound frame to `on_inbound_frame`" stands (the read-pump delivers every frame; `Session` stores it and runs fromAdmin for admin types); but US2's incidental "application messages" wording is corrected — app-message delivery to a user is a Phase-5 concern, not 015. (Gate A New-2.)

---

## Consolidated decisions

| # | Decision | Drives |
|---|----------|--------|
| R1 | Engine = new concrete type in **existing `session/`** module (`engine.{hpp,cpp}`); arch §4.4 entry only | FR-001/003/011; structure |
| R2 | **Defer** the dynamic-session-provider; static matching only this slice | FR-005; Complexity Tracking (none) |
| R3 | Per-session accept loop (**runs the TLS handshake + harvests peer_id**) + per-session read-pump; **total-cancellation reset mandatory** | FR-003/004/006/011; SC-001/003/005 |
| R4 | Acceptor resolves by **reversed-CompID SessionId** (QFC/QFJ); bounded first-frame Logon read | FR-005/006; SC-001 |
| R5 | Reuse 117/119; append **exactly 1** new slot `session_unknown_acceptor_session = 121` (no reusable code, pinned first-hand) | FR-002; `[const §X.4]` |
| R6 | New small `SessionId` value type (begin_string+sender+target; **no qualifier**) | FR-002; registry; arch §4.4 |
| R7 | **Distinct acceptor attach primitive** (no `install_reconnected_transport`, no LogonSent); rebindable outbound; live-arm at `:1048` only; happens-before invariant + fail-CLOSED regression | FR-005/006/007; SC-001/002; T-041 |
| R8 | Read-pump uses the real `Framer::feed(incoming, carry, out)` surface; over-capacity -> `wire_frame_too_large`; natural backpressure | FR-004; SC-003 |
| R9 | Accept-scope cancellation domain + bounded first-frame read (FR-014); strict-`stopped()` destructor; join-before-clear teardown; `open()` awaited inside loops | FR-011/014; SC-005/008/011 |
| R10 | 015 scope = admin/session-layer inbound flow; no app-message user sink (Application out of scope) | FR-004/013; US2 |

**No NEEDS CLARIFICATION remain.** This research.md was re-derived from a fresh first-hand source read at Gate A round 1; the fabricated `make_session`/`Application&` baseline, the `feed()/next()` Framer surface, the phantom field-count `static_assert`, and the inverted `:1913` gate geometry are all corrected. Ready for re-review and Phase 1 (data-model.md, contracts/, quickstart.md).
