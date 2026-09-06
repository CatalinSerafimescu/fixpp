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

**This page is a taxonomy, not a list.** Seven classes have produced most of the defects found in this
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
- ⚠️ **WHEN THE INSTRUMENT IS A PREDICATE OVER A GRAMMAR, ENUMERATE THE PRODUCTION — NOT THE EXAMPLES
  YOU CAN THINK OF.** A fix's own controls are written by whoever held the wrong model, so they
  inherit its blind spot, and brainstorming cases samples exactly the model that was wrong. Reading
  the production is the only step that does not. #289 batch 17: a lexer read every `'` as opening a
  character literal, so a C++14 digit separator (`10'000`) started a literal that ran to the next
  apostrophe and blanked every intervening line, code included. The fix shipped with controls for
  `10'000`, `0x1F'FF` and `u8'0'` — and a reviewer immediately produced `.1'0`, which the grammar
  admits (`fractional-constant: digit-sequence_opt . digit-sequence`) and which the fix still ate.
  The examples were the wrong instrument for choosing examples.
- ⚠️ **A FORMATTER CHANGES WHAT A SOURCE-READING INSTRUMENT SEES WITHOUT CHANGING WHAT THE PROGRAM
  DOES, so run those gates AFTER formatting.** C++ concatenates adjacent string literals and
  `clang-format` splits one that crosses the column limit; a gate harvesting "the last string literal
  in the call" then reads only the tail. #289 batch 17's label-uniqueness gate was GREEN before
  `clang-format` and reported a nonexistent duplicate after it — and the same defect has a
  fails-toward-clean sign: two sites carrying the SAME label, one split and one not, harvest as `tail`
  and `full` and read as DISTINCT. The runtime value never moved; only the instrument's view of it did.
- ⚠️ **A FIX for a false-clean is itself an instrument change, and routinely introduces the NEXT
  false-clean.** Measured: one detector took three rounds, each remedy creating the next hole —
  anchoring on one physical line, then swallowing a region whose parens were unbalanced *inside a
  string literal*, then leaking guard state across functions because the previous fix narrowed a
  window instead of removing the assumption. **Re-run every earlier control after each fix, and add
  the new mode as a control before believing the fix.** A remedy that only closes the reported case
  is class 2 wearing an instrument's clothes.
- ⚠️ **A CLASSIFIER'S FALLBACK IS A CLAIM, AND A FALLBACK SET TO THE COMMON CASE FAILS TOWARD THE
  EASY ANSWER.** This is the no-result path wearing a value: the search does not return "empty", it
  returns *the answer most rows have*, so a row the instrument could not classify is indistinguishable
  in the output from one it classified correctly. Measured in `classify-289.py`: the walk that finds a
  census site's enclosing function returned `("TEST", "<none>")` on exhaustion, and TEST-body is
  exactly the shape whose migration recipe is the simplest — so three rows inside an
  `extern "C" int LLVMFuzzerTestOneInput` harness that links no gtest were offered for a migration
  whose miss branch is `ADD_FAILURE()`. **Two independent causes** put them there (a signature regex
  that `extern "C"` defeats, and a lookahead the function sits beyond), and neither was visible,
  because a correct row and a fallback row printed identically. Give the unresolved case its **own**
  value, outside every recipe bucket, so a consumer filtering on the recipes drops it instead of
  migrating it — and add a control that fails if the fallback is reverted, since a control asserting
  the common case would have passed the whole time it was wrong.

- ⚠️ **A SHELL PIPELINE CAN TURN A SUCCESSFUL MATCH INTO A FAILURE, AND IT DOES SO ONLY ON LARGE
  INPUTS.** Under `set -o pipefail`, `printf '%s' "$out" | grep -q PATTERN` exits **141** when the
  pattern MATCHES: `grep -q` stops at the first hit and closes the pipe, `printf` takes SIGPIPE, and
  pipefail propagates it. Measured in `ci/pump-red-arm.sh`, where the `else` branch reads *"NO
  REPORT — the miss branch did not announce itself"*: three correctly-migrated sites were reported
  SILENT. **It is size-dependent**, so it had shipped a batch earlier and passed every time — with
  little output `printf` finishes before `grep` exits. A fixture-sized self-test cannot see it.
  Use a herestring (`grep -q PATTERN <<<"$out"`). ⚠️ **The tell is a contradiction, not an error**:
  the matcher says "not found" while the surrounding diagnostic prints the very text it wanted —
  chase that rather than re-reading the pattern.

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

### 7. Removing a spurious gate unmasks whatever the gate was holding back

A rejection you have *correctly* proven wrong is still a rejection. Deleting it is right, and it also
lets input reach code behind it that has never run on that input — code whose own correctness was never
tested there, because nothing ever got that far.

- **Trigger:** you are about to make something loadable, reachable, or acceptable that previously was
  not — fixing a false rejection, widening a filter, relaxing a guard proven over-strict.
- **Procedure:** ask what sits BEHIND the gate that has never seen this input, and test *that*, not
  only the gate. Diff acceptance in both directions: enumerate what the old code rejected and the new
  code accepts, then check each one is handled correctly downstream.
- ⚠️ **The unmasked defect usually has the OPPOSITE polarity.** The gate failed closed, so what it hid
  fails open — a rejection becomes a silently wrong answer rather than a louder rejection. Verifying
  "the thing I fixed now works" cannot see it; only the acceptance diff can.

### 8. Consolidating N copies dissolves the population an audit asserts over

Deduplication is usually unambiguous progress: one definition instead of six, one place to fix a bug.
What it also does — silently — is empty out any instrument whose job was to compare the copies. That
instrument does not report "my population is gone"; it reports whatever its extractor does on inputs
it was never designed for, and the reading of that output is usually "clean" or "broken", neither of
which is "this check no longer has anything to check".

- **Trigger:** you are hoisting a constant, extracting a shared helper, or collapsing duplicated
  blocks — and somewhere there is a consistency check, a byte-identity audit, a "these must agree"
  test, or a lint keyed to the duplication.
- **Procedure:** run that check BEFORE and AFTER. Then make the after-state *coherent* rather than
  merely quiet. The choices are to retire the check with its reason recorded, or to re-aim it at what
  the consolidation now makes true.
- ⚠️ **A SELECTOR THAT WAS EXACT BECOMES A PROXY.** This is the specific mechanism, and it is easy to
  miss because the selector's text does not change: a population picked by "the file mentions `X`" is
  identical to "the file DEFINES `X`" exactly while every mention sits beside its definition.
  Consolidation breaks that equivalence in one step — every former definer still *uses* the name — so
  the selector keeps matching and starts meaning something else.
- ⚠️ **An empty population is only evidence if the instrument is shown able to report a non-empty
  one.** Retire the population, keep a synthetic positive control that constructs the thing the
  selector looks for and requires it to be found. Otherwise "empty" and "broken" print identically —
  which is class 1 reached by a different road.

**Reference instance:** #289 batch 10. `kWindowMissSentinel` was copy-defined in three test files and
`audit-copy-span.sh` asserted byte-identity over the span containing it. Hoisting the constant into
`tests/support/pump_until_ready.hpp` left all three files still *mentioning* the name, so the
bare-token selector still selected them while the span they were selected for no longer existed —
three `EXTRACTOR FAILED` lines. The fix was to make the selector match the definition, not the token;
the FULL population is now empty by construction, and the control that builds a synthetic definer each
run is what makes that emptiness readable.

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
