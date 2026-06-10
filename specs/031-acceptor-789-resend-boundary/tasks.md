# Tasks: Acceptor NextExpectedMsgSeqNum(789) Resend-Range Boundary Fix

**Feature**: `031-acceptor-789-resend-boundary` · **Branch**: `031-acceptor-789-resend-boundary`
**Input**: spec.md (US1/US2, FR-001..009, SC-001..005), plan.md, research.md (R2–R8), data-model.md,
contracts/honor-next-expected.md, quickstart.md.
**Gate A**: CONVERGED 2026-06-11 (gate-a-done). Four banked directives are encoded inline below.

## Format: `[ID] [P?] [Story] Description`

## Path Conventions

- Source: `src/session/session.cpp`, `include/fixpp/session/session.hpp`
- Unit tests: `tests/session/test_next_expected_msgseqnum.cpp`
- Interop cell: `tests/interop/happy/hp_fix44_next_expected_test.cpp`
- Catalogue / docs: `spec/feature-catalogue.md`, `spec/coverage-index.md`, `spec/behaviors-and-limitations.md`

## Phase 1: Setup

- [ ] T001 Confirm a green baseline + the true RED before any edit: build the debug preset and run `next_expected_msgseqnum` (+ `session_reset_on_lifecycle`, `reset_seqnum_policy_matrix`, `persistent_seqnum_hydrate`). Record pass counts. Build parallelism ≤ `-j2` (WSL2 OOM cap). **Guard against stale build artifacts** (a prior-branch `session.cpp.o` can mask the defect — 030 T001 lesson): if the baseline looks off, `touch src/session/session.cpp` + rebuild. **Gate-A directive #2:** the W1 RED test (T004) MUST be confirmed RED on the unmodified `main` body before the fix lands.

## Phase 2: User Story 1 — Acceptor with 789 establishes with an in-sync peer (Priority: P1) 🎯 MVP

**Goal** (spec US1 / FR-002/FR-004/FR-005/FR-006): in-sync peer (`789=N_pre`) ⇒ no spurious resend, session establishes and stays; too-high (`789=N_pre+1`) ⇒ Logout; invalid-789 ⇒ Logout (unchanged).

**Independent test**: drive an acceptor with the 789 knob on; in-sync ⇒ zero `35=4`/`35=2`, Active, no peer reject; boundary ⇒ Logout.

### RED tests (write first; W1 must be RED on current `main`)

- [ ] T002 [US1] **Split the bug-compatible pin** (Gate-A directive #1; the 030-W9b analog): in `tests/session/test_next_expected_msgseqnum.cpp`, remove the acceptor arm of `TEST(Honor, XeqN_NoResend)` (`:776-817`, which feeds `789=5==N_post` and asserts Active — flips to Logout under the fix); its in-sync intent moves to W1 (T003, retaining the acceptor fixture structure but re-seeding `N_pre=4` + feeding `789=4`) and its `789=5` feed moves to W3 (T004). Keep the **initiator** arm (`789=3`) byte-identical in place.
- [ ] T003 [P] [US1] **W1 (in-sync, the bug)** — new `TEST(Honor, Acceptor_XeqNpre_NoResend_Establishes)`: acceptor, 789 knob on, seed outbound so `N_pre=4`; feed peer Logon `789=4` (`==N_pre`). Assert (Gate-A directive #2): (a) the reply Logon WAS emitted at `34==N_pre` (locate the frame); (b) inspecting only frames AFTER the reply, **zero** `35=4`, **zero** `35=2`, **zero** `43=Y`, and **no** further frame at `34==N_pre`; (c) state Active. RED on current `main` (emits the spurious GapFill at `N_pre`). [FR-002/FR-004, SC-001/SC-003, W1]
- [ ] T004 [P] [US1] **W3 (too-high boundary)** — new `TEST(Honor, Acceptor_XeqNprePlus1_TooHigh_Logout)`: acceptor, seed `N_pre=4`, feed peer initial Logon `789=5` (`==N_pre+1==N_post`). Assert a `Logout` (35=5) with text "expecting 4 ... received 5" then Disconnected (NOT Active). This is the repurposed old `789=5` feed. [FR-005, W3]
- [ ] T005 [P] [US1] **W5 (invalid-789, unchanged)** — confirm/add an acceptor invalid-789 (`789=""`/non-numeric → parse 0) ⇒ Logout "NextExpectedMsgSeqNum invalid" + Disconnected, unchanged by the fix. [FR-006, W5]

### Implementation (make T003/T004 GREEN; keep T005 GREEN)

- [ ] T006 [US1] In `include/fixpp/session/session.hpp:1003`, add the `seqnum_t next_outbound_ref` parameter to the `honor_peer_next_expected_` declaration. [contract C-031]
- [ ] T007 [US1] In `src/session/session.cpp` `honor_peer_next_expected_` (`:4509`): add the `next_outbound_ref` param. Set the comparison reference `const seqnum_t n789 = next_outbound_ref;` at `:4512` (replacing `seqnum_mgr_.peek_outbound()`) — the existing comparisons `x789 > n789` (`:4544`, too-high) and `x789 < n789` (`:4587`, behind) and the too-high Logout text value (`:4555`) then read the corrected reference automatically; the `x789 == 0` guard (`:4513`) is X-only (unchanged) and the **in-sync arm is the fall-through `co_return true` at `:4597`** (a comment + `co_return`, NOT an `==` expression — do NOT add a dead `== next_outbound_ref` comparison). **Gate-A directive #3 (contract-fidelity):** at `:4591`, write the resend-range endpoint explicitly as `seqnum_mgr_.peek_outbound() - 1U` (not the post-rename `n789 - 1U`, which would read `next_outbound_ref - 1`) so it matches INV-NEX-RANGE `[X, N_pre]`. **This is for clarity/contract-fidelity, not correctness:** the endpoint arg is behaviorally inert here because the call passes `end_is_through_current=true`, which forces `eff_end = our_last` at `:4419-4420` (the `requested_end` arg is ignored; see T010 note). Writing the explicit form keeps the code correct if `end_is_through_current` is ever flipped. [FR-001, contract C-031]
- [ ] T008 [US1] Acceptor call site (`src/session/session.cpp:2027`): **Gate-A directive #4** — capture `const seqnum_t n_pre = seqnum_mgr_.peek_outbound();` **lexically ABOVE** the reply Logon `store_then_emit` (`:2015`), and pass `n_pre` as `next_outbound_ref` at `:2027`. Preserve RC#4 reply-before-honor ordering (FR-007). [FR-001/FR-007]
- [ ] T009 [US1] Initiator call site (`src/session/session.cpp:3322`): pass `seqnum_mgr_.peek_outbound()` (current) as `next_outbound_ref` — byte-identical (FR-008). [FR-008]

## Phase 3: User Story 2 — Acceptor proactively resends to a genuinely-behind peer (Priority: P1)

**Goal** (spec US2 / FR-003): `789 = X < N_pre` ⇒ resend exactly the 027-shipped range starting at `X` (endpoint live `peek_outbound()-1 = N_pre`), no `ResendRequest`; non-regression.

- [ ] T010 [US2] **W2 (genuine-gap non-regression)** — confirm `TEST(Honor, Acceptor_XltN_ResendsExactRange_AfterReply_NoResendRequest)` (`:643`) stays GREEN: peer `789 = X < N_pre` ⇒ the proactive resend covers the stored gap (`[X, our_last]` as PossDup/GapFill) + GapFill-through-current with `NewSeqNo = peek_outbound()`, and **no** `35=2`. **Note (audit CHK017 / Gate-A directive #3 — corrected):** the resend-range *endpoint* arg is **behaviorally inert** at this call site — `replay_outbound_range_` is called with `end_is_through_current=true`, and `session.cpp:4419-4420` forces `eff_end = our_last` unconditionally (the `requested_end` arg is ignored; the trailing GapFill reads `peek_outbound()` directly at `:4430`). So `next_outbound_ref-1` vs `peek_outbound()-1` produce **identical** wire output and **no unit test can discriminate them**. Directive #3 is therefore a **contract-fidelity / clarity** directive (write `peek_outbound()-1U` at `:4591` to match INV-NEX-RANGE and stay correct if `end_is_through_current` ever changes), enforced by **code review of the one-line change at T007 + the live cell T017**, NOT by a unit RED. [FR-003, SC-002, W2]
- [ ] T011 [US2] Confirm `TEST(Honor, Initiator_XltN_ResendsExactRange_NoResendRequest)` (`:716`) + the initiator arm of `XeqN` (kept from T002) stay GREEN (W4 initiator non-regression). [FR-008, W4]

## Phase 4: Polish & Cross-Cutting

- [ ] T012 Run the full `next_expected_msgseqnum` suite + the session/recovery/027/029/030 regression suites on debug; confirm SC-005 (100% existing witnesses green; default knob-off + initiator byte-identical). [SC-005]
- [ ] T013 [P] §VI delta — `spec/feature-catalogue.md`: amend the **S-031** row Notes to cite `031` as the acceptor-honor conformance correction (peer 789 vs **pre-reply** outbound; in-sync `X==N_pre` ⇒ no resend; too-high boundary at `N_pre`; genuine-gap range `[X, N_pre]` + initiator unchanged). No new S-row.
- [ ] T014 [P] §VI delta — `spec/coverage-index.md`: map the parameterized comparison branches ↔ `test_next_expected_msgseqnum.cpp` (W1/W2/W3/W5) under the existing 027 entry.
- [ ] T015 [P] §VI delta — `spec/behaviors-and-limitations.md`: add **B-031-1** (acceptor 789 honor compares against the pre-reply next-outbound; in-sync peer ⇒ no resend, session establishes with no duplicate-seq frame; reference-engine-conformant).
- [ ] T016 **Obsolete-prose grep-sweep** (one exhaustive pass — [[feedback_verify_caught_design_pivot_stale_doc_bundle_drift]]): grep for any 027 comment/doc framing the acceptor honor as comparing against the post-reply outbound or implying the in-sync case resends. Needles: the `:4512` `// OUTBOUND (I-NEX-11)` comment (its semantics change — `n789` is now `next_outbound_ref`, not the live post-reply `peek_outbound()`); `peek_outbound()` near the honor doc (`session.cpp:4495-4512`); the `:2023-2030` RC#4 honor comment; the `I-NEX-11` "compare against peek_outbound() (OUTBOUND)" comments in `test_next_expected_msgseqnum.cpp` (`:629-637`); any S-031 / B-0xx prose asserting the post-reply comparison. Amend each to "pre-reply next-outbound (`next_outbound_ref`) on the acceptor arm."
- [ ] T017 Live interop close-out (SC-004) — re-run the 027 SC-005 acceptor cell (`NE-*-acc`) in `tests/interop/happy/hp_fix44_next_expected_test.cpp` vs **both** QuickFIX-cpp v1.16.0 and QuickFIX-J 3.0.1 (789 knob on, no `141=Y`): the session establishes, fixpp emits **zero** `SequenceReset`/`ResendRequest`, and the peer does **not** Logout-reject ("MsgSeqNum too low"). **Harden the witness past `drive_to_active`** (stay-Active / `recent_events` discriminator, mirroring 030 RC#2). Skip-guard for CI without a counterparty. [SC-004]
- [ ] T018 `/speckit-verify` 6-preset matrix (debug + ASan/UBSan/TSan + coverage + gcc-release, ONE AT A TIME per the WSL2 cap, `--clean-first` each): ≥95/85 branch coverage on the parameterized comparison branches on BOTH call sites; the new acceptor in-sync (`X==N_pre`) + too-high (`X==N_pre+1`) arms covered. Confirm XV.9 (no new awaitable-header include) via an unfiltered Tier-1. **FR-009 explicit check:** confirm no new C-ABI symbol / error slot / codegen output delta (abidiff clean; the signature change is internal source-rebuild only). [SC, IX.1/IX.2/XV.9, FR-009]
- [ ] T019 **Feature-completeness audit** (T058-class; /gate-b precondition — `[[feedback_feature_completeness_gate]]`): assert against the merged tree (i) every tasks.md row `[X]` or waived; (ii) every FR-001..009 + SC-001..005 ↦ a landed test AND landed impl; (iii) S-031 row `done`+amended with a matching coverage-index entry. Record 100% (or waivers) in the verify decision record.

## Dependencies & Execution Order

- **Phase 1 (Setup/RED-verify)** blocks everything (the W1-RED-on-main proof gates the fix's validity).
- **Phase 2 (US1)** RED tests T002–T005 → implementation T006–T009 (T006 before T007; T007/T008/T009 touch distinct sites but all depend on the new signature T006). US1 is the MVP (the live-found defect).
- **Phase 3 (US2)** is non-regression; T010 is the guard that catches the directive-#3 range off-by-one — it must stay GREEN after the fix.
- **Phase 4 (Polish)** is last: T012 (regression) → T013–T016 (docs/grep-sweep, parallel) → T017 (live) → T018 (verify matrix) → T019 (completeness). T018+T019 gate Gate B.

## Parallel Opportunities

- T003/T004/T005 (separate new TESTs in the same file — sequence the file writes to avoid conflicts, logically parallel).
- T013/T014/T015 (separate doc files) run in parallel.

## Implementation Strategy

MVP = Phase 2 (US1): the in-sync establish + boundary + the param fix. US2 is a non-regression guard. The fix is ~10–14 effective LoC (one param + one pre-reply capture + four comparison switches); the bulk of the work is the discriminating witnesses and the doc/grep-sweep.
