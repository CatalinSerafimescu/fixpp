# Implementation Plan: Live QuickFIX interop fidelity — peer-side typed readback + dictionary-backed conversation

**Branch**: `089-quickfix-interop-conversation` | **Date**: 2026-09-10 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `/specs/089-quickfix-interop-conversation/spec.md`

---

## Summary

Build the fidelity machinery the row-4d closure bar actually requires, and prove it on a narrow
message set. Both QuickFIX counterparties gain **typed, dictionary-backed parsing** and a **structured
per-field readback channel**; the interop sessions get a **real FIX 4.4 dictionary** on both sides
instead of a FIX 4.2 single-Heartbeat sentinel; results gain a **two-level cell/witness structure** with
an exact-set completeness gate at each level and run **evidence** binding a `pass` to a real run; and the
whole scripted conversation runs across **4 role×flavour × 2 validation arms × 4 build configs = 32 runs**.

The feature spans **two repositories** plus a container image. It closes **zero catalogue rows** by
itself — the narrow set is already `done` — and exists so the breadth follow-on has something true to
stand on.

---

## Technical Context

**Language/Version**: C++23 (fixpp + the QuickFIX-cpp counterparty), Java 21 (the QuickFIX-J
counterparty), Python 3.12 (the harness shim `run_interop_cell.py` and the schema checks)

**Primary Dependencies**: QuickFIX-cpp **v1.16.0** (pinned in `phase-9-harness/ci/counterparties.Dockerfile`),
QuickFIX-J **3.0.1** (Maven Central, `phase-9-harness/quickfixj/pom.xml:15`), GoogleTest, OpenSSL/TLS

**Storage**: filesystem artifacts only — counterparty transcripts, the new readback records, goldens,
`cell_results.yaml` and the new witness-evidence record. No database.

**Testing**: GoogleTest for the fixpp-side cells; `pytest` for the schema checks; `run_interop_cell.py`
as the process orchestrator. Article VII §3 (TDD) applies: each witness lands RED first.

**Target Platform**: Linux x86_64 / Clang. The live matrix is Linux-only; Tier 2 (MSVC) does not run
interop cells and this feature does not change that.

**Project Type**: C++ library + an out-of-process interop harness spanning two repositories.

**Performance Goals**: none — this is a correctness/evidence feature. The only timing constraint is that
a cell must terminate; see *Constraints*.

**Constraints**:
- **Disk is the binding constraint on this feature.** See *§ Disk preflight* below — it is a gate, not a
  footnote.
- Every cell must have a bounded wall-clock so a wedged conversation fails rather than hangs. Derive the
  bound from the competing timeout (heartbeat interval × N), never from a default.
- Only fixpp is sanitizer-instrumented; the counterparties are unmodified production binaries.

**Scale/Scope**: 5 business message types × 2 directions × 4 role×flavour × 2 validation arms × 4 build
configs. **8 logical cells; 32 runs**; witness count derived from the conversation script and cross-checked
against an independent declarative census (FR-015d), never hand-listed and never derived from one source
alone.

---

## ⛔ Disk preflight — a GATE before every configure/build task

> This section is normative. Every build-bearing task in `tasks.md` must cite it.

### The instrument that fails toward clean

⛔ **THE NUMBERS ARE NOT IN THIS SECTION, DELIBERATELY.** What follows is the **condition**, the
**re-derivation recipe**, and exactly one dated historical observation kept as motivation. ⚠️ This section's
whole subject is a number that reports comfort it cannot keep true, so a current reading pasted here
**re-arms it**: an earlier draft's figures were falsified **within the hour** by a routine scratch-tree
reclaim, and nothing ever re-runs a document. Correcting them would schedule the next falsification.

**The condition — this is what is normative:**

> `df` **inside** WSL and `df` on the **Windows host drive backing the VHD** measure two different
> quantities. The internal reading bounds *total data resident at once*; the host reading bounds *net new
> allocation* — how much the VHD may still grow. The internal reading is routinely the larger and the
> **more comfortable**, and it is the one that will authorise a build the host cannot finance, so that the
> build burns its compile time and dies on `ENOSPC` partway. The gate therefore encodes **two predicates**,
> never one ratio and never one threshold (D-1).

**The recipe — run it, do not read a figure:**

```bash
df -k /                       # internal: what may be RESIDENT
df -k /mnt/e                  # host: what the VHD may still GROW      (WSL only)
du -sh build/*/               # per-tree occupancy, for sequencing and reclaim
df -k /mnt/wsl/fixppbuild     # the evidence root + ccache device — a DIFFERENT device
```

**A dated historical observation, kept as motivation and never as an operand.** ⚠️ The load-bearing half is the *condition* — **motivation, never an operand** — not how many such observations there are; a count is a result nothing re-runs. On **2026-09-10** the
internal reading was ≈83 G while the host reading was ≈17 G, with the ASan build tree at ≈34 G — a tree
that would not fit in the host's remaining space, sitting inside a filesystem reporting four times that
much free. That is the shape of the failure. ⚠️ **Every one of those three figures had moved by later the
same day.** Do not carry them forward; run the recipe.

### The mechanism, so the reclaim procedure is understood rather than cargo-culted

The WSL2 ext4 VHD grows on demand and **does not auto-shrink**. Therefore:

- Deleting files **inside** WSL frees blocks for **reuse within the already-allocated VHD**. That is what
  makes reclaim work.
- It does **not** return space to `E:\`. Returning space to the host needs a VHD compaction, which is
  **out of scope** for this feature.
- ⇒ *"delete a build config to make room"* is a valid claim. *"delete a build config to give E: its space
  back"* is **not**, and no task may assert it.

Two ceilings, and they are different quantities. The VHD is
`/mnt/e/Catalin/Work/WSL/Ubuntu24.04LTS/ext4.vhdx`:

| Ceiling | What it bounds | Reading | Predicate |
|---|---|---|---|
| Free **inside** the VHD | total data resident at once | `df -k /` | `required_internal_free` |
| Free on **the host** (`E:\`) | **net new** allocation — how much the VHD may still grow | `df -k /mnt/e` | `required_host_growth` |

**The gap between them is a reuse pool**, and it is why the two predicates are not interchangeable: the
VHD has materialised more host space than its filesystem currently uses, so some writes land in already
-allocated extents and cost **zero** host growth, while writes beyond that force VHD growth against the
host reading alone. Its size is `materialised_on_host − used_inside − ext4 reserve`.

⚠️ **That pool is a BOUND, not a measurement, and the plan must not spend it — so its size is not recorded
here.** ext4 does not preferentially allocate into already-materialised extents, so the pool may simply not
be realised: an allocator that picks fresh extents converts a "free" write into 1:1 host growth. **Deriving
a reuse figure from the two `df` readings is exactly the inference R-1 exists to replace**, which is also
why writing the figure down at all invites the gate to subtract it (D-7a forbids that).

⚠️ **Which ceiling binds a given build is NOT known a priori and MUST be measured, not assumed.** **R-1**
in `research.md` mandates the experiment: build each configuration (targeted, per below) from clean while
sampling **both** numbers, and derive **both** predicates from what is observed.

⚠️ **Bootstrap, because R-1 cannot run under its own gate.** This section makes the gate normative for
*every build-bearing task*, D-9 makes an unset threshold a hard error, and D-7 requires both values to come
from R-1's measurement — whose experiment is four builds. That is circular. **Resolution: a bootstrap
mode.** `ci/disk-preflight.sh --bootstrap` accepts thresholds carrying the literal label
`estimated, pending R-1` with a date, sourced from the pessimistic model (host cost = build size), and
**prints that label on every line of its output**. It is admissible for the R-1 experiment **and for
nothing else**: any other task invoking it is a violation, and the label makes that visible in the log
rather than inferable. Once R-1 lands, the labelled values are replaced by measured ones and the bootstrap
path is dead.

### Reserve ballast — a one-shot valve

`/mnt/e/_wsl-reserve-1.bin` and `_wsl-reserve-2.bin`, **5 GiB each (10 GiB total)**, pre-allocated on the
host as an emergency valve.

- **Last resort only.** Not part of the normal budget; the preflight must not count it as available.
- **If spent, it must be refilled** as an explicit task step. A spent valve nobody refills is worse than
  no valve, because the next person believes they still have it.

### The gate itself — `ci/disk-preflight.sh`

Follows the established `ci/<name>.sh` + `ci/test-<name>.sh` convention, pinned in the `ci-script-pins`
job (`.github/workflows/tier1.yml:2130`) alongside `test-restore-conan-cache.sh`,
`test-ccache-scripts.sh`, `test-check-wheel-payload.sh`.

Required behaviour:

1. ⚠️ **TWO named predicates, evaluated INDEPENDENTLY, both per configuration** —
   `required_internal_free` against the **build-mount** reading, and `required_host_growth` against the
   **host-mount** reading. The failing predicate is named in the output.

   *This replaces "gate on the minimum of the two readings", and the reason is worth stating because the
   fail-open is not where it looks.* As an abstract predicate `min(a, b) ≥ T` is **stricter** than either
   alone, so it fails toward a false RED — not the dangerous direction. The dangerous direction is that
   **R-1 derives the threshold from the host delta while the gate applies it to the build-mount reading**,
   which bounds *total data resident at once*. Those two quantities differ by **more than an order of
   magnitude** on a sanitizer configuration, so a threshold sized for the host delta authorises a build the
   build-mount ceiling cannot hold: the exact ENOSPC this gate exists to prevent, produced by the gate's
   own arithmetic. **One threshold applied to two ceilings under-checks whichever ceiling is larger.**
   ⚠️ **No figures here.** The single dated illustration this bundle keeps lives in § *Disk preflight*
   above; repeating it here made it look like an operand. Re-derive with `du -sh build/*/`.

1a. ⚠️ **`reclaim-first` is offered ONLY for an internal-space failure.** Deleting inside WSL frees blocks
   for reuse within the VHD and returns nothing to the host (see *The mechanism* above), so prescribing
   reclaim for a **host** failure prescribes a remedy this section proves cannot work — and the readings
   are unchanged afterwards, so the operator loops.
2. **Detect WSL** via `microsoft` in `/proc/version` (verified present) and locate the host mount via
   `/proc/mounts` (`/mnt/e` is a `9p` `drvfs` mount — verified). On a non-WSL host (a CI runner, where
   `/mnt/e` does not exist) the host reading is **not applicable** and the plain `df` governs.
3. ⚠️ **On WSL, an unreadable or missing host mount is a FAILURE, not a pass.** "I could not read the
   binding constraint" must never resolve to "proceed". This is the arm most likely to be written
   backwards.
4. **FAIL LOUDLY and refuse to start the build** when below threshold. It must **not** warn-and-continue
   and must **not** exit 0 on the failing path. *(The repository has a recorded case of a gate that warns,
   says `DIAGNOSTIC ONLY`, and exits 0.)*
5. Print both readings and the derived verdict on **every** invocation, pass or fail — a diagnostic is a
   diff between the two cases, so the passing path must be legible too.

### ★ The build unit is TARGETED, not `all` — this is the single largest lever

**The condition:** `run_interop_cell.py` **never builds anything** — it expects a pre-built tree and runs
one named gtest binary per cell (each cell carries a `binary:` and a `gtest_filter:`). **What gets built is
therefore this plan's choice**, and a full build of a preset produces hundreds of executables of which the
matrix opens **four**.

⇒ **Each configuration builds only the interop driver targets**
(`cmake --build <preset> --target <the interop targets the cells name>`), never `all`.

**Re-derive the lever before relying on it** — a size read off a tree is a claim about that tree's current
state, nothing more:

```bash
du -sh build/<preset>/bin build/<preset>/lib build/<preset>/CMakeFiles
ls build/<preset>/bin | wc -l                       # total executables
ls build/<preset>/bin/interop_* | wc -l             # of which interop drivers
du -ch build/<preset>/bin/<the 4 binaries the cells name> | tail -1
```

⚠️ **Do not record the answer here.** An earlier draft did, and the `_packaging_tests` slice it counted was
reclaimed within the hour — inside the section whose subject is instruments that cannot stay true.

⚠️ **State the scope limit wherever the sanitizer result is reported, and do not overclaim.** A targeted
UBSan build gives UBSan coverage of **exactly the paths 089 exercises** — which is what FR-021 and SC-010
assert and all they assert. It is **not** repo-wide UBSan coverage; Tier 1 runs full suites under
sanitizers separately. Reporting it as the latter would repeat, at the reporting layer, the same
label-vs-substance error that made the previous `asan-ubsan` claim false.

### Per-configuration budget — a mapping and a recipe, not a table of sizes

| Matrix config | Build tree | Reclaimable? |
|---|---|---|
| `normal` | `build/linux-clang-debug/` | ⛔ **NEVER** — standing user rule 2026-09-10 |
| `asan` | `build/linux-clang-asan/` | yes — **after its 8 cells have run and their evidence is persisted** |
| `tsan` | `build/linux-clang-tsan/` | yes — same condition |
| `ubsan` | `build/linux-clang-ubsan/` | yes — same condition |

**Re-derive occupancy at the moment you need it** — `du -sh build/*/`, plus an object count
(`find build/<preset> -name '*.o' | wc -l`) whenever a tree's size is used to argue about a *build* rather
than about the tree.

⚠️ **A SIZE IS NOT A COST, and one tree here proves it: `linux-clang-ubsan` is essentially UNPOPULATED** —
a fraction of the object count of its three siblings. Its small directory is evidence about the directory,
not about a ubsan build; scaled to a populated sibling's object count at that sibling's bytes-per-object, a
**full** ubsan build is comparable to the others.

⛔ **This falsifies the "ubsan is nearly free" reasoning** an earlier draft used to justify the fourth
configuration and to order the sequence. The fourth config is cheap **only under the targeted build
above**. The decision itself is unaffected — it rests on Article IX §2, not on cost — but the cost argument
had to be re-derived rather than repeated.

⚠️ **All four are matrix configurations now, not three configurations plus a scratch tree.** Under FR-021
`linux-clang-ubsan` is a **required arm** as well as the cheapest thing to delete, so the reclaim order and
the matrix order are two different orderings over the same four trees. **A tree may be reclaimed only once
its runs are complete and its evidence is persisted outside the build tree** — otherwise the reclaim order
silently destroys a run the matrix still owes.

**Both required values are derived from R-1's measurement with headroom, not from a round number** —
`required_internal_free` and `required_host_growth`, **per configuration, all four, against the targeted
build**, each recorded with the figure and the date it was taken, because these move. An unset or
unparseable value is a hard error, never `0`.

**`/mnt/wsl/fixppbuild` is a different device and must NOT be budgeted against either ceiling.**
`CCACHE_DIR=/mnt/wsl/fixppbuild/ccache` — and the persisted evidence root
`$FIXPP_INTEROP_EVIDENCE_ROOT`, default `/mnt/wsl/fixppbuild/interop-evidence/` (FR-014b) — sit on
`/dev/sde`, a **separate VHD** whose backing file is not on `E:`, while `/` is `/dev/sdd`. Growth there
consumes neither `required_internal_free` nor `required_host_growth` (D-11). Confirm the device and its
free space with `df -k /mnt/wsl/fixppbuild`; no figure is recorded here.

⚠️ **That separateness is what makes the evidence root safe**: it is outside every build tree, so
`rm -rf build/<preset>` cannot destroy a run the matrix still owes, which is the hazard FR-014b exists to
close.

### Reclaim procedure

Order: **cheapest-useful-first, re-derived with `du -sh build/*/` at the moment of reclaim** — never from a
remembered order. ⚠️ The order genuinely moves: `linux-clang-ubsan` has headed it only because that tree is
currently unpopulated, and **this feature's own matrix populates it**, which changes its position. **⛔
`build/linux-clang-debug/` is never deleted** — standing user rule 2026-09-10, and it is also the `normal`
configuration's tree, so it stays resident throughout.

⚠️ Deleting a configuration means it must be **rebuilt** before its arm of the 32-run matrix can execute,
and it must not be deleted before that arm has run.

### ★ Matrix sequencing — four FULL builds cannot fit; four TARGETED builds can

This is a real planning constraint on FR-021, not an afterthought, and the two build units give opposite
answers — which is why the unit is decided above rather than left to the implementer:

**The condition:** four **full** builds of the four presets do not fit inside the VHD, by a wide margin.
Four **targeted** interop-driver builds do. That is the planning constraint on FR-021, and it is why the
build unit is decided above rather than left to the implementer.

**Derive it, do not read it** — the arithmetic is deliberately not written out here, because every operand
in it moves:

```bash
du -sh build/*/                                   # current occupancy of the four trees
find build/<populated preset> -name '*.o' | wc -l # objects, for scaling an UNPOPULATED tree
df -k /                                           # what may be resident
df -k /mnt/e                                      # what the VHD may still grow — the binding one
```

Scale any unpopulated tree to a populated sibling's object count at that sibling's bytes-per-object before
summing; sum the four; compare with `df -k /`.

⛔ **An earlier draft summed the four trees' *current* sizes and concluded they fit.** They did not: it took
an essentially unpopulated `linux-clang-ubsan` at face value. The conclusion (*sequence them*) survived;
the number that justified it did not — **a size read off a tree is a claim about that tree's current state,
not about the build it will hold.** A second draft replaced that number with a corrected one, which was
itself falsified the same day. Hence no number.

⚠️ **Fitting inside the VHD is necessary, not sufficient.** Only the already-materialised reuse pool may be
reusable — a *bound*, not a measurement (see *Two ceilings* above) — and beyond it every byte costs host
growth against the host reading alone. So sequencing is required even under targeted builds, and R-1's
`required_host_growth` is the predicate that decides it.

**Plan for the four-config matrix to be run in sequence**, with:

1. Preflight → build config *k* → run its 8 cells → **persist that config's results and evidence to disk
   outside the build tree** → reclaim config *k* (never `normal`) → preflight → build config *k+1*.
2. Results must be **durable before reclaim**, and the persisted location is a **named value**:
   `$FIXPP_INTEROP_EVIDENCE_ROOT`, default **`/mnt/wsl/fixppbuild/interop-evidence/`** (FR-014b). That is
   on `/dev/sde` — the separate VHD already holding `CCACHE_DIR`, whose backing file is not on `E:` — so it
   is outside **every** build tree and outside **both** disk predicates (D-11). The **named promotion
   command** `promote_interop_evidence.py` (FR-014b) copies the bundle there and writes the ledger entry;
   it runs **after** the config's 8 cells and **before** its tree is reclaimed. ⚠️ Deleting a build tree
   that still holds the only copy of a run's evidence loses the run. **No committed row ever names a path
   into a build tree** — no committed row names an absolute path at all.
3. The evidence record must therefore be **accumulated across configurations**, not rewritten per
   configuration — a later config must not erase an earlier one's rows. The witness completeness gate
   (FR-015b/FR-015c) compares each configuration's projection against every other configuration's and
   against the census's 100 keys — **across `config`, never across `arm`** — *and* runs over the union after
   the last configuration; the union reading alone cannot see a configuration that produced nothing.
4. **Suggested order: `normal` → `ubsan` → `asan` → `tsan`.** `normal`'s tree is resident and never
   reclaimed. `ubsan` goes early because it is the arm whose absence made the previous Article IX §2 claim
   false, so the constitutional coverage lands first — **not** because its directory is currently small;
   that tree is unpopulated and a targeted ubsan build costs what any other targeted build costs. The two
   heaviest configurations then run one at a time against the host ceiling.
   ⚠️ **Reclaiming the existing FULL trees is what buys the room**: deleting the two heaviest full trees
   frees space *inside* the VHD for the targeted rebuilds to land in — which is also the only case where
   the reuse pool could plausibly be realised, and therefore a case R-1 should sample rather than assume.

### When the preflight cannot be satisfied even after reclaim

**Stop and report.** Do not proceed with a partial build. Do not record any resulting cell as a `pass`
**or** as a `skip`. A run that died on `ENOSPC` must be **distinguishable from a genuine result** — it is
neither evidence of conformance nor evidence of an unavailable counterparty, and recording it as either
corrupts the evidence set this feature exists to make trustworthy.

---

## Constitution Check

*GATE: must pass before Phase 0 research. Re-checked after Phase 1 design.*

| Article | Gate | Status | Basis |
|---|---|---|---|
| **I** — Identity & Mission | core tag-value FIX | **PASS** | Verification-only; adds no protocol surface |
| **II** — Language, Compilers, Platforms | C++23 / Clang | **PASS** | Counterparty apps are separate programs against their own vendored engines; fixpp-side code is ordinary test C++ |
| **III** — Build & Dependency Toolchain | Conan/CMake | **PASS** — *watch resolved by R-3* | QuickFIX-cpp and QuickFIX-J stay **outside** the Conan graph — they are counterparties, not dependencies. **R-3 confirms no new dependency**: neither side has a JSON library (verified), and the readback writer is hand-rolled on both. Typed access needs no new link deps either — `src/C++/fix44/` is header-only and `quickfixj-messages-fix44` is already a compile dependency |
| **V** — License | AGPL-3.0 | **PASS** — *watch resolved by R-3* | Counterparty sources are ours; QuickFIX itself is not vendored. No new library is introduced, so no licence review is triggered |
| **VI §1/§4** — Spec Coverage Discipline | 100 % FIX rule | **PASS** | Produces evidence toward it; flips nothing. `[const §VI.4]`'s coverage-index obligation is not triggered — no new OFFICIAL row |
| **VI §5** — Normative References section | presence obligation | **PASS** — *added at Gate A round 1* | ⚠️ It was **absent** and this row did not exist; `[const §VI.5]` is unconditional and the VI row above addresses §1/§4, a different clause. 089 could not use 086/087's *"the FIX set is empty"* discharge — SC-007 names five catalogue rows and each carries `[FIX50SP2] Single General Order Handling`. Now present in `spec.md`, non-empty, covering the business set plus the FIX-SL §4 sections the admin repertoire exercises. **Fifth bundle running to be caught here (085, 086, 087, 088, 089)** |
| **VII §3** — TDD mandatory | red-green-refactor | **PASS — binding** | Every witness lands RED first. FR-017/FR-018 make the RED arms deliverables, not process |
| **VII §6** — Interop | ≥1 live QuickFIX interop test covering Logon → NOS → ExecRpt → Logout | **PASS — strengthened** | Already satisfied; this feature makes it assert field values in **both** directions rather than counters in one |
| **VII §8** — grouped tests, ctest labels | select by label | **PASS** *(watch)* | Interop cells are isolation-sensitive (live peer, ports, TLS) ⇒ they stay standalone, which §8 explicitly permits. Selection stays by `-L`, never `-R <exe>` |
| **VIII** — Performance Budgets | bench for perf-sensitive modules | **N/A** | No perf-sensitive module touched. No bench obligation |
| **IX §1** — Coverage ≥95/85 on touched modules | diff-scoped | **NEEDS CONFIRMATION → R-6** | Whether this feature touches any `include/fixpp/<mod>` or `src/<mod>` at all is undetermined; test files are excluded from measurement. If the diff is tests + harness + parent repo only, the module glob selects nothing and the gate is vacuous — **which must be stated as a measured fact, not assumed** |
| **IX §2** — Sanitizers every PR | ASan, UBSan, TSan | **PASS** — *false as previously written; corrected at Gate A round 1* | ⚠️ This row read *"**PASS — expanded** … FR-021 runs all three configs"*. It was **affirmatively false**: `asan-ubsan` maps to `linux-clang-asan`, which sets `FIXPP_ENABLE_ASAN` only, and `cmake/Sanitizers.cmake` adds `-fsanitize=address` and `-fsanitize=undefined` from **independent** `if()` blocks — so `-fsanitize=undefined` reached no compile or link line and **UBSan did not run**. Article IX §2 (`.specify/constitution.md`, Article IX §2) reads *"Sanitizers — Tier 1 (every PR, Linux/Clang): ASan, UBSan, TSan must all run and pass."* Now satisfied by FR-021's **four** configs — `normal`, `asan`, `ubsan`, `tsan` (user decision, Gate A round 1). ⚠️ FR-022 makes the TSan arm **bring-up**, not a config flip |
| **IX §4** — Static analysis | clang-tidy / clang-format / cppcheck / IWYU | ⚠️ **CONDITIONAL — disposition required** | ⚠️ **NOT a PASS, and the row said PASS while the re-check table below recorded the same clause as *"disposition required, not a pass"*.** An upheld scoping disposition does not launder a green row over an unmet clause — that is precisely the aggregate-verdict defect this bundle exists to remove, appearing in the bundle's own gate table. **Scoping (settled Gate A round 1, upheld round 2, NOT reopened here):** Article IX §4 reads *"Static analysis — **Tier 1**:"*; Tier 1 is the library's CI tier and does not build the QuickFIX counterparties, which are outside fixpp's Conan graph by design. **No parent-repo clang-tidy / clang-format / cppcheck / IWYU tasks are added.** What is required is the disposition itself — how the counterparty sources are linted, or that they are not and why — written at `/speckit-verify`. Related blind spot: **#265** |
| **X** — ABI Policy | C ABI versioned contract | **N/A** | `include/fix/c_api*` untouched |
| **XII** — Security & TLS | TLS posture | **PASS** | Cells stay on TLS `one_way_ca`; mutual mTLS remains `deferred:v1.1-mtls` |
| **XVI** — Spec Kit Workflow | phase order | **PASS** | specify → clarify → plan, in order |
| **XVII** — Review Gates | Gate A + Gate B | **PASS** | Gate A **available to waive** precisely because the constitution amendment was routed to 4c, not here |
| **XVIII** — Roadmap Discipline | scope honesty | **PASS** | Re-scope, the falsified premise, and the zero-rows-closed consequence are all recorded in the spec, `REMAINING-WORK.md` and `typed-messages.md` |
| **XX** — Amendments | ride a Gate A | **N/A — deliberately** | The Article XVIII §7 / Article I §1 amendment belongs to **4c**, the first catalogue-closure feature (user decision 2026-09-10) |

**Gate verdict (pre-Phase 0): CONDITIONAL**, with two items routed to research (**R-3** dependency, **R-6**
coverage applicability) and one standing disposition to write down rather than assume (Article IX §4 on
parent-repo C++).

⚠️ **The verdict was previously written as PASS over a table containing a `NEEDS CONFIRMATION` row.** A
gate verdict that aggregates to PASS over rows saying the gate is unresolved is the same defect class as
the instruments this feature exists to fix, and it should not appear in this bundle of all bundles. It is
also *why* the IX §2 and VI §5 failures survived: an aggregate PASS is not read row by row.

### Re-check after Phase 1 design — **CONDITIONAL**

| Article | Change | Basis |
|---|---|---|
| **III**, **V** | *watch* → **PASS** | R-3 settled it by **verified absence**, not by preference: no JSON library exists on either side, so a hand-rolled writer is the only option that adds no dependency. Typed access is free — R-2 |
| **VI §5** (Normative References) | **absent → PASS** | Added at Gate A round 1; non-empty, per the row above |
| **VII §3** (TDD) | unchanged, now **concrete** | The RED arms are enumerated as deliverables in `contracts/disk-preflight.md` § *Required RED arms*, `contracts/witness-evidence.md` § *Proof obligations*, `contracts/readback-jsonl.md` § *Witnesses this contract requires*, and `quickstart.md` § Step 4 — not left to process. ⚠️ This row previously claimed *"enumerated … in `contracts/disk-preflight.md` (7 arms) and `quickstart.md` § Step 4"*, which was **true of the disk gate and false of the readback contract**: six of that contract's own *"needs a witness"* clauses had no arm anywhere. The count is deliberately not repeated here — cite the tables, which cannot go stale against themselves |
| **IX §1** (coverage) | still **open — R-6** | Deliberate. Whether the module glob selects anything must be **measured and recorded**, including when the answer is *nothing*. An empty selection reporting success is indistinguishable from a pass |
| **IX §2** (sanitizers) | **corrected** — see the row above | FR-021's **four** configs; FR-022 keeps the TSan arm from going vacuously green, FR-015c keeps any arm from going vacuously green |
| **IX §4** (static analysis) | **disposition required, not a pass** | New C++ lands in the **parent** repo, outside `/speckit-verify`'s Step-1 glob (`src/**`, `include/**`) — the same blind spot filed as **#265**. Record how the counterparty sources are linted, or record that they are not and why. ⚠️ **Scoping, settled at Gate A round 1**: Article IX §4 reads *"Static analysis — **Tier 1**:"* and Tier 1 is the library's CI tier; the QuickFIX counterparties are deliberately outside it (they link QuickFIX, outside fixpp's Conan graph by design), and Tier 1 does not build them. A counter-proposal to add parent-repo clang-tidy / clang-format / cppcheck / IWYU as **required tasks** was therefore **rejected** — it over-reads the clause. The correct discharge is the disposition; what was wrong was calling it PASS while saying so |

**No new violations were introduced by the design.** The one structural addition — a second result
artifact beside `cell_results.yaml` — is justified in *Complexity Tracking* and exists to preserve the
exact-set completeness property at the witness level, which nesting would destroy.

**Open items behind the CONDITIONAL verdict**, named rather than aggregated away:

| Item | State |
|---|---|
| **IX §1** — coverage applicability | **open**, routed to R-6; must be *measured*, and recorded even when the answer is *nothing selected* |
| **IX §4** — parent-repo static-analysis disposition | **disposition required**, scoping settled (Tier 1 does not build the counterparties); the disposition itself is written at `/speckit-verify` |
| **IX §2** — sanitizer coverage | **resolved** by the four-config decision (Gate A round 1). Retained in this list because it was the row the aggregate PASS concealed |

⚠️ **Six design decisions are load-bearing against a false green and must survive `/speckit-tasks`
intact**, because all six look like details and none is:
1. **Readback file opened in TRUNCATE mode** (R-4). Append would silently accumulate stale records
   across runs, and a stale record can satisfy a comparator looking for a witness this run never
   produced.
2. **The expected witness set derived from the conversation script, never hand-listed** (W-2) **AND
   cross-checked against an independent declarative census** (W-2a / FR-015d). Both halves are required and
   neither alone is a check: a hand-maintained list drifts toward whatever is currently produced, and
   script-derivation alone is self-consistent under step deletion — one source drives both production and
   expectation, so deleting a step removes the witness *and* its expectation and the gate stays green.
3. **`config` on every witness row, and `cell_id` inside the completeness key** (FR-015c / W-3a / W-1). A
   union-only gate over rows with no `config` cannot see a configuration that produced **zero** witnesses —
   the union is unchanged and exact-set equality passes. And the obvious remainder key
   `(arm, script_step_id, direction, occurrence)` collapses all four role×flavour combos into one set, so a
   configuration that ran one of its four projects identically to one that ran all four. Both are the same
   blindness on neighbouring axes.
4. **The manifest and the run ledger are two artifacts, and the committed check opens nothing**
   (FR-014/FR-014b, `witness-evidence.md` § *Two artifacts*). The schema check is a **ctest** three CI tiers
   run on hosted runners with no artifacts, against a manifest holding pre-existing `pass` rows. Any
   restatement that merges them fails everywhere and breaks FR-020.
5. **Both streams carry a `hello` AND a `terminal` record** (FR-014a, data-model §12). The hello is written
   before any message is processed, so a peer that starts, announces itself and conversates not at all
   satisfies every field a hello-only corroboration reads.
6. **The arm attestation is `Session::has_validator_for_test()` read from the LIVE session** (FR-011a). The
   `arm` label is what the harness *intended*; without the live reading the whole of US4 can go vacuously
   green, and FR-011's *"a dictionary is loaded"* cannot discriminate because it is true in both arms.

---

## Project Structure

### Documentation (this feature)

```text
specs/089-quickfix-interop-conversation/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/           # Phase 1 output
├── checklists/
│   └── requirements.md  # spec quality checklist (14/16, 2 documented deviations)
└── tasks.md             # Phase 2 — /speckit-tasks, NOT created here
```

### Source code — TWO repositories

```text
# ── PARENT repo: /home/catalin/Work/Programming/Antreprenoriat ──────────────
research/G19-fix-fpml-iso20022/phase-9-harness/
├── quickfix-cpp/counterparty/
│   └── interop_counterparty_main.cpp   # typed parse + readback emitter (C++)
├── quickfixj/src/main/java/io/fixpp/phase9harness/quickfixj/
│   └── InteropCounterparty.java        # typed parse + readback emitter (Java)
├── configs/conversation-*.cfg.in       # NEW x4 — UseDataDictionary=Y; the EXISTING *-tls.cfg.in
│                                       #   templates are NOT edited (they are named by the PD-* and
│                                       #   idle-cadence cells R-5 protects)
├── golden/                             # re-captured per cell, verify-first
├── tools/run_interop_cell.py           # collect readback; capability handshake; 8 cells
├── tools/promote_interop_evidence.py   # NEW — the named promotion step (FR-014b): the ONLY reader
│                                       #   of a run artifact, and the only writer of a pass row
└── ci/counterparties.Dockerfile        # rebuilt + republished; consumed BY DIGEST

# ── LIBRARY submodule: .../G19-fix-fpml-iso20022/library ────────────────────
tests/interop/
├── happy/hp_support.hpp                # real FIX 4.4 dictionary, not the FIX 4.2 sentinel
├── conversation/                       # NEW — the scripted conversation cells
│   ├── conversation_script.yaml        # NEW — ⭐ THE SCRIPT. The executable conversation: ordered
│   │                                   #   steps with step_id, msg_type, originator, direction,
│   │                                   #   intent field values, typed-read declarations, depends_on.
│   │                                   #   Read by BOTH sides — the gtest, and the counterparty via
│   │                                   #   INTEROP_CP_SCRIPT_PATH. Its digest is FR-008c's pin.
│   └── census.yaml                     # NEW — ⭐ THE CENSUS. A mechanical TRANSCRIPTION of
│                                       #   spec.md § Conversation census (the B-01..B-12 table, the
│                                       #   applicability table, the declared-inapplicability table).
│                                       #   ⛔ It MUST NOT be generated from conversation_script.yaml —
│                                       #   see the note below.
├── support/                            # fixpp-side sent + readback records; the shared comparator
├── cell_results.yaml                   # + evidence fields, CONDITIONAL on kind: conversation
├── cell_results_schema_check_test.py   # structure only — opens NO artifact (it is a ctest that
│                                       #   tier1/tier2/tier3-libcxx run without any run artifacts)
└── witness_evidence.yaml               # NEW — witness rows + the runs: ledger (data-model §11)
ci/
├── disk-preflight.sh                   # NEW — the gate
└── test-disk-preflight.sh              # NEW — its RED arms, pinned in ci-script-pins
.github/workflows/interop-smoke.yml     # NO PIN — keeps :latest (FR-026; § External obligations)
```

### ⛔ SC-009a's two operands are FILES, and they have separate provenance

`SC-009a` / `W-2a` assert the census set **exactly equal** to the script-derived set. That is a second
opinion only if the two artifacts have **independent origins**. Both are named above; neither may be
produced from the other:

| Artifact | Authored from | May NOT be derived from |
|---|---|---|
| `conversation_script.yaml` | the conversation design — it is what actually runs | `census.yaml` |
| `census.yaml` | a **transcription of `spec.md` § *Conversation census***, which is the human statement of intent | `conversation_script.yaml`, or any traversal of it |

⚠️ **This is the sole argument that admits a hand-written census at all.** `spec.md` § *Conversation
census* concedes a hand-written table is normally this repository's rot class and claims exemption
*because* SC-009a compares it against an independently-authored script every run. **Generating `census.yaml`
from the script is the cheapest way to make SC-009a pass, and it converts the equality into a tautology
that passes forever** — at which point the census is exactly the rot class it claimed exemption from, and
the exemption argument is retroactively false. A task that generates one from the other has not
implemented SC-009a; it has deleted it.

**Structure Decision.** The split follows the existing seam: counterparty *programs* are parent-side,
everything fixpp *asserts* is submodule-side. That boundary is not negotiable here — the counterparties
link QuickFIX, which is deliberately outside fixpp's dependency graph.

⚠️ **Consequences that must be planned for, not discovered:**
1. **Gate A/B operate on the library PR.** The parent-side counterparty changes need an explicit review
   disposition — they are C++ that no Gate B reviewer sees by default.
   ⭐ **AND HERE IS THE MECHANISM, because *"needs a disposition"* names none, and a need with no producer
   is the RC-A shape this plan's own § *External obligations* exists to catch.** The parent-side diff is
   not discoverable from the library PR, so it is handed over deliberately: the `/gate-b` brief for this
   feature **names the parent-side counterparty diff as in-scope**, enumerated at the head being reviewed
   with `git -C <parent repo> diff --stat <merge-base>..HEAD -- phase-9-harness/ .github/workflows/`
   — ⛔ **re-derived at review time, never a file list written down here**, which would go stale on the
   next parent-side file this feature touches — and the reviewer's disposition on it is **recorded in
   this feature's Gate B record** alongside the library-side findings. ⛔ **Silence is not a disposition**:
   a gate record that never mentions the parent-side diff records that nobody looked, not that it was
   clean. The record's row, and what its section must carry, are in § *External obligations*.
2. **The counterparty image is the coupling.** A library-side cell requiring readback protocol *v1* and a
   published image predating it is the stale-peer hazard FR-016a/FR-016b exist to make loud.
3. **Order of operations**: parent counterparty change → image rebuild + publish → digest captured →
   library-side cells pinned to that digest. A library PR merged ahead of the image is a broken tree.
   ⭐ **USER DECISION (Gate A round 9) — THIS ORDERING IS LOAD-BEARING, AND THE TYPED-ACCESSOR COMPILE
   ARM GATES THE PUBLISH.** The counterparty is rebuilt and republished **before** 089's CI runs, and the
   arm executes inside that rebuild, so a schema-conformance regression stops the image from being
   published at all and 089 has **no new digest to pin**. ⛔ **No new PR-time CI workflow is created for
   the arm.** The mechanism, and the three conditions that make the gating real rather than nominal, are
   in § *External obligations* → the typed-accessor compile-arm row; they are not restated here.
   ⚠️ **THE RESIDUAL, STATED RATHER THAN IMPLIED**: `publish-counterparties.yml` is
   `on: push: branches: [main]`, so the arm's verdict lands **after** the parent-side counterparty change
   has merged and **before** 089's CI consumes the resulting digest. **It is not a check on any pull
   request, parent or library.** What it guarantees is that no image carrying a non-conforming
   counterparty is ever published, hence none can be pinned; it does **not** guarantee that a
   non-conforming change is caught before it reaches the parent's `main`. Do not read *"gates the
   publish"* as PR-time coverage.
4. ⚠️ **Republishing moves `:latest`, and everything NOT pinned rides it.** FR-016b pins *this feature's*
   cells to a digest; `interop-smoke.yml`'s `IMAGE:` key names the mutable `:latest`, which is the only tag
   that workflow knows. On republish, **every existing interop consumer immediately runs new counterparty
   code**, unpinned and ungated — including the cells FR-020 requires to keep passing, the cells R-5
   deliberately protects (`INTEROP_CP_CORRUPT_ADMIN`, the `PD-*` malformed-dup cells: the readback emitter
   is new code on the inbound path of every message they send), and the required smoke workflow itself.
   ⛔ **An earlier revision concluded from this that Step 0 is to pin existing consumers to the pre-089
   digest. That is REVERSED (user decision 2026-09-10).** Those cells running new counterparty code is
   not the hazard — it is FR-020's **subject**, and pinning them away from the new image is what makes
   FR-020 undischargeable. The ordering is **publish → verify → depend**: publish, run FR-020 against
   the new image, and only then let anything depend on it, with no pull request touching the consumer
   paths opened in between. **`spec.md` FR-026 is the normative home**; **R-11's prescription is
   superseded** while its concern — nothing should silently depend on an unverified image — is what the
   new ordering discharges.

---

## External obligations — files this feature MUST change that are not in the bundle

*Collected at Gate A round 1. Six of these were previously stated in prose scattered across three
artifacts, and `/speckit-tasks` derives from FRs — a prose obligation in a research item is not a task.
This table is the single place they are enumerated.*

### ⛔ THE COMPLETION RULE — read this before adding any MUST-clause to this bundle

> **A new MUST-clause lands in the SAME edit as its entry in the relevant CLOSED INVENTORY — or it is not
> applied.** The closed inventories are exactly four:
>
> 1. `contracts/readback-jsonl.md` § *Witnesses this contract requires*
> 2. `contracts/witness-evidence.md` § *Proof obligations for Level 1* (and the E-*/W-* obligation tables)
> 3. `quickstart.md` Step 4 (forced-failure · spurious-hit · controls)
> 4. **this table**
>
> If a clause deliberately belongs in **none** of them, say so **in the clause**, and say which
> inventories do carry it. Silence is indistinguishable from an omission, and the next review files it.
>
> ⭐ **RC-A — EVERY ENTITY IN `data-model.md` GETS A ROW IN THIS TABLE NAMING THE FILE IT LANDS IN AND THE
> COMMAND THAT PRODUCES IT.** A schema, a constraint and a Step-4 arm are three descriptions of an entity;
> none of them **makes** one. ⚠️ This line exists because the validation pair (data-model §10) was
> schemad, constrained by E-7 and armed in Step 4 while **no artifact held it, no command produced it, and
> nothing gated its existence** — so zero pairs was green through every gate in this bundle.
>
> ⚠️ **RC-A was SWEPT over all thirteen `data-model.md` sections in the edit that added it**, not applied
> only to the entity that prompted it — applying a rule to its own instance and no further is the very
> shape the rule is about. It closed **§12's terminal record on the COUNTERPARTY side** (fixpp's row named
> `hello`/`terminal`, the counterparty's named only the hello) and **§4's witness rows** (the comparator was
> in the directory tree, not in an obligation).
>
> ⛔ **No count of what the sweep found is recorded here, deliberately.** An earlier revision said *"two
> further unrowed producers"* and a later round found a third — and a replacement count is simply the next
> round's finding. **Re-derive instead**: walk every `##` heading of `data-model.md` — ⚠️ **the numbering is not
> contiguous**, so a `## N.` pattern silently drops `## 1a.` (the section carrying `has_validator`, E-6's
> operand) — and check each resolves to a row in the table below or to a named exception in the next
> paragraph.
>
> ⛔ **The deliberate exceptions, and the reason each is one:**
> - **§8, the disk preflight reading** — its producer is `ci/disk-preflight.sh`, specified at length in this
>   file's own § *The gate itself* and in `contracts/disk-preflight.md`, which is where a reader looks.
> - **§7, the conversation-script step** — `conversation_script.yaml` is a **hand-authored input**, not a
>   generated artifact, so it has no producing command by design. ⚠️ RC-A's failure mode (*an entity nothing
>   writes*) cannot arise for an input a human commits; what it needs instead is a **validator**, and it has
>   one — SC-009a asserts it exactly equal to `census.yaml`, whose independence rule is stated in
>   § *SC-009a's two operands*.
>
> ⭐ **RC-B — § *External obligations* is UNCONDITIONAL.** A clause that imposes work on any file **outside
> this bundle** lands in that table, whatever other inventory carries it. Relevance is judged only for the
> other three. ⚠️ **This does not make relevance mechanical in general** and must not be sold as doing so;
> it closes the one axis where the answer is a **fact** — *does this clause require a file outside
> `specs/089-quickfix-interop-conversation/` to change?* — rather than a judgement. The rule above named
> four inventories and said *"the relevant"* one with no test for relevance, and that judgement is exactly
> what went wrong: E-7 imposes work on `promote_interop_evidence.py` and `witness_evidence.yaml`, **both
> already rows in this table**, and would have been caught mechanically.

⚠️ **This rule exists because the post-exhaustion hand-edit added FIVE normative obligations and
instantiated an arm for ONE.** The other four came back as findings — two of them as
*replaced-with-new-defect* — and patching them individually would have left the mechanism that produced
them intact. ⚠️ `plan.md`'s own miniature of the same failure: a stray blank line once terminated this
table four rows early, so four obligations sat as prose **inside the section that claims to enumerate
them**. Markdown does not warn.

⚠️ **A clause of the form *"X is declared / enumerated / specified / named"* is scored ABSENT** — the
round-2 acceptance rule, still in force. Produce the artifact, not a sentence about the artifact.

| File | Obligation | Required by |
|---|---|---|
| `library/tests/interop/cell_results_schema_check_test.py` | **`CONFIGS`** gains `asan` / `ubsan`, loses `asan-ubsan` | FR-021a |
| ″ | ⛔ **`test_ids_unique` is NOT replaced.** Round 1 recorded that it must be, on the assumption that 8 ids had to serve 32 rows. The manifest/ledger split removes that: the committed manifest carries one row per `(cell_id, config)` slot with `id = "<cell_id>@<config>"`, unique across 32, and retry rows never reach it | FR-013a · E-1a · data-model §5 |
| ″ | **Status vocabulary** extended with `error:enospc` / `aborted`. The shipped set `{pass, fail, skip, known-limitation, n/a}` is closed by an assertion, `n/a` is bound to a `deferred:*` disposition and `known-limitation:*` to a tracking issue — so every existing option is forbidden and the implementer reaches for `fail` | FR-014a · E-5 |
| ″ | ⛔ **`REQUIRED_FIELDS` is extended CONDITIONALLY, on `kind: conversation` only — never globally.** An unconditional extension breaks the **pre-existing `status: pass` rows already committed**, none of which has an 089 run artifact, colliding head-on with FR-020. ⛔ **NO COUNT OF THOSE ROWS IS WRITTEN ANYWHERE IN THIS BUNDLE, DELIBERATELY — THIS ROW IS THE ONE HOME OF THE RE-DERIVATION RECIPE AND EVERY OTHER SITE STATES THE CONDITION ONLY**: `grep -c "status: pass" tests/interop/cell_results.yaml`. ⚠️ The figure is load-bearing on a file this bundle does **not** own, other features add rows to it, and **089 itself adds rows to it** — so a number written down here is stale before this feature even lands, which is a stronger case than the RC-A sweep count and the disk figures this bundle already de-numbered for the same reason. ⛔ A **partial** de-numbering would be worse than none: with both forms present a reader cannot tell a current bare count from a site the edit missed | FR-013 · FR-020 · E-1 |
| ″ | ⛔ **The check MUST NOT open any artifact path.** It is a **ctest** (`tests/interop/CMakeLists.txt:459`) provisioned in `tier1.yml`, `tier2.yml` and `tier3-libcxx.yml` on hosted runners that hold **no** run artifacts. What it checks instead: every `status: pass` conversation row names an existing ledger entry whose `terminal_state` is `completed` and whose `witness_count` equals the census figure for that slot; the set of slots carried by **`kind: conformance`, `authoritative: true`** ledger rows equals the 32-slot inventory exactly; exactly one such run per slot; ⭐ **the `validation_pairs:` section carries exactly the 16-pair `kind: conformance` inventory** (**E-7a**) with every `off_run_id`/`on_run_id` resolving to a `runs:` row; ⭐ **at least one `kind: validator-positive-control` pair exists**, resolving to two control ledger rows with opposite `has_validator` (**E-7b**); and ⭐ **for every `authoritative: true` pair, both referenced runs are still `authoritative: true`, each conformance reference still being its slot's selected run** (**E-7c**). ⛔ **E-7c is SCOPED to `authoritative: true` pairs**, exactly as E-7a is — data-model §10 retains superseded pairs and defines one as `authoritative: false` **iff** a referenced run was superseded, so an unscoped E-7c would be RED on every correctly recorded retry, permanently, in a committed ctest. `contracts/witness-evidence.md` **E-7c is the normative home** of that scope and of what it verifies; this row points at it and does not restate it. ⚠️ **Every operand of all three pair gates is inside the committed file**, so they open nothing and survive into CI — the promotion command cannot host E-7a (it runs per configuration and cannot see 16 pairs until the last one) and **must not be the only host of E-7c**, because `authoritative` is *mutable after a pair is written*: a retry landing later supersedes a referenced run, and a construction-time check does not re-run. ⛔ **No count of the operands is recorded here** — a count is what the RC-A sweep deleted for this reason. ⚠️ `validator-positive-control` and retry rows are recorded in the ledger and **excluded** from the slot equality (data-model §11 § *THE TWO DISCRIMINATORS*) | FR-014 · FR-012a · E-1 · E-1c · E-7a · **E-7b** · **E-7c** · W-3b · W-3d |
| `library/tests/interop/witness_evidence.yaml` (**NEW**) | **THREE sections, not two** — witness rows, the `runs:` **ledger**, and ⭐ **`validation_pairs:`** (data-model §10, FR-012a): committed, machine-independent, `evidence_relpath` relative to `$FIXPP_INTEROP_EVIDENCE_ROOT`, **never an absolute path**. ⚠️ The `runs:` ledger holds **every** run — `conformance` and `validator-positive-control`, authoritative and superseded — with `kind`/`authoritative` discriminating; it is not filtered on the way in, because a retry that is never written down cannot be shown not to have been counted. ⚠️ `validation_pairs:` was **absent from this row** while four artifacts described the entity — the RC-A defect, in the row RC-A is about | FR-014b · FR-012a · E-7 · E-7a · data-model §6 · §10 · §11 |
| `phase-9-harness/tools/promote_interop_evidence.py` (**NEW**) | The **named promotion command** (FR-014b). Opens both streams, requires a `hello` **and** a `terminal` on each, checks the join keys, evaluates the completeness gate, persists the bundle under the evidence root, records `evidence_digest`, writes the ledger entry and the manifest row. Runs **after** a config's 8 cells complete and **before** its build tree is reclaimed. Nothing else may write a `status: pass` conversation row.<br>⭐ **AND IT IS THE PRODUCER OF THE VALIDATION PAIRS.** Once both arms of a `(combo_id, config)` have promoted runs, it **constructs** the pair (data-model §10). ⛔ **AND IT CONSTRUCTS THE `validator-positive-control` PAIR ON THE SAME RULE, from the two control runs of FR-010a** — those runs sit **outside** the 32-slot inventory and have no `(combo_id, config)` slot, so a trigger worded only over slots never fires for them and **E-7b would gate an entity with no producer** — the RC-A shape, in the row that adds the gate. The control pair's trigger is *both `kind: validator-positive-control` runs of one `config` have promoted*; it carries `expected_verdict: diverged`, and E-7 is evaluated on it identically (E-7 is `kind`-agnostic). ⚠️ `quickstart.md` Step 4 arming the construction does **not** discharge this: Step 4 is a demonstration, not an obligation row. ⭐ **The promotion command then evaluates E-7** on every pair it constructs, conformance and control alike — `off_run_id ≠ on_run_id`; `off_run_id`'s run carries `cell_pair[0]` and `has_validator: false`; `on_run_id`'s carries `cell_pair[1]` and `true`; both carry the pair's `config`, `script_digest` and `kind` — and **writes it to `validation_pairs:`**. A pair failing E-7 is **promotion RED**; it is never written. ⛔ **AND IT IS THE WRITER OF `authoritative` ON A RUN — `conformance` AND `validator-positive-control` ALIKE.** Promoting a run writes it `authoritative: true` and **demotes the run it supersedes** in the same write: for a conformance run, the prior run of its `(cell_id, config)` slot; for a control run, the prior run of the same probe. **data-model §11 is the normative home of that rule and this row does not restate it.** ⚠️ **Without this the rule has no producer — the RC-A shape** — and §10's demotion trigger (2), which fires *on* a run being superseded, would have nothing to fire on. ⛔ **AND IT IS THE WRITER OF `authoritative` ON A PAIR, INCLUDING BOTH DEMOTION TRIGGERS.** ⭐ **data-model §10 is the normative home of those triggers and this row does not restate them** — it points, because a second copy is what four of round 9's findings were. What this row adds is the *obligation to implement them*: the promotion command is their only producer. ⚠️ **Trigger (2) — demoting the pairs that reference a run the retry superseded, including when no replacement pair is constructed — is the one an implementer will omit**, and **E-7c** is the check on exactly that omission; without the trigger E-7c would gate a field with no producer (the RC-A shape) *and* redden a compliant producer. The E-7c fixture (`contracts/witness-evidence.md` § *Proof obligations for Level 1*) forces it: run superseded, pair left `authoritative: true`, no replacement pair written. ⚠️ It does **not** host the 16-pair completeness gate: it runs per configuration and cannot see 16 pairs before the last one, so **E-7a lives in the committed schema check**. ⛔ **AND IT CONSTRUCTS THE VALIDATION PAIRS BY DERIVATION**: `accepted_off`/`accepted_on`/`dispositions` read from the two referenced run artifacts, `verdict` computed by exact set equality (`conformance ⇒ identical`, `validator-positive-control ⇒ diverged` matching `expected_verdict`); both references `authoritative: true` and, for a conformance pair, the run selected for its `(cell_id, config)` slot; exactly one authoritative conformance pair per `(cell_pair, config)`. ⚠️ Carrying the result instead of deriving it lets a hard-coded `identical` pass E-7, E-7a **and** FR-010a at once. ⚠️ **This clause sat in the *Required by* cell**, where it obligated nothing: a producer built from the Obligation column alone hard-codes `identical` and passes every gate, and the three things it carries — the extraction of the sets from the artifacts, the derivation of `verdict`, and the one-authoritative-pair-per-slot cardinality — appear in no other cell of this row | FR-014 · FR-014b · FR-012a · E-1b · E-7 · data-model §9 |
| `phase-9-harness/tools/run_interop_cell.py` | **`CONFIG_TO_PRESET`**: `{normal→linux-clang-debug, asan→linux-clang-asan, ubsan→linux-clang-ubsan, tsan→linux-clang-tsan}`; the `asan-ubsan` key is retired | FR-021 · FR-021a |
| ″ | **Six metadata keys added to the existing `cp_env` block in `launch_counterparty`** — `INTEROP_CP_{RUN_ID, CELL_ID, CONFIG, IMAGE_DIGEST, SCRIPT_PATH, SCRIPT_DIGEST}`. That block already assembles ten `INTEROP_CP_*` knobs and is passed as `env=cp_env` to **both** the C++ and the Java branch, so this is one more block in an existing pattern. ⚠️ Without it the counterparty is required by FR-013b to emit four hello fields it has **no channel to receive** | FR-013b · data-model §1 · R-4 (reversed) |
| ″ | The shim **computes** `script_digest` (lowercase-hex SHA-256 over the script bytes) and **compares** it against the value each side recomputes; a mismatch FAILS before the gtest is launched | FR-008c · FR-013b |
| ″ | **Four dedicated conversation config templates** for the eight new cells (`config_template` is already a per-cell attribute), carrying `UseDataDictionary=Y` with a FIX 4.4 `DataDictionary` path. ⛔ The existing `quickfix-cpp-{initiator,acceptor}-tls.cfg.in` and `quickfixj-{initiator,acceptor}-tls.cfg.in` MUST NOT be edited — they all carry `UseDataDictionary=N` and are named by the idle-cadence and `PD-*` cells R-5 protects, so editing them flips those cells. A regression check asserts a protected cell still renders `UseDataDictionary=N`. ⛔ A "narrowly scoped renderer override" was **rejected**: the per-cell `config_template` seam already exists | FR-002 · R-5 |
| ″ | ⭐ **A full `INTEROP_FIXPP_*` env block on the GTEST's environment** — `RUN_ID`, `CELL_ID`, `CONFIG`, `ARM`, `SCRIPT_PATH`, `SCRIPT_DIGEST`, `READBACK_PATH` (data-model §1). ⚠️ `INTEROP_CP_*` is `env=cp_env` on the **counterparty** launch and does not reach the gtest, while `run_id` is minted by the shim and has no other in-gtest source — so without this block fixpp emits a stream promotion cannot join. An absent key is a hard abort, never a default | FR-013b · FR-014 · E-1b |
| ″ | The **pre-conversation hello gate** (FR-016a), shim-side, before the gtest is launched. ⭐ **It reads the required minimum for each announced capability from `phase-9-harness/tools/interop_capability_minimums.yaml` (§ *External obligations* → the capability-minimums row) and compares the peer's announcement against it** — that artifact is this gate's operand. ⚠️ Its failure text must not use `unavailable:` — `parse_gtest_status` greps that token and returns `skip:` | FR-016a · FR-024 · R-4a · **C-2** · **C-12** |
| ″ | Invoke the named promotion command after each configuration's 8 cells and **before** reclaiming that configuration's build tree | FR-014b |
| `phase-9-harness/tools/interop_capability_minimums.yaml` (**NEW**) | ⭐ **THE CAPABILITY-MINIMUMS ARTIFACT — the required-version operand, materialized; this is the *capability-minimums row* every other site points at, and its normative home.** Applied under **RC-B** (a file outside the bundle). ⚠️ **This is a REPAIR, not a new requirement**: `contracts/readback-jsonl.md` **C-2** has compared `readback_protocol` against *"what the cell requires"* since before C-12 existed, `quickstart.md` § *Forced-miss arms* instantiates an arm for *"older than the cell requires"*, and **no artifact in this bundle or in the harness held that value** — the `Cell` dataclass in `run_interop_cell.py` has no version-minimum field of any kind — so the arm was **unbuildable** and both obligations compared against nothing. **C-12 inherited the same hole verbatim.** ⛔ **CONTENT**: for **each conversation cell**, the minimum required value of every capability the peer announces in its `hello` — today `readback_protocol` and `typed_accessor_arm` (data-model §1). Keyed **by cell**, because every consuming clause says *"what the **cell** requires"* and a globally-keyed file would silently change that rule at nine sites. ⛔ **NO VALUE IS WRITTEN INTO THIS BUNDLE — the minimums are chosen at implementation time, from the arm and format versions that actually ship.** A version pin in a design document is the hazard class that produced **four** wrong compiler-diagnostic literals in rounds 5–8: right on the day it is typed, silently rotten afterwards, never re-run. ⛔ **FAIL-CLOSED, AND THIS IS THE ANTI-VACUITY HALF — ask what ELSE satisfies *"the peer announces ≥ the minimum"***. The answer is: **an absent minimum**. So a missing or unparseable artifact, a cell with no entry, or an entry omitting a capability the `hello` carries MUST **FAIL the cell** — never default to *"no minimum"*, never skip, never pass. ⚠️ **The bundle already has this shape and it is the precedent, not an invention**: `contracts/disk-preflight.md` **D-9** (armed at `quickstart.md` § *Forced-miss arms*) forces a threshold *unset or unparseable* and requires RED, because a predicate with no operand **measures nothing while reporting success**. ⛔ **ONE ARTIFACT, AND EVERY COMPARISON RESOLVES ITS MINIMUM FROM IT** — any clause anywhere in this bundle that compares an announced capability against a required minimum resolves that minimum here and declares none of its own. ⛔ **NO CONSUMER ROSTER IS KEPT HERE**: a list of sites is the shape this round deleted four times over, and it goes stale on the next capability added or the next site renamed. The reader of a comparison is sent here by that clause; the reverse index is not maintained | **C-2** · **C-12** · FR-016a · FR-003b · data-model §1 |
| `library/tests/interop/` **and** `phase-9-harness/tools/` — ⭐ **THE E-*/W-* FIXTURE CORPUS (NEW row, not a new obligation)** | ⚠️ **Applied under RC-B, and it exposes a pre-existing omission rather than adding work.** `contracts/witness-evidence.md` § *Proof obligations for Level 1* already required a **negative fixture per obligation** — E-1, E-1b (five of them), E-1c, E-5, E-6, E-7 — plus the *"pre-existing `pass` rows stay GREEN"* control, and **none of them had a row in this table**, because relevance was judged and the evidence inventory looked sufficient. RC-B makes it unconditional: these are files outside the bundle. **Fixture host follows the checker** — E-1 · E-1c · **E-7a** · **E-7b** · **E-7c** · W-3a/b/c/d are checked by the committed ctest (`cell_results_schema_check_test.py`) and their fixtures sit beside it, opening nothing; E-1b · E-6 · **E-7** are checked at promotion and their fixtures sit beside `promote_interop_evidence.py`. ⛔ **NO FIXTURE IS ENUMERATED IN THIS ROW.** A partial list of E-7's and E-7a's fixtures stood here, dated *"this round's additions"* against a round that is no longer this one; it is deleted rather than extended. ⛔ **Plus the pair fixtures — POINTED AT, NOT COPIED.** `contracts/witness-evidence.md` § *Proof obligations for Level 1* **is** the inventory, and it carries each fixture's **polarity label** with it. ⚠️ **A second, partial copy of an inventory was here and is deleted**: it had drifted — it mislabelled one fixture's polarity, gave the first no label at all, and did not range over the obligations added after it was written. Neither counted nor enumerated here; extending the copy would have reproduced the two-sites-one-fix shape that has cost this bundle several rounds. Same move already made for the RC-A sweep count and for the compiler literals | E-1 · E-1b · E-1c · E-5 · E-6 · **E-7** · **E-7a** · **E-7b** · **E-7c** · W-3a · W-3b · W-3c · W-3d · FR-020 |
| `phase-9-harness/INTEROP-016-DESIGN.md` | The config vocabulary `normal\|asan-ubsan\|tsan` is corrected to the four-config set | FR-021a |
| `phase-9-harness/INTEROP-COVERAGE-REPORT.md` | The claim that the charter's ASan+UBSan requirement is met by `asan-ubsan` is corrected | FR-021a |
| `library/spec/behaviors-and-limitations.md`, `library/spec/feature-catalogue.md` | ⭐ **APPLIED UNDER RC-B — the two evidence cells that OVERCLAIMED, corrected rather than exempted.** Each reads *"green under `normal` + `asan-ubsan`"*, and `run_interop_cell.py` mapped `asan-ubsan` to the **ASan-only** preset, so each credits UBSan coverage that **never ran**. Each MUST state the preset that **actually ran** while keeping the historical label, so the record stays a record and stops overclaiming. ⛔ **This is not a vocabulary rename**: renaming the label alone leaves the false coverage claim standing, which is the substitution FR-021a's correction exists to prevent. ⚠️ **A record that was true when written is left alone; a record that overclaims is fixed** — FR-021a is the normative home of that distinction and this row does not restate it. ⚠️ **Rowed here because RC-B is unconditional**, independently of who executes the edit: these are files outside the bundle that a clause of this feature requires to change | FR-021 · FR-021a · FR-023b |
| `library/tests/interop/cell_results.yaml` | ⭐ **APPLIED UNDER RC-B — the vocabulary COMMENT, which is PRESCRIPTIVE.** Its line 12 states the config vocabulary as `normal \| asan-ubsan \| tsan`; it prescribes rather than records, so it is corrected to the four-config set in the same change as `CONFIG_TO_PRESET` and `CONFIGS`. ⛔ **The file's ROWS are not touched and need no carve-out** — every row carries `config: normal` and the file holds **no** `asan-ubsan` row, so the corpus an earlier revision of FR-021a deferred to *"a filed issue"* does not exist. ⚠️ **Re-derive before trusting either half, and note how the wrong answer presents**: the rows are inline flow-mappings (`- { id: …, config: normal, … }`), so an anchored `^ *config:` pattern returns **zero** and reads as confirmation of absence — use `grep -o "config: [a-z-]*" tests/interop/cell_results.yaml \| sort \| uniq -c`, seeding one mutated value first to prove the instrument can report a non-`normal` config at all | FR-021a |
| `library/.github/workflows/interop-smoke.yml` | ⛔ **NO PIN — the `IMAGE:` key keeps naming `:latest`, deliberately.** An earlier revision of this row required pinning it to the pre-089 digest before republishing. **That obligation is DELETED** (user decision 2026-09-10, FR-026): pinning the existing consumers away from the new image is what makes FR-020 undischargeable, since FR-020 asks whether they still pass **against the new counterparty**. What replaces it is an ordering, not a pin — publish, run FR-020, then depend. ⚠️ This row is kept rather than removed so the deletion is visible: a vanished obligation reads as an oversight to the next reviewer ⛔ **FR-016b's step-log obligation — a CONDITION, not a result.** It binds this workflow **iff** its handling of a cell's outcome lets a cell that did not run leave the job green. **Re-derive it; do not read it here**: open the step that interprets the cell's result and ask which non-`pass` outcomes still exit 0 — the pull step does not answer it. Where it binds, a result cited from this workflow's run is evidenced by that run's own **step log**, never by `.conclusion`. ⚠️ *No answer is recorded in this row, deliberately: the revisions that recorded one were each false at source (`spec.md` Clarifications, 2026-09-11).*| FR-026 · FR-016b · R-11 |
| `.github/workflows/interop-matrix.yml` (**PARENT repo**) | ⛔ **NO PIN, AND NO OVERRIDE MECHANISM IS OWED.** Two earlier revisions of this row required a pre-089 pin and then asserted an FR-016b override mechanism was *"settled"* while the task naming it still read *"decide the mechanism"*. **Both are DELETED.** With no workflow-level pin there is nothing for an 089 cell to override, so the obligation is **dissolved, not deferred** — the matrix pulls the published image like every other consumer, and 089's cells pin its digest per FR-016b. ⚠️ **The one live obligation this file still carries is a SEQUENCING rule**, whose normative home is `spec.md` FR-026: no pull request touching the consumer paths is opened between the publish and FR-020 reporting green. ⛔ Re-derive the open-PR set at publish time (`gh pr list --state open`); the count measured today is not a property of the design ⭐ **DO NOT COUNT THE CONSUMERS — RE-DERIVE THEM**: `grep -rln "fixpp-interop-counterparties" .github/workflows/ research/G19-fix-fpml-iso20022/library/.github/workflows/` enumerates every file naming the image in either repository, and the **discriminator** is that a *consumer* resolves the image at run time while `publish-counterparties.yml` **creates** it — so the publisher appears in that output and is **not** a consumer. ⛔ **This matters MORE under publish → verify → depend, not less**: with nothing pinned, every consumer rides `:latest` the instant the image is published, so the consumer set **is** the blast radius FR-020 must clear. A written-down count re-arms the moment a third consumer appears ⚠️ *Restored 2026-09-10: an earlier edit deleted this recipe as collateral while emptying the pin obligation it sat inside — it was never about pinning.* ⛔ **FR-016b's step-log obligation — a CONDITION, not a result.** It binds this workflow **iff** its handling of a cell's outcome lets a cell that did not run leave the job green. **Re-derive it; do not read it here**: open the step that interprets the cell's result and ask which non-`pass` outcomes still exit 0 — the pull step does not answer it. Where it binds, a result cited from this workflow's run is evidenced by that run's own **step log**, never by `.conclusion`. ⚠️ *No answer is recorded in this row, deliberately: the revisions that recorded one were each false at source (`spec.md` Clarifications, 2026-09-11).*| FR-026 · FR-016b · FR-020 · R-11 |
| `phase-9-harness/quickfix-cpp/counterparty/interop_counterparty_main.cpp`, `phase-9-harness/quickfixj/.../InteropCounterparty.java` | `sent` **and** `readback` emitters, the hello record — ⭐ **including its `typed_accessor_arm` attestation (data-model §1, C-12), read from the compile definition / generated constant the compile arm's own build step sets and ⛔ never written as a literal here**; the source must fail to compile when the arm's own build step is absent, so **removing or disabling the arm in the build** cannot leave a stale attestation behind — ⚠️ **the two languages do NOT get there the same way** and the mechanism for each (C++: a guard independent of the value expression; Java: an unconditional dependency on a generated marker) is stated once, in the typed-accessor compile-arm row below, together with the stated limit that it closes drift rather than forgery. This row does not restate it — ⭐ **the TERMINAL record (data-model §12)**, `occurrence` ordinals, and the specified header partition including the tag-1156 reconciliation. ⚠️ **The terminal half was found missing by the RC-A sweep run in the same edit that added RC-A**: fixpp's row names `hello`/`terminal`, this one named only the hello, while E-1b requires a `terminal` on **both** streams and `quickstart.md` Step 4 arms *"a stream carrying a `hello` and no `terminal`"* — an entity constrained and armed with **no producer**, which is item 2's shape one `data-model.md` section over. ⛔ Written **last, whatever the outcome**, including on an abort — `terminal_state` ∈ {`completed`, `aborted`, `error:enospc`} with `sent_count`/`readback_count` and the hello's join keys | FR-003 · FR-003a · FR-003b · FR-004 · FR-005 · FR-014a · E-1b · **C-12** · data-model §1 · §2 · §3 · §12 · R-10 |
| `library/tests/interop/conversation/conversation_script.yaml` (**NEW**) | ⭐ **THE SCRIPT** — the executable conversation SC-009a compares against. Ordered steps with `step_id`, `msg_type`, originator, direction, intent values, typed-read declarations, `depends_on`. Read by the gtest **and** the counterparty (`*_SCRIPT_PATH`); its digest is FR-008c's pin | FR-008a/b/c · SC-009a |
| `library/tests/interop/conversation/census.yaml` (**NEW**) | ⭐ **THE CENSUS — and W-2a's OPERAND.** A **manual, mechanical** transcription of `spec.md` § *Conversation census*, which is its **source**, not its operand. ⛔ **MUST NOT be generated from the script**: that makes SC-009a a tautology and retroactively voids the only argument admitting a hand-written census. ⛔ **MUST NOT hard-code `100`** — `spec.md` § *Conversation census* settles that the figure is derived in one place and a pointer everywhere else. ⚠️ **Stated limit** (`contracts/witness-evidence.md` W-2a): the transcription is manual and **nothing checks it** — mutate a row of the spec table and no gate reddens, because no check opens `spec.md`. **Re-transcribe in the same edit that changes the spec table** | FR-015d · W-2a · SC-009a |
| `phase-9-harness/quickfix-cpp/counterparty/interop_counterparty_main.cpp`, `phase-9-harness/quickfixj/.../InteropCounterparty.java`, **`phase-9-harness/quickfix-cpp/counterparty/CMakeLists.txt`**, **`phase-9-harness/quickfixj/pom.xml`** | ⭐ **The typed-accessor COMPILE ARM** (user decision, Gate A fresh loop round 1 — supersedes round 3's `accessor_witness` seam, which is **deleted**). **(a)** For every field the script declares as a typed read, the counterparty source reaches it through that message's **generated per-message accessor** — `FIX44::<Message>::get(FIX::<Field>&)` (C++) / `quickfix.fix44.<Message>.get(quickfix.field.<Field>)` (Java). The compiler is the schema-conformance check. **(b) The NEGATIVE-COMPILATION arm needs its own MECHANISM, and here it is — OVER THE REAL COUNTERPARTY SOURCES, not a snippet.** C++: a `try_compile` case in `phase-9-harness/quickfix-cpp/counterparty/CMakeLists.txt` **whose source argument is `interop_counterparty_main.cpp` itself** (or the shared typed-read adapter TU the production call site depends on), compiled against the same include paths and engine pin as the production target, with the mutation applied to **that file** — asserted to **FAIL**. Java: a `javac` invocation wired into `phase-9-harness/quickfixj/pom.xml`'s counterparty build **over `InteropCounterparty.java` itself**, asserted to exit non-zero. ⛔ **`check_cxx_source_compiles` over a standalone string is REJECTED as the mechanism**: a detached TU proves the *pinned engine API* accepts `Symbol` and rejects `LastPx` and proves **nothing about the counterparty** whose conformance FR-003b(a) claims — a counterparty reading everything through generic enumeration satisfies it. Three sites in this bundle say *"mutate the counterparty **source**"*; this row is the one that said *"a TU"*, and that granularity mismatch is the whole defect (RC-C: **restate the subject inside the mechanism**, not only in the requirement). **Both directions asserted**: the unmutated file compiles, the mutant does not. ⛔ **AND THE FAILURE MUST MATCH THE EXPECTED MISSING-OVERLOAD DIAGNOSTIC** — a **condition plus a per-toolchain recipe**, never a literal string: *the diagnostic identifies the `get` call as having no viable overload, **and** names the mutated field type somewhere in that diagnostic — error line or candidate notes*. ⛔ **MATCH THE WHOLE DIAGNOSTIC, NOT ITS FIRST LINE**: the toolchains do not carry the identity in the same place — clang, today, puts it in the **candidate notes only** — so *where* it lands is derived, not assumed. ⭐ **THIS ROW IS THE ONE PLACE THE ARM'S EXECUTION HOST IS STATED**; `spec.md` FR-003b and `quickstart.md` Step 4 point here instead of restating it, because three restatements are three fossils. ⛔ **STATE THE HOST, DERIVE THE TOOLCHAINS — DO NOT MAINTAIN A TOOLCHAIN TABLE.** The C++ arm is hosted by the **standalone counterparty CMake project** `phase-9-harness/quickfix-cpp/counterparty/CMakeLists.txt` (`project(interop_counterparty LANGUAGES CXX)`), which is **not** a fixpp preset target. It is built (a) in CI by the **builder stage of `phase-9-harness/ci/counterparties.Dockerfile`**, driven by the parent repo's `.github/workflows/publish-counterparties.yml`, whose `on.push.paths` includes `phase-9-harness/quickfix-cpp/**` **and** `phase-9-harness/quickfixj/**` — so the arm runs **once per counterparty-image publish**, on pushes to `main` that touch either counterparty — and (b) locally by a direct `cmake -S phase-9-harness/quickfix-cpp/counterparty`. The Java arm is hosted by `mvn package` over `phase-9-harness/quickfixj/pom.xml` in that same builder stage, and locally by a direct `mvn -f phase-9-harness/quickfixj/pom.xml package` — **both halves get a local host**, or a developer cannot run the Java arm at all. <br>⛔ **NAMING THE HOST SETTLES *WHICH COMPILER*, NOT *WHETHER THE ARM RUNS*. THREE CONDITIONS ON THE MECHANISM; THE FIRST TWO ARE THE GATE'S TEETH, THE THIRD KEEPS THE MATCHER HONEST.** Measured against the host's own two invocation lines — the C++ half is `cmake -S … && cmake --build …`, the Java half is `mvn -q -f … package -DskipTests`, and **no `ctest` and no surefire execution appears anywhere in the host**: **(i) THE ARM MUST EXECUTE UNDER THE HOST'S OWN INVOCATION.** The C++ arm is a **configure-time** `try_compile` — it runs on `cmake -S`, which the host does. ⛔ It MUST NOT be an `add_test`/`ctest` case: the host never runs `ctest`, so a ctest-bound arm is wired exactly as this row describes and **never executes** while the image builds green. The Java arm MUST be bound to a lifecycle phase **at or before `package`**; ⛔ it MUST NOT be a surefire test, because the host passes `-DskipTests`, which skips surefire *execution* (it does **not** skip `maven-compiler-plugin:testCompile`, so *"beware `-DskipTests`"* is the wrong condition to state — the right one is the positive binding above). ⛔ **AND NEITHER HALF MAY SIT BEHIND A SWITCH THE HOST DOES NOT SET.** The host passes only `CMAKE_BUILD_TYPE` and `QUICKFIX_CPP_ROOT`, and activates no maven profile. So the `try_compile` and its assertion MUST run unconditionally on `cmake -S` — **not** inside `if(BUILD_TESTING)`, **not** behind an `option()` that defaults OFF — and the Java arm MUST NOT live in a profile the host does not activate. Same fails-toward-clean shape as (i), one level down. **(ii) EXECUTING IS NOT FAILING.** ⛔ `try_compile()` only **sets a result variable**; it does not fail the configure. An arm that runs the mutant and never asserts on the result executes perfectly and publishes a **green image** — the defect this row exists to close, hiding inside the fix for it. The result MUST be asserted with `message(FATAL_ERROR …)` so `cmake -S` exits non-zero and the Dockerfile `RUN` layer fails, **in both directions** (unmutated compiles; mutant does not). ⛔ **And the asserted predicate is the CONJUNCTION, not the exit code**: RED is *the mutant compiled* **OR** *it failed without the expected missing-overload diagnostic* — capture the `try_compile` output and match it, per the matcher clause above. Loosening to *"the build failed"* is the spurious-hit hole restated. Symmetrically on the Java side: the `javac` invocation's non-zero exit MUST **propagate** and fail the maven build. ⚠️ **Observable in the log is not fatal to the invocation**, and the host runs `mvn -q`, which suppresses INFO — a log-only signal is invisible twice over. **(iii) DERIVE FROM THE ARM'S OWN CONFIGURE, NOT A NEIGHBOURING ONE.** ⛔ **Derive the required rows at build time** from `counterparty/CMakeLists.txt`'s own `CMAKE_CXX_COMPILER_{ID,VERSION}` at the point the `try_compile` is declared (or from that build's own `CMakeCache.txt`), and from `javac -version` as the maven build resolves it — never from a list written down anywhere in this bundle. ⛔ **`cmake --system-information` is REJECTED as the recipe and is not kept as an alternative**: it is a *throwaway default configure*, not this project's. It agrees today only because the host passes no `-DCMAKE_CXX_COMPILER`; the moment anything does, it reports a compiler the arm does not use, silently and toward a wrong pattern. ⚠️ **A toolchain list is a measurement and rots on every image rebuild; a host is structural.** <br>⭐ **THE PUBLISH IS GATED ON THE ARM — user decision, Gate A round 9, and it is a CONSEQUENCE of (i)–(ii), not a second mechanism.** `publish-counterparties.yml` builds and pushes in **one** `docker/build-push-action` step with `push: true`, i.e. one `buildx` invocation: a failed build never reaches the push. So *"a RED arm publishes no new digest"* follows from the arm failing the **image build**, which is exactly what (i)+(ii) require — there is nothing further to add to the workflow, and ⛔ **no new PR-time CI workflow is created**. ⚠️ **Two conditions keep that derivation true, and they are conditions rather than measurements**: the arm must fail the *builder stage* (not a later stage, and not only the carrier), and the publish workflow's concurrency group MUST NOT cancel in progress — a cancelled publish is a **missing verdict that looks like nothing**. ⛔ **The consequence for 089 is the ordering already stated in § *Project Structure* → **Structure Decision**, consequence 3**: counterparty change → gated rebuild + publish → digest captured → cells pinned to that digest. ⚠️ **Residual, stated plainly**: the verdict arrives at counterparty-**publish** time — before 089's CI, and **not on any pull request**. Read § *Project Structure* → **Structure Decision**, consequence 3 for the full statement; do not read *"gates the publish"* as PR-time coverage. <br>⭐ **AND THE IMAGE MUST RECORD THAT THE ARM RAN — the third element of the same user decision, and the one without which the other two are unfalsifiable.** ⚠️ **What goes wrong without it**: pinning an **older** digest — an image built before the arm existed — satisfies FR-016b perfectly while proving nothing, and it does so in exactly the shape a correct pin has. That is a **spurious hit**; a forced-miss arm on the pin cannot see it. **Mechanism — the existing capability handshake, no new surface**: the counterparty's `hello` (data-model §1) carries **`typed_accessor_arm`**, an integer the cell requires a minimum of, on the same rule `readback_protocol` already runs under (`contracts/readback-jsonl.md` **C-12**, C-2's sibling) — so an image predating the arm announces **nothing** and FR-016a's standing *announces-nothing ⇒ **failure**, never skip, never pass* rule fails the cell loudly, with no new rule invented. ⛔ **THE VALUE MUST BE EMITTED BY THE SAME CODE PATH THAT PERFORMS THE ASSERTION — ask what ELSE satisfies "the hello carries `typed_accessor_arm`".** A literal typed into `interop_counterparty_main.cpp` satisfies it and attests nothing. So: the C++ value is a **compile definition set by the same `CMakeLists.txt` branch that asserts the `try_compile` result**, and the counterparty TU carries `#ifndef INTEROP_TYPED_ACCESSOR_ARM` → `#error`, so **removing or disabling the arm in CMake breaks the build** instead of silently shipping a stale literal — ⚠️ **stated limit: that closes DRIFT, not forgery.** An implementer who removes the arm **and** writes a source-level `#define` of the same macro defeats it; nothing in-repo can prevent that, and this clause does not claim to. The defect being closed is the arm going away and the attestation surviving. ⛔ **THE JAVA HALF NEEDS ITS OWN STRUCTURAL DEPENDENCY, AND *"the generated constant is absent ⇒ compile error"* IS A WRONG CLAIM, NOT A MISSING CLAUSE — it was false in this bundle as written and is replaced, not softened.** The C++ guard works because `#ifndef INTEROP_TYPED_ACCESSOR_ARM` → `#error` is **independent of the value expression**: the spurious-hit fixture replaces the announced value with a literal, and the guard is still there to fire. The Java implication has no such surviving dependency — it holds only while the source still *references* the generated constant, which is exactly the reference that fixture replaces. Remove the execution from `pom.xml` and the generated constant simply has **no consumer**: `InteropCounterparty.java` compiles, the `hello` announces a truthful-looking integer, and every C-12 check passes with no arm behind it, while `quickstart.md` Step 4 asserts ⛔ *THE BUILD MUST FAIL*. **Required instead**: `InteropCounterparty.java` MUST carry an **unconditional compile dependency on a generated marker produced only by the arm's own maven execution** — a generated type or member the source references **outside** the announced-value expression, so that removing that execution fails `javac` **whatever the value expression has been rewritten to**. ⚠️ **This is a condition on the mechanism, not a diagnostic literal**: no compiler output is quoted, and the ⛔ against pinned diagnostic strings is untouched. ⚠️ **Ask what ELSE satisfies *"the hello carries `typed_accessor_arm`"*** — on the Java side the answer was *a literal, with the arm gone*; the marker dependency is what removes that answer, and the fixture that proves it is `quickstart.md` § *Spurious-hit arms* (replace the value with a literal **and** remove the execution ⇒ the maven build MUST still fail). ⛔ **Not a Dockerfile `LABEL` and not a hand-maintained constant** — both are decoupled from the assertion, so removing the arm leaves them intact and truthful-looking. ⚠️ **This is a BUILD-TIME attestation and is NOT the deleted `accessor_witness` seam**: it says the arm executed, and says nothing about *runtime* typed-accessor invocation, which stays recorded as structurally unwitnessable — it is not a fourth artifact-level observable of that invocation, and the settled decision is not reopened. ⛔ **Completion rule**: this clause's closed-inventory entries land in the **same edit** — `contracts/readback-jsonl.md` § *Conformance obligations* + § *Witnesses this contract requires* (**C-12**) and `quickstart.md` Step 4 (a forced-miss arm for the absent announcement **and** a spurious-hit arm for the hand-written literal). It deliberately gets **no** E-*/W-* obligation: `contracts/witness-evidence.md` gates the committed **evidence artifact**, and this is a **runtime peer-capability** gate, which is C-*'s population. ⛔ **AND THE ARM IS NOT IN FR-021's MATRIX AT ALL** — do not reason about it through that matrix. `CONFIG_TO_PRESET`'s `linux-clang-*` values map the **fixpp** build (its own source comment says *"config name → fixpp build preset dir"*); `run_interop_cell.py` consumes a **prebuilt** counterparty via `INTEROP_QFC_COUNTERPARTY_BIN`, and `interop-matrix.yml` `docker cp`s that binary out of the published image rather than rebuilding it, so the counterparty is never rebuilt per configuration. ⛔ **Do NOT repair a red arm by loosening the matcher to "the build failed"** — that reinstates the spurious-hit hole and would be invisible; derive that toolchain's pattern from the host instead. ⚠️ *A forced MISS cannot catch a spurious HIT*: a typo, a missing include or a wrong namespace makes the mutated build fail for a reason that has nothing to do with the schema, and a **snippet is strictly worse on this than the real file** (a snippet missing `#include <quickfix/fix44/NewOrderSingle.h>` fails "correctly" for entirely the wrong reason). ⚠️ **No match-count check on the mutation itself is required** — in this arm's polarity a rewrite that matched nothing leaves the source unmutated, so the "mutant" **compiles** and the assertion *"the mutant does not compile"* goes RED on its own. ⭐ **AND THE MUTANT NEEDS ITS OWN CONTROL — THE MUTATED FIELD TYPE MUST EXIST ON *SOME* FIX 4.4 MESSAGE.** ⚠️ *Ask what ELSE produces the expected diagnostic*: a **nonexistent** type does. The compiler reports the unknown type and then, **cascading from it**, no viable `get` overload naming that identifier — which satisfies the match condition stated above while **no schema check is exercised at all**. That is not hypothetical; it left the C++ arm GREEN. So the arm MUST assert the control: the chosen field is a real FIX 4.4 field, declared on a message **other than** the mutated one. `quickstart.md` § *Spurious-hit arms* carries it on the compile-time arm row, in the same edit. ⛔ **AND AN UNRECOGNISED TOOLCHAIN FAILS THE ARM CLOSED.** A host toolchain for which the derivation above yields **no** diagnostic pattern MUST fail the arm — never default to *"the build failed"*, never to *"any failure is the expected one"*. That default is this row's own spurious-hit hole reached by the absent-operand route instead of by loosening, and it is the same rule the capability-minimums row states for a missing minimum: **an absent operand is a failure, never a pass.** ⚠️ **Stated per-site coverage limit**: **one** mutated `(Message, Field)` site per language. FR-003b(a) binds *every* declared typed read, and the **positive** direction covers all of them by construction (the file compiles); the **negative** arm is an anti-vacuity probe of the mechanism, and one site suffices because the absence of a generic `get` is **structural, not per-message** — there is none on `FieldMap`, on `Message`, or on either generated class. ⚠️ **That argument is ENGINE-PIN-BOUND**: on any re-pin of either vendored engine, re-check the inherited overload set, generic fallbacks and field-type conversions before relying on one site. Parameterizing the mutation over the script's typed-read declarations would remove the limit and is **not** required. ⛔ *"The build MUST fail"* with no named mechanism is scored **absent** under this file's round-2 acceptance rule. ⚠️ The **vendored engines stay unpatched**, so FR-023 holds; the arm binds our call site, not the engine's interior | FR-003b · FR-018 · SC-003 |
| `library/tests/interop/support/` (**NEW** — fixpp's readback/sent emitter **and the shared comparator**) | ⭐ **THE WITNESS PRODUCER (data-model §4), named by the RC-A sweep.** The shared comparator pairs the two streams on `(seq_num, direction, occurrence)` and emits the **witness rows** — one per identity 1, carrying `combo_id`, `cell_id`, `config`, `run_id`, `arm`, `authoritative` and `kind` (W-1) — which promotion then persists into `witness_evidence.yaml`. The directory tree in § *Source code* already showed the comparator living here; the obligation column named only the emitter, so §4's producer was unrowed. ⭐ **fixpp is also the THIRD emitter of `contracts/readback-jsonl.md`, and C-7 is THREE-WAY.** fixpp's emitter is bound by every clause of that contract — canonical partition, canonical form and sort order, encoding rule, `hello`/`terminal` — and **participates in the committed cross-language golden fixture**: all three emitters invoked on the same constructed record, compared byte-for-byte against **one** committed expected artifact. ⚠️ **A declaration is not a binding.** FR-006 compares parsed field **sets** and is blind to sort order and escaping, so nothing else guards fixpp's byte-level form; the earlier claim that an unbound fixpp emitter *"goes RED on the first run"* was **false for three of the four clauses it covered** and is deleted. Force the walk-order mutation **in fixpp's emitter** too | C-7 · FR-004 · FR-025 · data-model §2 · §3 · §4 |
| ″ | ⭐ **THE ARM-ATTESTATION PRODUCER (data-model §1a).** fixpp's own `hello` carries `has_validator` — read from **`Session::has_validator_for_test()` on the LIVE session after `open()`**, never inferred from the config that requested the arm — and `dictionary_digest`, over the dictionary the session actually loaded. **data-model §1a is the normative home of both fields and this row does not restate them**; what it adds is the obligation that fixpp's emitter *produce* them. ⚠️ **§1a was the ONE section the RC-A sweep left with no producer row at all**, and the sweep's own warning names the mechanism: a `## N.` heading pattern silently drops `## 1a.`, so the section that was flagged as the one most likely to be missed was then missed. ⛔ **Without this row, E-6 gates a field with no producer and FR-011a's clause is unapplied under THE COMPLETION RULE** — the RC-A shape, in the entity RC-A's own text singles out. ⚠️ A `plan.md` § *Constitution Check* recap mentioning FR-011a is **not** this row: that section is a self-audit, not a closed inventory | FR-011a · E-6 · SC-009c · data-model §1a |
| `library/tests/interop/conversation/conversation_script.yaml`, `phase-9-harness/quickfixj/.../InteropCounterparty.java` | ⭐ **C-11 — the LIVE-path charset arm** (`contracts/readback-jsonl.md` § *C-11*). The script declares `EncodedTextLen(354)`/`EncodedText(355)` on step `B-05` with a value containing byte `0xff`; the QuickFIX-J readback's `value_b64` for path `355` MUST equal the base64 of the wire bytes on C3/C4. ⚠️ **Census-neutral** — existing step, same `(seq_num, direction, occurrence)`, no completeness key added. ⚠️ The synthetic C-7 fixture **cannot** discharge this: it never enters QFJ's decoder. ⚠️ **Two vacuity closures travel with this arm** (`contracts/readback-jsonl.md` § *C-11*): the `value`/`value_b64` decision is made on the **ISO-8859-1 re-encoded bytes**, not on the decoded `String` — under ISO-8859-1 `0xff` decodes to the valid char `U+00FF`, and an implementer classifying on the string would write `value` and pass without ever exercising the b64 path; and `EncodedTextLen(354)` is **derived from the value's octet count, never a literal** — an inconsistent LENGTH/DATA pair is rejected by the validation-on arm before any readback exists. | C-11 · C-7 · FR-004 |
| `.specify/decisions/089-quickfix-interop-conversation-verify.md` (**NEW**) | ⭐ **THE PRODUCER OF STEP 4's PER-ARM BASELINE RECORD — RC-A applied to the baseline evidence itself.** `quickstart.md` § *Step 4* rule 3 requires every arm to be run against the **unmutated** tree and confirmed GREEN there before its RED is trusted, and `tasks.md` T106 requires that result *"recorded"* — with **no named recipient**, which is exactly the RC-A shape this table's preamble describes (an obligation schemad, constrained and armed while no artifact holds it and no command produces it). **Producer**: `/speckit-verify`, mandatory after `/speckit-implement`, which already emits this record and already carries a per-cell arm table. ⛔ **SECTION NAMED, because a second producer already writes to this file**: the baseline entries land under `## Step 4 arm baselines`, distinct from the `## Completeness` section `tasks.md` T110 writes — two producers sharing one file with no agreed location is how one of them silently overwrites the other. **Content**: one entry per arm of **all three** Step-4 tables, carrying the arm's identity, its **unmutated-tree GREEN** result, and its RED result **together with the diagnostic that RED asserted** — a bare non-zero exit is not a recorded RED, per rule 2. ⛔ **No count of the arms is written here**; the enumeration is `quickstart.md` § *Step 4*, and an arm with no entry is **unevidenced**, not evidenced by the suite being green — a green suite is evidence about the executable that ran, not about an arm nobody ran | FR-017 · FR-018 · SC-003 |
| `.specify/decisions/089-quickfix-interop-conversation-gateb.md` (**NEW**) | ⭐ **THE PRODUCER OF THE PARENT-SIDE REVIEW DISPOSITION.** § *Project Structure* → **Structure Decision**, consequence 1 states that the parent-side counterparty C++/Java needs an explicit review disposition because no Gate B reviewer sees it by default; **that is its normative home and this row does not restate the reason.** What this row adds is the obligation to **produce** one, which consequence 1 did not: this feature's Gate B record carries the parent-side diff's disposition — reviewed clean, reviewed with findings, or waived with a stated reason — as a named section, and the `/gate-b` brief names that diff as in scope so the reviewer receives it. ⛔ **An absent section is scored ABSENT, not clean**: a gate record silent on the parent-side diff records that nobody looked | FR-003b · FR-025 · § *Structure Decision* |
| `phase-9-harness/quickfixj/.../InteropCounterparty.java` | Assert `org.quickfixj.CharsetSupport.getDefaultCharset()` is **`ISO-8859-1`** at startup and fail loudly otherwise. `value_b64` is only reconstructible because that charset is a total bijection over all 256 byte values; under a non-bijective charset the raw bytes are unrecoverable at application level | C-7 · FR-004 |

> ⛔ **THE RULE FOR THIS TABLE'S LAST COLUMN: *Required by* CARRIES IDENTIFIERS AND SEPARATORS ONLY.** No
> `⛔`, no `MUST`, no `Assert`, no prose — every normative clause belongs in **Obligation**. A clause that
> lands in the reference column **obligates nothing**: the row's own acceptance rule reads the Obligation
> cell, so a producer built from this table never sees it, and the misplacement is invisible to every
> detector this bundle has. ⚠️ **State it as a rule rather than building a third scanner, because the two
> that exist are STRUCTURALLY blind to it**: the round-8 pipe-count sweep passes — the row is well-formed
> and the pipe count is correct — and the round-9 seam detector hunts **sentence** splices, a clause whose
> subject was severed by an insertion, while this is a **cell-boundary** splice in which every sentence is
> intact. Both misplacements this rule was written for were introduced by appending past the final `|`.

⚠️ **The `asan-ubsan` carve-out this paragraph used to carry was deleted, because both of its premises
were false.** It deferred *"the `asan-ubsan` rows already in `cell_results.yaml`"* to *"a filed issue"*;
measurement found **no such rows** (that file is `config: normal` throughout) and **no such issue**.
⛔ **`spec.md` FR-021a is the normative home** of what replaced it — the retired label survives only in
**prescriptive** artifacts, which this table's rows correct, and the two evidence cells that genuinely
**overclaimed** UBSan coverage are **corrected, not exempted**. This paragraph points; it does not
restate which artifacts fall on which side.

---

## Gate A

- Round 1 applied 2026-09-10: Codex P1=9 P2=6 P3=1; Opus post-judging P1=12 P2=9 P3=3; rewrite addresses root causes #A #B #C #D #E #F. Reviews: research/reviews/codex_089-quickfix-interop-conversation_gate_a_review.md, research/reviews/opus_089-quickfix-interop-conversation_gate_a_adversarial_review.md.
- Round 2 applied 2026-09-10: Codex P1=9 P2=9 P3=2; Opus post-judging P1=10 P2=10 P3=4; rewrite addresses root causes #G #A #B #C #E #H #I. Reviews: research/reviews/codex_089-quickfix-interop-conversation_gate_a_2_review.md, research/reviews/opus_089-quickfix-interop-conversation_gate_a_2_adversarial_review.md.

> ⚠️ **The round-2 tally was taken against a partially-moving target, and must not later be read as a clean
> measurement.** The round-2 Codex review began at **11:46:38** while the round-1 rewriter was still
> writing two of the nine bundle files — `quickstart.md` (**11:47:07**) and `plan.md` (**11:47:17**), i.e.
> 29 s and 39 s *after* launch. The other seven files were stable (11:36:52 → 11:46:17, all before launch).
> The judge re-read both raced files in full after the race and every finding it carried forward is judged
> against the post-race bytes, so the dispositions stand on their own evidence — but the *tally* spans two
> states of the bundle. One sub-claim (`plan.md`'s duplicate `checklists/` entry) is permanently
> **indeterminate**: the intermediate state is unrecoverable, because the bundle files are uncommitted
> working-tree modifications and `git show HEAD:` yields the pre-rewrite round-0 text.
>
> ⚠️ **The same condition holds for round 3** — the bundle is still uncommitted working-tree state, so a
> round-3 review can be raced the same way and its findings would be equally unrecoverable. **Record every
> bundle file's mtime immediately before launching the round-3 review**
> (`stat -c '%y %n' specs/089-quickfix-interop-conversation/{,contracts/,checklists/}*.md`) and compare
> against the review's start time, so a raced finding is identifiable rather than argued about
> afterwards.

- Fresh loop round 3 reviewed 2026-09-10 (bundle `83a1157f`): Codex P1=3 P2=2 P3=1; Opus post-judging **P1=4 P2=2 P3=2**. **Loop EXHAUSTED** — both rewrites spent. Reviews: `research/reviews/codex_089-quickfix-interop-conversation_gate_a_6_review.md`, `.../opus_089-quickfix-interop-conversation_gate_a_6_adversarial_review.md`.
- **Post-exhaustion hand-edit (user decision 2026-09-10): fix the listed items, then re-review with Opus only** — Codex is not re-run, having missed findings in every round and having declared correct the very clause it would author. Eight items, two subsystems:

| Item | Fails toward | Fix | Where |
|---|---|---|---|
| Java matcher rejected the correct failure | RED | matcher is now a **condition + per-toolchain recipe**, not a literal | `spec.md` FR-003b · `plan.md` compile-arm row · `quickstart.md` Step 4 |
| **C++ matcher rejected CLANG's correct failure** | RED | clang needs a **two-line** match — the field identity is **absent from its error line** | same three |
| the instrument was proven on **g++ + javac only**, for a 3-toolchain matcher | — | all three now run. ⚠️ **The reason recorded here in round 7 was inverted and is withdrawn** (round 8): the *"every config is a `linux-clang-*` preset"* inference is about the **fixpp** build and says nothing about this arm's compiler. The defect is simply that an instrument was proven on a **subset** of the toolchains its matcher claimed | `spec.md` Clarifications |
| pairs backed by **superseded retries** | **GREEN** | both references `authoritative: true`; conformance pairs must name the slot's selected run | `data-model.md` §10 · E-7 |
| pair `verdict` **carried, not derived** | **GREEN** | sets and dispositions extracted from the referenced artifacts; `verdict` computed | `data-model.md` §10 · E-7 |
| **two conformance pairs for one slot** | **GREEN** | exactly one authoritative conformance pair per `(cell_pair, config)` | `data-model.md` §10 · E-7a |
| Witness entity omitted `kind`; FR-015c omitted `kind` + `authoritative` | — | both added, with a missing-field fixture | `data-model.md` §4 · `spec.md` FR-015c · W-1 table |
| the RC-A sweep's *"two further unrowed producers"* count was false | — | **count deleted, not amended** — a replacement count is the next round's finding; §7 recorded as a hand-authored exception | `plan.md` |

⚠️ **RED fixtures added, some of them spurious-hit arms** — a forced-miss arm cannot catch the hard-coded verdict, the fabricated sets or the duplicate slot, because each reports the same shape a correct result does. ⛔ **Enumerated, not counted** (`contracts/witness-evidence.md` § *Proof obligations for Level 1* is the inventory); round 8 added two more and a count written here would already be stale.

- Round 8 (Opus-only review) applied 2026-09-10: P1=4 P2=3 P3=3; rewrite by an independent agent after the orchestrator's hand-edit was found to have an inverted premise. Review: `research/reviews/opus_089-quickfix-interop-conversation_gate_a_7_review.md`.

⭐ **The structural change round 8 makes, and why it is not a fourth string repair.** The compile arm's
diagnostic matcher had been authored wrong **three consecutive times** — a plausibility literal, then the
wrong javac diagnostic family, then an inverted toolchain premise — each repair replacing a wrong claim
with a fresh claim that the next round falsified. Round 8 therefore **stops maintaining a toolchain table**:
§ *External obligations* → the typed-accessor compile-arm row now states the arm's **execution host** and
the recipe for deriving its compilers at build time, and `spec.md` FR-003b and `quickstart.md` Step 4 point
there instead of restating a list. ⚠️ **A host is structural; a toolchain list is a measurement and rots on
every image rebuild.** Same move `FR-023a` already makes for the disk figure and the RC-A sweep count.

- Round 9 (Opus-only review) applied 2026-09-10: P1=3 P2=5 P3=2; plus the user decision that the counterparty is rebuilt and republished BEFORE 089's CI, with the compile arm gating that publish. Review: research/reviews/opus_089-quickfix-interop-conversation_gate_a_8_review.md.

⭐ **What round 9 changed, and why it is a different subsystem from rounds 5–8.** The compile-arm *matcher*
was not the defect this round — round 8's structural move (state the host, derive the toolchains) held. The
defects were in the **pair/gate subsystem** rounds 7–8 built: **E-7c**, as written, contradicted
`data-model.md` §10's supersession **iff** in the same commit and would have put the committed schema-check
ctest permanently RED from the first retry onward; `plan.md`'s promotion row carried a **mangled splice**
that re-attributed E-7 evaluation and the pair write to `quickstart.md` Step 4; and §10's own gate
inventory, the duplicated fixture enumeration and the checklist's re-derivation had all gone stale against
obligations added by the very commits that left them unchanged. ⚠️ **Four of the five were one rule stated
in two places and repaired in one** — so the countermeasure applied is the one that fixed the matcher:
**one normative home, everything else points at it.** `contracts/witness-evidence.md` is the home for the
E-* obligations and their fixtures; `plan.md`'s fixture row and `data-model.md` §10's *Gated by* list now
point rather than copy, and a partial copy of the fixture inventory was **deleted rather than extended**.

⭐ **And the round-9 user decision, which is the answer to the review's one open scope question.** The arm
has no pre-merge CI host and the review declined to choose between *stating that* and *building one*. The
decision: **the counterparty is rebuilt and republished before 089's CI runs, and the compile arm gates
that publish**; ⛔ no new PR-time CI workflow is created. Three things make it real rather than nominal and
all three are in § *External obligations* → the typed-accessor compile-arm row: the arm must **execute
under the host's own invocation** (the host runs `cmake -S`/`cmake --build` and `mvn … package
-DskipTests`, and **no `ctest` and no surefire execution at all**); a wrong outcome must **fail that
invocation** (`try_compile` only sets a variable); and the image must **record that the arm ran**, or
pinning a pre-arm digest satisfies FR-016b while attesting nothing. ⚠️ **The residual is stated, not
implied**: the verdict lands at counterparty-publish time — before 089's CI, and **not on any pull
request** (§ *Project Structure* → **Structure Decision**, consequence 3).

- Round 10 applied 2026-09-10: Codex P1=4 P2=1 P3=0; Opus post-judging P1=5 P2=2 P3=1. Reviews: research/reviews/codex_089-quickfix-interop-conversation_gate_a_7_review.md, research/reviews/opus_089-quickfix-interop-conversation_gate_a_9_adversarial_review.md.

⭐ **What round 10 changed, and the shape of the change is the point.** Six of the seven fixes are
**propagation and deletion**, and for four of them the correct text already existed elsewhere in the
committed bundle. The two genuinely new mechanisms — the materialized required-version operand and the
Java marker dependency — are written as **conditions on a mechanism**, never as a value and never as a
claim about an outcome, because that is the authoring shape that produced *replaced-with-new-defect* twice
in round 8. What was deleted rather than corrected: a cardinality claim at three sites (falsified by E-7b),
four gate rosters in three files (converted to pointers at
`contracts/witness-evidence.md` § *Obligations*), and §10's *Gated by* inventory **together with the
maintenance duty it carried and broke four times in one commit**. ⚠️ **The two structural repairs behind
them**: E-7b is now scoped `authoritative: true` on the pair and both referenced runs — the same predicate
E-7 already carries, at the host that re-evaluates it — and **data-model §11 now states as a RULE whether a
control run can be superseded**, which the bundle had left to be inferred from *"it is not a retry"*.

### Round 10 — disagreements

*Findings declined, downgraded, or extended beyond their counter-proposal, with what decided each.*

| Item | Disposition | Why |
|---|---|---|
| **`plan.md`'s promotion-row reference cell (`:623` in the reviewed bundle)** | ⬇️ **DOWNGRADED P1 → P3 by the judge, against the orchestrator's over-call** — and applied as a P3 tidy in the same edit as the real P1 one row up | The orchestrator flagged it as *"Codex MISSED, worse than filed"*. The judge read the row and disagreed: the misplaced text is a **pointer note explaining a deletion**, not an obligation, and cell 1 already carries both operative statements — the full E-*/W-* host mapping and an explicit ⛔ *NO FIXTURE IS ENUMERATED IN THIS ROW*. **Nothing is lost to a reader of the Obligation column**, so filing it as a second P1 would have inflated the count. Recorded because a downgrade against the orchestrator is exactly what a judge is for |
| **`quickstart.md` § *Forced-miss arms*, the C-12 absent-announcement arm** | **NOT repointed** at the capability-minimums artifact, though the review listed it among that operand's consumers | Checked cell by cell: that arm forces an **absent** `typed_accessor_arm` and its Expect column names FR-016b's digest-pin contrast. It references no minimum, so there is nothing to repoint; the *older-than-required* half of C-12 lives in `contracts/readback-jsonl.md` § *Witnesses this contract requires*, which **is** repointed. Adding a new *"older than required"* arm here would be scope expansion, not a repair |
| **`data-model.md` §10's *Gated by* roster** | ⬆️ **EXTENDED past the filed findings — converted to a pointer, not left standing** | The judge did **not** file it: it is currently accurate, having been corrected in the reviewed commit. But it is a self-described **copy** carrying a maintenance duty — *"any new E-\* over this entity is added here in the same edit"* — that the same commit broke four times, and the round's one instruction is *pointers and deletions, not corrected copies*. Leaving an accurate copy in place would have preserved the mechanism while removing only its instances. **A pointer cannot undercount** |
| **`contracts/witness-evidence.md` § *Obligations* and § *Proof obligations for Level 1*** | **HEADINGS ADDED** — a structural change the findings did not ask for | Six of this round's pointers cite *"§ Obligations"* as the E-\* family's normative home, and that section **had no heading**: the obligations table sat unheaded under `### ⛔ Two artifacts, and they must not be one`, and *"Proof obligations for Level 1"* was **bold prose**, already cited by name from `plan.md` and elsewhere. Without the headings every pointer written this round — and several written before it — resolves to nothing. ⚠️ Measured, not assumed: `grep -n '^#'` over the file before editing |
| **`plan.md` § *External obligations*, the schema-check row** | **LEFT enumerating E-7a / E-7b / E-7c** while four other rosters became pointers | It is not a roster. Under **RC-B** that row is the obligation *imposing the work* on a file outside the bundle, and an obligation must name what it obliges. The rule now stated at § *Obligations* says so explicitly, so the exception is written down rather than left to be re-litigated |

### Round 8 — disagreements

*Findings whose counter-proposal was declined or altered, recorded with the measurement that decided it, so
a later round does not re-apply the rejected form.*

| Review finding | Disposition | Reason |
|---|---|---|
| **P2 `spec.md:170`** — *"correct the g++ string to the measured one (curly quotes, trailing ` const`)"* | **COUNTER-PROPOSAL DECLINED; the finding is upheld and fixed differently** | ⛔ **The review's replacement literal is also wrong, for the same structural reason its own recommendation names.** Measured here on the pinned tree: g++ appends ` const` to the printed call signature **iff the mutated call's object expression is const-qualified**, and omits it otherwise — invariant across `-std=c++17`, `c++20`, `c++23`. The review's `grep -F` → 0 therefore measured **its own TU's receiver constness**, not g++'s behaviour; the bundle's pre-existing string is *exactly correct* for a non-const receiver under `LC_ALL=C`, which is how the arm would naturally be written. Adopting the review's literal would install the **fourth** wrong claim, from the finding written to stop the third. ⭐ **Fix applied instead**: the pinned illustration is **deleted** from both `spec.md` sites; what survives is the polarity (control exits 0, mutant exits non-zero — re-measured here on g++ 13.3.0, clang 22.1.2, javac 21.0.12, reading process exit status, not a filtered log), the derivation recipe, and the **two free variables** — locale and receiver constness — that make any literal under-determined. The locale hazard the review names is kept, as a recipe line |
| **P1 (toolchain premise)** — supporting sub-claim *"no `publish-counterparties.yml` exists in either the parent or the library submodule"* | **SUB-CLAIM FALSE; the finding's conclusion stands on other evidence** | ⛔ **`.github/workflows/publish-counterparties.yml` EXISTS in the parent and is tracked** (`git ls-files` confirms; last touched by `76a4d50`). It builds `ci/counterparties.Dockerfile` via `docker/build-push-action` on `ubuntu-24.04`, passes **no** `CMAKE_CXX_COMPILER`, and triggers on pushes to `main` touching `phase-9-harness/quickfix-cpp/**`. ⭐ **This strengthens rather than weakens the host statement**: the arm is genuinely exercised on change, once per image publish — had the review been right, it would run only locally. The row is therefore written from the workflow's **trigger**, not from an absence claim. The rest of the chain re-verified independently: the builder stage installs `g++` and no clang (`grep -in clang` → nothing); the counterparty `CMakeCache.txt` records `/usr/bin/c++` with `gcc-ar-13`; `CONFIG_TO_PRESET`'s own comment scopes its `linux-clang-*` values to the **fixpp** build; and `interop-matrix.yml` `docker cp`s a **prebuilt** binary rather than rebuilding it, so the arm is not in FR-021's 32-run matrix |
| **P1 (toolchain premise)** — *"keep the clang row, re-labelled as the row a local clang-default build needs"* | **ALTERED** | ⛔ **A re-labelled row is still a maintained toolchain list**, which is the shape the review's own closing recommendation rules out and which has now failed three times. What is kept is the part that is **structural rather than a measurement**: that the toolchains do not carry the field identity in the same place, so the matcher must **match the whole diagnostic and derive where the identity landed** — clang's note-only placement is given as the live illustration of why, not as a row to keep in sync. ⛔ Per the review's own instruction, the deleted `linux-clang-*` sentence was **not** replaced with its inverse |
| **P1 (E-7b)** — the proposed obligation text | **ADOPTED, with an anti-vacuity clause added** | E-7b's forced-miss polarity is correct (the defect *is* an absence), but *what else satisfies its condition* had to be asked: a control pair naming one run twice, or two runs of the same arm. Checked — **E-7 is `kind`-agnostic** (it constrains `off_run_id ≠ on_run_id`, opposite `has_validator`, and `kind` matching *both* referenced runs, resolving against the ledger *"including `kind: validator-positive-control` rows"*), so both degenerate constructions are rejected. ⛔ **But E-7 fires at promotion and E-7b lives in the committed check**, so E-7b **restates** distinctness and opposite `has_validator` in its own text rather than leaning on E-7 — deferring would reproduce the very host split **E-7c** was added to close. ⚠️ A second producer gap surfaced while checking this and is fixed in § *External obligations*: the promotion row's pair trigger was worded over `(combo_id, config)` slots, which control runs do not occupy |
| **P2 (temporal `authoritative`)** — hosted as *"an E-7a clause (or E-7c)"* | **ADOPTED as E-7c**, a separate obligation | Folding it into E-7a would put a **pair-reference** predicate inside the **cardinality** obligation, and E-7a's own fixtures (empty section, 15-of-16) do not range over references. A distinct id also lets the fixture assert the contrast that *is* the arm — **E-7c RED while the committed check's pre-existing reference-resolution clause stays GREEN** (a superseded row still resolves) — which a merged clause could not express. ⚠️ **Round 9 correction, recorded in place**: this cell originally named that contrast *"E-7c RED while promotion-time E-7 stays GREEN"*, the phrasing `contracts/witness-evidence.md`'s own E-7c fixture ⛔s in the same commit — E-7 carries the same `authoritative: true` predicate and would go RED on the fixture too, so it names a contrast that instantiates nothing. The rule was stated in two files and repaired in one. ⚠️ **The forbidden phrase survives in this cell only as a STRUCK QUOTATION**: a repo-wide grep for it hits exactly here and `contracts/witness-evidence.md`'s prohibition, and neither is a live use — do not re-file it |

### Round 9 — disagreements

*Findings whose counter-proposal was declined, altered or extended, recorded with the measurement that
decided it, so a later round does not re-apply the rejected form.*

| Review finding | Disposition | Reason |
|---|---|---|
| **P2 #4** — supporting sub-claim *"per this repo's own recorded trap, a push to `main` can cancel that run's signal"* | **SUB-CLAIM FALSE; the finding's conclusion stands on other evidence, and the caveat is NOT carried into the bundle** | ⛔ Measured: `publish-counterparties.yml` sets `concurrency: {group: publish-counterparties, cancel-in-progress: **false**}`. A following push to `main` **queues**; it does not cancel. The recorded trap is about `tier1.yml`'s *per-ref* group with `cancel-in-progress: true`, a different workflow. ⭐ Repeating the parenthetical would have installed a false claim inside the fix for a false claim. What **is** written instead is the **condition** the design depends on — *the publish workflow's concurrency group MUST NOT cancel in progress, because a cancelled publish is a missing verdict that looks like nothing* — which cannot rot the way the observation would |
| **P2 #4** — *"the publish MUST be gated on the arm"* (user decision element 2) | **ADOPTED, but as a DERIVED CONSEQUENCE rather than a new mechanism** | `publish-counterparties.yml` builds and pushes in **one** `docker/build-push-action@v6` step with `push: true` — one `buildx` invocation, so a failed build never reaches the push. *"A RED arm publishes no new digest"* therefore follows from element 1 (the arm fails the **image build**) with nothing added to the workflow. ⚠️ Two conditions keep the derivation true and are stated as conditions: the arm must fail the **builder stage**, and the publish must not be cancellable |
| **P1 #1 / P3 #10** — the E-7c fixture: *"the run is superseded and the pair is NOT demoted"*, plus *"the retry's replacement authoritative row is present"* | **ADOPTED, with a THIRD condition added** | ⛔ **Anti-vacuity: what ELSE reddens on that artifact?** The review's two conditions close E-1c's co-fire. They do not close **E-7a**'s: if the producer wrote a *replacement pair* and merely forgot to demote the old one, the slot carries **two** `authoritative: true` conformance pairs and E-7a co-fires, so E-7c is not isolated. The fixture therefore also requires that **no replacement pair was written** — the producer re-promoted the run and never rebuilt the pair — and asserts **E-1c and E-7a both stay GREEN**. Recorded because the added condition is not the review's |
| **P2 #6** — *"delete the duplicated fixture enumeration"* | **ADOPTED and EXTENDED to a second copy in the same row** | The row contained a **second** partial inventory two sentences earlier (E-7's three fixtures and E-7a's two, dated *"this round's additions"* against a round that is no longer this one). The review filed only the trailing copy. Deleting one while leaving the other would have made the row contradict its own deletion note. Both are gone; the *Fixture host follows the checker* mapping was extended to E-7b and E-7c, which is a host assignment, not a copy |
| **User decision element 3** — *"the image must record that the arm ran, as part of the existing capability handshake"* | **ADOPTED as `hello`.`typed_accessor_arm` / C-12, and it required AMENDING a ⛔ in `contracts/readback-jsonl.md`** | That file's § *Witnesses this contract requires* carries an explicit ⛔ that FR-003b's typed-accessor guard has **no entry here and must not be given one**. C-12 is not that entry and the amendment says so normatively: C-12 witnesses that **the peer's BUILD was gated by the arm**, a peer-capability fact in C-2's population — **not** typed-accessor *invocation*, which stays structurally unwitnessable. ⛔ The settled *"no fourth artifact-level observable"* decision is **not** reopened. ⚠️ Recorded here because amending a ⛔ silently is how a settled decision gets reopened by accident |
| **Still-missing #4** — *"the clang note-only claim is restated in three files; if the compile-arm row is touched anyway, collapse the other two into pointers"* | **DECLINED for this round** | Filed by the review as P3 and **outside its own P1=3/P2=5/P3=2 tally**. The claim carries a `today` hedge, the bundle's rule is *"illustrations, never operands"*, and `plan.md`'s *"one place"* assertion is explicitly scoped to the **host**, which this is not. Collapsing it would extend this edit into `spec.md:489` and `quickstart.md:237`, text no counted finding reaches — and every previous edit in this bundle that reached past its brief introduced a defect. ⚠️ If a later round files it as a counted finding, do it then |
| **P1 #1, second order** — *nothing in the review, and nothing in the bundle, obliged the producer to DEMOTE A PAIR when a referenced run is superseded* | **ADDED — not a counter-proposal, and the scoped E-7c is unsatisfiable-in-reverse without it** | ⛔ **Found while checking the round-9 fixture against its own producer.** `data-model.md` §10's writer clause stated **one** trigger — write the new pair authoritative, demote the prior pair for that slot. The E-7c fixture forces the *other* case: a run superseded, **no replacement pair written**, the old pair left `authoritative: true`. Under the one-trigger clause that producer had discharged every stated duty, so E-7c would have reddened a **compliant** producer — the same unsatisfiability shape as P1 #1, one level down, authored by the fix for P1 #1. ⭐ **Fix**: §10's writer clause now states **two** triggers, trigger (2) being demotion of every pair referencing a superseded run **including when no replacement pair is constructed**; `plan.md`'s promotion row **points** at §10 rather than restating them. The `iff` states the invariant, trigger (2) is the act that maintains it, E-7c checks it held |
| **User decision element 3, mechanism** — the review proposed no mechanism; the brief named the surface only | **AUTHORED HERE, and recorded as authored** | The `#ifndef INTEROP_TYPED_ACCESSOR_ARM` → `#error` binding, the *value must come from the asserting build step* rule, the rejection of a Dockerfile `LABEL`, and the **stated limit that this closes DRIFT (the arm removed, the attestation surviving) and not FORGERY (a deliberate source-level `#define` alongside the removal)** are all this round's, not the review's. ⚠️ Recorded because an unattributed mechanism reads as previously agreed, and the next round cannot then tell what was reviewed from what was invented |
| **Still-missing #3** — *no `quickstart.md` Step-4 demonstration for E-7b or E-7c* | **NOT ACTIONED, as the review itself recommends** | The bundle's own rule is that a Step-4 demonstration is **not** a gate; the fixtures that matter are in `contracts/witness-evidence.md` § *Proof obligations for Level 1* and both exist. Recorded so the omission stays a decision |

### ⛔ The round-2 acceptance rule, recorded because it is the rule this rewrite was written against

> **Produce the artifact, not a sentence about the artifact.** A clause of the form *"X is declared /
> enumerated / specified / named"* is scored as **absent**. Round 1 replaced four wrong claims with claims
> that the work had been done — FR-015d's *"is therefore itself declared, in the spec"* (no step table),
> `readback-jsonl.md`'s *"this contract enumerates the reconciliation"* (undecided, and self-contradictory
> on tag 1156), FR-014b's *"a persisted evidence location outside every build tree"* (no location), and
> this table's *"a named promotion step"* (not named). Round 2's artifacts, so they can be pointed at:
> `spec.md` § *Conversation census* · `contracts/readback-jsonl.md` § *THE CANONICAL PARTITION* ·
> FR-014b's `$FIXPP_INTEROP_EVIDENCE_ROOT` and `promote_interop_evidence.py` · `quickstart.md` Step 4's
> observable column · `contracts/readback-jsonl.md` § *Canonical form*'s parsed-path sort rule.

### Round 2 — disagreements

*Findings the judge marked Disagree / Downgrade / counter-proposal-rejected, recorded with the reason so a
later round does not re-apply the rejected form.*

| Finding | Disposition | Reason |
|---|---|---|
| **Codex 14** — the bundle contradicts the live disk measurements | **counter-proposal REJECTED**; finding confirmed at P2 | Codex asked to *replace* `83 G`→`93 G` and `34 G`→`29 G`. That reproduces the defect it reports, in the section titled *"The instrument that fails toward clean"* whose entire subject is a number that reports comfort it cannot keep true. The decisive evidence is not that the figures were wrong but **how fast**: `plan.md` was rewritten at 11:47:17 and its figures were false **within the hour**, because ~9.8 G of `_packaging_tests` scratch was reclaimed after they were written. Patching them **re-arms** the section. **Applied as DELETION**: the condition, the `df`/`du` recipe, and exactly one dated historical pair kept as motivation and marked as never an operand |
| **Codex 20** — stale FR count and two duplicated headings | **two sub-claims DISAGREE (false)**; the count half confirmed at P3 | `research.md`'s duplicate `R-4a` heading is **false** — `grep -c '^## R-4a'` returns 1; the `:194` occurrence is a prose forward-reference. `plan.md`'s duplicate `checklists/` entry is **indeterminate** (raced file; the intermediate state is unrecoverable) and the bundle gets no credit and Codex no charge. For the count itself the fix is **deletion, not correction** — a hand-maintained count is the same rot class, and correcting it schedules the next round's finding |
| **Codex 9 / round-2 #17** — Article IX §4 | **scoping disposition UPHELD, not reopened**; verdict hygiene fixed | Article IX §4 reads *"Static analysis — **Tier 1**:"* and Tier 1 does not build the counterparties. **No parent-repo clang-tidy / clang-format / cppcheck / IWYU tasks are added.** What was wrong was the Constitution Check row rendering **PASS** over a clause the same plan recorded as *"disposition required, not a pass"*. Only the row changed |
| **Codex 4** — the divergence probe contradicts FR-010 and has no run model | **DOWNGRADED P1 → P2** | Substance confirmed, but the failure direction is **loud**: seeding the probe into the normal script makes FR-010 fail on all 32 runs immediately. Two fields (`kind`, `expected_verdict`) plus placing positive-control executions outside the 32 |
| **Codex 16** — C-9 contradicts the closed record grammar | **DOWNGRADED P2 → P3** | Real, but it is a scope word and the contradiction is visible in a schema table an implementer must read anyway. Applied: C-9 scoped to `sent`/`readback`, `script_step_id` dropped from receiver records |
| **Codex 18** — the script digest is not reproducible as specified | **DOWNGRADED P2 → P3** | Smaller than filed. Once the metadata handoff exists the **shim** is the single computer of record, so no cross-language digest *agreement* is needed and the algorithm choice is nearly free. What mattered was **who computes and who verifies** — applied as: shim computes, each side **recomputes over the file it opened**, shim compares. Algorithm named (lowercase-hex SHA-256; OpenSSL is already linked on the C++ side and `MessageDigest` is stdlib on the Java side, so R-3's *"no new dependency"* is preserved) |
| **Codex 12** — the per-cell dictionary scope has no structural implementation seam | **counter-proposal NARROWED** | First branch only: four dedicated conversation templates named by the eight new cells. The *"narrowly scoped renderer override"* is **rejected** — the per-cell `config_template` seam already exists and a renderer override is machinery for a problem the harness solved |
| **Round-2 #1** — the correlation key | ⚠️ **The key was NOT invalidated. Only the emission point moved.** | The user's recorded clarification — *"`MsgSeqNum(34)` + direction, using data already on the wire; nothing is injected"* — **survives intact**: tag 34 is on the wire and both engines expose it on the header object *before* serialization (QuickFIX-cpp `Session::sendRaw` → `fill(header)` → `toApp`; QuickFIX-J `sendRaw` → `initializeHeader` → `toApp`). What was wrong was `data-model.md` §3's own *"emitted before transmission"* design statement. **Do not put the clarification back to the user.** Fixed with the two-stage sender record (FR-003a) |

- Round 3 reviewed 2026-09-10 (bundle committed at `18813db6`): Codex P1=2 P2=3 P3=0; Opus post-judging **P1=3 P2=3 P3=1**. Trajectory 12/9/3 → 10/10/4 → **3/3/1**. **7 of 9 round-2 artifacts CLOSED under independent check**, census arithmetic recomputed from the tables (100 keys / 32 slots / 400 rows) and sound. Reviews: `research/reviews/codex_089-quickfix-interop-conversation_gate_a_3_review.md`, `research/reviews/opus_089-quickfix-interop-conversation_gate_a_3_adversarial_review.md`.
- **Loop EXHAUSTED at round 3** — both rewrites spent. Per user decision 2026-09-10 the residual findings were applied as a **hand-edit** (not a third rewrite, not a re-plan): the trajectory was converging and every closure held, so re-planning would have re-derived correct artifacts and put three rounds of settled decisions back at risk of the *"a fix that replaces a wrong claim with a new claim"* class this bundle hit in rounds 1, 2 and 3. A fresh `/gate-a` follows, with the rewrite counter reset.
- Fresh loop round 1 applied 2026-09-10: Codex P1=1 P2=3 P3=3; Opus post-judging P1=1 P2=6 P3=7; rewrite addresses RC-1..RC-4. Reviews: research/reviews/codex_089-quickfix-interop-conversation_gate_a_4_review.md, research/reviews/opus_089-quickfix-interop-conversation_gate_a_4_adversarial_review.md.
- Fresh loop round 2 applied 2026-09-10: Codex P1=2 P2=2 P3=1; Opus post-judging P1=2 P2=2 P3=4; rewrite addresses RC-A..RC-D. Reviews: research/reviews/codex_089-quickfix-interop-conversation_gate_a_5_review.md, research/reviews/opus_089-quickfix-interop-conversation_gate_a_5_adversarial_review.md.

**Hand-edit pass (2026-09-10, post-exhaustion).** Six files. Every change is an addition of a named artifact or a deletion; no settled decision was restated.

| Finding | Fix | Where |
|---|---|---|
| **N-1** [P1] — SC-009a's two operands did not exist as files | Named **both**, with a provenance table forbidding either being generated from the other | `plan.md` § *Project Structure*, § *SC-009a's two operands* |
| **#1** [P1] — fixpp could not source its own join identity | Added the **`INTEROP_FIXPP_*` gtest env block**; absent key ⇒ hard abort and promotion RED | `data-model.md` §1, `witness-evidence.md` E-1b |
| **#2** [P1] — `fix_type` proves a dictionary lookup, not accessor invocation | ~~**User decision**: `accessor_witness`, obtainable only from the object the getter returned~~ ⛔ **SUPERSEDED at fresh loop round 1 — do not re-implement from this row.** The getter returns the **caller's own object**, so `accessor_witness` was synthesizable too. Replaced by a **compile-time** arm; `accessor_witness` is **deleted from the bundle**. See `spec.md` § *Clarifications* → *Session 2026-09-10 (Gate A fresh loop, round 1)* | `spec.md` FR-003b + FR-018, `quickstart.md` Step 4, `plan.md` § *External obligations* |
| **#3** [P2] — validation pair bound to no runs | `off_run_id` / `on_run_id` / `kind` / `expected_verdict`; two **distinct** runs with opposite arms | `data-model.md` §10 |
| **N-2** [P2] — fixpp's emitter bound by nothing | ~~fixpp declared a **third producer**; "both emitters" ⇒ all three~~ ⛔ **NOT CLOSED by that edit — superseded at fresh loop round 1.** The blanket remap keyed on **one** spelling over a document using **five**, so it reached almost none of the normative sites; and the safety argument attached to it was **false**. Replaced by an explicit three-emitter population, a three-way C-7, and per-clause naming | `contracts/readback-jsonl.md` § *THE THREE EMITTERS* |
| **#5** [P2] — stale disk readings survived | **Deleted**, with the re-derivation recipe kept | `research.md` R-1 |
| **#4** [P3] — charset unpinned | `ISO-8859-1` pinned + asserted at startup; live-path arm required | `contracts/readback-jsonl.md` |

### Fresh loop round 2 — disagreements

*Findings, or parts of findings, declined or narrowed, recorded with the reason so a later round does not
re-apply the rejected form.*

| Item | Disposition | Reason |
|---|---|---|
| **Codex's *"fail if the expected source rewrite did not match"* clause on the compile arm** | ⛔ **DROPPED — wrong polarity for this arm** | Check the direction: a rewrite that matched nothing leaves the source **unmutated**, so the "mutant" **compiles**, and the assertion *"the mutant does not compile"* goes **RED on its own**. A vacuous mutant is self-catching here, so a match-count check adds a second failure path for a case the arm already reddens on. ⚠️ This is **not** a general dispensation — in the opposite polarity (an arm asserting a mutated build **passes**) a vacuous mutation is a false green and a match check is mandatory. What *was* missing is the **expected diagnostic**, and that is applied |
| **Codex's `control_runs:` half of the ledger counter-proposal** | ⚠️ **NOT FORCED — the `kind`-scoped route was taken instead; this was the round's one point of latitude** | Codex's premise (*"the ledger cannot fill this gap, control runs are outside the inventory"*) is the wrong reason for a right outcome: the ledger already **declares** `kind: validator-positive-control` and `expected_verdict` — it *tried* to hold them. The defect was that the **slot rule** made those columns dead. data-model **§9 is already `kind`-scoped** (*"exactly one `conformance` run per slot"*, *"a control run sits OUTSIDE the 32-slot inventory"*), so a second section would contradict §9's own model and force E-7 to resolve `off_run_id`/`on_run_id` across **two** populations. One `runs:` section, two discriminators — see data-model §11 § *THE TWO DISCRIMINATORS*. An `occupies_slot` column was also rejected: a second copy of `kind` that can disagree with it |
| **Codex's non-finding: *"the normalized `kind` enum and conditional verdict rules are internally consistent"*** | ⛔ **FALSE — a false CLOSED, and nothing downstream re-checked it** | The *spelling* was normalized; the *satisfiability* was not. §10 required the pair's `kind` to match the `kind` of **both referenced runs**, and **no legal ledger row existed** for a control run to match against — a control row either took a 33rd slot (breaking the 32-slot equality / W-3b) or shared one (breaking E-1c / W-3c), with no third option. So the ledger carried a `kind` value **no row could legally carry**, and E-7 was unenforceable for exactly the pairs carrying `expected_verdict`. Fixed by the `kind`-scoping above |
| **Codex's *"C-11 is present in all FOUR closed inventories"*** | ⚠️ **WRONG COUNT, NO GAP — no action taken** | C-11 is in **three**: `contracts/readback-jsonl.md` § *C-11* / its witness inventory, `quickstart.md` Step 4, and `plan.md` § *External obligations*. It is absent from `contracts/witness-evidence.md`, and correctly so — E-*/W-* is the **evidence** series, not the record-format series. Recorded because a wrong count in a closed non-finding is how the next round re-opens a settled item |
| **Codex's seven negative fixtures for E-7's seven predicates** | ⚠️ **NARROWED to three** | Three discriminate the checker — `off_run_id == on_run_id`; two distinct runs with the **same** `has_validator`; one referenced run disagreeing on `config`/`script_digest`/`kind`. A fixture per clause re-proves the same branch. ⭐ **What was ADDED instead, and Codex did not ask for it**: the **E-7a existence/completeness gate**, because **zero pairs was green** — the *shape* of a pair was constrained and its *cardinality* was not |

### Fresh loop round 1 — disagreements

*Findings, or parts of findings, declined or narrowed, recorded with the reason so a later round does not
re-apply the rejected form.*

| Item | Disposition | Reason |
|---|---|---|
| **Codex N-2 marked CLOSED while Codex #4 said the binding does not reach** | ⛔ **Resolved as NOT CLOSED** — the judge's reading is adopted | Both cannot hold. The *declaration* existed (`readback-jsonl.md`'s three-producer preamble); the *binding* did not, because the remap that carried it was keyed on one spelling. ⚠️ **A false CLOSED is the worse error**, because nothing downstream re-checks it. Applied as a real binding: `§ THE THREE EMITTERS`, a three-way C-7, per-clause naming, and the removal of the remap |
| **RC-2 — *"delete every 'both/two &lt;synonym&gt;' construction"*** | ⚠️ **PARTIALLY DECLINED — applied to the EMITTER count, NOT to the PROCESS count** | RC-2's sweep conflates two different numbers. `readback-jsonl.md` § *Transport* is dispositive: **TWO files, one per emitting process**, because a cell pairs fixpp with **exactly one** counterparty. So *"each side emits both record kinds"* (§ Transport, § Records) and C-8's *"emitted by each of the run's two processes"* are **TWO and correct**; blanket-applying RC-2 would have written *"three sides emit both record kinds"* into the contract — a **new false claim**, the exact class this bundle keeps reproducing. The distinction is now a table at the head of the contract so the next reviewer reads it as deliberate. **THREE** was applied everywhere it belongs: format, bytes, sort order, escaping, the partition, C-7 — plus `spec.md` FR-025 and `quickstart.md` Step 4 |
| **Disk figures inside `plan.md`'s Gate-A *disagreement-record* rows** (Codex 14's `83 G`→`93 G` / `34 G`→`29 G`; Codex 6's build-mount ceiling) | **RETAINED deliberately** — not swept by RC-3 | Those figures appear **inside the record of why a counter-proposal was rejected**. Deleting them deletes the reason and re-opens the decision. They are **history, not operands**, and are marked as such by the row they sit in. ⚠️ The distinguishing CONDITION, not a count: a figure inside a dated *disagreement record* is retained; a figure in any *live* clause is deleted. RC-3's sweep targets *live* claims; every one of those was deleted (`spec.md` § Assumptions, `data-model.md` §8, `contracts/disk-preflight.md`, `plan.md`'s second illustration), leaving **one** dated illustration in `plan.md` § *Disk preflight* |
| **`checklists/requirements.md` lines under dated *"after Gate A round 1 / round 2"* headings** | **NOT rewritten** | They are dated historical records of those sessions, and the file's own precedent is to **strike in place or append**, never to rewrite history. The three-emitter correction is recorded in a **new** dated re-validation section instead |
| **Codex #1's counter-proposal (an opaque receipt type)** | **DECLINED — superseded by the user decision** | The receipt's *serialized value* witnesses nothing (no value can). What it would have closed is a **compile-time** closure, which FR-003b already contained in one sentence — *"it compiles only if the field belongs to that message in FIX 4.4"* — at a fraction of the cost. The compile-time arm is adopted; the receipt type is not |

### Round 1 — disagreements

*Findings the judge marked Disagree / Downgrade / counter-proposal-rejected, recorded with the reason so a
later round does not re-apply the rejected form.*

| Finding | Disposition | Reason |
|---|---|---|
| **Codex 9** — Article IX §4 unresolved despite a PASS verdict | **counter-proposal REJECTED**; the verdict-hygiene half confirmed at P2 | The counter-proposal demanded parent-repo clang-tidy / clang-format / cppcheck / IWYU as required tasks. Article IX §4 reads *"Static analysis — **Tier 1**:"*; Tier 1 is the library's CI tier and does not build the QuickFIX counterparties, which are outside fixpp's Conan graph by design. Demanding four Tier-1 tools over code Tier 1 does not build over-reads the clause. **Fix the verdict, not the scope** — the disposition stands as the correct discharge |
| **Codex 11** — no named test seams for the acceptance criteria | **counter-proposal DOWNGRADED**; substance confirmed at P2 | It asked for an acceptance/test matrix mapping every FR and SC to a concrete test file and case — that is `tasks.md` output, and `tasks.md` is Phase 2, not created at Gate A. Mapping 28 FRs to file names before the tasks pass would be invented precision. The **checkable** half — six contract-mandated witnesses with no arm — is applied in full via `quickstart.md` Step 4 and the contracts' own witness tables |
| **Codex 6** — the disk gate compares two quantities to one threshold | **mechanism REPLACED**; finding confirmed at P1 | Codex's route was *"`min(build, host) ≥ T` cannot express two requirements"*. True but not the hazard: `min(a,b) ≥ T` is **stricter** than either alone, so it fails toward a false RED. The real fail-open is that **R-1 derives the threshold from the host delta while D-1 applies it to the build-mount reading**, which bounds total resident footprint (34 GiB for ASan) — the larger ceiling is the under-checked one. Fixed as two independent named predicates |
| **Codex 7** — the dictionary scope contradicts US1 and FR-001 | **DOWNGRADED P1 → P2**, and **narrowed** | The axes were crossed: R-5's evidence is entirely peer-side and governs **FR-002**, not FR-001. The genuine contradiction is narrower — US1's Independent Test required a peer-side flip on existing cells that R-5 declines. The hazard Codex missed is on the side R-5 does not cover (the fixpp-side flip's own collateral drift), now recorded as **R-5a**. P2 because the fix is a scope decision plus wording, not new design |
| **Codex 13** — `MsgSeqNum + direction + PossDupFlag` is not a unique identity | **DOWNGRADED P2 → P3**, absorbed into root cause #A | Technically correct but over-severe in isolation: within one stream `direction` is constant and two replays of one seq number need two ResendRequests over the same range in one scripted session. The occurrence ordinal is adopted because it costs nothing. ⚠️ The part that actually bites is the half Codex did not file — `L-021-3` records QuickFIX-cpp **strips** `PossDupFlag(43)`, so on those combos the disambiguator is not on the wire at all |

### Round 2 — residuals left open, named rather than silent

| Residual | Why it is acceptable here | Where it lands |
|---|---|---|
| **The FR/SC counts in `checklists/requirements.md`** | ⚠️ **Deleted rather than corrected.** A hand-maintained count is a result nothing re-runs, and correcting one schedules the next round's finding. The derivation is recorded in its place | `checklists/requirements.md` § *Re-validation — after Gate A round 2* |
| **`checklists/requirements.md` lines 76–84 three-config vocabulary** | Struck in place rather than rewritten — the lines sit under a dated *"after `/speckit-clarify`"* heading and are a historical record of that session; striking preserves the record while removing the false present tense | same file |
| **The `occurrence` ordinal is still computed per stream** | ⚠️ **Stated, not hidden.** The declared-occurrence route (FR-005 · W-7) closes the *reconciliation* — the comparator checks the observed pairing against the census's declared occurrence values, not against the peer's independent count — but the ordinal itself is still each side's own. **If implementation shows the declared route does not hold**, the alternative is a narrow exception to the no-injection rule **for the replay steps only**, and that is a **user decision**, because the no-injection half of the correlation-key clarification is the user's | FR-005 · W-7 · escalate to the user if the declared route fails |
| **R-1, R-6, R-9 remain open by design** | Each carries a mandated measurement rather than an assumption. R-1 is now unblocked by the D-9a bootstrap mode | `research.md` |

---

## Complexity Tracking

| Violation | Why Needed | Simpler Alternative Rejected Because |
|---|---|---|
| Two repositories in one feature | The counterparties link QuickFIX, deliberately outside fixpp's Conan graph; the assertions must live with fixpp's tests | Vendoring QuickFIX into the library would put a competing FIX engine in fixpp's dependency graph — a far larger change than the feature it serves |
| **32 runs (8 logical cells × 4 configs)** | User decision, Gate A round 1; satisfies Article IX §2 in full on this surface, **measured per sanitizer rather than inferred from a config label** | The recorded three-config answer (24 runs) is superseded: `asan-ubsan` mapped to an ASan-only preset, so it delivered two sanitizer kinds while claiming three. A **combined ASan+UBSan preset** was the other option and was declined — `linux-clang-ubsan` already exists, so a new preset buys nothing, and adding `FIXPP_ENABLE_UBSAN` to `linux-clang-asan` is forbidden outright (it is a Tier-1 CI lane; changing its flags moves every object's command line and invalidates its compiler cache). `normal` + `asan-ubsan` (16 runs) was the original recommendation and was declined |
| The fourth configuration's sequencing cost | Four build trees against two ceilings — what may be resident inside the VHD, and what the VHD may still grow on the host (§ *Disk preflight*; re-derive, never copy) | Not paid in a new toolchain — `linux-clang-ubsan` already exists as a preset. ⛔ It is **not** paid by that tree's small current size either; that tree is unpopulated, and a size is not a cost. What pays for it is the **targeted build unit**, which is what turns four full builds (do not fit) into four targeted ones (fit). The residual cost is one more build/run/persist round in § Matrix sequencing and one more measured pair of thresholds in R-1 |
| A first-ever TSan bring-up inside a feature | Falls out of the sanitizer-coverage decision | Deferring TSan was the recommendation and was declined. FR-022 contains the risk by forbidding a vacuous green: a TSan arm producing no witnesses is a failure — and FR-015c makes that detectable, which it was not while witness rows carried no `config` |
| A second result artifact (`witness_evidence.yaml`, carrying both the witness rows and the `runs:` ledger) beside `cell_results.yaml` | Two separations at once. (a) **Witness vs cell**: a cell is a logical identity the runner dispatches on; witnesses are (step × direction × occurrence), and flattening the census's population into cell ids would redefine what the runner dispatches on. (b) **Ledger vs manifest**: `cell_results.yaml` is checked by a **ctest** three CI tiers run on hosted runners with no run artifacts, against a committed manifest holding the pre-existing `pass` rows already committed — so it cannot also be a machine-local run ledger, and an unconditional `REQUIRED_FIELDS` extension would break those rows (FR-020) | Nesting witnesses inside cell rows loses the exact-set completeness property at the witness level — the very property that makes a silently-absent witness detectable. Making the manifest carry `artifact_path` and requiring the check to open it fails on **every hosted runner for every 089 row** — the shape the round-2 review escalated. ⛔ A **third** file was rejected: the ledger is a `runs:` section of the witness-evidence artifact, which already accumulates per run |
