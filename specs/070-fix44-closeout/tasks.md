# Tasks: FIX 4.4 closeout — session-negotiation fields + XMLnonFIX passthrough

**Feature**: `070-fix44-closeout` · **Branch**: `070-fix44-closeout`
**Design**: [plan.md](./plan.md) · [spec.md](./spec.md) · [data-model.md](./data-model.md) · [contracts/session-config-and-logon.md](./contracts/session-config-and-logon.md)

TDD is required (FR-014): every story writes a discriminating, red-provable test BEFORE the implementation, and proves it red against pre-change code, then green after.

**Invariant across all tasks**: with none of the new config set, engine behavior is byte-for-byte and disposition-for-disposition identical to baseline (FR-012 / SC-006). No C-ABI change (FR-013).

## Format: `[ID] [P?] [Story] Description`

## Path Conventions
Single C++23 library `fixpp`. Source: `include/fixpp/`, `src/`. Tests: `tests/session/`.

---

## Phase 1: Setup (Shared Infrastructure)

- [X] T001 Register the four new test targets in `tests/session/CMakeLists.txt` (`test_070_posture_mismatch_test`, `test_070_max_message_size_test`, `test_070_supported_msgtypes_test`, `test_070_xmlnonfix_passthrough_test`) following the feature-068 whole-binary grouping convention (group with the SAFE session tests, select by `ctest -L session`; do NOT create standalone binaries).

---

## Phase 2: Foundational (Blocking Prerequisites — BLOCKS all user stories)

- [X] T002 [P] Add shared types to `include/fixpp/session/session_config.hpp` (before the closing `};`, preserving the copy-constructible `static_assert`): `enum class session_posture { production, test };`, `enum class msg_direction { send, receive };`, and `struct supported_msg_type { msg_direction direction; std::string msg_type; };`. No behavior yet; heavily-commented per file convention.
- [X] T003 Add `struct logon_advertise_options { std::optional<std::uint32_t> max_message_size{}; bool test_message_indicator{false}; std::span<const supported_msg_type> supported_msg_types{}; };` to `include/fixpp/session/admin_messages.hpp` and add a trailing `const logon_advertise_options& opts = {}` parameter to the `build_logon` declaration (`:54`). Default `{}` MUST emit nothing new.
- [X] T004 Thread `opts` into the `build_logon` implementation plumbing in `src/session/admin_messages.cpp:79` WITHOUT emission logic yet (that lands per-story), and update the two call sites — initiator emit `src/session/session.cpp:838` and acceptor reply `src/session/session.cpp:2490` — to pass an (empty) `logon_advertise_options`. Verify byte-identical Logon output for empty opts (existing Logon tests stay green).
- [X] T005 [P] Add inbound-scan fields to `src/session/scan_frame_header.hpp`: `FrameHeader += std::string_view test_message_indicator; // 464` and `std::string_view max_message_size; // 383`, plus `case 464:` and `case 383:` in the scan switch (~`:103`), mirroring the existing `case 141`/`789` pattern.

**Checkpoint**: shared types + builder-opts plumbing + scanner fields exist; default behavior byte-identical.

---

## Phase 3: User Story 1 — Refuse test/production posture mismatch (Priority: P1) 🎯 MVP

**Goal**: an inbound Logon whose 464 posture conflicts with the local posture is refused (Logout+disconnect), never reaching established. Opt-in; default off.

**Independent test**: production-posture session + inbound `464=Y` ⇒ refused; test-posture + `464=N`/absent ⇒ refused; matched ⇒ proceeds; default (unset) ⇒ unchanged.

### Tests for User Story 1 (write first, prove RED)
- [X] T006 [P] [US1] Write `tests/session/test_070_posture_mismatch_test.cpp`: (a) production-posture + inbound `464=Y` ⇒ Logout emitted + Disconnected, not Active; (b) test-posture + `464=N` AND absent ⇒ refused; (c) production-posture + `464=N`/absent ⇒ Active (no false reject); (d) default unset ⇒ baseline (no new path); (e) malformed `464=foo` ⇒ refused-as-malformed. Prove RED against current code.

### Implementation for User Story 1
- [X] T007 [US1] Add `std::optional<session_posture> session_posture_cfg;` (name per data-model) config field to `include/fixpp/session/session_config.hpp` with the symmetric-rule doc comment.
- [X] T008 [US1] In `src/session/session.cpp`, after `interpret_logon` success + header scan and BEFORE the acceptor reply build (~`:2490`) / initiator establishment, add the posture check: validate `464` value ∈ {Y,N} (else malformed-refuse); compute `peer_is_test = (464=="Y")`; if enforcement enabled and `peer_is_test != (cfg posture==test)`, refuse via the Logon-time Logout+disconnect disposition (mirror `session.cpp:2676-2702`) with a distinct posture-mismatch reason. Empty/absent 464 ⇒ production.
- [X] T009 [US1] Emit `464=Y` in the outbound Logon when local `posture==test`: set `opts.test_message_indicator` at the two call sites and emit `464=Y` in `build_logon` (`admin_messages.cpp`) in the correct Logon-body position, when the flag is set.
- [X] T010 [US1] Run `test_070_posture_mismatch_test` GREEN; run the full existing `ctest -L session` suite to confirm no regression (FR-012).

**Checkpoint**: US1 independently testable and green.

---

## Phase 4: User Story 2 — Honor negotiated MaxMessageSize(383) (Priority: P2)

**Goal**: advertise our `383`; capture peer's; once established, disconnect a peer whose frame exceeds `min(N, max_frame_bytes)`. Framer backstop unchanged.

**Independent test**: advertise `383=N` on the wire; established peer frame of N accepted, N+1 disconnects; default unset ⇒ no 383, no enforcement.

### Tests for User Story 2 (write first, prove RED)
- [X] T011 [P] [US2] Write `tests/session/test_070_max_message_size_test.cpp`: (a) advertised config ⇒ outbound Logon carries `383=N` (assert on sent wire via `extract_field`); (b) established session, inbound frame length == N accepted, N+1 ⇒ Disconnected with the negotiated-max reason; (c) a pre-establishment oversized frame is NOT disconnected on the negotiated rule (only the framer backstop governs); (d) default unset ⇒ no `383`, no enforcement. Prove RED.

### Implementation for User Story 2
- [X] T012 [US2] Add `std::optional<std::uint32_t> advertised_max_message_size;` config field to `include/fixpp/session/session_config.hpp` (doc: negotiated inbound cap; effective = min(N, max_frame_bytes)).
- [X] T013 [US2] Emit `383=<N>` in `build_logon` (`admin_messages.cpp`) when `opts.max_message_size` set; set it at the two call sites from config. Capture the peer's advertised `383` (from `FrameHeader.max_message_size`) into a new session-state field for observability (FR-007).
- [X] T014 [US2] Add the established-state inbound enforcement in `src/session/session.cpp` `on_inbound_frame` (~`:1961`): when the FSM is established/Active AND `advertised_max_message_size` is set, if `frame.size() > min(N, max_frame_bytes)` ⇒ `record_state_transition_(Disconnected)` with a distinct "negotiated max message size exceeded" reason. Never fires pre-establishment; never weakens the framer backstop.
- [X] T015 [US2] Run `test_070_max_message_size_test` GREEN; full `ctest -L session` no regression.

**Checkpoint**: US2 independently testable and green.

---

## Phase 5: User Story 3 — Advertise NoMsgTypes(384) in Logon (Priority: P2)

**Goal**: emit `384=k` + contiguous `(372,385)` pairs from explicit config; typed `msg_direction`⇒S/R; fail-closed; default empty ⇒ no group.

**Independent test**: configured list ⇒ well-formed `384` group in the sent Logon, parse-back equals config in order; empty ⇒ no group.

### Tests for User Story 3 (write first, prove RED)
- [X] T016 [P] [US3] Write `tests/session/test_070_supported_msgtypes_test.cpp`: (a) configured `[(send,"D"),(receive,"8")]` ⇒ outbound Logon carries `384=2` then contiguous `372=D,385=S,372=8,385=R` (RefMsgType-then-MsgDirection, delimiter order), parse-back exact-set-equal in order; (b) empty ⇒ no `384`; (c) bounded-buffer fail-closed for a large list (assert error, no partial/garbage frame). Prove RED.

### Implementation for User Story 3
- [X] T017 [US3] Add `std::vector<supported_msg_type> supported_msg_types;` config field to `include/fixpp/session/session_config.hpp` (default empty; keep copy-constructible static_assert green).
- [X] T018 [US3] Emit the NoMsgTypes group in `build_logon` (`admin_messages.cpp`) when `opts.supported_msg_types` non-empty: append `384=k`, then per entry `372=<msg_type>` then `385=<S|R>` (render `msg_direction`), contiguous, in config order, via the existing bound-checked `append_raw` (fail-closed on overflow → `std::unexpected`, no heap), mirroring the 553/554/789 append pattern (`admin_messages.cpp:174-207`). Wire it at the two call sites from config.
- [X] T019 [US3] Run `test_070_supported_msgtypes_test` GREEN; full `ctest -L session` no regression.

**Checkpoint**: US3 independently testable and green.

---

## Phase 6: User Story 4 — XMLnonFIX(35=n) passthrough witness (Priority: P3)

**Goal**: pin that an inbound 35=n with embedded-SOH XmlData(213) is delivered on `fromApp` byte-exact (not admin, not rejected), incl. under enabled dictionary validation. No src change (already works).

**Independent test**: inbound 35=n with `213` containing SOH ⇒ `fromApp`, tag 213 byte-exact; validator-on ⇒ accepted.

### Tests for User Story 4 (write first, prove they exercise the real path)
- [X] T020 [P] [US4] Write `tests/session/test_070_xmlnonfix_passthrough_test.cpp`: (a) inbound `35=n` with `212=len`/`213=<xml containing 0x01 SOH>` ⇒ delivered on the `fromApp` (application) callback, NOT `fromAdmin`, NOT rejected; tag 213 reads back byte-exact incl. embedded SOH; (b) with `validate_inbound_messages=true` ⇒ still accepted (FR-011), not rejected. Confirm the test genuinely exercises delivery (mutation-check: it must fail if 35=n were mis-routed/rejected).
- [X] T021 [US4] Run `test_070_xmlnonfix_passthrough_test` GREEN. (No source change expected; if the validator-on case is red, add the minimal validator allowance and record it — but per Gate A, 212/213 are FIX44 header fields so acceptance is expected.)

**Checkpoint**: US4 independently testable and green.

---

## Phase 7: Polish & Cross-Cutting Concerns

- [X] T022 [P] Confirm no new alloc on the build_logon hot path (the opts struct is read-only; the 384 loop uses append_raw only) and that SessionConfig remains copy-constructible (static_assert green). Update `quickstart.md` if any config name drifted from the plan.
- [X] T023 Run the full local verification the feature warrants (clang debug + a sanitizer leg over `ctest -L session`) to confirm the four capabilities + zero regression before `/speckit-verify`.

### Mandatory close-out tasks (ALWAYS emit — Gate-B preconditions, Article XVII §8)
- [X] T024 Catalogue close-out: flip `spec/feature-catalogue.md` rows S-029, S-030, S-037, A-034 → `done` (feature `070-fix44-closeout`, PR #, tests), and add/point matching `spec/coverage-index.md` entries.
- [X] T025 **(FINAL)** Feature-completeness audit: verify every `tasks.md` row is `[X]` or explicitly waived; every FR-/SC- maps to a landed test AND landed implementation; every feature-owned OFFICIAL catalogue row is `done` with a coverage-index entry. Record the verdict (100% or fully-waived) in `.specify/decisions/070-fix44-closeout-verify.md` (`## Completeness`) — a hard `/gate-b` precondition.

---

## Dependencies & Execution Order

- **Phase 1 Setup** → no deps.
- **Phase 2 Foundational (T002–T005)** → depends on Setup; **BLOCKS all user stories** (shared types, builder-opts plumbing, scanner fields).
- **US1 (T006–T010)**, **US2 (T011–T015)**, **US3 (T016–T019)**, **US4 (T020–T021)** → each depends only on Foundational; independently testable. US1/US2/US3 each add emission to the same `build_logon` (sequential edits, same file — not `[P]` across stories at the impl level, though their tests are `[P]`).
- **Polish (T022–T025)** → depends on all desired stories complete. T025 is the FINAL task.

### MVP
US1 (S-029 posture reject) alone is a shippable MVP — the highest-value safety property.

### Parallel opportunities
Test-authoring tasks T006/T011/T016/T020 are `[P]` (distinct files). Foundational T002/T005 are `[P]` (distinct files). Story implementations touching `build_logon`/`session.cpp` are sequential.
