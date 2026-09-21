# Feature Specification: Three C-ABI calls that must refuse — C-ABI 1.7, marked BREAKING

**Feature Branch**: `447-458-452-capi-refusals`

> ⚠️ **THE BRANCH AND THE FEATURE DIRECTORY DIFFER DELIBERATELY, AND ONE TOOL REPORTS THE WRONG
> ONE.** This bundle lives in `specs/090-capi-refusals/`; the git branch is
> `447-458-452-capi-refusals`, which predates the bundle. `.specify/scripts/bash/check-prerequisites.sh`
> reports a `BRANCH` field that it derives from a **tracked pin, never from git**. Measured
> 2026-09-20, in this tree:
>
> ```
> bash .specify/scripts/bash/check-prerequisites.sh --json --paths-only
>   -> {"BRANCH":"090-capi-refusals","FEATURE_DIR":".../specs/090-capi-refusals", …}   rc=0
> git rev-parse --abbrev-ref HEAD
>   -> 447-458-452-capi-refusals
> ```
>
> The `FEATURE_DIR` it returns is now **correct** for this work; the `BRANCH` value is **not a git
> branch** and never was. This is **fixpp#490 — mitigated, not fixed**: the pin happens to point
> here, so the failure is silent rather than loud. **Any later step that consumes that `BRANCH`
> field — to check out, to validate, to name a PR head, to resolve a worktree — will be wrong.**
> Read the branch from `git rev-parse --abbrev-ref HEAD`.

**Created**: 2026-09-20

**Status**: Draft

**Input**: User description: "Three C-ABI calls that today return `FIXPP_ERR_OK` while doing
something wrong must instead refuse: `fixpp_msg_remove_tag` (fixpp#447), `fixpp_msg_clone`
(fixpp#458), and the two session-config setters (fixpp#452). The C-ABI version goes 1.6 → 1.7, a
MINOR bump marked BREAKING under the pre-first-release clause."

**Issues**: fixpp#447, fixpp#458, fixpp#452. The PR **closes fixpp#488**; **fixpp#487** and
**fixpp#489** stay open.

**Design authority**: `.specify/447-458-452-capi-refusals.md` — **read its Status line for the current version; do not trust a version written here.** It was **v0.9** when this spec was authored and is **v0.10** now; the pointer went stale within the day, which is the same defect class v0.10 itself exists to correct. ⚠️ **That document
remains the design authority and this spec does not supersede it.** This spec states **what** each
refusal does and **why**; the note states **how**, decides the options, and carries every
derivation, population and control. Where the two could be read as disagreeing, the note wins and
this file is the one to correct.

**Gate status, stated because it is not the usual one**: Gate A **ran and did NOT converge**. The
label is **`gate-a-waived`**, for **two** reasons — the C-4 scoped delta review's single **P1** (a
quotation whose ellipsis cut the clause's own non-exhaustiveness hedge), and the **corpus finding**
of 2026-09-20 (the amendment's derivation hardcoded its corpus to one file). Nothing here may be
read as "Gate A passed".

---

## Context — what is broken, and what this bundle is for

Three C-ABI call sequences return `FIXPP_ERR_OK` today while producing a result the caller did not
ask for and cannot detect:

1. **fixpp#447** — `fixpp_msg_remove_tag` succeeds while a group builder is open, invalidating the
   position that builder holds. Subsequent entry writes land in **the wrong group**, or index past
   the end — **undefined behaviour, not a return value**.
2. **fixpp#458** — `fixpp_msg_clone` silently degrades: when the dict-backed re-parse of the source
   fails, it returns a **dict-free clone** with `FIXPP_ERR_OK`. The caller believes it holds an
   equivalent message; field lookups on it report absent.
3. **fixpp#452** — `fixpp_session_config_set_comp_ids` and
   `fixpp_session_config_set_begin_string` accept configured strings containing SOH (`0x01`), any
   byte below `0x20`, or `'='`. Those bytes reach the wire verbatim in every admin message, where
   they forge header fields.

### Two tracks, and folding them into one is the failure this feature is most likely to produce

The design note names it in its own words: *"The single most likely way to get this document wrong
is to fold all five changes under 'C-ABI 1.7 BREAKING'."* There are **five** behavioural changes
across **two** tracks and **one** version bump:

| track | changes | how it is governed |
|---|---|---|
| **C-ABI** (three changes) | `fixpp_msg_remove_tag` refuses with a live open builder · `fixpp_msg_clone` refuses instead of degrading · the two session-config setters refuse injected bytes | `[const §X.7]`'s BREAKING machinery; `[const §X.1]`'s mandatory Gate A; `[const §X.6]`'s four Appendix A controls |
| **C++-only** (two changes) | a message handle from a dict-backed source materialises **eagerly** and refuses a failed re-parse instead of degrading · `Session::open`'s existing byte guard extends to CompIDs, BeginString and each configured RefMsgType(372) | `[const §XVII.1]`'s public-C++-API bullet. **`[const §X.7]` is recorded as NOT engaged**: these are not C-ABI symbols, consume no error-code slot, move no version macro, and are not in the byte-freeze manifest — the disposition `456-table-view-seal` set for a C++-only break |

### The version move

**1.6 → 1.7 — a MINOR bump, marked BREAKING, under `[const §X.7]`. No constitutional amendment is
triggered.** The premise is measured, not assumed: `gh release list --exclude-drafts` returns
nothing for this repository against a control that prints rows for another, so **the first public
release has not happened** and the pre-first-release clause governs — *"a MINOR bump marked
BREAKING, no amendment"*. `api-contract.md` §11 supplies the **definition** of a C-ABI breaking
change (*"a call that used to succeed and now fails"*); `[const §X.7]` supplies the **procedure**.
Citing §11 for the procedure would be wrong.

### The `[const §X.6]` controls — where each stands

| control | state |
|---|---|
| Gate A (`[const §X.1]`, mandatory) | **RAN; DID NOT CONVERGE.** Label **`gate-a-waived`**, two reasons (header block). Four rounds, a post-sign-off P1, a scoped delta review and two owner decisions. `P1 == 0 AND P2 == 0` has never been returned |
| `/clarify` | **Discharged in substance, not by running the skill.** The skill would have resolved `FEATURE_SPEC` through the fixpp#490 pin and written into a shipped, unrelated feature's spec. The questions were asked and answered by the owner against the design note. *"The control ran"* and *"the control's substance was executed by hand"* are different claims; only the second is true |
| `/analyze` | ✅ **DISCHARGED 2026-09-20 — but PINNED, not unconditional.** It is a cross-artifact check over `spec.md` / `plan.md` / `tasks.md`, so it could not run until this bundle had all three; it was then run through the canonical `spec-analyzer` executor **against the artifacts at `505adafa`**, returning **1 finding (F1, multi-site), 0 CRITICAL, 100 % coverage** — 20 FR + 14 SC over 93 tasks, every FR and every buildable SC mapping to at least one task — and F1 was **remediated at `e9833610`, not deferred**. ⚠️ **A plan entry claimed this control discharged until 2026-09-20 and that claim was FALSE WHEN MADE — it rested on no run at all.** Do not restate it: what discharges the control is the run above, which is different evidence, not a vindication of that entry. ⚠️ **A control discharged against ONE artifact state does not cover a LATER one** — the artifacts have moved since (`e9833610`, `40f2bb92`) and implementation will move them again, which is why **T084 survives as a re-run obligation** rather than being struck |
| user `/plan` sign-off | ✅ **DISCHARGED 2026-09-20** — given by the owner in session, **pinned to `plan.md` at `d12d2270`**; `plan.md`'s Constitution Check row 5 is the authoritative statement and carries the re-derivation command. The control is the **user's sign-off on the plan**, not the command's execution |

---

## Clarifications

⚠️ **Two `C-n` series exist in the design note and they are different things.** The five answers
below are the note's **`## Clarifications`** series (C-1 … C-5). The note *also* has a
*"Corrections to this document's own inputs"* table numbered C-1 … C-8 — where, for example, C-4 is
the `append_raw(372)` site count and C-7 is the probe-cap degradation. **Every `C-n` on this page
means the clarifications series**, which ends at **five**. ⚠️ **The two later owner decisions
therefore carry their own labels — `OD-1` and `OD-2` — rather than extending a series the authority
closed at C-5; extending it would collide with the corrections table at exactly the two numbers this
spec discusses elsewhere.**

### Session 2026-09-20

- **C-1 — the design-corpus amendment rides this PR in full, at all eight `[2i]` passages; not a
  minimum subset, not a split.** The eight are **the same false claim restated**, all falsified by
  the same measured producer population, and the fix is **prose-only**: it reds no pin, mints no
  enumerator and touches no source. Leaving any subset live means the owner document still
  contradicts itself after a PR that claimed to fix exactly that.
- **C-2 — the "CI grep enforces" claim in `[2i §5.2]` is DELETED in the same paragraph**, measured
  against **zero** references to the construct anywhere under the CI, tooling and build
  directories, with a different-pattern control positive on the same corpus. ⚠️ **Deleting the
  assertion that drift is caught is NOT closing the gap** — whether the constructs get implemented
  remains **fixpp#487**, which stays open. This bundle owns only that the document stops claiming a
  gate that does not exist.
- **C-3 — `fixpp_engine_start`'s partial worker launch is FILED, NOT FIXED: fixpp#492.** Stated at
  its true width: **DEGRADED is reported as NOMINAL.** The workers that did launch still drive the
  engine, so it is functional; what the caller cannot discover is that it got **fewer workers than
  it configured**. *"Fails silently"* overstates it. Changing what that call returns is its **own**
  `[const §X.7]` breaking change with its own witness and its own ledger row. ⚠️ **Two issues share
  that one site**: #492 is **strictly** the partial-launch-reports-success behaviour; the
  unreachable-retry half belongs to **fixpp#489**.
- **C-4 — the v0.6/v0.7 delta gets ONE SCOPED REVIEW, then a label; not a full re-read.** ⚠️ **It
  ran, and it returned 1 P1 / 0 P2 / 0 P3.** The `0 P1 / 0 P2` branch did **not** fire, so
  `gate-a-done` was **not earned**; the label is **`gate-a-waived`** and the P1 was applied. It was
  a **scoped** review, **not a round** — no round number is claimed for it.
- **C-5 — ONE PR for all three refusals.** The bump, the version comment, the freeze re-baselines
  and the design-corpus amendment are **shared infrastructure**; splitting duplicates them or makes
  a later PR depend on an unmerged earlier one. The binding ground is **`[const §X.7]` obligation
  3**, which requires every in-repository consumer updated *"in the same PR"* as the breaking
  change — and with three breaking changes sharing one consumer inventory, a split would either
  duplicate that inventory or leave one PR's consumers unupdated at its own merge.

**Two further owner decisions, both taken 2026-09-20, after the five above:**

- **OD-1 — the design-corpus amendment WIDENS beyond `[2i]`, to two derived restatements in other
  live documents.** `api-contract.md`'s §7.5 code-scoping parenthetical and `2m-pybind.md`'s
  *"Construction failure modes"* limb each **restate `[2i]`'s claim while naming `[2i]` as their
  source**, and `api-contract.md`'s own authority clause says it *"does not amend its sources, and
  on any conflict the source wins"*. ⚠️ **TWO POPULATIONS, NEVER SUMMED, AND NO TOTAL IS WRITTEN
  ANYWHERE:** the `[2i]` scope-claim population stays **eight**, derived by a criterion over
  passages that bind the code to a producer set; the two restatements are a **separate population
  with a separate ground** (they cite `[2i]` as their source). *Three*, then *eight*, were each
  falsified by the next pass, and a corrected count is the same defect at a new value. ⚠️ **No
  constitutional amendment is triggered** — the frozen rule governs surfaces marked *Stable from
  v1.0*, which this prose is not, and §11 itself routes a pre-first-release C-ABI breaking change to
  `[const §X.7]`. ⚠️ The **instrument** that hid this was **not repaired**: its corpus is still
  hardcoded to one file. The owner **declined** that repair and widened only the **outcome**; it is
  recorded here and in the note as a **deliberate scope decision**, not an oversight.
- **OD-2 — this work converts from ISSUE MODE to FEATURE MODE; this bundle is that conversion.**
  ⚠️ **This decision is recorded HERE; it is not in the design note at any version.** It follows
  from the controls rather than from preference: it is an **ABI surface change**, so `[const §X.6]`
  puts **all four** Appendix A controls on it; `/analyze` is a cross-artifact check over
  `spec.md` / `plan.md` / `tasks.md`, and **in issue mode none of those exist**, so the control
  could not be discharged at all. A `specs/<id>/` bundle is the only shape in which the two
  controls that were **then** owed can run.

---

## User Scenarios & Testing *(mandatory)*

⚠️ **The priority ordering below is this spec's own. The design note ranks nothing, and all three
refusals ship in ONE PR (C-5), so the ordering governs implementation and test sequencing, not
delivery.**

### User Story 1 - A message builder cannot be silently corrupted from under the caller (Priority: P1) — C-ABI track

A C consumer builds an outbound message, opens a repeating-group builder, and — before ending it —
removes an unrelated tag. Today that call returns `FIXPP_ERR_OK` and the builder it invalidated
keeps writing: entries land in **a different group than the one the caller opened**, or past the end
of the accumulated entries. After this change the call **refuses and erases nothing**, so the
caller's next write goes where the caller intended or not at all.

**Why this priority**: it is the only one of the three whose present behaviour reaches **undefined
behaviour** on a reachable path, and the accumulator is arena-backed, so a clean sanitizer run is
**not** evidence of absence. It is also the only refusal whose pre-fix RED has already been
executed — in fixpp#447 itself.

**Independent Test**: run the two-arm probe recorded in fixpp#447 — the same build sequence with and
without one `fixpp_msg_remove_tag` call — and compare the **committed payload bytes**. Before the
fix the two arms differ; after it the removing arm returns a refusal code and the payloads match.

**Acceptance Scenarios**:

1. **Given** an outbound message with at least one group builder open, **When** the caller calls
   `fixpp_msg_remove_tag` for any tag, **Then** the call returns `FIXPP_ERR_INVALID_HANDLE`, no
   entry is erased, and the message's committed bytes are identical to those of the same sequence
   without the call.
2. **Given** an open builder and a tag positioned **before** that builder's group entry, **When**
   the tag is removed, **Then** the call refuses — the guard is keyed on **a builder being open**,
   not on the erased position or the erased tag, so it cannot be defeated by a tag choice.
3. **Given** a message with **no** builder open, **When** the caller removes a present tag,
   **Then** the call still succeeds exactly as today and the tag is gone.
4. **Given** any index a C-ABI entry point resolves for a group or an instance, **When** that index
   is out of range, **Then** the entry point returns `FIXPP_ERR_INVALID_HANDLE` rather than reading
   out of bounds.

### User Story 2 - Configured strings cannot inject fields into the wire (Priority: P1) — C-ABI track and C++ track

An operator configures a session with a SenderCompID, TargetCompID or BeginString, or a supported
message type. Today any byte is accepted and emitted verbatim into every admin message, so a value
containing SOH or `'='` **forges additional header fields** on the wire that no code intended.
After this change the configuration is **refused at the point it is set** (C-ABI) and again when the
session is opened (C++), before any frame can be emitted.

**Why this priority**: it is the only defect of the three that a **counterparty** can be made to
observe, and the fix is a floor that already exists in the same code for credentials — it is
extended, not invented. The owner's decision (fixpp#452 had filed the C-ABI half as *optional*)
puts the refusal at the setters, which is what makes it visible to a C consumer at all.

**Independent Test**: call `fixpp_session_config_set_comp_ids` with a value containing a literal SOH
byte and assert the return code; then drive a session with the same value through the C++ surface
and assert it never opens. Both are independent of the other two stories.

**Acceptance Scenarios**:

1. **Given** a session config handle, **When** the caller sets a CompID or BeginString containing
   any byte below `0x20` (SOH included) or `'='`, **Then** the setter returns
   `FIXPP_ERR_CAPI_CONFIG_INVALID` and the configuration is unchanged.
2. **Given** a session configured through the C++ surface with such a value in a CompID, the
   BeginString, or any configured RefMsgType(372) entry, **When** the session is opened, **Then**
   the open fails before any message is emitted.
3. **Given** a configured value containing none of those bytes, **When** it is set and the session
   opened, **Then** behaviour is unchanged from today.
4. **Given** a Python consumer of the binding, **When** it passes a value containing SOH to the
   exposed setter, **Then** it observes the same refusal rather than a successful call.

### User Story 3 - A clone is either equivalent or an error, never a silent downgrade (Priority: P2) — C-ABI track

A C consumer clones a message that was parsed against a dictionary. Today, if the clone's re-parse
fails, the call returns `FIXPP_ERR_OK` and hands back a **dict-free** clone: lookups that succeed on
the original report **absent** on the copy, and nothing in the return value says so. After this
change the call **returns the wire error that caused the failure**, so the caller can tell a clone
it can use from one it cannot.

**Why this priority**: the corruption is quieter than Story 1's and narrower than Story 2's, but it
is the one a caller is most likely to build on — a clone that answers "field absent" is
indistinguishable from a message that genuinely lacks the field.

**Independent Test**: build a source message whose dict-backed re-parse fails (the raised-cap route
is preferred over allocation injection because it is deterministic), clone it, and assert the call
returns non-OK and produces no handle that reports a forged or absent field.

**Acceptance Scenarios**:

1. **Given** a message whose dict-backed re-parse will fail, **When** the caller clones it,
   **Then** the call returns a non-OK code describing the wire failure and produces no clone
   handle.
2. **Given** the same call, **When** the failure is an allocation failure, **Then** the caller sees
   `FIXPP_ERR_UNKNOWN` — pre-existing, documented behaviour for out-of-memory (limitation
   **L-049-2**), disclosed in the ledger delta rather than presented as new.
3. **Given** the same call, **When** the failure is a capacity or range failure, **Then** the caller
   sees `FIXPP_ERR_WIRE_LIMIT_EXCEEDED`; **when** it is a malformed-field failure,
   `FIXPP_ERR_WIRE_INVALID_FRAME`.
4. **Given** a source whose re-parse succeeds, **When** the caller clones it, **Then** the call
   succeeds exactly as today.

### User Story 4 - A C++ caller receives an error instead of a handle that will disappoint it (Priority: P3) — C++-only track

A C++ caller that obtains a message handle from a dict-backed source whose re-parse fails today
receives a **live handle** whose first read silently falls back to a dictionary-free view. After
this change the handle is materialised **before it is handed over**, and that one failure is
reported through the error channel the entry point already has.

**Why this priority**: it is the same defect as Story 3 seen from the C++ side, it consumes no
version-contract machinery, and it is the change with the largest surface of **shipped behaviour
that must NOT move** — which is why it is scoped as narrowly as it is.

**Independent Test**: call the dictionary reify entry point (and the generated dispatch) with a
dict-backed source whose re-parse fails and assert an error is returned; then re-run the shipped
cells that pin the retained behaviours below and assert they are unchanged.

**Acceptance Scenarios**:

1. **Given** a dict-backed source whose re-parse fails, **When** a caller requests a handle,
   **Then** it receives an error carrying the wire failure, not a handle.
2. **Given** a **dict-free** source whose index build degrades, **When** a caller requests a handle,
   **Then** it still receives a handle, the view still reports the affected field as absent, and the
   degradation is still reported through the already-public build status — **retained, pinned
   behaviour that this change deliberately leaves alone**.
3. **Given** a span that frames to nothing — including a zero-byte span — **When** a caller
   requests a handle, **Then** it still receives a handle over an empty view, exactly as today.
4. **Given** any successful case, **When** a caller reads the handle, **Then** the read surface is
   unchanged: no new status accessor exists to call.

### Edge Cases

- **A refusal that is safe to refuse.** Story 1's guard refuses two classes that would have been
  harmless — removing a tag that is absent while a builder is open, and removing a tag positioned
  after every live group entry. This is accepted deliberately: a guard keyed on the erased position
  cannot repair the case where the **group entry itself** is the target. The ledger row states the
  guard's true width.
- **Several builders open at once.** A second top-level group builder can be opened while a first
  is still open; strict ordering binds only at the end call. The refusal covers the whole stack, so
  the number of open builders is not a case split.
- **The out-of-range refusal is expected to be unreachable.** Once Story 1's guard lands, no shipped
  path reaches an out-of-range index. The bounds refusal is **defence in depth**: its value is that
  the outcome is a **defined error** instead of undefined behaviour, and its status is *assessed*,
  not claimed as exercised.
- **`'='` is a policy floor, not a grammar rule.** The engine's own scanner splits a field at the
  **first** `'='`, so `372=A=B` parses as one field whose value is `A=B`. The refusal is a
  conservative compatibility and security floor inherited from the credential guard — it must be
  documented as a policy, not as a syntactic necessity.
- **Configured values that are structurally non-injectable stay untouched.** The default
  application-version member reaches the wire only through a fixed enumeration-to-literal mapping,
  and the sub-ID fields a reader expects to find **do not exist as configuration members at all**.
  The sweep enumerated the configuration members rather than sampling them, which is the only reason
  that can be said.
- **RefMsgType(372) emitted from an inbound value is not in scope.** Two emission sites take their
  value from an inbound frame rather than from configuration; they are a checked negative on the
  condition that the inbound scanner terminates a non-Data field at SOH. What a **counterparty**
  parser does with a surviving `'='` is not measured and not claimed.
- **A non-allocation exception on the clone path.** It today returns
  `FIXPP_ERR_CAPI_CONFIG_INVALID`; after this change it terminates the process after a fatal log,
  matching every other steady-state C-ABI symbol. ⚠️ **Whether such an exception can be produced at
  all on that path is undecided** — the ledger row declares the change anyway rather than letting a
  limb change behaviour while its row says it did not. If no such exception can be constructed as a
  test seam, the declaration and ledger row still stand on the shape of the code (a narrowed inner
  `catch` beneath a matching outer one) rather than on a demonstrated trigger, and any resulting
  uncovered line is dispositioned per `[const §IX.1]` at implementation time.
- **Two distinct out-of-memory arms on the clone path, not one.** US3 scenario 2's
  `FIXPP_ERR_UNKNOWN` is the dict-backed re-parse's own allocation failure (FR-006). A **separate**,
  pre-existing arm — `std::bad_alloc` raised during the clone's own construction (copying the frame,
  allocating the clone shell or its arena) — is unrelated to the re-parse and keeps returning
  `FIXPP_ERR_CAPI_CONFIG_INVALID`, preserved and narrowed by FR-008's exception boundary, not
  widened by it.
- **Bytes `0x7F` and every byte `≥ 0x80` are deliberately accepted by the floor**, not merely
  untested — widening the floor to exclude them is the separate decision recorded as out of scope
  above.

---

## Requirements *(mandatory)*

Each requirement carries its track. **C-ABI** requirements are governed by `[const §X.7]`;
**C++** requirements are not.

### Functional Requirements

- **FR-001** *(C-ABI)*: `fixpp_msg_remove_tag` MUST return `FIXPP_ERR_INVALID_HANDLE` and erase
  nothing whenever any group builder is open on the message. Every open builder MUST remain usable
  after the refusal — its add-entry, field-set and group-end calls continue to succeed — as a
  post-condition distinct from SC-001's byte-identical committed payload.
- **FR-002** *(C-ABI)*: that refusal MUST be keyed on **a builder being open**, not on the erased
  tag or its position, so both failure modes — a shifted index and an erased group entry — are one
  predicate.
- **FR-003** *(C-ABI)*: `fixpp_msg_remove_tag` with no builder open MUST behave exactly as today.
- **FR-004** *(C-ABI)*: every group-index and instance-index dereference reachable from a C-ABI
  entry point MUST become a **defined refusal** (`FIXPP_ERR_INVALID_HANDLE`) instead of an
  out-of-bounds read. Three reachability classes exist and the delivered shape MUST discharge all
  three; the classes are the requirement, the members are a reading: **(1)** a subscript inside a
  resolver's own body; **(2)** a direct subscript inside a C-ABI entry point that no resolver
  covers; **(3)** an immediate dereference of a resolver's result, which becomes a new
  null-dereference site unless the resolver's failure is propagated through it.
- **FR-005** *(C-ABI)*: `fixpp_msg_clone` MUST return an error, and no clone handle, when the
  dict-backed re-parse of its source fails — instead of returning a dictionary-free clone with
  `FIXPP_ERR_OK`. On this and every other refusal arm, `*clone_out` MUST be set to `NULL` and the
  **source** handle MUST remain unchanged and usable — no clone was constructed and nothing was
  consumed from it.
- **FR-006** *(C-ABI)*: that error MUST be the caller-visible translation of the underlying wire
  failure, yielding a fan of **three already-published codes**: `FIXPP_ERR_UNKNOWN` for
  out-of-memory, `FIXPP_ERR_WIRE_LIMIT_EXCEEDED` for capacity and range failures, and
  `FIXPP_ERR_WIRE_INVALID_FRAME` for malformed-field failures. These three are the re-parse failure
  routes reachable **today**; the mapping is `translate()`'s existing total switch over
  `fixpp::core::error`, not a closed list this feature owns — a future route added to that switch
  translates through the same mechanism without further change here.
- **FR-007** *(C-ABI)*: **no new error code is minted.** The out-of-memory route surfacing as
  `FIXPP_ERR_UNKNOWN` is existing documented behaviour (**L-049-2**) and MUST be disclosed as such,
  not presented as new. Minting a code would move a frozen header, an audited oracle, an
  append-only history file and a downgrade table — the files fixpp#449/#450 exist to
  single-source — and 1.7 would stop being a pure semantics bump.
- **FR-008** *(C-ABI)*: a non-allocation exception raised on the clone path MUST terminate after a
  fatal log rather than returning `FIXPP_ERR_CAPI_CONFIG_INVALID`. The ledger row MUST state that
  its trigger set is **not enumerated**.
- **FR-009** *(C++)*: a message handle obtained from a **dict-backed** source whose re-parse fails
  MUST be reported, before the handle is handed to the caller, as an error carrying the **wire
  failure that caused the re-parse to fail** — through the existing error channel.
- **FR-010** *(C++)*: FR-009 MUST NOT change three retained behaviours: a **dict-free** source whose
  index build degrades still yields a handle and still reports through the already-public build
  status; a span that frames to nothing still yields a handle over an empty view; and no new status
  accessor is added. Shipped cells pin each of these — re-derive them from the entry point's call
  sites rather than from any list.
- **FR-011** *(C-ABI)*: `fixpp_session_config_set_comp_ids` and
  `fixpp_session_config_set_begin_string` MUST return `FIXPP_ERR_CAPI_CONFIG_INVALID` when a value
  contains any byte below `0x20` (SOH included) or `'='`, and MUST leave the configuration
  unchanged. For `set_comp_ids`, the refusal MUST be **atomic across both arguments**: a valid
  sender paired with an invalid target MUST leave neither stored. The code is chosen because
  **every sibling setter already returns it**, including their existing null and empty refusals.
- **FR-012** *(C++)*: opening a session MUST apply the same refusal to SenderCompID, TargetCompID,
  BeginString and **each configured RefMsgType(372) entry**, before any message is emitted,
  returning `core::error::invalid_session_config`.
- **FR-013** *(both tracks)*: the rule MUST have **exactly one definition**, callable from
  configuration validation before any session exists as well as from session open. The charset is
  the **existing credential floor, unchanged**: widening it is a separate decision with a separate
  blast radius. It MUST be named and documented as a **policy floor**, not as a statement of FIX
  grammar. Replacing the credential guard's existing function-local lambda with a call to this one
  definition MUST NOT change the credential refusal's observable behaviour — its return code and
  refusal condition stay exactly as shipped.
- **FR-014** *(C-ABI)*: the C-ABI version MUST move **1.6 → 1.7**, a MINOR bump **marked
  BREAKING**, with no constitutional amendment. The version macro's trailing comment is part of the
  pin and MUST be re-authored, not renumbered.
- **FR-015** *(C-ABI)*: the BREAKING marking MUST appear in **all three** places
  `[const §X.7]` obligation 2 requires: the documentation of each affected declaration — the three
  refusing calls' own declarations, FR-001's, FR-005's and FR-011's (or the version header's comment
  where no declaration carries the change) — the **PR description**, and the behaviors-and-limitations
  delta. **FR-004's index-bounds declarations are excluded from this marking**: the arm they replace
  is undefined behaviour, not a documented success, so no call there "used to succeed and now fails."
  ⚠️ The PR-description limb MUST be its own `##` **heading** —
  bold text under another heading has already failed a gate in this repository and sent every
  downstream tier red.
- **FR-016** *(both tracks)*: the ledger delta MUST carry **four behaviour rows and no limitation
  row** — one for the remove-tag refusal (stating the guard's true width, including the
  refused-but-safe classes), one for the clone refusal (naming the three codes, referencing
  L-049-2, and declaring the termination limb), one for the C++ handle refusal (naming the **one**
  new failure class and **exactly what is not covered**), and one for the configured-byte refusal
  across both surfaces. The "no limitation row" clause bounds only these four mandatory rows: the
  two out-of-scope residuals named below (the probe-cap degradation and RefMsgType(372) at the two
  inbound-fed reject builders) MUST additionally be recorded as their own limitation row apiece in
  the same live ledger — leaving either unrecorded there is a Gate B defect.
- **FR-017** *(C-ABI)*: every version pin outside the version header, and the byte-level freeze
  manifest for each header whose bytes change, MUST move in the same change. Documentation edits
  required by FR-015 are **byte** edits, so headers that carry only a comment change still
  re-baseline. Markings that correctly date the **previous** bump MUST NOT be re-dated.
- **FR-018** *(both tracks)*: **all three refusals ship in ONE PR**, with **every in-repository
  consumer** — the Python binding, tests, examples and interop harnesses — updated in that same PR,
  per `[const §X.7]` obligation 3. This includes a consumer that uses one of the three refused calls
  only as fixture **setup** — such a call is still broken by a refusal if it happens to run in a
  state the guard now catches, and must be counted.
- **FR-019** *(documentation)*: the C-ABI owner document's scope claims that this change falsifies
  MUST be amended at **every passage that binds the affected error code to a producer set** — a
  passage that merely mentions it in a count, a changelog or a hedged list is not amended — stated
  as a **condition** rather than as an enumeration; an enumeration is the shape four consecutive
  review rounds have falsified. The *"CI grep enforces"* sentence MUST be deleted in the same
  paragraph (C-2). The PR **closes fixpp#488**.
- **FR-020** *(documentation)*: the **two derived restatements outside** that document — the
  contract distillation's code-scoping parenthetical and the Python-binding design's
  construction-failure-modes limb — MUST be amended in the same PR, on the separate ground that each
  names its source. ⚠️ **The two populations MUST NOT be summed, and no total may be written**
  (OD-1). The positional citation in the second one MUST be replaced with a content-keyed citation in
  the same edit: the row it names is not the row a reader resolving that ordinal lands on, and a
  positional index is a **result**, which nothing ever re-runs.

### Key Entities

- **The three refusing calls** — `fixpp_msg_remove_tag`, `fixpp_msg_clone`, and the pair
  `fixpp_session_config_set_comp_ids` / `fixpp_session_config_set_begin_string`: the only C-ABI
  symbols whose **BREAKING** behaviour changes (FR-004 separately gives further declarations a new
  *defined, non-breaking* return code; see FR-004/FR-015). **None of the three changes signature,
  export macro or reentrancy class** — only their return-code behaviour in the newly refused states.
- **Dict-backed vs. dict-free** — a message handle is *dict-backed* when its view was built by
  re-parsing the frame against a loaded dictionary (membership and type information available);
  *dict-free* when built without one. FR-009 and FR-010 turn on this distinction.
- **The reused error codes** — `FIXPP_ERR_INVALID_HANDLE`, `FIXPP_ERR_CAPI_CONFIG_INVALID`,
  `FIXPP_ERR_UNKNOWN`, `FIXPP_ERR_WIRE_LIMIT_EXCEEDED`, `FIXPP_ERR_WIRE_INVALID_FRAME`: all already
  published; none is minted here.
- **The C-ABI version contract** — the version macros, their in-tree pins, and the byte-level freeze
  manifest that hashes the published headers.
- **Configured session strings that reach the wire verbatim** — SenderCompID, TargetCompID,
  BeginString and each supported message type's RefMsgType(372); the two credential fields are
  already guarded and are the pattern being extended.

---

## Success Criteria *(mandatory)*

Every criterion below is a **return code a caller can check** or a **byte sequence a test can
compare**, not an internal invariant.

- **SC-001**: the two-arm sequence recorded in fixpp#447 — identical builds differing only by one
  `fixpp_msg_remove_tag` call — produces **byte-identical committed payloads**, and the removing arm
  returns `FIXPP_ERR_INVALID_HANDLE`. ⚠️ A clean sanitizer run is **not** the instrument: the
  accumulator is arena-backed, so the committed byte string is.
- **SC-002**: `fixpp_msg_remove_tag` with **no** builder open still returns `FIXPP_ERR_OK` and the
  tag is gone — the positive baseline that proves the guard discriminates rather than refusing
  everything.
- **SC-003**: a C-ABI entry point handed an out-of-range group or instance index returns
  `FIXPP_ERR_INVALID_HANDLE`. Reached through an arrangement that presents an out-of-range index
  directly, since FR-001's guard is expected to make the public path unreachable once it lands (see
  Edge Cases).
- **SC-004**: cloning a message whose dict-backed re-parse fails returns a **non-OK** code and
  yields **no handle**; no caller can obtain a clone that reports a field the source resolves.
- **SC-005**: each of the three clone failure modes returns its stated code — `FIXPP_ERR_UNKNOWN`,
  `FIXPP_ERR_WIRE_LIMIT_EXCEEDED`, `FIXPP_ERR_WIRE_INVALID_FRAME`.
- **SC-006**: `fixpp_session_config_set_comp_ids` and `fixpp_session_config_set_begin_string` return
  `FIXPP_ERR_CAPI_CONFIG_INVALID` for a value containing a literal SOH byte, and the previously
  configured value is still in effect.
- **SC-007**: a session configured with such a value **never emits a frame**; opening it fails.
- **SC-008**: the Python binding exposes the same refusal — a value containing SOH (not NUL)
  produces the error rather than a successful call. NUL is excluded because a C string cannot carry
  an embedded NUL to this call at all — the byte scan never sees it — so SOH is the byte that
  exercises the floor.
- **SC-009**: a C++ caller requesting a handle over a dict-backed source whose re-parse fails
  receives an **error**; the same caller over a dict-free source whose index build degrades still
  receives a **handle**, and the shipped cells pinning that degradation and the frames-to-nothing
  case are **still green and unedited**.
- **SC-010**: a version query reports **1.7.0**, and every in-tree version pin (including the
  composite value and any test whose **name** encodes the version) reports the new value. These fail
  at test time, not build time — there is no compile-time version assertion.
- **SC-011**: the error-code enumeration, the append-only code history and the exported-symbol
  golden are **byte-for-byte unchanged**, compared against this PR's own merge-base — measurable
  proof that FR-007 held and that this PR does not collide with the error-taxonomy work.
- **SC-012**: the byte-freeze gate is observed in **both** states — **failing** after the header
  edits and **passing** after the re-baseline. ⚠️ A green result alone is consistent with a
  re-baseline applied before the edits and is not evidence.
- **SC-013**: the PR body carries a `##` **heading** naming all three changes as BREAKING, in
  fixpp#428's in-tree header-comment spelling (`(1.7, BREAKING)` / `Since 1.7 (BREAKING)`); the
  heading check is run locally before the PR is opened.
- **SC-014**: after the amendment, no live passage in the C-ABI owner document or in the two derived
  restatements binds the affected error code to a producer set that its measured producers falsify;
  re-derive by the amended criterion rather than by re-reading a list.

---

## Assumptions

- **No public release has happened**, so the pre-first-release clause of `[const §X.7]` governs and
  there is **no supported external consumer** of the four affected symbols. Measured with
  `gh release list --exclude-drafts` against a control that prints rows for another repository.
  ⚠️ **Whether any *unsupported* external use exists is not knowable from this repository** and is
  registered as such — an external checkout or a vendored copy leaves no trace here.
- **Every code this feature returns is already published**, so the version downgrade machinery needs
  no new row and 1.7 is a pure **semantics** bump (FR-007).
- **No other C-ABI entry point writes `SessionConfig::sender_comp_id`, `::target_comp_id` or
  `::begin_string`** — re-derive by searching `src/capi/` for writes to those members; a new route
  appears as a write outside the two setters. FR-011's C-ABI-side protection rests on this staying
  true.
- ✅ **`/analyze` and the user `/plan` sign-off are both DISCHARGED**, by different routes and each
  **PINNED to the state it covers**: the sign-off by the owner's act in session, pinned to `plan.md`
  at `d12d2270`; `/analyze` by a run through the canonical `spec-analyzer` executor on 2026-09-20,
  pinned to the artifacts at `505adafa` — **1 finding (F1, multi-site), 0 CRITICAL, 100 % coverage**
  (20 FR + 14 SC over 93 tasks), with F1 **remediated at `e9833610`, not deferred**.
  ⚠️ **A summary artifact claimed `/analyze` discharged long before any of this, and was wrong** —
  that claim rested on **no run**, it stays recorded because it is why the sweep that corrected this
  bundle exists, and the present state is **not** its vindication: different evidence, not the same
  claim turning out true. The rule it violated is unchanged, and is the rule the present state
  satisfies — a control's discharge is read from the artifact that owes it, never from a plan entry
  describing it. ⚠️ **Neither discharge extends past the state it is pinned to**: the artifacts have
  moved since (`e9833610`, `40f2bb92`) and implementation will move them again, so **T084 stands as a
  re-run obligation**.
- ⚠️ **Gate A did not converge; the label is `gate-a-waived` on two reasons.** No downstream step
  may treat this bundle as having a converged Gate A.
- ⚠️ **Only one pre-fix RED has been executed** — Story 1's, inside fixpp#447. Every other seam's
  RED requires writing a cell and **building**, which is governed by the constitution's
  resource gate (an agent must obtain the user's approval before a local build). They are registered
  as NOT MEASURED in the design note, not claimed.
- ⚠️ **The branch/feature-directory divergence is live** (header block): tooling reports a `BRANCH`
  field derived from a tracked pin, not from git. It currently resolves to this bundle, which makes
  the defect **silent**. fixpp#490 is **mitigated, not fixed** — read the branch from git.
- **This bundle does not depend on the error-taxonomy work (fixpp#449/#450).** If that work lands
  first and re-baselines the freeze, this work rebases; nothing here depends on its outcome.
- The design note's populations, derivations and controls are **taken as given**. This spec
  re-derives nothing and re-decides nothing.

---

## Normative References

- `.specify/447-458-452-capi-refusals.md` — the design authority for every decision,
  population and control behind this spec.
- `.specify/constitution.md` — `[const §X.1]` (mandatory Gate A on the C ABI), `[const §X.6]` (all
  four Appendix A controls on an ABI-affecting feature), `[const §X.7]` (pre-first-release breaking
  change: MINOR bump marked BREAKING, its four obligations), `[const §XVII.1]` (review trigger for
  the public C++ API), `[const §XVII.7]` (the local build resource gate).
- `.specify/api-contract.md` — §11 supplies the **definition** of a C-ABI breaking change; it
  explicitly routes the pre-first-release **procedure** to `[const §X.7]`.
- `.specify/2i-capi.md` — the C-ABI owner document amended by FR-019.
- `.specify/456-table-view-seal.md` — the precedent for recording `[const §X.7]` as **not engaged**
  on a C++-only break.
- `spec/behaviors-and-limitations.md` — the **live** ledger that FR-016's four rows land in.
  Resolved rows live in the closed file and are deliberately not grepped across.
- Issues: fixpp#447, fixpp#458, fixpp#452 (the work); fixpp#488 (closed by this PR); fixpp#487,
  fixpp#489, fixpp#490, fixpp#491, fixpp#492 (open, out of scope).

---

## Explicitly out of scope

Recorded here so that none of it is re-discovered as a finding against this change.

- **fixpp#487** — whether the construction-vs-steady-state split acquires the source-level construct
  it prescribes. This feature deletes the claim that CI enforces it (C-2); it does **not** close the
  gap.
- **fixpp#489** — whether the historical producers of `FIXPP_ERR_CAPI_CONFIG_INVALID` should be
  re-pointed at domain codes. This feature corrects what the documents **claim** about that code; it
  re-points nothing.
- **fixpp#491** — the occupancy checker's claim about what it verifies.
- **fixpp#492 — the partial-worker-launch behaviour** (one item, not two): a call that launches some
  but not all configured workers reports success, so **degraded is reported as nominal**. Filed, not
  fixed (C-3). Changing it is its own breaking change with its own witness and ledger row. ⚠️ Its
  site is shared with fixpp#489's unreachable-retry half — closing one must not be read as covering
  the other.
- **The `fixpp_strerror` string-text mismatch** between the owner document's lookup-table excerpt
  and the shipped string. It exists independently of the producer question and belongs to
  **fixpp#449/#450**, the error-taxonomy single-sourcing work. Letting it in would drag this PR into
  a reconciliation it must not carry. The excerpt's **producer parenthetical** is amended (FR-019);
  its **string text** is not.
- **The probe-cap degradation**: an occurrence left un-indexed while the build status stays
  successful, so a lookup reports a tag absent on a table that reports success. Out of scope because
  the parse **succeeds**, so this feature's fallback is never entered — it is a defect of a
  *successful* parse. Recommended: file separately. It must not be claimed as covered.
- **RefMsgType(372) at the two inbound-fed emission sites** — a checked negative, with its
  re-derivation condition recorded in the design note. What a counterparty parser does with a
  surviving `'='` is not measured.
- **The hardcoded derivation corpus, and the seam between the consumer derivation and the
  scope-claim derivation.** The instrument is **not repaired** (OD-1); the owner declined and widened
  only the outcome. Recorded as a deliberate scope decision.
- **The exact shape of FR-004's bounds check** — whether it lives in the index resolvers or at their
  callers is an implementation choice for `plan.md`. What is **not** open: the three reachability
  classes it must discharge, and the exported declarations through which it becomes observable.
- **Two further findings in the configuration surface** — a setter whose exception handler
  terminates, and a setter that discards its arguments. Both real, both found during this feature's
  sweep, neither a delimiter-injection surface.
- **Unifying the existing store-factory CompID validator** with FR-013's rule. It checks a different
  thing (path separators and a filename length bound) for a different reason. FR-013 adds a rule
  beside it.
- **Widening the refused charset** beyond the existing floor.
- **Re-assessing the no-allocation window for the eager materialisation** when the reify path
  acquires an in-window caller. Today it has none; the first change that adds one must re-run that
  assessment.
