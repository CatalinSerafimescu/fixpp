# Data Model: Acceptor 789 Resend-Range Boundary Fix (031)

This fix introduces **no new entities, types, fields, or stored state**. It corrects which
existing in-memory counter value an existing decision is taken against. The "model" here is the
set of sequence-number values and the honor decision states.

## Values (all existing `seqnum_t`, in-memory)

| Name | Meaning | Source |
|------|---------|--------|
| `X` (`x789`) | peer's advertised `NextExpectedMsgSeqNum(789)` — "the next message the peer expects from us" | parsed from the inbound Logon (`parse_seqnum`, existing) |
| `N_pre` | fixpp's next-outbound **before** the acceptor reply Logon consumes a sequence number | `seqnum_mgr_.peek_outbound()` captured at the inbound-Logon handler, before `store_then_emit` (`:2015`) |
| `N_post` | fixpp's next-outbound **after** the reply Logon (`= N_pre + 1`) | `seqnum_mgr_.peek_outbound()` at honor time on the acceptor arm (the **wrong** value used today) |
| `next_outbound_ref` | **new parameter** to `honor_peer_next_expected_`: the next-outbound `X` is compared against | acceptor passes `N_pre`; initiator passes its current `peek_outbound()` |

Invariant: on the acceptor arm, exactly one frame (the reply Logon) is emitted between capturing
`N_pre` and the honor call, so `N_post == N_pre + 1`. The capture (rather than `peek_outbound()-1`)
is used for robustness and to mirror QFJ's `nextSenderMsgNumAtLogonReceived` snapshot.

## Honor decision (states, keyed on `next_outbound_ref`)

| Condition (against `next_outbound_ref`) | Action | Changed by 031? |
|---|---|---|
| `X == 0` (present-but-invalid) | Logout "NextExpectedMsgSeqNum invalid" + disconnect | unchanged |
| `X > next_outbound_ref` | Logout "NextExpectedMsgSeqNum too high, expecting N…" + disconnect | **threshold** changes (acceptor: was `N_post`, now `N_pre`); text reports `next_outbound_ref` |
| `X < next_outbound_ref` | proactive resend `[X, peek_outbound()-1]` (= `[X, N_pre]`), no `ResendRequest` | **guard** changes (acceptor); **range unchanged** (research.md R4) |
| `X == next_outbound_ref` | in-sync — **no resend** | **fix**: acceptor in-sync (`X == N_pre`) no longer mis-fires a spurious GapFill |

## Per-role reference (research.md R5)

| Role | call site | `next_outbound_ref` passed | Why |
|------|-----------|----------------------------|-----|
| Acceptor | `session.cpp:2027` (after reply emit) | `N_pre` (captured before `:2015`) | peer's **initial** Logon `789` measured pre-reply (no `+1`); reply consumed a seq |
| Initiator | `session.cpp:3322` (on peer reply ACK) | `peek_outbound()` (current) | peer's **reply** `789 = target+1` already matches fixpp's post-own-Logon outbound; no reply emitted on this arm — **byte-identical** |

## Relationships / non-state

- No `MessageStore` interaction (the resend range still reads the live `peek_outbound()`; no
  persistence change; INV-H1 / 029 hydrate untouched).
- No FSM state added; the honor is invoked from the existing Logon-handling states (acceptor
  reply-then-honor, initiator ACK-honor).
- No wire field, error slot, codegen, or C-ABI surface (FR-009).
