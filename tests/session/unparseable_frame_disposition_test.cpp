// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/unparseable_frame_disposition_test.cpp
//
// 092-garbled-frame-reject — the session never acts on a frame it could not parse.
//
// Issue507Reproducer_* (tasks.md T005; spec SC-001; contract C-2 row D-4): fixpp#507's
// T076 arrangement as a real test. An Active session receives a SequenceReset in Reset
// mode (GapFillFlag(123) != "Y", NewSeqNo(36)=500) at the expected MsgSeqNum 2 whose
// body carries a field the parse cannot accept, then a conformant Heartbeat probe at
// MsgSeqNum 500:
//   - malformed count: `90=2<SOH>91=xyz<SOH>` — SecureDataLen(90) counts two bytes and
//     the byte after them is not SOH (length_data_mismatch; 373=5, 371=90);
//   - malformed tag:   `9x9=1<SOH>` (malformed_tag; 373=0, no 371);
// each with inbound validation on and off.
//
// The frame passes the Framer (BodyLength and CheckSum are correct), so what is
// exercised is the session's disposition of a framed-but-unparseable frame. Each cell
// asserts the post-092 outcome: the SequenceReset draws one Reject (45=2, 372=4 and
// the 373 and 371 above) and NewSeqNo is never applied, so the probe at 500 is a gap and
// draws a ResendRequest(35=2). The probe's ResendRequest is the witness that
// NextNumIn did not move to 500; this target reads no private counter (no
// FIXPP_TEST_HOOKS, fixpp#511).
//
// Every observation after open_to_active is non-fatal (EXPECT_*), so the probe runs
// and every clause reports in each cell.
//
// Anchor_D* (tasks.md T025; contract C-2): one cell per C-2 row, each on an input
// the pre-092 session mishandles (SC-006). Each Reject cell asserts 45, 372, 373, 371
// (or its absence) and 58 exactly.
//
// ReplayGuard_* (tasks.md T015; research R-12): the resend store walk classifies a
// stored frame by the header scan's MsgType. The scan stops at its first fault, so
// a stored admin frame with a fault before its 35 scans with no MsgType; such a
// slot is gap-filled, never rebuilt and resent as application data
// [FIX-SL §4.8.3]. The store is a custom MessageStore, which may hold bytes fixpp
// did not write.
//
// Anchors: specs/092-garbled-frame-reject/spec.md SC-001, FR-007;
//          contracts/unparseable-frame-disposition.md C-2 (D-4) and its Reject contents;
//          fixpp#507 (the T076 table and reproducer).

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/application.hpp>
#include <fixpp/session/message_store.hpp>
#include <fixpp/session/message_store_factory.hpp>
#include <fixpp/session/retrieve_visitor.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <future>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "support/extract_tag.hpp"
#include "support/minimal_security_profile.hpp"
#include "support/pump_until_ready.hpp"
#include "support/transport_double.hpp"
#include "support/validation_test_dictionary.hpp"

using namespace std::chrono_literals;

namespace fixpp::session::test {
namespace {

using fixpp::test_support::extract_tag;

// Wraps `body` (every field after BodyLength) in 8, 9 and 10, with a correct
// BodyLength(9) and CheckSum(10), so the frame passes the Framer whatever the body holds.
std::vector<std::byte> wrap_body(std::string const& body) {
    std::string full = "8=FIX.4.2\x01";
    full += "9=" + std::to_string(body.size()) + "\x01";
    full += body;
    unsigned int cs = 0;
    for (unsigned char c : full) {
        cs += c;
    }
    cs &= 0xFFU;
    char csbuf[5];
    std::snprintf(csbuf, sizeof(csbuf), "%03u", cs);
    full += "10=" + std::string(csbuf) + "\x01";

    std::vector<std::byte> frame;
    frame.reserve(full.size());
    for (char c : full) {
        frame.push_back(static_cast<std::byte>(c));
    }
    return frame;
}

// A SOH-delimited FIX frame whose field 3 is 35, then 34 and the peer's header fields.
std::vector<std::byte> make_raw_frame(std::string_view msg_type, std::uint32_t seq,
                                      std::string const& extra_body = {}) {
    std::string body;
    body += "35=" + std::string(msg_type) + "\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=TW\x01";
    body += "52=20240101-00:00:00.000\x01";
    body += "56=ISLD\x01";
    body += extra_body;
    return wrap_body(body);
}

bool has_field(std::span<const std::byte> frame, std::string_view tag_eq) {
    std::string const wire(reinterpret_cast<const char*>(frame.data()), frame.size());
    return wire.find("\x01" + std::string(tag_eq)) != std::string::npos;
}

struct DispositionFixture {
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine;
    TransportDouble transport;

    DispositionFixture() {
        using namespace std::chrono;
        auto utc = system_clock::time_point{} + seconds{1704067200};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, fixpp::core::steady_time_point{},
                                                          ioc.get_executor());
        engine.clock = clock;
        engine.executor = ioc.get_executor();
    }

    SessionConfig make_cfg(bool validate) {
        SessionConfig cfg;
        cfg.sender_comp_id = "ISLD";
        cfg.target_comp_id = "TW";
        cfg.begin_string = "FIX.4.2";
        cfg.heartbeat_interval = 30s;
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = fixpp::test_support::make_validation_test_dictionary();
        cfg.executor_override = ioc.get_executor();
        cfg.transport_send = [this](std::span<const std::byte> frame) {
            transport.capture_outbound(frame);
        };
        cfg.reset_seqnum_policy_field = reset_seqnum_policy::bilateral_lenient;
        cfg.validate_inbound_messages = validate;
        return cfg;
    }

    // open() alone: an initiator is then in LogonSent, an acceptor in NotConnected.
    void open_only(Session& sess) {
        transport.reset();
        auto fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, 200ms)) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock,
                                                            "DispositionFixture::open_only");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss << "DispositionFixture::open_only";
            return;
        }
        ASSERT_TRUE(fut.get().has_value()) << "open() failed";
    }

    // Initiator path: open() sends a Logon; the peer's Logon reply makes it Active.
    void open_to_active(Session& sess) {
        transport.reset();
        auto fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, 200ms)) {
            fixpp::test_support::cancel_and_drain_or_report(
                ioc, *clock, "DispositionFixture::open_to_active/open");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss
                          << "DispositionFixture::open_to_active/open";
            return;
        }
        ASSERT_TRUE(fut.get().has_value()) << "open() failed";

        auto logon = make_raw_frame("A", 1,
                                    "98=0\x01"
                                    "108=30\x01");
        transport.reset();
        auto fut2 = asio::co_spawn(ioc, sess.on_inbound_frame(logon), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut2, 200ms)) {
            fixpp::test_support::cancel_and_drain_or_report(
                ioc, *clock, "DispositionFixture::open_to_active/logon");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss
                          << "DispositionFixture::open_to_active/logon";
            return;
        }
        ASSERT_TRUE(fut2.get().has_value());
        ASSERT_EQ(sess.state(), fsm_state::Active);
    }

    // Feeds one frame; transport.sent_frames() then holds only what it drew.
    void feed(Session& sess, std::span<const std::byte> frame) {
        transport.reset();
        auto fut = asio::co_spawn(ioc, sess.on_inbound_frame(frame), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, 200ms)) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock,
                                                            "DispositionFixture::feed");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss << "DispositionFixture::feed";
            return;
        }
        (void)fut.get();
    }

    [[nodiscard]] std::vector<std::vector<std::byte>> sent_of_type(std::string_view mt) const {
        std::vector<std::vector<std::byte>> out;
        for (auto const& frame : transport.sent_frames()) {
            if (extract_tag(frame, 35) == mt) {
                out.push_back(frame);
            }
        }
        return out;
    }
};

// The expected Reject for the SequenceReset: 373, and 371 (empty = must be absent).
struct ExpectedReject {
    std::string_view reason;
    std::string_view ref_tag;
};

// Feeds the conformant Heartbeat probe at MsgSeqNum 500 and reports whether it drew a
// ResendRequest(35=2). Every cell below reads the probe through this one function, so
// the control cell exercises the same observation the reproducer cells rely on.
bool probe_draws_resend(DispositionFixture& fix, Session& sess) {
    auto const probe = make_raw_frame("0", 500);
    fix.feed(sess, probe);
    return !fix.sent_of_type("2").empty();
}

// SequenceReset (Reset mode, NewSeqNo=500) at the expected MsgSeqNum 2 carrying
// `garbled`, then a conformant Heartbeat probe at MsgSeqNum 500.
void run_issue507_cell(std::string const& garbled, bool validate, ExpectedReject expected) {
    DispositionFixture fix;
    Session sess{fix.engine, fix.make_cfg(validate)};
    fix.open_to_active(sess);
    if (::testing::Test::HasFatalFailure()) {
        return;
    }

    auto const seq_reset =
        make_raw_frame("4", 2, std::string{"123=Z\x01"} + "36=500\x01" + garbled);
    fix.feed(sess, seq_reset);

    auto const rejects = fix.sent_of_type("3");
    EXPECT_EQ(rejects.size(), 1U) << "SequenceReset: expected exactly one Reject(35=3), got "
                                  << rejects.size();
    if (!rejects.empty()) {
        auto const& r = rejects.front();
        EXPECT_EQ(extract_tag(r, 45), "2") << "Reject RefSeqNum(45)";
        EXPECT_EQ(extract_tag(r, 372), "4") << "Reject RefMsgType(372)";
        EXPECT_EQ(extract_tag(r, 373), expected.reason) << "Reject SessionRejectReason(373)";
        if (expected.ref_tag.empty()) {
            EXPECT_FALSE(has_field(r, "371=")) << "Reject must carry no RefTagID(371)";
        } else {
            EXPECT_EQ(extract_tag(r, 371), expected.ref_tag) << "Reject RefTagID(371)";
        }
    }
    EXPECT_EQ(sess.state(), fsm_state::Active) << "state after the SequenceReset";

    bool const resend = probe_draws_resend(fix, sess);
    EXPECT_TRUE(resend) << "probe(34=500): resend=" << (resend ? 1 : 0)
                        << " (0 means NewSeqNo(36)=500 of the unparseable SequenceReset was "
                           "applied)";
}

std::string const kMalformedCount = std::string{"90=2\x01"} + "91=xyz\x01";
std::string const kMalformedTag = "9x9=1\x01";

TEST(UnparseableFrameDisposition, Issue507Reproducer_MalformedCount_ValidationOn) {
    run_issue507_cell(kMalformedCount, /*validate=*/true, {.reason = "5", .ref_tag = "90"});
}

TEST(UnparseableFrameDisposition, Issue507Reproducer_MalformedTag_ValidationOn) {
    run_issue507_cell(kMalformedTag, /*validate=*/true, {.reason = "0", .ref_tag = {}});
}

TEST(UnparseableFrameDisposition, Issue507Reproducer_MalformedCount_ValidationOff) {
    run_issue507_cell(kMalformedCount, /*validate=*/false, {.reason = "5", .ref_tag = "90"});
}

TEST(UnparseableFrameDisposition, Issue507Reproducer_MalformedTag_ValidationOff) {
    run_issue507_cell(kMalformedTag, /*validate=*/false, {.reason = "0", .ref_tag = {}});
}

// Control for the Issue507Reproducer_* cells: the same fixture and pre-state, but the
// frame at MsgSeqNum 2 is a conformant Heartbeat, so NextNumIn advances to 3 and the
// probe at 500 is a gap. It shows the probe can report a ResendRequest, so a
// reproducer cell whose probe reports none is reporting the applied reset, not a
// probe that cannot see one. To check the control itself, break
// probe_draws_resend in a scratch copy (e.g. match a MsgType the session never sends)
// and run this cell: it must fail.
TEST(UnparseableFrameDisposition, Issue507Reproducer_ProbeControl) {
    DispositionFixture fix;
    Session sess{fix.engine, fix.make_cfg(/*validate=*/true)};
    fix.open_to_active(sess);
    if (::testing::Test::HasFatalFailure()) {
        return;
    }

    auto const heartbeat = make_raw_frame("0", 2);
    fix.feed(sess, heartbeat);
    EXPECT_TRUE(fix.transport.sent_frames().empty())
        << "control: the conformant Heartbeat at MsgSeqNum 2 must draw nothing";
    EXPECT_EQ(sess.state(), fsm_state::Active) << "control: state after the Heartbeat";

    bool const resend = probe_draws_resend(fix, sess);
    EXPECT_TRUE(resend) << "control probe(34=500): resend=" << (resend ? 1 : 0)
                        << " (0 means the probe cannot report a ResendRequest)";
}

// ── Anchor_D* (tasks.md T025; contract C-2, one cell per row) ────────────────
//
// One cell per C-2 row, each on an input the pre-092 session mishandles (SC-006).
// NextNumIn is witnessed by the next conformant frame (this target reads no private
// counter, fixpp#511): a Heartbeat at the old number delivered with no outbound
// means unchanged; one at the next number delivered with no outbound means
// advanced; a gap draws a ResendRequest(35=2) whose BeginSeqNo(7) is NextNumIn.
// The Active cells register an Application, so "delivered" is a fromAdmin call and
// an application frame is never answered by the no-Application Reject (373=3).

// Counts inbound deliveries.
class CountingApplication final : public Application {
public:
    int from_admin = 0;
    int from_app = 0;

    fixpp::core::expected_t<void> fromAdmin(
        const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& /*msg*/,
        const SessionId& /*id*/) override {
        ++from_admin;
        return {};
    }
    fixpp::core::expected_t<void> fromApp(
        const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& /*msg*/,
        const SessionId& /*id*/) override {
        ++from_app;
        return {};
    }
};

// Every field of the Reject a faulty frame draws. An empty ref_tag means 371 absent.
// The Text strings are spelled out here, not taken from the session's constants, so
// a change to the emitted Text fails this check.
struct WantReject {
    std::string_view ref_seq;       // 45
    std::string_view ref_msg_type;  // 372
    std::string_view reason;        // 373
    std::string_view ref_tag;       // 371
    std::string_view text;          // 58
};

constexpr std::string_view kTextMalformedTag = "Garbled field: malformed tag";
constexpr std::string_view kTextLengthDataMismatch =
    "Garbled field: Length does not match its Data field";

// The faulty frame drew exactly one frame, a Reject carrying `want`.
void expect_only_reject(DispositionFixture const& fix, WantReject const& want,
                        std::string_view row) {
    auto const& sent = fix.transport.sent_frames();
    std::string types;
    for (auto const& f : sent) {
        types += " 35=" + extract_tag(f, 35);
    }
    EXPECT_EQ(sent.size(), 1U) << row << ": expected exactly one outbound frame (the Reject); sent:"
                               << types;
    auto const rejects = fix.sent_of_type("3");
    EXPECT_EQ(rejects.size(), 1U) << row << ": expected one Reject(35=3); sent:" << types;
    if (rejects.empty()) {
        return;
    }
    auto const& r = rejects.front();
    EXPECT_EQ(extract_tag(r, 45), want.ref_seq) << row << ": Reject RefSeqNum(45)";
    EXPECT_EQ(extract_tag(r, 372), want.ref_msg_type) << row << ": Reject RefMsgType(372)";
    EXPECT_EQ(extract_tag(r, 373), want.reason) << row << ": Reject SessionRejectReason(373)";
    if (want.ref_tag.empty()) {
        EXPECT_FALSE(has_field(r, "371=")) << row << ": Reject must carry no RefTagID(371)";
    } else {
        EXPECT_EQ(extract_tag(r, 371), want.ref_tag) << row << ": Reject RefTagID(371)";
    }
    EXPECT_EQ(extract_tag(r, 58), want.text) << row << ": Reject Text(58)";
}

// A conformant Heartbeat at `seq`, which must be in sequence: delivered to fromAdmin,
// drawing no outbound frame, and leaving the session Active.
void expect_heartbeat_in_sequence(DispositionFixture& fix, Session& sess,
                                  CountingApplication const& app, std::uint32_t seq,
                                  std::string_view row) {
    int const admin_before = app.from_admin;
    fix.feed(sess, make_raw_frame("0", seq));
    EXPECT_TRUE(fix.transport.sent_frames().empty())
        << row << ": the conformant Heartbeat at MsgSeqNum " << seq
        << " must draw nothing; ResendRequests=" << fix.sent_of_type("2").size()
        << " Logouts=" << fix.sent_of_type("5").size();
    EXPECT_EQ(app.from_admin, admin_before + 1)
        << row << ": the Heartbeat at MsgSeqNum " << seq << " must be delivered to fromAdmin";
    EXPECT_EQ(sess.state(), fsm_state::Active) << row << ": state after the Heartbeat at " << seq;
}

std::string const kHeader =
    "49=TW\x01"
    "52=20240101-00:00:00.000\x01"
    "56=ISLD\x01";

// D-1: an acceptor awaiting the Logon refuses a Logon with a malformed tag.
TEST(UnparseableFrameDisposition, Anchor_D1_NotConnected_MalformedTagLogon_Refused) {
    DispositionFixture fix;
    auto cfg = fix.make_cfg(/*validate=*/true);
    cfg.role = session_role::acceptor;
    Session sess{fix.engine, cfg};
    fix.open_only(sess);
    if (::testing::Test::HasFatalFailure()) {
        return;
    }
    ASSERT_EQ(sess.state(), fsm_state::NotConnected);

    fix.feed(sess, make_raw_frame("A", 1,
                                  "98=0\x01"
                                  "108=30\x01"
                                  "9x9=1\x01"));
    EXPECT_EQ(sess.state(), fsm_state::Disconnected) << "D-1: the faulty Logon must be refused";
    EXPECT_TRUE(fix.transport.sent_frames().empty())
        << "D-1: a refusal draws nothing; Logons sent=" << fix.sent_of_type("A").size();
}

// D-2: an initiator awaiting the Logon reply refuses a reply with a malformed tag.
TEST(UnparseableFrameDisposition, Anchor_D2_LogonSent_MalformedTagReply_Refused) {
    DispositionFixture fix;
    Session sess{fix.engine, fix.make_cfg(/*validate=*/true)};
    fix.open_only(sess);
    if (::testing::Test::HasFatalFailure()) {
        return;
    }
    ASSERT_EQ(sess.state(), fsm_state::LogonSent);

    fix.feed(sess, make_raw_frame("A", 1,
                                  "98=0\x01"
                                  "108=30\x01"
                                  "9x9=1\x01"));
    EXPECT_EQ(sess.state(), fsm_state::Disconnected) << "D-2: the faulty reply must be refused";
    EXPECT_TRUE(fix.transport.sent_frames().empty()) << "D-2: a refusal draws nothing";
}

// D-9: in LogoutSent, a faulty Logout is not taken as the reply; the logout timeout
// ends the session. The fault follows 35, so the frame does carry 35=5.
// The logout timeout is below the heartbeat interval, so the clock advance cannot
// start the liveness exchange, and the close pump's budget is half that timeout, so
// the real-time close_grace timer (armed for the same duration) cannot complete the
// close within it: only the mock-clock logout timeout can.
TEST(UnparseableFrameDisposition, Anchor_D9_LogoutSent_FaultyLogout_NotTakenAsReply) {
    DispositionFixture fix;
    auto cfg = fix.make_cfg(/*validate=*/true);
    cfg.logout_disconnect_timeout_ms = 20000;
    auto const timeout = std::chrono::milliseconds{cfg.logout_disconnect_timeout_ms};
    Session sess{fix.engine, cfg};
    fix.open_to_active(sess);
    if (::testing::Test::HasFatalFailure()) {
        return;
    }

    auto close_fut = asio::co_spawn(fix.ioc, sess.close(close_mode::graceful), asio::use_future);
    if (!fixpp::test_support::pump_until(
            fix.ioc, [&sess] { return sess.state() == fsm_state::LogoutSent; }, timeout / 2)) {
        fixpp::test_support::cancel_and_drain_or_report(fix.ioc, *fix.clock, "Anchor_D9/stage");
        ADD_FAILURE() << fixpp::test_support::kPumpBudgetMiss << "Anchor_D9/stage";
        return;
    }

    fix.feed(sess, make_raw_frame("5", 2, "9x9=1\x01"));
    EXPECT_EQ(sess.state(), fsm_state::LogoutSent)
        << "D-9: the faulty Logout must not be taken as the Logout reply";
    EXPECT_NE(close_fut.wait_for(std::chrono::seconds{0}), std::future_status::ready)
        << "D-9: close(graceful) must still await the reply or the logout timeout";
    EXPECT_TRUE(fix.transport.sent_frames().empty()) << "D-9: the faulty Logout draws nothing";

    fix.clock->advance(timeout + std::chrono::milliseconds{1});
    if (!fixpp::test_support::pump_until_ready(fix.ioc, close_fut, timeout / 2)) {
        fixpp::test_support::cancel_and_drain_or_report(fix.ioc, *fix.clock, "Anchor_D9/close");
        ADD_FAILURE() << fixpp::test_support::kPumpBudgetMiss << "Anchor_D9/close";
        return;
    }
    (void)close_fut.get();
    EXPECT_EQ(sess.state(), fsm_state::Disconnected) << "D-9: the logout timeout ends the session";
}

// Active, with a counting Application: the fixture for rows D-8 … D-6.
struct ActiveCell {
    DispositionFixture fix;
    std::shared_ptr<CountingApplication> app = std::make_shared<CountingApplication>();
    std::unique_ptr<Session> sess;

    ActiveCell() {
        fix.engine.application = app;
        sess = std::make_unique<Session>(fix.engine, fix.make_cfg(/*validate=*/true));
    }
};

// D-8: field 3 is not 35 → disregarded: nothing sent, NextNumIn unchanged, Active.
TEST(UnparseableFrameDisposition, Anchor_D8_Active_Field3Not35_Disregarded) {
    ActiveCell c;
    c.fix.open_to_active(*c.sess);
    if (::testing::Test::HasFatalFailure()) {
        return;
    }

    c.fix.feed(*c.sess, wrap_body(std::string{"49=TW\x01"} + "35=D\x01" + "34=2\x01" + "9x9=1\x01" +
                                  "52=20240101-00:00:00.000\x01" + "56=ISLD\x01"));
    EXPECT_EQ(c.sess->state(), fsm_state::Active) << "D-8: the faulty frame must not disconnect";
    EXPECT_TRUE(c.fix.transport.sent_frames().empty()) << "D-8: the faulty frame draws nothing";
    EXPECT_EQ(c.app->from_app, 0) << "D-8: the faulty frame never reaches fromApp";

    expect_heartbeat_in_sequence(c.fix, *c.sess, *c.app, 2, "D-8 (NextNumIn unchanged)");
}

// D-7: field 3 is 35 but the fault precedes 34 → disregarded, as D-8.
TEST(UnparseableFrameDisposition, Anchor_D7_Active_FaultBefore34_Disregarded) {
    ActiveCell c;
    c.fix.open_to_active(*c.sess);
    if (::testing::Test::HasFatalFailure()) {
        return;
    }

    c.fix.feed(*c.sess, wrap_body(std::string{"35=D\x01"} + "9x9=1\x01" + "34=2\x01" + kHeader));
    EXPECT_EQ(c.sess->state(), fsm_state::Active) << "D-7: the faulty frame must not disconnect";
    EXPECT_TRUE(c.fix.transport.sent_frames().empty()) << "D-7: the faulty frame draws nothing";
    EXPECT_EQ(c.app->from_app, 0) << "D-7: the faulty frame never reaches fromApp";

    expect_heartbeat_in_sequence(c.fix, *c.sess, *c.app, 2, "D-7 (NextNumIn unchanged)");
}

// D-3: a faulty Logon in Active (field 3 is 35, 34 read) → silent Disconnected:
// no Reject, no Logout.
TEST(UnparseableFrameDisposition, Anchor_D3_Active_FaultyLogon_SilentDisconnect) {
    ActiveCell c;
    c.fix.open_to_active(*c.sess);
    if (::testing::Test::HasFatalFailure()) {
        return;
    }

    c.fix.feed(*c.sess, make_raw_frame("A", 2,
                                       "98=0\x01"
                                       "108=30\x01"
                                       "9x9=1\x01"));
    EXPECT_EQ(c.sess->state(), fsm_state::Disconnected) << "D-3: the faulty Logon ends the session";
    EXPECT_TRUE(c.fix.transport.sent_frames().empty())
        << "D-3: silent; Rejects=" << c.fix.sent_of_type("3").size()
        << " Logouts=" << c.fix.sent_of_type("5").size();
    EXPECT_EQ(c.app->from_app, 0) << "D-3: the faulty Logon never reaches fromApp";
}

// D-4: a faulty SequenceReset (Reset mode, NewSeqNo=500) at N=2 → Reject, no advance,
// NewSeqNo not applied: a Heartbeat at N+1 is a gap whose ResendRequest begins at 2.
TEST(UnparseableFrameDisposition, Anchor_D4_Active_FaultySequenceReset_RejectNoAdvance) {
    ActiveCell c;
    c.fix.open_to_active(*c.sess);
    if (::testing::Test::HasFatalFailure()) {
        return;
    }

    c.fix.feed(*c.sess, make_raw_frame("4", 2,
                                       "123=N\x01"
                                       "36=500\x01"
                                       "9x9=1\x01"));
    expect_only_reject(c.fix,
                       {.ref_seq = "2",
                        .ref_msg_type = "4",
                        .reason = "0",
                        .ref_tag = {},
                        .text = kTextMalformedTag},
                       "D-4");
    EXPECT_EQ(c.sess->state(), fsm_state::Active) << "D-4: state after the SequenceReset";

    c.fix.feed(*c.sess, make_raw_frame("0", 3));
    auto const resends = c.fix.sent_of_type("2");
    EXPECT_EQ(resends.size(), 1U) << "D-4: the Heartbeat at 3 must be a gap (NextNumIn still 2); "
                                  << "ResendRequests=" << resends.size()
                                  << " Logouts=" << c.fix.sent_of_type("5").size();
    if (!resends.empty()) {
        EXPECT_EQ(extract_tag(resends.front(), 7), "2") << "D-4: ResendRequest BeginSeqNo(7)";
    }
}

// D-5: a faulty NewOrderSingle at the expected N=2 → NextNumIn consumed, then Reject;
// fromApp not invoked; a Heartbeat at N+1 is in sequence. The fault is a
// SecureDataLen(90) count not followed by SOH (length_data_mismatch).
TEST(UnparseableFrameDisposition, Anchor_D5_Active_FaultyAppAtExpected_ConsumeThenReject) {
    ActiveCell c;
    c.fix.open_to_active(*c.sess);
    if (::testing::Test::HasFatalFailure()) {
        return;
    }

    c.fix.feed(*c.sess, make_raw_frame("D", 2,
                                       "90=2\x01"
                                       "91=xyz\x01"));
    expect_only_reject(c.fix,
                       {.ref_seq = "2",
                        .ref_msg_type = "D",
                        .reason = "5",
                        .ref_tag = "90",
                        .text = kTextLengthDataMismatch},
                       "D-5");
    EXPECT_EQ(c.app->from_app, 0) << "D-5: the faulty frame never reaches fromApp";
    EXPECT_EQ(c.sess->state(), fsm_state::Active) << "D-5: state after the Reject";

    expect_heartbeat_in_sequence(c.fix, *c.sess, *c.app, 3, "D-5 (NextNumIn advanced)");
}

// D-6: a faulty NewOrderSingle at N+5 → Reject(45=N+5); no ResendRequest; NextNumIn
// unchanged, so a Heartbeat at N=2 is in sequence.
TEST(UnparseableFrameDisposition, Anchor_D6_Active_FaultyAppNotExpected_RejectNoAdvance) {
    ActiveCell c;
    c.fix.open_to_active(*c.sess);
    if (::testing::Test::HasFatalFailure()) {
        return;
    }

    c.fix.feed(*c.sess, make_raw_frame("D", 7, "9x9=1\x01"));
    expect_only_reject(c.fix,
                       {.ref_seq = "7",
                        .ref_msg_type = "D",
                        .reason = "0",
                        .ref_tag = {},
                        .text = kTextMalformedTag},
                       "D-6");
    EXPECT_EQ(c.app->from_app, 0) << "D-6: the faulty frame never reaches fromApp";
    EXPECT_EQ(c.sess->state(), fsm_state::Active) << "D-6: state after the Reject";

    expect_heartbeat_in_sequence(c.fix, *c.sess, *c.app, 2, "D-6 (NextNumIn unchanged)");
}

// ── ReplayGuard_* (T015; research R-12) ──────────────────────────────────────

// A MessageStore that keeps outbound frames and serves them to the resend walk.
// add_outbound() plants bytes at a sequence number without Session::send.
class ReplayStore final : public MessageStore {
public:
    struct Record {
        seqnum_t seq;
        std::vector<std::byte> frame;
    };

    ReplayStore() noexcept : MessageStore(flush_thunk_for<ReplayStore>()) {}

    void add_outbound(seqnum_t seq, std::vector<std::byte> frame) {
        records_.push_back({.seq = seq, .frame = std::move(frame)});
        if (seq + 1U > next_out_) {
            next_out_ = seq + 1U;
        }
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> store(
        seqnum_t seq, std::span<const std::byte> frame, direction_t dir) noexcept override {
        if (dir == direction_t::outbound) {
            add_outbound(seq, std::vector<std::byte>(frame.begin(), frame.end()));
        }
        co_return fixpp::core::expected_t<void>{};
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> retrieve(
        seqnum_t from, seqnum_t to, direction_t dir, retrieve_visitor& visitor) noexcept override {
        if (dir == direction_t::outbound) {
            for (auto& rec : records_) {
                if (rec.seq >= from && rec.seq <= to) {
                    auto r =
                        co_await visitor.on_frame(rec.seq, std::span<const std::byte>(rec.frame));
                    if (!r || *r == visit_result::stop) {
                        break;
                    }
                }
            }
        }
        co_return fixpp::core::expected_t<void>{};
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<seqnum_t>> next_seqnum(
        direction_t dir, bool increment) noexcept override {
        auto& c = (dir == direction_t::outbound) ? next_out_ : next_in_;
        seqnum_t const curr = c;
        if (increment) {
            ++c;
        }
        co_return curr;
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> reset() noexcept override {
        next_in_ = next_out_ = seqnum_min;
        co_return fixpp::core::expected_t<void>{};
    }

private:
    std::vector<Record> records_;
    seqnum_t next_out_ = seqnum_min;
    seqnum_t next_in_ = seqnum_min;
};

class ReplayStoreFactory final : public MessageStoreFactory {
public:
    ReplayStore* last_store = nullptr;  // non-owning; the Session owns the store

    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<MessageStore>> make(
        std::string_view, std::string_view, std::pmr::memory_resource*, std::size_t,
        asio::any_io_executor) noexcept override {
        auto store = std::make_unique<ReplayStore>();
        last_store = store.get();
        return store;
    }
};

std::vector<std::byte> bytes_of(std::string_view s) {
    std::vector<std::byte> out;
    out.reserve(s.size());
    for (char const c : s) {
        out.push_back(static_cast<std::byte>(c));
    }
    return out;
}

std::string printable(std::span<const std::byte> frame) {
    std::string out;
    for (std::byte const b : frame) {
        char const c = static_cast<char>(b);
        out += (c == '\x01') ? '|' : c;
    }
    return out;
}

// A stored Heartbeat whose field 2 is a non-digit tag: a fault before its 35, of a
// shape the replay builder drops rather than refuses (so without the guard the rest
// of the frame would be rebuilt and resent). The peer's ResendRequest covers exactly
// that slot, which must be gap-filled (SequenceReset-GapFill), not resent.
TEST(UnparseableFrameDisposition, ReplayGuard_StoredAdminFrameFaultInField2_GapFilledNotResent) {
    DispositionFixture fix;
    auto const factory = std::make_shared<ReplayStoreFactory>();
    auto cfg = fix.make_cfg(/*validate=*/false);
    cfg.store_factory = factory;
    Session sess{fix.engine, cfg};
    fix.open_to_active(sess);
    if (::testing::Test::HasFatalFailure()) {
        return;
    }
    ASSERT_NE(factory->last_store, nullptr);

    constexpr seqnum_t kSlot = 2;
    factory->last_store->add_outbound(kSlot, bytes_of("8=FIX.4.2\x01"
                                                      "4X=V\x01"
                                                      "35=0\x01"
                                                      "34=2\x01"
                                                      "49=ISLD\x01"
                                                      "52=20240101-00:00:00.000\x01"
                                                      "56=TW\x01"));

    fix.feed(sess, make_raw_frame("2", 2,
                                  "7=2\x01"
                                  "16=2\x01"));

    std::size_t gap_fills = 0;
    std::size_t at_slot = 0;
    for (auto const& f : fix.transport.sent_frames()) {
        if (extract_tag(f, 34) != "2") {
            continue;
        }
        ++at_slot;
        bool const gap_fill = extract_tag(f, 35) == "4" && extract_tag(f, 123) == "Y";
        EXPECT_TRUE(gap_fill) << "slot 2 must be answered only by a GapFill; sent " << printable(f);
        if (gap_fill) {
            ++gap_fills;
            EXPECT_EQ(extract_tag(f, 36), "3") << "GapFill NewSeqNo(36)";
        }
    }
    EXPECT_EQ(gap_fills, 1U) << "one GapFill at 34=2; frames at 34=2: " << at_slot;
}

}  // namespace
}  // namespace fixpp::session::test
