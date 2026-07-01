# Feature Specification: Behavioral Reify / Typed-Read Round-Trip Unblock

**Feature Branch**: `057-behavioral-reify-unblock`
**Created**: 2026-07-01
**Status**: Draft
**Input**: User description: "Behavioral reify / typed-read round-trip unblock (lift R6 / L-003-1). Make dict::reify() and dict::reify_as<Msg>() return LIVE typed owning_<Msg> handles for parsed application and FIXT-admin frames of the three codegen-target versions (v42/v44/v50sp2), instead of the current dict_reify_wire_body_not_ready placeholder."

## Context & Problem Statement

Feature 003 shipped the typed-message machinery — `owning_<Msg>` flyweight classes, compile-time message shape, and the `dict::reify()` / `dict::reify_as<Msg>()` API — but deliberately deferred the **behavioral** half: turning a parsed inbound wire frame into a populated typed owning-handle whose accessors return real field values. That deferral was recorded as limitation **L-003-1** and roadmap item **R6** ("2b reify unblock"). Every application and FIXT-admin arm of `dict::reify()` today returns the placeholder error `dict_reify_wire_body_not_ready`.

The original deferral reason ("the real wire body was not yet available") **no longer holds**: the wire-decode primitives are all live and verified in the current codebase —
- `wire::MessageView::bytes()` returns the full validated frame span,
- `owning_<Msg>::from_view()` deep-copies that span into a caller-`mr` arena,
- `owning_<Msg>::view()` rebuilds a working `MessageView` over the owned bytes via `Framer::feed()`,
- typed accessors decode via `view().get<tag>()`.

The remaining blocker is **build-architecture, not decode logic**: `dict::reify()` lives in shipped `src/dictionary/reify.cpp`, which by rule (arch §2.4 / NFR-003-8) must not `#include` the build-tree-generated `_dispatch/reify_dispatch_{fixt,application}.hpp` headers where the dispatch switches live. The dispatch arms themselves already call `from_view()` — they simply discard the result and return the placeholder.

This feature removes that block. It is a **gating dependency** for the downstream typed-application-message workstream (the A/M/P/N catalogue rows): no per-message inbound read test can pass until `reify()` returns a live typed handle.

## Clarifications

### Session 2026-07-01

- Q: Multi-character MsgType dispatch — in this feature's scope, or a separate follow-up? → A: Fold multi-char dispatch into 057 unconditionally (Option B). This feature adds two-level (first-char + length, or string) MsgType dispatch, replacing the `msg_type.size() > 1` early-return guard, so multi-char MsgTypes (e.g. `AS`, `BE`/`BF`, `BW`/`BX`/`BY`) become reifiable — additionally unblocking the downstream P-003/N-002/N-003 rows. The per-message builders/read-tests for those rows remain downstream (out of scope); only the reify **dispatch mechanism** for multi-char types is in scope here.

## User Scenarios & Testing *(mandatory)*

The "user" is a fixpp library consumer (or a downstream feature author) who has parsed an inbound FIX frame into a `MessageView` and wants a self-owning, typed, readable copy of it.

### User Story 1 - Reify an application message into a live typed handle (Priority: P1)

A consumer has a parsed `MessageView` over an inbound application frame (e.g. a FIX 4.4 message) and calls `dict::reify(view, profile, mr)`. Instead of the `dict_reify_wire_body_not_ready` error, they receive a live `owning_message_handle` that owns a deep copy of the frame and whose resolved-version metadata correctly identifies the message.

**Why this priority**: This is the core of the feature and the single dam blocking all 30 downstream typed-message rows. Delivering just this makes the reify round-trip real for the largest message family (application messages across v42/v44/v50sp2).

**Independent Test**: Parse a real application frame, call `dict::reify()`, assert `has_value()` is true, assert the returned handle's resolved version metadata matches the input (kind = application, application version = the resolved version), and assert a typed field read returns the exact wire value. Mutation-test the dispatch return so flipping it back to the placeholder makes the test go RED.

**Acceptance Scenarios**:

1. **Given** a parsed FIX 4.4 application frame with known field values, **When** `dict::reify(view, profile_v44, mr)` is called, **Then** it returns a populated `owning_message_handle` (not `dict_reify_wire_body_not_ready`), whose resolved-version metadata reports `application` kind and the v44 application version.
2. **Given** the reified handle from scenario 1, **When** a typed accessor for a populated field is read, **Then** it returns exactly the value carried on the original wire frame (byte-faithful round-trip).
3. **Given** the same frame reified with a v42 and a v50sp2 profile respectively, **When** each is reified, **Then** each returns a handle whose resolved application version matches the requested profile's version.
4. **Given** an application frame carrying an explicit `ApplVerID(1128)`, **When** reified against a FIXT profile with an `Unknown` default application version, **Then** the resolved version is derived from `ApplVerID(1128)` (not the profile default).
5. **Given** a parsed frame with a **multi-character** MsgType that has a generated arm (e.g. `AS` AllocationReport in v44), **When** `dict::reify()` is called, **Then** it returns a populated handle (not `dict_reify_unknown_msg_type`) and a typed accessor returns the exact wire value — i.e. the `msg_type.size() > 1` early-return guard no longer blocks generated multi-char types.

---

### User Story 2 - Reify a FIXT-admin message into a live typed handle (Priority: P2)

A consumer parses an inbound FIXT.1.1 administrative frame (e.g. Logon/Heartbeat/TestRequest on the FIXT transport version) and calls `dict::reify()`. They receive a live typed handle resolved as a session-admin message, rather than the placeholder error.

**Why this priority**: The FIXT-admin path shares the same dispatch-bridge mechanism as the application path and is exercised on every live session, but it delivers value only after the P1 application path proves the mechanism. It is a second, structurally-identical consumer of the same bridge.

**Independent Test**: Parse a FIXT-admin frame, call `dict::reify()`, assert `has_value()`, assert the resolved-version metadata reports `session_admin` kind, and read a header field exactly.

**Acceptance Scenarios**:

1. **Given** a parsed FIXT-admin frame (single-char admin MsgType), **When** `dict::reify(view, fixt_profile, mr)` is called, **Then** it returns a populated handle whose resolved-version metadata reports `session_admin` kind.
2. **Given** the reified FIXT-admin handle, **When** a header field (e.g. sender/target CompID) is read, **Then** it returns the exact wire value.

---

### User Story 3 - Compile-time typed reify via `reify_as<Msg>` (Priority: P3)

A consumer who knows the message type at compile time calls `dict::reify_as<SomeMessage>(view, mr)` and receives a populated `owning_<SomeMessage>` (the concrete typed owner), rather than the placeholder error.

**Why this priority**: `reify_as<Msg>` is the ergonomic typed entry point; it is lower priority than the runtime-dispatched `reify()` because a caller who already knows `Msg` can construct via `owning_<Msg>::from_view()`, but the public API contract must return a live owner rather than the placeholder for consistency.

**Independent Test**: Call `dict::reify_as<Msg>()` on a matching parsed frame, assert `has_value()`, and read an exact field value.

**Acceptance Scenarios**:

1. **Given** a parsed frame whose MsgType matches `Msg`, **When** `dict::reify_as<Msg>(view, mr)` is called, **Then** it returns a populated `owning_<Msg>` and a typed accessor returns the exact wire value.

---

### Edge Cases

- **Unknown / unsupported MsgType**: reifying a frame whose MsgType has no arm in the target version returns `dict_reify_unknown_msg_type` (unchanged from today's default arm) — NOT a live handle and NOT `dict_reify_wire_body_not_ready`.
- **Multi-character MsgType** (e.g. `AS`, `BE`): the `msg_type.size() > 1` early-return guard is **replaced** by two-level dispatch. A multi-char type with a generated arm reifies into a populated handle; a multi-char type with **no** generated arm returns `dict_reify_unknown_msg_type` (same fall-through as an unknown single-char type).
- **Unresolvable application version**: a FIXT frame that cannot resolve its application version (no `ApplVerID`, `Unknown` default) returns the existing distinct error (`dict_unresolved_application_version` / `dict_unknown_appl_ver_id`), not a live handle — preserving B-003-5.
- **Allocation failure during deep copy**: `from_view()` OOM surfaces as `dict_reify_oom`, propagated unchanged through the dispatch arm.
- **Runtime-XML-only version** (a version with no codegen namespace): reify returns `dict_reify_unknown_msg_type` (the outer-switch default), preserving L-003-2.
- **Buffer reuse after reify**: because the handle owns a deep copy, the returned handle remains valid after the source buffer is reused (this is the reason reify deep-copies rather than aliasing).

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: `dict::reify()` MUST return a populated `owning_message_handle` for a parsed **application** frame of a codegen-target version (v42, v44, v50sp2) whose MsgType has a generated arm, instead of `dict_reify_wire_body_not_ready`.
- **FR-002**: `dict::reify()` MUST return a populated handle for a parsed **FIXT-admin** frame (single-char admin MsgType), instead of `dict_reify_wire_body_not_ready`.
- **FR-003**: The returned handle MUST carry correct resolved-version metadata — the message kind (`application` vs `session_admin`) and the resolved application version — consistent with the input profile and any `ApplVerID(1128)` present on the frame.
- **FR-004**: A typed accessor read on a reified handle MUST return exactly the value carried on the original wire frame for every populated field exercised by the tests (byte-faithful round-trip).
- **FR-005**: The reified handle MUST own its data (a deep copy of the frame) such that it remains valid and readable after the source parse buffer is reused or destroyed.
- **FR-006**: `dict::reify_as<Msg>()` MUST return a populated `owning_<Msg>` for a parsed frame whose MsgType matches `Msg`, instead of the placeholder error.
- **FR-007**: The build MUST provide a mechanism by which shipped `dict::reify()` (in `src/`) reaches the generated dispatch switches **without** shipped source `#include`-ing build-tree-generated headers (preserving arch §2.4 / NFR-003-8). The generated-header inclusion MUST be confined to a generated-aware translation unit.
- **FR-008**: The codegen emitter MUST emit dispatch arms that return a populated typed owner on `from_view()` success and propagate `from_view()` errors, rather than emitting the `dict_reify_wire_body_not_ready` placeholder. Regenerated output MUST be byte-deterministic run-to-run (preserving B-003-3).
- **FR-009**: Error behavior for non-reifiable inputs MUST be preserved exactly: unknown MsgType (single- or multi-char, with no generated arm) → `dict_reify_unknown_msg_type`; unresolved application version → `dict_unresolved_application_version` / `dict_unknown_appl_ver_id`; allocation failure → `dict_reify_oom`.
- **FR-014**: `dict::reify()` MUST dispatch **multi-character** MsgTypes via a two-level (first-char + length, or equivalent string) dispatch, replacing the `msg_type.size() > 1` early-return guard, so that a parsed frame whose multi-char MsgType has a generated arm returns a populated handle. Multi-char types with no generated arm fall through to `dict_reify_unknown_msg_type` (FR-009). This unblocks the downstream P-003/N-002/N-003 rows at the dispatch layer; their per-message builders and read-test coverage remain out of scope (FR-013).
- **FR-010**: The previously-deferred tests guarded by `FIXPP_R6_WIRE_BODY_READY` in the reify-dispatch test suite MUST be activated to run unconditionally, and MUST be supplemented with discriminating per-field read witnesses (exact field-value assertions), mutation-tested so that reverting the dispatch arm to the placeholder makes them RED.
- **FR-011**: Limitation **L-003-1** MUST be flipped from `deferred` to shipped in `spec/behaviors-and-limitations.md`, and the 003 roadmap reference (spec §11 R6) updated to reflect the unblock. The catalogue status for the affected 003 row MUST be updated accordingly.
- **FR-012**: This feature MUST NOT add any new wire format, error code, public message-builder surface, C-ABI symbol, or dependency. (It wires existing generated code and existing decode primitives; the only new artifacts are an internal dispatch-bridge translation unit / build target and its non-shipped bridge header.)
- **FR-013**: This feature MUST NOT expand into the downstream per-message scope: it delivers the reify **mechanism** validated over a representative set of messages, not per-message typed builders (FR-015a) nor full per-message read-test coverage of every A/M/P/N row nor all-version coverage (FR-015b).

### Key Entities

- **`dict::reify()`**: Runtime entry point taking a parsed `MessageView`, a version profile, and a memory resource; returns an `owning_message_handle` or a reify-domain error. The subject of FR-001/002/003.
- **`dict::reify_as<Msg>()`**: Compile-time-typed entry point returning a concrete `owning_<Msg>`. The subject of FR-006.
- **`owning_message_handle`**: The runtime-polymorphic owning handle returned by `reify()`, carrying resolved-version metadata and an owned deep copy of the frame.
- **`owning_<Msg>` flyweight**: Per-message generated owner holding `bytes_` (pmr deep copy) and a lazily-rebuilt `view_cache_`; exposes typed field accessors. Already implemented; consumed as-is.
- **Dispatch bridge (new, internal)**: A generated-aware translation unit (and its non-shipped bridge header) that includes the `_dispatch/reify_dispatch_{fixt,application}.hpp` headers and exposes an entry `dict::reify()` can call from shipped source. The subject of FR-007.
- **Resolved-version metadata**: The `{kind, session version, application version}` tuple attached to a reified handle. The subject of FR-003.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A consumer can reify a parsed application frame into a typed handle and read at least one exact field value back — a round-trip that returns `dict_reify_wire_body_not_ready` on `main` and returns a live value after this feature.
- **SC-002**: The reify round-trip works for all three codegen-target versions (v42, v44, v50sp2), for the FIXT-admin path, and for at least one **multi-character** MsgType — the distinct dispatch paths (single-char per version, FIXT-admin, and multi-char) all proven live by tests.
- **SC-003**: 100% of the `FIXPP_R6_WIRE_BODY_READY`-guarded deferred tests run unconditionally and pass, and each new per-field read witness is mutation-discriminating (reverting the dispatch arm to the placeholder turns it RED).
- **SC-004**: All preserved error paths (unknown MsgType, unresolved application version, OOM, multi-char guard) return their exact pre-existing error codes — 0 regressions in the reify error contract.
- **SC-005**: Regenerated codegen output is byte-identical run-to-run, and no shipped source file `#include`s a build-tree-generated header (arch §2.4 / NFR-003-8 preserved) — verifiable by inspection/gate.
- **SC-006**: Zero new wire/error/public-builder/C-ABI/dependency surface is introduced (FR-012), confirmed by diff review.

## Assumptions

- **The decode primitives are complete and correct.** `MessageView::bytes()`, `owning_<Msg>::from_view()`, `owning_<Msg>::view()` (Framer rebuild), and `view().get<tag>()` are live and verified on `main`; this feature wires them end-to-end and does not reimplement decoding. (Verified against current headers during scoping.)
- **The 2b-unblock design is the pre-blessed dispatch-bridge approach** documented in the `reify.cpp` R6 comments: a `fixpp::dict::dispatch` (or equivalently-named) CMake target — a generated-aware TU that includes the `_dispatch/` headers — plus a non-shipped bridge header to which `dict::reify()` delegates. The exact target/header naming and delegation seam are a `/speckit-plan` decision.
- **`reify_as<Msg>` may not require the runtime dispatch bridge** — because `Msg` is known at compile time, it can delegate directly to `owning_<Msg>::from_view()`. Whether it routes through the bridge or directly is a `/speckit-plan` implementation choice; the observable contract (FR-006) is fixed.
- **Multi-character MsgType dispatch is IN scope** (Clarification 2026-07-01, Option B). The `msg_type.size() > 1` early-return guard is replaced by two-level dispatch (FR-014), so multi-char generated types reify. The exact dispatch shape (first-char + length switch vs string switch/hash) is a `/speckit-plan` implementation choice; the observable contract (FR-014) is fixed. This unblocks P-003/N-002/N-003 at the dispatch layer only — their builders/read-tests stay downstream.
- **Scope is the reify mechanism, not per-message coverage.** A representative set of messages (at least one application MsgType per codegen version + one FIXT-admin type) proves the mechanism; exhaustive per-message read tests and typed builders remain downstream (the A/M/P/N rows), explicitly out of scope here.
- **No behavioral change to non-reify code paths.** Session inbound processing, validation gates, and the wire codec are untouched; only the reify dispatch wiring and its tests change (plus the codegen emitter and regenerated output).
