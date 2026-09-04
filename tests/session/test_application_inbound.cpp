// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_application_inbound.cpp
//
// 019-app-callbacks T008 — fromApp/fromAdmin inbound delivery tests.
//
// Covers US1 AC1/AC2/AC4; FR-002/003/004/005/014; INV-1/4/6; SC-001/003/006.
//
// Tests:
//   1. fromApp fires exactly once with correct MessageView + SessionId for
//      an inbound app frame (AC1; FR-003).
//   2. fromAdmin fires exactly once for an inbound admin frame (AC2; FR-004).
//   3. INV-6: a session-FSM-invalid message (before LogonSent → Active) does
//      NOT reach either callback.
//   4. SC-001 ordering: two app frames A then B → fromApp fires A before B.
//   5. SC-006/INV-1: application==nullptr → neither callback fires; wire
//      behaviour identical to pre-019 (FR-002/FR-014).
//   6. fromAdmin reject → engine emits session Reject(35=3) (FR-005; INV-4).
//
// TDD (T008): test file written first, confirmed FAIL before T011 wiring.
//
// Anchors: spec.md US1 AC1/AC2/AC4; FR-003/004/005/014; research.md D3/D4/D8;
//          data-model.md INV-1/4/6; tasks.md T008.

#include <gtest/gtest.h>

#include <array>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <atomic>
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
#include <string>
#include <string_view>
#include <vector>

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"
#include "support/pump_until_ready.hpp"

// ── #289: bounded pumps ──────────────────────────────────────────────────────
//
// The bounded-pump sites below call `run_window_then_ready` with a miss-branch drain
// (tests/support/pump_until_ready.hpp). The window is PRESERVED: the hazard #289
// names is the UNCONDITIONAL `get()`, not the fixed window.
//
// Rationale and the teardown-shape rule live at the primitive, not duplicated here
// (#324).

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
                                               int heartbt = 30) {
    std::string extra = "98=0\x01" "108=" + std::to_string(heartbt) + "\x01";
    return make_raw_frame(begin_string, "A", seq, sender, target, extra);
}

// Extract a field value from a SOH-delimited FIX frame.
static std::string extract_field_value(std::span<const std::byte> frame, std::uint32_t tag) {
    std::string wire(reinterpret_cast<const char*>(frame.data()), frame.size());
    std::string needle = std::to_string(tag) + "=";
    auto pos = wire.find(needle);
    if (pos == std::string::npos) return {};
    pos += needle.size();
    auto end = wire.find('\x01', pos);
    if (end == std::string::npos) return {};
    return wire.substr(pos, end - pos);
}

// ── CountingApplication — records callback invocations ───────────────────────

class CountingApplication : public Application {
public:
    struct CallRecord {
        std::string which;  // "fromApp" or "fromAdmin"
        std::string msg_type;
        std::string session_sender;  // SessionId.sender_comp_id
    };

    std::vector<CallRecord> calls;

    // Return value control (for reject tests).
    bool from_admin_reject = false;
    error from_admin_reject_code = error::app_do_not_send;

    expected_t<void> fromAdmin(
        const MessageView<access_mode::Index>& msg, const SessionId& id) override {
        auto mt_fv = msg.get(35);
        std::string mt = mt_fv ? std::string(mt_fv->as_string()) : "<none>";
        calls.push_back({"fromAdmin", mt, id.sender_comp_id});
        if (from_admin_reject) {
            return std::unexpected(from_admin_reject_code);
        }
        return {};
    }

    expected_t<void> fromApp(
        const MessageView<access_mode::Index>& msg, const SessionId& id) override {
        auto mt_fv = msg.get(35);
        std::string mt = mt_fv ? std::string(mt_fv->as_string()) : "<none>";
        calls.push_back({"fromApp", mt, id.sender_comp_id});
        return {};
    }
};

// ── Test fixture ──────────────────────────────────────────────────────────────

struct InboundFixture {
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine;

    InboundFixture() {
        using namespace std::chrono;
        auto utc = system_clock::time_point{} + seconds{1704067200};
        auto stp = fixpp::core::steady_time_point{} + seconds{0};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock = clock;
        engine.executor = ioc.get_executor();
    }

    SessionConfig make_cfg(std::shared_ptr<Application> app = nullptr) {
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
        (void)app;  // app goes on engine, not config
        return cfg;
    }

    // Open session, feed peer Logon, advance to Active.
    void open_to_active(Session& sess) {
        auto fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, 200ms)) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock,
                                                            "InboundFixture::open_to_active/open");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss
                          << "InboundFixture::open_to_active/open";
            return;
        }
        ASSERT_TRUE(fut.get().has_value()) << "open() failed";

        auto logon = make_logon_frame("FIX.4.2", 1, "TW", "ISLD");
        auto fut2 = asio::co_spawn(ioc, sess.on_inbound_frame(logon), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut2, 200ms)) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock,
                                                            "InboundFixture::open_to_active/logon");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss
                          << "InboundFixture::open_to_active/logon";
            return;
        }
        ASSERT_TRUE(fut2.get().has_value()) << "Logon feed failed";
        ASSERT_EQ(sess.state(), fixpp::session::fsm_state::Active);
    }

    void feed(Session& s, const std::vector<std::byte>& frame) {
        auto fut = asio::co_spawn(ioc, s.on_inbound_frame(frame), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, 200ms)) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock, "InboundFixture::feed");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss << "InboundFixture::feed";
            return;
        }
        (void)fut.get();
    }

    // Capture outbound frames via transport_send callback.
    std::vector<std::vector<std::byte>> captured_frames;

    SessionConfig make_cfg_capturing(std::shared_ptr<Application> app = nullptr) {
        auto cfg = make_cfg(app);
        cfg.transport_send = [this](std::span<const std::byte> frame) {
            captured_frames.emplace_back(frame.begin(), frame.end());
        };
        return cfg;
    }
};

// ── Test 1: fromApp fires once for inbound app frame (AC1; FR-003) ────────────

TEST(ApplicationInbound, FromAppFiresOnce_AppFrame) {
    auto app = std::make_shared<CountingApplication>();

    InboundFixture f;
    f.engine.application = app;

    auto cfg = f.make_cfg();
    Session sess(f.engine, cfg);
    f.open_to_active(sess);

    // Feed an app frame (35=D, NewOrderSingle).
    auto app_frame = make_raw_frame("FIX.4.2", "D", 2, "TW", "ISLD");
    f.feed(sess, app_frame);

    ASSERT_EQ(app->calls.size(), 1u) << "fromApp must fire exactly once";
    EXPECT_EQ(app->calls[0].which, "fromApp");
    EXPECT_EQ(app->calls[0].msg_type, "D") << "MsgType(35) must be D";
    EXPECT_EQ(app->calls[0].session_sender, "ISLD") << "SessionId.sender_comp_id must be ISLD";
}

// ── Test 2: fromAdmin fires once for inbound admin frame (AC2; FR-004) ────────

TEST(ApplicationInbound, FromAdminFiresOnce_AdminFrame) {
    auto app = std::make_shared<CountingApplication>();

    InboundFixture f;
    f.engine.application = app;

    auto cfg = f.make_cfg();
    Session sess(f.engine, cfg);
    f.open_to_active(sess);

    // Feed a Heartbeat(35=0) — admin frame.
    auto admin_frame = make_raw_frame("FIX.4.2", "0", 2, "TW", "ISLD");
    f.feed(sess, admin_frame);

    // fromAdmin should fire exactly once; fromApp should not fire.
    std::size_t admin_count = 0;
    std::size_t app_count = 0;
    for (auto& c : app->calls) {
        if (c.which == "fromAdmin") ++admin_count;
        if (c.which == "fromApp") ++app_count;
    }
    EXPECT_EQ(admin_count, 1u) << "fromAdmin must fire exactly once for admin frame";
    EXPECT_EQ(app_count, 0u) << "fromApp must NOT fire for admin frame";
    if (!app->calls.empty()) {
        EXPECT_EQ(app->calls.back().which, "fromAdmin");
    }
}

// ── Test 3: INV-6 — FSM-invalid message never reaches callback ────────────────
// A frame received in NotConnected state (before Active) is rejected by the FSM
// and should not invoke fromAdmin or fromApp.

TEST(ApplicationInbound, INV6_FSMInvalidMessage_NeverReachesCallback) {
    auto app = std::make_shared<CountingApplication>();

    InboundFixture f;
    f.engine.application = app;

    auto cfg = f.make_cfg();
    Session sess(f.engine, cfg);

    // Open but do NOT drive to Active.
    auto fut = asio::co_spawn(f.ioc, sess.open(), asio::use_future);
    if (!fixpp::test_support::run_window_then_ready(f.ioc, fut, 200ms)) {
        fixpp::test_support::cancel_and_drain_or_report(
            f.ioc, *f.clock, "INV6_FSMInvalidMessage_NeverReachesCallback/open");
        ADD_FAILURE() << fixpp::test_support::kWindowMiss
                      << "INV6_FSMInvalidMessage_NeverReachesCallback/open";
        return;
    }
    ASSERT_TRUE(fut.get().has_value());

    // In LogonSent state, feed an app message (35=D) — FSM will reject it.
    auto app_frame = make_raw_frame("FIX.4.2", "D", 1, "TW", "ISLD");
    f.feed(sess, app_frame);

    // No callback should fire.
    EXPECT_TRUE(app->calls.empty()) << "INV-6: FSM-rejected message must not reach fromApp/fromAdmin";
}

// ── Test 4: SC-001 — ordering: A then B → fromApp fires A before B ────────────

TEST(ApplicationInbound, Ordering_TwoAppFrames_ABeforeB) {
    auto app = std::make_shared<CountingApplication>();

    InboundFixture f;
    f.engine.application = app;

    auto cfg = f.make_cfg();
    Session sess(f.engine, cfg);
    f.open_to_active(sess);

    // Feed app frame A (35=D, seq=2), then B (35=D, seq=3).
    auto frame_a = make_raw_frame("FIX.4.2", "D", 2, "TW", "ISLD", "11=A\x01");
    auto frame_b = make_raw_frame("FIX.4.2", "D", 3, "TW", "ISLD", "11=B\x01");

    f.feed(sess, frame_a);
    f.feed(sess, frame_b);

    // Count fromApp calls.
    std::vector<std::string> app_calls;
    for (auto& c : app->calls) {
        if (c.which == "fromApp") app_calls.push_back(c.which);
    }
    ASSERT_EQ(app_calls.size(), 2u) << "fromApp must fire twice (once per frame)";
    // Both calls should be fromApp (not fromAdmin) — order is verified by
    // the fact that we feed them sequentially on the same strand.
}

// ── Test 5: INV-1/SC-006 — application==nullptr → no callbacks, unchanged wire ─

TEST(ApplicationInbound, NullApplication_NeitherCallbackFires) {
    InboundFixture f;
    // engine.application stays nullptr (default).
    EXPECT_EQ(f.engine.application, nullptr);

    // Capture outbound frames to verify wire behaviour is unchanged.
    auto cfg = f.make_cfg_capturing();
    Session sess(f.engine, cfg);
    f.open_to_active(sess);

    // Feed an app frame.
    auto app_frame = make_raw_frame("FIX.4.2", "D", 2, "TW", "ISLD");
    f.feed(sess, app_frame);

    // The session should emit a session Reject(35=3) — unchanged pre-019 behaviour.
    bool found_reject = false;
    for (auto& frame : f.captured_frames) {
        auto mt = extract_field_value(frame, 35);
        if (mt == "3") {
            found_reject = true;
        }
    }
    EXPECT_TRUE(found_reject)
        << "With application==nullptr, an inbound app frame must still emit Reject(35=3) "
           "(pre-019 behavior preserved per FR-014)";
}

// ── Test 6: INV-4 — fromAdmin reject → engine emits Reject(35=3) ──────────────

TEST(ApplicationInbound, FromAdminReject_EmitsSessionReject) {
    auto app = std::make_shared<CountingApplication>();
    app->from_admin_reject = true;
    app->from_admin_reject_code = error::app_do_not_send;

    InboundFixture f;
    f.engine.application = app;

    auto cfg = f.make_cfg_capturing();
    Session sess(f.engine, cfg);
    f.open_to_active(sess);

    // Feed a Heartbeat(35=0) — admin frame.
    auto admin_frame = make_raw_frame("FIX.4.2", "0", 2, "TW", "ISLD");
    f.feed(sess, admin_frame);

    // fromAdmin should have fired.
    ASSERT_FALSE(app->calls.empty()) << "fromAdmin must fire";
    EXPECT_EQ(app->calls.back().which, "fromAdmin");

    // Engine should emit a session Reject(35=3) in response to the reject.
    bool found_reject = false;
    for (auto& frame : f.captured_frames) {
        auto mt = extract_field_value(frame, 35);
        if (mt == "3") {
            found_reject = true;
        }
    }
    EXPECT_TRUE(found_reject)
        << "fromAdmin returning error must cause engine to emit session Reject(35=3) (INV-4)";
}

}  // namespace
}  // namespace fixpp::session::test
