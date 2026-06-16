# Contracts: Validation Gate Wiring

These are the public/behavioural contracts the implementation must satisfy. Each is a test target.

## C-1 — `dict::table_view` production surface

The validator (`wire::dictionary_driven_validator`) binds `dict::table_view` by value and calls exactly:

```cpp
bool        field_valid_for(std::string_view msg_type, std::uint16_t tag) const noexcept;
std::span<std::uint16_t const> required_fields(std::string_view msg_type) const noexcept;
std::uint16_t group_first_field(std::uint16_t no_tag) const noexcept;
std::span<std::uint16_t const> group_member_tags(std::uint16_t no_tag) const noexcept;
fixpp::dict::field_type field_type_of(std::uint16_t tag) const noexcept;
bool        enum_valid(std::uint16_t tag, std::span<const std::byte> value) const noexcept; // Phase-1: always true
```

Both `fixpp::dict::table_view` AND the 7-value `fixpp::dict::field_type` enum it returns are **NEW production types** (today they exist only in `tests/support/mock_dict_table.hpp`). They MUST be defined in the dict layer (`include/fixpp/dict/{table_view,field_type}.hpp`) and included by `validator.hpp` for a complete type — not left dependent on the mock being included first. A **compile-witness TU** MUST instantiate `dictionary_driven_validator` from a production `table_view` (no mock) to prove the production path compiles.

**Contract**: a `table_view` produced by `Dictionary::as_table_view()` returns, for every msg_type/tag/group in the source `Dictionary`, results consistent with that `Dictionary` (validity, required set, group first/member tags, field type). `enum_valid` returns `true` for all inputs (Phase-1). All returned spans point at storage owned by the `table_view` and remain valid for its lifetime.

## C-2 — Inbound validate gate (behavioural)

Given a session with `validate_inbound_messages == true`, **in an inbound-processing state** (`NotConnected`/`LogonSent`/`LogonReceived`/`Active`):

- A header-out-of-order message → not dispatched; `Reject(35=3, 373=14)`; sequence number **not** advanced.
- An undefined-tag message → `Reject(35=3, 373=2)`.
- A required-field-missing message **OR a malformed repeating group** (delimiter misplacement / NumInGroup count mismatch) → `Reject(35=3, 373=1)` (the validator surfaces group-structure failures as `wire_required_field_missing`; no distinct group reason in Phase-1).
- A type-nonconformant value (type arm — e.g. non-numeric `Int`, multi-byte `Char`) → `Reject(35=3, 373=5)`. (The enum arm also yields slot 40 but is dead Phase-1; the reason-5 witness MUST use the type arm.)
- A Float/decimal precision-loss value → `Reject(35=3, 373=6)`. This is the ONLY input that yields reason 6 (`wire_field_value_truncated` fires only on the Float decimal-precision-loss remap — NOT a generic "truncated/bad-format" value).
- A fully conformant message → passes validation, proceeds through the existing guards, is dispatched.
- The establishing Logon is subject to the same validation; a dictionary-invalid Logon is rejected and the session is not established.
- **No-reject-loop exemption preserved**: a malformed inbound `Reject(35=3)` or `Logout(35=5)` is NOT Reject-looped (existing guard, kept under validation).

**Logon-arm ordering + overlap precedence.** In the Logon-bearing arms (`NotConnected`/`LogonSent`) validation runs **before `interpret_logon()`** and the establishment checks (validate-first), because `interpret_logon()` (`admin_messages.cpp:217`) is the lead statement and silently Disconnects (no `Reject`) on CompID/BeginString/MsgType failure. Governing rule: **validation rejects iff the message violates the dictionary; a dict-clean message keeps its existing disposition** (FR-010). Witness rows:

| inbound first/Logon message | disposition | winning gate |
|---|---|---|
| dict-invalid + BeginString mismatch | `Reject(35=3, 373=…)`, not established | validation (runs first) |
| missing/invalid FIXT `1137` (dict-valid) | existing `1137`/establishment disposition | existing check (semantic, not dict) |
| **absent** `52` SendingTime (dict marks `52` required for Logon) | `Reject(35=3, 373=1)` (required-field-missing), not established | validation (absent required field is a dict-structure violation) |
| **present-but-malformed** `52` value (e.g. `52=NOTATIME`), or **dict-valid-but-stale** `52` | existing reason=10 / Logout-only (038 SendingTime path) | existing check (`field_type_of(52)`→`String`, no value-format check; malformed or stale timestamp value undetectable to Phase-1 validator) |
| CompID-authz failure (dict-valid) | existing authz reject/disconnect | existing check (semantic, not dict) |
| well-formed non-Logon first message | dict-valid → passes → `interpret_logon` refuses → silent Disconnect | existing disposition (FR-010) |
| malformed inbound `35=3`/`35=5` | NOT Reject-looped; existing handling | no-reject-loop exemption (FR-004) |

Given the `LogoutSent`/`Disconnected` **drain** states: the validate gate does NOT run; non-Logout inbound is drained as before (no validation-induced reject).

Given `validate_inbound_messages == false` (default): the validate gate does not run in any state; behaviour is identical to the prior release for all the above inputs (no validation-induced reject).

## C-3 — Validation runs before the sequence-number gate

With validation enabled, a message that is **both** structurally invalid **and** out-of-sequence is rejected for the validation failure (C-2 reason), and the sequence-number gate does not process/advance it. (Ordering parity with QuickFIX.) The gate is inserted **per-arm before each processing state's seqnum gate** — and, in the `NotConnected`/`LogonSent` Logon arms, **before `interpret_logon()` and the establishment checks** (validate-first, per the C-2 ordering rows) — there is no single shared insertion point in `on_inbound_frame` (it is a per-state `switch`).

## C-4 — `Engine::start()` clock gate

```cpp
[[nodiscard]] fixpp::core::expected_t<void> Engine::start();
```

- `start()` on a config with `clock == nullptr` → returns `error::clock_not_set`; the engine does not become operational; no session loops are spawned.
- `start()` on a config with a valid clock → returns success; engine operates as before (no behaviour change vs prior release).

## C-5 — Config fail-closed

`register_session` with `validate_inbound_messages == true` and `dictionary == nullptr` → returns `core::error::invalid_session_config` (slot 53, `error.hpp:148`) via its `expected_t<void>` result (does not silently disable validation). This is distinct from the `session_invalid_argument(119)` it returns on a duplicate session id.
