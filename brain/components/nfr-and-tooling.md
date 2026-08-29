---
type: Component Decision Map
title: nfr and tooling — why neither gets a subsystem page, and why nfr's status column cannot be trusted
description: Two catalogue families that are not subsystems. Recorded so the next person does not re-open the question, and so nfr's uniform backlog is not mistaken for fact.
status: stable
refs:
  - spec/feature-catalogue.md
  - .specify/constitution.md
codegraph_entry: []
---

# `nfr` and `tooling` — deliberately no subsystem page

> ## ⚠️ The CODE is authoritative. This page is not.
>
> SecondBrain is a **consultant**, not a source of truth. It points you at the right files and explains
> **why** a decision was taken and what was **rejected** — that half is historical and does not change
> retroactively. It does **not** establish what the code does today.
>
> **Anything here describing current behaviour is a LEAD TO CHECK, not a fact to cite.** Verify against
> source before you rely on it, and cite the source, not this page.
>
> This page exists because signed-off design documents rotted. **It has no immunity from that** — a page
> trusted instead of read becomes the next fossil, and it would be a worse one, because it is the page
> people come to for the fossil list.

## Why this page exists at all

The derived inventory (`tools/brain_inventory.py --census`) flags both families as having **no design
doc and no component page**. That is correct and, for these two, **intended**. This page records the
decision so the gap is not re-opened every time the census is run — and so the flag is read as
*"considered"* rather than *"missed"*.

## `nfr` — a cross-cutting requirement set, not a subsystem

`nfr` rows are quality requirements — language level, coverage floors, sanitizer cleanliness, no
exceptions on hot paths, allocator awareness, perf parity, benchmark gates, static analysis, fuzzing.
They constrain **every** subsystem and are owned by none, so a component page would have no component
to describe. Their real homes are `.specify/constitution.md` (the rules), the CI workflows (the
enforcement), and `bench/baselines/` (the perf gate).

### ⚠️ Its Status column is unreliable — in an UNKNOWN direction

**Every `nfr` row reads `backlog`**, which the catalogue defines as *not started*. Yet the practice
demonstrably ships: sanitizer legs run in CI, `bench/baselines/` exists, `.clang-tidy` exists, and the
no-exceptions rule is constitutional.

Two readings, and **the repository does not say which is true**:

1. **Stale** — the work landed and nobody flipped the rows.
2. **Deliberate** — an NFR is *continuous* and never becomes `done`, so `backlog` is a parking value
   rather than a claim.

> ⭐ **The uniformity is the evidence, and it points at (2).** All rows sit at the same value, whereas
> `dictionary` is mixed — and mixed status is what per-row tracking looks like. But uniformity is
> suggestive, **not decisive**, and these rows are discharged nowhere else either — not in
> `spec/coverage-index.md`, not in the constitution.
>
> **So the honest statement is that the column means something undocumented here.** Do not cite an
> `nfr` row's status as evidence of anything in either direction until someone writes the convention
> down. ⚠️ Note this cuts *against* the intuitive read: the rows most likely to look like a damning
> backlog are the ones most likely to be a bookkeeping convention.

## `tooling` — genuinely future work

Two rows: a FIX session monitoring / protocol analyzer, and git-backed plain-text session
configuration. Both `backlog`, and here that reading is unremarkable — neither exists, neither is
claimed, and no design doc pretends otherwise. **No page is warranted; there is nothing to route to
yet.** Revisit if either acquires a feature bundle.
