# Changelog

Notable changes to **fixpp**, newest first.

> **Scope, stated explicitly because this file is new.** Article XX §4 of
> `.specify/constitution.md` has required "an entry in `CHANGELOG.md`" for every
> backwards-incompatible constitutional amendment since v0.1 — but the file was never created,
> so the clause has never been satisfiable. It is created here.
>
> ⚠️ **This is not the first amendment that triggered §4, and an earlier revision of this file
> wrongly said it was.** Constitution **v0.4** (`ef9ca2bd`, 2026-07-11) added Article VII §8,
> whose text reads "`gtest_discover_tests` **prohibited** for these buckets" — a banned-pattern
> addition, which §4 classifies as backwards-incompatible. Its own Sync Impact Report
> nonetheless recorded "no banned-pattern addition" and bumped MINOR. That entry is **not
> backfilled here**, because reclassifying a ratified amendment is a separate constitutional act
> requiring its own review. The disposition — backfill v0.4 as v-major, or amend §4 to define
> "banned-pattern addition" narrowly enough to exclude Article VII §8 — is a **named follow-up**.
> Recording the discrepancy is not the same as resolving it, and this note does not resolve it.
>
> This file records **backwards-incompatible constitutional amendments** (Article XX §4) and,
> going forward, **released library versions**. It is not a commit log: routine features, fixes
> and MINOR/PATCH constitution bumps live in git history and in the Sync Impact Reports at the
> top of `.specify/constitution.md`.
>
> ⚠️ The constitution's version and the library's release version are **separate sequences**.
> Constitution v1.0 asserts nothing about project GA.

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

---

## Constitution v1.0 — 2026-08-20

**Backwards-incompatible.** Article XX §4 major bump: an effective perf-budget tightening.

### Changed

- **Article VIII §2 — the regression budget's comparand.** A **slowdown greater than +5%** —
  the budget is one-sided; a speed-up needs no approval — is now measured against a
  **paired base-vs-candidate run on one runner**, against the merge-base of the candidate and
  the PR's target branch (A-B-A-B, min-per-tree), replacing checked-in `bench/baselines/`
  files. Those files no longer gate the per-PR budget and are reported per row with a named
  reason when not comparable.
- **Article VIII §4 (latency bullet)** — clarified that §2's "checked-in baselines do not gate"
  does **not** reach the v1.0 release baseline, which remains blocking.

### Added

- **Article VIII §2a — fail-closed invariants of the paired comparand.** The base must be the
  merge-base of the candidate and the PR's target branch, distinct from the candidate; a
  crashed, empty or uninformative measurement — and a missing measurement for a binary the
  merge-base *can* build — is a **failure**; and the paired set is **non-decreasing**. Most of
  these were enforced by the implementation but not by the constitution, so a workflow edit
  could have removed them without amending anything.

  **One explicit exception, and only one:** a paired binary that does not exist in the
  merge-base is a *candidate-only addition*. Its base measurement is necessarily absent, and
  that absence is not an error — adding a benched binary is what §3 asks for. Such a row stays
  hard-gated on execution and schema, is excluded from timing comparison for that PR alone, and
  becomes a mandatory paired comparand from its first merged commit onward.
- **This file**, per Article XX §4.

### Why this is backwards-incompatible

The amendment both tightens and loosens, and the loosening does not cancel the tightening:

- **Tightening** — five binaries move from having *no hard timing decision at all* to a hard
  **>+50% slowdown** rejection. Changes that were previously mergeable now fail. That is a
  perf-budget tightening under Article XX §4 even though the printed "5%" numeral is unchanged:
  an unenforced obligation became an enforceable rejection criterion.
- **Loosening** — checked-in baselines cease to gate, and 18 of 23 manifest binaries carry no
  automated timing limit.

### Why the comparand changed

`bench/baselines/` could not serve as one. Of **27 tracked files** there, 25 are JSON and 2 are
`.gitkeep`. Of the 25 — an exact partition — **3** carry zero benchmark rows, **10** have no
`cpu_time` key on any row, **1** has a row whose `cpu_time` is `null`, and **11** carry numeric
`cpu_time`. Of those 11, one is a debug build and one more is a hand-authored partial schema,
leaving **nine** usable full-schema release comparands for 23 benched binaries.

Of the five binaries the paired tier gates, **four declare `none:`** in `bench/ci-suite.txt`,
and the fifth's comparand (`dictionary/xml_loader.json`) was shown by issue #263 never to have
described `main` — seeded, invalidated three commits later inside its own PR, never re-seeded,
so the ±5% budget was silently breached 8–16× against it for three months.

**A budget stated against a comparand that does not exist is unenforceable, not strict.**

### Known limits of the replacement, disclosed rather than discovered later

- Automated timing scope is **five** binaries; the other **18** are execution- and
  schema-checked only, with no automated timing limit.
- CI's provisional slowdown threshold **must not exceed +50%** until the estimator
  characterisation in `.specify/ci209-bench-gate.md` §6a is satisfied. That ceiling is
  constitutional — widening it requires an amendment. **A green sentinel is not evidence of
  +5% compliance.**
- A per-change budget does not bound **cumulative drift** — repeated +4.9% steps each pass. A
  release-anchored comparand closing that hole is **required at v1.0**, not optional.
- Every binary that becomes eligible under §6a **must be promoted into the paired set at or
  before the next release**; leaving an eligible binary unpaired past that point violates §2.

Counts above (5 paired / 18 unpaired / 23 total) are **as of this entry's date**. The paired set
is non-decreasing and required to grow, so treat them as a point-in-time record, not a standing
fact — the current set lives in `.specify/ci209-bench-gate.md`.

Implemented by PR #272 (`91abcc74`), which closes the instrumentation half of issue #263.
Amendment PR #285. Gate A returned **BLOCK twice** — round 1 on four P1s including the MINOR
misclassification, round 2 on four more, of which the sharpest was that the first draft of §2a
made *every* missing measurement a failure and so would have outlawed the candidate-only-addition
path that PR #272 itself had just spent two Gate B rounds building. This entry reflects both
corrections.
