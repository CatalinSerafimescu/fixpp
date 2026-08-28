// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/capi/error_live_test.cpp — 050 Feature B (CA-006), Q3+Q4 Gate-B live witnesses.
//
// gate-b/r1 additions: discriminating LIVE C-ABI send tests that prove the
// oracle mappings in error_block_test.cpp are exercised by the real send path,
// not just tested via translate() directly. The oracle is NOT duplicated here.
//
// Q3 (finding #4): two live send-path negative witnesses:
//   (a) malformed app frame (embedded session tag 34= at a field boundary) →
//       assert FIXPP_ERR_UNKNOWN (app_payload_malformed→UNKNOWN per L-050-4) AND
//       peer receive-callback count UNCHANGED (the "no transmit" leg).
//   (b) outbound seqnum pre-seeded to seqnum_max (FIXPP_TEST_HOOKS seam) →
//       assert FIXPP_ERR_STORE_RUNTIME AND peer callback count UNCHANGED.
//
// Q4 (finding #5): live exported-call downgrade witness:
//   engine A consumer_minor=0 + no realtime clock → fixpp_engine_start → assert
//   FIXPP_ERR_UNKNOWN (clock_not_set downgraded for minor=0);
//   engine B consumer_minor=2 + no realtime clock → fixpp_engine_start → assert
//   FIXPP_ERR_THREAD_CONFIG (real code, not downgraded).
//
// Anchors: spec.md FR-005/FR-006/FR-017; data-model E-4/E-6; tasks.md T022/T026;
//          contracts/send-and-receive.md; error_block_test.cpp oracle;
//          L-050-4 (session/app block deferred → app_payload_malformed stays UNKNOWN).
//
// NOTE: FIXPP_TEST_HOOKS is required here for seqnum_mgr_test_access() seam (Q3b).
// The Q4 downgrade test does NOT need TEST_HOOKS; it is included here for proximity
// to the send-path negative witnesses (all are "live send-path" witnesses).

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

#include "fix/c_api/engine.h"
#include "fix/c_api/session.h"

#include <asio/co_spawn.hpp>
#include <asio/use_future.hpp>

#include "capi_internal.hpp"  // fixpp_engine internals (handle cast seam)

#include "fixpp/session/seqnum.hpp"          // seqnum_max
#include "fixpp/session/seqnum_manager.hpp"  // set_counters_for_test (FIXPP_TEST_HOOKS)
#include "fixpp/session/session.hpp"         // Session::seqnum_mgr_test_access()

#include "capi_loopback_support.hpp"
#include "support/wait_until.hpp"

using namespace std::chrono_literals;
using namespace fixpp::capi_test;

// Helper: count callbacks received by a session (atomic, thread-safe).
struct CallbackCounter {
    std::atomic<int> count{0};
    static void on_msg(const fixpp_msg_t*, void* ud) {
        static_cast<CallbackCounter*>(ud)->count.fetch_add(1, std::memory_order_relaxed);
    }
};

// ── Q3(a): malformed app payload (session tag at field boundary) → UNKNOWN + no transmit
//
// A payload that embeds "34=" at a tag field boundary triggers app_payload_malformed
// (error 131) in Session::send_impl → FIXPP_ERR_UNKNOWN (L-050-4 deferred).
// The peer receive-callback must NOT fire (no transmit on malformed input).
TEST(CapiErrorLive, MalformedPayloadReturnsUnknownNoTransmit) {
    fixpp_engine_t* B = nullptr;
    fixpp_engine_t* A = nullptr;
    ASSERT_EQ(fixpp_engine_create(make_engine_cfg(), 1, 0, &B), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_engine_create(make_engine_cfg(), 1, 0, &A), FIXPP_ERR_OK);

    CallbackCounter b_counter;

    fixpp_session_config_t* acc =
        make_session_cfg("ACC-MALF", "INIT-MALF", FIXPP_ROLE_ACCEPTOR);
    set_loopback_endpoint(acc, "127.0.0.1", 0);
    auto acc_id = session_id_of(acc);
    fixpp_session_t* acc_h = nullptr;
    ASSERT_EQ(fixpp_session_open(B, acc, &acc_h), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_session_register_callback(acc_h, CallbackCounter::on_msg, &b_counter),
              FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_engine_start(B), FIXPP_ERR_OK);

    std::uint16_t port = wait_for_bound_port(B, acc_id);
    ASSERT_NE(port, 0u) << "acceptor did not bind";

    fixpp_session_config_t* ini =
        make_session_cfg("INIT-MALF", "ACC-MALF", FIXPP_ROLE_INITIATOR);
    set_loopback_endpoint(ini, "127.0.0.1", port);
    fixpp_session_t* ini_h = nullptr;
    ASSERT_EQ(fixpp_session_open(A, ini, &ini_h), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_engine_start(A), FIXPP_ERR_OK);
    ASSERT_TRUE(wait_for_established(ini_h)) << "initiator never established";

    // A well-formed send first (sanity: the channel is live; b_counter should tick).
    const auto good_payload = make_app_payload("GOOD");
    EXPECT_EQ(fixpp_session_send(ini_h, good_payload.data(), good_payload.size()), FIXPP_ERR_OK);

    // Wait for the good send to arrive (peer callback fires) — give it up to 3s.
    // Result discarded: the ASSERT_GE below is the oracle.
    (void)fixpp::test_support::wait_until_observed(
        [&b_counter] { return b_counter.count.load(std::memory_order_relaxed) != 0; }, 3s);
    ASSERT_GE(b_counter.count.load(), 1) << "good send never delivered to peer callback";
    const int count_before_malformed = b_counter.count.load(std::memory_order_acquire);

    // A malformed payload: embeds "34=" at a tag-field boundary.
    // Session::send_impl scans the app payload for session header/trailer tags
    // (8/9/34/49/52/56/10) at SOH boundaries and returns app_payload_malformed if found.
    // app_payload_malformed (131) → translate() → FIXPP_ERR_UNKNOWN (L-050-4 deferred).
    std::string malformed_str = "35=D\x01" "34=INJECTED\x01" "55=TESTSYM\x01";
    std::vector<std::uint8_t> malformed_payload(malformed_str.begin(), malformed_str.end());

    const fixpp_error_t send_rc =
        fixpp_session_send(ini_h, malformed_payload.data(), malformed_payload.size());
    EXPECT_EQ(send_rc, FIXPP_ERR_UNKNOWN)
        << "malformed payload (session tag at boundary) must return FIXPP_ERR_UNKNOWN "
           "(app_payload_malformed→UNKNOWN per L-050-4)";

    // Give the engine time to deliver anything that might have been transmitted.
    std::this_thread::sleep_for(std::chrono::milliseconds{50});

    // Peer callback count must be UNCHANGED — no transmit on malformed payload.
    const int count_after = b_counter.count.load(std::memory_order_acquire);
    EXPECT_EQ(count_after, count_before_malformed)
        << "peer callback fired after malformed payload send — frame was transmitted "
           "when it should have been suppressed (no-transmit leg)";

    fixpp_engine_destroy(A);
    fixpp_engine_destroy(B);
}

// ── Q3(b): seqnum_max overflow → FIXPP_ERR_STORE_RUNTIME + no transmit ──────
//
// Seeds the session's outbound counter to seqnum_max via the FIXPP_TEST_HOOKS seam
// (set_counters_for_test). The next send triggers store_seqnum_overflow (60) →
// FIXPP_ERR_STORE_RUNTIME. The peer callback must NOT fire (no transmit).
TEST(CapiErrorLive, SeqnumOverflowReturnsStoreRuntimeNoTransmit) {
    fixpp_engine_t* B = nullptr;
    fixpp_engine_t* A = nullptr;
    // Use consumer_minor=2 so translate_for_consumer preserves FIXPP_ERR_STORE_RUNTIME
    // (introducing_minor=2 <= consumer_minor=2 → no downgrade to UNKNOWN).
    ASSERT_EQ(fixpp_engine_create(make_engine_cfg(), 1, /*consumer_minor=*/2, &B), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_engine_create(make_engine_cfg(), 1, /*consumer_minor=*/2, &A), FIXPP_ERR_OK);

    CallbackCounter b_counter;

    fixpp_session_config_t* acc =
        make_session_cfg("ACC-SEQOV", "INIT-SEQOV", FIXPP_ROLE_ACCEPTOR);
    set_loopback_endpoint(acc, "127.0.0.1", 0);
    auto acc_id = session_id_of(acc);
    fixpp_session_t* acc_h = nullptr;
    ASSERT_EQ(fixpp_session_open(B, acc, &acc_h), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_session_register_callback(acc_h, CallbackCounter::on_msg, &b_counter),
              FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_engine_start(B), FIXPP_ERR_OK);

    std::uint16_t port = wait_for_bound_port(B, acc_id);
    ASSERT_NE(port, 0u) << "acceptor did not bind";

    fixpp_session_config_t* ini =
        make_session_cfg("INIT-SEQOV", "ACC-SEQOV", FIXPP_ROLE_INITIATOR);
    set_loopback_endpoint(ini, "127.0.0.1", port);
    fixpp_session_t* ini_h = nullptr;
    ASSERT_EQ(fixpp_session_open(A, ini, &ini_h), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_engine_start(A), FIXPP_ERR_OK);
    ASSERT_TRUE(wait_for_established(ini_h)) << "initiator never established";

    // Seed the outbound seqnum to seqnum_max via the FIXPP_TEST_HOOKS seam.
    // This must be done on the session strand to be race-free. Post a blocking
    // co_spawn onto the session's executor to perform the seed, then wait.
    {
        auto* e_internal = reinterpret_cast<fixpp_engine*>(A);
        ASSERT_TRUE(e_internal->state_ != nullptr && e_internal->state_->engine_.has_value());
        const auto& id_ref = ini_h->id;  // SessionId of the initiator
        std::shared_ptr<fixpp::session::Session> sess =
            e_internal->state_->engine_->lookup(id_ref);
        ASSERT_NE(sess, nullptr) << "session lookup failed after establishment";

        // Post the seqnum seed to the session's executor (strand-confined) and
        // block until it completes. Pass the lambda directly (not pre-invoked IIFE)
        // so co_spawn owns the lambda+capture on the heap; captures sess by value so
        // no reference to a stack variable crosses the thread boundary (ASan-clean).
        //
        // Spawn on the UNDERLYING executor (asio::any_io_executor strand), NOT on
        // the session_executor wrapper — mirroring the R1 fix in session.cpp and the
        // engine.cpp:1567 precedent.  The wrapper's ~impl() is destroyed across the
        // caller thread and the worker, causing a TSan race at any_executor.hpp:475.
        auto seed_fn = [sess]() -> asio::awaitable<void> {
            auto& mgr = sess->seqnum_mgr_test_access();
            const fixpp::session::seqnum_t cur_inbound = mgr.next_inbound_unsafe();
            mgr.set_counters_for_test(cur_inbound, fixpp::session::seqnum_max);
            co_return;
        };
        const asio::any_io_executor& seed_exec = sess->executor().underlying();
        auto fut = asio::co_spawn(seed_exec, std::move(seed_fn), asio::use_future);
        fut.get();  // wait for the seam to complete on the strand
    }

    const int count_before = b_counter.count.load(std::memory_order_acquire);

    // The next send must overflow (next_outbound == seqnum_max → assign_outbound fails
    // with store_seqnum_overflow=60 → translate→FIXPP_ERR_STORE_RUNTIME).
    const auto payload = make_app_payload("SEQOV");
    const fixpp_error_t send_rc =
        fixpp_session_send(ini_h, payload.data(), payload.size());
    EXPECT_EQ(send_rc, FIXPP_ERR_STORE_RUNTIME)
        << "seqnum_max overflow must return FIXPP_ERR_STORE_RUNTIME";

    // Give the engine time to deliver anything that might have been transmitted.
    std::this_thread::sleep_for(std::chrono::milliseconds{50});

    // Peer callback must NOT fire on the overflowed send (no transmit).
    const int count_after = b_counter.count.load(std::memory_order_acquire);
    EXPECT_EQ(count_after, count_before)
        << "peer callback fired after seqnum overflow send — frame was transmitted "
           "when it should have been suppressed (no-transmit leg, seqnum overflow)";

    fixpp_engine_destroy(A);
    fixpp_engine_destroy(B);
}

// ── Q4 (finding #5): live consumer_minor downgrade through a public exported thunk ──
//
// Proves that the consumer_minor recorded in fixpp_engine_create flows through
// fixpp_engine_start to produce the correct downgraded/real error code.
//
// Engine A: consumer_minor=0 + no clock → fixpp_engine_start → FIXPP_ERR_UNKNOWN
//   (clock_not_set→FIXPP_ERR_THREAD_CONFIG→downgraded-to-UNKNOWN for minor<2)
// Engine B: consumer_minor=2 + no clock → fixpp_engine_start → FIXPP_ERR_THREAD_CONFIG
//   (clock_not_set→FIXPP_ERR_THREAD_CONFIG preserved for minor>=2)
//
// Note: the minor-3-specific sub-witness is deferred WITH the L-050-4 block per
// tasks.md T022 and error_block_test.cpp header. This test witnesses only the
// minor-2 boundary (the only published real boundary at this feature level).
TEST(CapiErrorLive, Sc004LiveDowngradeMinor2Boundary) {
    // Engine A: consumer_minor=0 — below introducing_minor(2) → downgraded to UNKNOWN.
    // Engine::start() validates the clock in EngineConfig regardless of session count,
    // so no session needs to be opened to hit the clock gate.
    {
        fixpp_engine_config_t* cfg_a = nullptr;
        ASSERT_EQ(fixpp_engine_config_create(&cfg_a), FIXPP_ERR_OK);
        // Deliberately do NOT set realtime clock → clock_not_set (54) →
        // FIXPP_ERR_THREAD_CONFIG before downgrade.
        fixpp_engine_t* eng_a = nullptr;
        ASSERT_EQ(fixpp_engine_create(cfg_a, 1, /*consumer_minor=*/0, &eng_a), FIXPP_ERR_OK);
        ASSERT_NE(eng_a, nullptr);

        const fixpp_error_t start_rc_a = fixpp_engine_start(eng_a);
        EXPECT_EQ(start_rc_a, FIXPP_ERR_UNKNOWN)
            << "consumer_minor=0: clock_not_set must be downgraded to FIXPP_ERR_UNKNOWN "
               "(introducing_minor=2 > consumer_minor=0)";

        fixpp_engine_destroy(eng_a);
    }

    // Engine B: consumer_minor=2 — at the introducing_minor → real THREAD_CONFIG.
    {
        fixpp_engine_config_t* cfg_b = nullptr;
        ASSERT_EQ(fixpp_engine_config_create(&cfg_b), FIXPP_ERR_OK);
        // Deliberately do NOT set realtime clock.
        fixpp_engine_t* eng_b = nullptr;
        ASSERT_EQ(fixpp_engine_create(cfg_b, 1, /*consumer_minor=*/2, &eng_b), FIXPP_ERR_OK);
        ASSERT_NE(eng_b, nullptr);

        const fixpp_error_t start_rc_b = fixpp_engine_start(eng_b);
        EXPECT_EQ(start_rc_b, FIXPP_ERR_THREAD_CONFIG)
            << "consumer_minor=2: clock_not_set must NOT be downgraded (introducing_minor=2 "
               "<= consumer_minor=2) — must return FIXPP_ERR_THREAD_CONFIG";

        fixpp_engine_destroy(eng_b);
    }
}
