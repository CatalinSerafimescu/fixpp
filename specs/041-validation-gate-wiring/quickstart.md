# Quickstart: Validation Gate Wiring

## Enable strict inbound validation (opt-in, per session)

```cpp
SessionConfig cfg = /* ... */;
cfg.dictionary = my_dictionary;          // required when validation is on
cfg.validate_inbound_messages = true;    // default is false (lenient, current behaviour)
engine.register_session(std::move(cfg)); // rejects if validate_inbound_messages && !dictionary
```

When enabled, each inbound message (including the establishing Logon) is validated against
`cfg.dictionary` **before** the sequence-number gate. A dictionary violation produces a session
`Reject(35=3)` with the QuickFIX-parity `SessionRejectReason(373)`:

| Violation | 373 |
|---|---|
| Header field out of order | 14 |
| Tag not defined for message type | 2 |
| Required field missing | 1 |
| Value incorrect / out of range | 5 |
| Value truncated / bad format | 6 |

Enum-value conformance is **not** checked in this release (Phase-1; deferred to the 2c enum tables) —
a wrong enum constant of the correct type is accepted even with validation enabled.

## Default (no change)

If `validate_inbound_messages` is left `false`, inbound handling is byte-for-byte identical to the
prior release: out-of-order fields, undefined tags, etc. are accepted as before, and no validator is
constructed or invoked.

## Engine clock gate

`Engine::start()` now returns `expected_t<void>`:

```cpp
auto r = engine.start();
if (!r) {
    // r.error() == fixpp::core::error::clock_not_set  when EngineConfig::clock is null
}
```

A valid clock starts the engine unchanged; a null clock is refused before any session loop spawns.

## Verify

- Default-off no-op: run the existing session/interop corpus with default config → outcomes unchanged (SC-001).
- Each violation class → expected `373` reason (SC-002 / SC-003).
- Validation-before-seqnum ordering (C-3); malformed Logon rejected (C-2).
- Clock gate: null clock → `clock_not_set`; valid clock → ok (SC-004).
