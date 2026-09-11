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
#include <fixpp/session/direction.hpp>
#include <fixpp/session/message_store.hpp>
#include <fixpp/session/message_store_factory.hpp>
#include <fixpp/session/retrieve_visitor.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fixpp/wire/tag_scan.hpp>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "session/support/frame_field_extract.hpp"  // via -I tests/
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
    EXPECT_EQ(check.count_43, 1u) << "GapFill must carry PossDupFlag(43) exactly once";
    EXPECT_EQ(check.count_122, 1u) << "GapFill must carry OrigSendingTime(122) exactly once";
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

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> store(
        seqnum_t seq, std::span<const std::byte> frame, direction_t dir) noexcept override {
        if (dir == direction_t::outbound) {
            outbound_records.push_back({seq, std::vector<std::byte>(frame.begin(), frame.end())});
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
    cs &= 0xFFu;
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

std::vector<std::byte> make_resend_request(seqnum_t begin_seqno, seqnum_t end_seqno,
                                           std::uint32_t inbound_seq, std::string_view sender,
                                           std::string_view target) {
    std::string body;
    body += "35=2\x01";
    body += "34=" + std::to_string(inbound_seq) + "\x01";
    body += "49=" + std::string(sender) + "\x01";
    body += "52=20240101-00:00:00.000\x01";
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

    void feed(Session& sess, const std::vector<std::byte>& frame) {
        auto fut = asio::co_spawn(ioc, sess.on_inbound_frame(frame), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, kWindow,
                                                         "ResendAnswerReplayTest::feed/frame")) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock,
                                                             "ResendAnswerReplayTest::feed/frame");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss
                          << "ResendAnswerReplayTest::feed/frame";
            return;
        }
        (void)fut.get();
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
        ASSERT_EQ(original_check.count_43, 0u)
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
        EXPECT_EQ(check.count_43, 1u) << "replayed frame must carry PossDupFlag(43) exactly once";
        EXPECT_EQ(check.count_122, 1u)
            << "replayed frame must carry OrigSendingTime(122) exactly once";
    }
    EXPECT_EQ(replay_matches, 1u) << "ResendRequest for the stored NewOrderSingle (seq=" << app_seq
                                  << ", ClOrdID=" << kClOrdId
                                  << ") must produce EXACTLY ONE replayed frame identified by "
                                  << "35=D + 34==seq + 11==ClOrdID";
    EXPECT_EQ(gapfill_matches, 0u)
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
    EXPECT_EQ(gapfill_matches, 1u)
        << "sanity: force_empty_retrieve must actually produce a GapFill, or this "
           "cell proves nothing";
    EXPECT_EQ(replay_matches, 0u)
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
        EXPECT_EQ(check.count_43, 1u) << "fallback-path replay must carry PossDupFlag(43) once";
        EXPECT_EQ(check.count_122, 1u)
            << "fallback-path replay must carry OrigSendingTime(122) once";
    }
    EXPECT_EQ(replay_matches, 1u) << "ResendRequest for the bodyless stored frame (seq=" << app_seq
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
    ASSERT_LE(actual_digits, 6u) << "body exceeds the 6-digit BodyLength placeholder reservation";
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
        EXPECT_EQ(check.count_43, 1u);
        EXPECT_EQ(check.count_122, 1u);
        EXPECT_EQ(extract_field(fs, 11), "ORD-MALFORMED")
            << "the well-formed body field after the malformed one must still be replayed";

        const std::string_view f_sv(reinterpret_cast<const char*>(f.data()), f.size());
        EXPECT_EQ(f_sv.find("SENTINEL_MALFORMED_VALUE"), std::string_view::npos)
            << "a malformed stored field must be dropped, not copied into the replay";
    }
    EXPECT_EQ(replay_matches, 1u);
}

// ── build_replay_frame: no stored SendingTime(52) ────────────────────────────
//
// When no stored field carries tag 52, the pre-scan `while (i < n)` loop
// exits normally (its only other exit is the `break` on finding 52), leaving
// orig_sending_time at its default (empty) string_view. The write loop still
// inserts PossDupFlag(43)/OrigSendingTime(122) at the header/body boundary;
// wire::Writer::append_raw accepts an empty value span (write_span
// early-returns on an empty span, still writing "tag=" and the terminating
// SOH), so 122 is emitted PRESENT but EMPTY rather than omitted.
//
// FINDING (observed at this commit, not fixed here -- out of this PR's
// scope): OrigSendingTime is a UTCTimestamp field; an empty value is not a
// valid one. Every OTHER append-failure branch in this file fails CLOSED (no
// frame at all); this path fails OPEN (a frame carrying a field the FIX spec
// requires to be a real timestamp, empty instead). Reachable only via a
// custom/corrupted MessageStore -- Session::send_impl always stamps 52.
// Tracked in fixpp issue #424; a fix flips this test.
TEST_F(ResendAnswerReplayTest, Replay_NoStoredSendingTime_Emits122PresentButEmpty) {
    auto factory = std::make_shared<CapturingStoreFactory>();
    auto cfg = make_cfg(factory);
    Session sess(engine, cfg);
    drive_to_active(sess);

    const seqnum_t app_seq =
        send_and_capture_seq(sess, "Replay_NoStoredSendingTime_Emits122PresentButEmpty/send");

    ASSERT_NE(factory->last_store, nullptr);
    ASSERT_FALSE(factory->last_store->outbound_records.empty());
    std::string stored;
    stored += "8=FIX.4.4\x01";
    stored += "35=D\x01";
    stored += "34=" + std::to_string(app_seq) + "\x01";
    stored += "49=ISLD\x01";
    stored += "56=TW\x01";
    stored += "11=ORD-NO52\x01";
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
        EXPECT_EQ(check.count_43, 1u);
        EXPECT_EQ(check.count_122, 1u);

        const auto ost = extract_field(fs, 122);
        ASSERT_TRUE(ost.has_value()) << "OrigSendingTime(122) must be present (count_122==1)";
        EXPECT_TRUE(ost->empty())
            << "FINDING: with no stored SendingTime(52), OrigSendingTime(122) is emitted "
               "present but EMPTY rather than omitted -- see the TEST_F comment above";
    }
    EXPECT_EQ(replay_matches, 1u);
}

namespace {

// Shared by the three build_replay_frame overflow tests below. Walks
// captured_frames for any frame identified as this resend slot's answer --
// either the replay itself (35=D, 34==app_seq) or a substituted GapFill
// (35=4) -- and asserts it is well-formed per check_resend_answer_field_order.
// Does NOT assert that nothing was transmitted: per issue #424,
// replay_outbound_range_ currently skips a slot silently when
// build_replay_frame fails (no replay AND no GapFill for it); pinning that
// silence here would pin #424's defect rather than test this PR's overflow
// handling. What IS asserted, unconditionally: whatever ships for this slot,
// if anything, is never a malformed/partial build_replay_frame result.
void expect_no_malformed_or_partial_frame_for_slot(
    const std::vector<std::vector<std::byte>>& captured_frames, seqnum_t app_seq) {
    std::size_t candidates = 0;
    for (const auto& f : captured_frames) {
        const std::span<const std::byte> fs(f);
        const auto mt = extract_field(fs, 35);
        const bool is_replay_candidate =
            (mt == "D" && extract_field(fs, 34) == std::to_string(app_seq));
        const bool is_gapfill_candidate = (mt == "4");
        if (!is_replay_candidate && !is_gapfill_candidate) continue;
        ++candidates;
        const auto check = check_resend_answer_field_order(f);
        EXPECT_TRUE(check.ok) << "any frame transmitted for this resend slot must be well-formed "
                                 "(never a partial/malformed build_replay_frame result): "
                              << check.reason;
    }
    // Observation, not an assertion (see the function comment): at this
    // commit, `candidates` is 0 for every overflow scenario below -- the slot
    // is skipped silently, matching #424.
    (void)candidates;
}

}  // namespace

// ── build_replay_frame: overflow AT the PossDupFlag(43)/OrigSendingTime(122)
// insertion ───────────────────────────────────────────────────────────────
//
// The replay buffer (Session::replay_outbound_range_'s kRpBufSize) is the
// capture buffer (CaptureVisitor::kCapBufSize) plus a fixed headroom, and
// OrigSendingTime(122) duplicates the stored SendingTime(52) value. A stored
// 52 long enough that its duplication alone exceeds that fixed headroom
// overflows the replay buffer while still fitting (with room to spare) in
// the capture buffer -- so retrieve() succeeds (no truncation) but
// build_replay_frame fails. Here the stored SendingTime is long enough that
// the overflow happens appending 122 itself, at the header/body-boundary
// insertion point, before any stored body field is even reached.
TEST_F(ResendAnswerReplayTest, Replay_Overflow_AtPossDupInsertion) {
    auto factory = std::make_shared<CapturingStoreFactory>();
    auto cfg = make_cfg(factory);
    Session sess(engine, cfg);
    drive_to_active(sess);

    const seqnum_t app_seq = send_and_capture_seq(sess, "Replay_Overflow_AtPossDupInsertion/send");

    ASSERT_NE(factory->last_store, nullptr);
    ASSERT_FALSE(factory->last_store->outbound_records.empty());

    constexpr std::size_t kCapBufSize = 4096;       // mirrors CaptureVisitor::kCapBufSize
    constexpr std::size_t kRpBufSize = 4096 + 256;  // mirrors replay_outbound_range_'s kRpBufSize
    const std::string huge_sending_time(3900, 'S');

    std::string stored;
    stored += "8=FIX.4.4\x01";
    stored += "35=D\x01";
    stored += "34=" + std::to_string(app_seq) + "\x01";
    stored += "49=ISLD\x01";
    stored += "52=" + huge_sending_time + "\x01";
    stored += "56=TW\x01";
    stored += "11=X\x01";  // one small body field; never reached before overflow
    ASSERT_LE(stored.size(), kCapBufSize) << "precondition: must fit the capture buffer";
    // Conservative lower bound on the replayed size: duplicating the huge 52
    // as 122 alone adds at least huge_sending_time.size() new bytes.
    ASSERT_GT(stored.size() + huge_sending_time.size(), kRpBufSize)
        << "precondition: the duplicate-52 growth alone must exceed the replay buffer";
    factory->last_store->outbound_records.back().frame = to_payload(stored);

    auto rr = make_resend_request(app_seq, app_seq, /*inbound_seq=*/2, "TW", "ISLD");
    feed(sess, rr);

    expect_no_malformed_or_partial_frame_for_slot(captured_frames, app_seq);
}

// ── build_replay_frame: insertion fits, a LATER stored field overflows ──────
//
// Same headroom mechanism as above, but the stored SendingTime is short
// enough that inserting 43/122 (including the 122 duplicate) fits well
// within the replay buffer; the overflow instead happens re-appending a
// later, large stored body field -- a DIFFERENT append_raw call from the
// insertion's.
TEST_F(ResendAnswerReplayTest, Replay_Overflow_AtLaterStoredField) {
    auto factory = std::make_shared<CapturingStoreFactory>();
    auto cfg = make_cfg(factory);
    Session sess(engine, cfg);
    drive_to_active(sess);

    const seqnum_t app_seq = send_and_capture_seq(sess, "Replay_Overflow_AtLaterStoredField/send");

    ASSERT_NE(factory->last_store, nullptr);
    ASSERT_FALSE(factory->last_store->outbound_records.empty());

    constexpr std::size_t kCapBufSize = 4096;       // mirrors CaptureVisitor::kCapBufSize
    constexpr std::size_t kRpBufSize = 4096 + 256;  // mirrors replay_outbound_range_'s kRpBufSize
    const std::string moderate_sending_time(600, 'S');
    const std::string huge_body_field(3300, 'B');

    std::string header;
    header += "8=FIX.4.4\x01";
    header += "35=D\x01";
    header += "34=" + std::to_string(app_seq) + "\x01";
    header += "49=ISLD\x01";
    header += "52=" + moderate_sending_time + "\x01";
    header += "56=TW\x01";
    // Predicted pos_ once 43 and the 122-duplicate of the moderate 52 are
    // appended, starting from an empty replay buffer: header bytes copied
    // verbatim + "43=Y\x01" (5 bytes) + "122=<value>\x01" (3+1+len+1 bytes).
    const std::size_t predicted_pos_after_insertion =
        header.size() + 5 + (3 + 1 + moderate_sending_time.size() + 1);
    ASSERT_LE(predicted_pos_after_insertion, kRpBufSize)
        << "precondition: the 43/122 insertion itself must FIT the replay buffer";

    std::string stored = header;
    stored += "11=SMALL\x01";
    stored += "58=" + huge_body_field + "\x01";
    ASSERT_LE(stored.size(), kCapBufSize) << "precondition: must fit the capture buffer";
    ASSERT_GT(stored.size() + moderate_sending_time.size(), kRpBufSize)
        << "precondition: total replayed size must exceed the replay buffer";
    factory->last_store->outbound_records.back().frame = to_payload(stored);

    auto rr = make_resend_request(app_seq, app_seq, /*inbound_seq=*/2, "TW", "ISLD");
    feed(sess, rr);

    expect_no_malformed_or_partial_frame_for_slot(captured_frames, app_seq);
}

// ── build_replay_frame: overflow in the no-body fallback ────────────────────
//
// When every stored tag is in the header set (kReplayHeaderTags), the
// insertion branch inside the loop never triggers (nothing is ever "the
// first body tag"), so the loop copies the whole stored frame directly; the
// fallback `if (!inserted_pd) { append_possdup(...); }` after the loop is
// what inserts 43/122 for a header-only stored frame. A long enough stored
// 52 overflows there instead.
TEST_F(ResendAnswerReplayTest, Replay_Overflow_InNoBodyFallback) {
    auto factory = std::make_shared<CapturingStoreFactory>();
    auto cfg = make_cfg(factory);
    Session sess(engine, cfg);
    drive_to_active(sess);

    const seqnum_t app_seq = send_and_capture_seq(sess, "Replay_Overflow_InNoBodyFallback/send");

    ASSERT_NE(factory->last_store, nullptr);
    ASSERT_FALSE(factory->last_store->outbound_records.empty());

    constexpr std::size_t kCapBufSize = 4096;       // mirrors CaptureVisitor::kCapBufSize
    constexpr std::size_t kRpBufSize = 4096 + 256;  // mirrors replay_outbound_range_'s kRpBufSize
    const std::string huge_sending_time(3900, 'S');

    // No tag outside kReplayHeaderTags = {8,34,35,49,52,56} -- no body field
    // at all, so the loop's insertion branch is never reached.
    std::string stored;
    stored += "8=FIX.4.4\x01";
    stored += "35=D\x01";
    stored += "34=" + std::to_string(app_seq) + "\x01";
    stored += "49=ISLD\x01";
    stored += "52=" + huge_sending_time + "\x01";
    stored += "56=TW\x01";
    ASSERT_LE(stored.size(), kCapBufSize) << "precondition: must fit the capture buffer";
    ASSERT_GT(stored.size() + huge_sending_time.size(), kRpBufSize)
        << "precondition: the duplicate-52 growth alone must exceed the replay buffer";
    factory->last_store->outbound_records.back().frame = to_payload(stored);

    auto rr = make_resend_request(app_seq, app_seq, /*inbound_seq=*/2, "TW", "ISLD");
    feed(sess, rr);

    expect_no_malformed_or_partial_frame_for_slot(captured_frames, app_seq);
}

}  // namespace fixpp::session::test
