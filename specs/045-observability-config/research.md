# Phase 0 Research: Logging config (044 step 2, logging leg)

All decisions grounded against the **real headers AND `.cpp` implementation** read 2026-06-20 (per [[feedback_planning_explore_existence_claims_unreliable]] — a planning Explore pass's "exists/ready" claims are not trusted; every type/field/behavior below was verified in-source, including the runtime behavior behind the provider headers).

## THE scope-deciding finding — tracer/meter OTLP export is an unimplemented stub

`src/otel/providers.cpp` (read 2026-06-20):
- **`TracerProvider::Impl` ctor** (line 88-110): the production path unconditionally builds a hardcoded **`NullSpanExporter`** (line 97 — `Export()` discards every batch, returns `kSuccess`). It reads **only `cfg.resource`** (service attributes); `endpoint`, `cert_path`, `export_interval`, `export_timeout`, `use_grpc` are **never referenced**. `init_status_` flips to `otel_provider_init_failed` **only** when the `tracer_factory_for_test` seam throws — **never in production**.
- **`MeterProvider::Impl` ctor** (line 135-153): builds a `MeterProviderFactory::Create(ViewRegistry, resource)` with **no metric reader/exporter** at all; metrics go nowhere. Same: only `resource` consumed.

⇒ Configuring `[tracer]`/`[meter]` from a file would set **dead knobs**: an operator points at a collector, receives zero telemetry, and gets **no error** (`init_status()` never fails in production) — the exact silent-no-telemetry trap, but at a layer config cannot fix. The OTLP trace/metric export is a provider-layer (Phase-5-class) item (consistent with the `use_grpc` "deferred to Phase 5" comments).

**Decision (user-confirmed 2026-06-20):** **defer tracer/meter config to the backlog** (`REMAINING-WORK.md` + `phases/phase-4/config/`), gated on the OTLP trace/metric export pipeline shipping. Step 2 = the **logging leg** only. The two clarify questions about exporter-init-fail-closed and separate-vs-combined exporter blocks are **moot** (superseded by this finding).

**Contrast — the OTLP LOG sink is fully real.** `src/log/otlp_log_sink.cpp` (read 2026-06-20): `OtlpLogSink::open()` (line 159) builds a real `OtlpHttpLogRecordExporterFactory::Create(opts)` with `opts.url = cfg_.endpoint` (line 183) and `opts.ssl_ca_cert_path = cfg_.cert_source` (line 186) — **endpoint and cert ARE used** and records genuinely export over HTTP. The exporter is built in `open()`, **not the ctor**, so sink *construction* is side-effect-free (important for fail-closed teardown, D-7).

## Verified surface (the authoritative inventory — logging leg)

| Object | Type (header) | Construction | File-configurable params |
|---|---|---|---|
| Logger | `fixpp::log::Logger` (`logger.hpp:122`); `fixpp::core::Logger` is a **`using` alias** of it (`logger_fwd.hpp:37`) | **copy AND move deleted** → `std::make_shared<Logger>(LoggerConfig, std::pmr::vector<std::unique_ptr<Sink>>)` only; ctor side-effect-free (sinks open lazily) | `LoggerConfig`: `capacity` (uint32, **power-of-2**, default 65536), `on_overflow` (`overflow_policy{drop_newest[def], block}`), `drain_timeout` (ms, default 5000), `drain_cpu_affinity` (int, default -1). **`ring_resource` (pmr) = DEFERRED arena surface → NOT file-set** |
| Sink base | `fixpp::log::Sink` (4-pure-virtual plugin); `SinkConfig` base is **empty** (`sink.hpp:33`) | each via its factory `make(std::pmr::memory_resource*, SinkConfig const&)` | **no per-sink level/severity field exists** (filtering is logger-side) |
| file sink | `FileSink`/`FileSinkFactory` (`file_sink.hpp:82/162`) | `FileSinkFactory::make` → `FileSinkConfig` | `directory` (path, "."), `base_name` (str "fixpp"), `max_file_bytes` (uint64, 256 MiB), `max_keep_count` (uint32, 8), `async_fsync` (bool, true) |
| syslog sink | `SyslogSink`/`SyslogSinkFactory` (`syslog_sink.hpp`), **`#ifdef FIXPP_HAS_SYSLOG` (POSIX-only)** | `SyslogSinkFactory::make` → `SyslogSinkConfig` | `ident` (str "fixpp"), `facility` (**int**, def `LOG_DAEMON`) |
| otlp log sink | `OtlpLogSink`/`OtlpLogSinkFactory` (`otlp_log_sink.hpp:87/126`) | `OtlpLogSinkFactory::make` → `OtlpLogSinkConfig`; real HTTP export in `open()` | `endpoint` (str, required), `use_grpc` (bool, **deferred**), `cert_source` (str PEM CA), `export_timeout` (sec, 10), `max_export_batch` (size_t, 512), `max_export_retries` (size_t, 3) |

Bundle/config anchors (verified):
- `EngineConfig::logger` = `shared_ptr<core::Logger>` (`engine_config.hpp:134`), null → no-op. (`tracer`/`meter` fields exist too but are NOT populated by this feature.)
- `SessionConfig::logger_override` = `shared_ptr<log::Logger>` (`session_config.hpp:199`), null → engine default.
- `EngineEstablishment` (`config_bundle.hpp:37`) today carries clock/store/cert/transport/dictionaries — **no logger field** (the 044 host-supplied-after-load deferral).
- `reason_class` enum (`load_diagnostic.hpp:18`) — 9 values incl. `recognized_not_yet_supported_step2` + `invalid_or_contradictory_selector`; **sufficient, no new value needed**.

---

## Decisions

### D-1 — Bundle wiring: extend `EngineEstablishment` with ONE field; per-session via existing `SessionConfig::logger_override`

**Decision.** Add `std::shared_ptr<fixpp::log::Logger> logger;` to `EngineEstablishment` (E-2). The per-session logger override is written **directly** onto `SessionDefinition.config.logger_override` (existing `SessionConfig` member) by the per-session resolver.
**Rationale.** Mirrors 044's structural model; the override field already exists with exactly the right shape — no `SessionConfig` change. Use `fixpp::log::Logger` (= the `core::Logger` alias) for include-locality, matching `SessionConfig::logger_override`.
**Alternatives rejected.** Populate `EngineConfig` directly in the loader — rejected: the loader returns a `ConfigBundle`; the host assembles `EngineConfig` (FR-012).

### D-2 — Logger is a composite selector; heap-only; zero-sinks is an error

**Decision.** `[logger]` resolves a *composite*: the logger-level scalars (`capacity`, `on_overflow`, `drain_timeout`, optional `drain_cpu_affinity`) → a `LoggerConfig`, plus an **ordered, non-empty** `[[logger.sinks]]` array → `std::pmr::vector<std::unique_ptr<Sink>>` minted in array order, then `std::make_shared<Logger>(loggerCfg, std::move(sinks))`. Zero sinks → `empty_required` on `logger.sinks`. `capacity` not a power of 2 → `out_of_range`. `ring_resource` is **never** file-set (deferred arena, FR-010).
**Rationale.** `Logger` is copy-and-move-deleted so a `shared_ptr` heap instance is the *only* legal carrier — matches `EngineConfig::logger`. A logger draining nowhere is an operator mistake (omitting `[logger]` is the no-op). The sink factories' `make(resource, cfg)` ignore `resource` for these sinks, so the loader passes the 044 load-time `LoadOptions::resource` uniformly.
**Alternatives rejected.** A flat single-sink shorthand — rejected: the real surface is an ordered multi-sink chain (file + OTLP fan-out is a primary use case).

### D-3 — Sink resolvers: file/syslog/otlp; syslog facility name→int; syslog-on-non-POSIX is `invalid_or_contradictory_selector`

**Decision.** Resolve each `[[logger.sinks]]` entry by `kind`:
- `file` → `FileSinkConfig`. `directory` resolves relative to the config-file dir (FR-018).
- `syslog` → `SyslogSinkConfig`. **`facility` is an `int` in-source**; the loader owns a **closed name→int map** (canonical lowercase: `{daemon, user, mail, auth, syslog, local0..local7, …}`; unknown → `unknown_enum`). On a build **without** `FIXPP_HAS_SYSLOG`, `kind="syslog"` → `invalid_or_contradictory_selector` (Clarifications / FR-013); the resolver `#ifdef`-guards construction and the `#else` emits the diagnostic.
- `otlp` → `OtlpLogSinkConfig` (endpoint required; cert_source PEM relative to config dir; export_timeout; max_export_batch; max_export_retries). `use_grpc=true` → `recognized_not_yet_supported_step2` (deferred transport). Empty `endpoint` → `missing_required`/`empty_required`. Unreadable `cert_source` → `invalid_or_contradictory_selector` (FR-014).
**Rationale.** Each factory + config struct already exists; the loader is pure mapping. The syslog facility map is the only new vocabulary; a raw-int passthrough would violate the closed-enum invariant (FR-017).
**Alternatives rejected.** Raw-int `facility`; silent-skip of syslog on non-POSIX — both rejected (closed-enum rule; Clarifications loud-signal decision).

### D-4 — Cert readability validated at load; export connectivity is NOT (runtime)

**Decision.** For an `otlp` sink the loader validates at load: `endpoint` present/non-empty (required) + `cert_source` file readable/parseable (relative to config dir, FR-014/FR-018). It does **not** attempt to validate collector reachability — the exporter is built in `OtlpLogSink::open()` (host-called, post-load) and export failures are the sink's existing runtime `otel_export_failed` concern, not a load-time check.
**Rationale.** Construction is side-effect-free (verified); the loader can only check what is determinable at load (file readability), mirroring 044's cert-path validation. There is no provider-init-status to check (that was the tracer/meter path, now deferred).

### D-5 — Provider-init-failure machinery is OUT (tracer/meter deferred)

**Decision.** No `init_status()` check, no provider construction, no fail-closed-on-init logic in this step — that machinery belonged to the deferred tracer/meter legs. The logger sinks have no equivalent load-time init failure (construction is side-effect-free).
**Rationale.** Direct consequence of the scope cut (THE finding above). Removes the most complex part of the original step-2 plan.

### D-6 — Deferred-key split: flip exactly `{logger}`; keep everything else deferred

**Decision.** In `recognize_keys()` (`toml_config_loader.cpp:268`), move **only** `logger` from `kDeferred` into `kRecognized`. Keep deferred:
- `tracer` / `meter` — **export unimplemented** at the provider layer (THE finding); backlog item.
- `log_sink` / `otlp` / `exporter` — not standalone top-level blocks (sinks nest under `[[logger.sinks]]`).
- `prometheus` — no file channel (no Prometheus config surface).
- `arena` / `message_arena` / `session_arena` / `framer_carry_arena` — deferred memory-arena surface (user decision 2026-06-20).
- `dialect_overlay` — deferred (user decision 2026-06-20).
- `tap` / `tap_consumer` — forced-deferred (bare stub; unshipped 2l; verified `tap_consumer.hpp`).

The enum **symbol** `recognized_not_yet_supported_step2` is **kept** (renaming churns 044 tests; loader-local C++ enum); only the diagnostic **message text** is generalized to "recognized but not yet supported".
**Rationale.** A precise one-key flip prevents an FR-022 over-claim and keeps the deferral boundary honest (especially for tracer/meter, whose deferral is now load-bearing — operators must not believe traces flow).

### D-7 — Path resolution + redaction + collect-ALL reuse 044 mechanics; side-effect-free construction keeps fail-closed clean

**Decision.** Logger PEM/dir paths resolve relative to the config-file directory via 044's `base_dir` (FR-018); credentials in a collector endpoint are redacted (FR-023). Resolvers run inside the same single pass as 044's establishment resolvers using the session-local `acc.size()` delta pattern (collect-ALL, FR-021); a non-empty accumulator at end-of-pass ⇒ no bundle, nothing opened (FR-015). Because `Logger`/`Sink` construction is side-effect-free (sinks open at host-called `open()`, verified), a failed multi-error load that already built a valid `[logger]` leaves **no** open files/threads — clean fail-closed.
**Rationale.** Inherits the 044 Gate-B collect-ALL fix; the side-effect-free construction (advisor-flagged, source-verified) closes the "nothing left opened" purity concern US2 promises.

## Process actions (not code design)

- **Layer grounding:** extend `tools/check_layers.py` config whitelist += `log`; update `.specify/architecture.md` config rows += `log`. (`otel` NOT added — logging leg only.) NAMED pre-merge tasks (plan §Constitution Check).
- **Catalogue/coverage:** extend the `T-043` design row (or sibling) for the logging leg + a `coverage-index.md` note (§VI.4 pre-merge).
- **Fuzz:** extend the `fuzz_toml_loader` corpus with logger blocks (§VII.7 carry-over).
- **Backlog:** record tracer/meter config as a backlog item (`REMAINING-WORK.md` 14b-residual + a `phases/phase-4/config/` note), gated on the OTLP trace/metric export pipeline.

## Open questions carried to /tasks (not blocking Gate A)

- **OQ-1:** whether the logger resolver lives in `selector_resolver.cpp` or a new `logger_resolver.cpp` — a file-organization call for `/tasks` (`selector_resolver.cpp` is already ~40 KB).
- **OQ-2:** the syslog facility name→int map's exact accepted set (which `LOG_*` facilities to expose) — a small enumeration to finalize at `/tasks`/data-model.
