# Spec-Kit feature pipeline (canonical)

> **Source of truth.** User-authored canonical sequence. This file is the
> authoritative pipeline; agent memory defers to it. Cross-referenced by the
> constitution (Article XVI/XVII). Keep the "Canonical sequence" section
> verbatim as the user maintains it; agent reconciliation lives in its own
> appended section and never edits the user's text in place.

## Canonical sequence (user-authored)

```
0.  /speckit-constitution         (one-time per project)

PHASE 0 — SPEC
1.  /speckit-specify <description>   creates feature branch + spec.md
2.  /speckit-clarify                  mandatory for ABI/threading/wire/codegen/session/security [const §XVI.3]

PHASE 1 — DESIGN
3.  /speckit-plan                     plan.md + research.md + data-model.md + contracts/ + quickstart.md
4.  /gate-a <feature-id>              Codex design review loop; applies gate-a-{done,waived}
                                      [const §XVII.1] — blockers resolved BEFORE /tasks

PHASE 2 — TASKS
5.  /speckit-tasks                    dependency-ordered task list
6.  /speckit-analyze                  cross-artifact consistency; remediate findings
7.  /speckit-checklist                api/abi/nfr review checklists (audience: Gate B)
##8.  /speckit-taskstoissues            optional — sync to GitHub issues   [DISABLED]

9.  /speckit-checklist-audit          MANDATORY gate — BLOCKS step 10.
                                      CHECKLIST AUDIT. EXECUTOR: the
                                      /speckit-checklist-audit skill — do NOT
                                      hand-walk it ad hoc, do NOT rely on
                                      /speckit-implement's weak unticked-box
                                      prompt as the gate.
                                      For every domain checklist in
                                      specs/<id>/checklists/ (the auto
                                      /specify `requirements.md` is closed at
                                      steps 1–2 and is exempt): the
                                      orchestrator audits each CHK item
                                      against spec.md, cross-checked vs
                                      plan/tasks/data-model/contracts and the
                                      signed-off design-doc anchor. Every
                                      item MUST be ticked [x] with an inline
                                      disposition tag:
                                        SPEC-FIXED      — spec.md edited
                                        DD-DECIDED §X   — settled in the
                                                          signed-off design
                                                          doc (anchor rule);
                                                          recorded, not re-spec'd
                                        WAIVED: <reason> — recorded; allowed
                                                          ONLY for items NOT
                                                          tagged Completeness/
                                                          Clarity/Consistency
                                      Genuine Completeness/Clarity/Consistency
                                      gaps MUST be SPEC-FIXED or DD-DECIDED —
                                      never WAIVED. Every design-doc §/RC
                                      anchor cited by spec is spot-verified to
                                      exist in the signed-off revision (no
                                      dangling ref). Record = the checklist
                                      file itself (boxes + disposition tags).
                                      If ANY item is SPEC-FIXED, re-run
                                      step 6 (/speckit-analyze) before step 10
                                      (the spec edit invalidates the prior
                                      drift check). Exit: zero
                                      un-dispositioned [ ] boxes across all
                                      domain checklists.

PHASE 3 — IMPLEMENT
10. /speckit-implement                runs tasks, marks [X] (NOT evidence-based — see step 12)

11. /simplify                         3 specialized Opus review agents (reuse / quality /
                                      efficiency) → Opus triages: fix genuine in-scope
                                      simplifications + any real Gate-B-relevant defect;
                                      defer behavioral/perf redesigns + ambiguous items as
                                      tracked follow-ups in the verify decision doc
                                      [const §XVI.7 — before verify, NOT merely before PR]

12. /speckit-verify <feature>         MANDATORY local Tier-1 mirror [const §XVII.8]
                                      → .specify/decisions/<feature>-verify.md
                                      verdict must be GREEN or YELLOW before continuing

13. Gate-B preconditions              [const §XVII.8] /gate-b's pre-flight HARD-BLOCKS the
                                      loop on all of: verify-record non-RED (4b), feature-
                                      completeness audit non-failing (4d), Gate A evidence
                                      present (4c), and the independence rule (5). The
                                      catalogue + coverage-index ROW WRITES still ship as
                                      explicit tasks.md tasks (T052/T053/T058 — manual);
                                      4d verifies their on-disk state matches the diff.
                                      No separate skill needed — invoke /gate-b directly;
                                      it refuses to start when any precondition is unmet.

14. /gate-b <branch>                  Codex hostile review of main..HEAD on local branch
                                      → .specify/decisions/<feature>-gateb.md (round 1..N)
                                      Fix-loop (Sonnet fixer rounds 1-2 → Codex fixer rounds 3-4)
                                      converge to SHIP-AS-IS or SHIP-WITH-FIXES + documented waivers

PHASE 4 — PUBLISH + MERGE
15. git push -u origin <branch>       publish (only after Codex converged)
16. gh pr create                      body links to verify + Gate A + Gate B records
17. Apply gate-{a,b}-{done,waived}    paired-evidence rule [const §XVII.8]
                                      via gh pr edit OR /gate-b's Post-loop §3 if re-run on PR
18. gh pr merge                       user-driven

19. MARK DONE — close-out bookkeeping  MANDATORY; nothing automates these,
                                      each is updated from memory and gets
                                      dropped if not enumerated. Update ALL
                                      that exist for the feature:
    a. feature-catalogue.md row(s) → `done` (+ coverage-index.md if a
       baseline legitimately moved)
    b. parent: submodule-pointer bump commit (post-merge)
    c. gate-{a,b}-{done,waived} label via `gh api` REST on the merged PR
       (post-PR gap recurs; `gh pr edit --add-label/--body` silently fails)
    d. phases/phase-4.md Track Log: Phase status / Active module / Active
       feature / Last action / Next gate; + Module Status table row
    e. phases/phase-4/<module>/README.md: feature progress + exit-criteria
    f. controlling plan / decision-doc progress log if one governs this
       work (e.g. .specify/decisions/<plan>.md progress table) — LOCAL-ONLY
       (.specify/decisions/ gitignored); the most frequently forgotten one
    g. <feature>-verify.md / lifecycle doc: final "User sign-off" line
    h. project memory: update the relevant state note if the close changes
       cross-session status
    i. Phase-2 design-doc shipped-status pointer (when this feature
       realizes a Phase-2 design doc): update `.specify/<2N>-*.md`'s Status
       header with "+ shipped via <feature> PR #<N> (merged YYYY-MM-DD,
       squash <SHA>)" AND extend the matching `phases/phase-2.md` Track Log
       row with the same pointer. Precedents: 2c→003 PR #67, 2f→006 PR
       #73, 2d→007 PR #74; bullet (i) added 2026-05-20 because the
       previous three merges all silently dropped this surface —
       `feature-catalogue.md` and phase-4 docs captured the
       implementation-status delta, but the Phase-2 design-of-record kept
       reading as "Draft, Gate-A-converged" indefinitely.

20. Review and close all issues for this phase
```

---

## Reconciliation notes (Opus, cross-check 2026-05-17)

Cross-checked against agent memory + the submodule constitution. Sequence
sound, matches memory. Disposition (user-approved 2026-05-17):

- **[A] APPLIED.** Constitution §XVI.7 amended: "before PR open" →
  "before `/speckit-verify`", with the matrix-invalidation rationale.
- **[B] MERGED** into the canonical block (step 11: 3 specialized Opus
  review agents → Opus triage). Justification: review-agent severities are
  unreliable — 004-wire-codec, the agents' headline "O(n²) DoS" was a
  false alarm; the real defect was a `static thread_local` group-slice
  aliasing + zero-alloc-invariant breach, separated only by Opus triage.
- **[C] MERGED** into the canonical block as step 13 (the §XVII.8
  completeness-audit / catalogue second Gate-B precondition).
- **[D] Open coupling.** Step 8 (`/speckit-taskstoissues`) is DISABLED;
  step 20 (close all issues) is its pair and is a no-op while 8 is
  off. Re-enable together.
- **[E] APPLIED (user-directed 2026-05-17).** Inserted the "MARK DONE —
  close-out bookkeeping" step (now step 19). Root cause: the canonical
  pipeline ended at merge + a no-op close-issues step (the disabled-step-8
  pair), so marking trackers done was done only from memory and items were
  dropped — symptom: `retro-coverage-remediation-plan.md` progress log
  stalled at "⏳ next" though 001/002/003 all merged (#69/#70/#71);
  recurring post-PR `gate-a-done` label gap. Step 19 enumerates every
  close-out surface (catalogue, submodule bump, gate label, phase-4 Track
  Log + module README, controlling decision-doc log, lifecycle sign-off,
  memory) so the set is explicit, not recalled. The prior close-issues step
  shifted out to step 20.
- **[F] APPLIED (user-directed 2026-05-18).** Inserted the "CHECKLIST
  AUDIT" MANDATORY gate (now step 9) between step 8 (`/speckit-checklist`)
  and step 10 (`/speckit-implement`). Root cause: `/speckit-checklist`
  emits domain checklists but nothing required their CHK items to be
  dispositioned before code is written — they were de facto deferred to
  Gate B (step 14, post-implement), so a requirements-quality defect could
  survive into implementation and only surface at hostile PR review. The
  audit is a spec-vs-{plan,tasks,data-model,contracts,design-doc} review;
  disposition is recorded in the checklist file itself (SPEC-FIXED /
  DD-DECIDED §X / WAIVED). Completeness/Clarity/Consistency gaps cannot be
  WAIVED. SPEC-FIXED dispositions loop back to step 6 (`/speckit-analyze`)
  so a spec edit cannot bypass the drift check. First applied to
  006-async-mutex `checklists/concurrency.md` (24 PASS / 6 GAP / 9 MINOR /
  1 ACTION; 3 SPEC-FIXED: CHK017/CHK029/CHK037; CHK031 anchor spot-check
  verified clean against `2f-async-mutex.md` v1.5).
- **[G] APPLIED (user-directed 2026-05-19).** (i) The step-9 CHECKLIST
  AUDIT gate is now backed by a real command: the `/speckit-checklist-audit`
  skill is named as its EXECUTOR (it had been a prose-only orchestrator
  step and was therefore repeatedly skipped — only `/speckit-implement`'s
  fragile unticked-box prompt caught it; that prompt is no longer the gate).
  First exercised on 007-threading-clock `checklists/gate.md` (46/46;
  1 SPEC-FIXED: CHK012/F-1 cross-thread bench-soft ambiguity; anchors
  spot-verified clean against `2d-threading.md` v0.4). (ii) Step numbering
  normalized to integers — the inserted [E]/[F] gates had created the
  fractional/letter labels 8.5, 9.5, 10b. New mapping: old 8.5→9, 9→10,
  9.5→11, 10→12, 10b→13, 11→14, 12→15, 13→16, 14→17, 15→18, 16→19, 17→20;
  every internal "step N" cross-reference and the [B]/[C]/[D]/[E]/[F]
  references above were updated to the integer scheme. No step semantics
  changed; this is a label-only normalization plus the executor binding.
- **[H] APPLIED (user-directed 2026-05-22).** Per-phase Sonnet implementer
  is now backed by a bound agent: `.claude/agents/phase-implementer-sonnet.md`
  (at the parent root, beside `gate-*.md` commands). The orchestrator
  invokes it as `subagent_type=phase-implementer-sonnet` and passes only
  the per-call delta (task IDs / fix queue, feature context, anchor
  paths). The recurring brief — anchor citation, TDD ordering, scope
  discipline, constitutional bindings, the anti-pattern library from
  memory (placeholder tests, FSM-end-state false-pass, counting-PMR
  alloc-guard escapes, fork-inherited asio pools, asio post-resume
  executor bouncing, co_spawn terminal-only cancellation default,
  codegen emitter staleness, lcov DA/BRDA coverage basis, profraw
  staleness), commit-message convention, `no-EnterWorktree/no-push`
  constraints, and the CodeGraph lookup/sync rules — lives in the
  agent file, not in each brief. Two callsites: step 10
  (`/speckit-implement` per-phase Sonnet subagent per
  `[[feedback_speckit_subagent_phasing]]`) and step 14 (`/gate-b`
  Sonnet fixer rounds 1–2). The orchestrator's parent-verification step
  does NOT go away — it shifts from re-checking persona-line compliance
  to spot-checking dispositions and report claims against
  `[[feedback_subagent_phase_verification_two_traps]]` /
  `[[feedback_tracking_pmr_resource_false_pass]]`. Same date: `gate-b.md`
  added a `## CodeGraph — sub-agent expectations` section that every
  reviewer/triage/fixer brief references; `gate-a.md` added a `##
  CodeGraph — when it applies in Gate A` section scoping Gate-A's
  bundle-only review (no `codegraph sync` in Gate A — no code changes).
- **[I] APPLIED (user-directed 2026-05-22).** Two new bound agents
  carry the analysis steps out of the main session into subagent
  context (the heaviest cross-artifact reasoning was burning the
  orchestrator's context budget):
    * `.claude/agents/checklist-auditor.md` — canonical executor for
      step 9 (`/speckit-checklist-audit`). Walks every domain checklist
      CHK item, dispositions PASS / SPEC-FIXED / DD-DECIDED §X /
      WAIVED:<reason> with the realizability sub-check from
      `[[feedback_checklist_audit_realizability]]`, spot-verifies
      design-doc anchors, edits checklists in place. SPEC-FIXED
      autonomous (orchestrator diffs to verify); Completeness/Clarity/
      Consistency MAY NEVER be WAIVED.
    * `.claude/agents/spec-analyzer.md` — canonical executor for step 6
      (`/speckit-analyze`). Read-only; runs detection passes A–F
      (Duplication / Ambiguity / Underspecification / Constitution
      Alignment / Coverage Gaps / Inconsistency); CRITICAL / HIGH /
      MEDIUM / LOW severity; returns a structured report inline. No
      file writes (the upstream skill mandates read-only).
  Both use CodeGraph lightweight tools (`codegraph_search` / `node`
  for symbol-existence checks; `callers` when a referenced shape might
  break consumers) with explicit `projectPath`. Neither runs
  `codegraph sync` — analyze is read-only, audit edits checklists/
  spec.md only (no library code).
  Orchestrator's parent-verification gate carries forward: spot-check
  dispositions (auditor) and finding severities (analyzer); a
  subagent's "all green" or "0 CRITICAL" does NOT replace the
  spot-check, by the same memory-grounded reasoning that applies to
  `phase-implementer-sonnet` (per [H]).

No conflicts found on: `/clarify` before `/plan` (§XVI.3), Gate A before
`/tasks` (§XVII.1), `/speckit-verify` mandatory + non-RED (§XVII.8),
paired-evidence labels (§XVII.8).
