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

## Inherited parent-harness obligation (FR-021)

Only **fixpp** is sanitizer-instrumented (FR-021). The submodule carries **zero**
QuickFIX/Fix8 counterparty source — the counterparties run as their **unmodified
production binaries** in the gitignored parent harness, so no counterparty code is
ever compiled with sanitizer flags. The in-repo `interop_*` ctest targets link only
`interop_support` + `fixpp_*` + GTest (structurally no counterparty source). The
"counterparty-runs-unmodified" half is a parent-harness obligation, inherited here.
