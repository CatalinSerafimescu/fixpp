# Implementation Plan — 005-session-establishment-fsm

**Branch**: `005-session-establishment-fsm` | **Date**: 2026-05-17 | **Spec**: [spec.md](spec.md)
**Design anchors**: `.specify/2e-msgstore.md` **v0.4** (the FSM *consumes* the `MessageStore` seam; `seqnum_t` ownership re-pointed here per `[2e §3.1]`/`[2e §10 Q9]`), `.specify/2d-threading.md` **v0.4** (`fixpp::core::Clock` seam, `effective_clock`, `cancellable_dispatch`, two-phase close, `SessionConfig`), `.specify/2f-async-mutex.md` **v1.5** (`fixpp::sync::async_mutex` for the seqnum counter), plus the **FIX Session Layer** standard (`[FIX-SL]`) + **FIX Session Layer Test Cases** (`[FIX-TC]` TC-001..TC-017). On conflict the anchor wins; an inconsistency is a defect in this plan.

## Summary

Deliver the connection-lifecycle slice of the `fixpp::session` module — the FIX **session FSM** plus the five admin messages that drive it — implementing catalogue rows **S-001, S-002, S-003, S-004, S-007, S-008, S-009, S-015, S-016, S-019, S-020** and the folded **`core/` time-helper surface row #4** (closing the `core/` module-exit item). A `Session` runs a 7-state FSM (`NotConnected → LogonSent → LogonReceived → Active → {RecoveryPending} → LogoutSent → Disconnected`, `[FIX-SL §4.10]`), drives `Logon(35=A)` / `Logout(35=5)` / `Heartbeat(35=0)` / `TestRequest(35=1)` / `Reject(35=3)` for both initiator and acceptor roles, maintains the inbound-expected / outbound-next `MsgSeqNum(34)` counters, negotiates `HeartBtInt(108)`, validates `SenderCompID(49)`/`TargetCompID(56)` and `BeginString(8)` version gating, and stamps/validates `SendingTime(52)` against `MaxLatency` through the effective-clock seam. It **publishes `seqnum_t`** (`<fixpp/session/seqnum.hpp>`) — promoting the placeholder `2e-msgstore` consumed — and **consumes** the `[2e §4.1]` `MessageStore` 4-pure-virtual seam under durable-before-transmit ordering (a test-double store satisfies it; the in-mem/file impls S-012/S-013 are out of scope). The FSM composes via coroutines + ASIO native cancellation slots on the per-session strand, uses `fixpp::sync::async_mutex` for the seqnum counter, and reports every failure through `expected_t<T>` / `fixpp::core::error` with **no C++ type across the C ABI**.

Per `/speckit-clarify` 2026-05-17 (3 OSS-reference-grounded decisions): **Q1** — a sequence gap enters an **explicit `RecoveryPending` state** (entry/hold/surface owned here; the ResendRequest/SequenceReset exit transitions are the deferred recovery feature's seam). **Q2** — a **capability-partitioned `[FIX-TC]` subset** ships green in this PR; the concrete TC-### → in/deferred split is fixed in this plan (research D-10) against the version-partitioned QuickFIX/J acceptance-definition corpus as the executable oracle. **Q3** — inbound `SendingTime(52)` beyond `MaxLatency` → `Reject(SessionRejectReason=10, ref tag 52)` → `Logout` → disconnect (Logon → logout-with-error, no standalone reject).

## Technical Context

**Language/Version:** C++23 (`[const §II.1]`). Coroutines (`asio::awaitable<T>`), concepts, `std::expected` (via `core::expected_t`), `std::pmr`, `std::span`, `std::chrono`, deducing `this`. No fallback to earlier standards. The FSM is a non-template runtime class (`Session` holds `std::unique_ptr<MessageStore>` per `[2e §4.1]` N1 — virtual-by-construction; no `Session<Store>` template spread).

**Primary Dependencies:** GoogleTest 1.17.0 + GoogleMock, Google Benchmark 1.9.5 — pinned via Conan from Phase 3 CI. **No new Conan row.** Session reuses `fixpp::core` (`expected_t`, `error`, `Clock`/`mock_clock`, `cancellable_dispatch`, `session_executor` per `[2d §4.1]`/`[2d §4.3]`/`[2d §4.8]`), `fixpp::sync::async_mutex` (`[2f §4.1]`), the `[2e §4.1]` `MessageStore` seam, and the merged `wire/` (PR #68) + `dictionary/` (PR #66/#67) surfaces for admin-message framing/parse/serialize and typed field access. No new external dependency (`[const §III.2]`).

**Storage:** N/A as an owned store — this feature **consumes** the `[2e §4.1]` seam; a `tests/support/` in-memory **test-double** `MessageStore` satisfies it for tests (the production `MemoryStore` S-012 / `FileStore` S-013 are out of scope, FR-010/FR-017). Outbound ordering is durable-before-transmit (`store(seq, committed_span, outbound)` post-`Writer::commit`, pre-`transport::async_write`, `[2e §root cause #1]`/`[2e §7.6]`); inbound is post-Parser, pre-`fromAdmin`/`fromApp`. Steady-state inbound-process / timer-fire paths are zero global `new`/`delete` (`[const §VIII.5]`); allocation confined to `Session::open` and caller arenas.

**Testing:** GoogleTest + GoogleMock (C++), TDD red-green-refactor (`[const §VII.1]`/`[const §VII.3]`). Deterministic time via `fixpp::core::mock_clock` (`[2d §4.3]`); an in-memory transport double feeds verified inbound frames / captures outbound frames; a test-double `MessageStore`. The in-scope `[FIX-TC]` subset (Clarification Q2; concrete split research D-10) lands as executable `tests/conformance/` scenarios authored from the QuickFIX/J acceptance-definition oracle. **No new fuzz harness** — the session FSM is not parser-touching (`wire/` owns framing/parse fuzzing; `[const §VII.7]` does not bind here; session README exit criteria confirm "no fuzz-harness exit gate"). No new Python pytest seam — session emits **no** C-ABI surface in this PR (the `fixpp_session_*` C ABI is owned by 2i; `[const §X.2]`, FR-015).

**Target Platform:** Linux primary (Tier 1: Clang 22 Debug + Release + ASan + UBSan + TSan + Coverage; GCC Release sanity). Windows Tier 2 manual/nightly (`[const §IX.6]`). **No C-ABI surface added** (`[const §X.2]`, FR-015); `[const §IX.5]` abidiff **N/A here** (recorded for explicit non-applicability — research D-12).

**Project Type:** C++23 library, `session/` module (first feature). Header + out-of-line split: the FSM driver, admin-message builders/interpreters, seqnum manager, and time-helper get out-of-line `.cpp` for compile-time and the coroutine bodies; `seqnum.hpp` and small value types are header-only. No build-only tool, no SWIG/Python, no C-ABI in this PR.

**Performance Goals (Linux/Clang/x86_64, warm cache, release `-O2`):** the session FSM is not the parse hot path but is on the steady-state message path; ceilings per the `[2e §6.6]` / `[2d §6.3]` Tier-1 budget envelope. CI fails on >5% regression vs `bench/baselines/` (`[const §VIII.2]`):

| Operation | Workload | Ceiling |
|---|---|---|
| inbound admin dispatch (validate → seqnum advance → `fromAdmin`) | in-sequence `Heartbeat`, warm | ≤ 250 ns |
| outbound admin emit (stamp `SendingTime` → `store()` → hand to transport) | `Heartbeat`, test-double store | ≤ 400 ns |
| `seqnum` next-with-increment (async_mutex uncontended) | per `[2e §6.6]` next_seqnum budget | ≤ 50 ns |
| `utc_time_to_fix_string` | ms precision, fixed buffer | ≤ 60 ns |
| `fix_string_to_utc_time` | 21-byte UTCTimestamp | ≤ 80 ns |
| heartbeat timer fire → emit decision | mock clock, no alloc | ≤ 150 ns |

Bench harnesses `bench/session/{fsm,seqnum,fix_time,heartbeat}_bench.cpp` enforce via Google Benchmark (`[const §VIII.1]`); ±5% (`[const §VIII.2]`). Session-throughput parity-or-better vs QuickFIX is a v1.0 release gate (`[const §VIII.4]`), measured later (full interop is `transport/`'s milestone), not a this-PR blocker.

**Constraints:**

- Zero global `new`/`delete` on the steady-state inbound-process and timer-fire paths (`[const §VIII.5]`/`[const §XV.1]`); session-open and caller arenas only. `tools/check_alloc.py` under `mallocnesia` verifies (test seam #10).
- All public session surfaces `noexcept` across the inbound-process / timer-fire window; a throwing user callback **traps** rather than propagates (`core::detail::trap_throw`, `[arch §5.3]`, FR-015).
- Coroutines + ASIO native cancellation slots end-to-end; **no parallel `stop_token`** (`[const §XI.2]`, FR-014). `cancellable_dispatch(session_executor, slot, handler)` for the parser-completion → `fromAdmin`/`fromApp` hand-off (`[2d §6.5]`). Two-phase `Session::close(graceful)` runs Logout `async_write` + `Clock::sleep_until` timeout under a **child** cancellation state; phase-2 root `total` fires only after phase-1 resolves; `close(terminal)` skips phase 1; `close()` idempotent (`[2d §6.5]`).
- Callbacks on the per-session strand by default (`[const §XI.4]`/`[2d §4.5]` `threading_mode::per_session_strand`); `fixpp::sync::async_mutex` for serialized seqnum-counter mutation (`[const §XI.3]`/`[2f §7.3]`); plain `std::mutex` banned in any header including `asio::awaitable<...>` (`[const §XV.9]`).
- Durable-before-transmit: `co_await store->store(seq, committed_span, outbound)` **after** `wire::Writer::commit()`, **before** `transport::async_write`; a cancelled transmit MUST NOT leave a persisted-but-unsent inconsistency vs `[2e §root cause #1]` (the documented peer-ResendRequest-on-reconnect recovery case; recovery itself deferred).
- `seqnum_t` overflow is **session-fatal** with no wrap — surfaced via `[2e §6.7] store_seqnum_overflow` through a session-level error callback (`[2e §7.6]`, FR-008/FR-009); `seqnum_min = 1` (`[FIX-SL §4.1]`).
- All session time from `effective_clock = SessionConfig::clock_override ?: EngineConfig::clock`, resolved once at `Session::open` (`[2d §7.9]`, NFR-015) — never a direct wall-clock call.
- Session emits **no** `extern "C"` symbols; no session type appears in `<fix/c_api.h>` (`nm` check, `[const §X.2]`, FR-015).
- `MessageStore` is consumed as the `[2e §4.1]` **4-pure-virtual** seam (well within the `[const §XIV.2]` ≤5 cap; not redefined here — 2e owns it). `Clock` is the `[2d §4.1]` 4-pure-virtual seam (2d owns it). No new pluggable interface introduced by this feature.

**Scale/Scope:** ~8 public headers under `include/fixpp/session/` (`session`, `session_fsm`, `seqnum`, `admin_messages`, `heartbeat`, `sending_time`, `errors`, plus the consumed `message_store.hpp` re-pointed by 2e — see Cutover note) + `include/fixpp/core/fix_time.hpp` (folded time-helper #4, **core/ not session/**) + ~5 out-of-line `.cpp` under `src/session/` and `src/core/` + ~16 test files + ~4 bench harnesses + the in-scope `[FIX-TC]` conformance corpus + the cross-doc one-line edit promoting `2e`'s `seqnum.hpp` placeholder to the 005-owned type. ~N new `fixpp::core::error` variants appended at unused slots **43..N**, non-renumbering (`[const §X.4]`; research D-9 — `FIXPP_ERR_SESSION_*` prefix per the per-doc-prefix discipline). Estimate ~5000–5800 LOC hand-written (impl + tests + bench). Closes the `core/` time-helper module-exit row #4 and the `[2e §10 Q9]` cross-doc `seqnum_t` handoff in the same PR.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-evaluated post-Phase 1 design.* Canonical citation form `[const §<Roman>.<arabic>]` per `constitution.md:5`. **Mood:** at this `/speckit-plan`-stage gate the rows assert *planned conformance* (the plan reserves the artifacts/structure that will satisfy each article); delivered/verified evidence is produced by `/speckit-implement` + `/speckit-verify`, not here. The citation-verification pass at the end of this file was actually run.

| Article cited | Topic | How this feature satisfies it |
|---|---|---|
| `[const §I.1]`,`[const §I.3]`,`[const §I.4]` | Session layer scope; catalogue tracker; no silent omission | Owns S-001/2/3/4/7/8/9/15/16/19/20 + core time-helper #4; every owned row gets a `coverage-index.md` entry before merge; deferred TC cases recorded as deferred-with-traceability (Clarification Q2, research D-10). |
| `[const §II.1]` | C++23, no earlier fallback | Coroutines, concepts, `std::pmr`, `std::chrono`, `core::expected_t`, deducing `this`; no fallback. |
| `[const §III.2]` | Conan, pinned deps | **No new Conan row.** Reuses `core`/`sync`/`wire`/`dict`/2e-seam; GTest/Benchmark already pinned. |
| `[const §V.1]`,`[const §V.3]`,`[const §V.4]` | AGPL-3.0 dual; no LGPL; vendored attribution | No new dependency. Every new header carries `SPDX-License-Identifier: AGPL-3.0-or-later`. `async_mutex` (BSL-1.0 attribution) is consumed, not vendored here (owned by 2f). |
| `[const §VI.4]`,`[const §VI.5]` | Bidirectional traceability + Normative References | spec Authority anchor + Normative References list exact `[FIX-SL §X.Y]`/`[FIX-TC]` refs; `coverage-index.md` rows updated before merge; the `[2e §10 Q9]` `seqnum_t` handoff closes via a single-line re-export edit (`[const §VI.5]`). |
| `[const §VII.1]`,`[const §VII.3]` | GoogleTest + TDD | `tasks.md` ordered red-green-refactor; every C++ test target is GoogleTest; mock_clock + transport-double + store-double seams. |
| `[const §VII.5]`,`[const §VII.6]` | Conformance corpus + interop | In-scope `[FIX-TC]` subset as executable `tests/conformance/` scenarios (Q2 / research D-10), QuickFIX/J acceptance-definition oracle. Full QuickFIX Logon→…→Logout interop needs a live socket — deferred to `transport/`; this feature delivers the session-semantics substrate and the conformance subset. |
| `[const §VII.7]` | Fuzzing on parser-touching modules | **N/A** — the session FSM is not parser-touching (framing/parse fuzzed by `wire/`); session README exit criteria confirm no fuzz-harness gate. Recorded for explicit non-applicability (research D-12). |
| `[const §VIII.1]`,`[const §VIII.2]`,`[const §VIII.5]` | Benchmark + ±5% + zero hot-path alloc | `bench/session/*` + `bench/baselines/session/*.json` on the Technical-Context ceilings; three-no-alloc paths (inbound dispatch / timer fire / seqnum) verified by `tools/check_alloc.py` (seam #10). |
| `[const §VIII.4]` | Session throughput parity vs QuickFIX | Measured later under `transport/` real-socket interop; recorded as a v1.0 release gate, not a this-PR blocker (research D-13). |
| `[const §IX.1]` | ≥95% line / ≥85% branch on touched modules | Planned: `linux-clang-coverage` measures `include/fixpp/session/*`,`src/session/*`,`include/fixpp/core/fix_time.hpp`,`src/core/fix_time.cpp` as the Tier-1 gate; threshold asserted/enforced at `/speckit-verify` per the lcov DA/BRDA basis, not claimed met here. |
| `[const §IX.2]` | Tier-1 sanitizers | ASan+UBSan on every session test; **TSan** on the strand/async-mutex/cancellation tests (threading-affecting feature). |
| `[const §IX.4]` | Tier-1 static analysis | clang-tidy + clang-format + cppcheck + IWYU on all session/fix_time headers/sources; the `[const §XV.9]` `std::mutex`-in-coroutine grep gate applies. |
| `[const §IX.5]` | abidiff vs last tagged ABI | **N/A this PR** — no C-ABI surface (`[const §X.2]`, FR-015). Cited for explicit non-applicability (research D-12). |
| `[const §IX.6]` | Two-tier CI | Tier 1: every preset from quickstart §3. Tier 2: Windows manual/nightly. |
| `[const §X.2]`,`[const §X.5]` | No C++ leakage through C ABI; documented reentrancy | Session emits **no** `extern "C"` symbol; `nm` confirms (FR-015). Per-session-strand reentrancy contract documented per entry point in `contracts/` (FR-016, `[const §X.5]`). |
| `[const §X.4]` | Bounded `fixpp_error_t` + forwards-compat | New `session_*` `core::error` variants appended at unused slots 43..N, non-renumbering (research D-9; data-model "Error mapping"); C-ABI coalescing target `FIXPP_ERR_SESSION_*` documented for 2i (per-doc-prefix discipline); abi_history entry deferred to 2i under the same time-bounded waiver as 002/003/004 (no C-ABI surface here). |
| `[const §XI.1]`,`[const §XI.2]`,`[const §XI.3]`,`[const §XI.4]`,`[const §XI.5]`,`[const §XI.7]` | Coroutines; ASIO cancellation; async_mutex; per-session strand; lock policy; threading-affecting controls | FSM is coroutine-composed (XI.1); ASIO native slots, no `stop_token` (XI.2); `fixpp::sync::async_mutex` for the seqnum counter, no `std::mutex` in awaitable headers (XI.3/XV.9); per-session strand default (XI.4); `lock_policy` consumed, seqnum/store-write path mutex-always (XI.5); **threading-affecting → all four mandatory controls** (`/clarify` done, `/analyze` post-Gate-A, Codex Gate A, user `/plan` sign-off) per XI.7 / Appendix A. |
| `[const §XIII.3]` | Strand-stored trace context, no `thread_local` | Session reads `session_local<trace_context>` via the `session_executor` wrapper's `session_ptr()` accessor (`[2d §4.6]`/`[2d §7.9]`); no `thread_local` trace propagation. |
| `[const §XIV.2]` | ≤5 pure-virtual on pluggable interfaces | **No new pluggable interface.** `MessageStore` (4-pv, `[2e §4.1]`) and `Clock` (4-pv, `[2d §4.1]`) are *consumed*, defined/owned by 2e/2d; cap satisfied upstream, nothing owed here. |
| `[const §XV.1]`,`[const §XV.3]`,`[const §XV.9]`,`[const §XV.15]` | Banned: per-msg heap; global session lock; `std::mutex` in coroutine; app/session drop-oldest | Zero-alloc steady paths + arena (XV.1); per-session state, no coarse global lock (XV.3); `async_mutex` only in awaitable context (XV.9); seqnum contract never drops/wraps silently — overflow is session-fatal, no `drop-oldest` on the session path (XV.15). |
| `[const §XVI.3]` | `/clarify` mandatory pre-`/plan` (session FSM + threading + error semantics) | Ran 2026-05-17 (Q1 RecoveryPending / Q2 conformance subset / Q3 stale-SendingTime), OSS-reference-grounded; recorded in spec `## Clarifications`. |
| `[const §XVI.4]` | `/analyze` mandatory post-`/plan` | Runs after Gate A converges, before `/tasks` (`/speckit-analyze`). |
| `[const §XVII.1]` | Codex Gate A before `/tasks` (session FSM + threading + error-semantics triggers) | `gate_a_required: yes` (Appendix A "Session FSM" + "Threading / concurrency" + "Error semantics"). There is **no Phase-2 FSM design doc by design** (`[2e §3.1]` defers the FSM to this Phase-4 spec) — so the Phase-4 bundle Gate A is the first and authoritative FSM design review; `/gate-a 005-session-establishment-fsm` runs before `/tasks`; both Codex passes (rescue + `/codex:adversarial-review`) per `feedback_gate_a_codex_dual_pass`. |
| `[const §XVII.2]`,`[const §XVII.3]` | Gate B before merge; author≠reviewer | Standard Gate B precondition; Opus `/plan` author independent of Codex Gate A reviewer per `/gate-a`. |
| `[const §XVII.7]` | Local pre-PR build gate | Contributor confirms `local build: green on linux-clang-debug @ <sha>`; agent surfaces `AskUserQuestion` before any local Conan/CMake build. |
| `[const §XVII.8]` | `/speckit-verify` mandatory post-`/implement` | `/speckit-verify 005-session-establishment-fsm` → `.specify/decisions/005-session-establishment-fsm-verify.md`; `GREEN`/`YELLOW` required for the gate-b label per the evidence rule. |

**Gates — PASS (planned-conformance; no violation requiring justification); Complexity Tracking empty.** No new pluggable interface (the `[const §XIV.2]` cap is satisfied upstream at 2e/2d). The `RecoveryPending` state is an explicit FSM seam (Clarification Q1) consistent with `[FIX-SL §4.8.2]` and the fix8 explicit-state precedent — it adds no constitution/anchor amendment (the anchors defer the FSM here by design, `[2e §3.1]`). Promoting `2e`'s `seqnum_t` placeholder to the 005-owned type is the planned `[2e §10 Q9]` handoff, a single-line re-export (`[const §VI.5]`), not an amendment. All cited articles resolve under canonical form (see Citation verification pass).

## Project Structure

### Documentation (this feature)

```text
specs/005-session-establishment-fsm/
├── plan.md              # this file (/speckit-plan 2026-05-17)
├── spec.md              # /specify + /clarify 2026-05-17 (Q1 RecoveryPending / Q2 conformance / Q3 stale-SendingTime)
├── research.md          # Phase 0 — design decisions D-1..D-13 (anchored to 2e v0.4 / 2d v0.4 / 2f v1.5 / FIX-SL / FIX-TC)
├── data-model.md        # Phase 1 — Session/FSM/seqnum entities, the [FIX-SL §4.10] transition matrix, error slots 43..N, SessionConfig-consumed fields
├── quickstart.md        # Phase 1 — build / test / bench / sanitizer / coverage / verify / gate-a / gate-b / time-helper-exit close
├── contracts/
│   ├── session_fsm.hpp           # the 7-state FSM + the [FIX-SL §4.10] transition matrix (RecoveryPending entry/hold/surface; exits = deferred seam)
│   ├── session.hpp               # Session lifecycle: open/close(graceful|terminal), fromAdmin/fromApp, send path, reentrancy contract per entry point
│   ├── admin_messages.hpp        # Logon/Logout/Heartbeat/TestRequest/Reject session semantics over the wire/dict surfaces
│   ├── seqnum.hpp                # the 005-OWNED seqnum_t (promotes the [2e §4.7] placeholder) + seqnum_min/max + the seqnum manager contract
│   ├── sending_time.hpp          # SendingTime(52) stamp/validate + MaxLatency disposition (Clarification Q3)
│   ├── fix_time.hpp              # folded core/ time-helper #4: utc_time_to_fix_string / fix_string_to_utc_time (core/, not session/)
│   ├── session_config_consumed.hpp # the [2d §4.5] SessionConfig fields this feature consumes + the threshold-default values 005 owns
│   └── session_errors.hpp        # the session_* core::error variants (slots 43..N) + FIXPP_ERR_SESSION_* C-ABI coalescing target (2i-owned)
├── checklists/
│   └── requirements.md  # /specify quality checklist (all pass; clarifications resolved)
└── tasks.md             # Phase 2 (/speckit-tasks — NOT created here)
```

### Source Code (library submodule root)

```text
include/fixpp/session/
├── seqnum.hpp                  # 005 OWNS: using seqnum_t = std::uint32_t; seqnum_min=1; seqnum_max — promotes the [2e §4.7] placeholder (D-1)
├── session.hpp                 # Session class decl: FSM driver, open/close, fromAdmin/fromApp, send path (coroutine bodies in src/)
├── session_fsm.hpp             # FSM state enum + transition table type ([FIX-SL §4.10]; RecoveryPending per Clarification Q1)
├── admin_messages.hpp          # build/interpret Logon/Logout/Heartbeat/TestRequest/Reject over wire::Writer / typed dict access
├── heartbeat.hpp               # heartbeat / test-request / graceful-close timing decls (effective_clock-driven)
├── sending_time.hpp            # inbound SendingTime(52) MaxLatency validation + Q3 disposition decl
└── errors.hpp                  # session_* error helpers (variants live in core/error.hpp)

src/session/
├── CMakeLists.txt              # fixpp_session target + bench/test wiring
├── session.cpp                 # FSM driver coroutine bodies; Logon/Logout handshake; cancellable_dispatch hand-off; two-phase close
├── admin_messages.cpp          # admin-message serialize/interpret bodies
├── seqnum_manager.cpp          # inbound-expected / outbound-next counters; async_mutex bookkeeping; overflow → store_seqnum_overflow surface
└── heartbeat.cpp               # heartbeat/test-request/graceful-close timer logic over Clock::steady_now/sleep_until

include/fixpp/core/fix_time.hpp # FOLDED time-helper #4 (core/ module-exit row): utc_time_to_fix_string / fix_string_to_utc_time + duration alias (D-3)
src/core/fix_time.cpp           # UTCTimestamp grammar format/parse impl (YYYYMMDD-HH:MM:SS[.sss[sss]])

include/fixpp/core/error.hpp    # ADDITIVE EDIT: append session_* variants at unused slots 43..N (non-renumbering, [const §X.4]; D-9)

# Cross-doc handoff — [2e §10 Q9] close (single-line re-export, [const §VI.5]):
include/fixpp/session/message_store.hpp  # 2e-OWNED seam, CONSUMED here (NOT redefined). 2e's <fixpp/session/seqnum.hpp> placeholder
                                         #   is promoted: the file becomes 005's canonical type; 2e's include keeps resolving unchanged
                                         #   (value/constants byte-identical to the placeholder — D-1).

tests/session/
├── CMakeLists.txt
├── conformance/                # Q2 in-scope [FIX-TC] subset as parameterized GTest, authored from the QuickFIX/J .def oracle (D-10)
│   ├── tc_establishment_test.cpp        # TC-001/002 establishment + CompID/BeginString/first-msg-not-Logon
│   ├── tc_seqnum_test.cpp               # TC-002 in-seq + too-low (fatal); too-high → RecoveryPending entry/hold/surface
│   ├── tc_liveness_test.cpp             # TC-004 heartbeat / test-request
│   ├── tc_reject_test.cpp               # TC-005 session-level reject taxonomy (14a..j analogues)
│   ├── tc_logout_test.cpp               # TC-009 logout incl. timeout force-disconnect
│   └── tc_sendingtime_test.cpp          # SendingTime/MaxLatency (Q3) + version gating
├── fsm_transition_matrix_test.cpp       # seam #1 — every [FIX-SL §4.10] state×event cell has a defined transition (no UB)
├── logon_handshake_test.cpp             # seam #2 — initiator↔acceptor reach Active; HeartBtInt negotiation; refusals
├── seqnum_manager_test.cpp              # seam #3 — increment-by-one; too-low fatal; too-high RecoveryPending; overflow session-fatal no-wrap
├── recovery_pending_test.cpp            # seam #4 — gap entry, message held (not processed/counted), surfaced, not disconnected, no ResendRequest emitted
├── heartbeat_testrequest_test.cpp       # seam #5 — mock-clock heartbeat once-per-window; TestReqID echo; unanswered → unhealthy; HeartBtInt=0 disables
├── logout_exchange_test.cpp             # seam #6 — graceful both-directions; never-confirmed → clock-bound force-disconnect; non-Active Logout transitions
├── session_reject_test.cpp              # seam #7 — RefSeqNum/RefTagID/RefMsgType/SessionRejectReason; no reject loop
├── sending_time_test.cpp                # seam #8 — MaxLatency breach → Reject(10)→Logout→disconnect; Logon→logout-with-error (Q3)
├── fix_time_roundtrip_test.cpp          # seam #9 — format↔parse lossless over epoch/leap-adjacent/sub-second corpus (time-helper #4)
├── durable_before_transmit_test.cpp     # seam #10 — store(outbound) before transport; cancelled transmit leaves no persisted-but-unsent inconsistency
├── cancellation_two_phase_test.cpp      # seam #11 — cancellable_dispatch parse→fromAdmin; child-state Logout/timeout; close idempotent
├── alloc_discipline_test.cpp            # seam #12 — zero global new/delete on inbound-dispatch / timer-fire / seqnum paths (mallocnesia)
└── seqnum_t_handoff_test.cpp            # seam #13 — 2e's placeholder include now resolves to the 005-owned seqnum_t (build/consumer check, SC-010)

tests/support/
├── transport_double.hpp        # seam #0 — in-memory bidirectional frame transport (feeds inbound / captures outbound)
├── store_double.hpp            # seam #0 — in-memory test-double MessageStore satisfying the [2e §4.1] seam (NOT the S-012 impl)
└── (reuses tests/support/ mock_clock via [2d §4.3] fixpp::core::mock_clock)

bench/session/
├── fsm_bench.cpp / seqnum_bench.cpp / fix_time_bench.cpp / heartbeat_bench.cpp   # seam #5 ceilings
bench/baselines/session/*.json  # ±5% regression baselines
```

**Structure Decision:** Single-project library layout, `session/` module (first feature) per `[arch §4.3]`/`[arch §4.4]`. The FSM driver, admin-message handling, seqnum manager, and heartbeat logic split out-of-line `.cpp` (coroutine bodies, compile-time). `seqnum.hpp` is header-only and **005-owned** — it promotes the `[2e §4.7]` placeholder in place: the file `include/fixpp/session/seqnum.hpp` keeps its path so every existing `2e` include resolves unchanged, but the `// PLACEHOLDER` comment is removed and ownership transfers to 005 (value/constants byte-identical, so 2e consumers compile without edit — the `[2e §10 Q9]` cross-doc handoff, `[const §VI.5]`). **The time-helper #4 lives in `core/`, not `session/`** (`include/fixpp/core/fix_time.hpp` + `src/core/fix_time.cpp`): it is the deferred `core/` module-exit row, consumed by `session/` for `SendingTime(52)` — folding it here closes `core/`'s exit and the standalone `002-core-time` is not created (per the 2026-05-17 carry-in resolution, session README). `MessageStore` (`include/fixpp/session/message_store.hpp`) is the **2e-owned seam, consumed not redefined**; the in-mem/file impls (S-012/S-013) and the recovery FSM walking `retrieve()`/the visitor are explicitly out of scope (FR-010/FR-017) — a `tests/support/store_double.hpp` satisfies the seam.

### Test-seam → file mapping (every seam bound to explicitly named on-disk files — no globs)

| Seam | File | Spec link |
|---|---|---|
| #0 transport-double / store-double / mock_clock | `tests/support/transport_double.hpp`, `tests/support/store_double.hpp`, `[2d §4.3]` `mock_clock` | FR-010/011, all US |
| #1 FSM transition matrix (no undefined cell) | `tests/session/fsm_transition_matrix_test.cpp` | FR-001, edge cases |
| #2 Logon handshake (both roles) + HeartBtInt | `tests/session/logon_handshake_test.cpp` | FR-002/003/004, US1, SC-001/002 |
| #3 seqnum manager | `tests/session/seqnum_manager_test.cpp` | FR-008/009, US2, SC-003 |
| #4 RecoveryPending (Clarification Q1) | `tests/session/recovery_pending_test.cpp` | FR-001/008/017, US2#3, SC-003 |
| #5 heartbeat / test-request | `tests/session/heartbeat_testrequest_test.cpp` + `bench/session/heartbeat_bench.cpp` | FR-006, US3, SC-004 |
| #6 logout exchange | `tests/session/logout_exchange_test.cpp` | FR-005, US4, SC-005 |
| #7 session-level reject | `tests/session/session_reject_test.cpp` | FR-007, US5, SC-006 |
| #8 SendingTime / MaxLatency (Clarification Q3) | `tests/session/sending_time_test.cpp` | FR-013, US5#2, SC-007 |
| #9 time-helper #4 round-trip | `tests/session/fix_time_roundtrip_test.cpp` | FR-012, US5#3, SC-007/010 |
| #10 durable-before-transmit + alloc | `tests/session/durable_before_transmit_test.cpp`, `tests/session/alloc_discipline_test.cpp`, `tools/check_alloc.py` | FR-010/011, US2#5, SC-009 |
| #11 cancellation two-phase | `tests/session/cancellation_two_phase_test.cpp` | FR-014, edge cases, SC-005/009 |
| #12 alloc discipline (mallocnesia) | `tests/session/alloc_discipline_test.cpp` | FR-015, SC-009 |
| #13 `seqnum_t` handoff (build/consumer) | `tests/session/seqnum_t_handoff_test.cpp` | FR-009, SC-010 |
| (conformance) Q2 in-scope `[FIX-TC]` subset | `tests/session/conformance/tc_*.cpp` (6 files) ← QuickFIX/J `.def` oracle | FR-018, SC-008 (D-10) |

## Complexity Tracking

> Empty — no justified constitution violation. No new pluggable interface (the `[const §XIV.2]` ≤5 cap is satisfied upstream at `[2e §4.1]`/`[2d §4.1]`); `RecoveryPending` is an explicit-state seam consistent with `[FIX-SL §4.8.2]` and the fix8 precedent, not an amendment; the `seqnum_t` promotion is the planned `[2e §10 Q9]` single-line handoff (`[const §VI.5]`); no `[const §XX]` amendment required.

## Gate A

`gate_a_required: yes` — `[const §XVII.1]` / Appendix A: **Session FSM** (state additions, recovery semantics) **+ Threading / concurrency** (per-session strand, async-mutex, two-phase cancellation) **+ Error semantics** (new `session_*` `core::error` variants). This is the **first FSM design review of record** — there is no Phase-2 FSM design doc by design (`[2e §3.1]` defers the FSM to this Phase-4 spec), so the Gate A bundle reviews the FSM design itself, not a pre-converged sibling doc. Inherited cross-doc seams (`[2e §4.1]` store contract, `[2d §4.1]`/`[2d §4.5]`/`[2d §6.5]` clock/config/cancellation, `[2f §4.1]` async_mutex) are *converged upstream* (2e v0.4 / 2d v0.4 / 2f v1.5 all Gate-A-converged) — consumed, not re-litigated. `/gate-a 005-session-establishment-fsm` runs **before `/tasks`**; both Codex passes (rescue + `/codex:adversarial-review`) per `feedback_gate_a_codex_dual_pass`; Opus triages, Opus rewrites.

### Round 1 — TBD (runs pre-`/tasks`)

## Gate B

### Round 1 — TBD (post-`/implement`, pre-merge; `/speckit-verify` GREEN/YELLOW precondition per `[const §XVII.8]`)

## Citation verification pass (round 0 — run at /plan authoring, 2026-05-17)

All `[const §...]`, `[2e §...]`, `[2d §...]`, `[2f §...]`, `[FIX-SL §...]`, `[FIX-TC]`, `[arch §...]` citations in this plan resolve against: `.specify/constitution.md` (v0.1), `.specify/2e-msgstore.md` (v0.4), `.specify/2d-threading.md` (v0.4), `.specify/2f-async-mutex.md` (v1.5), `spec/coverage-index.md` (FIX-SL/FIX-TC section structure), `spec/feature-catalogue.md` (S-001..S-025), `include/fixpp/core/error.hpp` (occupied slots 1,10–13,20–29,30–42 → next free = 43).

Verified resolutions (sampled): `[2e §4.1]` (4 pure-virtual `store`/`retrieve`/`next_seqnum`/`reset` + `retrieve_visitor`, `include/fixpp/session/message_store.hpp`); `[2e §4.7]` (`using seqnum_t = std::uint32_t` placeholder, owner = Phase-4 session spec, `include/fixpp/session/seqnum.hpp`); `[2e §6.7]` (`store_seqnum_overflow` session-fatal); `[2e §3.1]`/`[2e §10 Q9]`/`[2e §7.6]` (FSM owned by Phase-4 spec; `seqnum_t` handoff; consumer callsite ordering); `[2d §4.1]` (`fixpp::core::Clock` 4-pv `now`/`steady_now`/`sleep_until`/`cancel_sleeps`, `include/fixpp/core/clock.hpp`); `[2d §4.5]` (`SessionConfig` — `sender_comp_id`/`target_comp_id`/`begin_string` + `heartbeat_interval`/`test_request_threshold`/`sending_time_threshold` values owned by session-module spec; `RejectPolicy reject_policy`); `[2d §7.9]` (`effective_clock = clock_override ?: EngineConfig::clock`, resolved once at open); `[2d §6.5]` (`cancellable_dispatch`, two-phase close, `session_already_closed`/`invalid_session_config`/`executor_not_serialised`); `[2f §7.3]`/`[2f §7.6]` (async_mutex for seqnum counter, graceful-close `cancel_and_drain`); `[FIX-SL §4.10]` state matrix, `§4.1` sequence numbers, `§4.2.1/2` BeginString/CompID, `§4.3.4` HeartBtInt, `§4.5.4` Reject, `§4.6` logout, `§4.2.3` SendingTime; `[FIX-TC]` TC-001..TC-017 (coverage-index lines 365–386, 20 scenarios → TC mapping); QuickFIX/J acceptance defs `reference-engines/quickfixj/.../definitions/server/fix4{0..4},fix50` @ QFJ_RELEASE_3_0_1 (481 `.def` files, version-partitioned — the Q2 oracle). No remaining vague or dead refs.

## Phase-2 input checklist (for `/tasks`)

- [x] Spec FR-001..018 + SC-001..010 ↔ `[FIX-SL §4]` / `[2e §4.1]` / `[2d §4.1/§4.5/§6.5]` / `[2f §7.3]` mapped (data-model + this plan)
- [x] All 14 test seams + Q2 conformance subset bound to explicitly named files (Test-seam mapping; no globs)
- [x] Clarification Q1 fixed: explicit `RecoveryPending` state — entry/hold/surface owned here, exits = documented deferred seam (research D-2)
- [x] Clarification Q2 fixed: concrete TC-### in/deferred split mapped onto the QuickFIX/J `.def` oracle (research D-10)
- [x] Clarification Q3 fixed: stale-SendingTime → `Reject(10)`→`Logout`→disconnect (Logon→logout-with-error) (research D-3 / contracts/sending_time.hpp)
- [x] `seqnum_t` width + `[2e §10 Q9]` handoff mechanism fixed (research D-1: `uint32_t`, single-line in-place promotion)
- [x] Time-helper #4 home + grammar fixed (research D-3: `core/fix_time.hpp`, `YYYYMMDD-HH:MM:SS[.sss[sss]]`); closes `core/` exit
- [x] Threshold defaults 005 owns fixed (research D-8: heartbeat 30 s, test-request 1×HeartBtInt, MaxLatency 120 s, `strict_reject_then_logout`)
- [x] Error slots fixed (research D-9: `session_*` at 43..N, `FIXPP_ERR_SESSION_*`, non-renumbering)
- [x] Constitution Check PASS, Complexity Tracking empty
- [ ] Gate A converged (runs before `/tasks`)
- [ ] `/analyze` drift check (post-Gate-A, pre-`/tasks`)
