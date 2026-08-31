---
type: Component Decision Map
title: nfr and tooling — why neither gets a subsystem page, and why nfr's status column cannot be trusted
description: Two catalogue families that are not subsystems. Recorded so the next person does not re-open the question, and so nfr's uniform backlog is not mistaken for fact.
status: stable
refs:
  - .specify/constitution.md
  - spec/feature-catalogue.md
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

### ⚠️ A `backlog` cell here is NOT evidence the capability is absent

The catalogue defines `backlog` as *not started*. For this family that definition is not being
honoured: rows sit at `backlog` while the practice they describe demonstrably ships — sanitizer legs
run in CI, `bench/baselines/` exists, `.clang-tidy` exists, the no-exceptions rule is constitutional.

**Adjudicated 2026-08-31 (user).** The question used to be open here in two readings — *stale status*
versus *an NFR is continuous and so never flips*. It is closed in favour of **stale status**, on two
independent grounds:

- The "never flips" reading is **refutable from the catalogue alone**: it predicts that no `nfr` row
  can ever read `done`. Run the recipe below and look for one.
- An out-of-repo planning tracker names a set of these rows as *delivered practice, never flipped*,
  and schedules the flip as a catalogue edit with **zero code**.

So the correct handling is not "distrust this column in an unknown direction" — it is:

> ⭐ **A `backlog` cell in this family is a lead, not a fact. Verify against the tree before
> concluding anything is missing, and never cite the cell as evidence of absence.** The rows most
> likely to look like a damning backlog are the ones most likely to be merely unflipped. The
> converse is not symmetric: a `done` cell went through the catalogue's own closure bar.

⚠️ **This is a property of the STATUS COLUMN, not of the `nfr` family.** `dictionary` carries the
same defect — see [`dictionary.md`](dictionary.md). Do not read "nfr is unreliable, the others are
fine".

**Recipe — derive the family's status breakdown yourself** (resolve columns *by name*; they have been
off-by-one here before):

```bash
python3 - <<'EOF'
import collections
lines = open('spec/feature-catalogue.md', encoding='utf-8').read().split('\n')
i, hdr = next((i, [c.strip() for c in l.strip().strip('|').split('|')])
              for i, l in enumerate(lines)
              if l.strip().startswith('|') and 'Status' in l and 'Category' in l)
ci, si = hdr.index('Category'), hdr.index('Status')
cnt = collections.Counter()
for l in lines[i + 2:]:
    if not l.strip().startswith('|'):
        continue
    c = [x.strip() for x in l.strip().strip('|').split('|')]
    if len(c) >= len(hdr):
        cnt[(c[ci], c[si])] += 1
for k in sorted(cnt):
    print(k, cnt[k])
EOF
```

`tools/brain_inventory.py --census` prints the same breakdown per family.

## `tooling` — genuinely future work

Two rows: a FIX session monitoring / protocol analyzer, and git-backed plain-text session
configuration. Both `backlog`, and here that reading is unremarkable — neither exists, neither is
claimed, and no design doc pretends otherwise. **No page is warranted; there is nothing to route to
yet.** Revisit if either acquires a feature bundle.
