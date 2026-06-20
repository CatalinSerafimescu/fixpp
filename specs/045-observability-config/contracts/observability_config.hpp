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
// The per-session logger override is written onto the EXISTING
// SessionConfig::logger_override member by the per-session resolver — no
// SessionConfig change.

// ── Resolver entry points (internal to fixpp_config_toml; illustrative) ──────
//
// Each mirrors the 044 resolver shape:
//   static void resolve_*(const toml::table& tbl, const std::filesystem::path& base_dir,
//                         const LoadOptions& opts, ConfigBundle& bundle,
//                         detail::DiagnosticAccumulator& acc);
// Contracts (see data-model.md E-3..E-5):
//
//   resolve_engine_logger  — [logger] composite: LoggerConfig scalars + non-empty
//                            [[logger.sinks]] array → make_shared<Logger>(cfg, sinks).
//                            Zero sinks → empty_required. capacity not pow2 → out_of_range.
//                            ring_resource NEVER file-set (deferred arena).
//
//   resolve_log_sink       — one [[logger.sinks]] entry → unique_ptr<Sink> via the
//                            kind's existing factory:
//                              file   → FileSinkFactory   / FileSinkConfig
//                              syslog → SyslogSinkFactory  / SyslogSinkConfig
//                                       (#ifdef FIXPP_HAS_SYSLOG; #else →
//                                        invalid_or_contradictory_selector)
//                                       facility: closed name→int map
//                              otlp   → OtlpLogSinkFactory / OtlpLogSinkConfig
//                                       (endpoint required; cert_source readable;
//                                        use_grpc=true → recognized_not_yet_supported_step2)
//                            unknown kind → unknown_enum {file,syslog,otlp}.
//
//   (per-session)          — [session].logger reuses resolve_engine_logger,
//                            writing config.logger_override.
//
// All resolvers append to the shared accumulator using the session-local
// acc.size() delta pattern (collect-ALL, D-7); a non-empty accumulator at
// end-of-pass ⇒ LoadResult holds the diagnostics vector, no bundle, nothing
// opened (FR-015/FR-021). Logger/Sink construction is side-effect-free (sinks
// open lazily at host-called open()), so a failed load leaves nothing opened.

}  // namespace fixpp::config
