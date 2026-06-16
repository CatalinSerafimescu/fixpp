# Implementation Plan: Validation Gate Wiring

**Branch**: `041-validation-gate-wiring` | **Date**: 2026-06-16 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/041-validation-gate-wiring/spec.md`

## Summary

Wire two fully-implemented-but-unwired validation gates into production:

- **(A) Opt-in dictionary-driven inbound validation.** `wire::dictionary_driven_validator` exists and binds a `fixpp::dict::table_view` (6 checks: header order, unexpected tag, required field, field type, repeating-group, enum value), but **neither the production `table_view` NOR the 7-value `dict::field_type` enum it switches on exist** (both live only in the test mock `tests/support/mock_dict_table.hpp`), and the validator is never instantiated in production (RC-A, a buildability blocker). We (1) define a production `dict::field_type` enum + a production `table_view` value type in the dict layer, give `validator.hpp` a complete-type include path (+ a compile-witness TU), and populate the `table_view` from the session's `dict::Dictionary` (5 of 6 inputs available today; `enum_valid()` returns `true` — Phase-1, enum tables 2c-deferred), and (2) insert a `validate()` gate **per-arm, before each inbound-processing state's sequence-number gate** in `on_inbound_frame`'s state `switch` (QuickFIX parity; `LogoutSent`/`Disconnected` drain arms untouched, `35=3`/`35=5` no-reject-loop preserved), behind a new per-session opt-in flag defaulting **OFF**. Validation failures emit a session `Reject(35=3)` with the QuickFIX-parity `SessionRejectReason` and do not advance the sequence number.
- **(B) Engine clock-config gate.** `validate_engine_config()` returns `clock_not_set` but is never called. We change `Engine::start()` from `void` to `expected_t<void>` and call `validate_engine_config()` at the top, rejecting a null time source.

The default (validation-disabled) inbound path is left byte-for-byte unchanged: the validator and the early parse are constructed/run only when the flag is set, so there is zero per-message cost and zero behaviour change at default (FR-002 / SC-001 / SC-005).

## Technical Context

**Language/Version**: C++23 (clang + gcc), CMake + Conan presets
**Primary Dependencies**: none new — `fixpp::wire` (validator, parser), `fixpp::dict` (Dictionary, table_view), `fixpp::session`, `fixpp::core` (error, expected_t)
**Storage**: N/A
**Testing**: GoogleTest via ctest — unit witnesses for each violation class + reason-code mapping; default-off no-op characterization; engine clock-gate unit test; live cross-engine parity DEFERRED (interop matrix, conditioned like 037/038)
**Target Platform**: Linux (primary); validation + clock gate are platform-independent
**Project Type**: Library (FIX engine) — wire codec + dict + session inbound FSM + engine lifecycle
**Performance Goals**: default path: zero measurable regression (no validator construction/invocation when disabled). Enabled path: validation is O(1)-stack / ≤~600 B working set / zero-heap-per-message (validator already meets [2b §6.5]); the `table_view` is built once at validator construction (config-time alloc, not hot path)
**Constraints**: `noexcept` inbound path; no per-message heap (Article XV §1); validation runs before the seqnum gate when enabled; default OFF; one public C++ API change (`Engine::start()` return type); no C-ABI change; no codegen change; enum-value checks scoped out (FR-005)
**Scale/Scope**: 1 new production `dict::field_type` enum + 1 new production `table_view` realization + `Dictionary::as_table_view()` builder + a compile-witness TU (dict/wire layer, RC-A); 1 new SessionConfig bool; per-arm inbound validate-gate insertions (one per processing state) + reason-code mapping in `Session`; **`emit_session_reject_` extended/overloaded to carry a reason code (the existing `build_reject` already accepts `ref_tag_id`+`session_reject_reason` and emits 371/373 — reused unchanged, RC-C)**; `Engine::start()` signature change + migration across the `fx.start()` fixture wrapper and the dozens of test/interop `start()` call sites

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

- **Gate A trigger (Article XVII / §XVI.3)**: **REQUIRED** — touches inbound **wire-validation behaviour**, the **session FSM** inbound path, **error semantics** (new reject reasons on a new path), and a **public-API signature** change (`Engine::start()`). `/clarify` was run (mandatory per §XVI.3 for wire-format / session-FSM / error-semantics features — DONE, 4 decisions recorded). Gate A runs after this plan, before `/tasks`.
- **§XV.1 (no per-message hot-path heap alloc)**: the validator is already zero-heap-per-message (O(1) stack, ≤~600 B); the `table_view` owns its tables but is built **once at construction** (config-time), not per message. The early parse on the enabled path uses the existing inbound PMR arena (no global heap). Default path constructs neither. → **PASS** (re-verify the table_view build allocates only at setup).
- **§XV.9 (no `std::mutex` / std-sync in awaitable closures)**: inbound path stays on `fixpp::sync` primitives; the new flag is a plain `bool`; no new mutex. The new headers must be run through the §XV.9 corpus gate (extended in 039 US4). → **PASS (to be re-verified).**
- **§VIII (performance budgets)**: default path unchanged (SC-005). Enabled path adds one parse + one O(1) validate before the seqnum gate; an opt-in mode, measured but not on the default budget. The enabled path may parse the frame twice (validate + dispatch) unless the MessageView is threaded through — see research.md R-2; either way no alloc-budget violation. → **PASS** (enabled-path cost documented).
- **Layering (`tools/check_layers.py` / architecture.md)**: `table_view` realization + `as_table_view()` live in the **dict layer** (`fixpp::dict`); the validator is **wire layer** and already binds `dict::table_view`; `fixpp::session` already depends on both. No layer inversion. The Gate-B fixer must re-run `tools/check_layers.py` after the new headers land (per `feedback_gate_b_check_layers_post_fixer`). → **PASS (re-verify post-impl).**
- **§IX.1 coverage (lcov DA/BRDA)**: every new branch — each validator-failure→reject-reason arm, the default-off skip, the enabled-pass path, the clock-gate reject + success — MUST be covered (no new zero-hit lines). → **PASS (by the FR/SC witnesses).**
- **§X.1 frozen C-ABI**: `Engine::start()` is a C++ API; `src/capi/capi.cpp` exports no engine binding, so the C ABI is untouched. New error reasons are existing `core::error` / `SessionRejectReason` values, not new C-ABI codes. → **PASS.**
- **Article VI (Normative References)**: a `## Normative References` section is present in BOTH `research.md` and `spec.md` (Article VI §5 requires it in every `/specify` artifact), citing `[2b §6.5.1/6.5.3/6.5.4/6.5.5]`, the FIX `SessionRejectReason(373)` taxonomy, `[2d §4.4]` (`clock_not_set`) / `[2d §4.5]` (`invalid_session_config`), and the QuickFIX `Session.cpp:1218-1229` anchor (with the minimal excerpt quoted). → **PASS.**

No violations requiring Complexity Tracking. The one public-API change (`Engine::start()` `void`→`expected_t<void>`) is a clarified, accepted decision with zero production callers (test-only blast radius) and is the spec's intended "Engine::open" gate — not an unjustified complexity addition.

## Project Structure

### Documentation (this feature)

```text
specs/041-validation-gate-wiring/
├── plan.md              # This file
├── research.md          # Phase 0 — design forks resolved
├── data-model.md        # Phase 1 — entities (config flag, table_view, reason mapping)
├── quickstart.md        # Phase 1 — how to enable + observe
├── contracts/           # Phase 1 — table_view surface, reject-reason mapping, engine-start contract
└── tasks.md             # Phase 2 (/speckit-tasks — NOT created here)
```

### Source Code (repository root)

```text
include/fixpp/dict/
├── field_type.hpp            # NEW — production home of the 7-value `dict::field_type` enum
│                             #       (currently ONLY in tests/support/mock_dict_table.hpp — RC-A)
├── table_view.hpp            # NEW — production realization of the 2c table_view value type
│                             #       (6-method surface the validator binds; owns its tables)
└── dictionary.hpp            # EDIT — add `as_table_view()` builder (the deferred method, dictionary.hpp:18)

include/fixpp/wire/
├── validator.hpp             # EDIT — include field_type.hpp + table_view.hpp for COMPLETE types
│                             #        (was forward-decl-only, mock-include-order dependent — RC-A);
│                             #        the 6-method binding surface is unchanged but the include
│                             #        dependency is NEW
└── reject_reason_map.hpp     # NEW (or inline in session) — wire_* → SessionRejectReason mapping

tests/wire/ or tests/dict/
└── (compile-witness TU)      # NEW — instantiates dictionary_driven_validator from a production
                              #       table_view WITHOUT the mock, proving the production path compiles

include/fixpp/session/
├── session_config.hpp        # EDIT — new `bool validate_inbound_messages = false;` (after validate_sequence_numbers)
├── session.hpp               # EDIT — validate-gate hook declaration
└── engine.hpp                # EDIT — Engine::start() void → expected_t<void>

src/session/
├── session.cpp               # EDIT — insert validate gate per-arm before each processing state's
│                             #        seqnum gate (enabled-only; drain arms untouched, 35=3/35=5
│                             #        no-reject-loop preserved); extend/overload emit_session_reject_
│                             #        with a reason code (build_reject ALREADY carries it — RC-C)
└── engine.cpp                # EDIT — start() calls validate_engine_config(); returns expected_t<void>

src/dict/  (or header-only)   # table_view impl if not header-only

tests/
├── wire/ or dict/            # table_view-from-Dictionary unit tests
├── session/                  # validate-gate witnesses (one per violation class + reason code),
│                             #   default-off no-op characterization, validate-before-seqnum ordering,
│                             #   malformed-Logon-rejected
└── session/                  # engine clock-gate unit test (null clock → clock_not_set; valid → ok)
                              #   + start() signature migration across existing fixtures
```

**Structure Decision**: Single-library layout. The new `table_view` realization and `as_table_view()` builder are **dict-layer** additions (the validator already depends on `dict::table_view`); the validate-gate insertion + reason mapping are **session-layer**; the clock gate is **engine-layer**. No new top-level module.

## Complexity Tracking

No constitution violations require justification. The notable design choices (parse-hoist before the seqnum gate on the enabled path; realizing the production `table_view`; the `Engine::start()` signature change) are each the minimal way to satisfy a clarified requirement and are detailed in research.md, not complexity exceptions.

## Gate A

- Round 1 applied 2026-06-16: Codex P1=3 P2=5 P3=0; Opus post-judging P1=4 P2=5 P3=5; rewrite addresses root causes RC-A (production realizability), RC-B (FSM state/message scope), RC-C (claim-fidelity grounding sweep). Reviews: research/reviews/codex_041-validation-gate-wiring_gate_a_review.md, research/reviews/opus_041-validation-gate-wiring_gate_a_adversarial_review.md.
- Round 2 applied 2026-06-16: Codex P1=0 P2=1 P3=2; Opus post-judging P1=0 P2=1 P3=2; rewrite closes the RC-B Logon-arm ordering residual (validate-first contract + overlap-precedence witness list + data-model Logon-arm diagram fix) and the 2 P3 anchor/path nits. Reviews: research/reviews/codex_041-validation-gate-wiring_gate_a_2_review.md, research/reviews/opus_041-validation-gate-wiring_gate_a_2_adversarial_review.md.
- Round 3 fix applied 2026-06-16 (fresh loop instance after round-3 exhaustion, user-approved): split overlap-witness row (c) — absent 52 → validator catch (373=1); present-but-malformed 52 → field_type collapses to String, no value check → existing reason=10 SendingTime path (not validate-first Reject). Design-neutral; closes the round-3 Opus P2. Review: research/reviews/opus_041-validation-gate-wiring_gate_a_3_adversarial_review.md.

### Round 1 — dispositions

- **RC-A (Codex #1 P1 + Opus New-1 P1)** — EDITED. `dict::field_type` (7-value) and `dict::table_view` are both NEW production types (lived only in the test mock); plan/research/data-model/contracts now define their production home (`include/fixpp/dict/field_type.hpp`, `table_view.hpp`), `validator.hpp` gets a real complete-type include (not mock-include-order), + a compile-witness TU. plan.md's prior "validator.hpp unchanged surface" claim corrected.
- **RC-B (Codex #2 P1 + Opus New-6 P3)** — EDITED. `on_inbound_frame` is a per-state `switch` with no shared insertion point; FR-003/FR-004, R-2/R-4, data-model flow, contracts C-2/C-3 rewritten to per-arm insertion in the processing states (NotConnected/LogonSent/LogonReceived/Active) before each seqnum gate, preserving LogoutSent/Disconnected drain + the 35=3/35=5 no-reject-loop exemption. New-6 (per-arm MessageView build) folded in.
- **Codex #3 P1 (Article VI)** — EDITED. `## Normative References` added to spec.md; Constitution Check Article VI now legitimately PASS.
- **RC-C (Codex #4/#5 P2 + Opus New-2 P2)** — EDITED. `build_reject` already carries `ref_tag_id`+`session_reject_reason`/emits 371/373 (R-3 was FALSE) → corrected to extend the caller `emit_session_reject_`. Reason map made faithful + identical across all 6 files: group-structure failures → reason 1 (no distinct group reason Phase-1); reason 6 narrowed to Float/decimal precision-loss ONLY; reason 5 witness pinned to the type arm (enum arm dead Phase-1). FR-004/SC-003 now enumerate reason 6.
- **Opus New-3 P2 (FIXT two-dictionary)** — EDITED. Documented as a bounded Phase-1 limitation (session-dictionary-only; app-dict-by-DefaultApplVerID deferred) in spec Out-of-Scope + a new Clarifications 2026-06-16 block + research R-4; B&L row to be added at Polish.
- **Opus New-4 P2 (enum-value parity caveat)** — EDITED. Enum caveat moved inline to US1 title / SC-002 / SC-003 (was only in edge-cases).
- **Codex #6 P2 → P3 (start() blast radius)** — EDITED (number only). "~25 test sites" → "dozens of test/interop call sites + the `fx.start()` fixture wrapper"; migration task noted. Load-bearing claims (zero prod callers, no C-ABI wrapper) verified TRUE; no design change.
- **Codex #7 P2 → P3 (C-5 config error)** — EDITED. Named `core::error::invalid_session_config` (slot 53) in C-5 + E-1 + FR-011 + R-6; noted distinct from `session_invalid_argument(119)` dup-id error.
- **Codex #8 P2 → P3 (QuickFIX path)** — EDITED (citation only, no design change). Path resolves at the **parent repo root** (`research/G19-fix-fpml-iso20022/` is the submodule); corrected to `reference-engines/quickfix-cpp/src/C++/Session.cpp:1218-1229` and the minimal validate-before-nextLogon excerpt quoted into spec.md + research.md so Gate A verifies without the external checkout.
- **Opus New-5 P3 (global field_type_of invariant)** — EDITED (framing only). R-1/E-2 softened from "well-defined / built once (settled)" to an unconfirmed cross-msg-type invariant; R-1a's confirm-at-implementation prerequisite retained.

### Round 2 — dispositions

- **Codex #1 P2 (RC-B Logon-arm ordering residual) / Opus New-1 P3 (data-model Logon-arm diagram)** — EDITED. Source-verified that both Logon arms run `interpret_logon()` (`admin_messages.cpp:217`) as the lead statement (silent Disconnect, NO `Reject`, on CompID/BeginString/MsgType failure), before `scan_frame_header`, the SendingTime guard (NotConnected `Reject(reason=10)` ~1825), `1137`/hydrate, and `check_inbound` (seqnum gate ~1890/~3440). Added a **validate-first** ordering contract (validation runs before `interpret_logon()` in `NotConnected`/`LogonSent`) to FR-003, the Assumptions block, research R-2/R-4, and contracts C-3; added an **overlap-precedence witness list** (governed by one rule — validation rejects iff the message violates the dictionary; a dict-clean message keeps its existing disposition, FR-010) to data-model + C-2, with rows (a) dict-invalid+BeginString-mismatch, (b) FIXT `1137`, (c) malformed/stale `52`, (d) CompID-authz, (e) non-Logon first message, (f) `35=3`/`35=5` exemption; and **restructured the data-model pseudocode into three arm-groups** (`NotConnected`/`LogonSent` validate-first; `LogonReceived`/`Active` validate-before-`check_inbound`; `LogoutSent`/`Disconnected` drain unchanged). Pre-establishment rejection is not a new security posture (NotConnected already emits a pre-establishment SendingTime `Reject(reason=10)`). No source edits; derivable from the existing clarified QuickFIX-parity decision — not a re-clarify.
- **Codex #2 P3 (normative-anchor exactness)** — EDITED. `spec.md`/research `## Normative References` now cite `[FIX50SP2 §2.1]` (Session-level error processing — the project anchor for `SessionRejectReason(373)`, consistent with `[2b §6.5]` rule 5) instead of a vague "taxonomy"; `[2d §4.5]/§6.1` → `[2d §4.5] / [2d §6.1]` (both prefixed; §4.5 + §6.1 both verified present in `.specify/2d-threading.md`) in spec.md AND research.md.
- **Codex #3 P3 (QuickFIX path wording) — REWORDED, not a design change.** Opus downgraded Codex's "false" framing: the file IS at `<parent-workspace-root>/reference-engines/quickfix-cpp/src/C++/Session.cpp:1218-1229` (verified to match the quoted excerpt), but that is the **parent workspace root, OUTSIDE this submodule subtree** (`research/G19-fix-fpml-iso20022/`) and therefore **not reachable from the submodule cwd**. spec.md:148 + research.md:95 reworded to state exactly that and to drop the "resolves at the parent root" phrasing; the quoted excerpt remains as the reviewable oracle so Gate A reasoning does not depend on the external checkout. plan.md's Round-1 disposition for this item is left as-is (historical record). Does not move the tally.
