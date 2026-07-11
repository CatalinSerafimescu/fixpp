<!-- SPECKIT START -->
**Last merged: `068-test-binary-grouping`** MERGED 2026-07-11 (PR #186, squash `ef9ca2bd`; `gate-a-waived` + `gate-b-done`; full `push:main` 3-tier + ABI-golden green). Whole-binary `add_test` grouping across all 23 test modules (FR-001 amended from the initially-spiked `gtest_discover_tests`, which regressed serial ctest 5.77×) — test-infra only (no src/ABI/runtime change), 454→292 test binaries (~15GB reclaimed across 6 presets); **3085 gtest cases preserved exactly** (0 dropped, verified clean before/after set-diff). 8-preset Linux + MSVC verified. Constitution **v0.4** (Article VII §8 — author tests grouped, select by `ctest -L`, never exe name). See `specs/068-test-binary-grouping/{dispositions,measurements}.md` + [[project_test_binary_grouping_disk_win]].

Prior merge: **`067-codegen-writer-emitter`** MERGED 2026-07-10 (PR #185, squash `84833c8d`; gate-a-done + gate-b-done). FR-015a-lite codegen writer-emitter: `build_<Msg>` + `validate_<Msg>` for all 33 OFFICIAL FIX44 MsgTypes over `wire::body_builder`; v44 representative ns. No runtime/C-ABI/Python change (FR-009). L-067-1/2/3.

Earlier: **`065-cabi-nested-group-membership`** — PR #182 (squash `0e3589e5`), 2026-07-10, gate-a-done + gate-b-done, 0 waivers; full 3-tier CI green. **Fixes #179 / L-063-2** (RESOLVED): the C-ABI **nested** repeating-group read deleted its hand-rolled positional scanner and delegates to the membership-aware `OffsetTable::nested_group_slices` (062) + `consume_group_extent` (063) — a trailing outer member now reads `TAG_NOT_FOUND`, not `OK`+wrong-value; C-ABI 1.5.0 freeze byte-identical. Deferred: **L-065-1/#183** (typed depth-≥2 unpushed-context gap) · **L-065-2/#184** (nested-read arena-exhaustion fail-loud). See `phases/phase-4/c-api/065-cabi-nested-group-membership.md` + [[project_065_cabi_nested_group_membership]].

**In flight: `069-v44-all-families`** — widen the 067 writer-emitter from 33 OFFICIAL to ALL v44 application messages (81 in-scope = `msgcat='app'` minus N-002/N-003), write-side only (read/reify already universal), CMake `FIXPP_CODEGEN_V44_FAMILIES=all|official` (default all), differential round-trip at breadth. Pre-Orchestra "close the legacy typed chapter" step; folds an **Article XVIII §7 amendment** (v1.x-scoped codegen pre-tag; user-approved 2026-07-11). Plan: `specs/069-v44-all-families/plan.md` (+ research/data-model/contracts/quickstart). Gate A next. [[project_v44_all_families_crank_before_orchestra]]

**Next candidates:** **#180** (harden dictionary census — parent/child scalar-member disjointness; closes L-062-3 + L-063-4) · **Orchestra/FIX-Latest** ([[project_orchestra_fix_latest_direction]]). C-ABI GA-frozen at `1.5.0`; Python PY-001..005 COMPLETE.

**This file is a THIN POINTER — do not paste per-feature history here.** The authoritative records:
- **Merged-feature changelog (newest first):** [`CLAUDE-history.md`](./CLAUDE-history.md).
- **Per-feature status + evidence:** `spec/feature-catalogue.md` + `spec/coverage-index.md`.
- **Per-feature Gate A/B convergence + sign-off:** parent `research/G19-fix-fpml-iso20022/phases/phase-4/<module>/<feature>.md` (+ `phases/phase-4.md` dashboard, `phases/phase-4/cross-module-decisions.md`).
- **Behaviours & limitations (operator-facing):** `spec/behaviors-and-limitations.md` (B-* / L-* rows).
- **Cross-session state + lessons:** project memory `project_*` / `feedback_*` (index `MEMORY.md`; older close-outs `MEMORY-archive.md`).

**CI procedure (per [[project_ci_run_tier_labels]]):** branch protection REQUIRES `tier{1,2,3}-required` + `Gate A`/`Gate B` labels; all three tiers run on `pull_request`, gated on `(gate-a-done ∧ gate-b-done)`. Turn a PR green by satisfying both gates via `/gate-a` + `/gate-b` — the gate-label event co-fires all three matrices in pull_request context. Known exception: stale duplicate `tierN-required` check-runs (`[FAILURE,…,SUCCESS]`) can wedge the merge box at latest-green → merge `--admin` (PRs #177, #182; `required_approving_review_count` is 0). `push:main` re-runs all three post-merge (badge + gate).

**Live deferred work:** session-recovery catalogue row 400 (010 F4 ResendRequest/SequenceReset Reject→Process — largely shipped via 013 + S-023; residual = v1.0 traceability confirm) · live-interop golden-capture + C-103 chunked-resend → G4 ([[project_release_interop_quickfix_fix8]]).
<!-- SPECKIT END -->
