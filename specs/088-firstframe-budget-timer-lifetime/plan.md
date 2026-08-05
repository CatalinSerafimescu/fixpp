# Implementation Plan: Bounded first-frame read — budget boundary + deadline-timer handler lifetime

**Branch**: `088-firstframe-budget-timer-lifetime` | **Date**: 2026-08-04 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/088-firstframe-budget-timer-lifetime/spec.md`

**Tracking issue**: [#233](https://github.com/CatalinSerafimescu/fixpp/issues/233)

## Summary

Correct two pre-existing production defects on the acceptor pre-session path so the implementation
matches the FR-014/SC-011 contract feature 015 already shipped, and fix the same defect *class* at
the three transport timer sites the census found.

1. **Budget boundary** — `read_first_frame_bounded` rejects at `>=` (FR-014 says *exceeds*) and
   rejects **before** framing, so a chunk already holding a complete Logon is discarded. Delivered
   invariant: *frame each chunk first, a complete frame wins unconditionally, and reject only when no
   frame is extractable and the peer has exceeded the budget* — with each read clamped to
   `max_bytes + 1 - buf.size()` so the reordering **tightens** the DoS bound to 4097 rather than
   widening it to 8192.
2. **Timer-handler lifetime** — the deadline `async_wait` handler captures coroutine-frame locals by
   reference, and `timer.cancel()` cannot un-queue an already-completed handler. Replaced by a joined
   read/deadline race (`awaitable_operators`' `||`), which retires both arms before the coroutine can
   return. This closes the write-to-freed leg *and* the sharper leg — a late `transport.cancel()`
   tearing down a session already established at `engine.cpp:922`.
   **Amended at Gate A round 2 — the join alone is NOT correct under `Engine::stop()` on TLS.**
   `stop()`'s `cancellation_type::total` is accepted and re-emitted unchanged by the transport's
   one-argument cancellation reset and then **silently discarded inside asio's SSL composed
   operation**, whose own cancellation state is terminal-only. The read arm never retires, so the
   group never completes and `stop()`'s deadline-less step-3 spin (`engine.cpp:1342-1353`) hangs
   **unboundedly** — worse than the pre-fix 5 s bound, which the same signal also destroys by retiring
   the deadline arm and consuming the group's one-shot cancel guard. The remedy is **FR-018**: a
   **two-argument** `reset_cancellation_state(enable_total_cancellation(), <non-`none` → `terminal`>)`
   inside `asio_tls_transport::async_read_some`, mirroring the same file's `async_connect` at
   `:918-933` (016 T008). It **cannot** be done at the call site — `reset_cancellation_state` replaces
   the single bottom-frame state and the last reset wins (`asio/impl/awaitable.hpp:726-732`). Plain
   TCP is unaffected. Witnessed by **SC-018 / cell T6**, on a **real** TLS transport, because every
   mock-driven cell is structurally green here. Research §D-2a.
3. **Class-fix** — the same stale-handler shape at three transport timer sites, closed with an
   attempt-epoch guard rather than a join (both connect sites sit behind a deliberately-shaped OUT
   cancellation filter a join would have to re-plumb). **Corrected at Gate A round 1:** those sites
   *do* have a dangle leg — `reconnect_fsm.cpp:250-252`/`:284-286` and `engine.cpp:841-844` all
   destroy the transport synchronously on the failure arm — so the epoch is held in **shared state the
   handler owns by value**, not in a member that would be read through a dangling `this`.

Public surface delta: **empty**. No installed header change, no error code, no ABI change. One new
*internal* header under `src/session/`, and the two *internal* transport headers under
`src/transport/` gain one member and one destructor each. None of `src/` is an installed include
root.

## Technical Context

**Language/Version**: C++23 (coroutines, `std::expected` via `core::expected_t`)

**Primary Dependencies**: `asio/1.38.0` — verified against `conanfile.py:67` for this feature, not
propagated from an anchor doc. OpenSSL via `asio::ssl` on the TLS transport. **No new dependency.**

**Storage**: N/A — pre-session path, no `Session`, no message store.

**Testing**: GoogleTest + ctest; `mock_transport` (lower-case — the class is
`fixpp::transport::test::mock_transport`, `include/fixpp/transport/test/mock_transport.hpp:126`;
earlier drafts of this plan wrote `MockTransport`, which does not exist), `FIXPP_ALLOW_MOCK_TRANSPORT`-gated,
whose contract already binds it to honour cancellation rather than short-circuit it (`:21-27`);
hand-driven `io_context` for the same-drain witnesses. **Four** test-only additions to
`mock_transport`, all priced in research §D-9 *(two at round 1, two more at Gate A round 3)*: a
`cancels_observed()` counter (contract S5) and an `async_reads_observed()` counter (T2a's
non-vacuity observable), both mirroring the mock's existing `writes_observed_` (`:321`) /
`async_writes_observed()` (`:311`) pair — the header has **no read counter today**, verified against
it rather than propagated; plus a **chunked `Script::inbound_chunks` inbound** and a **per-read
requested-size observable** (`read_sizes()`), **without which B2, B5 and B6 are not constructible at
all** (research §D-9's overturned claim, re-derived in §D-6.11). Plus **one real-TLS cell (T6)** built
on the pre-existing `tests/transport/loopback_tls_fixture.hpp`, which adds no test-only production
surface of its own.

**Target Platform**: Linux (Clang 22 local + CI per Article XVII §7); gcc-release and MSVC CI-only.

**Project Type**: C++ library (`fixpp`) — session and transport layers.

**Performance Goals**: None new. Not a perf change — Article VIII §3's "no perf change without a
bench in the same PR" is not triggered. **The grounds were re-derived at Gate A round 2 and are no
longer "the accept path is cold"**: FR-018's OUT map lands in `asio_tls_transport::async_read_some`,
which the source itself declares hot (`asio_tls_transport.cpp:401-403`). The disposition now rests on
a **zero-delta** argument — no allocation change, and the cancellation filters execute only when a
signal is delivered, never on a completing read. See the Constitution Check §3 row and research §D-7.

**Constraints**: Worst-case pre-session **logical** buffered bytes `max_bytes + 1` = 4097 (SC-013
bound 2 — tighter than the pre-fix logical maximum of **8191**, not of 4096); peak **resident** bytes
≈ 12 KiB across `buf` + `carry` + `read_buf` (bound 3); the framer's carry capacity must be **derived
from** the same bound, `>= max_bytes + 1` (FR-013 / research §D-1a) — the constraint that the first
draft missed and that makes SC-012 and FR-007 deliverable at all; deadline unchanged at 5000 ms and
armed exactly once (FR-017); `Engine::stop()`'s `cancellation_type::total` must still abort an
in-flight first-frame read promptly (FR-015) **on both transports — which on TLS requires FR-018's
two-argument OUT-mapping reset in `asio_tls_transport::async_read_some`, without which the joined form
does not merely fail to be prompt, it hangs `stop()` unboundedly** (added at Gate A round 2; research
§D-2a).

**Scale/Scope**: 3 production source files changed; the **2 internal transport headers** under
`src/transport/` gain one `std::shared_ptr<timer_epoch_state>` member, one user-provided destructor
and one const accessor each; 1 new internal header under `src/session/`; new witness targets in
`tests/session/` and `tests/transport/`; **4 test-only additions to `mock_transport`** (two counters,
one chunked `Script` inbound, one per-read requested-size observable). **Exactly one file under
`include/` is touched — `include/fixpp/transport/test/mock_transport.hpp`** *(corrected at Gate A
round 3; the previous "No `include/` file is touched" contradicted this plan's own Project Structure
tree)*. That header is `FIXPP_ALLOW_MOCK_TRANSPORT`-gated **and excluded from the install set by
construction** — `CMakeLists.txt:446-451` installs `include/` with
`PATTERN "fixpp/transport/test" EXCLUDE` — so **SC-010 and SC-017 hold unchanged**, verified rather
than assumed. No codegen, no schema, no generated artifacts, no dictionary.

## Constitution Check

*GATE: evaluated before Phase 0 and re-evaluated after Phase 1 design. Both passes are recorded.*

| Article | Applies? | Disposition (pre-Phase-0) | Re-check (post-Phase-1) |
|---|---|---|---|
| **XVII §1 — Gate A trigger** | **YES** | Touches the concurrency / cancellation / executor model on the accept path. Gate A is **mandatory**; there is no waiver path — §6's auto-waive covers comment-only edits, which this is not, and that is exactly why PR #232 deferred these defects rather than fixing them. | Unchanged. `/gate-a` runs after this plan and **before** `/speckit-tasks` (`.specify/pipeline.md` step 4). |
| **XVII §7 — local pre-PR build gate** | YES | `linux-clang-debug` Conan install + configure + build + ctest before any PR, with the confirmation line in the PR body. Resource gate honoured — the build is surfaced for user approval, never auto-run. | Unchanged. |
| **XVII §8 — verification gate** | YES | `/speckit-verify` mandatory after `/speckit-implement`; record at `.specify/decisions/088-firstframe-budget-timer-lifetime-verify.md` before `/gate-b` will start. | Unchanged. |
| **VIII §5 — allocator policy on the hot path** | **NO — analysed, not assumed** | The ban is "between parse and the `fromApp` callback", i.e. the established-session read pump. This path runs once per accepted connection, before any `Session` exists, and already allocates there today (`pmr_carry_buffer`, `engine.cpp:402`). | Unchanged: §5's scope does not reach this path. The allocation *delta* is dispositioned under XI §6 below, where it belongs. |
| **XI §6 — coroutine frame allocation (HALO-first, PMR fallback)** | **YES — added at Gate A round 1; the first draft had no row** | `.specify/constitution.md:236`. MANDATORY. HALO **cannot** fire for the two `co_spawn(ex, …, deferred)` arms `operator\|\|` creates (`asio/experimental/awaitable_operators.hpp:344-347`) — `co_spawn` type-erases the awaitable into a separately-scheduled operation, so there is no caller frame to elide into. §6's fallback clause is squarely engaged. | Post-design delta, re-enumerated (research D-7): **(1)** read-arm `co_spawn` frame, **(2)** deadline-arm `co_spawn` frame, **(3)** the `operator\|\|` frame itself (may HALO), **(4)** `parallel_group`'s `std::allocate_shared<state_type>` (`asio/experimental/impl/parallel_group.hpp:371-374`), **(5)** one `make_shared<timer_epoch_state>` per transport. Items 1–4 recur **per loop iteration**, bounded at `max_bytes + 1` iterations by FR-013. A sixth — a thrown-and-captured `system_error` on *every* established connection — existed in the first draft's design and is **removed** by D-3's `redirect_error`. **Two corrections at Gate A round 2 (research §D-7):** *(i)* the frame count is **four, not three** — `await_deadline`'s **own** coroutine frame is distinct from the `co_spawn` entry frame that carries it, and was omitted; *(ii)* item 4 and the small handler emplaces (`co_spawn_cancellation_handler` ×2, `asio/impl/co_spawn.hpp:326-328`) go through **asio's thread-local recycling pools** (`parallel_group_tag` at `parallel_group.hpp:371-374`; `cancellation_signal_tag` at `asio/impl/cancellation_signal.ipp:52-79`), so the steady-state **global-heap** delta is **~3–4 coroutine frames per join**, not 8+ raw `new`s — the round-1 table over-priced the join by roughly 2×. **FR-018 adds ZERO to this** — a same-size, same-alignment filter-pair emplace at a reset that already happens on every read. **Disposition: explicit deviation.** No per-awaiter PMR fallback is specified: `co_spawn` uses the executor's associated allocator, and `asio::any_io_executor` has none PMR-backed in this project; introducing one would exceed a defect fix. Grounds: per-accepted-connection, bounded by a constant, on a path that already allocates. |
| **VIII §3 — no perf change without a bench** | NO — **but the round-1 GROUNDS are withdrawn** | Not a perf change; no benchmark covers the accept path and none is added. | **Re-derived at Gate A round 2 on a corrected basis; verdict unchanged, reasoning replaced.** *For the join* the round-1 ground still holds: it lives entirely in `read_first_frame_bounded`, once per accepted connection, pre-`Session`. *For **FR-018** it does not*: the OUT map lands inside `asio_tls_transport::async_read_some`, which **this repo's own source declares hot** (`src/transport/asio_tls_transport.cpp:401-403` — *"per [const §VIII.5] the HOT path is async_read_some"*) and which the session read pump calls per read (`engine.cpp:542`). Arguing "the accept path is cold" for a site with two callers is invalid. **New grounds, three legs, each checkable:** (1) **zero allocation delta** — the reset already runs on every read (`asio_tls_transport.cpp:1134`); one-arg emplaces `impl<F,F>` (`cancellation_state.hpp:121-126`), two-arg emplaces `impl<F,L>` (`:153-161`), both filters are **empty** classes (`cancellation_filter<Mask>` `:30-39`, and a **captureless** lambda), so same size/alignment and `prepare_memory` reuses identically (`asio/impl/cancellation_signal.ipp:52-79`); (2) **zero steady-state instruction delta** — the filters run **only** inside the slot handler (`cancellation_state.hpp:216-223`), i.e. only when a signal is delivered, so on every non-cancelled read the OUT filter is never called; (3) **nothing to regress against** — `bench/baselines/` has no `transport/` profile (it carries capi, codegen, dictionary, log, session, sync, threading, wire). **One round-1 overstatement corrected:** *"no benchmark covers this path"* is too strong — `bench/transport/bench_async_read_some_dispatch.cpp` exists and is built (`bench/transport/CMakeLists.txt:6-13`) with a *"≤ 200 ns p99"* target (`:9`), **but its bodies are unimplemented `TODO (T029)` scaffolds** (`:16-21`, `:44-53`) and it has no baseline, so there is nothing to A/B. **This disposition is void if** the delivered filter captures state or moves off the per-call entry point. Research §D-7. |
| **IX §1 — coverage** | YES | Per-line assessment is the gate, the percentage a target (settled at 085 Gate B, tracked as #225). | **Corrected at Gate A round 1.** The first draft claimed *"8 witness cells (B1–B5, T1–T5) map onto every new or changed line"* — the count was wrong (B1–B5 + T1–T5 is **ten**; the round-1 re-derivation made it **twelve**, and it is **thirteen** after round 2 added T6 — round 3 added no cell but repaired five), and a per-line claim was the wrong instrument. Post-design: research **§D-6.9** enumerates the disposition **per branch**, naming the epoch retire point, the epoch guard's stale and fresh branches, the timer-`ec` branch, the join's winner-order branches, and the clamp boundary. **Three** branches remain uncovered — the F2a feed-error path (pre-existing, untouched), the **epoch guard's stale branch** (genuinely new, and the branch the feature adds, but reachable only in the interleaving research §D-6.4 shows is unconstructible — assessed as *no seam exists*, explicitly **not** waived as "defensive"), and the destructor-body retirement (unobservable, same reason). All three are dispositioned in the verify record per §1's binding rule. |
| **IX — sanitizers** | YES | ASan/UBSan/TSan matrix green, 0 findings (SC-009). | One defect leg is a genuine write-to-freed, so the ASan run is **evidence for the fix**, not hygiene. |
| **XI §2 — cancellation model** | **YES, and it bites** | `co_spawn` defaults to terminal-only; `stop()` emits `total`. | **Re-passed at Gate A round 2 — the round-1 disposition was necessary but NOT sufficient, and the design as it then stood was broken.** *Round 1 (retained):* a naive `\|\|` silently swallows `stop()`'s `total` on the **deadline** arm — asio's default `InFilter` is `enable_terminal_cancellation` (`asio/cancellation_state.hpp:199-201`) — so that arm is wrapped in a coroutine that resets first, matching the MANDATORY rule at `engine.cpp:307-309`. *Round 2 (new):* the **read** arm fails too, one hop further down and only on **TLS**. The transport's own one-argument reset accepts `total` and re-emits it **unchanged** (`asio::cancellation_filter` is a mask, `cancellation_state.hpp:31-39`; the one-arg form sets both filters, `:121-126`), and the `total` is then **discarded by the SSL composed op's terminal-only inner state** (`asio/ssl/detail/io.hpp:100-106` → `asio/detail/base_from_cancellation_state.hpp:44-48` → `cancellation_state.hpp:88-100`). Consequence: the read arm never retires, the group never completes, and `stop()`'s deadline-less step-3 spin (`engine.cpp:1342-1353`) **hangs unboundedly** — and `stop()`'s `total` has meanwhile retired the deadline arm and consumed the group's one-shot cancel guard, destroying even the pre-fix 5 s escape. **§XI.2's "ASIO native cancellation slots end-to-end" was true of the plumbing and false of the effect.** Closed by **FR-018** — the two-argument OUT-mapping reset in `asio_tls_transport::async_read_some`, mirroring the same file's `async_connect` at `:918-933`. **The remedy cannot live engine-side**: `reset_cancellation_state` replaces the single bottom-frame state (`asio/impl/awaitable.hpp:726-732`) and the last reset wins. Plain TCP is unaffected — its read op honours `total` natively (`asio/detail/reactive_socket_service_base.hpp:716-725`). Pins: T2a (deadline-arm mutant, mock — **structurally blind to the TLS leg**), T2b (accept-slot reclaim, engine scope, real mTLS — **must carry a ctest `TIMEOUT`**, its regression mode is a hang), and **T6** (SC-018, real TLS — the only cell that kills the un-mapped mutant). Research §D-2a. *Round 3 (delivery, not design):* the design above is unchanged, but **none of those pins could deliver a cancellation as specified.** (i) T2a and T6 `co_spawn` their subject with no reset, and `co_spawn`'s initial state is built with the **terminal-only** ctor (`asio/impl/co_spawn.hpp:336` → `cancellation_state.hpp:88-100`) forwarding the type verbatim (`co_spawn.hpp:260-263`) — so the test's own `total` died at the spawn and T6 would have failed **with FR-018 correctly present**. Both now require an **outer wrapper coroutine** that resets first, mirroring `engine.cpp:673-676`. (ii) The `bare deadline arm` mutant **does not change the returned error value** — the group's one-shot cancel guard (`parallel_group.hpp:168`, `:351`) is consumed by the external handler, the read arm cannot re-emit (`:222`), the bare arm runs to full expiry and `order[0] == 0` returns the read arm's `transport_read_cancelled`, late — so T2a/T2b now bind **promptness thresholds** (100 ms / 500 ms) instead of asserting an error value. Research §D-6.12. **A round-1/round-2 overstatement is corrected with it**: a bare deadline arm costs `stop()` the *full deadline*, bounded — it does **not** "break" `stop()`. The unbounded failure was only ever the read arm's. |
| **VII — test-only headers** | YES | `mock_transport` stays `FIXPP_ALLOW_MOCK_TRANSPORT`-gated and excluded from production targets — including the **four** additions this feature makes to it (research §D-9 #3–#6). | The new internal header is **not** a test-only header — it is production code that tests may include, exactly like `scan_first_frame_ids.hpp`. §VII is not engaged by it. **Re-checked at Gate A round 3**, when the additions grew from two to four: `include/` is installed with `PATTERN "fixpp/transport/test" EXCLUDE` (`CMakeLists.txt:446-451`), so the mock never enters the install set however much it gains, and SC-010/SC-017 are unaffected. The two round-3 additions (`Script::inbound_chunks`, `read_sizes()`) are **additive** — with `inbound_chunks` empty the mock behaves exactly as today so no other feature's cells move. |
| **XVIII — roadmap discipline** | NO | No protocol and no deferred scope pulled forward; this is a defect correction against an already-merged requirement. | Unchanged. |

**Gate result: PASS ON EVERY ARTICLE EXCEPT ARTICLE XI §6, WHICH CARRIES A REQUESTED WAIVER.**

*(Corrected at Gate A round 3 — C5. The previous wording, "PASS on both passes, with one recorded
deviation", is **self-contradictory** against a MANDATORY article: a knowing deviation from
`.specify/constitution.md:236` is not a PASS, whatever its grounds. The substance was always in the
XI §6 row; what was wrong was the verdict word and the omission of the table the plan template
requires. `.specify/templates/plan-template.md:106-112` says the Complexity Tracking table is filled
**"ONLY if Constitution Check has violations that must be justified"** — this is such a case, so it is
filled below rather than argued away. The deviation is **not new and its grounds are unchanged**; only
its accounting is.)*

### Complexity Tracking

| Violation | Why Needed | Simpler Alternative Rejected Because |
|---|---|---|
| **Article XI §6** (`.specify/constitution.md:236`) — *"Coroutine frame allocation: HALO-first. **PMR fallback per-awaiter where HALO doesn't fire.**"* No per-awaiter PMR fallback is specified for the un-HALO-able coroutine frames the `\|\|` join creates. | FR-005's structural guarantee — no handler can be stranded because `parallel_group::async_wait` retires **every** arm — is what closes both legs of Defect 2 with one mechanism. `co_spawn` type-erases each arm into an independently launched operation (`asio/impl/co_spawn.hpp:317-343`), so **HALO cannot fire** for them; that is a property of the composition, not a choice. Net cost: ~3–4 global-heap coroutine frames per join, once per accepted connection, bounded at `max_bytes + 1` iterations by FR-013, on a path that already allocates (`pmr_carry_buffer`, `engine.cpp:402`). | **(a) A PMR-backed executor.** `co_spawn` uses the executor's associated allocator; `asio::any_io_executor` has no PMR-backed variant in this project, so this means introducing one — a cross-cutting executor change far exceeding a defect fix, on a path that is not the hot path §XV.1 scopes. **(b) Shared-owned state + a `retired` flag instead of the join** (Q2's rejected (a′)): avoids the frames, but needs two mechanisms, reopens the same-drain ordering question, and leaves `Engine::stop()` non-prompt on TLS — see spec §Clarifications Q2. **(c) Descope Defect 2**: leaves a live use-after-free and a live session-teardown defect in production. |

**Precedent for the shape of this waiver.** `[const §XV.1]`'s own amendment (`:296`) grants exactly
this exemption class — *"a genuine cross-executor offload … routable to **neither HALO** … **nor a PMR
arena**"* — for the same structural reason, bounded the same way (O(1) frames per operation). This
request is narrower: per **accepted connection**, not per message, and off the in-memory path
entirely.

**Disposition sought: waiver, not a claim of compliance.** Gate B and the verify record should treat
Article XI §6 as **deviated-with-grounds** for this feature, not as satisfied.

**One item is deliberately carried rather than resolved**, flagged here so Gate A meets it as a
decision and not an oversight: `wait_for_one_success` returns `cancellation_type::none` when the
winning arm completes with a *thrown* exception (`asio/experimental/cancellation_condition.hpp:87-91`).

**Re-derived at Gate A round 1 — the first draft's answer was wrong on the arm this feature adds.**
The read arm indeed cannot throw (both transports return `expected_t`; neither body contains a
`throw`). But the first draft's deadline arm, `co_await t.async_wait(asio::use_awaitable)`, **throws
`std::system_error` on every successfully established connection**, because the join cancels it and
`use_awaitable` throws on a non-zero `error_code`. That was benign for correctness — the
`awaitable<T> || awaitable<void>` overload discards the losing arm's exception when the winner is
clean (`awaitable_operators.hpp:352-356`) — but it was a per-connection throw + `exception_ptr`, and
the "no arm throws" claim a Gate B reader would have relied on was false.

**Resolved, not carried:** `await_deadline` now uses `asio::redirect_error(asio::use_awaitable, ec)`
(research D-2/D-3). That removes the throw, makes the premise true, and is what makes
`outcome.index()` a sound discriminator — without it, a throw on the read arm re-labels the winner and
surfaces as `transport_handshake_timeout`. **What remains carried** is a genuine `std::bad_alloc` from
a frame or `parallel_group` state allocation: bounded, no leak, no UAF, and stated with its actual
consequence (a mis-typed error, not merely a 5 s delay) in research D-3. **No mitigation is added for
that.**

## Project Structure

### Documentation (this feature)

```text
specs/088-firstframe-budget-timer-lifetime/
├── plan.md              # This file
├── spec.md              # 18 FR / 18 SC, 7 locked decisions (Q1-Q3 at /specify, C1-C4 at /clarify)
│                        #   + 3 Gate A round-1 residuals (G-1..G-3; FR-017 added at that round)
│                        #   + 1 Gate A round-2 residual (G-4; FR-018 + SC-018 added at that round,
│                        #     Q2 amended (b) -> (b+))
│                        #   Gate A round 3 added NO FR and NO SC — it repaired the witness plan.
├── research.md          # Phase 0 — D-1..D-9, incl. D-2a (round 2) and
│                        #   D-6.10a/D-6.11/D-6.12/D-6.13 (round 3)
├── data-model.md        # Phase 1 — state and invariants of the first-frame read
├── quickstart.md        # Phase 1 — how to run and reproduce every witness
├── contracts/
│   └── read_first_frame_bounded.md   # the behavioural contract being corrected
├── checklists/
│   └── requirements.md  # spec quality checklist (from /specify, updated at /clarify)
└── tasks.md             # Phase 2 output — NOT created by /speckit-plan
```

### Source Code (repository root)

```text
src/session/
├── engine.cpp                        # MODIFIED — body lifted to the header; carry capacity derived
│                                     #   from the clamp bound (D-1a); comments corrected (FR-008/D-8)
├── read_first_frame_bounded.hpp      # NEW (internal, inline, NOT installed) — FR-016 / D-5
└── scan_first_frame_ids.hpp          # existing precedent for the above

src/transport/                        # NOTE: the CONCRETE transport headers live HERE, not under
│                                     #   include/fixpp/transport/ — corrected at Gate A round 1
├── asio_plain_transport.hpp          # MODIFIED — timer_epoch_state + shared_ptr member,
│                                     #   user-provided ~dtor (retires), timer_epochs() accessor (D-4.1)
├── asio_plain_transport.cpp          # MODIFIED — shared-epoch guard at the connect timer (D-4.1)
├── asio_tls_transport.hpp            # MODIFIED — same three additions
└── asio_tls_transport.cpp            # MODIFIED — shared-epoch guard at the connect + handshake timers
                                      #   + (Gate A round 2, FR-018) the one-arg reset at :1134 in
                                      #     async_read_some becomes the TWO-arg OUT-mapping form,
                                      #     mirroring :918-933. One line + its rationale comment.

include/fixpp/transport/              # UNTOUCHED. Contents: endpoint.hpp listener.hpp
                                      #   listener_events.hpp reconnect_policy.hpp tls_transport.hpp
                                      #   transport.hpp transport_errors.hpp transport_factory.hpp test/
                                      #   — there is no asio_{plain,tls}_transport.hpp here.

include/fixpp/transport/test/
└── mock_transport.hpp                # MODIFIED (test-only, FIXPP_ALLOW_MOCK_TRANSPORT-gated;
                                      #   EXCLUDED from the install set at CMakeLists.txt:446-451,
                                      #   which is what preserves SC-010/SC-017). Four additions:
                                      #   1. cancels_observed()      — contract S5      (D-6.2 / D-9 #3)
                                      #   2. async_reads_observed()  — T2a non-vacuity  (D-9 #4)
                                      #   3. Script::inbound_chunks  — B2/B6            (D-9 #5, round 3)
                                      #   4. read_sizes()            — B5               (D-9 #6, round 3)

tests/session/
├── engine_firstframe_test.cpp        # over-budget witness stays UNMODIFIED (FR-011 guard)
├── read_first_frame_bounded_test.cpp # NEW — B1..B6, T1, T2a via the internal header
├── first_frame_stop_test.cpp         # NEW — T2b, engine-level stop + accept-slot reclaim
└── CMakeLists.txt                    # MODIFIED — new targets + ${CMAKE_SOURCE_DIR}/src include path
                                      #   + the 088 ctest LABELS (research D-5)

tests/transport/
├── test_asio_plain_transport.cpp     # T3 — plain connect epoch-retirement
├── (TLS transport test target)       # T4, T5 — TLS connect + handshake epoch-retirement
├── loopback_tls_fixture.hpp          # EXISTING, unmodified — the real-TLS pair T6 composes
└── (T6 target)                       # NEW (Gate A round 2) — SC-018: total-cancel aborts a real
                                      #   ssl::stream read. MUST wire FIXPP_TLS_FIXTURE_DIR at
                                      #   configure time and carry a ctest TIMEOUT (research D-6.10).
                                      #   May live under tests/session/ instead if it drives
                                      #   read_first_frame_bounded — /tasks decides the file, not
                                      #   the obligation.
```

**Structure Decision.** The feature is confined to the session accept path and the two transports.
The one structural addition is `src/session/read_first_frame_bounded.hpp`, placed under `src/` rather
than `include/` **deliberately**: `src/` is not an installed include root, so SC-017 ("installed
headers byte-identical to `main`") holds *by construction* with no `install()` rule touched. This
mirrors `src/session/scan_first_frame_ids.hpp`, created for the same reason at 040 US2 Phase 4 and
wired into a test at `tests/session/CMakeLists.txt:712-728` (the full block is quoted in research §D-5;
the "No FIXPP_TEST_HOOKS needed" sentence this relies on is at **`:720`**, and the `LABELS` wiring at
`:727-728`). The new witness target copies the include-path shape — **but the precedent is exact for
placement only, not for wiring.** `read_first_frame_bounded` needs `wire::Framer::feed` (out-of-line in
`src/wire/framer.cpp`), `pmr_carry_buffer`, the `Transport` vtable and `asio::steady_timer`, so the
target links `fixpp`; what the `inline` header avoids is `engine.cpp`'s object and the `Engine`
machinery behind it, not the library link. Corrected at Gate A round 1 — see research §D-5.

The two **internal** transport headers under `src/transport/` (not `include/fixpp/transport/`, which
has no such files) each gain a `std::shared_ptr<timer_epoch_state>` member — one control block per
transport instance, carrying one `std::uint64_t` counter **per timer**: `connect` on the plain
transport, `connect` + `handshake` on the TLS one — plus a user-provided destructor whose **body**
retires every counter, plus a `timer_epochs()` const accessor used by the SC-014 cells. The counters
are strand-confined exactly as the existing plain-`bool` `read_in_flight_` is, so no atomic and no new
synchronisation primitive is introduced. Two decisions here are load-bearing and are argued in
research §D-4.1: the epochs live in **shared state the handler owns by value** (a plain member would
be read through a dangling `this` — see the census correction in `spec.md`), and they are **split per
timer** even though a single counter would be correct against today's callers, so that correctness
does not depend on a caller sequencing property the transport cannot enforce.

## Design decisions (full detail in [research.md](./research.md))

| ID | Decision | Why it belongs in the plan and not in `/tasks` |
|---|---|---|
| **D-1** | Loop order: clamp → joined read → insert → **feed** → frame-wins return → **single** budget check with strict `>` at the **foot** of the body | The order *is* the fix; and the foot placement is what makes the clamp proof valid (FR-007 × FR-013 interact) |
| **D-1a** | The framer's **carry capacity is `max_bytes + 1`, derived from the C1 clamp** — not the `max_bytes` the code has today | Without it the framer rejects at 4097 **before any parse**, so SC-012 is RED after the fix and FR-007's decision point is unreachable. Round-1 headline; source-confirmed |
| **D-1b** | The deadline timer is armed **once**, before the loop; `await_deadline` never calls `expires_after` | A per-iteration re-arm silently removes the deadline with every existing test green — D-2's trap's twin |
| **D-2** | `\|\|` join, with the deadline arm wrapped in a coroutine that resets to `enable_total_cancellation()` first | **Severity corrected at Gate A round 3.** The wrapper is what makes `stop()` **prompt** rather than **deadline-bounded** — without it the bare arm swallows `total`, is never re-cancelled (the group's one-shot guard is already consumed, `parallel_group.hpp:168`/`:351`/`:222`) and runs to full expiry, so `stop()` costs the whole 5 s deadline. That is bounded and equal to the pre-fix tail, **not** the "regression worse than the defect" this row previously claimed — the genuinely unbounded failure is the read arm's (D-2a/FR-018), and conflating the two inflated this one. The wrapper is still required, on these honest grounds plus D-3's |
| **D-2a** | **The join does not retire under `stop()` on TLS.** The transport's one-arg reset re-emits `total` unchanged (a filter is a **mask**), and the SSL composed op's terminal-only inner state discards it — so `stop()` hangs unboundedly. Closed by **FR-018**: the two-arg OUT-mapping reset in `asio_tls_transport::async_read_some`, mirroring `:918-933`. INV-4a dispositioned **SAFE BY STRAND CONSTRUCTION** | *Added at Gate A round 2.* Without it the feature ships a regression **worse than the defect** on the default accept path; and the fix is **not expressible at the call site** (`reset_cancellation_state` replaces the bottom-frame state — last reset wins), so it cannot be deferred to `/tasks` or to the implementer |
| **D-3** | The `wait_for_one_success` caveat is **reachable on the deadline arm** and is closed by `redirect_error`; only `bad_alloc` remains carried | The first draft's bare `use_awaitable` arm threw on **every established connection**; and no-throw is what makes `outcome.index()` a sound discriminator |
| **D-4** | Attempt-epoch guard (not a join) at the three transport sites, with the epoch in **shared state the handler owns by value** | A join would re-plumb the 016 T008 OUT cancellation filter; but the dangle leg is real (round-1 census correction), so a plain member epoch would be read through a dangling `this` |
| **D-5** | `read_first_frame_bounded` becomes `inline` in `src/session/read_first_frame_bounded.hpp`; the target still links `fixpp` | Determines the test wiring and keeps the install set untouched. Placement precedent exact; wiring precedent is not |
| **D-6** | **13 witness cells**, each with a named construction that is **derived against the actual test double**, not against its own prose; **B2 (fragmented, cumulative 4097) is the only discriminating one for the budget**, **T6 the only one for FR-018**, and SC-014's same-drain leg is narrowed with a filed residual | The obvious boundary test is green under the *rejected* fix too; five cells had no construction at all in the first draft; **every mock-driven cancellation cell is structurally blind to the TLS defect** (round 2); and **round 3 found five of the ten mutant-matrix columns had no valid RED** — four because B2/B5/B6 were unconstructible (D-6.11) and one because the `bare deadline arm` mutant's stated signature was wrong (D-6.12). Round 3 added **no** cell and repaired five |
| **D-7** | Article VIII §5 not engaged; **Article XI §6 is**, with the allocation delta (**four** coroutine frames, ~3–4 of them global-heap after asio's recycling pools) and a recorded deviation; **Article VIII §3 re-derived at round 2 on zero-delta grounds, not "the accept path is cold"** | Pre-empts a Gate A "new allocations on a read path" finding, closes the missing §6 row, and stops the §3 disposition from resting on a ground that FR-018 invalidates |
| **D-8** | **Five** production comments corrected, including the 015 `/simplify` Q-2 rationale and the new carry-capacity derivation | FR-008; the Q-2 requirement is *preserved* by the join and must be visibly so |
| **D-9** | **Six** new mechanisms, enumerated and priced (internal header, transport epoch state, two mock counters, and — **added at Gate A round 3** — a chunked `Script::inbound_chunks` inbound and a per-read requested-size observable). Gate A round 4 considered a **seventh** and **rejected it as unnecessary** | So each is met as a decision, not as an unexplained addition. **Round 3 overturned this ledger's own "No `Script` field is added"**: that sentence was written as a virtue and was in fact a constraint the witness plan could not live inside — B2, B5 and B6 specified constructions `mock_transport` cannot produce, and B6 was RED against the **delivered** design. A hand-rolled chunking `Transport` in the test file is not the cheaper route; it is an unpriced mechanism, which is what this ledger exists to prevent |

## Risks

| Risk | Mitigation |
|---|---|
| The `\|\|` swallows `stop()`'s `total` — asio's default filter is `enable_terminal_cancellation` (`cancellation_state.hpp:199-201`) | D-2's wrapper, plus FR-015's dedicated pin (T2a), whose non-vacuity observable is `read_latency > 0` **and** at least one read initiated before the signal |
| **The `\|\|` HANGS `stop()` on TLS — `total` is dropped one hop lower, inside the SSL composed op, and the same signal destroys the pre-fix 5 s escape** *(round-2 headline; the unamended design is worse than the defect on the default path)* | **FR-018**'s two-argument OUT map in `asio_tls_transport::async_read_some` (D-2a), pinned by **T6** on a **real** TLS transport — the mock cannot see this, ever, because its read is a `steady_timer` and a timer honours `total` |
| **A cancellation witness that is green because the instrument shares the property under test** | Structural, not fixable by better scripting: SC-018 clause 1 **requires** a real `ssl::stream`. Stated as a spec-level obligation so `/tasks` cannot satisfy T6 with a mock cell |
| **A regression whose signature is a HANG burns a CI job instead of failing a test** | SC-018 clauses 3–4: a per-test ctest `TIMEOUT` **and** a test-owned watchdog that converts the hang into a bounded assertion failure. T2b (real mTLS) gains the same `TIMEOUT` obligation for the same reason |
| The boundary witnesses pass under the *rejected* comparison-only fix → suite green and blind | B2's fragmented shape is mandatory; the spec's discrimination note makes this a spec-level obligation, not a test-review nicety |
| **A new numeric bound is chosen without reading the collaborator that already enforces one** — the round-1 headline, and the reason B2 was RED under the delivered design | D-1a: the carry capacity is stated as **derived from** the clamp bound, commented at the construction site, and carried as INV-B6/I6 in the data model and contract so the two cannot drift apart again |
| The clamp introduces an off-by-one (`room == 0` ⇒ zero-length read ⇒ spin) | D-1's inductive proof — now covering the **completion** side too (`n == 0`) — plus B5 pinning `buf.size() == max_bytes` explicitly and forbidding a scripted zero-byte completion |
| Four sites fixed, one witnessed → the Gate B finding that Q3 widened scope to avoid | C4 requires one pin per site; T3–T5 are not optional. **What they witness is narrowed** (SC-014 / G-1) but the count is not |
| Doc drift between spec/research/plan and the delivered code | `/gate-b` step 4d completeness audit, plus D-8's explicit comment-correction list |
| **An ASan RED that never fires is recorded as "no finding"** — HALO can elide the frame the defect writes into | D-6.3: every ASan RED cell drives the helper through `co_spawn(..., detached)` and must report a **heap**-use-after-free; a clean pre-fix run is read as "the proof did not fire" |
| A witness that never actually observes what it claims passes vacuously | SC-016, scoped: the **session-layer** orderings are *constructed* on a hand-driven `io_context` (D-6.2's elapse-then-poll). The transport cells no longer claim an ordering at all — SC-014 is narrowed to the retirement property and the residual is filed (G-1) |
| **The delivered design's own decision points turn out unreachable** — a dead check moved, not removed | FR-007 now carries an explicit **reachability** clause, and D-1a traces the no-frame path to the foot check at cumulative 4097 |

## Gate A

- Round 1 applied 2026-08-04: Codex P1=8 P2=6 P3=3; Opus post-judging P1=6 P2=9 P3=6; rewrite addresses root causes **#1 a new numeric bound chosen without reading the collaborator that already enforces one (the framer's carry capacity vs. the C1 clamp)**, **#2 "captures `this`" read as "cannot dangle" — the transports' owners were never opened**, **#3 every lifetime/cancellation witness specified as a scenario rather than a construction, three of them in a target that cannot host their postcondition**, **#4 the Constitution Check enumerating the articles the author expected rather than the ones the design engages (XI §6 absent, VI §5 unmet, IX §1's supporting claim false)**, and **#5 anchor, path and count drift**. Reviews: research/reviews/codex_088-firstframe-budget-timer-lifetime_gate_a_review.md, research/reviews/opus_088-firstframe-budget-timer-lifetime_gate_a_adversarial_review.md, research/reviews/orchestrator_088-firstframe-budget-timer-lifetime_gate_a_r1_measurements.md

- Round 2 applied 2026-08-05. **One P1 dominated the round and it is the reason this rewrite exists:
  the `||` join does not retire under `Engine::stop()` on the TLS transport, so `stop()` hangs
  unboundedly on the default accept path — strictly worse than the defect being fixed, and in direct
  contradiction of the bundle's own FR-015.** Disposition: **applied — Q2 amended from (b) to (b+)**
  (join **+** a transport-side OUT-mapping cancellation reset), adding **FR-018**, **SC-018** and cell
  **T6**, with research **§D-2a** carrying the full chain, the INV-4a disposition and the caller
  census. Q2's locked decision is **not** reopened; a `/speckit-clarify` re-run is therefore not
  required (only a replacement by (a′) would have needed one). Secondary items applied in the same
  rewrite: the Article XI §2 row re-passed; the Article VIII §3 disposition **re-derived on corrected
  grounds** (the round-1 "the accept path is cold" reasoning is withdrawn — FR-018's line sits on a
  path the source itself declares hot); D-7's coroutine-frame count corrected from three to **four**
  and its global-heap delta halved by recording asio's recycling pools; and the fifth touched site
  added to Q3's blast radius. Reviews:
  research/reviews/codex_088-firstframe-budget-timer-lifetime_gate_a_2_review.md,
  research/reviews/opus_088-firstframe-budget-timer-lifetime_gate_a_2_adversarial_review.md,
  research/reviews/orchestrator_088-firstframe-budget-timer-lifetime_gate_a_r2_measurements.md,
  research/reviews/fable_088-firstframe-budget-timer-lifetime_q2_assessment.md (independent
  assessment commissioned for the Q2 fork, including the blast-radius/cost follow-up).

- Round 3 applied 2026-08-05. **Three P1s, all of the same class the bundle has now been wrong about
  three rounds running — a witness that cannot fail for the reason it names.** The user approved
  exceeding the two-rewrite cap for this narrowly-scoped amendment; **no locked decision is reopened**
  and no design changes. **(1) C2** — T2a and T6 `co_spawn` their subject with no reset, so the test's
  own `total` dies at the spawn's terminal-only initial state and T6 — the *only* usable pin for
  FR-018 — would have failed with FR-018 correctly present. Fixed by an obligatory outer wrapper
  coroutine (SC-018 clause 4a, SC-015(d), D-6.12a). **(2) N1, NEW and found by the judge alone** —
  B2, B5 and B6 specify constructions `mock_transport` **cannot produce**; B6 was RED against the
  *delivered* design; four matrix columns, including round 1's own `carry@max_bytes` headline, had no
  valid RED. Fixed by ledgering two genuine new mechanisms (D-9 #5/#6) and re-deriving all three cells
  (D-6.11). **(3) C3/N2** — the `bare deadline arm` mutant's stated signature is wrong and **both**
  claimed REDs in that column were false; fixed by correcting the signature and binding promptness
  thresholds on T2a and T2b (D-6.12b). The seven P2s and seven P3s are applied per the judge's
  adjudication; the disagreements table below records what was **not** applied. Reviews:
  research/reviews/codex_088-firstframe-budget-timer-lifetime_gate_a_3_review.md,
  research/reviews/opus_088-firstframe-budget-timer-lifetime_gate_a_3_adversarial_review.md.

- Round 4 applied 2026-08-05. **Post-judging `P1=1 P2=7 P3=2`; corrected to `P1=2` by the
  orchestrator's measurement record, which overturned the judge's reclassification of the B5 finding
  from source.** The user approved a fourth, narrowly-scoped amendment — **seven mechanical text
  edits, no design change** — followed by a narrow verification of the touched cells rather than a
  fifth full round. **No finding this round is against the delivered design.** Rounds 1–3 each found a
  defect in the design or in what a witness could construct; round 4 found only claims **stated more
  strongly than their evidence supports** and statements an earlier round's own correction
  **falsified and did not chase down**. The loop order, the strict `>`, the derived carry capacity,
  the arm-once deadline, the join, the epoch guards and FR-018's OUT map all survived a fourth
  independent pass with nothing against them.
  **The two P1s.** *(1)* **T2b's barrier was not a barrier** — the inverted `test_hook_pre_publish_`
  proposal is withdrawn: the hook fires at `engine.cpp:931-933` and the interval also contains
  `scan_first_frame_ids` (`:877`), the registry compare (`:888`), **`co_await local_session->open()`
  (`:907`)** and the attach (`:922`), so *"the only awaitable in that interval"* was false; and an
  inverted hook is a **negative** barrier, the form this bundle withdrew for T6 at round 3. T2b is now
  **recorded as not discharging non-vacuity**, which is text round 3 had already written as its own
  fallback. *(2)* **B5's `room == 0` mutant would have HUNG rather than gone RED**, so the
  `room == 0` column was **not** recovered by the cells as specified — the one round-3 recovery that
  did not hold. Closed by binding a **non-zero `read_latency`** on B5 (plus a stated deadline and
  `ioc.run()`); **no new mechanism is owed and D-9 stays at six.** **The mechanism took three
  derivations to settle**, and all three are in the disagreements table because two were wrong in
  instructive ways.
  **The five P2/P3 corrections:** T2a's exact-value assertion widened (it could go RED on **correct**
  code); the zero-byte claim **qualified to non-empty requests**; **both** auxiliary defences in D-1's
  no-spin proof **withdrawn**, resting it on the clamp alone; two stale quickstart statements
  corrected; and `inbound_chunks`' contradictory semantics restated as seven numbered rules.
  Reviews:
  research/reviews/codex_088-firstframe-budget-timer-lifetime_gate_a_4_review.md,
  research/reviews/opus_088-firstframe-budget-timer-lifetime_gate_a_4_adversarial_review.md,
  research/reviews/orchestrator_088-firstframe-budget-timer-lifetime_gate_a_r4_measurements.md.

### Round 4 — disagreements

| Finding | Disposition | Reason |
|---|---|---|
| **B5's severity: judge reclassified Codex C-1 from P1 → P2** on the grounds that *"a `co_spawn` arm that completes without suspending is forced through a `post`, so every mutant iteration returns to the `io_context` and the once-armed deadline fires"* | **Conclusion OVERTURNED — restored to P1. But the judge's PREMISE was right and both counter-derivations were wrong**; the corrected basis is `order[0]` determinism | The severity is right: the mutant does not go RED, so this is a P1. The **mechanism took three attempts**, and the audit trail is kept because the error is precisely the class this gate exists to catch — an asio-semantics claim asserted inside a derivation. **(1)** The judge's reclassification was wrong in its conclusion, but its premise — *the arm yields* — is **correct**. **(2)** The counter-derivation that the spin is **synchronous, no yield anywhere** is **wrong**, and a second, independent pass reproduced the same error: **agreement between two derivations was not evidence.** **(3)** The error both shared is the **arm boundary**: both reasoned as though arm 0 were the whole loop. It is not — **the join is per-iteration** (D-1 step 2 sits inside `loop:`), so `operator||` `co_spawn`s **one mock read** fresh every iteration (`asio/experimental/awaitable_operators.hpp:343-347`); arm 0 completes every iteration, `co_spawn.hpp:152-157` **is** reached, and the `io_context` **does** regain control. **The correct mechanism is that yielding is not sufficient**: arms launch left-to-right in index order (`asio/experimental/impl/parallel_group.hpp:376-380`) with the read at index **0**; the per-arm handler writes `completion_order_[completed_++] = I` first (`:205-206`); a zero-length read completes **during its own launch**, so arm 0 is enqueued before arm 1's wait is initiated, and every arm-1 completion is necessarily later (`post_deferred_completions`, or an expiry pushed to the back of `op_queue_`). FIFO ⇒ `order[0] == 0` **every iteration** ⇒ the deadline branch is never taken. A **yielding infinite loop**, not a synchronous spin. |
| **The remedy — bind a non-zero `read_latency` on B5** | **APPLIED, and it is correct** — but only for the reason the corrected mechanism supplies | With `read_latency > 0` arm 0 no longer completes during launch; both arms become ordinary entries in the same **expiry-ordered** timer queue. Arm 0 is recorded first only while the *remaining* deadline exceeds the latency; once remaining < 3 ms **arm 1 is recorded first**, `order[0] == 1`, the deadline branch is taken, and the cell terminates at ~50 ms with `read_sizes()` beginning `{4096, 0, 0, …}` ≠ `{4096, 1}` ⇒ **RED**. *(Latency set to **3 ms** at the final pass: `1 ∤ 50` would co-expire the two series exactly at the deadline, which B6's own no-common-multiple rule forbids; `3 ∤ 50` removes the exception rather than explaining it. B5's outcome is invariant across a tie regardless — research §D-6.11.)* *(An intermediate round-4 draft called this a "timer-vs-timer **race**" and proposed a seventh D-9 mechanism — a defined mock result for a zero-length request — instead. Both are **withdrawn**: it is deterministic expiry ordering, not a race, and no new mechanism is owed. D-9 stays at **six**.)* |
| **Judge N4 — "the matrix under-claims: B4 also kills `room == 0`"** | **WITHDRAWN by its own author; not applied and NOT carried** | B4 is `read_latency`-silent for exactly the same reason B5 was, so as specified it **loops without terminating** on that mutant rather than killing it. The round-4 report's earlier `/tasks` obligation for a B4 matrix row is **retracted** — it rested on the withdrawn claim. Giving B4 the same non-zero `read_latency` treatment would remove its exposure, but **whether B4 then also kills the column is not claimed**, and no row is added. The column stands on **B5 alone**. |
| **Codex C-6's proposed remedy** — *"cite `transport.hpp:101-106` as the governing proof for positive successful reads"* | **Staleness applied; remedy REJECTED** | That sentence is the one at fault: it is true only of **non-empty** requests. asio succeeds with zero bytes on a **zero-length** request — `asio/detail/impl/socket_ops.ipp:890-895`, *"A request to read 0 bytes on a stream is a no-op"* → `error::clear(ec); return 0;`, reached via `all_empty(buffers)` at `asio/detail/reactive_socket_service_base.hpp:426-428`. Adopting the remedy would have **enshrined** the defect. The property is instead rested on the **clamp** (`room >= 1 ⇒ want >= 1`), which needs no transport contract. Codex's line numbers are also off by one: the TLS mapping is `:1167-1182`. |
| **A real near-side initiation barrier for T2b** (an instrumented accepted transport recording read initiation) | **Not built — filed as a residual** | It would be a **seventh D-9 mechanism on the production accept path**, priced and argued from scratch. That class of late structural addition is what cost this bundle rounds 2 and 3. The honest alternative — recording that T2b does not discharge non-vacuity, and resting SC-015 on T2a and T6 — is available, is text the bundle already contains, and leaves a reader able to weigh the residual inference. |
| **Codex C-7 — `ctest -R` in the RED procedure violates Article VII §8** | **Reclassified to P3 and carried, not applied** | `.specify/constitution.md:178` is self-tensioned: it names *"live `-R <target>` selection"* as a standing category of isolation-sensitive test **and then** says *"never `-R <exe-name>`"*. The bundle's carve-out is a defensible reading of §8's own category list; what is actually wrong is smaller — the quickstart claims *"two cases §8 explicitly allows"* where §8 names one. A dedicated RED-proof ctest label is the fix and is a `/tasks` obligation. |

### Round 3 — disagreements

Findings **not applied as raised**, each with the reason. The judge's adjudication is authoritative
where it and Codex differ, so the first three rows record Codex findings the judge **refuted or
reclassified** — kept here so round 4 does not re-open them.

| Finding | Disposition | Reason |
|---|---|---|
| **Codex C1, production leg** — *"external cancellation is reported to the caller as deadline expiry, a production defect"* | **Not applied — refuted by the judge, with source** | There is no caller-visible distinction to corrupt: `src/session/engine.cpp:863-866` takes the **identical** `close(); continue;` arm for every error, and the contract already records this (*"The caller does not distinguish"*, `contracts/read_first_frame_bounded.md`). The stated mechanism is also not established — `parallel_group`'s handler emits `i = 0…N-1` **ascending** (`asio/experimental/impl/parallel_group.hpp:352-353`) with arm 0 = read, so arm 0 is signalled first. **What survives is narrower and IS applied**: the *bundle's* own evidentiary standard was inconsistent — SC-018 asserted an exact `order[0]`-decided value while §D-6.4 disclaims that class of ordering. Fixed by splitting the assertion (D-6.10a), not by changing production behaviour. |
| **Codex C11, escalation to SC-010/SC-017** — *"the modified mock is under `include/`, so the empty-public-surface claims are falsified"* | **Not applied — refuted** | `CMakeLists.txt:446-451` installs `include/` with `PATTERN "fixpp/transport/test" EXCLUDE`, so `mock_transport.hpp` **never enters the install set**, however many additions it takes. SC-010/SC-017 stand. **The rest of C11 is real and IS applied** — the plan said "1 test-only counter" and "No `include/` file is touched" against its own tree, and the coverage row still said twelve cells. All corrected, and the *count* is now four additions, not two. |
| **Codex C4** — *"Normative References violate Article VI §5"* (P1) | **Applied as a P3, not as a violation** | §5 is a **presence** obligation and the section was present, stated why the FIX set was empty, and named the inherited 015 FR-014/SC-011. `[FIX-SL §4.3]` routes through S-001 in the coverage index (`spec/coverage-index.md:43`) and 088 adds no OFFICIAL row. The entry **is** added — it is a one-line traceability improvement and worth having — but recording it as a constitutional violation would misstate what §5 requires. |
| **Codex C13** — *"the carry-buffer census says four and lists five; change four to five"* | **Applied differently — the count was not the defect** | Five construction sites exist in `src/`, verified: `engine.cpp:402`, `engine.cpp:494`, `session.cpp:318`, `session.cpp:1962`, `dictionary/reify.cpp:118`. The sentence's "**plus** `reify.cpp:118` (at `0`)" is arithmetically defensible; what it never did was **name the category** that made four the right number. The fix is to say it — *four **framing** sites*, plus one non-framing site at capacity `0` — not to restate the count and leave the same ambiguity behind a different number. |
| **Codex C10** — *"add FR-018's OUT map as mechanism 5 in the D-9 ledger"* | **Not applied as stated — the ledger's unit is now named instead** | D-9 prices **artifacts** (new types, members, headers, accessors, allocations, test-only surface), because its purpose is to stop unpriced surface entering the tree; FR-018 adds none. A cancellation-type transformation *is* a change of mechanism in the design sense and is argued as one at length in §D-2a.6 — the two words were never in conflict, only the criterion was unstated. It is stated now. **Note what did change**: mechanisms 5 and 6 *are* artifacts by that same criterion and are therefore ledgered, which is what keeps the criterion a filter rather than a convenience. |
| **"Add an accessor for `asio_tls_transport::read_in_flight_` so T6 can prove the SSL read started"** | **Not applied** | `read_in_flight_` is private with no accessor (`src/transport/asio_tls_transport.hpp:286`). Adding one would be new internal surface existing solely for a witness — precisely what D-9 exists to price, and a heavier remedy than the property needs. T6's barrier proves the coroutine ran, reset, and **suspended inside the read** (D-6.13a), which is sufficient for what SC-018 asserts. **The limitation is stated in the bundle rather than hidden**: the barrier does not prove the socket layer was reached. |
| **N4 — *"either audit the newly-live TLS teardown path or name a Gate B obligation"*** | **Applied by AUDIT, not by deferral** | Deferring would have left round 2's *"no new pin required"* asserted over an admitted gap for a second round. Read instead: `stop_pump()` enters **terminal** close (`engine.cpp:511-513`); `Session::close()` **disables cancellation for its whole body** as its first statement (`session.cpp:1396`) with a comment naming this exact interleaving (`:1385-1395`); **terminal skips phase 1 entirely**, so the feared Logout-write-on-open-transport is unreachable (`session.cpp:1438`, `:1490`); and the three-state model makes concurrent entry a side-effect-free mirror (`:1398-1415`). Disposition now rests on evidence. Residual stated: phase 2's root-cancel fan-out is untraced and is **identical under both orderings**, so it is not the differential leg N4 named. |

### Round 2 — disagreements

Findings and framings from round 2 that are **not applied as stated**, each with the reason. Two of
them are corrections to the **reviewers' own reasoning** whose *conclusions* nonetheless survive —
recorded because this bundle's practice is to keep superseded reasoning visible rather than quietly
land the right answer for the wrong reason.

| Finding | Disposition | Reason |
|---|---|---|
| **Orchestrator r2, Link 4 framing** — *"the read arm is left at asio's terminal-only default with no out-filter widening; D-2's wrapper resets only the deadline arm"* | **Conclusion applied, framing corrected — wrong as stated** | The read arm **is** reset to a `total` mask, by the transport itself, as its first statement (`src/transport/asio_tls_transport.cpp:1134`; plain at `asio_plain_transport.cpp:195-196`). `co_spawn`'s terminal-only default (`asio/impl/co_spawn.hpp:336`) is real but **not operative here** — the arms run synchronously to first suspension at group launch, by which time the transport's reset has already replaced the default. **The hang survives the correction**, via a different link: the mask forwards `total` unchanged and the **SSL composed op** discards it (research §D-2a.1 link 4). The distinction is load-bearing, because it is what proves the fix cannot live on the engine side. |
| **"IN-filter-only" hypothesis** — *"the single-argument `reset_cancellation_state` sets the IN filter only, so the OUT path keeps the default"* | **Conclusion applied, hypothesis refuted in the letter** | The one-argument form sets **both** filters: `slot.emplace<impl<Filter, Filter>>(filter, filter)` (`asio/cancellation_state.hpp:121-126`). The conclusion survives for a different reason — `asio::cancellation_filter` is a **MASK, not a map** (`:31-39`), so setting the OUT filter to `enable_total_cancellation` forwards `total` **as `total`**, which is precisely what the child op will not honour. Getting this right is what identifies the remedy as an **OUT-mapping lambda** rather than a wider mask; a wider mask would change nothing. |
| **"Add a bounded escape to `stop()`'s step-3 join"** (`engine.cpp:1342-1353` spins with no deadline) | **Not applied — out of scope, and it trades a hang for a UAF window** | Defensible as defence-in-depth: that spin converts *any* future non-retiring loop into an unbounded hang. But it is a **mitigation, not the remedy** — it would turn "stop() hangs" into "stop() abandons a live coroutine", i.e. a use-after-free window at teardown, and 088 has no mandate to re-open 023's teardown ordering. **Filed as a separate hardening issue at close-out**, alongside the SC-014 residual. FR-018 removes the actual cause. |
| **"Scope the OUT map to the accept path only, leaving the session read pump on today's semantics"** | **Not applied — not expressible, and not needed** | Not expressible: `reset_cancellation_state` replaces the single bottom-frame state and the last reset wins (`asio/impl/awaitable.hpp:726-732`), so there is no per-caller variant of the transport's filter short of a second entry point. Not needed: the INV-4a concern that motivated it is dispositioned **SAFE BY STRAND CONSTRUCTION** (research §D-2a.7) — the invariant is strand confinement, not ordering, and the read pump's own comment (`engine.cpp:465-471`) already contracts for exactly the behaviour (b+) enables. |
| **"Re-enable the DISABLED TLS read-cancellation cells while you are here"** (`tests/transport/test_cancellation_propagation.cpp:81-88`) | **Not applied in 088 — carried as a follow-up** | The finding behind it is **true and worth recording**: `Transport::async_read_some`'s documented cancellation contract (`include/fixpp/transport/transport.hpp:105-106`) has **never been witnessed on a real `ssl::stream`** — its cells are `DISABLED_` + `GTEST_SKIP`, *"pending server+client SslCtxConfig fixture pair (post-MVP)"* — which is a large part of why this defect survived. The blocker is now gone (`LoopbackTlsFixture` **is** that pair). But re-enabling and repairing 012's transport cells is a transport-test debt, not this feature's defect; folding it in would widen a defect fix into a test-suite rehabilitation. **T6 discharges FR-018; the general contract pin is filed at close-out.** |

### Round 1 — disagreements

Findings raised in round 1 that are **not applied**, each with the reason. Recorded so round 2 meets
them as adjudicated rather than as omissions.

| Finding | Disposition | Reason |
|---|---|---|
| **Codex P3-2** — *"the `[[feedback_*]]` references are not resolvable citations; add an index or replace them"* | **Not applied — premise refuted** | `[[feedback_*]]` is this project's canonical citation form for its cross-session memory records, and it is used **in shipped production source**, not only in specs: `src/transport/asio_plain_transport.cpp:139-140` carries `// [[feedback_asio_cospawn_total_cancellation_default]];` and `// [[feedback_engine_stop_must_close_transports_total_cancel_insufficient]].`, and `src/transport/asio_tls_transport.cpp:928-929` repeats both. `research.md` cites one of exactly those slugs. Requiring 088 to invent a resolution index would make it the only artifact in the tree not using the established form. The mnemonics are **left exactly as they are**. |
| **Codex V3** — *"088 has no YAML front matter"* | **Not applied — adjudicated not a finding** | Census: **82 of 85** specs are plain, only 3 carry YAML, and `.specify/templates/spec-template.md:1` itself begins with a heading. Nothing downstream parses it: `.specify/scripts/bash/setup-tasks.sh:30-41` checks only that `plan.md` and `spec.md` exist, and feature resolution comes from `.specify/feature.json` / directory paths in `common.sh:180-224`. Codex marked this WRONG in its own verification table and did not raise it as a finding; it is recorded here only so round 2 does not re-open it. |
| **Codex P1-6, VIII §3 half** — *"measure accepted-connections/sec before and after; add a same-PR benchmark"* | **Not applied — refuted** | Article VIII §3 (*"No perf change merged without a benchmark in the same PR"*, `.specify/constitution.md:186`) is conditioned on the change **being** a perf change. §2 measures against `bench/baselines/` per profile and §4's v1.0 targets are parser throughput, session throughput and round-trip latency — none covers the pre-session accept path, and no baseline exists for it. Codex conflates §3 with §5's hot-path allocator ban, which is separately and correctly scoped out (research D-7: the ban is *"between parse and the `fromApp` callback"*, and this path already allocates). Demanding an accept-rate A/B for a defect fix is scope, not compliance. **The XI §6 half of the same finding IS applied** — see the new Constitution Check row. |
| **Codex P2-6, second leg** — *"the `:857-858` sole-caller citation is wrong; the call is at `:861-862`"* | **Not applied as stated — the cited line is correct for the claim it supports** | `engine.cpp:857` is `std::vector<std::byte> frame_buf;` and `:858` is `frame_buf.reserve(512);`. That is precisely where the buffer's **emptiness** is established — which is the property being cited, and the base case the clamp proof needs. The *call* is cited separately and correctly at `:861-862`. Changing `:857` to `:861-862` would send the reader to the wrong line for the wrong claim. **What was applied:** the wording is sharpened to name each anchor's role explicitly (`:857` construction / `:861-862` the call), so the pairing is no longer inferable-only. The other two legs of P2-6 (`:454` has no `timer.cancel()`; the CMake no-hook line is at `:720`, outside `:722-728`) were **true and are applied**. |
| **Codex P1-8, quickstart-coverage half** — *"add a `linux-clang-coverage` build/run, `llvm-profdata`/`llvm-cov` scope and a ≥95/85 target to the quickstart"* | **Not applied — enforced-practice drift** | No recent sibling `quickstart.md` carries a coverage procedure: across `083/084/085/086` a search for `llvm-cov`/`llvm-profdata`/`linux-clang-coverage` returns exactly one hit, and it is a *troubleshooting note* in `085/quickstart.md` about a preset-resolution failure. Coverage evidence in this project lives in `.specify/decisions/<feature>-verify.md`, which `plan.md` and `quickstart.md` §5 both already route to. **What survives and IS applied:** the false *"8 witness cells map onto every new or changed line"* claim, replaced by research §D-6.9's per-branch enumeration with two named uncovered branches. |
| **Codex P1-1, framing** — *"the epoch guard introduces a read-from-freed"* | **Applied with corrected framing, not as stated** | The guard does not introduce a *new* read-from-freed: pre-fix the same stranded handler already executes `socket_.cancel(ignored)` — a member call on a destroyed socket — through the identical dangling `this`, so a member epoch would only shrink a call-on-freed to a read-on-freed. **The defect is that the census's "Dangle leg: no" ×3 and research D-4's *"the transport outlives it"* are source-refuted, and they were the sole rationale for choosing an epoch over a join.** That is what is written into the bundle (spec §Census, research §D-4.0), and the mechanism is re-taken accordingly (§D-4.1). |
| **Codex P1-2 / P1-4 counter-proposals** — *"introduce a test-only timer/operation scheduler or clock abstraction"* | **Not applied** | For the session-layer cells it is unnecessary: research §D-6.2's elapse-then-poll construction is deterministic using only a hand-driven `io_context` and the existing `Script::read_latency`, because both competing completions are timers and asio's timer queue is expiry-ordered. For the transport cells it would not help — the competing completion there is a **socket** event from the reactor, which no clock abstraction reorders — so SC-014 is narrowed instead (clarification G-1) and the residual is filed. Adding an injectable scheduler would be new production surface for a witness that still could not be constructed. |

**Carried out of round 1, to be filed at close-out:** an issue for *"no deterministic same-drain seam
for the transport connect/handshake timeout handlers"* — the SC-014 residual (clarification G-1,
research §D-6.4).

**Carried out of round 2, to be filed at close-out** (**four**, none of them blocking, all recorded so
they are not re-discovered):

1. **`Engine::stop()`'s step-3 join has no deadline** (`src/session/engine.cpp:1342-1353`) — a 0 ms
   timer spin on `outstanding_counter_` with no escape. It is what turns any non-retiring loop into an
   unbounded hang rather than a bounded stall, and 088 removes one such loop without removing the
   amplifier. Hardening, with the UAF-window trade-off stated in the round-2 disagreements table.
2. **The TLS read-cancellation contract has no live witness.**
   `tests/transport/test_cancellation_propagation.cpp:81-88` — `DISABLED_…ReadCancelledStrand` /
   `…ReadCancelledDirect`, `GTEST_SKIP`ped *"pending server+client SslCtxConfig fixture pair"*. That
   fixture now exists (`tests/transport/loopback_tls_fixture.hpp`). Re-enable and repair.
3. **`bench/transport/bench_async_read_some_dispatch.cpp` is an empty scaffold.** It is built, is named
   for the path this repo calls hot, carries a *"≤ 200 ns p99"* target — and its bodies are
   `TODO (T029)` with no `bench/baselines/transport/` profile. Any future Article VIII §2/§3 argument
   about the transport read path is unmeasurable until it is filled in.
4. **`mock_transport`'s class documentation overstates its own behaviour.** `mock_transport.hpp:114-117`
   says *"**All** async methods compose an `asio::post(exec_)` checkpoint (deferred resume)"*. The
   post exists **only** in `async_connect` (`:155`); `async_read_some` (`:164-202`), `async_write`
   (`:204-…`) and `async_handshake` (`:261-…`) have none. Found while re-deriving B5's spin at Gate A
   round 4 — a `/tasks` author trusting that sentence would conclude the read yields per call, which
   is exactly the wrong conclusion. Production test-header comment, outside this feature's edit scope.

**Carried out of round 4, as `/tasks` obligations rather than blockers** (four; none changes the
design, and each is recorded where the work lands):

1. **The promptness construction (C-4).** T2a/T2b's 100 ms / 500 ms bounds are wall-clock and
   unmeasured. Prefer a test-owned intermediate-timer / ordering construction over a measured bound —
   **and reconcile or narrow SC-016 (`spec.md`) in the same edit**, because the contradiction is in
   the *binding* criterion, not only in the quickstart: SC-016 says no session-layer witness depends
   on winning a timing race **and names SC-015**, while SC-015's thresholds are normative. SC-016 also
   still attributes elapse-then-poll to T2a, which round 3 replaced with a signal-driven wrapper.
2. **A dedicated RED-proof ctest label (C-7)**, replacing the `-R` invocations in quickstart §4, and
   correcting its *"two cases §8 explicitly allows"* to one.
3. **B5's and B4's mutant REDs MUST be shown to TERMINATE**, and the termination captured in the
   verify record — not merely asserted to fail. This round is what makes the obligation
   non-optional: as specified at round 3 both cells **hung** on the `room == 0` mutant instead of
   going red, and a non-terminating loop is indistinguishable from a wrong assertion in a CI log
   ([[feedback_ci_hung_test_no_timeout_burns_6h_gdb_capture]]). B5's non-zero `read_latency` is what
   makes termination reachable; the verify record must show it actually happened, and B4 needs the
   same treatment.
   *(The earlier "B4 matrix row" obligation is **retracted** — the claim behind it was withdrawn; see
   the Round 4 disagreements table.)*
4. **A real near-side initiation barrier for T2b** — a seventh D-9 mechanism (an instrumented accepted
   transport recording read *initiation*), deliberately not bought at round 4. Filed against SC-015.

## Next pipeline step

**`/gate-a 088-firstframe-budget-timer-lifetime`** — mandatory (Article XVII §1, cancellation
surface), run **after `/plan` and before `/speckit-tasks`** per `.specify/pipeline.md` step 4.
Blockers must be resolved or explicitly waived with rationale before `/tasks` runs.
