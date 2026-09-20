# fixpp#456 — sealing `table_view`'s mutation surface

> **Status: v0.4 — Gate A CONVERGED at round 3, 2026-09-19.** Supersedes v0.3.
>
> - **Round 1: BLOCKED** — Codex `1 P1 / 4 P2`; Opus adversarial `2 P1 / 3 P2 / 5 P3` post-judging.
> - **Round 2: BLOCKED** — Codex `2 P1 / 1 P2`; Opus adversarial `2 P1 / 2 P2 / 3 P3` post-judging,
>   three root causes.
> - **Round 3: CONVERGED** — Codex `0 P1 / 1 P2`; Opus adversarial `0 P1 / 0 P2 / 2 P3` post-judging,
>   verdict *"Gate A converges"*. Recorded in `.specify/decisions/456-table-view-seal-gatea.md`.
> - **No finding across all three rounds touched the design.** Convergence is `P1 == 0 AND P2 == 0`;
>   it was reached at round 3. ⚠️ v0.2's header said *"Gate A round 1 converged"* against its own
>   appendix's `1 P1 / 4 P2`. Record a tally; record a convergence only when a round returns zero and
>   zero — the shape `.specify/426-428-length-data-pairs.md` uses, and the shape this status block now
>   follows.
>

> **Owner:** Opus (Phase A designer). **Issue:** fixpp#456. **Tree:** `main` at `f49f462a`, clean.
>
> **Inherits.** This document is the continuation of `.specify/215-dictionary-view.md` §7's
> residue — *"**`table_view`'s public mutation surface** (Option B's target). Left open. If it is
> ever privatized, that is a module-scale change touching 27 test TUs and **needs its own gate**;
> Option C is chosen precisely so this decision does not depend on it."* **This is that gate.**
> §4 of that document adjudicated Options A–F and is **not reopened here** (§1a).
>
> **Cites.** `.specify/215-dictionary-view.md` (v0.4, Gate A converged at round 3) ·
> `.specify/426-428-length-data-pairs.md` §3 (the bundle-snapshot paragraph that names this issue) ·
> `.specify/constitution.md` · `spec/behaviors-and-limitations.md`.
>
> **Trigger.** `[const §XVII.1]` first bullet — *"Touches the public C++ API or C ABI"*. This is a
> **source-breaking** change to a public C++ type. Also `[const §XVII.1]` last bullet — *"Any new
> design document under `.specify/` … qualifies by default"*.
>
> **Convergence log:** Appendix, at the end. It carries one section per revision: v0.1 → v0.2
> responds to round 1, v0.2 → v0.3 responds to round 2, v0.3 → v0.4 responds to round 3 and records
> Gate B round 1's post-convergence fix queue. Disagreements and declined prescriptions are recorded
> there with their reasoning rather than applied silently, and each rewrite's own re-runs added
> further rows.
>
> **Citation style.** No line numbers anywhere in this document, by `tools/check_line_citations.py`'s
> own rule — *"THE FIX FOR A FLAGGED CITATION IS TO DELETE THE NUMBER, NOT TO CORRECT IT … Cite a
> function/struct name plus a short quoted phrase instead."* Every anchor below is a symbol name or a
> quoted phrase that `grep` finds after arbitrary line motion. ⚠️ Note for anyone amending this
> document: `.specify/` **is** in that tool's `SCAN_DIRS` and `.md` **is** in its extension list, so
> an added `file.cpp:NNN` here fails the addition gate. Run
> `python3 tools/check_line_citations.py --range origin/main..HEAD` before pushing.
>
> ⚠️ **While this document is UNTRACKED, that gate reports clean because it cannot report anything
> else.** `--range` reads commits and `--staged` reads the index; an untracked file is in neither,
> so both print *"no new line-number citations … OK"* over a file they never opened, and `git add
> -N` does **not** fix it (an intent-to-add entry produces an empty `git diff --cached`). Stage the
> file for real, run `--staged`, then unstage. v0.3 did exactly that: the gate read **2452 added
> lines** and returned `OK` at `rc=0`, and it was **proven able to report non-zero on that same
> invocation** by seeding one `path:NNN` citation into the staged content — `rc=1`, form `[A]`,
> quoting the seeded line — after which the file was restored `cmp`-identical and the gate returned
> `OK` again.
>
> **Every figure in this document was executed at `f49f462a` and is printed beside the command that
> produced it**, and the two census instruments behind the headline numbers are **checked in** under
> `tools/` (§2a) — the rest are greps and compiler probes whose full command is given inline. Where a
> figure could not be executed it says **NOT MEASURED** and names what would measure it. Where an
> instrument could report a plausible wrong number, the guard that prevents it is stated alongside —
> and each such guard was itself **proven able to fire** (§2a).
>
> ⚠️ **What changed in v0.2 and again in v0.3, in one line: nothing in the design.** S1 plus deleted
> assignment, the `build() &&` ref-qualifier, the scalar readbacks, the `peek()` refusal and the §1a
> narrowing have now survived two adversarial passes unchanged, and every headline figure reproduced
> exactly on re-run. Both revisions harden **claims, instruments and censuses** — neither reopens a
> decision. **v0.3's substance is:** the sweep's classifier now carries the compiler's return code
> (§2a guard 7); the scoper now reads `auto`-spelled declarations and enforces that every mutator
> call is attributed (§2a, §5d); seam 2's completeness claim is **withdrawn**, not respelled; and
> §5d gains the seventh migration class the scoper was blind to.

---

## 1. What is actually being decided

`fixpp::dict::table_view` (`include/fixpp/dict/table_view.hpp`) documents itself as *"Constructed
ONCE at session/validator setup time … Immutable after construction"* and enforces none of it.
Sixteen non-`const` members sit in its public span, and copy- and move-assignment are public too.
A caller who owns a published view can repopulate it at any time.

The decision is **how to make that contract a property of the type**, and it has exactly two open
questions, both inside a single surviving design family (§1a):

1. **Must assignment be deleted as well as the mutators?** → §3.2; answered **yes** in §4.
2. **What migration shape does the test tree take?** → §3.3 and §5d.

Everything else this gate could argue about is already adjudicated, and §1a disposes of it so that a
review round is not spent re-deriving it.

### 1a. Constraints disposed of up front, so they are not relitigated

**(i) Options A–F are settled. Do not reopen them.** `.specify/215-dictionary-view.md` §3–§4
enumerated six options for the injection point and chose Option C (`dictionary_snapshot`, shipped).
Its **Option B** is precisely this issue's fix and was rejected **on migration cost only** — §4:
*"B fixes the better-argued defect at the root but leaves C4 open and costs 27 test TUs."* Cost is
not a reason the fix is wrong; it is a reason it needed its own gate, which is this one. Option C
explicitly does not subsume it (§3, Option C): *"Option C does **not** make `table_view` immutable …
callers can still mutate a `table_view` they own; they can no longer hand a session one they can
still mutate."* A caller who owns a view and builds a `dict_hooks` bundle from it — #456's
scenario — is exactly the case Option C left open.

**(ii) Three designs are already dead, by one sentence each.** #456's own witness mutates a
**default-constructed** `table_view` that never came from a `Dictionary`. Therefore **any design in
which a `table_view` *value* retains a reachable mutation surface fails the witness**, which kills:

| dead design | why, in one line |
|---|---|
| a runtime `frozen_` flag | it cannot produce a **compile-fail** RED (the batch plan's stated RED), and the `mutable` variant needed to freeze from `for_table_view(table_view const&)` would race on the shipped cross-thread sharing path that `215-dictionary-view.md` §2a's `grep -nE '\bmutable\b'` → nothing is what establishes |
| freeze at `Dictionary::as_table_view()` | it never runs on a default-constructed view — the witness's own object |
| a mutator **facade** holding `table_view&` | a caller can always construct a second facade over the same view |

**I agree with that narrowing and adopt it.** What survives is one family: **mutation lives on a
distinct owning type that is CONSUMED to produce the view.** §3 chooses inside that family and
nowhere else.

**(iii) The C ABI does not move, and the granted ABI latitude is declined.** `table_view` appears in
**no** `include/fix/` header:

```
grep -rc "table_view" include/fix/ | grep -v ":0"   ->   no output   (executed at f49f462a)
```

Its only C-ABI-side uses are members of the internal struct in `src/capi/capi_internal.hpp` and one
assignment in `src/capi/message_write.cpp`. `tools/check_capi_freeze.sh` hashes only `include/fix/`
headers, so it needs no manifest edit. The ABI-golden gate (`.github/workflows/abi-golden.yml`) is an
`nm --defined-only --extern-only` diff of the exported `fixpp_*` symbol set against
`tests/abi/golden/fixpp_capi_symbols.txt`; a header-only C++ class emits no such symbol. **Claim:
that golden file is byte-unchanged.** Check: re-run the workflow's own step, *"Check C-ABI exported
symbol set against golden"*. `[const §X.7]` (pre-first-release C-ABI breaks must be declared) is
therefore **not engaged** — this is a C++ source break, not an ABI break.

**(iv) Copy CONSTRUCTION must survive, unconditionally.** `src/session/session.cpp` copy-constructs
the strict validator from `*inbound_tv_`; `membership_copy()` in `include/fixpp/wire/parser.hpp`
copy-constructs; `tests/wire/validator_domain_test.cpp` carries
`static_assert(std::is_copy_constructible_v<fixpp::dict::table_view>, "dict::table_view (mock) must
be copy-constructible for by-value hold")`; and `wire::dictionary_driven_validator` holds a
`table_view` **by value** behind the frozen SC-007 *"no virtual edge"* design point. No design here
may touch it. §6's seal witness asserts its survival positively, in the same TU as the negatives.

**(v) The default constructor stays public.** Sealing the *mutators* already kills the witness; a
sealed empty view is not a hazard. Keeping it is also a migration advantage: the `table_view tv;`
declarations still compile, so the migration's compile errors land on the mutator lines and
nowhere else. §5d depends on this.

---

## 2. The defect, and its measured witness

`wire::dict_hooks::for_table_view` — declared in `include/fixpp/wire/dict_hooks.hpp`, defined in
`include/fixpp/wire/parser.hpp` because *"it needs both `fixpp::dict::table_view` and `group_context`
complete"* — reads `table_view::has_nonstandard_pair()` **once**, at bundle-build time, and installs
the Length+Data pair callback only when it is set (fixpp#426 design §3: this keeps a per-field
dictionary lookup off every scanner). A bundle is therefore a **snapshot of one bit**:

```cpp
fixpp::dict::table_view tv;
auto const hooks = fixpp::wire::dict_hooks::for_table_view(tv);
tv.set_length_pair_data_tag(5001, 5002);
hooks.data_tag_for_length(5001);   // 0 — the bundle predates the pair
```

**The remaining callbacks are NOT snapshots.** `for_table_view` stores `std::addressof(dict)` in
`dict_hooks`'s `void const* opaque_dict_`, and every membership and delimiter lambda casts it back
with `static_cast<fixpp::dict::table_view const*>(d)` and reads it **live**. So the defect is scoped
precisely: **one bit is latched; the rest is a live raw pointer.** That second half is a *different*
hazard — a bundle outliving its view is a dangling read — which this design does **not** close. §7
records it, here rather than leaving it for Gate B to find.

**Reachability today.** No shipped path can observe the latched bit: a `table_view` is populated by
`Dictionary::as_table_view()` and then left alone, and every production bundle is rebuilt from it
(`Validator::validate`, `Session`'s scanners, the C-ABI setters) — except `Parser`, which builds one
in its constructor and keeps it for its own lifetime (fixpp#426 Gate B r7 N-4). What is missing is
that the premise is a **temporal rule kept by convention**, not a property of the type.

**The witness, and it is load-bearing.** `DictHooksCustomPair.ABundleIsASnapshotOfTheDictionaryItWasBuiltFrom`
in `tests/wire/dict_hooks_custom_pair_test.cpp` pins the behaviour, and its own comment names this
issue: *"The TYPE does not enforce that order, so the behaviour is pinned here rather than left to be
discovered. Gate B r6 M-1; sealing the published view is fixpp#456."* Its premise is
mutate-after-publish. **Under every design in the surviving family the POPULATION half becomes
unconstructible; the IDENTITY half — re-seating the object a bundle latched against, e.g. via
`std::optional<table_view>::emplace` — does not (Gate B round 1 finding).** §5c and §5d decide what
happens to it, as a disclosure rather than a cleanup.

### 2a. The two instruments used throughout this document — CHECKED IN, and hardened

Rather than grep for mutator names, this gate **simulates the seal and compiles the tree against
it**. Both instruments ship with this change, under `tools/`, because *a design-time instrument that
is not preserved cannot be re-run by a reviewer*:

| instrument | what it answers |
|---|---|
| `tools/table_view_seal_sweep.py` | which TUs stop compiling under the seal, and with how many diagnostics |
| `tools/table_view_mutation_scope.py` | which mutated `table_view` declarations are pure build-then-use, which interleave — and whether **every** mutator call in a scanned file is attributed to a declaration it found (the attribution guard, §5d) |

⚠️ **v0.1 quoted both instruments' output without shipping either, and described the sweep's control
rule as something the script did not do.** Every figure it quoted was re-run by the adversarial
review and reproduced exactly — the defect was never in the arithmetic. It was that the numbers
rested on a script living in a session scratchpad, run by hand, whose control failure had been
adjudicated by a human and reported as the instrument working. Both halves are closed below.

#### The six ways this sweep could report a plausible wrong number, and the guard for each

Each guard **raises and exits non-zero before printing any figure**. None of them is optional, and
none is behind a `--self-test` flag — a witness someone can skip is the procedure nobody runs.

⚠️ **Read that as a property of the six guards below, not as "nothing can fail toward clean."** v0.2
made the same sentence and it was true of every guard it had — the failure was that a whole
condition (a compilation that fails without a classified diagnostic) had **no guard at all**, so
there was nothing for the sentence to be false about. What the sentence can carry is that each
listed guard raises before any output; what it cannot carry is that the list is complete. Round 2
lengthened it by one; a round-3 finding would lengthen it again.

1. **A relative `--scratch` silently degrades the sealed arm into the control.** The sweep runs each
   TU with `cwd=<build dir>` and injects `-I<scratch>/inc`. A *relative* scratch path resolves
   against the build directory, points at nothing, and **clang ignores a nonexistent `-I` with no
   error, no warning and no `rc`** — so the sealed arm recompiles the real header and reports zero.
   **The control pairing cannot catch this**: a degraded sealed arm agrees with the control
   perfectly, which is precisely what the pairing checks for. The adversarial review hit it by
   accident and it cost a false conclusion. Guard: `--scratch` is resolved with `os.path.abspath`,
   and a missing or empty include root is fatal.
2. **A run that did not shadow the header can still print a number.** Guard: the sealed arm seeds
   `#error FIXPP_456_SEAL_SWEEP_SHADOW` into the scratch header, compiles one TU, and **asserts the
   token appears**, every run. This is the technique §5d already applied to the typed-group overlay,
   now applied to the instrument that produces every headline number in this document.

   ⚠️ **The control arm runs the mirror check, and it is deliberately weaker — say so rather than
   claim two-sidedness the guard does not have.** The control injects no `-I`, so the token is
   unreachable and *that* assertion cannot fail on any input; it prints `ABSENT` unconditionally. It
   is a **guard on the script**, not evidence about a run: it fires if the `-I` injection ever
   becomes unconditional, which would silently make the control a second sealed arm and drive the
   differential to zero. The output labels it `shadow guard [control] … script-level only` so it
   cannot be misread. **It was mutation-proven in that role** — the inverse of the degradation
   mutant.
3. **A filter that selects no TU including `table_view.hpp` makes the sweep vacuous.** Guard: the
   witness TU is chosen by a dependency scan, and *no swept TU includes the header* is fatal.
4. **A must-fail negative-compile probe can absorb a seal error inside its expected failure.**
   Guard: `EXPECTED_CONTROL_FAILURES` binds each of the three probes to its intended diagnostic. The
   control arm passes on **identity**, not on count — a listed probe that fails differently, a listed
   probe that stops failing at all, and any unlisted control failure are each fatal. In the sealed
   arm a listed probe carrying any diagnostic other than its bound one is fatal too.
5. **A default error limit turns a count into a ceiling.** `-ferror-limit=0` is load-bearing: clang
   stops at 19, and the first run of this sweep reported `19` for eleven different TUs. The real
   per-TU figures range from 1 to 92.
6. **A compilation can FAIL without producing anything the classifier recognises** — and v0.2's
   classifier then reported it as a clean TU with zero errors. *(This is the script's **guard 7**;
   its guard 6 is the include-ordering property recorded in the ⚠️ note immediately below, which
   v0.1 discovered and which was never a numbered item here.)* v0.2's `run_arm()` discarded
   `subprocess.run(...).returncode` and defined a failing TU **solely** as stderr lines matching
   `": error:"`. **The boundary is exactly the colon**: in `clang: fatal error:` the ` error:` is
   preceded by a *space*, so it matches nothing — and a signal death (`rc=-11`, empty stderr) or an
   ICE (`rc=254`, a stack dump) matches nothing either. All three were reported **clean**.

   ⚠️ **What it cost in each arm, because the two costs are different and only one of them is a
   weak check.** In the **control** arm such a TU never enters `bad`, so guard 4's *"any unlisted
   control failure is fatal"* rule — this document's own identity check — **never sees it**: the
   guard is not weak there, it is **unreachable**. In the **sealed** arm the same TU is absent from
   the set difference, so **the bill is undercounted** and `--emit-set` — the file §5d calls *"what
   a migration should read"* — silently omits it.

   Guard: every compilation carries its return code. `rc == 0` with no classified diagnostic is the
   only clean state; `rc != 0` with nothing classified is fatal, printing the rc and the head of
   stderr; `rc == 0` *with* something classified is fatal too, because the two halves disagree and a
   census must not choose between them. A listed must-fail probe must additionally exit **non-zero**
   as well as carry its bound diagnostic. The dependency probe in `pick_witness()` had the same
   defect — a failing `-MM` read as *"this TU does not include `table_view.hpp`"*, which moves the
   witness silently and, if every probe fails, raises the **wrong** fatal (*"this sweep is
   vacuous"*) for a broken probe — and it checks `rc` now too.

   ⚠️ **Why this survived two rounds, which is worth more than the bug.** Re-running a figure on a
   tree where everything compiles exercises the classifier's **clean branch only**. Re-running a
   measurement cannot find a hole in the path that measurement never takes — and all seven guard
   mutants below targeted the **guards**, none the **classifier**. The fix is therefore not only the
   return code but the mutant class: the classifier is now proven by **injecting compiler results**
   rather than by compiling code (second table below). ⚠️ **And the untaken branch is about to be
   taken**: §5d and seam 5 both mandate a re-run on a `FIXPP_BUILD_FUZZ=ON` configuration that has
   never been built here, which is exactly where a driver-level `fatal error: 'X.hpp' file not
   found` is likeliest and the one run whose result cannot be sanity-checked against a prior figure.

⚠️ **v0.1 recorded one further failure — the script's guard 6, the scratch `-I` appended *after* the
build's own, reporting zero errors everywhere — and closed it by reordering a flag, with no witness
that the reorder took effect. A fix without a witness is the condition, not the cure.** Guard 2 is
that witness; the
ordering itself is now a property of the script (the scratch root is injected ahead of every flag the
compile database carries) rather than of how someone typed the command.

#### The guards were proven able to fire

Mutation-tested by running a **copy** of the script from a scratch directory, each copy asserted to
differ from the original before it runs — so a mutation that failed to apply cannot read as a pass.
Every guard raises the same `Fatal` and exits `2` before any arm result is printed; the exit status
was checked directly on the first mutant, and the rest are identified by their diagnostic:

| mutant | expected | observed |
|---|---|---|
| the sealed arm never injects `-I<scratch>` — i.e. **exactly the N-P1-1 degradation** | fatal, before any number | `rc=2`, *"SHADOW NOT WITNESSED … the sealed arm compiled the REAL header and its number would be the control's"* |
| a filter selecting TUs none of which include `table_view.hpp` (`--filter /tests/tls/`) | fatal | `rc=2`, *"NO SWEPT TU INCLUDES fixpp/dict/table_view.hpp — this sweep is vacuous"* |
| the source header is **already sealed** — i.e. a post-implementation re-run (a root whose `table_view.hpp` is the tool's own sealed output) | fatal, named | *"SOURCE HEADER DOES NOT MATCH THE SIMULATION ANCHORS: copy-assignment definition, move-assignment declaration. Either the header moved, or the seal has ALREADY LANDED."* |
| the `-I` injected **unconditionally** — the inverse of the first mutant, which is what the control-side guard is FOR | fatal | *"CONTROL ARM IS SHADOWED … the control is not a control and the differential would collapse toward zero"* |
| a probe **dropped from the manifest** (`--filter /tests/session/`) | fatal | *"control failure not in the expected-failure manifest (1 error(s)) — the harness is broken"* |
| a manifest entry naming a TU that **compiles clean** | fatal | *"expected to FAIL and did not — it has stopped being a must-fail probe"* |
| a probe bound to a **diagnostic it never emits** | fatal | *"1 diagnostic(s) other than its bound …"* |
| **NEGATIVE CONTROL — the unmutated script, same `--filter /tests/session/` (195 TUs), control arm** | **rc=0** | `rc=0` — seven mutants that all raise are also consistent with a script that always raises; this row is what excludes it |

⚠️ **All eight rows were RE-RUN against v0.3's edited script**, not carried over: guard 7 changes
`run_arm()` and `pick_witness()`, which five of the seven mutants pass through, so a mutation-proof
table quoted from a script that no longer exists would be exactly the fossil this document is about.


⚠️ **The `--filter /tests/tls/` mutant is worth reading twice.** It was written to test the
expected-failure manifest — and guard 3 fired first, because *none of the TUs holding the three
negative-compile probes includes `table_view.hpp`*. That is the vacuity guard catching a sweep the
author intended to run. The manifest guard is therefore exercised over `--filter /tests/session/` —
which carries one of the three probes **and** TUs that include `table_view.hpp`, so both guards
are satisfiable at once.

#### The classifier was proven able to fire — by INJECTING compiler results, not by compiling code

⚠️ **The seven mutants above cannot reach the classifier, and that is why guard 7's defect survived
two review rounds.** Each of them mutates the *script* and then runs a *real* compiler over a tree
where everything compiles, so the classifier is exercised on its clean branch and nowhere else. The
classifier is therefore mutated the other way round: the script is left **byte-unchanged** and the
compiler *result* is injected, by stubbing `subprocess.run` for the measurement compile only.

Each row is run twice — against the shipped script, and against a scratch copy with **guard 7
reverted**, which is v0.2's classifier exactly. That copy is `cmp`-asserted to differ from the
original and checked not to contain the guard, so a revert that failed to apply cannot read as a
pass; and the *pair* of verdicts is what shows an arm discriminates rather than merely passing.
`assert_shadow` is stubbed out for the probe **deliberately**: it fails *closed* in the sealed arm,
so leaving it in would mask which function is defective.

| injected compiler result | guard 7 REVERTED (v0.2's classifier) | shipped |
|---|---|---|
| `rc=1`, `clang: fatal error: simulated compiler failure` | **`TUs with errors: 0  total errors: 0`** — reported CLEAN | `Fatal` — *"COMPILATION FAILED WITH NO CLASSIFIED DIAGNOSTIC in the control arm"*, with the rc and the stderr head |
| `rc=-11`, empty stderr — signal death | **reported CLEAN** | `Fatal`, same guard |
| `rc=254`, an unrecognised ICE stack dump carrying a unique sentinel string | **reported CLEAN** | `Fatal`, **and the message echoes the sentinel** — which is what proves the injected result reached the classifier rather than the stub having silently failed to install |
| `rc=0`, empty stderr — **the negative control** | clean | **clean** |
| `rc=1`, a real `file:line:col: error:` diagnostic | `Fatal` — manifest violated | `Fatal` — manifest violated |
| `rc=0` **with** that same diagnostic — the two halves disagree | `Fatal`, but for an unrelated reason (an unlisted *control* failure); in the **sealed** arm, which has no unlisted-failure rule, v0.2 counted it as an ordinary failing TU | `Fatal` — *"DIAGNOSTIC CLASSIFIED BUT THE COMPILER EXITED 0"*, naming the actual condition |

Rows 1–3 are the defect; row 4 is the negative control that keeps rows 1–3 from being an instrument
that simply always raises; rows 5–6 are the two-sided half — the classifier still *classifies*. ⚠️
**Row 6 is a consistency rule, not an undercount fix, and is stated as such**: v0.2's behaviour there
was in the safe direction (it counted the TU) and the new rule only refuses to pick a side.

**Guard 7's OTHER half — `pick_witness()`'s `-MM` probe — has its own mutant, and it is a PAIR**,
because what that fix prevents is not a silent zero but a *loud misdiagnosis*. A bogus flag is
injected into the dependency-probe command line only (a `cmp`-confirmed scratch copy), and the same
mutant is run against a second copy with the rc check removed:

| arm | verdict |
|---|---|
| bogus `-MM` flag, **rc check present** (shipped) | `Fatal` — *"DEPENDENCY PROBE FAILED on … (rc=1)"*, naming the TU and printing `clang++: error: unknown argument: …` |
| the same mutant, **rc check removed** (v0.2) | `Fatal` — *"NO SWEPT TU INCLUDES fixpp/dict/table_view.hpp — this sweep is vacuous"* |

Both raise, so a single-arm probe would have called v0.2 fine. **The pair is the evidence**: v0.2
blames the *filter* for a broken *probe*, and would have moved the witness silently had only some
probes failed. ⚠️ Note the stderr line in the first arm — `clang++: error:` — is itself an instance
of the classifier's blind spot: the ` error:` is preceded by a space, so the pattern does not match
it. The two halves of guard 7 are the same defect seen at two call sites.

#### Scope of the sweep, stated because the seam that depends on it must not outlive it

`table_view_seal_sweep.py` is the **pre-implementation bill** instrument: it simulates a seal by
rewriting a header that is not yet sealed, anchored on `table_view& operator=(table_view const& other)`,
the `= default` move-assignment and the *"Build-time population surface"* banner. **After the seal
lands those anchors are gone.** The script then fails with a named message — *"SOURCE HEADER DOES NOT
MATCH THE SIMULATION ANCHORS … Either the header moved, or the seal has ALREADY LANDED"* — rather
than relaxing into a control-shaped run and printing a misleading zero. §6 seam 5 is written against
that fact: validating a *completed* migration is the job of an ordinary full build, not of this
sweep.

#### The differential is the tool's output, not arithmetic performed in prose

`--arm differential` runs both arms and emits the **set difference** of the failing-TU sets, with
`--emit-set` writing it to a file. v0.1 subtracted by count, in prose; the adversarial review had to
verify by hand with `comm -13` that the subtraction was a genuine set difference. It was — but the
check now lives in the instrument. **The error count is summed over that set**, not computed as
`sealed_total − control_total`: a by-count subtraction is precisely what the expected-failure
manifest exists to replace, and leaving one in the differential would have reintroduced it one line
below the fix.

⚠️ **`--emit-set` withholds the must-fail probes.** §5d's own warning is that, without a control arm,
those three TUs *"would have been 'migrated' — a rewrite of three tests whose failure is their
purpose."* A single-arm `--arm sealed --emit-set` reaches that failure mode from inside the tool, so
the sealed arm subtracts the manifest before writing and prints how many it withheld. Verified on a
run that contains one: `--filter /tests/session/ --arm sealed` reports 2 failing TUs and writes
`emit-set: 1 TUs (1 must-fail probe(s) withheld)`, with only
`tests/session/test_066_arena_fit_test.cpp` in the file.

---

## 3. Options inside the surviving family

§1a(ii) leaves one family: **mutation on a distinct owning type, consumed to produce the view.**
Three things are still open inside it.

### 3.1 Where the mutation surface lives

**S1 — a free `fixpp::dict::table_view_builder`, holding a `table_view` by value, with sixteen
forwarders and `[[nodiscard]] table_view build() &&`.** `table_view`'s mutators move behind
`private:` with exactly one `friend class table_view_builder;`. No member data moves; no mutator body
moves; `set_length_pair_data_tag`'s strong-guarantee body (fixpp#426 Gate B r6 M-2 and r7 N-1, the
one carrying *"capture what this call would overwrite, insert forward, and insert inverse under a
rollback"*) stays exactly where it is and keeps its comment.

**S2 — the builder owns the storage; `table_view` gains a private construct-from-builder path.**
Rejected. It relocates ten containers and the strong-guarantee body, so the diff stops being
"access specifiers plus forwarders" and becomes a rewrite of the type — with the bug-injection risk
that implies, for no gain over S1.

**S3 — `class table_view_builder : private table_view` with sixteen `using` declarations.**
Superficially the cheapest: a privately-derived class needs no forwarder bodies at all, and private
inheritance blocks the derived-to-base conversion, so no caller can launder a `table_view&` out of
it. **It fails on the chain return type, and the failure is fatal rather than cosmetic.** Eight of
the sixteen mutators return `table_view&` for chaining — `add_group_member`,
`add_group_required_member`, `add_valid`, `add_required`, `set_type`, `set_group_first`, `add_enum`,
`set_multi_value`. A `using`-imported `add_valid` still returns `table_view&`, so
`b.add_valid(…).add_valid(…)` calls a now-**private** member on the returned reference and does not
compile — and chaining is exactly what the test tree uses. Making it work means writing sixteen
forwarders, which is S1. Recorded rather than dropped, because "just inherit privately" is the first
idea a reviewer will have.

> ⚠️ **Correction to the pre-gate note carried into this gate.** M4 states *"9 of the 16 mutators
> return non-const `table_view&`"* and then lists eight mutator anchors **plus `operator=`**. The
> accurate count is **eight mutators**; the ninth entry is the assignment operator, which is not a
> mutator. Nothing downstream turns on it — §3.2 deletes `operator=` on independent grounds — but the
> number is stated correctly here rather than carried forward.

**S1a — nest it as `table_view::builder` instead of a free type.** A nested class already has access
to its enclosing class's private members, so this variant needs **zero `friend` declarations** — a
real advantage over S1's one. It loses on two concrete counts: a nested class cannot hold a
`table_view` **by value** inside the enclosing class's own definition (the type is incomplete there),
so it must be forward-declared inside and defined after the class body, splitting the definition
across an already-long header; and `table_view::builder` is a heavier spelling than
`table_view_builder` at every migrated line §5d has to rewrite. Close call; S1 wins on the split, not
on anything cleverer.

### 3.2 Must assignment be deleted?

**Yes. It is not an optional extra — leaving it in defeats the seal.**

Copy-assignment is *specifically* able to reproduce #456's defect, and the tree already contains the
test that proves it. `DictHooksCustomPair.CopyAssignmentCarriesTheFlagWithThePairs` asserts that
assigning into a view flips `has_nonstandard_pair()` from false to true, and that *"the assigned-in
pair must reach a bundle built from the target"*. That is the latched bit of §2, changed after
publication, through assignment rather than through a mutator. A caller who can write
`published = other;` has therefore not been sealed at all — they have been handed a
sixteen-mutator-wide hole with a different spelling.

**The bill, measured.** From the library root, with `$SCRATCH` an **absolute** path outside the
repository:

```
python3 tools/table_view_seal_sweep.py --root . --build build/linux-clang-debug \
        --filter /src/ --arm differential --scratch "$SCRATCH"
```

```
shadow guard  [control]: token ABSENT  in src/capi/config.cpp — script-level only (no -I injected, so this cannot fail on input)
arm=control  TUs=66  TUs with errors: 0  total errors: 0
shadow witness [sealed]: token OBSERVED in src/capi/config.cpp — the measurement is against the SEALED header
arm=sealed   TUs=66  TUs with errors: 3  total errors: 15
    src/capi/message_write.cpp        1
    src/dictionary/dictionary.cpp    13
    src/dictionary/reify.cpp          1

DIFFERENTIAL (set difference, sealed minus control): 3 TUs  15 errors
```

`dictionary.cpp`'s thirteen are all *"is a private member of 'fixpp::dict::table_view'"*; the other
two are `optional<table_view>` copy-assignments. Two independent cross-checks fall out of this and
are worth more than the number itself. `dictionary.cpp`'s **13** equals the independently-grepped
count of mutator lines in that file. And the two `optional` sites are **exactly** the two predicted
from a container census, found here by a compiler that knows nothing about that census.

**Both `optional` sites convert to `emplace` with no behavioural delta, and that was checked rather
than assumed.** `emplace` destroys-then-constructs, so it differs from assignment when the optional
is already engaged (a throw leaves it disengaged instead of leaving the old value). Reading both
sites: `src/dictionary/reify.cpp` runs `handle.pimpl_->owned_tv_ = view.membership_copy();` on an
`impl` allocated a few lines earlier in the same `try`; `src/capi/message_write.cpp` runs
`clone->owned_tv_ = h->view->membership_copy();` on a freshly built clone. **Both are disengaged at
the point of assignment**, so the conversion is exact. ⚠️ **This is the one claim in §3.2
established by reading rather than by execution**, and a reading is exactly what rots when a caller
is added upstream. It is therefore converted into a checked obligation rather than left as a note:
§6 seam 6 pins it.

**What dies with assignment, and must be argued rather than deleted quietly.**

- `table_view.hpp`'s trailing `static_assert(std::is_nothrow_move_assignable_v<table_view>)`. It is
  not decoration. The hand-written copy-assignment above it — *"Strong guarantee (Gate B r6 M-2):
  copy first, commit through the nothrow move-assignment below"* — exists **only** to commit through
  that nothrow move-assign, and the move-assignment is deliberately *not* spelled `noexcept` (Gate B
  r7 N-2: *"declaring it would make the assertion below observe that promise instead of proving
  it"*). Deleting assignment deletes the whole scheme.
- **The invariant it protected has a second, surviving home, and that is why this is a deletion
  rather than a loss.** "The maps and the flag never disagree after a failed allocation" is enforced
  *twice* today: once on the assignment path (the copy-and-commit scheme above) and once on the
  mutation path, inside `set_length_pair_data_tag`'s own rollback. Sealing deletes the first. The
  second is untouched, and it is the one that covers every way a pair can now enter a table. *(The
  adversarial review read that rollback independently and confirmed it is nothrow on both branches —
  an `erase` on a `uint16_t`-mapped key, or a write to a key inserted three lines earlier — with no
  dependence whatever on move-assignment.)*

#### The replacement assertion, and why the specification must be INFERRED

**`static_assert(std::is_nothrow_move_constructible_v<table_view>)`, and — this is the load-bearing
half — the implementation simultaneously DROPS the explicit `noexcept` from
`table_view(table_view&&) = default;`.** Move *construction* is how `build()` returns, how
`optional::emplace` works, and how the by-value validator copy is seated, so it is the property that
becomes load-bearing once move-assignment is gone.

⚠️ **v0.1 kept the explicit `noexcept` and offered the assertion as evidence. That assertion could
not report the failure it was installed to catch, and the header's own Gate B r7 N-2 comment — which
v0.1 quotes approvingly two bullets earlier — explains exactly why.** Since P1286R2 (C++20) an
explicit exception specification on a defaulted special member simply *wins* when it disagrees with
the implicit one, so against `table_view(table_view&&) noexcept = default;` the trait is **true by
construction**. §3.2 deleted a proof and installed a promise in its place, in the one bullet quoting
the ruling that says not to.

**Two-sided, at the real type rather than at an analogue.** Three overlays of the *real*
`table_view.hpp` were compiled against a TU carrying only that `static_assert`. The mutant is a
member that is copyable and assignable but whose move constructor is `noexcept(false)`, inserted
beside `has_nonstandard_pair_`. **Each arm carries all three parts of the evidence** — the overlay
DIFFERS from the shipped header, the overlay was READ, and only then the result — because for the
two GREEN arms the first two parts are what separate "the assertion held" from "the overlay never
applied" (§5d item 3 is where this rewrite learned that the hard way):

| overlay | move ctor | throwing-move member | differs | read | result |
|---|---|---|---|---|---|
| A | **inferred** (`= default`) | yes | 2 lines | witnessed | **RED** — exactly one error, the assertion |
| B | explicit `noexcept` | yes | 1 line | witnessed | **GREEN** — the assertion cannot see it |
| C | **inferred** | no | 1 line | witnessed | **GREEN** — no false positive |

B is the defect, demonstrated on the shipped type. A is the fix doing its job. C is the fix not
crying wolf. **Recipe:** generate each overlay from the real header by anchored replacement (so a
non-matching edit raises rather than passing silently), `cmp` it against the original, seed
`#error FIXPP_456_ABC_OVERLAY_WAS_READ`, confirm the diagnostic, remove it, then
`clang++ -std=c++23 -Wall -fsyntax-only -I<scratch> -Iinclude` a TU containing the assertion alone.

**Dropping the promise is free on every toolchain available here, and that was measured, not
argued.** An overlay differing from the shipped header in exactly one line — the `noexcept` dropped
from the move constructor, confirmed by `diff` as `+0 added, -0 removed, ~1 modified` — was compiled
under three toolchains, each with its own seeded-`#error` proof that the overlay was read:

| toolchain | `is_nothrow_move_constructible_v<table_view>` | `…<std::optional<table_view>>` | `is_copy_constructible_v` |
|---|---|---|---|
| clang 22.1.2 + libstdc++ | 1 | 1 | 1 |
| clang 22.1.2 + **libc++** | 1 | 1 | 1 |
| **g++ 13.3.0** + libstdc++ | 1 | 1 | 1 |

Identical to the shipped header's values in all three. Every member — `unordered_map`, `vector`,
`unordered_set`, `bool`, and the custom-hash `group_ctx_` map — is nothrow-move-constructible on all
three, so dropping the promise moves no trait and converts the assertion into the proof it claims to
be. Nothing depends on the promise: no `is_nothrow_*` trait names `table_view` outside the assertion
being deleted, no `noexcept(…)` operator reads it, and there is **no `std::vector<table_view>`**
anywhere, so no container reallocation strategy turns on it.

**The implementation must also carry r7 N-2's comment across to the move *constructor***, so the next
reader finds the reason beside the declaration rather than in this document.

> ⚠️ **Partial disagreement with the adversarial review, recorded because it inverts one of its own
> prescriptions.** The review concludes *"delete §3.2's MSVC/libc++ 'NOT MEASURED' clause and its §7
> residual — there is nothing for those lanes to decide."* That is true **only while the explicit
> promise stands**, which is the review's own qualifier (*"no conforming implementation can make that
> trait false while the explicit promise stands"*). The moment the promise is dropped — the fix — the
> specification is inferred from the members, `std::unordered_map`'s move constructor is not required
> by the standard to be `noexcept`, and the assertion becomes genuinely decidable by a lane. **A
> residual that can fail is not a defect; it is the entire point of the inferred form.** Deleting it
> would reintroduce, as an unmeasured claim, exactly the "this holds everywhere" assertion the
> finding objects to.
>
> **So the residual is narrowed by measurement rather than deleted.** `linux-gcc-release`
> (g++/libstdc++) and `tier3-libcxx` (clang/libc++) are **DISCHARGED** by the table above.
>
> ⚠️ **MSVC has now DECIDED IT, and it decided FALSE — Gate B, PR #485.** All three
> `windows-msvc-{debug,release,asan}` legs failed to configure with
> `table_view.hpp: error C2607: static assertion failed`, inside the codegen bootstrap's
> `fixpp_wire` build. Measured locally on MSVC **14.44.35207** (`_MSC_VER` 1944, STL 202503) with a
> standalone probe: the Microsoft STL does not declare `unordered_map`/`unordered_set` move
> construction `noexcept`, so **all ten hash-container members** decide it, while every
> `std::vector`, `std::string`, hash, equality functor and allocator involved **is** nothrow-move.
> The probe is two-sided by construction — it reports YES and NO in the same run.
>
> **The branch written below was taken exactly as written**: the assertion is REMOVED, the fact is
> filed as `L-456-2` naming the deciding members, and the `noexcept` promise is NOT restored.
>
> ⚠️ **And the measurement carries a finding this section did not anticipate.** `main` already
> spelled `table_view(table_view&&) noexcept = default;`, and since P1286R2 that explicit
> specification **wins** over the inferred one — so MSVC builds have always carried a move promising
> `noexcept` over ten members that promise nothing of the kind, with
> `wire::dictionary_driven_validator`'s **`noexcept`** constructor move-constructing its member on
> top of it. fixpp#456 did not introduce that; **dropping the promise is what made it visible.**
> That is the inferred form doing precisely the job §3.2 argued for, on its first real test.
>
> **And the obligation is already in a form Tier-2 discharges fail-closed, which was checked rather
> than assumed**: `tests/dictionary/CMakeLists.txt` has no `WIN32`/`MSVC` gating around its
> `table_view` targets, and `tier2.yml` builds `windows-msvc-{debug,release,asan}` with a bare
> `cmake --build --preset …` and no `--target` narrowing. So seam 1's
> `static_assert(std::is_nothrow_move_constructible_v<table_view>)` is compiled by MSVC, and a
> failure there is a red build rather than a silence.
>
> ⚠️ **Say what the remaining branch IS, because "record it, not restore the promise" does not
> unbreak a red build.** If MSVC decides the trait false: the `static_assert` is **removed** and the
> fact is filed as a limitation naming the deciding member; the `noexcept` promise is **not**
> restored, because restoring it would silence the assertion on *every* lane without making the move
> any safer. Recording the finding and restoring the promise are not the only two options, and the
> second is the one explicitly forbidden.

- **Three tests are deleted, not fixed** — their subject ceases to exist:
  `TableViewPairOom.CopyAssignmentIsAllOrNothing`,
  `TableViewPairOom.CopyAssignmentOverAPopulatedTargetIsAllOrNothing` (both in
  `tests/dictionary/table_view_pair_oom_test.cpp`), and
  `DictHooksCustomPair.CopyAssignmentCarriesTheFlagWithThePairs`.
  The same OOM file's three **mutation-path** cases — `NewPairInsertionIsAllOrNothing`,
  `RepairingTheLengthSideIsAllOrNothing`, `RepairingTheDataSideIsAllOrNothing` — **survive and must
  be re-grounded on the builder, not deleted.** They are the coverage of the surviving half of the
  invariant, and losing them alongside the assignment cases would silently drop the guarantee this
  design claims to preserve.
- `TableViewTest.SpansRemainingValidAfterDictionaryDestroyed` in
  `tests/dictionary/table_view_test.cpp` move-assigns (`tv = dict.as_table_view();`) in order to
  outlive the `Dictionary`. **Restructured, not deleted**: `std::optional<table_view> tv;` stays
  declared *outside* the inner scope and is seated with `tv.emplace(dict.as_table_view());` inside
  it, with an `ASSERT_TRUE(tv.has_value())` precondition after the scope closes — assignment is
  gone, and the optional makes the seating explicit — which tests the same property.

**Holders checked for a cascade.** Deleting assignment on `table_view` deletes the implicit
assignment operators of everything holding one by value. Executed census:

```
grep -rn "table_view [a-z_]*_;" include/ src/
  include/fixpp/wire/validator.hpp         fixpp::dict::table_view dict_;  // held BY VALUE (SC-007)
  include/fixpp/dict/dictionary_snapshot.hpp   table_view view_;
```

- `dictionary_snapshot` is **already** non-copyable and non-movable by design (`215` Option C).
  Measured by trait probe: `is_copy_assignable_v` and `is_move_assignable_v` are both already
  `false`. **No delta.**
- `dictionary_driven_validator` **is** copy- and move-assignable today (same probe, both `true`) and
  becomes neither. The production sweep shows no production site assigning one, and **no
  `is_*_assignable` assertion anywhere in the tree names `dictionary_driven_validator` or
  `Validator`** — the condition, checked by
  `grep -rn "assignable" tests/ include/ src/ | grep -i "validator\|dictionary_driven"`, which
  returns nothing. *(v0.1 instead listed the types the bare `assignable` grep does hit; that list was
  already incomplete — it omitted `MessageView<access_mode::Index>` in
  `tests/wire/parser_index_test.cpp` — while the claim that matters was never about the list.)*
  **This is the one place the in-tree cost could still grow**, and it is bounded by the same sweep:
  the test TUs in §5d's differential are the complete list of in-tree places a validator assignment
  could hide. `Session` holds its validator as
  `std::unique_ptr<fixpp::wire::dictionary_driven_validator> validator_;`, so the session path is
  unaffected. **Out of tree it is a break with no mechanical fix**, and §5d's migration matrix says
  so rather than implying otherwise.

### 3.3 What `build()` looks like

- **`[[nodiscard]] table_view build() &&`**, returning `std::move(tv_)`. Ref-qualified, so a named
  builder must be spelled `std::move(b).build()` and the consumption is visible at the call site.
- **No `build() const&` overload.** A copying overload would reintroduce, at every site and silently,
  the `table_view` copy `215` §1 measured as a share of a full dictionary walk. ⚠️ **Those percentages
  are inherited and are annotated PRE-082 at their source**, so they are cited here as a *direction*,
  not a value: 082 added group-registration work to `as_table_view()`, which makes the walk dearer and
  can therefore only shrink the copy's share. Not re-measured for this gate, and it does not need to
  be — the decision turns on "a silent copy at every site is worse than a visible `std::move`", which
  no revision of the ratio changes.
- ⚠️ **The chained-prvalue trap, stated because it is the first thing someone will write.** The
  forwarders return `table_view_builder&` — an **lvalue** — so
  `table_view_builder{}.add_valid(…).build()` does **not** compile against an `&&`-qualified
  `build()`. This is deliberate friction, and §5d's migration always emits two statements, so no
  call site hits it. It must be in the builder's header comment.
- **Zero call sites bind a chain result to a reference today**, so nothing depends on the chain's
  return type being `table_view&`:
  ```
  grep -rnE "(table_view *&|auto *&) *[a-zA-Z_]+ *= *[a-zA-Z_]+\.(add_|set_)" src/ tests/ include/
    ->  0
  ```
  ⚠️ **Zero, with the instrument proven able to report non-zero:** the same regex against a seeded
  file containing `fixpp::dict::table_view& r = tv.add_valid("D", 11);` returns **1**.

---

## 4. Recommendation

**S1 plus deleted assignment.** Add `fixpp::dict::table_view_builder` in `table_view.hpp`, holding a
`table_view` by value, exposing sixteen forwarders that each return `table_view_builder&` and a
`[[nodiscard]] table_view build() &&`. Move `table_view`'s sixteen mutators behind `private:` with
**exactly one** `friend class table_view_builder;`. Delete both assignment operators, delete the
`is_nothrow_move_assignable_v` static_assert, **drop the explicit `noexcept` from the move
constructor**, and add `is_nothrow_move_constructible_v` in the assertion's place (§3.2).
`Dictionary::as_table_view()` uses the builder like every other caller.

**Three properties make this the right member of the family.**

*It closes #456's own witness by construction.* The witness mutates a default-constructed view;
after the seal that object has no mutation surface and no assignment operator, so the witness cannot
be written. §6's RED asserts precisely that, in both directions.

*It does not touch layout, data, or any mutator body.* **Stated as structure, because that is what it
is:** only *member functions* change access, and member functions do not participate in layout, so
`sizeof(table_view)` and `alignof(table_view)` are unchanged **by construction**. No measurement can
make that claim stronger, and v0.1's paired sealed/unsealed `sizeof` figure is **deleted** rather
than restated — its outcome was structurally forced, so it was an instrument that could not have
reported otherwise. *(The bench baseline's own `BM_TableView_Sizeof` row remains the authority on the
byte count, and it is not this design's claim. ⚠️ The pre-gate note's *"392 B as of 075 T032"* is a
075-era figure superseded twice since — the baseline row's own verdict records `608` going to `728`,
and notes that `608` itself *"read UNCHANGED … until this measurement, which is the stale value Gate
B r5 L-4 caught"*. Nothing falsified the 392, because nothing re-runs a number written in prose.)*

**No `sizeof(table_view)` ASSERTION exists anywhere to break** — the condition, not a list of the
hits, because a list rots and the condition does not. Executed:
`grep -rn "sizeof(table_view)\|sizeof(fixpp::dict::table_view)\|sizeof(dict::table_view)" tests/
include/ src/ specs/ spec/ bench/`. **Every hit is prose, a build comment, a reported bench counter or
a baseline JSON value — none is a `static_assert` and none is an `EXPECT_EQ`.** *(v0.1 enumerated the
hits and the enumeration was already incomplete: it omitted a comment in
`bench/dictionary/CMakeLists.txt`. The load-bearing claim was unaffected, which is exactly why the
enumeration should not have been there.)*
⚠️ **The grep is proven able to report non-zero** — the same pattern against
`bench/dictionary/table_view_footprint_bench.cpp` alone returns **4** — so a small hit list from it is
the pattern working, not the pattern failing. ⚠️ **But be precise about what that proves.** No
instrument distinguishes an assertion from prose; **every hit the grep returns was read**, and the
"none is a `static_assert`" claim is a classification, not a measurement. It is stated as one so a
re-runner knows to re-read rather than to re-grep. So that bench reports whatever it reports and
needs no edit, and the baseline row needs no re-measurement for this change — though
`[const §VIII.2]` still wants the paired base-vs-candidate run in the PR.

⚠️ **One runtime delta does exist, and §4 must not elide it.** `Dictionary::as_table_view()` today
declares `table_view tv;` and has two `return tv;` — both NRVO, so **zero moves, zero copies**. Under
the builder it becomes `return std::move(b).build();`, where `build() &&`'s `return std::move(tv_);`
is a move-construction **from a member**, where NRVO cannot apply; C++17 guaranteed elision then
makes the outer return free. The delta is therefore **exactly one added move construction** — ten
container pointer-steals, O(1), no allocation — against a **config-time** call that walks the entire
dictionary to build the table in the first place. ⚠️ **The comparand is stated as that structure, not
as a millisecond figure**: `215`'s timing for `as_table_view()` is annotated pre-082, and 082 added
group-registration work to exactly this function, so any inherited number is stale in a direction
that only makes the added move *more* negligible. Favourable, and worth stating rather than leaving
for a Gate B reviewer to ask
about, since `[const §XV.1]` and the build-once design point make it the obvious question. The paired
run §5a already promises closes it.

*Its friend surface is a censusable structural gate.* `table_view` has **zero** friends today —
`grep -c "friend" include/fixpp/dict/table_view.hpp` returns **1**, and that one line is
`valid_tag_set_view`'s `friend class table_view;`, pointing the *other* way. After the change the
count is **2**, of which exactly one names the builder, and `Dictionary` is deliberately not a
friend. That is `215` §6's G1 shape: an exact-count assertion, not a "no unexpected friends"
hand-wave, so a second friend added later fails a check rather than passing unnoticed.

**Why not leave assignment in and call it "mostly sealed".** Because
`CopyAssignmentCarriesTheFlagWithThePairs` is a checked-in test proving assignment moves the exact
bit #456 is about, on a published view (§3.2). A seal with assignment left public is not a weaker
seal; it is a seal with a documented bypass and a test demonstrating the bypass works.

---

## 5. Consequences

### 5a. API changes

| surface | before | after |
|---|---|---|
| `table_view` — the sixteen mutators | `public:` | `private:`, reachable only through the builder |
| `table_view::operator=(table_view const&)` | public, hand-written strong-guarantee | **deleted** |
| `table_view::operator=(table_view&&)` | public, `= default` | **deleted** |
| `table_view(table_view&&)` | `noexcept = default` | **`= default`** — specification INFERRED, so the assertion below is a proof (§3.2) |
| `table_view()` / `~table_view()` / copy ctor | public | **unchanged** (§1a(iv), §1a(v)) |
| the 6-method validator surface and every `const` accessor | public | **unchanged** |
| `static_assert(is_nothrow_move_assignable_v<table_view>)` | present | **deleted**, replaced by `is_nothrow_move_constructible_v` (§3.2) |
| new type | — | `fixpp::dict::table_view_builder` — sixteen chaining forwarders, `[[nodiscard]] table_view build() &&`, plus three **scalar** `const` readback forwarders (§5d, item 4) |
| `Dictionary::as_table_view()` | public, returns by value | **signature unchanged**; body populates a builder; one added move construction (§4) |
| `wire::dict_hooks::for_table_view` | takes `table_view const&` | **unchanged** |
| `wire::Validator` / `dict::dictionary_snapshot` | hold `table_view` by value | **unchanged in shape**; both lose implicit `operator=` (§3.2) |
| C ABI (`include/fix/`) | frozen | **unchanged** — `table_view` appears in zero `include/fix/` headers |

**Bindings that apply.** The builder allocates and walks, so it is config-time only and barred from
the per-message path by `[const §XV.1]` — exactly as `as_table_view()` already is. Nothing here sits
between parse and `fromApp`, so `[const §VIII.5]` is not engaged. No new virtual, so
`[const §XIV.2]`'s ≤5 pure-virtual budget is untouched. `build()` is `[[nodiscard]]`.
`[const §VIII.2]`'s paired base-vs-candidate run covers the one added move (§4).

### 5b. Ownership and lifetime

Nothing about ownership changes, and that is the point — the builder is a **stack-local scaffold**
with no reference to anything. It holds a `table_view` by value, `build() &&` moves it out, and the
builder is then a moved-from object that the ref-qualifier makes awkward to reuse by accident. No
new indirection, no back-pointer, no lifetime edge.

`215` §2a established by census that `table_view` has no
`mutable` members and no lazy fill — that census is unaffected by this change. What this change adds:
`table_view`'s sixteen population members are `private`; its only `friend` is `table_view_builder`,
which holds its own `table_view` by value and yields it from `build() &&`; both `operator=` overloads
are `= delete`. This does not make the object unwritable. The `std::uint16_t` elements behind
`required_fields`, `group_member_tags` and `group_required_members` are allocated by the member
vectors and are not `const` objects, so `const_cast` on a span's pointer and a write through it is
defined behaviour — on a `const` view as much as a non-`const` one. A non-`const` view can
additionally be moved from (`table_view(table_view&&)` is public and load-bearing, §5a), and an
`optional<table_view>` re-seated with `emplace` substitutes a different object at the same address
(`B-456-2`); declaring the view `const` closes
those two and not the first.

**What it does NOT buy.** `dict_hooks` stores `std::addressof(dict)` as a raw non-owning
`void const* opaque_dict_`. A bundle outliving the view it was built from is still a dangling read,
and the seal neither causes nor cures it. §7.

### 5c. Documentation and ledger dispositions

**Read the live `spec/behaviors-and-limitations.md`; resolved rows live in
`spec/behaviors-and-limitations-closed.md`, and a grep across the pair reports resolved rows as
open.** Everything below was executed against the live file only.

**No existing B&L row closes, because no existing row records either half of this defect.**
`grep -n "B-456\|L-456" spec/behaviors-and-limitations.md` returns nothing; the mutability hole is
recorded nowhere (`215` §5c said so explicitly — under Option C a row was *"not needed"*), and the
bundle-snapshot behaviour is pinned only by the test and by `.specify/426-428-length-data-pairs.md`
§3.

#### Behaviours and limitations

| row | disposition |
|---|---|
| **`B-456-1` (NEW, behaviour)** | `table_view`'s population surface is sealed and the type is non-assignable, once built; a caller who does not declare the result `const` can still move from it, and a re-seated `optional<table_view>` substitutes a new object at the same address (`B-456-2`). Source-breaking, not a C-ABI change. **Shipped text lives in `spec/behaviors-and-limitations.md`** |
| **`B-456-2` (NEW, behaviour) — the disclosure** | A `wire::dict_hooks` bundle latches `has_nonstandard_pair()` at build time only; every other callback dereferences the stored address live, so only that one latch can go stale through a re-seated `optional<table_view>`. **Shipped text lives in `spec/behaviors-and-limitations.md`.** |
| **`B-384-2` — AMENDED, not closed** | Its reachability clause reads *"through the hand-built `dict::table_view` surface only (`add_group_member` without `set_group_first` sets `group_bit` while leaving `group_first_` empty)."* **The seal does not close this.** `table_view_builder` still permits `add_group_member` without `set_group_first`; only a consistency check inside `build()` would close it, and that is out of scope (§7). The row needs a **wording** amendment — "hand-built `dict::table_view` surface" becomes "`table_view_builder` surface" — and nothing more. Leaving it unamended would leave a row naming an API that no longer exists. |
| **`L-456-1` (NEW, limitation)** | *"`table_view_builder::build()` performs no consistency validation. A builder can still produce an internally inconsistent table — a group with members and no first field (`B-384-2`) is the known shape. The seal governs reachability, not consistency: `build()` is `return std::move(tv_);` and checks nothing."* |
| `L-215-1` / `B-215-2` / `L-215-2` / `B-215-1` | **untouched.** Different code path (the config injection point); `L-215-2` / SC-007 is explicitly out of scope per `215` §7. |
| `B-426-*` / `L-426-*` / `B-428-*` | **untouched** — pair semantics are unchanged. |

#### Prose that becomes FALSE the day this lands — including the shipped headers

⚠️ **v0.1's documentation census stopped at `.specify/` and the B&L ledger. The shipped headers are
the ones that will teach a defect that no longer exists**, and one of them — `mock_dict_table.hpp` —
was named in the pre-gate measurements (M5) and dropped. Every row below was opened at `f49f462a`
and its quoted text is printed beside it.

| file | the sentence that becomes false | disposition |
|---|---|---|
| `include/fixpp/dict/dictionary_snapshot.hpp` | *"C1 — the `const` on the pointee is not enforced, since `table_view` carries several public mutators (add_valid_tag, set_group_first, set_length_pair_data_tag, …)"* | **rewrite.** The premise is gone. C1's *history* stays — the row explains why `dictionary_snapshot` exists — but the present tense must go. |
| `include/fixpp/dict/dictionary_snapshot.hpp` | *"`Dictionary::as_table_view()` … stays public and keeps returning a mutable `table_view` BY VALUE … so this type does not make `table_view` itself immutable."* | **partial rewrite.** The middle clause is stale — the mutators are private now regardless of the caller's const-ness, so "mutable … BY VALUE" needs to name the residual channels (move-from, `const_cast` through a span, re-seat), not call the returned object simply "mutable". The closing clause — "this type does not make `table_view` itself immutable" — **stays true and stays in the rewrite**: `dictionary_snapshot` never constrained `as_table_view()`'s return, and per `B-456-1`/§5b no declaration of the view, `const` included, makes it fully immutable either. This is the shipped public header whose whole purpose is to explain the #215 boundary. |
| `include/fixpp/dict/table_view.hpp` | *"Copyable AND movable — see the copy/move block below, which defaults all four."* | **rewrite** — half false (copy-constructible and move-constructible; neither assignable). The same block's *"Move assignment is deliberately NOT spelled `noexcept` (Gate B r7 N-2)"* comment **moves to the move constructor**, which is where the inferred specification now matters (§3.2). |
| `include/fixpp/dict/table_view.hpp` | the class contract's *"Constructed ONCE at session/validator setup time … Immutable after construction"* | **amend, but not to assert immutability.** The seal does not make the type immutable (§5b: a non-`const` view can be moved from, `const_cast` through a span accessor is defined behaviour on a `const` view too, and `optional::emplace` re-seats under a live bundle) — asserting the reverse here would be 456-R2-1's error, generalised to the class contract. Amend to state the mechanism instead: the sixteen population members are private, reachable only through `table_view_builder`; naming that type is the pointer a reader needs. |
| `include/fixpp/dict/dictionary.hpp` | `as_table_view()`'s *"the returned table_view is immutable and must not be rebuilt on the per-message hot path"* | **amend, but not to say "immutable" is now enforced.** The seal narrows reachability of the sixteen mutators; it does not make the returned view immutable (§5b's three residual channels). Amend to point at `table_view.hpp`'s STORAGE banner for what is actually enforced, and keep the per-message-hot-path prohibition, which is unaffected by this change. |
| `tests/support/mock_dict_table.hpp` | *"The production `table_view` provides the same 6-method validator surface AND the chain-style builder API (add_required, add_valid, set_type, set_group_first, add_group_member, add_enum, set_multi_value) that these tests use."* | **rewrite.** A pure include shim with no mutator call and no declaration, so it needs no *code* edit under any design — but it is included across the test tree and its comment is a promise the type stops keeping. **This was M5's dropped input.** ⚠️ M5's *"33 TUs include it"* is **not restated here**: a re-run `grep -rl` over `tests/ src/ include/` returns 28, the two populations were never defined the same way, and the disposition turns on the comment being false, not on how many files read it. |
| `.specify/426-428-length-data-pairs.md` §3 | *"The type does not* enforce *the order — `set_length_pair_data_tag` and assignment stay public — so the rule is pinned by `DictHooksCustomPair.ABundleIsA…` and sealing the published view is fixpp#456."* | **amend to a POINTER at this document by name**, per the repo rule that a superseded decision names its successor. A pointer does not rot the way a restatement does. |

**Article XIX §5 — satisfied by absence, with the check printed.** `[const §XIX.5]` says *"Pages tied
to public API surfaces must be regenerated when the surface changes. Doxygen output drift is a Gate B
finding."* Executed at `f49f462a`: `docs/` contains only `book/`, `book.toml`, `prebuild.py` and
`src`; `find . -name 'Doxyfile*'` outside `build/` returns nothing; `grep -rln doxygen
.github/workflows/` returns nothing. **There is no generation step to omit**, so the obligation is
discharged by there being no generated pages — not by a regeneration task that cannot run. ⚠️ If a
Doxygen build is ever added, this disposition expires; it is a statement about the tree, not about
the clause.

**`CHANGELOG.md` — no row is owed by this change today, and v0.1 was wrong to promise one.** The
file's own scope block reads *"This file records **backwards-incompatible constitutional
amendments** (Article XX §4) and, going forward, **released library versions**. It is not a commit
log: routine features, fixes and MINOR/PATCH constitution bumps live in git history."* The
constitution agrees: `[const §XX.3]` and `[const §XX.4]` bind a `CHANGELOG.md` entry to
backwards-incompatible **amendments**, and `[const §X.7]` to the C-ABI reset at the first public
release. A pre-release C++ **source** break is neither. ⚠️ It is not unrecorded — `B-456-1` is the
operator-facing record, and the break belongs under the **first library-release heading**, whenever
that is written. Carrying "needs a CHANGELOG entry" forward unchecked, as both reviews did from
v0.1, would have created an obligation the file's own scope refuses.

### 5d. Migration

**Measured through the checked-in instrument, not estimated.** From the library root, `$SCRATCH`
absolute and outside the repository:

```
python3 tools/table_view_seal_sweep.py --root . --build build/linux-clang-debug \
        --filter /tests/ --arm differential --scratch "$SCRATCH" --emit-set tests_diff.txt
```

```
shadow guard  [control]: token ABSENT  in tests/alloc_guard/test_clock_sleep_alloc_guard.cpp — script-level only
arm=control  TUs=574  TUs with errors: 3   total errors: 3
    tests/session/security_profile_insecure_plain_tcp_deprecated_negative.cpp  1  [expected: must-fail probe]
    tests/tls/cipher_policy_banned_negative.cpp                                1  [expected: must-fail probe]
    tests/tls/security_profile_deprecated_negative.cpp                         1  [expected: must-fail probe]
shadow witness [sealed]: token OBSERVED in tests/alloc_guard/test_clock_sleep_alloc_guard.cpp — the measurement is against the SEALED header
arm=sealed   TUs=574  TUs with errors: 35  total errors: 828
    (the same three probes, 1 diagnostic each — bound, not subtracted)

DIFFERENTIAL (set difference, sealed minus control): 32 TUs  825 errors
```

**`rc=0`.** The largest single TU is `tests/wire/validator_type_check_test.cpp` at 92 diagnostics;
`--emit-set` writes the 32-TU differential to a file, and that file — not this table — is what a
migration should read. ⚠️ **This 574-TU run was re-executed through v0.3's classifier and
reproduces exactly** — same `3 / 3` control, same `35 / 828` sealed, same `32 / 825` differential,
`rc=0`. That is the one result guard 7 could most plausibly have moved: a TU failing with `rc != 0`
and nothing classified is now fatal, and over 574 + 66 TUs there was none. So the headline figures
are not merely unchanged in the prose — the fix is **shown not to have moved them**, and it is shown
on the arm the round-2 adversarial review did **not** re-run. ⚠️ **The sealed arm's manifest audit is a positive result, not a formality**:
each of the three probes carries **exactly** its bound diagnostic in the sealed arm too, which is
what licenses the set difference. Had one of them additionally acquired a seal error, the run would
have failed rather than absorbed it into a subtraction. ⚠️ **This run is the one the document quotes,
and it is the THIRD**: the script changed after the first `/tests/` sweep (v0.2's emit-set
withholding, summed differential and narrowed control guard) and again after the second (v0.3's
guard 7), so the figures are re-derived through the final checked-in code every time rather than
carried over. ⚠️ **What was compared, exactly**: all three runs agree on the printed figures and on
the 32 TU names, and v0.3's `--emit-set` file carries those same 32 names. **No byte comparison
across revisions was performed by v0.3** — the earlier runs' `--emit-set` files are not kept — so
this is agreement between a file and a printed list, not between two files. Stated that way because
the difference is exactly the one this document keeps finding elsewhere.

⚠️ **The control arm's failures are the repo's own must-fail negative-compile probes** —
`tests/tls/cipher_policy_banned_negative.cpp`, `tests/tls/security_profile_deprecated_negative.cpp`
and `tests/session/security_profile_insecure_plain_tcp_deprecated_negative.cpp` — whose passing state
*is* a failed compile (`WILL_FAIL TRUE` on the `tls_cipher_allow_list_negative_compile` and
`tls_security_profile_deprecated_compile` tests in `tests/tls/CMakeLists.txt`). **v0.1 subtracted
them in prose while quoting a rule the script did not implement** (§2a says the script exits
non-zero on an unclean control; on this very run it did, printing *"the sealed arm's number is
meaningless"*). They are now bound to their diagnostics in `EXPECTED_CONTROL_FAILURES`, so the
control arm passes on identity and the differential is a set operation performed by the tool. Had no
control arm been run at all, those three would have been "migrated" — a rewrite of three tests whose
failure is their purpose.

#### What the sweep cannot see — all five blind spots, not one

⚠️ **The denominator is "TUs configured in `linux-clang-debug`", not "TUs in the tree", and that
bounds every table in this document.** A `compile_commands.json` lists only what its preset
configures. v0.1 named **one** blind spot; there are five, and the complement argument it offered
covers only half of the change:

| unswept root | status |
|---|---|
| `tests/fuzz/` | **zero** TUs configured (they need `FIXPP_BUILD_FUZZ=ON`). `tests/fuzz/fuzz_wire_parser.cpp` is known to be in the bill from the mutator-name grep. **Re-run the sweep against a fuzz-enabled build before implementation.** |
| `bench/` | **zero** TUs configured |
| `bindings/` | **zero** TUs configured |
| the top-level `perf/` | **zero** TUs configured — one source file, `fixpp_perf_driver.cpp`. Clean **by content**: `grep -rl table_view perf/` returns **0 files**. ⚠️ Not to be confused with `--filter /perf/`, which selects `tests/perf/` TUs already inside the `/tests/` sweep |
| `build/linux-clang-debug/_codegen/` | **903 TUs — more than `tests/`** — which neither the `/src/` nor the `/tests/` filter touches. Clean **by construction, not by luck**: `grep -rl "table_view" build/linux-clang-debug/_codegen/` returns **0 files**. |

⚠️ **And the mutator-name grep is a complement for the *mutator* half only.** It finds `.add_valid(`
and its fifteen siblings. It **cannot see** `published = other;`, `*opt = view;`, a by-value
`table_view` member whose implicit assignment disappears, or a `dictionary_driven_validator`
assignment — which is the other half of what this change breaks, and the half §3.2 calls *"the one
place the cost could still grow."* v0.1 presented the grep as covering the complement; it does not.

**The one unswept-but-configured root was therefore swept directly**, so the residue is stated
rather than worried about:

```
--filter /tools/    control 11 TUs / 0 errors   sealed 11 TUs / 0 errors   differential 0 / 0
```

⚠️ **That zero is non-vacuous, and the run says so on its own face.** The sealed arm printed
`shadow witness [sealed]: token OBSERVED in tools/codegen/fixpp-codegen/ir.cpp`, so a TU that *does*
include `table_view.hpp` was compiled against the *sealed* header, and the zero is "nothing there
breaks", not "nothing there was looked at".

⚠️ **`--filter /perf/` is NOT a complement, and a first pass of this rewrite wrongly reported it as
one.** `--filter` is a substring match on the TU path, so `/perf/` selects four
**`tests/perf/`** TUs — already inside `/tests/` and already counted there. Its 0/0 is a subset
re-run, not new coverage, and the row is removed rather than restated. **The repository's top-level
`perf/` directory is a FIFTH blind spot**, not a swept root: it holds one source file
(`fixpp_perf_driver.cpp`) and configures **zero** TUs in `linux-clang-debug`. It is clean by content
rather than by sweep — `grep -rl table_view perf/` returns **0 files** — which is the same shape as
the `_codegen` tier, and is stated as such rather than as a compile result.

#### Pairing the instruments — and the pairing that was MISSING

⚠️ **v0.2 headed this *"the two instruments' blind spots are named by each other"*, and that was
false as executed.** A complement is only a complement against a **different axis**. The pairing
below set-differences the compile sweep against the **mutator-name grep**, which shares the sweep's
`.cpp`-and-call-site orientation — so it can only ever find files, never declaration spellings.
Nothing was paired against the **scoper's declaration pattern**, which is the input that actually
drives the rewrite, and an entire declaration spelling (`auto`) was consequently invisible to the
census, to the exception list and to the out-of-tree matrix **at once** (§5d item 7).

The two pairings that exist are therefore stated separately, with what each can and cannot see:

**(a) compile sweep × mutator-name grep — a FILE-level complement.** Both are keyed on call sites in
`.cpp` files, so this pairing finds files one instrument configures and the other does not. It cannot
see a declaration spelling, and it was never able to.
Set-differencing the compile sweep against the mutator-name grep
(`grep -rlE "\.($NAMES)[[:space:]]*\(" --include='*.cpp' tests`):

| in the grep, not in the compile sweep | why |
|---|---|
| `tests/fuzz/fuzz_wire_parser.cpp` | the fuzz tier, above |

| in the compile sweep, not in the grep | why |
|---|---|
| `tests/codegen/group_entry_read_test.cpp` | |
| `tests/codegen/typed_accessor_test.cpp` | the three includers of `tests/support/typed_group_table_views.hpp` that make **no mutator call of their own**. A `.cpp`-restricted grep is structurally unable to see them. |
| `tests/wire/repeating_group_equivalence_test.cpp` | |

**(b) mutator-call HEAD RECEIVER population × the scoper's DECLARATION census — a receiver-level
complement, and the one that was missing.** This is a different axis: it asks, of every mutator call
whose receiver is a named variable, whether the scoper found a declaration that explains it. It is
**not** a prose cross-check — it lives inside `tools/table_view_mutation_scope.py` as the attribution
guard, prints all three of its counts on every run, and **exits non-zero without writing the site
table** on any unattributed receiver not named on `--known-unscoped`. ⚠️ **It is proven able to report non-zero before anyone trusts its zero, in two
ways**: it reports the known bare-`table_view&` receivers today with no allow-list, and disabling the
`auto` declaration alternative in a `cmp`-confirmed scratch copy makes the five
`validator_type_check_test.cpp` receivers reappear as unattributed — the actual defect, replayed.
⚠️ **What "attributed" does not prove**, stated so the guard is not read as wider than it is — two
things. First, attribution is by (variable name, brace scope), so a call attributed to a same-named
declaration in a **sibling** scope still counts as attributed: the guard catches a receiver **no**
declaration explains, and does not adjudicate which one explains it. This now holds identically for
**both** columns — the builder-exemption column shares the same (variable name, brace scope)
machinery the declaration column uses, rather than a bare file-wide name set, so the sibling-scope
residue it concedes is the same one, not a wider one (Gate B round 1). Second, the population is
**head receivers**, not calls — see the chain-continuation and expression-headed shapes above.
Nothing is built for either residue; both are named.

#### The shape, and why it is two mechanical lines per scope

⚠️ **`--known-unscoped` takes a list that is DERIVED, never one copied from here.** The receiver
population moves with the tree — the seal added an entry, and fixpp#456's `/simplify` pass removed
another — so a hardcoded list rots in both directions: it exits `4` when it is missing a receiver
and `3` when it names one that no longer exists. The recipe, which cannot rot:

1. run the command **with no `--known-unscoped`**;
2. read the receivers it prints as `<< UNATTRIBUTED`;
3. pass exactly those. Every one is the shape the allow-list exists for — a reference with no named
   local to rename (a `table_view&` parameter or lambda argument), migrated by hand.

```
python3 tools/table_view_mutation_scope.py --root . --emit-sites sites.tsv --naive-decl-count \
    --known-unscoped '<the receivers step 2 printed, comma-separated>'
```

As of **`90c34ec0`** (post-`/simplify`) step 2 prints **three** — an example of that commit's output,
not a fixed argument:

```
include/fixpp/dict/table_view.hpp:tv_                      16   table_view_builder's own MEMBER
tests/dictionary/table_view_pair_oom_test.cpp:tv            3   lambda parameter (seam 3)
tests/dictionary/table_view_seal_compile_test.cpp:t        16   `requires(T& t)` — not executable code
```

⚠️ **Scope the run the same way you scope the allow-list.** `tv_` is invisible to a run filtered to
`--filter tests/`, so a list derived under that filter exits `4` on an unfiltered run. The two must
be derived together — which is the whole reason step 1 exists.

⚠️ **The block below is a PINNED HISTORICAL RECORD, not a current claim.** It was measured at
**`414875d5`, pre-seal** — the tree the 36-TU migration bill was computed from — and it is retained
because that bill rests on it. On any later tree the figures differ by construction: post-seal every
mutator call is on a `table_view_builder`, so `scoped declarations that are mutated` goes to **0**
and the receivers move into the `already on a table_view_builder` column that
commit `0b06afd6` added. Re-run the command for today's numbers; do not read these as them.

```
[measured at 414875d5, pre-seal — see the warning above]
files scanned: 32
scoped declarations that are mutated: 126
  ... with a non-mutator use of the SAME var BEFORE the last mutator call: 3
  ... pure build-then-use: 123
instrument two-sided: 3 non-zero report(s); 30 of 32 files report zero

attribution guard (every mutator-call HEAD RECEIVER must fall inside a scoped decl):
  head receivers attributed to a scoped declaration:    206
  head receivers attributed to NOTHING:                 4
  chain CONTINUATION lines (`    .add_valid(…)`, no receiver token of
    their own — in neither column above; renaming the head migrates
    them, so this is a scope statement, not a residue):  499
    tests/dictionary/table_view_pair_oom_test.cpp:tv  3 call(s)  [--known-unscoped]
    tests/wire/offset_table_test.cpp:dict  1 call(s)  [--known-unscoped]

sites written: sites.tsv (126 rows)
```

**The exit code is a statement about the allow-list, so state it as the condition rather than as a
value — the value is only true of the tree that produced it:**

| condition | rc |
|---|---|
| the allow-list names **exactly** the receivers the run finds unattributed | `0` |
| it names one that **no longer exists** (an exemption outliving its subject) | `3` |
| a receiver is **missing** from it | `4`, naming it `<< UNATTRIBUTED` |

⚠️ **In the `4` case the run writes NO site table at all.** The ordering is deliberate and is the same
rule as the sweep's `--emit-set` withholding (#18): a file this document calls *"the sole driver of
the migration"* must not exist in a complete-looking but incomplete form, because a caller who does
not check `$?` will open it and trust it. Every name on the allow-list is a receiver with **no named
local to rename**, which is the shape the list exists for — and that shape has more than one kind:
§5d item 2's bare-`table_view&` parameters and lambda arguments, a class's own **data member**
(`table_view_builder::tv_`), and a concept's `requires(T& t)` binding, which is not executable code
at all. They are allow-listed **explicitly by name**, not absorbed by widening the declaration
regex, which would blind the guard to the one receiver spelling nobody rewrote. An allow-list entry that stops naming anything exits `3` rather than passing
quietly: an exemption that outlives its subject would silently absorb the next receiver of that name
in that file.

⚠️ **The guard's population is HEAD RECEIVERS, not calls, and the two are far apart — say so rather
than write "every mutator call."** A wrapped chain (`t.add_valid(…)` followed by `    .add_valid(…)`
on the next line) is the tree's dominant spelling: **499** such continuation lines against **210**
head receivers at `f49f462a`, and a continuation carries no receiver token, so it is in neither
column. That is **correct for the migration** — sed form (2) renames the head and the chain follows
— which is why the number is printed as a scope statement rather than chased. The one shape in
neither column *and* not covered by renaming a head is a chain whose head is an **expression**
rather than a named variable (`get_view().add_valid(…)`): there is nothing to rename, and the
compile sweep, not this instrument, is what finds it. Named here rather than left for round 3.

⚠️ **That instrument's first version reported 30 of 39 and was wrong**: it scanned whole *files*, so
in a file of many `TEST(...) { table_view tv; … }` blocks, test 1's reads of `tv` preceded test 2's
writes to a *different* `tv`. Scoping by brace depth is the entire difference between 30 and 3. The
scoped version prints both halves of its own two-sidedness on **every** run — the non-zero report and
the count of files reporting zero — so neither can be assumed.
**Any re-run must use the checked-in scoped version; the FILE-scoped `30 of 39` is a fossil** — not
to be confused with the `30 of 32` the scoped version prints, which is its zero-report count. *(v0.1
wrote "29 of 32 files return 0"; the instrument prints 30, because the three interleaves live in two
files. Corrected by quoting the tool's own line rather than restating a hand count.)*

⚠️ **The declaration count moved from 121 to 126 in v0.3, and the reason is a blind spot, not a
correction to the scoping.** v0.2's `DECL` required the type to be **spelled**, so a `table_view`
obtained from a factory and bound with `auto` was invisible — five such scopes in
`tests/wire/validator_type_check_test.cpp` (the seventh migration class, below). The interleave
count, the zero-report count and the three named interleaved scopes are **unchanged**: the five new
declarations are all pure build-then-use, so `3` and `30 of 32` are the same figures as v0.2's and
`118` became `123`. ⚠️ **The naive-vs-scoped comparison is DELETED rather than re-pointed.** v0.2's
tool printed *"the regex OVER-matches when n > total"*; once the scoper reads `auto` the two sets are
not nested in **either** direction — the naive regex matches never-mutated `table_view X;`
declarations the scoper skips, and the scoper matches `auto` declarations the naive regex cannot
spell — so the comparison tests nothing. Both counts are still printed, as two raw facts
(`124` and `126` at `f49f462a`), with no inference drawn from their difference.

A representative scope, before and after:

```cpp
// before
table_view tv;
tv.add_valid("D", 11);
tv.set_field_type(11, field_type::String);
tv.add_group_member(73, 37);
EXPECT_TRUE(tv.field_valid_for("D", 11));

// after
table_view_builder tvb;
tvb.add_valid("D", 11);
tvb.set_field_type(11, field_type::String);
tvb.add_group_member(73, 37);
table_view const tv = std::move(tvb).build();
EXPECT_TRUE(tv.field_valid_for("D", 11));      // every read is byte-unchanged
```

**Is it sed-able? Partly, and the honest answer is the interesting one.** Two of the three edits are
line-local and mechanical:

```
(1) s/\btable_view ([A-Za-z_][A-Za-z0-9_]*);/table_view_builder \1b;/
(2) on lines matching \b\1\.($NAMES)\s*\(   ->   s/\b\1\./\1b./
```

**The third is not sed-able, and it defines the tool.** Inserting
`table_view const \1 = std::move(\1b).build();` after the **last mutator line in the enclosing brace
scope** requires exactly the brace-depth analysis the scoper already performs. **The migration tool
is the measurement instrument** — `--emit-sites` writes one row per mutated declaration carrying the
declaration line, the variable, the last mutator line and the scope end, which *is* the insertion
point. That is a genuine economy and also a risk: a bug in the scoping is a bug in both the census
and the rewrite, so the rewrite must be validated by something independent (§6 seam 5).

⚠️ **The rewrite must be driven from `--emit-sites`, NOT from re-running the regex across the tree.**
v0.1 instructed the opposite — run form (1), then compare its match count against 121, and *"if the
two disagree, the sed is blind to the difference and the compile sweep will find it as a residue."*
That anticipates only **under**-matching. The live direction is the other one: the naive regex
matches **more** declarations than the scoper found mutated (`--naive-decl-count` prints both counts
side by side, over the files this migration touches, and the adversarial review measured the same
direction across the whole tree). The surplus are declarations that are **never mutated**; rewriting
them into builders with no `build()` and no view breaks use sites in TUs the census reports clean.
That is damage the tool **creates**, not residue it leaves — and a count comparison performed after
the fact cannot prevent it. Driving from the site table makes the class impossible rather than
detectable.

⚠️ **But driving from the site table INHERITS the site table's blind spots, and v0.2 left nothing
checking them.** The instruction above is right about the over-match direction it was written for;
it also makes `--emit-sites` the *only* input, so a mutator call the scoper does not attribute is a
call nobody rewrites — which is exactly what happened to §5d item 7's `auto` class. The instruction
stands, and the completeness it assumes is now **enforced by the instrument** rather than assumed by
the prose: the attribution guard, above, refuses to exit zero while any mutator call in a scanned
file falls outside every declaration the scoper found.

**What a mechanical rewrite CANNOT do — seven things, each needing a human:**

1. **The 3 interleaves.** The one in `Dictionary::as_table_view()` is an **artifact, not an
   interleave** — the use is `return tv;  // null handle (moved-from Dictionary) → empty table_view`
   on the early exit, before any mutation; under the builder it becomes
   `return std::move(b).build();`. The other two are `DictHooksCustomPair.ZeroIsNeverHalfOfAPair` and
   `DictHooksCustomPair.ABundleIsASnapshotOfTheDictionaryItWasBuiltFrom`, dispositioned individually
   below.
2. **The 7 bare non-`const` `table_view&` sites**, where the reference *is* the mutation channel and
   there is no named local to rename: the three mutating lambdas in
   `tests/dictionary/table_view_pair_oom_test.cpp` (they become `table_view_builder&` lambdas and are
   the re-grounding of the three surviving OOM cases, §3.2); the two assigning lambdas in the same
   file (deleted with their two tests); `group73_table_view()` and `legs_and_alt_table_view()` in
   `tests/support/typed_group_table_views.hpp`; and
   `fill_group_with_trailing_field_dict(fixpp::dict::table_view& dict)` in
   `tests/wire/offset_table_test.cpp`, which becomes `(table_view_builder& dict)`.
3. **`tests/support/typed_group_table_views.hpp`** — 20 mutator lines inside two IIFE-initialised
   function-local statics, both returning non-`const` `table_view&`. Both become `table_view const&`
   over a `static table_view tv = [] { table_view_builder b; …; return std::move(b).build(); }();`.
   **The const-return half is measured**, and the evidence is three-part because two parts are not
   enough:
   1. the overlay **differs from the original** — `cmp` refuses an identical file, and the diff shows
      exactly the two changed signature lines;
   2. the overlay was **read** — `#error FIXPP_456_OVERLAY_WAS_READ` seeded into it, the diagnostic
      observed, then removed;
   3. with both established, it compiles its includer
      `tests/wire/repeating_group_equivalence_test.cpp` with **0 errors**.

   It works because `Parser`'s constructor is `explicit Parser(TV&& dict_metadata) noexcept`, a
   forwarding reference.

   > ⚠️ **Part 1 is not bookkeeping, and this rewrite learned that by failing.** v0.1 offered parts 2
   > and 3 only. On the first re-run here the `sed` building the overlay **silently did not match**,
   > so the overlay was a byte-copy of the real header — and it reported *"shadow witnessed: True,
   > 0 errors"*, because **a seeded-`#error` proves the overlay was READ, not that it was CHANGED.**
   > A read-witness and a green result are jointly consistent with the mutation never having been
   > applied. The `cmp` guard is what distinguishes them, and it is the same discipline
   > `tools/table_view_seal_sweep.py` already enforces by construction — that script *generates* its
   > sealed header from named anchors and hard-fails when an anchor is absent, so "the edit did not
   > apply" cannot be silent there. Any hand-built overlay needs the guard the generator gets for
   > free.
4. **`DictHooksCustomPair.ZeroIsNeverHalfOfAPair`** — it asserts readback *between* two
   `set_length_pair_data_tag` calls, so it needs state visible mid-build. The test reads exactly
   three accessors (executed: the distinct `tv.`-qualified reads in that case are
   `length_pair_data_tag`, `data_pair_length_tag`, `has_nonstandard_pair`), and every one of them
   returns a **scalar**. **Decision: give the builder those three `const` readback forwarders** —
   `std::uint16_t length_pair_data_tag(std::uint16_t) const noexcept`, its inverse, and
   `bool has_nonstandard_pair() const noexcept` — and restructure the test to
   `b.set_length_pair_data_tag(0, 5002); EXPECT_EQ(b.length_pair_data_tag(0), 0U);`.

   ⚠️ **Rejected: a `table_view const& peek()` accessor on the builder.** It is the obvious shape and
   it is wrong: the reference it returns points into storage that `build() &&` subsequently moves
   from, so `auto const& v = b.peek(); auto tv = std::move(b).build(); v.field_valid_for(…);` is a
   read of a moved-from view. That would be a **new dangling path introduced by this design**, in
   the same document whose §7 declines the `dict_hooks` dangling hazard as out of scope. Documenting
   it as "invalid after `build()`" is a comment, and a comment is not a check. Scalar forwarders
   have no reference to escape, so the hazard does not exist rather than being warned about.

   Also rejected: splitting into two builders and two `build()` calls — it changes what the test
   asserts, since the case currently pins that a rejected zero leaves *no inverse behind in the
   same table*.
5. **`DictHooksCustomPair.ABundleIsASnapshotOfTheDictionaryItWasBuiltFrom` — deleted, and that is a
   disclosure, not a cleanup.** ⚠️ **Gate B round 1 found the deletion's own justification too wide.**
   Its premise was read as mutate-after-publish in general, made unconstructible; re-grounding it on
   two separate views would test a different claim (two objects, no staleness) and would be a test
   wearing the old one's name. What is actually unconstructible is the **population** half only — a
   bundle latches against an **object at an address**, and `std::optional<table_view>::emplace`
   substitutes a *different* object at that same address without the bundle noticing, which is the
   **identity** half of the same premise and remains fully constructible. It is replaced by §6 seam
   1's compile-time seal witness for the population half, and by
   `DictHooksCustomPair.ABundleKeepsItsNullPairCallbackAcrossAReSeatThatAddsThePair` (`B-456-2`, §5c) for the identity half.
   **Rejected alternative: keeping it alive through a test-only friend or back door** — that
   reintroduces the mutation channel this gate exists to remove, in the one TU most likely to be read
   as authority on the subject. *(Its second assertion — that a bundle built afterwards honours the
   pair — survives the seal and is independently pinned elsewhere in the same file. The deletion lost
   only the move-based reconstruction of the staleness half; the identity-based reconstruction is
   restored under a new name.)*
6. **`Dictionary::as_table_view()` itself** — 13 mutator lines, confirmed by the compile sweep.
   Mechanical in form, but it is the one **production** site, it changes `table_view tv;` into a
   builder across an early return, and it carries §4's one added move construction; it gets read, not
   sed'd.
7. **A `table_view` returned BY VALUE from a factory, bound with `auto`, and mutated further.** The
   live shape is `make_grammar_with_framing` in `tests/wire/validator_type_check_test.cpp`:

   ```cpp
   table_view make_grammar_with_framing(std::string_view msg_type) { table_view t; …; return t; }
   …
   auto t = make_grammar_with_framing("D");                  // the declaration spells no type
   t.add_required("D", 35).add_required("D", 34).set_type(34, field_type::Int);
   dictionary_driven_validator v{std::move(t)};
   ```

   **The edit is different from all six above**: the *factory's return type* becomes
   `table_view_builder`, and each caller gains a `.build()` —
   `dictionary_driven_validator v{std::move(t).build()};`. It is **not** the item-2 shape (that one
   is about references) and **not** item 1 (nothing interleaves; every one of these scopes is pure
   build-then-use).

   ⚠️ **v0.2 was blind to this class in three places at once, and that is the finding — not the five
   sites.** The scoper's `DECL` required the type to be spelled, so it attributed none of these
   calls; the site table §5d makes the *sole* driver of the migration therefore omitted them; and
   the exception list above said **six**. The instrument is fixed (it reads `auto` now, and the
   attribution guard makes an unattributed receiver exit non-zero rather than vanish), the count of
   classes is corrected, and the out-of-tree matrix gains the matching row below. ⚠️ **The number of
   affected sites is deliberately not written here** — `--emit-sites` prints it, and a count in prose
   rots on the next edit to that file. **Mitigation, stated so the severity is not inflated:**
   `validator_type_check_test.cpp` is inside the 32-TU differential (it is the *"largest single TU at
   92 diagnostics"*), so a migration that missed these sites goes **red** in seam 5's full build. The
   defect was in the migration plan and in the census's completeness claims, never a false green in
   the bill.

#### Third-party / out-of-tree break — a matrix, not a two-line recipe

⚠️ **v0.1 claimed *"the mechanical downstream fix is the same two lines as §5d's sed"*, three pages
after enumerating six in-tree shapes for which that is false.** Nothing about being out of tree
removes those shapes, and the universal claim is withdrawn. Every out-of-tree caller breaks at
compile time, loudly, with `'add_valid' is a private member of 'fixpp::dict::table_view'` or
`object of type 'table_view' cannot be assigned`. **There is no silent behaviour change and no
runtime failure mode** — that much is universal, and it is the important half.

| out-of-tree shape | migration |
|---|---|
| direct local construction, build-then-use | **mechanical** — declare a `table_view_builder`, finish with `table_view const tv = std::move(b).build();` |
| a helper taking `table_view&` to populate | **mechanical, signature change** — take `table_view_builder&` |
| a helper *returning* `table_view&` (function-local static, IIFE factory) | **mechanical, signature change** — return `table_view const&` over a built static |
| a helper returning `table_view` **by value** whose result the caller mutates further (`auto t = make_grammar(…); t.add_required(…);`) | **mechanical, signature change at BOTH ends** — the helper returns `table_view_builder`, and every caller gains `.build()` at the point of consumption. ⚠️ Out of tree there is no compiler in this repo to find these, and the `auto` spelling means a grep for `table_view` at the declaration finds nothing either (§5d item 7) |
| reads interleaved with population | **redesign**, unless every mid-build read is one of the three scalars the builder forwards (§5d item 4) |
| chained prvalue — `table_view_builder{}.add_valid(…).build()` | **does not compile, by design** (§3.3). Split into two statements |
| `std::optional<table_view>` seating by assignment | **mechanical** — `emplace`, but only where the optional is disengaged; otherwise `reset()` then `emplace` and think about the strong guarantee you just lost |
| a by-value `table_view` member whose implicit `operator=` disappears | **redesign** — the holder is no longer assignable. Reconstruct it, or hold it differently |
| assigning a `wire::dictionary_driven_validator` | **redesign, and this one has no mechanical fix at all.** A public type goes from copy- and move-assignable to neither (§3.2). "Declare a builder and finish with `build()`" repairs nothing here |

**Version.** A C++ source break with no ABI component, so it lands under the ordinary pre-release
source-break allowance; it does **not** trigger `[const §X.7]`'s C-ABI declaration requirement
(§1a(iii)), and it is **not** owed a `CHANGELOG.md` row today (§5c). ⚠️ **NOT MEASURED: the release
version this actually lands in.** That depends on the release train at merge time.

---

## 6. Test seams

**Seam 1 — the RED: a compile-time seal witness, already proven in both directions.**

The batch plan's stated RED is *"a compile-fail test showing a published view can no longer be
mutated or assigned."* Taken literally that means a must-fail build target, and this repo has that
mechanism — `add_executable(… EXCLUDE_FROM_ALL)` plus `add_test(COMMAND cmake --build … --target …)`
plus `WILL_FAIL TRUE`, as used by `tls_cipher_allow_list_negative_compile` and
`tls_security_profile_deprecated_compile`. **Do not use it here.**

- A must-fail build passes for **any** compile error — a typo, a missing include, a renamed header.
  It is the canonical instrument that cannot report anything but clean, and this repo already wrote
  the argument down: `tests/consumer/CMakeLists.txt`'s rule 2 inverted its own must-not-resolve cells
  to `__has_include` plus a unique-token `#error` **so that they must COMPILE**, recording that under
  the old polarity a real toolchain failure *"would have been a SILENT FALSE GREEN."*
- The form is fragile in a second, independent way: `tls_security_profile_deprecated_negative` carries
  `FIXPP_WERROR_EXEMPT` with the rationale *"negative-compile probe: its passing state is a failed
  build, so a blanket -Werror would let any stray warning keep it green (#417)."*
- And §5d measured a third cost: three such probes sit in the control arm of every compile census
  taken over this tree, forever — and each needs a manifest entry in `tools/table_view_seal_sweep.py`
  to keep that census honest.

**Use the inverted form instead. It needs no new CMake mechanism at all** — it is an ordinary TU in
an existing bucket. The precedent is `tests/sync/test_consumer_contract_compile.cpp`, whose
`has_try_lock` / `declares_shared_variant` block asserts the *absence* of `try_lock`, of an engaged
guard constructor and of three type aliases, with `!concept<T>` and `!is_constructible_v`.

```cpp
// tests/dictionary/table_view_seal_compile_test.cpp
template <class T> concept can_set_pair =
    requires(T& t) { t.set_length_pair_data_tag(std::uint16_t{1}, std::uint16_t{2}); };
template <class T> concept can_add_valid = requires(T& t) { t.add_valid("D", std::uint16_t{11}); };
// … one per mutator, all sixteen

// NEGATIVE — the seal (sixteen of these, one per concept)
static_assert(!can_set_pair<table_view>,              "SEAL LEAK: set_length_pair_data_tag");
static_assert(!std::is_copy_assignable_v<table_view>, "SEAL LEAK: copy-assign");
static_assert(!std::is_move_assignable_v<table_view>, "SEAL LEAK: move-assign");
// POSITIVE — the same concept on the builder, in the SAME TU (sixteen of these too,
//            one paired with each negative above; an unpaired negative is unproven)
static_assert(can_set_pair<table_view_builder>,       "probe blind: concept matches nothing");
// POSITIVE — what must SURVIVE (§1a(iv))
static_assert(std::is_copy_constructible_v<table_view>);
static_assert(std::is_nothrow_move_constructible_v<table_view>);
```

⚠️ **The positive arm on the builder is not decoration; it is what makes the negative arm mean
anything.** A `requires`-expression is false for a **misspelled** member exactly as it is for a
**private** one. `!can_set_pair<table_view>` alone would stay green forever if the mutator were
renamed, or if the concept's argument list drifted. Instantiating the *same* concept on the builder
in the same TU is the structural check that distinguishes "sealed" from "typo'd" — a pairing, not a
spelling.

⚠️ **That argument applies once PER CONCEPT, so the pairing must too — say the count rather than
leaving it to the `// … one per mutator, all sixteen` convention.** The TU carries **sixteen
negatives on `table_view` and sixteen matching positives on `table_view_builder`**, each pair
instantiating the *same* concept in the same TU, plus the two assignment negatives and the two
survival positives of §1a(iv). **An unpaired negative is a probe with no proof that it is not
blind**, and sixteen negatives against one positive would leave fifteen of them unproven.

**Proven able to report non-zero, before the seal exists.** The assertions were compiled against the
simulated-sealed header (`tools/table_view_seal_sweep.py`'s own output, `cmp`-confirmed to differ
from the shipped one) and against the real header, with the builder-side positive arm omitted because
`table_view_builder` does not exist yet in either tree:

| arm | against the **sealed** header | against the **unsealed** header |
|---|---|---|
| four negatives + two positives | **exit 0** — all six pass | **4 errors** — `SEAL LEAK:` `add_valid`, `copy-assign`, `move-assign`, `set_length_pair_data_tag` |

The unsealed run failing on **exactly** the four negatives, by name, while both positives pass in
both runs, is the two-sided evidence: no probe is blind, and none is asserting something true for an
unrelated reason. ⚠️ **It is evidence about these four propositions and nothing wider** — see the
completeness caveat immediately below.

⚠️ **What these sixteen assertions are NOT: a completeness proof for the mutation boundary.** They
are **operation-specific diagnostics**. All sixteen probes are name-keyed, so they cannot see a
newly added `clear()`, a differently spelled mutator, a public data member or a mutable-returning
accessor. The `SEAL LEAK` framing invites a reader to believe otherwise; it should not.

**There is no live bypass today, and that was established rather than assumed** (the adversarial
review executed it over `table_view`'s public band): every accessor returns a scalar, a
`std::span<… const>`, a `std::optional<uint16_t>`, or `valid_tag_set_view`; **no public accessor
returns a mutable reference, pointer or span**; all storage is below the single `private:`; the type
has no public data and no aggregate initialisation path. `valid_tag_set_view`'s
`friend class table_view;` grants `table_view` the right to construct a view *into*
`valid_tag_set_view` — it grants nothing into `table_view` — and that view's backing pointer is
`unordered_set<uint16_t> const*` with only a `const` `contains()` public. It is not a channel.

> ⚠️ **Partial disagreement with Codex 456-R1-3, adopting the adversarial review's replacement.**
> Codex prescribes an AST gate (libTooling or `clang-query`) asserting no public data, no public
> non-`const` members, both assignments deleted, no mutable-returning accessor, and one friendship —
> wired into CI and mutation-proven three ways. **Declined as disproportionate**: a new CI instrument
> whose own failure modes need their own proof, for one type in one header, in a change whose diff is
> access specifiers and one-line forwarders. The observation it rests on is kept. ⚠️ **The
> prescription is declined, not substituted**: v0.2 said seam 2's check *"replaces"* it, which
> overstated a layout guard into a completeness proof. Seam 2 costs one `awk` line and catches a
> second public band; nothing here checks the contents of the first one, and seam 2 now says so.

**Seam 2 — the structural census: friendships AND the single `public:` band.**

*Friendships (`215` §6's G1 shape).* After the change `table_view.hpp` must declare **exactly two**
friendships: `valid_tag_set_view`'s pre-existing `friend class table_view;`, pointing the other way,
and **exactly one** `friend class table_view_builder;`. The baseline today is one
(`grep -c "friend" include/fixpp/dict/table_view.hpp` returns 1). An **exact** count, not "no
unexpected friends": a second friend added later must fail a check rather than pass unnoticed.

⚠️ **Do not implement it as `grep -c "friend"`.** That counts the word in comments and in string
literals as readily as in a declaration, and this header is heavily commented — one explanatory
sentence added to this file would move the count with no declaration changing. Assert the two
**declaration spellings** by pattern on non-comment lines instead:

```
grep -nE '^[[:space:]]*friend[[:space:]]+class[[:space:]]+(table_view|table_view_builder);' \
     include/fixpp/dict/table_view.hpp
```
must return exactly those two lines and nothing else, and a bare `friend` matched anywhere outside
that pattern is itself a failure (it is either a third friendship or a spelling the gate cannot
read).

*The single `public:` band — a LAYOUT guard, not a completeness proof.* `class table_view` has
**exactly one `public:` label** today, and exactly one `private:`, at the data members; the seal adds
a second `private:` before the *"Build-time population surface"* banner, so the `public:` count stays
1. **Assert that count.** It catches, structurally and with no name in it, a second public band
appearing — which is how a re-opened mutation surface would most plausibly arrive.

⚠️ **It does NOT catch a new member added inside the existing band, and v0.2 claimed it did.** v0.2
called this *"the completeness half seam 1 cannot give"*, two lines above its own sentence conceding
that *"a new public mutator can then arrive only by being added to that one band — reviewable and
enumerable."* Review is not a check, and a `void clear();`, a `std::vector<std::uint16_t>&
mutable_tags();` or an `int public_state;` added inside `table_view`'s existing public band leaves
the label count at exactly **one** and satisfies all four mutation arms below. **The claim is
withdrawn, not respelled** — per the repo's own rule that a fix replacing a claim with a *new* claim
reproduces the defect, and only deletion closes it. The `awk`, the exact count and all four arms are
kept unchanged, because they are correct for what they actually measure.

**So there is no automated completeness check over `table_view`'s public surface, by decision** (the
AST gate is declined as disproportionate — seam 1). The residue is carried where it can be acted on
rather than left implicit: seam 7's decision record (`.specify/decisions/456-table-view-seal-verify.md`)
must state that `table_view`'s public surface was **read** at the migrated commit, with the `awk`
output pasted, as an explicit human disposition. That is an obligation Gate B discharges, not one it
inherits.

⚠️ **It must be SCOPED TO THE CLASS BODY, and a bare `grep -c '^public:'` over the header is already
wrong today.** `table_view.hpp` also defines `valid_tag_set_view`, which has a `public:` of its own,
so the whole-file count is **2** while `class table_view`'s is **1**. A check written as a file-wide
grep would therefore start life mis-calibrated, and would then be "fixed" by hardcoding 2 — after
which a `public:` added to *either* class passes. Scope the range first:

```
awk '/^class table_view \{/{f=1} f&&/^(public|private|protected):/{print NR": "$0} f&&/^\};/{exit}' \
    include/fixpp/dict/table_view.hpp
```

**Prove every arm of seam 2 can report non-zero, both ways** — four arms, and the last one exists
only because of the scoping trap above:
- add a third `friend class Dictionary;` and assert the friend pattern returns 3;
- put the word `friend` inside a comment and assert the count does **not** move;
- add a second `public:` **inside `class table_view`** and assert the band check fails;
- add a second `public:` **inside `valid_tag_set_view`** and assert the band check does **not**
  move. A check that fires here is file-scoped rather than class-scoped, and is measuring the wrong
  thing in a way no other arm can see.

**Seam 3 — the surviving half of the strong guarantee.** The three mutation-path OOM cases in
`tests/dictionary/table_view_pair_oom_test.cpp` are **re-grounded on the builder**, not deleted;
their lambdas take `table_view_builder&` instead of `table_view&`. They are the only remaining
coverage of `set_length_pair_data_tag`'s rollback, so losing them silently would drop the invariant
§3.2 claims to preserve. Mutation arm: delete the rollback's `catch` block and assert the cases go
RED.

**Seam 4 — `build()` produces the same table the old direct population did.** A differential test:
populate a `table_view_builder` with a fixed script, `build()`, and compare every accessor against
the values `Dictionary::as_table_view()` produces for a real dictionary —
`TableViewTest.FullFix44DictionaryTableView` already does the second half and becomes the comparand.
This is what catches a forwarder that forwards the wrong argument, which sixteen near-identical
one-line forwarders make a live risk.

**Seam 5 — the migration is validated by an instrument independent of the tool that performed it.**
The rewrite is emitted by `tools/table_view_mutation_scope.py --emit-sites`; the census came from the
same scoping pass, so a scoping bug is a bug in both. Something independent must check it.

⚠️ **That something is NOT the seal sweep, and v0.1's acceptance criterion for this seam was
doubly unsound.** It said *"after migration, the sweep's **sealed** arm must report the same 3 TUs the
**control** arm does and nothing else."* Two problems, and the first is the whole of N-P1-1:

- **That pass condition is also the signature of the instrument having done nothing.** A sweep
  degraded by a relative `--scratch` reports exactly the control's TUs and nothing else, *because it
  is the control*. A migration leaving fifty mutator calls unconverted would have passed. §2a's
  shadow witness now makes that particular degradation impossible, but the criterion was the wrong
  shape regardless.
- **The sweep cannot run after the seal lands at all.** It simulates a seal by rewriting an unsealed
  header; post-implementation its anchors are gone and it hard-fails by design (§2a). It is the
  **pre-implementation bill** instrument and nothing else.

**The post-migration validator is an ordinary full build of the migrated tree, plus `ctest`** —
every TU compiled for real, against the real sealed header, with no simulation anywhere in the loop.
It must be run against a `FIXPP_BUILD_FUZZ=ON` configuration so the fuzz tier is not blind (§5d), and
the pre-implementation sweep must itself be re-run on a fuzz-enabled build **before** implementation
so the bill is complete.

⚠️ **The `FIXPP_BUILD_FUZZ=ON` half needs its own witness, and without one it is the same shape as
the criterion this seam just replaced.** A build configured **without** the flag builds fewer targets
and goes green — so "green" is also the signature of the fuzz tier never having been built. The
pre-implementation half is already safe by accident of guard 3: a `--filter /tests/fuzz/` sweep on a
non-fuzz build selects nothing and raises *"FILTER … SELECTED NO TU — a sweep over nothing reports
zero"* (**zero fuzz TUs are configured in `linux-clang-debug` today**, executed). The post-migration
build has no such guard. **Acceptance therefore requires the count first**: state how many
`/tests/fuzz/` TUs the migrated build's own `compile_commands.json` configures, and require it to be
**non-zero**, before the green build is accepted as evidence of anything. ⚠️ **This is an obligation
Gate B must DISCHARGE, not inherit** — neither the sweep re-run nor the fuzz-enabled build has been
performed by this document, and both are named here so that the absence is visible rather than
implied. **Both instruments are checked in under `tools/` as part of this change** —
a design-time instrument that is not preserved cannot be re-run at Gate B, which is the defect that
left every figure in v0.1 resting on a session scratchpad.

**Seam 6 — the two `emplace` conversions carry their precondition as an assertion, not as a note.**
§3.2 argues that `owned_tv_ = …` converts to `owned_tv_.emplace(…)` with no behavioural delta
*because both optionals are disengaged there*. That is a reading of two call sites, and a reading is
what rots the moment someone reuses an `impl` or a clone. Each converted site therefore carries
`assert(!…owned_tv_.has_value())` immediately before the `emplace`, in
`src/dictionary/reify.cpp` and `src/capi/message_write.cpp`. **Prove the assertions can fire**:
seat the optional before the call in a scratch build and confirm each aborts under
`linux-clang-debug`. If either cannot be made to fire, the site is not the one §3.2 read and the
argument has to be redone.

**Seam 7 — coverage.** `[const §IX.1]`: the per-line **assessment** is the gate and the percentage is
the target, with three dispositions available (tested, waived with a one-line rationale, or escalated
to a filed issue). The diff is almost entirely access specifiers and one-line forwarders; the
forwarders are trivial and exercised by seam 4, and `build()`'s single `return std::move(tv_);` is
exercised by every migrated test. Every uncovered line gets its one-line disposition in
`.specify/decisions/456-table-view-seal-verify.md`.

**The same record carries two obligations this design deliberately does not automate**, so that they
are discharged with a witness rather than inherited as prose:

1. **`table_view`'s public surface was READ** at the migrated commit, with seam 2's `awk` output
   pasted beside it, as an explicit human disposition — because seam 2's band check is a layout
   guard and no automated completeness check exists, by decision (seam 2, §7).
2. **The `/tests/fuzz/` TU count the migrated build's own `compile_commands.json` configures**, which
   must be **non-zero** before seam 5's green full build is accepted as evidence — because a build
   configured without `FIXPP_BUILD_FUZZ=ON` also goes green (seam 5).

**Tests added, changed, deleted:**

| test | disposition | reason |
|---|---|---|
| `tests/dictionary/table_view_seal_compile_test.cpp` | **added** | seam 1 — the RED |
| `TableViewPairOom.NewPairInsertionIsAllOrNothing` / `.RepairingTheLengthSideIsAllOrNothing` / `.RepairingTheDataSideIsAllOrNothing` | **changed** | lambdas take `table_view_builder&`; the guarantee's surviving half (seam 3) |
| `TableViewPairOom.CopyAssignmentIsAllOrNothing` / `.CopyAssignmentOverAPopulatedTargetIsAllOrNothing` | **deleted** | their subject (assignment) no longer exists |
| `DictHooksCustomPair.CopyAssignmentCarriesTheFlagWithThePairs` | **deleted** | same — and it is also the proof that assignment had to go (§3.2) |
| `DictHooksCustomPair.ABundleIsASnapshotOfTheDictionaryItWasBuiltFrom` | **deleted** | population half of premise unconstructible; replaced by seam 1 (population) and `ABundleKeepsItsNullPairCallbackAcrossAReSeatThatAddsThePair` (identity, `B-456-2`) — **a disclosure** (§5d, item 5) |
| `DictHooksCustomPair.ZeroIsNeverHalfOfAPair` | **changed** | reads through the builder's three scalar readback forwarders (§5d, item 4) |
| `TableViewTest.SpansRemainingValidAfterDictionaryDestroyed` | **changed** | move-**construct** instead of move-assign (§3.2) |
| `validator_domain_test.cpp`'s `is_copy_constructible_v` and `!is_polymorphic_v` static_asserts | **unchanged** | both still hold; measured |
| `src/dictionary/reify.cpp` and `src/capi/message_write.cpp` `emplace` preconditions | **added** (assertions, not tests) | seam 6 — §3.2's one read-not-executed claim |
| the other migrated test TUs | **changed** mechanically | §5d, driven from `--emit-sites` |
| `tests/support/typed_group_table_views.hpp` | **changed** | builder plus `const&` return (§5d, item 3) |
| `tools/table_view_seal_sweep.py` / `tools/table_view_mutation_scope.py` | **added** (instruments) | §2a, seam 5 |

---

## 7. What this does not decide

- **Validation inside `build()`.** `B-384-2`'s reachability — a group with members and no first
  field — survives this change unchanged (§5c), because the builder permits exactly the call sequence
  the row describes. The seal *creates the place* where such a check could live, which is the only
  thing that improves. Deliberately out of scope: it is a behaviour change with its own
  fail-closed / fail-open question, and folding it in would make the seal's diff unreviewable.
  Recorded as `L-456-1`.
- **The `dict_hooks` dangling-pointer half.** `for_table_view` stores a raw non-owning `void const*`
  to the view, and every membership and delimiter callback reads it live (§2, §5b). A bundle
  outliving its `table_view` is a dangling read today and still is afterwards. This gate closes the
  **staleness** half of #456 and not the **lifetime** half. Not filed as an issue by this document;
  it should be.
- **Whether `dictionary_driven_validator` should hold its `table_view` by `shared_ptr`.** SC-007 is
  frozen and `215` §7 already ruled it explicitly out of scope; `L-215-2` stays. This design does
  make the validator non-assignable as a side effect (§3.2), which is a fact about it, not a reason
  to reopen it.
- **Whether `Dictionary` should mint a builder rather than a view.** `as_table_view()`'s signature is
  unchanged here. If engine-side view caching is ever built (`215` §7's standing Option E
  follow-up), the builder is the primitive that would mint into it, and this gate does not foreclose
  that.
- **The release version this lands in** (§5d) — NOT MEASURED; depends on the release train at merge
  time. No `CHANGELOG.md` row is owed before then (§5c).
- **MSVC confirmation of `is_nothrow_move_constructible_v<table_view>` under the INFERRED move
  constructor** (§3.2) — **NOT MEASURED**; no MSVC toolchain was available here. `linux-gcc-release`
  and `tier3-libcxx` are **discharged by measurement**; Tier-2 MSVC is the one lane that can still
  decide it, and — this is the point of the inferred form — it genuinely can. A failure there is a
  finding about MSVC's containers, not a reason to restore the promise.
- **The fuzz tier's share of the migration** — the compile census is structurally blind to it, as it
  is to `bench/`, `bindings/` and the top-level `perf/`, none of which configures a TU in
  `linux-clang-debug` (§5d). `tests/fuzz/fuzz_wire_parser.cpp` is known to be in the bill from the
  mutator-name grep; whether anything else is requires the sweep re-run on a `FIXPP_BUILD_FUZZ=ON`
  build.
- **Whether an AST-level structural gate on `table_view` is worth building.** Declined here as
  disproportionate (§6 seam 1). ⚠️ **Nothing replaces it.** Seam 2's one-`public:`-band check is a
  layout guard: it catches a second public band, not a new member added inside the first one, so
  **no automated completeness check over `table_view`'s public surface exists — by decision.** The
  human disposition that stands in for it is seam 2's Gate B obligation. If the type ever grows a
  second public band for a legitimate reason, or if the public surface starts changing often enough
  that reading it stops being credible, revisit.

---

## Normative References

Per `.specify/215-dictionary-view.md`'s precedent: `[const §VI.5]` binds the Normative References
requirement to `/specify` artifacts, and a `.specify/` design doc is not one. The section is included
**voluntarily**, following `architecture.md`'s stated precedent for design docs.

**Normative FIX references informing this design: NONE.** This document decides the C++ shape of one
public type. It changes no wire behaviour, no session FSM state or transition, no message grammar, no
dictionary semantics and no C ABI surface. The grammar a session validates against is identical
before and after. No `[DocAbbrev §X.Y.Z]` entry from `spec/coverage-index.md` informs it, and
inventing one would be worse than none. Model: `2f-async-mutex.md`.

**Process / constitutional references.** ⚠️ Each row below was opened at `f49f462a` and the quoted
text is printed beside it.

| citation | what it says, as used here |
|---|---|
| `[const §XVII.1]` | *"Touches the public C++ API or C ABI"* — this gate's trigger (status block) |
| `[const §XVII.1]` | *"Any new design document under `.specify/` … qualifies by default"* — why this document itself goes through Gate A |
| `[const §XVII.8]` | the verification gate — `/speckit-verify` mandatory after `/speckit-implement`, and the label-evidence rule (*"the labels are evidence claims, not status decorations"*) — §6 seam 7's decision record |
| `[const §IX.1]` | *"no uncovered error/edge path without an explicit assessment — that is the enforced gate; the percentage is the target"*, and **THREE** dispositions, not two (tested / waived / escalated) — §6 seam 7 |
| `[const §XV.1]` | *"Heap-allocate per message or per field on the hot path"* — why the builder is config-time only (§5a) |
| `[const §VIII.2]` | *"measured as a **paired base-vs-candidate run on one runner** — both trees built and benchmarked in the same job, A-B-A-B, compared min-per-tree"* — the paired run §4 and §5a ask for. ⚠️ **v0.1 cited `[const §VIII.3]` for this clause; §VIII.3 is *"No perf change merged without a benchmark in the same PR"***, a different obligation |
| `[const §VIII.5]` | zero `new`/`delete` between parse and `fromApp` — not engaged; nothing here sits on that window (§5a) |
| `[const §XIV.2]` | *"≤5 pure-virtual methods"* — untouched; this design adds no virtual (§5a) |
| `[const §X.7]` | *"Before the first public release, a breaking C-ABI change is allowed but must be declared"* — cited to record that it is **NOT** engaged: this is a C++ source break with no ABI component (§1a(iii), §5d) |
| `[const §XIX.5]` | *"Pages tied to public API surfaces must be regenerated when the surface changes. Doxygen output drift is a Gate B finding."* — recorded as **satisfied by absence**, with the executed check (§5c) |
| `[const §XX.3]` / `[const §XX.4]` | a `CHANGELOG.md` entry is bound to backwards-incompatible **constitutional amendments** — cited to record that this change does **not** owe one (§5c) |

**Inherited design contracts** (not FIX-normative, listed so a reader can tell them apart):
**SC-007**, the frozen *"no virtual edge"* design point keeping `dictionary_driven_validator`'s
`table_view` by value in `include/fixpp/wire/validator.hpp`, which is why §1a(iv) is unconditional;
fixpp#426 design §3's bundle-snapshot contract and its Gate B r6 M-2 / r7 N-1 / r7 N-2 / r7 N-4
rulings, which §3.2 and §5c amend; and `.specify/215-dictionary-view.md` §4's Option A–F
adjudication, which §1a(i) declines to reopen.

**Design-document references:** `.specify/215-dictionary-view.md` (this document's parent; §7's
residue is its subject) and `.specify/426-428-length-data-pairs.md` §3 (the sentence §5c amends).

---

## Appendix — Convergence log

One section per revision. **Nothing in an earlier section is edited when a later round falsifies
it** — the falsification is recorded in the later section and flagged in place, because a rewritten
history cannot show which claims a process caught and which it did not.

---

## v0.1 → v0.2 (round 1)

**Round 1.** Codex review (`research/reviews/codex_456_table-view-seal_review.md`, 1 P1 / 4 P2) and
Opus adversarial review (`research/reviews/opus_456_table-view-seal_adversarial_review.md`, judging
every Codex finding and adding six of its own; post-judging `P1=2 P2=3 P3=5`). The adversarial review
re-ran every instrument in v0.1 and reproduced **every figure exactly** — `3 TUs / 15 errors` on
`/src/`, `3 / 3` control and `35 / 828` sealed on `/tests/`, the `32` differential as a genuine set
difference, `121 / 3` on the scope analysis, `0` configured fuzz TUs. **No finding touched the
design.** All twelve were in claims, instruments and censuses.

### The three root causes, and how v0.2 closes them

| RC | v0.1 | v0.2 |
|---|---|---|
| **RC-1 — a proof replaced by a promise** | `static_assert(is_nothrow_move_constructible_v<table_view>)` offered as evidence, against a move constructor spelled `noexcept = default`. P1286R2 makes the trait true by construction, so the assertion could not report the failure it was installed to catch — in the one bullet quoting the r7 N-2 ruling that explains why not | the implementation **drops the explicit `noexcept`**, leaving the specification inferred; the assertion becomes a proof. Proven two-sided at the **real type** (inferred + throwing-move member ⇒ RED; explicit `noexcept` + the same member ⇒ GREEN; inferred + clean ⇒ GREEN), and the drop measured free on clang/libstdc++, clang/libc++ and g++. r7 N-2's comment moves to the move constructor. §3.2 |
| **RC-2 — the instrument is trusted but not carried, and it fails toward clean** | both scripts lived in a session scratchpad; `--scratch` was accepted relative, so the sealed arm could silently become the control — whose output **is** seam 5's pass condition; the control's three failures were adjudicated by hand while §2a described a rule the script did not implement | both checked in: `tools/table_view_seal_sweep.py`, `tools/table_view_mutation_scope.py`. Absolute `--scratch` with a hard failure on a missing/empty root; an **asserted** seeded-`#error` shadow witness on the sealed arm (the control-side mirror is labelled for what it is — a script-level guard that cannot fail on input, see #17); a vacuity guard; an **expected-failure manifest** binding each negative-compile probe to its diagnostic so the control passes on identity; the differential emitted as a **set operation by the tool**. Each guard mutation-proven. Every figure re-run through the checked-in path. §2a, §3.2, §5d, seam 5 |
| **RC-3 — the documentation census stopped at `.specify/`** | design docs and the B&L ledger swept; the **shipped headers** were not, and `mock_dict_table.hpp` was a dropped input from the pre-gate measurements (M5) | §5c gains a prose-disposition table covering `dictionary_snapshot.hpp` (two sentences), `table_view.hpp` (class comment + copy/move block + the r7 N-2 comment's new home), `dictionary.hpp`'s `as_table_view()` contract, `mock_dict_table.hpp`, and the 426 design doc. Article XIX §5 recorded **satisfied by absence** with the executed check |

### Per-finding disposition

| # | source | sev | finding | resolution in v0.2 |
|---|---|---|---|---|
| 1 | Codex R1-1 | **P1** | the replacement nothrow assertion is tautological against the explicit `noexcept`; three CI lanes handed an undischargeable obligation | **Applied, with a measured refinement.** RC-1 above. ⚠️ The residual is **narrowed, not deleted** — see the disagreement below |
| 2 | Opus N-P1-1 | **P1** | `seal_sweep.py` silently degrades to its control arm on a relative `--scratch`; the degraded output **is** seam 5's pass condition | **Applied, twice over.** Absolute resolution + hard failure closes the degradation; the asserted shadow witness closes the class; and seam 5's criterion is **replaced** rather than patched — the post-migration validator is a full build, because the sweep cannot run after the seal lands at all. §2a, seam 5 |
| 3 | Codex R1-2 | **P2** | results without a rerunnable instrument; §2a's control rule contradicts the run it reports (`rc=2`, *"meaningless"*) | **Contradiction applied; "not reconstructable" half DISAGREED** — see below. Both scripts checked in; the manifest makes the control pass on identity, so the rule §2a states is now the rule the script implements |
| 4 | Codex R1-4 | **P2** | the out-of-tree recipe contradicts §5d's own six exception classes; no migration offered for `dictionary_driven_validator` assignment | **Applied.** The universal two-line claim is withdrawn and replaced by an eight-row migration matrix, ending with the validator row marked *"redesign, and this one has no mechanical fix at all"*. §5d |
| 5 | Codex R1-5 | **P2** | the documentation impact inventory is incomplete for a public source break | **Applied, plus the M5 dropped input.** RC-3 above. ⚠️ **Doxygen clause DISAGREED** — see below |
| 6 | Opus N-P2-1 | **P2** | the sweep omits `bench/`, `bindings/` and 903 `_codegen` TUs; the mutator grep is no complement for the assignment half | **Applied, and widened to FIVE blind spots.** The grep's scope is corrected to the *mutator* half only; `/tools/` is swept directly; `_codegen`'s and the top-level `perf/`'s `table_view`-absence are executed. ⚠️ The review's `/perf/` arm is **not** a fifth root — `--filter` is a substring match, so `/perf/` selects `tests/perf/` TUs already inside `/tests/`; the row is removed rather than restated, and the real top-level `perf/` (zero configured TUs) is named as a blind spot instead. §5d |
| 7 | Codex R1-3 | P2 → **P3** | name-keyed probes are not completeness — but no live bypass; AST gate disproportionate | **Observation applied, prescription DECLINED with reasoning.** §6 seam 1 now says plainly that the sixteen concepts are operation-specific diagnostics; the completeness half is covered by seam 2's **one-`public:`-label** structural check — no name in it, class-scoped, and specified with four mutation arms (see #22) |
| 8 | Opus N-P3-1 | **P3** | the migration sed **over**-matches; §5d anticipates only under-matching | **Applied by deletion of the check, not correction of it.** The rewrite is driven from `--emit-sites`, which makes the class impossible; v0.1's "compare the counts afterwards" instruction is removed. `--naive-decl-count` keeps the direction re-derivable without quoting a number |
| 9 | Opus N-P3-2 | **P3** | the `sizeof` 728 sealed/unsealed identity was structurally forced | **Applied by DELETING the measurement.** §4 states the structure — member functions do not participate in layout — and hands the byte count back to the bench baseline, which owns it |
| 10 | Opus N-P3-3 | **P3** | `as_table_view()` NRVO → exactly one added move, never stated | **Applied.** §4 states it, with the mechanism (NRVO cannot apply to a return of a member; guaranteed elision makes the outer return free) and the `[const §VIII.2]` paired run that covers it |
| 11 | Opus N-P3-4 | **P3** | `[const §VIII.3]` cited for `[const §VIII.2]`'s paired-run clause | **Applied.** Corrected at both use sites and in the citation table, which now quotes §VIII.2 verbatim and records what §VIII.3 actually says. The eight citations the review verified exact are **untouched** |
| 12 | Opus N-P3-5 | **P3** | seam 7 precedes seam 6 | **Applied.** The `emplace`-precondition seam is now **seam 6** and coverage is **seam 7**; every cross-reference in §3.2 and §6 follows |
| 13 | *this rewrite* | — | v0.1's *"29 of 32 files return 0"* is wrong — the scoper prints **30**; the three interleaves live in two files | **Applied by quoting the tool's own line** instead of restating a hand count. Found only because the figure was re-run through the checked-in script, which is the point of RC-2 |
| 14 | *this rewrite* | — | v0.1 enumerated the `sizeof(table_view)` grep's hits and the enumeration was **already incomplete** (it omitted a comment in `bench/dictionary/CMakeLists.txt`) | **Applied by replacing the enumeration with the CONDITION** — *no hit is a `static_assert` or an `EXPECT_EQ`* — which is what the claim always rested on and which cannot rot as the list does. §4 |
| 15 | *this rewrite* | — | v0.1 enumerated what `grep -rn "assignable"` returns and that list was **already incomplete** too (it omitted `MessageView<access_mode::Index>`) | **Applied the same way**: the condition is *no `is_*_assignable` assertion names the validator*, with the narrowing grep that checks exactly that. §3.2 |
| 16 | *this rewrite* | — | a seeded-`#error` shadow witness proves an overlay was **READ**, not that it was **CHANGED** — v0.1's typed-group overlay evidence (§5d item 3) had only the read-witness, and on re-run here the `sed` building that overlay silently failed to match while the run still reported *"shadow witnessed: True, 0 errors"* | **Applied.** §5d item 3's evidence is now three-part, with `cmp` refusing an overlay identical to its original. Noted that `tools/table_view_seal_sweep.py` gets this for free — it GENERATES its sealed header from named anchors and hard-fails when one is absent, so a non-applying edit cannot be silent there |
| 17 | *this rewrite, post-draft review* | — | §2a claimed the shadow witness was **two-sided**, but the control arm injects no `-I`, so its "token ABSENT" assertion **cannot fail on any input** — the repo's #1 class, inside the guard written to close it | **Applied by narrowing the claim to what the guard is.** It is a *script-level* regression guard (it fires if the `-I` injection becomes unconditional), labelled as such in the tool's own output, and **mutation-proven in that role** — the inverse of the degradation mutant makes it fire |
| 18 | *this rewrite, post-draft review* | — | `--emit-set` on a single `--arm sealed` run wrote the three must-fail probes into the file §5d calls *"what a migration should read"* — the exact failure the control arm exists to prevent, reachable from inside the tool | **Applied.** The sealed arm subtracts the manifest before writing and prints how many probes it withheld |
| 19 | *this rewrite, post-draft review* | — | the differential's TU count was a set operation but its **error count was still `sealed_total − control_total`** — a by-count subtraction one line below the fix that replaced by-count subtraction | **Applied.** Summed over the set difference instead; same figure today, exact by construction |
| 20 | *this rewrite, post-draft review* | — | `--filter /perf/` was reported as a swept *complement*; `--filter` is a substring match, so it selects `tests/perf/` TUs **already inside `/tests/`** | **Applied by deleting the row**, not restating it. The genuine top-level `perf/` (zero configured TUs, zero `table_view` mentions) is added as a **fifth** blind spot |
| 21 | *this rewrite, post-draft review* | — | two inherited timing figures were quoted without their provenance: `215` §1's 8.0 %/23.2 % copy share and its `as_table_view()` millisecond figure, both annotated **pre-082** at their source, in a function 082 added work to | **Applied — one caveated, one DELETED.** The copy share is cited as a *direction* with the pre-082 condition stated; the millisecond figure is removed and replaced by the structure it stood for (a config-time full-dictionary walk), because the decision never depended on the value. §3.3, §4 |
| 22 | *this rewrite, post-draft review* | — | the one-`public:`-label check prescribed for seam 2 is **not implementable as a file-wide grep**: `table_view.hpp` also defines `valid_tag_set_view`, so `grep -c '^public:'` returns **2** where `class table_view`'s count is **1** | **Applied.** Seam 2 now specifies a class-scoped range and adds a **fourth** mutation arm — a `public:` added to `valid_tag_set_view` must NOT move the count — which is the only arm that can distinguish a class-scoped check from a file-scoped one |
| 23 | *this rewrite, post-draft review* | — | v0.2's own first draft **dropped v0.1's seam-1 two-sided evidence table** (the six assertions compiled against the sealed and the unsealed header) while rewriting §6 around the completeness caveat — a regression introduced by a fix, in a document whose thesis is that evidence must be carried | **Restored and re-run**, not pasted back from v0.1: sealed ⇒ exit 0, unsealed ⇒ exactly the four `SEAL LEAK:` negatives by name, against a `cmp`-confirmed sealed overlay. §6 seam 1 |

### Disagreements recorded, with reasoning

1. **Codex R1-2's "not reconstructable" half — DISAGREED, by execution.** Codex ran
   `test -e tools/seal_sweep.py`, found nothing, and concluded the figures were unverifiable. The
   scripts existed in the orchestrator's scratchpad; Codex had no way to see them. The adversarial
   review **ran them and reproduced every figure exactly**. So v0.1's numbers were never wrong, and
   v0.2 says so plainly rather than implying a correction: **this fix is about durability, not
   accuracy.** The finding's other half — the control-semantics contradiction — is confirmed and
   applied.
2. **Codex R1-5's Doxygen clause — DISAGREED, adopting the adversarial review's judgment.**
   `[const §XIX.5]` **exists and Codex quoted it accurately**. But `docs/` has no Doxyfile and no
   workflow runs doxygen, so there is **no generation step to omit**. Adding a regeneration
   obligation would add a task that cannot run. Recorded as satisfied-by-absence with the executed
   check (§5c).
3. **The adversarial review's "delete the MSVC/libc++ residual" — PARTIALLY DISAGREED, by
   measurement.** The review's own reasoning carries the qualifier *"while the explicit promise
   stands"*. Once the promise is dropped — which is the fix — the trait is decided by the members,
   and `std::unordered_map`'s move constructor is not required by the standard to be `noexcept`. **A
   residual that a lane can decide is what the inferred form is FOR**; deleting it would put back an
   unmeasured "this holds everywhere". So the residual is **narrowed by measurement** instead:
   `linux-gcc-release` and `tier3-libcxx` discharged (measured here under the inferred overlay),
   **MSVC still NOT MEASURED and still live**. §3.2, §7.
4. **Codex R1-3's AST gate — DECLINED**, per the adversarial review, replaced by seam 2's
   one-`public:`-label check. Reasoning in §6 seam 1.

### Claims DELETED rather than restated

Recorded separately because it is the repo's own rule — a fix that replaces a claim with a *new*
claim reproduces the defect, and only deletion closes it:

- the `sizeof(table_view)` **sealed-vs-unsealed measurement** (#9) — replaced by the structural
  statement that no measurement could strengthen;
- the **"compare the sed's match count against 121 afterwards"** instruction (#8) — replaced by
  driving the rewrite from the site table, so the count comparison has nothing to check;
- the **universal two-line downstream recipe** (#4) — replaced by a matrix that says where no
  mechanical fix exists;
- the **"needs a `CHANGELOG.md` entry"** obligation (§5c) — checked against the file's own scope block
  and `[const §XX.3]`/`§XX.4` and found not owed; not restated in weaker form;
- **"29 of 32 files return 0"** (#13) — replaced by the instrument's own printed line;
- **two grep-hit ENUMERATIONS** (#14, #15) — both were already incomplete at `f49f462a`, and neither
  was ever the claim. Replaced by the conditions they stood in for. ⚠️ **#13, #14 and #15 are one
  shape: a count or a list standing where a condition belonged.** None was reachable by reading; all
  three surfaced from re-running the figure through a checked-in instrument, which is the argument
  for RC-2 restated as evidence;
- the **read-witness-alone** evidence for the typed-group overlay (#16) — not weakened, *extended*: the claim it supported reproduces, but the witness it rested on could not have told the difference;
- **seam 5's "the sealed arm must report the same 3 TUs the control does"** (#2) — deleted, not
  loosened. It was simultaneously the pass condition of a broken instrument and a criterion the
  instrument cannot evaluate post-implementation;
- the **`--filter /perf/` coverage row** (#20) — deleted once it turned out to select `tests/perf/`
  TUs already inside the `/tests/` sweep. A subset re-run reported as a complement is a coverage
  claim with nothing behind it, and there was nothing to restate.

### Net effect — as v0.2 claimed it, and where round 2 falsified it

⚠️ **Left as written, with the correction flagged rather than folded in.** The clause below —
*"every number now comes from a checked-in instrument that cannot report a plausible zero"* — is
**false as of round 2** and is one of three statements 456-R2-1 contradicted (with `Fatal`'s
docstring and §2a's *"each guard raises and exits non-zero before printing any figure"*). v0.2's
sweep classified a failing TU by stderr text alone, so a driver `fatal error:`, a signal death and
an ICE each reported zero. See §2a guard 7 and the v0.2 → v0.3 section below. It is not edited here
because a superlative quietly downgraded after the fact would hide that the process missed it.

The design is **unchanged**: S1 plus deleted assignment, `build() &&`, the scalar readbacks, the
`peek()` refusal, the `215` §4 non-reopening. What changed is that every number now comes from a
checked-in instrument that cannot report a plausible zero, every guard against that has been shown to
fire, the one assertion this design offers as exception-safety evidence has been converted from a
promise into a proof and demonstrated RED against a real mutant, the documentation census reaches the
shipped headers, and the claims listed above were **deleted** rather than rewritten.

⚠️ **Read the rows from #13 on as the honest part of this record, not as a tally.** They were found
*after* both reviews, by this rewrite, in v0.1's material and then in its own first draft — and every
one of them is the class the whole document is about: a claim wider than its evidence, or an
instrument that could not report the failure it was installed to catch. **Two of them (#17, #19) were
introduced by v0.2's own fixes for RC-2** — the pattern PR #325 spent five rounds on, where each
round's fix replaced a false claim with a new one. The guard against it here is the same one the
document prescribes everywhere else: re-run the figure through the checked-in path, and mutate the
guard before believing it. ⚠️ **That also means this list is not evidence of completeness.** It
records what was caught; a v0.3 finding a #22 would be the process working, not a surprise.

---

## v0.2 → v0.3 (round 2)

**Round 2.** Codex review (`research/reviews/codex_456_2_table-view-seal_review.md`, `2 P1 / 1 P2`)
and Opus adversarial review (`research/reviews/opus_456_2_table-view-seal_adversarial_review.md`,
judging every Codex finding and adding four of its own; post-judging `P1=2 P2=2 P3=3`, three root
causes). The adversarial review re-ran `/src/`, the scope analysis and all four blind-spot figures
and reproduced **every one exactly**, audited all nine of v0.2's deletions (**all nine right**), and
verified all eleven of v0.2's self-reported #13–#23 fixes against source. **Again, no finding touched
the design** — the second adversarial pass to reach that verdict. The round does **not** converge.

⚠️ **The prediction at the end of the v0.1 → v0.2 section — *"a v0.3 finding a #22 would be the
process working"* — was met, and the honest reading is narrower than it sounds.** Round 2 found the
RC-2 class for the **third** time, and this occurrence was the first to survive into a checked-in
artifact *and* into a claim in this document's own voice. That is not the process working; it is the
process's own re-run discipline being blind in a way the section below names.

### The three root causes, and how v0.3 closes them

| RC | v0.2 | v0.3 |
|---|---|---|
| **RC-A (dominant) — re-running a FIGURE cannot find a hole in a branch the figure never takes** | `run_arm()`'s `one()` discarded `subprocess.run(...).returncode` and classified a failing TU solely by stderr matching `": error:"`. `clang: fatal error:` (the ` error:` preceded by a *space*, not a colon), a signal death with empty stderr and an ICE were each reported as a clean TU with zero errors. It survived two review rounds because every re-run happened on a tree where everything compiles, so the classifier was only ever exercised on its **clean branch** — and all seven §2a mutants targeted the **guards**, none the classifier | **guard 7**: every compilation carries its rc; `rc == 0` with nothing classified is the only clean state; `rc != 0` with nothing classified and `rc == 0` with something classified are both fatal, printing rc and the stderr head; a listed must-fail probe must exit non-zero *as well as* carry its bound diagnostic; `pick_witness()`'s `-MM` probe checks rc too. And the **mutant class** is fixed, not only the bug: the classifier is now proven by **injecting compiler results** into an unmodified script, each row run against the shipped code and against a `cmp`-confirmed copy with guard 7 reverted. §2a |
| **RC-B — a correct check given an incorrect completeness inference, in the paragraph that concedes the gap** | seam 2's class-scoped `public:`-band check is right, calibrated and mutation-specified — but v0.2 called it *"the completeness half seam 1 cannot give"* two lines above its own *"a new public mutator can then arrive only by being added to that one band — reviewable and enumerable."* A `void clear();`, a mutable-return accessor or a public data member inside the existing band leaves the count at **one** and passes all four arms | **the claim is WITHDRAWN, not respelled.** Seam 2 is retitled *"a LAYOUT guard, not a completeness proof"*; §7's *"covers the same class of regression at a fraction of the cost"* is deleted and replaced by the honest statement that **no automated completeness check over `table_view`'s public surface exists, by decision**; seam 1's *"replaced by"* becomes *"declined, not substituted"*. The `awk`, the exact count and all four arms are unchanged. The residue is carried as an explicit Gate B obligation in the decision record. **No new check was built** |
| **RC-C — the census instruments were paired against the WRONG COMPLEMENT** | §5d's *"the two instruments' blind spots are named by each other"* set-differenced the compile sweep against the **mutator-name grep**, which shares the sweep's `.cpp`-and-call-site axis. Nothing was paired against the **scoper's declaration pattern** — the input that actually drives the rewrite — so an entire declaration spelling (`auto`) was invisible to the census, to the exception list and to the out-of-tree matrix at once | the scoper reads `auto` declarations, and the missing pairing is built as the **attribution guard**: every mutator call in a scanned file must fall inside a declaration the scoper found, unattributed receivers are printed, and the run **exits non-zero** unless each is named on `--known-unscoped`. §5d's heading is corrected and the two pairings are stated separately with what each can and cannot see |

### Per-finding disposition

| # | source | sev | finding | resolution in v0.3 |
|---|---|---|---|---|
| 24 | Codex R2-1 / Opus 456-R2-1 | **P1** | the sweep's classifier fails open: a `fatal error:`, a crash (`rc=-11`) or an ICE (`rc=254`) each report a clean TU with zero errors, contradicting `Fatal`'s *"nothing fails toward clean"*, §2a's *"each guard exits non-zero before printing any figure"* and the Net-effect claim | **Applied, wider than Codex scoped it.** RC-A above. **`pick_witness()`'s `-MM` probe is fixed too** — the adversarial review found that one and Codex did not; a failing dependency probe read as *"this TU does not include `table_view.hpp`"*, moving the witness silently and raising the **wrong** fatal if every probe failed. The **per-arm cost** the adversary separated is now stated in §2a: in the control arm guard 4's *"any unlisted control failure is fatal"* rule was **unreachable**, not weak; in the sealed arm the bill was **undercounted** and `--emit-set` omitted the TU |
| 25 | Codex R2-2 / Opus 456-R2-2 | **P1** | the status block claimed *"Gate A round 1 converged"* against the document's own appendix recording `1 P1 / 4 P2` | **Applied.** The status block now lists **per-round tallies with no convergence claim**, in `.specify/426-428-length-data-pairs.md`'s shape; `.specify/215-dictionary-view.md` states convergence only where the tallies carry it. ⚠️ **The wording came from the `/gate-a-ph2` command template and was passed into the round-1 rewriter brief verbatim — it is not a drafter error.** Fixing the document does not stop the template reintroducing it: **that fix is outside this rewrite's scope (doc + the two `tools/` scripts) and is recorded here as an item the orchestrator owns** |
| 26 | Codex R2-3 / Opus 456-R2-3 | **P2** | seam 2's band check is correct as a layout guard but is not the completeness half it claims to be | **Applied by WITHDRAWAL, per the adversarial review's prescription over Codex's first option.** RC-B above. Round 1 already judged the AST gate disproportionate and round 2 re-affirmed it, so **the AST gate was not built**; an exact public-surface inventory built to rescue a sentence is the PR #325 treadmill |
| 27 | Opus N-P2-1 | **P2** | `table_view_mutation_scope.py` cannot see an `auto`-declared view, so the site table §5d makes the **sole** driver of the migration is incomplete — by a class the document never names | **Applied in the instrument and in the prose, and every figure it moved was re-derived.** RC-C above. §5d gains the **seventh** migration class (the factory's return type becomes `table_view_builder`; each caller gains `.build()`), the out-of-tree matrix gains the matching row, and the *"blind spots are named by each other"* heading is corrected. ⚠️ **No site count is written into the prose** — `--emit-sites` prints it |
| 28 | Opus N-P3-1 | **P3** | the MSVC residual is correctly narrowed and discharged fail-closed, but the branch taken *if it fails* leaves no shippable tree — *"record it, not restore the promise"* does not unbreak a red Tier-2 build | **Applied.** §3.2 now states the branch: the `static_assert` is **removed** and the fact filed as a limitation naming the deciding member; the promise is **not** restored. The review's fail-closed finding is also recorded — `tests/dictionary/CMakeLists.txt` has no `WIN32`/`MSVC` gating and `tier2.yml` builds the MSVC presets with a bare `cmake --build --preset` and no `--target` narrowing, both re-verified here |
| 29 | Opus N-P3-2 | **P3** | seam 1 sketches sixteen negatives and **one** positive, while its own argument is per-concept | **Applied.** Seam 1 states *"sixteen negatives on `table_view` and sixteen matching positives on `table_view_builder`, in the same TU; an unpaired negative is a probe with no proof it is not blind"*, and both comment conventions in the sketch say so |
| 30 | Opus N-P3-3 | **P3** | seam 5's post-migration validator has no witness that `FIXPP_BUILD_FUZZ=ON` took effect — a build configured without it builds fewer targets and goes green | **Applied.** Seam 5 requires the migrated build's own `compile_commands.json` `/tests/fuzz/` TU count to be stated and **non-zero** before the green build is evidence, and records that the pre-implementation half is already guarded (guard 3 raises on a filter selecting nothing; **0 fuzz TUs configured today**, re-verified). Named as an obligation Gate B **discharges**, not inherits |
| 31 | *this rewrite* | — | v0.2's `--naive-decl-count` printed *"the regex OVER-matches when n > total"*. Once the scoper reads `auto` the two sets are **not nested in either direction**, so the count comparison stops testing the inference it was written for | **Applied by deleting the inference**, in the tool and in §5d, not by re-pointing it. Both counts are still printed as two raw facts (`124` and `126`) with nothing concluded from their difference. ⚠️ This is finding #8's deletion holding: the comparison was already removed as a migration instruction in v0.2, and what is deleted here is the last sentence still drawing a conclusion from it |
| 32 | *this rewrite* | — | a `--known-unscoped` entry that stops naming an existing receiver would silently absorb the next receiver of that name in that file — an exemption outliving its subject | **Applied.** A stale allow-list entry exits `3` with its own message, separately from the `4` an unattributed receiver exits with. Both were executed |
| 33 | *this rewrite* | — | §2a's seven guard mutants were run against **v0.2's** script, and guard 7 edits `run_arm()` and `pick_witness()`, which five of them pass through | **Applied by re-running all seven against the edited script**, plus a **negative control** (the unmutated script, same 195-TU filter, `rc=0`) the v0.2 table lacked — seven mutants that all raise are also consistent with a script that always raises |
| 34 | *this rewrite* | — | v0.2 wrote *"the 32-TU set written by `--emit-set` is byte-identical to the first run's"*; v0.3 did not keep the earlier files and cannot byte-compare across revisions | **Applied by narrowing the claim to what was done**: agreement is between v0.3's `--emit-set` file and the printed TU list, not between two files |
| 35 | *this rewrite, post-draft review* | — | v0.3's first draft wrote `--emit-sites` **before** running the attribution guard, so an unattributed receiver produced a complete-**looking** site table alongside a non-zero exit. A file the document calls *"the sole driver of the migration"* is a file someone opens without checking `$?` — the exact failure mode #18's `--emit-set` withholding was added to the sweep to prevent, reintroduced in the sibling tool | **Applied by reordering.** The guard runs first and an unattributed receiver means **no site table is written at all**, with the refusal said in the message. Both arms executed: without the allow-list, `rc=4` and the file does not exist; with it, `rc=0` and 126 rows |
| 36 | *this rewrite, post-draft review* | — | **`pick_witness()`'s new rc check had no mutant.** Every real run passed through it with `rc == 0`, and the classifier-mutant harness stubs `assert_shadow` out, so `pick_witness` is never reached there — a v0.3 guard with no evidence it can fire, under a header stating *"each such guard was itself proven able to fire"* | **Applied as a PAIR**, because the defect it prevents is a loud *misdiagnosis*, not a silent zero: a bogus flag injected into the `-MM` command line yields *"DEPENDENCY PROBE FAILED … (rc=1)"* with the shipped check, and *"this sweep is vacuous"* with the check removed. Both raise, so one arm alone would have cleared v0.2 |
| 37 | *this rewrite, post-draft review* | — | the guard's own prose said *"every mutator call"*, but `RECEIVER` is line-shaped: a **wrapped chain's continuation line** carries no receiver token and is in neither column. Measured **499** continuations against **210** head receivers — the dominant spelling, not an edge case | **Applied by narrowing the claim and printing the number.** The population is stated as **head receivers**, the tool prints the continuation count as a third figure, and the reason it is a scope statement rather than a residue is given (renaming the head migrates the chain). The one shape neither covers — a chain headed by an **expression** rather than a named variable — is named, with the compile sweep as what finds it. **Nothing built** |

### Disagreements recorded, with reasoning

1. **Codex R2-1's fix scope — WIDENED, following the adversarial review.** Codex prescribed the
   return code in `run_arm()`. `pick_witness()` has the same defect and Codex did not name it, so
   fixing only the classifier would have left a probe that misdiagnoses loudly — *"this sweep is
   vacuous"* when the truth is *"the dependency probe is broken"*. Both are fixed.
2. **Codex R2-3's first option (an exact class-scoped public-surface inventory) — DECLINED**, taking
   its second option, which the adversarial review judged the correct one. Building an inventory to
   rescue a sentence replaces a claim with a new claim, which the repo's own PR #325 rule says
   reproduces the defect. Round 1 and round 2 both judged the AST-level gate disproportionate for one
   type in one header.
3. **The adversarial review's *"do not write '9 unattributed calls across 3 files' into the
   document'"* — ADOPTED, and extended to the site count of the seventh migration class.** A result
   rots on the next edit to any of those files; the instrument prints it, the document states the
   condition. The one place a figure from the guard does appear is beside the command that produced
   it, which is this document's stated convention for every executed figure.
4. **The adversarial review's *"fix the `/gate-a-ph2` template as well"* — AGREED but NOT DONE HERE.**
   This rewrite's scope is this document and the two `tools/` scripts; editing a command template
   would put a fourth file in the change. Recorded as an orchestrator item, because round 3 will
   otherwise reintroduce the wording through the same channel.

### Claims DELETED rather than restated

- seam 2's **"the completeness half seam 1 cannot give"** (#26) — the check is kept, the inference is
  gone. Retitled *"a layout guard, not a completeness proof"*;
- §7's **"covers the same class of regression at a fraction of the cost"** (#26) — replaced by the
  statement that **no** automated completeness check exists, by decision;
- seam 1's **"the prescription is replaced by the structural check in seam 2"** (#26) — *declined*,
  not substituted;
- §5d's heading **"the two instruments' blind spots are named by each other"** (#27) — it was false
  as executed; the two pairings are now named separately with what each cannot see;
- the **naive-vs-scoped OVER-MATCH inference** (#31) — deleted from the tool's output and from §5d.
  The two counts remain, with nothing concluded from their difference;
- v0.2's **"byte-identical to the first run's"** `--emit-set` claim (#34) — narrowed to what was
  actually compared.

⚠️ **Six deletions and one withdrawal, against two additions that are both CHECKS** (guard 7 and the
attribution guard) and one that is a **named class** (the seventh migration shape). Nothing here
replaces a falsified claim with a stronger claim of the same kind.

### Net effect

The design is **unchanged for the second consecutive round**: S1 plus deleted assignment,
`build() &&`, the scalar readbacks, the `peek()` refusal, the `215` §4 non-reopening, RC-1's dropped
`noexcept` and §1a's narrowing all survived a second adversarial pass with no finding reaching them.

What changed, stated as conditions rather than as a superlative — ⚠️ **deliberately, because v0.2's
net effect claimed its instruments *"cannot report a plausible zero"* and round 2 falsified exactly
that sentence; a v0.3 that re-earns it would be the same defect with a fresh date**:

- the sweep's classifier **carries the compiler's return code**, and the three states it can now be
  in are enumerated in §2a guard 7. It was proven against injected `fatal error:`, signal-death and
  ICE results, each paired with a guard-7-reverted copy that reports them clean, and with a real
  clean compile as the negative control;
- the seven pre-existing guard mutants were **re-run against the edited script**, with a negative
  control added;
- `/src/` (66 TUs) and `/tests/` (574 TUs) were **both re-executed** through the edited classifier
  and reproduce exactly — including the `/tests/` arm the round-2 adversarial review did not re-run,
  which is where a newly-fatal `rc != 0` would have surfaced;
- the scoper **reads `auto` declarations** and **enforces** that every mutator-call head receiver is
  attributed, exiting non-zero **and writing no site table** otherwise. The figures that moved
  (`121` → `126` declarations, `118` → `123` pure) moved *because* of that, and the interleave count,
  the zero-report count and the three named interleaved scopes are unchanged;
- both halves of guard 7 and both new refusal paths of the attribution guard were **executed**:
  the `-MM` probe mutant as a pair (shipped names the broken probe, reverted blames the filter), the
  no-allow-list arm (`rc=4`, no file written), and the stale-allow-list arm (`rc=3`);
- seam 2's completeness claim is **withdrawn**, with the residue carried as a Gate B obligation and
  no new check built;
- §5d names the **seventh** migration class and the out-of-tree matrix row for it.

⚠️ **This record is not evidence of completeness either.** It records what round 2 caught and what
this rewrite caught while applying it. The standing caution the adversarial review attached to the
convergence pass applies to both new checks above: *a forced-miss arm cannot catch a spurious hit* —
ask of each what **else** could satisfy the condition it watches. For the attribution guard that
question has an answer, and it is written down beside the guard: attribution is by (variable name,
brace scope), so a call attributed to a same-named declaration in a **sibling** scope still counts as
attributed. Nothing is built for that residue, and it is named rather than left for round 3 to find.

---

## v0.3 → v0.4 (round 3, and Gate B round 1's post-convergence fix queue)

**Round 3.** Codex review (`research/reviews/codex_456_3_table-view-seal_review.md`, `0 P1 / 1 P2`)
and Opus adversarial review (`research/reviews/opus_456_3_table-view-seal_adversarial_review.md`,
judging Codex's one finding and adding its own; post-judging `P1=0 P2=0 P3=2`). **No finding touched
the design.** The adversarial review downgraded Codex's sole finding (456-R3-1: an rc audit over
every compilation behind the sweep's published figures) from P2 to P3 on a measured discriminator —
TU membership in the migration bill is preserved regardless of the finding, and a full rc audit
(`/src/` 132 compilations, `/tests/` 1148 compilations, none outside `{0,1}`) reproduced exactly.
**Gate A CONVERGED**, recorded in `.specify/decisions/456-table-view-seal-gatea.md`
(`gate-a-done`). Four obligations carried forward to Gate B: a public-surface read at seam 2's
class-scoped band, the MSVC `is_nothrow_move_constructible_v` disposition (pre-registered as
NOT MEASURED there), a non-zero fuzz-TU witness for seam 5, and hardening the sweep's classifier
before the migration bill is consumed by it — all named as dischargeable, none blocking convergence.

**Gate B round 1** (Codex review + Opus triage, `research/reviews/codex_456_gateb_r1.md` /
`opus_456_gateb_r1_triage.md`) reviewed the shipped migration against this converged design and found
**zero production-code defects** and **zero design defects**. What it found is **RC-1 from the v0.1 → v0.2
section, recurring a fourth time, in a fourth artifact class**: five prose sites (two shipped
headers, `spec/behaviors-and-limitations.md`'s `B-456-1`/`B-456-2`, and this document's own §5b)
described the seal as an absolute — *"immutable"*, *"no mutable alias … at all"*, *"no mutation
channel at all"* — where the true statement is narrower: the sixteen population methods are
unreachable except through `table_view_builder`, and assignment is deleted, but a non-`const` view
can still be moved from, `const_cast` through a span accessor reaches non-`const` storage, and
`std::optional<table_view>::emplace` substitutes a different object under a live `dict_hooks` bundle
at the same address. All three residual channels were already implied by facts this document itself
states (§5a's move-constructor entry; the `emplace`-not-assignment seating instruction), so the fix
is **prose scoped to its narrowest true form**, not a design change: §5b above is corrected to name
all three channels explicitly, `spec/behaviors-and-limitations.md` correspondingly, and a restored
regression witness (`DictHooksCustomPair.ABundleKeepsItsNullPairCallbackAcrossAReSeatThatAddsThePair`) pins the
`optional::emplace` channel specifically, since it — not move-from — is what reconstructs the premise
of the test `B-456-2` recorded as deleted. Gate B round 1 also confirmed and fixed a real code defect
outside this document's scope: `tools/table_view_mutation_scope.py`'s builder-exemption set was a
bare file-wide name set rather than brace-scoped like the declaration census beside it, so an
unrelated same-named `table_view_builder` anywhere in a file could silently absorb an unattributed
mutator receiver. Fixed to share the declaration census's (variable name, brace scope) machinery,
mutation-proven both before and after. **No design content in this document changed as a result of
Gate B round 1** — every edit is either this appendix section or a narrowing of a claim already
false at the time it was written.
