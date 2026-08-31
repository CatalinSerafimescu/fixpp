---
type: Render Recipe
title: Render the architecture — a prompt for an agent asked "how does this project work?"
description: What to read, in what order, what to state as uncertain, and how to name the gaps instead of papering over them.
status: stable
refs:
  - .specify/architecture.md
  - .specify/constitution.md
  - spec/feature-catalogue.md
refs_external:
  - research/G19-fix-fpml-iso20022/decisions/architecture.md
  - research/G19-fix-fpml-iso20022/decisions/constitution.md
codegraph_entry: []
---

# Render the architecture

**This page is a prompt, not a document.** If you were asked to explain how this project works, read
this first and follow it. It exists because the naive approach — read `architecture.md`, summarise —
produces a confident, well-organised description of a system that has not existed since May 2026.

---

## The one rule everything else serves

> ⛔ **The CODE is authoritative. Every document here, including this one, is a LEAD.**
>
> So: **cite the source, not the page.** A reader who follows your citation must land somewhere they
> can check. If you write "the accept loop runs on a per-session strand" and cite a design doc, you
> have laundered a claim; cite the header or the `.cpp`.

The corollary is what makes the bundle worth reading at all: **the half that does not rot is *why* a
decision was taken and *what was rejected*.** That is genuinely unavailable from the code, and it is
what you should mine these documents for. Their descriptions of current behaviour are leads.

---

## Order of reading

**1. Derive the shape — do not recall it.**

```bash
python3 tools/brain_inventory.py --census      # catalogue families, statuses, long-lived flows
ls .specify/                                   # the design docs that exist right now
ls -d src/*/                                   # the modules that exist right now
```

Your coverage obligation is the **families and modules those commands print**, not a list from this
page. If this page and those commands disagree, the commands are right and this page has rotted.

**2. `brain/index.md`** — the routing index, and the traps. Read the whole thing; it is short.

**3. `.specify/architecture.md`** — the spine: module layering, namespaces, plugin pattern.
⚠️ **Read its Appendix Z first.** The body dates from 2026-05-15 and Appendix Z lists what the shipped
tree contradicts. Inline `Z-n` markers flag the affected rows.

**4. `.specify/constitution.md`** — the non-negotiable rules. When a design doc and the constitution
disagree, the constitution wins; when a doc *quotes* the constitution "verbatim because normative",
**go read the article** — that quote is a copy that went stale the moment the article was amended, and
this repo has a documented instance.

**5. The per-subsystem design docs `2a`–`2m`** — the deepest "why" available, and the richest source of
rejected alternatives. ⛔ **Two files in `.specify/` hold designs that were REJECTED** — anything named
`*.draft-r1.md`. They are archived drafts, not history of what shipped. `brain/index.md` names them.

**6. `brain/components/`** — one page per component or flow, each listing **every** document that
claims to describe it, *including the wrong ones, flagged in place*. This is the part you cannot
reconstruct by reading well: rigor inside an incomplete set proves nothing.

**7. CodeGraph** for structure — who calls what, blast radius. Always current, because it is derived
from code. Use it for *structure*; use `brain/` for *intent*.

---

## What to state as uncertain — and how

Mark these explicitly rather than smoothing them over. A reader can act on a stated uncertainty; they
cannot act on a confident sentence that happens to be wrong.

| Signal | What to say |
|---|---|
| A design doc's **Status** header | It records **sign-off**, not shipping. It does not tell you whether the design shipped, was superseded, or was reversed. |
| A catalogue row reading **`backlog`** | `backlog` can mean *merely unflipped*, not *unbuilt*. It is never evidence of absence. See `brain/components/nfr-and-tooling.md`. |
| A **`[[deprecated]]`** or friction attribute | Say what it is *for*. Several here are deliberate construction-site friction, not decay. |
| Anything a component page **flags as wrong** | Name the document *and* that it is wrong. Omitting it is the defect the bundle exists to prevent. |
| A **line-number citation** into a `.specify/` doc | Treat as approximate. Amending a cited doc shifts every citation into it, and that has happened. |

---

## Naming the gaps is part of the deliverable, not a failure of it

Some things genuinely have no owning document. `session` — the largest engine surface — has **no
`2*` design doc of its own**; nine docs each contribute a slice. A renderer that quietly synthesises a
coherent story over that gap has produced fiction that reads better than the truth.

> ⭐ **Write the gap down.** *"No document owns X; this account is assembled from N and M, and the
> seam between them is unverified"* is more useful than a seamless paragraph, and it is checkable.

Explicitly in scope for a complete answer, because they are what people ask about: coroutines and the
executor model · session establishment and the FSM · acceptors and the connect path · timeouts and
liveness · the parser and wire format · the error taxonomy · the C ABI · the Python bindings · the
service wrapper (**a stub — say so**) · OTel and logging · configuration · QuickFIX compatibility · the
dictionary XML files · codegen. Route each through step 1's derived lists; if one has no home, that is
a gap to name.

---

## Before you hand it over

- [ ] Every family and module from step 1 appears, or is named as a gap.
- [ ] Every claim about current behaviour cites **code**, not a page.
- [ ] Every document you found to be wrong is named as wrong, not silently dropped.
- [ ] Rejected alternatives appear — that is the part the code cannot tell anyone.
- [ ] Nothing is asserted from a `*.draft-r1.md`.
- [ ] The uncertainties above are stated, not smoothed.
