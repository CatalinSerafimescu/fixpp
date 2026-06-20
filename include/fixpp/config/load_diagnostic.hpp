// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/config/load_diagnostic.hpp
// fixpp::config — per-key load diagnostic type and reason enum (E-5).
// 044-toml-session-config (FR-012 / FR-018).
//
// This header is intentionally minimal and dependency-free so it can be
// included by any consumer without dragging in session/transport/tls headers.
#pragma once

#include <cstdint>
#include <string>

namespace fixpp::config {

// Closed classification of why a key was rejected or flagged.
// Used by LoadDiagnostic::reason (see below).
enum class reason_class : std::uint8_t {
    parse_error,                           // TOML parse failure
    unknown_key,                           // key not in schema
    recognized_not_yet_supported_step2,   // known but deferred to step-2
    missing_required,                      // required key absent
    empty_required,                        // required key present but empty
    malformed_value,                       // present but unparseable
    out_of_range,                          // numeric/duration outside bounds
    unknown_enum,                          // unrecognized enum string value
    invalid_or_contradictory_selector,    // selector combination illegal
};

// Source location within the TOML file (line/column, 1-based).
struct SourceLoc {
    std::uint32_t line = 0;
    std::uint32_t col  = 0;
};

// One diagnostic entry emitted by load_toml_config() per offending key.
// FR-018: the loader collects ALL diagnostics in a single pass.
// FR-019: credential VALUES are redacted in `message`.
struct LoadDiagnostic {
    std::string  key_path;      // e.g. "session[0].heartbeat_interval"
    reason_class reason{};
    SourceLoc    location{};
    std::string  message;       // human-readable detail; credential values redacted
};

}  // namespace fixpp::config
