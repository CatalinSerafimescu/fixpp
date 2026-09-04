// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_application_business_reject.cpp
//
// 019-app-callbacks T009 — MANDATORY named test (Gate A RC#3).
//
// Tests:
//   1. fromApp returning an error → engine emits BusinessMessageReject(35=j)
//      with RefMsgType(372), RefSeqNum(45), BusinessRejectReason(380) present.
//      AND NOT a session Reject(35=3).
//   2. fromApp accepting → no reject on the wire.
//
// Anchors: spec.md US1 AC3; FR-005; SC-003; research.md D4;
//          [FIX50SP2] Infrastructure / Business Rejects (catalogue A-014);
//          tasks.md T009.

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
#include <string>
#include <string_view>
#include <vector>

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"
#include "support/pump_until_ready.hpp"

// ── #289: bounded pumps ──────────────────────────────────────────────────────
//
// The `run_for(W); restart(); get()` sites below call `run_window_then_ready` with a
// miss-branch drain (tests/support/pump_until_ready.hpp). The window is PRESERVED:
// the hazard #289 names is the UNCONDITIONAL `get()`, not the fixed window.
//
// The rationale and the teardown-shape rule -- why the drain runs on the miss branch
// and not in a fixture destructor -- are documented AT the primitive. Read it there.
// That text is deliberately NOT copied into this file: each earlier per-file copy
// acquired clauses that are false at its own sites (#324's FILE-SPECIFIC ADDENDA), a
// cost paid once per copy and avoided entirely by pointing.

using namespace std::chrono_literals;
using fixpp::core::error;
using fixpp::core::expected_t;
using fixpp::session::Application;
using fixpp::session::SessionId;
using fixpp::wire::MessageView;
using fixpp::wire::access_mode;

namespace fixpp::session::test {
namespace {

// ── Frame builders ────────────────────────────────────────────────────────────

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

static std::vector<std::byte> make_logon_frame() {
    std::string extra = "98=0\x01" "108=30\x01";
    return make_raw_frame("FIX.4.2", "A", 1, "TW", "ISLD", extra);
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

// ── RejectingApplication — rejects fromApp ───────────────────────────────────

class RejectingFromAppApplication : public Application {
public:
    int fromApp_calls = 0;

    expected_t<void> fromApp(const MessageView<access_mode::Index>& /*msg*/,
                             const SessionId& /*id*/) override {
        ++fromApp_calls;
        return std::unexpected(error::app_do_not_send);
    }
};

// ── AcceptingApplication — accepts fromApp ────────────────────────────────────

class AcceptingFromAppApplication : public Application {
public:
    int fromApp_calls = 0;

    expected_t<void> fromApp(const MessageView<access_mode::Index>& /*msg*/,
                             const SessionId& /*id*/) override {
        ++fromApp_calls;
        return {};
    }
};

// ── Test fixture ──────────────────────────────────────────────────────────────

struct BusinessRejectFixture {
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine;
    std::vector<std::vector<std::byte>> captured_frames;

    BusinessRejectFixture() {
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
        cfg.heartbeat_interval = 0s;
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override = ioc.get_executor();
        cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
        cfg.transport_send = [this](std::span<const std::byte> frame) {
            captured_frames.emplace_back(frame.begin(), frame.end());
        };
        return cfg;
    }

    void open_to_active(Session& sess) {
        auto fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, 200ms)) {
            fixpp::test_support::cancel_and_drain_or_report(
                ioc, *clock, "BusinessRejectFixture::open_to_active/open");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss
                          << "BusinessRejectFixture::open_to_active/open";
            return;
        }
        ASSERT_TRUE(fut.get().has_value());

        auto logon = make_logon_frame();
        auto fut2 = asio::co_spawn(ioc, sess.on_inbound_frame(logon), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut2, 200ms)) {
            fixpp::test_support::cancel_and_drain_or_report(
                ioc, *clock, "BusinessRejectFixture::open_to_active/logon");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss
                          << "BusinessRejectFixture::open_to_active/logon";
            return;
        }
        ASSERT_TRUE(fut2.get().has_value());
        ASSERT_EQ(sess.state(), fixpp::session::fsm_state::Active);
    }

    void feed(Session& s, const std::vector<std::byte>& frame) {
        auto fut = asio::co_spawn(ioc, s.on_inbound_frame(frame), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, 200ms)) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock,
                                                            "BusinessRejectFixture::feed");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss << "BusinessRejectFixture::feed";
            return;
        }
        (void)fut.get();
    }
};

// ── Test 1: fromApp reject → BusinessMessageReject(35=j), NOT Reject(35=3) ───
//
// AC3; FR-005; SC-003; D4; [FIX50SP2] Infrastructure / Business Rejects.

TEST(ApplicationBusinessReject, FromAppReject_EmitsBusinessMessageReject_NotSessionReject) {
    auto app = std::make_shared<RejectingFromAppApplication>();

    BusinessRejectFixture f;
    f.engine.application = app;

    auto cfg = f.make_cfg();
    Session sess(f.engine, cfg);
    f.open_to_active(sess);

    // Feed an app frame (35=D, NewOrderSingle, seq=2).
    auto app_frame = make_raw_frame("FIX.4.2", "D", 2, "TW", "ISLD");
    f.feed(sess, app_frame);

    // fromApp must have fired.
    ASSERT_GE(app->fromApp_calls, 1) << "fromApp must fire";

    // Scan outbound frames — MUST find BusinessMessageReject(35=j).
    bool found_bmr = false;
    bool found_session_reject = false;
    std::string bmr_ref_msg_type;
    std::string bmr_ref_seq_num;
    std::string bmr_reason;

    for (auto& frame : f.captured_frames) {
        auto mt = extract_field_value(frame, 35);
        if (mt == "j") {
            found_bmr = true;
            bmr_ref_msg_type = extract_field_value(frame, 372);  // RefMsgType
            bmr_ref_seq_num = extract_field_value(frame, 45);    // RefSeqNum
            bmr_reason = extract_field_value(frame, 380);         // BusinessRejectReason
        }
        if (mt == "3") {
            found_session_reject = true;
        }
    }

    EXPECT_TRUE(found_bmr)
        << "fromApp reject must cause engine to emit BusinessMessageReject(35=j) (FR-005; AC3)";
    EXPECT_FALSE(found_session_reject)
        << "fromApp reject must NOT emit session Reject(35=3) (SC-003: 35=j only)";

    // Assert that the required fields are present in the BusinessMessageReject.
    EXPECT_EQ(bmr_ref_msg_type, "D")
        << "RefMsgType(372) must match the rejected message's MsgType (research D4)";
    EXPECT_EQ(bmr_ref_seq_num, "2")
        << "RefSeqNum(45) must match the rejected message's MsgSeqNum (research D4)";
    EXPECT_FALSE(bmr_reason.empty())
        << "BusinessRejectReason(380) must be present in BusinessMessageReject (research D4)";
}

// ── Test 2: fromApp accept → no reject on the wire ───────────────────────────
//
// AC3 (positive case): when fromApp accepts, the engine emits nothing.

TEST(ApplicationBusinessReject, FromAppAccept_NoRejectOnWire) {
    auto app = std::make_shared<AcceptingFromAppApplication>();

    BusinessRejectFixture f;
    f.engine.application = app;

    auto cfg = f.make_cfg();
    Session sess(f.engine, cfg);
    f.open_to_active(sess);

    // Record frame count before feeding the app frame.
    const std::size_t frames_before = f.captured_frames.size();

    // Feed an app frame (35=D, seq=2).
    auto app_frame = make_raw_frame("FIX.4.2", "D", 2, "TW", "ISLD");
    f.feed(sess, app_frame);

    // fromApp must have fired.
    ASSERT_GE(app->fromApp_calls, 1) << "fromApp must fire";

    // No additional outbound frames should have been emitted after the accept.
    // (The session may have emitted admin frames during setup — we only check
    //  what was added after the app frame was fed.)
    bool found_bmr_or_reject = false;
    for (std::size_t i = frames_before; i < f.captured_frames.size(); ++i) {
        auto mt = extract_field_value(f.captured_frames[i], 35);
        if (mt == "j" || mt == "3") {
            found_bmr_or_reject = true;
        }
    }
    EXPECT_FALSE(found_bmr_or_reject)
        << "fromApp returning {} (accept) must not cause any reject to be emitted";
}

}  // namespace
}  // namespace fixpp::session::test
