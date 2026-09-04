---
type: Render Recipe
title: Recurring failure classes — what goes wrong here, and the check that catches each
description: Six classes, not a checklist. Each carries the condition that triggers it and the procedure that refutes it; the instances live outside this repo.
status: stable
refs:
  - tools/check_brain.py
  - tools/check_line_citations.py
  - tools/check_dropin_blocks.py
codegraph_entry: []
---

# Recurring failure classes

**This page is a taxonomy, not a list.** Six classes have produced most of the defects found in this
project — including several found *in the documents and tools built to prevent them*. Each entry gives
the **trigger** (when you are at risk) and the **procedure** (what refutes it). No counts, no instances.

> ⚠️ **The instances are deliberately NOT here.** They live in a private corpus of ~330 recorded
> defects and are queried at the moment of a trigger, not read up front. Copying a subset into this
> repo would create a second list that drifts from the first — which is class 4 below.

---

### 1. An instrument fails toward CLEAN

**The single most recurring class.** A check reports "no findings" because it *could not have reported
anything else* — a broken glob, a truncated corpus, a matcher that never matches, a shimmed tool that
prints `0` for a whole syntax.

- **Trigger:** you are about to believe a zero, an empty result, or a green.
- **Procedure:** **prove it can report non-zero first.** Run it against a case you know is positive —
  the unfixed tree, a seeded match, a deliberate mutant. Distrust a *uniform* result especially.
- ⚠️ **A proven PATTERN is not a proven TRAVERSAL.** A matcher demonstrated non-zero on a seeded case
  says nothing about whether the walk reached the corpus — a recursive grep does not descend a
  symlinked root, and a directory never entered is indistinguishable in the output from one that held
  no match. Assert how many files the sweep actually examined, and check whether any root is a link.
- ⚠️ **A self-test written from the implementation certifies the implementation**, bug included. Build
  fixtures from the real artefact, verbatim.
- ⚠️ **A control set thorough about ONE of an instrument's configurations proves nothing about the
  others.** If the instrument is parameterised — two closing markers, two directories, two presets,
  two file classes — then "the controls pass" is a claim about whichever parameter the controls
  happened to use. Enumerate the configurations the *real run* uses and require a control per
  configuration; a suite that is exhaustive within one of them still reports PROVEN while another is
  broken.
- ⚠️ **Controls anchored to REAL artefacts assert a contingent fact about today's tree; controls built
  from SYNTHETIC fixtures assert a property of the instrument.** Prefer synthetic. A real-file anchor
  fails when the tree legitimately changes, and a later reader cannot distinguish a rotted anchor from
  a broken instrument.
- ⚠️ **Ask what the instrument does when it finds NOTHING — and require that to be an error.** An
  extractor that runs to EOF, a query that returns empty, a matcher that never fires: if the
  no-result path exits 0 and yields a value, the value is wrong and confident. **Fail closed**, then
  mutate the tool to confirm the closed path is reachable.

### 2. A fix that replaces a wrong claim with a NEW claim reproduces the defect

Rounds of review converge only when a claim is **deleted**, not refreshed. A corrected claim is still a
claim, and it rots on the same schedule as the one it replaced.

- **Trigger:** you are about to fix a wrong statement by writing a truer statement.
- **Procedure:** ask whether the statement is needed at all. Prefer **the condition plus the command
  that re-derives it** over any value. A deletion cannot manufacture the next round's finding.

### 3. A document may record a PROCEDURE; it may not record a RESULT

Counts, byte offsets, "N sites", pasted output — all go stale, and **silently**, because nothing
re-runs a document. What follows from *structure* cannot rot; what follows from a *caller* can.

- **Trigger:** you are about to write a number, a list, or a measurement into a document.
- **Procedure:** keep the condition and the recipe; delete the answer. If the number is load-bearing,
  say so and name the command that regenerates it.

### 4. A copy propagates a claim that is false at the new site

A doc that quotes governing text — a constitution article, a sibling's contract — goes false the moment
the source is amended, and **nothing links the two**. A "verbatim because normative" quote reads as
*more* authoritative and is checked *less* often.

- **Trigger:** you are about to reproduce text, a type, or a rule that something else owns.
- **Procedure:** link, don't copy. If you must copy, name the source and the date, and expect it to rot.
- **Scanning heuristic:** grep for `verbatim`, `normative`, `quoted`.

### 5. A correction can carry the same bias as the claim it corrects

Finding one real error licenses neighbouring "corrections" that were never checked. **A fossil list
that over-reports is not the safe direction** — it spends the reader's trust, and it can point at a
"fix" that breaks a working invariant.

- **Trigger:** you just found one error and are writing up several.
- **Procedure:** re-derive **each** neighbouring claim independently against source. Check the
  *qualifier*, not just the number.

### 6. The reviewed artifact may not be the shippable one

A green run on an older commit, a doc amended in a different tree, a self-reported cost that cannot see
delegated work, a gate that skipped rather than passed.

- **Trigger:** you are about to accept evidence produced somewhere other than where the change lives.
- **Procedure:** check the SHA, the tree, and the *scope* of what ran. `continue-on-error` makes an
  `exit 1` inert; a path-skipped required check never reports at all.

---

## How to query the instances

The corpus is private and machine-local. From the parent repo:

```bash
research/G19-fix-fpml-iso20022/tools/lessons.py instrument zero clean
research/G19-fix-fpml-iso20022/tools/lessons.py citation line shift
```

⭐ **It is a LOOKUP, not a gate.** Nothing returned means *"no recorded lesson matched those words"* —
never *"there is no defect here"*. The corpus holds only what has already bitten someone.

⚠️ **There is deliberately no "antipattern checking" agent.** An agent that answers *"no antipattern
applies"* is a new instrument that fails toward clean — class 1, applied to the thing meant to police
class 1 — and nothing could tell whether it looked.
