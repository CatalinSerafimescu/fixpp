// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_refresh_on_logon.cpp
//
// 025-refresh-on-logon unit test suite.
//
// Phase 1 (RED-first setup) — T001/T010/T011:
//   skeleton + W1/W2 RED witnesses.
//   Real GREEN witnesses (W3..W8) are added in later phases.
//
// Harness shape mirrors test_persistent_seqnum_hydrate.cpp (029):
//   FaultStore / FaultStoreFactory / OutboundCapture / Fixture
//   make_initiator / make_reconnect_initiator helpers.
//
// RED witness design:
//   W1 and W2 use the drive_reconnect() path (the real "2nd-logon" vehicle):
//     1. Build initiator with FaultStore {seeded_in, seeded_out}, call open()
//        → cold one-shot hydrate fires, hydrated_=true, manager set from store.
//     2. set_counters_for_test to lower/raise the live counters (diverge from store).
//     3. Call session->drive_reconnect() with a mock transport factory that
//        completes connect+handshake synchronously; this triggers
//        install_reconnected_transport → LogonSent → emit_initiator_logon_()
//        → ensure_hydrated_(true) with hydrated_=true already set.
//     4. Without T004 force-param: hydrated_ latch fires → no re-read → counters
//        stay at the lowered/raised live values → assertions fail RED.
//     5. With T004-T006 (next slice): force=true bypasses latch → store re-read
//        → counters restored to store values → assertions pass GREEN.
//
// Anchors: specs/025-refresh-on-logon/data-model.md W1/W2;
//          specs/025-refresh-on-logon/contracts/refresh-knob.md C1/C2/C3/C4;
//          tests/session/test_persistent_seqnum_hydrate.cpp (029 harness template).

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/session/application.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/message_store.hpp>
#include <fixpp/session/message_store_factory.hpp>
#include <fixpp/session/retrieve_visitor.hpp>
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

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"

using namespace std::chrono_literals;

namespace {

// ── Frame-building helpers ────────────────────────────────────────────────────

static std::string field(int tag, std::string_view val) {
    return std::to_string(tag) + "=" + std::string(val) + "\x01";
}

static std::vector<std::byte> make_fix_frame(std::string_view begin_string,
                                             std::string_view msg_type, std::uint32_t seq,
                                             std::string_view sender, std::string_view target,
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
                                         std::string_view s, std::string_view t, int hbt = 30) {
    std::string extra;
    extra += field(98, "0");
    extra += field(108, std::to_string(hbt));
    return make_fix_frame(bs, "A", seq, s, t, extra);
}

// ── Outbound capture ──────────────────────────────────────────────────────────

struct OutboundCapture {
    std::vector<std::vector<std::byte>> frames;
    void operator()(std::span<const std::byte> data) {
        frames.emplace_back(data.begin(), data.end());
    }
};

// ── FaultStore: fault-injecting / callback-observing test MessageStore ────────
//
// Pre-seeded inbound/outbound counters returned by next_seqnum(dir, false).
// Observable call_count tracks total next_seqnum calls (reads + writes).
// Mirrors the 029 harness in test_persistent_seqnum_hydrate.cpp.

using fixpp::session::direction_t;
using fixpp::session::MessageStore;
using fixpp::session::MessageStoreFactory;
using fixpp::session::retrieve_visitor;
using fixpp::session::seqnum_t;
using fixpp::session::visit_result;

class FaultStore final : public MessageStore {
public:
    explicit FaultStore(seqnum_t seeded_inbound = 1, seqnum_t seeded_outbound = 1)
        : MessageStore(flush_thunk_for<FaultStore>()),
          next_inbound_(seeded_inbound),
          next_outbound_(seeded_outbound) {}

    // Observable state for witnesses:
    mutable int call_count{0};  // total next_seqnum calls (reads + writes)

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> store(
        seqnum_t /*seq*/, std::span<const std::byte> /*frame*/,
        direction_t /*dir*/) noexcept override {
        co_return fixpp::core::expected_t<void>{};
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> retrieve(
        seqnum_t /*begin*/, seqnum_t /*end*/, direction_t /*dir*/,
        retrieve_visitor& /*visitor*/) noexcept override {
        co_return std::unexpected(fixpp::core::error::store_seqnum_gap);
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<seqnum_t>> next_seqnum(
        direction_t dir, bool increment) noexcept override {
        ++call_count;
        if (increment) {
            if (dir == direction_t::inbound) {
                ++next_inbound_;
                co_return next_inbound_ - 1U;
            } else {
                ++next_outbound_;
                co_return next_outbound_ - 1U;
            }
        } else {
            co_return (dir == direction_t::inbound) ? next_inbound_ : next_outbound_;
        }
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> reset() noexcept override {
        next_inbound_ = 1;
        next_outbound_ = 1;
        co_return fixpp::core::expected_t<void>{};
    }

    [[nodiscard]] seqnum_t current_inbound() const noexcept { return next_inbound_; }
    [[nodiscard]] seqnum_t current_outbound() const noexcept { return next_outbound_; }

private:
    seqnum_t next_inbound_;
    seqnum_t next_outbound_;
};

// FaultStoreFactory: wraps FaultStore, yields_persistent_store() = true.
class FaultStoreFactory final : public MessageStoreFactory {
public:
    explicit FaultStoreFactory(seqnum_t seeded_inbound = 1, seqnum_t seeded_outbound = 1)
        : seeded_inbound_(seeded_inbound), seeded_outbound_(seeded_outbound) {}

    mutable FaultStore* last_store{nullptr};

    [[nodiscard]] bool yields_persistent_store() const noexcept override { return true; }

    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<MessageStore>> make(
        std::string_view /*sender*/, std::string_view /*target*/, std::pmr::memory_resource* /*mr*/,
        std::size_t /*max_store_memory_bytes*/,
        asio::any_io_executor /*file_io_executor*/) noexcept override {
        auto store = std::make_unique<FaultStore>(seeded_inbound_, seeded_outbound_);
        last_store = store.get();
        return store;
    }

private:
    seqnum_t seeded_inbound_;
    seqnum_t seeded_outbound_;
};

// ── MockReconnectFactory: TransportFactory returning mock_transport ────────────
//
// Used to drive session->drive_reconnect() in unit tests.
// make() returns a mock_transport with handshake_succeeds=true and empty inbound
// (so async_read_some immediately returns EOF after the 2nd Logon is emitted).
// cert_source_snapshot() returns nullptr (no cert rotation; step 2 safe with snap=null).

class MockReconnectFactory final : public fixpp::transport::TransportFactory {
public:
    std::atomic<int> make_call_count{0};

    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<fixpp::transport::Transport>> make(
        asio::any_io_executor exec, fixpp::tls::SslCtxConfig /*ssl_cfg*/,
        std::pmr::memory_resource* /*mr*/) noexcept override {
        ++make_call_count;
        fixpp::transport::test::Script script;
        script.handshake_succeeds = true;
        // Empty inbound → immediate EOF after connect+handshake.
        return std::make_unique<fixpp::transport::test::mock_transport>(std::move(exec),
                                                                        std::move(script));
    }

    [[nodiscard]] fixpp::core::expected_t<void> reload_credentials(
        std::shared_ptr<fixpp::tls::cert_source> /*new_source*/) noexcept override {
        return {};
    }

    [[nodiscard]] std::shared_ptr<fixpp::tls::cert_source> cert_source_snapshot()
        const noexcept override {
        return nullptr;
    }

    [[nodiscard]] bool yields_persistent_store() const noexcept { return true; }
};

// ── Fixture ───────────────────────────────────────────────────────────────────

struct Fixture {
    asio::io_context ioc;
    OutboundCapture capture;
    fixpp::core::EngineConfig eng;
    fixpp::session::SessionConfig cfg;
    std::unique_ptr<fixpp::session::Session> session;

    void feed(const std::vector<std::byte>& frame) {
        auto fut = asio::co_spawn(ioc, session->on_inbound_frame(frame), asio::use_future);
        ioc.run_for(500ms);
        ioc.restart();
        (void)fut.get();
    }

    void clear_capture() { capture.frames.clear(); }
};

// ── make_initiator: basic initiator without reconnect factory ─────────────────
//
// Used by the SkeletonBuilds test. Mirrors 029's make_initiator.

static std::unique_ptr<Fixture> make_initiator(
    std::shared_ptr<MessageStoreFactory> store_factory,
    bool refresh_on_logon = false) {
    auto fix = std::make_unique<Fixture>();

    fix->cfg.role = fixpp::session::session_role::initiator;
    fix->cfg.sender_comp_id = "CLI";
    fix->cfg.target_comp_id = "SRV";
    fix->cfg.begin_string = "FIX.4.4";
    fix->cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
    fix->cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
    fix->cfg.heartbeat_interval = std::chrono::seconds{0};
    fix->cfg.executor_override = fix->ioc.get_executor();
    fix->cfg.store_factory = std::move(store_factory);
    fix->cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
    fix->cfg.refresh_on_logon = refresh_on_logon;
    fix->cfg.transport_send = [&fix = *fix](std::span<const std::byte> data) { fix.capture(data); };

    fix->session = std::make_unique<fixpp::session::Session>(fix->eng, fix->cfg);

    auto open_fut = asio::co_spawn(fix->ioc, fix->session->open(), asio::use_future);
    fix->ioc.run_for(2s);
    fix->ioc.restart();
    (void)open_fut.get();

    EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::LogonSent)
        << "make_initiator: session must be LogonSent after open()";

    return fix;
}

// ── make_reconnect_initiator: initiator with MockReconnectFactory ─────────────
//
// Builds an initiator session with:
//   - FaultStoreFactory (persistent, seeded at seeded_in/seeded_out)
//   - MockReconnectFactory (transport; allows drive_reconnect() to succeed)
//   - Dummy reconnect_endpoint (mock transport doesn't use it)
//   - bilateral_lenient (required for refresh_on_logon to not be suppressed)
//   - refresh_on_logon = true (the knob under test)
//
// Returns the Fixture + a pointer to the FaultStore and MockReconnectFactory
// so tests can observe call_count and make_call_count.

struct ReconnectInitiatorFixture {
    std::unique_ptr<Fixture> fix;
    FaultStore* store{nullptr};           // pointer into the FaultStoreFactory's store
    MockReconnectFactory* transport_fac{nullptr};  // owning ptr kept in cfg_ via shared_ptr
};

static ReconnectInitiatorFixture make_reconnect_initiator(
    seqnum_t seeded_in, seqnum_t seeded_out) {
    ReconnectInitiatorFixture result;
    result.fix = std::make_unique<Fixture>();
    auto& fix = *result.fix;

    auto store_factory = std::make_shared<FaultStoreFactory>(seeded_in, seeded_out);
    auto transport_factory = std::make_shared<MockReconnectFactory>();
    result.transport_fac = transport_factory.get();

    fix.cfg.role = fixpp::session::session_role::initiator;
    fix.cfg.sender_comp_id = "CLI";
    fix.cfg.target_comp_id = "SRV";
    fix.cfg.begin_string = "FIX.4.4";
    fix.cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
    fix.cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
    fix.cfg.heartbeat_interval = std::chrono::seconds{0};  // disable heartbeat timer
    fix.cfg.executor_override = fix.ioc.get_executor();
    fix.cfg.store_factory = store_factory;
    fix.cfg.transport_factory_override = transport_factory;
    fix.cfg.reconnect_endpoint = fixpp::transport::Endpoint{"127.0.0.1", 19099};
    fix.cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
    fix.cfg.refresh_on_logon = true;
    fix.cfg.transport_send = [&fix](std::span<const std::byte> data) { fix.capture(data); };

    fix.session = std::make_unique<fixpp::session::Session>(fix.eng, fix.cfg);

    // open() — cold one-shot hydrate fires (hydrated_=false → store reads → hydrated_=true).
    // The Logon is emitted via transport_send_ (pre-live path at cold open).
    auto open_fut = asio::co_spawn(fix.ioc, fix.session->open(), asio::use_future);
    fix.ioc.run_for(2s);
    fix.ioc.restart();
    (void)open_fut.get();

    EXPECT_EQ(fix.session->state(), fixpp::session::fsm_state::LogonSent)
        << "make_reconnect_initiator: session must be LogonSent after open()";

    // Capture the FaultStore pointer for observable state assertions.
    result.store = store_factory->last_store;
    EXPECT_NE(result.store, nullptr) << "FaultStoreFactory must have minted a store";

    return result;
}

}  // namespace

// ── Phase 1 (T001) — SkeletonBuilds: compile+link+ctest smoke ────────────────

TEST(RefreshOnLogon, SkeletonBuilds) {
    // Placeholder: confirms the skeleton compiles + links + ctest registers
    // the target. Real assertions (W1-W8) are added in later phases.
    SUCCEED();
}

// ── Phase 1 (T010) — W1 RED: StoreAboveLive — store-wins UP ──────────────────
//
// Pre-condition:
//   - Persistent FaultStore seeded at {in=50, out=60}.
//   - Initiator with refresh_on_logon=true, bilateral_lenient.
//   - Cold open: hydrated_=false → ensure_hydrated_ fires → manager = {50, 60};
//     outbound Logon at seq=60 advances manager.outbound to 61.
//   - set_counters_for_test(40, 42): manager diverges to {40, 42}.
//     (Simulates the standby case: store reflects the primary's higher seqnums;
//      the standby's in-memory manager is behind after its local processing.)
//   - drive_reconnect(): mock transport connect+handshake succeed →
//     install_reconnected_transport → LogonSent →
//     emit_initiator_logon_() → ensure_hydrated_(true, force=??).
//
// Without T004 (force param not yet added): hydrated_=true → latch fires →
//   manager stays {40, 42} → inbound=40 != 50 → FAILS RED.
//
// With T004-T006 (next slice): force=true → latch bypassed → store re-read →
//   manager = {50, 60} → inbound=50 == 50 → PASSES GREEN.
//
// Anchors: data-model.md W1; contracts/refresh-knob.md C2/C4; INV-RoL-4 (store-wins UP).

TEST(RefreshOnLogon, W1_StoreAboveLive_RED) {
    // Seed store above the live divergence point: store={50,60}, live will be {40,42}.
    auto result = make_reconnect_initiator(/*seeded_in=*/50, /*seeded_out=*/60);
    auto& fix = *result.fix;
    auto* store = result.store;
    ASSERT_NE(store, nullptr);

    // Cold open completed; ensure_hydrated_ must have fired exactly once:
    // 2 reads (inbound read + outbound read).
    ASSERT_EQ(store->call_count, 2)
        << "W1 precondition: cold open must have issued exactly 2 store reads "
           "(in+out); call_count=" << store->call_count;

    // Manager after cold open: hydrated to {50, 60}; then Logon at seq=60
    // advances outbound to 61. Confirm the cold hydrate was applied.
    {
        auto& mgr = fix.session->seqnum_mgr_test_access();
        ASSERT_EQ(mgr.next_inbound_unsafe(), static_cast<seqnum_t>(50))
            << "W1 precondition: cold hydrate must set next_inbound=50";
        // After Logon emission at seq=60, outbound advanced to 61.
        ASSERT_EQ(mgr.peek_outbound(), static_cast<seqnum_t>(61))
            << "W1 precondition: after cold-open Logon(34=60), outbound must be 61";
    }

    // Simulate standby divergence: lower the live manager to {40, 42}.
    // This represents the standby's in-memory state after processing fewer
    // frames than the primary's store reflects.
    fix.session->seqnum_mgr_test_access().set_counters_for_test(
        /*next_inbound=*/40, /*next_outbound=*/42);

    {
        auto& mgr = fix.session->seqnum_mgr_test_access();
        ASSERT_EQ(mgr.next_inbound_unsafe(), static_cast<seqnum_t>(40))
            << "W1 setup: set_counters_for_test must lower inbound to 40";
        ASSERT_EQ(mgr.peek_outbound(), static_cast<seqnum_t>(42))
            << "W1 setup: set_counters_for_test must lower outbound to 42";
    }

    // Drive the 2nd logon via drive_reconnect().
    // The mock transport factory completes connect+handshake synchronously.
    // emit_initiator_logon_() runs with hydrated_=true already set.
    const int call_count_before = store->call_count;

    auto reconnect_fut = asio::co_spawn(
        fix.ioc, fix.session->drive_reconnect(), asio::use_future);
    fix.ioc.run_for(3s);
    fix.ioc.restart();

    // CRITICAL VEHICLE CHECK: drive_reconnect() must succeed (reach emit_initiator_logon_).
    // If this fails, the mock transport path broke before ensure_hydrated_ ran — the RED
    // witness would be vacuous. A successful reconnect returns ok from emit_initiator_logon_
    // (the hydrated_ latch short-circuits but still builds+sends the Logon without error).
    {
        auto rr = reconnect_fut.get();
        ASSERT_TRUE(rr.has_value())
            << "W1 vehicle: drive_reconnect() must succeed (mock transport connect+handshake). "
               "If this fails, ensure_hydrated_ was never reached and the RED witness is invalid. "
               "error=" << (rr.has_value() ? "ok" : "failed");
    }

    // Wait for any trailing asio work (EOF handling may transition Disconnected).
    fix.ioc.run_for(1s);
    fix.ioc.restart();

    // ── RED assertions (fail pre-T004; pass post-T004) ────────────────────────

    // W1 store-re-read: store must be re-read (2 additional reads: in+out).
    // RED: hydrated_ latch fires, no re-read, call_count unchanged at 2.
    // GREEN: force=true bypasses latch, 2 more reads, call_count=4.
    EXPECT_EQ(store->call_count, call_count_before + 2)
        << "W1 RED: ensure_hydrated_ must re-read the store (in+out = 2 reads) "
           "on the 2nd logon when refresh_on_logon=true + bilateral_lenient. "
           "WITHOUT T004 (force param), the hydrated_ latch fires and no reads "
           "occur. call_count before=" << call_count_before
        << " expected after=" << (call_count_before + 2)
        << " got=" << store->call_count;

    // W1 inbound post-condition: next_inbound must equal the store's seeded value (50).
    // RED: hydrated_ latch fires, manager stays at {40,42}, next_inbound=40 != 50.
    // GREEN: re-hydrated from store, next_inbound=50.
    EXPECT_EQ(fix.session->seqnum_mgr_test_access().next_inbound_unsafe(),
              static_cast<seqnum_t>(50))
        << "W1 RED: after 2nd logon with refresh_on_logon=true + bilateral_lenient, "
           "next_inbound must equal the store's seeded value (50). "
           "WITHOUT T004, hydrated_ latch prevents re-hydration → stays at live value 40. "
           "Actual next_inbound="
        << fix.session->seqnum_mgr_test_access().next_inbound_unsafe();

    // W1 outbound post-condition: after re-hydrate to out=60, the 2nd Logon emits at 60 →
    // peek_outbound()==61. Without re-hydrate: out=42 → Logon at 42 → peek_outbound()==43.
    // RED: 43 != 61. GREEN: 61 == 61.
    EXPECT_EQ(fix.session->seqnum_mgr_test_access().peek_outbound(),
              static_cast<seqnum_t>(61))
        << "W1 RED: after re-hydrate to out=60 + 2nd Logon emit, peek_outbound must be 61. "
           "WITHOUT T004: latch fires → out stays 42 → 2nd Logon at 42 → peek_outbound=43. "
           "Actual peek_outbound="
        << fix.session->seqnum_mgr_test_access().peek_outbound();
}

// ── Phase 1 (T011) — W2 RED: StoreWinsDown — store-wins DOWN ─────────────────
//
// W2 distinguishes store-wins-UNCONDITIONAL from advance-only (max/clamp) semantics:
//   - Store {in=5, out=6} is BELOW the live values {in=40, out=42}.
//   - store-wins-DOWN: manager is set to the LOWER store values (5, 6).
//   - advance-only: manager would not move DOWN (max(store, live) = live = {40,42}).
//
// RED assertion: next_inbound must equal 5 (store's lower value).
// Without T004: manager stays at 40 (live), failing RED.
// With T004-T006: manager is re-hydrated to 5 (store), passing GREEN.
//
// Primary use case: a primary node has reset its store to {1,1} or advanced
// less than the standby's in-memory state. The standby must follow the
// primary's store-side authoritative value, even if it means going DOWN.
//
// Anchors: data-model.md W2; contracts/refresh-knob.md C4 (INV-RoL-4, store-wins DOWN).

TEST(RefreshOnLogon, W2_StoreWinsDown_RED) {
    // Seed store BELOW the live divergence point: store={5,6}, live will be {40,42}.
    auto result = make_reconnect_initiator(/*seeded_in=*/5, /*seeded_out=*/6);
    auto& fix = *result.fix;
    auto* store = result.store;
    ASSERT_NE(store, nullptr);

    // Cold open: store={5,6}, ensure_hydrated_ fires, manager={5,6}.
    // Outbound Logon at seq=6 → manager.outbound advances to 7.
    ASSERT_EQ(store->call_count, 2)
        << "W2 precondition: cold open must issue exactly 2 store reads";

    {
        auto& mgr = fix.session->seqnum_mgr_test_access();
        ASSERT_EQ(mgr.next_inbound_unsafe(), static_cast<seqnum_t>(5))
            << "W2 precondition: cold hydrate must set next_inbound=5";
    }

    // Simulate live counter divergence ABOVE the store: {40, 42}.
    // This represents a standby that processed additional frames in RAM (above
    // what is in the store), and needs to revert to the store-authoritative value.
    fix.session->seqnum_mgr_test_access().set_counters_for_test(
        /*next_inbound=*/40, /*next_outbound=*/42);

    {
        auto& mgr = fix.session->seqnum_mgr_test_access();
        ASSERT_EQ(mgr.next_inbound_unsafe(), static_cast<seqnum_t>(40))
            << "W2 setup: set_counters_for_test must raise inbound to 40";
    }

    // Drive the 2nd logon via drive_reconnect() with the mock transport.
    const int call_count_before = store->call_count;

    auto reconnect_fut = asio::co_spawn(
        fix.ioc, fix.session->drive_reconnect(), asio::use_future);
    fix.ioc.run_for(3s);
    fix.ioc.restart();

    // CRITICAL VEHICLE CHECK: drive_reconnect() must succeed (same as W1).
    {
        auto rr = reconnect_fut.get();
        ASSERT_TRUE(rr.has_value())
            << "W2 vehicle: drive_reconnect() must succeed (mock transport connect+handshake). "
               "If this fails, ensure_hydrated_ was never reached and the RED witness is invalid. "
               "error=" << (rr.has_value() ? "ok" : "failed");
    }

    fix.ioc.run_for(1s);
    fix.ioc.restart();

    // ── RED assertions (fail pre-T004; pass post-T004) ────────────────────────

    // W2 store-re-read: same call_count gate as W1.
    EXPECT_EQ(store->call_count, call_count_before + 2)
        << "W2 RED: ensure_hydrated_ must re-read the store on 2nd logon "
           "(store-wins DOWN path). WITHOUT T004, hydrated_ latch fires, no reads. "
           "call_count before=" << call_count_before
        << " expected after=" << (call_count_before + 2)
        << " got=" << store->call_count;

    // W2 inbound post-condition: next_inbound must equal the LOWER store value (5).
    // An advance-only (max/clamp) implementation would FAIL this assertion too —
    // it would NOT lower the inbound counter to 5 even after force-hydration.
    // The store-wins-DOWN semantic (INV-RoL-4) requires unconditional overwrite.
    // RED: hydrated_ latch fires → manager stays at 40 → 40 != 5.
    // GREEN: re-hydrated → manager = 5 (store-wins DOWN).
    EXPECT_EQ(fix.session->seqnum_mgr_test_access().next_inbound_unsafe(),
              static_cast<seqnum_t>(5))
        << "W2 RED: after 2nd logon with refresh_on_logon=true + bilateral_lenient, "
           "next_inbound must equal the store's LOWER seeded value (5). "
           "An advance-only implementation would also fail here — store-wins must "
           "lower the counter unconditionally (INV-RoL-4). "
           "WITHOUT T004: hydrated_ latch → stays at live value 40. "
           "Actual next_inbound="
        << fix.session->seqnum_mgr_test_access().next_inbound_unsafe();

    // W2 outbound post-condition: after re-hydrate to out=6, the 2nd Logon emits at 6 →
    // peek_outbound()==7. Without re-hydrate: out=42 → 2nd Logon at 42 → peek_outbound()==43.
    // RED: 43 != 7. GREEN: 7 == 7.
    EXPECT_EQ(fix.session->seqnum_mgr_test_access().peek_outbound(),
              static_cast<seqnum_t>(7))
        << "W2 RED: after re-hydrate to out=6 (store-wins DOWN) + 2nd Logon emit, "
           "peek_outbound must be 7. WITHOUT T004: latch fires → out stays 42 → "
           "2nd Logon at 42 → peek_outbound=43. "
           "Actual peek_outbound="
        << fix.session->seqnum_mgr_test_access().peek_outbound();
}
