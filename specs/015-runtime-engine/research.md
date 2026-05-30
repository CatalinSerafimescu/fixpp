# Phase 0 — Research: 015-runtime-engine

**Feature**: Public Initiator/Acceptor Runtime Engine & Full T-041 Closure
**Date**: 2026-05-30 | **Plan**: [`plan.md`](./plan.md)

All "NEEDS CLARIFICATION" items from the plan's Technical Context are resolved here. Every load-bearing claim is grounded against post-014 `main` shipped source (file:line verified 2026-05-30) and the QFC/QFJ reference engines (`reference-engines/`, gitignored).

---

## Shipped-reality baseline (re-verified against post-PR-#87 `main`)

| Claim | Shipped reality | `file:line` |
|---|---|---|
| Accept primitive | `Listener` abstract = **exactly 1 pure-virtual**: `async_accept() -> awaitable<expected_t<unique_ptr<Transport>>>`; TLS handshake issued inside; `cancellation_type::total → transport_accept_cancelled` | `include/fixpp/transport/listener.hpp:52-53` |
| Read-pump target | `Session::on_inbound_frame(std::span<const std::byte>) noexcept -> awaitable<expected_t<void>>` — session-strand; store(inbound) before fromAdmin/fromApp | `include/fixpp/session/session.hpp:230` |
| Session factory | `static Session::make_session(SessionConfig, Application&, asio::any_io_executor) -> awaitable<unique_ptr<Session>>`; ctor private; per-session strand derived from the injected executor; engine wires `transport_send_` + drives `on_inbound_frame` | `include/fixpp/session/session.hpp:141` |
| Identity fields | `SessionConfig.{sender_comp_id, target_comp_id, begin_string}` (`std::string`); **NO `session_qualifier` field today** | `include/fixpp/session/session_config.hpp:151-153` |
| Initiator live-arm (014) | `live_peer_id_.has_value() && is_mtls` → authorize w/ real handshake peer_id (arm 1-live, ahead of seam) | `src/session/session.cpp:1864` |
| Acceptor gate (still seam-only) | three-way guard: (1) `logon_peer_identity_override` → authorize; (2) `else if (is_mtls)` → fail-CLOSED; (3) non-mTLS → skip. **NO live-identity arm** | `src/session/session.cpp:1048` (+ `:1913`) |
| Live identity plumbing (014) | `live_peer_id_ : std::optional<peer_identity>` member; `install_reconnected_transport(handshake_result) noexcept` stores it | `include/fixpp/session/session.hpp:552`, `:477`; `src/session/session.cpp:208-213` |
| Seam field | `std::optional<fixpp::tls::peer_identity> logon_peer_identity_override{}` — test seam to remove | `include/fixpp/session/session_config.hpp` (the `:1048`/`:1913` consumers) |
| Layer map | `check_layers.py` ALLOWED: **no `runtime` key**; `session` edges = `{core, dictionary, wire, transport, log, otel}` | `tools/check_layers.py:29` |
| Error-slot boundary | max = `session_seqnum_too_high = 120` (014); slot 70 permanent hole | `include/fixpp/core/error.hpp` (014 appended 120) |

---

## R1 — Engine module placement & public-type shape

**Decision**: The runtime engine is a **new public concrete type in the existing `session/` module** — `include/fixpp/session/engine.hpp` + `src/session/engine.cpp`. NOT a new `runtime/` module.

**Rationale**:
- `check_layers.py:29` already grants `session → {core, dictionary, wire, transport, log, otel}` — every edge the engine needs (it composes `transport::Listener`/`TransportFactory` + `session::Session`/`ReconnectFsm`). No `ALLOWED`-map change, no architecture.md §2.2 amendment.
- architecture.md §2.2 row 4 already scopes `session/` as owning the `Session`, FSM, and session lifecycle; the multi-session engine is the orchestration concern of that same module.
- A new `runtime/` module would force a `check_layers.py` ALLOWED key **and** an arch §2.2 amendment — a Gate-A-heavy change per `[[feedback_gate_b_check_layers_post_fixer]]` (007 RC#1 burned a PR hotfix on exactly this — wrong-module placement).
- The 005 `Session` scaffold already anticipates the engine: `make_session(...)` (`session.hpp:141`) documents "the engine (015) calls make_session() per accepted/initiated connection, then wires the transport." The engine is the long-anticipated owner.

**Public-type shape** (minimal, clarify-Q3-driven):
```
class Engine {
  explicit Engine(asio::any_io_executor exec);          // injected executor (Q3)
  expected_t<void> register_session(SessionConfig cfg);  // FR-002; dup SessionID → error
  void start();                                          // non-blocking; launch loops (Q3)
  asio::awaitable<void> stop();                          // idempotent; total-cancel teardown
  Session* lookup(SessionId const&) const;              // registry addressing
};
```
**Alternatives considered**: (a) new `runtime/` module — rejected (layer+arch churn, above). (b) free functions over a bag of sessions — rejected (no lifecycle owner for start/stop/teardown; FR-011 needs a single cancellation root). (c) put it in `capi/`/`service/` — rejected (those are Phase-5+ and downstream of the AGPL boundary; the engine is pre-Phase-5 per scope).

**Gate-A obligation**: add one architecture.md §4.4 public-type entry for `fixpp::session::Engine` (no layer-graph change). Per `[[feedback_gate_b_check_layers_post_fixer]]`, run `tools/check_layers.py` after the new header lands.

---

## R2 — Optional dynamic-session-provider: build or defer?

**Decision**: **DEFER the dynamic provider** to a later feature. 015 ships **static pre-configured matching only** (reversed-CompID SessionID match against the registry). The clarify-Q1 "optional dynamic hook" is recorded as an explicit, named future extension point — NOT built in this slice.

**Rationale**:
- Static matching alone fully closes **T-041** (the live acceptor identity → `authorize()` fail-CLOSED binding is proven on the static path); the dynamic provider adds no T-041 coverage.
- Per `[[karpathy-guidelines]]` Simplicity First: a pluggable `AcceptorSessionProvider` interface with no in-tree second impl is speculative configurability. QFJ ships both, but QFJ is a mature multi-release engine; fixpp's pre-1.0 MVP does not need accept-any-then-template-mint yet.
- Building it now would add a new `[const §XIV.2]` pluggable interface (needs a one-paragraph Gate-A justification) for a capability with zero current consumer — the exact "flexibility that wasn't requested" anti-pattern.
- The static path is also the **safer** default: an unmatched Logon is rejected with no session created (no accept-any surface), tightening the security posture for the T-041 review.

**Spec reconciliation**: spec FR-005 / Clarifications Q1 say the engine **MAY** support the dynamic hook ("MAY", not "MUST"). Deferring is spec-conformant. The spec text is updated only if Gate A or the user requires the hook in-slice; otherwise the "optional dynamic" language stands as documented-future.

**Alternatives considered**: (a) build the full QFJ-style `DynamicAcceptorSessionProvider` now — rejected (speculative, +1 interface, no consumer). (b) build a stub interface with no impl — rejected (a pure-virtual with no second impl is worse than a named extension point in prose). **Re-open if**: a concrete multi-tenant/accept-any requirement lands, or Gate A judges static-only insufficient for the carve-out.

---

## R3 — Accept loop & continuous inbound read-pump pattern

**Decision**: Per registered acceptor session, `co_spawn` an **accept loop** on the engine executor that repeatedly `co_await listener.async_accept()`; on each accepted `Transport`, resolve the target session by reversed-CompID (R4), `attach` the transport (wire `transport_send_`), and `co_spawn` a **read-pump** coroutine on that session's strand. The read-pump loops `transport.async_read_some(...)` → frame → `co_await session.on_inbound_frame(frame)` until EOF/read-error/cancellation. The **initiator** side reuses 014's `ReconnectFsm::drive_reconnect_attempt` for connect+handshake, then spawns the same read-pump.

**Rationale**:
- `Listener::async_accept()` (`listener.hpp:52`) already returns a fully-handshaken `Transport` (TLS issued inside) — the accept loop is a thin `while` over it.
- `on_inbound_frame` (`session.hpp:230`) is explicitly "the engine's transport feeds it after parse/frame-validate" and is documented session-strand — the read-pump is its only production driver (014 fed it only via tests).
- One read-pump coroutine per session, pinned to that session's strand, gives FR-004's in-order, on-strand, exactly-once delivery for free (single consumer, no cross-session sharing).

**Cancellation** (critical — `[const §XI.2]`, `[[feedback_asio_cospawn_total_cancellation_default]]`): every `co_spawn` (accept loop, read-pump, connect loop) defaults to **terminal-only** cancellation; each MUST `co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation())` or `stop()`'s total-cancel will silently hang. This is the headline teardown correctness target (FR-011/SC-005) and the first thing to check on any "stop() hangs" symptom.

**Framing note**: `Transport::async_read_some` yields bytes, not frames; the read-pump must accumulate + delimit FIX frames (SOH/BodyLength/checksum) before calling `on_inbound_frame`. Whether an existing Framer (004/wire) is reused or a thin accumulator lives in the pump is a **data-model decision (E-3)** — flagged there, not fabricated here.

**Alternatives considered**: (a) one shared read-pump multiplexing all sessions — rejected (breaks per-session strand isolation; serializes unrelated sessions). (b) callback-style `async_read_some` with completion handlers — rejected (the tree is coroutine-native; `[const §XI.1]`).

---

## R4 — Acceptor connection → session resolution (reversed-CompID)

**Decision**: On an accepted connection, read the inbound **Logon**, extract its SenderCompID(49)/TargetCompID(56), and look up the registry by the **reversed** tuple: `SessionId{begin_string, sender = inbound.TargetCompID, target = inbound.SenderCompID}`. Match → feed bytes to that session. No match (static-only) → reject, close the transport, create no session.

**Rationale** (reference-engine grounded):
- **QFC** `Acceptor::getSession(msg, responder)` builds `SessionID(beginString, SenderCompID(clTargetCompID), TargetCompID(clSenderCompID))` — reversed — and looks it up in `std::map<SessionID, Session*>` (`quickfix-cpp/src/C++/Acceptor.cpp:101-129`).
- **QFJ** `AcceptorIoHandler` does `MessageUtils.getReverseSessionID(message)` then `sessionProvider.getSession(sessionID)` (`quickfixj-core/.../mina/acceptor/AcceptorIoHandler.java:70-71`).
- Both engines key the session table by the FIX **SessionID tuple** — matching clarify-Q2.

**The first-frame problem**: resolution needs the inbound Logon's CompIDs, so the engine must read the **first** frame before it knows which `Session` owns the connection. **Decision**: a small pre-session read step in the accept loop reads exactly the first frame, resolves the session, then hands that first frame + the live transport to the resolved session (its `on_inbound_frame` processes the Logon, where the R5 acceptor live-identity arm authorizes). This is the QFC/QFJ "lookupSession(message)" shape. Detailed in data-model E-2.

**Alternatives considered**: (a) one listener+session statically bound 1:1 (no Logon peek) — rejected (a single listen port serves multiple acceptor sessions in both reference engines; CompID demux is required). (b) resolve by listen-port→session config — rejected (doesn't match the multi-session-per-port FIX convention; QFC/QFJ both demux by CompID).

---

## R5 — Error slots: does the engine need new `error::` variants?

**Decision**: **Reuse existing codes where they fit; append at most the genuinely-new ones** (next free slot after `session_seqnum_too_high = 120`, i.e. 121+), per `[const §X.4]` append-only. Candidate new conditions, each evaluated:
1. **Unmatched acceptor Logon** (no registry entry for the reversed CompIDs) — likely needs a new slot (e.g. `session_unknown_acceptor_session = 121`); no existing code means "no configured session for these CompIDs." **Confirm at /tasks** there's no reusable existing code.
2. **Duplicate registration** (FR-002, two configs → same SessionId) — this is a **programmatic API error at register time**, not a wire/session error; reuse `session_invalid_argument = 119` (or the core invalid-argument code) rather than mint a new slot. **Decision: reuse, do not append.**
3. **Acceptor authorization failure** — **reuse `session_compid_unauthorized`** unchanged (FR-008 inherits 013/014 shapes; no new code).

**Rationale**: Minimize ABI surface. Only #1 plausibly needs a new slot; #2/#3 reuse. Slot 70 stays a permanent hole; never renumber. The exact count (0 or 1 new slot) is locked at `/tasks`/`/implement` after a final read of `error.hpp` confirms the 120 boundary and scans for a reusable "unknown session" code.

**Alternatives considered**: minting fresh slots for all three — rejected (ABI bloat; #2/#3 have clean existing semantics). Coalescing #1 into `session_compid_unauthorized` — rejected (semantically distinct: "no such session" ≠ "session exists but peer unauthorized"; the security-review axis wants them separable).

---

## R6 — SessionId key type (registry) — new type or reuse?

**Decision**: Introduce a small value type `fixpp::session::SessionId { begin_string, sender_comp_id, target_comp_id }` (+ a reserved-but-unused `qualifier` slot for forward-compat) with value equality + a hash/`operator<`, used as the registry key. **There is no existing `SessionId` type today** (`SessionConfig` carries the three fields loose at `:151-153`; grep confirms no `SessionId`/`session_id` type).

**Rationale**:
- clarify-Q2 fixed the key as the FIX SessionID tuple; a named value type (vs keying on raw `SessionConfig`) gives clean equality for FR-002 duplicate rejection and O(1)/O(log n) lookup, and matches QFC/QFJ `SessionID` precedent.
- A **qualifier** field is included (defaulted empty) for forward-compat with QFC/QFJ session qualifiers, even though `SessionConfig` has no qualifier field yet — cheap now, avoids an ABI break later. (Mentioned in clarify-Q2 "optional qualifier.")
- Lives in `session/` (R1); no layer impact.

**Alternatives considered**: (a) key the `std::map`/`unordered_map` on `std::tuple<string,string,string>` — rejected (no semantic name; FR-002/lookup read worse; no qualifier extension point). (b) reuse `SessionConfig` as the key — rejected (heavy value type; identity ≠ whole config; two different configs can share an identity, which is exactly the FR-002 duplicate case). **Gate-A obligation**: this is a new (small) public type — add to architecture.md §4.4 alongside `Engine` (R1).

---

## Consolidated decisions

| # | Decision | Drives |
|---|----------|--------|
| R1 | Engine = new concrete type in **existing `session/`** module (`engine.{hpp,cpp}`); arch §4.4 entry only | FR-001/003/011; structure |
| R2 | **Defer** the dynamic-session-provider; static matching only this slice | FR-005; Complexity Tracking (none) |
| R3 | Per-session accept loop + per-session read-pump; **total-cancellation reset mandatory** | FR-003/004/011; SC-003/005 |
| R4 | Acceptor resolves by **reversed-CompID SessionId** (QFC/QFJ); first-frame Logon peek | FR-005/006; SC-001 |
| R5 | Reuse existing error codes; append **≤1** new slot (unmatched-Logon, 121+) | FR-002; `[const §X.4]` |
| R6 | New small `SessionId` value type (begin_string+sender+target+reserved qualifier) | FR-002; registry; arch §4.4 |

**Open items intentionally pushed to data-model.md (Phase 1), not fabricated here**: the frame-accumulation/Framer reuse decision (E-3), the exact first-frame-peek handoff mechanism (E-2), and the precise `Session` transport-attach entry point (does the engine reuse `install_reconnected_transport` or a new `attach_transport`? — confirm against `session.hpp`/`session.cpp` at design time; do not invent a method name).

**No NEEDS CLARIFICATION remain.** Ready for Phase 1 (data-model.md, contracts/, quickstart.md).
