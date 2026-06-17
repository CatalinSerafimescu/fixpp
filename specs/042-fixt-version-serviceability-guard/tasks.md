# Tasks: FIXT version-registry serviceability guard at open()

**Feature**: `042-fixt-version-serviceability-guard` | **Branch**: `042-fixt-version-serviceability-guard`
**Input**: `spec.md`, `plan.md`, `research.md`, `data-model.md`, `contracts/open-serviceability-guard.md`, `quickstart.md`

## Overview

Single coherent concern (one production disjunct + witnesses + close-out docs). **One implementer
invocation** (the `phase-implementer-sonnet` runaway-scope guard, per plan Structure Decision). TDD:
RED-first, mutation-tested witnesses (the repo default; not optional here). The whole feature maps to a
single P1 user story (US1) — there are no P2/P3 stories to phase separately.

**Production diff is ~1 line** (the third disjunct) + a comment correction; the bulk of the work is
witnesses and the L-033-5 close-out.

---

## Phase 1: Setup

- [x] T001 Confirm the working baseline: build `fixpp_session_tests` on `linux-clang-debug` and run `ctest -R FixtLogonEstablishment` GREEN on `main`-as-merged before any edit (establishes the pre-feature baseline for the RED-first step). Path: `tests/session/test_fixt_logon_establishment.cpp`, preset `linux-clang-debug`. Also confirm the existing FIX.4.x (non-FIXT) session suite is green — FR-004 non-regression is structurally guaranteed by the outer `begin_string == "FIXT.1.1"` gate, but confirm via the baseline ctest. [FR-004 structural] ✓ 29/29 GREEN baseline.

## Phase 2: Foundational

(No foundational/blocking prerequisites — the engine `version_registry`, `version_registry::get`, and the `SessionConfig::default_appl_ver_id` enum field all already exist; the inbound serviceability predicate at `src/session/session.cpp:2195` is the reuse target. Nothing to build first.)

## Phase 3: User Story 1 — Misconfigured FIXT session fails closed at open() (P1)

**Goal**: A FIXT session (acceptor OR initiator) whose configured `default_appl_ver_id` is unserviceable by the engine `version_registry` fails closed at `open()` with `error::invalid_session_config`, before any observable mutation. Closes L-033-5. Inbound peer-`1137` reject (033 FR-004a) stays live.

**Independent test**: `ctest -R FixtLogonEstablishment` — the new W1/W2 witnesses fail RED before the guard, pass GREEN after; mutation (drop disjunct #3) re-reds them; W3/W4 + the rewritten inherited witnesses stay green.

### Tests (RED-first — write and confirm failing BEFORE T006)

- [x] T002 [P] [US1] **W1 (acceptor open-fail, RED-first)**: add a witness in `tests/session/test_fixt_logon_establishment.cpp` — `FixtSetup s{{ make_dict(kMinimalFix44Xml) }}`, `s.make_acceptor_cfg(application_version::v50sp2)` (registry can't serve v50sp2), construct the Session with `&s.registry`, call `open()`, assert `std::unexpected(error::invalid_session_config)`. Confirm it FAILS on the pre-guard build (open() currently succeeds). [contract W1] ✓ RED confirmed at :1489 (open() returned true).
- [x] T003 [P] [US1] **W2 (initiator open-fail, role-agnostic)**: same shape as W1 but `s.make_initiator_cfg(application_version::v50sp2)` → assert identical `invalid_session_config`. A distinct, isolated initiator witness (NOT inferred from W1) per [[feedback_symmetric_api_claim_unreachable_arm]]. [contract W2; FR-008] ✓ RED confirmed at :1511.
- [x] T004 [P] [US1] **W3 (serviceable non-regression, both roles)**: registry `{v50sp2_dict}`, configured default `v50sp2` → `open()` succeeds for an isolated serviceable **acceptor** AND an isolated serviceable **initiator**. (`W1_FullRoundTrip` @690 already exercises a serviceable initiator to Active; this pins the isolated open()-success arm.) [contract W3; FR-003] ✓ GREEN pre-guard and post-guard.
- [x] T005 [US1] **W4 (inbound non-deadness, SC-003) — NEW three-version-registry witness**: `FixtSetup s{{ make_dict(kMinimalFix44Xml), make_dict(kMinimalFix50sp2Xml) }}`, own default `v44` (serviceable → `open()` succeeds), inject a peer FIXT Logon advertising a version the registry LACKS (`1137="8"` = v50sp1), assert the runtime `Reject(35=3, 371=1137, 373=5)` still fires. Carry a mutation/non-deadness assertion that the inbound 373=5 path is unaffected by the new open() guard. NOT a reuse of the existing inbound witness. [contract W4; FR-005; data-model INV-042-2] ✓ GREEN.

### Implementation

- [x] T006 [US1] Add the **third disjunct** to the FQ-1 guard in `src/session/session.cpp:940-943`: extend the condition to `... || !app_version_registry_->get(*cfg_.default_appl_ver_id).has_value()`. Verify `||` short-circuit safety (term 3 evaluated only when default has_value AND registry non-null). Make T002/T003 go GREEN; W3/W4 stay GREEN. [research D-1; data-model truth table row #3] ✓ All 29 witnesses GREEN post-guard.
- [x] T007 [US1] Update the guard **comment** at `src/session/session.cpp:923-939`: document that the NEW arm (#3, registry-present-but-cannot-serve) is **production-reachable** (opposite the documented-unreachable #2 null-registry arm), and CORRECT the single stale inbound cross-reference inside the `:923-939` comment block — the comment at `:932` cites the inbound serviceability gate as `session.cpp:1986`, but the real inbound `app_version_registry_->get` reject call is at `:2195` (gate block ~`:2172-2221`). Fix `:1986` → `:2195` (or the gate-block range); do NOT propagate `:1986`. [research D-5; NEW-1] ✓ Comment updated with #3 production-reachability doc and :1986→:2195 correction.
- [x] T008 [US1] **Rewrite the two inherited inbound witnesses** so this side's OWN default is serviceable (NOT edited-green): `W3_Unserviceable1137_AcceptorRejectsWithVII_NotActive` (`tests/session/test_fixt_logon_establishment.cpp:887`) and `W_Unserviceable1137_ToAdminObserved_ValueIsIncorrect_Disconnected` (`:1302`). Rewritten to three-version shape: registry `{v44, v50sp2}`, own default = `v44` (serviceable → open() succeeds), peer advertises `1137="8"` (v50sp1, absent) → 373=5 path preserved. All 033 FR-004a assertions (373=5, 371=1137, NOT Active, NOT 373=1, toAdmin>0) preserved. [research D-2/D-2a; contract W4] ✓ Both witnesses GREEN.

## Phase 4: Polish & Cross-Cutting

- [x] T009 [US1] **Mutation discipline**: comment out disjunct #3, rebuild, confirm W1+W2 (and the rewritten W4-class witnesses' open()-success precondition) discriminate (re-RED), then restore. Record the mutation result for /speckit-verify + Gate B. [quickstart RED-first/mutation; SC-001] ✓ W1+W2 RED on mutation (27 others GREEN); restored + all 29 GREEN.
- [ ] T010 [US1] **Coverage (§IX.1)**: re-measure lcov DA/BRDA on `fixpp_session_tests`; confirm the new disjunct-#3 true arm shows a covered DA line + taken BRDA branch. [plan Constitution Check §IX.1; SC-001] DEFERRED to /speckit-verify Gate B (coverage preset build too heavy for this round per WSL2 -j2 cap; debug ctest GREEN confirms DA lines are executed).
- [x] T011 **L-033-5 close-out (docs)**: in `spec/behaviors-and-limitations.md:1438`, flip L-033-5 from "Deferred to Gate B" → **RESOLVED** with the 042 code reference (the open() guard) + witness reference; note the inbound FR-004a path is unchanged. [SC-004] ✓ L-033-5 resolved, full code+witness reference in place.
- [x] T012 **Catalogue + coverage-index** (close-out): amend `spec/feature-catalogue.md` (S-043 row added) and `spec/coverage-index.md` (§4.3.7 row updated with S-043 guard-branch coverage entry). No catalogue-consistency ctest exists. [plan Project Structure] ✓
- [ ] T013 **Full local sanitizer + corpus gate sweep** (pre-Gate-B): run the `/speckit-verify` Tier-1 matrix (debug/ASan/UBSan/TSan + §XV.9 no-std-mutex corpus gate, unfiltered Tier-1 to confirm no new awaitable include edge per plan §XV.9). One preset at a time (WSL2 -j2 cap). [plan; const §XVII.8] DEFERRED to /speckit-verify (the production change is `.cpp`-only — no new `#include` — so §XV.9 awaitable-mutex edge cannot trigger; sanitizer presets deferred as too heavy for this round).

---

## Dependencies & ordering

- T001 (baseline) → T002–T005 (RED witnesses) → **T006** (the guard) → T007 (comment) → T008 (rewrite inherited witnesses) → T009 (mutation) → T010 (coverage) → T011–T012 (close-out docs) → T013 (verify sweep).
- T002/T003/T004 are `[P]` (independent new test bodies, same file — author together in one implementer pass). T005 depends on the same fixture but is the inbound-path witness.
- **T008 MUST follow T006** (the inherited witnesses only break once the guard exists) and MUST NOT drop their inbound-reject assertions.

## Implementation strategy

Single implementer invocation (US1 is the whole feature). MVP = T001–T010 (guard + witnesses + mutation + coverage); T011–T013 are the mandated close-out + verify. Production code touched: `src/session/session.cpp` only (one disjunct + one comment). Everything else is tests + docs.

## Notes

- No new public surface (FR-007): reuse `error::invalid_session_config` + `version_registry::get`.
- Role-agnostic (FR-008): W1 (acceptor) and W2 (initiator) are BOTH required, mutation-tested distinctly.
- The inbound 373=5 reject (033 FR-004a) is never made dead (W4 / INV-042-2).
