# Phase 0 — Research: 014-transport-active-binding

All `NEEDS CLARIFICATION` resolved. 014 is a realization of three 013 stubs + five carry-forward witnesses, so "research" here is mostly **grounding decisions against verified shipped reality** (file:line) and the two spec Clarifications, not technology selection. Each decision: **Decision / Rationale / Alternatives rejected**.

---

## R1 — Source of the live authenticated peer identity

**Decision.** The live identity is `handshake_result.peer_id` returned by **`TlsTransport::async_handshake()`** (`include/fixpp/transport/tls_transport.hpp:116`, returning `asio::awaitable<core::expected_t<handshake_result>>`). `handshake_result` is a value-typed POD with `fixpp::tls::peer_identity peer_id` that **owns** its SAN/CN material (`tls_transport.hpp:52-53`), held **by value** across the session lifetime by the FSM (`tls_transport.hpp:40`). `drive_reconnect_attempt` issues `async_connect` then `async_handshake`, captures `handshake_result`, and threads `peer_id` to the existing `authorize()` call sites.

**Rationale.** This surface was shipped by 012 specifically for this consumer; no new transport API is needed. The stub today (`reconnect_fsm.cpp:124-131`) calls only `factory_->make(...)` and never `async_connect`/`async_handshake`, so the peer identity is simply never produced — 014 calls the two steps the stub skips.

**Alternatives rejected.** (a) A new `Transport::peer_identity()` accessor — redundant; `async_handshake` already returns it and `[const §XIV.2]` discourages widening the interface. (b) Re-running `verify_peer` at the session layer to re-derive identity — duplicates the handshake's work and diverges from the single source of truth.

> **Erratum corrected during planning.** An initial exploration pass reported `handshake_result`/`peer_id` as "not shipped" — that pass inspected only the base `Transport` + `TransportFactory::make`, missing the `TlsTransport` sub-interface. Direct header inspection confirms `handshake_result` IS shipped (`tls_transport.hpp:52`/`:116`). The plan reflects the corrected reality.

---

## R2 — "Hand a live transport to the session and resume" — the 014/015 boundary

**Decision.** 014 realizes `drive_reconnect_attempt` to **produce an authorized live transport + `handshake_result`** and re-drive Logon to Active, binding the new transport to the session's outbound path (`transport_send_`, `session.hpp:496`). The **continuous inbound read-pump** (read the live transport → frame/parse → `Session::on_inbound_frame`, `session.cpp:862`), the **public connect-loop component**, and the **`SessionConfig`-keyed registry** are **feature 015** (spec Out of Scope §1; Clarifications Q2). 014's "session resumes" is proven at the FSM/attempt level: the loopback-TLS integration test drives the real `async_handshake` and then feeds the post-handshake inbound frames through the existing `on_inbound_frame` seam — exactly as every shipped session test does.

**Rationale.** The Session does **not** own a `Transport` or a read loop today — it is *fed* inbound frames and emits via a callback. The byte-pump is therefore inherently an engine-layer concern, which the 014/015 split assigns to 015 ("accept/**connect** loops as a public component ... acceptor accept→Session-create→byte-feed production path"). Pulling the production pump into 014 would re-merge the engine that was deliberately carved out. Proving resume via the `on_inbound_frame` seam is consistent with the shipped test architecture and keeps 014 bounded.

**Alternatives rejected.** (a) Build the production read-pump in 014 — re-introduces the 015 engine; contradicts the split + spec Out of Scope. (b) Have `drive_reconnect_attempt` only *return* the transport with no Logon re-drive — would leave "session resumes" (US1 AC1) unproven even at the FSM level. The chosen middle ground (produce authorized transport + re-drive Logon, harness supplies inbound) is the minimal honest scope.

---

## R3 — Computing the real leaf SHA-256 for `credentials_rotated`

**Decision.** Compute the SHA-256 over **our own** loaded leaf-certificate DER (from `cert_source::load_credentials() -> local_credentials`, `cert_source.hpp:32-41`) using OpenSSL, into the existing `std::array<std::byte,32>` fields. Rotation detection lives in the **FSM**, not the factory: `ReconnectFsm` holds two owning members — `last_active_source_ : std::shared_ptr<cert_source>` and `last_active_fp_ : std::array<std::byte,32>`. On each `drive_reconnect_attempt`: read `factory_->cert_source_snapshot()`; if it differs from `last_active_source_` (i.e. `reload_credentials` staged a new source), emit `credentials_rotated{old=last_active_fp_, new=fp(new source)}` on the session strand **before** `make()`, then update both members. First-ever attempt (`last_active_source_ == nullptr`) is the initial load, **not** a rotation → no event. No-op rotation (`new fp == old fp`) **still** emits (FR-011).

**Rationale.** Keeps the change inside 014's `session` module — **no new `cert_source` or `TransportFactory` pure-virtual**, so `[const §XIV.2]` caps stay untouched. Holding `last_active_source_` as a **strong-ref owning FSM member** (not a `weak_ptr`) is the explicit fix for `[[feedback_weak_ptr_cache_needs_owning_context]]`: the old source must survive to fingerprint it on the next rotation. The emit-before-`make()` ordering and the no-op-still-emits rule are inherited verbatim from 013 FR-032.

**Alternatives rejected.** (a) Compute/track fingerprints inside `TransportFactory::reload_credentials` and expose a `pending_rotation()` accessor — widens the transport surface for a session-observability concern. (b) A new `cert_source::leaf_fingerprint()` pure-virtual — consumes pluggable-interface budget for a value derivable from `load_credentials`. (c) `weak_ptr<cert_source>` cache — the old source would expire before the next rotation → wrong/empty `old_sha256` (the documented weak_ptr-cache trap).

---

## R4 — Authorization-failure treatment in the reconnect loop (Clarifications Q1)

**Decision.** **Reason-agnostic retry-to-cap.** An `authorize()` failure on a reconnect attempt (off-list / absent identity under a binding policy) emits the inherited fail-closed signals (`session_compid_unauthorized` + `compid_authorization_failed`) **and** consumes exactly one attempt, backing off per the schedule and retrying until the `ReconnectPolicy` cap, then terminal-disconnected — identical to a connect or handshake failure. No fail-fast, no distinct cap, **no new terminal cause/code**.

**Rationale.** Matches the spec's stated working default and the resolved Clarification. Reference-engine sweep: QuickFIX-cpp / QuickFIX-J reconnect is reason-agnostic (a rejected/invalid Logon still reschedules a reconnect via the configured interval) — there is no "auth failure stops forever" precedent. Reason-agnostic is also the simplest FSM wiring (one failure path, one counter) and avoids inventing a new error slot.

**Alternatives rejected.** (a) Fail-fast on auth (terminal, no retry) — diverges from the reference engines and the cap-bounded default; an operator misconfig would surface differently from a transient outage, which the spec explicitly declines. (b) A distinct lower cap / distinct terminal cause for auth failures — more wiring + a new code for no demonstrated need (Clarifications Q1 chose neither).

---

## R5 — Carry-forward witness feasibility on the live fixture (US4)

**Decision.** The live `asio_tls_transport`-over-loopback handshake fixture that US1 introduces is exactly what the 013/012 waivers were blocked on; each carry-forward gets a real witness:
- **FR-012 `sigalg_disallowed`**: add an Ed25519/Ed448 (unknown-`EVP_PKEY`) leaf fixture under `tests/tls/fixtures/`; the `[const §XII.3]` allow-list rejects it → the `sub_reason="sigalg_disallowed"` cell in `tests/transport/test_tls_validation_failed_taxonomy.cpp` (today annotated "not available in fixtures", covered only at unit level in `test_verify_peer_t039.cpp`).
- **FR-013a counter**: `tests/session/test_session_invariant_counter_witness.cpp` currently records `load_credentials()` as **infeasible/zero** under `mock_transport` (`:22-35`); re-target it at the live fixture so the once-per-handshake invariant (`calls == 1`) is genuinely asserted, per `[[feedback_half_restructure_symmetric_api]]` invariant-counting pattern.
- **FR-013b bench**: wire the `bench/transport/bench_tls_handshake_loopback.cpp` scaffold (T029 TODO) to the live factory + loopback acceptor → first real 1-RTT baseline.
- **FR-014 PMR-OOM depth**: add a **multi-SAN** leaf fixture so `throw_on_nth_resource` (`tests/transport/test_verify_peer_pmr_oom.cpp`) exhausts at the **mid** (san-dns construction) and **tail** sites, not only the boundary (`N=1`) — per `[[feedback_trap_throw_pmr_witness_enumerate_sites]]`.
- **FR-015 fuzz re-label**: catalogue/scope-label edit on `tests/fuzz/fuzz_transport_handshake.cpp`'s entry only — **no harness body change, no new parser-touching code** (so `[const §VII.7]` needs no new harness).

**Rationale.** All five waivers cited "needs a live TLS handshake" as the blocker; 014 supplies it. Bundling them here (rather than a separate chore branch) is the explicit intent of the 014/015 split and `[[project_013_carryforwards_to_014]]`.

**Alternatives rejected.** Deferring the witnesses to the `chore/` lint branch — that branch is for clang-format/tidy/iwyu/coverage cleanup (Out of Scope §3), not behavioural witnesses; these belong with the live path that unblocks them.

---

## R6 — Slot-74 stand-in cleanup mechanics (FR-016)

**Decision.** Mint **`error::session_seqnum_too_high = 120`** (the next free slot — 012 occupies 94..115, 013 occupies 116..119; cross-checked at `/speckit-implement`-time against `include/fixpp/core/error.hpp` to confirm the boundary is still 119, no ±N drift). Change `src/session/seqnum_manager.cpp:71-78` to `co_return std::unexpected(error::session_seqnum_too_high)` and update its comment; flip the assertion at `tests/session/seqnum_manager_test.cpp:145-150` to expect `session_seqnum_too_high`; update the contract note. Slot **70** stays a permanent numeric hole (`[const §X.4]`); slot **74** (`session_test_request_unanswered`) **keeps its real meaning** and its other uses.

**Rationale.** The too-high branch returning a *liveness/TestRequest* code (74) is a semantic misnomer introduced by 013 T006a when slot 70 was deleted. **Zero behavioural change**: all 3 production callers (`session.cpp:904`/`:1261`/`:1703`) discard `check_inbound`'s error code and just disconnect, so no observable output changes today — this is a latent-landmine / ABI-hygiene fix (a future consumer wiring the code into a `SessionEvent`/log would otherwise surface a phantom "TestRequest unanswered"). Append-only per `[const §X.4]`; it is a C++ enum value, **not** a C-ABI symbol, so no abidiff.

**Alternatives rejected.** (a) Re-use slot 70 — forbidden (permanent hole, `[const §X.4]`). (b) Leave the stand-in (zero impact today) — the spec explicitly lists FR-016 + the v1.0 taxonomy-hygiene gate flags it; deferring just re-accrues the carry-forward. (c) Overload slot 74 with a clarifying comment only — keeps the semantic misnomer in the type system.

---

### Resolved unknowns summary

| # | Unknown | Resolution |
|---|---------|-----------|
| R1 | Live peer-id source | `TlsTransport::async_handshake().peer_id` (shipped; stub skips the call) |
| R2 | "Hand to session / resume" scope | FSM-level produce+re-Logon; continuous pump + connect-loop + registry = 015 (Q2) |
| R3 | Real leaf fingerprint | OpenSSL SHA-256 over our `local_credentials` leaf; FSM-held strong-ref rotation state; no new pure-virtual |
| R4 | Auth-failure in reconnect loop | Reason-agnostic retry-to-cap (Q1); no new code (QFC/QFJ grounding) |
| R5 | Carry-forward feasibility | Live loopback fixture unblocks all five; fuzz = re-label only |
| R6 | Slot-74 cleanup | New `session_seqnum_too_high = 120`; zero behavioural change; slot 70 hole / 74 meaning preserved |
