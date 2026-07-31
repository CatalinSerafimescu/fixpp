# Quickstart — Validating 084-packaging-cpack-export

**Feature**: 084-packaging-cpack-export · **Date**: 2026-07-31

Runnable validation. Written to be followed after a context reset — every path and constraint needed is here.

---

## 0. Environment

| | |
|---|---|
| Worktree | `/home/catalin/Work/Programming/fixpp-parallel` (branch `084-packaging-cpack-export`) |
| Build storage | `/mnt/wsl/fixppbuild` — 64 GB, mounted from a dedicated vhdx on F: |
| Build trees | `build/<preset>` → symlinks into `/mnt/wsl/fixppbuild/build/<preset>` |
| Package output | `/mnt/wsl/fixppbuild/artifacts/` — **survives build-tree deletion** |
| ccache | 20 GB at `/mnt/wsl/fixppbuild/ccache` |

**Two environment rules that cause silent damage if skipped:**

```bash
source /mnt/wsl/fixppbuild/env.sh     # BEFORE the first cmake --preset, always
```
CMake bakes the compiler launcher in at **first configure only**. Configure without this and ccache is baked *out* of that tree — the only fix is deleting the tree and starting over.

If `/mnt/wsl/fixppbuild` is missing, the vhdx detached (any `wsl --shutdown` does this):
```bash
# from Windows:  schtasks /run /tn "WSL Mount fixpp-build"
```
A missing mount looks like a broken build tree. Check the mount first.

---

## 1. Build order — and why it is not the obvious one

**Start with `linux-gcc-release`.** It is the only configuration cheap on *both* axes.

| Configuration | Tree size | Third-party deps to build |
|---|---|---|
| **`linux-gcc-release`** | **3.4 GB** | **0** |
| `linux-clang-release` | 2.4 GB | 5 |
| `linux-clang-debug` | 22 GB | 0 |
| `linux-gcc-debug` | ~25 GB | 9 |

`linux-clang-release` has the smallest tree but five dependencies to build from source — smallest ≠ fastest to a first package.

**One configuration at a time. Delete before the next.** Four trees do not fit in 64 GB.

```bash
cd /home/catalin/Work/Programming/fixpp-parallel
source /mnt/wsl/fixppbuild/env.sh

conan install . -pr:a=gcc13 -s build_type=Release -b missing
cmake --preset linux-gcc-release
cmake --build --preset linux-gcc-release -j2      # -j2: wider OOM-kills the session
# ... package + validate (sections 2-5) ...
rm -rf /mnt/wsl/fixppbuild/build/linux-gcc-release  # artifacts/ is untouched
```

**Development accelerator**: while getting the CMake right, add `-o "&:with_otel=False"` to cut `linux-gcc-debug` from 9 dependency builds to 3. **Never ship an artifact built that way** — every other leg is telemetry-enabled, and a single telemetry-disabled package would be the only one missing those targets.

---

## 2. Minimal witness — `find_package` resolves

```bash
ctest --test-dir build/linux-gcc-release -R "consumer::install-witness" --output-on-failure
```

**Expected**: PASS — the standalone consumer configures via `find_package(fixpp)`, links `fixpp::fixpp`, and builds with no hand-added include or library path.

**Must include BOTH header kinds** (SC-002): a hand-written `include/` header *and* a generated per-version header (e.g. `Fields.hpp`). They arrive via two different install rules (`CMakeLists.txt:321` and `:346`), and only the second exercises the dict export. A core-header-only witness passes against a dict export pointing at headers that were never installed — `find_package` succeeds, the target exists, and the failure surfaces only when a real consumer writes `#include`.

---

## 3. Real-client witness — the export actually links

The tier that catches an export which *resolves but cannot link*. A header-only consumer never exercises the link interface and structurally cannot detect this.

```bash
ctest --test-dir build/linux-gcc-release -R "packaging::real-client" --output-on-failure
```

**Expected**: `perf/fixpp_perf_driver.cpp` — the fixpp half of the cross-engine benchmark rig — builds **out-of-tree** against the installed package, links, and runs against a live counterparty.

Three adaptations for the packaged variant (research R9):
1. Drop `src/` and `tests/` from the include path (`perf/CMakeLists.txt:51-54`) — SC-012 forbids any source-tree path.
2. Drop the `HdrHistogram_c` `FetchContent` (`:43-47`) — a network fetch; latency histograms are irrelevant to a link-and-run witness.
3. Replace `support/minimal_dictionary.hpp` with a runtime load of a **shipped** dictionary via `fixpp::dict::load_any`.

> **This inverts a standing caution.** The driver is documented as needing an in-tree build so it links freshly generated libraries. Building it against an installed package is safe **only** because the package comes from the build under test — which is what section 5 enforces.

---

## 4. Package contents — enumerate, never infer

```bash
ls /mnt/wsl/fixppbuild/artifacts/
ctest --test-dir build/linux-gcc-release -R "packaging::contents" --output-on-failure
```

**Must be present** (SC-013): public headers · generated typed headers · exported static libraries · `fixppConfig.cmake` + version file · FIX dictionaries · upstream license text · **`NOTICE`**.

**Must be absent** (SC-004): test executables · build scratch · `messages/` · `groups/` · `validators/` · `all.hpp` · `groups.hpp`.

> Checked by **listing package contents**, never by reading install rules. A rule whose pattern matches nothing yields a package missing content while looking correct in CMake — and for the attribution set that is a legal deficiency, not a cosmetic one.

---

## 5. Provenance — the stale-package trap

```bash
ctest --test-dir build/linux-gcc-release -R "packaging::provenance" --output-on-failure
```

**Expected**: a witness fed a package from a *different* configuration or source revision **fails**.

> **Why this is live, not theoretical.** The build strategy deletes trees between configurations while `artifacts/` deliberately survives (FR-021). That directory therefore accumulates packages from earlier configurations and earlier source states — precisely what would let a witness go green against a package predating the change under test.

---

## 6. Telemetry-disabled config resolution

```bash
conan install . -pr:a=gcc13 -s build_type=Release -o "&:with_otel=False" -b missing
cmake --preset linux-gcc-release -DFIXPP_BUILD_OTEL=OFF
# then re-run the minimal witness against this build
```

**Expected** (SC-015): the generated config resolves. A config unconditionally requiring the telemetry dependency breaks **every** telemetry-disabled consumer, and no telemetry-enabled configuration can detect it.

> This is the one defect class the descoped clang-libc++ lane would have caught — it is the only lane built telemetry-disabled. Excluding it from verification is only acceptable *because* this check replaces the coverage.

---

## 7. Debug vs Release — platform asymmetry

```bash
readelf -S <installed>/libfixpp_core.a | grep -c debug_info
```

| | Release | Debug |
|---|---|---|
| Linux | 0 debug sections (13 KB) | present (228 KB) |
| Windows | not shipped | **separate symbol files must be present** |

Neither the archiver nor the linker strips anything — the compiler never emits debug information in Release. On Windows the information lives outside the library entirely, so a Debug package shipped with Linux-shaped rules is undebuggable. **Verify Windows naming against real toolchain output; do not assume it.**

---

## 7b. Two traps when adding the new tests

**All three packaging tests must be `RUN_SERIAL` with an explicit `TIMEOUT`.** They configure and build sub-projects, so concurrent runs collide with each other and with the git-cleanliness gate. Mirror the existing consumer witness (`TIMEOUT 300`, driven via `cmake -P`).

**Commit `NOTICE` together with its install rule.** `tests/codegen/codegen_build_graph_test.cmake:198-221` runs `git status --porcelain` and fails on any output. The build symlinks are invisible to it (git never descends into an ignored directory), but a new **tracked** file at the repo root is not — `NOTICE` will red that gate for as long as it stays uncommitted.

---

## 8. Windows legs

Build in a **separate sandbox** under `/mnt/c/temp/`. **Do not reuse `/mnt/c/temp/fixpp`** — it holds unrelated in-flight state from another feature. Windows trees live on C: and do not consume the 64 GB budget.

---

## 9. Full acceptance

| # | Check | Criterion |
|---|---|---|
| 1 | `find_package(fixpp)` + `fixpp::fixpp`, no manual paths | SC-001 |
| 2 | Minimal witness green, both header kinds | SC-002 |
| 3 | All six configurations produce artifacts, names match exactly | SC-003 |
| 4 | No tests/scratch/denylisted content in any package | SC-004 |
| 5 | Debug symbolicates; Release carries none — both platforms | SC-005 |
| 6 | Incompatible version fails at **configure** | SC-006 |
| 7 | Export-closure + denylist assertions **proven to fail** on broken input | SC-007 |
| 8 | Matrix produced under 64 GB, one configuration at a time | SC-008 |
| 9 | `Args` boundary verification recorded with evidence | SC-009 |
| 10 | CI artifacts attached, uniquely named | SC-010 |
| 11 | Real client builds/links/runs against an installed package | SC-011 |
| 12 | No source-tree path participates in that build | SC-012 |
| 13 | Dictionaries + upstream license + `NOTICE` in every package | SC-013 |
| 14 | Shipped dictionary loads via the public API from the installed prefix | SC-014 |
| 15 | Config resolves against a telemetry-disabled build | SC-015 |

**A gate never observed failing proves nothing.** SC-007 requires the assertions be demonstrated red on a deliberately broken input before they count.
