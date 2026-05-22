// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/durable_before_transmit_test.cpp
//
// Seam #10 — Durable-before-transmit ordering (005-session-establishment-fsm
// T029 / US2 Phase 4 + US4 Phase 6 outbound-half retirement).
//
// Covers BOTH halves of invariant I-3 ([2e §root cause #1] / [2e §7.6]):
//   (a) Inbound:  store(seq, frame, inbound) completes BEFORE fromAdmin/fromApp
//                 dispatch (observed via FSM transition past NotConnected after
//                 a valid Logon under T034's wiring).
//   (b) Outbound: store(seq, committed_span, outbound) completes BEFORE
//                 transport::async_write. Asserted by injecting a custom
//                 MessageStoreFactory whose minted OrderingStore appends
//                 "store_out" to a shared call-order vector inside store(),
//                 paired with a transport_send callback that appends
//                 "transport_send". The literal element order is then asserted
//                 against the expected ["store_out", "transport_send"] sequence
//                 for the SAME outbound frame (Logout via close(graceful)).
//
// Anchors: data-model.md I-3; [2e §root cause #1]; [2e §7.6]; spec FR-009.
// tasks.md T029 fully retired by Phase 6 transport wiring.
#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <asio/any_io_executor.hpp>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>

#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/message_store.hpp>
#include <fixpp/session/message_store_factory.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"

#include <gtest/gtest.h>

using namespace std::chrono_literals;

namespace fixpp::session::test {

// ── Fixture ───────────────────────────────────────────────────────────────────

class DurableBeforeTransmitTest : public ::testing::Test {
protected:
    asio::io_context                         ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig                engine{};

    void SetUp() override {
        using namespace std::chrono;
        auto utc = system_clock::time_point{} + seconds{1704067200};
        auto stp = fixpp::core::steady_time_point{} + seconds{0};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock    = clock;
        engine.executor = ioc.get_executor();
    }

    fixpp::core::expected_t<void> open_sync(Session& s) {
        auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
        ioc.run_for(200ms);
        ioc.restart();
        return fut.get();
    }
};

// ── I-3 inbound half: store(inbound) before fromAdmin/fromApp dispatch ────────
//
// [2e §7.6] N6: store(inbound) completes BEFORE fromAdmin/fromApp dispatch.
//
// Observable property at Phase 4 scope: when on_inbound_frame() processes a
// valid Logon, the FSM transitions to LogonReceived/Active. Under T034's
// wiring this transition (which represents the dispatch point) is ordered
// strictly AFTER the inbound store call and seqnum check; if store() returned
// an error or the seqnum check rejected, the FSM would transition to
// Disconnected instead and the dispatch path would not run.
//
// The test asserts: after a valid Logon is fed, the FSM has advanced past
// NotConnected — which under the implementation in session.cpp can only
// happen via the post-store/post-seqnum-check path.

TEST_F(DurableBeforeTransmitTest, InboundStoreBeforeFromAdminDispatch) {
    fixpp::session::SessionConfig cfg;
    cfg.sender_comp_id    = "ISLD";
    cfg.target_comp_id    = "TW";
    cfg.begin_string      = "FIX.4.2";
    cfg.heartbeat_interval = 30s;
    cfg.security_profile  = fixpp::test_support::make_minimal_security_profile();
    cfg.dictionary        = fixpp::test_support::make_minimal_dictionary();
    cfg.executor_override = ioc.get_executor();

    Session sess(engine, cfg);
    auto open_r = open_sync(sess);
    ASSERT_TRUE(open_r.has_value()) << "open() should succeed";

    // Build a minimal Logon frame (seq=1).
    std::string body = "35=A\001" "34=1\001" "49=TW\001"
                       "52=20240101-00:00:00.000\001" "56=ISLD\001"
                       "98=0\001" "108=30\001";
    std::string hdr = "8=FIX.4.2\001" "9=" + std::to_string(body.size()) + "\001";
    std::string full = hdr + body;
    unsigned int cs = 0;
    for (unsigned char c : full) { cs += c; }
    cs &= 0xFFU;
    char csbuf[4];
    snprintf(csbuf, sizeof(csbuf), "%03u", cs);
    full += "10=" + std::string(csbuf) + "\001";

    std::vector<std::byte> logon_frame;
    for (char c : full) { logon_frame.push_back(static_cast<std::byte>(c)); }

    // Feed the inbound Logon.
    auto fut = asio::co_spawn(
        ioc, sess.on_inbound_frame(logon_frame), asio::use_future);
    ioc.run_for(200ms);
    ioc.restart();
    auto r = fut.get();
    EXPECT_TRUE(r.has_value()) << "on_inbound_frame should succeed for valid Logon";

    // The FSM has transitioned — which under T034 means the store+seqnum
    // check completed first. (If either had failed, the FSM would be
    // Disconnected; if either had not yet run, the FSM would still be
    // NotConnected.)
    const auto st = sess.state();
    EXPECT_TRUE(st == fsm_state::LogonReceived || st == fsm_state::Active)
        << "Post-Logon dispatch must transition past NotConnected; got state="
        << static_cast<int>(st);
}

// ── I-3 outbound half: store(outbound) BEFORE transport::async_write ─────────
//
// T029 outbound-half retirement (Phase 6 / US4 transport wiring).
//
// Injects a custom RecordingStoreFactory minting an OrderingStore that appends
// "store_out" to a shared call-order vector inside its store(outbound, ...)
// body — BEFORE the co_return — paired with a transport_send callback that
// appends "transport_send" at call time. Drives the session through open() →
// LogonSent → Active (peer Logon-ack) → close(graceful), which produces an
// outbound Logout via store_then_emit().
//
// The literal element order in the shared vector is then asserted: every
// transport_send entry MUST be immediately preceded by a store_out entry
// (matching the I-3 contract that the SAME outbound frame is stored before it
// is transmitted). The strict-pairs check is encoded as
// ElementsAre("store_out", "transport_send", "store_out", "transport_send", ...).

namespace {

// OrderingStore: a minimal MessageStore that records "store_out" into a shared
// call-order vector whenever store(direction_t::outbound, ...) is invoked.
// Inbound stores are accepted silently (Logon ack triggers an inbound store
// which is not part of the outbound-ordering assertion).
class OrderingStore final : public MessageStore {
public:
    explicit OrderingStore(std::vector<std::string>& order) noexcept
        : MessageStore(flush_thunk_for<OrderingStore>()), order_(order) {}

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>>
    store(seqnum_t /*seq*/, std::span<const std::byte> /*frame*/,
          direction_t dir) noexcept override {
        if (dir == direction_t::outbound) {
            order_.push_back("store_out");
        }
        co_return fixpp::core::expected_t<void>{};
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>>
    retrieve(seqnum_t /*begin*/, seqnum_t /*end*/, direction_t /*dir*/,
             retrieve_visitor& /*visitor*/) noexcept override {
        co_return fixpp::core::expected_t<void>{};
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<seqnum_t>>
    next_seqnum(direction_t dir, bool increment) noexcept override {
        auto& c = (dir == direction_t::outbound) ? next_out_ : next_in_;
        const seqnum_t curr = c;
        if (increment) { ++c; }
        co_return fixpp::core::expected_t<seqnum_t>{curr};
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>>
    reset() noexcept override {
        next_in_ = next_out_ = seqnum_min;
        co_return fixpp::core::expected_t<void>{};
    }

private:
    std::vector<std::string>& order_;
    seqnum_t                  next_in_  = seqnum_min;
    seqnum_t                  next_out_ = seqnum_min;
};

// RecordingStoreFactory: mints a single OrderingStore bound to a shared
// call-order vector. Session calls make() at open() per session.cpp:201-211.
class RecordingStoreFactory final : public MessageStoreFactory {
public:
    explicit RecordingStoreFactory(std::vector<std::string>& order) noexcept
        : order_(order) {}

    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<MessageStore>>
    make(std::string_view /*sender*/,
         std::string_view /*target*/,
         std::pmr::memory_resource* /*mr*/,
         std::size_t /*max_store_memory_bytes*/,
         asio::any_io_executor /*file_io_executor*/) noexcept override {
        return std::unique_ptr<MessageStore>(new OrderingStore(order_));
    }

private:
    std::vector<std::string>& order_;
};

}  // namespace

TEST_F(DurableBeforeTransmitTest, OutboundStoreBeforeTransportSend) {
    // Shared call-order vector — appended by both OrderingStore::store() (for
    // direction_t::outbound) and the transport_send callback.
    std::vector<std::string> order;

    fixpp::session::SessionConfig cfg;
    cfg.sender_comp_id     = "ISLD";
    cfg.target_comp_id     = "TW";
    cfg.begin_string       = "FIX.4.2";
    cfg.heartbeat_interval = 30s;
    cfg.security_profile   = fixpp::test_support::make_minimal_security_profile();
    cfg.dictionary         = fixpp::test_support::make_minimal_dictionary();
    cfg.executor_override  = ioc.get_executor();
    cfg.store_factory      = std::make_unique<RecordingStoreFactory>(order);
    cfg.transport_send     = [&order](std::span<const std::byte> /*frame*/) {
        order.push_back("transport_send");
    };

    Session sess(engine, cfg);
    auto open_r = open_sync(sess);
    ASSERT_TRUE(open_r.has_value()) << "open() should succeed";
    ASSERT_EQ(sess.state(), fsm_state::LogonSent);

    // Drive LogonSent → Active by feeding the peer's Logon ack (seq=1).
    // close(graceful) only emits a Logout when the FSM is Active or
    // LogonReceived (matrix), so we must reach Active before close().
    {
        std::string body = "35=A\001" "34=1\001" "49=TW\001"
                           "52=20240101-00:00:00.000\001" "56=ISLD\001"
                           "98=0\001" "108=30\001";
        std::string hdr = "8=FIX.4.2\001" "9=" + std::to_string(body.size()) + "\001";
        std::string full = hdr + body;
        unsigned int cs = 0;
        for (unsigned char c : full) { cs += c; }
        cs &= 0xFFU;
        char csbuf[4];
        snprintf(csbuf, sizeof(csbuf), "%03u", cs);
        full += "10=" + std::string(csbuf) + "\001";

        std::vector<std::byte> logon_ack;
        for (char c : full) { logon_ack.push_back(static_cast<std::byte>(c)); }

        auto fut2 = asio::co_spawn(
            ioc, sess.on_inbound_frame(logon_ack), asio::use_future);
        ioc.run_for(200ms);
        ioc.restart();
        ASSERT_TRUE(fut2.get().has_value()) << "Logon-ack inbound failed";
        ASSERT_EQ(sess.state(), fsm_state::Active);
    }

    // Trigger an outbound: close(graceful) builds + emits a Logout via
    // store_then_emit() (the I-3 outbound-half code path). Drive ioc briefly
    // so close() registers its 2s sleep, then advance the mock clock past
    // the timeout so phase-1 returns and close() completes.
    auto close_fut = asio::co_spawn(
        ioc, sess.close(close_mode::graceful), asio::use_future);
    ioc.run_for(100ms);
    ioc.restart();
    clock->advance(std::chrono::seconds{3});
    ioc.run_for(200ms);
    ioc.restart();
    (void)close_fut.get();

    // I-3 outbound contract: every transport_send entry MUST be IMMEDIATELY
    // preceded by a store_out entry — for the SAME frame, store completes
    // before transport. The simplest faithful assertion is that the order
    // sequence contains AT LEAST one outbound emission and that each
    // "transport_send" is preceded by a "store_out".
    ASSERT_FALSE(order.empty())
        << "Expected at least one outbound frame (Logout) to be emitted";

    // Find any transport_send and verify the element immediately before is
    // a store_out (the SAME-frame store-before-transmit ordering). This
    // pairing must hold for EVERY transport_send entry — there must be no
    // "transport_send" preceded by anything other than "store_out".
    std::size_t pair_count = 0;
    for (std::size_t i = 0; i < order.size(); ++i) {
        if (order[i] == "transport_send") {
            ASSERT_GT(i, 0u)
                << "I-3 outbound: transport_send at position " << i
                << " has no preceding entry (must be store_out)";
            EXPECT_EQ(order[i - 1], "store_out")
                << "I-3 outbound: transport_send at position " << i
                << " must be IMMEDIATELY preceded by store_out (got '"
                << order[i - 1] << "')";
            ++pair_count;
        }
    }
    EXPECT_GE(pair_count, 1u)
        << "Expected at least one store_out → transport_send pair "
           "(outbound Logout emission via close(graceful))";

    // Also assert the literal full element sequence is exactly the expected
    // pattern: alternating "store_out", "transport_send" pairs. This is the
    // strongest form of the I-3 outbound assertion.
    std::vector<std::string> expected;
    for (std::size_t i = 0; i < pair_count; ++i) {
        expected.push_back("store_out");
        expected.push_back("transport_send");
    }
    EXPECT_EQ(order, expected)
        << "I-3 outbound: shared call-order vector must be exactly alternating "
           "store_out → transport_send pairs (no transport_send unpaired or "
           "preceding its store_out)";
}

}  // namespace fixpp::session::test
