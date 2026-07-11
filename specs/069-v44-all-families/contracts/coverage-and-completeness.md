# Contract: Coverage selection & emitted-set completeness

069 exposes no new runtime/API/ABI surface. Its "contracts" are the **build-time coverage control** and the **emitted-set completeness invariant** the codegen must honor. The per-message generated-builder shape contract is inherited unchanged from 067 (`067`'s `contracts/generated-builder.md`, G1–G9).

## C1 — Coverage selection control

- **Control**: CMake cache option `FIXPP_CODEGEN_V44_FAMILIES`, values `{all, official}`, **default `all`**.
- **Guarantee (official)**: emitting under `official` produces `v44/Builders.hpp` **byte-identical** to the pre-069 output (the 33 OFFICIAL builders). Verified by regenerating and diffing against the committed 067 golden/output. (FR-005, FR-007, SC-003)
- **Guarantee (all)**: emitting under `all` produces builders for every `msgcat='app'` FIX44 message except the N-002/N-003 set — the 33 OFFICIAL **plus** all other application families (~48 more; 81 total). The 33 OFFICIAL builders are byte-identical to `official` output. (FR-001, FR-005)
- **Non-goal**: no runtime selection; the mode is fixed at build/codegen time. Selecting a mode never changes any runtime, C-ABI, or Python surface. (FR-012)

## C2 — Emitted-set completeness invariant

- **Invariant**: for the active mode, `{ MsgTypes for which build_<Msg> is emitted } == { mode's intended set }` — exact-set equality, no subset-pass, no silent extra. (FR-011)
  - `official` intended set: the 33 `kOfficial33` MsgTypes.
  - `all` intended set: `{ m ∈ FIX44 dict : m.is_application } \ { BE, BF, BW, BX, BY }`, computed from the same `VersionIR` the emitter consumes (NOT a second hardcoded list).
- **Enforcement**: the generalized completeness pin in `tests/codegen/test_067_emit_builders_unit.cpp` fails on any drift (message present in the intended set but not emitted, or emitted but not intended). This is the guard against a dictionary revision silently changing coverage.

## C3 — Differential round-trip verification invariant

- **Invariant**: for **every** emitted application-message builder, `parse_runtime(build_<Msg>(seed)) ⊇ seed` — each seeded field, at each group level, reads back with its exact value through the independent runtime-XML parse path. (FR-009, SC-002)
- **Exclusions**: none within the emitted set — 100% of emitted builders are in the harness. A message that cannot round-trip is a **named failing test**, never a skipped/absent one. (spec Edge Cases)

## C4 — External-anchor invariant (non-tautology)

- **Invariant**: for each exemplar-per-family representative, `build_<Msg>(seed)` bytes **equal** the checked-in reference-engine (QuickFIX) golden bytes for that message + seed. (FR-010, SC-006)
- **Purpose**: an independent external oracle so a co-wrong builder+parser pair cannot pass C3. Exemplar set spans the major family groups; full per-message external parity is optional hardening, not required.

## C5 — Validator scope contract (carried limitation)

- **In**: required-field presence + type conformance (identical to 067's `validate_<Msg>`).
- **Out**: enum value-domain checks — unbacked, recorded as `L-069-*`. A validator accepting an out-of-domain enum value is **expected** behavior under this contract, not a defect. (FR-013, R8)

## C6 — No-regression / freeze invariants

- C-ABI `capi_freeze.sha256` unchanged (no `include/fix/c_api.h` touch).
- No new public C++ symbol; `Builders.hpp` remains a generated flyweight header outside the frozen ABI.
- Forced-regen of all 4 codegen namespaces stays git-clean under the `codegen-build-graph-check` gate.
