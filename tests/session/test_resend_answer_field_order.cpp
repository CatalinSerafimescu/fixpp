// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_resend_answer_field_order.cpp
//
// #419: resend answers (replay + GapFill) placed PossDupFlag(43)/OrigSendingTime(122)
// after the body. A strict peer (QuickFIX-J with UseDataDictionary=Y) rejects them
// (373=14, "Tag specified out of required order"). This supersedes 037's Assumptions
// section ("Confirmed order-safe ... field order is irrelevant to inbound validation")
// — that claim was never checked against a strict peer; the live QuickFIX-J run that
// would have caught it was deferred in 037's own disposition. [issue #419]
//
// Regression witness for the two resend-answer builders (GapFill via
// build_sequence_reset_gapfill, replay via build_replay_frame). The scanner
// (check_resend_answer_field_order, below) checks, on whatever frame it is
// given:
//   - the frame's first three fields are exactly 8, 9, 35 in that order (the
//     preamble both QuickFIX-J and QuickFIX-cpp parse before anything else —
//     see the scanner's own comment for the exact source citation);
//   - no standard-header tag (QuickFIX-J's `Message.isHeaderField` set) appears
//     AFTER the first body (non-header, non-trailer) tag;
//   - the trailer (10, and 89/93 if ever present) comes last, with CheckSum(10)
//     the final field;
//   - PossDupFlag(43) and OrigSendingTime(122) each appear exactly once (this
//     count is reported, not folded into the pass/fail verdict — see the
//     struct comment).
//
// The scanner walks fields with fixpp::wire::accumulate_tag_digit — the bounded
// tag-digit accumulator shared by fixpp's production wire scanners (tag_scan.hpp,
// 040-inbound-tag-overflow-hardening research.md D-1) — rather than ad hoc substring
// matching.
//
// Cell 1 (GapFill): direct build_sequence_reset_gapfill() call.
// Cell 2 (Replay): a Session-driven resend of a stored NewOrderSingle. The payload
//   carries a NoPartyIDs(453)/NoPartySubIDs(802) repeating group because it mirrors
//   the live evidence quoted in issue #419 — it does NOT exercise the boundary
//   search walking into the group (insertion happens at the first body tag, 11,
//   before the group starts; see O3/O4 in the #419 Gate-B triage for why a
//   contrary claim was removed from this comment).
//
// RED (pre-#419-fix, `2e853adf`): Cell 1 fails because the builder emits
// ...52,56,36,123,43,122 (43/122 after body tags 36/123); Cell 2 fails because
// build_replay_frame appends 43/122 after the full stored body. Reproduce by
// reverting the two builder hunks of `2e853adf` (admin_messages.cpp,
// session.cpp) — both cells fail with "header tag 43 appears AFTER a body tag".
//
// Anchors: issue #419; specs/037-resend-reply-possdup-tags/spec.md (superseded tail
// placement, Assumptions section) — 037's spec.md was never checked against a
// strict peer, which is history and does not go stale; specs/013-session-reconnect-
// binding/spec.md FR-010.
//
// Build: cmake --build build/linux-clang-debug --target session_resend_answer_field_order -j2
// Run:   ctest --test-dir build/linux-clang-debug -R session_resend_answer_field_order -V

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/admin_messages.hpp>
#include <fixpp/session/application.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/message_store.hpp>
#include <fixpp/session/message_store_factory.hpp>
#include <fixpp/session/retrieve_visitor.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_event.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fixpp/wire/tag_scan.hpp>
#include <functional>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "session/support/frame_field_extract.hpp"   // via -I tests/
#include "session/support/possdup_test_support.hpp"  // CountingApplication
#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"
#include "support/pump_until_ready.hpp"

using namespace std::chrono_literals;
using fixpp::session::test_support::extract_field;

namespace fixpp::session::test {
namespace {

constexpr auto kWindow = 200ms;

// ── Field-order witness scanner ──────────────────────────────────────────────

// FIX standard-header tags, per QuickFIX-J 3.0.1's `quickfix.Message.
// isHeaderField(int)` — the exact switch the strict peer applies before
// SessionRejectReason=14 is even reached. Verified 2026-09-11 by
// disassembling `quickfixj-base-3.0.1.jar` with `javap -p -c`
// (~/.m2/repository/org/quickfixj/quickfixj-base/3.0.1/) — this worktree has
// no quickfixj source tree to cite by file:line. Defined independently of
// fixpp's own production header-tag set (`kReplayHeaderTags`,
// src/session/session.cpp): see that constant's comment for why fixpp's set
// is deliberately narrower and still correct.
constexpr std::array<std::uint32_t, 30> kWitnessHeaderTags = {
    8,   9,   34,  35,  43,  49,  50,  52,  56,  57,  90,  97,  115,  116,  122,
    128, 129, 142, 143, 144, 145, 212, 213, 347, 369, 370, 627, 1128, 1129, 1156};

// FIX trailer tags, per the same jar's `isTrailerField(int)`.
constexpr std::array<std::uint32_t, 3> kWitnessTrailerTags = {10, 89, 93};

// The first three fields of any FIX frame must be exactly BeginString(8),
// BodyLength(9), MsgType(35) in that order — both QFJ (`Message.parseHeader`)
// and QuickFIX-cpp (`Message::extractHeader`) enforce this before any other
// check; a violation is a parse-level rejection, not merely 373=14.
constexpr std::array<std::uint32_t, 3> kWitnessPreamble = {8, 9, 35};

[[nodiscard]] bool is_witness_header_tag(std::uint32_t tag) noexcept {
    return std::ranges::find(kWitnessHeaderTags, tag) != kWitnessHeaderTags.end();
}

[[nodiscard]] bool is_witness_trailer_tag(std::uint32_t tag) noexcept {
    return std::ranges::find(kWitnessTrailerTags, tag) != kWitnessTrailerTags.end();
}

struct OrderCheckResult {
    bool ok = false;
    std::string reason;
    std::size_t count_43 = 0;
    std::size_t count_122 = 0;
};

// Walks `frame` field-by-field using fixpp::wire::accumulate_tag_digit (the
// bounded tag-digit accumulator shared by fixpp's production wire scanners),
// not substring matching. Position is the property under test, not presence.
OrderCheckResult check_resend_answer_field_order(std::span<const std::byte> frame) {
    OrderCheckResult r;
    std::size_t i = 0;
    const std::size_t n = frame.size();
    std::size_t field_index = 0;
    bool seen_body_tag = false;
    bool seen_trailer_tag = false;
    bool seen_trailer = false;
    while (i < n) {
        if (seen_trailer) {
            r.reason = "field(s) present after CheckSum(10) - trailer not last";
            return r;
        }
        std::uint32_t tag = 0;
        const std::size_t tag_begin = i;
        bool tag_ok = true;
        while (i < n && frame[i] != std::byte{'='} && frame[i] != std::byte{0x01}) {
            const auto c = static_cast<unsigned char>(frame[i]);
            if (c < '0' || c > '9' || !fixpp::wire::accumulate_tag_digit(tag, c)) tag_ok = false;
            ++i;
        }
        if (i >= n || frame[i] != std::byte{'='} || !tag_ok || i == tag_begin) {
            r.reason = "malformed field while scanning (tag parse failure)";
            return r;
        }
        ++i;  // skip '='
        while (i < n && frame[i] != std::byte{0x01}) ++i;
        if (i < n) ++i;  // skip SOH

        if (field_index < kWitnessPreamble.size() && tag != kWitnessPreamble[field_index]) {
            r.reason = "preamble out of order: field #" + std::to_string(field_index) + " is tag " +
                       std::to_string(tag) + ", expected " +
                       std::to_string(kWitnessPreamble[field_index]);
            return r;
        }
        ++field_index;

        if (is_witness_trailer_tag(tag)) {
            seen_trailer_tag = true;
            if (tag == 10) seen_trailer = true;
            continue;
        }
        if (seen_trailer_tag) {
            r.reason = "tag " + std::to_string(tag) + " appears AFTER a trailer tag";
            return r;
        }
        if (tag == 43) ++r.count_43;
        if (tag == 122) ++r.count_122;

        if (is_witness_header_tag(tag)) {
            if (seen_body_tag) {
                r.reason = "header tag " + std::to_string(tag) + " appears AFTER a body tag";
                return r;
            }
        } else {
            seen_body_tag = true;
        }
    }
    if (!seen_trailer) {
        r.reason = "frame has no CheckSum(10) trailer";
        return r;
    }
    // `ok` is the pure POSITION property (header-before-body, trailer-last).
    // Cardinality of 43/122 is reported via count_43/count_122 but NOT folded
    // into `ok`: an ordinary (non-resend-answer) outbound frame legitimately
    // carries zero of either, and this scanner is also used to check those.
    // Each resend-answer test asserts count_43==1 && count_122==1 explicitly.
    r.ok = true;
    return r;
}

}  // namespace

// ── Cell 1: build_sequence_reset_gapfill ─────────────────────────────────────
//
// RED (pre-#419-fix): builder emits ...52,56,36,123,43,122 — 43/122 land after
// the body tags 36 (NewSeqNo) / 123 (GapFillFlag).
// The scanner's own trailer arm: a trailer tag (93 SignatureLength) before a
// body field must fail, not only a field after CheckSum(10).
TEST(ResendAnswerFieldOrder, Scanner_RejectsBodyFieldAfterATrailerTag) {
    const std::string f =
        "8=FIX.4.4\x01"
        "9=20\x01"
        "35=0\x01"
        "93=1\x01"
        "58=x\x01"
        "10=000\x01";
    const auto r =
        check_resend_answer_field_order(std::as_bytes(std::span<const char>{f.data(), f.size()}));
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.reason.find("AFTER a trailer tag"), std::string::npos) << r.reason;
}

TEST(ResendAnswerFieldOrder, GapFill_NoHeaderTagAfterBody) {
    constexpr std::string_view kSender = "ISLD";
    constexpr std::string_view kTarget = "TW";
    constexpr std::string_view kBeginString = "FIX.4.4";
    constexpr std::string_view kSendingTime = "20260614-12:00:00.000";
    constexpr fixpp::session::seqnum_t kSeq = 5;
    constexpr fixpp::session::seqnum_t kNewSeqno = 10;

    std::array<std::byte, 512> buf{};
    auto result = fixpp::session::build_sequence_reset_gapfill(
        std::span<std::byte>{buf}, kSeq, kSender, kTarget, kNewSeqno, kBeginString, kSendingTime);
    ASSERT_TRUE(result.has_value()) << "build_sequence_reset_gapfill must succeed";

    const auto check = check_resend_answer_field_order(*result);
    EXPECT_TRUE(check.ok) << check.reason;
    EXPECT_EQ(check.count_43, 1U) << "GapFill must carry PossDupFlag(43) exactly once";
    EXPECT_EQ(check.count_122, 1U) << "GapFill must carry OrigSendingTime(122) exactly once";
}

// ── Cell 2: build_replay_frame (stored NewOrderSingle w/ repeating group) ────

namespace {

// A MessageStore that records each outbound store call and serves as a real
// retrieve() source for the resend-reply store-walk. Based on the CapturingStore
// in test_send_allow_pos_dup_strip.cpp, extended here with the
// `force_empty_retrieve` fault-injection knob and a factory exposing `last_store`.
class CapturingStore final : public MessageStore {
public:
    struct Record {
        seqnum_t seq;
        std::vector<std::byte> frame;
    };
    std::vector<Record> outbound_records;

    // O1 fault-injection knob: when true, retrieve() visits nothing, regardless
    // of what is in outbound_records. Simulates the class of regression O1
    // describes (a store whose retrieve stops visiting, a CaptureVisitor
    // change, a msg-type misclassification) without needing to reproduce any
    // ONE of those specific production changes — replay_outbound_range_ folds
    // an unvisited slot into a SequenceReset-GapFill (session.cpp,
    // "Absent slot or admin message -> fold into a GapFill run"). Default
    // false: every other test in this file is unaffected.
    bool force_empty_retrieve = false;

    explicit CapturingStore() noexcept : MessageStore(flush_thunk_for<CapturingStore>()) {}

    // Adds an outbound record at `seq` without Session::send, advancing the next
    // outbound number past it as store() does, so a ResendRequest can cover it.
    void add_outbound(seqnum_t seq, std::vector<std::byte> frame) {
        outbound_records.push_back({.seq = seq, .frame = std::move(frame)});
        if (seq + 1U > next_out_) next_out_ = seq + 1U;
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> store(
        seqnum_t seq, std::span<const std::byte> frame, direction_t dir) noexcept override {
        if (dir == direction_t::outbound) {
            outbound_records.push_back(
                {.seq = seq, .frame = std::vector<std::byte>(frame.begin(), frame.end())});
            if (seq + 1U > next_out_) next_out_ = seq + 1U;
        }
        co_return fixpp::core::expected_t<void>{};
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> retrieve(
        seqnum_t from, seqnum_t to, direction_t dir, retrieve_visitor& visitor) noexcept override {
        if (force_empty_retrieve) co_return fixpp::core::expected_t<void>{};
        if (dir == direction_t::outbound) {
            for (auto& rec : outbound_records) {
                if (rec.seq >= from && rec.seq <= to) {
                    auto r =
                        co_await visitor.on_frame(rec.seq, std::span<const std::byte>(rec.frame));
                    if (!r || *r == visit_result::stop) break;
                }
            }
        }
        co_return fixpp::core::expected_t<void>{};
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<seqnum_t>> next_seqnum(
        direction_t dir, bool increment) noexcept override {
        auto& c = (dir == direction_t::outbound) ? next_out_ : next_in_;
        const seqnum_t curr = c;
        if (increment) ++c;
        co_return curr;
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> reset() noexcept override {
        next_in_ = next_out_ = seqnum_min;
        co_return fixpp::core::expected_t<void>{};
    }

private:
    seqnum_t next_out_ = seqnum_min;
    seqnum_t next_in_ = seqnum_min;
};

class CapturingStoreFactory final : public MessageStoreFactory {
public:
    // Raw, non-owning pointer to the store created by the last make() call
    // (Session owns it via unique_ptr). Lets a test reach in after Session
    // construction to arm force_empty_retrieve (O1). Null until make() runs.
    CapturingStore* last_store = nullptr;

    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<MessageStore>> make(
        std::string_view, std::string_view, std::pmr::memory_resource*, std::size_t,
        asio::any_io_executor) noexcept override {
        auto store = std::make_unique<CapturingStore>();
        last_store = store.get();
        return store;
    }
};

// Build a full FIX 4.4 wire frame: 8=FIX.4.4 / 9=<len> / <body> / 10=<cs>.
std::vector<std::byte> make_fix_frame(std::string_view body_str) {
    std::string hdr = "8=FIX.4.4\x01";
    hdr += "9=" + std::to_string(body_str.size()) + "\x01";
    std::string full = hdr + std::string(body_str);
    unsigned int cs = 0;
    for (unsigned char c : full) cs += c;
    cs &= 0xFFU;
    char csbuf[4];
    snprintf(csbuf, sizeof(csbuf), "%03u", cs);
    full += "10=" + std::string(csbuf) + "\x01";
    std::vector<std::byte> frame;
    frame.reserve(full.size());
    for (char c : full) frame.push_back(static_cast<std::byte>(c));
    return frame;
}

std::vector<std::byte> make_peer_logon_44(std::uint32_t seq, std::string_view sender,
                                          std::string_view target) {
    std::string body;
    body += "35=A\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=" + std::string(sender) + "\x01";
    body += "52=20240101-00:00:00.000\x01";
    body += "56=" + std::string(target) + "\x01";
    body += "98=0\x01";
    body += "108=30\x01";
    return make_fix_frame(body);
}

std::vector<std::byte> make_resend_request(
    seqnum_t begin_seqno, seqnum_t end_seqno, std::uint32_t inbound_seq, std::string_view sender,
    std::string_view target, std::string_view sending_time = "20240101-00:00:00.000") {
    std::string body;
    body += "35=2\x01";
    body += "34=" + std::to_string(inbound_seq) + "\x01";
    body += "49=" + std::string(sender) + "\x01";
    body += "52=" + std::string(sending_time) + "\x01";
    body += "56=" + std::string(target) + "\x01";
    body += "7=" + std::to_string(static_cast<std::uint32_t>(begin_seqno)) + "\x01";
    body += "16=" + std::to_string(static_cast<std::uint32_t>(end_seqno)) + "\x01";
    return make_fix_frame(body);
}

std::vector<std::byte> to_payload(std::string_view sv) {
    std::vector<std::byte> out;
    out.reserve(sv.size());
    for (char c : sv) out.push_back(static_cast<std::byte>(c));
    return out;
}

// ── Fixture: FIX.4.4 acceptor session with a real (capturing) store ─────────

class ResendAnswerReplayTest : public ::testing::Test {
protected:
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine{};
    std::vector<std::vector<std::byte>> captured_frames;

    void SetUp() override {
        using namespace std::chrono;
        auto utc = system_clock::time_point{} + seconds{1704067200};  // 2024-01-01
        auto stp = fixpp::core::steady_time_point{} + seconds{0};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock = clock;
        engine.executor = ioc.get_executor();
    }

    // `factory`, when supplied, lets the caller reach into `factory->last_store`
    // after Session construction (O1: to arm force_empty_retrieve). Defaults to
    // a fresh factory for tests that don't need that access.
    SessionConfig make_cfg(std::shared_ptr<CapturingStoreFactory> factory = nullptr) {
        SessionConfig cfg;
        cfg.sender_comp_id = "ISLD";
        cfg.target_comp_id = "TW";
        cfg.begin_string = "FIX.4.4";
        cfg.heartbeat_interval = 0s;
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override = ioc.get_executor();
        cfg.reset_seqnum_policy_field = reset_seqnum_policy::bilateral_lenient;
        cfg.store_factory =
            factory ? std::move(factory) : std::make_shared<CapturingStoreFactory>();
        cfg.transport_send = [this](std::span<const std::byte> frame) {
            captured_frames.emplace_back(frame.begin(), frame.end());
        };
        return cfg;
    }

    void drive_to_active(Session& sess) {
        auto fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(
                ioc, fut, kWindow, "ResendAnswerReplayTest::drive_to_active/open")) {
            fixpp::test_support::cancel_and_drain_or_report(
                ioc, *clock, "ResendAnswerReplayTest::drive_to_active/open");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss
                          << "ResendAnswerReplayTest::drive_to_active/open";
            return;
        }
        ASSERT_TRUE(fut.get().has_value()) << "open() failed";

        auto logon = make_peer_logon_44(1, "TW", "ISLD");
        auto fut2 = asio::co_spawn(ioc, sess.on_inbound_frame(logon), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(
                ioc, fut2, kWindow, "ResendAnswerReplayTest::drive_to_active/logon")) {
            fixpp::test_support::cancel_and_drain_or_report(
                ioc, *clock, "ResendAnswerReplayTest::drive_to_active/logon");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss
                          << "ResendAnswerReplayTest::drive_to_active/logon";
            return;
        }
        ASSERT_TRUE(fut2.get().has_value()) << "peer Logon feed failed";
        ASSERT_EQ(sess.state(), fsm_state::Active);
        captured_frames.clear();  // discard open()/logon-ack frames
    }

    // Returns on_inbound_frame's result, for the tests that assert it.
    fixpp::core::expected_t<void> feed_result(Session& sess, const std::vector<std::byte>& frame) {
        auto fut = asio::co_spawn(ioc, sess.on_inbound_frame(frame), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, kWindow,
                                                        "ResendAnswerReplayTest::feed/frame")) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock,
                                                            "ResendAnswerReplayTest::feed/frame");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss
                          << "ResendAnswerReplayTest::feed/frame";
            return {};  // the ADD_FAILURE above already fails the test
        }
        return fut.get();
    }

    // For the tests that need only the side effects.
    void feed(Session& sess, const std::vector<std::byte>& frame) {
        (void)feed_result(sess, frame);
    }

    // Sends a minimal bodyless NewOrderSingle via the public API (so the store
    // records it and the outbound seqnum advances normally), returns the
    // assigned MsgSeqNum(34), and clears captured_frames. Shared by the
    // build_replay_frame tests below, which then overwrite the just-stored
    // record's bytes in place (CapturingStore::outbound_records) to feed a
    // hand-crafted stored frame through the real resend-reply path.
    seqnum_t send_and_capture_seq(Session& sess, const char* label) {
        auto payload = to_payload("35=D\x01");
        auto fut =
            asio::co_spawn(ioc, sess.send(std::span<const std::byte>(payload)), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, kWindow, label)) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock, label);
            ADD_FAILURE() << fixpp::test_support::kWindowMiss << label;
            return 0;
        }
        EXPECT_TRUE(fut.get().has_value()) << label;
        EXPECT_FALSE(captured_frames.empty()) << label;
        const auto tag34_opt =
            extract_field(std::span<const std::byte>(captured_frames.back()), 34);
        EXPECT_TRUE(tag34_opt.has_value()) << label;
        const auto seq = static_cast<seqnum_t>(std::stoul(std::string(*tag34_opt)));
        captured_frames.clear();
        return seq;
    }
};

}  // namespace

// RED (pre-#419-fix): build_replay_frame copies the stored frame's header +
// full body verbatim, then appends 43=Y+122 after the loop.
TEST_F(ResendAnswerReplayTest, Replay_NoHeaderTagAfterBody_WithNestedRepeatingGroup) {
    auto cfg = make_cfg();
    Session sess(engine, cfg);
    drive_to_active(sess);

    // NewOrderSingle with NoPartyIDs(453)=2, the first party entry carrying a
    // nested NoPartySubIDs(802)=1 group — mirrors the issue's live evidence
    // (453=2, 448=BROKER01, ..., 803=3).
    constexpr std::string_view kClOrdId = "FXCL-B01-0001";
    const char kPayloadStr[] =
        "35=D\x01"
        "11=FXCL-B01-0001\x01"
        "54=1\x01"
        "40=2\x01"
        "60=20260614-12:00:00.000\x01"
        "55=AAPL\x01"
        "38=100\x01"
        "44=190.5\x01"
        "453=2\x01"
        "448=BROKER01\x01"
        "447=D\x01"
        "452=1\x01"
        "802=1\x01"
        "523=SUB1\x01"
        "803=3\x01"
        "448=CPTY01\x01"
        "447=D\x01"
        "452=3\x01";
    auto payload = to_payload(kPayloadStr);

    auto fut_send =
        asio::co_spawn(ioc, sess.send(std::span<const std::byte>(payload)), asio::use_future);
    if (!fixpp::test_support::run_window_then_ready(ioc, fut_send, kWindow,
                                                    "Replay_NoHeaderTagAfterBody/send")) {
        fixpp::test_support::cancel_and_drain_or_report(ioc, *clock,
                                                        "Replay_NoHeaderTagAfterBody/send");
        ADD_FAILURE() << fixpp::test_support::kWindowMiss << "Replay_NoHeaderTagAfterBody/send";
        return;
    }
    ASSERT_TRUE(fut_send.get().has_value()) << "Session::send must succeed";
    ASSERT_FALSE(captured_frames.empty()) << "app message must have been emitted";

    // Sanity: the ORIGINAL send (no PossDup) must already pass the order check —
    // this isolates the RED/GREEN signal to the REPLAY path, not the plain send.
    {
        const auto original_check = check_resend_answer_field_order(captured_frames.back());
        ASSERT_TRUE(original_check.ok)
            << "precondition: the original (non-replayed) send must itself be well-ordered; got: "
            << original_check.reason;
        ASSERT_EQ(original_check.count_43, 0U)
            << "precondition: the original send must not carry PossDupFlag(43)";
    }

    // Extract MsgSeqNum(34) from the original send. Position-independent lookup
    // is fine here — this reads a VALUE, not a position (the sibling helpers in
    // this directory, e.g. test_sending_time_precision.cpp, use the same shared
    // extract_field for exactly this purpose).
    const auto tag34_opt = extract_field(std::span<const std::byte>(captured_frames.back()), 34);
    ASSERT_TRUE(tag34_opt.has_value()) << "outbound frame must carry tag 34 (MsgSeqNum)";
    const seqnum_t app_seq = static_cast<seqnum_t>(std::stoul(std::string(*tag34_opt)));

    captured_frames.clear();

    // Peer inbound seqnum is 2 (Logon was seq=1; ResendRequest is next).
    auto rr = make_resend_request(app_seq, app_seq, /*inbound_seq=*/2, "TW", "ISLD");
    feed(sess, rr);

    // Identify the replayed frame UNAMBIGUOUSLY: 35=D, 34==app_seq, and
    // 11==the original ClOrdID. [O1] count_43>=1 alone is NOT sufficient — a
    // SequenceReset-GapFill also carries a well-ordered 43=Y/122 (Cell 1), so
    // a witness that treats "any frame with 43" as "the replay" would be
    // fooled by a GapFill substituted for a genuine regression elsewhere (see
    // GapFillOnly_IsNotMistakenForTheAppReplay below, which proves this
    // discriminates). Assert exactly one such frame, and that no GapFill
    // (35=4) was emitted for this single, present, non-admin slot.
    std::size_t replay_matches = 0;
    std::size_t gapfill_matches = 0;
    for (const auto& f : captured_frames) {
        const std::span<const std::byte> fs(f);
        const auto mt = extract_field(fs, 35);
        if (mt == "4") {
            ++gapfill_matches;
            continue;
        }
        if (mt != "D") continue;
        if (extract_field(fs, 34) != std::to_string(app_seq)) continue;
        if (extract_field(fs, 11) != kClOrdId) continue;
        ++replay_matches;

        const auto check = check_resend_answer_field_order(f);
        EXPECT_TRUE(check.ok) << check.reason;
        EXPECT_EQ(check.count_43, 1U) << "replayed frame must carry PossDupFlag(43) exactly once";
        EXPECT_EQ(check.count_122, 1U)
            << "replayed frame must carry OrigSendingTime(122) exactly once";
    }
    EXPECT_EQ(replay_matches, 1U) << "ResendRequest for the stored NewOrderSingle (seq=" << app_seq
                                  << ", ClOrdID=" << kClOrdId
                                  << ") must produce EXACTLY ONE replayed frame identified by "
                                  << "35=D + 34==seq + 11==ClOrdID";
    EXPECT_EQ(gapfill_matches, 0U)
        << "a single present, non-admin slot must be replayed, not gap-filled";
}

// [O1] Negative counterpart: force the store to visit nothing for the resend
// range (force_empty_retrieve), so replay_outbound_range_ answers with a
// SequenceReset-GapFill only — the exact substitution the identification
// above must not be fooled by. Proves the discrimination directly, rather
// than relying on the positive cell never happening to hit this case.
TEST_F(ResendAnswerReplayTest, GapFillOnly_IsNotMistakenForTheAppReplay) {
    auto factory = std::make_shared<CapturingStoreFactory>();
    auto cfg = make_cfg(factory);
    Session sess(engine, cfg);
    drive_to_active(sess);

    const char kPayloadStr[] =
        "35=D\x01"
        "11=ORD-O1\x01"
        "54=1\x01";
    auto payload = to_payload(kPayloadStr);
    auto fut_send =
        asio::co_spawn(ioc, sess.send(std::span<const std::byte>(payload)), asio::use_future);
    if (!fixpp::test_support::run_window_then_ready(
            ioc, fut_send, kWindow, "GapFillOnly_IsNotMistakenForTheAppReplay/send")) {
        fixpp::test_support::cancel_and_drain_or_report(
            ioc, *clock, "GapFillOnly_IsNotMistakenForTheAppReplay/send");
        ADD_FAILURE() << fixpp::test_support::kWindowMiss
                      << "GapFillOnly_IsNotMistakenForTheAppReplay/send";
        return;
    }
    ASSERT_TRUE(fut_send.get().has_value()) << "Session::send must succeed";
    ASSERT_FALSE(captured_frames.empty());
    const auto tag34_opt = extract_field(std::span<const std::byte>(captured_frames.back()), 34);
    ASSERT_TRUE(tag34_opt.has_value());
    const seqnum_t app_seq = static_cast<seqnum_t>(std::stoul(std::string(*tag34_opt)));
    captured_frames.clear();

    ASSERT_NE(factory->last_store, nullptr) << "store must have been created by open()";
    factory->last_store->force_empty_retrieve = true;

    auto rr = make_resend_request(app_seq, app_seq, /*inbound_seq=*/2, "TW", "ISLD");
    feed(sess, rr);

    std::size_t replay_matches = 0;
    std::size_t gapfill_matches = 0;
    for (const auto& f : captured_frames) {
        const std::span<const std::byte> fs(f);
        const auto mt = extract_field(fs, 35);
        if (mt == "4") ++gapfill_matches;
        if (mt == "D" && extract_field(fs, 34) == std::to_string(app_seq) &&
            extract_field(fs, 11) == "ORD-O1") {
            ++replay_matches;
        }
    }
    EXPECT_EQ(gapfill_matches, 1U)
        << "sanity: force_empty_retrieve must actually produce a GapFill, or this "
           "cell proves nothing";
    EXPECT_EQ(replay_matches, 0U)
        << "no 35=D frame identified as the app replay may appear when the store "
           "could not retrieve it — a GapFill must not be mistaken for the replay";
}

// [C6] build_replay_frame's degenerate fallback (no stored tag outside the
// header set, so the header/body-boundary insertion point in the main loop
// is never reached): send a payload whose ENTIRE stored frame is
// 8,9,35,34,49,52,56,10 — no body field at all — and confirm the replay
// still carries 43/122 exactly once, well-ordered. Reachable via the public
// API: Session::send("35=D\x01") passes T008 validation (payload leads with
// "35=", ends with SOH, non-empty MsgType) and appends no other field, so the
// stored frame has nothing outside {8,34,35,49,52,56}.
TEST_F(ResendAnswerReplayTest, Replay_NoBodyFallback_StillCarries43And122) {
    auto cfg = make_cfg();
    Session sess(engine, cfg);
    drive_to_active(sess);

    const char kPayloadStr[] = "35=D\x01";
    auto payload = to_payload(kPayloadStr);
    auto fut_send =
        asio::co_spawn(ioc, sess.send(std::span<const std::byte>(payload)), asio::use_future);
    if (!fixpp::test_support::run_window_then_ready(ioc, fut_send, kWindow,
                                                    "Replay_NoBodyFallback/send")) {
        fixpp::test_support::cancel_and_drain_or_report(ioc, *clock, "Replay_NoBodyFallback/send");
        ADD_FAILURE() << fixpp::test_support::kWindowMiss << "Replay_NoBodyFallback/send";
        return;
    }
    ASSERT_TRUE(fut_send.get().has_value()) << "Session::send must succeed";
    ASSERT_FALSE(captured_frames.empty());
    const auto tag34_opt = extract_field(std::span<const std::byte>(captured_frames.back()), 34);
    ASSERT_TRUE(tag34_opt.has_value());
    const seqnum_t app_seq = static_cast<seqnum_t>(std::stoul(std::string(*tag34_opt)));
    captured_frames.clear();

    auto rr = make_resend_request(app_seq, app_seq, /*inbound_seq=*/2, "TW", "ISLD");
    feed(sess, rr);

    // No ClOrdID(11) exists in this payload, so identification drops that
    // clause; 35=D + 34==app_seq is unambiguous here (no GapFill carries 35=D).
    std::size_t replay_matches = 0;
    for (const auto& f : captured_frames) {
        const std::span<const std::byte> fs(f);
        if (extract_field(fs, 35) != "D") continue;
        if (extract_field(fs, 34) != std::to_string(app_seq)) continue;
        ++replay_matches;

        const auto check = check_resend_answer_field_order(f);
        EXPECT_TRUE(check.ok) << check.reason;
        EXPECT_EQ(check.count_43, 1U) << "fallback-path replay must carry PossDupFlag(43) once";
        EXPECT_EQ(check.count_122, 1U)
            << "fallback-path replay must carry OrigSendingTime(122) once";
    }
    EXPECT_EQ(replay_matches, 1U) << "ResendRequest for the bodyless stored frame (seq=" << app_seq
                                  << ") must produce exactly one replayed frame";
}

// ── build_sequence_reset_gapfill: the append_raw(43, ...) / append_raw(122,
// ...) failure branches ──────────────────────────────────────────────────────
//
// Buffer-boundary witness. wire::Writer (writer.cpp) reserves a fixed 6-digit
// BodyLength(9) placeholder right after the first field (BeginString(8)) and
// writes every following field at a byte position that is a deterministic
// function of the tag/value lengths below -- independent of whether the call
// ultimately succeeds, since a truncated call never reaches commit()'s
// memmove/backpatch. field_bytes(tag, value_len) models one "tag=value\x01"
// field's on-wire cost; kPlaceholderBytes models the fixed "9=000000\x01"
// reservation.
//
// Every boundary asserted below is DERIVED from field_bytes/kPlaceholderBytes
// rather than a hardcoded literal, so the test fails loud -- not silently
// retargets a different field's failure branch -- if the Writer's placeholder
// width or the builder's field order ever changes.
TEST(ResendAnswerFieldOrder, GapFill_AppendFailureBranches43And122ArePinned) {
    constexpr std::string_view kSender = "ISLD";
    constexpr std::string_view kTarget = "TW";
    constexpr std::string_view kBeginString = "FIX.4.4";
    constexpr std::string_view kSendingTime = "20260614-12:00:00.000";
    constexpr fixpp::session::seqnum_t kSeq = 5;
    constexpr fixpp::session::seqnum_t kNewSeqno = 10;

    const auto field_bytes = [](std::uint32_t tag, std::size_t value_len) -> std::size_t {
        return std::to_string(tag).size() + 1 /* '=' */ + value_len + 1 /* SOH */;
    };
    constexpr std::size_t kPlaceholderBytes = 9;  // "9=000000\x01"

    const std::size_t body_start = field_bytes(8, kBeginString.size()) + kPlaceholderBytes;
    const std::size_t pos_after_35 = body_start + field_bytes(35, 1);
    const std::size_t pos_after_34 = pos_after_35 + field_bytes(34, std::to_string(kSeq).size());
    const std::size_t pos_after_49 = pos_after_34 + field_bytes(49, kSender.size());
    const std::size_t pos_after_52 = pos_after_49 + field_bytes(52, kSendingTime.size());
    const std::size_t pos_after_56 = pos_after_52 + field_bytes(56, kTarget.size());
    const std::size_t pos_after_43 = pos_after_56 + field_bytes(43, 1);
    const std::size_t pos_after_122 = pos_after_43 + field_bytes(122, kSendingTime.size());
    const std::size_t pos_after_36 =
        pos_after_122 + field_bytes(36, std::to_string(kNewSeqno).size());
    const std::size_t pos_after_123 = pos_after_36 + field_bytes(123, 1);

    const std::size_t body_length = pos_after_123 - body_start;
    const std::size_t actual_digits = std::to_string(body_length).size();
    ASSERT_LE(actual_digits, 6U) << "body exceeds the 6-digit BodyLength placeholder reservation";
    const std::size_t gap = 6 - actual_digits;  // over-reservation memmove'd away at commit()
    const std::size_t minimal_success_size = (pos_after_123 - gap) + 7;  // trailer "10=NNN\x01"

    // NOTE: `buf` must outlive the returned span (build_sequence_reset_gapfill
    // returns a subspan of its `out` argument), so each call site owns its own
    // buffer rather than a lambda-local one.
    auto build = [&](std::vector<std::byte>& buf) {
        return fixpp::session::build_sequence_reset_gapfill(std::span<std::byte>{buf}, kSeq,
                                                            kSender, kTarget, kNewSeqno,
                                                            kBeginString, kSendingTime);
    };

    // Room for everything through TargetCompID(56) but not PossDupFlag(43):
    // the append_raw(43, ...) call must fail there.
    std::vector<std::byte> buf_before_43(pos_after_56);
    EXPECT_FALSE(build(buf_before_43).has_value())
        << "buffer sized one field short of PossDupFlag(43) must fail";

    // Room through 43 but not OrigSendingTime(122): the append_raw(122, ...)
    // call must fail there.
    std::vector<std::byte> buf_before_122(pos_after_43);
    EXPECT_FALSE(build(buf_before_122).has_value())
        << "buffer sized one field short of OrigSendingTime(122) must fail";

    // Model pin: one byte short of the full frame fails; the exact minimal
    // size succeeds and returns a frame of exactly that length.
    std::vector<std::byte> buf_short(minimal_success_size - 1);
    EXPECT_FALSE(build(buf_short).has_value())
        << "one byte short of the modeled minimal size must fail";
    std::vector<std::byte> buf_full(minimal_success_size);
    auto full = build(buf_full);
    ASSERT_TRUE(full.has_value()) << "modeled minimal_success_size must be sufficient";
    EXPECT_EQ(full->size(), minimal_success_size);
    {
        const auto full_check = check_resend_answer_field_order(*full);
        EXPECT_TRUE(full_check.ok) << full_check.reason;
    }
}

// ── build_replay_frame: malformed stored field skipped in both scans ────────
//
// scan_field flags a field malformed when a non-digit character appears
// before its '=' (e.g. "4X=..."). The pre-scan pass (looking for stored
// SendingTime(52)) and the write loop (copying fields into the replay) share
// scan_field, so the malformed field is skipped identically in both -- they
// can never desync. Placed BEFORE the stored 52 field so the pre-scan's
// `if (!fr.ok) continue;` is exercised too: the pre-scan breaks as soon as it
// finds 52, so a malformed field placed AFTER 52 would never reach it.
TEST_F(ResendAnswerReplayTest, Replay_MalformedStoredField_SkippedInBothScans) {
    auto factory = std::make_shared<CapturingStoreFactory>();
    auto cfg = make_cfg(factory);
    Session sess(engine, cfg);
    drive_to_active(sess);

    const seqnum_t app_seq =
        send_and_capture_seq(sess, "Replay_MalformedStoredField_SkippedInBothScans/send");

    ASSERT_NE(factory->last_store, nullptr);
    ASSERT_FALSE(factory->last_store->outbound_records.empty());
    // build_replay_frame never requires tags 9/10 to be present -- it only
    // ever skips them if seen -- so the hand-crafted stored bytes below omit
    // them.
    std::string stored;
    stored += "8=FIX.4.4\x01";
    stored += "35=D\x01";
    stored += "34=" + std::to_string(app_seq) + "\x01";
    stored += "49=ISLD\x01";
    stored += "4X=SENTINEL_MALFORMED_VALUE\x01";  // malformed: non-digit tag char
    stored += "52=20260614-12:00:00.000\x01";
    stored += "56=TW\x01";
    stored += "11=ORD-MALFORMED\x01";
    factory->last_store->outbound_records.back().frame = to_payload(stored);

    auto rr = make_resend_request(app_seq, app_seq, /*inbound_seq=*/2, "TW", "ISLD");
    feed(sess, rr);

    std::size_t replay_matches = 0;
    for (const auto& f : captured_frames) {
        const std::span<const std::byte> fs(f);
        if (extract_field(fs, 35) != "D") continue;
        if (extract_field(fs, 34) != std::to_string(app_seq)) continue;
        ++replay_matches;

        const auto check = check_resend_answer_field_order(f);
        EXPECT_TRUE(check.ok) << check.reason;
        EXPECT_EQ(check.count_43, 1U);
        EXPECT_EQ(check.count_122, 1U);
        EXPECT_EQ(extract_field(fs, 11), "ORD-MALFORMED")
            << "the well-formed body field after the malformed one must still be replayed";

        const std::string_view f_sv(reinterpret_cast<const char*>(f.data()), f.size());
        EXPECT_EQ(f_sv.find("SENTINEL_MALFORMED_VALUE"), std::string_view::npos)
            << "a malformed stored field must be dropped, not copied into the replay";
    }
    EXPECT_EQ(replay_matches, 1U);
}

namespace {

// The SendingTime the replay tests' clock reads 10 s after the fixture's T0.
constexpr std::string_view kResendStamp = "20240101-00:00:10.000";

// A stored NewOrderSingle whose header carries `sending_time_field` (e.g.
// "52=...\x01", or "" for none) followed by `body`.
std::string stored_frame(seqnum_t app_seq, std::string_view sending_time_field,
                         std::string_view body) {
    std::string stored;
    stored += "8=FIX.4.4\x01";
    stored += "35=D\x01";
    stored += "34=" + std::to_string(app_seq) + "\x01";
    stored += "49=ISLD\x01";
    stored += sending_time_field;
    stored += "56=TW\x01";
    stored += body;
    return stored;
}

std::string stored_frame_without_52(seqnum_t app_seq) {
    return stored_frame(app_seq, "", "11=ORD-NO52\x01");
}

}  // namespace

// ── build_replay_frame: no stored SendingTime(52) ────────────────────────────
//
// fixpp#424 ruling (2026-09-14): the message is still replayed -- the peer
// keeps the business message -- with SendingTime(52) := the retransmission
// stamp and OrigSendingTime(122) := that same value. FIX-SL 2020 StandardHeader,
// OrigSendingTime: "If data is not available set to same value as SendingTime".
// Before the fix no 52 was emitted and 122 went out PRESENT but EMPTY -- the
// one fail-OPEN branch on this path. Rejected: a GapFill (drops the message),
// and both QuickFIX engines' FieldNotFound (abandons the resend). Reachable
// only via a custom/corrupted MessageStore -- Session::send_impl always stamps
// 52. The clock is advanced so the expected stamp is not the fixture's T0.
TEST_F(ResendAnswerReplayTest, Replay_NoStoredSendingTime_Emits52And122FromTheResendClock) {
    auto factory = std::make_shared<CapturingStoreFactory>();
    auto cfg = make_cfg(factory);
    Session sess(engine, cfg);
    drive_to_active(sess);

    const seqnum_t app_seq =
        send_and_capture_seq(sess, "Replay_NoStoredSendingTime_Emits52And122/send");

    ASSERT_NE(factory->last_store, nullptr);
    ASSERT_FALSE(factory->last_store->outbound_records.empty());
    factory->last_store->outbound_records.back().frame =
        to_payload(stored_frame_without_52(app_seq));

    // #289 batch 19 -- ESCALATION ROW, DISPOSITIONED: KIND A (time stamp).
    // What reads this advance is the replay's synchronous `now()` stamp inside the
    // `feed` of the ResendRequest below, on this thread; the oracle compares 52/122
    // with that stamp. No assertion here waits on a timer firing.
    // ⚠️ RE-DERIVE if an assertion ever reads a frame emitted by a timer.
    clock->advance(std::chrono::seconds{10});  // well inside the RR's own 120 s MaxLatency
    feed(sess,
         make_resend_request(app_seq, app_seq, /*inbound_seq=*/2, "TW", "ISLD", kResendStamp));

    std::size_t replay_matches = 0;
    for (const auto& f : captured_frames) {
        const std::span<const std::byte> fs(f);
        if (extract_field(fs, 35) != "D") continue;
        if (extract_field(fs, 34) != std::to_string(app_seq)) continue;
        ++replay_matches;

        const auto check = check_resend_answer_field_order(f);
        EXPECT_TRUE(check.ok) << check.reason;
        EXPECT_EQ(check.count_43, 1U);
        EXPECT_EQ(check.count_122, 1U);

        const auto st = extract_field(fs, 52);
        ASSERT_TRUE(st.has_value()) << "SendingTime(52) must be emitted though the store had none";
        EXPECT_EQ(*st, kResendStamp) << "52 := the retransmission stamp";
        const auto ost = extract_field(fs, 122);
        ASSERT_TRUE(ost.has_value()) << "OrigSendingTime(122) must be present (count_122==1)";
        EXPECT_EQ(*ost, *st) << "122 := the new 52 when the stored frame has none (#424)";
    }
    EXPECT_EQ(replay_matches, 1U);
}

// ── build_replay_frame: a long stored SendingTime(52) replays intact ─────────
//
// The replay buffer (Session::replay_outbound_range_'s kRpBufSize) is the
// capture buffer (CaptureVisitor::kCapBufSize) plus a fixed headroom, which
// must hold what a replay adds to a capturable frame. Before fixpp#420 the
// stored 52 was copied AND duplicated into OrigSendingTime(122), so a long
// stored 52 counted twice, exceeded that headroom, and the slot was skipped.
// After #420 the stored value moves to 122 and 52 carries the fixed-width
// retransmission stamp. These tests pin that a frame with ONE long 52 replays,
// at both 43/122 insertion sites. A frame with MANY 52 fields still outgrows
// the buffer, because each is restamped; see the overflow tests below.
namespace {

void expect_replayed_intact(const std::vector<std::vector<std::byte>>& captured_frames,
                            const Session& sess, seqnum_t app_seq,
                            std::string_view stored_sending_time) {
    std::size_t replays = 0;
    for (const auto& f : captured_frames) {
        const std::span<const std::byte> fs(f);
        const auto mt = extract_field(fs, 35);
        EXPECT_FALSE(mt == "4") << "a buildable replay must not be gap-filled";
        if (mt != "D" || extract_field(fs, 34) != std::to_string(app_seq)) continue;
        ++replays;
        const auto check = check_resend_answer_field_order(f);
        EXPECT_TRUE(check.ok) << check.reason;
        EXPECT_EQ(check.count_43, 1U);
        EXPECT_EQ(check.count_122, 1U);
        EXPECT_EQ(extract_field(fs, 52), kResendStamp) << "#420: 52 := the retransmission stamp";
        EXPECT_EQ(extract_field(fs, 122), stored_sending_time) << "#420: 122 := the stored 52";
    }
    EXPECT_EQ(replays, 1U);
    for (const auto& ev : sess.recent_events()) {
        EXPECT_FALSE(std::holds_alternative<session_event_resend_slot_gap_filled>(ev));
    }
}

}  // namespace

// 43/122 inserted at the header/body boundary (the stored frame has a body).
TEST_F(ResendAnswerReplayTest, Replay_LongStoredSendingTime_ReplaysIntact_AtBodyInsertion) {
    auto factory = std::make_shared<CapturingStoreFactory>();
    auto cfg = make_cfg(factory);
    Session sess(engine, cfg);
    drive_to_active(sess);

    const seqnum_t app_seq = send_and_capture_seq(
        sess, "Replay_LongStoredSendingTime_ReplaysIntact_AtBodyInsertion/send");

    ASSERT_NE(factory->last_store, nullptr);
    ASSERT_FALSE(factory->last_store->outbound_records.empty());
    const std::string huge_sending_time(3900, 'S');
    factory->last_store->outbound_records.back().frame =
        to_payload(stored_frame(app_seq, "52=" + huge_sending_time + "\x01", "11=X\x01"));

    // #289 batch 19 -- ESCALATION ROW, DISPOSITIONED: KIND A (time stamp).
    // What reads this advance is the replay's synchronous `now()` stamp inside the
    // `feed` of the ResendRequest below, on this thread; the oracle compares 52/122
    // with that stamp. No assertion here waits on a timer firing.
    // ⚠️ RE-DERIVE if an assertion ever reads a frame emitted by a timer.
    clock->advance(std::chrono::seconds{10});
    feed(sess,
         make_resend_request(app_seq, app_seq, /*inbound_seq=*/2, "TW", "ISLD", kResendStamp));

    expect_replayed_intact(captured_frames, sess, app_seq, huge_sending_time);
}

// Header-only stored frame: the loop never meets a body tag, so the fallback
// after the loop inserts 43/122.
TEST_F(ResendAnswerReplayTest, Replay_LongStoredSendingTime_ReplaysIntact_InNoBodyFallback) {
    auto factory = std::make_shared<CapturingStoreFactory>();
    auto cfg = make_cfg(factory);
    Session sess(engine, cfg);
    drive_to_active(sess);

    const seqnum_t app_seq = send_and_capture_seq(
        sess, "Replay_LongStoredSendingTime_ReplaysIntact_InNoBodyFallback/send");

    ASSERT_NE(factory->last_store, nullptr);
    ASSERT_FALSE(factory->last_store->outbound_records.empty());
    const std::string huge_sending_time(3900, 'S');
    factory->last_store->outbound_records.back().frame =
        to_payload(stored_frame(app_seq, "52=" + huge_sending_time + "\x01", ""));

    // #289 batch 19 -- ESCALATION ROW, DISPOSITIONED: KIND A (time stamp).
    // What reads this advance is the replay's synchronous `now()` stamp inside the
    // `feed` of the ResendRequest below, on this thread; the oracle compares 52/122
    // with that stamp. No assertion here waits on a timer firing.
    // ⚠️ RE-DERIVE if an assertion ever reads a frame emitted by a timer.
    clock->advance(std::chrono::seconds{10});
    feed(sess,
         make_resend_request(app_seq, app_seq, /*inbound_seq=*/2, "TW", "ISLD", kResendStamp));

    expect_replayed_intact(captured_frames, sess, app_seq, huge_sending_time);
}

// ── fixpp#424 (D4a): an unbuildable stored message is gap-filled, not skipped ─
//
// Ruling (2026-09-14): a stored application message whose replay frame cannot
// be built is folded into the surrounding SequenceReset-GapFill run and a
// session_event_resend_slot_gap_filled records it. Before the fix the slot was
// skipped silently -- no replay AND no GapFill -- so the peer's gap never
// closed. Rejected: failing the whole resend.
//
// Two triggers are witnessed. A stored frame with many SendingTime(52) fields
// outgrows the replay buffer (wire_field_value_truncated), as a corrupted store
// can produce. A Session with no clock replaying a frame with no 52 has no value
// for 52 or 122 (wire_required_field_missing). The second is a direct-Session
// posture only -- Engine::open rejects a null clock -- and the GapFill it emits
// carries an empty 52/122, as every admin frame of a clock-less Session does.
namespace {

void expect_slot_gap_filled(const std::vector<std::vector<std::byte>>& captured_frames,
                            const Session& sess, seqnum_t app_seq, fixpp::core::error why) {
    std::size_t replays = 0;
    std::size_t covering_gapfills = 0;
    for (const auto& f : captured_frames) {
        const std::span<const std::byte> fs(f);
        const auto mt = extract_field(fs, 35);
        if (mt == "D" && extract_field(fs, 34) == std::to_string(app_seq)) ++replays;
        if (mt != "4") continue;
        const auto check = check_resend_answer_field_order(f);
        EXPECT_TRUE(check.ok) << check.reason;
        EXPECT_TRUE(extract_field(fs, 123) == "Y")
            << "must be a GapFill, not a SequenceReset-Reset";
        const auto seq = std::stoul(std::string(extract_field(fs, 34).value_or("0")));
        const auto new_seq = std::stoul(std::string(extract_field(fs, 36).value_or("0")));
        if (seq <= app_seq && app_seq < new_seq) ++covering_gapfills;
    }
    EXPECT_EQ(replays, 0U) << "an unbuildable replay must not reach the wire";
    EXPECT_EQ(covering_gapfills, 1U) << "#424 D4a: the slot must be gap-filled, not skipped";

    std::size_t events = 0;
    for (const auto& ev : sess.recent_events()) {
        const auto* gf = std::get_if<session_event_resend_slot_gap_filled>(&ev);
        if (gf == nullptr) continue;
        EXPECT_EQ(gf->seq, app_seq);
        EXPECT_EQ(gf->code, why);
        ++events;
    }
    EXPECT_EQ(events, 1U) << "#424 D4a: a gap-filled business message must be recorded";
}

}  // namespace

TEST_F(ResendAnswerReplayTest, Replay_NoClockAndNoStoredSendingTime_SlotIsGapFilled) {
    engine.clock = nullptr;  // the fixture's `clock` stays alive for the pump helpers
    auto factory = std::make_shared<CapturingStoreFactory>();
    auto cfg = make_cfg(factory);
    Session sess(engine, cfg);
    drive_to_active(sess);

    const seqnum_t app_seq =
        send_and_capture_seq(sess, "Replay_NoClockAndNoStoredSendingTime_SlotIsGapFilled/send");

    ASSERT_NE(factory->last_store, nullptr);
    ASSERT_FALSE(factory->last_store->outbound_records.empty());
    factory->last_store->outbound_records.back().frame =
        to_payload(stored_frame_without_52(app_seq));

    feed(sess, make_resend_request(app_seq, app_seq, /*inbound_seq=*/2, "TW", "ISLD"));

    expect_slot_gap_filled(captured_frames, sess, app_seq,
                           fixpp::core::error::wire_required_field_missing);
}

// The ruling is "folded into the surrounding GapFill run", not "answered with
// its own GapFill". Range [1, app_seq]: seq 1 is the stored Logon reply (admin,
// so the gap run opens at 1) and app_seq is unbuildable. If the open run were
// flushed BEFORE the replay is attempted -- the pre-#424 loop order -- the
// fold would split it into two GapFills (1→app_seq and app_seq→app_seq+1).
// Exactly one is required: 34=1, 36=app_seq+1.
TEST_F(ResendAnswerReplayTest, Replay_UnbuildableSlot_JoinsTheSurroundingGapFillRun) {
    engine.clock = nullptr;
    auto factory = std::make_shared<CapturingStoreFactory>();
    auto cfg = make_cfg(factory);
    Session sess(engine, cfg);
    drive_to_active(sess);

    const seqnum_t app_seq =
        send_and_capture_seq(sess, "Replay_UnbuildableSlot_JoinsTheSurroundingGapFillRun/send");
    ASSERT_EQ(app_seq, 2U) << "precondition: the stored Logon reply occupies seq 1";

    ASSERT_NE(factory->last_store, nullptr);
    ASSERT_FALSE(factory->last_store->outbound_records.empty());
    factory->last_store->outbound_records.back().frame =
        to_payload(stored_frame_without_52(app_seq));

    feed(sess, make_resend_request(1, app_seq, /*inbound_seq=*/2, "TW", "ISLD"));

    expect_slot_gap_filled(captured_frames, sess, app_seq,
                           fixpp::core::error::wire_required_field_missing);
    std::vector<std::pair<std::string, std::string>> gapfills;  // (34, 36)
    for (const auto& f : captured_frames) {
        const std::span<const std::byte> fs(f);
        if (extract_field(fs, 35) != "4") continue;
        gapfills.emplace_back(extract_field(fs, 34).value_or(""),
                              extract_field(fs, 36).value_or(""));
    }
    ASSERT_EQ(gapfills.size(), 1U) << "#424 D4a: one GapFill run, not one per slot";
    EXPECT_EQ(gapfills[0].first, "1");
    EXPECT_EQ(gapfills[0].second, std::to_string(app_seq + 1U));
}

// ── the gap flush before a replay: a toAdmin throw ends the resend answer ────
//
// The open gap run is flushed after the replay frame is built (the ordering is
// witnessed by Replay_UnbuildableSlot_JoinsTheSurroundingGapFillRun). A toAdmin
// throw on that flush must end the resend answer with app_callback_threw, and
// the replay already built must not be sent. Range [1, app_seq]: seq 1 is the
// stored Logon reply, so the run is open when app_seq is replayed.
TEST_F(ResendAnswerReplayTest, Replay_GapFlushBeforeAReplay_ToAdminThrow_AbortsWithoutReplaying) {
    struct ThrowingToAdmin final : Application {
        bool armed = false;
        void toAdmin(const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& /*msg*/,
                     const SessionId& /*id*/) override {
            if (armed) throw std::runtime_error("toAdmin throw on the resend GapFill");
        }
    };
    auto app = std::make_shared<ThrowingToAdmin>();
    engine.application = app;
    Session sess(engine, make_cfg());
    drive_to_active(sess);

    const seqnum_t app_seq =
        send_and_capture_seq(sess, "Replay_GapFlushBeforeAReplay_ToAdminThrow/send");
    ASSERT_EQ(app_seq, 2U) << "precondition: the stored Logon reply occupies seq 1";

    app->armed = true;
    const auto r =
        feed_result(sess, make_resend_request(1, app_seq, /*inbound_seq=*/2, "TW", "ISLD"));
    ASSERT_FALSE(r.has_value()) << "a toAdmin throw on the gap flush must fail the resend answer";
    EXPECT_EQ(r.error(), fixpp::core::error::app_callback_threw);
    for (const auto& f : captured_frames) {
        EXPECT_FALSE(extract_field(std::span<const std::byte>(f), 35) == "D")
            << "the replay must not be sent after a failed gap flush";
    }
}

// ── fixpp#424: a present but EMPTY stored SendingTime(52) ─────────────────────
//
// An empty value is data that is not available, like an absent one: the replay
// carries exactly one 52 (the stored one, restamped in place) and 122 := that
// stamp. When "empty" was not told apart from "absent", the empty 52 was
// restamped AND a second 52 was inserted for the missing value.
TEST_F(ResendAnswerReplayTest, Replay_EmptyStoredSendingTime_CarriesExactlyOne52) {
    auto factory = std::make_shared<CapturingStoreFactory>();
    Session sess(engine, make_cfg(factory));
    drive_to_active(sess);

    const seqnum_t app_seq = send_and_capture_seq(sess, "Replay_EmptyStoredSendingTime/send");
    ASSERT_NE(factory->last_store, nullptr);
    ASSERT_FALSE(factory->last_store->outbound_records.empty());
    factory->last_store->outbound_records.back().frame =
        to_payload(stored_frame(app_seq, "52=\x01", "11=X\x01"));

    // #289 batch 19 -- ESCALATION ROW, DISPOSITIONED: KIND A (time stamp).
    // What reads this advance is the replay's synchronous `now()` stamp inside the
    // `feed` of the ResendRequest below, on this thread; the oracle compares 52/122
    // with that stamp. No assertion here waits on a timer firing.
    // ⚠️ RE-DERIVE if an assertion ever reads a frame emitted by a timer.
    clock->advance(std::chrono::seconds{10});
    feed(sess,
         make_resend_request(app_seq, app_seq, /*inbound_seq=*/2, "TW", "ISLD", kResendStamp));

    std::size_t replays = 0;
    for (const auto& f : captured_frames) {
        const std::span<const std::byte> fs(f);
        if (extract_field(fs, 35) != "D" || extract_field(fs, 34) != std::to_string(app_seq)) {
            continue;
        }
        ++replays;
        const std::string_view wire(reinterpret_cast<const char*>(f.data()), f.size());
        std::size_t count_52 = 0;
        for (auto at = wire.find("\x01"
                                 "52=");
             at != std::string_view::npos; at = wire.find("\x01"
                                                          "52=",
                                                          at + 1)) {
            ++count_52;
        }
        EXPECT_EQ(count_52, 1U) << "exactly one SendingTime(52)";
        EXPECT_EQ(extract_field(fs, 52), kResendStamp);
        EXPECT_EQ(extract_field(fs, 122), kResendStamp)
            << "122 := the new 52 when the stored one is empty";
    }
    EXPECT_EQ(replays, 1U);
}

// ── fixpp#424 (D4a): a replay that outgrows its buffer is gap-filled ─────────
//
// A stored frame with many empty SendingTime(52) fields fits the capture buffer,
// but every one is restamped, so its replay exceeds kRpBufSize. The slot must be
// gap-filled and recorded, not skipped (before #424 it was skipped silently).
namespace {

// A stored NewOrderSingle whose SenderCompID carries `pad` extra bytes and whose
// header repeats an empty SendingTime(52) `copies` times, followed by `body`.
std::vector<std::byte> stored_frame_with_many_52(seqnum_t seq, std::size_t copies, std::size_t pad,
                                                 std::string_view body) {
    std::string stored;
    stored += "8=FIX.4.4\x01";
    stored += "35=D\x01";
    stored += "34=" + std::to_string(seq) + "\x01";
    stored += "49=ISLD" + std::string(pad, 'P') + "\x01";
    for (std::size_t i = 0; i < copies; ++i) stored += "52=\x01";
    stored += "56=TW\x01";
    stored += body;
    return to_payload(stored);
}

}  // namespace

TEST_F(ResendAnswerReplayTest, Replay_ManyStoredSendingTimes_OutgrowTheBuffer_SlotIsGapFilled) {
    auto factory = std::make_shared<CapturingStoreFactory>();
    Session sess(engine, make_cfg(factory));
    drive_to_active(sess);

    const seqnum_t app_seq = send_and_capture_seq(sess, "Replay_ManyStoredSendingTimes/send");
    ASSERT_NE(factory->last_store, nullptr);
    ASSERT_FALSE(factory->last_store->outbound_records.empty());
    const auto stored = stored_frame_with_many_52(app_seq, /*copies=*/400, /*pad=*/0, "11=X\x01");
    ASSERT_LE(stored.size(), 4096U) << "precondition: must fit the capture buffer";
    factory->last_store->outbound_records.back().frame = stored;

    feed(sess, make_resend_request(app_seq, app_seq, /*inbound_seq=*/2, "TW", "ISLD"));

    expect_slot_gap_filled(captured_frames, sess, app_seq,
                           fixpp::core::error::wire_field_value_truncated);
}

// Wherever the replay buffer runs out, a slot is either replayed whole or
// covered by exactly one GapFill -- never sent partial, never skipped. One
// ResendRequest covers slots holding the same many-52 frame with the
// SenderCompID one byte longer each time, so the replay's size crosses the
// buffer's end. An empty `body` moves the 43/122 insertion to the no-body
// fallback. The sweep must see both outcomes, or it did not cross the end.
// Which field a slot fails at is not asserted.
TEST_F(ResendAnswerReplayTest, Replay_BufferEndSweep_SlotIsReplayedWholeOrGapFilled) {
    auto factory = std::make_shared<CapturingStoreFactory>();
    Session sess(engine, make_cfg(factory));
    drive_to_active(sess);
    ASSERT_NE(factory->last_store, nullptr);

    seqnum_t inbound_seq = 2;  // each ResendRequest needs the next inbound number
    for (const std::string_view body : {std::string_view{"11=X\x01"}, std::string_view{}}) {
        SCOPED_TRACE(body.empty() ? "no body" : "with body");
        constexpr std::size_t kCopies = 170;
        constexpr std::size_t kSlots = 90;
        captured_frames.clear();
        const seqnum_t first = factory->last_store->outbound_records.back().seq + 1U;
        for (std::size_t pad = 0; pad < kSlots; ++pad) {
            factory->last_store->add_outbound(
                first + static_cast<seqnum_t>(pad),
                stored_frame_with_many_52(first + static_cast<seqnum_t>(pad), kCopies, pad, body));
        }
        const seqnum_t last = first + static_cast<seqnum_t>(kSlots - 1);
        feed(sess, make_resend_request(first, last, inbound_seq++, "TW", "ISLD"));

        std::vector<std::pair<seqnum_t, seqnum_t>> gapfills;  // [34, 36)
        std::vector<seqnum_t> replayed;
        for (const auto& f : captured_frames) {
            const std::span<const std::byte> fs(f);
            const auto mt = extract_field(fs, 35);
            const auto seq =
                static_cast<seqnum_t>(std::stoul(std::string(extract_field(fs, 34).value_or("0"))));
            if (mt == "4") {
                gapfills.emplace_back(seq, static_cast<seqnum_t>(std::stoul(
                                               std::string(extract_field(fs, 36).value_or("0")))));
            } else if (mt == "D") {
                replayed.push_back(seq);
                const auto check = check_resend_answer_field_order(f);
                EXPECT_TRUE(check.ok) << "seq " << seq << ": " << check.reason;
                EXPECT_EQ(check.count_43, 1U) << "seq " << seq;
                EXPECT_EQ(check.count_122, 1U) << "seq " << seq;
            }
        }
        std::size_t folded = 0;
        for (seqnum_t k = first; k <= last; ++k) {
            const auto times_replayed = std::ranges::count(replayed, k);
            const auto times_covered = std::ranges::count_if(
                gapfills, [k](const auto& g) { return g.first <= k && k < g.second; });
            EXPECT_EQ(times_replayed + times_covered, 1) << "seq " << k;
            if (times_covered == 1) ++folded;
        }
        EXPECT_GT(folded, 0U) << "the sweep never ran out of buffer";
        EXPECT_LT(folded, kSlots) << "the sweep never fit the buffer";
    }
}

// ── a failed write ends the resend answer with dispatch_aborted ──────────────
//
// replay_outbound_range_'s transmit treats a transport_send that throws as a
// failed write. Each case arms, after the session is Active, a sink that throws
// on the one frame its transmit site sends -- matched on MsgType(35),
// MsgSeqNum(34) and NewSeqNo(36), so a different frame of the same type cannot
// stand in for it -- and checks that write was attempted and the answer's result.
namespace {

// The frame a case fails: `new_seq_no` is empty for a replay (no 36).
struct WriteToFail {
    std::string_view msg_type;
    std::string_view msg_seq_num;
    std::string_view new_seq_no;
};

// A transport_send that records into `frames` and, while `armed`, throws on the
// frame matching `target` instead of recording it, counting those attempts.
std::function<void(std::span<const std::byte>)> failing_sink(
    std::vector<std::vector<std::byte>>& frames, const bool& armed, WriteToFail target,
    int& attempts) {
    return [&frames, &armed, target, &attempts](std::span<const std::byte> frame) {
        const bool matches = extract_field(frame, 35) == target.msg_type &&
                             extract_field(frame, 34) == target.msg_seq_num &&
                             extract_field(frame, 36).value_or("") == target.new_seq_no;
        if (armed && matches) {
            ++attempts;
            throw std::runtime_error("transport write fails");
        }
        frames.emplace_back(frame.begin(), frame.end());
    };
}

}  // namespace

TEST_F(ResendAnswerReplayTest, ResendAnswer_WriteFails_Aborts_AtEachTransmitSite) {
    struct Case {
        const char* name;
        seqnum_t begin;  // 1 = the stored Logon reply; 2 = the app message
        seqnum_t end;
        WriteToFail fail;
    };
    // [1,2]: the GapFill flushed before the replay covers [1,2). Had slot 2 been
    // folded instead, the only GapFill would cover [1,3) and would not match.
    // [2,2]: the replay of 2. [1,1]: the trailing GapFill [1,2). [5,5] lies past
    // the last stored message, so the only frame is the nothing-to-replay
    // GapFill [5,6).
    for (const Case c :
         {Case{"gapfill before a replay", 1, 2, {"4", "1", "2"}},
          Case{"replay", 2, 2, {"D", "2", ""}}, Case{"trailing gapfill", 1, 1, {"4", "1", "2"}},
          Case{"nothing-to-replay gapfill", 5, 5, {"4", "5", "6"}}}) {
        SCOPED_TRACE(c.name);
        bool armed = false;
        int attempts = 0;
        auto cfg = make_cfg();
        cfg.transport_send = failing_sink(captured_frames, armed, c.fail, attempts);
        Session sess(engine, cfg);
        drive_to_active(sess);
        ASSERT_EQ(send_and_capture_seq(sess, "ResendAnswer_WriteFails/send"), 2U);

        armed = true;
        const auto r =
            feed_result(sess, make_resend_request(c.begin, c.end, /*inbound_seq=*/2, "TW", "ISLD"));
        armed = false;
        EXPECT_EQ(attempts, 1) << "the named transmit site must attempt its write once";
        // EXPECT, not ASSERT: an ASSERT would end the test at the first failing case
        // and leave the remaining transmit sites unchecked.
        if (r.has_value()) {
            ADD_FAILURE() << "a failed write must fail the resend answer";
        } else {
            EXPECT_EQ(r.error(), fixpp::core::error::dispatch_aborted);
        }
        for (const auto& f : captured_frames) {
            EXPECT_FALSE(extract_field(std::span<const std::byte>(f), 35) == "D")
                << "nothing may be replayed after a failed write";
        }
        captured_frames.clear();
    }
}

// ── nothing to replay: a GapFill for BeginSeqNo(7)=0 starts at 1 ─────────────
//
// With no MessageStore there is nothing to replay, so the whole range is
// answered by one GapFill. MsgSeqNum 0 does not exist, so a request that starts
// at 0 is answered from 1.
TEST_F(ResendAnswerReplayTest, NothingToReplay_BeginSeqNoZero_GapFillStartsAtOne) {
    auto cfg = make_cfg();
    cfg.store_factory = nullptr;
    Session sess(engine, cfg);
    drive_to_active(sess);

    const auto r = feed_result(sess, make_resend_request(0, 0, /*inbound_seq=*/2, "TW", "ISLD"));
    EXPECT_TRUE(r.has_value());

    std::size_t gapfills = 0;
    for (const auto& f : captured_frames) {
        const std::span<const std::byte> fs(f);
        if (extract_field(fs, 35) != "4") continue;
        ++gapfills;
        EXPECT_EQ(extract_field(fs, 34), "1") << "MsgSeqNum 0 does not exist";
        EXPECT_EQ(extract_field(fs, 123), "Y");
    }
    EXPECT_EQ(gapfills, 1U);
}

// ── fixpp#420: a replay older than the peer's MaxLatency is still accepted ──
//
// The witness is a latency run, not a byte comparison: a replay sent seconds
// after the original carries a 52 inside any latency window whether or not it
// is restamped, which is how #420 went unnoticed. Here the stored 52 is 300 s
// old when the ResendRequest arrives (MaxLatency default 120 s). The replay is
// fed to a second fixpp Session playing the peer, whose inbound SendingTime
// guard applies to every message except Reject/Logout -- PossDup included, as
// QuickFIX-J's and QuickFIX-cpp's isGoodTime do. Before the fix the replay
// carried 52 == 122 == the original send time and the peer answered
// Reject(373=10, 371=52) + Logout + Disconnect.
TEST_F(ResendAnswerReplayTest, Replay_OlderThanPeerMaxLatency_AcceptedByAFixppPeer) {
    // Counts app deliveries, so "accepted" means delivered to fromApp -- not merely
    // "not rejected". Without an Application the peer would answer any app
    // message with Reject(373=3), which says nothing about the latency guard.
    auto app = std::make_shared<CountingApplication>();
    engine.application = app;

    Session sess(engine, make_cfg());
    drive_to_active(sess);

    std::vector<std::vector<std::byte>> peer_frames;
    auto peer_cfg = make_cfg();
    peer_cfg.sender_comp_id = "TW";
    peer_cfg.target_comp_id = "ISLD";
    peer_cfg.transport_send = [&peer_frames](std::span<const std::byte> frame) {
        peer_frames.emplace_back(frame.begin(), frame.end());
    };
    Session peer(engine, peer_cfg);
    {
        auto fut = asio::co_spawn(ioc, peer.open(), asio::use_future);
        const char* label = "Replay_OlderThanPeerMaxLatency/peer-open";
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, kWindow, label)) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock, label);
            ADD_FAILURE() << fixpp::test_support::kWindowMiss << label;
            return;
        }
        ASSERT_TRUE(fut.get().has_value()) << "peer open() failed";
    }
    feed(peer, make_peer_logon_44(1, "ISLD", "TW"));
    ASSERT_EQ(peer.state(), fsm_state::Active);
    peer_frames.clear();

    const seqnum_t app_seq = send_and_capture_seq(sess, "Replay_OlderThanPeerMaxLatency/send");

    clock->advance(std::chrono::seconds{300});
    auto rr = make_resend_request(app_seq, app_seq, /*inbound_seq=*/2, "TW", "ISLD",
                                  "20240101-00:05:00.000");
    feed(sess, rr);

    const std::vector<std::byte>* replay = nullptr;
    for (const auto& f : captured_frames) {
        const std::span<const std::byte> fs(f);
        if (extract_field(fs, 35) == "D" && extract_field(fs, 34) == std::to_string(app_seq)) {
            replay = &f;
        }
    }
    ASSERT_NE(replay, nullptr) << "the ResendRequest must be answered with a replay";
    const std::span<const std::byte> rs(*replay);
    EXPECT_TRUE(extract_field(rs, 52) == "20240101-00:05:00.000") << "52 := retransmission time";
    EXPECT_TRUE(extract_field(rs, 122) == "20240101-00:00:00.000") << "122 := the stored 52";

    const int delivered_before = app->from_app_calls;
    feed(peer, *replay);
    EXPECT_EQ(peer.state(), fsm_state::Active)
        << "the peer's MaxLatency guard must accept a restamped replay of an old message";
    EXPECT_EQ(app->from_app_calls, delivered_before + 1)
        << "the replay must be delivered to the peer's application exactly once";
    for (const auto& f : peer_frames) {
        const auto mt = extract_field(std::span<const std::byte>(f), 35);
        std::string wire(reinterpret_cast<const char*>(f.data()), f.size());
        std::ranges::replace(wire, '\x01', '|');
        EXPECT_FALSE(mt == "3" || mt == "5")
            << "the peer must not Reject or Logout the replay; it sent: " << wire;
    }
}

}  // namespace fixpp::session::test
