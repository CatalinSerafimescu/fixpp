# Contract: `ci/disk-preflight.sh`

**Consumers**: every build-bearing task in `tasks.md`.
**Convention**: `ci/<name>.sh` + `ci/test-<name>.sh`, pinned in the `ci-script-pins` job
(`.github/workflows/tier1.yml:2130`), alongside `test-restore-conan-cache.sh`, `test-ccache-scripts.sh`
and `test-check-wheel-payload.sh`.

## The problem it exists to solve

⛔ **NO READINGS ARE RECORDED IN THIS CONTRACT.** The condition and the recipe are; the numbers are not.
A figure written into the document that gates on it goes stale silently and re-arms the exact failure the
gate exists to catch — an earlier draft's figures were falsified within the hour by a routine reclaim.

**The condition:**

> `df` inside the WSL2 VHD and `df` on the Windows host drive backing that VHD measure **two different
> quantities**. The internal reading bounds *total data resident at once*; the host reading bounds *net new
> allocation*. The internal reading is routinely the larger and the more comfortable, so a gate reading only
> it will authorise a build the host cannot finance.

| Reading | Command | Predicate it feeds |
|---|---|---|
| inside the VHD (`/dev/sdd`) | `df -k /` | `required_internal_free` |
| the host drive backing it (`E:`, a `9p` `drvfs` mount) | `df -k /mnt/e` | `required_host_growth` |

⚠️ **Part of the gap is already materialised on the host but free inside the VHD** — writes landing there
cost zero host growth. **That pool is a BOUND, not a measurement**, and its size is deliberately not
written here: ext4 does not preferentially allocate into already-materialised extents, so it may never be
realised. The gate must not compute it, assume it, or subtract it (D-7a). `required_host_growth` comes from
**R-1's measured host delta**, full stop.

⚠️ **`/mnt/wsl/fixppbuild` is a different device and is outside both predicates.**
`CCACHE_DIR=/mnt/wsl/fixppbuild/ccache` and the persisted evidence root
`$FIXPP_INTEROP_EVIDENCE_ROOT` (default `/mnt/wsl/fixppbuild/interop-evidence/`, FR-014b) sit on `/dev/sde`,
a separate VHD whose backing file is not on `E:`, while `/` is `/dev/sdd`. The gate must not sample it into
either reading (D-11). Confirm with `df -k /mnt/wsl/fixppbuild`.

## Behaviour

| # | Obligation |
|---|---|
| D-1 | ⚠️ **Two named predicates, evaluated INDEPENDENTLY**, both per configuration: `required_internal_free` compared to the **build-mount** reading, and `required_host_growth` compared to the **host-mount** reading. Neither value may be compared against the other reading |
| D-1a | The output **names the failing predicate** — `internal`, `host`, or `both` |
| D-2 | Detect WSL via `microsoft` in `/proc/version`; locate the host mount from `/proc/mounts` (it is a `9p` `drvfs` mount) |
| D-3 | ⚠️ **On WSL, an unreadable or absent host mount ⇒ FAIL.** Never `proceed` |
| D-4 | Off WSL, the host predicate is **skipped** and `required_internal_free` alone governs — a *different* case from D-3, on a different code path |
| D-5 | Below either threshold ⇒ **exit non-zero** and refuse the build. No warn-and-continue. No exit 0 on the failing path |
| D-6 | Print both readings, **both** required values with their measurement dates, the failing predicate and the verdict on **every** invocation, pass or fail |
| D-7 | Both required values come from R-1's measurement, **per configuration**, **against the targeted interop-driver build** (the unit the matrix actually performs — see `plan.md` § *The build unit is TARGETED*), each recorded **with its date** |
| D-7a | `required_host_growth` is R-1's **measured host delta**. The gate may **not** derive it from, or offset it by, the already-materialised reuse pool — that quantity is a *bound*, not a measurement, which is why its size is written nowhere in this bundle: a figure on the page is an invitation to subtract it |
| D-11 | Neither reading may include `/mnt/wsl/fixppbuild/ccache` — it is on a different device (`/dev/sde`) whose backing file is not on `E:` |
| D-8 | The reserve ballast is **not** counted as available |
| D-9 | An unset or unparseable threshold is a **hard error**, never 0 |
| D-9a | ⚠️ **Bootstrap, because R-1 cannot run under its own gate.** D-7 sources both values from R-1's measurement and R-1's experiment is four builds — circular. `--bootstrap` accepts thresholds carrying the literal label **`estimated, pending R-1`** with a date, taken from the plan's pessimistic model (host cost = build size), and **prints that label on every output line**. Admissible for the R-1 experiment and **nothing else**; any other task invoking it is a violation, made visible in the log rather than left inferable. The path is dead once R-1's measured values land |
| D-10 | The host mount is resolved independently; it may **not** fall back to the build mount |

### ⚠️ Why D-1 is two predicates and not a minimum

The previous rule — *gate on the minimum of the two readings* against a single threshold — had its
fail-open somewhere other than where it looks. As an abstract predicate `min(a, b) ≥ T` is **stricter**
than either alone, so it fails toward refusing a build that would have succeeded: a false RED, not the
dangerous direction.

**The actual fail-open is where the threshold comes from.** R-1 derives it from the observed **host
delta**; D-1 then applied it to the **build-mount** reading, which bounds *total data resident at once*.
Illustratively — *a dated observation from 2026-09-10, kept because the argument needs a vivid magnitude,
and NOT an operand: re-derive with `du -sh build/*/`* — the ASan tree stood at ≈34 GiB. If R-1 measures
block reuse and returns, say, a 4 GiB host delta, the gate authorises that ≈34 GiB build whenever the VHD
has 4 GiB free. **That is the exact ENOSPC the gate exists to prevent, reproduced by the gate's own
arithmetic.** One threshold applied to two ceilings under-checks whichever ceiling is larger — and the
larger one is the build-mount ceiling.

**D-3 vs D-4 is the second crux.** Both are "no host reading available"; one must stop and one must
proceed. Sharing a code path collapses them, and the collapse fails toward `proceed` — the direction that
hurts.

**D-5 restates a known local failure**: the repository has a recorded gate that warns, prints
`DIAGNOSTIC ONLY`, and exits 0.

## Verdicts

| Verdict | Meaning | Exit |
|---|---|---|
| `proceed` | **both** applicable predicates satisfied | 0 |
| `reclaim-first` | ⚠️ the **`internal`** predicate failed and reclaimable configs exist — offered for an internal-space failure **only** | non-zero |
| `stop` | the `host` predicate failed; or `internal` failed with nothing reclaimable; or D-3 | non-zero |

⚠️ **`reclaim-first` is never offered for a host failure.** Deleting inside WSL frees blocks for reuse
within the already-allocated VHD and returns **nothing** to `E:\` (plan.md § *The mechanism*). Prescribing
reclaim for a host-space failure prescribes a remedy the plan's own mechanism section proves cannot work —
and having done it, the readings are unchanged, so the operator loops.

## Reclaim

Order: **cheapest-useful-first, re-derived at the moment of reclaim** with `du -sh build/*/`. ⛔ **No order
is written here**, because the one that used to be has already moved and will move again: it was headed by
`linux-clang-ubsan` purely because that tree is currently unpopulated, and **this feature's matrix
populates it**.

⚠️ **A size read off a tree is a claim about that tree's current state, not about the build it will
hold** — so reclaiming an unpopulated tree frees almost nothing, and its position at the head of a reclaim
order is not evidence that its configuration is cheap. When a tree's size is used to argue about a *build*,
scale it: `find build/<preset> -name '*.o' | wc -l` against a populated sibling's objects and
bytes-per-object.

⚠️ **A configuration is reclaimable only once its runs are COMPLETE and PERSISTED.** Under the four-config
matrix every one of these trees is also a matrix configuration that must be **built and run**, so the
reclaim order and the matrix order are different orderings over the same set and must not be confused. In
particular `linux-clang-ubsan` is both *the first thing to delete* and *a required arm of FR-021*: it may
be deleted only after its 8 cells have run and their evidence has been written outside the build tree.

⛔ **`build/linux-clang-debug/` is NEVER deleted** — standing user rule, 2026-09-10. It is also the
`normal` configuration's build tree, so it is resident throughout.

⚠️ Deleting inside WSL frees blocks for **reuse within the VHD**; it does **not** return space to the
host. No output may claim otherwise.

⚠️ The ballast (`/mnt/e/_wsl-reserve-{1,2}.bin`, 5 GiB each) is a **one-shot last resort**. If spent, an
explicit step refills it — a spent valve nobody refills is worse than no valve, because the next person
believes they still have it.

## Required RED arms — `ci/test-disk-preflight.sh`

Following the `test-check-wheel-payload.sh` precedent: **every RED cell asserts its OWN diagnostic, not
merely a non-zero exit.** That distinction was not cosmetic there — two cells initially reddened via a
*different* check than the one they were written for, and stayed green under a mutation that reverted
the rule they were meant to pin.

A **spurious-hit** arm is *an arm that makes the guard report PASS for a reason other than the property it
claims to measure* (FR-018). A **forced-miss** arm proves the guard *can* fire. Both are required, and one
does not substitute for the other.

| Arm | Kind | Forces | Must show |
|---|---|---|---|
| A-1 | forced miss | host below `required_host_growth`, build mount comfortable | RED naming `failing_predicate: host` — the independent host predicate, the whole point |
| A-1a | forced miss | build mount below `required_internal_free`, host comfortable | RED naming `failing_predicate: internal`. ⚠️ Paired with A-1 this is what proves the two predicates are **separate**: one threshold applied to both readings passes one of these two arms for the wrong reason |
| A-2 | control | both comfortable | GREEN, with both readings, both required values and their dates printed |
| A-3 | forced miss | WSL, host mount unreadable | RED via **D-3** specifically, not incidentally |
| A-4 | control | not WSL, no host mount | GREEN via **D-4** — proves A-3 is not just "no host mount ⇒ fail" |
| A-5 | forced miss | `internal` below threshold, reclaimables present | `reclaim-first`, non-zero |
| A-5a | forced miss | **`host`** below threshold, reclaimables present | `stop`, non-zero — **never `reclaim-first`**. Reclaim returns nothing to the host, so offering it here prescribes a remedy that cannot work and loops the operator |
| A-6 | forced miss | below threshold, nothing reclaimable | `stop`, non-zero |
| **A-7** | **spurious hit** | the threshold is **unset or unparseable** and both mounts are nearly full | RED (D-9). ⚠️ A threshold defaulting to `0` makes `proceed` true for a reason unrelated to free space — the guard reports PASS while measuring nothing |
| **A-8** | **spurious hit** | the host mount **resolves to the build mount** (the same mount read twice) | RED (D-10). The host predicate is then vacuously satisfied by the build reading, so `proceed` is true for a reason unrelated to host free space |

⚠️ **A-7 replaces the arm this table previously carried under that number.** The old A-7 forced *"remove
the host reading entirely"* and expected RED — the same condition and the same outcome as **A-3**, i.e. a
second flavour of forced miss wearing a spurious-hit label. A forced miss cannot catch a spurious hit: it
answers *can the guard fire*, never *what else could satisfy the condition the guard is watching*. A-7 and
A-8 answer the second question.

**A-3 paired with A-4 is a load-bearing pair.** A-3 alone is satisfied by a script that fails whenever
the host mount is missing — which would break every CI runner. A-4 alone is satisfied by one that never
checks. Only together do they pin the actual rule.

**A-1 paired with A-1a is the second load-bearing pair**, for the same reason on the other axis: either
alone is satisfied by a single-threshold implementation.
