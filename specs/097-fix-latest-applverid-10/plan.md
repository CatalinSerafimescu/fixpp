# Implementation Plan: FIX Latest is ApplVerID 10 — its own application version, both directions

**Branch**: `097-fix-latest-applverid-10` | **Date**: 2026-10-29 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/097-fix-latest-applverid-10/spec.md`

## Gate A

- Round 1 applied 2026-10-29: Codex P1=7 P2=5 P3=0; Opus post-judging P1=3 P2=8 P3=3; rewrite addresses root causes RC-1..RC-4 (+ per-finding fixes). Owner ruling RC-1: no new rule. Reviews: research/reviews/codex_097-fix-latest-applverid-10_gate_a_review.md, research/reviews/opus_097-fix-latest-applverid-10_gate_a_adversarial_review.md.
- Round 2 applied 2026-10-29: Codex P1=1 P2=5 P3=0; Opus post-judging P1=1 P2=1 P3=6; rewrite addresses root causes RC-A..RC-C (+ per-finding fixes). Reviews: research/reviews/codex_097-fix-latest-applverid-10_gate_a_2_review.md, research/reviews/opus_097-fix-latest-applverid-10_gate_a_2_adversarial_review.md.
  - **Owner attention**: RC-A's canonical residue sentence (data-model §E.0) is constitution text, which
    the owner ratifies. It is shown to the owner verbatim with this rewrite. It changes no ruling.
- Round 3 (exhausted) 2026-10-29: Codex P1=1 P2=1 P3=0; Opus post-judging P1=0 P2=1 P3=2. Owner ruling 2026-10-29: hand-edit + one targeted round. Hand-edit applied: G097-019, G097-020, O097-201. Reviews: research/reviews/codex_097-fix-latest-applverid-10_gate_a_3_review.md, research/reviews/opus_097-fix-latest-applverid-10_gate_a_3_adversarial_review.md.
- Targeted round 2026-10-29 (owner ruling: hand-edit + one targeted round): Codex P1=0 P2=1 P3=0; Opus post-judging P1=0 P2=1 P3=2. Owner ruling 2026-10-29: apply the specified fixes and accept Gate A without a further round. Applied: G097-021 (W-9 census + M-9f), O097-301 (W-9 call paths), O097-302 (2c note as a condition). Reviews: research/reviews/codex_097-fix-latest-applverid-10_gate_a_4_review.md, research/reviews/opus_097-fix-latest-applverid-10_gate_a_4_adversarial_review.md. Gate A: converged by owner ruling.

### Round 1 — disagreements

Codex fixes that were not applied, following the Opus judgement:

- **G097-003, the counter-proposal's scope.** Codex asked for a new accept-and-ignore mechanism for
  Logon 1407 and header 1156 across every FIXT application dictionary. Not applied, for two reasons.
  The owner ruled "no new rule" (spec Clarifications, Gate A round 1). Validation by the session's own
  dictionary is the shipped rule (L-041-2), and selecting the validation dictionary is Out of Scope.
  QuickFIX/Go with its shipped FIXT11.xml rejects 1407 too, so a dictionary-agnostic accept would not
  be QuickFIX-faithful. What was applied: FR-008 restated, W-7 given named session dictionaries and
  control arms, B-097-3's disclosure, and research R-6 corrected.
- **G097-001, the re-plan.** Codex asked for `/plan` to be re-run in a clean context after
  clarification. Not applied. The one `/clarify` answer confirmed the existing engine-wide admission
  rule and changed no design decision. Its two consequences (W-6(d)'s session-dictionary arm and
  B-097-1's admission sentence) are already in the artifacts, so a re-plan would reproduce this plan.
  The deviation is instead recorded honestly in the Constitution Check, with a Complexity Tracking
  entry, a regenerated checklist, and an explicit plan↔spec drift check in `/speckit-analyze`
  (`[const §XVI.4]`).
- **G097-002, the C-ABI sub-point.** Codex asked to amend 2c's C-ABI constant set (2c's C-ABI
  commitment that lists application-version constants). Not applied. That constant set was never
  implemented: `git grep -n "APPL_VER\|application_version" include/fix src/capi bindings` finds no
  version constant, and the C ABI carries only the raw 1137 string. 097 changes nothing there. The
  §4.3 enum and wire-table annotation was applied (FR-012(f)).
- **G097-009, a W-row per FR-011/FR-012 leg.** Downgraded to P3 by Opus. FR-010 enumerates the behaviour
  FRs only, so one compact records witness, W-11, carries the positive-presence checks of quickstart
  §4 instead of one row per leg. The full-suite witness W-10 is not renumbered.
- **G097-010, the removal of history.** Codex suggested deleting the withdrawn-Strict discussion from
  `spec.md` and `research.md`. Not applied. Those passages are labelled history of a withdrawn ruling.
  Only the checklist's active "strict" / "open markers" statements were removed (regenerated).

### Round 2 — disagreements

Codex fixes that were not applied, or applied in a different shape, following the Opus judgement:

- **G097-016, the `ir.cpp` / `ir.hpp` parts.** Codex asked to rewrite `ir.cpp`'s "four codegen-target
  versions" banner, its unmapped-version diagnostic and `VersionIR::ns`'s namespace list in `ir.hpp`.
  Not applied. That staleness dates from 076, which added the `vlatest` row without updating them, and
  the bundle's policy leaves pre-097 "four codegen versions" statements alone (FR-012(f); data-model
  §C; "clean up only your own mess"). 097 edits only the `vlatest` row's own comment in `ir.cpp`; the
  nearby pre-existing staleness is mentioned in the PR body as pre-existing. Applied: every
  `emit_dispatch.cpp` comment 097 makes false (data-model §B), and Codex's request to add these
  strings to W-11 for that file, as a reversed-polarity quickstart §4.2 row (the §4.1 pattern does not
  match them). Codex's severity P2 was downgraded to P3 by Opus.
- **G097-017, the BREAKING label for the layout change.** Codex asked to mark the `version_registry`
  size change BREAKING. Not applied as BREAKING: `[const §X.7]` and `[const §IX.5]` govern the C ABI
  only, fixpp is pre-release, the C handles are opaque, no size pin exists, and no B&L precedent labels
  a C++ layout change BREAKING. Applied: B-097-2 discloses that `dict::version_registry` and `Engine`
  grow by one slot and that C++ consumers rebuild (no C++ ABI promise pre-release). Downgraded to P3.
- **G097-014, the alternative M-7c.** Codex offered either a serviceability bypass or a mutant that
  maps the composite onto the registered FIX50SP2 slot. The bypass (Opus's fix) was taken; one mutant
  suffices to show (c) RED, so the second is not added.
- **G097-015, narrowing AS-3 instead.** Codex offered narrowing US1 AS-3 to the captured families as
  an alternative. Not taken: W-8 now drives every family AS-3 names (Opus's preferred fix), with one
  production mutant per tag (M-8a..c), which also covers Codex's every-tag request.
- **G097-018, severity.** Applied in full (executable exclusions, zero unannotated hits, the
  `CLAUDE.md` line moved into implementation); Opus downgraded it P2→P3 because the prose exclusions
  were deterministic and the base run fails loud.

### Round 3 — disagreements

Codex fixes that were not applied, or applied in a different shape, following the Opus judgement
(applied by the owner-authorized hand-edit):

- **G097-019, filtering the vlatest arm by message category.** Codex offered, as an alternative, to
  generate the vlatest arm only from messages whose `is_application` is true, which would make
  XMLnonFIX (35=n) get `dict_reify_unknown_msg_type` under FIX Latest. Rejected. It would make vlatest
  the only arm that filters: the shipped v44 arm reifies 35=n as `application` / `v44` today. It would
  depart from fixpp's own session classifier (catalogue A-034: 35=n reaches `Application::fromApp`)
  and from QuickFIX C++ and QuickFIX/J, whose admin set is `0A12345`, against the standing owner rule
  to keep QuickFIX's observable behaviour. It would also add a version-specific rule. Applied instead:
  Codex's first alternative, the shipped partition stated once in FR-009(a) and restated by US3 AS-1,
  B-097-4 and W-9(a3), with the explicit 35=n sentence, W-9(a4) and mutant M-9e, which is that filter
  and turns (a4) RED. Codex's severity P1 was downgraded to P2 by Opus (a wording and witness defect,
  not a design defect).
- **G097-020, the §6.3 registry sentence, §9 seam 15b and a closed-list sweep.** Codex asked for notes
  there too and for an executable sweep of v42/v44/v50sp2-only application-dispatch claims. Not
  applied. The registry sentence ("the four codegen-version dictionaries") is about the registry, not
  the switch, and was never accurate; seam 15b is test-seam prose that was stale before 097. 2c lists
  `v42, v44, v50sp2` many times for typed-class scope, which stays true, so a sweep would produce only
  noise. Applied: notes at §4.8's application-dispatch sketch, §6.3 step 3 and §6.6 item 8, with the
  rule keyed on the claim (FR-012(f), data-model §C), and quickstart §4.2's count. Codex's C-ABI
  remark is already recorded under Round 1 (G097-002). Severity P2 downgraded to P3 by Opus.

## Summary

FIX Latest gets the wire identity the FIX documents give it, ApplVerID `"10"`, in place of 074's
mapping onto FIX50SP2:
- a new `application_version::vlatest` enumerator, parsed from and rendered to `"10"`, and accepted
  as a config token (research R-1, R-2, R-7);
- its own `version_registry` slot, which retires the FR-010 abort, L-074-1 and L-080-1. Lookup is a
  one-way fallback per the owner's second Q1 ruling: FIX Latest answers FIX50SP2 when that dictionary is
  absent, never the reverse (R-3). L-097-1 records that only direct C++ can register both;
- a FIX Latest arm in the generated reify dispatch, which returns handles labelled `vlatest`, with the
  vlatest typed classes' static `which()` reporting FIX Latest (R-4, R-5). The arm is not emitted when
  the FIX Latest codegen tier is off;
- no 1407/1156-specific rule (owner, Gate A round 1). Neither tag affects resolution, serviceability or
  state, and a validated frame's outcome stays with the session's own dictionary (L-041-2). Nothing new
  is originated (R-6).

The session code is unchanged. Its three version paths are `open()` (registry lookup of the typed
default), the acceptor (parse, then lookup) and the initiator (parse and record). They change answer
only through the parse (R-2) and the registry (R-3). Records: a MINOR constitution amendment, `.specify/2c-codegen.md` §4.3/§4.8/§6.3/§6.6 notes, B&L,
the catalogue and the coverage index (data-model §C–§F).

FIX Latest is negotiated when the DefaultApplVerID(1137) a session reads from its peer is `10` (the
acceptor reads the peer's Logon, the initiator the peer's Logon reply; data-model §A). A session's own
`default_appl_ver_id` is what it sends. A FIX Latest dictionary session configured `"9"` sends
`1137=9`; with a peer that sends `9` too it negotiates FIX50SP2 and reifies under the FIX50SP2 arm.
That is the fallback, and B-097-4 discloses it.

## Technical Context

**Language/Version**: C++23 (library, codegen tool, tests); Python 3 and Bash for the records checks
**Primary Dependencies**: GoogleTest, pugixml (both already linked by the touched binaries), `fixpp_dictionary`,
`fixpp_session`, `fixpp_config`, `fixpp-codegen`
**Storage**: N/A. Inputs are the vendored `dictionaries/FIX50SP2.xml`, `dictionaries/FIXT11.xml` and
`dictionaries/orchestra/OrchestraFIXLatest.xml`
**Testing**: existing gtest binaries for dictionary, config, session and codegen, each gaining ctest label `097`
**Target Platform**: every CI platform. The FIX Latest dispatch arm exists only under
`FIXPP_CODEGEN_FIX_LATEST` (default ON). Under OFF the emitter writes no `vlatest` case, and the value
falls to the outer fail-loud `default` (R-5). No CI lane builds OFF, so verify runs one OFF
configure+build (W-9(c))
**Project Type**: library — production change in `dict`, `config` and `tools/codegen`, plus tests and records
**Performance Goals**: no hot path changes (the Logon-time parse gains one length check). No bench
row is expected to move; verify runs the paired gate as usual
**Constraints**: the orchestrator writes no library code, so `phase-implementer` writes every source,
test and CMake line. Additive public enum. No C-ABI change. No `src/session/` change. No validator
(`include/fixpp/wire`, `src/dictionary` validation) change
**Scale/Scope**: the files listed under "Files changed"

## Constitution Check

| Clause | Status |
|---|---|
| `[const §I.1]` | Amended (FR-012(a); data-model §E). Without the amendment, Article I §1 would contradict the shipped mapping. |
| `[const §I.3]` | No `backlog` row closes. L-074-1 and L-080-1 retire. |
| `[const §VI.4]`, `§VI.5` | Coverage-index rows updated (data-model §F). spec Normative References lists the Spec refs in `[DocAbbrev §X.Y.Z] Title` form, using the coverage index's spellings. §11.2 has no coverage-index entry and is listed as a supporting citation. |
| `[const §XVI.3]` | **Deviated.** `/speckit-clarify` is mandatory before `/speckit-plan` for a wire/codegen feature. Here it ran after: `/speckit-plan` at `cebca1eb` (reconciled with the second Q1 ruling at `afb8f92a`), then `/speckit-clarify` at `886e587f`. The owner was informed on 2026-10-29. Remedy: the one clarify answer was carried into the plan artifacts in the same commit (W-6(d), B-097-1); the checklist was regenerated at Gate A round 1; `/speckit-analyze` checks plan↔spec drift explicitly (next row). No re-plan, for the reason given under Gate A, Round 1 — disagreements. See Complexity Tracking. |
| `[const §XVI.4]` | `/speckit-analyze` runs after `/speckit-tasks` and MUST include an explicit plan↔spec drift pass over every Clarifications answer: Q1 (both rulings), Q2, the `/speckit-clarify` answer and the Gate A round-1 answer. That pass is the gate the order rule protects. |
| `[const §XVI.6]` | Mutation proofs M-1..M-9h (and W-12's re-runs of M-1, M-1n and M-2) run in the parallel worktree `fixpp-parallel`, detached, never in the PR's worktree (contracts preamble; owner, T002). |
| `[const §VII.3]`/`§VII.4` | TDD order: every witness shown RED (unfixed tree or named mutant) before GREEN (contracts). |
| `[const §VII.7]` | Applies (analyze D1). T018's two-character arm in `resolve_application_version` parses a peer-supplied 1137/1128 value, and no existing harness reaches that function (research R-11). A new libFuzzer harness, `tests/fuzz/fuzz_appl_ver_id.cpp`, is added with its seed corpus (contracts W-12). Precedent: 033's plan carries a `VII.7` row and its tasks T036; 033 extended an existing harness's corpus, holding that the harness reached its new scanner arms (a claim research R-11 records as an unverified lead). Here no harness reaches the function, so the harness is new. CI replays the seeds on every PR (`fuzz_replay_appl_ver_id`, `linux-clang-asan` lane). No CI job runs a timed campaign, so the ten-minute run is `/speckit-verify` Step 7 with `--fuzz-duration=600` (tasks T073). That gap between §VII.7's wording and CI predates 097 and is reported, not fixed. |
| `[const §VII.8]` | No new gtest executable. Witnesses join existing binaries; selected by `ctest -L 097`. The W-12 fuzz harness is a libFuzzer target outside §VII.8's grouped-gtest scope, built only under `FIXPP_BUILD_FUZZ` (`linux-clang-asan`), so the `linux-clang-debug` ctest total is unchanged. |
| `[const §IX.1]` | Touched modules ≥ 95/85 or a per-line assessment, at verify. |
| `[const §IX.4]` | clang-tidy/clang-format on changed files, `tools/codegen/` included. |
| `[const §X.1]`, `§X.7` | C ABI unchanged; nothing to bump. |
| `[const §XVII.1]` | Gate A runs: public C++ API change, behaviour change and amendment. |
| `[const §XVII.7]` | Local full build + ctest of `linux-clang-debug` at the final head. |
| `[const §XVIII.1]`, `§XVIII.5` | Read and unchanged, argued in the Sync Impact Report (data-model §E.4). |
| `[const §XVIII.2]` | Annotated (data-model §E.3). |
| `[const §XX.1]`–`§XX.4` | Amendment first in commit order; MINOR v4.3; PR titled in §XX.2 form; owner sign-off before RATIFIED. |

One deviation, `[const §XVI.3]`, recorded above and in Complexity Tracking. Everything else is
satisfied.

## Project Structure

### Documentation (this feature)

```text
specs/097-fix-latest-applverid-10/
├── spec.md
├── plan.md            # this file
├── research.md        # R-1..R-11
├── data-model.md      # §A identities, §B surfaces, §C superseded text, §D B&L, §E amendment, §F catalogue
├── quickstart.md      # §0..§5 validation
├── contracts/
│   └── witnesses.md   # W-1..W-12, mutants M-1..M-9h
└── checklists/
    └── requirements.md
```

### Files changed

- `include/fixpp/dict/version_profile.hpp`, `src/dictionary/version_profile.cpp`
- `include/fixpp/dict/version_registry.hpp`, `src/dictionary/version_registry.cpp`
- `src/config/scalar_mappers.cpp`
- `tools/codegen/fixpp-codegen/emit_dispatch.cpp`, `ir.cpp`, `gen_util.hpp`
- `specs/076-fix-latest-typed-codegen/contracts/golden/vlatest_Messages.golden.hpp` (regenerated)
- tests: `tests/dictionary/{version_profile_test,orchestra_loader_test,reify_dispatch_test}.cpp`,
  `tests/config/test_load_happy_path.cpp` and `tests/config/test_load_negative_battery.cpp` (one W-3
  case each) with the new fixtures `tests/config/fixtures/pos_fixt_appl_ver_id_10.toml` and
  `tests/config/fixtures/neg_fixt_unknown_appl_ver_id.toml`, and the comment in
  `pos_fixt_appl_ver_ids.toml` and `test_load_happy_path.cpp` that restates the `"2".."9"` range
  (FR-012(c)),
  `tests/session/test_fixt_logon_establishment.cpp` (W-6..W-8, with named session dictionaries for W-7),
  `tests/codegen/{vlatest_dispatch_exclusion_test,vlatest_compile_smoke_test}.cpp`, the W-9(c)
  OFF-only case and its CMake guard, the CMake label lines, and in `tests/codegen/CMakeLists.txt`
  `FIXPP_DICT_DATA_DIR` added to `codegen_vlatest_tests`'s `target_compile_definitions` (the W-9(a3)
  census; Gate A round 4, G097-021)
- fuzz (contracts W-12, `[const §VII.7]`): `tests/fuzz/fuzz_appl_ver_id.cpp` (new), its target in
  `tests/fuzz/CMakeLists.txt`, and its seeds under `tests/fuzz/corpus/appl_ver_id/` (new)
- records: `.specify/constitution.md`, `.specify/2c-codegen.md` (§4.3/§4.8/§6.3/§6.6 notes),
  `spec/behaviors-and-limitations{,-closed}.md`, `spec/feature-catalogue.md`, `spec/coverage-index.md`,
  `brain/components/dictionary.md`, the library `CLAUDE.md` (the "Also open, no issue filed" line is
  deleted in phase 6, so W-11 sees it gone at verify; Gate A round 2, G097-018)

## Phases

1. **Amendment** (the first commit): data-model §E text and the Sync Impact Report with PENDING
   sign-off slots.
2. **Unmodified-code witnesses first** (quickstart §1.1). Before any production edit, write the witnesses
   that compile against unmodified code:
   - W-1's string half;
   - W-3's text arm;
   - W-4 construction-only (the death test, as it stands);
   - W-5's FIX-Latest-alone arm;
   - W-6 (a)/(b)/(c), with `validate_inbound_messages` at its default (off);
   - W-7 (a)/(b)/(c)/(d)/(e)/(f) and every control arm, with `validate_inbound_messages` ON and the
     session dictionary each arm names (contracts W-7);
   - W-8's arms that name no new enumerator: the `"9"` initiator (ii) and the W-6(b)/(c) frames, with
     every frame family present (contracts W-8).

   Build and run them in `build/linux-clang-debug`; that run is the RED record and the
   characterization record. **Stop rule, in three parts:**
   - If a **control arm** (the frame without 1407/1156) departs from its expected outcome on this tree
     (contracts W-7; on `origin/main`, (b0) and (c0) expect the 1137 Reject, (a0) and (e0)
     establishment, and (d0) and (f0) delivery to `fromApp` on an established session), the fixture is wrong: the dictionary, the frame or the session setup. Fix the
     fixture, which is test setup and not FR-008, and re-run. Proceed only once every control matches.
   - If a 1407/1156 arm's outcome differs from its matching control's (for example rejected where the
     control is established), under a session dictionary that declares the tag (`FIXT11.xml` or the Orchestra file), RC-1's premise is false: FR-008 would need code, and
     "no `src/session/` change, no validator change" is wrong. Stop and decide, and do not adjust the
     test.
   - W-7(e) and W-7(f) (the dictionary-decided rejects of 1407 and of header 1156) are
     characterization pins. If an observed Reject shape departs from contracts' text while its control
     passes, record the observed shape and correct the text. No production code changes. If instead
     (e) is established or (f) is delivered with no Reject at all while its control passes, the
     dictionary-decided reject that B-097-3 and research R-6's table describe does not exist on this
     tree: stop and decide, as for the second part, and do not adjust the test.
3. **Core**: enumerator, parse, render, registry with the one-way fallback, config. W-1..W-5 green.
   Mutants M-1..M-5c. The W-12 fuzz harness and its seeds land with the parse change; its corpus
   replay passes in `build/linux-clang-asan`, and M-1, M-1n and M-2 are re-run against it in the
   scratch worktree's ASan tree.
4. **Session witnesses**: W-6..W-8 (no session source change). Mutants M-1n, M-7a..e, M-8a..c, and
   M-2 against W-8's `1137=10` assertion.
5. **Codegen**: dispatch row, the emitter skipping an arm whose IR is absent, `ir.cpp`, `gen_util.hpp`,
   golden regenerated (quickstart §3), W-9. Mutants M-9a..h, and M-1n re-run against W-9's in-frame
   1128 case.
6. **Comments and records**: data-model §C–§F, including the canonical residue sentence (§E.0) at
   every locus, the in-place 097 supersession comments (§F), and the deletion of the library
   `CLAUDE.md`'s "Also open, no issue filed: ApplExtID(1156)=303 …" line; quickstart §4 checks, the
   superseded-phrase sweep and the positive-presence checks with their seeded negatives (W-11). A
   rewritten comment states the rule and the derivation, never a count or an enumerated range.
   `kTableSize` is derived from the enum, `static_cast<std::size_t>(application_version::vlatest) + 1`,
   with no literal (data-model §B; no `static_assert`, which would only restate the definition). The `"2".."9"` tables in
   `version_profile.hpp` and `scalar_mappers.cpp`, and the two `tests/config` comments FR-012(c) names, point at the code instead of re-listing values
   (CLAUDE.md: a comment may record a procedure, not a result). The unknown-token diagnostic in
   `scalar_mappers.cpp` is not a comment: it is a runtime string FR-003 requires to name the accepted
   range, so it keeps its `"2".."10"` text. It is pinned by W-3's text arm, so it cannot drift from the
   mapper silently, and the comment-claims check is blind to it, so T065 reads it by eye. Check with
   `.claude/scripts/check-comment-claims.py` (quickstart §4).
7. **Final head**: one full configure + build + ctest (`-j8`). Then one `FIXPP_CODEGEN_FIX_LATEST=OFF`
   configure+build with `FIXPP_WERROR=ON`, which runs W-9(c), its build tree on `/mnt/wsl/fixppbuild`.
   Then `/speckit-verify` with `--fuzz-duration=600` (W-12's ten-minute fuzz run), then Gate B.

## Risks

- **A generated-code blast radius larger than the golden.** Other checked-in generated output may embed
  the vlatest classes' version. Mitigation: after phase 5, `git status` must list only the files named
  above. Any other changed generated file stops the phase for a decision.
- **The fallback hides a mis-registration.** A C++ integrator who meant to register FIX50SP2 but did not
  gets FIX Latest answering `1137=9`. This is today's behaviour, kept by ruling. Inbound validation uses
  the session's own dictionary in either case.
- **`-Wswitch` in a consumer** sees the new enumerator. Disclosed in B-097-2.
- **A witness that cannot fail.** Every row of contracts carries its RED arm, and quickstart §1.2 gates
  each mutant row on build rc 0. Every W-7 arm has a control arm that must pass first, so a fixture that
  rejects everything cannot pass as an FR-008 result. Each characterization pin's mutant is derived
  from the code path that produces the pinned outcome (Gate A round 2, RC-B): M-7c bypasses the
  registry lookup that decides W-7(c), and M-8a..c make the admin builders originate the tags W-8
  counts. W-8 asserts that every frame family was captured before it reads a zero count.
- **A validated session whose dictionary does not declare 1407/1156 rejects them.** This is by ruling
  (no new rule), and B-097-3 discloses it. W-7(e) (1407) and W-7(f) (header 1156) pin it, so a later
  validator change cannot drift it silently.
- **A `"9"`-configured FIX Latest session with a peer that also sends `9` keeps the FIX50SP2 label.** This is by ruling (the one-way
  fallback). B-097-4 and the constitution/catalogue text state the condition (RC-4).
- **The OFF codegen tier has no CI lane.** W-9(c) runs only in verify's local OFF configure+build.
  Disclosed, not closed.

## Complexity Tracking

| Item | Why | Simpler alternative rejected because |
|---|---|---|
| `[const §XVI.3]` deviation: `/speckit-clarify` ran after `/speckit-plan` | It was skipped before `/plan` and run before Gate A, once found (spec Clarifications, `/speckit-clarify` block) | A clean-context re-plan would reproduce this plan: the clarify answer changed no design decision, and its two consequences are already in the artifacts. The order rule's purpose, catching plan↔spec drift, is served by the regenerated checklist and `/speckit-analyze`'s explicit drift pass (`[const §XVI.4]` row). |
