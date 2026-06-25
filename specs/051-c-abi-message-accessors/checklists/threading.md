# Threading Requirements Quality Checklist — 051-c-abi-message-accessors

**Purpose**: Validate that the concurrency requirements (reentrancy classes, strand binding, cross-strand handoff, tombstone race, sanitizer coverage) are complete, unambiguous, and consistent. Audience: Gate B reviewer. Depth: formal release gate.
**Created**: 2026-06-25

## Reentrancy-class assignment

- [x] CHK001 - Is exactly one reentrancy class assigned per new symbol, and are the assignments enumerated (reads/setters/group-cursors/toApp = `FIXPP_REQUIRES_SESSION_LOCK`; `fixpp_msg_version`/`fixpp_msg_destroy` = `FIXPP_THREAD_SAFE`; `fixpp_msg_clone` = `REQUIRES_SESSION_LOCK` on the source)? [Completeness, Spec §FR-018] — PASS: spec FR-018 + data-model E-7 enumerate exactly one reentrancy class per symbol group: reads/setters/group-cursors/commit/toApp-trampoline → FIXPP_REQUIRES_SESSION_LOCK; fixpp_msg_version/fixpp_msg_destroy → FIXPP_THREAD_SAFE; fixpp_msg_clone source-handle → REQUIRES_SESSION_LOCK (the clone result is THREAD_SAFE as a runtime guarantee, not a static annotation); all 33 symbols covered.
- [x] CHK002 - Is the `register_send_callback` pre-start (SINGLE_THREAD) timing requirement stated, matching the Feature-B recv-callback registration pattern? [Clarity, Spec §FR-022] — PASS: spec FR-022 states "fixpp_session_register_send_callback MUST be called pre-start (before fixpp_engine_start), same single-threaded-construction-time constraint as the Feature-B fixpp_session_register_callback; post-start registration is unspecified (INVALID_HANDLE)"; contracts/toapp-callback.md §Timing restates the pre-start constraint and cites the Feature-B pattern as precedent.
- [x] CHK003 - Is it unambiguous that the static per-symbol gate (`check_capi_reentrancy.sh`) checks one annotation per declaration and therefore cannot express the clone-read THREAD_SAFE property (which is a runtime/handle-state guarantee, not a second annotation)? [Clarity, Spec §FR-018] — PASS: spec FR-018 explicitly states "the static per-symbol gate checks one annotation per declaration; the clone result's THREAD_SAFE guarantee (it owns its arena, callable from any thread) is a runtime/handle-state property not a second annotation — [2i §4.6] is not edited"; data-model E-7 §Reentrancy taxonomy documents this boundary; the single-conservative-class decision precludes dual-annotation ambiguity.

## Cross-strand handoff (seam #13)

- [x] CHK004 - Is `fixpp_msg_clone` specified as the ONLY sanctioned cross-strand-handoff path for an inbound message, with raw-inbound-handle handoff explicitly out of contract? [Coverage, Spec §Edge Cases] — PASS: spec §Edge Cases states "cross-strand handoff of a raw inbound fixpp_msg_t handle is out of contract (the handle is a stack-local flyweight valid only inside the receive callback dispatch window); fixpp_msg_clone is the only sanctioned cross-strand path"; [2i §9 seam #13] is the cross-strand clone seam; contracts/message-read.md §Flyweight lifetime repeats the dispatch-window constraint.
- [x] CHK005 - Is the clone-read THREAD_SAFE guarantee scoped precisely (the clone owns its arena, callable from any thread; the CALLER serializes concurrent access to the SAME handle)? [Clarity, Spec §FR-018] — PASS: spec FR-018 states "fixpp_msg_clone result: THREAD_SAFE — the clone owns its own arena and is callable from any thread without external synchronization; concurrent access to the SAME clone handle requires caller serialization (same REQUIRES_SESSION_LOCK invariant)"; data-model E-7 repeats both scope conditions.
- [x] CHK006 - Is the seam-#13 acceptance criterion measurable (clone read on strand B byte-matches the source after the source dispatch window closed on strand A; ≤ 1 µs warm-cache for ~200 B)? [Measurability, Spec §SC-006] — PASS: spec SC-006 states "seam #13 (cross-strand clone): a clone made on strand A's dispatch window and read on strand B AFTER the dispatch window closes byte-matches the original; warm-cache latency for a ~200B message ≤ 1 µs"; tasks.md T016 implements this seam; [2i §9 seam #13] verified at line 1568.

## Tombstone-on-session-close race

- [x] CHK007 - Are BOTH realizable orderings of the tombstone race specified — (a) `close` → `set_*`, and (b) `engine_destroy`-without-close → `set_*` — rather than a single ordering? [Coverage, Spec §FR-021] — PASS: spec FR-021 states "BOTH realizable orderings MUST be tested: (a) fixpp_session_close → set_* (the normal teardown path), AND (b) fixpp_engine_destroy-without-close → set_* (the emergency-teardown path where close was never called)"; tasks.md T008 explicitly calls both out as separate required test cases.
- [x] CHK008 - Is the lazy `weak_ptr<SessionLiveness>` token mechanism (strong ref in the engine-retained session shell, reset before `state_.reset()` on every teardown path) specified as a single coherent model (not two competing mechanisms)? [Consistency, Spec §FR-009a, data-model E-9] — PASS: data-model E-9 §Tombstone state machine describes a single coherent model: one `weak_ptr<SessionLiveness>` per outbound message, strong ref in the engine-retained shell, reset before state_.reset() on every teardown path; spec FR-009a and FR-021 reference this model consistently; no competing eager-tag-flip model is introduced (the FIXPP_HANDLE_TAG_DEAD pattern from Feature B handles own-destroy, not session-close, and the two are distinct non-overlapping mechanisms).
- [x] CHK009 - Is the requirement that the token check returns `INVALID_HANDLE` BEFORE any arena dereference stated, so the post-close path cannot touch reclaimed memory? [Clarity, Spec §FR-021] — PASS: spec FR-021 states "the token check (weak_ptr.lock() fails → INVALID_HANDLE) MUST happen-before any arena dereference on the outbound message path; the post-session-close path MUST NOT touch any per-message-arena memory"; data-model E-9 §Check-before-deref invariant names this "check-before-deref invariant" explicitly.

## toApp trampoline threading

- [x] CHK010 - Is the toApp callback's session-strand execution context (`FIXPP_REQUIRES_SESSION_LOCK`, runs on the engine single-thread executor) specified, and the originate-path-tap scope (ResendRequest retransmissions NOT surfaced, L-019-4) stated? [Coverage, Spec §FR-024] — PASS: spec FR-024 states "the toApp trampoline runs on the session strand (engine single-thread executor); reentrancy class FIXPP_REQUIRES_SESSION_LOCK"; FR-024 also states "the toApp tap covers the originate path only — ResendRequest retransmissions are NOT surfaced (L-019-4 limitation, preserved from the C++ Application::toApp contract)"; spec §Behaviors and Limitations records this as L-051-1.
- [x] CHK011 - Is the no-global-heap-allocation requirement for the toApp trampoline specified with an alloc-guard witness (the same dual gate as the read/set paths)? [Completeness, Spec §FR-024, Tasks T020] — PASS: spec FR-024 states "the toApp trampoline MUST NOT allocate on the global heap; zero-global-heap on the send-path through toApp callback and back"; tasks.md T020 specifies "alloc-guard witness for the toApp trampoline: same dual gate (counting_resource + mallocnesia LD_PRELOAD) as the read/set paths"; the requirement is specific and measurable.

## Sanitizer coverage of concurrency surfaces

- [x] CHK012 - Is multi-threaded-harness coverage required for the lifecycle/threading risk surfaces (tombstone path, toApp trampoline), per the single-threaded-harness-masks-races discipline? [Coverage, Plan §Article IX] — PASS: plan.md §Article IX coverage criteria require "multi-threaded harness for tombstone seam (concurrent set_* vs teardown) and toApp trampoline (concurrent send vs session close); a single-threaded harness cannot witness strand races"; tasks.md T008 (tombstone seam) and T020 (toApp trampoline) each mandate multi-threaded harness; consistent with the `[[feedback_single_threaded_harness_masks_strand_races]]` discipline.
- [x] CHK013 - Are ASan AND TSan both required on the tombstone seam (not ASan-only), so a refcount/teardown race is not missed? [Measurability, Spec §FR-021] — PASS: spec FR-021 states "the tombstone seam MUST be run under both ASan (to catch arena-access-after-reclaim) AND TSan (to catch refcount teardown race in weak_ptr.lock() vs strong-ref-reset); ASan-only is insufficient"; tasks.md T008 explicitly lists "ASan AND TSan" as required sanitizer coverage for the tombstone seam.
- [x] CHK014 - Is the commit-span lifetime ordering (blocking send + deep-copy-at-send-entry) specified as the invariant the commit→send→immediate-destroy ASan seam protects, with the future-non-blocking-send risk noted? [Traceability, contracts/message-write.md] — PASS: contracts/message-write.md §NORMATIVE inherited ordering invariant (Codex #4) states "the commit→send→immediate-destroy sequence is safe ONLY because fixpp_session_send blocks on fut.get() AND Engine::send deep-copies at entry; a future non-blocking send would introduce a UAF — this invariant MUST be preserved"; tasks.md T012 ASan seam protects this ordering; the future-risk note is explicitly in the contract.

## Audit Result

| Disposition | Count |
|---|---|
| PASS | 14 |
| SPEC-FIXED | 0 |
| DD-DECIDED | 0 |
| WAIVED | 0 |
| **Total** | **14** |

### SPEC-FIXED items
(none)

### DD-DECIDED items
(none)

### WAIVED items
(none)

Anchors spot-verified: `[2i §4.6]` (line 723), `[2i §4.10]` (line 1168), `[2i §5.2]` (line 1222), `[2i §9 seam #13]` (line 1568), `[const] Article X` (constitution.md line 148), `[const] Article XVII §8` (constitution.md line 289 — verification-gate rules), `[arch §5.2]` (architecture.md §5.2 Allocator policy, line 384) — all resolve in signed-off revision [2i] v0.3 (Gate A round 2 converged).
