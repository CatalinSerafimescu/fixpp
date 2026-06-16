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

**Contract**: a `table_view` produced by `Dictionary::as_table_view()` returns, for every msg_type/tag/group in the source `Dictionary`, results consistent with that `Dictionary` (validity, required set, group first/member tags, field type). `enum_valid` returns `true` for all inputs (Phase-1). All returned spans point at storage owned by the `table_view` and remain valid for its lifetime.

## C-2 — Inbound validate gate (behavioural)

Given a session with `validate_inbound_messages == true`:

- A header-out-of-order message → not dispatched; `Reject(35=3, 373=14)`; sequence number **not** advanced.
- An undefined-tag message → `Reject(35=3, 373=2)`.
- A required-field-missing message → `Reject(35=3, 373=1)`.
- A type-nonconformant value → `Reject(35=3, 373=5)`.
- A truncated/format-invalid value → `Reject(35=3, 373=6)`.
- A fully conformant message → passes validation, proceeds through the existing guards, is dispatched.
- The establishing Logon is subject to the same validation; a dictionary-invalid Logon is rejected and the session is not established.

Given `validate_inbound_messages == false` (default): the validate gate does not run; behaviour is identical to the prior release for all the above inputs (no validation-induced reject).

## C-3 — Validation runs before the sequence-number gate

With validation enabled, a message that is **both** structurally invalid **and** out-of-sequence is rejected for the validation failure (C-2 reason), and the sequence-number gate does not process/advance it. (Ordering parity with QuickFIX.)

## C-4 — `Engine::start()` clock gate

```cpp
[[nodiscard]] fixpp::core::expected_t<void> Engine::start();
```

- `start()` on a config with `clock == nullptr` → returns `error::clock_not_set`; the engine does not become operational; no session loops are spawned.
- `start()` on a config with a valid clock → returns success; engine operates as before (no behaviour change vs prior release).

## C-5 — Config fail-closed

`register_session` with `validate_inbound_messages == true` and `dictionary == nullptr` → returns a config error (does not silently disable validation).
