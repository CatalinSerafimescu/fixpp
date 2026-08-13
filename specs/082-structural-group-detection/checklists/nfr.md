# Non-Functional Requirements Quality Checklist: Structural Repeating-Group Detection

**Purpose**: Validate that this feature's **non-functional requirements** — performance (Article VIII),
coverage and sanitizer discipline (Article IX), the fail-closed error model (FR-023), and governance
closure (Article XVIII/XX) — are complete, unambiguous, consistent, and measurable.
Audience: **Gate B reviewer** (formal pre-merge gate).
**Created**: 2026-07-30
**Feature**: [spec.md](../spec.md) · [research.md](../research.md) D-11/D-12/D-13 · contract
[group-detection.md](../contracts/group-detection.md) K7/K10/K11

**Scope note.** Two NFR areas here were *reshaped* during Gate A: the perf posture moved from "N/A" to
an intentional-perf-change obligation after the bench census was re-based, and the fail-closed error
model was added by user decision after convergence. Both are newer than the rest of the bundle, so
several items below interrogate the reasoning and not only the requirement text.

## Performance — Requirement Completeness

- [x] CHK001 Is the perf posture stated as a specific constitutional obligation rather than as a generic "no hot-path change"? [Completeness, Spec §FR-022] — PASS: FR-022 opens "This is an Article VIII **§2 intentional perf change**, not a §3 N/A."
- [x] CHK002 Is the census that establishes which profiles move based on **which benches time the changed function**, rather than on which dictionaries they load? [Completeness, Research §D-12] — PASS: D-12 Decision §1 states the basis explicitly and records the prior (rejected) dictionary-load basis as wrong.
- [x] CHK003 Is that census stated as a closed enumeration over the whole bench tree, including harnesses that have no compiled target? [Completeness, Research §D-12] — PASS: D-12 §1 — "closed... **34** profiles... two of them are script harnesses with no `.cpp` at all."
- [x] CHK004 Are all benchmark obligations enumerated, with each attributed to the specific constitutional clause it discharges? [Completeness, Spec §FR-022, Contract §K10] — PASS: FR-022 (a)/(b)/(c), with the Gate A round-3 attribution correction recorded in `plan.md` — legs (a)/(b) discharge §2, leg (c) discharges §3.
- [x] CHK005 Is the distinction between an obligation that produces a checked-in baseline and one that is a ceiling check stated explicitly? [Clarity, Spec §FR-022, Contract §K10] — PASS: FR-022 — "(a)/(b) MUST land... (c)... produces **no baseline**... it is a ceiling check."
- [x] CHK006 Is the absence of any pre-existing baseline for the profile that measures the changed function stated, since it determines that a "before" figure must be captured? [Completeness, Contract §K10] — PASS: FR-022(a) — "this profile has **no** `bench/baselines/` entry today (075 recorded it in-file only)."
- [x] CHK007 Are requirements defined for the memory-footprint axis of the affected structure, not only its build time? [Coverage, Research §D-12] — PASS: FR-022(a) — "`BM_TableView_Sizeof` MUST be re-reported... and the `group_bits_` heap growth stated explicitly, because `sizeof` cannot capture it."
- [x] CHK008 Is the fact that no CI job currently runs the compile-cost harness recorded, so the obligation is understood as manual? [Assumption, Contract §K10] — PASS: FR-022(c) — "No CI job runs it — `tier1.yml`'s `bench` job is soft... so FR-022 (c) requires it run **manually**."

## Performance — Clarity & Measurability

- [x] CHK009 Is the mechanism by which the parse path changes cost stated concretely, rather than as "groups become active"? [Clarity, Research §D-12] — PASS: D-12 states the concrete mechanism — `table_view::group_bits_` flips from an all-clear short-circuit to real `group_ctx_`/`group_members_` hash-probe resolution per message.
- [x] CHK010 Is the reason set-equality does **not** bound build time stated, given set-equality is elsewhere the grounds for no-change claims? [Clarity, Research §D-12] — PASS: FR-022(a) — "Set-equality does not bound build time here: the per-field test goes from one enum compare to an O(log G)... on **every** dictionary — including the C2 EQUAL rows."
- [x] CHK011 Is the per-field cost change characterized in complexity terms with the governing magnitude named? [Measurability, Research §D-12] — PASS: "O(log G) `groups_` binary search (`G = 507` on FIX50SP2, ≈9 comparisons)."
- [x] CHK012 Is the regression budget stated as a specific percentage against a specific enumerated baseline set? [Measurability, Spec §SC-012] — PASS: SC-012 — "within ±5%" against the enumerated 8-file set.
- [x] CHK013 Is that baseline set enumerated identically wherever it appears? [Consistency, Spec §SC-012, Research §D-12] — PASS: cross-checked directly — the same 8 files (`wire/{framer,offset_table,parser,validator,writer}_bench.json`, `codegen/typed_accessor_bench.json`, `dictionary/{reify_bench,xml_loader}.json`) appear identically in `spec.md` SC-012, `research.md` D-12 §4, `quickstart.md` S9, and `tasks.md` T049.
- [x] CHK014 Is any baseline entry whose no-move ground is narrowed by another requirement identified rather than silently included? [Consistency, Spec §SC-012, §FR-023] — PASS: SC-012/D-12 §4 both flag `dictionary/xml_loader.json` explicitly as the one entry whose no-move ground FR-023 narrows.
- [x] CHK015 Is asserting a leg unmoved instead of measuring it stated as non-conforming? [Measurability, Contract §K10] — PASS: K10's closing sentence — "Asserting any leg unmoved instead of measuring it is non-conforming."

## Coverage, Sanitizers & Static Analysis

- [x] CHK016 Are the touched modules enumerated for the coverage obligation, including the modules added by the fail-closed requirement? [Completeness, Plan Constitution Check Article IX] — PASS: `plan.md` Article IX row — "Touched modules: `src/dictionary/` (incl. **both loaders**, FR-023), `tools/codegen/`."
- [x] CHK017 Is the coverage-waiver posture re-grounded now that new error paths exist, rather than resting on "no error path is added"? [Consistency, Plan Constitution Check Article IX] — PASS: same row — "FR-023 does add two error paths... so the earlier 'no error path is added' ground no longer covers the whole diff; no uncovered-error-path waiver is anticipated anyway, on new grounds."
- [x] CHK018 Is each newly-added error path required to have its own dedicated covering witness? [Measurability, Spec §FR-023, Contract §K11] — PASS: same row — "each throw has its own dedicated rejection fixture (K11, one per loader, on a non-first-seen occurrence)."
- [x] CHK019 Are the coverage thresholds stated numerically rather than as "adequate coverage"? [Measurability, Plan Constitution Check Article IX] — PASS: the Article IX row's Requirement column states "≥95% line, ≥85% branch on touched modules" — numeric, not qualitative.
- [x] CHK020 Are requirements stated for the test-selection scope, given a label-filtered run has previously missed a touched subsystem's pin? [Coverage, Plan §Implementation Sequencing] — PASS: plan.md — "Because `tools/codegen/**` is touched, `ctest -L codegen` is mandatory locally."
- [x] CHK021 Is the hazard of conjunctive test-label selection recorded so a verification command cannot select the empty set? [Edge Case, Quickstart §Prerequisites] — PASS: quickstart Prerequisites states this in full, with the `ctest --help` citation and the "0 tests and exits 0" failure shape named.
- [x] CHK022 Is an expected non-zero selection size stated for each verification command, so a vacuous run is detectable? [Measurability, Quickstart] — PASS: every quickstart scenario (S1–S6, Full local gate) states an explicit "Expected selection: ≥ N tests."

## Fail-Closed Error Model (FR-023)

- [x] CHK023 Is the rejected condition defined in terms of the loader's own member-resolution outcome rather than as "an empty group"? [Clarity, Spec §FR-023] — PASS: FR-023's "The rejected state, stated precisely" bullet defines it as "the case in which the loader's own member scan resolves no `field`/`group`/`component` child."
- [x] CHK024 Is the requirement stated **per loader**, rather than once for "the loader"? [Completeness, Spec §FR-023, Contract §K11] — PASS: FR-023 states both the `<fix>` loader and the Orchestra loader separately, each with its own line cites and error class.
- [x] CHK025 Is the order-independence requirement stated — that the rejection fires on any occurrence, not only a tag's first-seen one? [Completeness, Spec §FR-023] — PASS: FR-023's dedicated bullet — "MUST fire on ANY member-less occurrence, not only the first-seen one."
- [x] CHK026 Is the reason order-independence matters stated, so an implementer understands why placement relative to the dedup guard is load-bearing? [Clarity, Spec §FR-023, Contract §K11] — PASS: FR-023 — "a check placed *inside* that guard would admit a member-less **second** occurrence of a tag whose first occurrence had members, making the rule order-dependent."
- [x] CHK027 Does the witness requirement specify a fixture shape that can actually fail a wrongly-placed check? [Measurability, Contract §K11] — PASS: K11/data-model FR-023 row — "the fixture MUST place the member-less `<group>` at a **non-first-seen** occurrence... or the pin cannot fail on a check wrongly placed."
- [x] CHK028 Is the choice to reject rather than tolerate justified on representational grounds rather than only on convention? [Clarity, Spec §Edge Cases, Contract §C1.3] — PASS: contract P1-NON + research D-1a's three grounds (unrepresentable `table_view` state, the context store's unconditional `members.empty()` defeat, and only then the measured no-regression fact).
- [x] CHK029 Is the precedent this rejection mirrors cited, so it reads as consistent with existing dispositions rather than novel? [Traceability, Spec §Edge Cases] — PASS: FR-023 cites `xml_loader.cpp:584`'s sibling rejection and `:1017`'s excluded case; verified at the source — both citations resolve exactly as described.
- [x] CHK030 Is the no-regression obligation stated with a measured basis rather than an expectation? [Measurability, Spec §FR-023, Contract §K7] — PASS: FR-023 — "This is not merely expected but *measured*: `predicate_census.py` emits no zero-member-`<group>` warning... and an independent walk... confirms it."
- [x] CHK031 Is the operator-facing consequence captured as a documentation requirement with an explicit statement of how many shipped dictionaries are affected? [Completeness, Spec §SC-013] — PASS: SC-013 — "recorded... as a named behavior change with an operator-facing release note" and states "0 affected" explicitly.
- [x] CHK032 Is the diagnostic content specified as named facts rather than as a quality adjective? [Measurability, Spec §FR-023] — PASS: FR-023's "Diagnostic content" bullet names the group's `name` attribute, its `no_tag`, and "no member resolved" as the required content.

## Behavior-Change & Compatibility

- [x] CHK033 Is the ungated nature of the parse correction stated as a requirement, including that no new configuration key is introduced? [Completeness, Spec §FR-006] — PASS: FR-006 — "no new configuration key, and no per-dictionary opt-in for structural detection."
- [x] CHK034 Are the two behavior axes — read shape and acceptance — separated so a pin cannot conflate them? [Clarity, Spec §FR-006a, §SC-008a] — PASS: FR-006a (read shape, strict OFF) and FR-006b (acceptance, strict ON) are separate requirements; SC-008a states both legs as distinct clauses.
- [x] CHK035 Is the no-new-rejection property under the default configuration stated as its own requirement? [Completeness, Spec §SC-008a] — PASS: SC-008a's first sentence is exactly this, as its own clause.
- [x] CHK036 Is the enforcement set under the opt-in configuration required to be **enumerated** rather than merely observed to be non-empty? [Measurability, Spec §FR-006b, §SC-008a] — PASS: SC-008a's second leg — "enumerated and pinned rather than merely observed"; `tasks.md` T021b implements this as a set-equality pin.
- [x] CHK037 Is the derivation source for that enumeration named and independent of the code under change? [Measurability, Spec §FR-006b] — PASS: T021b — "Derive the expected rejection set from T005's oracle" (the FR-018 raw-XML oracle, independent of the predicate under change).
- [x] CHK038 Are all named behavior changes required to carry both a behavior record and an operator-facing release note? [Completeness, Spec §FR-006c, §SC-013] — PASS: FR-006c and FR-023's Behavior-change obligation bullet both require this identically.

## Governance & Documentation Closure

- [x] CHK039 Is governing-document closure specified as reaching the constitution, not stopping at the behaviour-record file? [Completeness, Spec §FR-020] — PASS: FR-020 is entirely this requirement; research D-13 states "The gap" the reviewed revision left.
- [x] CHK040 Is the amendment's scope bounded to specific clauses, with anything that must remain unchanged named? [Clarity, Spec §FR-020] — PASS: FR-020 enumerates (a)/(b)/(c), with (c) naming Article I §1 as explicitly unchanged.
- [x] CHK041 Is the amendment characterized as permissive with respect to existing version scope, rather than as a widening? [Clarity, Spec §FR-020] — PASS: FR-020 — "The required change is **annotation-only** and **permissive**... 082 moves the library *toward* Article I §1 with no scope widening."
- [x] CHK042 Is the amendment's delivery mechanism specified — folded into this branch versus a standalone change — with its precedent cited? [Completeness, Spec §FR-020, Research §D-13] — PASS: FR-020 — "folded into this feature's own branch, per the unbroken precedent of v0.5 (069)... v0.10 (078) — **not** a standalone `Constitution: amend …` PR."
- [x] CHK043 Is the historical-record entry that must be preserved identified, so the amendment does not rewrite history? [Edge Case, Spec §FR-020] — PASS: FR-020 — "The v0.9 amendment-log entry at `:18` carries the same sentence as *historical record* and MUST be left intact"; verified at the source — `.specify/constitution.md:18` matches exactly.
- [x] CHK044 Are all stale citations that this feature's documentation pass must refresh identified specifically? [Completeness, Spec §FR-019] — PASS: FR-019 names the exact stale cite — "`behaviors-and-limitations.md:1749` cites '`dictionary.cpp:335`'s NumInGroup gate'; the gate is now at `dictionary.cpp:398`"; verified at the source — `behaviors-and-limitations.md:1749` (the L-066-1 row) contains exactly that stale citation.
- [x] CHK045 Is the set of limitation records to be closed enumerated, with the exit condition stated as no remaining open carve-out? [Measurability, Spec §FR-019, §SC-010] — PASS: SC-010 — "no remaining open L-063-1 / L-061-1 / L-066-1 / L-077-1 carve-out."

## Dependencies, Assumptions & Conflicts

- [x] CHK046 Are the mandatory process controls for this feature's trigger categories enumerated with their individual status? [Completeness, Plan Constitution Check Appendix A] — **SPEC-FIXED**: `plan.md`'s Appendix A row *did* enumerate all four controls with individual status, but three loci (the Appendix A row, the `XVI §4` row, and the § Result paragraph) still read `/analyze` as **PENDING** even though `/analyze` ran today (2026-07-30) and its findings were remediated in `spec.md`/`data-model.md`/`tasks.md` the same day — confirmed via `git diff`/`git log`, which shows those three artifacts modified today with no committed `/analyze` report yet filed. Updated all three loci in `plan.md` to **DONE**.
- [x] CHK047 Is the one control still outstanding identified, rather than the set being presented as complete? [Clarity, Plan Constitution Check Appendix A] — **SPEC-FIXED** (same edit as CHK046): post-fix there is no longer an outstanding control to identify — all four are DONE — so the item's premise (an outstanding control exists) is itself now stale. `plan.md`'s Result paragraph no longer names `/analyze` as remaining.
- [x] CHK048 Is the assumption that the strict-validation opt-in already exists — and is not introduced here — stated? [Assumption, Spec §FR-006b] — PASS: FR-006b — "MUST ride the **existing** `validate_inbound_messages` opt-in — no additional gate."
- [x] CHK049 Does any non-functional requirement conflict with the frozen-surface requirement? [Conflict, Spec §FR-017, §FR-022] — PASS: no edit needed. FR-022's obligations are confined to `bench/` and `src/dictionary/`/`tools/codegen/` internals; none touches `src/capi/`, `include/fixpp/capi/`, or any exported symbol, so the two requirements' scopes are disjoint on their face.
- [x] CHK050 Is the standing user decision on compatibility posture recorded with its date, so a reviewer does not re-open it? [Traceability, Spec §Clarifications] — PASS: spec.md § Clarifications is headed "Session 2026-07-29" and § Assumptions repeats "the FIX40/41/42 compat posture is **settled** (Clarifications, 2026-07-29)."

## Audit Result

| Disposition | Count |
|---|---|
| PASS | 48 |
| SPEC-FIXED | 2 |
| DD-DECIDED | 0 |
| WAIVED | 0 |
| **Total** | 50 |

### SPEC-FIXED items
- CHK046 — updated `plan.md`'s three stale `/analyze`-PENDING loci (`XVI §4` row, Appendix A row, § Result paragraph) to DONE, reflecting that `/analyze` ran 2026-07-30 with same-day remediation; affected: `plan.md` Constitution Check table + § Result paragraph.
- CHK047 — same edit as CHK046; the "one outstanding control" premise is retired along with it.

### DD-DECIDED items
None.

### WAIVED items
None.

Anchors spot-verified: `.specify/constitution.md:18` (v0.9 amendment-log historical record), `spec/behaviors-and-limitations.md:1749` (L-066-1's stale `dictionary.cpp:335` cite, confirmed present pre-fix), `src/dictionary/xml_loader.cpp:584,609,1017` (rejection precedents), `plan.md` Constitution Check Article IX/§2-§3 rows — all resolve as cited. Baseline-set enumeration (8 files) cross-checked identical across `spec.md` SC-012, `research.md` D-12 §4, `quickstart.md` S9, `tasks.md` T049.
