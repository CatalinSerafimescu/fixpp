# Feature Specification: Inbound tag-overflow hardening

**Feature Branch**: `040-inbound-tag-overflow-hardening`
**Created**: 2026-06-15
**Status**: Draft
**Input**: User description: "Centralized bounded tag-parse hardening across all live-inbound hand-rolled FIX tag scanners; fix the defective scan_frame_header overflow guard. Split out of 039 after Gate A round 1 found US1 to be a real 5-site security fix."

> **Origin:** Split out of `039-ff-tail-hardening` during its Gate A round 1. A definitive
> hand-rolled-tag-scanner census (in
> `research/reviews/opus_039-ff-tail-hardening_gate_a_adversarial_review.md`) found **five**
> live-inbound hand-rolled tag scanners vulnerable to forged-tag overflow aliasing — including one
> (`scan_frame_header`) that already ships a **defective** guard. This is a real (TLS-auth-bounded)
> security fix, not a LOW hardening item.

## Background — the defect class

FIX field tags are 16-bit (`0 < tag ≤ 0xFFFF`). Several scanners hand-roll tag parsing as
`tag = tag*10 + digit` into a `uint32_t`. Without an in-loop bound, a forged multi-digit tag token
**overflows uint32 and wraps** to a small value, which then **aliases a small, security-relevant
tag** (e.g. `34` MsgSeqNum, `49`/`56` CompID, `52` SendingTime, `1137` DefaultApplVerID). Because the
field is dispatched by the wrapped value, a counterparty can smuggle or shadow a security-relevant
field. The threat is bounded to a **TLS-authenticated, CompID-identity-bound counterparty** (015) —
no anonymous MITM — but a malicious or non-conforming authenticated peer reaches every one of these
scanners directly from inbound bytes.

**The census (5 live-inbound scanners need the guard; 1 is correctly excluded):**

| # | Site | Function | Guard today | Aliasable tags | In scope |
|---|------|----------|-------------|----------------|:--------:|
| 1 | `src/wire/offset_table.cpp:168` | `OffsetTable::build` (Index) | insufficient — post-loop `>0xFFFF` at `:176`, after wrap | Length/Data dispatch | **yes** |
| 2 | `include/fixpp/wire/parser.hpp:340` | `field_iterator::advance` (Scan) | none | any | **yes** |
| 3 | `src/session/admin_messages.cpp:264` | `interpret_logon` | none | 8/35/49/56/108/1137/553/554 | **yes** |
| 4 | `src/session/scan_first_frame_ids.hpp` (extracted from `engine.cpp` anon-ns during impl) | `scan_first_frame_ids` | none | 8/49/56 (acceptor routing) | **yes** |
| 5 | `src/session/scan_frame_header.hpp` (extracted from `session.cpp` anon-ns during impl) | `scan_frame_header` | **DEFECTIVE** — `>429496729U` pre-multiply; admits wrap-and-continue | 34/49/52/56/… | **yes** |
| 6 | `src/session/session.cpp` `build_replay_frame` | `build_replay_frame` | none | 9/10/43/52/122 | NO — stored own-outbound, not inbound |

**Census completeness basis:** the set above is closed by two sweeps — the `tag = tag*10 + digit`
idiom AND a non-idiom numeric-parse sweep (`from_chars`/`strtoul`/`atoi`/`sscanf`/`stoul`/`stoi`),
which found no inbound tag scanner using a non-idiom path (the `from_chars` hits are config-time
dictionary XML + decimal/field value parses, not inbound tag dispatch). See research.md §D-3a;
independently re-verified by the Gate-A Codex + Opus reviews.

The correct reference shape already exists twice in-tree: the framer `BodyLength` guard
(`src/wire/framer.cpp:120`) and the seqnum guard (`src/session/session.cpp:1588`,
`val > 429496729U || (val == 429496729U && digit > 5U)`) — both bound the accumulator *inside* the
digit loop. The `scan_frame_header` tag guard at `:1493` is the broken sibling (it omits the boundary
clause, so the accumulator can reach `429496729` then wrap on the next digit).

## Clarifications

### Session 2026-06-15

- Q: What is the canonical bound and disposition for the centralized guard? → A: Reject any tag whose
  accumulated value would exceed **`0xFFFF`** (the 16-bit FIX tag space), detected **in-loop** before
  any multiply can wrap. The helper returns a bounded result + an `ok`/overflow signal; **each call
  site keeps its existing disposition** (Index `entries_.clear()`; Scan `done_`; session scanners
  `tag_ok=false`/skip-field). Rationale: bounding to `0xFFFF` (not `UINT32_MAX/10`) is both correct
  and simpler — a tag `>0xFFFF` is already invalid by the field's 16-bit width, so there is no
  legitimate tag the tighter bound rejects. (The `scan_frame_header` author's intent was this; the
  bug was using the looser `UINT32_MAX/10` bound *and* getting its boundary wrong.)
- Q: When a session-layer scanner (`scan_frame_header` / `interpret_logon` / `scan_first_frame_ids`)
  hits an overflowing tag, what disposition (forged hostile frame, not accidental malformation)? → A:
  **Keep each site's existing disposition.** The forged field is rejected/skipped per existing
  behavior (`tag_ok=false`/skip-field for the session scanners; `entries_.clear()` Index; `done_`
  Scan) — so the forged field can never be consumed under the aliased tag. Where the skipped field
  was a required header field, the session's existing missing-required-field handling provides
  frame-level rejection. Minimal change, lowest blast radius (Opus census rec). NOT a uniform
  whole-frame reject (that would change all five sites' control flow).
- Q: Does 040 need a live cross-engine (QFcpp/QFJ) interop witness for forged-tag rejection, or do
  unit-level witnesses suffice? → A: **Unit witnesses suffice; live cross-engine witness DEFERRED.**
  Per-scanner unit witnesses (drive wrap-and-continue tokens, assert rejection) carry the proof. A
  live forged-frame witness is deferred to the Item-1 live-golden workstream — the
  038 L-038-2 / L-021-3 / L-037-2 deferral family (reference engines do not emit forged overflow
  tags, so a live witness needs custom hostile-frame injection).

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Fix the defective central-scanner guard via a shared bounded-tag-parse helper (Priority: P1)

A TLS-authenticated counterparty sends an inbound frame whose header carries a forged tag token that
overflows uint32 and wraps to a small value — e.g. `429496729649`, which the central
`scan_frame_header` admits today as tag **49** (SenderCompID) with `tag_ok=true`. After this change,
a single shared bounded-tag-parse helper is introduced and applied to `scan_frame_header`, which now
rejects any token whose accumulated value exceeds `0xFFFF` *before* it can wrap — closing the
aliasing of `34`/`49`/`52`/`56` in the most central live-inbound header scanner (called on every
received frame). The `52`=SendingTime aliasing specifically removes a regression vector against the
038 SendingTime guard.

**Why this priority**: `scan_frame_header` is the single most central live-inbound scanner and the
only one that ships a guard *pretending* to handle this — giving false confidence. Fixing it first,
together with the reusable helper it will share, is the MVP and the highest-risk slice.

**Independent Test**: Drive `scan_frame_header` with the verified wrap-and-continue tokens
(`429496729634`→34, `429496729649`→49) and assert the extracted header does NOT report the aliased
tag (the field is rejected / `tag_ok=false`); confirm the naive token `4294967330` still rejects and
a valid maximal tag `65535` still parses. No other story required.

**Acceptance Scenarios**:

1. **Given** an inbound frame header with token `429496729649` (uint32 wrap → 49), **When**
   `scan_frame_header` parses it, **Then** the field is rejected (not surfaced as SenderCompID(49)).
2. **Given** token `429496729652` (→ 52), **When** parsed, **Then** it is not surfaced as
   SendingTime(52) — no regression vector against the 038 guard.
3. **Given** the naive token `4294967330`, **When** parsed, **Then** it is still rejected (no
   regression of the pre-existing partial guard).
4. **Given** a valid maximal tag `65535` and ordinary tags, **When** parsed, **Then** they are
   surfaced exactly as today (no conforming-path regression).

---

### User Story 2 - Apply the shared helper to the remaining four live-inbound scanners (Priority: P1)

The other four live-inbound hand-rolled scanners (the two wire decode twins, `interpret_logon`, and
`scan_first_frame_ids`) are migrated to the same shared bounded-tag-parse helper, each keeping its
existing disposition. After this change, no forged out-of-range tag can alias a small tag at any
live-inbound scanner.

**Why this priority**: P1 — these four are unguarded today (sites 1–4 in the census); two of them
(`interpret_logon`, `scan_first_frame_ids`) gate Logon CompID/version resolution and acceptor
routing. The security outcome is incomplete until all five live-inbound scanners are covered.

**Independent Test**: For each of the four sites, drive a wrap-and-continue token aliasing to a
security-relevant tag for that site (e.g. `…49` for CompID, `…1137` for DefaultApplVerID in
`interpret_logon`; Index/Scan decode for the wire twins) and assert the forged field is rejected /
not queryable under the aliased tag; conforming tags unchanged.

**Acceptance Scenarios**:

1. **Given** an Index-mode decode of a frame with a wrap-aliased tag, **When** decoded, **Then** the
   message fails closed (`entries_.clear()`) and the forged field is not queryable under the aliased
   tag.
2. **Given** a Scan-mode decode, **When** iterated, **Then** iteration terminates at the forged field
   (`done_`); it is never yielded under the aliased tag.
3. **Given** a Logon frame whose forged token wrap-aliases to `1137` (or `49`/`56`) in
   `interpret_logon`, **When** parsed, **Then** the forged field is rejected, not consumed as
   DefaultApplVerID/CompID.
4. **Given** an acceptor first frame whose forged token wrap-aliases to `49`/`56` in
   `scan_first_frame_ids`, **When** parsed, **Then** the forged field is rejected, not used for
   registry resolution.

---

### User Story 3 - Justify and document the build_replay_frame exclusion (Priority: P3)

`build_replay_frame` (`session.cpp:1639`) also hand-rolls an unguarded tag accumulator, but it parses
**stored own-outbound** frames during resend replay, not received bytes — so it is not a forged-tag
inbound vector (an attacker who can rewrite our own store has already won). After this change, the
exclusion is recorded with a justification (comment + B&L/research note) so a future maintainer does
not rediscover it as a "missed scanner."

**Why this priority**: P3 — documentation of a justified non-inbound exclusion; no behavior change.

**Independent Test**: Confirm a code comment at `build_replay_frame` and a research/B&L note record
the exclusion rationale (stored own-outbound, not live-inbound).

**Acceptance Scenarios**:

1. **Given** the `build_replay_frame` tag accumulator, **When** a maintainer reads it, **Then** a
   comment explains it is exempt from the inbound tag-overflow guard because it parses stored
   own-outbound frames.

---

### Edge Cases

- **Boundary**: accumulated value exactly `0xFFFF` (65535) is valid; `0x10000` (65536) rejects; a
  wrap-and-continue token whose final wrapped value is `≤ 0xFFFF` (e.g. `429496729649`→49) MUST
  reject — the guard fires on the *accumulated value crossing `0xFFFF` mid-scan*, before any wrap.
- **Zero-padded**: `000000000034` (in-range value, many digits) parses as 34 — the guard is on
  accumulated value, not digit count.
- **Per-site disposition**: the helper returns a value + overflow flag; it MUST NOT embed disposition
  (the five sites differ: `entries_.clear()` / `done_` / `tag_ok=false` / goto-skip). Each site keeps
  its current control flow.
- **Non-tag accumulators are out of scope**: value/length/seqnum/checksum accumulators
  (`framer.cpp:123` BodyLength, `:~173` Checksum, `offset_table.cpp:212` Data length,
  `parser.hpp:85` `parse_u32`, `validator.hpp:197` group count, `admin_messages.cpp:303` HeartBtInt,
  `session.cpp:1591` `parse_seqnum`) accumulate field *values*, not tags driving a `switch(tag)`, and
  MUST NOT be changed.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: A single shared bounded-tag-parse helper MUST reject any tag whose accumulated decimal
  value exceeds `0xFFFF`, detecting the overflow **in-loop** (before any multiply that could wrap a
  fixed-width accumulator). It MUST return the bounded tag value plus an overflow/`ok` signal and MUST
  NOT embed call-site disposition.
- **FR-002**: The helper MUST be applied to **all five live-inbound hand-rolled tag scanners**:
  `OffsetTable::build` (Index), `field_iterator::advance` (Scan), `interpret_logon`,
  `scan_first_frame_ids`, and `scan_frame_header`. Each site MUST retain its existing disposition on
  rejection.
- **FR-003**: `scan_frame_header`'s defective `> 429496729U` guard MUST be replaced (it currently
  admits wrap-and-continue aliasing of 34/49/52/56).
- **FR-004**: No forged out-of-range tag token MUST be surfaced/queryable under any aliased small tag
  at any of the five scanners, in particular the verified vectors `429496729634`→34 and
  `429496729649`→49.
- **FR-005**: Conforming tags (including the maximal `65535` and zero-padded forms) MUST parse
  exactly as today at every site (no behavioral regression on conforming input).
- **FR-006**: The helper MUST preserve `noexcept` and the existing per-site hot-path characteristics
  (inlinable; no allocation; no measurable throughput regression).
- **FR-007**: Each of the five sites MUST have a wrap-and-continue negative **unit** witness asserting
  the forged field is rejected; the helper itself MUST have a unit test covering the boundary
  (`65535` ok, `65536` reject, wrap-and-continue reject, zero-padded ok). A **live** cross-engine
  (QFcpp/QFJ) forged-frame witness is DEFERRED to the Item-1 live-golden workstream (038 L-038-2
  family); unit witnesses carry the proof.
- **FR-007a**: Sites 4 (`scan_first_frame_ids`) and 5 (`scan_frame_header`) MUST keep their explicit
  non-digit-class check *before* calling the helper (the helper's `'0'..'9'` precondition), and MUST
  have a **non-digit negative witness** (e.g. a token containing a non-digit) asserting the field is
  rejected — guarding against a future "simplification" that folds the digit check into the helper
  and would otherwise accept/dispatch a non-numeric tag token. (Gate A round 1 P3.)
- **FR-008**: `build_replay_frame` MUST be recorded as a justified out-of-scope exclusion (comment +
  research/B&L note), not silently omitted.
- **FR-009**: No new error codes beyond reusing existing out-of-range/invalid dispositions; no new
  config; no codegen regeneration; no wire-format or C-ABI change.

### Out of Scope

- `build_replay_frame` *behavioral* hardening (it is not a live-inbound vector; FR-008 only documents
  the exclusion).
- Non-tag value/length/seqnum/checksum accumulators (explicitly enumerated in Edge Cases).
- The 039 LOW bundle (US2–US5 there) — independent.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: At all five live-inbound scanners, a forged tag token that overflows uint32 and wraps
  to a small security-relevant tag is rejected in 100% of cases; the forged field is never
  surfaced/queryable under the aliased tag. The verified vectors `429496729634` and `429496729649`
  reject everywhere.
- **SC-002**: The `scan_frame_header` defective guard is fixed — `429496729652` no longer aliases
  SendingTime(52), removing the 038-guard regression vector.
- **SC-003**: All conforming tags (boundary `65535`, zero-padded, existing corpora) parse
  byte-identically to pre-change at every site — zero regressions across wire + session test suites.
- **SC-004**: Exactly one shared bounded-tag-parse helper exists; the five hand-rolled tag loops no
  longer each carry their own (divergent) bound. (Invariant-count style assertion / review.)
- **SC-005**: The helper and all five sites carry wrap-and-continue witnesses; `build_replay_frame`'s
  exclusion is documented.

## Normative References

- 16-bit FIX tag space — `[FIX-SL §4]` field/tag width (the `0 < tag ≤ 0xFFFF` invariant the guard
  enforces).
- `[const §XV.*]` banned-pattern / safety norms (helper stays `noexcept`, no allocation).
- 015 CompID↔TLS-identity binding (the threat-model bound: authenticated counterparty, no anonymous
  MITM).
- 038 acceptor SendingTime(52) guard — the regression vector the `scan_frame_header` 52-aliasing
  defeats (S-019).
- In-tree reference guards: `src/wire/framer.cpp:120` (BodyLength), `src/session/session.cpp:1588`
  (seqnum) — correct in-loop pre-wrap bound shapes.

## Assumptions

- Bounding to `0xFFFF` is correct and loses no legitimate tag (tags `>0xFFFF` are invalid by the
  16-bit field width).
- The five sites' differing control flow (immediate return / `done_` / `tag_ok` flag / goto-skip)
  can each consume a `(value, ok)` return without changing their disposition semantics.
- The threat is a TLS-authenticated, CompID-bound counterparty (015) — MED severity, not anonymous
  MITM; but the `scan_frame_header` defect is a real shipped latent defect worth fixing promptly.
- `build_replay_frame` parses only stored own-outbound frames (not received bytes) — confirmed in the
  census; hence excluded.
