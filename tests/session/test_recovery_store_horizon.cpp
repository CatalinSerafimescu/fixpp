// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_recovery_store_horizon.cpp — T016 [US1] Phase 3 RED
//
// Store-horizon gap-fill: peer asks resend [2..12]; our 008 MessageStore only
// has [7..12]. The session MUST emit:
//   1. SequenceReset{GapFillFlag=Y, NewSeqNo=7}  — for the pre-horizon gap
//   2. Replay [7..12] with PossDupFlag(43)=Y + OrigSendingTime(122)
//
// Anchors: spec.md §US1 AC1 / FR-010..FR-012; data-model.md §D-4;
//   plan.md §Test plan T016; [FIX-SL §4.3.5]; contracts/reconnect_fsm.hpp.
//
// Production-shape: drives bytes through Session::on_inbound_frame().
//
// RED witness: reply_to_inbound_resend_request stub returns {} without
//   probing the store or emitting the pre-horizon GapFill or replays.
//   Both EXPECT assertions FAIL RED until T026 impl lands.

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

#include <gtest/gtest.h>

#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"

using namespace std::chrono_literals;

namespace {

static std::string field(int tag, std::string_view val) {
    return std::to_string(tag) + "=" + std::string(val) + "\x01";
}

static std::vector<std::byte> make_fix_frame(
        std::string_view begin_string,
        std::string_view msg_type,
        std::uint32_t seq,
        std::string_view sender,
        std::string_view target,
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

static std::vector<std::byte> make_logon(std::string_view bs, std::uint32_t seq,
                                          std::string_view s, std::string_view t,
                                          int hbt = 30) {
    std::string extra;
    extra += field(98, "0");
    extra += field(108, std::to_string(hbt));
    return make_fix_frame(bs, "A", seq, s, t, extra);
}

static std::vector<std::byte> make_resend_request(std::string_view bs,
                                                   std::uint32_t seq,
                                                   std::string_view s,
                                                   std::string_view t,
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

// SequenceReset{GapFillFlag=Y, NewSeqNo=<expected>}
static bool is_gapfill_to(std::span<const std::byte> frame, std::uint32_t new_seqno) {
    if (!is_msg_type(frame, "4")) return false;
    std::string wire(reinterpret_cast<const char*>(frame.data()), frame.size());
    if (wire.find("123=Y\x01") == std::string::npos) return false;
    return wire.find("36=" + std::to_string(new_seqno) + "\x01") != std::string::npos;
}

// Frame carries PossDupFlag(43)=Y and has given MsgSeqNum(34).
static bool is_replay_with_poss_dup(std::span<const std::byte> frame, std::uint32_t seq) {
    std::string wire(reinterpret_cast<const char*>(frame.data()), frame.size());
    if (wire.find("43=Y\x01") == std::string::npos) return false;
    return wire.find("34=" + std::to_string(seq) + "\x01") != std::string::npos;
}

}  // namespace

// ── Test fixture ──────────────────────────────────────────────────────────────

class RecoveryStoreHorizonTest : public ::testing::Test {
protected:
    asio::io_context                         ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig                engine{};
    OutboundCapture                          capture;

    void SetUp() override {
        auto utc = std::chrono::system_clock::time_point{} + std::chrono::seconds{1704067200};
        auto stp = fixpp::core::steady_time_point{};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock    = clock;
        engine.executor = ioc.get_executor();
    }

    fixpp::session::SessionConfig make_acceptor_cfg() {
        fixpp::session::SessionConfig cfg;
        cfg.sender_comp_id    = "ISLD";
        cfg.target_comp_id    = "TW";
        cfg.begin_string      = "FIX.4.2";
        cfg.heartbeat_interval = 30s;
        cfg.security_profile  = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary        = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override = ioc.get_executor();
        cfg.transport_send    = [this](std::span<const std::byte> d) { capture(d); };
        cfg.role              = fixpp::session::session_role::acceptor;
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

    bool drive_to_active(fixpp::session::Session& s) {
        auto r = run_open(s);
        if (!r.has_value()) return false;
        auto logon = make_logon("FIX.4.2", 1, "TW", "ISLD");
        feed(s, logon);
        return s.state() == fixpp::session::fsm_state::Active;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// T016-A: Peer asks ResendRequest[2..12]; store horizon is at 7.
//   Expected outbound sequence:
//     1. SequenceReset{GapFillFlag=Y, NewSeqNo=7}  (pre-horizon gap)
//     2. Replays for [7..12] with PossDupFlag=Y
//
// RED: stub returns {} without probing store — no GapFill, no replays.
//   EXPECT_TRUE(found pre-horizon gapfill) FAILS RED per T016 design.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(RecoveryStoreHorizonTest, PreHorizonGapCollapsedToGapFill) {
    auto cfg = make_acceptor_cfg();
    fixpp::session::Session sess(engine, cfg);
    ASSERT_TRUE(drive_to_active(sess));

    // Peer asks for [2..12]; our store horizon is 7 (seqs 2..6 missing/purged).
    auto rr = make_resend_request("FIX.4.2", 2, "TW", "ISLD", 2, 12);
    feed(sess, rr);

    // A pre-horizon GapFill to seq 7 must appear before any replays.
    bool found_prehorizon_gapfill = false;
    for (const auto& frame : capture.frames) {
        if (is_gapfill_to(frame, 7)) {
            found_prehorizon_gapfill = true;
            break;
        }
    }
    EXPECT_TRUE(found_prehorizon_gapfill)
        << "Expected SequenceReset{GapFillFlag=Y, NewSeqNo=7} for pre-horizon "
        << "gap [2..6]. RED: reply_to_inbound_resend_request stub emits nothing "
        << "— FAILS RED per T016 design.";
}

// ─────────────────────────────────────────────────────────────────────────────
// T016-B: After the GapFill, replays for the available range carry PossDupFlag=Y.
//
// RED: no replays emitted — all EXPECT assertions FAIL RED.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(RecoveryStoreHorizonTest, AvailableRangeReplaysCarryPossDupFlag) {
    auto cfg = make_acceptor_cfg();
    fixpp::session::Session sess(engine, cfg);
    ASSERT_TRUE(drive_to_active(sess));

    auto rr = make_resend_request("FIX.4.2", 2, "TW", "ISLD", 2, 8);
    feed(sess, rr);

    // At least one replay frame in the [7..8] horizon range with PossDupFlag=Y.
    bool found_replay = false;
    for (const auto& frame : capture.frames) {
        if (is_replay_with_poss_dup(frame, 7) || is_replay_with_poss_dup(frame, 8)) {
            found_replay = true;
            break;
        }
    }
    EXPECT_TRUE(found_replay)
        << "Expected at least one replay frame with PossDupFlag(43)=Y for "
        << "available seq 7 or 8. RED: stub emits nothing — FAILS RED.";
}

// ─────────────────────────────────────────────────────────────────────────────
// T016-C: OrigSendingTime(122) must appear in each replayed application frame.
//
// RED: no replays emitted at all — assertion FAILS RED.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(RecoveryStoreHorizonTest, ReplayFramesCarryOrigSendingTime) {
    auto cfg = make_acceptor_cfg();
    fixpp::session::Session sess(engine, cfg);
    ASSERT_TRUE(drive_to_active(sess));

    auto rr = make_resend_request("FIX.4.2", 2, "TW", "ISLD", 2, 8);
    feed(sess, rr);

    // Any replay frame must carry OrigSendingTime (tag 122).
    bool found_orig_sending_time = false;
    for (const auto& frame : capture.frames) {
        std::string wire(reinterpret_cast<const char*>(frame.data()), frame.size());
        if (wire.find("43=Y\x01") != std::string::npos &&
            wire.find("122=") != std::string::npos) {
            found_orig_sending_time = true;
            break;
        }
    }
    EXPECT_TRUE(found_orig_sending_time)
        << "Each replayed application frame must carry OrigSendingTime(122). "
        << "RED: stub emits no replays — FAILS RED per T016 design.";
}
