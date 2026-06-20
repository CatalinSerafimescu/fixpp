// SPDX-License-Identifier: AGPL-3.0-or-later
//
// contracts/observability_config.hpp — 045-observability-config (step 2, logging leg)
//
// ILLUSTRATIVE design contract, NOT the shipped header. It shows the additive
// surface change (EngineEstablishment extension) and the resolver entry-point
// shapes. The shipped change AMENDS include/fixpp/config/config_bundle.hpp and
// src/config/selector_resolver.cpp; load_diagnostic.hpp / toml_config_loader.hpp
// are UNCHANGED (no new reason_class, no entry-point signature change, no new
// fixpp_error_t — [const §X.4] pinned).
//
// SCOPE: logging leg only. Tracer/meter config is deferred (their OTLP export is
// an unimplemented provider-layer stub — see research.md §finding) — NO tracer/
// meter field is added to EngineEstablishment this step.
#pragma once

namespace fixpp::log { class Logger; }

namespace fixpp::config {

// ── E-2: the ONLY public-surface change — one additive shared_ptr field ──────
//
// AMENDS the existing EngineEstablishment (config_bundle.hpp). Shown here in
// isolation; the real struct also carries 044's clock/store/cert/transport/
// dictionaries fields, unchanged.
//
//   struct EngineEstablishment {
//       /* ... 044 fields ... */
//       std::shared_ptr<fixpp::log::Logger> logger;  // [logger] → null = no-op
//       // NO tracer / meter field this step (deferred — OTLP export unimplemented).
//   };
//
// The per-session logger override is ultimately written onto the EXISTING
// SessionConfig::logger_override member — but only at the final clean-accumulator
// construction step (see PendingLoggerSet below), NOT inside the per-session loop.
// No SessionConfig change.

// ── Pending carrier (loader-local; NOT a ConfigBundle field) — P2-a/N-1/N-2 ──
//
// The live Logger is copy+move-deleted and its ctor is side-effectful (opens every
// sink + spawns the drain thread), so the deferred model needs somewhere to PARK
// the move-only resolved spec between resolve -> preflight -> final-construct. The
// ConfigBundle/SessionConfig only hold the FINAL shared_ptr<Logger>, so the carrier
// lives in loader-local state, keyed by its destination:
//
//   struct PendingLogger {
//       LoggerConfig                                      cfg;
//       std::pmr::vector<std::unique_ptr<Sink>>           sinks;   // N-1: bound to
//                                                                  // LoadOptions::resource
//       std::string                                       key_path; // e.g. "logger"
//       SourceLoc                                         loc;
//       // target key (one of):
//       //   engine slot        -> assigns bundle.engine.logger
//       //   session index (i)  -> assigns bundle.sessions[i].config.logger_override
//   };
//   struct PendingLoggerSet {                       // file-scoped (N-2)
//       std::optional<PendingLogger>  engine;       // [logger]
//       std::vector<PendingLogger>    sessions;     // each [session].logger (carries i)
//   };
//
// N-1: the carrier's pmr::vector AND the minted sink allocations all use the 044
//      LoadOptions::resource; the move into make_shared<Logger> preserves that
//      allocator identity (the load-time arena must outlive construction).
// N-2: the set is FILE-SCOPED — every per-session PendingLogger is accumulated
//      across the whole file, NOT constructed inside the per-session loop, so a
//      later session's error still suppresses an earlier session's logger.

// ── Resolver entry points (internal to fixpp_config_toml; illustrative) ──────
//
// Each mirrors the 044 resolver shape, but resolves INTO the loader-local pending
// set (not directly into ConfigBundle — that is the final clean-construct step):
//   static void resolve_*(const toml::table& tbl, const std::filesystem::path& base_dir,
//                         const LoadOptions& opts, PendingLoggerSet& pending,
//                         detail::DiagnosticAccumulator& acc);
// Contracts (see data-model.md E-3..E-5):
//
//   resolve_engine_logger  — [logger] composite: resolves LoggerConfig scalars +
//                            a non-empty [[logger.sinks]] array into a
//                            std::pmr::vector<std::unique_ptr<Sink>> (side-effect-free —
//                            no open()), and EMITS a PendingLogger (engine slot) into
//                            `pending`. It does NOT construct the live Logger here and
//                            does NOT write ConfigBundle — construction is the single
//                            end-of-load step below (see research D-7).
//                            Zero sinks → empty_required. capacity not pow2 → out_of_range.
//                            ring_resource NEVER file-set (deferred arena).
//
//   resolve_log_sink       — one [[logger.sinks]] entry → unique_ptr<Sink> via the
//                            kind's existing factory (object minting only — open()
//                            is deferred to Logger construction):
//                              file   → FileSinkFactory   / FileSinkConfig
//                              syslog → SyslogSinkFactory  / SyslogSinkConfig
//                                       (#ifdef FIXPP_HAS_SYSLOG; #else →
//                                        invalid_or_contradictory_selector)
//                                       facility: closed name→LOG_* map (exact set in
//                                       data-model E-4; build-undefined LOG_* →
//                                       invalid_or_contradictory_selector)
//                              otlp   → OtlpLogSinkFactory / OtlpLogSinkConfig
//                                       (BUILD-CONDITIONAL: when fixpp_log_otlp /
//                                        FIXPP_CONFIG_HAS_OTLP absent, kind="otlp" →
//                                        invalid_or_contradictory_selector;
//                                        endpoint required; cert_source readable +
//                                        PEM-magic-validated (full CA parse at open);
//                                        use_grpc=true → recognized_not_yet_supported_step2)
//                            unknown kind → unknown_enum {file,syslog,otlp}.
//
//   (per-session)          — [session].logger reuses resolve_engine_logger, emitting
//                            a session-keyed PendingLogger (carrying the session index)
//                            into pending.sessions — NOT writing logger_override
//                            directly (that happens only at the clean-construct step).
//
//   construct_loggers_if_clean(PendingLoggerSet&&, ConfigBundle& bundle, acc)
//                          — the SOLE side-effectful step. Only when the whole-file
//                            accumulator is empty, moves each PendingLogger into
//                            make_shared<Logger>(std::move(cfg), std::move(sinks)) and
//                            assigns bundle.engine.logger (engine slot) /
//                            bundle.sessions[i].config.logger_override (session index).
//
// All resolvers append to the shared accumulator using the session-local
// acc.size() delta pattern (collect-ALL, D-7). After resolution, a SIDE-EFFECT-FREE
// load-time RESOURCE PREFLIGHT (file-sink dir already-exists + writable [no mkdir,
// no probe file], OTLP cert readable + PEM-magic-validated, OTLP endpoint present)
// runs over the pending set's resolved sinks, also collect-ALL. The live Logger —
// whose ctor opens every sink and starts the drain thread (NOT side-effect-free) —
// is constructed only when the whole-file accumulator is empty (FileSink::open()
// creates/opens the live log file, not the directory; the directory must pre-exist
// — the preflight requires it). A non-empty accumulator ⇒ LoadResult holds the diagnostics
// vector, no bundle, and no Logger constructed → no files opened, no directory
// created, no thread started (FR-014/FR-015/FR-021).
// (Named inherited 017 limitation: the ctor silently disables a sink whose open()
// fails post-preflight — research D-7; offered post-construct sink_error_counts_
// check, not required this step.)

}  // namespace fixpp::config
