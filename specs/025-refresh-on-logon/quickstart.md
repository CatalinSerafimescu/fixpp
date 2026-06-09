# Quickstart: RefreshOnLogon (025) — the RED witnesses

TDD entry points for `tests/session/test_refresh_on_logon.cpp`. Each is RED before the change-set
(C1–C5) lands. Build on the 029 test harness (`make_initiator`/`make_acceptor` with a counting +
fault-injecting persistent store; all use a **non-strict** policy unless the witness is about
`bilateral_strict`). See [data-model.md](./data-model.md) §Witness matrix.

## W1 — store-wins UP (the core feature)

```
persistent store {in:50, out:60}; session live counters {in:40, out:42} (advanced in-session);
cfg.refresh_on_logon = true; cfg.reset_seqnum_policy_field = bilateral_lenient;
→ drive a 2nd logon (reconnect)
EXPECT manager.next_inbound == 50 && manager.next_outbound == 60   // adopted the higher store values
```
RED pre-change: the 029 one-shot latch makes the 2nd logon a no-op → counters stay {40,42} → FAIL.

## W2 — store-wins DOWN (distinguishes store-wins from advance-only)

```
persistent store {in:5, out:6} (primary did a reset); session live {in:40, out:42};
cfg.refresh_on_logon = true; policy = bilateral_lenient;
→ drive a 2nd logon
EXPECT manager.next_inbound == 5 && manager.next_outbound == 6     // moved DOWN — store-wins
```
RED pre-change: no re-hydrate → {40,42}. An advance-only impl would also FAIL this (it would clamp
to {40,42}) — W2 is the guard against a `max()` regression.

## W3 — default-off: no re-read, byte-identical

```
persistent store {in:50, out:60}; cfg.refresh_on_logon = false (default);
record store read-count after cold open = N;
→ drive a 2nd logon
EXPECT store read-count == N (no re-read) && counters retain their live values
+ run the full existing session/recovery/029-hydrate regression suite → all green
```
Assert the **read call-count** directly (counting store), not a proxy.

## W4 — non-persistent store: no-op even with knob on

```
MemoryStore (yields_persistent_store()==false); cfg.refresh_on_logon = true; policy = bilateral_lenient;
→ drive logons
EXPECT zero store reads on the refresh path; behaviour byte-identical to knob-off
```

## W5 — bilateral_strict: suppressed, no malformed Logon

```
persistent store {in:37, out:42}; cfg.refresh_on_logon = true;
cfg.reset_seqnum_policy_field = bilateral_strict (default);
→ drive a 2nd logon (initiator)
EXPECT (a) zero EXTRA store reads beyond the 029 cold one-shot (re-hydrate suppressed)
       (b) the emitted Logon is byte-identical to the knob-off bilateral_strict baseline
           and NEVER carries 141=Y with a non-1 body
```
Asserts BOTH postconditions (read-count AND emitted bytes) — the suppression has two observable
effects.

## W6 — acceptor received-141 still wins under refresh (RC-1)

```
acceptor; persistent store inbound {in:37}; cfg.refresh_on_logon = true; policy = bilateral_lenient;
→ feed a peer reset Logon (34=1, 141=Y) on a 2nd logon
EXPECT inbound seed withheld; the :1925 received-141 reset establishes {1,1};
       peer 34=1 accepted (in-seq, not too-low); session reaches Active
```

## W7 — refresh read-failure → fatal

```
fault-injecting persistent store: succeed at cold open, FAIL the inbound read on the 2nd logon;
cfg.refresh_on_logon = true; policy = bilateral_lenient;
→ drive a 2nd logon
EXPECT session transitions to Disconnected; no partial seed (manager unchanged from pre-read);
       no new error slot (reuses the 029 store-failure disposition)
```

## W8 — no-heap on the re-hydrate path

```
mallocnesia / non-allocating ready-awaitable persistent store; knob on; policy = bilateral_lenient;
→ drive a 2nd logon
EXPECT zero allocations attributable to the per-logon re-hydrate
```

## W9 — live interop (skip-without-counterparty)

```
fixpp standby (policy = bilateral_lenient, refresh_on_logon = on) over a store a "primary" advanced;
peer = running QFcpp/QFJ; → standby logs on
EXPECT the standby re-hydrates to the primary-advanced counters and logs on at them, no fatal
GTEST_SKIP without a live counterparty.
```
