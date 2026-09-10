# Specification Quality Checklist: Live QuickFIX interop fidelity — peer-side typed readback + dictionary-backed conversation

**Purpose**: Validate specification completeness and quality before proceeding to planning
**Created**: 2026-09-10
**Feature**: [spec.md](../spec.md)

## Content Quality

- [x] No implementation details (languages, frameworks, APIs)
- [x] Focused on user value and business needs
- [ ] Written for non-technical stakeholders
- [x] All mandatory sections completed

## Requirement Completeness

- [x] No [NEEDS CLARIFICATION] markers remain
- [x] Requirements are testable and unambiguous
- [x] Success criteria are measurable
- [ ] Success criteria are technology-agnostic (no implementation details)
- [x] All acceptance scenarios are defined
- [x] Edge cases are identified
- [x] Scope is clearly bounded
- [x] Dependencies and assumptions identified

## Feature Readiness

- [x] All functional requirements have clear acceptance criteria
- [x] User scenarios cover primary flows
- [x] Feature meets measurable outcomes defined in Success Criteria
- [x] No implementation details leak into specification

## Notes

Two items are left **unchecked** — deliberate, reviewed deviations, not oversights. They were briefly
marked `[~]`; that marker is not a recognised checkbox state, so any tool scanning this file would have
counted 14/14 and reported a full pass. Left unchecked, they register as what they are.

**"Written for non-technical stakeholders"** and **"Success criteria are technology-agnostic"**.
This is a systems-library feature whose entire subject matter is protocol-level fidelity between two
FIX engines. Its stakeholders are the maintainer and the downstream library consumer; there is no
non-technical audience for "the peer's dictionary-backed parse must report the decoded value of
tag 40". The repository's house style, visible in every prior spec (e.g. `088-firstframe-budget-timer-lifetime`),
anchors claims to source with `file:line` citations, and `CLAUDE.md` requires that any description of
current behaviour be verifiable against source rather than asserted. Writing this spec to the generic
template's non-technical register would have made it unverifiable, which is the failure mode the
repository's own guidance is written against.

The substantive half of both items **is** honoured: the spec states **what must be true**, never
**how to build it**.

- FR-003/FR-004 require a "structured readback record" in a format identical across both
  counterparties — they do not choose JSON vs YAML vs a delimited line, a file vs a socket, or a
  schema.
- FR-013 lists the evidence a row must carry — it does not specify the field names, the file, or the
  serialization.
- FR-005 requires a message identity sufficient for pairing — it does not pick the identifier.
- No class names, function signatures, or file layouts are invented anywhere in the spec.

The `file:line` citations that do appear are all references to **existing** code, cited as evidence
for claims about the current state of the tree (the Context table), never as prescriptions for new
code. That distinction is what keeps the "no implementation details leak" item a genuine pass.

**Validation performed**: every factual claim in the Context section was verified against source on
`main` @ `e798a0f9` before the spec was written — the four capability gaps, the one-directional
assertion asymmetry, the sentinel dictionary, the absent evidence field, and the absent full-matrix
workflow were each confirmed independently rather than taken from the tracker or from a summary. The
row-4d detail file's "not the plumbing" premise was found to be **false** and is corrected in the
spec rather than inherited.


## Re-validation — 2026-09-10, after `/speckit-clarify`

Re-evaluated every item against the updated spec. **14/16 → 14/16.** No item changed state: nothing
newly passes, nothing regressed.

Five clarifications were integrated (exact-set field comparison; `MsgSeqNum(34)`+direction as the
correlation key; digest pin + capability handshake; two-level cell/witness result shape with its own
completeness gate; ~~all three sanitizer configs~~ — **superseded**, see the round-1 note below). Each made
an existing requirement *more* testable rather than adding a new open question, and the circular
"field of interest" wording in FR-003/FR-006 that the first clarification targeted is gone.

~~The two unchecked items are unchanged in character: the clarifications added FIX-protocol vocabulary
(`MsgSeqNum(34)`, `PossDupFlag(43)`) and existing build-config identifiers (`normal`, `asan-ubsan`,
`tsan`), which are domain terms already used by the artifacts this feature edits — not newly-introduced
implementation prescriptions.~~ ⚠️ **STRUCK at Gate A round 2 — the present tense was false.**
`asan-ubsan` is **retired** by FR-021a: it named a preset enabling ASan only. The build-config identifiers
are now `normal`, `asan`, `ubsan`, `tsan`. The substantive point survives in the round-1 note below; only
the vocabulary was stale. No class name, function signature, file format, or transport is chosen anywhere
in the spec.

⚠️ One clarification resolved *against* the stated recommendation: the sanitizer axis was decided as
~~**all three configs**, not `normal` + `asan-ubsan`~~ — **superseded at Gate A round 1, see below**. That
is recorded in Assumptions with its cost stated plainly, because it pulls a first-ever TSan bring-up on the
paired live matrix into scope.

## Re-validation — 2026-09-10, after Gate A round 1

Re-evaluated every item against the rewritten bundle. **14/16 → 14/16** on the tick counts, but one tick
was **false when it was written** and is now true for the first time.

⚠️ **"All mandatory sections completed" was `[x]` while `spec.md` had no `Normative References`
section.** `[const §VI.5]` is unconditional and is a *presence* obligation, so that item could not honestly
have been ticked. This is the fifth bundle running to be caught on it at this gate (085, 086, 087, 088,
089), and 088's own checklist note said as much. The section now exists and is **non-empty** — 089 could
not use the *"the FIX set is empty"* discharge 086 and 087 used, because SC-007 names five catalogue rows
and each carries `[FIX50SP2] Single General Order Handling`. The tick now stands on something.

The sanitizer clarification is **superseded**: the axis is now **four configs** — `normal`, `asan`,
`ubsan`, `tsan` — because `asan-ubsan` named a harness config whose preset enables **ASan only**, so the
recorded "all three sanitizer configs" decision was taken on a false label and delivered two sanitizer
kinds. The run count moves 24 → **32**. The TSan bring-up cost is unchanged.

⛔ **The FR and SC counts that stood here are DELETED, not corrected.** They were stale (the file said 45
against a spec carrying more), and a hand-maintained count in a document is a result nothing re-runs —
correcting it only schedules the next round's finding. Derive them when they are wanted:

```bash
grep -o 'FR-[0-9]\+[a-z]*' spec.md | sort -u | wc -l
grep -o 'SC-[0-9]\+[a-z]*' spec.md | sort -u | wc -l
```

The additions are not new open questions — each closes an axis the bundle already gated on but could not express (the peer's `sent` record; `config`
on the witness row; the run/cell identity split; the `error:enospc` terminal state; the validation pair;
the declarative script; the two disk predicates; the corrected spurious-hit definition).

The two unchecked items are unchanged in character. The rewrite added FIX-protocol vocabulary
(`ApplExtID(1156)`, the header/trailer partition) and named two **existing** engine functions as evidence
for a structural rule — `isHeaderField` on both engines — rather than prescribing new code. No class name,
function signature, file format, or transport is chosen anywhere in the spec; where the rewrite *does*
name a serialization (base64 for non-UTF-8 bytes, FR-025), that is a **contract-level interoperability
decision between two independently-written emitters**, not an implementation choice — leaving it unstated
was the defect, because Java `String` and C++ `std::string` would have diverged.

## Re-validation — 2026-09-10, after Gate A round 2

Re-evaluated every item against the rewritten bundle. **No item changed state.** The tick counts are
deliberately not recorded — see the deletion note above; run the two `grep` recipes if a count is wanted.

The round-2 rewrite changed no item's character. It added no new implementation prescription: the values it
does fix are **contract-level interoperability decisions between two independently-written emitters**,
which is the same category as round 1's base64 decision and for the same reason — leaving them unstated
does not defer to implementation, it **diverges**:

- the **canonical header/body partition** (union of both engines' built-in lists ∪ the dictionary's
  `<header>` block) and tag `1156`'s disposition — the two engines already classify it differently, so
  "whatever each engine says" is not one format;
- the **canonical parsed-path sort order** for field entries — two engines' walks are not one order, so a
  committed cross-language golden fixture could not otherwise be byte-compatible for both producers;
- the **lowercase-hex SHA-256** script digest, computed by the shim and **recomputed** by each side over
  the file it opened.

Two items remain unchecked for the reasons already recorded, and their character is unchanged: this is a
protocol-fidelity feature between two FIX engines and has no non-technical audience.

⚠️ **One round-2 claim about this file was FALSE and is recorded so it is not "fixed" later**: the review
alleged a duplicated `R-4a` heading in `research.md`. It is single (`grep -c '^## R-4a'` returns 1); the
other occurrence is a prose forward-reference. A second alleged duplicate (`plan.md`'s `checklists/` entry)
is permanently **indeterminate** — the file was rewritten mid-review and the intermediate state is
unrecoverable, since the bundle files are uncommitted working-tree modifications.

## Re-validation — 2026-09-10, after Gate A round 3 + the post-exhaustion hand-edit + fresh loop round 1

⚠️ **The re-validation above stopped at round 2.** Round 3, the hand-edit and fresh-loop round 1 all landed
after it, and the *"No item changed state"* conclusion was never re-derived against them. It is re-derived
here.

**No item changed state.** The additions since round 2 are of the same character as rounds 1–2: each closes
an axis the bundle already gated on but could not express (the run ledger's join keys; the validation
pair's two referenced runs; the live-path charset arm; the third emitter; the typed tier's compile arm).
None adds an implementation prescription the spec did not already owe as a contract-level
interoperability decision. Tick counts are deliberately not recorded — see the deletion note above.

⚠️ **One statement in the dated sections above is now FALSE and is corrected HERE rather than rewritten
there** (those sections are records of the sessions they name, and this file's precedent is to strike in
place or append, never to rewrite history):

> *"contract-level interoperability decisions between **two** independently-written emitters"* — lines
> under *"after Gate A round 1"* and *"after Gate A round 2"*, and *"byte-compatible for both producers"*
> under the round-2 list.

**There are THREE emitters, not two**: the QuickFIX-cpp counterparty, the QuickFIX-J counterparty, and
**fixpp** — see `contracts/readback-jsonl.md` § *THE THREE EMITTERS*. The reasoning in those sections is
unaffected (an unstated cross-language decision diverges rather than deferring to implementation); only
the count was wrong, and it was wrong because fixpp's role as a **producer** of this format — not only its
consumer — was recognised after those sessions were written. C-7 is correspondingly **three-way**.

⚠️ **A second count in those sections is CORRECT and must not be "fixed" to three**: the *two files, one
per emitting process* rule. A cell pairs fixpp with exactly **one** counterparty, so the process count in
a run is two while the implementation count is three. `contracts/readback-jsonl.md` carries that
distinction as a table at its head.

⚠️ **One round-2 obligation of this file is now discharged differently.** The typed-accessor guard's
anti-vacuity arm is no longer a record-level witness at all — it is a **negative-compilation** arm, and
FR-018's spurious-hit obligation for *runtime* typed-accessor invocation is recorded as **structurally
unsatisfiable** with source evidence (`spec.md` § *Clarifications* → *Session 2026-09-10 (Gate A fresh
loop, round 1)*, and SC-003's named exception). That is a scoping decision with a proof, not an unchecked
item.

## Re-validation — 2026-09-10, after Gate A rounds 4–9

⚠️ **The re-derivation above stopped at fresh-loop round 1, and its stated basis therefore did not range
over the additions that followed** — which is the defect that paragraph was written to close, recurring
inside it (Gate A round 9, P2 #8). ⛔ **Appended, not rewritten**: the paragraph above is a record of the
session it names, and this file's precedent is to strike in place or append. Its round list is left alone.

**The additions since fresh-loop round 1, enumerated so the conclusion below has an operand:**

- the validation pair's **`authoritative`** discriminator, with truth conditions, a named writer and
  supersession semantics (`data-model.md` §10);
- **E-7a** — the pairs must exist and the conformance set must be complete;
- **E-7b** — at least one `validator-positive-control` pair must exist;
- **E-7c** — a pair's references must *still* be authoritative when the committed check re-evaluates them,
  **scoped in round 9 to `authoritative: true` pairs** (unscoped it contradicted §10's own supersession
  *iff* and would have put the committed ctest permanently RED);
- the typed-accessor compile arm restated as an **execution host** with the toolchain table deleted
  (round 8), plus round 9's conditions on the *invocation* — configure-time `try_compile` rather than a
  ctest case, a maven phase at or before `package` rather than surefire, neither behind a switch the host
  does not set, and an explicit `FATAL_ERROR` on the conjunction of result and diagnostic;
- the **user decision** that the counterparty is rebuilt and republished before 089's CI, with the compile
  arm gating that publish, and no new PR-time workflow;
- **C-12 / `typed_accessor_arm`** — the peer announces, in its `hello`, the arm version its own *build* was
  gated by, so that pinning a pre-arm image cannot satisfy FR-016b while attesting nothing.

**No item changed state, and here is the derivation rather than the assertion.** Every addition above is
either (a) a **gate over an entity the spec already owed** — the validation pair is FR-012a's and
FR-010a's, and E-7a/b/c only make its existence, completeness and freshness checkable rather than assumed;
or (b) a **statement of where an already-required check executes** — rounds 8 and 9 moved the compile arm
from a maintained toolchain list to a host plus an invocation condition, and added no new obligation on the
counterparty's behaviour. Neither class introduces an implementation prescription the spec did not already
owe as a contract-level interoperability decision, which is the criterion every re-validation above uses.

⚠️ **C-12 is the one addition that is a NEW peer-observable, and it is dispositioned rather than waved
past.** It adds a `hello` field, so it is a genuine interoperability decision between independently-written
emitters — exactly the class this checklist's *"no implementation details"* items already admit (the
charset rule, the canonical partition and the script digest are the precedents, all recorded above). It is
**not** a runtime witness of typed-accessor invocation, which stays recorded as structurally unsatisfiable;
`contracts/readback-jsonl.md` § *Witnesses this contract requires* carries that distinction normatively.
Item states are unchanged.

⚠️ **The two unchecked items are unchanged and unchanged for the same reason**: this is a protocol-fidelity
feature between two FIX engines and has no non-technical audience. Tick counts are still deliberately not
recorded — see the deletion note above.
