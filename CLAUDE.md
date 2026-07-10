<!-- SPECKIT START -->
**FEATURE IN FLIGHT: `068-test-binary-grouping`** (test-infra only — no src/ABI/runtime change). Group isolation-safe test `.cpp` into fewer `gtest_discover_tests` binaries, bucketed by (link-deps ∩ label), to cut the 66.9G test-binary matrix (462 one-exe-per-.cpp × 8 presets). Pilot=`dictionary` (3-metric harness), then `session`→interop→capi→… one module at a time. Preserve coverage-index + completeness audits (key on `.cpp` stem + gtest `Suite.Name` — unaffected) + `ctest -L`/`-R` selectability; CI ctest wall-time ≤10%/preset. Gate A **not triggered** (no trigger path) → `gate-a-waived` candidate; Gate B mandatory. Plan: `specs/068-test-binary-grouping/plan.md` (spec+clarify+plan done → Gate A/tasks next). Baseline: `research/test-grouping-baseline/`.

Last merged: **`067-codegen-writer-emitter`** MERGED 2026-07-10 (PR #185, squash `84833c8d`; gate-a-done + gate-b-done). FR-015a-lite codegen writer-emitter: `build_<Msg>` + `validate_<Msg>` for all 33 OFFICIAL FIX44 MsgTypes over `wire::body_builder`; v44 representative ns. No runtime/C-ABI/Python change (FR-009). L-067-1/2/3.

Prior merge: **`065-cabi-nested-group-membership`** — PR #182 (squash `0e3589e5`), 2026-07-10, gate-a-done + gate-b-done, 0 waivers; full 3-tier CI green. **Fixes #179 / L-063-2** (RESOLVED): the C-ABI **nested** repeating-group read deleted its hand-rolled positional scanner and delegates to the membership-aware `OffsetTable::nested_group_slices` (062) + `consume_group_extent` (063) — a trailing outer member now reads `TAG_NOT_FOUND`, not `OK`+wrong-value; C-ABI 1.5.0 freeze byte-identical. Deferred: **L-065-1/#183** (typed depth-≥2 unpushed-context gap) · **L-065-2/#184** (nested-read arena-exhaustion fail-loud). See `phases/phase-4/c-api/065-cabi-nested-group-membership.md` + [[project_065_cabi_nested_group_membership]].

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
