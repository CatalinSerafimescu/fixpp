<!-- SPECKIT START -->

**START HERE:** the routing index is [`brain/index.md`](./brain/index.md) — it maps a question to the surface that answers it, and states the boundary against the graph index (structure) in one table. `brain/components/<component>.md` lists EVERY document describing a component, including superseded ones flagged in place.

**This file is a THIN POINTER — do not paste per-feature history here.** The authoritative records:
- **Merged-feature changelog (newest first) and the non-feature merge log:** [`CLAUDE-history.md`](./CLAUDE-history.md).
- **Per-feature status + evidence:** `spec/feature-catalogue.md` + `spec/coverage-index.md`.
- **Per-feature Gate A/B convergence + sign-off:** parent `research/G19-fix-fpml-iso20022/phases/phase-4/<module>/<feature>.md` (+ `phases/phase-4.md` dashboard, `phases/phase-4/cross-module-decisions.md`).
- **Behaviours & limitations (operator-facing):** `spec/behaviors-and-limitations.md` (B-* / L-* rows).
- **Cross-session state + lessons:** project memory `project_*` / `feedback_*` (index `MEMORY.md`; older close-outs `MEMORY-archive.md`).

**Last merged FEATURE: `106-logon-guards-write-bounds`** (PR #725, squash `263b23a6`, 2026-11-27) — B129 + B110 + B123 + B124; closes fixpp#702, #703, #714, #717 and #724. Tier 2 `windows-msvc-asan`'s job cap is 300 minutes, the other legs keep 210 (#724). The identity gates run first in both roles, so an unauthorised identity's Logon draws nothing and changes nothing (B-106-1), and every refused Logon or acknowledgement keeps NextNumIn (B-106-2). The give-up of an unanswered TestRequest fires while a write is blocked, and the session ends once (B-106-3). An opt-in `write_timeout_ms` limits every write that does not end the session; a cut write returns `session_write_timeout` (136), `FIXPP_ERR_TRANSPORT_IO` in C (B-106-4). The TLS handshake bound is configurable on both roles: `tls_handshake_timeout_ms` for an initiator, `acceptor_tls_handshake_timeout` for the Engine's acceptor (B-106-5). Surfaces: C-ABI **1.19**, BREAKING, behaviour only; C++ layout changes in `Session`, `SessionConfig`, `EngineConfig` and `EngineEstablishment` (B-106-6). Retires L-104-3 and L-104-8. Full entry: [`CLAUDE-history.md`](./CLAUDE-history.md); records `decisions/speckit/106-logon-guards-write-bounds-*` in the parent repo.

**Earlier features:** [`CLAUDE-history.md`](./CLAUDE-history.md), newest first. Shipped behaviour: `spec/behaviors-and-limitations.md`.

**Decision records:** in the main checkout and `fixpp-parallel`, `.specify/decisions/` is a symlink that resolves to the parent repo's tracked `research/G19-fix-fpml-iso20022/decisions/speckit/`. A newly created worktree gets a plain gitignored directory instead: re-create the symlink there, or its records stay on that machine. The symlink needs its own `.gitignore` entry, because git treats a symlink as a file and `decisions/` matches directories only.

**Open issues:** derive with `gh issue list --state open`. Placement: the parent repo's `research/G19-fix-fpml-iso20022/phases/phase-4/issue-batches.md`.

C-ABI version: `include/fix/c_api/version.h` (`FIXPP_C_ABI_VERSION_MAJOR/MINOR`). Pre-release breaking changes bump MINOR; the version resets to `1.0.0` at the first public release (constitution §X.7). Per-tier delivery status: `spec/feature-catalogue.md`.


**CI procedure (per [[project_ci_run_tier_labels]]):** branch protection REQUIRES `tier{1,2,3}-required` + `Gate A`/`Gate B` labels; all three tiers run on `pull_request`, gated on `(gate-a-done ∧ gate-b-done)`. Turn a PR green by satisfying both gates via `/gate-a` + `/gate-b` — the gate-label event co-fires all three matrices in pull_request context. Known exception: stale duplicate `tierN-required` check-runs (`[FAILURE,…,SUCCESS]`) can wedge the merge box at latest-green → merge `--admin` (PRs #177, #182; `required_approving_review_count` is 0). `push:main` re-runs all three post-merge (badge + gate).
- Apply gate labels in one API call. Each label call fires its own `pull_request` wave, concurrency cancels the others, and `tier*-required` can be left on a cancelled run that reads as BLOCKED.
- GitHub links a PR as closing an issue whenever a closing keyword precedes it, even inside a negation ("does NOT close #N"), in the PR body or in a commit message. Check `closingIssuesReferences` before merging.

**Live deferred work:** session-recovery catalogue row 400 (010 F4 ResendRequest/SequenceReset Reject→Process — largely shipped via 013 + S-023; residual = v1.0 traceability confirm) · live-interop golden-capture + C-103 chunked-resend → G4 ([[project_release_interop_quickfix_fix8]]).
<!-- SPECKIT END -->
