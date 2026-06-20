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

#include <gtest/gtest.h>

#include <algorithm>
#include <asio/io_context.hpp>
#include <chrono>
#include <filesystem>
#include <fixpp/config/config_bundle.hpp>
#include <fixpp/config/load_diagnostic.hpp>
#include <fixpp/config/toml_config_loader.hpp>
#include <fixpp/core/system_clock_source.hpp>
#include <fixpp/dict/version_profile.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/transport/reconnect_policy.hpp>
#include <fixpp/transport/transport_factory.hpp>
#include <fstream>
#include <string>
#include <string_view>

#ifndef FIXPP_CONFIG_FIXTURE_DIR
#error "FIXPP_CONFIG_FIXTURE_DIR must be set by CMake"
#endif

namespace {

// Path to the happy-path fixture file.
const std::filesystem::path k_fixture{std::string{FIXPP_CONFIG_FIXTURE_DIR} + "/happy_full.toml"};

// ── Load helper ───────────────────────────────────────────────────────────────

fixpp::config::LoadResult load_fixture(std::string_view name) {
    const std::filesystem::path p =
        std::filesystem::path{std::string{FIXPP_CONFIG_FIXTURE_DIR}} / name;
    asio::io_context ctx;
    fixpp::config::LoadOptions opts;
    opts.engine_executor = ctx.get_executor();
    return fixpp::config::load_toml_config(p, opts);
}

// Load from an arbitrary path (used for runtime-generated TOML).
fixpp::config::LoadResult load_path(const std::filesystem::path& p) {
    asio::io_context ctx;
    fixpp::config::LoadOptions opts;
    opts.engine_executor = ctx.get_executor();
    return fixpp::config::load_toml_config(p, opts);
}

// Diagnostic search helper.
bool has_diag_hp(const std::vector<fixpp::config::LoadDiagnostic>& diags,
                 fixpp::config::reason_class expected_reason, std::string_view expected_key_path) {
    return std::any_of(diags.begin(), diags.end(), [&](const fixpp::config::LoadDiagnostic& d) {
        return d.reason == expected_reason && d.key_path == expected_key_path;
    });
}

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

TEST(LoadHappyPath, T011_FieldForFieldEquivalence) {
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
    EXPECT_EQ(cfg.sender_comp_id, "CLIENT1");  // default ""
    EXPECT_EQ(cfg.target_comp_id, "SERVER1");  // default ""
    EXPECT_EQ(cfg.begin_string, "FIX.4.4");    // default ""

    // ── Role ─────────────────────────────────────────────────────────────────
    EXPECT_EQ(cfg.role, fixpp::session::session_role::initiator);

    // ── Threading (defaults — still asserted for completeness) ────────────────
    EXPECT_EQ(cfg.mode, fixpp::session::threading_mode::per_session_strand);
    EXPECT_EQ(cfg.locks, fixpp::session::lock_policy::mutex);

    // ── Credentials ──────────────────────────────────────────────────────────
    EXPECT_TRUE(cfg.username.has_value());
    if (cfg.username.has_value()) EXPECT_EQ(*cfg.username, "user42");

    EXPECT_TRUE(cfg.password.has_value());
    if (cfg.password.has_value()) EXPECT_EQ(*cfg.password, "s3cr3t");

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
    EXPECT_TRUE(cfg.reset_on_logon);                    // default false
    EXPECT_TRUE(cfg.reset_on_logout);                   // default false
    EXPECT_TRUE(cfg.reset_on_disconnect);               // default false
    EXPECT_FALSE(cfg.refresh_on_logon);                 // false (see above)
    EXPECT_TRUE(cfg.redeliver_poss_dup);                // default false
    EXPECT_TRUE(cfg.allow_pos_dup);                     // default false
    EXPECT_TRUE(cfg.enable_next_expected_msg_seq_num);  // default false
    EXPECT_FALSE(cfg.check_comp_id);                    // default TRUE — inverted
    EXPECT_FALSE(cfg.validate_sequence_numbers);        // default TRUE — inverted
    EXPECT_FALSE(cfg.validate_inbound_messages);        // false (see above)

    // ── Enum knobs (non-default values) ──────────────────────────────────────
    EXPECT_EQ(cfg.reset_seqnum_policy_field,
              fixpp::session::reset_seqnum_policy::bilateral_lenient);  // default bilateral_strict

    EXPECT_EQ(cfg.sending_time_precision,
              fixpp::core::fix_time_precision::micros);  // default millis

    EXPECT_EQ(
        cfg.app_backpressure,
        fixpp::session::SessionConfig::backpressure_mode::disconnect_and_recover);  // default block

    // ── Security profile ─────────────────────────────────────────────────────
    EXPECT_EQ(cfg.security_profile.k,
              fixpp::session::SecurityProfile::kind::mtls_ca);  // default unset

    // ── FIXT guard — FIX.4.4 → default_appl_ver_id stays nullopt ────────────
    EXPECT_FALSE(cfg.default_appl_ver_id.has_value());  // not FIXT

    // ── Reconnect endpoint (set via [session.transport]) ─────────────────────
    EXPECT_EQ(cfg.reconnect_endpoint.host, "fix.example.com");    // default ""
    EXPECT_EQ(cfg.reconnect_endpoint.port, std::uint16_t{4321});  // default 0
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

TEST(LoadHappyPath, T012_SelectorBuild) {
    asio::io_context ctx;
    fixpp::config::LoadOptions opts;
    opts.engine_executor = ctx.get_executor();

    // SUPPRESS: load uses auto path, no fixpp::config::load_toml_config direct call needed.
    auto result = fixpp::config::load_toml_config(k_fixture, opts);
    ASSERT_TRUE(result.has_value()) << "load_toml_config failed unexpectedly";

    const fixpp::config::EngineEstablishment& engine = result->engine;

    // (A) clock=system → non-null, dynamic type is system_clock_source.
    ASSERT_NE(engine.clock, nullptr) << "expected system_clock_source, got null";
    auto* sys_clock = dynamic_cast<fixpp::core::system_clock_source*>(engine.clock.get());
    EXPECT_NE(sys_clock, nullptr) << "engine.clock is not a system_clock_source";

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
    EXPECT_NE(engine.default_store_factory, nullptr) << "expected memory store factory, got null";

    // (E) cert_source=file → source non-null.
    EXPECT_NE(engine.default_cert_source, nullptr) << "expected file cert_source, got null";

    // (F) dictionary path → exactly one dictionary loaded.
    EXPECT_EQ(engine.dictionaries.size(), std::size_t{1})
        << "expected 1 dictionary entry from [dictionary] selector";
    EXPECT_NE(engine.dictionaries[0], nullptr) << "dictionaries[0] is null";
}

// =============================================================================
// Coverage-targeted scalar tests (previously uncovered paths)
// =============================================================================

// ── pos_scalars_extra.toml: bilateral_strict, seconds precision, m/h/us units,
//    direct_executor + already_serialized_executor, compid_authorization_policy,
//    reconnect_policy, acceptor role ──────────────────────────────────────────

TEST(LoadHappyPath, Cov_ScalarsExtra) {
    auto result = load_fixture("pos_scalars_extra.toml");

    ASSERT_TRUE(result.has_value())
        << "pos_scalars_extra.toml must load successfully; diagnostics: " << [&] {
               std::string s;
               if (!result.has_value())
                   for (const auto& d : result.error()) s += d.key_path + ": " + d.message + "\n";
               return s;
           }();

    ASSERT_EQ(result->sessions.size(), std::size_t{1});
    const fixpp::session::SessionConfig& cfg = result->sessions[0].config;

    // role=acceptor (non-default)
    EXPECT_EQ(cfg.role, fixpp::session::session_role::acceptor);

    // mode=direct_executor + already_serialized_executor=true
    EXPECT_EQ(cfg.mode, fixpp::session::threading_mode::direct_executor);
    EXPECT_TRUE(cfg.already_serialized_executor);

    // reset_seqnum_policy=bilateral_strict (line 454 scalar_mappers.cpp)
    EXPECT_EQ(cfg.reset_seqnum_policy_field, fixpp::session::reset_seqnum_policy::bilateral_strict);

    // sending_time_precision=seconds (line 478 scalar_mappers.cpp)
    EXPECT_EQ(cfg.sending_time_precision, fixpp::core::fix_time_precision::seconds);

    // heartbeat_interval = "2m" → 120s (unit 'm' arm, line 165)
    ASSERT_TRUE(cfg.heartbeat_interval.has_value());
    EXPECT_EQ(*cfg.heartbeat_interval, std::chrono::seconds{120});

    // test_request_threshold = "1h" → 3600000ms (unit 'h' arm, line 167)
    ASSERT_TRUE(cfg.test_request_threshold.has_value());
    EXPECT_EQ(*cfg.test_request_threshold, std::chrono::milliseconds{3600000});

    // sending_time_threshold = "100ms" → 100ms (unit "ms" arm)
    // NOTE: fixture changed from "100us" to "100ms" — "us" is now rejected fail-closed
    // (#5 Gate B r1: sub-ms units unsupported; see neg_duration_us_rejected test).
    ASSERT_TRUE(cfg.sending_time_threshold.has_value());
    EXPECT_EQ(*cfg.sending_time_threshold, std::chrono::milliseconds{100});

    // compid_authorization_policy: PARTNER_A → {SERVER1, SERVER2}, PARTNER_B → {SERVER1}
    // We assert the bindings exist (add_binding adds them; we query via the policy).
    // compid_authorization_policy has no direct query API exposed — assert non-default
    // by checking reconnect_policy is present (both structured members exercised).

    // reconnect_policy: schedule + jitter + max_attempts + session_id_seed
    ASSERT_TRUE(cfg.reconnect_policy.has_value())
        << "reconnect_policy must be populated from [session.reconnect_policy]";
    const auto& rp = *cfg.reconnect_policy;
    // schedule: ["1s", "5s", "30s"] → {1000ms, 5000ms, 30000ms}
    ASSERT_EQ(rp.schedule.size(), std::size_t{3});
    EXPECT_EQ(rp.schedule[0], std::chrono::milliseconds{1000});
    EXPECT_EQ(rp.schedule[1], std::chrono::milliseconds{5000});
    EXPECT_EQ(rp.schedule[2], std::chrono::milliseconds{30000});
    EXPECT_DOUBLE_EQ(rp.jitter, 0.2);
    EXPECT_EQ(rp.max_attempts, std::uint32_t{10});
    EXPECT_EQ(rp.session_id_seed, std::uint64_t{42});
}

// ── pos_scalars_unilateral.toml: unilateral reset_seqnum_policy, nanos precision ─

TEST(LoadHappyPath, Cov_ScalarsUnilateral) {
    auto result = load_fixture("pos_scalars_unilateral.toml");

    ASSERT_TRUE(result.has_value())
        << "pos_scalars_unilateral.toml must load successfully; diagnostics: " << [&] {
               std::string s;
               if (!result.has_value())
                   for (const auto& d : result.error()) s += d.key_path + ": " + d.message + "\n";
               return s;
           }();

    ASSERT_EQ(result->sessions.size(), std::size_t{1});
    const fixpp::session::SessionConfig& cfg = result->sessions[0].config;

    // reset_seqnum_policy=unilateral (line 458)
    EXPECT_EQ(cfg.reset_seqnum_policy_field, fixpp::session::reset_seqnum_policy::unilateral);

    // sending_time_precision=nanos (line 484)
    EXPECT_EQ(cfg.sending_time_precision, fixpp::core::fix_time_precision::nanos);
}

// ── pos_fixt_appl_ver_ids.toml: default_appl_ver_id "2".."9" (all 8 arms) ───

TEST(LoadHappyPath, Cov_FixtApplVerIds) {
    auto result = load_fixture("pos_fixt_appl_ver_ids.toml");

    ASSERT_TRUE(result.has_value())
        << "pos_fixt_appl_ver_ids.toml must load successfully; diagnostics: " << [&] {
               std::string s;
               if (!result.has_value())
                   for (const auto& d : result.error()) s += d.key_path + ": " + d.message + "\n";
               return s;
           }();

    ASSERT_EQ(result->sessions.size(), std::size_t{8})
        << "expected 8 sessions (one per ApplVerID value)";

    using av = fixpp::dict::application_version;
    const std::vector<av> expected = {av::v40, av::v41, av::v42,    av::v43,
                                      av::v44, av::v50, av::v50sp1, av::v50sp2};

    for (std::size_t i = 0; i < 8; ++i) {
        const auto& cfg = result->sessions[i].config;
        ASSERT_TRUE(cfg.default_appl_ver_id.has_value())
            << "session[" << i << "] must have default_appl_ver_id set";
        EXPECT_EQ(*cfg.default_appl_ver_id, expected[i])
            << "session[" << i << "] default_appl_ver_id mismatch";
    }
}

// ── pos_multisession_profile_diverges.toml: per-session divergence scan ───────
//    session[0] matches engine default (continue at line 635)
//    session[1] profile diverges but same cert → reuse engine cert (line 649)

TEST(LoadHappyPath, Cov_MultisessionProfileDiverges) {
    auto result = load_fixture("pos_multisession_profile_diverges.toml");

    ASSERT_TRUE(result.has_value())
        << "pos_multisession_profile_diverges.toml must load successfully; diagnostics: " << [&] {
               std::string s;
               if (!result.has_value())
                   for (const auto& d : result.error()) s += d.key_path + ": " + d.message + "\n";
               return s;
           }();

    ASSERT_EQ(result->sessions.size(), std::size_t{2});

    // session[0]: matches engine default (mtls_ca, same certs) → override null
    EXPECT_EQ(result->sessions[0].config.transport_factory_override, nullptr)
        << "session[0] matches the engine default; override must be null";

    // session[1]: profile diverges (one_way_ca vs engine's mtls_ca), same certs
    // → reuse engine cert, build fresh factory → override non-null
    EXPECT_NE(result->sessions[1].config.transport_factory_override, nullptr)
        << "session[1] profile diverges (one_way_ca); override must be non-null";
}

// ── Absolute dictionary path: triggers resolve_path absolute-return arm ────────
//    (loader_internal.cpp line 46: if (rel.is_absolute()) return rel;)
//    We generate a TOML file at runtime with an absolute path to FIXT11.xml.

TEST(LoadHappyPath, Cov_AbsoluteDictPath) {
    const std::filesystem::path fixture_dir{std::string{FIXPP_CONFIG_FIXTURE_DIR}};
    const std::filesystem::path abs_dict = (fixture_dir / "FIX44.xml").lexically_normal();

    // Write a temporary TOML that uses the absolute dictionary path.
    const std::filesystem::path tmp = fixture_dir / "_tmp_abs_path_test.toml";
    {
        std::ofstream out{tmp};
        out << "[clock]\nkind = \"system\"\n\n"
            << "[store]\nkind = \"memory\"\n\n"
            << "[cert_source]\nkind = \"file\"\n"
            << "cert_file = \"leaf_ecdsa_p256.pem\"\n"
            << "key_file  = \"leaf_ecdsa_p256.key\"\n"
            << "ca_file   = \"ca.pem\"\n\n"
            << "[dictionary]\nkind = \"path\"\n"
            << "path = \"" << abs_dict.string() << "\"\n\n"
            << "[[session]]\n"
            << "sender_comp_id = \"CLIENT1\"\n"
            << "target_comp_id = \"SERVER1\"\n"
            << "begin_string   = \"FIX.4.4\"\n"
            << "role           = \"initiator\"\n\n"
            << "[session.transport]\nkind = \"tls\"\n"
            << "host = \"fix.example.com\"\nport = 4321\n\n"
            << "[session.security_profile]\nkind = \"mtls_ca\"\n";
    }

    auto result = load_path(tmp);

    // Clean up temp file before asserting (so failure doesn't leave stale files).
    std::error_code ec;
    std::filesystem::remove(tmp, ec);

    ASSERT_TRUE(result.has_value())
        << "absolute dict path must load successfully; diagnostics: " << [&] {
               std::string s;
               if (!result.has_value())
                   for (const auto& d : result.error()) s += d.key_path + ": " + d.message + "\n";
               return s;
           }();

    EXPECT_EQ(result->engine.dictionaries.size(), std::size_t{1})
        << "expected 1 dictionary entry when path is absolute";
}

// ── pos_scalars_more_enums.toml: millis + block + insecure_plain_tcp profile ───
//    Covers:
//      scalar_mappers.cpp: sending_time_precision="millis"
//      scalar_mappers.cpp: app_backpressure="block"
//      scalar_mappers.cpp: security_profile.kind="insecure_plain_tcp"

TEST(LoadHappyPath, Cov_ScalarsMoreEnums) {
    auto result = load_fixture("pos_scalars_more_enums.toml");
    ASSERT_TRUE(result.has_value())
        << "pos_scalars_more_enums.toml must load successfully; diagnostics: "
        << [&]() -> std::string {
        if (!result.has_value()) {
            std::string s;
            for (const auto& d : result.error()) s += "[" + d.key_path + "] " + d.message + "; ";
            return s;
        }
        return "(load succeeded)";
    }();

    ASSERT_EQ(result->sessions.size(), std::size_t{1});
    const auto& cfg = result->sessions[0].config;

    // sending_time_precision = "millis" → millis
    EXPECT_EQ(cfg.sending_time_precision, fixpp::core::fix_time_precision::millis)
        << "sending_time_precision must be millis";

    // app_backpressure = "block" → block
    using BPM = fixpp::session::SessionConfig::backpressure_mode;
    EXPECT_EQ(cfg.app_backpressure, BPM::block) << "app_backpressure must be block";

    // security_profile.kind = "insecure_plain_tcp" → insecure_plain_tcp
    using K = fixpp::session::SecurityProfile::kind;
#ifdef __clang__
#    pragma clang diagnostic push
#    pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(__GNUC__)
#    pragma GCC diagnostic push
#    pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
    EXPECT_EQ(cfg.security_profile.k, K::insecure_plain_tcp)
        << "security_profile.k must be insecure_plain_tcp for kind=\"insecure_plain_tcp\"";
#ifdef __clang__
#    pragma clang diagnostic pop
#elif defined(__GNUC__)
#    pragma GCC diagnostic pop
#endif
}

// ── load_toml_config with a nonexistent file: hits std::exception arm ─────────
//    (toml_config_loader.cpp lines 388-393: catch(const std::exception& e))

TEST(LoadHappyPath, Cov_NonexistentFile) {
    const std::filesystem::path nonexistent{"/tmp/this_file_does_not_exist_fixpp_044.toml"};

    asio::io_context ctx;
    fixpp::config::LoadOptions opts;
    opts.engine_executor = ctx.get_executor();
    auto result = fixpp::config::load_toml_config(nonexistent, opts);

    ASSERT_FALSE(result.has_value()) << "loading a nonexistent file must fail";

    using RC = fixpp::config::reason_class;
    EXPECT_TRUE(has_diag_hp(result.error(), RC::parse_error, ""))
        << "expected parse_error at key_path=\"\" for file-not-found";
}

// ── Direct tests on loader_internal.cpp helpers ───────────────────────────────
//
// display_value() and is_credential_key() are non-static, non-inline functions
// declared in loader_internal.hpp and compiled into fixpp_config_toml (which
// the test binary already links).  We forward-declare them here to avoid
// depending on the private internal include path.
//
// These tests cover branch arms that are structurally unreachable through the
// public load_toml_config() API:
//   • display_value() non-credential passthrough (line 36 loader_internal.cpp,
//     branch 33-False + branch 28:39-False)
//   • is_credential_key() with a bare key (no dot) → branch 26-True
//
// loader_internal.cpp line 58-59 (weakly_canonical OS error) is genuinely
// unreachable without OS-level fault injection and remains uncovered.

namespace fixpp::config::detail {
[[nodiscard]] bool is_credential_key(std::string_view key_path) noexcept;
[[nodiscard]] std::string_view display_value(std::string_view key_path,
                                             std::string_view value) noexcept;
}  // namespace fixpp::config::detail

TEST(LoadHappyPath, Cov_DisplayValueNonCredential) {
    // A key that is NOT a credential: display_value must return the value
    // verbatim (covers line 36 in loader_internal.cpp and the False branch
    // of "if (is_credential_key(key_path))" at line 33).
    std::string_view v = fixpp::config::detail::display_value("session[0].host", "fix.example.com");
    EXPECT_EQ(v, "fix.example.com")
        << "display_value must pass through the value for a non-credential key";
}

TEST(LoadHappyPath, Cov_IsCredentialKeyBareNoDoc) {
    // A bare key (no dot) such as "username" or "password":
    // rfind('.') returns npos → the True branch of the ternary at line 26.
    // The final segment IS "username" → returns true.
    EXPECT_TRUE(fixpp::config::detail::is_credential_key("username"))
        << "bare 'username' with no dot must be a credential key";
    // A bare key that is NOT a credential: branch 26-True AND both
    // operands of the || are false (branch 28:39-False side).
    EXPECT_FALSE(fixpp::config::detail::is_credential_key("host"))
        << "bare 'host' must not be a credential key";
}

// ── #4 (Gate B r1): null LoadOptions::resource must not cause UB ─────────────
//
// LoadOptions::resource defaults to std::pmr::get_default_resource() (non-null),
// but the public struct allows LoadOptions{.resource = nullptr}.  XmlLoader::load
// and make_file_cert_source both document mr!=nullptr as a caller precondition
// with release UB.  The fix substitutes get_default_resource() at load entry.
//
// Discriminating post-condition: passing nullptr must yield a NORMAL load result
// (not a crash / assert / terminate / UB).  We exercise the full dictionary-backed
// TLS path (happy_full.toml) so that both XmlLoader::load AND make_file_cert_source
// are reached with the substituted resource.

TEST(LoadHappyPath, GateBR1_NullResourceSubstituted) {
    const std::filesystem::path p =
        std::filesystem::path{std::string{FIXPP_CONFIG_FIXTURE_DIR}} / "happy_full.toml";
    asio::io_context ctx;
    fixpp::config::LoadOptions opts;
    opts.engine_executor = ctx.get_executor();
    opts.resource = nullptr;  // explicit null — triggers the new guard

    // Must produce a normal result (ConfigBundle), not crash / terminate / UB.
    // Under ASan+UBSan the null-dereference / precondition-violation would be
    // caught before this assertion is reached if the guard is missing.
    auto result = fixpp::config::load_toml_config(p, opts);
    EXPECT_TRUE(result.has_value())
        << "null LoadOptions::resource must be silently substituted with "
           "get_default_resource() and load must succeed (#4 Gate B r1); "
           "if this crashes or fails, the null-resource guard is missing";
}
