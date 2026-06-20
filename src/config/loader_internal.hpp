// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/config/loader_internal.hpp
// 044-toml-session-config — loader-internal helpers shared across Phase 2+.
//
// NOT a public header. NEVER include from include/fixpp/config/ (FR-004).
// Include from src/config/*.cpp only.
//
// Helpers:
//   T005 — credential redaction (FR-019 / data-model E-5)
//   T008 — noexcept-boundary throwing-site wrapper (trap_throw_to_expected)
//   T009 — collect-ALL diagnostic accumulator (DiagnosticAccumulator)
//   T010 — relative-path resolver (FR-016a / research D-7)
#pragma once

#include <exception>
#include <expected>
#include <filesystem>
#include <fixpp/config/load_diagnostic.hpp>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <vector>

// ---------------------------------------------------------------------------
// Portable -Wdeprecated-declarations suppression (shared across config TUs)
// ---------------------------------------------------------------------------
// The TOML loader deliberately maps to deprecated-but-supported enumerators
// (e.g. the insecure_plain_tcp / one_way_ca security profiles) ONLY when a
// config file explicitly requests them. Each such mapping arm is wrapped in
// this push/pop pair so the deliberate use does not warn, without globally
// silencing the diagnostic. Both clang and gcc are covered (the gcc-release
// preset is in the verify matrix).
#ifdef __clang__
#define FIXPP_SUPPRESS_DEPRECATED_BEGIN \
    _Pragma("clang diagnostic push")    \
        _Pragma("clang diagnostic ignored \"-Wdeprecated-declarations\"")
#define FIXPP_SUPPRESS_DEPRECATED_END _Pragma("clang diagnostic pop")
#else
#define FIXPP_SUPPRESS_DEPRECATED_BEGIN \
    _Pragma("GCC diagnostic push") _Pragma("GCC diagnostic ignored \"-Wdeprecated-declarations\"")
#define FIXPP_SUPPRESS_DEPRECATED_END _Pragma("GCC diagnostic pop")
#endif

namespace fixpp::config::detail {

// ---------------------------------------------------------------------------
// T005 — credential-redaction helpers (FR-019)
// ---------------------------------------------------------------------------

// Returns true when the final segment of a dotted key_path refers to a
// credential key that must never appear in a diagnostic message.
// Matches exactly "username" or "password" — NOT "password_policy" etc.
// Examples:
//   is_credential_key("session[0].password") → true
//   is_credential_key("session[0].username") → true
//   is_credential_key("session[0].heartbeat_interval") → false
//   is_credential_key("password_policy") → false  (no '.' separator, final seg = all)
[[nodiscard]] bool is_credential_key(std::string_view key_path) noexcept;

// Returns the display value to embed in a LoadDiagnostic::message.
// When key_path is a credential key, returns "***REDACTED***" regardless of
// the actual value. Otherwise returns value unchanged.
// Usage (Phase 2b message builder):
//   msg += display_value(key_path, raw_value);
[[nodiscard]] std::string_view display_value(std::string_view key_path, std::string_view value
                                             [[clang::lifetimebound]]) noexcept;

// ---------------------------------------------------------------------------
// T008 — noexcept-boundary throwing-site wrapper (validation rule 9 / D-3)
// ---------------------------------------------------------------------------
//
// Wraps a callable that may throw and converts ANY escaping exception into a
// LoadDiagnostic, returning std::expected<T, LoadDiagnostic>. This is the
// adapter Phase 3 (selector_resolver.cpp) uses for the three live throwing
// construction sites.
//
// IMPORTANT (Phase 3 rule — prefer noexcept factories, no trap_throw):
//   The following factories already return expected_t (noexcept) and MUST be
//   called directly with a check-the-expected pattern, NOT via trap_throw:
//     make_file_cert_source(cfg, mr)         — file_cert_source.hpp:56
//     make_asio_tls_transport_factory(...)   — returns expected_t
//     make_asio_plain_transport_factory(...) — returns expected_t
//     make_ssl_ctx_config(...)               — returns expected_t
//   Use trap_throw_to_expected ONLY for these three throwing sites (D-3):
//     (1) system_clock_source(opts.engine_executor)  — ctor NOT noexcept
//     (2) XmlLoader::load(path, mr)                  — throws xml_parse_error /
//                                                       unknown_version_error /
//                                                       xml_oom_error
//     (3) file_cert_source direct ctor               — throws (use make_file_cert_source
//                                                       instead whenever possible)
//
// Exception coverage: std::exception captures xml_parse_error and
// unknown_version_error (both : std::runtime_error) and xml_oom_error
// (: std::bad_alloc). The catch(...) guard handles non-std throws.
//
// Usage example (Phase 3):
//   auto result = detail::trap_throw_to_expected(
//       "engine.clock", reason_class::invalid_or_contradictory_selector,
//       [&]{ return std::make_shared<system_clock_source>(opts.engine_executor); });
//   if (!result) { acc.add(std::move(result).error()); return; }
//   bundle.engine.clock = std::move(*result);
template <typename F>
[[nodiscard]] auto trap_throw_to_expected(std::string key_path, reason_class reason, F&& f) noexcept
    -> std::expected<std::invoke_result_t<F>, LoadDiagnostic> {
    using T = std::invoke_result_t<F>;
    try {
        return std::expected<T, LoadDiagnostic>{std::forward<F>(f)()};
    } catch (const std::exception& e) {
        LoadDiagnostic diag;
        diag.key_path = std::move(key_path);
        diag.reason = reason;
        diag.message = e.what();
        // location stays {0,0}: throwing ctors do not carry TOML source position
        return std::unexpected(std::move(diag));
    } catch (...) {
        LoadDiagnostic diag;
        diag.key_path = std::move(key_path);
        diag.reason = reason;
        diag.message = "unknown exception at throwing construction site";
        return std::unexpected(std::move(diag));
    }
}

// ---------------------------------------------------------------------------
// T009 — collect-ALL diagnostic accumulator (FR-018)
// ---------------------------------------------------------------------------
//
// Collects per-key LoadDiagnostics across the whole file in one pass.
// The loader NEVER stops at the first error (FR-018).
// Reused by Phase 3/4 scalar_mappers and selector_resolver.
//
// Usage:
//   DiagnosticAccumulator acc;
//   acc.add(LoadDiagnostic{ .key_path="session[0].foo", .reason=..., ... });
//   if (!acc.empty()) return std::unexpected(std::move(acc).release());
class DiagnosticAccumulator {
public:
    DiagnosticAccumulator() = default;

    void add(LoadDiagnostic diag) { diags_.push_back(std::move(diag)); }

    [[nodiscard]] bool empty() const noexcept { return diags_.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return diags_.size(); }

    // Moves out the collected diagnostics. Call once at function exit.
    [[nodiscard]] std::vector<LoadDiagnostic> release() && { return std::move(diags_); }

private:
    std::vector<LoadDiagnostic> diags_;
};

// ---------------------------------------------------------------------------
// T010 — relative-path resolver (FR-016a / D-7)
// ---------------------------------------------------------------------------

// Resolves `rel` against `base_dir` (the config file's parent directory).
//   - If `rel` is absolute: returned verbatim (spec: "absolute verbatim").
//   - If `rel` is relative: joined as `base_dir / rel` then lexically
//     normalized (weakly_canonical without OS lookup, so it remains correct
//     even when the target does not yet exist).
//
// Uses the noexcept overload of weakly_canonical to avoid throwing inside
// the noexcept load boundary (validation rule 9).  On OS error the
// error_code is set and the raw joined path is returned as a safe fallback;
// callers validate existence/accessibility separately.
//
// `base_dir` should be `config_path.parent_path()`, captured ONCE by the
// caller so this helper is reused across many paths (D-7 "capture once").
[[nodiscard]] std::filesystem::path resolve_path(const std::filesystem::path& base_dir,
                                                 const std::filesystem::path& rel) noexcept;

}  // namespace fixpp::config::detail
