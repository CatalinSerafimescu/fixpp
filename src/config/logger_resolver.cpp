// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/config/logger_resolver.cpp
// 045-observability-config Phase-3 (T010–T012) — logger resolver.
//
// T010: resolve_log_sink    — one [[logger.sinks]] entry → unique_ptr<Sink>
// T011: resolve_engine_logger — [logger] composite → PendingLogger (engine slot)
// T012: construct_loggers_if_clean — sole side-effectful step
//
// PRIVATE to the fixpp_config_toml target. Do NOT include from public headers.
//
// ODR (044 T039): uses the toml_include.hpp shim — NEVER #include <toml++/toml.hpp>
// directly from this TU.
//
// Anchors:
//   data-model.md E-3/E-4 (scalars + sink param maps)
//   research.md D-3/D-7 (sink resolver, deferred-construction protocol)
//   contracts/observability_config.hpp lines 77-116
//   tasks.md T010–T012

#include "logger_resolver.hpp"  // PendingLogger, PendingLoggerSet

#include <fixpp/config/config_bundle.hpp>
#include <fixpp/config/load_diagnostic.hpp>
#include <fixpp/config/toml_config_loader.hpp>
#include <fixpp/log/file_sink.hpp>
#include <fixpp/log/logger.hpp>
#include <fixpp/log/sink.hpp>

#include "loader_internal.hpp"  // DiagnosticAccumulator, trap_throw_to_expected
#include "mappers.hpp"          // map_syslog_facility, validate_pow2_capacity
#include "toml_include.hpp"     // ODR-safe shim (T039 carry-over)

// Unconditional include: syslog_sink.hpp self-#defines FIXPP_HAS_SYSLOG on a
// POSIX platform that has <syslog.h> (there is no CMake define). It MUST be
// included before any #ifdef FIXPP_HAS_SYSLOG test, or the macro is never seen
// and the syslog branch is dead on every build (matches scalar_mappers.cpp:27).
#include <fixpp/log/syslog_sink.hpp>

#ifdef FIXPP_CONFIG_HAS_OTLP
#include <fixpp/log/otlp_log_sink.hpp>
#endif

#ifndef _WIN32
// access(2) — POSIX file-accessibility check; not available on Windows/MSVC.
// Guard mirrors file_store_factory.cpp:46. The ::access(W_OK) call inside
// resolve_log_sink is also wrapped in the same #ifndef _WIN32 block.
#include <unistd.h>
#endif

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace fixpp::config::detail {

namespace {

// ---------------------------------------------------------------------------
// Local SourceLoc helper — extracts from a toml::node.
// ---------------------------------------------------------------------------

SourceLoc loc_node(const toml::node& n) noexcept {
    const auto& src = n.source().begin;
    return SourceLoc{
        .line = static_cast<std::uint32_t>(src.line),
        .col = static_cast<std::uint32_t>(src.column),
    };
}

// kp (dotted key-path builder) and parse_duration_to_ms (unit-suffix duration
// parser) are toml-free and shared from loader_internal (reached via
// mappers.hpp → loader_internal.hpp) so this TU and scalar_mappers.cpp cannot
// drift on the overflow guards. See loader_internal.hpp.

}  // anonymous namespace

// ---------------------------------------------------------------------------
// T010 — resolve_log_sink
//
// One [[logger.sinks]] entry → std::unique_ptr<Sink> (object-minting only;
// open() is NOT called — deferred to Logger construction, research D-7).
//
// `sink_tbl`  — the individual [[logger.sinks]] table node
// `sink_idx`  — 0-based index in the sinks array (for key-path names)
// `key_prefix`— outer logger key path (e.g. "logger")
// `base_dir`  — config-file parent directory (for relative path resolution)
// `resource`  — PMR resource for sink allocation (N-1 arena; factories ignore it)
// `acc`       — diagnostic accumulator
//
// Returns the minted Sink on success, or nullptr (with diagnostics) on any error.
// ---------------------------------------------------------------------------

std::unique_ptr<fixpp::log::Sink> resolve_log_sink(const toml::table& sink_tbl,
                                                   std::size_t sink_idx,
                                                   std::string_view key_prefix,
                                                   const std::filesystem::path& base_dir,
                                                   std::pmr::memory_resource* /*resource*/,
                                                   DiagnosticAccumulator& acc) {
    // Build key_path prefix for this sink entry.
    const std::string sink_kp =
        std::string{key_prefix} + ".sinks[" + std::to_string(sink_idx) + "]";

    // Gate B r1 #4 (collect-ALL): capture acc size before parsing this sink.
    // At the end of each sink branch, return nullptr iff new diagnostics were
    // added (acc.size() > sink_acc_before).  Early-return is kept ONLY for
    // fields where subsequent parsing genuinely depends on an earlier value
    // (e.g. kind is absent/empty → can't branch on sink type; endpoint absent
    // → cfg.endpoint is empty → later fields that embed it in error messages
    // would be misleading).  All INDEPENDENT field errors accumulate without
    // short-circuiting (FR-014 / FR-021 collect-ALL per key).
    const std::size_t sink_acc_before = acc.size();

    // ── kind is required ──────────────────────────────────────────────────────
    const toml::node* kind_node = sink_tbl.get("kind");
    if (!kind_node || !kind_node->is_string()) {
        acc.add(LoadDiagnostic{
            .key_path = kp(sink_kp, "kind"),
            .reason = reason_class::missing_required,
            .location = kind_node ? loc_node(*kind_node) : SourceLoc{},
            .message = "logger sink must have a \"kind\" string field",
        });
        return nullptr;
    }
    const std::string_view kind = kind_node->as_string()->get();
    if (kind.empty()) {
        acc.add(LoadDiagnostic{
            .key_path = kp(sink_kp, "kind"),
            .reason = reason_class::empty_required,
            .location = loc_node(*kind_node),
            .message = "logger sink kind must not be empty",
        });
        return nullptr;
    }

    // ── file sink ─────────────────────────────────────────────────────────────
    if (kind == "file") {
        fixpp::log::FileSinkConfig cfg;

        if (const auto* n = sink_tbl.get("directory"); n && n->is_string()) {
            std::filesystem::path dir{n->as_string()->get()};
            if (dir.is_relative()) dir = base_dir / dir;
            cfg.directory = std::move(dir);
        } else if (const auto* n = sink_tbl.get("directory"); n && !n->is_string()) {
            // Gate B r2 #1: present but wrong type → malformed_value (fail-closed).
            // Without this, directory=123 falls through (neither string nor absent),
            // leaves cfg.directory at the default ".", and the preflight below would
            // run against "." — silently accepting a wrong-type explicit selector
            // as CWD/default (FR-020 fail-closed / FR-018 path semantics).
            acc.add(LoadDiagnostic{
                .key_path = kp(sink_kp, "directory"),
                .reason = reason_class::malformed_value,
                .location = loc_node(*n),
                .message = "directory must be a string path",
            });
        } else if (!sink_tbl.get("directory")) {
            // Gate B r1 #3a: default directory (".") must also be resolved against
            // base_dir (FR-018 / data-model E-4).  Without this, the default stays
            // CWD-relative while the spec says relative paths — including the default
            // — resolve against the config-file directory.  Preflight for the default
            // is deliberately skipped (#3b waived; no mkdir, no stat here).
            if (cfg.directory.is_relative()) cfg.directory = base_dir / cfg.directory;
        }
        if (const auto* n = sink_tbl.get("base_name"); n && n->is_string()) {
            cfg.base_name = std::string{n->as_string()->get()};
        } else if (const auto* n = sink_tbl.get("base_name"); n && !n->is_string()) {
            // Gate B r1 #6: present but wrong type → malformed_value (fail-closed).
            acc.add(LoadDiagnostic{
                .key_path = kp(sink_kp, "base_name"),
                .reason = reason_class::malformed_value,
                .location = loc_node(*n),
                .message = "base_name must be a string",
            });
        }
        if (const auto* n = sink_tbl.get("max_file_bytes"); n && n->is_integer()) {
            const auto v = n->as_integer()->get();
            // Gate B r1 #5: v<0 must be rejected; v==0 is also rejected (>0 required).
            if (v <= 0) {
                acc.add(LoadDiagnostic{
                    .key_path = kp(sink_kp, "max_file_bytes"),
                    .reason = reason_class::out_of_range,
                    .location = loc_node(*n),
                    .message = "max_file_bytes must be > 0",
                });
            } else {
                cfg.max_file_bytes = static_cast<std::uint64_t>(v);
            }
        }
        if (const auto* n = sink_tbl.get("max_keep_count"); n && n->is_integer()) {
            const auto v = n->as_integer()->get();
            // Gate B r1 #5: negative value wraps to UINT32_MAX; reject with out_of_range.
            if (v < 0 || v > static_cast<long long>(std::numeric_limits<std::uint32_t>::max())) {
                acc.add(LoadDiagnostic{
                    .key_path = kp(sink_kp, "max_keep_count"),
                    .reason = reason_class::out_of_range,
                    .location = loc_node(*n),
                    .message = "max_keep_count must be a non-negative uint32",
                });
            } else {
                cfg.max_keep_count = static_cast<std::uint32_t>(v);
            }
        }
        if (const auto* n = sink_tbl.get("async_fsync"); n && n->is_boolean()) {
            cfg.async_fsync = n->as_boolean()->get();
        } else if (const auto* n = sink_tbl.get("async_fsync"); n && !n->is_boolean()) {
            // Gate B r1 #6: present but wrong type → malformed_value (fail-closed).
            acc.add(LoadDiagnostic{
                .key_path = kp(sink_kp, "async_fsync"),
                .reason = reason_class::malformed_value,
                .location = loc_node(*n),
                .message = "async_fsync must be a boolean (true or false)",
            });
        }

        // T016 — preflight: configured directory must already exist + be writable.
        // (stat/access ONLY — no mkdir, no probe file; research D-4/D-7)
        // Only validate when directory was explicitly set (default "." is not
        // validated here; operators who don't configure a directory get runtime errors).
        // Gate B r1 #4: do not early-return on directory errors; accumulate and let the
        // delta check below produce the final nullptr (collect-ALL within this sink).
        // Gate B r2 #1: only preflight a validly-typed EXPLICIT directory — a present
        // wrong-type node (handled above with malformed_value) must NOT be preflighted
        // against the bogus default ".".
        if (const auto* dn = sink_tbl.get("directory"); dn && dn->is_string()) {
            const std::filesystem::path& dir = cfg.directory;
            std::error_code fs_ec;
            if (!std::filesystem::is_directory(dir, fs_ec)) {
                acc.add(LoadDiagnostic{
                    .key_path = kp(sink_kp, "directory"),
                    .reason = reason_class::invalid_or_contradictory_selector,
                    .location = loc_node(*sink_tbl.get("directory")),
                    .message = "file sink directory does not exist or is not a directory: \"" +
                               dir.string() + "\"",
                });
                // Do NOT access(W_OK) if the path isn't a directory at all.
            } else {
                // POSIX access(2) W_OK: readable + writable by current process.
#ifndef _WIN32
                if (::access(dir.c_str(), W_OK) != 0) {
                    acc.add(LoadDiagnostic{
                        .key_path = kp(sink_kp, "directory"),
                        .reason = reason_class::invalid_or_contradictory_selector,
                        .location = loc_node(*sink_tbl.get("directory")),
                        .message = "file sink directory is not writable: \"" + dir.string() + "\"",
                    });
                }
#endif
            }
        }

        // Gate B r1 #4: return nullptr iff this sink added new diagnostics.
        if (acc.size() > sink_acc_before) return nullptr;

        fixpp::log::FileSinkFactory factory;
        return factory.make(nullptr, cfg);
    }

    // ── syslog sink ───────────────────────────────────────────────────────────
#ifdef FIXPP_HAS_SYSLOG
    if (kind == "syslog") {
        fixpp::log::SyslogSinkConfig cfg;

        if (const auto* n = sink_tbl.get("ident"); n && n->is_string()) {
            cfg.ident = std::string{n->as_string()->get()};
        }
        if (const auto* n = sink_tbl.get("facility"); n && n->is_string()) {
            const std::string_view fac_name = n->as_string()->get();
            int fac_val = 0;
            if (map_syslog_facility(fac_name, fac_val, kp(sink_kp, "facility"), loc_node(*n),
                                    acc)) {
                cfg.facility = fac_val;
            }
            // !map_syslog_facility: appended a diagnostic; fall through to delta check.
        } else if (const auto* n = sink_tbl.get("facility"); n && !n->is_string()) {
            // Gate B r1 #6: facility present but wrong type → malformed_value (fail-closed).
            // Gate B r1 #4: do not early-return (collect-ALL pattern).
            acc.add(LoadDiagnostic{
                .key_path = kp(sink_kp, "facility"),
                .reason = reason_class::malformed_value,
                .location = loc_node(*n),
                .message = "syslog facility must be a string (e.g. \"user\", \"daemon\", \"local0\")",
            });
        }

        // Gate B r1 #4: return nullptr iff this sink added new diagnostics.
        if (acc.size() > sink_acc_before) return nullptr;

        fixpp::log::SyslogSinkFactory factory;
        return factory.make(nullptr, cfg);
    }
#else
    if (kind == "syslog") {
        acc.add(LoadDiagnostic{
            .key_path = kp(sink_kp, "kind"),
            .reason = reason_class::invalid_or_contradictory_selector,
            .location = loc_node(*kind_node),
            .message = "syslog sink is not available on this build (FIXPP_HAS_SYSLOG not defined)",
        });
        return nullptr;
    }
#endif  // FIXPP_HAS_SYSLOG

    // ── OTLP log sink ─────────────────────────────────────────────────────────
#ifdef FIXPP_CONFIG_HAS_OTLP
    if (kind == "otlp") {
        fixpp::log::OtlpLogSinkConfig cfg;

        // endpoint — required
        const toml::node* ep_node = sink_tbl.get("endpoint");
        if (!ep_node || !ep_node->is_string()) {
            acc.add(LoadDiagnostic{
                .key_path = kp(sink_kp, "endpoint"),
                .reason = reason_class::missing_required,
                .location = ep_node ? loc_node(*ep_node) : SourceLoc{},
                .message = "otlp sink requires an \"endpoint\" string field",
            });
            return nullptr;
        }
        cfg.endpoint = std::string{ep_node->as_string()->get()};
        if (cfg.endpoint.empty()) {
            acc.add(LoadDiagnostic{
                .key_path = kp(sink_kp, "endpoint"),
                .reason = reason_class::empty_required,
                .location = loc_node(*ep_node),
                .message = "otlp sink endpoint must not be empty",
            });
            return nullptr;
        }

        // use_grpc — deferred (recognized_not_yet_supported_step2)
        // Gate B r1 #4: do NOT early-return; let the delta check below handle it
        // so subsequent independent fields are also validated (collect-ALL).
        if (const auto* n = sink_tbl.get("use_grpc"); n && n->is_boolean()) {
            if (n->as_boolean()->get()) {
                acc.add(LoadDiagnostic{
                    .key_path = kp(sink_kp, "use_grpc"),
                    .reason = reason_class::recognized_not_yet_supported_step2,
                    .location = loc_node(*n),
                    .message =
                        "use_grpc=true is not yet supported (gRPC OTLP transport is deferred)",
                });
                // Do not early-return; fall through to collect other independent errors.
            }
        }

        // cert_source — optional, relative→base_dir
        // T016 preflight: must be readable AND PEM-magic-validated (research D-4).
        // Gate B r1 #4: preflight errors append diagnostics but do NOT early-return
        // (subsequent fields like export_timeout are independent; collect-ALL).
        if (const auto* n = sink_tbl.get("cert_source"); n && n->is_string()) {
            std::string_view cs_val = n->as_string()->get();
            if (!cs_val.empty()) {
                std::filesystem::path cs_path{cs_val};
                if (cs_path.is_relative()) cs_path = base_dir / cs_path;
                cfg.cert_source = cs_path.string();

                // Preflight: readable check.
                std::ifstream cert_f(cs_path, std::ios::binary);
                if (!cert_f.is_open()) {
                    acc.add(LoadDiagnostic{
                        .key_path = kp(sink_kp, "cert_source"),
                        .reason = reason_class::invalid_or_contradictory_selector,
                        .location = loc_node(*n),
                        .message = "otlp cert_source file is not readable (endpoint: " +
                                   redact_url_userinfo(cfg.endpoint) + "): \"" + cs_path.string() +
                                   "\"",
                    });
                    // cert_f not open → cannot do PEM check; skip.
                } else {
                    // Preflight: PEM-magic check — first non-whitespace bytes must be "-----BEGIN".
                    // Tolerate leading whitespace/BOM so we don't reject real PEM files.
                    constexpr std::string_view kPemMagic = "-----BEGIN";
                    std::string header;
                    header.resize(kPemMagic.size() + 8);  // read ahead a few extra bytes
                    cert_f.read(header.data(), static_cast<std::streamsize>(header.size()));
                    const std::size_t n_read = static_cast<std::size_t>(cert_f.gcount());
                    header.resize(n_read);
                    // Strip leading whitespace before checking magic.
                    const auto first_nonws = header.find_first_not_of(" \t\r\n\xEF\xBB\xBF");
                    const bool has_pem_magic =
                        (first_nonws != std::string::npos) &&
                        (header.size() - first_nonws >= kPemMagic.size()) &&
                        (std::string_view{header}.substr(first_nonws, kPemMagic.size()) == kPemMagic);
                    if (!has_pem_magic) {
                        acc.add(LoadDiagnostic{
                            .key_path = kp(sink_kp, "cert_source"),
                            .reason = reason_class::invalid_or_contradictory_selector,
                            .location = loc_node(*n),
                            .message = "otlp cert_source does not appear to be a PEM file "
                                       "(missing \"-----BEGIN\" header; endpoint: " +
                                       redact_url_userinfo(cfg.endpoint) + "): \"" +
                                       cs_path.string() + "\"",
                        });
                    }
                }
            }
        } else if (const auto* n = sink_tbl.get("cert_source"); n && !n->is_string()) {
            // Gate B r1 #6 CRITICAL: cert_source present but wrong type → malformed_value.
            // Without this: cfg.cert_source stays "" → OTLP uses plain HTTP instead of
            // TLS (silent fail-open security downgrade, data-model E-4).
            acc.add(LoadDiagnostic{
                .key_path = kp(sink_kp, "cert_source"),
                .reason = reason_class::malformed_value,
                .location = loc_node(*n),
                .message = "cert_source must be a string path to a PEM certificate file",
            });
        }

        // export_timeout — duration string (044 unit-suffix rule)
        // Gate B r1 #4: !d.ok appends a diagnostic already; do not early-return.
        if (const auto* n = sink_tbl.get("export_timeout"); n && n->is_string()) {
            const auto d = parse_duration_to_ms(n->as_string()->get(),
                                                kp(sink_kp, "export_timeout"), acc, loc_node(*n));
            if (d.ok) {
                // OtlpLogSinkConfig::export_timeout is chrono::seconds; duration_cast from ms.
                cfg.export_timeout = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::milliseconds{d.value_ms});
            }
            // !d.ok: parse_duration_to_ms already appended a diagnostic; fall through.
        } else if (const auto* n = sink_tbl.get("export_timeout"); n && !n->is_string()) {
            // Gate B r1 #6: present but wrong type → malformed_value (fail-closed).
            acc.add(LoadDiagnostic{
                .key_path = kp(sink_kp, "export_timeout"),
                .reason = reason_class::malformed_value,
                .location = loc_node(*n),
                .message = "export_timeout must be a duration string (e.g. \"10s\", \"500ms\")",
            });
        }

        // max_export_batch — size_t, 0 → out_of_range; <0 → wraps to SIZE_MAX (out_of_range)
        // Gate B r1 #4+#5: do NOT early-return; fall through to delta check below.
        if (const auto* n = sink_tbl.get("max_export_batch"); n && n->is_integer()) {
            const auto v = n->as_integer()->get();
            if (v <= 0) {
                acc.add(LoadDiagnostic{
                    .key_path = kp(sink_kp, "max_export_batch"),
                    .reason = reason_class::out_of_range,
                    .location = loc_node(*n),
                    .message = "max_export_batch must be > 0",
                });
            } else {
                cfg.max_export_batch = static_cast<std::size_t>(v);
            }
        }

        // max_export_retries — size_t; <0 → wraps to SIZE_MAX (out_of_range)
        // Gate B r1 #5: guard on v<0.
        if (const auto* n = sink_tbl.get("max_export_retries"); n && n->is_integer()) {
            const auto v = n->as_integer()->get();
            if (v < 0) {
                acc.add(LoadDiagnostic{
                    .key_path = kp(sink_kp, "max_export_retries"),
                    .reason = reason_class::out_of_range,
                    .location = loc_node(*n),
                    .message = "max_export_retries must be >= 0",
                });
            } else {
                cfg.max_export_retries = static_cast<std::size_t>(v);
            }
        }

        // Gate B r1 #4: return nullptr iff this sink added new diagnostics.
        if (acc.size() > sink_acc_before) return nullptr;

        fixpp::log::OtlpLogSinkFactory factory;
        return factory.make(nullptr, cfg);
    }
#else
    if (kind == "otlp") {
        acc.add(LoadDiagnostic{
            .key_path = kp(sink_kp, "kind"),
            .reason = reason_class::invalid_or_contradictory_selector,
            .location = loc_node(*kind_node),
            .message = "otlp log sink is not available on this build "
                       "(FIXPP_CONFIG_HAS_OTLP not defined — "
                       "OpenTelemetry SDK required)",
        });
        return nullptr;
    }
#endif  // FIXPP_CONFIG_HAS_OTLP

    // ── Unknown kind ──────────────────────────────────────────────────────────
    acc.add(LoadDiagnostic{
        .key_path = kp(sink_kp, "kind"),
        .reason = reason_class::unknown_enum,
        .location = loc_node(*kind_node),
        .message = std::string{"unknown logger sink kind: \""} + std::string{kind} +
                   R"(" (valid values: "file", "syslog", "otlp"))",
    });
    return nullptr;
}

// ---------------------------------------------------------------------------
// T011 — resolve_engine_logger
//
// Resolves the [logger] composite from `logger_tbl` into a PendingLogger
// parked in pending.engine (engine slot). Does NOT construct the live Logger.
//
// `logger_tbl` — the [logger] TOML table
// `key_prefix` — key path for this logger (e.g. "logger" at root or
//                "session[i].logger" for per-session)
// `loc`        — SourceLoc of the [logger] table header
// `base_dir`   — config-file parent directory
// `opts`       — LoadOptions (opts.resource = N-1 PMR arena)
// `pending`    — PendingLoggerSet to receive the resolved PendingLogger
// `acc`        — diagnostic accumulator
// `session_index` — for session-keyed loggers (0 = engine slot)
// `is_engine`     — true → emit into pending.engine; false → pending.sessions
// ---------------------------------------------------------------------------

void resolve_engine_logger(const toml::table& logger_tbl, std::string_view key_prefix,
                           SourceLoc loc, const std::filesystem::path& base_dir,
                           const LoadOptions& opts, PendingLoggerSet& pending,
                           DiagnosticAccumulator& acc, bool is_engine, std::size_t session_index) {
    const std::size_t acc_before = acc.size();

    // ── LoggerConfig scalars ──────────────────────────────────────────────────

    fixpp::log::LoggerConfig cfg;  // defaults from E-3 (capacity=65536, drop_newest, 5000ms, -1)

    // capacity — uint32, power of 2
    if (const auto* n = logger_tbl.get("capacity"); n && n->is_integer()) {
        const auto v = n->as_integer()->get();
        if (v < 0 || v > static_cast<long long>(std::numeric_limits<std::uint32_t>::max())) {
            acc.add(LoadDiagnostic{
                .key_path = kp(key_prefix, "capacity"),
                .reason = reason_class::out_of_range,
                .location = loc_node(*n),
                .message = "capacity must be a non-zero power-of-2 uint32",
            });
        } else {
            const auto u = static_cast<std::uint32_t>(v);
            if (validate_pow2_capacity(u, kp(key_prefix, "capacity"), loc_node(*n), acc)) {
                cfg.capacity = u;
            }
        }
    }

    // on_overflow enum: "drop_newest" | "block"
    if (const auto* n = logger_tbl.get("on_overflow"); n && n->is_string()) {
        const std::string_view tok = n->as_string()->get();
        if (tok == "drop_newest") {
            cfg.on_overflow = fixpp::log::overflow_policy::drop_newest;
        } else if (tok == "block") {
            cfg.on_overflow = fixpp::log::overflow_policy::block;
        } else {
            acc.add(LoadDiagnostic{
                .key_path = kp(key_prefix, "on_overflow"),
                .reason = reason_class::unknown_enum,
                .location = loc_node(*n),
                .message = std::string{"unknown on_overflow token: \""} + std::string{tok} +
                           R"(" (valid values: "drop_newest", "block"))",
            });
        }
    } else if (const auto* n = logger_tbl.get("on_overflow"); n && !n->is_string()) {
        // Gate B r1 #6: present but wrong type → malformed_value (fail-closed).
        acc.add(LoadDiagnostic{
            .key_path = kp(key_prefix, "on_overflow"),
            .reason = reason_class::malformed_value,
            .location = loc_node(*n),
            .message = "on_overflow must be a string (\"drop_newest\" or \"block\")",
        });
    }

    // drain_timeout — duration string (ms); default 5000ms
    if (const auto* n = logger_tbl.get("drain_timeout"); n && n->is_string()) {
        const auto d = parse_duration_to_ms(n->as_string()->get(), kp(key_prefix, "drain_timeout"),
                                            acc, loc_node(*n));
        if (d.ok) {
            cfg.drain_timeout = std::chrono::milliseconds{d.value_ms};
        }
    }

    // drain_cpu_affinity — plain int, optional; default -1
    // Gate B r1 #5: int narrowing from int64 is implementation-defined for values
    // outside [INT_MIN, INT_MAX].  Guard with explicit range check (mirrors capacity).
    if (const auto* n = logger_tbl.get("drain_cpu_affinity"); n && n->is_integer()) {
        const auto v = n->as_integer()->get();
        if (v < static_cast<long long>(std::numeric_limits<int>::min()) ||
            v > static_cast<long long>(std::numeric_limits<int>::max())) {
            acc.add(LoadDiagnostic{
                .key_path = kp(key_prefix, "drain_cpu_affinity"),
                .reason = reason_class::out_of_range,
                .location = loc_node(*n),
                .message = "drain_cpu_affinity must fit in a signed 32-bit integer",
            });
        } else {
            cfg.drain_cpu_affinity = static_cast<int>(v);
        }
    }

    // ring_resource is NEVER file-set (deferred arena, FR-010) — stays default.

    // ── [[logger.sinks]] — required, ordered, non-empty ──────────────────────

    const std::string sinks_kp = kp(key_prefix, "sinks");

    const toml::node* sinks_node = logger_tbl.get("sinks");
    if (!sinks_node || !sinks_node->is_array()) {
        acc.add(LoadDiagnostic{
            .key_path = sinks_kp,
            .reason = sinks_node ? reason_class::malformed_value : reason_class::missing_required,
            .location = sinks_node ? loc_node(*sinks_node) : loc,
            .message = sinks_node ? "logger.sinks must be an array of tables ([[logger.sinks]])"
                                  : "logger must have at least one [[logger.sinks]] entry",
        });
        return;
    }

    const toml::array& sinks_arr = *sinks_node->as_array();
    if (sinks_arr.empty()) {
        acc.add(LoadDiagnostic{
            .key_path = sinks_kp,
            .reason = reason_class::empty_required,
            .location = loc_node(*sinks_node),
            .message = "logger.sinks must not be empty; at least one sink is required",
        });
        return;
    }

    // Mint each sink (side-effect-free — open() deferred to Logger construction).
    std::pmr::vector<std::unique_ptr<fixpp::log::Sink>> sinks{opts.resource};
    sinks.reserve(sinks_arr.size());

    std::size_t sink_idx = 0;
    for (const auto& sink_elem : sinks_arr) {
        if (!sink_elem.is_table()) {
            acc.add(LoadDiagnostic{
                .key_path = sinks_kp + "[" + std::to_string(sink_idx) + "]",
                .reason = reason_class::malformed_value,
                .location = loc_node(sink_elem),
                .message = "each [[logger.sinks]] entry must be a TOML table",
            });
            ++sink_idx;
            continue;
        }
        auto sink_ptr = resolve_log_sink(*sink_elem.as_table(), sink_idx, key_prefix, base_dir,
                                         opts.resource, acc);
        if (sink_ptr) {
            sinks.push_back(std::move(sink_ptr));
        }
        // If resolve_log_sink returned nullptr it already added a diagnostic; collect-ALL
        // continues.
        ++sink_idx;
    }

    // If any error was accumulated, stop — do not park a partially-valid pending logger.
    if (acc.size() > acc_before) {
        return;
    }

    // Park into the pending set (N-2: file-scoped, NOT constructed here).
    PendingLogger pl{
        .cfg = cfg,
        .sinks = std::move(sinks),
        .key_path = std::string{key_prefix},
        .loc = loc,
        .session_index = session_index,
    };

    if (is_engine) {
        pending.engine = std::move(pl);
    } else {
        pending.sessions.push_back(std::move(pl));
    }
}

// ---------------------------------------------------------------------------
// T012 — construct_loggers_if_clean
//
// The SOLE side-effectful step (research D-7 / FR-015).
// Called only AFTER the whole-file accumulator is checked to be empty.
// Moves each PendingLogger into std::make_shared<Logger>(...) (which opens
// every sink and spawns the drain thread) and assigns to its destination.
// A non-empty accumulator → no Logger constructed, nothing opened.
// ---------------------------------------------------------------------------

void construct_loggers_if_clean(PendingLoggerSet&& pending, ConfigBundle& bundle,
                                DiagnosticAccumulator& acc) {
    // The whole-file accumulator must be empty (FR-015 / research D-7).
    if (!acc.empty()) {
        // Non-empty accumulator: no Logger constructed.
        return;
    }

    // Engine slot
    if (pending.engine.has_value()) {
        auto& pl = *pending.engine;
        bundle.engine.logger =
            std::make_shared<fixpp::log::Logger>(pl.cfg, std::move(pl.sinks));
    }

    // Per-session slots (US3/T019 — wired in Phase 5)
    for (auto& pl : pending.sessions) {
        const std::size_t i = pl.session_index;
        if (i < bundle.sessions.size()) {
            bundle.sessions[i].config.logger_override =
                std::make_shared<fixpp::log::Logger>(pl.cfg, std::move(pl.sinks));
        }
    }
}

}  // namespace fixpp::config::detail
