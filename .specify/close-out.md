# Close-out checklist (pipeline step 19)

**This file replaces step 19's inline enumeration.** One source of truth for close-out; `pipeline.md`
step 19 points here.

## How to use it

1. Copy §3's template into the feature's Gate B record (`.specify/decisions/<feature>-gateb.md`).
2. Give **every** row a disposition: **`DONE`** or **`N/A — <reason>`**. A row left blank is not a
   skipped row, it is an unknown one — and the reason step 19 exists is that unenumerated surfaces get
   dropped from memory. **Silence is not a disposition.**
3. Rows marked **auto** have a mechanical check. Run it; do not eyeball it.

> ⚠️ **Order matters at the end.** Do **body → label → push** so ONE `pull_request` wave lands on the
> final SHA. And note that **pushing to `main` cancels the merge commit's in-flight run** — `tier1.yml`'s
> per-ref concurrency group is `cancel-in-progress: true` and every push to `main` shares the ref. Check
> `gh run list --branch main --limit 1` before pushing if you need the merge SHA's own result.

---

## §1 — ALWAYS. Every PR, no exceptions.

| # | Row | auto/manual | Check / trap |
|---|---|---|---|
| 1 | `spec/feature-catalogue.md` row(s) → `done` | **auto** | Covered by `/gate-b` pre-flight 4d against the diff |
| 2 | **B&L functional delta** — the B-\*/L-\* rows for *what a user or operator must now know that they did not before*, **or** an explicit `B&L delta: none — <reason>` | **auto** | `.claude/scripts/check_bl_delta.py gate --feature <id>`. ⚠️ Its green proves a B&L surface **moved**, not that it moved for *this* feature — that judgement is Gate B Post-loop §4b |
| 3 | **SecondBrain** — the update itself happens **PRE-PUSH** (pipeline step 15a), on the branch. Here it is a **RE-CHECK**: fires only if the pre-push pass no longer holds. See "Row 3 is now two things" below | manual edit · **auto** re-check | Re-run the `refs_external` sweep as the **LAST** close-out action, after the parent commits — same reason as row 4: a later commit falsifies an earlier verify |
| 4 | Parent: **submodule-pointer bump** commit (post-merge) | **auto** | ⚠️ **Two silent wrong-SHA traps.** `update-index --cacheinfo` does **not** validate the SHA exists, and `git commit -- <path>` afterwards **silently discards** the staged gitlink and re-reads the worktree. Stage with `git add`, commit **without** a pathspec, then verify: `git ls-tree HEAD <path>` matches `git -C <path> rev-parse HEAD`, and `git cat-file -t <sha>` says `commit` |
| 5 | `gate-{a,b}-{done,waived}` labels on the merged PR | **auto** | ⚠️ `gh pr edit --add-label` **silently no-ops** on this repo — use `.claude/scripts/gh-pr-meta.sh` (REST) and **read back**. (`gh issue edit --add-label` does work; PRs are the broken case.) A `*-waived` label is half a disclosure pair — the rationale belongs in the PR body |
| 6 | `phases/phase-4.md` — **status dashboard ONLY** | manual | Terse Track Log cells + the Module Status row. **No decision narrative here** — that goes in the per-feature sub-file |
| 7 | `phases/phase-4/<module>/README.md` — feature progress + exit criteria | manual | |
| 8 | `<feature>-verify.md` / lifecycle doc — final **User sign-off** line | manual | |
| 9 | **Issues** — review and close what this PR closes | manual | ⚠️ Verify with `closingIssuesReferences`, **not** the PR body. A commit message saying a PR does *not* close an issue is what **closed** it — the linker ignores negation. Grep the whole commit range |

**Row 3 is now two things, and the first one is not here.**

*What to update* is unchanged: if the PR changed a component with a page under `brain/components/`,
update that page; if it **superseded** a decision an existing document records, flag that document
there; and name the governing feature id in a **header comment** at the code, the way
`async_mutex.hpp` names *"Erratum E-5 (048)"* — a pointer, not a result, so it does not rot like a
line citation (#310).

*When* changed. That work now happens at **pipeline step 15a — on the branch, before the push** —
together with a local run of BOTH freshness halves. It used to happen here, post-merge, and the
records show what that produced: `337` (`fcbcf7cb`), `pr367-264` (`89f453ef`) and `360-361`
(PR #383) all landed the brain edit as a separate direct-to-`main` push or its own PR, so the
instruments first ran on a commit that no review was going to look at.

⚠️ **And a red there blocks nothing.** `brain-freshness.yml` is **not** in branch protection's
required set — that is `Gate A`, `Gate B`, `tier{1,2,3}-required`, derived from
`repos/.../branches/main/protection`, not assumed. So the failure mode is not "CI stops you", it is
"CI goes red on `main` and nobody is obliged to look". That is the argument for running the
instruments **locally, pre-push**, rather than treating CI as the discovery channel. Folding the
brain edit into the feature branch costs **zero** extra CI: `brain-freshness.yml` fires on
`pull_request: branches: ["**"]` ungated by the gate labels, so it runs on that PR whether or not
brain/ is in the diff. A separate post-merge push is the shape that buys a `push: main` run nobody
reviewed.

**Run the WRAPPER for the external half.** `check_brain.py sweep` exits **0** on *"no refs_external
declared"* — a green over nothing, this repo's signature failure. The parent's
`research/G19-fix-fpml-iso20022/tools/brain-external-sweep.sh` is what CI runs and what you run: it
self-tests the instrument, seeds a broken ref into a **temp copy** and requires RED on it, then
requires the real sweep to pass **and** report a non-zero execution count. Quote that count in the
disposition — `337`'s record already does (*"self-test 11/11 first, then sweep OK (`<N>` refs) +
gate clean"*), which is the shape to copy. Copy the SHAPE, not a number: what the sweep should
report is "more than nothing", and any figure written here is a result that nothing re-runs.

**The re-check (row 3b) has TWO trigger arms — the second one is the structural gap.**

1. **Gate B or CI forced a change** after the pre-push pass: a fix round touched a component, or
   changed a decision the pre-push brain update does not record.
2. **Close-out itself dangled a ref.** Rows 4/6/7/11/12/13 write `phases/**` and
   `decisions/**` — which is *exactly* what `refs_external` names. Row 11 is literally *"amend the
   signed-off artifact a gate decision invalidated"*. So close-out can break a brain reference
   **after** step 15a proved them all resolving, and the submodule's `brain-freshness.yml` is
   structurally blind to it: it checks `refs` only, because `refs_external` paths do not exist in a
   `fixpp` checkout. The parent's `brain-external-freshness.yml` does catch it — as a red on `main`,
   post-merge, on a `push` no PR reviewed. **So do not evaluate arm 2 by judgement: re-run the
   sweep as the last close-out action, after the parent commits.**

**One line to settle in the Gate B record, not to leave implicit.** A brain commit inserted between
steps 14 and 15 is a commit **Gate B never reviewed** — a gate result is scoped to the commits that
existed when it ran. Either land the brain edit inside a Gate B fix round (`gate-b.md` is already
brain-aware — it has the reviewer paste the component page's superseded-documents table), or
state in the record that the brain commit is docs-only and deliberately outside the reviewed range.
Pick one and write it down; otherwise it comes back as a Gate B finding.


## §2 — CONDITIONAL. Row applies only when its trigger fires; otherwise `N/A — <reason>`.

| # | Row | Trigger | auto/manual |
|---|---|---|---|
| 10 | `spec/coverage-index.md` entries | a coverage baseline legitimately moved | **auto** (4d) |
| 11 | **Amend the signed-off artifact a gate decision invalidated** | Gate A or Gate B changed a design that a `.specify/` design doc or a prior `specs/<id>/research.md` records | manual |
| 12 | Controlling plan / decision-doc progress log | a controlling plan governs this work | manual |
| 13 | Phase-2 design-doc **shipped-status pointer** | this feature realizes a Phase-2 design doc | manual |
| 14 | Project memory state note | the close changes cross-session status | manual |
| 15 | **Anti-pattern library** entry → `.claude/agents/phase-implementer-sonnet.md` | Gate B Post-loop §4 produced candidates **and** the user approved them | manual |

**Row 11 is the one that produced issue #334.** A gate decides something, the code changes, and the
signed-off document that recorded the old design is never amended — so it re-seeds the wrong model into
the next feature. `.specify/2j-controlplane.md` still states the engine accept loop runs on the engine
executor, as a *threading invariant*, while the shipped engine asserts a per-session strand. Two further
documents repeat it. If you cannot amend the artifact in this PR, **flag it on the component page and
file an issue** — do not leave it silent.

**Row 12 — a stale parenthetical worth knowing.** Step 19's old text called this LOCAL-ONLY because
`.specify/decisions/` is gitignored. Half true: it is gitignored **in the submodule**, but it is a
symlink to the parent's **tracked** `research/G19-fix-fpml-iso20022/decisions/speckit/`. A record
written there **is committed to the private parent repo** — write it accordingly. (A *newly created*
worktree gets a plain gitignored directory instead; re-create the symlink or its records die with it.)

---

## §3 — Template. Paste into `<feature>-gateb.md` and fill every row.

```markdown
## Close-out checklist (.specify/close-out.md)

ALWAYS
1.  catalogue row(s) done .............. DONE | N/A — <reason>
2.  B&L functional delta ............... DONE (<ids>) | N/A — none, <reason>   [auto: check_bl_delta.py]
3.  SecondBrain component page ......... pre-push: DONE (<sha>; gate clean, sweep <N> refs) | N/A — <reason>
3b. SecondBrain close-out re-check ..... trigger: <fired|not fired> — <disposition>   [sweep re-run LAST]
4.  submodule pointer bump ............. DONE (<old> -> <new>, ls-tree verified) | N/A
5.  gate labels ........................ DONE (<labels>, read back) | N/A
6.  phase-4.md dashboard ............... DONE | N/A
7.  module README ...................... DONE | N/A
8.  verify/lifecycle sign-off .......... DONE | N/A
9.  issues closed ...................... DONE (<#s>, via closingIssuesReferences) | N/A

CONDITIONAL — state the trigger's status, not just N/A
10. coverage-index .................... trigger: <fired|not fired> — <disposition>
11. amend superseded artifact ......... trigger: <fired|not fired> — <disposition>
12. controlling plan log .............. trigger: <fired|not fired> — <disposition>
13. Phase-2 shipped pointer ........... trigger: <fired|not fired> — <disposition>
14. memory state note ................. trigger: <fired|not fired> — <disposition>
15. anti-pattern entry ................ trigger: <fired|not fired> — <disposition>
```

A filled instance per PR is the point. *"Consistent"* without one means the same rows with no evidence
any of them ran.
