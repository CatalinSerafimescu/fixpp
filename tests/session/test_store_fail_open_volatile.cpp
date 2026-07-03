// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_store_fail_open_volatile.cpp
//
// 059-outbound-store-fail-closed — T009 [US2] regression pin.
//
// W2 (quickstart.md) — a bounded MemoryStore (volatile) filled to its
// configured outbound capacity: the NEXT store() call returns
// store_capacity_exhausted for real (no mock/seam needed). Asserts the
// volatile-store leg is byte-for-byte unchanged from `main`: T006/T007's
// persistent-only fail-closed disposition must NOT catch this arm — the
// session stays Active and the send still succeeds (logged-then-proceed,
// FR-003 / SC-003; the documented L-008-2 limitation stands).
//
// Harness: asio::thread_pool{2} + a single per-session strand (mirrors
// test_store_fail_closed_persistent.cpp) — the store path is
// strand-confined and async_mutex-guarded; a single-threaded harness would
// mask races (feedback_single_threaded_harness_masks_strand_races).
//
// Anchors: specs/059-outbound-store-fail-closed/{spec.md FR-003, SC-003;
// quickstart.md W2}.
#include <gtest/gtest.h>

#include <asio/any_io_executor.hpp>
#include <asio/co_spawn.hpp>
#include <asio/strand.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/memory_store.hpp>
#include <fixpp/session/memory_store_factory.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"

using namespace std::chrono_literals;

namespace {

using fixpp::session::fsm_state;
using fixpp::session::MemoryStore;
using fixpp::session::MemoryStoreFactory;
using fixpp::session::Session;
using fixpp::session::session_role;

// ── Frame-building helpers (mirror test_store_fail_closed_persistent.cpp) ────

std::string field(int tag, std::string_view val) {
    return std::to_string(tag) + "=" + std::string(val) + "\x01";
}

std::vector<std::byte> make_fix_frame(std::string_view begin_string, std::string_view msg_type,
                                      std::uint32_t seq, std::string_view sender,
                                      std::string_view target, std::string_view extra = {}) {
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

std::vector<std::byte> make_logon(std::string_view bs, std::uint32_t seq, std::string_view s,
                                  std::string_view t, int hbt = 30) {
    std::string extra;
    extra += field(98, "0");
    extra += field(108, std::to_string(hbt));
    return make_fix_frame(bs, "A", seq, s, t, extra);
}

// Minimal app payload (35=D). Session::send builds the full wire frame
// (header + MsgSeqNum) around this opaque body (mirrors
// test_store_fail_closed_persistent.cpp's make_app_payload).
std::vector<std::byte> make_app_payload(std::string_view clordid) {
    std::string body = "35=D\x01" + std::string(field(11, clordid)) + "54=1\x01"
                                                                       "55=AAPL\x01";
    std::vector<std::byte> v;
    v.reserve(body.size());
    for (char c : body) v.push_back(static_cast<std::byte>(c));
    return v;
}

// ── Fixture ───────────────────────────────────────────────────────────────────

class StoreFailOpenVolatileTest : public ::testing::Test {
protected:
    void SetUp() override {
        pool_ = std::make_unique<asio::thread_pool>(2);
        sx_ = asio::make_strand(pool_->get_executor());

        auto utc = std::chrono::system_clock::time_point{} + std::chrono::seconds{1704067200};
        auto stp = fixpp::core::steady_time_point{};
        clock_ = std::make_shared<fixpp::core::mock_clock>(utc, stp, sx_);
        engine_.executor = sx_;
        engine_.clock = clock_;
        engine_.max_store_memory_per_session = 1ULL << 20;  // comfortably above the product
    }

    void TearDown() override {
        if (pool_) {
            pool_->stop();
            pool_->join();
        }
    }

    // Bounded outbound_capacity == kOutboundCapacity: exactly enough slots for
    // the initiator Logon (outbound seq 1) + the baseline app send (outbound
    // seq 2). The NEXT store() call (outbound seq 3) hits real capacity
    // exhaustion — no mock/seam needed.
    static constexpr std::size_t kOutboundCapacity = 2;

    fixpp::session::SessionConfig make_initiator_cfg() {
        MemoryStore::Config mcfg;
        mcfg.policy = fixpp::session::capacity_policy::bounded;
        mcfg.inbound_capacity = 10;
        mcfg.outbound_capacity = kOutboundCapacity;
        mcfg.max_frame_bytes = 4096;

        fixpp::session::SessionConfig cfg;
        cfg.role = session_role::initiator;
        cfg.sender_comp_id = "INITR";
        cfg.target_comp_id = "ACCEPTR";
        cfg.begin_string = "FIX.4.2";
        cfg.heartbeat_interval = 0s;  // disable liveness loop noise
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override = sx_;
        // bilateral_lenient (mirrors test_store_fail_closed_persistent.cpp): this
        // witness exercises W2's volatile-store capacity exhaustion, not the
        // reset-policy variants (those are US3's T011).
        cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
        cfg.store_factory = std::make_shared<MemoryStoreFactory>(mcfg);
        return cfg;
    }

    std::unique_ptr<asio::thread_pool> pool_;
    asio::any_io_executor sx_;
    std::shared_ptr<fixpp::core::mock_clock> clock_;
    fixpp::core::EngineConfig engine_{};
};

// ─────────────────────────────────────────────────────────────────────────────
// W2 — volatile-store capacity exhaustion does NOT disconnect: byte-identical
// to `main` (FR-003 / SC-003; L-008-2 stands). Regression pin guarding that
// T006's persistent-store fail-closed disposition did not widen to catch the
// volatile arm.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(StoreFailOpenVolatileTest, VolatileCapacityExhaustion_DoesNotDisconnect_TransmitProceeds) {
    std::vector<std::vector<std::byte>> wire;
    auto cfg = make_initiator_cfg();
    cfg.transport_send = [&](std::span<const std::byte> f) { wire.emplace_back(f.begin(), f.end()); };

    auto sess = std::make_unique<Session>(engine_, cfg);

    // open() emits the initiator Logon (outbound seq 1, stored — 1/2 capacity).
    auto open_r = asio::co_spawn(sx_, sess->open(), asio::use_future).get();
    ASSERT_TRUE(open_r.has_value()) << "open() must succeed";
    ASSERT_EQ(sess->state(), fsm_state::LogonSent);

    // Feed the peer's Logon-ack (inbound seq 1) — reaches Active.
    auto peer_logon = make_logon("FIX.4.2", 1, "ACCEPTR", "INITR");
    auto logon_r =
        asio::co_spawn(sx_, sess->on_inbound_frame(std::span<const std::byte>(peer_logon)),
                       asio::use_future)
            .get();
    ASSERT_TRUE(logon_r.has_value()) << "peer Logon-ack must be accepted";
    ASSERT_EQ(sess->state(), fsm_state::Active);

    // Baseline app send (outbound seq 2, stored — 2/2 capacity: the bounded
    // outbound store is now exactly full).
    auto payload_pre = make_app_payload("ORD-PRE");
    auto pre_r =
        asio::co_spawn(sx_, sess->send(std::span<const std::byte>(payload_pre)), asio::use_future)
            .get();
    ASSERT_TRUE(pre_r.has_value()) << "the baseline send must succeed";
    ASSERT_EQ(sess->state(), fsm_state::Active);
    const std::size_t wire_before = wire.size();

    // Next app send (outbound seq 3): the volatile MemoryStore's store() hits
    // real capacity exhaustion (entries.size() == outbound_capacity). Per
    // FR-003 / L-008-2 this is logged-then-proceed on a volatile store — NOT
    // a fail-closed disconnect (unlike the persistent-store disposition
    // proven in test_store_fail_closed_persistent.cpp).
    auto payload_over = make_app_payload("ORD-OVER");
    auto over_r =
        asio::co_spawn(sx_, sess->send(std::span<const std::byte>(payload_over)), asio::use_future)
            .get();

    EXPECT_TRUE(over_r.has_value())
        << "a volatile-store capacity-exhaustion retain failure must NOT fail closed "
           "(FR-003); send() must still succeed";
    EXPECT_EQ(sess->state(), fsm_state::Active)
        << "the session must remain Active after a volatile-store retain failure "
           "(FR-003 / L-008-2 — the documented limitation stands, no new disconnect)";
    EXPECT_EQ(wire.size(), wire_before + 1)
        << "the un-retained frame must still be transmitted on the volatile-store path "
           "(logged-then-proceed, byte-identical to main)";

    asio::co_spawn(sx_, sess->close(fixpp::session::close_mode::terminal), asio::use_future).get();
}

}  // namespace
