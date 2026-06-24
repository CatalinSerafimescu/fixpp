# Tasks: C ABI message surface — Feature C (field/group accessors, outbound construct + commit, toApp hook) + the [2i §4.3] session/app error-block amendment

**Input**: Design documents from `specs/051-c-abi-message-accessors/`
**Prerequisites**: plan.md ✔, spec.md ✔, research.md (D-1..D-13), data-model.md (E-1..E-9), contracts/{message-read,message-write,toapp-callback,error-block-amendment}.md ✔

**Tests**: INCLUDED — the spec mandates a C-ABI test corpus (FR-021, SC-001..006, [2i §9] seams). TDD per story: write the test first, watch it FAIL, then implement.

**Organization**: by user story (P1: US1/US2/US3; P2: US4/US6/US5). Exact symbol set is governed by the nm golden `tests/abi/golden/fixpp_capi_symbols.txt` — **exactly 33 new exported functions** (8 read + 10 outbound + 6 group-read + 8 group-build + 1 register), plus 6 error `#define`s + the `fixpp_toapp_verdict` enum + callback typedef (NOT exported, NOT counted).

## Format: `[ID] [P?] [Story] Description`

- **[P]**: parallelizable (different file, no incomplete-task dependency)
- **[Story]**: US1..US6 (Setup/Foundational/Polish carry no story label)

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: scaffold the new public header + build wiring. The 049/050 C-ABI infrastructure (opaque-handle type-tag plumbing, tombstone discipline, reentrancy/occupancy/golden gates, `translate_for_consumer`) is reused, not rebuilt.

- [ ] T001 [P] Create `include/fix/c_api/message.h` skeleton — C11-clean (`<stdint.h>`/`<stddef.h>`/`<stdbool.h>` only), opaque typedefs `fixpp_group_t` / `fixpp_group_builder_t` / `fixpp_entry_t`, include guard, per-symbol reentrancy-annotation macro scaffolding; add `#include "fix/c_api/message.h"` to the `include/fix/c_api.h` umbrella (session.h already included).
- [ ] T002 [P] Register TUs + test targets: add `message_read.cpp` / `message_write.cpp` to `src/capi/CMakeLists.txt` (`fixpp_capi_objects`); add `message_read_test` / `message_write_test` / `msg_clone_cross_strand_test` / `toapp_callback_test` / `error_block_test` to `tests/capi/CMakeLists.txt`.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: handle plumbing + the complete error-block amendment that every story depends on. **⚠️ No user story can begin until this phase is complete.**

- [ ] T003 Outbound-handle plumbing in `src/capi/capi_internal.hpp`: outbound `fixpp_msg` in-arena accumulator struct + handle `tag_` field; `std::weak_ptr<SessionLiveness>` validity token (data-model E-9); the `fixpp_session` shell gains a strong `std::shared_ptr<SessionLiveness>` that is **reset on EVERY arena-teardown path** — `fixpp_session_close` AND the `fixpp_engine_destroy` `sessions_` loop AND internal session removal (NOT close-only, per FR-009a / the 050 tombstone discipline `[[feedback_cabi_handle_destroy_needs_tombstone]]`); `SessionSlot` gains the `send_cb` trampoline slot (used by US6).
- [ ] T004 **Complete `[2i §4.3]` error-block amendment — test-first, in ONE atomic pass** (FR-013/014/015/016/017; the occupancy gate cross-checks error.h ↔ `[2i §4.3]` ↔ `error_codes_v1.txt` ↔ EXPECTED, so a split goes transiently red for every following commit). Per Article VII §3, write the RED unit witness FIRST, watch it FAIL (codes/translate absent), THEN land the co-update to green it. All of:
  - **RED first** — `tests/capi/error_block_test.cpp` (partial, error.cpp unit level): assert `translate()` maps the five C++ ordinals (119/77/129/130/131) to `FIXPP_ERR_SESSION_*`/`FIXPP_ERR_APP_*` (1400–1404), NOT `FIXPP_ERR_UNKNOWN`; `fixpp_strerror(1400..1405)` non-empty; per-code minor downgrade BOTH ways at `consumer_minor=3` (a new minor-4 code → `UNKNOWN`; existing minor-2 `FIXPP_ERR_DICT_CONFIG` survives, FR-017). This FAILS until the co-update lands. (T022 later extends this same file with the live end-to-end 5-arm + toApp witness.)
  - `include/fix/c_api/error.h` — six codes in the dedicated Phase-4 block `[1400,1499]` (`FIXPP_ERR_SESSION_INVALID_ARGUMENT`=1400, `FIXPP_ERR_SESSION_INVALID_STATE`=1401, `FIXPP_ERR_APP_DO_NOT_SEND`=1402, `FIXPP_ERR_APP_CALLBACK_THREW`=1403, `FIXPP_ERR_APP_PAYLOAD_MALFORMED`=1404, `FIXPP_ERR_MSG_FRAMING_TAG_FORBIDDEN`=1405);
  - `include/fix/c_api/version.h` — bump `FIXPP_C_ABI_VERSION_MINOR` (0.3.0 → 0.4.0, FR-019);
  - `src/capi/error.cpp` — replace the scalar `kIntroducingMinor=2` with a **per-code** introducing-minor lookup (existing codes minor 2, the six new minor 4, FR-015/017), re-point the **five** mapped `translate()` arms (`session_invalid_argument`/`session_invalid_state_for_send`/`app_do_not_send`/`app_callback_threw`/`app_payload_malformed`) off `FIXPP_ERR_UNKNOWN` (1405 gets NO `translate()` arm), add six `fixpp_strerror` entries (incl. 1405);
  - `.specify/2i-capi.md` — §4.3 dedicated Phase-4 `[1400,1499]` block + §1.1 magnitude-domain table (new Phase-4 row) + §1.1 final-layout (`[1400,1499]` row) + §1.1 reserved-blocks prose (mark spent) + §4.3 inline `#define` block. **`[2i §4.7]` is NOT edited** (FR-008a local deviation);
  - `tools/abi_history/error_codes_v1.txt` — append 6 rows (introducing-minor 4; 1405 = construction reject, no C++ ordinal);
  - `tools/check_capi_occupancy.sh` — Check A `EXPECTED` map +6 (1400–1405). **Unchanged**: `[0,99]` count (stays 11/8), Check B's 8 prior-doc source-domain counts, the prior-doc `97` total, `[2i §3.11]`/`[2i §6.5]`/Appendix D.2.

**Checkpoint**: handles + the full error surface available; occupancy/audit gates green — all stories can start.

---

## Phase 3: User Story 1 - Read scalar fields from an inbound message (Priority: P1) 🎯 MVP

**Goal**: tag-keyed inbound field read accessors (CA-008) thunking into `wire::MessageView::get`, no exception across the boundary, zero global-heap, aliasing views.

**Independent Test**: inside a receive callback a pure-C program reads `35` + string/int/double/decimal tags; absent→`TAG_NOT_FOUND`, wrong-flavour→`TYPE_MISMATCH`, non-numeric int→`WIRE_INVALID_FRAME`; string pointer aliases the wire buffer (zero global-heap).

- [ ] T005 [US1] Write `tests/capi/message_read_test.cpp` (FAIL first): read `string/bytes/int/double/decimal` + `get_msg_type` + `has_tag` + `version`; absent→`FIXPP_ERR_TAG_NOT_FOUND`, wrong-flavour→`FIXPP_ERR_TYPE_MISMATCH`, non-numeric int/double→`FIXPP_ERR_WIRE_INVALID_FRAME`, NULL→`FIXPP_ERR_NULL_HANDLE`, expired/destroyed→`FIXPP_ERR_INVALID_HANDLE`; assert the string pointer **aliases** the wire buffer (not a copy); zero-global-heap allocation guard (counting-resource + mallocnesia LD_PRELOAD dual gate, SC-003).
- [ ] T006 [US1] Implement CA-008 in `src/capi/message_read.cpp` (per contracts/message-read.md): `fixpp_msg_get_string/get_bytes/get_int/get_double/get_decimal`, `fixpp_msg_has_tag`, `fixpp_msg_version`, `fixpp_msg_get_msg_type` — thin thunks over `wire::MessageView::get<...>`; reentrancy = `FIXPP_REQUIRES_SESSION_LOCK` (reads) except `fixpp_msg_version` = `FIXPP_THREAD_SAFE` (FR-018); steady-state thunks abort-on-escape (no translate, `[2i §5.2]`).
- [ ] T007 [US1] Append the 8 CA-008 symbols to `tests/abi/golden/fixpp_capi_symbols.txt` (same commit as T006 — the nm gate compares exported ↔ golden).

**Checkpoint**: inbound scalar read works pure-C end-to-end.

---

## Phase 4: User Story 2 - Construct, populate, commit, send an outbound message (Priority: P1)

**Goal**: net-new in-arena outbound accumulator + `fixpp_msg_commit` serialising a valid wire-order app-payload (no framing tags) that the existing Feature-B `fixpp_session_send` accepts; tombstone-on-session-close (FR-009a); clone escape hatch.

**Independent Test**: `fixpp_msg_create_outbound(session,"D",1,&msg)` → set string/int/double/decimal → `fixpp_msg_commit` → `fixpp_session_send` → peer receives well-formed; over-cap commit→`WIRE_LIMIT_EXCEEDED`; destroy idempotent/NULL-safe.

- [ ] T008 [US2] Write `tests/capi/message_write_test.cpp` (FAIL first): create→set→commit→send round-trip (SC-001, span carries `35=D` + app fields, no `8/9/34/49/52/56/10`); set framing tag→`FIXPP_ERR_MSG_FRAMING_TAG_FORBIDDEN` (set-time fail-fast); wrong dictionary type→`TYPE_MISMATCH`; tag absent from dict→`DICT_CONFIG`; commit over frame-cap→`WIRE_LIMIT_EXCEEDED`; `set_*` on an inbound handle→`INVALID_HANDLE`; destroy idempotent + NULL-safe; **outbound-tombstone seam (FR-009a)** with the arena ACTUALLY reclaimed, BOTH orderings — (a) `close`→`set_*`, (b) `fixpp_engine_destroy` WITHOUT prior `close`→`set_*` (the discriminating path that frees `session_arena_`) → `INVALID_HANDLE` via the lazy weak-ptr token, under **ASan AND TSan**; commit→send→immediate-`destroy` ASan seam (Codex #4).
- [ ] T009 [US2] Implement CA-009 in `src/capi/message_write.cpp` (per contracts/message-write.md + data-model E-3 emission order / E-9 token): outbound in-arena ordered field accumulator + `fixpp_msg_set_string/set_bytes/set_int/set_double/set_decimal` (deep-copy borrowed buffers into the per-message arena, zero global-heap), `fixpp_msg_remove_tag`, `fixpp_msg_commit` (serialise wire-order app-payload, framing-tag-free, over-cap→`WIRE_LIMIT_EXCEEDED`), `fixpp_msg_create_outbound` (construction-time translate per `[arch §5.3]`), `fixpp_msg_destroy` (idempotent/NULL-safe/arena-slot release), `fixpp_msg_clone` (independent owner-controlled copy; `VERSION_MISMATCH` when source version not loaded, FR-009); reentrancy annotations per FR-018; tombstone via the E-9 token.
- [ ] T010 [US2] Append the 10 CA-009 symbols to `tests/abi/golden/fixpp_capi_symbols.txt` (same commit as T009).
- [ ] T011 [US2] Write + pass `tests/capi/msg_clone_cross_strand_test.cpp` ([2i §9] seam #13, SC-006): clone read on strand B byte-matches the source after the source's dispatch window closed on strand A; clone reads are `THREAD_SAFE` (documented runtime/handle-state guarantee, OUTSIDE the per-symbol gate — FR-018); ≤ 1 µs warm-cache clone budget for a ~200 B message.

**Checkpoint**: pure-C outbound construct/commit/send round-trip + clone handoff work.

---

## Phase 5: User Story 3 - Read repeating groups, including nested (Priority: P1)

**Goal**: inbound group read cursors (CA-010-read) over `OffsetTable::group_slices`, per-entry typed reads, nested descent.

**Independent Test**: obtain cursor+count via `fixpp_msg_get_group`, read entry `[0]` and `[count-1]`, `entry_index==count`→`INDEX_OUT_OF_RANGE`, descend a nested group, cursor lifetime bounded by the parent message.

- [ ] T012 [US3] Extend `tests/capi/message_read_test.cpp` (FAIL first): `fixpp_msg_get_group` count; per-entry `string/int/double/decimal` reads at `[0]` and `[count-1]`; `i>=N`→`INDEX_OUT_OF_RANGE`; absent field in entry→`TAG_NOT_FOUND`; flavour mismatch→`TYPE_MISMATCH`; nested descent via `fixpp_group_get_nested_group`; `group_tag` not a `NumInGroup` tag→`TYPE_MISMATCH`; absent group→`TAG_NOT_FOUND` (SC-002).
- [ ] T013 [US3] Extend `src/capi/message_read.cpp`: `fixpp_msg_get_group`, `fixpp_group_get_field_{string,int,double,decimal}`, `fixpp_group_get_nested_group` (cursor aliases parent message; nested cursor bounded by parent cursor lifetime, `[2c §4.7]`/W-007); reentrancy `FIXPP_REQUIRES_SESSION_LOCK`.
- [ ] T014 [US3] Append the 6 CA-010-read symbols to `tests/abi/golden/fixpp_capi_symbols.txt` (same commit as T013).

**Checkpoint**: inbound group walk (incl. one nested level) works pure-C.

---

## Phase 6: User Story 4 - Construct outbound messages containing repeating groups (Priority: P2)

**Goal**: outbound group builder (CA-010-write), incl. nested entry groups under a LIFO close-order contract.

**Independent Test**: `fixpp_msg_group_begin` → add 2 entries → `fixpp_entry_set_*` → `fixpp_msg_group_end` → commit; serialised payload carries the group with correct `NoXxx` count + per-entry fields; builder invalidated after `group_end`.

- [ ] T015 [US4] Extend `tests/capi/message_write_test.cpp` (FAIL first): `group_begin`→`add_entry`×2→`entry_set_*`→`group_end`→commit, assert correct `NoXxx` count + field order per dictionary grammar; **nested** `fixpp_entry_group_begin` + LIFO close (ending a parent while a younger nested builder is open→`INVALID_HANDLE`); reuse of an ended builder→`INVALID_HANDLE`; **`fixpp_msg_commit` with an open (unended) builder→`INVALID_HANDLE`** (C2); `group_tag` not a group tag→`TYPE_MISMATCH`; group-build on an inbound message→`INVALID_HANDLE` (FR-007).
- [ ] T016 [US4] Extend `src/capi/message_write.cpp`: `fixpp_msg_group_begin`, `fixpp_group_builder_add_entry`, `fixpp_entry_set_{string,int,double,decimal}`, `fixpp_entry_group_begin` (nested, FR-012), `fixpp_msg_group_end`; builder + entry handle invalidation at `group_end`; LIFO close-order enforcement.
- [ ] T017 [US4] Append the 8 CA-010-build symbols to `tests/abi/golden/fixpp_capi_symbols.txt` (same commit as T016).

**Checkpoint**: outbound group origination (flat + nested) works pure-C.

---

## Phase 7: User Story 6 - Inspect/veto outbound messages via a registered toApp callback (Priority: P2)

> **Ordered before US5**: US5's 5-arm witness needs the `app_do_not_send`/`app_callback_threw` arms, which only the US6 toApp hook makes reachable. Both P2; this ordering is free.

**Goal**: the outbound mirror of Feature-B's `fromApp` — a pre-start `toApp` callback registration routed through a new `CapiApplication::toApp` override; closed `fixpp_toapp_verdict` enum; framed read-only view in the callback.

**Independent Test**: register toApp cb; SEND verdict→transmitted+OK; VETO→`APP_DO_NOT_SEND`+nothing sent; ERROR→`APP_CALLBACK_THREW`; inside the callback the outbound msg is readable (US1 accessors).

- [ ] T018 [US6] Header surface in `include/fix/c_api/session.h`: `fixpp_session_register_send_callback(session, cb, userdata)` decl + the **closed** `fixpp_toapp_verdict` enum (`FIXPP_TOAPP_SEND=0`/`FIXPP_TOAPP_VETO=1`/`FIXPP_TOAPP_ERROR=2`, NOT an alias of `fixpp_error_t`) + the callback typedef (FR-022/FR-023).
- [ ] T019 [US6] Write `tests/capi/toapp_callback_test.cpp` (FAIL first): `FIXPP_TOAPP_SEND`→transmit + `fixpp_session_send`==`OK`; `FIXPP_TOAPP_VETO`→`APP_DO_NOT_SEND`, nothing transmitted; `FIXPP_TOAPP_ERROR` **and an out-of-range verdict**→`APP_CALLBACK_THREW` (terminal-close); inside the callback the outbound message is a **FRAMED read-only view** — `8/9/34/49/52/56/10` ARE readable at their wire positions (FR-024, distinct from the accumulator's framing-forbidden read).
- [ ] T020 [US6] Implement the trampoline (per contracts/toapp-callback.md + data-model E-6): `src/capi/session.cpp` `fixpp_session_register_send_callback` (pre-start, populate the `SessionSlot.send_cb` slot) + `src/capi/engine.cpp` `CapiApplication::toApp` override mapping the verdict → `expected_t` (`{}` / `unexpected(app_do_not_send)` / `unexpected(app_callback_threw)`; out-of-range → `app_callback_threw`), exposing the framed read-only `fixpp_msg_t`; reentrancy `FIXPP_REQUIRES_SESSION_LOCK` (runs on the session strand); no global-heap alloc — **alloc-guarded under the same mallocnesia + counting-resource dual gate as T005/T008** (SC-003 third zero-heap path, `[[feedback_tracking_pmr_resource_false_pass]]`).
- [ ] T021 [US6] Append `fixpp_session_register_send_callback` (the 33rd symbol) to `tests/abi/golden/fixpp_capi_symbols.txt` (verdict enum + callback typedef are NOT exported; same commit as T020).

**Checkpoint**: toApp veto/error live; all 5 session/app arms now reachable.

---

## Phase 8: User Story 5 - Session/app failures surface as published, stable C-ABI codes (Priority: P2)

> Witness-only — the error-block implementation landed atomically in Foundational T004 (FR-015 one-pass). This phase proves it end-to-end.

**Goal**: confirm the 5 reachable `session_*`/`app_*` arms return published 1400–1404 codes (not `UNKNOWN`), each with `fixpp_strerror`, and the per-code minor-gated downgrade. Discharges L-050-4 + L-049-2 (session/app arms).

**Independent Test**: drive all 5 arms → published 1400–1404; each has a non-empty `fixpp_strerror`; per-code minor downgrade witnessed BOTH ways at `consumer_minor=3`.

- [ ] T022 [US5] Extend `tests/capi/error_block_test.cpp` (created RED in T004) with the live end-to-end witness (SC-004): all 5 arms — `session_invalid_argument` / `session_invalid_state_for_send` / `app_payload_malformed` (direct send path), `app_do_not_send` / `app_callback_threw` (via the US6 toApp callback) — return their published `FIXPP_ERR_SESSION_*`/`FIXPP_ERR_APP_*` code, not `UNKNOWN`; `1405` framing reject (from the US2 `set_*` path); `fixpp_strerror` non-empty for each of the 6; **per-code minor downgrade at `consumer_minor=3` BOTH ways** — a NEW minor-4 code → `UNKNOWN` AND an EXISTING minor-2 code (`FIXPP_ERR_DICT_CONFIG`) SURVIVES (the per-code-table witness a scalar bump would break, FR-017).

**Checkpoint**: 5 session/app arms legible; forward-compat downgrade witnessed.

---

## Phase 9: Polish & Cross-Cutting Concerns

- [ ] T023 [P] Validate `quickstart.md` — the pure-C read+write round-trip walkthrough compiles + runs against loopback.
- [ ] T024 [P] Finalise the B&L rows (`spec/behaviors-and-limitations.md`): B-051-1..4 + L-051-1 (log/otel arms stay `UNKNOWN`), per `[[project_behaviors_limitations_catalogue]]`.
- [ ] T025 Run the ABI gates green: nm symbol-golden (exactly **33** new exported symbols, the single source of truth), `abidiff` reports **additive** (no breaking re-definition, SC-005), `check_capi_occupancy.sh`, `check_capi_reentrancy.sh` (0 unannotated symbols, FR-018).

### Mandatory close-out tasks (ALWAYS emit — Gate-B preconditions, Article XVII §8)

- [ ] T026 [P] **Catalogue close-out**: flip `CA-008` / `CA-009` / `CA-010` in `spec/feature-catalogue.md` from `backlog` → `done` (with the PR / evidence ref) AND add/update their `spec/coverage-index.md` entries.
- [ ] T027 **Feature-completeness audit (FINAL task)**: assert against the merged tree that (i) every `tasks.md` row is `[X]` or carries an explicit waiver; (ii) every spec FR-001..024 and SC-001..006 maps to a landed test AND a landed implementation; (iii) `CA-008/009/010` are `done` with matching `coverage-index.md` entries. Record the verdict (100% or fully-waived) in `.specify/decisions/051-c-abi-message-accessors-verify.md` (`## Completeness`). HARD `/gate-b` precondition (pre-flight 4d).

---

## Dependencies & Execution Order

### Phase dependencies

- **Setup (P1)**: none.
- **Foundational (P2)**: depends on Setup. T003 (handle plumbing) + T004 (complete error block, atomic) BLOCK all stories. US2/US6 need T003 (accumulator/tombstone token + send_cb slot); US2 needs 1405 (T004); US5/US6 need the 1400–1404 codes (T004).
- **US1 (P1)**: after Foundational. Independent.
- **US2 (P1)**: after Foundational (T003 tombstone token, T004 1405). Independent of US1.
- **US3 (P1)**: after US1 (extends `message_read.cpp` / `message_read_test.cpp`).
- **US4 (P2)**: after US2 (extends `message_write.cpp` / `message_write_test.cpp`).
- **US6 (P2)**: after Foundational (T003 send_cb slot, T004 APP codes). Lands before US5.
- **US5 (P2)**: after US6 (its 5-arm witness needs the toApp arms live) + US2 (the 1405 framing reject path).
- **Polish (P9)**: after all stories.

### Within each story

TDD: the `*_test.cpp` task is written and FAILS before its implementation task. Golden-append lands in the SAME commit as its implementation task (the nm gate compares exported ↔ golden).

### Parallel opportunities

- T001 ∥ T002 (Setup).
- Once Foundational lands: US1, US2 can proceed in parallel (different files). US3 follows US1; US4 follows US2.
- Golden-append edits (T007/T010/T014/T017/T021) all edit the same file — NOT parallel with each other; serialise (each rides its impl task's commit).

---

## Implementation Strategy

### MVP (US1 + US2 + US3 — the P1 round-trip)

1. Setup + Foundational.
2. US1 (inbound scalar read) → US2 (outbound construct/commit/send + clone) → US3 (inbound group read). **STOP & VALIDATE**: pure-C read+write round-trip over loopback (SC-001/SC-002/SC-003/SC-006).

### Incremental (P2)

3. US4 (outbound groups) → US6 (toApp hook) → US5 (error-block witness — joint with US6 proves all 5 arms, SC-004).

### Notes

- Each implementer brief MUST point the subagent at the specific `contracts/<file>.md` section + the data-model entry (E-3 emission order, E-9 token, E-6 verdict) — tasks.md defers signatures to the contracts on purpose.
- Build caps (`[[feedback_build_resource_cap_oom]]`): max `-j2`; sanitizer presets + the verify matrix ONE AT A TIME (WSL2 OOM).
- Steady-state thunks abort-on-escape; only `create_outbound`/`clone` translate (`[2i §5.2]`).
- **Article VIII §1/§3 (perf benchmarks) exemption (analyze B1):** this is an additive thin-thunk C-ABI layer; the read/set p99 budgets (`[2i §4.6]` ≤50 ns read / ≤100–200 ns set, ≤10 ns delta over the C++ accessor) are tracked at the contract level over the already-benched C++ `MessageView`/arena primitives — no separate `bench/capi/` target, matching the merged 049/050 precedent (neither added a capi bench). The ≤1 µs clone budget IS witnessed as a correctness seam (T011/SC-006). Article VIII §5 (zero-alloc/exception-free) is gated by the SC-003 alloc guards + abort-on-escape.
- Re-index CodeGraph (`codegraph sync`) after each code-changing task per the project CLAUDE.md.
