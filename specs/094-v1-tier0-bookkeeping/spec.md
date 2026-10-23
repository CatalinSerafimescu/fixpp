# Feature Specification: v1.0 Tier-0 bookkeeping — catalogue reconciliation, DECISION 1, SPDX marking

**Feature Branch**: `094-v1-tier0-bookkeeping`

**Created**: 2026-10-22

**Status**: Draft

**Input**: User description: "v1.0 Tier-0 bookkeeping (070-style, [const §VI.6]): a docs-only feature
that (1) moves W-015, W-016, SVC-001..005 to `dropped(post-v1.0, planned, not abandoned: <reason>)`
per owner DECISION 1, adds the catalogue legend line, and amends the constitution so it no longer
requires a v1.0 service wrapper; (2) flips 18 catalogue rows to `done` as a reconciliation citing the
delivering PRs and verifying tests; (3) marks every `src/`/`include/` C/C++ file AGPL-3.0-or-later."

## Terminology

- **Reconciliation flip** — a catalogue row whose behaviour already shipped in earlier PRs, moved to
  `done` by this feature with its cells citing those PRs and tests. It is not a delivery claim. The
  precedents are D-002 (070's bookkeeping flip) and D-010 (083: "stated as a reconciliation, not as a
  delivery claim").
- **Post-v1.0 drop** — the status form `dropped(post-v1.0, planned, not abandoned: <reason>)`. It
  takes a row out of the v1.0 tag gate (`[const §I.3]`'s done-or-dropped rule) and nothing more: the
  row stays planned and returns to `backlog` when its release opens. A row with no release yet (the
  unscheduled service wrapper's SVC-001..005, and NFR-019) returns when one is assigned to it; for the
  service wrapper that assignment is itself an `[const §XVIII.4]` amendment.
- **Residual on record** — a part of a row's original wording that the reworded row no longer
  claims, and that is named in its Verified cell as a live B&L row or an open issue.
- **Narrowing not required** — a part of a row's original wording that the reworded row no longer
  claims and that FIX / FIX-TC does not require. By the owner's rule (Context) it needs no record;
  its Verified cell says what is no longer claimed. FR-006b classifies every narrowing of the 18 flips
  as one or the other.

## Context: why this feature exists

The ★ V1.0 FUNCTIONAL TRACK (parent `REMAINING-WORK.md`, owner 2026-10-21) makes this PR its step 1:
the cheapest movement towards the v1.0 tag, with no behavioural code. Its inputs are all decided:

- **DECISION 1 (owner, 2026-10-21):** the seven rows W-015 (FIXP), W-016 (SOFH) and SVC-001..005
  (gRPC control plane, iceoryx2 data plane, data-plane opt-in, health, pluggable `ControlPlane`) leave
  the v1.0 tag gate, and **none is abandoned**. The owner's words: *"We don't want to drop them
  forever, we want them to implement post-v1.0 … as long we don't permanently drop them and we don't
  get back to a debate about saying that we 'dropped' them today."* This ruling is settled. Both
  reason strings are the owner's wording in DECISION 1. The SVC reason's "owner decision 2026-07-15"
  is the ★ scope decision of that date (parent repo (private) `REMAINING-WORK.md`), which kept the
  service wrapper out of v1.0 as a deferral; a deferred row still counts as open under `[const §I.3]`,
  and DECISION 1 is the user-signed rationale that turns that deferral into the post-v1.0 drop the
  legend names.
- **Row-by-row verification at fixpp `9b176090`**
  (`research/G19-fix-fpml-iso20022/research/findings/v1-tier0-flip-verification-2026-10-21.md`): 4 rows
  flip as written, 11 flip once reworded to what shipped, 4 needed an owner call, and 9 are NOT
  delivered.
- **Owner rulings (2026-10-21):** all 11 rewordings approved. NFR-002 is reworded and flipped. NFR-013
  is split, with io_uring becoming a new post-v1.0 row. TC-017 is not supported by design and is
  flipped. NFR-010 is postponed and stays `backlog`. The narrowing rule, in the owner's words (parent
  repo (private) `REMAINING-WORK.md`, the 11 reworded rows): *"a row is narrowed to what shipped only
  where the removed part is not required by FIX / FIX-TC, or is on record (live B&L row or open issue)
  and named in the row's Verified cell."*
- **Owner rulings (2026-10-22):** the service wrapper goes on the post-1.0 roadmap as **unscheduled**,
  with no version slot. The 16 `Apache-2.0` transport files are **relicensed to AGPL-3.0-or-later** in
  this PR. Their Apache identifier was inherited from 012's contract templates, not chosen. After
  Gate A round 1: the control-plane interface goes post-1.0 with its default implementation, the
  amendment is MAJOR (v4.0), the PR is titled in `[const §XX.2]` form, and a `conanfile.py` comment
  edit for #607 rides along (§Clarifications, Session 2026-10-22 (Gate A round 1)). After Gate A
  round 2: a `[const §VI.6]` clarification for process rows joins the v4.0 amendment, TC-017 gains two
  session-level test cells, and the `conanfile.py` comment is reworded (§Clarifications, Session
  2026-10-22 (Gate A round 2)).

`main` at `9b176090` still states the dropped scope as v1.0 in the constitution:
- `[const §XVIII.1]` "v1.0 scope is locked: … the gRPC service wrapper, iceoryx2 SHM data plane (opt-in)".
- `[const §XIV.1]` "each with one default impl in v1.0: … Control plane (default: gRPC …)".
- `[const §XIV.3]` "The control plane (gRPC) works without it".
- `[const §IV.4]` "Service wrapper (`service/`) is opt-in. gRPC is the control-plane transport".

As long as these clauses stand, dropping the rows would leave the constitution requiring something
the catalogue no longer schedules. `[const §XX.1]` says to amend the article first, in the same PR.

## Clarifications

### Session 2026-10-22 (owner)

- Q: Where does the service wrapper sit on `[const §XVIII.2]`'s locked post-1.0 roadmap? → A:
  **Unscheduled post-1.0.** One line under §2. No version slot, and v1.1–v1.8 are not re-ordered.
- Q: The 16 `Apache-2.0` transport files? → A: **Relicense to AGPL-3.0-or-later in this PR.**
- Q: `conanfile.py` says `AGPL-3.0-only`, while the marked files and `pyproject.toml` say `-or-later`.
  Which is intended? → A: **`AGPL-3.0-or-later`; fix `conanfile.py` in this PR.** The owner was told
  that the edit misses the Conan cache on every CI lane once, and accepted that cost.
- Q (resolved without the owner, by `[const §XX.2]`): the owner's brief said "short spec + Gate B".
  Article XX §2 requires a Codex Gate A review on **every** amendment, so this feature runs `/gate-a`
  on the bundle too. It is a governance requirement, not a choice of scope.

### Session 2026-10-22 (Gate A round 1)

Owner rulings made after the round-1 reviews (Codex, and the Opus adversarial judge). Binding.

- Q: DECISION 1 said "the control plane stays a pluggable interface". In source, the installed
  `include/fixpp/service/control_plane_factory.hpp` is an empty base class with no operations, no
  `fixpp::service::ControlPlane` type exists, and nothing in `src/` reads
  `EngineConfig::control_plane_factory`. What does `[const §XIV.1]` say? → A: **The control plane —
  both the pluggable interface and its default gRPC implementation — is post-1.0, with SVC-005.**
  v1.0 installs only the `ControlPlaneFactory` ABI anchor and the C-ABI codes 900/901, with **no
  behavioural commitment**. The silently ignored `EngineConfig::control_plane_factory` gets **both** a
  new labelled fixpp issue (filed at implement time by the orchestrator, FR-018) **and** a new live
  limitation in `spec/behaviors-and-limitations.md`, L-094-1 (FR-017). So the B&L file is no longer
  unchanged.
- Q: MINOR (v3.1) or MAJOR? → A: **MAJOR, constitution v3.0 → v4.0, with a `CHANGELOG.md` entry.**
  The reason is the one the adversarial review confirmed: `[const §XVIII.5]`'s no-early-shipping rule,
  under the hypothetical-PR test of the v1.0, v2.0 and v3.0 Sync Impact Reports (research R-2). The
  amendment's status reads **PENDING** until Gate A converges and the owner signs off, following the
  constitution's PENDING/RATIFIED precedent.
- Q: A MAJOR amendment has always been a standalone PR. Record a new §XX.2 deviation, or change the
  PR? → A: **Title the 094 PR in `[const §XX.2]` form**:
  `Constitution: amend §IV.4, §XIV.1, §XIV.3, §XVIII.1, §XVIII.2 — service wrapper post-1.0 (+ 094 Tier-0 bookkeeping)`.
  There is then no §XX.2 deviation to record (FR-003c). *(Round 2 adds §VI.6 and the `(v4.0)` suffix to
  this title; FR-003c holds the current form.)*
- Q (from a peer session, batch B58 / #607): any `conanfile.py` edit misses the Conan cache once. →
  A: **In this PR, as a separate commit referencing `Refs #607` with no closing keyword,** replace the
  two comment lines above `tool_requires = []` with the text in data-model §G. B58 moves
  SWIG off apt, and both `conanfile.py` edits then ride one cache-key move (FR-013b). *(The text was
  reworded at round 2, below.)*

### Session 2026-10-22 (Gate A round 2)

Owner rulings made after the round-2 reviews (Codex, and the Opus adversarial judge). Binding.

- Q: NFR-002 is a process obligation with no verifying test, and `[const §VI.6]`(b) asks for
  "verifying tests". How does it close? → A: **Fold a §VI.6 clarification into the v4.0 amendment, then
  flip NFR-002.** The clause text is data-model §E.3a: a row whose subject is a process obligation of
  the constitution, with no library behaviour under test, closes on the per-feature records that
  obligation names, and the exception does not relax (b) for any row that describes behaviour. It is a
  loosening; the MAJOR classification still rests on `[const §XVIII.5]` (research R-2). The PR title
  gains §VI.6 and `(v4.0)` (FR-003c).
- Q (decided by the bundle's Edge Cases rule, following the Opus review): `544-hot-path-zero-alloc-verify.md`
  assessed added lines only. → A: **The orchestrator files one labelled issue for that lapse at
  implement time, and NFR-002's Verified cell names it (FR-020).**
- Q: TC-017's in-domain ending (98=1..6) has no session-level witness. → A: **Add two session-level
  cells (FR-022)**, one acceptor and one initiator, in files already compiled into
  `session_pure_tests`, so no ctest entry is added. Each asserts the session ends Disconnected and no
  frame was sent, and each is proven RED. The implementer writes them, not the orchestrator. The stale
  initiator-arm comment "(with Logout in US4)" in `src/session/session.cpp` is recorded as a P3 note and
  not edited (plan.md §Gate A), because a non-licence line in `src/` would break SC-005.
- Q: the FR-013b `conanfile.py` comment stated B58's end state, which is false on `main` until B58
  lands. → A: **Reworded to the four-line text in data-model §G**, still its own commit with
  `Refs #607` and no closing keyword.
- Defaults applied, so no question stays open (the Opus review's recorded defaults):
  - coverage-index's "Cross-cuts forwarded" note (T-039/T-041 wait on 2j's `ReloadCertSource`): informational. T-039, T-040 and
    T-041 already read `done`, so the note has no tag-gate effect. Reported, not edited.
  - The "AGPL-3.0" licence prose (`README.md`'s licence badge and its "Open source" / "Commercial" bullets, `NOTICE`,
    `LICENSE-COMMERCIAL.md`'s opening sentence): left as it is and reported. It is not package metadata.
  - The 40 `Apache-2.0` files under `tests/` and `bench/`: the orchestrator files one labelled
    follow-up issue at implement time (FR-021). They stay out of scope here.

### Session 2026-10-22 (Gate A round 3)

Owner approvals given after the round-3 reviews (Codex, and the Opus adversarial judge). Binding.

- Q (Opus round 3, N3-1): "the per-feature records that obligation names, showing it applied in full"
  in the §VI.6 sentence (data-model §E.3a) can be read as every record of that kind, including one that
  did not apply the obligation in full. Reword? → A: **Approved.** The sentence reads "per-feature
  records of the kind that obligation names, cited in the row, each showing it applied in full", in
  §E.3a and wherever the bundle quotes it (the SIR's Article VI §6 entry, the CHANGELOG entry, FR-003,
  research R-13). The round-2 entry above records the ruling as it was then worded.
- Q (Opus round 3, N3-2): the FR-013b comment's "Restore in Phase 5." points at a phase that is partly
  done and partly post-1.0. Replace it with the FR-023 issue? → A: **Approved.** The comment's last
  line ends "Restore: fixpp#619.", filled with the FR-023 issue's real number at implement
  (data-model §G). The comment stays four lines.

### Session 2026-10-22 (checklist audit)

Owner rulings made on the two items the step-9 checklist audit escalated. Binding.

- Q (licence checklist CHK020): the FR-013b comment's line 1 says "cmake and ninja come from the
  system (apt in CI)", but no workflow apt-installs cmake (it is preinstalled on the runner image) and
  Windows CI installs ninja with choco. Reword? → A: **Reword line 1** to
  "# cmake and ninja come from the system (runner image, apt or choco in CI); swig is installed by each".
  The rest of the comment is unchanged, so it stays four lines (data-model §G). It is still the C-607
  commit's text, with `Refs #607` and no closing keyword. It is not clause text, so Gate B covers it.
- Q (governance checklist CHK017): Article XIV §3's converged after-text keeps "The control plane
  (gRPC) works without it." as a present-tense sentence, while Articles IV §4 and XIV §1 place the
  control plane post-1.0. Change it? → A: **Apply the clause edit, and run a scoped Gate A round on
  it.** The last sentence becomes "When the service wrapper is built, its gRPC control plane works
  without it." (data-model §E.5; scoped Gate A round 1 then refined it to "…works without iceoryx2 SHM.", Codex's counter-proposal for the ambiguous "it"). Under FR-003d this is amendment text no Codex round has seen, so
  the orchestrator runs a scoped Codex Gate A round, with an Opus judge, on that line next, and the
  owner signs off on the reviewed text (tasks T061). The RATIFIED templates name the scoped rounds
  (data-model §E.1's scoped-round variant, in force). Done: two scoped rounds ran and converged on
  2026-10-22 (Codex P1=0 P2=1 P3=0, then P1=0 P2=0 P3=0; Opus judge: clause converged; plan.md
  `## Gate A`).

### Session 2026-10-22 (US2 evidence)

Rulings on the findings of the T030 evidence run (verify record, `## US2 evidence (T030)`), made
before any catalogue flip. Binding.

- Q (A-1, D-001): no cited cell asserts that the loaded FIX 4.2 dictionary holds every standard field,
  or the admin messages beyond A, 0 and 3, and FR-005 keeps D-001's Title. Narrow, cite the codegen
  byte pins, add a cell, or withhold? → A (owner): **Add a census cell.** FR-022b specifies it
  (`DictionaryCensus.Fix42AllMessagesAndFieldsLoaded`); D-001's Tests cell cites it, and D-001 flips
  only after the cell is GREEN and proven RED (tasks T029a).
- Q (A-2, TC-015): FIX-TC 15 asks for "more than one message of the same type with header and body
  fields ordered differently" and that "subsequent messages' MsgSeqNum(34) are accepted"; the one
  cited cell feeds a single message. Narrow, add a cell, or withhold? → A (owner): **Narrow.** The
  Title and Verified cell say that one message with reordered header and body fields is accepted
  (B-005-7), with no multi-message or later-MsgSeqNum claim. Scenario 15 is optional, so the removed
  half is "Not required by FIX / FIX-TC" under FR-006b, like TC-017's 17b and 17c–17j.
- Orchestrator correction (B-1, NFR-011; an overclaim fix, not a narrowing of a Title or version
  clause): the Verified cell's "(every byte from the resource)" for XmlLoader becomes "(the loaded
  Dictionary's allocations land on the supplied resource; no cited cell asserts that none reach the
  global heap)", because `PmrAllocation.EveryByteFromMr` asserts only that allocations reach the
  resource.
- Orchestrator correction (B-2, NFR-005; an overclaim fix): "the if: of the three #448 steps" becomes
  "the if: of the two #448 gate steps in tier1.yml, which a third step asserts ran on exactly those
  presets", because the third step runs under `if: always()` and checks the preset in its script.

### Session 2026-10-23 (Gate B round 1)

Owner rulings on the Gate B round 1 triage (parent repo (private)
`research/G19-fix-fpml-iso20022/research/reviews/opus_pr620_1_triage.md`). Binding.

- Q (Q1, Codex C-1): the eleven feature-owned rows SVC-001..004, NFR-001, NFR-002, NFR-003, NFR-005,
  NFR-011, D-003 and D-007 have no `spec/coverage-index.md` entry, and the `[impl]`/`[constitution]`
  ones are rows `[const §VI.3]` says are noted as design choices. Record and file, fill them here, or
  both? → A (owner): **Fill the eleven here** (option (b)). data-model §D carries the text ("Gate B
  round 1 supplementals"), FR-011 names them, and plan.md's §VI.3 row names the notes 094 adds. No
  issue is filed.
- Q (Q2, Codex C-4): D-008's Title keeps "zero-runtime-cost field lookup", and no cited cell
  witnesses a lookup over the generated code. Narrow the Title, add a discriminating witness, or
  withhold D-008? → A (owner): **Keep the clause and add the witness** (option (b)).
  - Orchestrator definition (written after the ruling; superseded by Gate B round 2, below): the
    property is 2c's: a generated typed accessor resolves its standard field's tag and value type at
    compile time and never consults a runtime dictionary or table. FR-022c specifies the cell;
    D-008's Tests and Verified cells name it, and data-model §H maps the clause to it.
  - Round-1 fixer's boundary (`ca483a38`, written after the ruling; superseded by Gate B round 2,
    below): FR-022c reads the property over the accessor's own generated text, and names under
    "Boundary" the callees' runtime work, which may call the session dictionary and which the cell
    does not cover.

### Session 2026-10-23 (Gate B round 2)

Owner ruling on the Gate B round 2 triage (parent repo (private)
`research/G19-fix-fpml-iso20022/research/reviews/opus_pr620_2_triage.md`). Binding.

- Q (Q1, Codex C-1 and C-2): at round 1 the clause was kept on the basis that no generated lookup
  exists. But the generated accessors find each value at runtime (`OffsetTable::find` for a message
  field, a scan of the entry for a group-entry field), group accessors ask the session's runtime
  dictionary for group membership on their first call, the witness kept the Title only through a
  boundary written after the ruling, and its design anchor quoted a banned pattern (`[const §XV.13]`)
  as the property. What should D-008 claim? → A (owner): **Narrow + strengthen census**
  (option (a)). Option (a)'s Title, as the triage worded it: `Code-generated constexpr field metadata
  from data dictionary — each typed accessor binds its field's tag at compile time, with its value
  type fixed in its generated text`.
  - Applied by the Gate B round 2 fixer (`655b3d5b`, `de2f01c4`), from the triage's edit list for
    option (a): the value type is stated as the one the codegen's data-type mapping assigns, not
    necessarily the field's correct type (MultiCharValue → char, fixpp#621); FR-022c's census gains a
    name-identity check and a mapped-type check, and the Verified cell states the accessor path's
    runtime work; FR-006 and FR-006b record the narrowing, data-model §C and §H carry the text, and
    data-model §F.7 annotates `.specify/2c-codegen.md`'s two quotes of the old Title.

### Session 2026-10-23 (Gate B round 3)

Owner ruling on the Gate B round 3 triage (parent repo (private)
`research/G19-fix-fpml-iso20022/research/reviews/opus_pr620_3_triage.md`). Binding.

- Q (Q1, Codex C-1 and C-2): D-008's identity check reads each field's name through the loader the codegen also
  uses, and only FIX 4.2's loaded names are checked against the XML (FR-022b); D-003's cells check headline messages,
  not every message and field. Extend FR-022b's census to FIX 4.4, FIX 5.0SP2, FIXT 1.1 and FIX Latest, disclose the
  boundary, or withhold D-003? → A (owner): **(a) Extend the census.**
  - Applied by Codex: one shared helper drives four new `DictionaryCensus.*AllMessagesAndFieldsLoaded` cells, and
    every census also checks `field_name(number) == name` against its source XML.

### Session 2026-10-23 (Gate B round 4)

Owner ruling on the Gate B round 4 triage (parent repo (private)
`research/G19-fix-fpml-iso20022/research/reviews/opus_pr620_4_triage.md`). Binding.

- Q (Q1, Codex C-1): TC-012's Title says PossResend(97)=Y reaches the application "with the flag
  intact", but `InboundPossResendTest.DeliverInSeq_97Y` asserts only that tag 97 is present, and it
  passes with `97=N`. Add a value assertion, narrow the Title, or withhold TC-012? → A (owner):
  **(a) Add the value assertion.** FR-022d specifies it; SC-005 names the file.
- Applied by Codex: FR-022d strengthens TC-012's existing witness with the owner-approved value
  assertion.
- Orchestrator correction (Opus round 4, N-1; a missing citation, not a narrowing): TC-014's
  "ResetSeqNumFlag(141)=Y" clause was witnessed outbound by presence only. Its Tests cell and §H row
  add `Reset.InitiatorResetLogon_Advertises1` (FIX.4.4), which asserts the emitted value is `Y`.
- Applied by Codex: the TC-014 citation correction is applied on every surface named by the fix queue.

## User Scenarios & Testing *(mandatory)*

The "users" are the owner tagging v1.0 and any contributor who reads the catalogue or the
constitution to decide what v1.0 owes.

### User Story 1 — The tag gate stops counting rows nobody will build for v1.0 (Priority: P1)

The owner counts the catalogue's open OFFICIAL rows to see what blocks the v1.0 tag. The seven
post-v1.0 rows no longer count as open. Each one still says it is planned and why it was deferred,
and the constitution no longer requires a v1.0 service wrapper.

**Why this priority**: DECISION 1 is the only owner-signed scope change on the track. Until it is
written down, the rows block the tag although nobody intends to build them for v1.0.

**Independent Test**: Read the seven rows, the catalogue legend and the amended articles. Run the
open-row census (research R-4) before and after the change: the after-count drops by exactly seven
for these rows, and no other row changes.

**Acceptance Scenarios**:

1. **Given** the catalogue on this branch, **When** a reader looks up W-015, W-016 or SVC-001..005,
   **Then** each row's Status reads `dropped(post-v1.0, planned, not abandoned: <reason>)` with the
   owner's reason, and its other cells are unchanged.
2. **Given** the catalogue header, **When** a reader looks up the Status legend, **Then** a line
   defines the post-v1.0 drop form: excluded from the v1.0 tag only, still planned, back to `backlog`
   when its release opens.
3. **Given** the amended constitution, **When** a reader looks for a v1.0 obligation to ship the gRPC
   control plane, the iceoryx2 data plane or the service wrapper, **Then** none remains. §XVIII.2
   lists the service wrapper as unscheduled post-1.0, and §XIV.1 places the control-plane interface
   and its default implementation post-1.0, keeping only the `ControlPlaneFactory` ABI anchor and the
   codes 900/901 for v1.0.
4. **Given** the installed `include/fixpp/service/control_plane_factory.hpp` and the C-ABI codes
   `FIXPP_ERR_CTRL_CONFIG` (900) / `FIXPP_ERR_CTRL_RUNTIME` (901), **When** this feature merges,
   **Then** the header is byte-unchanged (it already carries the AGPL identifier), and
   `include/fix/c_api/error.h` changes only by FR-013's licence line.
5. **Given** the live B&L file, **When** a reader looks up `EngineConfig::control_plane_factory`,
   **Then** L-094-1 says it is accepted and ignored and names the issue that tracks it.

---

### User Story 2 — Every delivered row reads `done` and says only what shipped (Priority: P1)

A contributor reads an NFR, D or TC row to learn whether fixpp provides it. Each of the 18 rows reads
`done`. Its cells cite the PRs that delivered it and the tests that witness it. Anything the original
wording promised but did not ship is named as a residual on record.

**Why this priority**: every row is a §I.3 open row today. A `done` that claims more than shipped
would be worse than `backlog`, so the rewording is the half that matters.

**Independent Test**: For each row, open its Tests cell and confirm every cited test exists and
passes on this branch (the verify record lists them). Confirm each residual its Verified cell names
is a live B&L row or an open issue.

**Acceptance Scenarios**:

1. **Given** one of the 4 clean rows (NFR-001, D-001, D-003, D-007), **When** read, **Then** its
   Title is unchanged and Status is `done`. Its /specify, PR, Tests and Verified cells cite the
   delivery, and Verified says it is a reconciliation.
2. **Given** one of the 11 reworded rows, **When** read, **Then** its Title and FIX-version cells
   carry the owner-approved wording (FR-006), and Verified names each residual on record.
3. **Given** TC-005, **When** read, **Then** it no longer claims that an inbound Reject is logged, and
   Verified names fixpp#612 (no `fromAdmin` delivery or log of an inbound Reject).
4. **Given** NFR-013, **When** read, **Then** its Title claims "ASIO async I/O transport (Linux and
   Windows)" only, its Verified cell states the backend as asio's platform default with a re-derive
   grep, and a new row NFR-019 carries the io_uring opt-in as a post-v1.0 drop.
5. **Given** NFR-002, **When** read, **Then** its Verified cell says it closes under `[const §VI.6]`(b)
   as amended in v4.0 (a process row), and names the FR-020 issue for the 544 record.
6. **Given** TC-017, **When** read, **Then** its Tests cell names the two session-level 98=2 cells of
   FR-022, and both pass on this branch.
7. **Given** D-001, **When** read, **Then** its Tests cell names FR-022b's census cell, which passes on
   this branch.

---

### User Story 3 — Every library source file says which licence it is under (Priority: P2)

A reader or a licence scanner opens any C/C++ file under `src/` or `include/` and finds
`SPDX-License-Identifier: AGPL-3.0-or-later` at the top.

**Why this priority**: NFR-010's AGPL half is independent of the postponed commercial half, and the
owner put the marking in this PR. It does not block the other stories.

**Independent Test**: A census over the tracked `src/` and `include/` C/C++ files finds every file
carrying the AGPL identifier and none carrying another. A diff filter confirms the only lines changed
in those files are licence comment lines.

**Acceptance Scenarios**:

1. **Given** the 37 files with no SPDX line at `9b176090`, **When** this feature merges, **Then** each
   starts with an AGPL-3.0-or-later SPDX comment in the comment form its language uses.
2. **Given** the 16 files marked `Apache-2.0` at `9b176090`, **When** this feature merges, **Then**
   each says `AGPL-3.0-or-later`, and its copyright line is unchanged.
3. **Given** the C-ABI freeze gate, **When** it runs on this branch, **Then** it passes against a
   manifest regenerated in this PR, and the exported C-ABI symbol set is unchanged.

---

### Edge Cases

- **A row's evidence fails at run time.** A cited test that fails or is missing on this branch means
  the row does not flip. The row stays `backlog`, the finding is recorded, and the owner is told. A
  row is never flipped on a cell nobody ran.
- **A residual is not on record.** If a removed part that FIX / FIX-TC requires is neither a live B&L
  row nor an open issue, the row does not flip until an issue is filed. A removed part that FIX /
  FIX-TC does not require needs no record (a narrowing not required, FR-006b). The rule is the owner's
  (2026-10-21, quoted in Context).
- **A Status string breaks a parser.** The drop form contains commas and a colon. Every tool that
  reads the catalogue's Status column must still parse all rows (FR-016).
- **A file is outside the census.** Generated files under the build tree are not tracked and are out
  of scope. A tracked file with a non-C/C++ extension under `src/`/`include/` is listed in the verify
  record, not silently skipped.
- **The stale coverage-index notes.** The `005-defer: recovery-dependent` notes on FIX-TC scenarios 8–11
  are false since 013 (PR #86) and S-023 (PR #90), and the "093 (partial; TC-003 stays backlog)" note
  on scenario 3 becomes false when TC-003 flips. They are rewritten in the same edit (FR-011).

## Requirements *(mandatory)*

### Functional Requirements

**Post-v1.0 drops (DECISION 1)**

- **FR-001**: W-015, W-016 and SVC-001..005 MUST carry the Status
  `dropped(post-v1.0, planned, not abandoned: <reason>)`, written exactly as data-model §A has it
  (data-model is the copy source):
  - W-015 and W-016:
    `dropped(post-v1.0, planned, not abandoned: new wire format, not core tag-value FIX, no QuickFIX reference implementation)`;
  - SVC-001..005:
    `dropped(post-v1.0, planned, not abandoned: service wrapper deferred by owner decision 2026-07-15; src/service/ is a stub)`
    (no backtick span inside the cell).

  Every other cell of these rows MUST stay unchanged.
- **FR-002**: The catalogue's Status legend MUST define the post-v1.0 drop form. It excludes the row
  from the v1.0 tag only, the row stays planned, and it returns to `backlog` when its release opens
  (a row with no release yet returns when one is assigned to it; Terminology). The wording MUST NOT
  read as abandonment. The legend MUST name the user-signed rationale `[const
  §I.3]` requires: DECISION 1, and the NFR-013 split ruling. The definition is appended to the
  existing Status line so no catalogue line moves.
- **FR-003**: The constitution MUST be amended so no clause requires a v1.0 service wrapper, gRPC
  control plane or iceoryx2 data plane, and so `[const §VI.6]` says how a process row closes:
  - `[const §XVIII.1]` drops them from the locked v1.0 scope.
  - `[const §XVIII.2]` gains one line: service wrapper (gRPC control plane, iceoryx2 data plane),
    unscheduled post-1.0.
  - `[const §XIV.1]`: the lead-in excepts the control plane, and the control-plane bullet places both
    the pluggable interface and its default gRPC implementation post-1.0 (owner, 2026-10-22). v1.0
    installs only the `ControlPlaneFactory` ABI anchor and reserves the codes 900/901, with no
    behavioural commitment.
  - `[const §XIV.3]` and `[const §IV.4]` get the same post-1.0 note.
  - `[const §VI.6]`(b) gains the process-row clarification (owner, Gate A round 2): a row whose
    subject is a process obligation of the constitution, with no library behaviour under test, closes
    on per-feature records of the kind that obligation names, cited in the row, each showing it
    applied in full; the exception does not relax (b) for any row that describes behaviour.
  - The exact before → after text of all six clauses is data-model §E.
  - Any further clause the FR-004 sweep finds naming the same scope as v1.0 is amended too. The sweep
    found none: §I.2, §IV.2, §XIII.3 and §XVIII.5 are read and kept (research R-12).
- **FR-003a**: The amendment MUST carry a Sync Impact Report (`[const §XX.3]`) with: the date, the
  version bump and an argued `[const §XX.4]` classification, the clauses changed, the rationale, the
  affected rows and specs, the review and sign-off, and the feature and PR it rode. The
  classification is **MAJOR, v3.0 → v4.0** (owner, 2026-10-22; research R-2), so `CHANGELOG.md` MUST
  gain the entry in data-model §E.8. The Sync Impact Report's header, the Status line and the
  CHANGELOG heading MUST read PENDING / `Unreleased` until Gate A converges, Gate B converges and the
  owner signs off, in that order (the owner signs off on a packet that carries Gate B's rounds, and the
  flip records them; FR-003d); the flip to RATIFIED and the dated heading MUST be the last commit
  before merge. The report MUST
  record that the first draft classified itself MINOR and was corrected at Gate A round 1, and that
  the §VI.6 clarification is a loosening that leaves the MAJOR classification resting on
  `[const §XVIII.5]`.
- **FR-003b**: The installed `include/fixpp/service/control_plane_factory.hpp` and the C-ABI codes 900
  and 901 MUST be kept as shipped. Removing a frozen code would be a break for nothing.
- **FR-003c**: The 094 PR MUST be titled in `[const §XX.2]` form,
  `Constitution: amend §IV.4, §VI.6, §XIV.1, §XIV.3, §XVIII.1, §XVIII.2 — service wrapper post-1.0, process-row closure (v4.0) (+ 094 Tier-0 bookkeeping)`,
  with the `(v4.0)` suffix PRs #451 and #500 carry, and its description MUST state the change, the
  rationale and the affected catalogue rows and specs (owner, 2026-10-22). No §XX.2 deviation is
  recorded anywhere.
- **FR-003d**: After Gate A's convergence, the amendment's text changes only under these rules (plan.md
  `## Gate A`, "After convergence"):
  - Any change to the amended **clause** text (data-model §E.3–§E.7: §IV.4, §VI.6, §XIV.1, §XIV.3,
    §XVIII.1, §XVIII.2), whoever proposes it (the owner at sign-off, a Gate B finding, the checklist
    audit), MUST get a scoped Codex Gate A round on the changed lines, with an Opus judge, under the
    owner's own `/gate-a` invocation (`[const §XX.2]`, `[const §XVII.4]`), before the RATIFIED flip;
    the owner then signs off on the text that round reviewed.
  - A change to the **amendment's records** amends no rule and needs only Gate B's re-check: the
    Status line, and all of the Sync Impact Report and the `CHANGELOG.md` entry outside the six clause
    bodies (status and review-tally wording, the affected-rows and per-article summary lines, the
    RATIFIED templates; data-model §E.1, §E.2, §E.8). A summary line must still agree with the clause
    it summarises; the §VI.6 summary bullets also carry the owner's round-3 N3-1 wording. Catalogue, coverage-index, B&L and design-doc text is not amendment text either (FR-015b
    covers a narrowing found at verify).
  - The RATIFIED flip (C-RATIFY) is written after Gate B's review, from data-model §E.1, §E.2 and §E.8's
    pre-specified text, and its evidence is the docs gates re-run on the flip head. The Gate B record
    says so and claims no review of that commit.
- **FR-004**: Every in-repo surface that states the dropped scope as v1.0, or states the old status
  or content of a changed row, MUST be updated, or annotated with a pointer to this feature. The set
  is not hand-picked: it is the output of research R-12's repository-wide recipe over every tracked
  file outside `specs/`, for (a) the dropped-scope terms, (b) all 26 changed or added row IDs and (c)
  the licence-metadata strings. The recipe MUST be proven able to fire on a known positive before its
  output is trusted, and every hit MUST carry a recorded disposition (edit, header, keep with a reason,
  other concept, pattern false positive). Every hit dispositioned "edit" has exact target text in
  data-model §D–§G. The recipe is re-run after the edits (SC-003). A surface that states such scope or
  status but that no R-12 command reaches (it carries neither a dropped-scope term nor a row ID, as
  `brain/components/nfr-and-tooling.md`'s "out-of-repo planning tracker" bullet, found by review) is in scope too once found, by review or
  otherwise: it is added to R-12's table with who found it and its disposition, and the recipe's
  blind spot is named there.

**Reconciliation flips (18 rows)**

- **FR-005**: NFR-001, D-001, D-003 and D-007 MUST move to `done`, Title unchanged. Their /specify,
  PR, Tests and Verified cells follow the verification file, and Verified states that the flip is a
  reconciliation, not a delivery claim.
- **FR-006**: The 11 reworded rows MUST move to `done` with the owner-approved wording (REMAINING-WORK
  ★ functional track, rulings of 2026-10-21; text in the verification file). The cell text is written
  exactly as data-model §C has it; data-model is the copy source, and the summaries below name what
  each row says, not its words:
  - **Version column only:** TC-003 and TC-008 take research R-3's "Column written" value
    (narrower than the owner's "4.2, 4.4 witnessed; FIXT.1.1 shares the code path" wording, by
    FR-006a).
  - **Narrowed, residual on record:**
    - D-008: its FIX version(s) cell →
      `4.2, 4.4, 5.0SP2, FIXT.1.1 (+ FIX Latest, opt-in build flag); runtime-XML-only versions: L-003-2`;
      and the Title's "zero-runtime-cost field lookup" becomes the compile-time binding of each
      accessor's tag, with its value type fixed in its generated text (owner, Gate B round 2; the
      MultiCharValue → char mapping is fixpp#621).
    - NFR-003: names #555 and the allowlisted allocation-failure cells.
    - NFR-005: zero allocation on the gated windows only, L-497-1; the PMR clause names only the parse arena and MemoryStore's retrieve() snapshot, the two surfaces a cell asserts (Gate A round 3; narrower than the verification file's wording, reported to the owner).
    - NFR-011: library surfaces only, naming L-002-4, #565 and B-495-3; "C++17" dropped from the Title.
    - TC-006: names #588 and #580.
    - TC-012: the duplicate check belongs to the application, per FIX-TC.
    - TC-014: ResetSeqNumFlag only; FIX-TC accepts either option.
  - **TC-015** (owner, 2026-10-22, §Clarifications "Session 2026-10-22 (US2 evidence)"): R-3's
    "Column written" version value, and a Title and Verified cell narrowed to one message with
    reordered header and body fields accepted (B-005-7), with no multi-message or later-MsgSeqNum
    claim (FIX-TC 15 is optional).
  - **TC-005:** Title `Session conformance: Reject (35=3) receipt — NextNumIn incremented, session continues, no Reject of a Reject (test 7)`; Verified names fixpp#612.
  - Each TC row's FIX version(s) cell is the literal value data-model §C lists for it (research R-3's
    "Column written", by FR-006a). Where only one BeginString is driven, that value is narrower than
    both the verification file's and the owner's "4.2, 4.4 witnessed" wording; FR-006a says why the
    narrower text governs.
- **FR-006b**: Every narrowing of the 18 flips is one of the two kinds the owner's rule (Context)
  allows, and the row states it: in its Verified cell, or in its FIX version(s) cell where the
  narrowing is of the versions (D-008 names L-003-2 there):
  - **On record** (FIX / FIX-TC requires the removed part, or the row names it anyway): NFR-002 (the
    544 record's lapse, the FR-020 issue); NFR-003 (#555); NFR-005 (L-497-1, #569, #565); NFR-011
    (L-002-4, #565, B-495-3); D-008 (runtime-XML-only versions, L-003-2); TC-005 (#612); TC-006 (#588,
    #580, L-019-4); TC-012 (L-022-1); every TC row's unwitnessed FIX-TC §4.1 versions (4.2, 4.4 or
    FIXT.1.1, as R-3 lists; #614).
  - **Not required by FIX / FIX-TC** (no record needed): NFR-005's SBO and its parse → validate →
    dispatch path beyond the two asserted surfaces (MemoryStore's slab, the validator scratch
    resource, the cancellable_dispatch node); NFR-011's "throughout" and OrchestraLoader's unasserted
    routing (NFR rows are `[impl]` / `[constitution]` rows, which FIX does not require); NFR-011's
    "C++17", a stale label corrected to the project's C++23, not a removed capability; NFR-003's
    allocation-failure cells listed in `ci/expected-preset-conditional-tests.txt`, which the
    sanitizer legs do not build (named in its Title; an `[impl]` CI row, which FIX does not
    require); NFR-003's allowlisted `capi_send_recv_uaf_negative` canary in
    `ci/expected-sanitizer-reports.txt` (named in its Verified cell; a deliberate test of the
    sanitizer gate itself, not a removed capability, and an `[impl]` row FIX does not require). The
    row names both in its text, which is not a record in the Edge Cases sense (a live B&L row or an
    open issue), and neither needs one; NFR-013's
    io_uring half, moved to the new row NFR-019, and its backend, moved to Verified as a design fact;
    TC-012's 19a/19b duplicate check (FIX-TC 19 assigns it to the application); TC-014's outbound
    Reset-mode option (FIX-TC 9 accepts either option); D-008's "zero-runtime-cost field lookup"
    (reading a field is runtime work; an `[impl]` row); TC-017's 17b ending and 17c–17j (FIX-TC 17 is
    optional; `[const §XII.7]`); TC-015's "more than one message of the same type" and "subsequent
    messages' MsgSeqNum(34) are accepted" (FIX-TC 15 is optional; owner, 2026-10-22); and the versions 4.0, 4.1, 4.3, 5.0 and 5.0SP1 of the old TC columns
    (outside FIX-TC §4.1's scope).
  - A narrowing found at verify is classified the same way before its row flips (FR-015b).
- **FR-007**: NFR-002 MUST be reworded to `[const §IX.1]`'s per-PR obligation: ≥95/85 on touched
  modules, or a recorded assessment for every uncovered **line/branch** the PR adds or newly makes
  reachable, checked in the per-feature verify record, not a CI threshold. It then moves to `done`
  under `[const §VI.6]`(b) as amended in v4.0 (data-model §E.3a): it is a process row, so it closes on
  the records `[const §IX.1]` names in place of verifying tests. Its evidence cells cite only records
  that assess both lines and branches, and label them parent repo (private) (research R-13). Its
  Verified cell names the FR-020 issue for the 544 record. The global ≥95 % stays a separate release
  question (row 11).
- **FR-008**: NFR-013 MUST be reworded to "ASIO async I/O transport (Linux and Windows)" and moved to
  `done`. The backend (asio's platform default: epoll on Linux, IOCP on Windows) is stated in its
  Verified cell as a design fact with a re-derive grep, not in the Title, because no test asserts the
  reactor type (data-model §C). A new row **NFR-019** (`OFFICIAL`, nfr) MUST carry the io_uring opt-in with
  Status `dropped(post-v1.0, planned, not abandoned: io_uring opt-in, to be measured in the row-16 perf
  pass)`. Before it lands, NFR-019 gets a coverage-index entry (`[const §VI.4]`).
- **FR-009**: TC-017 MUST be reworded to data-model §C's Title,
  `Session conformance (optional): EncryptMethod(98) (test 17) — 0 accepted; ≠0 refused at Logon (B-043-1); no application-layer encryption ([const §XII.7], [const §XV.10]), so 17c–17j do not apply`,
  and moved to `done`. Its Verified cell MUST name both refusal endings the source has (research R-14):
  in-domain ≠0 → Disconnected with nothing sent; out-of-domain with validation on → Reject 373=5
  RefTagID 98, not established. Ending (1) is witnessed by the two session-level cells of FR-022, and
  the Verified cell names them.
- **FR-006a**: A TC row's version column MUST name only the BeginStrings its cited witnesses actually
  drive, determined per test (fixture, helper default or instantiation list, not a file-level grep).
  The owner's "4.2, 4.4 witnessed" wording applies where both are driven. Where only one is, the
  column narrows further, because the owner's rule (narrow to what shipped) governs the wording, not
  the other way round. Each such deviation is listed in research.md, was reported to the owner
  in-session on 2026-10-22 before Gate A started, and is repeated in the owner report (tasks T053), as
  is NFR-005's PMR narrowing of Gate A round 3. The owner is told, not asked: the owner's rule (Context)
  already approves a narrowing to what shipped. The versions left unwitnessed are required by FIX-TC §4.1, so the owner's rule (narrow only
  where the removed part is not required, or is on record and named) needs them on record. They are,
  as **fixpp#614** (filed 2026-10-22, OPEN; research R-10), which every affected Verified cell names.
  The other versions the old columns named (4.0, 4.1, 4.3, 5.0, 5.0SP1) are outside FIX-TC §4.1's
  scope, and each TC Verified cell says so.
- **FR-010**: Every flipped row's cells MUST cite tests that exist literally in the tree, except the
  process row (FR-007) and FR-015's CI rows (NFR-001, NFR-003, NFR-005, NFR-013), whose cells cite CI
  or records as data-model §C states. NFR-005 and NFR-013 also cite tests, and every test they cite
  MUST exist literally. Its PR cell MUST cite the PR and squash SHA, resolved from git, not copied from
  a note. Its /specify cell MUST
  read `094-v1-tier0-bookkeeping (reconciliation; delivered by <feature or PR>)`, following D-002's
  `070-… (bookkeeping; …)` form, so that 094 is the `/specify` artifact `[const §VI.6]`(a) requires.
- **FR-011**: `spec/coverage-index.md` MUST be updated in the same edit:
  - the stale `005-defer` notes on FIX-TC scenarios 8, 9, 10 and 11 are rewritten to their current
    owners (TC-007 stays `backlog`, and its note says why);
  - scenario 3's "TC-003 stays backlog" note is replaced;
  - the 005 deferral ledger rows for scenarios 8–11 are annotated;
  - (Gate B round 1, owner Q1 (b)) the eleven feature-owned rows that had no entry, D-003, D-007,
    NFR-001, NFR-002, NFR-003, NFR-005, NFR-011 and SVC-001..004, gain a supplemental note each,
    with the exact text in data-model §D ("Gate B round 1 supplementals"): a design choice per
    `[const §VI.3]` for the `[impl]`/`[constitution]` and design-doc rows, D-003 under its FIX spec
    sections, and the SVC rows with the post-1.0 drop note.
  TC-003's "Feature 093 does not own this row … stays backlog" note in the catalogue is removed.
- **FR-012**: These rows MUST NOT change, because they are not delivered: NFR-010 (postponed), NFR-004,
  NFR-008, TC-001, TC-002, TC-004, TC-007, TC-009, TC-010 and TC-013.

**Licence marking**

- **FR-013**: Every tracked C/C++ file under `src/` and `include/` MUST carry
  `SPDX-License-Identifier: AGPL-3.0-or-later` as its first comment line: `//` in C++ files, and
  `/* … */` in C headers, matching `include/fix/c_api/log.h`. The 37 unmarked files gain the line.
  The 16 `Apache-2.0` files have their identifier replaced, and their `Copyright` line stays. No other
  byte of these files changes.
  - "C/C++ file" means a tracked file whose extension is `.c`, `.cc`, `.cpp`, `.h`, `.hpp`, `.ipp` or
    `.inl` (quickstart §3's census); any other tracked file under `src/`/`include/` is listed in the
    verify record (Edge Cases).
  - The line is line 1 in each of the 53 files: an insert goes at line 1, and each `Apache-2.0`
    identifier already sits on line 1 and is replaced in place (re-derive:
    `git ls-files src include | grep -E '\.(c|cc|cpp|h|hpp|ipp|inl)$' | xargs /usr/bin/grep -n 'SPDX-License-Identifier: Apache' | cut -d: -f2 | sort -u`
    prints `1` on `origin/main`).
  - Precondition, with a stop rule: before C-SPDX, research R-8's provenance grep is re-run over the 37
    unmarked files and its authorship census over all 53. A foreign copyright or licence notice, or a
    contributor other than the owner and the AI assistants R-8 names, means that file is not
    relabelled and the owner is told.
- **FR-013a**: Every package-metadata licence field MUST read `AGPL-3.0-or-later`, so the package
  metadata agrees with the files and with `bindings/python/pyproject.toml` (owner, 2026-10-22). The
  R-12 sweep (c) finds two that do not: `conanfile.py`'s `license = "AGPL-3.0-only"` and
  `cmake/FixppPackaging.cmake`'s `CPACK_RPM_PACKAGE_LICENSE "AGPL-3.0"` (the deprecated SPDX id for
  AGPL-3.0-only, which is the RPM's `License:` tag). Both change, and verify builds the packaging
  lane's RPM. The owner accepted the known cost: `ci/conan-cache-key.sh` hashes the whole of
  `conanfile.py`, so this PR's CI misses the Conan cache on every lane once, and its `push:main`
  reseeds the cache.
- **FR-013b**: In a **separate commit** of this PR, whose message references `Refs #607` and carries no
  closing keyword, the two comment lines above `tool_requires = []` in `conanfile.py` MUST be replaced
  with the four lines in data-model §G (owner, 2026-10-22, via a peer session; reworded by the owner at
  Gate A round 2 so it is true before and after B58, and line 1's "(runner image, apt or choco in CI)"
  reworded by the owner at the checklist audit, §Clarifications). Batch B58 / #607 moves SWIG off apt, and both
  `conanfile.py` edits then ride one cache-key move.
- **FR-014**: `tools/capi_freeze.sha256` MUST be regenerated for the 10 of the 12 frozen C-ABI headers
  that change (one licence line each); the other 2 already carry it and are unchanged.
  `tools/check_capi_freeze.sh` MUST pass. The PR body MUST state that the C-ABI change is comment-only.
  The freeze gate passing proves only that the manifest matches the headers, so the comment-only claim
  is measured on its own: quickstart §3's per-file `--numstat` and line filter over the frozen headers
  (one licence line each, nothing else), and research R-7's `libfixpp_capi` symbol diff (built at
  `9b176090` and at the head, empty) and C-ABI occupancy check. There is no C-ABI version bump: the
  versioned contract (`[const §X.1]`, `[const §X.7]`) is the C ABI's symbols, types and values, and a
  comment line changes none of them. The manifest is still regenerated, because
  `tools/check_capi_freeze.sh` makes any byte change to a frozen header a reviewed manifest edit.

**Evidence and gates**

- **FR-015**: The verify record (`.specify/decisions/094-v1-tier0-bookkeeping-verify.md`) MUST give
  each flipped row its evidence in one of three forms:
  - **Test rows** (D-*, TC-*, NFR-011): the binaries run, on the preset CI runs them on, with the
    filter, `--no-tests=error`, and the pass count read from the binary's output.
  - **CI rows** (NFR-001, NFR-003, NFR-005, NFR-013): the required jobs green on the cited `push:main`
    runs at `9b176090` (Tier 1 `39594667883`, Tier 2 `39594667975`), plus the workflow job and step
    names re-read on this branch. NFR-005 and NFR-013 also run their witnesses locally. Runs at the
    base are enough because these rows claim CI configuration (jobs, legs, steps, `if:` lines and
    required checks), which the branch re-reads unchanged, and the branch's diff outside `spec/`,
    `.specify/`, `brain/`, `specs/` and `CHANGELOG.md` is limited to the members SC-005 names, none
    of which edits a workflow job, step or `if:` line; the branch's own required tier runs must
    still be green on the C-RATIFY head before merge (tasks T065).
  - **Process row** (NFR-002, under `[const §VI.6]`(b) as amended in v4.0): the two cited verify
    records (parent repo (private)), each showing a coverage assessment of every uncovered changed line
    and every untaken branch, and the FR-020 issue's number and state.

  The verify record also snapshots `gh issue view 614` (state, labels, date), because the library
  cannot show an issue's state and every TC Verified cell depends on it, and the same for the FR-018,
  FR-020, FR-021 and FR-023 issues. A row whose evidence did not
  hold MUST stay out of the flip set (Edge Cases).
- **FR-015b**: The verify record MUST carry the claim → witness map of data-model §H in full: one row
  per clause of each flipped row's Title or version column, each mapped to the `file:line` of an
  `EXPECT`/`ASSERT`, or to a CI step's `if:` and command, that it ran or read. Every cited path MUST
  resolve in this repository or be labelled parent repo (private). A clause with no mapped assertion
  does not flip as worded: it is narrowed, and the narrowing goes into the owner report.
  - **Clause.** A clause is a part of the cell that could be false while the rest of the cell is true.
    A cell is split until no part can be; a qualifier (a version, a leg, "on the gated windows") stays
    with the claim it qualifies. Where two splits are possible, the finer one is used.
  - **Implication.** An assertion witnesses a clause it implies when the row's Verified cell states
    the implication (TC-005: an order delivered at the next number after in-sequence Rejects implies
    NextNumIn advanced over them).
  - **Verified cells are not mapped.** They carry evidence pointers, residuals and design facts. Where a
    Verified sentence names a behaviour no cited cell asserts, the cell says so (NFR-005's design facts,
    NFR-013's backend, TC-008's RefTagID 36).
  - **A narrowing found at verify** is written into data-model §C first (the copy source), classified
    under FR-006b, then copied to the catalogue. It is not amendment text, so it does not re-open Gate
    A (FR-003d); Gate B reviews it and the owner report (tasks T053) lists it. A row whose clause cannot
    be narrowed to something witnessed stays out of the flip set (Edge Cases).
- **FR-015a**: Every Verified cell MUST state its QuickFIX-interop disposition, because the catalogue
  legend's Verified rule names interop "where applicable". The disposition is "not applicable" for the
  D/NFR rows. For the TC rows it is "Witnessed in-process; the live QuickFIX interop cells skip without
  a counterparty, so they are not per-PR evidence and are not claimed." (data-model §C's literal text).
- **FR-016**: Every tool that parses the catalogue MUST still parse every row. The sweep (research
  R-5) finds two: `tools/brain_inventory.py`, and `docs/prebuild.py`, whose `_status_header` counts each
  raw Status string for the published "Status at a glance" table. The drop form cannot break it (it
  splits on `|`, and the drop strings hold none); it renders three more Status strings. It runs in
  `.github/workflows/docs.yml` on `push: main` and `workflow_dispatch` only, so it is not a PR check,
  and verify exercises it as research R-5 says. The open-row census MUST be recorded before and after
  the change, with a delta of −25 open rows (7 drops + 18 flips), +18 done, and +8 post-v1.0 drops
  (the 7 plus the new row NFR-019); the other `dropped` count is unchanged (research R-4). The census
  is first proven against a known positive. These figures, and those in SC-001, research R-4,
  quickstart §1 and tasks T038, assume no row is withheld. If k of the 18 flips are withheld under Edge
  Cases, the expected delta is −(25 − k) open and +(18 − k) done (the drops and NFR-019 do not depend
  on evidence), and the verify record names the withheld rows and states the adjusted figures.
- **FR-017**: The live B&L file MUST gain exactly one entry, **L-094-1** (data-model §F.6), in a new
  094 section at its end: `EngineConfig::control_plane_factory` is accepted and ignored (owner,
  2026-10-22). No other live entry changes, and the closed file does not change. The field is the only
  entry point: no C-ABI function, configuration key (TOML / QuickFIX CFG) or Python binding sets a
  control-plane factory. Re-derive with
  `git grep -n -i -e control_plane -e controlplane -- src include bindings ':!*.md'`: its hits must be
  only the `EngineConfig` field and its include, the anchor header itself, comments naming
  `ControlPlaneFactory` as a pattern, the Python binding's `ControlPlaneError` mapping of the codes
  900–999 (`bindings/python/fixpp.i`), and the binding tests under `bindings/python/tests/` that list
  `ControlPlaneError` among the exception names (audit, 2026-10-22). A new hit is a new entry point, and L-094-1 must name it. The Gate B record
  carries this as the feature's B&L delta: one new limitation, and no behaviour change.
- **FR-018**: Before the L-094-1 edit is written, the orchestrator MUST file one fixpp issue for the
  silently ignored field, labelled at creation from `gh label list`. Its body names the field, the
  re-derive grep (`grep -rn control_plane_factory src/`) and the two options (refuse a non-null factory
  in v1.0, or document the field as reserved). L-094-1 and research R-10 then name its real number;
  no placeholder number is ever committed.
  - **Placeholder rule (applies to FR-018, FR-020, FR-021 and FR-023).** A placeholder is an
    issue-number token in angle brackets: `fixpp#` directly followed by a less-than sign, or a
    less-than sign directly followed by `N` and a hyphen (the bundle's are the FR-018, FR-020 and FR-023 issue
    tokens). The grep in tasks.md's Execution rules finds each one; this paragraph describes the
    tokens in words so that the grep does not list it. "Never committed" means: never in a
    file outside `specs/094-v1-tier0-bookkeeping/` (the catalogue, the B&L file, `conanfile.py`,
    every other product file), and, inside the bundle, none left after tasks T011 fills the real
    numbers. The bundle carried the tokens before T011 by design. Two bundle files keep them after T011
    because they name what is replaced: `tasks.md`, and the checklists under `checklists/`, which quote
    the tokens in their items. The grep excludes both.
- **FR-019**: The PR body MUST state:
  - the Conan cache miss on every lane, once (FR-013a);
  - that `include/fix/c_api` and `conanfile.py` match tier1's `PY_RE`, so the PR also launches the
    python-wheel fleet, and that the header comment edits invalidate ccache for every TU that includes
    the touched headers;
  - that the C-ABI change is comment-only (FR-014);
  - the relicensing basis: the 16 Apache-2.0 files have one human author, the owner (their
    `Co-Authored-By` trailers name only AI assistants), and versions already published under
    Apache-2.0 keep that grant (research R-8).
  The PR body, every commit message, every issue body this feature files, and the squash-merge commit
  message MUST NOT put a closing keyword ("close", "fix", "resolve" and their forms) next to #607,
  #612, #614 or the FR-018, FR-020, FR-021 and FR-023 issues, including in a negation such as "does
  not close".
  - "Next to" is the form GitHub's linker reads: a keyword (close, closes, closed, fix, fixes, fixed,
    resolve, resolves, resolved; any case), an optional colon, whitespace, then the reference, which
    is `#N`, `owner/repo#N` or the issue's URL. tasks T048's grep is the oracle for that form, run
    with its positive controls.
  - The squash-merge message is the one GitHub composes from the PR title and body or from the commits
    (both covered above). Before the merge (tasks T065) `closingIssuesReferences` is read empty, and if
    the merge message is edited, the same grep runs over it.
- **FR-020**: Before the NFR-002 row is written, the orchestrator MUST file one fixpp issue for the 544
  record's lapse, labelled at creation from `gh label list`. Its body says that
  `544-hot-path-zero-alloc-verify.md` (parent repo (private)) reported 81.73 % branch and assessed
  added lines only, short of `[const §IX.1]`'s "every uncovered line/branch", and asks for the
  untaken branches to be assessed or each disposition recorded. NFR-002's Verified cell and research
  R-10 name its real number; no placeholder number is ever committed. This is the Edge Cases rule
  applied: a residual is on record before the row flips.
- **FR-021**: At implement time the orchestrator MUST file one fixpp follow-up issue, labelled at
  creation from `gh label list`, for the 40 files outside `src/`/`include/` that still carry
  `SPDX-License-Identifier: Apache-2.0` (37 under `tests/`, 3 under `bench/`; re-derive grep in
  research R-8). Its body carries R-8's re-derive grep and its output and says 094 does not change
  those files. It is filed before research R-8 records its number, which is written as the real
  number (FR-018's placeholder rule). 094 does not change those files.
- **FR-022**: `phase-implementer` MUST add two test cells (a test-only change; owner, Gate A round 2),
  each in a file already compiled into `session_pure_tests` (`tests/session/CMakeLists.txt`), so no
  ctest entry is added. Both files already open with `// SPDX-License-Identifier: AGPL-3.0-or-later`,
  so no licence line changes in them:
  - **Acceptor:** `EnumValidationLogonTest.InDomainNonZeroEncryptMethod_DisconnectsAndSendsNothing`
    in `tests/session/test_enum_validation_logon.cpp`, next to `InDomainEncryptMethod_EstablishesAsToday`.
    It feeds `make_fix44_logon_frame(1, "TW", "ISLD", 30, /*encrypt_method=*/2)` and asserts the state
    is `Disconnected` and `transport.sent_frames()` is empty.
  - **Initiator:** `FsmMatrixWitness.LS_InboundLogonEncryptMethodNonZero_DisconnectsAndSendsNothing`
    in `tests/session/fsm_matrix_witness_test.cpp`. At LogonSent it feeds a Logon built with
    `build_logon(kTarget, kSender, kBeginStr, 1, 30, /*encrypt_method=*/2)` (the file's Logon builder, given an `encrypt_method` parameter defaulting to 0 at /simplify; first written as a hand-built `build_frame` call) and asserts the state is
    `Disconnected` and `captured_frames.size()` is unchanged across the feed.
  - **RED proof, each cell:** in a scratch copy, delete the sticky refusal in
    `src/session/admin_messages.cpp`, `interpret_logon`: the `if (invalid_encrypt_method_seen)` block
    that returns `session_invalid_logon`. Each cell must then fail on its state assertion (the session
    establishes). The acceptor's send-count assertion also fails (it sends its Logon reply). For the
    initiator's send-count assertion, the known positive is the fixture itself: `captured_frames`
    holds the initiator's own outbound Logon before `clear_handshake_captures()`.
  - **What counts as RED.** The mutant build compiles, the binary runs to completion, and gtest reports
    the cell FAILED with a failure message at the state assertion's `file:line` (for the acceptor, also
    at the send-count assertion's). A build error, a crash, a timeout or a failure only at another
    assertion is not RED. The quoted RED output in the verify record shows those lines.
  - **The initiator's send-count assertion is not required to fail under the mutant.** Whether an
    initiator that establishes at LogonSent sends anything is not relied on: the state assertion carries
    the mutant-level RED, and the send-count assertion's ability to fail is shown at fixture level (the
    known positive above).
  - **If either cell cannot be made GREEN on the branch, or cannot be proven RED as above,** TC-017
    does not flip: Edge Cases applies (the row stays `backlog`, the finding is recorded, the owner is
    told), and no narrowed TC-017 text is drafted at implement.
  - The stale initiator-arm comment in `src/session/session.cpp` ("with Logout in US4", on the
    `interpret_logon` refusal in `on_inbound_logon_sent_`) is not edited; it is a P3 note (plan.md
    §Gate A).
- **FR-022b**: `phase-implementer` MUST add one test cell, D-001's census (a test-only change; owner,
  2026-10-22, §Clarifications "Session 2026-10-22 (US2 evidence)"):
  `DictionaryCensus.Fix42AllMessagesAndFieldsLoaded`, a `TEST` in `tests/dictionary/lookup_test.cpp`.
  That file is a source of `dictionary_pure_tests` (`tests/dictionary/CMakeLists.txt`), whose fixture
  already loads `FIX42.xml` through `XmlLoader` and whose binary already links pugixml, so no ctest
  entry is added. The file already opens with `// SPDX-License-Identifier: AGPL-3.0-or-later`.
  - **What it reads.** It loads `FIX42.xml` (under `FIXPP_DICT_DATA_DIR`) with `XmlLoader`. It walks the
    same file independently with pugixml, without the loader, as the raw-XML ground-truth scans of
    `tests/dictionary/reused_tag_census_test.cpp` do: every `<fix><messages><message>`'s `msgtype`, and
    every `<fix><fields><field>`'s `number` and `name`.
  - **What it asserts, by exact set equality.**
    - The MsgTypes of `Dictionary::messages()` equal the raw `msgtype` set.
    - The tags `t` in 1..65535 for which `Dictionary::field_name(t)` is non-empty equal the raw
      `number` set.
    - For each raw field, `Dictionary::field_by_name(name)` returns its `number`.
      Since Gate B round 3, `Dictionary::field_name(number)` also returns its `name`.
    - A guard runs first and asserts that both raw sets are non-empty: the walk's known positive, so
      an empty walk cannot pass.
    - Components are vacuous for FIX 4.2 (its `<components />` is empty), and groups are
      `DelimiterCensus`'s, so the cell checks neither.
  - **RED proof.** Two mutants, each in its own scratch copy (`git archive HEAD | tar -x`), each in
    `LoaderState::finalize()` in `src/dictionary/xml_loader.cpp`:
    - (a) a field mutant: the field-name index loop skips the raw field with tag 24 (IOIOthSvc, which
      no FIX 4.2 message, component or group references, so the load still completes);
    - (b) a message mutant: the messages loop skips one message (MsgType "2", ResendRequest).
    - Each mutant must fail the cell at its set-equality assertion. FR-022's "What counts as RED"
      applies.
    - If a named drop site aborts the load or fails elsewhere, the implementer may drop the same
      field or message at another site in the loader's registration path. The verify record names
      the site used.
  - **If the cell cannot be made GREEN on the branch, or cannot be proven RED by both mutants,** D-001
    does not flip: Edge Cases applies (the row stays `backlog`, the finding is recorded, the owner is
    told).
  - **Extended at Gate B round 3 (owner, 2026-10-23, §Clarifications "Session 2026-10-23 (Gate B round 3)").** The
    same census runs, through one shared helper, in four more TESTs of the same file:
    `DictionaryCensus.Fix44AllMessagesAndFieldsLoaded`, `DictionaryCensus.Fix50Sp2AllMessagesAndFieldsLoaded` and
    `DictionaryCensus.Fixt11AllMessagesAndFieldsLoaded` (`XmlLoader`, the raw `<fix>` walk above), and
    `DictionaryCensus.OrchestraFixLatestAllMessagesAndFieldsLoaded` (`OrchestraLoader`; the raw walk reads every
    `fixr:repository/fixr:messages/fixr:message`'s `msgType` and every `fixr:repository/fixr:fields/fixr:field`'s
    `id` and `name`). Every census, FIX 4.2's included, also asserts that `Dictionary::field_name(number)` equals each
    raw field's `name`. The FIX50SP2 and FIXT11 cells witness D-003's dictionaries; all five anchor to the source XML
    the field names that the codegen and FR-022c's identity check read through the same loaders. RED proof: three
    loader mutants, each in both loaders, in a scratch copy on F: — a field-name index entry dropped (BeginSeqNo(7)),
    a message entry dropped (MsgTypes "2" and "DZ"), and two name pairs exchanged in the field-name index (Account /
    ClOrdID, SenderCompID / TargetCompID) — each failing every census at its set-equality, `field_by_name` or
    `field_name` assertion; under the exchange, FR-022c's census still passes, which is the common mode this
    extension closes.
- **FR-022c**: `phase-implementer` MUST add one test cell, D-008's accessor-resolution census (a
  test-only change; owner, 2026-10-23, §Clarifications "Session 2026-10-23 (Gate B round 1)", Q2,
  and "Session 2026-10-23 (Gate B round 2)", Q1):
  `CodegenAccessorResolutionCensus.EveryTypedAccessorResolvesTagAndTypeAtCompileTime`, a `TEST` in a
  new file, `tests/codegen/accessor_resolution_census_test.cpp`, added to the sources of
  `codegen_pure_tests` (`tests/codegen/CMakeLists.txt`), so no ctest entry is added. The file opens
  with `// SPDX-License-Identifier: AGPL-3.0-or-later`. `codegen_pure_tests` already defines
  `FIXPP_CODEGEN_INC`; the CMake edit adds the source and a compile definition,
  `FIXPP_D008_CENSUS_HAS_VLATEST`, set only when `FIXPP_CODEGEN_FIX_LATEST` is ON.
  - **The property** it witnesses, D-008's compile-time binding clause: each typed accessor binds its
    field's tag at compile time, with its value type fixed in its generated text, and its own
    generated text never consults a runtime `Dictionary`, a name→tag or tag→type table, or any
    dictionary lookup function. The tag is an integer literal: the template argument of
    `view_.template get<N>()` or of `view_.template group<N, …G_N>()` (and, in `Reify.hpp`'s
    `owning_<Msg>` classes, of `view().template get<N>()` or of `view().template group<N, …G_N>()`),
    or the literal argument of `::fixpp::wire::get(ctx_.span, N, …)` or of
    `nested_group_slices(…, N, …)`; and it is the tag of
    the field the accessor is named for. The value type is fixed by the emitted text:
    `decode_field<T>` with T equal to the accessor's `expected_t<T>` and one of the four scalar types
    the emitter uses, `decimal_t::parse` for a `decimal_t` accessor, or the group flyweight type; and
    it is the type the codegen's data-type mapping assigns to the field's data type, not necessarily
    the field's correct type (fixpp#621).
  - **Boundary.** The cell reads the accessor's own text. The runtime work of what it calls is outside
    the cell; the Title makes no claim about it: `MessageView::get` finds the field in the offset table, and
    `MessageView::group<>()`, `nested_group_slices` and the group-entry `::fixpp::wire::get` segment
    group occurrences and split Length+Data pairs, which may call the session dictionary through
    `dict_hooks` (group membership, group delimiter, a dictionary's own pairs). Re-derive the
    callbacks: `grep -n 'group_member_fn\|group_delim_fn\|length_pair_fn' include/fixpp/wire/*.hpp src/wire/*.cpp`.
  - **Population.** Every typed field accessor of each codegen version's generated `Messages.hpp`
    and `Reify.hpp`: v42, v44, v50sp2 and vt11, and vlatest when the build generates it. In
    `Messages.hpp`, message flyweights and group flyweights both count, with their group and
    nested-group accessors; in `Reify.hpp` (`emit_reify.cpp`), every `owning_<Msg>` class's scalar,
    decimal and group accessors. A listed version whose header is missing fails; it is never
    skipped. **Excluded:** `field_value(::std::uint16_t tag)`, the runtime-tag passthrough, whose tag
    is the caller's, and the `view()` bridge (in `Reify.hpp`, the `view()` declaration), which returns
    the bound view. Neither is a typed field accessor; each is accepted only with its exact signature
    and body, and exactly once per class (once per message for `view()`), so a second member of that
    shape cannot carry a lookup. `Reify.hpp`'s other non-accessor members (the deleted copies, the
    move declarations, `which()`, `from_view`, the private constructor and data members, the
    out-of-line definitions of the moves, `view()` and `from_view`, and the `owning_message_traits`
    specialisation, once per class) are accepted only by their exact text.
  - **What it asserts.** Three checks over each version's `Messages.hpp`, and the same three over its
    `Reify.hpp`, each with its own assertion:
    - *Grammar.* A closed grammar, not a forbidden-token list: every line of the file is one of the
      shapes the emitter writes (the pinned include set, the namespace and class lines, the
      constructors and members, the `static_assert`s, comments), and every member function matches
      one of the accessor templates in full, signature and body, or one of the two exclusions. Any
      other line fails the cell. A deliberate emitter change that adds an accessor shape must add it
      to the cell by hand. The `Reify.hpp` grammar is closed the same way over the whole file.
    - *Population.* For every class, the set of tags its typed accessors bind equals the set the
      version's `Fields.hpp` declares for it, less the rows whose data type the mapping below omits:
      a message's `<Msg>_fields[]` rows with `group_no_tag` 0, and a group flyweight `G_K`'s rows
      with `group_no_tag` K across every message. No tag is bound twice, and the classified count
      equals the `Fields.hpp`-derived count. In `Reify.hpp` each `owning_<Msg>` class is compared with
      `<Msg>_fields[]` (it declares no group flyweights of its own). `Fields.hpp` is written by another emitter
      (`emit_fields.cpp`) from the same IR, so the check proves the walk reached every accessor the
      version declares; dictionary completeness is FR-022b's censuses' and the 082 tests'.
    - *Identity.* Per accessor, its name and its value type. The name: the accessor's identifier is
      the accessor name of its tag's field name in the dictionary the version is generated from
      (`FIX42.xml`, `FIX44.xml`, `FIX50SP2.xml`, `FIXT11.xml`, and `orchestra/OrchestraFIXLatest.xml`
      for vlatest, read with `XmlLoader` or `OrchestraLoader` as `build_ir` reads them), under the
      naming rule restated in the cell (`to_accessor`'s documented rule; a group accessor first drops
      a leading "No", as `strip_no_prefix` does). The base name followed by `_t<tag>` is accepted
      only when another accessor of the class holds the base name, the emitter's collision form;
      which of the two holds the base name is not checked. The value type: a tag that heads a group
      in `Fields.hpp` has a group accessor, and any other has the kind that a restatement, in the
      cell, of the codegen's data-type mapping (`kind_of`) assigns to its `Fields.hpp` data type.
      The restatement is the design mapping, not each field's correct type: it carries
      MultiCharValue → char, which fixpp#621 reports as wrong, so a wrong mapping is not caught. The
      field names come from the library's loaders, which the codegen also uses, so a loader name defect common to
      both would pass this cell; FR-022b's censuses, one per dictionary this cell reads, check those loaders' names
      against an independent walk of the source XML.
  - **Positive control.** A guard runs first, over a synthetic `Messages.hpp`, a synthetic
    `Reify.hpp`, a synthetic `Fields.hpp` and a synthetic name map:
    - the synthetic text passes all three checks, an accessor in the `_t<tag>` collision form
      included;
    - each of a set of seeded runtime-resolution shapes fails the grammar (a tag looked up by name,
      a tag read from a runtime-indexed table, a type chosen at runtime, an extra include, a
      namespace-scope table, a dictionary member, an accessor without `[[nodiscard]] inline`, a
      second passthrough);
    - each seeded identity mutation passes the grammar and the population check and fails the
      identity check, so the identity check is what catches it: two same-type message accessors'
      tag literals swapped; a scalar's return and `decode_field<T>` type changed together; a message
      scalar, a group-entry scalar, a group accessor and a nested-group accessor renamed; a `_t<tag>`
      suffix naming another tag, or used with no collision; and a `Fields.hpp` data type changed
      under a char and under a decimal accessor;
    - the synthetic `Reify.hpp` passes all three checks; each of its seeded runtime-resolution and
      grammar shapes fails the grammar (an `owning_` accessor's tag looked up by name, a tag read from
      a member, a namespace-scope table, a changed out-of-line definition, a missing traits
      specialisation); and each of its identity seeds passes the grammar and the population check
      and fails the identity check (two same-type `owning_` accessors' tag literals swapped, a
      consistent type change, a scalar renamed, a group accessor named without dropping "No").

    Then, per version and per file, the classified typed-accessor count is nonzero, and the version's dictionary
    loads and names at least one of its `Fields.hpp` tags (a tag it leaves unnamed fails the identity
    check for any accessor of that tag).
  - **RED proof.** Emitter mutants, each in a scratch copy on F: (`git archive HEAD | tar -x` of a
    head that holds the cell), each in `tools/codegen/fixpp-codegen/emit_messages.cpp`, so the
    configure-time codegen writes it into every version's header:
    - (a) the message-flyweight path: ClOrdID(11)'s accessor resolves its tag at runtime, by name,
      through a function-local name→tag table, and reads it with `view_.get(tag)`;
    - (b) the group-entry path: one group-entry scalar accessor resolves its tag the same way and
      passes it to `::fixpp::wire::get`;
    - (c) a tag swap that keeps the tag set: NewOrderSingle's (MsgType D) Account(1) and ClOrdID(11)
      accessors are emitted under each other's names, so `account()` binds 11 and `cl_ord_id()`
      binds 1, both `string_view`;
    - (d) a type change inside the grammar: in `emit_scalar`, a Boolean field's accessor is emitted
      with `char` as both its return type and its `decode_field<T>` type;
    - (e) a rename: NewOrderSingle's Account(1) accessor is emitted as `account_id()`, its tag and
      type kept;
    - (g) and (h), in `tools/codegen/fixpp-codegen/emit_reify.cpp`, so they reach `Reify.hpp` only:
      (g) `owning_` ClOrdID(11)'s accessor resolves its tag at runtime by name and reads it with
      `view().get(tag)`; (h) `owning_NewOrderSingle`'s Account(1) and ClOrdID(11) accessors are
      emitted under each other's names, the tag set kept.

    One test-side mutant, in the scratch copy of the cell: (f) the identity check's name
    comparison is removed. Before each emitter run, the scratch's generated `Messages.hpp` is
    checked to hold the mutant bytes. The whole `codegen_pure_tests` binary runs unfiltered.
    FR-022's "What counts as RED" applies: the binary exits 1, not with an abort, and the census
    is the only failing test. (a) and (b) fail at the unclassified-line assertion naming the
    mutated accessor. (c), (d) and (e) fail at the identity assertion, and the grammar and
    population assertions pass. (f) fails in the guard, at "identity mutation accepted". (g) fails
    at the `Reify.hpp` grammar assertion, and (h) at the `Reify.hpp` identity assertion only, with
    every `Messages.hpp` assertion passing under both.
  - **Why the byte pins are not this witness.** `read_tier_byte_diff_test.cmake` and
    `DeterminismTest.GeneratedMatchesGolden` pin the generated text, and are republished at every
    intended emitter change; this cell asserts the property, so a republished pin that carried a
    runtime lookup would still fail it. vlatest has no read-tier byte pin.
  - **If the cell cannot be made GREEN on the branch, or cannot be proven RED by its mutants,** the
    clause is not witnessed: the finding is recorded and the owner is told (Edge Cases).
- **FR-022d**: `phase-implementer` MUST strengthen one existing test cell, TC-012's witness (a
  test-only change; owner, §Clarifications "Session 2026-10-23 (Gate B round 4)", Q1):
  `InboundPossResendTest.DeliverInSeq_97Y` (`tests/session/test_inbound_poss_resend.cpp`, ctest
  `session_inbound_poss_resend`) copies tag 97's value from the view delivered to `fromApp` and
  asserts it equals `Y`. RED proof: with the fixture's `97=Y` changed to `97=N`, the cell fails at
  that assertion; the same change against the cell before FR-022d passes, which is the gap it closes.
- **FR-023**: Before the FR-013b commit, the orchestrator MUST file one fixpp issue, labelled at creation from
  `gh label list`, recording that `[const §III.4]` is not met on `main`: `tool_requires = []` since `629f2170`
  (2026-05-10), so CMake and Ninja come from the host toolchain. Its body asks the owner to choose: restore Conan-pinned
  `cmake`/`ninja` `tool_requires`, or amend §III.4 to the host-provisioned model with its reproducibility requirement.
  Implement replaces every occurrence of the FR-023 issue-number placeholder, in the bundle and in the committed files
  (the FR-013b `conanfile.py` comment), with its real number; no placeholder number is committed. 094 changes no tool
  provisioning.

### Key Entities

- **Catalogue row**: ID, Source, Category, Title, FIX version(s), Spec ref, Status, /specify, PR,
  Tests, Verified (`spec/feature-catalogue.md` schema line).
- **Residual on record**: a live B&L row ID (`L-…`/`B-…`) or an open fixpp issue number.
- **Amendment**: the six clauses changed, the Status line, the Sync Impact Report, the version bump
  and the `CHANGELOG.md` entry.

## Success Criteria *(mandatory)*

- **SC-001**: The open OFFICIAL row count falls by exactly 25 (7 drops, 18 flips), the done count rises
  by exactly 18 (152 → 170 parsed; 154 → 172 counting C-002 and T-042, research R-4), the post-v1.0
  drop count goes 0 → 8, and no other row changes Status. All counts come from the census output.
  These figures assume no flip is withheld; if k flips are withheld under Edge Cases, the open count
  falls by 25 − k and the done count rises by 18 − k (FR-016), and the verify record names the k rows.
- **SC-002**: 100 % of the tests cited in the 18 flipped rows exist literally and pass on this branch,
  and every clause in the data-model §H map has its assertion recorded. "Exist" is research R-11's
  counting rule and check (54 distinct entries since FR-022b, 55 since FR-022c, 59 since FR-022b's round-3 extension, both negative controls reporting MISSING); "pass" is
  each named test's PASSED read from its binary's own output, not from ctest's exit code (quickstart
  §2).
- **SC-003**: 0 constitution clauses state the service wrapper, gRPC control plane or iceoryx2 data
  plane as v1.0 scope, and the R-12 recipe re-run after the edits leaves 0 hits outside its "keep",
  "header", "other concept" and "pattern FP" classes and the lines its "edit" hits became (an applied
  edit still matches the terms). The constitution half has a mechanical part (quickstart §4: every
  data-model §E.3–§E.7 after-text present once, every replaced before-text absent) and a reading part
  (every remaining grep hit in a named class).
- **SC-004**: 100 % of tracked C/C++ files under `src/` and `include/` carry the AGPL-3.0-or-later
  identifier, and 0 carry another identifier.
- **SC-005**: The diff outside `spec/`, `.specify/`, `brain/`, `specs/` and `CHANGELOG.md` contains
  only: the licence comment lines of the 53 `src/`/`include/` files; `tools/capi_freeze.sha256`;
  `conanfile.py`'s `license` line and its four `tool_requires` comment lines (FR-013b, separate
  commit); `cmake/FixppPackaging.cmake`'s `CPACK_RPM_PACKAGE_LICENSE` line; `README.md`'s tagline;
  `docs/src/intro.md`'s opening sentence; the two FR-022 test cells in `tests/session/test_enum_validation_logon.cpp`
  and `tests/session/fsm_matrix_witness_test.cpp`; the FR-022b census cells in
  `tests/dictionary/lookup_test.cpp`; FR-022c's new file `tests/codegen/accessor_resolution_census_test.cpp`
  and its registration in `tests/codegen/CMakeLists.txt` (one source line, and the
  `FIXPP_D008_CENSUS_HAS_VLATEST` block); FR-022d's assertion in
  `tests/session/test_inbound_poss_resend.cpp`; and one comment line in
  `tests/tls/test_pinset_snapshot_publish_acquire.cpp`, whose `pinset.hpp §139` line citation the
  licence-line insert made stale. The number is replaced by the member it meant (`snapshot_`),
  following #310's rule (delete the number, cite the symbol). Found at C-SPDX by a basename sweep
  for `§N` / `#LN` / `line N` forms, which the shift audit cannot see.
- **SC-006**: Gate A (on the amendment and bundle) and Gate B converge, and the owner signs off on the
  amendment.

## Assumptions

- The verification file's readings hold at the branch base, because it was read at `9b176090` and that
  is still `origin/main`. Every citation is still re-checked (FR-010, FR-015).
- 51 of the 53 test entries the flips cite were grep-checked on 2026-10-22, with two negative controls:
  all exist (research R-11). The other two are the FR-022 cells, added at implement. FR-022b adds a
  54th, D-001's census cell, also added at implement (T029a). FR-022c adds a 55th, D-008's
  accessor-resolution census, added at Gate B round 1. Passing is checked at verify.
- The atomic-shared-ptr spike (parent `research/G19-fix-fpml-iso20022/atomic-shared-ptr/`) carries no
  licence or copyright. The 37 unmarked files carry no foreign copyright notice (provenance grep,
  2026-10-22). So marking them AGPL relabels the owner's own work.
- Licence comments are outside Doxygen output, and no golden test pins those bytes. The plan checks
  the second point with a byte-pin grep.

## Out of scope

- NFR-010's commercial half (postponed by the owner) and counsel review (row 15d, L-2).
- The 9 not-delivered rows (FR-012), which stay `backlog` after the typed-message track.
- SPDX marking outside `src/`/`include/` (tests, tools, bench, bindings, examples); the 40 `Apache-2.0`
  files there are the FR-021 issue.
- Any library code or behaviour change, including the fixpp#612 fix (batch B65). The only test changes
  are FR-022's two cells, FR-022b's census cells, FR-022c's accessor-resolution census and FR-022d's
  TC-012 assertion.

## Normative References

- `[const §VI.6]` — closing a `done` row (amended in v4.0 for process rows); `[const §VI.4]` — coverage-index entry before a new OFFICIAL
  row; `[const §XX.1]`–`[const §XX.4]` — the amendment process and classification; `[const §XVIII.5]`
  — why the amendment is MAJOR; `[const §I.3]` — the tag gate; `[const §IX.1]` — NFR-002's new
  wording; `[const §XII.7]`, `[const §XV.10]` — TC-017.
- `[FIX-TC §4 scenario 3]`, `[FIX-TC §4 scenario 7]`, `[FIX-TC §4 scenario 8]`,
  `[FIX-TC §4 scenario 9]`, `[FIX-TC §4 scenario 11]`, `[FIX-TC §4 scenario 15]`,
  `[FIX-TC §4 scenario 17]`, `[FIX-TC §4 scenario 19]` — the coverage-index entries for the TC rows
  flipped.
- `[FIX42]`, `[FIX50SP2]` — D-001, D-003.
- Parent repo (private) `research/G19-fix-fpml-iso20022/research/findings/v1-tier0-flip-verification-2026-10-21.md`
  — the per-row evidence and proposed cells.
- Parent repo (private) `REMAINING-WORK.md` — ★ V1.0 FUNCTIONAL TRACK row 1, DECISION 1, the Tier-0
  re-verification note and the owner rulings of 2026-10-21.
- `spec/behaviors-and-limitations.md` — L-003-2, L-022-1, L-497-1, L-002-4, B-043-1, B-005-7,
  B-495-3, B-544-1, L-019-4, L-003-1 (the residuals named in Verified cells); L-094-1 (added by this
  feature).
