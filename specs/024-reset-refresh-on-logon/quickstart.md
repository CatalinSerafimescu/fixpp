# Quickstart: ResetOn{Logon,Logout,Disconnect} Knobs (024, S-017)

How to exercise the three reset knobs. Each witness maps to a `contracts/reset-knobs.md` clause and is written RED-first.

## Configure

```cpp
fixpp::session::SessionConfig cfg;
// ... existing required fields (ids, dictionary, security_profile, store_factory, ...)
cfg.reset_on_logon      = true;   // reset seqnums to 1 at logon; initiator announces via 141=Y
cfg.reset_on_logout     = true;   // reset at Logout teardown (sent OR received)
cfg.reset_on_disconnect = true;   // reset on ANY disconnect (incl. abnormal drop)
// all default false (QuickFIX-compatible no-op) if unset
```

## Unit witnesses (`tests/session/test_reset_on_lifecycle.cpp`)

| Witness | Clause | RED-first asserts |
|---------|--------|-------------------|
| `ResetOnLogon_Initiator_ResetsAndEmits141` | C2.1, C2.6 | Seed seqnums non-1 (`set_counters_for_test`), `reset_on_logon=true`, open as initiator → captured outbound Logon has `MsgSeqNum=1` AND `141=Y`; persisted counters == 1. |
| `ResetOnLogout_DrivesNext141OnInitiatorLogon` | C2.1 | `reset_on_logout=true`, `reset_on_logon=false`; after a prior teardown left seqnums `{1,1}`, open as initiator → outbound Logon carries `141=Y` (the OR-of-three predicate, not `reset_on_logon`-only). |
| `ResetOnDisconnect_DrivesNext141OnInitiatorLogon` | C2.1 | `reset_on_disconnect=true`, `reset_on_logon=false`; prior teardown left `{1,1}`, open as initiator → outbound Logon carries `141=Y`. |
| `ResetOnLogon_Acceptor_AdmitsFresh34eq1_LocalExpectedGt1` | C2.2, C2.5, FR-003 | Seed acceptor local `next_inbound_ > 1`, `reset_on_logon=true`, feed a fresh peer Logon `34=1` → **no disconnect, no ResendRequest**, reaches `Active`, seqnums `{1,1}` (the pre-validation reset; RED against an after-`check_inbound` placement). |
| `ResetOnLogon_Acceptor_ResetsIdempotentWith141` | C2.2, C5.1 | Seed non-1, `reset_on_logon=true`, feed an inbound Logon carrying `141=Y` → seqnums `{1,1}` AND **exactly one** observable `store_->reset()` I/O (the single combined `need_logon_reset` decision, NOT two store rewrites). |
| `ResetOnLogon_Off_Inbound141_StoreFailure_StillActive` | C2.6, FR-001 | **All 024 knobs off**, feed an inbound Logon carrying `141=Y`, inject a `store_->reset()` failure → session STILL reaches `Active` (the 013-only received-`141` path keeps I-07 logged-then-proceed; RED against the knob-driven fatal disposition leaking onto the all-off path). |
| `ResetOnLogon_Off_No141Beyond013` | C2.3 | `reset_on_logon=false`, policy=`bilateral_lenient` → outbound Logon has NO `141=Y` (today's behavior); no logon-time reset. |
| `ResetOnLogon_AllPolicies_LogonClean` | C2.4 | For each `reset_seqnum_policy_field` ∈ {strict, lenient, unilateral} with `reset_on_logon=true` → session reaches `Active`, no wedge. |
| `ResetOnLogon_SuppressesPreResetGap` | C2.5 | Initiator: seed a pre-reset inbound gap, `reset_on_logon=true`, logon → no ResendRequest emitted for seqnums below the reset point. |
| `ResetOnLogon_DurableStoreFailure_BlocksActive` | C2.6 | `reset_on_logon=true`, inject a `store_->reset()` failure on the Logon path → session does NOT reach `Active` (error propagated, not swallowed). |
| `ResetOnLogout_LocalInitiated_Resets` | C3.1 | `reset_on_logout=true`, drive a locally-initiated graceful Logout (`close(graceful)`) → persisted counters == 1. |
| `ResetOnLogout_PeerInitiated_Resets` | C3.1 | `reset_on_logout=true`, feed an inbound peer Logout (`35=5`) → teardown via terminal close still resets; persisted counters == 1 (RED against a `close_mode::graceful`-only keying). |
| `ResetOnLogout_Off_Preserves` | C3.2 | `reset_on_logout=false` → Logout preserves non-1 counters. |
| `ResetOnDisconnect_AbnormalDrop_Resets` | C4.1, C4.2 | `reset_on_disconnect=true`, drive a raw transport EOF (no Logout) → persisted counters == 1. |
| `ResetOnDisconnect_Off_Preserves` | C4.3 | `reset_on_disconnect=false` → disconnect preserves non-1 counters. |
| `ResetOnLogoutAndDisconnect_DoubleTrigger_OneStoreReset` | C5.1 | both on, graceful Logout then disconnect → exactly **one** observable `store_->reset()` I/O (single-fire guard), final counters `{1,1}`. |
| `ResetOnLogon_BothRoles_Resets` | C5.2 | `reset_on_logon` reset holds as initiator AND acceptor. |
| `ResetOnLogout_BothRoles_Resets` | C5.2 | `reset_on_logout` reset holds as initiator AND acceptor (incl. the peer-initiated Logout path). |
| `ResetOnDisconnect_BothRoles_Resets` | C5.2 | `reset_on_disconnect` reset holds as initiator AND acceptor. |

Run:

```bash
ctest --preset linux-clang-debug -R reset_on_lifecycle
# sanitizers:
ctest --preset linux-clang-asan  -R reset_on_lifecycle
ctest --preset linux-clang-tsan  -R reset_on_lifecycle   # R4 close-drain ordering
```

## Live interop cells — both roles (extends the 018 fixture + parent `phase-9-harness/`)

| Cell | Clause | Asserts |
|------|--------|---------|
| `reset_on_logon_initiator` (QFcpp + QFJ acceptors) | C6.1 | fixpp initiator with `reset_on_logon` → Logon carries `141=Y`, the live counterparty accepts, both sides resync from seqnum 1; captured wire shows `141=Y` + `34=1`. |
| `reset_on_logon_acceptor` (QFcpp + QFJ initiators) | C6.2 | A live QFcpp/QFJ **initiator** sends a `141=Y` Logon at fresh `34=1`; the fixpp **acceptor** with `reset_on_logon` resets before validation, admits `34=1` (no disconnect, no ResendRequest), reaches `Active`, both sides resync from seqnum 1. |

```bash
ctest --preset linux-clang-debug -R interop -L reset   # skips cleanly without a counterparty
```

## No-heap / regression gates

- The reset branches in `open()`/`close()` add no heap (only branch + existing-awaitable calls). Assert via a mallocnesia/alloc-guard witness on the reset path (not merely "covered by existing discipline") — `/speckit-verify` Step 6.
- All-knobs-off regression: the existing session + `013` `bilateral_strict` suites stay green unchanged (the only delta is gated behind opted-in knobs).
