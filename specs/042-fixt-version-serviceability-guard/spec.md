# Feature Specification: FIXT version-registry serviceability guard at open()

**Feature Branch**: `042-fixt-version-serviceability-guard`
**Created**: 2026-06-17
**Status**: Draft
**Input**: User description: "Acceptor FIXT version-registry serviceability guard at open() (closes L-033-5 'A+'). Extend the Session::open() config-load guard so a FIXT session whose configured default_appl_ver_id cannot be served by the engine version registry fails closed at open()-time, instead of opening and silently rejecting every inbound FIXT Logon."

## Context & Background

Feature 033 (FIXT.1.1 / FIX 5.0 SP2 session) established that an inbound FIXT Logon whose
`DefaultApplVerID(1137)` resolves to an application version with **no** application dictionary
registered in the engine version registry is refused with `Reject(35=3, 371=1137, 373=5)` and never
reaches Active (FR-004a — spec-correct: no silent mis-versioned establishment).

The live 033 acceptor cells surfaced an operator footgun, recorded as **L-033-5** in
`spec/behaviors-and-limitations.md`: an acceptor configured with a `default_appl_ver_id` (e.g.
FIX50SP2) whose application dictionary is **not** registered in the engine **silently rejects every
inbound FIXT Logon**. `Session::open()` succeeds, the operator believes the session is healthy, yet
no peer can ever establish — because the runtime serviceability gate consults the engine registry,
not the session's own configured default. The misconfiguration is invisible until live traffic
fails. L-033-5 left an explicit "open question for Gate-B adjudication"; the **adjudicated
disposition** (per `REMAINING-WORK.md` line 51 and `library/CLAUDE.md`) is the **fail-closed
open()-guard** — validate serviceability of this side's own configured default at config-load time,
the way QuickFIX validates `DataDictionary` presence at config-load. The alternative
(self-register / fall back to the configured default so the registry need only carry additional
versions) is adjudicated **against** and is out of scope.

This is the **last residual bullet** of the Fable F-f release-gate hardening tail.

## Clarifications

### Session 2026-06-17

- Q: Role scope — should the open()-time serviceability guard apply role-agnostically (both acceptor
  and initiator) or acceptor-only? → A: **Role-agnostic** (both roles). The MANDATORY fail-closed
  serviceability requirement is grounded in fixpp's own L-033-5 disposition, the symmetric `open()` path,
  and Constitution §XII.5 fail-closed posture. QuickFIX-cpp `SessionFactory::create` (verified in
  `reference-engines/quickfix-cpp/src/C++/SessionFactory.cpp:38-68,279-307`; the path is
  parent-repo-relative) corroborates *role-independence* only: it performs FIXT app-dictionary
  config-load processing **role-independently when data dictionaries are enabled/configured** (the only
  role-specific check there is the unrelated `SessionQualifier`).
  This config-load guard is orthogonal to 033 FR-004a, which governs the **runtime** disposition for a
  **peer-advertised** unserviceable version (acceptor-scoped) — not this side's own configured default.
  Role-agnostic is the least-code path (one shared `open()` guard, no role gate) and also closes the
  initiator misconfiguration footgun.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Misconfigured FIXT session fails closed at open() (Priority: P1)

An operator configures a FIXT session (`begin_string = "FIXT.1.1"`) with a `default_appl_ver_id` for
an application version whose dictionary they forgot to register in the engine. Instead of the session
opening "successfully" and then silently refusing every counterparty, `Session::open()` fails
immediately at config-load with a configuration error, so the operator learns of the misconfiguration
before any traffic — exactly as a missing `DataDictionary` is caught at config-load in QuickFIX.

**Why this priority**: This is the entire feature — it converts a silent runtime footgun into a loud
config-load failure. It is the sole deliverable and the close-out of L-033-5.

**Independent Test**: Construct a FIXT session whose engine registry does not carry the dictionary for
the configured `default_appl_ver_id`; call `open()`; assert it returns the configuration error and the
session never reaches an operable state. Mutation check: remove the new guard ⇒ `open()` succeeds and
the test fails.

**Acceptance Scenarios**:

1. **Given** a FIXT session configured with a `default_appl_ver_id` that the engine version registry
   cannot serve (no application dictionary registered for it), **When** `open()` is called, **Then**
   it fails closed with the configuration-invalid error before any observable state mutation or wire
   emission.
2. **Given** a FIXT session whose configured `default_appl_ver_id` **is** serviceable by the engine
   registry, **When** `open()` is called, **Then** it succeeds and the session establishes exactly as
   it does today (behaviour byte-identical to current).
3. **Given** the unserviceable-configured-default condition holds, **When** `open()` is called on
   **either** an acceptor or an initiator FIXT session, **Then** both fail closed identically (the guard
   is role-agnostic — see FR-008 / Clarifications).

### Edge Cases

- **Peer advertises a different (unserviceable) version than this side's configured default.** The
  new open()-time guard validates **this** side's own `default_appl_ver_id`. The existing inbound
  runtime check on the **peer-advertised** `DefaultApplVerID(1137)` (033 FR-004a, `Reject 373=5`)
  remains live and unchanged — a peer can still advertise a version different from this side's default,
  and that path must still refuse at runtime.
- **Non-FIXT session (`begin_string != "FIXT.1.1"`).** Unaffected — the guard is gated on the FIXT
  begin-string, so every FIX.4.x session opens byte-identically.
- **FIXT begin-string with no `default_appl_ver_id` set**, or **a null engine registry handle**:
  already refused by the existing FQ-1 open()-guard (those two disjuncts are unchanged); this feature
  adds the "registry present but cannot serve the configured default" disjunct.
- **Engine registry carries the configured default plus other versions**: serviceable ⇒ open()
  succeeds (the registry need only serve the configured default for this guard).

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: When a session's `begin_string` is `"FIXT.1.1"` and a `default_appl_ver_id` is
  configured, `Session::open()` MUST verify that the engine version registry can **serve** that
  configured version (i.e. an application dictionary is registered for it), and MUST fail closed with
  the configuration-invalid error (`error::invalid_session_config`) when it cannot.
- **FR-002**: The serviceability failure MUST occur at `open()`-time, before any observable state
  mutation or wire emission (fail-closed, consistent with the sibling FQ-1 / credential / security-
  profile open()-guards).
- **FR-003**: A FIXT session whose configured `default_appl_ver_id` **is** serviceable MUST open and
  behave byte-identically to current behaviour (no regression on the correctly-configured path).
- **FR-004**: All non-FIXT sessions (`begin_string != "FIXT.1.1"`) MUST be entirely unaffected
  (byte-identical open() behaviour).
- **FR-005**: The existing inbound runtime serviceability check on the **peer-advertised**
  `DefaultApplVerID(1137)` (033 FR-004a — `Reject(35=3, 371=1137, 373=5)`) MUST remain live and
  unchanged. This feature adds a config-load guard on **this** side's own configured default; it does
  not replace, weaken, or make dead the inbound peer-version check.
- **FR-006**: The chosen disposition MUST be the fail-closed config-load guard. The self-register /
  fall-back-to-configured-default alternative is out of scope (adjudicated against).
- **FR-007**: This feature MUST NOT introduce any new public wire field, error slot, configuration
  field, codegen output, or C-ABI surface. It reuses the existing `error::invalid_session_config`
  disposition and the existing engine version registry.
- **FR-008 [Role scope]**: The guard MUST apply **role-agnostically** — to both acceptor and initiator
  FIXT sessions — at the shared `Session::open()` config-load path, with no role gate (resolved in
  `/speckit-clarify`; see Clarifications). QuickFIX performs FIXT app-dictionary config-load processing
  role-independently when data dictionaries are enabled/configured (corroborating reference-engine
  evidence for role-independence only; the mandatory requirement is grounded in fixpp's L-033-5 +
  §XII.5), and it does not contradict 033 FR-004a (which scopes
  only the **runtime** peer-advertised-version refuse to the acceptor). The implementer MUST confirm the
  existing initiator FIXT session tests stay green under the role-agnostic guard (the `FixtSetup`
  fixture registers the matching dictionary for both roles, so no regression is expected). **Two
  existing inbound witnesses** — `W3_Unserviceable1137_AcceptorRejectsWithVII_NotActive`
  (`tests/session/test_fixt_logon_establishment.cpp:887`) and
  `W_Unserviceable1137_ToAdminObserved_ValueIsIncorrect_Disconnected`
  (`tests/session/test_fixt_logon_establishment.cpp:1302`) — configure this side's OWN
  `default_appl_ver_id = v50sp2` against a v44-only registry and rely on `open()` succeeding before
  injecting the peer `1137=9` frame; under 042 `open()` now fails for them. They MUST be rewritten so
  this side's own default is serviceable (preserving the inbound 033 FR-004a reject coverage), NOT
  edited-green by dropping their inbound-reject assertions — see research.md D-2 / D-2a.

## Normative References

Per `[const §VI.5]` — the exact coverage-index / feature-catalogue entries this feature informs:

- `[FIX-SL §4.3.7]` Specifying application version — `DefaultApplVerID(1137)` on the FIXT Logon, the
  configured-default application version whose serviceability this guard validates at `open()`-time
  (catalogue **S-025**; coverage-index §4.3.7).
- `[FIX-SL §4.2.1]` The FIX session profile — `BeginString(8)` = `FIXT.1.1` session-profile
  identification, the gate that scopes this guard to FIXT sessions (catalogue **S-020**, FIXT.1.1 half;
  coverage-index §4.2.1).

> **Reference Engine Evidence (not a normative FIX ref).** QuickFIX-cpp `SessionFactory::create`
> (`reference-engines/quickfix-cpp/src/C++/SessionFactory.cpp` — parent-repo-relative path) corroborates
> that FIXT app-dictionary config-load processing is role-independent **when data dictionaries are
> enabled/configured**. This is supporting evidence for the role-agnostic scope only; the mandatory
> fail-closed serviceability requirement is grounded in fixpp's own L-033-5 disposition, the symmetric
> `open()` path, and Constitution §XII.5.

### Key Entities

- **Configured default application version** (`SessionConfig::default_appl_ver_id`): this side's own
  advertised application version for a FIXT session; an optional application-version value.
- **Engine version registry** (`dict::version_registry`, engine-lifetime, non-owning handle on the
  session): the authority on which application versions the engine can **serve** (has an application
  dictionary registered for). "Serviceable" = the registry has an entry for the version.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A FIXT session configured with an unserviceable `default_appl_ver_id` is rejected at
  `open()`-time in 100% of cases (no path where it opens and then silently rejects inbound Logons).
- **SC-002**: A FIXT session configured with a serviceable `default_appl_ver_id`, and every non-FIXT
  session, open with byte-identical behaviour to the pre-feature baseline (0 regressions across the
  existing session/FIXT test suite and the live interop matrix).
- **SC-003**: The inbound peer-advertised-`1137` unserviceable refuse (033 FR-004a) continues to fire
  on a peer advertising an unserviceable version different from this side's serviceable default
  (the runtime check is demonstrably still live, not made dead by the new guard).
- **SC-004**: L-033-5 is dischargeable — the operator footgun is removed and the limitation row can be
  marked RESOLVED with a code reference.

## Assumptions

- The engine version registry's "can serve version V" query is the same authority the inbound runtime
  check already uses (`app_version_registry_->get(...)`); this feature reuses it at open() rather than
  introducing a new notion of serviceability.
- `SessionConfig::default_appl_ver_id` already holds the resolved application-version value (not a raw
  wire string), so the open()-time check needs no wire-string resolution step — unlike the inbound
  path which resolves the peer's `1137` wire value first.
- The fix lives at the existing shared `Session::open()` config-load validation block (alongside the
  FQ-1 FIXT guard, the security-profile sentinel guard, and the credential guards); it is a guard
  extension, not a new validation phase.
- "Like QuickFIX" refers to QuickFIX validating `DataDictionary` presence at config/session-settings
  load time and refusing to start a misconfigured session, rather than deferring the failure to the
  first inbound message.
