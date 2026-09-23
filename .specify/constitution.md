<!--
Sync Impact Report — v2.0 → v3.0 (2026-09-23) — PROPOSED (Codex Gate A, then user sign-off, pending)
  Bump: MAJOR, per Article XX §4, with a CHANGELOG.md entry. Under v2.0 the /speckit-implement skill
    (step 5a) let the orchestrator implement task bodies itself when the implementer escalated or when
    a phase was one trivial task. v3.0 removes both exceptions, so a PR that used either conformed under
    v2.0 and does not under v3.0 — a tightening by the reading v1.0 established. The first draft of this
    amendment classified itself MINOR and was corrected at Gate A round 1 (P1).
  Modified: Article XVI §6 — a clean-context implementer sub-agent executes and the orchestrator (the
    main session) does not implement. The clause now defines "implement" (production code, tests,
    scripts, build/CI/configuration files, committed generated files and the regeneration producing
    them, and resolving merge conflicts in any of those; classified by file type, not directory) and
    lists what the orchestrator MAY do, including governance texts and transient build/test/index
    outputs. The model that runs the
    implementer is configuration, not a constitutional term.
  Modified: Article XVI §7 — the orchestrator triages /simplify findings and the implementer sub-agent
    applies the accepted ones; the step citation is corrected to pipeline steps 11 and 12.
  Modified: Article XVII §4 — no Claude agent starts Codex on its own initiative; a user's /gate-a or
    /gate-b invocation authorizes that gate's bounded Codex calls, and the §XVI.8 fallback runs only
    once the user confirms it. The model list ("Neither Sonnet nor Opus") is removed.
  Modified: Article XVII §5 — accepted findings are applied by the active fixer, the non-orchestrator
    actor the applicable procedure assigns (the implementer sub-agent, Gate A's rewrite agent, or Codex
    where §XVI.8 or the Gate B fixer hand-off applies), never by the orchestrator.
  Modified: Article XX §5 — the illustrative hand-off rule names "the implementer" instead of Sonnet.
  Rationale: the owner moved the implementer and Gate B fixer from Sonnet to the Opus alias and ruled
    that the orchestrator never implements (2026-09-23). The recorded case: PR #258's Gate B ran 9
    rounds, rounds 3-8 "Codex-review + orchestrator-fix with no Step B triage at all" (research
    repository decisions/speckit/pr258-python-fold-gateb.md, "Convergence story"). Naming a model in
    the constitution also made every model change an amendment; naming the role does not.
  Affected catalogue rows: none.
  Affected feature specs: none (merged specs/<id>/ bundles that name the old agent are historical
    records and stay as written).
  Companion governance docs changed: .specify/pipeline.md (step 11 wording, step 14 diagram,
    disposition [L]), .specify/close-out.md §2 row 15, .specify/codex-review.md,
    .claude/skills/speckit-implement/SKILL.md (step 5a: no orchestrator exception; between-phase
    comment-claim lint), CONTRIBUTING.md, CHANGELOG.md. Parent-repository counterparts (agent
    definition, /gate-b, the edit guard) are outside this repository.
  Review and sign-off: pending — Codex Gate A on this branch (round 1: NOT CONVERGED, 1 P1 / 4 P2 /
    3 P3; round 2: NOT CONVERGED, 0 P1 / 3 P2 / 1 P3 — A-2, A-4 and A-7 were only partly closed by
    the round-1 revision), then the owner's sign-off. PR: not yet opened.
-->
<!--
Sync Impact Report — v1.0 → v2.0 (2026-09-15) — RATIFIED
  Article XX §2 sequence observed in order: Codex Gate A converged (round 4: 0 P1 / 0 P2, one P3
    fixed; rounds 1-3 raised 8, 3 and 3 P2), THEN user sign-off 2026-09-15. Amendment PR #451.
  Bump: MAJOR, per Article XX §4, with the CHANGELOG.md entry that clause requires. Both changes
    loosen a rule, and both attach obligations that a PR mergeable under v1.0 would now fail:
    - §X.7: a pre-release breaking C-ABI change that bumps MAJOR, or that carries no BREAKING marker,
      conformed under v1.0 and does not under v2.0.
    - §XX.3: an amendment whose Sync Impact Report omits the listed contents conformed and does not.
    v1.0 set the reading: making a previously mergeable change fail is a backwards-incompatible
    tightening even when the same amendment also loosens, and v1.0's own draft MINOR was corrected to
    MAJOR at its Gate A round 1 (P1). The first draft of THIS amendment classified itself MINOR by the
    narrower reading of §4 and was corrected at Gate A round 1 (P2). §4 itself is unchanged.
    NOTE: as at v1.0, the constitution's version is independent of the library's and of the C ABI's.
    Constitution v2.0 is not C-ABI 2.0.
  Modified: Article X — adds §7. Before the first public release (the first GitHub Release of this
    repository) a breaking C-ABI change bumps MINOR, is declared BREAKING, and updates every
    in-repository consumer in the same PR. A breaking change is defined by its effect on a consumer
    written against the previous headers and documentation, and includes every C-ABI effect listed
    in .specify/api-contract.md §11. The compatibility promise starts at the first public release,
    where one release PR resets the C-ABI version to 1.0.0, rebases every error code to
    introducing_minor 0, and adds a check that the rebased sites agree (fixpp#449); from then on a
    breaking change requires a MAJOR bump.
  Rationale: the 0→1 C-ABI "GA freeze" (PR #160) was declared with no consumer, and #160 deferred the
    MINOR reset to the next breaking MAJOR. Under that rule every WIP breaking change costs a MAJOR,
    which resets MINOR and rebases the introducing_minor table (src/capi/error.cpp) each time, to
    protect nobody. #160's objection to a reset (already-published codes downgrade to UNKNOWN for an
    existing consumer) does not apply to a first release, which has no prior consumer.
    First applied by fixpp#428 (C-ABI 1.6.0, BREAKING: fixpp_msg_set_string / fixpp_entry_set_string
    refuse SOH outside a Data field; fixpp_msg_commit refuses malformed Length+Data pairs).
  Modified: Article IX §5 — the ABI check starts at §X.7's first public release: that release records
    the baseline and each later release is compared against the previous tagged ABI. Before it a
    breaking change follows §X.7 rather than bumping MAJOR.
  §X.7 also prevails, before the first public release, over freeze statements elsewhere (§X.3's
    decimal shape, the MAJOR-1 layout freezes in 2a-decimal.md, 2i-capi.md, decimal.h and version.h,
    and "1.5.0 is stable" wording), which describe the rule from that release on. A single
    precedence sentence rather than an edit per site, so a site this sweep missed is still covered.
  Known pre-existing gap, not changed here: the implemented ABI gate (.github/workflows/abi-golden.yml)
    checks the exported symbol set only, not the layout/type comparison §IX.5 describes. fixpp#450
    tracks building that comparison before the first public release.
  Modified: Article XX §3 — from v2.0 on, an amendment is recorded in its Sync Impact Report (and in
    CHANGELOG.md when §4 applies), replacing `_log.md`. No decision log of that name ever existed: the
    only file called _log.md (research/_log.md) is the Phase 1 research log, and no amendment since
    v0.1 wrote to one. First raised as 082 Gate B finding F-7 (P3, research/reviews/opus_pr261_1_triage.md).
    The listed contents apply forward only: earlier reports (v0.1 → v0.2 and v0.2 → v0.3 are one-line
    records) stay as written, because reconstructing them would be guesswork. The clause names the
    feature's spec bundle for feature-level decisions and no longer names `.specify/decisions/`, which
    is a local link to a separately tracked research repository rather than a path in this one.
  Affected catalogue rows: none.
  Affected specs and docs (changed):
    - .specify/2i-capi.md — supersession notes at §4.3's stability rule and after §4.5's version-macro
      excerpt. The excerpt itself and Appendix D (a point-in-time Gate A record) stay as written.
    - .specify/api-contract.md — the Frozen rule, §2's Stable tier, §4's C-ABI track row and ABI-check
      bullet, and §11's remedy each state §X.7's pre-release consequence; §11 gains the C-ABI effects
      §X.7 relies on (layout and constant values, calling convention and visibility, ownership,
      lifetime and reentrancy rules, a call that used to succeed and now fails).
    - .specify/architecture.md §9.2 and .specify/2a-decimal.md — the Tier 2 ABI check starts at the
      first public release.
    - include/fix/c_api/version.h comment (+ tools/capi_freeze.sha256), tools/check_capi_freeze.sh
      header, brain/components/c-api.md (body, title, description, heading), CLAUDE.md, CHANGELOG.md.
  Reviewed, no change: merged spec bundles (specs/NNN-*), CLAUDE-history.md, and
    spec/behaviors-and-limitations*.md rows, which are point-in-time records. The error-code
    stability rules in .specify/2m-pybind.md, .specify/2j-controlplane.md and spec/coverage-index.md
    say "published in a tagged C-ABI release", which §X.7 now defines, so they need no edit. The
    "previous tagged release" perf-regression bars in the 2b-2f design docs are about benchmarks,
    not the C ABI.
  Templates reviewed: plan-template.md / spec-template.md / tasks-template.md — no change.

Sync Impact Report — v0.11 → v1.0 (2026-08-21) — RATIFIED
  Article XX §2 sequence observed in order: Gate A converged (round 5, PASS, 0 P1 / 0 P2 / 0 P3),
    THEN user sign-off 2026-08-21. This header read PENDING until both had happened — an earlier
    revision asserted RATIFIED while Gate A was still running (round 2, P1), and the flip to v1.0
    is deliberately the last commit before merge rather than a claim made in advance.
  Bump: MAJOR, per Article XX §4, with the CHANGELOG.md entry that clause requires.
    This amendment is BOTH a tightening and a loosening, and the loosening does not cancel the
    tightening. TIGHTENING: five binaries move from having NO hard timing decision at all to a hard
    >+50% slowdown rejection, so changes that were previously mergeable now fail — an effective perf-budget
    tightening even though the printed "±5%" numeral is unchanged. LOOSENING: checked-in baselines
    cease to gate, and 18 of 23 manifest binaries carry no automated timing limit. Article XX §4
    classifies perf-budget tightening as backwards-incompatible; converting an unenforced obligation
    into an enforceable rejection criterion is such a tightening. (The v0.12/MINOR classification
    carried by the first draft of this amendment was WRONG and was corrected at Gate A round 1, P1.)
    NOTE: this document's version is independent of the library's release version. Constitution v1.0
    asserts nothing about project GA; it is Article XX §4's major increment from v0.11.
    NOTE: `CHANGELOG.md` did not exist when this amendment was written, though Article XX §4 has
    required an entry in it since v0.1 — a mandate on a file that was never created, the same class
    of defect as issue #209's FINDINGS.md. It is CREATED by this PR rather than the clause being
    quietly ignored.
  Modified principles:
    - Article VIII §2 (Regression budget) — REWRITTEN. The comparand changes from checked-in
      `bench/baselines/` to a PAIRED base-vs-candidate measurement on one runner, against the
      merge-base of the candidate and the PR's TARGET BRANCH (A-B-A-B, min-per-tree). Stored
      baselines are reclassified as informational and do not gate the per-PR budget. The budget is
      stated ONE-SIDED throughout ("a slowdown greater than +5%", "must not exceed +50%") to match
      the shipped comparator, which rejects only `delta > band` and passes an improvement
      (`tools/bench_compare.py`'s `run_suite`, the `delta > band` regression branch); the previous revision's two-sided "±" wording would have
      required approval for a 10% speed-up (Gate A round 2, P2). The +50% sentinel is expressed as
      a CONSTITUTIONAL CEILING on how weak CI may become — widening it needs an amendment — while
      the current threshold/paired-set/sample-count/promotion-state are delegated to the bench-gate
      decision record. The partial timing coverage is stated WITHOUT embedding its cardinality:
      naming "five of twenty-three" made the constitution stale the moment a sixth binary is
      promoted, which the non-decreasing rule actively invites (Gate A round 2, P2); the counts now
      live in the historical rationale below, and the normative text carries a binding PROMOTION
      DEADLINE instead — every binary eligible under §6a must be paired at or before the next
      release. Acceptance of a >+5% slowdown requires a recorded paired measurement AND approval by
      someone OTHER THAN THE AUTHOR, as a GitHub PR approval or an explicit user statement in the
      PR thread, with an explicit sole-maintainer fallback — the previous revision's undefined
      "maintainer approval" was self-approval with an extra step, since CODEOWNERS names only the
      author (Gate A round 2, P1). The cumulative-drift follow-up is bound to v1.0 rather than left
      permanently optional.
    - Article VIII §4 (v1.0 perf targets, latency bullet) — clarified that §2's "checked-in baselines
      do not gate" does NOT reach the v1.0 release baseline, which remains blocking. §2 governs the
      per-PR budget only.
  Added sections: Article VIII §2a (fail-closed invariants of the paired comparand: merge-base of
    candidate and PR target branch, distinct from the candidate; crashed/empty/uninformative
    measurement is a FAILURE, as is a missing measurement for any paired row PRESENT IN THE
    MERGE-BASE'S MANIFEST; the sole exemption is a row ABSENT from that manifest; paired status is
    IRREVERSIBLE).
    ★ §2a has been rewritten TWICE, and both times the defect was the same: the constitution
      asserted a rule the implementation does not have. Recorded because it is the transferable
      lesson of this amendment.
      Gate A round 2 (P1) — the first draft made EVERY missing measurement a failure, which would
        have outlawed the candidate-only-addition path PR #272 had just spent Gate B rounds 4 and 5
        building (`check_paired_not_narrowed`'s docstring "must never be an error"; `run_paired`'s base-run-manifest filter excludes those
        rows; the merge-base build step's candidate-only-addition exemption classifies them before the base build).
      Gate A round 3 (P1 ×3) — the replacement then (i) asserted a BUILDABILITY predicate the
        classifier does not implement: it tests MANIFEST MEMBERSHIP, and `bench/transport/*` are
        real CMake targets absent from the manifest, so a buildable base binary would be exempted
        anyway; (ii) declared the paired set "non-decreasing" AND permitted approved narrowing —
        incompatible, and the comparator (`check_paired_not_narrowed`) has no approval path at all,
        failing removal unconditionally; (iii) left a resurrection hole where an approved removal
        followed by a re-add collects the addition exemption again.
      Gate A round 4 (P1 ×1) — the round-3 text then permitted "retiring a benchmark entirely" as
        a separate act. To the comparator that IS removal (`check_paired_not_narrowed` fails removal
        and downgrade alike, with no retirement input), so the clause once more permitted what the
        code forbids — AND reopened the resurrection hole it claimed to close, since a retired row
        re-added later collects the candidate-only exemption again.
      The current text fixes all four by DESCRIBING THE SHIPPED INSTRUMENT: the manifest-membership
        predicate, with the over-exemption it causes named as a tracked follow-up rather than
        legislated around; and paired status ABSOLUTELY irreversible with NO retirement carve-out,
        which is exactly what the comparator enforces. The consequence — that there is no supported
        way to delete a paired benchmark without an amendment — is stated as a named gap rather
        than softened by an exception nothing enforces.
      Also dropped: "required to be an ancestor of the candidate", redundant — a merge-base is
        necessarily an ancestor.
  Added files: CHANGELOG.md (Article XX §4).
  Removed sections: none.
  §XVIII.5 disposition: NO conflict — this amends a verification instrument, not a protocol scope or
    a post-1.0 carve-out.
  Rationale: the rule as written was UNENFORCEABLE on exactly the set that matters, and a census
    rather than an argument establishes it. Under `bench/baselines/` there are 27 TRACKED FILES: 25
    JSON and 2 `.gitkeep`. Of the 25 JSON — an exact partition, 3+10+1+11=25 — 3 carry zero benchmark
    rows (`codegen/typed_accessor_bench.json`, `dictionary/reify_bench.json`,
    `session/placeholder.json`), 10 have no `cpu_time` KEY on any row, 1 (`bench/baselines/placeholder.json`,
    the TOP-LEVEL file, distinct from the session one) has a row whose `cpu_time` is `null`, and 11
    carry numeric `cpu_time`. Of those 11, one (`log/log_enqueue.json`) is a debug build → 10 release
    records; one of those (`sync/async_mutex_baselines.json`) is a hand-authored PARTIAL SCHEMA
    lacking `real_time`/`time_unit`/`run_type` → NINE usable full-schema release comparands, for 23
    benched binaries. FIVE files declare a debug build (`log_enqueue.json` + four wire records) across
    TWO key spellings, `library_build_type` and `build_type` — a single-spelling sweep undercounts
    them, which is how the first draft said "1 debug build".
    Of the FIVE binaries the paired tier hard-gates, FOUR (`framer_bench`, `parser_bench`,
    `writer_bench`, `validator_bench`) declare `none:hand-authored-record-no-cpu_time-field`, and the
    fifth's comparand — `dictionary/xml_loader.json` — was shown by #263 to describe a state that
    never shipped: seeded by `d526e082`, invalidated THREE commits later (`git rev-list --count
    d526e082..20a40f7b` = 3, not six as the first draft said) by `20a40f7b` inside the same PR (#66),
    never re-seeded, so the ±5% budget was silently breached 8–16x against it for three months. Only
    6 of 23 `bench/ci-suite.txt` rows declare a `gb-json:` comparand at all. The replacement
    instrument needs no stored comparand and is the only one this repo has MEASURED to be valid
    (paired same-VM 2.10x where unpaired read 1.02x on the same change). The ±5% figure is NOT
    changed; what changes is what it is measured against, what CI can presently decide, and who may
    accept a breach.
  CI-state dependency: the CI facts §2 relies on are TRUE on `main` as of `91abcc74` (PR #272, merged
    2026-08-20) and were re-verified against that tree, not against the branch this amendment was
    first drafted on: `PAIRED_BAND_PCT = 50.0`; `bench/ci-suite.txt` has 23 rows, 5 `paired`, 6
    `gb-json:`; the job is `bench` with no `continue-on-error` (the string "bench (soft)" survives
    only inside the `bench` job's own ccache-history comment (#273)); `.specify/ci209-bench-gate.md`
    exists. Gate A round 1's first P1 — that the amendment described CI absent from its own tree —
    is closed by that merge plus this rebase.
  Templates / dependents reviewed: plan-template.md / spec-template.md / tasks-template.md — no
    change. Affected catalogue rows: none (no OFFICIAL row asserts a comparand).
  Affected docs making PRESENT-TENSE comparand claims now superseded. ⚠️ This inventory is
    EXPLICITLY NOT EXHAUSTIVE, and the previous revision's claim that active docs were "listed in
    full" was FALSE (Gate A round 2, P2). Two successive sweeps found different sets: the first
    required a CI verb AND the word "baseline" on one line and returned 13 files; a semantic sweep
    over all of `.specify/*.md` + `bench/**` returned 25+, including several the first missed
    entirely. Completing and correcting the inventory is tracked as a FOLLOW-UP ISSUE, not claimed
    done here — an incomplete list presented as complete is the same defect class this amendment
    exists to fix. Historical `specs/NNN-*` bundles are deliberately out of scope.
    Known members, by claim (none fixed in this PR):
      ±5%-vs-previous-tagged-release, a comparand that was never implemented at all:
        .specify/2b-wire.md · .specify/2c-codegen.md + .draft-r1.md ·
        .specify/2d-threading.md · .specify/2e-msgstore.md (also asserts >2x for
        FileStore rows) · .specify/2f-async-mutex.md + .draft-r1.md
      ±5%-vs-bench/baselines, the comparand this amendment retires:
        .specify/2f-async-mutex.phase4-tests.md · .specify/2j-controlplane.md ·
        .specify/2l-tap.md · .specify/2k-log-otel.md · .specify/2g-tls.md ·
        .specify/2m-pybind.md · .specify/2i-capi.md · bench/README.md ·
        bench/REPORT.md · bench/session/CMakeLists.txt · bench/sync/CMakeLists.txt ·
        bench/threading/CMakeLists.txt · bench/threading/bench_threading.cpp ·
        bench/session/{bench_heartbeat_cadence.cpp, fix_time_bench.cpp, fsm_bench.cpp,
        heartbeat_bench.cpp, seqnum_bench.cpp, bench_compid_authorize.cpp} ·
        bench/sync/bench_async_mutex_{contended.cpp, uncontended.cpp} ·
        bench/tls/bench_pinset_snapshot_acquire.cpp · bench/dictionary/xml_loader_bench.cpp
      claims about binaries that are NOT in the 23-row manifest at all, so no CI gate of any kind
      applies to them — a stronger defect than a stale comparand:
        bench/transport/bench_async_read_some_dispatch.cpp ·
        bench/transport/bench_async_write_issue.cpp
      asserts a THIRD threshold, neither +5% nor +50%:
        bench/session/bench_file_store.cpp and bench/session/CMakeLists.txt ("> 2x regression")
      bench/session/bench_heartbeat_cadence.cpp · bench/session/fix_time_bench.cpp ·
        bench/session/fsm_bench.cpp · bench/dictionary/xml_loader_bench.cpp (header comment)
  Process: standalone amendment PR per Article XX §2 — NOT folded into PR #272, which carries a Gate
    A WAIVER, and §2 mandates Codex Gate A review on EVERY amendment. Gate A therefore runs on this
    PR and must not be waived; the Gate-A-fold deviation used by 035/043/068/069/075-078/082 applies
    only to amendments riding a feature branch, which this is not. Gate A round 1 verdict was BLOCK
    (4 P1 / 5 P2); this revision answers all nine. User ratification of the v-major classification and
    of creating CHANGELOG.md given 2026-08-20.

Sync Impact Report — v0.10 → v0.11 (2026-08-12) — RATIFIED
  Bump: MINOR (reclassifies a post-1.0 carve-out as delivered; no banned-pattern addition, no perf-budget tightening, Article I §1's codegen scope permissively unchanged → not v-major per Article XX §4).
  Modified principles:
    - Article XVIII §7 (Application-message codegen scope for v1.0) — reclassifies the "`fixpp::v42` builders remain DEFERRED" clause (v0.9) as DELIVERED. Feature 082 removes the root blocker: repeating-group detection moves from the count field's declared XML datatype (`FieldRef::type == NumInGroup`) to the `<group>` element itself, at every read/validate/codegen site, so FIX 4.2's legacy `INT`-typed count fields now materialize typed repeating groups like any other version (18 groups; FIX 4.0 gains 4, FIX 4.1 gains 7, FIX 4.3 gains one — `NoClearingInstructions(576)`, a real group its dictionary mis-types). `fixpp::v42` therefore carries the full typed `build_/validate_/Args` tier through the same 077 structural-plan Args-dedup emitter as v44/v50sp2/vlatest, packaged per 078 as a precompiled per-version library. The Article VI silent-omission hazard that justified the original descope is closed by direct test, not by construction: all 14 required-repeating-group omissions across the v42 set are rejected by `writer_traits<Args>::group_checks`.
  Added sections: none. Removed sections: none.
  §XVIII.5 disposition: NO conflict — 082 delivers already-in-scope typed codegen for an already-supported dictionary (FIX 4.2, already named in Article I §1); it is not an early-shipped post-1.0 protocol, so §5 is not engaged.
  Rationale: feature 082 (structural-group-detection, PR #261, closes issue #196) is the root-cause fix L-063-1 identified: repeating-group detection was keyed on the count field's declared XML datatype rather than the `<group>` element, which is why FIX 4.0/4.1/4.2 registered zero groups and `fixpp::v42` builders were descoped rather than shipped with a silent-omission hazard. Making detection structural (`group_first_field(t) != 0`) closes that gap at the source, reachable on all nine QuickFIX-XML dictionaries; Article I §1's codegen scope already read "FIX 4.2, FIX 4.4, FIX 5.0 SP2, FIXT.1.1" and is CONFIRMED UNCHANGED — the amendment is permissive and widens no version set. The six unaffected dictionaries are pinned byte-identical by golden diff (SC-005); the v0.9 entry recording the original v42 descope is left intact as historical record even though its clause is now superseded.
  Templates / dependents reviewed: plan-template.md / spec-template.md / tasks-template.md — no change. Affected catalogue rows: no new OFFICIAL row (082 widens the DICTIONARY SCOPE of existing rows W-014/M-002/A-002 and others from six/five versions to nine/v42, per feature-catalogue.md's per-row amendments); coverage-index.md rows amended in place, no new entry.
  Process: Rides feature 082's branch / PR #261 per the established Gate-A-fold deviation from Article XX §2 (precedents: 035, 043, 068, 069, 075, 076, 077, 078). Article XX §2 user-ratification given 2026-07-30 (`specs/082-structural-group-detection/spec.md` § Open decisions OD-2); Gate A converged 3 rounds (gate-a-done), user-signed-off 2026-07-30.

Sync Impact Report — v0.9 → v0.10 (2026-07-18) — RATIFIED
  Bump: MINOR (annotation-only: records feature 078's implementation-layout restructure of the already-delivered typed builder/validator tier; no principle/scope/carve-out change, wire byte-identical, zero core/C-ABI change → not v-major per Article XX §4).
  Modified principles:
    - Article I §1 (FIX Latest read/dictionary + typed builder-codegen tier bullet) — annotates the "576 plans / ~78 MB single-TU header" narration: feature 078 restructures the typed builder/validator tier's on-disk layout into precompiled per-version STATIC libraries (`fixpp::{builders,validators}::{v44,v50sp2,vlatest}`) + a slim declaration surface (per-plan `groups/<Plan>.hpp` + `all.hpp` + per-message header-only inline mode). Consumer single-message compile RSS is closure-bounded, NOT universally order-of-magnitude below the monolith (L-078-1); wire output byte-identical.
    - Article XVIII §7 (Application-message codegen scope for v1.0) — annotates the "single version-agnostic Args-dedup emitter" narration: feature 078 packages the tier as precompiled per-version builder/validator STATIC libraries; consumer opt-in is purely link-time. Layout restructure only.
  Added sections: none. Removed sections: none.
  §XVIII.5 disposition: NO conflict — 078 changes the on-disk layout of an already-delivered tier, not a deferred post-1.0 protocol or a scope widening.
  Rationale: feature 078 (precompiled-builder-libs) restructures 077's monolithic header-only `Builders.hpp` builder tier into precompiled per-version STATIC libs + slim declaration headers + per-plan group headers + per-message header-only inline mode (issue #198). Zero `src/`/`capi/`/`bindings/` change (C-ABI frozen 1.5.0); wire byte-identical / validator result-identical (SC-004); determinism byte-stable over the regenerated golden set. The "single-TU header" characterization in I §1 / XVIII §7 is annotated to the delivered split layout. `install(EXPORT)` for an installed external consumer + the #197 heavy-test stopgap deletion (T034) are named follow-ups.
  Templates / dependents reviewed: plan-template.md / spec-template.md / tasks-template.md — no change. Affected catalogue rows: 078 adds NO new OFFICIAL feature-catalogue row (layout restructure, byte-identical wire); D-011 tier note + coverage-index "no new coverage entry" disposition (078 T039).
  Process: Rides feature 078's branch / PR #200 per the established Gate-A-fold deviation from Article XX §2 (precedents: 035, 043, 068, 069, 075, 076, 077). Gate A converged 3 rounds (gate-a-done); Gate B converged 7 rounds (gate-b-done, 0 waivers). Ratified: 2026-07-18 (user pre-authorized merge).

Sync Impact Report — v0.8 → v0.9 (2026-07-16) — RATIFIED
  Bump: MINOR (records two in-scope v1.0 deliveries by removing/reclassifying post-1.0 carve-outs; no banned-pattern addition, no perf-budget tightening → not v-major per Article XX §4).
  Modified principles:
    - Article I §1 (FIX Latest read/dictionary-tier bullet) — REMOVES the "typed `build_<Msg>` builder codegen" post-1.0 carve-out for FIX Latest. Feature 077 delivers the FIX Latest typed builder tier (`build_<Msg>`/`validate_<Msg>`/`Args`) under `fixpp::vlatest` via a structural-plan Args-dedup redesign (each repeating group's Args emitted once per distinct recursive `(no_tag, signature)` plan into `fixpp::vlatest::groups`, collapsing the 076-blocking 137 MB / 26,806-struct header to 576 plans / ~78 MB, single-TU-compilable). Only ApplExtID(1156)=303 differentiation and session negotiation remain post-1.0.
    - Article I §1 (post-1.0 milestone line) — "FIX Latest (typed-BUILDER-codegen / session-negotiation tiers)" → "FIX Latest (session-negotiation tier)"; the parenthetical records the typed builder tier is delivered in v1.0 by 077.
    - Article XVIII §7 (Application-message codegen scope for v1.0) — reclassifies the "`fixpp::v50sp2` application-message widening remains v1.x-deferred" clause as DELIVERED by feature 077: v50sp2 (156 app msgs) now carries typed `build_/validate_/Args` builders (full `is_application` set; no v44 {BE,BF,BW,BX,BY} exclusion inherited — BW/BX/BY are genuine FIX 5.0 application messages), joined by the re-enabled `fixpp::vlatest` builder tier (173) and the deduped v44 tier. vt11 is admin-only → no builders. **`fixpp::v42` builders remain DEFERRED** (discovered at /implement): FIX 4.2 types `NumInGroup` as legacy XML `INT`, so the emitter materializes zero typed groups (L-063-1) and a scalar-only v42 builder would silently omit required groups (invalid FIX 4.2); the fix regenerates the v42 read golden (out of 077's FR-009 scope). Tracked as issue #196.
    - Article XVIII §2 (v1.2 FIX Latest application-messages item) — narrows the annotation: the A-035..A-065 typed builders are now delivered under `fixpp::vlatest` in v1.0 by 077; only the EP-level field back-port into the legacy dicts and ApplExtID(1156)=303 on-wire differentiation remain post-1.0.
  Added sections: none. Removed sections: none.
  §XVIII.5 disposition: NO residual conflict. §5 bars early-shipping deferred post-1.0 **protocols** (SOFH, SBE, FIXP, FAST, JSON, GPB, MMT); typed builder codegen for already-supported dictionaries is not a protocol, so §5 is not engaged. The amendment is what makes the builder tier cease to be deferred, not a waiver against §5.
  Rationale: feature 077 (builder-args-dedup) delivers the typed builder tier that 076 descoped — the component-identity Args-dedup redesign 076's close-out named. It keys each group's Args by `(no_tag, recursive structural signature)`, emits each distinct plan once into `fixpp::<ns>::groups`, re-enables the vlatest builder tier, and widens builders to v50sp2 (v44 already shipped, now deduped; vlatest re-enabled; **v42 DEFERRED** — L-063-1 zero-typed-groups renders a v42 builder unable to express required groups, tracked as issue #196; vt11 admin-only, none). A non-circular raw-XML/Orchestra completeness census (FR-010) proves per-version exact-set coverage; v44's shipped builder golden is deliberately regenerated to the deduped output (v44 nested-Args source-API break, no aliases — pre-1.0, no downstream users); legacy read tiers stay byte-identical.
  Templates / dependents reviewed: plan-template.md / spec-template.md / tasks-template.md — no change. Affected catalogue rows: v42/v50sp2/vlatest builder tiers + v44 dedup flip to `done` with matching coverage-index entries (077 T031).
  Process: Rides feature 077's branch per the established Gate-A-fold deviation from Article XX §2 (precedents: 035, 043, 068, 069, 075, 076). Gate A converged 3 rounds (2026-07-16), gate-a-done, no waivers. Ratified: 2026-07-16 pending user sign-off.

Sync Impact Report — v0.7 → v0.8 (2026-07-16) — RATIFIED
  Bump: MINOR (narrows a post-1.0 carve-out by recording an in-scope v1.0 delivery; no banned-pattern addition, no perf-budget tightening → not v-major per Article XX §4).
  Modified principles (BOTH loci edited in one bump — else internally contradictory post-merge):
    - Article I §1 (FIX Latest read/dictionary-tier bullet) — narrows the deferred-to-post-1.0 list from "typed codegen, ApplExtID(1156)=303 differentiation, and session negotiation" to "typed BUILDER codegen (`build_<Msg>`), ApplExtID(1156)=303 differentiation, and session negotiation". Feature 076 delivers the FIX Latest typed **read/reify/args/validator codegen tier for all 181 messages** under `fixpp::vlatest` (behind the opt-in `FIXPP_CODEGEN_FIX_LATEST` build flag, default ON) PLUS a non-circular completeness census proving it — squarely in-scope v1.0 work. Only the typed **`build_<Msg>` builder** tier is NOT delivered: it is deferred to a follow-up feature (the shared 067/069 `emit_builders` emits per-message non-deduplicated nested-group `Args`, producing a 137 MB uncompilable `vlatest/Builders.hpp`; a component-identity Args-dedup redesign is required first).
    - Article I §1 (post-1.0 milestone line) — "FIX Latest (typed-codegen / session-negotiation tiers)" → "FIX Latest (typed-BUILDER-codegen / session-negotiation tiers)"; the parenthetical records that the typed read/reify/args/validator tier is delivered in v1.0 by 076.
    - Article XVIII §2 (v1.2 FIX Latest application-messages item) — annotated: the A-035..A-065 typed **read** classes are delivered under `fixpp::vlatest` in v1.0 by 076; only their typed builders, the EP-level field back-port into the legacy dicts, and ApplExtID(1156)=303 on-wire differentiation remain post-1.0.
  Added sections: none. Removed sections: none.
  §XVIII.5 disposition: NO residual conflict. §5 bars early-shipping deferred post-1.0 **protocols** (SOFH, SBE, FIXP, FAST, JSON, GPB, MMT); typed codegen for an already-supported dictionary is not a protocol, so §5 is not engaged. The amendment is what makes the read-tier typed codegen cease to be deferred, not a waiver against §5.
  Rationale: feature 076 (fix-latest-typed-codegen) generates the typed read/reify/args/validator surface for all 181 FIX Latest messages from 074's native OrchestraLoader, with a two-leg non-circular completeness census (no QuickFIX peer). The typed BUILDER tier was descoped mid-implementation (user-decided 2026-07-16, spec.md Clarifications) on discovering the 137 MB uncompilable Builders.hpp; `emit_builders` stays v44-only so the v44 golden and the additive guarantee are untouched. Narrowing the carve-out to "typed BUILDER codegen" (not all "typed codegen") records exactly what shipped.
  Templates / dependents reviewed: plan-template.md / spec-template.md / tasks-template.md — no change. Affected catalogue rows: the 076 read-tier typed-codegen delivery is recorded once, authoritatively, on the **tier row `D-011`** (in `spec/feature-catalogue.md` and `spec/coverage-index.md`) — 076 delivers typed read/reify/args/validator classes for **all 181** FIX Latest messages, not the 31 new A-035..A-065 MsgTypes specifically. The A-035..A-065 application-message rows stay `backlog` (their FULL delivery — builders + ApplExtID(1156)=303 wire-differentiation — is v1.2/post-1.0 per Article XVIII §2) with a one-line partial-progress cross-ref to D-011; NOT flipped to `done` (076 T026).
  Process: Rides feature 076's branch per the established Gate-A-fold deviation from Article XX §2 (precedents: 035, 043, 068, 069, 075). Ratified: 2026-07-16 pending user sign-off.

Sync Impact Report — v0.6 → v0.7 (2026-07-14) — RATIFIED
  Bump: MINOR (narrows a post-1.0 carve-out by recording an in-scope v1.0 delivery; no banned-pattern addition, no perf-budget tightening → not v-major per Article XX §4).
  Modified principles:
    - Article I §1 (FIX Latest read/dictionary-tier bullet, added by v0.6) — narrows the deferred-to-post-1.0 list from "typed codegen, live wire validation, ApplExtID(1156)=303 differentiation, and session negotiation" to "typed codegen, ApplExtID(1156)=303 differentiation, and session negotiation" — dictionary-driven wire validation (required/type/enum-domain/group-structure checking via `wire::dictionary_driven_validator` + `table_view::enum_valid()`) is explicitly called out as NOT in the carve-out: feature 075 delivers it generically for all ten supported dictionaries (the nine QuickFIX-XML versions plus FIX Latest), with no FIX-Latest-specific code.
    - Article I §1 (post-1.0 milestone line) — "FIX Latest (typed-codegen / wire / session tiers)" → "FIX Latest (typed-codegen / session-negotiation tiers)"; the parenthetical is updated to record that dictionary-driven wire validation is delivered in v1.0 by 075.
  Added sections: none. Removed sections: none.
  §XVIII.5 disposition: NO residual conflict once §1 is narrowed. §5 bars early-shipping deferred post-1.0 **protocols**; Article XVIII §2's roadmap enumerates protocols (SOFH, SBE, FIXP, FAST, JSON, GPB, MMT). Dictionary-driven wire validation is not a protocol, so §5 is not engaged on its own terms — the amendment is what makes the scope cease to be deferred, not a waiver against §5.
  Rationale: feature 075 (live-wire-enum-validation) makes `table_view::enum_valid()` real, discharging the long-standing Phase-1 stub (L-041-1) for the nine legacy dictionaries — squarely in-scope v1.0 work, independent of FIX Latest. But the check is dictionary-generic: a FIX Latest dictionary loaded into a validating session gets enum-domain checking as an automatic consequence of the same store-driven projection (FR-002/FR-003), with no FIX-Latest-specific code. That touches the letter of the pre-amendment Article I §1 carve-out (which listed "live wire validation" as post-1.0) and Article XVIII §5. Narrowing the carve-out resolves the conflict by construction rather than shipping a silent violation. User-dispositioned at `/clarify` (spec.md Clarifications, "Which dictionaries get enum-domain validation" — all ten).
  Templates / dependents reviewed: plan-template.md / spec-template.md / tasks-template.md — no change. Affected catalogue rows: `spec/coverage-index.md`'s 074-orchestra-native-reader note / W-014 row / §4.5.4 row / D-011 row, `spec/feature-catalogue.md`'s D-011 row / W-014 row — landed alongside this amendment (075 T035/T036).
  Process: Appendix-A mandatory-trigger feature (wire-format/parser: validator changes) → Codex Gate A required (converged round 5, user-signed-off 2026-07-14, submodule `8c8ec699`). Rides feature 075's branch per the established Gate-A-fold deviation from Article XX §2's standalone-PR letter (precedents: 035, 043, 068, 069). Ratified: 2026-07-14 pending user sign-off at Gate A / `/plan`.

  Prior: Sync Impact Report — v0.5 → v0.6 (2026-07-13) — RATIFIED
  Bump: MINOR (additive version-set widening at the read/dictionary tier; no banned-pattern/perf/config change → not v-major per Article XX §4). This is the FIRST amendment to add a NEW FIX version to the supported set (distinct from the 035/043/068/069 within-scope reclassifications, none of which widened the FIX version set).
  Modified principles:
    - Article I §1 — after the runtime-XML-scope bullet, adds a "FIX Latest (read/dictionary tier only)" bullet: `dict::OrchestraLoader` natively ingests the official FIX Orchestra machine-readable standard (`OrchestraFIXLatest.xml`, EP303) into a runtime `Dictionary` under `session_version::vlatest`. Explicitly scoped to the dictionary/runtime-read tier — it does NOT extend the "v1.0 ships 100% of the official spec" session+application obligation to FIX Latest (typed codegen, live wire validation, ApplExtID(1156)=303 differentiation, and session negotiation remain post-1.0).
    - Article I §1 (post-1.0 milestone line) — annotates "FIX Latest" to record that its read/dictionary tier is delivered in v1.0 by feature 074; typed/wire tiers remain post-1.0.
  Added sections: none. Removed sections: none.
  Rationale: REMAINING-WORK.md §A row 4b promoted FIX Latest (read/dictionary tier) to v1.0-gating (user, 2026-07-13). `session_version::vlatest` widens the supported version set, so Article XX requires amending the constitution rather than silently violating Article I §1. Scoped to the read/dictionary tier only so it does not trigger Article I §1's "100% of the official spec" (session+application) obligation for FIX Latest.
  Templates / dependents reviewed: plan/spec/tasks templates — no change. Affected catalogue rows: feature 074 close-out rows (feature-catalogue.md) + coverage-index.md D-011 promotion + A-035..A-065 linkage (feature-074 task T027).
  Process: Appendix-A mandatory-trigger feature (multi-version dictionary coexistence + version identity) → Codex Gate A required (satisfied on the 074 branch, converged round 2 `c2611b2f`). Rides feature 074's branch per the established Gate-A-fold deviation from Article XX §2's standalone-PR letter (precedents: 035/043/068/069). Ratified: 2026-07-13 pending user sign-off at Gate A / `/plan`.

  Prior: Sync Impact Report — v0.4 → v0.5 (2026-07-11) — RATIFIED
  Bump: MINOR (roadmap reclassification; additive — reclassifies v44 msgcat='app' typed codegen from §XVIII.7 v1.x-deferred to v1.0-delivered-by-069; no banned-pattern/perf/config change → not v-major per Article XX §4).
  Modified principles:
    - Article XVIII §7 (Application-message codegen scope for v1.0) — the FIX44-present v44 msgcat='app' subset of A-014..A-034 (BusinessMessageReject A-014, DontKnowTrade A-015, the List family A-019, the Quote/RFQ family A-021, the SecurityList family A-025 incl. SecurityType `v`/`w`, the Network-status family N-001, plus A-016/017/020/022/026 and the C-001/002, R-, P- families) is reclassified as DELIVERED by feature 069 under fixpp::v44 — **delivery is set-based on the full 83-message msgcat='app' scope (the illustrative family list is not exhaustive; every msgcat='app' MsgType except {BE,BF} is delivered)**; the mixed rows A-022/A-026/C-002 deliver only their FIX44 MsgTypes (AW / z,AA / AL,AM,AN,AO,AP). The FIX50-only rows A-018/023/027/028/029/030/031/032/033 + C-003 (whole rows absent from FIX44) and the mixed-row siblings BO (A-022) / BR (A-026) / BL (C-002) carry no FIX44 message and stay deferred to fixpp::v50sp2/all-version widening (NOT delivered by 069). XMLnonFIX (A-034, 35=n) stays deferred — it is msgcat='admin', outside the application-writer emitter, runtime-XML only. A-024 stays dropped-as-duplicate ([SYN §4.4]). fixpp::v42 / fixpp::v50sp2 app-message widening remains v1.x-deferred (069 is v44-only). Absorbs the pending D7 §XVIII.7 staleness (which listed the C/R families inconsistently).
  Added sections: none. Removed sections: none.
  §XVIII.5 disposition: NO amendment required — reclassifying this scope as delivered-now removes it from the "deferred post-1.0 scope being early-shipped" category, so §5's no-early-ship bar has no residual conflict once §7 is rewritten.
  Rationale: 067 shipped the v1.0 OFFICIAL v44 set; nothing coherent remains to widen without landing the §7-deferred app families, and the v1.0 tag is not yet cut. User elected proceed-now (2026-07-11).
  Templates / dependents reviewed: plan/spec/tasks templates — no change. Affected catalogue rows: coverage-index.md write-column flips for A-014/015/019/025 + A-016/017/020/022/026 + C-001/002 + R-001..005 + P-004..008 (v44-present MsgTypes only — the mixed rows A-022/A-026/C-002 flip only AW / z,AA / AL,AM,AN,AO,AP; the FIX50-only rows A-018/023/027/028/029/030/031/032/033 + C-003 and siblings BO/BR/BL are NOT flipped, no v44 row exists to flip); feature-catalogue.md 069 close-out rows.
  Process: Appendix-A codegen-trigger feature → Codex Gate A required (satisfied on the 069 branch). Rides feature 069's branch rather than a standalone `Constitution: amend §XVIII.7 — …` PR (Article XX §2 PR-title form) — a deviation from the letter of §2, recorded here (precedent: 035/043 Gate-A-folded amendments). Ratified: 2026-07-11 pending user sign-off at Gate A.

  Prior: Sync Impact Report — v0.3 → v0.4 (2026-07-11) — RATIFIED
  Bump: MINOR (adds a testing-authoring convention sub-clause; purely additive — no existing config breaks, no banned-pattern addition, no perf-budget tightening → not v-major per Article XX §4).
  Modified principles:
    - Article VII (Testing Requirements) — added §8: new isolation-safe test `.cpp` files default to an existing whole-binary grouped executable per module (`add_executable` + `add_test(NAME <bucket>)` + `LABELS`), not one executable per `.cpp`; `gtest_discover_tests` prohibited for these buckets (regresses serial ctest ~5–6×, measured in feature 068). Tests are selected by `ctest -L <label>`, never `-R <exe-name>`. Isolation-sensitive tests (global alloc/OOM injection, TSan-heterogeneous env, own `main()`/concurrency, exact-set completeness gates) remain standalone.
  Added sections: Article VII §8. Removed sections: none.
  Rationale: feature 068 (test-binary-grouping) cut a 66.9 GB one-exe-per-.cpp test matrix via whole-binary grouping; this codifies the pattern as the durable authoring convention so future tests don't regress the disk/ctest-launch win.
  Templates / dependents reviewed:
    - plan-template.md / spec-template.md / tasks-template.md — no change needed (generic stock templates, no CMake/ctest content to align).
    - specs/068-test-binary-grouping/quickstart.md — aligned in the same change (superseded `gtest_discover_tests` example → whole-binary `add_test`; `-R` → `-L` selection guidance).
  Process: additive documentation/convention amendment, not an Appendix A mandatory-trigger category (no ABI/threading/error-semantics/wire-format/codegen/session-FSM/security surface) — no Codex Gate A required on the amendment itself (068 itself is a gate-a-waived candidate, test-infra only; see `.specify/decisions/068-test-binary-grouping-gatea.md`). Rides feature 068's own branch rather than a standalone `Constitution: amend §VII.8 — …` PR (Article XX §2's PR-title form); this is a deviation from the letter of §2, recorded here rather than silently taken. **Ratified: 2026-07-11 (explicit user request, feature 068)** — the amendment was directly requested by the user as part of scoping feature 068's durable authoring convention, which stands in for the formal Gate-A-reviewed sign-off §2 otherwise prescribes (waived per the same gate-a-waived rationale as the feature itself: additive, outside every Appendix A trigger category). No fabricated reviewer signature is recorded; the sole basis for ratification is the user request itself.

  Prior: Sync Impact Report — v0.2 → v0.3 (2026-06-17): Article XII §5 — reopened closed SecurityProfile set + added `insecure_plain_tcp` non-TLS profile (Gate A folded into feature 043). User-signed-off 2026-06-17.
  Prior: Sync Impact Report — v0.1 → v0.2 (2026-06-13): Article XV §1 + XI §6 — FileStore §XV.4-offload bounded-frame exemption (Gate A folded into feature 035). User-signed-off 2026-06-13.
-->
# fixpp Constitution

> **Status:** **v3.0** PROPOSED (2026-09-23; Codex Gate A and user sign-off pending) — **backwards-incompatible** (Article XX §4 v-major, with a `CHANGELOG.md` entry). The orchestrator does not implement, and Article XVI §6 defines what that covers; Articles XVI §7, XVII §4–§5 and XX §5 name roles instead of models. Prior: user-ratified **v2.0** (2026-09-15) — **backwards-incompatible** (Article XX §4 v-major, with a `CHANGELOG.md` entry). Adds Article X §7: before fixpp's first public release (its first GitHub Release) a breaking C-ABI change bumps MINOR and is declared BREAKING; the compatibility promise, and a reset of the C-ABI version to 1.0.0, start at that release. Qualifies Article IX §5 to match. Rewrites Article XX §3: from v2.0 on, amendments are recorded in their Sync Impact Report (and in `CHANGELOG.md` when §4 applies), not in `_log.md`. Standalone amendment PR #451; Codex Gate A converged at round 4, then user sign-off 2026-09-15. Prior: user-ratified **v1.0** (2026-08-21) — **first backwards-incompatible amendment** (Article XX §4 v-major, with the `CHANGELOG.md` entry that clause has required since v0.1 and which did not exist until this PR created it). Re-points Article VIII §2's regression comparand from checked-in `bench/baselines/` — of which only **nine** of 25 JSON files are usable full-schema release records — to a **paired base-vs-candidate run on one runner** against the merge-base of the candidate and the PR's target branch. The budget is stated **one-sided** (a slowdown greater than **+5%**) to match the shipped comparator; CI's provisional threshold **must not exceed +50%**, a constitutional ceiling on how weak CI may become. Adds **§2a**, the fail-closed invariants, whose sole exemption is a paired row **absent from the merge-base's manifest** and under which paired status is **irreversible with no exception**. Accepted slowdowns require approval by someone other than the author. §4's v1.0 release baseline is explicitly untouched. Implemented by PR #272 (`91abcc74`, closes the instrumentation half of #263). Standalone amendment PR #285 per Article XX §2 — **Gate A ran and was NOT waived**, converging at round 5 after four BLOCKs; Gate B waived (docs-only: `.specify/` + `CHANGELOG.md`, zero code and zero CI), rationale disclosed in the PR body. Prior: v0.11 (2026-08-12) — annotation-only: records feature **082 (structural-group-detection, PR #261, closes issue #196)** reclassifying Article XVIII §7's "**`fixpp::v42` builders remain DEFERRED**" clause as **DELIVERED**. Repeating-group detection moves from the count field's declared XML datatype (`FieldRef::type == NumInGroup`) to the `<group>` element, at every read/validate and codegen site, so FIX 4.0/4.1/4.2's legacy `INT`-typed count fields register groups (4 / 7 / 18) and FIX 4.3 gains one real group its dictionary mis-types (`NoClearingInstructions(576)`); `fixpp::v42` gains the full typed builder/validator tier, with all 14 required-group omissions rejected by test rather than avoided by descope. **Article I §1's codegen scope is CONFIRMED UNCHANGED** — it already reads "FIX 4.2, FIX 4.4, FIX 5.0 SP2, FIXT.1.1", so this amendment is permissive and widens the version set nowhere; the six unaffected dictionaries are pinned byte-identical by golden diff. **The v0.9 entry below is left intact as historical record** even though its v42 clause is now superseded. Article XX §2 user-ratification given **2026-07-30** (`specs/082-structural-group-detection/spec.md` § Open decisions OD-2); Gate A converged at round 3 and was user-signed-off 2026-07-30, folded into feature 082. Prior: v0.10 (2026-07-18) — annotation-only: records feature 078 (precompiled-builder-libs, PR #200) restructuring the already-delivered typed builder/validator tier's on-disk layout into precompiled per-version STATIC libraries + a slim declaration surface (Article I §1 + XVIII §7 annotated); wire byte-identical, zero core/C-ABI change, consumer compile closure-bounded (L-078-1). Gate A (3 rounds) + Gate B (7 rounds, 0 waivers) folded into feature 078. Prior: v0.9 (2026-07-16) — REMOVES the FIX Latest typed `build_<Msg>` builder codegen post-1.0 carve-out from Article I §1 (both loci) + reclassifies Article XVIII §7 v50sp2 application-message builder widening as v1.0-delivered (v42 builders DEFERRED — L-063-1 zero-typed-groups, issue #196) + narrows the Article XVIII §2 v1.2 annotation: feature 077 delivers the FIX Latest typed builder tier (and v50sp2 builders + v44 dedup) via a structural-plan Args-dedup redesign (each group's Args emitted once per distinct `(no_tag, recursive signature)` plan into `fixpp::<ns>::groups`; 576 plans / ~78 MB single-TU vlatest header, down from 137 MB), proven per-version complete by a non-circular raw-XML/Orchestra census; only ApplExtID(1156)=303 differentiation + session negotiation remain post-1.0 for FIX Latest. Gate A folded into feature 077 (converged 3 rounds, no waivers). Prior: v0.8 (2026-07-16) — amends Article I §1 (BOTH the FIX Latest bullet and the post-1.0 milestone line) + annotates Article XVIII §2 to record that feature 076 delivers the FIX Latest typed **read/reify/args/validator codegen tier for all 181 messages** under `fixpp::vlatest` in v1.0 (opt-in `FIXPP_CODEGEN_FIX_LATEST`, non-circular census); the post-1.0 carve-out narrows to *typed `build_<Msg>` builder codegen + ApplExtID(1156)=303 differentiation + session negotiation* — the builder tier deferred to a follow-up (137 MB uncompilable Builders.hpp; Args-dedup redesign required). Gate A folded into feature 076. Prior: v0.7 (2026-07-14) — amends Article I §1 to narrow the FIX Latest post-1.0 carve-out (feature 074, v0.6) to *typed codegen + ApplExtID(1156)=303 differentiation + session negotiation*: dictionary-driven wire validation (required/type/enum-domain/group-structure checking) now ships generically for all ten supported dictionaries in v1.0 via feature 075; Gate A folded into feature 075, converged round 5, user-signed-off 2026-07-14. Prior: v0.6 (2026-07-13) — amends Article I §1 to add FIX Latest at the read/dictionary tier via `dict::OrchestraLoader` / `session_version::vlatest` (feature 074) — the first version-set widening; scoped to the dictionary/runtime-read tier only (typed/wire/session tiers stay post-1.0); Gate A folded into feature 074, user-signed-off 2026-07-13. Prior: v0.5 (2026-07-11) — amends Article XVIII §7 (reclassifies the FIX44 `msgcat='app'` typed-codegen subset of A-014..A-034 from v1.x-deferred to v1.0-delivered-by-069; full 83-message set-based delivery, FIX50-only rows + XMLnonFIX stay deferred; Gate A folded into feature 069, user-signed-off 2026-07-11). Prior: v0.4 (2026-07-11) — adds Article VII §8 (test-authoring convention: whole-binary grouped executables, `gtest_discover_tests` prohibited for buckets, `ctest -L` selection; feature 068). Ratified by explicit user request 2026-07-11 (not a standalone Article XX §2 PR — rode feature 068's branch; Codex Gate A waived, additive/no-trigger-category, same rationale as the feature's own `gate-a-waived` disposition). Prior: v0.3 (2026-06-17) — amends Article XII §5 (reopens the closed `SecurityProfile` set + adds `insecure_plain_tcp` non-TLS profile; opt-in-only, loud `[[deprecated]]`-class friction; Gate A folded into feature 043). v0.2 (2026-06-13) — amends Article XV §1 + XI §6 (FileStore §XV.4-offload bounded-frame exemption; Gate A folded into feature 035). Base v0.1 (2026-05-10) — Phase 2 Gate A converged (Codex review + Claude Sonnet review + Codex adversarial pass, all 18 issues resolved); see `decisions/constitution.md`.
> **Authority:** This document is project-wide non-negotiables. Every `/specify`, `/plan`, ADR, and PR must satisfy it. Conflicts are resolved by amending the constitution first (Article XX) — never by silently violating an article.
> **Citation form:** other documents cite articles as `[const §Roman.arabic]` (e.g., `[const §VIII.3]`).

---

## Article I — Identity & Mission

1. **`fixpp` is a modern C++23 implementation of the FIX protocol.** Session layer + application layer for FIX 4.0 through 5.0SP2 + FIXT.1.1. v1.0 ships 100% of the official spec for those versions, with the following codegen-vs-runtime split:
   - **Codegen scope (per `[2c §1.3]`):** FIX 4.2, FIX 4.4, FIX 5.0 SP2, FIXT.1.1. Typed-message classes, `constexpr` field metadata, per-message validators, `dict::reify` runtime-dispatch all generated under per-version namespaces (`fixpp::v42`, `fixpp::v44`, `fixpp::v50sp2`, `fixpp::vt11`).
   - **Runtime-XML scope:** FIX 4.0, FIX 4.1, FIX 4.2, FIX 4.3, FIX 4.4, FIX 5.0, FIX 5.0 SP1, FIX 5.0 SP2, FIXT.1.1. `dict::XmlLoader` accepts QuickFIX-XML for any of these; runtime `Dictionary` works for field/required/group/length-pair lookups; users access fields through the runtime tag-keyed accessor.

   The runtime-XML-only versions (4.0 / 4.1 / 4.3 / 5.0 / 5.0 SP1) ship without a typed-message namespace in v1.0. Per-version codegen for those versions is deferred to post-v1.0 best-effort per Article XVIII §6.

   - **FIX Latest (read/dictionary + typed read/builder-codegen tiers):** `dict::OrchestraLoader` natively ingests the official FIX Orchestra machine-readable standard (`OrchestraFIXLatest.xml`, EP303) into a runtime `Dictionary` under `session_version::vlatest` (wire application version `v50sp2` via `session_to_application`; no distinct ApplVerID) — delivered by feature 074. Feature 076 additionally delivers the **typed read/reify/args/validator codegen tier for all 181 FIX Latest messages** under `fixpp::vlatest` (behind the opt-in `FIXPP_CODEGEN_FIX_LATEST` build flag, default ON), proven complete non-circularly by a two-leg census (no QuickFIX peer); feature 077 delivers the **typed `build_<Msg>`/`validate_<Msg>` builder codegen tier** under `fixpp::vlatest` via a structural-plan Args-dedup redesign (each group's Args emitted once per distinct `(no_tag, recursive signature)` plan into `fixpp::vlatest::groups`; 576 plans / ~78 MB single-TU header, down from the 076-blocking 137 MB). *(Annotation, feature 078 / v0.10: the typed builder/validator tier's on-disk layout is restructured from that single-TU header into precompiled per-version STATIC libraries — `fixpp::{builders,validators}::{v44,v50sp2,vlatest}` — plus a slim declaration surface (per-plan `groups/<Plan>.hpp` + `all.hpp` + per-message header-only inline mode). A consumer's single-message compile RSS is closure-bounded, NOT universally an order of magnitude below the monolith — see L-078-1. Wire output byte-identical; zero core / C-ABI change.)* This does NOT extend the "v1.0 ships 100% of the official spec" session+application obligation to FIX Latest: **ApplExtID(1156)=303 differentiation and session negotiation remain post-1.0** (the typed read tier is delivered by 076, the typed builder tier by 077). **Dictionary-driven wire validation is NOT in the carve-out** — feature 075 delivers `enum_valid()`/type/required/group-structure checking generically for all ten supported dictionaries (the nine QuickFIX-XML versions plus FIX Latest via `session_version::vlatest`), with no FIX-Latest-specific code; a v1.0-scoped capability.

   FIX Latest (session-negotiation tier), FIXP, SBE, FAST, SOFH, JSON, GPB, and FIX MMT are post-1.0 milestones (Article XVIII). *(FIX Latest's read/dictionary tier is delivered in v1.0 per feature 074; its typed read/reify/args/validator codegen tier per feature 076; its typed `build_<Msg>` builder tier per feature 077; dictionary-driven wire validation per feature 075 (generic across all ten dictionaries); only its ApplExtID(1156)=303 differentiation and session-negotiation tiers remain post-1.0.)*
2. **Primary distribution: in-process C++23 library** (static `.a`/`.lib` or shared `.so`/`.dll`). The C ABI is *adjacent* — it exists for non-C++ consumers, language bindings, and the out-of-process service mode, not as the primary surface.
3. **The Master Feature Catalogue** (`spec/feature-catalogue.md` in this repo) is the single coverage tracker. v1.0 cannot ship until every `OFFICIAL` row is `done` or explicitly `dropped` with user-signed rationale.
4. **No silent omissions.** Every normative section of every supported FIX spec must produce at least one catalogue row, traceable through `spec/coverage-index.md`.

---

## Article II — Language, Compilers, Platforms

1. **Language standard: C++23.** No fallback to earlier standards. Modules are not required (toolchain support is uneven); free use of concepts, coroutines, ranges, `std::expected`, `std::flat_map`, `std::pmr`, deducing `this`.
2. **Compiler matrix:**
   - **Clang** — primary development compiler, Linux. Sanitizers + fuzzing run here.
   - **GCC** — Linux CI sanity build only.
   - **MSVC** — Windows; `clang-cl` is **prohibited**.
3. **Platforms:**
   - **Linux is the primary development environment.** Day-to-day work, sanitizers, fuzzing, perf profiling, and e-book authoring all run on Linux.
   - **Windows** builds run **manual / on-demand / nightly via Tier 2 CI** (Article IX), not on every PR.
4. **No compiler-version pinning** for HALO (Heap Allocation eLision Optimization — the compiler eliding a coroutine's heap frame when its lifetime is bounded by the caller) and other optimizations (SYNTHESIS §3.2 Q6). The codebase tolerates HALO not firing on a given compiler revision; PMR fallback paths handle the gap.

---

## Article III — Build & Dependency Toolchain

1. **Build system: CMake ≥ 3.28 + Ninja.** No alternative generators in CI.
2. **Dependency manager: Conan.** Vcpkg is **prohibited**. Every external dep is declared in `conanfile.py` with a pinned version; transitive overrides go in profile files, not in code.
3. **Conan profiles** (under `conan/profiles/`):
   - `linux-clang-debug`, `linux-clang-release`
   - `linux-clang-asan`, `linux-clang-ubsan`, `linux-clang-tsan`
   - `linux-clang-coverage`
   - `linux-gcc-release`
   - `windows-msvc-debug`, `windows-msvc-release`
   - `windows-msvc-asan`
4. **CMake presets reference Conan profiles.** `tool_requires` pins `cmake` and `ninja` versions so the build is reproducible without relying on the host toolchain.
5. **`tools/` is build-only.** Code generators run during configure; no runtime dependency on tooling at user-link time.

---

## Article IV — Distribution Model

1. The C++ library is the primary public surface. It is consumed in-process by C++23 code via Conan.
2. The **C ABI** (`include/fix/c_api.h`) is the legal isolation boundary for AGPL/commercial dual licensing **and** the foundation for language bindings and the service wrapper. It is not the primary API.
3. **Python bindings** ship via SWIG over the C ABI, packaged as a CPython wheel. Linux x86_64 wheel is mandatory for v1.0; Windows wheel is best-effort via Tier 2.
4. **Service wrapper** (`service/`) is opt-in. gRPC is the control-plane transport; iceoryx2 SHM is the optional data-plane (Article XIV §3).
5. **v1.0 release artifacts are built but not published.** Conan packages and Python wheels are attached to GitHub releases; no upload to Conan Center or PyPI in v1. Publishing is gated on production-readiness and the README disclaimer being removed.

---

## Article V — License

1. **`fixpp` library:** AGPL-3.0 + commercial dual. The C ABI is the linkage isolation boundary for commercial users.
2. **E-book** (`book/` in the parent repo): CC-BY-SA 4.0.
3. **No LGPL dependencies.** Viral linkage is incompatible with the dual-license model.
4. **Vendored algorithm code** (e.g., `fixpp::sync::async_mutex` lifted from avast/asio-mutex BSL-1.0) carries upstream attribution at the file level; license compatibility is verified at vendoring time.
5. **Public repo from day one.** The README contains the disclaimer **"Work in progress — sandbox project — NOT for production use."** at the top of the file. The disclaimer must remain visible until publishing is unblocked (Article IV §5).

---

## Article VI — Spec Coverage Discipline (the 100% FIX Rule)

1. Every normative FIX spec section produces **at least one** `OFFICIAL` row in the catalogue (`feature-catalogue.md`).
2. Every OFFICIAL row's `Spec ref` uses canonical format `[DocAbbrev §X.Y.Z] Section title`. Vague refs (`§4`, "FIX spec") are a CI-linting failure.
3. Rows backed by design decisions instead of spec sections carry `[impl] description` or `[constitution] description` and are explicitly noted in `coverage-index.md` as design choices, not spec gaps.
4. **Bidirectional traceability:** `spec/coverage-index.md` maps every spec section → catalogue rows. Every new OFFICIAL row must have a coverage-index entry **before** it lands.
5. Every `/specify` artifact must include a **Normative References** section listing the exact `[DocAbbrev §X.Y.Z] Title` entries from the coverage index that inform the spec.
6. **No PR may close a `done` row without:** (a) a matching `/specify` artifact, (b) verifying tests, (c) Codex Gate B pass, (d) Gate A pass for non-trivial designs.

---

## Article VII — Testing Requirements

1. **Test framework: GoogleTest + GoogleMock** for C++ tests.
2. **Python tests: pytest** against the SWIG bindings.
3. **TDD is mandatory.** Every feature lands as red-green-refactor: failing test first, then implementation. Implementation without a preceding failing test is a Gate B blocker.
4. **No code without a test.** Untested code on `main` is a constitution violation; Codex Gate B prompts for it explicitly.
5. **Conformance corpus:** `tests/conformance/` holds the official FIX session-layer test cases (TC-001..TC-017, sourced from the **FIX Session Layer Test Cases** specification — `FIX-TC` in the coverage index) as executable scenarios. Every PR must pass them in CI.
6. **Interop:** v1.0 includes at least one interop test against an independent FIX implementation (QuickFIX) covering Logon → NewOrderSingle → ExecutionReport → Logout.
7. **Fuzzing (parser-touching modules):** libFuzzer corpus run ≥10 minutes on every PR; longer overnight runs on `main`. New parser-touching code without a fuzz harness is a Gate B blocker.
8. **Author isolation-safe tests grouped; select by ctest label, not executable name (added v0.4).** New isolation-safe test `.cpp` files (pure/stateless — read/parse/compute, single-threaded, no global allocation/OOM/singleton state) default to an existing whole-binary grouped executable in their module — `add_executable` + `add_test(NAME <bucket> COMMAND <bucket>)` + `set_tests_properties(... PROPERTIES LABELS "<label>")` — not one executable per `.cpp`. `gtest_discover_tests` is prohibited for these buckets (per-case discovery regresses serial ctest ~5–6×, measured in feature 068). Isolation-sensitive tests (global alloc/OOM injection, TSan-heterogeneous env, own `main()`/`abort()`/`_exit()`, genuine concurrency, per-`-D` variants, exact-set completeness gates, live `-R <target>` selection) stay standalone. Tests are selected by `ctest -L <label>`, never `-R <exe-name>`. Canonical pattern: `specs/068-test-binary-grouping/IMPLEMENTATION-PROCEDURE.md`, `tests/dictionary/CMakeLists.txt`.

---

## Article VIII — Performance Budgets & Benchmarks

1. **Bench framework: Google Benchmark.** Every perf-sensitive module has a benchmark in `bench/`.
2. **Regression budget: a slowdown greater than +5%** against the **merge-base of the candidate and the PR's target branch**, measured as a **paired base-vs-candidate run on one runner** — both trees built and benchmarked in the same job, A-B-A-B, compared min-per-tree. The budget is **one-sided**: it bounds slowdowns. A speed-up needs no approval.

   **+5% is the standard. It is not what automated CI presently decides.** Until the estimator characterisation in `.specify/ci209-bench-gate.md` §6a is satisfied, CI may enforce a provisional slowdown threshold that **must not exceed +50%** over an explicitly listed set of paired binaries. **A green sentinel is not evidence of +5% compliance.** That +50% ceiling is a constitutional limit on how weak CI may become — widening it requires an amendment — while the *current* threshold, paired set, sample count and promotion state are operational status maintained in that decision record, not here.

   **Automated timing coverage is partial, and this clause promises no more than it delivers.** Only an explicitly listed subset of the binaries in `bench/ci-suite.txt` receives a paired timing comparison; the remainder are execution- and schema-checked only and carry **no automated timing limit whatsoever** — a slowdown in one is caught by review or not at all. **Execution coverage is not regression-budget coverage.** Paired status is **irreversible** and the subset therefore only grows (§2a).

   **Closing that hole is an obligation with a deadline, and the deadline is on the evidence, not on the promotion.** `.specify/ci209-bench-gate.md` §6a makes a binary eligible for promotion on its A-vs-A spread across 20 `push:main` runs — but the workflow produces the A2 leg **only for rows already marked `paired`**, so an unpaired binary can never generate the evidence that would promote it. **That criterion is circular as it stands and cannot fire.** The binding requirement is therefore: a **characterisation lane that collects A-vs-A evidence for unpaired candidates must exist before the v1.0 library release**, and from the release at which it exists, every binary it shows eligible under §6a **must be paired at or before the next library release** ("release" here means a tagged library release, never a constitution version). Until that lane exists, no binary is promotable and this clause obliges building it — not pretending a promotion path is already open.

   **Checked-in `bench/baselines/` files do not gate.** They are an informational tier: each row is reported with a named reason when it is not comparable. This governs the **per-PR** budget only — **§4's v1.0 release baseline is untouched by it and remains a blocker**. A per-change budget cannot by itself bound **cumulative drift** (repeated +4.9% steps each pass); a release-anchored comparand closing that hole **is required at v1.0** and is tracked as such, not left permanently optional.

   **An accepted regression is never author-declared.** A slowdown beyond +5% requires, in the same PR: the actual paired measurement recorded, a rationale, and **approval by someone other than the change's author**, recorded as a GitHub PR approval or an explicit user statement in the PR thread — **not** a claim in the PR body or a label the author applied. Where the author is the sole maintainer and no independent approver exists, the fallback is **explicit user ratification recorded in the PR thread**, which is a distinct act from authoring the PR. Attaching a rationale is not acceptance, and §3's "a benchmark in the same PR" does not require that benchmark to *pass* — this clause does.

   2a. **Fail-closed invariants of the paired comparand.** Constitutional, not workflow detail, so that a future workflow edit cannot remove them silently. Each states what the instrument **does**, not what would be ideal; where the two differ the gap is named rather than papered over.
   - the base is the **merge-base of the candidate and the PR's target branch**, required to be distinct from the candidate;
   - a **crashed, empty or uninformative** measurement is a **failure**, never a pass; so is a **missing** measurement for any paired row **present in the merge-base's `bench/ci-suite.txt`**;
   - **exception, and the only one:** a paired row **absent from the merge-base's `bench/ci-suite.txt`** is a **candidate-only addition**. That absence must not be an error — adding a benched binary is what §3 asks for. Such a row is still **hard-gated on execution and schema** in the candidate, and is excluded from timing comparison for that PR only.
     ⚠️ **The predicate is manifest membership, not buildability, and that is a known over-exemption.** A binary the merge-base could build but never listed is exempted too — `bench/transport/*` are exactly that shape today: real CMake targets absent from the manifest. Narrowing the exemption to *genuinely unmeasurable in the base* requires a semantic target census in the base build tree, which is tracked as a follow-up against the bench gate, **not** claimed here. This clause deliberately describes the shipped predicate; a constitution that describes a better instrument than the one running is the defect this amendment exists to remove.
   - **paired status is irreversible, with no exception.** Once a row is `paired` in a merged manifest it may not be removed or downgraded — there is no approval path, and the comparator fails both unconditionally. **This includes retiring the benchmark entirely**: to the comparator, deleting a paired row *is* removing it, and no separate retirement path exists. Admitting one would reopen the resurrection hole the absolute rule closes — retire, then re-add, and the row collects the candidate-only exemption a second time. **Retiring a paired benchmark therefore requires amending this clause**, which is the intended friction: the alternative is an exemption whose only enforcement is that nobody abuses it.
     ⚠️ Named gap, not a hidden one: there is consequently **no supported way to delete a paired benchmark**. Should that become necessary, it needs comparator support (an append-only paired-history ledger, and a reintroduced identity comparing against its latest paired ancestor rather than qualifying as a fresh addition) *and* an amendment — in that order, so the rule never again promises a path the instrument does not have.

   *Why the comparand changed (v1.0).* `bench/baselines/` cannot serve as one, by census rather than by argument. Of **27 tracked files** there, 25 are JSON and 2 are `.gitkeep`. Of the 25: **3** carry zero benchmark rows, **10** have no `cpu_time` key on any row, **1** (`bench/baselines/placeholder.json`) has a row whose `cpu_time` is `null`, and **11** carry numeric `cpu_time` — an exact partition, 3 + 10 + 1 + 11 = 25. Of those 11, one (`log/log_enqueue.json`) is a debug build, leaving **10** release records; one of those (`sync/async_mutex_baselines.json`) is a hand-authored partial schema lacking `real_time`, `time_unit` and `run_type`. **Nine** usable full-schema release comparands therefore exist, for 23 benched binaries. **Five** files in total declare a debug build — `log_enqueue.json` plus four wire records — across **two different key spellings** (`library_build_type` and `build_type`), which is why a single-spelling sweep undercounts them.

   Of the five binaries the paired tier gates, **four declare `none:` in `bench/ci-suite.txt`**, and the fifth's comparand — `dictionary/xml_loader.json` — was shown by issue #263 never to have described `main`: seeded by `d526e082`, invalidated **three** commits later by `20a40f7b` inside the same PR (#66), and never re-seeded, so the ±5% budget was breached 8–16× against it for three months without detection. **A budget stated against a comparand that does not exist is *unenforceable*, not strict.**

   Paired same-runner measurement needs no stored comparand, and is the only instrument this repo has *measured* to be valid: an unpaired CI A/B read **1.02×** on a change that a paired same-VM A/B read **2.10×**.
3. **No perf change merged without a benchmark in the same PR.**
4. **v1.0 perf targets:**
   - Parser: parity-or-better with `hffix` on identical hardware (parse/sec).
   - Session throughput: parity-or-better with QuickFIX on identical hardware (messages/sec, end-to-end).
   - Latency: end-to-end session round-trip p50 and p99 measured and reported in `bench/REPORT.md`; no specific number is constitutional, but regressions vs the v1.0 baseline are blockers. **§2's "checked-in baselines do not gate" does not reach this clause** — the v1.0 release baseline is a release anchor, not a per-PR comparand, and remains blocking.
5. **Allocator policy on the hot path:** zero `new`/`delete` between parse and `fromApp` callback. Arena/PMR is the default; deviations require justification in the relevant `/plan`.
6. **Codex adversarial perf review** (v1.0 release-candidate gate) hunts for benchmark hacks, compiler optimization that elides work, and unrealistic data shapes. Findings are blockers.

---

## Article IX — Coverage, Sanitizers, Static Analysis

1. **Coverage thresholds (user-raised 2026-05-17, supersedes the prior 90/80):**
   - **Per-PR:** **≥95% line, ≥85% branch** on **touched modules** (`include/fixpp/<mod>/*`+`src/<mod>/*`, test files excluded), measured with fresh per-binary profraw (never reuse a prior/aborted build's profraw — a mismatched profraw makes `llvm-cov` silently zero a function; see the verify-procedure note). **The module glob selects what is MEASURED; the obligation in the binding rule below is scoped to the DIFF** — lines the PR adds or newly makes reachable. Pre-existing uncovered lines in a touched file are surfaced and assessed, not required to be tested by the PR that happened to touch the file. An interpretation that bills a PR for debt it did not create is an incentive not to look, and 040/061/063/064/066/075/080/081 all applied the diff-scoped reading (063 landed on `offset_table.cpp` at 89.5%/83.3%).
   - **Binding rule — no silent uncovered error/edge path.** A PR may land below raw 95/85 **only if every uncovered line/branch carries a recorded Opus risk assessment** in `.specify/decisions/<feature>-verify.md`: *genuine error/edge path* → **must be tested** (a `catch`, a `wire_*`/error return, a DoS-cap, an overflow/truncation guard is genuine by default); *defensive / unreachable / trivial-accessor / dead-under-a-cap / dead-under-the-default-trait* → **waived with a one-line rationale**. Raw ≥95/≥85 with no uncovered error path also satisfies the gate. Either way: **no uncovered error/edge path without an explicit assessment** — that is the enforced gate; the percentage is the target. **THREE dispositions satisfy it, not two:** *tested*; *waived* with a one-line rationale (asserting no test is needed); or **escalated to a filed issue** (asserting a test IS needed and naming who owes it). An escalation is not a weaker waiver and must not be collapsed into "neither is a test, therefore blocked" — the PR that first assesses a long-uncovered line discharges the obligation rather than incurring it. Raised at Gate B on PR #224 and settled in #225.
   - **Global** (once `wire/` and `session/` modules have shipped): ≥95% line.
   - Coverage is measured on Linux/Clang only (`llvm-cov` + `llvm-profdata`). Windows/MSVC builds (Tier 2) do not run a coverage step — coverage thresholds are platform-independent, and the only viable Windows tool (`OpenCppCoverage`, last release 2019) cannot reliably measure modern MSVC output.
   - **Retroactive remediation backlog (not an instant violation, not blocking):** features merged before this raise — **001-core-decimal, 002-dictionary-xml-loader, 003-dictionary-codegen** — are tracked debt. Each gets a sequenced per-feature `/simplify`→coverage pass (own branch + `/speckit-verify` + review) **after 004-wire-codec closes**; until then they remain `done` and are not re-opened as violations. ⚠️ This clause names those three features ONLY. It is not the general exemption for pre-existing uncovered code — that is the diff-scoping in the Per-PR bullet, and reading this clause as the sole exemption is what made the literal text look like a hard threshold for every other file.
2. **Sanitizers — Tier 1 (every PR, Linux/Clang):** ASan, UBSan, TSan must all run and pass.
3. **Sanitizers — Tier 2 (Windows/MSVC, manual/nightly):** ASan only. UBSan is not available under MSVC; equivalent UB coverage is provided by Linux/Clang Tier 1 (Article IX §2), since UBSan findings are language-level and platform-independent.
4. **Static analysis — Tier 1:**
   - `clang-tidy` clean against the project ruleset.
   - `clang-format` check.
   - `cppcheck` clean.
   - `include-what-you-use` clean.
5. **ABI check (from the first public release onward, Article X §7):** C ABI surface is dumped (`abidiff` Linux; structural diff Windows in CI). The first public release records the baseline, and each later release is compared against the previous tagged ABI. From that release on, breaking changes are explicit `MAJOR` bumps; before it, Article X §7 governs them. Silent breaks are a release-blocker bug.
6. **Two-tier CI** (per `opus_plan.md` Quality Gate):
   - **Tier 1 — every PR (required to merge):** Linux/Clang Debug+Release, Linux/GCC Release sanity, sanitizers, coverage, perf, static analysis, fuzz (parser-touching modules), Python pytest, catalogue consistency check.
   - **Tier 2 — Windows + ABI:** manual / nightly / on-demand. Triggered by the `windows` PR label or nightly schedule.

---

## Article X — ABI Policy

1. **The C ABI in `include/fix/c_api.h` is a versioned contract.** Every change to it is reviewed against the contract; Codex Gate A is mandatory.
2. **No C++ symbol leakage** through the C ABI. CI verifies via `nm` (Linux) and `dumpbin` (Windows): the public C ABI surface contains only `extern "C"` symbols.
3. **Decimal at the C ABI boundary:** PoD `(int64 mantissa, int8 exponent)`. C++ users get full template flexibility via `decimal_traits<T>` (per SYNTHESIS §3.1 Q5); the C ABI picks one shape and freezes it.
4. **Error reporting at the C ABI:** `fixpp_error_t` is a bounded enum with reserved range and explicit forwards-compatibility rules (per SYNTHESIS §3.5 Q19). Out-of-range values are mapped to a documented "unknown error" code on read; unknown values from old consumers are tolerated by the engine. **Operational detail (per `[2i §4.4]` / `[2i §4.5]`):** the engine's translation layer downgrades to `FIXPP_ERR_UNKNOWN` (numeric 2) on the return path based on the consumer's published ABI minor version recorded at `fixpp_engine_create` time per `[2i §4.5]` version-binding protocol; a code introduced after the consumer's minor version is mapped to `FIXPP_ERR_UNKNOWN` before return. The FROM-consumer direction stays opaque pass-through — the engine does not actively reject unknown FROM-consumer values; `FIXPP_ERR_VERSION_MISMATCH` (numeric 5) is reserved for the explicit major-version-mismatch case at engine construction (per `[2i §4.5]`), not for unknown-value-tolerance. **Stability rule:** once a numeric value is published in a tagged C ABI release (`FIXPP_C_ABI_VERSION_MAJOR == 1`), it never changes meaning; new variants append at unused numeric slots within their domain block. Audit trail via `tools/abi_history/error_codes_v1.txt` (checked-in append-only file); CI verifies no re-definitions per the abidiff check `[const §IX.5]` and the occupancy gate `tools/check_capi_occupancy.sh`. Numeric-block layout per `[2i §4.3]`.
5. **Reentrancy contract** is documented per C ABI symbol (thread-safe / single-thread / requires-session-lock). No undocumented reentrancy.
6. **ABI-affecting features trigger all four mandatory controls (Appendix A):** `/clarify`, `/analyze`, Codex Gate A, user `/plan` sign-off.
7. **Before the first public release, a breaking C-ABI change is allowed but must be declared.**
   - **First public release.** The first published GitHub Release of this repository. A draft does not count; a published pre-release does. `gh release list --exclude-drafts` shows whether it has happened. A copy of fixpp distributed any other way before then is unsupported and does not start the compatibility promise. Until that release the C ABI has no supported external consumer and its version is not a compatibility promise: the 0→1 freeze (`FIXPP_C_ABI_VERSION_MAJOR == 1`) fixes the surface's shape and its review discipline only. "A tagged C ABI release" in §4 and in Article IX §5 means the first public release or a later one. §4's append-only audit trail applies throughout: a published numeric error code is never reassigned, before that release or after it.
   - **Breaking change.** A C-ABI change after which a consumer written against the previous headers and documentation no longer compiles or links, is no longer ABI-compatible, or sees a call it could already make fail or return a result other than the one documented. Every C-ABI effect listed in `.specify/api-contract.md` §11 is a breaking change. So is a call that used to succeed and now fails, whatever the documentation said about it. Adding a symbol, a constant, or an error code in an unused slot is additive, and so is changing output the documentation leaves unspecified.
   - **Freeze statements elsewhere.** §3's frozen decimal shape, and any design document, header comment or brain page that calls a C-ABI shape frozen for `FIXPP_C_ABI_VERSION_MAJOR == 1` or calls `1.5.0` stable, state the rule from the first public release on. Before that release this clause prevails over them: such a shape may change, as a declared breaking change under this clause.

   Before the first public release, a breaking change:
   - bumps `FIXPP_C_ABI_VERSION_MINOR`, not MAJOR, so the error-code downgrade frame of §4 stays continuous;
   - is marked **BREAKING** in the documentation of each affected declaration (in the version comment of `version.h` where no declaration carries the change), in the PR description, and in the behaviors-and-limitations delta;
   - updates every in-repository consumer (the Python binding, tests, examples, interop harnesses) in the same PR;
   - remains subject to §1 review and to all four Appendix A controls.

   **At the first public release** the C-ABI version resets to `1.0.0` and every surviving error code is rebased to `introducing_minor` 0 (the procedure `version.h` describes for a breaking MAJOR). One release PR does the whole reset: every site that encodes the C-ABI version or a code's introducing minor, the tests pinned to them, and the freeze manifest. That PR also adds a check that fails when those sites disagree, unless one exists by then. A consumer built against a pre-release header is unsupported after the reset, and the engine does not refuse it, because pre-release versions and the released `1.0.0` share MAJOR 1. The reset is recorded in `CHANGELOG.md` as the start of the compatibility promise. **From then on**, any breaking change requires a MAJOR bump.

---

## Article XI — Concurrency & Coroutines

1. **C++20/23 coroutines (`asio::awaitable<T>`) are the session/transport composition primitive.** Logon flow, recv, gap-fill, TLS handshake — all coroutines.
2. **Cancellation: ASIO native cancellation slots end-to-end.** No parallel `stop_token` abstraction. `fixpp_session_close()` from the C ABI signals the cancellation slot.
3. **Awaitable mutex required in coroutine context.** `fixpp::sync::async_mutex` (own implementation, BSL-1.0 algorithm attribution to avast/asio-mutex) is the only allowed mutex shape for coroutines. **Plain `std::mutex` is banned in any header that includes `asio::awaitable<...>`.** Enforced by clang-tidy custom check or grep gate.
4. **Application threading default: per-session strand.** Users who say nothing get callbacks serialised per session, never on the I/O thread. Custom executors are opt-in (per SYNTHESIS §3.2 Q6c).
5. **Hot-path lock policy: per-session policy with hard-coded callsite caps.** Default = mutex. Spin opt-in via session config. Store-write path always uses mutex regardless of policy (SYNTHESIS §3.2 Q8).
6. **Coroutine frame allocation: HALO-first.** PMR fallback per-awaiter where HALO doesn't fire. No global compiler-version pin (Article II §4). *(Limit — see §XV.1 scope: a cross-executor offload completion frame falls back to neither HALO nor PMR; §XV.1 permits a single bounded O(1) such frame per durable-I/O op on the §XV.4 FileStore offload path.)*
7. **Threading/concurrency-affecting features trigger all four mandatory controls (Appendix A):** `/clarify`, `/analyze`, Codex Gate A, user `/plan` sign-off.

---

## Article XII — Security & TLS

1. **TLS implementation: OpenSSL on both Linux and Windows.** Schannel is **dropped** (locked decision 2026-05-06).
2. **Allowed TLS versions: 1.2 and 1.3 only.** TLS 1.0, TLS 1.1, all SSL versions are **prohibited** at compile time.
3. **Allowed cipher suites are an explicit compile-time allow-list. The engine refuses to load anything outside it (FIXS RC1 alignment).**
   - **TLS 1.3:** `TLS_AES_128_GCM_SHA256`, `TLS_AES_256_GCM_SHA384`, `TLS_CHACHA20_POLY1305_SHA256` (RFC 8446 §9.1 mandatory + recommended set).
   - **TLS 1.2:** ECDHE-(RSA\|ECDSA) with AES-128-GCM, AES-256-GCM, or ChaCha20-Poly1305; SHA-256 or SHA-384 PRF only.
   - **Key exchange groups:** X25519, secp256r1, secp384r1.
   - **Signature algorithms:** ECDSA (P-256, P-384), RSA-PSS (key size ≥ 2048 bits).

   Anything not on these four lists — including TLS 1.3 0-RTT data, static RSA key exchange, CBC-mode suites, SHA-1 signatures, and 1024-bit RSA — is rejected at compile time.
4. **Banned cryptography:** RC4, DES, 3DES, MD5, DH_anon, NULL ciphers, anonymous key exchange, export-grade ciphers. Enforced at compile time.
5. **`Session` construction requires an explicit `SecurityProfile` choice — there is no implicit default.** The profile selects the trust mode:
   - `mtls_ca` — mutual TLS with CA-chain trust on the peer cert. The recommended starting profile for v1.0 deployments.
   - `mtls_pinned` — mutual TLS with leaf-cert pinning (FIXS RC1 strict profile). Required for FIXS-conformant deployments.
   - `one_way_ca` — server-cert TLS only, CA trust; permitted for legacy interop where the counterparty does not present a client cert. Construction emits a compile-time `[[deprecated]]` diagnostic.
   - `insecure_plain_tcp` — **NO TLS** (amended v0.3, 2026-06-17). A plain-TCP byte stream with **no transport encryption, no peer authentication, and no integrity protection**. Permitted for (a) plaintext FIX over a transport secured beneath the application — colocation cross-connect, VPN/IPsec tunnel — which is a common production deployment, and (b) engine-only benchmark fairness (the `TLS off` workload rows in `benchmark-plan.md`). It is **opt-in only and MUST NOT become an implicit default**: the `unset` sentinel is still rejected at `Session::open()` (the no-implicit-default rule in this clause + N-P2-3 is unchanged), and selecting `insecure_plain_tcp` MUST surface the **compile-time `[[deprecated]]` construction-site diagnostic this section prescribes for `one_way_ca`** — announcing that transport security is OFF (applied at the session-layer `SecurityProfile::kind` enumerator the operator selects). On this profile **no TLS context is constructed**, so the TLS-mechanism rules §1–§4 (OpenSSL impl, allowed versions, cipher allow-list, banned cryptography) are inapplicable and vacuously satisfied; **§7 still applies in full** — `insecure_plain_tcp` removes *transport* encryption only and never permits the banned application-layer `EncryptMethod(98)` encryption.

   Pinset rotation (multiple valid peer certs per counterparty, FIXS §5) is supported under both `mtls_pinned` and `mtls_ca`. The TLS-mechanism rules §1–§4 are conditioned on a TLS profile being selected (`mtls_ca` / `mtls_pinned` / `one_way_ca`); they constrain TLS *when present* and do not assert that every session uses TLS — `insecure_plain_tcp` is the explicit, friction-gated non-TLS escape.
6. **Certificate pinset rotation API** is a v1.0 feature (multiple valid peer certs per counterparty, FIXS §5).
7. **`EncryptMethod(98)` ≠ 0 is rejected.** Application-layer encryption is deprecated since FIX 4.3; encryption lives at TLS only.
8. **Pluggable `cert_source` interface** with one default impl (file-based PEM/DER) in v1.0; HSM/TPM/cloud-KMS impls are user-side or future bundles (Article XIV).
9. **Security-affecting features trigger all four mandatory controls (Appendix A):** `/clarify`, `/analyze`, Codex Gate A, user `/plan` sign-off.

---

## Article XIII — Observability & Logging

1. **OpenTelemetry instrumentation from v1.0.** Traces, metrics, logs all OTLP-exportable. Prometheus + OTLP dual export is the v1.0 minimum.
2. **Async logging is mandatory.** Synchronous logging on the hot path is a banned pattern (Article XV). The in-process logger is zero-alloc producer, bounded MPSC queue, dedicated drain thread, deferred formatting. Telemetry and log queues are permitted to use `drop-oldest` under bounded-queue overflow; this exception is scoped strictly to non-business signals (logs, metrics, traces) and never applies to FIX application or session messages (Article XV §15).
3. **OTel `trace_id` / `span_id` in every log record.** Each `Session` carries a `trace_context` field; logging on the session strand reads it directly. Code paths outside session scope (e.g., listener accept, control-plane handlers) use the `co_await fixpp::current_trace_context` awaitable backed by a strand-stored context (see `architecture.md`). **[Non-normative editorial note, 2026-08-29 — no rule changed, no version bump.** The example *"listener accept"* is **no longer a structural instance** of "outside session scope": feature 023 (T010) `co_spawn`s the accept loop on the per-session strand. The **rule is unaffected** — the loop runs on a bare `asio::strand`, not a `core::session_executor` wrapper, so the awaitable still resolves the engine-level fallback, and the `thread_local` prohibition below is untouched. Only the **illustration** aged. Amending the illustration is a normative edit requiring its own Gate A pass and was deliberately not done here; see the document-level note in `.specify/2d-threading.md`.]** `thread_local` propagation of trace context is **prohibited** — coroutines may resume on a different thread than they suspended on, and a `thread_local` write made before suspension is not guaranteed visible after resume. Correlation must work at the observability backend without manual stitching.
4. **Same sink interface backs OTel log export and file/stderr sinks.** No double-write paths.
5. **Bench spike mandatory** for the in-house logger vs `quill` before locking the implementation choice.

---

## Article XIV — Pluggable Interfaces

1. **The following are pluggable, each with one default impl in v1.0:**
   - **Transport** (default: ASIO TCP/TLS over OpenSSL).
   - **Control plane** (default: gRPC over Unix socket / named pipe).
   - **Cert source** (default: file-based PEM/DER).
   - **Logger sinks** (default: in-process async logger + OTLP exporter).
   - **MessageStore** (default: in-memory; file-based impl also v1.0).
2. **Interface surfaces are small.** Each pluggable interface defines **≤5 pure-virtual methods**. Bigger surfaces are permitted only with an explicit design-doc justification (one paragraph naming the necessary methods and why each is irreducible). The justification is reviewed at Gate A.
3. **Data-plane SHM via iceoryx2** is opt-in for sidecar mode. The control plane (gRPC) works without it.
4. **Plugin discovery is compile-time only in v1.0.** Dynamic plugin loading (`dlopen`) is post-1.0.

---

## Article XV — Banned Patterns

The following patterns are **prohibited** in `fixpp` source code. Each is rooted in a real failure mode observed in surveyed implementations.

1. **Heap-allocate per message or per field on the hot path.** Use zero-copy views; arena/PMR for the rare materialise cases.
   **Scope & §XV.4 exemption (amended v0.2, 2026-06-13).** The "hot path" of this ban is the latency-critical **in-memory** path — parse → validate → dispatch and `MemoryStore`, which MUST stay zero-allocation per message. The **durable-store async-journal offload mandated by §XV.4** (FileStore offloading `pwrite`/`fdatasync`/`rename` to a `file_io_executor`) is exempt to **a single bounded O(1) coroutine frame per offloaded I/O op**: a genuine cross-executor offload must `co_await` its completion inside an `asio::awaitable`, and that completion frame is routable to **neither HALO** (cannot fire across executors) **nor a PMR arena** (the Asio awaitable frame is opaque to the bound allocator) — so it is unavoidably one global-heap frame per op. The exemption is **strictly scoped**: O(1) frames/op only (the compliant fix is a *reduction* — 1 frame/op vs the prior inert offload's 4); it does **not** permit per-field, unbounded/growing, or in-memory-path allocation, nor any allocation on `MemoryStore::store`. This harmonises §XV.1 with §XV.4 and §XI.6 (whose "PMR fallback per-awaiter" presumes a fallback that does not exist for this cross-executor frame). Empirical basis: Gate-A probes `research/G19-fix-fpml-iso20022/research/probes/cospawn_probe*.cpp` (feature 035).
2. **Thread-per-session blocking I/O.** Use ASIO async I/O; multiplex N sessions onto M executor threads.
3. **Coarse global session lock.** Per-session state; lock-free queue between I/O and app thread.
4. **Synchronous disk I/O on every send** (e.g., QuickFIX `FileStore` flush per write). Async journal with background flush; sync-on-failover is opt-in.
5. **Synchronous logging on the hot path.** Async logger only (Article XIII §2).
6. **Runtime-only field validation.** Constexpr field metadata + typed accessors generated from the dictionary; misuse fails to compile, not at runtime.
7. **Forward-only field iteration with linear find** as the only access mode. Offset table is mandatory for typed/random-access path.
8. **`std::multimap` (or any other cache-hostile map) for field storage.** Vector + offset table; SBO for the common case.
9. **`std::mutex` in coroutine context.** Use `fixpp::sync::async_mutex` (Article XI §3).
10. **Application-layer encryption** (`EncryptMethod(98)` ≠ 0). TLS only (Article XII §7).
11. **TLS 1.0 / 1.1 / SSL / RC4 / DES / MD5 / DH_anon / anonymous KX / NULL ciphers / export-grade ciphers.** Compile-time allow-list refusal (Article XII §4).
12. **LGPL dependencies.** Viral linkage is incompatible with dual licensing.
13. **Eager codegen with no runtime dictionary path.** Hybrid mandated: codegen for standard fields (D-008), runtime XML loader for custom (D-007 + D-009).
14. **FAST / SBE / FIXP / SOFH shoehorned into v1.0.** Roadmap-locked to post-1.0 (Article XVIII).
15. **Application-layer message drops on slow consumer.** Backpressure-aware dispatch with two configurable modes: `block` (push back to the producer) or `disconnect-and-recover` (terminate the session and rely on FIX `ResendRequest` semantics on reconnect). `drop-oldest` is **never** permitted on the application or session message path — silent loss desynchronises the sequence-number contract. Telemetry and log queues may use `drop-oldest` under the rules in Article XIII §2.
16. **Custom XML config format** incompatible with the QuickFIX `[DEFAULT]` / `[SESSION]` CFG format. We accept QuickFIX CFG verbatim; TOML is also accepted; new formats require justification.
17. **Vendored OSS based on `master`/`main` when a release tag is older.** Read from the last release tag unless `master` represents a justified upstream improvement (SYNTHESIS §2.1).
18. **Research / decision content** (`research/`, `decisions/`, `book/`) committed into the `fixpp` repo. The `.github/workflows/no-research.yml` guard rejects it.

Each entry is a CI-enforced rule wherever feasible (Article IX §4 covers static analysis; Article XV.18 has its own guard workflow).

---

## Article XVI — Spec Kit Workflow Rules

1. **Full Spec Kit command set, each invoked in a clean context:** `/constitution`, `/specify`, `/clarify`, `/plan`, `/tasks`, `/analyze`, `/checklist`, `/taskstoissues`, `/implement`, `/simplify`.
2. **Clean-context rule:** every Spec Kit command runs as a fresh subagent invocation that loads only the artifacts it needs. No cross-phase context bleeding.
3. **`/clarify` is MANDATORY before `/plan` for any feature that touches:** ABI, threading, error semantics, wire format, codegen, session FSM, or security. (Same trigger set as Codex Gate A — Article XVII.)
4. **`/analyze` is MANDATORY** for the same trigger set as `/clarify`. Drift between constitution ↔ spec ↔ plan ↔ tasks is caught here, before `/implement`.
5. **`/checklist` output is part of CI evidence.** Checklists tied to NFRs and acceptance criteria become the e-book's "how to verify" appendix.
6. **`/implement` is one task at a time, TDD red-green-refactor.** A clean-context implementer sub-agent executes; the orchestrator (the main session) reviews increments and does not implement. *Implement* means authoring or changing production code, tests, scripts, build/CI/configuration files, or generated files committed to the repository (including running the regeneration that produces them), and resolving merge conflicts in any of those; code is classified by what a file is, not by the directory it sits in. The orchestrator MAY edit documentation, specs and design documents (a feature bundle's `specs/<id>/contracts/` headers are design artifacts), SecondBrain pages, catalogue rows, gate/decision records, and governance texts (agent, skill and command definitions); produce transient outputs that are not committed — configuring, building and running tests and checks, refreshing code indexes, benchmark and comparison runs; commit edits a fixer authored but could not commit; make empty flag commits; push, open PRs, bump the parent repository's gitlink, and merge the base branch when it merges without conflict; and run mutation proofs in a scratch copy of the tree, never in the PR's worktree. Which model runs the implementer is configuration (the agent definition), not a constitutional term.
7. **`/simplify` runs on the implementation diff before `/speckit-verify`** (pipeline step 11, before step 12 — see `.specify/pipeline.md`). Code-reuse, quality, efficiency findings reviewed by 3 specialized review agents, then triaged by the orchestrator; the implementer sub-agent applies the genuine in-scope simplifications + any real Gate-B-relevant defect; behavioral/perf redesigns + ambiguous items deferred as tracked follow-ups in the verify decision doc. **Rationale:** a post-`/simplify` source change invalidates every preset build dir, forcing the full 6-preset `/speckit-verify` matrix to re-run — so `/simplify` must precede verify, not merely precede PR open.
8. **Stuck loop:** three failed `/implement` invocations on the same red test (each invocation is a fresh-context attempt at one TDD cycle, per §1 and §6) → escalate to Codex as fallback implementer; if still stuck, `AskUserQuestion`. Codex's PR review for that task must come from a **fresh** Codex session, not the one that wrote the code (independence between author and reviewer is non-negotiable).

---

## Article XVII — Codex Review Gates

1. **Gate A — Design review (pre-implementation, non-trivial designs).** Triggers (any one):
   - Touches the public C++ API or C ABI.
   - Touches concurrency / threading / cancellation / executor model.
   - Touches the wire format, parser, or codegen layout.
   - Touches the session FSM, recovery, or message store contract.
   - Touches the security surface (TLS, cert handling, PSK).
   - Any new design document under `.specify/` (`architecture.md` and sibling design docs) — qualifies by default.

   Trivial features (rename a private helper, add a P2 boilerplate row over an existing module) skip Gate A. **When in doubt, run it.** Blockers from Gate A must be resolved or explicitly waived with rationale before `/tasks` runs.

2. **Gate B — PR review (post-implementation, every PR before merge).** Mandatory regardless of feature size. High-severity findings resolved or waived with rationale in the PR description before merge.

3. **Independence rule.** Codex's review is independent of the implementer. When Codex implements (escalation), Codex's PR review for that PR comes from a separate Codex session.

4. **User invokes Codex.** No Claude agent starts Codex on its own initiative: a user's `/gate-a` or `/gate-b` invocation authorizes that gate's bounded Codex calls, and the §XVI.8 fallback runs only once the user confirms it; the gates are user-driven (`codex:codex-rescue` agent or local Codex CLI). The PR description links to the Gate A outcome and the Gate B outcome.

5. **Findings triage:** Opus triages; the active fixer applies accepted items — the non-orchestrator actor the applicable procedure assigns: the implementer sub-agent, Gate A's rewrite agent for design documents, or Codex where §XVI.8 or the Gate B fixer hand-off applies — never the orchestrator (§XVI.6); user signs off feature completion at `/specify` boundaries and at module close.

6. **CI enforcement.** The `.github/workflows/gate-a.yml` workflow inspects every PR's changed-file set against the Appendix A trigger paths (path globs are owned by the workflow itself, not the constitution). If any trigger path is touched, the workflow blocks merge unless the PR carries either a `gate-a-done` label (Codex Gate A passed) or a `gate-a-waived` label with mandatory rationale in the PR body. Trivial diffs auto-waive: comment-only edits, doc fixes, single-line whitespace, dependency-pin bumps without code changes.

7. **Local pre-PR build gate (mandatory, all PRs).** Before opening any PR, the contributor MUST run a local Conan install + CMake configure + build + ctest cycle on at least the `linux-clang-debug` preset, and `pytest bindings/python/tests/` if the change touches `bindings/python/`. The PR description must include a one-line confirmation (`local build: green on linux-clang-debug @ <git-sha>`). PRs without that line, or with a known-red local build, are rejected at review.
   - **Resource gate:** local builds are resource-heavy (Conan fetches + full compile + sanitizer rebuilds). When an AI agent needs to run the local build, it MUST surface an `AskUserQuestion` first; the user approves the build before it runs. The agent never auto-runs `conan install` / `cmake --build` without explicit approval.
   - **All dev work happens locally.** Contributors do not push speculative commits to remote branches "to see what CI says" as a substitute for local testing. CI is verification of green local work, not a remote test runner.
   - **Local toolchain target: Clang 22** (matches the user's local install and the Conan profile pin per Article II §2 / Article III §3). CI provisions Clang 22 via `apt.llvm.org` so local==CI.

8. **Verification gate (`/speckit-verify`) — required after every `/speckit-implement`.** `/speckit-implement` marks `tasks.md` rows `[X]` on agent confidence, not on evidence. "Run X command and verify Y threshold" tasks (sanitizer presets, coverage gate, static analysis, ABI hygiene, allocation discipline, fuzz smoke, bench regression, abidiff golden) routinely get marked complete without ever firing; the first real run then happens in CI on the open PR, which fails late and costs a Gate B round. The `/speckit-verify` command (in `.claude/commands/speckit-verify.md`) is the local Tier-1 mirror that actually executes each polish task serially against artifacts and writes a decision record at `.specify/decisions/<feature>-verify.md`.
   - **Mandatory after `/speckit-implement`.** A `/speckit-implement` run is not considered complete until `/speckit-verify <feature>` has produced a decision record. The record's verdict is `GREEN` (all PASS or SKIPPED-with-reason), `YELLOW` (every FAIL paired with a `--waive=<task-id>:<rationale>` rationale), or `RED` (at least one unwaived FAIL).
   - **Label evidence rule.** The CI-enforced `gate-a-done` / `gate-a-waived` labels (§6) and the analogous `gate-b-done` / `gate-b-waived` labels (consumed by the planned `gate-b.yml` workflow) may only be applied with paired evidence:
     - `gate-{a,b}-done` requires `/speckit-verify` `GREEN` **and** a corresponding Codex convergence record (`.specify/decisions/<feature>-gate{a,b}.md`).
     - `gate-{a,b}-waived` requires `/speckit-verify` `YELLOW` (or `GREEN` with explicit Codex-side waivers) **and** waiver rationales recorded both in the verify record and the PR body.
     - Applying gate labels by hand without these records is a constitutional violation — the labels are evidence claims, not status decorations.
   - **`/gate-b` precondition.** `/gate-b` pre-flight reads `.specify/decisions/<feature>-verify.md` and refuses to start the Codex review loop if absent or `RED`. `YELLOW` is accepted but carries waiver context forward into the Codex brief. This makes the verification gate enforceable without an `extensions.yml` hook: the only way to apply gate labels is through `/gate-b`, and `/gate-b` cannot run without verify evidence.
   - **Serial preset matrix.** `/speckit-verify` builds the Tier-1 preset matrix one configuration at a time — never in parallel — to keep failures isolable and avoid resource contention with the rest of the contributor's machine.

---

## Article XVIII — Roadmap Discipline

1. **v1.0 scope is locked:** FIX 4.0–5.0SP2 + FIXT.1.1 session + application layers, FIXS over TLS, the C ABI, Python bindings, the gRPC service wrapper, iceoryx2 SHM data plane (opt-in).
2. **Post-1.0 roadmap (locked):**
   - **v1.1 — SOFH** (Simple Open Framing Header).
   - **v1.2 — FIX Latest application messages** (new MsgTypes A-035..A-065 + EP-level field additions to existing messages, per coverage-index Post-1.0 Gap Registry). *(Annotation, 076 v0.8 / 077 v0.9: the A-035..A-065 typed **read/reify/args/validator** classes are delivered under `fixpp::vlatest` in v1.0 by feature 076, and their typed `build_<Msg>` **builders** by feature 077; only the EP-level field back-port into the legacy dictionaries and ApplExtID(1156)=303 on-wire differentiation remain post-1.0.)*
   - **v1.3 — SBE** (Simple Binary Encoding).
   - **v1.4 — FIXP** (FIX Performance Session Layer).
   - **v1.5 — FAST** (FIX Adapted for STreaming).
   - **v1.6 — JSON** (FIX JSON encoding).
   - **v1.7 — GPB** (Google Protocol Buffers FIX encoding).
   - **v1.8 — FIX MMT** (Market Model Typology).
3. **Permanently dropped:** FIXML (XML representation; superseded), FIXatdl (UI/display spec, not a wire protocol).
4. **Roadmap changes are constitution amendments.** Re-ordering, additions, removals all require Article XX.
5. **No early shipping** of post-1.0 protocols into v1.0 to "get them done." Each shipping target is its own Spec Kit cycle, gated by the same Tier 1 quality bar.
6. **Post-v1.0 codegen for runtime-XML-only versions.** FIX 4.0, FIX 4.1, FIX 4.3, FIX 5.0, and FIX 5.0 SP1 ship in v1.0 with runtime-XML support only (no per-version codegen namespace). Per-version codegen for these versions is post-v1.0 best-effort, prioritised at the discretion of the maintainer team based on observed downstream demand. The recommended priority order is: FIX 4.3 first (most-used legacy version in the post-v1 backlog), 5.0 SP1 second, 5.0 third, 4.0 / 4.1 last (vanishingly few production deployments). Each version's codegen is its own minor-version Spec Kit cycle, gated by the same Tier 1 quality bar.
7. **Application-message codegen scope for v1.0.** v1.0's typed-message scope under `fixpp::v42`, `fixpp::v44`, `fixpp::v50sp2` is A-001..A-013 plus the M-/P-/C-/R-/N- families per the catalogue. **Under `fixpp::v44`, the full `msgcat='app'` set (83 in-scope messages = 85 app minus the N-002/N-003 pair BE/BF) is DELIVERED by feature 069** — the previously-deferred FIX44 application rows A-014, A-015, A-016/017/020, A-019, A-021 (`AH`/`AI`/`AJ`), A-025 (`v`/`w`/`x`/`y`), the FIX44 members of A-022 (`AW`) and A-026 (`z`, `AA`), the Collateral (C-001), Position (C-002: `AL`/`AM`/`AN`/`AO`/`AP`), Registration/indication (R-), Network-status (N-001: `BC`/`BD`), and post-trade (P-) families now carry typed `build_/validate_/Args` builders. **The delivered set is the full `msgcat='app'` scope (83) — the family enumeration here is illustrative, not the operative bound.** **The FIX50-only rows whose only MsgTypes are absent from FIX44 — A-018 (BN), A-023 (BZ/CA), A-027 (BI/BJ/BS), A-028 (BT/BU/BV), A-029 (BK/BP), A-030 (BQ), A-031 (BM), A-032 (CB), A-033 (CC/CD/CE), C-003 (CQ) — plus the mixed-row FIX50-only siblings BO (A-022), BR (A-026), BL (C-002) are NOT delivered by 069**; they carry no FIX44 message and remain deferred to future `fixpp::v50sp2`/all-version widening. **XMLnonFIX (A-034, 35=n) is NOT delivered** — it is `msgcat='admin'`, outside the application-writer emitter; runtime-XML access via `view.get(uint16_t tag)` remains its only path. A-024 stays dropped as a duplicate per `[SYN §4.4]`. The N-002/N-003 session-FSM pair (BE UserRequest / BF UserResponse) remains deferred to the separate v1.0-tagging gate. **`fixpp::v50sp2` application-message widening is DELIVERED by feature 077** — v50sp2 (156 `is_application` messages; no v44 `{BE,BF,BW,BX,BY}` exclusion inherited — BW/BX/BY are genuine FIX 5.0 SP2 application messages) now carries typed `build_/validate_/Args` builders, joined by the re-enabled `fixpp::vlatest` builder tier (173) and v44 (already shipped, now deduped); vt11 is admin-only and emits none. These three builder-bearing versions flow through feature 077's single version-agnostic structural-plan Args-dedup emitter (each group's Args emitted once per distinct `(no_tag, recursive signature)` plan into `fixpp::<ns>::groups`), proven per-version complete by a non-circular raw-XML/Orchestra census (FR-010). *(Annotation, feature 078 / v0.10: this tier is packaged as precompiled per-version builder/validator STATIC libraries + a slim declaration surface; a consumer's opt-in is purely link-time. Layout restructure only — wire byte-identical, zero core change.)* **`fixpp::v42` application-message builders are DELIVERED by feature 082** *(v0.11, 2026-08-12 — reclassified from DEFERRED; PR #261, closes issue #196)*. The blocker this clause recorded was real and is now removed at its root: repeating-group detection no longer keys on the count field's declared XML datatype but on the `<group>` element itself, so FIX 4.2's legacy `INT`-typed count fields materialize typed repeating groups like any other version (**18** groups; FIX 4.0 gains 4, FIX 4.1 gains 7, FIX 4.3 gains 1). `fixpp::v42` therefore carries the full typed `build_/validate_/Args` tier through the same 077 structural-plan Args-dedup emitter as v44 / v50sp2 / vlatest, packaged per 078 as a precompiled per-version library. **The Article VI silent-omission hazard that justified the descope is closed by direct test, not by construction** — all **14** required-repeating-group omissions across the v42 set are rejected by `writer_traits<Args>::group_checks`, so a scalar-only build of e.g. `NewOrderList`/`NoOrders` now fails validation instead of emitting invalid FIX 4.2. 077's FR-009 read-byte-identity objection resolved rather than being overridden: the v42 read golden **was** regenerated, and the other **14** read-tier artifacts were proven bit-identical. **`fixpp::vt11` emits no builders** — unchanged, admin-only by policy, and not a residual. Runtime-XML access to any not-yet-typed application message via `view.get(uint16_t tag)` continues to ship across all 9 supported FIX versions.

---

## Article XIX — Documentation

1. **Library docs:** Doxygen → mdBook bridge under `docs/`. Hand-written getting-started, session cookbook, custom dictionary how-to, C ABI usage, Python tutorial, perf tuning, troubleshooting.
2. **E-book:** mdBook + mdbook-pdf under `book/` in the parent repo. CC-BY-SA 4.0 (Article V §2).
3. **Every code example in either artifact is a runnable file in `examples/` exercised by CI.** Stale examples are a documentation bug.
4. **Cross-link to source** at pinned tags, not at `main`, so references survive history rewrites.
5. **Pages tied to public API surfaces** must be regenerated when the surface changes. Doxygen output drift is a Gate B finding.

---

## Article XX — Amendments

1. **The constitution is amendable but not silently violatable.** Any conflict between this document and a feature spec must be resolved by amending the article first (with rationale committed in the same PR), then proceeding.
2. **Amendment process:**
   - Open a PR titled `Constitution: amend §<article>.<number> — <summary>`.
   - PR description states the change, the rationale, and which catalogue rows / specs are affected.
   - Codex Gate A review on every amendment.
   - User signs off.
3. **From v2.0 on, every amendment is recorded in a Sync Impact Report** at the top of this document: its date, the version bump and its §4 classification, the clauses changed, the rationale, the affected catalogue rows and specs, the review and sign-off that ratified it, and the PR or feature it rode. Reports written before v2.0 stay as written. A backwards-incompatible amendment is also entered in `CHANGELOG.md` (§4). Feature-level decisions are recorded in the feature's own spec bundle, not here.
4. **Backwards-incompatible amendments** (banned-pattern additions, perf-budget tightening) require a v-major bump and an entry in `CHANGELOG.md`.
5. **Cross-cutting hand-off rules** (e.g., "the implementer asks before every `git push`", "sudo confirmation required") are amendments to this constitution; they live here, not in ad-hoc memory or `CLAUDE.md` carve-outs. *No such rules defined as of v0.1; future rules of this shape are added below as numbered sub-clauses (5.a, 5.b, …) under this article via the standard amendment process (§2).*

---

## Appendix A — Mandatory triggers reference

**This appendix is the canonical mandatory-trigger reference.** Article-level trigger clauses must match it; conflicts are resolved in favour of this table (Article XX). The following features trigger **all four** mandatory controls: `/clarify`, `/analyze`, Codex Gate A, user `/plan` sign-off.

| Trigger | Examples |
|---|---|
| ABI surface change | new C ABI symbol, signature change, error-code addition |
| Threading / concurrency | new awaitable, new strand discipline, lock policy change |
| Error semantics | new `fixpp_error_t` value, exception-vs-`expected` change |
| Wire format / parser | offset-table semantics, framing rules, validator changes |
| Codegen layout | dictionary loader, multi-version coexistence |
| Session FSM | state additions, recovery semantics, gap-fill rules |
| Security | TLS config, cipher allow-list, cert-source plug-in |

Trivial features (P2 boilerplate over an existing P0 module, doc-only) skip all four. **When in doubt, run them.**

---

## Appendix B — Cross-references

- **`opus_plan.md`** (parent repo) — phase plan, owns the Codex gate workflow descriptions, owns the Quality Gate Tier 1/Tier 2 split.
- **`SYNTHESIS.md`** (parent repo) — Phase 1.5 output; the decisions encoded here are sourced from §1, §2, §3, §5.
- **`spec/coverage-index.md`** (this repo) — bidirectional spec ↔ catalogue traceability index. Article VI §4 binds `/specify` to it.
- **`spec/feature-catalogue.md`** (this repo) — the 100% FIX tracker. Article VI §1 binds it to the spec.
- **`architecture.md`** (this directory, drafted next) — module layering, public namespaces, design patterns. Implements the rules; this constitution sets them.
