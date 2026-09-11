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
// Regression witness: a strict FIELD-ORDER property over every resend-answer frame
// fixpp emits (GapFill via build_sequence_reset_gapfill, replay via build_replay_frame):
//   - no standard-header tag (8/9/35/34/49/52/56/43/122) appears AFTER the first
//     body (non-header) tag;
//   - CheckSum(10) is the last field;
//   - PossDupFlag(43) and OrigSendingTime(122) each appear exactly once.
//
// The scanner walks fields with fixpp::wire::accumulate_tag_digit — the bounded
// tag-digit accumulator shared by fixpp's production wire scanners (tag_scan.hpp,
// 040-inbound-tag-overflow-hardening research.md D-1) — rather than ad hoc substring
// matching. Its header-tag set is defined independently of any production header-tag
// set: no reusable session/wire-layer header set exists in this codebase (see the
// equivalent note above `build_replay_frame` in src/session/session.cpp), so a
// wrong production set cannot hide from this witness.
//
// Cell 1 (GapFill): direct build_sequence_reset_gapfill() call.
// Cell 2 (Replay): a Session-driven resend of a stored NewOrderSingle carrying a
//   NoPartyIDs(453)/NoPartySubIDs(802) repeating group, so the header/body boundary
//   search must not stop inside a group (a substring-position scan could).
//
// RED (pre-#419-fix): Cell 1 fails because the builder emits
// ...52,56,36,123,43,122 (43/122 after body tags 36/123); Cell 2 fails because
// build_replay_frame appends 43/122 after the full stored body (groups included).
// Reproduce: `git stash` the two builder fixes (admin_messages.cpp, session.cpp),
// rebuild this target, and re-run — both cells fail with "header tag 43 appears
// AFTER a body tag".
//
// Anchors: issue #419; specs/037-resend-reply-possdup-tags/spec.md (superseded tail
// placement, Assumptions section); specs/013-session-reconnect-binding/spec.md FR-010.
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

// FIX standard-header tags relevant to a resend-answer frame. Defined
// independently of any production header-tag set — no reusable session/
// wire-layer header-tag list exists in this codebase (see the equivalent
// note above `build_replay_frame` in src/session/session.cpp).
constexpr std::array<std::uint32_t, 9> kWitnessHeaderTags = {8, 9, 35, 34, 49, 52, 56, 43, 122};

[[nodiscard]] bool is_witness_header_tag(std::uint32_t tag) noexcept {
    return std::ranges::find(kWitnessHeaderTags, tag) != kWitnessHeaderTags.end();
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
    bool seen_body_tag = false;
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

        if (tag == 10) {
            seen_trailer = true;
            continue;
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
// retrieve() source for the resend-reply store-walk. Mirrors the CapturingStore
// used by test_send_allow_pos_dup_strip.cpp / test_sending_time_precision.cpp
// (each resend-adjacent test file carries its own copy; no shared header exists).
class CapturingStore final : public MessageStore {
public:
    struct Record {
        seqnum_t seq;
        std::vector<std::byte> frame;
    };
    std::vector<Record> outbound_records;

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
    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<MessageStore>> make(
        std::string_view, std::string_view, std::pmr::memory_resource*, std::size_t,
        asio::any_io_executor) noexcept override {
        return std::make_unique<CapturingStore>();
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

    SessionConfig make_cfg() {
        SessionConfig cfg;
        cfg.sender_comp_id = "ISLD";
        cfg.target_comp_id = "TW";
        cfg.begin_string = "FIX.4.4";
        cfg.heartbeat_interval = 0s;
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override = ioc.get_executor();
        cfg.reset_seqnum_policy_field = reset_seqnum_policy::bilateral_lenient;
        cfg.store_factory = std::make_shared<CapturingStoreFactory>();
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
};

}  // namespace

// RED (pre-#419-fix): build_replay_frame copies the stored frame's header +
// full body (groups included) verbatim, then appends 43=Y+122 after the loop
// — i.e. after the NoPartySubIDs(802) group nested inside NoPartyIDs(453).
// A substring/position-oblivious scan could miss this; check_resend_answer_field_order
// walks real fields via accumulate_tag_digit so it cannot.
TEST_F(ResendAnswerReplayTest, Replay_NoHeaderTagAfterBody_WithNestedRepeatingGroup) {
    auto cfg = make_cfg();
    Session sess(engine, cfg);
    drive_to_active(sess);

    // NewOrderSingle with NoPartyIDs(453)=2, the first party entry carrying a
    // nested NoPartySubIDs(802)=1 group — mirrors the issue's live evidence
    // (453=2, 448=BROKER01, ..., 803=3). The boundary search must not stop
    // inside this group.
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

    // Find the replayed frame (carries PossDupFlag(43)) and assert its field order.
    // Identification uses count_43 >= 1 (not ==1) so a dedup regression that
    // duplicates 43 doesn't silently drop the frame from consideration — the
    // exactly-once requirement is asserted explicitly below instead.
    bool found_replayed = false;
    for (const auto& f : captured_frames) {
        const auto check = check_resend_answer_field_order(f);
        if (check.count_43 >= 1) {
            found_replayed = true;
            EXPECT_TRUE(check.ok) << check.reason;
            EXPECT_EQ(check.count_43, 1u) << "replayed frame must carry PossDupFlag(43) exactly once";
            EXPECT_EQ(check.count_122, 1u)
                << "replayed frame must carry OrigSendingTime(122) exactly once";
        }
    }
    ASSERT_TRUE(found_replayed)
        << "ResendRequest for the stored NewOrderSingle (seq=" << app_seq
        << ") must produce a replayed frame carrying PossDupFlag(43)";
}

}  // namespace fixpp::session::test
