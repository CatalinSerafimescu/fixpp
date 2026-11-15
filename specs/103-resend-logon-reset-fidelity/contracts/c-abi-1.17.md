# Contract: C ABI 1.17 — the default reset policy (BREAKING, Article X §7)

**Surface**: `include/fix/c_api/session.h` (the declarations under "Marks"), `include/fix/c_api/version.h`
(`FIXPP_C_ABI_VERSION_MINOR` and its 1.17 paragraph).

## Change

| Item | 1.16 | 1.17 |
|---|---|---|
| `FIXPP_C_ABI_VERSION_MINOR` | 16 | 17 |
| Default when the setter is never called | `FIXPP_RESET_SEQNUM_BILATERAL_STRICT` | `FIXPP_RESET_SEQNUM_BILATERAL_LENIENT` |
| Enum values | STRICT = 0, LENIENT = 1, UNILATERAL = 2 | unchanged |
| Symbols, types, error codes | — | unchanged |

Proof that nothing but the documented result changes (tasks T035): `git diff origin/main...HEAD -- include/fix`
changes only comment lines and the `FIXPP_C_ABI_VERSION_MINOR` definition (read the hunks: a declaration, a
signature or an error-code line stops); `capi_pure_tests`' `abi_symbol_golden_test` passes unchanged, so the symbol
set has no delta; `tools/check_capi_occupancy.sh` and the introducing-minor table (`tools/abi_history/error_codes_v1.txt`)
show no new code; `tools/capi_freeze.sha256` is re-recorded by the procedure in `tools/check_capi_freeze.sh`'s
header, after that check is seen failing on the edited headers first (its known positive).

## Why it is BREAKING

A consumer that never calls the setter now sees a different documented result (§7: "sees a call it could already
make … return a result other than the one documented"):
- an initiator sends no 141=Y unless a reset setting is on and both numbers are 1;
- an acceptor accepts a Logon without 141=Y.

Reached through `fixpp_session_*` for every consumer (as 102 listed for 1.16), also new in 1.17: a strict session
resets before every Logon and a strict acceptor's refusal carries a Logout with Text; BeginSeqNo 0 is answered as 1
and an absent BeginSeqNo is Rejected; a too-high ResendRequest is serviced and a too-high Logout acknowledged within
`logout_disconnect_timeout_ms`; outbound frames are ordered around a resend answer; an unfilled gap ends at the next
non-advancing frame after 2 × HeartBtInt; every too-low frame without PossDupFlag draws a Logout with Text.

## Marks (Article X §7: "marked **BREAKING** in the documentation of each affected declaration")

Amended at Gate A round 1 (N3). The list follows from the behaviour changes, one row per change (research D7's table
is the source; re-derive it from the changes, not from this list), on the 1.12 model, where every observer carries a
per-call clause:

| Declaration (`session.h`) | Its 1.17 clause names |
|---|---|
| `fixpp_reset_seqnum_policy` | the default moves to LENIENT (an initiator's Logon no longer carries 141=Y, at 34=1 either way on the in-memory store a C-ABI session runs on, the premise of every "no reach as shipped" below: it holds while `command grep -n store_factory src/capi` finds only `MemoryStoreFactory` and `session.h` declares no store setter, and a later store setter invalidates those clauses visibly); a STRICT acceptor refuses a Logon without 141=Y with a Logout with Text, in or out of sequence; STRICT's reset before every Logon has no reach as shipped (the in-memory store's numbers are already 1); the enumerator descriptions match data-model.md's policy table |
| `fixpp_session_config_set_reset_seqnum_policy` | the default it overrides; the migration below |
| `fixpp_session_config_create` | the config it creates carries LENIENT |
| `fixpp_session_config_set_reset_on_logon` | a strict acceptor with the knob no longer resets for a Logon it refuses; no reach as shipped (in-memory store at 1), stated as 1.12 states its no-reach cases |
| `fixpp_session_is_established` | true where a Logon without 141=Y was refused; turns false at a too-low Heartbeat, at the gap deadline on any non-advancing frame, and at a too-high Logout's acknowledgement |
| `fixpp_session_close` | `FIXPP_ERR_OK` where 1.16 returned `FIXPP_ERR_THREAD_SESSION_LIFECYCLE`, which it returns for a session that never established: a default-config acceptor's session whose Logon without 141=Y 1.16 refused (strict) now establishes; a close while a too-high Logout is pending sends the close's own Logout; a graceful close waits for a running resend answer, and for a claimed acknowledgement, inside the logout timeout, and sends no Logout when that timeout runs out first |
| `fixpp_session_send` | returns the invalid-state code while a too-high Logout is pending, where it returned `FIXPP_ERR_OK`; may return after a resend answer, and returns the invalid-state code if the session closes, stops or receives such a Logout meanwhile |
| `fixpp_session_register_callback` | messages are delivered to a default session that was refused before, and no longer after the endings named for `fixpp_session_is_established` |
| `fixpp_session_register_send_callback` | the same, for the outbound callback |

- In each of the five observers, the 1.12 sentences 103 falsifies ("the next too-high frame that neither ends the
  session by another check nor advances the expected number draws a Logout"; "a too-high frame that reaches the
  sequence check while a resend is outstanding is ignored", not for a ResendRequest or a Logout) are amended in
  place, marked "(amended in C-ABI 1.17, 103: …)", as 1.11 and 1.12 amended 1.10's. Sites, by recipe:
  `command grep -n 'neither ends the session by another check' include/fix/c_api/session.h` and
  `command grep -n 'outstanding is ignored' include/fix/c_api/session.h`, each hit read; known positive:
  `fixpp_session_is_established`'s clause carries both. The amendments read: for the first, "(amended in C-ABI
  1.17, 103: any frame that neither ends the session by another check nor advances the expected number, at, below
  or above it, draws that Logout)"; for the second, "(amended in C-ABI 1.17, 103: except a ResendRequest, which is
  answered, and a Logout, which is acknowledged after a bounded recovery)".
- The `(default)` marker moves from STRICT to LENIENT.
- A 1.17 paragraph in `version.h`'s version comment, one line per change naming the declarations that carry it,
  and the counterparty's side with no carrying declaration: 7=0 answered as 7=1 and an absent 7 Rejected; a too-high
  ResendRequest serviced; every too-low frame without 43=Y draws a Logout with Text; outbound frames ordered around
  a resend answer; #578's clear (no reach: the C ABI has no `drive_reconnect`).
- The PR description and the B&L delta (B-103-1) say BREAKING.

## Migration

To keep a reset on every Logon (now conforming: 34=1), call
`fixpp_session_config_set_reset_seqnum_policy(cfg, FIXPP_RESET_SEQNUM_BILATERAL_STRICT)`. A consumer that already
sets STRICT sees its 1.17 meaning too: a durable reset of both numbers to 1 before every Logon (no reach on the
in-memory store, where they are already 1), and a refusal of a Logon without 141=Y that sends a Logout with Text,
in or out of sequence, where 1.16 disconnected silently.

## Witnesses

`CapiVersion.CApiVersionIsExactly_1_17_0` (renamed from `_1_16_0`), `CapiVersion.CompositeMacroValue`, a C-ABI cell
asserting the default pair's Logon (no 141, 34 = stored), a C-ABI cell for `fixpp_session_send`'s refusal while a
too-high Logout is pending, and the existing strict-naming cells in `tests/capi/public_roundtrip_test.cpp`.

Per mark: `fixpp_reset_seqnum_policy`, `fixpp_session_config_set_reset_seqnum_policy` and
`fixpp_session_config_create`, the default pair (tasks T024); `fixpp_session_is_established` true for a Logon
without 141=Y, and `fixpp_session_close`'s `FIXPP_ERR_OK` for that session, T024; `fixpp_session_is_established`
false at a too-low Heartbeat, the flipped `CapiC7Witness.Row6_*` cells (T150); `fixpp_session_send`'s refusal, T077.
Witnessed by C++ cells only (no C-ABI cell): the gap deadline at a non-advancing frame (T135), the graceful close's
wait for an answer and for a claimed acknowledgement (R-588-d, R-588-e), the strict acceptor's Logout (T022 AS3),
and the delivery changes of `fixpp_session_register_callback` and `fixpp_session_register_send_callback`, which
follow the establishment and ending changes above. `fixpp_session_config_set_reset_on_logon`'s clause has no reach
as shipped and no cell.
