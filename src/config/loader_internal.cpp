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
