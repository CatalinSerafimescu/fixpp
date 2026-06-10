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

## W5a — bilateral_strict: re-hydrate suppressed (the 025 delta)

```
persistent store {in:37, out:42}; cfg.refresh_on_logon = true;
cfg.reset_seqnum_policy_field = bilateral_strict (default);
→ drive a 2nd logon (initiator)
EXPECT (a) zero EXTRA store reads beyond the 029 cold one-shot (the per-logon re-hydrate suppressed)
       (b) no NEW malformed Logon attributable to the knob — establishment proceeds exactly as the
           knob-off bilateral_strict path
```
Asserts the 025-delta suppression. Does NOT assert the strict + non-1 **cold-open** Logon is
well-formed — that is the inherited L-029-3 gap (see W5b).

## W5b — L-029-3 inherited-gap witness (knob-OFF strict cold open)

```
persistent store {in:37, out:42}; cfg.refresh_on_logon = false (knob OFF);
cfg.reset_seqnum_policy_field = bilateral_strict; initiator; reset_on_logon = false;
→ drive the COLD OPEN (first logon)
EXPECT documents the inherited 029 cold-open behaviour AS-IS — asserts ONLY what holds (the 029
       cold seed runs). Does NOT assert the cold Logon is well-formed: under bilateral_strict + a
       non-1 store it may carry 141=Y with a non-1 body.
```
This is the **L-029-3 gap witness** (inherited 029 behaviour, **NOT a 025 guarantee**) — labeled so
it cannot be mistaken for a correctness witness. The fix (e.g. a `{1,1}`-guard on `bilateral_strict`)
is a deferred 029/024 follow-up, OUT OF 025's scope.

## W6 — acceptor received-141 still wins under refresh (RC-1) (cold acceptor)

```
acceptor; persistent store inbound {in:37}; cfg.refresh_on_logon = true; policy = bilateral_lenient;
→ cold first acceptor logon (hydrated_==false); feed a peer reset Logon (34=1, 141=Y)
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

## W8 — no-heap on the re-hydrate apply step (`SeqnumManager::hydrate()`) proxy

```
mallocnesia / non-allocating ready-awaitable persistent store; knob on; policy = bilateral_lenient;
→ drive a 2nd logon
EXPECT zero allocations on the re-hydrate apply step (SeqnumManager::hydrate()), the same proxy 029 W8 uses (full path not witnessed)
```

## W9 — live interop (skip-without-counterparty)

```
fixpp standby (policy = bilateral_lenient, refresh_on_logon = on) over a store a "primary" advanced;
peer = running QFcpp/QFJ; → standby logs on
EXPECT the standby re-hydrates to the primary-advanced counters and logs on at them, no fatal
GTEST_SKIP without a live counterparty.
```

> **FR-012** (catalogue S-018 flip + B&L L-024-1 retire / L-025-1 add) has **no synthetic runtime
> witness** here by design — it is a doc-surface §VI delta discharged at **Polish** and verified by
> the mandatory **`/speckit-checklist-audit`** gate (the 027/028 precedent), not by a W-row over
> `feature-catalogue.md`/`coverage-index.md`/`behaviors-and-limitations.md` bytes.
