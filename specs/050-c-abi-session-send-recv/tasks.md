---
description: "Task list — 050-c-abi-session-send-recv (C-ABI Feature B)"
---

# Tasks: C ABI engine surface — Feature B (session lifecycle, message send, receive callback)

**Input**: Design documents from `specs/050-c-abi-session-send-recv/`
**Prerequisites**: plan.md ✔, spec.md ✔ (3 user stories, all P1), research.md (D-1..D-12), data-model.md (E-1..E-7), contracts/ (4).

**Tests**: REQUIRED (constitution Article VII §3 + the spec requests explicit batteries: SC-001..008, `tests/capi/*`). TDD ordering — write the test, see it FAIL, then implement.

**Build caps** (WSL2, per memory): `-j2` max; sanitizer presets ONE AT A TIME; never Linux+Windows builds in parallel.

**Symbol surface (21 new exported `fixpp_*`)** — engine: `fixpp_engine_create/start/destroy`; engine-config: `fixpp_engine_config_create/set_worker_threads/set_realtime_clock/destroy`; session: `fixpp_session_open/close/is_established/send/register_callback`; session-config: `fixpp_session_config_create/set_comp_ids/set_begin_string/set_role/set_heartbeat_seconds/set_security/set_dictionary/set_reset_on_logon/destroy`. Plus the `fixpp_recv_cb` typedef + 5 new `FIXPP_ERR_*` codes (119/77/129/130/131).

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: grounding + build wiring so every later task compiles.

- [ ] T001 Read the anchors before writing code: `.specify/2i-capi.md` (shape, `[2i §4.2/4.3/4.6/4.9/4.10/5.2/10]`), Feature A's shipped surface (`include/fix/c_api/{handles,error,version,export,c_api}.h`, `src/capi/{error,version,handles}.cpp`, `tests/abi/golden/fixpp_capi_symbols.txt`, `tools/abi_history/error_codes_v1.txt`, `tools/check_capi_{occupancy,reentrancy}.sh`), and the real C++ surface (`include/fixpp/session/{engine,session,application,engine_config,session_config}.hpp`, `src/session/{engine,session,seqnum_manager}.cpp`). Confirm the `## Gate A → Round 1 deviations` (send=THREAD_SAFE) + `Round 2 decisions` (LEAVE threading-block) + the round-3 reachable-arm set in plan.md.
- [ ] T002 [P] Wire `src/capi/CMakeLists.txt`: add `engine.cpp`, `session.cpp`, `config.cpp` to `fixpp_capi_objects`; ensure the OBJECT lib links the existing `fixpp_session` (Engine/Session/Application) + `fixpp_core` (error) targets. (No new third-party dep.)
- [ ] T003 [P] Wire `tests/capi/CMakeLists.txt`: register the four new targets `lifecycle_test`, `send_recv_test`, `error_block_test`, `thunk_split_test`; link the C-ABI lib + GoogleTest + the `tests/interop/support` loopback harness + `tests/support/minimal_dictionary.hpp` (test-supplied dict seam, L-050-1). The pure-C linkage smoke (SC-003) compiles a `.c` TU against the headers.
- [ ] T004 Confirm the `capi → session`/`capi → core` layering is whitelisted in `tools/check_layers.py` + `.specify/architecture.md` (Feature A activated `capi/`; Feature B adds TUs linking `fixpp_session` — extend the whitelist row if `engine.cpp`/`session.cpp`/`config.cpp` are not yet covered). Run `tools/check_layers.py` → green.

---

## Phase 2: Foundational (Blocking Prerequisites)

**⚠️ CRITICAL**: the public header skeleton, the error block, the config builders, and the internal event-loop/trampoline scaffold block ALL user stories.

- [ ] T005 [P] `include/fix/c_api/handles.h` — add opaque forward typedefs `fixpp_engine_config_t` / `fixpp_session_config_t` (and confirm `fixpp_engine_t`/`fixpp_session_t`/`fixpp_msg_t` from Feature A). Pure C11; no struct layout exposed. (FR-014.)
- [ ] T006 [P] `include/fix/c_api/version.h` + `include/fix/c_api/c_api.h` — bump `FIXPP_C_ABI_VERSION_MINOR` 2 → 3 (0.2.0 → 0.3.0, FR-020); umbrella `c_api.h` includes the new `engine.h` + `session.h`.
- [ ] T007 `include/fix/c_api/engine.h` — NEW: declare `fixpp_engine_create/start/destroy` + the `fixpp_engine_config_*` builder family, each `extern "C"` with its reentrancy doc-block (FR-016/017) and the `[2i §5.2]` thunk-split note. `create` takes `(fixpp_engine_config_t* cfg, uint16_t consumer_major, uint16_t consumer_minor, fixpp_engine_t** out)`; `cfg` CONSUMED-on-success.
- [ ] T008 `include/fix/c_api/session.h` — NEW: declare `fixpp_session_open/close/is_established/send/register_callback` + the `fixpp_session_config_*` builder family + the `fixpp_recv_cb` typedef + the frozen enums `fixpp_session_role {INITIATOR=0,ACCEPTOR=1}` and `fixpp_security_kind {TLS=0,INSECURE_PLAIN_TCP=1}`. Reentrancy doc-blocks per FR-017 (send=THREAD_SAFE w/ the callback carve-out + Gate-A-deviation note; close=SINGLE_THREAD non-callback-caller; is_established=THREAD_SAFE; register_callback=SINGLE_THREAD construction-time). The `register_callback` doc-block MUST state the FR-013a no-blocking-call-from-callback rule.
- [ ] T009 `include/fix/c_api/error.h` — append the `FIXPP_ERR_SESSION_*`/app block at the reserved `[2i §4.3]` slots: `FIXPP_ERR_SESSION_INVALID_ARGUMENT` (119), `FIXPP_ERR_SESSION_INVALID_STATE` (77), `FIXPP_ERR_APP_DO_NOT_SEND` (129), `FIXPP_ERR_APP_CALLBACK_THREW` (130), `FIXPP_ERR_APP_PAYLOAD_MALFORMED` (131). Never re-use/re-number an existing slot (Article X §4). (FR-015; data-model E-4.)
- [ ] T010 `src/capi/error.cpp` — re-point `translate()` arms for the **5 reachable** UNKNOWN variants (119/77/129/130/131) to the new codes; keep `translate()` a TOTAL switch, no `default` (`-Wswitch`). Leave the threading-block arms (51/52/53/48/54) on their existing `THREAD_*` codes (LEAVE decision) and the non-reachable session/app/log/otel/oom variants on `UNKNOWN` (residual L-049-2). Add a non-null `fixpp_strerror` static string per new code (zero-alloc).
- [ ] T011 [P] `tools/abi_history/error_codes_v1.txt` — append the 5 new codes → symbol with `introducing_minor = 3`; run `tools/check_capi_occupancy.sh` → green (Check A header layout == `[2i §4.3]`; Check B sibling variant-row counts).
- [ ] T012 `src/capi/config.cpp` — NEW: the two opaque builder families. Each wraps a heap `EngineConfig`/`SessionConfig` under construction (E-1/E-3); `create` = construction-time thunk; setters validate cheaply (empty CompID → `FIXPP_ERR_CAPI_CONFIG_INVALID`) else defer to open/create; `destroy` NULL-safe; non-`const` consuming pointer semantics (consumed at create/open, invalidated on success, owner-frees on failure — Codex #6). `set_security` typed `(kind, const char* cert, const char* key)`; `set_dictionary(fixpp_dict_t*)` thin pass-through (creation is Feature C; test-supplied seam, L-050-1). The engine `application` is set internally to the trampoline (not consumer-settable).
- [ ] T013 `src/capi/engine.cpp` — NEW (scaffold, no thunks yet): the internal io_context + worker-thread(s) + work-guard owner (D-2) and the `CapiApplication` trampoline class (subclass of `Application`, holds a `SessionId → {fixpp_recv_cb, void* userdata}` map; `fromApp` override is a stub for now). This is the load-bearing structural novelty (engine owns no threads — `engine.hpp:222`); isolate it here. Construct executors INSIDE the worker, honour the no-fork limitation (L-050-2, no code, comment only).
- [ ] T014 [P] `tests/abi/golden/fixpp_capi_symbols.txt` — append all 21 new exported `fixpp_*` symbols (sorted). (FR-018; the per-PR nm gate verifies set == golden AND 0 C++ leak — SC-003.)

**Checkpoint**: headers compile as C11; error block + occupancy green; config builders + event-loop/trampoline scaffold exist. User stories can begin.

---

## Phase 3: User Story 1 — Engine + session lifecycle from C (Priority: P1) 🎯 MVP

**Goal**: a pure-C program creates an engine, opens a session, starts, polls establishment, closes, destroys — no exception crosses the boundary, destroy idempotent incl. never-started.

**Independent Test**: `lifecycle_test` — create→open→start→is_established→close→destroy over the loopback harness; register-after-start rejected; double-destroy + null-destroy no-op; never-started destroy does NOT abort; SC-007 close-breaks-blocked-read on real sockets (TSan).

### Tests for US1 (write FIRST, see them FAIL)

- [ ] T015 [P] [US1] `tests/capi/lifecycle_test.cpp` — create/start/destroy + open/close/is_established happy path; **never-started destroy** (`create`→`session_open` fails→`destroy`) asserts no abort (FR-003); double-destroy + NULL-destroy no-op; `session_open` after `engine_start` → domain error (FR-004); `register_callback` after `engine_start` → `FIXPP_ERR_CAPI_CONFIG_INVALID` (FR-011 enforcement — mirrors FR-004; no mutex in coroutine context requires pre-start only); NULL/dead handle → `FIXPP_ERR_NULL_HANDLE`/`INVALID_HANDLE` (FR-006).
- [ ] T016 [P] [US1] In `lifecycle_test.cpp` add the **SC-007** witness — BOTH conjuncts: (a) park an established idle session in `async_read`, `fixpp_session_close`, assert prompt teardown by socket-close/cancellation NOT by a deadline elapsing (distinguishing witness); (b) issue an in-flight `fixpp_session_send` during engine total-cancellation/teardown and assert the return is `FIXPP_ERR_CANCELLED` (uniform code, FR-010) — a LIVE cross-thread cancel stimulus on the send bridge, not merely the T022 mapping oracle. Multi-threaded harness, TSan-gated (cross-thread close+send bridges, E-6).

### Implementation for US1

- [ ] T017 [US1] `src/capi/engine.cpp` — `fixpp_engine_create` thunk: build `EngineConfig` from the consumed builder; stand up the internal io_context+worker; install the `CapiApplication` as `EngineConfig::application`; `new Engine`; record `consumer_minor` (D-9, L-049-1); `consumer_major != 0` → `FIXPP_ERR_VERSION_MISMATCH`; bad config → `*_CONFIG`, no exception escapes (construction-time thunk).
- [ ] T018 [US1] `src/capi/engine.cpp` — `fixpp_engine_start` (once; null clock → `clock_not_set` translated; second call → error) and `fixpp_engine_destroy` (**unconditional** `co_await Engine::stop()` → reset work-guard → join worker → `delete Engine`; NULL/double → no-op — FR-003).
- [ ] T019 [US1] `src/capi/session.cpp` — NEW: `fixpp_session_open` (reject if engine started; `Engine::register_session`; key a non-owning `fixpp_session_t` by `SessionId::from_config`; dup → `FIXPP_ERR_SESSION_INVALID_ARGUMENT`; builder consumed-on-success) + `fixpp_session_is_established` (`lookup(id)!=null && is_open()`, THREAD_SAFE).
- [ ] T020 [US1] `src/capi/session.cpp` — `fixpp_session_close`: `lookup(id)`; post `Session::close(graceful)` onto the session domain and block on completion (E-6 bridge); translate; invalidate the handle; already-closed → `session_already_closed` (52) → `FIXPP_ERR_THREAD_SESSION_LIFECYCLE` (existing). Use ASIO native cancellation that closes the transport (FR-005; SC-007).
- [ ] T021 [US1] Apply the FR-017 reentrancy annotations to every US1 symbol + run `tools/check_capi_reentrancy.sh` → green for the lifecycle + config-builder symbols.

**Checkpoint**: US1 fully functional + independently testable (MVP). Build + `lifecycle_test` green (debug + TSan).

---

## Phase 4: User Story 2 — Send a message over an open session (Priority: P1)

**Goal**: a C consumer sends a committed wire-frame byte span and learns the outcome as a stable code; durable-before-transmit honoured by reference; the session-domain error surface is live + downgrade-correct.

**Independent Test**: send returns success + peer receives; the reachable error arms map to their published codes (incl. `store_seqnum_overflow`→STORE_RUNTIME via a restored-at-`seqnum_max` session, and `wire_frame_too_large`→WIRE_LIMIT_EXCEEDED on an oversized frame); cancellation → uniform code; steady-state abort on synthetic throw; downgrade live.

### Tests for US2 (write FIRST, see them FAIL)

- [ ] T022 [P] [US2] `tests/capi/error_block_test.cpp` — mutation-tested per-row oracle (D-11): each newly-published arm (119/77/129/130/131) maps to its **specific** new code and flipping it off UNKNOWN → RED; each existing-published arm (threading-block, `store_seqnum_overflow`/STORE_RUNTIME, `wire_frame_too_large`/WIRE_LIMIT_EXCEEDED, CANCELLED) still maps to its existing code (not silently re-pointed). (FR-015; SC-005.)
- [ ] T023 [P] [US2] In `error_block_test.cpp` add **SC-004** downgrade-live: an engine created with simulated `consumer_minor < 3` downgrades a new minor-3 session code → `FIXPP_ERR_UNKNOWN` on the return path; `consumer_minor >= 3` sees the real code (L-049-1).
- [ ] T024 [P] [US2] `tests/capi/thunk_split_test.cpp` — **SC-006**: synthetic throw into the steady-state `fixpp_session_send` thunk → SIGABRT (reuse Feature A's §9-seam-5b `sigaction`+`setjmp/longjmp` trap); a throw into a construction-time thunk (create/open/start) → `*_CONFIG` + NO abort.

### Implementation for US2

- [ ] T025 [US2] `src/capi/session.cpp` — `fixpp_session_send(session, const uint8_t* frame, size_t len)`: map to `Engine::send(SessionId, span<const byte>)`; **steady-state** thunk (escaping exception → fatal-log + `std::abort`, NOT translated — FR-008/019); durable-before-transmit honoured by reference (FR-009, `[2e §6.1.4]`); borrowed-frame discipline (caller may free on return). Reentrancy `FIXPP_THREAD_SAFE` with the receive-callback carve-out (FR-017/FR-013a).
- [ ] T026 [US2] Ensure the send-path return codes match data-model E-4 exactly: `OK`; app arms 129/130/131; session arms 77/119; `WIRE_LIMIT_EXCEEDED` (30, oversized frame); `STORE_RUNTIME` (60, `store_seqnum_overflow`); `THREAD_SESSION_LIFECYCLE` (52, send-vs-close TOCTOU); `CANCELLED` (uniform, FR-010). NO `store_io_failure` (I-07-swallowed, L-050-3). All run through `translate_for_consumer`.
- [ ] T027 [US2] Run `tools/check_capi_reentrancy.sh` → green for `fixpp_session_send`; build + `error_block_test` + `thunk_split_test` green (debug + ASan + UBSan).

**Checkpoint**: US1 + US2 work independently. Session-domain error surface live; downgrade live (SC-004/005); thunk split proven (SC-006).

---

## Phase 5: User Story 3 — Receive inbound messages via a registered callback (Priority: P1)

**Goal**: a registered C callback fires on inbound application messages with an inbound `fixpp_msg_t` valid only inside the dispatch window; no further callbacks after close.

**Independent Test**: register callback → loopback peer sends → callback fires with a readable inbound handle + the registered user-data; inbound handle invalid after return (ASan); no callbacks after close; the D-10 supported reply path (copy-out-then-send-from-a-drain-thread) completes.

### Tests for US3 (write FIRST, see them FAIL)

- [ ] T028 [P] [US3] `tests/capi/send_recv_test.cpp` — the **HEADLINE SC-001 + SC-008** round-trip (D-11): two C-ABI engines (initiator A + acceptor B) over loopback plaintext TCP, each its own io_context+worker; A logs on to B → both poll `is_established` → A sends an app frame → B's on-strand callback receives + copies out + **replies from a drain thread** (D-10 supported path) → A's callback receives the reply → both graceful-close → both destroy. Test-supplied dictionary (L-050-1).
- [ ] T029 [P] [US3] In `send_recv_test.cpp` add **SC-008**: a use-after-return on the inbound `fixpp_msg_t` (retained past the callback) is caught under **ASan** (negative test) — the `[2i §4.6]` dispatch-window lifetime; and assert no callbacks fire for a session after `fixpp_session_close` (FR-012).
- [ ] T030 [P] [US3] In `send_recv_test.cpp` witness the **FR-013a** SUPPORTED path only (D-10): copy-out-then-send-from-a-drain-thread completes (no watchdog "prove it hangs" test). The completing reply path IS the positive proof.

### Implementation for US3

- [ ] T031 [US3] `src/capi/session.cpp` — `fixpp_session_register_callback(session, fixpp_recv_cb, void* userdata)`: construction-time thunk (MUST precede `fixpp_engine_start`); if called after engine started, return `FIXPP_ERR_CAPI_CONFIG_INVALID` (enforcement mirrors FR-004 for `session_open` — the map is read on-strand without a mutex so a post-start call is a data race); else populate the `CapiApplication` `SessionId→{cb,userdata}` map.
- [ ] T032 [US3] `src/capi/engine.cpp` — implement the `CapiApplication::fromApp` trampoline: on the session strand, wrap the `MessageView` in a stack `fixpp_msg_t`, look up `{cb,userdata}`, invoke the C callback inside a `try/catch(...)` → fatal-log + `std::abort` (steady-state on-strand invariant; zero-global-heap-alloc per `[const §VIII.5]`); the inbound handle is engine-owned + destroyed at parse-window close (FR-012). No-op (accept) if no callback registered.
- [ ] T033 [US3] Run `tools/check_capi_reentrancy.sh` → green for `register_callback` (the callback's on-strand dispatch = REQUIRES_SESSION_LOCK is a typedef contract, documented); build + `send_recv_test` green (debug + ASan + TSan).

**Checkpoint**: full round-trip (SC-001) green; all 3 user stories independently functional.

---

## Phase 6: Polish & Cross-Cutting Concerns

- [ ] T034 [P] Confirm **SC-002** (100% of the 21 new symbols carry exactly one reentrancy class, 0 unannotated) via `tools/check_capi_reentrancy.sh` both directions; **SC-003** (nm symbol-golden == exported set AND 0 C++ leak) via the per-PR nm gate on the branch with the updated golden.
- [ ] T034a [P] **§VIII.5 zero-global-alloc gate** (the trampoline hot path, FR-013): run the `send_recv_test` round-trip under `LD_PRELOAD=tools/mallocnesia/libmallocnesia.so` (global-malloc interception) and assert NO global heap allocation fires on the on-strand `CapiApplication::fromApp` callback path. A tracking-PMR/counting-resource alone is a FALSE-PASS without global interception (memory `feedback_tracking_pmr_resource_false_pass`); mallocnesia is the binding gate. Guard the alloc-witness under the sanitizer-incompat caveat (`feedback_operator_new_witness_breaks_sanitizers`).
- [ ] T035 [P] `fixpp_version()` reflects 0.3.0; the `version.h`/`c_api.h` bump is consistent; `fixpp_decimal_t` PoD untouched (Article X §3).
- [ ] T036 [P] Add the Behaviors & Limitations rows to `spec/behaviors-and-limitations.md`: B-050-* (the C-ABI session/send/receive surface) + L-050-1 (round-trip dict blocked on Feature C), L-050-2 (no fork() holding a live engine), L-050-3 (store I/O failure I-07-swallowed on send), L-050-x (lifecycle callbacks / non-blocking send deferred).
- [ ] T037 Run the local Tier-1 mirror prep: full unfiltered `ctest` (public-header change → run FULL ctest, not a prefix filter — memory `feedback_speckit_verify_prefix_filter_misses_header_consumers`); commit/stash any dirty tree first (codegen graph-check is a git-cleanliness gate).

### Mandatory close-out tasks (ALWAYS emit — Gate-B preconditions, Article XVII §8)

- [ ] T038 [P] **Catalogue close-out**: flip `spec/feature-catalogue.md` rows **CA-005 / CA-006 / CA-007** → `done` with the PR/evidence ref, and add/update their `spec/coverage-index.md` entries.
- [ ] T039 **Feature-completeness audit (FINAL task)**: assert against the merged tree that (i) every `tasks.md` row is `[X]` or carries an explicit waiver; (ii) every FR-001..022 and SC-001..008 maps to a landed test AND landed implementation; (iii) CA-005/006/007 are `done` with matching `coverage-index.md` entries. Record the verdict (100% or fully-waived) in `.specify/decisions/050-c-abi-session-send-recv-verify.md` `## Completeness` (or a sibling `-completeness.md`). HARD `/gate-b` precondition (Article XVII §8 / pre-flight 4d).

---

## Dependencies & Execution Order

- **Phase 1 Setup** → **Phase 2 Foundational** (BLOCKS all stories: headers + error block + config builders + event-loop/trampoline scaffold) → **Phase 3 US1** → **Phase 4 US2** → **Phase 5 US3** → **Phase 6 Polish**.
- US1 is the MVP and the lifecycle prerequisite for US2 (needs an open+started session to send) and US3 (needs an open session to receive). US2 and US3 are otherwise independent of each other.
- The error block (T009–T011) is foundational because every thunk returns through `translate()`; the downgrade wiring (T017 records `consumer_minor`) is exercised by the US2 `error_block_test`.

### Within each story

- Tests written and FAILING before implementation (TDD).
- Headers/config/scaffold (Phase 2) before any thunk.
- `src/capi/session.cpp` open/is_established (T019) before close (T020) before send (T025) before register_callback (T031).

### Parallel opportunities

- Phase 1: T002/T003 [P]; Phase 2: T005/T006/T011/T014 [P] (different files); the test files T015/T022/T024/T028 [P] (different test TUs).
- US2 and US3 implementation can proceed in parallel once US1 lands (different concerns in `session.cpp`/`engine.cpp` — coordinate the shared files).

---

## Implementation Strategy

**MVP** = Phase 1 + Phase 2 + Phase 3 (US1 lifecycle): a C program can create/open/start/poll/close/destroy. **Then** US2 (send + error surface) → US3 (receive callback, closes the round-trip SC-001) → Polish + close-out. Build `-j2`; sanitizer presets ONE AT A TIME; full unfiltered ctest before Gate B (public-header change).

## Notes

- The headline `send_recv_test` drives BOTH ends through the C ABI (no mock transport — a mock hides SC-007). Lighter cases use the one-C-ABI-side + one-C++-side `tests/interop/support` harness.
- Inbound-handle UAF (SC-008) and the steady-state abort (SC-006) are negative tests; the D-10 deadlock is witnessed by the SUPPORTED path completing, not a watchdog.
- Deferred (recorded so the completeness audit reads them as intentional, not gaps): lifecycle callbacks (L-050-x), app-reject-from-callback (D-4), multi-thread executor (D-2), post-start registration (D-1/D-4), outbound `fixpp_msg_*` construction (Feature C).
