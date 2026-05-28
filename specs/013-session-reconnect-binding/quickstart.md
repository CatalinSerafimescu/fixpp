# Quickstart — 013-session-reconnect-binding

**Branch**: `013-session-reconnect-binding` | **Date**: 2026-05-28 | **Plan**: [plan.md](plan.md) | **Spec**: [spec.md](spec.md)

Five scenarios — four operator-facing (recovery / CompID binding / TLS validation event / credential rotation) + one developer-facing (FSM-via-mock-transport recovery seam). Each scenario maps to one or more FRs and is exercised by a named test in the plan.md §Project Structure test plan.

---

## Scenario A — Recovery-active reconnect after transient disconnect (US1)

**FRs covered**: FR-001 / FR-002 / FR-009..FR-016 / SC-001 / SC-002

```cpp
#include <chrono>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/transport/reconnect_policy.hpp>

using namespace std::chrono_literals;

auto run_long_lived_initiator() -> asio::awaitable<void> {
    auto exec = co_await asio::this_coro::executor;

    // 1. Build SessionConfig with a non-trivial reconnect policy.
    fixpp::session::SessionConfig cfg;
    cfg.heartbeat_interval = 30s;
    cfg.logout_disconnect_timeout_ms = 2000;            // FR-008 default
    cfg.reset_seqnum_policy = fixpp::session::reset_seqnum_policy::bilateral_strict;
    cfg.reconnect_policy = fixpp::transport::ReconnectPolicy::defaults();
        // [100ms, 200ms, 400ms, 800ms, 1.6s, 3.2s, 6.4s, 12.8s, 25.6s, 30s]
        // × jitter=0.10 × max_attempts=10

    // 2. Open the session. On any transient disconnect (peer drops, network blip),
    //    the FSM walks reconnect_policy via the consumed TransportFactory, mints a
    //    fresh Transport per attempt (FR-002), re-Logons, and runs the recovery
    //    sub-protocol (FR-009..FR-016) without operator intervention.
    auto session = fixpp::session::Session{cfg, exec};
    auto open_result = co_await session.open(/* endpoint, peer-compid, ... */);
    if (!open_result) {
        // Logged error::session_* or error::transport_* variant; the operator's
        // SessionEvent handler also saw structured events.
        co_return;
    }

    // 3. The session is Active. Send application messages; recovery happens
    //    automatically on every transient disconnect that lands within the policy
    //    envelope.
    for (int i = 0; i < 1000; ++i) {
        co_await session.send(/* NewOrderSingle ... */);
        // If a venue blip occurred, the FSM transparently:
        //   (a) destroys the dead Transport
        //   (b) walks reconnect_policy schedule
        //   (c) mints fresh Transport per attempt
        //   (d) re-Logons
        //   (e) issues ResendRequest for missing-inbound range
        //   (f) replies to peer's ResendRequest with replay-from-store
        //   (g) reaches Active with both sides on coherent seqnums
    }

    co_await session.logout();
}
```

**Observability** — operator polls the 013-introduced ring-buffer accessor for SessionEvent values:

```cpp
// FR-035 — NEW 013 accessor; distinct from 010 F-04's
// `fsm_visit_history()` (which observes fsm_state transitions, UNCHANGED).
// Membership-witness: physical-buffer order, NOT chronologically ordered;
// consumer asserts via Contains / std::find / std::visit pattern-match.
auto events_view = session.recent_events();  // std::span<const SessionEvent>
for (auto const& ev : events_view) {
    std::visit([](auto const& e) {
        using T = std::decay_t<decltype(e)>;
        if constexpr (std::is_same_v<T, fixpp::session::session_event_sequence_numbers_reset>) {
            // FR-018: sequence numbers reset; by_peer_request distinguishes who requested
        }
        // ... other 013 variants: peer_identity_bound / compid_authorization_failed /
        //     tls_validation_failed / credentials_rotated ...
    }, ev);
}
```

**Independent test**: `tests/session/test_reconnect_happy_path.cpp` drives this scenario against `mock_transport` (deterministic; no OpenSSL / ASIO networking); the equivalent live-engine matrix is the 014-interop-harness feature.

---

## Scenario B — Multi-tenant acceptor with CompID↔TLS-identity binding (US2)

**FRs covered**: FR-019..FR-025 / SC-003

```cpp
#include <fixpp/session/compid_authorization_policy.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/transport/listener.hpp>

auto run_multi_tenant_acceptor() -> asio::awaitable<void> {
    auto exec = co_await asio::this_coro::executor;

    // 1. Build the binding policy — allow-list of {principal → {compid_set}}.
    //    Default-deny: empty policy rejects ALL Logons (FR-023 / D-9).
    fixpp::session::CompIdAuthorizationPolicy policy;
    policy.add_binding("ACME-PROD-01", "ACME01");
    policy.add_binding("ACME-PROD-02", "ACME02");
    policy.add_binding("BETA-PROD-01", "BETA01");
    // ... 50 buy-side clients ...

    // 2. Build the acceptor's SessionConfig with the policy.
    fixpp::session::SessionConfig cfg;
    cfg.security_profile = fixpp::session::SecurityProfile::mtls_pinned;
    cfg.compid_authorization_policy = std::move(policy);  // moved-into-config
    cfg.heartbeat_interval = 30s;

    // 3. Stand up the asio_listener; for each accepted Transport, the FSM:
    //    (a) consumes handshake_result.peer_id (from 011's verify_peer)
    //    (b) extracts the principal per FR-022 canonical-fixed order
    //          (CN → SAN-DNS → SAN-URI → SHA-256-fingerprint, first-non-empty wins)
    //    (c) consults cfg.compid_authorization_policy.authorize(peer_id, asserted_compid):
    //          // Call-shape (types inlined for clarity):
    //          //   auto result = cfg.compid_authorization_policy.authorize(
    //          //       /* fixpp::tls::peer_identity const& = */ pid,
    //          //       /* std::string_view asserted_compid = */ logon_senderCompID);
    //          // result: expected_t<bound_principal>
    //          //   bound_principal::value  -> std::string_view (extracted principal value)
    //          //   bound_principal::from   -> source enum: CN / SAN_DNS / SAN_URI / SHA256_FINGERPRINT
    //          //   on failure: expected_t::unexpected(error::session_compid_unauthorized)
    //    (d) on success: emits SessionEvent::peer_identity_bound; session reaches Active
    //    (e) on failure: emits SessionEvent::compid_authorization_failed;
    //          session rejects Logon with session_compid_unauthorized (slot 117);
    //          Transport closes; session goes Disconnected.
    auto listener = fixpp::transport::make_asio_listener(/* bind addr / port */, cfg);
    while (true) {
        auto accepted = co_await listener.async_accept();
        if (!accepted) co_break;
        // each accepted Transport drives a Session FSM independently.
    }
}
```

**Independent test**: `tests/session/test_compid_binding_default_deny.cpp` + `test_compid_binding_principal_extraction.cpp` + `test_compid_binding_symmetry.cpp` cover the 3-axis matrix (default-deny / 4 principal sources / initiator-vs-acceptor symmetry).

---

## Scenario C — TLS validation outcome as session-level event (US3)

**FRs covered**: FR-026..FR-029 / SC-007

```cpp
#include <fixpp/session/session_event.hpp>
#include <fixpp/core/error.hpp>

// Operator polls the 013-introduced recent_events() ring-buffer accessor;
// the event surfaces even when no Session ever opens (FR-028 — channel is
// bound to Listener / SessionConfig, not to a Session-instance lifecycle).
auto handle_recent_events(std::span<const fixpp::session::SessionEvent> events) -> void {
    for (auto const& ev : events) {
        std::visit([](auto const& e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, fixpp::session::session_event_tls_validation_failed>) {
                // FR-026: `code` is the precise master-enum error::tls_* variant
                //          per shipped include/fixpp/core/error.hpp:403-429
                //          (6 cells: tls_handshake_failed GROUPING + 5 specifics).
                // FR-027: operator-config errors vs peer-cert errors share the
                //          GROUPING `code = tls_handshake_failed` but the
                //          `sub_reason` field discriminates (per 011 /clarify Q2).
                //          Triage: switch on `code` first; for `tls_handshake_failed`,
                //          switch on `sub_reason`.
                switch (e.code) {
                    case fixpp::core::error::tls_pin_mismatch:
                        // potential MITM; investigate
                        break;
                    case fixpp::core::error::tls_rsa_key_too_large:
                    case fixpp::core::error::tls_cert_der_too_large:
                    case fixpp::core::error::tls_san_entries_exceeded:
                        // DoS-cap violations; log + drop
                        break;
                    case fixpp::core::error::tls_load_cancelled:
                        // operator-initiated cancellation; no action
                        break;
                    case fixpp::core::error::tls_handshake_failed:
                        // GROUPING — discriminate via sub_reason:
                        if (e.sub_reason == "expired") {
                            // page on-call; peer cert rotation needed
                        } else if (e.sub_reason == "tls_pin_empty_at_open") {
                            // operator-config error per 011 /clarify Q2 — fix Pinset config
                        } else if (e.sub_reason == "sigalg_disallowed") {
                            // cipher / sig-alg policy violation
                        }
                        // ... other sub_reasons: rsa_under_min / ecdsa_curve /
                        //     chain_too_deep / x509_v1 / not_yet_valid / empty_chain ...
                        break;
                    default:
                        // future master-enum tls_* variant — log + ignore
                        break;
                }
            }
        }, ev);
    }
}
```

**Independent test**: `tests/session/test_tls_validation_failed_taxonomy.cpp` fault-injects each of the 6 master-enum cells (`tls_handshake_failed` GROUPING + `tls_rsa_key_too_large` + `tls_cert_der_too_large` + `tls_san_entries_exceeded` + `tls_pin_mismatch` + `tls_load_cancelled`) PLUS at least 3 representative sub_reason cells (`expired`, `tls_pin_empty_at_open`, `sigalg_disallowed`) and verifies the matching `SessionEvent::tls_validation_failed{code, sub_reason, ...}` surfaces. Mitigates `[[feedback_trap_throw_pmr_witness_enumerate_sites]]` — 6+3 representative cells exercised, covering both top-level master-enum cardinality and the `sub_reason` discriminator within the GROUPING.

---

## Scenario D — In-process credential rotation without session restart (US4)

**FRs covered**: FR-030..FR-034 / SC-006

```cpp
#include <fixpp/session/session.hpp>
#include <fixpp/tls/cert_source.hpp>

auto run_quarterly_cert_rotation(fixpp::session::Session& active_session) -> void {
    // 1. The operator's cert-rotation job builds a new cert_source from the
    //    rotated PEM files.
    auto new_source = fixpp::tls::make_file_cert_source(
        /* cert_pem_path = */ "/etc/fixpp/certs/2026-Q3/server.pem",
        /* key_pem_path  = */ "/etc/fixpp/certs/2026-Q3/server.key",
        /* trust_pem_path = */ "/etc/fixpp/certs/2026-Q3/ca.pem");

    // 2. Atomically swap via the operator-facing forwarder. Session::reload_credentials
    //    delegates to TransportFactory::reload_credentials, which performs an
    //    atomic store on the factory-internal
    //    `cert_source_slot_: std::atomic<std::shared_ptr<cert_source>>`.
    //    O(1) under no contention; strand-free. The active session continues
    //    exchanging application messages WITHOUT INTERRUPTION (FR-030 — no
    //    Logout emitted; no TLS teardown).
    auto rotation_result = active_session.reload_credentials(std::move(new_source));
    if (!rotation_result) {
        // nullptr rejected; or other config-level error
        return;
    }

    // 3. Any in-flight handshake (concurrent reconnect on a flaky link) observes
    //    the OLD source per FR-033 / D-11 — the FSM captured a strong-ref
    //    std::shared_ptr<cert_source> BY VALUE COPY via
    //    factory_->cert_source_snapshot() before calling make(...); the captured
    //    copy keeps the OLD cert_source alive past the atomic store. The NEXT
    //    drive_reconnect_attempt reads the NEW snapshot. Worst-case rotation
    //    latency = one in-flight HANDSHAKE duration (~50–500 ms typical for
    //    TLS 1.3 1-RTT) — NOT an in-flight `make()` window.
    //
    // 4. On the first handshake using the rotated source, the operator's
    //    recent_events() poll receives
    //    SessionEvent::credentials_rotated{old_sha256, new_sha256} BEFORE the
    //    handshake's Logon completes (per D-12 / FR-032).
}
```

**Symmetric — acceptor side** (per FR-030 + `[[feedback_half_restructure_symmetric_api]]`):

Both initiator and acceptor rotation route through the SAME `TransportFactory::reload_credentials(...)` call — the factory IS the symmetric authority. The acceptor operator reaches the factory via the listener-built factory handle (the listener consumes the same `SessionConfig::transport_factory_override` field as the initiator), so there is no separate `Listener::reload_credentials` method:

```cpp
auto rotate_acceptor_credentials(fixpp::transport::TransportFactory& factory,
                                 std::shared_ptr<fixpp::tls::cert_source> new_source) -> void {
    auto result = factory.reload_credentials(std::move(new_source));
    // ... same semantics as Session::reload_credentials forwarder ...
    // (The Session::reload_credentials forwarder for the initiator side ALSO
    //  delegates to this exact factory call — eliminating the half-restructure
    //  trap by construction.)
}
```

**Independent test**: `tests/session/test_reload_credentials_in_flight.cpp` drives the in-flight-handshake-defer race deterministically via `mock_clock` + `mock_transport` scripted handshake delay; verifies the in-flight handshake observes OLD (because it captured a strong-ref shared_ptr<cert_source> via the snapshot); next handshake observes NEW; `credentials_rotated` event emits with correct old/new SHA-256.

---

## Scenario E — Developer: FSM-via-mock-transport recovery seam

**FRs covered**: FR-009..FR-016, FR-038 (production-shaped exercise discipline)

```cpp
#include <fixpp/transport/test/mock_transport.hpp>
#include <fixpp/session/session.hpp>
#include <gtest/gtest.h>

TEST(SessionRecovery, AdminSpanCollapsesToGapFill) {
    // 1. Construct a deterministic mock_transport with a scripted byte stream:
    //    - peer-side outbound: messages [10..15] are app, [16..20] are admin
    //      (Heartbeat), [21..25] are app again.
    //    - we issue ResendRequest{10, 25}.
    //    - peer's reply MUST collapse [16..20] to SequenceReset-GapFill{NewSeqNo=21}
    //      per FR-011 / D-3.
    auto mock = fixpp::transport::test::mock_transport::make_with_script({
        // ... scripted peer bytes ...
    });

    // 2. Construct a Session bound to the mock transport. The FSM drives recovery
    //    against the deterministic script; no OpenSSL / ASIO networking linkage.
    fixpp::session::SessionConfig cfg{ /* ... */ };
    auto session = fixpp::session::Session{cfg, /* exec */ };

    // 3. Drive the FSM through Active → AwaitingResend → Active. Assert that:
    //    - we emitted ResendRequest{10, 25}
    //    - peer's reply was processed: GapFill advanced next_expected_inbound to 21
    //      (without storing the synthetic gap-fill body per FR-013)
    //    - application messages 10..15 + 21..25 were dispatched to fromApp
    //      (NOT 16..20 — admin replay forbidden)
    //    - SequenceReset-GapFill body is NOT in MessageStore (FR-013)
    //
    // The assertions must drive bytes through the FSM via mock_transport
    // (production-shaped entry-point exercise per FR-038); SUCCEED()-placeholder
    // tests count as MISSING coverage per `[[project_005_phase8_completeness_false_pass]]`.

    // ... assertions ...
}
```

**Independent tests**: `tests/session/test_recovery_admin_span_gapfill.cpp` + `test_recovery_store_horizon.cpp` + the production-shaped exercise discipline tracked in plan.md §Test Plan.

---

## Anti-pattern guards encoded in the test plan

Per research.md §8:

1. **FR-024 binding-policy symmetry** — `test_compid_binding_symmetry.cpp` exercises initiator AND acceptor halves; invariant-counting witness on `CompIdAuthorizationPolicy::authorize` call site (count = 1 per Logon). Closed-by-construction note: rotation half-restructure is closed by routing both initiator + acceptor through `TransportFactory::reload_credentials` (the factory IS the symmetric authority); binding half-restructure still needs explicit test coverage of both paths.
2. **FR-026 6-master-enum-cells + 3 representative sub_reason cells** — `test_tls_validation_failed_taxonomy.cpp` has 6 master-enum cells (`tls_handshake_failed` GROUPING + `tls_rsa_key_too_large` + `tls_cert_der_too_large` + `tls_san_entries_exceeded` + `tls_pin_mismatch` + `tls_load_cancelled`) + 3 representative sub_reason cells (`expired` peer-cert / `tls_pin_empty_at_open` operator-config / `sigalg_disallowed` cipher-policy) covering both top-level master-enum cardinality and the `sub_reason` discriminator within the `tls_handshake_failed` GROUPING.
3. **FR-033 reload_credentials in-flight defer** — `test_reload_credentials_in_flight.cpp` drives the race deterministically.
4. **FR-022 canonical-fixed principal extraction** — `test_compid_binding_principal_extraction.cpp` has 4 cells (one per `principal_source` enum value).
5. **FR-009 005-FR-008-amendment-in-place** — /speckit-tasks-time task row + completeness-audit cross-check per `[[feedback_simplify_pass_catches_9th_burn]]`.
6. **FR-033 captured-by-value-copy strong-ref** — `[[feedback_weak_ptr_cache_needs_owning_context]]`: the FSM's per-attempt cert_source snapshot via `factory_->cert_source_snapshot()` MUST be `std::shared_ptr<cert_source>` (strong-ref) — NEVER raw `cert_source*`, NEVER `weak_ptr`. Invariant-counting witness on `cert_source::~cert_source()` destructor + concurrent-rotation race test in `test_reload_credentials_in_flight.cpp`.
