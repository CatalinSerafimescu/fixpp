# Implementation Plan — 005-session-establishment-fsm

**Branch**: `005-session-establishment-fsm` | **Date**: 2026-05-17 | **Spec**: [spec.md](spec.md)
**Design anchors**: `.specify/2e-msgstore.md` **v0.4** (the FSM *consumes* the `MessageStore` seam; `seqnum_t` ownership re-pointed here per `[2e §3.1]`/`[2e §10 Q9]`), `.specify/2d-threading.md` **v0.4** (`fixpp::core::Clock` seam, `effective_clock`, `cancellable_dispatch`, two-phase close, `SessionConfig`), `.specify/2f-async-mutex.md` **v1.5** (`fixpp::sync::async_mutex` for the seqnum counter), plus the **FIX Session Layer** standard (`[FIX-SL]`) + **FIX Session Layer Test Cases** (`[FIX-TC]` TC-001..TC-017). On conflict the anchor wins; an inconsistency is a defect in this plan.

## Summary

Deliver the connection-lifecycle slice of the `fixpp::session` module — the FIX **session FSM** plus the five admin messages that drive it — implementing catalogue rows **S-001, S-002, S-003, S-004, S-007, S-008, S-009, S-015, S-016, S-019, S-020** and the folded **`core/` time-helper surface row #4** (closing the `core/` module-exit item). A `Session` runs a 6-state FSM (`NotConnected → LogonSent → LogonReceived → Active → LogoutSent → Disconnected`, the `[FIX-SL §4.10]` state set — **no bundle-invented `RecoveryPending` half-state**, removed Session-2026-05-18 / Gate A round 1: a too-high `MsgSeqNum` gap is session-fatal because ResendRequest recovery is deferred), drives `Logon(35=A)` / `Logout(35=5)` / `Heartbeat(35=0)` / `TestRequest(35=1)` / `Reject(35=3)` for both initiator and acceptor roles, maintains the inbound-expected / outbound-next `MsgSeqNum(34)` counters, negotiates `HeartBtInt(108)`, validates `SenderCompID(49)`/`TargetCompID(56)` (point-to-point portion of S-016 only) and `BeginString(8)` version gating (FIX.4.2/4.4; FIXT.1.1/5.0SP2 not claimed), and stamps/validates `SendingTime(52)` against `MaxLatency` through the effective-clock seam. It **publishes `seqnum_t`** (`<fixpp/session/seqnum.hpp>`) — promoting the placeholder `2e-msgstore` consumed — and **consumes** the `[2e §4.1]` `MessageStore` 4-pure-virtual seam under durable-before-transmit ordering (a test-double store satisfies it; the in-mem/file impls S-012/S-013 are out of scope). The FSM composes via coroutines + ASIO native cancellation slots on the per-session strand, uses `fixpp::sync::async_mutex` for the seqnum counter, and reports every failure through `expected_t<T>` / `fixpp::core::error` with **no C++ type across the C ABI**.

Per `/speckit-clarify` 2026-05-17 + the Gate A round-1 re-clarification (Session-2026-05-18): **Q1 (re-scoped Session-2026-05-18 — the 2026-05-17 OSS survey was verified inverted against the bound oracle)** — a too-high `MsgSeqNum` gap is **session-fatal for 005** (Logout-with-text → disconnect; no `RecoveryPending` state); the real ResendRequest-driven recovery is the deferred session-recovery feature's, forward-referenced at the `[2e §3.1]` Phase-4 hand-off. **Q2** — a **capability-partitioned `[FIX-TC]` subset** ships green in this PR (FIX.4.2/4.4); the concrete TC-### → in/deferred split is fixed in this plan (research D-10) against the QuickFIX/J acceptance-definition corpus as the executable oracle; there is **no constitutional scoped-subset allowance**, so `[const §VII.5]` is **NOT SATISFIED** for the deferred cases and proceeds under an explicit **Article XVII §1 recorded Gate-A-blocker waiver** (`[const §XVII.1]`; the article itself is not waived), not a satisfied gate (see Constitution Check + Complexity Tracking). **Q3** — inbound `SendingTime(52)` beyond `MaxLatency` → `Reject(SessionRejectReason=10, ref tag 52)` → `Logout` → disconnect (Logon → logout-with-error, no standalone reject).

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

**Scale/Scope:** ~8 public headers under `include/fixpp/session/` (`session`, `session_fsm`, `seqnum`, `admin_messages`, `heartbeat`, `sending_time`, `errors`, plus the consumed `message_store.hpp` re-pointed by 2e — see Cutover note; each public header has a matching `contracts/*.hpp` shape oracle, incl. the Gate-A-round-1-added `contracts/heartbeat.hpp`) + `include/fixpp/core/fix_time.hpp` (folded time-helper #4, **core/ not session/**) + ~5 out-of-line `.cpp` under `src/session/` and `src/core/` + ~16 test files + ~4 bench harnesses + the in-scope `[FIX-TC]` conformance corpus (FIX.4.2/4.4; the rest deferred-with-traceability — `[const §VII.5]` is a recorded scoped WAIVER, see Constitution Check + Complexity Tracking) + the cross-doc one-line edit promoting `2e`'s `seqnum.hpp` placeholder to the 005-owned type. ~11 new `fixpp::core::error` variants at proposed slots **43..53** plus the cross-doc-coordinated `[2d]` set at **54..N** — a *planned* allocation pinned at Gate A / `/speckit-tasks`, non-renumbering only once published (`[const §X.4]`; research/data-model D-9 — `FIXPP_ERR_SESSION_*` prefix). Estimate ~5000–5800 LOC hand-written (impl + tests + bench). Closes the `core/` time-helper module-exit row #4 and the `[2e §10 Q9]` cross-doc `seqnum_t` handoff in the same PR.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-evaluated post-Phase 1 design.* Canonical citation form `[const §<Roman>.<arabic>]` per `constitution.md:5`. **Mood:** at this `/speckit-plan`-stage gate the rows assert *planned conformance* (the plan reserves the artifacts/structure that will satisfy each article); delivered/verified evidence is produced by `/speckit-implement` + `/speckit-verify`, not here. The citation-verification pass at the end of this file was actually run.

| Article cited | Topic | How this feature satisfies it |
|---|---|---|
| `[const §I.1]`,`[const §I.3]`,`[const §I.4]` | Session layer scope; catalogue tracker; no silent omission | Owns the **FIX.4.2/4.4 point-to-point establishment slice** of S-001/2/3/4/7/8/9/15/16/19/20 + core time-helper #4. The scope-deferral ledger **is recorded now** in `spec/coverage-index.md` (the **005 session-establishment — scope-deferral ledger** section + per-cell `005-defer` notes on the FIX-TC scenario rows and `[FIX-SL §6.2]`): version-scope (4.0/4.1/4.3/5.0 + FIXT.1.1/5.0SP2), S-016 115/128 third-party addressing, too-high + recovery-dependent TC cases, and the scenario-14 `14h`/`14i`/`14j` cases. Per-row delivery-coverage entries for the owned FIX.4.2/4.4 rows are finalized at `/speckit-implement`/merge as those tests land. No silent narrowing — every deferred portion is recorded deferred-with-traceability (Session-2026-05-18, research D-10). |
| `[const §II.1]` | C++23, no earlier fallback | Coroutines, concepts, `std::pmr`, `std::chrono`, `core::expected_t`, deducing `this`; no fallback. |
| `[const §III.2]` | Conan, pinned deps | **No new Conan row.** Reuses `core`/`sync`/`wire`/`dict`/2e-seam; GTest/Benchmark already pinned. |
| `[const §V.1]`,`[const §V.3]`,`[const §V.4]` | AGPL-3.0 dual; no LGPL; vendored attribution | No new dependency. Every new header carries `SPDX-License-Identifier: AGPL-3.0-or-later`. `async_mutex` (BSL-1.0 attribution) is consumed, not vendored here (owned by 2f). |
| `[const §VI.4]`,`[const §VI.5]` | Bidirectional traceability + Normative References | spec has the Authority-anchors blockquote **and** a dedicated `## Normative References` section (added Gate A round 2) listing the exact `[FIX-SL §X.Y.Z] Title` / `[FIX-TC §4]` / `[2e §...]`/`[2d §...]`/`[2f §...]`/`[arch §...]`/`[const §...]` entries per `[const §VI.5]` (`constitution.md:80`); the `spec/coverage-index.md` 005 scope-deferral ledger is recorded now (per-row delivery-coverage finalized at implement/merge); the `[2e §10 Q9]` `seqnum_t` handoff closes via a single-line re-export edit. |
| `[const §VII.1]`,`[const §VII.3]` | GoogleTest + TDD | `tasks.md` ordered red-green-refactor; every C++ test target is GoogleTest; mock_clock + transport-double + store-double seams. |
| `[const §VII.5]` | Conformance corpus (full TC-001..TC-017 every PR) | **NOT SATISFIED for the deferred TC cases — feature proceeds under an explicit Article XVII §1 recorded Gate-A-blocker waiver.** `[const §VII.5]` requires *every* PR to pass the full TC corpus; the article itself is **not** waived (no article-level exemption is claimed, and there is no constitutional scoped-subset allowance — the bundle's prior `[const §V.5]` citation was fabricated; `[const §V.5]` is the README-disclaimer rule). 005 ships only the in-scope subset green (establishment/liveness/logout/reject/in-seq+too-low/SendingTime-CompID-version, FIX.4.2/4.4); recovery-dependent + too-high + FIXT/5.0SP2 + 4.0/4.1/4.3/5.0 cases are deferred-with-traceability in `coverage-index.md` (the **005 scope-deferral ledger**). The unmet `[const §VII.5]` requirement is a **Gate-A blocker explicitly waived with recorded rationale before `/tasks`** per `[const §XVII.1]` (`constitution.md:255`: "Blockers from Gate A must be resolved or explicitly waived with rationale before `/tasks` runs"); the waiver entry + rejected alternatives are in Complexity Tracking. Full-corpus conformance is recorded debt discharged when the session-recovery feature lands. (No Article XX amendment in this bundle; no claim that Article VII §5 itself is waived.) |
| `[const §VII.6]` | Interop (QuickFIX Logon→…→Logout) | Full QuickFIX live-socket interop needs a real socket + app layer — deferred to `transport/` (research D-13), recorded as a v1.0 release gate, not a this-PR blocker. This feature delivers the session-semantics substrate + the in-scope conformance subset. |
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
| `[const §XVI.3]` | `/clarify` mandatory pre-`/plan` (session FSM + threading + error semantics) | Ran 2026-05-17 (Q1/Q2/Q3); **re-clarified Session-2026-05-18 (Gate A round 1): Q1 re-scoped to too-high=session-fatal/no-RecoveryPending after the 2026-05-17 OSS survey was verified inverted; Q2's `[const §V.5]` allowance retracted as fabricated**. All recorded in spec `## Clarifications`. |
| `[const §XVI.4]` | `/analyze` mandatory post-`/plan` | Runs after Gate A converges, before `/tasks` (`/speckit-analyze`). |
| `[const §XVII.1]` | Codex Gate A before `/tasks` (session FSM + threading + error-semantics triggers) | `gate_a_required: yes` (Appendix A "Session FSM" + "Threading / concurrency" + "Error semantics"). There is **no Phase-2 FSM design doc by design** (`[2e §3.1]` defers the FSM to this Phase-4 spec) — so the Phase-4 bundle Gate A is the first and authoritative FSM design review; `/gate-a 005-session-establishment-fsm` runs before `/tasks`; both Codex passes (rescue + `/codex:adversarial-review`) per `feedback_gate_a_codex_dual_pass`. |
| `[const §XVII.2]`,`[const §XVII.3]` | Gate B before merge; author≠reviewer | Standard Gate B precondition; Opus `/plan` author independent of Codex Gate A reviewer per `/gate-a`. |
| `[const §XVII.7]` | Local pre-PR build gate | Contributor confirms `local build: green on linux-clang-debug @ <sha>`; agent surfaces `AskUserQuestion` before any local Conan/CMake build. |
| `[const §XVII.8]` | `/speckit-verify` mandatory post-`/implement` | `/speckit-verify 005-session-establishment-fsm` → `.specify/decisions/005-session-establishment-fsm-verify.md`; `GREEN`/`YELLOW` required for the gate-b label per the evidence rule. |

**Gates — PASS except `[const §VII.5]`, which is NOT SATISFIED for the deferred TC cases and proceeds under an explicit Article XVII §1 recorded Gate-A-blocker waiver (rationale in Complexity Tracking); no other violation requiring justification.** `[const §VII.5]` (full TC corpus every PR) is **not satisfied** by this feature and there is **no constitutional scoped-subset allowance**; the article itself is not waived — the unmet requirement is recorded and waived as a Gate-A blocker per `[const §XVII.1]` (`constitution.md:255`), not greened. No new pluggable interface (the `[const §XIV.2]` cap is satisfied upstream at 2e/2d). The FSM is the 6-state `[FIX-SL §4.10]` set with **no bundle-invented `RecoveryPending` half-state** (removed Session-2026-05-18; too-high gap = session-fatal because ResendRequest recovery is deferred) — it adds no constitution/anchor amendment (the anchors defer the *recovery* FSM to the Phase-4 line by design, `[2e §3.1]`/`[2e §4 last bullet]` — the deferred session-recovery feature, not this slice). Promoting `2e`'s `seqnum_t` placeholder to the 005-owned type is the planned `[2e §10 Q9]` handoff, a single-line re-export (`[const §VI.5]`), not an amendment. All cited articles resolve under canonical form (see Citation verification pass).

## Project Structure

### Documentation (this feature)

```text
specs/005-session-establishment-fsm/
├── plan.md              # this file (/speckit-plan 2026-05-17)
├── spec.md              # /specify + /clarify 2026-05-17 + Gate A round 1 re-clarify 2026-05-18 (Q1 too-high=fatal/no-RecoveryPending / Q2 §VII.5 waiver / Q3 stale-SendingTime)
├── research.md          # Phase 0 — design decisions D-1..D-13 (anchored to 2e v0.4 / 2d v0.4 / 2f v1.5 / FIX-SL / FIX-TC)
├── data-model.md        # Phase 1 — Session/FSM/seqnum entities, the [FIX-SL §4.10] transition matrix, error slots 43..N, SessionConfig-consumed fields
├── quickstart.md        # Phase 1 — build / test / bench / sanitizer / coverage / verify / gate-a / gate-b / time-helper-exit close
├── contracts/
│   ├── session_fsm.hpp           # the 6-state FSM + the [FIX-SL §4.10] transition matrix (NO RecoveryPending — too-high = session-fatal, Session-2026-05-18)
│   ├── session.hpp               # Session lifecycle: open/close(graceful|terminal), fromAdmin/fromApp, send path, reentrancy contract per entry point
│   ├── admin_messages.hpp        # Logon/Logout/Heartbeat/TestRequest/Reject session semantics over the wire/dict surfaces (S-016 49/56-only)
│   ├── heartbeat.hpp             # heartbeat / test-request / graceful-close TIMING entry points + reentrancy contract (added Gate A round 1, F-03)
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
├── session_fsm.hpp             # FSM state enum (6 states) + transition table type ([FIX-SL §4.10]; no RecoveryPending — Session-2026-05-18)
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
│   ├── tc_seqnum_test.cpp               # TC-002 in-seq + too-low (fatal, oracle 2c_MsgSeqNumTooLow) + 2q/2r MsgType. NOT a too-high oracle case: the too-high `.def` cases (1a_ValidLogonMsgSeqNumTooHigh, 2b_MsgSeqNumTooHigh) are DEFERRED per research D-10 — 005's own too-high fatal disposition (Logout-with-text→disconnect, no 35=2) is tested at seam #4 (seqnum_gap_fatal_test.cpp), not here, and has no QFJ oracle case in scope.
│   ├── tc_liveness_test.cpp             # TC-004 heartbeat / test-request
│   ├── tc_reject_test.cpp               # TC-005 (scenario 7 Receive Reject) + TC-010 (scenario 14 message validation 14a..g)
│   ├── tc_logout_test.cpp               # TC-009 logout incl. timeout force-disconnect
│   └── tc_sendingtime_test.cpp          # SendingTime/MaxLatency (Q3) + version gating
├── fsm_transition_matrix_test.cpp       # seam #1 — every [FIX-SL §4.10] state×event cell has a defined transition (no UB)
├── logon_handshake_test.cpp             # seam #2 — initiator↔acceptor reach Active; HeartBtInt negotiation; refusals
├── seqnum_manager_test.cpp              # seam #3 — increment-by-one; too-low fatal; too-high session-fatal (no-wrap overflow too)
├── seqnum_gap_fatal_test.cpp            # seam #4 — too-high gap: session_seqnum_gap_unrecoverable surfaced, orderly Logout+disconnect, NO ResendRequest (recovery deferred)
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
| #4 too-high gap = session-fatal (Session-2026-05-18) | `tests/session/seqnum_gap_fatal_test.cpp` | FR-001/008/017, US2#3, SC-003 |
| #5 heartbeat / test-request | `tests/session/heartbeat_testrequest_test.cpp` + `bench/session/heartbeat_bench.cpp` | FR-006, US3, SC-004 |
| #6 logout exchange | `tests/session/logout_exchange_test.cpp` | FR-005, US4, SC-005 |
| #7 session-level reject | `tests/session/session_reject_test.cpp` | FR-007, US5, SC-006 |
| #8 SendingTime / MaxLatency (Clarification Q3) | `tests/session/sending_time_test.cpp` | FR-013, US5#2, SC-007 |
| #9 time-helper #4 round-trip | `tests/session/fix_time_roundtrip_test.cpp` | FR-012, US5#3, SC-007/010 |
| #10 durable-before-transmit + alloc | `tests/session/durable_before_transmit_test.cpp`, `tests/session/alloc_discipline_test.cpp`, `tools/check_alloc.py` | FR-010/011, US2#5, SC-009 |
| #11 cancellation two-phase | `tests/session/cancellation_two_phase_test.cpp` | FR-014, edge cases, SC-005/009 |
| #12 alloc discipline (mallocnesia) | `tests/session/alloc_discipline_test.cpp` | FR-015, SC-009 |
| #13 `seqnum_t` handoff (build/consumer) | `tests/session/seqnum_t_handoff_test.cpp` | FR-009, SC-010 |
| (conformance) Q2 in-scope `[FIX-TC]` subset (FIX.4.2/4.4; TC-005=scn7, TC-010=scn14 per catalogue) | `tests/session/conformance/tc_*.cpp` (6 files) ← QuickFIX/J `.def` oracle | FR-018, SC-008 (D-10) |

## Complexity Tracking

| Violation | Why needed | Simpler alternative rejected because |
|---|---|---|
| **`[const §VII.5]` NOT SATISFIED — Article XVII §1 recorded Gate-A-blocker waiver** (not an article-level exemption): this PR ships only the in-scope `[FIX-TC]` subset green (establishment / liveness / logout / reject / in-seq + too-low / SendingTime-CompID-version, FIX.4.2/4.4); recovery-dependent + too-high + FIXT/5.0SP2 + 4.0/4.1/4.3/5.0 cases are deferred-with-traceability in `coverage-index.md` (the **005 scope-deferral ledger**) pointing at the named later session-recovery feature. The unmet `[const §VII.5]` requirement is the Gate-A blocker; per `[const §XVII.1]` (`constitution.md:255`) it is **explicitly waived with this recorded rationale before `/tasks`**. Article VII §5 itself is *not* waived. | The full TC corpus requires ResendRequest/SequenceReset/gap-fill/PossDup/store-recovery and FIXT/5.0SP2 establishment — all explicitly out of scope (FR-017) and not green-able in this slice against the bound oracle. `[const §VII.5]` mandates the full corpus every PR with no scoped-subset allowance, so the honest representation is `[const §XVII.1]`'s recorded Gate-A-blocker waiver, not a green gate and not a claimed article exemption. | (a) Article XX amendment to §VII.5 permitting capability-partitioned subsets — a policy decision not made in this bundle; deferred to a future amendment if the project wants it. (b) Pulling full recovery in — contradicts FR-017 and the deferred-feature boundary. (c) Greening §VII.5 anyway (the bundle's prior state, via a fabricated `[const §V.5]` allowance) — dishonest; the fabricated citation is removed. (d) Labelling the Article VII §5 *article* "WAIVED" in the Constitution Check — over-broad; no article grants a scoped exemption from §VII.5, so the device is correctly attributed to the `[const §XVII.1]` Gate-A-blocker waiver instead. |

No new pluggable interface (the `[const §XIV.2]` ≤5 cap is satisfied upstream at `[2e §4.1]`/`[2d §4.1]`); the 6-state FSM with no `RecoveryPending` is the `[FIX-SL §4.10]` state set, not an amendment; the `seqnum_t` promotion is the planned `[2e §10 Q9]` single-line handoff (`[const §VI.5]`); no `[const §XX]` amendment required by this bundle.

## Gate A

`gate_a_required: yes` — `[const §XVII.1]` / Appendix A: **Session FSM** (state additions, recovery semantics) **+ Threading / concurrency** (per-session strand, async-mutex, two-phase cancellation) **+ Error semantics** (new `session_*` `core::error` variants). This is the **first FSM design review of record** — there is no Phase-2 FSM design doc by design (`[2e §3.1]` defers the FSM to this Phase-4 spec), so the Gate A bundle reviews the FSM design itself, not a pre-converged sibling doc. Inherited cross-doc seams (`[2e §4.1]` store contract, `[2d §4.1]`/`[2d §4.5]`/`[2d §6.5]` clock/config/cancellation, `[2f §4.1]` async_mutex) are *converged upstream* (2e v0.4 / 2d v0.4 / 2f v1.5 all Gate-A-converged) — consumed, not re-litigated. `/gate-a 005-session-establishment-fsm` runs **before `/tasks`**; both Codex passes (rescue + `/codex:adversarial-review`) per `feedback_gate_a_codex_dual_pass`; Opus triages, Opus rewrites.

### Round 1 — applied 2026-05-18

- Round 1 applied 2026-05-18: Codex defect P1=2 P2=4 P3=0; Codex approach P1=2 P2=3 P3=0; Opus post-judging P1=2 P2=2 P3=1; rewrite addresses root causes #1–#5. Reviews: research/reviews/codex_005-session-establishment-fsm_gate_a_review.md, research/reviews/codex_005-session-establishment-fsm_gate_a_adversarial_review.md, research/reviews/opus_005-session-establishment-fsm_gate_a_adversarial_review.md.

**Root causes resolved in-bundle (Opus adversarial review, source of truth):**

- **RC#1** (fabricated `[const §V.5]`/§VII.5 allowance — F-01, A-P2-2, O-4): resolved via Opus fix path (b). The constitution is unchanged (no Article XX amendment); `[const §VII.5]` is restated as a **recorded scoped WAIVER** (Constitution Check row GREEN→WAIVED; Complexity Tracking now non-empty with the waiver entry + rejected alternatives). All `[const §V.5]` conformance-allowance citations removed across spec/plan/research. O-4 error-slot overclaim fixed: the slot table is now explicitly a *planned, not-yet-published* allocation pinned at Gate A / `/speckit-tasks`.
- **RC#2** (RecoveryPending seam — F-02, A-P1-1, A-P1-2, O-1, O-2, O-3, entry-state contradiction): resolved via the **smaller honest slice** (Opus option i). New `### Session 2026-05-18 (Gate A round 1)` clarification block in spec.md re-asks Q1 with the corrected OSS survey (verified against the QFJ `.def` oracle + C++/fix8 — all emit `ResendRequest(35=2)` on the gap). `RecoveryPending` removed; FSM is the 6-state `[FIX-SL §4.10]` set; too-high gap = session-fatal (Logout-with-text→disconnect, `session_seqnum_gap_unrecoverable`); the inverted `[FIX-SL §4.8.2]` cite is removed; `1a_/2b_` too-high oracle cases moved to deferred-with-traceability. Does not contradict `[2e §3.1]` (which forward-references the *recovery* FSM to the Phase-4 line = the deferred session-recovery feature, not this establishment slice). Propagated through spec/plan/research/data-model/contracts.
- **RC#3** (version/FIXT overclaim — F-05, A-P2-3): resolved. SC-001 now carries an explicit **version-coverage ledger** (oracle has `fix40..fix50,fixLatest,future`, no `fixt11`/`fix50sp2`; 005 validates FIX.4.2/4.4 only; FIXT.1.1/5.0SP2 establishment explicitly NOT claimed; 4.0/4.1/4.3/5.0 deferred-with-traceability). FR-017 adds the FIXT logon-time (`DefaultApplVerID`) deferral; catalogue-ownership claims narrowed.
- **RC#4** (bundle-completeness — F-03, F-04, F-06): resolved. `contracts/heartbeat.hpp` added (the declared public header had no shape oracle). TC-005/TC-010 re-mapped to the catalogue (TC-005=scenario 7 Receive Reject; TC-010=scenario 14 Message validation — scenario-14 corpus is exactly `14a`–`14j`, 005 ships `14a`–`14g`, `14h`/`14i`/`14j` deferred per D-10 [enumeration precision corrected Gate A round 2]; 2q/2r MsgType under TC-002). S-016 narrowed honestly: 005 owns the 49/56 point-to-point portion only; `OnBehalfOfCompID(115)`/`DeliverToCompID(128)` recorded deferred-with-traceability (FR-004/FR-017).
- **RC#5** (FSM depth — A-P2-1, O-3): resolved to design-doc depth. The data-model transition matrix now enumerates the full FR-001 event alphabet (inbound Heartbeat/Reject/ResendRequest/SequenceReset/dup-Logon/simultaneous-logon/invalid-MsgType/timer), per-cell actions, and explicit guard precedence; the contradictory `RecoveryPending`-entry rows are gone (RC#2). No livelock sink remains (too-high is a terminal fatal transition, not a held-forever state) — O-3's I-1/seam-#1 unverifiability is removed.

- **O-5 [P3] preserved, not regressed:** the `[2e §10 Q9]` `seqnum_t` in-place promotion (D-1 / `contracts/seqnum.hpp`) and the D-8 threshold picks are the contractually-correct discharge of the 2e/2d handoffs and were **not** altered — only the too-high disposition comment in `seqnum.hpp` was corrected.

#### Round 1 — disagreements

Codex findings the Opus adversarial review (source of truth) marked **Disagree** — Codex's proposed fix was NOT applied; Opus's reasoning recorded:

- **Codex defect-pass F-02 counter-proposal** ("widen FR-001 / D-2 / contracts so `RecoveryPending` can be entered from logon states `LogonSent`/`LogonReceived`"): **NOT applied.** Opus reason: the dispute is not "which entry-state invariant to write down" but "can a hold-and-emit-nothing state exist at all for the cases kept in scope" — adopting the widen would entrench the deeper structural defect. The correct resolution is the slice-boundary redesign (RC#2 smaller-honest-slice), not a wording widen. Opus confirmed the F-02 *contradiction* at P1 but rejected the defect pass's "fixable contradiction" framing in favour of the approach pass's "slice boundary is incoherent" framing.

All other Codex findings were Confirmed (often escalated) by Opus and are addressed above; no other Disagree verdicts were issued.

### Round 1 — converged? (Opus closing recommendation was "structural / re-run Spec-Kit")

The Opus review's closing recommendation was that the bundle does not converge at Gate A and a fresh `/speckit-clarify`+`/speckit-plan` is the clean path, because two materially different futures (shrink vs. pull-recovery-in) and a policy choice (amend vs. waive) had to be decided. Per the Gate A loop's sanctioned in-bundle move, this rewrite **made those decisions in-bundle and recorded them as a new clarification + a recorded waiver** (the smaller honest slice + the §VII.5 waiver with no amendment). All five root causes are reconciled in-bundle without contradicting any signed-off anchor or the deferral — see `### Round 1 — unresolved structural` below for the honest residual. Round-2 review should re-verify the new Session-2026-05-18 clarification and the §VII.5 waiver framing.

### Round 1 — unresolved structural

None of the five root causes required escalation to a fresh `/speckit-clarify`+`/speckit-plan`: each was reconcilable in-bundle without contradicting a signed-off anchor or the deferral, because the decisive grounding (`[2e §3.1]`/`[2e §4 last bullet]`) forward-references the *recovery* FSM to the Phase-4 session line — i.e. to the **deferred successor session-recovery feature**, not to this establishment slice — so dropping the recovery-claiming half-state and deferring the too-high oracle cases is consistent with the anchors rather than in tension with them. Two items are flagged as **user-decision-shaped, recorded-not-fabricated**, so the round-2 review and any Step-F escalation are well-founded:

1. **Slice-boundary fork (RC#2).** This rewrite took Opus option (i) — the smaller honest slice (drop `RecoveryPending`; too-high = session-fatal) — per the instruction to prefer the smaller honest slice when the review leaves the choice open. The review explicitly framed this as a *user* choice between (i) shrink and (ii) pull minimal `ResendRequest` emission in. Option (ii) remains a legitimate alternative the user may prefer (it would keep the too-high `.def` cases in scope by emitting the real `ResendRequest(35=2)` and deferring only inbound gap-fill). If the user prefers (ii), a fresh `/speckit-clarify` Q1 + `/speckit-plan` is required — recorded here so that decision is well-founded, not silently foreclosed.
2. **Conformance-subset policy (RC#1).** This rewrite took Opus path (b) — recorded waiver, constitution unchanged. Path (a) — an Article XX amendment to `[const §VII.5]` explicitly permitting a capability-partitioned conformance subset — is a project-level policy decision that is **out of scope for an in-bundle convergence rewrite** and is deliberately *not* taken here. If the project wants §VII.5 satisfiable-by-subset rather than perpetually waived for pre-recovery session features, that amendment (Codex Gate A on the amendment + user sign-off) is the path; recorded here as the explicit, non-fabricated alternative.

### Round 2 — applied 2026-05-18

- Round 2 applied 2026-05-18: Codex defect P1=1 P2=3 P3=0; Codex approach P1=1 P2=2 P3=0; Opus post-judging P1=0 P2=4 P3=2 (both Codex P1s overruled→P2); rewrite addresses RC#1-residual/RC#2-residual/RC#4-residual/RC#6 + P3 sweep. Reviews: research/reviews/codex_005-session-establishment-fsm_gate_a_2_review.md, research/reviews/codex_005-session-establishment-fsm_gate_a_2_adversarial_review.md, research/reviews/opus_005-session-establishment-fsm_gate_a_2_adversarial_review.md.

**Round-2 residual root causes resolved (Opus adversarial review round 2, source of truth):**

- **RC#1-residual** (§VII.5 row over-labelled as an article-level "WAIVED"): the Constitution-Check row + Gates line + Complexity-Tracking entry relabelled — `[const §VII.5]` is **NOT SATISFIED** for the deferred TC cases and the feature proceeds under an explicit **Article XVII §1 recorded Gate-A-blocker waiver** (`constitution.md:255`); the article itself is not waived, no Article XX invoked.
- **RC#2-residual** (pre-shrink prose surfaces): `plan.md` `tc_seqnum_test.cpp` conformance-file comment corrected — too-high is explicitly 005's own fatal disposition (seam #4) with **no in-scope QFJ oracle case** (`1a_/2b_` deferred per D-10); `library/CLAUDE.md` rewritten from the superseded 7-state/`RecoveryPending` design to the 6-state post-shrink design (N-1).
- **RC#4-residual** (scenario-14 enumeration): `research.md` D-10 corrected — scenario-14 corpus is exactly `14a`–`14j` (verified `ls fix42/ fix44/ fixLatest/`, no `14k`–`14o`); `14a`–`14g` in scope, `14h`/`14i`/`14j` (repeating-group/repeated-tag dictionary-validation) deferred-with-traceability.
- **RC#6** (asserted-but-absent ledger + missing Normative References): the **005 scope-deferral ledger** is now actually recorded in `spec/coverage-index.md` (dedicated section + per-cell `005-defer` notes on FIX-TC scenarios 8/9/10/11/19, scenario-14, and `[FIX-SL §6.2]`; conclusion line updated), so the bundle's "recorded in `coverage-index.md`" assertions are now true; a `## Normative References` section was added to `spec.md` in the `[const §VI.5]` `[DocAbbrev §X.Y.Z] Title` grammar (plan.md §VI.5 row corrected to match).
- **P3 sweep:** N-2 (`data-model.md` guard-precedence `(E.. Q3)` placeholder) resolved to the explicit Q3 SendingTime/MaxLatency disposition; N-3 (D-1 `seqnum_t` in-place promotion + D-8 threshold picks) confirmed correct and **not regressed** (no edit beyond the already-corrected too-high comment).

#### Round 2 — disagreements

The Opus adversarial review round 2 (source of truth) **overruled both round-2 Codex P1s to P2** — Codex's structural counter-proposals were NOT applied; Opus's reasoning recorded:

- **Codex defect-pass round-2 finding 1 [P1] & approach-pass round-2 finding 1 [P1]** ("the smaller-honest-slice still leaks too-high into the green corpus; round-1 structural NOT closed; treat `plan.md:149` as the only file-level conformance partition"): **Disagreed → downgraded to P2 (RC#2-residual).** Opus reason: the only *oracle-binding* partition is `research.md:123` (D-10), which already defers `1a_/2b_` with the correct ResendRequest rationale; conformance tests are authored only from the in-scope `.def` subset. `plan.md`'s `tc_seqnum_test.cpp` "too-high" phrasing carries **no `.def`** — it denotes 005's own fatal disposition (seam #4), a prose/ownership smear, not a re-imported ungreenable oracle case. The structural slice-boundary defect is closed; the residue is an editorial prose fix (applied), not a re-`/clarify`/re-`/plan`. Codex's "widen the green corpus / treat 149 as the partition" framing was not adopted.
- **Codex approach-pass round-2 finding 3 [P2]** ("round-1's structural re-scope is parked, not discharged — structural"): **Disagreed on the "structural / parked" characterization** (confirmed only the P2 residual). Opus reason: the shrink decision is operationalized (`RecoveryPending` removed from enum/matrix/contracts, inverted `[FIX-SL §4.8.2]` cite removed, seam #4 added, D-10 oracle partition clean); the two user-decision-shaped forks at `plan.md` Round-1-unresolved are honest recorded disclosures, not defects. No structural re-run; only the prose residuals (RC#2-residual, applied above).

The §VII.5-over-broad finding (approach-pass round-2 finding 2) and all coverage-index / Normative-References / scenario-14 confirms were **adopted** by the Opus judge and are applied above (RC#1-residual / RC#6 / RC#4-residual) — not disagreements.

## Gate B

### Round 1 — TBD (post-`/implement`, pre-merge; `/speckit-verify` GREEN/YELLOW precondition per `[const §XVII.8]`)

## Citation verification pass (round 0 — run at /plan authoring, 2026-05-17)

All `[const §...]`, `[2e §...]`, `[2d §...]`, `[2f §...]`, `[FIX-SL §...]`, `[FIX-TC]`, `[arch §...]` citations in this plan resolve against: `.specify/constitution.md` (v0.1), `.specify/2e-msgstore.md` (v0.4), `.specify/2d-threading.md` (v0.4), `.specify/2f-async-mutex.md` (v1.5), `spec/coverage-index.md` (FIX-SL/FIX-TC section structure), `spec/feature-catalogue.md` (S-001..S-025), `include/fixpp/core/error.hpp` (occupied slots 1,10–13,20–29,30–42 → next free = 43).

Verified resolutions (sampled): `[2e §4.1]` (4 pure-virtual `store`/`retrieve`/`next_seqnum`/`reset` + `retrieve_visitor`, `include/fixpp/session/message_store.hpp`); `[2e §4.7]` (`using seqnum_t = std::uint32_t` placeholder, owner = Phase-4 session spec, `include/fixpp/session/seqnum.hpp`); `[2e §6.7]` (`store_seqnum_overflow` session-fatal); `[2e §3.1]`/`[2e §10 Q9]`/`[2e §7.6]` (FSM owned by Phase-4 spec; `seqnum_t` handoff; consumer callsite ordering); `[2d §4.1]` (`fixpp::core::Clock` 4-pv `now`/`steady_now`/`sleep_until`/`cancel_sleeps`, `include/fixpp/core/clock.hpp`); `[2d §4.5]` (`SessionConfig` — `sender_comp_id`/`target_comp_id`/`begin_string` + `heartbeat_interval`/`test_request_threshold`/`sending_time_threshold` values owned by session-module spec; `RejectPolicy reject_policy`); `[2d §7.9]` (`effective_clock = clock_override ?: EngineConfig::clock`, resolved once at open); `[2d §6.5]` (`cancellable_dispatch`, two-phase close, `session_already_closed`/`invalid_session_config`/`executor_not_serialised`); `[2f §7.3]`/`[2f §7.6]` (async_mutex for seqnum counter, graceful-close `cancel_and_drain`); `[FIX-SL §4.10]` state matrix, `§4.1` sequence numbers, `§4.2.1/2` BeginString/CompID, `§4.3.4` HeartBtInt, `§4.5.4` Reject, `§4.6` logout, `§4.2.3` SendingTime; `[FIX-TC]` TC-001..TC-017 (coverage-index lines 365–386 — **TC-005 = `[FIX-TC §4 scenario 7]` Receive Reject; TC-010 = `[FIX-TC §4 scenario 14]` Message validation; TC-002 = standard-header validation incl. 2q/2r MsgType** — corrected Gate A round 1, F-04); QuickFIX/J acceptance defs `reference-engines/quickfixj/.../definitions/server/` @ QFJ_RELEASE_3_0_1 — verified server dirs `fix40, fix41, fix42, fix43, fix44, fix50, fixLatest, future` (**no `fixt11`/`fix50sp2` dir**; 005 validates against `fix42`/`fix44` only — F-05/version-coverage ledger). **Gate A round 1 corrections to round-0:** the round-0 pass falsely asserted "No remaining vague or dead refs" — `[const §V.5]` was a **fabricated** conformance allowance (it is the README-disclaimer rule; `[const §VII.5]` mandates the full corpus, now WAIVED-scoped, F-01); `[FIX-SL §4.8.2]` (the ResendRequest section, owned by deferred S-005/S-024) was **inverted-cited** as authority for a no-ResendRequest state (O-1) and is **removed** from the bundle; `[FIX-SL §4.5.3]` (gap→ResendRequest, deferred S-005/S-014) was mis-cited for the too-low fatal disposition and is corrected to `[FIX-SL §4.1]`. All other sampled `[const §...]`/`[2e §...]`/`[2d §...]`/`[2f §...]`/`[FIX-SL §...]`/`[arch §...]` resolve under canonical form; the round-1-touched cites were re-verified against the cited sources.

## Phase-2 input checklist (for `/tasks`)

- [x] Spec FR-001..018 + SC-001..010 ↔ `[FIX-SL §4]` / `[2e §4.1]` / `[2d §4.1/§4.5/§6.5]` / `[2f §7.3]` mapped (data-model + this plan)
- [x] All 14 test seams + Q2 conformance subset bound to explicitly named files (Test-seam mapping; no globs)
- [x] Clarification Q1 fixed (re-scoped Session-2026-05-18): too-high = session-fatal, **no `RecoveryPending` state** — the 2026-05-17 OSS survey was verified inverted; recovery deferred to the named session-recovery feature (research D-2)
- [x] Clarification Q2 fixed: concrete TC-### in/deferred split (incl. too-high deferred) mapped onto the QuickFIX/J `.def` oracle; **`[const §VII.5]` NOT SATISFIED for the deferred cases — explicit Article XVII §1 recorded Gate-A-blocker waiver, not greened, article itself not waived** (research D-10)
- [x] Clarification Q3 fixed: stale-SendingTime → `Reject(10)`→`Logout`→disconnect (Logon→logout-with-error) (research D-3 / contracts/sending_time.hpp)
- [x] `seqnum_t` width + `[2e §10 Q9]` handoff mechanism fixed (research D-1: `uint32_t`, single-line in-place promotion)
- [x] Time-helper #4 home + grammar fixed (research D-3: `core/fix_time.hpp`, `YYYYMMDD-HH:MM:SS[.sss[sss]]`); closes `core/` exit
- [x] Threshold defaults 005 owns fixed (research D-8: heartbeat 30 s, test-request 1×HeartBtInt, MaxLatency 120 s, `strict_reject_then_logout`)
- [x] Error slots = *planned* allocation 43..53 + 2d-coordinated 54..N (research/data-model D-9: `session_*`, `FIXPP_ERR_SESSION_*`); exact numbers + 2d-reuse ownership **decided at Gate A / pinned at `/speckit-tasks`**, non-renumbering only once published (not yet)
- [x] Constitution Check: `[const §VII.5]` NOT SATISFIED for the deferred TC cases — feature proceeds under an explicit Article XVII §1 recorded Gate-A-blocker waiver (rationale in Complexity Tracking — non-empty; article itself not waived); no other violation
- [ ] Gate A converged (runs before `/tasks`)
- [ ] `/analyze` drift check (post-Gate-A, pre-`/tasks`)
