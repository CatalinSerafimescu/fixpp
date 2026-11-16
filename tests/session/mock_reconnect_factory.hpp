// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/mock_reconnect_factory.hpp — test-only.
//
// MockReconnectFactory: the TransportFactory that lets a directly driven initiator's
// Session::drive_reconnect() reach its Logon emission with no socket. It was written in
// tests/session/test_refresh_on_logon.cpp (025) and is here so that a second binary can
// drive that entry point (.specify/536-537-538-recovery-liveness.md §5, the rows
// "D-OUT-5, the Logon root" and "D-CAN-1, the hydration flag").
//
// The including translation unit defines FIXPP_ALLOW_MOCK_TRANSPORT, in its source or
// through its target, before this header: mock_transport.hpp refuses to compile without
// it.
//
// The reconnect-initiator rig (namespace reconnect_rig below: FaultStore, FaultStoreFactory,
// Fixture, make_reconnect_initiator, make_acceptor_notconnected) was written in
// test_refresh_on_logon.cpp (025) and moved here by feature 103 (tasks T022), whose
// default-policy cells drive it from a second binary.
#pragma once

#include <gtest/gtest.h>

#include <asio/any_io_executor.hpp>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/message_store.hpp>
#include <fixpp/session/message_store_factory.hpp>
#include <fixpp/session/retrieve_visitor.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fixpp/transport/test/mock_transport.hpp>
#include <fixpp/transport/transport_factory.hpp>
#include <functional>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"
#include "support/pump_until_ready.hpp"
#include "support/session_executor_of.hpp"

namespace fixpp::test_support {

// ── MockReconnectFactory: TransportFactory returning mock_transport ────────────
//
// Used to drive session->drive_reconnect() in unit tests.
// make() returns a mock_transport with handshake_succeeds=true and empty inbound
// (so async_read_some immediately returns EOF after the 2nd Logon is emitted).
// cert_source_snapshot() returns nullptr (no cert rotation; step 2 safe with snap=null).
//
// last_transport: raw pointer to the most-recently created mock_transport. Remains
// valid as long as the session holds the transport (session outlives the test scope).
// Used by W5a to read outbound_bytes_seen() from the reconnect transport.

class MockReconnectFactory final : public fixpp::transport::TransportFactory {
public:
    std::atomic<int> make_call_count{0};
    // Raw pointer to the last transport created. Owned by the session.
    fixpp::transport::test::mock_transport* last_transport{nullptr};
    // Applied to each new transport's Script before it is built (feature 103: the connect
    // and write holds).
    std::function<void(fixpp::transport::test::Script&)> tweak_script;

    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<fixpp::transport::Transport>> make(
        asio::any_io_executor exec, fixpp::tls::SslCtxConfig /*ssl_cfg*/,
        std::pmr::memory_resource* /*mr*/) noexcept override {
        ++make_call_count;
        fixpp::transport::test::Script script;
        script.handshake_succeeds = true;
        if (tweak_script) tweak_script(script);
        // Empty inbound → immediate EOF after connect+handshake.
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

// ── The reconnect-initiator rig ──────────────────────────────────────────────────

namespace reconnect_rig {

using fixpp::session::direction_t;
using fixpp::session::MessageStore;
using fixpp::session::MessageStoreFactory;
using fixpp::session::retrieve_visitor;
using fixpp::session::seqnum_t;

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

class FaultStore final : public MessageStore {
public:
    // fail_on_nth_call: if nonzero, the Nth TOTAL next_seqnum call fails with
    //   store_io_failure. Used for W7 (T033): fail the forced re-read on 2nd logon
    //   (cold open uses calls 1+2; set fail_on_nth_call=3 to fail 2nd-logon's read).
    explicit FaultStore(seqnum_t seeded_inbound = 1, seqnum_t seeded_outbound = 1,
                        int fail_on_nth_call = 0)
        : MessageStore(flush_thunk_for<FaultStore>()),
          next_inbound_(seeded_inbound),
          next_outbound_(seeded_outbound),
          fail_on_nth_call_(fail_on_nth_call) {}

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
        if (fail_on_nth_call_ > 0 && call_count >= fail_on_nth_call_) {
            co_return std::unexpected(fixpp::core::error::store_io_failure);
        }
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
    int fail_on_nth_call_{0};
};

// FaultStoreFactory: wraps FaultStore, yields_persistent_store() configurable.
// persistent=true (default)  → persistent store (used by W1/W2/W3/W5a/W6).
// persistent=false           → non-persistent store (used by W4 to test INV-RoL-2).
// fail_on_nth_call (default 0) → passed to FaultStore; used by W7 (T033) to inject
//   a store-read failure on the 2nd logon forced re-hydrate.
class FaultStoreFactory final : public MessageStoreFactory {
public:
    explicit FaultStoreFactory(seqnum_t seeded_inbound = 1, seqnum_t seeded_outbound = 1,
                               bool persistent = true, int fail_on_nth_call = 0)
        : seeded_inbound_(seeded_inbound),
          seeded_outbound_(seeded_outbound),
          persistent_(persistent),
          fail_on_nth_call_(fail_on_nth_call) {}

    mutable FaultStore* last_store{nullptr};

    [[nodiscard]] bool yields_persistent_store() const noexcept override { return persistent_; }

    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<MessageStore>> make(
        std::string_view /*sender*/, std::string_view /*target*/, std::pmr::memory_resource* /*mr*/,
        std::size_t /*max_store_memory_bytes*/,
        asio::any_io_executor /*file_io_executor*/) noexcept override {
        auto store =
            std::make_unique<FaultStore>(seeded_inbound_, seeded_outbound_, fail_on_nth_call_);
        last_store = store.get();
        return store;
    }

private:
    seqnum_t seeded_inbound_;
    seqnum_t seeded_outbound_;
    bool persistent_;
    int fail_on_nth_call_;
};

// ── Fixture ───────────────────────────────────────────────────────────────────

struct Fixture {
    asio::io_context ioc;
    OutboundCapture capture;
    fixpp::core::EngineConfig eng;
    fixpp::session::SessionConfig cfg;
    std::unique_ptr<fixpp::session::Session> session;

    void feed(const std::vector<std::byte>& frame) {
        auto fut = asio::co_spawn(fixpp::test_support::session_executor_of(*session, ioc),
                                  session->on_inbound_frame(frame), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, std::chrono::milliseconds{500},
                                                        "RefreshOnLogon::Fixture::feed")) {
            fixpp::test_support::drain_or_report(ioc, "RefreshOnLogon::Fixture::feed");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss << "RefreshOnLogon::Fixture::feed";
            return;
        }
        (void)fut.get();
    }
};

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
    FaultStore* store{nullptr};                    // pointer into the FaultStoreFactory's store
    MockReconnectFactory* transport_fac{nullptr};  // owning ptr kept in cfg_ via shared_ptr
};

// refresh_on_logon: true for W1/W2 (knob-on); false for W3 (knob-off byte-identity).
// persistent: true (default) for persistent store; false for W4 (non-persistent no-op).
// policy: bilateral_lenient (default) for W1-W4; bilateral_strict for W5a/W5b; nullopt
//   leaves SessionConfig's own default in place (feature 103's default-policy cells).
// fail_on_nth_call: 0 (default) for normal stores; 3 for W7 (fail 2nd-logon re-read).
// wrap_store: when set, the session's store factory is what it returns for the
//   FaultStoreFactory (a recording wrapper, for a cell that reads the store's call log);
//   `store` still points at the FaultStore inside.
// tweak: when set, runs on the config last, before the Session is built.
inline ReconnectInitiatorFixture make_reconnect_initiator(
    seqnum_t seeded_in, seqnum_t seeded_out, bool refresh_on_logon = true, bool persistent = true,
    std::optional<fixpp::session::reset_seqnum_policy> policy =
        fixpp::session::reset_seqnum_policy::bilateral_lenient,
    int fail_on_nth_call = 0,
    std::function<std::shared_ptr<MessageStoreFactory>(std::shared_ptr<FaultStoreFactory>)>
        wrap_store = nullptr,
    std::function<void(fixpp::session::SessionConfig&)> const& tweak = nullptr) {
    ReconnectInitiatorFixture result;
    result.fix = std::make_unique<Fixture>();
    auto& fix = *result.fix;

    auto store_factory =
        std::make_shared<FaultStoreFactory>(seeded_in, seeded_out, persistent, fail_on_nth_call);
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
    fix.cfg.store_factory = wrap_store ? wrap_store(store_factory) : store_factory;
    fix.cfg.transport_factory_override = transport_factory;
    fix.cfg.reconnect_endpoint = fixpp::transport::Endpoint{"127.0.0.1", 19099};
    if (policy) fix.cfg.reset_seqnum_policy_field = *policy;
    fix.cfg.refresh_on_logon = refresh_on_logon;
    fix.cfg.transport_send = [&fix](std::span<const std::byte> data) { fix.capture(data); };

    if (tweak) tweak(fix.cfg);

    fix.session = std::make_unique<fixpp::session::Session>(fix.eng, fix.cfg);

    // open() — cold one-shot hydrate fires (hydrated_=false → store reads → hydrated_=true).
    // The Logon is emitted via transport_send_ (pre-live path at cold open).
    auto open_fut = asio::co_spawn(fix.ioc, fix.session->open(), asio::use_future);
    if (!fixpp::test_support::run_window_then_ready(fix.ioc, open_fut, std::chrono::seconds{2},
                                                    "make_reconnect_initiator/open")) {
        // Clock-free drain: `Fixture` has no clock member, same as the sibling
        // `Fixture::feed`.
        // Whether that suffices is measured by the seam arm. claim-ok: names the arm
        fixpp::test_support::drain_or_report(fix.ioc, "make_reconnect_initiator/open");
        ADD_FAILURE() << fixpp::test_support::kWindowMiss << "make_reconnect_initiator/open";
        // Return the fixture VALID, not null. The condition, which is what matters and
        // does not rot: every caller dereferences `result.fix` WITHOUT checking it, so
        // nulling would trade a hang for a null dereference. Re-derive the callers of this
        // helper and of `make_acceptor_notconnected`, which are the thing that moves:
        // clang-format off
        //   git grep -n -A2 'make_reconnect_initiator(\|make_acceptor_notconnected(' -- tests/session
        // clang-format on
        // ⚠️ WHAT STOPS A QUIET PASS IS THE `ADD_FAILURE` ABOVE, AND ONLY THAT. An
        // earlier draft of this line added "the state EXPECT below fails too" -- false,
        // and falsifiable by reading three lines further: this `return` leaves the
        // function, so that EXPECT never runs. `store` IS left null here, so a caller that
        // re-asserts `ASSERT_NE(result.store, nullptr)` on its next line gets a second
        // chance to fail -- but that is the caller's assertion, not one "below".
        return result;
    }
    (void)open_fut.get();

    EXPECT_EQ(fix.session->state(), fixpp::session::fsm_state::LogonSent)
        << "make_reconnect_initiator: session must be LogonSent after open()";

    // Capture the FaultStore pointer for observable state assertions.
    result.store = store_factory->last_store;
    EXPECT_NE(result.store, nullptr) << "FaultStoreFactory must have minted a store";

    return result;
}

// ── make_acceptor: acceptor session in NotConnected (not yet fed a Logon) ────
//
// Builds an acceptor, calls open() to reach NotConnected. Does NOT feed a peer Logon
// (W6 does that explicitly). Mirrors 029's make_acceptor but leaves the session in
// NotConnected so the test controls the peer Logon.
//
// refresh_on_logon: true for W6 (knob-on path at call-site 2).
// store_factory: pre-seeded FaultStoreFactory.
// policy: as make_reconnect_initiator's.

struct AcceptorFixture {
    std::unique_ptr<Fixture> fix;
    FaultStore* store{nullptr};
};

inline AcceptorFixture make_acceptor_notconnected(
    std::shared_ptr<MessageStoreFactory> store_factory, bool refresh_on_logon,
    std::optional<fixpp::session::reset_seqnum_policy> policy =
        fixpp::session::reset_seqnum_policy::bilateral_lenient) {
    AcceptorFixture result;
    result.fix = std::make_unique<Fixture>();
    auto& fix = *result.fix;

    auto* raw_factory = dynamic_cast<FaultStoreFactory*>(store_factory.get());

    fix.cfg.role = fixpp::session::session_role::acceptor;
    fix.cfg.sender_comp_id = "SRV";
    fix.cfg.target_comp_id = "CLI";
    fix.cfg.begin_string = "FIX.4.4";
    fix.cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
    fix.cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
    fix.cfg.heartbeat_interval = std::chrono::seconds{30};
    fix.cfg.executor_override = fix.ioc.get_executor();
    fix.cfg.store_factory = std::move(store_factory);
    if (policy) fix.cfg.reset_seqnum_policy_field = *policy;
    fix.cfg.refresh_on_logon = refresh_on_logon;
    fix.cfg.reset_on_logon = false;
    fix.cfg.transport_send = [&fix](std::span<const std::byte> data) { fix.capture(data); };

    fix.session = std::make_unique<fixpp::session::Session>(fix.eng, fix.cfg);

    auto open_fut = asio::co_spawn(fix.ioc, fix.session->open(), asio::use_future);
    if (!fixpp::test_support::run_window_then_ready(fix.ioc, open_fut, std::chrono::seconds{1},
                                                    "make_acceptor_notconnected/open")) {
        // Clock-free drain and valid-fixture return, both as at `make_reconnect_initiator/open`.
        fixpp::test_support::drain_or_report(fix.ioc, "make_acceptor_notconnected/open");
        ADD_FAILURE() << fixpp::test_support::kWindowMiss << "make_acceptor_notconnected/open";
        return result;
    }
    (void)open_fut.get();

    EXPECT_EQ(fix.session->state(), fixpp::session::fsm_state::NotConnected)
        << "make_acceptor_notconnected: session must be in NotConnected after open()";

    if (raw_factory) {
        result.store = raw_factory->last_store;
        EXPECT_NE(result.store, nullptr) << "FaultStoreFactory must have minted a store";
    }

    return result;
}

}  // namespace reconnect_rig

}  // namespace fixpp::test_support
