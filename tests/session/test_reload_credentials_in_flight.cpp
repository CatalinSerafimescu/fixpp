// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_reload_credentials_in_flight.cpp — T042 [P] [US4] Phase 6
//
// In-process credential rotation witness tests.
//
// Anchors: FR-030 / FR-031 / FR-033 / US4 AC1+AC2 / Clarifications Q4=A /
//          D-11 / data-model §E-7 /
//          [[feedback_weak_ptr_cache_needs_owning_context]] /
//          [[feedback_half_restructure_symmetric_api]].
//
// Test cells:
//
//   Cell 1 — NullReject: Session::reload_credentials(nullptr) returns
//             error::session_invalid_argument (slot 119) per FR-033.
//             [data-model §E-7 validation rule]
//
//   Cell 2 — ForwarderDelegates: Session::reload_credentials(new_source)
//             delegates to TransportFactory::reload_credentials(new_source)
//             exactly once; factory's cert_source_snapshot() returns new_source
//             after the call. [FR-030 / data-model §E-7]
//
//   Cell 3 — StrongRefInvariant: cert_source_snapshot() captured BY VALUE COPY
//             keeps the OLD cert_source alive even after reload_credentials()
//             stores a new source. The strong-ref invariant ensures in-flight
//             handshakes complete against the OLD source.
//             [data-model §E-7 / [[feedback_weak_ptr_cache_needs_owning_context]]]
//
//   Cell 4 — CallCountWitness: N calls to Session::reload_credentials produce
//             exactly N calls to factory.reload_credentials; factory slot holds
//             the last source passed. [FR-030 1:1 forwarding contract]
//
// session_event_credentials_rotated DEFERRED to 014:
//   The event must fire BEFORE the first handshake on the rotated cert_source
//   (data-model E-7) with the real cert SHA-256 fingerprint. Both the correct
//   emit-site (drive_reconnect_attempt, before TransportFactory::make) and the
//   fingerprint (only available inside async load_credentials()) require the
//   live-transport lifecycle, which is a stub in 013. No event assertions appear
//   in this test. [[project_013_carryforwards_to_014]] / FR-032 / data-model §E-7.
//
// TDD RED witness:
//   Cells 1 / 2 / 4 FAIL RED because Session::reload_credentials is not declared
//   in session.hpp → compile error before any assertion is reached.
//   Cell 3 PASSES immediately (pure TransportFactory unit test, no Session call).

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>

#include <gtest/gtest.h>

#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/tls/cert_source.hpp>
#include <fixpp/transport/transport.hpp>
#include <fixpp/transport/transport_factory.hpp>

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"

using namespace std::chrono_literals;

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// mock_cert_source — tracks identity and call counts.
// Does NOT implement load_credentials() with real OpenSSL; no TLS handshake
// is driven in T042. Each instance carries an id for identity checking.
// ─────────────────────────────────────────────────────────────────────────────
struct mock_cert_source final : public fixpp::tls::cert_source {
    const int id;
    mutable std::atomic<int> load_credentials_call_count{0};

    explicit mock_cert_source(int _id) : id(_id) {}

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<fixpp::tls::local_credentials>>
    load_credentials() override {
        ++load_credentials_call_count;
        co_return std::unexpected{fixpp::core::error::tls_load_cancelled};
    }

    [[nodiscard]] fixpp::core::expected_t<std::span<const fixpp::tls::Certificate>>
    load_trust_anchors() override {
        return std::span<const fixpp::tls::Certificate>{};
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// mock_transport_factory — records reload_credentials calls and tracks the
// current cert_source via an atomic slot (mirrors asio_tls_transport_factory).
// No OpenSSL dependency.
// ─────────────────────────────────────────────────────────────────────────────
class mock_transport_factory final : public fixpp::transport::TransportFactory {
public:
    std::atomic<std::shared_ptr<fixpp::tls::cert_source>> cert_source_slot_{};
    std::atomic<int> reload_call_count{0};
    std::shared_ptr<fixpp::tls::cert_source> last_reloaded_source{};

    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<fixpp::transport::Transport>>
    make(asio::any_io_executor /*exec*/,
         fixpp::tls::SslCtxConfig /*ssl_cfg*/,
         std::pmr::memory_resource* /*mr*/) noexcept override
    {
        return std::unexpected{fixpp::core::error::transport_factory_failed};
    }

    [[nodiscard]] fixpp::core::expected_t<void>
    reload_credentials(
        std::shared_ptr<fixpp::tls::cert_source> new_source) noexcept override
    {
        if (!new_source) {
            return std::unexpected{fixpp::core::error::session_invalid_argument};
        }
        last_reloaded_source = new_source;
        cert_source_slot_.store(std::move(new_source), std::memory_order_release);
        ++reload_call_count;
        return {};
    }

    // 014 T004 — C4: cert_source_snapshot() promoted to pure-virtual on the
    // abstract TransportFactory base; this override was already present.
    // Added override keyword. Returns current source BY VALUE (strong-ref).
    // [[feedback_weak_ptr_cache_needs_owning_context]]
    [[nodiscard]] std::shared_ptr<fixpp::tls::cert_source>
    cert_source_snapshot() const noexcept override {
        return cert_source_slot_.load(std::memory_order_acquire);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Fixture
// ─────────────────────────────────────────────────────────────────────────────
class ReloadCredentialsTest : public ::testing::Test {
protected:
    asio::io_context                         ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig                engine{};

    void SetUp() override {
        auto utc = std::chrono::system_clock::time_point{} + std::chrono::seconds{1704067200};
        auto stp = fixpp::core::steady_time_point{};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock    = clock;
        engine.executor = ioc.get_executor();
    }

    fixpp::core::expected_t<void> run_open(fixpp::session::Session& s) {
        auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
        ioc.run_for(100ms);
        ioc.restart();
        return fut.get();
    }

    fixpp::session::SessionConfig make_cfg(
        std::shared_ptr<fixpp::transport::TransportFactory> factory)
    {
        fixpp::session::SessionConfig cfg;
        cfg.sender_comp_id      = "SELL";
        cfg.target_comp_id      = "BUY";
        cfg.begin_string        = "FIX.4.4";
        cfg.heartbeat_interval  = 30s;
        cfg.security_profile    = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary          = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override   = ioc.get_executor();
        cfg.role                = fixpp::session::session_role::initiator;
        cfg.transport_factory_override = std::move(factory);
        return cfg;
    }
};

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Cell 1 — NullReject
//
// Session::reload_credentials(nullptr) must return
// error::session_invalid_argument (slot 119) per FR-033.
//
// RED: Session::reload_credentials not declared → compile error.
// GREEN: function exists and rejects nullptr immediately.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(ReloadCredentialsTest, NullReject_ReturnsInvalidArgument) {
    auto factory = std::make_shared<mock_transport_factory>();

    fixpp::session::Session sess(engine, make_cfg(factory));
    ASSERT_TRUE(run_open(sess).has_value()) << "Session::open() failed.";

    auto result = sess.reload_credentials(nullptr);

    EXPECT_FALSE(result.has_value())
        << "reload_credentials(nullptr) must return an error per FR-033.";

    if (!result.has_value()) {
        EXPECT_EQ(result.error(), fixpp::core::error::session_invalid_argument)
            << "Expected session_invalid_argument (slot 119) per FR-033 / D-11.";
    }

    // Factory must NOT have been invoked on the nullptr path.
    EXPECT_EQ(factory->reload_call_count.load(), 0)
        << "Factory::reload_credentials must not be called when Session-level "
        << "nullptr check fires first.";
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell 2 — ForwarderDelegates
//
// Session::reload_credentials(new_source) delegates to
// TransportFactory::reload_credentials(new_source) exactly once.
// After the call, factory->cert_source_snapshot() returns new_source.
// [FR-030 / data-model §E-7 atomic-store contract]
//
// No session_event_credentials_rotated assertion: emission is DEFERRED to 014.
// [[project_013_carryforwards_to_014]] / data-model §E-7.
//
// RED: Session::reload_credentials not declared → compile error.
// GREEN: factory.reload_call_count == 1; snapshot == new_source.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(ReloadCredentialsTest, ForwarderDelegates_DelegatesToFactory) {
    auto factory = std::make_shared<mock_transport_factory>();

    fixpp::session::Session sess(engine, make_cfg(factory));
    ASSERT_TRUE(run_open(sess).has_value()) << "Session::open() failed.";

    EXPECT_EQ(factory->reload_call_count.load(), 0)
        << "Initial: factory must not have been called yet.";

    auto new_source = std::make_shared<mock_cert_source>(42);
    auto result = sess.reload_credentials(new_source);

    // Forwarder returns success. [FR-030]
    EXPECT_TRUE(result.has_value())
        << "reload_credentials(valid_source) must succeed per FR-030.";

    // Factory was called exactly once. [FR-030 1:1 forwarding contract]
    EXPECT_EQ(factory->reload_call_count.load(), 1)
        << "TransportFactory::reload_credentials must be called exactly once per "
        << "Session::reload_credentials invocation (forwarder-only contract).";

    // Factory received the correct source pointer.
    EXPECT_EQ(factory->last_reloaded_source, new_source)
        << "Factory must receive the new cert_source pointer passed to "
        << "Session::reload_credentials.";

    // Snapshot reflects the new source after the atomic store.
    // [data-model §E-7: cert_source_snapshot() returns new source post-swap]
    EXPECT_EQ(factory->cert_source_snapshot(), new_source)
        << "cert_source_snapshot() must return new_source after reload. "
        << "This is the canonical check that the next drive_reconnect_attempt "
        << "will read the rotated source (FR-031).";
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell 3 — StrongRefInvariant (factory-level unit test; no Session call)
//
// cert_source_snapshot() returns a shared_ptr BY VALUE.
// Capturing a snapshot BEFORE calling reload_credentials keeps the OLD
// cert_source alive even after the atomic slot is replaced with new_source.
//
// Invariant: use_count of old_source >= 2 while the snapshot is held.
// [[feedback_weak_ptr_cache_needs_owning_context]]
//
// This cell does NOT require Session::reload_credentials — it tests the
// mock_transport_factory directly. GREEN before T044 lands.
// ─────────────────────────────────────────────────────────────────────────────
TEST(ReloadCredentialsFactoryUnit, StrongRefInvariant_OldSourceSurvivesSwap) {
    auto factory = std::make_shared<mock_transport_factory>();

    auto old_source = std::make_shared<mock_cert_source>(1);
    auto new_source = std::make_shared<mock_cert_source>(2);

    // Install old_source into the factory slot.
    ASSERT_TRUE(factory->reload_credentials(old_source).has_value());
    EXPECT_EQ(factory->cert_source_snapshot(), old_source);

    // Capture a strong-ref snapshot BEFORE the swap.
    // Mirrors ReconnectFsm::drive_reconnect_attempt() at attempt entry.
    // [data-model §E-7 / FR-033]
    auto snapshot = factory->cert_source_snapshot();
    EXPECT_EQ(snapshot.get(), old_source.get())
        << "Snapshot before swap must reference old_source.";

    // Atomically swap to new_source.
    ASSERT_TRUE(factory->reload_credentials(new_source).has_value());

    // Factory slot now holds new_source.
    EXPECT_EQ(factory->cert_source_snapshot(), new_source)
        << "After reload, factory slot must hold new_source.";

    // The captured snapshot still refers to old_source (BY VALUE shared_ptr,
    // not weak_ptr — so the old cert_source survives the atomic store).
    // [[feedback_weak_ptr_cache_needs_owning_context]]
    EXPECT_EQ(snapshot.get(), old_source.get())
        << "Captured snapshot must still point to old_source after the swap.";

    EXPECT_GE(old_source.use_count(), 2L)
        << "old_source use_count must be >= 2 while snapshot is held "
        << "(old_source variable + snapshot variable).";

    // Releasing the snapshot drops the extra ref.
    snapshot.reset();
    EXPECT_EQ(old_source.use_count(), 1L)
        << "After snapshot reset, only old_source variable remains (use_count=1).";
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell 4 — CallCountWitness
//
// N calls to Session::reload_credentials produce exactly N calls to
// factory.reload_credentials; factory slot holds the last source passed.
//
// No session_event_credentials_rotated assertion: emission is DEFERRED to 014.
// [[project_013_carryforwards_to_014]] / data-model §E-7.
//
// RED: Session::reload_credentials not declared → compile error.
// GREEN: reload_call_count == N; last source == last passed source.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(ReloadCredentialsTest, CallCountWitness_NForwardingsProduceNFactoryCalls) {
    constexpr int kN = 3;

    auto factory = std::make_shared<mock_transport_factory>();

    fixpp::session::Session sess(engine, make_cfg(factory));
    ASSERT_TRUE(run_open(sess).has_value()) << "Session::open() failed.";

    std::shared_ptr<mock_cert_source> last_source;

    for (int i = 0; i < kN; ++i) {
        auto source = std::make_shared<mock_cert_source>(100 + i);
        auto result = sess.reload_credentials(source);

        EXPECT_TRUE(result.has_value())
            << "Iteration " << i << ": reload_credentials must succeed with valid source.";

        last_source = source;
    }

    // Factory was called exactly N times. [FR-030 1:1 forwarding contract]
    EXPECT_EQ(factory->reload_call_count.load(), kN)
        << "Expected exactly " << kN << " calls to factory.reload_credentials. "
        << "Actual: " << factory->reload_call_count.load();

    // Factory slot holds the last source. [data-model §E-7 atomic-store contract]
    EXPECT_EQ(factory->cert_source_snapshot(), last_source)
        << "After " << kN << " rotations, factory slot must hold the last source.";
}
