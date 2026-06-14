# Contract — Resend-reply wire output (037)

Internal wire contract for the two resend-reply frame emitters. This is not a public C++ API contract (no signature changes); it is the **emitted-bytes** contract the witnesses and goldens enforce.

## C-1 — SequenceReset-GapFill (35=4)

When `replay_outbound_range_` emits a `SequenceReset`-GapFill to cover a range of skipped administrative messages, the emitted frame MUST satisfy:

1. Contains `123=Y` (GapFillFlag) — unchanged.
2. Contains exactly one `PossDupFlag(43)` with value `Y`.
3. Contains exactly one `OrigSendingTime(122)` whose value byte-equals the frame's own `SendingTime(52)`.
4. Retains, unchanged, every field it carried before 037: `8`, `35=4`, `34`, `49`, `52`, `56`, `36`, `123`, and the recomputed `9`/`10`.
5. Field order is unconstrained for interop (peers parse map-based); the implementation appends `43`/`122` after `123`.

Rationale: a GapFill's `34` is at/below the peer's expected number; `43=Y` marks it a legitimate duplicate so the peer does not kill the session for a too-low sequence number, and `122` is the conditionally-required companion (a strict `RequiresOrigSendingTime` peer rejects `43=Y` without `122`).

## C-2 — Replayed application frame

When `replay_outbound_range_` re-serializes a stored outbound application frame for retransmission, the emitted frame MUST satisfy:

1. Contains exactly one `PossDupFlag(43)` (value `Y`) — even if the stored frame already contained `43`.
2. Contains exactly one `OrigSendingTime(122)` — even if the stored frame already contained `122`.
3. The `122` value byte-equals the stored frame's original `SendingTime(52)` — NOT any caller-supplied `122` retained under `allow_pos_dup=true`.
4. Under default config (`allow_pos_dup=false`), the emitted frame is byte-identical to pre-037 output (the send path strips caller `43`/`122`, so the stored frame is clean and the new skip set never matches).

Rationale: matches QFJ's resend (`setField` replaces; `122` sourced from the message's own `SendingTime`), preventing duplicate-tag rejection by strict counterparties.

## C-3 — Cross-frame invariant (the resend reply as a whole)

A single resend reply may emit both kinds (replayed app frames interleaved with GapFills covering admin ranges). Every frame in the reply that carries `43=Y` MUST also carry exactly one `122 == that frame's 52`. No frame in a resend reply carries a duplicate `43` or `122`.

## C-4 — Negative / honesty checks (witness obligations)

- A GapFill witness MUST first assert the GapFill is the frame under test (`35=4` AND `123=Y`) before asserting `43`/`122`, so it cannot pass by inspecting a replayed app frame instead.
- The retain-case witness MUST first prove the **stored** frame contained `43`/`122` (else "exactly one" is trivially satisfied by a clean frame and proves nothing about dedup) before asserting the replayed frame has exactly one each.
- The default-path non-regression witness MUST assert exact byte-identity of the replayed app frame against the pre-037 expected bytes (not merely "≤ one 43").
