# Contract: `Session::open()` FIXT serviceability guard extension

**Surface**: behavioral contract on the existing `Session::open()` (`expected_t<void>`-returning
awaitable). No signature change, no new symbol. This documents the **added failure condition**.

## Preconditions (unchanged)

`open()` is called on a constructed `Session` before establishment, as today.

## Contract (added clause)

> **C-042-1**: For a session with `cfg_.begin_string == "FIXT.1.1"` and a configured
> `cfg_.default_appl_ver_id`, `open()` MUST return `std::unexpected(error::invalid_session_config)`
> when the engine version registry cannot serve that configured version — i.e. when
> `app_version_registry_ == nullptr` **or** `!app_version_registry_->get(*cfg_.default_appl_ver_id).has_value()`.
> The failure MUST occur before any observable state mutation or wire emission (fail-closed).

(The `app_version_registry_ == nullptr` half and the `!default_appl_ver_id.has_value()` half are the
pre-existing FQ-1 clauses; C-042-1 adds the `registry-present-but-cannot-serve` half.)

## Role applicability

Role-agnostic — applies identically to `session_role::acceptor` and `session_role::initiator`
(spec FR-008; clarify 2026-06-17). No role gate.

## Non-goals / preserved behavior (MUST NOT change)

- **NG-1 (correctly-configured FIXT)**: when the registry serves the configured default, `open()`
  succeeds and establishment is byte-identical to the pre-042 baseline.
- **NG-2 (non-FIXT)**: when `begin_string != "FIXT.1.1"`, `open()` is byte-identical (the outer FIXT
  gate short-circuits before the new clause).
- **NG-3 (inbound peer-version reject stays live)**: the runtime inbound check on the
  **peer-advertised** `DefaultApplVerID(1137)` (033 FR-004a — `Reject(35=3, 371=1137, 373=5)`,
  `session.cpp:2186-2200`) is unchanged and still fires; C-042-1 governs only **this** side's own
  configured default at config-load.
- **NG-4 (no new surface)**: no new public wire field, error slot, config field, codegen output, or
  C-ABI symbol. Reuses `error::invalid_session_config` and `version_registry::get`.

## Error mapping

| Condition | Return |
|---|---|
| FIXT + configured default unserviceable by the engine registry | `std::unexpected(error::invalid_session_config)` |
| FIXT + configured default serviceable | proceeds (no change) |
| non-FIXT | proceeds (no change) |

## Witness obligations (Gate B / verify)

- **W1 (acceptor, RED-first)**: a FIXT **acceptor** whose registry lacks the dict for its configured
  `default_appl_ver_id` → `open()` returns `invalid_session_config`. Mutation: drop disjunct #3 ⇒ test
  fails (open succeeds).
- **W2 (initiator open-fail, role-agnostic)**: an isolated FIXT **initiator** whose registry lacks the
  dict for its configured `default_appl_ver_id` → identical fail-closed `invalid_session_config`
  (FR-008). Mutation-tested as a distinct obligation: drop disjunct #3 ⇒ this initiator witness fails.
  A symmetric-API claim with an unwitnessed arm is exactly the trap 033 fell into
  ([[feedback_symmetric_api_claim_unreachable_arm]]) — the initiator fail-closed arm MUST be witnessed
  directly, not inferred from W1 + "tests stay green".
- **W3 (serviceable, non-regression)**: a FIXT session with a serviceable configured default →
  `open()` succeeds (NG-1). Pin **both** role arms explicitly: a serviceable **acceptor** open()-success
  AND an isolated serviceable **initiator** open()-success. (`W1_FullRoundTrip` at
  `test_fixt_logon_establishment.cpp:690` already drives a serviceable initiator to Active, but the
  isolated initiator open()-success arm is still called out so the symmetric-API claim is directly
  witnessed on both sides.)
- **W4 (inbound non-deadness, SC-003)**: a **NEW** three-distinct-version-registry witness (NOT a reuse
  of the existing 033/038 inbound reject witness). The registry serves this side's own default (so
  `open()` succeeds) but does NOT serve the peer's advertised version — e.g. registry `{v44, v50sp2}`,
  own default = v44 (serviceable), peer Logon advertises a version the registry lacks (e.g. `1137="8"`
  = v50sp1) → still `Reject(35=3, 371=1137, 373=5)` at runtime (NG-3). Carry a mutation/non-deadness
  assertion that the inbound `373=5` path still fires. **The two inherited inbound witnesses (W3 @887,
  W_…Disconnected @1302) must be REWRITTEN so this side's own default is serviceable — NOT edited-green
  by dropping their inbound-reject assertions, which would silently erode merged 033 FR-004a coverage**
  (see research.md D-2 / D-2a).
