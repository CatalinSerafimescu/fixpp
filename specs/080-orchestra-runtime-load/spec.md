# Feature Specification: Orchestra runtime dictionary load for non-C++ consumers

**Feature Branch**: `080-orchestra-runtime-load`

**Created**: 2026-07-19

**Status**: Draft

**Input**: User description: "Expose Orchestra / FIX-Latest runtime dictionary loading to non-C++ consumers (the C-API and TOML config) via root-element sniff-and-dispatch, without adding new public C-API symbols; turn the dual-dictionary registry abort into a clean config-layer error."

## Clarifications

### Session 2026-07-19

- Q: How should the dual-dictionary (FIX50SP2 + FIX-Latest) collision be surfaced — reuse the existing config-invalid error, or a new distinct error code? → A: Introduce a **new distinct** error code/reason (Option B) — operators can distinguish a dictionary-collision from a malformed-config error.
  - **Surface correction (2026-07-19, against the code-surface map — recorded, not silently changed):** the question was originally framed as a C-API `fixpp_error_t` enum append. A `version_registry`-construction census then established that the FIX50SP2+FIX-Latest collision is reachable **only** via the TOML config-file path (`selector_resolver` → `EngineConfig.dictionaries` → the single runtime `version_registry` build at `engine.cpp`); the C-API `fixpp_engine_create` never populates `EngineConfig.dictionaries`, so the collision is **not** C-API-reachable. Therefore the new distinct code is a **new `reason_class` value in the config-layer diagnostics** (`include/fixpp/config/load_diagnostic.hpp`, a `uint8_t` C++ enum surfaced to the config consumer via `LoadResult`/`LoadDiagnostic`), **not** a `fixpp_error_t` addition. Consequence: **no `error.h` edit, no C-ABI version bump, no abidiff/byte-freeze regeneration** — the C-ABI stays frozen at `1.5.0`. The user's Q1=B intent (distinguishability of a dictionary-collision from a malformed document) is fully preserved by the distinct `reason_class`. If a future feature ever exposes multi-dictionary configuration through the C-API, that feature adds the corresponding C-API mapping then (YAGNI here).
- Q: Where is the dual-dictionary collision detected and converted — config-layer pre-check, or modify the registry ctor? → A: **Config/dispatch-layer pre-check** (Option A) — detect the FIX50SP2+FIX-Latest pair and return the new error *before* constructing the registry. `version_registry` (core, `noexcept`) is NOT modified; its existing `std::abort` stays as a last-resort fail-loud invariant for any direct C++ path that bypasses the config layer.

## Context

Feature 074 made FIX Latest a first-class **runtime-loadable** dictionary at the C++ level: `dict::OrchestraLoader::load` reads an Orchestra `<fixr:repository>` document into a runtime `Dictionary`, after which parse / validate / read / encode-by-tag all work data-driven, identically to the nine classic QuickFIX `<fix>` dictionaries loaded by `dict::XmlLoader`.

But runtime **acquisition** of that dictionary is asymmetric and second-class for every non-C++ consumer:

- The C-API entry point `fixpp_dict_load_from_xml` is hard-wired to `dict::XmlLoader`. Feeding it an `OrchestraFIXLatest.xml` returns a config-invalid error rather than loading.
- The TOML `dictionary.path` resolution is hard-wired to `dict::XmlLoader` as well — no way to name an Orchestra document in configuration.
- `dict::OrchestraLoader::load` therefore has **zero non-test production callers**; runtime Orchestra load is reachable only by calling it directly in C++.

This feature closes that gap so that FIX Latest is acquirable at runtime through the same entry points every other dictionary uses.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Load a FIX Latest dictionary through the C-API (Priority: P1)

A non-C++ consumer (a C, Python, or other-language integrator using the shipped C-API) points the existing dictionary-load entry point at an Orchestra `OrchestraFIXLatest.xml` file and receives a valid, usable dictionary handle — the same outcome a C++ caller gets from `OrchestraLoader::load`.

**Why this priority**: This is the core capability gap. Without it, FIX Latest is unreachable from every non-C++ surface, which contradicts the "FIX Latest is a first-class runtime dictionary" guarantee established by 074.

**Independent Test**: Call the C-API dictionary-load entry point with an `OrchestraFIXLatest.xml` path and assert a valid handle is returned; then use that handle to parse/validate a FIX Latest message and confirm the result matches a dictionary acquired via `OrchestraLoader::load` directly.

**Acceptance Scenarios**:

1. **Given** an `OrchestraFIXLatest.xml` (root `<fixr:repository>`), **When** it is passed to the C-API dictionary-load entry point, **Then** a valid dictionary handle is returned (no config-invalid error).
2. **Given** a dictionary loaded via the C-API from `OrchestraFIXLatest.xml`, **When** a FIX Latest message is parsed/validated through it, **Then** the outcome is identical to the same message processed by a dictionary obtained from `OrchestraLoader::load` directly.
3. **Given** a classic `<fix>` dictionary (e.g. `FIX44.xml`), **When** it is passed to the same C-API entry point, **Then** it loads via the classic loader with byte-identical behavior to before this feature (no regression).

### User Story 2 - Name an Orchestra dictionary in TOML configuration (Priority: P1)

An operator configuring the engine via TOML sets `dictionary.path` to an `OrchestraFIXLatest.xml` file and the engine loads FIX Latest at startup, with no code change and no new config key.

**Why this priority**: The TOML config path is the primary way operators select a dictionary without recompiling. Parity with the C-API surface is required for the capability to be genuinely usable, not just reachable.

**Independent Test**: Provide a config whose `dictionary.path` names an `OrchestraFIXLatest.xml`, resolve it, and assert the resulting dictionary parses/validates FIX Latest messages; provide a config naming a classic `<fix>` dictionary and confirm unchanged behavior.

**Acceptance Scenarios**:

1. **Given** a TOML config with `dictionary.path` = an `OrchestraFIXLatest.xml`, **When** the config is resolved, **Then** the FIX Latest dictionary is loaded successfully.
2. **Given** a TOML config with `dictionary.path` = a classic `<fix>` dictionary, **When** the config is resolved, **Then** it loads via the classic loader unchanged (no regression).

### User Story 3 - Dual FIX50SP2 + FIX Latest config fails cleanly instead of aborting (Priority: P2)

An operator whose configuration names **both** a FIX50SP2 dictionary and a FIX Latest dictionary (which today collide on the shared version-registry slot) receives a diagnosable configuration error surfaced through the normal error channel — the process does not abort.

**Why this priority**: The moment configuration can name an Orchestra dictionary, a two-dictionary config that pairs FIX50SP2 with FIX Latest becomes expressible. Today that collision reaches a `std::abort` at registry construction (release-effective). Turning process death into a clean, catchable config-layer error is the minimum safety bar for exposing the path; it is not a full multi-version capability.

**Independent Test**: Provide a config listing both a FIX50SP2 dictionary and an `OrchestraFIXLatest.xml`; assert the load returns/raises a config-layer error (surfaced to the caller) and that the process does **not** abort.

**Acceptance Scenarios**:

1. **Given** a config that names both a FIX50SP2 dictionary and a FIX Latest dictionary, **When** it is resolved, **Then** a clean config-layer error is surfaced to the caller and the process continues running (no `std::abort`).
2. **Given** a single-dictionary FIX-Latest-only config, **When** it is resolved, **Then** it loads successfully (the dual-dictionary guard does not false-trigger on the single-dictionary case).

### Edge Cases

- **Malformed / non-dictionary XML** (root is neither `<fix>` nor `<fixr:repository>`): the entry point returns the existing config-invalid error, unchanged. Root-sniffing only adds a second recognized root; it does not broaden acceptance to arbitrary XML.
- **Well-formed `<fixr:repository>` that fails Orchestra-loader validation** (e.g. structural error inside the repository): surfaces as the Orchestra loader's existing fail-closed parse error through the entry point's error channel, not as a success or an abort.
- **A `<fix>` document that XmlLoader still rejects at the loader-unit level** (the 074 T022h invariant): unchanged — XmlLoader itself continues to reject a `<fixr:repository>` root; only the shared entry-point dispatch, not XmlLoader, gains Orchestra awareness.
- **Empty file / unreadable path**: existing file-error behavior is preserved (sniffing occurs after the document is readable).

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The C-API dictionary-load entry point (`fixpp_dict_load_from_xml`) MUST accept an Orchestra `<fixr:repository>` document and return a valid dictionary handle equivalent to one produced by `OrchestraLoader::load` on the same document.
- **FR-002**: The TOML `dictionary.path` resolution MUST accept an Orchestra `<fixr:repository>` document and load it as a FIX Latest dictionary.
- **FR-003**: Dispatch between the classic and Orchestra loaders MUST be decided by sniffing the document's **root element** (`<fix>` → classic `XmlLoader`; `<fixr:repository>` → `OrchestraLoader`). A document with any other root MUST yield the existing config-invalid error, unchanged.
- **FR-004**: The sniff-and-dispatch logic MUST live in a **single shared dictionary-layer helper** invoked by BOTH the C-API entry point and the TOML resolution, so the dispatch rule is defined once (not duplicated).
- **FR-005**: No **C-ABI change** is made — no new public C-API function symbol, no new `fixpp_error_t` value, and no edit to any byte-frozen C-ABI header (`error.h`/`version.h`). The load capability is delivered by widening the *behavior* of the existing C-API entry point (internal dispatch only); the C-ABI stays frozen at `1.5.0`.
- **FR-006**: Loading a classic `<fix>` dictionary through either entry point MUST remain behaviorally unchanged (byte-identical dictionary, identical parse/validate/read results) versus before this feature.
- **FR-007**: A configuration that names **both** a FIX50SP2 dictionary and a FIX Latest dictionary MUST surface a clean config-layer error to the caller instead of triggering the registry `std::abort` / process termination. The error MUST be a **new distinct `reason_class` value** in the config-layer diagnostics (`load_diagnostic.hpp`), separable from the existing config-invalid / malformed-document reasons, so operators can tell a dictionary-collision apart from a bad document (see Clarifications 2026-07-19). This is a config-layer C++ enum addition surfaced via `LoadResult`/`LoadDiagnostic`; it is **not** a C-ABI change (no `fixpp_error_t` value, no frozen-header edit, no version bump).
- **FR-007a**: The collision MUST be detected by a **pre-check in the config/dispatch layer**, returning the new error *before* the `version_registry` is constructed. The `version_registry` construction path (core, `noexcept`) MUST NOT be modified; its existing `std::abort` remains as a last-resort fail-loud invariant for any direct C++ path that bypasses the config layer.
- **FR-008**: A single-dictionary FIX-Latest-only configuration MUST load successfully; the dual-dictionary guard MUST NOT false-trigger on it.
- **FR-009**: The loader-unit invariant from 074 T022h — `XmlLoader`'s own unit rejects a `<fixr:repository>` root — MUST remain valid. Only the shared entry-point/config contract widens; `XmlLoader` itself is not modified to accept Orchestra roots.

### Contract & Compatibility Notes

- **Contract-widening (must be documented, not silent)**: This is a deliberate widening of the shipped *behavior* of `fixpp_dict_load_from_xml` (052) and of the TOML `dictionary.path` contract. An input (an Orchestra repository document) that previously returned a config-invalid error now loads successfully. This MUST be called out in the spec/plan and captured as a behaviors-and-limitations note. It is **not** a C-ABI change at all: no exported symbol is added or changed, no `fixpp_error_t` value is added, no byte-frozen header is edited, and dispatch is purely internal — so the abidiff/`nm` symbol golden and the `capi_freeze.sha256` header freeze are untouched. The only new distinct error (the dual-dictionary collision, FR-007) lives in the config-layer `reason_class`, which is a C++ surface, not the C-ABI.
- **Out of scope**: Full multi-version `version_registry` re-keying (ApplExtID(1156)=303 differentiation) that would let FIX50SP2 and FIX Latest genuinely coexist. This feature only converts the existing collision **abort** into a diagnosable error. Multi-version coexistence remains tracked under REMAINING-WORK row 4b.
- **Out of scope**: Any codegen / golden / emitter change; any new wire behavior beyond making the already-supported `OrchestraLoader` reachable from the C-API and TOML.

### Key Entities

- **Dictionary document**: an XML file identified at runtime by its root element — `<fix>` (classic tag-value) or `<fixr:repository>` (Orchestra). The root element is the sole dispatch discriminant.
- **Shared load-dispatch helper**: the single dictionary-layer entry that sniffs a document's root and returns a loaded `Dictionary` via the correct loader; consumed by both the C-API thunk and the TOML resolver.
- **Version registry**: the existing structure that maps a session/application version to its dictionary; it retains its current fail-loud `std::abort` on a dual-dictionary collision. The config/dispatch layer pre-empts that abort by detecting the collision and erroring before the registry is constructed (FR-007a) — the registry itself is unchanged.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: An `OrchestraFIXLatest.xml` loaded via the C-API entry point and the same file loaded via `OrchestraLoader::load` directly produce dictionaries that yield identical parse/validate/read outcomes across the FIX Latest message set used in the test corpus (100% agreement, 0 divergences).
- **SC-002**: An `OrchestraFIXLatest.xml` named in TOML `dictionary.path` loads and validates FIX Latest messages successfully (previously impossible — a hard 0→working transition).
- **SC-003**: All existing classic-`<fix>` load paths (every currently-shipped dictionary, via both C-API and TOML) remain behaviorally unchanged — the full existing dictionary-load and parse/validate/read regression suite stays green with zero result changes.
- **SC-004**: A dual FIX50SP2 + FIX Latest configuration returns a config-layer error and the process survives (0 aborts observed across the dual-config test), while a single-FIX-Latest config still loads.
- **SC-005**: The C-ABI is unchanged — the `nm` exported-symbol golden (`tests/abi/golden/fixpp_capi_symbols.txt`) and the header byte-freeze (`tools/capi_freeze.sha256`) both pass without regeneration, and `FIXPP_C_ABI_VERSION` stays `1.5.0`. The only additive surfaces are one new config-layer `reason_class` value and one new public C++ `dict::` load-dispatch symbol.

## Assumptions

- **Decided design (user, 2026-07-18, "Option B — root-sniff"; not re-opened at planning):** dispatch on root element inside the existing entry points via one shared `dict::`-layer helper, rather than adding a new C-API symbol or a new `format`/`kind` config key. `/plan` implements this decision; it is not to be re-litigated.
- The Orchestra loader (`dict::OrchestraLoader`) and its fail-closed parse-error behavior from 074 are reused as-is; this feature adds no new loader logic beyond sniff-and-dispatch and the config-layer collision pre-check (FR-007a).
- The existing config-invalid / file-error dispositions of the C-API and TOML paths are preserved for all inputs other than a well-formed Orchestra document.
- The test corpus for FIX Latest equivalence reuses 074/076-era FIX Latest message fixtures; no new golden artifacts are introduced.
- FIX Latest continues to occupy the shared `v50sp2` version slot (074's L-074-1); this feature does not change version-slot keying, only the failure mode when two dictionaries collide on it.
