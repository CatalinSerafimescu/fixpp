# Changelog

Notable changes to **fixpp**, newest first.

> **Scope, stated explicitly because this file is new.** Article XX §4 of
> `.specify/constitution.md` has required "an entry in `CHANGELOG.md`" for every
> backwards-incompatible constitutional amendment since v0.1 — but the file was never created,
> so the clause has never been satisfiable. It is created here, with the first entry being the
> amendment that first triggered it.
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

- **Article VIII §2 — the regression budget's comparand.** ±5% is now measured against a
  **paired base-vs-candidate run on one runner**, against the merge-base of the candidate and
  the PR's target branch (A-B-A-B, min-per-tree), replacing checked-in `bench/baselines/`
  files. Those files no longer gate the per-PR budget and are reported per row with a named
  reason when not comparable.
- **Article VIII §4 (latency bullet)** — clarified that §2's "checked-in baselines do not gate"
  does **not** reach the v1.0 release baseline, which remains blocking.

### Added

- **Article VIII §2a — fail-closed invariants of the paired comparand.** The base must be the
  merge-base of the candidate and the PR's target branch, an ancestor of and distinct from the
  candidate; a missing, crashed, empty or uninformative measurement is a **failure**; and the
  paired set is **non-decreasing**. These existed in the implementation but not in the
  constitution, so a workflow edit could have removed them without amending anything.
- **This file**, per Article XX §4.

### Why this is backwards-incompatible

The amendment both tightens and loosens, and the loosening does not cancel the tightening:

- **Tightening** — five binaries move from having *no hard timing decision at all* to a hard
  >±50% rejection. Changes that were previously mergeable now fail. That is a perf-budget
  tightening under Article XX §4 even though the printed "±5%" numeral is unchanged: an
  unenforced obligation became an enforceable rejection criterion.
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
- CI enforces a sentinel **no wider than ±50%** until the estimator characterisation in
  `.specify/ci209-bench-gate.md` §6a is satisfied. **A green sentinel is not evidence of ±5%
  compliance.**
- A per-change budget does not bound **cumulative drift** — repeated +4.9% steps each pass. A
  periodic or release-anchored comparand is a named follow-up.

Implemented by PR #272 (`91abcc74`), which closes the instrumentation half of issue #263.
Amendment PR #285. Gate A round 1 returned BLOCK on the first draft — including its
misclassification as a MINOR bump — and this entry reflects the corrected classification.
