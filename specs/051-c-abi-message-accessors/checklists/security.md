# Security Requirements Quality Checklist — 051-c-abi-message-accessors

**Purpose**: Validate that the security-relevant requirements (boundary integrity, injection prevention, lifetime/UAF safety, exception containment) are complete, unambiguous, and consistent. Audience: Gate B reviewer. Depth: formal release gate.
**Created**: 2026-06-25

## Boundary integrity (no C++ across `extern "C"`)

- [x] CHK001 - Is the "no C++ exception crosses the boundary" requirement stated for every accessor/setter/commit/group/toApp thunk, with the construction-vs-steady split (translate vs abort) unambiguous? [Completeness, Spec §FR-020] — PASS: spec FR-020 states "construction-time thunks translate caught C++ errors to codes ([arch §5.3] carve-out); steady-state thunks treat an escaping exception as an invariant violation → fatal log + std::abort ([2i §5.2]), never a translated code"; data-model E-9 repeats the two-class split; contracts/message-write.md marks each function with its class (C1=construction, C2=steady-abort).
- [x] CHK002 - Is the "no C++ symbol leakage" requirement (the `fixpp_capi.map` + per-PR nm gate; all symbols plain `fixpp_*`) specified for the new TUs? [Coverage, Plan §Constraints] — PASS: plan.md §Constraints states "all 33 new symbols are plain C `fixpp_*`; the nm golden gate and fixpp_capi.map export filter must stay green"; tasks.md T007/T010/T014/T017/T021 each include golden-file append + map update; the no-C++-symbol-leakage invariant is maintained from Features A/B.
- [x] CHK003 - Is it specified that steady-state thunks abort (fatal-log + `std::abort`) on an escaping exception rather than translating, so an invariant violation cannot be silently swallowed? [Clarity, Spec §FR-020] — PASS: spec FR-020 explicitly states "steady-state thunks treat an escaping exception as an invariant violation → fatal log + std::abort ([2i §5.2]), never a translated code"; [2i §5.2] construction-vs-steady thunk split verified to exist at line 1222; tasks.md T020 alloc-guard seam covers the abort path witness for the toApp trampoline.

## Wire-injection prevention (framing-tag forbidding)

- [x] CHK004 - Is the exact set of forbidden framing tags (8/9/34/49/52/56/10) enumerated identically at every site (set_*, entry_set_*, commit, the toApp framed-view distinction)? [Consistency, Spec §FR-006/§FR-024] — PASS: the set {8,9,34,49,52,56,10} appears identically in spec FR-006 ("framing-tag set {8/9/34/49/52/56/10}"), data-model E-3 INV-3, contracts/message-write.md set_* reject description, error-block-amendment.md comment on 1405, and security.md CHK004 itself; no drift across sites.
- [x] CHK005 - Is the framing-tag reject specified as fail-fast at set-time (not deferred to commit), and mapped to the distinct `FIXPP_ERR_MSG_FRAMING_TAG_FORBIDDEN` (NOT the session-domain code)? [Clarity, Spec §FR-006] — PASS: spec FR-006 states "MUST be checked at set-time (not deferred to commit) — fail-fast → FIXPP_ERR_MSG_FRAMING_TAG_FORBIDDEN (1405)"; data-model E-3 INV-3 labels this "reject at set-time"; error-block-amendment.md Gate-A-talking-point 2 explains why a distinct 1405 (not SESSION_INVALID_ARGUMENT) is the correct code; [2i §4.3] line 473 is the anchor.
- [x] CHK006 - Is the committed app-payload contract (no framing tags; the session stamps 8/9/34/49/52/56/10) specified so a consumer cannot forge session-framing fields? [Completeness, Spec §FR-008] — PASS: spec FR-008 states "the committed application payload contains NO framing tags — the session stamps 8/9/34/49/52/56/10 from its own session state"; contracts/message-write.md §fixpp_msg_commit notes the committed byte span is application-payload only; INV-3 + the framing-tag reject at set-time together enforce this invariant.
- [x] CHK007 - Is the asymmetry between the framing-forbidden accumulator view and the framing-VISIBLE toApp framed view stated so a consumer is not surprised that `34=`/`49=` are readable in toApp but rejected in their own accumulator? [Clarity, Spec §FR-024] — PASS: spec FR-024 §Framed toApp view (New-P2-b) explicitly states "toApp inspection window: same fixpp_msg_t opaque type but ALL tags visible including 34=/49= that are forbidden in the outbound accumulator"; contracts/message-read.md §Framed toApp view (New-P2-b) repeats the asymmetry; data-model E-1 dual-flavour section documents both read behaviours.

## Lifetime / use-after-free safety

- [x] CHK008 - Is the outbound-tombstone-on-session-close requirement (FR-009a) specified for ALL teardown paths — `fixpp_session_close` AND `fixpp_engine_destroy`-without-close AND internal removal — not close-only? [Coverage, Spec §FR-009a] — PASS: spec FR-009a states "the lazy weak_ptr<SessionLiveness> tombstone MUST fire on EVERY teardown path: fixpp_session_close, fixpp_engine_destroy-without-close, and internal removal"; data-model E-9 tombstone state machine lists all three paths; tasks.md T008 repeats "BOTH orderings" explicitly.
- [x] CHK009 - Is the discriminating witness requirement (the engine-destroy-without-close path that genuinely reclaims `session_arena_`, where a close-only token would still `.lock()`) stated as a mandatory seam? [Completeness, Spec §FR-021] — PASS: spec FR-021 explicitly requires "a mandatory seam: engine_destroy-without-close path that genuinely reclaims session_arena_ (a close-only token would still .lock()); this seam proves the check happens-before any arena dereference"; tasks.md T008 makes this seam mandatory and states BOTH orderings as separate test cases (close→set_* AND engine_destroy-without-close→set_*).
- [x] CHK010 - Is the "arena ACTUALLY reclaimed (not merely valid=false)" condition specified, so the seam proves the token check precedes any arena dereference under ASan AND TSan? [Measurability, Spec §FR-021] — PASS: spec FR-021 states "the seam must ACTUALLY reclaim the arena (state_.reset()), not merely set valid=false, so ASan detects any access-after-reclaim AND TSan detects any concurrent token-check vs teardown race"; tasks.md T008 specifies "run the tombstone seam under both ASan and TSan".
- [x] CHK011 - Is the returned read-pointer invalidation contract (next set_* on the same handle, callback-return for inbound, destroy for outbound) specified for the aliasing flyweight pointers? [Clarity, Spec §FR-002] — PASS: spec FR-002 states "returned pointers are flyweight aliases into the message arena; invalidated by: the next set_* on the same handle (for outbound), callback-return (for inbound dispatch window), fixpp_msg_destroy (for outbound)"; contracts/message-read.md flyweight-lifetime section repeats all three invalidation points.
- [x] CHK012 - Is the deep-copy-at-set requirement (borrowed buffers copied into the arena; caller may free immediately) stated so a borrowed-buffer pointer cannot escape into the message? [Completeness, Spec §FR-006] — PASS: spec FR-006 states "borrowed string/bytes inputs are deep-copied into the outbound accumulator's per-message arena at set-time; the caller may free the input buffer immediately after set returns"; contracts/message-write.md set_string/set_bytes rows confirm "copies into arena"; zero-global-heap invariant (FR-024 §US3) is the complementary constraint.
- [x] CHK013 - Is the commit-span lifetime invariant (immediate destroy safe ONLY because send blocks on `fut.get()` + `Engine::send` deep-copies at entry) recorded as a NORMATIVE dependency with a regression seam? [Traceability, contracts/message-write.md] — PASS: contracts/message-write.md §NORMATIVE inherited ordering invariant (Codex #4) states "commit→send→immediate-destroy is safe ONLY because Feature B's fixpp_session_send blocks on fut.get() AND Engine::send deep-copies at entry; a future non-blocking send would introduce UAF"; this block is labelled NORMATIVE explicitly; tasks.md T012 ASan seam witnesses the commit-span lifetime.

## Immutability & misuse paths

- [x] CHK014 - Is inbound immutability (`set_*`/group-build on an inbound flyweight → `INVALID_HANDLE`; clone-first to mutate) specified consistently? [Consistency, Spec §FR-007] — PASS: spec FR-007 states "set_* / group_begin / group_builder_add_entry on an inbound flyweight → FIXPP_ERR_INVALID_HANDLE; to mutate an inbound message, clone it first (CA-009 fixpp_msg_clone returns an independent outbound accumulator)"; contracts/message-write.md repeats this for each write operation; [2i §10 Q5] immutable-inbound is DECIDED.
- [x] CHK015 - Is the handle-type-tag mismatch path (passing a `fixpp_session_t*` where a `fixpp_msg_t*` is expected → `INVALID_HANDLE` before any struct read) specified? [Coverage, Spec §Edge Cases] — PASS: spec §Edge Cases explicitly lists "type-tag mismatch: passing a fixpp_session_t* where fixpp_msg_t* is expected — FIXPP_ERR_INVALID_HANDLE before any struct member read"; the handle-tag constants FIXPP_HANDLE_TAG_ENGINE / FIXPP_HANDLE_TAG_DEAD in capi_internal.hpp provide the verified mechanism from Feature B; FR-004 §handle-tag check is the requirement.
- [x] CHK016 - Is the toApp verdict a CLOSED enum (not an alias of `fixpp_error_t`), and is the out-of-range verdict a DEFINED misuse path (→ `APP_CALLBACK_THREW`, never silently coerced to send/veto)? [Clarity, Spec §FR-023] — PASS: spec FR-023 states "fixpp_toapp_verdict is a CLOSED C enum (NOT an alias of fixpp_error_t): FIXPP_TOAPP_SEND=0, FIXPP_TOAPP_VETO=1, FIXPP_TOAPP_ERROR=2; out-of-range → treated as FIXPP_TOAPP_ERROR (→ app_callback_threw, FIXPP_ERR_APP_CALLBACK_THREW), never silently coerced"; contracts/toapp-callback.md verdict mapping table explicitly handles out-of-range.
- [x] CHK017 - Are NULL-input requirements (`NULL_HANDLE`) specified uniformly across the new surface (read, set, create, commit, group, register)? [Completeness, Spec §FR-003/§FR-005] — PASS: spec FR-003 states "NULL handle pointer on any new function → FIXPP_ERR_NULL_HANDLE (or NULL return for pointer-returning functions) before any dereference"; FR-005 extends to output pointer args; contracts/message-read.md, message-write.md, and toapp-callback.md each list NULL handling per function; no function in the 33 new symbols is missing a NULL path specification.

## Audit Result

| Disposition | Count |
|---|---|
| PASS | 17 |
| SPEC-FIXED | 0 |
| DD-DECIDED | 0 |
| WAIVED | 0 |
| **Total** | **17** |

### SPEC-FIXED items
(none)

### DD-DECIDED items
(none)

### WAIVED items
(none)

Anchors spot-verified: `[2i §4.3]` (line 473), `[2i §4.6]` (line 723), `[2i §4.7]` (line 887), `[2i §4.8]` (line 1017), `[2i §5.2]` (line 1222), `[2i §10 Q5]` (line 1613), `[arch §5.3]` (architecture.md §5.3 Error model, line 391), `[arch §5.2]` (architecture.md §5.2 Allocator policy, line 384) — all resolve in signed-off revision [2i] v0.3 (Gate A round 2 converged).
