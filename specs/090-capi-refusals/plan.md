# Implementation Plan: Three C-ABI calls that must refuse — C-ABI 1.7, marked BREAKING

**Branch**: ⚠️ **`447-458-452-capi-refusals` — read from git, NOT from the tooling's `BRANCH` field.**
See *"Trap 1"* immediately below. | **Date**: 2026-09-20 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `/specs/090-capi-refusals/spec.md`

**Design authority**: `.specify/447-458-452-capi-refusals.md` at **v0.10**. See *"Trap 2"* below.

---

## ⚠️ Trap 1 — the feature directory and the git branch differ, and one tool reports the wrong one

This bundle lives in `specs/090-capi-refusals/`. The git branch is `447-458-452-capi-refusals`,
which predates the bundle. `.specify/scripts/bash/check-prerequisites.sh` emits a `BRANCH` field
that **disagrees**, and the mechanism — read from `.specify/scripts/bash/common.sh` for this plan,
not quoted from a summary — is:

1. `get_current_branch` returns `$SPECIFY_FEATURE` when that variable is set, and otherwise the
   **empty string**. ⚠️ **It never invokes git.** Its own comment says *"Return empty to signal
   'unknown'."*
2. `get_feature_paths` then resolves the feature directory from `SPECIFY_FEATURE_DIRECTORY` or from
   the **tracked** `.specify/feature.json`, and — under a comment reading *"fall back to the feature
   directory basename so CURRENT_BRANCH is a usable identifier rather than an empty, misleading
   value"* — assigns that **basename** to `CURRENT_BRANCH` whenever step 1 produced nothing.

So the emitted `BRANCH` is **the pinned directory's basename**, and it is **not a git branch**.

**Re-derive the disagreement; do not read a value from this page** (the note's own §8 item 1 states
it as a condition for exactly this reason — *"Re-derive rather than reading any value here"*):

```
bash .specify/scripts/bash/check-prerequisites.sh --json --paths-only   # its BRANCH field
git rev-parse --abbrev-ref HEAD                                          # the real branch
```

They disagree whenever the checked-out branch is not the pinned directory's basename, which is the
case for this feature.

**This is fixpp#490 — MITIGATED, NOT FIXED, and the mitigation made the defect SILENT rather than
LOUD.** Converting this work to feature mode re-pinned `.specify/feature.json` at this bundle, so
`FEATURE_SPEC` and `FEATURE_DIR` now resolve **correctly by coincidence of the pin** — not because
the resolution was repaired. ⚠️ **Anything downstream that consumes that `BRANCH` field — to check
out, to validate, to name a PR head, to resolve a worktree — will be wrong.** Read the branch from
git.

## ⚠️ Trap 2 — the design note remains the design authority; this plan does not supersede it

`.specify/447-458-452-capi-refusals.md` **v0.10** carries every derivation, population, control and
adjudication behind this feature. `spec.md` states **what** and **why**; this plan states **how the
work is organised**; the note decides. Where any of the three could be read as disagreeing, **the
note wins**, and the other file is the one to correct.

⚠️ **`spec.md` pins the authority at v0.9** in its header block and in its Normative References.
The note has since taken a **v0.9 → v0.10** revision (a published control transcript that stopped
reproducing). This plan and `research.md` cite **v0.10**. The stale pointer in `spec.md` is
**flagged, not edited** — `spec.md` is not this phase's to change.

---

## Summary

Three C-ABI call sequences return `FIXPP_ERR_OK` today while producing a result the caller did not
ask for and cannot detect. This feature makes each of them **refuse**, and extends two C++-only
guards beside them — **five behavioural changes across two governing tracks, one C-ABI MINOR bump,
one PR**.

- **fixpp#447** — `fixpp_msg_remove_tag` refuses with `FIXPP_ERR_INVALID_HANDLE` while any group
  builder is open (D-1), keyed on the **builder stack**, not on the erased tag or position; and
  every group/instance index dereference reachable from a C-ABI entry point becomes a **defined
  refusal** instead of an out-of-bounds read (D-2b, three reachability classes).
- **fixpp#458** — `fixpp_msg_clone` returns the **translation of the wire error it discards today**
  rather than a silently dict-free clone (D-3, a fan of three already-published codes, **nothing
  minted**), with a **nested local exception boundary** inside clone's own body (D-3b); and the C++
  reify factory materialises **eagerly**, refusing through its existing error channel on **exactly
  one** condition — a failed dict-backed re-parse (D-4).
- **fixpp#452** — the two session-config setters refuse configured bytes below `0x20` or `'='`
  (D-5c), `Session::open` applies the same floor to CompIDs, BeginString and each configured
  RefMsgType(372) (D-5a), and the rule acquires **exactly one definition** as a free predicate in a
  session-owned config-validation leaf header (D-5b).

**The approach is derived, not invented.** Every option above was adjudicated in the design note and
transcribed into [research.md](./research.md) in Decision / Rationale / Alternatives form, including
every **rejected** alternative and the reason. ⚠️ **The single most likely way to get this feature
wrong is to fold all five changes under "C-ABI 1.7 BREAKING"** — the note names that failure in its
own words. The C++-only pair is governed by `[const §XVII.1]`, and `[const §X.7]` is recorded for it
as **NOT engaged**.

---

## Technical Context

**Language/Version**: **C++23** (`[const §II.1]`, no fallback; `CMAKE_CXX_STANDARD 23` in the
top-level `CMakeLists.txt`). Python **3** for the binding cell and the harness checks. Bash for the
freeze and citation gates.

**Primary Dependencies**: ⚠️ **NONE ADDED, AND NO PIN MOVES.** Stating the negative is the complete
answer here and it cannot rot; enumerating the project's full pin set into this plan would mint a
rot surface for a feature that adds nothing to it. The feature-relevant existing ones are
**GoogleTest + GoogleMock** (`[const §VII.1]`, the C++ cells), **pytest** against the SWIG bindings
(`[const §VII.2]`, the Python refusal cell), and **Conan** as the dependency manager
(`[const §III.2]`). Any version a task needs is read from `conanfile.py` at that time, never copied
from this page or from an anchor document.

**Storage**: **N/A** — no persistent store is involved. The only files this feature writes outside
source and tests are the byte-freeze manifest, the live behaviours-and-limitations ledger, the
catalogue and coverage-index rows, and the design-corpus amendments.

**Testing**: **GoogleTest** for the C-ABI and C++ cells (`tests/capi/`, `tests/dictionary/`);
**pytest** for the Python binding cell; **ctest** with label selection (`[const §VII.8]` — select by
`ctest -L <label>`, **never** `-R <exe-name>`). `[const §VII.3]` **TDD is mandatory**: every seam
lands RED first. ⚠️ **Only one pre-fix RED has been executed** — Story 1's, inside fixpp#447 itself.
Every other seam's RED needs a cell written **and built**, which is gated (see the Constitution
Check's `[const §XVII.7]` row).

**Target Platform**: **Linux x86_64 / Clang** is primary and is where sanitizers, coverage and fuzz
run (`[const §II.3]`, `[const §IX.6]` Tier 1). **Windows/MSVC** is Tier 2 (manual/nightly); this
feature adds no platform-specific behaviour and does not change that split.

**Project Type**: **library** — an in-process C++23 library with an *adjacent* C ABI
(`[const §I.2]`) and a SWIG-generated Python binding. Single project; no frontend/backend or mobile
split exists in this repository.

**Performance Goals**: ⚠️ **No new budget is introduced and none is claimed met.** The existing
budgets that touch this change are `[2c §6.6]`'s reify budget and the `[2i §6.4]` clone row it
informs. `[const §VIII.2]`'s **paired base-vs-candidate run on one runner** is the only instrument
that decides either. **Two obligations are pre-registered NOT MEASURED in the note's §8** — the
**per-call** cost of D-4's eager materialisation (item 8) and the bench impact of D-5b's byte scan
(item 6). ⚠️ §2.4b measures the **population** of affected callers (empty in production); that is a
**different proposition** from per-call cost and does not substitute for it.

**Constraints**:
- **`[const §VIII.5]`** (zero `new`/`delete` between parse and `fromApp`) is **NOT ENGAGED**, on the
  **narrow** ground that the reify entry point **has no in-window caller today** — not on the wider
  ground that a materialise path can never be in the window. ⚠️ **That reason stops being true the
  moment reify is wired into the session**, which the dictionary design anticipates; the note's §7
  records it, and the first change that adds such a caller must re-run the assessment.
- **No new error enumerator may be minted** (FR-007). The moment one is, 1.7 stops being a pure
  semantics bump and this work collides with the error-taxonomy work (fixpp#449/#450).
- **The byte-level freeze hashes comments.** Obligation 2's documentation edits are **byte** edits,
  so a header that gains only a comment still re-baselines.
- **No local build may be run without explicit user approval** (`[const §XVII.7]` resource gate).

**Scale/Scope**: **3 issues · 5 behavioural changes · 2 governing tracks · 1 C-ABI MINOR bump ·
1 PR · 7 test seams** (the note's §6, seams 1–7, with 1a/1b/2b/2c/3b as named arrangements).
Declaration population and consumer inventory are **derived in the note (§5a, §5d) with a stated
re-derivation recipe** and are not re-counted here.

**NEEDS CLARIFICATION**: **none.** Every field above resolved against the repository, the
constitution or the design authority.

---

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

⚠️ **This gate is stated honestly. Several rows are NOT `PASS`, and each appears in Complexity
Tracking below with its justification.** Nothing on this page may be read as "Gate A passed".

**Reading the Verdict column.** Every **gate** carries exactly one of **`PASS`**,
**`JUSTIFIED-DEVIATION`** or **`FAIL`**. Rows marked **`NOT ENGAGED`** are constraints this feature
does not touch: a constraint that is not engaged is **not a gate with a verdict**, and it is listed
because *"no edit needed" is itself a claim* and is cheaper to state than to re-derive. Do not score
a `NOT ENGAGED` row as a missing verdict.

| # | Gate | Verdict | Basis |
|---|---|---|---|
| 1 | **`[const §X.7]` — pre-release breaking C-ABI change** | **PASS** | This *is* the constitution's pre-first-release path, not a deviation from it: **1.6 → 1.7**, a **MINOR** bump **marked BREAKING**, with **no constitutional amendment**. The premise is measured, not assumed — `gh release list --exclude-drafts` against a **positive control on another repository**, because an empty listing and a broken invocation both print nothing. Four obligations, each with a home (row 1a–1d) |
| 1a | obligation 1 — *"bumps `FIXPP_C_ABI_VERSION_MINOR`, not MAJOR"* | **PASS as planned** | Discharged in note §5b. The version macro's **trailing comment is part of the pin** and is re-authored, not renumbered. Its two hard test cells move; ⚠️ **they fail at ctest time, not build time** — there is no compile-time version assertion |
| 1b | obligation 2 — BREAKING marked in **each affected declaration**, in the **PR description**, and in the **B&L delta** | **PASS as planned — three limbs, three homes** | Declarations: §5a. B&L delta: §5c's **four behaviour rows and no limitation row**. ⚠️ **PR description: §5c's PR-body sub-clause, and this limb had NO HOME until v0.6.** It must be its own `##` **heading**, in #428's in-tree spelling — **bold text under another heading has already failed a gate in this repository and sent every downstream tier red** |
| 1c | obligation 3 — every in-repository consumer updated **in the same PR** | **PASS as planned** | Discharged in §5d, whose inventory is **shared** across the three refusals — which is the binding ground for **one PR** (O-4/C-5), not convenience |
| 1d | obligation 4 — *"remains subject to §1 review and to all four Appendix A controls"* | ⚠️ **FAIL** — this row is the **roll-up of rows 2–5** and takes the worst of them | Two of the four controls (`/analyze`, user `/plan` sign-off) are **OWED**, one (Gate A) is a **non-converged waiver** and one (`/clarify`) is an **adapted** discharge. Obligation 4 cannot be `PASS` while any of its four is outstanding |
| 2 | **`[const §X.1]` / `[const §X.6]` — Codex Gate A (mandatory on the C ABI)** | ⚠️ **JUSTIFIED-DEVIATION** | **RAN; DID NOT CONVERGE.** Label **`gate-a-waived`**, on **two** reasons: the C-4 scoped delta review's single **P1** (a quotation whose ellipsis cut the clause's own non-exhaustiveness hedge) and the **corpus finding** of 2026-09-20 (the amendment's derivation hardcoded its corpus to one file). Four rounds, a post-sign-off P1, a scoped delta review and two owner decisions; **`P1 == 0 AND P2 == 0` has never been returned.** ⚠️ Never record this as PASS |
| 3 | **`[const §X.6]` / Appendix A — `/clarify`** | ⚠️ **JUSTIFIED-DEVIATION** | **Discharged in substance, NOT by running the skill.** The skill resolves `FEATURE_SPEC` through the fixpp#490 pin and would have written into a shipped, unrelated feature's spec at `rc=0`. The questions were asked and answered by the owner against the design note and are recorded in `spec.md`'s `## Clarifications` (C-1 … C-5). ⚠️ ***"The control ran"* and *"the control's substance was executed by hand"* are different claims; only the second is true.** The by-hand discharge **STANDS** — it was correct when taken, on evidence correct when taken, and a later mitigation cannot convert it into a control that ran |
| 4 | **`[const §X.6]` / Appendix A — `/analyze`** | ⚠️ **FAIL — OWED, NOT DISCHARGED** | ⚠️ **A plan entry claimed otherwise until 2026-09-20 and that claim was FALSE. Do not restate it.** `/analyze` is a cross-artifact check over `spec.md` / `plan.md` / `tasks.md`; **until this bundle has all three it cannot run at all**. It becomes runnable after `/speckit-tasks`, and it is scheduled there — not waived |
| 5 | **`[const §X.6]` / Appendix A — user `/plan` sign-off** | ⚠️ **FAIL — OWED** | The control is **the user's sign-off on the plan**, not the command's execution. It cannot be discharged by this document's existence |
| 6 | **`[const §XVII.1]` — Gate A trigger, public C++ API** | **PASS (engaged, and the C++ track is governed by it)** | The C++-only pair (D-4's eager factory, D-5a/b's `Session::open` extension) touches the public C++ API. ⚠️ **`[const §X.7]` is recorded for it as NOT ENGAGED** — those are not C-ABI symbols, consume no error-code slot, move no version macro and are absent from the byte-freeze manifest. This is the disposition `456-table-view-seal` set for a C++-only break, cited there *"to record that it is **NOT** engaged"* |
| 7 | **`[const §XVII.8]` — verification gate** | ⚠️ **JUSTIFIED-DEVIATION** — and the deviation is **live now**, not future: `gate-a-waived` is **already applied** while the paired evidence it requires **cannot yet exist**, because `/speckit-verify` runs after `/speckit-implement` | `/speckit-verify` is **mandatory after `/speckit-implement`** and must produce `.specify/decisions/090-capi-refusals-verify.md` before any gate label is applied. ⚠️ **`gate-a-waived` is already in force (row 2), and the label-evidence rule makes that label an evidence claim**: it requires `/speckit-verify` **YELLOW** (or GREEN with explicit Codex-side waivers) **and waiver rationales recorded in BOTH the verify record and the PR body**. `/gate-b` refuses to start if the record is absent or RED. The two waiver reasons from row 2 must therefore reach that record and that PR body — this is a **live downstream obligation of a label already applied**, not a future formality |
| 8 | **`[const §XVII.7]` — local pre-PR build gate** | ⚠️ **FAIL — OWED, and resource-gated** (it is a pre-PR obligation, so it is outstanding by sequencing, not by refusal) | Mandatory before opening the PR, with the one-line `local build: green on linux-clang-debug @ <git-sha>` confirmation. ⚠️ **An AI agent MUST surface an `AskUserQuestion` before running it.** Registered NOT MEASURED as the note's §8 item 2, and §8 item 10 **inherits that gate** — it is why every seam's RED except Story 1's is unexecuted. ⚠️ *"The C++ source and the Python binding are present locally"* is **not** the proposition *"the agent may build"* |
| 9 | **`[const §VII.3]` — TDD, red-green-refactor** | **PASS as planned** | Every seam lands RED first, per the note's §6, which pairs each RED with **what could make it green for the wrong reason**. ⚠️ Seam 1's RED asserts a **commit refusal with an empty payload**, not *"the payload differs"* — the two-builder arrangement is the one anybody has actually run. ⚠️ A clean sanitizer run is **not** the instrument for seams 1b and 2c; §1.2 proves it cannot fail. The instrument is **the committed byte string** |
| 10 | **`[const §IX.1]` — coverage; no silent uncovered error path** | **PASS** — by a **disposition pre-registered now** rather than by a percentage, which is what the clause permits | D-2b's bounds refusals are **deliberately assessed-unreachable** after D-1, so they will land as **uncovered** lines. `[const §IX.1]` permits **three** dispositions — tested, waived, or escalated — and this one is **assessed/waived with the assessment written at the site**, recorded in the verify record. ⚠️ Choosing the disposition **here**, before the code exists, is the point: an obligation named before the fact is a disposition; discovered after the fact it is a Gate B finding |
| 11 | **`[const §IX.2]` — sanitizers Tier 1** | **PASS (they run), but NOT as this feature's instrument** | ASan/UBSan/TSan run on every PR and must stay green. ⚠️ **They cannot see #447's defect**: the accumulator is **arena-backed**, so an out-of-range index lands inside a live allocation. A clean sanitizer run is **not evidence of absence** here |
| 12 | **`[const §IX.5]` — ABI check / exported-symbol golden** | **PASS — expected byte-unchanged** | **No symbol is added, removed or re-signed.** ⚠️ Stated as a **check, not a claim**: re-run the golden workflow's own `nm --defined-only --extern-only` diff step. A validation-tightening exports nothing new |
| 13 | **`[const §XI]` — concurrency / coroutines** | **NOT ENGAGED** | No awaitable, strand, executor or lock policy changes |
| 14 | **`[const §XIV.2]` — ≤5 pure-virtual on a pluggable interface** | **NOT ENGAGED** | Nothing here adds a virtual method to anything |
| 15 | **`[arch §5.3]` — no exceptions across the parse→`fromApp` window** | **NOT ENGAGED — but one thunk's trap DOES change, and an earlier row said the opposite** | Every new refusal is an ordinary error return and the translation is `noexcept`. ⚠️ D-3b replaces clone's single blanket handler with a **nested** pair, so a **non-allocation** exception now **aborts after a fatal log** where it returned a code before. That moves **toward** the invariant-violation rule, not away from it — but the row must not claim the traps are untouched, and must not attribute the trap to a construct **nothing implements** (fixpp#487) |
| 16 | **`[arch §2.3]` — module layering** | **PASS** | D-5b's predicate lives in a **session** leaf header. The allowed-edge whitelist **explicitly permits** the C-ABI → session edge, and the C-ABI config translation unit already includes session headers. ⚠️ v0.1's layering argument for putting it in `core` was **FALSE against the clause it cited and is retracted**; the home is chosen on **ownership** |
| 17 | **`[[clang::lifetimebound]]` / `[[nodiscard]]` conventions** | **PASS** | The view accessor already carries `lifetimebound` and its signature is unchanged; the factory is already `[[nodiscard]]` and expected-returning and **widens its failure set, not its signature**. **No new view-returning accessor and no new expected-returning method is introduced.** D-5b's predicate is `[[nodiscard]]` |
| 18 | **`[const §XIX.5]` — pages tied to public API surfaces regenerated** | **NOT ENGAGED — and the reason is that there is no generation step to drift** | Executed in the note's §5a: **no Doxyfile anywhere outside the build tree**, **no Doxygen invocation** in the top-level `CMakeLists.txt`, `cmake/` or any workflow, against a **positive control on the same corpus**. `docs/` is an mdBook whose only generator is a deny-by-default allowlisted **copier** that reads no header. ⚠️ Adding a regeneration task that cannot be executed would be a worse outcome than recording the clause as inert. Same disposition, same grounding, as `456-table-view-seal` reached on the identical clause |
| 19 | **`[const §VI]` — catalogue and coverage-index traceability** | **PASS as planned — two EDITS OWED, both derived and printed in §5d** | The catalogue's `CA-009` row: its **Title enumerates symbols and its Status tracks delivery, and neither moves** — what moves is its **`Tests` column**, where §6's new seams land. The coverage index: its reify-mechanism block calls the view **lazy**, which **D-4 falsifies** — that is a live claim, not a number that merely narrows |
| 20 | **`[const §XVI.7]` — `/simplify` before `/speckit-verify`** | **PASS as planned** | Scheduled at pipeline step 9.5. ⚠️ A post-`/simplify` source change invalidates every preset build directory and forces the whole verify matrix to re-run, so it must precede verify — not merely precede PR open |

---

## Project Structure

### Documentation (this feature)

```text
specs/090-capi-refusals/
├── spec.md              # Requirements input — EXISTS (not this phase's to edit)
├── plan.md              # This file (/speckit-plan command output) — Phase 0
├── research.md          # Phase 0 output (/speckit-plan command) — created with this file
├── data-model.md        # Phase 1 output — DOES NOT EXIST YET
├── quickstart.md        # Phase 1 output — DOES NOT EXIST YET
├── contracts/           # Phase 1 output — DOES NOT EXIST YET
└── tasks.md             # Phase 2 output (/speckit-tasks — NOT created by /speckit-plan)
```

⚠️ **The three Phase 1 entries are listed as obligations, not as present files.** `/analyze` cannot
run until `tasks.md` exists (Constitution Check row 4).

### Source Code (repository root)

```text
include/fix/c_api/                  # the C-ABI surface — byte-frozen; the manifest's header
                                    #   count is printed by tools/check_capi_freeze.sh beside its
                                    #   verdict, so it is read there and NOT written here
├── message.h                       # remove_tag, clone, and D-2b's seven observable declarations
├── session.h                       # the two config setters
├── version.h                       # FIXPP_C_ABI_VERSION_MINOR + its trailing comment (part of the pin)
└── error.h                         # UNCHANGED — no enumerator is minted (FR-007)

include/fixpp/
├── dict/reify.hpp                  # D-4: the eager factory, the handle, and reify()
└── session/
    ├── session_config.hpp          # CompIDs + BeginString gain the D-5b floor as a precondition
    ├── session_types.hpp           # the configured RefMsgType(372) member
    └── <new leaf header>           # D-5b: the ONE definition of the policy-floor predicate

src/
├── capi/
│   ├── message_write.cpp           # ONE translation unit carries THREE of the changes:
│   │                               #   D-1's guard, D-2b's bounds refusals and the resolvers,
│   │                               #   AND fixpp_msg_clone itself — D-3's translate() plus
│   │                               #   D-3b's nested boundary. ⚠️ There is no src/capi/message.cpp;
│   │                               #   verified with `grep -rln fixpp_msg_clone src/ include/`
│   ├── config.cpp                  # D-5c: the two setters call the predicate
│   ├── error.cpp                   # translate() REUSED; introducing_minor() UNCHANGED
│   └── version.cpp                 # Tier-2 prose pin, ALREADY STALE — fix opportunistically
└── session/session.cpp             # Session::open's lambda REPLACED by the predicate call

tests/
├── capi/                           # seams 1a/1b/2/2b/2c/3/3b/5/7 + the existing cells §5d dispositions
├── dictionary/                     # seam 4 — the reify factory's refusal
├── wire/                           # the shipped pins D-4 must NOT move
└── abi/golden/                     # expected BYTE-UNCHANGED; the golden workflow checks it

bindings/python/                    # seam 6 — the same refusal through the binding
tools/
├── capi_freeze.sha256              # THREE headers re-baseline (see below)
├── check_capi_freeze.sh            # must be observed FAILING, then PASSING
└── abi_history/                    # UNCHANGED — no code is minted

spec/behaviors-and-limitations.md   # the LIVE ledger: four behaviour rows, no limitation row
spec/feature-catalogue.md           # CA-009's Tests column only
spec/coverage-index.md              # the reify block's "lazy" clause, which D-4 falsifies
.specify/2i-capi.md                 # FR-019: eight passages + the "CI grep enforces" deletion (C-2)
.specify/api-contract.md            # OD-1: the code-scoping parenthetical
.specify/2m-pybind.md               # OD-1: the construction-failure-modes limb + its positional citation
```

**Structure Decision**: **single-project library layout**, which is what this repository already is
— public headers under `include/`, implementation under `src/`, GoogleTest suites under `tests/`
mirroring the module tree, benchmarks under `bench/`, the SWIG binding under `bindings/python/`,
and gate scripts plus the freeze manifest under `tools/`. The template's web-application and
mobile+API options are removed: neither exists here. **No new module or directory is created.** The
one genuinely new file is D-5b's session config-validation leaf header, which depends on nothing but
`<string_view>` and is placed so that both the C-ABI configuration path and the session
implementation can reach it by a **permitted** layering edge.

**Three coupling notes a reader meets here rather than at the end of implementation:**

1. **The freeze re-baseline is caused by obligation 2, not by the version bump.** The version header
   re-baselines because its macro moves; the session and message headers re-baseline because
   **obligation 2 requires editing doc comments in those files**, and the manifest hashes **bytes,
   comments included**. ⚠️ Expect this at the **start** of implementation. The gate must be observed
   in **both** states — failing after the edits, passing after the re-baseline; **a green result
   alone is consistent with a re-baseline applied before the edits and is not evidence.**
2. **Do not re-date #428's markings.** The `(1.6, BREAKING)` / `Since 1.6` mentions and the
   `C-ABI 1.6` banners correctly date **#428**; re-dating them would falsify history. They stay —
   but any *other* edit in the same header still re-triggers its freeze hash.
3. ⚠️ **OD-1's two populations are NEVER SUMMED and NO TOTAL IS WRITTEN.** The `[2i]` scope-claim
   passages are bound by a criterion over passages that bind the code to a producer set; the two
   derived restatements are a **separate** population on a **separate** ground (each cites its
   source). *Three*, then *eight*, were each falsified by the next pass — **a corrected count is the
   same defect at a new value.**

### What Phase 0 deliberately leaves open

**FR-004 / D-2b's bounds-check shape** — whether the check lives in the index resolvers or at their
callers. `spec.md` hands this to `plan.md` as an **implementation choice**, and choosing it is not
Phase 0's job; leaving the **constraint** visible is the value. The constraint, which is **not**
open:

- both resolvers are `noexcept` and return raw pointers, so **the refusal cannot be a return value
  from them** — it must be a checked precondition at their callers, or the functions must change
  signature;
- whichever shape is chosen must discharge **all three reachability classes**, and **a
  null-returning resolver discharges class (3) only if `builder_context` and the two internal uses
  are rewritten with it**;
- the **observable declaration population** is a design fact already derived in the note's §5a, with
  a stated re-derivation recipe — it is not deferred with the shape. ⚠️ Counting *call sites*
  **under-counts declarations**: one non-exported call site fans out to four exported setters.

---

## Complexity Tracking

> **Fill ONLY if Constitution Check has violations that must be justified**

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| **Row 2 — `[const §X.1]`/`§X.6` Gate A ran and DID NOT CONVERGE; label `gate-a-waived` on TWO reasons** | Gate A is mandatory on any C-ABI change and it **did run** — four rounds, a post-sign-off P1, a C-4 scoped delta review. The two open reasons are the scoped review's **P1** (a quotation whose ellipsis cut the clause's own non-exhaustiveness hedge) and the **corpus finding** (the amendment's derivation hardcoded its corpus to one file). Both were **applied**; what was never returned is a `P1 == 0 AND P2 == 0` verdict | **`gate-a-done` is rejected**: the `0 P1 / 0 P2` branch did not fire, and under `[const §XVII.8]` a gate label is an **evidence claim, not a status decoration**. **A further full re-read is rejected** as disproportionate to a scoped delta (C-4). ⚠️ The waiver's rationale must reach **both** the verify record and the PR body, or the label is unsupported |
| **Row 3 — `/clarify` discharged in SUBSTANCE, not by running the skill** | The skill resolves its target through the fixpp#490 pin and would have **written into a shipped, unrelated feature's spec at `rc=0`** — a destructive outcome with no error. The questions were asked and answered by the owner against the design authority and are recorded as C-1 … C-5 | **Running the skill is rejected** on that destructive resolution. **Recording it as "the control ran" is rejected** because it is false: *"a control discharged by an adapted procedure is recorded as adapted, not as run."* ⚠️ fixpp#490 is **mitigated, not fixed** — the pin now happens to point here, which makes the defect **silent rather than loud**, and a defect that resolves correctly by accident is more dangerous than one that resolves wrongly |
| **Row 4 — `/analyze` OWED, NOT DISCHARGED** | It is a cross-artifact check over `spec.md` / `plan.md` / `tasks.md`. **In issue mode none of those existed**, which is precisely why OD-2 converted this work to feature mode. With this plan it has two of three; `tasks.md` completes the set | **Claiming it discharged is rejected — a plan entry did exactly that and the claim was FALSE.** **Waiving it is rejected**: it is not unrunnable, merely not yet runnable, and it is scheduled at `/speckit-tasks`. ⚠️ A control's discharge is read from the artifact that owes it, **never from a plan entry describing it** |
| **Row 5 — user `/plan` sign-off OWED** | Appendix A puts all four controls on an ABI surface change, and this one is **the user's act**, not an agent's | **Treating the plan's existence as the sign-off is rejected**: the control is the **sign-off**, not the command's execution. No document can discharge it on the user's behalf |
| **Row 7 — `[const §XVII.8]`: `gate-a-waived` is applied while its paired evidence cannot yet exist** | The label records a **real, already-completed** Gate A outcome (ran, did not converge, two reasons), and Gate A precedes implementation. `/speckit-verify` runs **after** `/speckit-implement`, so the record the label-evidence rule demands is **not producible at this point in the pipeline** | **Removing the label is rejected** — it would misrepresent a gate that genuinely ran and would let a downstream step read the bundle as un-gated. **Treating the label as self-supporting is rejected**: the rule is explicit that *"the labels are evidence claims, not status decorations"*, and applying them by hand without the records is a constitutional violation. The deviation is therefore **closed by producing the record before merge**, with **both** waiver reasons in **both** the verify record and the PR body — `/gate-b` refuses to start on an absent or RED record, which is the enforcement |
| **Row 8 — `[const §XVII.7]` local build OWED, and every seam's RED except one is unexecuted** | Local builds are resource-heavy and an agent **MUST** surface an `AskUserQuestion` first. The note pre-registers this as §8 item 2, and item 10 **inherits** it — every seam RED except Story 1's needs a cell written **and built** | **Claiming the REDs is rejected**: an unexecuted RED stated as a result is the defect class this feature's authority was written against. **Auto-running the build is rejected** by the resource gate. If the user approves the build, those REDs become executable and the row is **discharged, not re-parked** |
| **Row 15 — `[arch §5.3]` not engaged, yet one thunk's exception trap DOES change** | D-3b narrows a blanket handler that today advertises **every** exception as a retryable configuration result. The nested boundary keeps the allocation return and **aborts after a fatal log** on anything else, matching the steady-state disposition the owner document prescribes | **Leaving the blanket handler and merely documenting its return is rejected**: that converts an untested catch-all into a **published guarantee**, which is a contract change made without reviewing it against the contract. ⚠️ The row must not claim the traps are untouched, and must not attribute the trap to a construct **nothing implements** — the two named guard flavours have **no source-level implementation** (fixpp#487) |
| **Row 10 — `[const §IX.1]` uncovered lines by design (D-2b)** | The bounds refusals are **defence in depth**, assessed **unreachable** from the C ABI once D-1 lands. Their value is that the outcome is a **defined error** instead of undefined behaviour | **Omitting the check is rejected** — that leaves undefined behaviour reachable in principle. **Claiming the lines exercised is rejected.** `[const §IX.1]` permits three dispositions; this is the **assessed** one, written at the site and recorded in the verify record, pre-registered **before** the code exists rather than discovered at Gate B |

---

## Constitution Check — POST-DESIGN RE-EVALUATION (after Phase 1)

*The gate above was evaluated before Phase 0. The skill requires a re-check after Phase 1 design.
This is that re-check.*

**Verdict: NO ROW CHANGES.** Phase 1 produced `data-model.md`, six `contracts/` files and
`quickstart.md`. It made **no decision** — every one was already settled in the design authority and
transcribed in `research.md` — so no gate's basis moved.

⚠️ **One Phase 1 act COULD have changed a verdict, and is recorded here rather than left implicit.**
Phase 1 added a contract file the plan's four-symbol list did not name — `msg-index-bounds.md`
(D-2b / FR-004 / SC-003) — because `§5a` of the design authority declares that surface gains a
documented `FIXPP_ERR_INVALID_HANDLE` return, and omitting it would have left an FR uncovered.

**It does NOT enlarge the `[const §X.7]` BREAKING population**, and the discriminator is the design
authority's own: *"the arm it replaces is undefined behaviour, not a documented success."*
`[api-contract §11]` defines the C-ABI breaking effect as **"making a call to a Stable-from-v1.0
C-ABI symbol fail where it used to succeed"** — undefined behaviour is not a success that can be
taken away. The file is therefore deliberately **not** marked BREAKING, and is kept **separate** so
no reader inherits BREAKING from an adjacent section.

**The four `FAIL` rows are unchanged and all four remain SEQUENCING failures, not violations:**
`/analyze` (owed), the user `/plan` sign-off (owed), `[const §XVII.7]`'s local pre-PR build (owed by
sequencing), and obligation 4 as their roll-up. ⚠️ **Phase 1 discharged none of them and claims
none.** Gate A remains **`JUSTIFIED-DEVIATION`** — ran, did **not** converge, `gate-a-waived` on two
reasons.

**Re-derivation recipe, not a result:** re-read the Verdict column against
`.specify/constitution.md` and the design authority's `§5e`; a row changes only when a decision
changes, and Phase 1 changed none.

---

## Phase 0 exit state

- **Produced**: this file and [research.md](./research.md).
- **Not produced, and not this phase's**: `data-model.md`, `contracts/`, `quickstart.md` (Phase 1);
  `tasks.md` (Phase 2).
- **Unresolved clarifications**: **none.**
- **Owed before merge, carried forward so nothing below is re-discovered as a finding**:
  `/analyze` (row 4) · user `/plan` sign-off (row 5) · the `[const §XVII.7]` local build and the
  seam REDs it gates (row 8) · `/speckit-verify` with the `gate-a-waived` rationales in **both** the
  record and the PR body (row 7) · the PR body's `##` BREAKING **heading** in #428's spelling
  (row 1b) · the freeze gate observed **failing then passing** (row 1a/structure note 1) · the
  catalogue and coverage-index edits (row 19).

