// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_098_dialect_overlay_inbound_base.cpp
//
// 098-dialect-overlay, contracts/witnesses.md W-5, the base-session arm: a
// negative control (spec FR-018). A FIX.4.4 session over the unmodified
// dictionaries/FIX44.xml, with validate_inbound_messages = true, is fed each
// W-5 frame (test_098_inbound_frames.hpp) and must answer as it does without
// 098, both before and after the feature. It uses only the API that exists
// without 098, so it compiles on either tree.
//
// Each refused frame's expected Reject fields are written below as literals,
// independent of the frame header's constants (tasks.md T004a). A run that
// disagrees with a literal stops for a decision; the literal is not re-recorded
// from the run. To re-derive a row, follow the frame through
// `Session::on_inbound_active_` on an Active session:
//   - `scan_frame_header` with the session's dictionary hooks: a field it cannot
//     read sends the frame to `Session::dispose_unparseable_`, whose
//     any-other-type row consumes the number and sends Reject 373=0 with no 371
//     and Text(58) "Malformed field: invalid tag";
//   - otherwise the validate gate (`Session::validate_inbound_`) runs before any
//     MsgType dispatch: `dictionary_driven_validator::validate` checks the header
//     order, then refuses a MsgType the table does not define (101 FR-001: 373=11,
//     no 371), then walks every field from BeginString(8) in document order
//     (step 1: a tag outside the message, then enum, then type), then the required
//     fields (step 2), then the groups (step 3); its error maps to 373 through
//     `wire_error_to_session_reject_reason` (include/fixpp/wire/reject_reason_map.hpp),
//     its offending tag is 371, and `emit_session_reject_` writes 45 (the frame's
//     34), 372 (its 35) and Text(58), QuickFIX C++'s text for the 373 (101 FR-003,
//     `session_reject_reason_text` in src/session/session.cpp);
//   - a frame that passes the gate reaches Application::fromApp.
// Every frame is fed to its own freshly established session at MsgSeqNum 2.

#include <gtest/gtest.h>

#include <algorithm>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <fixpp/core/clock.hpp>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/application.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "support/extract_tag.hpp"
#include "support/fix44_dictionary.hpp"
#include "support/minimal_security_profile.hpp"
#include "support/pump_until_ready.hpp"
#include "test_098_inbound_frames.hpp"

namespace fixpp::session::test {
namespace {

namespace f098 = fixpp::session::test098;
using fixpp::core::expected_t;
using fixpp::wire::access_mode;
using fixpp::wire::MessageView;

// Records the MsgType of every fromApp call.
class FromAppRecorder final : public Application {
public:
    std::vector<std::string> from_app_types;

    expected_t<void> fromApp(const MessageView<access_mode::Index>& msg,
                             const SessionId& /*id*/) override {
        from_app_types.emplace_back(msg.msg_type());
        return {};
    }
};

// What one frame drew from a freshly established base session.
struct BaseRun {
    std::vector<std::vector<std::byte>> sent;  // frames the session sent while fed
    std::vector<std::string> from_app_types;
    fsm_state state = fsm_state::Disconnected;
};

struct BaseFixture {
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    std::shared_ptr<FromAppRecorder> app = std::make_shared<FromAppRecorder>();
    fixpp::core::EngineConfig engine;
    std::vector<std::vector<std::byte>> captured;

    BaseFixture() {
        using namespace std::chrono;
        // 2024-01-01T00:00:00Z, the instant f098::kSendingTime names.
        auto utc = system_clock::time_point{} + seconds{1704067200};
        auto stp = fixpp::core::steady_time_point{} + seconds{0};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock = clock;
        engine.executor = ioc.get_executor();
        engine.application = app;
    }

    SessionConfig make_cfg() {
        SessionConfig cfg;
        cfg.sender_comp_id = std::string{f098::kOurCompId};
        cfg.target_comp_id = std::string{f098::kPeerCompId};
        cfg.begin_string = std::string{f098::kBeginString};
        cfg.heartbeat_interval = std::chrono::seconds{0};
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = fixpp::test_support::make_fix44_dictionary();
        cfg.executor_override = ioc.get_executor();
        cfg.reset_seqnum_policy_field = reset_seqnum_policy::bilateral_lenient;
        cfg.validate_inbound_messages = true;
        cfg.transport_send = [this](std::span<const std::byte> frame) {
            captured.emplace_back(frame.begin(), frame.end());
        };
        return cfg;
    }

    // Runs `fut` for a bounded window; false (after reporting) when it missed.
    template <class Fut>
    bool pump(Fut& fut, const char* site) {
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, std::chrono::milliseconds{200},
                                                        site)) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock, site);
            ADD_FAILURE() << fixpp::test_support::kWindowMiss << site;
            return false;
        }
        return true;
    }

    // open() (the Logon goes out at 34=1), then the peer's Logon at 34=1.
    bool open_to_active(Session& sess) {
        auto fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
        if (!pump(fut, "DialectOverlayInboundBase098::open")) {
            return false;
        }
        if (!fut.get().has_value()) {
            ADD_FAILURE() << "open() failed";
            return false;
        }
        std::string logon_body = f098::field(98, "0");
        logon_body += f098::field(108, "30");
        auto const logon = f098::make_frame("A", 1, logon_body);
        auto fut2 = asio::co_spawn(ioc, sess.on_inbound_frame(logon), asio::use_future);
        if (!pump(fut2, "DialectOverlayInboundBase098::logon")) {
            return false;
        }
        (void)fut2.get();
        if (sess.state() != fsm_state::Active) {
            ADD_FAILURE() << "the session did not reach Active";
            return false;
        }
        return true;
    }

    BaseRun feed_one(std::vector<std::byte> const& frame) {
        BaseRun run;
        Session sess{engine, make_cfg()};
        if (!open_to_active(sess)) {
            return run;
        }
        std::size_t const before = captured.size();
        auto fut = asio::co_spawn(ioc, sess.on_inbound_frame(frame), asio::use_future);
        if (pump(fut, "DialectOverlayInboundBase098::feed")) {
            (void)fut.get();
        }
        run.sent.assign(captured.begin() + static_cast<std::ptrdiff_t>(before), captured.end());
        run.from_app_types = app->from_app_types;
        run.state = sess.state();
        return run;
    }
};

std::size_t count_msg_type(BaseRun const& run, std::string_view msg_type) {
    return static_cast<std::size_t>(std::ranges::count_if(run.sent, [&](auto const& f) {
        return fixpp::test_support::extract_tag(f, 35) == msg_type;
    }));
}

// The frame drew exactly one Reject(35=3) with these fields and nothing reached
// fromApp. An empty `ref_tag` or `text` means that field is absent.
void expect_reject(BaseRun const& run, std::string_view ref_msg_type, std::string_view reason,
                   std::string_view ref_tag, std::string_view text) {
    EXPECT_EQ(count_msg_type(run, "3"), 1U) << "exactly one Reject(35=3)";
    EXPECT_EQ(count_msg_type(run, "j"), 0U) << "no BusinessMessageReject(35=j)";
    EXPECT_TRUE(run.from_app_types.empty()) << "nothing reaches fromApp";
    EXPECT_EQ(run.state, fsm_state::Active) << "the session stays Active";
    for (auto const& f : run.sent) {
        if (fixpp::test_support::extract_tag(f, 35) != "3") {
            continue;
        }
        EXPECT_EQ(fixpp::test_support::extract_tag(f, 45), "2") << "RefSeqNum(45)";
        EXPECT_EQ(fixpp::test_support::extract_tag(f, 372), ref_msg_type) << "RefMsgType(372)";
        EXPECT_EQ(fixpp::test_support::extract_tag(f, 373), reason) << "SessionRejectReason(373)";
        EXPECT_EQ(fixpp::test_support::extract_tag(f, 371), ref_tag) << "RefTagID(371)";
        EXPECT_EQ(fixpp::test_support::extract_tag(f, 58), text) << "Text(58)";
    }
}

// The frame drew no Reject of either kind and reached fromApp once.
void expect_accepted(BaseRun const& run, std::string_view msg_type) {
    EXPECT_EQ(count_msg_type(run, "3"), 0U) << "no Reject(35=3)";
    EXPECT_EQ(count_msg_type(run, "j"), 0U) << "no BusinessMessageReject(35=j)";
    ASSERT_EQ(run.from_app_types.size(), 1U) << "fromApp called once";
    EXPECT_EQ(run.from_app_types.front(), msg_type);
    EXPECT_EQ(run.state, fsm_state::Active) << "the session stays Active";
}

// AS-1: step 1 meets 5001, not in NewOrderSingle's valid tags (unexpected tag). 101 FR-003
// adds the gate Reject's Text(58).
TEST(DialectOverlayInboundBase098, AS1_CustomFieldOnNewOrderSingle_Rejected) {
    BaseFixture fx;
    expect_reject(fx.feed_one(f098::frame_as1_custom_field(2)), "D", "2", "5001",
                  "Tag not defined for this message type");
}

// AS-2: the parse has no group check, so it passes; step 1's walk is flat and
// meets 5002 inside the Parties entry (unexpected tag). 101 FR-003 adds the
// gate Reject's Text(58).
TEST(DialectOverlayInboundBase098, AS2_CustomPartiesMember_Rejected) {
    BaseFixture fx;
    expect_reject(fx.feed_one(f098::frame_as2_parties_member(2)), "D", "2", "5002",
                  "Tag not defined for this message type");
}

// AS-3: U1 is not a FIX44 MsgType, so the validator refuses it as an invalid
// MsgType, with or without U1's required field: 373=11, no 371, Text(58). 101
// FR-001 and FR-003 supersede this cell's 098 ending (373=2 on BeginString(8),
// B-075-6's universal 371).
TEST(DialectOverlayInboundBase098, AS3_PrivateMsgTypeWithRequired_Rejected) {
    BaseFixture fx;
    expect_reject(fx.feed_one(f098::frame_as3_private_with_required(2)), "U1", "11", "",
                  "Invalid MsgType");
}

TEST(DialectOverlayInboundBase098, AS3_PrivateMsgTypeMissingRequired_Rejected) {
    BaseFixture fx;
    expect_reject(fx.feed_one(f098::frame_as3_private_missing_required(2)), "U1", "11", "",
                  "Invalid MsgType");
}

// AS-4: FIX44 declares no Length+Data pair on 5010 and 5011, so the header scan
// reads 5011 up to its first SOH and then meets the bytes after it as a field
// with no '='.
TEST(DialectOverlayInboundBase098, AS4_CustomLengthDataWithSoh_RejectedAsMalformed) {
    BaseFixture fx;
    expect_reject(fx.feed_one(f098::frame_as4_length_data(2)), "D", "0", "",
                  "Malformed field: invalid tag");
}

// AS-5: Account(1) is Optional on OrderCancelRequest and on NewOrderSingle in
// FIX44, so both frames without it pass.
TEST(DialectOverlayInboundBase098, AS5_PromotedFieldMissing_AcceptedOnBase) {
    BaseFixture fx;
    expect_accepted(fx.feed_one(f098::frame_as5_promoted_missing(2)), "F");
}

TEST(DialectOverlayInboundBase098, AS5_PromotedFieldMissingOnOtherMsgType_Accepted) {
    BaseFixture fx;
    expect_accepted(fx.feed_one(f098::frame_as5_other_msgtype(2)), "D");
}

// AS-6: step 1's enum check: Z is not among OrdType(40)'s FIX44 values. 101
// FR-003 adds the gate Reject's Text(58).
TEST(DialectOverlayInboundBase098, AS6_CustomOrdTypeValue_Rejected) {
    BaseFixture fx;
    expect_reject(fx.feed_one(f098::frame_as6_ordtype_custom(2)), "D", "5", "40",
                  "Value is incorrect (out of range) for this tag");
}

// Enum closure: Text(58) declares no values in FIX44, so any value passes the
// enum check (table_view::enum_valid's empty-code-set accept).
TEST(DialectOverlayInboundBase098, EnumClosure_UnlistedTextValue_AcceptedOnBase) {
    BaseFixture fx;
    expect_accepted(fx.feed_one(f098::frame_enum_closure(2)), "D");
}

}  // namespace
}  // namespace fixpp::session::test
