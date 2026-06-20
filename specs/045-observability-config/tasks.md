# Tasks: Native TOML config-file loading (logging pipeline)

**Input**: Design documents from `specs/045-observability-config/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/observability_config.hpp, quickstart.md
**Branch**: `045-observability-config` (library submodule)

**Tests**: REQUIRED — `[const §VII.3]` makes TDD mandatory, and the spec explicitly requests a negative-file battery (US2), a field-for-field equivalence test (US1), a multi-session logger-override test (US3), and a still-deferred-surface test (US4). Tests are written FIRST and must FAIL before implementation.

**Component**: an **in-place extension** of the EXISTING `fixpp_config_toml` target (044) — **no new dependency, no new target, no new module, no new public type** (one additive `shared_ptr` field on `EngineEstablishment`; one loader-local `PendingLoggerSet` carrier). The target's link set newly gains `fixpp_log` (unconditional) plus a **conditional** `fixpp_log_otlp` link (gated by `FIXPP_CONFIG_HAS_OTLP`). The include-layer edge `config → log` and the conditional link edge `config → fixpp_log_otlp` are grounded in Phase 1. `load_diagnostic.hpp` / `toml_config_loader.hpp` are UNCHANGED (no new `reason_class`, no entry-point signature change, no new `fixpp_error_t` — `[const §X.4]` pinned).

**ODR note (044 T039 carry-over)**: any TU that instantiates tomlplusplus types MUST include the `src/config/toml_include.hpp` shim, never `<toml++/toml.hpp>` directly (the `#undef NDEBUG` save/restore changes inline/template bodies — mixing shimmed and un-shimmed TUs is UB). The new `logger_resolver.cpp` reads `toml::table`, so it MUST include the shim.

## Format: `[ID] [P?] [Story] Description`
- **[P]**: parallelizable (different file, no incomplete-task dependency)
- **[Story]**: US1–US4 (story-phase tasks only)

---

## Phase 1: Setup (Shared Infrastructure — build + layer grounding)

**Purpose**: ground the new include edge + conditional link edge so the extended target compiles and passes the layer gate. These are the **NAMED pre-merge grounding tasks** from plan §"Layer-grounding action" / Constitution Check §III & §VI — tracked here so they are not lost as prose.

- [x] T001 **[grounding — check_layers.py]** Extend the `config` whitelist in `tools/check_layers.py` to add `log`: `config → {core, session, dictionary, tls, transport, log}` (was `{core, session, dictionary, tls, transport}`). Re-assert the negative edge: no `core`/`session`/`dict`/`tls`/`transport`/`log` module includes or links `config` (no FORBIDDEN-edge conflict). **Do NOT add `otel`** — the loader includes no `<fixpp/otel/…>`; the OTel SDK is reached only via the conditional `fixpp_log_otlp` **link** edge (T003), not an include edge. Run `python3 tools/check_layers.py` → clean (plan §Constitution Check §VI action (1); research "Process actions").
- [x] T002 **[grounding — architecture.md]** Update `.specify/architecture.md`: add `log` to the `config` module row (≈line 67) and the dependency-table row (≈line 129) — `log` is an allowed dependency of `config`, pulled only by the opt-in adjacent loader (plan §Constitution Check §VI action (2)).
- [x] T003 **[grounding — CMakeLists.txt conditional link]** In `src/config/CMakeLists.txt`: link `fixpp_log` to `fixpp_config_toml` **unconditionally**; add an `if(TARGET fixpp::log_otlp)` block that links `fixpp_log_otlp` and `target_compile_definitions(fixpp_config_toml PRIVATE FIXPP_CONFIG_HAS_OTLP)` (mirrors the `FIXPP_HAS_SYSLOG` gate). Confirm a build WITHOUT the OTel SDK does not drag it in (no `FIXPP_CONFIG_HAS_OTLP`, no `fixpp_log_otlp` link). Build the (still logger-less) target green in both configurations if available (plan §Constitution Check §III; research D-3/D-7 / FR-013).

**Checkpoint**: the layer gate accepts `config → log`; the OTLP link is conditional; nothing in core links `config`.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: the additive bundle field, the loader-local pending carrier, the deferred-key flip, and the shared sink vocabulary every story needs. **⚠️ No US work until this is complete.**

- [x] T004 **[bundle field — E-2]** Amend `include/fixpp/config/config_bundle.hpp`: add `std::shared_ptr<fixpp::log::Logger> logger;` to `EngineEstablishment` (forward-declare `namespace fixpp::log { class Logger; }` — no full include needed for a `shared_ptr` member). Null ⇒ engine logger no-op (FR-007, maps 1:1 onto `EngineConfig::logger`). **No tracer/meter field** (deferred — data-model E-2 note). No `SessionConfig` change (per-session override uses the existing `SessionConfig::logger_override`).
- [x] T005 **[pending carrier — research D-7 / contract]** Add the loader-local `PendingLogger` + `PendingLoggerSet` carriers (NOT a `ConfigBundle` field) in a loader-internal header (`src/config/logger_resolver.hpp`, new): `PendingLogger { LoggerConfig cfg; std::pmr::vector<std::unique_ptr<Sink>> sinks; std::string key_path; SourceLoc loc; /* target: engine slot | session index */ }`; `PendingLoggerSet { std::optional<PendingLogger> engine; std::vector<PendingLogger> sessions; }` (file-scoped). N-1: the `pmr::vector` + minted sinks bind to `LoadOptions::resource`. N-2: file-scoped, populated across the whole file (data-model E-3/E-5, contract lines 38-66). **`key_path` allocator scope:** the `std::string key_path` (and `SourceLoc loc`) are diagnostic metadata, NOT part of the N-1 arena-bound set — they may use the global allocator (matching how 044 `LoadDiagnostic.key_path`/`message` are stored); only the move-only logger payload (`cfg` + `sinks`) is N-1-bound. (Resolves analyze C1.)
- [x] T006 **[deferred-key flip — D-6]** In `src/config/toml_config_loader.cpp` `recognize_keys()`, move **only** `logger` from the `kDeferred` set into `kRecognized`. Keep deferred (no regression): `tracer`, `meter`, `log_sink`, `otlp`, `exporter`, `prometheus`, `arena`, `message_arena`, `session_arena`, `framer_carry_arena`, `dialect_overlay`, `tap`, `tap_consumer`. Keep the `recognized_not_yet_supported_step2` enum **symbol** (renaming churns 044 tests); generalize only the diagnostic **message** text to "recognized but not yet supported" (FR-022, data-model E-5, research D-6).
- [x] T007 **[shared sink vocabulary]** In `src/config/scalar_mappers.cpp`: add the closed syslog-`facility` name→`LOG_*` map (exact set: `kern, user, mail, daemon, auth, syslog, lpr, news, uucp, cron, authpriv, ftp, local0..local7`) — a build-undefined `LOG_*` macro (`#ifdef`-conditional, e.g. `LOG_AUTHPRIV`/`LOG_FTP`/`LOG_CRON`) → `invalid_or_contradictory_selector`; a name not in the set → `unknown_enum` + legal set. Add a `capacity` **power-of-2** validation helper (not pow2 → `out_of_range`) (data-model E-4, research D-3 / FR-017).

**Checkpoint**: the logger key reaches the resolver; the carrier + sink vocabulary exist; selectors/preflight/construction not yet wired.

---

## Phase 3: User Story 1 — Configure logging from the config file (Priority: P1) 🎯 MVP

**Goal**: a well-formed `[logger]` block (logger scalars + an ordered, non-empty `[[logger.sinks]]` array) resolves to a logger behaviorally indistinguishable from the equivalent hand-built one (same sink kinds, order, parameters); absent `[logger]` ⇒ byte-identical to the 044 result.

**Independent Test**: author a file configuring a logger with a representative sink set (file + OTLP); load it; assert the resulting `EngineEstablishment::logger` is equivalent object-for-object to a hand-built `Logger` (sink kinds/order/parameters), and that omitting `[logger]` yields the step-1 result with a null logger (SC-002/SC-004).

### Tests for US1 (write FIRST, must FAIL)

- [x] T008 [P] [US1] **Equivalence test** in `tests/config/test_load_logger.cpp` + fixture `tests/config/fixtures/logger_happy.toml`: load a `[logger]` with a file sink + an OTLP sink (ordered); assert the resolved logger drains to exactly those sinks in order with the given params, observably equal to a programmatically-built reference (SC-002, AC US1-1/2/3). Guard the OTLP-sink assertions under `FIXPP_CONFIG_HAS_OTLP`. **Plus a POSITIVE duplicate-sink-kind cell** (spec Edge Cases line 102): two `file` sinks to distinct directories (and, under `FIXPP_CONFIG_HAS_OTLP`, two `otlp` sinks to distinct collectors) load as a valid ordered fan-out — duplicate kinds with distinct params are NOT an error. RED confirmed: 3 sub-cells fail with `engine.logger==nullptr` (resolver T010-T013 not yet written). Behavioural observation via file-creation chosen as discriminating strategy (pimpl has no sink inspector).
- [x] T009 [P] [US1] **Optional-absence test** in `tests/config/test_load_logger.cpp`: a file that omits `[logger]` loads to a bundle with `engine.logger == nullptr`, byte-identical to the step-1 result; absence is not an error (FR-003/SC-004, AC US1-4). GREEN immediately as expected (preserved-behaviour guard).

### Implementation for US1

- [x] T010 [US1] **Sink resolver** (`resolve_log_sink`) in new `src/config/logger_resolver.cpp` (include the `toml_include.hpp` shim): one `[[logger.sinks]]` entry → `std::unique_ptr<Sink>` via the kind's existing factory, **object-minting only (no `open()`)** — `file`→`FileSinkFactory`/`FileSinkConfig` (`directory` relative→config dir, `base_name`, `max_file_bytes` `0`→`out_of_range`, `max_keep_count`, `async_fsync`); `syslog`→`SyslogSinkFactory`/`SyslogSinkConfig` under `#ifdef FIXPP_HAS_SYSLOG` (`#else` → `invalid_or_contradictory_selector`), `facility` via the T007 map; `otlp`→`OtlpLogSinkFactory`/`OtlpLogSinkConfig` under `#ifdef FIXPP_CONFIG_HAS_OTLP` (`#else` → `invalid_or_contradictory_selector`) (`endpoint`, `cert_source` relative→config dir, `export_timeout`, `max_export_batch` `0`→`out_of_range`, `max_export_retries`). Unknown `kind` → `unknown_enum {file,syslog,otlp}` (data-model E-4, research D-3, contract lines 87-104).
- [x] T011 [US1] **Composite logger resolver** (`resolve_engine_logger`) in `src/config/logger_resolver.cpp`: resolve the `LoggerConfig` scalars (`capacity` pow2 via T007, `on_overflow` enum, `drain_timeout` 044-duration rule, optional `drain_cpu_affinity`) + a **non-empty, ordered** `[[logger.sinks]]` array (via T010) into a `std::pmr::vector<std::unique_ptr<Sink>>` (`LoadOptions::resource`), and **emit a `PendingLogger` (engine slot) into `pending`** — does NOT construct the live `Logger`, does NOT write `ConfigBundle`. Zero/absent sinks → `empty_required`/`missing_required` on `logger.sinks`. `ring_resource` NEVER file-set (deferred arena, FR-010) (data-model E-3, research D-2/D-7, contract lines 77-85).
- [x] T012 [US1] **Clean-accumulator construction** (`construct_loggers_if_clean`) in `src/config/logger_resolver.cpp`: the SOLE side-effectful step — only when the **whole-file** accumulator is empty, move each `PendingLogger` into `std::make_shared<Logger>(std::move(cfg), std::move(sinks))` (its ctor opens every sink + spawns the drain thread, `logger.cpp:165-180`) and assign to its keyed destination (engine slot → `bundle.engine.logger`; session index → `bundle.sessions[i].config.logger_override`). A non-empty accumulator ⇒ no `Logger` constructed (research D-7, FR-015, contract lines 111-116).
- [x] T013 [US1] **Wire into the loader** in `src/config/toml_config_loader.cpp`: after the 044 resolution, call `resolve_engine_logger` for `[logger]`, then (after the US2 preflight, **T016**) `construct_loggers_if_clean` as the end-of-load step; assemble `engine.logger` into the `ConfigBundle`. Make T008/T009 pass.

**Checkpoint**: a well-formed file loads to a bundle carrying an equivalent logger; omission is a clean no-op.

---

## Phase 4: User Story 2 — Bad logging config rejected before open, with an actionable diagnostic (Priority: P1)

**Goal**: every logger error class is rejected **before any session opens** (at validation or at the side-effect-free load-time resource preflight, before any live logger is constructed); the diagnostic names the offending key/selector, reason, and location; all errors collected in one pass; **zero** silent no-op substitution; no file opened, no directory created, no drain thread started on a failed load.

**Independent Test**: a battery of deliberately-broken logger files (one per error class); each rejected with the correct key + reason, no session opens, no no-op logger substituted; an N-error file yields N diagnostics (SC-003/SC-007).

### Tests for US2 (write FIRST, must FAIL)

- [x] T014 [P] [US2] **Negative battery** in `tests/config/test_load_logger_negative.cpp` + `tests/config/fixtures/neg_logger_*.toml` — **one cell per reason_class reachable on the logger surface** (the 044 standard): unknown sink kind (`unknown_enum`); unknown `on_overflow` token (`unknown_enum`, distinct site); unknown `facility` name (`unknown_enum`); OTLP missing `endpoint` (`missing_required`); present-but-empty `endpoint` (`empty_required`); zero/absent sinks (`empty_required` on `logger.sinks`); `capacity` not pow2 + zero `max_export_batch`/`max_file_bytes` (`out_of_range`); **unitless/ambiguous `drain_timeout` or `export_timeout` (`malformed_value` — the 044 duration rule, data-model E-4)**; `syslog` on a non-`FIXPP_HAS_SYSLOG` build + `otlp` on a non-`FIXPP_CONFIG_HAS_OTLP` build (`invalid_or_contradictory_selector`, build-conditional cells); unreadable / non-PEM `cert_source` (`invalid_or_contradictory_selector`); file-sink `directory` that does not exist / is not writable (`invalid_or_contradictory_selector`); `use_grpc=true` (`recognized_not_yet_supported_step2`). Assert key_path + reason + location, **no session opens**, and **no no-op logger substituted** (SC-003, AC US2-1..5). **Also assert no side effects on a failed load**: no sink file created, no directory created, no drain thread started (SC-003 tail).
- [x] T014a [P] [US2] **Redaction test** (FR-023) in `tests/config/test_load_logger_negative.cpp` + fixture: an OTLP sink whose `endpoint` embeds a credential (e.g. `http://user:secret@collector:4318/v1/logs`) that triggers a diagnostic (e.g. paired with another error) yields a diagnostic whose `message` redacts the secret (`***REDACTED***`), never the cleartext — reusing 044's redaction behavior (FR-023, mirrors 044 T021).
- [x] T015 [P] [US2] **Collect-ALL test** in `tests/config/test_load_logger_negative.cpp`: a file with N independent errors (logger + establishment interleaved) yields exactly N distinct per-key diagnostics in one load pass; a later session's logger error suppresses construction of an earlier session's logger (N-2) (SC-007/FR-021, AC US2-6). ("whole-file accumulator" = the loader-scope 044 `DiagnosticAccumulator` `acc` reached via the `acc.size()`-delta pattern — NOT a per-session sub-counter; resolves analyze B1.)

### Implementation for US2

- [x] T016 [US2] **Side-effect-free load-time resource preflight** in `src/config/logger_resolver.cpp` (research D-4/D-7, FR-014): over the resolved pending set's sinks, BEFORE any live `Logger` is constructed, collect-ALL — file-sink `directory` **already exists and is a writable directory** (stat/access only — **no `mkdir`, no probe file**); OTLP `cert_source` **readable + PEM-magic-validated** (leading `-----BEGIN`/PEM magic; full CA-bundle parse deferred to sink `open()`); OTLP `endpoint` present/non-empty. Each failure → `invalid_or_contradictory_selector` (cert/dir) or `missing_required`/`empty_required` (endpoint), attributed to that sink/selector, appended via the 044 `acc.size()`-delta pattern. Paths resolve relative to the config-file dir (FR-018); credentials redacted (FR-023). **Do NOT** reuse `make_file_cert_source` (it throws on empty leaf/key — CA-only `cert_source` is incompatible; research D-4 / Gate A round-2 disagreement).
- [x] T017 [US2] **Validators wired into the resolvers** (T010/T011): unknown sink kind / facility / overflow token → `unknown_enum` + legal set; OTLP missing/empty `endpoint` → `missing_required`/`empty_required` (loader is stricter than the sink's silent-drop-on-empty default — edge case in spec); out-of-range numerics → `out_of_range`; `use_grpc=true` → `recognized_not_yet_supported_step2`; platform/build-unavailable sink → `invalid_or_contradictory_selector` (FR-013/FR-016/FR-017/FR-020). Confirm the construct step (T012) runs only on a clean accumulator so a failed load constructs no `Logger` (FR-015). Make T014/T015 pass.

**Checkpoint**: fail-closed-before-open is enforced per-key for the logger surface with collect-ALL diagnostics and zero side effects on failure.

---

## Phase 5: User Story 3 — Engine-default logger with per-session overrides (Priority: P2)

**Goal**: an engine-level logger applies to all sessions; a per-session `[session].logger` override replaces it for that session only; a session without an override inherits the engine default (null override).

**Independent Test**: a multi-session file with an engine-level logger plus a per-session override; load; assert the overriding session carries the override logger while other sessions carry the engine default and non-overriding sessions carry a null `logger_override` (SC-005, AC US3-1/2).

### Tests for US3 (write FIRST, must FAIL)

- [x] T018 [P] [US3] **Multi-session override test** in `tests/config/test_load_logger_overrides.cpp` + fixture `tests/config/fixtures/logger_multisession.toml`: an engine `[logger]` + one session with `[session.logger]` + one without; assert the overriding session's `config.logger_override` is the override logger, the non-overriding session's `logger_override` is null (inherits engine default), and `engine.logger` is the engine default (SC-005, AC US3-1/2).

### Implementation for US3

- [x] T019 [US3] **Per-session logger resolver** in `src/config/logger_resolver.cpp`: `[session].logger` reuses `resolve_engine_logger` (T011) to emit a **session-keyed** `PendingLogger` (carrying the session index) into `pending.sessions` — NOT writing `logger_override` directly and NOT constructing inside the per-session loop (N-2 file-scoped). Wire the call into the per-session resolution path in `toml_config_loader.cpp`; the existing `construct_loggers_if_clean` (T012) assigns `bundle.sessions[i].config.logger_override` at clean-accumulator construction (data-model E-5, research D-7 N-2). Make T018 pass.

**Checkpoint**: engine-default + per-session logger overrides honored; later-session errors suppress earlier-session construction.

---

## Phase 6: User Story 4 — The deferred surface stays visible, not silently ignored (Priority: P3)

**Goal**: each still-deferred key (`[tracer]`/`[meter]`, memory-arena selectors, `dialect_overlay`, `tap`) is rejected under the distinct `recognized_not_yet_supported_step2` reason with its key named — distinct from an unknown-key typo — while `logger` is now supported. No previously-supported key regresses.

**Independent Test**: files containing each still-deferred key; each rejected under the deferred reason with its key named, distinct from the unknown-key reason; a `logger` block is NOT rejected (SC-007, AC US4-1/2/3).

### Tests for US4 (write FIRST, must FAIL)

- [ ] T020 [P] [US4] **Deferred-surface test** in `tests/config/test_load_deferred_surface.cpp` (new) or amend the 044 deferred cells + fixtures: `[tracer]`, `[meter]`, a memory-arena selector (`message_arena`), `dialect_overlay`, `[tap]` each → `recognized_not_yet_supported_step2` naming that key, distinct from an `unknown_key` typo; assert a `[logger]` block does NOT report the deferred reason (the one-key flip, FR-022). Confirm no previously-supported 044 key regresses (FR-025) (AC US4-1/2/3, SC-007).

### Implementation for US4

- [ ] T021 [US4] Confirm the T006 `recognize_keys()` disposition satisfies T020 (logger flipped to supported; all other observability/arena/dialect/tap keys stay deferred under the kept enum symbol with the generalized message). Adjust the deferred-set membership / message text if T020 surfaces a gap (FR-022/FR-025, data-model E-5). Make T020 pass.

**Checkpoint**: the deferral boundary stays loud; exactly one key (`logger`) flipped; no regression.

---

## Phase 7: Polish & Cross-Cutting

> **Implementation-discovered deviations to reconcile before Gate B** (record in T022 catalogue/B&L; decide whether a `/speckit-analyze` re-run is needed): note any divergence between the resolver file organization here (`logger_resolver.cpp`, OQ-1 resolved) and the plan's "MAY live in selector_resolver.cpp" wording; note the named inherited-017 silent-sink-disable TOCTOU limitation (research D-7) as an L-row.

- [ ] T022 [P] **[NAMED pre-merge — catalogue + coverage-index, §VI.4]** Extend the 044 `T-043` design row (or add a sibling design row) in `spec/feature-catalogue.md` for the logging leg (design-blessed, NOT `OFFICIAL`) AND add a design-choice note to `spec/coverage-index.md`. Add B&L rows in `spec/behaviors-and-limitations.md`: the inherited-017 silent-sink-disable preflight→construct TOCTOU limitation (research D-7) and the loader-stricter-than-sink empty-endpoint rejection (spec edge case). Run any catalogue-consistency check that exists (plan §Constitution Check §VI action; research "Process actions").
- [ ] T023 [P] **[NAMED pre-merge — fuzz corpus, §VII.7]** Extend the `fuzz_toml_loader` seed corpus (`tests/config/fuzz/corpus/toml_config_loader/`) with `[logger]` / `[[logger.sinks]]` blocks (file/syslog/otlp variants, good + malformed) so the new resolver branches are reachable from malformed input. No new harness (the loader entry point is unchanged); the §VII.7 ≥10-min Gate-B fuzz run carries over (plan §Constitution Check §VII action; research "Process actions").
- [ ] T024 [P] **[process — backlog record]** Record tracer/meter config as a backlog item: update parent `REMAINING-WORK.md` item 14b-residual and add a `phases/phase-4/config/` note, **gated on the OTLP trace/metric export pipeline shipping** (provider-layer item; research "THE finding" / "Process actions"). (Parent-repo edit — orchestrator bookkeeping at merge.)
- [ ] T025 [P] Register the new test binaries in `tests/config/CMakeLists.txt` (`test_load_logger`, `test_load_logger_negative`, `test_load_logger_overrides`, `test_load_deferred_surface`) with LABELS `"config;045"`; confirm `ctest -L config` selects them (note: name-prefix `-R '^config'` matches nothing — use the label, per 044 T035).
- [ ] T026 `/speckit-verify` gate prep (checklist hook — runs in `/speckit-verify`, not here): confirm coverage ≥95/85 on the touched `src/config/` files (`logger_resolver.cpp`/`.hpp`, the `scalar_mappers.cpp`/`toml_config_loader.cpp` deltas) + `include/fixpp/config/config_bundle.hpp`; the 6/7-preset sanitizer matrix green; **TSan note**: a clean load constructs the live `Logger`(s) and starts the drain thread (research D-7 / Constitution Check §IX/§XI) — the constructed-logger threads + the inherited 017 silent-sink-disable are governed by the 017 lifecycle contract (host owns shutdown), not new loader concurrency. **Also confirm (cheap doc checks):** (a) FR-024/[const §X.4] pin — the `load_diagnostic.hpp` diff has ZERO additions to `reason_class` and no new `fixpp_error_t` (analyze E3); (b) FR-019 frozen-at-open — after `construct_loggers_if_clean`, the bundle holds a value-typed `shared_ptr<Logger>` with no loader-retained alias into pending state (analyze E1).
- [ ] T027 [P] Run `quickstart.md` end-to-end as an example check: the example logger TOML + host stub compiles/loads against the implemented resolver (fix any drift, as 044 T034 did); the referenced sink directories must pre-exist (preflight requirement). No `examples/` program required unless warranted (`[const §XIX.3]`).

---

## Dependencies & Execution Order

- **Setup (Phase 1)** T001–T003: T001/T002 [P]; T003 needs the link targets to exist (independent of T001/T002 logically, but run after for a clean gate). BLOCKS compilation of the logger edges.
- **Foundational (Phase 2)** T004–T007: BLOCKS all stories. T004/T005/T007 [P]; T006 [P] (loader-file edit). All after Phase 1.
- **US1 (Phase 3)** T008–T013: after Foundational. Tests T008/T009 [P] first (FAIL), then T010→T011→T012→T013 (sink → composite → construct → wire).
- **US2 (Phase 4)** T014–T017: after US1's resolvers exist (adds preflight + validators). Tests T014/T014a/T015 [P] first. T016 (preflight) before T013's `construct_loggers_if_clean` call ordering is finalized.
- **US3 (Phase 5)** T018–T019: after US1 (`resolve_engine_logger`) + T012 (construct step). Test T018 [P] first.
- **US4 (Phase 6)** T020–T021: after T006 (the flip). Test T020 [P] first; T021 is mostly verification of the foundational change.
- **Polish (Phase 7)** T022–T027: after the stories that close their evidence. T023 (fuzz corpus) needs the resolvers (T010–T012); T022/T024/T025 [P].

### MVP
US1 (T001–T013) = the migration MVP: a file loads to a bundle carrying a configured logger. US2 (T014–T017) makes it safe (fail-closed diagnostics, zero side effects on failure) and is the P1 companion — ship US1+US2 together as the safe MVP.

### Parallel opportunities
T001/T002 · T004/T005/T006/T007 · all `[P]` test tasks within a story · T022/T023/T024/T025.

---

## Notes
- TDD: every story's tests are written and FAIL before its implementation (`[const §VII.3]`).
- The five NAMED pre-merge tasks are pinned here so they are not lost as prose: **T001** (check_layers.py `config → log`), **T002** (architecture.md config rows), **T003** (conditional `config → fixpp_log_otlp` CMake link), **T022** (feature-catalogue + coverage-index design row), **T023** (fuzz-corpus extension). Plus **T024** (tracer/meter backlog record).
- **OQ-1 resolved**: the logger composite resolver + sink resolvers + preflight + construct step live in a NEW `src/config/logger_resolver.cpp` (+ `.hpp` for the carrier) — `selector_resolver.cpp` is already ~40 KB. Still the SAME `fixpp_config_toml` target (no new target).
- No new dependency, no new public type, no new `reason_class`, no new `fixpp_error_t`, no C-ABI/wire/codegen surface (FR-024). The parser never enters a public header (FR-004/SC-006).
- ODR (044 T039): `logger_resolver.cpp` includes `src/config/toml_include.hpp`, never `<toml++/toml.hpp>` directly.
- Commit after each task or logical group; re-run `codegraph sync` after code-changing tasks.
