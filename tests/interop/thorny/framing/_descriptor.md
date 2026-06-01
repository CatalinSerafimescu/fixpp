<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
# Thorny corpus — `framing/` category (016 US2, T019)

Framing / validation / reject / negotiation scenarios derived from the capped
closed-with-fix tail (`../CORPUS-INDEX.md`). Standalone witnesses: they drive a
crafted **first inbound** frame against a freshly-`open()`ed acceptor (still
NotConnected, no Logon yet) and observe fixpp's spec-conformant refusal.

| P | File | Provenance | Asserts |
|---|---|---|---|
| P1 | `qfj-603-unsupported-beginstring_test.cpp` | quickfix-j#603 | inbound Logon w/ unsupported BeginString (FIX.3.9) → not Active/LogonReceived, no Logon reply |
| P1 | `qfj-721-non-logon-first-message_test.cpp` | quickfix-j#721 | non-Logon (Heartbeat) first frame on non-FIXT session → not logged on, no crash/NPE |

Refusal observable: fixpp asserts the **negative** end-state (`!= Active &&
!= LogonReceived`), matching `tests/session/logon_handshake_test.cpp`
(RefusedLogonWrongBeginString / RefusedFirstMessageNotLogon) — it does NOT
guarantee a specific terminal `Disconnected` for a pre-Active first-frame refusal.
