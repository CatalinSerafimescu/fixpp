// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_recovery_admin_span_gapfill.cpp — T015 [US1] Phase 3 RED
//
// Admin-span gap-fill: peer's ResendRequest covers a span [10..20] in which
// all stored messages are admin (Heartbeat/TestRequest). The session MUST
// collapse the entire span into ONE SequenceReset{NewSeqNo=21, GapFillFlag=Y}
// (FIX-SL §4.3.5: admin messages cannot be replayed).
//
// Anchors: spec.md §US1 AC1 / FR-010..FR-012 / FR-013; data-model.md §D-3;
//   plan.md §Test plan T015; [FIX-SL §4.3.5]; contracts/reconnect_fsm.hpp.
//
// Production-shape: drives bytes through Session::on_inbound_frame().
//
// RED witness: ReconnectFsm::reply_to_inbound_resend_request is a Phase-2 stub
//   returning {} without walking the store or emitting SequenceReset(4). The
//   EXPECT for a SequenceReset-GapFill outbound frame FAILS RED until T026 impl
//   lands.

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

using namespace std::chrono_literals;

namespace {

static std::string field(int tag, std::string_view val) {
    return std::to_string(tag) + "=" + std::string(val) + "\x01";
}

static std::vector<std::byte> make_fix_frame(std::string_view begin_string,
                                             std::string_view msg_type, std::uint32_t seq,
                                             std::string_view sender, std::string_view target,
                                             std::string_view extra = {}) {
    std::string body;
    body += field(35, msg_type);
    body += field(34, std::to_string(seq));
    body += field(49, sender);
    body += field(52, "20240101-00:00:00.000");
    body += field(56, target);
    if (!extra.empty()) body += std::string(extra);

    std::string msg;
    msg += "8=" + std::string(begin_string) + "\x01";
    msg += "9=" + std::to_string(body.size()) + "\x01";
    msg += body;
    unsigned int cs = 0;
    for (unsigned char c : msg) cs += c;
    cs &= 0xFFU;
    char csbuf[5];
    snprintf(csbuf, sizeof(csbuf), "%03u", cs);
    msg += "10=" + std::string(csbuf) + "\x01";

    std::vector<std::byte> frame;
    frame.reserve(msg.size());
    for (char c : msg) frame.push_back(static_cast<std::byte>(c));
    return frame;
}

static std::vector<std::byte> make_logon(std::string_view bs, std::uint32_t seq, std::string_view s,
                                         std::string_view t, int hbt = 30) {
    std::string extra;
    extra += field(98, "0");
    extra += field(108, std::to_string(hbt));
    return make_fix_frame(bs, "A", seq, s, t, extra);
}

// ResendRequest(2) with BeginSeqNo(7) and EndSeqNo(16).
static std::vector<std::byte> make_resend_request(std::string_view bs, std::uint32_t seq,
                                                  std::string_view s, std::string_view t,
                                                  std::uint32_t begin_seqno,
                                                  std::uint32_t end_seqno) {
    std::string extra;
    extra += field(7, std::to_string(begin_seqno));
    extra += field(16, std::to_string(end_seqno));
    return make_fix_frame(bs, "2", seq, s, t, extra);
}

struct OutboundCapture {
    std::vector<std::vector<std::byte>> frames;
    void operator()(std::span<const std::byte> data) {
        frames.emplace_back(data.begin(), data.end());
    }
};

static bool is_msg_type(std::span<const std::byte> frame, std::string_view type) {
    std::string wire(reinterpret_cast<const char*>(frame.data()), frame.size());
    std::string needle = "35=" + std::string(type) + "\x01";
    return wire.find(needle) != std::string::npos;
}

// SequenceReset (MsgType=4) with GapFillFlag(123)=Y and given NewSeqNo(36).
static bool is_sequence_reset_gapfill(std::span<const std::byte> frame,
                                      std::uint32_t expected_new_seqno) {
    if (!is_msg_type(frame, "4")) return false;
    std::string wire(reinterpret_cast<const char*>(frame.data()), frame.size());
    // GapFillFlag(123)=Y
    if (wire.find("123=Y\x01") == std::string::npos) return false;
    // NewSeqNo(36)=<expected_new_seqno>
    std::string ns = "36=" + std::to_string(expected_new_seqno) + "\x01";
    return wire.find(ns) != std::string::npos;
}

}  // namespace

// ── Test fixture ──────────────────────────────────────────────────────────────

class RecoveryAdminSpanGapfillTest : public ::testing::Test {
protected:
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine{};
    OutboundCapture capture;

    void SetUp() override {
        auto utc = std::chrono::system_clock::time_point{} + std::chrono::seconds{1704067200};
        auto stp = fixpp::core::steady_time_point{};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock = clock;
        engine.executor = ioc.get_executor();
    }

    fixpp::session::SessionConfig make_acceptor_cfg() {
        fixpp::session::SessionConfig cfg;
        cfg.sender_comp_id = "ISLD";
        cfg.target_comp_id = "TW";
        cfg.begin_string = "FIX.4.2";
        cfg.heartbeat_interval = 30s;
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override = ioc.get_executor();
        cfg.transport_send = [this](std::span<const std::byte> d) { capture(d); };
        cfg.role = fixpp::session::session_role::acceptor;
        // RC#C (gate-b/r1): bilateral_lenient — tests here don't exercise reset semantics.
        cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
        return cfg;
    }

    fixpp::core::expected_t<void> run_open(fixpp::session::Session& s) {
        auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
        ioc.run_for(100ms);
        ioc.restart();
        return fut.get();
    }

    fixpp::core::expected_t<void> feed(fixpp::session::Session& s,
                                       std::span<const std::byte> frame) {
        auto fut = asio::co_spawn(ioc, s.on_inbound_frame(frame), asio::use_future);
        ioc.run_for(100ms);
        ioc.restart();
        return fut.get();
    }

    // Drive to Active; after Logon(seq=1) next_expected = 2.
    bool drive_to_active(fixpp::session::Session& s) {
        auto r = run_open(s);
        if (!r.has_value()) return false;
        auto logon = make_logon("FIX.4.2", 1, "TW", "ISLD");
        feed(s, logon);
        return s.state() == fixpp::session::fsm_state::Active;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// FQ-2 (gate-b/r1): near-capacity GapFill overflow — fail-closed witness.
//
// With long CompIDs (S=T=80 chars) and nanosecond sending-time precision, the
// GapFill frame (≈ 281 bytes) overflows the 256-byte production buffer while the
// Logon (≈ 244 bytes) still fits — proving the Logon-fits ⟹ GapFill-fits
// assumption is FALSE.
//
// RED (before gate-b/r1): emit_gapfill_async's "if (!gf) { co_return true; }"
//   treats the build failure as success → feed() returns expected_t<void>{} (no
//   error), FSM stays Active. The EXPECT below for unexpected/Disconnected FAILS.
//
// GREEN (after gate-b/r1): "co_return false" → replay_outbound_range_ maps to
//   dispatch_aborted → on_inbound_frame transitions to Disconnected and returns
//   std::unexpected(dispatch_aborted). Both asserts pass.
//
// Anchor: build_logon fail-closed precedent (session.cpp:811-821); fail-closed
//   contract; "silent-loss real until disproven" project rule.
// ─────────────────────────────────────────────────────────────────────────────

class RecoveryGapfillNearCapacityTest : public ::testing::Test {
protected:
    // S = T = 80-char strings — chosen so the GapFill overflows the 256-byte
    // production buffer (GapFill ≈ 281 bytes with nanos) while the Logon fits
    // (Logon ≈ 244 bytes with nanos). Empirically verified: see FQ-2 RED proof.
    static constexpr std::size_t kIdLen = 80;
    const std::string kSender = std::string(kIdLen, 'A');
    const std::string kTarget = std::string(kIdLen, 'B');

    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine{};
    std::vector<std::vector<std::byte>> outbound_frames;

    void SetUp() override {
        auto utc = std::chrono::system_clock::time_point{} + std::chrono::seconds{1704067200};
        auto stp = fixpp::core::steady_time_point{};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock = clock;
        engine.executor = ioc.get_executor();
    }

    fixpp::session::SessionConfig make_cfg() {
        fixpp::session::SessionConfig cfg;
        cfg.sender_comp_id = kSender;
        cfg.target_comp_id = kTarget;
        cfg.begin_string = "FIX.4.2";
        cfg.heartbeat_interval = 30s;
        // Nanos precision: sending time is 27 chars; GapFill carries it TWICE
        // (52 + 122), making the overflow window reachable with S=T=80.
        cfg.sending_time_precision = fixpp::core::fix_time_precision::nanos;
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override = ioc.get_executor();
        cfg.transport_send = [this](std::span<const std::byte> d) {
            outbound_frames.emplace_back(d.begin(), d.end());
        };
        cfg.role = fixpp::session::session_role::acceptor;
        cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
        return cfg;
    }

    fixpp::core::expected_t<void> run_open(fixpp::session::Session& s) {
        auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
        ioc.run_for(100ms);
        ioc.restart();
        return fut.get();
    }

    fixpp::core::expected_t<void> feed(fixpp::session::Session& s,
                                       std::span<const std::byte> frame) {
        auto fut = asio::co_spawn(ioc, s.on_inbound_frame(frame), asio::use_future);
        ioc.run_for(100ms);
        ioc.restart();
        return fut.get();
    }
};

// FQ-2 cell: long-CompID GapFill overflow is fail-closed, never silent success.
//
// Discriminating witnesses:
//   (1) feed() returns unexpected — not expected_t<void>{} (silent success).
//   (2) FSM is Disconnected — not Active with an unfilled gap.
//
// The seed (Active state reached, then ResendRequest) is non-trivial: if the
// Logon also overflowed, the session would never reach Active; asserting
// drive_to_active() succeeds first proves the GapFill-only overflow.
TEST_F(RecoveryGapfillNearCapacityTest, LargeCompId_GapFillOverflow_IsFailClosed) {
    auto cfg = make_cfg();
    fixpp::session::Session sess(engine, cfg);

    // Step 1: open (acceptor waits for peer Logon in NotConnected).
    auto open_r = run_open(sess);
    ASSERT_TRUE(open_r.has_value()) << "open() must succeed";

    // Step 2: peer sends Logon with reversed CompIDs (sender=kTarget, target=kSender).
    // With S=T=80 and nanos, the acceptor's reply Logon ≈ 244 bytes < 256 → Active.
    auto peer_logon = make_logon("FIX.4.2", 1, kTarget, kSender);
    feed(sess, peer_logon);

    ASSERT_EQ(sess.state(), fixpp::session::fsm_state::Active)
        << "Precondition: session MUST reach Active (Logon ≈ 244 bytes fits 256-byte buffer). "
        << "If this ASSERT fails, reduce kIdLen so the Logon fits.";

    // Step 3: peer sends ResendRequest[2..5] — drives emit_gapfill_async with the
    // session's 256-byte GapFill buffer. With S=T=80 nanos, GapFill ≈ 281 bytes
    // overflows → build_sequence_reset_gapfill returns unexpected.
    auto rr = make_resend_request("FIX.4.2", 2, kTarget, kSender, 2, 5);
    auto rr_result = feed(sess, rr);

    // (1) feed() must return unexpected (visible failure, not silent success).
    // RED: before FQ-1, co_return true → replay succeeds → feed returns expected_t<void>{}.
    // GREEN: after FQ-1, co_return false → dispatch_aborted → feed returns unexpected.
    EXPECT_FALSE(rr_result.has_value())
        << "GapFill build failure with large CompIDs must return a visible error, "
        << "NOT silent success (dispatch_aborted expected from replay_outbound_range_). "
        << "RED: before gate-b/r1 fix, co_return true makes feed() return success here.";

    // (2) FSM must be Disconnected — never left falsely Active with unfilled gap.
    // RED: before FQ-1, the session stays Active (silent no-op).
    // GREEN: after FQ-1, on_inbound_frame transitions to Disconnected on dispatch_aborted.
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Disconnected)
        << "Session must be Disconnected after GapFill build failure. "
        << "RED: before gate-b/r1 fix, session stays Active with the gap unfilled.";
}

// ─────────────────────────────────────────────────────────────────────────────
// T015-A: Peer sends ResendRequest[2..12] (BeginSeqNo=2, EndSeqNo=12).
//   All messages in that range are admin (Heartbeat/TestRequest) so the session
//   MUST emit exactly ONE SequenceReset{GapFillFlag=Y, NewSeqNo=13}.
//
// RED: reply_to_inbound_resend_request stub returns {} without emitting
//   SequenceReset. EXPECT for SequenceReset(4)+GapFillFlag=Y FAILS RED.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(RecoveryAdminSpanGapfillTest, AllAdminSpanCollapsesToSingleGapFill) {
    auto cfg = make_acceptor_cfg();
    fixpp::session::Session sess(engine, cfg);
    ASSERT_TRUE(drive_to_active(sess)) << "Precondition: session must reach Active state";

    // Inject ResendRequest from peer (they want us to replay [2..12]).
    // Since our outbound store only has admin msgs in [2..12],
    // we must collapse to SequenceReset{NewSeqNo=13, GapFillFlag=Y}.
    auto rr = make_resend_request("FIX.4.2", 2, "TW", "ISLD", 2, 12);
    feed(sess, rr);

    // Check outbound for SequenceReset-GapFill with NewSeqNo=13.
    bool found_gapfill = false;
    for (const auto& frame : capture.frames) {
        if (is_sequence_reset_gapfill(frame, 13)) {
            found_gapfill = true;
            break;
        }
    }
    EXPECT_TRUE(found_gapfill)
        << "Expected ONE SequenceReset(4){GapFillFlag=Y, NewSeqNo=13} outbound "
        << "when peer asks ResendRequest[2..12] all-admin span. "
        << "RED: reply_to_inbound_resend_request stub returns {} without emitting "
        << "any SequenceReset frame — this assertion FAILS RED per T015 design.";
}

// ─────────────────────────────────────────────────────────────────────────────
// T015-B: EndSeqNo=0 means "replay through our last outbound seqnum".
//   Admin-only store again → one SequenceReset-GapFill covering the whole range.
//
// RED: same stub path — no SequenceReset emitted.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(RecoveryAdminSpanGapfillTest, EndSeqNoZeroMeansThrough) {
    auto cfg = make_acceptor_cfg();
    fixpp::session::Session sess(engine, cfg);
    ASSERT_TRUE(drive_to_active(sess));

    // Peer asks ResendRequest[2..0] meaning "all from 2 through current max".
    auto rr = make_resend_request("FIX.4.2", 2, "TW", "ISLD", 2, 0);
    feed(sess, rr);

    // Any SequenceReset-GapFill with GapFillFlag=Y is sufficient to pass.
    bool found_any_gapfill = false;
    for (const auto& frame : capture.frames) {
        if (is_msg_type(frame, "4")) {
            std::string wire(reinterpret_cast<const char*>(frame.data()), frame.size());
            if (wire.find("123=Y\x01") != std::string::npos) {
                found_any_gapfill = true;
                break;
            }
        }
    }
    EXPECT_TRUE(found_any_gapfill)
        << "Expected SequenceReset-GapFill for ResendRequest[2..0] (all-admin store). "
        << "RED: stub emits nothing — FAILS RED per T015 design.";
}

// ─────────────────────────────────────────────────────────────────────────────
// T015-C: A single admin message span emits exactly ONE GapFill (not multiple).
//   Verifies D-3: contiguous admin spans are collapsed, not per-message.
//
// RED: no SequenceReset emitted at all — count-based check also FAILS RED.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(RecoveryAdminSpanGapfillTest, SingleAdminSpanEmitsExactlyOneGapFill) {
    auto cfg = make_acceptor_cfg();
    fixpp::session::Session sess(engine, cfg);
    ASSERT_TRUE(drive_to_active(sess));

    auto rr = make_resend_request("FIX.4.2", 2, "TW", "ISLD", 2, 5);
    feed(sess, rr);

    std::size_t seq_reset_count = 0;
    for (const auto& frame : capture.frames) {
        if (is_msg_type(frame, "4")) ++seq_reset_count;
    }
    // At most 1 SequenceReset for a contiguous admin span per D-3.
    // RED: count == 0 (stub emits nothing); EXPECT_LE(0, 1) would pass but
    // the meaningful RED assertion is that we emit exactly the correct count.
    EXPECT_EQ(seq_reset_count, 1u)
        << "Admin span [2..5] must collapse to exactly 1 SequenceReset-GapFill. "
        << "RED: stub emits 0 → count=0 ≠ 1, FAILS RED per T015 design.";
}
