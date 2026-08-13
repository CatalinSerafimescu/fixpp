# ABI & C-ABI Requirements Quality Checklist: Structural Repeating-Group Detection

**Purpose**: Validate that this feature's **requirements** about the C-ABI — the frozen surface, and the
deliberate *behavior* change behind it — are complete, unambiguous, consistent, and measurable.
Audience: **Gate B reviewer** (formal pre-merge gate, Article X domain).
**Created**: 2026-07-30
**Feature**: [spec.md](../spec.md) · contract [group-detection.md](../contracts/group-detection.md) C4

**Scope note.** 082 changes C-ABI *behavior* for FIX 4.0/4.1/4.2 group reads while changing **no**
surface. That asymmetry is the whole Article X story here, and it is what these items interrogate.

## Requirement Completeness

- [x] CHK001 Are the requirements for an unchanged C-ABI surface stated as something to be **verified** rather than asserted as true-by-omission? [Completeness, Spec §FR-017] — PASS: FR-017 requires verification via the `nm`-based symbol golden; `tasks.md` T049b names three verification legs (zero-diff assert, `AbiSymbolGolden.*` tests run, `abi-golden.yml` green), not omission.
- [x] CHK002 Is the frozen ABI version value itself specified, rather than left as "unchanged"? [Completeness, Spec §FR-017] — PASS: `spec.md` FR-017 names `1.5.0` explicitly, and `FIXPP_C_ABI_VERSION` is pinned in T049b leg (i).
- [x] CHK003 Are the specific artifacts that must not move enumerated — symbol golden, error-enum slots, header set — rather than referred to collectively? [Completeness, Spec §FR-017, §SC-009] — PASS: `tasks.md` T049b enumerates `src/capi/`, `include/fixpp/capi/`, `error.h`, `version.h` (zero diff), the `nm` symbol golden (`CabiSymbolSetUnchanged`/`ErrorEnumUnchanged`), and `tests/core/test_020_error_completeness.cpp`'s error-enum slot pin — not a collective reference.
- [x] CHK004 Does the spec state which of the `fixpp_group_*` / `fixpp_msg_*` families change behavior and which do not? [Completeness, Contract §C4.4] — PASS: contract C4.4 splits "Read family — CHANGES" vs "Write family — ALREADY WORKS" with named call sites for each.
- [x] CHK005 Are requirements defined for the C-ABI **write** path, given it already resolves group-ness structurally and therefore does *not* change? [Coverage, Contract §C4.4] — PASS: C4.4's write leg + FR-006's naming of the four write call sites (`message_write.cpp:157/719/812/923`) + K6b's cross-path pin.
- [x] CHK006 Is the new loader-rejection error type specified as reusing an existing exception class, with the "no new `fixpp::core::error` variant" consequence stated? [Completeness, Spec §FR-023] — PASS: FR-023's "Error type" bullet states this explicitly, citing `error.hpp:44/98/18-27`.
- [x] CHK007 Are requirements stated for what a C-ABI consumer observes when a dictionary now fails to load that previously loaded? [Gap, Spec §FR-023] — **SPEC-FIXED**: added a "C-ABI-observable consequence" bullet to FR-023 (`spec.md`) stating `fixpp_dict_load_from_xml` (`src/capi/dictionary.cpp:44-61`) already wraps every load in a generic `catch (...)` → `FIXPP_ERR_CAPI_CONFIG_INVALID`, so the new rejection surfaces exactly as any other malformed-dictionary load error — no new error code. Verified at the source (`catch (...) { *out_dict = nullptr; return FIXPP_ERR_CAPI_CONFIG_INVALID; }`). No new task needed — this is pre-existing generic handling.

## Requirement Clarity

- [x] CHK008 Is "no silent C-ABI break" distinguished from "no C-ABI change", given behavior demonstrably changes? [Clarity, Spec §FR-017] — PASS: contract C4.4's header states "No symbol, signature, or `FIXPP_C_ABI_VERSION` change. The behavior change behind that unchanged surface is **asymmetric**" — surface and behavior are explicitly separated, not conflated.
- [x] CHK009 Is the predicate that the runtime tier must share with the C-ABI write path named explicitly, rather than described as "the same accessor"? [Clarity, Spec §FR-006] — PASS: FR-006 names `Dictionary::group_first_field(t) != 0` explicitly as the shared predicate.
- [x] CHK010 Is the rejection of any *alternative* accessor stated normatively, so an implementer cannot substitute an equivalent-looking one? [Clarity, Spec §FR-006, Research §D-1] — PASS: FR-006's closing sentence: "Adopting any *other* accessor in `as_table_view()` — including `Dictionary::group(t).has_value()` — is non-conforming"; contract P4 repeats it normatively.
- [x] CHK011 Are the diagnostic contents for the new load error specified precisely enough to be asserted, rather than "a helpful message"? [Measurability, Spec §FR-023] — PASS: FR-023's "Diagnostic content" bullet names the group's `name` attribute and `no_tag` and "no member resolved" as the required content, following `error.hpp:73`'s convention.

## Requirement Consistency

- [x] CHK012 Do the C-ABI claims in the spec, the contract's C4 section, and the plan's Article X row agree with each other? [Consistency, Spec §FR-017, Contract §C4, Plan Constitution Check] — PASS (verified post-edit): `spec.md` FR-017/SC-009 (now `nm`-gate-worded), `contracts/group-detection.md` C4.4, and `plan.md`'s Article X row ("no `capi/` edit, no symbol-golden or abidiff regeneration") all agree — no symbol/signature/version change, read-family behavior changes.
- [x] CHK013 Is the "behavior changes, surface does not" position stated identically wherever it appears, without one locus implying a surface change? [Consistency, Spec §FR-017, Contract §C4.4] — PASS: same wording pattern in spec Context ("asymmetric… structural on the C-ABI write side"), FR-017, and contract C4.4.
- [x] CHK014 Does the read/write split in the contract's C4.4 remain consistent with the acceptance scenarios that are scoped to reads only? [Consistency, Contract §C4.4, Spec §US1 AC3] — PASS: US1 AC3, SC-008, SC-008a are all read-scoped and name `validate_inbound_messages` OFF explicitly, matching C4.4's read leg; no acceptance scenario claims a write-family behavior change.
- [x] CHK015 Are the error-class choices for the two loaders consistent with each loader's existing disposition for malformed structure? [Consistency, Spec §FR-023] — PASS: FR-023 cites `xml_loader.cpp:584`'s existing sibling `xml_parse_error` disposition and `orchestra_parse_error`'s standing convention; verified at the source — `:584` throws `xml_parse_error` for a `<group>` with no matching `<field>`, and `orchestra_parse_error : xml_parse_error` (confirmed in `error.hpp:88-100`).

## Acceptance Criteria Quality

- [x] CHK016 Can the frozen-surface requirement be objectively verified by a named, runnable gate rather than by inspection? [Measurability, Spec §FR-017, §SC-009] — PASS: T049b names `capi_pure_tests AbiSymbolGolden.CabiSymbolSetUnchanged` / `.ErrorEnumUnchanged` and `.github/workflows/abi-golden.yml` as the runnable gates.
- [x] CHK017 Is the success criterion for the unchanged surface expressed as a checkable outcome rather than an absence of activity? [Measurability, Spec §SC-009] — PASS (post-edit): SC-009 now ties to "the `nm`-based `abi-golden.yml` gate… stays green with no regeneration" — a checkable CI outcome, not silence.
- [x] CHK018 Are the newly-correct C-ABI group reads expressed as a criterion an integrator could confirm, rather than as "membership-bounded"? [Measurability, Spec §US1 AC3, §SC-008] — PASS: US1 AC3 names the concrete before/after: "`TYPE_MISMATCH`/absent" before, a membership-bounded read after, on a named message (`MarketDataSnapshotFullRefresh`) and tag (`NoMDEntries(268)`) — a confirmable before/after pair, not a bare adjective.
- [x] CHK019 Is the cross-path equivalence between the runtime registration set and the C-ABI write path's accepted set stated as a two-directional set comparison? [Measurability, Contract §K6b] — PASS: K6b states "`fixpp_msg_group_begin(t)` succeeds for exactly the same tag set that `as_table_view()` registers in the bare store, **both directions**."

## Scenario Coverage

- [x] CHK020 Are requirements defined for a C-ABI consumer on a dictionary whose group set **grows** (FIX40/41/42)? [Coverage, Spec §US1] — PASS: US1's Independent Test + AC3 + C4.4 read leg.
- [x] CHK021 Are requirements defined for a C-ABI consumer on the six dictionaries whose group set does **not** change? [Coverage, Spec §FR-014] — PASS: FR-014/US1 AC5 pin the registered set exact-equal for the six unchanged dictionaries; since C-ABI reads resolve through the same `table_view`/`group_first_field` (C1.1), an unchanged registered set implies unchanged C-ABI read behavior for those six — covered by implication through the single-predicate contract (C1.3 P4), not left unstated.
- [x] CHK022 Are requirements defined for the FIX43 case where exactly one tag moves in each direction of interpretation? [Coverage, Spec §US3] — PASS: US3 AC2 states a typed/C-ABI group read on tag 576 returns membership-bounded; AC3/AC4 cover tag 82's unchanged plain-field enforcement.
- [x] CHK023 Are requirements stated for the nested-group C-ABI read path, given FIX42 has nested occurrences? [Coverage, Spec §US1 AC4] — PASS: C4.4's general read-family bullet ("`fixpp_msg_get_group` and friends" begin returning membership-bounded results) covers nested reads generically since nesting is a detection/registration consequence, not a separate C-ABI code path; US1 AC4 covers the typed nested accessor. The pre-existing nested-*extraction* residuals (distinct from detection) are now explicitly scoped out — see CHK024.
- [x] CHK024 Is the pre-existing nested positional defect explicitly held out of scope so a reviewer does not read it as regressed? [Boundary, Spec §Assumptions] — **SPEC-FIXED**: `spec.md` § Assumptions had no such statement (grepped for "nested"/"positional" — none). Added a bullet naming `spec/behaviors-and-limitations.md` L-065-1 (fixed) and L-065-2 (deferred, #184) — the C-ABI/typed nested-group *extraction* residuals (`offset_table.cpp`, `message_read.cpp`) — as unaffected, since 082 changes only detection (registration/discovery), not extraction/slicing.

## Edge Case Coverage

- [x] CHK025 Are requirements defined for a tag that is a group count in one dictionary and a plain field in another? [Edge Case, Spec §FR-003] — PASS: FR-003 + US3 + C2's FIX43/FIX44 cross-check.
- [x] CHK026 Is the behavior specified for a `<group>` declaring no member, including why it cannot be represented rather than merely that it is rejected? [Edge Case, Spec §FR-023, Contract §C1.3] — PASS: FR-023 + § Edge Cases + contract P1-NON give the representational reason (`table_view::set_group_first(t,0)` would insert member tag 0; the 063 context store's `members.empty()` guard).
- [x] CHK027 Are requirements stated for a member-less `<group>` appearing at a **non-first-seen** occurrence of its count tag? [Edge Case, Spec §FR-023, Contract §K11] — PASS: FR-023's "fires on ANY member-less occurrence" bullet + K11's fixture requirement, both explicit.
- [x] CHK028 Is the ambiguity of the delimiter sentinel documented as scoped to inputs the loaders now reject, rather than as fully resolved? [Clarity, Contract §C1.3 P1-NON] — PASS: P1-NON's "Do not overstate it" clause states the sentinel "is still ambiguous *when read in isolation*"; only the reachable input is removed.

## Dependencies & Assumptions

- [x] CHK029 Is the assumption that no vendored dictionary declares a member-less `<group>` recorded with its measurement source rather than asserted? [Assumption, Spec §FR-023, Contract §K7] — PASS: FR-023's no-regression leg cites `contracts/predicate_census.py` (S0) plus an independent walk of FIX40/41/42/43 as the measurement source.
- [x] CHK030 Are the already-structural C-ABI write sites documented as a dependency the runtime predicate choice rests on? [Dependency, Spec §FR-006, Research §D-1] — PASS: FR-006 names all four write call sites; research D-1 states the runtime predicate "converges" onto them.
- [x] CHK031 Is the dependency on the repo's current symbol-golden gate (rather than a retired tool) stated so the verification target is unambiguous? [Dependency, Spec §FR-017] — **SPEC-FIXED**: FR-017 and SC-009 (`spec.md`) previously said "the abidiff baseline are/is untouched" — stale, since abidiff was retired 2026-06-22 (confirmed via `.github/workflows/abi-golden.yml`'s header comment) and the `nm`-based symbol golden is the current gate (`tasks.md` T049b already said this correctly). Rewrote both to name the `nm`-based `abi-golden.yml` gate as the current abidiff-equivalent check; also fixed `data-model.md`'s matching FR-017 pin row for the same staleness.
- [x] CHK032 Does any requirement leave open whether a load-failure surface change counts as an ABI-policy event? [Ambiguity, Spec §FR-017, §FR-023] — **SPEC-FIXED** (same edit as CHK007): the new FR-023 C-ABI-observable bullet explicitly classifies the load-failure change as NOT an ABI-policy event (generic pre-existing `catch (...)`, no new error code), closing the ambiguity.
- [x] CHK033 Is there any conflict between the "no new configuration key" requirement and the new fail-closed load behavior? [Conflict, Spec §FR-006, §FR-023] — PASS: no edit needed. FR-006's "no new configuration key" is textually scoped to "structural detection" (the predicate-gating axis); FR-023 is an unrelated load-time validation axis. The two FRs' scopes are disjoint on their face, so no conflict exists to state.
- [x] CHK034 Is the operator-facing consequence of the new load rejection captured as a documentation requirement, not left implicit? [Gap, Spec §FR-023, §SC-013] — PASS: FR-023's own "Behavior-change obligation" bullet and SC-013 both explicitly require a `spec/behaviors-and-limitations.md` row with an operator-facing release note — already explicit, not a gap.

## Audit Result

| Disposition | Count |
|---|---|
| PASS | 30 |
| SPEC-FIXED | 4 |
| DD-DECIDED | 0 |
| WAIVED | 0 |
| **Total** | 34 |

### SPEC-FIXED items
- CHK007 — added a "C-ABI-observable consequence" bullet to FR-023 naming `fixpp_dict_load_from_xml`'s generic `catch (...)` → `FIXPP_ERR_CAPI_CONFIG_INVALID` translation; affected: `spec.md` FR-023.
- CHK024 — added a § Assumptions bullet scoping the pre-existing nested-group C-ABI/typed *extraction* residuals (L-065-1, L-065-2) as unaffected by 082's detection-only change; affected: `spec.md` § Assumptions.
- CHK031 — rewrote FR-017 and SC-009 to name the current `nm`-based `abi-golden.yml` gate instead of the retired "abidiff baseline"; also fixed the matching `data-model.md` FR-017 pin row; affected: `spec.md` FR-017/SC-009, `data-model.md` FR→pin map.
- CHK032 — same edit as CHK007; the added FR-023 bullet also closes the ABI-policy-event ambiguity.

### DD-DECIDED items
None.

### WAIVED items
None.

Anchors spot-verified: `.specify/constitution.md` Article X (untouched by 082, no citation needed here — see `nfr.md`/`codegen.md` for constitution anchors), `src/capi/dictionary.cpp:44-61` (`fixpp_dict_load_from_xml`), `.github/workflows/abi-golden.yml` (abidiff-retirement comment, 2026-06-22), `src/dictionary/xml_loader.cpp:584/609/610/644/649/1017`, `include/fixpp/dict/error.hpp:18-27,40-46,65-100`, `src/capi/message_write.cpp:132-138,155-160,717-721,810-815,920-925`, `include/fixpp/dict/dictionary.hpp:108-135` — all resolve as cited, verified against the current tree.
