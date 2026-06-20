// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/config/logger_resolver.hpp
// 045-observability-config (logging leg) — loader-local pending carrier.
//
// NOT a public header. NEVER include from include/fixpp/config/ (FR-004).
// Include from src/config/*.cpp only.
//
// Defines PendingLogger and PendingLoggerSet — the loader-local vessels that
// park resolved logger specs between the resolve, preflight, and construct
// steps (data-model E-3/E-5, research D-7, contract lines 38-66).
//
// Allocator scope (research D-7 / analyze C1):
//   N-1 (arena-bound): LoggerConfig cfg + pmr::vector<unique_ptr<Sink>> sinks
//       — bound to LoadOptions::resource; moved into make_shared<Logger> at
//       the final clean-accumulator construction step.
//   Global: std::string key_path + SourceLoc loc — diagnostic metadata only;
//       mirrors how 044's LoadDiagnostic.key_path/message are stored.
//
// PendingLoggerSet is file-scoped (N-2): all per-session pending loggers are
// accumulated across the whole file, NOT constructed inside the per-session
// loop, so a later session's error suppresses an earlier session's construction.
#pragma once

#include <cstddef>
#include <memory>
#include <memory_resource>
#include <optional>
#include <string>
#include <vector>

// LoggerConfig (by-value member → full type required)
#include <fixpp/log/logger.hpp>  // fixpp::log::LoggerConfig, fixpp::log::Logger

// Sink (via unique_ptr in pmr::vector → full type required for vector element)
#include <fixpp/log/sink.hpp>  // fixpp::log::Sink

// SourceLoc (by-value member → type from load_diagnostic.hpp)
#include <fixpp/config/load_diagnostic.hpp>  // fixpp::config::SourceLoc

namespace fixpp::config {

// ── PendingLogger ─────────────────────────────────────────────────────────────
//
// Holds the fully-resolved (side-effect-free) spec for one logger candidate:
//   cfg    — LoggerConfig scalars (capacity, on_overflow, drain_timeout, etc.)
//   sinks  — ordered list of minted sink objects (factories called, open() NOT)
//             bound to LoadOptions::resource (N-1 arena).
//   key_path — diagnostic path ("logger" or "session[i].logger")
//   loc      — TOML source location of the [logger] table
//   session_index — meaningful only for session-keyed entries in
//                   PendingLoggerSet::sessions; ignored for the engine entry.
//                   Value: the [[session]] array index (0-based).
struct PendingLogger {
    fixpp::log::LoggerConfig cfg;
    std::pmr::vector<std::unique_ptr<fixpp::log::Sink>> sinks;  // N-1 arena-bound
    std::string key_path;
    SourceLoc loc;
    std::size_t session_index{0};
};

// ── PendingLoggerSet ──────────────────────────────────────────────────────────
//
// File-scoped carrier (N-2): one engine slot + ordered per-session slots.
// Populated across the whole-file resolution pass; consumed by the single
// final construct_loggers_if_clean step.
struct PendingLoggerSet {
    std::optional<PendingLogger> engine;  // [logger] → bundle.engine.logger
    std::vector<PendingLogger>
        sessions;  // [session[i].logger] → sessions[i].config.logger_override
};

}  // namespace fixpp::config
