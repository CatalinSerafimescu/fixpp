# Phase 0 Research: Three C-ABI calls that must refuse — C-ABI 1.7, marked BREAKING

**Feature**: `specs/090-capi-refusals` | **Date**: 2026-09-20

> ⚠️ **NOTHING WAS RE-RESEARCHED AND NOTHING WAS RE-DECIDED HERE.** Every decision below was
> adjudicated in `.specify/447-458-452-capi-refusals.md` at **v0.10** — four Gate A rounds, a
> post-sign-off P1, a C-4 scoped delta review and two owner decisions. **That document is the design
> authority; this file transcribes and compresses it and does not supersede it.** Where the two
> could be read as disagreeing, the note wins and this file is the one to correct.
>
> ⚠️ **Citations here name a section or a symbol plus a short quoted phrase, never a line number.**
> A positional locator is a *result*, and nothing ever re-runs a document.

---

## How to read this file

Each entry is **Decision / Rationale / Alternatives considered**. The *Alternatives considered*
column is not padding: it records what the authority **rejected** and why. That half is historical
and does not rot, and it is the half a later reader needs in order not to re-open a settled
question.

**Two decision series exist and they are different things.** `O-n` are the owner decisions of
`## 0b Owner decisions (2026-09-20), not open for review`; `D-n` are the design decisions of §1–§3,
tabulated in *"§4 The decisions, in one table"*; `C-n` are the note's `## Clarifications` series,
which closes at **C-5**; `OD-n` are the two later owner decisions, which carry their own labels
precisely so they do not collide with the note's *"Corrections to this document's own inputs"*
table at the two numbers this bundle discusses elsewhere.

---

## 1. Owner decisions — O-1 … O-4

### O-1 — one C-ABI bump, `1.6 → 1.7`, declared BREAKING

- **Decision**: a single MINOR bump, marked **BREAKING**, under the pre-first-release clause of
  `[const §X.7]`. **No constitutional amendment is triggered.**
- **Rationale**: the premise is **measured, not assumed**. `[const §X.7]` names its own test —
  *"`gh release list --exclude-drafts` shows whether it has happened"* — and §0b runs it with a
  **positive control on a different repository**, because an empty listing and a broken invocation
  both print nothing. The first public release has not happened, so the clause governs: a breaking
  change *"bumps `FIXPP_C_ABI_VERSION_MINOR`, not MAJOR, so the error-code downgrade frame of §4
  stays continuous."*
- **Alternatives considered**: a **MAJOR** bump plus a constitutional amendment — the consequence
  `api-contract.md` §11 states for its own C-ABI effects. **Rejected by §11 itself**, which routes
  the pre-first-release case away from its own remedy: §11 supplies the **definition** of a C-ABI
  breaking change, `[const §X.7]` supplies the **procedure**. §0c records that citing §11 for the
  procedure would be wrong.

### O-2 — #452 refuses at the two C-ABI config setters, not only at `Session::open`

- **Decision**: `fixpp_session_config_set_comp_ids` and `fixpp_session_config_set_begin_string`
  refuse the injected bytes themselves.
- **Rationale**: fixpp#452 filed that half as **optional** solely because it is a declared breaking
  change. O-1 removes the objection. Refusing at the setter is what makes the defect visible to a
  **C** consumer at all.
- **Alternatives considered**: guard **only** at `Session::open` (the C++ half). **Rejected**: a C
  consumer would then configure a forging value successfully and discover it at open time, or never,
  since both engine call sites discard `Session::open()`'s result and `fixpp_session_open` never
  calls it (§5c, §3.4).

### O-3 — all three issues ride ONE bump

- **Decision**: one version move covers #447, #458 and #452.
- **Rationale**: three semantics changes to one versioned contract, landing together, are one
  contract revision.
- **Alternatives considered**: a bump per issue. **Rejected**: it would move the version macro,
  its trailing comment and the freeze manifest three times for one revision.

### O-4 — all three refusals ride ONE PR

- **Decision**: one pull request. ⚠️ **A SEPARATE decision from O-3 and not readable off it** —
  O-3 settles that there is one *bump*, O-4 that there is one *pull request*.
- **Rationale**: the binding ground is **`[const §X.7]` obligation 3**, which requires every
  in-repository consumer updated *"in the same PR"* as the breaking change. §5d derives **one**
  consumer inventory shared across the three refusals — the same `tests/capi/` translation units,
  the same build registration, the same live-documentation rows. The version bump, the version
  comment, the freeze re-baselines and the design-corpus amendment are **shared infrastructure**.
- **Alternatives considered**: three PRs, one per issue. **Rejected on the clause, not on
  convenience**: a split must either **duplicate** the shared inventory in each PR or leave one PR's
  consumers unupdated **at its own merge**, which is what obligation 3 forbids. A secondary cost is
  that the later PRs would depend on an unmerged earlier one.

---

## 2. fixpp#447 — the remove-tag refusal

### D-1 — refuse while any builder is open; do not re-index

- **Decision**: `fixpp_msg_remove_tag` returns an error and **erases nothing** whenever the
  accumulator's open-builder stack is non-empty. The guard is keyed on **the builder stack being
  non-empty** — not on the erased position, not on the erased tag — so failure mode (a) *a shifted
  index* and mode (b) *the erased entry IS the group* are **one predicate** that no tag choice can
  defeat.
- **Rationale**: three **structural** facts decide it, each following from the shape of the code
  rather than from a census, so none of them rots.
  1. The open-builder stack is both **necessary and sufficient** as the population of live index
     holders: a closed builder is inert (`fixpp_msg_group_end` clears its open flag and
     `check_builder` refuses on it before any resolution; `check_entry` delegates to
     `check_builder`), and a *nested* builder resolves through its root, which is on that stack.
  2. Re-indexing **cannot repair mode (b)**: when the erased entry *is* the group, there is no
     correct value to renumber to. It would have to detect that case and refuse anyway — two
     behaviours for one call, and the caller cannot predict which it gets.
  3. Re-indexing's write set is **larger than it looks**: `fixpp_msg_group_begin` does not refuse
     while a builder is open, so several roots can be live at once and each above the erased
     position must be adjusted — a loop with an off-by-one per element, on a path §1.2 proves is
     invisible to every sanitizer in the matrix.
  ⚠️ **The cost is stated at its true width, and v0.1's wider claim is withdrawn.** Two classes
  that are safe today are refused: an **absent tag with a builder open** (documented today as
  *"Idempotent (absent returns OK)"*, so a caller relying on that idempotence loses it) and a
  **present tag positioned after every live root's group entry**. Uniformity is chosen over
  precision deliberately: *"One predicate that over-refuses in two enumerable, harmless ways is a
  smaller contract surface than two predicates that under-refuse in ways nobody can enumerate."*
  The migration is **one move** for every refused class — put the removal before the group begins
  or after it ends.
- **Alternatives considered**:
  - **Re-index** (the issue offers it): **rejected** on fact 2 above, with fact 3 secondary.
    Breaking-ness does not discriminate — under `[const §X.7]` both options are breaking — but
    **predictability does**: re-indexing changes the *meaning of a successful call* (indices move
    silently under the caller) where refusing changes only *which calls succeed*.
  - **A narrow guard** — find the tag first, return OK if absent, refuse only when the erased
    position is at or before some live root index: **rejected**. Its outcome depends on the caller's
    tag argument and on the accumulator's current layout, which is exactly the unpredictability
    re-indexing was rejected for; and it is two predicates (a *position* test for mode (a), a *tag*
    test for mode (b)), each with its own edge, on the sanitizer-invisible path.

### D-2 — the error code is the existing invalid-handle code

- **Decision**: `FIXPP_ERR_INVALID_HANDLE`.
- **Rationale**: the **one existing precedent for this exact condition** in the C ABI is
  `fixpp_msg_commit`, which already refuses on a non-empty open-builder stack with that code —
  *"An open (unended) group builder ⇒ not a sealed, committable state."* Two symbols then answer
  the same question with the same code, and **no new enumerator is minted** (see D-3 for the cost
  that avoids).
- **Alternatives considered**: minting a dedicated code. **Rejected** on D-3's cost argument, which
  applies identically here.

### D-2b — the out-of-range arm becomes a defined refusal, at three reachability classes

- **Decision**: independently of D-1, **every raw dereference of a group or instance index that is
  reachable from a C-ABI entry point** becomes a bounds-checked, defined `FIXPP_ERR_INVALID_HANDLE`
  refusal rather than an out-of-bounds read. The requirement is stated as **reachability classes so
  that a new site cannot hide between them** — the classes are the claim, the members are a reading:
  1. the two **resolver bodies**;
  2. the **direct subscript inside a C-ABI entry point** — `fixpp_entry_set_data`'s instance
     subscript, which no resolver covers;
  3. the **immediate dereferences of a resolver result**, through which the new failure must
     propagate rather than become a null dereference — `builder_context`'s use, `resolve_group`'s
     own recursion, and `resolve_instance`'s call to `resolve_group`.
- **Rationale**: *"This is defence in depth, not the fix"* — after D-1 no shipped path reaches an
  out-of-range index. `[const §IX.1]` permits three dispositions and this one is **assessed**, with
  the assessment written at the site. Stating the status honestly is the obligation; omitting the
  check is not the alternative.
- **Alternatives considered**: scoping the check to **the two resolver bodies only** (v0.2's
  mechanism). **Rejected**: class (2) is not covered by any resolver, and class (3) is why the
  change is not local to the resolvers even after class (2) is covered.
- ⚠️ **What is deliberately left open for `plan.md`**: the two resolvers are `noexcept` and return
  raw pointers, so the refusal **cannot be a return value from them** — it must be a checked
  precondition at their callers, or the functions must change signature. §7 leaves that **shape**
  open as an implementation choice. **The constraint that is not open**: whichever shape is chosen
  must discharge **all three** classes, and a null-returning resolver discharges class (3) **only
  if** `builder_context` and the two internal uses are rewritten with it. The **observable
  declaration population** is a design fact and is derived in §5a, not deferred.

---

## 3. fixpp#458 — the clone refusal and the C++ handle

### D-3 — reuse the existing translation; mint no error code

- **Decision**: `fixpp_msg_clone` stops **discarding** the re-parse error and returns the C-ABI
  translation of it. The result is a **fan of three already-published codes**, not two:
  `FIXPP_ERR_UNKNOWN` for out-of-memory, `FIXPP_ERR_WIRE_LIMIT_EXCEEDED` for capacity and range
  failures, `FIXPP_ERR_WIRE_INVALID_FRAME` for malformed-field failures.
- **Rationale**: the error is **already computed and thrown away at both sites** — *"`parsed.error()`
  is discarded at both sites. It is never inspected. That is the design lever."* `translate()` is
  already **total** over the core error type (*"Total switch, no default"*), with `-Wswitch` as the
  enforcement and an audited CSV oracle. Routing the discarded error through it costs nothing new.
  **Consequences accepted explicitly**: the OOM route surfaces as `FIXPP_ERR_UNKNOWN`, which is
  **documented v1.0 behaviour** recorded as limitation **L-049-2** — it is **referenced** in the
  ledger delta, never presented as new.
- **Alternatives considered**:
  - **Mint a new enumerator.** A slot exists, so this is a **cost argument, not a feasibility one**
    — and the cost is where this work would collide with the error-taxonomy work (fixpp#449/#450):
    a freeze re-baseline of the error header, an oracle update, an append-only history line carrying
    introducing-minor **7**, and an arm in the introducing-minor function whose own comment warns
    against the wrong fix. **Rejected**: 1.7 would stop being a pure **semantics** bump, and the two
    work streams would touch the same files.
  - **Reuse the dictionary OOM code for the allocation route.** **Rejected**: its *name* scopes it
    to dictionary load/reify, so `fixpp_msg_clone` would report a dictionary-subsystem failure for a
    **wire-parse** allocation failure.

### D-3b — a local, nested exception boundary inside clone's own body

- **Decision**: option **(c)** — a **nested** pair of handlers **inside `fixpp_msg_clone`'s own
  body**: an **outer** `catch (...)` that logs at fatal level and aborts, wrapping an **inner**
  `catch (std::bad_alloc const&)` that keeps returning `FIXPP_ERR_CAPI_CONFIG_INVALID` (preserving
  the 066 pin) beside the arm that takes D-3's translation. **`fixpp_msg_clone` stays a
  steady-state symbol and `[2i §5.2]`'s construction whitelist is NOT amended.**
- **Rationale**: the defect is the blanket handler's **WIDTH, not the code it returns** — a
  distinction v0.2 lost, and the reason its repair reclassified the whole symbol when narrowing one
  handler was the smaller true move. Writing the blanket return into the public header as it stands
  *publishes the blanket catch as policy*: every exception, including a foreign one, advertised as a
  retryable configuration result. The owner document places the symbol on the **steady** side twice
  and its prescribed disposition there is *"log the exception at fatal level … and `std::abort()`"*.
  ⚠️ **The mechanism was corrected at v0.4 against source**: the two named guard flavours have **no
  source-level implementation at all** — measured at zero hits across `src/`, `include/`, `tests/`
  and `bench/` against a **77-hit control on the same corpus**. What implements the steady side is a
  **hand-copied idiom, written per function and absent wherever nobody wrote it**. So v0.3's
  *"escapes to the steady guard"* delegated to nothing and, taken literally, would have **newly
  violated** the no-exception-crosses-`extern "C"` limb. Filed as **fixpp#487**.
- **Alternatives considered**:
  - **Reclassify the symbol as construction-time** and amend the whitelist (v0.2's move).
    **Rejected and withdrawn**, along with its whitelist amendment and its four-allocation
    enumeration: it moves a symbol the owner document names on the other side twice, to repair a
    defect that narrowing one handler repairs.
  - **Delegate to the named steady-state guard.** **Rejected on source**: the construct does not
    exist.
  - **Leave the blanket handler and merely document its return.** **Rejected**: *"documenting an
    existing return is not a change to behaviour"* is true of the **behaviour** and false of the
    **contract** — it converts an untested catch-all into a published guarantee, which under
    `[const §X.1]` is a contract change made without reviewing it against the contract.

### D-4 — the C++ handle materialises eagerly and refuses on exactly one condition

- **Decision**: the reify factory performs the frame and, when the dict-backed source is engaged,
  the parse **before returning**, and refuses through its **existing** error channel on **exactly
  one** condition — a **failed dict-backed re-parse**. The accessor keeps its signature, its
  `noexcept` and its `[[clang::lifetimebound]]`, and becomes a pure accessor over a populated cache.
  **No public status accessor is added.**
- **Rationale**: the refusal channel **already exists and is already used** — the factory is
  already `[[nodiscard]]` and expected-returning and already refuses on the deep copy. v0.1's
  rejection (*"the refusal would have nowhere to go"*) was true only because the factory **chooses**
  to defer the fallible work, in its own comment — an unadjudicated decision presented as a source
  fact. The scoping *"is not a hedge — it is what the source dictates twice over"*: the parser
  **checks the build status and propagates**, so a dict-backed table failure is already an error
  return; the dict-free two-argument view constructor **checks nothing**, so a dict-free table
  failure is a degraded view; and the framer on an empty span **returns success**, so a
  framed-but-empty result is not a failure the factory may invent one for. **The one arm that
  disappears is today's dict-free fallback reached by a dict-backed source whose re-parse failed —
  that is the whole of the behaviour change.**
- **Alternatives considered**:
  - **A three-state public status accessor on the handle** (v0.1). **Rejected and deleted**: those
    three states cannot report the framing-failure observable the same section used to reject a
    rival option, and the accessor cannot be simultaneously *non-allocating* and
    *post-materialisation*.
  - **"Nothing stays degraded"** (v0.2). **False and withdrawn**: a dict-free table degradation is
    **retained**, publicly observable through the already-public build status, and pinned by a
    shipped cell.
  - **Refusing on a framing failure too** (v0.3). **Withdrawn at v0.4 as a SCOPE reduction, not an
    enumerator choice**: ten shipped gtest cells in three files hand a default-constructed view to a
    generated dispatch entry point and assert a live handle, and the framer succeeds on that
    zero-byte span. ⚠️ *"No enumerator rescues an arm that reds ten shipped cells."* Choosing an
    enumerator would have fixed nothing; stopping the refusal on that arm makes the enumerator
    question stop existing rather than be deferred.
  - **Lazy materialisation with an error reported on first read.** **Rejected**: the error channel
    at first read does not exist, and adding one is the deleted accessor.

---

## 4. fixpp#452 — the configured-byte refusal

### D-5a — RefMsgType(372) is in scope, on the C++ track only

- **Decision**: the configured supported-message-type field is **in scope**. It has **no C-ABI
  setter**, so it rides the C++ track only and touches neither the version bump nor the freeze.
- **Rationale**: *"the issue itself asks for the sweep; omitting the field ships a fix that closes
  two of three doors and leaves the third open behind a document saying the sweep was done."*
- **Alternatives considered**: restricting the sweep to CompIDs and BeginString. **Rejected** as
  above.

### D-5b — one free predicate in a session-owned config-validation leaf header

- **Decision**: **one free function**, `[[nodiscard]] constexpr bool` over a string view, in a
  **session-owned config-validation leaf header** — not a lambda, not a method, and **not `core`**.
  It reports *"contains a byte this engine refuses in a configured FIX field value"*: any byte below
  `0x20` (SOH included) or `'='`. The existing `Session::open` lambda is **replaced** by a call to
  it, so the rule has **exactly one definition**, callable from configuration validation before any
  session exists and from session open.
- **Rationale**: the real reason is **OWNERSHIP**. The rule is not a byte-level primitive; it is a
  policy about what may appear in a **configured FIX field value**, whose authority is the session
  config and whose existing definition lives in `Session::open`. `core` is the protocol-independent
  leaf and would become the owner of a rule it cannot justify. A leaf header depending on nothing
  but `<string_view>` is reachable from the C-ABI config translation unit by a **permitted** layering
  edge and from the session implementation directly.
  ⚠️ **The advertised semantics change too.** The predicate must be named and documented as a
  **policy floor**, not as a statement of FIX grammar: fixpp's own scanner skips the **first** `=`
  before taking the value, so `=` **can** appear in a FIX field value as fixpp parses it. A name
  that says *forbidden configured byte* says what is true; a name that says *valid FIX field value*
  does not. **The charset is the existing credential floor, unchanged.**
- **Alternatives considered**:
  - **Keep the lambda and copy it.** **Rejected**: one rule, one definition.
  - **Put the predicate in `core`, on a layering argument** (v0.1). **The layering reason was FALSE
    against the clause it cited and is RETRACTED**: the allowed-edge whitelist **explicitly permits**
    the C-ABI → session edge, and the C-ABI config translation unit already includes session headers
    and stores a session config. `core` is rejected on **ownership** instead.
  - **Widening the refused charset** (all C0 bytes, or demanding ASCII-printable). **Rejected as a
    separate decision with a separate blast radius**: it would refuse values legal today. The
    looser-than-TagValue stance is already recorded elsewhere as *"a compatibility choice"*. This
    change **propagates** the floor; it does not raise it.
  - ⚠️ **The duplication figures v0.1 gave are DELETED, not corrected downward**, and **no
    replacement count is written**: a count here would rot and the argument does not need one.

### D-5c — the setters keep the sibling configuration code; nothing is re-pointed

- **Decision**: the two C-ABI setters return `FIXPP_ERR_CAPI_CONFIG_INVALID`, and the grouped
  translation arm for the invalid-session-config error is **NOT re-pointed**. Each declaration
  states **its own** refusal and its code and says **nothing about the other surface**.
- **Rationale**: **every sibling config setter already returns that code**, including their existing
  null and empty refusals; and validating early is what that file's own header comment asks for —
  *"setters validate cheaply (else defer to open/create)"*. A byte scan is cheap. The two
  declarations do not cross-reference each other because **no caller can compare them**.
- **Alternatives considered**:
  - **Re-point the invalid-session-config translation arm.** **Rejected, independently of the
    deleted claim below**: that arm is **grouped with an unrelated clock error**, so the change
    would silently re-point that error too — a broader C-ABI behaviour change than the three this
    work is scoped to.
  - ⚠️ **A cross-surface code-asymmetry note plus a limitation row `L-452-1`** (v0.1, opening *"This
    is design-decisive and it must not be discovered at Gate B"*). **DELETED — not narrowed, not
    hedged, not re-worded.** The claim is false for every field in scope, and **no consumer can
    observe the asymmetry it described**: both engine call sites discard `Session::open()`'s result
    and `fixpp_session_open` never calls it. *A limitation nothing can observe is not narrowed; it
    is removed from the ledger delta.* The deletion follows the repo's own five-round lesson that
    replacing a false claim with a **new** claim reproduces the defect.

---

## 5. The five clarifications — C-1 … C-5

| # | decision | rationale | alternative rejected |
|---|---|---|---|
| **C-1** | the design-corpus amendment rides this PR **in full**, at all eight `[2i]` passages | the eight are **the same false claim restated**, all falsified by the same measured producer population, and the fix is **prose-only** — it reds no pin, mints no enumerator, touches no source | a **minimum subset** or a **split**: leaving any subset live means the owner document still contradicts itself after a PR that claimed to fix exactly that |
| **C-2** | the *"CI grep enforces"* sentence is **DELETED in the same paragraph** | measured at **zero** references to the construct anywhere under the CI, tooling and build directories, with a different-pattern control **positive on the same corpus** | keeping the sentence. ⚠️ **Deleting the assertion that drift is caught is NOT closing the gap** — whether the constructs get implemented is **fixpp#487**, which stays open |
| **C-3** | the partial-worker-launch behaviour is **FILED, NOT FIXED** — fixpp#492 | stated at its true width: **DEGRADED is reported as NOMINAL**. The workers that did launch still drive the engine, so *"fails silently"* overstates it. Changing that return is its **own** `[const §X.7]` breaking change with its own witness and ledger row | folding it into this bundle. ⚠️ **Two issues share that one site**: #492 is strictly the partial-launch-reports-success half; the unreachable-retry half is **fixpp#489** |
| **C-4** | the v0.6/v0.7 delta gets **ONE SCOPED REVIEW**, then a label | a full re-read is not proportionate to a scoped delta | ⚠️ **It ran and returned 1 P1 / 0 P2 / 0 P3.** The `0 P1 / 0 P2` branch did **not** fire, so `gate-a-done` was **not earned**; the label is **`gate-a-waived`** and the P1 was applied. It was a **scoped review, not a round** — no round number is claimed for it |
| **C-5** | **ONE PR** for all three refusals | identical to **O-4** and not re-argued: `[const §X.7]` obligation 3's *"in the same PR"*, over one shared consumer inventory | a per-issue split: duplicate the inventory, or leave one PR's consumers unupdated at its own merge |

---

## 6. The two later owner decisions — OD-1, OD-2

### OD-1 — the design-corpus amendment widens beyond the C-ABI owner document

- **Decision**: **two derived restatements in other live documents** are amended in the same PR —
  the contract distillation's code-scoping parenthetical and the Python-binding design's
  *"Construction failure modes"* limb.
- **Rationale**: each **restates the owner document's claim while naming that document as its
  source**, and the contract distillation's own authority clause says it *"does not amend its
  sources, and on any conflict the source wins"*. The ground is therefore **separate** from the
  `[2i]` population's: those eight are bound by a criterion over passages that bind the code to a
  producer set; these two are bound because they **cite their source**.
  ⚠️ **TWO POPULATIONS, NEVER SUMMED, AND NO TOTAL IS WRITTEN ANYWHERE.** *Three*, then *eight*,
  were each falsified by the next pass, and a corrected count is the same defect at a new value.
  ⚠️ **No constitutional amendment is triggered**: the frozen rule governs surfaces marked *Stable
  from v1.0*, which this prose is not.
  ⚠️ **The positional citation inside the second restatement is replaced with a content-keyed one in
  the same edit** — the row it names is not the row a reader resolving that ordinal lands on, and a
  positional index is a **result**, which nothing ever re-runs.
- **Alternatives considered**:
  - **Amending only the owner document** (the pre-OD-1 scope). **Rejected**: it leaves two live
    documents restating a claim the same PR falsified at its source.
  - **Repairing the INSTRUMENT whose corpus is hardcoded to one file.** ⚠️ **The owner DECLINED the
    repair and widened only the OUTCOME.** Recorded in the note and in `spec.md` as a **deliberate
    scope decision, not an oversight** — the defect that hid this is the **seam** between a
    derivation by *symbol* (§5d) and a derivation by *scope claim* (§5c), and *"a file cleared by
    one was never seen by the other."*

### OD-2 — this work converts from issue mode to FEATURE mode

- **Decision**: the work becomes a `specs/<id>/` bundle. **This bundle is that conversion.**
- ⚠️ **Cited from `spec.md`'s `## Clarifications`, NOT from the design note — OD-2 is recorded in
  the spec and, in its own words, "is not in the design note at any version."**
- **Rationale**: it follows from **the controls**, not from preference. This is an **ABI surface
  change**, so `[const §X.6]` puts **all four** Appendix A controls on it; `/analyze` is a
  cross-artifact check over `spec.md` / `plan.md` / `tasks.md`, and **in issue mode none of those
  exist**, so the control could not be discharged at all. A feature bundle is the only shape in
  which the two controls that were **then** owed can run.
- **Alternatives considered**: staying in issue mode. **Rejected**: `/analyze` would remain
  permanently undischargeable.

---

## 7. What Phase 0 deliberately does NOT settle

Recorded so that none of it is re-discovered as a finding, and so that no later reader mistakes an
open item for a decided one.

- **The shape of the bounds check (D-2b)** — whether it lives in the index resolvers or at their
  callers. `spec.md` hands this to `plan.md`; **the three reachability classes and the exported
  declarations through which the refusal becomes observable are NOT open.**
- **Phase 1 artifacts** — `data-model.md`, `contracts/` and `quickstart.md` are Phase 1 outputs and
  are **not** produced by this phase.
- **The note's NOT MEASURED register (§8)** carries the pre-registered obligations that Phase 0
  cannot discharge: the local build behind `[const §XVII.7]`'s resource gate; the freeze hashes in
  **both** states; the per-call cost of eager materialisation and the bench impact of the byte scan
  (both `[const §VIII.2]` paired runs); whether a shipped fail-on-call ordinal still lands where it
  does once materialisation moves; whether an injectable non-allocation exception seam on the clone
  path **exists at all** (the candidate set **may be empty**, discharged by a **mutation** if not);
  and every seam's pre-fix RED except Story 1's, which was executed inside fixpp#447 itself.
  ⚠️ **A pre-registered NOT MEASURED item is a DISPOSITION, not an unresolved clarification.**
- **Out of scope and enumerated in `spec.md`**: fixpp#487, #489, #491, #492; the string-text
  mismatch belonging to the error-taxonomy work; the probe-cap degradation (a defect of a
  *successful* parse, so this feature's fallback is never entered); the two inbound-fed
  RefMsgType(372) emission sites (a **checked negative**, with its re-derivation condition recorded
  in the note — what a **counterparty** parser does with a surviving `'='` is not measured);
  unifying the existing store-factory CompID validator; widening the refused charset.

---

## 8. Normative references for this file

- `.specify/447-458-452-capi-refusals.md` **v0.10** — the design authority for every decision above.
  ⚠️ **`spec.md`'s header block and its Normative References pin it at v0.9**; the note itself
  carries a v0.9 → v0.10 revision in its convergence-log appendix. The **v0.10 content is what this
  file transcribes**; the stale pointer in `spec.md` is flagged, not edited here.
- `.specify/constitution.md` — `[const §X.1]`, `[const §X.6]`, `[const §X.7]`, `[const §XVII.1]`,
  `[const §XVII.7]`, `[const §XVII.8]`, `[const §IX.1]`, `[const §VIII.5]`, `[const §XIX.5]`.
- `.specify/api-contract.md` §11 — the **definition** of a C-ABI breaking change, which routes the
  pre-first-release **procedure** to `[const §X.7]`.
- `.specify/2i-capi.md` — the C-ABI owner document amended by FR-019.
- `.specify/456-table-view-seal.md` — the precedent for recording `[const §X.7]` as **NOT engaged**
  on a C++-only break, and for the inert-documentation-generation disposition.
- `specs/090-capi-refusals/spec.md` — the requirements input, and the sole record of **OD-2**.

