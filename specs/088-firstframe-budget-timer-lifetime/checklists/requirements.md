# Specification Quality Checklist: Bounded first-frame read — budget boundary + deadline-timer handler lifetime

**Purpose**: Validate specification completeness and quality before proceeding to planning
**Created**: 2026-08-04
**Feature**: [spec.md](../spec.md)

## Content Quality

- [x] No implementation details (languages, frameworks, APIs)
      — *Deviation, deliberate and recorded*: this is a defect-correction feature whose subject **is**
      two specific source constructs. The Context and Clarifications sections cite file:line and show
      the defective code, because the requirement "reject only when the budget is exceeded" is not
      meaningful to a reviewer without seeing what it is being corrected *from*. The **Requirements**
      and **Success Criteria** sections themselves stay behavioural. Same precedent as 086's spec.
- [x] Focused on user value and business needs — US1 is a live interoperability failure against a
      well-behaved counterparty; US2 is a silent teardown of an established session.
- [x] Written for non-technical stakeholders — to the extent a wire-protocol defect permits; each
      user story leads with the peer-visible symptom, not the mechanism.
- [x] All mandatory sections completed — **re-ticked at Gate A round 1.** This item was **false** when
      first ticked: `[const §VI.5]` (`.specify/constitution.md:164`) makes a **Normative References**
      section mandatory in every `/specify` artifact, and the spec had none. The same false tick was
      recorded and corrected at 085's Gate A round 1 for the same reason. The section now exists; the
      tick is honest.

## Requirement Completeness

- [x] No [NEEDS CLARIFICATION] markers remain — **3 markers resolved at `/speckit-specify`**
      (Q1/Q2/Q3) and **4 residuals resolved at `/speckit-clarify`** (C1–C4), all 2026-08-04, all
      recorded in the Clarifications section with their rejected alternatives.
- [x] Requirements are testable and unambiguous — **18 FRs** (FR-001…FR-018; FR-017 added at Gate A
      round 1 for the arm-once deadline invariant, **FR-018 at Gate A round 2** for the TLS
      OUT-mapping cancellation reset) each name an observable behaviour;
      FR-010 additionally requires the *test itself* be proven RED pre-fix. **Counts re-derived from
      the spec at each Gate A round, not carried from the pre-clarification draft.**
- [x] Success criteria are measurable — SC-001…SC-018; six carry an explicit "RED against pre-fix
      source" obligation, which is the measurement that this feature's evidence turns on. Four of
      those (SC-014's cells, plus B6/T2a and **SC-018's T6**) RED against **mutants of the delivered
      design** rather than against `main`, because the properties they pin did not exist pre-fix —
      stated per cell in research §D-6.7 and §D-6.10 rather than left to be discovered at Gate B.
- [x] **Every witness is capable of failing on the property it names** — added as an explicit item at
      Gate A round 2, because this is where round 1 passed and should not have. Round 2 found that the
      whole cancellation half of the suite drives `mock_transport`, whose read is a `steady_timer` and
      therefore honours `total`, while the production TLS read does not — so **every planned
      cancellation cell was green against the exact defect the feature had introduced**. SC-018
      clause 1 now requires a **real `ssl::stream`** for that leg, and SC-015 records that T2a must
      not be cited as evidence for it. Instrument-shares-the-property is not a scripting bug and no
      amount of mock tuning fixes it ([[feedback_verification_corpus_built_from_the_read_it_checks_is_blind]]).
      **Re-ticked at Gate A round 3, and it was FALSE when first ticked** — the item was right and the
      audit behind it was not deep enough. Round 3 found **three further instances of the same class**,
      in three different mechanisms: *(a)* **the construction did not exist** — B2, B5 and B6 specified
      shapes `mock_transport` cannot produce, and B6 was RED against the **delivered** design (D-9's
      overturned claim, D-6.11); *(b)* **the signal never arrived** — T2a and T6 `co_spawn` their
      subject with no reset, so `co_spawn`'s terminal-only initial state discarded the test's own
      `total` and T6 would have failed with FR-018 correctly present (D-6.12a); *(c)* **the mutant did
      not change what was asserted** — the `bare deadline arm` mutant returns the same error value as
      correct code, just later, so both claimed REDs in that column were false (D-6.12b). **Three
      rounds, three different ways for a witness to be unable to fail.** The generalisation now
      recorded in D-6.8: *a column is only as good as the constructibility of the cell claiming it —
      derive each construction against the actual test double and the actual delivered loop, never
      against the cell's own prose.*
- [x] **Every priced mechanism is sufficient for the witnesses that depend on it** — new at Gate A
      round 3. The D-9 ledger existed to stop **unpriced** surface entering the tree, and it did that;
      what it did not do was check the converse — that the mechanisms it *did* price could actually
      carry the cells. *"No `Script` field is added"* read as a virtue and functioned as a constraint
      the witness plan could not live inside. Two mechanisms (#5 chunked `inbound_chunks`, #6 per-read
      requested-size observable) were ledgered at round 3, and the check is stated so a future round
      runs it in both directions.
      **Re-ticked at Gate A round 4 — and round 3's answer was still incomplete.** Mechanisms 5 and 6
      made B5 *constructible*, but left the mutant's **termination** unexamined: a zero-length request
      returns a successful zero-byte completion, and although the loop yields each iteration the read
      arm is always recorded first, so the `room == 0` mutant **never terminates** and B5 would have
      hung rather than gone RED. **A cell must make its mutant FAIL, not merely be expressible** —
      that is the sharper form of this item. Closed for B5 by binding a non-zero `read_latency`
      (no new mechanism); **B4 carries the same exposure and the same obligation**. Recorded because
      the first-order check ("can the cell be built?") passed at round 3 while the second-order one
      ("does the mutant stop?") had never been asked.
- [x] Success criteria are technology-agnostic — they name peer-observable outcomes (frame returned /
      connection closed / slot reclaimed / sanitizer findings), not mechanisms. SC-005's
      reference to ASan is a measurement instrument, not an implementation choice. *Two were narrowed
      at Gate A round 1 (SC-001/SC-012, from "a session establishes" to the helper outcome) because
      the witness target cannot express the wider claim — see research §D-6.6.*
- [x] All acceptance scenarios are defined — 4 user stories, **10** scenarios (US1 3 + US2 3 + US3 2 +
      US4 2). *The pre-round-1 count of "8" was stale.*
- [x] Edge cases are identified — **11** listed, including the two the fix forks on (`== max_bytes`
      with and without a complete frame), the two that must NOT regress (slow-loris, `stop()`), and
      the zero-byte-completion case added at Gate A round 1. *The pre-round-1 count of "8" was stale.*
- [x] Scope is clearly bounded — the census table enumerates all four candidate sites and states why
      the `co_await`ed sites are excluded; FR-012 pins the empty public-surface delta and SC-017
      makes it checkable against the installed package.
- [x] Dependencies and assumptions identified — **8** assumptions (*the pre-round-1 count of "9" was
      stale*), including the three that would silently invalidate the work if false (single-executor
      model; the same-drain race not reproducing by chance — narrowed at Gate A round 1 to record that
      at the transport layer it cannot be *constructed* at all; the clamp preserving F-015-002 surplus
      carry at the boundary). **A fourth was missing entirely and is now stated as a requirement
      rather than an assumption**: that every buffer on the path is sized to the same cumulative bound
      (FR-013's carry-capacity clause) — its absence is what made SC-012 unsatisfiable under the
      first-draft design.

## Feature Readiness

- [x] All functional requirements have clear acceptance criteria — FR↔SC mapping is 1:many and
      complete; every FR is observable through at least one SC.
- [x] User scenarios cover primary flows — the two defects (US1, US2), the regression guard that the
      protective behaviour survives (US3), and the census class-fix (US4).
- [x] Feature meets measurable outcomes defined in Success Criteria
- [x] No implementation details leak into specification — see the Content Quality deviation above;
      the leak is confined to Context/Clarifications by design.

## Notes

- **Resolved 2026-08-04, in two passes.** Q1–Q3 (the blocking forks) were settled at
  `/speckit-specify`; `/speckit-clarify` then ran per `[[feedback_always_invoke_speckit_clarify]]`
  and surfaced four *residuals those decisions created* — C1 (the DoS bound Q1's reordering widened,
  now clamped tighter than the status quo), C2 (the `stop()` pin Q2's joined form owes), C3 (the
  deterministic seam the same-drain witnesses need), C4 (per-site witnesses for Q3's widened scope).
  All seven are recorded inline in spec.md §Clarifications with their rejected alternatives.
- **Requirement/criterion growth from `/clarify` is itself the signal**: FR-012 → FR-016 and
  SC-011 → SC-017. Three of the four residuals added obligations that would otherwise have been
  discovered at Gate B rather than at design time.
- **Gate A round 1 (2026-08-04) added a third pass.** Three further residuals (G-1, G-2, G-3) were
  recorded in `spec.md` §Clarifications and integrated into the FR/SC bodies, and **FR-017** was
  added. All counts in this checklist were re-derived from the spec rather than carried forward — the
  scenario, edge-case and assumption counts were each stale, which is the drift class this item exists
  to catch. Root causes and the disagreement record are in `plan.md` §Gate A.
- **Gate A round 2 (2026-08-05) added a fourth pass, and it is the one that found a shipped-design
  defect rather than a documentation defect.** Residual **G-4**: Q2's joined form does not retire
  under `Engine::stop()` on the **TLS** transport — the `total` is discarded inside asio's SSL
  composed operation — so `stop()` would have hung **unboundedly** on the default accept path, and the
  same signal destroys the pre-fix 5 s escape. Q2 is amended **(b) → (b+)**; **FR-018** and
  **SC-018** added; cell **T6** added (13 cells, from 12); research **§D-2a** and **§D-6.10** are new;
  the Article XI §2 row is re-passed and the Article VIII §3 disposition **re-derived** because its
  round-1 ground ("the accept path is cold") does not apply to FR-018's site. **Q2's locked decision
  is retained**, so no `/speckit-clarify` re-run is triggered. Two reviewer framings were corrected in
  the record while their conclusions survived — see `plan.md` §Gate A → *Round 2 — disagreements*.
- **The item this checklist did not have, and now does.** Round 2's finding was not reachable by any
  completeness or count check: the bundle was internally consistent, its citations resolved, and its
  witnesses were fully specified. What was wrong was that the **witnesses could not fail** on the
  property they named. That is why *"every witness is capable of failing on the property it names"* is
  now a Requirement Completeness item rather than a review habit.
- **Gate A round 3 (2026-08-05) — a fifth pass, run under an explicitly user-approved exception to the
  two-rewrite cap.** Three P1s, all of the same class as round 2's, none of them reachable by a count
  or citation audit: **C2** (the outer `co_spawn` swallows the test's `total`), **N1** (B2/B5/B6
  unconstructible with `mock_transport`; four mutant-matrix columns lost their only RED, including
  round 1's own `carry@max_bytes` headline; B6 was red against the delivered design), and **C3/N2**
  (the `bare deadline arm` mutant's stated signature is wrong and both of that column's claimed REDs
  were false). **No locked decision was reopened and no design changed** — Q1–Q3, C1–C4, the join, the
  epoch mechanism, the internal header and FR-018's OUT map all stand, and the Article VIII §3
  disposition was independently upheld by the judge on all three legs. What changed is the **witness
  plan**: two new priced mechanisms, three re-derived cells, an obligatory outer-spawn reset, two
  promptness thresholds, two positive initiation barriers, T6's missing build contract, and the
  N4 teardown leg **audited** rather than deferred. FR/SC counts are **unchanged at 18/18** and the
  cell count is **unchanged at 13**; round 3 added no requirement and no cell. Root causes and the
  disagreement record are in `plan.md` §Gate A → *Round 3*.
- **Two accounting corrections that are not about counts.** `plan.md`'s Constitution Check no longer
  reads *"PASS … with one recorded deviation"* — a knowing deviation from a MANDATORY article is not a
  PASS, and the required **Complexity Tracking** table is now filled per
  `.specify/templates/plan-template.md:106-112`, seeking Article XI §6 as a **waiver**. And the plan's
  *"No `include/` file is touched"* is corrected: exactly one is — the test-only
  `mock_transport.hpp` — which is excluded from the install set at `CMakeLists.txt:446-451`, so
  SC-010/SC-017 are unaffected. Neither is a design change; both were self-contradictions against this
  bundle's own text.
- The Content Quality "no implementation details" item is dispositioned rather than passed silently:
  a bug-fix spec that hides the bug is unreviewable. Flagged here so Gate A sees the deviation was
  deliberate.
