# Feature Specification: Typed Application Messages (builders + read/round-trip witnesses)

**Feature Branch**: `061-typed-app-messages`
**Created**: 2026-07-05
**Status**: Draft
**Input**: User description: typed-message slice of the §I.3 "build all 59 OFFICIAL v1.0 rows" decision — hand-written typed builders + read/round-trip witnesses for the 28 order-management / market-data / allocation catalogue rows (A-001..A-013, M-001..M-012, P-001..P-003).

**Sources**: `research/G19-fix-fpml-iso20022/remaining-work/typed-messages.md`; `spec/feature-catalogue.md` lines 134–204; `.specify/constitution.md` §XVIII.7 + §I.3. Prerequisite feature 057 (PR #161) lifted the R6/2b reify dam and multi-char MsgType dispatch; all target typed flyweights are already generated under `fixpp::v42/v44/v50sp2`.

## Clarifications

### Session 2026-07-05

- Q: Version-namespace coverage per row for v1.0? → A: One representative applicable namespace per row (FIX44 for order-management/allocation, FIX42 for market-data, per catalogue authority); FR-015b all-version coverage stays deferred post-v1.0.
- Q: What closes a covered row (definition of done)? → A: All three artifacts — hand-written builder + independent inbound read witness (independently hand-authored wire, not builder output) + round-trip witness. Read and round-trip are complementary; both required.
- Q: MsgType-pair rows (e.g. M-005 c/d) and inherently-inbound/response messages — scope? → A: Every distinct message is fully treated — both halves of a MsgType-pair row (request AND response) and inherently-inbound/response messages each get builder + read + round-trip (fixpp is a full initiator+acceptor engine, so response-message builders are real send-path code). ~34 distinct messages across the 28 rows.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Parse an inbound typed application message (Priority: P1)

An application developer using fixpp receives an inbound FIX application message (e.g. an ExecutionReport, a MarketDataSnapshotFullRefresh, an AllocationInstruction) and needs to read its business fields with exact, correctly-typed values — strings, chars, integers, and decimals — and to be told (fail-closed) when a required field is missing or malformed, rather than silently reading garbage.

**Why this priority**: Reading inbound app messages is the primary reason a downstream client integrates a FIX engine; the typed read path is the shipped v1.0 surface for the 28 rows. Without it these catalogue rows cannot be marked done.

**Independent Test**: For any covered row, hand-author a wire body, frame it through the production framer, construct the version-namespaced typed flyweight over the parsed view, and assert each accessor returns the exact expected value; assert a missing required field yields the correct typed error. Delivers value with zero dependence on the builder.

**Acceptance Scenarios**:

1. **Given** a well-formed wire frame for a covered MsgType, **When** it is parsed and wrapped in the typed flyweight, **Then** every business-field accessor returns the exact value present on the wire (discriminating witness — not merely "parse succeeded").
2. **Given** a frame missing a required business field, **When** that field's accessor is invoked, **Then** it returns the typed missing-required-field error (e.g. `wire_required_field_missing`), not a defaulted value.
3. **Given** a grouped message (repeating group present), **When** it is parsed, **Then** the group entry count and at least one per-entry field are read back at their exact wire values.

---

### User Story 2 - Construct an outbound typed application message body (Priority: P1)

An application developer needs to build the business body of an outbound FIX application message from typed field inputs, with fail-closed validation of those inputs, producing a body the engine will frame (the engine stamps the session header and checksum trailer).

**Why this priority**: Constructing app messages is the complementary half of a usable engine and the second required deliverable per row. The v1.0 scope is the hand-written builder (the 020 pattern); the codegen writer-emitter (FR-015a) remains deferred.

**Independent Test**: For any covered row, call its builder with typed field values into a caller buffer and assert the emitted bytes are the expected canonical body (`35=<MsgType>` + business fields, no session header/trailer); assert invalid inputs return an error and leave the caller buffer untouched.

**Acceptance Scenarios**:

1. **Given** valid typed field inputs and a sufficiently sized buffer, **When** the builder is invoked, **Then** it returns the body-only byte span leading with `35=<MsgType>\x01` followed by the business fields in canonical form, with no `8=`/`9=`/`34=`/`49=`/`52=`/`56=` header and no `10=` trailer.
2. **Given** an invalid input (empty required string, out-of-range enum/side, unformattable decimal, malformed timestamp) or an undersized buffer, **When** the builder is invoked, **Then** it returns a fail-closed error and does not write to the caller buffer (atomic all-or-nothing).
3. **Given** a grouped message, **When** the builder emits a repeating group, **Then** the `No<Group>` count precedes the correct number of correctly-ordered per-entry fields.

---

### User Story 3 - Round-trip fidelity (Priority: P2)

A developer (and CI) needs assurance that a message built by the outbound builder parses back through the inbound read path to the same field values — proving builder and reader agree on the wire encoding.

**Why this priority**: Round-trip is the acceptance witness that closes a row (a read test alone does not — see Assumptions). It cross-checks the two independently hand-written halves against each other.

**Independent Test**: Feed the builder output into the framer, parse it, wrap it in the flyweight, and assert every field read back equals the value fed to the builder.

**Acceptance Scenarios**:

1. **Given** a builder invoked with a set of typed field values, **When** its output is framed, parsed, and read via the flyweight, **Then** every read-back field (including repeating-group entries and trailing-zero-normalised decimals) equals the input value.

---

### User Story 4 - Typed message headers consumable by external clients (Priority: P2)

An external consumer of the installed fixpp package needs to `#include` the typed application-message headers and use the `fixpp::v42/v44/v50sp2::<Msg>` flyweights — today those generated headers are not installed, so the typed-message scope is only witnessable in-tree.

**Why this priority**: §XVIII.7 states the typed-message scope "ships in v1.0"; that is only true if the headers are on the installed public include path. This is a shared packaging fix folded into this workstream.

**Independent Test**: Configure/install the package and, from a consumer outside the build tree, include a typed-message header and construct a flyweight over a parsed view — it compiles and links against the installed tree.

**Acceptance Scenarios**:

1. **Given** an installed fixpp package, **When** an external translation unit includes a typed application-message header, **Then** the header resolves from the installed include path (not a build-tree-private path).

---

### Edge Cases

- **Repeating groups**: grouped messages (NewOrderList, MarketDataSnapshotFullRefresh, MarketDataIncrementalRefresh, MassQuote, SecurityDefinition, QuoteRequest, etc.) — count field, per-entry field ordering, and the count-of-zero case. This is the primary unproven area: no existing builder emits a repeating group.
- **Multi-character MsgTypes**: `AS` (AllocationReport) and any other multi-char covered MsgType — dispatch is already unblocked by 057; witnesses must confirm the length-first path resolves the typed flyweight.
- **Decimal canonicalisation**: trailing-zero equality (`190.50` == `190.5`), no scientific notation, locale independence.
- **Buffer-boundary**: builder output that exactly fills, or overflows, the caller buffer — fail-closed on overflow with the buffer untouched.
- **Missing/empty required fields**: read side returns typed error; build side rejects.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: For each of the 28 covered rows the system MUST provide a hand-written typed builder (extending the feature-020 pattern in `src/session/business_messages.cpp` + its header) that emits the application **body only** — leading `35=<MsgType>\x01` then the business fields — with NO session header tags (8/9/34/49/52/56) and NO `10=` checksum trailer (INV-2; these are engine-stamped).
- **FR-002**: Builders MUST validate inputs fail-closed and be atomic: on any invalid input (empty required string, out-of-range enum/side, unformattable decimal, malformed UTCTimestamp) or insufficient output buffer, return a typed error and leave the caller buffer untouched (INV-4).
- **FR-003**: Numeric/decimal fields MUST be serialised canonically (via `decimal_t::format`): locale-independent, no scientific notation (INV-3).
- **FR-004**: Builders for grouped messages MUST support repeating groups — emit the `No<Group>` count followed by the correct number of correctly-ordered per-entry field sequences, including the count-of-zero case.
- **FR-005**: For each covered row the system MUST provide an inbound **read witness**: a hand-authored wire body → framed (body length + checksum computed) → parsed by the production framer/`MessageView` → wrapped in the version-namespaced typed flyweight → asserting the EXACT typed value of each read field (a discriminating witness, including at least one repeating-group field for grouped rows), plus a missing-required-field → typed-error assertion.
- **FR-006**: For each covered row the system MUST provide a **round-trip witness**: builder output → framed → parsed → flyweight → asserting every read-back field equals the value fed to the builder.
- **FR-007**: The system MUST install the generated typed-message flyweight headers (`_codegen/include/fixpp/{v42,v44,v50sp2}/…`) onto the public installed include path so external consumers can use the typed messages, and MUST verify consumability from outside the build tree.
- **FR-008**: The feature MUST NOT modify the codegen writer-emitter path (FR-015a stays deferred) and MUST NOT introduce new wire-format, error-enum, or C-ABI surface beyond the builder function declarations and the header-install change. (The typed flyweights already exist; the read path needs no codegen change.)
- **FR-009**: On completion, each covered row's `spec/feature-catalogue.md` status MUST flip from `backlog` to done with an evidence PR reference, and `spec/coverage-index.md` / `spec/behaviors-and-limitations.md` updated accordingly.
- **FR-010**: The unit of coverage is the distinct **message**, not the catalogue row. A row that maps to a MsgType pair (e.g. M-005 SecurityDefinitionRequest `c` / SecurityDefinition `d`) contributes both messages; inherently-inbound/response messages (e.g. SecurityDefinition, MarketDataRequestReject, ExecutionReport) are treated identically to request messages — each distinct message gets a builder + read witness + round-trip witness (~34 messages across the 28 rows). A row is done only when every message it maps to is done (FR-005/006 + builder).

### Out of Scope

- **N-002 / N-003** (UserRequest/UserResponse `BE/BF`; ApplicationMessageRequest family `BW/BX/BY`) — require session-FSM dispatch (routing through the state machine), a different class of work; deferred to a separate later feature.
- **FR-015a** — the codegen writer-emitter (automatic generation of builders). Hand-written builders only here.
- **FR-015b** — all-version coverage (per-row builders/witnesses in every applicable namespace). v1.0 targets one representative namespace per row (see Assumptions).
- **A-014..A-034** typed accessors — deferred to v1.x per §XVIII.7; runtime `view.get(tag)` already ships.

### Key Entities

- **Covered row**: one catalogue entry (A-001..A-013, M-001..M-012, P-001..P-003) mapping a MsgType (or MsgType pair) to one or more messages and a version range. Coverage is per distinct message (FR-010).
- **Typed flyweight**: the already-generated zero-copy `fixpp::v{42,44,50sp2}::<Msg>` wrapper over a parsed `MessageView`, exposing typed field accessors returning `expected_t<T>`.
- **Builder**: a hand-written `build_<message>(out, typed-fields…) -> expected_t<span>` emitting a body-only fragment.
- **Witness**: a discriminating test (read, round-trip) asserting exact field values.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Every distinct message across the 28 covered rows (~34 messages, one representative namespace each) ships a hand-written builder, an inbound read witness, and a round-trip witness; all 28 catalogue statuses flip from `backlog` to done with an evidence PR.
- **SC-002**: 100% of read and round-trip witnesses are discriminating (assert exact field values, not "parse succeeded"); every grouped row asserts at least one repeating-group entry field and the group count.
- **SC-003**: An external consumer translation unit, built against the installed package, includes a typed application-message header and constructs a flyweight — compiling and linking with no reference to any build-tree-private path.
- **SC-004**: No regressions — the Tier-1 sanitizer/analysis matrix is green — and no new public wire-format, error-enum, or C-ABI surface is introduced beyond the builder declarations and the header install.

## Assumptions

- **Version-namespace coverage** (decided, Clarifications 2026-07-05): v1.0 targets **one representative version namespace per row** (FIX44 for order-management/allocation rows, FIX42 for market-data rows, per catalogue authority). FR-015b all-version coverage is deferred post-v1.0.
- **Row-done definition** (decided, Clarifications 2026-07-05): a message is DONE when it has a hand-written builder + independent inbound read witness + round-trip witness in its representative namespace; a row is DONE when every message it maps to is done (FR-010), with catalogue status flipped and evidence recorded. A pre-existing read test alone does NOT close a row (this is why A-001/A-006, which already have read tests, remained `backlog`).
- The 28 target typed flyweights already exist in the generated `v42/v44/v50sp2` headers (verified 2026-07-05: present across all three namespaces with version-appropriate gaps); the read path needs no codegen change.
- Builders extend `src/session/business_messages.cpp` and `include/fixpp/session/business_messages.hpp`; witnesses follow `tests/session/test_business_messages_read.cpp`.
- **Implementation sequencing (for `/speckit-plan`)**: the FIRST implemented row MUST be a repeating-group exemplar (e.g. NewOrderList or MarketDataSnapshotFullRefresh) built end-to-end (builder + read + round-trip) to de-risk the unproven grouped-builder pattern before fanning out to the remaining rows.
- A-001/A-006 already have minimal FIX-4.4 interop builders (`build_new_order_single`, `build_execution_report`) and read tests from feature 020; they are closed under this feature by adding the round-trip witness (and, if the representative namespace differs, reconciling per the row-done definition).
