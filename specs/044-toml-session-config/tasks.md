# Tasks: Native TOML config-file loading (session establishment)

**Input**: Design documents from `specs/044-toml-session-config/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/toml_config_loader.hpp, quickstart.md
**Branch**: `044-toml-session-config` (library submodule)

**Tests**: REQUIRED — `[const §VII.3]` makes TDD mandatory, and the spec explicitly requests a negative-file battery (US2), a field-for-field equivalence test (US1), a multi-session-defaults test (US4), and a QuickFIX parity table (SC-004). Tests are written FIRST and must FAIL before implementation.

**Component**: a NEW isolated CMake target `fixpp_config_toml` (`src/config/` + `include/fixpp/config/`) that links `fixpp_session`/`_core`/`_tls`/`_dict`/`_transport` and PRIVATE-includes header-only `tomlplusplus/3.4.0` in its `.cpp` only. Namespace `fixpp::config`. Zero new core artifacts (`MemoryStoreFactory` already exists — reused).

## Format: `[ID] [P?] [Story] Description`
- **[P]**: parallelizable (different file, no incomplete-task dependency)
- **[Story]**: US1–US4 (story-phase tasks only)

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: dependency, build target, and architecture grounding so the component compiles and passes the layer gate.

- [X] T001 Add `"tomlplusplus/3.4.0"` to `conanfile.py` `requires` (header-only, MIT — verified Conan Center 2026-06-19; no shared/static option, no `tool_requires`). Confirm `conan install` resolves it on `linux-clang-debug` (`[const §III.2]`, plan Constitution Check §III action).
- [X] T002 Ground the new module in the layer architecture: add the `config` module + `fixpp::config` namespace to `.specify/architecture.md` (module-layering + namespace sections), and add a `config` whitelist entry to `tools/check_layers.py` allowing edges `config → {core, session, dictionary, tls, transport}`; assert NO `core`/`session`/`dict`/`tls`/`transport` module includes or links `config` (Codex Gate A round 1 #3; `check_layers.py` defaults unknown modules to an empty allowed-set). Run `python3 tools/check_layers.py` → clean.
- [X] T003 Create the `fixpp_config_toml` CMake target skeleton in `src/config/CMakeLists.txt` (links `fixpp_session fixpp_core fixpp_tls fixpp_dict fixpp_transport`; PRIVATE `tomlplusplus::tomlplusplus`; nothing in core links it) + wire it into the parent `src/CMakeLists.txt`. Build empty target green.
- [X] T004 [P] Create the public header skeletons under `include/fixpp/config/`: `load_diagnostic.hpp`, `config_bundle.hpp`, `toml_config_loader.hpp` (forward shapes only, no parser include in any public header — realize the contract at `contracts/toml_config_loader.hpp`; FR-004/SC-006 — verify no tomlplusplus type appears in a public header).

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: the loader-local types + the cross-cutting machinery every user story needs. **⚠️ No US work until this is complete.**

- [X] T005 [P] Implement `reason_class` enum + `LoadDiagnostic` { `key_path`, `reason`, `location{line,col}`, `message` } + `SourceLoc` in `include/fixpp/config/load_diagnostic.hpp` (data-model E-5; closed 9-value enum). Credential redaction helper: `username`/`password` values → `***REDACTED***` in `message` (FR-019). **Redaction helpers in `src/config/loader_internal.hpp/.cpp` (`is_credential_key` + `display_value`).**
- [X] T006 [P] Implement `EngineEstablishment`, `SessionDefinition`, `ConfigBundle`, `LoadOptions{ engine_executor, resource=get_default_resource() }`, and `LoadResult = std::expected<ConfigBundle, std::vector<LoadDiagnostic>>` in `include/fixpp/config/config_bundle.hpp` + `toml_config_loader.hpp` (data-model E-1/E-2/E-3; `std::expected` directly — `expected_t<T>` is single-param, no new `fixpp_error_t`). **Already realized by T004 — all types verified field-for-field against E-1/E-2/E-3; all forward-decl namespaces confirmed correct.**
- [X] T007 Implement the TOML parse front-end in `src/config/toml_config_loader.cpp`: pin tomlplusplus to its error/exception mode, parse `path`, convert a `parse_error` to a `parse_error` `LoadDiagnostic` with `source_region` line/col — **no throw escapes the `noexcept` boundary** (validation rule 9 / FR-017).
- [X] T008 Implement the **noexcept-boundary throwing-site wrapper** util in `src/config/loader_internal.hpp` (validation rule 9 / D-3): a `try/catch`→`LoadDiagnostic` adapter (`trap_throw_to_expected`) for the live throwing sites (`system_clock_source` ctor, `XmlLoader::load`, `file_cert_source` direct ctor); prefer the `expected_t`-returning factories (`make_file_cert_source`, `make_asio_{tls,plain}_transport_factory`, `make_ssl_ctx_config`) where they exist (check-the-expected, no try/catch). Document Phase 3 rule in the header comment.
- [X] T009 Implement the **collect-ALL diagnostic accumulator** (`DiagnosticAccumulator` in `loader_internal.hpp`) + the **`[default]`-merge** of array-of-tables `[[session]]` under `[default]` (per-session override; override of a key absent from `[default]` is valid) in `src/config/toml_config_loader.cpp` (FR-008/FR-018; D-8). One pass accumulates every per-key failure; returns the full vector.
- [X] T010 [P] Implement the **relative-path base resolver** (resolve store `directory` / PEM cert/key/ca paths / dictionary path against `path.parent_path()`; absolute verbatim) in `src/config/loader_internal.hpp/.cpp` (FR-016a / D-7). **`resolve_path(base_dir, rel)` in `fixpp::config::detail`; noexcept via `error_code` overload of `weakly_canonical`.**

**Checkpoint**: parse → merge → collect-all → fail-closed plumbing exists; selectors/scalars not yet mapped.

---

## Phase 3: User Story 1 — Establish a session entirely from a config file (Priority: P1) 🎯 MVP

**Goal**: a well-formed file + injected callbacks brings up a session whose effective config equals an equivalent hand-built config field-for-field.

**Independent Test**: author a TOML file with the required keys + a representative scalar set; load it via `load_toml_config(path, LoadOptions{exec,res})`; assert the resulting `SessionConfig`/`EngineEstablishment` equals a hand-built equivalent field-for-field and a session opens.

### Tests for US1 (write FIRST, must FAIL)

- [X] T011 [P] [US1] Field-for-field **equivalence test** in `tests/config/test_load_happy_path.cpp`: load a fixture covering ~28 scalars + the 5 selectors; assert each `SessionConfig`/`EngineEstablishment` field equals a programmatically-built reference (SC-002). Add the good fixture `tests/config/fixtures/happy_full.toml`.
- [X] T012 [P] [US1] Selector-build test in `tests/config/test_load_happy_path.cpp`: assert `clock=system` builds a live `system_clock_source(opts.engine_executor)`; a TLS `transport` builds a factory whose `SslCtxConfig` embeds that same clock (D-5/D-6 — executor-at-load).

### Implementation for US1

- [X] T013 [P] [US1] Implement the **scalar mappers** (~28 Bucket-A fields) in `src/config/scalar_mappers.cpp`: string/number/bool/duration/enum-token → the `SessionConfig` field, sourcing canonical enum-token sets directly from the enum defs in `session_config.hpp`/`security_profile.hpp` (FR-005; D-9). Duration keys require an explicit unit (unitless → `malformed_value`). NOTE: `reject_policy` deferred — `RejectPolicy` is forward-declared only in session_config.hpp (no enumerators; "owned by 005"); emits `recognized_not_yet_supported_step2` if present.
- [X] T014 [P] [US1] Implement the **structured-member mappers** in `src/config/scalar_mappers.cpp`: `security_profile`, `compid_authorization_policy` (principal→compid-set table), `reconnect_endpoint` (host/port → `SessionConfig::reconnect_endpoint`), `reconnect_policy` (FR-006).
- [X] T015 [US1] Implement the **object-selector resolvers** in `src/config/selector_resolver.cpp` (D-5): `store=file`→`FileStoreFactory(FileStore::Config{...})`, `store=memory`→**reuse existing** `MemoryStoreFactory`, `cert_source`→`make_file_cert_source(cfg, opts.resource)`, `dictionary`(by-path)→`XmlLoader::load(path, opts.resource)`, `clock=system`→`system_clock_source(opts.engine_executor)`. (`file_io_executor`/`mr` for store stay host-threaded at `make()`.) **NOTE (analyze C1):** `XmlLoader::load` **throws** (`dict::xml_parse_error`/`unknown_version_error`/`xml_oom_error`) and returns a move-only `Dictionary` **by value** — route it through the T008 noexcept adapter and wrap via `std::make_shared<const dict::Dictionary>(std::move(d))` before emplacing into `EngineEstablishment::dictionaries` (`vector<shared_ptr<const Dictionary>>`).
- [X] T016 [US1] Implement the **transport resolver** in `src/config/selector_resolver.cpp` (D-6/D-6a): `transport.kind` picks `make_asio_tls_transport_factory(cfg, ssl_cfg)` (ssl_cfg embeds resolved cert_source + profile + the load-built clock) vs `make_asio_plain_transport_factory(cfg)`; host/port → `reconnect_endpoint` ONLY; engine-shared `default_transport_factory` by default, per-session `transport_factory_override` only on cert/profile divergence (use_count==1). **NOTE (analyze C2):** `make_asio_{tls,plain}_transport_factory` return `expected_t<unique_ptr<TransportFactory>>` — check-the-`expected`, then convert to the `shared_ptr<TransportFactory>` field via `std::shared_ptr<TransportFactory>(std::move(*result))`.
- [X] T017 [US1] Assemble `ConfigBundle` in `src/config/toml_config_loader.cpp`: build `EngineEstablishment` (clock + store/cert/transport factories + dictionaries from the load inputs) + one `SessionDefinition` per merged `[[session]]`; leave host-supplied fields default (E-2/E-3 SET/LEFT split). Make T011/T012 pass.

**Checkpoint**: a well-formed file loads to a complete-modulo-host-injection bundle and opens.

---

## Phase 4: User Story 2 — Bad config rejected before open, with an actionable diagnostic (Priority: P1)

**Goal**: every error class is rejected before any session opens; the diagnostic names the offending key, reason, and location; all errors collected in one pass.

**Independent Test**: a battery of deliberately-broken files (one per error class); each rejected with the correct key + reason; no session reaches open; an N-error file yields N diagnostics (SC-003/SC-007).

### Tests for US2 (write FIRST, must FAIL)

- [X] T018 [P] [US2] **Negative battery** in `tests/config/test_load_negative_battery.cpp` + `tests/config/fixtures/neg_*.toml` — one cell per `reason_class`: `parse_error`, `unknown_key`, `recognized_not_yet_supported_step2` (logger/tap/arena + `dialect_overlay` + `dictionary.kind="version"` + `security_profile.kind="mtls_pinned"`), `missing_required`, `empty_required`, `malformed_value` (unitless duration), `out_of_range` (≤0 timeout), `unknown_enum`, `invalid_or_contradictory_selector` (tcp+TLS-material; tls-missing-material; `direct_executor`+`spin`; encrypted-PEM key). Assert key_path + reason + location, and **no session opens**.
- [X] T019 [P] [US2] **Collect-ALL test**: an N-independent-error fixture yields exactly N distinct per-key diagnostics in one load (SC-007); a step-2 deferred key reports `recognized_not_yet_supported_step2`, distinct from an `unknown_key` typo.
- [X] T020 [P] [US2] **Required-key cells**: one negative cell per required-at-load key (Bucket B): `clock.kind`, `sender_comp_id`, `target_comp_id`, `begin_string`, `security_profile.kind`, `store.kind`, `dictionary`, `transport.kind`; plus the conditionals — `transport.host/port` missing when `role=initiator`, `default_appl_ver_id` missing when `begin_string="FIXT.1.1"`, `cert_source` missing under a TLS profile. Assert each → `missing_required`/`empty_required` at load (NOT an opaque open-time failure). Add a POSITIVE cell: an acceptor legitimately omits `transport.host/port`; a `FIX.4.x` session omits `default_appl_ver_id`; `insecure_plain_tcp` omits cert_source — all load OK. Tests appended to `tests/config/test_load_negative_battery.cpp`; new fixtures in `tests/config/fixtures/`; FIXT11.xml copied. RED confirmed for session-scalar + conditional cells (assertion-failure on `result.has_value()==true`); 3 positive cells + 4 already-GREEN Phase-3b cells PASS.
- [X] T021 [P] [US2] **Redaction test**: a malformed `password`/`username` yields a diagnostic whose `message` shows `***REDACTED***`, never the secret value (FR-019). Fixture: `neg_password_wrong_type.toml` (password = 99887766 integer). RED confirmed (Phase 3b silently skips wrong-typed password; ASSERT_FALSE fails with assertion-failure, not compile error).

### Implementation for US2

- [X] T022 [US2] Implement the **required-at-load enumeration** (Bucket B, data-model E-3) in `src/config/toml_config_loader.cpp`: per-key `missing_required`/`empty_required` for the unconditional set + the three conditionals (`transport.host/port` initiator-conditional; `default_appl_ver_id` FIXT.1.1-conditional — PRESENCE only, registry serviceability stays open-time; `cert_source` TLS-profile-conditional — no open() backstop). Do NOT over-require defaulted keys (E-3 "stay optional" list).
- [X] T023 [US2] Implement the **enum-token + range + malformed** validators in `src/config/scalar_mappers.cpp`: unknown token → `unknown_enum` + legal set (FR-014); ≤0/out-of-range → `out_of_range`; unitless/typewrong → `malformed_value` (FR-017).
- [X] T024 [US2] Implement the **threading-guard rules** in `src/config/scalar_mappers.cpp` (validation rules 7/7a): `direct_executor` without `already_serialized_executor` → fail closed (FR-011); `direct_executor`+`spin` → `invalid_or_contradictory_selector` on the threading key group at load (D-6b / `session.cpp:904`).
- [X] T025 [US2] Implement the **selector-contradiction + deferral dispositions** in `src/config/selector_resolver.cpp`: file-internal contradiction (tcp+TLS-material / tls-missing-material) → `invalid_or_contradictory_selector` (D-6, the loader-side detection; the resolved-pair invariant stays the 043 `Session::open()` check); the three deferred selections → `recognized_not_yet_supported_step2` (E-4/E-6); unknown selector kind → `unknown_enum` + legal set; encrypted-PEM key → graceful `invalid_or_contradictory_selector` (not terminate, not a deferral). Make T018–T021 pass.

**Checkpoint**: fail-closed-before-open is enforced per-key with collect-all diagnostics.

---

## Phase 5: User Story 3 — Select built-in objects from the file (Priority: P2)

**Goal**: each selectable kind backs the session with the requested built-in; unknown kinds rejected; deferred selections reported distinctly.

**Independent Test**: files exercising each kind (`store=file`/`memory`; `transport=tls`/`plaintext`; `dictionary` by-path; `clock=system`) each produce a working session backed by the requested built-in; an unknown kind is rejected (US3).

### Tests for US3 (write FIRST, must FAIL)

- [ ] T026 [P] [US3] **Selector-kind matrix** in `tests/config/test_load_selectors.cpp` + fixtures: each kind resolves to the requested built-in (assert the concrete factory/store/transport type or behavior); `store=memory` reuses the existing `MemoryStoreFactory`; `transport=plaintext` builds the plain factory with `kind()==plaintext`.
- [ ] T027 [P] [US3] **Divergent-cert multi-session test**: two `[[session]]` with different cert/profile → each gets a freshly-minted `transport_factory_override` with `use_count()==1` (never a shared override); a session matching the engine default uses the shared `default_transport_factory` (D-6a).
- [ ] T028 [P] [US3] **Deferred-selection + FR-011 guard cells**: `dictionary.kind="version"`, `dialect_overlay`, `security_profile.kind="mtls_pinned"` each → `recognized_not_yet_supported_step2`; `mode=direct_executor` without attestation fails closed (US3 AC-4).

### Implementation for US3

- [ ] T029 [US3] Harden the selector resolver registry in `src/config/selector_resolver.cpp` so each `seam`+`kind` maps to its built-in with the legal-set diagnostic on miss; wire the divergent-cert `transport_factory_override` minting path (one-owner, use_count==1). Make T026–T028 pass.

**Checkpoint**: the file genuinely selects the session's built-ins, not just scalars.

---

## Phase 6: User Story 4 — QuickFIX vocabulary parity (Priority: P3)

**Goal**: every QuickFIX session-establishment setting has a documented equivalent key; a multi-session file applies shared defaults with per-session overrides.

**Independent Test**: the catalogue of QuickFIX establishment settings each maps to a documented 044 key (parity table); a multi-session `[default]`+`[[session]]` file applies defaults + overrides (SC-004/SC-005).

### Tests for US4 (write FIRST, must FAIL)

- [ ] T030 [P] [US4] **Parity-table test** in `tests/config/test_quickfix_parity_table.cpp`: enumerate the QuickFIX-cpp `SessionSettings` establishment keys **from the cloned reference source** (`reference-engines/quickfix-cpp`, per D-10 — NOT from memory); assert each maps to a documented 044 key or an explicit out-of-scope/deferred disposition (schedule keys = out-of-scope, with rationale). Emit the parity table doc artifact (SC-004).
- [ ] T031 [P] [US4] **Multi-session defaults test** in `tests/config/test_load_multisession_defaults.cpp` + fixture: a `[default]` + multiple `[[session]]` file; each session inherits defaults, honors per-session overrides, and an override of a key absent from `[default]` is valid (SC-005).

### Implementation for US4

- [ ] T032 [US4] Finalize the parity-key spellings (already pre-aligned to QuickFIX cfg) + author the parity-table doc; ensure the `[default]`-merge (T009) satisfies T031. Make T030/T031 pass.

**Checkpoint**: vocabulary parity demonstrated; multi-session honored.

---

## Phase 7: Polish & Cross-Cutting

> **Implementation-discovered spec deviations to reconcile before Gate B (record in T033 catalogue/B&L + decide whether `/speckit-analyze` re-run is needed):**
> 1. **`reject_policy` → `recognized_not_yet_supported_step2`.** `RejectPolicy` is forward-declared only (owned by feature 005; the enum has NO enumerators in this checkout), so a file cannot select any value. `scalar_mappers` emits `recognized_not_yet_supported_step2` for a present `reject_policy` key. This is NOT currently in data-model E-6's deferred set — add it (or document as a step-1 limitation in B&L). SPEC-FIXED-class.
> 2. **`security_profile.kind` missing now emits the data-model E-3 `missing_required` at `session[0].security_profile.kind` (loader-boundary primary check, added in T022).** The resolver ALSO surfaces a redundant `invalid_or_contradictory_selector` at `…transport` for the same broken input (collect-ALL runs both layers). Redundant, not contradictory — note it; optionally suppress the resolver arm when profile.kind is unset (selector_resolver.cpp) for a single-diagnostic result.

- [ ] T033 [P] **Catalogue + coverage-index** (Article VI §4 — NAMED pre-merge task): add the `[const §XV.16]` **design** row for 044 to `spec/feature-catalogue.md` (design row, NOT `OFFICIAL`) AND a design-choice note to `spec/coverage-index.md` (both files). Run the catalogue-consistency check. **Also reconcile the two implementation-discovered deviations noted above.**
- [ ] T034 [P] Run `quickstart.md` end-to-end as an example check (load the example file + host-completion stub compiles/loads); add to `examples/` if a runnable example is warranted (`[const §XIX.3]`).
- [ ] T035 [P] Add the `tests/config/CMakeLists.txt` registrations for all five test binaries + labels; confirm `ctest -R '^config'` selects them.
- [ ] T036 `/speckit-verify` gate prep: confirm coverage ≥95/85 on `src/config/` + `include/fixpp/config/` (the loader is synchronous cold-path; no new concurrency), and the 6-preset matrix is green. (Runs in `/speckit-verify`, not here — this task is the checklist hook.)
- [ ] T037 **libFuzzer harness** (analyze D1 / `[const §VII.7]` — parser-touching MUST, Gate-B blocker) in `tests/config/fuzz/fuzz_toml_loader.cpp`: feed arbitrary bytes to `load_toml_config(tmpfile, LoadOptions{exec,res})` and assert it NEVER terminates/UB — every input yields either a `ConfigBundle` or a `vector<LoadDiagnostic>` (the central FR-012/rule-9 guarantee: no throw escapes the noexcept boundary). Seed corpus from the US1/US2 fixtures; wire into the fuzz CI target; ≥10-min corpus run at Gate B. (Logically validates US2's fail-closed promise.)
- [ ] T038 [P] **FR-021 migration note** (analyze E1): author a short section (in `quickstart.md` or `book/migration_from_quickfix.md`) stating explicitly that moving configuration into text **relocates type/spelling errors from build time to load time**, and that the mitigation is strict load-time validation (FR-012–FR-018) with per-key fail-closed diagnostics. This is the FR-021 documentation obligation, currently uncovered.

---

## Dependencies & Execution Order

- **Setup (P1)** T001–T004: T001/T002 [P]; T003 needs T001; T004 [P] after T003.
- **Foundational (P2)** T005–T010: BLOCKS all stories. T005/T006/T010 [P]; T007→T008→T009 sequential (parse→wrap→accumulate).
- **US1 (P3 phase)** T011–T017: after Foundational. Tests T011/T012 [P] first (FAIL), then T013/T014 [P] → T015→T016→T017.
- **US2 (P4)** T018–T025: after Foundational; shares the accumulator. Builds on US1's mappers/resolvers (validation arms). Tests T018–T021 [P] first.
- **US3 (P5)** T026–T029: after US1's resolver exists (extends it). Tests T026–T028 [P] first.
- **US4 (P6)** T030–T032: after US1 (mappers) + T009 (merge). Tests T030/T031 [P] first.
- **Polish (P7)** T033–T038: after the stories that close their evidence. T037 (fuzz) needs the loader entry point (T007–T017); T038 [P] doc anytime after US1.

### MVP
US1 (T001–T017) = the migration MVP: a file loads to an openable bundle. US2 makes it safe (fail-closed diagnostics) and is the P1 companion — ship US1+US2 together as the safe MVP.

### Parallel opportunities
T001/T002 · T005/T006/T010 · all `[P]` test tasks within a story · T013/T014 · T033/T034/T035.

---

## Notes
- TDD: every story's tests are written and FAIL before its implementation (`[const §VII.3]`).
- The three pre-merge grounding tasks (T001 conanfile dep, T002 architecture/check_layers, T033 catalogue/coverage-index) are tracked here per Gate A so they are not lost as prose.
- No new `fixpp_error_t`, no C-ABI/wire/codegen surface; the parser never enters a public header (FR-004).
- Commit after each task or logical group; re-run `codegraph sync` after code-changing tasks.
