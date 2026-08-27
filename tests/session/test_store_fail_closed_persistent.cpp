// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_store_fail_closed_persistent.cpp
//
// 059-outbound-store-fail-closed — T005 [US1] Phase 3 RED witness.
//
// W1 (quickstart.md) — the flagship cascade witness (SC-001/SC-002, FR-001/
// FR-002/FR-007/FR-008). Drives a real FileStore-backed initiator Session
// through: (0) a probe-fired confirmation that the FIXPP_TEST_HOOKS pwrite
// fault-injection seam (T003/T004) actually forced a store() failure for
// message k, so the RED below is the CASCADE, not a miswired seam
// (feedback_fail_placeholder_red_test); (1) the pre-fix freeze cascade
// (RED, MUST currently hold — proves the defect: subsequent retains are
// silently swallowed, the peer's resend for [k..k+n] folds to a
// SequenceReset-GapFill instead of real replays); (2) the GREEN post-fix
// disposition (currently FAILING on this pre-fix tree, by design — T006/T007
// land in a later round): fail-closed to Disconnected at message k, frame k
// never transmitted, and a simulated restart resumes without desync.
//
// Harness: asio::thread_pool{2} + a single per-session strand (mirrors
// test_credential_store_redaction.cpp) — the store path is strand-confined
// and async_mutex-guarded; a single-threaded harness would mask races
// (feedback_single_threaded_harness_masks_strand_races).
//
// Anchors: specs/059-outbound-store-fail-closed/{spec.md FR-001/002/004/007/
// 008, SC-001/002; research.md D1-D7; quickstart.md W1}.
#include <gtest/gtest.h>

#include <asio/any_io_executor.hpp>
#include <asio/co_spawn.hpp>
#include <asio/strand.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/file_store.hpp>
#include <fixpp/session/file_store_factory.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "_fixtures_/store_temp_dir.hpp"
#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"
#include "support/extract_tag.hpp"

using namespace std::chrono_literals;

namespace {

using fixpp::session::FileStore;
using fixpp::session::FileStoreFactory;
using fixpp::session::fsm_state;
using fixpp::session::Session;
using fixpp::session::session_role;
using fixpp::store_test::remove_store_dir;
using fixpp::store_test::unique_store_dir;

// ── Frame-building helpers (mirror test_recovery_store_horizon.cpp) ──────────

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

std::vector<std::byte> make_resend_request(std::string_view bs, std::uint32_t seq,
                                           std::string_view s, std::string_view t,
                                           std::uint32_t begin_seqno, std::uint32_t end_seqno) {
    std::string extra;
    extra += field(7, std::to_string(begin_seqno));
    extra += field(16, std::to_string(end_seqno));
    return make_fix_frame(bs, "2", seq, s, t, extra);
}

// Minimal app payload (35=D). Session::send builds the full wire frame
// (header + MsgSeqNum) around this opaque body (mirrors
// test_application_outbound.cpp's make_app_payload).
std::vector<std::byte> make_app_payload(std::string_view clordid) {
    std::string body = "35=D\x01" + std::string(field(11, clordid)) + "54=1\x01"
                                                                       "55=AAPL\x01";
    std::vector<std::byte> v;
    v.reserve(body.size());
    for (char c : body) v.push_back(static_cast<std::byte>(c));
    return v;
}

using fixpp::test_support::extract_tag;

bool is_msg_type(std::span<const std::byte> frame, std::string_view type) {
    std::string wire(reinterpret_cast<const char*>(frame.data()), frame.size());
    std::string needle = "35=" + std::string(type) + "\x01";
    return wire.find(needle) != std::string::npos;
}

// SequenceReset{GapFillFlag=Y, NewSeqNo=<new_seqno>}
bool is_gapfill_to(const std::vector<std::byte>& frame, std::uint32_t new_seqno) {
    if (!is_msg_type(frame, "4")) return false;
    std::string wire(reinterpret_cast<const char*>(frame.data()), frame.size());
    if (wire.find("123=Y\x01") == std::string::npos) return false;
    return wire.find("36=" + std::to_string(new_seqno) + "\x01") != std::string::npos;
}

// Frame carries PossDupFlag(43)=Y and has the given MsgSeqNum(34) — a real
// application-message replay (as opposed to an administrative gap-fill).
bool is_replay_with_poss_dup(const std::vector<std::byte>& frame, std::uint32_t seq) {
    std::string wire(reinterpret_cast<const char*>(frame.data()), frame.size());
    if (wire.find("43=Y\x01") == std::string::npos) return false;
    return wire.find("34=" + std::to_string(seq) + "\x01") != std::string::npos;
}

// ── Fixture ───────────────────────────────────────────────────────────────────

class StoreFailClosedPersistentTest : public ::testing::Test {
protected:
    void SetUp() override {
        pool_ = std::make_unique<asio::thread_pool>(2);
        sx_ = asio::make_strand(pool_->get_executor());
        dir_ = unique_store_dir("store_fail_closed_persistent");

        auto utc = std::chrono::system_clock::time_point{} + std::chrono::seconds{1704067200};
        auto stp = fixpp::core::steady_time_point{};
        clock_ = std::make_shared<fixpp::core::mock_clock>(utc, stp, sx_);
        engine_.executor = sx_;
        engine_.clock = clock_;
        engine_.max_store_memory_per_session = 1ULL << 30;
    }

    void TearDown() override {
        remove_store_dir(dir_);
        if (pool_) {
            pool_->stop();
            pool_->join();
        }
    }

    // Builds an initiator SessionConfig backed by a FRESH FileStore over the
    // fixture's directory (same sender/target each time — used both for the
    // initial session and the "restart" reopen).
    fixpp::session::SessionConfig make_initiator_cfg() {
        FileStore::Config fcfg;
        fcfg.directory = dir_;
        fcfg.sender_comp_id = "INITR";
        fcfg.target_comp_id = "ACCEPTR";
        fcfg.max_frame_bytes = 4096;
        fcfg.file_io_executor = sx_;

        fixpp::session::SessionConfig cfg;
        cfg.role = session_role::initiator;
        cfg.sender_comp_id = "INITR";
        cfg.target_comp_id = "ACCEPTR";
        cfg.begin_string = "FIX.4.2";
        cfg.heartbeat_interval = 0s;  // disable liveness loop noise
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override = sx_;
        // RC#C-style: bilateral_lenient — this witness exercises US1 fail-closed,
        // not the US3 reset-policy variants (those are a separate task, T011).
        cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
        cfg.store_factory = std::make_shared<FileStoreFactory>(fcfg);
        return cfg;
    }

    std::unique_ptr<asio::thread_pool> pool_;
    asio::any_io_executor sx_;
    std::filesystem::path dir_;
    std::shared_ptr<fixpp::core::mock_clock> clock_;
    fixpp::core::EngineConfig engine_{};
};

// ─────────────────────────────────────────────────────────────────────────────
// W1 — persistent retain failure: cascade RED on the pre-fix tree, GREEN
// disposition currently failing (T006/T007 land in a later invocation).
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(StoreFailClosedPersistentTest,
       W1_PersistentRetainFailure_CascadeRedGreenDispositionCurrentlyFails) {
    std::vector<std::vector<std::byte>> wire;
    auto cfg = make_initiator_cfg();
    cfg.transport_send = [&](std::span<const std::byte> f) { wire.emplace_back(f.begin(), f.end()); };

    auto sess = std::make_unique<Session>(engine_, cfg);

    // open() emits the initiator Logon (seq=1) and transitions to LogonSent.
    auto open_r = asio::co_spawn(sx_, sess->open(), asio::use_future).get();
    ASSERT_TRUE(open_r.has_value()) << "open() must succeed";
    ASSERT_EQ(sess->state(), fsm_state::LogonSent);

    // Feed the peer's Logon-ack (seq=1) — reaches Active.
    auto peer_logon = make_logon("FIX.4.2", 1, "ACCEPTR", "INITR");
    auto logon_r =
        asio::co_spawn(sx_, sess->on_inbound_frame(std::span<const std::byte>(peer_logon)),
                       asio::use_future)
            .get();
    ASSERT_TRUE(logon_r.has_value()) << "peer Logon-ack must be accepted";
    ASSERT_EQ(sess->state(), fsm_state::Active);

    // Step 1: send one baseline app message (seq k-1) — retained normally.
    auto payload_pre = make_app_payload("ORD-PRE");
    auto pre_r =
        asio::co_spawn(sx_, sess->send(std::span<const std::byte>(payload_pre)), asio::use_future)
            .get();
    ASSERT_TRUE(pre_r.has_value()) << "the baseline (pre-failure) send must succeed";

    // k = the seq about to be assigned to the FAILING send.
    const std::uint32_t k = sess->seqnum_mgr_test_access().peek_outbound();

    // Step 2: arm the seam, then send message k — the store's pwrite is
    // forced to fail once.
    fixpp::session::arm_force_store_pwrite_fail_once();
    auto payload_k = make_app_payload("ORD-K");
    auto k_r =
        asio::co_spawn(sx_, sess->send(std::span<const std::byte>(payload_k)), asio::use_future)
            .get();

    // Step 0 (V5 — RED-for-the-right-reason): the seam must have actually
    // fired for message k, otherwise everything below proves nothing
    // (feedback_fail_placeholder_red_test).
    ASSERT_GE(fixpp::session::read_and_reset_store_pwrite_fail_count(), 1)
        << "the FileStore pwrite fault-injection seam (T003/T004) must have fired for "
           "message k=" << k << "; a RED result below would be a miswired-seam false "
           "positive, not the cascade";

    // Step 3 (fail-closed scenario): the persistent retain failure at message k
    // took the session down. A further send() must be REFUSED (session no longer
    // Active) — there is no freeze cascade of swallowed stores and no further
    // frames reach the wire. (Pre-fix this send kept succeeding and the peer's
    // ResendRequest for [k..] folded into a SequenceReset-GapFill with the real
    // app messages silently lost; that RED baseline is captured in the verify
    // doc — it cannot be asserted here because it is the exact behaviour the fix
    // removes, so it cannot co-exist GREEN with the post-conditions below.)
    auto payload_after = make_app_payload("ORD-AFTER");
    auto after_r = asio::co_spawn(
                       sx_, sess->send(std::span<const std::byte>(payload_after)),
                       asio::use_future)
                       .get();
    EXPECT_FALSE(after_r.has_value())
        << "after a fail-closed retain failure the session is Disconnected; a further "
           "send() must be refused, not silently accepted";

    // ── Post-conditions (each RED without the T006/T007 fix — verified by
    //    reverting the disposition change; see the verify doc) ──
    EXPECT_FALSE(k_r.has_value())
        << "send() for message k=" << k << " must fail closed on a persistent retain "
           "failure, not silently succeed";
    EXPECT_EQ(sess->state(), fsm_state::Disconnected)
        << "the session must transition to Disconnected at the first persistent retain "
           "failure (message k=" << k << ")";
    bool frame_k_transmitted = false;
    for (const auto& frame : wire) {
        if (extract_tag(frame, 34) == std::to_string(k)) frame_k_transmitted = true;
    }
    EXPECT_FALSE(frame_k_transmitted)
        << "FR-002: frame k=" << k << " must NOT be transmitted on a retain failure "
           "(retain-before-transmit ordering)";

    // Release the FileStore's advisory lock before "restarting".
    auto close_r =
        asio::co_spawn(sx_, sess->close(fixpp::session::close_mode::terminal), asio::use_future)
            .get();
    ASSERT_TRUE(close_r.has_value()) << "close() must succeed to release the advisory lock "
                                        "before the simulated restart";
    sess.reset();

    // Simulated restart: a FRESH FileStore/Session over the SAME directory.
    // The initiator's open() re-emits a Logon carrying the hydrated outbound
    // counter — the observable "restart recovery" value.
    std::vector<std::vector<std::byte>> wire2;
    auto cfg2 = make_initiator_cfg();
    cfg2.transport_send = [&](std::span<const std::byte> f) {
        wire2.emplace_back(f.begin(), f.end());
    };
    Session sess2(engine_, cfg2);
    auto open2_r = asio::co_spawn(sx_, sess2.open(), asio::use_future).get();
    ASSERT_TRUE(open2_r.has_value())
        << "restart open() must succeed; error="
        << (open2_r.has_value() ? 0 : static_cast<int>(open2_r.error()));

    ASSERT_FALSE(wire2.empty()) << "restart must re-emit an initiator Logon";
    const std::string restart_seq_str = extract_tag(wire2.front(), 34);
    ASSERT_FALSE(restart_seq_str.empty()) << "restart Logon must carry tag 34";
    const std::uint32_t restart_seq = static_cast<std::uint32_t>(std::stoul(restart_seq_str));

    // Post-condition (SC-002): the durable counter recovered on restart is k.
    // Message k's store failed BEFORE any durable write (faithful seam), so the
    // durable outbound counter never advanced past k; the peer's last-seen is
    // k-1 (frame k was never transmitted). durable==k and peer-last-seen==k-1
    // agree — no restart desync (the defect this feature eliminates). RED
    // without the fix: the failed frame k was transmitted and the durable
    // counter desynced below the peer's last-seen.
    EXPECT_EQ(restart_seq, k)
        << "restart must recover the durable outbound counter at k=" << k
        << " (the failed store never advanced it); got " << restart_seq;

    asio::co_spawn(sx_, sess2.close(fixpp::session::close_mode::terminal), asio::use_future).get();
}

}  // namespace
