// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/capi/capi_loopback_support.hpp — 050 Feature B test-only seams.
//
// Helpers for driving REAL established C-ABI sessions over a plaintext-TCP
// loopback socket end-to-end through the C ABI. The C ABI exposes no dict
// producer (Feature C) and no transport-endpoint setter (delegated to 2j), so
// this header reaches behind the opaque handles through TWO documented seams:
//
//   L-050-1 dictionary seam: construct a fixpp_dict internally over the test
//     minimal dictionary, then pass it through the REAL setter
//     fixpp_session_config_set_dictionary().
//
//   L-050-5 transport-endpoint seam: cast the opaque fixpp_session_config_t* to
//     the internal fixpp_session_config* and set .cfg.reconnect_endpoint (=bind
//     endpoint for the acceptor, peer endpoint for the initiator) + the initial
//     transport_send no-op the engine rebinds at accept. There is NO C-ABI
//     endpoint setter by design ([2i] non-goal #7).
//
// Gold reference for the engine-level plaintext loopback pattern:
//   tests/session/test_session_plaintext_roundtrip.cpp (port-0 OS bind +
//   acceptor_bound_endpoint readback) — adapted here for a TWO-C-ABI-engine
//   pair (both ends through the C ABI).
#pragma once

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include "fix/c_api/engine.h"
#include "fix/c_api/session.h"

#include "capi_internal.hpp"  // fixpp_session_config, fixpp_dict, fixpp_engine internals

#include "fixpp/session/session_config.hpp"  // SessionId::from_config
#include "fixpp/transport/endpoint.hpp"
#include "support/minimal_dictionary.hpp"
#include "support/wait_until.hpp"

namespace fixpp::capi_test {

// A test-owned fixpp_dict handle over the minimal FIX 4.2 dictionary (L-050-1).
// Heap-owned; pass it through the real setter, then destroy it once the setter
// has copied the shared_ptr (the setter does a thin shared_ptr pass-through, so
// the dict outlives the handle via the session config's own copy).
inline fixpp_dict_t* make_test_dict_handle() {
    auto* d = new fixpp_dict{fixpp::test_support::make_minimal_dictionary()};
    return reinterpret_cast<fixpp_dict_t*>(d);
}

inline void destroy_test_dict_handle(fixpp_dict_t* h) {
    delete reinterpret_cast<fixpp_dict*>(h);
}

// L-050-5: set the session config's reconnect_endpoint (the engine repurposes it
// as the acceptor bind endpoint / the initiator peer endpoint) + the initial
// transport_send no-op (the accept loop rebinds the acceptor's send slot to the
// live socket). Must be called BEFORE fixpp_session_open — open copies the
// config by value, so a later mutation would be lost.
inline void set_loopback_endpoint(fixpp_session_config_t* cfg, const char* host,
                                  std::uint16_t port) {
    auto* internal = reinterpret_cast<fixpp_session_config*>(cfg);
    internal->cfg.reconnect_endpoint = fixpp::transport::Endpoint{host, port};
    internal->cfg.transport_send = [](std::span<const std::byte>) {};
    // bilateral_lenient is the proven fresh-pair establishment policy used by
    // every two-engine session test (logon_handshake / logon_received_observability
    // / test_engine_session_strand). The C-ABI config builder exposes no reset-policy
    // setter (none is in [2i]); the default is bilateral_strict, which is not the
    // shape those round-trip witnesses establish under. Set it via the same
    // test-only cast bridge as the endpoint (L-050-5) — NOT a new C-ABI setter.
    internal->cfg.reset_seqnum_policy_field =
        fixpp::session::reset_seqnum_policy::bilateral_lenient;
}

// Reach the C++ Engine through the internal fixpp_engine handle to read the
// acceptor's OS-assigned bound port (port 0 in reconnect_endpoint → OS picks).
// Readable once a worker has run at least one accept-loop step after start.
inline std::uint16_t acceptor_bound_port(fixpp_engine_t* engine,
                                         const fixpp::session::SessionId& id) {
    auto* e = reinterpret_cast<fixpp_engine*>(engine);
    if (e->state_ == nullptr || !e->state_->engine_.has_value()) return 0;
    return e->state_->engine_->acceptor_bound_endpoint(id).port;
}

// Spin until acceptor_bound_port(engine, id) != 0 or the deadline elapses.
// Returns the bound port (0 on timeout). The acceptor engine's own worker drives
// the accept-loop bind, so this is a passive poll on the caller thread.
inline std::uint16_t wait_for_bound_port(
    fixpp_engine_t* engine, const fixpp::session::SessionId& id,
    std::chrono::milliseconds deadline = std::chrono::milliseconds{3000}) {
    // The predicate CAPTURES what it observed, so the port is read exactly once
    // per poll and the read that succeeded is the one returned.
    std::uint16_t p = 0;
    if (!fixpp::test_support::wait_until_observed(
            [&] {
                p = acceptor_bound_port(engine, id);
                return p != 0;
            },
            deadline)) {
        return 0;
    }
    return p;
}

// Poll fixpp_session_is_established(session) until true or the deadline elapses.
// Returns true if it became established. The owning engine's worker drives the
// handshake; this is a passive poll on the caller thread.
inline bool wait_for_established(
    fixpp_session_t* session,
    std::chrono::milliseconds deadline = std::chrono::milliseconds{4000}) {
    return fixpp::test_support::wait_until_observed(
        [&] {
            bool est = false;
            return fixpp_session_is_established(session, &est) == FIXPP_ERR_OK && est;
        },
        deadline);
}

// Build a minimal valid FIX application payload for fixpp_session_send.
//
// CRITICAL contract (verified against src/session/session.cpp Session::send_impl,
// NOT the header comment): fixpp_session_send takes an APPLICATION PAYLOAD, not a
// committed wire frame. The session stamps 8=/9=/34=/49=/52=/56=/10= and assigns
// the in-sequence MsgSeqNum itself. The payload MUST:
//   - lead with "35=<msgtype>\x01" (msgtype non-empty),
//   - end with SOH,
//   - contain NO session header/trailer tags (8/9/34/49/52/56/10) at a field
//     boundary (else app_payload_malformed=131 → FIXPP_ERR_UNKNOWN, no transmit).
// MsgType "D" (NewOrderSingle) is an APPLICATION type (is_admin_msgtype("D")==false)
// so the peer session routes it to Application::fromApp (→ the C receive callback).
inline std::vector<std::uint8_t> make_app_payload(const char* tag11_value) {
    std::string p;
    p += "35=D\x01";                                 // MsgType = NewOrderSingle (app)
    p += std::string("11=") + tag11_value + "\x01";  // ClOrdID (non-session tag)
    p += "55=TESTSYM\x01";                           // Symbol
    p += "54=1\x01";                                 // Side = Buy
    p += "38=100\x01";                               // OrderQty
    p += "40=1\x01";                                 // OrdType = Market
    return std::vector<std::uint8_t>(p.begin(), p.end());
}

// Build an engine-config builder with a real-time clock (Engine::start rejects a
// null clock). Returns the opaque builder; CONSUMED by fixpp_engine_create.
inline fixpp_engine_config_t* make_engine_cfg() {
    fixpp_engine_config_t* ec = nullptr;
    EXPECT_EQ(fixpp_engine_config_create(&ec), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_engine_config_set_realtime_clock(ec), FIXPP_ERR_OK);
    return ec;
}

// Build a plaintext session-config builder through the real setters + the dict
// seam. Endpoint is set separately via set_loopback_endpoint (L-050-5).
inline fixpp_session_config_t* make_session_cfg(const char* sender, const char* target,
                                                fixpp_session_role role) {
    fixpp_session_config_t* sc = nullptr;
    EXPECT_EQ(fixpp_session_config_create(&sc), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_comp_ids(sc, sender, target), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_begin_string(sc, "FIX.4.2"), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_role(sc, role), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_heartbeat_seconds(sc, 30), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_security(sc, FIXPP_SECURITY_INSECURE_PLAIN_TCP, nullptr,
                                                nullptr),
              FIXPP_ERR_OK);
    // Initiator resets to seq 1 on logon so a fresh pair logs on cleanly.
    EXPECT_EQ(fixpp_session_config_set_reset_on_logon(sc, role == FIXPP_ROLE_INITIATOR),
              FIXPP_ERR_OK);
    fixpp_dict_t* d = make_test_dict_handle();
    EXPECT_EQ(fixpp_session_config_set_dictionary(sc, d), FIXPP_ERR_OK);
    destroy_test_dict_handle(d);  // setter copied the shared_ptr
    return sc;
}

// Derive the SessionId the engine will key the session by (needed to read the
// acceptor's bound port back). Mirrors what fixpp_session_open does internally.
inline fixpp::session::SessionId session_id_of(fixpp_session_config_t* cfg) {
    auto* internal = reinterpret_cast<fixpp_session_config*>(cfg);
    return fixpp::session::SessionId::from_config(internal->cfg);
}

}  // namespace fixpp::capi_test
