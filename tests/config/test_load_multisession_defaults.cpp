// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/config/test_load_multisession_defaults.cpp
// 044-toml-session-config T031 [P] [US4] — multi-session [default]+[[session]] defaults test.
//
// Validates that the [default]-merge (T009) and cross-scope key-recognition
// (FR-018a) behave correctly for multi-session TOML files (SC-005):
//
//   Cell 1 (T031_MultiSessionDefaults):
//     Fixture multisession_defaults.toml — 3 [[session]] blocks with a shared
//     [default] table. Verifies:
//       (a) Inheritance: session A inherits heartbeat_interval=30s unchanged.
//       (b) Override-wins: session B's heartbeat_interval=45s overrides [default]'s 30s.
//       (c) Override-of-absent: session B's reset_on_logout=true is applied only to B;
//           sessions A and C retain the struct default (false).
//       (d) Shared scalars (reset_on_logon, check_comp_id) inherited identically across
//           sessions A and C; verified against [default]'s values.
//
//   Cell 2 (T031_DefaultTypoCrossScope):
//     Fixture neg_default_typo.toml — a [default] block containing a TYPO'd key
//     ("heartbeat_intervall"). Verifies the loader emits unknown_key at
//     key_path="default.heartbeat_intervall", proving recognition over [default].
//
// Anti-hang: load_toml_config is synchronous (cold). We create an io_context
//   to satisfy LoadOptions but NEVER call ctx.run() — get_executor() is all that
//   is needed.
//
// Anchors: spec.md D-8 ([default]-merge), plan.md SC-005, FR-018/FR-018a.

#include <fixpp/config/toml_config_loader.hpp>
#include <fixpp/config/config_bundle.hpp>
#include <fixpp/config/load_diagnostic.hpp>
#include <fixpp/session/session_config.hpp>

#include <asio/io_context.hpp>

#include <algorithm>
#include <chrono>
#include <string>

#include <gtest/gtest.h>

#ifndef FIXPP_CONFIG_FIXTURE_DIR
#error "FIXPP_CONFIG_FIXTURE_DIR must be set by CMake"
#endif

namespace {

// ── Load helper ───────────────────────────────────────────────────────────────
// Returns a LoadResult (std::expected<ConfigBundle, vector<LoadDiagnostic>>).
// The io_context is created per-call; NEVER call ctx.run() (anti-hang rule).

fixpp::config::LoadResult load(std::string_view fixture_name)
{
    const std::filesystem::path p =
        std::filesystem::path{std::string{FIXPP_CONFIG_FIXTURE_DIR}} / fixture_name;
    asio::io_context ctx;
    fixpp::config::LoadOptions opts;
    opts.engine_executor = ctx.get_executor();
    return fixpp::config::load_toml_config(p, opts);
}

// ── Diagnostic search helper ─────────────────────────────────────────────────
// Returns true iff diags contains an entry with BOTH the expected reason AND
// key_path (both must match — not a bypassable guard).

bool has_diag(const std::vector<fixpp::config::LoadDiagnostic>& diags,
              fixpp::config::reason_class                        expected_reason,
              std::string_view                                   expected_key_path)
{
    return std::any_of(diags.begin(), diags.end(),
        [&](const fixpp::config::LoadDiagnostic& d) {
            return d.reason == expected_reason
                && d.key_path == expected_key_path;
        });
}

}  // namespace

// =============================================================================
// T031 Cell 1 — multi-session [default] inheritance + override
// =============================================================================
//
// multisession_defaults.toml has 3 [[session]] blocks (A, B, C) plus a [default]
// table with heartbeat_interval=30s, reset_on_logon=true, check_comp_id=false.
// Session B overrides heartbeat_interval=45s and adds reset_on_logout=true (absent
// from [default]). Sessions A and C do not override any scalar.
//
// Discriminating assertions:
//   (a) Inherited: sessions[0].heartbeat_interval == 30s  (A inherits [default])
//   (b) Override-wins: sessions[1].heartbeat_interval == 45s  (B overrides)
//       AND sessions[1].heartbeat_interval != 30s  (default did NOT win)
//   (c) Override-of-absent (positive): sessions[1].reset_on_logout == true
//   (c') Override-of-absent (negative — not-leaked): sessions[0].reset_on_logout == false
//       AND sessions[2].reset_on_logout == false  (B's per-session key does not bleed)
//   (d) Shared scalars: sessions[0].reset_on_logon == true, check_comp_id == false
//       (values match [default], NOT the SessionConfig defaults false/true)

TEST(LoadMultisessionDefaults, T031_MultiSessionDefaults)
{
    auto result = load("multisession_defaults.toml");

    // Load must succeed — a failure here is a fixture/impl bug, not expected.
    ASSERT_TRUE(result.has_value())
        << "load_toml_config failed; diagnostics: "
        << [&]{
            std::string s;
            if (!result.has_value())
                for (const auto& d : result.error())
                    s += d.key_path + ": " + d.message + "\n";
            return s;
        }();

    // (d) Exactly 3 sessions.
    ASSERT_EQ(result->sessions.size(), std::size_t{3})
        << "expected 3 sessions from multisession_defaults.toml";

    const fixpp::session::SessionConfig& a = result->sessions[0].config;
    const fixpp::session::SessionConfig& b = result->sessions[1].config;
    const fixpp::session::SessionConfig& c = result->sessions[2].config;

    // ── Identity — each session has its own comp IDs ──────────────────────────
    EXPECT_EQ(a.sender_comp_id, "CLIENT_A");
    EXPECT_EQ(b.sender_comp_id, "CLIENT_B");
    EXPECT_EQ(c.sender_comp_id, "CLIENT_C");

    // ── (a) Inheritance: session A inherits [default]'s heartbeat_interval=30s ─
    ASSERT_TRUE(a.heartbeat_interval.has_value())
        << "session[0] must inherit heartbeat_interval from [default]";
    EXPECT_EQ(*a.heartbeat_interval, std::chrono::seconds{30})
        << "session[0] must inherit 30s from [default], not any other value";

    // ── (b) Override-wins: session B uses 45s, NOT the default 30s ───────────
    ASSERT_TRUE(b.heartbeat_interval.has_value())
        << "session[1] must have heartbeat_interval (either inherited or overridden)";
    EXPECT_EQ(*b.heartbeat_interval, std::chrono::seconds{45})
        << "session[1] must use its own 45s override, not [default]'s 30s";
    // Explicitly confirm it is NOT the default value (discriminates against
    // an impl that ignores per-session overrides and always uses [default]).
    EXPECT_NE(*b.heartbeat_interval, std::chrono::seconds{30})
        << "session[1]'s override must win over [default] (30s must NOT appear)";

    // ── (c) Override-of-absent (positive): B's reset_on_logout is applied ─────
    EXPECT_TRUE(b.reset_on_logout)
        << "session[1].reset_on_logout=true must be applied (override-of-absent)";

    // ── (c') Not-leaked: A and C retain the struct default (false) ────────────
    EXPECT_FALSE(a.reset_on_logout)
        << "session[0].reset_on_logout must be false — B's per-session key must "
           "not bleed into session A";
    EXPECT_FALSE(c.reset_on_logout)
        << "session[2].reset_on_logout must be false — B's per-session key must "
           "not bleed into session C";

    // ── (d) Shared scalars inherited identically by A and C ──────────────────
    // reset_on_logon=true in [default] (non-default vs struct default false).
    EXPECT_TRUE(a.reset_on_logon)
        << "session[0].reset_on_logon must inherit true from [default]";
    EXPECT_TRUE(c.reset_on_logon)
        << "session[2].reset_on_logon must inherit true from [default]";

    // check_comp_id=false in [default] (inverted from struct default true).
    EXPECT_FALSE(a.check_comp_id)
        << "session[0].check_comp_id must inherit false from [default]";
    EXPECT_FALSE(c.check_comp_id)
        << "session[2].check_comp_id must inherit false from [default]";

    // Session B also inherits the shared scalars (it only overrides heartbeat+logout).
    EXPECT_TRUE(b.reset_on_logon)
        << "session[1].reset_on_logon must inherit true from [default]";
    EXPECT_FALSE(b.check_comp_id)
        << "session[1].check_comp_id must inherit false from [default]";

    // ── Reconnect endpoints: each session has its own host/port ──────────────
    EXPECT_EQ(a.reconnect_endpoint.host, "fix-a.example.com");
    EXPECT_EQ(a.reconnect_endpoint.port, std::uint16_t{4001});
    EXPECT_EQ(b.reconnect_endpoint.host, "fix-b.example.com");
    EXPECT_EQ(b.reconnect_endpoint.port, std::uint16_t{4002});
    EXPECT_EQ(c.reconnect_endpoint.host, "fix-c.example.com");
    EXPECT_EQ(c.reconnect_endpoint.port, std::uint16_t{4003});
}

// =============================================================================
// T031 Cell 2 — cross-scope key-recognition: typo in [default] is caught
// =============================================================================
//
// neg_default_typo.toml has [default] containing "heartbeat_intervall" (double-l).
// The loader must emit unknown_key at key_path="default.heartbeat_intervall",
// proving that recognize_keys runs on the [default] table with prefix "default"
// (FR-018a / E-6).
//
// Without this guard, a [default]-only typo would be silently inherited by every
// session with no diagnostic — masking operator errors.

TEST(LoadMultisessionDefaults, T031_DefaultTypoCrossScope)
{
    auto result = load("neg_default_typo.toml");

    // Load must FAIL — a typo in [default] must be rejected.
    ASSERT_FALSE(result.has_value())
        << "expected load failure for a typo key in [default]; "
           "cross-scope key recognition must flag unknown keys under [default]";

    const auto& diags = result.error();
    ASSERT_FALSE(diags.empty())
        << "expected at least one diagnostic for the typo in [default]";

    using RC = fixpp::config::reason_class;

    // The diagnostic must have BOTH reason=unknown_key AND the exact key_path
    // (both must match; a reason-only check would be a bypassable guard per
    // the anti-pattern library).
    const bool found = has_diag(diags, RC::unknown_key, "default.heartbeat_intervall");
    EXPECT_TRUE(found)
        << "expected unknown_key diagnostic at key_path='default.heartbeat_intervall'; "
           "got " << diags.size() << " diagnostic(s):\n"
        << [&]{
            std::string s;
            for (const auto& d : diags)
                s += "  key_path='" + d.key_path + "' message='" + d.message + "'\n";
            return s;
        }();
}
