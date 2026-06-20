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

#include "toml_include.hpp"  // ODR-safe shim (T039 carry-over)

#include "loader_internal.hpp"  // DiagnosticAccumulator, trap_throw_to_expected
#include "logger_resolver.hpp"  // PendingLogger, PendingLoggerSet
#include "mappers.hpp"           // map_syslog_facility, validate_pow2_capacity

#include <fixpp/config/config_bundle.hpp>
#include <fixpp/config/load_diagnostic.hpp>
#include <fixpp/config/toml_config_loader.hpp>
#include <fixpp/log/file_sink.hpp>
#include <fixpp/log/logger.hpp>
#include <fixpp/log/sink.hpp>

#ifdef FIXPP_HAS_SYSLOG
#include <fixpp/log/syslog_sink.hpp>
#endif

#ifdef FIXPP_CONFIG_HAS_OTLP
#include <fixpp/log/otlp_log_sink.hpp>
#endif

#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
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
        .col  = static_cast<std::uint32_t>(src.column),
    };
}

// Helper: build "prefix.key" key path string.
std::string kpath(std::string_view prefix, std::string_view key) {
    std::string s;
    s.reserve(prefix.size() + 1 + key.size());
    s += prefix;
    s += '.';
    s += key;
    return s;
}

// ---------------------------------------------------------------------------
// parse_duration_to_ms_local — reuses the 044 unit-suffix duration rule.
// Returns {value_ms, ok}. Errors accumulated into acc.
// The 044 canonical parser lives in scalar_mappers.cpp (anonymous ns) so we
// replicate the logic here for this TU, matching it exactly.
//
// REFACTOR DEBT (T035-Polish): centralise into loader_internal.hpp (toml-free;
// takes only std::string_view + DiagnosticAccumulator) so scalar_mappers.cpp
// and logger_resolver.cpp share one definition and cannot drift.
// ---------------------------------------------------------------------------

struct ParsedDuration { long long value_ms; bool ok; };

ParsedDuration parse_dur(std::string_view tok, const std::string& key_path,
                         DiagnosticAccumulator& acc, SourceLoc loc) {
    if (tok.empty()) {
        acc.add(LoadDiagnostic{
            .key_path = key_path,
            .reason   = reason_class::malformed_value,
            .location = loc,
            .message  = "empty duration string",
        });
        return {0, false};
    }
    std::size_t num_end = 0;
    while (num_end < tok.size() && tok[num_end] >= '0' && tok[num_end] <= '9') ++num_end;
    if (num_end == 0) {
        acc.add(LoadDiagnostic{
            .key_path = key_path,
            .reason   = reason_class::malformed_value,
            .location = loc,
            .message  = "duration must start with a numeric value",
        });
        return {0, false};
    }
    long long num = 0;
    {
        auto [ptr, ec] = std::from_chars(tok.data(), tok.data() + num_end, num);
        if (ec != std::errc{}) {
            acc.add(LoadDiagnostic{
                .key_path = key_path,
                .reason   = reason_class::malformed_value,
                .location = loc,
                .message  = "duration numeric part could not be parsed",
            });
            return {0, false};
        }
    }
    std::string_view unit = tok.substr(num_end);
    if (unit.empty()) {
        acc.add(LoadDiagnostic{
            .key_path = key_path,
            .reason   = reason_class::malformed_value,
            .location = loc,
            .message  = R"(duration requires an explicit unit suffix (e.g. "30s", "500ms"))",
        });
        return {0, false};
    }
    long long ms = 0;
    if (unit == "ms") {
        ms = num;
    } else if (unit == "s") {
        constexpr long long kMax = (std::numeric_limits<long long>::max)() / 1000LL;
        if (num > kMax) {
            acc.add(LoadDiagnostic{.key_path = key_path, .reason = reason_class::out_of_range,
                                   .location = loc, .message = "duration value overflows (s scale)"});
            return {0, false};
        }
        ms = num * 1000LL;
    } else if (unit == "m") {
        constexpr long long kMax = (std::numeric_limits<long long>::max)() / 60000LL;
        if (num > kMax) {
            acc.add(LoadDiagnostic{.key_path = key_path, .reason = reason_class::out_of_range,
                                   .location = loc, .message = "duration value overflows (m scale)"});
            return {0, false};
        }
        ms = num * 60000LL;
    } else if (unit == "h") {
        constexpr long long kMax = (std::numeric_limits<long long>::max)() / 3600000LL;
        if (num > kMax) {
            acc.add(LoadDiagnostic{.key_path = key_path, .reason = reason_class::out_of_range,
                                   .location = loc, .message = "duration value overflows (h scale)"});
            return {0, false};
        }
        ms = num * 3600000LL;
    } else if (unit == "us") {
        acc.add(LoadDiagnostic{
            .key_path = key_path,
            .reason   = reason_class::out_of_range,
            .location = loc,
            .message  = "sub-millisecond unit \"us\" not supported; use \"ms\", \"s\", \"m\", or \"h\"",
        });
        return {0, false};
    } else {
        acc.add(LoadDiagnostic{
            .key_path = key_path,
            .reason   = reason_class::malformed_value,
            .location = loc,
            .message  = std::string{"unrecognised duration unit: \""} + std::string{unit} + "\"",
        });
        return {0, false};
    }
    return {ms, true};
}

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

std::unique_ptr<fixpp::log::Sink> resolve_log_sink(
    const toml::table& sink_tbl,
    std::size_t         sink_idx,
    std::string_view    key_prefix,
    const std::filesystem::path& base_dir,
    std::pmr::memory_resource*   /*resource*/,
    DiagnosticAccumulator&       acc)
{
    // Build key_path prefix for this sink entry.
    const std::string sink_kp = std::string{key_prefix} + ".sinks[" +
                                 std::to_string(sink_idx) + "]";

    // ── kind is required ──────────────────────────────────────────────────────
    const toml::node* kind_node = sink_tbl.get("kind");
    if (!kind_node || !kind_node->is_string()) {
        acc.add(LoadDiagnostic{
            .key_path = kpath(sink_kp, "kind"),
            .reason   = reason_class::missing_required,
            .location = kind_node ? loc_node(*kind_node) : SourceLoc{},
            .message  = "logger sink must have a \"kind\" string field",
        });
        return nullptr;
    }
    const std::string_view kind = kind_node->as_string()->get();
    if (kind.empty()) {
        acc.add(LoadDiagnostic{
            .key_path = kpath(sink_kp, "kind"),
            .reason   = reason_class::empty_required,
            .location = loc_node(*kind_node),
            .message  = "logger sink kind must not be empty",
        });
        return nullptr;
    }

    // ── file sink ─────────────────────────────────────────────────────────────
    if (kind == "file") {
        fixpp::log::FileSinkConfig cfg;

        if (const auto* n = sink_tbl.get("directory"); n && n->is_string()) {
            std::filesystem::path dir{std::string{n->as_string()->get()}};
            if (dir.is_relative()) dir = base_dir / dir;
            cfg.directory = std::move(dir);
        }
        if (const auto* n = sink_tbl.get("base_name"); n && n->is_string()) {
            cfg.base_name = std::string{n->as_string()->get()};
        }
        if (const auto* n = sink_tbl.get("max_file_bytes"); n && n->is_integer()) {
            const auto v = n->as_integer()->get();
            if (v == 0) {
                acc.add(LoadDiagnostic{
                    .key_path = kpath(sink_kp, "max_file_bytes"),
                    .reason   = reason_class::out_of_range,
                    .location = loc_node(*n),
                    .message  = "max_file_bytes must be > 0",
                });
                return nullptr;
            }
            cfg.max_file_bytes = static_cast<std::uint64_t>(v);
        }
        if (const auto* n = sink_tbl.get("max_keep_count"); n && n->is_integer()) {
            cfg.max_keep_count = static_cast<std::uint32_t>(n->as_integer()->get());
        }
        if (const auto* n = sink_tbl.get("async_fsync"); n && n->is_boolean()) {
            cfg.async_fsync = n->as_boolean()->get();
        }

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
            if (!map_syslog_facility(fac_name, fac_val,
                                     kpath(sink_kp, "facility"), loc_node(*n), acc)) {
                return nullptr;
            }
            cfg.facility = fac_val;
        }

        fixpp::log::SyslogSinkFactory factory;
        return factory.make(nullptr, cfg);
    }
#else
    if (kind == "syslog") {
        acc.add(LoadDiagnostic{
            .key_path = kpath(sink_kp, "kind"),
            .reason   = reason_class::invalid_or_contradictory_selector,
            .location = loc_node(*kind_node),
            .message  = "syslog sink is not available on this build (FIXPP_HAS_SYSLOG not defined)",
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
                .key_path = kpath(sink_kp, "endpoint"),
                .reason   = reason_class::missing_required,
                .location = ep_node ? loc_node(*ep_node) : SourceLoc{},
                .message  = "otlp sink requires an \"endpoint\" string field",
            });
            return nullptr;
        }
        cfg.endpoint = std::string{ep_node->as_string()->get()};
        if (cfg.endpoint.empty()) {
            acc.add(LoadDiagnostic{
                .key_path = kpath(sink_kp, "endpoint"),
                .reason   = reason_class::empty_required,
                .location = loc_node(*ep_node),
                .message  = "otlp sink endpoint must not be empty",
            });
            return nullptr;
        }

        // use_grpc — deferred (recognized_not_yet_supported_step2)
        if (const auto* n = sink_tbl.get("use_grpc"); n && n->is_boolean()) {
            if (n->as_boolean()->get()) {
                acc.add(LoadDiagnostic{
                    .key_path = kpath(sink_kp, "use_grpc"),
                    .reason   = reason_class::recognized_not_yet_supported_step2,
                    .location = loc_node(*n),
                    .message  = "use_grpc=true is not yet supported (gRPC OTLP transport is deferred)",
                });
                return nullptr;
            }
        }

        // cert_source — optional, relative→base_dir (load-time PEM check is T016/Phase 4)
        if (const auto* n = sink_tbl.get("cert_source"); n && n->is_string()) {
            std::string_view cs_val = n->as_string()->get();
            if (!cs_val.empty()) {
                std::filesystem::path cs_path{std::string{cs_val}};
                if (cs_path.is_relative()) cs_path = base_dir / cs_path;
                cfg.cert_source = cs_path.string();
            }
        }

        // export_timeout — duration string (044 unit-suffix rule)
        if (const auto* n = sink_tbl.get("export_timeout"); n && n->is_string()) {
            const auto d = parse_dur(n->as_string()->get(),
                                     kpath(sink_kp, "export_timeout"), acc, loc_node(*n));
            if (!d.ok) return nullptr;
            // OtlpLogSinkConfig::export_timeout is chrono::seconds; duration_cast from ms.
            cfg.export_timeout = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::milliseconds{d.value_ms});
        }

        // max_export_batch — size_t, 0 → out_of_range
        if (const auto* n = sink_tbl.get("max_export_batch"); n && n->is_integer()) {
            const auto v = n->as_integer()->get();
            if (v == 0) {
                acc.add(LoadDiagnostic{
                    .key_path = kpath(sink_kp, "max_export_batch"),
                    .reason   = reason_class::out_of_range,
                    .location = loc_node(*n),
                    .message  = "max_export_batch must be > 0",
                });
                return nullptr;
            }
            cfg.max_export_batch = static_cast<std::size_t>(v);
        }

        // max_export_retries
        if (const auto* n = sink_tbl.get("max_export_retries"); n && n->is_integer()) {
            cfg.max_export_retries = static_cast<std::size_t>(n->as_integer()->get());
        }

        fixpp::log::OtlpLogSinkFactory factory;
        return factory.make(nullptr, cfg);
    }
#else
    if (kind == "otlp") {
        acc.add(LoadDiagnostic{
            .key_path = kpath(sink_kp, "kind"),
            .reason   = reason_class::invalid_or_contradictory_selector,
            .location = loc_node(*kind_node),
            .message  = "otlp log sink is not available on this build "
                        "(FIXPP_CONFIG_HAS_OTLP not defined — "
                        "OpenTelemetry SDK required)",
        });
        return nullptr;
    }
#endif  // FIXPP_CONFIG_HAS_OTLP

    // ── Unknown kind ──────────────────────────────────────────────────────────
    acc.add(LoadDiagnostic{
        .key_path = kpath(sink_kp, "kind"),
        .reason   = reason_class::unknown_enum,
        .location = loc_node(*kind_node),
        .message  = std::string{"unknown logger sink kind: \""} + std::string{kind} +
                    "\" (valid values: \"file\", \"syslog\", \"otlp\")",
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

void resolve_engine_logger(const toml::table&           logger_tbl,
                           std::string_view             key_prefix,
                           SourceLoc                    loc,
                           const std::filesystem::path& base_dir,
                           const LoadOptions&           opts,
                           PendingLoggerSet&            pending,
                           DiagnosticAccumulator&       acc,
                           bool                         is_engine,
                           std::size_t                  session_index)
{
    const std::size_t acc_before = acc.size();

    // ── LoggerConfig scalars ──────────────────────────────────────────────────

    fixpp::log::LoggerConfig cfg;  // defaults from E-3 (capacity=65536, drop_newest, 5000ms, -1)

    // capacity — uint32, power of 2
    if (const auto* n = logger_tbl.get("capacity"); n && n->is_integer()) {
        const auto v = n->as_integer()->get();
        if (v < 0 || v > static_cast<long long>(std::numeric_limits<std::uint32_t>::max())) {
            acc.add(LoadDiagnostic{
                .key_path = kpath(key_prefix, "capacity"),
                .reason   = reason_class::out_of_range,
                .location = loc_node(*n),
                .message  = "capacity must be a non-zero power-of-2 uint32",
            });
        } else {
            const auto u = static_cast<std::uint32_t>(v);
            if (validate_pow2_capacity(u, kpath(key_prefix, "capacity"), loc_node(*n), acc)) {
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
                .key_path = kpath(key_prefix, "on_overflow"),
                .reason   = reason_class::unknown_enum,
                .location = loc_node(*n),
                .message  = std::string{"unknown on_overflow token: \""} + std::string{tok} +
                            "\" (valid values: \"drop_newest\", \"block\")",
            });
        }
    }

    // drain_timeout — duration string (ms); default 5000ms
    if (const auto* n = logger_tbl.get("drain_timeout"); n && n->is_string()) {
        const auto d = parse_dur(n->as_string()->get(),
                                 kpath(key_prefix, "drain_timeout"), acc, loc_node(*n));
        if (d.ok) {
            cfg.drain_timeout = std::chrono::milliseconds{d.value_ms};
        }
    }

    // drain_cpu_affinity — plain int, optional; default -1
    if (const auto* n = logger_tbl.get("drain_cpu_affinity"); n && n->is_integer()) {
        cfg.drain_cpu_affinity = static_cast<int>(n->as_integer()->get());
    }

    // ring_resource is NEVER file-set (deferred arena, FR-010) — stays default.

    // ── [[logger.sinks]] — required, ordered, non-empty ──────────────────────

    const std::string sinks_kp = kpath(key_prefix, "sinks");

    const toml::node* sinks_node = logger_tbl.get("sinks");
    if (!sinks_node || !sinks_node->is_array()) {
        acc.add(LoadDiagnostic{
            .key_path = sinks_kp,
            .reason   = sinks_node ? reason_class::malformed_value
                                   : reason_class::missing_required,
            .location = sinks_node ? loc_node(*sinks_node) : loc,
            .message  = sinks_node
                ? "logger.sinks must be an array of tables ([[logger.sinks]])"
                : "logger must have at least one [[logger.sinks]] entry",
        });
        return;
    }

    const toml::array& sinks_arr = *sinks_node->as_array();
    if (sinks_arr.empty()) {
        acc.add(LoadDiagnostic{
            .key_path = sinks_kp,
            .reason   = reason_class::empty_required,
            .location = loc_node(*sinks_node),
            .message  = "logger.sinks must not be empty; at least one sink is required",
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
                .reason   = reason_class::malformed_value,
                .location = loc_node(sink_elem),
                .message  = "each [[logger.sinks]] entry must be a TOML table",
            });
            ++sink_idx;
            continue;
        }
        auto sink_ptr = resolve_log_sink(*sink_elem.as_table(), sink_idx,
                                         key_prefix, base_dir, opts.resource, acc);
        if (sink_ptr) {
            sinks.push_back(std::move(sink_ptr));
        }
        // If resolve_log_sink returned nullptr it already added a diagnostic; collect-ALL continues.
        ++sink_idx;
    }

    // If any error was accumulated, stop — do not park a partially-valid pending logger.
    if (acc.size() > acc_before) {
        return;
    }

    // Park into the pending set (N-2: file-scoped, NOT constructed here).
    PendingLogger pl{
        .cfg          = std::move(cfg),
        .sinks        = std::move(sinks),
        .key_path     = std::string{key_prefix},
        .loc          = loc,
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

void construct_loggers_if_clean(PendingLoggerSet&&     pending,
                                 ConfigBundle&           bundle,
                                 DiagnosticAccumulator& acc)
{
    // The whole-file accumulator must be empty (FR-015 / research D-7).
    if (!acc.empty()) {
        // Non-empty accumulator: no Logger constructed.
        return;
    }

    // Engine slot
    if (pending.engine.has_value()) {
        auto& pl = *pending.engine;
        bundle.engine.logger = std::make_shared<fixpp::log::Logger>(
            std::move(pl.cfg), std::move(pl.sinks));
    }

    // Per-session slots (US3/T019 — wired in Phase 5)
    for (auto& pl : pending.sessions) {
        const std::size_t i = pl.session_index;
        if (i < bundle.sessions.size()) {
            bundle.sessions[i].config.logger_override = std::make_shared<fixpp::log::Logger>(
                std::move(pl.cfg), std::move(pl.sinks));
        }
    }
}

}  // namespace fixpp::config::detail
