# Phase 1 Data Model: v44 all-families typed codegen coverage

This feature adds no runtime data types. The "data model" here is the **codegen IR extension** and the **coverage-selection model** that drive emission. No public C++ / C-ABI / Python type changes (FR-012).

## Entity: `MessageIR.is_application` (IR extension)

- **Where**: `tools/codegen/fixpp-codegen/ir.hpp` (field) + `ir.cpp` (population).
- **Shape**: `bool is_application` (or `enum class MsgCategory { Admin, App }`), one per message.
- **Source**: the `<message … msgcat='app'|'admin'>` attribute in the version XML.
- **Validation**: every `<message>` in a FIX44 dictionary carries `msgcat`; a message missing it is a loader error (fail-closed — do not default-guess category).
- **Consumers**: `emit_builders.cpp` selection predicate (R1). No other emitter needs it (they are already unfiltered, R4).

## Entity: Coverage mode

- **Values**: `all` (default) | `official`.
- **Flow**: CMake cache option `FIXPP_CODEGEN_V44_FAMILIES` → `cmake/Codegen.cmake` → `fixpp-codegen --families <mode>` → `main.cpp` → `emit_builders(ir, mode)`.
- **Semantics**:
  - `official`: emit iff `msg_type ∈ kOfficial33` → byte-identical to today (SC-003 / FR-005).
  - `all`: emit iff `is_application && msg_type ∉ {BE, BF, BW, BX, BY}` (N-002/N-003 exclusion).
- **Scope**: applies to the `v44` namespace only; other namespaces emit no builders regardless of mode (FR-004, unchanged `ir.ns != "v44"` gate).

## Entity: N-002/N-003 exclusion set

- **Members**: `BE` (UserRequest), `BF` (UserResponse), `BW` (ApplicationMessageRequest), `BX` (ApplicationMessageRequestAck), `BY` (ApplicationMessageReport).
- **Reason**: session-FSM-dispatch messages, a different work class; remain the separate v1.0-tagging gate (FR-003). They are `msgcat='app'`, so they are NOT auto-excluded by the app/admin filter — this explicit set removes them.
- **Representation**: a small `constexpr` array in `emit_builders.cpp` alongside (not replacing) `kOfficial33`.

## Entity: Intended emitted set + completeness pin

- **`all` intended set**: the FIX44 dictionary's `msgcat='app'` messages minus the N-002/N-003 set. Measured cardinality **81** (spike measured 86 before excluding N-002/N-003).
- **`official` intended set**: exactly the 33 `kOfficial33` entries.
- **Completeness pin** (`tests/codegen/test_067_emit_builders_unit.cpp`): the existing exact-set assertion (`visited == kOfficial33.size()`) is generalized to assert the emitted set equals the **mode's intended set** — no silent drop, no silent extra (FR-011). Under `official` it still pins 33; under `all` it pins the dictionary-derived app-minus-exclusions set (computed from the same IR, not a second hardcoded list).

## Entity: Generated artifacts per newly-covered message (unchanged shapes from 067)

For each newly in-scope message the emitter produces the SAME shapes it already emits for the 33 (contract `contracts/generated-builder.md` from 067, reused verbatim):
- `<Msg>Args` struct (+ nested `<Msg>…Args` for each group level).
- `build_<Msg>(std::span<std::byte> out, <Msg>Args const&) noexcept → expected_t<std::span<std::byte>>`.
- `validate_<Msg>` + `writer_traits<Msg>` over the already-emitted `_rules`/`FieldRef` tables (required-presence + type conformance; enum-domain out of scope, R8).
- Registry entry in the generated `builder_registry`.

Read-back (`Messages.hpp` view + `dict::reify`) is **already generated** for every message (R4) — not re-emitted here.

## State / lifecycle

None. Codegen is a pure function of `(VersionIR, coverage-mode)`; no runtime state, no transitions.
