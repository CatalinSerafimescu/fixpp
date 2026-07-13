# Requirements-Quality Checklist: Native Orchestra Reader (FIX Latest)

**Purpose**: Unit-test the *requirements* (spec/plan/tasks) for completeness, clarity, consistency, measurability, and coverage — audience: Gate B reviewer. NOT implementation verification.
**Created**: 2026-07-13
**Feature**: [spec.md](../spec.md)
**Depth**: release-gate | **Focus**: reader contract · version identity · fail-closed / registry guard · scope boundaries

## Requirement Completeness

- [x] CHK001 Are the Orchestra→internal mapping rules specified for every `fixr:repository` construct the reader consumes — datatypes, codesets, fields, components, groups, messages, header/trailer? [Completeness, Spec §FR-002, data-model §A] — PASS: data-model.md §A tabulates every construct (root/version, datatypes, global fields, codesets, unionDataType, components, group+numInGroup, message, fieldRef required, header/trailer, scenario N/A); each row cites a concrete internal target + mapping rule.
- [x] CHK002 Is the NumInGroup **count-field** handling (inclusion in the parent run with `type==NumInGroup`) and component-contained-group parent-path expansion explicitly specified rather than implied? [Completeness, data-model §A, Spec §FR-004] — PASS: data-model.md §A group row states (i) count-field inclusion in the parent run with `type==NumInGroup`, (ii) nested group count-field shape, (iii) component-contained groups take parent-path from the expansion site, not the component definition (Gate A r1 addition); mirrored in FR-004 and contracts/orchestra_loader.md.
- [x] CHK003 Are requirements defined for the vendored source's provenance artifacts (pinned commit, content hash, `UPSTREAM.txt`, Apache-2.0 §4 `LICENSE`/`NOTICE`)? [Completeness, Spec §FR-007, §SC-007] — PASS: FR-007, SC-007, US4 acceptance scenarios 1–2, tasks T001/T002/T021 all name commit+sha1+UPSTREAM.txt+LICENSE/NOTICE explicitly.
- [x] CHK004 Is a fuzz-harness requirement present for the new parser (parser-touching → `[const §VII.7]`), not just functional tests? [Completeness, tasks §T023] — PASS: plan.md Constitution Check VII row + tasks T023 (libFuzzer harness over `load_from_string`, `[const §VII.7]` verified to exist at constitution.md Article VII §7).
- [x] CHK005 Are requirements stated for the constitution amendment (version-set widening) and the coverage-index bidirectional entries as landing obligations? [Completeness, Spec §Dependencies, tasks §T003/§T027] — PASS: spec.md Dependencies item 1 + Normative References blocking-obligation note + tasks T003 (amendment) / T027 (coverage-index close-out), both marked mandatory close-out preconditions.

## Requirement Clarity

- [x] CHK006 Is the FIX Latest version identity unambiguous — `session_version::vlatest` distinct at the dictionary layer, wire application version = existing `v50sp2`/ApplVerID 9, no `application_version::vlatest`? [Clarity, Spec §FR-005] — PASS: FR-005 + Clarifications + Assumptions state this identically and unambiguously; cross-verified against `version_profile.hpp:151-174` (`render_appl_ver_id` renders application_version→char, would be non-injective if a `vlatest` member were added — confirms the "no new member" constraint is load-bearing, not stylistic).
- [x] CHK007 Is the FR-010 registry fail-loud guard's **mechanism** disambiguated enough to implement (release-effective, not an NDEBUG-stripped assert; preferred fatal-in-ctor; fallback error-return), or does it leave a blocking ambiguity? [Clarity, Spec §FR-010] — PASS: FR-005/FR-010 deliberately defer the exact mechanism choice to `/speckit-tasks` between two release-effective options; tasks.md T020 concretizes the choice (contained fatal-in-ctor, preferred) and T019 concretizes the test wiring (`EXPECT_DEATH` inside the grouped bucket) — no blocking ambiguity remains at the implement boundary. `version_registry.cpp:60-73` (ctor, `noexcept`) and `:71-72` ("Last writer wins" / `entries_[idx] = d;`) verified as the exact guard site.
- [x] CHK008 Is "minimal semantic model" quantified precisely — codeset values+descriptions preserved, `unionDataType` second arm dropped, scenarios N/A for EP303 — rather than a vague "flattened"? [Clarity, Spec §FR-002, Assumptions] — PASS: Assumptions §"Minimal semantic-richness dial" states exactly these three points; Edge Cases section repeats them with the concrete `SettlType` example.
- [x] CHK009 Is the message-count success criterion an exact number (181, zero drops) rather than an approximate? [Clarity, Measurability, Spec §SC-001/§FR-003] — PASS: SC-001/FR-003 both state "exactly 181 messages, zero drops"; matches spike Deliverable #3 grade-1 finding (181, corrected from the direction doc's "~179" estimate).

## Requirement Consistency

- [x] CHK010 Is the "downstream unchanged" claim consistently scoped to **read-path** consumers (validator/`table_view`/C-ABI) with codegen explicitly excluded (`build_ir` throws on `vlatest`) across spec Overview, FR-001, SC-004, US1-scenario-3, plan, contracts? [Consistency, Spec §FR-001/§SC-004] — PASS: verified verbatim consistency across spec.md Overview, FR-001, SC-004, US1 scenario 3, plan.md Summary, and contracts/orchestra_loader.md's top follow-on note. CodeGraph/grep-verified `tools/codegen/fixpp-codegen/ir.cpp:249-251` (`XmlLoader loader; ... loader.load(...)`, not loader-polymorphic) and the throw at `ir.cpp:265-270` ("XML version is not a codegen-target version") — the codegen-exclusion claim is grade-1 accurate, not just asserted.
- [x] CHK011 Is SC-005's "no collision" consistently layered — distinct at the `session_version` layer, deliberately shared at the `application_version` registry slot (guarded by FR-010) — with no contradiction? [Consistency, Spec §SC-005/§FR-010] — PASS: SC-005 text explicitly separates the two layers and cross-references FR-010; FR-010/data-model §B (B2/B4) and L-074-1 all layer identically. No contradiction found.
- [x] CHK012 Is the pinned sha1 `26f60db1…` treated consistently across spec/research/quickstart/tasks (a grade-1 official integrity pin asserted on fetch — not simultaneously "provisional / do not assert")? [Consistency, Conflict, research §D-6, tasks §T001] — **SPEC-FIXED**: found a genuine cross-artifact drift. `research.md` D-6 and `tasks.md` T001 carry the reconciled position (sha1 `26f60db1c1f52d169d3b6825ac68800abf487fde` is the spike's grade-1 recorded sha1 of the **OFFICIAL** file per spike-doc line 36 — assert-and-STOP-on-mismatch), but `data-model.md` §C "Vendored source" row and `quickstart.md` Prerequisites still carried the stale pre-reconcile language ("provisional/unverified... do **not** assert against the pre-filled constant") — directly contradicting T001 and even self-contradicting quickstart.md's own Provenance-check section two screens below (which already asserts `== 26f60db1…`). Edited both `data-model.md` (§C Vendored-source row) and `quickstart.md` (Prerequisites bullet) to match the reconciled research.md/tasks.md assert-and-stop position. Spike-doc line 36 spot-verified: `FIX Standard/OrchestraFIXLatest.xml — 7,525,180 bytes, sha1 26f60db1c1f52d169d3b6825ac68800abf487fde` (the official file, not `OrchestraFIXLatest_relabeled.xml`).
- [x] CHK013 Is the edit-site count outside the reader consistent (three: `version_profile.hpp`, `version_registry.cpp`, `dictionary.hpp` friend) across plan/research/data-model? [Consistency, plan §Structure Decision] — PASS: plan.md Structure Decision, research.md D-1/D-5, and data-model.md §B/§C all enumerate the same three files (version_profile.hpp enum add; version_registry.cpp two logical edits — forced switch arm + FR-010 guard; dictionary.hpp one-line friend). Count is scoped to the **chosen fatal-in-ctor** path (T020); the error-returning fallback would additionally ripple into `version_registry.hpp`/`engine_config.hpp`/`engine.cpp` — this conditionality is stated consistently in plan.md and research.md D-5, not a hidden discrepancy. Grade-1 spot-verified: `version_profile.hpp:32-43` (session_version enum), `version_registry.cpp:60-73`/`:71-72` (ctor + last-writer-wins), `dictionary.hpp:188`/`:192` (`friend class XmlLoader;` / private handle-ctor) all resolve exactly as cited.

## Acceptance Criteria Quality (Measurability)

- [x] CHK014 Can every SC-001..007 be objectively verified by a named test/artifact (count, RED proof, full parent-path, no-regression, provenance)? [Measurability, Spec §SC-001..007] — PASS: SC-001→T011 (`OrchestraLoad.LoadsEP303`); SC-002→T022(a-c) discriminating fail-closed matrix; SC-003→T015 (`OrchestraGroups.DeepAndReused`, full parent path); SC-004→T025 (full ctest no-regression) + structural no-source-change claim; SC-005→T018/T019 (`OrchestraVersionIdentity` + registry guard); SC-006→T024 (`OrchestraLegacy.NoRegression`); SC-007→T021 (`OrchestraProvenance`). Every SC maps to a named task/test.
- [x] CHK015 Is the depth-7 group criterion measurable as a **full parent path** (`296→295→555→40241→41686→41680→41683`) rather than a weaker "non-empty lookup"? [Measurability, Spec §SC-003] — PASS: SC-003, FR-004, data-model §"Validation rules", contracts Behavioral-contract row, and tasks T015 all explicitly require the **full path** assertion "not merely a non-empty lookup", with the reused-tag-555 non-empty-per-parent case kept as a separate, weaker-but-adequate check for the reuse property (not conflated with the depth-7 full-path assertion).
- [x] CHK016 Is the fail-closed criterion (SC-002) specified as a **discriminating** RED proof (used-unknown throws; unused declaration does not; union-primary-arm throws) rather than a single non-discriminating case? [Measurability, Spec §SC-002, contracts fail-closed matrix] — PASS: SC-002 + data-model §"Validation rules" + contracts fail-closed test matrix all enumerate the three-way discrimination (used-unknown throws / unused-unknown does not / union-primary-unknown throws) plus the valid-EP303 negative arm; tasks T022(a-c) implement all three.

## Scenario & Edge-Case Coverage

- [x] CHK017 Are fail-closed requirements defined for every malformed-input class — unknown datatype, wrong root, wrong-grammar file (QuickFIX fed to Orchestra AND the reverse), truncated XML, dangling component ref, missing required attributes? [Coverage, Spec §FR-006/§FR-009, Edge Cases] — PASS: FR-006/FR-009 + Edge Cases section name every one of these classes explicitly, including the bidirectional cross-feed (QuickFIX→OrchestraLoader and Orchestra→XmlLoader); tasks T022(a,d,e,f,g,h) implement the matrix, with T022(h) spot-verified as a regression pin on the pre-existing `xml_loader.cpp:284-285` non-`"fix"`-root check.
- [x] CHK018 Are the union-datatype and codeset-flattening edge cases specified deterministically (drop second arm without error; preserve values+descriptions; the drop must not mask an unknown *primary* arm)? [Edge Case, Spec §FR-002, contracts] — PASS: Edge Cases section + FR-002 + contracts fail-closed matrix all specify this deterministically, with the "must not mask an unknown primary arm" safeguard called out explicitly (data-model Validation rules, tasks T022(c)).
- [x] CHK019 Is the multi-dictionary registry coexistence scenario (FIX50SP2 + FIX Latest) addressed with an explicit fail-loud requirement rather than left to silent last-writer-wins? [Coverage, Exception Flow, Spec §FR-010, §L-074-1] — PASS: FR-010 + L-074-1 + Gate A round-1 clarification session all address this explicitly; `version_registry.cpp:71-72` ("Last writer wins if two dicts map to the same version." / `entries_[idx] = d;`) spot-verified as the exact silent-loss site the guard targets.
- [x] CHK020 Is the legacy no-regression scenario (all nine QuickFIX dicts, `XmlLoader` untouched) an explicit requirement? [Coverage, Spec §FR-008/§SC-006] — PASS: FR-008/SC-006 explicit; tasks T024 names the regression pin.

## Non-Functional Requirements

- [x] CHK021 Are memory-discipline requirements stated (PMR-allocated build on the supplied `mr`; `bad_alloc` → `xml_oom_error`; `assert(mr)`)? [NFR, contracts Behavioral contract] — PASS: contracts/orchestra_loader.md Behavioral-contract "Memory" row states this exactly; mirrors the existing `XmlLoader::load`/`load_from_string` pattern spot-verified at `xml_loader.cpp:912-935` (`assert(mr != nullptr...)` + `trap_throw_or_throw<xml_oom_error>`).
- [x] CHK022 Is the additive / C-ABI-frozen constraint (no `core::error` append, C-ABI 1.5.0 untouched) stated as a requirement? [NFR, Spec §FR-001/§FR-008, plan Constitution Check §X] — PASS: FR-001/FR-008 + plan.md Constitution Check Article X row + research.md D-4 (error-strategy rationale) all state this; `error.hpp:16-26` header note + the `group_delimiter_collision_error` precedent (`error.hpp:67-85`) spot-verified as the exact reused pattern.
- [x] CHK023 Is the test-grouping convention (grouped `dictionary_orchestra_tests` bucket, `ctest -L orchestra`, no `gtest_discover_tests`) captured as a requirement per `[const §VII.8]`? [NFR, tasks §T004] — PASS: tasks T004 + quickstart.md + plan.md all specify this; constitution Article VII §8 spot-verified to exist and match verbatim (grouped-bucket rule, `gtest_discover_tests` prohibited, select by `ctest -L`).

## Dependencies & Assumptions

- [x] CHK024 Is the pugixml-reuse assumption (already pinned `1.15`, no new dependency) documented and validated? [Assumption, Spec §Assumptions] — PASS: Assumptions section + plan.md Technical Context state pugixml/1.15 is reused, PRIVATE to `fixpp_dictionary`; spot-verified `conanfile.py:66` (`"pugixml/1.15",`) matches. Plan explicitly records the dependency-management currency check ("1.16 exists but has no Conan recipe yet — no bump required") per the CLAUDE.md anchor-staleness rule; this audit did not re-hit the live Conan Center registry (no network tool in this session) but the internal repo-state check is consistent and the diligence is recorded at the correct pipeline stage (`/speckit-plan`).
- [x] CHK025 Is the ApplExtID(1156)=303 deferral documented as an explicit, scheduled follow-on (not silently dropped) and is the `version_registry`-rekeying coupling to that phase recorded? [Assumption, Dependency, Spec §Non-Goals, §L-074-1] — PASS: Non-Goals + L-074-1 + FR-010 + FR-005 all record this explicitly, with the coupling to `REMAINING-WORK.md` row 4b and the spike RECONCILE (`orchestra-fix-latest-spike-and-plan.md` L145-146, spot-verified — the RECONCILE block explicitly states "that phase must also revisit `version_registry` keying").
- [x] CHK026 Is the assumption that a distinct `session_version::vlatest` needs no codegen namespace (runtime-XML-only, like the five existing such members) stated and justified? [Assumption, research §D-5] — PASS: research.md D-5 "Enum-census gate check" paragraph states and justifies this (no exhaustive `session_version` census found; codegen goldens keyed to the 4 codegen-target versions only); spot-verified `tools/codegen/fixpp-codegen/ir.cpp:212-227` `kCodegenVersions[]` contains exactly `{v42, v44, v50sp2, vt11}` — `vlatest` correctly absent — and `emit_builders.cpp:646` gates emission to `ir.ns == "v44"` only, confirming no codegen namespace is triggered.

## Ambiguities & Conflicts

- [x] CHK027 Is the Article VI status honestly represented (CONDITIONAL, not an unbacked PASS) with the slug/entry work tracked, avoiding a false-green? [Conflict, plan Constitution Check §VI, tasks §T027] — PASS: plan.md Constitution Check VI row explicitly states "CONDITIONAL (not an unbacked PASS)" with the before-land obligation named; tasks T027 tracks it as a mandatory close-out precondition; constitution Article VI §4/§5 spot-verified (bidirectional traceability + Normative References requirements) as the actual rule being satisfied conditionally.
- [x] CHK028 Are the scope boundaries unambiguous — typed 181-class codegen, live wire validation, round-trip, ApplExtID, session negotiation, richer typing, legacy regen all explicitly out of scope? [Ambiguity, Spec §Non-Goals] — PASS: Non-Goals section enumerates all seven boundaries explicitly (typed codegen, live wire validation, round-trip, ApplExtID(1156)=303, session negotiation, richer semantic modeling, legacy dict regen) plus the QuickFIX-license README carve-out. Cross-checked against constitution Article XVIII §2 (v1.2 roadmap slot "FIX Latest application messages") — consistent as long as T003's constitution-amendment edit to Article I §1 line 42 uses the **annotate** option ("read/dictionary tier delivered in v1.0; typed/wire tiers remain post-1.0") rather than bare removal, so the v1.2 roadmap slot isn't orphaned; flagged as a T003-execution note below, not a spec defect (the requirement itself is unambiguous).

## Notes

- Auto-generated `/specify` checklist `requirements.md` is closed at pipeline steps 1–2 and is exempt from the step-9 audit; this `reader.md` is the domain checklist the step-9 checklist-audit walks.

## Audit Result

**Audited**: 2026-07-13 (pipeline.md step 9 — checklist audit, subagent execution). Domain checklist: `specs/074-orchestra-native-reader/checklists/reader.md` (28 items). Design authority (no `inherits_design`): spike-doc Deliverable #6 (`research/G19-fix-fpml-iso20022/remaining-work/orchestra-fix-latest-spike-and-plan.md` + its 2026-07-13 RECONCILE block) + `.specify/memory/constitution.md`.

| Disposition | Count |
|---|---|
| PASS | 27 |
| SPEC-FIXED | 1 |
| DD-DECIDED | 0 |
| WAIVED | 0 |
| **Total** | 28 |

### SPEC-FIXED items

- **CHK012** — Cross-artifact sha1-integrity-pin drift: `data-model.md` §C ("Vendored source" row) and `quickstart.md` (Prerequisites bullet) carried stale pre-reconcile language ("sha1 provisional/unverified... do NOT assert against the pre-filled constant") that directly contradicted the reconciled `research.md` D-6 / `tasks.md` T001 position (fetch, compute, **assert** equals `26f60db1c1f52d169d3b6825ac68800abf487fde`, STOP on mismatch — spike-doc line 36 confirms this is the OFFICIAL file's grade-1 recorded sha1, not the relabelled one). Edited both `data-model.md:49` (Vendored-source table row) and `quickstart.md` (Prerequisites bullet, 2nd paragraph) to match the reconciled assert-and-stop position. `quickstart.md` was additionally internally self-contradictory (its own Provenance-check section already asserted `== 26f60db1…`); the fix aligns Prerequisites to that section rather than the reverse.

### DD-DECIDED items

None. (CHK007/CHK013's FR-010-mechanism deferral is a legitimate spec-level design choice concretized in `tasks.md` T019/T020, not a design-doc-anchor-settled item — dispositioned PASS, not DD-DECIDED.)

### WAIVED items

None. Zero Completeness/Clarity/Consistency items were waived (none needed to be — CHK012, the one real defect in that bucket, was SPEC-FIXED per the rule that such gaps can never be WAIVED).

### Anchors spot-verified (grade-1, this session)

| Anchor | Resolves at |
|---|---|
| Constitution Article I §1 (incl. line 42 "FIX Latest ... post-1.0 milestones") | `.specify/memory/constitution.md:36-42` |
| Constitution Article VI §4/§5 (bidirectional traceability, Normative References) | `.specify/memory/constitution.md:104-105` |
| Constitution Article VII §7/§8 (fuzzing, grouped-bucket testing) | `.specify/memory/constitution.md:118-119` |
| Constitution Article XVII §8 (verify gate) | `.specify/memory/constitution.md:300-310` |
| Constitution Article XVIII §2 (v1.2 roadmap: FIX Latest application messages) | `.specify/memory/constitution.md:316` |
| Constitution Article XX (amendments) | `.specify/memory/constitution.md:341-350` |
| Appendix A (mandatory-trigger table incl. "Codegen layout") | `.specify/memory/constitution.md:355-365` |
| Spike-doc sha1 pin `26f60db1c1f52d169d3b6825ac68800abf487fde` (OFFICIAL file, not relabelled) | `remaining-work/orchestra-fix-latest-spike-and-plan.md:36` |
| Spike-doc RECONCILE block (ApplVerID / ApplExtID / re-keying coupling) | `remaining-work/orchestra-fix-latest-spike-and-plan.md:136-146` |
| `dictionary.hpp:188`/`:192` (`friend class XmlLoader;` / private handle-ctor) | `include/fixpp/dict/dictionary.hpp` |
| `version_profile.hpp:32-43` (session_version enum) / `:49-59` (application_version enum) / `:151-174` (render_appl_ver_id) | `include/fixpp/dict/version_profile.hpp` |
| `version_registry.cpp:60-73` (ctor) / `:71-72` ("Last writer wins" / `entries_[idx] = d;`) | `src/dictionary/version_registry.cpp` |
| `version_registry.hpp:64` (`kTableSize = 9`) | `include/fixpp/dict/version_registry.hpp` |
| `error.hpp:16-26` (two-pattern header note) / `:67-85` (`group_delimiter_collision_error` precedent) | `include/fixpp/dict/error.hpp` |
| `xml_loader.cpp:284-285` (non-`"fix"`-root check), `:346-350` (fail-closed datatype pattern), `:611-616` (`parse_document`), `:912-935` (`assert(mr)` + `trap_throw_or_throw`) | `src/dictionary/xml_loader.cpp` |
| `ir.cpp:249-251` (`XmlLoader loader`, not loader-polymorphic), `:265-270` (throw on unmapped session), `:212-227` (`kCodegenVersions[]` = v42/v44/v50sp2/vt11, no vlatest) | `tools/codegen/fixpp-codegen/ir.cpp` |
| `emit_builders.cpp:646` (v44-only emission gate) | `tools/codegen/fixpp-codegen/emit_builders.cpp` |
| `emit_dispatch.cpp:57-65` (kAppVersions — future vlatest touch site, correctly absent today) | `tools/codegen/fixpp-codegen/emit_dispatch.cpp` |
| `field_ref.hpp:29-64` (`field_data_type` enum) / `group_ref.hpp` (`GroupRef` shape) / `table_view.hpp:83-89` (`kMaxGroupContextDepth = 16`) | `include/fixpp/dict/` |
| `dictionary.cpp:209` (`which_session_version`), `:296` (`as_table_view`) | `src/dictionary/dictionary.cpp` |
| `conanfile.py:66` (`pugixml/1.15`) | `conanfile.py` |
| coverage-index.md `D-011` row ("FIX Orchestra... machine-readable format") | `spec/coverage-index.md:701` |
| coverage-index.md `A-035..A-065` MsgType rows (FIX Latest section) | `spec/coverage-index.md:576-614` |
| coverage-index.md `FIX-Latest` DocAbbrev (registered) | `spec/coverage-index.md:19` |
| REMAINING-WORK.md row 4b ("FIX Latest via native Orchestra reader") | `REMAINING-WORK.md:23` |

All anchors resolved exactly as cited, including the spec.md Normative-References/Overview cites into `coverage-index.md`/`REMAINING-WORK.md` (initially asserted, then opened and confirmed on advisor prompt) — zero dangling refs found.

### Realizability sub-check

Value-typed entities checked: `dict::OrchestraLoader` (member-less stateless facade — no incomplete-type risk), `dict::orchestra_parse_error` (derives from `xml_parse_error`, a complete type in the same header via `using`-inherited ctor), `Dictionary` (pre-existing, unchanged shape). **No new value type introduces a forward-declared owned dependency** (no `unique_ptr<Dep>`/by-value `Dep`/`Dep`-base pattern with `Dep` only forward-declared) — the 007/D-15 latent-completeness pattern does not apply here. Verdict: **clean**, no SPEC-FIXED needed on realizability grounds.

### Re-run `/speckit-analyze`?

**YES.** One `SPEC-FIXED` disposition landed (CHK012 — edits to `data-model.md` and `quickstart.md`). Per pipeline.md, any spec-artifact edit invalidates the prior `/speckit-analyze` drift check; re-run `/speckit-analyze` before `/speckit-implement` (step 10).

### Verdict

**GREEN** — zero un-dispositioned `[ ]` boxes remain; all cited anchors (constitution, spike-doc, source-code grade-1 refs) resolve in the signed-off/live revisions; zero Completeness/Clarity/Consistency items closed as WAIVED. Pipeline.md step 9 satisfied; step 10 (`/speckit-implement`) may proceed **after** the mandated `/speckit-analyze` re-run.
