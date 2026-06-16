# Implementation Plan: Validation Gate Wiring

**Branch**: `041-validation-gate-wiring` | **Date**: 2026-06-16 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/041-validation-gate-wiring/spec.md`

## Summary

Wire two fully-implemented-but-unwired validation gates into production:

- **(A) Opt-in dictionary-driven inbound validation.** `wire::dictionary_driven_validator` exists and binds a `fixpp::dict::table_view` (6 checks: header order, unexpected tag, required field, field type, repeating-group, enum value), but **no production `table_view` exists** (only a test mock) and the validator is never called. We (1) realize a production `table_view` populated from the session's `dict::Dictionary` (5 of 6 inputs available today; `enum_valid()` returns `true` — Phase-1, enum tables 2c-deferred), and (2) insert a `validate()` gate into the inbound path **before the sequence-number gate** (QuickFIX parity), behind a new per-session opt-in flag defaulting **OFF**. Validation failures emit a session `Reject(35=3)` with the QuickFIX-parity `SessionRejectReason` and do not advance the sequence number.
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
**Scale/Scope**: 1 new production `table_view` realization + `Dictionary::as_table_view()` builder (dict layer); 1 new SessionConfig bool; 1 inbound validate-gate insertion + reason-code mapping in `Session`; `emit_session_reject_`/`build_reject` extended to carry a reason code; `Engine::start()` signature change + ~25 test/fixture call-site updates

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

- **Gate A trigger (Article XVII / §XVI.3)**: **REQUIRED** — touches inbound **wire-validation behaviour**, the **session FSM** inbound path, **error semantics** (new reject reasons on a new path), and a **public-API signature** change (`Engine::start()`). `/clarify` was run (mandatory per §XVI.3 for wire-format / session-FSM / error-semantics features — DONE, 4 decisions recorded). Gate A runs after this plan, before `/tasks`.
- **§XV.1 (no per-message hot-path heap alloc)**: the validator is already zero-heap-per-message (O(1) stack, ≤~600 B); the `table_view` owns its tables but is built **once at construction** (config-time), not per message. The early parse on the enabled path uses the existing inbound PMR arena (no global heap). Default path constructs neither. → **PASS** (re-verify the table_view build allocates only at setup).
- **§XV.9 (no `std::mutex` / std-sync in awaitable closures)**: inbound path stays on `fixpp::sync` primitives; the new flag is a plain `bool`; no new mutex. The new headers must be run through the §XV.9 corpus gate (extended in 039 US4). → **PASS (to be re-verified).**
- **§VIII (performance budgets)**: default path unchanged (SC-005). Enabled path adds one parse + one O(1) validate before the seqnum gate; an opt-in mode, measured but not on the default budget. The enabled path may parse the frame twice (validate + dispatch) unless the MessageView is threaded through — see research.md R-2; either way no alloc-budget violation. → **PASS** (enabled-path cost documented).
- **Layering (`tools/check_layers.py` / architecture.md)**: `table_view` realization + `as_table_view()` live in the **dict layer** (`fixpp::dict`); the validator is **wire layer** and already binds `dict::table_view`; `fixpp::session` already depends on both. No layer inversion. The Gate-B fixer must re-run `tools/check_layers.py` after the new headers land (per `feedback_gate_b_check_layers_post_fixer`). → **PASS (re-verify post-impl).**
- **§IX.1 coverage (lcov DA/BRDA)**: every new branch — each validator-failure→reject-reason arm, the default-off skip, the enabled-pass path, the clock-gate reject + success — MUST be covered (no new zero-hit lines). → **PASS (by the FR/SC witnesses).**
- **§X.1 frozen C-ABI**: `Engine::start()` is a C++ API; `src/capi/capi.cpp` exports no engine binding, so the C ABI is untouched. New error reasons are existing `core::error` / `SessionRejectReason` values, not new C-ABI codes. → **PASS.**
- **Article VI (Normative References)**: the spec must cite the `[2b §6.5.*]` validation rules + the FIX `SessionRejectReason` definitions; added in research.md / spec Normative References. → **PASS (to be added).**

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
├── table_view.hpp            # NEW — production realization of the 2c table_view value type
│                             #       (6-method surface the validator binds; owns its tables)
└── dictionary.hpp            # EDIT — add `as_table_view()` builder (the deferred method, dictionary.hpp:18)

include/fixpp/wire/
├── validator.hpp             # (unchanged surface — already binds table_view)
└── reject_reason_map.hpp     # NEW (or inline in session) — wire_* → SessionRejectReason mapping

include/fixpp/session/
├── session_config.hpp        # EDIT — new `bool validate_inbound_messages = false;` (after validate_sequence_numbers)
├── session.hpp               # EDIT — validate-gate hook declaration
└── engine.hpp                # EDIT — Engine::start() void → expected_t<void>

src/session/
├── session.cpp               # EDIT — insert validate gate before seqnum gate (enabled-only);
│                             #        extend emit_session_reject_/build_reject with a reason code
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
