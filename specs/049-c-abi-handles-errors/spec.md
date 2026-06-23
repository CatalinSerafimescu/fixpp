# Feature Specification: C ABI engine surface — Feature A (handles, error surface, version negotiation)

**Feature Branch**: `049-c-abi-handles-errors`
**Created**: 2026-06-23
**Status**: Draft
**Input**: User description: "C ABI engine surface — Feature A: handle types, error surface, and version negotiation (CA-001..004)."

> **Workstream context.** This is the first of three C-ABI features (A.1 in the v1.0 tracker). Feature A lays the **foundation surface** — handle catalogue, error model, version accessors — that the later features build on. Feature B (CA-005..007: session lifecycle / send / receive callback) and Feature C (CA-008..010: field accessors / repeating groups) follow in separate specs. Python bindings (PY-001..005) are blocked until Feature C merges. Authoritative contract: `.specify/2i-capi.md` ([2i]); ABI policy: `.specify/constitution.md` Article X.

## Clarifications

### Session 2026-06-23

- Q: C-ABI version number for Feature A — stay pre-1.0 or freeze at 1.0.0 now? → A: Stay 0.x (additive MINOR bump 0.1.0 → 0.2.0); the 0→1 major freeze is deferred to GA after all CA features ship, per `remaining-work/release-engineering.md` Task 2 (lines 54 & 106). (Ratifies FR-018.)
- Q: Commit the full master error-enum layout, or only Feature-A-reachable codes? → A: Publish the full `[2i §4.3]` per-domain numeric layout now (all blocks, even for functions arriving in later features); `error_codes_v1.txt` is born complete and later features add functions, not numbers. (Ratifies FR-006 / FR-012.)
- Q: How to make the version-downgrade logic (FR-009) testable given `fixpp_engine_create` is Feature B's? → A: Implement downgrade as a pure `translate(error, consumer_minor)` unit-tested in isolation; declare the engine_create binding as a named limitation for the completeness audit. (Ratifies FR-017 + the engine-create-deferral assumption.)
- Q: Provisional decimal error codes (3/10/11) conflict with `[2i §4.3]` master values (6/800/801) — which way? → A: Re-number to the master layout; census every reference and re-capture the abi-golden baseline. Permitted pre-1.0. (Ratifies FR-011.)

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Detect engine/library version and ABI compatibility (Priority: P1)

A developer integrating fixpp from a C program (or building a language binding on top of the C ABI) needs to know, at runtime, which engine version they are linked against and whether that engine's C ABI is compatible with the header they compiled against — before they rely on any other call.

**Why this priority**: Version/compatibility detection is the entry point for every other C-ABI interaction. Without it a consumer cannot safely decide whether the rest of the surface behaves as their header promises. It is also self-contained and deliverable independently of handles and message access.

**Independent Test**: Compile a C program against the published header, call the runtime version accessor, and compare the returned major/minor/patch against the compile-time version macros. A matching major confirms compatibility; a mismatching major is reported as an incompatibility per the documented protocol.

**Acceptance Scenarios**:

1. **Given** a C consumer compiled against the published C-ABI header, **When** it queries the runtime engine version, **Then** it receives a plain value-type carrying major/minor/patch that equals the engine binary's C-ABI version.
2. **Given** a C consumer compiled against the published header, **When** it queries the separate library (C++ surface) version, **Then** it receives that version independently of the C-ABI version (the two version tracks are decoupled).
3. **Given** the compile-time version macros and the runtime accessor, **When** the consumer compares them, **Then** equality of the major component indicates a compatible surface and inequality of the major component indicates a hard incompatibility.

---

### User Story 2 - Receive a stable, human-readable error from every call (Priority: P1)

A C consumer needs every fallible C-ABI call to report outcomes as a stable numeric code drawn from a documented, bounded set, and needs a way to turn any code into a human-readable string for logs and diagnostics — including codes its own (older) header does not yet define.

**Why this priority**: A predictable error model is a precondition for using any other C-ABI function safely. The numeric layout is a frozen contract that all later features append to, so it must be defined coherently and first.

**Independent Test**: Enumerate every published error code, pass each to the string-lookup function, and confirm a non-null, stable description is returned with no memory allocation. Pass an out-of-range / undefined value and confirm it maps to the documented "unknown error" description rather than crashing.

**Acceptance Scenarios**:

1. **Given** any published numeric error code, **When** the consumer asks for its description, **Then** a non-null, statically-stored, human-readable string is returned that the consumer must not free.
2. **Given** a numeric value the consumer's header does not define (e.g. produced by a newer engine), **When** the consumer asks for its description, **Then** the documented "unknown error" description is returned (no crash, no allocation).
3. **Given** an engine newer than the consumer that would otherwise surface a code introduced after the consumer's recorded ABI minor version, **When** that outcome crosses the boundary, **Then** the code is downgraded to the "unknown error" sentinel before the consumer sees it (forward-compatibility), per the documented downgrade rule.
4. **Given** the published numeric layout, **When** the occupancy / audit tooling runs in CI, **Then** it confirms no numeric slot has been re-used or re-defined relative to the checked-in audit record.

---

### User Story 3 - Build against a stable opaque-handle catalogue (Priority: P2)

A C consumer (and downstream binding authors) needs a fixed catalogue of opaque handle types with a per-handle ownership/destroy/invalidation discipline per `[2i §4.2.1]` and well-defined behaviour when a null or already-destroyed handle is passed, so they can write forward-compatible code against handles whose concrete operations land in later features.

**Why this priority**: The handle catalogue and its plumbing rules must exist before any function that produces or consumes a handle (Features B/C). Establishing the catalogue and the null/invalid-handle contract now prevents churn in the later features. It depends on the error model (US2) being in place.

**Independent Test**: Confirm each handle type is exposed as an incomplete (opaque) type that leaks no C++ symbols across the boundary (verified by the symbol-visibility gate), and that passing a null handle to a handle-taking function yields the documented null-handle error rather than undefined behaviour.

**Acceptance Scenarios**:

1. **Given** the published header, **When** a consumer inspects the handle types, **Then** each is an opaque/incomplete type with a documented per-handle destroy/invalidation discipline and no exposed C++ internals.
2. **Given** a handle-taking C-ABI function, **When** a null handle is passed, **Then** the documented "null handle" code is returned and distinguished from the "invalid (destroyed/corrupted) handle" code.
3. **Given** the compiled C-ABI surface, **When** the symbol-visibility gate runs, **Then** only `extern "C"` `fixpp_*` symbols are exported and no C++ symbol leaks.

---

### User Story 4 - Rely on documented per-symbol thread-safety (Priority: P2)

A C consumer needs each public C-ABI symbol to carry an explicit, documented reentrancy guarantee, so they know from which threads each function may be called without external synchronization.

**Why this priority**: Concurrency guarantees are required for correct integration but build on the surface defined by the other stories. They are documentation-and-annotation work over the symbols introduced here.

**Independent Test**: Confirm every public C-ABI symbol introduced by this feature is annotated/documented with exactly one of the three permitted reentrancy classes, and that a gate or review verifies no symbol is left unannotated.

**Acceptance Scenarios**:

1. **Given** the published header, **When** a consumer reads any public symbol's contract, **Then** it states exactly one of: thread-safe / single-thread / requires-session-lock.
2. **Given** the set of public symbols, **When** the annotation is audited, **Then** no public symbol is missing a reentrancy class.

---

### Edge Cases

- **Out-of-range error code** passed to the string lookup → returns the documented "unknown error" description; never dereferences out of bounds, never allocates.
- **Forward-compat downgrade**: a code introduced after the consumer's recorded ABI minor version is mapped to the "unknown" sentinel on the return path; the from-consumer direction stays opaque pass-through (unknown values from older consumers are tolerated, not rejected).
- **Null vs invalid handle**: a null handle and a previously-valid-but-destroyed/corrupted handle are reported with two distinct codes.
- **Numeric-slot drift**: any re-use or re-definition of a published numeric error slot fails the CI occupancy/audit gate.
- **Provisional-code migration**: the currently-shipped provisional decimal error codes are re-numbered to the authoritative master layout; this is a permitted pre-1.0 change and every reference to the old numbers must move in lockstep.
- **Major-version mismatch**: a consumer whose major version differs from the engine's is reported as a hard incompatibility (a reserved dedicated code), distinct from the per-call "unknown error" downgrade.

## Requirements *(mandatory)*

### Functional Requirements

**Handle catalogue (CA-001)**

- **FR-001**: The C ABI MUST expose the opaque handle catalogue defined by the authoritative contract `[2i §4.2]` — `fixpp_engine_t`, `fixpp_session_t`, `fixpp_msg_t`, `fixpp_dict_t`, `fixpp_store_t` — each as an incomplete (forward-declared) type, using the `fixpp_*_t` naming from `[2i]` (the v1-tracker shorthand `FixSession`/`FixMessage`/`FixDictionary` is reconciled to this naming).
- **FR-002**: The C ABI MUST document a **per-handle** ownership / destroy / invalidation discipline per `[2i §4.2.1]` (engine/dict/outbound-msg have idempotent `*_destroy`; session is closed via `fixpp_session_close`; store has no destroy — invalidates on session close).
- **FR-003**: Handle-taking C-ABI functions MUST distinguish a null handle (dedicated "null handle" code) from a previously-valid but destroyed/corrupted handle (dedicated "invalid handle" code), per `[2i §4.2.1]`.
- **FR-004**: No C++ symbol MUST leak through the C ABI; only `extern "C"` `fixpp_*` symbols are exported, verified by the existing symbol-visibility gate (`nm` / `fixpp_capi.map` "fixpp_*; local: *"), per constitution Article X §2.
- **FR-005**: Exceptions MUST NOT cross the `extern "C"` boundary; the C↔C++ thunk discipline (`[2i §4.2.3]`) wraps engine-internal calls and translates outcomes to numeric codes.

**Error surface (CA-002)**

- **FR-006**: The C ABI MUST publish the full bounded `fixpp_error_t` numeric layout exactly as specified in `[2i §4.3]` — an `int32_t`-typed code carried as constants (not a C `enum` declaration), organized into reserved per-domain numeric blocks (cross-cutting [0,99], wire [100,199], dict [200,299], threading [300,399], store [400,499], sync [500,599], TLS [600,699], transport [700,799], decimal [800,899], control-plane [900,999], and the reserved/bindings blocks ≥1000).
- **FR-007**: The C ABI MUST provide a string-lookup function that converts any `fixpp_error_t` to a non-null, statically-stored, human-readable description; it MUST perform zero allocation and the returned pointer MUST NOT be freed by the caller, per `[2i §4.4]`.
- **FR-008**: An out-of-range / undefined error value passed to the string-lookup function MUST return the documented "unknown error" description without crashing or allocating.
- **FR-009**: The forward-compatibility downgrade rule MUST be implemented: an error code introduced after the consumer's recorded ABI minor version is mapped to the "unknown error" sentinel (numeric 2) on the return path; the from-consumer direction remains opaque pass-through, per `[2i §4.4]` / constitution Article X §4. (The point where the consumer's minor version is recorded — `fixpp_engine_create` — is owned by Feature B; see Out of Scope / Assumptions.)
- **FR-010**: A reserved dedicated code MUST exist for explicit major-version mismatch at engine construction, distinct from the per-call "unknown error" downgrade, per `[2i §4.4]` / `[2i §4.5]`.
- **FR-011**: The currently-shipped provisional decimal-boundary error codes MUST be re-numbered to the authoritative master layout (`BUFFER_TOO_SMALL` → 6, `DECIMAL_INVALID` → 800, `DECIMAL_PRECISION_LOSS` → 801; `OK` = 0, `UNKNOWN` = 2 unchanged), and **every** in-repo reference to the old numbers MUST move in lockstep (header, implementation, decimal C tests, contract copies, any binding skeleton). This is a permitted pre-1.0 change (the stability freeze binds only at major version 1).
- **FR-012**: An append-only audit record (`tools/abi_history/error_codes_v1.txt`) MUST be created mapping every published numeric value to its symbolic name and `uint16_t` **introducing_minor** ordinal (the column the FR-009 downgrade consumes; current codes = 2); CI MUST verify no published slot is re-defined relative to it.
- **FR-013**: An occupancy-drift gate (`tools/check_capi_occupancy.sh`) MUST be created and wired into CI (Tier 1) to mechanically verify (A) the header `#define` layout equals the `[2i §4.3]` published values, and (B) the source-domain variant-row counts (`[2X §6.X]` sibling tables) equal the expected coalescing-coverage table — the two quantities are never compared to each other, per `[2i §4.3]` occupancy gate.

**Thread-safety contract (CA-003)**

- **FR-014**: Every public C-ABI symbol introduced by this feature MUST be documented with exactly one reentrancy class — thread-safe, single-thread, or requires-session-lock — per constitution Article X §5 / `[2i §4.10]`. No public symbol may be left unannotated (no undocumented reentrancy). A discrete gate (`tools/check_capi_reentrancy.sh`, separate from the occupancy gate) MUST mechanically verify exactly one reentrancy class per exported symbol's doc-block.

**Version negotiation (CA-004)**

- **FR-015**: The C ABI MUST expose a runtime version accessor returning a plain value-type (major/minor/patch) for the engine's C-ABI version, plus a separate accessor for the library (C++ surface) version, per `[2i §4.5]`.
- **FR-016**: The C ABI MUST publish compile-time version macros (major/minor/patch + a composite macro) that a consumer can compare against the runtime accessor, per `[2i §4.5]`.
- **FR-017**: The version-binding / downgrade decision MUST be implemented as an independently-testable translation that, given an error code and a recorded consumer minor version, applies the FR-009 downgrade rule (so it is unit-testable without the engine handle lifecycle).
- **FR-018**: The published C-ABI version MUST remain pre-1.0 in this feature (additive MINOR bump from the current `0.1.0`); the `0 → 1` major bump (the stability freeze) is deferred to GA after all C-ABI features ship, per `remaining-work/release-engineering.md` Task 2 (B-REL-5, lines 54 & 106). `[2i §4.5]`'s literal `1.0.0` is the documented GA end-state, not a directive to freeze in this feature.

**ABI gates (cross-cutting)**

- **FR-019**: The per-PR ABI-golden gate MUST stay green; when new exported symbols (`fixpp_strerror`, `fixpp_version`, `fixpp_library_version`) and the re-numbered decimal codes land, the golden artifacts and the captured baseline MUST be updated/re-captured in the same change, per `.github/workflows/abi-golden.yml`.

### Key Entities

- **Handle catalogue**: the five opaque engine-domain handle types; each an incomplete type with a per-handle destroy/invalidation discipline per `[2i §4.2.1]`. Concrete create/operate functions are owned by later features.
- **`fixpp_error_t` code space**: an `int32_t` value drawn from reserved per-domain numeric blocks; frozen-meaning once published at major version 1; audited append-only.
- **Version descriptor**: a plain value-type carrying major/minor/patch for the C-ABI surface, with a parallel descriptor for the library surface.
- **Reentrancy class**: one of {thread-safe, single-thread, requires-session-lock} attached to every public symbol.
- **Audit / occupancy tooling**: the append-only error-code history file and the occupancy-drift CI check.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A C consumer can compile against the published header and, with no C++ headers, query both the C-ABI and library versions and decide compatibility — demonstrated by a pure-C smoke program that links only the C ABI.
- **SC-002**: 100% of published `fixpp_error_t` codes return a non-null, allocation-free description; every out-of-range input returns the documented "unknown" description (verified by an enumerating test).
- **SC-003**: The symbol-visibility gate confirms 0 C++ symbols leak through the C ABI surface.
- **SC-004**: The occupancy-drift gate and append-only audit check both pass in CI and fail deterministically when a numeric slot is re-used or re-defined (demonstrated by a negative check).
- **SC-005**: Every public C-ABI symbol introduced carries exactly one of the three reentrancy classes (0 unannotated symbols).
- **SC-006**: The forward-compat downgrade is verified: a code "introduced after" a simulated consumer minor version maps to the unknown sentinel, while earlier codes pass through unchanged.
- **SC-007**: The per-PR ABI-golden gate is green on the feature branch with the updated baseline reflecting exactly the new/changed **exported-symbol set** (the nm gate sees symbols, not `#define` values). The decimal re-numbering is verified separately — by `error_codes_v1.txt` and the enumerating test — since it is invisible to the nm gate (D-4).

## Assumptions

- **Engine-create binding is out of scope (deferred to Feature B).** The forward-compat downgrade (FR-009) needs the consumer's minor version, which the contract records at `fixpp_engine_create` time. That handle-lifecycle entry point is owned by Feature B / `[2j]`. Feature A implements the downgrade as a pure, unit-testable translation (FR-017) and declares the engine-create wiring as a **named limitation** so the feature-completeness audit does not later read CA-004 as falsely-complete.
- **Full master enum is committed now, even for domains whose functions don't exist yet.** `[2i §4.3]` allocates the complete numeric layout it publishes (transport, TLS, store, etc.) up front so the blocks are coherent and the audit baseline is complete. `[2i §4.3]` publishes **no session block** (and no log/otel/app block); per D-8 / L-049-2 `session_*` C++ variants map to `UNKNOWN` in Feature A, and the `FIXPP_ERR_SESSION_*` block is a Feature-B `[2i]` amendment. Codes whose producing functions arrive in later features are published now and covered by the string-lookup table; this is intended forward-compatibility, not premature surface.
- **`fixpp_*_t` naming is authoritative** over the v1-tracker's `FixSession`/`FixMessage`/`FixDictionary` shorthand; the catalogue follows `[2i §4.2]` exactly (including `fixpp_engine_t` and `fixpp_store_t`, which the shorthand omits).
- **The numeric error layout is taken verbatim from `[2i §4.3]`** — it supersedes the loose values in the original feature request (which carried the stale provisional decimal numbers). The decimal frozen PoD boundary type itself (`fixpp_decimal_t`) is unchanged per constitution Article X §3.
- **Version stays 0.x** per the verified release-engineering policy; the GA major bump is a separate Phase-8 / release-gate task.
- This is an **ABI-affecting feature**, so all four mandatory controls apply per constitution Article X §6: `/clarify`, `/analyze`, Codex Gate A, and user `/plan` sign-off.

## Out of Scope

- CA-005..007 — session lifecycle, message send, receive callback (Feature B).
- CA-008..010 — field accessors, field setters, repeating-group accessors (Feature C).
- `fixpp_engine_create` / engine handle lifecycle and the actual storage of the consumer's minor version (Feature B / `[2j]`).
- Python bindings PY-001..005 (blocked until Feature C).
- The `0 → 1` C-ABI major-version freeze (GA / Phase-8 release-engineering Task 2).

## Normative References

*(Article VI §5 — every cite resolves to specific text in the named source.)*

- **C-ABI contract `[2i]`** (`.specify/2i-capi.md`) — the authoritative C-ABI surface this feature `inherits_design` from (NOT reopened; the bundle re-aligns to it): **§4.2** opaque handle catalogue + `fixpp_*_t` naming (FR-001); **§4.2.1** per-handle ownership/destroy/invalidation table + null-vs-invalid contract (FR-002/003); **§4.2.3** the C↔C++ thunk discipline / no-exception-across-`extern "C"` (FR-005); **§4.3** the master `fixpp_error_t` per-domain numeric layout + the two-check occupancy gate definition (FR-006/012/013); **§4.4** `fixpp_strerror` zero-alloc static-table contract + the forward-compat downgrade rule (FR-007/008/009); **§4.5** version surface + macros (FR-010/015/016); **§4.10** the reentrancy taxonomy (FR-014).
- **Constitution Article X** (`.specify/constitution.md`) — the governing ABI-policy article: §X.1 versioned contract + mandatory Codex Gate A; §X.2 no C++ symbol leakage (FR-004); §X.3 frozen decimal boundary PoD (`fixpp_decimal_t` untouched); §X.4 bounded enum + reserved ranges + audit trail + occupancy gate + the downgrade keyed on the consumer's published ABI minor (stability binds only at MAJOR==1) (FR-006/009/011/012); §X.5 per-symbol reentrancy (FR-014); §X.6 the four mandatory ABI controls (`/clarify`, `/analyze`, Codex Gate A, `/plan` sign-off).
- **Constitution Article IX** (`.specify/constitution.md`) — per-PR ≥95% line / ≥85% branch coverage + ASan/UBSan/TSan on the touched modules (the 116-arm `translate()` switch + the `fixpp_strerror` static string literals are the coverage surface).
- **Constitution Article XV §1** (`.specify/constitution.md`) — no hot-path heap allocation; `fixpp_strerror`/`fixpp_version`/`fixpp_library_version` are zero-alloc (FR-007).
- **Constitution Article VI §5** (`.specify/constitution.md`) — reference precision (this section).
- **Architecture §9.2** (`.specify/architecture.md`) — the C-ABI surface version and the C++ library version are decoupled tracks (FR-015; the `fixpp_version()` vs `fixpp_library_version()` split).
- **Sibling error tables** (`[2a §7.4]` decimal, `[2b §6.7]` wire, `[2c §6.7]` dict, `[2e §6.7]` store) — the source-domain variant-row counts the occupancy gate's Check B compares against (FR-013), and the store-handle non-owning-observer ownership row (E-1).
- **`include/fixpp/core/error.hpp`** — the 116-enumerator source the `translate()` coalescing decision audits (data-model E-3); **`include/fixpp/core/version.hpp`** — `fixpp::core::FIXPP_VERSION` for `fixpp_library_version()`.
