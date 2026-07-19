# Validation-Correctness Requirements Checklist: Strict-Validation-Path Residual Closeout

**Purpose**: Unit-tests-for-English on the requirements that govern this feature's load-bearing correctness invariants — accept-only semantics, no-false-accept / no-false-reject, census non-circularity, two-tier coverage, and the preserved-invariant set (parser / golden / C-ABI / opt-in no-op). Tests whether these requirements are complete, clear, consistent, and measurable — NOT whether the code works.
**Created**: 2026-07-19
**Feature**: [spec.md](../spec.md) · [plan.md](../plan.md) · [tasks.md](../tasks.md)
**Audience**: Gate A/B reviewer (PR) · **Depth**: Standard

## Accept-Only Semantics (Concern A)

- [x] CHK001 Is "accept-only" defined with an explicit boundary between *tag acceptance at Step-1* and *required-presence enforcement*, so a reader cannot conflate them? [Clarity, Spec §FR-003a] — PASS: FR-003a states the boundary explicitly ("accepted at the validator's Step-1 unexpected-tag gate... MUST NOT add new message-level 'header field required' enforcement"); contract clause 1 (Step-1 accept) vs clause 2 (no new required-presence) restate the same split as two numbered clauses.
- [x] CHK002 Are the requirements consistent about which layer owns header-field required-presence (session FSM vs dictionary validator) across spec, contract, and B&L? [Consistency, Spec §FR-003a / contracts/validation-acceptance.md clause 2] — PASS: FR-003a ("required-presence of session-owned header fields stays governed by the session-layer FSM..., not the dictionary validator"), contract clause 2 (identical wording), and research.md D-6's B&L intent ("fixpp's dictionary validator does not enforce header-field required-presence (the session FSM owns it)") all agree; T024 lands the B&L edit at /implement, not a drift risk to the audited requirement text.
- [x] CHK003 Is the set of framing tags to be accepted specified as an enumerable rule (the full FIXT.1.1 `<header>`+`<trailer>` set) rather than an illustrative subset (8/9/34/49/52/56/10)? [Completeness, Spec §FR-001 / research.md D-2] — PASS: FR-001's operative sourcing clause ("sourced by merging the vendored FIXT.1.1 standard header/trailer into the application dictionary's validation view") merges the whole vendored block, not the parenthetical 6+1 list (which is illustrative); research.md D-2 explicitly resolves any doubt ("Full header set, not just 6... incl. session-version tags like ApplVerID(1128)/ApplExtID(1156)"); data-model.md E-1 + T005 pin the full set exact-set-equal to `FIXT11.xml` both directions — the rule is enumerable and CI-enforced, not left as a subset reading.
- [x] CHK004 Is the disposition of the nested `<header>` `NoHops` group (627/628/629/630) explicitly stated in the requirements, including the recurse-one-level rule and per-tag datatype? [Gap→resolved, data-model.md E-1 / contracts A invariants] — PASS: data-model.md E-1 states the recurse-one-level rule and per-tag datatypes (627→Int, 628→String, 629→String, 630→Int) explicitly, matches FIXT11.xml (verified: `dictionaries/FIXT11.xml:32-36` NoHops group, :209-212 field datatypes NUMINGROUP/STRING/UTCTIMESTAMP/SEQNUM, reduced via `field_type.hpp`); contracts/validation-acceptance.md Invariants restates it; T005/T007 pin it as a census test. (Pre-audit /analyze fix C1 — confirmed sound.)

## No-False-Accept / No-False-Reject (Article VI)

- [x] CHK005 Is "no false-accept" stated as a measurable requirement with a concrete witness (malformed `34=abc` / `1156=abc` → Int-arm reject with exact error + ref_tag)? [Measurability, Spec §FR-011 / contracts A clause 4] — PASS: contract clause 4 pins the exact witness ("`34=abc`... and `1156=abc`... MUST be rejected at the Int arm with `wire_field_value_out_of_range` **and** the reported `ref_tag` equal to the malformed tag"); T004(c)/quickstart A2 restate the same RED→GREEN pin with exact error+ref_tag (not "generically rejects"). Verified against `validator.hpp`'s Int arm (`value.empty()`/non-digit → `wire_field_value_out_of_range`, ~:501-520) and FIXT11.xml datatypes (34=SEQNUM→Int, 1156=INT→Int) — the mechanism the requirement pins exists and matches.
- [x] CHK006 Are the requirements explicit about which framing tags are structurally *un*checkable (52=UTCTIMESTAMP→String) so that "accepted" there is documented intent, not an undetected false-accept? [Clarity, research.md D-2 / quickstart A2] — PASS: research.md D-2 states it explicitly ("52=UTCTIMESTAMP→`String`... structurally undetectable... 52 is an accept-only tag, not a malformed-reject tag"); contract clause 4 and quickstart A2 both restate "`52=notatime` is NOT rejected... document, do not pin as a reject." Verified `field_type.hpp` reduces `UtcTimestamp → String` and FIXT11.xml:130 declares 52 as UTCTIMESTAMP — the documented limitation is technically accurate, not an unflagged gap.
- [x] CHK007 Does SC-003's blanket "no conforming FIX50SPx message is false-rejected for a standard header/trailer tag" have complete coverage of the *entire* header/trailer tag set (incl. hop tags), or is its scope narrowed anywhere without saying so? [Coverage, Spec §SC-003] — PASS: SC-003's text is unqualified ("a standard header/trailer tag"), and data-model.md E-1 explicitly ties the NoHops flat-inclusion decision back to SC-003 ("Excluding them would leave a residual false-reject of routed FIXT traffic — the exact SC-003 defect class this feature targets"), so scope is affirmatively widened to cover hop tags rather than silently narrowed; T005's exact-set census (both directions) is the coverage gate.
- [x] CHK008 Is the accept-only guard requirement (a genuinely-required *application* field still rejects) stated distinctly from the framing-acceptance requirement, so the two cannot regress together? [Consistency, Spec §FR-003 / contracts A clause 3] — PASS: FR-003 (existing app-field/enum/group checks unchanged) and FR-003a (accept-only Step-1 definition) are two separate FRs; contract clause 3 (existing checks preserved) is a separate clause from clause 1 (framing acceptance); T004(a) is a distinct RED→GREEN pin from T003, so a regression in one cannot silently pass the other's test.

## Group-Gating Parity (Concern B)

- [x] CHK009 Is the gating rule specified unambiguously as *immediate-enclosing* group `required=` (NOT an ancestor-AND), with the QuickFIX `addXMLGroup` anchor? [Clarity, Spec §FR-005 / research.md D-3] — DD-DECIDED §D-3 (Gate A round-1 N1): spec.md FR-005 says only "gated on the enclosing group's own `required=` attribute," without spelling out the immediate-vs-ancestor-AND distinction. The disambiguation is explicitly a Gate A decision point ("N1: this was previously deferred to the golden, risking a circular oracle... DECIDED NOW") settled in research.md D-3, which names the QuickFIX `addXMLGroup` anchor and states "NOT an AND across ancestor groups"; contracts/census-and-parity.md clause restates it. This is a frozen-authority design choice, correctly not re-spec'd word-for-word into spec.md's FR text — traced here rather than PASSed as self-evident from FR-005 alone. Note: the QuickFIX `addXMLGroup` C++ source itself was NOT locally re-verified (`reference-engines/quickfix-cpp` absent on this tree per N2 — expected, gitignored); the anchor rests on inherited 079 precedent, corroborated downstream by the T020 parity golden at /verify, not re-derived here.
- [x] CHK010 Are the required-group-holds (FR-006) and optional-group-relaxes (FR-005) requirements stated as a matched pair, so relaxing one cannot silently weaken the other? [Consistency, Spec §FR-005/006] — PASS: FR-005/FR-006 are adjacent, cross-referencing FRs ("Concern B relaxes only the optional-group case"); contract clauses 1-2 restate the same pair; T013's RED→GREEN pin explicitly keeps the required-group-instance-still-rejects case in the same test as the optional-group flip, so the two cannot regress independently unnoticed.
- [x] CHK011 Is the direction of the parity change constrained in the requirements (fixpp only relaxes toward QuickFIX-exact; never introduces a false-accept)? [Completeness, Spec §FR-011] — PASS: FR-011 states "neither concern may introduce a false-accept"; spec.md Context section additionally states the empirical direction constraint ("the 24 disagreements are all fixpp strict-supersets... never under-requires"), and User Story 2's priority rationale repeats it ("cannot cause a false-accept regardless of direction").
- [x] CHK012 Is the requirement that the typed tier agree with the runtime tier (FR-007) tied to a specific mechanism claim (fork plan identity by enclosing-required) that is testable at the affected sites? [Measurability, Spec §FR-007 / data-model.md E-4] — PASS: FR-007 states the observable requirement (typed/runtime agreement); data-model.md E-4 (part of this feature's own Phase-1 design surface, not an external doc) supplies the concrete, testable mechanism (fork plan interning identity by enclosing-group-required-ness); T018 implements it, T013/T019 test it. Verified against `emit_builders.cpp` source: `compute_signature` (:251-282) folds `item.required`/`item.group_required` (own member / nested-group required) but the intern key (`intern.intern(gm.tag, nested->delimiter_tag, signature, …)`, :649) does not fold in the *enclosing*-usage required-ness (`item.group_required` at :656 is set on the parent's `LevelItem`, never fed into the child's `signature`) — the gap D-4/E-4 describe is real and the fork is the correct, testable fix.

## Census Non-Circularity & Exact-Set (SC-002)

- [x] CHK013 Do the requirements state that the census oracle derives its rule *independently from raw XML* — not from the loaded tables nor the parity golden — so the completeness gate cannot be self-confirming? [Clarity, contracts/census-and-parity.md / research.md D-3] — PASS: contracts/census-and-parity.md states it verbatim ("Oracle side = a raw-XML walk... independent of the loaded tables and of the parity golden"); research.md D-3 repeats it and cites the non-circularity feedback memos ([[feedback_verification_corpus_built_from_the_read_it_checks_is_blind]], [[feedback_noncircular_census_projection_source_stops_pinning_shipped_classes]]).
- [x] CHK014 Is "exact-set-equal" specified as *both directions* (not subset/superset) with an explicit target count (24→0)? [Measurability, Spec §SC-002] — PASS: SC-002 states "exact-set-equal to a QuickFIX-derived oracle in both directions — 0 divergences (down from the 24 strict-superset contexts)"; contract's Census contract section repeats "exact-set, both directions... collapse to 0."
- [x] CHK015 Are the roles of the census (defines) and the quickfix-cpp parity golden (corroborates) distinguished, so the golden is not mistaken for the rule's source of truth? [Consistency, contracts/census-and-parity.md] — PASS: contract explicitly labels the roles ("The parity golden **corroborates** this rule; it does not **define** it") and the Parity golden contract section is a separate subsection from the Census contract section, keeping the two mechanisms visibly distinct.
- [x] CHK016 Is the parity-golden prerequisite (clone+build quickfix-cpp 1.16.0, else the oracle silently skips) documented as a hard prerequisite rather than an assumption? [Dependency, research.md N2 / tasks T001] — PASS: research.md N2 labels it "an explicit `/tasks` + `/verify` prerequisite... Without it the parity oracle is silently skipped and false-greened"; T001 is a first-phase Setup task (not an Assumptions-section footnote) that gates T020, so it is enforced as a task dependency, not merely assumed.

## Coverage Split & Traceability (N3)

- [x] CHK017 Are the divergent contexts enumerated by `(version, message, group)` with a stated typed-vs-runtime-only tier for each? [Completeness, tasks.md N3 table] — PASS: the N3 table has explicit `Dict | Message | Group | required='Y' member(s) | Tier` columns for all 23 legacy-enumerated contexts, each row carrying a `typed` or `RUNTIME-ONLY` tier value; the table's own framing note additionally covers the vlatest contexts the raw-XML walk cannot enumerate ("typed-tier contexts... and vlatest contexts (oracle-enumerated)"). The count-completeness question (23 vs the authoritative 24) is a distinct concern, tracked separately at CHK019.
- [x] CHK018 Is the *runtime-only-by-scope* case (FIX50 with no typed builder tier) explicitly identified and assigned a pinning mechanism (census + parity only), rather than assumed covered by the typed tier? [Coverage, tasks.md N3 / research.md N3] — PASS: N3's framing note names the single case explicitly ("**FIX50 `ListStrikePrice / NoUnderlyings / Price`** (FIX50 has no typed builder tier per L-077-1) — has no typed tier to compare and is pinned by the census (T015) + parity golden (T020) **alone**"); research.md N3 states the same rule generally for any runtime-only version.
- [x] CHK019 Is the gap between the best-effort legacy enumeration (23) and the authoritative oracle set (24) acknowledged with a reconciliation step, so the count discrepancy is not silently enshrined? [Ambiguity, tasks.md N3 / T021] — PASS: the N3 table's header note states the delta explicitly ("The ~1-context delta (23 here vs 24 measured) is expected... T021 reconciles this table against it") and gives the root cause (the raw-XML walk cannot parse the Orchestra/`vlatest` schema); T021 is a dedicated task ("Reconcile the N3 enumeration... against the reworked oracle's authoritative divergent-context set... update the legacy 23-context table to the authoritative set") — the discrepancy is flagged, not enshrined as final.

## Preserved-Invariant Set

- [x] CHK020 Is the parser-containment invariant (FR-009) specified as a *direct* assertion (`field_valid_for`/`valid_tags_for` stay false while `validate()` accepts) rather than a blind on/off `unknown_fields()` compare that the requirements themselves flag as near-vacuous? [Clarity, Spec §FR-009 / research.md D-1] — PASS: FR-009's Parser invariant clause, contract clause 5 + Invariants section, and T006 all specify the direct `field_valid_for`/`valid_tags_for`-false assertion and explicitly reject the on/off `unknown_fields()` compare as near-vacuous ("`inbound_tv_` is built flag-independently at `session.cpp:992`, so a both-sides `valid_` widening classifies identically on and off"). Verified against source: `session.cpp:992` (`inbound_tv_ = cfg_.dictionary->as_table_view()`, unconditional) and `parser.hpp:321-347` (`unknown_fields()` hardcodes tags 8/9/10 as always-exempt and classifies all other tags via `classify_fn_`→`field_valid_for`, which this feature does not touch) confirm the near-vacuity claim is technically correct — the direct pin is the only assertion that would actually catch a `valid_`-store re-widening regression.
- [x] CHK021 Are the goldens that MUST stay byte-identical (read/reify: v42/v44/v50sp2/vt11/vlatest) enumerated distinctly from the goldens that MAY change (typed-validator: v44/v50sp2/vlatest, Concern B only)? [Consistency, Spec §FR-009 / research.md D-5] — PASS: FR-009 lists both sets by name in one sentence with an explicit "only" qualifier ("Concern B changes the generated typed validator goldens (v44/v50sp2/vlatest) only at the affected group-gating sites... Concern A changes no golden at all"); research.md D-5 repeats the same split with concrete golden paths.
- [x] CHK022 Is the "no C-ABI change" requirement stated with its concrete gates (abidiff 0-diff, nm symbol golden byte-identical, no capi/error.h/version.h edit)? [Measurability, Spec §FR-008] — PASS: FR-008 names the concrete gates directly ("no `error.h` / `version.h` / capi header edits, no symbol-golden or abidiff regeneration"); plan.md Constraints repeats with "abidiff clean (0 diff), `nm` symbol golden byte-identical."
- [x] CHK023 Is the opt-in no-op requirement (default-off = byte-identical) stated for *both* concerns and both the wire and validator paths? [Completeness, Spec §FR-010 / SC-005] — PASS: FR-010 states "byte-identical no-op for **both concerns**"; SC-005 states "byte-identical **wire and validator** results" — both axes (concern-pair and path-pair) are covered.

## Scope Boundaries & Edge Cases

- [x] CHK024 Are the out-of-scope items (`vlatest` for Concern A, FIX42 INT-group carve-out #196/L-066-1, ApplExtID(1156)=303 re-keying / L-074-1) stated as explicit exclusions with rationale, not omissions? [Coverage, Spec §Edge Cases / Assumptions] — PASS: Edge Cases names `vlatest` with rationale (Orchestra already expands StandardHeader/Trailer inline) and separately flags the L-074-1/ApplExtID(1156)=303 entanglement as out of scope; Assumptions names FIX42's #196/L-066-1 carve-out as "a separate feature and out of scope." All three carry an explicit rationale sentence, not a bare mention.
- [x] CHK025 Are the non-FIXT dictionaries' unchanged behavior (FIX40–44 own populated header; FIXT.1.1 admin messages) specified as a preserved requirement with a regression pin? [Completeness, Spec §Edge Cases / FR-002] — PASS: Edge Cases states both preservation requirements ("Non-FIXT dictionaries keep their own header... MUST be unchanged"; "FIXT.1.1 admin/session messages... MUST be unchanged"); FR-002 restates for the dictionary layer; T011 is the explicit regression-pin task (SC-004, byte-identical read/reify + codegen-determinism run).
- [x] CHK026 Is the version-gating boundary ({v50,v50sp1,v50sp2} only) specified precisely enough that a reviewer can tell whether a given dictionary is in or out of the framing-surface population? [Clarity, Spec §FR-002 / research.md D-2 Gating] — PASS: FR-002 names the three empty-`<header/>` dictionaries by version string; research.md D-2's "Gating" subsection gives the exact mechanism ("read `version_` from `dict_metadata_handle`... populate the framing surface iff version ∈ {v50,v50sp1,v50sp2}. FIX40–44/FIXT11/vlatest untouched") — unambiguous for any dictionary a reviewer names.

## Perf & Non-Functional

- [x] CHK027 Are the perf requirements clear about *where* Concern A's cost lands (setup-time `as_table_view()`, up to twice/session; never per-`validate()`), so "no hot-path delta" is a checkable claim? [Measurability, plan.md Performance Goals / research.md D-1] — PASS: plan.md Performance Goals names the exact two call sites with file:line (`session.cpp:992` for `inbound_tv_`, `session.cpp:1234` for the strict validator) and states the cost is bounded to setup-time, never per-`validate()`; research.md D-1's "Perf-safe" bullet repeats the same claim. Verified against source: `session.cpp:992` (`inbound_tv_ = cfg_.dictionary->as_table_view();`) and `session.cpp:1233` (`validator_ = std::make_unique<...>(cfg_.dictionary->as_table_view());`) — both call sites exist essentially where cited (line 1233 vs cited 1234, immaterial drift), confirming the claim is checkable against real code, not aspirational.
- [x] CHK028 Is it specified whether the *newly-enabled* FIX50SPx accept path (previously fast-rejected on tag 8) needs its own benchmark characterization, or an explicit rationale for none? [Gap, tasks.md T023 D1 note] — PASS: T023's "(D1 — perf, /verify decision)" note states the gap explicitly (existing `validator_bench` cases don't exercise the new accept path) and assigns a concrete decision point at `/speckit-verify` ("either add a `BM_Validate_FixtHeaderAccept` case... or record an explicit rationale for why... no characterization" — pre-empting the named 075-class Gate B perf-waiver failure mode). The gap is acknowledged with an assigned resolution mechanism, not silently left open; tagged `[Gap]` (not Completeness/Clarity/Consistency), so WAIVED would have been permissible, but PASS is the accurate disposition since the requirement to decide is itself explicitly specified.

## Notes

- Check items off as completed: `[x]`; annotate disposition inline (SPEC-FIXED / DD-DECIDED §X / WAIVED:<reason>) at checklist-audit.
- Traceability: 28/28 items carry a spec/plan/contract/task reference or a `[Gap]`/`[Ambiguity]`/`[Dependency]` marker (≥80% target met).
- This checklist tests requirement *quality*; behavioral RED→GREEN pins live in quickstart.md + tasks.md T003–T022.

## Audit Result

Executed as pipeline.md step 9 (checklist audit gate), 2026-07-19, ahead of `/speckit-implement`. Prior to this audit the orchestrator applied two `/speckit-analyze` fixes (C1 — NoHops nested-`<header>` group disposition; C2 — xml_loader member-record citation `:557–559`) which this audit confirmed sound (CHK004; direct source read of `xml_loader.cpp:557–559`).

| Disposition | Count |
|---|---|
| PASS | 27 |
| SPEC-FIXED | 0 |
| DD-DECIDED | 1 |
| WAIVED | 0 |
| **Total** | 28 |

### SPEC-FIXED items

None. No genuine Completeness/Clarity/Consistency defect was found in the audited artifact set; the two prior `/speckit-analyze` fixes (C1, C2) were applied by the orchestrator before this audit began and are reflected as PASS (CHK004) / confirmed-sound anchors, not fixed here.

### DD-DECIDED items

- CHK009 — anchor `research.md D-3` / Gate A round-1 N1; rationale: the immediate-enclosing-vs-ancestor-AND group-gating semantic is an explicit Gate A design decision ("N1... DECIDED NOW," citing QuickFIX `addXMLGroup`) that spec.md's FR-005 states only at the "enclosing group's own `required=`" level without the ancestor-AND disambiguation — correctly left as a frozen-authority traceability ref rather than re-spec'd verbatim into the FR text.

### WAIVED items

None.

### Realizability sub-check

Two value-typed entities are newly introduced by this feature:

- **E-1 — baked FIXT.1.1 framing `tag→field_type` table** (`fixt_framing_types_` source constant): a compile-time `std::array`-like static constant in the dictionary layer, alongside existing `kVersionTable`/`kFieldTypeTable` precedent. No forward-declared dependency, no deferred owner — realizable from the contract alone. **Clean.**
- **E-2 — validator-private `table_view` members** (`fixt_framing_tags_` set, `fixt_framing_types_` map): added members on an existing complete type (`table_view`), holding standard-library container types (set/map) by value, no incomplete-type dependency. **Clean.**

Neither entity holds a `unique_ptr<Dep>`/by-value `Dep`/base-class `Dep` where `Dep` is only forward-declared and owned by a deferred spec. **Verdict: clean — no SPEC-FIX required.**

### Anchors spot-verified

All anchors resolve in the audited artifact set (this feature has no separate signed-off design-doc file; research.md IS the frozen Phase-0 authority, cross-checked against live source):

- `research.md D-1` (merge site, validator-private surface) — resolves; cross-checked against `session.cpp:992`/`:1233`, `parser.hpp:321-347,578-590` (parser-shared `field_valid_for`/`classify_fn_` untouched, `unknown_fields()` hardcodes 8/9/10 exempt independent of this feature).
- `research.md D-2` (framing source/gating, full-set decision, NoHops recursion) — resolves; cross-checked against `dictionaries/FIXT11.xml:32-36,116-131,209-212` (NoHops group + all cited tag datatypes: 34=SEQNUM, 1156=INT, 52=UTCTIMESTAMP, 627=NUMINGROUP, 628=STRING, 629=UTCTIMESTAMP, 630=SEQNUM) and `field_type.hpp` (SeqNum/Int/NumInGroup→Int, UtcTimestamp→String reduction).
- `research.md D-3` (Gate A N1, immediate-enclosing group-gating) — resolves; cross-checked against `xml_loader.cpp:522-563,650-663` (member record at exactly `:557-559` confirming the C2 fix; recursion hardcodes `group_scope_component_required=true`, confirming the "drops greq" claim).
- `research.md D-4`/data-model.md E-4 (codegen plan-identity fork) — resolves; cross-checked against `emit_builders.cpp:251-282,600-660,723` (`compute_signature`, intern call at `:649`, `item.group_required` at `:656` — confirmed NOT folded into the child's `signature`, the exact gap the fork closes).
- `research.md D-5`/D-6/D-7, N2, N3 — resolve; cross-checked against `tasks.md` T001/T012/T015/T019/T020/T021/T024 which implement each.
- `contracts/validation-acceptance.md` clauses 1-5 and Invariants — resolve; consistent with spec.md FR-001/002/003/003a/004/009/011 and data-model.md E-1/E-2.
- `contracts/census-and-parity.md` (gating rule, census contract, parity golden contract, scope) — resolves; consistent with spec.md FR-005/006/007/011, SC-002, and data-model.md E-3/E-4.
- `validator.hpp:170` (Step-1 gate), `:264` (`consume_group`), `:467`/`:501-520` (type-check/Int arm) — resolve at or within 1-3 lines of the cited line numbers (expected pre-implementation drift).
- `table_view.hpp:259-263/273-276` (`field_valid_for`/`valid_tags_for`), `:398-401` (`field_type_of`) — resolve almost exactly (`:259-262`, `:273-276`\*≈, `:398-401` exact).
- `dictionary.cpp:358` (`as_table_view()`) — resolves exactly.
- QuickFIX `addXMLGroup` (cited in research.md D-3/contracts/census-and-parity.md) — **not locally re-verified**: `reference-engines/quickfix-cpp` is absent on this tree (gitignored, per N2 — expected). This is inherited 079-era precedent; corroborated downstream at `/verify` by the T020 parity golden, not blocking this audit (see CHK009 disposition).

Design-doc revision: no separate signed-off design doc for this feature — research.md IS the frozen Phase-0/Gate-A authority (plan.md `## Gate A`, 2 rounds converged 2026-07-19, both recorded in plan.md with Codex+Opus review file paths).

### CodeGraph / source lookups performed

Anchor verification was done via direct source `Read`/`grep` (stronger evidence than a CodeGraph symbol search per the advisor's guidance, since exact line-range and datatype content needed inspection, not just symbol existence):

- `include/fixpp/wire/validator.hpp` — Step-1 gate, `consume_group`, `check_field_type`/Int arm.
- `include/fixpp/dict/table_view.hpp` — `field_valid_for`, `valid_tags_for`, `field_type_of`.
- `src/dictionary/dictionary.cpp` — `as_table_view()`.
- `src/dictionary/xml_loader.cpp` — `expand_field_list`, member-record line, recursion greq-drop, `kVersionTable`.
- `src/session/session.cpp` — `inbound_tv_` construction, strict-validator construction, `pd_parser` construction.
- `include/fixpp/wire/parser.hpp` — `unknown_fields()` (8/9/10 hardcoded exemption), `classify_fn_` binding to `field_valid_for`.
- `tools/codegen/fixpp-codegen/emit_builders.cpp` — `compute_signature`, intern call, `item.group_required`/`item.required`.
- `dictionaries/FIXT11.xml` — `<header>`/`<trailer>` field list, NoHops group, all cited tag datatypes.
- `include/fixpp/dict/field_type.hpp` — `field_data_type → field_type` reduction table.
- `is_fixt_framing_tag` — confirmed NOT present in current source (expected; new symbol this feature introduces, not a defect).

### Re-run /speckit-analyze?

**NO.** Zero `SPEC-FIXED` dispositions were applied in this audit — the spec/contract/data-model/tasks artifact set is unchanged by this pass (only the checklist file itself was edited, which `/speckit-analyze` does not consume). The two prior `/speckit-analyze` fixes (C1, C2) the orchestrator applied before this audit remain the only artifact edits, and `/speckit-analyze` was already re-scoped to have produced those findings — no new drift was introduced.

### Verdict

**GREEN** — pipeline.md step 9 satisfied; `/speckit-implement` (step 10) may proceed.

1. Zero un-dispositioned `[ ]` boxes: confirmed (28/28 ticked `[x]` with exactly one disposition tag each).
2. Every cited anchor verified to resolve in the signed-off revision: confirmed, with one noted non-blocking exception (QuickFIX `addXMLGroup` C++ source not locally re-verifiable — `reference-engines/quickfix-cpp` absent, expected per N2; inherited precedent, corroborated at `/verify`).
3. No Completeness/Clarity/Consistency item closed as WAIVED: confirmed (0 WAIVED total).
