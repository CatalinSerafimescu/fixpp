# Quickstart / validation guide: 088 — bounded first-frame read

**Feature**: `088-firstframe-budget-timer-lifetime` · **Date**: 2026-08-04

How to build the feature and prove each success criterion. This is a **validation guide** — it
records the commands and the expected outcomes, not the implementation. Every witness named here is
specified in [research.md](./research.md) §D-6 and mapped to an SC below.

> **Resource gate (Article XVII §7).** Local builds are heavy — Conan fetch + full compile +
> sanitizer rebuilds. An agent MUST surface an `AskUserQuestion` and get approval before running any
> of the build commands below. Use `-j2` at most; wide parallel C++ builds OOM-kill the session
> (`[[feedback_build_resource_cap_oom]]`).

---

## 0. Prerequisites

```bash
cd research/G19-fix-fpml-iso20022/library     # from the parent repo root
git rev-parse --abbrev-ref HEAD               # expect: 088-firstframe-budget-timer-lifetime
```

Toolchain: Clang 22 (Article XVII §7 — local == CI). `asio/1.38.0` per `conanfile.py:67`.

---

## 1. Build

```bash
conan install . --build=missing -s build_type=Debug -pr:h=./conan/profiles/linux-clang \
                -pr:b=./conan/profiles/linux-clang
cmake --preset linux-clang-debug
cmake --build --preset linux-clang-debug -j2
```

Sanitizer presets used by this feature's evidence: `linux-clang-asan`, `linux-clang-ubsan`,
`linux-clang-tsan`. Build them **one at a time** (Article XVII §8 — serial preset matrix, so failures
stay isolable).

---

## 2. Run the feature's witnesses

> ⚠️ **These target names, labels and `-R` patterns do not exist at this commit.** They are the
> *contract* for what `/speckit-tasks` and `/speckit-implement` must create — the `088` ctest label,
> the `session_read_first_frame_bounded` target, and the regex-matchable test names. Run against the
> plan-phase tree they match **zero** tests, and a zero-test ctest run **exits green**
> (`[[feedback_ci_gate_observes_not_asserts_witness_skips_into_green]]`). Treat a green run here as
> evidence of nothing until the targets exist.

```bash
# the whole feature's cells
ctest --preset linux-clang-debug -L 088 --output-on-failure

# the boundary/framing cells only            (B1..B5 -> SC-001/002/003/012/013)
ctest --preset linux-clang-debug -R 'read_first_frame_bounded' --output-on-failure

# the lifetime cells                          (T1, T2 -> SC-005/006/015/016)
ctest --preset linux-clang-debug -R 'first_frame_(same_drain|stop)' --output-on-failure

# the transport class-fix cells               (T3..T5 -> SC-014)
ctest --preset linux-clang-debug -R 'transport.*same_drain' --output-on-failure

# FR-011 guard: the PR #232 over-budget witness must pass UNMODIFIED
ctest --preset linux-clang-debug -R 'engine_firstframe' --output-on-failure
git diff main..HEAD -- tests/session/engine_firstframe_test.cpp   # expect: no change to the
                                                                  # over-budget witness body
```

---

## 3. Success-criterion → witness map

| SC | Witness | What proves it |
|---|---|---|
| SC-001 | **B1** | single delivery, cumulative exactly 4096, complete Logon → session establishes |
| SC-002 | **B3** | returned length is the Logon's exact length (3500), not the buffered size |
| SC-003 | **B4** + the unmodified #232 witness | over-budget with no complete frame → `wire_frame_too_large` |
| SC-004 | existing 015 coverage + T1's timeout leg | nothing sent / partial frame → closed within the deadline |
| SC-005 | **T1** under `linux-clang-asan` **and** `linux-clang-tsan` | no write-to-freed, no race on the captured state |
| SC-006 | **T1** | the established session's read pump is not cancelled; it processes a later frame |
| SC-007 | source inspection + **B5** | exactly one budget decision point; the clamp cannot request 0 bytes |
| SC-008 | the delivered census in `research.md` §D-4 vs the diff | all four sites fixed |
| SC-009 | full ASan/UBSan/TSan ctest | 0 findings |
| SC-010 / SC-017 | `git diff main..HEAD -- include/` and the install manifest | no installed-header delta |
| SC-011 | source inspection | the three comments in research §D-8 match delivered behaviour |
| SC-012 | **B2 — the only discriminating cell** | fragmented 1000 + 3097, Logon ends at 3500, cumulative 4097 → frame wins |
| SC-013 | **B5** + the clamp proof in research §D-1 | peak buffered = 4097, never more |
| SC-014 | **T3, T4, T5** | one per transport site; a succeeded connect/handshake is not cancelled |
| SC-015 | **T2** | `stop()` mid-read aborts promptly — and the test is shown to actually catch a read in flight |
| SC-016 | all of T1–T5 | ordering constructed on a hand-driven `io_context`; no timing margins |

---

## 4. Proving each new pin RED against pre-fix source (FR-010)

**This is evidence, not a formality** — an unproven canary proves nothing
(`[[feedback_sanitizer_canary_must_be_proven_red]]`).

Because D-5 puts the function in an `inline` header, the RED run is a **source A/B on one function**,
not a branch checkout — so it cannot be contaminated by stale objects
(`[[feedback_stale_build_objects_false_green_masks_pins]]`):

```bash
# 1. capture the pre-fix body
git show main:src/session/engine.cpp > /tmp/pre-fix-engine.cpp
#    extract read_first_frame_bounded (main: lines 378-455) into a scratch copy of
#    src/session/read_first_frame_bounded.hpp, keeping the new signature

# 2. build ONLY the witness target against the scratch header
cmake --build --preset linux-clang-debug --target session_read_first_frame_bounded -j2

# 3. run and CAPTURE the failure output — this text goes into the verify record
ctest --preset linux-clang-debug -R 'read_first_frame_bounded' --output-on-failure 2>&1 \
  | tee /tmp/088-red-boundary.txt

# 4. restore the fixed header, rebuild, confirm green
```

For **T1** the same procedure under `linux-clang-asan` must surface a use-after-free /
stack-use-after-return on the `timed_out` write; under `linux-clang-tsan`, a race on the captured
state. For **T2**, the RED variant is the *naive* join — a bare
`timer.async_wait(asio::use_awaitable)` arm instead of the wrapped one — which must fail to abort on
`stop()` (research D-2). For **T3–T5**, the RED variant is the pre-fix handler without the epoch
check.

Expected RED signatures, per cell:

| Cell | Pre-fix failure |
|---|---|
| B1 | returns `wire_frame_too_large` instead of a length |
| **B2** | returns `wire_frame_too_large` — **and also fails under the rejected comparison-only fix**, which is the whole point of this cell |
| B3 | returns 4096 (or errors) instead of 3500 |
| B5 | zero-length read / spin, or a wrapped `room` |
| T1 | ASan: write to freed coroutine frame; session torn down by a spurious `transport_read_cancelled` |
| T2 | `stop()` does not abort; the call runs to the 5000 ms deadline |
| T3–T5 | the first operation after a successful connect/handshake is cancelled |

---

## 5. Full verification (before Gate B)

```bash
# Article XVII §7 local pre-PR build gate — the line that must appear in the PR body
ctest --preset linux-clang-debug --output-on-failure
git rev-parse --short HEAD      # -> "local build: green on linux-clang-debug @ <sha>"
```

Then the mandatory `/speckit-verify 088-firstframe-budget-timer-lifetime`, which writes
`.specify/decisions/088-firstframe-budget-timer-lifetime-verify.md`. Its verdict must be **GREEN** or
**YELLOW** before `/gate-b` will start (Article XVII §8).

> ⚠️ `/speckit-verify` is clang-only — gcc-release and MSVC are CI-only
> (`[[feedback_local_verify_clang_only_misses_gcc_release_ci_job]]`). It also hardcodes the **main
> checkout**, which is where this feature lives, so no worktree workaround applies here.

---

## 6. What a reviewer should check by hand

1. **The loop has exactly one budget decision**, at the foot of the body, past the frame-found
   return. Grep for `max_bytes` in the new header — a second comparison is an FR-007 violation and
   breaks the clamp proof.
2. **The deadline arm resets its cancellation filter.** A bare `timer.async_wait(use_awaitable)` in
   the join is the D-2 trap: it compiles, it passes every functional test, and it silently breaks
   `Engine::stop()`.
3. **B2 is fragmented.** A single-write version of that cell discriminates nothing — it passes under
   the delivered fix and under the rejected one.
4. **The #232 over-budget witness is untouched.** If it needed editing, the fix overreached (FR-011).
5. **All four census sites are in the diff.** Three transports plus the engine — `git diff main..HEAD
   --stat` should show `asio_plain_transport.cpp` as well as the two the issue named.
