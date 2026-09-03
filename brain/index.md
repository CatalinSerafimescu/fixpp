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

> ### ⚠️ About to trust a zero, write a count, or copy a rule?
>
> Read [`failure-classes.md`](failure-classes.md) — six recurring classes with the **trigger** that puts
> you at risk and the **procedure** that refutes each. It is a taxonomy, not a checklist: the instances
> live in a private corpus queried at the moment of the trigger, via
> `research/G19-fix-fpml-iso20022/tools/lessons.py`. ⭐ **A lookup, never a gate** — nothing returned
> means no recorded lesson matched, not that the code is safe.

> ### 🧭 Asked to explain how the whole project works?
>
> **Start at [`render-architecture.md`](render-architecture.md), not here.** It is a prompt: reading
> order, what to state as uncertain, and how to name the gaps instead of papering over them. The naive
> route — read `architecture.md`, summarise — produces a confident description of a system that has not
> existed since May 2026.

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
| **How the system is put together** — module layering, the dependency graph, public namespaces, per-module public surface, the plugin pattern, build layout, the service boundary, the glossary | **`.specify/architecture.md`** — the **spine**, cited as `[arch §N.m]` by 21 documents. ⚠️ **Read its `Appendix Z` FIRST** — it lists what the shipped tree contradicts, with inline `Z-1`..`Z-5` markers. Same Status-header trap as the `2*.md` docs below, and it has a queued amendment that was never applied (`2k` **§D.2**, not §D.3). Verify before citing | reconstructing it from the `2*` design docs, which own *subsystem* detail and explicitly do **not** own the spine |
| **Which design doc owns a subsystem** | `[arch §10]`'s hand-off table — a closed 13-row list, `2a`–`2m` | guessing from filenames |
| **The FIX session engine** — establishment, FSM, sequence numbers, resend, logout | ⭐ [`components/session.md`](./components/session.md). **`[arch §10]` has NO session row and no `2*` doc owns it**, so authority is split across headers, `specs/<id>/` bundles and B&L | any single design doc — there isn't one |
| **The error taxonomy** — one C++ enum, a coarser C ABI, and why an old consumer sees `UNKNOWN` | [`components/errors.md`](./components/errors.md) | the enum alone, which does not show the downgrade |
| **The C ABI** — the licence seam, why GA is `1.5.0`, and what the ABI gate does **not** prove | [`components/c-api.md`](./components/c-api.md) | assuming a green ABI gate means ABI-compatible |
| **Logging / OpenTelemetry** | [`components/observability.md`](./components/observability.md) — ⚠️ trace *context* is plumbed; **spans have no engine call site** | "OTel shipped", which is true and misleading at once |
| **Python bindings** | [`components/python-api.md`](./components/python-api.md) | widening the SWIG surface without widening e2e coverage |
| **Configuration** — TOML loading and name→plugin resolution | [`components/config.md`](./components/config.md) | assuming a new plugin is reachable once the class exists |
| **QuickFIX compatibility** | [`components/quickfix-compat.md`](./components/quickfix-compat.md) — translation, **not** emulation; the adapter was rejected | planning a runtime shim |
| **The service wrapper** | [`components/service.md`](./components/service.md) — a stub **by decision**; `src/` has no implementation | treating the empty module as a gap to fill |
| **Which features TOUCH a given file or contract** | `git log --oneline -- <path>` | ⛔ **the catalogue.** A row names the feature that **DELIVERED** the thing plus its PR — not every feature that has since modified it. `wire/validator.hpp` has ~15 commits against a row citing one feature; several are perf/hardening passes with no row of their own |
| **Whether a `co_spawn` site's closure outlives its coroutine** — the #291/#354 lifetime rule, and which of the two instruments answers it | the pair below: `tools/check_co_spawn_lambda.py` (lexer, in CI) and `tools/audit_co_spawn_named_closure.py` (AST, on demand) | grepping for `}(),` — see the pair's own docstrings for why a regex cannot decide the named-closure form |
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

## ⛔ Two files in `.specify/` hold designs that were REJECTED

`2c-codegen.draft-r1.md` (959 lines) and `2f-async-mutex.draft-r1.md` (1,166 lines) are archived
**v0.1** drafts. Each was replaced after an adversarial review closed with *"needs full rewrite /
structural rethink"* — so it is not a stale document, it is **the design that was killed**, at full
length, in the same directory as its replacement.

Until 2026-08-29 **the pointer ran only one way**: each successor said *"v0.1 archived as
`…draft-r1.md`"*, and neither draft said anything. A reader who opened a draft directly saw
*"Status: Draft v0.1 — Pre-Gate-A"*, which reads as **early**, not as **rejected**. Both now carry a
forward-pointing banner.

> **The general rule this is an instance of: a supersession pointer must run FORWARD.** A successor
> naming its predecessor helps nobody who started at the predecessor — and starting at the wrong file
> is the whole failure mode. Compare `async_mutex.hpp`'s *"Erratum E-5 (048)"*, which points forward
> from the superseded thing and is why that supersession was found unaided.

⚠️ `2f-async-mutex.phase4-tests.md` is **not** in this class — it is a live Phase-4 input artifact,
cited by `constitution.md`.

## ⚠️ A Phase-2 design doc's Status header does NOT tell you whether it shipped

Most `.specify/2*.md` headers read *"Draft vN — Gate A round N converged"* whether or not features
have since realized, changed, or superseded them. `pipeline.md` step 19 has a row for updating that
pointer precisely because it kept being dropped — the surface was added after three consecutive merges
silently skipped it, and it kept being skipped after that.

**So a "Draft" header is not evidence a doc is unshipped, and a converged-Gate-A header is not evidence
it is current.** Derive it instead — which features cite the doc:

```bash
grep -rl "2h-transport" specs/*/spec.md
```

No count is written here on purpose; it moves. The **condition** is what is durable: *a design doc
whose Status header does not name the features that realized it tells you nothing about its currency,
and reads as authoritative anyway.*

## ⚠️ Amending a document that is cited BY LINE NUMBER

**Append at the END of the file, and make every in-body edit an in-place, same-line-count
replacement.** An insertion anywhere above a cited line shifts it, silently, and re-points the
citation at the wrong content.

This is not hypothetical and not someone else's mistake: the Step-R pass that exists to *stop*
documents rotting broke **28 accurate line citations into `.specify/2d-threading.md`** by inserting a
47-line note near the top. Caught only because the next document in the queue happened to cite the
one just edited.

> ✅ **`tools/check_line_citations.py --shift-audit A..B` now checks this** (issue #336). Its other
> three modes cannot: they *fail a diff that **ADDS** a line-number citation*, and an edit that
> **INVALIDATES** existing ones adds none. `--shift-audit` runs both checks below mechanically, and
> reports its own denominator — a clean run means "no rot among the citations it could RESOLVE".

**The check, which costs one command.** A pure line-shift audit — if every hunk is `NcN` (same count
in, same count out) plus at most one append at the original last line, nothing moved:

```bash
diff <(git show <base>:<file>) <file> | grep -E '^[0-9]'
```

And to prove the citations actually still resolve, compare the **content** of each cited line before
and after — a line number that still exists is not the same as a line number that still means what it
meant.

## Three conventions that carry the same load, cheaply

1. **A header comment naming the governing feature id** — `async_mutex.hpp`'s *"Erratum E-5 (048)"* is
   the working example, and it did the whole job unaided. It is a **pointer, not a result**, so it does
   not rot the way a line-number citation does (issue #310). Write one when a later feature supersedes
   an earlier decision about a file.
2. **The functional delta at close-out** — what a user must now know that they did not before goes in
   B&L, or an explicit `B&L delta: none — <reason>` disposition. Checked by
   `.claude/scripts/check_bl_delta.py` at `/gate-b` pre-flight; silence is not a disposition.

3. **`refs:` is ordered, and the order IS the routing.** Most authoritative first:
   **code (`include/`, `src/`) → `.specify/` → `specs/<id>/` → `spec/behaviors-*` → everything else.**
   Decision records are `refs_external` and CodeGraph symbols are `codegraph_entry` — separate keys, so
   their position is fixed by construction. `check_brain.py gate` **enforces** the order; a convention
   only written down is one nobody can tell has been broken.

### What a component page is FOR — and the test that keeps it that way

> ⭐ **A page is pointers plus rejected alternatives. Nothing else.**
> The test: **delete every sentence that describes what the code does today. Did the page lose any
> routing value?** If yes, that sentence was doing a job the code should be doing — replace it with the
> pointer. If no, it was the beginning of the next fossil.
>
> This is not stylistic. A behavioural sentence here has no gate, no test and no reviewer; it goes
> stale silently, and it goes stale in the one place people come to *find out what is stale*. What does
> **not** rot is **why** a decision was taken and **what was rejected** — that half is historical and
> is the entire reason this bundle exists.

### Two instruments for one rule, and why only one is in CI

A lambda coroutine reaches its captures **through the closure object**, so the closure must outlive
the coroutine. asio's `awaitable` promise returns `suspend_always` from `initial_suspend`, so the body
does not begin until after `co_spawn` returns — meaning a violation is a use-after-free **on the first
execution**, not a hazard some later edit arms, and no compiler diagnostic fires on it.

Two forms, two instruments, and the split is deliberate:

| form | instrument | where it runs |
|---|---|---|
| `co_spawn(ioc, [&]{…}(), tok)` — immediately-invoked temporary | `tools/check_co_spawn_lambda.py` | **CI, ungated tier1**, buildless |
| `auto lam = […]; co_spawn(ioc, lam(), tok)` — named closure | `tools/audit_co_spawn_named_closure.py` | **locally, on demand** |

**Why the second is NOT in CI, which is the part that will get re-litigated.** Deciding the named
form needs the closure's scope compared against the call that DRIVES the coroutine, which needs an
AST and a compilation database. A full sweep is ~242 TUs at ~31 s each — ~35 min on four cores, hours
on a 2-vCPU runner. A diff-scoped variant would be affordable and **strictly weaker**: it cannot see a
site whose safety changed because a driving call moved in a file the diff did not touch. Either way it
needs a build, so it could only live in a **gated** job — emitting nothing during the review rounds
that are the only thing between a fresh unsafe site and merge, which is the window the buildless lexer
was placed to cover.

Weighed against that: the immediately-invoked form is already caught in the ungated job, and the named
form requires a closure declared in a *narrower* scope than its driving call. ⚠️ **Whether the
population is currently clean is a measurement with a date on it — look it up in #354, do not assume
it from this page.**

**Run the audit when there is a reason to:** before a release or after a wave of new `co_spawn` sites;
when touching the pump/drain helpers, since its `DRIVING_FREE_FUNCTIONS` set decides what counts as
*driven* and a rename there silently turns callers into findings; or as the AST-level follow-up when
the lexer fires.

⚠️ **Both tools carry their own blind-spot lists, and those lists are the point.** The lexer's
docstring enumerates four forms that defeat it, each measured against it. The AST tool's enumerates
the false-SAFE paths it cannot close — reachability, cross-file driving calls, fixture destructors.
A tool trusted past its documented reach is class 1 on [`failure-classes.md`](./failure-classes.md).

⚠️ **The AST tool is cross-checked by a second instrument sharing no code** —
`tools/reconcile_co_spawn_census.py` runs the same population through `clang-query` and diffs the
site **sets**, not the counts. Two instruments agreeing on a total while disagreeing about which
sites those are is a finding, not a pass. Keep them as a pair; a clean sweep from a single tool with
this one's defect history is not evidence.

### ⭐ The local idiom: pin a decision with a `static_assert`, not a comment

Seen in at least three unrelated places — a frozen error-code range, the trace-context size, and a
compile-time guard that a QuickFIX-shaped synchronous store cannot bind to the awaitable
`MessageStore` (*"the BUILD IS THE TEST"*). **A comment saying "do not do X" is a claim nobody
re-checks; a `static_assert` is the same claim checked on every build.** Reach for it when a decision
must not be undone by accident.

## Where evidence lives, and why you may not be able to open it

Gate A / Gate B / verify / completeness records are **private**: they live in the parent research repo
at `research/G19-fix-fpml-iso20022/decisions/speckit/`, gitignored on the public side. Component pages
link to them as `refs_external`. If you are outside this machine those links will not resolve — that is
deliberate, not rot.

## Contents

- [`components/`](./components/) — component → **every** governing decision document
- [`history.md`](./history.md) — superseded claims, on demand
- [`log.md`](./log.md)
