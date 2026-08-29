# 012-2h-transport — Operator + Developer Quickstart

**Branch**: `012-2h-transport` | **Date**: 2026-05-27 | **Spec**: [spec.md](spec.md) | **Plan**: [plan.md](plan.md)

This guide walks an operator (initiator + acceptor) and a fixpp developer (FSM-via-mock-transport) through the four user stories enumerated in spec.md. Each scenario shows the minimum config + the exact call sequence; the production headers under `include/fixpp/transport/` will mirror the `contracts/` directory at `/speckit-implement` time.

---

## Prerequisites

- **011-tls-policy** SHIPPED (merged 2026-05-26 per `[[project_011_tls_policy_closed]]`). The 5 public types (`cert_source` / `file_cert_source` / `Pinset` / `CipherPolicy` / `SecurityProfile` + `SslCtxConfig` + `peer_identity`) are LOCKED.
- **2b wire / 2d threading / 2e msgstore / 2f async-mutex / 2g tls** signed-off design docs are in `.specify/`; the published surfaces are consumed UNCHANGED.
- **Session FSM (Phase-4)** is NOT YET implemented — the post-this-feature spec drafts it. The scenarios below show 2h's surface as consumed BY a hypothetical FSM; the actual FSM lands in the next feature.

---

## Scenario A — TLS-encrypted FIX initiator session (US1; P1)

Open an initiator session against a counterparty gateway over TLS, exchange a Logon, then run the read-loop.

### A.1 Build the SslCtxConfig (consumed from 011)

```cpp
#include <fixpp/tls/security_profile.hpp>
#include <fixpp/tls/cert_source.hpp>

namespace tls = fixpp::tls;

// Operator-supplied configuration.
auto cert_source_cfg = tls::file_cert_source::Config{
    .leaf_cert_path     = "/etc/fixpp/certs/initiator_leaf.pem",
    .private_key_path   = "/etc/fixpp/certs/initiator_leaf.key",
    .trust_anchors_path = "/etc/fixpp/certs/ca_bundle.pem",
    .password_cb        = nullptr,
};
auto cert_source = tls::make_file_cert_source(cert_source_cfg, /*mr*/ nullptr).value();

auto pinset = std::make_shared<tls::Pinset>(tls::Pinset::Config{/*mr*/ nullptr});
// Pinset is populated separately at operator-control-plane time per [2g §4.3].

auto cipher_policy = tls::CipherPolicy{};  // compile-time allow-list per [const §XII.3]
auto clock         = std::make_shared<fixpp::core::system_clock_source>();

auto ssl_cfg = tls::make_ssl_ctx_config(
    /* profile */ tls::SecurityProfile::mtls_pinned,
    /* cs      */ cert_source,
    /* clock   */ clock,
    /* pinset  */ pinset,
    /* mr      */ nullptr).value();
```

### A.2 Construct the TransportFactory (engine-anchored)

```cpp
#include <fixpp/transport/transport.hpp>
#include <fixpp/transport/transport_factory.hpp>

namespace tx = fixpp::transport;

// Engine-anchored factory; caches the SslCtxConfig + OpenSSL SSL_CTX* at
// factory level per spec FR-026 — long-lived state is NOT rebuilt per
// reconnect attempt.
auto factory = std::make_unique<tx::asio_tls_transport_factory>(
    tx::Transport::Config{
        // Per-session knobs: Nagle OFF + no linger per Clarifications Q5=A.
        // .tcp_nodelay = true (default per Q5)
        // .so_linger_enabled = false (default per Q5)
        .max_read_window_bytes  = 256 * 1024,    // matches [2b §1.2]
        .tls_handshake_timeout  = std::chrono::seconds(30),
        .tls_close_timeout      = std::chrono::seconds(1),
    });

// Engine wiring (post-this-feature; shown for context):
//   EngineConfig::default_transport_factory = std::move(factory);
//   ReconnectPolicy reconnect = tx::ReconnectPolicy::defaults(mr);
```

### A.3 Open the session (post-this-feature FSM; sketched)

```cpp
// At session open the FSM will:
//   1. Mint a Transport via the resolved factory.
auto transport = factory->make(exec, ssl_cfg, mr).value();

//   2. dynamic_cast<TlsTransport*> EXACTLY ONCE; store typed pointer on Session.
//      v1.0 partition invariant per contracts/tls_transport.hpp class-level note
//      (lines 69-71): every Transport returned by the v1.0 factory is TLS-capable
//      per [FIX-SL §4.3.1] + [FIXS §1.1] mandatory-TLS reality and [2h §4.5]'s
//      drop of the v0.1 plain-TCP-via-empty-SslCtxConfig narrative — so the cast
//      MUST NOT return null in v1.0. The assert documents the invariant; in
//      release builds a null result here means a misconfigured factory.
auto* tls_transport = dynamic_cast<tx::TlsTransport*>(transport.get());
assert(tls_transport != nullptr
    && "v1.0 partition: factory MUST return a TlsTransport-capable instance "
       "per tls_transport.hpp class note");

//   3. async_connect on the resolved Endpoint.
auto endpoint = tx::Endpoint{"gateway.venue.example.com", 4001};
auto connect_result = co_await transport->async_connect(endpoint);
if (!connect_result) {
    // connect_result.error() carries one of:
    //   transport_resolve_failed / transport_connect_refused /
    //   transport_connect_timeout / transport_connect_cancelled
    co_return propagate(connect_result.error());
}

//   4. async_handshake on the TlsTransport*.
auto handshake_result = co_await tls_transport->async_handshake(ssl_cfg);
if (!handshake_result) {
    // handshake_result.error() carries one of:
    //   transport_handshake_failed (diagnostic sub-reason: OpenSSL string +
    //     [2g §6.6] tls_* sub-reason if verify_peer rejected; per FR-034a) /
    //   transport_handshake_timeout / transport_handshake_cancelled
    // The 15 tls_* variants from [2g §6.6]:986-1004 surface UNCHANGED — no
    // re-translation.
    co_return propagate(handshake_result.error());
}

//   5. Hold handshake_result BY VALUE for the session lifetime.
session.handshake_ = std::move(handshake_result.value());
//   session.handshake_.peer_id → consumed by T-041 CompID binding (session
//                                  Phase-4 spec).
//   session.handshake_.captured_pinset → null under mtls_ca; non-null under
//                                          mtls_pinned (guard before deref).
//   session.handshake_.negotiated_cipher → e.g. "TLS_AES_128_GCM_SHA256".
```

### A.4 The read-loop (zero-alloc on dispatch)

```cpp
// 2b owns the framer-carry arena per [2b §6.6]. The transport NEVER allocates
// a read buffer — the caller passes the framer's per-session arena slice.
std::array<std::byte, 64 * 1024> framer_carry;  // arena-backed in production
fixpp::wire::Framer framer{...};

while (!session.shutting_down()) {
    auto read_result = co_await transport->async_read_some(
        std::span<std::byte>(framer_carry));  // aliasing 2b's framer-carry arena
    if (!read_result) {
        if (read_result.error() == errors::read_eof) {
            // Peer closed cleanly; FSM transitions to disconnect / reconnect.
        }
        co_return propagate(read_result.error());
    }
    // ZERO global new/delete on this dispatch chain per spec FR-003 +
    // [2h §9 seam #4]:1345 alloc guard (DUAL gate: counting_resource +
    // mallocnesia LD_PRELOAD). PMR arena allocs are expected.
    framer.feed(std::span<const std::byte>(framer_carry.data(), *read_result));
}
```

### A.5 The write-loop (durable-before-transmit)

```cpp
// The FSM sequences: Writer::commit → store(committed_span, outbound) →
// async_write. 2h's async_write is purely the wire-side hop; the
// durable-before-transmit invariant per [2e §6.1.4] is the FSM's contract.
auto committed_span = writer.commit();
auto store_result = co_await message_store->store(seqnum, committed_span,
                                                  fixpp::msgstore::direction::outbound);
if (!store_result) { /* persisted-store-failed; treat as fatal */ }

auto write_result = co_await transport->async_write(committed_span);
if (!write_result) {
    // write_result.error() carries one of:
    //   transport_write_cancelled (persisted frame is NOT rolled back per
    //                              [2e §6.1.4]; recovery via ResendRequest) /
    //   transport_write_error / transport_write_short
}
```

### A.6 Acceptance criteria mapping

| AC | Verified by |
|---|---|
| AC1: handshake succeeds; `handshake_result.peer_id` carries verified counterparty by value | spec FR-010 + `[2h §9 seam #7]` |
| AC2: zero heap allocs on read-path completion-handler dispatch | spec FR-003 + SC-003 + `[2h §9 seam #4]:1345` DUAL gate (`counting_resource` PMR-routed + mallocnesia LD_PRELOAD global `operator new` interception per `[[feedback_tracking_pmr_resource_false_pass]]`) |
| AC3: outbound frame round-trips OR cancels via `transport_write_cancelled` without rollback | spec FR-030 + `[2h §9 seam #8]` |
| AC4: handshake against bad-cert peer surfaces UNCHANGED `tls_*` variant from `[2g §6.6]` | spec FR-012 + `[2h §9 seam #7]` |
| AC5: transport-private failures surface DISTINCT named variants | spec FR-002 + spec FR-034 + `[2h §9 seam #15]` |

---

## Scenario B — Bounded reconnect after transient disconnect (US2; P2)

Configure a session with `ReconnectPolicy::defaults()`; verify the FSM destroys the dead Transport, mints a fresh one via the factory, and respects the cumulative 73–89 s envelope.

### B.1 Configure the ReconnectPolicy

```cpp
#include <fixpp/transport/reconnect_policy.hpp>

namespace tx = fixpp::transport;

// Default envelope: [100ms, 200ms, 400ms, 800ms, 1.6s, 3.2s, 6.4s, 12.8s,
//                    25.6s, 30s] × jitter=0.10 × max_attempts=10.
// Cumulative wall-clock at max_attempts=10: 73–89 s (±10% jitter spread per
// design-doc §1.1 corrected numeric).
auto reconnect = tx::ReconnectPolicy::defaults(/*mr*/ nullptr);

// Alternative: industry-canonical QuickFIX-cpp / Fix8 shape — single fixed
// 30 s interval, no jitter, no cap.
auto reconnect_qfc = tx::ReconnectPolicy::defaults_quickfix_compat(/*mr*/ nullptr);

// Operator opt-in to unbounded retry on top of the v1.0 defaults:
auto reconnect_unbounded = tx::ReconnectPolicy::defaults(/*mr*/ nullptr);
reconnect_unbounded.max_attempts = 0;  // explicit opt-in per [const §XII.5]
```

### B.2 The reconnect loop (post-this-feature FSM; sketched)

```cpp
// FSM-owned reconnect loop per Clarifications 2026-05-27 Q1=B:
// destroy dead Transport → sleep → mint FRESH Transport via factory →
// async_connect on the NEW instance.
//
// attempt_n is 0-indexed (first reconnect attempt uses schedule[0] = 100 ms);
// attempt_n is incremented EXACTLY ONCE per iteration at the bottom of the
// loop body. The cap is checked at the top so the first attempt with
// attempt_n == 0 always runs.
auto attempt_n = 0u;
while (true) {
    // max_attempts=10 ⇒ attempt_n iterates 0..9 (10 attempts); when attempt_n
    // becomes 10 at the bottom-of-loop increment, this cap check fires and
    // surfaces transport_reconnect_limit_exceeded per FR-022 + US2 AC2.
    if (reconnect.max_attempts != 0 && attempt_n >= reconnect.max_attempts) {
        co_return propagate(errors::reconnect_limit_exceeded);
    }

    auto delay = reconnect.delay_for_attempt(attempt_n);  // 0-indexed
    co_await clock->sleep_for(delay);

    // Destroy dead Transport BEFORE requesting new one — at most one live
    // Transport per session per spec FR-028.
    session.transport_.reset();

    // Mint fresh Transport via SAME factory; SslCtxConfig + OpenSSL SSL_CTX*
    // cached at factory level per spec FR-026 — no per-attempt cold-path cost.
    auto fresh = factory->make(exec, ssl_cfg, mr);
    if (fresh) {
        session.transport_ = std::move(fresh.value());

        auto connect_result = co_await session.transport_->async_connect(endpoint);
        if (connect_result) {
            co_return /* success — proceed to async_handshake */;
        }
    }
    // Factory failure OR connect failure → next attempt.

    ++attempt_n;  // single increment site per iteration; matches the
                  // "delay BEFORE each attempt" semantics described in
                  // FR-021 + spec US2 AC1.
}
```

### B.3 Acceptance criteria mapping

| AC | Verified by |
|---|---|
| AC1: full 10-entry schedule (100ms/200ms/.../25.6s/30s) at ±10% jitter produces pre-jitter cumulative 81 100 ms widened to 73-89 s wall-clock | spec FR-020 + `[2h §9 seam #13]` reconnect_policy_schedule (pins the schedule numerics) |
| AC2: 10th attempt fails → `transport_reconnect_limit_exceeded` | spec FR-022 + `[2h §9 seam #13]` |
| AC3: `max_attempts = 0` (explicit) → unbounded retry at `max_delay` | spec FR-019 + `[2h §9 seam #13]` parallel cell |
| AC4: ±10% jitter spreads reconnect bursts across fleet — no synchronised spike | spec FR-021 (deterministic-per-attempt jitter; seam #13) |

---

## Scenario C — Multi-session acceptor on a single port (US3; P3)

Stand up an `asio_listener` on `0.0.0.0:0`; accept 64 concurrent connections; each produces a fresh Transport.

### C.1 Construct the Listener

```cpp
#include <fixpp/transport/listener.hpp>

namespace tx = fixpp::transport;

auto listener_cfg = tx::asio_listener::Config{
    .bind_endpoint = tx::Endpoint{"0.0.0.0", 4001, /*backlog=*/256},
    .so_reuseaddr  = true,
    .so_reuseport  = false,   // Linux-only; off by default
    .accepted_transport_config = tx::Transport::Config{},  // defaults apply
    .ssl_cfg = ssl_cfg_acceptor,   // ACCEPTOR-side SslCtxConfig
                                    // (mtls_pinned / mtls_ca; SSL_VERIFY_PEER
                                    // requires client cert per [2g §4.5.1])
};

// Hold a typed asio_listener directly so the concrete-impl-only cancel() API
// (per spec FR-023 + FR-025; the abstract Listener base does NOT publish
// cancel() — see contracts/listener.hpp class-level NOTE lines 56-63) is
// reachable. make_asio_listener returns unique_ptr<Listener> for production
// engine-owned wiring (per data-model E-8 ownership row); this quickstart
// demonstrates the consumer-held shape needed for the C.3 cancel example.
// Throwing-on-failure constructor is permitted at engine bootstrap per
// [arch §5.3] carve-out (contracts/listener.hpp:109-111).
auto listener = std::make_unique<tx::asio_listener>(
    /* exec */ acceptor_exec,         // caller's choice; the accepted socket is built on it
    listener_cfg);
```

### C.2 The accept loop

```cpp
while (!engine.shutting_down()) {
    auto accept_result = co_await listener->async_accept();
    if (!accept_result) {
        if (accept_result.error() == errors::accept_cancelled) {
            // Listener::cancel() called per Option-A contract.
            co_return;
        }
        // Log + continue; spurious accept failures shouldn't kill the loop.
        log_warn("async_accept failed: {}", accept_result.error());
        continue;
    }

    // Fresh Transport — ownership transferred at this point. Per Clarifications
    // 2026-05-27 Q4=A, this Transport is UNAFFECTED if Listener::cancel() fires
    // after this line — listener has no handle to it.
    auto new_session = open_session_on_acceptor_side(std::move(accept_result.value()));
    engine.attach_session(std::move(new_session));
}
```

### C.3 Cancelling the listener

```cpp
// Option-A contract per Clarifications Q4=A:
//   (1) close listening socket — no new connects;
//   (2) complete in-flight async_accept with transport_accept_cancelled;
//   (3) already-resumed unique_ptr<Transport> results UNAFFECTED.
auto cancel_result = listener->cancel();

// To close already-produced Transports, the CONSUMER tracks them:
for (auto& session : engine.sessions()) {
    session.transport_->close();
}
```

### C.4 Acceptance criteria mapping

| AC | Verified by |
|---|---|
| AC1: 64 concurrent clients → 64 distinct Transports, 64 sessions Active | spec FR-023 + `[2h §9 seam #14]` listener_acceptor |
| AC2: `cancel()` closes listening socket; subsequent connects refused; in-flight accept → `transport_accept_cancelled` per `[2h §6.6]:1191` | spec FR-025 + `[2h §9 seam #14]` parallel cell |
| AC3: 65th client at backlog full → TCP RST per OS behaviour (not over-promised) | spec FR-024 + Edge Cases |

---

## Scenario D — Deterministic mock-transport test seam (US4; P3)

Write a hermetic FSM unit test against `mock_transport` — no real TCP, no OpenSSL, no kernel scheduling jitter.

### D.1 Construct the mock with a scripted byte stream

```cpp
#include <fixpp/transport/test/mock_transport.hpp>

namespace tx_test = fixpp::transport::test;

// Test binary does NOT link OpenSSL or ASIO networking — only the awaitable +
// strand primitives. mock_transport's handshake is faked (returns the
// pre-recorded peer_identity directly; no real OpenSSL handshake exchange).
auto script = tx_test::Script{
    .inbound_bytes = encode_fix_logon_response(...),
    .expected_outbound_writes = {encode_fix_logon(...)},
    .handshake_succeeds = true,
    .peer_identity_to_return = make_test_peer_identity("CN=peer42.example.com"),
    .pinset_snapshot_to_return = nullptr,   // mtls_ca-mode test
    .negotiated_cipher = "TLS_AES_128_GCM_SHA256",
    .read_latency  = std::chrono::milliseconds(0),
    .write_latency = std::chrono::milliseconds(0),
    .handshake_latency = std::chrono::milliseconds(0),
};

auto mock = std::make_unique<tx_test::mock_transport>(test_strand_exec, std::move(script));

// FSM-under-test consumes mock as TlsTransport*; the swap is at the
// TransportFactory boundary (SessionConfig::transport_factory_override).
auto* tls_transport = static_cast<tx::TlsTransport*>(mock.get());
```

### D.2 Cancellation tests run hermetically

```cpp
// Cancel an in-flight async_read_some — under the mock just as under the
// production asio_tls_transport.
cs.emit(asio::cancellation_type::total);
auto result = co_await mock->async_read_some(buf);

EXPECT_FALSE(result);
EXPECT_EQ(result.error(), errors::read_cancelled);  // Identical variant as
                                                     // production impl per
                                                     // spec FR-037.
```

### D.3 Acceptance criteria mapping

| AC | Verified by |
|---|---|
| AC1: FSM reaches Active deterministically (no timing flakiness, no real socket) | spec FR-036 + `[2h §9 seam #6]` (FSM-via-mock-transport binary; no OpenSSL link) |
| AC2: cancelled `mock_transport::async_read_some` → `transport_read_cancelled` (identical to production) | spec FR-037 + `[2h §9 seam #5]` cancellation_propagation |
| AC3: scripted `transport_handshake_failed` triggers same FSM path as bad-cert peer | spec FR-038 |

---

## 011 F-1 carryover witness (FR-033 + SC-008; BINDING for 012 Gate B)

After 012's `/speckit-implement` completes, the 8-cell `cancellable_dispatch`-recipe witness for `file_cert_source::load_credentials` MUST be GREEN at 012's Gate B per `[[project_011_tls_policy_closed]]` carryover. Test fixture wires a real `Session*` + `session_executor` (only constructible once transport wiring exists).

### The 8 cells (4 deterministic cases × 2 executor modes; Clarifications Q3=C)

| Cell | Case | Mode | Expected outcome |
|---|---|---|---|
| 1 | Cached-state fast path per `[2g §6.4]:927-928` | `per_session_strand` | returns cached `local_credentials` directly (NO dispatch invoked) |
| 2 | Cached-state fast path per `[2g §6.4]:927-928` | `direct_executor` | returns cached `local_credentials` directly (NO dispatch invoked) |
| 3 | Slot signalled BEFORE pickup per `[2d §6.5]` case 1 | `per_session_strand` | `tls_load_cancelled` |
| 4 | Slot signalled BEFORE pickup per `[2d §6.5]` case 1 | `direct_executor` | `tls_load_cancelled` |
| 5 | Slot signalled DURING execution per `[2d §6.5]` case 2 | `per_session_strand` | `tls_load_cancelled` |
| 6 | Slot signalled DURING execution per `[2d §6.5]` case 2 | `direct_executor` | `tls_load_cancelled` |
| 7 | Slot NOT signalled (happy path) per `[2d §6.5]` case 3 | `per_session_strand` | success |
| 8 | Slot NOT signalled (happy path) per `[2d §6.5]` case 3 | `direct_executor` | success |

Test file: `tests/transport/test_load_credentials_seam13_witness.cpp` (per plan Project Structure). All 8 cells GREEN is the spec SC-008 outcome and the 012-Gate-B-pass precondition for 011 F-1 closure.

---

## Reference

- [Spec](spec.md) — FR / SC / Edge Cases / Assumptions / Clarifications.
- [Plan](plan.md) — Technical Context + Constitution Check + Project Structure + Phase 1/2 outputs.
- [Research](research.md) — Decisions D-1..D-25 with reference-engine sweep evidence.
- [Data Model](data-model.md) — Entity E-1..E-15 reference.
- [Contracts](contracts/) — 8 header shapes re-emitted verbatim from `[2h §4]`.
- `.specify/2h-transport.md` v0.3 — bound upstream.
- `.specify/2g-tls.md` v0.4 — 011 producer surface (LOCKED).
- `.specify/architecture.md` §4.5 — `transport/` module surface inventory.
- `.specify/constitution.md` Article XII — Security & TLS.
