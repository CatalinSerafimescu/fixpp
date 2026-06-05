---
description: "Task list for 022-possresend-allowpossdup-send"
---

# Tasks: PossResend(97) Inbound + AllowPosDup Send-Path Strip (S-010 completion, G3 slice 2)

**Input**: Design documents from `specs/022-possresend-allowpossdup-send/`
**Prerequisites**: plan.md, spec.md, research.md (D1–D6), data-model.md, contracts/session-send-possdup.md (C1–C6), quickstart.md
**Repository root** = the library submodule (`research/G19-fix-fpml-iso20022/library/`). All paths below are submodule-relative.

**Tests**: REQUIRED and RED-first — `[const §VII]` TDD is binding for this codebase. Every behavior lands as a failing GoogleTest witness before the production change.

**Scope reality** (from plan.md Complexity Tracking + research D4):
- **US1 (P1)** is **witness-only — ZERO production code** (clarify-confirmed D4: neither QFcpp nor QFJ handles `PossResend(97)` session-level; fixpp's `fromApp` already delivers the full `MessageView`). Its tests are parity/characterization witnesses that must PASS against current behavior; **a failing US1 witness falsifies D4 → STOP and escalate** (it would mean real production work is needed, out of this slice's design).
- **US2 (P2)** is the **only production change**: `+1` additive `SessionConfig` field + one fail-closed, boundary-anchored `43`/`122` excision (behind a 022-owned no-heap field scanner) at a single `send_impl` site.

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Build-graph registration for the two new unit suites.

- [X] T001 Register two new GoogleTest executables in `tests/session/CMakeLists.txt`, mirroring the 021 possdup pattern (`session_inbound_poss_dup_tolerance`, `:1356-1374`): `session_inbound_poss_resend` (source `test_inbound_poss_resend.cpp`) and `session_send_allow_pos_dup_strip` (source `test_send_allow_pos_dup_strip.cpp`). Each links `fixpp_session` + `fixpp_mock_clock` + `$<TARGET_OBJECTS:session_test_support>` + `GTest::gtest`/`gtest_main`, includes `${CMAKE_SOURCE_DIR}/tests`, compiles `FIXPP_TEST_HOOKS`, sets the `TSAN_OPTIONS` env + asio suppression, and tags `LABELS "022;us1;possresend"` / `LABELS "022;us2;allowposdup;strip"`. Create empty placeholder `tests/session/test_inbound_poss_resend.cpp` and `tests/session/test_send_allow_pos_dup_strip.cpp` so the build graph configures. → verify: `cmake` configures clean; both targets build (empty).

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: The single additive public-surface field US2 references. US1 needs **no** foundational work and may start immediately.

**⚠️ Note**: This field adds the knob's *value only* (default `false`), with **no strip behavior yet** — so US2's strip-default witnesses are RED on *behavior* (43/122 still emitted), not on a compile error.

- [X] T002 Add the additive POD field `bool allow_pos_dup = false;` to `include/fixpp/session/session_config.hpp` immediately next to `redeliver_poss_dup` (`:293`), with the data-model §1 doc-comment (QFJ `SETTING_ALLOW_POS_DUP_MESSAGES` parity; default-strip; struct-layout/source-rebuild note; FR-007 resend-independence note). No behavior change. (data-model §1; research D1; contract C1) → verify: library builds; `allow_pos_dup` is a public default-`false` member; no other field touched.

**Checkpoint**: Foundation ready — both user stories can proceed.

---

## Phase 3: User Story 1 — Accept & deliver an application possible-resend (Priority: P1) 🎯 MVP

**Goal**: Prove fixpp processes an in-sequence `97=Y` application message normally (seqnum advances), delivers it to `fromApp`, and never session-rejects it for `97` — in both the registered- and no-application cases — **with zero production change** (D4).

**Independent Test**: Feed an established session an in-sequence app message with `97=Y`; assert expected inbound seqnum N→N+1, `fromApp` invoked with tag 97 readable, no `Reject`/`Logout`/disconnect, session `Active`.

### Tests for User Story 1 (RED-first; witness-only — expected to PASS against current behavior)

> Author all four witnesses in the single file `tests/session/test_inbound_poss_resend.cpp` (same file ⇒ not parallel). These are the disposition-table rows in data-model §3 / contract C4.

- [X] T003 [US1] Write the four PossResend witnesses in `tests/session/test_inbound_poss_resend.cpp`: (1) **deliver in-seq `97=Y`** → `fromApp` gets the full frame (tag 97 readable), seqnum N→N+1, no `Reject`/`Logout`/disconnect, session `Active` (C4.1 / FR-001/FR-002); (2) **no-app byte-identity** → with no `Application` registered, `97=Y` handled byte-identically to the same message without `97` (C4.4 / FR-005); (3) **`43=Y`+`97=Y` combo** → the 021 PossDup arms (A–E) fire on `43` only, `97` adds no extra reject (C4.3 / FR-004); (4) **`97=Y` without `122`** → no `Reject(371=122)` (122-required keys on `43=Y` only) (C4.2 / FR-003). Reuse the 021 `session_test_support` `extract_field` + established-session fixtures. **Edge-case coverage (spec.md Edge Cases):** also add a thin witness (or a documented delegation note inline) for (5) **`97=Y` at a too-high seqnum** → the existing too-high/ResendRequest path fires unchanged, `97` has no effect; and (6) **`97=Y` on an admin message** → the existing admin path runs, `97` has no effect. If prior-phase (021/005) too-high/admin tests already pin these byte-for-byte, cite them explicitly here instead of duplicating; do not leave the two edge cases silently uncovered.
- [X] T004 [US1] Build + run `session_inbound_poss_resend`; **confirm all four witnesses PASS** against current behavior — this validates the witness-only claim (D4). **If any witness FAILS, STOP**: D4 is falsified (a real `PossResend` gap exists requiring production code beyond this slice's design) — escalate before proceeding. → verify: `ctest -R session_inbound_poss_resend` green; no `src/`/`include/` change was made for US1.

**Checkpoint**: US1 complete — PossResend inbound disposition proven witness-only, no production delta.

---

## Phase 4: User Story 2 — Strip caller-supplied duplicate flags on a plain send (Priority: P2)

**Goal**: By default, a plain `send` strips caller-supplied `PossDupFlag(43)` / `OrigSendingTime(122)` from the opaque app payload before framing (fail-closed against malformed/injection); `allow_pos_dup=true` retains them; the auto-resend path always re-adds them independently.

**Independent Test**: Default knob → `send` a payload with `43=Y`+`122=<ts>`, capture framed bytes, assert neither tag in the app portion; flip to retain → both pass through; drive a resend → replay re-adds `43=Y`+`122` regardless of the knob.

### Tests for User Story 2 (RED-first)

> All US2 witnesses live in the single file `tests/session/test_send_allow_pos_dup_strip.cpp` (same file ⇒ T005/T006 are sequential, not parallel). They cover the eight quickstart scenarios. After Phase 2 (knob value only, no strip), the behavioral witnesses must be RED.

- [X] T005 [US2] Write the **behavioral** strip witnesses in `tests/session/test_send_allow_pos_dup_strip.cpp`: (1) **strip-default** — default `allow_pos_dup==false`, `send` `35=D\x01…43=Y\x01…122=…\x01…`, capture framed bytes, assert no boundary `43=`/`122=` in the app portion (C2.1 / SC-002); (2) **retain** — `allow_pos_dup=true`, same payload, both survive verbatim (C2.2 / FR-009); (3) **embedded-text `43=` hostile witness** — a field whose *value* contains literal `43=` with NO preceding SOH (e.g. `11=ORD43=Y\x01`) plus a *separate* real boundary `43=Y\x01`; default-strip removes only the real boundary field, leaves `11=ORD43=Y` intact; RED-prove an unanchored strip would corrupt `11=` (C2.3a / INV-2 / [[feedback_delimiter_injection_verbatim_field_copy]]); (4) **true SOH-boundary excise** — genuine `…\x0143=Y\x01` + `…\x01122=…\x01`: removed under default, retained under `true` (C2.3b); (5) **no-op-when-absent** — payload with no `43`/`122` is byte-identical under both knob settings (C2.5).
- [X] T006 [US2] Write the **fail-closed + invariant** witnesses in the same file: (6) **malformed-field → 131**, parameterized over cases that pass today's 020 floor but fail per-field grammar — `35=D\x0111BROKEN\x0143=Y\x01` (missing `=`), `35=D\x01=bad\x01122=x\x01` (empty tag), `35=D\x014a=x\x0143=Y\x01` (non-digit tag), `35=D\x01\x0143=Y\x01` (empty/zero-length field): assert `app_payload_malformed=131`, **outbound seqnum NOT consumed, nothing transmitted, NO excision** (C2.4 / D2 / [[feedback_conjunctive_parse_guard_tolerates_malformed_field]]); (7) **resend independence (FR-007)** — `allow_pos_dup=false`, `send` a payload that **contained** `43`/`122` (strip provably removed them from the stored frame), then drive a ResendRequest reply/replay and assert `build_replay_frame` still emits `43=Y`+`122` (C3 — the original send MUST carry 43/122 or the witness passes trivially); (8) **no-heap** — a dedicated `send_impl`-strip no-heap cell **inside `test_send_allow_pos_dup_strip.cpp`** asserts zero global-heap allocation across the strip (scanner + excision + copy-to-scratch), verified under the **mallocnesia LD_PRELOAD** interceptor (the binding gate per [[feedback_tracking_pmr_resource_false_pass]] — counting_resource alone is a false-pass). **NOT** `test_session_alloc_guard` — that binary covers only the 005-era seqnum + admin-build paths and never exercises `send_impl` (C2.6 / INV-4). Do NOT add a "no final SOH" case (the 020 floor already rejects it at `session.cpp:2951` — it would measure the floor, not the 022 scanner).
- [X] T007 [US2] Build + run `session_send_allow_pos_dup_strip`; **confirm RED** — behavioral witnesses fail (knob value exists but no strip), malformed-field witnesses fail (no 022 scanner yet → the 020 denylist admits all four). → verify: the failures are *behavioral*, not compile errors — assert the specific RED signals: a boundary `43=Y` (and `122=`) still appears in the captured outbound bytes (strip absent), and the malformed-field cases return success / do NOT return `app_payload_malformed=131` (scanner absent). If the suite fails to *compile* instead, fix the fixture wiring before declaring RED.

### Implementation for User Story 2

> Single production site: `Session::send_impl` in `src/session/session.cpp`, **after** the 020 opaque-payload validation (`~:2932-2990`) and **before** seqnum peek / SendingTime stamp / frame build (`~:3034`). `has_boundary_token` (`:2917`) is the boundary-rule **precedent only** (bool-only) — NOT the excision primitive. `build_replay_frame` (`:1186-1239`) stays UNCHANGED. Sequential (same file, T009 depends on T008's spans).

- [X] T008 [US2] Implement the **022-owned no-heap field scanner** in `Session::send_impl` (`src/session/session.cpp`), inserted after the 020 validation / before seqnum/stamp/frame: walk each SOH-terminated field after the leading `35=…\x01`, validating each is `<non-empty digit-only tag>=<value>\x01` (required `=`, non-empty digit-only tag, non-empty field). On the FIRST malformed field, return `app_payload_malformed=131` **before** any seqnum peek/assign, SendingTime stamp, excision, or transmit. The scanner yields each field's span (offset+length) for the excision pass. No heap. (research D2/D5; data-model §2 scanner pass; contract C2.4) → verify: T006 malformed-field witnesses go GREEN (131, no seqnum/transmit/excision).
- [X] T009 [US2] Implement the **boundary-anchored excision** over the scanner-validated spans in the same `send_impl` site: when `!cfg_.allow_pos_dup`, excise the **complete** `43=…\x01` and `122=…\x01` boundary fields (span-bounded by the scanner) by copying the surviving fields, in original order, into ONE bounded named **strip stack scratch** (sized to the validated payload, ≤ ~3800B; oversized → the existing `wire_frame_too_large` disposition the framer's `body_buf` already owns); when `allow_pos_dup`, pass through verbatim. `35=` (field 0) is never touched (INV-1); a literal `43=`/`122=` inside a value is never matched (INV-2); no-op when absent (C2.5); the stripped payload is then handed to the existing framer (INV-3). No heap (INV-4 / D6). (data-model §2 transform + invariants; contract C2.1/2.2/2.3/2.5/2.6) → verify: T005 behavioral witnesses GREEN.
- [X] T010 [US2] Build + run `session_send_allow_pos_dup_strip` GREEN (all eight scenarios); confirm no-heap under the mallocnesia LD_PRELOAD gate; run the §XV.9 awaitable-include watch-item check (UNFILTERED Tier-1 or `-L sync`) to confirm the `session_config.hpp` field added no `std::mutex` into the `session.hpp` awaitable closure. → verify: full US2 suite green; mallocnesia clean; no `sync`-corpus regression.

**Checkpoint**: US1 + US2 both independently functional; S-010 behaviors delivered.

---

## Phase 5: Polish & Cross-Cutting Concerns

**Purpose**: Live interop proof (SC-005), the binding local verify mirror, and the catalogue/coverage/docs close-out (applied at Polish per the 020/021 precedent).

- [X] T011 [P] Add the live **AllowPosDup wire-capture** interop cell extending the 018/020 fixture (`tests/interop/`) + parent `phase-9-harness/`, both roles, skip-without-counterparty: fixpp (default knob) sends an app message, capture outbound bytes via the engine-log-seam (016 P4), assert no boundary `43`/`122` in the app portion, QFcpp/QFJ accept it. Run under normal + ASan/UBSan/TSan (018/020 discipline); gate skip-without-counterparty using the **same skip mechanism as the 018/020 cells** (the existing `GTEST_SKIP()`-on-missing-counterparty guard / ctest `SKIP_REGULAR_EXPRESSION`, plan §VII.6) — do not invent a new one. (quickstart "Live interop cells"; contract C5 / SC-005 / FR-010)
- [X] T012 [P] Add the live **PossResend deliver** interop cell (`tests/interop/` + `phase-9-harness/`), both roles, skip-without-counterparty: the QF counterparty sends a `97=Y` business message; assert fixpp delivers it to `fromApp` and the session stays established. Same sanitizer matrix; same 018/020 skip-without-counterparty guard as T011. (quickstart; contract C5 / SC-001/SC-005)
- [X] T013 [P] Update `spec/feature-catalogue.md`: flip **S-010** (`:30`) `backlog → done`, citing 022; replace the 021 partial-delivery note with the completion note enumerating **BOTH** halves (`43`/`122` inbound via 021; `PossResend(97)` inbound witness-confirmed + the `AllowPosDup` send-path knob FR-008 via 022) so it cannot be misread as a 97-only close; flip the AllowPossDup send-path knob sub-note `backlog/DEFERRED → done (022, knob allow_pos_dup, default strip)`. (plan §VI delta; FR-011 / contract C6)
- [X] T014 [P] Update `spec/behaviors-and-limitations.md`: add **B-022-1** (plain `send` strips caller `43`/`122` by default; `AllowPosDup=true` retains; boundary-anchored, fail-closed on malformed framing via slot 131; the auto-resend path always re-adds 43/122 independent of the knob) and **L-022-1** (`PossResend(97)` is delivered to the application for business-level duplicate determination — fixpp adds NO session-level PossResend handling, matching QFcpp/QFJ; the app must dedup on its own business keys). (plan §VI delta)
- [X] T015 [P] Update `spec/coverage-index.md`: flip **S-010 → done** with a 022 reference and remove it from the deferred set, asserting an **exact-set** diff (the done-flip moves exactly S-010; the deferred set loses exactly S-010 and retains `NextExpectedMsgSeqNum(789)` + the remaining named G3 knobs) — not a subset-presence check ([[feedback_completeness_gate_exact_set_not_subset]]). Confirm S-033 (021) is already `done` and is NOT re-touched. (plan §VI delta)
- [X] T016 Update `library/CLAUDE.md` active-feature pointer for 022 (status → implemented; next = `/simplify` → `/speckit-verify` → Gate B) per the merge-bookkeeping convention.
- [X] T017 Run the full local Tier-1 verify mirror (`/speckit-verify`): ASan/UBSan/TSan on the `send_impl` strip + scanner and the interop cells; coverage ≥95/85 on the new strip branches AND the scanner's per-malformed-field fail-closed path; mallocnesia no-heap gate; UNFILTERED Tier-1 (or `-L sync`) for the §XV.9 watch-item. Produces `.specify/decisions/022-possresend-allowpossdup-send-verify.md` — the required evidence for `/gate-b`. (plan Constitution Check IX.1/IX.2/XV.9; pipeline step 17)

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (T001)**: no dependencies — start immediately.
- **Foundational (T002)**: after Setup. Blocks **US2 only** (the knob field US2 references); US1 has no foundational dependency.
- **US1 (T003–T004)**: after Setup. **Independent of US2 and of T002** — may run fully in parallel with the US2 track.
- **US2 (T005–T010)**: after T002. Internal order: T005 → T006 → T007 (RED) → **T008 → T009** (impl, sequential, same file, T009 consumes T008's spans) → T010 (GREEN + no-heap + §XV.9).
- **Polish (T011–T017)**: after US1 + US2 complete. T011–T015 are `[P]` (distinct files); T016 sequential; T017 verify runs last (consumes the finished tree).

### User Story Independence

- **US1 (P1)** and **US2 (P2)** touch disjoint surfaces (US1 = one test file, zero prod; US2 = one config field + one `session.cpp` site + one test file) and are independently testable. US1 is the MVP and can ship/validate alone.

### Within US2

- Tests (T005–T006) written and RED (T007) before implementation; scanner (T008) before excision (T009) — the excision consumes scanner spans; GREEN gate (T010) closes the story.

### Parallel Opportunities

- The entire **US1 track (T003–T004)** runs in parallel with the **US2 track (T002, T005–T010)** — different files, no shared surface.
- Polish doc tasks **T011, T012, T013, T014, T015** are all `[P]` (distinct files: two interop fixtures, catalogue, B&L, coverage-index).

---

## Parallel Example

```text
# After T001 (setup), the two stories proceed concurrently:
Track A (US1, witness-only):  T003 → T004
Track B (US2, production):    T002 → T005 → T006 → T007 → T008 → T009 → T010

# Polish docs in parallel once both tracks land:
T011 | T012 | T013 | T014 | T015   (all [P], distinct files)
```

---

## Implementation Strategy

### MVP First (User Story 1)

1. T001 Setup → T003–T004 US1 witnesses. **If all PASS, the witness-only design (D4) holds** — MVP (the inbound-correctness core) is proven with zero production risk. If any fails, STOP/escalate.

### Incremental Delivery

1. Setup + US1 → inbound PossResend disposition proven (MVP).
2. T002 + US2 (RED → scanner → excision → GREEN) → the send-path strip ships, fail-closed and no-heap.
3. Polish → live interop cells green, catalogue/coverage/B&L updated (S-010 → done), `/speckit-verify` evidence produced for Gate B.

---

## Notes

- `[P]` = different files, no dependencies. Same-file test authoring (T003 / T005 / T006) and the two `session.cpp` impl tasks (T008 / T009) are deliberately **not** `[P]`.
- `[Story]` label maps each task to US1 / US2 for traceability; Setup / Foundational / Polish carry no story label.
- RED-first is binding (`[const §VII]`): verify witnesses fail (US2) or characterize current behavior (US1) before/around the production change.
- The only production surface is the `allow_pos_dup` field + the `send_impl` scanner/excision — no new module, codegen, error slot (reuses `app_payload_malformed=131`), or C-ABI. `build_replay_frame` is read-only here (FR-007 holds by construction).
- Next pipeline steps after `/speckit-tasks`: `/speckit-analyze` → `/speckit-checklist` → `/speckit-checklist-audit` (MANDATORY gate before `/speckit-implement`) → `/speckit-implement` → `/simplify` → `/speckit-verify` → Gate B (per `.specify/pipeline.md`).
```