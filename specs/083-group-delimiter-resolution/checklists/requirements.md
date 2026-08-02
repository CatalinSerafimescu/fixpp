# Specification Quality Checklist: Group Delimiter Resolution

**Purpose**: Validate specification completeness and quality before proceeding to planning
**Created**: 2026-07-30
**Feature**: [spec.md](../spec.md)

## Content Quality

- [x] No implementation details (languages, frameworks, APIs)
- [x] Focused on user value and business needs
- [x] Written for non-technical stakeholders
- [x] All mandatory sections completed

## Requirement Completeness

- [x] No [NEEDS CLARIFICATION] markers remain — all 3 resolved in the 2026-07-30 clarification session
- [x] Requirements are testable and unambiguous
- [x] Success criteria are measurable
- [x] Success criteria are technology-agnostic (no implementation details)
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

### RESOLVED 2026-07-30 — all three markers settled in `/speckit-clarify` (4 questions asked)

| Was | Decision | Effect on scope |
|---|---|---|
| FR-018 | **Thread context into the construction path.** The construction-side check already recurses through nested instances and already has the message type, so it carries the ancestor path through that walk. | Neutral. No exported ABI signature changes (FR-018a). |
| FR-020 | **Declaration order wins unconditionally.** Interop gate is observational — it records divergence, never arbitrates it. No compatibility mode, no config surface, no hot-path branch (FR-020a). | **Narrows.** Removes a possible lenient-mode branch and keeps FR-012's pin carve-out-free. Also unblocks the feature despite `reference-engines/` being absent. |
| FR-021 | **Fully in scope** — the typed-read instance splitter is investigated and fixed here. | **Widens.** Added User Story 5 (P3), FR-021/021a/021b, SC-010. |
| *(4th question, not a pre-existing marker)* FR-006 | **Reject the load by default, plus an explicit opt-in tolerant mode.** | **Widens slightly.** Added FR-006a/006b and SC-011: a new loader option surface, both dispositions tested, and a precondition that all ten shipped dictionaries still load under the default. |

Net after `/speckit-clarify`: 21 → 28 functional requirements, 9 → 12 success criteria, 4 → 5 user stories.

### Gate A round 1 (2026-07-30) — additions, and why the "all 3 / 4 questions" reading was NOT changed

**Counts after the Gate A round-1 convergence pass: 28 → 38 functional requirements, 12 → 15 success criteria, 5 user stories (unchanged).** All additions are suffixed or appended — **nothing was renumbered**, so every existing cross-reference in `plan.md`, `research.md`, `data-model.md`, `quickstart.md` and the contracts still resolves.

> **Count corrected and re-derived 2026-07-31 (Gate A round 3, N25(a)).** The round-1 figure of **38** was already stale when round 2 added **FR-018b** without incrementing it — the round-2 table below lists FR-018b as an addition and the total was never touched. Recounted item by item from `spec.md`: **39 functional requirements after round 2** (23 base numbers + 16 suffixed), and **40 after the round-3 scope amendment adds FR-021e**; success criteria **15 after round 2**, **16 after round 3 adds SC-016**. Running totals, stated so the next reader does not have to recount: **28 → 38 (r1) → 39 (r2) → 40 (r3) → 42 (checklist audit)** functional requirements, **12 → 15 (r1) → 15 (r2) → 16 (r3) → 16 (checklist audit, unchanged)** success criteria, 5 user stories throughout (User Story 5 gained a third acceptance scenario at r3, which is not a new story). This project treats a small unreconciled count as a silent-omission detector; `plan.md`'s round-2 rewrite recounted its test-file totals for exactly that reason and this page was not given the same treatment until now.

| New | Closes | Landed in |
|---|---|---|
| FR-006c | the FR-006 throw must land inside both loader fuzz harnesses' documented exception sets (per-loader type) | research D-7, `loader_tolerant_mode.md` C-6.1b |
| FR-006d | "declared group, zero contexts" is not the fail-closed condition — reconciles FR-006/C-6.1 (per declaration) with C-1.4 (per context) | `loader_tolerant_mode.md` C-6.1a, `group_ctx_delims.md` C-3.6 |
| FR-007a, SC-013 | fixpp#210 Consequence 2 (extent swallow) as a named RED witness, not substituted | spec US1 scenario 5, quickstart §1a |
| FR-010a, SC-013 | fixpp#210 Consequence 1 (over-permissive membership) by direct negative witness, not by cardinality proxy | spec US1 scenario 4, quickstart §1a |
| FR-012a, SC-014 | 072's load-time collision guard reads a value this feature re-derives; L-063-4's audit was taken against pre-fix delimiters | research D-10, `loader_tolerant_mode.md` C-7.2 |
| FR-021c | the other three wire-derived delimiter derivations in `offset_table.cpp` are censused **with their true roles** and scoped out, incl. the `:597` arena-reserve assessment. *(Role column corrected at Gate A round 2: `:454` and `:526` each serve a membership probe **and** an instance-boundary rule; only the probe role is frozen by C-8.0, and the two boundary roles are scoped out on their own grounds by C-8.0b — which is also why this feature does not claim to close #180.)* | research D-6, `typed_read_splitter.md` C-8.0/C-8.0a/C-8.0b |
| FR-021d | FR-021a's characterisation starts from L-063-4 / #180, which the repo already holds | research D-6, `typed_read_splitter.md` |
| FR-023, FR-023a, FR-023b | the Entity-2 consumer lookup-miss disposition, previously **undefined**; enforced as a load-time invariant, no silent fallback, tolerant-mode interaction stated, and — because the invariant holds by *measurement* (the reconstructed `immediate_parent` chain could otherwise be a hybrid key) — a shipped-set precondition before the new fail-closed path is enabled | research D-11, `group_ctx_delims.md` C-3.4 / C-7.3 |
| SC-015 | Phase-1 exit becomes a **reconciliation** against 335/52/30/232, and names the pin (not the deleted scratch probe) as authority of record | research "Measurement provenance", plan Phase 1 |

Two new contracts were added for the surfaces `/clarify` widened onto but never gave design depth: `contracts/typed_read_splitter.md` (FR-021 family, SC-010) and `contracts/capi_group_grammar.md` (FR-018/018a/018b, SC-012).

**Gate A round 2 (2026-07-31) — the two contracts' central mechanical legs, added:**

| Item | How it is now resolved | Where |
|---|---|---|
| FR-018b | the route from the C-ABI's `const Dictionary*` to Entity 2 is **named**: a session-owned `table_view` built once at `fixpp_session_open`, not `dict->as_table_view()` at the check (barred by `[const §XV.1]` on the per-message commit path), with its lifetime, its null-disposition (= C-9.4 unchanged) and its exported-surface impact (none) stated | `capi_group_grammar.md` C-9.2a, W-11a/W-11b |
| FR-021c | the splitter census's **role column** is corrected — `:454` and `:526` each carry a boundary rule as well as a probe — and the two boundary rules are scoped out on their own grounds, with the residual and the non-closure of #180 recorded | `typed_read_splitter.md` C-8.0/C-8.0b/C-8.5, research D-6, spec FR-021c |
| FR-022 | perf scope widened from one hot path to **three** (inbound validate, typed read, C-ABI commit), each with its own bench | spec FR-022 / SC-009, plan Article VIII row + `bench/` tree |
| FR-023 | C-3.4a defines **how the checked set is computed** at `finalize()`, including the `!members.empty()` exclusion, and marks set-equality with the consumer's enumeration **conditional on D-12 resolving to branch (b)** | `group_ctx_delims.md` C-3.4a, C-7.3 |

**Gate A round 3 (2026-07-31) — user scope amendment after gate exhaustion:**

| Item | How it is now resolved | Where |
|---|---|---|
| FR-021e *(new)* | the offset table's extent walk MUST descend when the instance-opening delimiter is itself a nested group's count tag — C-4.1's symmetric twin at the second site that implements the same scan. Round 2 scoped this site out on the ground that its boundary rule "is not defective"; round 3 verified that ground **false at source** and the user amended the scope rather than converging over it | `typed_read_splitter.md` **C-8.0c** (+ C-8.0b's `:454` bullet withdrawn, C-8.6a), `consume_group.md` C-4.4 (pointer), research **D-4a**, spec FR-021e |
| SC-016 *(new)* | validation and typed read agree on instance count **and boundaries**, and the extent spans all instances, for the delimiter-is-a-nested-count shape — the **485** contexts *(corrected 2026-08-02, Gate B r2 — superseding the earlier "262"; see `spec.md:342-346`)* for which SC-010 was unachievable as scoped through round 2 | spec SC-016, `typed_read_splitter.md` **W-10a**, plan Phase-4 gate, quickstart §2a/§4 |
| W-10 | **fixture re-stated, with TWO exclusions.** As round 2 wrote it, W-10 asserted the extent bound was "unchanged from pre-083 values" with no constraint on the delimiter's kind — a pin whose passing condition was that the defect is still present. Its anti-class-fix purpose is kept, and its fixture now excludes **both** the mode-(c) shape whose extent C-8.0c must change **and** polluted contexts, whose extent moves because `:478`'s termination test consults the **member set** that the delimiter injection at `table_view.hpp:645` stops polluting — the #210 Consequence 2 movement FR-007a / SC-013 pin *as a change*. Both exclusions are asserted in the case rather than assumed. *(The second exclusion was found by the orchestrator while verifying the first; no reviewer reported it.)* | `typed_read_splitter.md` W-10, plan Phase-4 gate |
| C-3.3 / D-5 / D-12 citation | the `set_group_first_ctx` member-injection call is at `include/fixpp/dict/table_view.hpp:645`, inside the function at `:641-646`. Round 1 said `:641-645`, round 2 "corrected" it to a call at `:646` inside `:641-647` — **both off by one**, verified at source | `group_ctx_delims.md` C-3.3, research D-5 / D-12 |
| W-9 | **widened from one fixture to two.** Round 2's mandated fixture was FR-021 mode (b), which is the shape the extent walk already handled correctly, so W-9 was structurally incapable of detecting the C-8.0c defect | `typed_read_splitter.md` W-9, data-model Entity 6 |
| W-3 | given a **named case and a file** — it was the only witness for FR-009 and User Story 2 scenario 4 and had neither (N25(b)). The dangling **W-2** alias is resolved to W-9 / W-10a in the same pass | `consume_group.md` W-2/W-3, plan Project Structure |
| citation sweep | Codex #1's C-ABI copy sites (`src/capi/session.cpp:109-111`, `src/capi/message_write.cpp:289-291`; `capi_internal.hpp:261-266` kept for the member's declaration/comment, `:493` kept as-is — the proposed `:489-493` leg was **rejected**), and N24's `dictionary.cpp` cluster: FR-011's false sentence is at **`:507-508`**, not the **true** text at `:503-505`; `:508-509`→`:510-511` normalised at all four loci; `:407`→`:398`; `:437-443`→`:439-444` | data-model, plan Source Code tree, `capi_group_grammar.md` C-9.2a, research D-3/D-11/D-12/D-13, `group_ctx_delims.md` C-3.5 |

**Checklist audit (2026-07-31, pipeline step 9) — 3 SPEC-FIXED, FR 40 → 42:**

| New | Closes | Landed in |
|---|---|---|
| FR-019b | the **phantom release-note artifact**. FR-019/FR-020, T069/T074, C-9.6, `plan.md` and `quickstart.md` all required "a release-note entry"; **no `CHANGELOG.md` or release-notes file exists in this repo**. Same defect 075 resolved at its Gate A round 2 — the B&L row *is* the release note, no second artifact is invented. Fixed at all six loci, since leaving one uncorrected carries the undischargeable instruction into `/implement` (075's exact failure mode) | spec.md FR-019b, `capi_group_grammar.md` C-9.6, tasks T069/T074, plan.md, quickstart.md |
| FR-019a | the **C-ABI disclosure was under-enumerated**. The construction check reads the **bare global** today (`message_write.cpp:719`) and FR-018 re-points it at the **context-keyed** value, so the enumeration must be over *"whose resolved value moves"*, not *"whose global value moves"*. Two further classes: **(b)** cause-1 tags — global unchanged (hence invisible to a global-lookup enumeration), per-context value moves in divergent contexts, **same severity as (a)**; **(c)** `1499`/`1669`/`1919` + six nested children — `group_begin` rejects them outright today (`:812`, `:923`) and the commit check never runs (`:720` guards `delim != 0`), so registration makes them **buildable**: a widening, not an SC-007 exception. Left as written, T069 would have produced a five-row table and the SC-007 audit would have certified green over an unenumerated exception — the **RC2** shape. **No new witness**: W-11 already pins (b), W-12 pins (a) | spec.md FR-019a + SC-007 exception scope, `capi_group_grammar.md` C-9.6/C-9.7, tasks T069, plan.md Evidence row, data-model.md Entity 7 |
| *(no new FR)* | **W-11a's counter seam was named only by analogy** to a counter in a module with no such instrumentation. Pinned as `fixpp::dict::detail::as_table_view_call_count()`, declared in **`src/dictionary/dictionary_internal.hpp`** (internal, already edited by T025) — **not** the public `include/fixpp/dict/dictionary.hpp`, which would have been *stricter* than the `fixpp_capi::detail` precedent it cites (`capi_internal.hpp:496-503` is itself internal, and `tests/capi/` already includes internal headers) | `capi_group_grammar.md` W-11a, tasks T049, plan.md Source Code tree |

The mandatory post-SPEC-FIXED `/speckit-analyze` re-run returned **8 findings, 0 CRITICAL** — every one a **stale restatement of the scope the audit had just corrected**, at loci the SPEC-FIXED sweep missed (`data-model.md` entirely, plus a second location each in `tasks.md` and `plan.md`, and this ledger's own count). All 8 remediated. That is the *"multi-locus sweep misses one"* pattern this bundle has now produced at least three times — Gate A rounds 2/3, and here — and it is why the re-run is mandatory rather than discretionary.

**Research D-12 is deliberately left OPEN**, with its Phase-1 `fr.type` instrumentation task and C-3.4a's conditional set-equality leg intact. The round-3 amendment does not touch it.

**`:16` and the notes heading below are both left as written.** A round-1 Codex finding read `:16`'s *"all 3 resolved"* against the heading's *"4 questions asked"* as an inconsistency. It is not: `:16` counts **`[NEEDS CLARIFICATION]` markers** and the heading counts **questions**, the fourth table row is itself labelled *"(4th question, not a pre-existing marker)"*, and both numbers appear in the same table with the discrepancy named. The Opus adversarial review disagreed with the finding and dropped it; the disagreement is recorded in `plan.md` `## Gate A → Round 1 — disagreements`.

### Original rationale for deferring the three markers to `/speckit-clarify` (retained for the record)

`/speckit-clarify` is the **mandatory next pipeline step** for this feature and must not be skipped
(project rule: always invoke it, never skip on "spec complete"). Resolving these three here would
make that step a no-op and would ask the user the same three questions twice. They are all genuine
scope decisions with no reasonable default, which is exactly the category `/speckit-clarify` exists
to settle:

| Marker | Question | Why no default |
|---|---|---|
| FR-018 | How is the construction-path delimiter reconciled with the validation-path delimiter? | The construction commit point may not know the message type and ancestor path at all. Threading context in, narrowing the check, and deferring are all defensible and have different blast radii on a GA-frozen ABI. |
| FR-020 | If an external reference engine's delimiter disagrees with declaration order, which wins? | Every member of the affected groups is schema-optional, so *any* choice rejects some schema-legal shape. This is an interop-policy call that cannot be settled from inside the repository. |
| FR-021 | Is the typed-read instance splitter in scope? | It is adjacent, reachable today, and was explicitly not investigated during triage. Pulling it in or leaving it out both change the feature's size materially. |

### Measurement-quality notes carried into the spec

- Baseline figures are measured on `main` @ `0539b56d`, not inherited from the issue text. Three of
  the issue's figures were corrected: the polluted-context count (42 → 52 across ten dictionaries,
  after removing 10 context-miss artefacts and adding FIX42 and Orchestra, which the issue omitted),
  the framing of pollution as the primary symptom (it is secondary to 335 wrong delimiters), and the
  scope of the defect as cross-loader rather than XML-loader-only.
- One premise is explicitly recorded as **inference, not measurement** (Assumptions): that a wrong
  delimiter causes mis-parsing. It is a reading of the receiver's logic; no wire reproduction exists
  yet. The spec requires the first test to close this before any fix is made.
- The root-cause split between the two causes is recorded as **corroborated but not proven**.

### Corrections applied during validation (iteration 2)

- **Fabricated message names removed.** The first draft glossed measured msgType codes with message
  names that were not derivable from the probe output (it attributed `BidResponse(l)` to
  `NoOrders(73)`, where `l` was measured under `NoBidComponents(420)`). All message names in the
  spec are now resolved from the dictionary's own `<messages>` block and every one traces to a line
  of probe output.
- **SC-001's denominator corrected.** The probe skipped the 30 unregistered contexts *before* the
  delimiter check, so 335 is measured over a population that excludes them. Once FR-006 registers
  their parents, the post-fix population is 365. The spec now labels this the one projected rather
  than measured figure, and requires it to be measured before the fix.
- **New coupling surfaced.** Those three groups' declared delimiters (453, 1529, 1920) are
  themselves nested-group count tags, so they are *additional* Story-2 cases — 30 on top of the 232.
  Story 2 is now sized against both.
- **SC-004 descoped from an impossible enumeration.** Was "all 232 affected contexts" as wire
  witnesses. Now: the delimiter pin covers all of them by construction, and wire acceptance is
  witnessed on a named per-count-tag subset. A sibling feature already hit the underlying constraint
  — some contexts are unconstructable because every member is schema-optional.
- **FR-022 added** requiring the benchmark in the same change. SC-009 stated the budget but nothing
  required the bench, and this is a hot-path change (delimiter resolution is queried per received
  field on the inbound validation path).
- **Dependency tightened**: the census oracle extension must be additive, because the existing
  member sets are consumed by pins on a parked branch that cannot currently be built.

### Deliberate anti-pattern guards written into the requirements

- FR-016 forbids citing the existing 78 collision-membership cases as delimiter coverage — their
  discriminator is derived independently of the delimiter, so their green is a proxy gap.
- FR-013 forbids a circular pin (expected values must not mirror the implementation).
- FR-014 requires the pin to be *observed* failing; a gate never proven red proves nothing.
- FR-015 forbids emitting member-set exactness as a second half-pin.
- FR-005 forbids a half-restructure across the two loaders.
- FR-017 requires the registered-count delta to be justified by construction, not by a bare number.
