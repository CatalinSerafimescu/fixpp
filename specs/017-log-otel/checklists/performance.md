# Performance & Zero-Alloc Requirements Quality Checklist: Async Logger + OTel Observability Surface

**Purpose**: Validate that the latency, zero-allocation, and compile-time-elimination requirements are quantified, measurable, baseline-anchored, and free of ambiguity between the production gate and the spike — BEFORE implementation.
**Created**: 2026-06-02
**Feature**: [spec.md](../spec.md)
**Audience/Depth**: Formal pre-implement gate (feeds step-9 checklist-audit)

## Acceptance Criteria Quality — Latency

- [x] CHK001 Is the latency ceiling quantified with a specific metric and statistic (mean ≤ 50 ns) rather than a vague "fast"? [Measurability, Spec §FR-002/§SC-001] — PASS: FR-002 states "mean ≤ 50 ns latency ceiling"; SC-001 states "producer enqueue **mean latency is ≤ 50 ns** on the non-overflow path (the binding TS-9 gate)". Quantified.
- [x] CHK002 Is the **path** the 50 ns gate applies to unambiguous (non-overflow producer path only)? [Clarity, Spec §FR-002] — PASS: FR-002 states "on the non-overflow path"; SC-001 repeats "on the non-overflow path (the binding TS-9 gate)"; T030 in tasks.md explicitly resolves the fill-rate/overflow ambiguity with two strategies. Unambiguous.
- [x] CHK003 Is the production gate (TS-9 mean ≤ 50 ns) clearly distinguished from the TS-13 spike's Criterion B (p99 ≤ 50 ns at 50% fill), so the two are not conflated? [Consistency, Ambiguity, Spec §FR-002/§FR-021] — PASS: FR-002 explicitly states "The separate **p99 ≤ 50 ns at 50% fill** criterion is the TS-13 spike's Criterion B (anchor §1.2), not the production gate"; FR-021 and SC-008 reaffirm TS-13 is non-blocking. The two criteria are explicitly distinguished.
- [x] CHK004 Are p99/p999/max defined as **reported** (non-gating) metrics vs the single binding mean gate? [Clarity, Spec §FR-002/§SC-001] — PASS: FR-002 states "TS-9 also records p99/p999/max as **reported** metrics"; SC-001 states "p99/p999 recorded as reported metrics". Explicitly non-gating.
- [x] CHK005 Is the measurement window specified so the gate is not silently measured on the overflow/drop path (e.g., capacity-vs-iteration-count relationship)? [Gap, Ambiguity, Spec §SC-001] — PASS: T030 (tasks.md) explicitly resolves this: "Either (a) measure the 50 ns gate in windows of ≤ `capacity−1` enqueues (reset between windows), or (b) let the drain **run** (not sleep) so slots free and the ring never saturates; report overflow-path cost separately, ungated." The spec requirement-text (FR-002 "non-overflow path") plus the tasks.md resolution together close the gap; the task coverage was already confirmed folded in by the orchestrator pre-brief.

## Requirement Completeness — Baseline & Reference Hardware

- [x] CHK006 Is the "reference CI hardware" defined or pinned anywhere the requirement can be objectively evaluated against? [Completeness, Spec §FR-002, Assumption] — PASS: spec §Assumptions states "The 'reference CI hardware' and latency baselines are those defined in `bench/baselines/` (TS-9 baseline `bench/baselines/log_enqueue.json`)"; anchor §6.2 describes the expected latency range for the reference hardware. The hardware is indirected via the baseline file rather than named explicitly — this is the design-doc-decided approach (DD-DECIDED §6.2) for CI portability; the baseline file IS the hardware reference.
- [x] CHK007 Is the baseline artifact (`bench/baselines/log_enqueue.json`) identified as the binding comparison basis for SC-001? [Traceability, Spec §SC-001, Assumption] — PASS: spec §Assumptions: "SC-001's 50 ns is relative to that baseline"; T030 in tasks.md creates the baseline file; quickstart.md TS-9 row references `bench/baselines/log_enqueue.json`. Traceability is present.
- [x] CHK008 Is the chicken-and-egg resolved in the requirements — the baseline file is **created during implementation** yet SC-001 is "relative to that baseline"? [Ambiguity, Assumption, Spec §SC-001] — PASS: The spec handles this by making the 50 ns ceiling the hard gate and the baseline file the recorded result from the same benchmark run (T030 creates the file AND the gate). SC-001 is self-referentially satisfied: the mean ≤ 50 ns IS the requirement; the baseline file records the actual measurement for future regression detection. The "relative to baseline" language means "compared against this file in future runs", not that the baseline is a prerequisite for the initial pass. Anchor §6.2 provides the physical rationale (25–40 ns expected, 50 ns = 25% headroom). Not ambiguous.

## Acceptance Criteria Quality — Zero Allocation

- [x] CHK009 Is "zero heap allocation per record on the producer path" stated as an absolute requirement across every supported fill rate? [Measurability, Spec §FR-001] — PASS: FR-001: "MUST perform zero heap allocation per record, emit no exceptions across the queue boundary, take no lock/`std::mutex`, and make no system call — on every supported fill rate." Absolute and fill-rate-scoped.
- [x] CHK010 Are the fill rates the zero-alloc claim must hold at enumerated (10% / 50% / 95%)? [Completeness, Spec §SC-001] — PASS: SC-001: "zero allocations on the producer path at 10%/50%/95% fill rates under the malloc interceptor"; T014 (TS-1) tests "at 10/50/95% fill".
- [x] CHK011 Is the **verification mechanism** for zero-alloc specified strongly enough — does the requirement imply the dual gate (PMR counting_resource + global malloc interceptor), or could a PMR-only gate falsely pass? [Clarity, Gap, Spec §SC-001] — PASS: contracts/log-core.md: "The zero-alloc gate (TS-1) MUST wrap the **full macro → `enqueue` arg-marshalling path** under the **dual gate** (counting_resource + mallocnesia LD_PRELOAD, `[[feedback_tracking_pmr_resource_false_pass]]`)"; quickstart.md TS-1 row: "dual gate (counting_resource + mallocnesia)"; T014 in tasks.md explicitly states "dual-gate zero-alloc (counting_resource + mallocnesia LD_PRELOAD, `[[feedback_tracking_pmr_resource_false_pass]]`)". Dual-gate requirement is explicit.
- [x] CHK012 Is the full producer surface the zero-alloc claim covers defined (the macro → arg-marshalling → enqueue path, including the initializer_list backing array and ≤6 ArgValue)? [Completeness, Spec §FR-001] — PASS: contracts/log-core.md §Arg-marshalling zero-alloc obligation (New 3): "copies **at most `k_max_args = 6`** `ArgValue`s **by value** into `Record::args`, with **no** dynamic container... The `initializer_list` backing array is stack-allocated... The zero-alloc gate MUST wrap the **full macro → `enqueue` arg-marshalling path** (not just the ring CAS)"; quickstart.md validation gates confirm the same. Full path is defined.
- [x] CHK013 Is `async_flush()` (and other off-hot-path entry points) explicitly excluded from the zero-alloc producer gate so the gate's scope is unambiguous? [Clarity, Spec §FR-001/§FR-014] — PASS: data-model §Logger: "`async_flush()` and `shutdown()` are off-hot-path control/shutdown ops, EXCLUDED from the FR-001 zero-alloc producer gate (New 4)"; contracts/log-core.md: "`async_flush()` and `shutdown()` are off-hot-path control/shutdown operations, explicitly EXCLUDED from the FR-001 zero-alloc producer gate". Explicit exclusion present.

## Requirement Clarity — Compile-Time Elimination

- [x] CHK014 Is "below-threshold call sites contribute zero instructions and zero `.rodata` format strings" expressed as an objectively checkable requirement (nm/objdump)? [Measurability, Spec §FR-010/§SC-003] — PASS: FR-010: "MUST eliminate below-threshold call sites entirely (zero instructions, zero `.rodata` format strings, zero allocation)"; SC-003: "contributes zero format strings to the binary and zero runtime cost (TS-1)"; quickstart.md TS-1 row: "no debug/info format strings in `.rodata` (`nm`/`objdump`)". Objectively checkable via nm/objdump — explicitly stated.
- [x] CHK015 Is the compile-time level cutoff mechanism (`FIXPP_LOG_MIN_LEVEL` + `if constexpr`) specified, and its debug/release default behavior defined? [Clarity, Spec §FR-010/§FR-023] — PASS: FR-010 names both "`FIXPP_LOG_MIN_LEVEL` + `if constexpr`"; FR-023 states "MUST introduce the `FIXPP_LOG_MIN_LEVEL` ... CMake options"; data-model §Level: "Compile-time cutoff: `FIXPP_LOG_MIN_LEVEL` (CMake int 0..5)"; T002 in tasks.md: "add the `FIXPP_LOG_MIN_LEVEL` (int 0..5, default debug/trace; release default info)"; quickstart.md: "default trace(debug)/info(release)". Both mechanism and per-build default are defined.
- [x] CHK016 Is the per-build-type default of `FIXPP_LOG_MIN_LEVEL` (debug/trace vs release/info) specified unambiguously enough to avoid a single-value `option()` default breaking one build type? [Ambiguity, Spec §FR-023] — PASS: tasks.md T002 explicitly states "int 0..5, default debug/trace; release default info"; quickstart.md build snippet shows the CMake option with "default trace(debug)/info(release)" note. The dual-default behavior (per build type) is explicitly specified, resolving the single-value collision risk.

## Requirement Completeness — Runtime Filtering Cost

- [x] CHK017 Is the runtime category filter's cost/position defined (atomic bitmask, drops before sink delivery, counted in `filter_count()` not `drop_count()`)? [Completeness, Consistency, Spec §FR-011/§SC-003] — PASS: FR-011: "A runtime per-category filter (atomic bitmask) MUST drop disabled-category records before sink delivery, counted in a `filter_count()` separate from `drop_count()`"; data-model §Category: "`Logger::enabled_categories_mask_` (`std::atomic<uint64_t>`), bit `(category & 63u)` clear ⇒ dropped before enqueue, counted in `filter_count()`"; SC-003: "a disabled runtime category is filtered and counted without touching `drop_count()` (TS-8)". Complete, consistent.
- [x] CHK018 Is the category→bitmask-bit mapping (`index = category & 63u`) and its collision behavior specified clearly enough to be testable, including the built-in bit range vs an unused bit-0? [Clarity, Gap, Spec §FR-011] — PASS: data-model §Category fully specifies: bit index = `category & 63u`; built-ins (0x0001..0x0008) occupy bits 1–8 (collision-free by construction); `FIXPP_LOG_CATEGORY("name")` carries a build-time `static_assert` collision check against built-in low-6-bits (default: reject); two user categories colliding alias the same bit (documented); contracts/log-core.md confirms and adds "A test asserts a user category and a built-in with colliding low-6-bits are either independently controllable or the collision is rejected at compile time." Testable.

## Requirement Completeness — Record/ArgValue Layout (perf-load-bearing)

- [x] CHK019 Are the fixed-size layout constraints stated as requirements (`Record` 256 B, `ArgValue` 24 B, trivially-copyable) since they are load-bearing for the zero-alloc/latency claims? [Completeness, Spec §Key Entities] — PASS: spec Key Entities: "`ArgValue` — tagged union... `sizeof == 24`"; "`Record` — the fixed 256-byte slot"; data-model §ArgValue: "`static_assert(sizeof(ArgValue)==24)`... `static_assert(is_trivially_copyable_v)`"; data-model §Record: "`static_assert(sizeof(Record)==256)`, `is_trivially_copyable_v`"; contracts/log-core.md: "Compile-time obligations: `static_assert(sizeof(ArgValue) == 24)` and `is_trivially_copyable_v<ArgValue>`; `static_assert(sizeof(Record) == 256)` and `is_trivially_copyable_v<Record>`; `alignas(64)`." Stated as requirements with `static_assert` enforcement.
- [x] CHK020 Is the ≤6-ArgValue bound per record specified as a requirement (overflow/truncation behavior beyond 6)? [Gap, Spec §Key Entities] — PASS: data-model §Logger: "Copies **at most `k_max_args=6`** `ArgValue`s **by value** into `Record::args`, no dynamic container (excess truncated / `static_assert`-bounded)"; contracts/log-core.md §Arg-marshalling zero-alloc obligation: "there is no `args.size() > 6` heap fallback". The truncation behavior (excess args truncated or `static_assert`/debug-assert bounded) is specified. No unbounded path.

## Non-Functional Consistency

- [x] CHK021 Is the mandatory bench-spike obligation (`[const §XIII.5]`, TS-13 executes + records) consistent with TS-13 being **non-blocking** for v1.0 delivery? [Consistency, Spec §FR-021/§SC-008] — PASS: FR-021: "TS-13 MUST **execute** and record its disposition... it does **not** gate delivery of the facade and could justify a later swap"; SC-008: "TS-13, which MUST **execute and record its disposition** (discharging `[const §XIII.5]`'s mandatory-spike obligation)... The TS-13 backend-selection validity... is a **recorded, non-blocking** metric". The "MUST execute" + "non-blocking for delivery" combination is explicitly stated and internally consistent.
- [x] CHK022 Are the performance requirements consistent with the backend being an implementation detail (the 50 ns gate holds for the own-ring v1.0 candidate behind the facade)? [Consistency, Spec §FR-021, Assumption] — PASS: FR-021: "The public `Logger` facade contract is identical regardless of backend"; spec §Assumptions: "The v1.0 shipping candidate is the own lock-free MPSC ring (FR-021); the backend is an implementation detail behind the `Logger` facade, the spec's contract holds for either"; FR-002's 50 ns gate applies to the producer path, not the backend — consistent whether own-ring or quill is used.
- [x] CHK023 Is the third-party pin (`opentelemetry-cpp/1.26.0`, `quill/11.1.0` spike-only) consistent across spec/plan/tasks and the superseded-anchor note? [Consistency, Spec §FR-023, Assumption] — PASS: FR-023: "pinned **exactly** to `opentelemetry-cpp/1.26.0`"; spec §Assumptions: "The OpenTelemetry C++ SDK is pinned exactly to `opentelemetry-cpp/1.26.0`... refreshed 2026-06-02 from the anchor's now-stale `1.16.1`/`quill 3.9.0` pins... the anchor's pins are superseded by FR-023"; plan.md: same `1.26.0` + `quill/11.1.0`; T001/T002 in tasks.md: same pins; research.md R1: "Pin **exactly `opentelemetry-cpp/1.26.0`**"; anchor §10 Q3/Q5: both resolved (pins recorded). Consistent across all artifacts; superseded-anchor note is explicit in spec and research.

## Notes

- Tests requirement quality (is the perf target quantified/measurable/baseline-anchored), not whether the code hits 50 ns.
- Step-9 audit dispositions each item; CHK005/CHK008 (bench fill-rate + baseline chicken-egg) trace to `/speckit-analyze` F5/B1 — F5 was folded into tasks.md T030; the **requirement-text** clarity is re-checked here.
- CHK011 traces to `[[feedback_tracking_pmr_resource_false_pass]]` (dual-gate or false pass).

## Audit Result

| Disposition | Count |
|---|---|
| PASS | 23 |
| SPEC-FIXED | 0 |
| DD-DECIDED | 0 |
| WAIVED | 0 |
| **Total** | **23** |

### SPEC-FIXED items
_(none)_

### DD-DECIDED items
_(none)_

### WAIVED items
_(none)_

Anchors spot-verified: `[2k §1.2]` (spike Criterion B), `[2k §4.1]` (category/bitmask), `[2k §4.2]` (ArgValue/Record layout), `[2k §4.3]` (Logger, 50 ns ceiling, MPSC), `[2k §6.2]` (latency rationale), `[const §VIII.5]`, `[const §XIII.5]`, `[const §XV.17]`, `[arch §9.3]` — all resolve in signed-off revision `.specify/2k-log-otel.md` v0.5.
