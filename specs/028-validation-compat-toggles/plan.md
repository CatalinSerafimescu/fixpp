# Implementation Plan: Validation-compat toggles — CheckCompID & ValidateSequenceNumbers (G3 slice)

**Branch**: `028-validation-compat-toggles` | **Date**: 2026-06-07 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/028-validation-compat-toggles/spec.md`

## Summary

Add two opt-in per-session `SessionConfig` bools — `check_comp_id` and
`validate_sequence_numbers`, **both default `true` (current strict behaviour)** —
that relax inbound validation for QuickFIX-compat counterparties. `check_comp_id=false`
skips the steady-state `49`/`56` equality match; `validate_sequence_numbers=false`
accepts out-of-order frames without the gap dance (no ResendRequest on too-high, no
fatal on too-low). Default-true ⇒ byte-identical no-op.

**This is a session-FSM-touch slice (own Gate A), but a tightly bounded one.** All
edits live in the single steady-state handler `case fsm_state::LogonReceived: case
fsm_state::Active:` (`session.cpp:1856-1857`). The Logon-establishment paths
(`NotConnected` `:1524`, `LogonSent` `:2688`) and the 013/024 reset FSM are
**deliberately left strict** (steady-state-only scope — clarify Q3 / D-4), which is
what keeps this a thin slice rather than a recovery-FSM rewrite.

**Key design grounding (source sweep + reference engines, in [research.md](./research.md)):**

- **Two independent additive bools, default `true`** (D-1): polarity inverts vs the false-default knobs (021/022/024/026/027) because strict IS the current default. Primitive `bool` ⇒ no new include, [const §XV.9] N/A.
- **CompID relaxation = one gate** (D-2): the Active/LogonReceived CompID/BeginString gate (`:1869-1874`). Gate the two CompID clauses behind `check_comp_id`; keep `begin_string` always. Distinct from the Logon-establishment CompID check (`interpret_logon`) and from the 013 authorization allow-list (`:1678`) — neither is touched (FR-003 / clarify Q1).
- **Seqnum relaxation = too-high arm guard + Arm-B relaxed dispatch + S6/S7 SequenceReset(35=4) intercept gates** (D-3, five sites): guard the too-high ResendRequest arm (S2, `:2037`) with `&& validate_sequence_numbers`; replace the too-low Arm-B fatal (S4, `:2273-2276`) with deliver-without-advance when the knob is off; gate BOTH `SequenceReset(35=4)` intercepts — reset-mode (S6, `:1966`, before the gate) and gapfill-mode (S7, `:2294`, after the gate) — so `NewSeqNo` is NOT applied when off (FR-013 / I-VCT-11); keep PossDup Stage-1/Stage-2 unchanged (clarify Q4). The counter advances on exact match only via the existing `check_inbound` success (S5, QFJ-parity, clarify Q2) — and exact-match advance holds across ALL inbound paths only because S6/S7 are also gated (the `35=4` family otherwise advances outside the seqnum gate via `apply_inbound_sequence_reset`), not merely from `check_inbound`. No SeqnumManager API change; the shared `apply_inbound_sequence_reset` is untouched.
- **Steady-state only** (D-4 / clarify Q3): deliberate QFJ divergence (QFJ relaxes at Logon via `verify`); fixpp keeps Logon establishment strict for safe establishment and to avoid entangling the 013/024 reset FSM.
- **Default-true ⇒ pure no-op** (D-6): both changes are `if (cfg_.<knob>)` wrappers / `&& cfg_.<knob>` arm-guards; default-true takes the existing branch verbatim — no wire delta, no new alloc/suspension (FR-009/SC-003).

## Technical Context

**Language/Version**: C++23 (Clang; asio awaitables, `std::expected`) — [const §II]
**Primary Dependencies**: the `LogonReceived/Active` inbound handler in `session.cpp` (CompID gate `:1869`, reset-mode `SequenceReset(35=4)` intercept `:1966`, too-high arm `:2037`, `check_inbound` `:2232`, Arm-B fatal `:2273`, gapfill-mode `SequenceReset(35=4)` intercept `:2294`), `parse_and_dispatch_` (reused for deliver-without-advance), `SessionConfig` (+2 bools). No new third-party deps, no new SeqnumManager/store API (the shared `apply_inbound_sequence_reset` is gated, not modified), no codegen, no wire field.
**Storage**: none — read-only use of the existing inbound path; no schema/persistence change.
**Testing**: GoogleTest; ASan/UBSan/TSan; coverage llvm-cov; both-knob + 4-combination unit witnesses; default-off byte-identity + full-regression; live interop ctest cell (skip-without-counterparty) vs a `CheckCompID=N` / `ValidateSequenceNumbers=N` peer. — [const §VII, §IX]
**Target Platform**: Linux/Clang Tier-1 (sanitizer matrix); the live cell runs against QFcpp/QFJ in the parent harness.
**Project Type**: single C++ library (`fixpp`) + tests + interop-harness extension.
**Performance Goals**: N/A — default path unchanged; relaxed paths remove work (skip ResendRequest/recovery). No new allocation.
**Constraints**: `noexcept`/`expected_t` preserved; no new `std::mutex` in awaitable headers ([const §XV.9] — N/A: only primitive `bool` additions to `session_config.hpp`, no new include); default-strict (unset, default-`true`) MUST be byte-identical (FR-009); the relaxed seqnum path MUST reuse the existing `parse_and_dispatch_` (no second dispatch implementation).
**Scale/Scope**: +2 `SessionConfig` bools; 5 edit sites — CompID gate change (S1, 1 site); too-high arm guard (S2, 1 site); Arm-B deliver-without-advance else (S4, 1 site); reset-mode `SequenceReset(35=4)` intercept gate (S6, 1 site); gapfill-mode `SequenceReset(35=4)` intercept gate (S7, 1 site); all in one FSM handler. New `tests/session/test_validation_compat_toggles.cpp` + a live interop cell. No FSM state added; no SeqnumManager/store/codegen/C-ABI change (the shared `apply_inbound_sequence_reset` is gated, not modified).

## Constitution Check

*GATE: must pass before Phase 0 (passed) and re-checked after Phase 1 (re-confirmed).*

| Article | Gate | Status |
|---------|------|--------|
| **II** Language | C++23/Clang, no new deps | ✅ PASS |
| **VI** Spec coverage | **Two net-new catalogue rows** `S-040` (CheckCompID) + `S-041` (ValidateSequenceNumbers), status `done` (FIX 4.4). Normative refs: QuickFIX `CheckCompID` / `ValidateSequenceNumbers` + `[FIX-SL §4.2.2]` (Identification of FIX session peers / CompID) / `[FIX-SL §4.8]` (Message recovery the seqnum knob suppresses — `§4.8.2`/`§4.8.5`/`§4.8.6`). Exact delta below (Polish). | ⚠ RESOLVED (delta specified) |
| **VII** Testing/TDD | RED-first: CompID mismatch accepted when `check_comp_id=false` + rejected at default (both incl. BeginString-still-strict); 013 authz still refuses non-allow-listed even with knob off; too-high no-ResendRequest + too-low no-disconnect when `validate_sequence_numbers=false`; exact-match-only counter advance; PossDup handling retained; steady-state-only (Logon mismatch still refused); 4-combination matrix; default-off byte-identical + full regression | ✅ planned |
| **VII.6** Interop | live both-role cell: fixpp with each knob relaxed vs QFJ/QFcpp peer driving mismatching CompIDs / out-of-order seqnums → accepted, no reject/disconnect, no ResendRequest | ✅ planned |
| **VIII.5** Allocator | relaxed paths reuse the existing no-heap `parse_and_dispatch_` / builders; no new allocation. No-heap witness on the relaxed delivery path | ✅ planned (witnessed) |
| **IX.1** Coverage | ≥95/85 on the CompID guard, the too-high arm guard, and the deliver-without-advance branch | ✅ planned |
| **IX.2** Sanitizers | ASan/UBSan/TSan on the session inbound changes + interop ctest | ✅ planned |
| **X** ABI | no C-ABI/error-slot/wire change. +2 `SessionConfig` bools — source rebuild only (C++ value type). Default-off preserves wire behaviour | ✅ source rebuild (additive bools) |
| **XI.4** Threading | runs on the existing session strand (inbound handler); no new concurrency surface, no callback | ✅ PASS |
| **XII.5** No-implicit-default | both knobs default **`true`** explicitly (strict = current behaviour), documented per-field | ✅ PASS |
| **XIV.2** Pluggable ≤5 pure-virtual | no pluggable interface touched | ✅ N/A |
| **XV.9** Banned (`std::mutex` in awaitable hdr) | only primitive `bool` additions to `session_config.hpp` — no new include | ✅ N/A |
| **XVI.3/4** /clarify before /plan | Session 2026-06-07 (4 asked: authz separation, counter mechanics, Logon-vs-steady-state scope, PossDup interaction) + reference sweep (QFcpp/QFJ CheckCompID + ValidateSequenceNumbers) | ✅ PASS |
| **XVII.1** Gate A before /tasks | mandatory — runs after this plan, before `/speckit-tasks` | ⏳ PENDING (Gate A next) |

**Result**: PASS to proceed. The relaxations are opt-in; both bools default `true`
(strict), so unset config is byte-identical. Both wire-behaviour relaxations are
confined to the single steady-state inbound handler, reuse the existing dispatch,
leave Logon establishment + 013 authz + 024 reset untouched, and are grounded
against both live interop targets. No unjustified violations.

**Exact §VI delta (applied at Polish):**
- `spec/feature-catalogue.md`: add **`S-040`** (`CheckCompID — skip steady-state SenderCompID/TargetCompID match; QuickFIX-compat`) and **`S-041`** (`ValidateSequenceNumbers — accept out-of-order inbound without gap-fill recovery; QuickFIX-compat`), both `done` (FIX 4.4), cite `028-validation-compat-toggles`, evidence_pr `(pending merge)`, Tests `tests/session/test_validation_compat_toggles.cpp` + the interop cell.
- `spec/coverage-index.md`: add the two coverage entries (exact-set diff at Polish — [[feedback_completeness_gate_exact_set_not_subset]]).
- `spec/behaviors-and-limitations.md`: **B-028-1**, **B-028-2**, **L-028-1**, **L-028-2**, **L-028-3** (per research D-7).

## Project Structure

### Documentation (this feature)

```text
specs/028-validation-compat-toggles/
├── plan.md  ├── research.md  ├── data-model.md
├── contracts/validation-compat-toggles.md  ├── quickstart.md
├── checklists/requirements.md  └── tasks.md (Phase 2 — NOT created here)
```

### Source Code (repository root = library submodule)

```text
include/fixpp/session/
└── session_config.hpp     # +2 additive fields:
                           #   bool check_comp_id = true;
                           #   bool validate_sequence_numbers = true;
                           #   (primitive bools — no new include, §XV.9 N/A).
src/session/
└── session.cpp            # ALL edits in the `case LogonReceived: case Active:` handler:
                           #  (S1) CompID/BeginString gate (:1869-1874): gate the two CompID
                           #       clauses behind cfg_.check_comp_id; keep begin_string always.
                           #  (S2) too-high arm (:2037): add `&& cfg_.validate_sequence_numbers`
                           #       so it is skipped (no ResendRequest/AwaitingResend) when off.
                           #  (S4) check_inbound Arm-B fatal (:2273-2276): when
                           #       !cfg_.validate_sequence_numbers, deliver via parse_and_dispatch_
                           #       — admin msgtype -> fromAdmin, app msgtype -> fromApp (via the
                           #       existing is_admin_msgtype helper), no advance, stay Active —
                           #       instead of Disconnected. Too-low Heartbeat(35=0) still silently
                           #       dropped at :2234 (pre-existing carve-out). (S3 PossDup arms +
                           #       S5 in-seq advance UNCHANGED.)
                           #  (S6) reset-mode SequenceReset(35=4) intercept (:1966): when
                           #       !cfg_.validate_sequence_numbers, BYPASS the intercept so
                           #       apply_inbound_sequence_reset is NOT called — deliver to fromAdmin,
                           #       counter unchanged (FR-013). apply_inbound_sequence_reset itself
                           #       UNCHANGED (shared with 013/024/027 strict paths).
                           #  (S7) gapfill-mode SequenceReset(35=4,123=Y) intercept (:2294):
                           #       same knob gate — NewSeqNo NOT applied when off (FR-013).
                           #  Logon paths (NotConnected :1524, LogonSent :2688) + 013 authz
                           #  (:1678) + 024 reset — UNTOUCHED (steady-state-only, D-4).
tests/session/
└── test_validation_compat_toggles.cpp (NEW; target session_validation_compat_toggles)
                           #   CompID accept/reject (knob + default), BeginString-still-strict,
                           #   013-authz-still-enforced; seqnum too-high-no-RR / too-low-no-disconnect,
                           #   exact-match advance, PossDup retained; steady-state-only (Logon strict);
                           #   SequenceReset(35=4) knob-off: too-high AND too-low x reset-mode AND
                           #     gapfill-mode under validate_sequence_numbers=false (assert NewSeqNo
                           #     NOT applied, frame delivered to fromAdmin, counter unchanged) +
                           #     strict-mode paired test (NewSeqNo applied as today);
                           #   admin-vs-app deliver fan-out (fromAdmin/fromApp); too-low Heartbeat
                           #     still silently dropped; 4-combination matrix; default-off
                           #     byte-identical; no-heap relaxed path.
tests/interop/happy/
└── hp_fix44_validation_compat_test.cpp (NEW)  # both-knob live cell, skip-without-counterparty.
phase-9-harness/           # parent: live QFJ/QFcpp counterparty with CheckCompID=N / ValidateSequenceNumbers=N.
```

**Structure Decision**: A session-layer inbound-validation relaxation behind two
default-true `SessionConfig` bools, entirely within the steady-state
`LogonReceived/Active` handler. CompID = gate the two `49`/`56` clauses (keep
BeginString, S1); seqnum = guard the too-high ResendRequest arm (S2) + reroute the
too-low fatal to deliver-without-advance (S4) + gate the two `SequenceReset(35=4)`
intercepts so `NewSeqNo` is not applied when off (S6 reset-mode `:1966`, S7
gapfill-mode `:2294`), reusing the existing `parse_and_dispatch_` and the existing
`check_inbound` exact-match advance (S5). No new module, no new error slot, no
new SeqnumManager/store API (the shared `apply_inbound_sequence_reset` is gated, not
modified), no codegen, no FSM state. Logon establishment + 013 authz + 024 reset are
out of scope by the steady-state-only decision.

## Complexity Tracking

| Change | Why needed | Why it carries a real Gate B (not a chore) |
|--------|------------|-------------------------------------------|
| Seqnum relaxation: too-high arm guard + too-low deliver-without-advance (S2/S4) | FR-006 — the core out-of-order tolerance | Touches the steady-state seqnum gate. Hazards: (1) the too-high frame, once the arm is skipped, falls through to `check_inbound` which is NOT designed for `seq>expected` in Active — the deliver-without-advance branch must catch BOTH too-low and fallen-through too-high without advancing or disconnecting; (2) the counter must advance on exact match ONLY (clarify Q2) — getting this wrong desyncs the session; (3) PossDup Stage-1/Stage-2 (S3) MUST stay on the relaxed path (clarify Q4) — accidentally skipping it loses duplicate handling; (4) `seq==0` MUST stay fatal (I-VCT-10). RED witnesses: too-high no-RR; too-low no-disconnect+delivered; exact-match-only advance; PossDup retained; seq==0 still fatal. |
| CompID relaxation: gate the two CompID clauses (S1) | FR-003 — skip the steady-state match | Hazards: (1) MUST keep `BeginString(8)` strict (a single `if` over all three clauses today — splitting it wrong relaxes BeginString too); (2) MUST NOT touch the Logon-establishment CompID check or the 013 authz allow-list (different sites — a careless "relax all CompID checks" would open the security control, clarify Q1). RED: mismatch accepted (knob) / rejected (default); BeginString-still-strict; 013-authz-still-refuses with knob off; Logon-time mismatch still refused. |
| Steady-state-only scoping (D-4 / FR-012) | clarify Q3 — Logon establishment stays strict | The edits are confined to the `LogonReceived/Active` handler; the Logon paths (`NotConnected`/`LogonSent`) and 013/024 reset are untouched. The risk is a reviewer (or a later edit) assuming QFJ-parity (relax at Logon too); the divergence is explicit and witnessed (Logon-time mismatch/too-high still refused with knobs off). |
| `SessionConfig` +2 bools | FR-001/FR-004 — the config surface | Additive primitive `bool`s (default true). No new include (§XV.9 N/A). Low risk; included for completeness. |

No 4th-project / repository-pattern / speculative-abstraction violations. Every row
extends the existing steady-state inbound handler behind a default-true knob; the
wire delta is grounded against both live interop targets and RED-witnessed. The
substantive risk concentration is the seqnum deliver-without-advance branch + the
CompID-clause split — exactly what the RED witnesses + Gate A target.

## Gate A

- Round 1 applied 2026-06-07: Codex P1=1 P2=1 P3=2; Opus post-judging P1=1 P2=2 P3=4; rewrite resolves the SequenceReset(35=4) exact-match-advance contradiction (QFJ-parity: knob-off does not apply NewSeqNo) as RC#1, fixes the §4.4→§4.8 recovery cite (RC#3), pins relaxed-delivery callback fan-out + too-low-Heartbeat carve-out (RC#2), updates stale checklist + default-off phrasing. Reviews: research/reviews/codex_028-validation-compat-toggles_gate_a_review.md, research/reviews/opus_028-validation-compat-toggles_gate_a_adversarial_review.md.
- Round 2 applied 2026-06-07: Codex P1=0 P2=1 P3=2; Opus post-judging P1=0 P2=1 P3=5; doc-consistency sweep — research D-3/D-6 + plan Summary/Technical-Context/Scale-Scope re-stated to the S1–S7 (5-site) model (RC: round-1 design pivot left the summary docs stale), quickstart Heartbeat carve-out, plan:39 default-strict wording, exact-match-35=4 clarification. No design change. Reviews: research/reviews/codex_028-validation-compat-toggles_gate_a_2_review.md, research/reviews/opus_028-validation-compat-toggles_gate_a_2_adversarial_review.md.
- Round 3 applied 2026-06-07 (CONVERGED): Codex P1=0 P2=2 P3=0; Opus post-judging P1=0 P2=1 P3=3. Opus disagreed Codex-P2#1 (missing S-040/S-041 catalogue/coverage rows) — a Gate-A-vs-Polish phase confusion: the §VI delta is applied at Polish (verified vs the merged 027 precedent, plan.md:69) and gates /gate-b, not Gate A → P3-tracked, not a blocker. Opus confirmed Codex-P2#2 (the round-2 exact-match-gapfill `35=4` carve-out in data-model I-VCT-11 was not back-propagated to the normative layer; SC-008 drives a counter-state assertion that would be wrong for the exact-match-gapfill knob-off arm). Rewrite budget exhausted (2/2) → user escalation; user chose direct-apply-no-further-review. Orchestrator applied the reviewer's verbatim per-mode/ordering wording to the 5 normative sites (FR-013 spec.md:101, SC-008 :122, edge spec.md:79, clarification spec.md:21, contract C2.7) behind a grep-gate on "without advancing"/"any other out-of-order frame"; data-model I-VCT-11 already correct (unchanged). No design/FSM/data-model change. Bundle converged P1=0 P2=0. Reviews: research/reviews/codex_028-validation-compat-toggles_gate_a_3_review.md, research/reviews/opus_028-validation-compat-toggles_gate_a_3_adversarial_review.md.
