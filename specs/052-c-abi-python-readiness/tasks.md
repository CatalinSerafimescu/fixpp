---
description: "Task list for 052-c-abi-python-readiness implementation"
---

# Tasks: C-ABI Python-readiness — dictionary loader, transport-endpoint config, inbound field iteration

**Input**: Design documents from `specs/052-c-abi-python-readiness/`
**Prerequisites**: plan.md, spec.md, research.md (D-1..D-6), data-model.md (E-1..E-4), contracts/ (×3), quickstart.md
**Branch**: `052-c-abi-python-readiness` | **ABI**: additive MINOR 0.4.0 → 0.5.0 (7 new symbols + 1 PoD + 1 enum)

**Tests**: REQUIRED (Constitution §VII.3 + FR-012 names the test seams + SC-001..006 are witness-defined; TDD per the 049/050/051 C-ABI convention). Write each story's test FIRST and confirm it FAILS before implementing.

**Organization**: by user story. US1 (dict loader, P1) and US2 (endpoint + reset-policy, P1) together stand up the SC-001 public round-trip (US2's round-trip witness depends on US1's loader). US3 (field iteration, P2) is independent.

**Build discipline** (`[[feedback_build_resource_cap_oom]]`): max `-j2`; sanitizer presets + the verify matrix run strictly ONE AT A TIME (WSL2 OOM). All paths are relative to the library submodule root.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: can run in parallel (different files, no dependency on an incomplete task)
- **[Story]**: US1 / US2 / US3 (user-story phase tasks only)

---

## Phase 1: Setup (Shared grounding)

**Purpose**: anchor the implementation to the as-built patterns before writing code (the build "makes the L-050-1/L-050-5 seams real").

- [ ] T001 [P] Source-grounding read: `tests/capi/capi_loopback_support.hpp` (L-050-1 dict seam `make_test_dict_handle`/`destroy_test_dict_handle`, L-050-5 endpoint seam), `src/capi/capi_internal.hpp` (the `fixpp_dict` struct, the clone-flavour `fixpp_msg`, the Index-mode inbound view ~:227), `src/capi/engine.cpp` (`fixpp_engine_destroy` tombstone `tag_=FIXPP_HANDLE_TAG_DEAD` + retained `s_dead_shells` registry), `include/fixpp/wire/offset_table.hpp` (`entries()` multiset / `find()` first-occurrence), `include/fixpp/session/session_config.hpp:92-96` (the `reset_seqnum_policy` enum) — confirm every anchor before implementing.

---

## Phase 2: Foundational (Blocking prerequisite)

**Purpose**: the version all 7 new symbols ship under. Must precede the ABI golden/abidiff gates.

- [ ] T002 Bump `FIXPP_C_ABI_VERSION_MINOR` 4 → 5 in `include/fix/c_api/version.h` (FR-009 / SC-006; MAJOR stays 0, additive).

**Checkpoint**: foundation ready — the three surfaces can proceed in parallel.

---

## Phase 3: User Story 1 — Load a real FIX dictionary from pure C (Priority: P1) 🎯 MVP

**Goal**: a pure-C consumer obtains the `fixpp_dict_t` that `fixpp_session_config_set_dictionary` requires, by loading a bundled FIX XML from a path, and releases it safely.

**Independent Test**: pure-C `fixpp_dict_load_from_xml("dictionaries/FIX44.xml", &dict)` → OK + non-null owning handle, usable in `set_dictionary`; missing/malformed path → `CAPI_CONFIG_INVALID` + non-empty `fixpp_strerror` + no abort; NULL path/out → `NULL_HANDLE`; double-destroy on the same handle + NULL-destroy → safe no-op; no C++ header linked.

### Tests for US1 (write FIRST, confirm RED)

- [ ] T003 [P] [US1] Write `tests/capi/dictionary_load_test.cpp` (pure public headers only): load each bundled `dictionaries/FIX{42,44,50SP2,T11}.xml` → OK + usable in `fixpp_session_config_set_dictionary`; missing path + a syntactically-malformed XML → `CAPI_CONFIG_INVALID` + non-empty `fixpp_strerror` + no abort; NULL path / NULL out → `NULL_HANDLE` + `*out=NULL`; sequential double-destroy on the same handle + `fixpp_dict_destroy(NULL)` → safe no-op (SC-004 / US1 AC1-4).

### Implementation for US1

- [ ] T004 [P] [US1] Create the new public header `include/fix/c_api/dict.h` (`[2i]`-reserved name): `#ifndef FIXPP_C_API_DICT_H` guard, `extern "C"`, includes `<fix/c_api/error.h>`/`export.h`/`handles.h`; declare `fixpp_error_t fixpp_dict_load_from_xml(const char* path, fixpp_dict_t** out_dict)` (`FIXPP_SINGLE_THREAD`) + `void fixpp_dict_destroy(fixpp_dict_t* dict)` (`FIXPP_THREAD_SAFE`) with the reentrancy annotations + the doc-comments from `contracts/dictionary-loader.md`; note the header coexists with the reserved 2c `FIXPP_APPL_VER_*` constants (FR-001/FR-002/FR-008).
- [ ] T005 [US1] Add `#include <fix/c_api/dict.h>` to the umbrella `include/fix/c_api.h` so `<fix/c_api.h>` exposes the dict symbols (FR-014); do NOT propagate the pre-existing stale `FIXPP_VERSION_*`/"0.2.0" comment in that file. (Depends on T004.)
- [ ] T006 [P] [US1] Add a `tag_` liveness token as the FIRST member of the `fixpp_dict` struct in `src/capi/capi_internal.hpp` (fixed-offset type-tag-before-deref; E-1 tombstone).
- [ ] T007 [US1] Create `src/capi/dictionary.cpp`: `fixpp_dict_load_from_xml` as a construction-time thunk (`guarded_call_construction` → `CAPI_CONFIG_INVALID` on `XmlLoader` throws) wrapping `fixpp::dict::XmlLoader::load(std::filesystem::path{path}, std::pmr::get_default_resource())` into `new fixpp_dict{ live tag_, make_shared<const Dictionary>(...) }`; NULL path/out → `NULL_HANDLE`; `*out_dict=NULL` on every failure path (FR-001/FR-003/FR-011). `fixpp_dict_destroy`: a process-global mutex covers the ENTIRE critical section as one atomic unit — `{ check tag_ != DEAD → release the shared_ptr (dict.reset()) → rewrite tag_ = FIXPP_HANDLE_TAG_DEAD → insert the shell into the bounded dead-shell registry }`; NULL-safe; never throws; second same-pointer destroy sees `tag_==DEAD` under the lock and no-ops (FR-002/FR-008). (Depends on T004, T006.)
- [ ] T008 [US1] Append `fixpp_dict_load_from_xml` + `fixpp_dict_destroy` to `tests/abi/golden/fixpp_capi_symbols.txt` and their 2 reentrancy entries to the `tools/check_capi_reentrancy.sh` expected list (FR-008/FR-009).
- [ ] T009 [US1] Add a sanitizer-gated TSan concurrent-double-destroy witness on the same dict handle to `tests/capi/dictionary_load_test.cpp` (the full-critical-section lock contract; SC-004) — race-free under TSan.
- [ ] T010 [US1] Build (`-j2`) + run `dictionary_load_test` → GREEN; confirm the `CAPI_CONFIG_INVALID` path takes no abort, the ABI golden + reentrancy gates pass with the 2 new symbols.

**Checkpoint**: US1 fully functional — a pure-C consumer can construct + release a dictionary.

---

## Phase 4: User Story 2 — Configure where a session connects (+ reset-policy) (Priority: P1)

**Goal**: a pure-C initiator sets its peer `host:port` and an acceptor reads back its OS-assigned port; the reset-seqnum policy is settable; together with US1 this stands up the first fully-public-C-ABI live round-trip.

**Independent Test**: the setters' error paths (`NULL cfg/host` → `NULL_HANDLE`; empty host / out-of-range enum → `CAPI_CONFIG_INVALID`) need no dictionary; the full SC-001 two-engine round-trip depends on US1's loader.

### Tests for US2 (write FIRST, confirm RED)

- [ ] T011 [P] [US2] Write `tests/capi/public_roundtrip_test.cpp` (pure public headers, NO seams): SC-001 TWO-engine live round-trip — acceptor on engine A with `set_tcp_endpoint("127.0.0.1", 0)` (port-0) → `fixpp_session_acceptor_bound_endpoint` readback (poll until non-zero); initiator on engine B dialing that port; both `set_reset_on_logon(true)` under the production-default `bilateral_strict`; dictionary via US1's `fixpp_dict_load_from_xml`; pair establishes + exchanges an application message. **D-4 EMPIRICAL-FIRST**: the first assertion is that the fresh `bilateral_strict` + both-side `reset_on_logon(true)` pair ESTABLISHES through the public C-ABI — if it does not, treat it as a real defect and use `fixpp_session_config_set_reset_seqnum_policy(..., LENIENT)` as the witnessed fallback (do NOT waive). Plus: NULL cfg/host → `NULL_HANDLE`; empty host → `CAPI_CONFIG_INVALID`; out-of-range policy enum → `CAPI_CONFIG_INVALID` (SC-001 / US2 AC1-4). Multi-threaded, sanitizer-gated. (Depends on US1.)

### Implementation for US2

- [ ] T012 [P] [US2] Declare in `include/fix/c_api/session.h`: `fixpp_session_config_set_tcp_endpoint(fixpp_session_config_t*, const char* host, uint16_t port)` (`FIXPP_SINGLE_THREAD`), `fixpp_session_acceptor_bound_endpoint(fixpp_session_t*, uint16_t* port_out)` (`FIXPP_THREAD_SAFE`), `fixpp_session_config_set_reset_seqnum_policy(fixpp_session_config_t*, fixpp_reset_seqnum_policy)` (`FIXPP_SINGLE_THREAD`), and the C11 enum `fixpp_reset_seqnum_policy { FIXPP_RESET_SEQNUM_BILATERAL_STRICT=0, FIXPP_RESET_SEQNUM_BILATERAL_LENIENT=1, FIXPP_RESET_SEQNUM_UNILATERAL=2 }` (mirrors `session_config.hpp:92-96`) with doc-comments from `contracts/transport-endpoint.md` (FR-004/005/005b/008).
- [ ] T013 [US2] Implement in `src/capi/config.cpp`: `set_tcp_endpoint` → `cfg->cfg.reconnect_endpoint = Endpoint{host, port}` + set the internal `transport_send` placeholder; NULL cfg/host → `NULL_HANDLE`, empty/unusable host → `CAPI_CONFIG_INVALID` (FR-004). `set_reset_seqnum_policy` → `cfg->cfg.reset_seqnum_policy_field = <mapped enum>`; NULL → `NULL_HANDLE`, out-of-range enum → `CAPI_CONFIG_INVALID` (FR-005b). (Depends on T012.)
- [ ] T014 [US2] Implement `fixpp_session_acceptor_bound_endpoint` in `src/capi/session.cpp`: `*port_out = session->engine->state_->engine_->acceptor_bound_endpoint(session->id).port`; NULL session/out → `NULL_HANDLE`, destroyed session → `INVALID_HANDLE`, not-yet-bound → `*port_out=0` + `OK` (pollable); `THREAD_SAFE`; steady-state thunk → fatal log + `std::abort` on exception escape (FR-005/FR-011). (Depends on T012.)
- [ ] T015 [US2] Append the 3 new symbols to `tests/abi/golden/fixpp_capi_symbols.txt` + their 3 reentrancy entries to `tools/check_capi_reentrancy.sh` (FR-008/FR-009).
- [ ] T016 [US2] (sequence after T012 — same file `session.h`, no `[P]`) Fix the stale `include/fix/c_api/session.h:190` doc comment ("app_payload_malformed → `FIXPP_ERR_UNKNOWN`") → reference the now-published `FIXPP_ERR_APP_PAYLOAD_MALFORMED` (FR-013; comment-only, no behaviour change).
- [ ] T017 [US2] Build (`-j2`) + run `public_roundtrip_test` → GREEN; D-4 establishment confirmed empirically FIRST; app message exchanged through only public headers; multi-threaded harness, sanitizer-gated. (Depends on T010, T013, T014.)

**Checkpoint**: US1 + US2 → the first fully-public-C-ABI live round-trip works (no L-050-1/L-050-5 seams).

---

## Phase 5: User Story 3 — Enumerate inbound message fields (Priority: P2)

**Goal**: a pure-C consumer (or a Pythonic `for tag, value in msg`) iterates the fields of an inbound message by index without knowing tags a priori.

**Independent Test**: inside a receive callback, `field_count` then `field_at` over `[0,count)`; every scalar-getter-readable tag appears with matching first-occurrence bytes (repeated tags → superset); `index==count` → `INDEX_OUT_OF_RANGE`; each `value` aliases the wire buffer; zero global-heap; no C++ header linked.

### Tests for US3 (write FIRST, confirm RED)

- [ ] T018 [P] [US3] Write `tests/capi/message_field_iteration_test.cpp`: inside a receive callback, `fixpp_msg_field_count` then `fixpp_msg_field_at` over `[0,count)`; assert the FR-007 one-directional invariant on a **REPEATING-GROUP message** (every scalar-getter tag appears with matching FIRST-occurrence bytes; a tag repeated across N group instances yields N enumeration entries → superset; SC-002); **assert FR-006 WIRE ORDER directly** (not just multiset membership — a tag-sorted impl must FAIL): for the known fixture, `field_at(msg,0).tag` == the first wire tag, `field_at(msg,1).tag` == the second, and the full `[0,count)` tag sequence equals the document-order tag sequence (mutation-discriminating per `[[feedback_witness_asserts_named_postcondition_not_proxy]]`); `index >= count` → `INDEX_OUT_OF_RANGE`; NULL handle/out → `NULL_HANDLE`; destroyed / type-mismatched (`fixpp_session_t*` as `fixpp_msg_t*`) → `INVALID_HANDLE`; each `field.value` aliases the wire buffer; **zero global-heap** under the mallocnesia LD_PRELOAD + counting-resource dual gate (SC-003); a **clone-iteration cross-strand witness** — iterate a detached 051 clone OFF the session strand after the dispatch window closed (FR-006/007).

### Implementation for US3

- [ ] T019 [P] [US3] Declare in `include/fix/c_api/message.h`: the PoD `typedef struct fixpp_msg_field { uint16_t tag; const uint8_t* value; size_t len; } fixpp_msg_field_t;` + `fixpp_msg_field_count(const fixpp_msg_t*, size_t* count_out)` + `fixpp_msg_field_at(const fixpp_msg_t*, size_t index, fixpp_msg_field_t* field_out)` (`FIXPP_REQUIRES_SESSION_LOCK`) with the doc-comments from `contracts/field-iteration.md` incl. the 051 FR-018 runtime clone-read-`THREAD_SAFE` guarantee (FR-006/008).
- [ ] T020 [US3] Implement `field_count`/`field_at` in `src/capi/message_read.cpp`: resolve the inbound (or clone) Index-mode `MessageView`; `field_count` → `offsets().entries().size()`; `field_at(i)` → `e = entries()[i]` → `field_out = { e.tag, wire_base + e.offset, e.length }` in wire/document order (multiset); type-tag/tombstone check first; `index >= count` → `INDEX_OUT_OF_RANGE`; NULL → `NULL_HANDLE`; steady-state thunk → abort on escape; zero global-heap (alias the wire buffer); works on inbound + clone handles with the handle's own lifetime (FR-006/007/011). (Depends on T019.)
- [ ] T021 [US3] Append `fixpp_msg_field_count` + `fixpp_msg_field_at` to `tests/abi/golden/fixpp_capi_symbols.txt` + their 2 reentrancy entries to `tools/check_capi_reentrancy.sh` (FR-008/FR-009).
- [ ] T022 [US3] Build (`-j2`) + run `message_field_iteration_test` → GREEN; confirm the zero-global-heap dual gate, the repeating-group superset, and the clone cross-strand read. (Depends on T020.)

**Checkpoint**: all three surfaces independently functional.

---

## Phase 6: Polish & Cross-Cutting Concerns

- [ ] T023 [P] Verify the ABI surface gates (SC-005 / FR-010): `tests/abi/golden/fixpp_capi_symbols.txt` carries all 7 new symbols; `abidiff` reports the change **additive** (no breaking re-definition); `tools/check_capi_occupancy.sh` passes **UNCHANGED** (zero new error codes — no `EXPECTED`/`EXPECT_COUNT` edit); `tools/check_capi_reentrancy.sh` passes with all 7 symbols annotated (0 unannotated).
- [ ] T024 [P] Run `quickstart.md` validation: the pure-C two-engine happy-path compiles + runs against ONLY `include/fix/c_api/` headers (the umbrella `<fix/c_api.h>` exposes every new symbol; SC-001).
- [ ] T025 Run the per-PR sanitizer matrix (ASan / UBSan / TSan) over the three new `tests/capi/` tests, ONE preset at a time (`-j2`, WSL2 OOM cap); confirm zero findings incl. the dict double-destroy TSan witness and the iteration zero-heap dual gate.
- [ ] T026 Update the Behaviors & Limitations catalogue `spec/behaviors-and-limitations.md` with B-052-1/B-052-2 + L-052-1/L-052-2/L-052-3 (finalised per `[[project_behaviors_limitations_catalogue]]`).

### Mandatory close-out tasks (ALWAYS emit — Gate-B preconditions, Article XVII §8)

- [ ] T027 [P] **Catalogue close-out**: flip every feature-owned OFFICIAL row in `spec/feature-catalogue.md` to `done` with the PR / evidence ref — the GAP-001 dict-loader rows (CA-001/CA-002 close-out of the `[2i §2]` commitment-2 symbol) + any net-new 052 rows for the endpoint config / reset-policy / field iteration — AND add/update each matching `spec/coverage-index.md` entry.
- [ ] T028 **Feature-completeness audit (FINAL task)**: assert against the merged tree that (i) every `tasks.md` row is `[X]` or carries an explicit waiver rationale; (ii) every spec FR-001..014 (incl. the FR-001a/FR-005a recorded deviations) and SC-001..006 maps to a landed test AND a landed implementation; (iii) every feature-owned OFFICIAL `feature-catalogue.md` row is `done` with a matching `coverage-index.md` entry. Record the verdict (100% or fully-waived) in `.specify/decisions/052-c-abi-python-readiness-verify.md` (`## Completeness` section) OR a sibling `.specify/decisions/052-c-abi-python-readiness-completeness.md`. HARD `/gate-b` pre-flight 4d precondition.

---

## Dependencies & Execution Order

### Phase dependencies

- **Setup (T001)**: read-only grounding — start immediately.
- **Foundational (T002, version bump)**: precedes the golden/abidiff gates; does not block writing the symbol code.
- **US1 (P1)**: after Foundational. The MVP — the precondition for every session operation.
- **US2 (P1)**: setter error-path unit checks are independent of US1; the **SC-001 two-engine round-trip (T011/T017) depends on US1** (needs `fixpp_dict_load_from_xml`).
- **US3 (P2)**: fully independent of US1/US2.
- **Polish (T023-T028)**: after all desired stories; T028 is the final task.

### Within each story

- The test task is written FIRST and must FAIL before implementation (TDD).
- Header decl → struct/impl → golden+reentrancy entries → build+run GREEN.

### Parallel opportunities

- US1 / US3 can be implemented fully in parallel (disjoint files); US2's setters are parallel too, but its SC-001 round-trip test waits on US1.
- Within a story, the `[P]` tasks (test authoring, header decl, the `capi_internal.hpp` struct edit) touch different files and can run together; the impl `.cpp` tasks depend on their header decls.
- T023 / T024 / T027 are `[P]` (different artifacts); T025 (sanitizer matrix) and T028 (audit) are serial.

---

## Parallel Example: US1 + US3 kickoff

```bash
# After T002 (version bump), launch the disjoint-file work together:
Task: "T003 Write tests/capi/dictionary_load_test.cpp (US1, RED)"
Task: "T004 Create include/fix/c_api/dict.h (US1)"
Task: "T006 Add tag_ to fixpp_dict in src/capi/capi_internal.hpp (US1)"
Task: "T018 Write tests/capi/message_field_iteration_test.cpp (US3, RED)"
Task: "T019 Declare fixpp_msg_field_t + count/at in include/fix/c_api/message.h (US3)"
```

---

## Implementation Strategy

### MVP (US1 only)

1. T001 grounding → T002 version bump → US1 (T003-T010) → **STOP & VALIDATE**: a pure-C consumer constructs + releases a real dictionary (the precondition for everything else).

### Incremental delivery

1. Setup + Foundational → US1 (MVP: dictionary) → US2 (live round-trip — the first fully-public-C-ABI session, the headline deliverable) → US3 (field iteration — the Python iteration ergonomic) → Polish.
2. **D-4 gate**: US2's first build (T017) empirically confirms fresh-pair establishment under `bilateral_strict`; the pinned reset-policy setter is the witnessed fallback if it does not (never waive).

## Notes

- `[P]` = different files, no dependency on an incomplete task. `[Story]` maps each task to its user story.
- Build `-j2` max; sanitizer presets + verify matrix ONE AT A TIME (WSL2 OOM).
- No new `fixpp_error_t` codes; `check_capi_occupancy.sh` stays UNCHANGED (a deliberate simplification vs 051).
- `[2i]` is NOT reopened (GAP-002 = a recorded LOCAL Gate-A deviation); `§7.8` transport handles/PoD + `§4.3` error layout stay intact.
- Commit after each task or logical group; the next pipeline step after `/speckit-tasks` is `/speckit-analyze` (MANDATORY — ABI/Security/Threading triggers).
