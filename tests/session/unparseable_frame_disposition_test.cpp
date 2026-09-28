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
// I1_* (tasks.md T027; contract C-3 I-1; spec FR-010): a faulty TestRequest,
// ResendRequest, Logout, Heartbeat or GapFill in Active, in both fault shapes, draws
// only the Reject: no handler acts on it. The section comment above StateCell states
// how each cell is held and witnessed.
//
// Matrix_* (tasks.md T028; spec SC-002, SC-003): at the expected N, in Active and in
// LogonReceived, one faulty frame per MsgType row draws exactly one Reject, and the
// next conformant frame shows whether NextNumIn advanced.
//
// D6Edge_*, WrongCompIdBeforeFault_*, TwoMalformedFields_* (tasks.md T029; spec
// clarification Q2): the D-6 edges, an identity field read before the fault, and a
// frame whose first fault decides the reason.
//
// RejectLoop_* (tasks.md T030; spec FR-003): a peer that answers each fixpp Reject with
// a malformed Reject draws one fixpp Reject per malformed frame, and none for a
// well-formed Reject.
//
// Liveness_* (tasks.md T031; spec FR-018): one faulty frame inside the first heartbeat
// interval does not refresh inbound liveness, so a TestRequest is still sent at it.
//
// ProfileRoleMatrix/*, ValidatorLive/*, RowByValidation/* (tasks.md T033; spec FR-011;
// contract C-3 I-6): the disposition does not depend on inbound validation, the profile,
// the role or whether an Application is registered.
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

#include <array>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/redirect_error.hpp>
#include <asio/steady_timer.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/version_profile.hpp>
#include <fixpp/dict/version_registry.hpp>
#include <fixpp/dict/xml_loader.hpp>
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
#include <memory_resource>
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
std::vector<std::byte> wrap_body(std::string const& body,
                                 std::string_view begin_string = "FIX.4.2") {
    std::string full = "8=" + std::string(begin_string) + "\x01";
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
                                      std::string const& extra_body = {},
                                      std::string_view begin_string = "FIX.4.2") {
    std::string body;
    body += "35=" + std::string(msg_type) + "\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=TW\x01";
    body += "52=20240101-00:00:00.000\x01";
    body += "56=ISLD\x01";
    body += extra_body;
    return wrap_body(body, begin_string);
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
void anchor_d1(bool validate) {
    DispositionFixture fix;
    auto cfg = fix.make_cfg(validate);
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

TEST(UnparseableFrameDisposition, Anchor_D1_NotConnected_MalformedTagLogon_Refused) {
    anchor_d1(/*validate=*/true);
}

// D-2: an initiator awaiting the Logon reply refuses a reply with a malformed tag.
void anchor_d2(bool validate) {
    DispositionFixture fix;
    Session sess{fix.engine, fix.make_cfg(validate)};
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

TEST(UnparseableFrameDisposition, Anchor_D2_LogonSent_MalformedTagReply_Refused) {
    anchor_d2(/*validate=*/true);
}

// D-9: in LogoutSent, a faulty Logout is not taken as the reply; the logout timeout
// ends the session. The fault follows 35, so the frame does carry 35=5.
// The logout timeout is below the heartbeat interval, so the clock advance cannot
// start the liveness exchange, and the close pump's budget is half that timeout, so
// the real-time close_grace timer (armed for the same duration) cannot complete the
// close within it: only the mock-clock logout timeout can.
void anchor_d9(bool validate) {
    DispositionFixture fix;
    auto cfg = fix.make_cfg(validate);
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

TEST(UnparseableFrameDisposition, Anchor_D9_LogoutSent_FaultyLogout_NotTakenAsReply) {
    anchor_d9(/*validate=*/true);
}

// Active, with a counting Application: the fixture for rows D-8 … D-6.
struct ActiveCell {
    DispositionFixture fix;
    std::shared_ptr<CountingApplication> app = std::make_shared<CountingApplication>();
    std::unique_ptr<Session> sess;

    explicit ActiveCell(bool validate = true) {
        fix.engine.application = app;
        sess = std::make_unique<Session>(fix.engine, fix.make_cfg(validate));
    }
};

// D-8: field 3 is not 35 → disregarded: nothing sent, NextNumIn unchanged, Active.
void anchor_d8(bool validate) {
    ActiveCell c{validate};
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

TEST(UnparseableFrameDisposition, Anchor_D8_Active_Field3Not35_Disregarded) {
    anchor_d8(/*validate=*/true);
}

// D-7: field 3 is 35 but the fault precedes 34 → disregarded, as D-8.
void anchor_d7(bool validate) {
    ActiveCell c{validate};
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

TEST(UnparseableFrameDisposition, Anchor_D7_Active_FaultBefore34_Disregarded) {
    anchor_d7(/*validate=*/true);
}

// D-3: a faulty Logon in Active (field 3 is 35, 34 read) → silent Disconnected:
// no Reject, no Logout.
void anchor_d3(bool validate) {
    ActiveCell c{validate};
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

TEST(UnparseableFrameDisposition, Anchor_D3_Active_FaultyLogon_SilentDisconnect) {
    anchor_d3(/*validate=*/true);
}

// D-4: a faulty SequenceReset (Reset mode, NewSeqNo=500) at N=2 → Reject, no advance,
// NewSeqNo not applied: a Heartbeat at N+1 is a gap whose ResendRequest begins at 2.
void anchor_d4(bool validate) {
    ActiveCell c{validate};
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

TEST(UnparseableFrameDisposition, Anchor_D4_Active_FaultySequenceReset_RejectNoAdvance) {
    anchor_d4(/*validate=*/true);
}

// D-5: a faulty NewOrderSingle at the expected N=2 → NextNumIn consumed, then Reject;
// fromApp not invoked; a Heartbeat at N+1 is in sequence. The fault is a
// SecureDataLen(90) count not followed by SOH (length_data_mismatch).
void anchor_d5(bool validate) {
    ActiveCell c{validate};
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

TEST(UnparseableFrameDisposition, Anchor_D5_Active_FaultyAppAtExpected_ConsumeThenReject) {
    anchor_d5(/*validate=*/true);
}

// D-6: a faulty NewOrderSingle at N+5 → Reject(45=N+5); no ResendRequest; NextNumIn
// unchanged, so a Heartbeat at N=2 is in sequence.
void anchor_d6(bool validate) {
    ActiveCell c{validate};
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

TEST(UnparseableFrameDisposition, Anchor_D6_Active_FaultyAppNotExpected_RejectNoAdvance) {
    anchor_d6(/*validate=*/true);
}

// ── ReplayGuard_* (T015; research R-12) ──────────────────────────────────────

// A MessageStore that keeps outbound frames and serves them to the resend walk.
// add_outbound() plants bytes at a sequence number without Session::send.
// park_outbound(seq) holds the store of outbound frame `seq` until release_parked(),
// which completes it successfully.
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

    void park_outbound(seqnum_t seq) { park_seq_ = seq; }
    [[nodiscard]] bool parked() const noexcept { return parked_ != nullptr; }
    void release_parked() {
        if (parked_ != nullptr) {
            parked_->cancel();
        }
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> store(
        seqnum_t seq, std::span<const std::byte> frame, direction_t dir) noexcept override {
        if (dir == direction_t::outbound && seq == park_seq_) {
            park_seq_ = 0;
            asio::steady_timer gate{co_await asio::this_coro::executor,
                                    asio::steady_timer::time_point::max()};
            parked_ = &gate;
            asio::error_code ec;
            co_await gate.async_wait(asio::redirect_error(asio::use_awaitable, ec));
            parked_ = nullptr;
        }
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
    seqnum_t park_seq_ = 0;
    asio::steady_timer* parked_ = nullptr;
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

// ── Phase 3 (US1): I1_*, Matrix_*, D6Edge_* and RejectLoop_* ────────────────
//
// Each cell holds the session in Active (initiator) or in LogonReceived (acceptor),
// feeds one faulty frame, and then witnesses NextNumIn with the next conformant frame.
// Expected N is 2 in both states: the peer's Logon consumed 1.
//
// LogonReceived is left inside the on_inbound_frame call that entered it, and the
// Engine awaits each on_inbound_frame, so in production no later frame reaches that
// arm. StateCell reaches it by parking the store of the acceptor's Logon reply
// (outbound seq 1) and feeding the faulty frame while the reply is parked; the Reject
// is therefore transmitted ahead of the reply. feed_faulty asserts the state before
// and after the faulty frame, so a cell that did not run in LogonReceived fails.

enum class At : std::uint8_t { active, logon_received };

// The two fault shapes, each appended after the frame's own fields, so every field a
// handler would act on precedes the fault.
struct Shape {
    std::string garble;
    std::string_view reason;   // 373
    std::string_view ref_tag;  // 371; empty = absent
    std::string_view text;     // 58
};

Shape const kTagShape{
    .garble = kMalformedTag, .reason = "0", .ref_tag = {}, .text = kTextMalformedTag};
Shape const kCountShape{
    .garble = kMalformedCount, .reason = "5", .ref_tag = "90", .text = kTextLengthDataMismatch};

WantReject want_reject(std::string_view ref_seq, std::string_view ref_msg_type,
                       Shape const& shape) {
    return {.ref_seq = ref_seq,
            .ref_msg_type = ref_msg_type,
            .reason = shape.reason,
            .ref_tag = shape.ref_tag,
            .text = shape.text};
}

struct StateCell {
    DispositionFixture fix;
    std::shared_ptr<CountingApplication> app = std::make_shared<CountingApplication>();
    std::shared_ptr<ReplayStoreFactory> factory = std::make_shared<ReplayStoreFactory>();
    std::vector<std::byte> logon_frame = make_raw_frame("A", 1,
                                                        "98=0\x01"
                                                        "108=30\x01");
    std::unique_ptr<Session> sess;
    std::future<fixpp::core::expected_t<void>> logon;  // the acceptor's parked Logon exchange
    fsm_state held;

    explicit StateCell(At at, bool validate = true)
        : held(at == At::active ? fsm_state::Active : fsm_state::LogonReceived) {
        fix.engine.application = app;
        auto cfg = fix.make_cfg(validate);
        if (at == At::logon_received) {
            cfg.role = session_role::acceptor;
        }
        cfg.store_factory = factory;
        sess = std::make_unique<Session>(fix.engine, cfg);
    }

    StateCell(StateCell const&) = delete;
    StateCell& operator=(StateCell const&) = delete;

    // A cell that stops early still completes the parked exchange, so no suspended
    // frame outlives the session it references.
    ~StateCell() {
        if (logon.valid() && factory->last_store != nullptr) {
            factory->last_store->release_parked();
            (void)fixpp::test_support::pump_until_ready(fix.ioc, logon, 2s);
        }
    }

    void enter() {
        if (held == fsm_state::Active) {
            fix.open_to_active(*sess);
            return;
        }
        fix.open_only(*sess);
        if (::testing::Test::HasFatalFailure()) {
            return;
        }
        ASSERT_NE(factory->last_store, nullptr);
        factory->last_store->park_outbound(1);
        logon = asio::co_spawn(fix.ioc, sess->on_inbound_frame(logon_frame), asio::use_future);
        ASSERT_TRUE(fixpp::test_support::pump_until(
            fix.ioc, [this] { return factory->last_store->parked(); }, 200ms))
            << "StateCell: the acceptor's Logon reply never reached the store";
        ASSERT_EQ(sess->state(), fsm_state::LogonReceived);
    }

    // LogonReceived: releases the parked reply, which completes the Logon exchange.
    void settle() {
        if (held == fsm_state::Active) {
            return;
        }
        factory->last_store->release_parked();
        ASSERT_TRUE(fixpp::test_support::pump_until_ready(fix.ioc, logon, 200ms))
            << "StateCell: the Logon exchange did not complete after the release";
        auto const r = logon.get();
        ASSERT_TRUE(r.has_value()) << "StateCell: the Logon exchange failed";
        ASSERT_EQ(sess->state(), fsm_state::Active);
    }
};

std::string_view state_name(fsm_state s) {
    return s == fsm_state::Active ? "Active" : "LogonReceived";
}

// Feeds a faulty frame: exactly one outbound frame, the Reject `want`; neither
// fromAdmin nor fromApp invoked; the state unchanged.
void feed_faulty(StateCell& c, std::vector<std::byte> const& faulty, WantReject const& want,
                 std::string_view row) {
    EXPECT_EQ(c.sess->state(), c.held) << row << ": state before the faulty frame";
    int const admin_before = c.app->from_admin;
    int const app_before = c.app->from_app;
    c.fix.feed(*c.sess, faulty);
    expect_only_reject(c.fix, want, row);
    EXPECT_EQ(c.app->from_admin, admin_before) << row << ": the faulty frame reached fromAdmin";
    EXPECT_EQ(c.app->from_app, app_before) << row << ": the faulty frame reached fromApp";
    EXPECT_EQ(c.sess->state(), c.held) << row << ": state after the faulty frame";
}

// A conformant Heartbeat at `seq` above NextNumIn: its only outbound frame must be a
// ResendRequest whose BeginSeqNo(7) is `begin`, and the session must stay Active.
void expect_heartbeat_gap(StateCell& c, std::uint32_t seq, std::string_view begin,
                          std::string_view row) {
    c.fix.feed(*c.sess, make_raw_frame("0", seq));
    auto const resends = c.fix.sent_of_type("2");
    EXPECT_EQ(c.fix.transport.sent_frames().size(), 1U)
        << row << ": the Heartbeat at " << seq
        << " must draw only a ResendRequest; Logouts=" << c.fix.sent_of_type("5").size();
    EXPECT_EQ(resends.size(), 1U) << row << ": the Heartbeat at " << seq << " must be a gap";
    if (!resends.empty()) {
        EXPECT_EQ(extract_tag(resends.front(), 7), begin) << row << ": ResendRequest BeginSeqNo(7)";
    }
    EXPECT_EQ(c.sess->state(), fsm_state::Active) << row << ": state after the Heartbeat";
}

// SC-002 and SC-003 at the expected N=2: the faulty frame of `type` carrying `fields`
// then the shape's fault draws one Reject; then a conformant Heartbeat at 3 is
// delivered with no ResendRequest (D-5 advanced), or, for a SequenceReset (D-4), is a
// gap from 2. With NewSeqNo(36)=500 applied the Heartbeat at 3 would be too low
// instead (FR-010).
void run_expected_n_cell(At at, std::string_view type, std::string const& fields,
                         Shape const& shape, bool validate = true) {
    StateCell c{at, validate};
    c.enter();
    if (::testing::Test::HasFatalFailure()) {
        return;
    }
    std::string const row = std::string{state_name(c.held)} + " 35=" + std::string{type} +
                            " at N (373=" + std::string{shape.reason} + ")";
    feed_faulty(c, make_raw_frame(type, 2, fields + shape.garble), want_reject("2", type, shape),
                row);
    c.settle();
    if (::testing::Test::HasFatalFailure()) {
        return;
    }
    if (type == "4") {
        expect_heartbeat_gap(c, 3, "2", row + " (NextNumIn unchanged)");
    } else {
        expect_heartbeat_in_sequence(c.fix, *c.sess, *c.app, 3, row + " (NextNumIn advanced)");
    }
}

// A faulty frame of `type` at `seq` != N=2 (D-6): the Reject, then a conformant
// Heartbeat at 2 is delivered with no ResendRequest (NextNumIn unchanged).
void run_not_expected_cell(At at, std::string_view type, std::uint32_t seq,
                           std::string const& fields, Shape const& shape, std::string_view what) {
    StateCell c{at};
    c.enter();
    if (::testing::Test::HasFatalFailure()) {
        return;
    }
    std::string const seq_text = std::to_string(seq);
    std::string const row =
        std::string{state_name(c.held)} + " 35=" + std::string{type} + " " + std::string{what};
    feed_faulty(c, make_raw_frame(type, seq, fields + shape.garble),
                want_reject(seq_text, type, shape), row);
    c.settle();
    if (::testing::Test::HasFatalFailure()) {
        return;
    }
    expect_heartbeat_in_sequence(c.fix, *c.sess, *c.app, 2, row + " (NextNumIn unchanged)");
}

std::string const kTestRequestFields = "112=PING\x01";
std::string const kResendRequestFields = std::string{"7=1\x01"} + "16=0\x01";
std::string const kLogoutFields = "58=bye\x01";
std::string const kGapFillFields = std::string{"123=Y\x01"} + "36=500\x01";
std::string const kRejectFields = std::string{"45=1\x01"} + "373=0\x01";
std::string const kOrderFields = "11=ORD1\x01";
std::string const kPossDupFields = std::string{"43=Y\x01"} + "122=20231231-23:59:59.000\x01";

// ── I1_* (tasks.md T027; contract C-3 I-1): no handler acts on a faulty frame ──
//
// Active, both shapes. Each handler's own fields precede the fault. The ResendRequest's
// range covers the initiator's stored Logon (outbound 1), so a handler that ran would
// answer it; "exactly one outbound frame, the Reject" is the no-reply witness for the
// TestRequest (no Heartbeat), the ResendRequest (nothing resent) and the Logout (no
// Logout reply), and the state check is the no-disconnect witness. An I1_* cell that
// runs run_expected_n_cell also serves as the Active row of the SC-002/SC-003 matrix
// for its MsgType.

TEST(UnparseableFrameDisposition, I1_Active_TestRequest_MalformedTag) {
    run_expected_n_cell(At::active, "1", kTestRequestFields, kTagShape);
}
TEST(UnparseableFrameDisposition, I1_Active_TestRequest_LengthDataMismatch) {
    run_expected_n_cell(At::active, "1", kTestRequestFields, kCountShape);
}
TEST(UnparseableFrameDisposition, I1_Active_ResendRequestAtN_MalformedTag) {
    run_expected_n_cell(At::active, "2", kResendRequestFields, kTagShape);
}
TEST(UnparseableFrameDisposition, I1_Active_ResendRequestAtN_LengthDataMismatch) {
    run_expected_n_cell(At::active, "2", kResendRequestFields, kCountShape);
}
TEST(UnparseableFrameDisposition, I1_Active_ResendRequestAboveN_MalformedTag) {
    run_not_expected_cell(At::active, "2", 5, kResendRequestFields, kTagShape, "at N+3");
}
TEST(UnparseableFrameDisposition, I1_Active_ResendRequestAboveN_LengthDataMismatch) {
    run_not_expected_cell(At::active, "2", 5, kResendRequestFields, kCountShape, "at N+3");
}
TEST(UnparseableFrameDisposition, I1_Active_Logout_MalformedTag) {
    run_expected_n_cell(At::active, "5", kLogoutFields, kTagShape);
}
TEST(UnparseableFrameDisposition, I1_Active_Logout_LengthDataMismatch) {
    run_expected_n_cell(At::active, "5", kLogoutFields, kCountShape);
}
TEST(UnparseableFrameDisposition, I1_Active_Heartbeat_MalformedTag) {
    run_expected_n_cell(At::active, "0", {}, kTagShape);
}
TEST(UnparseableFrameDisposition, I1_Active_Heartbeat_LengthDataMismatch) {
    run_expected_n_cell(At::active, "0", {}, kCountShape);
}
TEST(UnparseableFrameDisposition, I1_Active_SequenceResetGapFill_MalformedTag) {
    run_expected_n_cell(At::active, "4", kGapFillFields, kTagShape);
}
TEST(UnparseableFrameDisposition, I1_Active_SequenceResetGapFill_LengthDataMismatch) {
    run_expected_n_cell(At::active, "4", kGapFillFields, kCountShape);
}

// ── Matrix_* (tasks.md T028; spec SC-002, SC-003; contract C-2 D-4, D-5) ─────
//
// The faulty frame at the expected N must draw a Reject as its only outbound frame,
// and the conformant Heartbeat at N+1 is then delivered with no ResendRequest, except
// after a SequenceReset (D-4), where it is a gap. An Active row whose MsgType has an
// I1_* cell is covered by that cell; list the cells with --gtest_list_tests. In
// LogonReceived, StateCell parks the acceptor's Logon reply.

TEST(UnparseableFrameDisposition, Matrix_Active_Reject) {
    run_expected_n_cell(At::active, "3", kRejectFields, kTagShape);
}
TEST(UnparseableFrameDisposition, Matrix_Active_NewOrderSingle) {
    run_expected_n_cell(At::active, "D", kOrderFields, kTagShape);
}
TEST(UnparseableFrameDisposition, Matrix_LogonReceived_Reject) {
    run_expected_n_cell(At::logon_received, "3", kRejectFields, kTagShape);
}
TEST(UnparseableFrameDisposition, Matrix_LogonReceived_Logout) {
    run_expected_n_cell(At::logon_received, "5", kLogoutFields, kTagShape);
}
TEST(UnparseableFrameDisposition, Matrix_LogonReceived_SequenceReset) {
    run_expected_n_cell(At::logon_received, "4", kGapFillFields, kTagShape);
}
TEST(UnparseableFrameDisposition, Matrix_LogonReceived_Heartbeat) {
    run_expected_n_cell(At::logon_received, "0", {}, kTagShape);
}
TEST(UnparseableFrameDisposition, Matrix_LogonReceived_TestRequest) {
    run_expected_n_cell(At::logon_received, "1", kTestRequestFields, kTagShape);
}
TEST(UnparseableFrameDisposition, Matrix_LogonReceived_ResendRequest) {
    run_expected_n_cell(At::logon_received, "2", kResendRequestFields, kTagShape);
}
TEST(UnparseableFrameDisposition, Matrix_LogonReceived_NewOrderSingle) {
    run_expected_n_cell(At::logon_received, "D", kOrderFields, kTagShape);
}

// ── D6Edge_*, WrongCompId*, TwoMalformedFields_* (tasks.md T029; contract C-2
// D-5, D-6; spec clarification Q2) ──────────────────────────────────────────────
//
// D-6: a faulty NewOrderSingle below (34=1) or above (34=5) N=2, with and without
// PossDupFlag(43)=Y and an OrigSendingTime(122), both before the fault. The Reject is
// its only outbound frame (no ResendRequest, no too-low Logout), the state is
// unchanged, and the conformant Heartbeat at 2 is then in sequence (no advance).

TEST(UnparseableFrameDisposition, D6Edge_Active_TooLow) {
    run_not_expected_cell(At::active, "D", 1, kOrderFields, kTagShape, "at N-1");
}
TEST(UnparseableFrameDisposition, D6Edge_Active_TooLow_PossDup) {
    run_not_expected_cell(At::active, "D", 1, kPossDupFields + kOrderFields, kTagShape,
                          "at N-1, 43=Y");
}
TEST(UnparseableFrameDisposition, D6Edge_Active_TooHigh) {
    run_not_expected_cell(At::active, "D", 5, kOrderFields, kTagShape, "at N+3");
}
TEST(UnparseableFrameDisposition, D6Edge_Active_TooHigh_PossDup) {
    run_not_expected_cell(At::active, "D", 5, kPossDupFields + kOrderFields, kTagShape,
                          "at N+3, 43=Y");
}
TEST(UnparseableFrameDisposition, D6Edge_LogonReceived_TooLow) {
    run_not_expected_cell(At::logon_received, "D", 1, kOrderFields, kTagShape, "at N-1");
}
TEST(UnparseableFrameDisposition, D6Edge_LogonReceived_TooLow_PossDup) {
    run_not_expected_cell(At::logon_received, "D", 1, kPossDupFields + kOrderFields, kTagShape,
                          "at N-1, 43=Y");
}
TEST(UnparseableFrameDisposition, D6Edge_LogonReceived_TooHigh) {
    run_not_expected_cell(At::logon_received, "D", 5, kOrderFields, kTagShape, "at N+3");
}
TEST(UnparseableFrameDisposition, D6Edge_LogonReceived_TooHigh_PossDup) {
    run_not_expected_cell(At::logon_received, "D", 5, kPossDupFields + kOrderFields, kTagShape,
                          "at N+3, 43=Y");
}

// Clarification Q2: a SenderCompID(49) the session does not expect, read before the
// fault, is not acted on. The frame is at N=2, so it is D-5: the Reject, no Logout and
// no disconnect, then the conformant Heartbeat at 3 is in sequence.
TEST(UnparseableFrameDisposition, WrongCompIdBeforeFault_RejectOnly) {
    StateCell c{At::active};
    c.enter();
    if (::testing::Test::HasFatalFailure()) {
        return;
    }
    feed_faulty(
        c,
        wrap_body(std::string{"35=D\x01"} + "34=2\x01" + "49=WRONG\x01" +
                  "52=20240101-00:00:00.000\x01" + "56=ISLD\x01" + kOrderFields + kMalformedTag),
        want_reject("2", "D", kTagShape), "wrong 49 before the fault");
    expect_heartbeat_in_sequence(c.fix, *c.sess, *c.app, 3,
                                 "wrong 49 before the fault (NextNumIn advanced)");
}

// Two malformed fields: the first one decides 373 and 371. Both orders run, so a
// last-fault-wins record fails one of them. To check it, make scan_frame_header skip a
// malformed tag and keep scanning in a scratch copy: MalformedTagFirst must fail.
TEST(UnparseableFrameDisposition, TwoMalformedFields_LengthDataFirst) {
    Shape const both{.garble = kMalformedCount + kMalformedTag,
                     .reason = "5",
                     .ref_tag = "90",
                     .text = kTextLengthDataMismatch};
    run_expected_n_cell(At::active, "D", kOrderFields, both);
}
TEST(UnparseableFrameDisposition, TwoMalformedFields_MalformedTagFirst) {
    Shape const both{.garble = kMalformedTag + kMalformedCount,
                     .reason = "0",
                     .ref_tag = {},
                     .text = kTextMalformedTag};
    run_expected_n_cell(At::active, "D", kOrderFields, both);
}

// ── RejectLoop_* (tasks.md T030; spec FR-003; contract C-2 "A loop is bounded") ──
//
// A scripted peer that garbles every Reject it sends and rejects fixpp's Rejects: it
// opens with a faulty Heartbeat, then answers each fixpp Reject with a malformed Reject
// (45 = that Reject's MsgSeqNum) at the next in-sequence number, for kPeerRounds
// rounds. Each fixpp Reject must answer exactly one peer frame, so the counts match.
// The peer then sends a well-formed Reject, which must draw nothing: that last step is
// a control (the no-reject-loop exemption for a well-formed frame, unchanged by 092).
// To check the control, force the Active arm's fault test to true in a scratch copy:
// the well-formed Reject then draws a Reject and this step must fail.
TEST(UnparseableFrameDisposition, RejectLoop_EachFixppRejectAnswersOnePeerFrame) {
    constexpr int kPeerRounds = 3;
    StateCell c{At::active};
    c.enter();
    if (::testing::Test::HasFatalFailure()) {
        return;
    }

    std::uint32_t peer_seq = 2;
    int malformed_sent = 0;
    int fixpp_rejects = 0;
    std::string last_reject_seq;
    c.fix.feed(*c.sess, make_raw_frame("0", peer_seq, kMalformedTag));
    ++malformed_sent;
    for (int round = 0;; ++round) {
        std::string const seq_text = std::to_string(peer_seq);
        std::string const row = "round " + std::to_string(round);
        // The first fixpp Reject answers the faulty Heartbeat (372=0), each later one a
        // malformed Reject (372=3).
        expect_only_reject(c.fix, want_reject(seq_text, round == 0 ? "0" : "3", kTagShape), row);
        auto const rejects = c.fix.sent_of_type("3");
        if (rejects.empty()) {
            break;
        }
        fixpp_rejects += static_cast<int>(rejects.size());
        last_reject_seq = extract_tag(rejects.front(), 34);
        if (round == kPeerRounds) {
            break;
        }
        ++peer_seq;
        c.fix.feed(*c.sess,
                   make_raw_frame("3", peer_seq,
                                  "45=" + last_reject_seq + "\x01" + "373=0\x01" + kMalformedTag));
        ++malformed_sent;
    }
    EXPECT_EQ(fixpp_rejects, malformed_sent)
        << "each fixpp Reject must answer one malformed peer frame";

    ++peer_seq;
    c.fix.feed(*c.sess,
               make_raw_frame("3", peer_seq, "45=" + last_reject_seq + "\x01" + "373=0\x01"));
    EXPECT_TRUE(c.fix.transport.sent_frames().empty())
        << "control: a well-formed Reject must draw nothing; Rejects="
        << c.fix.sent_of_type("3").size();
    EXPECT_EQ(c.sess->state(), fsm_state::Active) << "state after the well-formed Reject";
}

// ── Liveness_* (tasks.md T031; spec FR-018; contract C-2 "Liveness") ──────────
//
// In Active, exactly one faulty frame of one row arrives inside the first heartbeat
// interval, after the clock has moved past the Logon (which seeds inbound liveness), so
// a refresh by the faulty frame would move the deadline. Every field a guard reads (34,
// 35, 49, 52, 56) precedes the fault, except in the D-7 cell, whose fault precedes 34.
// Each cell asserts the session is Active one millisecond before the interval and that
// a TestRequest(35=1) is sent at it: the interval runs from the Logon, not from the
// faulty frame. The D-6 cell then answers with a well-formed Heartbeat echoing the
// TestReqID(112) and stays Active past the grace window; the others send nothing more
// and are disconnected exactly at the grace window.
//
// A cell labelled a pin sends a frame that returns before the Active arm's liveness
// refresh on the pre-092 session as well, so reverting the disposer cannot fail it. To
// check that such a cell (or the D-7 cell) can fail, make dispose_unparseable_ write
// last_inbound_steady_ in a scratch copy and run the cell: it must fail at the
// TestRequest-at-the-interval check.
//
// Time is the mock clock's. After each advance, drain_ready runs every handler the
// advance made ready; no wall-clock window decides a cell.

// Runs every ready handler until none is left. No wall-clock bound: the mock clock's
// wake-ups are posted handlers, so after an advance they are all ready.
void drain_ready(asio::io_context& ioc) {
    ioc.restart();
    while (ioc.poll() > 0) {
        ioc.restart();
    }
    ioc.restart();
}

enum class Ending : std::uint8_t { answered, silent };

// `next_in` is NextNumIn after the faulty frame (the answering Heartbeat's 34).
void run_liveness_cell(std::vector<std::byte> const& faulty, std::uint32_t next_in, Ending ending,
                       std::string_view row) {
    DispositionFixture fix;
    auto const app = std::make_shared<CountingApplication>();
    fix.engine.application = app;
    auto const cfg = fix.make_cfg(/*validate=*/true);
    ASSERT_TRUE(cfg.heartbeat_interval.has_value());
    auto const interval =
        std::chrono::duration_cast<std::chrono::milliseconds>(*cfg.heartbeat_interval);
    auto const fault_at = interval / 3;
    Session sess{fix.engine, cfg};
    fix.open_to_active(sess);
    if (::testing::Test::HasFatalFailure()) {
        return;
    }
    drain_ready(fix.ioc);

    fix.clock->advance(fault_at);
    drain_ready(fix.ioc);
    fix.feed(sess, faulty);
    EXPECT_EQ(sess.state(), fsm_state::Active)
        << row << ": the faulty frame must not end the session";
    fix.transport.reset();

    fix.clock->advance(interval - fault_at - 1ms);
    drain_ready(fix.ioc);
    EXPECT_EQ(sess.state(), fsm_state::Active)
        << row << ": state one millisecond before the interval";
    EXPECT_TRUE(fix.sent_of_type("1").empty())
        << row << ": no TestRequest may be sent before the interval";

    fix.clock->advance(1ms);
    drain_ready(fix.ioc);
    auto const test_requests = fix.sent_of_type("1");
    EXPECT_EQ(test_requests.size(), 1U)
        << row << ": a TestRequest must be sent at the interval counted from the Logon; none means "
        << "the faulty frame refreshed inbound liveness";
    EXPECT_EQ(sess.state(), fsm_state::Active) << row << ": state at the interval";
    if (test_requests.empty()) {
        return;
    }

    if (ending == Ending::answered) {
        std::string const id = extract_tag(test_requests.front(), 112);
        fix.feed(sess, make_raw_frame("0", next_in, "112=" + id + "\x01"));
        fix.clock->advance(interval + 1ms);
        drain_ready(fix.ioc);
        EXPECT_EQ(sess.state(), fsm_state::Active)
            << row << ": the Heartbeat echoing 112=" << id
            << " answers the TestRequest, so the grace window must not disconnect";
        return;
    }
    fix.clock->advance(interval - 1ms);
    drain_ready(fix.ioc);
    EXPECT_EQ(sess.state(), fsm_state::Active)
        << row << ": state one millisecond before the grace window ends";
    fix.clock->advance(2ms);
    drain_ready(fix.ioc);
    EXPECT_EQ(sess.state(), fsm_state::Disconnected)
        << row << ": the unanswered TestRequest must end the session at the grace window";
}

// Pin (see the section comment).
TEST(UnparseableFrameDisposition, Liveness_D4_SequenceReset_TestRequestAtInterval) {
    run_liveness_cell(
        make_raw_frame("4", 2, std::string{"123=N\x01"} + "36=500\x01" + kMalformedTag), 2,
        Ending::silent, "D-4");
}

TEST(UnparseableFrameDisposition, Liveness_D5_Application_TestRequestAtInterval) {
    run_liveness_cell(make_raw_frame("D", 2, kOrderFields + kMalformedTag), 3, Ending::silent,
                      "D-5 (application)");
}

// Pin (see the section comment).
TEST(UnparseableFrameDisposition, Liveness_D5_Reject_TestRequestAtInterval) {
    run_liveness_cell(make_raw_frame("3", 2, kRejectFields + kMalformedTag), 3, Ending::silent,
                      "D-5 (Reject)");
}

// Pin (see the section comment). The one answered cell.
TEST(UnparseableFrameDisposition, Liveness_D6_TooHigh_TestRequestAnswered) {
    run_liveness_cell(make_raw_frame("D", 7, kOrderFields + kMalformedTag), 2, Ending::answered,
                      "D-6");
}

TEST(UnparseableFrameDisposition, Liveness_D7_FaultBefore34_TestRequestAtInterval) {
    run_liveness_cell(wrap_body(std::string{"35=D\x01"} + kHeader + kMalformedTag + "34=2\x01"), 2,
                      Ending::silent, "D-7");
}

// ── I-6 matrix (tasks.md T033; spec FR-011; contract C-3 I-6) ────────────────
//
// ProfileRoleMatrix: T005's SequenceReset cell (Reset mode, NewSeqNo=500, a malformed
// tag) and the D-5 TestRequest cell, each repeated with inbound validation on and off,
// on FIX.4.2, FIX.4.4 and FIXT.1.1, as acceptor and as initiator, with and without an
// Application. Every cell asserts the same disposition. NextNumIn is witnessed by a
// later gap's ResendRequest BeginSeqNo(7), which needs no Application.
// RowByValidation: one cell per C-2 row, with inbound validation on and off.
// ValidatorLive: the control that the validation axis is not vacuous: per profile, a
// fault-free NewOrderSingle missing ClOrdID(11) draws the validation Reject only when
// validation is on.

enum class Profile : std::uint8_t { fix42, fix44, fixt11 };

std::string_view begin_string_of(Profile p) {
    switch (p) {
        case Profile::fix42:
            return "FIX.4.2";
        case Profile::fix44:
            return "FIX.4.4";
        case Profile::fixt11:
            return "FIXT.1.1";
    }
    return {};
}

std::string_view profile_label(Profile p) {
    switch (p) {
        case Profile::fix42:
            return "FIX42";
        case Profile::fix44:
            return "FIX44";
        case Profile::fixt11:
            return "FIXT11";
    }
    return {};
}

void replace_once(std::string& s, std::string_view from, std::string const& to) {
    auto const pos = s.find(from);
    if (pos == std::string::npos) {
        ADD_FAILURE() << "make_profile_dictionary: text not found: " << from;
        return;
    }
    s.replace(pos, from.size(), to);
}

// The validation test dictionary for the profile. FIX.4.4 and FIXT.1.1 load it as FIX
// 4.4 (for FIXT.1.1 that is the session-layer dictionary, and the registry's one
// application version), with DefaultApplVerID(1137) declared on the Logon, so a
// validating FIXT session accepts the Logon that carries it.
std::shared_ptr<const fixpp::dict::Dictionary> make_profile_dictionary(Profile p) {
    std::string xml{fixpp::test_support::kValidationTestFix42Xml};
    if (p != Profile::fix42) {
        replace_once(xml, R"(<fix major="4" minor="2">)", R"(<fix major="4" minor="4">)");
        replace_once(xml, R"(<field number="108" name="HeartBtInt"    required="Y"/>)",
                     R"(<field number="108" name="HeartBtInt"    required="Y"/>)"
                     R"(<field number="1137" name="DefaultApplVerID" required="N"/>)");
        replace_once(xml, R"(<field number="112" name="TestReqID"    type="STRING"/>)",
                     R"(<field number="112" name="TestReqID"    type="STRING"/>)"
                     R"(<field number="1137" name="DefaultApplVerID" type="STRING"/>)");
    }
    constexpr std::size_t kBufSize = 128U * 1024U;
    auto buf = std::make_unique<std::array<std::byte, kBufSize>>();
    auto* mr = new std::pmr::monotonic_buffer_resource{buf->data(), buf->size()};
    fixpp::dict::Dictionary d = fixpp::dict::XmlLoader{}.load_from_string(xml, mr);
    auto* raw_dict = new fixpp::dict::Dictionary{std::move(d)};
    auto* raw_buf = buf.release();
    return std::shared_ptr<const fixpp::dict::Dictionary>{
        raw_dict, [mr, raw_buf](const fixpp::dict::Dictionary* d2) {
            delete d2;
            delete mr;
            delete raw_buf;
        }};
}

// A session on `profile`, driven to Active at NextNumIn 2 by the peer's Logon at 1. Its
// feeds wait for the frame's own completion and then run whatever it left ready, so no
// fixed wall-clock window is spent per frame.
struct ProfileCell {
    DispositionFixture fix;
    std::shared_ptr<CountingApplication> app;
    std::shared_ptr<const fixpp::dict::Dictionary> dict;
    std::unique_ptr<fixpp::dict::version_registry> registry;
    std::string begin_string;
    std::unique_ptr<Session> sess;

    ProfileCell(Profile profile, session_role role, bool validate, bool with_app)
        : dict(make_profile_dictionary(profile)), begin_string(begin_string_of(profile)) {
        if (with_app) {
            app = std::make_shared<CountingApplication>();
            fix.engine.application = app;
        }
        auto cfg = fix.make_cfg(validate);
        cfg.role = role;
        cfg.begin_string = begin_string;
        cfg.dictionary = dict;
        if (profile == Profile::fixt11) {
            cfg.default_appl_ver_id = fixpp::dict::application_version::v44;
            registry = std::make_unique<fixpp::dict::version_registry>(
                std::vector<std::shared_ptr<const fixpp::dict::Dictionary>>{dict});
        }
        sess = std::make_unique<Session>(fix.engine, cfg, registry.get());
    }

    ProfileCell(ProfileCell const&) = delete;
    ProfileCell& operator=(ProfileCell const&) = delete;

    [[nodiscard]] std::vector<std::byte> frame(std::string_view type, std::uint32_t seq,
                                               std::string const& fields = {}) const {
        return make_raw_frame(type, seq, fields, begin_string);
    }

    void feed(std::span<const std::byte> bytes) {
        fix.transport.reset();
        auto fut = asio::co_spawn(fix.ioc, sess->on_inbound_frame(bytes), asio::use_future);
        if (!fixpp::test_support::pump_until_ready(fix.ioc, fut, "ProfileCell::feed")) {
            fixpp::test_support::cancel_and_drain_or_report(fix.ioc, *fix.clock,
                                                            "ProfileCell::feed");
            ADD_FAILURE() << fixpp::test_support::kPumpBudgetMiss << "ProfileCell::feed";
            return;
        }
        drain_ready(fix.ioc);
        (void)fut.get();
    }

    void enter() {
        auto fut = asio::co_spawn(fix.ioc, sess->open(), asio::use_future);
        if (!fixpp::test_support::pump_until_ready(fix.ioc, fut, "ProfileCell::open")) {
            fixpp::test_support::cancel_and_drain_or_report(fix.ioc, *fix.clock,
                                                            "ProfileCell::open");
            ADD_FAILURE() << fixpp::test_support::kPumpBudgetMiss << "ProfileCell::open";
            return;
        }
        drain_ready(fix.ioc);
        ASSERT_TRUE(fut.get().has_value()) << "open() failed on " << begin_string;
        std::string logon_fields = std::string{"98=0\x01"} + "108=30\x01";
        if (begin_string == "FIXT.1.1") {
            logon_fields += "1137=6\x01";
        }
        feed(frame("A", 1, logon_fields));
        ASSERT_EQ(sess->state(), fsm_state::Active) << "the peer's Logon on " << begin_string;
    }

    [[nodiscard]] int app_deliveries() const {
        return app == nullptr ? 0 : app->from_admin + app->from_app;
    }
};

// The only outbound frame is a ResendRequest whose BeginSeqNo(7) is `begin`.
void expect_only_resend_from(ProfileCell const& c, std::string_view begin, std::string_view row) {
    EXPECT_EQ(c.fix.transport.sent_frames().size(), 1U)
        << row << ": expected only a ResendRequest; Logouts=" << c.fix.sent_of_type("5").size();
    auto const resends = c.fix.sent_of_type("2");
    EXPECT_EQ(resends.size(), 1U) << row << ": expected one ResendRequest(35=2)";
    if (!resends.empty()) {
        EXPECT_EQ(extract_tag(resends.front(), 7), begin) << row << ": ResendRequest BeginSeqNo(7)";
    }
    EXPECT_EQ(c.sess->state(), fsm_state::Active) << row << ": state after the gap";
}

struct MatrixParam {
    bool validate;
    Profile profile;
    session_role role;
    bool with_app;
};

std::vector<MatrixParam> matrix_params() {
    std::vector<MatrixParam> out;
    for (bool const validate : {true, false}) {
        for (Profile const profile : {Profile::fix42, Profile::fix44, Profile::fixt11}) {
            for (session_role const role : {session_role::acceptor, session_role::initiator}) {
                for (bool const with_app : {true, false}) {
                    out.push_back({validate, profile, role, with_app});
                }
            }
        }
    }
    return out;
}

std::string matrix_name(::testing::TestParamInfo<MatrixParam> const& info) {
    auto const& p = info.param;
    return std::string{p.validate ? "ValOn" : "ValOff"} + "_" +
           std::string{profile_label(p.profile)} + "_" +
           (p.role == session_role::acceptor ? "Acceptor" : "Initiator") + "_" +
           (p.with_app ? "App" : "NoApp");
}

class ProfileRoleMatrix : public ::testing::TestWithParam<MatrixParam> {};

// T005's cell: the faulty Reset-mode SequenceReset at N=2 draws only the Reject, and
// NewSeqNo(36)=500 is not applied: a conformant Heartbeat at 500 is a gap from 2.
TEST_P(ProfileRoleMatrix, SequenceReset_RejectNoAdvance) {
    auto const& p = GetParam();
    ProfileCell c{p.profile, p.role, p.validate, p.with_app};
    c.enter();
    if (::testing::Test::HasFatalFailure()) {
        return;
    }
    int const delivered = c.app_deliveries();
    c.feed(c.frame("4", 2, std::string{"123=Z\x01"} + "36=500\x01" + kMalformedTag));
    expect_only_reject(c.fix, want_reject("2", "4", kTagShape), "SequenceReset");
    EXPECT_EQ(c.app_deliveries(), delivered) << "the faulty SequenceReset reached the Application";
    EXPECT_EQ(c.sess->state(), fsm_state::Active) << "state after the SequenceReset";

    c.feed(c.frame("0", 500));
    expect_only_resend_from(c, "2", "Heartbeat at 500 (NewSeqNo not applied)");
}

// The D-5 TestRequest cell: the faulty TestRequest at N=2 draws only the Reject (no
// Heartbeat) and consumes 2: a Heartbeat at 3 draws nothing, and one at 10 is a gap
// from 4.
TEST_P(ProfileRoleMatrix, TestRequest_RejectAdvance) {
    auto const& p = GetParam();
    ProfileCell c{p.profile, p.role, p.validate, p.with_app};
    c.enter();
    if (::testing::Test::HasFatalFailure()) {
        return;
    }
    int const delivered = c.app_deliveries();
    c.feed(c.frame("1", 2, kTestRequestFields + kMalformedTag));
    expect_only_reject(c.fix, want_reject("2", "1", kTagShape), "TestRequest");
    EXPECT_EQ(c.app_deliveries(), delivered) << "the faulty TestRequest reached the Application";
    EXPECT_EQ(c.sess->state(), fsm_state::Active) << "state after the TestRequest";

    c.feed(c.frame("0", 3));
    EXPECT_TRUE(c.fix.transport.sent_frames().empty())
        << "the Heartbeat at 3 must draw nothing; ResendRequests="
        << c.fix.sent_of_type("2").size();
    EXPECT_EQ(c.sess->state(), fsm_state::Active) << "state after the Heartbeat at 3";

    c.feed(c.frame("0", 10));
    expect_only_resend_from(c, "4", "Heartbeat at 10 (NextNumIn 4)");
}

INSTANTIATE_TEST_SUITE_P(UnparseableFrameDisposition, ProfileRoleMatrix,
                         ::testing::ValuesIn(matrix_params()), matrix_name);

// Control for the validation axis: with validation on, a fault-free NewOrderSingle
// missing ClOrdID(11) draws the validation Reject; with it off, it is delivered.
struct ValidatorParam {
    Profile profile;
    bool validate;
};

class ValidatorLive : public ::testing::TestWithParam<ValidatorParam> {};

TEST_P(ValidatorLive, MissingRequiredField_RejectedOnlyWhenValidating) {
    auto const& p = GetParam();
    ProfileCell c{p.profile, session_role::initiator, p.validate, /*with_app=*/true};
    c.enter();
    if (::testing::Test::HasFatalFailure()) {
        return;
    }
    c.feed(c.frame("D", 2, std::string{"54=1\x01"} + "60=20240101-00:00:00\x01"));
    auto const rejects = c.fix.sent_of_type("3");
    if (p.validate) {
        EXPECT_EQ(rejects.size(), 1U) << "validation on: the missing ClOrdID(11) must be Rejected";
        if (!rejects.empty()) {
            EXPECT_EQ(extract_tag(rejects.front(), 373), "1") << "Reject SessionRejectReason(373)";
            EXPECT_EQ(extract_tag(rejects.front(), 371), "11") << "Reject RefTagID(371)";
        }
        EXPECT_EQ(c.app->from_app, 0) << "validation on: the invalid frame reached fromApp";
    } else {
        EXPECT_TRUE(rejects.empty()) << "validation off: the frame must not be Rejected";
        EXPECT_EQ(c.app->from_app, 1) << "validation off: the frame must be delivered";
    }
}

INSTANTIATE_TEST_SUITE_P(UnparseableFrameDisposition, ValidatorLive,
                         ::testing::Values(ValidatorParam{Profile::fix42, true},
                                           ValidatorParam{Profile::fix42, false},
                                           ValidatorParam{Profile::fix44, true},
                                           ValidatorParam{Profile::fix44, false},
                                           ValidatorParam{Profile::fixt11, true},
                                           ValidatorParam{Profile::fixt11, false}),
                         [](::testing::TestParamInfo<ValidatorParam> const& info) {
                             return std::string{profile_label(info.param.profile)} +
                                    (info.param.validate ? "_ValOn" : "_ValOff");
                         });

// One cell per C-2 row, with inbound validation on and off: the fault branch sits in
// front of the validate gate (C-1 step 3), so the knob cannot change the row.
enum class Row : std::uint8_t { d1, d2, d9, d8, d7, d3, d4, d5_test_request, d5_application, d6 };

struct RowParam {
    Row row;
    bool validate;
};

std::string_view row_label(Row r) {
    switch (r) {
        case Row::d1:
            return "D1";
        case Row::d2:
            return "D2";
        case Row::d9:
            return "D9";
        case Row::d8:
            return "D8";
        case Row::d7:
            return "D7";
        case Row::d3:
            return "D3";
        case Row::d4:
            return "D4";
        case Row::d5_test_request:
            return "D5_TestRequest";
        case Row::d5_application:
            return "D5_Application";
        case Row::d6:
            return "D6";
    }
    return {};
}

std::vector<RowParam> row_params() {
    std::vector<RowParam> out;
    for (Row const row : {Row::d1, Row::d2, Row::d9, Row::d8, Row::d7, Row::d3, Row::d4,
                          Row::d5_test_request, Row::d5_application, Row::d6}) {
        for (bool const validate : {true, false}) {
            out.push_back({row, validate});
        }
    }
    return out;
}

class RowByValidation : public ::testing::TestWithParam<RowParam> {};

TEST_P(RowByValidation, Disposition) {
    auto const& p = GetParam();
    switch (p.row) {
        case Row::d1:
            anchor_d1(p.validate);
            break;
        case Row::d2:
            anchor_d2(p.validate);
            break;
        case Row::d9:
            anchor_d9(p.validate);
            break;
        case Row::d8:
            anchor_d8(p.validate);
            break;
        case Row::d7:
            anchor_d7(p.validate);
            break;
        case Row::d3:
            anchor_d3(p.validate);
            break;
        case Row::d4:
            anchor_d4(p.validate);
            break;
        case Row::d5_test_request:
            run_expected_n_cell(At::active, "1", kTestRequestFields, kTagShape, p.validate);
            break;
        case Row::d5_application:
            anchor_d5(p.validate);
            break;
        case Row::d6:
            anchor_d6(p.validate);
            break;
    }
}

INSTANTIATE_TEST_SUITE_P(UnparseableFrameDisposition, RowByValidation,
                         ::testing::ValuesIn(row_params()),
                         [](::testing::TestParamInfo<RowParam> const& info) {
                             return std::string{row_label(info.param.row)} +
                                    (info.param.validate ? "_ValOn" : "_ValOff");
                         });

}  // namespace
}  // namespace fixpp::session::test
