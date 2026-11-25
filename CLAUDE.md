<!-- SPECKIT START -->

**START HERE:** the routing index is [`brain/index.md`](./brain/index.md) — it maps a question to the surface that answers it, and states the boundary against the graph index (structure) in one table. `brain/components/<component>.md` lists EVERY document describing a component, including superseded ones flagged in place.

**This file is a THIN POINTER — do not paste per-feature history here.** The authoritative records:
- **Merged-feature changelog (newest first) and the non-feature merge log:** [`CLAUDE-history.md`](./CLAUDE-history.md).
- **Per-feature status + evidence:** `spec/feature-catalogue.md` + `spec/coverage-index.md`.
- **Per-feature Gate A/B convergence + sign-off:** parent `research/G19-fix-fpml-iso20022/phases/phase-4/<module>/<feature>.md` (+ `phases/phase-4.md` dashboard, `phases/phase-4/cross-module-decisions.md`).
- **Behaviours & limitations (operator-facing):** `spec/behaviors-and-limitations.md` (B-* / L-* rows).
- **Cross-session state + lessons:** project memory `project_*` / `feedback_*` (index `MEMORY.md`; older close-outs `MEMORY-archive.md`).

**Last merged FEATURE: `104-sequence-reset-fidelity`** (PR #706, squash `20a45577`, 2026-11-20) — B53 + B107 + #701; closes fixpp#595, #695 and #701. With `reset_on_logout`, a received Logout resets the numbers at its reset point and a Logout fixpp sends never resets (B-104-1); from that point until the connection ends nothing but a reset changes the numbers and nothing more is transmitted (B-104-8). `close()` waits, bounded by `logon_timeout_ms`, for a received Logout short of its reset point and for any reset unit in flight, whatever the knobs; `Engine::stop()` waits with no bound (B-104-2). A lenient or unilateral initiator accepts an unasked 141=Y Logon acknowledgement whatever NextNumIn was (B-104-4, option (i)); a Logon below NextNumIn in NotConnected or LogonSent draws a Logout with Text once the identity gates pass (B-104-6); a ResendRequest whose EndSeqNo is at or above the next outbound number is answered through-current (B-104-5). Surfaces: C ABI unchanged; C++ layout changes in `session.hpp`, `engine.hpp` and `seqnum_manager.hpp` (B-104-7). Retires L-103-2 and L-103-7. Full entry: [`CLAUDE-history.md`](./CLAUDE-history.md); records `decisions/speckit/104-sequence-reset-fidelity-*` in the parent repo.

**Earlier features:** [`CLAUDE-history.md`](./CLAUDE-history.md), newest first. Shipped behaviour: `spec/behaviors-and-limitations.md`.

**Decision records:** in the main checkout and `fixpp-parallel`, `.specify/decisions/` is a symlink that resolves to the parent repo's tracked `research/G19-fix-fpml-iso20022/decisions/speckit/`. A newly created worktree gets a plain gitignored directory instead: re-create the symlink there, or its records stay on that machine. The symlink needs its own `.gitignore` entry, because git treats a symlink as a file and `decisions/` matches directories only.

**Open issues:** derive with `gh issue list --state open`. Placement: the parent repo's `research/G19-fix-fpml-iso20022/phases/phase-4/issue-batches.md`.

C-ABI version: `include/fix/c_api/version.h` (`FIXPP_C_ABI_VERSION_MAJOR/MINOR`). Pre-release breaking changes bump MINOR; the version resets to `1.0.0` at the first public release (constitution §X.7). Per-tier delivery status: `spec/feature-catalogue.md`.


**CI procedure (per [[project_ci_run_tier_labels]]):** branch protection REQUIRES `tier{1,2,3}-required` + `Gate A`/`Gate B` labels; all three tiers run on `pull_request`, gated on `(gate-a-done ∧ gate-b-done)`. Turn a PR green by satisfying both gates via `/gate-a` + `/gate-b` — the gate-label event co-fires all three matrices in pull_request context. Known exception: stale duplicate `tierN-required` check-runs (`[FAILURE,…,SUCCESS]`) can wedge the merge box at latest-green → merge `--admin` (PRs #177, #182; `required_approving_review_count` is 0). `push:main` re-runs all three post-merge (badge + gate).
- Apply gate labels in one API call. Each label call fires its own `pull_request` wave, concurrency cancels the others, and `tier*-required` can be left on a cancelled run that reads as BLOCKED.
- GitHub links a PR as closing an issue whenever a closing keyword precedes it, even inside a negation ("does NOT close #N"), in the PR body or in a commit message. Check `closingIssuesReferences` before merging.

**Live deferred work:** session-recovery catalogue row 400 (010 F4 ResendRequest/SequenceReset Reject→Process — largely shipped via 013 + S-023; residual = v1.0 traceability confirm) · live-interop golden-capture + C-103 chunked-resend → G4 ([[project_release_interop_quickfix_fix8]]).
<!-- SPECKIT END -->
