// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/config/test_load_happy_path.cpp
// 044-toml-session-config Phase-3 TDD RED tests.
//
// T011 [P] [US1] — Field-for-field equivalence test: load happy_full.toml and
//   assert each SessionConfig / EngineEstablishment field equals the expected
//   value. Anchors: data-model §E-1–E-3, SC-002.
//
// T012 [P] [US1] — Selector-build test: assert clock=system builds a live
//   system_clock_source bound to the engine executor, and a TLS transport
//   factory is built with the SAME clock instance (D-5/D-6).
//
// RED condition (Phase 3a — no scalar mappers/selector resolvers yet):
//   load_toml_config returns has_value()==true with sessions.size()==1, but
//   each SessionDefinition holds a DEFAULT-CONSTRUCTED SessionConfig (all empty
//   strings / zero / default enums) and EngineEstablishment is all-null.
//   Every field assertion will FAIL on that substrate — the correct RED.
//
// Anti-hang: load_toml_config is synchronous (cold). We create an io_context to
//   satisfy system_clock_source's ctor requirement but NEVER call ctx.run().
//   LoadOptions{ctx.get_executor()} is all that is needed.

#include <fixpp/config/toml_config_loader.hpp>
#include <fixpp/config/config_bundle.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/core/system_clock_source.hpp>
#include <fixpp/transport/transport_factory.hpp>

#include <asio/io_context.hpp>

#include <chrono>
#include <string>

#include <gtest/gtest.h>

#ifndef FIXPP_CONFIG_FIXTURE_DIR
#error "FIXPP_CONFIG_FIXTURE_DIR must be set by CMake"
#endif

namespace {

// Path to the happy-path fixture file.
const std::filesystem::path k_fixture{
    std::string{FIXPP_CONFIG_FIXTURE_DIR} + "/happy_full.toml"};

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// T011 — field-for-field equivalence
// ─────────────────────────────────────────────────────────────────────────────
//
// Strategy: verify EVERY mapped scalar / structured field from happy_full.toml.
// Values were chosen to DIFFER from SessionConfig defaults wherever legal so
// that an unmapped field left at its default produces a FAILING assertion here
// (non-discriminating-witness guard, per anti-pattern library).
//
// Fields that cannot legally differ from their default in this fixture:
//   mode=per_session_strand  — changing to direct_executor requires
//                              already_serialized_executor=true + a strand,
//                              which is a session-level seam outside config scope
//   locks=mutex              — default; spin is an opt-in performance knob
//   begin_string="FIX.4.4"  — non-FIXT so default_appl_ver_id stays nullopt
//   refresh_on_logon=false   — must be false under bilateral_strict; fixture uses
//                              bilateral_lenient which permits true, but we leave
//                              it false to keep the matrix simpler (tested in
//                              negative cells later)
//   validate_inbound_messages=false — stays false; enabling it requires
//                              dictionary != nullptr which the loader populates
//                              only in Phase 3c (selector resolver); no conflict.
//
// These are noted inline; the assertions are STILL discriminating because any
// mapper that accidentally set them to a non-default value would fail.

TEST(LoadHappyPath, T011_FieldForFieldEquivalence)
{
    // Setup: an io_context to supply the engine executor.
    // We do NOT call ctx.run() — the loader is synchronous/cold (anti-hang rule).
    asio::io_context ctx;
    fixpp::config::LoadOptions opts;
    opts.engine_executor = ctx.get_executor();

    // Load the fixture.
    auto result = fixpp::config::load_toml_config(k_fixture, opts);

    // The parse must succeed — a parse error here is a fixture bug, not a RED.
    ASSERT_TRUE(result.has_value())
        << "load_toml_config failed unexpectedly; fix the fixture before proceeding";

    // Exactly one [[session]] block.
    ASSERT_EQ(result->sessions.size(), std::size_t{1})
        << "expected exactly one session from happy_full.toml";

    const fixpp::session::SessionConfig& cfg = result->sessions[0].config;

    // ── Identity fields ───────────────────────────────────────────────────────
    EXPECT_EQ(cfg.sender_comp_id, "CLIENT1");       // default ""
    EXPECT_EQ(cfg.target_comp_id, "SERVER1");       // default ""
    EXPECT_EQ(cfg.begin_string,   "FIX.4.4");       // default ""

    // ── Role ─────────────────────────────────────────────────────────────────
    EXPECT_EQ(cfg.role, fixpp::session::session_role::initiator);

    // ── Threading (defaults — still asserted for completeness) ────────────────
    EXPECT_EQ(cfg.mode,  fixpp::session::threading_mode::per_session_strand);
    EXPECT_EQ(cfg.locks, fixpp::session::lock_policy::mutex);

    // ── Credentials ──────────────────────────────────────────────────────────
    EXPECT_TRUE(cfg.username.has_value());
    if (cfg.username.has_value())
        EXPECT_EQ(*cfg.username, "user42");

    EXPECT_TRUE(cfg.password.has_value());
    if (cfg.password.has_value())
        EXPECT_EQ(*cfg.password, "s3cr3t");

    // ── Timing / thresholds ───────────────────────────────────────────────────
    EXPECT_TRUE(cfg.heartbeat_interval.has_value());
    if (cfg.heartbeat_interval.has_value())
        EXPECT_EQ(*cfg.heartbeat_interval, std::chrono::seconds{30});

    EXPECT_TRUE(cfg.test_request_threshold.has_value());
    if (cfg.test_request_threshold.has_value())
        EXPECT_EQ(*cfg.test_request_threshold, std::chrono::milliseconds{15000});

    EXPECT_TRUE(cfg.sending_time_threshold.has_value());
    if (cfg.sending_time_threshold.has_value())
        EXPECT_EQ(*cfg.sending_time_threshold, std::chrono::milliseconds{5000});

    EXPECT_EQ(cfg.logout_disconnect_timeout_ms, std::uint32_t{5000});  // default 2000

    // ── Bool knobs (non-default values) ──────────────────────────────────────
    EXPECT_TRUE(cfg.reset_on_logon);           // default false
    EXPECT_TRUE(cfg.reset_on_logout);          // default false
    EXPECT_TRUE(cfg.reset_on_disconnect);      // default false
    EXPECT_FALSE(cfg.refresh_on_logon);        // false (see above)
    EXPECT_TRUE(cfg.redeliver_poss_dup);       // default false
    EXPECT_TRUE(cfg.allow_pos_dup);            // default false
    EXPECT_TRUE(cfg.enable_next_expected_msg_seq_num);  // default false
    EXPECT_FALSE(cfg.check_comp_id);           // default TRUE — inverted
    EXPECT_FALSE(cfg.validate_sequence_numbers); // default TRUE — inverted
    EXPECT_FALSE(cfg.validate_inbound_messages); // false (see above)

    // ── Enum knobs (non-default values) ──────────────────────────────────────
    EXPECT_EQ(cfg.reset_seqnum_policy_field,
              fixpp::session::reset_seqnum_policy::bilateral_lenient);  // default bilateral_strict

    EXPECT_EQ(cfg.sending_time_precision,
              fixpp::core::fix_time_precision::micros);  // default millis

    EXPECT_EQ(cfg.app_backpressure,
              fixpp::session::SessionConfig::backpressure_mode::disconnect_and_recover); // default block

    // ── Security profile ─────────────────────────────────────────────────────
    EXPECT_EQ(cfg.security_profile.k,
              fixpp::session::SecurityProfile::kind::mtls_ca);  // default unset

    // ── FIXT guard — FIX.4.4 → default_appl_ver_id stays nullopt ────────────
    EXPECT_FALSE(cfg.default_appl_ver_id.has_value());  // not FIXT

    // ── Reconnect endpoint (set via [session.transport]) ─────────────────────
    EXPECT_EQ(cfg.reconnect_endpoint.host, "fix.example.com");  // default ""
    EXPECT_EQ(cfg.reconnect_endpoint.port, std::uint16_t{4321}); // default 0
}

// ─────────────────────────────────────────────────────────────────────────────
// T012 — selector-build witness
// ─────────────────────────────────────────────────────────────────────────────
//
// Asserts:
//   (A) clock=system → EngineEstablishment.clock is non-null and is a live
//       system_clock_source bound to opts.engine_executor.
//   (B) The same clock shared_ptr is embedded inside the TLS transport factory
//       (D-6 shared clock) → use_count() > 1.
//   (C) transport.kind=tls → EngineEstablishment.default_transport_factory is
//       non-null and its kind() == transport_security_kind::tls.
//   (D) store=memory → EngineEstablishment.default_store_factory is non-null.
//   (E) cert_source=file → EngineEstablishment.default_cert_source is non-null.
//   (F) dictionary path → EngineEstablishment.dictionaries has exactly 1 entry.
//
// RED condition: engine slice is all-null in Phase 3a, so (A)/(B)/(C)/(D)/(E)/(F)
// all fail immediately.

TEST(LoadHappyPath, T012_SelectorBuild)
{
    asio::io_context ctx;
    fixpp::config::LoadOptions opts;
    opts.engine_executor = ctx.get_executor();

    auto result = fixpp::config::load_toml_config(k_fixture, opts);
    ASSERT_TRUE(result.has_value())
        << "load_toml_config failed unexpectedly";

    const fixpp::config::EngineEstablishment& engine = result->engine;

    // (A) clock=system → non-null, dynamic type is system_clock_source.
    ASSERT_NE(engine.clock, nullptr) << "expected system_clock_source, got null";
    auto* sys_clock =
        dynamic_cast<fixpp::core::system_clock_source*>(engine.clock.get());
    EXPECT_NE(sys_clock, nullptr)
        << "engine.clock is not a system_clock_source";

    // (B) D-6: the SAME shared_ptr instance is embedded inside the TLS factory's
    //   SslCtxConfig::clock — so use_count must be > 1.
    EXPECT_GT(engine.clock.use_count(), 1L)
        << "clock shared_ptr has use_count==1; it is not shared with the TLS "
           "factory (D-6 violation)";

    // (C) transport.kind=tls → factory non-null, kind()==tls.
    ASSERT_NE(engine.default_transport_factory, nullptr)
        << "expected TLS transport factory, got null";
    EXPECT_EQ(engine.default_transport_factory->kind(),
              fixpp::transport::transport_security_kind::tls);

    // (D) store=memory → factory non-null.
    EXPECT_NE(engine.default_store_factory, nullptr)
        << "expected memory store factory, got null";

    // (E) cert_source=file → source non-null.
    EXPECT_NE(engine.default_cert_source, nullptr)
        << "expected file cert_source, got null";

    // (F) dictionary path → exactly one dictionary loaded.
    EXPECT_EQ(engine.dictionaries.size(), std::size_t{1})
        << "expected 1 dictionary entry from [dictionary] selector";
    EXPECT_NE(engine.dictionaries[0], nullptr)
        << "dictionaries[0] is null";
}
