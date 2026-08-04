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
3. **Class-fix** — the same stale-handler shape at three transport timer sites, closed with an
   attempt-epoch guard rather than a join (they have no dangle leg, and both connect sites sit behind
   a deliberately-shaped OUT cancellation filter a join would have to re-plumb).

Public surface delta: **empty**. No installed header change, no error code, no ABI change. One new
*internal* header under `src/` (not installed), following an exact precedent in the same file.

## Technical Context

**Language/Version**: C++23 (coroutines, `std::expected` via `core::expected_t`)

**Primary Dependencies**: `asio/1.38.0` — verified against `conanfile.py:67` for this feature, not
propagated from an anchor doc. OpenSSL via `asio::ssl` on the TLS transport. **No new dependency.**

**Storage**: N/A — pre-session path, no `Session`, no message store.

**Testing**: GoogleTest + ctest; `MockTransport`
(`include/fixpp/transport/test/mock_transport.hpp`, `FIXPP_ALLOW_MOCK_TRANSPORT`-gated, whose
contract already binds it to honour cancellation rather than short-circuit it); hand-driven
`io_context` for the same-drain witnesses.

**Target Platform**: Linux (Clang 22 local + CI per Article XVII §7); gcc-release and MSVC CI-only.

**Project Type**: C++ library (`fixpp`) — session and transport layers.

**Performance Goals**: None new. Not a perf change — Article VIII §3's "no perf change without a
bench in the same PR" is not triggered (see Constitution Check and research D-7).

**Constraints**: Worst-case pre-session buffered bytes **`max_bytes + 1` = 4097** (SC-013 — tighter
than the status quo, not looser); deadline unchanged at 5000 ms; `Engine::stop()`'s
`cancellation_type::total` must still abort an in-flight first-frame read promptly (FR-015).

**Scale/Scope**: 3 production source files changed, 2 production headers gain one member each, 1 new
internal header, new witness targets in `tests/session/` and `tests/transport/`. No codegen, no
schema, no generated artifacts, no dictionary.

## Constitution Check

*GATE: evaluated before Phase 0 and re-evaluated after Phase 1 design. Both passes are recorded.*

| Article | Applies? | Disposition (pre-Phase-0) | Re-check (post-Phase-1) |
|---|---|---|---|
| **XVII §1 — Gate A trigger** | **YES** | Touches the concurrency / cancellation / executor model on the accept path. Gate A is **mandatory**; there is no waiver path — §6's auto-waive covers comment-only edits, which this is not, and that is exactly why PR #232 deferred these defects rather than fixing them. | Unchanged. `/gate-a` runs after this plan and **before** `/speckit-tasks` (`.specify/pipeline.md` step 4). |
| **XVII §7 — local pre-PR build gate** | YES | `linux-clang-debug` Conan install + configure + build + ctest before any PR, with the confirmation line in the PR body. Resource gate honoured — the build is surfaced for user approval, never auto-run. | Unchanged. |
| **XVII §8 — verification gate** | YES | `/speckit-verify` mandatory after `/speckit-implement`; record at `.specify/decisions/088-firstframe-budget-timer-lifetime-verify.md` before `/gate-b` will start. | Unchanged. |
| **VIII §5 — allocator policy on the hot path** | **NO — analysed, not assumed** | The ban is "between parse and the `fromApp` callback", i.e. the established-session read pump. This path runs once per accepted connection, before any `Session` exists, and already allocates there today (`pmr_carry_buffer`, `engine.cpp:401`). | Post-design delta: **two `co_spawn` coroutine frames per accepted connection**, three lines after a buffer allocation that already exists. Recorded in research D-7 so Gate A does not meet it as an unexplained new allocation. No deviation justification required — §5's scope does not reach this path. |
| **VIII §3 — no perf change without a bench** | NO | Not a perf change; no benchmark covers the accept path and none is added. | Unchanged. The clamp *reduces* bytes read near the boundary; it moves no measured figure. |
| **IX §1 — coverage** | YES | Per-line assessment is the gate, the percentage a target (settled at 085 Gate B, tracked as #225). | Post-design: 8 witness cells (B1–B5, T1–T5) map onto every new or changed line — see research D-6. |
| **IX — sanitizers** | YES | ASan/UBSan/TSan matrix green, 0 findings (SC-009). | One defect leg is a genuine write-to-freed, so the ASan run is **evidence for the fix**, not hygiene. |
| **XI §2 — cancellation model** | **YES, and it bites** | `co_spawn` defaults to terminal-only; `stop()` emits `total`. | **Post-design finding (research D-2): a naive `\|\|` silently swallows `stop()`'s `total` on the deadline arm** — asio's default `InFilter` is `enable_terminal_cancellation` (`asio/cancellation_state.hpp:199-201`). The design wraps that arm in a coroutine that resets first, matching the MANDATORY rule already written at `engine.cpp:307-309`. FR-015's dedicated pin (T2) exists to hold it. |
| **VII — test-only headers** | YES | `MockTransport` stays `FIXPP_ALLOW_MOCK_TRANSPORT`-gated and excluded from production targets. | The new internal header is **not** a test-only header — it is production code that tests may include, exactly like `scan_first_frame_ids.hpp`. §VII is not engaged by it. |
| **XVIII — roadmap discipline** | NO | No protocol and no deferred scope pulled forward; this is a defect correction against an already-merged requirement. | Unchanged. |

**Gate result: PASS on both passes.** No violations require justification, so the Complexity Tracking
table is omitted rather than filled with "N/A" rows.

**One item is deliberately carried rather than resolved**, flagged here so Gate A meets it as a
decision and not an oversight: `wait_for_one_success` returns `cancellation_type::none` when the
winning arm completes with a *thrown* exception (`asio/experimental/cancellation_condition.hpp:87-91`).
Research D-3 shows it is unreachable on any path this feature creates — both transports return
`expected_t` and neither body contains a `throw` — and that the consequence if it somehow fired is
bounded at 5 s with no leak and no UAF, which is strictly better than the pre-fix stranding on the
same input. **No mitigation is added.**

## Project Structure

### Documentation (this feature)

```text
specs/088-firstframe-budget-timer-lifetime/
├── plan.md              # This file
├── spec.md              # 16 FR / 17 SC, 7 locked decisions (Q1-Q3 at /specify, C1-C4 at /clarify)
├── research.md          # Phase 0 — D-1..D-8
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
├── engine.cpp                        # MODIFIED — body lifted to the header; comments corrected (FR-008)
├── read_first_frame_bounded.hpp      # NEW (internal, inline, NOT installed) — FR-016 / D-5
└── scan_first_frame_ids.hpp          # existing precedent for the above

src/transport/
├── asio_plain_transport.cpp          # MODIFIED — epoch guard at the connect timer (D-4)
└── asio_tls_transport.cpp            # MODIFIED — epoch guard at the connect + handshake timers (D-4)

include/fixpp/transport/
├── asio_plain_transport.hpp          # MODIFIED — one `std::uint64_t timer_epoch_` member
└── asio_tls_transport.hpp            # MODIFIED — one `std::uint64_t timer_epoch_` member

tests/session/
├── engine_firstframe_test.cpp        # over-budget witness stays UNMODIFIED (FR-011 guard)
├── read_first_frame_bounded_test.cpp # NEW — B1..B5, T1, T2 via the internal header
└── CMakeLists.txt                    # MODIFIED — new target + ${CMAKE_SOURCE_DIR}/src include path

tests/transport/
├── test_asio_plain_transport.cpp     # T3 — plain connect same-drain
└── (TLS transport test target)       # T4, T5 — TLS connect + handshake same-drain
```

**Structure Decision.** The feature is confined to the session accept path and the two transports.
The one structural addition is `src/session/read_first_frame_bounded.hpp`, placed under `src/` rather
than `include/` **deliberately**: `src/` is not an installed include root, so SC-017 ("installed
headers byte-identical to `main`") holds *by construction* with no `install()` rule touched. This
mirrors `src/session/scan_first_frame_ids.hpp`, created for the same reason at 040 US2 Phase 4 and
wired into a test at `tests/session/CMakeLists.txt:722-728`; the new witness target copies that wiring
verbatim, including the `${CMAKE_SOURCE_DIR}/src` include path and the absence of any
`FIXPP_TEST_HOOKS` requirement.

The transport headers gain one `std::uint64_t` epoch **per timer** — `connect_timer_epoch_` on the
plain transport, `connect_timer_epoch_` + `handshake_timer_epoch_` on the TLS one. Both transports
are strand-confined and already carry plain-`bool` members guarded the same way (`read_in_flight_`),
so no atomic and no new synchronisation primitive is introduced. (A single shared member per
transport would be correct against today's callers — verified in research §D-4 — but is split so that
correctness does not depend on a caller sequencing property the transport cannot enforce.)

## Design decisions (full detail in [research.md](./research.md))

| ID | Decision | Why it belongs in the plan and not in `/tasks` |
|---|---|---|
| **D-1** | Loop order: clamp → joined read → insert → **feed** → frame-wins return → **single** budget check with strict `>` at the **foot** of the body | The order *is* the fix; and the foot placement is what makes the clamp proof valid (FR-007 × FR-013 interact) |
| **D-2** | `\|\|` join, with the deadline arm wrapped in a coroutine that resets to `enable_total_cancellation()` first | Without the wrapper the join silently breaks `Engine::stop()` — a regression worse than the defect being fixed |
| **D-3** | The `wait_for_one_success` "winner errored" caveat is **unreachable**; no mitigation added | Analysed and carried, not missed |
| **D-4** | Attempt-**epoch** guard (not a join) at the three transport sites | A join would re-plumb the 016 T008 OUT cancellation filter for no lifetime benefit |
| **D-5** | `read_first_frame_bounded` becomes `inline` in `src/session/read_first_frame_bounded.hpp` | Determines the test wiring and keeps the install set untouched |
| **D-6** | 8 witness cells; **B2 (fragmented, cumulative 4097) is the only discriminating one** | The obvious boundary test is green under the *rejected* fix too |
| **D-7** | Article VIII §5 not engaged; +2 coroutine frames per accepted connection, recorded | Pre-empts a Gate A "new allocations on a read path" finding |
| **D-8** | Three production comments corrected, including the 015 `/simplify` Q-2 rationale | FR-008; the Q-2 requirement is *preserved* by the join and must be visibly so |

## Risks

| Risk | Mitigation |
|---|---|
| The `\|\|` swallows `stop()`'s `total` — asio's default filter is `enable_terminal_cancellation` (`cancellation_state.hpp:199`) | D-2's wrapper, plus FR-015's dedicated pin (T2), which must be shown to actually catch a read in flight rather than passing vacuously |
| The boundary witnesses pass under the *rejected* comparison-only fix → suite green and blind | B2's fragmented shape is mandatory; the spec's discrimination note makes this a spec-level obligation, not a test-review nicety |
| The clamp introduces an off-by-one (`room == 0` ⇒ zero-length read ⇒ spin) | D-1's inductive proof, plus B5 pinning `buf.size() == max_bytes` explicitly |
| Four sites fixed, one witnessed → the Gate B finding that Q3 widened scope to avoid | C4 requires one pin per site; T3–T5 are not optional |
| Doc drift between spec/research/plan and the delivered code | `/gate-b` step 4d completeness audit, plus D-8's explicit comment-correction list |
| A same-drain witness that never actually observes the race passes vacuously | SC-016: every ordering is *constructed* on a hand-driven `io_context`; no witness uses a timing margin |

## Next pipeline step

**`/gate-a 088-firstframe-budget-timer-lifetime`** — mandatory (Article XVII §1, cancellation
surface), run **after `/plan` and before `/speckit-tasks`** per `.specify/pipeline.md` step 4.
Blockers must be resolved or explicitly waived with rationale before `/tasks` runs.
