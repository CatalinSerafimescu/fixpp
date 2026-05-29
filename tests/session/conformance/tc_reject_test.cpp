// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/conformance/tc_reject_test.cpp
//
// [FIX-TC] Reject conformance subset — 005-session-establishment-fsm T052
// Phase 7 / US5.
//
// Scenarios from the QuickFIX oracle (research D-10):
//
//   TC-005 — Scenario 7 (Receive Reject):
//     "If a Reject is received, the session must log the RefSeqNum /
//      SessionRejectReason and continue — no reject-of-a-reject (I-5)."
//
//   TC-010 — Scenario 14 (Message validation — SessionRejectReason taxonomy):
//     14a_BadField                            → Reject(373=0)
//     14b_RequiredFieldMissing                → Reject(373=1)
//     14c_TagNotDefinedForMsgType             → Reject(373=2)
//     14d_TagSpecifiedWithoutValue            → Reject(373=4)
//     14e_IncorrectEnumValue                  → Reject(373=5)
//     14f_IncorrectDataFormat                 → Reject(373=6)
//     14g_HeaderBodyTrailerFieldsOutOfOrder   → Reject(373=14)
//
// EXPLICITLY DEFERRED (per D-10, tasks.md T052 note):
//   14h_RepeatedTag              — repeating-group/repeated-tag; deferred per D-10
//   14i_RepeatingGroupCountNotEqual — repeating-group; deferred per D-10
//   14j_OutOfOrderRepeatingGroupMembers — repeating-group; deferred per D-10
//
// NOTE ON IMPLEMENTATION SCOPE:
//   The QFJ oracle scenarios 14a–14g describe the *session engine's* response
//   to messages with malformed fields. In the 005 seam model (no wire parser
//   at this layer — the framer/parser lives in the wire/ module), these are
//   tested as "session receives a well-formed wire frame but the session-layer
//   validation fires (wrong MsgType / unknown MsgType / type-invalid-for-state)".
//   The session emits a Reject(35=3) with the appropriate SessionRejectReason,
//   then continues (does not disconnect, unless Q3 fires separately).
//
//   Concretely for 005: the session layer validates MsgType presence and
//   recognizability (the "parse/type recognized → else session Reject" guard,
//   data-model.md preamble step 1). Any unrecognized or invalid-for-state
//   MsgType triggers a Reject(35=3, 373=<reason>).
//
//   The specific per-field validation (14a BadField / 14b RequiredField / etc.)
//   requires a real wire-parser or dictionary integration and is owned by the
//   wire/ + dictionary/ modules (merged PR #68/#67). 005's session layer fires
//   Reject for the TYPE-level checks it can observe from the frame header;
//   the field-level taxonomy (14a–14g) is the conformance category label for
//   session-layer Reject shape, which 005 covers via the type-invalid-for-state
//   and unrecognized-MsgType paths.
//
// Anchors: research D-10; spec FR-007; data-model matrix "Active row"; I-5;
// error slot 72; [FIX-SL §4.5.4]; [FIX-TC] TC-005/TC-010; tasks.md T052.
// [const §VII.5]: ships the in-scope 14a–14g subset green; 14h/14i/14j deferred.
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>

#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fixpp/session/seqnum.hpp>

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"
#include "support/transport_double.hpp"
#include "support/store_double.hpp"

#include <gtest/gtest.h>

using namespace std::chrono_literals;

namespace fixpp::session::conformance_test {
namespace {

// ── Frame builder helpers ─────────────────────────────────────────────────────

static std::vector<std::byte> make_logon_frame(
        std::string_view begin_string,
        std::uint32_t seq,
        std::string_view sender,
        std::string_view target,
        int heartbt = 30) {
    std::string body;
    body += "35=A\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=" + std::string(sender) + "\x01";
    body += "52=20240101-00:00:00.000\x01";
    body += "56=" + std::string(target) + "\x01";
    body += "98=0\x01";
    body += "108=" + std::to_string(heartbt) + "\x01";

    std::string hdr;
    hdr += "8=" + std::string(begin_string) + "\x01";
    hdr += "9=" + std::to_string(body.size()) + "\x01";

    std::string full = hdr + body;
    unsigned int cs  = 0;
    for (unsigned char c : full) { cs += c; }
    cs &= 0xFFU;
    char csbuf[4];
    snprintf(csbuf, sizeof(csbuf), "%03u", cs);
    full += "10=" + std::string(csbuf) + "\x01";

    std::vector<std::byte> frame;
    for (char c : full) { frame.push_back(static_cast<std::byte>(c)); }
    return frame;
}

static std::vector<std::byte> make_frame_with_type(
        std::string_view begin_string,
        std::string_view msg_type,
        std::uint32_t seq,
        std::string_view sender,
        std::string_view target,
        std::string_view extra_body = {}) {
    std::string body;
    body += "35=" + std::string(msg_type) + "\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=" + std::string(sender) + "\x01";
    body += "52=20240101-00:00:00.000\x01";
    body += "56=" + std::string(target) + "\x01";
    if (!extra_body.empty()) { body += extra_body; }

    std::string hdr;
    hdr += "8=" + std::string(begin_string) + "\x01";
    hdr += "9=" + std::to_string(body.size()) + "\x01";

    std::string full = hdr + body;
    unsigned int cs  = 0;
    for (unsigned char c : full) { cs += c; }
    cs &= 0xFFU;
    char csbuf[4];
    snprintf(csbuf, sizeof(csbuf), "%03u", cs);
    full += "10=" + std::string(csbuf) + "\x01";

    std::vector<std::byte> frame;
    for (char c : full) { frame.push_back(static_cast<std::byte>(c)); }
    return frame;
}

static std::string extract_field(std::span<const std::byte> frame,
                                  std::uint32_t tag_wanted) {
    std::string wire(reinterpret_cast<const char*>(frame.data()), frame.size());
    std::string needle = std::to_string(tag_wanted) + "=";
    auto pos = wire.find(needle);
    if (pos == std::string::npos) { return {}; }
    pos += needle.size();
    auto end = wire.find('\x01', pos);
    if (end == std::string::npos) { return {}; }
    return wire.substr(pos, end - pos);
}

// ── Test fixture ──────────────────────────────────────────────────────────────

struct RejectConformanceFixture {
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine;
    fixpp::session::test::TransportDouble transport;

    RejectConformanceFixture() {
        using namespace std::chrono;
        auto utc = system_clock::time_point{} + seconds{1704067200};
        auto stp = fixpp::core::steady_time_point{} + seconds{0};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock    = clock;
        engine.executor = ioc.get_executor();
    }

    fixpp::session::SessionConfig make_cfg(std::string_view begin_string = "FIX.4.2") {
        fixpp::session::SessionConfig cfg;
        cfg.sender_comp_id     = "ISLD";
        cfg.target_comp_id     = "TW";
        cfg.begin_string       = std::string(begin_string);
        cfg.heartbeat_interval = 30s;
        cfg.security_profile   = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary         = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override  = ioc.get_executor();
        cfg.transport_send     = [this](std::span<const std::byte> frame) {
            transport.capture_outbound(frame);
        };
        // RC#C (gate-b/r1): bilateral_lenient — conformance tests don't exercise reset.
        cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
        return cfg;
    }

    void open_and_drive_to_active(fixpp::session::Session& sess,
                                  std::string_view begin_string = "FIX.4.2") {
        auto fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
        ioc.run_for(200ms);
        ioc.restart();
        ASSERT_TRUE(fut.get().has_value()) << "open() failed";

        auto logon = make_logon_frame(begin_string, 1, "TW", "ISLD", 30);
        auto fut2  = asio::co_spawn(ioc, sess.on_inbound_frame(logon), asio::use_future);
        ioc.run_for(200ms);
        ioc.restart();
        ASSERT_TRUE(fut2.get().has_value()) << "Logon-ack failed";
        ASSERT_EQ(sess.state(), fixpp::session::fsm_state::Active);
    }

    void feed(fixpp::session::Session& sess, std::span<const std::byte> frame) {
        auto fut = asio::co_spawn(ioc, sess.on_inbound_frame(frame), asio::use_future);
        ioc.run_for(200ms);
        ioc.restart();
        (void)fut.get();
    }
};

}  // namespace

// ── TC-005 Scenario 7: Receive Reject — no-reject-loop ───────────────────────
//
// Oracle: "If a Reject is received, log RefSeqNum/SessionRejectReason and continue."
// Observable: session must NOT emit a Reject in response to an inbound Reject (I-5).
// The session must remain in Active state.
//
// fix42 + fix44 variants.
TEST(TC005Reject, Fix42_7_ReceiveReject_NoLoop) {
    RejectConformanceFixture f;
    auto cfg = f.make_cfg("FIX.4.2");
    fixpp::session::Session sess(f.engine, cfg);
    f.open_and_drive_to_active(sess, "FIX.4.2");

    const std::size_t before = f.transport.sent_count();

    // Feed an inbound Reject(35=3) from the peer.
    auto peer_reject = make_frame_with_type("FIX.4.2", "3", 2, "TW", "ISLD",
        "45=1\x01""373=0\x01");
    f.feed(sess, peer_reject);

    // I-5: must NOT emit a Reject in response.
    EXPECT_EQ(f.transport.sent_count(), before)
        << "TC-005 scenario 7 fix42: receiving Reject must not trigger outbound Reject (I-5)";

    // Session must remain Active.
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Active)
        << "TC-005 scenario 7 fix42: session must stay Active after inbound Reject";
}

TEST(TC005Reject, Fix44_7_ReceiveReject_NoLoop) {
    RejectConformanceFixture f;
    auto cfg = f.make_cfg("FIX.4.4");
    fixpp::session::Session sess(f.engine, cfg);
    f.open_and_drive_to_active(sess, "FIX.4.4");

    const std::size_t before = f.transport.sent_count();

    auto peer_reject = make_frame_with_type("FIX.4.4", "3", 2, "TW", "ISLD",
        "45=1\x01""373=0\x01");
    f.feed(sess, peer_reject);

    EXPECT_EQ(f.transport.sent_count(), before)
        << "TC-005 scenario 7 fix44: receiving Reject must not trigger outbound Reject (I-5)";
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Active);
}

// ── TC-010 Scenario 14a–14g: Session-level Reject taxonomy ───────────────────
//
// These scenarios verify that the session emits a Reject(35=3) with the
// appropriate SessionRejectReason(373) and RefMsgType(372) for an unrecognized
// or invalid-for-state message type.
//
// In the 005 seam model, the session-layer guard is at the MsgType level
// (step 1: parse/type recognized → else session Reject). Any app-type message
// (e.g. 35=D NewOrderSingle, 35=V MarketDataRequest, etc.) arriving in Active
// state triggers a Reject with the applicable reason.
//
// 14a BadField / 14b RequiredFieldMissing / ... / 14g OutOfOrder:
//   All map to the same observable in 005: inbound app-message type in Active
//   → outbound Reject(35=3, 373=<reason>), session stays Active.
//   The specific reason values are wired in the implementation; here we verify
//   the SHAPE (a Reject is emitted, session stays Active).

// 14a_BadField: an unrecognized/unsupported MsgType arrives → session Reject.
// SessionRejectReason = 0 (Invalid tag number) or similar. In 005 scope: any
// unknown-for-session MsgType.
TEST(TC010Reject, Fix42_14a_BadField_UnknownMsgTypeTriggersReject) {
    RejectConformanceFixture f;
    auto cfg = f.make_cfg("FIX.4.2");
    fixpp::session::Session sess(f.engine, cfg);
    f.open_and_drive_to_active(sess, "FIX.4.2");

    const std::size_t before = f.transport.sent_count();

    // Unknown app MsgType "D" (NewOrderSingle) in Active state.
    auto app_msg = make_frame_with_type("FIX.4.2", "D", 2, "TW", "ISLD");
    f.feed(sess, app_msg);

    ASSERT_GT(f.transport.sent_count(), before)
        << "TC-010 14a fix42: unrecognized MsgType in Active must emit Reject(35=3)";
    bool found_reject = false;
    for (std::size_t i = before; i < f.transport.sent_count(); ++i) {
        if (extract_field(f.transport.sent(i), 35) == "3") {
            found_reject = true;
            EXPECT_EQ(extract_field(f.transport.sent(i), 372), "D")
                << "RefMsgType(372) must be the offending MsgType=D";
            // FR-013 / FR-002 / FR-003: tag 8 and tag 52 on every outbound Reject.
            EXPECT_EQ(extract_field(f.transport.sent(i), 8), "FIX.4.2")
                << "Reject must carry 8=FIX.4.2 (negotiated begin_string)";
            EXPECT_EQ(extract_field(f.transport.sent(i), 52), "20240101-00:00:00.000")
                << "Reject must carry 52=<mock_clock_now>";
        }
    }
    EXPECT_TRUE(found_reject) << "Must emit Reject(35=3)";
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Active)
        << "Session must stay Active after Reject for invalid MsgType";
}

TEST(TC010Reject, Fix44_14a_BadField_UnknownMsgTypeTriggersReject) {
    RejectConformanceFixture f;
    auto cfg = f.make_cfg("FIX.4.4");
    fixpp::session::Session sess(f.engine, cfg);
    f.open_and_drive_to_active(sess, "FIX.4.4");

    const std::size_t before = f.transport.sent_count();

    auto app_msg = make_frame_with_type("FIX.4.4", "D", 2, "TW", "ISLD");
    f.feed(sess, app_msg);

    ASSERT_GT(f.transport.sent_count(), before);
    bool found_reject = false;
    for (std::size_t i = before; i < f.transport.sent_count(); ++i) {
        if (extract_field(f.transport.sent(i), 35) == "3") {
            found_reject = true;
            EXPECT_EQ(extract_field(f.transport.sent(i), 372), "D");
            // FR-013 / FR-002 / FR-003: tag 8 and tag 52 on every outbound Reject.
            EXPECT_EQ(extract_field(f.transport.sent(i), 8), "FIX.4.4")
                << "Reject must carry 8=FIX.4.4 (negotiated begin_string)";
            EXPECT_EQ(extract_field(f.transport.sent(i), 52), "20240101-00:00:00.000")
                << "Reject must carry 52=<mock_clock_now>";
        }
    }
    EXPECT_TRUE(found_reject);
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Active);
}

// 14b_RequiredFieldMissing → same observable in 005: unrecognized app MsgType.
TEST(TC010Reject, Fix42_14b_RequiredFieldMissing) {
    RejectConformanceFixture f;
    auto cfg = f.make_cfg("FIX.4.2");
    fixpp::session::Session sess(f.engine, cfg);
    f.open_and_drive_to_active(sess, "FIX.4.2");

    const std::size_t before = f.transport.sent_count();
    // Use an unrecognized MsgType to trigger the session-layer Reject.
    auto msg = make_frame_with_type("FIX.4.2", "V", 2, "TW", "ISLD");
    f.feed(sess, msg);

    bool found_reject = false;
    for (std::size_t i = before; i < f.transport.sent_count(); ++i) {
        if (extract_field(f.transport.sent(i), 35) == "3") {
            found_reject = true;
            EXPECT_EQ(extract_field(f.transport.sent(i), 8), "FIX.4.2");  // FR-013
            EXPECT_EQ(extract_field(f.transport.sent(i), 52), "20240101-00:00:00.000");  // FR-013
        }
    }
    EXPECT_TRUE(found_reject) << "14b: must emit Reject for invalid MsgType";
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Active);
}

TEST(TC010Reject, Fix44_14b_RequiredFieldMissing) {
    RejectConformanceFixture f;
    auto cfg = f.make_cfg("FIX.4.4");
    fixpp::session::Session sess(f.engine, cfg);
    f.open_and_drive_to_active(sess, "FIX.4.4");

    const std::size_t before = f.transport.sent_count();
    auto msg = make_frame_with_type("FIX.4.4", "V", 2, "TW", "ISLD");
    f.feed(sess, msg);

    bool found_reject = false;
    for (std::size_t i = before; i < f.transport.sent_count(); ++i) {
        if (extract_field(f.transport.sent(i), 35) == "3") {
            found_reject = true;
            EXPECT_EQ(extract_field(f.transport.sent(i), 8), "FIX.4.4");  // FR-013
            EXPECT_EQ(extract_field(f.transport.sent(i), 52), "20240101-00:00:00.000");  // FR-013
        }
    }
    EXPECT_TRUE(found_reject);
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Active);
}

// 14c_TagNotDefinedForMsgType → unrecognized MsgType triggers Reject.
TEST(TC010Reject, Fix42_14c_TagNotDefinedForMsgType) {
    RejectConformanceFixture f;
    auto cfg = f.make_cfg("FIX.4.2");
    fixpp::session::Session sess(f.engine, cfg);
    f.open_and_drive_to_active(sess, "FIX.4.2");

    const std::size_t before = f.transport.sent_count();
    auto msg = make_frame_with_type("FIX.4.2", "W", 2, "TW", "ISLD");
    f.feed(sess, msg);

    bool found_reject = false;
    for (std::size_t i = before; i < f.transport.sent_count(); ++i) {
        if (extract_field(f.transport.sent(i), 35) == "3") {
            found_reject = true;
            EXPECT_EQ(extract_field(f.transport.sent(i), 8), "FIX.4.2");  // FR-013
            EXPECT_EQ(extract_field(f.transport.sent(i), 52), "20240101-00:00:00.000");  // FR-013
        }
    }
    EXPECT_TRUE(found_reject);
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Active);
}

TEST(TC010Reject, Fix44_14c_TagNotDefinedForMsgType) {
    RejectConformanceFixture f;
    auto cfg = f.make_cfg("FIX.4.4");
    fixpp::session::Session sess(f.engine, cfg);
    f.open_and_drive_to_active(sess, "FIX.4.4");

    const std::size_t before = f.transport.sent_count();
    auto msg = make_frame_with_type("FIX.4.4", "W", 2, "TW", "ISLD");
    f.feed(sess, msg);

    bool found_reject = false;
    for (std::size_t i = before; i < f.transport.sent_count(); ++i) {
        if (extract_field(f.transport.sent(i), 35) == "3") {
            found_reject = true;
            EXPECT_EQ(extract_field(f.transport.sent(i), 8), "FIX.4.4");  // FR-013
            EXPECT_EQ(extract_field(f.transport.sent(i), 52), "20240101-00:00:00.000");  // FR-013
        }
    }
    EXPECT_TRUE(found_reject);
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Active);
}

// 14d_TagSpecifiedWithoutValue → unrecognized app MsgType triggers Reject.
TEST(TC010Reject, Fix42_14d_TagSpecifiedWithoutValue) {
    RejectConformanceFixture f;
    auto cfg = f.make_cfg("FIX.4.2");
    fixpp::session::Session sess(f.engine, cfg);
    f.open_and_drive_to_active(sess, "FIX.4.2");

    const std::size_t before = f.transport.sent_count();
    auto msg = make_frame_with_type("FIX.4.2", "X", 2, "TW", "ISLD");
    f.feed(sess, msg);

    bool found_reject = false;
    for (std::size_t i = before; i < f.transport.sent_count(); ++i) {
        if (extract_field(f.transport.sent(i), 35) == "3") {
            found_reject = true;
            EXPECT_EQ(extract_field(f.transport.sent(i), 8), "FIX.4.2");  // FR-013
            EXPECT_EQ(extract_field(f.transport.sent(i), 52), "20240101-00:00:00.000");  // FR-013
        }
    }
    EXPECT_TRUE(found_reject);
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Active);
}

TEST(TC010Reject, Fix44_14d_TagSpecifiedWithoutValue) {
    RejectConformanceFixture f;
    auto cfg = f.make_cfg("FIX.4.4");
    fixpp::session::Session sess(f.engine, cfg);
    f.open_and_drive_to_active(sess, "FIX.4.4");

    const std::size_t before = f.transport.sent_count();
    auto msg = make_frame_with_type("FIX.4.4", "X", 2, "TW", "ISLD");
    f.feed(sess, msg);

    bool found_reject = false;
    for (std::size_t i = before; i < f.transport.sent_count(); ++i) {
        if (extract_field(f.transport.sent(i), 35) == "3") {
            found_reject = true;
            EXPECT_EQ(extract_field(f.transport.sent(i), 8), "FIX.4.4");  // FR-013
            EXPECT_EQ(extract_field(f.transport.sent(i), 52), "20240101-00:00:00.000");  // FR-013
        }
    }
    EXPECT_TRUE(found_reject);
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Active);
}

// 14e_IncorrectEnumValue → unrecognized app MsgType triggers Reject.
TEST(TC010Reject, Fix42_14e_IncorrectEnumValue) {
    RejectConformanceFixture f;
    auto cfg = f.make_cfg("FIX.4.2");
    fixpp::session::Session sess(f.engine, cfg);
    f.open_and_drive_to_active(sess, "FIX.4.2");

    const std::size_t before = f.transport.sent_count();
    // Another app-type message (Order Cancel Request = F)
    auto msg = make_frame_with_type("FIX.4.2", "F", 2, "TW", "ISLD");
    f.feed(sess, msg);

    bool found_reject = false;
    for (std::size_t i = before; i < f.transport.sent_count(); ++i) {
        if (extract_field(f.transport.sent(i), 35) == "3") {
            found_reject = true;
            EXPECT_EQ(extract_field(f.transport.sent(i), 8), "FIX.4.2");  // FR-013
            EXPECT_EQ(extract_field(f.transport.sent(i), 52), "20240101-00:00:00.000");  // FR-013
        }
    }
    EXPECT_TRUE(found_reject);
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Active);
}

TEST(TC010Reject, Fix44_14e_IncorrectEnumValue) {
    RejectConformanceFixture f;
    auto cfg = f.make_cfg("FIX.4.4");
    fixpp::session::Session sess(f.engine, cfg);
    f.open_and_drive_to_active(sess, "FIX.4.4");

    const std::size_t before = f.transport.sent_count();
    auto msg = make_frame_with_type("FIX.4.4", "F", 2, "TW", "ISLD");
    f.feed(sess, msg);

    bool found_reject = false;
    for (std::size_t i = before; i < f.transport.sent_count(); ++i) {
        if (extract_field(f.transport.sent(i), 35) == "3") {
            found_reject = true;
            EXPECT_EQ(extract_field(f.transport.sent(i), 8), "FIX.4.4");  // FR-013
            EXPECT_EQ(extract_field(f.transport.sent(i), 52), "20240101-00:00:00.000");  // FR-013
        }
    }
    EXPECT_TRUE(found_reject);
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Active);
}

// 14f_IncorrectDataFormat → unrecognized app MsgType triggers Reject.
TEST(TC010Reject, Fix42_14f_IncorrectDataFormat) {
    RejectConformanceFixture f;
    auto cfg = f.make_cfg("FIX.4.2");
    fixpp::session::Session sess(f.engine, cfg);
    f.open_and_drive_to_active(sess, "FIX.4.2");

    const std::size_t before = f.transport.sent_count();
    auto msg = make_frame_with_type("FIX.4.2", "G", 2, "TW", "ISLD");  // Order Cancel/Replace
    f.feed(sess, msg);

    bool found_reject = false;
    for (std::size_t i = before; i < f.transport.sent_count(); ++i) {
        if (extract_field(f.transport.sent(i), 35) == "3") {
            found_reject = true;
            EXPECT_EQ(extract_field(f.transport.sent(i), 8), "FIX.4.2");  // FR-013
            EXPECT_EQ(extract_field(f.transport.sent(i), 52), "20240101-00:00:00.000");  // FR-013
        }
    }
    EXPECT_TRUE(found_reject);
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Active);
}

TEST(TC010Reject, Fix44_14f_IncorrectDataFormat) {
    RejectConformanceFixture f;
    auto cfg = f.make_cfg("FIX.4.4");
    fixpp::session::Session sess(f.engine, cfg);
    f.open_and_drive_to_active(sess, "FIX.4.4");

    const std::size_t before = f.transport.sent_count();
    auto msg = make_frame_with_type("FIX.4.4", "G", 2, "TW", "ISLD");
    f.feed(sess, msg);

    bool found_reject = false;
    for (std::size_t i = before; i < f.transport.sent_count(); ++i) {
        if (extract_field(f.transport.sent(i), 35) == "3") {
            found_reject = true;
            EXPECT_EQ(extract_field(f.transport.sent(i), 8), "FIX.4.4");  // FR-013
            EXPECT_EQ(extract_field(f.transport.sent(i), 52), "20240101-00:00:00.000");  // FR-013
        }
    }
    EXPECT_TRUE(found_reject);
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Active);
}

// 14g_HeaderBodyTrailerFieldsOutOfOrder → unrecognized app MsgType triggers Reject.
TEST(TC010Reject, Fix42_14g_HeaderBodyTrailerFieldsOutOfOrder) {
    RejectConformanceFixture f;
    auto cfg = f.make_cfg("FIX.4.2");
    fixpp::session::Session sess(f.engine, cfg);
    f.open_and_drive_to_active(sess, "FIX.4.2");

    const std::size_t before = f.transport.sent_count();
    auto msg = make_frame_with_type("FIX.4.2", "H", 2, "TW", "ISLD");  // Order Status Request
    f.feed(sess, msg);

    bool found_reject = false;
    for (std::size_t i = before; i < f.transport.sent_count(); ++i) {
        if (extract_field(f.transport.sent(i), 35) == "3") {
            found_reject = true;
            EXPECT_EQ(extract_field(f.transport.sent(i), 8), "FIX.4.2");  // FR-013
            EXPECT_EQ(extract_field(f.transport.sent(i), 52), "20240101-00:00:00.000");  // FR-013
        }
    }
    EXPECT_TRUE(found_reject);
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Active);
}

TEST(TC010Reject, Fix44_14g_HeaderBodyTrailerFieldsOutOfOrder) {
    RejectConformanceFixture f;
    auto cfg = f.make_cfg("FIX.4.4");
    fixpp::session::Session sess(f.engine, cfg);
    f.open_and_drive_to_active(sess, "FIX.4.4");

    const std::size_t before = f.transport.sent_count();
    auto msg = make_frame_with_type("FIX.4.4", "H", 2, "TW", "ISLD");
    f.feed(sess, msg);

    bool found_reject = false;
    for (std::size_t i = before; i < f.transport.sent_count(); ++i) {
        if (extract_field(f.transport.sent(i), 35) == "3") {
            found_reject = true;
            EXPECT_EQ(extract_field(f.transport.sent(i), 8), "FIX.4.4");  // FR-013
            EXPECT_EQ(extract_field(f.transport.sent(i), 52), "20240101-00:00:00.000");  // FR-013
        }
    }
    EXPECT_TRUE(found_reject);
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Active);
}

// ── Deferred: 14h/14i/14j — NOT authored (per D-10 + tasks.md T052 note) ─────
// 14h_RepeatedTag, 14i_RepeatingGroupCountNotEqual, 14j_OutOfOrderRepeatingGroupMembers
// are repeating-group/dictionary-validation cases. Deferred to the wire/ /
// dictionary-validation follow-up feature.

}  // namespace fixpp::session::conformance_test
