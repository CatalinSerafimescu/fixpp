// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 fixpp contributors
//
// tests/session/test_session_plaintext_authz.cpp — T008 [P] [US1]
//
// SC-004 (FR-008a, D-10) on the plaintext surface (5/6 cells; see Cell 4 note below):
//
//   Cell 1 (authorize NOT called — composite witness):
//     On insecure_plain_tcp, compid_authorization_policy.authorize(peer_id, compid)
//     is NEVER called — the mTLS gate (is_mtls=false) AND the D-10 guard
//     (live_peer_id_ == nullopt) together suppress the entire authorize() arm.
//     Witnessed via recent_events(): NEITHER session_event_peer_identity_bound NOR
//     session_event_compid_authorization_failed appears after the Logon exchange.
//     This cell is a COMPOSITE witness: Cell 2a independently tests the D-10 guard;
//     Cell 1b independently tests the is_mtls gate (positive control). Flipping Cell 1
//     alone requires TWO simultaneous mutations (remove D-10 guard AND remove is_mtls
//     check) — any SINGLE mutation is caught by Cell 2a or Cell 1b independently.
//     [SC-004; D-10; session.cpp:2207 is_mtls gate; session.cpp:~555 D-10 guard]
//
//   Cell 1b (TLS positive control: authorize IS called on mTLS):
//     On mtls_ca + live peer_id, authorize() succeeds → peer_identity_bound event.
//     Proves the observable (event presence) is real on the TLS path.
//     [SC-004; session.cpp:2207 is_mtls gate]
//
//   Cell 2a (live_peer_id_ nullopt — accepted handoff):
//     attach_accepted_transport called with a NON-EMPTY sentinel handshake_result
//     (sentinel CN="SENTINEL-MUST-NOT-STICK") on an insecure_plain_tcp session →
//     live_peer_id_has_value_for_test() == false. The D-10 #3 guard must suppress
//     assignment. Single-mutation discriminating: drop the guard → sentinel sticks
//     → live_peer_id_has_value_for_test()==true → FAIL (RED). [D-10; session.cpp:~555]
//
//   Cell 2b (TLS positive control: sentinel DOES stick on one_way_ca):
//     Same sentinel on a one_way_ca session → live_peer_id_has_value_for_test()==true.
//     Proves the assignment path is real; the plaintext guard suppresses it.
//
//   Cell 3 (check_comp_id preserved):
//     A mismatched inbound SenderCompID(49) on a plaintext session → session
//     disconnects (Logon rejected). The cert-independent CompID check is unaffected
//     by the plaintext profile.
//
// Note (Cell 4 — SC-004 FR-009 scope): The 043 spec says EncryptMethod(98)≠0 is
// "unchanged" on plaintext — but the production code has NEVER rejected EncryptMethod≠0
// inbound (S-021 feature-catalogue.md: "Inbound 98≠0 not handled — reserved values per
// spec; TLS is transport-layer"). 043 made no production change to this gap; the
// "unchanged" claim in spec.md FR-009 is an overstatement. Cell 4 (EncryptMethod≠0)
// is OMITTED because the production code does not implement the rejection, and the
// brief prohibits adding new production behavior. [escalation for orchestrator: spec
// S-021 vs 043 tasks.md T008 FR-009 cell — pre-existing gap, not a 043 regression;
// implementing the reject would require production code change + Gate A reconsideration]
//
// No GTEST_SKIP() — plaintext sessions need no cert files.
//
// Anchors: spec.md SC-004 / FR-008a / FR-009; research.md D-10; tasks.md T008;
//          session.cpp:2207 (is_mtls gate) / :~555 (D-10 guard);
//          session_event.hpp (session_event_peer_identity_bound /
//          session_event_compid_authorization_failed).

// FIXPP_TEST_HOOKS is defined via CMakeLists.txt compile definition — exposes
// live_peer_id_has_value_for_test() in session.hpp.
#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/redirect_error.hpp>
#include <asio/steady_timer.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/fix_time.hpp>
#include <fixpp/core/system_clock_source.hpp>
#include <fixpp/session/compid_authorization_policy.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_event.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fixpp/tls/peer_identity.hpp>
#include <fixpp/transport/tls_transport.hpp>
#include <fixpp/transport/transport.hpp>
#include <fixpp/transport/transport_factory.hpp>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "support/minimal_dictionary.hpp"

// SecurityProfile::kind::insecure_plain_tcp — [[deprecated]] friction not yet
// added (T019 is a future task); no pragma suppression needed.
#include <fixpp/session/security_profile.hpp>

using namespace std::chrono_literals;

namespace {

// Current wall-clock UTC as a FIX UTCTimestamp "YYYYMMDD-HH:MM:SS.mmm".
// Required by the 038 acceptor first-Logon SendingTime(52) MaxLatency guard.
static std::string utc_now_fix_timestamp() {
    std::array<char, 32> buf{};
    auto r = fixpp::core::utc_time_to_fix_string(std::chrono::system_clock::now(),
                                                  fixpp::core::fix_time_precision::millis,
                                                  std::span<char>{buf});
    return r ? std::string{r->data(), r->size()} : std::string{};
}

// Build a FIX Logon frame as bytes.
static std::vector<std::byte> make_logon_bytes(std::string_view begin_str,
                                                std::string_view sender,
                                                std::string_view target,
                                                int seq = 1) {
    auto field = [](int tag, std::string_view v) -> std::string {
        return std::to_string(tag) + "=" + std::string(v) + "\x01";
    };
    std::string body;
    body += field(35, "A");
    body += field(34, std::to_string(seq));
    body += field(49, sender);
    body += field(52, utc_now_fix_timestamp());
    body += field(56, target);
    body += field(98, "0");   // EncryptMethod=0 (None — per FR-009)
    body += field(108, "30");

    std::string msg;
    msg += "8=" + std::string(begin_str) + "\x01";
    msg += "9=" + std::to_string(body.size()) + "\x01";
    msg += body;
    unsigned int cs = 0;
    for (unsigned char c : msg) cs += c;
    cs &= 0xFFu;
    char csbuf[5];
    snprintf(csbuf, sizeof(csbuf), "%03u", cs);
    msg += "10=" + std::string(csbuf) + "\x01";

    std::vector<std::byte> out;
    out.reserve(msg.size());
    for (char c : msg) out.push_back(static_cast<std::byte>(c));
    return out;
}

// ── A minimal transport that captures writes + blocks reads ──────────────────
// Used so Session::attach_accepted_transport has a valid transport to work with.
// Reads never complete (the session never gets a peer EOF); close() unblocks.
class BlockingTransport final : public fixpp::transport::Transport {
public:
    explicit BlockingTransport(asio::any_io_executor exec)
        : exec_{std::move(exec)}, timer_{exec_} {}

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<fixpp::transport::ConnectInfo>>
    async_connect(fixpp::transport::Endpoint const&) noexcept override {
        co_return fixpp::transport::ConnectInfo{};
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<std::size_t>> async_read_some(
        std::span<std::byte> /*buf*/) noexcept override {
        // Block until cancelled.
        asio::error_code ec;
        timer_.expires_after(std::chrono::hours{24});
        co_await timer_.async_wait(asio::redirect_error(asio::use_awaitable, ec));
        co_return std::unexpected{fixpp::core::error::transport_read_cancelled};
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<std::size_t>> async_write(
        std::span<const std::byte> data) noexcept override {
        co_return data.size();
    }

    [[nodiscard]] fixpp::core::expected_t<void> cancel() noexcept override {
        timer_.cancel();
        return {};
    }

    [[nodiscard]] fixpp::core::expected_t<void> close() noexcept override {
        timer_.cancel();
        return {};
    }

private:
    asio::any_io_executor exec_;
    asio::steady_timer timer_;
};

// ── Build a base acceptor SessionConfig ──────────────────────────────────────
static fixpp::session::SessionConfig make_acceptor_cfg(
    asio::any_io_executor exec,
    fixpp::session::SecurityProfile::kind profile_kind,
    fixpp::session::CompIdAuthorizationPolicy authz = {}) {
    fixpp::session::SessionConfig cfg;
    cfg.sender_comp_id = "ACCEPTOR";
    cfg.target_comp_id = "INITIATOR";
    cfg.begin_string = "FIX.4.2";
    cfg.role = fixpp::session::session_role::acceptor;
    cfg.executor_override = exec;
    cfg.security_profile = fixpp::session::SecurityProfile{profile_kind};
    cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
    cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
    cfg.heartbeat_interval = 0s;  // disable liveness loop
    cfg.logout_disconnect_timeout_ms = 500;
    cfg.compid_authorization_policy = std::move(authz);
    cfg.transport_send = [](std::span<const std::byte>) {};
    return cfg;
}

// Build a sentinel handshake_result with a non-empty subject_dn.
// The sentinel CN="SENTINEL-MUST-NOT-STICK" is distinct from the empty default;
// if the D-10 guard is absent, the sentinel sticks in live_peer_id_ → assertion FAILS.
// [advisor: sentinel must be non-empty to discriminate — empty hr{} self-heals]
static fixpp::transport::handshake_result make_sentinel_hr() {
    fixpp::transport::handshake_result hr{};
    hr.peer_id.subject_dn = "CN=SENTINEL-MUST-NOT-STICK";
    return hr;
}

// Helper: does recent_events() contain any event of type T?
template <typename T>
static bool has_event(const fixpp::session::Session& sess) {
    auto events = sess.recent_events();
    return std::ranges::any_of(events, [](const auto& ev) {
        return std::holds_alternative<T>(ev);
    });
}

}  // namespace

// ── Cell 1 — authorize() NOT called on plaintext (composite witness) ──────────
//
// Scenario: plaintext acceptor session completes a Logon exchange.
// Observable: recent_events() contains NEITHER session_event_peer_identity_bound
// (emitted on authorize() SUCCESS) NOR session_event_compid_authorization_failed
// (emitted on authorize() FAILURE or fail-closed). Both are emitted exclusively
// through the authorize() call path.
//
// Discrimination: this cell is a COMPOSITE witness. Flipping it alone requires
// TWO simultaneous mutations:
//   (a) Remove D-10 guard in attach_accepted_transport (session.cpp:~555), AND
//   (b) Remove the `&& is_mtls` check (session.cpp:2207).
//   Cell 2a catches (a) independently; Cell 1b catches (b) as a positive control.
// Any single mutation is discriminated by one of the sibling cells.
//
// Anchors: SC-004; D-10; session.cpp:2207 (is_mtls gate); session.cpp:~555 (D-10 guard)

TEST(PlaintextAuthzTest, AuthorizeNotCalledOnPlaintext) {
    asio::io_context ioc;
    fixpp::core::EngineConfig eng;
    eng.executor = ioc.get_executor();
    eng.clock = std::make_shared<fixpp::core::system_clock_source>(ioc.get_executor());

    // Policy with a binding that WOULD authorize "SENTINEL-MUST-NOT-STICK" → "INITIATOR".
    // If authorize() were called (regression), the call would either succeed (emitting
    // peer_identity_bound) or fail (emitting compid_authorization_failed). Both indicate
    // an authorize() invocation that should NOT have happened on plaintext.
    fixpp::session::CompIdAuthorizationPolicy authz;
    authz.add_binding("SENTINEL-MUST-NOT-STICK", "INITIATOR");

    auto cfg = make_acceptor_cfg(ioc.get_executor(),
                                  fixpp::session::SecurityProfile::kind::insecure_plain_tcp,
                                  authz);

    fixpp::session::Session sess{eng, cfg};
    auto open_fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
    ioc.run();
    ASSERT_TRUE(open_fut.get().has_value()) << "open() failed for insecure_plain_tcp session";

    // Attach with an EMPTY handshake_result (D-10: plaintext path → empty hr).
    auto raw_transport = std::make_unique<BlockingTransport>(ioc.get_executor());
    fixpp::transport::handshake_result empty_hr{};
    sess.attach_accepted_transport(std::move(raw_transport), std::move(empty_hr));

    // Feed a valid Logon — sender=INITIATOR, target=ACCEPTOR.
    auto logon = make_logon_bytes("FIX.4.2", "INITIATOR", "ACCEPTOR");
    {
        auto feed_fut = asio::co_spawn(
            ioc, sess.on_inbound_frame(std::span<const std::byte>{logon}), asio::use_future);
        ioc.restart();
        ioc.run();  // on_inbound_frame + liveness loop (exits immediately for heartbt=0) complete
        (void)feed_fut.get();
    }

    // SC-004 / D-10: authorize() must NOT have been called. No peer_identity_bound
    // AND no compid_authorization_failed in recent_events() — both are exclusively
    // emitted through the authorize() code path (session.cpp:2207).
    EXPECT_FALSE(
        has_event<fixpp::session::session_event_peer_identity_bound>(sess))
        << "Cell 1 (SC-004): no session_event_peer_identity_bound expected on "
           "insecure_plain_tcp — this event is emitted only on authorize() success. "
           "Composite mutation: remove D-10 guard (cell 2a catches this) AND remove "
           "the is_mtls check (cell 1b positive control catches this independently).";
    EXPECT_FALSE(
        has_event<fixpp::session::session_event_compid_authorization_failed>(sess))
        << "Cell 1 (SC-004): no session_event_compid_authorization_failed expected on "
           "insecure_plain_tcp — this event is emitted only on authorize() invocation "
           "(failure path or fail-closed mTLS+no-peer-id arm).";

    // Clean up (graceful close; runs Logout grace period of logout_disconnect_timeout_ms=500ms).
    {
        auto close_fut = asio::co_spawn(ioc, sess.close(), asio::use_future);
        ioc.restart();
        ioc.run();
        (void)close_fut.get();
    }
}

// ── Cell 1b — TLS positive control: authorize() IS called on mTLS ────────────
//
// Same scenario on a mtls_ca session with a live peer_id set via attach_accepted_transport.
// Proves the event-based observable actually fires on the TLS path — a plaintext "no
// event" is meaningful only if we know the event CAN fire on mTLS.
//
// After authorize() succeeds, session_event_peer_identity_bound is emitted.

TEST(PlaintextAuthzTest, AuthorizeCalledOnMtlsPositiveControl) {
    asio::io_context ioc;
    fixpp::core::EngineConfig eng;
    eng.executor = ioc.get_executor();
    eng.clock = std::make_shared<fixpp::core::system_clock_source>(ioc.get_executor());

    // Policy: allow "SENTINEL-MUST-NOT-STICK" → "INITIATOR".
    fixpp::session::CompIdAuthorizationPolicy authz;
    authz.add_binding("SENTINEL-MUST-NOT-STICK", "INITIATOR");

    auto cfg = make_acceptor_cfg(ioc.get_executor(),
                                  fixpp::session::SecurityProfile::kind::mtls_ca,
                                  authz);

    fixpp::session::Session sess{eng, cfg};
    auto open_fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
    ioc.run();
    ASSERT_TRUE(open_fut.get().has_value()) << "open() failed for mtls_ca session";

    // Attach with the sentinel — on mtls_ca, D-10 guard does NOT suppress it.
    auto raw_transport = std::make_unique<BlockingTransport>(ioc.get_executor());
    auto sentinel_hr = make_sentinel_hr();
    sess.attach_accepted_transport(std::move(raw_transport), std::move(sentinel_hr));

    // Feed a valid Logon — triggers the is_mtls path → authorize() called.
    auto logon = make_logon_bytes("FIX.4.2", "INITIATOR", "ACCEPTOR");
    {
        auto feed_fut = asio::co_spawn(
            ioc, sess.on_inbound_frame(std::span<const std::byte>{logon}), asio::use_future);
        ioc.restart();
        ioc.run();
        (void)feed_fut.get();
    }

    // Positive control: peer_identity_bound must be present (authorize() succeeded).
    EXPECT_TRUE(
        has_event<fixpp::session::session_event_peer_identity_bound>(sess))
        << "Cell 1b (positive control): on mtls_ca with a live peer_id matching the policy, "
           "session_event_peer_identity_bound must be emitted after Logon. "
           "This validates that the event-based observable used in Cell 1 is meaningful.";

    {
        auto close_fut = asio::co_spawn(ioc, sess.close(), asio::use_future);
        ioc.restart();
        ioc.run();
        (void)close_fut.get();
    }
}

// ── Cell 2a — live_peer_id_ == nullopt on accepted handoff (insecure_plain_tcp)
//
// Sentinel handshake_result (non-empty CN) passed to attach_accepted_transport on
// insecure_plain_tcp. The D-10 #3 guard must suppress assignment.
//
// Single-mutation discriminating:
//   Mutation: drop `if (k != insecure_plain_tcp)` in session.cpp:~555
//             → sentinel sticks → live_peer_id_has_value_for_test()==true → FAIL (RED).

TEST(PlaintextAuthzTest, LivePeerIdNulloptOnAcceptedHandoff) {
    asio::io_context ioc;
    fixpp::core::EngineConfig eng;
    eng.executor = ioc.get_executor();
    eng.clock = std::make_shared<fixpp::core::system_clock_source>(ioc.get_executor());

    auto cfg = make_acceptor_cfg(ioc.get_executor(),
                                  fixpp::session::SecurityProfile::kind::insecure_plain_tcp);

    fixpp::session::Session sess{eng, cfg};
    auto open_fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
    ioc.run();
    ASSERT_TRUE(open_fut.get().has_value()) << "open() failed";

    // Pre-check: live_peer_id_ must already be nullopt before attach.
    ASSERT_FALSE(sess.live_peer_id_has_value_for_test())
        << "live_peer_id_ should be nullopt before attach_accepted_transport";

    // Pass a SENTINEL (non-empty CN) to attach_accepted_transport.
    // On insecure_plain_tcp the guard MUST suppress the assignment.
    auto raw_transport = std::make_unique<BlockingTransport>(ioc.get_executor());
    auto sentinel_hr = make_sentinel_hr();
    sess.attach_accepted_transport(std::move(raw_transport), std::move(sentinel_hr));

    // D-10 MUST: live_peer_id_ must stay nullopt — guard must have suppressed assignment.
    EXPECT_FALSE(sess.live_peer_id_has_value_for_test())
        << "Cell 2a (D-10 MUST): attach_accepted_transport on insecure_plain_tcp must "
           "leave live_peer_id_ == nullopt even when called with a non-empty sentinel "
           "handshake_result. Guard in session.cpp:~555 must suppress the assignment. "
           "Mutation: drop the guard → sentinel sticks → has_value()==true → RED.";

    {
        auto close_fut = asio::co_spawn(ioc, sess.close(), asio::use_future);
        ioc.restart();
        ioc.run();
        (void)close_fut.get();
    }
}

// ── Cell 2b — TLS positive control: sentinel DOES stick on one_way_ca ────────
//
// Same sentinel on a one_way_ca session → live_peer_id_has_value_for_test()==true.
// Proves the assignment path is real; the plaintext guard is what suppresses it.
// one_way_ca skips mTLS authorize() (is_mtls=false) but DOES accept the peer_id.

TEST(PlaintextAuthzTest, LivePeerIdSetOnTlsPositiveControl) {
    asio::io_context ioc;
    fixpp::core::EngineConfig eng;
    eng.executor = ioc.get_executor();
    eng.clock = std::make_shared<fixpp::core::system_clock_source>(ioc.get_executor());

    auto cfg = make_acceptor_cfg(ioc.get_executor(),
                                  fixpp::session::SecurityProfile::kind::one_way_ca);

    fixpp::session::Session sess{eng, cfg};
    auto open_fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
    ioc.run();
    ASSERT_TRUE(open_fut.get().has_value()) << "open() failed for one_way_ca";

    // Pass the sentinel to a TLS session — must NOT be suppressed.
    auto raw_transport = std::make_unique<BlockingTransport>(ioc.get_executor());
    auto sentinel_hr = make_sentinel_hr();
    sess.attach_accepted_transport(std::move(raw_transport), std::move(sentinel_hr));

    // Positive control: sentinel must have been assigned.
    EXPECT_TRUE(sess.live_peer_id_has_value_for_test())
        << "Cell 2b (positive control): attach_accepted_transport on one_way_ca must "
           "store the sentinel in live_peer_id_ (has_value()==true). "
           "This validates that the assignment path is real; the plaintext guard "
           "in Cell 2a is what suppresses it.";

    {
        auto close_fut = asio::co_spawn(ioc, sess.close(), asio::use_future);
        ioc.restart();
        ioc.run();
        (void)close_fut.get();
    }
}

// ── Cell 3 — check_comp_id rejects mismatched inbound CompID on plaintext ─────
//
// cert-independent check_comp_id must still reject a Logon whose SenderCompID(49)
// doesn't match cfg_.target_comp_id. This check is INDEPENDENT of mTLS/cert identity.

TEST(PlaintextAuthzTest, CheckCompIdRejectsMismatchOnPlaintext) {
    asio::io_context ioc;
    fixpp::core::EngineConfig eng;
    eng.executor = ioc.get_executor();
    eng.clock = std::make_shared<fixpp::core::system_clock_source>(ioc.get_executor());

    // cfg_.target_comp_id = "INITIATOR" — peer must send SenderCompID(49)="INITIATOR"
    auto cfg = make_acceptor_cfg(ioc.get_executor(),
                                  fixpp::session::SecurityProfile::kind::insecure_plain_tcp);

    fixpp::session::Session sess{eng, cfg};
    auto open_fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
    ioc.run();
    ASSERT_TRUE(open_fut.get().has_value()) << "open() failed";

    auto raw_transport = std::make_unique<BlockingTransport>(ioc.get_executor());
    fixpp::transport::handshake_result empty_hr{};
    sess.attach_accepted_transport(std::move(raw_transport), std::move(empty_hr));

    // Feed a Logon with WRONG SenderCompID(49)="WRONG-SENDER" (not "INITIATOR").
    auto bad_logon = make_logon_bytes("FIX.4.2", "WRONG-SENDER", "ACCEPTOR");
    {
        auto feed_fut = asio::co_spawn(
            ioc, sess.on_inbound_frame(std::span<const std::byte>{bad_logon}), asio::use_future);
        ioc.restart();
        ioc.run();
        (void)feed_fut.get();
    }

    // check_comp_id must have rejected the mismatched CompID → Disconnected.
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Disconnected)
        << "Cell 3 (SC-004): check_comp_id must reject a mismatched inbound SenderCompID(49) "
           "even on insecure_plain_tcp. The cert-independent CompID check is unaffected "
           "by the plaintext profile. Expected Disconnected, got state="
        << static_cast<int>(sess.state());
    // Session is Disconnected — close() returns session_already_closed (fast path).
    {
        auto close_fut = asio::co_spawn(ioc, sess.close(), asio::use_future);
        ioc.restart();
        ioc.run();
        (void)close_fut.get();
    }
}
