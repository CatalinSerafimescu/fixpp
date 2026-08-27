// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/cfg_lifetime_safety_test.cpp
//
// 010-session-cfg-lifetime T007 — FR-001 / FR-002 / SC-001
//
// Verifies that Session copies the caller's SessionConfig by value at
// construction time (W-5 lifetime fix). Three runtime contracts:
//
//   A. CallerCfgDropsAfterCtor_SessionContinuesWithoutUAF
//      Build a Session from a locally-scoped cfg; let the cfg die (inner-scope
//      exit); open the session from the outer scope. Pre-fix (const-ref member)
//      this dereferences a dangling reference and ASan fires
//      `stack-use-after-scope`. Post-fix (by-value member, T008/T009) the
//      Session holds its own copy and open() succeeds clean.
//
//   B. CallerMutatesCfgAfterCtor_SessionUnaffected
//      Build a Session; mutate the original cfg after the ctor returns. Open
//      the session and inspect the outbound Logon frame — it must carry the
//      original (pre-mutation) sender_comp_id, proving the Session's copy is
//      independent of the caller's cfg.
//
//   C. MultipleSessionsFromSameCfgEvolveIndependently
//      Build two Sessions from the same cfg with different heartbt_int values
//      mutated between the two ctors. Each session emits a Logon with its own
//      value (108=<X> for sess1, 108=<Y> for sess2), proving the copies are
//      independent.
//
// Anchors: FR-001 / D-1 / SC-001 / W-5 (010 spec.md).
#include <gtest/gtest.h>

#include <array>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <future>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"
#include "support/extract_tag.hpp"

using namespace std::chrono_literals;

namespace fixpp::session::test {

// ── Helpers ───────────────────────────────────────────────────────────────────

using fixpp::test_support::extract_tag;

// ── Fixture ───────────────────────────────────────────────────────────────────

class CfgLifetimeSafetyTest : public ::testing::Test {
protected:
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine{};

    void SetUp() override {
        using namespace std::chrono;
        auto utc = system_clock::time_point{} + seconds{1704067200};
        auto stp = fixpp::core::steady_time_point{} + seconds{0};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock = clock;
        engine.executor = ioc.get_executor();
    }

    // Build a minimal initiator cfg. The heartbt_int parameter controls the
    // HeartBtInt(108) value emitted in the initial Logon frame so tests can
    // verify which copy is being read by the session.
    SessionConfig make_cfg(std::string_view sender, std::string_view target, int heartbt_int = 30) {
        SessionConfig cfg;
        cfg.sender_comp_id = std::string(sender);
        cfg.target_comp_id = std::string(target);
        cfg.begin_string = "FIX.4.2";
        cfg.heartbeat_interval = std::chrono::seconds{heartbt_int};
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override = ioc.get_executor();
        cfg.role = session_role::initiator;
        return cfg;
    }

    // Drive open() synchronously; returns the expected_t<void> result.
    fixpp::core::expected_t<void> open_sync(Session& s) {
        auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
        ioc.run_for(200ms);
        ioc.restart();
        return fut.get();
    }

    // Drive close(terminal) synchronously.
    void close_sync(Session& s) {
        auto fut = asio::co_spawn(ioc, s.close(close_mode::terminal), asio::use_future);
        ioc.run_for(200ms);
        ioc.restart();
    }
};

// ── Test A: CallerCfgDropsAfterCtor_SessionContinuesWithoutUAF ───────────────
//
// FR-001 / SC-001 / W-5: the session must remain functional after the
// caller's cfg goes out of scope.
//
// Pre-fix: cfg_ was a const-ref; after the inner scope closes the ref dangles.
// open() dereferences cfg_ (e.g. executor_override, mode, dictionary, …)
// → ASan fires stack-use-after-scope on the ref-member read.
//
// Post-fix: cfg_ is a value-typed copy; the ref going out of scope is a no-op.
// open() succeeds and the session enters LogonSent.

TEST_F(CfgLifetimeSafetyTest, CallerCfgDropsAfterCtor_SessionContinuesWithoutUAF) {
    // Capture outbound frames so open() can emit the initial Logon without
    // crashing (no transport_send → frames silently dropped; that's fine —
    // we only care that open() reaches LogonSent without UAF).
    std::vector<std::byte> captured_frame;

    // Inner scope: cfg is local here.
    std::optional<Session> sess;
    {
        SessionConfig cfg = make_cfg("SENDER", "TARGET", 30);
        // Wire a transport_send so the Logon frame build+emit path
        // exercises ALL cfg_ fields inside open() (sender_comp_id,
        // target_comp_id, begin_string, heartbeat_interval).
        cfg.transport_send = [&captured_frame](std::span<const std::byte> f) {
            captured_frame.assign(f.begin(), f.end());
        };
        // Construct: cfg_ is copied by value into the Session. (FR-001 / D-1)
        sess.emplace(engine, cfg);
        // cfg dies here — if cfg_ were a const-ref, sess holds a dangling ptr.
    }  // cfg leaves scope here

    // Open from the outer scope — reads cfg_.executor_override, cfg_.mode,
    // cfg_.locks, cfg_.app_backpressure, cfg_.dictionary,
    // cfg_.security_profile, cfg_.sender_comp_id, cfg_.target_comp_id,
    // cfg_.begin_string, cfg_.heartbeat_interval inside open().
    // Pre-fix: this is a UAF. Post-fix: reads from the owned copy. (FR-001)
    auto result = open_sync(*sess);
    ASSERT_TRUE(result.has_value()) << "Session::open() failed after caller cfg dropped: error="
                                    << static_cast<int>(result.error());

    // Session entered LogonSent — confirms open() completed a full
    // execution path including reading cfg_. (SC-001)
    EXPECT_EQ(sess->state(), fsm_state::LogonSent)
        << "Expected LogonSent after open(); cfg reads after scope exit";

    // Logon frame was captured — confirms sender_comp_id from the owned
    // copy was used to build the frame. (FR-001)
    ASSERT_FALSE(captured_frame.empty()) << "Expected an outbound Logon frame to be emitted";
    EXPECT_EQ(extract_tag(captured_frame, 49), "SENDER")
        << "Logon frame tag 49 (SenderCompID) must match the original cfg value";

    close_sync(*sess);
}

// ── Test B: CallerMutatesCfgAfterCtor_SessionUnaffected ──────────────────────
//
// FR-001 / FR-002 / SC-001: mutating the caller's cfg after the ctor returns
// must not affect the Session's copy. The Session's Logon frame carries the
// original (pre-mutation) sender_comp_id.

TEST_F(CfgLifetimeSafetyTest, CallerMutatesCfgAfterCtor_SessionUnaffected) {
    std::vector<std::byte> captured_frame;

    SessionConfig cfg = make_cfg("ORIGINAL", "TARGET", 30);
    cfg.transport_send = [&captured_frame](std::span<const std::byte> f) {
        captured_frame.assign(f.begin(), f.end());
    };

    Session sess(engine, cfg);

    // Mutate the caller's cfg AFTER the ctor. (FR-002 contract)
    cfg.sender_comp_id = "MUTATED";
    cfg.heartbeat_interval = 999s;

    // open() reads cfg_.sender_comp_id — must be "ORIGINAL" (the copy), not "MUTATED".
    auto result = open_sync(sess);
    ASSERT_TRUE(result.has_value())
        << "Session::open() failed; error=" << static_cast<int>(result.error());

    ASSERT_FALSE(captured_frame.empty()) << "Expected an outbound Logon frame";

    // Tag 49 = SenderCompID; must be the original value from the copied cfg.
    EXPECT_EQ(extract_tag(captured_frame, 49), "ORIGINAL")
        << "SenderCompID in Logon must reflect the cfg copy made at ctor time, "
           "not the post-ctor mutation of the caller's cfg";

    // Tag 108 = HeartBtInt; must be 30 (original), not 999 (mutated).
    EXPECT_EQ(extract_tag(captured_frame, 108), "30")
        << "HeartBtInt in Logon must reflect the original cfg value (30s), "
           "not the post-ctor mutation (999s)";

    close_sync(sess);
}

// ── Test C: MultipleSessionsFromSameCfgEvolveIndependently ───────────────────
//
// FR-001 / FR-002 / SC-001: two Sessions built from the same cfg with
// heartbt_int mutated between ctors must each carry their own independent copy.
// sess1 must use X (10s), sess2 must use Y (20s) in their Logon frames.

TEST_F(CfgLifetimeSafetyTest, MultipleSessionsFromSameCfgEvolveIndependently) {
    std::vector<std::byte> frame1;
    std::vector<std::byte> frame2;

    SessionConfig cfg = make_cfg("SENDER1", "TARGET", 10);  // heartbt=10s for sess1
    cfg.transport_send = [&frame1](std::span<const std::byte> f) {
        frame1.assign(f.begin(), f.end());
    };

    // Build sess1 — copy of cfg with heartbt=10s.
    Session sess1(engine, cfg);

    // Mutate cfg between the two ctors: switch to heartbt=20s + different sender.
    cfg.heartbeat_interval = 20s;
    cfg.sender_comp_id = "SENDER2";
    cfg.transport_send = [&frame2](std::span<const std::byte> f) {
        frame2.assign(f.begin(), f.end());
    };

    // Build sess2 — copy of cfg with heartbt=20s.
    Session sess2(engine, cfg);

    // Open sess1.
    auto r1 = open_sync(sess1);
    ASSERT_TRUE(r1.has_value()) << "sess1 open() failed";
    ASSERT_FALSE(frame1.empty()) << "Expected Logon from sess1";

    // Open sess2.
    auto r2 = open_sync(sess2);
    ASSERT_TRUE(r2.has_value()) << "sess2 open() failed";
    ASSERT_FALSE(frame2.empty()) << "Expected Logon from sess2";

    // sess1 Logon must carry the original values (heartbt=10, sender=SENDER1).
    EXPECT_EQ(extract_tag(frame1, 108), "10")
        << "sess1 Logon HeartBtInt must be 10 (the value at sess1 ctor time)";
    EXPECT_EQ(extract_tag(frame1, 49), "SENDER1") << "sess1 Logon SenderCompID must be SENDER1";

    // sess2 Logon must carry the mutated values (heartbt=20, sender=SENDER2).
    EXPECT_EQ(extract_tag(frame2, 108), "20")
        << "sess2 Logon HeartBtInt must be 20 (the value at sess2 ctor time)";
    EXPECT_EQ(extract_tag(frame2, 49), "SENDER2") << "sess2 Logon SenderCompID must be SENDER2";

    close_sync(sess1);
    close_sync(sess2);
}

}  // namespace fixpp::session::test
