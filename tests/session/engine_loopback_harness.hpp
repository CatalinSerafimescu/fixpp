// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// tests/session/engine_loopback_harness.hpp — T007 [P] Phase 2
//
// Shared live-loopback-TLS Engine harness for 015 US1/US2/US3 tests.
// Mirrors loopback_tls_session_harness.hpp but wraps the public Engine API.
// Anchors: tasks.md T007; data-model.md E-1/E-6; research.md R7(b);
//          [const §XV.9]: tests/-only; concrete transport headers are safe here.
//
// Usage:
//   auto h = EngineLoopbackHarness::build(ioc.get_executor(),
//                                         std::move(engine_cfg));
//   if (!h) { GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set"; }
//   h->engine().start();
//   // … drive ioc … then:
//   asio::co_spawn(ioc, h->engine().stop(), asio::use_future).get();

#pragma once

#include <gtest/gtest.h>

#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <cstdlib>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/system_clock_source.hpp>
#include <fixpp/session/engine.hpp>
#include <fixpp/session/security_profile.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/tls/file_cert_source.hpp>
#include <fixpp/tls/security_profile.hpp>
#include <fixpp/transport/transport_factory.hpp>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>

#include "support/minimal_dictionary.hpp"
#include "transport/loopback_tls_fixture.hpp"

using namespace std::chrono_literals;

namespace fixpp::test_support {

class EngineLoopbackHarness {
public:
    // Build. Returns nullptr + GTEST_SKIP() intent when fixture dir absent.
    // Takes EngineConfig by move (it contains non-copyable unique_ptr members).
    [[nodiscard]] static std::unique_ptr<EngineLoopbackHarness> build(
        asio::any_io_executor exec, fixpp::core::EngineConfig engine_cfg,
        bool register_sessions = true) {
        const char* dir = std::getenv("FIXPP_TLS_FIXTURE_DIR");
#ifdef FIXPP_TLS_FIXTURE_DIR
        static const char* kDir = FIXPP_TLS_FIXTURE_DIR;
#else
        static const char* kDir = nullptr;
#endif
        const char* fixture_dir = dir ? dir : kDir;
        if (!fixture_dir || fixture_dir[0] == '\0') return nullptr;

        auto h = std::unique_ptr<EngineLoopbackHarness>(new EngineLoopbackHarness{});
        h->fixture_dir_ = fixture_dir;
        h->transport_fixture_ =
            std::make_unique<fixpp::transport::test::LoopbackTlsFixture>(h->fixture_dir_, exec);
        // 041 T019: inject a real system clock if the caller did not supply one.
        // Engine::start() rejects a null clock with clock_not_set (FR-007/C-4).
        if (!engine_cfg.clock) {
            engine_cfg.clock = std::make_shared<fixpp::core::system_clock_source>(exec);
        }
        h->engine_ = std::make_unique<fixpp::session::Engine>(exec, std::move(engine_cfg));
        if (register_sessions) h->register_default_sessions(exec);
        return h;
    }

    fixpp::session::Engine& engine() noexcept { return *engine_; }
    fixpp::transport::test::LoopbackTlsFixture& transport_fixture() noexcept {
        return *transport_fixture_;
    }
    // server_endpoint() returns the engine's acceptor bound endpoint.
    // The engine builds its asio_listener on start(); the port is OS-assigned
    // (port 0 in reconnect_endpoint → OS assigns). The endpoint is readable
    // once the executor has run at least one step after start().
    // Call engine().acceptor_bound_endpoint(acceptor_id()) for the same value.
    fixpp::transport::Endpoint server_endpoint() const noexcept {
        return engine_->acceptor_bound_endpoint(acceptor_id_);
    }
    fixpp::session::SessionId acceptor_id() const noexcept { return acceptor_id_; }
    fixpp::session::SessionId initiator_id() const noexcept { return initiator_id_; }

    std::future<void> spawn_stop(asio::io_context& ioc) {
        return asio::co_spawn(ioc, engine_->stop(), asio::use_future);
    }

private:
    EngineLoopbackHarness() = default;

    void register_default_sessions(asio::any_io_executor exec) {
        // Shared TLS factory (FR-026: SSL_CTX cached once).
        fixpp::tls::file_cert_source::Config cs_cfg;
        cs_cfg.leaf_path = fixture_dir_ + "/leaf_rsa2048.pem";
        cs_cfg.private_key_path = fixture_dir_ + "/leaf_rsa2048.key";
        cs_cfg.ca_bundle_path = fixture_dir_ + "/ca.pem";
        auto cs = fixpp::tls::file_cert_source::make_file_cert_source(
            cs_cfg, std::pmr::new_delete_resource());
        if (!cs) throw std::runtime_error{"EngineLoopbackHarness: cert_source failed"};

        fixpp::tls::SslCtxConfig ssl;
        ssl.profile = fixpp::tls::SecurityProfile::mtls_ca;
        ssl.cs = std::move(*cs);
        ssl.clock = nullptr;
        ssl.caps = fixpp::tls::CertSourceCaps{};

        auto fres = fixpp::transport::make_asio_tls_transport_factory(
            fixpp::transport::Transport::Config{}, ssl);
        if (!fres) throw std::runtime_error{"EngineLoopbackHarness: factory failed"};
        std::shared_ptr<fixpp::transport::TransportFactory> fac{std::move(*fres)};

        auto make_cfg = [&](const char* sender, const char* target,
                            fixpp::session::session_role role) {
            fixpp::session::SessionConfig c;
            c.sender_comp_id = sender;
            c.target_comp_id = target;
            c.begin_string = "FIX.4.2";
            c.role = role;
            c.executor_override = exec;
            c.security_profile =
                fixpp::session::SecurityProfile{fixpp::session::SecurityProfile::kind::mtls_ca};
            c.dictionary = fixpp::test_support::make_minimal_dictionary();
            c.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
            c.transport_factory_override = fac;
            c.heartbeat_interval = std::chrono::seconds{30};
            c.logout_disconnect_timeout_ms = 2000;
            return c;
        };

        auto acc = make_cfg("ACCEPTOR", "INITIATOR", fixpp::session::session_role::acceptor);
        // reconnect_endpoint is repurposed as the acceptor's bind endpoint
        // (SC-010 delta #6 / "Listener acquisition" design decision).
        // Port 0 → OS-assigned; readable via engine.acceptor_bound_endpoint().
        acc.reconnect_endpoint = fixpp::transport::Endpoint{"127.0.0.1", 0};
        // Rebindable send-slot: no-op until attach_accepted_transport (T011/E-1/R7(b)).
        // The engine's run_accept_loop calls attach_accepted_transport which rebinds
        // transport_send_ to the live Transport::async_write. This no-op is the
        // initial value that is captured at open() time.
        acc.transport_send = [](std::span<const std::byte>) {};
        acceptor_id_ = fixpp::session::SessionId::from_config(acc);
        if (!engine_->register_session(std::move(acc)))
            throw std::runtime_error{"EngineLoopbackHarness: register acceptor failed"};

        // Initiator: reconnect_endpoint points to the engine's acceptor listener.
        // The port is 0 here at registration time — the real port is set when
        // the accept loop binds (after start() + some executor steps).
        // For tests that drive both roles, they should use:
        //   ioc.run_for(1ms) after start() to let the listener bind, then
        //   set the initiator's reconnect_endpoint via the harness accessor.
        // (This is handled per-test; the harness exposes acceptor_bound_endpoint.)
        auto ini = make_cfg("INITIATOR", "ACCEPTOR", fixpp::session::session_role::initiator);
        // Initiator reconnect_endpoint: will be updated by tests that use the
        // real acceptor port. Set to {127.0.0.1, 0} as a placeholder; the
        // connect loop (US2) will need the real port at attempt time.
        // For US1 tests (acceptor-only), the initiator is a standalone TLS
        // client that connects to acceptor_bound_endpoint after the loop binds.
        ini.reconnect_endpoint = fixpp::transport::Endpoint{"127.0.0.1", 0};
        initiator_id_ = fixpp::session::SessionId::from_config(ini);
        if (!engine_->register_session(std::move(ini)))
            throw std::runtime_error{"EngineLoopbackHarness: register initiator failed"};
    }

    std::string fixture_dir_;
    std::unique_ptr<fixpp::transport::test::LoopbackTlsFixture> transport_fixture_;
    std::unique_ptr<fixpp::session::Engine> engine_;
    fixpp::session::SessionId acceptor_id_;
    fixpp::session::SessionId initiator_id_;
};

}  // namespace fixpp::test_support

namespace fixpp::test_support {

// Stops the engine on EVERY exit path from a test body. (#323)
//
// THE DEFECT IT REMOVES. `GTEST_SKIP()` and a failed `ASSERT_*` both RETURN from
// the test body. Any such return positioned between `engine().start()` and
// `engine().stop()` skips the stop, and `~Engine()` asserts
// `stopped_ && "Engine destroyed without calling co_await stop() first"` --
// so a path whose whole purpose is to SKIP a test aborts the entire binary
// instead. Measured on Tier 3 `linux-clang-libc++-ubsan`:
// `135 - engine_firstframe (Subprocess aborted)`, triggered by the
// `port == 0` / "acceptor listener did not bind" skip.
//
// It was 7 tests across 2 files, every one with the identical shape. The OTHER
// skip in each test ("FIXPP_TLS_FIXTURE_DIR not set") is safe only because it
// happens to precede `start()` -- a positional accident that nothing enforced.
// Declaring this guard immediately after `start()` makes the stop unconditional,
// so position stops mattering and a future skip cannot reintroduce the abort.
//
// Safe to combine with an explicit `stop()`: `Engine::stop()` carries an
// idempotency guard (`if (stopped_) co_return;`), so the second call returns
// without re-tearing-down. Tests that measure stop PROMPTNESS are therefore
// unaffected -- this runs after their measurement, and returns immediately.
class engine_stop_guard {
public:
    engine_stop_guard(EngineLoopbackHarness& h, asio::io_context& ioc) noexcept
        : h_{&h}, ioc_{&ioc} {}
    engine_stop_guard(const engine_stop_guard&) = delete;
    engine_stop_guard& operator=(const engine_stop_guard&) = delete;

    ~engine_stop_guard() {
        // ⚠️ NOTHING MAY ESCAPE THIS DESTRUCTOR, and the report is itself a throw
        // site -- this is #308's lesson applied one layer out. A destructor is
        // implicitly `noexcept`; driving an io_context here dispatches arbitrary
        // handlers, and `ADD_FAILURE()` RECORDS the failure and then THROWS
        // `GoogleTestFailureException` under GoogleTest's supported
        // `--gtest_throw_on_failure`. So the inner catch reports and the OUTER
        // catch swallows the report's own throw. Reporting inside a single catch
        // would trade this abort for a different one -- which is exactly the
        // mistake #308's first fix shipped.
        try {
            try {
                auto fut = asio::co_spawn(*ioc_, h_->engine().stop(), asio::use_future);
                ioc_->restart();
                ioc_->run();
                fut.get();
            } catch (const std::exception& e) {
                ADD_FAILURE() << "engine_stop_guard: engine teardown threw -- what(): " << e.what();
            } catch (...) {
                ADD_FAILURE() << "engine_stop_guard: engine teardown threw a non-std exception.";
            }
        } catch (...) {  // NOLINT(bugprone-empty-catch) -- see the paragraph above
        }
    }

private:
    EngineLoopbackHarness* h_;
    asio::io_context* ioc_;
};

}  // namespace fixpp::test_support
