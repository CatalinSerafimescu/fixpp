// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/config/test_load_deferred_surface.cpp
// 045-observability-config Phase-6 TDD tests (T020, T021).
//
// T020 [P] [US4] — Deferred-surface test.
//   Each still-deferred observability/arena/dialect/tap key placed at the
//   TOP-LEVEL authoring scope (root table) must yield:
//     reason_class::recognized_not_yet_supported_step2
//     key_path == "<key>" (no "session[N]." prefix)
//
//   Per data-model E-5 / research D-6 / FR-022, the still-deferred keys are:
//     tracer, meter, message_arena, dialect_overlay, tap
//   (Additional deferred keys in kDeferred are exercised indirectly via the
//    existing session-scope cells in test_load_negative_battery.cpp.)
//
//   Typo-distinction cell (SC-007):
//     A typo'd key "loggerr" (not in kDeferred, not in kRecognized) must
//     yield reason_class::unknown_key at "loggerr", NOT the deferred reason.
//     AND must NOT produce recognized_not_yet_supported_step2 at "loggerr".
//     This proves the two reason classes never conflate.
//
//   Logger-not-deferred cell (FR-022 / the one-key flip):
//     A [logger] block loaded against a pre-created file-sink directory must
//     NOT produce recognized_not_yet_supported_step2 at "logger".
//     (Logger was flipped from kDeferred → kRecognized in T006 / 045.)
//
// T021 [US4] — Confirmed: the T006 recognize_keys() disposition in
//   toml_config_loader.cpp already satisfies these tests (kDeferred has
//   logger removed; all 5 keys above remain deferred).  T020 is the witness;
//   no source change is needed.
//
// Edge note (session-scope deferred keys): recognize_keys() runs the same
//   kDeferred/kRecognized logic for both root and per-session scopes —
//   only the key_prefix changes ("" vs "session[N]").  Top-level coverage
//   here is sufficient per E-5; existing cells in test_load_negative_battery
//   cover the session-scope case for dialect_overlay and tracer.
//
// Anti-hang: load_toml_config is synchronous (cold).  asio::io_context is
//   created only to satisfy system_clock_source; ctx.run() is never called.
//
// Fixture sources:
//   neg_deferred_toplevel.toml — multi-key fixture: tracer, meter, message_arena,
//                                dialect_overlay, tap at root + valid [[session]]
//   neg_deferred_typo.toml    — single typo'd key "loggerr" at root + valid [[session]]
//   logger_happy.toml         — [logger] with file sink; test pre-creates the dir
//                               via temp_directory_path()
//
// Anchors: spec.md US4 / SC-007; data-model E-5; research D-6; FR-022/FR-025.

#include <gtest/gtest.h>

#include <asio/io_context.hpp>
#include <filesystem>
#include <fixpp/config/load_diagnostic.hpp>
#include <fixpp/config/toml_config_loader.hpp>
#include <fixpp/log/logger.hpp>
#include <fstream>
#include <string>

#ifndef FIXPP_CONFIG_FIXTURE_DIR
#error "FIXPP_CONFIG_FIXTURE_DIR must be set by CMake"
#endif

namespace {

// ── Helpers ──────────────────────────────────────────────────────────────────

using RC = fixpp::config::reason_class;

std::filesystem::path fixture_path(std::string_view name) {
    return std::filesystem::path{std::string{FIXPP_CONFIG_FIXTURE_DIR}} / name;
}

fixpp::config::LoadResult load(const std::filesystem::path& p) {
    asio::io_context ctx;
    fixpp::config::LoadOptions opts;
    opts.engine_executor = ctx.get_executor();
    return fixpp::config::load_toml_config(p, opts);
}

// Discriminating check: reason AND key_path must both match.
bool has_diag(const std::vector<fixpp::config::LoadDiagnostic>& diags,
              RC expected_reason, std::string_view expected_key_path) {
    for (const auto& d : diags) {
        if (d.reason == expected_reason && d.key_path == expected_key_path) {
            return true;
        }
    }
    return false;
}

// Dump diagnostics as a human-readable string for ASSERT messages.
std::string diag_string(const std::vector<fixpp::config::LoadDiagnostic>& diags) {
    std::string s;
    for (const auto& d : diags) {
        s += "[reason=" + std::to_string(static_cast<int>(d.reason)) + " key=" + d.key_path +
             "] " + d.message + "\n";
    }
    return s;
}

}  // namespace

// =============================================================================
// T020 — Deferred-surface tests (US4 / SC-007)
// =============================================================================

// ── T020_DeferredTracer — [tracer] at root scope ──────────────────────────────
//
// A top-level [tracer] block (OTLP trace export unimplemented at the provider
// layer; data-model E-5 / research D-6) must yield:
//   reason_class::recognized_not_yet_supported_step2 at key_path "tracer"
//
// Fixture: neg_deferred_toplevel.toml (also carries meter, message_arena,
// dialect_overlay, tap — each tested in their own cell below).

TEST(LoadDeferredSurface, T020_DeferredTracer) {
    auto result = load(fixture_path("neg_deferred_toplevel.toml"));

    ASSERT_FALSE(result.has_value())
        << "neg_deferred_toplevel.toml must fail (deferred keys present)";

    const auto& diags = result.error();
    EXPECT_TRUE(has_diag(diags, RC::recognized_not_yet_supported_step2, "tracer"))
        << "expected recognized_not_yet_supported_step2 at key_path \"tracer\"; "
           "diagnostics:\n"
        << diag_string(diags);

    // Must NOT be mis-classified as an unknown key
    EXPECT_FALSE(has_diag(diags, RC::unknown_key, "tracer"))
        << "tracer must NOT produce unknown_key (it is a known-deferred key, "
           "not a typo) — SC-007 requires the two reason classes never conflate";
}

// ── T020_DeferredMeter — [meter] at root scope ───────────────────────────────
//
// A top-level [meter] block (OTLP metric export unimplemented; data-model E-5)
// must yield:
//   reason_class::recognized_not_yet_supported_step2 at key_path "meter"

TEST(LoadDeferredSurface, T020_DeferredMeter) {
    auto result = load(fixture_path("neg_deferred_toplevel.toml"));

    ASSERT_FALSE(result.has_value())
        << "neg_deferred_toplevel.toml must fail (deferred keys present)";

    const auto& diags = result.error();
    EXPECT_TRUE(has_diag(diags, RC::recognized_not_yet_supported_step2, "meter"))
        << "expected recognized_not_yet_supported_step2 at key_path \"meter\"; "
           "diagnostics:\n"
        << diag_string(diags);

    EXPECT_FALSE(has_diag(diags, RC::unknown_key, "meter"))
        << "meter must NOT produce unknown_key (SC-007)";
}

// ── T020_DeferredMessageArena — message_arena at root scope ──────────────────
//
// A top-level `message_arena` scalar (deferred memory-arena surface;
// data-model E-5 / D-6) must yield:
//   reason_class::recognized_not_yet_supported_step2 at key_path "message_arena"

TEST(LoadDeferredSurface, T020_DeferredMessageArena) {
    auto result = load(fixture_path("neg_deferred_toplevel.toml"));

    ASSERT_FALSE(result.has_value())
        << "neg_deferred_toplevel.toml must fail (deferred keys present)";

    const auto& diags = result.error();
    EXPECT_TRUE(has_diag(diags, RC::recognized_not_yet_supported_step2, "message_arena"))
        << "expected recognized_not_yet_supported_step2 at key_path \"message_arena\"; "
           "diagnostics:\n"
        << diag_string(diags);

    EXPECT_FALSE(has_diag(diags, RC::unknown_key, "message_arena"))
        << "message_arena must NOT produce unknown_key (SC-007)";
}

// ── T020_DeferredDialectOverlay — dialect_overlay at root scope ───────────────
//
// A top-level `dialect_overlay` scalar (deferred; data-model E-5 / D-6) must
// yield:
//   reason_class::recognized_not_yet_supported_step2 at key_path "dialect_overlay"
//
// NOTE: the existing T018_Step2DialectOverlay cell in test_load_negative_battery
// exercises dialect_overlay at SESSION scope ("session[0].dialect_overlay").
// This cell covers the ROOT scope (key_path == "dialect_overlay", no prefix).

TEST(LoadDeferredSurface, T020_DeferredDialectOverlay) {
    auto result = load(fixture_path("neg_deferred_toplevel.toml"));

    ASSERT_FALSE(result.has_value())
        << "neg_deferred_toplevel.toml must fail (deferred keys present)";

    const auto& diags = result.error();
    EXPECT_TRUE(has_diag(diags, RC::recognized_not_yet_supported_step2, "dialect_overlay"))
        << "expected recognized_not_yet_supported_step2 at key_path \"dialect_overlay\"; "
           "diagnostics:\n"
        << diag_string(diags);

    EXPECT_FALSE(has_diag(diags, RC::unknown_key, "dialect_overlay"))
        << "dialect_overlay must NOT produce unknown_key (SC-007)";
}

// ── T020_DeferredTap — [tap] at root scope ───────────────────────────────────
//
// A top-level [tap] block (forced-deferred — 2l stub unshipped; data-model E-5)
// must yield:
//   reason_class::recognized_not_yet_supported_step2 at key_path "tap"

TEST(LoadDeferredSurface, T020_DeferredTap) {
    auto result = load(fixture_path("neg_deferred_toplevel.toml"));

    ASSERT_FALSE(result.has_value())
        << "neg_deferred_toplevel.toml must fail (deferred keys present)";

    const auto& diags = result.error();
    EXPECT_TRUE(has_diag(diags, RC::recognized_not_yet_supported_step2, "tap"))
        << "expected recognized_not_yet_supported_step2 at key_path \"tap\"; "
           "diagnostics:\n"
        << diag_string(diags);

    EXPECT_FALSE(has_diag(diags, RC::unknown_key, "tap"))
        << "tap must NOT produce unknown_key (SC-007)";
}

// ── T020_DeferredSetExactCoverage — every kDeferred key stays deferred ───────
//
// FR-022 (the one-key flip) requires `logger` to become supported while EVERY
// OTHER recognized-but-deferred key — including the spec-named observability
// keys otlp / prometheus / exporter / log_sink — stays under
// recognized_not_yet_supported_step2.  The per-key cells above witness only a
// subset (tracer/meter/message_arena/dialect_overlay/tap); a subset-presence
// check passes even if a key is silently DEMOTED out of kDeferred (it would then
// report unknown_key, which no above cell would catch for the un-listed keys).
//
// This cell pins the WHOLE deferred surface: the fixture carries all 13 kDeferred
// keys, and we assert each one yields the deferred reason AND never unknown_key.
// Dropping any key from kDeferred (toml_config_loader.cpp:268-283) demotes it to
// unknown_key → both assertions fail RED here.  This is the exact-coverage
// discipline of [[feedback_completeness_gate_exact_set_not_subset]] applied at
// the public-API level (the kDeferred set itself is file-scoped in the .cpp; this
// is the strongest demotion-detector reachable without exposing it).
//
// NOTE: this is exact coverage of "every deferred key STAYS deferred", not of the
// set's literal membership — ADDING a new deferred key is not an FR-022 regression
// and is intentionally not flagged here.  If a key is added to kDeferred, append
// it to neg_deferred_toplevel.toml and to kAllDeferredKeys below.

TEST(LoadDeferredSurface, T020_DeferredSetExactCoverage) {
    // The full kDeferred set (toml_config_loader.cpp), minus `logger` (flipped to
    // supported in 045 FR-022).  Mirror of the fixture's keys.
    static constexpr std::string_view kAllDeferredKeys[] = {
        "tracer",   "meter",         "tap",          "otlp",
        "prometheus", "exporter",    "log_sink",     "message_arena",
        "dialect_overlay", "arena",  "session_arena", "framer_carry_arena",
        "tap_consumer",
    };

    auto result = load(fixture_path("neg_deferred_toplevel.toml"));

    ASSERT_FALSE(result.has_value())
        << "neg_deferred_toplevel.toml must fail (deferred keys present)";

    const auto& diags = result.error();
    for (const std::string_view key : kAllDeferredKeys) {
        EXPECT_TRUE(has_diag(diags, RC::recognized_not_yet_supported_step2, key))
            << "deferred key \"" << key
            << "\" must yield recognized_not_yet_supported_step2 — a missing "
               "diagnostic here means it was silently DEMOTED out of kDeferred "
               "(FR-022 regression); diagnostics:\n"
            << diag_string(diags);
        EXPECT_FALSE(has_diag(diags, RC::unknown_key, key))
            << "deferred key \"" << key
            << "\" must NOT produce unknown_key — that is the exact demotion "
               "FR-022 forbids (SC-007); diagnostics:\n"
            << diag_string(diags);
    }
}

// ── T020_TypoDistinction — typo'd key yields unknown_key, NOT deferred ───────
//
// SC-007 / AC US4-2: a key "loggerr" (double-r typo) that is neither in
// kDeferred NOR in kRecognized must yield:
//   reason_class::unknown_key at key_path "loggerr"
// and must NOT yield recognized_not_yet_supported_step2 at "loggerr".
//
// The `loggerr` typo is particularly discriminating: it proves the logger FLIP
// (logger → kRecognized) did NOT accidentally promote lookalike typos into the
// deferred class (which would be a false positive — operators would see a
// "recognized but deferred" message for what is just a typo).

TEST(LoadDeferredSurface, T020_TypoDistinction) {
    auto result = load(fixture_path("neg_deferred_typo.toml"));

    ASSERT_FALSE(result.has_value())
        << "neg_deferred_typo.toml must fail (unknown key 'loggerr')";

    const auto& diags = result.error();

    // Must be classified as an unknown key, not a deferred feature
    EXPECT_TRUE(has_diag(diags, RC::unknown_key, "loggerr"))
        << "typo key 'loggerr' must yield unknown_key at \"loggerr\"; "
           "diagnostics:\n"
        << diag_string(diags);

    // Critical: must NOT be mistakenly classified as a deferred feature
    EXPECT_FALSE(has_diag(diags, RC::recognized_not_yet_supported_step2, "loggerr"))
        << "typo key 'loggerr' must NOT produce recognized_not_yet_supported_step2 "
           "— that reason is reserved for known-deferred features; a typo must be "
           "an unknown_key (SC-007)";
}

// ── T020_LoggerNotDeferred — a [logger] block does NOT produce the deferred ──
//    reason (the one-key flip: logger → kRecognized in T006 / 045 FR-022)
//
// Load a minimal [logger] block with a file sink to a pre-created temp dir.
// Assert:
//   (a) recognized_not_yet_supported_step2 at "logger" is NOT present
//   (b) the load SUCCEEDS (has_value() == true) — logger is now fully supported
//   (c) engine.logger is non-null
//
// The file-sink directory is created in the system temp dir to avoid source-tree
// pollution; it is removed afterwards via RAII.

TEST(LoadDeferredSurface, T020_LoggerNotDeferred) {
    // Pre-create a temp directory for the file sink.
    // The loader's side-effect-free preflight requires the directory to exist.
    const auto tmp_base = std::filesystem::temp_directory_path() / "fixpp_test_045_T020";
    const auto log_dir = tmp_base / "logs";

    {
        std::error_code ec;
        std::filesystem::remove_all(tmp_base, ec);  // clean from any prior run
        std::filesystem::create_directories(log_dir, ec);
        ASSERT_FALSE(ec) << "failed to create temp log dir: " << log_dir
                         << " — " << ec.message();
    }

    // Write a minimal logger TOML into a temp file so we can embed the absolute
    // log_dir path (relative-path fixtures only work relative to the fixture dir,
    // not system temp dirs).
    const auto toml_path = tmp_base / "deferred_logger.toml";
    {
        std::ofstream f(toml_path);
        ASSERT_TRUE(f.is_open()) << "failed to create temp TOML at " << toml_path;
        f << "[clock]\nkind = \"system\"\n\n"
          << "[store]\nkind = \"memory\"\n\n"
          << "[cert_source]\nkind = \"file\"\n"
          << "cert_file = \""
          << (std::filesystem::path{std::string{FIXPP_CONFIG_FIXTURE_DIR}} / "leaf_ecdsa_p256.pem")
                 .string()
          << "\"\n"
          << "key_file = \""
          << (std::filesystem::path{std::string{FIXPP_CONFIG_FIXTURE_DIR}} / "leaf_ecdsa_p256.key")
                 .string()
          << "\"\n"
          << "ca_file = \""
          << (std::filesystem::path{std::string{FIXPP_CONFIG_FIXTURE_DIR}} / "ca.pem").string()
          << "\"\n\n"
          << "[dictionary]\nkind = \"path\"\npath = \""
          << (std::filesystem::path{std::string{FIXPP_CONFIG_FIXTURE_DIR}} / "FIX44.xml").string()
          << "\"\n\n"
          << "[logger]\ncapacity = 4096\ndrain_timeout = \"2000ms\"\n\n"
          << "[[logger.sinks]]\nkind = \"file\"\ndirectory = \"" << log_dir.string()
          << "\"\nbase_name = \"fixpp_t020\"\n\n"
          << "[[session]]\n"
          << "sender_comp_id = \"CLIENT1\"\ntarget_comp_id = \"SERVER1\"\n"
          << "begin_string = \"FIX.4.4\"\nrole = \"initiator\"\n\n"
          << "[session.transport]\nkind = \"tls\"\nhost = \"fix.example.com\"\nport = 4321\n\n"
          << "[session.security_profile]\nkind = \"mtls_ca\"\n";
        f.close();
    }

    auto result = load(toml_path);

    // (a) logger must NOT produce the deferred diagnostic — it is now SUPPORTED
    if (!result.has_value()) {
        EXPECT_FALSE(has_diag(result.error(), RC::recognized_not_yet_supported_step2, "logger"))
            << "logger must NOT produce recognized_not_yet_supported_step2 — "
               "it was moved to kRecognized in T006 / 045 FR-022; "
               "diagnostics:\n"
            << diag_string(result.error());
    }

    // (b) the load must SUCCEED
    ASSERT_TRUE(result.has_value())
        << "a minimal valid [logger] block must load successfully; "
           "diagnostics:\n"
        << (result.has_value() ? "" : diag_string(result.error()));

    // (c) engine.logger must be non-null (the one-key flip is observable)
    ASSERT_NE(result->engine.logger, nullptr)
        << "engine.logger must be non-null after loading a supported [logger] block "
           "(T006 / 045 FR-022 flip from deferred to recognized)";

    // Drain the logger before removing the sink directory (matches established
    // pattern in test_load_logger). On Linux an open inode survives unlink, so
    // this is not strictly required, but it prevents TSan noise from the drain
    // thread racing against remove_all at the coverage gate (T026).
    [[maybe_unused]] auto sd_res = result->engine.logger->shutdown();

    // Cleanup temp dir
    {
        std::error_code ec;
        std::filesystem::remove_all(tmp_base, ec);
    }
}
