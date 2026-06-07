# Contract: Validation-compat toggles (028)

Interface surface for the two additive knobs. No C-ABI surface, no error slot, no
wire field — the only public contract change is two `SessionConfig` fields.

## C1 — `SessionConfig::check_comp_id`

```cpp
// include/fixpp/session/session_config.hpp
bool check_comp_id = true;
```

- **C1.1** Default `true` ⇒ strict: post-Logon inbound messages MUST satisfy
  `header.SenderCompID(49) == cfg.target_comp_id && header.TargetCompID(56) ==
  cfg.sender_comp_id`; a mismatch disconnects the session (unchanged from today).
- **C1.2** `false` ⇒ the `49`/`56` equality compare is skipped for post-Logon
  traffic; mismatching-CompID messages are accepted and delivered.
- **C1.3** `false` MUST NOT relax `BeginString(8)` validation, MUST NOT make
  `49`/`56` optional, MUST NOT change the Logon-establishment CompID check, and
  MUST NOT bypass the 013 `compid_authorization_policy` allow-list.

## C2 — `SessionConfig::validate_sequence_numbers`

```cpp
// include/fixpp/session/session_config.hpp
bool validate_sequence_numbers = true;
```

- **C2.1** Default `true` ⇒ strict: a too-high post-Logon `MsgSeqNum` enters
  recovery and emits a `ResendRequest`; a too-low `MsgSeqNum` is handled by the
  existing too-low disposition (fatal / PossDup). Unchanged from today.
- **C2.2** `false` ⇒ a too-high `MsgSeqNum` MUST NOT emit a `ResendRequest` or
  enter AwaitingResend; a too-low `MsgSeqNum` MUST NOT disconnect; the frame is
  delivered via the existing `parse_and_dispatch_` path, discriminated by the
  existing `is_admin_msgtype` helper: an **admin** message type → `fromAdmin`, an
  **application** message type → `fromApp` (matching the in-sequence dispatch
  fan-out). **Carve-out (pre-existing, unchanged):** a too-low `Heartbeat(35=0)` is
  silently dropped (`session.cpp:2234`) and is NOT delivered even with the knob off
  — this is retained, out of the knob's intent, and is the one exception to "the
  frame is delivered".
- **C2.3** `false` ⇒ the inbound next-expected counter advances ONLY when
  `MsgSeqNum` equals the currently-expected value; an out-of-order frame is
  delivered but does NOT advance it.
- **C2.4** `false` ⇒ PossDup(43)-flagged frames still flow through the existing
  duplicate handling (021); only the too-high/too-low gap checks are skipped.
- **C2.5** `false` ⇒ an unparseable `MsgSeqNum` (parse → 0) is STILL fatal
  (malformed, not merely out-of-order).
- **C2.6** Both knobs apply to post-Logon (`LogonReceived`/`Active`) traffic only;
  Logon establishment sequence/reset handling is unchanged.
- **C2.7** `false` ⇒ an inbound `SequenceReset(35=4)` — BOTH reset-mode
  (`GapFillFlag≠Y`, intercept `session.cpp:1966`) and gapfill-mode (`123=Y`,
  intercept `:2294`) — MUST NEVER apply its `NewSeqNo(36)`: the intercept is
  bypassed so `apply_inbound_sequence_reset` is NOT called; the frame is delivered
  to `fromAdmin` (admin msgtype) (QFJ-parity, `Session.java:1543`/`:1550`). The
  counter advance then follows the ordinary per-mode/ordering rule: reset-mode
  (intercepted before the seqnum gate) and any out-of-order `35=4` ⇒ counter
  **unchanged**; an **exact-match gapfill `35=4`** passes `check_inbound` first ⇒
  counter **advances by one** via the ordinary exact-match path, after which the
  gapfill intercept is bypassed (`NewSeqNo` still not applied). The +1 is a fixpp
  ordering artifact (the gapfill intercept sits after `check_inbound`), not a
  QFJ-parity claim. The shared `apply_inbound_sequence_reset` is UNCHANGED (still
  used by the strict 013/024/027 paths); only whether it is invoked is gated. At
  default (`true`) the `35=4` applies `NewSeqNo` as today.

## C3 — Invariants for both

- **C3.1** Independence: each knob may be set without the other; all four
  combinations are valid.
- **C3.2** Inbound-only: neither knob affects any outbound message construction.
- **C3.3** Byte-identical default: both `true` ⇒ behaviour byte-identical to the
  pre-feature baseline on every path.
- **C3.4** Additive C++-only value-type fields ⇒ struct-layout change requiring a
  normal source rebuild; no C-ABI surface, no error slot.
