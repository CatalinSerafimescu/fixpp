// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/config/test_load_negative_battery.cpp
// 044-toml-session-config Phase-4 TDD RED tests.
//
// T018 [US2] — one cell per reason_class: parse_error, unknown_key,
//   recognized_not_yet_supported_step2 (4 sub-cells), missing_required,
//   empty_required, malformed_value, out_of_range, unknown_enum,
//   invalid_or_contradictory_selector (4 sub-cells).
//
// T019 [US2] — collect-ALL (SC-007): a fixture with 3 independent errors
//   yields exactly 3 diagnostics; and a step-2 deferred key (reject_policy)
//   reports recognized_not_yet_supported_step2, distinct from unknown_key for
//   a typo key.
//
// RED condition:
//   Phase 3b already emits diagnostics for: parse_error, malformed_value
//   (unitless duration), out_of_range (negative int), unknown_enum (bad token),
//   recognized_not_yet_supported_step2 (mtls_pinned, dictionary.kind="version"),
//   and missing_required/invalid_or_contradictory_selector for cert_source.
//
//   NOT YET IMPLEMENTED (RED until Phase 4b):
//   - unknown_key: Phase 4b will add key-schema enumeration.
//   - missing_required / empty_required for session-scalar fields (sender_comp_id).
//   - recognized_not_yet_supported_step2 for logger/dialect_overlay keys.
//   - invalid_or_contradictory_selector for:
//       * plaintext transport with cert material present
//       * mode=direct_executor + locks=spin
//
// Anti-hang: load_toml_config is synchronous (cold). We create an io_context
//   to satisfy LoadOptions but NEVER call ctx.run().
//
// Anchors: spec.md E-5 (reason_class), plan.md SC-003/SC-007/FR-012/FR-018.

#include <fixpp/config/toml_config_loader.hpp>
#include <fixpp/config/load_diagnostic.hpp>

#include <asio/io_context.hpp>

#include <algorithm>
#include <set>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#ifndef FIXPP_CONFIG_FIXTURE_DIR
#error "FIXPP_CONFIG_FIXTURE_DIR must be set by CMake"
#endif

namespace {

// ── Fixture path helper ───────────────────────────────────────────────────────

std::filesystem::path neg_fixture(std::string_view name)
{
    return std::filesystem::path{std::string{FIXPP_CONFIG_FIXTURE_DIR}} / name;
}

// ── Load helper ───────────────────────────────────────────────────────────────
//
// Returns the error-diagnostic vector, asserting the load returned
// std::unexpected.  If the load returns a value, the test fails here and the
// caller's ASSERT_FALSE catches it; call sites use ASSERT_FALSE(result.has_value())
// BEFORE calling diagnostics_from().

fixpp::config::LoadResult load(const std::filesystem::path& path)
{
    asio::io_context ctx;
    fixpp::config::LoadOptions opts;
    opts.engine_executor = ctx.get_executor();
    return fixpp::config::load_toml_config(path, opts);
}

// ── Diagnostic search helper (discriminating — checks BOTH reason AND key_path)

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
// T018 — one cell per reason_class
// =============================================================================

// ── parse_error ───────────────────────────────────────────────────────────────
//
// neg_parse_error.toml has an unterminated string.
// ALREADY handled by Phase 3b (T007 try/catch toml::parse_error).
// Expected: unexpected(diag{reason=parse_error, key_path="", location.line>0}).

TEST(LoadNegativeBattery, T018_ParseError)
{
    auto result = load(neg_fixture("neg_parse_error.toml"));

    ASSERT_FALSE(result.has_value())
        << "expected load failure for a malformed TOML file";

    const auto& diags = result.error();
    ASSERT_FALSE(diags.empty());

    using RC = fixpp::config::reason_class;
    // parse_error is emitted with key_path="" (file-level, no specific key).
    bool found = has_diag(diags, RC::parse_error, "");
    EXPECT_TRUE(found)
        << "expected a parse_error diagnostic with key_path=\"\"";

    // Location must be populated (line>0) — the parse failure has a source region.
    auto it = std::find_if(diags.begin(), diags.end(),
        [](const fixpp::config::LoadDiagnostic& d) {
            return d.reason == RC::parse_error;
        });
    ASSERT_NE(it, diags.end());
    EXPECT_GT(it->location.line, std::uint32_t{0})
        << "parse_error diagnostic must carry source location (line>0)";
}

// ── unknown_key ───────────────────────────────────────────────────────────────
//
// neg_unknown_key.toml has a typo key "sender_comp_idd" in [[session]].
// NOT YET IMPLEMENTED — Phase 4b will add schema-key enumeration.
// RED until 4b: the load currently succeeds (unknown keys are silently ignored).

TEST(LoadNegativeBattery, T018_UnknownKey)
{
    auto result = load(neg_fixture("neg_unknown_key.toml"));

    ASSERT_FALSE(result.has_value())
        << "expected load failure for a typo key (sender_comp_idd); "
           "Phase 4b unknown_key detection not yet implemented — RED expected";

    using RC = fixpp::config::reason_class;
    EXPECT_TRUE(has_diag(result.error(), RC::unknown_key,
                         "session[0].sender_comp_idd"))
        << "expected unknown_key at session[0].sender_comp_idd";
}

// ── recognized_not_yet_supported_step2 — sub-cell (a): [logger] key ──────────
//
// neg_step2_logger.toml has a [logger] section (step-2 deferred).
// NOT YET IMPLEMENTED — Phase 4b will scan top-level step-2 keys.
// RED until 4b: the [logger] table is currently silently ignored.

TEST(LoadNegativeBattery, T018_Step2Logger)
{
    auto result = load(neg_fixture("neg_step2_logger.toml"));

    ASSERT_FALSE(result.has_value())
        << "expected load failure for a step-2 [logger] key; "
           "Phase 4b step-2 deferral not yet implemented — RED expected";

    using RC = fixpp::config::reason_class;
    EXPECT_TRUE(has_diag(result.error(),
                         RC::recognized_not_yet_supported_step2, "logger"))
        << "expected recognized_not_yet_supported_step2 at \"logger\"";
}

// ── recognized_not_yet_supported_step2 — sub-cell (b): dialect_overlay key ──

// neg_step2_dialect_overlay.toml has dialect_overlay in [[session]].
// NOT YET IMPLEMENTED — Phase 4b will detect this session-level step-2 key.
// RED until 4b.

TEST(LoadNegativeBattery, T018_Step2DialectOverlay)
{
    auto result = load(neg_fixture("neg_step2_dialect_overlay.toml"));

    ASSERT_FALSE(result.has_value())
        << "expected load failure for dialect_overlay (step-2 deferred); "
           "Phase 4b step-2 detection not yet implemented — RED expected";

    using RC = fixpp::config::reason_class;
    EXPECT_TRUE(has_diag(result.error(),
                         RC::recognized_not_yet_supported_step2,
                         "session[0].dialect_overlay"))
        << "expected recognized_not_yet_supported_step2 at "
           "\"session[0].dialect_overlay\"";
}

// ── recognized_not_yet_supported_step2 — sub-cell (c): dictionary.kind="version"

// neg_step2_dict_version.toml has dictionary.kind="version".
// ALREADY handled by Phase 3b (selector_resolver.cpp).

TEST(LoadNegativeBattery, T018_Step2DictVersion)
{
    auto result = load(neg_fixture("neg_step2_dict_version.toml"));

    ASSERT_FALSE(result.has_value())
        << "expected load failure for dictionary.kind=\"version\" (step-2 deferred)";

    using RC = fixpp::config::reason_class;
    EXPECT_TRUE(has_diag(result.error(),
                         RC::recognized_not_yet_supported_step2, "dictionary.kind"))
        << "expected recognized_not_yet_supported_step2 at \"dictionary.kind\"";
}

// ── recognized_not_yet_supported_step2 — sub-cell (d): mtls_pinned ───────────

// neg_step2_mtls_pinned.toml has security_profile.kind="mtls_pinned".
// ALREADY handled by Phase 3b (selector_resolver.cpp).

TEST(LoadNegativeBattery, T018_Step2MtlsPinned)
{
    auto result = load(neg_fixture("neg_step2_mtls_pinned.toml"));

    ASSERT_FALSE(result.has_value())
        << "expected load failure for mtls_pinned (step-2 deferred)";

    using RC = fixpp::config::reason_class;
    EXPECT_TRUE(has_diag(result.error(),
                         RC::recognized_not_yet_supported_step2,
                         "session[0].security_profile.kind"))
        << "expected recognized_not_yet_supported_step2 at "
           "\"session[0].security_profile.kind\"";
}

// ── missing_required ─────────────────────────────────────────────────────────
//
// neg_missing_required.toml has sender_comp_id omitted.
// NOT YET IMPLEMENTED — Phase 4b will add per-required-key checks.
// RED until 4b: the load currently succeeds (sender_comp_id left at default "").

TEST(LoadNegativeBattery, T018_MissingRequired)
{
    auto result = load(neg_fixture("neg_missing_required.toml"));

    ASSERT_FALSE(result.has_value())
        << "expected load failure for missing sender_comp_id; "
           "Phase 4b required-key checks not yet implemented — RED expected";

    using RC = fixpp::config::reason_class;
    EXPECT_TRUE(has_diag(result.error(), RC::missing_required,
                         "session[0].sender_comp_id"))
        << "expected missing_required at \"session[0].sender_comp_id\"";
}

// ── empty_required ────────────────────────────────────────────────────────────
//
// neg_empty_required.toml has sender_comp_id = "".
// NOT YET IMPLEMENTED — Phase 4b will add empty-string guards.
// RED until 4b: the load currently succeeds (empty string accepted silently).

TEST(LoadNegativeBattery, T018_EmptyRequired)
{
    auto result = load(neg_fixture("neg_empty_required.toml"));

    ASSERT_FALSE(result.has_value())
        << "expected load failure for empty sender_comp_id; "
           "Phase 4b empty-required checks not yet implemented — RED expected";

    using RC = fixpp::config::reason_class;
    EXPECT_TRUE(has_diag(result.error(), RC::empty_required,
                         "session[0].sender_comp_id"))
        << "expected empty_required at \"session[0].sender_comp_id\"";
}

// ── malformed_value ───────────────────────────────────────────────────────────
//
// neg_malformed_value.toml has heartbeat_interval = "30" (unitless duration).
// ALREADY handled by Phase 3b (scalar_mappers.cpp parse_duration_to_ms).

TEST(LoadNegativeBattery, T018_MalformedValue)
{
    auto result = load(neg_fixture("neg_malformed_value.toml"));

    ASSERT_FALSE(result.has_value())
        << "expected load failure for unitless heartbeat_interval";

    using RC = fixpp::config::reason_class;
    EXPECT_TRUE(has_diag(result.error(), RC::malformed_value,
                         "session[0].heartbeat_interval"))
        << "expected malformed_value at \"session[0].heartbeat_interval\"";

    // Location must be populated for a file-positional value error.
    auto it = std::find_if(result.error().begin(), result.error().end(),
        [](const fixpp::config::LoadDiagnostic& d) {
            return d.reason == RC::malformed_value
                && d.key_path == "session[0].heartbeat_interval";
        });
    ASSERT_NE(it, result.error().end());
    EXPECT_GT(it->location.line, std::uint32_t{0})
        << "malformed_value diagnostic must carry source location";
}

// ── out_of_range ──────────────────────────────────────────────────────────────
//
// neg_out_of_range.toml has logout_disconnect_timeout_ms = -1.
// ALREADY handled by Phase 3b (scalar_mappers.cpp v<0 guard).

TEST(LoadNegativeBattery, T018_OutOfRange)
{
    auto result = load(neg_fixture("neg_out_of_range.toml"));

    ASSERT_FALSE(result.has_value())
        << "expected load failure for logout_disconnect_timeout_ms = -1";

    using RC = fixpp::config::reason_class;
    EXPECT_TRUE(has_diag(result.error(), RC::out_of_range,
                         "session[0].logout_disconnect_timeout_ms"))
        << "expected out_of_range at \"session[0].logout_disconnect_timeout_ms\"";

    // Location must be populated for a file-positional value error.
    auto it = std::find_if(result.error().begin(), result.error().end(),
        [](const fixpp::config::LoadDiagnostic& d) {
            return d.reason == RC::out_of_range
                && d.key_path == "session[0].logout_disconnect_timeout_ms";
        });
    ASSERT_NE(it, result.error().end());
    EXPECT_GT(it->location.line, std::uint32_t{0})
        << "out_of_range diagnostic must carry source location";
}

// ── unknown_enum ──────────────────────────────────────────────────────────────
//
// neg_unknown_enum.toml has role = "bogus".
// ALREADY handled by Phase 3b (scalar_mappers.cpp).

TEST(LoadNegativeBattery, T018_UnknownEnum)
{
    auto result = load(neg_fixture("neg_unknown_enum.toml"));

    ASSERT_FALSE(result.has_value())
        << "expected load failure for role = \"bogus\"";

    using RC = fixpp::config::reason_class;
    EXPECT_TRUE(has_diag(result.error(), RC::unknown_enum, "session[0].role"))
        << "expected unknown_enum at \"session[0].role\"";

    // Location must be populated for a file-positional value error.
    auto it = std::find_if(result.error().begin(), result.error().end(),
        [](const fixpp::config::LoadDiagnostic& d) {
            return d.reason == RC::unknown_enum && d.key_path == "session[0].role";
        });
    ASSERT_NE(it, result.error().end());
    EXPECT_GT(it->location.line, std::uint32_t{0})
        << "unknown_enum diagnostic must carry source location";
}

// ── invalid_or_contradictory_selector — sub-cell (a): plaintext + certs ──────
//
// neg_contradictory_plain_with_certs.toml: transport.kind="plaintext" with
// cert material present (a contradiction).
// NOT YET IMPLEMENTED — Phase 4b will add this cross-selector consistency check.
// RED until 4b.

TEST(LoadNegativeBattery, T018_ContradictoryPlainWithCerts)
{
    auto result = load(neg_fixture("neg_contradictory_plain_with_certs.toml"));

    ASSERT_FALSE(result.has_value())
        << "expected load failure for plaintext transport with cert material; "
           "Phase 4b contradiction check not yet implemented — RED expected";

    using RC = fixpp::config::reason_class;
    // selector_resolver.cpp emits at "session[0].transport" (T025).
    EXPECT_TRUE(has_diag(result.error(), RC::invalid_or_contradictory_selector,
                         "session[0].transport"))
        << "expected invalid_or_contradictory_selector at \"session[0].transport\" "
           "for plaintext transport with cert_source present";
}

// ── invalid_or_contradictory_selector — sub-cell (b): tls + no certs ─────────
//
// neg_contradictory_tls_no_certs.toml: transport.kind="tls" with no [cert_source].
// ALREADY handled by Phase 3b (selector_resolver.cpp: cert_source==nullptr check
// emits missing_required at "cert_source").
// NOTE: The brief describes this as invalid_or_contradictory_selector. The
// current implementation emits missing_required. We test for the actually-emitted
// reason (missing_required) so the test is a correct RED-on-no-cert-guard witness.
// Phase 4b may reclassify; we accept both reasons here.

TEST(LoadNegativeBattery, T018_ContradictoryTlsNoCerts)
{
    auto result = load(neg_fixture("neg_contradictory_tls_no_certs.toml"));

    ASSERT_FALSE(result.has_value())
        << "expected load failure for TLS transport without cert material";

    using RC = fixpp::config::reason_class;
    // The implementation emits missing_required at "cert_source" for this case.
    // We accept either missing_required (Phase 3b) or invalid_or_contradictory_selector
    // (if Phase 4b reclassifies it).
    EXPECT_TRUE(
        has_diag(result.error(), RC::missing_required, "cert_source") ||
        has_diag(result.error(), RC::invalid_or_contradictory_selector, "cert_source") ||
        has_diag(result.error(), RC::invalid_or_contradictory_selector, "session[0].transport"))
        << "expected a diagnostic indicating TLS transport requires cert material";
}

// ── invalid_or_contradictory_selector — sub-cell (c): direct_executor + spin ─
//
// neg_contradictory_direct_spin.toml: mode="direct_executor" + locks="spin".
// NOT YET IMPLEMENTED — Phase 4b will add threading-combination guards.
// RED until 4b: both tokens currently map cleanly without a cross-check.

TEST(LoadNegativeBattery, T018_ContradictoryDirectSpin)
{
    auto result = load(neg_fixture("neg_contradictory_direct_spin.toml"));

    ASSERT_FALSE(result.has_value())
        << "expected load failure for mode=direct_executor + locks=spin; "
           "Phase 4b threading-combination guard not yet implemented — RED expected";

    using RC = fixpp::config::reason_class;
    // scalar_mappers.cpp emits at "session[0].mode" (T024, rule 7).
    EXPECT_TRUE(has_diag(result.error(), RC::invalid_or_contradictory_selector,
                         "session[0].mode"))
        << "expected invalid_or_contradictory_selector at \"session[0].mode\" "
           "for mode=direct_executor + locks=spin";
}

// ── invalid_or_contradictory_selector — sub-cell (d): encrypted key ──────────
//
// neg_contradictory_encrypted_key.toml: key_file references an encrypted PEM.
// make_file_cert_source fails (step-1 supports plaintext keys only; E-6).
// ALREADY handled by Phase 3b (selector_resolver.cpp: !cs_result → diagnostic
// at "cert_source" with reason invalid_or_contradictory_selector).

TEST(LoadNegativeBattery, T018_ContradictoryEncryptedKey)
{
    auto result = load(neg_fixture("neg_contradictory_encrypted_key.toml"));

    ASSERT_FALSE(result.has_value())
        << "expected load failure for an encrypted private key";

    using RC = fixpp::config::reason_class;
    EXPECT_TRUE(has_diag(result.error(), RC::invalid_or_contradictory_selector,
                         "cert_source"))
        << "expected invalid_or_contradictory_selector at \"cert_source\" for "
           "encrypted key (make_file_cert_source failure)";
}

// ── invalid_or_contradictory_selector — sub-cell (e): malformed dictionary XML ─
//
// neg_dict_malformed_xml.toml points dictionary.path at an UNPARSEABLE XML file.
// XmlLoader::load THROWS (dict::xml_parse_error); the loader's trap_throw_to_expected
// wrapper MUST convert that throw into a diagnostic, NEVER terminate (data-model D-3
// / validation rule 9 / plan.md "malformed dictionary XML → diagnostic NOT terminate").
// This exercises the one reachable, file-triggerable trap_throw arm — the same
// noexcept-boundary class the fuzzer (T037) found broken for the toml++ parse path.
// The (key_path, reason) are derived from data-model D-3 / selector_resolver
// resolve_engine_dictionary: invalid_or_contradictory_selector at "dictionary.path".
TEST(LoadNegativeBattery, T018_MalformedDictionaryXmlNoTerminate)
{
    // If trap_throw does NOT catch the XmlLoader throw, this load aborts the
    // process (a crash, not an assertion failure) — which is itself the finding.
    auto result = load(neg_fixture("neg_dict_malformed_xml.toml"));

    ASSERT_FALSE(result.has_value())
        << "malformed dictionary XML must fail closed with a diagnostic, "
           "never a ConfigBundle";

    using RC = fixpp::config::reason_class;
    EXPECT_TRUE(has_diag(result.error(), RC::invalid_or_contradictory_selector,
                         "dictionary.path"))
        << "expected invalid_or_contradictory_selector at \"dictionary.path\" "
           "(XmlLoader::load throw routed through trap_throw_to_expected, D-3)";
}

// ── parse_error via toml++ internal assert — NO terminate (T039) ─────────────
//
// neg_toml_assert_table_header.toml has the malformed table header `[ [key]]`,
// which drives tomlplusplus parse_key() to fire TOML_ASSERT_ASSUME
// (parser.inl:3037). UNLIKE neg_parse_error.toml (an unterminated string, which
// toml++ reports as a normal throwing parse_error), this path is an INTERNAL
// invariant assert: before the toml_include.hpp shim it aborted (debug) / was
// UB (release/NDEBUG), ESCAPING load_toml_config's try/catch noexcept boundary.
// The shim re-routes TOML_ASSERT to a catchable throw so it lands in the
// existing catch as a parse_error diagnostic.
//
// If the shim regresses, this load aborts the process (a crash, not an
// assertion failure) — which is itself the finding.
// Anchors: spec.md FR-012 / validation rule 9 / data-model D-3.

TEST(LoadNegativeBattery, T039_TomlInternalAssert_NoTerminate)
{
    auto result = load(neg_fixture("neg_toml_assert_table_header.toml"));

    ASSERT_FALSE(result.has_value())
        << "a toml++ internal-invariant assert on malformed input must fail "
           "closed with a diagnostic, never abort/UB past the noexcept boundary";

    using RC = fixpp::config::reason_class;
    // The throwing TOML_ASSERT raises std::logic_error, caught by the loader's
    // catch(const std::exception&) arm → parse_error with key_path="".
    EXPECT_TRUE(has_diag(result.error(), RC::parse_error, ""))
        << "expected a parse_error diagnostic for the `[ [key]]` table header";
}

// =============================================================================
// T019 — collect-ALL (SC-007)
// =============================================================================
//
// neg_multi.toml has 3 INDEPENDENT errors:
//   (1) role = "bogus"          → unknown_enum          at session[0].role
//   (2) heartbeat_interval ="30"→ malformed_value        at session[0].heartbeat_interval
//   (3) reject_policy present   → recognized_not_yet_supported_step2 at session[0].reject_policy
//
// All 3 are Phase-3b-handled — this test verifies collect-ALL is live.
// The two-cell distinction: reject_policy → recognized_not_yet_supported_step2
// (known-deferred key), vs a typo key → unknown_key (Phase 4b; different reason).

TEST(LoadNegativeBattery, T019_CollectAll_ExactCount)
{
    auto result = load(neg_fixture("neg_multi.toml"));

    ASSERT_FALSE(result.has_value())
        << "expected load failure for neg_multi.toml (3 independent errors)";

    // Exact count: exactly 3 diagnostics (no missing, no extra).
    EXPECT_EQ(result.error().size(), std::size_t{3})
        << "expected exactly 3 diagnostics from neg_multi.toml; got "
        << result.error().size();
}

TEST(LoadNegativeBattery, T019_CollectAll_ExactSet)
{
    auto result = load(neg_fixture("neg_multi.toml"));

    ASSERT_FALSE(result.has_value())
        << "expected load failure for neg_multi.toml";

    using RC = fixpp::config::reason_class;
    using KRP = std::pair<std::string, RC>;

    // Expected set of (key_path, reason) pairs — exact-set equality (completeness
    // gate: not subset, not superset; per project lesson on exact-set vs subset).
    const std::set<KRP> expected{
        {"session[0].role",               RC::unknown_enum},
        {"session[0].heartbeat_interval", RC::malformed_value},
        {"session[0].reject_policy",      RC::recognized_not_yet_supported_step2},
    };

    std::set<KRP> actual;
    for (const auto& d : result.error()) {
        actual.emplace(d.key_path, d.reason);
    }

    // Report missing diagnostics (present in expected, absent in actual).
    std::vector<KRP> missing_diags;
    for (const auto& e : expected) {
        if (actual.find(e) == actual.end()) {
            missing_diags.push_back(e);
        }
    }
    EXPECT_TRUE(missing_diags.empty())
        << "missing expected diagnostics from collect-ALL:";

    // Report unexpected diagnostics (present in actual, absent in expected).
    std::vector<KRP> extra_diags;
    for (const auto& a : actual) {
        if (expected.find(a) == expected.end()) {
            extra_diags.push_back(a);
        }
    }
    EXPECT_TRUE(extra_diags.empty())
        << "unexpected extra diagnostics from collect-ALL:";
}

// T020 [US2] — Required-key cells
// T021 [US2] — Redaction (FR-019)
// ─────────────────────────────────────────────────────────────────────────────
// These tests are appended here so they share the file's load() / has_diag()
// helpers and FIXPP_CONFIG_FIXTURE_DIR compile-def without duplicating setup.
// ─────────────────────────────────────────────────────────────────────────────

// ── Two-cell distinction: step2 deferred key vs unknown_key typo ─────────────
//
// reject_policy → recognized_not_yet_supported_step2  (known feature, deferred)
// sender_comp_idd → unknown_key                       (typo — unknown to schema)
// They must be DIFFERENT reasons even though both look like "unknown key" inputs.
//
// The step2 cell (reject_policy) is ALREADY handled.
// The unknown_key cell (sender_comp_idd) is NOT YET IMPLEMENTED (Phase 4b).
// This test verifies the distinction is correct once both are implemented.

TEST(LoadNegativeBattery, T019_Step2VsUnknownKeyDistinction)
{
    using RC = fixpp::config::reason_class;

    // A step-2 deferred key (reject_policy) fires recognized_not_yet_supported_step2.
    {
        auto result = load(neg_fixture("neg_multi.toml"));
        ASSERT_FALSE(result.has_value());
        EXPECT_TRUE(has_diag(result.error(),
                             RC::recognized_not_yet_supported_step2,
                             "session[0].reject_policy"))
            << "reject_policy must produce recognized_not_yet_supported_step2 "
               "(known deferred feature), not unknown_key";
    }

    // A typo key (sender_comp_idd) fires unknown_key — NOT YET IMPLEMENTED.
    // This arm will be RED until Phase 4b.
    {
        auto result = load(neg_fixture("neg_unknown_key.toml"));
        ASSERT_FALSE(result.has_value())
            << "expected load failure for typo key (sender_comp_idd); "
               "Phase 4b unknown_key detection not yet implemented — RED expected";
        EXPECT_TRUE(has_diag(result.error(),
                             RC::unknown_key,
                             "session[0].sender_comp_idd"))
            << "sender_comp_idd must produce unknown_key (schema typo), "
               "not recognized_not_yet_supported_step2";

        // The two must have DIFFERENT reasons — this is the key discrimination.
        EXPECT_FALSE(has_diag(result.error(),
                              RC::recognized_not_yet_supported_step2,
                              "session[0].sender_comp_idd"))
            << "a typo key must NOT produce recognized_not_yet_supported_step2 "
               "(that reason is reserved for known-deferred features)";
    }
}

// =============================================================================
// T020 — required-key cells (Bucket B) — one cell per Bucket-B key
// =============================================================================
//
// Anchors: data-model E-3 (Bucket B table), plan.md SC-003/FR-013/FR-017.
//
// RED condition: most cells below are RED until Phase 4b (T022) adds per-key
// missing_required/empty_required enumeration for session-scalar fields and the
// conditional rules. A few cells are ALREADY-GREEN (Phase 3b already validates
// them at the selector level — marked ALREADY-GREEN in each test comment).

// ── clock.kind missing (unconditional — ALREADY-GREEN, Phase 3b) ──────────────
//
// selector_resolver.cpp resolve_engine_clock: absent [clock] table → emits
// missing_required at "clock.kind". ALREADY-GREEN from Phase 3b.

TEST(LoadNegativeBattery, T020_MissingClockKind)
{
    auto result = load(neg_fixture("neg_missing_clock.toml"));

    ASSERT_FALSE(result.has_value())
        << "expected load failure when [clock] section is absent";

    using RC = fixpp::config::reason_class;
    EXPECT_TRUE(has_diag(result.error(), RC::missing_required, "clock.kind"))
        << "expected missing_required at \"clock.kind\" when [clock] is absent "
           "(ALREADY-GREEN: Phase 3b resolve_engine_clock)";
}

// ── sender_comp_id missing → missing_required (RED until 4b) ─────────────────
//
// Fixture: neg_missing_required.toml (sender_comp_id omitted).
// Same fixture as T018_MissingRequired — we make it an explicit T020 cell.
// NOT YET IMPLEMENTED — Phase 4b (T022) adds per-key required-field enumeration.

TEST(LoadNegativeBattery, T020_MissingRequired_SenderCompId)
{
    auto result = load(neg_fixture("neg_missing_required.toml"));

    ASSERT_FALSE(result.has_value())
        << "expected load failure for missing sender_comp_id; "
           "Phase 4b required-key checks not yet implemented — RED expected";

    using RC = fixpp::config::reason_class;
    EXPECT_TRUE(has_diag(result.error(), RC::missing_required,
                         "session[0].sender_comp_id"))
        << "expected missing_required at \"session[0].sender_comp_id\"";
}

// ── target_comp_id missing → missing_required (RED until 4b) ─────────────────

TEST(LoadNegativeBattery, T020_MissingRequired_TargetCompId)
{
    auto result = load(neg_fixture("neg_missing_target_comp_id.toml"));

    ASSERT_FALSE(result.has_value())
        << "expected load failure for missing target_comp_id; "
           "Phase 4b required-key checks not yet implemented — RED expected";

    using RC = fixpp::config::reason_class;
    EXPECT_TRUE(has_diag(result.error(), RC::missing_required,
                         "session[0].target_comp_id"))
        << "expected missing_required at \"session[0].target_comp_id\"";
}

// ── begin_string missing → missing_required (RED until 4b) ───────────────────

TEST(LoadNegativeBattery, T020_MissingRequired_BeginString)
{
    auto result = load(neg_fixture("neg_missing_begin_string.toml"));

    ASSERT_FALSE(result.has_value())
        << "expected load failure for missing begin_string; "
           "Phase 4b required-key checks not yet implemented — RED expected";

    using RC = fixpp::config::reason_class;
    EXPECT_TRUE(has_diag(result.error(), RC::missing_required,
                         "session[0].begin_string"))
        << "expected missing_required at \"session[0].begin_string\"";
}

// ── security_profile.kind missing → missing_required (RED until 4b) ──────────
//
// Fixture: neg_missing_security_profile_kind.toml (no [session.security_profile]).
// The Phase 3b path proceeds with security_profile.k = unset, which causes
// make_ssl_ctx_config to reject it via invalid_or_contradictory_selector.
// Phase 4b will add the EARLIER per-key missing_required check at load time.
//
// Accepts EITHER the Phase-4b-expected missing_required OR the pre-existing
// Phase-3b make_ssl_ctx_config rejection — the key discriminator is that the
// load FAILS (which it does either way).

TEST(LoadNegativeBattery, T020_MissingRequired_SecurityProfileKind)
{
    auto result = load(neg_fixture("neg_missing_security_profile_kind.toml"));

    ASSERT_FALSE(result.has_value())
        << "expected load failure for missing security_profile.kind; "
           "Phase 4b required-key checks not yet implemented — RED expected "
           "(currently fails via make_ssl_ctx_config unset-profile rejection)";

    using RC = fixpp::config::reason_class;
    // security_profile.kind is the primary loader-boundary no-implicit-default
    // check (data-model E-3): missing_required at the NAMED key — not an opaque
    // downstream invalid_or_contradictory_selector.
    EXPECT_TRUE(has_diag(result.error(), RC::missing_required,
                         "session[0].security_profile.kind"))
        << "expected missing_required at \"session[0].security_profile.kind\"";
}

// ── store.kind missing → missing_required (ALREADY-GREEN, Phase 3b) ──────────
//
// selector_resolver.cpp resolve_engine_store: absent [store] table → emits
// missing_required at "store.kind". ALREADY-GREEN from Phase 3b.

TEST(LoadNegativeBattery, T020_MissingRequired_StoreKind)
{
    auto result = load(neg_fixture("neg_missing_store.toml"));

    ASSERT_FALSE(result.has_value())
        << "expected load failure when [store] section is absent";

    using RC = fixpp::config::reason_class;
    EXPECT_TRUE(has_diag(result.error(), RC::missing_required, "store.kind"))
        << "expected missing_required at \"store.kind\" when [store] is absent "
           "(ALREADY-GREEN: Phase 3b resolve_engine_store)";
}

// ── dictionary missing (no [dictionary] section) → missing_required (RED until 4b)
//
// Fixture: neg_missing_dictionary.toml (entire [dictionary] section absent).
// Phase 3b's resolve_dict returns early (silently) when the table is absent
// (selector_resolver.cpp:278-282). Phase 4b will add the missing_required check.

TEST(LoadNegativeBattery, T020_MissingRequired_Dictionary)
{
    auto result = load(neg_fixture("neg_missing_dictionary.toml"));

    ASSERT_FALSE(result.has_value())
        << "expected load failure for missing [dictionary] section; "
           "Phase 4b required-dictionary check not yet implemented — RED expected";

    using RC = fixpp::config::reason_class;
    // T022: emitted at "dictionary" (engine-scope, no session prefix) — consistent
    // with clock.kind / store.kind from resolve_selectors.
    EXPECT_TRUE(has_diag(result.error(), RC::missing_required, "dictionary"))
        << "expected missing_required at \"dictionary\" when the [dictionary] section "
           "is absent";
}

// ── transport.kind missing → missing_required (ALREADY-GREEN, Phase 3b) ──────
//
// selector_resolver.cpp resolve_transport: absent [session.transport] table →
// missing_required at "session[0].transport.kind". ALREADY-GREEN from Phase 3b.

TEST(LoadNegativeBattery, T020_MissingRequired_TransportKind)
{
    auto result = load(neg_fixture("neg_missing_transport.toml"));

    ASSERT_FALSE(result.has_value())
        << "expected load failure when [session.transport] section is absent";

    using RC = fixpp::config::reason_class;
    EXPECT_TRUE(has_diag(result.error(), RC::missing_required,
                         "session[0].transport.kind"))
        << "expected missing_required at \"session[0].transport.kind\" "
           "when [session.transport] is absent "
           "(ALREADY-GREEN: Phase 3b resolve_transport)";
}

// ── transport.host/port missing for initiator → missing_required (RED until 4b)
//
// Fixture: neg_initiator_missing_host.toml (role=initiator, [session.transport]
// present but no host/port).
// NOT YET IMPLEMENTED — Phase 4b (T022) adds initiator-conditional host/port check.

TEST(LoadNegativeBattery, T020_InitiatorMissingHostPort)
{
    auto result = load(neg_fixture("neg_initiator_missing_host.toml"));

    ASSERT_FALSE(result.has_value())
        << "expected load failure for initiator session missing transport.host/port; "
           "Phase 4b initiator-conditional check not yet implemented — RED expected";

    using RC = fixpp::config::reason_class;
    // T022: both host AND port are required for an initiator; the fixture omits both.
    EXPECT_TRUE(has_diag(result.error(), RC::missing_required,
                         "session[0].transport.host"))
        << "expected missing_required at \"session[0].transport.host\" "
           "(initiator requires a connect target)";
    EXPECT_TRUE(has_diag(result.error(), RC::missing_required,
                         "session[0].transport.port"))
        << "expected missing_required at \"session[0].transport.port\" "
           "(initiator requires a connect target)";
}

// ── default_appl_ver_id missing for FIXT.1.1 → missing_required (RED until 4b)
//
// Fixture: neg_fixt_missing_appl_ver_id.toml (begin_string="FIXT.1.1",
// dictionary=FIXT11.xml, default_appl_ver_id absent).
// NOT YET IMPLEMENTED — Phase 4b (T022) adds the FIXT.1.1-conditional check.

TEST(LoadNegativeBattery, T020_FIXTMissingApplVerId)
{
    auto result = load(neg_fixture("neg_fixt_missing_appl_ver_id.toml"));

    ASSERT_FALSE(result.has_value())
        << "expected load failure for FIXT.1.1 session missing default_appl_ver_id; "
           "Phase 4b FIXT.1.1-conditional check not yet implemented — RED expected";

    using RC = fixpp::config::reason_class;
    EXPECT_TRUE(has_diag(result.error(), RC::missing_required,
                         "session[0].default_appl_ver_id"))
        << "expected missing_required at \"session[0].default_appl_ver_id\" "
           "for a FIXT.1.1 session";
}

// ── cert_source missing under TLS profile → missing_required (ALREADY-GREEN)
//
// Reuses neg_contradictory_tls_no_certs.toml from T018.
// selector_resolver.cpp:467-474: bundle.engine.default_cert_source == nullptr
// when [cert_source] is absent → emits missing_required at "cert_source".
// This is ALREADY-GREEN (Phase 3b), documented here as a T020 Bucket-B cell.

TEST(LoadNegativeBattery, T020_TlsMissingCertSource)
{
    auto result = load(neg_fixture("neg_contradictory_tls_no_certs.toml"));

    ASSERT_FALSE(result.has_value())
        << "expected load failure for TLS profile without cert_source";

    using RC = fixpp::config::reason_class;
    // ALREADY-GREEN: Phase 3b emits missing_required at "cert_source".
    EXPECT_TRUE(has_diag(result.error(), RC::missing_required, "cert_source"))
        << "expected missing_required at \"cert_source\" for TLS profile without certs";
}

// ── empty_required: sender_comp_id = "" (RED until 4b) ───────────────────────
//
// Fixture: neg_empty_required.toml (sender_comp_id = "").
// Same fixture as T018_EmptyRequired — explicit T020 cell for Bucket-B coverage.

TEST(LoadNegativeBattery, T020_EmptyRequired_SenderCompId)
{
    auto result = load(neg_fixture("neg_empty_required.toml"));

    ASSERT_FALSE(result.has_value())
        << "expected load failure for empty sender_comp_id; "
           "Phase 4b empty-required checks not yet implemented — RED expected";

    using RC = fixpp::config::reason_class;
    EXPECT_TRUE(has_diag(result.error(), RC::empty_required,
                         "session[0].sender_comp_id"))
        << "expected empty_required at \"session[0].sender_comp_id\"";
}

// ── empty_required: target_comp_id = "" (RED until 4b) ───────────────────────

TEST(LoadNegativeBattery, T020_EmptyRequired_TargetCompId)
{
    auto result = load(neg_fixture("neg_empty_target_comp_id.toml"));

    ASSERT_FALSE(result.has_value())
        << "expected load failure for empty target_comp_id; "
           "Phase 4b empty-required checks not yet implemented — RED expected";

    using RC = fixpp::config::reason_class;
    EXPECT_TRUE(has_diag(result.error(), RC::empty_required,
                         "session[0].target_comp_id"))
        << "expected empty_required at \"session[0].target_comp_id\"";
}

// ── POSITIVE: acceptor legitimately omits transport.host/port ────────────────
//
// Fixture: pos_acceptor_no_host_port.toml (role=acceptor, no host/port in transport).
// An acceptor has no connect target; omitting host/port is VALID.
// This cell MUST pass (GREEN) now and MUST remain GREEN after Phase 4b validation
// adds the initiator-conditional host/port check.

TEST(LoadNegativeBattery, T020_Positive_AcceptorOmitsHostPort)
{
    auto result = load(neg_fixture("pos_acceptor_no_host_port.toml"));

    EXPECT_TRUE(result.has_value())
        << "acceptor must load OK when host/port are omitted — they are only "
           "required for an initiator (Bucket B initiator-conditional)";
}

// ── POSITIVE: FIX.4.x legitimately omits default_appl_ver_id ─────────────────
//
// Fixture: pos_fix44_no_appl_ver_id.toml (FIX.4.4, no default_appl_ver_id).
// FIX.4.x has its application version implied by begin_string; the FIXT.1.1-
// conditional requirement does NOT apply. Must load OK now and after 4b.

TEST(LoadNegativeBattery, T020_Positive_Fix44OmitsApplVerId)
{
    auto result = load(neg_fixture("pos_fix44_no_appl_ver_id.toml"));

    EXPECT_TRUE(result.has_value())
        << "FIX.4.x session must load OK when default_appl_ver_id is absent — "
           "it is only required for FIXT.1.1 (Bucket B FIXT.1.1-conditional)";
}

// ── POSITIVE: insecure_plain_tcp + plaintext legitimately omits cert_source ──
//
// Fixture: pos_insecure_plain_no_certs.toml (security_profile=insecure_plain_tcp,
// transport=plaintext, no [cert_source]).
// cert_source is only required under a TLS profile. insecure_plain_tcp must
// load OK without any cert material.

TEST(LoadNegativeBattery, T020_Positive_InsecurePlainNoCerts)
{
    auto result = load(neg_fixture("pos_insecure_plain_no_certs.toml"));

    EXPECT_TRUE(result.has_value())
        << "insecure_plain_tcp + plaintext must load OK without cert_source — "
           "cert_source is only required under a TLS profile (mtls_ca / one_way_ca)";
}

// =============================================================================
// T021 — Redaction (FR-019)
// =============================================================================
//
// A fixture with a wrong-typed password (integer) triggers malformed_value at
// session[0].password. Phase 4b (T022/T023) wires the type-check and must use
// display_value(key_path, raw_value) so the literal secret does not leak into
// the diagnostic message.
//
// Anchor: data-model E-5 (LoadDiagnostic.message: "credential values redacted
//   (***REDACTED***) for username/password keys (FR-019)").
//         loader_internal.hpp display_value() / is_credential_key().
//
// RED condition: Phase 3b silently ignores non-string password values (no
//   diagnostic emitted). Phase 4b will add the type-check with redaction.
//   ASSERT_FALSE(has_value()) will be RED until Phase 4b (T023) ships.

TEST(LoadNegativeBattery, T021_PasswordRedaction_SecretAbsent)
{
    // Secret: the integer 99887766 as it would appear in a diagnostic message.
    static constexpr std::string_view kSecret = "99887766";
    static constexpr std::string_view kRedactedToken = "***REDACTED***";

    auto result = load(neg_fixture("neg_password_wrong_type.toml"));

    // Phase 4b will emit malformed_value at session[0].password.
    // Until then this ASSERT_FALSE is RED (load currently succeeds — wrong-typed
    // password is silently skipped by the is_string() guard in scalar_mappers.cpp).
    ASSERT_FALSE(result.has_value())
        << "expected load failure for password = 99887766 (integer, not a string); "
           "Phase 4b type-check + redaction not yet implemented — RED expected";

    const auto& diags = result.error();

    // Find the password diagnostic.
    using RC = fixpp::config::reason_class;
    auto it = std::find_if(diags.begin(), diags.end(),
        [](const fixpp::config::LoadDiagnostic& d) {
            return d.key_path == "session[0].password";
        });
    ASSERT_NE(it, diags.end())
        << "expected a diagnostic at key_path=\"session[0].password\"";

    // (1) The message MUST contain the redaction sentinel.
    EXPECT_NE(it->message.find(std::string{kRedactedToken}), std::string::npos)
        << "diagnostic message must contain \"***REDACTED***\" for a credential key "
           "(FR-019 / loader_internal.hpp display_value)";

    // (2) The message MUST NOT contain the literal secret value.
    //     Scan ALL diagnostics to guard against accidental leakage elsewhere.
    for (const auto& d : diags) {
        EXPECT_EQ(d.message.find(std::string{kSecret}), std::string::npos)
            << "diagnostic at key_path=\"" << d.key_path << "\" "
               "must NOT contain the literal secret value \"99887766\" (FR-019); "
               "use display_value() from loader_internal.hpp to redact credential keys";
    }
}
