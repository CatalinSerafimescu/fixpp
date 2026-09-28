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
#include <fixpp/session/message_store.hpp>
#include <fixpp/session/message_store_factory.hpp>
#include <fixpp/session/retrieve_visitor.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
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

// A SOH-delimited FIX frame with a correct BodyLength(9) and CheckSum(10).
std::vector<std::byte> make_raw_frame(std::string_view msg_type, std::uint32_t seq,
                                      std::string const& extra_body = {}) {
    std::string body;
    body += "35=" + std::string(msg_type) + "\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=TW\x01";
    body += "52=20240101-00:00:00.000\x01";
    body += "56=ISLD\x01";
    body += extra_body;

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
