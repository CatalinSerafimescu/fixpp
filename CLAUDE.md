<!-- SPECKIT START -->
**Last merged: `081-strict-validation-residuals`** (PR #207, squash `710e658e`) — see `phases/phase-4/wire/081-strict-validation-residuals.md`. Full merged-feature changelog: [`CLAUDE-history.md`](./CLAUDE-history.md).

**Next candidates:** **#196** — the **sole open issue** (re-instate v42 typed builders — relax `INT`-typed `NumInGroup` group detection to structural, closes L-063-1/L-061-1/L-066-1; the "do AFTER #198" ordering constraint is now discharged, so v42 emits straight into 078's split layout) · **ApplExtID(1156)=303** differentiation + `version_registry` re-keying (deferred by 074's L-074-1, no issue filed). Orchestra read/dictionary tier DONE via 074 + runtime-load entry point via 080; typed **read** tier DONE via 076; typed **builder** tier DONE via 077 + split into per-version libs via 078 (**closed #198**); dictionary census hardening DONE via 072 (**closed #180**); live-wire validation DONE via 075, required-presence scope via 079, strict-validation residuals via 081 (**closed #203 + #205**). C-ABI GA-frozen at `1.5.0`; Python PY-001..005 COMPLETE.

**This file is a THIN POINTER — do not paste per-feature history here.** The authoritative records:
- **Merged-feature changelog (newest first):** [`CLAUDE-history.md`](./CLAUDE-history.md).
- **Per-feature status + evidence:** `spec/feature-catalogue.md` + `spec/coverage-index.md`.
- **Per-feature Gate A/B convergence + sign-off:** parent `research/G19-fix-fpml-iso20022/phases/phase-4/<module>/<feature>.md` (+ `phases/phase-4.md` dashboard, `phases/phase-4/cross-module-decisions.md`).
- **Behaviours & limitations (operator-facing):** `spec/behaviors-and-limitations.md` (B-* / L-* rows).
- **Cross-session state + lessons:** project memory `project_*` / `feedback_*` (index `MEMORY.md`; older close-outs `MEMORY-archive.md`).

**CI procedure (per [[project_ci_run_tier_labels]]):** branch protection REQUIRES `tier{1,2,3}-required` + `Gate A`/`Gate B` labels; all three tiers run on `pull_request`, gated on `(gate-a-done ∧ gate-b-done)`. Turn a PR green by satisfying both gates via `/gate-a` + `/gate-b` — the gate-label event co-fires all three matrices in pull_request context. Known exception: stale duplicate `tierN-required` check-runs (`[FAILURE,…,SUCCESS]`) can wedge the merge box at latest-green → merge `--admin` (PRs #177, #182; `required_approving_review_count` is 0). `push:main` re-runs all three post-merge (badge + gate).

**Live deferred work:** session-recovery catalogue row 400 (010 F4 ResendRequest/SequenceReset Reject→Process — largely shipped via 013 + S-023; residual = v1.0 traceability confirm) · live-interop golden-capture + C-103 chunked-resend → G4 ([[project_release_interop_quickfix_fix8]]).
<!-- SPECKIT END -->
