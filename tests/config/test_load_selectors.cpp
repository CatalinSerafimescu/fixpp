// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/config/test_load_selectors.cpp
// 044-toml-session-config US3 — selector-kind matrix + deferred/guard cells.
//
// T026 [US3] — selector-kind matrix: for each selectable kind, load a fixture
//   and assert the correct concrete built-in is resolved (discriminating).
//   Cells that are ALREADY implemented go GREEN (confirming existing impl).
//   Cells that hit an unimplemented resolver go FAIL (= real T029 gap).
//
// T028 [US3] — deferred-selection + FR-011 guard:
//   - dictionary.kind="version"  → recognized_not_yet_supported_step2
//   - dialect_overlay            → recognized_not_yet_supported_step2
//   - security_profile.kind="mtls_pinned" → recognized_not_yet_supported_step2
//   - mode="direct_executor" without already_serialized_executor=true
//                                → invalid_or_contradictory_selector (FR-011 / US3 AC-4)
//
// Assertion strategy (anti-pattern library):
//   - store → dynamic_cast to concrete type (not just non-null).
//   - transport → kind() enum (not just non-null).
//   - clock  → dynamic_cast<system_clock_source*> (mirrors T012).
//   - dictionary → size()==1 (positive cardinality).
//   - deferred → check reason AND key_path (discriminating; cannot be bypassable guard).
//
// Anti-hang: load_toml_config is synchronous (cold). We create an io_context to
//   supply LoadOptions::engine_executor but NEVER call ctx.run().
//
// Anchors: plan.md US3 / data-model §E-2 / FR-011 / specs/044 selector_resolver.cpp.

#include <gtest/gtest.h>

#include <algorithm>
#include <asio/io_context.hpp>
#include <filesystem>
#include <fixpp/config/config_bundle.hpp>
#include <fixpp/config/load_diagnostic.hpp>
#include <fixpp/config/toml_config_loader.hpp>
#include <fixpp/core/system_clock_source.hpp>
#include <fixpp/session/file_store_factory.hpp>
#include <fixpp/session/memory_store_factory.hpp>
#include <fixpp/transport/transport_factory.hpp>
#include <string>
#include <string_view>

#ifndef FIXPP_CONFIG_FIXTURE_DIR
#error "FIXPP_CONFIG_FIXTURE_DIR must be set by CMake"
#endif

namespace {

// ── Helpers ───────────────────────────────────────────────────────────────────

std::filesystem::path fixture(std::string_view name) {
    return std::filesystem::path{std::string{FIXPP_CONFIG_FIXTURE_DIR}} / name;
}

fixpp::config::LoadResult load(const std::filesystem::path& path) {
    asio::io_context ctx;
    fixpp::config::LoadOptions opts;
    opts.engine_executor = ctx.get_executor();
    return fixpp::config::load_toml_config(path, opts);
}

// Discriminating diagnostic search: checks BOTH reason AND key_path.
bool has_diag(const std::vector<fixpp::config::LoadDiagnostic>& diags,
              fixpp::config::reason_class expected_reason, std::string_view expected_key_path) {
    return std::any_of(diags.begin(), diags.end(), [&](const fixpp::config::LoadDiagnostic& d) {
        return d.reason == expected_reason && d.key_path == expected_key_path;
    });
}

}  // namespace

// =============================================================================
// T026 — selector-kind matrix
// =============================================================================

// ── T026-A: store=memory → MemoryStoreFactory ─────────────────────────────────
//
// Fixture: happy_full.toml ([store] kind="memory").
// Assert: dynamic_cast<MemoryStoreFactory*> succeeds.
// Also: yields_persistent_store()==false discriminates (base default=true; FileStoreFactory
// does not override; only MemoryStoreFactory explicitly returns false).
// PASS expected (existing impl: selector_resolver.cpp line 158).

TEST(LoadSelectors, T026_StoreMemory) {
    auto result = load(fixture("happy_full.toml"));

    ASSERT_TRUE(result.has_value()) << "load failed for happy_full.toml";

    const auto& engine = result->engine;
    ASSERT_NE(engine.default_store_factory, nullptr) << "expected a store factory, got null";

    auto* mem =
        dynamic_cast<fixpp::session::MemoryStoreFactory*>(engine.default_store_factory.get());
    EXPECT_NE(mem, nullptr) << "expected MemoryStoreFactory from store.kind=\"memory\"; "
                               "dynamic_cast returned null (wrong concrete type)";

    // Secondary discriminator: MemoryStoreFactory::yields_persistent_store()==false.
    EXPECT_FALSE(engine.default_store_factory->yields_persistent_store())
        << "MemoryStoreFactory must report yields_persistent_store()==false "
           "(base default is true; only MemoryStoreFactory overrides it false)";
}

// ── T026-B: store=file → FileStoreFactory ─────────────────────────────────────
//
// Fixture: pos_file_store.toml ([store] kind="file" + directory=".").
// Assert: dynamic_cast<FileStoreFactory*> succeeds.
//
// NOTE: The selector_resolver.cpp resolve_engine_store() ONLY handles kind=="memory";
// kind="file" falls through to the unknown_enum branch (line 161-167). This test
// FAILS until T029 implements the file-store selector. A FAIL here is a real T029 gap.

TEST(LoadSelectors, T026_StoreFile) {
    auto result = load(fixture("pos_file_store.toml"));

    ASSERT_TRUE(result.has_value()) << "load failed for pos_file_store.toml; "
                                       "store=file selector not yet implemented (T029 gap) — "
                                       "expected FAIL until T029 wires FileStoreFactory";

    const auto& engine = result->engine;
    ASSERT_NE(engine.default_store_factory, nullptr)
        << "expected a FileStoreFactory, got null (T029 gap)";

    auto* file =
        dynamic_cast<fixpp::session::FileStoreFactory*>(engine.default_store_factory.get());
    EXPECT_NE(file, nullptr) << "expected FileStoreFactory from store.kind=\"file\"; "
                                "dynamic_cast returned null (T029 gap — resolver not yet wired)";
}

// ── T026-C: transport=tls → transport_security_kind::tls ──────────────────────
//
// Fixture: happy_full.toml ([session.transport] kind="tls").
// Assert: default_transport_factory->kind() == transport_security_kind::tls.
// PASS expected (existing impl).

TEST(LoadSelectors, T026_TransportTLS) {
    auto result = load(fixture("happy_full.toml"));

    ASSERT_TRUE(result.has_value()) << "load failed for happy_full.toml";

    const auto& engine = result->engine;
    ASSERT_NE(engine.default_transport_factory, nullptr)
        << "expected TLS transport factory, got null";

    EXPECT_EQ(engine.default_transport_factory->kind(),
              fixpp::transport::transport_security_kind::tls)
        << "expected transport_security_kind::tls from transport.kind=\"tls\"";
}

// ── T026-D: transport=plaintext (insecure_plain_tcp) → transport_security_kind::plaintext
//
// Fixture: pos_insecure_plain_no_certs.toml ([session.transport] kind="plaintext").
// Assert: default_transport_factory->kind() == transport_security_kind::plaintext.
// PASS expected (existing impl — 043 plaintext transport).

TEST(LoadSelectors, T026_TransportPlaintext) {
    auto result = load(fixture("pos_insecure_plain_no_certs.toml"));

    ASSERT_TRUE(result.has_value()) << "load failed for pos_insecure_plain_no_certs.toml";

    const auto& engine = result->engine;
    ASSERT_NE(engine.default_transport_factory, nullptr)
        << "expected plaintext transport factory, got null";

    EXPECT_EQ(engine.default_transport_factory->kind(),
              fixpp::transport::transport_security_kind::plaintext)
        << "expected transport_security_kind::plaintext from transport.kind=\"plaintext\"";
}

// ── T026-E: dictionary by path → non-empty dictionaries vector ────────────────
//
// Fixture: happy_full.toml ([dictionary] kind="path" path="FIX44.xml").
// Assert: dictionaries.size()==1 and the entry is non-null.
// PASS expected (existing impl).

TEST(LoadSelectors, T026_DictionaryByPath) {
    auto result = load(fixture("happy_full.toml"));

    ASSERT_TRUE(result.has_value()) << "load failed for happy_full.toml";

    const auto& engine = result->engine;
    EXPECT_EQ(engine.dictionaries.size(), std::size_t{1})
        << "expected exactly 1 dictionary from [dictionary] kind=\"path\"";

    if (!engine.dictionaries.empty()) {
        EXPECT_NE(engine.dictionaries[0], nullptr) << "dictionaries[0] must not be null";
    }
}

// ── T026-F: clock=system → system_clock_source ────────────────────────────────
//
// Fixture: happy_full.toml ([clock] kind="system").
// Assert: dynamic_cast<system_clock_source*> succeeds.
// PASS expected (mirrors T012 assertion in test_load_happy_path.cpp).

TEST(LoadSelectors, T026_ClockSystem) {
    auto result = load(fixture("happy_full.toml"));

    ASSERT_TRUE(result.has_value()) << "load failed for happy_full.toml";

    const auto& engine = result->engine;
    ASSERT_NE(engine.clock, nullptr) << "expected system_clock_source, got null";

    auto* sys_clock = dynamic_cast<fixpp::core::system_clock_source*>(engine.clock.get());
    EXPECT_NE(sys_clock, nullptr) << "expected engine.clock to be a system_clock_source; "
                                     "dynamic_cast returned null (wrong concrete type)";
}

// =============================================================================
// T028 — deferred-selection + FR-011 guard cells
// =============================================================================

// ── T028-A: dictionary.kind="version" → recognized_not_yet_supported_step2 ────
//
// Fixture: neg_step2_dict_version.toml (dictionary.kind="version").
// Assert: load fails; diagnostic at "dictionary.kind" with
// reason=recognized_not_yet_supported_step2. PASS expected (already handled in
// selector_resolver.cpp line 307-313).

TEST(LoadSelectors, T028_DictionaryVersionDeferred) {
    auto result = load(fixture("neg_step2_dict_version.toml"));

    ASSERT_FALSE(result.has_value())
        << "expected load failure for dictionary.kind=\"version\" (step-2 deferred)";

    using RC = fixpp::config::reason_class;
    EXPECT_TRUE(has_diag(result.error(), RC::recognized_not_yet_supported_step2, "dictionary.kind"))
        << "expected recognized_not_yet_supported_step2 at \"dictionary.kind\" "
           "for dictionary.kind=\"version\"";
}

// ── T028-B: dialect_overlay → recognized_not_yet_supported_step2 ──────────────
//
// Fixture: neg_step2_dialect_overlay.toml (dialect_overlay key in [[session]]).
// Assert: load fails; diagnostic at "session[0].dialect_overlay" with
//   reason=recognized_not_yet_supported_step2.
// NOTE: Phase 4b implements this scan; this test may FAIL (RED) until 4b.

TEST(LoadSelectors, T028_DialectOverlayDeferred) {
    auto result = load(fixture("neg_step2_dialect_overlay.toml"));

    ASSERT_FALSE(result.has_value())
        << "expected load failure for dialect_overlay (step-2 deferred); "
           "Phase 4b step-2 detection not yet implemented — RED expected";

    using RC = fixpp::config::reason_class;
    EXPECT_TRUE(has_diag(result.error(), RC::recognized_not_yet_supported_step2,
                         "session[0].dialect_overlay"))
        << "expected recognized_not_yet_supported_step2 at \"session[0].dialect_overlay\"";
}

// ── T028-C: security_profile.kind="mtls_pinned" → recognized_not_yet_supported_step2
//
// Fixture: neg_step2_mtls_pinned.toml (security_profile.kind="mtls_pinned").
// Assert: load fails; diagnostic at "session[0].security_profile.kind".
// PASS expected (already handled in selector_resolver.cpp line 442-451).

TEST(LoadSelectors, T028_MtlsPinnedDeferred) {
    auto result = load(fixture("neg_step2_mtls_pinned.toml"));

    ASSERT_FALSE(result.has_value()) << "expected load failure for mtls_pinned (step-2 deferred)";

    using RC = fixpp::config::reason_class;
    EXPECT_TRUE(has_diag(result.error(), RC::recognized_not_yet_supported_step2,
                         "session[0].security_profile.kind"))
        << "expected recognized_not_yet_supported_step2 at "
           "\"session[0].security_profile.kind\"";
}

// ── T028-D: mode="direct_executor" without already_serialized_executor=true ───
//
// Fixture: neg_direct_executor_no_attest.toml (mode="direct_executor", no attest).
// Assert: load fails; diagnostic at "session[0].mode" with
// reason=invalid_or_contradictory_selector. US3 AC-4 / FR-011. PASS expected (already in
// scalar_mappers.cpp Rule 7a, line 373-382).

TEST(LoadSelectors, T028_DirectExecutorNoAttest) {
    auto result = load(fixture("neg_direct_executor_no_attest.toml"));

    ASSERT_FALSE(result.has_value())
        << "expected load failure for mode=\"direct_executor\" without "
           "already_serialized_executor=true (FR-011 guard)";

    using RC = fixpp::config::reason_class;
    EXPECT_TRUE(has_diag(result.error(), RC::invalid_or_contradictory_selector, "session[0].mode"))
        << "expected invalid_or_contradictory_selector at \"session[0].mode\" "
           "for mode=\"direct_executor\" without already_serialized_executor=true (FR-011)";
}

// =============================================================================
// T027 — divergent-cert multi-session → per-session transport_factory_override hygiene
// =============================================================================
//
// Fixture: multisession_divergent_cert.toml
//   session[0]: cert trio #1 (ECDSA P-256) + security_profile=mtls_ca → MATCHES engine default.
//   session[1]: cert trio #2 (RSA 2048)    + security_profile=one_way_ca → DIVERGES from engine
//   default.
//
// Assertions (all discriminating per D-6a / session_config.hpp:298-307):
//   (1) Load succeeds; sessions.size()==2    (fixture schema is valid)
//   (2) session[0].transport_factory_override is NULL   (uses shared engine default)
//   (3) session[1].transport_factory_override is NON-NULL, use_count()==1 (freshly minted, single
//   owner) (4) session[1].transport_factory_override != bundle.engine.default_transport_factory
//   (distinct instance)
//
// EXPECTED RESULT: T027 FAILS at assertion (3) — the current selector_resolver.cpp
// only builds the engine default from session[0] (line 387) and NEVER mints a
// per-session override for divergent sessions. This is a real T029 gap:
//   "wire the divergent-cert transport_factory_override minting path
//    (one-owner, use_count==1)" — tasks.md T029.
//
// Do NOT weaken assertions. A FAIL here is the correct Red phase signal.
//
// Anti-hang: load_toml_config is synchronous. We create io_context for
// LoadOptions::engine_executor but NEVER call ctx.run() (anti-hang rule).
//
// use_count() caution: access all shared_ptrs via const reference to the live
// bundle — never copy the SessionConfig or bind to a local (that would bump
// use_count and make the ==1 assertion spuriously fail).
//
// Anchor: research D-6a / session_config.hpp:298-307 / tasks.md T029.

// ── Mixed TLS + plaintext multi-session → plaintext session gets its own
//    plaintext factory override (#1 Gate B r1 fix) ───────────────────────────
//
// Before fix: the divergence scan skipped any non-"tls" session (line 612
// continue), so session[1] declaring "plaintext" got no override and silently
// fell back to the engine-default TLS factory (wrong transport — FR-007 drift).
// pos_multisession_tls_and_plain.toml was a positive fixture that encoded this
// latent bug.
//
// After fix (#1 Gate B r1): session[1] declaring "plaintext" with a TLS-engine-
// default receives a freshly-minted per-session plaintext factory override
// (make_asio_plain_transport_factory), ensuring it runs on the correct transport.
//
// Discriminating post-conditions (both required):
//   (a) session[1].transport_factory_override is non-null → NOT the engine default
//   (b) session[1].transport_factory_override->kind() == plaintext → correct kind

TEST(LoadSelectors, Cov_MultisessionTlsAndPlain) {
    auto result = load(fixture("pos_multisession_tls_and_plain.toml"));

    ASSERT_TRUE(result.has_value())
        << "pos_multisession_tls_and_plain.toml must load successfully; diagnostics: "
        << [&]() -> std::string {
        if (!result.has_value()) {
            std::string s;
            for (const auto& d : result.error()) {
                s += "[" + d.key_path + "] " + d.message + "; ";
            }
            return s;
        }
        return "(load succeeded)";
    }();

    ASSERT_EQ(result->sessions.size(), std::size_t{2});

    // Engine default: TLS factory (from session[0])
    EXPECT_NE(result->engine.default_transport_factory, nullptr)
        << "engine default transport factory must be non-null (TLS from session[0])";
    EXPECT_EQ(result->engine.default_transport_factory->kind(),
              fixpp::transport::transport_security_kind::tls);

    // session[0]: TLS, matches engine default → no override
    EXPECT_EQ(result->sessions[0].config.transport_factory_override, nullptr)
        << "session[0] matches engine default; override must be null";

    // session[1]: plaintext — #1 Gate B r1 fix: must receive a per-session
    // plaintext factory override (NOT fall back to the engine-default TLS factory).
    // (a) override must be non-null
    ASSERT_NE(result->sessions[1].config.transport_factory_override, nullptr)
        << "session[1] declares plaintext but engine default is TLS; "
           "#1 Gate B r1 fix must mint a plaintext factory override — "
           "null here means session[1] would run on the wrong (TLS) transport";
    // (b) override must be the plaintext kind
    EXPECT_EQ(result->sessions[1].config.transport_factory_override->kind(),
              fixpp::transport::transport_security_kind::plaintext)
        << "session[1] override must be plaintext kind, not TLS (#1 Gate B r1)";
    // (c) override must NOT be the engine-default factory (must be a fresh instance)
    EXPECT_NE(result->sessions[1].config.transport_factory_override,
              result->engine.default_transport_factory)
        << "session[1] plaintext override must be a fresh factory instance "
           "(use_count()==1 per D-6a), not the shared engine-default TLS factory";
}

// =============================================================================
// #1 Gate B r1 — plaintext-default per-session validation (symmetric path)
// =============================================================================
//
// The per-session transport.kind validation loop was added to the TLS-default
// branch during Gate B r1.  The symmetric plaintext-default branch also needs
// the same loop (half-restructure gap caught by Gate B advisor).
//
// Fixtures and expected outcomes:
//   neg_p1s1_transport_kind_missing.toml → missing_required @ "session[1].transport.kind"
//   neg_p1s1_transport_kind_tls.toml     → invalid_or_contradictory_selector @
//   "session[1].transport.kind" pos_p1_multisession_all_plain.toml   → success; session[1].override
//   == nullptr

// ── Plaintext-engine-default + session[1] missing transport section ────────────
//
// Discriminating: before the fix the plaintext branch had no per-session loop,
// so session[1] missing its [transport] silently loaded with no diagnostic
// (session[1] would run on the engine-default plaintext factory regardless).
// After fix: missing_required at "session[1].transport.kind".
TEST(LoadSelectors, GateBR1_Plaintext_Session1KindMissing) {
    auto result = load(fixture("neg_p1s1_transport_kind_missing.toml"));

    ASSERT_FALSE(result.has_value())
        << "expected load failure: session[1] has no [session.transport] on a "
           "plaintext-default engine; plaintext-branch per-session validation "
           "not yet implemented (symmetric half-restructure gap)";

    using RC = fixpp::config::reason_class;
    EXPECT_TRUE(has_diag(result.error(), RC::missing_required, "session[1].transport.kind"))
        << "expected missing_required at \"session[1].transport.kind\" "
           "(plaintext-default branch must validate non-first sessions)";
}

// ── Plaintext-engine-default + session[1] declares tls → cross-kind reject ─────
//
// Discriminating: before the fix session[1] declaring "tls" on a plaintext-default
// engine had no cert_source available and would silently emit malformed TLS config.
// After fix: invalid_or_contradictory_selector at "session[1].transport.kind".
TEST(LoadSelectors, GateBR1_Plaintext_Session1KindTLS) {
    auto result = load(fixture("neg_p1s1_transport_kind_tls.toml"));

    ASSERT_FALSE(result.has_value())
        << "expected load failure: session[1] declares \"tls\" but engine default is "
           "plaintext + no cert_source; cross-kind must fail closed (FR-012 / D-6a)";

    using RC = fixpp::config::reason_class;
    EXPECT_TRUE(has_diag(result.error(), RC::invalid_or_contradictory_selector,
                         "session[1].transport.kind"))
        << "expected invalid_or_contradictory_selector at \"session[1].transport.kind\" "
           "for tls-on-plaintext-engine without cert_source";
}

// ── Plaintext-engine-default + all sessions plaintext → positive (no override) ──
//
// Discriminating: the per-session validation loop must NOT add spurious diagnostics
// when session[1] matches the engine-default kind ("plaintext" → "plaintext").
TEST(LoadSelectors, GateBR1_Plaintext_MultisessionAllPlain) {
    auto result = load(fixture("pos_p1_multisession_all_plain.toml"));

    ASSERT_TRUE(result.has_value()) << "pos_p1_multisession_all_plain.toml must load successfully; "
                                       "two-plaintext-session config is valid; diagnostics: "
                                    << [&]() -> std::string {
        if (!result.has_value()) {
            std::string s;
            for (const auto& d : result.error()) {
                s += "[" + d.key_path + "] " + d.message + "; ";
            }
            return s;
        }
        return "(load succeeded)";
    }();

    ASSERT_EQ(result->sessions.size(), std::size_t{2});

    // Engine default: plaintext
    EXPECT_EQ(result->engine.default_transport_factory->kind(),
              fixpp::transport::transport_security_kind::plaintext);

    // session[1] matches engine default → no override needed
    EXPECT_EQ(result->sessions[1].config.transport_factory_override, nullptr)
        << "session[1] has same kind as engine default (plaintext); "
           "transport_factory_override must be null (no override minted)";
}

// ── Plaintext-default: session[1] has [transport] but NO kind key ─────────────
// Hits lines 792-797 in the plaintext-default per-session loop:
//   `else if (!kind_node)` → missing_required.
// Distinct from GateBR1_Plaintext_Session1KindMissing which has NO [transport]
// section at all (lines 778-784 = missing table arm).
TEST(LoadSelectors, GateBR1_Plaintext_Session1NoKindKey) {
    auto result = load(fixture("neg_p1s1_transport_no_kind.toml"));

    ASSERT_FALSE(result.has_value())
        << "session[1] with [session.transport] present but no 'kind' key must fail "
           "(plaintext-default branch; distinct from no-table arm)";

    using RC = fixpp::config::reason_class;
    EXPECT_TRUE(has_diag(result.error(), RC::missing_required, "session[1].transport.kind"))
        << "expected missing_required at \"session[1].transport.kind\" "
           "(plaintext-default: table present, kind key absent)";
}

// ── Plaintext-default: session[1] has transport.kind = 42 (non-string) ────────
// Hits lines 799-804 in the plaintext-default per-session loop:
//   the `else` arm (kind_node present but not is_string()).
TEST(LoadSelectors, GateBR1_Plaintext_Session1KindNonString) {
    auto result = load(fixture("neg_p1s1_transport_kind_nonstring.toml"));

    ASSERT_FALSE(result.has_value())
        << "session[1] with transport.kind=42 (integer, not string) must fail "
           "(plaintext-default branch)";

    using RC = fixpp::config::reason_class;
    EXPECT_TRUE(has_diag(result.error(), RC::missing_required, "session[1].transport.kind"))
        << "expected missing_required at \"session[1].transport.kind\" "
           "(plaintext-default: kind present but not a string)";
}

// ── Plaintext-default: session[1] has transport.kind = "" (empty) ─────────────
// Hits lines 807-813 in the plaintext-default per-session loop.
TEST(LoadSelectors, GateBR1_Plaintext_Session1KindEmpty) {
    auto result = load(fixture("neg_p1s1_transport_kind_empty.toml"));

    ASSERT_FALSE(result.has_value())
        << "session[1] with empty transport.kind must fail (plaintext-default branch)";

    using RC = fixpp::config::reason_class;
    EXPECT_TRUE(has_diag(result.error(), RC::empty_required, "session[1].transport.kind"))
        << "expected empty_required at \"session[1].transport.kind\" "
           "(plaintext-default branch)";
}

// ── Plaintext-default: session[1] has transport.kind = "websocket" (unknown) ──
// Hits lines 832-839 in the plaintext-default per-session loop.
TEST(LoadSelectors, GateBR1_Plaintext_Session1KindUnknown) {
    auto result = load(fixture("neg_p1s1_transport_kind_unknown.toml"));

    ASSERT_FALSE(result.has_value())
        << "session[1] with unknown transport.kind must fail (plaintext-default branch)";

    using RC = fixpp::config::reason_class;
    EXPECT_TRUE(has_diag(result.error(), RC::unknown_enum, "session[1].transport.kind"))
        << "expected unknown_enum at \"session[1].transport.kind\" "
           "for value 'websocket' (plaintext-default branch)";
}

TEST(LoadSelectors, T027_DivergentCertMultiSession) {
    auto result = load(fixture("multisession_divergent_cert.toml"));

    // (1) Load must succeed — a failure here is a fixture/schema bug, not the T029 gap.
    ASSERT_TRUE(result.has_value())
        << "load_toml_config failed for multisession_divergent_cert.toml; "
           "this is a fixture/schema bug, not the T029 gap. "
           "Diagnostics: "
        << [&]() -> std::string {
        if (!result.has_value()) {
            std::string s;
            for (const auto& d : result.error()) {
                s += "[" + d.key_path + "] " + d.message + "; ";
            }
            return s;
        }
        return "(load succeeded)";
    }();

    ASSERT_EQ(result->sessions.size(), std::size_t{2})
        << "expected exactly 2 sessions from multisession_divergent_cert.toml";

    // Access via const references to avoid bumping use_count.
    const auto& bundle = *result;
    const auto& s0_override = bundle.sessions[0].config.transport_factory_override;
    const auto& s1_override = bundle.sessions[1].config.transport_factory_override;
    const auto& engine_default = bundle.engine.default_transport_factory;

    // (2) session[0] matches the engine default → no override (null).
    EXPECT_EQ(s0_override, nullptr)
        << "session[0] matches the engine default cert/profile; "
           "transport_factory_override must be null (D-6a: uses shared engine default)";

    // (3) session[1] diverges → freshly-minted override, exclusively owned.
    //     This assertion FAILS until T029 implements divergent-cert override minting.
    ASSERT_NE(s1_override, nullptr)
        << "session[1] declares a divergent cert (RSA 2048) + one_way_ca profile vs "
           "the engine default (ECDSA P-256 + mtls_ca); "
           "transport_factory_override must be non-null (D-6a / T029 gap: "
           "resolver does not yet mint per-session overrides)";

    EXPECT_EQ(s1_override.use_count(), 1L)
        << "session[1].transport_factory_override must have use_count()==1 "
           "(freshly minted, single owner — cross-session sharing forbidden; "
           "session_config.hpp:298-307 / D-6a)";

    // (4) The override must be a distinct factory instance from the engine default.
    EXPECT_NE(s1_override, engine_default)
        << "session[1].transport_factory_override must NOT be the same object as "
           "bundle.engine.default_transport_factory (D-6a: per-session factory is "
           "freshly minted, never the shared engine anchor)";
}

// =============================================================================
// Gate B r2 Fix B — discriminating counter-test for collect-ALL kind validation
// =============================================================================
//
// Before Fix B, per-session transport.kind validation was embedded in the
// minting loops and was skipped whenever a prerequisite return fired (e.g. the
// cert-null return at :573).  validate_per_session_transport_kinds now runs
// UNCONDITIONALLY for all sessions 1..n, independent of global acc state.
//
// Discriminating case (proves the fix vs a 554-only patch):
//   TLS engine default + NO root [cert_source] → fires the cert-null return
//   (:573) that previously suppressed all further per-session validation.
//   session[1] has transport.kind="websocket" (unknown enum).
//
//   A 554-only patch: passes Codex's original root-unknown_key+websocket test
//   because session[0] is valid and the factory builds. But under the
//   cert-null path, the validation loop was never reached.
//
//   Fix B: BOTH diagnostics must accumulate:
//     (1) cert_source: missing_required
//     (2) session[1].transport.kind: unknown_enum
//
// Mutation-test: remove the validate_per_session_transport_kinds call in
// resolve_transport → the unknown_enum diagnostic disappears → test FAILS RED.

TEST(LoadSelectors, GateBR2B_TlsNoCerts_Session1WebsocketBothDiags) {
    auto result = load(fixture("neg_tls_no_certs_session1_websocket.toml"));

    ASSERT_FALSE(result.has_value())
        << "TLS-default config with no [cert_source] + session[1].kind=\"websocket\" "
           "must fail (cert_source missing + websocket unknown_enum)";

    const auto& diags = result.error();
    using RC = fixpp::config::reason_class;

    // (1) cert_source missing — from the prerequisite check, not suppressed.
    EXPECT_TRUE(has_diag(diags, RC::missing_required, "cert_source"))
        << "expected missing_required at \"cert_source\" "
           "(engine-level cert_source required for TLS transport)";

    // (2) session[1].transport.kind unknown_enum — from validate_per_session_transport_kinds
    // which runs UNCONDITIONALLY before the cert-null return.
    // A 554-only patch or pre-Fix-B code returns before this validation → diagnostic absent.
    EXPECT_TRUE(has_diag(diags, RC::unknown_enum, "session[1].transport.kind"))
        << "expected unknown_enum at \"session[1].transport.kind\" for \"websocket\" — "
           "validate_per_session_transport_kinds must run regardless of cert-null prerequisite "
           "(Gate B r2 Fix B / FR-018 collect-ALL)";
}
