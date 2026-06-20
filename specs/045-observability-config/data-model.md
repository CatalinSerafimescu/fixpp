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

Built via `std::make_shared<Logger>(LoggerConfig, std::pmr::vector<std::unique_ptr<Sink>>)` (the only path — `Logger` is copy+move-deleted; ctor side-effect-free).

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
| `facility` | `facility` | **int**, via name→int map | `LOG_DAEMON` | canonical lowercase names `{daemon, user, mail, auth, syslog, local0..local7, …}`; unknown → `unknown_enum` |

**Platform gate:** on a build without `FIXPP_HAS_SYSLOG`, `kind="syslog"` → `invalid_or_contradictory_selector` (FR-013 / Clarifications). The resolver `#ifdef`-guards construction; the `#else` emits the diagnostic (never silently dropped).

### kind = `otlp` → `OtlpLogSinkConfig` (`otlp_log_sink.hpp:34`) — **fully real (HTTP export in `open()`)**

| File key | → Field | Type | Default | Required | Notes |
|---|---|---|---|---|---|
| `endpoint` | `endpoint` | string | — | **required** | absent/empty → `missing_required`/`empty_required` |
| `use_grpc` | `use_grpc` | bool | false | — | `true` → `recognized_not_yet_supported_step2` (deferred transport) |
| `cert_source` | `cert_source` | string (PEM CA) | "" (plain HTTP) | optional | relative → config dir; unreadable → `invalid_or_contradictory_selector` (FR-014) |
| `export_timeout` | `export_timeout` | duration (sec) | 10 s | optional | |
| `max_export_batch` | `max_export_batch` | size_t | 512 | optional | `0` → `out_of_range` |
| `max_export_retries` | `max_export_retries` | size_t | 3 | optional | |

Unknown sink `kind` → `unknown_enum` (legal set `{file, syslog, otlp}`). Export connectivity is NOT validated at load (D-4) — runtime `otel_export_failed` is the sink's concern.

## E-5 — Per-session override + deferred-key disposition

**Per-session override** (written onto the existing `SessionConfig::logger_override` by the per-session resolver):

| Scope | Logger |
|---|---|
| Engine default | `engine.logger` (E-2) |
| Per-session override key | `[session].logger` → `config.logger_override` (reuses the E-3 resolver) |
| Absent | session inherits the engine default (null override) |

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
- Unreadable/unparseable PEM (`otlp` `cert_source`) → `invalid_or_contradictory_selector`.
- Platform-unavailable sink (syslog/non-POSIX) → `invalid_or_contradictory_selector`.
- `use_grpc=true` → `recognized_not_yet_supported_step2`.
- All diagnostics collected in **one pass** (D-7) with the session-local `acc.size()` delta pattern; non-empty accumulator ⇒ no bundle, nothing opened. Construction is side-effect-free, so a failed load leaves nothing opened.
