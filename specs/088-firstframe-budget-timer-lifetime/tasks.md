---
description: "Task list for 088-firstframe-budget-timer-lifetime"
---

# Tasks: First-frame byte budget + timer-handler lifetime

**Feature**: `088-firstframe-budget-timer-lifetime` · closes **#233**
**Branch**: `088-firstframe-budget-timer-lifetime`
**Input**: `spec.md` (18 FR / 18 SC), `plan.md`, `research.md` (D-1…D-9), `data-model.md`,
`contracts/read_first_frame_bounded.md`, `quickstart.md`
**Gate A**: CONVERGED 2026-08-05 (`P1=0 P2=0`) — 4 rounds, 4 amendments. Bundle commit `f60e2d5b`.

**Tests are MANDATORY for this feature, and RED-first is normative.** Article XVII §8 plus the
bundle's own D-6: every one of the **13 witness cells** must be *mutation-proven* — seen RED against
its named mutant before the fix lands — and the RED captured in the verify record. A cell authored
green-first proves nothing here; that is the failure this feature's own Gate A caught three rounds
running. See `quickstart.md` for the per-cell RED procedure.

## Format: `[ID] [P?] [Story] Description`

- **[P]** — parallelizable (different file, no dependency on an incomplete task)
- **[USn]** — user-story phase tasks only; Setup / Foundational / Polish carry no story label

## Path Conventions

Repository root is the library submodule (`research/G19-fix-fpml-iso20022/library/`). All paths below
are relative to it. Note: the **concrete transport headers live under `src/transport/`**, not
`include/fixpp/transport/` (corrected at Gate A round 1).

---

## Phase 1: Setup (Shared Infrastructure)

- [X] T001 Confirm the pre-fix baseline is green and record it: build the `linux-clang-debug` preset and run ctest **directory-scoped** over `build/linux-clang-debug/tests/session` and `.../tests/transport` (`ctest --test-dir <dir>`), capturing the pass list into `.specify/decisions/088-firstframe-budget-timer-lifetime-verify.md` under `## Baseline` — every subsequent RED proof is read against this list. **Corrected at /implement:** the originally-specified `ctest -L session -L transport` selects **zero** tests — ctest ANDs repeated `-L` flags, and no test carries both labels. The union form `-L 'session|transport'` selects only **29**, because label sets in these directories are per-feature (`LABELS "040;us2;…"`), not per-subsystem; the two directories actually hold **151** tests (131 + 20). Directory scoping is the only complete selector here and is not the `-R <exe-name>` form Article VII §8 prohibits
- [X] T002 [P] Re-read the Gate A convergence caveat in `specs/088-firstframe-budget-timer-lifetime/plan.md` §Gate A and confirm no bundle file has been amended since commit `f60e2d5b`; the narrow-verify verdict is pinned to a snapshot with checksums and any amendment voids its line numbers

---

## Phase 2: Foundational (Blocking Prerequisites)

**⚠️ Blocks every user story.** The mock mechanisms and the internal header are what make the cells
constructible at all — round 3 found three cells specifying I/O shapes the mock could not produce.

- [X] T003 Add mechanism 3 `cancels_observed()` (contract S5) to `include/fixpp/transport/test/mock_transport.hpp` — a counter of `cancel()` calls observed, per research D-6.2 / D-9 #3
- [X] T004 Add mechanism 4 `async_reads_observed()` to `include/fixpp/transport/test/mock_transport.hpp` — T2a non-vacuity, per D-9 #4
- [X] T005 Add mechanism 5 `Script::inbound_chunks` (`std::vector<std::vector<std::byte>>`) to `include/fixpp/transport/test/mock_transport.hpp` implementing research D-9's **seven numbered rules**: one chunk contributes to each completion; **no completion crosses a chunk boundary**; a non-empty chunk MAY span multiple reads with the remainder left at the head; `read_latency` applies once per read attempt/completion **including remainder reads**; **empty chunks are forbidden** and MUST never yield a zero-byte success; exhaustion → `transport_read_eof`; `bytes_read_so_far()` in chunk mode is cumulative bytes delivered. An empty `inbound_chunks` MUST preserve today's cursor-path behaviour byte-for-byte
- [X] T006 Add mechanism 6 `read_sizes()` to `include/fixpp/transport/test/mock_transport.hpp` — records the **requested** length (`buf.size()`) per read; the *sequence* is what B5 asserts (D-9 #6)
- [X] T007 Verify the additive claim for T005/T006 across the **real consumer set** and confirm no pre-existing cell moves, since every current `Script` user default-constructs or assigns existing fields only. **Corrected at /implement:** the originally-specified `ctest -L session` selects only **9** of the **131** tests in `tests/session`, because labels here are per-feature (`LABELS "040;us2;…"`), not per-subsystem. Required instead: a successful **build**, plus a passing directory-scoped **run** of `tests/session` (131) and `tests/transport` (20) against the `## Baseline` list. **Census, by `#include` and not by substring:** the consumers of `include/fixpp/transport/test/mock_transport.hpp` are **7 files — 6 in `tests/session`, 1 in `tests/transport`, and none anywhere else.** Do not widen this to `tests/conformance` or `tests/fuzz`: `tests/conformance/mock_transport.hpp` is a **different, unrelated local file**, and the `tests/fuzz` matches are comment-only (`// … via mock_transport …`), not includes
- [X] T008 Extract `read_first_frame_bounded` from `src/session/engine.cpp` into a NEW `src/session/read_first_frame_bounded.hpp` as `inline`, **behaviour-preserving** (no loop changes yet) — FR-016 / D-5, mirroring the placement precedent `src/session/scan_first_frame_ids.hpp`. It stays under `src/` deliberately so SC-017 holds by construction with no `install()` rule touched
- [X] T009 Wire a new test target for the direct-helper cells in `tests/session/CMakeLists.txt`: `tests/session/read_first_frame_bounded_test.cpp`, with `${CMAKE_SOURCE_DIR}/src` on the include path — copy the include-path shape from the `:712-728` block, **not** its wiring. **Use D-5's delivered-wiring block verbatim (`research.md:957-962`), which is more precise than this task's prose was:** (a) **no explicit `fixpp` link is needed or wanted** — `add_threading_test` already links `fixpp_session`, which PUBLIC-links `fixpp_wire` (`Framer::feed`), `fixpp_transport` (the `Transport` vtable) and `asio::asio`, so the dependency is satisfied by construction; `fixpp` is only an `INTERFACE` aggregate (`CMakeLists.txt:623`). (b) The block also requires `target_compile_definitions(... PRIVATE FIXPP_ALLOW_MOCK_TRANSPORT)`, which this task's text omitted — without it `mock_transport.hpp` is a hard `#error`. Set the numeric ctest `TIMEOUT` **explicitly** rather than inheriting `add_threading_test`'s default 120 (`tests/session/CMakeLists.txt:92`): this target hosts B4 and B5, whose mutants hang rather than fail, and D-5's own reasoning for the T2b target is that a target whose regression mode is a hang "may not inherit a default silently"
- [X] T010 Add the 088 ctest `LABELS` to every target this feature adds, per research D-5's label table, and **include the T6 target** — D-5's table omitted it at round 3, so `ctest -L 088` was not guaranteed to include the only cell pinning FR-018. **Scope note added at /implement:** this is a **running obligation, not a Phase-2 task.** Only one of D-5's four targets exists after T009 (`session_read_first_frame_bounded`, labelled there); the other three are created later — `session_first_frame_stop` (T022), `session_first_frame_total_cancel_tls` (T024) and `transport_timer_epoch_retire` (T036), each of which must apply its own D-5 row at creation. **T010 closes only after T036**, and its closing check is `ctest -N -L 088` listing **exactly four** targets. Ticking it earlier would claim label coverage for targets that do not yet exist
- [X] T011 Add a **dedicated RED-proof ctest label** and use it in `specs/088-firstframe-budget-timer-lifetime/quickstart.md` §4 in place of the `ctest -R <exe-name>` invocations, which violate Article VII §8's label-only rule; correct quickstart's *"two cases §8 explicitly allows"* to one *(carried Gate A obligation 2)*

---

## Phase 3: User Story 1 — A valid Logon at the budget boundary establishes a session (Priority: P1) 🎯 MVP

**Goal**: a complete, in-budget Logon at or past the boundary is admitted, not dropped.

**Independent test**: drive an accepted connection with a single write whose cumulative size sits at
the boundary and begins with a complete Logon; assert the session establishes and surplus bytes are
carried into the read pump.

### Tests for User Story 1 (RED-first — prove each against its named mutant BEFORE T019)

- [X] T012 [US1] Write cell **B1** (SC-001) in `tests/session/read_first_frame_bounded_test.cpp` — single delivery, cumulative **exactly** `max_bytes`, complete Logon at its head. Prove RED against the `>=` retained mutant and capture the failure text
- [X] T013 [US1] Write cell **B2** (SC-012) in `tests/session/read_first_frame_bounded_test.cpp` — **fragmented** via `inbound_chunks = {1000 B, 3097 B}`, Logon ending at byte 3500, cumulative 4097. **This is the only discriminating cell for the budget**: prove it RED against **all three** of budget-before-frame, `carry@max_bytes` (D-1a) and `>=` retained — **escalated in the verify record's RED-proofs §B2: not achievable from one run against `main`; only the ordering defect REDs pre-fix, the other two are discharged at T019 against mutants of the delivered design**
- [X] T014 [US1] Write cell **B3** (SC-002) in `tests/session/read_first_frame_bounded_test.cpp` — B1's delivery plus surplus; assert the returned length is **3500 exactly**. Prove RED against the `return buf.size()` mutant
- [X] T015 [US1] Write cell **B5** (edge / FR-013) in `tests/session/read_first_frame_bounded_test.cpp` — `inbound_chunks = {max_bytes, 1}`, `deadline = 50 ms`, **non-zero `read_latency = 3 ms`**, `ioc.run()`. Assert the **sequence** `read_sizes() == {max_bytes, 1}` and the outcome `wire_frame_too_large`. **The mutant RED MUST be shown to TERMINATE** and the termination captured in the verify record — as specified at round 3 this cell *hung* on the `room == 0` mutant; the non-zero latency is what makes termination reachable *(carried Gate A obligation 3)*. Do not use a latency that divides the deadline. **The target MUST carry a numeric ctest `TIMEOUT`** as second-line defence — the non-zero latency makes termination reachable, the `TIMEOUT` is what stops a regression in that reasoning from burning a CI job

### Implementation for User Story 1

> **⚠️ T016 and T017 are ONE atomic edit wearing two task IDs — land them together.** *(Established
> at /implement.)* Neither half yields a coherent state, and **B2 cannot go green against either
> one alone**:
> - **T016 without T017** — the loop is reordered and the clamp admits `max_bytes + 1 = 4097`, but
>   `carry` is still `pmr_carry_buffer{max_bytes}`, so the framer rejects at 4097 **before any
>   parse**. B2 then fails for a framer-capacity reason wearing the costume of a budget failure.
> - **T017 without T016** — `carry` is `max_bytes + 1` but the `>=` check still precedes
>   `framer.feed`, so B2 still fails on budget-before-frame.
>
> This is D-1a's own point: the C1 clamp and the carry capacity are a single decision. Do not take
> any RED or GREEN reading against a half-applied state. The four Phase-3 cells (B1/B2/B3/B5) all
> flip from RED to GREEN at this one edit — see the expected-failure ladder in
> `.specify/decisions/088-firstframe-budget-timer-lifetime-verify.md`.

- [X] T016 [US1] Rewrite the loop in `src/session/read_first_frame_bounded.hpp` to D-1's order: clamp → joined read → insert → **feed** → frame-wins return → **single** budget check with strict `>` at the **foot** of the body (FR-001 / FR-002 / FR-007 / SC-007)
- [X] T017 [US1] Derive the framer's carry capacity from the C1 clamp — `pmr_carry_buffer carry{max_bytes + 1, …}` (SC-013) in `src/session/read_first_frame_bounded.hpp` (D-1a). Without this the framer rejects at 4097 **before any parse**, so B2 is RED under the delivered design and FR-007's decision point is unreachable
- [X] T018 [US1] (SC-007) Delete the unreachable duplicate budget check at the loop top (was `src/session/engine.cpp:408-411`) — FR-007 requires exactly one budget decision point; it is deleted, not relocated
- [X] T019 [US1] Run B1/B2/B3/B5 green and re-run the mutants to confirm each cell now passes and each mutant still discriminates per the D-6.8 matrix. **Scope sharpened at /implement — T019 owns two mutant columns that T013 could not reach.** The pre-fix REDs captured at T012-T015 exercise the matrix's **`>=` retained** column only (that column is, in effect, the pre-fix source — which is why the matrix marks B1/B2/B3/B5 RED in it, matching the four captured REDs exactly). B2's other two columns are mutations of the **delivered** design and are unreachable against `main`: **(a) `comparison-only`** — apply the strict `>` but leave the budget check *before* `framer.feed`; B2 must go RED (4097 > 4096 fires before the frame at 3500 is seen). **(b) `carry@max_bytes`** — restore `pmr_carry_buffer carry{max_bytes}` while keeping the delivered order; B2 must go RED because feeding chunk 2 accumulates 4097 into a 4096-capacity carry and the framer rejects **before any parse**. Neither is optional: `carry@max_bytes` was Gate A round 1's own headline empty column, and D-6.8 records that it *"had no RED cell at all"* before the D-1a correction — leaving it unexercised would restore exactly that hole

---

## Phase 4: User Story 2 — A session that establishes as the deadline fires is not torn down (Priority: P1)

**Goal**: no stranded deadline handler survives the call; `Engine::stop()` aborts promptly on every
transport, including TLS.

**Independent test**: force the same-drain selection deterministically, run under ASan and TSan, and
assert no write-to-freed and no `cancel()` from any handler the call armed.

### Tests for User Story 2 (RED-first)

> **⚠️ RED-basis differs in this phase.** T2a's and T2b's named mutants are mutations of the
> **delivered** `||`-join construction (T026–T028), **not** of pre-fix `main`. Write the cells here,
> but take their RED proof **after T028 lands** — mutating code that does not yet exist is the
> D-6.2 failure this bundle already hit once (a 0 ms deadline with an inline read reaches
> `timer.cancel()` before the scheduler runs, so the RED never fires and a clean run is recorded as
> "no finding"). T6 (T023/T025) already splits write-from-prove for the same reason.

- [X] T020 [US2] Write cell **T1** (SC-005 / SC-006) in `tests/session/read_first_frame_bounded_test.cpp` using D-6.2's **elapse-then-poll** construction — `deadline = 10 ms`, `read_latency = 1 ms`, `poll()`, then `sleep_for(50 ms)` so both timers expire with no handler running. Assert via `cancels_observed()` that **zero** `cancel()` calls occurred. Prove RED against the stranded-handler mutant under ASan
- [X] T021 [US2] Write cell **T2a** (SC-015 / FR-015) in `tests/session/read_first_frame_bounded_test.cpp` — `co_spawn` an **outer wrapper coroutine** whose first statement is `co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation())` and which then awaits the helper, bound to a test-owned signal via `bind_cancellation_slot`. **The outer reset is mandatory**: `co_spawn`'s initial state is terminal-only, so without it the test's `total` is discarded before reaching the join and the cell fails for the wrong reason. Assert a **cancellation-attributable error set**, not the exact `transport_read_cancelled` — under the delivered design both arms are cancelled together and `order[0]` decides which error surfaces. **Prove RED against D-2's trap — a bare `timer.async_wait(use_awaitable)` deadline arm (research D-6.1/D-6.7) — taken AFTER T028**, since the mutant is of the delivered join
- [X] T022 [US2] Write cell **T2b** (SC-015 accept-slot leg) in a NEW `tests/session/first_frame_stop_test.cpp` — #232's mTLS harness: peer completes the handshake then sends nothing, then `Engine::stop()`; assert the accept slot is reclaimed and `stop()` returns within the promptness threshold, with a ctest `TIMEOUT`. **T2b does NOT discharge non-vacuity** — record that explicitly; T2a and T6 carry it. **Prove RED against an arm that outlives `stop()` at engine scope (research D-6.1/D-6.7) — taken AFTER T029**, since the mutant is of the delivered join plus FR-018
- [X] T023 [US2] Write cell **T6** (SC-018 / FR-018) in a NEW `tests/session/first_frame_tls_cancel_test.cpp` driving `LoopbackTlsFixture` (`tests/transport/loopback_tls_fixture.hpp`), as **one cell with two legs**: leg A the joined helper (watchdog, promptness, ordering-robust error *class*), leg B `async_read_some` driven directly with no join, where the exact `transport_read_cancelled` **is** assertable. Placed under `tests/session/` because the configure-time fixture-wiring precedent is `tests/session/CMakeLists.txt:837-843`. **Filename corrected at /implement**: `first_frame_total_cancel_tls_test.cpp` per research.md D-6.13b / D-5's label table, not this task's `first_frame_tls_cancel_test.cpp` — see `.specify/decisions/088-firstframe-budget-timer-lifetime-verify.md`
- [X] T024 [US2] Wire T6's target in `tests/session/CMakeLists.txt` with `FIXPP_TLS_FIXTURE_DIR` set **unconditionally at configure time** (never a runtime env var — the harness precedent `GTEST_SKIP`s when unset, and a skip is a false pass), a numeric ctest `TIMEOUT`, and a **test-owned watchdog** that converts the un-mapped build's hang into a deterministic assertion failure. A run ending in a ctest `TIMEOUT` instead means the watchdog is mis-wired — a broken cell, not a RED proof
- [X] T025 [US2] Prove T6 **RED against the un-mapped build** before T029 lands, and capture the failure text. **No revert edit is required if T025 precedes T029**: the tree is *already* at the one-argument `reset_cancellation_state(enable_total_cancellation())` at `src/transport/asio_tls_transport.cpp:1134` (verified at /implement), so the un-mapped build IS the current build. Only if T029 has already landed does this become a temporary revert — and such a revert must never reach the committed diff. **A mock-driven cell cannot substitute**: `mock_transport`'s read is a `steady_timer`, which honours `total`, so every mock cell is green exactly where a real `ssl::stream` hangs

### Implementation for User Story 2

- [X] T026 [US2] Arm the deadline timer **exactly once**, with an absolute expiry, **before** the loop in `src/session/read_first_frame_bounded.hpp`; `await_deadline` never calls `expires_after` (FR-017 / D-1b) — a per-iteration re-arm silently removes the deadline with every existing test green
- [X] T027 [US2] Replace the raw `timer.async_wait(...)` + `transport.cancel()` with the `||` join in `src/session/read_first_frame_bounded.hpp`, the deadline arm wrapped in a coroutine that resets to `enable_total_cancellation()` first (FR-005 / FR-006 / D-2)
- [X] T028 [US2] Give the deadline arm `asio::redirect_error(asio::use_awaitable, ec)` in `src/session/read_first_frame_bounded.hpp` (D-3) — a bare `use_awaitable` arm throws on **every established connection**, and no-throw is what makes `outcome.index()` a sound discriminator
- [X] T029 [US2] Change the one-argument reset at `src/transport/asio_tls_transport.cpp:1134` in `async_read_some` to the **two-argument** OUT-mapping form, mapping any non-`none` cancellation to `terminal`, mirroring the `async_connect` precedent at `:918-933` **including its commenting discipline** (in-source reason + both `[[feedback_*]]` mnemonics) — FR-018. Without it `Engine::stop()`'s `total` dies in the SSL composed op's terminal-only inner state and `stop()` hangs unboundedly on the default accept path. **The fix is not expressible at the call site** — `reset_cancellation_state` replaces the bottom-frame state, last reset wins. Do **not** touch the plain transport: its socket op already honours `terminal|partial|total`
- [X] T030 [US2] Run T1/T2a/T2b/T6 green under ASan and TSan and re-run every mutant

---

## Phase 5: User Story 3 — Genuinely over-budget peers are still closed (Priority: P1, regression guard)

**Goal**: the fix narrows *when* the budget fires, never *whether* it fires.

**Independent test**: the existing over-budget witness from PR #232 stays green, joined by a pin that
the new accept path does not admit an unbounded incomplete stream.

### Tests for User Story 3 (RED-first)

- [X] T031 [P] [US3] Write cell **B4** (SC-003) in `tests/session/read_first_frame_bounded_test.cpp` — over-budget with **no** complete frame ever (a declared BodyLength that never completes). Prove RED against over-relaxation of FR-003. **The mutant RED MUST be shown to TERMINATE**, with the termination captured in the verify record — B4 is `read_latency`-silent as specified and hangs on the `room == 0` mutant exactly as B5 did; give it the same non-zero-latency treatment, **and the same numeric ctest `TIMEOUT`** *(carried Gate A obligation 3)*
- [X] T032 [US3] Write cell **B6** (SC-004 / D-1b) in `tests/session/read_first_frame_bounded_test.cpp` — `max_bytes = 200`, `deadline = 50 ms`, `read_latency = 7 ms`, `inbound_chunks` = **201 chunks of 1 byte**. Reads complete at 7, 14 … 49 ms; the 8th is in flight when the deadline expires at 50 ms with `buf.size() == 7 ≪ 200`. **7 ms is load-bearing** — the two timer series MUST NOT share a common multiple inside the deadline window, or the cell depends on ordering this bundle refuses to depend on. Prove RED against the `timer re-armed per iteration` mutant. **DELIVERED DEVIATION from D-6.11, intentional — reconcile the doc before Gate B.** D-6.11 pins the assertion as `buf.size() == 7`; the delivered cell instead asserts the **band** `>= 1 && < max_bytes`. Reason: the exact value rests on the 49 ms/50 ms gap, a **<2% margin**, which would false-RED under ASan/TSan instrumentation or CI load — the recorded `[[feedback_timing_band_witness_range_admits_the_mutant_it_claims_to_kill]]` hazard read in the other direction. **The band is safe against exactly that anti-pattern**: the `timer re-armed` mutant drains all 201 chunks, giving `buf.size() == 201` and `wire_frame_too_large`, which fails the upper bound **and** the error assertion independently; the lower bound `>= 1` keeps it non-vacuous (a deadline firing before any read completed gives 0 and fails). `spec.md` SC-004 names no value, so **only research.md D-6.11 drifts** — annotate it there, do not re-point it silently. **Filler-byte correction, also at /implement:** the payload must still begin `8=` — `Framer::parse_frame` (`src/wire/framer.cpp:73-110`) rejects any buffer that does not, producing `wire_framing_resync` rather than the deadline path. Arbitrary `'X'` filler makes the cell fail for the wrong reason. **Helper constraint discovered at /implement:** `make_logon_of_length()` in `read_first_frame_bounded_test.cpp` self-checks that the BodyLength is exactly **4 digits** and `ADD_FAILURE()`s otherwise, so it **cannot build a complete frame below ~1024 bytes**. B6's `max_bytes = 200` is far under that — but B6 does not need a *complete* frame (its whole point is that the deadline fires with `buf.size() == 7 ≪ 200`), so use arbitrary filler bytes and do **not** reach for that helper. Same applies to B4 (T031), which also needs a never-completing payload
- [X] T033 [P] [US3] Confirm `tests/session/engine_firstframe_test.cpp` is **byte-identical to `main`** — FR-011 requires the pre-existing over-budget witness stay unmodified; a diff here is a finding, not a fixup

### Implementation for User Story 3

- [X] T034 [US3] Confirm the strict `>` at the foot of the loop in `src/session/read_first_frame_bounded.hpp` still closes an over-budget peer with `wire_frame_too_large` and reclaims the accept slot (FR-003), and that B4/B6 pass without relaxing FR-014's protective intent

---

## Phase 6: User Story 4 — Transport connect/handshake timers do not cancel a socket that succeeded (Priority: P3)

**Goal**: close the same late-handler shape at all three transport timer sites — a class-fix scoped
to one occurrence is the pattern that returns at Gate B.

**Independent test**: per transport, assert the attempt's timer epoch has been retired by the time
the connect (resp. handshake) returns, so any expiry still in flight is stale.

### Tests for User Story 4 (RED-first)

- [X] T035 [P] [US4] Write cell **T3** (SC-014, plain connect) in `tests/transport/test_asio_plain_transport.cpp` — drive a real successful connect against a loopback stub peer; assert `timer_epochs()->connect` has **advanced past** the value armed for that attempt. Prove RED against the retire-point-omitted mutant
- [X] T036 [P] [US4] Write cells **T4** (TLS connect) and **T5** (TLS handshake) in a NEW `tests/transport/test_asio_tls_transport_timer_epochs.cpp` — same shape against `timer_epochs()->connect` and `->handshake`, and wire the target in `tests/transport/CMakeLists.txt` with the 088 labels from T010. Resolves `plan.md:240`'s `(TLS transport test target)` placeholder, which `/tasks` owns: no existing file fits — `test_asio_tls_transport_error_paths.cpp` is wired at `tests/transport/CMakeLists.txt:339` but scoped to error paths, and the other TLS files (pinset rotation, CompID identity binding, validation taxonomy) are unrelated. **C4 requires one pin per site**, so keep T4 and T5 as two distinct cells in the one file. Prove each RED against the retire-point-omitted mutant

### Implementation for User Story 4

- [X] T037 [P] [US4] Add `timer_epoch_state` + a `std::shared_ptr` member + a user-provided destructor whose **body** retires every counter + a `timer_epochs()` const accessor to `src/transport/asio_plain_transport.hpp` (D-4.1). The epoch lives in **shared state the handler owns by value** — a plain member would be read through a dangling `this`
- [X] T038 [P] [US4] Add the same three additions to `src/transport/asio_tls_transport.hpp`, with **two** counters (`connect` and `handshake`) — split per timer even though one would be correct against today's callers, so correctness does not rest on a caller sequencing property the transport cannot enforce
- [X] T039 [US4] Add the shared-epoch guard at the connect timer in `src/transport/asio_plain_transport.cpp:130`, retiring immediately before the existing `timer.cancel()` at `:150` (FR-014 / FR-009)
- [X] T040 [US4] Add the shared-epoch guards at `src/transport/asio_tls_transport.cpp:910` (connect, retire before `:941`) and `:1032` (handshake, retire before `:1045`) — FR-014 / FR-009
- [X] T041 [US4] Run T3/T4/T5 green and confirm the delivered census in `spec.md` matches the four enumerated sites fixed (SC-008)

---

## Phase 7: Polish & Cross-Cutting Concerns

- [ ] T042 [P] Correct the **five** production comments that state the delivered contract, per research D-8 — including the 015 `/simplify` Q-2 rationale and the new carry-capacity derivation — in `src/session/read_first_frame_bounded.hpp` and `src/session/engine.cpp` (FR-008 / SC-011). **Sixth site added at /implement, outside D-8's production-only enumeration:** the header comment of `tests/session/read_first_frame_bounded_test.cpp` (~lines 12-22, written at T012) still describes the **budget-before-frame** defect as the current shape of the tree, which T016-T018 have since fixed. Flagged by the T020 implementer, deliberately not self-fixed. It is a *test* file so D-8 does not cover it, but a stale comment asserting a defect that no longer exists is exactly the doc-drift Gate B reads as a finding
- [ ] T043 [P] Confirm the public surface delta is **empty**: no header change, no new or removed error code, no C-ABI change (FR-012 / SC-010). `mock_transport.hpp` is excluded from the install set at `CMakeLists.txt:446-451`, which is what preserves SC-010/SC-017 — verify that exclusion still holds
- [ ] T044 Run the **full sanitizer ctest matrix** (ASan / UBSan / TSan) and confirm 0 findings (SC-009). Treat every finding as a real defect until disproven with a reproduction or a client-path analysis
- [ ] T045 Replace T2a/T2b's wall-clock promptness bounds with a **test-owned intermediate-timer / ordering construction**, and **reconcile or narrow SC-016 in `spec.md` in the same edit** — the contradiction is in the *binding* criterion: SC-016 says no session-layer witness depends on winning a timing race and names SC-015, while SC-015's thresholds are normative; SC-016 also still attributes elapse-then-poll to T2a, which round 3 replaced with a signal-driven wrapper *(carried Gate A obligation 1)*. **How to resolve it, established at /implement — do NOT read the tension as "the cell cannot be built" and descope it.** Per D-6.12b the bare-arm mutant **does not change the returned error value**, so a value-only assertion cannot kill it and **promptness is necessarily the discriminator** — dropping the timing dimension entirely would leave T2a/T2b unable to kill their only mutant. The fix is not to abandon latency but to make the comparison **test-owned and deterministic**: arm an intermediate timer the test controls and assert `stop()` completes *before that timer fires*, instead of comparing elapsed wall-clock against a fixed 100/500 ms threshold. That is still a promptness check, so SC-015's bound survives, but it is no longer a race the test has to win — which is exactly what SC-016 demands. Note also D-6.12b's round-3 correction: a bare arm costs `stop()` the **whole deadline (bounded, equal to the pre-fix tail)**, not an unbounded hang — the unbounded failure is the read arm's and belongs to FR-018; `plan.md`'s D-2 row calling it *"a regression worse than the defect being fixed"* was the overstatement that correction withdrew
- [ ] T046 [P] File a follow-up issue for a **real near-side initiation barrier for T2b** — a seventh D-9 mechanism (an instrumented accepted transport recording read *initiation*), deliberately not bought at Gate A round 4; filed against SC-015 *(carried Gate A obligation 4)*
- [ ] T047 [P] File a follow-up issue for the `include/fixpp/transport/test/mock_transport.hpp:114-117` class-doc overstatement — it claims *"All async methods compose an `asio::post(exec_)` checkpoint"* when only `async_connect` (`:155`) has one; three of four async methods have none *(carried Gate A obligation 5)*
- [ ] T048 Run `/speckit-verify` and record every mutation RED — including **B4's and B5's proven termination** — in `.specify/decisions/088-firstframe-budget-timer-lifetime-verify.md`. Note `/speckit-verify` is clang-only locally; gcc-release and MSVC are CI-only

### Mandatory close-out tasks (ALWAYS emit — Gate-B preconditions, Article XVII §8)

- [ ] T049 Catalogue close-out — flip every feature-owned OFFICIAL row in `spec/feature-catalogue.md` to `done` and add the corresponding `spec/coverage-index.md` entry. **Plus one cross-feature row added at /implement:** record in `spec/behaviors-and-limitations.md` that 088 widens **`asio_plain_transport`'s two constructors from `noexcept` to potentially-throwing**. Cause: D-4.1's `timer_epoch_state` mechanism requires a `std::make_shared` default member initializer, and `make_shared` can throw `bad_alloc` — which inside a `noexcept` constructor would **terminate**, making the declaration false. The type is **internal** (`src/transport/` is not an installed include root; it is named in no installed header and no C-ABI surface), so **no installed surface, no ABI boundary and no C-ABI symbol moves** — SC-010/SC-017 are unaffected. The `trap_throw` try/catch already wired at `transport_factory.cpp:513-522` and `:539-549` becomes non-vacuous; it needed no change. `specs/043-plaintext-tcp-transport/contracts/asio_plain_transport.hpp:44,49` is **deliberately left unmodified** — it is the historical record of what 043 shipped and is superseded on this one point, not falsified
- [ ] T050 **Feature-completeness audit (FINAL task)** — verify tasks ↔ FR/SC ↔ catalogue all map to a landed test **and** a landed implementation; record the verdict (100%-or-waived) in `.specify/decisions/088-firstframe-budget-timer-lifetime-verify.md` under `## Completeness`. `/gate-b` 4d **HARD-BLOCKS** without this record

---

## Dependencies & Execution Order

### Phase Dependencies

- **Phase 1 (Setup)** → no dependencies
- **Phase 2 (Foundational)** → blocks **all** user stories. T005/T006 (mock mechanisms) block every
  cell that scripts an I/O shape; T008 (internal header) blocks every direct-helper cell; T009/T010
  block running any of them by label
- **Phase 3 (US1)** → depends on Phase 2. MVP.
- **Phase 4 (US2)** → depends on Phase 2. Independent of US1 in principle, but T016/T026/T027 all edit
  `read_first_frame_bounded.hpp`, so sequence US1's loop rewrite before US2's timer work
- **Phase 5 (US3)** → depends on Phase 2, on T016 (the strict `>` it guards) **and on T026** — B6's mutant is the delivered arm-once timer, so its RED must be taken against T026, not against pre-fix `main`
- **Phase 6 (US4)** → depends on Phase 2 only. Independent of US1–US3 *behaviourally*, and it can run
  alongside Phases 3 and 5. **But it is NOT file-disjoint from Phase 4**, as this bullet originally
  claimed: **T040 and T029 both edit `src/transport/asio_tls_transport.cpp`** (T040 at `:910`/`:1032`,
  T029 at `:1134`). Serialize those two, or give them to a single implementer — never fan them out
  concurrently
- **Phase 7 (Polish)** → depends on all stories

### User Story Dependencies

- **US1 (P1)** — independent; the MVP
- **US2 (P1)** — independent of US1's *behaviour*, shares its file
- **US3 (P1)** — regression guard over US1's change
- **US4 (P3)** — fully independent; separate files, separate cells

### Parallel Opportunities

- **Phase 2**: T003–T006 all edit `mock_transport.hpp`, so none is marked `[P]` — author in parallel if you like, but the edits serialize. T005 → T006 in order
- **Phase 3**: T012–T015 all edit `read_first_frame_bounded_test.cpp`, so none is marked `[P]`; T013 and T015 are the nontrivial constructions and should land last
- **Phase 6**: T035, T036 in parallel; T037, T038 in parallel (different files)
- **Across phases**: Phase 6 can run alongside Phases 3 and 5. **Not alongside Phase 4** — T029 and
  T040 share `src/transport/asio_tls_transport.cpp`

---

## Implementation Strategy

**MVP = User Story 1** — the boundary Logon is admitted. That alone closes the interoperability half
of #233 and is independently shippable.

**Increment 2 = User Story 2** — the timer-lifetime half, including FR-018. Note this increment
carries the only *unbounded* failure in the feature: without T029, `Engine::stop()` hangs forever on
the default TLS accept path, which is strictly worse than the defect being fixed. **T029 and T023/T025
must land together** — the fix and its only usable witness.

**Increment 3 = User Story 3** — the regression guard. Cheap, and it is what stops the fix from
relaxing FR-014.

**Increment 4 = User Story 4** — the transport class-fix. Lowest priority, fully parallelizable, and
the one most likely to be split out if scope pressure appears — but a class-fix scoped to one
occurrence is this project's recorded anti-pattern, so split the *feature*, never the *census*.

**Standing discipline for this feature**: every cell RED-first, every mutant killing exactly one
cell, and **every mutant RED shown to terminate**. A non-terminating loop is indistinguishable from a
wrong assertion in a CI log.
