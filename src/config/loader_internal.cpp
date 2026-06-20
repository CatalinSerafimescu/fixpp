// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/config/loader_internal.cpp
// 044-toml-session-config — loader-internal helpers (T005 + T010).
// See loader_internal.hpp for API documentation.

#include "loader_internal.hpp"

#include <charconv>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>

namespace fixpp::config::detail {

// ---------------------------------------------------------------------------
// T005 — credential-redaction helpers (FR-019)
// ---------------------------------------------------------------------------

static constexpr std::string_view kRedacted = "***REDACTED***";

std::string redact_url_userinfo(std::string_view url) {
    // Detect the "://" authority prefix.
    const auto scheme_end = url.find("://");
    if (scheme_end == std::string_view::npos) {
        return std::string{url};
    }
    const auto auth_start = scheme_end + 3;  // first char after "://"
    // Find the '@' that delimits userinfo from host, within the authority.
    // The authority ends at the first '/', '?', '#', or end-of-string.
    const auto auth_end = url.find_first_of("/?#", auth_start);
    const std::string_view authority = (auth_end == std::string_view::npos)
                                           ? url.substr(auth_start)
                                           : url.substr(auth_start, auth_end - auth_start);
    const auto at_pos = authority.rfind('@');
    if (at_pos == std::string_view::npos) {
        return std::string{url};  // no userinfo
    }
    // Replace everything between "://" and "@" (inclusive) with "***REDACTED***@".
    std::string result;
    result.reserve(url.size());
    result += url.substr(0, auth_start);  // scheme + "://"
    result += kRedacted;
    result += '@';
    result += url.substr(auth_start + at_pos + 1);  // host + rest
    return result;
}

bool is_credential_key(std::string_view key_path) noexcept {
    // Extract the final dotted segment (everything after the last '.').
    // If there is no '.', the whole key_path is the final segment.
    const auto pos = key_path.rfind('.');
    const std::string_view final_seg =
        (pos == std::string_view::npos) ? key_path : key_path.substr(pos + 1);

    return final_seg == "username" || final_seg == "password";
}

std::string_view display_value(std::string_view key_path,
                               std::string_view value [[clang::lifetimebound]]) noexcept {
    if (is_credential_key(key_path)) {
        return kRedacted;
    }
    return value;
}

// ---------------------------------------------------------------------------
// T010 — relative-path resolver (FR-016a / D-7)
// ---------------------------------------------------------------------------

std::filesystem::path resolve_path(const std::filesystem::path& base_dir,
                                   const std::filesystem::path& rel) noexcept {
    if (rel.is_absolute()) {
        return rel;  // absolute: verbatim per D-7
    }

    // Relative: join with base_dir then normalize.
    // Use the error_code overload of weakly_canonical so we never throw
    // inside the noexcept load boundary (validation rule 9 / D-3).
    std::error_code ec;
    auto canonical = std::filesystem::weakly_canonical(base_dir / rel, ec);
    if (ec) {
        // OS-level error (e.g. permission denied on a path component).
        // Return the lexically-normalized joined path as a safe fallback;
        // the caller validates accessibility and will produce a diagnostic.
        return (base_dir / rel).lexically_normal();
    }
    return canonical;
}

// ---------------------------------------------------------------------------
// Dotted key-path builder — "prefix.key" (toml-free).
// ---------------------------------------------------------------------------

std::string kp(std::string_view prefix, std::string_view key) {
    std::string s;
    s.reserve(prefix.size() + 1 + key.size());
    s += prefix;
    s += '.';
    s += key;
    return s;
}

// ---------------------------------------------------------------------------
// Unit-suffix duration parser (toml-free) — single canonical definition.
// See loader_internal.hpp for the contract. The overflow guards (Gate B r1
// #3/#5) live here once so scalar_mappers.cpp and logger_resolver.cpp cannot
// drift.
// ---------------------------------------------------------------------------

ParsedDuration parse_duration_to_ms(std::string_view tok, std::string_view key_path,
                                    DiagnosticAccumulator& acc, SourceLoc loc) {
    if (tok.empty()) {
        acc.add(LoadDiagnostic{
            .key_path = std::string{key_path},
            .reason = reason_class::malformed_value,
            .location = loc,
            .message = "empty duration string",
        });
        return {.value_ms = 0, .ok = false};
    }

    // Find where the digits end.
    std::size_t num_end = 0;
    while (num_end < tok.size() && (tok[num_end] >= '0' && tok[num_end] <= '9')) {
        ++num_end;
    }

    if (num_end == 0) {
        acc.add(LoadDiagnostic{
            .key_path = std::string{key_path},
            .reason = reason_class::malformed_value,
            .location = loc,
            .message = "duration must start with a numeric value",
        });
        return {.value_ms = 0, .ok = false};
    }

    long long num = 0;
    auto [ptr, ec] = std::from_chars(tok.data(), tok.data() + num_end, num);
    if (ec != std::errc{}) {
        acc.add(LoadDiagnostic{
            .key_path = std::string{key_path},
            .reason = reason_class::malformed_value,
            .location = loc,
            .message = "duration numeric part could not be parsed",
        });
        return {.value_ms = 0, .ok = false};
    }

    std::string_view unit = tok.substr(num_end);
    if (unit.empty()) {
        acc.add(LoadDiagnostic{
            .key_path = std::string{key_path},
            .reason = reason_class::malformed_value,
            .location = loc,
            .message = R"(duration requires an explicit unit suffix (e.g. "30s", "500ms"))",
        });
        return {.value_ms = 0, .ok = false};
    }

    long long ms = 0;
    if (unit == "ms") {
        ms = num;
    } else if (unit == "s") {
        // #3 (Gate B r1): guard signed-multiply overflow before scaling.
        // from_chars yields values up to LLONG_MAX which overflows * 1000.
        if (num > (std::numeric_limits<long long>::max)() / 1000LL) {
            acc.add(LoadDiagnostic{
                .key_path = std::string{key_path},
                .reason = reason_class::out_of_range,
                .location = loc,
                .message = "duration value overflows when converted to milliseconds (s scale)",
            });
            return {.value_ms = 0, .ok = false};
        }
        ms = num * 1000LL;
    } else if (unit == "m") {
        if (num > (std::numeric_limits<long long>::max)() / 60000LL) {
            acc.add(LoadDiagnostic{
                .key_path = std::string{key_path},
                .reason = reason_class::out_of_range,
                .location = loc,
                .message = "duration value overflows when converted to milliseconds (m scale)",
            });
            return {.value_ms = 0, .ok = false};
        }
        ms = num * 60000LL;
    } else if (unit == "h") {
        if (num > (std::numeric_limits<long long>::max)() / 3600000LL) {
            acc.add(LoadDiagnostic{
                .key_path = std::string{key_path},
                .reason = reason_class::out_of_range,
                .location = loc,
                .message = "duration value overflows when converted to milliseconds (h scale)",
            });
            return {.value_ms = 0, .ok = false};
        }
        ms = num * 3600000LL;
    } else if (unit == "us") {
        // #5 (Gate B r1): "us" (microseconds) cannot be represented in the
        // loader's millisecond-only duration model without silent 1000x error
        // ("ms = num" was wrong) or silent truncation to 0 (num/1000).
        // Fail-closed: reject sub-millisecond units (FR-013 / FR-012).
        acc.add(LoadDiagnostic{
            .key_path = std::string{key_path},
            .reason = reason_class::out_of_range,
            .location = loc,
            .message = "sub-millisecond duration unit \"us\" is not supported "
                       "(the loader represents durations in whole milliseconds); "
                       "use \"ms\", \"s\", \"m\", or \"h\"",
        });
        return {.value_ms = 0, .ok = false};
    } else {
        acc.add(LoadDiagnostic{
            .key_path = std::string{key_path},
            .reason = reason_class::malformed_value,
            .location = loc,
            .message = std::string{"unrecognised duration unit: \""} + std::string{unit} + "\"",
        });
        return {.value_ms = 0, .ok = false};
    }

    return {.value_ms = ms, .ok = true};
}

}  // namespace fixpp::config::detail
