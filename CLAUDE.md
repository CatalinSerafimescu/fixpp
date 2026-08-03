<!-- SPECKIT START -->
**Last merged: `083-group-delimiter-resolution`** (PR #216, squash `1b9356bd`) — see `phases/phase-4/wire/083-group-delimiter-resolution.md`. Full merged-feature changelog: [`CLAUDE-history.md`](./CLAUDE-history.md).

**Open issues: 9 post-merge of 085** (derived 2026-08-03 via `gh issue list --state open`, which returned **9**: 196, 209, 211, 213, **214**, 215, 217, 218, 220; **#221** was then filed by 085's /speckit-verify, making 10 — closing **#214** with 085 leaves **9**. The previous revision of this line said "6" and predated #217/#218/#220; **derive this count, never copy it** — it has now gone stale twice):
**#196** v42 typed builder tier (blocked on L-063-1 structural group detection — that is what in-flight `082-structural-group-detection` delivers) · **#215** 083 `/simplify` follow-ups (duplicate `as_table_view` per session + 4 cleanups) · **#213** no fuzz corpus replayed in CI (~198 seeds, zero ctest registrations) · **#211** FileSink backpressure/drop untested · **#209** `compile_time_bench` flat 3s ceiling is not a meaningful gate · **#217** packaging needs a libc++ tier (standard-library dimension in the artifact name first) · **#218** C-ABI include isolation not delivered by the installed package (`fixpp::capi` exposes the whole `include/` tree) · **#220** dict-free per-instance cap counts trailing top-level fields → spurious `wire_group_too_large` under a tightened `Config` (filed by 085; recorded as L-085-1, unreachable under default configuration) · **#221** two hostile-input guards in `offset_table.cpp` uncovered — the W-P2-1a Length/Data overrun guard (`:275-278`) and the reserve-bound clamp (`:647-648`); both pre-existing, surfaced by 085's coverage pass.
**#214** — *fold the redundant flat cap loop into the nesting-aware traversal (L-063-4 leg 2)* — **closes with `085-fold-flat-cap-loop`**.
Also open, no issue filed: **ApplExtID(1156)=303** differentiation + `version_registry` re-keying (deferred by 074's L-074-1).

Orchestra read/dictionary tier DONE via 074 + runtime-load entry point via 080; typed **read** tier DONE via 076; typed **builder** tier DONE via 077 + split into per-version libs via 078 (**closed #198**); dictionary census hardening DONE via 072 (**closed #180**); live-wire validation DONE via 075, required-presence scope via 079, strict-validation residuals via 081 (**closed #203 + #205**), per-context group-delimiter resolution via 083 (**closed #210 + #208 + #212**). C-ABI GA-frozen at `1.5.0`; Python PY-001..005 COMPLETE.

**This file is a THIN POINTER — do not paste per-feature history here.** The authoritative records:
- **Merged-feature changelog (newest first):** [`CLAUDE-history.md`](./CLAUDE-history.md).
- **Per-feature status + evidence:** `spec/feature-catalogue.md` + `spec/coverage-index.md`.
- **Per-feature Gate A/B convergence + sign-off:** parent `research/G19-fix-fpml-iso20022/phases/phase-4/<module>/<feature>.md` (+ `phases/phase-4.md` dashboard, `phases/phase-4/cross-module-decisions.md`).
- **Behaviours & limitations (operator-facing):** `spec/behaviors-and-limitations.md` (B-* / L-* rows).
- **Cross-session state + lessons:** project memory `project_*` / `feedback_*` (index `MEMORY.md`; older close-outs `MEMORY-archive.md`).

**CI procedure (per [[project_ci_run_tier_labels]]):** branch protection REQUIRES `tier{1,2,3}-required` + `Gate A`/`Gate B` labels; all three tiers run on `pull_request`, gated on `(gate-a-done ∧ gate-b-done)`. Turn a PR green by satisfying both gates via `/gate-a` + `/gate-b` — the gate-label event co-fires all three matrices in pull_request context. Known exception: stale duplicate `tierN-required` check-runs (`[FAILURE,…,SUCCESS]`) can wedge the merge box at latest-green → merge `--admin` (PRs #177, #182; `required_approving_review_count` is 0). `push:main` re-runs all three post-merge (badge + gate).

**Live deferred work:** session-recovery catalogue row 400 (010 F4 ResendRequest/SequenceReset Reject→Process — largely shipped via 013 + S-023; residual = v1.0 traceability confirm) · live-interop golden-capture + C-103 chunked-resend → G4 ([[project_release_interop_quickfix_fix8]]).
<!-- SPECKIT END -->
