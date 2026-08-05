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

**Select by label, never by executable name** — `[const §VII.8]` (`.specify/constitution.md:178`):
*"Tests are selected by `ctest -L <label>`, never `-R <exe-name>`."* The `-R` forms below are shown
only for the two cases §8 explicitly allows (a live single-target selection during the RED A/B). The
labels are specified in research §D-5.

```bash
# the whole feature's cells
ctest --preset linux-clang-debug -L 088 --output-on-failure

# the boundary/framing + session lifetime cells   (B1..B6, T1, T2a)
ctest --preset linux-clang-debug -L 'first_frame' --output-on-failure

# the engine-level stop cell                      (T2b -> SC-015 accept-slot leg)
ctest --preset linux-clang-debug -L 'stop' --output-on-failure

# the transport class-fix cells                   (T3..T5 -> SC-014, narrowed)
ctest --preset linux-clang-debug -L 'timer_epoch' --output-on-failure

# the real-TLS cancellation cell                   (T6 -> SC-018 / FR-018; added at Gate A r2)
# Target: session_first_frame_total_cancel_tls, LABELS "088;us2;live_tls;first_frame;cancellation",
# TIMEOUT 60 (research D-5 + D-6.13b — the D-5 table OMITTED this target until Gate A r3, so
# `ctest -L 088` was not guaranteed to run the only cell pinning FR-018).
# Needs a live loopback socket + the TLS fixture certs. Its target MUST set
# FIXPP_TLS_FIXTURE_DIR at configure time — if this cell reports SKIPPED, that is a
# FALSE PASS, not a pass ([[feedback_codex_sandbox_blocks_sockets_false_pass]]).
ctest --preset linux-clang-debug -L 'live_tls' --output-on-failure

# FR-011 guard: the PR #232 over-budget witness must pass UNMODIFIED
# (its existing labels are "015;us1;live_tls;firstframe;window", tests/session/CMakeLists.txt:1142)
ctest --preset linux-clang-debug -L 'window' --output-on-failure
git diff main..HEAD -- tests/session/engine_firstframe_test.cpp   # expect: no change to the
                                                                  # over-budget witness body
```

---

## 3. Success-criterion → witness map

| SC | Witness | What proves it |
|---|---|---|
| SC-001 | **B1** | single delivery, cumulative exactly 4096, complete Logon → the helper **returns the frame's exact length**, not `wire_frame_too_large` (narrowed at Gate A r1 — research §D-6.6) |
| SC-002 | **B3** | returned length is the Logon's exact length (3500), not the buffered size |
| SC-003 | **B4** *(regression guard, GREEN pre-fix)* + the unmodified #232 witness | over-budget with no complete frame → `wire_frame_too_large` |
| SC-004 | existing 015 coverage + **B6** | nothing sent / a drip-feeding peer → still closed on the deadline (B6 also kills the timer-re-arm mutant) |
| SC-005 | **T1** under `linux-clang-asan` | no use-after-free. The RED must name a **heap**-use-after-free; a clean pre-fix ASan run means the proof did not fire (research §D-6.3). *TSan clause removed at Gate A r1 — serialized strand handlers give a sequential UAF, not a race* |
| SC-006 | **T1** | after the call returns and the context is drained, the transport has recorded **zero** `cancel()` calls (contract S5) — restated structurally at Gate A r1, clarification G-2 |
| SC-007 | source inspection + **B5** + **B2** | exactly one budget decision point, and it is **reachable** — B2 is what shows the framer does not pre-empt it |
| SC-008 | the delivered census in `spec.md` §Census vs the diff | all four sites fixed |
| SC-009 | full ASan/UBSan/TSan ctest | 0 findings |
| SC-010 / SC-017 | `git diff main..HEAD -- include/` and the install manifest | no installed-header delta. **Only `include/fixpp/transport/test/mock_transport.hpp` is touched under `include/`, and it is test-only and `FIXPP_ALLOW_MOCK_TRANSPORT`-gated, excluded from production targets and from the install set** |
| SC-011 | source inspection | the **five** comments in research §D-8 match delivered behaviour |
| SC-012 | **B2 — the only discriminating cell** | fragmented 1000 + 3097, Logon ends at 3500, cumulative 4097 → frame wins. **Only satisfiable with the carry capacity at `max_bytes + 1`** (research §D-1a) |
| SC-013 | **B5** + the clamp proof in research §D-1 + §D-1a's sufficiency proof | three bounds: bytes read ≤ 4097, logical `buf.size()` ≤ 4097 (vs a pre-fix 8191), peak resident ≈ 12 KiB |
| SC-014 | **T3, T4, T5** | one per transport site; the attempt's epoch is retired before the operation returns. *Narrowed at Gate A r1 (G-1) — the same-drain ordering is not constructible at this layer; residual filed* |
| SC-015 | **T2a** (dedicated, direct) + **T2b** (accept-slot reclaim, engine level) + **T6** (the TLS leg) | `stop()` mid-read aborts promptly — and T2a is shown to actually catch a read in flight (`read_latency > 0` and `async_reads_observed() >= 1` before the signal; that counter is **new** — the mock has no read counter today, research §D-9). **Amended at Gate A r2: T2a is mock-driven and therefore green even against the un-mapped TLS build — it must NOT be cited as evidence for the TLS leg.** T2b (real mTLS) does hit it but only by **hanging**, hence its ctest `TIMEOUT`; **T6 is the discriminating cell** |
| SC-016 | **T1, T2a** | ordering constructed on a hand-driven `io_context` (elapse-then-poll, §D-6.2); no timing margins. *Scoped at Gate A r1 to the session-layer cells — T3–T5 no longer test an ordering* |
| **SC-018** | **T6 — real TLS, and nothing else can** *(added at Gate A r2)* | a `total` delivered while a real `ssl::stream` first-frame read is blocked on a silent peer aborts it with `transport_read_cancelled`, before a test-owned watchdog fires. **Every mock-driven cell is green here even against the un-mapped build** — the mock's read is a `steady_timer` and a timer honours `total` (`asio/detail/deadline_timer_service.hpp:315-320`), so no `mock_transport` cell can ever discriminate (research §D-6.10) |

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

⚠️ **The scratch pre-fix header must keep `pmr_carry_buffer carry{max_bytes, …}` exactly as `main` has
it.** Silently "fixing" the capacity while extracting the body would make B2 pass for the wrong reason
and destroy the A/B. Change **only** the namespace, the `inline`, and the includes (research §D-6.7).

For **T1** the same procedure under `linux-clang-asan` must surface a **heap**-use-after-free on the
`timed_out` write. The helper MUST be driven through `asio::co_spawn(ioc, …, asio::detached)` — never
`co_await`ed from an enclosing test coroutine — so HALO cannot elide the frame the defect writes into;
a `stack-use-after-return` report instead means the frame was elided and the proof measured something
else, and a **clean** ASan run means *the proof did not fire*, never *there is no defect*
(research §D-6.3). No TSan RED is claimed — see SC-005. For **T2a/T2b**, the RED variant is the
*naive* join — a bare `timer.async_wait(asio::use_awaitable)` arm instead of the wrapped one — which
**aborts only at the full deadline; the RED is failure of the promptness bound, not a wrong error
value** (research §D-6.12b). *(Corrected at Gate A round 4: this line previously read "must fail to
abort on `stop()`", which round 3's own D-2 self-correction had already falsified — the mutant does
abort, just late.)*

**Not every cell REDs against `main`.** B4 is a regression guard and is GREEN pre-fix. B6, T2a, T2b,
T3–T5 and **T6** RED against **mutants of the delivered design**, because the properties they pin did
not exist pre-fix in a form a cell can address (research §D-6.7).

> **T6's RED is the round-2 amendment's whole evidence, and it must be run BEFORE the OUT map is
> added** — not after, with the map reverted as an afterthought. The order matters because the cell is
> being written against a defect the rest of the suite cannot see: if it is authored green-first, a
> mis-wired signal path produces a passing cell that proves nothing
> (`[[feedback_sanitizer_canary_must_be_proven_red]]`). Capture the watchdog assertion's failure text
> into the verify record, exactly as for the ASan cells.

Expected RED signatures, per cell:

| Cell | RED basis | Failure signature |
|---|---|---|
| B1 | `main` | returns `wire_frame_too_large` instead of a length |
| **B2** | `main` | returns `wire_frame_too_large` — **and also fails under the rejected comparison-only fix, and under a carry capacity of `max_bytes`**, which is the whole point of this cell. **Requires `Script::inbound_chunks = {1000 B, 3097 B}`** (research D-9 mechanism 5, added at round 3): without per-read chunk control the mock returns all 4096 requested bytes in **one** read, the fragmentation never happens, and the cell **silently stops discriminating both mutants it exists for** while still going red on `main` — i.e. it still looks correct in review. Re-derived in D-6.11 |
| B3 | `main` | returns 4096 (or errors) instead of 3500 |
| **B4** | *(none — regression guard, GREEN on `main`)* | a `budget + 1` no-frame payload is rejected identically by `>=` and `>`; labelled as a guard, not a RED cell |
| B5 | `main`; and the `room == 0` mutant | **`read_sizes()` does not begin `{4096, 1}`** — it begins `{4096, 0, 0, …}` — **and** the call ends in `transport_handshake_timeout` rather than the asserted `wire_frame_too_large`. Under the `max_bytes - buf.size()` mutant the second request is **0**, the mock returns a **successful zero-byte** completion consuming nothing, and the loop re-enters. **Requires `read_latency = 3 ms`, `deadline = 50 ms`, and `ioc.run()`** (3 ∤ 50 — B6's no-common-multiple rule) — *at zero latency the cell does not go RED, it **LOOPS WITHOUT BOUND**: the loop yields (the join is per-iteration, so `operator||` co_spawns a fresh read arm each time, `asio/experimental/awaitable_operators.hpp:343-347`), but arms launch in index order with the read at index 0 (`asio/experimental/impl/parallel_group.hpp:376-380`), the per-arm handler writes `completion_order_[completed_++] = I` first (`:205-206`), and an arm 0 that completes during its own launch is enqueued before arm 1's wait is initiated — so `order[0] == 0` **every iteration** and the deadline branch is never taken. The 3 ms latency is what lets arm 1 be recorded first once the remaining deadline drops below it; B5's outcome is invariant even on an exact tie (research §D-6.11).* Corrected at Gate A round 4 — research §D-6.11 |
| B6 | mutant: `expires_after` moved into the loop / into `await_deadline` | returns `wire_frame_too_large` at byte 201 instead of `transport_handshake_timeout` — the deadline never fires. **Requires 201 one-byte `inbound_chunks` and `read_latency = 7 ms` against a 50 ms deadline** (mechanism 5). *Round 2's form was **RED against the DELIVERED design**: the mock returned all 201 bytes in one read, so correct code returned `wire_frame_too_large` while the cell asserted `transport_handshake_timeout`. The 7 ms is load-bearing — at 5 ms the 10th read co-expires with the deadline at exactly 50 ms, reintroducing an ordering this bundle refuses to depend on* |
| T1 | `main`, under ASan | **heap**-use-after-free writing to the freed coroutine frame; and post-return, a `cancel()` recorded on the transport |
| T2a / T2b | mutant: bare `timer.async_wait(use_awaitable)` arm | **Corrected at Gate A round 3 (C3/N2) — the previous signature, `transport_handshake_timeout`, was WRONG and both cells' claimed REDs were false.** Under an external `total` the group's one-shot cancel guard is consumed by the external handler (`asio/experimental/impl/parallel_group.hpp:168`, `:351`), so the read arm's later completion cannot re-emit (`:222`), the bare arm runs to **full expiry**, and `order[0] == 0` makes `operator\|\|` return the **read** arm's value (`awaitable_operators.hpp:352-357`). The mutant therefore returns **`transport_read_cancelled`, at the deadline** — the same value as the correct code. **The discriminator is LATENCY:** T2a asserts return within **100 ms** of the emit (deadline 500 ms); T2b asserts `Engine::stop()` returns within **500 ms** (deadline 5000 ms). A value-only assertion passes the mutant |
| T2b, **second mutant** *(added at Gate A r2)* | mutant: FR-018 reverted | T2b drives real mTLS, so it does hit the un-mapped defect — but the failure **is a hang**, registering only as a ctest `TIMEOUT`. Real, and unusable as a RED record; that is what **T6**'s watchdog exists to convert into an assertion (research §D-6.8 footnote) |
| T3–T5 | mutant: **retire-point omitted only** | the attempt's epoch is still current when the operation returns, so a queued expiry would cancel a live socket. The **guard-omitted** mutant has **no killer** — it changes no counter, and on every constructible run the handler short-circuits on `ec` before the guard is consulted; the guard is discharged structurally and carried in the filed residual (research §D-6.4) |
| **T6** | mutant: **FR-018 reverted** — restore the one-argument `reset_cancellation_state(enable_total_cancellation())` at `src/transport/asio_tls_transport.cpp:1134` | the `total` never aborts the real `ssl::stream` read, so the join never retires and the call **never returns**. The observable failure is the test-owned **watchdog** firing (assertion A2), not the ctest timeout — the watchdog exists precisely so the RED is bounded and produces a captured assertion instead of a killed job (research §D-6.10). A run that ends in a ctest `TIMEOUT` instead means the watchdog itself is mis-wired; that is a broken cell, not a RED proof. **⚠ Round-3 prerequisite (C2): if the cell's outer `co_spawn` has no wrapper coroutine resetting to `enable_total_cancellation()` first, the watchdog fires WITH FR-018 CORRECTLY PRESENT** — a red run then proves nothing about the OUT map. Verify the wrapper before believing either colour |

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
2. **The carry buffer is `max_bytes + 1`, and the derivation is commented.** `grep -n 'pmr_carry_buffer carry'`
   in the new header must show `max_bytes + 1`, with a comment tying it to the C1 clamp bound. At
   `max_bytes` the framer rejects at cumulative 4097 **before any parse**, so B2 fails, FR-007's
   decision point is dead, and the whole discriminating half of the fix is undelivered (research
   §D-1a). **This is the single highest-value line in the diff.**
3. **`expires_after` appears exactly once, outside the loop.** `grep -n 'expires_after'` in the new
   header must return one hit, in the prologue. A hit inside `await_deadline` or inside the loop
   resets the deadline on every read and silently removes it (FR-017 / research §D-1b) — with every
   existing test green.
4. **The deadline arm resets its cancellation filter, and does not throw.** A bare
   `timer.async_wait(use_awaitable)` in the join is the D-2 trap: it compiles, it passes every
   functional test, and **it makes `Engine::stop()` take the full deadline (5 s) instead of returning
   promptly** — bounded, equal to the pre-fix tail, and invisible to any assertion on the error value,
   which is why T2a/T2b bind a *promptness* bound (research §D-6.12b). It also throws on every
   established connection, which is why the arm uses `redirect_error` (D-3). *(Corrected at Gate A
   round 4: this item previously said the bare arm "silently breaks `Engine::stop()`" — round 3's own
   D-2 self-correction had already withdrawn that as an overstatement. The **unbounded** failure is
   the read arm's, item 4a.)*
4a. **`asio_tls_transport::async_read_some` uses the TWO-argument `reset_cancellation_state`, with an
    OUT filter mapping non-`none` → `terminal`, and says why in-source.** *(Added at Gate A round 2 —
    FR-018.)* `grep -n 'reset_cancellation_state' src/transport/asio_tls_transport.cpp` must show the
    two-argument form at **two** sites: `async_connect` (`~:930`, pre-existing) and
    `async_read_some` (`~:1134`, new). If the read site still shows the one-argument form, the join
    **hangs `Engine::stop()` unboundedly on every TLS connection** — with every test green except T6.
    Check the comment too, not just the code: it must state that the SSL composed op honours only
    `terminal` (so an unmapped `total` is silently discarded), and carry the same two
    `[[feedback_*]]` mnemonics the connect site has at `:928-929`. **This is the second-highest-value
    line in the diff**, after the carry capacity — and unlike that one, its absence is invisible to
    every other cell in the suite.
5. **B2 is fragmented — and check the MECHANISM, not just the prose.** A single-write version of that
   cell discriminates nothing: it passes under the delivered fix and under the rejected one. `grep`
   the cell for `inbound_chunks`; if it sets `inbound_bytes` instead, the mock coalesces the whole
   delivery into one read and the fragmentation **did not happen**, whatever the comment says
   (research §D-9's overturned claim / §D-6.11). Same check on **B6** (201 one-byte chunks) and **B5**
   (`read_sizes() == {4096, 1}`).
5a. **Every cancellation cell `co_spawn`s a wrapper that resets FIRST.** *(Round 3 — C2.)* For T2a and
    both legs of T6, read the spawned lambda: its **first statement** must be
    `co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());`. Without
    it `co_spawn`'s terminal-only initial state (`asio/impl/co_spawn.hpp:336`) discards the test's
    `total` and the cell fails on **correct** code. This is the same rule production already obeys at
    `src/session/engine.cpp:673-676`.
5b. **T2a and T2b assert a TIME BOUND, not just an error value.** *(Round 3 — C3/N2.)* The bare-arm
    mutant returns the **same** `transport_read_cancelled` the correct code does — just at the
    deadline. A cell that only checks the error code passes it. Look for the elapsed-time assertion
    (100 ms for T2a, 500 ms for T2b); if it is absent, that column of the mutant matrix is empty and
    should be published as such.
6. **The transport handlers touch no member before the guard passes.** Read each of the three
   lambdas: the epoch comparison must go through the by-value `shared_ptr`, and `socket_` must appear
   only *after* the early return. A member `timer_epoch_` compared inside the handler is read through
   a pointer that may already be dangling (research §D-4.0/§D-4.1).
7. **Each transport destructor has a body that retires every epoch**, and is not `= default`. Without
   it, item 6's safety argument does not close.
8. **The #232 over-budget witness is untouched.** If it needed editing, the fix overreached (FR-011).
9. **All four census sites are in the diff.** Three transports plus the engine — `git diff main..HEAD
   --stat` should show `asio_plain_transport.cpp` as well as the two the issue named.
10. **Nothing under `include/` changed except the test-only mock.** `git diff main..HEAD --stat -- include/`
    should show `include/fixpp/transport/test/mock_transport.hpp` and nothing else (SC-010/SC-017).
