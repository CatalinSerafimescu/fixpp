# Implementation Plan: Native TOML config-file loading (logging pipeline)

**Branch**: `045-observability-config` | **Date**: 2026-06-20 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/045-observability-config/spec.md`

## Summary

Extend the existing `fixpp_config_toml` loader (044, merged) to hydrate the **logger** — with its file / syslog / OTLP sinks — that step 1 left as host-supplied-after-load and rejected under the "recognized-but-not-yet-supported" reason. This is the **logging leg** of step 2 (REMAINING-WORK item 14b). It is *pure config translation* (PATH-B): it hydrates the **existing** public logging value-types (`fixpp::log::Logger` + its `Sink` factories) from TOML and wires the resolved logger into the configuration bundle. No new logging machinery, **no new dependency** (`tomlplusplus` already present), **no new wire/codegen/C-ABI surface**, and **no new `fixpp_error_t` value**.

**Scope note (source-verified 2026-06-20).** The original step-2 intent also covered the tracer and metrics exporters. A read of `src/otel/providers.cpp` found the production `TracerProvider` hardwires a `NullSpanExporter` (discards all spans, ignores `endpoint`/`cert_path`) and `MeterProvider` has no metric reader — the OTLP **trace/metric** export is unimplemented at the provider layer, so configuring `[tracer]`/`[meter]` would set dead knobs. Tracer/meter config is therefore **deferred to the backlog** (gated on the export pipeline shipping; tracked in `REMAINING-WORK.md` + `phases/phase-4/config/`). The OTLP **log** sink (`src/log/otlp_log_sink.cpp`) is fully real — it wires `OtlpHttpLogRecordExporterFactory` with the configured `endpoint`/`cert_source` — so the logging leg is the honest, complete deliverable.

**Technical approach.** One new top-level TOML block resolved inside the existing `src/config/selector_resolver.cpp` registry: `[logger]` (a *composite* selector — logger-level settings plus an ordered `[[logger.sinks]]` array), plus its per-session counterpart for the logger override. The loader mints each sink via its existing factory (`FileSinkFactory` / `SyslogSinkFactory` / `OtlpLogSinkFactory`) into a loader-local pending carrier and, only on a clean whole-file accumulator, heap-constructs the `Logger` (which is copy-AND-move-deleted, so it can only live behind a `shared_ptr`) as the sole end-of-load step. The configuration bundle's `EngineEstablishment` slice grows **one** `shared_ptr<fixpp::log::Logger>` field; the per-session override is written directly onto the existing `SessionConfig::logger_override` member. It preserves every text-boundary invariant 044 established — no-implicit-default, closed-enum canonical spellings, frozen-at-open, fail-closed, credential redaction, collect-ALL per-key diagnostics — and flips exactly the top-level `logger` key from the step-1 deferred set to supported, keeping tracer/meter/arenas/dialect-overlay/tap deferred.

## Technical Context

**Language/Version**: C++23 (Clang 22 primary; GCC sanity; MSVC) — `[const §II.1]`
**Primary Dependencies**: **No new dependency.** Reuses `tomlplusplus/3.4.0` (already a 044 PRIVATE dep of `fixpp_config_toml`) + the existing `fixpp_log` value-types and sink factories. The loader **always** links `fixpp_log` (file/syslog sinks + the `Logger`/`Sink` interface). **OTLP log-sink support is compile-conditional** (mirroring the syslog `FIXPP_HAS_SYSLOG` pattern): `OtlpLogSink` lives in a **separate** `fixpp_log_otlp` target (`src/log/CMakeLists.txt:38`, guarded `if(TARGET opentelemetry-cpp::api)`), NOT in `fixpp_log` — so the OTel SDK is **not** a transitive dep of `fixpp_log`. When the `fixpp::log_otlp` target exists, `fixpp_config_toml` adds a **conditional link edge** `config → fixpp_log_otlp` + a `FIXPP_CONFIG_HAS_OTLP` compile-def, and `kind="otlp"` resolves; when absent, `kind="otlp"` → `invalid_or_contradictory_selector` (build-unavailable, exactly like syslog on non-POSIX — FR-013), keeping a non-OTLP config build free of the OTel SDK. (The *include-layer* edge is just `config → log`: the OTLP-log-sink header lives in the `log` module — see the grounding action below.)
**Storage**: N/A (the loader configures logging sinks; it owns no storage).
**Testing**: GoogleTest (`[const §VII.1]`); TDD red-green-refactor (`[const §VII.3]`). Negative-file battery (one cell per logger error class) + a field-for-field equivalence test vs a hand-built `EngineConfig`/`SessionConfig` logger + a multi-session override test + a platform-unavailable-sink cell.
**Target Platform**: Linux primary; platform-neutral host code. **Platform-conditional surface:** the syslog sink is POSIX-only (`FIXPP_HAS_SYSLOG`); the loader must reject `kind="syslog"` under the invalid/contradictory-selector reason on a build where the type is not compiled (FR-013).
**Project Type**: In-process C++23 library component — the existing opt-in adjacent `fixpp_config_toml` target (no new target).
**Performance Goals**: None constitutional — the loader is a **cold, one-shot, pre-open path**. `[const §XV.1]` does not apply (the configured logger's own runtime allocation discipline is governed by its 017 contract, unchanged here).
**Constraints**: Fail-closed before any `Session::open`; no implicit defaults; closed-enum canonical spellings only; frozen-at-open; redact credentials in all diagnostics; the new `log` link edge must NOT enter the embeddable core (the loader stays an opt-in adjacent target).
**Scale/Scope**: 1 new top-level block resolver (logger composite) + 3 sink param-struct maps + 1 syslog-facility name→int map + 1 power-of-2 capacity validation + the `EngineEstablishment` 1-field extension + per-session logger override wiring + the negative-file battery. Sizing **SMALL–MEDIUM** — narrower than 044 (no new module, no new dependency, no new diagnostic reason-class) and narrower than the original step-2 (tracer/meter dropped: no provider/init-status/separate-blocks complexity).

## Constitution Check

*GATE: evaluated article-by-article against `.specify/constitution.md` v0.3. Re-checked after Phase 1.*

| Article | Verdict | Notes |
|---|---|---|
| **I — Identity/Mission** | PASS | No FIX-version-coverage impact; config supply mechanism only. |
| **VI — Spec Coverage** | PASS (with action) | Design-blessed (no normative FIX section mandates a TOML logging loader) → **design catalogue row** under `[const §XV.16]`, not `OFFICIAL` (§VI.3). **Action (NAMED pre-merge task, both files):** extend the 044 `T-043` design row (or add a sibling) in `spec/feature-catalogue.md` for the logging leg + a design-choice note in `spec/coverage-index.md` (§VI.4). **Normative References** present in spec (§VI.5). |
| **II — Language/Compilers** | PASS | C++23, no new compiler constraints. |
| **III — Build/Deps** | **PASS — no new dep** | Reuses the already-pinned `tomlplusplus/3.4.0`. The `fixpp_config_toml` target newly links `fixpp_log` unconditionally + a **conditional** `fixpp_log_otlp` link (gated by `FIXPP_CONFIG_HAS_OTLP`, present only when the OTel SDK is available) — both already in the build graph. No `conanfile.py` change. |
| **IV/V — Distribution/License** | PASS | No new dependency ⇒ no new license surface. Loader is AGPL library code. |
| **VII — Testing/TDD** | PASS (fuzz carries over) | TDD mandatory; negative-file battery + equivalence + multi-session-override tests. **§VII.7 fuzzing:** the loader remains a TOML parser front-end; the existing `fuzz_toml_loader` harness (044 T037) covers the parse + noexcept-boundary path. **Action:** extend the fuzz corpus with logger blocks (logger/sinks) so the new resolver branches are reachable from malformed input; the §VII.7 gate carries over (Gate-B ≥10-min run). |
| **VIII — Perf** | N/A | Cold load-time path; no bench. |
| **IX — Coverage/Sanitizers** | PASS | 95/85 on touched files; ASan/UBSan/TSan via `/speckit-verify`. The loader's *resolution + preflight* are single-threaded synchronous code, but a **clean** load ends by constructing the live `Logger`(s) — and the `Logger` ctor **opens every sink and spawns its drain OS thread** (`logger.cpp:165-180`); it is NOT side-effect-free (corrected from the round-1 framing). One drain thread is started per constructed logger (engine + each per-session override). **Fail-closed mechanism (research D-7):** the loader mints sinks side-effect-free, runs a load-time resource preflight (collect-ALL), and calls `make_shared<Logger>` **only when the accumulator is empty** — so a failed multi-error load constructs **no** logger and leaves nothing opened (no files, no threads). **TSan scope:** the constructed-logger threads (and the inherited 017 silent sink-disable on a post-preflight `open()` failure) are governed by the 017 lifecycle contract; the loader hands the `shared_ptr<Logger>` to the host, which owns shutdown. |
| **X — ABI** | **PASS — pinned** | **No C-ABI change. No new `fixpp_error_t` value.** Diagnostics are the C++-only loader-local `LoadDiagnostic`; the OTLP sink's existing `otel_export_failed` is a runtime concern of the sink, not surfaced by the loader. Never crosses `include/fix/c_api.h`. |
| **XI — Concurrency** | **PASS — Threading trigger (Gate-A)** | No coroutine/strand/executor change in the loader. But a **clean** load constructs the live `Logger`(s), and the `Logger` ctor starts the drain OS thread (`logger.cpp:180`) — so this feature **does** start threads at end-of-load. Per Appendix A this is the **Threading/concurrency** Gate-A trigger (the honest additional trigger beyond Security; Appendix A has no "public C++ API" row — the additive `shared_ptr<Logger>` field does not itself trip a generic API trigger). The thread lifecycle is the existing 017 `Logger` contract, owned by the host; the loader only constructs. |
| **XII — Security** | **PASS — constrained (Gate-A trigger)** | The OTLP log sink carries **TLS material**: `cert_source` (PEM CA) ⇒ **Gate A mandatory** (Appendix A: TLS config / cert-material). The loader MUST resolve the PEM path relative to the config-file directory (FR-018), fail closed on an unreadable / non-PEM-magic cert at load (FR-014 — full CA-bundle parse deferred to sink open()), and **redact** any credential embedded in a collector endpoint (FR-023). No security profile / transport-factory surface is touched (that was 044). |
| **XIII — Observability** | **PASS — logging leg in scope** | This feature resolves the **existing** `fixpp::log` logger/sinks; it adds no new logging machinery, no new sink kind, no new metric/span. **Tracer/meter are deferred** (provider OTLP export unimplemented — source-verified; backlog item). `prometheus`/`exporter`/`log_sink` stay deferred (no standalone file channel). |
| **XIV — Pluggable Interfaces** | PASS | Resolves the **existing** `Sink` (4-pure-virtual plugin) + `Logger`; adds **no new PUBLIC type, no new pluggable interface, no new pure-virtual, no ABI surface** — only one loader-local internal `PendingLoggerSet` carrier. The `Sink` factories (`FileSinkFactory`/`SyslogSinkFactory`/`OtlpLogSinkFactory`) already exist. |
| **XV — Banned Patterns** | PASS | §16 blesses TOML. §1 hot-path-alloc N/A (cold path). **The logger's `ring_resource` (`pmr::memory_resource*`) is the deferred arena surface — the loader MUST NOT let the file select it** (FR-010); it stays at the built-in default. |
| **XVI — Spec-Kit Workflow** | PASS | `/clarify` DONE (Session 2026-06-20; the syslog-platform decision stands; the two exporter clarifications are moot post-scope-cut). `/analyze` MANDATORY before `/implement` (Security trigger). |
| **XVII — Codex Gates** | PASS (gates pending) | **Gate A MANDATORY** (Security trigger — OTLP-sink TLS cert; **also** Threading trigger — a clean load starts the logger drain thread) before `/tasks`. Gate B mandatory pre-merge. `/speckit-verify` after `/implement`. User `/plan` sign-off MANDATORY (Appendix A). |
| **XVIII — Roadmap** | PASS | No protocol shipping; v1.0 in-scope. |

**No violations requiring Complexity Tracking.** No new PUBLIC type, no new pluggable interface, no new pure-virtual, no ABI surface — only one loader-local internal `PendingLoggerSet` carrier; zero new dependencies, zero new modules. The only structural change is one new `shared_ptr<fixpp::log::Logger>` field on the existing `EngineEstablishment` struct + new resolver branches in the existing `selector_resolver.cpp`.

**Layer-grounding action (the step-2 analogue of 044's new-module grounding).** Two **distinct** edges — the round-1 plan conflated them:

1. *Include-layer edge* `config → log` — captured by `check_layers.py` + `architecture.md`. `tools/check_layers.py` currently whitelists `config → {core, session, dictionary, tls, transport}` — no `log`. Resolving the logger requires `src/config/*.cpp` to `#include <fixpp/log/…>` (including `fixpp/log/otlp_log_sink.hpp`, which lives in the `log` module), which the layer gate would flag. So a single `config → log` include edge covers **all** logger/sink headers, OTLP included.
2. *Link edge* `config → fixpp_log_otlp` — a **conditional CMake link target** (+ `FIXPP_CONFIG_HAS_OTLP` compile-def), present only when `opentelemetry-cpp` is available. The OTel SDK enters **only** via this conditional link, NOT via a new `check_layers.py`/`architecture.md` module — `check_layers.py` operates on include layers, not link targets.

Three NAMED tasks (the files are outside the design bundle):
- **Task (check_layers.py):** extend the `config` whitelist to add `log` → `{core, session, dictionary, tls, transport, log}`. Re-assert the negative edge: no `core`/`session`/`dict`/`tls`/`transport`/`log` module includes or links `config`. (No FORBIDDEN-edge conflict.) **`otel` is NOT added** — the loader includes no `<fixpp/otel/…>`; the OTel SDK is reached only via the conditional `fixpp_log_otlp` **link** edge, not an include edge.
- **Task (architecture.md):** update the `config` module row (line 67) + the dependency-table row (line 129) to add `log` to the config module's allowed dependencies (pulled only by the opt-in adjacent loader).
- **Task (CMakeLists.txt):** in `src/config/CMakeLists.txt`, link `fixpp_log` unconditionally; add a `if(TARGET fixpp::log_otlp)` block linking `fixpp_log_otlp` + `target_compile_definitions(... FIXPP_CONFIG_HAS_OTLP)` (mirrors the syslog gate). When the def is absent, the resolver maps `kind="otlp"` → `invalid_or_contradictory_selector`.

**Mandatory controls (Appendix A) for this feature:** `/clarify` ✅done · `/analyze` ⏳required · **Codex Gate A** ⏳required (**Security** — OTLP-sink TLS cert [primary] · **Threading** — a clean load constructs the logger drain thread) · user `/plan` sign-off ⏳required.

## Project Structure

### Documentation (this feature)

```text
specs/045-observability-config/
├── plan.md              # this file
├── research.md          # Phase 0 — decisions D-1..D-7 (bundle wiring, composite logger, sink resolvers, tracer/meter deferral, deferred-key split)
├── data-model.md        # Phase 1 — E-1..E-4 entities + field-population map + sink→factory param tables
├── quickstart.md        # Phase 1 — example logger TOML + host stub
├── contracts/
│   └── observability_config.hpp   # Phase 1 — the EngineEstablishment extension + resolver entry points (illustrative)
├── checklists/
│   └── requirements.md  # spec-quality checklist (from /specify)
└── tasks.md             # Phase 2 — created by /speckit-tasks (NOT here)
```

### Source Code (repository root = the library submodule)

```text
include/fixpp/config/
├── config_bundle.hpp            # AMEND: EngineEstablishment gains ONE field — logger (shared_ptr<fixpp::log::Logger>) (E-2)
└── (load_diagnostic.hpp, toml_config_loader.hpp unchanged — no new reason-class, no entry-point signature change)

src/config/                      # existing fixpp_config_toml target — links += fixpp_log (+ conditional fixpp_log_otlp)
├── CMakeLists.txt               # AMEND: link fixpp_log always; if(TARGET fixpp::log_otlp) link fixpp_log_otlp + define FIXPP_CONFIG_HAS_OTLP
├── toml_config_loader.cpp       # AMEND: recognize_keys() — move {logger} kDeferred→kRecognized; call logger resolver
├── selector_resolver.cpp        # AMEND: new resolve_engine_logger + resolve_log_sink + per-session logger_override resolve
├── scalar_mappers.cpp           # AMEND (if needed): syslog-facility name→int map; capacity power-of-2 validation helper
└── (logger resolver code MAY live in a new src/config/logger_resolver.cpp if selector_resolver.cpp grows too large — a /tasks call)

tests/config/                    # existing test dir
├── test_load_logger.cpp             # NEW — US1 equivalence (logger + file/syslog/otlp sinks) vs hand-built config
├── test_load_logger_negative.cpp    # NEW — US2 negative battery: unknown sink kind, missing endpoint,
│                                    #   out-of-range capacity/batch, syslog-on-non-POSIX, unreadable cert,
│                                    #   gRPC-transport deferred; collect-ALL assertion
├── test_load_logger_overrides.cpp   # NEW — US3 engine-default + per-session logger override
├── test_load_deferred_surface.cpp   # NEW/AMEND — US4 tracer/meter/arena/dialect_overlay/tap recognized_not_yet_supported
└── fixtures/                        # NEW .toml fixtures (good + one-error-per-class logger files)

tools/check_layers.py            # AMEND: config whitelist += {log} (grounding action)
.specify/architecture.md         # AMEND: config module row + dependency table += log (grounding action)
spec/feature-catalogue.md        # AMEND: extend T-043 (or sibling) design row for the logging leg (pre-merge §VI.4)
spec/coverage-index.md           # AMEND: design-choice note (pre-merge §VI.4)
fuzz/                            # AMEND: extend fuzz_toml_loader corpus with logger blocks
```

**Structure Decision**: **No new module, no new target, no new dependency.** The feature is an in-place extension of the existing `fixpp_config_toml` component (044). The new build edges are `fixpp_config_toml → fixpp_log` (unconditional) plus a **conditional** `fixpp_config_toml → fixpp_log_otlp` link (present only when the OTel SDK is available, gated by `FIXPP_CONFIG_HAS_OTLP`), grounded in `check_layers.py` + `architecture.md` + the CMake gate (the layer-grounding action above). The public surface change is additive: `EngineEstablishment` gains one `shared_ptr` field; the loader entry point and `LoadResult`/`LoadDiagnostic`/`reason_class` are unchanged.

## Complexity Tracking

> No Constitution Check violations. Table intentionally empty.

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| — | — | — |

## Gate A

- Round 1 applied 2026-06-20: Codex P1=2 P2=4 P3=1; Opus post-judging P1=2 P2=4 P3=3; rewrite addresses root causes (1) deferred live-Logger construction + load-time resource preflight, (2) OTLP compile-conditional + corrected build/link grounding, (3) localized contract fixes (syslog facility set, unit-suffixed durations, empty-endpoint, stale cite) + Threading trigger. Reviews: research/reviews/codex_045-observability-config_gate_a_review.md, research/reviews/opus_045-observability-config_gate_a_adversarial_review.md.
- Round 2 applied 2026-06-20: Codex P1=0 P2=2 P3=2; Opus post-judging P1=0 P2=2 P3=4; rewrite addresses (P2-a) keyed loader-local PendingLogger carrier + construct-on-clean-accumulator (folds N-1 allocator-identity, N-2 file-scoped per-session), (P2-b) side-effect-free directory preflight (stat/access only; creation at FileSink::open on clean load), (P3-c) corrected cert preflight to readable+PEM-magic [make_file_cert_source unusable for CA-only — throws on empty leaf/key], (P3-d) swept stale D-2 immediate-construction wording. Reviews: research/reviews/codex_045-observability-config_gate_a_2_review.md, research/reviews/opus_045-observability-config_gate_a_2_adversarial_review.md.
- Round 3 (review-only, no rewrite — budget exhausted): Codex P1=0 P2=0 P3=2; Opus post-judging P1=0 P2=0 P3=2 → CONVERGED. Two P3 doc nits fixed as convergence polish (plan "no new type" precision; FileSink::open() does-not-create-directory factual correction across 6 sites + quickstart pre-exist note). Reviews: research/reviews/codex_045-observability-config_gate_a_3_review.md, research/reviews/opus_045-observability-config_gate_a_3_adversarial_review.md.

### Round 1 — disagreements

- **Codex closing ("structural — re-run Spec-Kit from scratch") is REJECTED.** The Opus adversarial judge ruled the bundle convergeable in a single in-loop rewrite: every defect traces to shared root causes (design artifacts written against headers while `research.md` claimed it read the `.cpp`; a build-model conflation; a few localized contract slips), all in-bundle and fixable without re-running the cycle. The spec's FRs (fail-closed-before-open, collect-ALL, per-key taxonomy, behaviorally-indistinguishable, optional/additive) are sound goals, all achievable under the deferred-construction shape.
- **Codex #3 severity DOWNGRADED P2 → P3.** Codex justified it via a "public C++ API" Gate-A trigger, but Appendix A has **no "public C++ API" trigger row** — so the additive `shared_ptr<Logger>` field does not itself trip a generic API trigger. The honest additional trigger is **Threading/concurrency** (a clean load starts the logger drain thread), now listed alongside Security. Net impact is small: Gate A is **already** mandatory via Security (TLS cert) regardless, so this only corrects the Constitution Check's honesty (the §XI `N/A` cell → Threading-trigger PASS), not whether Gate A runs.

### Round 2 — disagreements

- **Codex's P3-c proposed mechanism REJECTED.** Codex proposed reusing `tls::file_cert_source::make_file_cert_source(Config{.ca_bundle_path=cert_source})` as the load-time CA-PEM preflight. The Opus judge verified in-source that this is **wrong**: `make_file_cert_source`/`load()` **unconditionally throws if `leaf_path`/`private_key_path` is empty** (`file_cert_source.cpp:237-242`) — it is a client-identity cert source — and the OTLP `cert_source` is a **CA trust bundle only** (`otlp_log_sink.cpp` → `ssl_ca_cert_path`). Calling it with a CA-only `Config` would fail with `tls_cert_load_failed`, not validate the PEM. The only real CA-bundle parser is the private `Impl::load_ca_bundle`; `is_pem()` is a magic-byte check. Corrected to **readable + PEM-magic-validated at load** (full CA-bundle parse deferred to sink `open()`); a dedicated public CA-bundle-parse TLS helper is recorded as a possible future tightening, not required this step.
