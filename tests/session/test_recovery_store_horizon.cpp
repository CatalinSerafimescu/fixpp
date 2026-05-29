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
// GREEN witness (013 FR-010/FR-012 extension slice): the session walks the
//   per-session MessageStore (here a HorizonStore double modelling outbound
//   [7..12]), emits the pre-horizon SequenceReset-GapFill{NewSeqNo=7}, then
//   replays [7..12] with PossDupFlag(43)=Y + OrigSendingTime(122).

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <future>
#include <map>
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
#include <fixpp/session/direction.hpp>
#include <fixpp/session/message_store.hpp>
#include <fixpp/session/message_store_factory.hpp>
#include <fixpp/session/retrieve_visitor.hpp>
#include <fixpp/session/seqnum.hpp>
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

// ── HorizonStore: in-test MessageStore double modelling a purged store ─────────
// Outbound seqs [7..12] are retained as application messages (35=D); seqs
// [1..6] are below the store horizon (purged) and report store_seqnum_gap.
// next_seqnum(outbound) == 13 ⇒ our_last_outbound == 12. Inbound stores and
// the acceptor's own Logon-ack outbound store() are accepted as no-ops.
using fixpp::session::direction_t;
using fixpp::session::MessageStore;
using fixpp::session::MessageStoreFactory;
using fixpp::session::retrieve_visitor;
using fixpp::session::seqnum_t;
using fixpp::session::visit_result;

class HorizonStore final : public MessageStore {
public:
    HorizonStore() : MessageStore(flush_thunk_for<HorizonStore>()) {
        for (std::uint32_t k = kHorizon; k <= kLast; ++k) {
            frames_[k] = make_fix_frame("FIX.4.2", "D", k, "ISLD", "TW",
                                        field(11, "ORD" + std::to_string(k)));
        }
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>>
    store(seqnum_t /*seq*/, std::span<const std::byte> /*frame*/,
          direction_t /*dir*/) noexcept override {
        co_return fixpp::core::expected_t<void>{};  // accept (incl. Logon-ack)
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>>
    retrieve(seqnum_t begin, seqnum_t end, direction_t dir,
             retrieve_visitor& visitor) noexcept override {
        if (dir != direction_t::outbound) {
            co_return std::unexpected(fixpp::core::error::store_seqnum_gap);
        }
        const seqnum_t hi = (end == 0) ? kLast : end;
        bool visited_any = false;
        for (seqnum_t k = begin; k <= hi; ++k) {
            auto it = frames_.find(k);
            if (it == frames_.end()) {
                // Gap at k: stop (matches the store contract — gap → store_seqnum_gap).
                co_return visited_any ? fixpp::core::expected_t<void>{}
                                      : std::unexpected(fixpp::core::error::store_seqnum_gap);
            }
            visited_any = true;
            auto r = co_await visitor.on_frame(
                k, std::span<const std::byte>{it->second.data(), it->second.size()});
            if (!r) co_return std::unexpected(r.error());
            if (*r != visit_result::cont) break;
        }
        co_return fixpp::core::expected_t<void>{};
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<seqnum_t>>
    next_seqnum(direction_t dir, bool /*increment*/) noexcept override {
        co_return fixpp::core::expected_t<seqnum_t>{
            dir == direction_t::outbound ? (kLast + 1U) : seqnum_t{1}};
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>>
    reset() noexcept override {
        co_return fixpp::core::expected_t<void>{};
    }

private:
    static constexpr std::uint32_t kHorizon = 7;
    static constexpr std::uint32_t kLast    = 12;
    std::map<seqnum_t, std::vector<std::byte>> frames_;
};

class HorizonStoreFactory final : public MessageStoreFactory {
public:
    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<MessageStore>>
    make(std::string_view /*sender*/, std::string_view /*target*/,
         std::pmr::memory_resource* /*mr*/, std::size_t /*max_store_memory_bytes*/,
         asio::any_io_executor /*file_io_executor*/) noexcept override {
        return std::unique_ptr<MessageStore>(new HorizonStore());
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
        cfg.store_factory     = std::make_shared<HorizonStoreFactory>();
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

// ── RC#B (gate-b/r1): >1024-byte app message store double ───────────────────
// A NewOrderSingle with repeating NoPartyIDs group large enough to exceed
// the old 1024-byte CaptureVisitor buffer. The session MUST replay it with
// PossDupFlag(43)=Y + OrigSendingTime(122), NOT collapse it to a GapFill.
// [triage RC#B; spec.md FR-010/FR-012]

class LargeFrameStore final : public MessageStore {
public:
    explicit LargeFrameStore(std::vector<std::byte> big_frame, seqnum_t seq)
        : MessageStore(flush_thunk_for<LargeFrameStore>()),
          big_frame_(std::move(big_frame)), seq_(seq) {}

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>>
    store(seqnum_t /*seq*/, std::span<const std::byte> /*frame*/,
          direction_t /*dir*/) noexcept override {
        co_return fixpp::core::expected_t<void>{};
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>>
    retrieve(seqnum_t begin, seqnum_t end, direction_t dir,
             retrieve_visitor& visitor) noexcept override {
        if (dir != direction_t::outbound) {
            co_return std::unexpected(fixpp::core::error::store_seqnum_gap);
        }
        const seqnum_t hi = (end == 0) ? seq_ : end;
        bool visited_any = false;
        for (seqnum_t k = begin; k <= hi && k <= seq_; ++k) {
            if (k == seq_) {
                visited_any = true;
                auto r = co_await visitor.on_frame(
                    k, std::span<const std::byte>{big_frame_.data(), big_frame_.size()});
                if (!r) co_return std::unexpected(r.error());
                if (*r != visit_result::cont) break;
            }
        }
        if (!visited_any) co_return std::unexpected(fixpp::core::error::store_seqnum_gap);
        co_return fixpp::core::expected_t<void>{};
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<seqnum_t>>
    next_seqnum(direction_t dir, bool /*increment*/) noexcept override {
        co_return fixpp::core::expected_t<seqnum_t>{
            dir == direction_t::outbound ? (seq_ + 1U) : seqnum_t{1}};
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>>
    reset() noexcept override {
        co_return fixpp::core::expected_t<void>{};
    }

private:
    std::vector<std::byte> big_frame_;
    seqnum_t seq_;
};

class LargeFrameStoreFactory final : public MessageStoreFactory {
public:
    explicit LargeFrameStoreFactory(std::vector<std::byte> big_frame, seqnum_t seq)
        : big_frame_(std::move(big_frame)), seq_(seq) {}

    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<MessageStore>>
    make(std::string_view /*sender*/, std::string_view /*target*/,
         std::pmr::memory_resource* /*mr*/, std::size_t /*max_store_memory_bytes*/,
         asio::any_io_executor /*file_io_executor*/) noexcept override {
        return std::unique_ptr<MessageStore>(new LargeFrameStore(big_frame_, seq_));
    }

private:
    std::vector<std::byte> big_frame_;
    seqnum_t seq_;
};

// Build a FIX NewOrderSingle-like app frame with padding to exceed 1024B.
// Uses ClOrdID (tag 11) with a large filler string in the "extra" field.
static std::vector<std::byte> make_large_app_frame(seqnum_t seq) {
    // Build a "D" (NewOrderSingle) with a 1200-byte filler value in tag 58 (Text)
    // to push the total frame size well above 1024 bytes.
    std::string filler(1100, 'X');  // 1100-char padding in tag 58 (Text)
    std::string extra;
    extra += field(11, "ORDER-RC-B-1");
    extra += field(58, filler);  // tag 58 = Text
    return make_fix_frame("FIX.4.2", "D", seq, "ISLD", "TW", extra);
}

// ─────────────────────────────────────────────────────────────────────────────
// T016-D (RC#B gate-b/r1): Stored app message > 1024B replayed intact.
//
// Before fix: CaptureVisitor::buf=1024 → truncated=true → app_present=false
//   → slot collapsed to GapFill. Silent data loss.
// After fix: buffer enlarged → captured=true → replayed with 43=Y + 122=...
//
// RED (before fix): replay not found; gapfill found instead.
// GREEN (after fix): replay with 43=Y and 122= present.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(RecoveryStoreHorizonTest, LargeAppFrameReplayed_NotGapFilled) {
    capture.frames.clear();

    // Build a large frame at seq=7 (store horizon aligns with test).
    constexpr seqnum_t kLargeSeq = 7;
    auto big_frame = make_large_app_frame(kLargeSeq);
    ASSERT_GT(big_frame.size(), 1024u)
        << "Test prerequisite: large frame must exceed old 1024B capture limit.";

    auto cfg = make_acceptor_cfg();
    cfg.store_factory = std::make_shared<LargeFrameStoreFactory>(big_frame, kLargeSeq);
    fixpp::session::Session sess(engine, cfg);
    ASSERT_TRUE(drive_to_active(sess));

    // Peer asks to resend [7..7].
    auto rr = make_resend_request("FIX.4.2", 2, "TW", "ISLD", 7, 7);
    feed(sess, rr);

    // Find the replay frame (43=Y + seq=7).
    bool found_replay = false;
    bool found_gapfill = false;
    for (const auto& frame : capture.frames) {
        std::string wire(reinterpret_cast<const char*>(frame.data()), frame.size());
        if (is_replay_with_poss_dup(frame, kLargeSeq)) {
            found_replay = true;
        }
        if (wire.find("35=4\x01") != std::string::npos &&
            wire.find("123=Y\x01") != std::string::npos) {
            found_gapfill = true;
        }
    }

    EXPECT_TRUE(found_replay)
        << "Large app frame (>1024B) MUST be replayed with PossDupFlag(43)=Y. "
        << "RED (before fix): CaptureVisitor buf=1024 silently truncates → GapFill.";
    EXPECT_FALSE(found_gapfill)
        << "Large app frame (>1024B) must NOT be collapsed into a GapFill. "
        << "RED (before fix): truncated=true → treated as absent → GapFill emitted.";
}
