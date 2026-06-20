# Quickstart: Logging config (044 step 2, logging leg)

A single TOML file now drives session establishment **and** the logging pipeline. Below: an example file, then the host stub.

> Tracer/metrics config is **not** in this step — the OTLP trace/metric export is unimplemented at the provider layer (a `[tracer]`/`[meter]` block is rejected as recognized-but-not-yet-supported). Logging (file/syslog/OTLP-log) is fully implemented and exports real output.

## Example `fixpp.toml` (logging excerpt)

```toml
# ── A fan-out logger draining to a rotating file AND an OTLP log collector ──
[logger]
capacity      = 65536          # ring slots (power of 2)
on_overflow   = "drop_newest"  # or "block"
drain_timeout = "5000ms"       # unit-suffixed duration (044 rule — a bare integer is malformed_value)

  [[logger.sinks]]
  kind        = "file"
  directory   = "logs"         # relative → resolved against this file's directory
  base_name   = "fixpp"
  max_file_bytes = 268435456   # 256 MiB
  max_keep_count = 8
  async_fsync = true

  [[logger.sinks]]
  kind        = "otlp"
  endpoint    = "http://collector:4318/v1/logs"
  cert_source = "collector-ca.pem"   # optional PEM CA → relative to this file
  export_timeout   = "10s"     # unit-suffixed duration (044 rule)
  max_export_batch = 512

# (syslog example: kind = "syslog", ident = "fixpp", facility = "local0")

# ── Per-session override: this session logs elsewhere ──
[[session]]
sender_comp_id = "ACME"
target_comp_id = "EXCH"
# ... 044 establishment keys ...
  [session.logger]             # overrides the engine logger for THIS session only
  capacity = 16384
    [[session.logger.sinks]]
    kind = "file"
    directory = "logs/acme"
```

> The referenced file-sink directories (`logs/`, `logs/acme/`) must exist before load — the loader fails closed if a configured file-sink directory does not exist (`FileSink::open()` opens the log file, not the directory).

## Host stub (unchanged shape from step 1 — just the logger wired for you)

```cpp
auto result = fixpp::config::load_toml_config(
    "fixpp.toml",
    fixpp::config::LoadOptions{ .engine_executor = exec, .resource = &load_arena });

if (!result) {
    for (const auto& d : result.error())          // collect-ALL: every error, one pass
        log_startup_error(d.key_path, d.reason, d.location, d.message);
    return EXIT_FAILURE;                           // fail-closed: nothing opened
}
auto& bundle = *result;

fixpp::core::EngineConfig engine_cfg;
engine_cfg.executor = exec;                        // host-supplied (no file channel)
engine_cfg.clock    = bundle.engine.clock;
engine_cfg.logger   = bundle.engine.logger;        // ← step 2 (logging leg)
// ... 044 establishment fields (store/cert/transport factories, dictionaries) ...
// engine_cfg.tracer / .meter stay host-supplied (deferred — not file-driven this step)

for (auto& sd : bundle.sessions) {
    sd.config.application = my_app;                // host-supplied (no file channel)
    // sd.config.logger_override already set by the loader if the session overrode it
    engine.open(sd.config);
}
```

## Failure examples (all fail closed, before any `open`)

| File mistake | Diagnostic reason | key_path |
|---|---|---|
| `[[logger.sinks]] kind = "stdout"` | `unknown_enum` (legal: file/syslog/otlp) | `logger.sinks[0].kind` |
| `[logger]` with no `[[logger.sinks]]` | `empty_required` | `logger.sinks` |
| `kind="otlp"` with no `endpoint` | `missing_required` | `logger.sinks[1].endpoint` |
| `capacity = 1000` (not power of 2) | `out_of_range` | `logger.capacity` |
| `kind="syslog"` on a non-POSIX build | `invalid_or_contradictory_selector` | `logger.sinks[0].kind` |
| `kind="otlp"` cert_source unreadable | `invalid_or_contradictory_selector` | `logger.sinks[1].cert_source` |
| `kind="otlp" use_grpc = true` | `recognized_not_yet_supported_step2` | `logger.sinks[1].use_grpc` |
| `[tracer]` / `[meter]` block | `recognized_not_yet_supported_step2` | `tracer` / `meter` |
| `[message_arena]` / `[tap]` | `recognized_not_yet_supported_step2` | `message_arena` / `tap` |

(Omitting `[logger]` entirely is **not** an error — logging is optional; the engine runs with a no-op logger, byte-identical to the step-1 result.)
