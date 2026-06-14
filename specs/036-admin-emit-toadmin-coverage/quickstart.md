# Quickstart — the exact-count observation witness

How to reproduce and verify the FR-008 coverage closure. Target file:
`tests/session/test_admin_emit_toadmin_coverage.cpp` (new).

## The test double — counting + throwing/vetoing Application

```cpp
struct CoverageApp : fixpp::session::Application {
    int toAdmin_calls = 0;
    int toApp_calls   = 0;
    enum class Mode { Count, Throw, Veto } mode = Mode::Count;

    void toAdmin(const MessageView& mv, const SessionID&) override {
        ++toAdmin_calls;
        if (mode == Mode::Throw) throw std::runtime_error("toAdmin boom");
        // admin is inspect-only: no veto path
    }
    // toApp may veto (DoNotSend) or throw, mirroring the originate-path contract
    fixpp::core::expected_t<void> toApp(const MessageView& mv, const SessionID&) override {
        ++toApp_calls;
        if (mode == Mode::Throw) throw std::runtime_error("toApp boom");
        if (mode == Mode::Veto)  return std::unexpected(fixpp::core::error::app_do_not_send);
        return {};
    }
    // fromApp returns a reject to PROVOKE the engine's BusinessMessageReject(35=j)
    // (set per-cell where the BMR site is under test)
};
```

(Match the real `Application` callback signatures — `toApp` returns `expected_t<void>` so a veto is
`app_do_not_send`; `toAdmin` is `void`/inspect-only. Use the existing 019 app-callback test doubles
as the shape reference.)

## Per-site cells (Decision 5 — the 10 sites span distinct FSM states)

Each cell drives the session to exactly the state that provokes one emit, captures the wire frames
(engine-log seam / mock transport), and asserts the invariant **within the cell**:

| Cell | Provoke | Assert |
|---|---|---|
| `EmitSessionReject_FromAdminVeto` | inbound admin frame a `fromAdmin` vetoes → helper `:1728` via `:3026` (app **is** registered here) | `toAdmin_calls == 1` and one `35=3` on wire |
| `EmitSessionReject_NoAppUnknownType_NoOp` | unknown app MsgType in Active with **no** `Application` → helper via `:3215`. This caller is structurally reachable **only** when `application == nullptr` (helper comment `:1714-1716`), so `fire_to_admin_` is always a no-op there. **Witness as a byte-identical no-op**, NOT a `toAdmin` count: assert the `35=3` is emitted and the wire bytes match the pre-036 baseline. (Registering a counting app would change the path — an app-registered unknown-type msg goes through `fromApp`, not this reject.) |
| `Reject_Q3SendingTimeAccuracy` | established session, stale `52=` → `:2400` reject + `:2421` logout | `toAdmin_calls == 2`, two admin frames (reject **and** paired logout) |
| `Reject_SequenceResetVeto` | `fromAdmin` veto on inbound `35=4` → `:2484` | `toAdmin_calls == 1` |
| `Reject_021ArmC_Malformed122` | inbound PossDup with malformed `122` (Arm C) → `:2599` | `toAdmin_calls == 1` |
| `Reject_021RC1_Malformed122` | RC#1 malformed-122 → `:2644` | `toAdmin_calls == 1` |
| `Reject_021ArmD` | 021 Arm D → `:2675` reject + `:2696` logout | `toAdmin_calls == 2` |
| `Reject_LogoutVeto` | `fromAdmin` veto on inbound `35=5` → `:2946` | `toAdmin_calls == 1` |
| `Reject_SeqResetNewSeqNoTooLow` | inbound `35=4` `NewSeqNo` < expected → `:4576` | `toAdmin_calls == 1` |
| `Logout_Guard3LogonAckSendingTime` | initiator LogonSent, peer Logon-ack with stale/absent `52=` → `:3368` | `toAdmin_calls == 1`, one `35=5` |
| `BMR_ToApp_Observed` | Active, `fromApp` rejects an inbound app msg → `:3249` | `toApp_calls == 1`, `toAdmin_calls == 0`, one `35=j` on wire |

**Exact-count aggregate** (C3): in each cell assert
`toAdmin_calls == count(admin frames on wire)` and the `35=j` is counted only on the `toApp` side.

## Throwing-callback variant (FR-003 / SC-003)

For each site, re-run with `mode = Throw` and assert:

```cpp
EXPECT_EQ(result.error(), fixpp::core::error::app_callback_threw);
EXPECT_TRUE(session_in_terminal_or_disconnected_state());
```

(Admin sites → `Disconnected`; the BMR `toApp` throw → terminal close.)

## BMR veto cell (FR-004 / SC-004 / INV-COV-5)

```cpp
app.mode = CoverageApp::Mode::Veto;   // toApp returns app_do_not_send
const auto inbound_durable_before = store_persisted_inbound_seqnum();
// provoke the BMR (fromApp reject) ...
EXPECT_EQ(app.toApp_calls, 1);
EXPECT_FALSE(wire_contains_msgtype("j"));      // 35=j suppressed
EXPECT_TRUE(session_is_active());              // stays Active
EXPECT_EQ(outbound_seqnum_after, outbound_seqnum_before);  // NO outbound seqnum consumed
// CRITICAL (INV-COV-5): the veto suppresses the BMR but must NOT skip the inbound persist —
// the durable inbound seqnum still advances (else restart reprocesses the message).
EXPECT_EQ(store_persisted_inbound_seqnum(), inbound_durable_before + 1);
```

This persist-on-veto assertion is the discriminating witness for the under-persist hazard: an
implementation that `co_return`s on the veto (skipping `persist_inbound_advance_()` at `:3279`) passes
the "stays Active / no 35=j on wire" checks but **fails this one** — exactly the bug the contract C2
control-flow guards against. Use a persistent store so the durable counter is observable.

## No-Application no-op (FR-006 / SC-005)

Run each provocation with **no** registered `Application` and assert the wire bytes + emit ordering
are byte-for-byte identical to the pre-change baseline (capture once on `main`, compare).

## Sanitizers

Run the new binary under ASan / UBSan / TSan (session suite is the regression set). The callback
fires on the session strand (`[const §XI.4]`) — TSan must stay clean; a multi-thread pool variant is
not required (no new cross-thread surface — the callbacks run where the 16 wired sites already run).
