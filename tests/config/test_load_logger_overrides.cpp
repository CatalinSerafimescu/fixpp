// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/config/test_load_logger_overrides.cpp
// 045-observability-config Phase-5 TDD test (T018).
//
// T018 [P] [US3] — Multi-session override test.
//
//   Loads logger_multisession.toml which contains:
//     - An engine-level [logger] with a file sink (directory logs/t018_engine).
//     - session[0] with a [session.logger] override (DISTINCT dir/base_name).
//     - session[1] with NO [session.logger] — logger_override stays null.
//
//   Assertions (discriminating, per [[feedback_witness_asserts_named_postcondition_not_proxy]]):
//     (a) bundle.engine.logger != nullptr              (engine default present)
//     (b) sessions[0].config.logger_override != nullptr
//         AND != engine.logger  (distinct instances — a resolver that aliased
//                                the engine logger into the session would fail)
//         AND writing to logs/t018_session0 (behavioural dir check — the
//             strong discriminant against a resolver that built session[0]'s
//             override from the engine's config block)
//     (c) sessions[1].config.logger_override == nullptr
//         (null = session inherits engine default; a resolver that built a
//          logger for every session regardless of a per-session override key
//          would fail this cell)
//
//   RED condition (Phase 5 before T019 wiring):
//     The per-session resolution loop does not call resolve_engine_logger for
//     [session.logger]; sessions[0].config.logger_override stays null.
//     Assertion (b) → ASSERT_NE(session0_override, nullptr) FAILS.
//     This is the correct RED: "per-session logger override not resolved."
//
// N-2 cross-check note: T015 already covers the "broken session logger
//   suppresses ALL construction" path (data-model D-7 N-2). This test focuses
//   only on the happy-path multi-session wiring.
//
// File-sink directory pre-existence: the resolver preflight (E-4) requires
//   the configured directory to already exist. Each cell calls
//   std::filesystem::create_directories() before loading.
//
// Cleanup: log files are written to /tmp directories and left there.
//   The test uses remove_all + create_directories to ensure each run is clean.
//
// Anti-hang: load_toml_config is synchronous. The asio::io_context is
//   constructed only to satisfy opts.engine_executor.

#include <gtest/gtest.h>

#include <asio/io_context.hpp>
#include <chrono>
#include <filesystem>
#include <fixpp/config/config_bundle.hpp>
#include <fixpp/config/toml_config_loader.hpp>
#include <fixpp/log/logger.hpp>
#include <string>
#include <vector>

#ifndef FIXPP_CONFIG_FIXTURE_DIR
#error "FIXPP_CONFIG_FIXTURE_DIR must be set by CMake"
#endif

// ── Helpers ──────────────────────────────────────────────────────────────────

namespace {

fixpp::config::LoadResult load_fixture(std::string_view name) {
    const std::filesystem::path p =
        std::filesystem::path{std::string{FIXPP_CONFIG_FIXTURE_DIR}} / name;
    asio::io_context ctx;
    fixpp::config::LoadOptions opts;
    opts.engine_executor = ctx.get_executor();
    return fixpp::config::load_toml_config(p, opts);
}

std::string diag_string(const std::vector<fixpp::config::LoadDiagnostic>& diags) {
    std::string s;
    for (const auto& d : diags) {
        s += "[" + d.key_path + "] " + d.message + "\n";
    }
    return s;
}

std::filesystem::path fixture_dir() {
    return std::filesystem::path{std::string{FIXPP_CONFIG_FIXTURE_DIR}};
}

}  // namespace

// =============================================================================
// T018 — Multi-session logger override test
// =============================================================================

TEST(LoadLoggerOverrides, T018_MultiSessionOverride) {
    // Pre-create BOTH sink directories that the fixture references.
    // remove_all first so file-existence witnesses below reflect THIS run only.
    const auto engine_log_dir = fixture_dir() / "logs" / "t018_engine";
    const auto session0_log_dir = fixture_dir() / "logs" / "t018_session0";
    {
        std::error_code ec;
        std::filesystem::remove_all(engine_log_dir, ec);
        std::filesystem::remove_all(session0_log_dir, ec);
        std::filesystem::create_directories(engine_log_dir, ec);
        ASSERT_FALSE(ec) << "Failed to create engine log dir: " << engine_log_dir;
        std::filesystem::create_directories(session0_log_dir, ec);
        ASSERT_FALSE(ec) << "Failed to create session0 log dir: " << session0_log_dir;
    }

    auto result = load_fixture("logger_multisession.toml");

    ASSERT_TRUE(result.has_value())
        << "logger_multisession.toml must load successfully; diagnostics:\n"
        << (result.has_value() ? "" : diag_string(result.error()));

    ASSERT_EQ(result->sessions.size(), std::size_t{2})
        << "expected exactly two sessions";

    // ── Assertion (a): engine logger is non-null ──────────────────────────────
    //
    // RED before T019: this may or may not fail depending on whether the engine
    // logger was resolved (it should be — engine-level resolver is already wired).
    ASSERT_NE(result->engine.logger, nullptr)
        << "engine.logger must be non-null; engine-level [logger] resolver "
           "not wired correctly";

    // ── Assertion (b): session[0] has a DISTINCT override logger ─────────────
    //
    // RED before T019 wiring: sessions[0].config.logger_override stays null
    // because the per-session loop does not call resolve_engine_logger for
    // [session.logger]. ASSERT_NE below fails — this is the expected RED.
    const auto& session0_override = result->sessions[0].config.logger_override;

    ASSERT_NE(session0_override, nullptr)
        << "sessions[0].config.logger_override must be non-null; the session "
           "has a [session.logger] block but per-session resolution (T019) is "
           "not yet wired — this is the expected RED before T019 implementation";

    // Instance-inequality: a resolver that aliased the engine logger into the
    // session would fail here.
    ASSERT_NE(session0_override.get(), result->engine.logger.get())
        << "sessions[0].config.logger_override must be a DISTINCT Logger "
           "instance from engine.logger (not an alias)";

    // Behavioural discriminant: emit through the OVERRIDE logger, shutdown,
    // verify a file appears in the OVERRIDE's dir (logs/t018_session0) and NOT
    // in the engine's dir (logs/t018_engine).
    //
    // This is the strong discriminant per [[feedback_witness_asserts_named_postcondition_not_proxy]]:
    // instance-inequality alone doesn't prove the override was built from the
    // session's OWN config block (two make_shared calls on the engine's config
    // would give distinct instances but identical dir targets).
    {
        auto& override_logger = *session0_override;
        FIXPP_LOG0(&override_logger, info, fixpp::log::cat::session,
                   "T018_MultiSessionOverride session0 probe record");
        [[maybe_unused]] auto res = override_logger.shutdown();
    }

    // File must appear in session0's dir with the override base_name.
    // This is the strong discriminant: a resolver that built session[0]'s override
    // from the ENGINE's config (wrong directory) would fail here because the file
    // would appear in the engine dir (t018_engine) not the session0 dir (t018_session0).
    bool found_in_session0_dir = false;
    {
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(session0_log_dir, ec)) {
            if (entry.path().filename().string().starts_with("fixpp_t018_session0")) {
                found_in_session0_dir = true;
                break;
            }
        }
    }
    EXPECT_TRUE(found_in_session0_dir)
        << "Expected a log file starting with 'fixpp_t018_session0' in "
        << session0_log_dir
        << "; file not found — override logger wrote to wrong directory, or "
           "the session override was not built from session[0]'s [session.logger] config";

    // Also verify the engine logger wrote to ITS own dir (confirms the engine
    // logger was built from the engine [logger] block, not the session block).
    // FileSink creates the log file on open() — both loggers produce files in
    // their own configured directories.
    bool found_in_engine_dir = false;
    {
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(engine_log_dir, ec)) {
            if (entry.path().filename().string().starts_with("fixpp_t018_engine")) {
                found_in_engine_dir = true;
                break;
            }
        }
    }
    EXPECT_TRUE(found_in_engine_dir)
        << "Expected a log file starting with 'fixpp_t018_engine' in "
        << engine_log_dir
        << "; engine logger file not found in its configured directory";

    // Shutdown engine logger too (resource cleanup).
    {
        [[maybe_unused]] auto res = result->engine.logger->shutdown();
    }

    // ── Assertion (c): session[1] has NO logger override ─────────────────────
    //
    // A resolver that built a logger for every session (ignoring the absence of
    // [session.logger] in session[1]) would fail here.
    const auto& session1_override = result->sessions[1].config.logger_override;
    EXPECT_EQ(session1_override, nullptr)
        << "sessions[1].config.logger_override must be null; session[1] has "
           "no [session.logger] block so it should inherit the engine default "
           "(null override), not get a constructed override logger";
}
