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
| P2 | `qfj-626-resend-recomputes-checksum_test.cpp` | quickfix-j#626 (C-101) | stored frame with wrong `9=`/`10=` → ResendRequest replay recomputes correct `9=`/`10=` (+ `43=Y`, `122=`) |
| P2 | `021-poss-dup-replay-survives_test.cpp` | 021 T006 / SC-001/SC-004 | ResendRequest→replay; counterparty re-sends 43=Y frame; fixpp stays Active (LIVE cell, both engines × both roles) |
| P2 | `021-poss-dup-malformed-dup-rejected_test.cpp` | 021 T009 / SC-002/SC-004 | 43=Y missing 122; fixpp emits Reject(35=3,371=122,373=1), stays Active (LIVE cell, both engines × both roles) |
| P2 | `022-poss-resend-deliver_test.cpp` | 022 T012 / C5 / SC-001/SC-005 | counterparty sends 97=Y business message; fixpp delivers to fromApp; session stays Active (LIVE cell, both engines × both roles) |

Covered-by-parity (NOT re-encoded — see `../CORPUS-INDEX.md` C-001..C-003):
quickfix-j#646 (T023), #658/#750/#788 reorder-queue (T024), inbound bare
SequenceReset arms (T025).
