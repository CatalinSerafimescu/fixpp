# Phase 1 Data Model: Logging config (044 step 2, logging leg)

Entities extend the 044 `ConfigBundle` model. Field types/defaults are **verified in-source** (headers + `.cpp` cited in research.md). "File key" = the TOML spelling; "→ Field" = the target value-type member; "Required" = behavior when absent/empty.

## E-1 — Where the logger sits in the bundle

```
ConfigBundle (config_bundle.hpp)
├── engine : EngineEstablishment            ← E-2 gains ONE field: logger
└── sessions : vector<SessionDefinition>
        └── config : SessionConfig          ← logger_override (existing) set per-session
```

The host, after a successful load, copies `engine.logger` onto its `EngineConfig.logger` and opens sessions (each `SessionConfig` already carries its resolved `logger_override`). Absent `[logger]` ⇒ the field stays null ⇒ engine logger no-op (FR-003/SC-004 — byte-identical to the 044 result).

> **Tracer/meter NOT modeled.** `EngineEstablishment` does **not** gain tracer/meter fields this step — their OTLP export is unimplemented (research §finding); they stay host-supplied-after-load and recognized-but-deferred in the loader (backlog).

## E-2 — `EngineEstablishment` extension (config_bundle.hpp AMEND)

| New field | Type | Set from | Null means |
|---|---|---|---|
| `logger` | `std::shared_ptr<fixpp::log::Logger>` | `[logger]` (E-3) | engine logger no-op |

Maps 1:1 onto `EngineConfig::logger` (`engine_config.hpp:134`, `shared_ptr`, null→no-op — verified). Field type uses `fixpp::log::Logger` (= the `core::Logger` alias, `logger_fwd.hpp:37`; same type). The header forward-declares `namespace fixpp::log { class Logger; }` (no full include needed — shared_ptr member).

## E-3 — Logger (composite selector) — `[logger]`

Built via `std::make_shared<Logger>(LoggerConfig, std::pmr::vector<std::unique_ptr<Sink>>)` (the only path — `Logger` is copy+move-deleted). The **sink objects** are minted side-effect-free during resolution and parked, with the `LoggerConfig`, as a **`PendingLogger` candidate** in a loader-local keyed `PendingLoggerSet` (research D-7 — held *outside* `ConfigBundle`, since the bundle holds only the final `shared_ptr<Logger>`). The live `Logger` is constructed only at end-of-clean-load (its ctor opens every sink + spawns the drain thread — research D-7), after the side-effect-free load-time resource preflight passes; at that point each pending logger is moved into `make_shared<Logger>` and assigned to its keyed destination (engine slot → `engine.logger`; session index → `sessions[i].config.logger_override`). The carrier's `pmr::vector` and the minted sinks all use the 044 `LoadOptions::resource` (allocator identity preserved across resolve→hold→construct).

**Logger-level scalars → `LoggerConfig` (`logger.hpp:79`):**

| File key | → Field | Type / canonical values | Default | Required | Notes |
|---|---|---|---|---|---|
| `logger.capacity` | `capacity` | uint32, **power of 2** | 65536 | optional | not power-of-2 → `out_of_range` |
| `logger.on_overflow` | `on_overflow` | enum `{drop_newest, block}` | `drop_newest` | optional | unknown token → `unknown_enum` |
| `logger.drain_timeout` | `drain_timeout` | duration (ms) | 5000 ms | optional | unitless/ambiguous → `malformed_value` (044 duration rule) |
| `logger.drain_cpu_affinity` | `drain_cpu_affinity` | int (-1 = none) | -1 | optional | niche; plain int |
| — `ring_resource` | — | `pmr::memory_resource*` | default | **NEVER file-set** | deferred arena (FR-010); selecting it → deferred reason |

**Sinks → ordered `[[logger.sinks]]` array (E-4):** non-empty list, minted in array order. Zero/absent sinks → `empty_required`/`missing_required` on `logger.sinks`.

## E-4 — Sink selectors — `[[logger.sinks]]` (each `{ kind, params }`)

Each resolved via its factory `make(memory_resource*, SinkConfig const&)` (resource = 044 load-time `LoadOptions::resource`; ignored by these sinks). `SinkConfig` base is empty ⇒ **no per-sink level**.

### kind = `file` → `FileSinkConfig` (`file_sink.hpp:45`)

| File key | → Field | Type | Default | Notes |
|---|---|---|---|---|
| `directory` | `directory` | path | "." | relative → resolved vs config-file dir (FR-018) |
| `base_name` | `base_name` | string | "fixpp" | |
| `max_file_bytes` | `max_file_bytes` | uint64 | 256 MiB | `0` → `out_of_range` |
| `max_keep_count` | `max_keep_count` | uint32 | 8 | |
| `async_fsync` | `async_fsync` | bool | true | |

### kind = `syslog` → `SyslogSinkConfig` (`syslog_sink.hpp:44`, POSIX-only `FIXPP_HAS_SYSLOG`)

| File key | → Field | Type | Default | Notes |
|---|---|---|---|---|
| `ident` | `ident` | string | "fixpp" | |
| `facility` | `facility` | **int**, via name→`LOG_*` map | `LOG_DAEMON` | exact closed set below; unknown → `unknown_enum`; build-undefined → `invalid_or_contradictory_selector` |

**Accepted `facility` names (exact closed set — canonical lowercase POSIX, no ellipsis):**
`kern`, `user`, `mail`, `daemon`, `auth`, `syslog`, `lpr`, `news`, `uucp`, `cron`, `authpriv`, `ftp`, `local0`, `local1`, `local2`, `local3`, `local4`, `local5`, `local6`, `local7` → mapped to `LOG_KERN`, `LOG_USER`, …, `LOG_LOCAL7`.

- A name **in this set whose `LOG_*` macro is not defined on the build** (some are `#ifdef`-conditional, e.g. `LOG_AUTHPRIV`, `LOG_FTP`, `LOG_CRON` on some platforms) → `invalid_or_contradictory_selector` (build-unavailable — same class as the platform gate below).
- A name **not in this set** → `unknown_enum`, reporting the legal set.

**Platform gate:** on a build without `FIXPP_HAS_SYSLOG`, `kind="syslog"` itself → `invalid_or_contradictory_selector` (FR-013 / Clarifications). The resolver `#ifdef`-guards construction; the `#else` emits the diagnostic (never silently dropped).

### kind = `otlp` → `OtlpLogSinkConfig` (`otlp_log_sink.hpp:34`) — **fully real (HTTP export in `open()`)**, **build-conditional**

**Build availability:** OTLP log-sink support compiles into the separate `fixpp_log_otlp` target only when the OpenTelemetry SDK is present (`src/log/CMakeLists.txt:38`, `if(TARGET opentelemetry-cpp::api)`). On a build without it (no `FIXPP_CONFIG_HAS_OTLP`), `kind="otlp"` → `invalid_or_contradictory_selector` (build-unavailable, mirroring syslog on non-POSIX — FR-013). The rows below apply when OTLP support is compiled in.

| File key | → Field | Type | Default | Required | Notes |
|---|---|---|---|---|---|
| `endpoint` | `endpoint` | string | — | **required** | absent/empty → `missing_required`/`empty_required` |
| `use_grpc` | `use_grpc` | bool | false | — | `true` → `recognized_not_yet_supported_step2` (deferred transport) |
| `cert_source` | `cert_source` | string (PEM CA) | "" (plain HTTP) | optional | relative → config dir; unreadable **or not PEM-magic** → `invalid_or_contradictory_selector` (FR-014); full CA-bundle parse at sink `open()` |
| `export_timeout` | `export_timeout` | duration (sec) | 10 s | optional | |
| `max_export_batch` | `max_export_batch` | size_t | 512 | optional | `0` → `out_of_range` |
| `max_export_retries` | `max_export_retries` | size_t | 3 | optional | |

Unknown sink `kind` → `unknown_enum` (legal set `{file, syslog, otlp}`). Export connectivity is NOT validated at load (D-4) — runtime `otel_export_failed` is the sink's concern.

## E-5 — Per-session override + deferred-key disposition

**Per-session override** (resolved into a session-keyed `PendingLogger`, then — only at end-of-clean-load — assigned onto the existing `SessionConfig::logger_override`):

| Scope | Logger |
|---|---|
| Engine default | `engine.logger` (E-2) |
| Per-session override key | `[session].logger` → session-keyed `PendingLogger` → `config.logger_override` at clean-construct (reuses the E-3 resolver) |
| Absent | session inherits the engine default (null override) |

**Keyed pending carrier (loader-local; research D-7).** The engine and every per-session pending logger are accumulated in **one file-scoped `PendingLoggerSet`** (`{ optional<PendingLogger> engine; vector<PendingLogger> sessions; }`), held *outside* `ConfigBundle`. Each `PendingLogger` carries its target key (engine slot OR session index). Construction is a **single final pass** over the whole set, gated on an empty whole-file accumulator: a later session's error suppresses construction of an earlier session's logger (N-2). The per-session pending loggers are therefore NOT constructed inside the per-session resolution loop.

**Deferred-key disposition** (`recognize_keys()`, D-6):

| Key(s) | Disposition this step |
|---|---|
| `logger` | **SUPPORTED** (flipped from deferred) |
| `tracer`, `meter` | deferred — OTLP trace/metric export unimplemented (backlog) |
| `log_sink`, `otlp`, `exporter` | deferred — not standalone top-level blocks (nest under logger) |
| `prometheus` | deferred — no file channel |
| `arena`, `message_arena`, `session_arena`, `framer_carry_arena` | deferred — memory-arena surface (next step) |
| `dialect_overlay` | deferred — next step |
| `tap`, `tap_consumer` | forced-deferred — 2l unshipped (bare stub) |

All deferred keys → `recognized_not_yet_supported_step2` (symbol kept; message generalized), never `unknown_key`, never silent-ignore (FR-022).

## Validation summary (fail-closed, collect-ALL)

- Required key absent/empty → `missing_required`/`empty_required` (sink list, OTLP endpoint).
- Every enum (sink kind, overflow policy, syslog facility) → canonical spellings only; else `unknown_enum` + legal set.
- Numeric bounds (capacity power-of-2, positive batch/bytes) → `out_of_range`.
- Unreadable or non-PEM-magic `otlp` `cert_source` → `invalid_or_contradictory_selector` (caught by the **side-effect-free** load-time resource preflight: readable + leading `-----BEGIN`/PEM-magic; the full CA-bundle parse happens at sink `open()`, research D-4/D-7).
- File-sink `directory` that does **not already exist** or is **not a writable directory** → `invalid_or_contradictory_selector` (same **side-effect-free** preflight — stat/access only, **no `mkdir`, no probe file**; `FileSink::open()` creates/opens the live log file, not the directory; the directory must pre-exist (the preflight requires it)).
- Platform/build-unavailable sink (syslog on non-POSIX; otlp on a no-OTel build) → `invalid_or_contradictory_selector`.
- `use_grpc=true` → `recognized_not_yet_supported_step2`.
- All diagnostics collected in **one pass** (D-7) with the session-local `acc.size()` delta pattern. Pending loggers (engine + per-session) are parked in a file-scoped keyed `PendingLoggerSet`; the live `Logger`(s) are constructed only at end-of-clean-load (after the resource preflight passes) in a single final pass keyed to engine slot / session index; a non-empty accumulator ⇒ no `Logger` constructed, no bundle, **no files opened, no directory created, no drain thread started**. (Named inherited 017 limitation: the ctor silently disables a sink whose `open()` fails post-preflight — research D-7.)
