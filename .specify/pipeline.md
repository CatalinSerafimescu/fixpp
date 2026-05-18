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

8.5 CHECKLIST AUDIT                    MANDATORY gate — BLOCKS step 9.
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
                                      step 6 (/speckit-analyze) before step 9
                                      (the spec edit invalidates the prior
                                      drift check). Exit: zero
                                      un-dispositioned [ ] boxes across all
                                      domain checklists.

PHASE 3 — IMPLEMENT
9.  /speckit-implement                runs tasks, marks [X] (NOT evidence-based — see step 10)

9.5 /simplify                         3 specialized Opus review agents (reuse / quality /
                                      efficiency) → Opus triages: fix genuine in-scope
                                      simplifications + any real Gate-B-relevant defect;
                                      defer behavioral/perf redesigns + ambiguous items as
                                      tracked follow-ups in the verify decision doc
                                      [const §XVI.7 — before verify, NOT merely before PR]

10. /speckit-verify <feature>         MANDATORY local Tier-1 mirror [const §XVII.8]
                                      → .specify/decisions/<feature>-verify.md
                                      verdict must be GREEN or YELLOW before continuing

10b. Gate-B preconditions             [const §XVII.8] verify non-RED AND the feature-
                                      completeness audit non-failing AND feature-catalogue.md
                                      / coverage-index.md updated (these ship as explicit
                                      tasks.md tasks — nothing automates them)

11. /gate-b <branch>                  Codex hostile review of main..HEAD on local branch
                                      → .specify/decisions/<feature>-gateb.md (round 1..N)
                                      Fix-loop (Sonnet fixer rounds 1-2 → Codex fixer rounds 3-4)
                                      converge to SHIP-AS-IS or SHIP-WITH-FIXES + documented waivers

PHASE 4 — PUBLISH + MERGE
12. git push -u origin <branch>       publish (only after Codex converged)
13. gh pr create                      body links to verify + Gate A + Gate B records
14. Apply gate-{a,b}-{done,waived}    paired-evidence rule [const §XVII.8]
                                      via gh pr edit OR /gate-b's Post-loop §3 if re-run on PR
15. gh pr merge                       user-driven

16. MARK DONE — close-out bookkeeping  MANDATORY; nothing automates these,
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

17. Review and close all issues for this phase
```

---

## Reconciliation notes (Opus, cross-check 2026-05-17)

Cross-checked against agent memory + the submodule constitution. Sequence
sound, matches memory. Disposition (user-approved 2026-05-17):

- **[A] APPLIED.** Constitution §XVI.7 amended: "before PR open" →
  "before `/speckit-verify`", with the matrix-invalidation rationale.
- **[B] MERGED** into the canonical block (step 9.5: 3 specialized Opus
  review agents → Opus triage). Justification: review-agent severities are
  unreliable — 004-wire-codec, the agents' headline "O(n²) DoS" was a
  false alarm; the real defect was a `static thread_local` group-slice
  aliasing + zero-alloc-invariant breach, separated only by Opus triage.
- **[C] MERGED** into the canonical block as step 10b (the §XVII.8
  completeness-audit / catalogue second Gate-B precondition).
- **[D] Open coupling.** Step 8 (`/speckit-taskstoissues`) is DISABLED;
  step 17 (close all issues, was 16) is its pair and is a no-op while 8 is
  off. Re-enable together.
- **[E] APPLIED (user-directed 2026-05-17).** Inserted step 16 "MARK DONE —
  close-out bookkeeping". Root cause: the canonical pipeline ended at merge
  + a no-op close-issues step (the disabled-step-8 pair), so marking
  trackers done was done only from memory and items were dropped — symptom:
  `retro-coverage-remediation-plan.md` progress log stalled at "⏳ next"
  though 001/002/003 all merged (#69/#70/#71); recurring post-PR
  `gate-a-done` label gap. Step 16 enumerates every close-out surface
  (catalogue, submodule bump, gate label, phase-4 Track Log + module
  README, controlling decision-doc log, lifecycle sign-off, memory) so the
  set is explicit, not recalled. Old step 16 → 17.
- **[F] APPLIED (user-directed 2026-05-18).** Inserted step 8.5 "CHECKLIST
  AUDIT" as a MANDATORY gate between step 7 (`/speckit-checklist`) and
  step 9 (`/speckit-implement`). Root cause: `/speckit-checklist` emits
  domain checklists but nothing required their CHK items to be
  dispositioned before code is written — they were de facto deferred to
  Gate B (step 11, post-implement), so a requirements-quality defect could
  survive into implementation and only surface at hostile PR review. The
  audit is a spec-vs-{plan,tasks,data-model,contracts,design-doc} review;
  disposition is recorded in the checklist file itself (SPEC-FIXED /
  DD-DECIDED §X / WAIVED). Completeness/Clarity/Consistency gaps cannot be
  WAIVED. SPEC-FIXED dispositions loop back to step 6 (`/speckit-analyze`)
  so a spec edit cannot bypass the drift check. First applied to
  006-async-mutex `checklists/concurrency.md` (24 PASS / 6 GAP / 9 MINOR /
  1 ACTION; 3 SPEC-FIXED: CHK017/CHK029/CHK037; CHK031 anchor spot-check
  verified clean against `2f-async-mutex.md` v1.5).

No conflicts found on: `/clarify` before `/plan` (§XVI.3), Gate A before
`/tasks` (§XVII.1), `/speckit-verify` mandatory + non-RED (§XVII.8),
paired-evidence labels (§XVII.8).
