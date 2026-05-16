# Feature Specification: Wire Codec — Framer, Parser, Offset Table, Writer, Validator

**Feature Branch**: `004-wire-codec`
**Created**: 2026-05-16
**Status**: Draft
**Input**: User description: "Wire layer: framer, zero-copy parser, offset table, message writer/serializer, and validator (W-001..W-014) per signed-off design doc `.specify/2b-wire.md` (Draft v0.2, Gate A round 1 converged). Wire owns how bytes go on/off the wire and produces `fixpp::wire::MessageView`; typed message classes are owned by 2c, field-type representation by 2a/2c. Reify bridge is NOT in scope. Unblocks 2b-gated deferred work in 001-core-decimal and 003-dictionary-codegen. Constraints: zero `new`/`delete` on the hot path [const §VIII.5]; wire invisible to C ABI [const §X.2]; Validator runtime-virtual plugin ≤5 pure-virtual [const §XIV.2]; hybrid eager/lazy offset-table spike [arch §11 row 1]."

> **Authority anchor:** This spec is anchored to `.specify/2b-wire.md` **Draft v0.2 — Gate A round 1 converged**. Where this spec and the design doc disagree, the design doc wins; an inconsistency is a defect in this spec, not a design change. Catalogue rows owned (in part): **W-001..W-014**, **OSS-006**, **OSS-008**, **OSS-013**, plus the parse/serialize/validate behaviour of every generated typed message under Appendix A. This is the third Phase-4 feature of the `wire/` module and the critical-path unblocker for the 2b-gated deferred work in `001-core-decimal` (wire FLOAT accessor) and `003-dictionary-codegen` (behavioural typed-read / `dict::reify` round-trip, tracked R6).

## User Scenarios & Testing *(mandatory)*

The "users" of this feature are the downstream library layers that compile against the wire surface (codegen-generated typed messages from 2c, the session FSM, MessageStore from 2e, the C-ABI accessors from 2i, the session tap from 2l) and, transitively, the application developer building a FIX engine on top of fixpp.

### User Story 1 - Parse an inbound frame into a zero-copy, indexed view (Priority: P1)

A consumer holds a contiguous byte buffer containing one complete, structurally-valid FIX message and needs random-access, O(1)-by-tag field lookup without copying the bytes or allocating, so that codegen-generated typed accessors and `dict::reify` can read fields directly out of the buffer.

**Why this priority**: This is the single critical-path capability. Until a real `MessageView` + `OffsetTable` exists, the typed-read and `dict::reify` round-trip deferred from 003 (tracked R6) and the wire FLOAT accessor deferred from 001 cannot be exercised end-to-end. Every other wire capability is built on the parse + index path.

**Independent Test**: Feed a known-good FIX 4.4 / 5.0SP2 message buffer to the parser; assert each tag resolves to the correct `(offset, length)` byte range, repeated tags and repeating-group occurrences are addressable, and no heap allocation occurs between parse and the simulated `fromApp` return.

**Acceptance Scenarios**:

1. **Given** a buffer with one well-formed FIX message, **When** it is parsed, **Then** every standard header, body, and trailer field is retrievable by tag as a byte view aliasing the original buffer with zero copies and zero heap allocation.
2. **Given** a parsed message, **When** a tag that occurs multiple times (e.g. inside a repeating group) is looked up, **Then** each occurrence is independently addressable in document order.
3. **Given** a parsed message accessed through the typed dictionary surface (random-access intent), **When** the offset table is built, **Then** it is built eagerly; **and given** access through the raw iterator path (streaming intent), **Then** the offset table is built lazily — with the eager/lazy choice resolved at compile time, no runtime branch on the hot path.
4. **Given** a debug build, **When** a view is accessed after its originating buffer has been reused, **Then** the access traps deterministically rather than reading freed/rotated memory.

---

### User Story 2 - Serialize a message to wire bytes with automatic framing fields (Priority: P2)

A consumer composes a message field-by-field into a caller-supplied buffer and needs the framing fields — `BodyLength(9)` and `CheckSum(10)` — and mandatory header/trailer ordering computed and written automatically at commit, so generated typed messages and session admin messages produce spec-conformant bytes.

**Why this priority**: Required for any outbound traffic and for round-trip testing (parse → serialize → byte-identical), but the inbound parse path (P1) is the gating unblocker, so this is P2.

**Independent Test**: Build a message via the writer, commit it, and assert the emitted bytes are byte-identical to a known-good golden frame including a correctly computed `BodyLength` and 3-digit zero-padded `CheckSum`.

**Acceptance Scenarios**:

1. **Given** a sequence of fields written in required order, **When** the message is committed, **Then** `BodyLength(9)` equals the byte count from the field after `BodyLength` through the byte before `CheckSum`, and `CheckSum(10)` equals the unsigned sum of all preceding bytes modulo 256, formatted as exactly 3 zero-padded ASCII digits.
2. **Given** a typed message parsed from the wire, **When** it is re-serialized, **Then** the output bytes are identical to the input frame (round-trip fidelity), including opaque round-trip preservation of unknown/custom fields.
3. **Given** a caller buffer too small for the message, **When** commit is attempted, **Then** a defined wire error is returned and no out-of-bounds write occurs.

---

### User Story 3 - Frame a multi-message TCP byte stream (Priority: P2)

A transport consumer feeds arbitrary chunks of received bytes (which may split or coalesce messages) and needs zero or more complete, individually verified frames back, with partial trailing bytes carried over to the next feed.

**Why this priority**: Necessary for realistic ingestion from a TCP stream and pipelined framing (W-010), but a fixed single-message buffer is sufficient to exercise P1, so framing is P2.

**Independent Test**: Feed a byte stream split at adversarial boundaries (mid-tag, mid-`BodyLength`, between two messages); assert exactly the complete frames are emitted in order, partials are carried over, and each emitted frame has had `BodyLength` and `CheckSum` verified before it is exposed.

**Acceptance Scenarios**:

1. **Given** a feed containing two complete messages plus a partial third, **When** it is processed, **Then** exactly two frames are returned in order and the partial third is carried into the next feed and completed there.
2. **Given** an incoming frame whose declared `BodyLength` or `CheckSum` does not verify, **When** framing runs, **Then** the frame is rejected with a defined wire error before any parser sees it.
3. **Given** an incoming frame larger than the configured maximum frame size, **When** framing runs, **Then** it is rejected with `wire_frame_too_large` and the stream does not deadlock.

---

### User Story 4 - Validate a parsed message against dictionary metadata (Priority: P3)

A consumer needs message-level structural and field-level type/required/enum checks driven by the dictionary metadata table for a given FIX version, delivered through a runtime-virtual plugin so alternative validation policies can be substituted.

**Why this priority**: Structural parse correctness (P1) is independently valuable and testable without semantic validation; validation hardens the surface against malformed/hostile input and is required before production but after the core read path.

**Independent Test**: Run the default validator over messages with (a) a missing required field, (b) a field whose value violates its declared type, (c) an out-of-range enum value, and (d) a malformed repeating-group count; assert each is reported with the correct wire/validator error and conforming messages pass.

**Acceptance Scenarios**:

1. **Given** a parsed message missing a required field for its type/version, **When** validated, **Then** a defined validator error identifying the missing tag is returned.
2. **Given** a field whose bytes do not conform to its declared data type, or an enum field with a value outside its allowed set, **When** validated, **Then** a defined validator error is returned.
3. **Given** a repeating group whose `NoXxx` count does not match the number of group instances, or whose first field is not the required delimiter, **When** validated, **Then** a defined validator error is returned.
4. **Given** the `Validator` interface, **When** its pure-virtual surface is inspected, **Then** it has at most 5 pure-virtual methods and holds dictionary metadata by value (no virtual `wire/`→`dict/` runtime edge).

---

### Edge Cases

- **Partial reads at every boundary**: bytes split mid-tag, mid-`=`, mid-value, mid-SOH, mid-`BodyLength`, between messages, and a single byte at a time — all must reassemble correctly.
- **Nested repeating groups**: groups within groups (W-007) index and round-trip correctly; the per-group-instance entry cap (`default_max_group_entries_per_instance`, 4096) is enforced per instance, not aggregate.
- **`Length`+`Data` fields** (W-008): the raw `Data` field is read using the preceding `Length` field and may legally contain `=` and SOH bytes; it must not be misparsed as field delimiters.
- **Hostile peer / DoS bounds**: oversized frame (> 256 KiB default), offset table over `default_max_offset_entries` (4096 occurrences) → `wire_offset_table_full`, tag outside `uint16_t` range → `wire_tag_out_of_range`, group instance over cap → `wire_group_too_large` — each returns a defined error and bounded memory, never unbounded allocation or crash.
- **Custom / unknown fields**: tags absent from the dictionary are preserved opaquely for byte-exact round-trip and exposed via a filtered `unknown_fields()` view with no vector materialization.
- **Corruption signals**: a wrong `CheckSum` or inconsistent `BodyLength` is always rejected (no bypass mode).
- **Empty / zero-length values**, missing trailer, fields after `CheckSum(10)`, duplicated standard header fields, and out-of-order mandatory header fields.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The system MUST parse Tag=Value SOH-delimited FIX messages (W-001) into a non-owning, zero-copy view whose lifetime is the caller-owned buffer's, exposing standard header (W-002) and trailer (W-003) fields.
- **FR-002**: The system MUST provide an offset-table index giving O(1)-by-tag field lookup (W-012), with each repeated-tag / repeating-group occurrence independently addressable in document order.
- **FR-003**: The offset table MUST be built eagerly on the random-access (typed-dictionary) path and lazily on the raw-iterator (streaming) path, with the choice resolved at compile time and no runtime branch on the hot path (`[SYN §3.1 Q1]`, `[arch §11 row 1]`).
- **FR-004**: The system MUST decode/encode repeating groups including nested groups (W-006, W-007), exposing a typed `group_view<T>` by default and an `.iter()` opt-out that skips group-index construction for streaming callers (`[SYN §3.1 Q3]`).
- **FR-005**: The system MUST handle `Length`+`Data` raw-byte field pairs (W-008) such that `Data` content (which may contain `=`/SOH) is read by length and never misinterpreted as delimiters.
- **FR-006**: The system MUST support, at the byte boundary, all FIX field data types (W-009) by delegating decode/encode to the field-representation traits owned by 2a (`decimal<T>`) and 2c (`dict::field_traits<...>`); the wire layer MUST NOT re-implement field decoding.
- **FR-007**: The system MUST serialize a message into a caller-supplied buffer (W-013) writing fields in required order and computing `BodyLength(9)` (W-004) and `CheckSum(10)` (W-005, unsigned byte-sum mod 256, 3-digit zero-padded ASCII) automatically at commit.
- **FR-008**: A parse-then-serialize round trip MUST produce byte-identical output for every generated typed message across the supported versions (`v42`, `v44`, `v50sp2`, `vt11`), including opaque preservation of unknown/custom fields (`[SYN §3.1 Q4]`).
- **FR-009**: The system MUST frame a multi-message TCP byte stream (W-010, OSS-013) emitting zero or more complete frames per feed, carrying partial trailing bytes over to the next feed, and verifying `BodyLength` and `CheckSum` before any parser is exposed to a frame.
- **FR-010**: The system MUST provide a message `Validator` (W-014) checking required-field presence, field type conformance, enum value membership, and repeating-group structure, driven by dictionary metadata for the message's FIX version.
- **FR-011**: The `Validator` MUST be a runtime-virtual plugin with at most 5 pure-virtual methods (`[const §XIV.2]`) and MUST hold dictionary metadata by value, introducing no virtual `wire/`→`dict/` runtime dependency edge.
- **FR-012**: The system MUST NOT perform any heap allocation (`new`/`delete`) between the start of parse and the return of the simulated `fromApp` (`[const §VIII.5]`); allocation-bearing trait specializations MUST use the per-message arena supplied by the wire layer.
- **FR-013**: The five wire primitives (`Framer`, `Parser`, `OffsetTable`, `Writer`, `Validator`) and the shared `View` flyweight base MUST be `noexcept` end-to-end across the parse→`fromApp` window; throwing third-party trait wrappers MUST trap rather than propagate (`[arch §5.3]`).
- **FR-014**: All wire failures (parse, framer, validator) MUST be reported through `expected_t<T>` and the `fixpp::core::error` enum (`[arch §5.3]`); the wire layer MUST expose no C++ types through the C ABI (`[const §X.2]`).
- **FR-015**: The system MUST enforce caller-tunable DoS bounds — maximum frame size (default 256 KiB → `wire_frame_too_large`), maximum offset-table occurrences (default 4096 → `wire_offset_table_full`), maximum group entries per instance (default 4096 → `wire_group_too_large`), and tag numeric range `uint16_t` 0..65535 (→ `wire_tag_out_of_range`) — with bounded memory and no crash when exceeded.
- **FR-016**: The system MUST enforce the flyweight lifetime contract: `[[clang::lifetimebound]]` markers on view-producing surfaces (best-effort on Clang/GCC, accepted gap on MSVC per `[const §IX.4]`) and a debug-build generation-counter trap on use-after-buffer-reuse, with the counter compiled out in release builds.
- **FR-017**: `CheckSum` verification MUST be mandatory with no production bypass switch (a tests-only hook is permitted).
- **FR-018**: The feature MUST unblock the deferred behavioural work it gates: the `003-dictionary-codegen` typed-read / `dict::reify` round-trip (R6) and the `001-core-decimal` wire FLOAT accessor become exercisable end-to-end against the real `MessageView`.

### Key Entities

- **Frame**: a complete byte range for exactly one FIX message, produced by the `Framer` after `BodyLength`/`CheckSum` verification; aliases a caller-owned buffer.
- **View (flyweight base)**: zero-copy, non-owning; lifetime is the underlying buffer's; carries a debug-only generation token to detect use-after-reuse.
- **MessageView**: the parsed message — a `View` plus structural access to header, body, trailer, and an associated `OffsetTable`.
- **OffsetTable entry**: a `(tag, offset, length, group sub-index)` record indexing one field *occurrence* (not one distinct tag); fixed small size, count bounded by `default_max_offset_entries`.
- **FieldView**: a single tag's byte range within a parsed message, decoded via 2a/2c traits at the boundary.
- **GroupView**: a typed view over a repeating-group instance (and its nested groups), with an iterator opt-out.
- **Unknown-fields view**: a filtered, non-materialized view over offset-table entries whose tags are absent from the dictionary, preserved opaquely for round trip.
- **Writer buffer**: a caller-supplied output buffer the `Writer` serializes into, finalized with computed `BodyLength`/`CheckSum` at commit.
- **Framer carry buffer**: caller-managed storage holding partial trailing bytes between feeds.
- **Validator**: a runtime-virtual plugin (≤5 pure-virtual) holding dictionary metadata by value, producing structural/field validation results.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: 100% of a representative corpus of well-formed FIX messages across versions `v42`, `v44`, `v50sp2`, `vt11` parse with every field resolvable by tag and survive a parse→serialize round trip byte-identical (including unknown/custom fields).
- **SC-002**: Zero heap allocations occur between the start of parse and the return of `fromApp`, verified by an allocation-counting harness on the full parse + serialize path (no allocation regressions tolerated).
- **SC-003**: 100% of a malformed/adversarial input corpus (bad `CheckSum`, inconsistent `BodyLength`, oversized frame, offset-table overflow, out-of-range tag, oversized group, truncation at every byte boundary) is rejected with the defined wire error and bounded memory — zero crashes, zero unbounded allocation, zero out-of-bounds access (clean under sanitizers and fuzzing).
- **SC-004**: A byte stream arbitrarily fragmented (down to one byte per feed) at all message and field boundaries reassembles into exactly the original sequence of frames in order, with no lost or duplicated frames.
- **SC-005**: The validator correctly classifies 100% of a labelled corpus of conforming vs. non-conforming messages (missing required field, type violation, enum violation, malformed repeating group) with no false accept of a non-conforming message.
- **SC-006**: The previously 2b-gated tests ship green: the `003-dictionary-codegen` typed-read / `dict::reify` round-trip (R6) and the `001-core-decimal` wire FLOAT accessor execute end-to-end against the real `MessageView` (no remaining vendored frozen stub on those paths).
- **SC-007**: The `Validator` plugin interface exposes ≤5 pure-virtual methods and the module dependency graph contains no `wire/`→`dict/` virtual runtime edge (enforced by the layering check).
- **SC-008**: The eager-vs-lazy offset-table measurement spike (`[arch §11 row 1]`, design §10 Q1) is executed on the named venue/message corpora in occurrence space and its result recorded, confirming the hybrid disposition.

## Assumptions

- The spec is anchored to `.specify/2b-wire.md` Draft v0.2 (Gate A round 1 converged); all design decisions locked there (flyweight + caller-owned buffer, hybrid offset table, typed `group_view<T>`, opaque custom-field round trip, mandatory CheckSum, the magnitude/DoS caps) are treated as decided and are not re-opened by this spec.
- v1.0 wire bytes are exclusively Tag=Value SOH (`[FIX50SP2 §3]`). FIXP / SOFH / SBE binary encodings (W-015, W-016) are out of scope (post-v1, P3).
- Session-FSM semantics (sequence numbers, gap fill, ResendRequest/TestRequest, PossDup) are out of scope — owned by the session-module Phase-4 spec; wire reports a parsed, structurally-valid frame upward and does not interpret session or application semantics.
- The `dict::reify` bridge itself is out of scope here — it is owned on the dictionary side (003) and merely consumes the `MessageView`/`OffsetTable` this feature provides; this feature's obligation is to make that consumer's deferred round-trip exercisable.
- Field-representation types are reused, not built here: `decimal<T>` from 2a (merged, PR #62) and `dict::field_traits<...>` from 2c/003 (merged, PR #67); the wire layer calls their `from_chars`/`to_chars` at the byte boundary.
- The per-message arena (`SessionConfig::message_arena`, `[arch §5.2]`) is provided by the caller/config and reset after `fromApp`; the wire layer always passes it to allocation-bearing traits.
- MSVC lifetime-annotation enforcement is a known, accepted gap (`[const §IX.4]`); Clang is the primary enforcement target, GCC best-effort.
- The DoS caps are caller-tunable bounds for memory/DoS protection, not FIX-spec invariants; defaults target FX/equities, and derivatives/options venues are expected to raise them per the design doc guidance.
- Build/test/toolchain conventions follow the established pattern of the merged `001`/`002`/`003` features (same project structure, sanitizer/fuzz/bench/coverage gates); specifics are deferred to `/speckit-plan`.
