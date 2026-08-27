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
// Variant B — reset_on_logon=true: the durable reset (session.cpp:776-782)
//   overrides BOTH counters to {1,1} before the reconnect Logon; the
//   reconcile is neutral.
// Variant C — bilateral_strict (the DEFAULT policy, no reset knob): NO
//   durable reset exists on this path; the reconnect Logon carries 34=k
//   (k>1) WITH 141=Y — the pre-existing, DEFERRED L-029-3 malformed-Logon
//   limitation (behaviors-and-limitations.md:1252-1265). This variant is a
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
#include <fixpp/session/direction.hpp>
#include <fixpp/session/file_store.hpp>
#include <fixpp/session/file_store_factory.hpp>
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
#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"
#include "support/extract_tag.hpp"

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
// 141=Y (session.cpp:3733-3744).
std::vector<std::byte> make_logon_reset(std::string_view bs, std::uint32_t seq,
                                        std::string_view s, std::string_view t, int hbt = 30) {
    std::string extra;
    extra += field(98, "0");
    extra += field(108, std::to_string(hbt));
    extra += field(141, "Y");
    return make_fix_frame(bs, "A", seq, s, t, extra);
}

// Minimal app payload (35=D). Session::send builds the full wire frame
// (header + MsgSeqNum) around this opaque body.
std::vector<std::byte> make_app_payload(std::string_view clordid) {
    std::string body = "35=D\x01" + std::string(field(11, clordid)) + "54=1\x01"
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
    cfg.transport_send = [&](std::span<const std::byte> f) { wire.emplace_back(f.begin(), f.end()); };

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
        << "the reconcile must reseed the wire counter down to the durable value k="
        << k << " at disconnect time, before any reconnect";
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

    asio::co_spawn(sx_, sess->close(fixpp::session::close_mode::terminal), asio::use_future).get();
}

TEST_F(StoreFailReconcileTest, VariantB_ResetOnLogon_ReconnectLogonAtOneWellFormed) {
    std::vector<std::vector<std::byte>> wire;
    auto transport_fac = std::make_shared<MockReconnectFactory>();
    auto cfg = make_initiator_cfg(reset_seqnum_policy::bilateral_lenient,
                                  /*reset_on_logon=*/true, transport_fac);
    cfg.transport_send = [&](std::span<const std::byte> f) { wire.emplace_back(f.begin(), f.end()); };

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

    // reset_on_logon's durable reset (session.cpp:776-782) runs before the
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

    asio::co_spawn(sx_, sess->close(fixpp::session::close_mode::terminal), asio::use_future).get();
}

TEST_F(StoreFailReconcileTest, VariantC_BilateralStrictDefault_RegressionGuardNotClean) {
    std::vector<std::vector<std::byte>> wire;
    auto transport_fac = std::make_shared<MockReconnectFactory>();
    // bilateral_strict IS the production default (session_config.hpp:231) —
    // pass it explicitly here for test clarity, no reset knob.
    auto cfg = make_initiator_cfg(reset_seqnum_policy::bilateral_strict,
                                  /*reset_on_logon=*/false, transport_fac);
    cfg.transport_send = [&](std::span<const std::byte> f) { wire.emplace_back(f.begin(), f.end()); };

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
    // initiator disconnects with session_seqnum_reset_mismatch (session.cpp:3733-3744).
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
           "RECONCILED (non-1) seq k=" << k << ", matching the pre-existing "
           "malformed-Logon shape (an un-reconciled k+1 would also be non-1)";
    EXPECT_EQ(extract_tag(recon_bytes, 141), "Y")
        << "bilateral_strict unconditionally emits 141=Y on reconnect; combined with "
           "34=k (k>1) this IS the pre-existing L-029-3 malformed Logon, not a 059 "
           "regression";

    EXPECT_EQ(sess->seqnum_mgr_test_access().next_inbound_unsafe(), inbound_before)
        << "post-reconnect inbound sequencing must remain unaffected (no reset "
           "runs under bilateral_strict without a reset knob)";

    asio::co_spawn(sx_, sess->close(fixpp::session::close_mode::terminal), asio::use_future).get();
}

}  // namespace
