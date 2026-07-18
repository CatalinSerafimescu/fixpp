# Census & Parity Requirements Checklist: Runtime validator required-presence scoping

**Purpose**: Requirements-quality gate ("unit tests for the English") on the VERIFICATION legs — the non-circular required-set census (Contract 1), the per-group required-member census (Contract 1a), the QuickFIX required-set parity (Contract 2), and the two-tier verdict agreement (Contract 3). These are the legs that distinguish "correct" from "passes three examples", so their requirements must be airtight. Audience: Gate B reviewer.
**Created**: 2026-07-18
**Feature**: [spec.md](../spec.md) · [contracts/census-and-agreement.md](../contracts/census-and-agreement.md) · [data-model.md](../data-model.md)

**Note**: This checklist validates whether the REQUIREMENTS are complete, clear, consistent, and measurable — NOT whether the census code passes.

## Requirement Completeness — the message-level census (Contract 1)

- [x] CHK001 Is the independent oracle's derivation rule fully specified (own `required='Y'` AND full-ancestor-chain componentRef-usage AND, NOT enclosed by any group, EXCEPT the header/trailer carve-out tags)? [Completeness, Spec §FR-009/§US4, contracts Contract 1] — PASS: FR-009/US4 and Contract 1 state the rule verbatim with identical wording.
- [x] CHK002 Is the exact set of header/trailer carve-out tags (8/9/34/35/49/52/56/10) enumerated, and their "never dropped even under optional header/trailer componentRef" treatment stated? [Completeness, Spec §US4, contracts Contract 1] — PASS: US4 Independent Test enumerates the 8 tags and states the never-dropped-under-optional-componentRef treatment verbatim; Contract 1 repeats it.
- [x] CHK003 Is the census's actual-side surface named unambiguously as `table_view::required_fields(msg_type)` — the validator Step-2 pre-skip input, held by value — rather than "a sibling projection"? [Completeness, Spec §FR-009, data-model §Shipped required set] — PASS: FR-009 states "`dict_` is a `table_view` held by value in the validator, so this accessor IS the probe surface, not a sibling projection" verbatim; verified against live source (`table_view::required_fields` at `include/fixpp/dict/table_view.hpp:280-287`).
- [x] CHK004 Is the pre-skip treatment of tags {8,9,10} specified (Step-2 skips them; both census sides compare the pre-skip span, which also verifies 8/9/10 are present)? [Completeness, Spec §FR-009, contracts Contract 1] — PASS: FR-009 and Contract 1 both state this exactly.
- [x] CHK005 Is the codegen IR-data-structure projection leg specified as a distinct census surface (the `MessageIR` top-level list, present for every version incl. FIX42; NOT the emitted validator)? [Completeness, Spec §SC-003/§FR-009, plan.md §ir.cpp] — PASS: FR-009/SC-003 state this; plan.md §Project Structure `ir.cpp` note states "NO CHANGE... the census exercises the IR data-structure projection as a safety net"; verified `collect_top_fields` exists at `tools/codegen/fixpp-codegen/ir.cpp:533-545` and filters `group_no_tag != 0`.
- [x] CHK006 Are BOTH RED-proof witnesses specified (a: revert the `in_group` gate; b: inject a synthetic NON-header/trailer optional-component-required field), including why (b) makes the "scope narrowing does not narrow verification" claim load-bearing? [Completeness, Spec §SC-003, contracts Contract 1 RED-proof] — PASS: SC-003 and Contract 1 RED-proof both state witnesses (a) and (b) with the load-bearing rationale, and Contract 1 adds the explicit "a header/trailer field would be kept by the carve-out and so would NOT go RED" caveat on (b)'s non-header/trailer requirement.
- [x] CHK007 Is the coverage scope "all 10 dictionaries, every message" stated (not a sampled subset)? [Completeness, Spec §SC-003] — PASS: SC-003 "for every message in all 10 dictionaries."

## Requirement Completeness — the per-group census (Contract 1a)

- [x] CHK008 Are the TWO per-group legs specified distinctly — (1) context store == per-context oracle both directions for every `(msg_type, parent_path, no_tag)` (PRIMARY, drives FR-004); (2) bare store == global first-seen fallback (NOT required to equal every context)? [Completeness, Spec §FR-009a/§SC-003a, contracts Contract 1a] — PASS: FR-009a/SC-003a/Contract 1a all state the two-leg split with identical language.
- [x] CHK009 Is the unsatisfiability rationale for a single-leg requirement documented with a concrete example (FIX44 tag 295 NoQuoteEntries reused with `{}` vs `{299}`)? [Completeness, Spec §FR-009a, contracts Contract 1a] — PASS: FR-009a states the example; Contract 1a is even more concrete, naming the two component defs (`QuotCxlEntriesGrp` → `{}` vs `QuotEntryGrp` → `{299}`).
- [x] CHK010 Is the per-group RED-proof (inject/omit a per-context required member → context leg RED) specified? [Completeness, Spec §SC-003a, contracts Contract 1a] — PASS: SC-003a and Contract 1a both state this.
- [x] CHK011 Is the max-per-group-required-member-count census requirement (RC5, pinning the small-count assumption) specified within this leg? [Completeness, Spec §FR-004/§SC-002, contracts Contract 1a] — PASS: FR-004/SC-002/Contract 1a all require it, citing RC5.

## Requirement Completeness — QuickFIX parity (Contract 2) & two-tier (Contract 3)

- [x] CHK012 Is the QuickFIX parity specified as required-SET parity (via `DataDictionary::isRequiredField`), explicitly NOT a QuickFIX frame-validation harness? [Completeness, Spec §FR-010/§SC-005, contracts Contract 2] — PASS: FR-010/SC-005 state "a set-parity gate, NOT a QuickFIX frame-validation harness" verbatim.
- [x] CHK013 Is the "no vlatest/Orchestra QuickFIX row" exclusion specified with its reason (quickfix 1.16.0 does not parse Orchestra → an absent-surface row goes spuriously RED)? [Completeness, contracts Contract 2 Scope note] — PASS: Contract 2 Scope note states this exactly; US4 AS2 restates it in spec.md.
- [x] CHK014 Is the golden's supply-chain discipline specified (manifest + content hash + stale-golden regen/diff rule, 075 precedent; `-DFIXPP_BUILD_QUICKFIX_GOLDEN=ON` local-only, no CI link)? [Completeness, Spec §Assumptions, contracts Contract 2, quickstart §4] — PASS: spec Assumptions cites the 069/075 parity precedent; Contract 2 states the manifest+hash+stale-regen rule; quickstart §4 gives the concrete local-regen command behind `-DFIXPP_BUILD_QUICKFIX_GOLDEN=ON` with no CI link.
- [x] CHK015 Is the two-tier agreement corpus specified (named messages + one-per-version, conforming AND malformed, v44/v50sp2/vlatest only)? [Completeness, Spec §SC-004, contracts Contract 3] — PASS: SC-004 and Contract 3 both state the corpus and version scope identically.

## Requirement Clarity

- [x] CHK016 Is "non-circular" defined operationally — the oracle is an independent pugixml pass that MUST NOT call `XmlLoader`/`OrchestraLoader`/`build_ir()` (banner + review check)? [Clarity, Spec §US4, contracts Contract 1 Non-circularity] — PASS: Contract 1 Non-circularity states this exactly; tasks.md T015 operationalizes it as a banner + review check and clarifies the IR-projection leg's legitimate `build_ir()` call is on the actual side, not the walker.
- [x] CHK017 Is "exact set equality, both directions" unambiguous (no tag present in one and absent in the other), distinct from subset-presence? [Clarity, Spec §FR-009, contracts Contract 1] — PASS: used consistently throughout FR-009/SC-003/Contract 1 ("0 extra, 0 missing"), matching [[feedback_completeness_gate_exact_set_not_subset]].
- [x] CHK018 Is the QuickFIX extraction API named precisely (`DataDictionary::isRequiredField(msgType, tag)`, encoding the component AND-rule at `:510/:522` and per-group members at `:560/:570`) so the parity leg cannot be read as verdict-inference? [Clarity, Spec §FR-010, contracts Contract 2] — PASS: FR-010/Contract 2 name the exact API + line citations, explicitly stating "no verdict-inference or private-internals access needed."
- [x] CHK019 Is the parity-tolerance note clear (QuickFIX ANDs only the immediate enclosing component vs the oracle's full ancestor chain; 0 nested-optional-component sites so the divergence never bites)? [Clarity, contracts Contract 1, data-model §Census entities] — PASS: US4/Contract 1/data-model §Census entities all state the identical parity-tolerance note.

## Requirement Consistency

- [x] CHK020 Do spec (FR-009), data-model (§Shipped required set), and contracts (Contract 1) name the SAME actual-side surface (`required_fields()` pre-skip span) with the SAME 8/9/10 treatment? [Consistency, Spec §FR-009 vs data-model vs Contract 1] — PASS: all three use identical wording for the actual-side surface and the pre-skip 8/9/10 treatment.
- [x] CHK021 Is the three-way message-level equality (oracle == runtime `required_fields()` == IR `collect_top_fields`) stated as consistent on 8/9/10 under the pre-skip definition (IR `group_no_tag==0` filter includes top-level 8/9/10)? [Consistency, contracts Contract 1] — PASS: Contract 1 states this explicitly; verified against live `collect_top_fields` (`ir.cpp:533-545`) which filters `f.ref.group_no_tag != 0`, i.e. keeps all top-level tags including 8/9/10.
- [x] CHK022 Is the census's scope-agnostic role (it compares FULL required sets, so it would surface any component/codegen over-require as RED even though none is expected) stated consistently between plan.md and spec so the narrowed fix is not read as narrowed verification? [Consistency, plan.md §Summary vs Spec §SC-003] — PASS: plan.md §Summary and spec SC-003 both state the scope-agnostic safety-net claim with matching rationale.
- [x] CHK023 Is the FIX42 treatment consistent across the census (IR-structure leg present), the two-tier (excluded), and parity (QuickFIX covers FIX42 by set, no typed-tier row)? [Consistency, Spec §SC-003/§SC-004/§SC-005] — PASS: SC-003 includes FIX42 via the IR-structure leg; SC-004 excludes FIX42 (no typed tier); SC-005/Contract 2/US4 AS2 confirm FIX42 IS one of the 9 QuickFIX-parity dicts (only vlatest is excluded from parity) — no contradiction, each leg's inclusion/exclusion tracks a different, correctly-stated reason (typed-tier absence vs QuickFIX-parseable-schema presence).

## Acceptance Criteria Quality (Measurability)

- [x] CHK024 Is each census/parity pass condition objectively checkable (a set diff naming msg + dict + differing tag on failure)? [Measurability, contracts Contract 1 Failure] — PASS: Contract 1 Failure states "test RED with `msg`, dict, and the differing tag(s) named."
- [x] CHK025 Is the RED-proof obligation measurable (both witnesses demonstrated RED before GREEN, recorded), not asserted? [Measurability, Spec §SC-003, contracts Contract 1 "Prove both RED before GREEN"] — PASS: SC-003/Contract 1 require both witnesses proven RED before GREEN; tasks.md T016 requires recording both captures in `.specify/decisions/079-required-presence-scope-verify.md`.
- [x] CHK026 Is the durability requirement measurable (the census is a checked-in subtest/tool, not a planning-time throwaway)? [Measurability, Spec §FR-009] — PASS: FR-009 "MUST be a **durable checked-in** subtest/tool (not a planning-time throwaway)."

## Scenario & Coverage Gaps

- [x] CHK027 Does the census requirement cover the header/trailer carve-out REGRESSION detection (a carve-out drop is caught because 8/9/10 must be present in the pre-skip span)? [Coverage, Spec §FR-009, contracts Contract 1] — PASS: FR-009/Contract 1 both state "which also verifies 8/9/10 are present" as a regression-detection consequence of the pre-skip comparison.
- [x] CHK028 Is the vlatest-only blind spot (genuine optional-component, guarded by the stronger walker + synthetic RED witness) explicitly bounded vs the 9 QuickFIX dicts (independently guarded by Contract 2)? [Coverage, contracts Contract 2 Scope note] — PASS: Contract 2 Scope note states this bound explicitly.
- [x] CHK029 Is the two-tier failure interpretation specified (an unexpected mismatch LOCALIZES a missed codegen leg → adds a codegen change), so the "no codegen change" conclusion is falsifiable, not assumed? [Coverage, Spec §SC-008, contracts Contract 3 Purpose] — PASS: SC-008/Contract 3 Purpose/tasks.md T014 all state the localize-and-fix interpretation, making the conclusion falsifiable.

## Notes

- Check items off as dispositioned at /speckit-checklist-audit: `[x]` + one of SPEC-FIXED / DD-DECIDED §X / WAIVED:<reason>.
- Completeness / Clarity / Consistency gaps MUST be SPEC-FIXED or DD-DECIDED — never WAIVED (pipeline.md step 9).

## Audit Result

Audited 2026-07-18 against spec.md, plan.md, data-model.md, research.md, contracts/census-and-agreement.md, quickstart.md, tasks.md, and live source (`table_view.hpp` accessors, `ir.cpp::collect_top_fields`). This checklist covers the verification legs (Contracts 1/1a/2/3), which were the specific focus of Gate-A round 2's two P1 fixes (message-level actual-side prohibition prose, per-group bare-store over-correction) — re-derivation from source found the contract prose now internally consistent and fully cross-referenced.

| Disposition | Count |
|---|---|
| PASS | 29 |
| SPEC-FIXED | 0 |
| DD-DECIDED | 0 |
| WAIVED | 0 |
| **Total** | 29 |

### SPEC-FIXED items

None.

### DD-DECIDED items

None — every cited contract/spec clause is re-spec'd directly in spec.md/contracts/census-and-agreement.md (not merely referenced by anchor), so all items disposition as PASS.

### WAIVED items

None.

Anchors spot-verified: `spec/coverage-index.md:189` / `:184` (shared with scope-and-correctness.md audit) · `spec/behaviors-and-limitations.md` L-067-1 (line 1759) / L-077-1 (line 1817) — all resolve. Additionally spot-verified the QuickFIX line citations named in Contract 2/FR-010 (`DataDictionary.cpp:510/:522/:560/:570`) are referenced consistently across spec.md, research.md R5, and Contract 2 (not independently re-verified against the vendored quickfix-cpp source tree in this audit — that verification belongs to the T018/T019 golden-generator implementation, not the requirements-quality gate).

### Realizability sub-check

No new value-typed entity. The census/parity surfaces are test-only in-memory structures (sets, spans) built over the existing `table_view` (already a complete type) and a to-be-added `MessageIR` accessor exposure (test-reachability wiring, not a new type) — no forward-declared dependency. Verdict: **clean**.

### CodeGraph / source cross-checks performed (this audit)

- `table_view::required_fields(msg_type)` — confirmed at `include/fixpp/dict/table_view.hpp:280-287`.
- `table_view::group_required_members(no_tag)` / `group_required_members(msg_type, parent_path, no_tag)` — confirmed at `include/fixpp/dict/table_view.hpp:310-317` / `:383-394`.
- `collect_top_fields(MessageIR const&)` — confirmed at `tools/codegen/fixpp-codegen/ir.cpp:533-545`, filters `group_no_tag != 0`.
- `main.cpp:132 if (ir.ns != "v42")` — confirmed FIX42 builder/validator emission skip.
- `in_group` flag — confirmed present in both `src/dictionary/xml_loader.cpp` and `src/dictionary/orchestra_loader.cpp`.
