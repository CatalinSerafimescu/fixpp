// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/config/mappers.hpp
// 044-toml-session-config Phase-3b internal mapper declarations.
//
// NOT a public header. NEVER include from include/fixpp/config/ (FR-004).
// Include from src/config/*.cpp only.
//
// FR-004 / SC-006: tomlplusplus is PRIVATE.
// This header is toml-aware (includes toml++) because its callers are all
// in src/config/*.cpp where toml++ is already private.
#pragma once

#include <filesystem>
#include <string_view>
#include <vector>

// toml++ include — PRIVATE to src/config/ only (FR-004).
// Routed through the hardened shim (T039): NEVER include <toml++/toml.hpp>
// directly from a config TU (ODR — see toml_include.hpp).
#include "toml_include.hpp"

// loader_internal.hpp is toml-free — it provides DiagnosticAccumulator.
#include "loader_internal.hpp"

#include <fixpp/config/config_bundle.hpp>
#include <fixpp/config/toml_config_loader.hpp>
#include <fixpp/session/session_config.hpp>

namespace fixpp::config::detail {

// ---------------------------------------------------------------------------
// T013 — scalar mapper
// ---------------------------------------------------------------------------
//
// Maps ~28 Bucket-A scalar, bool, duration, and enum fields from the merged
// TOML session table onto `out`.
//
// Rules:
//  - Duration keys require an explicit unit suffix ("30s", "500ms"); a bare
//    integer is a malformed_value diagnostic.
//  - Enum-token fields accept only the exact canonical spellings listed in
//    the respective enum definitions (data-model §D-9); an unknown token
//    produces an unknown_enum diagnostic.
//  - Credential keys (username, password) are stored verbatim in `out` but
//    their values in diagnostic messages go through display_value() (FR-019).
//  - Absent optional key → leave struct default; no diagnostic emitted.
//  - All failures accumulate into `acc`; mapping does not stop on first error.
//
// `key_prefix` is the key path prefix for diagnostics (e.g. "session[0]").
// Callers pass this from the outer session-index context.
//
// `raw_session` is a pointer to the original (pre-merge) parsed session table.
// When provided, diagnostic SourceLoc is read from `raw_session` (which retains
// toml++ source positions), NOT from `merged` (whose nodes lose position on copy).
// Pass nullptr when no raw table is available (e.g. pure-code construction).
// (FR-017 — SourceLoc population)
void map_scalars(const toml::table&               merged,
                 fixpp::session::SessionConfig&   out,
                 DiagnosticAccumulator&           acc,
                 std::string_view                 key_prefix,
                 const toml::table*               raw_session = nullptr);

// ---------------------------------------------------------------------------
// T014 — structured-member mapper
// ---------------------------------------------------------------------------
//
// Maps sub-table structured members:
//  - [session.security_profile]  → out.security_profile.k
//  - [session.compid_authorization_policy]  → out.compid_authorization_policy
//  - [session.transport]  → out.reconnect_endpoint.{host, port}
//  - [session.reconnect_policy]  → out.reconnect_policy (if present)
//
// Absent sub-table → leave struct default; no diagnostic.
// `key_prefix` is the same prefix passed to map_scalars.
// `raw_session` serves the same purpose as in map_scalars (FR-017).
void map_structured_members(const toml::table&               merged,
                             fixpp::session::SessionConfig&   out,
                             DiagnosticAccumulator&           acc,
                             std::string_view                 key_prefix,
                             const toml::table*               raw_session = nullptr);

// ---------------------------------------------------------------------------
// T015/T016 — selector resolver
// ---------------------------------------------------------------------------
//
// Resolves engine-scope selectors (clock, store, cert_source, dictionary) and
// the per-session transport selector (transport.kind → TransportFactory).
//
// `root_tbl`             — the parsed root TOML table (contains [clock], [store],
//                          [cert_source], [dictionary] at the top level).
// `merged_session_tables` — vector of merged session tables (after [default]-merge);
//                          one entry per [[session]] in document order.
// `base_dir`             — config file's parent directory (for relative path resolution).
// `opts`                 — LoadOptions carrying engine_executor + resource.
// `bundle`               — the ConfigBundle being assembled (engine slice populated here).
// `acc`                  — diagnostic accumulator (all errors added here; no early return).
void resolve_selectors(const toml::table&                     root_tbl,
                       const std::vector<const toml::table*>& merged_session_tables,
                       const std::filesystem::path&           base_dir,
                       const LoadOptions&                     opts,
                       ConfigBundle&                          bundle,
                       DiagnosticAccumulator&                 acc);

}  // namespace fixpp::config::detail
