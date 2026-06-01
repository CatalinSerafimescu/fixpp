<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
# Thorny-issues corpus — provenance index (016 US2, T018/T019/T022)

> The in-repo provenance index for the thorny-corpus replay suite (FR-010..FR-015,
> data-model E3 `CorpusScenario`). Each row is an upstream FIX-engine bug whose
> *triggering message sequence* is replayed against fixpp as a standalone
> session-layer scenario, asserting fixpp's **spec-conformant** outcome (FR-018:
> reconciled to the FIX session spec, never to a reference engine). Executable
> encodings live under `tests/interop/thorny/<category>/`; the raw issue-tracker
> sweep analysis stays in the parent (R2, `phases/phase-9/parity-*.md`).
>
> This file is **NOT** research/decision content — it is the test-suite manifest
> (does not match `no-research.yml`: not `*-decisions.md`).

## Scope of the v1.0 sweep (FR-010 — bounded + capped)

The corpus does not pre-exist; this is its **initial bulk population** (permitted —
FR-013's append-only rule governs *subsequent* releases, data-model E3). The v1.0
worklist is a **bounded, enumerable** set:

1. **Pre-seeded list** — `phase-9-harness/manifest/scenarios.yaml` (the
   cross-communication matrix; consumed by US1's happy-path cells).
2. **Capped per-engine closed-with-fix tail** — the closed JIRA/GitHub issues
   already mined into `phases/phase-9/parity-quickfixj.md` (QFJ-*) and
   `phases/phase-9/parity-quickfix-cpp.md` (SessionTestCase sections) that touch
   **wire / session / persistence** behavior. **Cap: ≤ the issues enumerated in
   those two parity audits** (QuickFIX-J: 18 GAP + the COVERED issue-linked rows;
   QuickFIX-cpp: 9 GAP + COVERED issue-linked rows). No live tracker re-scrape is
   performed in this feature — the parity audits ARE the capped tail.
3. **Fix8** — corpus-only at v1.0 (FR-009); upstream dormant, no new closed-tail
   mining. No Fix8 corpus rows at v1.0 (revisited post-9.D).

The open-issue `watch:` bucket (still-open upstream bugs) is **explicitly phased as
a follow-on sweep** beyond this v1.0 worklist (FR-010 scope-refinement) — NOT an
in-feature triage of all open issues across three trackers. It is out of scope here.

## Disposition rule (T022 — FR-014)

A corpus scenario's `disposition` is one of:

- **`pass`** — fixpp produces the spec-conformant outcome; an executable witness
  asserts it (this suite, or a cited US3 parity witness / `tests/session/` test).
- **`known-limitation:<tracking-issue>`** — fixpp does not pass; the behavior is a
  documented, deferred-by-design limitation with an **open tracking issue**.

**Release gate:** a failing **`P1`** (or `watch:P1`) scenario **MUST block the GA
tag** until it is fixed OR recorded as a `known-limitation:<open-issue>` (FR-014).
`P2`/`P3` failures are tracked but non-blocking. The corpus is **append-only**
(FR-013): later release sweeps add rows; none are ever removed.

`differentiator: true` marks a scenario where fixpp is spec-correct **while the
upstream engine was buggy** — a positive release-notes signal (FR-015).

---

## P1 — release-blocking (closed-with-fix tail)

| # | Provenance (engine#issue) | Category | Triggering sequence → expected fixpp behavior | Diff? | Disposition (witness) |
|---|---|---|---|---|---|
| C-001 | quickfix-j#646 | ResendRequest edges | Transport write returns false mid-resend → resend aborts, session disconnects | yes | `pass` — **covered-by-parity** `parity/resend_abort_on_failing_write_test.cpp` (US3 T023) |
| C-002 | quickfix-j#658 / #750 / #788 | high-volume / SequenceReset | Too-high inbound → ResendRequest (not reorder-queue); GapFill resyncs; large backlog no blowup | yes | `pass` — **covered-by-parity** `parity/replay_subsumes_reorder_queue_test.cpp` (US3 T024) |
| C-003 | quickfix-cpp `nextSequenceReset` | SequenceReset/GapFill | Inbound bare SequenceReset NewSeqNo >/=/< expected → advance / no-op / Reject(373=5) | no | `pass` — **covered-by-parity** `parity/inbound_sequencereset_arms_test.cpp` (US3 T025) |
| **C-004** | **quickfix-j#750** | **Logon/Logout race** | Active session; inbound Logout with **too-low** MsgSeqNum → `Disconnected`; with **too-high** MsgSeqNum → **gap-recovery precedence** (ResendRequest, stays Active) — a **documented divergence** from QFJ-750's disconnect-on-Logout-gap special-case (fixpp applies its uniform §4.5.3 too-high policy; spec-defensible) | div. | **`pass`** (divergence documented) — `recovery/qfj-750-logout-seqnum-mismatch_test.cpp` (T020) |
| **C-005** | **quickfix-j#271** | **high-volume / SequenceReset** | Active; inbound **SequenceReset-GapFill NewSeqNo=20000** → next-expected-inbound hard-advances to 20000, stays **Active**, **no stack-overflow / recursion** (fixpp store-replay has no inbound reorder-queue) | **yes** | **`pass`** — `recovery/qfj-271-sequencereset-large-gapfill_test.cpp` (T020) |
| **C-006** | **quickfix-j#603** | **reject / negotiation** | Acceptor `open()` (NotConnected); inbound **Logon with unsupported BeginString (FIX.3.9)** → does **NOT** reach Active/LogonReceived; **no Logon reply** emitted | no | **`pass`** — `framing/qfj-603-unsupported-beginstring_test.cpp` (T021) |
| **C-007** | **quickfix-j#721** | **Logon/Logout race / reject** | Acceptor `open()`; **first inbound is a Heartbeat (not Logon)** on a non-FIXT session → not logged on, clean refusal, **no crash/NPE** (the test running to completion IS the no-crash proof) | **yes** | **`pass`** — `framing/qfj-721-non-logon-first-message_test.cpp` (T021) |

**P1 cap note:** C-001..C-003 are dispositioned **covered-by-parity** (US3 already
encoded the witness during the parity GAP-closure pass) and are listed here for
provenance completeness + append-only traceability — they are **not** re-encoded
(no duplicate test). C-004..C-007 are the **fresh** P1 encodings this feature adds,
each a triggering sequence distinct from the US3 witnesses.

## P2 / P3 — tracked, non-blocking

| # | Provenance | Category | Why not P1 | Disposition |
|---|---|---|---|---|
| C-101 | quickfix-j#626 | persistence/recovery | Resend replays stored frames; fixpp recomputes checksum on emit — `build_replay_frame()` (session.cpp) explicitly skips stored 9=/10= and lets `wire::Writer::commit()` recompute them; differentiator | `pass` — `thorny/recovery/qfj-626-resend-recomputes-checksum_test.cpp` (gate-b/r1): stores frame with deliberately wrong 9=/10=, feeds ResendRequest, asserts replayed frame has correct 9=/10= + 43=Y + 122= |
| C-102 | quickfix-j#557 | reject | GenerateReject advances target seqnum over a run — fixpp advances `next_inbound_unsafe()` past each invalid message that triggers a Reject | `pass` — `thorny/reject/qfj-557-generatereject-advances-seqnum_test.cpp` (gate-b/r1): feeds two invalid 35=D at seq=2+3 in Active state, asserts two Reject(35=3) emitted AND next_inbound==4 (advanced past both) |
| C-103 | quickfix-j#751 | SequenceReset/GapFill | Configurable `ResendRequestChunkSize` splitting — fixpp resend walks the full range (no chunk knob) | `known-limitation:S-backlog-chunked-resend` (P3) |

## known-limitation — deferred-by-design (open tracking, NOT executed at v1.0)

These are upstream behaviors fixpp **intentionally scopes out**; each is a
`known-limitation` with its catalogue/backlog tracking ref (FR-014). They are
recorded for provenance, **not** encoded as v1.0 corpus tests, and several are
the session-recovery / app-message successor features' concern (per the 005
`/clarify` Q2 decision: upstream RR/SR bugs are `watch:P1` for the successor,
**not** v1.0 release blockers).

| Provenance | Category | known-limitation tracking |
|---|---|---|
| quickfix-cpp `noOrigSendingTime` / `badOrigSendingTime`; quickfix-j#703 | field-validation | `known-limitation:S-010` (inbound PossDup/OrigSendingTime — out of scope, session-recovery successor) |
| quickfix-cpp `logOn_ResetOnLogon` / `disconnect_ResetOnDisconnect` / `sessionResetsOnLogout` | session-admin | `known-limitation:S-017` (connection-event auto-reset knobs — catalogue backlog) |
| quickfix-cpp `logOn_RefreshOnLogon` | session-admin | `known-limitation:S-018` (RefreshOnLogon — catalogue backlog) |
| quickfix-j `testParseFixt*` / #921-era FIXT routing | encoding / negotiation | `known-limitation:S-020-FIXT / S-025` (FIXT.1.1 / FIX5.0SP2 routing + DefaultApplVerID(1137)) |
| quickfix-j#60 / #696 / #572 (RejectLogon-from-callback, BusinessMessageReject) | reject | `known-limitation:app-message-layer` (no Application callback layer — matrix option (a), session-only badge) |
| quickfix-j#873 (nanos precision) | encoding | `known-limitation:S-time-nanos` (fix_time covers s/ms/µs, not ns — minor) |
| quickfix-cpp `sessionHasMaxLatency` / quickfix-j `setMaxLatency` | field-validation | `known-limitation:S-latency-knob` (MaxLatency/CheckLatency not a per-session knob) |

---

### Follow-on (out of this feature)

- **Open-issue `watch:` sweep** — still-open upstream bugs replayed as
  `watch:P1`/`watch:P2`/`watch:info` (FR-012); phased follow-on (FR-010), governed
  by the append-only rule (FR-013).
- **Fix8 corpus mining** — revisited after the live-disposition decision (FR-009).
