<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
# Thorny corpus — `recovery/` category (016 US2, T019)

Recovery / sequencing / Logon-Logout-race scenarios derived from the capped
closed-with-fix tail (`../CORPUS-INDEX.md`). Standalone witnesses (no
counterparty): they drive crafted admin frames through `Session::on_inbound_frame()`
via the shared `parity/parity_support.hpp` `ParityAcceptorFixture` and observe
fixpp's spec-conformant recovery behavior.

| P | File | Provenance | Asserts |
|---|---|---|---|
| P1 | `qfj-750-logout-seqnum-mismatch_test.cpp` | quickfix-j#750 | inbound Logout w/ too-high(999) & too-low(1) MsgSeqNum → still `Disconnected` |
| P1 | `qfj-271-sequencereset-large-gapfill_test.cpp` | quickfix-j#271 | inbound SequenceReset-GapFill NewSeqNo=20000 → resync to 20000, Active, no recursion |

Covered-by-parity (NOT re-encoded — see `../CORPUS-INDEX.md` C-001..C-003):
quickfix-j#646 (T023), #658/#750/#788 reorder-queue (T024), inbound bare
SequenceReset arms (T025).
