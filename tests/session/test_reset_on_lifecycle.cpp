// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_reset_on_lifecycle.cpp
//
// 024-reset-refresh-on-logon (S-017, G3 slice 3) — ResetOn{Logon,Logout,Disconnect}
// sequence-number lifecycle knob witnesses.
//
// T015 no-heap witness uses alloc_guard_start/alloc_guard_end markers.
// mallocnesia replaces these weak no-ops with its interceptor scope markers.
// Run under: LD_PRELOAD=tools/mallocnesia/libmallocnesia.so MALLOCNESIA_MAX_ALLOCS=0

extern "C" {
// NOLINTNEXTLINE(misc-use-internal-linkage) — weak override by mallocnesia.so
__attribute__((weak)) void alloc_guard_start() {}
// NOLINTNEXTLINE(misc-use-internal-linkage)
__attribute__((weak)) void alloc_guard_end() {}
}
//
// T004 [US1] initiator witnesses (1-5):
//   (1) ResetOnLogon_Initiator_ResetsAndEmits141
//   (2) ResetOnLogon_Off_No141Beyond013
//   (3) ResetOnLogon_AllPolicies_LogonClean
//   (4) ResetOnLogon_SuppressesPreResetGap
//   (5) ResetOnLogon_Off_Inbound141_StoreFailure_StillActive     [zero-regression guard]
//
// T005 [US1] acceptor witnesses (6-9):
//   (6) ResetOnLogon_Acceptor_AdmitsFresh34eq1_LocalExpectedGt1
//   (7) ResetOnLogon_Acceptor_ResetsIdempotentWith141
//   (8) ResetOnLogon_BothRoles_Resets
//   (9) ResetOnLogon_DurableStoreFailure_BlocksActive
//
// T006 [US1]: build + run; confirm RED on behavior (not compile); (5) must PASS.
//
// RED rationale: knob values exist (T002), helper exists (T003), NO trigger wired
//   → reset never fires; initiator Logon lacks 141=Y; acceptor disconnects on
//   fresh 34=1 at local-expected>1; reset_call_count()==0; (9) still reaches Active.
//
// Anchors: specs/024-reset-refresh-on-logon/quickstart.md (witness table),
//          contracts/reset-knobs.md C2.1–C2.6, C5.1, C5.2.

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
#include <fixpp/session/message_store.hpp>
#include <fixpp/session/message_store_factory.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "session/support/frame_field_extract.hpp"   // via -I tests/
#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"
#include "support/store_double.hpp"

using namespace std::chrono_literals;
using fixpp::session::test_support::extract_field;

namespace fixpp::session::test {

// ── Frame builders ────────────────────────────────────────────────────────────
//
// All frames are FIX.4.4. Roles: our session is ISLD (sender), peer is TW.
// For acceptor role tests, peer (TW) sends the Logon to us (ISLD).
// For initiator role tests, we (ISLD) open() and emit a Logon to TW.

// Build a raw FIX.4.4 frame from a body string.
static std::vector<std::byte> make_fix44_frame(std::string_view body_str) {
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

// Build a peer Logon (35=A) — peer (TW) → our acceptor (ISLD).
// Optional: reset_seqnum=true adds 141=Y.
static std::vector<std::byte> make_peer_logon(std::uint32_t seq, bool reset_seqnum = false) {
    std::string body;
    body += "35=A\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=TW\x01";
    body += "52=20240101-00:00:00.000\x01";
    body += "56=ISLD\x01";
    body += "98=0\x01";
    body += "108=30\x01";
    if (reset_seqnum) body += "141=Y\x01";
    return make_fix44_frame(body);
}

// ── StoreDouble-backed factory ─────────────────────────────────────────────────
//
// Gives the test back a shared_ptr to the StoreDouble so we can call
// reset_call_count(), fail_next_reset(), and inspect counters.
//
// The factory creates a thin delegating wrapper so the returned unique_ptr<MessageStore>
// satisfies the MessageStoreFactory contract while the shared_ptr<StoreDouble> remains
// under the test's direct control.

// Delegating wrapper: a MessageStore that forwards every call to a shared StoreDouble.
class SharedStoreWrapper final : public MessageStore {
public:
    explicit SharedStoreWrapper(std::shared_ptr<StoreDouble> delegate) noexcept
        : MessageStore(flush_thunk_for<SharedStoreWrapper>()),
          delegate_(std::move(delegate)) {}

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> store(
        seqnum_t seq, std::span<const std::byte> frame, direction_t dir) noexcept override {
        return delegate_->store(seq, frame, dir);
    }
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> retrieve(
        seqnum_t begin, seqnum_t end, direction_t dir,
        retrieve_visitor& visitor) noexcept override {
        return delegate_->retrieve(begin, end, dir, visitor);
    }
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<seqnum_t>> next_seqnum(
        direction_t dir, bool increment) noexcept override {
        return delegate_->next_seqnum(dir, increment);
    }
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> reset() noexcept override {
        return delegate_->reset();
    }

private:
    std::shared_ptr<StoreDouble> delegate_;
};

class StoreDoubleFactory final : public MessageStoreFactory {
public:
    std::shared_ptr<StoreDouble> store = std::make_shared<StoreDouble>();

    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<MessageStore>> make(
        std::string_view, std::string_view, std::pmr::memory_resource*, std::size_t,
        asio::any_io_executor) noexcept override {
        return std::make_unique<SharedStoreWrapper>(store);
    }
};

// 030 T003: non-persistent variant — yields_persistent_store()==false so the
// FR-010 fatal-when-persistent disposition does NOT apply (stays logged).
// StoreDoubleFactory is final, so we derive from MessageStoreFactory directly
// and duplicate the minimal factory body.
class NonPersistentStoreDoubleFactory final : public MessageStoreFactory {
public:
    std::shared_ptr<StoreDouble> store = std::make_shared<StoreDouble>();

    [[nodiscard]] bool yields_persistent_store() const noexcept override { return false; }

    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<MessageStore>> make(
        std::string_view, std::string_view, std::pmr::memory_resource*, std::size_t,
        asio::any_io_executor) noexcept override {
        return std::make_unique<SharedStoreWrapper>(store);
    }
};

// ── Fixture ────────────────────────────────────────────────────────────────────

class ResetOnLifecycleTest : public ::testing::Test {
protected:
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine{};

    // All outbound frames captured by transport_send.
    std::vector<std::vector<std::byte>> captured_frames;

    // The StoreDoubleFactory shared across make_cfg() calls for a single test.
    // Tests that need direct store access get it via factory->store.
    std::shared_ptr<StoreDoubleFactory> factory;

    void SetUp() override {
        using namespace std::chrono;
        auto utc = system_clock::time_point{} + seconds{1704067200};  // 2024-01-01
        auto stp = fixpp::core::steady_time_point{} + seconds{0};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock = clock;
        engine.executor = ioc.get_executor();
        factory = std::make_shared<StoreDoubleFactory>();
    }

    // Build a session config.
    //   role: initiator (default) or acceptor
    //   reset_on_logon: knob under test
    //   policy: reset_seqnum_policy_field override
    //   reset_on_logout: US2 teardown knob (C3.1/C3.2)
    //   reset_on_disconnect: US2 teardown knob (C4.1/C4.2/C4.3)
    SessionConfig make_cfg(
        session_role role = session_role::initiator,
        bool reset_on_logon = false,
        reset_seqnum_policy policy = reset_seqnum_policy::bilateral_lenient,
        bool reset_on_logout = false,
        bool reset_on_disconnect = false) {
        SessionConfig cfg;
        cfg.sender_comp_id = "ISLD";
        cfg.target_comp_id = "TW";
        cfg.begin_string = "FIX.4.4";
        cfg.heartbeat_interval = 0s;
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override = ioc.get_executor();
        cfg.role = role;
        cfg.reset_seqnum_policy_field = policy;
        cfg.reset_on_logon = reset_on_logon;
        cfg.reset_on_logout = reset_on_logout;
        cfg.reset_on_disconnect = reset_on_disconnect;
        cfg.store_factory = factory;
        cfg.transport_send = [this](std::span<const std::byte> frame) {
            captured_frames.emplace_back(frame.begin(), frame.end());
        };
        return cfg;
    }

    // Run the ioc for a bounded duration (self-deadline = 200 ms).
    void run_ioc() {
        ioc.run_for(200ms);
        ioc.restart();
    }

    // Open (initiator: sends Logon; acceptor: waits for inbound Logon).
    // Returns the open() result.
    fixpp::core::expected_t<void> open_sync(Session& sess) {
        auto fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
        run_ioc();
        return fut.get();
    }

    // Feed an inbound frame synchronously.
    fixpp::core::expected_t<void> feed_sync(Session& sess,
                                            const std::vector<std::byte>& frame) {
        auto fut = asio::co_spawn(ioc, sess.on_inbound_frame(frame), asio::use_future);
        run_ioc();
        return fut.get();
    }

    // Drive a graceful close to completion: advance the clock past the 2 s
    // graceful-close (Logout) timeout so teardown completes.
    void graceful_close_sync(Session& sess) {
        auto fut = asio::co_spawn(ioc, sess.close(close_mode::graceful), asio::use_future);
        ioc.run_for(50ms);
        ioc.restart();
        clock->advance(std::chrono::seconds{3});
        run_ioc();
        (void)fut.get();
    }

    // Drive a terminal close to completion: abnormal drop / post-Logout teardown
    // (no graceful Logout exchange, so no clock advance needed).
    void terminal_close_sync(Session& sess) {
        auto fut = asio::co_spawn(ioc, sess.close(close_mode::terminal), asio::use_future);
        run_ioc();
        (void)fut.get();
    }

    // Drive an acceptor session to Active by feeding a valid Logon from TW.
    // seq: the Logon seqnum the peer sends (usually 1 for a clean session).
    // reset_seqnum: whether the peer Logon carries 141=Y.
    void drive_acceptor_to_active(Session& sess, std::uint32_t seq = 1,
                                  bool reset_seqnum = false) {
        auto r = open_sync(sess);
        ASSERT_TRUE(r.has_value()) << "open() failed";
        auto logon = make_peer_logon(seq, reset_seqnum);
        auto r2 = feed_sync(sess, logon);
        // We only assert active here when the test expects success; callers that
        // expect failure (e.g. witness 9) call drive_acceptor_expect_failure.
        ASSERT_TRUE(r2.has_value()) << "Logon feed failed";
        ASSERT_EQ(sess.state(), fsm_state::Active);
    }

    // Build a peer Logout (35=5) — peer (TW) → our session (ISLD).
    static std::vector<std::byte> make_peer_logout(std::uint32_t seq) {
        std::string body;
        body += "35=5\x01";
        body += "34=" + std::to_string(seq) + "\x01";
        body += "49=TW\x01";
        body += "52=20240101-00:00:00.000\x01";
        body += "56=ISLD\x01";
        return make_fix44_frame(body);
    }

    // Drive an initiator session to Active:
    //   open_sync() → LogonSent, then feed an inbound peer Logon-ack → Active.
    // Seq used for the peer Logon-ack (default: 1).
    void drive_initiator_to_active(Session& sess, std::uint32_t peer_seq = 1) {
        auto r = open_sync(sess);
        ASSERT_TRUE(r.has_value()) << "open() failed";
        ASSERT_EQ(sess.state(), fsm_state::LogonSent);

        // Feed peer Logon-ack: LogonSent → Active.
        auto logon_ack = make_peer_logon(peer_seq, /*reset_seqnum=*/false);
        auto r2 = feed_sync(sess, logon_ack);
        ASSERT_TRUE(r2.has_value()) << "Logon-ack feed failed";
        ASSERT_EQ(sess.state(), fsm_state::Active);
    }

    // Helper: check whether any captured frame carries a given SOH-boundary tag.
    static bool frame_has_tag(const std::vector<std::byte>& frame, std::uint16_t tag) {
        return extract_field(std::span<const std::byte>(frame), tag).has_value();
    }

    // Helper: check whether any captured frame has MsgType == mt.
    bool any_msg_type(std::string_view mt) const {
        for (const auto& f : captured_frames) {
            if (extract_field(std::span<const std::byte>(f), 35) == mt) return true;
        }
        return false;
    }

    bool any_resend_request() const { return any_msg_type("2"); }
};

// ═══════════════════════════════════════════════════════════════════════════════
// T004 [US1] — Initiator witnesses (1–5)
// ═══════════════════════════════════════════════════════════════════════════════

// ── Witness (1): ResetOnLogon_Initiator_ResetsAndEmits141 ─────────────────────
//
// Seed seqnums non-1 via set_counters_for_test(), reset_on_logon=true, open() as
// initiator → captured outbound Logon must have MsgSeqNum=1 AND 141=Y; persisted
// counters == 1 after reset.
//
// [C2.1, C2.6, SC-001, SC-004]
//
// RED: no trigger wired → reset never fires → Logon carries the seeded MsgSeqNum
//      (not 1), and 141=Y is absent (policy=bilateral_lenient, no reset).
TEST_F(ResetOnLifecycleTest, ResetOnLogon_Initiator_ResetsAndEmits141) {
    auto cfg = make_cfg(session_role::initiator, /*reset_on_logon=*/true,
                        reset_seqnum_policy::bilateral_lenient);
    Session sess(engine, cfg);

    // Seed non-1 counters: next_outbound=5, next_inbound=3.
    // FIXPP_TEST_HOOKS required (compiled in by CMakeLists.txt for this target).
    sess.seqnum_mgr_test_access().set_counters_for_test(
        /*next_inbound=*/static_cast<seqnum_t>(3),
        /*next_outbound=*/static_cast<seqnum_t>(5));

    // Also seed the store's counters to match so the durable reset observable via
    // reset_call_count() is a real I/O call.
    factory->store->reset_sync();
    // Manually bump store counters to simulate a non-1 persisted state.
    // (StoreDouble counts inbound/outbound internally; we use reset_sync to clear,
    // then we don't seed further — the key assertion is reset_call_count()==1.)

    auto r = open_sync(sess);
    ASSERT_TRUE(r.has_value()) << "open() failed";

    // After open(), the outbound Logon is the LAST captured frame (or only frame).
    ASSERT_FALSE(captured_frames.empty()) << "No outbound frame emitted by open()";
    const auto& logon_frame = captured_frames.back();

    // C2.1: MsgSeqNum must be 1 (reset happened before Logon was built).
    // RED: without trigger, MsgSeqNum will be 5 (the seeded value).
    auto seq34 = extract_field(std::span<const std::byte>(logon_frame), 34);
    ASSERT_TRUE(seq34.has_value()) << "Outbound Logon must carry MsgSeqNum (tag 34)";
    EXPECT_EQ(std::string(*seq34), "1")
        << "ResetOnLogon_Initiator_ResetsAndEmits141 RED (C2.1): "
           "MsgSeqNum must be 1 after reset_on_logon reset; "
           "got " << *seq34 << " (reset trigger not yet wired)";

    // C2.6 / SC-004: 141=Y must be present in the outbound Logon.
    // RED: bilateral_lenient without a reset does not emit 141=Y.
    EXPECT_TRUE(frame_has_tag(logon_frame, 141))
        << "ResetOnLogon_Initiator_ResetsAndEmits141 RED (C2.6/SC-004): "
           "141=Y must be present in the outbound Logon when reset_on_logon=true; "
           "absent (OR-of-three predicate not yet wired)";

    // C2.1: persisted counters must be 1 after the durable reset.
    // Assert via SeqnumManager live state (post-reset peek_outbound should be 1).
    const seqnum_t post_outbound = sess.seqnum_mgr_test_access().peek_outbound();
    // After emitting the Logon, peek_outbound advances to 2 (or stays at the
    // seeded value+1 if not reset). The reset places it at 1 before the Logon
    // is emitted, so after the Logon the next outbound is 2.
    // We verify: peek_outbound() == 2 (reset happened at 1, Logon consumed it).
    // Without reset: peek_outbound() == 6 (seeded 5, Logon consumed it).
    EXPECT_EQ(post_outbound, static_cast<seqnum_t>(2))
        << "ResetOnLogon_Initiator_ResetsAndEmits141 RED (C2.1): "
           "post-open peek_outbound must be 2 (reset to 1, Logon consumed); "
           "got " << post_outbound << " (reset trigger not yet wired)";
}

// ── Witness (2): ResetOnLogon_Off_No141Beyond013 ──────────────────────────────
//
// reset_on_logon=false, policy=bilateral_lenient → outbound Logon must NOT carry
// 141=Y (today's behavior); no logon-time reset.
//
// [C2.3]
//
// Expected: GREEN both pre-impl and post-impl (this IS today's behavior).
TEST_F(ResetOnLifecycleTest, ResetOnLogon_Off_No141Beyond013) {
    auto cfg = make_cfg(session_role::initiator, /*reset_on_logon=*/false,
                        reset_seqnum_policy::bilateral_lenient);
    Session sess(engine, cfg);

    auto r = open_sync(sess);
    ASSERT_TRUE(r.has_value()) << "open() failed";

    ASSERT_FALSE(captured_frames.empty()) << "No outbound frame emitted by open()";
    const auto& logon_frame = captured_frames.back();

    // C2.3: with reset_on_logon=false and bilateral_lenient policy, 141=Y must NOT appear.
    EXPECT_FALSE(frame_has_tag(logon_frame, 141))
        << "ResetOnLogon_Off_No141Beyond013: 141=Y must NOT be emitted when "
           "reset_on_logon=false and policy=bilateral_lenient; [C2.3]";

    // No reset: seqnums start at 1 (fresh session), Logon is seq=1, next=2. Fine.
}

// ── Witness (3): ResetOnLogon_AllPolicies_LogonClean ─────────────────────────
//
// For each reset_seqnum_policy ∈ {bilateral_strict, bilateral_lenient, unilateral},
// with reset_on_logon=true: the initiator opens, emits a Logon, and reaches
// LogonSent without wedging.
//
// [C2.4]
//
// RED: the knob is unwired, so initiator open() still reaches LogonSent normally.
// This witness is expected to be GREEN pre-impl (open() succeeds for all policies).
TEST_F(ResetOnLifecycleTest, ResetOnLogon_AllPolicies_LogonClean) {
    const reset_seqnum_policy kPolicies[] = {
        reset_seqnum_policy::bilateral_strict,
        reset_seqnum_policy::bilateral_lenient,
        reset_seqnum_policy::unilateral,
    };
    const char* kPolicyNames[] = {"bilateral_strict", "bilateral_lenient", "unilateral"};

    for (int i = 0; i < 3; ++i) {
        // Fresh ioc + factory for each policy cell.
        ioc.restart();
        captured_frames.clear();
        factory = std::make_shared<StoreDoubleFactory>();

        auto cfg = make_cfg(session_role::initiator, /*reset_on_logon=*/true, kPolicies[i]);
        Session sess(engine, cfg);

        auto r = open_sync(sess);
        EXPECT_TRUE(r.has_value())
            << "ResetOnLogon_AllPolicies_LogonClean [policy=" << kPolicyNames[i]
            << "]: open() must succeed (no wedge); [C2.4]";

        // Session must be in LogonSent after open() as an initiator.
        EXPECT_EQ(sess.state(), fsm_state::LogonSent)
            << "ResetOnLogon_AllPolicies_LogonClean [policy=" << kPolicyNames[i]
            << "]: initiator must be in LogonSent after open(); [C2.4]";

        // C2.4 post-impl: 141=Y must be present in the outbound Logon for all policies.
        // RED pre-impl: bilateral_strict emits 141=Y (existing behavior); bilateral_lenient
        // and unilateral do NOT (the OR-of-three predicate is not yet wired).
        // We assert 141=Y is present; failures here are expected RED for lenient/unilateral.
        if (!captured_frames.empty()) {
            const auto& logon_frame = captured_frames.back();
            EXPECT_TRUE(frame_has_tag(logon_frame, 141))
                << "ResetOnLogon_AllPolicies_LogonClean [policy=" << kPolicyNames[i]
                << "] RED: 141=Y must be emitted for all policies when reset_on_logon=true; "
                   "[C2.4; OR-of-three predicate not yet wired]";
        }
    }
}

// ── Witness (4): ResetOnLogon_SuppressesPreResetGap ──────────────────────────
//
// Seed a pre-reset inbound gap (next_inbound > 1 simulates missing frames),
// reset_on_logon=true, initiator open() → no ResendRequest for seqnums below
// the reset point.
//
// [C2.5, FR-003]
//
// RED: without the trigger, the reset never fires. After open(), the session is
//      in LogonSent. If we then feed a peer Logon at seq=1 with the acceptor
//      having a high next_inbound, a ResendRequest would be emitted. To witness
//      the suppression, we verify: post-reset the session does NOT emit a
//      ResendRequest for the "gap" that existed before the reset.
//
// The witness works as follows:
//   - Seed next_inbound=5 (there was a gap of frames 1-4 from a previous session).
//   - reset_on_logon=true: the reset should make next_inbound=1 before any check.
//   - Open as initiator (reset fires here); then feed a peer Logon seq=1.
//   - After reset, peer seq=1 == next_inbound=1 → no gap → no ResendRequest.
//   - RED: without trigger, reset never fires; next_inbound remains 5; peer Logon
//     seq=1 is "too low" (Logon validation is lenient) but the seqnum manager
//     sees inbound=1 < expected=5 → the session may disconnect or emit Logout.
//     The key assertion is: NO ResendRequest(35=2) is emitted post-reset.
TEST_F(ResetOnLifecycleTest, ResetOnLogon_SuppressesPreResetGap) {
    auto cfg = make_cfg(session_role::initiator, /*reset_on_logon=*/true,
                        reset_seqnum_policy::bilateral_lenient);
    Session sess(engine, cfg);

    // Seed next_inbound=5: simulates a prior session that left gaps 1-4.
    sess.seqnum_mgr_test_access().set_counters_for_test(
        /*next_inbound=*/static_cast<seqnum_t>(5),
        /*next_outbound=*/static_cast<seqnum_t>(1));  // outbound fresh

    auto r = open_sync(sess);
    ASSERT_TRUE(r.has_value()) << "open() failed";

    // Now feed a peer Logon at seq=1 (fresh peer).
    // Post-reset: next_inbound==1, so seq=1 is exactly expected → no gap, no ResendRequest.
    // Pre-reset (RED): next_inbound==5, seq=1 is too-low → disconnect; no ResendRequest
    //                  either, but for the wrong reason (too-low = fatal). After reset, the
    //                  gap is gone and seq=1 is admitted cleanly.
    captured_frames.clear();
    auto logon = make_peer_logon(1);
    (void)feed_sync(sess, logon);

    // C2.5 / FR-003: No ResendRequest must be emitted (the pre-reset gap is gone).
    EXPECT_FALSE(any_resend_request())
        << "ResetOnLogon_SuppressesPreResetGap: no ResendRequest(35=2) must be emitted "
           "after reset_on_logon reset; the pre-reset inbound gap is erased; [C2.5/FR-003]";

    // Also assert the session reached Active (peer Logon seq=1 was admitted).
    // RED: without reset, next_inbound=5 → seq=1 is too-low → Disconnected.
    EXPECT_EQ(sess.state(), fsm_state::Active)
        << "ResetOnLogon_SuppressesPreResetGap RED: after reset_on_logon reset, "
           "peer Logon seq=1 must be admitted (next_inbound reset to 1); "
           "got Disconnected (reset trigger not yet wired; next_inbound still 5); [C2.5]";
}

// ── Witness (5a): ResetOnLogon_Off_Received141_StoreFailure_PersistentDisconnects ─
//
// 030 FR-010 — contract amendment of the 024 FR-001/C2.6 I-07 logged-then-proceed
// rule for the PERSISTENT received-141 sub-case. ALL 024 knobs off, inbound Logon
// carries 141=Y, store_.fail_next_reset() injected. The default StoreDoubleFactory
// yields_persistent_store()==true, so under FR-010 (fatal-when-persistent) the reset
// failure is now FATAL: the session Disconnects and the store error propagates.
// (Was: stay-Active under 024 I-07 — see the non-persistent sibling (5b) for the
// retained stay-Active characterization.)
//
// Rationale: a swallowed reset failure on a persistent store would leave the store
// stale, then the FR-005 persist-to-2 would advance the stale store → store > manager
// (the 029 over-persist loss on restart). Fatal-when-persistent guarantees persist-to-2
// only runs after a known-good reset.
//
// [030 FR-010; amends 024 C2.6/FR-001 I-07]
TEST_F(ResetOnLifecycleTest, ResetOnLogon_Off_Received141_StoreFailure_PersistentDisconnects) {
    // Default StoreDoubleFactory → yields_persistent_store()==true.
    auto cfg = make_cfg(session_role::acceptor, /*reset_on_logon=*/false,
                        reset_seqnum_policy::bilateral_lenient);
    Session sess(engine, cfg);

    // Inject a store reset failure: the next awaitable reset() call returns error.
    factory->store->fail_next_reset();

    auto r = open_sync(sess);
    ASSERT_TRUE(r.has_value()) << "open() failed";

    // Feed Logon with 141=Y (the received-141 path).
    auto logon = make_peer_logon(1, /*reset_seqnum=*/true);
    auto r2 = feed_sync(sess, logon);

    // 030 FR-010: persistent received-141 reset failure → fatal → Disconnected + error.
    EXPECT_FALSE(r2.has_value())
        << "ResetOnLogon_Off_Received141_StoreFailure_PersistentDisconnects: "
           "on_inbound_frame must propagate the store error on a persistent store "
           "(fatal-when-persistent); [030 FR-010]";
    EXPECT_EQ(sess.state(), fsm_state::Disconnected)
        << "ResetOnLogon_Off_Received141_StoreFailure_PersistentDisconnects: "
           "persistent received-141 reset failure must Disconnect (was stay-Active under "
           "024 I-07; 030 FR-010 amends that contract for the persistent sub-case)";
}

// ── Witness (5b): ResetOnLogon_Off_Received141_StoreFailure_NonPersistentStillActive ─
//
// The RETAINED 024 I-07 characterization for NON-persistent stores. With a
// yields_persistent_store()==false factory, FR-010 keeps the disposition `logged`
// (the reset cannot meaningfully fail / no durable counter), so a received-141 reset
// "failure" does NOT disconnect — the session still reaches Active. Non-persistent
// stores are unaffected by the 030 fatal-when-persistent amendment.
//
// [030 FR-010 non-persistent arm; retains 024 C2.6/FR-001 I-07 stay-Active]
TEST_F(ResetOnLifecycleTest, ResetOnLogon_Off_Received141_StoreFailure_NonPersistentStillActive) {
    auto np_factory = std::make_shared<NonPersistentStoreDoubleFactory>();
    auto cfg = make_cfg(session_role::acceptor, /*reset_on_logon=*/false,
                        reset_seqnum_policy::bilateral_lenient);
    cfg.store_factory = np_factory;  // override the persistent default (5b)
    Session sess(engine, cfg);

    np_factory->store->fail_next_reset();

    auto r = open_sync(sess);
    ASSERT_TRUE(r.has_value()) << "open() failed";

    auto logon = make_peer_logon(1, /*reset_seqnum=*/true);
    auto r2 = feed_sync(sess, logon);

    // 030 FR-010 non-persistent: stays logged → session reaches Active (024 I-07 retained).
    ASSERT_TRUE(r2.has_value())
        << "ResetOnLogon_Off_Received141_StoreFailure_NonPersistentStillActive: "
           "non-persistent received-141 reset 'failure' must NOT disconnect (logged "
           "disposition retained for non-persistent stores); [030 FR-010 / 024 I-07]";
    EXPECT_EQ(sess.state(), fsm_state::Active)
        << "ResetOnLogon_Off_Received141_StoreFailure_NonPersistentStillActive: "
           "non-persistent store received-141 reset failure stays Active (024 I-07 retained)";
}

// ═══════════════════════════════════════════════════════════════════════════════
// T005 [US1] — Acceptor witnesses (6–9)
// ═══════════════════════════════════════════════════════════════════════════════

// ── Witness (6): ResetOnLogon_Acceptor_AdmitsFresh34eq1_LocalExpectedGt1 ──────
//
// Acceptor with local next_inbound > 1, reset_on_logon=true, inbound Logon
// at seq=1 → no disconnect, no ResendRequest, reaches Active, seqnums {1,1}.
//
// [C2.2, FR-002, FR-003, SC-001]
//
// RED: without the trigger, the reset never fires before check_inbound.
//      Logon at seq=1 with expected=5 → too-low → Disconnected (session-fatal).
TEST_F(ResetOnLifecycleTest, ResetOnLogon_Acceptor_AdmitsFresh34eq1_LocalExpectedGt1) {
    auto cfg = make_cfg(session_role::acceptor, /*reset_on_logon=*/true,
                        reset_seqnum_policy::bilateral_lenient);
    Session sess(engine, cfg);

    // Seed local next_inbound=5 (acceptor's expected inbound seqnum).
    sess.seqnum_mgr_test_access().set_counters_for_test(
        /*next_inbound=*/static_cast<seqnum_t>(5),
        /*next_outbound=*/static_cast<seqnum_t>(3));

    auto r = open_sync(sess);
    ASSERT_TRUE(r.has_value()) << "open() failed";

    // Feed Logon from peer at seq=1 (fresh peer).
    captured_frames.clear();
    auto logon = make_peer_logon(1);
    (void)feed_sync(sess, logon);

    // C2.2 / FR-002: acceptor must NOT disconnect — must reach Active.
    // RED: without reset, next_inbound=5, peer seq=1 → too-low → Disconnected.
    EXPECT_EQ(sess.state(), fsm_state::Active)
        << "ResetOnLogon_Acceptor_AdmitsFresh34eq1_LocalExpectedGt1 RED (C2.2/FR-002): "
           "acceptor with reset_on_logon=true must admit peer Logon seq=1 even when "
           "local next_inbound=5; got Disconnected (reset must fire BEFORE check_inbound; "
           "trigger not yet wired)";

    // FR-003: no ResendRequest must be emitted (the pre-reset gap is erased).
    EXPECT_FALSE(any_resend_request())
        << "ResetOnLogon_Acceptor_AdmitsFresh34eq1_LocalExpectedGt1 RED (FR-003): "
           "no ResendRequest(35=2) must be emitted after the acceptor reset; [FR-003/C2.5]";

    // SC-001: seqnums must be {1,1} after the acceptor reset.
    // After reset, next_inbound reset to 1, Logon seq=1 consumed → next_inbound=2.
    const seqnum_t next_in = sess.seqnum_mgr_test_access().next_inbound_unsafe();
    EXPECT_EQ(next_in, static_cast<seqnum_t>(2))
        << "ResetOnLogon_Acceptor_AdmitsFresh34eq1_LocalExpectedGt1 RED (SC-001): "
           "next_inbound must be 2 after reset (1) + Logon consumed (1); "
           "got " << next_in << " (reset trigger not yet wired; still at 5+1=6)";
}

// ── Witness (7): ResetOnLogon_Acceptor_ResetsIdempotentWith141 ────────────────
//
// Acceptor with reset_on_logon=true AND inbound Logon carrying 141=Y: the combined
// The knob + peer-141 overlap resolves (by mutual exclusion) to one store reset. Assert exactly ONE
// observable store_.reset() call via reset_call_count().
//
// [C2.2, C5.1]
//
// RED: without trigger, no store reset fires → reset_call_count()==0 (not 1).
TEST_F(ResetOnLifecycleTest, ResetOnLogon_Acceptor_ResetsIdempotentWith141) {
    auto cfg = make_cfg(session_role::acceptor, /*reset_on_logon=*/true,
                        reset_seqnum_policy::bilateral_lenient);
    Session sess(engine, cfg);

    auto r = open_sync(sess);
    ASSERT_TRUE(r.has_value()) << "open() failed";

    // The store starts with reset_call_count()==0.
    ASSERT_EQ(factory->store->reset_call_count(), 0u)
        << "StoreDouble must start at reset_call_count==0";

    // Feed Logon carrying 141=Y (both knob + peer-141 fire; must collapse to 1 reset).
    auto logon = make_peer_logon(1, /*reset_seqnum=*/true);
    (void)feed_sync(sess, logon);

    // C5.1: exactly one store reset observable (knob-driven arm; 013-only arm mutually excluded).
    // RED: without trigger, no store reset fires → count==0.
    EXPECT_EQ(factory->store->reset_call_count(), 1u)
        << "ResetOnLogon_Acceptor_ResetsIdempotentWith141 RED (C5.1): "
           "exactly one store_.reset() must fire for (reset_on_logon=true + 141=Y); "
           "got " << factory->store->reset_call_count()
           << " (acceptor reset not yet wired)";

    // Also check session reached Active.
    EXPECT_EQ(sess.state(), fsm_state::Active)
        << "ResetOnLogon_Acceptor_ResetsIdempotentWith141: session must reach Active; [C2.2]";
}

// ── Witness (8): ResetOnLogon_BothRoles_Resets ────────────────────────────────
//
// reset_on_logon=true holds for BOTH initiator AND acceptor roles.
//
// [C5.2]
//
// Initiator arm: seed non-1 outbound, open() → Logon carries MsgSeqNum=1 AND 141=Y.
// Acceptor arm:  seed non-1 inbound,  feed seq=1 → reaches Active, next_inbound=2.
//
// RED: both arms fail on behavior (same root cause: trigger not wired).
TEST_F(ResetOnLifecycleTest, ResetOnLogon_BothRoles_Resets) {
    // ── Initiator arm ──
    {
        ioc.restart();
        captured_frames.clear();
        factory = std::make_shared<StoreDoubleFactory>();

        auto cfg = make_cfg(session_role::initiator, /*reset_on_logon=*/true,
                            reset_seqnum_policy::bilateral_lenient);
        Session sess(engine, cfg);

        // Seed non-1 outbound.
        sess.seqnum_mgr_test_access().set_counters_for_test(
            /*next_inbound=*/static_cast<seqnum_t>(1),
            /*next_outbound=*/static_cast<seqnum_t>(7));

        auto r = open_sync(sess);
        ASSERT_TRUE(r.has_value()) << "BothRoles_Resets initiator: open() failed";

        // Initiator Logon must carry MsgSeqNum=1 (post-reset) and 141=Y.
        ASSERT_FALSE(captured_frames.empty())
            << "BothRoles_Resets initiator: no outbound frame";
        const auto& logon = captured_frames.back();

        auto seq34 = extract_field(std::span<const std::byte>(logon), 34);
        EXPECT_EQ(std::string(seq34.value_or("missing")), "1")
            << "ResetOnLogon_BothRoles_Resets initiator RED (C5.2): "
               "MsgSeqNum must be 1 after reset; got "
               << seq34.value_or("missing") << "; [C5.2/C2.1]";

        EXPECT_TRUE(frame_has_tag(logon, 141))
            << "ResetOnLogon_BothRoles_Resets initiator RED (C5.2): "
               "141=Y must be emitted; [C5.2/C2.6]";
    }

    // ── Acceptor arm ──
    {
        ioc.restart();
        captured_frames.clear();
        factory = std::make_shared<StoreDoubleFactory>();

        auto cfg = make_cfg(session_role::acceptor, /*reset_on_logon=*/true,
                            reset_seqnum_policy::bilateral_lenient);
        Session sess(engine, cfg);

        // Seed non-1 inbound.
        sess.seqnum_mgr_test_access().set_counters_for_test(
            /*next_inbound=*/static_cast<seqnum_t>(8),
            /*next_outbound=*/static_cast<seqnum_t>(1));

        auto r = open_sync(sess);
        ASSERT_TRUE(r.has_value()) << "BothRoles_Resets acceptor: open() failed";

        // Feed Logon at seq=1 from peer.
        auto logon = make_peer_logon(1);
        (void)feed_sync(sess, logon);

        // Acceptor must reach Active (fresh seq=1 admitted because reset fired).
        EXPECT_EQ(sess.state(), fsm_state::Active)
            << "ResetOnLogon_BothRoles_Resets acceptor RED (C5.2): "
               "acceptor must reach Active when reset_on_logon=true and peer seq=1; "
               "got Disconnected (trigger not yet wired; next_inbound=8 → too-low fatal); "
               "[C5.2/C2.2]";
    }
}

// ── Witness (9): ResetOnLogon_DurableStoreFailure_BlocksActive ────────────────
//
// reset_on_logon=true (knob-driven → fatal disposition), store_.fail_next_reset()
// injected → session does NOT reach Active (error propagated, not swallowed).
// This is the DUAL of witness (5): knob ON + store failure = fatal; knob OFF = I-07.
//
// [C2.6, FR-008]
//
// RED: without the trigger wired, the reset never fires, so the store failure is
//      never encountered → session reaches Active normally (wrong behavior).
//      Assert: session does NOT reach Active.
TEST_F(ResetOnLifecycleTest, ResetOnLogon_DurableStoreFailure_BlocksActive) {
    auto cfg = make_cfg(session_role::acceptor, /*reset_on_logon=*/true,
                        reset_seqnum_policy::bilateral_lenient);
    Session sess(engine, cfg);

    // Inject a store reset failure.
    factory->store->fail_next_reset();

    auto r = open_sync(sess);
    ASSERT_TRUE(r.has_value()) << "open() failed";

    // Feed a peer Logon (this triggers the reset path if wired).
    auto logon = make_peer_logon(1);
    auto r2 = feed_sync(sess, logon);

    // C2.6 / FR-008: with reset_on_logon=true and a store failure, the session
    // MUST NOT reach Active (fatal disposition propagates the error).
    //
    // RED: without trigger, store reset never fires → session reaches Active normally.
    EXPECT_NE(sess.state(), fsm_state::Active)
        << "ResetOnLogon_DurableStoreFailure_BlocksActive RED (C2.6/FR-008): "
           "session must NOT reach Active when knob-driven reset fails at store; "
           "got Active (the fatal disposition is not yet wired; reset never fired)";

    // The session must have disconnected (store failure on the knob path = fatal).
    EXPECT_EQ(sess.state(), fsm_state::Disconnected)
        << "ResetOnLogon_DurableStoreFailure_BlocksActive RED (C2.6): "
           "session must reach Disconnected on a fatal store-reset failure; [C2.6]";
}

// ═══════════════════════════════════════════════════════════════════════════════
// T010 [US2] — Teardown reset witnesses (SC-002: logouts/disconnects leave {1,1})
// ═══════════════════════════════════════════════════════════════════════════════
//
// RED rationale: T014 (teardown reset wiring) is NOT yet implemented.
// → after any teardown, reset_call_count()==0 and the seeded non-1 live counters
//   are UNCHANGED.
// → (4) off-preserves witnesses are GREEN today (current behavior IS preservation).
//
// Teardown seam:
//   graceful close  : close(close_mode::graceful) → advance clock past 2s timeout
//   peer-initiated  : feed_sync(sess, make_peer_logout(seq)) → session transitions
//                     inline to Disconnected (Active + inbound 35=5 → terminal close)
//   abnormal drop   : close(close_mode::terminal) with no Logout exchange
//
// Mirrors logout_exchange_test.cpp (GracefulBothDirections / DisconnectedInboundLogout)
// and cancellation_two_phase_test.cpp (CloseTerminalSkipsPhase1).

// ── Witness (1): ResetOnLogout_LocalInitiated_Resets ─────────────────────────
//
// reset_on_logout=true, drive initiator to Active with seeded non-1 seqnums,
// close(graceful) → reset fired: reset_call_count()==1 AND live counters→1.
//
// [C3.1]
//
// RED: no teardown reset wired → count==0 and seeded counters unchanged.
TEST_F(ResetOnLifecycleTest, ResetOnLogout_LocalInitiated_Resets) {
    auto cfg = make_cfg(
        session_role::initiator,
        /*reset_on_logon=*/false,
        reset_seqnum_policy::bilateral_lenient,
        /*reset_on_logout=*/true,
        /*reset_on_disconnect=*/false);
    Session sess(engine, cfg);

    // Drive to Active first (with clean {1,1} seqnums so Logon exchange succeeds).
    drive_initiator_to_active(sess);
    ASSERT_EQ(sess.state(), fsm_state::Active);

    // Seed non-1 seqnums AFTER reaching Active so "==1 after teardown" is meaningful.
    // The Logon exchange is done; we can safely advance the live counters.
    sess.seqnum_mgr_test_access().set_counters_for_test(
        /*next_inbound=*/static_cast<seqnum_t>(7),
        /*next_outbound=*/static_cast<seqnum_t>(5));

    const std::size_t calls_before = factory->store->reset_call_count();

    // Initiate graceful close.
    graceful_close_sync(sess);

    ASSERT_EQ(sess.state(), fsm_state::Disconnected);

    // C3.1: teardown reset must have fired exactly once.
    // RED: reset_call_count() stays at calls_before (teardown not wired).
    EXPECT_EQ(factory->store->reset_call_count(), calls_before + 1u)
        << "ResetOnLogout_LocalInitiated_Resets RED (C3.1): "
           "exactly one store_.reset() must fire on graceful Logout teardown "
           "when reset_on_logout=true; got count=="
        << factory->store->reset_call_count()
        << " (teardown trigger not yet wired — T014 pending)";

    // C3.1: live manager counters must be reset to 1.
    // After reset, peek_outbound()==1 (not yet used), next_inbound()==1.
    const seqnum_t out_after = sess.seqnum_mgr_test_access().peek_outbound();
    const seqnum_t in_after  = sess.seqnum_mgr_test_access().next_inbound_unsafe();
    EXPECT_EQ(out_after, static_cast<seqnum_t>(1))
        << "ResetOnLogout_LocalInitiated_Resets RED (C3.1): "
           "peek_outbound must be 1 after teardown reset; got " << out_after;
    EXPECT_EQ(in_after, static_cast<seqnum_t>(1))
        << "ResetOnLogout_LocalInitiated_Resets RED (C3.1): "
           "next_inbound must be 1 after teardown reset; got " << in_after;
}

// ── Witness (2): ResetOnLogout_PeerInitiated_Resets ──────────────────────────
//
// reset_on_logout=true, drive initiator to Active, feed inbound peer Logout (35=5)
// → session transitions to Disconnected inline (Active + inbound Logout → emit
//   confirming Logout → Disconnected) → reset must fire.
//
// This is the half a close(graceful)-only key would MISS (peer drives Logout,
// our session receives it and immediately disconnects without a close() call).
//
// [C3.1]
//
// RED: no teardown reset wired → count==0 and seeded counters unchanged.
TEST_F(ResetOnLifecycleTest, ResetOnLogout_PeerInitiated_Resets) {
    auto cfg = make_cfg(
        session_role::initiator,
        /*reset_on_logon=*/false,
        reset_seqnum_policy::bilateral_lenient,
        /*reset_on_logout=*/true,
        /*reset_on_disconnect=*/false);
    Session sess(engine, cfg);

    // Drive to Active first with clean seqnums.
    drive_initiator_to_active(sess);
    ASSERT_EQ(sess.state(), fsm_state::Active);

    // Seed non-1 seqnums AFTER reaching Active.
    sess.seqnum_mgr_test_access().set_counters_for_test(
        /*next_inbound=*/static_cast<seqnum_t>(8),
        /*next_outbound=*/static_cast<seqnum_t>(4));

    const std::size_t calls_before = factory->store->reset_call_count();

    // Feed peer Logout at seq=8 (matching next_inbound we just seeded to 8).
    // Active + inbound 35=5: sets logout_seen_=true, emits confirming Logout,
    // transitions fsm_state → Disconnected (but lifecycle state_ stays open).
    // The teardown reset is in close() only — mirrors the production path where
    // the read-pump hits EOF after the peer Logout and calls close(terminal).
    // [contracts/reset-knobs.md C3.1; plan.md teardown design]
    auto peer_logout = make_peer_logout(8);
    (void)feed_sync(sess, peer_logout);

    // After the inbound peer Logout, fsm_state is Disconnected but lifecycle
    // state_ is still open — close() has NOT been called yet.
    ASSERT_EQ(sess.state(), fsm_state::Disconnected);

    // Mirror the production read-pump EOF path: call close(terminal) now.
    // In production the engine's read-pump exits after the inline Disconnected
    // transition and calls close(terminal) to run the two-phase teardown.
    // close() does NOT early-return on fsm_state::Disconnected — it checks the
    // lifecycle state_ (still open), enters the two-phase body, and reaches the
    // teardown-reset block where (logout_seen_=true && reset_on_logout=true) fires.
    terminal_close_sync(sess);

    // C3.1: teardown reset must have fired (via close(terminal) reaching the
    // teardown-reset block with logout_seen_=true and reset_on_logout=true).
    EXPECT_EQ(factory->store->reset_call_count(), calls_before + 1u)
        << "ResetOnLogout_PeerInitiated_Resets (C3.1): "
           "store_.reset() must fire on peer-initiated Logout teardown "
           "when reset_on_logout=true (via close(terminal) mirroring read-pump EOF); "
           "got count==" << factory->store->reset_call_count();

    const seqnum_t out_after = sess.seqnum_mgr_test_access().peek_outbound();
    const seqnum_t in_after  = sess.seqnum_mgr_test_access().next_inbound_unsafe();
    EXPECT_EQ(out_after, static_cast<seqnum_t>(1))
        << "ResetOnLogout_PeerInitiated_Resets (C3.1): "
           "peek_outbound must be 1 after peer-Logout teardown reset; got " << out_after;
    EXPECT_EQ(in_after, static_cast<seqnum_t>(1))
        << "ResetOnLogout_PeerInitiated_Resets (C3.1): "
           "next_inbound must be 1 after peer-Logout teardown reset; got " << in_after;
}

// ── Witness (3): ResetOnDisconnect_AbnormalDrop_Resets ───────────────────────
//
// reset_on_disconnect=true, drive to Active, close(close_mode::terminal) with no
// Logout exchange (simulates raw transport EOF / abnormal drop) → reset fired.
//
// [C4.1, C4.2]
//
// RED: no teardown reset wired → count==0 and seeded counters unchanged.
TEST_F(ResetOnLifecycleTest, ResetOnDisconnect_AbnormalDrop_Resets) {
    auto cfg = make_cfg(
        session_role::initiator,
        /*reset_on_logon=*/false,
        reset_seqnum_policy::bilateral_lenient,
        /*reset_on_logout=*/false,
        /*reset_on_disconnect=*/true);
    Session sess(engine, cfg);

    // Drive to Active first with clean seqnums.
    drive_initiator_to_active(sess);
    ASSERT_EQ(sess.state(), fsm_state::Active);

    // Seed non-1 seqnums AFTER reaching Active so "==1 after" is meaningful.
    sess.seqnum_mgr_test_access().set_counters_for_test(
        /*next_inbound=*/static_cast<seqnum_t>(9),
        /*next_outbound=*/static_cast<seqnum_t>(6));

    const std::size_t calls_before = factory->store->reset_call_count();

    // Terminal close: no Logout emitted (simulates abnormal transport drop).
    terminal_close_sync(sess);

    ASSERT_EQ(sess.state(), fsm_state::Disconnected);

    // C4.1/C4.2: teardown reset must fire on abnormal drop.
    // RED: count stays at calls_before.
    EXPECT_EQ(factory->store->reset_call_count(), calls_before + 1u)
        << "ResetOnDisconnect_AbnormalDrop_Resets RED (C4.1/C4.2): "
           "store_.reset() must fire on terminal close (abnormal drop) "
           "when reset_on_disconnect=true; got count=="
        << factory->store->reset_call_count()
        << " (teardown trigger not yet wired — T014 pending)";

    const seqnum_t out_after = sess.seqnum_mgr_test_access().peek_outbound();
    const seqnum_t in_after  = sess.seqnum_mgr_test_access().next_inbound_unsafe();
    EXPECT_EQ(out_after, static_cast<seqnum_t>(1))
        << "ResetOnDisconnect_AbnormalDrop_Resets RED (C4.1): "
           "peek_outbound must be 1 after reset_on_disconnect teardown reset; "
           "got " << out_after;
    EXPECT_EQ(in_after, static_cast<seqnum_t>(1))
        << "ResetOnDisconnect_AbnormalDrop_Resets RED (C4.1): "
           "next_inbound must be 1 after reset_on_disconnect teardown reset; "
           "got " << in_after;
}

// ── Witness (4a): ResetOnLogout_Off_Preserves ────────────────────────────────
//
// reset_on_logout=false → Logout teardown preserves non-1 counters.
// reset_call_count()==0 after teardown; seeded counters unchanged.
//
// [C3.2]
//
// Expected: GREEN today (current behavior IS preservation — no teardown reset wired).
TEST_F(ResetOnLifecycleTest, ResetOnLogout_Off_Preserves) {
    auto cfg = make_cfg(
        session_role::initiator,
        /*reset_on_logon=*/false,
        reset_seqnum_policy::bilateral_lenient,
        /*reset_on_logout=*/false,
        /*reset_on_disconnect=*/false);
    Session sess(engine, cfg);

    // Drive to Active first with clean seqnums.
    drive_initiator_to_active(sess);
    ASSERT_EQ(sess.state(), fsm_state::Active);

    // Seed non-1 inbound AFTER reaching Active; we expect it PRESERVED after teardown.
    sess.seqnum_mgr_test_access().set_counters_for_test(
        /*next_inbound=*/static_cast<seqnum_t>(5),
        /*next_outbound=*/static_cast<seqnum_t>(3));

    // C3.2: snapshot inbound counter before teardown.
    // (outbound is consumed by possible Logout emits, so we track next_inbound
    //  to verify preservation of the non-reset path.)
    const seqnum_t in_before = sess.seqnum_mgr_test_access().next_inbound_unsafe();

    // Graceful close — knob is off, so no reset must fire.
    graceful_close_sync(sess);

    ASSERT_EQ(sess.state(), fsm_state::Disconnected);

    // C3.2: reset_call_count must be 0 (no store reset fired).
    EXPECT_EQ(factory->store->reset_call_count(), 0u)
        << "ResetOnLogout_Off_Preserves (C3.2): "
           "reset_call_count must remain 0 when reset_on_logout=false; "
           "got " << factory->store->reset_call_count();

    // C3.2: next_inbound preserved (not reset to 1).
    const seqnum_t in_after = sess.seqnum_mgr_test_access().next_inbound_unsafe();
    EXPECT_EQ(in_after, in_before)
        << "ResetOnLogout_Off_Preserves (C3.2): "
           "next_inbound must be preserved (unchanged) after Logout when reset_on_logout=false; "
           "expected " << in_before << " got " << in_after;
}

// ── Witness (4b): ResetOnDisconnect_Off_Preserves ────────────────────────────
//
// reset_on_disconnect=false → abnormal disconnect preserves non-1 counters.
// reset_call_count()==0 after teardown; seeded counters unchanged.
//
// [C4.3]
//
// Expected: GREEN today (current behavior IS preservation).
TEST_F(ResetOnLifecycleTest, ResetOnDisconnect_Off_Preserves) {
    auto cfg = make_cfg(
        session_role::initiator,
        /*reset_on_logon=*/false,
        reset_seqnum_policy::bilateral_lenient,
        /*reset_on_logout=*/false,
        /*reset_on_disconnect=*/false);
    Session sess(engine, cfg);

    // Drive to Active first with clean seqnums.
    drive_initiator_to_active(sess);
    ASSERT_EQ(sess.state(), fsm_state::Active);

    // Seed non-1 inbound AFTER reaching Active; expect it PRESERVED after teardown.
    sess.seqnum_mgr_test_access().set_counters_for_test(
        /*next_inbound=*/static_cast<seqnum_t>(6),
        /*next_outbound=*/static_cast<seqnum_t>(4));

    const seqnum_t in_before = sess.seqnum_mgr_test_access().next_inbound_unsafe();

    // Terminal close (abnormal drop): knob is off.
    terminal_close_sync(sess);

    ASSERT_EQ(sess.state(), fsm_state::Disconnected);

    // C4.3: no store reset fired.
    EXPECT_EQ(factory->store->reset_call_count(), 0u)
        << "ResetOnDisconnect_Off_Preserves (C4.3): "
           "reset_call_count must remain 0 when reset_on_disconnect=false; "
           "got " << factory->store->reset_call_count();

    // C4.3: next_inbound preserved.
    const seqnum_t in_after = sess.seqnum_mgr_test_access().next_inbound_unsafe();
    EXPECT_EQ(in_after, in_before)
        << "ResetOnDisconnect_Off_Preserves (C4.3): "
           "next_inbound must be preserved (unchanged) after abnormal close "
           "when reset_on_disconnect=false; "
           "expected " << in_before << " got " << in_after;
}

// ── Witness (5): ResetOnLogoutAndDisconnect_DoubleTrigger_OneStoreReset ───────
//
// Both reset_on_logout=true AND reset_on_disconnect=true; graceful Logout (which
// sets logout_seen_=true) followed by the session reaching Disconnected → the
// single-fire guard collapses both triggers to exactly ONE observable store reset.
//
// C5.1: FileStore::reset() is non-idempotent I/O; a double-fire corrupts state.
//
// [C5.1]
//
// RED: no teardown reset wired → reset_call_count()==0 (not 1). After T014 wires
//      it, the guard must prevent a count of 2.
TEST_F(ResetOnLifecycleTest, ResetOnLogoutAndDisconnect_DoubleTrigger_OneStoreReset) {
    auto cfg = make_cfg(
        session_role::initiator,
        /*reset_on_logon=*/false,
        reset_seqnum_policy::bilateral_lenient,
        /*reset_on_logout=*/true,
        /*reset_on_disconnect=*/true);
    Session sess(engine, cfg);

    // Drive to Active first with clean seqnums.
    drive_initiator_to_active(sess);
    ASSERT_EQ(sess.state(), fsm_state::Active);

    // Seed non-1 seqnums AFTER reaching Active.
    sess.seqnum_mgr_test_access().set_counters_for_test(
        /*next_inbound=*/static_cast<seqnum_t>(4),
        /*next_outbound=*/static_cast<seqnum_t>(2));

    // Graceful close: emits Logout (logout_seen_=true), times out → Disconnected.
    // Both knobs fire conceptually; the single-fire guard must reduce to 1 reset.
    graceful_close_sync(sess);

    ASSERT_EQ(sess.state(), fsm_state::Disconnected);

    // C5.1: exactly ONE store reset must fire (single-fire guard collapses double trigger).
    // RED: count==0 (teardown not wired).
    EXPECT_EQ(factory->store->reset_call_count(), 1u)
        << "ResetOnLogoutAndDisconnect_DoubleTrigger_OneStoreReset RED (C5.1): "
           "exactly one store_.reset() must fire even when both reset_on_logout "
           "AND reset_on_disconnect are true (single-fire guard); "
           "got count==" << factory->store->reset_call_count()
        << " (teardown not wired — T014 pending; expected RED=0 until then)";
}

// ── Witness (6a): ResetOnLogout_BothRoles_Resets ─────────────────────────────
//
// reset_on_logout=true fires for BOTH initiator AND acceptor roles.
//
// Initiator arm: graceful close → reset fired.
// Acceptor arm : peer-initiated Logout (feed 35=5 from peer) → reset fired.
//
// [C5.2]
//
// RED: no teardown reset wired → count==0 for both arms.
TEST_F(ResetOnLifecycleTest, ResetOnLogout_BothRoles_Resets) {
    // ── Initiator arm ──
    {
        ioc.restart();
        captured_frames.clear();
        factory = std::make_shared<StoreDoubleFactory>();

        auto cfg = make_cfg(
            session_role::initiator,
            /*reset_on_logon=*/false,
            reset_seqnum_policy::bilateral_lenient,
            /*reset_on_logout=*/true,
            /*reset_on_disconnect=*/false);
        Session sess(engine, cfg);

        // Drive to Active first with clean seqnums.
        drive_initiator_to_active(sess);
        ASSERT_EQ(sess.state(), fsm_state::Active)
            << "BothRoles initiator: must reach Active";

        // Seed non-1 AFTER reaching Active.
        sess.seqnum_mgr_test_access().set_counters_for_test(
            static_cast<seqnum_t>(6), static_cast<seqnum_t>(3));

        graceful_close_sync(sess);

        ASSERT_EQ(sess.state(), fsm_state::Disconnected);

        EXPECT_EQ(factory->store->reset_call_count(), 1u)
            << "ResetOnLogout_BothRoles_Resets initiator arm RED (C5.2): "
               "store_.reset() must fire on initiator graceful Logout "
               "when reset_on_logout=true; got count=="
            << factory->store->reset_call_count()
            << " (teardown not wired — T014 pending)";
    }

    // ── Acceptor arm ──
    {
        ioc.restart();
        captured_frames.clear();
        factory = std::make_shared<StoreDoubleFactory>();

        auto cfg = make_cfg(
            session_role::acceptor,
            /*reset_on_logon=*/false,
            reset_seqnum_policy::bilateral_lenient,
            /*reset_on_logout=*/true,
            /*reset_on_disconnect=*/false);
        Session sess(engine, cfg);

        // Drive acceptor to Active with clean seqnums (seq=1 from peer).
        drive_acceptor_to_active(sess, /*seq=*/1, /*reset_seqnum=*/false);
        ASSERT_EQ(sess.state(), fsm_state::Active)
            << "BothRoles acceptor: must reach Active";

        // Seed non-1 inbound AFTER reaching Active.
        sess.seqnum_mgr_test_access().set_counters_for_test(
            static_cast<seqnum_t>(7), static_cast<seqnum_t>(2));

        // Feed peer Logout at seq=7 (matching seeded next_inbound).
        // Sets logout_seen_=true, emits confirming Logout, fsm_state→Disconnected.
        // lifecycle state_ stays open — close() has NOT run yet.
        auto peer_logout = make_peer_logout(7);
        (void)feed_sync(sess, peer_logout);

        ASSERT_EQ(sess.state(), fsm_state::Disconnected);

        // Mirror production read-pump EOF: call close(terminal) to complete teardown.
        // close() checks lifecycle state_ (still open), enters the two-phase body,
        // and fires the teardown reset via (logout_seen_=true && reset_on_logout=true).
        // [contracts/reset-knobs.md C3.1; plan.md teardown design]
        terminal_close_sync(sess);

        EXPECT_EQ(factory->store->reset_call_count(), 1u)
            << "ResetOnLogout_BothRoles_Resets acceptor arm (C5.2): "
               "store_.reset() must fire on acceptor peer-initiated Logout "
               "when reset_on_logout=true (via close(terminal) mirroring read-pump EOF); "
               "got count==" << factory->store->reset_call_count();
    }
}

// ── Witness (6b): ResetOnDisconnect_BothRoles_Resets ─────────────────────────
//
// reset_on_disconnect=true fires for BOTH initiator AND acceptor roles.
//
// Both arms: close(terminal) with no Logout (abnormal drop) → reset fired.
//
// [C5.2]
//
// RED: no teardown reset wired → count==0 for both arms.
TEST_F(ResetOnLifecycleTest, ResetOnDisconnect_BothRoles_Resets) {
    // ── Initiator arm ──
    {
        ioc.restart();
        captured_frames.clear();
        factory = std::make_shared<StoreDoubleFactory>();

        auto cfg = make_cfg(
            session_role::initiator,
            /*reset_on_logon=*/false,
            reset_seqnum_policy::bilateral_lenient,
            /*reset_on_logout=*/false,
            /*reset_on_disconnect=*/true);
        Session sess(engine, cfg);

        // Drive to Active first.
        drive_initiator_to_active(sess);
        ASSERT_EQ(sess.state(), fsm_state::Active)
            << "BothRoles initiator: must reach Active";

        // Seed non-1 AFTER reaching Active.
        sess.seqnum_mgr_test_access().set_counters_for_test(
            static_cast<seqnum_t>(5), static_cast<seqnum_t>(3));

        terminal_close_sync(sess);

        ASSERT_EQ(sess.state(), fsm_state::Disconnected);

        EXPECT_EQ(factory->store->reset_call_count(), 1u)
            << "ResetOnDisconnect_BothRoles_Resets initiator arm RED (C5.2): "
               "store_.reset() must fire on initiator terminal close "
               "when reset_on_disconnect=true; got count=="
            << factory->store->reset_call_count()
            << " (teardown not wired — T014 pending)";
    }

    // ── Acceptor arm ──
    {
        ioc.restart();
        captured_frames.clear();
        factory = std::make_shared<StoreDoubleFactory>();

        auto cfg = make_cfg(
            session_role::acceptor,
            /*reset_on_logon=*/false,
            reset_seqnum_policy::bilateral_lenient,
            /*reset_on_logout=*/false,
            /*reset_on_disconnect=*/true);
        Session sess(engine, cfg);

        // Drive acceptor to Active with clean seqnums first.
        drive_acceptor_to_active(sess, /*seq=*/1, /*reset_seqnum=*/false);
        ASSERT_EQ(sess.state(), fsm_state::Active)
            << "BothRoles acceptor: must reach Active";

        // Seed non-1 AFTER reaching Active.
        sess.seqnum_mgr_test_access().set_counters_for_test(
            static_cast<seqnum_t>(8), static_cast<seqnum_t>(5));

        terminal_close_sync(sess);

        ASSERT_EQ(sess.state(), fsm_state::Disconnected);

        EXPECT_EQ(factory->store->reset_call_count(), 1u)
            << "ResetOnDisconnect_BothRoles_Resets acceptor arm RED (C5.2): "
               "store_.reset() must fire on acceptor terminal close "
               "when reset_on_disconnect=true; got count=="
            << factory->store->reset_call_count()
            << " (teardown not wired — T014 pending)";
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// T011 [US2] — 141-on-next-Logon witnesses (exercise T007 OR-of-three predicate)
// ═══════════════════════════════════════════════════════════════════════════════
//
// These witnesses verify that the OR-of-three predicate (`(reset_on_logon ||
// reset_on_logout || reset_on_disconnect) && seqnums=={1,1}`) fires through
// the US2 knobs. reset_on_logon=false; seqnums are seeded to {1,1} (simulating
// a prior teardown reset); the next initiator Logon must carry 141=Y.
//
// T007 (already wired): the predicate fires when ANY of the three knobs is set
// AND peek_outbound()==1 (==seqnum_min) AND next_inbound()==1.
//
// Expected: GREEN (T007 wired the OR-of-three predicate; these knobs are already
// in the predicate). If RED, the predicate check for reset_on_logout /
// reset_on_disconnect is missing — report it.
//
// [C2.1, data-model precedence rule 2]

// ── Witness (7): ResetOnLogout_NextInitiatorLogon_Emits141 ───────────────────
//
// reset_on_logout=true, reset_on_logon=false; seqnums at {1,1} (simulating a
// prior teardown reset); open as initiator → outbound Logon carries 141=Y.
//
// Expected: GREEN (T007 wired the predicate).
TEST_F(ResetOnLifecycleTest, ResetOnLogout_NextInitiatorLogon_Emits141) {
    auto cfg = make_cfg(
        session_role::initiator,
        /*reset_on_logon=*/false,
        reset_seqnum_policy::bilateral_lenient,
        /*reset_on_logout=*/true,
        /*reset_on_disconnect=*/false);
    Session sess(engine, cfg);

    // Seqnums are already {1,1} (fresh session, seqnum_min = 1).
    // Explicit confirmation:
    ASSERT_EQ(sess.seqnum_mgr_test_access().peek_outbound(), static_cast<seqnum_t>(1));
    ASSERT_EQ(sess.seqnum_mgr_test_access().next_inbound_unsafe(), static_cast<seqnum_t>(1));

    auto r = open_sync(sess);
    ASSERT_TRUE(r.has_value()) << "open() failed";

    // The OR-of-three predicate: (reset_on_logon=false OR reset_on_logout=true OR
    // reset_on_disconnect=false) && {1,1} → 141=Y must be emitted.
    ASSERT_FALSE(captured_frames.empty()) << "No outbound frame emitted";
    const auto& logon_frame = captured_frames.back();

    EXPECT_TRUE(frame_has_tag(logon_frame, 141))
        << "ResetOnLogout_NextInitiatorLogon_Emits141 (C2.1): "
           "141=Y must be present in the outbound Logon when reset_on_logout=true "
           "and seqnums=={1,1} (OR-of-three predicate, T007); "
           "if RED: the predicate does not include reset_on_logout — report to orchestrator";
}

// ── Witness (8): ResetOnDisconnect_NextInitiatorLogon_Emits141 ───────────────
//
// reset_on_disconnect=true, reset_on_logon=false; seqnums at {1,1};
// open as initiator → outbound Logon carries 141=Y.
//
// Expected: GREEN (T007 wired the predicate).
TEST_F(ResetOnLifecycleTest, ResetOnDisconnect_NextInitiatorLogon_Emits141) {
    auto cfg = make_cfg(
        session_role::initiator,
        /*reset_on_logon=*/false,
        reset_seqnum_policy::bilateral_lenient,
        /*reset_on_logout=*/false,
        /*reset_on_disconnect=*/true);
    Session sess(engine, cfg);

    // Seqnums are {1,1} (fresh session).
    ASSERT_EQ(sess.seqnum_mgr_test_access().peek_outbound(), static_cast<seqnum_t>(1));
    ASSERT_EQ(sess.seqnum_mgr_test_access().next_inbound_unsafe(), static_cast<seqnum_t>(1));

    auto r = open_sync(sess);
    ASSERT_TRUE(r.has_value()) << "open() failed";

    ASSERT_FALSE(captured_frames.empty()) << "No outbound frame emitted";
    const auto& logon_frame = captured_frames.back();

    EXPECT_TRUE(frame_has_tag(logon_frame, 141))
        << "ResetOnDisconnect_NextInitiatorLogon_Emits141 (C2.1): "
           "141=Y must be present in the outbound Logon when reset_on_disconnect=true "
           "and seqnums=={1,1} (OR-of-three predicate, T007); "
           "if RED: the predicate does not include reset_on_disconnect — report to orchestrator";
}

// ═══════════════════════════════════════════════════════════════════════════════
// T015 — No-heap witness: reset_seqnums_to_one_durable() on open() and close()
// ═══════════════════════════════════════════════════════════════════════════════
//
// ResetKnobs_NoHeapOnResetPath
//
// Asserts zero global-heap allocation across the reset_seqnums_to_one_durable()
// call sites — the open() (reset_on_logon=true, initiator) and close() (reset_on_disconnect=true)
// lifecycle boundary reset paths.
//
// NOTE: The reset fires at lifecycle boundaries, NOT the per-message hot path.
// This is a NO-REGRESSION GUARD (not a §VIII.5-MUST hot-path cell per plan.md §VIII.5):
// the reset runs at most once per session lifecycle event (logon/logout/disconnect).
//
// Gate: mallocnesia LD_PRELOAD interceptor (global-malloc interception).
//   Run: LD_PRELOAD=$(pwd)/tools/mallocnesia/libmallocnesia.so \
//        MALLOCNESIA_MAX_ALLOCS=0 \
//        build/linux-clang-debug/bin/session_reset_on_lifecycle \
//        --gtest_filter="*NoHeapOnResetPath*"
//
// Why mallocnesia is the binding gate (not counting_resource alone):
// [[feedback_tracking_pmr_resource_false_pass]]: a counting_resource PMR only
// counts allocs routed through it; a non-PMR std::vector/std::string (or asio
// coroutine frame) escapes via global operator new and the PMR gate silently passes.
//
// Methodology:
//   1. Warm-up N cycles to prime asio's internal coroutine-frame cache and the
//      async_mutex slot pool (so the guard window catches ONLY unexpected per-call
//      allocations, not one-time startup/asio-pool growth).
//   2. Measure TWO separate guarded windows:
//      Window A: open() with reset_on_logon=true (fires reset_seqnums_to_one_durable(fatal)).
//      Window B: close() with reset_on_disconnect=true (fires teardown reset(logged)).
//   Each window uses its own session so the measured path is isolated.
//   The StoreDouble::reset() uses vector::clear() (no dealloc/realloc after warmup).
//   SeqnumManager::reset_to_one() touches only atomic counters under the async_mutex.
//
// [plan.md §VIII.5; contracts/reset-knobs.md C2.2/C2.6/C3.1/C4.1; const §VIII.5]
TEST_F(ResetOnLifecycleTest, ResetKnobs_NoHeapOnResetPath) {
    // ── Warm-up phase: prime asio coroutine cache + async_mutex slot pool ──
    //
    // Run kWarmup open/close cycles before the measured windows. The first few
    // cycles may allocate asio-internal state (coroutine promise frames, timer
    // service entries); after warm-up these are recycled from internal pools.
    constexpr int kWarmup = 6;
    for (int i = 0; i < kWarmup; ++i) {
        ioc.restart();
        captured_frames.clear();
        factory = std::make_shared<StoreDoubleFactory>();

        auto cfg = make_cfg(
            session_role::initiator,
            /*reset_on_logon=*/false,
            reset_seqnum_policy::bilateral_lenient,
            /*reset_on_logout=*/false,
            /*reset_on_disconnect=*/true);
        Session sess(engine, cfg);

        // Drive to Active then terminal close (fires teardown reset via disconnect).
        drive_initiator_to_active(sess);
        terminal_close_sync(sess);
    }

    // ── Measured window A: open() reset (reset_on_logon=true) ────────────────
    //
    // Covers:
    //   (a) initiator open() with reset_on_logon=true → reset_seqnums_to_one_durable(fatal).
    //
    // alloc_guard_start() marks the start of the intercepted window.
    // alloc_guard_end()   marks the end — mallocnesia exits 1 if any malloc/calloc/realloc
    //                     was intercepted and the count exceeds MALLOCNESIA_MAX_ALLOCS=0.
    {
        ioc.restart();
        captured_frames.clear();
        factory = std::make_shared<StoreDoubleFactory>();

        auto cfg_a = make_cfg(
            session_role::initiator,
            /*reset_on_logon=*/true,
            reset_seqnum_policy::bilateral_lenient,
            /*reset_on_logout=*/false,
            /*reset_on_disconnect=*/false);
        Session sess_a(engine, cfg_a);

        alloc_guard_start();
        {
            auto r = open_sync(sess_a);
            ASSERT_TRUE(r.has_value())
                << "ResetKnobs_NoHeapOnResetPath [window A]: open() must succeed "
                   "(reset_on_logon path)";
        }
        alloc_guard_end();

        ASSERT_EQ(factory->store->reset_call_count(), 1u)
            << "ResetKnobs_NoHeapOnResetPath [window A]: reset must have fired on "
               "open() (reset_on_logon=true); count must be 1";

        // Tear down outside the guard (no teardown reset — reset_on_disconnect=false).
        terminal_close_sync(sess_a);
    }

    // ── Measured window B: close() teardown reset (reset_on_disconnect=true) ──
    //
    // Covers:
    //   (b) close(terminal) with reset_on_disconnect=true → reset_seqnums_to_one_durable(logged).
    //
    // The open() reset for this session runs outside the guard (structural prerequisite only).
    {
        ioc.restart();
        captured_frames.clear();
        factory = std::make_shared<StoreDoubleFactory>();

        auto cfg_b = make_cfg(
            session_role::initiator,
            /*reset_on_logon=*/true,
            reset_seqnum_policy::bilateral_lenient,
            /*reset_on_logout=*/false,
            /*reset_on_disconnect=*/true);
        Session sess_b(engine, cfg_b);

        // Structural prerequisite: open() succeeds (reset_on_logon=true fires the logon reset).
        {
            auto r = open_sync(sess_b);
            ASSERT_TRUE(r.has_value())
                << "ResetKnobs_NoHeapOnResetPath [window B]: open() must succeed";
            ASSERT_EQ(factory->store->reset_call_count(), 1u)
                << "ResetKnobs_NoHeapOnResetPath [window B]: logon reset must have fired";
        }

        // Feed a peer Logon-ack to reach Active (required before close()).
        auto logon_ack = make_peer_logon(1, /*reset_seqnum=*/false);
        {
            auto r2 = feed_sync(sess_b, logon_ack);
            ASSERT_TRUE(r2.has_value())
                << "ResetKnobs_NoHeapOnResetPath [window B]: Logon-ack feed must succeed";
            ASSERT_EQ(sess_b.state(), fsm_state::Active)
                << "ResetKnobs_NoHeapOnResetPath [window B]: session must reach Active";
        }

        // alloc_guard window B: terminal close triggers the teardown reset.
        // The teardown reset path: reset_seqnums_to_one_durable(logged) which calls
        // SeqnumManager::reset_to_one() [atomic counter update + async_mutex]
        // then StoreDouble::reset() [vector::clear() — no-alloc after warmup].
        alloc_guard_start();
        {
            terminal_close_sync(sess_b);
        }
        alloc_guard_end();

        ASSERT_EQ(sess_b.state(), fsm_state::Disconnected)
            << "ResetKnobs_NoHeapOnResetPath [window B]: session must reach Disconnected";

        // Verify the teardown reset fired (reset_on_disconnect=true; teardown_reset_done_ guard).
        // Total resets: 1 from open() + 1 from close() = 2.
        EXPECT_EQ(factory->store->reset_call_count(), 2u)
            << "ResetKnobs_NoHeapOnResetPath [window B]: exactly 2 store resets expected "
               "(1 from open/reset_on_logon + 1 from close/reset_on_disconnect); "
               "got " << factory->store->reset_call_count();

        // Seqnums reset to {1,1} after teardown.
        EXPECT_EQ(sess_b.seqnum_mgr_test_access().peek_outbound(), static_cast<seqnum_t>(1))
            << "ResetKnobs_NoHeapOnResetPath [window B]: peek_outbound must be 1 after teardown";
        EXPECT_EQ(sess_b.seqnum_mgr_test_access().next_inbound_unsafe(), static_cast<seqnum_t>(1))
            << "ResetKnobs_NoHeapOnResetPath [window B]: next_inbound must be 1 after teardown";
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// 030 US1 — Acceptor received-141 inbound-advance witnesses (T004–T009)
// ═══════════════════════════════════════════════════════════════════════════════
//
// Bug: on the acceptor received-141 path (peer sends Logon(34=1,141=Y),
// reset_on_logon knob OFF), check_inbound advances next-expected-inbound 1→2,
// then reset_seqnums_to_one_durable() rewinds both counters back to 1, clobbering
// the advance. Result: next-expected-inbound=1 instead of 2 → spurious
// ResendRequest on peer seq-2 + reply Logon advertises 789=1 not 2 (027-on).
//
// Fix: after the durable reset, restore next-expected-inbound to seqnum_min+1
// (=2) in BOTH the in-memory manager (set_next_inbound) AND the durable store
// (one persist_inbound_advance_() write-through) → store==manager==2.
// Guarded on logon_inbound_advanced (the reset Logon was consumed in-sequence).
//
// Anchors: specs/030-received-reset-inbound-advance/plan.md FR-001/005/007/010;
//          029 INV-H1 (store ≤ manager); 027 I-NEX-1/E-OBO.
//
// RED reason for T004–T008: the production fix is NOT yet applied; the guard at
// §A is absent → next_inbound stays 1 after reset.

// ── Witness T004: ResetOnLogon_Off_Received141_NextInboundIsTwo ───────────────
//
// Acceptor, knob off, feed Logon(34=1,141=Y): after the received-141 reset,
// next_inbound_unsafe() must be 2 (consumed the seq-1 reset Logon).
//
// RED today: 1 (reset clobbers the check_inbound advance; guard absent).
// [FR-001, FR-005]
TEST_F(ResetOnLifecycleTest, ResetOnLogon_Off_Received141_NextInboundIsTwo) {
    auto cfg = make_cfg(session_role::acceptor, /*reset_on_logon=*/false,
                        reset_seqnum_policy::bilateral_lenient);
    Session sess(engine, cfg);

    auto r = open_sync(sess);
    ASSERT_TRUE(r.has_value()) << "open() failed";

    auto logon = make_peer_logon(/*seq=*/1, /*reset_seqnum=*/true);
    auto r2 = feed_sync(sess, logon);
    ASSERT_TRUE(r2.has_value()) << "Logon(141=Y) feed must succeed";
    ASSERT_EQ(sess.state(), fsm_state::Active) << "session must reach Active";

    const seqnum_t next_in = sess.seqnum_mgr_test_access().next_inbound_unsafe();
    EXPECT_EQ(next_in, static_cast<seqnum_t>(2))
        << "T004 (FR-001/005) RED: next_inbound must be 2 after acceptor received-141 "
           "(consumed seq-1 reset Logon); got " << next_in
        << " (expected 2; fix not yet applied — reset clobbers the advance)";
}

// ── Witness T005: Received141_PeerNextMsgSeq2_HarmCheck ──────────────────────
//
// After the received-141 reset, feed a peer Heartbeat at 34=2: must NOT trigger
// a ResendRequest (35=2) and next_inbound must advance to 3.
//
// RED today: next_inbound=1 after bug → seq-2 Heartbeat is too-high → ResendRequest
// emitted (spurious gap). [FR-002]
TEST_F(ResetOnLifecycleTest, Received141_PeerNextMsgSeq2_HarmCheck) {
    auto cfg = make_cfg(session_role::acceptor, /*reset_on_logon=*/false,
                        reset_seqnum_policy::bilateral_lenient);
    Session sess(engine, cfg);

    auto r = open_sync(sess);
    ASSERT_TRUE(r.has_value()) << "open() failed";

    // Drive to Active with received-141 Logon at seq=1.
    auto logon = make_peer_logon(/*seq=*/1, /*reset_seqnum=*/true);
    auto r2 = feed_sync(sess, logon);
    ASSERT_TRUE(r2.has_value()) << "Logon(141=Y) feed must succeed";
    ASSERT_EQ(sess.state(), fsm_state::Active) << "session must reach Active";

    // Clear outbound frames captured during Logon exchange.
    captured_frames.clear();

    // Build a peer Heartbeat (35=0) at seq=2.
    static const auto make_peer_hb = [](std::uint32_t seq) {
        std::string body;
        body += "35=0\x01";
        body += "34=" + std::to_string(seq) + "\x01";
        body += "49=TW\x01";
        body += "52=20240101-00:00:00.000\x01";
        body += "56=ISLD\x01";
        return make_fix44_frame(body);
    };
    auto hb = make_peer_hb(2);
    auto r3 = feed_sync(sess, hb);
    ASSERT_TRUE(r3.has_value()) << "Heartbeat feed must succeed";

    // T005a: no ResendRequest (35=2) emitted.
    EXPECT_FALSE(any_resend_request())
        << "T005 (FR-002) RED: spurious ResendRequest(35=2) emitted for peer seq=2 "
           "Heartbeat; indicates next_inbound was 1 (not 2) after received-141 reset "
           "(fix not yet applied → seq-2 looks too-high → gap detected)";

    // T005b: next_inbound advanced to 3 (consumed the seq-2 Heartbeat).
    const seqnum_t next_in = sess.seqnum_mgr_test_access().next_inbound_unsafe();
    EXPECT_EQ(next_in, static_cast<seqnum_t>(3))
        << "T005 (FR-002) RED: next_inbound must be 3 after consuming seq-2 Heartbeat; "
           "got " << next_in << " (indicates next_inbound was wrong before the Heartbeat)";
}

// ── Witness T006: Received141_AcceptorDiscriminatingTriple ────────────────────
//
// 027-ON. After received-141, assert the discriminating triple:
//   (a) next_inbound_unsafe() == 2
//   (b) reply Logon MsgSeqNum(34) == 1  (outbound counter unchanged)
//   (c) reply Logon NextExpectedMsgSeqNum(789) == 2
//
// RED today: (a) and (c) both fail with value 1; (b) passes already.
// [FR-001, FR-005, 027 I-NEX-1/E-OBO]
TEST_F(ResetOnLifecycleTest, Received141_AcceptorDiscriminatingTriple) {
    auto cfg = make_cfg(session_role::acceptor, /*reset_on_logon=*/false,
                        reset_seqnum_policy::bilateral_lenient);
    cfg.enable_next_expected_msg_seq_num = true;
    Session sess(engine, cfg);

    auto r = open_sync(sess);
    ASSERT_TRUE(r.has_value()) << "open() failed";

    // Clear any frames from open() (acceptor emits nothing until Logon arrives).
    captured_frames.clear();

    auto logon = make_peer_logon(/*seq=*/1, /*reset_seqnum=*/true);
    auto r2 = feed_sync(sess, logon);
    ASSERT_TRUE(r2.has_value()) << "Logon(141=Y) feed must succeed";
    ASSERT_EQ(sess.state(), fsm_state::Active) << "session must reach Active";

    // Find the reply Logon (35=A) in captured_frames — scan for MsgType==A.
    const std::vector<std::byte>* reply_frame = nullptr;
    for (const auto& f : captured_frames) {
        if (extract_field(std::span<const std::byte>(f), 35) == "A") {
            reply_frame = &f;
            break;
        }
    }
    ASSERT_NE(reply_frame, nullptr) << "T006: acceptor must emit a reply Logon (35=A)";

    // (a) next_inbound_unsafe() == 2.
    const seqnum_t next_in = sess.seqnum_mgr_test_access().next_inbound_unsafe();
    EXPECT_EQ(next_in, static_cast<seqnum_t>(2))
        << "T006 (a) (FR-001/005) RED: next_inbound must be 2 after received-141; "
           "got " << next_in << " (fix not yet applied)";

    // (b) reply MsgSeqNum(34) == 1 (outbound counter starts at 1, unchanged).
    auto tag34 = extract_field(std::span<const std::byte>(*reply_frame), 34);
    ASSERT_TRUE(tag34.has_value()) << "T006: reply Logon must carry MsgSeqNum(34)";
    EXPECT_EQ(std::string(*tag34), "1")
        << "T006 (b) (outbound byte-identical): reply MsgSeqNum must be 1; got " << *tag34;

    // (c) reply NextExpectedMsgSeqNum(789) == 2.
    auto tag789 = extract_field(std::span<const std::byte>(*reply_frame), 789);
    ASSERT_TRUE(tag789.has_value()) << "T006: reply Logon must carry tag 789 (027-on)";
    EXPECT_EQ(std::string(*tag789), "2")
        << "T006 (c) (027 I-NEX-1/E-OBO) RED: reply 789 must be 2 (next_inbound after reset+advance); "
           "got " << *tag789 << " (fix not yet applied → 789 reads 1)";
}

// ── Witness T007: Received141_PersistentStore_InvH1_StoreEqualsManagerTwo ─────
//
// Persistent store (default factory). After received-141, assert DIRECTLY ON THE
// STORE: store.current_next_inbound() == 2 AND == sess.next_inbound_unsafe().
// (INV-H1: store ≤ manager; here equality = 2.)
//
// RED today: store.current_next_inbound() == 1 (store never got the write-through;
// the reset left store=1 and no persist-to-2 ran). [029 INV-H1, FR-005]
TEST_F(ResetOnLifecycleTest, Received141_PersistentStore_InvH1_StoreEqualsManagerTwo) {
    // factory is the default StoreDoubleFactory (yields_persistent_store()==true).
    auto cfg = make_cfg(session_role::acceptor, /*reset_on_logon=*/false,
                        reset_seqnum_policy::bilateral_lenient);
    Session sess(engine, cfg);

    auto r = open_sync(sess);
    ASSERT_TRUE(r.has_value()) << "open() failed";

    auto logon = make_peer_logon(/*seq=*/1, /*reset_seqnum=*/true);
    auto r2 = feed_sync(sess, logon);
    ASSERT_TRUE(r2.has_value()) << "Logon(141=Y) feed must succeed";
    ASSERT_EQ(sess.state(), fsm_state::Active) << "session must reach Active";

    const seqnum_t store_in = factory->store->current_next_inbound();
    const seqnum_t mgr_in   = sess.seqnum_mgr_test_access().next_inbound_unsafe();

    // Assert the store value directly (not via the manager as proxy).
    EXPECT_EQ(store_in, static_cast<seqnum_t>(2u))
        << "T007 (029 INV-H1 / FR-005) RED: store.current_next_inbound() must be 2 "
           "after received-141 persist-to-2; got " << store_in
        << " (fix not yet applied → store stuck at 1 after reset)";

    // INV-H1 equality: store == manager (both 2 after fix; both 1 under bug).
    EXPECT_EQ(store_in, mgr_in)
        << "T007 (INV-H1): store.current_next_inbound() must equal "
           "seqnum_mgr_.next_inbound_unsafe(); store=" << store_in << " mgr=" << mgr_in;
}

// ── Witness T008: Received141_PersistentStore_ResetFailure_Disconnects_NoOverPersist
//
// Persistent store; fail_next_reset() BEFORE feeding Logon(141=Y).
// Under FR-010: fatal-when-persistent → reset failure → Disconnected.
// Under the BUG (logged): failure swallowed → Active, persist-to-2 runs → store=2.
//
// Asserts (i) Disconnected and (ii) store.current_next_inbound() == 1 (unchanged).
//
// RED today on (i): session reaches Active (not Disconnected) because the logged
// disposition swallows the failure. (ii) may also be violated if persist-to-2 ran.
// [FR-010, 029 INV-H1]
TEST_F(ResetOnLifecycleTest, Received141_PersistentStore_ResetFailure_Disconnects_NoOverPersist) {
    // factory is the default StoreDoubleFactory (yields_persistent_store()==true).
    auto cfg = make_cfg(session_role::acceptor, /*reset_on_logon=*/false,
                        reset_seqnum_policy::bilateral_lenient);
    Session sess(engine, cfg);

    // Inject a store reset failure BEFORE the Logon arrives.
    factory->store->fail_next_reset();

    auto r = open_sync(sess);
    ASSERT_TRUE(r.has_value()) << "open() failed";

    auto logon = make_peer_logon(/*seq=*/1, /*reset_seqnum=*/true);
    auto r2 = feed_sync(sess, logon);

    // (i) Session must be Disconnected (fatal-when-persistent FR-010).
    // RED today: session reaches Active (logged disposition swallows the failure).
    EXPECT_EQ(sess.state(), fsm_state::Disconnected)
        << "T008 (FR-010) RED: persistent store reset failure must Disconnect the session "
           "(fatal-when-persistent); got Active (fix not yet applied → logged swallows failure)";

    // (ii) Persist-to-2 must NOT have run: store counter stays at 1 (pre-reset value).
    // CRITICAL: must assert store == 1, NOT store <= manager.
    // Under the bug (logged): swallowed failure → restore runs → store becomes 2 == manager 2,
    // so store <= manager would FALSELY PASS. Only "store unchanged at 1" discriminates.
    const seqnum_t store_in = factory->store->current_next_inbound();
    EXPECT_EQ(store_in, static_cast<seqnum_t>(1u))
        << "T008 (029 INV-H1 / FR-010) RED: store.current_next_inbound() must stay 1 "
           "(persist-to-2 must not run after failed reset); got " << store_in;

    // Also: the reset was attempted exactly once (then short-circuited).
    EXPECT_EQ(factory->store->reset_call_count(), 1u)
        << "T008: store.reset() must have been attempted exactly once; "
           "got " << factory->store->reset_call_count();
}

// ── Witness T009: Received141_GuardSkipsWhenNoConsumedReset ──────────────────
//
// Guard-correctness witness: received-141 path where logon_inbound_advanced=false
// (too-high reset Logon tolerated behind-side under 027-on, check_inbound did NOT
// advance) → the restore+persist block is guarded on logon_inbound_advanced → does
// NOT fire → next_inbound stays at seqnum_min (1), NOT forced to 2.
//
// Construction: acceptor + enable_next_expected_msg_seq_num=true (enables behind-side
// tolerance); peer sends Logon(34=5, 141=Y) — seq=5 is too-high, hydration withholds
// the inbound seed (next_inbound=1), check_inbound(5) is too-high → tolerated
// (behind-side, 027-on) → logon_inbound_advanced=false → reset fires (peer_sent_reset,
// !reset_on_logon) → restore-to-2 guard skips → next_inbound remains 1.
//
// This is GREEN both before AND after the hunk (the `if (logon_inbound_advanced)` guard
// is correct in both states; the test is a guard-correctness/mutation witness).
// [FR-001, 027 I-NEX-5; 030 guard on logon_inbound_advanced]
TEST_F(ResetOnLifecycleTest, Received141_GuardSkipsWhenNoConsumedReset) {
    auto cfg = make_cfg(session_role::acceptor, /*reset_on_logon=*/false,
                        reset_seqnum_policy::bilateral_lenient);
    cfg.enable_next_expected_msg_seq_num = true;
    Session sess(engine, cfg);

    auto r = open_sync(sess);
    ASSERT_TRUE(r.has_value()) << "open() failed";

    // Peer sends Logon(34=5, 141=Y): too-high (next_inbound=1, seq=5 → behind-side
    // tolerated under 027-on) → check_inbound did NOT advance → logon_inbound_advanced=false.
    static const auto make_peer_logon_with_seq = [](std::uint32_t seq, bool rst) {
        std::string body;
        body += "35=A\x01";
        body += "34=" + std::to_string(seq) + "\x01";
        body += "49=TW\x01";
        body += "52=20240101-00:00:00.000\x01";
        body += "56=ISLD\x01";
        body += "98=0\x01";
        body += "108=30\x01";
        if (rst) body += "141=Y\x01";
        return make_fix44_frame(body);
    };
    auto logon_toohi = make_peer_logon_with_seq(/*seq=*/5, /*rst=*/true);
    auto r2 = feed_sync(sess, logon_toohi);
    // Behind-side tolerated → reaches Active.
    ASSERT_TRUE(r2.has_value()) << "behind-side tolerated Logon feed must succeed";
    ASSERT_EQ(sess.state(), fsm_state::Active)
        << "T009: behind-side tolerated Logon(34=5,141=Y) must reach Active (027-on)";

    // Guard skipped: next_inbound must still be 1 (NOT set to 2 by the restore).
    // A hypothetical unguarded restore would force it to seqnum_min+1 = 2.
    const seqnum_t next_in = sess.seqnum_mgr_test_access().next_inbound_unsafe();
    EXPECT_EQ(next_in, static_cast<seqnum_t>(1))
        << "T009 (guard on logon_inbound_advanced): next_inbound must remain 1 "
           "when behind-side tolerated (not consumed in-sequence); got " << next_in
           << " (if 2: restore fired without logon_inbound_advanced — guard missing)";
}

// ═══════════════════════════════════════════════════════════════════════════════
// T012-T014 [US2] — Initiator received-141 arm (peer_ack_sent_reset_flag)
// ═══════════════════════════════════════════════════════════════════════════════
//
// Symmetric twin of the acceptor witnesses (T004-T008) on the SEPARATE, reachable
// initiator Logon-ack reset arm (FR-009). Identical clobber: the peer's reset-ack
// Logon at seq=1 advances next_inbound 1->2 (logon_inbound_advanced_init), then the
// reset rebases to 1; 030 restores to 2. No reply Logon on this arm → no 789 clause.

// ── Witness T012: Initiator_Received141Ack_NextInboundTwo_NoResend ─────────────
//
// Initiator, knob off, bilateral_lenient. open() → LogonSent; peer Logon-ack at
// seq=1 carrying 141=Y → consumed → next_inbound must be 2; then a peer seq-2
// message is accepted with NO ResendRequest. [FR-009, FR-001, FR-002]
// RED today: initiator arm not yet fixed → next_inbound=1 → spurious ResendRequest.
TEST_F(ResetOnLifecycleTest, Initiator_Received141Ack_NextInboundTwo_NoResend) {
    auto cfg = make_cfg(session_role::initiator, /*reset_on_logon=*/false,
                        reset_seqnum_policy::bilateral_lenient);
    Session sess(engine, cfg);

    auto r = open_sync(sess);
    ASSERT_TRUE(r.has_value()) << "open() failed";
    ASSERT_EQ(sess.state(), fsm_state::LogonSent) << "initiator must be LogonSent after open()";

    // Peer Logon-ack at seq=1 with 141=Y (the peer_ack_sent_reset_flag arm).
    auto ack = make_peer_logon(/*seq=*/1, /*reset_seqnum=*/true);
    auto r2 = feed_sync(sess, ack);
    ASSERT_TRUE(r2.has_value()) << "Logon-ack(141=Y) feed must succeed";
    ASSERT_EQ(sess.state(), fsm_state::Active) << "session must reach Active";

    // FR-001/009: next_inbound must be 2 (consumed seq-1 reset-ack Logon).
    EXPECT_EQ(sess.seqnum_mgr_test_access().next_inbound_unsafe(), static_cast<seqnum_t>(2))
        << "T012 (FR-009/001) RED: initiator next_inbound must be 2 after received-141 "
           "Logon-ack; got " << sess.seqnum_mgr_test_access().next_inbound_unsafe()
        << " (initiator arm fix not yet applied → reset clobbers the advance)";

    // FR-002: peer's next message at seq=2 accepted in-sequence, no ResendRequest.
    captured_frames.clear();
    static const auto make_peer_hb = [](std::uint32_t seq) {
        std::string body;
        body += "35=0\x01";
        body += "34=" + std::to_string(seq) + "\x01";
        body += "49=TW\x01";
        body += "52=20240101-00:00:00.000\x01";
        body += "56=ISLD\x01";
        return make_fix44_frame(body);
    };
    auto r3 = feed_sync(sess, make_peer_hb(2));
    ASSERT_TRUE(r3.has_value()) << "seq-2 Heartbeat feed must succeed";
    EXPECT_FALSE(any_resend_request())
        << "T012 (FR-002) RED: spurious ResendRequest(35=2) for peer seq=2 after initiator "
           "received-141 reset (indicates next_inbound was 1 not 2)";
    EXPECT_EQ(sess.seqnum_mgr_test_access().next_inbound_unsafe(), static_cast<seqnum_t>(3))
        << "T012 (FR-002): next_inbound must be 3 after consuming seq-2 Heartbeat";
}

// ── Witness T013: Initiator_Received141Ack_PersistentStore_StoreEqualsManagerTwo ─
//
// Persistent store. After the initiator received-141 reset, assert DIRECTLY ON THE
// STORE: store.current_next_inbound() == 2 AND == manager (INV-H1 equality). [FR-005/009]
// RED today: store stuck at 1 (no initiator persist-to-2).
TEST_F(ResetOnLifecycleTest, Initiator_Received141Ack_PersistentStore_StoreEqualsManagerTwo) {
    auto cfg = make_cfg(session_role::initiator, /*reset_on_logon=*/false,
                        reset_seqnum_policy::bilateral_lenient);
    Session sess(engine, cfg);

    auto r = open_sync(sess);
    ASSERT_TRUE(r.has_value()) << "open() failed";
    ASSERT_EQ(sess.state(), fsm_state::LogonSent);

    auto ack = make_peer_logon(/*seq=*/1, /*reset_seqnum=*/true);
    auto r2 = feed_sync(sess, ack);
    ASSERT_TRUE(r2.has_value());
    ASSERT_EQ(sess.state(), fsm_state::Active);

    const seqnum_t store_in = factory->store->current_next_inbound();
    const seqnum_t mgr_in   = sess.seqnum_mgr_test_access().next_inbound_unsafe();
    EXPECT_EQ(store_in, static_cast<seqnum_t>(2u))
        << "T013 (FR-005/009) RED: initiator store.current_next_inbound() must be 2 "
           "after received-141 persist-to-2; got " << store_in;
    EXPECT_EQ(store_in, mgr_in)
        << "T013 (INV-H1): store must equal manager; store=" << store_in << " mgr=" << mgr_in;
}

// ── Witness T014: Initiator_Received141Ack_PersistentStore_ResetFailure_Disconnects ─
//
// Persistent store; fail_next_reset() before the ack. Under FR-010 (fatal-when-
// persistent) the reset failure → Disconnected; persist-to-2 NOT reached → store
// counter UNCHANGED at 1 (NOT advanced to 2). Symmetric to acceptor T008. [FR-010/009]
// ⚠ assertion (ii) asserts store == 1 (unchanged), NOT store <= manager (which would
// false-pass under the bug: swallowed failure → restore runs → store==2==manager).
TEST_F(ResetOnLifecycleTest, Initiator_Received141Ack_PersistentStore_ResetFailure_Disconnects) {
    auto cfg = make_cfg(session_role::initiator, /*reset_on_logon=*/false,
                        reset_seqnum_policy::bilateral_lenient);
    Session sess(engine, cfg);

    factory->store->fail_next_reset();

    auto r = open_sync(sess);
    ASSERT_TRUE(r.has_value()) << "open() failed";
    ASSERT_EQ(sess.state(), fsm_state::LogonSent);

    auto ack = make_peer_logon(/*seq=*/1, /*reset_seqnum=*/true);
    auto r2 = feed_sync(sess, ack);

    // (i) FR-010: persistent reset failure → fatal → Disconnected + error propagated.
    EXPECT_FALSE(r2.has_value())
        << "T014 (FR-010) RED: initiator persistent reset failure must propagate the error";
    EXPECT_EQ(sess.state(), fsm_state::Disconnected)
        << "T014 (FR-010) RED: initiator persistent received-141 reset failure must Disconnect";

    // (ii) persist-to-2 NOT reached: store counter unchanged at 1 (pre-reset value).
    const seqnum_t store_in = factory->store->current_next_inbound();
    EXPECT_EQ(store_in, static_cast<seqnum_t>(1u))
        << "T014 (INV-H1/FR-010) RED: store must stay 1 (persist-to-2 must not run after a "
           "failed reset); got " << store_in;
    EXPECT_EQ(factory->store->reset_call_count(), 1u)
        << "T014: store.reset() must have been attempted exactly once";
}

}  // namespace fixpp::session::test
