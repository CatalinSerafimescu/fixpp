# Quickstart: Reproduce & Validate (031)

## Reproduce the bug (live, current `main`)

From the parent harness, with the QF-initiator `*-tls-next-expected.cfg.in` configs set to all
three reset knobs OFF (so the peer sends no `141=Y`):

```bash
cd research/G19-fix-fpml-iso20022/phase-9-harness
python3 tools/run_interop_cell.py NE-QFcpp-acc-fix44-next-expected --run-dir /tmp/ne_qfcpp_acc --keep
```

Observe in `/tmp/ne_qfcpp_acc/qfcpp-initiator-log/*.messages.*`:
```
CPTY → 35=A 34=1 789=1
fixpp→ 35=A 34=1 789=2
fixpp→ 35=4 34=1 36=2 123=Y        ← spurious GapFill at seq 1 (already used by the reply Logon)
CPTY → 35=5 58="MsgSeqNum too low, expecting 2 but received 1"   ← peer Logout, session fails
```

## In-process RED (TDD entry, unit)

In `tests/session/test_next_expected_msgseqnum.cpp`, an acceptor with the `789` knob on whose peer
advertises `789 == N_pre` (fixpp's pre-reply next-outbound) must, after the reply Logon, emit
**zero** `SequenceReset` and **zero** `ResendRequest`, and never re-use the reply Logon's sequence
number. On current `main` this is RED (a spurious `SequenceReset-GapFill` at `N_pre` is emitted).

## Validate the fix

- **W1 (in-sync)**: acceptor + peer `789 = N_pre` ⇒ no `SequenceReset`/`ResendRequest`; emitted
  seqnums strictly increasing; reaches Active and stays (no peer Logout). GREEN after fix.
- **W2 (genuine gap, non-regression)**: acceptor outbound `N>2`, peer `789 = X < N_pre` ⇒ resend
  exactly `[X, N_pre]`, no `ResendRequest`. Must stay GREEN (range unchanged).
- **W3 (too-high boundary)**: peer initial Logon `789 = N_pre + 1` ⇒ Logout + disconnect.
- **W5 (invalid-789)**: empty/non-numeric `789` ⇒ Logout (unchanged).
- **W4 (initiator non-regression)**: `NE-*-init` unit + live ⇒ unchanged (in-sync, no resend).
- **Live close-out**: re-run `NE-QFcpp-acc` and `NE-QFj-acc`; the session establishes and the peer
  does **not** Logout-reject (SC-004). Witness hardened past `drive_to_active` (stay-Active /
  `recent_events` discriminator).

## Sanitizer / coverage

```bash
# from the library submodule, one preset at a time (see CLAUDE.md build caps)
cmake --build build/linux-clang-debug --target session_next_expected_msgseqnum -j2
ctest --test-dir build/linux-clang-debug -R next_expected
# + the 6-preset verify matrix at /speckit-verify
```
