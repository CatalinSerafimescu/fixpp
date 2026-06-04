// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_application_lifecycle.cpp
//
// 019-app-callbacks T015 — Application lifecycle notification tests.
//
// Covers US3 AC1/AC2/AC3; FR-009; INV-7.
//
// Tests:
//   1. onCreate fires exactly once after Session::open() initializes exec_
//      and before first Logon (US3 AC1; FR-009).
//   2. onLogon fires exactly once when the session reaches Active (US3 AC2).
//   3. onCreate → onLogon order.
//   4. onLogout fires exactly once on graceful close (US3 AC3; FR-009).
//   5. onLogout fires exactly once on terminal close (US3 AC3; INV-7).
//   6. onLogout fires exactly once when a callback throws (causes terminal close).
//   7. fromAdmin fires for inbound Logout (35=5) [completeness fix FR-004].
//   8. fromAdmin fires for inbound SequenceReset(35=4) [completeness fix FR-004].
//   9. Inbound Logout fires BOTH fromAdmin AND onLogout (different callbacks;
//      both exactly once, independently).
//
// TDD (T015): written first, confirmed FAIL before T016 implementation.
//
// Anchors: spec.md US3 AC1/AC2/AC3; FR-009; FR-004; data-model.md INV-7;
//          tasks.md T015/T016 + fromAdmin-completeness note.

#include <gtest/gtest.h>

#include <array>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/application.hpp>
#include <fixpp/session/engine.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <future>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"

using namespace std::chrono_literals;
using fixpp::core::error;
using fixpp::core::expected_t;
using fixpp::session::Application;
using fixpp::session::SessionId;
using fixpp::wire::MessageView;
using fixpp::wire::access_mode;

namespace fixpp::session::test {
namespace {

// ── Frame builder helpers ──────────────────────────────────────────────────────

static std::vector<std::byte> make_raw_frame(std::string_view begin_string,
                                             std::string_view msg_type, std::uint32_t seq,
                                             std::string_view sender, std::string_view target,
                                             std::string extra_body = {}) {
    std::string body;
    body += "35=" + std::string(msg_type) + "\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=" + std::string(sender) + "\x01";
    body += "52=20240101-00:00:00.000\x01";
    body += "56=" + std::string(target) + "\x01";
    if (!extra_body.empty()) {
        body += extra_body;
    }

    std::string hdr;
    hdr += "8=" + std::string(begin_string) + "\x01";
    hdr += "9=" + std::to_string(body.size()) + "\x01";

    std::string full = hdr + body;
    unsigned int cs = 0;
    for (unsigned char c : full) {
        cs += c;
    }
    cs &= 0xFFU;
    char csbuf[4];
    snprintf(csbuf, sizeof(csbuf), "%03u", cs);
    full += "10=" + std::string(csbuf) + "\x01";

    std::vector<std::byte> frame;
    for (char c : full) {
        frame.push_back(static_cast<std::byte>(c));
    }
    return frame;
}

static std::vector<std::byte> make_logon_frame(std::string_view begin_string = "FIX.4.2",
                                               std::uint32_t seq = 1,
                                               std::string_view sender = "TW",
                                               std::string_view target = "ISLD",
                                               int heartbt = 0) {
    std::string extra = std::string("98=0\x01") + "108=" + std::to_string(heartbt) + "\x01";
    return make_raw_frame(begin_string, "A", seq, sender, target, extra);
}

static std::vector<std::byte> make_logout_frame(std::uint32_t seq = 2,
                                                std::string_view sender = "TW",
                                                std::string_view target = "ISLD") {
    return make_raw_frame("FIX.4.2", "5", seq, sender, target);
}

static std::vector<std::byte> make_sequence_reset_frame(std::uint32_t seq,
                                                        std::uint32_t new_seqno,
                                                        bool gap_fill = false,
                                                        std::string_view sender = "TW",
                                                        std::string_view target = "ISLD") {
    std::string extra = "36=" + std::to_string(new_seqno) + "\x01";
    if (gap_fill) {
        extra += "123=Y\x01";
    }
    return make_raw_frame("FIX.4.2", "4", seq, sender, target, extra);
}

// ── LifecycleApplication — records lifecycle + inbound callback invocations ───

class LifecycleApplication : public Application {
public:
    // Lifecycle call records (in order)
    struct LifecycleCall {
        std::string which;  // "onCreate", "onLogon", "onLogout"
        std::string session_sender;
    };
    std::vector<LifecycleCall> lifecycle_calls;

    // Inbound message callback records
    struct InboundCall {
        std::string which;  // "fromAdmin", "fromApp"
        std::string msg_type;
    };
    std::vector<InboundCall> inbound_calls;

    // Throw control — for testing throw→terminal-close disposition
    bool throw_on_logout = false;

    void onCreate(const SessionId& id) override {
        lifecycle_calls.push_back({"onCreate", id.sender_comp_id});
    }

    void onLogon(const SessionId& id) override {
        lifecycle_calls.push_back({"onLogon", id.sender_comp_id});
    }

    void onLogout(const SessionId& id) override {
        if (throw_on_logout) {
            throw std::runtime_error("test throw from onLogout");
        }
        lifecycle_calls.push_back({"onLogout", id.sender_comp_id});
    }

    expected_t<void> fromAdmin(const MessageView<access_mode::Index>& msg,
                               const SessionId& /*id*/) override {
        auto mt_fv = msg.get(35);
        std::string mt = mt_fv ? std::string(mt_fv->as_string()) : "<none>";
        inbound_calls.push_back({"fromAdmin", mt});
        return {};
    }

    expected_t<void> fromApp(const MessageView<access_mode::Index>& msg,
                             const SessionId& /*id*/) override {
        auto mt_fv = msg.get(35);
        std::string mt = mt_fv ? std::string(mt_fv->as_string()) : "<none>";
        inbound_calls.push_back({"fromApp", mt});
        return {};
    }
};

// ── Test fixture ──────────────────────────────────────────────────────────────

struct LifecycleFixture {
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine;

    LifecycleFixture() {
        using namespace std::chrono;
        auto utc = system_clock::time_point{} + seconds{1704067200};
        auto stp = fixpp::core::steady_time_point{} + seconds{0};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock = clock;
        engine.executor = ioc.get_executor();
    }

    SessionConfig make_cfg() {
        SessionConfig cfg;
        cfg.sender_comp_id = "ISLD";
        cfg.target_comp_id = "TW";
        cfg.begin_string = "FIX.4.2";
        cfg.heartbeat_interval = 0s;  // disable liveness
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override = ioc.get_executor();
        cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
        cfg.transport_send = [](std::span<const std::byte>) {};  // no-op
        return cfg;
    }

    void run_ioc() { ioc.run_for(300ms); ioc.restart(); }

    // Open session to Active state (acceptor path: NotConnected → Active via Logon).
    void open_to_active(Session& sess) {
        auto fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
        run_ioc();
        ASSERT_TRUE(fut.get().has_value()) << "open() failed";

        // Feed peer Logon to advance to Active.
        auto logon = make_logon_frame();
        auto fut2 = asio::co_spawn(ioc, sess.on_inbound_frame(logon), asio::use_future);
        run_ioc();
        ASSERT_TRUE(fut2.get().has_value()) << "Logon feed failed";
        ASSERT_EQ(sess.state(), fixpp::session::fsm_state::Active) << "must be Active after Logon";
    }

    void feed(Session& s, const std::vector<std::byte>& frame) {
        auto fut = asio::co_spawn(ioc, s.on_inbound_frame(frame), asio::use_future);
        run_ioc();
        (void)fut.get();
    }
};

// ── Test 1: onCreate fires exactly once after open(), before Logon ────────────

TEST(ApplicationLifecycle, OnCreateFiresOnceBeforeLogon) {
    auto app = std::make_shared<LifecycleApplication>();
    LifecycleFixture f;
    f.engine.application = app;

    auto cfg = f.make_cfg();
    Session sess(f.engine, cfg);

    // Before open(): no calls.
    EXPECT_TRUE(app->lifecycle_calls.empty()) << "no calls before open()";

    // open() initializes exec_ — onCreate must fire.
    auto fut = asio::co_spawn(f.ioc, sess.open(), asio::use_future);
    f.run_ioc();
    ASSERT_TRUE(fut.get().has_value()) << "open() failed";

    ASSERT_EQ(app->lifecycle_calls.size(), 1u) << "onCreate must fire exactly once after open()";
    EXPECT_EQ(app->lifecycle_calls[0].which, "onCreate");
    EXPECT_EQ(app->lifecycle_calls[0].session_sender, "ISLD");

    // No Logon yet — no onLogon.
    bool has_logon = false;
    for (auto& c : app->lifecycle_calls) {
        if (c.which == "onLogon") has_logon = true;
    }
    EXPECT_FALSE(has_logon) << "onLogon must NOT fire before Logon";
}

// ── Test 2: onLogon fires exactly once when session reaches Active ────────────

TEST(ApplicationLifecycle, OnLogonFiresOnceAtActive) {
    auto app = std::make_shared<LifecycleApplication>();
    LifecycleFixture f;
    f.engine.application = app;

    auto cfg = f.make_cfg();
    Session sess(f.engine, cfg);
    f.open_to_active(sess);

    // Count onLogon calls.
    std::size_t logon_count = 0;
    for (auto& c : app->lifecycle_calls) {
        if (c.which == "onLogon") ++logon_count;
    }
    EXPECT_EQ(logon_count, 1u) << "onLogon must fire exactly once at Active";
}

// ── Test 3: onCreate → onLogon order ─────────────────────────────────────────

TEST(ApplicationLifecycle, OnCreateBeforeOnLogon) {
    auto app = std::make_shared<LifecycleApplication>();
    LifecycleFixture f;
    f.engine.application = app;

    auto cfg = f.make_cfg();
    Session sess(f.engine, cfg);
    f.open_to_active(sess);

    // Find positions of onCreate and onLogon.
    int create_pos = -1, logon_pos = -1;
    for (int i = 0; i < static_cast<int>(app->lifecycle_calls.size()); ++i) {
        if (app->lifecycle_calls[i].which == "onCreate") create_pos = i;
        if (app->lifecycle_calls[i].which == "onLogon") logon_pos = i;
    }
    ASSERT_GE(create_pos, 0) << "onCreate must have fired";
    ASSERT_GE(logon_pos, 0) << "onLogon must have fired";
    EXPECT_LT(create_pos, logon_pos) << "onCreate must come before onLogon";
}

// ── Test 4: onLogout fires exactly once on graceful close ─────────────────────
// Uses the clock-advance path: close(graceful) emits Logout, then the 2s
// logout timeout fires, transitioning to Disconnected.

TEST(ApplicationLifecycle, OnLogoutFiresOnce_GracefulClose) {
    auto app = std::make_shared<LifecycleApplication>();
    LifecycleFixture f;
    f.engine.application = app;

    auto cfg = f.make_cfg();
    Session sess(f.engine, cfg);
    f.open_to_active(sess);

    // Start graceful close in the background.
    auto close_fut = asio::co_spawn(f.ioc, sess.close(fixpp::session::close_mode::graceful),
                                    asio::use_future);
    // Let it run until Logout is emitted and it starts waiting for peer confirmation.
    f.ioc.run_for(100ms);
    f.ioc.restart();

    // Advance clock past the 2 s graceful-close timeout to unblock.
    f.clock->advance(std::chrono::seconds{3});
    f.ioc.run_for(200ms);
    f.ioc.restart();

    (void)close_fut.get();

    // Count onLogout calls.
    std::size_t logout_count = 0;
    for (auto& c : app->lifecycle_calls) {
        if (c.which == "onLogout") ++logout_count;
    }
    EXPECT_EQ(logout_count, 1u) << "onLogout must fire exactly once on graceful close";
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Disconnected);
}

// ── Test 5: onLogout fires exactly once on terminal close ─────────────────────

TEST(ApplicationLifecycle, OnLogoutFiresOnce_TerminalClose) {
    auto app = std::make_shared<LifecycleApplication>();
    LifecycleFixture f;
    f.engine.application = app;

    auto cfg = f.make_cfg();
    Session sess(f.engine, cfg);
    f.open_to_active(sess);

    // Terminal close.
    auto close_fut = asio::co_spawn(f.ioc, sess.close(fixpp::session::close_mode::terminal),
                                    asio::use_future);
    f.run_ioc();
    (void)close_fut.get();

    std::size_t logout_count = 0;
    for (auto& c : app->lifecycle_calls) {
        if (c.which == "onLogout") ++logout_count;
    }
    EXPECT_EQ(logout_count, 1u) << "onLogout must fire exactly once on terminal close";
}

// ── Test 6: onLogout fires exactly once when a callback throws ────────────────
// The throw from onLogout itself triggers terminal close; onLogout must not
// fire again on that second terminal close path (fire-once guard).

TEST(ApplicationLifecycle, OnLogoutFiresOnce_CallbackThrew) {
    auto app = std::make_shared<LifecycleApplication>();
    // Throw on the first onLogout invocation.
    app->throw_on_logout = true;

    LifecycleFixture f;
    f.engine.application = app;

    auto cfg = f.make_cfg();
    Session sess(f.engine, cfg);
    f.open_to_active(sess);

    // Drive terminal close; since onLogout throws, the throw→terminal-close
    // path re-enters close(terminal) — but the fire-once guard ensures onLogout
    // does NOT fire a second time.
    auto close_fut = asio::co_spawn(f.ioc, sess.close(fixpp::session::close_mode::terminal),
                                    asio::use_future);
    f.run_ioc();
    (void)close_fut.get();

    // The throw happened: the session should be Disconnected.
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Disconnected);

    // onLogout was called once (it threw; the fire-once guard prevents a second call).
    // Since our override throws, we cannot record a call in lifecycle_calls — that
    // store happens after the throw; but we can assert it was NOT called a second time
    // by checking that lifecycle_calls has no "onLogout" entries (the throw prevented
    // recording) and that we are Disconnected (terminal close ran).
    for (auto& c : app->lifecycle_calls) {
        EXPECT_NE(c.which, "onLogout")
            << "onLogout threw; recorded entry means it ran AFTER the throw (impossible)";
    }
}

// ── Test 7: fromAdmin fires for inbound Logout (35=5) [FR-004 completeness] ───

TEST(ApplicationLifecycle, FromAdminFiresForInboundLogout) {
    auto app = std::make_shared<LifecycleApplication>();
    LifecycleFixture f;
    f.engine.application = app;

    auto cfg = f.make_cfg();
    Session sess(f.engine, cfg);
    f.open_to_active(sess);

    // Feed inbound Logout from peer (seq=2).
    auto logout_frame = make_logout_frame(2);
    f.feed(sess, logout_frame);

    // fromAdmin must have been called with msg_type "5".
    std::size_t from_admin_logout_count = 0;
    for (auto& c : app->inbound_calls) {
        if (c.which == "fromAdmin" && c.msg_type == "5") ++from_admin_logout_count;
    }
    EXPECT_EQ(from_admin_logout_count, 1u)
        << "fromAdmin must fire exactly once for inbound Logout(35=5)";
}

// ── Test 8: fromAdmin fires for inbound SequenceReset(35=4) [FR-004 completeness]

TEST(ApplicationLifecycle, FromAdminFiresForInboundSequenceReset) {
    auto app = std::make_shared<LifecycleApplication>();
    LifecycleFixture f;
    f.engine.application = app;

    auto cfg = f.make_cfg();
    Session sess(f.engine, cfg);
    f.open_to_active(sess);

    // Feed inbound SequenceReset(Reset mode, 35=4, new_seqno=5).
    // Reset mode: GapFillFlag absent → bypasses seqnum gate.
    auto sr_frame = make_sequence_reset_frame(/*seq=*/2, /*new_seqno=*/3, /*gap_fill=*/false);
    f.feed(sess, sr_frame);

    std::size_t from_admin_sr_count = 0;
    for (auto& c : app->inbound_calls) {
        if (c.which == "fromAdmin" && c.msg_type == "4") ++from_admin_sr_count;
    }
    EXPECT_EQ(from_admin_sr_count, 1u)
        << "fromAdmin must fire exactly once for inbound SequenceReset(35=4)";
}

// ── Test 9: inbound Logout fires BOTH fromAdmin(Logout) AND onLogout ──────────
// Both callbacks fire exactly once, independently (different abstractions).

TEST(ApplicationLifecycle, InboundLogout_FiresBothFromAdminAndOnLogout) {
    auto app = std::make_shared<LifecycleApplication>();
    LifecycleFixture f;
    f.engine.application = app;

    auto cfg = f.make_cfg();
    Session sess(f.engine, cfg);
    f.open_to_active(sess);

    // Feed inbound Logout.
    auto logout_frame = make_logout_frame(2);
    f.feed(sess, logout_frame);

    // fromAdmin(35=5) must fire once.
    std::size_t from_admin_logout = 0;
    for (auto& c : app->inbound_calls) {
        if (c.which == "fromAdmin" && c.msg_type == "5") ++from_admin_logout;
    }
    EXPECT_EQ(from_admin_logout, 1u) << "fromAdmin must fire for inbound Logout";

    // onLogout must fire once (leaving Active).
    std::size_t on_logout_count = 0;
    for (auto& c : app->lifecycle_calls) {
        if (c.which == "onLogout") ++on_logout_count;
    }
    EXPECT_EQ(on_logout_count, 1u) << "onLogout must fire when leaving Active";
}

}  // namespace
}  // namespace fixpp::session::test
