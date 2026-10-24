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
- **Closure-required set** — per MsgType, the required fields and required groups above. It is the
  **owner's closure bar**, stricter than either engine's own validation: only its **field** leg is
  QuickFIX's rule (QuickFIX-cpp does not require a component-wrapped group, L-067-1; QuickFIX/J
  requires the groups its own file flags). Nothing in this feature calls the group leg QuickFIX
  semantics.
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
parent `main` by the orchestrator. At Gate A round 1 the owner extended the version narrowing quoted
above to **N-002** (Clarifications, Session 2026-10-24 (Gate A round 1)). At Gate A round 2 the owner
ruled that the harness registration lands **after** the fixpp merge, with the gitlink bump
(Clarifications, Session 2026-10-24 (Gate A round 2)); the parent push happens at that step only.

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
  engines per the readiness findings Q1). The plan names it and shows the check. *(Superseded in part
  at Gate A round 1: which MsgTypes differ, and on which axis, is what FR-014's check prints; no
  artifact lists them.)*
- Q: Does "against both QuickFIX peers" mean one cell or one per peer? → A: one scenario, run against
  each peer — at least one cell per peer, each with its own evidence row.
- Q: Does N-002/N-003 need a routing witness? → A: yes, one: DECISION 2's premise is that fixpp's
  session delivers these MsgTypes to the application callback like any application message. N-002
  and N-003 are catalogue-categorised `session`, so their `done` cites a witness of that premise, not
  only a statement of it (FR-008). No production code.

### Session 2026-10-24 (Gate A round 1)

- Q: N-002's version column reads `5.0–5.0SP2`, but only `v50sp2` has a typed tier for BE/BF. Narrow
  it? → A (**owner ruling**, answering the Opus review's N-3): **narrow N-002 to `5.0SP2`**, the same
  rule as A-018/A-029/A-030 (FR-018 extends to N-002). Its Verified cell states that FIX 5.0 and
  5.0SP1 have no typed tier (runtime-XML path only), and that BE/BF are present in `FIX44.xml` but
  the v44 codegen still excludes them through `kN002N003Excluded` / `is_n002_n003_excluded` in
  `tools/codegen/fixpp-codegen/emit_builders.cpp`, whose "session-FSM" reason DECISION 2 supersedes
  (as it does the same reason in `[const §XVIII.7]`). Lifting that exclusion is fixpp#625;
  **no codegen change in 095** (FR-020).
- Q: The three `[FIX50SP1] …` Spec refs (A-018, A-029, A-030) and N-002's `[FIX50] …` — do they move
  when the version column narrows? → A: no. The Spec ref names the section that introduces the
  message, which FIX 5.0SP2 still carries. *(Reworded at Gate A round 2: the message-level form is
  the recorded `[const §VI.3]` disposition of `spec/coverage-index.md`, which 095 extends to its 12
  rows — Normative References — not catalogue debt.)*
- Q: The clarification above excludes BJ/BS for differing required sets, while research compared BQ
  and CC. Which is it? → A: both, on two different **axes**. (a) *Engine semantics:* each engine's
  own rule over its own file (QuickFIX-cpp's group requiredness versus QuickFIX/J's directly flagged
  groups). (b) *Closure-required walk:* the Terminology's rule over the vendored file versus over
  QuickFIX/J's file. FR-014's check prints both axes; the live message must agree on both. No artifact
  lists which MsgTypes differ on which axis (FR-005).
- Q: What exactly is a witness's name? → A: `TailRows095.<RowId>_<GeneratedName>`, where `<RowId>` is
  the catalogue ID with its hyphen dropped (`A018`; a gtest name cannot contain `-`) and
  `<GeneratedName>` is the vendored dictionary's message name (`ExecutionAck`). The name does not
  carry the MsgType literal; the census maps the generated name to its MsgType through the dictionary
  (FR-004a).
- Q: A required group nested in a group instance, a required component holding a group among
  siblings, or one tag at both message level and inside a group — how does a witness handle them? →
  A: for a tail MsgType, the walk **fails loudly** on each of the three; no reading is guessed and no
  recursive readback ships untested. The fail-loud is proven able to fire on a message outside the
  tail set that has a nested required group (MassQuote, 35=i). *(Superseded in part at Gate A round
  2: the tag-at-two-levels detector is deleted, and the group-among-siblings detector gets a
  synthetic fixture; see that session.)*
- Q: How are "every set field is read back" and "at least two fields" checked, rather than trusted to
  the witness's author? → A: each witness declares one **intent list** of `(path, literal)` entries;
  the readback is a bidirectional exact comparison of that list against an enumeration of the parsed
  body (FR-003).

### Session 2026-10-24 (Gate A round 2)

- Q: Registering the two cells in the parent harness before the fixpp PR merges breaks the parent
  check `RegistryAgreesWithTheRealTemplateTest` (`phase-9-harness/tools/test_emit_matrix.py`), which
  exists to stop a registration landing ahead of the gitlink that brings its template rows
  (fixpp#462). Keep #462, or accept a red window? → A (**owner ruling**, answering the Opus review's
  N-1): **"Keep #462: register after merge."** Consequences:
  - The condition: no parent commit registers the two cells while its gitlink points at a library
    commit without their `cell_results.yaml` rows. Every parent harness change of this feature (the
    registration, the per-cell dictionary check, the capability minimums, the QuickFIX/J A-TESTREQ
    fix, the goldens, the `emit_matrix` `run_kind` handling) lands in **one** parent commit, made
    after the fixpp PR merges, together with the gitlink bump to the merged fixpp `main` (or, if
    another parent commit has already bumped the gitlink past that merge, alone).
  - Before the merge, the harness changes exist only as uncommitted edits in the parent working copy,
    and the two cells run **locally** against both peers (FR-015, SC-003).
  - After the parent commit and the image republish, `interop-live` dispatched on fixpp `main` must
    list both cells as passed; its run id is recorded at close-out.
  - The fixpp PR's own `interop-live` runs against the pre-095 image and fails its population
    reconciliation (plan, Ordering). It is advisory and is not SC-003 evidence.
- Q: The tag-at-two-levels detector can fire on no vendored dictionary. Fixture it, or delete it? →
  A: delete it. The detector guarded a first-occurrence lookup, and no check uses one: FR-003's
  enumeration assigns every parsed entry to message level or to the group instance whose extent
  contains it, and FR-004's presence check reads that same enumeration. The group-among-siblings
  detector stays, because it guards the inheritance rule's own precondition; it gets an inline
  synthetic dictionary as its arm (contracts W6).
- Q: CC's required group `NoAsgnReqs`(1499) has no field child; its first emitted tag is the nested
  `NoPartyIDs`(453) count. What is its delimiter? → A: a group's delimiter is its **first emitted
  tag** in dictionary order, the rule the loader applies (`src/dictionary/xml_loader.cpp`,
  `expand_field_list`, the #208 shape), and it may be a nested group's count tag. Then each instance
  carries that nested group with exactly one instance, which sets its own delimiter. A delimiter
  chain longer than that fails loudly (FR-004).
- Q: FR-014's check asserted, for the whole tail set, that QuickFIX/J's closure walk is contained in
  the vendored one. BM differs on that axis (both round-2 reviews measured it on a message-level
  field), so the check fails on today's inputs. Narrow it? → A: yes. It hard-asserts only
  what selects BN and prints the other MsgTypes. Its RED arm is the same equality assertion pointed
  at BM, which must fail.

## User Scenarios & Testing *(mandatory)*

### User Story 1 — Every tail MsgType has a named, complete offline witness (Priority: P1)

A maintainer deciding whether a tail row may be `done` reads the row's Tests cell, finds one test per
MsgType named after the row, and that test builds the message with every required field and every
required group populated, parses it with an independent dictionary-driven reader, and checks every
value it set.

**Why this priority**: it is the bulk of the closure bar and covers all 12 rows, including C-003,
which no QuickFIX/J peer can witness live.

**Independent Test**: build and run the witness target alone; every one of the 23 tests passes, and
each test's name contains its row ID (hyphen dropped) and its generated message name (FR-001).

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

**Independent Test**: run the new cell against each peer locally before merge, then in `interop-live`
dispatched on fixpp `main` after the post-merge parent commit and image republish.

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

The 12 rows read `done`, their Tests and Verified cells name the witnesses, and the four rows whose
version column claimed a FIX 5.0 version without a typed tier (A-018, A-029, A-030, N-002) now claim
only 5.0SP2.

**Independent Test**: re-derive the 12 rows from `spec/feature-catalogue.md` and check each cited
test name exists in the tree.

**Acceptance Scenarios**:

1. **Given** the merged feature, **When** a reader opens any tail row, **Then** its Status is `done`
   and each named witness resolves to a test in the tree.
2. **Given** A-018, A-029, A-030, **When** read, **Then** the version column is `5.0SP2` and the
   Verified cell says SP1 is not covered by a typed tier and stays on the runtime-XML path.
3. **Given** N-002, **When** read, **Then** the version column is `5.0SP2` and the Verified cell says
   FIX 5.0 and 5.0SP1 have no typed tier (runtime-XML path only) and that the v44 codegen excludes
   BE/BF (`kN002N003Excluded`), naming fixpp#625.

### Edge Cases

- **A required group's own members include a nested required group** (tail MsgType) → the walk fails
  loudly naming it; the feature does not ship an untested recursive branch. The fail-loud is proven
  able to fire on 35=i (FR-004).
- **A tag that is both a message-level member and a group member of the same MsgType** → no check
  looks it up by first occurrence: FR-003's enumeration assigns each parsed entry to message level or
  to the group instance whose extent contains it, and FR-004's presence check reads that
  enumeration. A misattributed entry therefore fails the readback naming its path (Clarifications,
  Gate A round 2).
- **A tail MsgType with no required fields and no required group** → its witness still sets and reads
  back at least two fields. FR-003's intent-size assertion and its "intended, not parsed" direction
  check it, so it is not vacuous.
- **A required group with no required instance member** → each instance still sets the group's
  delimiter (FR-002); without it the parser cannot open the instance.
- **A required group whose delimiter is a nested group's count tag** (CC's `NoAsgnReqs`(1499), whose
  first emitted tag is `NoPartyIDs`(453)) → each instance carries that nested group with exactly one
  instance, which sets its own delimiter. Its entries take paths in the outer instance (FR-003). A
  nested delimiter that is itself a count tag fails loudly (FR-004).
- **Generated name ≠ catalogue title** (BN) → the witness and the row cite the generated name and the
  catalogue title side by side.
- **C-003 (CQ) has no QuickFIX/J peer** → it closes on the offline witness; the live cell does not
  carry it.
- **A future dictionary change adds or removes a required member** → the dictionary-derived check
  fails until the witness is updated; nothing in this feature records the derived counts as fixed
  numbers.
- **The peer image the live cell ran against is older than the harness change** → the lane refuses to
  run any cell, rather than passing against a stale peer. Mechanism: `ci/run-interop-live.py`'s
  `reconcile` fails its coverage check when a tree skip id is neither covered by the image's
  registered cells nor excluded, which is what a pre-095 image is once the two skip-list lines land.
  This is what the fixpp PR's own `interop-live` shows (plan, Ordering).
- **fixpp inbound validation on a FIXT session** → off on this cell; the reason (L-041-2) is stated
  in the cell's record and in the B&L delta.

## Requirements *(mandatory)*

### Functional Requirements

**Offline witnesses (US1)**

- **FR-001**: Each of the 23 tail MsgTypes MUST have exactly one offline witness test named
  `TailRows095.<RowId>_<GeneratedName>`: the catalogue row ID with its hyphen dropped, and the
  vendored dictionary's message name (Clarifications, Gate A round 1).
- **FR-002**: Each witness MUST set every member of its message's closure-required set
  (Terminology), each required group with at least two instances, each instance with all its required
  members **and the group's delimiter**; and MUST set at least two fields in total. A group's
  delimiter is its first emitted tag in dictionary order, the loader's rule (Clarifications, Gate A
  round 2). When that tag is a nested group's count tag, each instance MUST carry that nested group
  with exactly one instance, which sets the nested group's own delimiter.
- **FR-003**: Each value MUST be chosen before the build as a literal in the test (never captured from
  a build). Each witness MUST declare one **intent list** of `(path, literal)` entries, where a path
  is a tag at message level or `<NumInGroup tag>[<k>].<tag>` inside instance k, and the count fields
  are entries too. The entries of a delimiter's nested group (FR-002), its count tag included, take
  paths in the outer instance that contains them (`1499[<k>].453`, `1499[<k>].448`). After an independent dictionary-driven parse of `dictionaries/FIX50SP2.xml`, the
  witness MUST compare that list, as an exact multiset **in both directions**, against an enumeration
  of every parsed body entry, each entry assigned to message level or to the group instance that
  contains it. The tags the test's framing writes and tag 35 (checked by FR-004a) are removed from
  both sides. A set field that is not read back, a parsed field that was not intended, and a value
  mismatch each fail it, naming the path. The witness MUST also assert that its intent list holds at
  least two entries.
- **FR-004**: A check derived independently from `dictionaries/FIX50SP2.xml` by the Terminology's
  rules MUST fail when any witness omits a member of its message's closure-required set. The check:
  - returns the list of members it found missing, rather than only emitting failures;
  - takes the MsgType from the parsed frame's tag 35, never from a caller argument;
  - reads message-level and per-instance entries from FR-003's enumeration (by extent), never by
    first occurrence;
  - fails loudly, for a tail MsgType, on a nested required group, on a required component holding a
    group among siblings, and on a delimiter chain longer than FR-002 allows (Edge Cases).

  Every output of the walk, of the readback comparator, of the presence check and of the census MUST
  be shown able to fire, each arm demonstrated RED with its command and the message that fired. The
  arms are the table of contracts W6. Where no real dictionary can drive an output, its arm is an
  inline synthetic dictionary fed to the same walk entry point. The non-circular known positives
  among them are:
  - the walk's **field** leg equals the measured QuickFIX-cpp golden
    `tools/quickfix_required_golden/golden.csv` for every tail MsgType and for 35=E, read from that
    file at test time;
  - deficient NewOrderList (35=E) and StreamAssignmentRequest (35=CC) frames are each reported
    missing exactly what they lack;
  - the nested fail-loud fires on MassQuote (35=i);
  - a mutant that removes one required member from one witness, **its setter and its intent entry
    both**, goes RED with the presence check's message (not the readback's).
- **FR-004a**: A census MUST show that each of the 23 tail MsgTypes has **exactly one** witness,
  independently of test run order, `--gtest_filter` and `--gtest_shuffle`: it iterates the registered
  tests of suite `TailRows095` (`::testing::UnitTest`), maps each name's generated name to a MsgType
  through the dictionary, and requires multiplicity 1 per tail MsgType, the name's row equal to the
  row the catalogue gives that MsgType, no unmapped name and no extra MsgType. It collects every
  finding before it asserts, so one run reports all of them. Each witness also asserts that its
  parsed tag 35 equals the MsgType its own name maps to.
- **FR-005**: The rule and the command that re-derives the closure-required sets MUST be stated in the
  feature's artifacts; no artifact or comment may record the derived counts or member lists as a
  result. Authority: the parent repo's `.claude/CLAUDE.md`, section "Testing & Verification" ("A
  comment may record a PROCEDURE; it may not record a RESULT"). The known-positive assertions of
  FR-004 and FR-014 are the one exception (contracts W7 lists them): they re-run on every
  invocation, so they cannot rot silently.
- **FR-006**: The existing 077 round-trip tests MUST keep passing unchanged in behaviour; this
  feature strengthens the evidence, it does not weaken or delete existing coverage.

**N-002 / N-003 (US2)**

- **FR-007**: A per-row completeness pin MUST assert, by name and in tests named after N-002 and
  N-003, that BE, BF (N-002) and BW, BX, BY (N-003) are keys of the generated v50sp2 builder registry.
- **FR-008**: A session-level cell MUST show that an inbound BE and an inbound BW on an established
  session are delivered to the application callback and not handled as admin messages, using existing
  session test infrastructure and no production-code change. Each leg (BE, BW) MUST be shown able
  to fail on its own.

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
  dictionaries with the same required set on both axes (Clarifications, Gate A round 1). The check
  that selected it MUST be committed and runnable: it reads the vendored `dictionaries/FIX50SP2.xml`
  and the QuickFIX/J 3.0.1 jar's `FIX50SP2.xml` (the file the peer config loads) and
  `FIX50SP2.modified.xml`, and prints both axes for the tail set. It hard-asserts only what selects
  the chosen MsgType: present in every file, equal on every axis, its closure-required set within the
  sent fields, and every sent field a member in every file. It MUST also show that its equality
  assertion can fail: the same assertion pointed at BM, which differs, fails. It **fails, never
  skips,** when an input is absent (Clarifications, Gate A round 2).
- **FR-015**: No parent commit may register the two cells while its gitlink points at a library
  commit without their template rows (fixpp#462; owner ruling, Gate A round 2). Every parent harness
  change of this feature therefore lands in one parent commit after the fixpp PR merges, together with
  the gitlink bump to the merged fixpp `main` (alone, if another parent commit has already bumped the
  gitlink to a library commit that contains the merge), so `RegistryAgreesWithTheRealTemplateTest` holds at
  every parent commit. Before the merge, the two cells MUST be run locally against both peers, with
  the harness changes held uncommitted in the parent working copy, and the commands and outputs
  recorded in the verify record. After the parent commit and the counterparty image republish,
  `interop-live` dispatched on fixpp `main` MUST list both cells as passed, and its run id is recorded
  at close-out. The cell's evidence MUST identify the peer build it ran against: the hello record
  carries the harness's image-digest sentinel unless the lane sets the digest, and the image digest
  each run pulled is recorded (data-model §D).
- **FR-016**: The cell's evidence MUST be recorded in the interop evidence files under the same
  schema and gates as the existing live cells; any hand-maintained pin those gates use MUST be
  updated and its gate shown RED before the update.

**Catalogue and records (US4)**

- **FR-017**: The 12 tail rows MUST move to `done`, with Tests cells naming each witness (FR-001,
  FR-007, FR-008, and the live cell for the row that carries it) and Verified cells stating the bar
  they closed on.
- **FR-018**: A-018, A-029, A-030 and N-002's FIX version column MUST become `5.0SP2`, with the
  Verified cell stating that the FIX 5.0 versions the column dropped have no typed tier and stay on
  the runtime-XML path. N-002's Verified cell also states the v44 BE/BF exclusion and names
  fixpp#625 (Clarifications, Gate A round 1). The Spec ref cells are unchanged.
- **FR-019**: `spec/coverage-index.md`'s existing row for each tail MsgType MUST name its witness in
  its gap-note column (no new row), and the B&L files MUST carry the delta: FR-013's disclosure, that
  FIX 4.4 BE/BF have no typed builder (the v44 exclusion), and anything a witness exposes.
- **FR-020**: No production source changes (`src/`, non-generated `include/`, codegen). If a witness
  exposes a defect in production code, the defect MUST be filed as an issue and brought to the owner
  before the affected row is flipped.

### Key Entities

- **Tail row**: catalogue ID, its MsgTypes, version column, Tests/Verified cells.
- **Closure-required set**: per MsgType, the required fields and required groups (with instance
  members), derived from the dictionary by the Terminology's rules.
- **Witness**: per MsgType, its intent list and the bidirectional comparison against the parsed body.
- **Live cell record**: cell ID, peer, peer build identity, outcome, per-field comparison.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: 23 of 23 tail MsgTypes have a passing named witness; 0 tail MsgTypes are left with an
  unset required field or required group according to the dictionary-derived check.
- **SC-002**: Every non-vacuity arm named in FR-004 (the contracts W6 table), FR-004a, FR-008,
  FR-012, FR-014 and FR-016, and the records check's own RED run (quickstart §7), has been run and
  recorded RED with its command and the message that fired.
- **SC-003**: The live cell passes against both QuickFIX peers locally before merge (verify record),
  and in `interop-live` dispatched on fixpp `main` after the post-merge parent commit and the image
  republish (run id recorded at close-out). If that run does not pass both cells, the close-out does
  not complete and the failure goes to the owner. The fixpp PR's own `interop-live` is not SC-003
  evidence (FR-015).
- **SC-004**: 12 of 12 tail rows read `done` on `main` after merge. The `[const §I.3]` count of
  OFFICIAL rows that are neither `done` nor `dropped` drops by 12 between the merge-base and the
  head, and the only rows whose Status differs are the 12 tail rows, re-derived with 094's
  catalogue census (quickstart §7).
- **SC-005**: No file under `src/`, non-generated `include/` or `tools/codegen/` changes.

## Assumptions

- The vendored `dictionaries/FIX50SP2.xml` is the dictionary of record for the tail set; it is
  byte-identical to QuickFIX-cpp v1.16.0's (readiness findings Q1). Quickstart §5 re-checks it with
  `cmp` against the `qfcpp-spec` the peer loads.
- The existing live interop lane (`interop-live.yml`, `pull_request` and `workflow_dispatch` only)
  and its GHCR counterparty image are the CI vehicle for the live cell, run by dispatch on `main`
  after merge; it stays advisory, as today.
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
- Lifting the v44 BE/BF codegen exclusion (`kN002N003Excluded`): fixpp#625, not done here
  (Clarifications, Gate A round 1).

## Normative References

- **Granularity: message-level, a recorded `[const §VI.3]` disposition.** `spec/coverage-index.md`
  indexes "FIX Application Layer (FIX40–FIX50SP2)" "at message granularity (one row per MsgType)".
  Its "ART VI §3 DISPOSITION (067-codegen-writer-emitter …)" comment records message-level
  `[FIX44]`/`[FIX50SP2]` refs as a deliberate design-authority disposition for 067's rows, and 069's
  spec (Normative References) extended it to the FIX44 families. **095 extends it to its 12 rows**,
  which are FIX 5.0–5.0SP2 messages: plan P7 records a one-line disposition beside 067's comment, and
  quickstart §7 checks that it is there. The 12 rows' Spec refs stay as `spec/feature-catalogue.md`
  carries them: `[FIX50SP1] Single General Order Handling` (A-018); `[FIX50SP2] Order Mass Handling`
  (A-023); `[FIX50SP2] Market Structure Reference Data` (A-027, A-028); `[FIX50SP1] Securities
  Reference Data` (A-029); `[FIX50SP1] Settlement Instructions` (A-030); `[FIX50SP2] Allocation`
  (A-031); `[FIX50SP2] User Management` (A-032); `[FIX50SP2] Application Sequencing` (A-033, N-003);
  `[FIX50SP2] Position Maintenance` (C-003); `[FIX50] User Management` (N-002).
- **The coverage-index entries** (`| MsgType | Message | Catalogue ID |`), by heading of "FIX
  Application Layer (FIX40–FIX50SP2)":
  - "Application Messages — Reference Data": `BI TradingSessionListRequest A-027`,
    `BJ TradingSessionList A-027`, `BS TradingSessionListUpdateReport A-027`,
    `BT MarketDefinitionRequest A-028`, `BU MarketDefinition A-028`,
    `BV MarketDefinitionUpdateReport A-028`, `BK SecurityListUpdateReport A-029`,
    `BP SecurityDefinitionUpdateReport A-029`.
  - "Application Messages — Trade (FIX 4.0+)": `BN ExecutionAcknowledgement A-018, A-024`,
    `CA OrderMassActionRequest A-023`, `BZ OrderMassActionReport A-023`.
  - "Application Messages — Post-Trade (FIX 4.0+)": `BQ SettlementObligationReport A-030`,
    `BM AllocationInstructionAlert A-031`.
  - "Application Messages — Infrastructure (FIX 4.2+)": `CB UserNotification A-032`,
    `CC StreamAssignmentRequest A-033`, `CD StreamAssignmentReport A-033`,
    `CE StreamAssignmentReportACK A-033`, `CQ AccountSummaryReport C-003`, `BE UserRequest N-002`,
    `BF UserResponse N-002`, `BW ApplicationMessageRequest N-003`,
    `BX ApplicationMessageRequestAck N-003`, `BY ApplicationMessageReport N-003`.
- **The live cell's session profile** (coverage-index FIX-SL section table, titles quoted):
  - `[FIX-SL §5.3.1] Profile identification (BeginString=FIXT.1.1)` (S-020);
  - `[FIX-SL §4.3.7] Specifying application version (DefaultApplVerID 1137 / FIXT)` (S-025, S-026);
  - `[FIX-SL §5.3.3] Session default application version identification (DefaultApplVerID 1137)`
    (S-025);
  - `[FIX-SL §5.3.5] Explicit application version per message (ApplVerID 1128)` (S-026; not stamped
    by the cell, research R-8).

  The "FIXT.1.1 (FIXT)" table indexes the same sections by concept ("BeginString=FIXT.1.1 — session
  profile identification", "DefaultApplVerID(1137) on Logon — default app version", "ApplVerID(1128)
  per message — per-message app version override").
- **Constitution:** `[const §I.3]` the tag gate; `[const §VI.4]`, `[const §VI.5]`, `[const §VI.6]`
  coverage-index, this section, closing a `done` row; `[const §VII.6]` interop; `[const §VII.8]`
  grouped tests and label selection; `[const §XVI.6]`, `[const §XVI.7]` orchestrator role and
  `/simplify`; `[const §XVII.1]`, `[const §XVII.7]`, `[const §XVII.8]` Gate A, local build and its
  resource gate, verify; `[const §XVIII.7]` the N-002/N-003 deferral text DECISION 2 supersedes.
- **B&L:** L-041-2 (FIXT validation, FR-013), L-067-1 (component-wrapped groups), L-069-1 (enum
  domains), L-089-1 (evidence attests no fixpp revision).
