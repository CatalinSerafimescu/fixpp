# Feature Specification: Native TOML config-file loading (session establishment)

**Feature Branch**: `044-toml-session-config`
**Created**: 2026-06-19
**Status**: Draft
**Input**: User description: "Native TOML config-file loading for fixpp session establishment — STEP 1 of a 2-step rollout. Session-establishment essentials only; the observability/diagnostics pipeline is a separate later feature (item 14b). A native loader that parses a TOML config file into a fully-validated configuration and FAILS CLOSED before Session::open. PATH-B (pure config translation), opt-in parser dependency outside the embeddable core, per-key error taxonomy, QuickFIX .cfg parity floor."

## Overview & Context *(informative)*

Today every fixpp configuration value is set programmatically in host code. The only file-driven path is the `quickfix_compat` `cfg_loader`, which extracts just three keys (`FileStorePath` / `SenderCompID` / `TargetCompID`) and collapses every failure to a single opaque error. Every incumbent engine an adopter migrates from (QuickFIX-cpp, QuickFIX-J, Fix8) is config-file-driven. Programmatic-only configuration is therefore the single largest migration-friction point for a library that ships under an interop mandate.

This feature delivers a **native config-file loader** that reads a human-authored TOML file and produces a **fully-validated configuration bundle** — the per-session settings plus the file-expressible engine-level establishment settings — which the host then completes with the small set of values no file can express (its application callbacks and its executor) before opening sessions.

This is **step 1 of a deliberate two-step rollout**. Step 1 (this feature) covers everything needed to *establish a session*: identity, transport, message store, dictionary, TLS material, security profile, the behavioral scalars, and reconnect settings. The **observability/diagnostics pipeline** (log sinks, tracer/meter/exporters), the **diagnostic tap**, and **memory-arena selection** are explicitly deferred to step 2 (a separate later feature). The deferred surface is the part adopters almost always wire in code anyway, so deferring it keeps step 1 bounded without reducing migration value.

## Glossary *(informative)*

- **Configuration bundle** — the validated result the loader produces from one file: engine-level establishment settings + one or more session definitions.
- **Bucket-A scalar** — a plain-valued setting (string / number / boolean / duration / enumerated choice) that maps directly from a config key to a field.
- **Object selector** — a config entry of the form *kind + parameters* (e.g. `store = file` with a path) that the loader resolves into a live built-in object via a registry of known kinds.
- **Host-supplied value** — a value no file can express (the application's business callbacks; the host's threading executor). The host injects these into the bundle after loading. (Two host-supplied values are additionally passed *into* the loader as **load inputs** — the engine executor and a cold-path load-time memory resource — because the loader needs them to build the executor-dependent clock/transport objects and to allocate the load-time dictionary/cert objects; the host still sets the same executor instance on the engine and injects the application callbacks after load.)
- **Fail-closed** — on any error the loader returns no usable configuration and the affected session can never be opened; it never silently substitutes a default or proceeds with a partial config.

## Clarifications

### Session 2026-06-19

- Q: How should the loader treat a key it doesn't map — a typo vs. a deferred step-2 key (logger/tracer/tap/arena)? → A: Reject any unrecognized key (fail-closed), but classify a known step-2/deferred key under a distinct "recognized-but-not-yet-supported (step 2)" reason rather than a plain "unknown key" typo.
- Q: When a file has several errors, report all at once or stop at the first? → A: Collect and report ALL per-key errors in a single load pass.
- Q: Relative filesystem paths (store path, PEM cert/key/CA paths) resolve against what base? → A: The directory containing the config file (relocatable bundle), not the process working directory.
- Q: Allow FIX Logon credentials (Username/Password) inline in the file? → A: Yes, allowed inline and always redacted from diagnostics/logs; document operator responsibility to protect file permissions.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Establish a session entirely from a config file (Priority: P1)

An operator migrating from QuickFIX writes a TOML file describing a FIX session — its identity, the peer endpoint and transport security, where messages are stored, which data dictionary applies, and the behavioral knobs — points fixpp at the file, injects only their application callbacks, and brings the session up. No per-field programmatic configuration is required for the common case.

**Why this priority**: This is the migration MVP and the entire reason the feature exists. Without it, an adopter cannot move their existing file-based operational practice to fixpp. Delivered alone it already replaces hand-written configuration for the typical deployment.

**Independent Test**: Author a TOML file populating the required identity/transport/store/dictionary keys plus a representative set of behavioral scalars; load it; confirm the resulting configuration matches an equivalent hand-built configuration field-for-field and that a session opens successfully against a peer.

**Acceptance Scenarios**:

1. **Given** a well-formed TOML file with all required establishment keys, **When** the operator loads it and supplies their application callbacks, **Then** a session is created whose effective settings equal those of the equivalent programmatic configuration, and the session opens.
2. **Given** a loaded configuration, **When** any behavioral scalar (e.g. a reset knob, a validation toggle, a heartbeat interval) is set in the file, **Then** the running session exhibits exactly the behavior that the same value set programmatically would produce.
3. **Given** a TOML file that omits an optional key, **When** it is loaded, **Then** the corresponding field takes its documented explicit default — never an undocumented or implicit one.

### User Story 2 - A bad config is rejected before anything opens, with an actionable diagnostic (Priority: P1)

An operator makes a mistake — a misspelled key, an out-of-range number, an unknown enumerated value, a missing required field, a malformed file, or an unreadable certificate path. The loader rejects the file **before any session is opened** and reports a diagnostic that names the specific offending key, why it failed, and where in the file it is — so the operator can fix it without guesswork. No session is ever left half-configured or in an opened-but-broken state.

**Why this priority**: Fail-closed-before-open is a non-negotiable safety invariant of the codebase; moving configuration into text moves a whole class of errors from compile time to load time, so the load-time diagnostics must be strong enough to compensate. A loader that accepted partial or silently-defaulted configuration would be more dangerous than the programmatic path it replaces.

**Independent Test**: Feed the loader a battery of deliberately broken files — one per error class — and assert that each is rejected, that the diagnostic identifies the correct key and reason, and that no session reaches an open state.

**Acceptance Scenarios**:

1. **Given** a file with an unknown/misspelled key, **When** loaded, **Then** loading fails identifying that key as unrecognized, and no session opens.
2. **Given** a file whose enumerated value is not one of the canonical spellings, **When** loaded, **Then** loading fails identifying the bad value and the legal set, and no session opens.
3. **Given** a file missing a required establishment key, **When** loaded, **Then** loading fails identifying the missing key, and no session opens.
4. **Given** a file that is syntactically malformed, **When** loaded, **Then** loading fails with a parse-location diagnostic, and no session opens.
5. **Given** a file that selects an object whose backing resource is invalid (e.g. a certificate path that does not exist or cannot be parsed), **When** loaded, **Then** loading fails identifying which selector failed and why, and no session opens.
6. **Given** any load failure, **When** the operator inspects the result, **Then** no partially-built session is observable and nothing is left in an opened state.

### User Story 3 - Select built-in objects from the file, not just scalars (Priority: P2)

Beyond plain settings, the operator selects *which* built-in implementation backs each pluggable seam — the message store (file or in-memory), the TLS credential material (certificate/key/CA paths), the data dictionary (by path this step; by-version deferred per FR-007a) and any dialect overlay (deferred), the transport (plaintext TCP or TLS, with host and port), and the clock — by naming a *kind* and its parameters in the file. The loader builds the corresponding built-in object. Behaviors that no file can express remain host-injected.

**Why this priority**: This is what makes the file genuinely replace host wiring rather than just carrying a handful of strings; it is the difference between "configures three keys" and "configures the session." It builds on US1/US2 and is independently demonstrable once the scalar path exists.

**Independent Test**: Author files that exercise each selectable kind (e.g. `store = file` with a path; `store = memory`; `transport = tls` with PEM paths; `transport = tcp`; a dictionary selected by path); confirm each produces a working session backed by the requested built-in, that an unknown kind is rejected per US2, and that a deferred sub-selector (`dictionary.kind="version"`, `dialect_overlay`) is rejected under the distinct "recognized-but-not-yet-supported (step 2)" reason (FR-007a).

**Acceptance Scenarios**:

1. **Given** a file selecting a file-backed store with a storage path, **When** loaded, **Then** the session persists messages to that location.
2. **Given** a file selecting a TLS transport with certificate/key/CA paths and a host/port, **When** loaded, **Then** the session connects over TLS using those credentials.
3. **Given** a file naming an unknown object kind, **When** loaded, **Then** loading fails identifying the unknown kind and the legal set (US2 behavior), and no session opens.
4. **Given** a file that names the threading *policy* but attempts to select the expert direct-executor mode without the required serialization attestation, **When** loaded, **Then** loading fails closed rather than producing an unsafe configuration.
5. **Given** a file that selects `mode="direct_executor"` together with `locks="spin"` (a second unsafe threading combination), **When** loaded, **Then** loading fails closed at load time — regardless of the serialization attestation — rather than letting the combination reach `Session::open()` as one opaque failure (FR-011a).
6. **Given** a file naming a deferred sub-selector (`dictionary.kind="version"` or a `dialect_overlay`), **When** loaded, **Then** loading fails under the "recognized-but-not-yet-supported (step 2)" reason, distinct from an unknown-key typo (FR-007a).

### User Story 4 - Operators recognize the configuration vocabulary they already know (Priority: P3)

An operator who runs QuickFIX today can express, in the new file, everything their existing QuickFIX configuration expresses for session establishment, using key names they recognize. Anything a QuickFIX configuration can set for establishing a session has an equivalent here.

**Why this priority**: Vocabulary familiarity lowers the migration cliff and is the concrete meaning of the "parity floor," but it is a refinement on top of a working loader rather than a prerequisite for one.

**Independent Test**: Take the set of QuickFIX configuration settings that pertain to session establishment and confirm each has a documented equivalent key in the new format that produces the same effect.

**Acceptance Scenarios**:

1. **Given** the catalogue of QuickFIX establishment settings, **When** compared against this loader's accepted keys, **Then** every QuickFIX establishment setting has a documented equivalent.
2. **Given** a file describing several sessions plus shared defaults (mirroring a QuickFIX multi-session file), **When** loaded, **Then** each session is produced with the shared defaults applied and per-session overrides honored.

### Edge Cases

- A file that defines **multiple sessions** with a shared defaults section: each session inherits defaults and may override them; an override of a key not present in defaults is still valid.
- A key present but **empty** (e.g. an empty string for a required identity): treated as a validation failure, not as "absent → default."
- A value that is **type-correct but out of range** (e.g. a zero or negative timeout where a positive value is required): rejected with a range diagnostic.
- A **duration** expressed in the file (e.g. a heartbeat interval): the accepted spelling/units are explicit and unambiguous; an ambiguous or unitless value is rejected.
- A selector that is **internally contradictory** (e.g. a plaintext transport that also supplies TLS material, or a TLS transport missing required credentials): rejected with a diagnostic naming the contradiction.
- A cert selector referencing an **encrypted** PEM private key: this step accepts **plaintext (unencrypted) keys only** (a file cannot express a passphrase securely). The encrypted key is rejected fail-closed as an invalid/contradictory cert selector (a graceful load diagnostic, **not** a crash and not a silent default), so the boundary is visible to the operator (FR-006b / FR-016).
- A session selecting `security_profile.kind="mtls_pinned"`: recognized but **not selectable from a file** this step (pin material has no file channel). Rejected under the "recognized-but-not-yet-supported (step 2)" reason, distinct from an unknown-enum typo (FR-006b / FR-018a).
- A file that supplies a value for something this step has **deferred or that is host-supplied** (e.g. a log-sink, an executor instance, an application): rejected fail-closed rather than silently ignored, so the boundary is visible to the operator. A *known* step-2/deferred key is reported under a distinct "recognized-but-not-yet-supported (step 2)" reason; an unrecognized key is reported as an unknown-key (likely typo) error (per Clarifications).
- A file whose **on-disk resource changes** between load and open: configuration is frozen at load/open per the existing frozen-at-open invariant; later file edits have no effect until an explicit reload-and-reopen.

## Requirements *(mandatory)*

### Functional Requirements

**Loading & translation**

- **FR-001**: The system MUST load a human-authored TOML configuration file and produce a configuration bundle consisting of the file-expressible engine-level establishment settings plus one or more session definitions.
- **FR-002**: The system MUST translate configuration purely into the existing public configuration value-types (pure config translation); it MUST NOT introduce a parallel runtime adapter or alter session runtime behavior beyond what the equivalent programmatic configuration produces.
- **FR-003**: A configuration produced from a file MUST be behaviorally indistinguishable from the equivalent programmatically-built configuration (the file is an input format, not a behavior change).
- **FR-004**: The configuration-file parser and loader MUST live outside the embeddable core such that a host that does not use file-based configuration does not take on the parser dependency.

**Scope — in (session establishment)**

- **FR-005**: The loader MUST accept and map the full set of session-establishment behavioral scalars: identity (sender/target identifiers, protocol begin-string), role, threading policy and lock policy (with the safety guard below), heartbeat interval, test-request and sending-time thresholds, reject policy, application back-pressure mode, sequence-number reset policy, the reset-on-{logon,logout,disconnect} knobs, refresh-on-logon, logout-disconnect timeout, redeliver/allow possible-duplicate knobs, sending-time precision, next-expected-sequence-number enablement, check-comp-id and validate-sequence-numbers and validate-inbound-messages toggles, default application version, and optional username/password credentials.
- **FR-006**: The loader MUST accept and map the structured establishment members: the security profile, the CompID authorization policy (a table of principal → permitted CompIDs), the reconnect endpoint (host/port), and the reconnect/backoff policy.
- **FR-006a**: `security_profile.kind` is a **required per-session key** (after `[default]` merge): a session whose `security_profile.kind` is absent → `missing_required`; present-but-empty → `empty_required`. No-implicit-default (`[const §XII.5]`) is enforced **at the loader boundary, per-key** — this is the primary boundary; the `Session::open()` `kind::unset` reject is retained as defense-in-depth, not the primary check.
- **FR-006b**: The step-1 **accepted** security profiles are `{mtls_ca, one_way_ca, insecure_plain_tcp}`. `security_profile.kind="mtls_pinned"` is **recognized but not selectable from a file in this step** and MUST be rejected under the "recognized-but-not-yet-supported (step 2)" reason class (FR-018a), exactly like the deferred sub-selectors of FR-007a — because `mtls_pinned` requires a non-empty pin set at config-build time and no file input can express pin material (the only population path takes a parsed runtime certificate object; pins are by-design runtime-rotated). The programmatic path to `mtls_pinned` is unaffected; only the FILE cannot select it this step, pending a future file-based pin-set/rotation slice. Additionally, this step supports **plaintext (unencrypted) private keys only**: a config file cannot express a key passphrase securely, so a referenced **encrypted** PEM key is not a deferral but a fail-closed cert-load failure (see the encrypted-key edge case + FR-016).
- **FR-007**: The loader MUST resolve the in-scope object selectors from *kind + parameters*: message store (file or in-memory), TLS credential material (certificate / key / CA / chain paths and associated limits), data dictionary (**by path only** this step — see FR-007a), transport (plaintext TCP or TLS, with host/port), and clock (system).
- **FR-007a**: Two sub-selectors are recognized but **not resolvable in this step** and MUST be rejected under the "recognized-but-not-yet-supported (step 2)" reason class (FR-018a), never silently ignored: (1) the **dialect overlay** selector — the overlay-loading API is not yet public (deferred to a future dictionary feature); (2) the **dictionary `kind="version"`** selector — version→path resolution is not self-contained in a single file (the version registry is only populated from dictionaries already loaded into the engine; nothing in the file populates it), so by-version is deferred (research OQ-1 → option A). The plain dictionary selector **by path** (`kind="path"` + a `path`) is fully in scope and meets the QuickFIX parity floor (QuickFIX is path-based, `DataDictionary=FIX44.xml`).
- **FR-008**: The loader MUST support a file describing **multiple sessions** with a shared-defaults section, applying defaults to each session and honoring per-session overrides (QuickFIX multi-session parity).

**Scope — out (deferred / host-supplied)**

- **FR-009**: The loader MUST NOT, in this step, configure the observability/diagnostics pipeline (log sinks, tracer/meter/exporters), the diagnostic tap, or memory-arena selection; these are reserved for the step-2 feature.
- **FR-010**: The loader MUST NOT attempt to express host-supplied behaviors that no file can express — the application business callbacks, the executor instance, host-written custom object kinds, or an already-existing shared host execution context — and MUST require the host to inject these into the bundle before a session is opened.
- **FR-011**: The threading configuration MUST allow selecting the threading *policy* but MUST NOT permit selecting the expert direct-executor mode without the accompanying serialization attestation; a file that attempts the former without the latter MUST fail closed.
- **FR-011a**: The loader MUST reject the `direct_executor` + `spin` combination (`mode="direct_executor"` with `locks="spin"`) at LOAD as a contradictory/invalid threading selector, **regardless of the serialization attestation** — this is a second unsafe combination the engine rejects at `Session::open()` (the always-mutex store-write path has no engine-internal serialisation to fall back on under a bare attested executor), so it MUST be caught per-key at load rather than reaching open as one opaque failure (research D-6b).

**Validation & safety (fail-closed)**

- **FR-012**: The loader MUST validate the entire configuration and MUST fail closed before any session is opened; on any failure no session is created and nothing is left in an opened or partially-configured state.
- **FR-013**: The loader MUST apply no implicit defaults: an absent optional key yields its documented explicit default, and a required key that is absent or empty is an error.
- **FR-014**: The loader MUST accept only the canonical spellings of enumerated values and MUST reject any unknown token, reporting the legal set.
- **FR-015**: Configuration MUST remain frozen at load/open; subsequent edits to the source file MUST NOT affect a running session until an explicit reload-and-reopen.
- **FR-016**: When an object selector's backing resource is invalid (unreadable/unparseable/contradictory), the loader MUST fail closed and attribute the failure to the specific selector.
- **FR-016a**: Relative filesystem paths in the file (message-store path, PEM certificate/key/CA paths, dictionary path) MUST resolve against the **directory containing the config file**, not the process working directory, so a config bundle is relocatable and launch-CWD-independent (per Clarifications). Absolute paths are used as-is.

**Diagnostics (per-key error taxonomy)**

- **FR-017**: On any load failure the system MUST produce a diagnostic that identifies the specific offending key (or selector), the reason class (unknown key, **recognized-but-not-yet-supported (step 2)**, missing required, malformed value, out-of-range, unknown enum, invalid/contradictory selector, parse error), and the location within the file.
- **FR-018**: The diagnostics MUST distinguish these reason classes from one another (replacing the legacy single collapsed error) so that a file with many keys yields actionable, per-key feedback. The loader MUST **collect and report ALL errors found in a single load pass** (not stop at the first), so an operator can fix a multi-error file in one iteration (per Clarifications).
- **FR-018a**: An unrecognized key MUST be rejected as an unknown-key error; a key or **recognized-but-deferred selection** belonging to the known deferred step-2 surface MUST instead be rejected under the distinct "recognized-but-not-yet-supported (step 2)" reason — never silently ignored (per Clarifications). The deferred step-2 set is: observability sinks/exporters, tap, arena selection (FR-009); the `dialect_overlay` selector and `dictionary.kind="version"` (FR-007a); and `security_profile.kind="mtls_pinned"` (FR-006b). (An **encrypted** PEM private key is NOT in this set — it is a runtime cert-load failure reported as an invalid/contradictory selector per FR-016, not a recognized-but-deferred selection; step 1 is plaintext-key-only per FR-006b.)
- **FR-019**: FIX Logon credentials (Username/Password) MAY be supplied inline in the file; any sensitive value (credential/password) MUST always be redacted from diagnostics and logs, consistent with existing redaction behavior. (Operator guidance to protect file permissions is documented; no env-var/indirection mechanism is required in this step — per Clarifications.)

**Parity & continuity**

- **FR-020**: Every QuickFIX configuration setting that pertains to session establishment MUST have a documented equivalent key in this loader (parity floor).
- **FR-021**: The feature MUST document explicitly that moving configuration into text relocates a class of type/spelling errors from build time to load time, and MUST mitigate this with strict load-time validation (FR-012–FR-018).
- **FR-022**: The loader MAY reuse or share mechanics with the existing `quickfix_compat` configuration resolution, but MUST be an independent native path and MUST NOT regress the existing compat behavior.

### Key Entities

- **Configuration bundle**: the validated output of one load — engine-level establishment settings plus one or more session definitions; the unit the host completes with host-supplied values and then opens.
- **Session definition**: the per-session settings and selected objects sufficient (together with host-supplied values) to open one session.
- **Shared defaults**: a section whose values apply to every session definition in the file unless a session overrides them.
- **Object selector**: a *kind + parameters* entry the loader resolves into a built-in object (store / TLS material / dictionary / transport / clock).
- **Load diagnostic**: a structured per-failure report carrying offending key/selector, reason class, and file location.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: An operator can bring up a session using only a configuration file plus injected application callbacks — zero per-field programmatic configuration — for the common establishment case.
- **SC-002**: A configuration loaded from a file yields a session whose observable behavior is identical to the equivalent programmatically-configured session across the full in-scope scalar and object set (verified field-for-field / behavior-for-behavior).
- **SC-003**: For every defined failure class, a deliberately broken file is rejected before any session opens, and the diagnostic correctly identifies the offending key/selector and reason — with zero cases of silent default substitution or partial configuration.
- **SC-004**: 100% of QuickFIX session-establishment settings have a documented equivalent key (parity floor met and demonstrated by a parity table).
- **SC-005**: A multi-session file produces each session with shared defaults applied and per-session overrides honored.
- **SC-006**: A host that does not use file-based configuration incurs no additional dependency from this feature (the parser stays out of the core).
- **SC-007**: A file containing N independent errors yields N distinct per-key diagnostics from a single load attempt (no fix-one / re-run loop); a known deferred step-2 key is reported under its own reason, distinct from an unknown-key typo.

## Assumptions

- **Format is TOML** (decided): nested tables, typed values, and comments; not the flat legacy INI shape. The specific parser library is an implementation choice for planning, not part of this spec.
- **Configuration target**: the loader hydrates the existing public configuration value-types for session establishment, i.e. the per-session settings and the file-expressible engine-level anchors a session references (dictionaries, default store/cert/transport factories, clock). The engine layer is otherwise predominantly host-supplied and out of scope.
- **Host completion**: every deployment retains a small stub — inject application callbacks, provide the executor, optionally register host-written custom kinds — then load and open. This is by design (FR-010), not a gap.
- **Establishment-only scope**: the observability/diagnostics pipeline, the diagnostic tap, and memory-arena selection are deferred to step 2 (item 14b) and are intentionally not configurable here.
- **Reuse**: existing single-key store resolution in `quickfix_compat` is the proof-of-concept for the *kind + parameters* pattern and may inform the implementation; the native path does not depend on the legacy loader's behavior.
- **No new wire/protocol surface**: this feature changes how configuration is *supplied*, not what goes on the wire; it introduces no new FIX behavior.

## Dependencies

- The existing public configuration value-types for sessions and the engine (the loader maps onto them; it does not redefine them).
- The existing built-in object factories for the in-scope selectors (store, TLS material, transport, dictionary loader, clock).
- The existing fail-closed, frozen-at-open, no-implicit-default, and credential-redaction invariants, which this feature must preserve at the text boundary.

## Normative References

This feature is **design-blessed**, not a FIX-spec-coverage feature: there is no normative FIX specification section that mandates a TOML config loader. Its authority is the constitution and the QuickFIX configuration vocabulary it targets for parity. Per Article VI §3 the catalogue row is a design row (`[const §XV.16]`), not an `OFFICIAL` spec-coverage row, and `coverage-index.md` records it as a design choice.

- **`[const §XV.16]`** — *Custom config format ban / TOML acceptance.* "We accept QuickFIX CFG verbatim; TOML is also accepted; new formats require justification." This feature is the native TOML acceptance path; it introduces no new format.
- **`[const §XII.5]`** — *Session requires an explicit `SecurityProfile`; no implicit default.* The loader MUST preserve no-implicit-default at the text boundary: `security_profile.kind` is a **required per-session key** rejected per-key at load (`missing_required`/`empty_required`, FR-006a) — this is the primary boundary; the `Session::open()` `kind::unset` reject (`session.cpp:938`) is retained as defense-in-depth. Step-1 accepted profiles are `{mtls_ca, one_way_ca, insecure_plain_tcp}`; `mtls_pinned` is recognized-but-deferred (FR-006b). `insecure_plain_tcp` is never an implicit default (it carries the loud `[[deprecated]]`-class friction at the enumerator the operator selects).
- **`[const §XII.7]`** — *`EncryptMethod(98)` ≠ 0 rejected.* Unaffected; the loader configures transport security only.
- **`[const §X.4]`** — *C-ABI error reporting / `fixpp_error_t` stability.* The loader introduces **no new `fixpp_error_t` value**; its per-key diagnostics are a C++-only loader-local type (FR-017/FR-018) and never cross the C ABI.
- **`[const §XIV.1/§XIV.2]`** — *Pluggable interfaces with one default impl each (≤5 pure-virtuals).* The loader resolves the **existing** pluggable defaults (store / cert_source / dictionary / transport / clock); it adds no new pluggable interface.
- **`[const §XV.1]`** — *No hot-path heap allocation.* The loader is a cold, load-time path (executed once before `Session::open`); §XV.1 does not constrain it, and it touches no parse→dispatch hot path.
- **QuickFIX `[DEFAULT]`/`[SESSION]` cfg vocabulary** (the parity floor, FR-020 / SC-004) — the catalogue of QuickFIX session-establishment settings, sourced from the cloned QuickFIX-cpp reference source, is the parity oracle. (`OSS-002`, `feature-catalogue.md`.)
- **Precedent (non-normative):** `008-message-store` FR-030 `quickfix_compat::cfg_loader` — the single-key `store=` → `FileStoreFactory` translation is the *kind + parameters* existence proof; this feature is an independent native path (FR-022) and does **not** isolate the parser the way this feature must (cfg_loader compiles into `fixpp_session`).

## Out of Scope (this step)

- Observability/diagnostics configuration: log sinks, tracer/meter/exporters.
- Diagnostic tap configuration.
- Memory-arena selection.
- Expressing host-supplied behaviors in the file: application callbacks, the executor instance, host-written custom kinds, a shared pre-existing execution context.
- File-based selection of the **`mtls_pinned`** security profile (no file channel for pin material — pins are by-design runtime-rotated, FR-006b); deferred to a future file-based pin-set/rotation slice.
- **Encrypted PEM private keys** (no file channel for the key passphrase — step 1 is plaintext-key-only, FR-006b).
- Any change to FIX wire behavior, error codes, generated code, or the C-ABI surface.
