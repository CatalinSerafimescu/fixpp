# Contract: `ci/disk-preflight.sh`

**Consumers**: every build-bearing task in `tasks.md`.
**Convention**: `ci/<name>.sh` + `ci/test-<name>.sh`, pinned in the `ci-script-pins` job
(`.github/workflows/tier1.yml:2130`), alongside `test-restore-conan-cache.sh`, `test-ccache-scripts.sh`
and `test-check-wheel-payload.sh`.

## The problem it exists to solve

`df` inside WSL2 reports **83.0 GiB** available; the Windows host drive backing that VHD has **16.9 GiB**
(measured 2026-09-10). A gate reading only the first will authorise a 34 GiB build that cannot complete.

## Behaviour

| # | Obligation |
|---|---|
| D-1 | Gate on the **minimum** of the build-mount and host-mount readings |
| D-2 | Detect WSL via `microsoft` in `/proc/version`; locate the host mount from `/proc/mounts` (it is a `9p` `drvfs` mount) |
| D-3 | ⚠️ **On WSL, an unreadable or absent host mount ⇒ FAIL.** Never `proceed` |
| D-4 | Off WSL, the host reading is **not applicable** and the build-mount reading governs — a *different* case from D-3, on a different code path |
| D-5 | Below threshold ⇒ **exit non-zero** and refuse the build. No warn-and-continue. No exit 0 on the failing path |
| D-6 | Print both readings and the verdict on **every** invocation, pass or fail |
| D-7 | The threshold comes from R-1's measurement, recorded **with its date** |
| D-8 | The reserve ballast is **not** counted as available |

**D-3 vs D-4 is the crux.** Both are "no host reading available"; one must stop and one must proceed.
Sharing a code path collapses them, and the collapse fails toward `proceed` — the direction that hurts.

**D-5 restates a known local failure**: the repository has a recorded gate that warns, prints
`DIAGNOSTIC ONLY`, and exits 0.

## Verdicts

| Verdict | Meaning | Exit |
|---|---|---|
| `proceed` | headroom ≥ threshold on both applicable readings | 0 |
| `reclaim-first` | below threshold, but reclaimable configs exist | non-zero |
| `stop` | below threshold with nothing reclaimable, or D-3 | non-zero |

## Reclaim

Order: `linux-clang-ubsan` (1.5 G) → `linux-clang-tsan` (25 G) → `linux-clang-asan` (34 G).

⛔ **`build/linux-clang-debug/` is NEVER deleted** — standing user rule, 2026-09-10.

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

| Arm | Forces | Must show |
|---|---|---|
| A-1 | host below threshold, build mount comfortable | RED — the D-1 minimum, the whole point |
| A-2 | both comfortable | GREEN, with both readings printed |
| A-3 | WSL, host mount unreadable | RED via **D-3** specifically, not incidentally |
| A-4 | not WSL, no host mount | GREEN via **D-4** — proves A-3 is not just "no host mount ⇒ fail" |
| A-5 | below threshold, reclaimables present | `reclaim-first`, non-zero |
| A-6 | below threshold, nothing reclaimable | `stop`, non-zero |
| A-7 | **spurious hit** — remove the host reading entirely | RED. Proves the gate is not passing for a reason unrelated to what it measures |

**A-3 paired with A-4 is the load-bearing pair.** A-3 alone is satisfied by a script that fails whenever
the host mount is missing — which would break every CI runner. A-4 alone is satisfied by one that never
checks. Only together do they pin the actual rule.
