<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
# Thorny corpus — `reject/` category (016 US2, T019; category added Gate-B/r1)

Session-level Reject (`35=3`) generation scenarios derived from the capped
closed-with-fix tail (`../CORPUS-INDEX.md`). Standalone witnesses (no
counterparty): they drive crafted frames through `Session::on_inbound_frame()`
via the shared `parity/parity_support.hpp` `ParityAcceptorFixture` and observe
fixpp's spec-conformant Reject + sequence-advancement behavior.

| P | File | Provenance | Asserts |
|---|---|---|---|
| P2 | `qfj-557-generatereject-advances-seqnum_test.cpp` | quickfix-j#557 (C-102) | two invalid app msgs at seq 2+3 → ≥2 `Reject(35=3)`, `next_inbound` advances to 4, session stays Active |
