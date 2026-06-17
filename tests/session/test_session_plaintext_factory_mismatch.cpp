// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 fixpp contributors
//
// tests/session/test_session_plaintext_factory_mismatch.cpp — T021 + T022 [043 US3]
//
// SC-003 / FR-008 / US3 AC1-4: Session::open() must fail closed with
// error::invalid_session_config on a profile↔factory kind mismatch (the
// effective/resolved factory, not just an explicit override).
//
// T021 — REJECT cells (open() returns invalid_session_config, BEFORE any connect):
//   Cell (a): insecure_plain_tcp profile + explicit TLS factory override.
//   Cell (b): TLS profile (mtls_ca) + explicit plaintext factory override.
//   Cell (c): TLS profile + NO session override + a plaintext engine-default factory.
//             This is the effective-factory case: without T023 it opens instead of
//             failing at open() (fails later at the FSM dynamic_cast). This cell is
//             the Gate-A-round-1 finding and the key RED before T023 is added.
//
// T021 — OPEN cells (open() returns success):
//   Cell (d): insecure_plain_tcp profile + no override (auto-derived plaintext factory).
//   Cell (e): TLS profile (one_way_ca) + no override (engine default is a TLS double).
//   Cell (f): insecure_plain_tcp profile + explicit plaintext factory override.
//
// T022 — Effective-factory-reaches-mint witness (D-4 / research.md D-4 test note):
//   Cell (g): plaintext/NO-OVERRIDE (auto-derive) — drive_reconnect() returns a
//             connect-stage error (NOT transport_factory_failed). Discrimination:
//             null factory_ → FSM null-check fires → transport_factory_failed.
//             Wired auto-derive factory → make() called → connect attempted → refused.
//   Cell (h): TLS/no-override with a counting engine-default double — make_count > 0
//             after drive_reconnect() confirms the engine default was wired.
//
// WITNESS QUALITY:
//   REJECT cells assert error::invalid_session_config returned AT open() specifically.
//   OPEN cells assert has_value() → open() succeeded with the matched pair.
//   T022 counting cells assert make_count_ > 0 after one drive_reconnect() attempt,
//   proving the factory was wired and called (not null-pointer'd past).
//
// RED evidence (before T023):
//   Cell (a): currently OPENS (returns has_value()==true) — no mismatch reject yet.
//   Cell (b): currently OPENS.
//   Cell (c): currently OPENS — the Gate-A-round-1 key regression (effective-factory
//             case escapes open(), would fail late at FSM dynamic_cast).
//   After T023, all three REJECT cells return error::invalid_session_config.
//
// CONSTRAINT: TLS-kind factories are represented by a MinimalTlsFactory double (no
//   cert files needed). Counting factories use make_count_ to witness make() calls.
//
// Anchors: spec.md SC-003 / FR-008 / US3 AC1-4; research.md D-4 / D-5 / D-6;
//          data-model.md E-3 / E-4 / E-6; [const §XII.5 v0.3]; tasks.md T021/T022/T023.

// SecurityProfile::kind::insecure_plain_tcp carries [[deprecated]]; suppress file-wide.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#include <fixpp/session/security_profile.hpp>
#pragma clang diagnostic pop

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <atomic>
#include <chrono>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/system_clock_source.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/transport/transport_factory.hpp>
#include <memory>
#include <span>

#include "support/minimal_dictionary.hpp"

namespace {

using fixpp::core::error;
using fixpp::session::SecurityProfile;
using fixpp::session::Session;
using fixpp::session::SessionConfig;

// ─────────────────────────────────────────────────────────────────────────────
// MinimalTlsFactory — a minimal TLS-kind double.
//
// Does NOT override kind() → returns the defaulted transport_security_kind::tls.
// D-5: "a factory that forgets to override returns tls — safe default."
// make() returns transport_factory_failed (no cert files needed).
// ─────────────────────────────────────────────────────────────────────────────
class MinimalTlsFactory final : public fixpp::transport::TransportFactory {
public:
    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<fixpp::transport::Transport>> make(
        asio::any_io_executor, fixpp::tls::SslCtxConfig,
        std::pmr::memory_resource*) noexcept override {
        ++make_count_;
        return std::unexpected{fixpp::core::error::transport_factory_failed};
    }

    [[nodiscard]] fixpp::core::expected_t<void> reload_credentials(
        std::shared_ptr<fixpp::tls::cert_source>) noexcept override {
        return {};
    }

    [[nodiscard]] std::shared_ptr<fixpp::tls::cert_source> cert_source_snapshot()
        const noexcept override {
        return nullptr;
    }

    // Counter for T022 mint-witness assertions.
    std::atomic<int> make_count_{0};
};


// ─────────────────────────────────────────────────────────────────────────────
// Build helpers.
// ─────────────────────────────────────────────────────────────────────────────

// Build a minimal SessionConfig for an acceptor (no auto-connect at open()).
static SessionConfig make_cfg(SecurityProfile::kind k) {
    SessionConfig cfg;
    cfg.sender_comp_id = "TW";
    cfg.target_comp_id = "ISLD";
    cfg.begin_string = "FIX.4.2";
    cfg.role = fixpp::session::session_role::acceptor;
    cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
    cfg.heartbeat_interval = std::chrono::seconds{0};

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    cfg.security_profile = SecurityProfile{k};
#pragma clang diagnostic pop

    cfg.transport_send = [](std::span<const std::byte>) {};
    return cfg;
}

// ─────────────────────────────────────────────────────────────────────────────
// T021 — REJECT cells
// ─────────────────────────────────────────────────────────────────────────────

// Cell (a): insecure_plain_tcp + explicit TLS factory override.
// The effective factory is the TLS override; kind()==tls disagrees with
// insecure_plain_tcp (which requires kind()==plaintext). Must reject.
TEST(PlaintextFactoryMismatch, Cell_a_PlaintextProfileWithTlsOverrideRejects) {
    asio::io_context ioc;
    fixpp::core::EngineConfig eng;
    eng.executor = ioc.get_executor();
    eng.clock = std::make_shared<fixpp::core::system_clock_source>(ioc.get_executor());

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    auto cfg = make_cfg(SecurityProfile::kind::insecure_plain_tcp);
#pragma clang diagnostic pop

    // TLS-kind override (MinimalTlsFactory defaults kind() → tls).
    cfg.transport_factory_override = std::make_shared<MinimalTlsFactory>();
    cfg.executor_override = ioc.get_executor();

    Session s{eng, cfg};
    auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
    ioc.run();
    auto val = fut.get();

    ASSERT_FALSE(val.has_value())
        << "Cell (a) SC-003/FR-008: insecure_plain_tcp + explicit TLS factory override "
           "must be rejected at open() before any connect attempt. "
           "[RED before T023: open() returns has_value()==true]";
    EXPECT_EQ(val.error(), error::invalid_session_config)
        << "Cell (a): expected error::invalid_session_config (slot 53); "
           "got error=" << static_cast<int>(val.error());
}

// Cell (b): TLS profile (mtls_ca) + explicit plaintext factory override.
// The effective factory is the plaintext override; kind()==plaintext disagrees
// with mtls_ca (which requires kind()==tls). Must reject.
TEST(PlaintextFactoryMismatch, Cell_b_TlsProfileWithPlaintextOverrideRejects) {
    asio::io_context ioc;
    fixpp::core::EngineConfig eng;
    eng.executor = ioc.get_executor();
    eng.clock = std::make_shared<fixpp::core::system_clock_source>(ioc.get_executor());

    auto cfg = make_cfg(SecurityProfile::kind::mtls_ca);

    // Plaintext factory as session override (kind()==plaintext, mismatches mtls_ca).
    auto plain_r = fixpp::transport::make_asio_plain_transport_factory(
        fixpp::transport::Transport::Config{});
    ASSERT_TRUE(plain_r.has_value()) << "make_asio_plain_transport_factory failed";
    cfg.transport_factory_override = std::move(*plain_r);
    cfg.executor_override = ioc.get_executor();

    Session s{eng, cfg};
    auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
    ioc.run();
    auto val = fut.get();

    ASSERT_FALSE(val.has_value())
        << "Cell (b) SC-003/FR-008: mtls_ca profile + explicit plaintext factory override "
           "must be rejected at open() before any connect attempt. "
           "[RED before T023: open() returns has_value()==true]";
    EXPECT_EQ(val.error(), error::invalid_session_config)
        << "Cell (b): expected error::invalid_session_config; "
           "got error=" << static_cast<int>(val.error());
}

// Cell (c): TLS profile + NO session override + plaintext engine-default factory.
//
// The effective factory is the engine default (plaintext); kind()==plaintext
// disagrees with the TLS profile (mtls_ca). WITHOUT T023 this cell returns
// has_value()==true (the session opens, then would fail late at the FSM
// dynamic_cast<TlsTransport*>). WITH T023 it returns invalid_session_config AT open().
//
// This is the Gate-A-round-1 finding: FR-008 must check the EFFECTIVE factory,
// not just an explicit override.
//
// RED evidence (before T023):
//   ASSERT_FALSE(val.has_value()) fails because val.has_value()==true (open succeeds).
//   This shows the mismatch is NOT caught at open() without T023.
TEST(PlaintextFactoryMismatch, Cell_c_TlsProfileWithPlaintextEngineDefaultRejects) {
    asio::io_context ioc;
    fixpp::core::EngineConfig eng;
    eng.executor = ioc.get_executor();
    eng.clock = std::make_shared<fixpp::core::system_clock_source>(ioc.get_executor());

    // Plaintext factory as ENGINE default (not session override).
    // TLS session with no override → effective factory = engine default = plaintext.
    auto plain_r = fixpp::transport::make_asio_plain_transport_factory(
        fixpp::transport::Transport::Config{});
    ASSERT_TRUE(plain_r.has_value()) << "make_asio_plain_transport_factory failed";
    eng.default_transport_factory = std::move(*plain_r);

    auto cfg = make_cfg(SecurityProfile::kind::mtls_ca);
    // NO transport_factory_override — effective factory is the engine default.
    cfg.executor_override = ioc.get_executor();

    Session s{eng, cfg};
    auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
    ioc.run();
    auto val = fut.get();

    ASSERT_FALSE(val.has_value())
        << "Cell (c) SC-003/FR-008 (Gate-A-round-1): TLS profile (mtls_ca) with no "
           "session override but a plaintext ENGINE-DEFAULT factory must be rejected at "
           "open() — the effective/resolved factory's kind() is checked, not just an "
           "explicit override. Without T023, this cell OPENS (val.has_value()==true) "
           "and would fail late at the FSM dynamic_cast<TlsTransport*>. "
           "[RED before T023: open() returns has_value()==true]";
    EXPECT_EQ(val.error(), error::invalid_session_config)
        << "Cell (c): expected error::invalid_session_config (the effective-factory "
           "mismatch reject); got error=" << static_cast<int>(val.error());
}

// ─────────────────────────────────────────────────────────────────────────────
// T021 — OPEN cells
// ─────────────────────────────────────────────────────────────────────────────

// Cell (d): insecure_plain_tcp + no override → auto-derived plaintext factory.
// kind()==plaintext matches the profile. Must open.
TEST(PlaintextFactoryMismatch, Cell_d_PlaintextProfileNoOverrideOpens) {
    asio::io_context ioc;
    fixpp::core::EngineConfig eng;
    eng.executor = ioc.get_executor();
    eng.clock = std::make_shared<fixpp::core::system_clock_source>(ioc.get_executor());

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    auto cfg = make_cfg(SecurityProfile::kind::insecure_plain_tcp);
#pragma clang diagnostic pop
    cfg.executor_override = ioc.get_executor();
    // No transport_factory_override → auto-derive.

    Session s{eng, cfg};
    auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
    ioc.run();
    auto val = fut.get();

    EXPECT_TRUE(val.has_value())
        << "Cell (d) SC-003/US3 AC4: insecure_plain_tcp with no override must succeed "
           "(auto-derive plaintext factory; kind()==plaintext matches profile). "
           "Error=" << (val.has_value() ? 0 : static_cast<int>(val.error()));

    if (val.has_value()) {
        ioc.restart();
        auto close_fut = asio::co_spawn(ioc, s.close(), asio::use_future);
        ioc.run();
        (void)close_fut.get();
    }
}

// Cell (e): TLS profile (one_way_ca) + no override + TLS engine-default.
// kind()==tls matches the profile. Must open.
TEST(PlaintextFactoryMismatch, Cell_e_TlsProfileWithTlsEngineDefaultOpens) {
    asio::io_context ioc;
    fixpp::core::EngineConfig eng;
    eng.executor = ioc.get_executor();
    eng.clock = std::make_shared<fixpp::core::system_clock_source>(ioc.get_executor());
    eng.default_transport_factory = std::make_shared<MinimalTlsFactory>();

    auto cfg = make_cfg(SecurityProfile::kind::one_way_ca);
    cfg.executor_override = ioc.get_executor();

    Session s{eng, cfg};
    auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
    ioc.run();
    auto val = fut.get();

    EXPECT_TRUE(val.has_value())
        << "Cell (e) SC-003/US3 AC4: one_way_ca + TLS engine-default must succeed "
           "(kind()==tls matches the TLS profile). "
           "Error=" << (val.has_value() ? 0 : static_cast<int>(val.error()));

    if (val.has_value()) {
        ioc.restart();
        auto close_fut = asio::co_spawn(ioc, s.close(), asio::use_future);
        ioc.run();
        (void)close_fut.get();
    }
}

// Cell (f): insecure_plain_tcp + explicit plaintext factory override.
// kind()==plaintext matches the profile. Must open.
TEST(PlaintextFactoryMismatch, Cell_f_PlaintextProfileWithPlaintextOverrideOpens) {
    asio::io_context ioc;
    fixpp::core::EngineConfig eng;
    eng.executor = ioc.get_executor();
    eng.clock = std::make_shared<fixpp::core::system_clock_source>(ioc.get_executor());

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    auto cfg = make_cfg(SecurityProfile::kind::insecure_plain_tcp);
#pragma clang diagnostic pop

    auto plain_r = fixpp::transport::make_asio_plain_transport_factory(
        fixpp::transport::Transport::Config{});
    ASSERT_TRUE(plain_r.has_value()) << "make_asio_plain_transport_factory failed";
    cfg.transport_factory_override = std::move(*plain_r);
    cfg.executor_override = ioc.get_executor();

    Session s{eng, cfg};
    auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
    ioc.run();
    auto val = fut.get();

    EXPECT_TRUE(val.has_value())
        << "Cell (f) SC-003/US3 AC4: insecure_plain_tcp + explicit plaintext factory "
           "override must succeed (kind()==plaintext matches). "
           "Error=" << (val.has_value() ? 0 : static_cast<int>(val.error()));

    if (val.has_value()) {
        ioc.restart();
        auto close_fut = asio::co_spawn(ioc, s.close(), asio::use_future);
        ioc.run();
        (void)close_fut.get();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// T022 — Effective-factory-reaches-mint witness (D-4 / research.md D-4 test note)
//
// Proves the resolved factory is WIRED into the FSM (not a stale null pointer),
// and that drive_reconnect() calls make() on it.
//
// Cell (g): plaintext/NO-OVERRIDE (auto-derive path) — make() is called by
//   drive_reconnect(), producing a connect-stage error (not transport_factory_failed).
//   Discrimination:
//     Old null path: effective_transport_factory_ not assigned at open() →
//       set_transport_factory(nullptr) → factory_==nullptr → FSM null-check fires
//       WITHOUT calling make() → returns transport_factory_failed.
//     New T012 path: auto-derive assigns effective_transport_factory_ → wired into
//       factory_ → make() called → a plain transport is minted → connect attempted
//       → returns transport_connect_refused (or equivalent connect-stage error).
//   The ASSERT is: drive_reconnect() returns an error != transport_factory_failed.
//   IPv6 ::1 is used as the endpoint because on this host ::1:<port> returns immediate
//   ECONNREFUSED (IPv4 loopback connects time out — WSL2 kernel behavior).
//
// Cell (h): TLS/no-override — counting engine-default double witnesses the same
//   discrimination for the TLS arm.
// ─────────────────────────────────────────────────────────────────────────────

// Cell (g): plaintext/no-override auto-derive — factory reaches mint.
//
// No transport_factory_override set. Session::open() auto-derives the plaintext factory
// (D-4 step 2: insecure_plain_tcp + no override → make_asio_plain_transport_factory).
// drive_reconnect() calls make() on the wired factory, minting a real plain transport.
// The transport then tries to connect to IPv6 ::1 (loopback) port 19999 — refused
// immediately on this host — returning transport_connect_refused or similar.
// This is NOT transport_factory_failed, which would indicate factory_ was null.
//
// max_attempts=1 (bounded): the reconnect loop exits after one attempt regardless of
// the error code, so no backoff delay is incurred.
TEST(PlaintextFactoryMintWitness, Cell_g_PlaintextNoOverrideAutoDeriveMint) {
    asio::io_context ioc;
    fixpp::core::EngineConfig eng;
    eng.executor = ioc.get_executor();
    eng.clock = std::make_shared<fixpp::core::system_clock_source>(ioc.get_executor());
    // No engine default — auto-derive derives its own factory.

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    SessionConfig cfg;
    cfg.sender_comp_id = "TW";
    cfg.target_comp_id = "ISLD";
    cfg.begin_string = "FIX.4.2";
    cfg.role = fixpp::session::session_role::initiator;
    cfg.engine_managed = true;   // defer connect to drive_reconnect()
    cfg.security_profile = SecurityProfile{SecurityProfile::kind::insecure_plain_tcp};
#pragma clang diagnostic pop
    cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
    cfg.heartbeat_interval = std::chrono::seconds{0};
    cfg.executor_override = ioc.get_executor();
    cfg.transport_send = [](std::span<const std::byte>) {};
    // IPv6 ::1 loopback: gives immediate ECONNREFUSED on this WSL2 host.
    // (IPv4 127.0.0.1 connects time out — WSL2 kernel SYN filter behavior.)
    cfg.reconnect_endpoint = fixpp::transport::Endpoint{"::1", 19999};
    // max_attempts=1: exit after one attempt, no backoff delay.
    fixpp::transport::ReconnectPolicy rp;
    rp.max_attempts = 1;
    cfg.reconnect_policy = rp;
    // NO transport_factory_override — tests the auto-derive path.

    Session s{eng, cfg};
    auto open_fut = asio::co_spawn(ioc, s.open(), asio::use_future);
    ioc.run();
    ASSERT_TRUE(open_fut.get().has_value())
        << "Cell (g): open() must succeed for plaintext/no-override (auto-derive) session";

    // Drive one reconnect attempt.
    // The auto-derived factory is wired → make() is called → a plain transport is minted →
    // connect to ::1:19999 → ECONNREFUSED → transport_connect_refused is returned.
    ioc.restart();
    auto drive_fut = asio::co_spawn(ioc, s.drive_reconnect(), asio::use_future);
    ioc.run();
    auto drive_r = drive_fut.get();

    ASSERT_FALSE(drive_r.has_value())
        << "Cell (g): drive_reconnect() must fail (connect refused), not succeed";
    EXPECT_NE(drive_r.error(), error::transport_factory_failed)
        << "Cell (g) D-4 mint witness: drive_reconnect() returned transport_factory_failed "
           "which means factory_ was NULL — the auto-derive path did NOT wire the factory "
           "into the FSM (T012 regression). Expected a connect-stage error "
           "(transport_connect_refused), not transport_factory_failed. "
           "[research.md D-4: 'drive_reconnect_attempt() fails at null-guard if factory_ null']";

    ioc.restart();
    auto close_fut = asio::co_spawn(ioc, s.close(), asio::use_future);
    ioc.run();
    (void)close_fut.get();
}

// Cell (h): TLS/no-override — engine-default counting double reaches mint.
//
// A counting TLS double is installed as engine.default_transport_factory.
// After open() (which succeeds for matched one_way_ca + TLS engine-default),
// drive_reconnect() calls make() on the counting double. make_count_ > 0 witnesses
// that T012 wired the engine default (not the ctor-time nullptr) into factory_.
//
// Discrimination: without T012 set_transport_factory(), factory_ = ctor-time pointer =
// cfg.transport_factory_override.get() (nullptr for no override) → FSM null-check fires
// → make_count_ stays 0. With T012, factory_ = counting double.get() → make() called.
TEST(PlaintextFactoryMintWitness, Cell_h_TlsNoOverrideEngineDefaultReachesMint) {
    asio::io_context ioc;
    fixpp::core::EngineConfig eng;
    eng.executor = ioc.get_executor();
    eng.clock = std::make_shared<fixpp::core::system_clock_source>(ioc.get_executor());

    auto counting_fac = std::make_shared<MinimalTlsFactory>();
    eng.default_transport_factory = counting_fac;

    SessionConfig cfg;
    cfg.sender_comp_id = "TW";
    cfg.target_comp_id = "ISLD";
    cfg.begin_string = "FIX.4.2";
    cfg.role = fixpp::session::session_role::initiator;
    cfg.engine_managed = true;
    cfg.security_profile = SecurityProfile{SecurityProfile::kind::one_way_ca};
    cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
    cfg.heartbeat_interval = std::chrono::seconds{0};
    cfg.executor_override = ioc.get_executor();
    cfg.transport_send = [](std::span<const std::byte>) {};
    cfg.reconnect_endpoint = fixpp::transport::Endpoint{"127.0.0.1", 1};
    // max_attempts=1 so drive_reconnect() exits after one make() call (not unbounded loop).
    fixpp::transport::ReconnectPolicy rp;
    rp.max_attempts = 1;
    cfg.reconnect_policy = rp;

    Session s{eng, cfg};
    auto open_fut = asio::co_spawn(ioc, s.open(), asio::use_future);
    ioc.run();
    ASSERT_TRUE(open_fut.get().has_value())
        << "Cell (h): open() must succeed for one_way_ca/no-override + TLS engine-default";

    EXPECT_EQ(counting_fac->make_count_.load(), 0)
        << "Cell (h): make() must NOT be called at open() time";

    ioc.restart();
    auto drive_fut = asio::co_spawn(ioc, s.drive_reconnect(), asio::use_future);
    ioc.run();
    (void)drive_fut.get();

    EXPECT_GT(counting_fac->make_count_.load(), 0)
        << "Cell (h) D-4 mint witness: make() was never called on the counting engine-default "
           "factory after drive_reconnect(). This means factory_ was null (ctor-time override "
           "= nullptr for no-override session) and T012's set_transport_factory() was not "
           "called or did not reach the engine-default arm. "
           "Expected make_count_ > 0 after one drive_reconnect() call.";

    ioc.restart();
    auto close_fut = asio::co_spawn(ioc, s.close(), asio::use_future);
    ioc.run();
    (void)close_fut.get();
}

}  // namespace
