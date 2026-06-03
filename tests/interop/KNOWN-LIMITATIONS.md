<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
# Interop — documented known-limitations (016 US5, T030)

> The in-repo source for the GA interop badge's **known-limitations list** (FR-024,
> SC-007). The parent `interop-badge-emit` check (out-of-repo, gitignored
> `../phase-9-harness/`) consumes this file + the goldens + `thorny/CORPUS-INDEX.md`
> and renders the badge naming exact counterparty versions. Each limitation carries
> its tracking reference. Append-only alongside the corpus (FR-013).

## Counterparty versions paired against (badge text, T031)

The badge names exact `{name, version, commit}` (FR-024). In-repo we pin the
**name + version**; the parent capture pins the exact **commit** at pairing time
(the commit is a runtime property of the counterparty binary the parent builds):

- **QuickFIX-cpp v1.16.0** (live counterparty, both roles)
- **QuickFIX-J 3.0.1** (live counterparty, both roles)
- **Fix8 1.4.3** — corpus-only at v1.0 (FR-009), not a live-paired counterparty.

Badge text shape: `Interop verified against QuickFIX-cpp v1.16.0 / QuickFIX-J 3.0.1`
(+ archived-transcript link + this limitations list).

## Badge scope — what is asserted (018 G1, SC-006)

As of **018-interop-live-admin (gap-fill G1)**, the QuickFIX-J live cells assert
**real bidirectional FIX 4.4 session-admin round-trips on the established
session** — not merely the Logon/Logout handshake the original badge captured:
TestRequest→Heartbeat `112` echo (both directions), idle Heartbeat cadence
(`108=1`s, ≥3 beats/dir), ResendRequest / SequenceReset-GapFill recovery (both
inbound-detect and outbound-answer), and session-level `Reject(35=3)` survival —
each for **both fixpp roles** over `one_way_ca` TLS.

**Scope boundary (do NOT overstate):** the badge covers **session-admin** interop
only. **Application-message interop** (`NewOrderSingle → ExecutionReport`, the
`[const §VII.6]` business flow / G2) is **still NOT asserted** and remains an open
v1.0-GA residual. G1 enriches the session-layer badge; it does not extend it to
business messages.

## Corpus known-limitations (FR-014 — deferred-by-design)

From `thorny/CORPUS-INDEX.md`'s known-limitation table — upstream behaviors fixpp
intentionally scopes out at v1.0, each with its tracking ref:

| Limitation | Upstream provenance | Tracking |
|---|---|---|
| Inbound PossDup / OrigSendingTime handling (dup-tolerance; missing/bad OrigSendingTime → Reject/Logout) | quickfix-cpp `noOrigSendingTime`/`badOrigSendingTime`, quickfix-j#703 | `S-010` (out of scope; session-recovery successor feature) |
| Connection-event auto-reset knobs (ResetOnLogon / ResetOnLogout / ResetOnDisconnect) | quickfix-cpp `logOn_ResetOnLogon` / `disconnect_ResetOnDisconnect` | `S-017` (catalogue backlog) |
| RefreshOnLogon (store refresh before logon) | quickfix-cpp `logOn_RefreshOnLogon` | `S-018` (catalogue backlog) |
| FIXT.1.1 / FIX 5.0 SP2 parse routing + DefaultApplVerID(1137) | quickfix-j `testParseFixt*` | `S-020-FIXT` / `S-025` (backlog) |
| Application-callback layer (RejectLogon-from-callback, BusinessMessageReject, callback-exception rollback) | quickfix-j#60 / #696 / #572 | `app-message-layer` (matrix option (a); session-only badge) |
| SendingTime nanosecond precision (9 frac digits) | quickfix-j#873 | `S-time-nanos` (fix_time covers s/ms/µs; minor) |
| Configurable MaxLatency / CheckLatency knobs | quickfix-cpp `sessionHasMaxLatency` | `S-latency-knob` (latency window not a per-session knob) |
| Configurable ResendRequestChunkSize splitting | quickfix-j#751 (corpus C-103, P3) | `S-backlog-chunked-resend` (open) |

## Session-only scope residual (FR-027 / SC-008)

- **`[const §VII.6]` NewOrderSingle → ExecutionReport interop clause** is **NOT
  discharged** by this session-only badge. It remains an **open v1.0-GA residual**
  (the one Gate-A adjudication carried in `plan.md`, R7) — the business-message
  matrix cells are `deferred:app-messages` (FR-005). Forward pointer: corpus/matrix
  A-001/A-006. The badge MUST state the scope is **session-layer interop**, not
  application-message interop.

## TLS / transport caveats

- **All-TLS, server-auth `one_way_ca` baseline** (FR-025). fixpp ships TLS-only (no
  plaintext transport); every live cell runs over TLS. The v1.0 baseline is
  server-auth `one_way_ca` (counterparty presents a server cert fixpp's CA trusts).
- **Mutual-certificate (client-cert) mTLS is `deferred:v1.1-mtls`** — the app-layer
  client-cert ↔ CompID identity binding (013/014 fail-closed, session profile
  `mtls_ca`) is the v1.1 reach, not part of the v1.0 badge.
- **Down-peer initiator teardown (L2 carry-forward, from 015).** An initiator aimed
  at a never-accepting peer is not promptly torn down by `Engine::stop()` on the
  mid-connect path (a single connect timeout runs to completion before cancellation
  re-checks). The `016` down-peer watchdog cell (`HP-down-peer-stop-watchdog`)
  pins the bounded-stop contract on the policy-wired path (T008/FR-028); the
  unbounded mid-connect case is tracked in `spec/behaviors-and-limitations.md` (L2).

## Session-reject vs peer-disconnect divergence (018 US4 / T021)

When a malformed or invalid admin frame is received, the FIX specification
(`[FIX-SL §4.5.4]`) requires the receiving engine to emit `Reject(35=3)` with
`RefSeqNum(45)` and an appropriate `SessionRejectReason(373)`[/`RefTagID(371)`],
and **continue the session** (non-fatal). fixpp follows this rule (S-007 path).

In practice, some counterparties may **disconnect** instead of sending a
`Reject(35=3)` in response to certain malformed inputs. This is a
**counterparty divergence**, not a fixpp defect: the session-reject cell
(`HP-QFj-{init,acc}-fix44-reject-invalid-admin`) asserts fixpp's own behaviour
(the Reject is emitted, the session stays `Active`). If a counterparty
disconnects on a given input, that input is not suitable as the proxy-corrupt
induction for this cell; the parent harness must choose a malformed input that
the counterparty tolerates with a `Reject` (not a `Logout`/disconnect).

Counterparties confirmed to emit `Reject(35=3)` (and not disconnect) on the
pinned malformed input: **QuickFIX-J 3.0.1** (paired at G1 release-prep tier).
Any divergence observed on other inputs or counterparty versions should be
recorded here with the specific triggering input and counterparty version.

## Inherited parent-harness obligation (FR-021)

Only **fixpp** is sanitizer-instrumented (FR-021). The submodule carries **zero**
QuickFIX/Fix8 counterparty source — the counterparties run as their **unmodified
production binaries** in the gitignored parent harness, so no counterparty code is
ever compiled with sanitizer flags. The in-repo `interop_*` ctest targets link only
`interop_support` + `fixpp_*` + GTest (structurally no counterparty source). The
"counterparty-runs-unmodified" half is a parent-harness obligation, inherited here.
