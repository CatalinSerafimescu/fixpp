// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_store_fail_reconcile.cpp
//
// 059-outbound-store-fail-closed — T011 [US3] Phase 5 GREEN witness.
//
// W3 (quickstart.md) — reconcile-from-durable on reconnect (SC-004, FR-007),
// three HONEST policy variants. Drives a real FileStore-backed initiator
// Session through the same W1 fail-closed cascade (arm the FIXPP_TEST_HOOKS
// pwrite seam, send message k, observe store_then_emit's fatal branch), then
// exercises the reconcile's observable effect via an IN-PROCESS reconnect on
// the SAME Session (Session::drive_reconnect() over a mock_transport) — a
// fresh-Session "restart" would re-hydrate next_outbound straight from the
// (unchanged) durable file and bypass the reconcile entirely, so it cannot
// witness FR-007 (the reconcile mutates only the in-memory counter via
// set_next_outbound, never the durable file).
//
// Variant A — plain persistent (bilateral_lenient, no reset knob): reconcile
//   lands the reconnect Logon at seq k, cleanly.
// Variant B — reset_on_logon=true: the durable reset (reset_seqnums_to_one_durable)
//   overrides BOTH counters to {1,1} before the reconnect Logon; the
//   reconcile is neutral.
// Variant C — bilateral_strict (the DEFAULT policy, no reset knob): NO
//   durable reset exists on this path; the reconnect Logon carries 34=k
//   (k>1) WITH 141=Y — the pre-existing, DEFERRED L-029-3 malformed-Logon
//   limitation (behaviors-and-limitations.md's L-029-3). This variant is a
//   REGRESSION GUARD ONLY: it asserts 059 does not worsen L-029-3 (the
//   reconciled k is non-1, same as an un-reconciled k+1 would be) — it does
//   NOT assert clean recovery.
//
// Harness: asio::thread_pool{2} + a single per-session strand (mirrors
// test_store_fail_closed_persistent.cpp) — the store path is strand-confined
// and async_mutex-guarded; a single-threaded harness would mask races
// (feedback_single_threaded_harness_masks_strand_races).
//
// Anchors: specs/059-outbound-store-fail-closed/{spec.md US3/FR-007, SC-004;
// research.md D4; quickstart.md W3; behaviors-and-limitations.md L-029-3}.
#include <gtest/gtest.h>

#include <asio/any_io_executor.hpp>
#include <asio/co_spawn.hpp>
#include <asio/strand.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_future.hpp>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/application.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/file_store.hpp>
#include <fixpp/session/file_store_factory.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fixpp/transport/transport_factory.hpp>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// mock_transport is a test-only header; gate it with the required define.
#define FIXPP_ALLOW_MOCK_TRANSPORT
#include <fixpp/transport/test/mock_transport.hpp>

#include "_fixtures_/store_temp_dir.hpp"
#include "support/extract_tag.hpp"
#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"

using namespace std::chrono_literals;

namespace {

using fixpp::session::FileStore;
using fixpp::session::FileStoreFactory;
using fixpp::session::fsm_state;
using fixpp::session::reset_seqnum_policy;
using fixpp::session::Session;
using fixpp::session::session_role;
using fixpp::store_test::remove_store_dir;
using fixpp::store_test::unique_store_dir;

// ── Frame-building helpers (mirror test_store_fail_closed_persistent.cpp) ───

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

// make_logon_reset: build a Logon with 141=Y (ResetSeqNumFlag). Needed for the
// Variant C cold-open peer ack — bilateral_strict rejects a peer ack without
// 141=Y (the RC#C bilateral_strict initiator-path guard).
std::vector<std::byte> make_logon_reset(std::string_view bs, std::uint32_t seq, std::string_view s,
                                        std::string_view t, int hbt = 30) {
    std::string extra;
    extra += field(98, "0");
    extra += field(108, std::to_string(hbt));
    extra += field(141, "Y");
    return make_fix_frame(bs, "A", seq, s, t, extra);
}

// Minimal app payload (35=D). Session::send builds the full wire frame
// (header + MsgSeqNum) around this opaque body.
std::vector<std::byte> make_app_payload(std::string_view clordid) {
    std::string body = "35=D\x01" + std::string(field(11, clordid)) +
                       "54=1\x01"
                       "55=AAPL\x01";
    std::vector<std::byte> v;
    v.reserve(body.size());
    for (char c : body) v.push_back(static_cast<std::byte>(c));
    return v;
}

using fixpp::test_support::extract_tag;

// ── MockReconnectFactory: TransportFactory returning mock_transport ────────
//
// Used to drive session->drive_reconnect() as the in-process "2nd logon"
// vehicle (mirrors test_refresh_on_logon.cpp's MockReconnectFactory). make()
// returns a mock_transport with handshake_succeeds=true and empty inbound (so
// async_read_some immediately returns EOF after the reconnect Logon is sent).

class MockReconnectFactory final : public fixpp::transport::TransportFactory {
public:
    fixpp::transport::test::mock_transport* last_transport{nullptr};

    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<fixpp::transport::Transport>> make(
        asio::any_io_executor exec, fixpp::tls::SslCtxConfig /*ssl_cfg*/,
        std::pmr::memory_resource* /*mr*/) noexcept override {
        fixpp::transport::test::Script script;
        script.handshake_succeeds = true;
        auto t = std::make_unique<fixpp::transport::test::mock_transport>(std::move(exec),
                                                                          std::move(script));
        last_transport = t.get();
        return t;
    }

    [[nodiscard]] fixpp::core::expected_t<void> reload_credentials(
        std::shared_ptr<fixpp::tls::cert_source> /*new_source*/) noexcept override {
        return {};
    }

    [[nodiscard]] std::shared_ptr<fixpp::tls::cert_source> cert_source_snapshot()
        const noexcept override {
        return nullptr;
    }
};

// ── Fixture ───────────────────────────────────────────────────────────────

class StoreFailReconcileTest : public ::testing::Test {
protected:
    void SetUp() override {
        pool_ = std::make_unique<asio::thread_pool>(2);
        sx_ = asio::make_strand(pool_->get_executor());
        dir_ = unique_store_dir("store_fail_reconcile");

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
    // fixture's directory, wired for BOTH the cold-open transport_send path
    // AND a MockReconnectFactory (the drive_reconnect() vehicle).
    fixpp::session::SessionConfig make_initiator_cfg(reset_seqnum_policy policy,
                                                     bool reset_on_logon,
                                                     std::shared_ptr<MockReconnectFactory> tf) {
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
        cfg.reset_seqnum_policy_field = policy;
        cfg.reset_on_logon = reset_on_logon;
        cfg.store_factory = std::make_shared<FileStoreFactory>(fcfg);
        cfg.transport_factory_override = tf;
        cfg.reconnect_endpoint = fixpp::transport::Endpoint{"127.0.0.1", 19099};
        return cfg;
    }

    std::unique_ptr<asio::thread_pool> pool_;
    asio::any_io_executor sx_;
    std::filesystem::path dir_;
    std::shared_ptr<fixpp::core::mock_clock> clock_;
    fixpp::core::EngineConfig engine_{};
};

// ─────────────────────────────────────────────────────────────────────────
// W3 — reconcile-from-durable: fail closed at message k, reconcile
// peek_outbound() to k, then drive an in-process reconnect and inspect the
// reconnect Logon per policy variant.
// ─────────────────────────────────────────────────────────────────────────

TEST_F(StoreFailReconcileTest, VariantA_PlainPersistent_CleanResumeAtK) {
    std::vector<std::vector<std::byte>> wire;
    auto transport_fac = std::make_shared<MockReconnectFactory>();
    auto cfg = make_initiator_cfg(reset_seqnum_policy::bilateral_lenient,
                                  /*reset_on_logon=*/false, transport_fac);
    cfg.transport_send = [&](std::span<const std::byte> f) {
        wire.emplace_back(f.begin(), f.end());
    };

    auto sess = std::make_unique<Session>(engine_, cfg);

    auto open_r = asio::co_spawn(sx_, sess->open(), asio::use_future).get();
    ASSERT_TRUE(open_r.has_value()) << "open() must succeed";
    ASSERT_EQ(sess->state(), fsm_state::LogonSent);

    auto peer_logon = make_logon("FIX.4.2", 1, "ACCEPTR", "INITR");
    auto logon_r =
        asio::co_spawn(sx_, sess->on_inbound_frame(std::span<const std::byte>(peer_logon)),
                       asio::use_future)
            .get();
    ASSERT_TRUE(logon_r.has_value()) << "peer Logon-ack must be accepted";
    ASSERT_EQ(sess->state(), fsm_state::Active);

    const auto inbound_before = sess->seqnum_mgr_test_access().next_inbound_unsafe();

    auto payload_pre = make_app_payload("ORD-PRE");
    auto pre_r =
        asio::co_spawn(sx_, sess->send(std::span<const std::byte>(payload_pre)), asio::use_future)
            .get();
    ASSERT_TRUE(pre_r.has_value()) << "the baseline (pre-failure) send must succeed";

    const std::uint32_t k = sess->seqnum_mgr_test_access().peek_outbound();

    fixpp::session::arm_force_store_pwrite_fail_once();
    auto payload_k = make_app_payload("ORD-K");
    auto k_r =
        asio::co_spawn(sx_, sess->send(std::span<const std::byte>(payload_k)), asio::use_future)
            .get();

    ASSERT_GE(fixpp::session::read_and_reset_store_pwrite_fail_count(), 1)
        << "the FileStore pwrite fault-injection seam must have fired for message k=" << k;
    ASSERT_FALSE(k_r.has_value()) << "send() for message k must fail closed";
    ASSERT_EQ(sess->state(), fsm_state::Disconnected)
        << "a persistent retain failure must transition to Disconnected";

    // The trap: at disconnect time, the reconcile has already run inside
    // store_then_emit's fatal branch — peek_outbound() reads back k, NOT
    // k+1 (assign_outbound already advanced the manager past k before the
    // failed store call; without the reconcile this would read k+1). This is
    // the single discriminating assertion for the reconcile itself.
    EXPECT_EQ(sess->seqnum_mgr_test_access().peek_outbound(), k)
        << "the reconcile must reseed the wire counter down to the durable value k=" << k
        << " at disconnect time, before any reconnect";
    EXPECT_EQ(sess->seqnum_mgr_test_access().next_inbound_unsafe(), inbound_before)
        << "the reconcile is outbound-only; inbound sequencing must be unaffected";

    // In-process reconnect: drive_reconnect() over the mock transport is the
    // ONLY vehicle that can witness the reconcile (a fresh-Session restart
    // would re-hydrate next_outbound from the unchanged durable file,
    // bypassing the in-memory reconcile entirely).
    auto reconnect_r = asio::co_spawn(sx_, sess->drive_reconnect(), asio::use_future).get();
    ASSERT_TRUE(reconnect_r.has_value())
        << "drive_reconnect() must succeed (mock transport connect+handshake); "
           "no repeating disconnect";

    ASSERT_NE(transport_fac->last_transport, nullptr);
    auto recon_bytes = transport_fac->last_transport->outbound_bytes_seen();
    ASSERT_FALSE(recon_bytes.empty()) << "reconnect must re-emit an initiator Logon";

    // FR-007: clean resume at k — no gap, no reuse, and (plain, no reset
    // knob) no 141=Y.
    EXPECT_EQ(extract_tag(recon_bytes, 34), std::to_string(k))
        << "reconnect Logon must resume cleanly at the reconciled seq k=" << k;
    EXPECT_TRUE(extract_tag(recon_bytes, 141).empty())
        << "plain persistent session (no reset knob) must NOT carry 141=Y on reconnect";

    EXPECT_EQ(sess->seqnum_mgr_test_access().next_inbound_unsafe(), inbound_before)
        << "post-reconnect inbound sequencing must remain unaffected (reconcile is "
           "outbound-only; no reset ran on this variant)";

    // Teardown; call alone (not its result) drains the detached liveness loop.
    (void)asio::co_spawn(sx_, sess->close(fixpp::session::close_mode::terminal), asio::use_future)
        .get();
}

TEST_F(StoreFailReconcileTest, VariantB_ResetOnLogon_ReconnectLogonAtOneWellFormed) {
    std::vector<std::vector<std::byte>> wire;
    auto transport_fac = std::make_shared<MockReconnectFactory>();
    auto cfg = make_initiator_cfg(reset_seqnum_policy::bilateral_lenient,
                                  /*reset_on_logon=*/true, transport_fac);
    cfg.transport_send = [&](std::span<const std::byte> f) {
        wire.emplace_back(f.begin(), f.end());
    };

    auto sess = std::make_unique<Session>(engine_, cfg);

    auto open_r = asio::co_spawn(sx_, sess->open(), asio::use_future).get();
    ASSERT_TRUE(open_r.has_value()) << "open() must succeed";
    ASSERT_EQ(sess->state(), fsm_state::LogonSent);

    auto peer_logon = make_logon("FIX.4.2", 1, "ACCEPTR", "INITR");
    auto logon_r =
        asio::co_spawn(sx_, sess->on_inbound_frame(std::span<const std::byte>(peer_logon)),
                       asio::use_future)
            .get();
    ASSERT_TRUE(logon_r.has_value()) << "peer Logon-ack must be accepted";
    ASSERT_EQ(sess->state(), fsm_state::Active);

    const auto inbound_before = sess->seqnum_mgr_test_access().next_inbound_unsafe();

    auto payload_pre = make_app_payload("ORD-PRE");
    auto pre_r =
        asio::co_spawn(sx_, sess->send(std::span<const std::byte>(payload_pre)), asio::use_future)
            .get();
    ASSERT_TRUE(pre_r.has_value()) << "the baseline (pre-failure) send must succeed";

    const std::uint32_t k = sess->seqnum_mgr_test_access().peek_outbound();

    fixpp::session::arm_force_store_pwrite_fail_once();
    auto payload_k = make_app_payload("ORD-K");
    auto k_r =
        asio::co_spawn(sx_, sess->send(std::span<const std::byte>(payload_k)), asio::use_future)
            .get();

    ASSERT_GE(fixpp::session::read_and_reset_store_pwrite_fail_count(), 1)
        << "the FileStore pwrite fault-injection seam must have fired for message k=" << k;
    ASSERT_FALSE(k_r.has_value()) << "send() for message k must fail closed";
    ASSERT_EQ(sess->state(), fsm_state::Disconnected)
        << "a persistent retain failure must transition to Disconnected";

    EXPECT_EQ(sess->seqnum_mgr_test_access().peek_outbound(), k)
        << "the reconcile must reseed the wire counter down to k=" << k
        << " at disconnect time, regardless of the reset_on_logon knob (the durable "
           "reset only runs later, at the next Logon emission)";
    EXPECT_EQ(sess->seqnum_mgr_test_access().next_inbound_unsafe(), inbound_before)
        << "the reconcile is outbound-only; inbound sequencing must be unaffected at "
           "disconnect time (the durable reset has not run yet)";

    auto reconnect_r = asio::co_spawn(sx_, sess->drive_reconnect(), asio::use_future).get();
    ASSERT_TRUE(reconnect_r.has_value())
        << "drive_reconnect() must succeed (mock transport connect+handshake); "
           "no repeating disconnect";

    ASSERT_NE(transport_fac->last_transport, nullptr);
    auto recon_bytes = transport_fac->last_transport->outbound_bytes_seen();
    ASSERT_FALSE(recon_bytes.empty()) << "reconnect must re-emit an initiator Logon";

    // reset_on_logon's durable reset (reset_seqnums_to_one_durable) runs before the
    // reconnect Logon and overrides the outbound counter to 1 — the
    // reconcile from k is neutral (superseded by the reset). Logon must be
    // 34=1 + 141=Y, well-formed.
    EXPECT_EQ(extract_tag(recon_bytes, 34), "1")
        << "reset_on_logon must override the reconciled outbound counter to 1 on "
           "reconnect (the reconcile is neutral vs the durable reset)";
    EXPECT_EQ(extract_tag(recon_bytes, 141), "Y")
        << "reset_on_logon must emit 141=Y on the reconnect Logon (OR-of-three "
           "predicate, seqnums at {1,1} post-reset)";

    // reset_seqnums_to_one_durable resets BOTH counters — inbound is now 1,
    // NOT inbound_before (unlike Variant A/C, where no reset runs).
    EXPECT_EQ(sess->seqnum_mgr_test_access().next_inbound_unsafe(), 1U)
        << "reset_on_logon's durable reset overrides BOTH counters to {1,1}; "
           "inbound sequencing after reconnect must be 1, not the pre-reset value";

    // Teardown; call alone (not its result) drains the detached liveness loop.
    (void)asio::co_spawn(sx_, sess->close(fixpp::session::close_mode::terminal), asio::use_future)
        .get();
}

TEST_F(StoreFailReconcileTest, VariantC_BilateralStrictDefault_RegressionGuardNotClean) {
    std::vector<std::vector<std::byte>> wire;
    auto transport_fac = std::make_shared<MockReconnectFactory>();
    // bilateral_strict IS the production default (SessionConfig::reset_seqnum_policy_field's
    // default) — pass it explicitly here for test clarity, no reset knob.
    auto cfg = make_initiator_cfg(reset_seqnum_policy::bilateral_strict,
                                  /*reset_on_logon=*/false, transport_fac);
    cfg.transport_send = [&](std::span<const std::byte> f) {
        wire.emplace_back(f.begin(), f.end());
    };

    auto sess = std::make_unique<Session>(engine_, cfg);

    auto open_r = asio::co_spawn(sx_, sess->open(), asio::use_future).get();
    ASSERT_TRUE(open_r.has_value()) << "open() must succeed";
    ASSERT_EQ(sess->state(), fsm_state::LogonSent);
    // bilateral_strict unconditionally emits 141=Y in our own Logon; confirm
    // that so the peer-ack requirement below is understood, not assumed.
    ASSERT_FALSE(wire.empty());
    ASSERT_EQ(extract_tag(wire.front(), 141), "Y")
        << "bilateral_strict must unconditionally emit 141=Y on the cold-open Logon";

    // bilateral_strict REQUIRES the peer's ack to also carry 141=Y, else the
    // initiator disconnects with session_seqnum_reset_mismatch (the RC#C bilateral_strict
    // initiator-path guard).
    auto peer_logon = make_logon_reset("FIX.4.2", 1, "ACCEPTR", "INITR");
    auto logon_r =
        asio::co_spawn(sx_, sess->on_inbound_frame(std::span<const std::byte>(peer_logon)),
                       asio::use_future)
            .get();
    ASSERT_TRUE(logon_r.has_value()) << "peer Logon-ack (with 141=Y) must be accepted";
    ASSERT_EQ(sess->state(), fsm_state::Active);

    const auto inbound_before = sess->seqnum_mgr_test_access().next_inbound_unsafe();

    auto payload_pre = make_app_payload("ORD-PRE");
    auto pre_r =
        asio::co_spawn(sx_, sess->send(std::span<const std::byte>(payload_pre)), asio::use_future)
            .get();
    ASSERT_TRUE(pre_r.has_value()) << "the baseline (pre-failure) send must succeed";

    const std::uint32_t k = sess->seqnum_mgr_test_access().peek_outbound();
    ASSERT_GT(k, 1U) << "the failing message k must be non-1 for this variant to exercise "
                        "the L-029-3 cold-open shape";

    fixpp::session::arm_force_store_pwrite_fail_once();
    auto payload_k = make_app_payload("ORD-K");
    auto k_r =
        asio::co_spawn(sx_, sess->send(std::span<const std::byte>(payload_k)), asio::use_future)
            .get();

    ASSERT_GE(fixpp::session::read_and_reset_store_pwrite_fail_count(), 1)
        << "the FileStore pwrite fault-injection seam must have fired for message k=" << k;
    ASSERT_FALSE(k_r.has_value()) << "send() for message k must fail closed";
    ASSERT_EQ(sess->state(), fsm_state::Disconnected)
        << "a persistent retain failure must transition to Disconnected";

    EXPECT_EQ(sess->seqnum_mgr_test_access().peek_outbound(), k)
        << "the reconcile must reseed the wire counter down to k=" << k
        << " at disconnect time (bilateral_strict has no durable reset on this path)";
    EXPECT_EQ(sess->seqnum_mgr_test_access().next_inbound_unsafe(), inbound_before)
        << "the reconcile is outbound-only; inbound sequencing must be unaffected";

    auto reconnect_r = asio::co_spawn(sx_, sess->drive_reconnect(), asio::use_future).get();
    ASSERT_TRUE(reconnect_r.has_value())
        << "drive_reconnect() must succeed (mock transport connect+handshake); "
           "no repeating disconnect";

    ASSERT_NE(transport_fac->last_transport, nullptr);
    auto recon_bytes = transport_fac->last_transport->outbound_bytes_seen();
    ASSERT_FALSE(recon_bytes.empty()) << "reconnect must re-emit an initiator Logon";

    // REGRESSION GUARD ONLY (per quickstart.md W3 / research.md D4): under
    // bilateral_strict there is no durable reset, so the reconnect Logon
    // carries 34=k (k>1) WITH 141=Y — this IS the pre-existing, DEFERRED
    // L-029-3 malformed-Logon limitation (behaviors-and-limitations.md:
    // 1252-1265), not a 059 regression. 059's reconcile is neutral vs
    // no-reconcile here: both the reconciled k and an un-reconciled k+1 are
    // non-1, so the malformed-Logon shape is unchanged by 059. We do NOT
    // assert clean recovery in this variant
    // ([[feedback_coverage_push_enshrines_bugs]] — asserting a "clean"
    // recovery here would enshrine the wrong behaviour as a passing test).
    EXPECT_EQ(extract_tag(recon_bytes, 34), std::to_string(k))
        << "059 does not worsen L-029-3: the reconnect Logon carries the "
           "RECONCILED (non-1) seq k="
        << k
        << ", matching the pre-existing "
           "malformed-Logon shape (an un-reconciled k+1 would also be non-1)";
    EXPECT_EQ(extract_tag(recon_bytes, 141), "Y")
        << "bilateral_strict unconditionally emits 141=Y on reconnect; combined with "
           "34=k (k>1) this IS the pre-existing L-029-3 malformed Logon, not a 059 "
           "regression";

    EXPECT_EQ(sess->seqnum_mgr_test_access().next_inbound_unsafe(), inbound_before)
        << "post-reconnect inbound sequencing must remain unaffected (no reset "
           "runs under bilateral_strict without a reset knob)";

    // Teardown; call alone (not its result) drains the detached liveness loop.
    (void)asio::co_spawn(sx_, sess->close(fixpp::session::close_mode::terminal), asio::use_future)
        .get();
}

// ── 092-garbled-frame-reject T052 — the inbound seqnum_max bound on a FileStore ───
//
// The same five consuming frames as test_validation_compat_toggles.cpp's SeqnumMax_*
// cells, over this fixture's FileStore-backed initiator. A Reset-mode SequenceReset sets
// NextNumIn to seqnum_max (4294967295); the cell asserts Active and NextNumIn ==
// 4294967295 so that a disconnect from another cause cannot satisfy it, then sends a frame at
// 4294967295 that would consume NextNumIn and asserts: Disconnected; NextNumIn still
// 4294967295; no outbound frame after it (no Reject); no fromApp or fromAdmin; and a
// durable inbound counter the frame did not move. MessageStore has no set-to-value
// operation, so the SequenceReset jump is never persisted (research R-14): the durable
// counter holds what the peer's Logon at 1 persisted, the Logon's successor. Session
// exposes no store accessor, so the durable counter is read by reopening a FileStore over
// dir_ after the session is closed and destroyed, and calling next_seqnum(inbound, false).
// To check the cells can fail, delete the bound in SeqnumManager::check_inbound in a
// scratch copy (quickstart §2 "Inbound bound deletion").
// Anchors: specs/092-garbled-frame-reject spec FR-019, SC-010; contract C-3 I-7, C-5 L-7;
//          research R-14.

class CountingApp092 final : public fixpp::session::Application {
public:
    int from_app_count{0};
    int from_admin_count{0};

    fixpp::core::expected_t<void> fromApp(
        const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& /*msg*/,
        const fixpp::session::SessionId& /*id*/) override {
        ++from_app_count;
        return {};
    }

    fixpp::core::expected_t<void> fromAdmin(
        const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& /*msg*/,
        const fixpp::session::SessionId& /*id*/) override {
        ++from_admin_count;
        return {};
    }
};

constexpr fixpp::session::seqnum_t kSeqMax092 = fixpp::session::seqnum_max;

class StoreFailReconcileSeqnumMax092 : public StoreFailReconcileTest {
protected:
    void run_cell(const std::vector<std::byte>& frame, std::string_view what) {
        std::vector<std::vector<std::byte>> wire;
        auto app = std::make_shared<CountingApp092>();
        engine_.application = app;
        auto cfg =
            make_initiator_cfg(reset_seqnum_policy::bilateral_lenient,
                               /*reset_on_logon=*/false, std::make_shared<MockReconnectFactory>());
        cfg.transport_send = [&](std::span<const std::byte> f) {
            wire.emplace_back(f.begin(), f.end());
        };

        auto sess = std::make_unique<Session>(engine_, cfg);
        auto open_r = asio::co_spawn(sx_, sess->open(), asio::use_future).get();
        ASSERT_TRUE(open_r.has_value()) << what << ": open() must succeed";

        auto feed = [&](const std::vector<std::byte>& f) {
            return asio::co_spawn(sx_, sess->on_inbound_frame(std::span<const std::byte>(f)),
                                  asio::use_future)
                .get();
        };
        ASSERT_TRUE(feed(make_logon("FIX.4.2", 1, "ACCEPTR", "INITR")).has_value());
        ASSERT_EQ(sess->state(), fsm_state::Active) << what << ": precondition: Active";
        (void)feed(make_fix_frame("FIX.4.2", "4", 2, "ACCEPTR", "INITR",
                                  field(36, std::to_string(kSeqMax092))));
        ASSERT_EQ(sess->state(), fsm_state::Active)
            << what << ": precondition: Active after the Reset-mode SequenceReset";
        ASSERT_EQ(sess->seqnum_mgr_test_access().next_inbound_unsafe(), kSeqMax092)
            << what << ": precondition: NextNumIn == 4294967295";
        wire.clear();
        const int from_app_before = app->from_app_count;
        const int from_admin_before = app->from_admin_count;

        // The result is not asserted: the plain application frame's too-low fallback arm
        // also disconnects, and returns ok.
        (void)feed(frame);
        EXPECT_EQ(sess->state(), fsm_state::Disconnected)
            << what << ": FR-019: the message at NextNumIn = seqnum_max must end the session";
        EXPECT_EQ(sess->seqnum_mgr_test_access().next_inbound_unsafe(), kSeqMax092)
            << what << ": FR-019: NextNumIn must stay 4294967295, never wrap";
        EXPECT_TRUE(wire.empty()) << what << ": FR-019: the disconnect is silent (no Reject)";
        EXPECT_EQ(app->from_app_count, from_app_before) << what << ": no fromApp";
        EXPECT_EQ(app->from_admin_count, from_admin_before) << what << ": no fromAdmin";

        (void)asio::co_spawn(sx_, sess->close(fixpp::session::close_mode::terminal),
                             asio::use_future)
            .get();
        sess.reset();

        FileStore::Config fcfg;
        fcfg.directory = dir_;
        fcfg.sender_comp_id = "INITR";
        fcfg.target_comp_id = "ACCEPTR";
        fcfg.max_frame_bytes = 4096;
        fcfg.file_io_executor = sx_;
        FileStoreFactory reopen{fcfg};
        auto durable =
            asio::co_spawn(
                sx_,
                [&]() -> asio::awaitable<fixpp::core::expected_t<fixpp::session::seqnum_t>> {
                    auto minted = reopen.make("INITR", "ACCEPTR", nullptr, 1ULL << 30, sx_);
                    if (!minted) {
                        co_return std::unexpected(minted.error());
                    }
                    co_return co_await (*minted)->next_seqnum(fixpp::session::direction_t::inbound,
                                                              false);
                },
                asio::use_future)
                .get();
        ASSERT_TRUE(durable.has_value()) << what << ": reopening the FileStore must succeed";
        EXPECT_EQ(*durable, fixpp::session::seqnum_t{2})
            << what << ": the frame at 4294967295 must not move the durable inbound counter";
    }
};

TEST_F(StoreFailReconcileSeqnumMax092, Application_Disconnects) {
    run_cell(make_fix_frame("FIX.4.2", "D", kSeqMax092, "ACCEPTR", "INITR", field(11, "O1")),
             "application");
}

TEST_F(StoreFailReconcileSeqnumMax092, Heartbeat_Disconnects) {
    run_cell(make_fix_frame("FIX.4.2", "0", kSeqMax092, "ACCEPTR", "INITR"), "Heartbeat");
}

// A Reject(35=3), not a Heartbeat: Guard 4's error branch tests the Heartbeat first.
TEST_F(StoreFailReconcileSeqnumMax092, PossDupAdmin_Disconnects) {
    run_cell(make_fix_frame("FIX.4.2", "3", kSeqMax092, "ACCEPTR", "INITR",
                            field(43, "Y") + field(122, "20240101-00:00:00.000") + field(45, "1")),
             "PossDupFlag=Y admin");
}

TEST_F(StoreFailReconcileSeqnumMax092, Issue423PossDupAppNo122_Disconnects) {
    run_cell(make_fix_frame("FIX.4.2", "D", kSeqMax092, "ACCEPTR", "INITR",
                            field(43, "Y") + field(11, "O1")),
             "#423 Reject site (43=Y, no 122)");
}

TEST_F(StoreFailReconcileSeqnumMax092, D5FaultyApplication_Disconnects) {
    run_cell(make_fix_frame("FIX.4.2", "D", kSeqMax092, "ACCEPTR", "INITR",
                            field(11, "O1") + "9x9=1\x01"),
             "D-5 faulty application");
}

}  // namespace
