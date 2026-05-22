# Phase 1 — Data Model

**Feature:** 009-session-fsm-finalize
**Date:** 2026-05-22
**Status:** No new entities; modifies existing 005 entities.

## Inheritance

The binding data model is [`005-session-establishment-fsm/data-model.md`](../005-session-establishment-fsm/data-model.md) E1..E9 — read it first. This document records only the **modifications** this slice applies to existing entities. No new entity is introduced.

## Modified entities

### E2 — `SessionConfig` (005-owned; this slice extends)

**New field:**

| Field | Type | Default | Source | Notes |
|---|---|---|---|---|
| `role` | `session_role` enum | `session_role::initiator` | Phase 0 D-1 | Drives `Session::open()` initial-state choice per FR-004. |

**New enum (declared in `include/fixpp/session/session_config.hpp`, shape oracle in [`contracts/session_role.hpp`](contracts/session_role.hpp)):**

```cpp
enum class session_role : std::uint8_t { initiator = 0, acceptor = 1 };
```

Default-initialization of `SessionConfig::role` to `initiator` preserves all existing 005 test behavior (the `make_acceptor_cfg` helper in `tests/session/logon_handshake_test.cpp` is updated by this slice to set `cfg.role = session_role::acceptor` explicitly).

### E1 — `Session` (005-owned; this slice extends)

**New private member:**

| Field | Type | Default | Source | Notes |
|---|---|---|---|---|
| `next_test_request_id_` | `std::uint32_t` | `0` | Phase 0 D-3 | Replaces the process-global `static std::uint32_t tr_counter` in `run_liveness_loop` (FR-010). |

**Modified methods (semantics, not signature):**

| Method | Change | Source |
|---|---|---|
| `Session::send(payload)` | NO-OP body → wired end-to-end pipeline (assign seqnum → stamp SendingTime → build → store → transport_send). Signature unchanged. | FR-001 / RC#1 |
| `Session::open()` | Unconditional `LogonSent` → branch on `cfg_.role` (initiator: existing behavior; acceptor: `NotConnected`, no outbound Logon emitted). Signature unchanged. | FR-004 / RC#2 |
| `Session::close(graceful)` | Add `co_await seqnum_mgr_.drain()` in phase 2 (after `root_cancel_.emit(...)` + `trace_slot_.clear()`, before `state_ = closed_drained`). Drain failures logged-then-proceed per D-2. Signature unchanged. | FR-011 / RC#7 |
| `Session::on_inbound_frame` (NotConnected row, refused-Logon) | Drop the Phase-3 `is_logon` compromise that preserved `NotConnected`. Every refused Logon → `Disconnected`. | FR-006 / RC#3 |
| `Session::on_inbound_frame` (Active+LogonReceived row, SendingTime guard) | Strengthen guard: empty `52` AND parse-failure `52` → Reject(SessionRejectReason=10, RefTagID=52) → Logout → Disconnected (same path as existing stale-SendingTime branch). | FR-007 / FR-008 / RC#5 |
| `Session::on_inbound_frame` (LogonSent row, SendingTime guard) | Strengthen guard: empty `52` AND parse-failure `52` → Logout(58=`<error text>`) → Disconnected (LogonSent-special D-3 path; no standalone Reject pre-establishment). | FR-009 / RC#5 |
| `Session::run_liveness_loop` (TestRequest ID generation) | Replace `static std::uint32_t tr_counter` with `++next_test_request_id_` member access. | FR-010 / RC#6 |

### `admin_messages` builders (005-owned; this slice extends signatures)

The 5 admin builders (`build_logon`, `build_logout`, `build_heartbeat`, `build_test_request`, `build_reject`) in `include/fixpp/session/admin_messages.hpp` + `src/session/admin_messages.cpp` gain parameters per the table below. **Note (analyze finding C1):** `build_logon` (line 40 of `admin_messages.hpp`) already has `begin_string` in its signature — it gains only `sending_time` here. The other 4 builders gain both `begin_string` AND `sending_time`.

| Parameter | Type | Source | Notes |
|---|---|---|---|
| `begin_string` | `std::string_view` | FR-002 / RC#4 | Threaded from `cfg_.begin_string` at the call site. Replaces the hardcoded `kBeginStringDefault = "FIX.4.2"`. **Added to 4 builders** (`build_logout`, `build_heartbeat`, `build_test_request`, `build_reject`); `build_logon` already has it. |
| `sending_time` | `std::string_view` | FR-003 / RC#4 | Pre-formatted via `core::utc_time_to_fix_string(effective_clock_->now())` at the call site. Replaces the hardcoded `kSendingTimePlaceholder = "00000000-00:00:00.000"`. **Added to all 5 builders.** |

Constants `kBeginStringDefault` and `kSendingTimePlaceholder` are **removed**. All 9 call sites in `src/session/session.cpp` (lines per Opus triage: 322, 657, 671, 715, 880, 1054, plus `run_logout_phase1` etc.) are updated to pass the appropriate arguments per builder.

## Unmodified entities (inherited from 005)

- **E3 — `SeqnumManager`** — no signature change; FR-011 just calls its already-documented `drain()` method.
- **E4 — `MessageStore` consumed interface** — unchanged; FR-001's `Session::send` calls the already-binding `store(seq, committed_span, outbound)` contract.
- **E5 — admin-message wire structures** — unchanged (the structure of Logon/Logout/Heartbeat/TestRequest/Reject is 005's data; this slice only fixes which clock/version values get plugged into them).
- **E6 — `[FIX-SL §4.10]` transition matrix** — unchanged in the data-model document; the `NotConnected × refused Logon` cell's *implementation* in session.cpp gets corrected per FR-006, but the matrix's *specification* in `005/data-model.md:19` already says `Disconnected` (this slice closes drift, not redesigns).
- **E7 — `session_*` `core::error` variant slots 66..76** — unchanged. No new error variant required; FR-001's transport errors flow through existing 005-pinned slots.
- **E8 — `SessionConfig` consumed fields from `[2d §4.5]`** — unchanged except for the added `role` field (recorded above).
- **E9 — `effective_clock` resolution** — unchanged; the slice's FR-001 + FR-002/003 *use* `effective_clock_->now()` per the already-binding `[2d §7.9]` contract.

## No state transitions added

The `[FIX-SL §4.10]` 6-state set is unchanged. FR-004 / FR-005 / FR-006 reach existing-but-previously-unreachable matrix cells from production paths; they do not introduce new states.

## No new validation rules added

FR-007 / FR-008 / FR-009 enforce the existing `005/contracts/sending_time.hpp:23-24` validation rule literally — the rule was already there, the implementation was lenient.
