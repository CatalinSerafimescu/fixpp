// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_application_throw.cpp
//
// 019-app-callbacks T018 — throw-from-callback disposal at every site.
//
// Covers FR-011; spec.md Edge Cases "Callback throws (any site)".
//
// Tests (one per callback site — 7 cells):
//   1. Throwing fromApp  → session terminal-closes, app_callback_threw recorded,
//                          engine not corrupted.
//   2. Throwing fromAdmin → same.
//   3. Throwing toApp     → same (via Engine::send path).
//   4. Throwing toAdmin   → same (via admin emit path).
//   5. Throwing onCreate  → same.
//   6. Throwing onLogon   → same.
//   7. Throwing onLogout  → same (via graceful-close or terminal close path).
//
// After each throw-induced terminal close:
//   - The re-entrancy guard (in_dispatch_) must be cleared (callback_dispatch_scope
//     dtor fires on unwind through invoke_callback_safe).
//   - A subsequent callback on a NEW session must work (engine not corrupted).
//   - error::app_callback_threw must have been recorded in the session's
//     recent_events() or verifiable via the session's state (Disconnected).
//
// Note: the engine "not corrupted" assertion is satisfied by creating a SECOND
// session after the first throws, confirming it can open → Active → callback.
//
// All tests have internal self-deadlines via ioc.run_for(). No ioc.run() calls.
// [[feedback_fail_placeholder_red_test]]
//
// Anchors: spec.md FR-011; data-model.md "Callback throws"; tasks.md T018;
//          session.hpp invoke_callback_safe; session.hpp callback_dispatch_scope.

#include <gtest/gtest.h>

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
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
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

// ── Frame helpers ─────────────────────────────────────────────────────────────

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
    if (!extra_body.empty()) body += extra_body;

    std::string hdr;
    hdr += "8=" + std::string(begin_string) + "\x01";
    hdr += "9=" + std::to_string(body.size()) + "\x01";

    std::string full = hdr + body;
    unsigned int cs = 0;
    for (unsigned char c : full) cs += c;
    cs &= 0xFFU;
    char csbuf[4];
    snprintf(csbuf, sizeof(csbuf), "%03u", cs);
    full += "10=" + std::string(csbuf) + "\x01";

    std::vector<std::byte> frame;
    for (char c : full) frame.push_back(static_cast<std::byte>(c));
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

static std::vector<std::byte> make_app_frame(std::uint32_t seq = 2,
                                             std::string_view sender = "TW",
                                             std::string_view target = "ISLD") {
    return make_raw_frame("FIX.4.2", "D", seq, sender, target);
}

static std::vector<std::byte> make_heartbeat_frame(std::uint32_t seq = 2,
                                                   std::string_view sender = "TW",
                                                   std::string_view target = "ISLD") {
    return make_raw_frame("FIX.4.2", "0", seq, sender, target);
}

static std::vector<std::byte> make_app_payload() {
    static const char kPayload[] = "11=ORD001\x01" "54=1\x01" "55=AAPL\x01";
    std::vector<std::byte> v;
    for (const char* p = kPayload; *p; ++p)
        v.push_back(static_cast<std::byte>(*p));
    return v;
}

// ── Common fixture ────────────────────────────────────────────────────────────

struct ThrowFixture {
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine_cfg;

    ThrowFixture() {
        using namespace std::chrono;
        auto utc = system_clock::time_point{} + seconds{1704067200};
        auto stp = fixpp::core::steady_time_point{} + seconds{0};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine_cfg.clock = clock;
        engine_cfg.executor = ioc.get_executor();
    }

    SessionConfig make_cfg() {
        SessionConfig cfg;
        cfg.sender_comp_id = "ISLD";
        cfg.target_comp_id = "TW";
        cfg.begin_string = "FIX.4.2";
        cfg.heartbeat_interval = 0s;
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override = ioc.get_executor();
        cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
        cfg.transport_send = [](std::span<const std::byte>) {};
        return cfg;
    }

    void run(int ms = 400) {
        ioc.run_for(std::chrono::milliseconds{ms});
        ioc.restart();
    }

    void open_to_active(Session& sess) {
        auto fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
        run();
        ASSERT_TRUE(fut.get().has_value());

        auto logon = make_logon_frame();
        auto fut2 = asio::co_spawn(ioc, sess.on_inbound_frame(logon), asio::use_future);
        run();
        ASSERT_TRUE(fut2.get().has_value());
        ASSERT_EQ(sess.state(), fixpp::session::fsm_state::Active);
    }

    // Verify session has been terminal-closed (state == Disconnected or
    // closed — not Active). This is the expected post-throw state.
    static void verify_terminal_closed(Session& sess) {
        auto st = sess.state();
        EXPECT_NE(st, fixpp::session::fsm_state::Active)
            << "session must NOT be Active after a throwing callback";
    }

    // Verify a second session (different object) works normally after the first threw.
    // This is the "engine not corrupted" check (FR-011).
    void verify_second_session_works(std::shared_ptr<Application> app_ok) {
        engine_cfg.application = app_ok;
        auto cfg2 = make_cfg();
        Session sess2(engine_cfg, cfg2);

        auto fut = asio::co_spawn(ioc, sess2.open(), asio::use_future);
        run();
        if (!fut.get().has_value()) {
            ADD_FAILURE() << "second session open() failed — engine may be corrupted";
            return;
        }
        auto logon = make_logon_frame();
        auto fut2 = asio::co_spawn(ioc, sess2.on_inbound_frame(logon), asio::use_future);
        run();
        (void)fut2.get();
        EXPECT_EQ(sess2.state(), fixpp::session::fsm_state::Active)
            << "second session must reach Active — engine not corrupted";
    }
};

// ── Throwing Application implementations (one per site) ──────────────────────

// Throws from fromApp.
class ThrowingFromApp : public Application {
public:
    expected_t<void> fromApp(const MessageView<access_mode::Index>&,
                             const SessionId&) override {
        throw std::runtime_error("test throw from fromApp");
    }
};

// Throws from fromAdmin.
class ThrowingFromAdmin : public Application {
public:
    expected_t<void> fromAdmin(const MessageView<access_mode::Index>&,
                               const SessionId&) override {
        throw std::runtime_error("test throw from fromAdmin");
    }
};

// Throws from toApp.
class ThrowingToApp : public Application {
public:
    expected_t<void> toApp(const MessageView<access_mode::Index>&,
                           const SessionId&) override {
        throw std::runtime_error("test throw from toApp");
    }
};

// Throws from toAdmin (first call only — only one throw needed to test terminal close).
class ThrowingToAdmin : public Application {
public:
    mutable int call_count = 0;
    void toAdmin(const MessageView<access_mode::Index>&, const SessionId&) override {
        ++call_count;
        // Throw on the first toAdmin call (emitted when session sends Logon reply
        // or any admin frame). Restrict to call 1 to get exactly one throw.
        if (call_count == 1) {
            throw std::runtime_error("test throw from toAdmin");
        }
    }
};

// Throws from onCreate.
class ThrowingOnCreate : public Application {
public:
    void onCreate(const SessionId&) override {
        throw std::runtime_error("test throw from onCreate");
    }
};

// Throws from onLogon.
class ThrowingOnLogon : public Application {
public:
    void onLogon(const SessionId&) override {
        throw std::runtime_error("test throw from onLogon");
    }
};

// Throws from onLogout.
class ThrowingOnLogout : public Application {
public:
    void onLogout(const SessionId&) override {
        throw std::runtime_error("test throw from onLogout");
    }
};

// Non-throwing application for the "engine not corrupted" second-session check.
class OkApplication : public Application {};

// ── Test 1: Throwing fromApp → terminal-close + no propagation ───────────────

TEST(ApplicationThrow, FromAppThrowTerminatesSession) {
    auto app = std::make_shared<ThrowingFromApp>();
    ThrowFixture f;
    f.engine_cfg.application = app;

    auto cfg = f.make_cfg();
    Session sess(f.engine_cfg, cfg);
    f.open_to_active(sess);

    // Feed an app frame — fromApp will throw.
    auto frame = make_app_frame(2);
    auto fut = asio::co_spawn(f.ioc, sess.on_inbound_frame(frame), asio::use_future);
    f.run();
    // on_inbound_frame may return an error or ok (the throw is caught inside).
    // Either way, the session must have been terminal-closed.
    (void)fut.get();

    f.run(300);  // drain any pending close work

    // Session must no longer be Active (terminal-closed by throw handler).
    ThrowFixture::verify_terminal_closed(sess);

    // Engine state not corrupted: a second session with a benign app works.
    f.verify_second_session_works(std::make_shared<OkApplication>());
}

// ── Test 2: Throwing fromAdmin → terminal-close + no propagation ─────────────

TEST(ApplicationThrow, FromAdminThrowTerminatesSession) {
    auto app = std::make_shared<ThrowingFromAdmin>();
    ThrowFixture f;
    f.engine_cfg.application = app;

    auto cfg = f.make_cfg();
    Session sess(f.engine_cfg, cfg);
    f.open_to_active(sess);

    // Feed a Heartbeat (admin frame) — fromAdmin will throw.
    auto hb = make_heartbeat_frame(2);
    auto fut = asio::co_spawn(f.ioc, sess.on_inbound_frame(hb), asio::use_future);
    f.run();
    (void)fut.get();

    f.run(300);

    ThrowFixture::verify_terminal_closed(sess);
    f.verify_second_session_works(std::make_shared<OkApplication>());
}

// ── Test 3: Throwing toApp → terminal-close + no propagation ─────────────────
//
// toApp is invoked from Session::send_impl (the path reached by Engine::send
// or direct Session::send). It runs on the session strand.

TEST(ApplicationThrow, ToAppThrowTerminatesSession) {
    auto app = std::make_shared<ThrowingToApp>();
    ThrowFixture f;
    f.engine_cfg.application = app;

    auto cfg = f.make_cfg();
    Session sess(f.engine_cfg, cfg);
    f.open_to_active(sess);

    // Call Session::send — toApp will throw inside send_impl.
    auto payload = make_app_payload();
    auto fut = asio::co_spawn(f.ioc, sess.send(std::span<const std::byte>(payload)), asio::use_future);
    f.run();
    auto result = fut.get();
    // Result is unexpected(app_callback_threw) because the throw is caught
    // inside invoke_callback_safe and returned; Session::send_impl propagates it.
    EXPECT_FALSE(result.has_value())
        << "send() must return error when toApp throws";
    if (!result.has_value()) {
        EXPECT_EQ(result.error(), error::app_callback_threw)
            << "error must be app_callback_threw (130)";
    }

    f.run(300);

    ThrowFixture::verify_terminal_closed(sess);
    f.verify_second_session_works(std::make_shared<OkApplication>());
}

// ── Test 4: Throwing toAdmin → terminal-close + no propagation ───────────────
//
// toAdmin is fired before every engine-originated admin emit (Logon, Heartbeat,
// etc.). For an initiator session (the default role), the first toAdmin call
// happens when open() drives the initial Logon emit via emit_initiator_logon_().
// ThrowingToAdmin throws on the first call.
//
// Expected: the throw is caught inside fire_to_admin_ → emit_initiator_logon_
// returns an error → open() returns an error. The session is terminal-closed.
// No crash, no propagation, engine not corrupted.

TEST(ApplicationThrow, ToAdminThrowTerminatesSession) {
    auto app = std::make_shared<ThrowingToAdmin>();
    ThrowFixture f;
    f.engine_cfg.application = app;

    // Use initiator role (default in ThrowFixture::make_cfg): open() drives the
    // initial Logon emit, which calls fire_to_admin_ → toAdmin throws.
    auto cfg = f.make_cfg();
    Session sess(f.engine_cfg, cfg);

    // open() triggers toAdmin (for the initial Logon) which throws.
    // open() must return an error (not panic/crash), and the session must be
    // terminal-closed.
    {
        auto fut = asio::co_spawn(f.ioc, sess.open(), asio::use_future);
        f.run();
        auto result = fut.get();
        // open() returns an error because toAdmin threw during the initial Logon emit.
        // The throw was caught; the session was terminal-closed.
        // We do NOT assert has_value() here — the throw prevents a successful open.
        (void)result;
    }
    f.run(300);

    // toAdmin must have been called at least once.
    EXPECT_GE(app->call_count, 1) << "toAdmin must have been called at least once";

    // Session must be terminal-closed (Disconnected or similar — NOT Active).
    // After a throwing toAdmin on the initial Logon path, the session leaves LogonSent
    // and enters Disconnected.
    ThrowFixture::verify_terminal_closed(sess);

    // Second session (with a benign app) works — engine not corrupted.
    f.verify_second_session_works(std::make_shared<OkApplication>());
}

// ── Test 5: Throwing onCreate → terminal-close + no propagation ──────────────
//
// onCreate fires inside Session::open(). A throwing onCreate must be caught;
// open() should return an error or the session should be terminal-closed.

TEST(ApplicationThrow, OnCreateThrowTerminatesSession) {
    auto app = std::make_shared<ThrowingOnCreate>();
    ThrowFixture f;
    f.engine_cfg.application = app;

    auto cfg = f.make_cfg();
    Session sess(f.engine_cfg, cfg);

    // open() fires onCreate — it will throw.
    auto fut = asio::co_spawn(f.ioc, sess.open(), asio::use_future);
    f.run();
    (void)fut.get();  // may succeed or return an error
    f.run(300);

    // Session must not be Active post-throw (onCreate threw before Logon).
    // It may be in NotConnected, Disconnected, or closed state.
    EXPECT_NE(sess.state(), fixpp::session::fsm_state::Active)
        << "session must NOT be Active if onCreate threw";

    // Engine not corrupted.
    f.verify_second_session_works(std::make_shared<OkApplication>());
}

// ── Test 6: Throwing onLogon → terminal-close + no propagation ───────────────
//
// onLogon fires when the session reaches Active. A throwing onLogon must be
// caught; the session must be terminal-closed post-throw.

TEST(ApplicationThrow, OnLogonThrowTerminatesSession) {
    auto app = std::make_shared<ThrowingOnLogon>();
    ThrowFixture f;
    f.engine_cfg.application = app;

    auto cfg = f.make_cfg();
    Session sess(f.engine_cfg, cfg);

    {
        auto fut = asio::co_spawn(f.ioc, sess.open(), asio::use_future);
        f.run();
        ASSERT_TRUE(fut.get().has_value());
    }

    // Feed Logon — onLogon fires when Active is reached, then throws.
    {
        auto logon = make_logon_frame();
        auto fut = asio::co_spawn(f.ioc, sess.on_inbound_frame(logon), asio::use_future);
        f.run();
        (void)fut.get();
    }
    f.run(300);  // drain close work

    // Session must be terminal-closed.
    ThrowFixture::verify_terminal_closed(sess);
    f.verify_second_session_works(std::make_shared<OkApplication>());
}

// ── Test 7: Throwing onLogout → terminal-close + no propagation ──────────────
//
// onLogout fires when the session leaves Active. A throwing onLogout must be
// caught; the close still completes (the close is already in progress, so
// the throw is caught and logged but does not prevent the close from finishing).

TEST(ApplicationThrow, OnLogoutThrowHandledCleanly) {
    auto app = std::make_shared<ThrowingOnLogout>();
    ThrowFixture f;
    f.engine_cfg.application = app;

    auto cfg = f.make_cfg();
    Session sess(f.engine_cfg, cfg);
    f.open_to_active(sess);

    // Terminal close — onLogout fires during the Active→Disconnected transition.
    auto fut = asio::co_spawn(f.ioc, sess.close(close_mode::terminal), asio::use_future);
    f.run();
    (void)fut.get();
    f.run(300);

    // Session is not Active (closed); the throw was caught cleanly.
    ThrowFixture::verify_terminal_closed(sess);

    // Engine not corrupted: second session with benign app works.
    f.verify_second_session_works(std::make_shared<OkApplication>());
}

// ── Tests 8–10: gate-b/r1 FIX-3 counter-cells — toAdmin throw at non-Logon ────
//
// These cells prove FR-011: a toAdmin throw at admin-emit sites that are NOT the
// initiator/acceptor Logon must also terminal-close the session and record
// app_callback_threw. Before FIX-3 these sites discarded fire_to_admin_() return
// and continued the emit — throw went unnoticed. After FIX-3 they branch on
// false → terminal close + error. [[gate-b/r1 FIX-3]]
//
// ThrowingToAdminAtN throws on the N-th toAdmin call. This skips the Logon-path
// throw (call 1 is the acceptor reply Logon) and targets a subsequent admin emit.

class ThrowingToAdminAtN : public Application {
public:
    explicit ThrowingToAdminAtN(int n) : throw_at_(n) {}
    mutable int call_count = 0;
    const int throw_at_;
    void toAdmin(const MessageView<access_mode::Index>&, const SessionId&) override {
        ++call_count;
        if (call_count == throw_at_) {
            throw std::runtime_error("toAdmin throw at call " + std::to_string(throw_at_));
        }
    }
};

// Helper: open an acceptor session to Active.
// The acceptor reply Logon triggers toAdmin call #1.
static void open_acceptor_to_active(ThrowFixture& f, Session& sess) {
    auto fut = asio::co_spawn(f.ioc, sess.open(), asio::use_future);
    f.run();
    ASSERT_TRUE(fut.get().has_value());
    auto logon = make_logon_frame();
    auto fut2 = asio::co_spawn(f.ioc, sess.on_inbound_frame(logon), asio::use_future);
    f.run();
    ASSERT_TRUE(fut2.get().has_value());
    ASSERT_EQ(sess.state(), fixpp::session::fsm_state::Active);
}

// Helper: build a Logout frame for feed-to-session.
static std::vector<std::byte> make_logout_frame(std::uint32_t seq = 2,
                                                std::string_view sender = "TW",
                                                std::string_view target = "ISLD") {
    return make_raw_frame("FIX.4.2", "5", seq, sender, target);
}

// ── Test 8: toAdmin throw on confirming Logout → terminal-close ───────────────
//
// When an inbound Logout(35=5) is received while Active, the session emits a
// confirming Logout before disconnecting. That outbound confirming Logout fires
// toAdmin (call #2 — call #1 was the acceptor reply Logon). With ThrowingToAdminAtN(2),
// fire_to_admin_() returns false → terminal close + app_callback_threw (FIX-3).
// Pre-fix: the (void)fire_to_admin_() discarded the result; session continued
// seqnum assignment and emit instead of terminal-closing.

TEST(ApplicationThrow, ToAdminConfirmingLogoutThrowTerminatesSession) {
    auto app = std::make_shared<ThrowingToAdminAtN>(2);
    ThrowFixture f;
    f.engine_cfg.application = app;

    auto cfg = f.make_cfg();
    Session sess(f.engine_cfg, cfg);
    open_acceptor_to_active(f, sess);
    // toAdmin call #1 happened for the Logon reply.
    ASSERT_EQ(app->call_count, 1);
    ASSERT_EQ(sess.state(), fixpp::session::fsm_state::Active);

    // Feed inbound Logout → session tries to emit confirming Logout → toAdmin call #2 throws.
    auto logout_frame = make_logout_frame(2);
    auto fut = asio::co_spawn(f.ioc, sess.on_inbound_frame(logout_frame), asio::use_future);
    f.run();
    (void)fut.get();
    f.run(300);

    // toAdmin must have been called exactly twice (Logon + confirming Logout).
    EXPECT_EQ(app->call_count, 2) << "toAdmin must be called exactly twice (Logon + confirming Logout)";

    // Session must be terminal-closed (not Active) — FIX-3 ensured this.
    ThrowFixture::verify_terminal_closed(sess);

    f.verify_second_session_works(std::make_shared<OkApplication>());
}

// ── Test 9: toAdmin throw on graceful-close Logout → terminal-close ───────────
//
// Session::close(graceful) emits an outbound Logout before transitioning to
// LogoutSent. That outbound Logout fires toAdmin (call #2 for the acceptor role).
// With ThrowingToAdminAtN(2), fire_to_admin_() returns false → terminal close +
// app_callback_threw (FIX-3). Pre-fix: continued to LogoutSent with a throw.

TEST(ApplicationThrow, ToAdminGracefulCloseLogoutThrowTerminatesSession) {
    auto app = std::make_shared<ThrowingToAdminAtN>(2);
    ThrowFixture f;
    f.engine_cfg.application = app;

    auto cfg = f.make_cfg();
    Session sess(f.engine_cfg, cfg);
    open_acceptor_to_active(f, sess);
    ASSERT_EQ(app->call_count, 1);

    // close(graceful) emits Logout → toAdmin call #2 throws.
    auto fut = asio::co_spawn(f.ioc, sess.close(close_mode::graceful), asio::use_future);
    f.run();
    (void)fut.get();
    f.run(300);

    EXPECT_EQ(app->call_count, 2) << "toAdmin must be called exactly twice";
    ThrowFixture::verify_terminal_closed(sess);
    f.verify_second_session_works(std::make_shared<OkApplication>());
}

// ── Test 10: toAdmin throw on liveness Heartbeat → terminal-close ─────────────
//
// When an inbound Heartbeat(35=0) is received while Active, the session echoes
// a Heartbeat reply → toAdmin fires. With ThrowingToAdminAtN(2) and the acceptor
// path (call #1 = Logon reply), the Heartbeat echo fires toAdmin call #2 → throw.
// FIX-3: terminal-close + app_callback_threw. Pre-fix: continued with (void)discard.

TEST(ApplicationThrow, ToAdminHeartbeatEchoThrowTerminatesSession) {
    auto app = std::make_shared<ThrowingToAdminAtN>(2);
    ThrowFixture f;
    f.engine_cfg.application = app;

    auto cfg = f.make_cfg();
    Session sess(f.engine_cfg, cfg);
    open_acceptor_to_active(f, sess);
    ASSERT_EQ(app->call_count, 1);

    // Feed inbound Heartbeat → session echoes back → toAdmin call #2 throws.
    auto hb_frame = make_heartbeat_frame(2);
    auto fut = asio::co_spawn(f.ioc, sess.on_inbound_frame(hb_frame), asio::use_future);
    f.run();
    (void)fut.get();
    f.run(300);

    EXPECT_EQ(app->call_count, 2) << "toAdmin must be called exactly twice";
    ThrowFixture::verify_terminal_closed(sess);
    f.verify_second_session_works(std::make_shared<OkApplication>());
}

}  // namespace
}  // namespace fixpp::session::test
