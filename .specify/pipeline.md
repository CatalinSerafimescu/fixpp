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

16. Review and close all issues for this phase
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
  step 16 (close all issues) is its pair and is a no-op while 8 is off.
  Re-enable together.

No conflicts found on: `/clarify` before `/plan` (§XVI.3), Gate A before
`/tasks` (§XVII.1), `/speckit-verify` mandatory + non-RED (§XVII.8),
paired-evidence labels (§XVII.8).
