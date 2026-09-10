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
   run on hosted runners with no artifacts, against a manifest holding 59 pre-existing `pass` rows. Any
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
.github/workflows/interop-smoke.yml     # digest pin replaces :latest
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
2. **The counterparty image is the coupling.** A library-side cell requiring readback protocol *v1* and a
   published image predating it is the stale-peer hazard FR-016a/FR-016b exist to make loud.
3. **Order of operations**: parent counterparty change → image rebuild + publish → digest captured →
   library-side cells pinned to that digest. A library PR merged ahead of the image is a broken tree.
4. ⚠️ **Republishing moves `:latest`, and everything NOT pinned rides it.** FR-016b pins *this feature's*
   cells to a digest; `interop-smoke.yml`'s `IMAGE:` key names the mutable `:latest`, which is the only tag
   that workflow knows. On republish, **every existing interop consumer immediately runs new counterparty
   code**, unpinned and ungated — including the cells FR-020 requires to keep passing, the cells R-5
   deliberately protects (`INTEROP_CP_CORRUPT_ADMIN`, the `PD-*` malformed-dup cells: the readback emitter
   is new code on the inbound path of every message they send), and the required smoke workflow itself.
   **Step 0 of the ordering is therefore to pin existing consumers to the pre-089 digest**, so `:latest`
   moving is inert. See **FR-026** and **R-11**; it costs one line in the workflow.

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
> shape the rule is about. The sweep found **two further unrowed producers** and both are closed in this
> table: **§12's terminal record on the COUNTERPARTY side** (fixpp's row named `hello`/`terminal`, the
> counterparty's named only the hello) and **§4's witness rows** (the comparator was in the directory
> tree, not in an obligation). ⛔ **One entity is deliberately NOT rowed and this is the clause saying so**:
> **§8, the disk preflight reading** — its producer is `ci/disk-preflight.sh`, specified at length in this
> file's own § *The gate itself* and in `contracts/disk-preflight.md`, which is where a reader looks for
> it. Every other section resolves to a row above.
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
| ″ | ⛔ **`test_ids_unique` is NOT replaced.** Round 1 recorded that it must be, on the assumption that 8 ids had to serve 32 rows. The manifest/ledger split removes that: the committed manifest carries one row per `(cell_id, config)` slot with `id = "<cell_id>@<config>"`, unique across 32, and retry rows never reach it | FR-013a · E-1a |
| ″ | **Status vocabulary** extended with `error:enospc` / `aborted`. The shipped set `{pass, fail, skip, known-limitation, n/a}` is closed by an assertion, `n/a` is bound to a `deferred:*` disposition and `known-limitation:*` to a tracking issue — so every existing option is forbidden and the implementer reaches for `fail` | FR-014a · E-5 |
| ″ | ⛔ **`REQUIRED_FIELDS` is extended CONDITIONALLY, on `kind: conversation` only — never globally.** An unconditional extension breaks the **59** `status: pass` rows already committed, none of which has an 089 run artifact, colliding head-on with FR-020 | FR-013 · FR-020 · E-1 |
| ″ | ⛔ **The check MUST NOT open any artifact path.** It is a **ctest** (`tests/interop/CMakeLists.txt:459`) provisioned in `tier1.yml`, `tier2.yml` and `tier3-libcxx.yml` on hosted runners that hold **no** run artifacts. What it checks instead: every `status: pass` conversation row names an existing ledger entry whose `terminal_state` is `completed` and whose `witness_count` equals the census figure for that slot; the set of slots carried by **`kind: conformance`, `authoritative: true`** ledger rows equals the 32-slot inventory exactly; exactly one such run per slot; and ⭐ **the `validation_pairs:` section carries exactly the 16-pair `kind: conformance` inventory** (**E-7a**) with every `off_run_id`/`on_run_id` resolving to a `runs:` row. ⚠️ **All four operands are inside the committed file**, so the pair gate opens nothing and survives into CI — the promotion command cannot host it, because it runs per configuration and cannot see 16 pairs until the last one. ⚠️ `validator-positive-control` and retry rows are recorded in the ledger and **excluded** from the slot equality (data-model §11 § *THE TWO DISCRIMINATORS*) | FR-014 · FR-012a · E-1 · E-1c · E-7a · W-3b · W-3d |
| `library/tests/interop/witness_evidence.yaml` (**NEW**) | **THREE sections, not two** — witness rows, the `runs:` **ledger**, and ⭐ **`validation_pairs:`** (data-model §10, FR-012a): committed, machine-independent, `evidence_relpath` relative to `$FIXPP_INTEROP_EVIDENCE_ROOT`, **never an absolute path**. ⚠️ The `runs:` ledger holds **every** run — `conformance` and `validator-positive-control`, authoritative and superseded — with `kind`/`authoritative` discriminating; it is not filtered on the way in, because a retry that is never written down cannot be shown not to have been counted. ⚠️ `validation_pairs:` was **absent from this row** while four artifacts described the entity — the RC-A defect, in the row RC-A is about | FR-014b · FR-012a · E-7 · E-7a · data-model §10 · §11 |
| `phase-9-harness/tools/promote_interop_evidence.py` (**NEW**) | The **named promotion command** (FR-014b). Opens both streams, requires a `hello` **and** a `terminal` on each, checks the join keys, evaluates the completeness gate, persists the bundle under the evidence root, records `evidence_digest`, writes the ledger entry and the manifest row. Runs **after** a config's 8 cells complete and **before** its build tree is reclaimed. Nothing else may write a `status: pass` conversation row.<br>⭐ **AND IT IS THE PRODUCER OF THE VALIDATION PAIRS.** Once both arms of a `(combo_id, config)` have promoted runs, it **constructs** the pair (data-model §10), **evaluates E-7** on it — `off_run_id ≠ on_run_id`; `off_run_id`'s run carries `cell_pair[0]` and `has_validator: false`; `on_run_id`'s carries `cell_pair[1]` and `true`; both carry the pair's `config`, `script_digest` and `kind` — and **writes it to `validation_pairs:`**. A pair failing E-7 is **promotion RED**; it is never written. ⚠️ It does **not** host the 16-pair completeness gate: it runs per configuration and cannot see 16 pairs before the last one, so **E-7a lives in the committed schema check** | FR-014 · FR-014b · FR-012a · E-1b · E-7 |
| `phase-9-harness/tools/run_interop_cell.py` | **`CONFIG_TO_PRESET`**: `{normal→linux-clang-debug, asan→linux-clang-asan, ubsan→linux-clang-ubsan, tsan→linux-clang-tsan}`; the `asan-ubsan` key is retired | FR-021 · FR-021a |
| ″ | **Six metadata keys added to the existing `cp_env` block in `launch_counterparty`** — `INTEROP_CP_{RUN_ID, CELL_ID, CONFIG, IMAGE_DIGEST, SCRIPT_PATH, SCRIPT_DIGEST}`. That block already assembles ten `INTEROP_CP_*` knobs and is passed as `env=cp_env` to **both** the C++ and the Java branch, so this is one more block in an existing pattern. ⚠️ Without it the counterparty is required by FR-013b to emit four hello fields it has **no channel to receive** | FR-013b · data-model §1 · R-4 (reversed) |
| ″ | The shim **computes** `script_digest` (lowercase-hex SHA-256 over the script bytes) and **compares** it against the value each side recomputes; a mismatch FAILS before the gtest is launched | FR-008c · FR-013b |
| ″ | **Four dedicated conversation config templates** for the eight new cells (`config_template` is already a per-cell attribute), carrying `UseDataDictionary=Y` with a FIX 4.4 `DataDictionary` path. ⛔ The existing `quickfix-cpp-{initiator,acceptor}-tls.cfg.in` and `quickfixj-{initiator,acceptor}-tls.cfg.in` MUST NOT be edited — they all carry `UseDataDictionary=N` and are named by the idle-cadence and `PD-*` cells R-5 protects, so editing them flips those cells. A regression check asserts a protected cell still renders `UseDataDictionary=N`. ⛔ A "narrowly scoped renderer override" was **rejected**: the per-cell `config_template` seam already exists | FR-002 · R-5 |
| ″ | ⭐ **A full `INTEROP_FIXPP_*` env block on the GTEST's environment** — `RUN_ID`, `CELL_ID`, `CONFIG`, `ARM`, `SCRIPT_PATH`, `SCRIPT_DIGEST`, `READBACK_PATH` (data-model §1). ⚠️ `INTEROP_CP_*` is `env=cp_env` on the **counterparty** launch and does not reach the gtest, while `run_id` is minted by the shim and has no other in-gtest source — so without this block fixpp emits a stream promotion cannot join. An absent key is a hard abort, never a default | FR-013b · FR-014 · E-1b |
| ″ | The **pre-conversation hello gate** (FR-016a), shim-side, before the gtest is launched. ⚠️ Its failure text must not use `unavailable:` — `parse_gtest_status` greps that token and returns `skip:` | FR-016a · FR-024 · R-4a |
| ″ | Invoke the named promotion command after each configuration's 8 cells and **before** reclaiming that configuration's build tree | FR-014b |
| `library/tests/interop/` **and** `phase-9-harness/tools/` — ⭐ **THE E-*/W-* FIXTURE CORPUS (NEW row, not a new obligation)** | ⚠️ **Applied under RC-B, and it exposes a pre-existing omission rather than adding work.** `contracts/witness-evidence.md` § *Proof obligations for Level 1* already required a **negative fixture per obligation** — E-1, E-1b (five of them), E-1c, E-5, E-6, E-7 — plus the *"59 pre-existing `pass` rows stay GREEN"* control, and **none of them had a row in this table**, because relevance was judged and the evidence inventory looked sufficient. RC-B makes it unconditional: these are files outside the bundle. **Fixture host follows the checker** — E-1 · E-1c · **E-7a** · W-3a/b/c/d are checked by the committed ctest (`cell_results_schema_check_test.py`) and their fixtures sit beside it, opening nothing; E-1b · E-6 · **E-7** are checked at promotion and their fixtures sit beside `promote_interop_evidence.py`. ⛔ **E-7's three negative fixtures** (`off_run_id == on_run_id`; two distinct runs with the **same** `has_validator`; a referenced run disagreeing on `config`/`script_digest`/`kind`) and ⛔ **E-7a's two** (an **empty** `validation_pairs:` section against an **otherwise-complete** artifact; a **15-of-16** section) are named here because they are this round's additions; the older ones are recorded above as the omission RC-B closes | E-1 · E-1b · E-1c · E-5 · E-6 · **E-7** · **E-7a** · W-3a · W-3b · W-3c · W-3d · FR-020 |
| `phase-9-harness/INTEROP-016-DESIGN.md` | The config vocabulary `normal\|asan-ubsan\|tsan` is corrected to the four-config set | FR-021a |
| `phase-9-harness/INTEROP-COVERAGE-REPORT.md` | The claim that the charter's ASan+UBSan requirement is met by `asan-ubsan` is corrected | FR-021a |
| `library/.github/workflows/interop-smoke.yml` | The `IMAGE:` key is pinned to the **pre-089 digest before the counterparty image is republished**, so moving `:latest` is inert for existing consumers | FR-026 · R-11 |
| `phase-9-harness/quickfix-cpp/counterparty/interop_counterparty_main.cpp`, `phase-9-harness/quickfixj/.../InteropCounterparty.java` | `sent` **and** `readback` emitters, the hello record, ⭐ **the TERMINAL record (data-model §12)**, `occurrence` ordinals, and the specified header partition including the tag-1156 reconciliation. ⚠️ **The terminal half was found missing by the RC-A sweep run in the same edit that added RC-A**: fixpp's row names `hello`/`terminal`, this one named only the hello, while E-1b requires a `terminal` on **both** streams and `quickstart.md` Step 4 arms *"a stream carrying a `hello` and no `terminal`"* — an entity constrained and armed with **no producer**, which is item 2's shape one `data-model.md` section over. ⛔ Written **last, whatever the outcome**, including on an abort — `terminal_state` ∈ {`completed`, `aborted`, `error:enospc`} with `sent_count`/`readback_count` and the hello's join keys | FR-003 · FR-003a · FR-004 · FR-005 · FR-014a · E-1b · data-model §12 · R-10 |
| `library/tests/interop/conversation/conversation_script.yaml` (**NEW**) | ⭐ **THE SCRIPT** — the executable conversation SC-009a compares against. Ordered steps with `step_id`, `msg_type`, originator, direction, intent values, typed-read declarations, `depends_on`. Read by the gtest **and** the counterparty (`*_SCRIPT_PATH`); its digest is FR-008c's pin | FR-008a/b/c · SC-009a |
| `library/tests/interop/conversation/census.yaml` (**NEW**) | ⭐ **THE CENSUS — and W-2a's OPERAND.** A **manual, mechanical** transcription of `spec.md` § *Conversation census*, which is its **source**, not its operand. ⛔ **MUST NOT be generated from the script**: that makes SC-009a a tautology and retroactively voids the only argument admitting a hand-written census. ⛔ **MUST NOT hard-code `100`** — `spec.md` § *Conversation census* settles that the figure is derived in one place and a pointer everywhere else. ⚠️ **Stated limit** (`contracts/witness-evidence.md` W-2a): the transcription is manual and **nothing checks it** — mutate a row of the spec table and no gate reddens, because no check opens `spec.md`. **Re-transcribe in the same edit that changes the spec table** | FR-015d · W-2a · SC-009a |
| `phase-9-harness/quickfix-cpp/counterparty/interop_counterparty_main.cpp`, `phase-9-harness/quickfixj/.../InteropCounterparty.java`, **`phase-9-harness/quickfix-cpp/counterparty/CMakeLists.txt`**, **`phase-9-harness/quickfixj/pom.xml`** | ⭐ **The typed-accessor COMPILE ARM** (user decision, Gate A fresh loop round 1 — supersedes round 3's `accessor_witness` seam, which is **deleted**). **(a)** For every field the script declares as a typed read, the counterparty source reaches it through that message's **generated per-message accessor** — `FIX44::<Message>::get(FIX::<Field>&)` (C++) / `quickfix.fix44.<Message>.get(quickfix.field.<Field>)` (Java). The compiler is the schema-conformance check. **(b) The NEGATIVE-COMPILATION arm needs its own MECHANISM, and here it is — OVER THE REAL COUNTERPARTY SOURCES, not a snippet.** C++: a `try_compile` case in `phase-9-harness/quickfix-cpp/counterparty/CMakeLists.txt` **whose source argument is `interop_counterparty_main.cpp` itself** (or the shared typed-read adapter TU the production call site depends on), compiled against the same include paths and engine pin as the production target, with the mutation applied to **that file** — asserted to **FAIL**. Java: a `javac` invocation wired into `phase-9-harness/quickfixj/pom.xml`'s counterparty build **over `InteropCounterparty.java` itself**, asserted to exit non-zero. ⛔ **`check_cxx_source_compiles` over a standalone string is REJECTED as the mechanism**: a detached TU proves the *pinned engine API* accepts `Symbol` and rejects `LastPx` and proves **nothing about the counterparty** whose conformance FR-003b(a) claims — a counterparty reading everything through generic enumeration satisfies it. Three sites in this bundle say *"mutate the counterparty **source**"*; this row is the one that said *"a TU"*, and that granularity mismatch is the whole defect (RC-C: **restate the subject inside the mechanism**, not only in the requirement). **Both directions asserted**: the unmutated file compiles, the mutant does not. ⛔ **AND THE FAILURE MUST MATCH THE EXPECTED MISSING-OVERLOAD DIAGNOSTIC** — *no matching function for call to … `get(FIX::<Field>&)`* (clang/gcc), *cannot find symbol … method `get(quickfix.field.<Field>)`* (javac) — never merely a non-zero exit. ⚠️ *A forced MISS cannot catch a spurious HIT*: a typo, a missing include or a wrong namespace makes the mutated build fail for a reason that has nothing to do with the schema, and a **snippet is strictly worse on this than the real file** (a snippet missing `#include <quickfix/fix44/NewOrderSingle.h>` fails "correctly" for entirely the wrong reason). ⚠️ **No match-count check on the mutation itself is required** — in this arm's polarity a rewrite that matched nothing leaves the source unmutated, so the "mutant" **compiles** and the assertion *"the mutant does not compile"* goes RED on its own. ⚠️ **Stated per-site coverage limit**: **one** mutated `(Message, Field)` site per language. FR-003b(a) binds *every* declared typed read, and the **positive** direction covers all of them by construction (the file compiles); the **negative** arm is an anti-vacuity probe of the mechanism, and one site suffices because the absence of a generic `get` is **structural, not per-message** — there is none on `FieldMap`, on `Message`, or on either generated class. ⚠️ **That argument is ENGINE-PIN-BOUND**: on any re-pin of either vendored engine, re-check the inherited overload set, generic fallbacks and field-type conversions before relying on one site. Parameterizing the mutation over the script's typed-read declarations would remove the limit and is **not** required. ⛔ *"The build MUST fail"* with no named mechanism is scored **absent** under this file's round-2 acceptance rule. ⚠️ The **vendored engines stay unpatched**, so FR-023 holds; the arm binds our call site, not the engine's interior | FR-003b · FR-018 · SC-003 |
| `library/tests/interop/support/` (**NEW** — fixpp's readback/sent emitter **and the shared comparator**) | ⭐ **THE WITNESS PRODUCER (data-model §4), named by the RC-A sweep.** The shared comparator pairs the two streams on `(seq_num, direction, occurrence)` and emits the **witness rows** — one per identity 1, carrying `combo_id`, `cell_id`, `config`, `run_id`, `arm`, `authoritative` and `kind` (W-1) — which promotion then persists into `witness_evidence.yaml`. The directory tree in § *Source code* already showed the comparator living here; the obligation column named only the emitter, so §4's producer was unrowed. ⭐ **fixpp is also the THIRD emitter of `contracts/readback-jsonl.md`, and C-7 is THREE-WAY.** fixpp's emitter is bound by every clause of that contract — canonical partition, canonical form and sort order, encoding rule, `hello`/`terminal` — and **participates in the committed cross-language golden fixture**: all three emitters invoked on the same constructed record, compared byte-for-byte against **one** committed expected artifact. ⚠️ **A declaration is not a binding.** FR-006 compares parsed field **sets** and is blind to sort order and escaping, so nothing else guards fixpp's byte-level form; the earlier claim that an unbound fixpp emitter *"goes RED on the first run"* was **false for three of the four clauses it covered** and is deleted. Force the walk-order mutation **in fixpp's emitter** too | C-7 · FR-004 · FR-025 |
| `library/tests/interop/conversation/conversation_script.yaml`, `phase-9-harness/quickfixj/.../InteropCounterparty.java` | ⭐ **C-11 — the LIVE-path charset arm** (`contracts/readback-jsonl.md` § *C-11*). The script declares `EncodedTextLen(354)`/`EncodedText(355)` on step `B-05` with a value containing byte `0xff`; the QuickFIX-J readback's `value_b64` for path `355` MUST equal the base64 of the wire bytes on C3/C4. ⚠️ **Census-neutral** — existing step, same `(seq_num, direction, occurrence)`, no completeness key added. ⚠️ The synthetic C-7 fixture **cannot** discharge this: it never enters QFJ's decoder. ⚠️ **Two vacuity closures travel with this arm** (`contracts/readback-jsonl.md` § *C-11*): the `value`/`value_b64` decision is made on the **ISO-8859-1 re-encoded bytes**, not on the decoded `String` — under ISO-8859-1 `0xff` decodes to the valid char `U+00FF`, and an implementer classifying on the string would write `value` and pass without ever exercising the b64 path; and `EncodedTextLen(354)` is **derived from the value's octet count, never a literal** — an inconsistent LENGTH/DATA pair is rejected by the validation-on arm before any readback exists. | C-11 · C-7 · FR-004 |
| `phase-9-harness/quickfixj/.../InteropCounterparty.java` | Assert `org.quickfixj.CharsetSupport.getDefaultCharset()` is **`ISO-8859-1`** at startup and fail loudly otherwise. `value_b64` is only reconstructible because that charset is a total bijection over all 256 byte values; under a non-bijective charset the raw bytes are unrecoverable at application level | C-7 · FR-004 |

⚠️ **Out of scope, and it must stay stated rather than assumed**: re-characterising the `asan-ubsan` rows
already in `cell_results.yaml`. Every one of them records a result that was never under UBSan. That is a
pre-existing corpus problem and belongs in a filed issue, not in this feature.

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
| A second result artifact (`witness_evidence.yaml`, carrying both the witness rows and the `runs:` ledger) beside `cell_results.yaml` | Two separations at once. (a) **Witness vs cell**: a cell is a logical identity the runner dispatches on; witnesses are (step × direction × occurrence), and flattening the census's population into cell ids would redefine what the runner dispatches on. (b) **Ledger vs manifest**: `cell_results.yaml` is checked by a **ctest** three CI tiers run on hosted runners with no run artifacts, against a committed manifest holding 59 pre-existing `pass` rows — so it cannot also be a machine-local run ledger, and an unconditional `REQUIRED_FIELDS` extension would break those 59 rows (FR-020) | Nesting witnesses inside cell rows loses the exact-set completeness property at the witness level — the very property that makes a silently-absent witness detectable. Making the manifest carry `artifact_path` and requiring the check to open it fails on **every hosted runner for every 089 row** — the shape the round-2 review escalated. ⛔ A **third** file was rejected: the ledger is a `runs:` section of the witness-evidence artifact, which already accumulates per run |
