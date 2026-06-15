// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_acceptor_logon_sending_time.cpp
//
// 038-acceptor-sendingtime-guard (S-033/§4.5.4 amends S-019) — Group 1:
// Acceptor first-Logon SendingTime(52) MaxLatency guard witness cells.
//
// 14 cells from quickstart.md §Group-1:
//   1. stale-past  — Reject(371=52,373=10) + NO Logout + Disconnected
//   2. stale-future — same
//   3. malformed-52 — same (parse failure → reason=10)
//   4. absent-52   — same (no tag 52; reason=10 per Clarifications)
//   5. empty-52    — same (52= present but empty)
//   6. toAdmin-throws on new Reject → app_callback_threw + Disconnected
//   7. assign_outbound failure → fail-closed + Disconnected
//   8. GENUINE build_reject overflow (500-char CompID) → fail-closed, NO frame
//   9. persistent-store inbound seeded-not-advanced (INV-4)
//  10. stale-52 + extra 1137 field → rejects on 52 (genuine 52-vs-1137-GATE
//      ordering is witnessed in the FIXT suite / US3, where the gate is reachable)
//  11. bad-52 + bad-credentials → 52 wins (credential reject never reached)
//  12. bad-52 + too-high inbound seqnum → 52 wins (check_inbound never reached)
//  13. conforming → Active (no regression)
//  14. persistent-reconnect durable outbound N>1 → Reject carries 34=N
//
// Anchors: spec.md FR-002/FR-003/FR-004/FR-005; contracts/acceptor-logon-sendingtime.md C-1;
//          data-model.md INV-4; quickstart.md §Group-1; tasks.md T001-T008.
// [[feedback_fixed_buffer_build_failure_silent_success]]: buffer ≥512B
// [[feedback_admin_emit_bypasses_fire_to_admin]]: fire_to_admin_ on Reject
// [[feedback_witness_asserts_named_postcondition_not_proxy]]: direct post-cond asserts

#ifndef FIXPP_TEST_HOOKS
#error "test_acceptor_logon_sending_time.cpp requires FIXPP_TEST_HOOKS"
#endif

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/application.hpp>
#include <fixpp/session/compid_authorization_policy.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/message_store.hpp>
#include <fixpp/session/message_store_factory.hpp>
#include <fixpp/session/retrieve_visitor.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "session/support/frame_field_extract.hpp"  // via -I tests/
#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"

using namespace std::chrono_literals;
using fixpp::session::test_support::extract_field;
using fixpp::session::seqnum_t;
using fixpp::session::seqnum_min;
using fixpp::session::seqnum_max;
using fixpp::session::direction_t;
using fixpp::session::MessageStore;
using fixpp::session::MessageStoreFactory;
using fixpp::session::retrieve_visitor;
using fixpp::session::visit_result;
using fixpp::session::Application;

namespace fixpp::session::test {

// ── Constants ─────────────────────────────────────────────────────────────────

// Mock clock "now": 2024-01-01 00:00:00.000 UTC (unix epoch seconds = 1704067200)
static constexpr std::int64_t kNowSec = 1704067200;

// A valid sending time matching the mock clock exactly.
static constexpr std::string_view kValidSendingTime = "20240101-00:00:00.000";

// A sending time 10 minutes (600 s) in the past — stale (default threshold 120 s).
static constexpr std::string_view kStalePast = "20231231-23:50:00.000";

// A sending time 10 minutes in the future — also stale.
static constexpr std::string_view kStaleFuture = "20240101-00:10:00.000";

// A malformed sending time (not a parseable FIX timestamp).
static constexpr std::string_view kMalformed = "NOTATIME";

// ── Application doubles ────────────────────────────────────────────────────────

// CountingApp: counts toAdmin calls (no throw).
struct CountingApp : Application {
    int to_admin_calls{0};

    void toAdmin(const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& /*msg*/,
                 const SessionId& /*id*/) override {
        ++to_admin_calls;
    }
};

// ThrowOnRejectApp: throws in toAdmin when it sees a Reject(35=3).
// Used for Cell 6 to confirm app_callback_threw.
// The throw fires only on the first Reject frame to avoid infinite loops.
struct ThrowOnRejectApp : Application {
    int to_admin_calls{0};

    void toAdmin(const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& msg,
                 const SessionId& /*id*/) override {
        ++to_admin_calls;
        // Check 35= field value via raw span from the parsed message.
        // msg.raw() gives the original span.
        auto mt = extract_field(msg.bytes(), 35);
        if (mt && *mt == "3") {
            throw std::runtime_error("simulated toAdmin throw on Reject");
        }
    }
};

// ── Frame builders ─────────────────────────────────────────────────────────────

static std::vector<std::byte> build_frame(const std::string& body_str,
                                           std::string_view begin_string = "FIX.4.4") {
    std::string hdr;
    hdr += "8=" + std::string(begin_string) + "\x01";
    hdr += "9=" + std::to_string(body_str.size()) + "\x01";

    std::string full = hdr + body_str;
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

// Build a peer Logon (35=A) from TW→ISLD with the given sending_time value.
static std::vector<std::byte> make_logon_with_time(std::string_view sending_time,
                                                    std::uint32_t seq = 1,
                                                    std::string_view extra = {}) {
    std::string body;
    body += "35=A\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=TW\x01";
    body += "52=" + std::string(sending_time) + "\x01";
    body += "56=ISLD\x01";
    body += "98=0\x01";
    body += "108=30\x01";
    if (!extra.empty()) body += std::string(extra);
    return build_frame(body);
}

// Build a peer Logon with NO tag 52 field at all.
static std::vector<std::byte> make_logon_absent_sending_time(std::uint32_t seq = 1) {
    std::string body;
    body += "35=A\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=TW\x01";
    // 52 deliberately omitted
    body += "56=ISLD\x01";
    body += "98=0\x01";
    body += "108=30\x01";
    return build_frame(body);
}

// Build a peer Logon with tag 52 present but EMPTY (52=\x01).
static std::vector<std::byte> make_logon_empty_sending_time(std::uint32_t seq = 1) {
    std::string body;
    body += "35=A\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=TW\x01";
    body += "52=\x01";  // present, empty value
    body += "56=ISLD\x01";
    body += "98=0\x01";
    body += "108=30\x01";
    return build_frame(body);
}

// ── Persistent store double ────────────────────────────────────────────────────
//
// A seeded, observable persistent store: tracks durable inbound/outbound
// counters and exposes them for witnesses (INV-4 / cell 9 / cell 14).

class SeededStore final : public MessageStore {
public:
    explicit SeededStore(seqnum_t seeded_inbound = seqnum_min,
                         seqnum_t seeded_outbound = seqnum_min)
        : MessageStore(flush_thunk_for<SeededStore>()),
          next_inbound_(seeded_inbound),
          next_outbound_(seeded_outbound),
          durable_inbound(seeded_inbound),
          durable_outbound(seeded_outbound) {}

    // Observable durable counters (post-persist values).
    seqnum_t durable_inbound;
    seqnum_t durable_outbound;

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> store(
        seqnum_t /*seq*/, std::span<const std::byte> /*frame*/,
        direction_t /*dir*/) noexcept override {
        co_return fixpp::core::expected_t<void>{};
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> retrieve(
        seqnum_t /*from*/, seqnum_t /*to*/, direction_t /*dir*/,
        retrieve_visitor& /*visitor*/) noexcept override {
        co_return std::unexpected(fixpp::core::error::store_seqnum_gap);
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<seqnum_t>> next_seqnum(
        direction_t dir, bool increment) noexcept override {
        auto& n = (dir == direction_t::outbound) ? next_outbound_ : next_inbound_;
        auto& d = (dir == direction_t::outbound) ? durable_outbound : durable_inbound;
        const seqnum_t curr = n;
        if (increment) {
            ++n;
            d = n;
            co_return curr;
        }
        co_return curr;
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> reset() noexcept override {
        next_inbound_ = next_outbound_ = seqnum_min;
        durable_inbound = durable_outbound = seqnum_min;
        co_return fixpp::core::expected_t<void>{};
    }

    [[nodiscard]] seqnum_t current_inbound() const noexcept { return next_inbound_; }
    [[nodiscard]] seqnum_t current_outbound() const noexcept { return next_outbound_; }

private:
    seqnum_t next_inbound_;
    seqnum_t next_outbound_;
};

class SeededStoreFactory final : public MessageStoreFactory {
public:
    explicit SeededStoreFactory(seqnum_t seeded_inbound = seqnum_min,
                                seqnum_t seeded_outbound = seqnum_min)
        : seeded_inbound_(seeded_inbound), seeded_outbound_(seeded_outbound) {}

    mutable SeededStore* last_store{nullptr};

    [[nodiscard]] bool yields_persistent_store() const noexcept override { return true; }

    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<MessageStore>> make(
        std::string_view, std::string_view, std::pmr::memory_resource*, std::size_t,
        asio::any_io_executor) noexcept override {
        auto store = std::make_unique<SeededStore>(seeded_inbound_, seeded_outbound_);
        last_store = store.get();
        return store;
    }

private:
    seqnum_t seeded_inbound_;
    seqnum_t seeded_outbound_;
};

// ── Fixture ────────────────────────────────────────────────────────────────────

class AcceptorLogonSendingTimeTest : public ::testing::Test {
protected:
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock_;
    fixpp::core::EngineConfig engine_{};

    // All outbound frames captured.
    std::vector<std::vector<std::byte>> captured_frames_;

    void SetUp() override {
        using namespace std::chrono;
        // "now" = 2024-01-01 00:00:00.000 UTC
        auto utc = system_clock::time_point{} + seconds{kNowSec};
        auto stp = fixpp::core::steady_time_point{} + seconds{0};
        clock_ = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine_.clock = clock_;
        engine_.executor = ioc.get_executor();
    }

    // Build an acceptor session config.
    // store_factory: if non-null, enables persistent store mode.
    SessionConfig make_cfg(
        std::shared_ptr<MessageStoreFactory> store_factory = nullptr) {
        SessionConfig cfg;
        cfg.sender_comp_id = "ISLD";
        cfg.target_comp_id = "TW";
        cfg.begin_string = "FIX.4.4";
        cfg.heartbeat_interval = 0s;
        cfg.role = fixpp::session::session_role::acceptor;
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override = ioc.get_executor();
        cfg.reset_seqnum_policy_field = reset_seqnum_policy::bilateral_lenient;
        cfg.store_factory = std::move(store_factory);
        cfg.transport_send = [this](std::span<const std::byte> frame) {
            captured_frames_.emplace_back(frame.begin(), frame.end());
        };
        return cfg;
    }

    void run_ioc() {
        ioc.run_for(200ms);
        ioc.restart();
    }

    void open_session(Session& sess) {
        auto fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
        run_ioc();
        ASSERT_TRUE(fut.get().has_value()) << "open() failed";
    }

    fixpp::core::expected_t<void> feed(Session& sess, std::span<const std::byte> frame) {
        auto fut = asio::co_spawn(ioc, sess.on_inbound_frame(frame), asio::use_future);
        run_ioc();
        return fut.get();
    }

    // Assert Reject(35=3, 371=52, 373=10) present and NO Logout(35=5).
    void assert_reject_no_logout(const char* cell) const {
        bool found_reject = false;
        bool found_371_52 = false;
        bool found_373_10 = false;
        bool found_logout = false;
        for (const auto& f : captured_frames_) {
            auto sp = std::span<const std::byte>(f);
            auto mt = extract_field(sp, 35);
            if (mt && *mt == "3") {
                found_reject = true;
                auto r371 = extract_field(sp, 371);
                auto r373 = extract_field(sp, 373);
                if (r371 && *r371 == "52") found_371_52 = true;
                if (r373 && *r373 == "10") found_373_10 = true;
            }
            if (mt && *mt == "5") found_logout = true;
        }
        EXPECT_TRUE(found_reject)  << cell << ": must emit Reject(35=3)";
        EXPECT_TRUE(found_371_52)  << cell << ": Reject must carry 371=52";
        EXPECT_TRUE(found_373_10)  << cell << ": Reject must carry 373=10";
        EXPECT_FALSE(found_logout) << cell << ": pre-establishment guard must NOT emit Logout";
    }

    void assert_disconnected(const Session& sess, const char* cell) const {
        EXPECT_EQ(sess.state(), fsm_state::Disconnected) << cell << ": must be Disconnected";
    }

    // Extract 34= from the first Reject(35=3) in captured frames.
    std::optional<seqnum_t> reject_seq_num() const {
        for (const auto& f : captured_frames_) {
            auto sp = std::span<const std::byte>(f);
            auto mt = extract_field(sp, 35);
            if (mt && *mt == "3") {
                auto r34 = extract_field(sp, 34);
                if (r34) {
                    try { return static_cast<seqnum_t>(std::stoul(std::string(*r34))); }
                    catch (...) {}
                }
            }
        }
        return std::nullopt;
    }
};

// ═══════════════════════════════════════════════════════════════════════════════
// Cell 1 — stale-past: 52 is 10 minutes in the past
// ═══════════════════════════════════════════════════════════════════════════════
TEST_F(AcceptorLogonSendingTimeTest, Cell1_StalePast_RejectNoLogout) {
    auto app = std::make_shared<CountingApp>();
    engine_.application = app;

    auto cfg = make_cfg();
    Session sess(engine_, cfg);
    open_session(sess);
    captured_frames_.clear();

    auto logon = make_logon_with_time(kStalePast);
    feed(sess, logon);

    assert_reject_no_logout("Cell1");
    assert_disconnected(sess, "Cell1");
    // toAdmin was invoked for the Reject.
    EXPECT_GE(app->to_admin_calls, 1) << "Cell1: toAdmin must be called for the Reject";
}

// ═══════════════════════════════════════════════════════════════════════════════
// Cell 2 — stale-future: 52 is 10 minutes in the future
// ═══════════════════════════════════════════════════════════════════════════════
TEST_F(AcceptorLogonSendingTimeTest, Cell2_StaleFuture_RejectNoLogout) {
    auto app = std::make_shared<CountingApp>();
    engine_.application = app;

    auto cfg = make_cfg();
    Session sess(engine_, cfg);
    open_session(sess);
    captured_frames_.clear();

    auto logon = make_logon_with_time(kStaleFuture);
    feed(sess, logon);

    assert_reject_no_logout("Cell2");
    assert_disconnected(sess, "Cell2");
    EXPECT_GE(app->to_admin_calls, 1) << "Cell2: toAdmin must be called for the Reject";
}

// ═══════════════════════════════════════════════════════════════════════════════
// Cell 3 — malformed-52: parse failure → reason=10
// ═══════════════════════════════════════════════════════════════════════════════
TEST_F(AcceptorLogonSendingTimeTest, Cell3_Malformed52_RejectNoLogout) {
    auto app = std::make_shared<CountingApp>();
    engine_.application = app;

    auto cfg = make_cfg();
    Session sess(engine_, cfg);
    open_session(sess);
    captured_frames_.clear();

    auto logon = make_logon_with_time(kMalformed);
    feed(sess, logon);

    assert_reject_no_logout("Cell3");
    assert_disconnected(sess, "Cell3");
    EXPECT_GE(app->to_admin_calls, 1) << "Cell3: toAdmin must be called for the Reject";
}

// ═══════════════════════════════════════════════════════════════════════════════
// Cell 4 — absent-52: no tag 52 field; reason=10 per Clarifications
// ═══════════════════════════════════════════════════════════════════════════════
TEST_F(AcceptorLogonSendingTimeTest, Cell4_Absent52_RejectReason10_NoLogout) {
    auto app = std::make_shared<CountingApp>();
    engine_.application = app;

    auto cfg = make_cfg();
    Session sess(engine_, cfg);
    open_session(sess);
    captured_frames_.clear();

    auto logon = make_logon_absent_sending_time();
    feed(sess, logon);

    assert_reject_no_logout("Cell4");
    assert_disconnected(sess, "Cell4");
    EXPECT_GE(app->to_admin_calls, 1) << "Cell4: toAdmin must be called for the Reject";
}

// ═══════════════════════════════════════════════════════════════════════════════
// Cell 5 — empty-52: 52= present but empty; distinct short-circuit path
// ═══════════════════════════════════════════════════════════════════════════════
TEST_F(AcceptorLogonSendingTimeTest, Cell5_Empty52_RejectReason10_NoLogout) {
    auto app = std::make_shared<CountingApp>();
    engine_.application = app;

    auto cfg = make_cfg();
    Session sess(engine_, cfg);
    open_session(sess);
    captured_frames_.clear();

    auto logon = make_logon_empty_sending_time();
    feed(sess, logon);

    assert_reject_no_logout("Cell5");
    assert_disconnected(sess, "Cell5");
    EXPECT_GE(app->to_admin_calls, 1) << "Cell5: toAdmin must be called for the Reject";
}

// ═══════════════════════════════════════════════════════════════════════════════
// Cell 6 — toAdmin throws on the new Reject → app_callback_threw + Disconnected
// ═══════════════════════════════════════════════════════════════════════════════
TEST_F(AcceptorLogonSendingTimeTest, Cell6_ToAdminThrows_AppCallbackThrew) {
    auto app = std::make_shared<ThrowOnRejectApp>();
    engine_.application = app;

    auto cfg = make_cfg();
    Session sess(engine_, cfg);
    open_session(sess);
    captured_frames_.clear();

    auto logon = make_logon_with_time(kStalePast);
    auto r = feed(sess, logon);

    // on_inbound_frame must return error (app_callback_threw).
    EXPECT_FALSE(r.has_value()) << "Cell6: on_inbound_frame must return error when toAdmin throws";
    if (!r.has_value()) {
        EXPECT_EQ(r.error(), fixpp::core::error::app_callback_threw)
            << "Cell6: error must be app_callback_threw";
    }
    assert_disconnected(sess, "Cell6");
    // toAdmin WAS called (it threw; fire_to_admin_ caught and converted to false).
    EXPECT_GE(app->to_admin_calls, 1) << "Cell6: toAdmin must have been called";
}

// ═══════════════════════════════════════════════════════════════════════════════
// Cell 7 — assign_outbound failure → fail-closed + Disconnected
// ═══════════════════════════════════════════════════════════════════════════════
TEST_F(AcceptorLogonSendingTimeTest, Cell7_AssignOutboundFails_Disconnected) {
    auto cfg = make_cfg();
    Session sess(engine_, cfg);
    open_session(sess);

    // Inject overflow: next assign_outbound returns store_seqnum_overflow.
    {
        seqnum_t cur_in = sess.seqnum_mgr_test_access().next_inbound_unsafe();
        sess.seqnum_mgr_test_access().set_counters_for_test(cur_in, seqnum_max);
    }
    captured_frames_.clear();

    auto logon = make_logon_with_time(kStalePast);
    auto r = feed(sess, logon);

    // Fail-closed: returns an error.
    EXPECT_FALSE(r.has_value()) << "Cell7: must fail-closed when assign_outbound fails";
    assert_disconnected(sess, "Cell7");

    // No Logout emitted.
    for (const auto& f : captured_frames_) {
        auto mt = extract_field(std::span<const std::byte>(f), 35);
        EXPECT_NE(mt.value_or(""), "5") << "Cell7: NO Logout on assign_outbound failure path";
    }

    // Discriminator: without this, the cell passes via the DOWNSTREAM reply-Logon
    // assign_outbound fail (which also overflows the injected seqnum_max), so it would
    // stay green even if the guard's assign-fail handling were absent. The guard fires
    // BEFORE check_inbound, so on the guard path inbound is NOT advanced; without the
    // guard, check_inbound advances inbound (to seqnum_min+1) before the reply assign
    // fails. Asserting inbound unchanged makes the cell discriminate the guard's arm.
    // [[feedback_witness_asserts_named_postcondition_not_proxy]]
    const seqnum_t mem_in = sess.seqnum_mgr_test_access().next_inbound_unsafe();
    EXPECT_EQ(mem_in, seqnum_min)
        << "Cell7: inbound must NOT be advanced (52 guard precedes check_inbound); got "
        << mem_in;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Cell 8 — GENUINE build_reject build-FAILURE → fail-closed (no silent-success)
//
// An oversized sender_comp_id (500 chars) overflows the guard's 512B build buffer,
// so build_reject returns unexpected. The guard's `if (rj_r)` skips the emit and the
// unconditional `record_state_transition_(Disconnected)` fires — FAIL CLOSED with NO
// frame on the wire (never establish, never emit a partial/garbage frame).
// Mirrors logon_handshake_test Acceptor_BuildLogonOverflow_DoesNotReachActive: the
// CompIDs MATCH (peer 49=TW=our target, peer 56=<long>=our sender) so interpret_logon
// passes and the ONLY overflow site is the guard's build_reject.
// [[feedback_fixed_buffer_build_failure_silent_success]] — this is the build-FAILURE
// arm Cell 1 (build-success) does not cover.
// ═══════════════════════════════════════════════════════════════════════════════
TEST_F(AcceptorLogonSendingTimeTest, Cell8_BuildRejectOverflow_FailClosed_NoFrame) {
    // 500-char sender_comp_id: build_reject body ≈ 90B fixed + 500 (49=) + len(56=TW)
    // ≈ 595B > 512B → wire_frame_too_large.
    const std::string long_sender(500, 'X');
    auto cfg = make_cfg();
    cfg.sender_comp_id = long_sender;
    cfg.target_comp_id = "TW";
    Session sess(engine_, cfg);
    open_session(sess);
    captured_frames_.clear();

    // Peer Logon: 49=TW (our target), 56=<long_sender> (our sender) — CompIDs match so
    // interpret_logon passes; stale 52 so the guard fires and attempts build_reject.
    std::string body;
    body += "35=A\x01";
    body += "34=1\x01";
    body += "49=TW\x01";
    body += "52=" + std::string(kStalePast) + "\x01";
    body += "56=" + long_sender + "\x01";
    body += "98=0\x01";
    body += "108=30\x01";
    auto logon = build_frame(body);
    feed(sess, logon);

    // FAIL CLOSED: Disconnected, and NO frame emitted (build_reject overflowed → the
    // `if (rj_r)` guard skipped the emit; the unconditional Disconnected still fired).
    // A silent-success bug would either establish or emit a truncated frame.
    assert_disconnected(sess, "Cell8");
    EXPECT_TRUE(captured_frames_.empty())
        << "Cell8: build_reject overflow must emit NO frame (fail-closed); got "
        << captured_frames_.size() << " frame(s)";
}

// ═══════════════════════════════════════════════════════════════════════════════
// Cell 9 — persistent-store inbound seeded-not-advanced (INV-4)
//
// With a persistent store seeded to durable_inbound=D:
//   - after rejected Logon: durable inbound == D (guard precedes check_inbound)
//   - in-memory next_inbound == D (ensure_hydrated_ seeded it; NOT seqnum_min)
// ═══════════════════════════════════════════════════════════════════════════════
TEST_F(AcceptorLogonSendingTimeTest, Cell9_PersistentStore_InboundNotAdvanced) {
    constexpr seqnum_t kSeedIn = 5;
    constexpr seqnum_t kSeedOut = 3;

    auto factory = std::make_shared<SeededStoreFactory>(kSeedIn, kSeedOut);
    auto cfg = make_cfg(factory);
    Session sess(engine_, cfg);
    open_session(sess);
    captured_frames_.clear();

    // Feed stale logon at the expected inbound seq.
    auto logon = make_logon_with_time(kStalePast, /*seq=*/kSeedIn);
    feed(sess, logon);

    assert_disconnected(sess, "Cell9");
    assert_reject_no_logout("Cell9");

    // Durable inbound must be UNCHANGED (guard fires before check_inbound).
    ASSERT_NE(factory->last_store, nullptr) << "Cell9: store was not minted";
    EXPECT_EQ(factory->last_store->durable_inbound, kSeedIn)
        << "Cell9: durable inbound must be unchanged (seeded D=" << kSeedIn << ")";

    // In-memory inbound must equal the seeded value (ensure_hydrated_ seeded it,
    // guard fired before check_inbound could advance it).
    const seqnum_t mem_in = sess.seqnum_mgr_test_access().next_inbound_unsafe();
    EXPECT_EQ(mem_in, kSeedIn)
        << "Cell9: in-memory next_inbound must be seeded D=" << kSeedIn
        << " (NOT seqnum_min=" << seqnum_min << ")";
}

// ═══════════════════════════════════════════════════════════════════════════════
// Cell 10 — stale-52 rejects even when a 1137 field is present on the Logon.
//
// SCOPE NOTE: the FIXT DefaultApplVerID(1137) reject gate is reachable ONLY for a
// FIXT session (`cfg_.is_fixt() && app_version_registry_`); this FIX.4.4 fixture
// never runs it, so a "no 371=1137" assertion here would be vacuously true. This
// cell instead witnesses the (true, discriminating) fact that the 52 guard fires on
// a stale 52 even when the Logon carries an extra 1137 field (the guard is not
// deflected by the tag). The GENUINE 52-vs-1137-GATE ordering witness (bad-52 +
// missing/unserviceable 1137 on a FIXT session → Reject 371=52 wins, no 371=1137) is
// added in the FIXT suite test_fixt_logon_establishment.cpp (US3 / T013), where
// FixtSetup + the version registry + the 1137 reject path already live — it is NOT
// duplicated here. The core "52 guard pre-empts downstream gates" ordering is already
// witnessed discriminatingly by Cell 11 (vs credentials) and Cell 12 (vs check_inbound),
// both reachable on FIX.4.4. [[feedback_witness_asserts_named_postcondition_not_proxy]]
// ═══════════════════════════════════════════════════════════════════════════════
TEST_F(AcceptorLogonSendingTimeTest, Cell10_Stale52WithExtra1137Field_RejectsOn52) {
    auto cfg = make_cfg();
    Session sess(engine_, cfg);
    open_session(sess);
    captured_frames_.clear();

    // Stale 52 + an extra 1137 field appended (inert on FIX.4.4; must not deflect the guard).
    auto logon = make_logon_with_time(kStalePast, 1, "1137=BADVER\x01");
    feed(sess, logon);

    assert_reject_no_logout("Cell10");
    assert_disconnected(sess, "Cell10");

    // The emitted Reject is the 52 guard's (371=52); no 371=1137 reject exists (the
    // 1137 gate is FIXT-only and not run here).
    for (const auto& f : captured_frames_) {
        auto sp = std::span<const std::byte>(f);
        auto mt = extract_field(sp, 35);
        if (mt && *mt == "3") {
            auto r371 = extract_field(sp, 371);
            if (r371) {
                EXPECT_EQ(*r371, "52")
                    << "Cell10: Reject 371 must be 52 (guard fires on stale 52)";
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Cell 11 — bad-52 + rejecting credentials → 52 wins (authorize_logon not reached)
// ═══════════════════════════════════════════════════════════════════════════════
TEST_F(AcceptorLogonSendingTimeTest, Cell11_BadStalePlusBadCreds_52Wins) {
    auto cfg = make_cfg();
    // Always-reject credential validator.
    cfg.compid_authorization_policy.set_logon_validator(
        [](std::string_view, fixpp::session::logon_credentials const&) -> bool {
            return false;
        });
    Session sess(engine_, cfg);
    open_session(sess);
    captured_frames_.clear();

    auto logon = make_logon_with_time(kStalePast);
    feed(sess, logon);

    // 52 guard fires first → Reject(371=52) + Disconnected.
    assert_reject_no_logout("Cell11");
    assert_disconnected(sess, "Cell11");

    bool found_reject_52 = false;
    for (const auto& f : captured_frames_) {
        auto sp = std::span<const std::byte>(f);
        auto mt = extract_field(sp, 35);
        if (mt && *mt == "3") {
            auto r371 = extract_field(sp, 371);
            if (r371 && *r371 == "52") found_reject_52 = true;
        }
    }
    EXPECT_TRUE(found_reject_52)
        << "Cell11: Reject(371=52) must be emitted (52 wins over credential check)";
}

// ═══════════════════════════════════════════════════════════════════════════════
// Cell 12 — bad-52 + too-high inbound seqnum → 52 wins (check_inbound not reached)
// ═══════════════════════════════════════════════════════════════════════════════
TEST_F(AcceptorLogonSendingTimeTest, Cell12_BadStalePlusTooHighSeq_52Wins) {
    auto cfg = make_cfg();
    Session sess(engine_, cfg);
    open_session(sess);
    captured_frames_.clear();

    // seq=999 is too-high (expected 1); stale 52. 52 guard fires before check_inbound.
    auto logon = make_logon_with_time(kStalePast, /*seq=*/999);
    feed(sess, logon);

    assert_reject_no_logout("Cell12");
    assert_disconnected(sess, "Cell12");

    // Reject must carry 371=52 (not from seqnum handling).
    bool found_52 = false;
    for (const auto& f : captured_frames_) {
        auto sp = std::span<const std::byte>(f);
        auto mt = extract_field(sp, 35);
        if (mt && *mt == "3") {
            auto r371 = extract_field(sp, 371);
            if (r371 && *r371 == "52") found_52 = true;
        }
    }
    EXPECT_TRUE(found_52)
        << "Cell12: Reject(371=52) must be emitted (52 guard precedes check_inbound)";
}

// ═══════════════════════════════════════════════════════════════════════════════
// Cell 13 — conforming: 52=now → Active (no regression)
// ═══════════════════════════════════════════════════════════════════════════════
TEST_F(AcceptorLogonSendingTimeTest, Cell13_Conforming_SessionReachesActive) {
    auto cfg = make_cfg();
    Session sess(engine_, cfg);
    open_session(sess);
    captured_frames_.clear();

    auto logon = make_logon_with_time(kValidSendingTime);
    auto r = feed(sess, logon);

    EXPECT_TRUE(r.has_value()) << "Cell13: conforming Logon must succeed";
    EXPECT_EQ(sess.state(), fsm_state::Active)
        << "Cell13: session must be Active after conforming Logon";

    // Reply Logon(35=A) must be emitted.
    bool found_logon_reply = false;
    for (const auto& f : captured_frames_) {
        auto mt = extract_field(std::span<const std::byte>(f), 35);
        if (mt && *mt == "A") found_logon_reply = true;
    }
    EXPECT_TRUE(found_logon_reply) << "Cell13: acceptor must emit reply Logon(35=A)";

    // No Reject on a conforming path.
    bool found_reject = false;
    for (const auto& f : captured_frames_) {
        auto mt = extract_field(std::span<const std::byte>(f), 35);
        if (mt && *mt == "3") found_reject = true;
    }
    EXPECT_FALSE(found_reject) << "Cell13: NO Reject on conforming SendingTime path";
}

// ═══════════════════════════════════════════════════════════════════════════════
// Cell 14 — persistent-reconnect durable outbound N>1 → Reject carries 34=N
//
// Persistent store seeded to durable_outbound=N. ensure_hydrated_ seeds the
// in-memory outbound counter to N. The 52 guard fires after ensure_hydrated_,
// so peek_outbound() returns N and the Reject carries 34=N.
// Proves guard placement: if placed BEFORE ensure_hydrated_, Reject would carry 34=1.
// ═══════════════════════════════════════════════════════════════════════════════
TEST_F(AcceptorLogonSendingTimeTest, Cell14_PersistentReconnect_RejectCarriesHydratedSeq) {
    constexpr seqnum_t kSeedIn = 1;
    constexpr seqnum_t kSeedOut = 7;  // N > 1

    auto factory = std::make_shared<SeededStoreFactory>(kSeedIn, kSeedOut);
    auto cfg = make_cfg(factory);
    Session sess(engine_, cfg);
    open_session(sess);
    captured_frames_.clear();

    auto logon = make_logon_with_time(kStalePast, /*seq=*/kSeedIn);
    feed(sess, logon);

    assert_disconnected(sess, "Cell14");
    assert_reject_no_logout("Cell14");

    // Reject 34 must equal N (hydrated from store).
    auto rj_seq = reject_seq_num();
    ASSERT_TRUE(rj_seq.has_value()) << "Cell14: Reject must have been emitted";
    EXPECT_EQ(*rj_seq, kSeedOut)
        << "Cell14: Reject(34) must equal hydrated durable outbound N=" << kSeedOut
        << " (guard fires AFTER ensure_hydrated_); got " << *rj_seq;

    // Durable inbound unchanged.
    ASSERT_NE(factory->last_store, nullptr) << "Cell14: store was not minted";
    EXPECT_EQ(factory->last_store->durable_inbound, kSeedIn)
        << "Cell14: durable inbound must be unchanged";
}

}  // namespace fixpp::session::test
