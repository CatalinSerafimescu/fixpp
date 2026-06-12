---
description: "Task list for 033-fixt-fix50sp2-session implementation"
---

# Tasks: FIXT.1.1 / FIX 5.0 SP2 Session Establishment

**Input**: Design documents from `specs/033-fixt-fix50sp2-session/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/fixt-logon-establishment.md, quickstart.md
**Branch**: `033-fixt-fix50sp2-session` (library submodule)

**Tests**: REQUIRED and RED-first — `[const §VII]` TDD is mandatory for this project (research R8 names witnesses W1–W7). Within each story, witness tests are written and must FAIL before the implementation tasks that satisfy them.

**Organization**: by user story (US1 P1 establish · US2 P2 credentials · US3 P3 live interop), on a shared Foundational plumbing phase.

> **Gate A residual P3s — applied before this file (2026-06-12):** (1) C10 trace tag now cites `FR-012`; (2) plan.md perf aside reworded to the dict-free R4 framing; (3) FR-004a/W3 explicitly scoped **acceptor-only** — the initiator unserviceable-`1137` refuse disposition is **deferred** (no witness/cell exercises it; avoids an unwitnessed symmetric-arm claim per `feedback_symmetric_api_claim_unreachable_arm`). These are doc edits in the bundle, not tasks.

## Path Conventions

Repository root = the library submodule `research/G19-fix-fpml-iso20022/library/`. Live-interop harness paths are in the **parent** `research/G19-fix-fpml-iso20022/phase-9-harness/`.

---

## Phase 1: Setup

**Purpose**: Locate the establishment seams and stand up the new test targets.

- [X] T001 Confirm the FIXT substrate is present before editing: `build_logon`/`interpret_logon` in `src/session/admin_messages.cpp`, the initiator-emit (`src/session/session.cpp:752`) + acceptor-reply (`~:2023`; the reply-block comment starts ~:1937) + inbound-Logon arms, and that `dictionaries/FIXT11.xml`, `FIX50SP2.xml`, `FIX44.xml` ship and load (`xml_loader` maps `FIXT`→`session_version::vt11`, `src/dictionary/xml_loader.cpp:148`). Record exact line anchors for the diffs.
- [X] T002 [P] Create RED test targets `tests/session/test_fixt_logon_establishment.cpp` and `tests/session/test_fixt_credentials.cpp` (empty `TEST` stubs) and register them in the session test CMake so `ctest -R fixt` discovers them. Confirm they build + run (0 assertions) before any witness is written.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Config, types, and plumbing all three stories sit on. **No story work begins until this phase is complete.**

- [X] T003 Extend `SessionConfig` (`include/fixpp/session/session_config.hpp`): add `std::optional<dict::application_version> default_appl_ver_id` (E3 — the dict-layer enum, PINNED), the optional `username`/`password` credential fields, and the `is_fixt()` predicate (`begin_string=="FIXT.1.1" && default_appl_ver_id.has_value()`). Additive only; `SessionId::from_config` unchanged.
- [X] T004 [P] RED unit test for the NEW inverse render helper `application_version → wire 1137 string` (research R3 / data-model E3) in `tests/dictionary/` (or the existing version_profile suite): assert `v50sp2`→`"9"`, `v44`→`"6"`, `v50`→`"7"` (divergent — proves the helper, not the C++ enum index), and `Unknown`/invalid → failure (no garbage on the wire). MUST fail (helper absent today).
- [X] T005 Implement the inverse render helper in `include/fixpp/dict/version_profile.hpp` (+ `src/dictionary/version_profile.cpp` if non-inline), driving the one canonical wire↔C++ table; make T004 pass. (Existing `resolve_application_version` is wire→C++ only — do not modify it.)
- [X] T006 [P] Thread the engine-built `dict::version_registry` (from `core::build_version_registry(cfg)`, `engine_config.hpp:211`) as a non-owning reference into each `Session` at construction (research R2): add the member to `include/fixpp/session/session.hpp` and pass it through the engine→session construction path (`include/fixpp/session/engine.hpp`). Registry is engine-lifetime; Session holds `version_registry const*` (nullable, default null for test/FIX.4.x paths).
- [X] T007 Extend the `interpret_logon` return struct (E5): add `default_appl_ver_id` (optional wire value), `username` (optional), `password` (optional) to the return type in `include/fixpp/session/admin_messages.hpp`, and add the `case 1137:`/`case 553:`/`case 554:` arms to the dict-free scanner in `src/session/admin_messages.cpp` (purely additive — `default: break` scanner, research R9). No behaviour change for FIX.4.x callers.
- [X] T008 Add the negotiated-version state + exposure (E2 / research R4) to `include/fixpp/session/session.hpp`: strand-confined `negotiated_appl_version_` member (set-once) and the NEW `negotiated_version_profile() const → dict::version_profile` accessor returning `{session=vt11, default_appl=negotiated_appl_version_}`. Reachable via the existing `Engine::lookup(SessionId)→shared_ptr<Session>` spine — no `fromApp` callback-shape change.
- [X] T009 [P] RED unit test for the shared tag-`554` field redactor + `logon_credentials` value type (C8/FR-011): assert a populated `554` is elided by the redactor and that `logon_credentials`' debug/`operator<<` form never prints the password. MUST fail (neither exists today).
- [X] T010 Implement the `logon_credentials` value type (redacting `password` in any debug form) and the **single shared** tag-`554` field redactor utility (one redactor, applied at every persistence site — not per-site ad-hoc, per `feedback_verify_caught_design_pivot_stale_doc_bundle_drift`); make T009 pass. Consumed by US2 (seam + logger/transcript) and US3 (golden writer).

**Checkpoint**: config + render helper + registry handle + scanner fields + negotiated-version accessor + redactor all in place — stories can begin.

---

## Phase 3: User Story 1 — Establish a FIXT.1.1 / FIX 5.0 SP2 session (Priority: P1) 🎯 MVP

**Goal**: A fixpp session establishes over `8=FIXT.1.1` negotiating an application version via `DefaultApplVerID(1137)`, both roles; records + exposes the negotiated version; refuses conformantly on missing/unserviceable `1137`; FIX.4.x byte-identical.

**Independent Test**: fixpp initiator and acceptor each reach Active vs a FIXT.1.1/5.0SP2 counterparty with `8=FIXT.1.1`+`1137` on the Logon; a missing/unserviceable `1137` never reaches Active; a FIX.4.4 session is byte-identical.

### Tests for User Story 1 (RED-first — write and confirm FAIL before T016–T018)

- [ ] T011 [P] [US1] W1 — FIXT Logon round-trip in `tests/session/test_fixt_logon_establishment.cpp`: initiator emits `8=FIXT.1.1`+`1137`; acceptor parses + replies with its own `1137`; both reach Active. Assert the emitted/parsed wire fields directly (not a reach-Active proxy). *(FR-001/FR-002/FR-003; C1/C3)*
- [ ] T012 [P] [US1] W2 — missing-`1137`: inbound `8=FIXT.1.1` Logon omitting `1137` ⇒ `Reject(35=3, 373=RequiredTagMissing=1)` and NOT Active. Assert the `373=1` value + non-Active. *(FR-004/SC-005; C4)*
- [ ] T013 [P] [US1] W3 — unserviceable version (acceptor-scoped): inbound FIXT Logon whose `1137` resolves to no registered dictionary ⇒ emit `Reject(35=3, 371=1137, 373=ValueIsIncorrect=5)` AND NOT Active. Assert the **frame** (`373=5`, `371=1137`), distinct from W2's `373=1`. *(FR-004a/SC-005; C5)*
- [X] T014 [P] [US1] W4 — FIX.4.x byte-identical regression guard: a `8=FIX.4.4` session's outbound Logon carries **no** `1137/553/554` and the full establishment wire is byte-for-byte pre-033 (capture bytes, compare). *(FR-009/SC-002; C2 — load-bearing)*
- [ ] T015 [P] [US1] W5 — version-general discriminating witness: a FIXT session configured for FIX.4.4 (wire `ApplVerID=6`) establishes via the same path as 5.0SP2, and `Engine::lookup(sid)->negotiated_version_profile().default_appl == application_version::v44` (and `== v50sp2` for the 5.0SP2 cell). Not the "both reach Active" proxy. *(FR-006/SC-006; C6)*
- [ ] T035 [P] [US1] W8 — `1128` tolerance: an **established** FIXT session receiving an inbound application message carrying `ApplVerID(1128)` delivers it dict-free to `Application::fromApp` without parse failure and without session shutdown (asserts the session-layer dict-free delivery path; the session never reifies/switches dictionary — INV-FIXT-3). Not a claim about reify. *(FR-010/S-026 deferred; C9)*

### Implementation for User Story 1

- [X] T016 [US1] `build_logon` (`src/session/admin_messages.cpp`): emit `DefaultApplVerID(1137)` (rendered via the T005 helper) after `108`, before `141`, **for FIXT sessions only**; FIX.4.x emits none (preserves W4). 
- [X] T017 [US1] Thread the configured application version into the emit sites in `src/session/session.cpp`: initiator Logon emit (`:752`) + acceptor Logon reply (`~:2023`) — each side advertises its own `default_appl_ver_id` (FR-002; matches QFcpp `Session.cpp:674/701`). (Same file as US2 T023 — sequence them.)
- [ ] T018 [US1] Inbound Logon arm (`src/session/session.cpp`): read peer `1137` from the T007 struct; resolve via `resolve_application_version` + test serviceability via `registry.get(resolved)` (T006 handle); on success set `negotiated_appl_version_` (T008); on **missing** `1137` ⇒ `Reject(373=1)` reusing the missing-`98` pattern (`:2370+`); on **present-but-unserviceable** ⇒ `Reject(371=1137, 373=5)`; no Active on either refuse (INV-FIXT-2). Makes W1–W3, W5 pass; confirm W4 still green.

**Checkpoint**: MVP — FIXT/5.0SP2 + 4.4-over-FIXT establish both roles; missing/unserviceable refuse; FIX.4.x byte-identical.

---

## Phase 4: User Story 2 — Optional credentialed Logon (Priority: P2)

**Goal**: FIXT Logon optionally carries `Username(553)`/`Password(554)`; acceptor surfaces inbound credentials to a default-accept seam; `554` never leaks clear-text.

**Independent Test**: configured creds appear on the outbound Logon; absent when unconfigured; inbound creds reach `authorize_logon(...)`; credential-free Logon still establishes; a logged/transcribed `554` is redacted.

### Tests for User Story 2 (RED-first — write and confirm FAIL before T021–T024)

- [ ] T019 [P] [US2] W6 in `tests/session/test_fixt_credentials.cpp`: `553`/`554` emitted with configured values; absent when unconfigured (establishment unaffected); parsed inbound and surfaced to `authorize_logon(asserted_compid, logon_credentials)`; credential-free FIXT Logon still reaches Active. *(FR-007/FR-008; C7)*
- [ ] T020 [P] [US2] W7: a Logon carrying a populated `554`, when logged/transcribed/written to a unit golden, shows the value **redacted** at each persisted-artifact class (session logger/tap, transport transcript, unit golden fixture). Assert per-class. *(FR-011; C8)*

### Implementation for User Story 2

- [ ] T021 [US2] Add the NEW default-accept `CompIdAuthorizationPolicy::authorize_logon(std::string_view asserted_compid, logon_credentials const&)` seam (`include/fixpp/session/compid_authorization_policy.hpp`), default implementation accepts; the FR-008a future config-gated validation knob attaches here. Independent of the existing mTLS-gated `authorize(peer_identity, compid)` (research R6).
- [ ] T022 [US2] `build_logon` (`src/session/admin_messages.cpp`): optionally append `Username(553)`/`Password(554)` when configured, omit when not (depends on T016 — same function).
- [ ] T023 [US2] Inbound Logon arm (`src/session/session.cpp`): surface parsed `553`/`554` (T007 struct) as a `logon_credentials` value to `authorize_logon`, fired on the establishment path independently of mTLS; credential-free still establishes (depends on T018 — same arm).
- [ ] T024 [US2] Apply the shared T010 tag-`554` redactor at the session logger/tap and transport transcript-capture sites; make W7 pass for the unit/log/transcript classes (the interop-golden class is wired in US3 T026).

**Checkpoint**: US1 + US2 both work; credentials surfaced + redacted; credential-free path unchanged.

---

## Phase 5: User Story 3 — Live interop conformance (Priority: P3)

**Goal**: Replace the parked `deferred:fixt-routing` cell with passing live cells — 5.0SP2 + 4.4-over-FIXT, both roles × QFcpp/QFJ.

**Independent Test**: each of the 8 cells establishes live and matches a banked golden; manifest flipped off `deferred:fixt-routing`.

- [ ] T025 [P] [US3] Register the 8 cells (`HP-fixt50sp2-{qfcpp,qfj}-{init,acc}` + `HP-fixt44-{qfcpp,qfj}-{init,acc}`) + counterparty config templates in `phase-9-harness/`: `TransportDataDictionary=FIXT11.xml`; `AppDataDictionary=FIX50SP2.xml` (50sp2) / `FIX44.xml` (4.4); `DefaultApplVerID=9` (50sp2) / `6` (4.4). *(C10)*
- [ ] T026 [US3] Add the live-cell entry (`tests/interop/happy/hp_fixt_fix50sp2_test.cpp` or harness-driver path — confirm at implement) and wire the shared T010 tag-`554` redactor into `phase-9-harness/tools/run_interop_cell.py`'s golden writer/normalizer before any golden is written (C8 interop class).
- [ ] T027 [US3] Run all 8 cells **live with sockets allowed** (outside the sandbox — the 032 close-out pattern; sandbox silently SKIPs sockets → false-pass per `feedback_codex_sandbox_blocks_sockets_false_pass`). 2-pass `--update-goldens` capture (both golden layers: in-repo `diff_golden_or_skip` + engine-log seam), then re-run flag-free expecting `pass; golden match` for both engines, both roles. Self-run — do not trust a sandboxed green.
- [ ] T028 [US3] Flip the manifest: `tests/interop/cell_results.yaml` off `deferred:fixt-routing` → `status: pass`/`matrix_disposition: live` for all 8 cells; update `cell_results_schema_check_test.py` if a tag retires; bank both golden layers; confirm schema-check passes. *(FR-012/SC-004)*

**Checkpoint**: FIXT axis live-proven both engines/roles; manifest no longer deferred.

---

## Phase 6: Polish & Cross-Cutting Concerns

- [ ] T029 [P] §VI catalogue delta in `spec/feature-catalogue.md`: flip **S-020** (FIXT.1.1/5.0SP2 half no longer deferred — cite 033), **S-025** (`DefaultApplVerID`) backlog→done, **S-022** (`Username`/`Password`) backlog→done; **S-026** stays deferred with a 033 note (inbound `1128` tolerated, per-message routing a follow-on). Decide distinct rows vs amendments for 553/554/1137 here.
- [ ] T030 [P] Update `spec/coverage-index.md`: map FR-001..FR-012 (+ FR-004a/FR-008a) ↔ the new unit suites + the 8 live cells.
- [ ] T031 [P] Add to `spec/behaviors-and-limitations.md`: B-033-* (FIXT.1.1/5.0SP2 establishment; transport/app decoupling) and L-033-* (deferred per-message `1128` routing S-026; **deferred initiator unserviceable-`1137` refuse disposition**; future credential-validation knob FR-008a).
- [ ] T032 Completeness audit (/gate-b precondition, `feedback_feature_completeness_gate`): assert every FR-001..FR-012 + FR-004a + FR-008a and SC-001..SC-006 maps to a passing witness/cell, and the catalogue rows reconcile — 100% or explicitly waived with rationale.
- [ ] T033 Confirm-at-verify flags from plan Constitution Check: **VIII.5** no new heap on the establishment path (reuse no-alloc witnesses); **XV.9** no new `#include` drags a `std::mutex`/`shared_mutex` into the `session.hpp` awaitable closure (run UNFILTERED Tier-1 per `feedback_awaitable_header_mutex_include_edge`).
- [ ] T036 Extend the fuzz harness for the new `interpret_logon` parser arms (analyze D1; `[const §VII]` item 7 — "New parser-touching code without a fuzz harness is a Gate B blocker"). T007 adds `case 1137:/553:/554:` to the inbound Logon scanner, already driven by `tests/fuzz/fuzz_session_recovery_admin_parse.cpp` (inbound admin frame → header scan → `interpret_logon`) — so this is a **seed/corpus extension, NOT a new harness** (the 027 T026 pattern): add FIXT Logon variants to the corpus (`1137` present / `1137` missing / `1137` malformed/non-numeric / optional `553`/`554` present, incl. over-long values) and confirm the new arms are reached. Depends on T007. Verify via `/speckit-verify` fuzz smoke (≥10 min on the full gate).
- [ ] T034 Run quickstart.md validation: `ctest -R fixt` (unit, all witnesses green) + the live-cell pass for both engines.

---

## Dependencies & Execution Order

### Phase order
- **Setup (P1)** → **Foundational (P2)** → **US1 (P3)** → **US2 (P4)** → **US3 (P5)** → **Polish (P6)**.
- Foundational BLOCKS all stories. US3 (live cells) depends on US1 establishment landing (and US2 if creds appear in a cell). Polish depends on US1–US3.

### Key cross-task dependencies
- T005 (render helper) ← T004 (RED); used by T016.
- T010 (redactor + `logon_credentials`) ← T009 (RED); used by T024 (US2) and T026 (US3).
- T006 (registry handle) + T007 (scanner struct) + T008 (negotiated accessor) all feed T018.
- T036 (fuzz seed) ← T007 (the new `1137/553/554` scanner arms); T035 (`1128` tolerance witness) is established-session behaviour, runs with the US1 witnesses but exercises the post-establishment delivery path.
- **Same-file sequencing**: `build_logon` — T016 (US1) before T022 (US2). `session.cpp` inbound/emit arms — T017/T018 (US1) before T023 (US2).
- T028 (manifest flip) ← T027 (live pass) ← T025+T026 (cells + harness wiring).

### Parallel opportunities
- T002 with the rest of Setup.
- Within Foundational: T004, T006, T009 are `[P]` (distinct files); T005/T007/T008/T010 sequence behind their REDs/handles.
- All US1 witness tests T011–T015 are `[P]` (one file, independent `TEST`s — or split if the harness serializes). US2 T019–T020 `[P]`.
- Polish T029–T031 are `[P]` (distinct catalogue/index/B&L files).

---

## Implementation Strategy

**MVP = Setup + Foundational + US1.** That alone un-defers nothing live but delivers a working FIXT/5.0SP2 + 4.4-over-FIXT session both roles with the regression guard — the irreducible capability. US2 (credentials) and US3 (live conformance) are separable increments. Commit after each task or logical group; keep FIX.4.x byte-identical at every step (W4 is the standing guard).

**Next pipeline step after this file**: `/speckit-analyze` (step 6) → `/speckit-checklist` → `/speckit-checklist-audit` (step 9, mandatory gate) → `/speckit-implement`.
