# Contract: C ABI 1.17 — the default reset policy (BREAKING, Article X §7)

**Surface**: `include/fix/c_api/session.h` (`fixpp_reset_seqnum_policy`, `fixpp_session_config_set_reset_seqnum_policy`),
`include/fix/c_api/version.h` (`FIXPP_C_ABI_VERSION_MINOR`).

## Change

| Item | 1.16 | 1.17 |
|---|---|---|
| `FIXPP_C_ABI_VERSION_MINOR` | 16 | 17 |
| Default when the setter is never called | `FIXPP_RESET_SEQNUM_BILATERAL_STRICT` | `FIXPP_RESET_SEQNUM_BILATERAL_LENIENT` |
| Enum values | STRICT = 0, LENIENT = 1, UNILATERAL = 2 | unchanged |
| Symbols, types, error codes | — | unchanged |

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

## Marks (Article X §7)

- "BREAKING since 1.17" on `fixpp_reset_seqnum_policy` and on `fixpp_session_config_set_reset_seqnum_policy`.
- The `(default)` marker moves from STRICT to LENIENT; the enumerator descriptions match data-model.md's policy table.
- A 1.17 paragraph in `version.h`'s version comment.
- The PR description and the B&L delta (B-103-1) say BREAKING.

## Migration

To keep a reset on every Logon (now conforming: 34=1), call
`fixpp_session_config_set_reset_seqnum_policy(cfg, FIXPP_RESET_SEQNUM_BILATERAL_STRICT)`.

## Witnesses

`CapiVersion.CApiVersionIsExactly_1_17_0` (renamed from `_1_16_0`), `CapiVersion.CompositeMacroValue`, a C-ABI cell
asserting the default pair's Logon (no 141, 34 = stored), and the existing strict-naming cells in
`tests/capi/public_roundtrip_test.cpp`.
