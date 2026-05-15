# Pre-Gate-B Self-Review Checklist: 002-dictionary-xml-loader

**Purpose**: Author self-review of requirements quality across four themes — C++ API contract, memory + error model, test seam coverage, wire-format grammar acceptance — before opening the PR and triggering `/gate-b`. Items test the *requirements themselves*, not the implementation.
**Created**: 2026-05-14
**Feature**: [spec.md](../spec.md)
**Audience**: feature author, pre-Gate B
**Depth**: standard (20 items)
**Theme tags**: `T1` = C++ API contract; `T2` = memory + error model; `T3` = test seam coverage; `T4` = wire-format / XML grammar

## Requirement Completeness

- [ ] CHK001 Are AC-D1..D8 each bound to a named test file in tasks.md, with the per-AC assertion explicitly enumerated (no "AC-D1..D7 covered" hand-waves)? [Completeness, T3, tasks.md Phase 3..6 / spec.md §4.2]
- [ ] CHK002 Does spec.md specify the full failure-translation contract for every error origin — `std::filesystem` access failure, `pugi::xml_parse_result.status != ok`, semantic defects (AC-L5..L8), PMR `std::bad_alloc`, and out-of-vocabulary `<fix>` — with one exception type per origin? [Completeness, T2, spec.md §4.1 AC-L2..L9 / research.md D-4]
- [ ] CHK003 Are each of the four shipped FIX versions (FIX42/44/50SP2/FIXT11) paired with both a concrete AC-D6 headline list AND an AC-D7 delimiter list, including the FIX42 "no `Parties`" sub-bullet? [Completeness, T4, spec.md §4.2 AC-D6/D7]
- [ ] CHK004 Does data-model.md enumerate every PMR allocation site (one row per source) so AC-P1's "every byte allocated from `mr`" can be verified by inspection without re-reading the implementation? [Completeness, T2, data-model.md §"PMR allocation accounting"]
- [ ] CHK005 Are both the canonical methods (`field_ref`, `required_fields`, `field_valid_for`, `group_first_field`, `length_pair_data_tag`) AND the spec.md descriptive aliases (`field`, `field_by_name`, `component`, `group`, `messages`) documented as part of the v1.0 surface with explicit return-type and `noexcept` clauses? [Completeness, T1, research.md D-20 / contracts/dictionary.hpp]

## Requirement Clarity

- [ ] CHK006 Is "deterministic" quantified — does NFR-002-4 specify *both* the within-process invariant (run-to-run on the same machine, verified by test seam #5) AND the cross-machine claim (satisfied by construction via D-6 bytewise sort, not by a runtime test)? [Clarity, T2, spec.md §6 NFR-002-4 / research.md D-6]
- [ ] CHK007 Is the "≤ 500 ms" wall-clock budget qualified with a storage-medium constraint (Linux-native ext4 vs. WSL2 cross-mount `/mnt/c/...`)? [Clarity, T2, quickstart.md §4 storage assumption / spec.md §6 NFR-002-1]
- [ ] CHK008 Is "the loader is single-pass" defined precisely (single DOM-walk vs. single character-stream pass)? Without disambiguation, a reviewer cannot judge whether forward-only XML streaming is in scope. [Clarity, T4, spec.md §Assumptions A4]
- [ ] CHK009 Is the `_reserved == 0` discipline phrased as a load-time emit invariant *and* a forwards-compat read rule (ignored-on-read in v1.0), so a future v1.x extension reading `_reserved` is unambiguous to the writer of v1.0 code? [Clarity, T1, spec.md §4.3 AC-F4 / data-model.md Entity 1]

## Requirement Consistency

- [ ] CHK010 Do spec.md, plan.md, research.md, data-model.md, and `contracts/error.hpp` use the same names for the exception types (`xml_parse_error` / `unknown_version_error` / `xml_oom_error`) AND the same names for their enum mates (`dict_xml_parse_failed` / `dict_unknown_version` / `dict_xml_oom`)? [Consistency, T2, research.md D-10]
- [ ] CHK011 Do spec.md §A3 ("move-only, no copy"), data-model.md Entity 4 "Move discipline" / "Copy is deleted" lines, and `contracts/dictionary.hpp:91-92` (`Dictionary(Dictionary const&) = delete; Dictionary& operator=(Dictionary const&) = delete;`) all agree on the copy-deletion rule with no remnant "copies are deep" wording? [Consistency, T1, spec.md §A3 / data-model.md Entity 4 / contracts/dictionary.hpp]
- [ ] CHK012 Do the seam→file mappings in spec.md §9 (10 seams), plan.md §"Test seam → file mapping" (10 + 2 cross-cutting), and tasks.md Phase 3..6 (T016..T026, T029) list the same file names with the same AC linkages? [Consistency, T3]
- [ ] CHK013 Do plan.md §"C++ headers (core)" and research.md D-3 agree on (a) the three additive `core/error.hpp` enum slots (20/21/22), (b) the new `trap_throw_or_throw<E,F>` helper in `core/decimal_helpers.hpp`, and (c) the explicit decision that NO new `core/expected.hpp` or `core/pmr.hpp` header is added? [Consistency, T1, plan.md §Project Structure / research.md D-3]

## Scenario & Edge Case Coverage

- [ ] CHK014 Are recovery flows specified for the partial-`Dictionary`-construction case — when PMR fails mid-load, are the partial tables torn down deterministically with no leak, and is the test seam (#9 OOM injection) named in spec.md as the verification site? [Coverage, Exception Flow, T2, spec.md §3 Edge Cases / AC-L9]
- [ ] CHK015 Are concurrent-construction scenarios addressed as a requirement (not only by inspection) — does spec.md or data-model.md state that calling `XmlLoader::load` from N threads on the *same* `XmlLoader` value is safe, and that two threads each holding their own `XmlLoader{}` are also safe? [Coverage, T1, research.md D-7 / spec.md §3 Edge Cases #1]
- [ ] CHK016 Are requirements specified for the cross-vocabulary case — one session host owning two distinct `Dictionary` values (FIXT11 admin-only + FIX50SP2 application-only) — with explicit non-overlap assertions (FIXT11 has no `D`; FIX50SP2 has no `Logon`)? [Coverage, T4, spec.md §3.4 US4 / AC-D6 FIXT11 sub-bullet]
- [ ] CHK017 Are XML-grammar edge cases enumerated beyond AC-L5..L8: empty `<component>`, nested-component cycles (e.g., A→B→A), `<message>` with no `<field>` rows, `<field number="0">`, `<fix major="-1">`? At least the "rejected vs accepted" disposition of each should be a defined requirement. [Coverage, Edge Case, T4, Gap — spec.md §4.1]

## Non-Functional Requirements & Measurability

- [ ] CHK018 Are the `[const §IX.1]` coverage thresholds (≥90% line / ≥80% branch) bound to the new files in `src/dictionary/` and `include/fixpp/dict/` specifically (not just engine-wide), so a partial-coverage regression in this slice fails the gate even if engine-wide coverage stays green? [Measurability, T3, plan.md Constitution Check IX.1 row / tasks.md T035]
- [ ] CHK019 Is the bench-baseline regression rule (`[const §VIII.2]` ±5%) defined with *both* an absolute ceiling (1 s on FIX50SP2 Debug per spec.md §6 NFR-002-1) AND a baseline-relative budget (median of 100 iterations, `tools/bench_compare.py --tolerance 0.05`)? Without both, a regression that stays under the absolute bar but exceeds the relative tolerance has no defined disposition. [Measurability, T2, research.md D-18 / quickstart.md §4-5 / tasks.md T031+T033]
- [ ] CHK020 Are TSan-only test targets named explicitly in the requirements (vs. "every test runs under every preset"), so AC-T2 verification is not lost in the Tier-1 matrix when the TSan preset is added/removed? [Clarity, T3, plan.md Constitution Check IX.2 row / tasks.md T021]

## Notes

- Check items off as completed: `[x]`.
- Add inline comments or findings after each item — e.g., quote the exact spec line or note "Gap: needs spec.md edit".
- Items marked `[Gap]` flag requirement quality issues that may need a `/speckit-specify` or `/speckit-plan` round before `/speckit-implement`.
- Theme tags `T1`/`T2`/`T3`/`T4` allow filtering by review focus area.
- Traceability rate: 20/20 items carry at least one document reference, marker, or `[Gap]` tag (≥80% target per skill).
- Companion checklist: [requirements.md](./requirements.md) covers the earlier `/specify`-era spec-quality validation pass (already complete).

## Review Results — 2026-05-14

Author self-review pass walked through all 20 items; 14 passed cleanly, 5 partial, 1 explicit gap. The 5 partials + 1 gap were closed via spec.md edits in the same review session (commit pending).

| Item | Verdict | Resolution |
|---|---|---|
| CHK001 | ✅ PASS | tasks.md T016/T017/T019/T021/T029 enumerate AC-D1..D8 with explicit per-AC assertions. |
| CHK002 | ✅ PASS | spec.md §4.1 AC-L2..L9 + research.md D-4 + data-model.md error-mapping table cover every origin. |
| CHK003 | ✅ CLOSED | spec.md AC-D7 expanded to enumerate per-version delimiter tags inline (FIX44/FIX50SP2/FIX42/FIXT11). |
| CHK004 | ✅ PASS | data-model.md "PMR allocation accounting" 9-row table exhaustive. |
| CHK005 | ✅ PASS | contracts/dictionary.hpp lists both canonical + descriptive surfaces with `[[nodiscard]]`+`noexcept`. |
| CHK006 | ✅ PASS | spec.md NFR-002-4 explicit on within-process vs. cross-machine claims. |
| CHK007 | ✅ CLOSED | spec.md NFR-002-1 lifted WSL2 storage-medium caveat from quickstart.md §4 into the NFR itself. |
| CHK008 | ✅ CLOSED | spec.md A4 rewritten to "single forward DOM-walk" + explicit "not a streaming SAX pass" disambiguation. |
| CHK009 | ✅ PASS | AC-F4 + data-model.md Entity 1 both state emit-zero + ignored-on-read. |
| CHK010 | ✅ PASS | grep: 101 occurrences of 6 naming tokens across 8 files — no drift. |
| CHK011 | ✅ PASS | spec.md §A3 + data-model.md Entity 4 + contracts/dictionary.hpp:91-94 agree. |
| CHK012 | ✅ PASS | Spot-checked seam→file mapping across spec.md §9, plan.md, tasks.md — consistent (minor wording drift on seam #7 fixtures path; acceptable disjunction). |
| CHK013 | ✅ PASS | plan.md §Project Structure + research.md D-3 agree on the two `core/` mods + no new core/ headers. |
| CHK014 | ✅ PASS | spec.md §3 Edge Cases + AC-L9 + seam #9 cover partial-construction recovery. |
| CHK015 | ✅ CLOSED | spec.md §3 Edge Cases gained a new bullet stating concurrent same-`XmlLoader` is safe (matches research.md D-7). |
| CHK016 | ✅ PASS | spec.md §3.4 US4 Independent Test specifies the FIXT11 + FIX50SP2 cohabitation case. |
| CHK017 | ✅ CLOSED (deferred) | spec.md §3 Edge Cases gained a paragraph mapping the four uncovered grammar defects to natural AC-L3/L4/L5/L7 landings; spec.md §10 F4 records the formal taxonomy deferral. |
| CHK018 | ✅ PASS | plan.md Constitution Check IX.1 row + tasks.md T035 scope coverage to `src/dictionary/*` + `include/fixpp/dict/*`. |
| CHK019 | ✅ PASS | research.md D-18 + quickstart.md §4-5 + spec.md NFR-002-1 define both ceilings. |
| CHK020 | ✅ PASS | plan.md Constitution Check IX.2 row + tasks.md T021 explicitly name `dictionary_concurrent_readers_test` for TSan. |

**Outcome:** all 20 items resolved. No blocker before `/speckit-implement`. Five spec.md edits landed: AC-D7 expansion, NFR-002-1 storage caveat, A4 DOM-walk phrasing, new Edge Case bullet for concurrent same-`XmlLoader`, new Edge Case paragraph + §10 F4 entry for narrow XML-grammar edge cases.
