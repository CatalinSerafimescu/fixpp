# Feature Specification: Typed app-message tail — evidence pass for the 31 FIX Latest rows

**Feature Branch**: `096-fix-latest-tail-evidence`

**Created**: 2026-10-28

**Status**: Draft

**Input**: User description: "V1.0 FUNCTIONAL TRACK row 3 — close the 31 OFFICIAL catalogue rows
A-035..A-065 on 095's offline evidence bar, on both typed tiers (`fixpp::v50sp2` and
`fixpp::vlatest`), with the Spec refs corrected to `[const §VI.2]`, L-069-1 widened, and the
roadmap clause that still lists these rows as v1.2 work amended."

## Terminology

- **Tail row** — one of the 31 OFFICIAL catalogue rows A-035..A-065. Each names exactly one MsgType;
  together they are the **tail set**: DS, DT, DJ, DK, DC, DR, DO, DP, CF, CG, CK, CX, CY, CU, CV, CZ,
  DA, DB, CL, CM, CR, CS, CT, DE, DH, DI, DF, DG, CN, CO, CW. The catalogue row is the authority for
  which MsgType a row names.
- **The two dictionaries.** `dictionaries/FIX50SP2.xml` (QuickFIX-XML, byte-identical to
  QuickFIX-cpp v1.16.0's `spec/FIX50SP2.xml`; it carries FIX 5.0SP2 Extension Packs, which is why
  the tail set is in it) and `dictionaries/orchestra/OrchestraFIXLatest.xml` (FIX Orchestra, EP303).
  Every tail MsgType is an application message of both: a `message` with `msgcat='app'` in the
  first, a `fixr:message` whose `category` is not `Session` in the second, and Orchestra marks each
  `added="FIX.5.0SP2"`. Re-derive:
  `grep -o "msgtype='<MT>'[^>]*" dictionaries/FIX50SP2.xml` and
  `grep -o '<fixr:message [^>]*msgType="<MT>"[^>]*>' dictionaries/orchestra/OrchestraFIXLatest.xml`.
- **The two typed tiers.** `fixpp::v50sp2::build_<Msg>` / `<Msg>Args` (feature 077, generated from
  `FIX50SP2.xml`) and `fixpp::vlatest::build_<Msg>` / `<Msg>Args` (features 076/077, generated from
  the Orchestra file). The generated name is the dictionary's message name; for the tail set both
  dictionaries use the catalogue title's name.
- **Closure-required set, v50sp2 leg** — exactly 095's closure-required set (095 spec, Terminology:
  "Required field", "Required group", "Closure-required set"), derived from `FIX50SP2.xml` by 095's
  walk, unchanged.
- **Closure-required set, vlatest leg** — the same notion over the Orchestra file:
  - a field is required when every element on its path from the message's `fixr:structure` (each
    enclosing `fixr:componentRef`, each enclosing `fixr:groupRef`, and the `fixr:fieldRef` itself)
    carries `presence="required"`; an absent `presence` is optional (Orchestra's default);
  - a group is required when every element on its path, its own `fixr:groupRef` included, carries
    `presence="required"`;
  - a group instance's required members follow the same rules, starting from the group's members;
  - the message's references to the standard header and trailer components are not part of the
    walk (the test's framing writes those tags);
  - a group's delimiter is the first tag it emits in declaration order with components expanded,
    the group's `fixr:numInGroup` excluded: the rule `dict::OrchestraLoader` applies (the plan cites
    the loader line).
  Orchestra references a group directly (`fixr:groupRef`), so 095's "only child of a component"
  inheritance has no counterpart here; the walk fails loudly, naming the element, on any shape it
  does not handle (Edge Cases).
- **Offline witness** — per MsgType and per tier: values chosen before the build → that tier's typed
  builder → wire bytes → an independent, dictionary-driven parse → bidirectional per-field and
  per-group-instance readback against the chosen values. "Independent" means the parse uses neither
  the typed builder's code nor its generated tables. The v50sp2 witness parses with a dictionary
  loaded from `FIX50SP2.xml`; the vlatest witness parses with a dictionary loaded from the Orchestra
  file by `dict::OrchestraLoader`. This is 095's bar (095 FR-001..FR-004a), applied per tier.
- **Cross-dictionary differential** — per tail MsgType, the vlatest-leg closure-required set compared
  with the v50sp2-leg set: two walks over two independently authored files (the FIX Trading
  Community's Orchestra repository and QuickFIX's XML), sharing no code (FR-008).

## Context: why this feature exists

`[const §I.3]` blocks the v1.0 tag until every OFFICIAL row is `done` or explicitly dropped. The
owner's ★ V1.0 FUNCTIONAL TRACK (parent `REMAINING-WORK.md`, 2026-10-21) makes this its step 3,
after 095 (merged 2026-10-25 as `42d2c648`), and says it "reuses 2's evidence-per-row shape".

**The premise this row was planned on does not hold.** The track and the closure-bar amendment
(parent `REMAINING-WORK.md` ★ SCOPE DECISION) say: "FIX Latest rows have no QuickFIX peer and close
by differential verification vs the 181-message baseline". Read at `2de73f3f` on 2026-10-28:
- all 31 tail MsgTypes are application messages of the vendored `FIX50SP2.xml`, so each also has a
  `fixpp::v50sp2` typed builder;
- QuickFIX-cpp's `FIX50SP2.xml` is byte-identical to fixpp's, so QuickFIX-cpp's dictionary defines
  all 31;
- QuickFIX/J 3.0.1's `FIX50SP2.modified.xml` (the 108-message base edition) defines none of the 31
  (re-derive: grep each MsgType in
  `reference-engines/quickfixj/quickfixj-messages/quickfixj-messages-fix50sp2/target/classes/FIX50SP2.modified.xml`
  in the parent repo; `35=BN` is the positive control).

The coverage index's FIX-Latest section has carried the same premise since Phase 1 ("A-035–A-065
account for all new MsgTypes in FIX Latest not present in FIX 5.0SP2"): true of the base 5.0SP2
edition, false of the vendored dictionary.

**Owner rulings, 2026-10-28** (asked with that evidence in front of the owner):
1. **Both tiers on 095's bar.** The version column becomes `5.0SP2, FIX Latest`. Per row: a v50sp2
   witness on 095's walk, unchanged, plus a vlatest witness against a new required-walk over the
   Orchestra file — the "differential" leg.
2. **No new live interop cell.** Row 2's live FIXT.1.1 / FIX.5.0SP2 scenario (35=BN against both
   QuickFIX peers; 095, B-095-1) is the tail's one live cell under the 2026-10-21 bar. Each Verified
   cell records that QuickFIX/J 3.0.1 defines none of the 31.
3. **L-069-1:** widen the live B&L entry to every builder tier, cite it per row, and file a labelled
   issue for builder-side enum validation — filed as **fixpp#637**. No emitter code.
4. **Autonomy** as for row 2: Gate A through merge; gate exhaustion, owner matters and the Article XX
   sign-off come back to the owner.

**What exists today (read at `2de73f3f`):**
- `tests/session/test_077_allversions_builder_roundtrip.cpp` (suite `AllVersionsRoundtrip077V50SP2`)
  and `tests/session/test_077_allversions_builder_roundtrip_vlatest.cpp` (suite
  `AllVersionsRoundtrip077Vlatest`) each round-trip all 31 MsgTypes — at most six scalar fields per
  message, every repeating group unset, test names naming no catalogue row. Their headers say so.
- 076's census (`tests/codegen/vlatest_completeness_census_test.cpp`) and reify round-trip
  (`tests/wire/vlatest_reify_roundtrip_test.cpp`) pin the 181-message vlatest SET and the read path;
  077's completeness tests pin each tier's builder SET. No assertion names a tail row.
- 095's `tests/session/test_095_tail_rows_roundtrip.cpp` holds the walk, the readback comparator, the
  presence check and their non-vacuity arms for `FIX50SP2.xml`, in an anonymous namespace of that TU.
- Every tail row's Spec ref but A-037's and A-038's (`[FIX-Latest] Trade — Order Mass Handling`)
  fails `[const §VI.2]` (v4.1): each names a category that is not the Orchestra category of its
  MsgType, or writes the section with a hyphen (`Pre-Trade`) that the clause's "identifier's words"
  rule does not produce (data-model gives the before → after table).
- `[const §XVIII.2]` lists "new MsgTypes A-035..A-065" under **v1.2 — FIX Latest application
  messages**, and `[const §XVIII.7]` closes A- rows only by naming them. Flipping the rows without an
  amendment would contradict the constitution (`[const §XVIII.4]`, `[const §XX.1]`).

## Clarifications

### Session 2026-10-28 (orchestrator, from the owner rulings above)

- Q: Which tier closes the rows? → A: Both (ruling 1). A row flips only when both of its witnesses
  pass and the cross-dictionary differential holds for its MsgType.
- Q: What is "differential verification vs the 181-message baseline" in this feature? → A: Two
  things, both named per row: the vlatest witness (typed vlatest builder against an Orchestra
  required-walk that shares no code with `dict::OrchestraLoader` or the codegen projection, 076's
  non-circularity rule), and the cross-dictionary differential (FR-008). The 181-message set-level
  baseline is 076's census and reify round-trip, cited unchanged.
- Q: Is a live cell carrying a tail MsgType needed? → A: No (ruling 2).
- Q: Which Spec ref token? → A: Keep each row's `[FIX-Latest]` token and correct its words to the
  Orchestra section and category (`[const §VI.2]` admits both tokens; keeping it changes the fewest
  cells). The coverage-index rows stay in the FIX-Latest section.

## User Scenarios & Testing *(mandatory)*

### User Story 1 — Every tail MsgType has a named, complete witness on each tier (Priority: P1)

A maintainer deciding whether a tail row may be `done` reads its Tests cell and finds two tests named
after the row, one per tier. Each builds the message with every closure-required field and group of
that tier's dictionary populated, parses it independently, and checks every value it set.

**Why this priority**: it is the closure bar, and it covers all 31 rows.

**Independent Test**: build and run the witness targets alone; all 62 witnesses pass, each named
with its row ID (hyphen dropped) and generated message name (FR-001).

**Acceptance Scenarios**:

1. **Given** a tail MsgType and a tier, **When** its witness runs, **Then** every closure-required
   field and group of that tier's dictionary (each required group with at least two instances, each
   instance with its required members and the group's delimiter) is set, built, parsed independently,
   and read back equal to the chosen value, group instances in order.
2. **Given** a witness from which one required field or group is removed, setter and intent entry
   both, **When** the suite runs, **Then** it fails with the dictionary-derived presence check's
   message.
3. **Given** a parsed value that differs from the chosen one, **When** the witness runs, **Then** it
   fails naming the path.

---

### User Story 2 — The two dictionaries agree on what each tail MsgType requires (Priority: P1)

A maintainer can see, per tail MsgType, that the Orchestra file and `FIX50SP2.xml` require the same
fields and groups, or exactly where they differ.

**Why this priority**: the version column will claim both `5.0SP2` and `FIX Latest`; this is the
check that the two claims describe the same message.

**Independent Test**: run the differential test alone.

**Acceptance Scenarios**:

1. **Given** a tail MsgType, **When** the differential runs, **Then** the two closure-required sets
   are compared by tag (fields; groups by count tag, delimiter and per-instance members) and any
   difference fails naming the MsgType, the tag and the side that has it.
2. **Given** a MsgType whose two sets are known to differ, **When** the same comparator runs on it,
   **Then** it reports the difference (FR-009).

---

### User Story 3 — The catalogue and the constitution say what shipped (Priority: P2)

The 31 rows read `done`; their Tests and Verified cells name both witnesses and the bar; their
version column reads `5.0SP2, FIX Latest`; their Spec refs conform to `[const §VI.2]`; and the
constitution no longer lists them as v1.2 work.

**Independent Test**: re-derive the 31 rows from `spec/feature-catalogue.md`, check every cited test
name exists in the tree, and check each Spec ref against the Orchestra file with data-model's
command.

**Acceptance Scenarios**:

1. **Given** the merged feature, **When** a reader opens any tail row, **Then** its Status is
   `done`, each named witness resolves to a test in the tree, and its Verified cell names L-069-1
   (fixpp#637), L-074-1 (no ApplExtID(1156) on the wire, row 4b), and QuickFIX/J 3.0.1's 0 of 31.
2. **Given** `[const §XVIII.2]` and `[const §XVIII.7]`, **When** read after the merge, **Then**
   §XVIII.2's v1.2 bullet no longer names A-035..A-065 and §XVIII.7 names them as closed by 096.

### Edge Cases

- **A required group nested in a required group's instance**, **a delimiter chain longer than one
  nested group**, or (v50sp2 leg) **a required component holding a group among siblings** → the walk
  fails loudly naming the element (095's rule, kept for both legs). The plan re-measures the tail set
  against each before tasks are written; if one fires on a tail MsgType, the implementer stops and the
  orchestrator brings it to the owner rather than widening the walk silently.
- **Vlatest leg: a required `fixr:componentRef` whose component holds only a group whose
  `fixr:groupRef` is not required** (the QuickFIX wrapper shape, written in Orchestra) → the walk
  fails loudly; this feature does not decide an inheritance rule Orchestra does not state.
- **A tail MsgType with no required field and no required group** on a leg → its witness still sets
  and reads back at least two fields (095 FR-002/FR-003), so it is not vacuous.
- **A tag both message-level and a group member of the same MsgType** → attribution is by extent, not
  first occurrence (095 FR-003/FR-004).
- **The two dictionaries differ on a tail MsgType** → the differential fails; the row does not flip
  until the owner rules (FR-012).
- **A future dictionary change adds or removes a required member** → the dictionary-derived check
  fails until the witness is updated; nothing records the derived sets as fixed numbers (FR-006).
- **The vlatest dictionary and FIX50SP2 in one `version_registry`** → not exercised: each witness
  loads one dictionary into its own parser, never a registry (L-074-1 / L-080-1 stay as they are).

## Requirements *(mandatory)*

### Functional Requirements

**Witnesses (US1)**

- **FR-001**: Each of the 31 tail MsgTypes MUST have exactly one v50sp2 witness named
  `TailRows096.<RowId>_<GeneratedName>` and exactly one vlatest witness named
  `TailRows096Latest.<RowId>_<GeneratedName>`: the catalogue row ID with its hyphen dropped, and the
  dictionary's message name.
- **FR-002**: Each witness MUST meet 095 FR-002 and FR-003 against its own tier's closure-required
  set and dictionary: every member set (required groups with at least two instances, each instance
  with its required members and the group's delimiter), at least two intent entries, values chosen
  as literals before the build, one intent list compared as an exact multiset in both directions
  against an extent-attributed enumeration of the parsed body.
- **FR-003**: The v50sp2 leg MUST use 095's walk, readback comparator and presence check with no
  change in behaviour. If they are moved out of 095's TU to be shared, the move changes no logic, and
  095's suites, including every arm of 095's contracts W6 that is a committed test, still pass.
- **FR-004**: The vlatest leg MUST derive its closure-required sets from a walk over the raw Orchestra
  file (Terminology) that shares no code with `dict::OrchestraLoader` or the codegen IR projection
  (076's non-circularity rule). Its presence check MUST meet 095 FR-004's shape: it returns the members
  it found missing, takes the MsgType from the parsed frame's tag 35, reads entries by extent, and fails
  loudly on the shapes in Edge Cases.
- **FR-005**: A census per suite MUST show that each tail MsgType has exactly one witness in it,
  independently of run order, `--gtest_filter` and `--gtest_shuffle` (095 FR-004a), with the name's
  row equal to the catalogue row of its MsgType. Each witness also asserts that its parsed tag 35
  equals the MsgType its own name maps to.
- **FR-006**: The rules and the commands that re-derive the closure-required sets MUST be stated in
  the feature's artifacts; no artifact or comment records the derived counts or member lists as a
  result (parent `.claude/CLAUDE.md`, "A comment may record a PROCEDURE; it may not record a
  RESULT"). Known-positive assertions that re-run on every invocation of their binary are the one
  exception, listed in the witness contract.
- **FR-007**: The existing 077, 076 and 095 tests MUST keep passing unchanged in behaviour.

**Cross-dictionary differential (US2)**

- **FR-008**: A test MUST compare, for each tail MsgType, the vlatest-leg closure-required set with
  the v50sp2-leg set, by tag: required fields; required groups by count tag, delimiter and the
  per-instance required members. It collects every difference before it asserts and names the
  MsgType, the tag and the side holding it.
- **FR-009**: Every output of the Orchestra walk, the differential and the two censuses MUST be shown
  able to fire, each arm demonstrated RED with its command and the message that fired (095 FR-004's
  rule, its contracts W6 table extended in this feature's witness contract). The non-circular known
  positives include:
  - the Orchestra walk's **field** leg equals QuickFIX-cpp's measured message-level required set,
    `tools/quickfix_required_golden/golden.csv`, for every tail MsgType, read at test time;
  - the differential reports a difference on a MsgType where the two files are known to differ
    (the plan selects it by measurement and the witness contract records how to re-derive it);
  - a mutant that removes one required member from one vlatest witness, setter and intent entry
    both, goes RED with the presence check's message.

  Where neither engine's measured output can stand as a positive for a leg (095 FR-004 records why
  for the group and per-instance legs), the arms are deficient frames and inline synthetic Orchestra
  fragments fed to the same walk entry point; this is a stated limit, not a gap closed by measurement.

**Catalogue, constitution and records (US3)**

- **FR-010**: `.specify/constitution.md` MUST be amended, v4.1 → v4.2, before the catalogue edits
  (`[const §XX.1]`):
  - `[const §XVIII.2]`'s v1.2 bullet no longer names "new MsgTypes A-035..A-065"; it keeps the EP-level
    field additions. Its annotation's statements about ApplExtID(1156)=303 are row 4b's and stay as
    they are;
  - `[const §XVIII.7]` names A-035 … A-065 as closed by feature 096 on both `fixpp::v50sp2` and
    `fixpp::vlatest`, and states that their MsgTypes
    are application messages of the vendored `FIX50SP2.xml` and of the Orchestra file. Its opening
    sentence, which names the typed namespaces, is unchanged (data-model §F.2 gives the reason).
  The plan classifies the version bump under `[const §XX.4]`. The exact before → after text, the
  Status line and the Sync Impact Report are data-model's. The Status line and the Sync Impact Report
  read PENDING until the scoped Gate A rounds on the amended clauses converge, Gate B converges and
  the owner signs off; the flip to RATIFIED is the last commit before merge (095 FR-021's procedure).
  The PR is titled in `[const §XX.2]` form.
- **FR-011**: The 31 tail rows MUST move to `done`, each with:
  - FIX version(s) `5.0SP2, FIX Latest`;
  - a Spec ref in `[const §VI.2]`'s message-level form, `[FIX-Latest]` followed by the words of the
    Orchestra section and category of its MsgType (data-model has the table and its re-derivation;
    A-037's and A-038's already read so and stay byte-unchanged);
  - `/specify` `096-fix-latest-tail-evidence`, and the PR;
  - a Tests cell naming both witnesses and the differential;
  - a Verified cell stating the bar, that `5.0SP2` means the vendored Extension-Pack dictionary
    (QuickFIX-cpp's file; absent from QuickFIX/J 3.0.1's base edition), that row 2's live FIXT cell is
    the tail's one live cell and carries none of the 31, L-069-1 (fixpp#637) and L-074-1.
- **FR-012**: No production source changes (`src/`, non-generated `include/`, `tools/codegen/`). If a
  witness or the differential exposes a defect or a dictionary disagreement, it is filed as an issue
  and brought to the owner before the affected row flips. A row that does not flip keeps its
  catalogue and coverage-index text as on `origin/main`, the §XVIII.7 after-text that names it changes
  (another scoped Gate A round), and the success criteria are read over the rows that flip.
- **FR-013**: `spec/coverage-index.md` MUST name each tail MsgType's witnesses in its existing
  FIX-Latest row (no new row), and correct, with a comment naming its superseder in place (the parent
  repo's issue #310 rule), the FIX-Latest section's scope line, its 074 link paragraph ("stay
  `backlog`"), and the Post-1.0 Gap Registry's reference to A-035–A-065.
- **FR-014**: `spec/behaviors-and-limitations.md` (the live file) MUST widen L-069-1 to every
  builder-bearing namespace (`fixpp::v42`, `fixpp::v44`, `fixpp::v50sp2`, `fixpp::vlatest`), naming
  fixpp#637, and record anything a witness or the differential exposes.

### Key Entities

- **Tail row**: catalogue ID, its MsgType, version column, Spec ref, Tests/Verified cells.
- **Closure-required set (per leg)**: per MsgType, required fields and required groups with
  delimiter and instance members, derived from one dictionary by that leg's rules.
- **Witness**: per MsgType and tier, its intent list and the bidirectional comparison against the
  parsed body.
- **Differential finding**: MsgType, tag, path, and the dictionary that holds it.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: 31 of 31 tail MsgTypes have a passing named witness on each tier (62 witnesses); 0 are
  left with an unset closure-required member according to that tier's dictionary-derived check.
- **SC-002**: The cross-dictionary differential reports 0 differences over the tail set, or every
  difference is filed and ruled on by the owner before its row flips (FR-012).
- **SC-003**: Every non-vacuity arm of FR-005, FR-008 and FR-009, and the records check's own RED
  run, has been run and recorded RED with its command and the message that fired.
- **SC-004**: 31 of 31 tail rows read `done` on `main` after merge. The `[const §I.3]` count of OFFICIAL
  rows that are neither `done` nor `dropped` drops by 31 between the merge-base and the head, and the
  only rows whose Status differs are the 31, re-derived with 094's catalogue census.
- **SC-005**: No file under `src/`, non-generated `include/` or `tools/codegen/` changes.

## Assumptions

- Both vendored dictionaries are the dictionaries of record for their tier. `FIX50SP2.xml` is
  byte-identical to QuickFIX-cpp v1.16.0's; quickstart re-checks it with `cmp`.
- The local verify gate follows 095's precedent (linux-clang-debug locally; the other presets to CI),
  confirmed at plan time against the measured build cost of the vlatest witness TU.
- The owner rulings quoted in Context are settled and not re-opened by this feature.

## Out of Scope

- `ApplExtID(1156)=303` on-wire differentiation and the `version_registry` re-key (row 4b, L-074-1,
  L-080-1). Every tail row discloses it.
- Builder-side enum-domain validation (fixpp#637), and `validate_<Msg>` enforcing component-required
  groups (095's out-of-scope item, QuickFIX-cpp parity).
- A live interop cell carrying a tail MsgType; a QuickFIX/J route for the tail set.
- Back-porting FIX Latest EP-level field additions into the legacy dictionaries (stays v1.2).
- A CI lint of `[const §VI.2]` and the `[const §IX.6]` catalogue check (fixpp#626); Spec refs of rows
  outside the 31.
- Moving the 31 coverage-index rows into the "FIX Application Layer (FIX40–FIX50SP2)" section.

## Normative References

- **Section entries** (coverage index, "General Encoding Rules (common to all app versions)"; the
  same three 095 cites):
  - `[FIX50SP2 §3.2] Repeating groups (NoXxx delimiter, ordered field list, nested groups)` — the
    delimiter and instance rules and every group witness.
  - `[FIX50SP2 §3.3] Field data types (28 types: STRING, CHAR, INT, FLOAT, BOOLEAN, UTCTIMESTAMP,
    etc.)` — the literal rules.
  - `[FIX50SP2 §3.1] Standard header (BeginString/8, BodyLength/9, MsgType/35, mandatory
    ordering)` — the framing.
- **Message-level entries** (`[const §VI.2]`/`[const §VI.5]` form): the 31 rows' Spec refs as they
  read after FR-011 (data-model's table). They do not stand in for the section entries above.
- **The coverage-index entries**: section "FIX Latest — MsgType entries only (FIX-Latest)", one row
  per tail MsgType (`| MsgType | Message | Catalogue ID | … |`).
- **Constitution:** `[const §I.3]` the tag gate; `[const §VI.2]`, `[const §VI.4]`, `[const §VI.5]`,
  `[const §VI.6]` Spec refs, coverage index, Normative References, closing a `done` row;
  `[const §VII.8]` grouped tests and label selection; `[const §XVII.1]`, `[const §XVII.7]`,
  `[const §XVII.8]` Gate A, local build, verify; `[const §XVIII.2]`, `[const §XVIII.4]`,
  `[const §XVIII.7]` the roadmap and v1.0 codegen scope, amended by this feature (FR-010);
  `[const §XX.1]`–`[const §XX.4]` the amendment process.
- **B&L:** L-069-1 (widened, FR-014), L-074-1 and L-080-1 (disclosed), L-067-1 (the AND composition
  for fields), B-095-1 (row 2's live cell).
- **Precedent:** `specs/095-tail-evidence-pass/` (spec Terminology and FR-001..FR-006,
  contracts W1–W7, data-model §B); `specs/076-fix-latest-typed-codegen/spec.md` FR-006 (independent
  walkers).
