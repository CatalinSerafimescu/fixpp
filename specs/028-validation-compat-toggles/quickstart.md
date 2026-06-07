# Quickstart: Validation-compat toggles (028)

Two opt-in per-session knobs that relax inbound validation for QuickFIX-compat
counterparties. **Both default to strict** — leaving them unset changes nothing.

## Relax steady-state CompID matching

```cpp
fixpp::session::SessionConfig cfg;
cfg.sender_comp_id = "ME";
cfg.target_comp_id = "PEER";
// ... usual setup ...

cfg.check_comp_id = false;  // accept post-Logon messages whose 49/56 don't match
```

With `check_comp_id = false`, post-Logon messages whose `SenderCompID(49)` /
`TargetCompID(56)` do not equal the configured pair are accepted instead of
disconnecting the session. `BeginString(8)` is still enforced, the Logon-time
CompID check stays strict, and the 013 CompID authorization allow-list still
applies.

## Tolerate out-of-order sequence numbers

```cpp
cfg.validate_sequence_numbers = false;  // accept gaps / replays without recovery
```

With `validate_sequence_numbers = false`, a too-high post-Logon `MsgSeqNum` does
NOT trigger a `ResendRequest` and a too-low `MsgSeqNum` does NOT disconnect — the
message is delivered. The next-expected counter advances only on an exact-expected
match (out-of-order frames are delivered but do not move it). Exception: a too-low
`Heartbeat(35=0)` keeps the pre-existing silent-drop behaviour and is not delivered.
PossDup(43)-flagged duplicate handling still applies.

## Combine (independent)

```cpp
cfg.check_comp_id = false;
cfg.validate_sequence_numbers = false;
```

The two knobs are independent; any combination is valid.

## Caveats (see B&L L-028-*)

- `validate_sequence_numbers = false` disables gap detection/recovery — real gaps
  are silently accepted and messages may be processed out of order.
- `check_comp_id = false` removes the steady-state mis-routing guard — rely on the
  013 authorization allow-list + transport binding for security.
- Both relaxations are **steady-state only**; Logon establishment is unaffected.
