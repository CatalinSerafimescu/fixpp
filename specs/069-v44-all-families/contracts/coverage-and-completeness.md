# Contract: Coverage selection & emitted-set completeness

069 exposes no new **runtime / C-ABI / Python / link-ABI** surface (the generated C++ header surface intentionally grows — see C6). Its "contracts" are the **build-time coverage control** and the **emitted-set completeness invariant** the codegen must honor. The per-message generated-builder shape contract is inherited unchanged from 067 (`067`'s `contracts/generated-builder.md`, G1–G9).

## C1 — Coverage selection control

- **Control**: CMake **`CACHE STRING`** `FIXPP_CODEGEN_V44_FAMILIES`, values `{all, official}` (`set_property(... STRINGS all official)`; configure-time `FATAL_ERROR` on any other value — fails before codegen runs), **default `all`**. Not `option()` (boolean; cannot carry the string domain).
- **Guarantee (official)**: emitting under `official` produces `v44/Builders.hpp` **byte-identical** to the pre-069 output (the 33 OFFICIAL builders). Verified by regenerating and diffing against the committed 067 golden/output. (FR-005, FR-007, SC-003)
- **Guarantee (all)**: emitting under `all` produces builders for every `msgcat='app'` FIX44 message except the present N-002/N-003 members {BE, BF} — the 33 OFFICIAL **plus** all other application families (**50 more; 83 total**). The 33 OFFICIAL builders are byte-identical to `official` output. (FR-001, FR-005)
- **Non-goal**: no runtime selection; the mode is fixed at build/codegen time. Selecting a mode never changes any runtime, C-ABI, or Python surface. (FR-012)

## C2 — Emitted-set completeness invariant

- **Invariant**: for the active mode, `{ MsgTypes for which build_<Msg> is emitted } == { mode's intended set }` — exact-set equality, no subset-pass, no silent extra. (FR-011)
  - `official` intended set: the 33 `kOfficial33` MsgTypes.
  - `all` intended set: `{ m ∈ FIX44 dict : msgcat='app' } \ { BE, BF }` (BW/BX/BY absent from FIX44) — cardinality **83**.
- **Enforcement**: the generalized completeness pin in **`tests/session/test_067_completeness.cpp`** (the REAL FR-011 emitted-set pin — today it hardcodes `builder_registry == {kExpectedOfficial33}` + `== 33U`, and REDs under default `all` until generalized; **NOT** `test_067_emit_builders_unit.cpp`, whose `visited == kOfficial33.size()` is only the N3-census vacuous-pass guard) fails on any drift (message present in the intended set but not emitted, or emitted but not intended).
- **Non-circular expected set (all mode)**: the pin computes its `all`-mode expected set from an **independent raw `FIX44.xml` census** — a pugixml/grep walk of `<message msgcat='app'>` minus the present exclusion members {BE, BF}, asserting cardinality **83** for the vendored dictionary — then compares that independent set to `builder_registry`. It MUST NOT re-derive the expected set from the same `VersionIR`/`build_ir` the emitter consumes: a mis-parsed/defaulted `msgcat` would drop from both sides and the pin would pass vacuously. Reuses the in-repo N3-census raw-pugixml precedent (`test_067_emit_builders_unit.cpp` lines 36/221). This is the guard against a dictionary revision silently changing coverage.

## C3 — Differential round-trip verification invariant

- **Invariant**: for **every** emitted application-message builder, `parse_runtime(build_<Msg>(seed)) ⊇ seed` — each seeded field, at each group level, reads back with its exact value through the independent runtime-XML parse path. (FR-009, SC-002)
- **Exclusions**: none within the emitted set — 100% of emitted builders are in the harness. A message that cannot round-trip is a **named failing test**, never a skipped/absent one. (spec Edge Cases)

## C4 — External-anchor invariant (non-tautology)

- **Invariant**: for each exemplar in the **fixed required set below**, `build_<Msg>(seed)` bytes **equal** the checked-in reference-engine (QuickFIX) golden bytes for that message + seed. (FR-010, SC-006)
- **Required exemplar set** (fixed — implementation MUST cover all of these; a smaller/easier subset does NOT satisfy FR-010). One newly-covered message per newly-covered family class + ≥1 group-heavy/nested case. Seed names are the golden fixture basenames under `golden/`:

  | Family class | MsgType | Golden seed name | Notes |
  |---|---|---|---|
  | Post-trade / TradeCapture (P-008) | `AE` TradeCaptureReport | `069_tradecapturereport_ae` | **group-heavy/nested** (NoSides / NoLegs) — the required nested case |
  | Position (C-002) | `AP` PositionReport | `069_positionreport_ap` | NoPositions group |
  | Collateral (C-001) | `BB` CollateralInquiry | `069_collateralinquiry_bb` | |
  | Reference data / SecurityList (A-025) | `y` SecurityList | `069_securitylist_y` | NoRelatedSym group |
  | Confirmation (P-005) | `AK` Confirmation | `069_confirmation_ak` | |
  | Registration (R-001) | `o` RegistrationInstructions | `069_registrationinstructions_o` | NoRegistDtls nested |
  | List-handling (A-019) | `N` ListStatus | `069_liststatus_n` | NoOrders group |
  | BusinessMessageReject (A-014) | `j` BusinessMessageReject | `069_businessmessagereject_j` | flat, ref-tag echo |

  This same fixed list is carried into `quickstart.md` (the `family_golden`-labelled run) and the `test_069_family_exemplar_golden.cpp` test names. Additional exemplars may be added but none of the above may be dropped.
- **Purpose**: an independent external oracle so a co-wrong builder+parser pair cannot pass C3. Full per-message external parity (all 83) is optional hardening, not required.

## C5 — Validator scope contract (carried limitation)

- **In**: required-field presence + type conformance (identical to 067's `validate_<Msg>`).
- **Out**: enum value-domain checks — unbacked, recorded as `L-069-*`. A validator accepting an out-of-domain enum value is **expected** behavior under this contract, not a defect. (FR-013, R8)

## C6 — No-regression / freeze invariants

- **No C-ABI / Python / runtime-link-ABI change.** C-ABI `capi_freeze.sha256` unchanged (no `include/fix/c_api.h` touch); Python bindings unchanged; the frozen runtime link surface is untouched. ABI no-regression checks target **`capi_freeze.sha256` / `c_api.h`**, NOT the absence of new C++ builder names.
- **The generated C++ header surface intentionally GROWS** — `v44/Builders.hpp` gains ~50 new public `build_<Msg>` / `validate_<Msg>` / `<Msg>Args` symbols in `fixpp::v44` (that is the feature's purpose). These stay **outside the frozen C ABI**: `Builders.hpp` remains a generated flyweight header, header-only typed convenience, not part of `capi_freeze.sha256`. (Corrects the earlier "no new public C++ symbol" claim, which contradicted FR-001.)
- Forced-regen of all 4 codegen namespaces stays git-clean under the `codegen-build-graph-check` gate.
