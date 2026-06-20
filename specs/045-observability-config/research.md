# Phase 0 Research: Logging config (044 step 2, logging leg)

All decisions grounded against the **real headers AND `.cpp` implementation** read 2026-06-20 (per [[feedback_planning_explore_existence_claims_unreliable]] — a planning Explore pass's "exists/ready" claims are not trusted; every type/field/behavior below was verified in-source, including the runtime behavior behind the provider headers).

## THE scope-deciding finding — tracer/meter OTLP export is an unimplemented stub

`src/otel/providers.cpp` (read 2026-06-20):
- **`TracerProvider::Impl` ctor** (line 88-110): the production path unconditionally builds a hardcoded **`NullSpanExporter`** (line 97 — `Export()` discards every batch, returns `kSuccess`). It reads **only `cfg.resource`** (service attributes); `endpoint`, `cert_path`, `export_interval`, `export_timeout`, `use_grpc` are **never referenced**. `init_status_` flips to `otel_provider_init_failed` **only** when the `tracer_factory_for_test` seam throws — **never in production**.
- **`MeterProvider::Impl` ctor** (line 135-153): builds a `MeterProviderFactory::Create(ViewRegistry, resource)` with **no metric reader/exporter** at all; metrics go nowhere. Same: only `resource` consumed.

⇒ Configuring `[tracer]`/`[meter]` from a file would set **dead knobs**: an operator points at a collector, receives zero telemetry, and gets **no error** (`init_status()` never fails in production) — the exact silent-no-telemetry trap, but at a layer config cannot fix. The OTLP trace/metric export is a provider-layer (Phase-5-class) item (consistent with the `use_grpc` "deferred to Phase 5" comments).

**Decision (user-confirmed 2026-06-20):** **defer tracer/meter config to the backlog** (`REMAINING-WORK.md` + `phases/phase-4/config/`), gated on the OTLP trace/metric export pipeline shipping. Step 2 = the **logging leg** only. The two clarify questions about exporter-init-fail-closed and separate-vs-combined exporter blocks are **moot** (superseded by this finding).

**Contrast — the OTLP LOG sink is fully real.** `src/log/otlp_log_sink.cpp` (read 2026-06-20): `OtlpLogSink::open()` (line 159) builds a real `OtlpHttpLogRecordExporterFactory::Create(opts)` with `opts.url = cfg_.endpoint` (line 183) and `opts.ssl_ca_cert_path = cfg_.cert_source` (line 186) — **endpoint and cert ARE used** and records genuinely export over HTTP. The exporter is built in `OtlpLogSink::open()`, **not the sink ctor**, so minting the sink *object* is side-effect-free — but `open()` is called by the **`Logger` ctor**, not lazily, so constructing a live `Logger` is side-effectful (it opens every sink and starts the drain thread; see D-7). The loader exploits the side-effect-free sink minting to defer live-`Logger` construction to end-of-clean-load (D-7).

## Verified surface (the authoritative inventory — logging leg)

| Object | Type (header) | Construction | File-configurable params |
|---|---|---|---|
| Logger | `fixpp::log::Logger` (`logger.hpp:122`); `fixpp::core::Logger` is a **`using` alias** of it (`logger_fwd.hpp:37`) | **copy AND move deleted** → `std::make_shared<Logger>(LoggerConfig, std::pmr::vector<std::unique_ptr<Sink>>)` only. **Ctor is side-effectful** (`logger.cpp:165-180`): it calls `sinks_[i]->open()` on every sink (silently disabling + counting any that fail) and spawns the drain OS thread. The sink FACTORIES/objects are side-effect-free; the live `Logger` is not (deferred to end-of-clean-load — D-7) | `LoggerConfig`: `capacity` (uint32, **power-of-2**, default 65536), `on_overflow` (`overflow_policy{drop_newest[def], block}`), `drain_timeout` (ms, default 5000), `drain_cpu_affinity` (int, default -1). **`ring_resource` (pmr) = DEFERRED arena surface → NOT file-set** |
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

**Decision.** `[logger]` resolves a *composite*: the logger-level scalars (`capacity`, `on_overflow`, `drain_timeout`, optional `drain_cpu_affinity`) → a `LoggerConfig`, plus an **ordered, non-empty** `[[logger.sinks]]` array → `std::pmr::vector<std::unique_ptr<Sink>>` minted in array order. The resolution produces a **pending logger candidate** (a `PendingLogger` carrier — D-7) held in loader-local state; it does **not** construct the live `Logger`. The single `std::make_shared<Logger>(std::move(loggerCfg), std::move(sinks))` belongs only to the final clean-accumulator construction step (D-7). Zero sinks → `empty_required` on `logger.sinks`. `capacity` not a power of 2 → `out_of_range`. `ring_resource` is **never** file-set (deferred arena, FR-010).
**Rationale.** `Logger` is copy-and-move-deleted so a `shared_ptr` heap instance is the *only* legal carrier — matches `EngineConfig::logger`. A logger draining nowhere is an operator mistake (omitting `[logger]` is the no-op). The sink factories' `make(resource, cfg)` ignore `resource` for these sinks, so the loader passes the 044 load-time `LoadOptions::resource` uniformly.
**Alternatives rejected.** A flat single-sink shorthand — rejected: the real surface is an ordered multi-sink chain (file + OTLP fan-out is a primary use case).

### D-3 — Sink resolvers: file/syslog/otlp; syslog facility name→int; syslog-on-non-POSIX is `invalid_or_contradictory_selector`

**Decision.** Resolve each `[[logger.sinks]]` entry by `kind`:
- `file` → `FileSinkConfig`. `directory` resolves relative to the config-file dir (FR-018).
- `syslog` → `SyslogSinkConfig`. **`facility` is an `int` in-source**; the loader owns a **closed name→`LOG_*` map** with the exact accepted set (canonical lowercase POSIX facility names, no ellipsis): `kern, user, mail, daemon, auth, syslog, lpr, news, uucp, cron, authpriv, ftp, local0, local1, local2, local3, local4, local5, local6, local7`. A name **whose `LOG_*` macro is not defined on the build** (some are `#ifdef`-conditional, e.g. `LOG_AUTHPRIV`/`LOG_FTP`) → `invalid_or_contradictory_selector` (build-unavailable). An **unknown** name (not in the set) → `unknown_enum` with the legal set. On a build **without** `FIXPP_HAS_SYSLOG`, `kind="syslog"` itself → `invalid_or_contradictory_selector` (Clarifications / FR-013); the resolver `#ifdef`-guards construction and the `#else` emits the diagnostic. (See data-model E-4 for the full table.)
- `otlp` → `OtlpLogSinkConfig` (endpoint required; cert_source PEM relative to config dir; export_timeout; max_export_batch; max_export_retries). **Build-conditional:** OTLP log-sink support compiles into the separate `fixpp_log_otlp` target only when `opentelemetry-cpp::api` is present (`src/log/CMakeLists.txt:38`); on a build without it, `kind="otlp"` → `invalid_or_contradictory_selector` (build-unavailable, like syslog on non-POSIX — D-7 / FR-013). When available: `use_grpc=true` → `recognized_not_yet_supported_step2` (deferred transport); empty `endpoint` → `missing_required`/`empty_required`; unreadable `cert_source` → `invalid_or_contradictory_selector` (FR-014).
**Rationale.** Each factory + config struct already exists; the loader is pure mapping. The syslog facility map is the only new vocabulary; a raw-int passthrough would violate the closed-enum invariant (FR-017).
**Alternatives rejected.** Raw-int `facility`; silent-skip of syslog on non-POSIX — both rejected (closed-enum rule; Clarifications loud-signal decision).

### D-4 — Cert readability validated by an explicit load-time preflight; export connectivity is NOT (runtime)

**Decision.** For an `otlp` sink the loader validates by an **explicit load-time resource preflight** (D-7): `endpoint` present/non-empty (required) + `cert_source` file **readable + PEM-magic-validated** (a cheap readability check plus a leading `-----BEGIN`/PEM-magic-byte check, relative to config dir, FR-014/FR-018). The preflight runs over the resolved sinks **before** any live `Logger` is constructed (the `Logger` ctor's eager `open()` is too late and failure-suppressing — D-7). The **full CA-bundle parse happens at sink `open()`** (final construction), not at load. The loader does **not** attempt to validate collector reachability — the exporter is built in `OtlpLogSink::open()` (called by the `Logger` ctor at end-of-clean-load) and export failures are the sink's existing runtime `otel_export_failed` concern, not a load-time check.
**Rationale.** There is **no cheap public load-time CA-PEM *parse* primitive** the loader can reuse: `tls::file_cert_source::make_file_cert_source` is a **client-identity** cert source that unconditionally throws if `leaf_path`/`private_key_path` is empty (`file_cert_source.cpp:237-242`), and the OTLP `cert_source` is a **CA bundle only** (`otlp_log_sink.cpp` → `ssl_ca_cert_path`) — so calling it with a CA-only `Config` fails with `tls_cert_load_failed`, NOT a usable preflight. The only real CA-bundle parser is the private `file_cert_source::Impl::load_ca_bundle` (`is_pem()` is a magic-byte check, not a parse). The loader therefore implements a cheap readability + PEM-magic check at load and leaves the full parse to `OtlpLogSink::open()` (where `ssl_ca_cert_path` is consumed) at clean-accumulator construction. There is no provider-init-status to check (that was the tracer/meter path, now deferred). (Future tightening, **not required this step**: lift `load_ca_bundle` into a public "parse CA-bundle PEM" TLS helper to give the loader a true load-time CA parse.)

### D-5 — Provider-init-failure machinery is OUT (tracer/meter deferred)

**Decision.** No `init_status()` check, no provider construction, no fail-closed-on-init **provider** logic in this step — that machinery belonged to the deferred tracer/meter legs. The logger sinks instead get a **side-effect-free resource preflight** (D-4/D-7: file-sink dir already-exists + writable [stat/access only, no mkdir, no probe file] + OTLP cert readable + PEM-magic-validated + endpoint-present) — a load-time *resource* check, distinct from the deferred provider-init-status machinery.
**Rationale.** Direct consequence of the scope cut (THE finding above). Removes the most complex part of the original step-2 plan (provider-init-status), while the logger leg keeps the cheaper, deterministic resource preflight that its fail-closed guarantees require.

### D-6 — Deferred-key split: flip exactly `{logger}`; keep everything else deferred

**Decision.** In the `recognize_keys()` `kDeferred` set (`toml_config_loader.cpp`), move **only** `logger` from `kDeferred` into `kRecognized`. Keep deferred:
- `tracer` / `meter` — **export unimplemented** at the provider layer (THE finding); backlog item.
- `log_sink` / `otlp` / `exporter` — not standalone top-level blocks (sinks nest under `[[logger.sinks]]`).
- `prometheus` — no file channel (no Prometheus config surface).
- `arena` / `message_arena` / `session_arena` / `framer_carry_arena` — deferred memory-arena surface (user decision 2026-06-20).
- `dialect_overlay` — deferred (user decision 2026-06-20).
- `tap` / `tap_consumer` — forced-deferred (bare stub; unshipped 2l; verified `tap_consumer.hpp`).

The enum **symbol** `recognized_not_yet_supported_step2` is **kept** (renaming churns 044 tests; loader-local C++ enum); only the diagnostic **message text** is generalized to "recognized but not yet supported".
**Rationale.** A precise one-key flip prevents an FR-022 over-claim and keeps the deferral boundary honest (especially for tracer/meter, whose deferral is now load-bearing — operators must not believe traces flow).

### D-7 — Deferred live-`Logger` construction with a load-time resource preflight; path resolution + redaction + collect-ALL reuse 044 mechanics

**Decision.** The fail-closed "nothing opened on a failed load" guarantee is made real by **deferring the one side-effectful step** — not by a (false) side-effect-free-construction claim. The resolve→construct order is:

1. **Resolve into a pending carrier (side-effect-free).** Each `[[logger.sinks]]` entry → a `std::unique_ptr<Sink>` via its factory (`FileSinkFactory`/`SyslogSinkFactory`/`OtlpLogSinkFactory`), and the logger scalars → a `LoggerConfig`; together they are parked as a **`PendingLogger` candidate** in a **loader-local, keyed `PendingLoggerSet`** (see "Pending carrier" below), keyed by destination (engine slot OR session index). Verified side-effect-free: the sink factories/ctors only construct the object; the filesystem/exporter work lives in `Sink::open()` (`file_sink.cpp:91`, `otlp_log_sink.cpp:159`), which the **factories never call**. No live `Logger` is constructed yet.
2. **Side-effect-free load-time resource preflight (collect-ALL).** Over the pending set's resolved sinks, before any `Logger` is built: file-sink `directory` **already exists and is a writable directory** (a stat/access check — **no `mkdir`, no probe file**; `FileSink::open()` creates/opens the live log file, not the directory; the directory must pre-exist (the preflight requires it)); OTLP `cert_source` readable + **PEM-magic-validated** (leading `-----BEGIN`/PEM-magic byte check; the full CA-bundle parse happens at sink `open()` — D-4); OTLP `endpoint` present/non-empty. Each failure appends to the same session-local `acc.size()`-delta accumulator (FR-014/FR-021). Paths resolve relative to the config-file directory via 044's `base_dir` (FR-018); credentials in a collector endpoint are redacted (FR-023).
3. **Construct only on a clean accumulator (keyed drain).** Only when the **whole-file** accumulator is empty (engine logger + every per-session override), each `PendingLogger` is moved into `std::make_shared<Logger>(std::move(cfg), std::move(sinks))` — the **sole** side-effectful step (it calls `Sink::open()` on every sink — `FileSink::open()` creates/opens the live log file, not the directory; the directory must pre-exist (the preflight requires it) — and spawns the drain thread, `logger.cpp:165-180`) — and assigned to its keyed destination: the engine slot → `bundle.engine.logger`; a session-index entry → `bundle.sessions[i].config.logger_override`. A non-empty accumulator ⇒ no `Logger` constructed, no bundle returned, **no files opened, no directory created, no thread started** (FR-015).

**Pending carrier (loader-local; outside `ConfigBundle`).** The `ConfigBundle`/`SessionConfig` hold only the **final** `shared_ptr<Logger>`, and `Logger` is copy+move-deleted, so the move-only resolved spec (`LoggerConfig` + `std::pmr::vector<std::unique_ptr<Sink>>`) needs a loader-local vessel to live in across resolve → preflight → construct. The carrier is a keyed `PendingLoggerSet`:
- `PendingLogger { LoggerConfig cfg; std::pmr::vector<std::unique_ptr<Sink>> sinks; std::string key_path; SourceLoc loc; /* target = engine slot OR session index */ }`.
- `PendingLoggerSet { std::optional<PendingLogger> engine; std::vector<PendingLogger> sessions; }` — **file-scoped**.
- **N-1 (allocator identity).** The carrier's `pmr::vector` and every minted sink allocation use the 044 `LoadOptions::resource` consistently; the move into `make_shared<Logger>` preserves that allocator identity (the load-time arena must outlive construction).
- **N-2 (file-scoped per-session lifecycle).** Per-session pending loggers are accumulated into `PendingLoggerSet::sessions` across the **whole file** — NOT constructed inside the per-session resolution loop — so a later session's error still suppresses construction of an earlier session's logger. The construct step is a single final pass over the whole set.

**Rationale.** Keeps the `shared_ptr<fixpp::log::Logger>` carrier (no `SessionConfig` change; honors D-1) and makes "nothing opened" *actually* true (construction is never reached when diagnostics exist), rather than resting on a false purity claim about the ctor. Inherits the 044 Gate-B collect-ALL fix.

**Named limitation (inherited from 017, not introduced here).** Even after the preflight, the `Logger` ctor silently disables (resets to null + bumps `sink_error_counts_`) any sink whose `open()` fails (`logger.cpp:168-174`) — a narrow time-of-check/time-of-use window between the preflight and construction. The preflight closes the determinable-at-load cases FR-014 scopes; this residual is a pre-existing 017 `Logger` defect this feature inherits. **Offered (not required this step):** the loader could read `sink_error_counts_` immediately after construction and fail closed if any sink failed to open, closing the window without touching the 017 contract — recorded as a future option, not a claim that the gap is already closed.

## Process actions (not code design)

- **Layer grounding (two distinct edges — do not conflate):** (a) the *include-layer* edge `config → log` — extend `tools/check_layers.py` config whitelist += `log` and update `.specify/architecture.md` config rows += `log` (the OTLP-log-sink header `fixpp/log/otlp_log_sink.hpp` lives in the `log` module, so this single include edge covers it; `otel` is NOT added — the loader includes no `<fixpp/otel/…>`). (b) the *link* edge `config → fixpp_log_otlp` — a **conditional** CMake link edge (+ a `FIXPP_CONFIG_HAS_OTLP` compile-def) present only when the `fixpp::log_otlp` target exists; the OTel SDK enters only via this conditional link, never as a new `check_layers.py` module. NAMED pre-merge tasks (plan §Constitution Check).
- **Catalogue/coverage:** extend the `T-043` design row (or sibling) for the logging leg + a `coverage-index.md` note (§VI.4 pre-merge).
- **Fuzz:** extend the `fuzz_toml_loader` corpus with logger blocks (§VII.7 carry-over).
- **Backlog:** record tracer/meter config as a backlog item (`REMAINING-WORK.md` 14b-residual + a `phases/phase-4/config/` note), gated on the OTLP trace/metric export pipeline.

## Open questions carried to /tasks (not blocking Gate A)

- **OQ-1:** whether the logger resolver lives in `selector_resolver.cpp` or a new `logger_resolver.cpp` — a file-organization call for `/tasks` (`selector_resolver.cpp` is already ~40 KB).
- *(OQ-2 RESOLVED at Gate A round 1)* the syslog facility name→`LOG_*` accepted set is now fixed in D-3 / data-model E-4 (the full closed set, with the build-conditional `#ifdef LOG_*` exposure rule) — no longer open.
