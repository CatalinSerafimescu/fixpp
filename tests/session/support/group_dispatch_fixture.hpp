// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/session/support/group_dispatch_fixture.hpp
//
// 066-dict-backed-inbound-parse T001 — shared real-Session-dispatch harness:
// a mock-transport-fed Session, opened with the REAL FIX44 dictionary (via
// tests/support/fix44_dictionary.hpp), drives an inbound frame through
// `Session::on_inbound_frame` -> `parse_and_dispatch_` and delivers the
// resulting `MessageView` to an installable application callback.
//
// This is the SHIPPED-PATH dispatch shape research.md Decision 6 mandates
// (never a `Parser<Index>{dict}` unit parse): every 066 US1/US2 correctness
// witness (T004/T005/T010) drives its frame through THIS fixture (or the
// tests/capi C-ABI engine-loopback counterpart).
//
// Fixture shape mirrors the proven OutboundFixture pattern in
// tests/session/test_application_outbound.cpp /
// test_business_messages_roundtrip.cpp (ioc + mock_clock + EngineConfig +
// SessionConfig + open_to_active + feed) — swapped from the FIX 4.2 minimal
// dictionary to the real FIX44 dictionary so a group-bearing message
// (NoLegs(555) on ExecutionReport(35=8)) actually registers membership.
//
// RED-FIRST PRESERVATION (T001 hard constraint): this header contains NO
// group-membership/extent/trailing-absence assertions — those belong to
// T004/T010 and must be observed RED first, against the unchanged dict-free
// parse. This file only provides the dispatch mechanics + a callback-
// capturing Application.
#pragma once

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>

#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/application.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>

#include "support/fix44_dictionary.hpp"
#include "support/fix44_group_frame_bodies.hpp"
#include "support/minimal_security_profile.hpp"
#include "support/pump_until_ready.hpp"

namespace fixpp::session::test066 {

// Application that forwards fromApp/fromAdmin to installable callbacks — lets
// a witness inspect the delivered MessageView (valid ONLY inside the
// callback window, per the standard Application contract).
class CallbackCapturingApplication : public Application {
public:
    using ViewCallback =
        std::function<void(const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>&)>;

    ViewCallback on_from_app;
    ViewCallback on_from_admin;
    int from_app_calls = 0;
    int from_admin_calls = 0;

    fixpp::core::expected_t<void> fromApp(
        const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& msg,
        const SessionId& /*id*/) override {
        ++from_app_calls;
        if (on_from_app) on_from_app(msg);
        return {};
    }

    fixpp::core::expected_t<void> fromAdmin(
        const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& msg,
        const SessionId& /*id*/) override {
        ++from_admin_calls;
        if (on_from_admin) on_from_admin(msg);
        return {};
    }
};

// FIX44-dict-backed inbound-dispatch fixture. Sender/target/role mirror the
// proven OutboundFixture pattern exactly (default session_role — initiator;
// see test_application_outbound.cpp's open_to_active: open() emits our
// Logon, then the peer's Logon frame fed inbound completes the handshake).
struct GroupDispatchFixture {
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine_cfg;
    std::shared_ptr<CallbackCapturingApplication> app;
    std::vector<std::vector<std::byte>> captured_frames;

    GroupDispatchFixture() {
        using namespace std::chrono;
        auto utc = system_clock::time_point{} + seconds{1704067200};
        auto stp = fixpp::core::steady_time_point{} + seconds{0};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine_cfg.clock = clock;
        engine_cfg.executor = ioc.get_executor();
        app = std::make_shared<CallbackCapturingApplication>();
        engine_cfg.application = app;
    }

    SessionConfig make_cfg() {
        using namespace std::chrono_literals;
        SessionConfig cfg;
        cfg.sender_comp_id = "ISLD";
        cfg.target_comp_id = "TW";
        cfg.begin_string = "FIX.4.4";
        cfg.heartbeat_interval = 0s;  // disable liveness
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = fixpp::test_support::make_fix44_dictionary();
        cfg.executor_override = ioc.get_executor();
        cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
        cfg.transport_send = [this](std::span<const std::byte> frame) {
            captured_frames.emplace_back(frame.begin(), frame.end());
        };
        return cfg;
    }

    // Open the session and complete the handshake: our Logon goes out via
    // open(), then we feed the peer's Logon reply to reach Active.
    void open_to_active(Session& sess) {
        auto fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, std::chrono::milliseconds{200})) {
            fixpp::test_support::cancel_and_drain_or_report(
                ioc, *clock, "GroupDispatchFixture::open_to_active/open");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss
                          << "GroupDispatchFixture::open_to_active/open";
            return;
        }
        ASSERT_TRUE(fut.get().has_value()) << "open() failed";

        auto logon = make_peer_logon_frame();
        auto fut2 = asio::co_spawn(ioc, sess.on_inbound_frame(logon), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut2, std::chrono::milliseconds{200})) {
            fixpp::test_support::cancel_and_drain_or_report(
                ioc, *clock, "GroupDispatchFixture::open_to_active/logon");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss
                          << "GroupDispatchFixture::open_to_active/logon";
            return;
        }
        ASSERT_TRUE(fut2.get().has_value()) << "Logon feed failed";
        ASSERT_EQ(sess.state(), fixpp::session::fsm_state::Active);
    }

    // Feed an inbound frame through the SHIPPED dispatch path
    // (Session::on_inbound_frame -> parse_and_dispatch_).
    void feed(Session& sess, std::span<const std::byte> frame, int ms = 200) {
        auto fut = asio::co_spawn(ioc, sess.on_inbound_frame(frame), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, std::chrono::milliseconds{ms})) {
            fixpp::test_support::cancel_and_drain_or_report(
                ioc, *clock, "GroupDispatchFixture::feed");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss << "GroupDispatchFixture::feed";
            return;
        }
        (void)fut.get();
    }

private:
    // Peer (TW) Logon(35=A), seq=1, replying to our initiator Logon.
    static std::vector<std::byte> make_peer_logon_frame() {
        std::string body =
            "35=A\x01"
            "34=1\x01"
            "49=TW\x01"
            "52=20240101-00:00:00.000\x01"
            "56=ISLD\x01"
            "98=0\x01"
            "108=30\x01";
        return fixpp_test_support::make_frame("FIX.4.4", body);
    }
};

}  // namespace fixpp::session::test066
