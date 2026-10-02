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
// NextNumIn did not move to 500; this target reads no private counter.
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
// App_*, I2_*, Dup34_*, Dup35_*, AwaitingResend_*, MaxMessageSize_*, RefMsgTypeBound_*
// (tasks.md T034; spec FR-005, FR-013; contract C-3 I-2, I-3): a faulty application
// message is rejected, never delivered, and accounted as C-2's D-5/D-6 say. The section
// comment above App_AtN_MalformedTag states each cell.
//
// ReplayGuard_* (tasks.md T015; research R-12): the resend store walk classifies a
// stored frame by the header scan's MsgType. The scan stops at its first fault, so
// a stored admin frame with a fault before its 35 scans with no MsgType; such a
// slot is gap-filled, never rebuilt and resent as application data
// [FIX-SL §4.8.3]. The store is a custom MessageStore, which may hold bytes fixpp
// did not write.
//
// LateSite_* (tasks.md T038; contract C-6; spec FR-016, SC-008): one cell per late
// inbound parse site, each with a frame the header scan finds fault-free but the parse
// cannot index. The session closes terminally, sends no Reject and does not invoke the
// parse target's receive callback; the durable NextNumIn a reconnect resumes from is
// pinned per site (contract C-5 L-6). The section comment above LateKnobs states each
// cell.
//
// ScriptedPeer_* (tasks.md T041; spec SC-007): a scripted peer that handles fixpp's
// Rejects and ResendRequests as QuickFIX does sends a malformed application message
// too high; the resend converges and every later message is delivered. The section
// comment above ClOrdIdApplication states the script and the QuickFIX sources.
//
// LogonRefusal/*, PreActive_* (tasks.md T043; spec SC-004; contract C-2 D-1, D-2): before
// Active, a malformed-tag Logon, a faulty Heartbeat and a Logon whose field 3 is not 35
// are refused, in both roles; the Logon on every profile. The section comment above
// expect_pre_active_refusal states which cells are pins.
//
// D3_* (tasks.md T044; spec SC-004; contract C-2 D-3): a faulty Logon in Active and in
// LogonReceived, in both fault shapes, ends in a silent Disconnected. The section comment
// above sent_types states what the LogonReceived cells assert.
//
// D7_*, D8_* (tasks.md T046, T047; spec SC-005; contract C-2 D-7, D-8): in Active and in
// LogonReceived, a faulty frame whose 34 was not read or whose field 3 is not 35 draws
// nothing, and the next valid message draws a ResendRequest. The section comment above
// run_disregard_cell states what each cell asserts.
//
// D9_* (tasks.md T048; spec FR-015; contract C-2 D-9): in LogoutSent a faulty Logout is
// not taken as the Logout reply and a faulty non-Logout draws nothing; the logout
// timeout ends the session. The section comment above run_d9_cell states which cell is
// a pin.
//
// Disclosed_* (tasks.md T049; contract C-5 L-1, L-2, L-4): the disclosed outcomes of a
// replayed frame whose fault precedes 34, of a faulty GapFill during AwaitingResend,
// and of a malformed SignatureLength(93)/Signature(89) pair. The section comment above
// any_state_name states how the scripted-peer runs are pinned.
//
// GuardPrecedence_*, OffExpectedSequenceReset_*, ToAdmin_* (tasks.md T082; spec FR-002,
// FR-010, FR-013): a faulty frame is disposed of before the BeginString, TargetCompID and
// SendingTime guards; a faulty SequenceReset above or below the expected number never
// applies NewSeqNo; and the 092 Reject is passed to toAdmin. The section comment
// above each group states its cells.
//
// Anchors: specs/092-garbled-frame-reject/spec.md SC-001, FR-007;
//          contracts/unparseable-frame-disposition.md C-2 (D-4) and its Reject contents;
//          fixpp#507 (the T076 table and reproducer).

#include <gtest/gtest.h>

#include <algorithm>
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
#include <deque>
#include <filesystem>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/version_profile.hpp>
#include <fixpp/dict/version_registry.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fixpp/session/application.hpp>
#include <fixpp/session/file_store.hpp>
#include <fixpp/session/file_store_factory.hpp>
#include <fixpp/session/message_store.hpp>
#include <fixpp/session/message_store_factory.hpp>
#include <fixpp/session/retrieve_visitor.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fixpp/wire/offset_table.hpp>
#include <future>
#include <limits>
#include <map>
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
#include "support/reify_test_frame.hpp"
#include "support/temp_dir.hpp"
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
    return fixpp::test_support::assemble_frame("8=" + std::string(begin_string) + "\x01", body);
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
    return wire.contains("\x01" + std::string(tag_eq));
}

// Runs every ready handler until none is left. No wall-clock bound: the mock clock's
// wake-ups are posted handlers, so after an advance they are all ready.
void drain_ready(asio::io_context& ioc) {
    ioc.restart();
    while (ioc.poll() > 0) {
        ioc.restart();
    }
    ioc.restart();
}

// The fixture's waits pump until the future is ready, then drain_ready() runs whatever
// is still ready; no wall-clock window decides a cell (#526). That is sound while
// nothing these operations start completes on wall-clock time: the clock is the mock
// clock and the store's file_io_executor is this io_context, so every handler a window
// would have run is ready by the time drain_ready() returns. To re-check, swap each pump
// for a full window that counts handlers dispatched after readiness
// (decisions/speckit/526-531-fixed-window-pumps-tools/ in the parent repo).
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
        if (!fixpp::test_support::pump_until_ready(ioc, fut, "DispositionFixture::open_only")) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock,
                                                            "DispositionFixture::open_only");
            ADD_FAILURE() << fixpp::test_support::kPumpBudgetMiss
                          << "DispositionFixture::open_only";
            return;
        }
        drain_ready(ioc);
        ASSERT_TRUE(fut.get().has_value()) << "open() failed";
    }

    // Initiator path: open() sends a Logon; the peer's Logon reply makes it Active.
    void open_to_active(Session& sess) {
        transport.reset();
        auto fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
        if (!fixpp::test_support::pump_until_ready(ioc, fut,
                                                   "DispositionFixture::open_to_active/open")) {
            fixpp::test_support::cancel_and_drain_or_report(
                ioc, *clock, "DispositionFixture::open_to_active/open");
            ADD_FAILURE() << fixpp::test_support::kPumpBudgetMiss
                          << "DispositionFixture::open_to_active/open";
            return;
        }
        drain_ready(ioc);
        ASSERT_TRUE(fut.get().has_value()) << "open() failed";

        auto logon = make_raw_frame("A", 1,
                                    "98=0\x01"
                                    "108=30\x01");
        transport.reset();
        auto fut2 = asio::co_spawn(ioc, sess.on_inbound_frame(logon), asio::use_future);
        if (!fixpp::test_support::pump_until_ready(ioc, fut2,
                                                   "DispositionFixture::open_to_active/logon")) {
            fixpp::test_support::cancel_and_drain_or_report(
                ioc, *clock, "DispositionFixture::open_to_active/logon");
            ADD_FAILURE() << fixpp::test_support::kPumpBudgetMiss
                          << "DispositionFixture::open_to_active/logon";
            return;
        }
        drain_ready(ioc);
        ASSERT_TRUE(fut2.get().has_value());
        ASSERT_EQ(sess.state(), fsm_state::Active);
    }

    // Feeds one frame; transport.sent_frames() then holds only what it drew.
    void feed(Session& sess, std::span<const std::byte> frame) {
        transport.reset();
        auto fut = asio::co_spawn(ioc, sess.on_inbound_frame(frame), asio::use_future);
        if (!fixpp::test_support::pump_until_ready(ioc, fut, "DispositionFixture::feed")) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock,
                                                            "DispositionFixture::feed");
            ADD_FAILURE() << fixpp::test_support::kPumpBudgetMiss << "DispositionFixture::feed";
            return;
        }
        drain_ready(ioc);
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
                        << " (0 means no ResendRequest was drawn: either NewSeqNo(36)=500 of "
                           "the unparseable SequenceReset was applied, or the session is no "
                           "longer Active)";
}

// Test-fixture constants: a bad_alloc while building one before main aborts the
// test binary, which fails the run loudly.
// NOLINTBEGIN(bugprone-throwing-static-initialization,cert-err58-cpp)
std::string const kMalformedCount = std::string{"90=2\x01"} + "91=xyz\x01";
std::string const kMalformedTag = "9x9=1\x01";
// NOLINTEND(bugprone-throwing-static-initialization,cert-err58-cpp)

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

constexpr std::string_view kTextMalformedTag = "Malformed field: invalid tag";
constexpr std::string_view kTextLengthDataMismatch =
    "Malformed field: Length does not match its Data field";

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

// Test-fixture constants: a bad_alloc while building one before main aborts the
// test binary, which fails the run loudly.
// NOLINTBEGIN(bugprone-throwing-static-initialization,cert-err58-cpp)
std::string const kHeader =
    "49=TW\x01"
    "52=20240101-00:00:00.000\x01"
    "56=ISLD\x01";
// NOLINTEND(bugprone-throwing-static-initialization,cert-err58-cpp)

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
// `events` logs, in completion order, every durable inbound advance (with the
// counter after it) and every store() call (its direction, and an outbound frame's
// 35 and 45), so a cell can read what was persisted and in which order.
class ReplayStore final : public MessageStore {
public:
    struct Record {
        seqnum_t seq;
        std::vector<std::byte> frame;
    };

    enum class EventKind : std::uint8_t { inbound_advance, inbound_store, outbound_store };
    struct Event {
        EventKind kind;
        seqnum_t seq;          // inbound_advance: the counter after it; else store()'s seq
        std::string msg_type;  // outbound_store: the frame's 35
        std::string ref_seq;   // outbound_store: the frame's 45
    };
    std::vector<Event> events;

    ReplayStore() noexcept : MessageStore(flush_thunk_for<ReplayStore>()) {}

    void add_outbound(seqnum_t seq, std::vector<std::byte> frame) {
        records_.push_back({.seq = seq, .frame = std::move(frame)});
        next_out_ = std::max(next_out_, seq + 1U);
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
            events.push_back({.kind = EventKind::outbound_store,
                              .seq = seq,
                              .msg_type = extract_tag(frame, 35),
                              .ref_seq = extract_tag(frame, 45)});
        } else {
            events.push_back({.kind = EventKind::inbound_store, .seq = seq});
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
            if (dir == direction_t::inbound) {
                events.push_back({.kind = EventKind::inbound_advance, .seq = c});
            }
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

// Test-fixture constants: a bad_alloc while building one before main aborts the
// test binary, which fails the run loudly.
// NOLINTBEGIN(bugprone-throwing-static-initialization,cert-err58-cpp)
Shape const kTagShape{
    .garble = kMalformedTag, .reason = "0", .ref_tag = {}, .text = kTextMalformedTag};
Shape const kCountShape{
    .garble = kMalformedCount, .reason = "5", .ref_tag = "90", .text = kTextLengthDataMismatch};
// NOLINTEND(bugprone-throwing-static-initialization,cert-err58-cpp)

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
            (void)fixpp::test_support::pump_until_ready(fix.ioc, logon);
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
        ASSERT_TRUE(fixpp::test_support::pump_until(fix.ioc, [this] {
            return factory->last_store->parked();
        })) << "StateCell: the acceptor's Logon reply never reached the store";
        ASSERT_EQ(sess->state(), fsm_state::LogonReceived);
    }

    // LogonReceived: releases the parked reply, which completes the Logon exchange.
    void settle() {
        if (held == fsm_state::Active) {
            return;
        }
        factory->last_store->release_parked();
        ASSERT_TRUE(fixpp::test_support::pump_until_ready(fix.ioc, logon))
            << "StateCell: the Logon exchange did not complete after the release";
        auto const r = logon.get();
        ASSERT_TRUE(r.has_value()) << "StateCell: the Logon exchange failed";
        ASSERT_EQ(sess->state(), fsm_state::Active);
    }
};

std::string_view any_state_name(fsm_state s) {
    switch (s) {
        case fsm_state::NotConnected:
            return "NotConnected";
        case fsm_state::LogonSent:
            return "LogonSent";
        case fsm_state::LogonReceived:
            return "LogonReceived";
        case fsm_state::Active:
            return "Active";
        case fsm_state::LogoutSent:
            return "LogoutSent";
        case fsm_state::Disconnected:
            return "Disconnected";
    }
    return "?";
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
    std::string const row = std::string{any_state_name(c.held)} + " 35=" + std::string{type} +
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
        std::string{any_state_name(c.held)} + " 35=" + std::string{type} + " " + std::string{what};
    feed_faulty(c, make_raw_frame(type, seq, fields + shape.garble),
                want_reject(seq_text, type, shape), row);
    c.settle();
    if (::testing::Test::HasFatalFailure()) {
        return;
    }
    expect_heartbeat_in_sequence(c.fix, *c.sess, *c.app, 2, row + " (NextNumIn unchanged)");
}

// Test-fixture constants: a bad_alloc while building one before main aborts the
// test binary, which fails the run loudly.
// NOLINTBEGIN(bugprone-throwing-static-initialization,cert-err58-cpp)
std::string const kTestRequestFields = "112=PING\x01";
std::string const kResendRequestFields = std::string{"7=1\x01"} + "16=0\x01";
std::string const kLogoutFields = "58=bye\x01";
std::string const kGapFillFields = std::string{"123=Y\x01"} + "36=500\x01";
std::string const kRejectFields = std::string{"45=1\x01"} + "373=0\x01";
std::string const kOrderFields = "11=ORD1\x01";
std::string const kPossDupFields = std::string{"43=Y\x01"} + "122=20231231-23:59:59.000\x01";
// NOLINTEND(bugprone-throwing-static-initialization,cert-err58-cpp)

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
        std::string body = "45=";
        body.append(last_reject_seq).append("\x01").append("373=0\x01").append(kMalformedTag);
        c.fix.feed(*c.sess, make_raw_frame("3", peer_seq, body));
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

enum class Ending : std::uint8_t { answered, silent };

// `next_in` is NextNumIn after the faulty frame (the answering Heartbeat's 34).
void run_liveness_cell(std::vector<std::byte> const& faulty, std::uint32_t next_in, Ending ending,
                       std::string_view row) {
    DispositionFixture fix;
    auto const app = std::make_shared<CountingApplication>();
    fix.engine.application = app;
    auto const cfg = fix.make_cfg(/*validate=*/true);
    if (!cfg.heartbeat_interval.has_value()) {
        FAIL() << "precondition: the config carries a heartbeat interval";
    }
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
    // Members are destroyed in reverse order: the dictionary before the resource it
    // allocated from, the resource before the buffer it carves.
    struct Owned {
        std::unique_ptr<std::array<std::byte, kBufSize>> buf;
        std::unique_ptr<std::pmr::monotonic_buffer_resource> mr;
        std::unique_ptr<const fixpp::dict::Dictionary> dict;
    };
    auto owned = std::make_shared<Owned>();
    owned->buf = std::make_unique<std::array<std::byte, kBufSize>>();
    owned->mr = std::make_unique<std::pmr::monotonic_buffer_resource>(owned->buf->data(),
                                                                      owned->buf->size());
    owned->dict = std::make_unique<const fixpp::dict::Dictionary>(
        fixpp::dict::XmlLoader{}.load_from_string(xml, owned->mr.get()));
    return std::shared_ptr<const fixpp::dict::Dictionary>{owned, owned->dict.get()};
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

    // open() alone: an acceptor is then in NotConnected, an initiator in LogonSent.
    void open_only() {
        auto fut = asio::co_spawn(fix.ioc, sess->open(), asio::use_future);
        if (!fixpp::test_support::pump_until_ready(fix.ioc, fut, "ProfileCell::open")) {
            fixpp::test_support::cancel_and_drain_or_report(fix.ioc, *fix.clock,
                                                            "ProfileCell::open");
            ADD_FAILURE() << fixpp::test_support::kPumpBudgetMiss << "ProfileCell::open";
            return;
        }
        drain_ready(fix.ioc);
        ASSERT_TRUE(fut.get().has_value()) << "open() failed on " << begin_string;
    }

    // The fields of the peer's Logon after the header.
    [[nodiscard]] std::string logon_fields() const {
        std::string fields = std::string{"98=0\x01"} + "108=30\x01";
        if (begin_string == "FIXT.1.1") {
            fields += "1137=6\x01";
        }
        return fields;
    }

    void enter() {
        open_only();
        if (::testing::Test::HasFatalFailure()) {
            return;
        }
        feed(frame("A", 1, logon_fields()));
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
                    out.push_back({.validate = validate,
                                   .profile = profile,
                                   .role = role,
                                   .with_app = with_app});
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
            EXPECT_EQ(extract_tag(rejects.front(), 45), "2") << "Reject RefSeqNum(45)";
            EXPECT_EQ(extract_tag(rejects.front(), 372), "D") << "Reject RefMsgType(372)";
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
            out.push_back({.row = row, .validate = validate});
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

// ── App_*, I2_*, Dup*_*, AwaitingResend_*, MaxMessageSize_*, RefMsgTypeBound_*
// (tasks.md T034; spec FR-005, FR-013; contract C-2 D-5, D-6 and the Reject
// contents, C-1 step 1b, C-3 I-2, I-3; data-model E-1; research R-5) ─────────────
//
// A faulty application message, in Active, in both fault shapes.
//   App_AtN_*: at the expected N=2, fromApp is not invoked, the Reject carries 45=2,
//     and NextNumIn advances (the conformant Heartbeat at 3 is in sequence).
//   App_AboveN_*: above N, the Reject's 45 is the frame's 34 and the Reject is its one
//     outbound frame (no ResendRequest, no disconnect), it is not delivered, and
//     NextNumIn stays N.
//   I2_*: ReplayStore's event log shows what was persisted: at N, one durable
//     inbound advance and then the Reject stored with 45=N; above N, no advance and
//     the Reject; in neither case an inbound frame stored.
//   Dup34_*: the Reject is addressed from the first 34 (fault_ref_seq_num), so a
//     faulty frame whose first 34 is 99 and whose later 34 is the expected 2 is D-6
//     (45=99, no advance). Field 3 is 35, as C-2's D-6 row requires. The fault-free
//     Dup34_FaultFree_LastWins_Pin keeps today's last-wins sequencing (I-3): it pins an
//     inherited outcome; to check it can fail, make scan_frame_header's msg_seq_num
//     first-wins in a scratch copy.
//   Dup35_*: the disposition reads field 3's 35 (fault_ref_msg_type, D), never the
//     last-wins 4: NextNumIn advances (consume_rejected_seqnum_ excludes 4) and 372=D.
//   AwaitingResend_*: a too-high Heartbeat at 3 opens a gap from 2; the faulty frame at
//     2 fills it, so the gap closes, and a Heartbeat at 5 then draws a fresh
//     ResendRequest from 3 (none is sent while a gap is still open).
//   MaxMessageSize_*_Control: an oversized faulty frame in Active is disconnected with
//     nothing sent, because the negotiated MaxMessageSize(383) guard runs before the
//     state switch (C-1 step 1b). A control: to check it can fail, delete that guard in
//     a scratch copy and the cell must fail.
//   RefMsgTypeBound_*: a faulty frame at N whose MsgType is far longer than any shipped
//     MsgType draws a Reject without 372, and NextNumIn advances only together with that
//     Reject. The length below is spelled out, not derived from the session's bound.
//     To check it can fail, pass the unbounded 372 in dispose_unparseable_ in a scratch
//     copy: the Reject no longer fits its buffer, and the cell must fail on the Reject.

TEST(UnparseableFrameDisposition, App_AtN_MalformedTag) {
    run_expected_n_cell(At::active, "D", kOrderFields, kTagShape);
}
TEST(UnparseableFrameDisposition, App_AtN_LengthDataMismatch) {
    run_expected_n_cell(At::active, "D", kOrderFields, kCountShape);
}
TEST(UnparseableFrameDisposition, App_AboveN_MalformedTag) {
    run_not_expected_cell(At::active, "D", 5, kOrderFields, kTagShape, "at N+3");
}
TEST(UnparseableFrameDisposition, App_AboveN_LengthDataMismatch) {
    run_not_expected_cell(At::active, "D", 5, kOrderFields, kCountShape, "at N+3");
}

// The store events the faulty NewOrderSingle at `seq` drew (expected N=2).
void run_i2_cell(std::uint32_t seq, Shape const& shape) {
    StateCell c{At::active};
    c.enter();
    if (::testing::Test::HasFatalFailure()) {
        return;
    }
    ReplayStore* const store = c.factory->last_store;
    ASSERT_NE(store, nullptr);
    std::size_t const before = store->events.size();
    std::string const seq_text = std::to_string(seq);
    std::string const row = "I-2 35=D at " + seq_text + " (373=" + std::string{shape.reason} + ")";
    feed_faulty(c, make_raw_frame("D", seq, kOrderFields + shape.garble),
                want_reject(seq_text, "D", shape), row);

    std::vector<ReplayStore::Event> const drawn(
        store->events.begin() + static_cast<std::ptrdiff_t>(before), store->events.end());
    std::string log;
    for (auto const& e : drawn) {
        log += e.kind == ReplayStore::EventKind::inbound_advance
                   ? " advance->" + std::to_string(e.seq)
                   : (e.kind == ReplayStore::EventKind::inbound_store
                          ? " store(inbound)"
                          : " store(35=" + e.msg_type + ",45=" + e.ref_seq + ")");
    }
    std::size_t const want_events = (seq == 2) ? 2U : 1U;
    ASSERT_EQ(drawn.size(), want_events) << row << ": store events:" << log;
    std::size_t i = 0;
    if (seq == 2) {
        EXPECT_EQ(drawn[i].kind, ReplayStore::EventKind::inbound_advance)
            << row << ": the advance must be persisted first; store events:" << log;
        EXPECT_EQ(drawn[i].seq, 3U) << row << ": the durable inbound counter after it";
        ++i;
    }
    EXPECT_EQ(drawn[i].kind, ReplayStore::EventKind::outbound_store)
        << row << ": then the Reject is stored; store events:" << log;
    EXPECT_EQ(drawn[i].msg_type, "3") << row << ": the stored frame is the Reject";
    EXPECT_EQ(drawn[i].ref_seq, seq_text) << row << ": the stored Reject's 45";
}

TEST(UnparseableFrameDisposition, I2_AtN_MalformedTag) { run_i2_cell(2, kTagShape); }
TEST(UnparseableFrameDisposition, I2_AtN_LengthDataMismatch) { run_i2_cell(2, kCountShape); }
TEST(UnparseableFrameDisposition, I2_AboveN_MalformedTag) { run_i2_cell(5, kTagShape); }
TEST(UnparseableFrameDisposition, I2_AboveN_LengthDataMismatch) { run_i2_cell(5, kCountShape); }

// 35=D, then 34=99, the header, 34=2 (the expected number) and the fault.
void run_dup34_cell(Shape const& shape) {
    StateCell c{At::active};
    c.enter();
    if (::testing::Test::HasFatalFailure()) {
        return;
    }
    std::string const row = "Dup34 (373=" + std::string{shape.reason} + ")";
    feed_faulty(c,
                wrap_body(std::string{"35=D\x01"} + "34=99\x01" + kHeader + "34=2\x01" +
                          kOrderFields + shape.garble),
                want_reject("99", "D", shape), row);
    expect_heartbeat_in_sequence(c.fix, *c.sess, *c.app, 2, row + " (NextNumIn unchanged)");
}

TEST(UnparseableFrameDisposition, Dup34_FirstRead_D6_MalformedTag) { run_dup34_cell(kTagShape); }
TEST(UnparseableFrameDisposition, Dup34_FirstRead_D6_LengthDataMismatch) {
    run_dup34_cell(kCountShape);
}

// Inbound validation is off: the validation dictionary requires NewOrderSingle fields
// this frame does not carry, and the pin is about sequencing, not validation.
TEST(UnparseableFrameDisposition, Dup34_FaultFree_LastWins_Pin) {
    StateCell c{At::active, /*validate=*/false};
    c.enter();
    if (::testing::Test::HasFatalFailure()) {
        return;
    }
    for (std::uint32_t seq = 2; seq <= 4; ++seq) {
        expect_heartbeat_in_sequence(c.fix, *c.sess, *c.app, seq, "Dup34 pin (setup)");
    }
    int const app_before = c.app->from_app;
    c.fix.feed(*c.sess, wrap_body(std::string{"35=D\x01"} + "34=1\x01" + kHeader + "34=5\x01" +
                                  kOrderFields));
    EXPECT_TRUE(c.fix.transport.sent_frames().empty())
        << "Dup34 pin: the fault-free 34=1|...|34=5 at expected 5 must draw nothing; Logouts="
        << c.fix.sent_of_type("5").size() << " Rejects=" << c.fix.sent_of_type("3").size();
    EXPECT_EQ(c.app->from_app, app_before + 1) << "Dup34 pin: it must be delivered to fromApp";
    EXPECT_EQ(c.sess->state(), fsm_state::Active) << "Dup34 pin: state after it";
    expect_heartbeat_in_sequence(c.fix, *c.sess, *c.app, 6, "Dup34 pin (NextNumIn advanced)");
}

// 35=D, 34=2 (the expected number), the header, then a second 35=4 and the fault.
void run_dup35_cell(Shape const& shape) {
    StateCell c{At::active};
    c.enter();
    if (::testing::Test::HasFatalFailure()) {
        return;
    }
    std::string const row = "Dup35 (373=" + std::string{shape.reason} + ")";
    feed_faulty(
        c, wrap_body(std::string{"35=D\x01"} + "34=2\x01" + kHeader + "35=4\x01" + shape.garble),
        want_reject("2", "D", shape), row);
    expect_heartbeat_in_sequence(c.fix, *c.sess, *c.app, 3, row + " (NextNumIn advanced)");
}

TEST(UnparseableFrameDisposition, Dup35_FaultRefMsgType_D5_MalformedTag) {
    run_dup35_cell(kTagShape);
}
TEST(UnparseableFrameDisposition, Dup35_FaultRefMsgType_D5_LengthDataMismatch) {
    run_dup35_cell(kCountShape);
}

void run_awaiting_resend_cell(Shape const& shape) {
    StateCell c{At::active};
    c.enter();
    if (::testing::Test::HasFatalFailure()) {
        return;
    }
    std::string const row = "AwaitingResend (373=" + std::string{shape.reason} + ")";
    expect_heartbeat_gap(c, 3, "2", row + " (the gap opens)");
    feed_faulty(c, make_raw_frame("D", 2, kOrderFields + shape.garble),
                want_reject("2", "D", shape), row + " (the faulty frame fills it)");
    expect_heartbeat_gap(c, 5, "3", row + " (a fresh gap, so the first one closed)");
}

TEST(UnparseableFrameDisposition, AwaitingResend_FaultyFillClosesGap_MalformedTag) {
    run_awaiting_resend_cell(kTagShape);
}
TEST(UnparseableFrameDisposition, AwaitingResend_FaultyFillClosesGap_LengthDataMismatch) {
    run_awaiting_resend_cell(kCountShape);
}

void run_max_message_size_control(Shape const& shape) {
    DispositionFixture fix;
    auto app = std::make_shared<CountingApplication>();
    fix.engine.application = app;
    auto cfg = fix.make_cfg(/*validate=*/true);
    cfg.advertised_max_message_size = 256;
    Session sess{fix.engine, cfg};
    fix.open_to_active(sess);
    if (::testing::Test::HasFatalFailure()) {
        return;
    }
    std::string const row = "MaxMessageSize (373=" + std::string{shape.reason} + ")";
    auto const oversized = make_raw_frame(
        "D", 2, kOrderFields + "58=" + std::string(300, 'x') + "\x01" + shape.garble);
    ASSERT_GT(oversized.size(), 256U) << row << ": the frame must exceed the advertised size";
    fix.feed(sess, oversized);
    EXPECT_EQ(sess.state(), fsm_state::Disconnected) << row << ": the oversized frame ends it";
    EXPECT_TRUE(fix.transport.sent_frames().empty())
        << row << ": nothing is sent; Rejects=" << fix.sent_of_type("3").size();
    EXPECT_EQ(app->from_app, 0) << row << ": the oversized frame never reaches fromApp";
}

TEST(UnparseableFrameDisposition,
     MaxMessageSize_OversizedFaulty_Disconnected_Control_MalformedTag) {
    run_max_message_size_control(kTagShape);
}
TEST(UnparseableFrameDisposition,
     MaxMessageSize_OversizedFaulty_Disconnected_Control_LengthDataMismatch) {
    run_max_message_size_control(kCountShape);
}

void run_ref_msg_type_bound_cell(Shape const& shape) {
    StateCell c{At::active};
    c.enter();
    if (::testing::Test::HasFatalFailure()) {
        return;
    }
    std::string const row = "RefMsgTypeBound (373=" + std::string{shape.reason} + ")";
    std::string const long_type(600, 'Z');
    feed_faulty(c, make_raw_frame(long_type, 2, kOrderFields + shape.garble),
                want_reject("2", "", shape), row);
    for (auto const& r : c.fix.sent_of_type("3")) {
        EXPECT_FALSE(has_field(r, "372=")) << row << ": the Reject must carry no RefMsgType(372)";
    }
    expect_heartbeat_in_sequence(c.fix, *c.sess, *c.app, 3, row + " (NextNumIn advanced)");
}

TEST(UnparseableFrameDisposition, RefMsgTypeBound_LongMsgType_RejectWithout372_MalformedTag) {
    run_ref_msg_type_bound_cell(kTagShape);
}
TEST(UnparseableFrameDisposition, RefMsgTypeBound_LongMsgType_RejectWithout372_LengthDataMismatch) {
    run_ref_msg_type_bound_cell(kCountShape);
}

// ── LateSite_* (tasks.md T038; contract C-6; spec FR-016, SC-008) ────────────
//
// One cell per late inbound parse site: a parse_and_dispatch_ or validate_inbound_
// call over bytes received from the peer. Re-derive the population with contract C-6's
// command, `grep -n "parse_and_dispatch_(\|validate_inbound_(" src/session/session.cpp`,
// and classify each call by the provenance of the bytes it parses.
//
// The trigger is a real frame the header scan finds fault-free, carrying more fields
// than fixpp::wire::default_max_offset_entries. Parser<Index>::parse fails on it on every
// lane: the parse arena is exhausted where its upstream is null, and the offset-table
// cap is exceeded where it is not. So no cell needs a platform guard, and none asserts
// which error the parse returned. The filler is distinct user-defined tags.
//
// Each cell asserts the C-6 disposition: a terminal close (is_open() false: the
// Disconnected transitions that are not a close leave it true), no Reject(35=3), and
// the parse target's receive callback not invoked. onLogout from the close and a
// callback fired earlier at the site are not counted, and no cell asserts that an
// effect taken before the site's parse is absent (contract C-6).
//
// The store is a FileStore over a temporary directory. After the verdict is captured,
// the session is closed if it is still open and destroyed, which releases the store's
// lock; the store is then reopened and next_seqnum(inbound, false) read. That durable
// NextNumIn is what a reconnect over the same store resumes from, and it pins whether
// the closed-on frame was consumed (contract C-5 L-6).
//
// Every observation is non-fatal (EXPECT_*), so each clause reports in each cell.

// More fields than the offset table admits, so the parse fails on every lane.
constexpr std::size_t kLateFillerFields = fixpp::wire::default_max_offset_entries + 100;
// Few enough fields for the parse to succeed on every lane; the controls need that.
constexpr std::size_t kControlFillerFields = 100;
constexpr int kFirstFillerTag = 5000;

// `count` fields with distinct user-defined tags.
std::string filler(std::size_t count) {
    std::string out;
    for (std::size_t i = 0; i < count; ++i) {
        out += std::to_string(kFirstFillerTag + static_cast<int>(i)) + "=v\x01";
    }
    return out;
}

// Test-fixture constants: a bad_alloc while building one before main aborts the
// test binary, which fails the run loudly.
// NOLINTBEGIN(bugprone-throwing-static-initialization,cert-err58-cpp)
std::string const kLogonFields = std::string{"98=0\x01"} + "108=30\x01";
std::string const kNewOrderFields =
    std::string{"11=ORD1\x01"} + "54=1\x01" + "60=20240101-00:00:00.000\x01";
// NOLINTEND(bugprone-throwing-static-initialization,cert-err58-cpp)

// The session configuration a late-site cell varies.
struct LateKnobs {
    bool validate = false;  // validate_inbound_messages
    bool acceptor = false;
    bool redeliver_poss_dup = false;
    bool validate_sequence_numbers = true;
};

// A session over a FileStore in its own temporary directory, with a counting
// Application.
struct LateCell {
    DispositionFixture fix;
    std::shared_ptr<CountingApplication> app = std::make_shared<CountingApplication>();
    std::filesystem::path dir = fixpp::test_support::unique_temp_dir("late_site");
    std::unique_ptr<Session> sess;

    explicit LateCell(LateKnobs knobs) {
        fix.engine.application = app;
        auto cfg = fix.make_cfg(knobs.validate);
        if (knobs.acceptor) {
            cfg.role = session_role::acceptor;
        }
        cfg.redeliver_poss_dup = knobs.redeliver_poss_dup;
        cfg.validate_sequence_numbers = knobs.validate_sequence_numbers;
        cfg.store_factory = std::make_shared<FileStoreFactory>(file_cfg());
        sess = std::make_unique<Session>(fix.engine, cfg);
    }

    LateCell(LateCell const&) = delete;
    LateCell& operator=(LateCell const&) = delete;
    LateCell(LateCell&&) = delete;
    LateCell& operator=(LateCell&&) = delete;

    ~LateCell() {
        release();
        (void)fixpp::test_support::try_remove_temp_dir(dir);
    }

    [[nodiscard]] FileStore::Config file_cfg() {
        FileStore::Config fcfg;
        fcfg.directory = dir;
        fcfg.sender_comp_id = "ISLD";
        fcfg.target_comp_id = "TW";
        fcfg.file_io_executor = fix.ioc.get_executor();
        return fcfg;
    }

    // Closes the session if it is still open, then destroys it, which releases the
    // store. Run only after the cell's verdict is captured: the close writes is_open().
    void release() {
        if (!sess) {
            return;
        }
        if (sess->is_open()) {
            auto fut = asio::co_spawn(fix.ioc, sess->close(close_mode::terminal), asio::use_future);
            if (!fixpp::test_support::pump_until_ready(fix.ioc, fut, "LateCell::release")) {
                fixpp::test_support::cancel_and_drain_or_report(fix.ioc, *fix.clock,
                                                                "LateCell::release");
                ADD_FAILURE() << fixpp::test_support::kPumpBudgetMiss << "LateCell::release";
            } else {
                drain_ready(fix.ioc);
                (void)fut.get();
            }
        }
        sess.reset();
    }

    // Releases the session, reopens the store and reads its durable NextNumIn.
    // Returns 0 when the store cannot be reopened or read (reported as a failure).
    [[nodiscard]] seqnum_t durable_next_inbound() {
        release();
        FileStoreFactory factory{file_cfg()};
        auto minted =
            factory.make("ISLD", "TW", nullptr, std::size_t{1} << 30U, fix.ioc.get_executor());
        if (!minted.has_value()) {
            ADD_FAILURE() << "LateCell: the FileStore could not be reopened";
            return 0;
        }
        MessageStore& store = **minted;
        auto fut = asio::co_spawn(
            fix.ioc,
            [&store]() -> asio::awaitable<fixpp::core::expected_t<seqnum_t>> {
                co_return co_await store.next_seqnum(direction_t::inbound, false);
            },
            asio::use_future);
        if (!fixpp::test_support::pump_until_ready(fix.ioc, fut,
                                                   "LateCell::durable_next_inbound")) {
            fixpp::test_support::cancel_and_drain_or_report(fix.ioc, *fix.clock,
                                                            "LateCell::durable_next_inbound");
            ADD_FAILURE() << fixpp::test_support::kPumpBudgetMiss
                          << "LateCell::durable_next_inbound";
            return 0;
        }
        drain_ready(fix.ioc);
        auto const r = fut.get();
        if (!r.has_value()) {
            ADD_FAILURE() << "LateCell: next_seqnum(inbound, false) failed on the reopened store";
            return 0;
        }
        return *r;
    }
};

// The receive callback the frame's parse would reach.
enum class Target : std::uint8_t { from_admin, from_app };

// Feeds `frame` and asserts the C-6 disposition, then the durable NextNumIn after the
// close.
void expect_late_close(LateCell& c, std::vector<std::byte> const& frame, Target target,
                       seqnum_t durable_after, std::string_view row) {
    int const admin_before = c.app->from_admin;
    int const app_before = c.app->from_app;
    c.fix.feed(*c.sess, frame);

    // The verdict, captured before release() closes a session left open.
    bool const open_after = c.sess->is_open();
    fsm_state const state_after = c.sess->state();
    std::size_t const rejects = c.fix.sent_of_type("3").size();
    int const admin_calls = c.app->from_admin - admin_before;
    int const app_calls = c.app->from_app - app_before;

    EXPECT_FALSE(open_after) << row << ": the late parse failure must close the session";
    EXPECT_EQ(static_cast<int>(state_after), static_cast<int>(fsm_state::Disconnected))
        << row << ": state after the frame";
    EXPECT_EQ(rejects, 0U) << row << ": a late parse failure sends no Reject(35=3)";
    if (target == Target::from_admin) {
        EXPECT_EQ(admin_calls, 0) << row << ": fromAdmin must not be invoked for the frame";
    } else {
        EXPECT_EQ(app_calls, 0) << row << ": fromApp must not be invoked for the frame";
    }

    EXPECT_EQ(c.durable_next_inbound(), durable_after)
        << row << ": durable NextNumIn after the close (what a reconnect resumes from)";
}

// The validate gate of the NotConnected arm: an acceptor's first Logon. The gate runs
// before check_inbound.
TEST(UnparseableFrameDisposition, LateSite_NotConnected_ValidateGate_Logon_Closes) {
    LateCell c{{.validate = true, .acceptor = true}};
    c.fix.open_only(*c.sess);
    if (::testing::Test::HasFatalFailure()) {
        return;
    }
    ASSERT_EQ(c.sess->state(), fsm_state::NotConnected);
    expect_late_close(c, make_raw_frame("A", 1, kLogonFields + filler(kLateFillerFields)),
                      Target::from_admin, 1, "NotConnected validate gate");
}

// The validate gate of the LogonSent arm: the initiator's Logon reply. The gate runs
// before check_inbound.
TEST(UnparseableFrameDisposition, LateSite_LogonSent_ValidateGate_LogonReply_Closes) {
    LateCell c{{.validate = true}};
    c.fix.open_only(*c.sess);
    if (::testing::Test::HasFatalFailure()) {
        return;
    }
    ASSERT_EQ(c.sess->state(), fsm_state::LogonSent);
    expect_late_close(c, make_raw_frame("A", 1, kLogonFields + filler(kLateFillerFields)),
                      Target::from_admin, 1, "LogonSent validate gate");
}

// The validate gate of the LogonReceived/Active arm, on a GapFill at the expected
// number. The gate runs before check_inbound. The cell must reach no late site but the
// gate, or a missing gate close goes unnoticed: a frame whose path past the
// gate has a parse_and_dispatch_ call (an application message, a Heartbeat) is closed
// there instead. To re-check the GapFill, read the GapFill branch of the
// LogonReceived/Active arm with validate_sequence_numbers on for such a call.
TEST(UnparseableFrameDisposition, LateSite_Active_ValidateGate_GapFill_Closes) {
    LateCell c{{.validate = true}};
    c.fix.open_to_active(*c.sess);
    if (::testing::Test::HasFatalFailure()) {
        return;
    }
    expect_late_close(c, make_raw_frame("4", 2, kGapFillFields + filler(kLateFillerFields)),
                      Target::from_admin, 2, "Active validate gate");
}

// fromAdmin on a Reset-mode SequenceReset, which is handled before the seqnum check.
// Its NewSeqNo jump is not persisted.
TEST(UnparseableFrameDisposition, LateSite_Active_SequenceResetResetMode_FromAdmin_Closes) {
    LateCell c{{}};
    c.fix.open_to_active(*c.sess);
    if (::testing::Test::HasFatalFailure()) {
        return;
    }
    expect_late_close(c, make_raw_frame("4", 2, "36=500\x01" + filler(kLateFillerFields)),
                      Target::from_admin, 2, "SequenceReset (Reset mode) fromAdmin");
}

// fromApp redelivering a too-low PossDup application message (redeliver_poss_dup).
// A too-low message does not advance NextNumIn.
TEST(UnparseableFrameDisposition, LateSite_Active_TooLowPossDupRedeliver_FromApp_Closes) {
    LateCell c{{.redeliver_poss_dup = true}};
    c.fix.open_to_active(*c.sess);
    if (::testing::Test::HasFatalFailure()) {
        return;
    }
    expect_late_close(
        c, make_raw_frame("D", 1, kPossDupFields + kNewOrderFields + filler(kLateFillerFields)),
        Target::from_app, 2, "too-low PossDup redeliver fromApp");
}

// fromApp delivering an out-of-sequence message with validate_sequence_numbers off.
// The out-of-sequence message does not advance NextNumIn.
TEST(UnparseableFrameDisposition, LateSite_Active_SeqCheckOff_OutOfSequence_FromApp_Closes) {
    LateCell c{{.validate_sequence_numbers = false}};
    c.fix.open_to_active(*c.sess);
    if (::testing::Test::HasFatalFailure()) {
        return;
    }
    expect_late_close(c, make_raw_frame("D", 10, kNewOrderFields + filler(kLateFillerFields)),
                      Target::from_app, 2, "out-of-sequence fromApp (sequence check off)");
}

// fromAdmin on an in-sequence GapFill with validate_sequence_numbers off. The seqnum
// check advanced the counter in memory before the parse; its persist follows the
// dispatch.
TEST(UnparseableFrameDisposition, LateSite_Active_SeqCheckOff_GapFill_FromAdmin_Closes) {
    LateCell c{{.validate_sequence_numbers = false}};
    c.fix.open_to_active(*c.sess);
    if (::testing::Test::HasFatalFailure()) {
        return;
    }
    expect_late_close(c, make_raw_frame("4", 2, kGapFillFields + filler(kLateFillerFields)),
                      Target::from_admin, 2, "GapFill fromAdmin (sequence check off)");
}

// fromAdmin on an in-sequence Logout. The confirming Logout is sent before the parse
// and is not counted. The Logout's persist follows the dispatch.
TEST(UnparseableFrameDisposition, LateSite_Active_Logout_FromAdmin_Closes) {
    LateCell c{{}};
    c.fix.open_to_active(*c.sess);
    if (::testing::Test::HasFatalFailure()) {
        return;
    }
    expect_late_close(c, make_raw_frame("5", 2, filler(kLateFillerFields)), Target::from_admin, 2,
                      "Logout fromAdmin");
}

// fromAdmin on an in-sequence Heartbeat in Active. Its persist follows the dispatch.
TEST(UnparseableFrameDisposition, LateSite_Active_Heartbeat_FromAdmin_Closes) {
    LateCell c{{}};
    c.fix.open_to_active(*c.sess);
    if (::testing::Test::HasFatalFailure()) {
        return;
    }
    expect_late_close(c, make_raw_frame("0", 2, filler(kLateFillerFields)), Target::from_admin, 2,
                      "Heartbeat fromAdmin");
}

// fromApp on an in-sequence application message in Active. Its persist follows the
// dispatch.
TEST(UnparseableFrameDisposition, LateSite_Active_NewOrderSingle_FromApp_Closes) {
    LateCell c{{}};
    c.fix.open_to_active(*c.sess);
    if (::testing::Test::HasFatalFailure()) {
        return;
    }
    expect_late_close(c, make_raw_frame("D", 2, kNewOrderFields + filler(kLateFillerFields)),
                      Target::from_app, 2, "NewOrderSingle fromApp");
}

// Controls for the LateSite_* cells: the same filler with a field count below every
// parse ceiling parses. At a dispatch site the message is delivered and consumed, and
// the durable read reports the advance. At the Active validate gate the validator runs
// over the parsed frame and rejects the first filler tag, which the dictionary does not
// define for the message. So a LateSite_* frame differs from a parseable one only in
// its field count, and the durable read can report an advance.
TEST(UnparseableFrameDisposition, LateSite_Control_BelowCeiling_FromApp_DeliveredAndConsumed) {
    LateCell c{{}};
    c.fix.open_to_active(*c.sess);
    if (::testing::Test::HasFatalFailure()) {
        return;
    }
    int const app_before = c.app->from_app;
    c.fix.feed(*c.sess, make_raw_frame("D", 2, kNewOrderFields + filler(kControlFillerFields)));
    EXPECT_TRUE(c.sess->is_open()) << "control: the session stays open";
    EXPECT_EQ(c.sess->state(), fsm_state::Active) << "control: state after the frame";
    EXPECT_EQ(c.app->from_app, app_before + 1) << "control: fromApp is invoked";
    EXPECT_TRUE(c.fix.transport.sent_frames().empty()) << "control: nothing is sent";
    EXPECT_EQ(c.durable_next_inbound(), 3U) << "control: the message is consumed durably";
}

TEST(UnparseableFrameDisposition, LateSite_Control_BelowCeiling_ValidateGate_ParsedAndValidated) {
    LateCell c{{.validate = true}};
    c.fix.open_to_active(*c.sess);
    if (::testing::Test::HasFatalFailure()) {
        return;
    }
    int const app_before = c.app->from_app;
    c.fix.feed(*c.sess, make_raw_frame("D", 2, kNewOrderFields + filler(kControlFillerFields)));
    expect_only_reject(
        c.fix, {.ref_seq = "2", .ref_msg_type = "D", .reason = "2", .ref_tag = "5000", .text = ""},
        "control: the validator's Reject of the first filler tag");
    EXPECT_TRUE(c.sess->is_open()) << "control: the session stays open";
    EXPECT_EQ(c.sess->state(), fsm_state::Active) << "control: state after the frame";
    EXPECT_EQ(c.app->from_app, app_before) << "control: a rejected message is not delivered";
}

// ── ScriptedPeer_* (tasks.md T041; spec SC-007; quickstart §2 "Scripted peer") ──
//
// An in-process peer drives an Active fixpp initiator (NextNumIn 2 after the peer's
// Logon). The peer's store holds a well-formed NewOrderSingle at 2 that never reaches
// fixpp, then the raw malformed NewOrderSingle at 3 (a malformed tag after every field
// a handler reads), so the malformed frame arrives too high and a gap forms. Each
// later scripted message is a well-formed NewOrderSingle at the next number, sent only
// once everything already queued has been fed. The ClOrdID(11) of each is ORD<34>.
//
// The peer handles each frame fixpp sends as QuickFIX does:
//   - a Reject(35=3): QuickFIX/J quickfix/Session.java Session.nextReject and
//     QuickFIX C++ src/C++/Session.cpp Session::nextReject run verify(reject, false,
//     true), which checks too-low but not too-high, then advance the peer's expected
//     number: QuickFIX/J only when the Reject carries it, QuickFIX C++ whenever verify
//     passes. The two agree when the Reject is in sequence, so the peer asserts that
//     every frame fixpp sends is in sequence.
//   - a ResendRequest(35=2): QuickFIX/J Session.resendMessages and QuickFIX C++
//     Session::generateRetransmits / Session::resend resend each stored application
//     message with PossDupFlag(43)=Y and OrigSendingTime(122) set to its SendingTime.
//     The peer replays its stored bytes verbatim with those two fields inserted after
//     MsgSeqNum(34), so both precede the fault. SendingTime(52) is left as stored: the
//     mock clock does not move, and 122 must not exceed 52. QuickFIX/J gap-fills a
//     stored message it cannot parse; the peer replays the malformed bytes instead, so
//     fixpp meets them again at the expected number.
//
// The run converges when the queue drains with the script exhausted. The loop is
// bounded by an iteration cap derived from the script: a converging run feeds each
// scripted frame once plus at most one replay of the whole store per scripted frame,
// so a run that reaches the cap is still being asked to resend.
//
// Every observation is non-fatal, so each clause reports.

// Records the ClOrdID(11) of every fromApp delivery, in order.
class ClOrdIdApplication final : public Application {
public:
    std::vector<std::string> delivered;
    int from_admin = 0;

    fixpp::core::expected_t<void> fromAdmin(
        const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& /*msg*/,
        const SessionId& /*id*/) override {
        ++from_admin;
        return {};
    }
    fixpp::core::expected_t<void> fromApp(
        const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& msg,
        const SessionId& /*id*/) override {
        auto const fv = msg.get(11);
        delivered.emplace_back(fv ? std::string{fv->as_string()} : std::string{"<no 11>"});
        return {};
    }
};

std::string as_text(std::span<const std::byte> frame) {
    return {reinterpret_cast<const char*>(frame.data()), frame.size()};
}

// The stored frame with PossDupFlag(43)=Y and OrigSendingTime(122) = its
// SendingTime(52) inserted after MsgSeqNum(34), re-wrapped so BodyLength(9) and
// CheckSum(10) match.
std::vector<std::byte> with_poss_dup(std::vector<std::byte> const& stored) {
    std::string const wire = as_text(stored);
    std::string const soh{"\x01"};
    auto const body_begin = wire.find(soh, wire.find(soh + "9=") + 1) + 1;
    auto const body_end = wire.rfind(soh + "10=") + 1;
    std::string body = wire.substr(body_begin, body_end - body_begin);
    auto const after_34 = body.find(soh, body.find("34=")) + 1;
    body.insert(after_34, "43=Y" + soh + "122=" + extract_tag(stored, 52) + soh);
    return wrap_body(body);
}

struct ScriptedPeer {
    std::map<std::uint32_t, std::vector<std::byte>> store;  // the peer's sent frames
    std::deque<std::vector<std::byte>> wire;                // queued for fixpp, in order
    std::uint32_t expected_from_fixpp = 2;                  // after fixpp's Logon at 1
    std::vector<std::string> out_of_sequence;               // fixpp frames not at it

    struct RejectSeen {
        std::string ref_seq;   // 45
        bool drawn_by_replay;  // the frame fed before it carried 43=Y
        std::vector<std::byte> frame;
    };
    std::vector<RejectSeen> rejects;
    std::vector<std::pair<std::string, std::string>> resend_requests;  // 7, 16

    // Stores a frame the peer sends, whether or not it reaches fixpp.
    void sent(std::vector<std::byte> const& frame) {
        store[static_cast<std::uint32_t>(std::stoul(extract_tag(frame, 34)))] = frame;
    }

    // One frame fixpp sent, in reply to `fed`.
    void on_fixpp_frame(std::vector<std::byte> const& reply, std::vector<std::byte> const& fed) {
        std::string const type = extract_tag(reply, 35);
        std::string const seq_text = extract_tag(reply, 34);
        if (seq_text != std::to_string(expected_from_fixpp)) {
            out_of_sequence.push_back("35=" + type + " 34=" + seq_text + " (expected " +
                                      std::to_string(expected_from_fixpp) + ")");
        } else {
            ++expected_from_fixpp;
        }
        if (type == "3") {
            rejects.push_back({.ref_seq = extract_tag(reply, 45),
                               .drawn_by_replay = extract_tag(fed, 43) == "Y",
                               .frame = reply});
        } else if (type == "2") {
            std::string const begin = extract_tag(reply, 7);
            std::string const end = extract_tag(reply, 16);
            resend_requests.emplace_back(begin, end);
            auto const from = static_cast<std::uint32_t>(std::stoul(begin));
            auto const to = end == "0" ? std::numeric_limits<std::uint32_t>::max()
                                       : static_cast<std::uint32_t>(std::stoul(end));
            for (auto it = store.lower_bound(from); it != store.end() && it->first <= to; ++it) {
                wire.push_back(with_poss_dup(it->second));
            }
        }
    }
};

TEST(UnparseableFrameDisposition, ScriptedPeer_MalformedTooHigh_ResendConverges_NoStall) {
    DispositionFixture fix;
    auto const app = std::make_shared<ClOrdIdApplication>();
    fix.engine.application = app;
    Session sess{fix.engine, fix.make_cfg(/*validate=*/false)};
    fix.open_to_active(sess);
    if (::testing::Test::HasFatalFailure()) {
        return;
    }

    auto const order = [](std::uint32_t seq, std::string const& garble = {}) {
        return make_raw_frame("D", seq, "11=ORD" + std::to_string(seq) + "\x01" + garble);
    };
    ScriptedPeer peer;
    peer.sent(order(2));  // lost in transit: stored, never fed to fixpp
    std::vector<std::vector<std::byte>> script;
    script.push_back(order(3, kMalformedTag));
    for (std::uint32_t seq = 4; seq <= 7; ++seq) {
        script.push_back(order(seq));
    }

    // The store never holds more than the lost frame and the scripted ones.
    std::size_t const cap = script.size() * (1 + 1 + script.size());
    std::size_t next = 0;
    std::size_t fed = 0;
    while (fed < cap && sess.state() == fsm_state::Active) {
        if (peer.wire.empty()) {
            if (next == script.size()) {
                break;
            }
            peer.sent(script[next]);
            peer.wire.push_back(script[next++]);
        }
        auto const frame = peer.wire.front();
        peer.wire.pop_front();
        fix.feed(sess, frame);
        ++fed;
        for (auto const& out : fix.transport.sent_frames()) {
            peer.on_fixpp_frame(out, frame);
        }
    }

    bool const drained = peer.wire.empty() && next == script.size();
    EXPECT_TRUE(drained) << "the scripted run must drain before the iteration cap (" << cap
                         << "); fed " << fed << ", queued " << peer.wire.size()
                         << ", scripted left " << (script.size() - next) << ", ResendRequests "
                         << peer.resend_requests.size();
    EXPECT_EQ(sess.state(), fsm_state::Active) << "state after the scripted run";

    std::string oos;
    for (auto const& o : peer.out_of_sequence) {
        oos += " [" + o + "]";
    }
    EXPECT_TRUE(peer.out_of_sequence.empty())
        << "every frame fixpp sends must carry the peer's expected number; out of sequence:" << oos;

    std::string resends;
    for (auto const& [b, e] : peer.resend_requests) {
        resends.append(" [7=").append(b).append(" 16=").append(e).append("]");
    }
    EXPECT_EQ(peer.resend_requests.size(), 1U)
        << "one ResendRequest closes the gap; a second means the replay was refused again:"
        << resends;
    if (!peer.resend_requests.empty()) {
        EXPECT_EQ(peer.resend_requests.front().first, "2") << "ResendRequest BeginSeqNo(7)";
    }

    EXPECT_EQ(peer.rejects.size(), 2U)
        << "one Reject for the raw malformed frame and one for its PossDup replay";
    if (peer.rejects.size() == 2U) {
        EXPECT_FALSE(peer.rejects[0].drawn_by_replay) << "the first Reject answers the raw frame";
        EXPECT_TRUE(peer.rejects[1].drawn_by_replay) << "the second Reject answers the replay";
    }
    for (auto const& r : peer.rejects) {
        EXPECT_EQ(r.ref_seq, "3") << "Reject RefSeqNum(45)";
        EXPECT_EQ(extract_tag(r.frame, 372), "D") << "Reject RefMsgType(372)";
        EXPECT_EQ(extract_tag(r.frame, 373), "0") << "Reject SessionRejectReason(373)";
        EXPECT_FALSE(has_field(r.frame, "371=")) << "Reject must carry no RefTagID(371)";
        EXPECT_EQ(extract_tag(r.frame, 58), kTextMalformedTag) << "Reject Text(58)";
    }

    std::vector<std::string> const want{"ORD2", "ORD4", "ORD5", "ORD6", "ORD7"};
    EXPECT_EQ(app->delivered, want)
        << "every well-formed message is delivered once, in order, and the malformed one never";
}

// ── LogonRefusal/*, PreActive_* (tasks.md T043; spec SC-004; contract C-2 D-1, D-2) ──
//
// Before Active, a frame the header scan could not read is refused: the session ends
// Disconnected, sends nothing (no Logon reply, no Reject) and delivers nothing. D-1 and
// D-2 are a state transition, not a close, so is_open() is not asserted.
//   LogonRefusal/*: a Logon whose malformed tag follows every field interpret_logon
//     reads (98, 108 and, on FIXT.1.1, 1137), as acceptor (NotConnected) and as
//     initiator (the reply, LogonSent), on FIX.4.2, FIX.4.4 and FIXT.1.1, with inbound
//     validation on and off.
//   PreActive_FaultyHeartbeat_*_Pin: a faulty Heartbeat. A pin: both pre-Active arms
//     refused every frame that is not a valid Logon before 092 (C-2's "today's
//     handling, kept"; re-derive with contract C-2's grep for "No MsgType
//     discrimination" and "out-of-scope admin"), so reverting the disposer cannot fail
//     it. To check that it can fail, drop the D-1/D-2 record_state_transition_ in
//     dispose_unparseable_ in a scratch copy: the cell must fail on the state.
//   PreActive_D8Logon_*: a full Logon whose field 3 is SenderCompID(49), not 35, with
//     the malformed tag last (the pre-Active disregard of such a frame is fixpp#514).

// The refusal: Disconnected, nothing sent, nothing delivered.
void expect_pre_active_refusal(ProfileCell const& c, int delivered_before, std::string_view row) {
    std::string types;
    for (auto const& f : c.fix.transport.sent_frames()) {
        types += " 35=" + extract_tag(f, 35);
    }
    EXPECT_EQ(c.sess->state(), fsm_state::Disconnected) << row << ": the frame must be refused";
    EXPECT_TRUE(c.fix.transport.sent_frames().empty())
        << row << ": a refusal sends nothing; sent:" << types;
    EXPECT_EQ(c.app_deliveries(), delivered_before) << row << ": the frame reached the Application";
}

// Opens a session awaiting the Logon (acceptor) or its reply (initiator) and feeds `bytes`.
void run_pre_active_cell(ProfileCell& c, session_role role, std::vector<std::byte> const& bytes,
                         std::string_view row) {
    c.open_only();
    if (::testing::Test::HasFatalFailure()) {
        return;
    }
    fsm_state const awaiting =
        role == session_role::acceptor ? fsm_state::NotConnected : fsm_state::LogonSent;
    ASSERT_EQ(c.sess->state(), awaiting) << row << ": state before the frame";
    int const delivered = c.app_deliveries();
    c.feed(bytes);
    expect_pre_active_refusal(c, delivered, row);
}

class LogonRefusal : public ::testing::TestWithParam<MatrixParam> {};

TEST_P(LogonRefusal, MalformedTagLogon_Refused) {
    auto const& p = GetParam();
    ProfileCell c{p.profile, p.role, p.validate, /*with_app=*/true};
    run_pre_active_cell(c, p.role, c.frame("A", 1, c.logon_fields() + kMalformedTag),
                        "malformed-tag Logon");
}

std::vector<MatrixParam> logon_refusal_params() {
    std::vector<MatrixParam> out;
    for (bool const validate : {true, false}) {
        for (Profile const profile : {Profile::fix42, Profile::fix44, Profile::fixt11}) {
            for (session_role const role : {session_role::acceptor, session_role::initiator}) {
                out.push_back(
                    {.validate = validate, .profile = profile, .role = role, .with_app = true});
            }
        }
    }
    return out;
}

INSTANTIATE_TEST_SUITE_P(UnparseableFrameDisposition, LogonRefusal,
                         ::testing::ValuesIn(logon_refusal_params()),
                         [](::testing::TestParamInfo<MatrixParam> const& info) {
                             auto const& p = info.param;
                             return std::string{p.validate ? "ValOn" : "ValOff"} + "_" +
                                    std::string{profile_label(p.profile)} + "_" +
                                    (p.role == session_role::acceptor ? "Acceptor" : "Initiator");
                         });

void run_pre_active_heartbeat_pin(session_role role, std::string_view row) {
    ProfileCell c{Profile::fix42, role, /*validate=*/false, /*with_app=*/true};
    run_pre_active_cell(c, role, c.frame("0", 1, kMalformedTag), row);
}

TEST(UnparseableFrameDisposition, PreActive_FaultyHeartbeat_NotConnected_Refused_Pin) {
    run_pre_active_heartbeat_pin(session_role::acceptor, "NotConnected faulty Heartbeat");
}
TEST(UnparseableFrameDisposition, PreActive_FaultyHeartbeat_LogonSent_Refused_Pin) {
    run_pre_active_heartbeat_pin(session_role::initiator, "LogonSent faulty Heartbeat");
}

void run_pre_active_d8_logon(session_role role, std::string_view row) {
    ProfileCell c{Profile::fix42, role, /*validate=*/false, /*with_app=*/true};
    run_pre_active_cell(c, role,
                        wrap_body(std::string{"49=TW\x01"} + "35=A\x01" + "34=1\x01" +
                                  "52=20240101-00:00:00.000\x01" + "56=ISLD\x01" +
                                  c.logon_fields() + kMalformedTag),
                        row);
}

TEST(UnparseableFrameDisposition, PreActive_D8Logon_NotConnected_Refused) {
    run_pre_active_d8_logon(session_role::acceptor, "NotConnected D-8 Logon");
}
TEST(UnparseableFrameDisposition, PreActive_D8Logon_LogonSent_Refused) {
    run_pre_active_d8_logon(session_role::initiator, "LogonSent D-8 Logon");
}

// ── D3_* (tasks.md T044; spec SC-004; contract C-2 D-3) ──────────────────────
//
// A faulty Logon at the expected N=2 (field 3 is 35, 34 read before the fault), in both
// fault shapes, in Active and in LogonReceived: the session ends Disconnected, and
// sends no Reject(35=3) and no Logout(35=5). D-3 is a state transition, not a close,
// so is_open() is not asserted. LogonReceived is reached through StateCell's parked
// Logon reply (the Engine awaits each on_inbound_frame, so in production no frame
// reaches that arm while the exchange is in flight). The released reply resumes the
// acceptor's Logon path in the NotConnected arm, which transitions to Active whatever
// state it resumes into, so the LogonReceived cells assert the disposer's outcome
// captured before the release, and across the release only that no Reject or Logout
// is sent.

std::string sent_types(DispositionFixture const& fix) {
    std::string types;
    for (auto const& f : fix.transport.sent_frames()) {
        types += " 35=" + extract_tag(f, 35);
    }
    return types;
}

void run_d3_cell(At at, Shape const& shape) {
    StateCell c{at};
    c.enter();
    if (::testing::Test::HasFatalFailure()) {
        return;
    }
    std::string const row = "D-3 " + std::string{any_state_name(c.held)} +
                            " (373=" + std::string{shape.reason} + " shape)";
    EXPECT_EQ(c.sess->state(), c.held) << row << ": state before the faulty Logon";
    int const admin_before = c.app->from_admin;
    int const app_before = c.app->from_app;

    c.fix.feed(*c.sess, make_raw_frame("A", 2, kLogonFields + shape.garble));
    EXPECT_EQ(c.sess->state(), fsm_state::Disconnected) << row << ": the faulty Logon ends it";
    EXPECT_TRUE(c.fix.transport.sent_frames().empty())
        << row << ": silent; sent:" << sent_types(c.fix);
    EXPECT_EQ(c.app->from_admin, admin_before) << row << ": the faulty Logon reached fromAdmin";
    EXPECT_EQ(c.app->from_app, app_before) << row << ": the faulty Logon reached fromApp";

    if (c.held != fsm_state::LogonReceived) {
        return;
    }
    c.fix.transport.reset();
    c.factory->last_store->release_parked();
    bool const done = fixpp::test_support::pump_until_ready(c.fix.ioc, c.logon);
    EXPECT_TRUE(done) << row << ": the parked Logon exchange did not complete after the release";
    if (done) {
        (void)c.logon.get();
    }
    EXPECT_TRUE(c.fix.sent_of_type("3").empty() && c.fix.sent_of_type("5").empty())
        << row << ": the released exchange sends no Reject or Logout; sent:" << sent_types(c.fix);
}

TEST(UnparseableFrameDisposition, D3_Active_FaultyLogon_MalformedTag_SilentDisconnect) {
    run_d3_cell(At::active, kTagShape);
}
TEST(UnparseableFrameDisposition, D3_Active_FaultyLogon_LengthDataMismatch_SilentDisconnect) {
    run_d3_cell(At::active, kCountShape);
}
TEST(UnparseableFrameDisposition, D3_LogonReceived_FaultyLogon_MalformedTag_SilentDisconnect) {
    run_d3_cell(At::logon_received, kTagShape);
}
TEST(UnparseableFrameDisposition,
     D3_LogonReceived_FaultyLogon_LengthDataMismatch_SilentDisconnect) {
    run_d3_cell(At::logon_received, kCountShape);
}

// ── D7_*, D8_* (tasks.md T046, T047; spec SC-005; contract C-2 D-7, D-8) ─────
//
// In Active and in LogonReceived, a faulty NewOrderSingle whose 34 was not read before
// the fault (D-7), or whose field 3 is not 35 (D-8), is disregarded: it draws nothing,
// reaches neither fromAdmin nor fromApp, and leaves the session open in its state. The
// next conformant message, a Heartbeat at N+1 = 3, must then be a gap whose
// ResendRequest begins at N = 2: NextNumIn unchanged. Every frame whose 34 is read
// carries 34=2, the expected number, so a disposer that routed it to D-5 would advance
// NextNumIn and the Heartbeat at 3 would draw no ResendRequest; one that routed it to
// D-6 would send a Reject. In LogonReceived the disregard is checked before
// StateCell::settle() releases the parked Logon reply.

void run_disregard_cell(At at, std::string const& body, std::string_view what) {
    StateCell c{at};
    c.enter();
    if (::testing::Test::HasFatalFailure()) {
        return;
    }
    std::string const row = std::string{what} + " in " + std::string{any_state_name(c.held)};
    EXPECT_EQ(c.sess->state(), c.held) << row << ": state before the faulty frame";
    int const admin_before = c.app->from_admin;
    int const app_before = c.app->from_app;

    c.fix.feed(*c.sess, wrap_body(body));
    EXPECT_TRUE(c.fix.transport.sent_frames().empty())
        << row << ": the faulty frame must draw nothing; sent:" << sent_types(c.fix);
    EXPECT_EQ(c.app->from_admin, admin_before) << row << ": the faulty frame reached fromAdmin";
    EXPECT_EQ(c.app->from_app, app_before) << row << ": the faulty frame reached fromApp";
    EXPECT_EQ(c.sess->state(), c.held) << row << ": state after the faulty frame";
    EXPECT_TRUE(c.sess->is_open()) << row << ": the faulty frame must not close the session";

    c.settle();
    if (::testing::Test::HasFatalFailure()) {
        return;
    }
    expect_heartbeat_gap(c, 3, "2", row + " (NextNumIn unchanged)");
}

// D-7 frames: field 3 is 35 and the fault precedes any positive 34.
// Test-fixture constants: a bad_alloc while building one before main aborts the
// test binary, which fails the run loudly.
// NOLINTBEGIN(bugprone-throwing-static-initialization,cert-err58-cpp)
std::string const kD7LengthDataBefore34 =
    std::string{"35=D\x01"} + kMalformedCount + "34=2\x01" + kHeader + kOrderFields;
std::string const kD7TagBefore34 =
    std::string{"35=D\x01"} + kMalformedTag + "34=2\x01" + kHeader + kOrderFields;
std::string const kD7NonNumeric34 =
    std::string{"35=D\x01"} + "34=abc\x01" + kHeader + kOrderFields + kMalformedTag;
std::string const kD7Zero34 =
    std::string{"35=D\x01"} + "34=0\x01" + kHeader + kOrderFields + kMalformedTag;
// NOLINTEND(bugprone-throwing-static-initialization,cert-err58-cpp)

TEST(UnparseableFrameDisposition, D7_Active_LengthDataBefore34_Disregarded) {
    run_disregard_cell(At::active, kD7LengthDataBefore34, "D-7 Length+Data before 34");
}
TEST(UnparseableFrameDisposition, D7_LogonReceived_LengthDataBefore34_Disregarded) {
    run_disregard_cell(At::logon_received, kD7LengthDataBefore34, "D-7 Length+Data before 34");
}
TEST(UnparseableFrameDisposition, D7_Active_MalformedTagBefore34_Disregarded) {
    run_disregard_cell(At::active, kD7TagBefore34, "D-7 malformed tag before 34");
}
TEST(UnparseableFrameDisposition, D7_LogonReceived_MalformedTagBefore34_Disregarded) {
    run_disregard_cell(At::logon_received, kD7TagBefore34, "D-7 malformed tag before 34");
}
TEST(UnparseableFrameDisposition, D7_Active_NonNumeric34BeforeFault_Disregarded) {
    run_disregard_cell(At::active, kD7NonNumeric34, "D-7 34=abc before the fault");
}
TEST(UnparseableFrameDisposition, D7_LogonReceived_NonNumeric34BeforeFault_Disregarded) {
    run_disregard_cell(At::logon_received, kD7NonNumeric34, "D-7 34=abc before the fault");
}
TEST(UnparseableFrameDisposition, D7_Active_Zero34BeforeFault_Disregarded) {
    run_disregard_cell(At::active, kD7Zero34, "D-7 34=0 before the fault");
}
TEST(UnparseableFrameDisposition, D7_LogonReceived_Zero34BeforeFault_Disregarded) {
    run_disregard_cell(At::logon_received, kD7Zero34, "D-7 34=0 before the fault");
}

// D-8 frames: field 3 is not 35. The mixed defect reads 35=D and 34=2 before the fault
// with field 3 = SenderCompID(49); in the other frame field 3 is itself the malformed
// field, so the fault comes first.
// Test-fixture constants: a bad_alloc while building one before main aborts the
// test binary, which fails the run loudly.
// NOLINTBEGIN(bugprone-throwing-static-initialization,cert-err58-cpp)
std::string const kD8MixedDefect = std::string{"49=TW\x01"} + "35=D\x01" + "34=2\x01" +
                                   kMalformedTag + "52=20240101-00:00:00.000\x01" + "56=ISLD\x01" +
                                   kOrderFields;
std::string const kD8Field3Malformed =
    kMalformedTag + "35=D\x01" + "34=2\x01" + kHeader + kOrderFields;
// NOLINTEND(bugprone-throwing-static-initialization,cert-err58-cpp)

TEST(UnparseableFrameDisposition, D8_Active_MixedDefect_Disregarded) {
    run_disregard_cell(At::active, kD8MixedDefect, "D-8 mixed defect (field 3 is 49)");
}
TEST(UnparseableFrameDisposition, D8_LogonReceived_MixedDefect_Disregarded) {
    run_disregard_cell(At::logon_received, kD8MixedDefect, "D-8 mixed defect (field 3 is 49)");
}
TEST(UnparseableFrameDisposition, D8_Active_Field3Malformed_Disregarded) {
    run_disregard_cell(At::active, kD8Field3Malformed, "D-8 field 3 malformed");
}
TEST(UnparseableFrameDisposition, D8_LogonReceived_Field3Malformed_Disregarded) {
    run_disregard_cell(At::logon_received, kD8Field3Malformed, "D-8 field 3 malformed");
}

// ── D9_* (tasks.md T048; spec FR-015; contract C-2 D-9) ──────────────────────
//
// In LogoutSent (an Active initiator's close(graceful) awaiting the peer's Logout), a
// faulty frame at N = 2 is disregarded: it draws nothing, reaches neither fromAdmin nor
// fromApp, the state stays LogoutSent and close() keeps waiting. The mock-clock logout
// timeout then ends the session. The budgets follow Anchor_D9: the logout timeout is
// below the heartbeat interval, and each pump's budget is half that timeout, so the
// real-time close_grace timer (armed for the same duration) cannot complete the close
// within it; only the mock-clock advance can.
//
// The faulty TestRequest cell is a pin: the pre-092 LogoutSent arm drained every frame
// that is not a Logout, so reverting the disposer cannot fail it. To check that it can
// fail, move `case fsm_state::LogoutSent:` in dispose_unparseable_ to the LogonReceived /
// Active group in a scratch copy: the TestRequest at N then draws a Reject and the cell
// must fail on "draws nothing".

void run_d9_cell(std::vector<std::byte> const& faulty, std::string_view row) {
    DispositionFixture fix;
    auto const app = std::make_shared<CountingApplication>();
    fix.engine.application = app;
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
        fixpp::test_support::cancel_and_drain_or_report(fix.ioc, *fix.clock, "D9/stage");
        ADD_FAILURE() << fixpp::test_support::kPumpBudgetMiss << row << "/stage";
        return;
    }

    int const admin_before = app->from_admin;
    int const app_before = app->from_app;
    fix.feed(sess, faulty);
    EXPECT_EQ(sess.state(), fsm_state::LogoutSent)
        << row << ": the faulty frame must not be taken as the Logout reply";
    EXPECT_NE(close_fut.wait_for(std::chrono::seconds{0}), std::future_status::ready)
        << row << ": close(graceful) must still await the reply or the logout timeout";
    EXPECT_TRUE(fix.transport.sent_frames().empty())
        << row << ": the faulty frame must draw nothing; sent:" << sent_types(fix);
    EXPECT_EQ(app->from_admin, admin_before) << row << ": the faulty frame reached fromAdmin";
    EXPECT_EQ(app->from_app, app_before) << row << ": the faulty frame reached fromApp";

    fix.clock->advance(timeout + std::chrono::milliseconds{1});
    if (!fixpp::test_support::pump_until_ready(fix.ioc, close_fut, timeout / 2)) {
        fixpp::test_support::cancel_and_drain_or_report(fix.ioc, *fix.clock, "D9/close");
        ADD_FAILURE() << fixpp::test_support::kPumpBudgetMiss << row << "/close";
        return;
    }
    (void)close_fut.get();
    EXPECT_EQ(sess.state(), fsm_state::Disconnected)
        << row << ": the logout timeout must end the session";
}

TEST(UnparseableFrameDisposition, D9_FaultyLogout_MalformedTag_NotTakenAsReply) {
    run_d9_cell(make_raw_frame("5", 2, kLogoutFields + kMalformedTag),
                "D-9 faulty Logout (373=0 shape)");
}
TEST(UnparseableFrameDisposition, D9_FaultyLogout_LengthDataMismatch_NotTakenAsReply) {
    run_d9_cell(make_raw_frame("5", 2, kLogoutFields + kMalformedCount),
                "D-9 faulty Logout (373=5 shape)");
}
// Pin (see the section comment).
TEST(UnparseableFrameDisposition, D9_FaultyTestRequest_DrawsNothing_Pin) {
    run_d9_cell(make_raw_frame("1", 2, kTestRequestFields + kMalformedTag),
                "D-9 faulty TestRequest");
}

// ── Disclosed_* (tasks.md T049; contract C-5 L-1, L-2, L-4) ──────────────────
//
// Disclosed_L1_* and Disclosed_L2_* drive an Active initiator over a FileStore
// (LateCell) with a scripted peer (ScriptedPeer: it stores what it sends and answers
// each ResendRequest by replaying its stored bytes with PossDupFlag(43)=Y and
// OrigSendingTime(122) inserted after 34). Each fed frame is rendered as one step: the
// frame's 35, 34 and 43, every frame fixpp sent in reply (a ResendRequest with 7 and
// 16, a Reject with 45, 372, 373, 371 and 58), and the state after it. The whole trace is
// compared with the expected trace spelled out in the cell, so a change to any step,
// or to the step at which the run ends, fails the cell. The run is bounded by an
// iteration cap derived from the script, as in the ScriptedPeer_* cell. After the run
// the store is reopened and its durable NextNumIn read (LateCell::durable_next_inbound):
// the number a reconnect resumes from, and so where the peer's next replay begins.

// One step: `fed`, what fixpp sent in reply, and the state after it.
std::string render_step(std::vector<std::byte> const& fed, DispositionFixture const& fix,
                        Session const& sess) {
    std::string out = "35=" + extract_tag(fed, 35) + " 34=" + extract_tag(fed, 34);
    if (extract_tag(fed, 43) == "Y") {
        out += " 43=Y";
    }
    out += " ->";
    for (auto const& f : fix.transport.sent_frames()) {
        std::string const type = extract_tag(f, 35);
        out += " [35=" + type;
        if (type == "2") {
            out += " 7=" + extract_tag(f, 7) + " 16=" + extract_tag(f, 16);
        } else if (type == "3") {
            out += " 45=" + extract_tag(f, 45) + " 372=" + extract_tag(f, 372) +
                   " 373=" + extract_tag(f, 373) +
                   (has_field(f, "371=") ? " 371=" + extract_tag(f, 371) : std::string{}) +
                   " 58=" + extract_tag(f, 58);
        }
        out += "]";
    }
    return out + " " + std::string{any_state_name(sess.state())};
}

// Feeds the script through `peer` until the queue drains with the script exhausted,
// the session leaves Active, or the cap is reached; returns the rendered steps.
std::vector<std::string> run_scripted(LateCell& c, ScriptedPeer& peer,
                                      std::vector<std::vector<std::byte>> const& script) {
    std::size_t const cap = script.size() * (1 + 1 + script.size());
    std::vector<std::string> steps;
    std::size_t next = 0;
    while (steps.size() < cap && c.sess->state() == fsm_state::Active) {
        if (peer.wire.empty()) {
            if (next == script.size()) {
                break;
            }
            peer.sent(script[next]);
            peer.wire.push_back(script[next++]);
        }
        auto const frame = peer.wire.front();
        peer.wire.pop_front();
        c.fix.feed(*c.sess, frame);
        steps.push_back(render_step(frame, c.fix, *c.sess));
        for (auto const& out : c.fix.transport.sent_frames()) {
            peer.on_fixpp_frame(out, frame);
        }
    }
    return steps;
}

std::string joined(std::vector<std::string> const& steps) {
    std::string out;
    for (auto const& s : steps) {
        out += "\n  " + s;
    }
    return out;
}

// L-1: the peer sends a NewOrderSingle at N = 2 whose malformed tag precedes 34 (D-7),
// then a NewOrderSingle at 3, a Heartbeat at 4 and NewOrderSingles at 5 and 6. The
// replay of the faulty frame carries the same fault before 34, so it is disregarded
// again. Contract C-5 L-1 describes the first steps of the trace: the gap draws a
// ResendRequest and the replay is disregarded. The rest of the trace pins what the
// session does next in this scenario: AwaitingResend draws no second ResendRequest for
// the PossDup resend or the too-high Heartbeat, and the next new message that is
// neither a Heartbeat nor a PossDup ends the session. The durable NextNumIn after the
// run must be 2: a reconnect resumes from 2, the number of the faulty frame.
TEST(UnparseableFrameDisposition, Disclosed_L1_ReplayedFaultBefore34_ResendLoop) {
    LateCell c{{.validate = false}};
    c.fix.open_to_active(*c.sess);
    if (::testing::Test::HasFatalFailure()) {
        return;
    }
    std::vector<std::vector<std::byte>> script;
    script.push_back(
        wrap_body(std::string{"35=D\x01"} + kMalformedTag + "34=2\x01" + kHeader + "11=ORD2\x01"));
    script.push_back(make_raw_frame("D", 3, "11=ORD3\x01"));
    script.push_back(make_raw_frame("0", 4));
    script.push_back(make_raw_frame("D", 5, "11=ORD5\x01"));
    script.push_back(make_raw_frame("D", 6, "11=ORD6\x01"));

    ScriptedPeer peer;
    auto const steps = run_scripted(c, peer, script);
    std::vector<std::string> const want{
        "35=D 34=2 -> Active",      "35=D 34=3 -> [35=2 7=2 16=0] Active",
        "35=D 34=2 43=Y -> Active", "35=D 34=3 43=Y -> Active",
        "35=0 34=4 -> Active",      "35=D 34=5 -> Disconnected",
    };
    EXPECT_EQ(steps, want) << "L-1 trace:" << joined(steps);
    EXPECT_EQ(c.app->from_app, 0) << "L-1: no application message may be delivered";
    EXPECT_EQ(c.durable_next_inbound(), 2U)
        << "L-1: durable NextNumIn after the run (a reconnect resumes from it)";
}

// L-2: the peer's NewOrderSingle at N = 2 is lost; its store answers slot 2 with a
// SequenceReset-GapFill (NewSeqNo 3) carrying a malformed tag after 36, and slot 3 with
// the NewOrderSingle it sent. The peer sends the NewOrderSingle at 3 (a gap: fixpp
// enters AwaitingResend and sends a ResendRequest), then a NewOrderSingle at 4 and one
// at 5. The expected trace follows contract C-5 L-2's sequence: the faulty GapFill is
// Rejected and not applied (D-4); the PossDup resend of 3 draws nothing (no second
// ResendRequest while AwaitingResend); the next new message ends the session. The
// durable NextNumIn after the run must be 2, so a reconnect asks for slot 2 again and
// the peer answers it with the same GapFill.
TEST(UnparseableFrameDisposition, Disclosed_L2_FaultyGapFillDuringAwaitingResend_Disconnects) {
    LateCell c{{.validate = false}};
    c.fix.open_to_active(*c.sess);
    if (::testing::Test::HasFatalFailure()) {
        return;
    }
    ScriptedPeer peer;
    peer.sent(make_raw_frame("4", 2, std::string{"123=Y\x01"} + "36=3\x01" + kMalformedTag));
    std::vector<std::vector<std::byte>> script;
    script.push_back(make_raw_frame("D", 3, "11=ORD3\x01"));
    script.push_back(make_raw_frame("D", 4, "11=ORD4\x01"));
    script.push_back(make_raw_frame("D", 5, "11=ORD5\x01"));

    auto const steps = run_scripted(c, peer, script);
    std::vector<std::string> const want{
        "35=D 34=3 -> [35=2 7=2 16=0] Active",
        "35=4 34=2 43=Y -> [35=3 45=2 372=4 373=0 58=Malformed field: invalid tag] Active",
        "35=D 34=3 43=Y -> Active",
        "35=D 34=4 -> Disconnected",
    };
    EXPECT_EQ(steps, want) << "L-2 trace:" << joined(steps);
    EXPECT_EQ(c.app->from_app, 0) << "L-2: no application message may be delivered";
    EXPECT_EQ(c.durable_next_inbound(), 2U)
        << "L-2: durable NextNumIn after the run (a reconnect resumes from it)";
}

// L-4: TC2020 Scenario 17d is not followed. A malformed SignatureLength(93)/Signature(89)
// pair (the count is not followed by SOH) on a NewOrderSingle at N draws 373=5 with
// 371=93, not 17d's 373=8; the Reject is its only outbound frame and NextNumIn advances
// (D-5).
// Test-fixture constants: a bad_alloc while building one before main aborts the
// test binary, which fails the run loudly.
// NOLINTBEGIN(bugprone-throwing-static-initialization,cert-err58-cpp)
Shape const kSignatureShape{.garble = std::string{"93=2\x01"} + "89=xyz\x01",
                            .reason = "5",
                            .ref_tag = "93",
                            .text = kTextLengthDataMismatch};
// NOLINTEND(bugprone-throwing-static-initialization,cert-err58-cpp)

TEST(UnparseableFrameDisposition, Disclosed_L4_Active_MalformedSignaturePair_Reason5) {
    run_expected_n_cell(At::active, "D", kOrderFields, kSignatureShape);
}
TEST(UnparseableFrameDisposition, Disclosed_L4_LogonReceived_MalformedSignaturePair_Reason5) {
    run_expected_n_cell(At::logon_received, "D", kOrderFields, kSignatureShape);
}

// ── GuardPrecedence_* (tasks.md T082; spec FR-002, edge case "Wrong CompID or
// BeginString"; clarification A-3) ───────────────────────────────────────────────
//
// In Active, a faulty NewOrderSingle at N=2 whose header, before the fault, carries a
// value a well-formed-frame guard acts on: a BeginString(8) other than the session's,
// a TargetCompID(56) other than its SenderCompID, or a SendingTime(52) outside the
// accuracy threshold of the fixture clock. The fault branch runs first, so the frame
// draws exactly the D-5 Reject and the conformant Heartbeat at 3 is in sequence. Each
// cell also feeds the same frame without the fault to a fresh session, which must meet
// the guard: that control shows the chosen value does trip the guard.
// Inbound validation is off in these cells, so the validate gate, which sits between the
// fault branch and the guards when validation is on, does not act on the frame whatever
// the branch's position. The frames go to on_inbound_frame directly, so the
// Framer is not exercised here; the engine's first-frame routing read of 8/49/56 is
// out of FR-001's scope.
// To check a cell, move the fault branch of the LogonReceived/Active arm of
// Session::on_inbound_frame to after the guard concerned in a scratch copy: the cell
// must fail.

enum class GuardAction : std::uint8_t { silent_disconnect, sending_time_reject_logout };

// The header of a NewOrderSingle at 34=2, then its order fields, then `tail`.
std::vector<std::byte> guard_frame(std::string_view begin_string, std::string_view target,
                                   std::string_view sending_time, std::string const& tail) {
    return wrap_body(std::string{"35=D\x01"} + "34=2\x01" + "49=TW\x01" +
                         "52=" + std::string{sending_time} + "\x01" + "56=" + std::string{target} +
                         "\x01" + kOrderFields + tail,
                     begin_string);
}

void run_guard_precedence_cell(std::string_view begin_string, std::string_view target,
                               std::string_view sending_time, GuardAction control,
                               std::string_view what) {
    std::string const row = std::string{what} + " before the fault";
    {
        StateCell c{At::active, /*validate=*/false};
        c.enter();
        if (::testing::Test::HasFatalFailure()) {
            return;
        }
        feed_faulty(c, guard_frame(begin_string, target, sending_time, kMalformedTag),
                    want_reject("2", "D", kTagShape), row);
        expect_heartbeat_in_sequence(c.fix, *c.sess, *c.app, 3, row + " (NextNumIn advanced)");
    }

    std::string const ctl = std::string{what} + " control (no fault)";
    StateCell k{At::active, /*validate=*/false};
    k.enter();
    if (::testing::Test::HasFatalFailure()) {
        return;
    }
    k.fix.feed(*k.sess, guard_frame(begin_string, target, sending_time, {}));
    EXPECT_EQ(k.sess->state(), fsm_state::Disconnected)
        << ctl << ": the guard must end the session";
    EXPECT_EQ(k.app->from_app, 0) << ctl << ": the frame must not reach fromApp";
    std::string types;
    for (auto const& f : k.fix.transport.sent_frames()) {
        types += " 35=" + extract_tag(f, 35);
    }
    if (control == GuardAction::silent_disconnect) {
        EXPECT_TRUE(k.fix.transport.sent_frames().empty()) << ctl << ": sent:" << types;
        return;
    }
    EXPECT_EQ(types, " 35=3 35=5") << ctl << ": expected the SendingTime Reject, then a Logout";
    auto const rejects = k.fix.sent_of_type("3");
    if (!rejects.empty()) {
        EXPECT_EQ(extract_tag(rejects.front(), 373), "10") << ctl << ": Reject 373";
        EXPECT_EQ(extract_tag(rejects.front(), 371), "52") << ctl << ": Reject 371";
    }
}

TEST(UnparseableFrameDisposition, GuardPrecedence_WrongBeginString_D5RejectOnly) {
    run_guard_precedence_cell("FIX.4.4", "ISLD", "20240101-00:00:00.000",
                              GuardAction::silent_disconnect, "8=FIX.4.4");
}
TEST(UnparseableFrameDisposition, GuardPrecedence_WrongTargetCompId_D5RejectOnly) {
    run_guard_precedence_cell("FIX.4.2", "WRONG", "20240101-00:00:00.000",
                              GuardAction::silent_disconnect, "56=WRONG");
}
TEST(UnparseableFrameDisposition, GuardPrecedence_SendingTimeOutsideThreshold_D5RejectOnly) {
    run_guard_precedence_cell("FIX.4.2", "ISLD", "20200101-00:00:00.000",
                              GuardAction::sending_time_reject_logout, "52=20200101");
}

// ── OffExpectedSequenceReset_* (tasks.md T082; spec FR-010; contract C-2 D-4) ───
//
// A faulty SequenceReset, in Reset mode and in GapFill mode, both with NewSeqNo(36)=500
// before the fault, at a MsgSeqNum above (34=5) and below (34=1, no PossDupFlag) the
// expected N=2. C-2 row D-4 is keyed on the MsgType alone: the Reject (45 = the 34
// sent, 372=4) is the only outbound frame, and a conformant Heartbeat at 2 is then
// delivered in sequence, so NewSeqNo was not applied and NextNumIn did not move.
// To check a cell, make dispose_unparseable_ apply NewSeqNo for a faulty SequenceReset
// in a scratch copy: the cell must fail.

// Test-fixture constants: a bad_alloc while building one before main aborts the
// test binary, which fails the run loudly.
// NOLINTBEGIN(bugprone-throwing-static-initialization,cert-err58-cpp)
std::string const kResetModeFields = std::string{"123=N\x01"} + "36=500\x01";
// NOLINTEND(bugprone-throwing-static-initialization,cert-err58-cpp)

TEST(UnparseableFrameDisposition, OffExpectedSequenceReset_ResetMode_AboveN) {
    run_not_expected_cell(At::active, "4", 5, kResetModeFields, kTagShape, "Reset mode at N+3");
}
TEST(UnparseableFrameDisposition, OffExpectedSequenceReset_ResetMode_BelowN) {
    run_not_expected_cell(At::active, "4", 1, kResetModeFields, kTagShape, "Reset mode at N-1");
}
TEST(UnparseableFrameDisposition, OffExpectedSequenceReset_GapFill_AboveN) {
    run_not_expected_cell(At::active, "4", 5, kGapFillFields, kTagShape, "GapFill at N+3");
}
TEST(UnparseableFrameDisposition, OffExpectedSequenceReset_GapFill_BelowN) {
    run_not_expected_cell(At::active, "4", 1, kGapFillFields, kTagShape, "GapFill at N-1");
}

// ── ToAdmin_* (tasks.md T082; spec FR-013) ───────────────────────────────────
//
// The 092 Reject is passed to toAdmin like any other outbound session Reject: a D-5
// faulty NewOrderSingle in Active draws exactly one toAdmin call, for a Reject(35=3)
// carrying the 092 Text(58), and it is the Reject that is sent (same MsgSeqNum(34)).
// Calls made while establishing the session are excluded by a snapshot.
// To check the cell, skip fire_to_admin_ for a Reject with a Text(58) in
// Session::emit_session_reject_ in a scratch copy: the cell must fail.

// Records the MsgType(35), MsgSeqNum(34) and Text(58) of every toAdmin call.
class ToAdminRecordingApplication final : public Application {
public:
    struct Seen {
        std::string msg_type;
        std::string seq;
        std::string text;
    };
    std::vector<Seen> to_admin;
    int from_app = 0;

    void toAdmin(const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& msg,
                 const SessionId& /*id*/) override {
        auto const text = msg.get(58);
        to_admin.push_back({.msg_type = std::string{msg.msg_type()},
                            .seq = std::to_string(msg.msg_seq_num()),
                            .text = text ? std::string{text->as_string()} : std::string{}});
    }
    fixpp::core::expected_t<void> fromApp(
        const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& /*msg*/,
        const SessionId& /*id*/) override {
        ++from_app;
        return {};
    }
};

TEST(UnparseableFrameDisposition, ToAdmin_D5Reject_ObservedOnce) {
    DispositionFixture fix;
    auto const app = std::make_shared<ToAdminRecordingApplication>();
    fix.engine.application = app;
    Session sess{fix.engine, fix.make_cfg(/*validate=*/true)};
    fix.open_to_active(sess);
    if (::testing::Test::HasFatalFailure()) {
        return;
    }
    std::size_t const before = app->to_admin.size();

    fix.feed(sess, make_raw_frame("D", 2, kOrderFields + kMalformedCount));
    expect_only_reject(fix, want_reject("2", "D", kCountShape), "FR-013 D-5");
    EXPECT_EQ(app->from_app, 0) << "FR-013 D-5: the faulty frame never reaches fromApp";
    EXPECT_EQ(app->to_admin.size(), before + 1U)
        << "FR-013 D-5: the Reject must be observed by exactly one toAdmin call";
    if (app->to_admin.size() <= before) {
        return;
    }
    auto const& seen = app->to_admin.back();
    EXPECT_EQ(seen.msg_type, "3") << "FR-013 D-5: toAdmin MsgType(35)";
    EXPECT_EQ(seen.text, "Malformed field: Length does not match its Data field")
        << "FR-013 D-5: toAdmin Text(58)";
    auto const rejects = fix.sent_of_type("3");
    if (!rejects.empty()) {
        EXPECT_EQ(seen.seq, extract_tag(rejects.front(), 34))
            << "FR-013 D-5: toAdmin saw a different MsgSeqNum(34) than the Reject sent";
    }
}
}  // namespace
}  // namespace fixpp::session::test
