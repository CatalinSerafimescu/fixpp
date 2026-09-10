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
whole scripted conversation runs across **4 role×flavour × 2 validation arms × 3 build configs = 24 runs**.

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

**Scale/Scope**: 5 business message types × 2 directions × 4 role×flavour × 2 validation arms × 3 build
configs. 8 cells; 24 runs; witness count derived from the conversation script, never hand-listed.

---

## ⛔ Disk preflight — a GATE before every configure/build task

> This section is normative. Every build-bearing task in `tasks.md` must cite it.

### The instrument that fails toward clean

Measured 2026-09-10 on this machine:

| Reading | Command | Value |
|---|---|---|
| Inside the WSL2 VHD | `df -k /` | `/dev/sdd` — **87,050,032 KiB (83.0 GiB) available** |
| The Windows host drive backing that VHD | `df -k /mnt/e` | `E:\` — **17,766,184 KiB (16.9 GiB) available**, 93 % used |

**`df` inside WSL over-reports available space by roughly 5×.** A task that gates on `df /` will start a
34 GiB ASan configuration, burn the compile time, and die on `ENOSPC` partway. This is the repository's
dominant recorded failure class — an instrument reporting a comfortable number because it cannot see the
constraint that actually binds — and it is treated here as a defect class, not as housekeeping.

### The mechanism, so the reclaim procedure is understood rather than cargo-culted

The WSL2 ext4 VHD grows on demand and **does not auto-shrink**. Therefore:

- Deleting files **inside** WSL frees blocks for **reuse within the already-allocated VHD**. That is what
  makes reclaim work.
- It does **not** return space to `E:\`. Returning space to the host needs a VHD compaction, which is
  **out of scope** for this feature.
- ⇒ *"delete a build config to make room"* is a valid claim. *"delete a build config to give E: its space
  back"* is **not**, and no task may assert it.

Two ceilings, and they are different quantities:

| Ceiling | What it bounds | Value now |
|---|---|---|
| Free **inside** the VHD | total data resident at once | 83.0 GiB |
| Free on **the host** (`E:\`) | **net new** allocation — how much the VHD may still grow | 16.9 GiB |

⚠️ **Which ceiling binds a given build is NOT known a priori and MUST be measured, not assumed.** A build
that lands entirely in blocks freed by a prior reclaim may cost the host nothing; a build on a VHD with no
internal free blocks costs the host 1:1. **R-1** in `research.md` mandates the experiment: build one
configuration from clean while sampling **both** numbers, and derive the budget from what is observed.
Until R-1 has run, tasks must assume the pessimistic model (host cost = build size).

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

1. **Gate on the MINIMUM of two readings** — the build mount *and* the host mount. Gating on the WSL
   reading alone is precisely the defect above.
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

### Per-configuration budget — measured, not guessed

Current occupancy (`du -sh build/*/`, 2026-09-10):

| Configuration | Size | Reclaimable? |
|---|---:|---|
| `build/linux-clang-asan/` | 34 G | yes — third |
| `build/linux-clang-debug/` | 27 G | ⛔ **NEVER** — standing user rule 2026-09-10 |
| `build/linux-clang-tsan/` | 25 G | yes — second |
| `build/linux-clang-ubsan/` | 1.5 G | yes — **first** (cheapest useful) |

**Thresholds are derived from R-1's measurement with headroom, not from a round number**, and each
recorded with the figure and the date it was taken, because these move.

### Reclaim procedure

Order, cheapest-useful-first: `linux-clang-ubsan` (1.5 G) → `linux-clang-tsan` (25 G) →
`linux-clang-asan` (34 G). **`build/linux-clang-debug/` is never deleted.**

⚠️ Deleting a configuration means it must be **rebuilt** before its arm of the 24-run matrix can execute.

### ★ Matrix sequencing — all three configurations may not fit at once

This is a real planning constraint on FR-021, not an afterthought. `asan (34) + tsan (25) + debug (27)`
= 86 G already, against 83.0 GiB free inside the VHD and 16.9 GiB of host growth. **Plan for the
three-config matrix to be run in sequence, one configuration resident at a time**, with:

1. Preflight → build config *k* → run its 8 cells → **persist that config's results and evidence to disk
   outside the build tree** → reclaim config *k* → preflight → build config *k+1*.
2. Results must be **durable before reclaim**. Deleting a build tree that still holds the only copy of a
   run's evidence loses the run.
3. The evidence record must therefore be **accumulated across configurations**, not rewritten per
   configuration — a later config must not erase an earlier one's rows. The witness completeness gate
   (FR-015b) runs over the **union**, after the last configuration.

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
| **VI** — Spec Coverage Discipline | 100 % FIX rule | **PASS** | Produces evidence toward it; flips nothing |
| **VII §3** — TDD mandatory | red-green-refactor | **PASS — binding** | Every witness lands RED first. FR-017/FR-018 make the RED arms deliverables, not process |
| **VII §6** — Interop | ≥1 live QuickFIX interop test covering Logon → NOS → ExecRpt → Logout | **PASS — strengthened** | Already satisfied; this feature makes it assert field values in **both** directions rather than counters in one |
| **VII §8** — grouped tests, ctest labels | select by label | **PASS** *(watch)* | Interop cells are isolation-sensitive (live peer, ports, TLS) ⇒ they stay standalone, which §8 explicitly permits. Selection stays by `-L`, never `-R <exe>` |
| **VIII** — Performance Budgets | bench for perf-sensitive modules | **N/A** | No perf-sensitive module touched. No bench obligation |
| **IX §1** — Coverage ≥95/85 on touched modules | diff-scoped | **NEEDS CONFIRMATION → R-6** | Whether this feature touches any `include/fixpp/<mod>` or `src/<mod>` at all is undetermined; test files are excluded from measurement. If the diff is tests + harness + parent repo only, the module glob selects nothing and the gate is vacuous — **which must be stated as a measured fact, not assumed** |
| **IX §2** — Sanitizers every PR | ASan, UBSan, TSan | **PASS — expanded** | FR-021 runs all three configs (user decision 2026-09-10). ⚠️ FR-022 makes the TSan arm **bring-up**, not a config flip |
| **IX §4** — Static analysis | clang-tidy / clang-format / cppcheck / IWYU | **PASS** *(watch)* | ⚠️ `/speckit-verify` is the **only** enforcement point for three of the four — no CI job runs them. New C++ in the counterparty lives in the **parent** repo, outside the Step-1 glob (`src/**`, `include/**`) — the same blind spot filed as **#265**. State the disposition; do not let it pass silently |
| **X** — ABI Policy | C ABI versioned contract | **N/A** | `include/fix/c_api*` untouched |
| **XII** — Security & TLS | TLS posture | **PASS** | Cells stay on TLS `one_way_ca`; mutual mTLS remains `deferred:v1.1-mtls` |
| **XVI** — Spec Kit Workflow | phase order | **PASS** | specify → clarify → plan, in order |
| **XVII** — Review Gates | Gate A + Gate B | **PASS** | Gate A **available to waive** precisely because the constitution amendment was routed to 4c, not here |
| **XVIII** — Roadmap Discipline | scope honesty | **PASS** | Re-scope, the falsified premise, and the zero-rows-closed consequence are all recorded in the spec, `REMAINING-WORK.md` and `typed-messages.md` |
| **XX** — Amendments | ride a Gate A | **N/A — deliberately** | The Article XVIII §7 / Article I §1 amendment belongs to **4c**, the first catalogue-closure feature (user decision 2026-09-10) |

**Gate verdict (pre-Phase 0): PASS**, with two items routed to research (**R-3** dependency, **R-6**
coverage applicability) and one standing disposition to write down rather than assume (Article IX §4 on
parent-repo C++).

### Re-check after Phase 1 design — **PASS**

| Article | Change | Basis |
|---|---|---|
| **III**, **V** | *watch* → **PASS** | R-3 settled it by **verified absence**, not by preference: no JSON library exists on either side, so a hand-rolled writer is the only option that adds no dependency. Typed access is free — R-2 |
| **VII §3** (TDD) | unchanged, now **concrete** | The RED arms are enumerated as deliverables in `contracts/disk-preflight.md` (7 arms) and `quickstart.md` § Step 4, not left to process |
| **IX §1** (coverage) | still **open — R-6** | Deliberate. Whether the module glob selects anything must be **measured and recorded**, including when the answer is *nothing*. An empty selection reporting success is indistinguishable from a pass |
| **IX §2** (sanitizers) | unchanged | FR-021's three configs; FR-022 keeps the TSan arm from going vacuously green |
| **IX §4** (static analysis) | **disposition required, not a pass** | New C++ lands in the **parent** repo, outside `/speckit-verify`'s Step-1 glob (`src/**`, `include/**`) — the same blind spot filed as **#265**. Record how the counterparty sources are linted, or record that they are not and why |

**No new violations were introduced by the design.** The one structural addition — a second result
artifact beside `cell_results.yaml` — is justified in *Complexity Tracking* and exists to preserve the
exact-set completeness property at the witness level, which nesting would destroy.

⚠️ **Two design decisions are load-bearing against a false green and must survive `/speckit-tasks`
intact**, because both look like details and neither is:
1. **Readback file opened in TRUNCATE mode** (R-4). Append would silently accumulate stale records
   across runs, and a stale record can satisfy a comparator looking for a witness this run never
   produced.
2. **The expected witness set derived from the conversation script, never hand-listed** (W-2). A
   hand-maintained list drifts toward whatever is currently produced — a gate that agrees with reality
   by construction and can therefore never fail.

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
├── configs/*.cfg.in                    # UseDataDictionary=Y for the FIX 4.4 cells
├── golden/                             # re-captured per cell, verify-first
├── tools/run_interop_cell.py           # collect readback; capability handshake; 8 cells
└── ci/counterparties.Dockerfile        # rebuilt + republished; consumed BY DIGEST

# ── LIBRARY submodule: .../G19-fix-fpml-iso20022/library ────────────────────
tests/interop/
├── happy/hp_support.hpp                # real FIX 4.4 dictionary, not the FIX 4.2 sentinel
├── conversation/                       # NEW — the scripted conversation cells
├── support/                            # intent records + the readback comparator
├── cell_results.yaml                   # + evidence fields
├── cell_results_schema_check_test.py   # reject a pass with no evidence
└── witness_evidence.yaml               # NEW — per-witness rows + completeness gate
ci/
├── disk-preflight.sh                   # NEW — the gate
└── test-disk-preflight.sh              # NEW — its RED arms, pinned in ci-script-pins
.github/workflows/interop-smoke.yml     # digest pin replaces :latest
```

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

---

## Complexity Tracking

| Violation | Why Needed | Simpler Alternative Rejected Because |
|---|---|---|
| Two repositories in one feature | The counterparties link QuickFIX, deliberately outside fixpp's Conan graph; the assertions must live with fixpp's tests | Vendoring QuickFIX into the library would put a competing FIX engine in fixpp's dependency graph — a far larger change than the feature it serves |
| 24 runs (8 cells × 3 configs) | User decision 2026-09-10; satisfies Article IX §2 in full on this surface | `normal` + `asan-ubsan` (16 runs) was recommended and declined. The cost is accepted deliberately and is recorded in the spec's Assumptions |
| A first-ever TSan bring-up inside a feature | Falls out of the three-config decision | Deferring TSan was the recommendation and was declined. FR-022 contains the risk by forbidding a vacuous green: a TSan arm producing no witnesses is a failure |
| A second result artifact (witness evidence) beside `cell_results.yaml` | A cell is one process run; witnesses are (message × direction). Flattening ~80 witnesses into cell ids would redefine what the runner dispatches on | Nesting witnesses inside cell rows loses the exact-set completeness property at the witness level — the very property that makes a silently-absent witness detectable |
