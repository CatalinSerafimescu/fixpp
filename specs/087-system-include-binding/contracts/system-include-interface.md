# Contract — installed system include interface, per exported target

**Feature**: 087-system-include-binding · **Date**: 2026-08-04 · **Issue**: #234

**This file is the single authority for the 087 mechanism.** Anything in `spec.md`, `plan.md`, `tasks.md`,
`research.md` or `quickstart.md` that contradicts it is stale and must be corrected here first, then swept in
the *same commit*. (086 spent four Gate B rounds discovering that fixing a mechanism without sweeping its
specification merely relocates the defect.)

---

## 1. What this contract binds

For a consumer of the **installed** package that runs `find_package(fixpp REQUIRED)` and one
`target_link_libraries` line and nothing else, the **complete** set of include directories CMake supplies to
that consumer's compilation, each with its system/non-system classification.

| linked target | expected include set (relative to the install prefix) | count |
|---|---|---|
| `fixpp::capi` | `include/capi` *(system)* | **1** |
| `fixpp::service` | `include/service-iface` *(system)*, `include/capi` *(system)* | **2** |

Both sets are **closed** — exact equality, not containment (§3). Measured on Linux/clang and MSVC/Conan;
identical on both (`research.md` R4, R6).

**Toolchain scope of the measurement.** The sets above are measured on Linux/clang and MSVC-under-Conan. The
other matrix toolchains (gcc, libc++) are **not** measured pre-implementation; they are covered by CI
**executing the same gate** — `fixpp::consumer::install-witness` carries the `consumer` label
(`CMakeLists.txt:420-421`). Which `ctest` step actually runs it, **per toolchain**:

| toolchain | matrix that carries it | the `ctest` step(s) that run the witness |
|---|---|---|
| clang (measured) | `tier1.yml:293-299` — `linux-clang-{debug,release,asan,ubsan,tsan}` | `.github/workflows/tier1.yml:513` (`-LE packaging`; excludes `packaging` only, so `consumer` is retained) |
| **gcc** | `tier1.yml:293-299` — `linux-gcc-release` | `.github/workflows/tier1.yml:544` (unfiltered — `:513` is skipped on this lane) |
| **libc++** | `tier3-libcxx.yml:174-178` — `linux-clang-libc++{,-asan,-ubsan,-tsan}` | `.github/workflows/tier3-libcxx.yml:341` and `:349` — **unfiltered** |
| MSVC (measured) | `tier2.yml:176-179` — `windows-msvc-{debug,release,asan}` | `.github/workflows/tier2.yml:363` (`-LE packaging`) and `:389` (unfiltered) |

> **Tier 1 alone does not carry this argument.** Until Gate A round 2 both unmeasured toolchains were
> discharged by one cite — *"all six lanes (`tier1.yml:511-513`)"*. That cite reaches **gcc** and does not
> reach **libc++ at all**: tier 1's matrix (`tier1.yml:293-299`) has no libc++ lane; libc++ is a tier-3 matrix
> and MSVC a tier-2 one. The mapping is stated per toolchain here so the claim is carried by the line that
> decides it. *(Corrected at Gate A round 2.)*

A divergence on any of those steps is therefore a **red**, not a scope-out: the sets are unqualified by
compiler on purpose, and if some toolchain's File API model differs, the first PR touching this path fails
loudly. That is what FR-010a prohibits silence about, and it is satisfied by execution rather than by
pre-measurement.

**That argument holds only once each of those steps carries FR-014's registration-count assertion (§6).**
`ctest -L` exits 0 when it selects nothing, so "CI executes the same gate" is otherwise an assumption about an
invocation that may have run zero witnesses — and the toolchain the old cite missed, libc++, sits on the one
workflow with no count assertion of any kind. This is the substantive reason §6 prescribes the assertion for
**all three** workflows rather than narrowing it to tier 1.

### 1a. What this contract does NOT bind

- **Targets other than the two named.** `fixpp::fixpp` legitimately carries the whole tree plus six
  third-party roots; that is the umbrella's purpose, and 086's `probe_umbrella` already pins its reachability.
- **Compiler and SDK search paths.** These never appear in the observation — on Linux the compiler supplies
  its own, on MSVC they arrive via the `INCLUDE` environment variable from `vcvars64.bat`. Neither is
  CMake-supplied, so neither is in scope. *(Measured, not assumed — R2 and R6.)*
- **Header search ORDER as a behavioural claim.** The expected sets are written in observed order and compared
  as sets; ordering effects on header resolution are not asserted. Recorded as a limitation, not closed.

---

## 2. The instrument

**CMake File API, `codemodel-v2`.** For each target, the reply's `compileGroups[].includes[]` gives
`{path, isSystem}`.

Chosen because it reports **CMake's own model** of the include interface. It therefore needs no
compiler-specific command-line parsing (`-I` / `/I` / `-isystem` / `/external:I`) and no compiler invocation.

**Rejected alternatives**, with the reason each fails:

| alternative | why rejected |
|---|---|
| `$<TARGET_PROPERTY:tgt,SYSTEM_INCLUDE_DIRECTORIES>` via `file(GENERATE)` | **The vacuous form.** No documented *collected* consumer property exists, so it yields empty **by construction rather than by measurement** — this is precisely why 086 declined to write this leg |
| parse `compile_commands.json` | re-introduces per-compiler flag spelling, the exact thing this instrument avoids |
| invoke the compiler with `-v` and diff the search path | measures the *compiler*, not the *package interface*; maximally platform-specific |

### 2a. The query must precede configure

A reply exists only if `.cmake/api/v1/query/codemodel-v2` was present when CMake configured the sub-build.
The driver (`run_consumer_witness.cmake`) performs that configure, so **the driver writes the query file
first**. `tests/consumer/CMakeLists.txt` cannot: it executes *during* the configure it would be requesting a
reply for.

**Consequence, and it is the load-bearing one:** the realistic failure is that **no reply exists at all**, not
that a reply is partially populated. A missing reply MUST be `FATAL_ERROR` naming the file (§3, C-2). Reading
absence as "no includes" would reproduce the vacuity this feature exists to remove.

### 2b. One reply directory holds every target — FR-007a's same-run evidence is a GATE OUTPUT

A single `codemodel-v2` reply directory contains a `target-<name>-*.json` for **every** target in the
sub-build, produced by one configure. FR-007a's obligation — that the service red carries same-run evidence
the C-ABI leg stayed isolated — is therefore discharged by **reading a second target's reply out of the same
reply directory**, not by a second staging run or a parallel install. Stated here so `/speckit-tasks` does not
invent one.

**And it is discharged by the gate, not by the demonstrator.** C-6.2 requires the carrier to invoke the
`capi` leg **before** the `service` leg, and C-6.1 requires `compare` to write its per-leg result file
**before** it terminates. So on §5 row #8 the capi-leg result is produced by the same invocation that later
reds the service leg. FR-007a (`spec.md:192-195`) puts the obligation on what the run *captures*; a manual
follow-up read of a surviving work directory would satisfy the letter and depend on an incidental property —
`run_consumer_witness.cmake:46` wipes the sub-build at the **start** of a run, so a failed run happens to
leave its reply on disk. Nothing pins that, and a future edit that cleaned up on failure would break the
evidence with nothing to notice. *(Made a gate property at Gate A round 2; leg ordering tightened at round 3.)*

---

## 3. Comparison rules

Every failure below MUST emit a **named diagnostic token** — `LEAK`, `DROP`, `RECLASSIFIED`, `MISSING_REPLY`,
`INPUT_ERROR`, `LEG_ERROR` — in addition to a human-readable message. The token is what §5's demonstrations
record; an exit status alone does not identify which branch fired, and a red recorded without its branch does
not discharge the branch it was meant to exercise.

**Each token names exactly one cause class**, and §5's table asserts the **complete token set** each row
produces. That is what makes FR-008's "distinguishable from a genuine violation" checkable rather than
asserted. It is **not** a claim that one comparison emits exactly one token.

> **Corrected at Gate A round 2.** This paragraph previously read *"the six tokens are mutually exclusive: one
> failure emits exactly one, and no two causes share a token"*. Both halves were wrong. *No two causes share a
> token* is falsified by §5 on this same page — rows #1 and #2 both assert `LEAK`, rows #3 and #8 both assert
> `DROP`. *One failure emits exactly one* is narrower than the spec it implements: `spec.md` FR-004 requires
> the diagnostic to name **the direction of the mismatch** — *"unexpected present / expected absent /
> classification differs"* — and one mutation can produce more than one direction at once. §5 row #8 does,
> demonstrably: reverting the service line alone gains `include` and loses `include/service-iface` in the same
> comparison.

`MISSING_REPLY`, `INPUT_ERROR` and `LEG_ERROR` remain **exclusive pre-comparison terminations**: each ends the
leg *before* C-1 runs, so no C-1 token can accompany them and no two of them can co-occur. The multi-token
permission below is confined to C-1's three.

- **C-1 Exact set equality, computed in two ORDERED stages.** For each leg, the observed set MUST equal the
  declared set exactly. **The staging is normative**, not an implementation note: without it one cause yields
  contradictory tokens, because `data-model.md` I2 canonicalises both sides to `(path, isSystem)` pairs and a
  flipped `isSystem` therefore lands in *both* residual set differences as well as in the classification test.

  1. **Match by `path`.** Pair each observed entry with the expected entry carrying the same prefix-relative
     `path`. A matched pair whose `isSystem` values differ ⇒ **`RECLASSIFIED`** — fail, naming the path and
     both classifications. **Every path-matched pair is removed before stage 2**, whether or not its
     `isSystem` agreed. `path` is the match key; `isSystem` is compared *within* a matched pair and is never
     part of the key.
  2. **Residual differences.** Of what stage 1 did not match: observed-only entries ⇒ **`LEAK`** — fail,
     naming each offending entry; expected-only entries ⇒ **`DROP`** — fail, naming each missing entry.

  A single comparison **MAY** report more than one of `LEAK` / `DROP` / `RECLASSIFIED`, and **every non-empty
  mismatch class MUST be named**. Reporting one and suppressing the others would hide half of a mixed
  mismatch — precisely what §5 row #8 produces, and precisely what FR-004's "direction of the mismatch"
  forbids. There is no precedence rule between the three because none is needed: the staging is what makes
  each row's token set determinate.

  Containment is explicitly insufficient: it cannot detect a DROP, which is half of what 086's C-3 claims.
- **C-2 Absence is fatal — and is distinct from unparseable input.** Two failures, **two tokens**, because
  FR-008/SC-004 require an unrelated failure to be distinguishable from a genuine violation:
  - a missing reply directory, or a missing `target-<name>-*.json` for a leg ⇒ **`MISSING_REPLY`** — fail
    naming the missing artifact. It MUST NOT be treated as an empty set.
  - a reply that is **present but does not parse**, or parses without the expected `compileGroups` structure
    ⇒ **`INPUT_ERROR`** — fail naming the file and the parse failure, distinguishable from `MISSING_REPLY` and
    from all of C-1's tokens. This token is about the **input data**.
  - the comparator or carrier is **invoked wrongly** — an unknown leg, a duplicate leg, a missing leg, or an
    empty expectation ⇒ **`LEG_ERROR`** (C-6.4). This token is about the **invocation**, and it is separate
    from `INPUT_ERROR` on purpose: "the reply was corrupt" and "the carrier was driven wrong" are different
    defects with different owners, and a demonstration that recorded one token for both would not
    discriminate them — the discrimination this split exists to provide.

  > **A reply that parses to zero include entries is caught by C-1, not by C-2.** Reply *existence and parse*
  > is all C-2 checks; a populated-but-empty observation passes it. What rejects the empty observation is the
  > non-empty expectation (`data-model.md` I3): ∅ ≠ a non-empty declared set, so it fails by arithmetic as a
  > **DROP**. The two guards are easy to conflate and are stated separately here on purpose.
  >
  > **That arithmetic holds only while the expectation *reaching* `compare` is non-empty**, which is why the
  > empty-expectation cause above is a `LEG_ERROR` and not a documentation note: ∅ compared against ∅ is green
  > having asserted nothing. C-6.4 defines the guard that makes I3 a **runtime** property rather than a
  > property of the literal declared in the tree.
- **C-3 Prefix-relative.** Both sides are compared with the install prefix stripped. The File API emits
  forward slashes on **both** platforms, so only the expected side needs constructing with `/`. An observed
  entry outside that prefix remains in its canonical absolute form and therefore cannot match the closed
  prefix-relative expectation; it is classified as **`LEAK`**.
- **C-4 The expectation is a literal with an origin.** Declared in the tree with a rationale per member.
  Nothing may derive it from the observation it checks — such a comparison is satisfied by whatever the run
  produced, the same no-op shape as a `file(GENERATE)` nothing reads.

  > **C-4 is a review-time invariant only, and so is `data-model.md` I4.** No demonstration in §5 and no
  > mechanised check would catch a future edit that reintroduced a computed expectation; US3's Independent
  > Test (`spec.md` §US3) is a human inspection. This is accepted, and it is recorded here so I4 is not read
  > as enforced by the gate.
- **C-5 The reply is located by glob.** `target-<name>-*.json`; reply names carry a content hash and change
  between configures.
- **C-6 The comparator is a standalone script, carried by a uniquely-named 087 target required by name.**

  The 087 comparison MUST NOT be an inline block inside `run_consumer_witness.cmake`. 086 established that
  *"the gate can be removed without anything noticing is the same defect class as the gate cannot fail"*
  (`tests/consumer/run_consumer_witness.cmake:99-106`), and an inline block reproduces exactly that: deleting
  it leaves every named target buildable and the witness green. Therefore:

  1. **The script, with two documented modes.** Reply-location, parse and compare are factored into a
     standalone `tests/consumer/compare_system_includes.cmake`, invoked as `cmake -P` in one of two modes:

     | mode | arguments | what it does |
     |---|---|---|
     | **compare** | `(reply-dir, leg, install-prefix, expectation, result-file)` | **validates its arguments first** — an unknown `leg` or an **empty `expectation`** ⇒ `LEG_ERROR`, before any reply is located (C-6.4) — then locates and parses that leg's reply, normalises observed paths relative to `install-prefix` per C-3, compares per C-1, writes one per-leg result file naming that `leg`, and emits the **complete** token set C-1/C-2 produce for that leg |
     | **leg-set** | `(result-file list)` | the C-6.4 assertion over already-collected per-leg results; emits `LEG_ERROR` or nothing |

     `compare` MUST write its per-leg result file **before** it terminates, including on a red comparison.
     Otherwise a `message(FATAL_ERROR)`-first implementation can emit a token and leave no per-leg result
     behind — the exact anti-vacuity hole C-6.2 exists to prevent.

     **Where the result files live.** The `result-file` path is the carrier's to choose, but the path **the
     carrier passes** MUST lie
     **under the sub-build's binary directory** — `CMAKE_BINARY_DIR` as `tests/consumer/CMakeLists.txt` sees
     it, which is the `-B` directory the driver configures (`run_consumer_witness.cmake:80`, `_sub_build` at
     `:34`) and therefore part of the tree `file(REMOVE_RECURSE "${_stage}" "${_sub_build}")` wipes at the
     **start** of every run (`:46`). This is what makes C-6.4's *"exactly two"* an assertion about **this**
     run: written anywhere the wipe does not reach, two files left behind by a previous run would satisfy the
     count. *(Location pinned at Gate A instance 2 round 2 — it was previously unstated, so the property that
     closed the stale-result read was incidental rather than required.)*

     **Both modes live in the one script on purpose.** It keeps C-6.3's guarantee intact without a second
     file — deleting `compare_system_includes.cmake` still fails the carrier's own command, and the leg-set
     assertion is covered by that same deletion — and it adds no second entry to the out-of-tree file
     accounting the Constitution Check must enumerate. *(Mode split added at Gate A round 2; see C-6.4 for
     why the leg-set assertion must be separately invocable at all.)*
  2. **The carrier target.** A **new, uniquely named** target — `probe_system_include_contract`, declared in
     `tests/consumer/CMakeLists.txt` — whose build command invokes **compare** mode once per leg and then
     **leg-set** mode over the collected result files. It is a *new* target, not one of 086's; reusing
     `probe_usage_requirements` (which 086's own follow-up note at
     `specs/086-capi-include-isolation/contracts/include-interface.md:143-149` suggests) would make the 087
     comparison deletable without any name disappearing.

     **The carrier MUST invoke the `capi` leg before the `service` leg.** The build command may fail-fast on
     the first red compare; it is **not** required to continue past a red `capi` leg. The ordering is what
     makes FR-007a's same-run evidence a property of the **gate's own output** rather than of the
     demonstrator's follow-up read: on §5 row #8 the capi leg runs first and writes its result — still exactly
     `include/capi` — before the service leg reds. Without this the carrier could evaluate `service` first,
     fail, and produce no capi-leg result at all; what makes the demonstration work today is only that
     `run_consumer_witness.cmake:46`'s `file(REMOVE_RECURSE …)` runs at the **start** of a run, leaving the
     reply directory on disk for a human to read afterwards — an incidental property no requirement pins.
     *(General both-legs rule added at Gate A round 2; tightened to capi-before-service at round 3.)*
  3. **Required by name.** `probe_system_include_contract` MUST be added to
     `run_consumer_witness.cmake`'s `_required_targets`, so deleting either the target *or* the script fails
     the build — the script's absence fails the target's own command. Ninja reports the missing target as
     `ninja: error: unknown target '<name>'` — **measured in 086**; the Makefile generators' "No rule to make
     target" phrasing never appears in this project.

     The driver's `_build_rc` `FATAL_ERROR` message (`tests/consumer/run_consumer_witness.cmake:135-142`)
     MUST be updated in the same edit, and it has **two** defects to fix, not one:

     - it says the driver builds *"the **086** witness targets BY NAME"* — a list entry whose own error text
       denies it exists is doc drift shipped at birth;
     - it also asserts a **diagnosis**: that an error here *"means a gate was deleted or renamed, **not that
       the code is broken**"* (`:139-140`). Once `probe_system_include_contract` is in `_required_targets`, an
       ordinary `LEAK` / `DROP` / `RECLASSIFIED` red surfaces through **this same branch** — a genuine
       interface violation, which the message tells the reader it is not. The replacement MUST enumerate both
       dispositions: an *"unknown target"* (Ninja) / *"No rule to make target"* (Makefiles) error means a gate
       was deleted or renamed; a **non-zero exit from `probe_system_include_contract`** means the
       system-include comparison **failed**, and its token and first diagnostic line are in the build output
       printed directly below. *(Second defect added at Gate A round 2 — it ships at birth in the very edit
       this clause mandates.)*
  4. **Exactly two legs, named — and the assertion is SEPARATELY INVOCABLE.** The carrier MUST evaluate
     exactly **two** legs and no others:

     | leg | probe target whose reply is read | expectation |
     |---|---|---|
     | `capi` | `probe_usage_requirements` (links `fixpp::capi`) | §1 row 1 — `include/capi`, 1 entry |
     | `service` | `probe_service_positive` (links `fixpp::service`) | §1 row 2 — `include/service-iface`, `include/capi`, 2 entries |

     Before reporting success the carrier MUST assert it collected **exactly two** per-leg results with
     **distinct, known** leg names, and fail (**`LEG_ERROR`**) on a missing, duplicate or unknown leg.
     The two are necessarily **this run's**: C-6.1 pins the carrier's result-file paths under the sub-build
     tree that `run_consumer_witness.cmake:46` wipes at the start of every run, so the count can never be met
     by files a previous run left behind.
     **Those are three of C-2's four `LEG_ERROR` causes — the leg faults. The fourth, an empty expectation,
     is `compare`'s and is defined immediately below; this paragraph is not the whole cause list.**
     `leg-set` therefore reads the `leg` recorded in each per-leg result file; without that field it cannot
     distinguish "one file twice" from "two distinct legs". Without this assertion a comparator implemented
     for `capi` alone runs through an already-required target and reports green, silently deleting FR-001a and
     half of SC-001.

     **The fourth `LEG_ERROR` cause, and it belongs to `compare` rather than to `leg-set`: an EMPTY
     EXPECTATION ARGUMENT.** `compare` MUST reject an empty `expectation` argument with **`LEG_ERROR`**,
     at **argument-validation time** — before the reply is located and before C-1 runs, so it remains an
     exclusive pre-comparison termination in the sense of §3 (`LEG_ERROR` never co-occurs with a C-1 token).
     C-2's cause list names four invocation faults; the three above are leg faults, this is the fourth, and
     it is stated here because C-2 delegates the whole list to this clause.

     **Why it is a mechanism and not a note.** `data-model.md` I3 — the expectation is non-empty, so an
     observation of ∅ "fails by arithmetic" as a `DROP` — is the feature's headline anti-vacuity property
     (`research.md` R7 guard #1). The arithmetic is a property of the expectation **that actually reaches
     `compare`**, not of the literal declared in `tests/consumer/CMakeLists.txt`: a mis-spelled
     `${FIXPP_087_EXPECTED_*}` reference expands to nothing, and a quoting error in the carrier's command can
     drop the argument entirely. Composed with an observation that parses to zero entries, an unguarded
     `compare` then compares **∅ against ∅ and reports green having asserted nothing** — the exact outcome
     I3 is written to make impossible. `leg-set` cannot cover this: it never sees an expectation. With this
     rejection in place I3 is a **runtime** invariant enforced by the shipped script; without it I3 would be
     a review-time invariant like C-4 / I4 and would have to be labelled one. *(Added at Gate A instance 2
     round 1, where C-2 named this cause and nothing defined or demonstrated it.)*

     **The exactly-two-legs assertion MUST live in the script's `leg-set` mode (C-6.1), not inline in the
     carrier's command declaration**, and the mode MUST be invocable directly as `cmake -P` over an arbitrary
     list of per-leg result files. The reason is demonstrability, and it is the one this feature exists to
     serve: §5 row 6a requires **four** mandatory sub-cases — an unknown leg, **one leg missing**, **a leg
     duplicated**, and an **empty expectation** — and §5 defines the `invocation` class as driving the
     shipped comparator/carrier wrongly *with no tree or reply mutation at all*. With leg enumeration and
     aggregation buried in the carrier's declaration in
     `tests/consumer/CMakeLists.txt`, the missing-leg and duplicated-leg sub-cases could be induced **only by
     editing the tree** — which that class forbids. An implementer facing that would demonstrate the
     unknown-leg case, tick the row, and leave undemonstrated exactly the assertion this clause's own
     rationale is about (the `capi`-only comparator above). That is this feature's dominant failure mode
     reproduced inside its own gate.

     **Which mode induces which sub-case — §5 row 6a is the authority on the induction, and this enumeration
     matches it.** `leg-set`'s separate invocability is what makes the *missing-leg* and *duplicated-leg*
     sub-cases pure invocation: pass one result file, pass the same file twice. The *unknown-leg* and
     *empty-expectation* sub-cases are **`compare`** invocations — both are argument faults `compare` rejects
     before a reply is located, and an unknown leg is therefore rejected before any per-leg result file
     naming one could exist for `leg-set` to read. `leg-set` MUST still validate that the legs it reads are
     known, but that branch is belt-and-braces over a state the shipped path cannot produce, which is why §5
     row 6a demonstrates the `compare` form instead. *(Mode split added at Gate A round 2; this enumeration
     reconciled with row 6a — it previously attributed all sub-cases to `leg-set`, contradicting row 6a on
     this same page — and the empty-expectation sub-case added, at Gate A instance 2 round 1.)*
  5. **The demonstrations invoke the shipped script.** Every §5 row that induces a fault does so against
     `compare_system_includes.cmake` itself, not against a re-implementation — a red produced by a bespoke
     harness proves nothing about the shipped path.

---

## 4. Relationship to 086 C-3

086's C-3 says: *"nothing but the include path and the enumerated, unreachable definition set is withheld"*,
and records that of the four properties `$<LINK_ONLY:>` withholds, its instrument binds three —
`INTERFACE_SYSTEM_INCLUDE_DIRECTORIES` explicitly **not** among them.

**This contract binds the fourth.** On delivery, 086's C-3 scope note is amended to say the property is bound
by 087 rather than open, and the artifacts that record the same scope limit follow.

### 4a. The FR-011 amendment set — established HERE, cited elsewhere

This table is the **single definition** of what FR-011 edits. `spec.md` FR-011 and `plan.md`'s Article VI
Constitution Check row cite this section rather than restating the list; SC-007 is scoped against it. Paths
are **repository-relative** — every reference below is to *086's* artifacts or to shipped source, never to
087's own, and the bare filename spellings that made that ambiguous are not used anywhere in this bundle.

| # | artifact (repository-relative) | clause | what it currently records | amendment |
|---|---|---|---|---|
| 1 | `specs/086-capi-include-isolation/contracts/include-interface.md` | **C-3** (`:122-149`) | the closed enumeration binds three of four; `INTERFACE_SYSTEM_INCLUDE_DIRECTORIES` is *"recorded as a follow-up, not done here"* (`:148`) | the property is **bound by 087** for the two legs named in §1, by the File API instrument of §2 — see the "must not claim" limit below |
| 2 | `specs/086-capi-include-isolation/spec.md` | **FR-009a** (`:373`; sub-clauses `:396`, `:417`) | FR-009a(ii) covers definitions/options/features at the consumer | note that the fourth property is now bound by 087, and that FR-009a's own instrument is unchanged |
| 3 | `specs/086-capi-include-isolation/checklists/abi.md` | **CHK006** (`:16`) | "PASS **with a recorded scope limit (Gate B r3/r4)**… an **OPEN FOLLOW-UP**, not delivered by 086" | the follow-up is delivered by 087; the scope limit is closed, not open |
| 4 | `src/capi/CMakeLists.txt` | `:63-67` | *"System include directories are **NOT** asserted — no collected consumer property exists to assert them against"* | **operational source documentation** — a future reader of the isolation line hits this first, and after 087 it actively contradicts the delivered mechanism. It must say the property *is* asserted, by the File API at the installed consumer, and that the "no collected property" statement is the reason the instrument is the File API rather than a target property |
| 5 | `specs/086-capi-include-isolation/research.md` | `:277-281` (the clause at `:280`) | *"and (NOT asserted — see C-3, narrowed at Gate B r3) `INTERFACE_SYSTEM_INCLUDE_DIRECTORIES`"* | **provenance-preserving only** — this is a historical measurement record and MUST NOT be rewritten to read as though it always said this. The amendment appends, in a dated parenthetical: *not asserted by 086; subsequently bound by 087 (#234)*. The original claim stays legible as what 086 measured and recorded |
| 6 | `tests/consumer/CMakeLists.txt` | the expectation rationale block, `:205-218` (clause at `:213`) | 086's three `FIXPP_086_EXPECTED_*` literals are introduced with: *"`$<LINK_ONLY:>` withholds COMPILE_DEFINITIONS, COMPILE_OPTIONS, COMPILE_FEATURES and `SYSTEM_INCLUDE_DIRECTORIES` along with the include path. **Naming an expected non-empty list instead would have to enumerate what survives, and nothing does.**"* | **operational source documentation, and 087 declares two NON-EMPTY expectations in this same file.** The amendment must scope the closing sentence to **086's instrument** — the three target-property comparisons declared there, for which it stays true — and record that 087's expectations in this file are a *different* observation: system-classified **effective** includes at the installed consumer, read through the File API (§2), not a collected target property. **Provenance-preserving**: 086's sentence is not deleted or reversed; it stops reading as a claim about the file as a whole |
| 7 | `tests/consumer/run_consumer_witness.cmake` | block 3b, `:171-180` (clause at `:173`) | enumerates **four** properties `$<LINK_ONLY:>` withholds, then requires *"an EMPTY effective set for **all three**"* — the three/four seam is left unexplained | name the seam: 086's instrument here (`file(GENERATE)` + this driver's compare) binds **three** of the four by construction, and the fourth — `SYSTEM_INCLUDE_DIRECTORIES` — is bound by 087 through the File API in `probe_system_include_contract`, listed in this same file's `_required_targets`. **Provenance-preserving**: leg 3's own scope is unchanged; only the unexplained gap between "four" and "all three" is closed |

**SC-007's scope, reconciled.** SC-007 is written universally (*"no document still describes the property as an
open scope limit"*). It is discharged against **this table**, which is exhaustive as of 2026-08-04. The
evidence is the grep's own output, re-run and read back rather than merely named — sites listed compactly, the
full command included:

```console
$ grep -rn "INTERFACE_SYSTEM_INCLUDE_DIRECTORIES\|SYSTEM_INCLUDE_DIRECTORIES" specs/ src/ tests/ \
    | grep -v "^specs/087-system-include-binding/" | cut -d: -f1,2
specs/086-capi-include-isolation/research.md:280                     → row 5
specs/086-capi-include-isolation/checklists/abi.md:16                → row 3
specs/086-capi-include-isolation/spec.md:375                         → row 2 (inside FR-009a, :373)
specs/086-capi-include-isolation/spec.md:379                         → row 2 (inside FR-009a, :373)
specs/086-capi-include-isolation/contracts/include-interface.md:126  → row 1 (C-3, :122-149)
specs/086-capi-include-isolation/contracts/include-interface.md:132  → row 1 (C-3, :122-149)
specs/086-capi-include-isolation/contracts/include-interface.md:145  → row 1 (C-3, :122-149)
src/capi/CMakeLists.txt:64                                           → row 4
tests/consumer/CMakeLists.txt:213                                    → row 6
tests/consumer/run_consumer_witness.cmake:173                        → row 7
```

**Ten in-scope hits, seven rows, no residue.** Two notes on the pipeline, because both filters are load-bearing
and neither hides a site:

- `grep -v "^specs/087-system-include-binding/"` drops **087's own bundle**, which this section's scope rule
  above already excludes: those occurrences *describe the binding*, none records the property as an open scope
  limit, so none is an FR-011 amendment target. Their count is deliberately **not** pinned here — this very
  block matches the search term, so quoting a number for them would be a self-referential figure that goes
  stale on the next edit to any 087 artifact. Drop the `grep -v` to see them; the in-scope ten are unaffected.
- `cut -d: -f1,2` shows `file:line` only. Three of the raw matches run to several hundred characters
  (`checklists/abi.md:16` is a single wrapped CHK item). No line is filtered by it.

> **Rows 6 and 7 were added at Gate A round 2, and the reason matters more than the rows.** Until then this
> paragraph asserted exhaustiveness *on the basis of* the grep above while omitting two of its hits — an
> exhaustiveness claim anchored to a command whose own output falsified it, in the section written as the
> single authority SC-007 is discharged against, and which instructs the FR-011 sweep to extend the table only
> *"if the FR-011 pass finds a site not in this table"* — a pass that trusts the claim will not look. Both
> omitted sites are in the two files 087 itself edits, which is the same *"a future reader hits this first"*
> rationale row 4 already accepted. The command is now run and its output pasted so the claim carries the
> evidence that supports it.

Row 5 is included precisely so the universal wording is not quietly narrowed to "current normative
artifacts": historical research records are amended by **appending provenance**, never by rewriting. If the
FR-011 pass finds a site not in this table, the table is extended here first and the sweep follows — the same
authority-first rule §5 and the header of this file state.

What the amendments must **not** claim: 086's §1 reachability matrix still covers system include directories
only at its two named header boundaries. 087 binds the *include set of the two consumer targets*; it does not
turn the reachability matrix into a general system-path assertion. Each amendment states exactly which legs
are bound and by what instrument, and leaves the matrix's scope note intact.

---

## 5. Demonstrated-red obligations

The gate ships only with each of these observed, recorded with exit status, the **asserted diagnostic token**,
and the first diagnostic line. Recording the exit status alone is insufficient: it does not identify which
C-1/C-2 branch fired, so it does not discharge the branch the row exists to exercise.

**Four induction classes**, distinguished because they prove different things. Each row below carries exactly
one, and every class is defined here:

> *(This prose said "**two**" until Gate A round 2, while the table already used four — `invocation` was added
> at round 1 and never swept, and `expectation-side` has been row #1's class since the first draft without
> ever appearing in the prose. The round-2 review counted **three**, having also overlooked `expectation-side`;
> the table is the source and it says four. Both undercounts are recorded rather than quietly replaced,
> because the class list is what tells an implementer which induction means each row permits.)*

- **expectation-side** (#1) — mutate **the declared expectation** and nothing else, before the correct one is
  ever written. This is the vacuity proof: it is the only class that can red the gate without touching the
  package, the reply, or the invocation, and so the only one that proves the gate reads *real data* rather
  than comparing two constants.
- **package-side** (#2, #7, #8) — mutate the tree, re-stage the install, run the witness end to end. These
  prove the *shipped* pipeline reacts to a real interface change.
- **invocation** (#6a) — drive the **shipped** comparator/carrier wrongly, with **no tree and no reply
  mutation at all**: invoke `compare_system_includes.cmake` directly with bad arguments. These prove the gate
  rejects being mis-driven, distinguishably from a corrupt input. C-6.4 requires the leg-set assertion to be a
  separately invocable mode precisely so every #6a sub-case is reachable this way rather than by a tree edit.
- **reply-side** (#3, #4, #5, #6) — `cp -r` a **real** reply directory produced by a real configure, mutate or
  delete the **copy**, then invoke the **shipped** `compare_system_includes.cmake` against the copy
  (C-6.5). This is the only realisable fault-injection seam: `run_consumer_witness.cmake:46` wipes the
  sub-build and reconfigures on every invocation (`file(REMOVE_RECURSE …)`), so the reply is regenerated
  before any persistent tree edit could reach it. **"Remove an entry from the observed side" is not
  achievable by editing the tree** — an implementer who tries will end up editing the *expectation*, which is
  row #1 with the sign flipped and proves nothing new.

| # | cause | class | how induced | asserted diagnostic | expected |
|---|---|---|---|---|---|
| 1 | **vacuity proof** | expectation-side | *Before the `service` leg's correct expectation is ever written*: declare the **`service`** expectation as `include/service-iface` **only**, omitting `include/capi` — a **strict, non-empty subset** of the measured set — **with the `capi` expectation already declared at its measured value** (see the box below; this is required, not incidental), run against the real reply | **`LEAK`** | red naming `include/capi` as observed-but-unexpected. Proves the gate reads real data. The direction is `LEAK` **by construction** so it cannot be confused with #3's `DROP`; the service leg is used because the capi leg's single entry admits no non-empty strict subset |
| 2 | **leak — package-side** | package-side | the exact diff below against `src/capi/CMakeLists.txt`, then re-stage and re-run | **`LEAK`** | red naming each entry the reverted interface adds. **The expectation is qualitative:** the observed set gains the umbrella include root and the third-party roots; **the exact count is recorded at demonstration time.** See the box below — no figure is stated here because none has been measured on a reverted `fixpp::capi` |
| 3 | **drop** | reply-side | copy a real reply directory; in the copy's `target-probe_service_positive-*.json`, delete one entry from `compileGroups[].includes[]`; invoke the shipped script against the copy, passing the same install prefix the original configure used | **`DROP`** | red naming the deleted entry as expected-but-absent — reachable **only** because C-1 asserts equality, not containment |
| 4 | **reclassified** | reply-side | copy a real reply directory; in the copy, flip one entry's `isSystem` from `true` to `false`, **leaving both paths identical**; invoke the shipped script against the copy, passing the same install prefix the original configure used | **`RECLASSIFIED`**, and **that token alone** | red naming the path and both classifications, with **no** accompanying `LEAK` or `DROP` — the path matches on both sides, so C-1 stage 1 claims the pair and removes it, leaving stage 2 nothing. **This row is why C-1's staging is normative:** under an unstaged reading of `data-model.md` I2's `(path, isSystem)` canonicalisation the same mutation fires all three tokens at once. This is also the **only** demonstration that exercises FR-003a's classification leg — `isSystem` is uniformly `true` in the passing state (`research.md` R4), so no happy-path run varies it, and a comparator that parsed `path` and discarded `isSystem` would satisfy every other row |
| 5 | **missing reply** | reply-side | copy a real reply directory; delete the per-target `target-<name>-*.json` from the copy — and, as a second sub-case, delete the whole reply directory; invoke the shipped script against each, passing the same install prefix the original configure used | **`MISSING_REPLY`** | red naming the missing artifact — **not** read as "no includes", and distinct from #6's token |
| 6 | **input error / counter-test** | reply-side | copy a real reply directory; truncate the per-target JSON mid-object in the copy so it is **present but unparseable**; invoke the shipped script against the copy, passing the same install prefix the original configure used | **`INPUT_ERROR`** | red naming the file and the parse failure, **distinguishable from `MISSING_REPLY` and from every C-1 token**. This row is FR-008 / SC-004 — an unrelated failure reported distinguishably from a genuine violation |
| 6a | **leg error / counter-test** | invocation | **four mandatory sub-cases, all pure `cmake -P` invocation** (C-6.4): *(i)* **compare** mode with an unknown `leg`; *(ii)* **leg-set** mode over **one** result file — the missing-leg case; *(iii)* **leg-set** mode over the **same result file twice** — the duplicated-leg case; *(iv)* **compare** mode with an **empty `expectation` argument**, every other argument correct. No tree edit and no reply mutation in any of the four | **`LEG_ERROR`** | red naming the offending leg (*(i)*–*(iii)*) or the empty expectation (*(iv)*), **distinguishable from `INPUT_ERROR`** — a corrupt reply and a mis-driven carrier are different defects. Sub-case *(ii)* is not discharged by *(i)*: C-6.4's own rationale is the missing-leg case — *"a comparator implemented for `capi` alone runs through an already-required target and reports green, silently deleting FR-001a and half of SC-001."* Sub-case *(iv)* is what makes `data-model.md` I3 a **runtime** property: it must red **before** the reply is located, so it reds even against a correct reply, and it is the only demonstration of the guard standing between an empty expectation and a green ∅-vs-∅ comparison. Sub-cases *(ii)* and *(iii)* were only reachable by editing the carrier's declaration until the leg-set assertion was made separately invocable at Gate A round 2; *(iv)* was added at Gate A instance 2 round 1 |
| 7 | **carrier deleted** | package-side | delete the **new 087 target** `probe_system_include_contract` from `tests/consumer/CMakeLists.txt`; as a second sub-case delete `tests/consumer/compare_system_includes.cmake` and leave the target | **build failure by name** | with the target gone: `ninja: error: unknown target 'probe_system_include_contract'` (Ninja's phrasing — *not* Make's "No rule to make target"). With only the script gone: the target's own command fails. **Deleting an 086 target would re-prove an 086 obligation, not this one** |
| 8 | **service leg** | package-side | restore the **pre-086** service `$<INSTALL_INTERFACE:>` value in `src/service/CMakeLists.txt` **alone** — the exact diff is in the box below, with its `git show` provenance | **`LEAK` *and* `DROP`** (service leg) — both, per C-1's multi-token rule | red naming **`include` as observed-but-unexpected** (LEAK) **and `include/service-iface` as expected-but-absent** (DROP), in one comparison. `include/capi` matches on both sides and is removed by C-1 stage 1. **Plus the capi leg's own result from the same invocation**, still exactly `include/capi` — emitted because the carrier runs `capi` first and `compare` writes that result before the later service red terminates the build (§2b, C-6.2). Reverting capi reds both legs and proves nothing about service (086 FR-011e) |
| — | **controls** | — | all restored | — | green, both legs, exactly two leg results (C-6.4) |

> ### Demonstration #1 — the `capi` expectation MUST already be correct, or the row cannot emit its token
>
> *(Added 2026-08-05 at `/speckit-tasks`. Row 1 predates both the round-3 leg-ordering fix and the instance-2
> empty-expectation guard, and its opening phrase — "before **the** correct expectation is ever written" —
> reads naturally as "before **either** expectation is written". Composed with the two clauses added after it,
> that reading makes the row unable to produce `LEAK`. `plan.md` step 3's plural "correct the **expectations**"
> invited the same reading.)*
>
> C-6.2 makes the carrier invoke `capi` **before** `service`, and the carrier's `COMMAND` list short-circuits
> on the first non-zero exit (the i1r3 finding). So whatever the `capi` leg does happens first and decides
> whether the `service` comparison is reached at all:
>
> | `capi` expectation at demonstration #1 | what actually happens |
> |---|---|
> | **absent / empty** | `compare` rejects it at argument-validation time ⇒ **`LEG_ERROR`** (C-6.4), before any reply is located. The build stops. The service leg never runs, and the row records the **wrong token** |
> | **present but wrong** | the `capi` leg reds first. The build stops. Same outcome |
> | **declared at its measured value** (`include/capi`, `isSystem=true`) | the `capi` leg passes and writes its result; the `service` leg then reds with **`LEAK`** naming `include/capi` — the token this row asserts |
>
> **Only the third configuration realises the row.** The `capi` expectation is therefore declared correctly
> from the start; "before the correct expectation is ever written" scopes to the **`service`** leg alone. This
> costs the row nothing: the vacuity proof is a property of the *service* comparison reading real data, and a
> correct `capi` leg is not a green the gate has been credited with — it is the control that lets the service
> red be reached.

> ### Demonstration #2 — the exact mutation, and why no count is stated
>
> **"Flip `PRIVATE` → `PUBLIC`" is not a one-line change**, and 086 already paid a `/speckit-implement` round
> discovering that on this same file (`specs/086-capi-include-isolation/contracts/include-interface.md:85-101`
> — *"The bundle carried 'flip the keyword' as a one-line change through Gate A. It is not one."*). The
> mutation is written out here so the demonstration is executable and so the bundle cannot repeat it.
>
> The shipped state is **two separate commands**. `src/capi/CMakeLists.txt:97-99`:
>
> ```cmake
> target_link_libraries(fixpp_capi
>   PRIVATE fixpp_capi_objects
>   PUBLIC  "$<BUILD_INTERFACE:fixpp_capi_objects>")
> ```
>
> and `src/capi/CMakeLists.txt:112-115`:
>
> ```cmake
> target_include_directories(fixpp_capi PUBLIC
>   "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>"
>   "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/capi>"
> )
> ```
>
> **What reverts** (`:97-99` only) — the whole two-keyword arrangement collapses back to the pre-086 single
> public edge:
>
> ```diff
> -target_link_libraries(fixpp_capi
> -  PRIVATE fixpp_capi_objects
> -  PUBLIC  "$<BUILD_INTERFACE:fixpp_capi_objects>")
> +target_link_libraries(fixpp_capi PUBLIC fixpp_capi_objects)
> ```
>
> Merely flipping the `PRIVATE` keyword is **not** this: it would leave a redundant second
> `PUBLIC "$<BUILD_INTERFACE:fixpp_capi_objects>"` entry alongside the now-public first one.
>
> **What stays** — `:112-115` is **not** touched. It is a different command, so the demonstration leaves
> `$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/capi>` in place.
>
> **What installed interface this represents.** Not the pre-086 state. It is *086's isolated root plus the
> full transitive usage-requirement set of `fixpp_capi_objects`* — the regression #218 describes (a narrowing
> that silently stops narrowing), which is exactly what this gate must catch, and the reason the demonstration
> is written this way rather than as a full revert. Reverting `:112-115` as well would produce the true
> pre-086 state; that is **not** what this row induces, and if a task wants it, it is a separate row.
>
> **Why no count.** It follows from the above that the reverted set **retains `include/capi`** — and
> `research.md` R3's 7-entry `probe_umbrella` set contains `<prefix>/include` and **not**
> `<prefix>/include/capi`. So the reverted `fixpp::capi` set is provably **not** the umbrella's 7-entry set;
> it is `{include/capi} ∪ closure(fixpp_capi_objects)`, and nothing measures that closure to equal
> `closure(fixpp::fixpp)` — it is a subset of it. Its cardinality has **never been measured**: every figure in
> `research.md` and in the orchestrator's Gate A round-1 measurement record comes from the *isolated* tree. No
> replacement figure is supplied here on purpose — substituting a second inferred number would reproduce the
> defect this box exists to remove. The expectation is qualitative; the count is recorded when the revert is
> actually run.

> ### Demonstration #8 — the exact mutation, its provenance, and why the token set is LEAK **and** DROP
>
> *(Added at Gate A round 2. Row #2 was written out because "flip the keyword" had already cost 086 a
> `/speckit-implement` round; the same rule was not swept to its sibling row #8 — and here the ambiguity
> decides the **asserted token**, so it is not cosmetic.)*
>
> **"Revert" was ambiguous** between *deleting* the `$<INSTALL_INTERFACE:>` entry — which would yield observed
> `{include/capi}`, a pure `DROP` — and *restoring the pre-086 value*, which yields a mixed mismatch. The
> bundle recorded neither the choice nor what the pre-086 value was. It is recoverable only from the 086
> commit:
>
> ```console
> $ git show cb397284 -- src/service/CMakeLists.txt
> cb397284 086 T020-T033: service leg isolated; all three gates DEMONSTRATED RED
>   @@ -7,9 +7,23 @@
>    target_include_directories(fixpp_service INTERFACE
>      "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>"
>   -  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
>   +  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/service-iface>"
>    )
> ```
>
> **This row induces the second reading — restore the pre-086 value** (current state at
> `src/service/CMakeLists.txt:24-27`):
>
> ```diff
>  target_include_directories(fixpp_service INTERFACE
>    "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>"
> -  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/service-iface>"
> +  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
>  )
> ```
>
> It is the right reading because it is the **real regression** — the narrowing 086 delivered, undone — rather
> than a synthetic deletion of an install root no state of this repository ever had. `src/capi/CMakeLists.txt`
> is **not** touched: reverting capi reds both legs (FR-007a).
>
> **Resulting observed set** (`CMAKE_INSTALL_INCLUDEDIR` is `include` under GNUInstallDirs): `include` from
> the service line itself, `include/capi` inherited through `fixpp::service`'s link to `fixpp::capi` ⇒
> `{include, include/capi}`. Expected (§1 row 2): `{include/service-iface, include/capi}`.
>
> **Resulting token set**, per C-1's stages: stage 1 matches `include/capi` on both sides with identical
> `isSystem` and removes it — no `RECLASSIFIED`. Stage 2 residuals: observed-only `{include}` ⇒ **`LEAK`**;
> expected-only `{include/service-iface}` ⇒ **`DROP`**. **Both, from one mutation**, which is the concrete
> case that falsified the old one-token-per-failure rule (§3).
>
> **Same-run capi evidence**: the carrier's `capi`-leg result from this same invocation, still exactly
> `include/capi` — present because the carrier runs `capi` before `service` and `compare` writes its result
> before terminating (C-6.2, §2b) — recorded alongside the service red.

---

## 6. The last GATE-CLOSABLE vacuity path — the `consumer` label's registration count

§5's rows all assert things *inside* a gate that runs. One path remains by which the gate reports nothing
having asserted nothing that **the gate itself could close** — and it is the only one closable in CI. It sits
**outside** every §5 row: the witness is registered only inside
`if(FIXPP_BUILD_CODEGEN_TOOL)` nested in `if(FIXPP_BUILD_TESTS)` (`CMakeLists.txt:401`), and
`ctest -L`/`-R` **exits 0 when the filter matches nothing**. Every CI step that runs the witness does so with
**no count assertion for the `consumer` label** — `tier1.yml:513`/`:544`, `tier2.yml:363`/`:389`,
`tier3-libcxx.yml:341`/`:349`. If `FIXPP_BUILD_CODEGEN_TOOL` ever goes OFF on a lane, the whole 087 gate stops
existing and the lane reports green.

`quickstart.md` §0 names this hazard, but a quickstart banner binds a **human**; it does not bind CI.

> **"Last" means last *closable by the gate*, not last in existence — the residual set is REVIEW-enforced and
> is enumerated, not implied.** Two other paths sit outside every §5 row and neither is closable from inside
> the gate: the carrier's own `leg-set` invocation is not self-policing — deleting that one line from
> `tests/consumer/CMakeLists.txt` leaves the build green in every state the gate can reach (`research.md` R7's
> *"one last turtle"* box) — and the computed-expectation prohibition is a human inspection (C-4 above,
> `data-model.md` I4). Both are disclosed as review-time invariants where they are stated, and neither is
> misdescribed as mechanised. *(This paragraph read "**One** path remains… **the last** vacuity path" until
> Gate A instance 2 round 1. §6's substance and prescription are unchanged; only the singularity framing was
> false, and this project has twice found real defects behind exhaustiveness claims.)*

### 6a. Scope — all three workflows, because the hazard is per lane and uniform

The assertion is required on **`tier1.yml`, `tier2.yml` and `tier3-libcxx.yml`**, not on tier 1 alone.
Verified, and the verification is why:

- `CMakeLists.txt:421` is the **only** `LABELS consumer` in the tree
  (`grep -rn "LABELS consumer" --include=CMakeLists.txt .` → one hit), and `CMakeLists.txt:290` declares
  `FIXPP_BUILD_CODEGEN_TOOL` default **`ON`, overridden nowhere** in any preset, workflow or cache — so the
  hazard is latent on **every** lane of **every** tier equally.
- The witness executes on all of them: tier 1's `-LE packaging` excludes one *other* label; tier 3 runs
  `ctest --preset` **unfiltered**; tier 2 mirrors tier 1. See §1's per-toolchain table for the step anchors.
- `spec.md` FR-014 (*"CI MUST assert… fail the lane"*) and SC-008 (*"A lane on which … fails to register
  **fails**"*) are **unqualified**. Prescribing tier 1 alone would be a strictly narrower implementation than
  the spec requires, on the feature whose declared identity is closing vacuity paths.
- Narrowing instead is not available: §1's FR-010a argument covers **libc++** by CI *executing the same gate*,
  and libc++ lives on `tier3-libcxx.yml` — the one workflow with no count assertion of any kind today. For
  libc++, "covered by execution" would rest on neither a cite nor a count. *(This scope was tier-1-only until
  Gate A round 2.)*

### 6b. The shape of the assertion

Modelled on the one `tier1.yml:528-540` and `tier2.yml:371-384` already carry for the sibling `packaging`
label — *"⚠️ ASSERT THE COUNT, NOT JUST THE EXIT CODE… so an exit-code-only check reports this lane green
having run zero witnesses"*:

- **one step per workflow**, running `ctest --preset <preset> -L consumer -N`, parsing `Total Tests:`, and
  **exit-1ing** if the number is not the expected one;
- **unconditional — no `if:` guard, one step per workflow rather than one per test step.** In each file the
  two test steps are mutually exclusive by `if:` and jointly cover every lane (tier 1: `:513` runs on all but
  `linux-gcc-release`, `:544` only on it; tier 2: `:363` on all but `windows-msvc-release`, `:389` only on it;
  tier 3: `:341` on all but `-tsan`, `:349` only on it). An unguarded step therefore covers both without six
  conditional copies;
- **placed after that workflow's `Build` step and before its first test step** — `ctest -N` reads
  `CTestTestfile.cmake`, so it requires a configured build tree. Verified insertion points: `tier1.yml` after
  `:492` (before `:511`), `tier2.yml` after `:323` (before `:360`), `tier3-libcxx.yml` after `:314` (before
  `:339`). Note this is **earlier** than the `packaging` assertion it is modelled on, which sits between the
  two test steps because only one lane needs it;
- `tier2.yml`'s step MUST use `shell: bash` — that file's `ctest` steps are `shell: cmd`, and its own
  `packaging` assertion at `:371-384` already switches to bash for exactly this. Model tier 2 on **its own**
  step, not on tier 1's;
- the expected number is **1** as of 2026-08-04, on every lane of all three tiers, from the single-registration
  fact above. It is asserted as an exact equality, not a lower bound; if 087 or a later feature adds a
  `consumer`-labelled registration, this number is updated in all three workflows in the same commit;
- it does **not** add a registered test, so FR-013 and the CHK063 zero-selection posture are unchanged — it
  asserts the count of what is already registered.

> **A single shared script was considered and not prescribed.** Three near-identical twelve-line steps invite
> factoring into one `.github/scripts/…` helper. It is not taken: no `.github/scripts/` directory exists, so
> this would create one; the project's own precedent for this exact assertion is inline duplication
> (`tier1.yml:528-540` and `tier2.yml:371-384` are already two copies, deliberately, because each matches its
> workflow's shell idiom); and a shared script would add a **fourth** file to the out-of-tree set the
> Constitution Check's Article VII §8 row must now enumerate, working against the accounting this bundle just
> had to correct. An implementer who prefers the shared script may take it — but then `plan.md`'s Technical
> Context, Project Structure and VII §8 row are updated in the same commit.

**Why prescribe rather than scope out.** This feature's identity is closing vacuity paths; the assertion is
small, the count is known, and it is the last enumerated path by which this gate can report green having
observed nothing **that a mechanised check can close at all** — the two residuals above are review-enforced
by construction. Scoping it out as inherited from 086/084 CHK063 would be defensible for a feature about
something else. It is not defensible for this one.
