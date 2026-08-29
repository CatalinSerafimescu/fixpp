---
okf_version: "0.2"
type: Routing Index
title: SecondBrain — where to look
description: One hop from a question to the surface that answers it. Read this before exploring.
status: stable
---

# SecondBrain — where to look

> ## ⚠️ The CODE is authoritative. SecondBrain is not.
>
> This bundle is a **consultant**: it points you at the right files and explains **why** something was
> decided and what was **rejected**. That half is historical — it does not change retroactively, and it
> is the half you cannot get from reading code.
>
> It does **not** establish what the code does today. **Treat every description of current behaviour
> here as a lead to check, then cite the source, not this page.**
>
> This bundle exists because signed-off design documents rotted. It has **no immunity** from that.
> A page trusted instead of read becomes the next fossil — and a worse one, because it is where people
> come for the fossil list.

**Read this first.** It routes; it does not restate. Every surface below already exists and is
maintained — this page exists because agents did not find them, not because they were missing.

## Route by question

| You want to know | Go to | Not to |
|---|---|---|
| Which symbols exist, who calls what, blast radius | **the graph index** (`codegraph_*`, always pass `projectPath`) | this bundle — it holds no structure |
| What a feature delivered, its PR, tests, status | `spec/feature-catalogue.md` | the specs bundle |
| Which FIX spec section maps to which feature row | `spec/coverage-index.md` | grepping `specs/` |
| Shipped behaviour a user must know; known limitations | `spec/behaviors-and-limitations.md` (B-\*/L-\* rows) | code comments |
| Whether a limitation is **still open** | the same file — **resolved rows are moved out** to `spec/behaviors-and-limitations-closed.md`. Reading the live file alone gives you only open ones. | grepping `L-0NN-` repo-wide, which hits both |
| Governing rules, citable as `[const §N.M]` | `.specify/constitution.md` | anywhere else |
| **Why** something is the way it is, and what was rejected | **`components/` in this bundle**, then the decision records it names | a single design doc — see below |
| What is left to do for v1.0 | `../REMAINING-WORK.md` (parent) | this bundle |
| History — what we used to believe | `history.md` (deliberately off this path) | — |

## The one thing this bundle adds

Everything above is a pointer. **`components/` is not** — it is the only thing here carrying content,
and the content is *completeness*: for one component, **every document that claims to describe it**,
including superseded ones **flagged as superseded**.

That exists because of a measured failure. Two blind agents were asked composite questions about a
component (what is it / how is it built / why / what breaks). Both were careful and self-verifying.

- The one working on `async_mutex` **found** that `specs/006-async-mutex/*` and
  `decisions/2f-async-mutex.md` still describe a design that feature 048 removed — because the shipped
  header names *"Erratum E-5 (048 — strand-local reap)"* in a comment, which routed it there.
- The one working on the engine accept path read `specs/023-engine-session-strand/research.md`,
  never saw that `specs/015-runtime-engine/research.md` and the signed-off `.specify/2j-controlplane.md`
  say something different, and reported **"no disagreements found"** — true of its four sources, false
  of the repository.

**Rigor inside the chosen set does not compensate for an incomplete set.** A component page is the
complete set.

## Two conventions that carry the same load, cheaply

1. **A header comment naming the governing feature id** — `async_mutex.hpp`'s *"Erratum E-5 (048)"* is
   the working example, and it did the whole job unaided. It is a **pointer, not a result**, so it does
   not rot the way a line-number citation does (issue #310). Write one when a later feature supersedes
   an earlier decision about a file.
2. **The functional delta at close-out** — what a user must now know that they did not before goes in
   B&L, or an explicit `B&L delta: none — <reason>` disposition. Checked by
   `.claude/scripts/check_bl_delta.py` at `/gate-b` pre-flight; silence is not a disposition.

## Where evidence lives, and why you may not be able to open it

Gate A / Gate B / verify / completeness records are **private**: they live in the parent research repo
at `research/G19-fix-fpml-iso20022/decisions/speckit/`, gitignored on the public side. Component pages
link to them as `refs_external`. If you are outside this machine those links will not resolve — that is
deliberate, not rot.

## Contents

- [`components/`](./components/) — component → **every** governing decision document
- [`history.md`](./history.md) — superseded claims, on demand
- [`log.md`](./log.md)
