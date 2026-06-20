// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/config/loader_internal.cpp
// 044-toml-session-config — loader-internal helpers (T005 + T010).
// See loader_internal.hpp for API documentation.

#include "loader_internal.hpp"

#include <filesystem>
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
    const std::string_view authority =
        (auth_end == std::string_view::npos) ? url.substr(auth_start)
                                              : url.substr(auth_start, auth_end - auth_start);
    const auto at_pos = authority.rfind('@');
    if (at_pos == std::string_view::npos) {
        return std::string{url};  // no userinfo
    }
    // Replace everything between "://" and "@" (inclusive) with "***REDACTED***@".
    std::string result;
    result.reserve(url.size());
    result += url.substr(0, auth_start);     // scheme + "://"
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

}  // namespace fixpp::config::detail
