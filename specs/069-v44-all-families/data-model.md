# Phase 1 Data Model: v44 all-families typed codegen coverage

This feature adds no runtime data types. The "data model" here is the **codegen IR extension** and the **coverage-selection model** that drive emission. No public C++ / C-ABI / Python type changes (FR-012).

## Entity: `MessageIR.is_application` (IR extension)

- **Where**: `tools/codegen/fixpp-codegen/ir.hpp` (field) + `ir.cpp` (population).
- **Shape**: `bool is_application` (or `enum class MsgCategory { Admin, App }`), one per message.
- **Source**: the `<message … msgcat='app'|'admin'>` attribute in the version XML.
- **Validation**: every `<message>` in a FIX44 dictionary carries `msgcat`; a message missing it is a loader error (**fail-closed — do not default-guess category**; a default-to-app or default-to-admin would be exactly the silent-drop/silent-add surface the completeness pin exists to catch). **Pinned by a named regression test** — the msgcat fail-closed witness added to `tests/codegen/test_067_emit_builders_unit.cpp`: a synthetic `<message>` lacking `msgcat` must make `build_ir` reject the dictionary (fail-closed, no default-guess), mirroring the existing synthetic-XML discriminating-witness pattern already in that file (line ~331).
- **Consumers**: `emit_builders.cpp` selection predicate (R1). No other emitter needs it (they are already unfiltered, R4).

## Entity: Coverage mode

- **Values**: `all` (default) | `official`.
- **Flow**: CMake `CACHE STRING` `FIXPP_CODEGEN_V44_FAMILIES` (`STRINGS all official`; configure-time `FATAL_ERROR` on any other value) → `cmake/Codegen.cmake` → `fixpp-codegen --families <mode>` → `main.cpp` → `emit_builders(ir, mode)`.
- **Semantics**:
  - `official`: emit iff `msg_type ∈ kOfficial33` → byte-identical to today (SC-003 / FR-005).
  - `all`: emit iff `is_application && msg_type ∉ {BE, BF, BW, BX, BY}` (N-002/N-003 exclusion). In FIX44 only BE/BF are present (BW/BX/BY absent → no-op), so this yields **83** = 85 app − 2.
- **Scope**: applies to the `v44` namespace only; other namespaces emit no builders regardless of mode (FR-004, unchanged `ir.ns != "v44"` gate).

## Entity: N-002/N-003 exclusion set

- **Members**: `BE` (UserRequest), `BF` (UserResponse), `BW` (ApplicationMessageRequest), `BX` (ApplicationMessageRequestAck), `BY` (ApplicationMessageReport). **In FIX44 only BE and BF exist**; BW/BX/BY are FIX 5.0 messages, absent from the vendored FIX44 dictionary — their presence in the exclusion set is a harmless no-op (keeps the set forward-compatible if a newer dict is vendored).
- **Reason**: session-FSM-dispatch messages, a different work class; remain the separate v1.0-tagging gate (FR-003). They are `msgcat='app'`, so they are NOT auto-excluded by the app/admin filter — this explicit set removes them.
- **Representation**: a small `constexpr` array in `emit_builders.cpp` alongside (not replacing) `kOfficial33`.

## Entity: Intended emitted set + completeness pin

- **`all` intended set**: the FIX44 dictionary's `msgcat='app'` messages minus the present N-002/N-003 members {BE, BF}. Cardinality **83** = 85 app − 2 (the spike's msgtype-based 86 counted XMLnonFIX + BE/BF).
- **`official` intended set**: exactly the 33 `kOfficial33` entries.
- **Completeness pin** — **`tests/session/test_067_completeness.cpp`** (the REAL FR-011 emitted-set pin; NOT `test_067_emit_builders_unit.cpp`, whose `visited == kOfficial33.size()` is only the N3-census vacuous-pass guard). Today it hardcodes `builder_registry == {kExpectedOfficial33}` + `builder_registry.size() == 33U`; under the default `all` build the registry is 83 ≠ 33, so this test **REDs unless generalized**. Generalize it to assert the emitted set equals the **mode's intended set** — no silent drop, no silent extra (FR-011). Under `official` it still pins 33; under `all` it pins the app-minus-{BE,BF} set.
- **Non-circular all-mode expected set**: the `all`-mode expected set MUST be computed by an **independent raw `FIX44.xml` census** (a small pugixml/grep walk of `<message msgcat='app'>` minus the present exclusion members {BE, BF}), asserting cardinality **83** for the vendored dictionary, then compared against `builder_registry`. It MUST NOT be re-derived from the same `VersionIR`/`build_ir` the emitter consumes — if a `msgcat` were mis-parsed/defaulted it would drop from both sides and the pin would pass vacuously (the circularity Codex/Opus flagged). This reuses the in-repo N3-census raw-pugixml precedent (`tests/codegen/test_067_emit_builders_unit.cpp` lines 36/221).

## Entity: Generated artifacts per newly-covered message (unchanged shapes from 067)

For each newly in-scope message the emitter produces the SAME shapes it already emits for the 33 (contract `contracts/generated-builder.md` from 067, reused verbatim):
- `<Msg>Args` struct (+ nested `<Msg>…Args` for each group level).
- `build_<Msg>(std::span<std::byte> out, <Msg>Args const&) noexcept → expected_t<std::span<std::byte>>`.
- `validate_<Msg>` + `writer_traits<Msg>` over the already-emitted `_rules`/`FieldRef` tables (required-presence + type conformance; enum-domain out of scope, R8).
- Registry entry in the generated `builder_registry`.

Read-back (`Messages.hpp` view + `dict::reify`) is **already generated** for every message (R4) — not re-emitted here.

## State / lifecycle

None. Codegen is a pure function of `(VersionIR, coverage-mode)`; no runtime state, no transitions.
