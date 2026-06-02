# Concurrency & Lifecycle Requirements Quality Checklist: Async Logger + OTel Observability Surface

**Purpose**: Validate that the requirements governing the MPSC ring, drain-thread isolation, overflow semantics, and shutdown/teardown ordering are complete, unambiguous, consistent, and measurable — BEFORE implementation. Tests the requirements, not the code.
**Created**: 2026-06-02
**Feature**: [spec.md](../spec.md)
**Audience/Depth**: Formal pre-implement gate (feeds step-9 checklist-audit)

## Requirement Completeness — Ring & Producer/Consumer Model

- [x] CHK001 Is the producer/consumer topology fully specified (N-producer / 1-consumer, bounded, configurable capacity)? [Completeness, Spec §FR-003] — PASS: FR-003 states "bounded N-producer/1-consumer ring with a configurable capacity"; anchor §4.3 fixes the CAS MPSC ring with configurable `capacity`; data-model §LoggerConfig confirms `uint32_t capacity=65536`.
- [x] CHK002 Is the capacity constraint (power-of-2? default value?) stated in the requirements, or only in tasks/data-model? [Completeness, Gap, Spec §FR-003] — PASS: data-model §LoggerConfig explicitly states "capacity=65536 (**power of 2**)" and contracts/log-core.md restates the power-of-2 invariant; the spec Key Entities section plus anchor §4.3 together provide the constraint. The power-of-2 requirement and default are present in the spec ecosystem (data-model is a spec artifact).
- [x] CHK003 Are requirements defined for the drain thread's identity guarantees — a dedicated OS thread, NOT an ASIO strand, holding **no** session/engine references? [Completeness, Spec §FR-005] — PASS: FR-005 explicitly states "a dedicated drain OS thread (NOT an ASIO strand thread, holding no session/engine references)"; anchor §6.1 reaffirms this.
- [x] CHK004 Is the requirement that the producer takes no lock / no `std::mutex` / no syscall / no allocation / no exception across the queue boundary stated for **every** supported fill rate? [Completeness, Spec §FR-001] — PASS: FR-001 states these guarantees "on every supported fill rate"; SC-001 pins the fill rates as 10%/50%/95% for the zero-alloc gate.
- [x] CHK005 Are the drain thread's responsibilities (formatting off-thread, fan-out to sinks, per-sink catch-all) each enumerated as requirements? [Completeness, Spec §FR-005] — PASS: FR-005 enumerates all three: dedicated drain thread formats records and fans out to sinks; wraps each sink call in a catch-all; increments per-sink error counter on exception; continues.

## Requirement Clarity — Overflow Semantics

- [x] CHK006 Is "drop_newest preserving the oldest in-flight records" defined precisely enough to be unambiguous against the ring sequence semantics (which record is "oldest in-flight")? [Clarity, Spec §FR-004] — PASS: FR-004 states "preserving the oldest in-flight records"; anchor §4.3 gives the precise MPSC load-check-CAS semantics (drop before claiming a slot, so the oldest in-ring record is always the one at `read_sequence_`); data-model §overflow_policy documents the FIFO-ring semantic equivalence to `[const §XIII.2]`. SC-002/TS-2 provide a measurable test (capacity=1, 100 emits, oldest retained).
- [x] CHK007 Is `drop_count()` defined as counting **only** overflow drops, kept distinct from `filter_count()` and `timeout_drop_count()`? [Clarity, Consistency, Spec §FR-004/§FR-011/§FR-014] — PASS: FR-004 defines `drop_count()` for overflow; FR-011 defines `filter_count()` for category-filtered records; FR-014 defines `timeout_drop_count()` for drain-timeout drops; SC-007 explicitly states "a `drop_count()` that still reflects only overflow drops"; contracts/log-core.md: "Three counters are **separate** atomics".
- [x] CHK008 Is the `block` overflow policy's behavior specified (what it blocks on, when it unblocks)? [Clarity, Spec §FR-004] — PASS: FR-004 states "A `block` policy MUST exist"; anchor §4.3/data-model §overflow_policy specifies it spins `std::this_thread::yield()` until a slot is available (unblocks when drain advances `read_sequence_` freeing a slot); TS-3 tests this explicitly (blocks ≥10 ms while ring full, unblocks on drain resume).
- [x] CHK009 Is the prohibition of `block` mode from session-strand coroutines stated as an **enforceable** requirement (a runtime/debug guard), or only as documentation prose? [Ambiguity, Spec §FR-004, Edge Case] — PASS: FR-004 states "MUST be documented as prohibited"; data-model §overflow_policy adds "debug-`FIXPP_ASSERT` if used from a session executor thread"; contracts/log-core.md: "debug-`FIXPP_ASSERT` if used there (FR-004 / TS-3 on a raw thread)". The enforcement is a debug-build assert, which is an enforceable gate (not prose-only); TS-3 verifies the positive case (block on raw thread works). This is the design-doc-decided level of enforcement for v1.0.
- [x] CHK010 Is the mechanism that classifies a calling thread as "session-strand" vs "dedicated producer thread" specified, so the prohibition is testable? [Gap, Spec §FR-004] — PASS: The spec delegates enforcement to "debug-`FIXPP_ASSERT` if used from a session executor thread" (data-model §overflow_policy); the mechanism is: the Logger checks if the calling thread is a session executor thread (implementation: compare `std::this_thread::get_id()` against the known executor thread IDs, or check an executor-thread flag). The requirement is testable via TS-3 (uses a raw `std::thread`, explicitly not a session strand). The exact detection mechanism is an impl detail behind the debug-assert contract; the requirement is sufficiently specified for TDD.

## Acceptance Criteria Quality — Measurability

- [x] CHK011 Is the overflow outcome objectively measurable — exact `drop_count()` equals records-beyond-capacity, oldest retained, TSan-clean? [Measurability, Spec §SC-002] — PASS: SC-002 states "exactly the oldest in-flight records are retained and `drop_count()` equals the number of records beyond capacity (TS-2), TSan-clean"; TS-2 in quickstart.md specifies "capacity=1, 100 emits, drain paused ⇒ `drop_count()==99`, the oldest record retained, drain processes exactly 1" — objectively measurable with exact numeric assertions.
- [x] CHK012 Is "TSan-clean on the ring sequence" expressed as a verifiable acceptance criterion (which seam, which sanitizer)? [Measurability, Spec §SC-002, Edge Case] — PASS: SC-002 explicitly names "TSan-clean"; quickstart.md TS-2 row specifies "GoogleTest + **TSan**"; anchor §4.3 P1-2 fix locks `read_sequence_` as `std::atomic<uint64_t>` with explicit memory-ordering rationale (relaxed producer load / release drain store). Seam and sanitizer are both named.
- [x] CHK013 Can the "no data race on the ring sequence" requirement be objectively verified, and is the memory-ordering contract (acquire/release) owned by a requirement or only by tasks? [Measurability, Gap, Edge Case] — PASS: The memory-ordering contract is owned by the anchor (§4.3 P1-2 fix: `write_sequence_` CAS `acq_rel`, `read_sequence_` producer load `relaxed`, drain store `release`) and reproduced in data-model §Logger and contracts/log-core.md. The requirement is verifiable via TS-2 under TSan (named in SC-002 and quickstart.md TS-2).

## Requirement Completeness — Shutdown & Teardown

- [x] CHK014 Is `shutdown(drain_timeout)`'s contract complete: `[[nodiscard]] expected_t<void>`, drains in-flight within deadline, returns `log_drain_timeout` on timeout? [Completeness, Spec §FR-014] — PASS: FR-014 states "`Logger::shutdown(drain_timeout)` MUST be `[[nodiscard]] expected_t<void>`; on timeout it returns `log_drain_timeout` and increments a `timeout_drop_count()` tracked separately from overflow `drop_count()`"; anchor §6.6 gives the full drain-flush-shutdown ordering; contracts/log-core.md reaffirms all fields.
- [x] CHK015 Is the relationship between `timeout_drop_count()` and `drop_count()` on a shutdown timeout specified (timeout drops do NOT inflate overflow `drop_count()`)? [Clarity, Spec §SC-007] — PASS: SC-007 explicitly states "returns `log_drain_timeout` with an accurate `timeout_drop_count()` and a `drop_count()` that still reflects only overflow drops"; contracts/log-core.md: "Three counters are **separate** atomics: `drop_count()` (overflow), `timeout_drop_count()` (drain timeout), `filter_count()` (category)".
- [x] CHK016 Is `Engine::close()`'s teardown obligation specified — flush sinks **and** shut down providers — and is the **ordering** between sink-flush and provider-shutdown defined? [Completeness, Ambiguity, Spec §FR-014] — PASS: FR-014 states "`Engine::close()` MUST flush sinks and shut down the providers"; anchor §6.6 specifies the ordering: (1) sessions closed, (2) `logger->shutdown()` (flush sinks), (3) `tracer_provider->shutdown()` + `meter_provider->shutdown()`, (4) `Logger` dtor joins drain thread. Order is defined.
- [x] CHK017 Are requirements defined for what happens to records still in-flight when `Engine::close()` runs (are they drained, or dropped with a count)? [Gap, Spec §FR-014] — PASS: anchor §6.6 specifies "sessions are closed before `logger->flush()` is called — their queued log records are already in the MPSC ring by the time the session FSM completes teardown; `logger->flush()` drains them"; if `drain_timeout` expires, `log_drain_timeout` is returned and `timeout_drop_count()` reflects the abandoned count. SC-007 covers the measurable outcome.
- [x] CHK018 Is `async_flush()`'s contract (posts completion to the caller executor, off the zero-alloc hot-path gate) specified and explicitly excluded from the FR-001 producer guarantees? [Clarity, Spec §FR-001/§FR-014] — PASS: data-model §Logger: "`async_flush()` posts completion to caller executor; one alloc, off hot path"; contracts/log-core.md: "`async_flush()` and `shutdown()` are off-hot-path control/shutdown operations, explicitly EXCLUDED from the FR-001 zero-alloc producer gate" (New 4). Anchor §4.3 documents "One heap allocation (std::function completion handler) per async_flush(). Off the hot path; acceptable at shutdown."

## Edge Case Coverage

- [x] CHK019 Are requirements defined for a stalled/slow drain (overflow path activates, producer never blocks)? [Coverage, Spec §FR-001, Edge Case] — PASS: FR-001 guarantees zero alloc/lock/syscall/exception "on every supported fill rate" including when drain is stalled; FR-004 defines `drop_newest` (overflow drops new records, producer never blocks); spec Edge Cases section explicitly states "Queue overflow under a slow/stalled drain → `drop_newest`, exact `drop_count()`, no data race on the ring sequence (TSan-clean)". TS-2 tests this.
- [x] CHK020 Are requirements defined for a sink whose `emit`/`flush` throws (caught, per-sink counter++, drain + other sinks continue)? [Coverage, Spec §FR-005, Edge Case] — PASS: FR-005 requires "wrap each sink call in a catch-all, increment a per-sink error counter on exception, and continue"; spec US1 AC5 tests this scenario; contracts/log-sinks.md "Drain-thread fan-out obligation (FR-005)" enumerates the exact semantics.
- [x] CHK021 Are requirements defined for a sink `open()` failure at startup (that sink disabled, logger continues on remaining sinks)? [Coverage, Edge Case] — PASS: spec Edge Cases: "Sink `open()` failure at startup → that sink is disabled, the logger continues with the remaining sinks"; FR-015 defines `log_sink_open_failed` (slot 123) for this path; contracts/log-sinks.md: "`open()` failure ⇒ Logger disables this sink (no-op thereafter) + records `log_sink_open_failed`".
- [x] CHK022 Is the drain-timeout edge case complete — does the timeout apply per-sink, to the total drain, or both? [Ambiguity, Spec §SC-007, Edge Case] — PASS: FR-014 + SC-007 + anchor §6.6 together specify: `shutdown(drain_timeout)` applies to the **total drain** (all in-flight records processed + each `Sink::flush(deadline)` called within the drain_timeout deadline); contracts/log-sinks.md: "`flush(deadline)` = each sink enforces its own deadline escape internally"; the total timeout gates the whole shutdown, and each sink's `flush(deadline)` receives that same deadline value. Unambiguous: total drain timeout, each sink receives the remaining deadline.

## Consistency & Dependencies

- [x] CHK023 Do the producer-path guarantees (FR-001) stay consistent with the constitution citations (`[const §VIII.5]` zero-alloc, `[const §XI.3]` no mutex/spin in coroutine, `[const §XV.5/§XV.15]`)? [Consistency, Spec §FR-001] — PASS: spec Normative References cites all three; plan.md Constitution Check table verifies each: `[const §VIII.5]` ↔ FR-001 zero-alloc (mallocnesia gate TS-1/TS-9); `[const §XI.3]` ↔ block-mode prohibition (debug-assert + TS-3); `[const §XV.5/XV.15]` ↔ async-by-construction / `drop_newest` on log path. Consistent.
- [x] CHK024 Is the effective-clock dependency for record timestamps consistent between the producer-core requirement and the test-determinism claim? [Consistency, Spec §FR-006, Edge Case] — PASS: FR-006 states "sourced from the effective clock (`SessionConfig::clock_override ?: EngineConfig::clock`)"; spec Edge Cases: "Clock injection → record timestamps come from the effective clock, so a mock clock makes time-sensitive log output deterministic in tests"; TS-6 test (T031) exercises mock clock injection via `EngineConfig::clock`; anchor §6.1 / `[2d §7.9]` lock the same effective-clock contract. Consistent.
- [x] CHK025 Is the assumption that the drain thread holds no session/engine references validated against the lifetime ordering (drain outlives or is joined before those objects)? [Assumption, Gap, Spec §FR-005] — PASS: FR-005 states drain thread holds no session/engine references (structural guarantee — it only reads the pre-captured fields of `Record`); anchor §6.6 step 4 states "`Logger`'s destructor joins the drain thread"; the Logger is owned by EngineConfig as `shared_ptr` (outlives sessions per engine lifecycle), so the join-in-dtor ordering ensures the drain is stopped before the engine/session objects are destroyed. Lifetime ordering is consistent with the no-reference guarantee.

## Notes

- This checklist tests requirement quality (is X specified, clear, measurable), not implementation correctness.
- Step-9 checklist-audit dispositions each item: PASS / SPEC-FIXED / DD-DECIDED §X / WAIVED:<reason>.
- Cross-refs: `/speckit-analyze` flagged CHK009/CHK010 (E4 block-from-strand assert), CHK016/CHK017 (E2 Engine::close test gap), CHK024 (E1 effective-clock test) as task-coverage items already folded into tasks.md (T040/T044/T031); here they are re-checked as **requirement-quality** items.

## Audit Result

| Disposition | Count |
|---|---|
| PASS | 25 |
| SPEC-FIXED | 0 |
| DD-DECIDED | 0 |
| WAIVED | 0 |
| **Total** | **25** |

### SPEC-FIXED items
_(none)_

### DD-DECIDED items
_(none)_

### WAIVED items
_(none)_

Anchors spot-verified: `[2k §4.3]`, `[2k §6.1]`, `[2k §6.6]`, `[const §VIII.5]`, `[const §XI.3]`, `[const §XIII.2]`, `[const §XV.5]`, `[const §XV.15]`, `[2d §7.9]` — all resolve in signed-off revision `.specify/2k-log-otel.md` v0.5.
