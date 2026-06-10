# Contract: `Session::honor_peer_next_expected_` (031 amendment)

**Scope**: internal session method (not a public/C-ABI surface). 031 changes its signature and the
counter its comparisons are taken against. No wire field / error slot / codegen change.

## Signature

**Before (027)**:
```cpp
asio::awaitable<fixpp::core::expected_t<bool>>
honor_peer_next_expected_(std::string_view raw_789, bool present_789) noexcept;
```
Internally reads `const seqnum_t n789 = seqnum_mgr_.peek_outbound();` and uses `n789` for all
comparisons.

**After (031)**:
```cpp
asio::awaitable<fixpp::core::expected_t<bool>>
honor_peer_next_expected_(std::string_view raw_789, bool present_789,
                          seqnum_t next_outbound_ref) noexcept;
```
- `next_outbound_ref` is the next-outbound sequence number the peer's advertised `789` is compared
  against. The three comparisons use `next_outbound_ref`; the resend range still reads the live
  `seqnum_mgr_.peek_outbound()`.

## Semantics (C-031, supersedes the 027 honor comparison)

Let `X = parse_seqnum(raw_789)` and `R = next_outbound_ref`. Returns `expected_t<bool>`:

| Pre-condition | Effect | Return |
|---|---|---|
| `X == 0` | Logout "NextExpectedMsgSeqNum invalid" + `record_state_transition_(Disconnected)` | `false` |
| `X > R` | Logout "NextExpectedMsgSeqNum too high, expecting `R` but received `X`" + Disconnected | `false` |
| `X < R` | `replay_outbound_range_(X, peek_outbound() - 1, end_is_through_current=true)`; on error → Disconnected + propagate | `true` (or `unexpected` on replay error) |
| `X == R` | no resend (in-sync) | `true` |

`true` ⇒ caller proceeds to `Active`; `false` ⇒ caller returns without entering Active (matches the
027 caller protocol at `:2028-2029` / `:3324-3325`).

## Caller obligations (C-031-CS)

| Caller | MUST pass `next_outbound_ref` = | Rationale |
|--------|----------------------------------|-----------|
| Acceptor (`session.cpp:2027`) | the **pre-reply** outbound — capture `const seqnum_t n_pre = seqnum_mgr_.peek_outbound();` **before** the reply Logon `store_then_emit` (`:2015`) | the peer's **initial** Logon `789` was measured before fixpp replied; the reply consumed a seq |
| Initiator (`session.cpp:3322`) | `seqnum_mgr_.peek_outbound()` (current) | the peer's **reply** `789 = target+1` already accounts for fixpp's Logon; no reply emitted on this arm — **byte-identical to 027** |

## Invariants preserved

- **INV-NEX-RANGE**: the genuine-gap resend range is `[X, peek_outbound()-1]` = `[X, N_pre]`,
  identical to both reference engines and to 027 (research.md R4) — unchanged.
- **INV-NEX-INIT**: the initiator honor is byte-identical to 027 (same `next_outbound_ref =
  peek_outbound()`, same range, same comparisons) — FR-008.
- **INV-NEX-MONO**: no frame fixpp emits during/after the Logon exchange may share a sequence number
  with the reply Logon (FR-004) — the in-sync case must emit no resend at all.
- **RC#4 ordering** (027): the acceptor still emits its reply Logon **before** honoring `789`; 031
  changes only the captured comparison reference, not the emit ordering — FR-007.

## Conformance oracle

- QuickFIX-cpp v1.16.0 `Session.cpp`: decision pre-reply (`:228`); too-high pre-reply (`:230`);
  initial-Logon 789 = `getExpectedTargetNum()` (`:687`); reply 789 = `+1` (`:709-710`); range
  `[X, getExpectedSenderNum()-1]` post-reply = `[X, N_pre]` (`:277`).
- QuickFIX-J 3.0.1 `Session.java`: too-high pre-reply (`:2250/2255`); resend decision +
  range against `nextSenderMsgNumAtLogonReceived` (pre-reply snapshot) (`:2312-2313`); range
  `[X, N_pre]` (`:2334`).
