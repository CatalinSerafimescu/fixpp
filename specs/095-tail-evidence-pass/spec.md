# Feature Specification: Typed app-message tail — evidence pass for the 12 FIX 5.0SP2 rows

**Feature Branch**: `095-tail-evidence-pass`

**Created**: 2026-10-24

**Status**: Draft

**Input**: User description: "V1.0 FUNCTIONAL TRACK row 2 — close 12 OFFICIAL catalogue rows (A-018,
A-023, A-027..A-033, C-003, N-002, N-003; 23 FIX 5.0SP2 MsgTypes) on the owner's amended closure bar:
a named offline witness per message that sets every required field and every required repeating
group, a per-row completeness pin for N-002/N-003, ONE live FIXT.1.1 / FIX.5.0SP2 interop cell
against both QuickFIX peers carrying a typed-built SP2 message, the SP1 halves of three rows narrowed
to 5.0SP2, and the 12 rows flipped to `done`."

## Terminology

- **Tail row** — one of the 12 OFFICIAL catalogue rows this feature closes: A-018 (35=BN), A-023
  (CA, BZ), A-027 (BI, BJ, BS), A-028 (BT, BU, BV), A-029 (BK, BP), A-030 (BQ), A-031 (BM), A-032 (CB),
  A-033 (CC, CD, CE), C-003 (CQ), N-002 (BE, BF), N-003 (BW, BX, BY). Together they name 23 MsgTypes,
  the **tail set**. The catalogue row is the authority for which MsgTypes a row names.
- **Typed builder** — the generated `fixpp::v50sp2::build_<Msg>` / `<Msg>Args` pair for a MsgType.
  The generated name follows the vendored dictionary's message name, which can differ from the
  catalogue title (35=BN is `ExecutionAck` in fixpp and QuickFIX-cpp, `ExecutionAcknowledgement` in
  the catalogue and QuickFIX/J).
- **Required field** — a field whose every element on its path from the message (each enclosing
  component reference, group and the field itself) is `required='Y'` in `dictionaries/FIX50SP2.xml`.
  This is QuickFIX's AND composition, which fixpp matches (B&L L-067-1).
- **Required group** — a repeating group whose every element on its path from the message is
  required, where a group that is the **only child** of a component inherits that component
  reference's `required` flag. Reason: the vendored FIX50SP2 dictionary (byte-identical to
  QuickFIX-cpp v1.16.0's `spec/FIX50SP2.xml`) writes a required group as a `required='Y'` component
  reference wrapping a `required='N'` group; QuickFIX/J 3.0.1's `FIX50SP2.modified.xml` marks the
  same groups `required='Y'` directly. Required **members of a group instance** are the instance's
  required fields and required nested groups by the same two rules, starting from the group.
- **Offline witness** — a test per MsgType: values chosen before the build → typed builder → wire
  bytes → an independent, dictionary-driven parse of `FIX50SP2.xml` → per-field (and per-group-
  instance) readback compared against the chosen values. "Independent" means the parse does not use
  the typed builder's code or tables. This is the bar A-001..A-012 and N-001 closed on (067/069/077).
- **Live FIXT cell** — the one new interop cell (§User Story 3).

## Context: why this feature exists

`[const §I.3]` blocks the v1.0 tag until every OFFICIAL row is `done` or explicitly dropped. The
owner's ★ V1.0 FUNCTIONAL TRACK (parent `REMAINING-WORK.md`, 2026-10-21) makes this its step 2,
after the bookkeeping PR (094, merged 2026-10-23 as `45a620f4`).

**The closure bar was amended by the owner on 2026-10-21** (parent `REMAINING-WORK.md` ★ SCOPE
DECISION, "Closure bar AMENDED"; evidence `research/findings/v1-breadth-sweep-readiness-2026-10-21.md`).
The earlier bar ("typed builder + live QuickFIX interop, satisfied in bulk by row 4d") rested on a
false premise: 089's live machinery is one hand-coded FIX 4.4 conversation, and a per-message live
sweep would need a two-repo FIXT infrastructure feature. The amended bar, quoted:

> typed builder → wire → independent dictionary-driven parse → per-field readback against chosen
> values (077's all-versions round-trip, strengthened with each message's required repeating groups
> and named per row), **plus ONE live FIXT.1.1 / FIX.5.0SP2 interop cell** against both QuickFIX peers
> carrying a representative SP2 application message (validation off; L-041-2 disclosed). A-018,
> A-029, A-030's version column narrows to 5.0SP2 (no `v50sp1` typed tier; SP1 stays runtime-XML).
> Applies to the 10 QuickFIX-route rows and N-002/N-003.

**DECISION 2 (owner, 2026-10-21) — N-002/N-003 are BUILT as application messages**, not as a
session-FSM feature: the typed builders already exist in `v50sp2`; QuickFIX-cpp, QuickFIX/J and
QuickFIX/Go treat only `0 A 1 2 3 4 5` as admin, so BE/BF/BW/BX/BY reach `fromApp` like any
application message, and fixpp's session does not special-case them either. "Close the N-001 way: a
differential round-trip witness per message, a completeness-pin entry, then `done`. Routing stays
`fromApp`. No production code."

**Owner rulings, 2026-10-24:** the live FIXT cell sends a message built with the **typed builder**,
not 089's generic body builder; the parent harness change that the cell needs may be pushed to the
parent `main` by the orchestrator.

**What exists today (read at `4e8b3779`):**
- `tests/session/test_077_allversions_builder_roundtrip.cpp` already round-trips every one of the 23
  MsgTypes through its typed builder and an independent dictionary-driven parse — but with at most
  six scalar fields per message, **required groups left unset**, and test names that do not name a
  catalogue row. Its header says so.
- `tests/codegen/test_077_v50sp2_builder_completeness.cpp` pins the generated v50sp2 builder registry
  set-for-set against a raw walk of `FIX50SP2.xml`. It pins the SET; no assertion names a tail row.
- The only live FIXT cells are 033's Logon → Heartbeat → Logout cells
  (`tests/interop/happy/hp_fixt50sp2_logon_hb_logout_test.cpp`); no FIXT cell carries an application
  message.
- fixpp's generated `validate_<Msg>` does not require a group that the dictionary makes required only
  through its wrapping component (e.g. `TradingSessionListArgs::trading_sessions` is optional). This
  is QuickFIX-cpp parity (L-067-1) and stays out of scope; the witnesses set those groups anyway.

## Clarifications

### Session 2026-10-24 (orchestrator, from the owner rulings above)

- Q: Which live message? → A: one message of the tail set that is present in both QuickFIX-cpp
  v1.16.0's and QuickFIX/J 3.0.1's FIX50SP2 dictionaries and whose required set is the same in both
  (so not CQ, absent from QuickFIX/J; not BJ/BS/BM, whose required sets differ between the two
  engines per the readiness findings Q1). The plan names it and shows the check.
- Q: Does "against both QuickFIX peers" mean one cell or one per peer? → A: one scenario, run against
  each peer — at least one cell per peer, each with its own evidence row.
- Q: Does N-002/N-003 need a routing witness? → A: yes, one: DECISION 2's premise is that fixpp's
  session delivers these MsgTypes to the application callback like any application message. N-002
  and N-003 are catalogue-categorised `session`, so their `done` cites a witness of that premise, not
  only a statement of it (FR-008). No production code.

## User Scenarios & Testing *(mandatory)*

### User Story 1 — Every tail MsgType has a named, complete offline witness (Priority: P1)

A maintainer deciding whether a tail row may be `done` reads the row's Tests cell, finds one test per
MsgType named after the row, and that test builds the message with every required field and every
required group populated, parses it with an independent dictionary-driven reader, and checks every
value it set.

**Why this priority**: it is the bulk of the closure bar and covers all 12 rows, including C-003,
which no QuickFIX/J peer can witness live.

**Independent Test**: build and run the witness target alone; every one of the 23 tests passes, and
each test's name contains its row ID and MsgType.

**Acceptance Scenarios**:

1. **Given** a tail MsgType, **When** its witness runs, **Then** every required field and every
   required group (with at least two instances, each with its required members) is set, built,
   parsed independently, and read back equal to the chosen value, group instances in order.
2. **Given** a witness from which one required field or one required group is removed, **When** the
   suite runs, **Then** it fails, and the failure is caught by a check derived from the dictionary,
   not by a list in the witness itself.
3. **Given** a value in the parsed frame that differs from the chosen one, **When** the witness runs,
   **Then** it fails naming the tag.

---

### User Story 2 — N-002 / N-003 close the N-001 way (Priority: P1)

A maintainer can see, per row, that the five MsgTypes are in the generated builder registry, round-
trip through their typed builders, and are delivered by the session to the application callback.

**Why this priority**: DECISION 2 makes these two rows BUILD; without this they stay `backlog`.

**Independent Test**: run the per-row pin and the routing cell alone.

**Acceptance Scenarios**:

1. **Given** the generated v50sp2 builder registry, **When** the per-row pin runs, **Then** each of
   BE, BF, BW, BX, BY is asserted present by name, in a test named after N-002 or N-003.
2. **Given** an established FIX session, **When** a BE (N-002) or a BW (N-003) message arrives,
   **Then** the application callback receives it and the session does not treat it as an admin
   message.

---

### User Story 3 — One live FIXT.1.1 / FIX.5.0SP2 cell against both QuickFIX peers (Priority: P2)

A maintainer can see that a message fixpp builds with a typed v50sp2 builder, sent on a real FIXT.1.1
session with DefaultApplVerID FIX.5.0SP2, is accepted and parsed by QuickFIX-cpp and by QuickFIX/J
with their own dictionaries, and that each peer reads back the values fixpp chose.

**Why this priority**: it is the one live element of the amended bar; it depends on a parent harness
change and an image republish, so it is the long pole.

**Independent Test**: run the new cell against each peer locally, then in the live interop lane.

**Acceptance Scenarios**:

1. **Given** fixpp and a QuickFIX peer on a FIXT.1.1 session with DefaultApplVerID FIX.5.0SP2,
   **When** fixpp sends the representative message built by its typed builder, **Then** the peer
   accepts it with its data dictionary on, and the cell compares the peer's per-field readback with
   fixpp's chosen values and passes only if every chosen field matches.
2. **Given** the same cell with one chosen value changed on fixpp's side after the expected values
   were fixed, **When** it runs, **Then** it fails.
3. **Given** a peer that rejects the message, **When** the cell runs, **Then** it fails.

---

### User Story 4 — The catalogue says what shipped (Priority: P2)

The 12 rows read `done`, their Tests and Verified cells name the witnesses, and the three rows whose
version column claimed 5.0SP1 now claim only 5.0SP2.

**Independent Test**: re-derive the 12 rows from `spec/feature-catalogue.md` and check each cited
test name exists in the tree.

**Acceptance Scenarios**:

1. **Given** the merged feature, **When** a reader opens any tail row, **Then** its Status is `done`
   and each named witness resolves to a test in the tree.
2. **Given** A-018, A-029, A-030, **When** read, **Then** the version column is `5.0SP2` and the
   Verified cell says SP1 is not covered by a typed tier and stays on the runtime-XML path.

### Edge Cases

- **A required group's own members include a nested required group** → the instance sets it too,
  by the same rule; the dictionary-derived check covers nested levels.
- **A field present both at message level and inside a group** (same tag) → readback is scoped:
  message-level fields are read outside group instances, instance fields within their instance.
- **A tail MsgType with no required fields and no required group** → its witness still sets and reads
  back at least two fields, so it is not vacuous.
- **Generated name ≠ catalogue title** (BN) → the witness and the row cite the generated name and the
  catalogue title side by side.
- **C-003 (CQ) has no QuickFIX/J peer** → it closes on the offline witness; the live cell does not
  carry it.
- **A future dictionary change adds or removes a required member** → the dictionary-derived check
  fails until the witness is updated; nothing in this feature records the derived counts as fixed
  numbers.
- **The peer image the live cell ran against is older than the harness change** → the cell refuses to
  run or fails, rather than passing against a stale peer.
- **fixpp inbound validation on a FIXT session** → off on this cell; the reason (L-041-2) is stated
  in the cell's record and in the B&L delta.

## Requirements *(mandatory)*

### Functional Requirements

**Offline witnesses (US1)**

- **FR-001**: Each of the 23 tail MsgTypes MUST have exactly one offline witness test whose name
  contains its catalogue row ID and the generated message name.
- **FR-002**: Each witness MUST set every required field and every required group of its message
  (Terminology), each required group with at least two instances, each instance with all its required
  members; and MUST set at least two fields in total.
- **FR-003**: Each value MUST be chosen before the build as a literal in the test (never captured from
  a build), and the readback MUST compare every set field — message-level and per group instance, in
  instance order, plus each group's count field — against that literal after an independent
  dictionary-driven parse of `dictionaries/FIX50SP2.xml`.
- **FR-004**: A check derived independently from `dictionaries/FIX50SP2.xml` by the Terminology's
  rules MUST fail when any witness omits a required field or a required group (at any nesting level)
  of its message. The check MUST be shown able to fail: a known-positive arm (a message outside the
  tail set whose required group the rule must find, e.g. NewOrderList's NoOrders) and a mutant that
  removes one required member from one witness, each demonstrated RED.
- **FR-005**: The rule and the command that re-derives the required-member sets MUST be stated in the
  feature's artifacts; no artifact or comment may record the derived counts or member lists as a
  result (`[const §XVII]` claims-rot rule in `.claude/CLAUDE.md`: procedure, not result).
- **FR-006**: The existing 077 round-trip tests MUST keep passing unchanged in behaviour; this
  feature strengthens the evidence, it does not weaken or delete existing coverage.

**N-002 / N-003 (US2)**

- **FR-007**: A per-row completeness pin MUST assert, by name and in tests named after N-002 and
  N-003, that BE, BF (N-002) and BW, BX, BY (N-003) are keys of the generated v50sp2 builder registry.
- **FR-008**: A session-level cell MUST show that an inbound BE and an inbound BW on an established
  session are delivered to the application callback and not handled as admin messages, using existing
  session test infrastructure and no production-code change.

**Live FIXT cell (US3)**

- **FR-009**: One live cell scenario MUST run against QuickFIX-cpp v1.16.0 and against QuickFIX/J
  3.0.1 on a FIXT.1.1 session with DefaultApplVerID FIX.5.0SP2 (`1137=9`).
- **FR-010**: fixpp MUST build the cell's application message with the tail MsgType's typed builder
  (owner ruling 2026-10-24), from values fixed before the build.
- **FR-011**: Each peer MUST parse the message with its own data dictionary on (transport FIXT11,
  application FIX50SP2), and the cell MUST pass only if the peer accepted it and the peer's per-field
  readback equals fixpp's chosen value for every chosen field.
- **FR-012**: The cell MUST be shown able to fail: a changed chosen value and a peer-side rejection
  each turn it RED.
- **FR-013**: fixpp's inbound validation MUST be off on this cell, and the cell's record and the B&L
  delta MUST state why (L-041-2: the session validator takes one dictionary).
- **FR-014**: The representative message MUST be a tail MsgType present in both engines' FIX50SP2
  dictionaries with the same required set in both; the plan MUST show the check that selected it.
- **FR-015**: The parent harness change MUST land on the parent `main` and the counterparty image
  MUST be republished before any fixpp commit that depends on it is pushed; the cell's evidence MUST
  identify the peer build it ran against.
- **FR-016**: The cell's evidence MUST be recorded in the interop evidence files under the same
  schema and gates as the existing live cells; any hand-maintained pin those gates use MUST be
  updated and its gate shown RED before the update.

**Catalogue and records (US4)**

- **FR-017**: The 12 tail rows MUST move to `done`, with Tests cells naming each witness (FR-001,
  FR-007, FR-008, and the live cell for the row that carries it) and Verified cells stating the bar
  they closed on.
- **FR-018**: A-018, A-029 and A-030's FIX version column MUST become `5.0SP2`, with the Verified cell
  stating that FIX 5.0SP1 has no typed tier and stays on the runtime-XML path.
- **FR-019**: `spec/coverage-index.md` MUST carry a line per tail MsgType naming its witness, and the
  B&L files MUST carry the delta (FR-013's disclosure, and anything a witness exposes).
- **FR-020**: No production source changes (`src/`, non-generated `include/`, codegen). If a witness
  exposes a defect in production code, the defect MUST be filed as an issue and brought to the owner
  before the affected row is flipped.

### Key Entities

- **Tail row**: catalogue ID, its MsgTypes, version column, Tests/Verified cells.
- **Required-member set**: per MsgType, the required fields and required groups (with instance
  members), derived from the dictionary by the Terminology's rules.
- **Witness**: per MsgType, the chosen values and their readback.
- **Live cell record**: cell ID, peer, peer build identity, outcome, per-field comparison.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: 23 of 23 tail MsgTypes have a passing named witness; 0 tail MsgTypes are left with an
  unset required field or required group according to the dictionary-derived check.
- **SC-002**: Every non-vacuity arm named in FR-004, FR-012 and FR-016 has been run and recorded RED
  with its command.
- **SC-003**: The live cell passes against both QuickFIX peers, locally and in the live interop lane
  on the PR head.
- **SC-004**: 12 of 12 tail rows read `done` on `main` after merge, and the `[const §I.3]` backlog
  count of OFFICIAL rows drops by 12, re-derived from the catalogue.
- **SC-005**: No file under `src/`, non-generated `include/` or `tools/codegen/` changes.

## Assumptions

- The vendored `dictionaries/FIX50SP2.xml` is the dictionary of record for the tail set; it is
  byte-identical to QuickFIX-cpp v1.16.0's (readiness findings Q1, re-checkable with `cmp`).
- The existing live interop lane (`interop-live.yml`) and its GHCR counterparty image are the
  vehicle for the live cell; it stays advisory, as today.
- 089's evidence-integrity residual L-089-1 (evidence attests no fixpp revision) applies to the live
  cell's committed evidence as it does to 089's; this feature does not close it.
- The owner rulings quoted in Context are settled and not re-opened by this feature.

## Out of Scope

- FIX Latest rows A-035..A-065 (FUNCTIONAL TRACK row 3) and `ApplExtID(1156)=303` / version-registry
  re-key (row 4, L-074-1).
- `validate_<Msg>` enforcing component-required groups (QuickFIX-cpp parity, L-067-1) and enum value
  domains (L-069-1).
- A FIX 5.0SP1 typed tier; per-message live sweeps; fixpp validation-on over FIXT (L-041-2).
- Exporting/installing the typed builder libraries (fixpp#613, running in parallel).
