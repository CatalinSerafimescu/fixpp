# Feature Specification: Native TOML config-file loading (logging pipeline)

**Feature Branch**: `045-observability-config`
**Created**: 2026-06-20
**Status**: Draft
**Input**: User description: "Native TOML config-file loading for the observability/diagnostics pipeline — STEP 2 of the 2-step 044 rollout (item 14b). Extends the existing config loader to hydrate the observability objects step 1 deferred. **Scoped during planning (2026-06-20) to the LOGGING leg only** — the logger and its file/syslog/OTLP sinks, all of which are fully implemented and export real output. The tracer and metrics exporters are **deferred to the backlog**: a source-level check found their OTLP export is an unimplemented provider-layer stub (spans/metrics are discarded regardless of any configured endpoint), so configuring them would ship dead knobs. Same architecture as step 1: synchronous cold-path loader, fail-closed before Session::open, per-key collect-ALL diagnostics, parser isolated outside the embeddable core, no new wire/error/generated-code/C-ABI surface."

## Overview & Context *(informative)*

Step 1 (044-toml-session-config, merged) brings a FIX **session** up entirely from a TOML file and fails closed before `Session::open`. It deliberately deferred the **observability/diagnostics pipeline**, recognizing those keys but rejecting them under a distinct "recognized-but-not-yet-supported (step 2)" reason rather than silently ignoring them.

This feature is **step 2**, scoped (during planning, after a source-level verification) to the **logging leg**: it makes the **logger** and its **sinks** (file, syslog, OTLP) configurable from the same file. An operator who today brings a session up from a config file but still wires logging in host code can now describe where logs go — rotating local files, the host syslog, and/or an OpenTelemetry log collector — entirely in the file. The host stub shrinks toward "inject application callbacks, provide the executor, load, open."

**Why logging only.** The original step-2 intent also covered the tracer and metrics exporters. A source-level check during planning (2026-06-20) found that the tracer/meter **OTLP export is an unimplemented stub** at the provider layer — the production `TracerProvider` discards all spans via a null exporter and ignores the configured endpoint/cert entirely; the `MeterProvider` has no metric reader. Configuring `[tracer]`/`[meter]` would therefore set knobs with **no runtime effect** — an operator would point at a collector, receive zero telemetry, and get no error. Rather than ship dead knobs, tracer/meter config is moved to the backlog, **gated on the OTLP trace/metric export pipeline actually shipping** (a provider-layer item). The logging leg, by contrast, is fully implemented end-to-end (the OTLP **log** sink genuinely exports records over HTTP to the configured endpoint), so it is the honest, complete deliverable for this step.

The logger and its sinks all already exist as built-in, parameterizable implementations; step 1 already proved the *kind + parameters* selector pattern and the per-key fail-closed diagnostic taxonomy. This feature is therefore primarily a **translation-and-wiring** extension of the existing loader, plus the small data-model extension needed to carry the resolved logger through the configuration bundle (step 1 left it host-supplied-after-load).

## Glossary *(informative)*

- **Logging pipeline** — the configurable diagnostic output of this step: the **logger** (a logging-record ring drained to one or more **sinks**).
- **Sink** — a single destination for log records. Built-in kinds this step: **file** (rotating local files), **syslog** (the host syslog daemon, where available), and **OTLP** (an OpenTelemetry-protocol log collector over HTTP).
- **Composite selector** — the logger entry, which is not a single *kind + parameters* but a small block of logger-level settings **plus an ordered list** of sink selectors.
- **Engine-scope vs session-scope** — a logger configured once for the whole engine (the default) versus one configured per session as an override.
- **Fail-closed** — on any error the loader returns no usable configuration and the affected configuration is never opened; it never silently substitutes a default, a no-op output, or a partial configuration.

## Clarifications

### Session 2026-06-20

- Q: When a file selects a real sink kind that is unavailable on the current platform/build (e.g. syslog on a non-POSIX build), which diagnostic reason class? → A: Invalid/contradictory selector — a hard fail-closed config error (the kind is real but unusable on this deployment), never silently skipped and not conflated with the recognized-but-deferred reason.
- *(Superseded)* Two earlier clarifications about the tracer/meter exporter (provider-init fail-closed vs noop fallback; separate vs combined `[tracer]`/`[meter]` blocks) are **moot** as of the same-day source-level finding that the tracer/meter OTLP export is unimplemented: those legs are deferred to the backlog, so no exporter-init or exporter-block-shape question applies in this step.

### Session 2026-06-20 (Gate A round 1)

- Q: The logger constructor is not side-effect-free (it opens every sink and starts the drain thread, silently disabling any sink whose open fails) — so how does the loader keep its fail-closed "nothing opened on a failed load" guarantee? → A: Defer the one side-effectful step. The loader resolves the `LoggerConfig` + mints each sink object (side-effect-free), runs a side-effect-free load-time resource preflight (file-sink directory already-exists + writable [stat/access only], OTLP cert readable + PEM-magic-validated, OTLP endpoint present) collecting ALL diagnostics, and constructs the live logger only at end-of-load when the whole-file accumulator is empty; on any error no logger is constructed (FR-014/FR-015). The constructor's silent sink-disable remains a named, inherited 017 limitation (a narrow preflight→construct TOCTOU window).
- Q: Is `kind="otlp"` always available? → A: No — OTLP log-sink support is compile-conditional (it requires the OpenTelemetry SDK / the `fixpp_log_otlp` target). On a build where it was not compiled in, `kind="otlp"` is rejected under the invalid/contradictory-selector reason (build-unavailable), mirroring `kind="syslog"` on a non-POSIX build (FR-013); a non-OTLP build therefore does not drag in the OTel SDK.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Configure logging from the config file (Priority: P1)

An operator who already establishes sessions from a config file now also describes their logging there: a logger with one or more sinks — a rotating file plus an OTLP log collector, say. They point fixpp at the file, inject only their application callbacks, and bring the engine up with logging active and routed exactly as the file specifies — no per-object programmatic logging wiring.

**Why this priority**: This is the migration MVP for step 2 — it removes the largest remaining block of mandatory host wiring (log routing) for the common deployment, and every configured destination produces real output. Delivered alone it lets an adopter move their log-routing operational configuration into the same file that already carries session establishment.

**Independent Test**: Author a file that configures a logger with a representative set of sinks (file + OTLP); load it; confirm the resulting configuration carries a logger equivalent to the hand-built one (same sink kinds, order, and parameters) and that the engine logs to those destinations.

**Acceptance Scenarios**:

1. **Given** a file with a logger block naming an ordered list of sinks (a file sink and an OTLP sink), **When** loaded, **Then** the configuration carries a logger that drains to exactly those sinks, in order, with the parameters given.
2. **Given** a file configuring a file sink with a directory, rotation size, and retention count, **When** loaded, **Then** the session logs to rotating files at that location with those limits.
3. **Given** a file configuring an OTLP log sink with a collector endpoint and CA certificate, **When** loaded, **Then** the configuration carries an OTLP sink that exports records to that endpoint using that certificate.
4. **Given** a file that omits the logger block entirely, **When** loaded, **Then** no logger is created and the configuration is byte-for-byte the step-1 result (logging is fully optional; absence is not an error).

### User Story 2 - A bad logging config is rejected before anything opens, with an actionable diagnostic (Priority: P1)

An operator misconfigures the logger — an unknown sink kind, a missing required endpoint, an out-of-range buffer capacity or batch size, a sink unavailable on this platform, or an unreadable collector certificate. The loader rejects the file **before any session opens** and reports a diagnostic that names the specific offending key, the reason, and where in the file it is. No logger is ever left half-built, and the loader never silently falls back to a no-op output that would leave the operator believing logs are flowing when they are not.

**Why this priority**: Fail-closed-before-open is the same non-negotiable invariant as step 1, and it matters especially for logging: a silent fallback to a no-op logger is a classic operational trap (the operator thinks they have audit logs and they do not). Moving this configuration into text must not weaken the guarantee that a misconfigured output is loud, not silent.

**Independent Test**: Feed the loader a battery of deliberately broken logger files — one per error class — and assert each is rejected, the diagnostic names the correct key and reason, no session reaches an open state, and in no case is a no-op logger silently substituted.

**Acceptance Scenarios**:

1. **Given** a file naming an unknown sink kind, **When** loaded, **Then** loading fails identifying the unknown kind and the legal set, and no session opens.
2. **Given** a file with a sink missing a required parameter (e.g. an OTLP sink with no endpoint), **When** loaded, **Then** loading fails identifying the missing key, and no session opens.
3. **Given** a file with a type-correct but out-of-range logger value (e.g. a ring capacity that is not a power of two, or a zero export batch size), **When** loaded, **Then** loading fails with a range diagnostic, and no session opens.
4. **Given** a file selecting a sink kind that is not available on the current platform (e.g. syslog on a non-POSIX build), **When** loaded, **Then** loading fails under the invalid/contradictory-selector reason naming the unavailable kind — it is **not** silently skipped.
5. **Given** a file whose OTLP sink references an unreadable or non-PEM collector certificate (caught by the load-time readability + PEM-magic check), **When** loaded, **Then** loading fails attributing the failure to that sink/selector, and no session opens.
6. **Given** any logger load failure in a file that also has other errors, **When** loaded, **Then** all errors (logger and otherwise) are reported together in one pass, and nothing is left opened.

### User Story 3 - Engine-default logger with per-session overrides (Priority: P2)

An operator configures a logger once for the whole engine, then overrides it for a specific session that needs different routing (a noisy session sent to its own log sink, say).

**Why this priority**: Multi-session deployments routinely want a shared default plus targeted per-session exceptions; this is the logging analogue of step 1's shared-defaults / per-session-override model. It builds on US1 and is independently demonstrable once the engine-level path exists.

**Independent Test**: Author a multi-session file with an engine-level logger plus a per-session logger override; load it; confirm the overriding session carries the override logger while other sessions carry the engine default.

**Acceptance Scenarios**:

1. **Given** a file with an engine-level logger and a session that overrides the logger, **When** loaded, **Then** that session carries the override logger and all other sessions carry the engine-level logger.
2. **Given** a file with engine-level logging and a session that does **not** override it, **When** loaded, **Then** that session inherits the engine-level logger (no per-session logger object).

### User Story 4 - The deferred surface stays visible, not silently ignored (Priority: P3)

An operator who puts a tracer/metrics block, a memory-arena selection, a dictionary dialect overlay, or a diagnostic-tap block in the file gets a clear "recognized but not yet supported" diagnostic rather than silence. The loader continues to distinguish a known-deferred key from an unknown-key typo, exactly as step 1 did — the supported set has grown by the logger keys, and everything else stays deferred.

**Why this priority**: Keeping the deferral boundary loud is the same safety property step 1 established; it prevents an operator from believing a not-yet-built capability is active (especially important for tracer/meter, whose export is genuinely unimplemented). It is a refinement on top of the working logging path rather than a prerequisite for it.

**Independent Test**: Author files containing each still-deferred key (a tracer block, a meter block, a memory-arena selector, a dialect overlay, a tap block) and confirm each is rejected under the distinct "recognized-but-not-yet-supported" reason with its key named, distinct from the unknown-key typo reason.

**Acceptance Scenarios**:

1. **Given** a file containing a `[tracer]` or `[meter]` block, **When** loaded, **Then** loading fails under the "recognized-but-not-yet-supported" reason naming that key (the OTLP trace/metric export pipeline is not yet implemented), distinct from an unknown-key typo.
2. **Given** a file containing a memory-arena selector or a dictionary dialect-overlay selector, **When** loaded, **Then** loading fails under the same deferred reason.
3. **Given** a file containing a diagnostic-tap block, **When** loaded, **Then** loading fails under the same deferred reason (the tap surface is not yet defined).

### Edge Cases

- A logger block with **zero sinks**: treated as a configuration error (a logger that drains nowhere is almost certainly a mistake), reported as a missing/empty required list — not silently accepted as a no-op logger.
- A logger block that names the **same sink kind twice** (two file sinks to different directories, two OTLP sinks to different collectors): valid — sinks are an ordered list, and duplicate kinds with distinct parameters are a legitimate fan-out.
- A sink referencing a **relative filesystem path** (a file-sink directory, an OTLP CA certificate path): resolves against the directory containing the config file, consistent with step 1's relocatable-bundle rule — not the process working directory.
- An OTLP sink with an **empty endpoint**: the underlying sink treats an empty endpoint as a no-op exporter (records are buffered and silently dropped on flush) — so a file-explicit empty endpoint would silently lose all log export. The loader is deliberately stricter than the sink default: a present-but-empty `endpoint` is treated as a missing/empty required value and rejected fail-closed (a file-explicit empty is far more likely a mistake than intent, and the silent-drop sink behavior makes the loud rejection the safe choice).
- An OTLP sink selecting the **non-default export transport** (the gRPC transport, recognized but not yet wired): recognized but rejected under the "recognized-but-not-yet-supported" reason, distinct from an unknown-key typo.
- A logger **ring/buffer memory resource**: not file-selectable this step — it belongs to the deferred memory-arena surface. The file configures the logger's behavioral settings (capacity, overflow policy, drain timeout) but the underlying buffer allocator stays at its built-in default; an attempt to select it is treated as the deferred arena surface (US4).
- A logger configured but the **engine never started**: configuration is frozen at load/open exactly as in step 1. The loader resolves the logger settings and mints each sink object (side-effect-free — sink construction opens no files/exporters), runs a **side-effect-free** load-time resource preflight (file-sink directory already exists and is writable — a stat/access check, no directory creation and no probe file; OTLP certificate readable and PEM-magic-validated; OTLP endpoint present), and constructs the live logger — the one side-effectful step that opens sinks (`FileSink::open()` creates/opens the live log file, not the directory; the directory must pre-exist (the preflight requires it)) and starts the drain thread — only at the very end of a fully clean load. On any error, no logger is constructed, so a failed multi-error load creates no directories, opens no files, and starts no threads (nothing is left opened).

## Requirements *(mandatory)*

### Functional Requirements

**Loading & translation (continuity with step 1)**

- **FR-001**: The system MUST extend the existing native TOML loader to resolve the logger (with its sinks) into the existing public logging value-types — pure config translation, introducing no parallel runtime adapter and no logging behavior beyond what the equivalent programmatic configuration produces.
- **FR-002**: A logger configuration produced from a file MUST be behaviorally indistinguishable from the equivalent programmatically-built logger (same sink kinds/order/parameters).
- **FR-003**: The logger configuration MUST be entirely optional: a file that omits the logger block MUST load to the identical result step 1 produced (no logger created), and absence MUST NOT be an error.
- **FR-004**: The parser/loader dependency MUST remain isolated outside the embeddable core exactly as in step 1; a host that does not use file-based configuration MUST take on no additional dependency from this feature.

**Scope — in (the logger and its sinks)**

- **FR-005**: The loader MUST resolve a **logger** as a composite selector: logger-level settings (record-buffer capacity, overflow policy, drain timeout) plus an **ordered, non-empty list of sink selectors**. The resolved logger MUST drain to exactly the listed sinks, in the listed order.
- **FR-006**: The loader MUST resolve the built-in **sink** kinds from *kind + parameters*: **file** (output directory, base file name, per-file size limit, retained-file count, fsync policy), **syslog** (identity string, facility — subject to platform availability per FR-013), and **OTLP** (collector endpoint, optional collector-TLS certificate, export timeout, max export batch size, max export retries — subject to build availability per FR-013, since the OTLP log-sink support is compiled only when the OpenTelemetry SDK is present).
- **FR-007**: The loader MUST carry the resolved logger through the configuration bundle so the host opens sessions with it already wired — extending the bundle's engine-establishment slice to hold the logger (step 1 left it host-supplied-after-load).

**Scope — per-object rules**

- **FR-008**: The logger MUST be configurable at engine-scope (a default applied to all sessions) and overridable per session; a per-session override MUST replace the engine default for that session only.

**Scope — out (deferred / forced-deferred)**

- **FR-009**: The loader MUST NOT, in this step, configure the **tracer or metrics exporter**; their OTLP export is unimplemented at the provider layer (configuring them would have no runtime effect), so a `[tracer]`/`[meter]` block MUST be rejected under the "recognized-but-not-yet-supported" reason, never silently accepted. (Backlog: gated on the OTLP trace/metric export pipeline shipping.)
- **FR-010**: The loader MUST NOT configure memory-arena selection (including the logger's buffer memory resource) or the dictionary dialect overlay; these remain deferred to a later step and MUST be rejected under the "recognized-but-not-yet-supported" reason, never silently ignored.
- **FR-011**: The loader MUST NOT configure the diagnostic **tap**; the tap configuration surface is not yet defined (it depends on the unshipped tap feature), so a tap block MUST be rejected under the "recognized-but-not-yet-supported" reason, never silently ignored.
- **FR-012**: The loader MUST continue to refuse host-supplied behaviors no file can express (application callbacks, the executor instance, host-written custom kinds, a shared pre-existing execution context), unchanged from step 1.

**Validation & safety (fail-closed)**

- **FR-013**: A sink kind that is **real but not available on the current platform/build** MUST be rejected under the **invalid/contradictory selector** reason with a clear diagnostic naming the unavailable kind (Clarifications 2026-06-20); the loader MUST NOT silently skip it, silently drop it from the chain, conflate it with the recognized-but-deferred reason, or report it as an unknown enum. This covers two build-conditional cases symmetrically: `kind="syslog"` on a build without `FIXPP_HAS_SYSLOG` (non-POSIX), and `kind="otlp"` on a build where the OTLP log-sink support was not compiled in (the OpenTelemetry SDK was absent, so the `fixpp_log_otlp` target / `FIXPP_CONFIG_HAS_OTLP` is unavailable).
- **FR-014**: The loader MUST run a deterministic **side-effect-free** load-time resource preflight over the resolved sinks — collecting ALL diagnostics — before any live logger is constructed: an OTLP collector certificate that is unreadable or fails a PEM-magic-header check (a cheap readability + leading-`-----BEGIN`/PEM-magic validation — the full CA-bundle parse happens later at sink open()), a file-sink directory that does not already exist or is not a writable directory (a stat/access check — the preflight MUST NOT create the directory or write a probe file), and a present-but-empty OTLP endpoint MUST each fail closed and attribute the failure to that sink/selector. `FileSink::open()` creates/opens the live log file, not the directory; the directory must pre-exist (the preflight requires it). (The preflight covers what is determinable at load without mutating the filesystem; see FR-015 for the construction ordering that makes "nothing opened" real, and the named residual limitation.)
- **FR-015**: The loader MUST validate the entire logger configuration and run the FR-014 preflight, and fail closed before any session is opened. A live logger (which opens its sinks and starts its drain thread) MUST be constructed only at the very end of a load whose whole-file diagnostic accumulator is empty — engine-default and every per-session override alike. On ANY error no live logger is constructed, so no sink files are opened and no drain thread is started; nothing is left partially configured. **Known limitation (inherited from the 017 logger):** the logger constructor silently disables (and counts) any sink whose `open()` fails, so a narrow time-of-check/time-of-use window remains between the load-time preflight and construction. This step does not change the 017 logger contract; a future option is to read the post-construction sink-error counters and fail closed if any sink failed to open — offered, not required this step.
- **FR-016**: The loader MUST apply no implicit defaults to logger settings: an absent optional key yields its documented explicit default; a required key (e.g. an OTLP endpoint, a non-empty sink list) that is absent or empty is an error.
- **FR-017**: The loader MUST accept only the canonical spellings of logger enumerated values (sink kinds, overflow policy, syslog facility names) and reject any unknown token, reporting the legal set.
- **FR-018**: Relative filesystem paths in logger selectors (file-sink directory, collector-TLS certificate path) MUST resolve against the directory containing the config file, consistent with step 1 (FR-016a of 044).
- **FR-019**: Configuration MUST remain frozen at load/open; later edits to the source file MUST NOT affect a running engine's logging until an explicit reload-and-reopen.

**Diagnostics (per-key taxonomy, continuity)**

- **FR-020**: On any logger load failure the loader MUST produce a diagnostic identifying the specific offending key/selector, the reason class (reusing step 1's taxonomy: unknown key, recognized-but-not-yet-supported, missing required, empty required, malformed value, out-of-range, unknown enum, invalid/contradictory selector, parse error), and the file location.
- **FR-021**: The loader MUST collect and report ALL errors found in a single load pass (including logger errors interleaved with establishment errors), preserving step 1's collect-ALL guarantee — no fix-one / re-run loop.
- **FR-022**: The loader MUST flip exactly the top-level **`logger`** key from the step-1 "recognized-but-not-yet-supported" reason to fully supported. It MUST keep all other previously-deferred keys under the deferred reason: `tracer` / `meter` / `otlp` / `prometheus` / `exporter` / `log_sink` (tracer/meter export unimplemented; the others are not standalone top-level blocks / have no file channel), and the unrelated deferred surface (arenas, dialect overlay, tap). No previously-supported key may regress. (Research D-6 holds the authoritative key list.)
- **FR-023**: Any sensitive value reachable through a logger selector (concretely: credentials embedded in an OTLP collector endpoint URL — e.g. the userinfo in `http://user:secret@collector:4318/v1/logs`) MUST be redacted from diagnostics and logs, consistent with step 1's redaction behavior. (Header-based auth is **not** file-expressible this step — `OtlpLogSinkConfig` carries no header/auth field — so endpoint userinfo is the only file-reachable credential surface; the redaction scope is bounded to it.)

**Continuity (no new external surface)**

- **FR-024**: This feature MUST introduce no new FIX wire behavior, no new error code crossing the C-ABI, no generated-code change, and no C-ABI surface change; the logger diagnostics remain a loader-local concern.
- **FR-025**: The feature MUST NOT regress step-1 behavior: every step-1 file that loaded successfully MUST still load identically, and every step-1 diagnostic MUST be unchanged except for the `logger` key deliberately flipped to supported (FR-022).

### Key Entities

- **Logger configuration**: the logger (with its ordered sink list) resolved from a file, carried in the configuration bundle's engine-establishment slice and per-session model.
- **Sink selector**: a *kind + parameters* entry (file / syslog / OTLP) the loader resolves into a built-in log sink; a logger carries an ordered list of these.
- **Per-session logger override**: a session-scoped logger that replaces the engine default for that session.
- **Load diagnostic**: the same structured per-failure report as step 1 (offending key/selector, reason class, file location), now also covering logger keys.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: An operator can bring up an engine whose logging is configured entirely from a config file plus injected application callbacks — zero per-object programmatic logging wiring — for the common case.
- **SC-002**: A logger configuration loaded from a file produces a logger observably identical to the equivalent programmatically-built one across the full in-scope sink set (verified object-for-object: sink kinds, order, parameters).
- **SC-003**: For every defined logger failure class — unknown kind, missing required, out-of-range, platform/build-unavailable sink, unreadable/non-PEM certificate, missing-or-unwritable file-sink directory — a deliberately broken file is rejected before any session opens (caught at validation or at the side-effect-free load-time resource preflight, before any live logger is constructed), with a correct per-key diagnostic and **zero** cases of silent no-op substitution or partial configuration. No sink file is opened, no directory is created, and no drain thread is started on a failed load.
- **SC-004**: A file that omits the logger block loads to a result identical to the step-1 output (logging is fully optional and additive).
- **SC-005**: A multi-session file produces each session with the engine-level logger applied and per-session logger overrides honored.
- **SC-006**: A host that does not use file-based configuration incurs no additional dependency from this feature (the parser stays out of the core), unchanged from step 1.
- **SC-007**: A file containing N independent errors (logger and/or establishment) yields N distinct per-key diagnostics from a single load attempt; the still-deferred keys (tracer, meter, arenas, dialect overlay, tap) are reported under the distinct deferred reason, not as unknown-key typos.

## Assumptions

- **Reuses step 1 wholesale** (decided): the format is TOML, the loader is a synchronous cold path, the fail-closed / frozen-at-open / no-implicit-default / redaction invariants and the per-key collect-ALL diagnostic taxonomy are inherited unchanged. This feature extends, it does not redefine.
- **The logger and its sinks map onto existing built-in objects** (verified in-source 2026-06-20): the logger, its file/syslog/OTLP sinks, and their config structs and factories all exist; the OTLP log sink genuinely exports over HTTP. This feature supplies the TOML→struct mapping and the bundle wiring, not new logging machinery.
- **Tracer/meter deferred to backlog** (decided 2026-06-20, source-verified): the tracer/meter OTLP export is an unimplemented provider-layer stub (spans/metrics discarded; endpoint/cert ignored). Configuring them would ship dead knobs, so they are deferred and tracked in the backlog, gated on the export pipeline shipping. They remain recognized-but-not-yet-supported in the loader.
- **No per-sink severity filter** (default): the built-in sinks carry no per-sink level threshold today (filtering is logger-side, by category); this step exposes only the parameters the existing sink configs carry and does not invent a per-sink level.
- **Logger buffer allocator stays default** (decided): the logger's record-ring memory resource is part of the deferred memory-arena surface (FR-010); the file configures the logger's behavioral settings but not its allocator.
- **Host completion unchanged**: every deployment still injects application callbacks and provides the executor before opening; logger config does not change that stub (FR-012).

## Dependencies

- The existing public built-in logging value-types and their parameterizable factories/config structs (logger + file/syslog/OTLP sinks) — the loader maps onto them; it does not redefine them.
- The step-1 loader, its selector-resolver registry, its diagnostic taxonomy, and its configuration bundle — all extended here, none replaced.
- The existing fail-closed, frozen-at-open, no-implicit-default, and redaction invariants, preserved at the text boundary.

## Normative References

This feature, like step 1, is **design-blessed**, not a FIX-spec-coverage feature: no normative FIX specification section mandates a TOML logging loader. Its authority is the constitution and the existing logging object surface it configures.

- **`[const §XV.16]`** — *Custom config format ban / TOML acceptance.* This feature extends the native TOML acceptance path to the logging surface; it introduces no new format.
- **`[const §X.4]`** — *C-ABI error reporting / `fixpp_error_t` stability.* The loader introduces no new `fixpp_error_t` value; its per-key logger diagnostics are the same loader-local C++ type as step 1 and never cross the C ABI (FR-024).
- **`[const §XIV.1/§XIV.2]`** — *Pluggable interfaces with one default impl each.* The loader resolves the **existing** built-in logger/sink implementations (`Sink` is a 4-pure-virtual plugin); it adds no new pluggable interface.
- **`[const §XV.1]`** — *No hot-path heap allocation.* The loader is a cold, load-time path executed once before `Session::open`; it touches no parse→dispatch hot path. (The configured logger's own runtime allocation discipline is governed by its existing contract, unchanged by this feature.)
- **Precedent (normative for continuity):** `044-toml-session-config` — the loader architecture, the *kind + parameters* selector-resolver registry, the per-key collect-ALL diagnostic taxonomy, and the configuration bundle this feature extends. Step 1's "recognized-but-not-yet-supported (step 2)" deferral of the observability keys is the explicit hand-off point this feature picks up for the `logger` key (FR-022).
- **Precedent (non-normative):** `017-log-otel` and the logger/sink surface it established — the built-in logging objects this loader selects.

## Out of Scope (this step)

- **Tracer and metrics exporter configuration** — deferred to the backlog (source-verified 2026-06-20: OTLP trace/metric export is an unimplemented provider-layer stub; configuring it would have no effect). Gated on the export pipeline shipping.
- Memory-arena selection (including the logger's buffer memory resource) — deferred to a later step (user decision 2026-06-20).
- The dictionary dialect overlay selector — deferred to a later step (user decision 2026-06-20).
- The diagnostic tap — forced-deferred: the tap surface is a bare stub with no hooks/parameters, owned by the unshipped tap feature.
- The non-default (gRPC) OTLP log-export transport (recognized-but-not-yet-supported, like step 1's deferred sub-selectors).
- Encrypted collector-TLS private keys (plaintext-key-only, consistent with step 1).
- Expressing host-supplied behaviors in the file (application callbacks, executor instance, host-written custom kinds, a shared pre-existing execution context).
- Any change to FIX wire behavior, error codes, generated code, or the C-ABI surface.
