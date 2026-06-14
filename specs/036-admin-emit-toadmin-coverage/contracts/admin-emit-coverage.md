# Contract — engine-originated admin/app emit observation coverage

Internal contract (no public header surface). Governs which `Application` callback every
engine-originated frame emit invokes, and the throw/veto semantics. Amends **FR-008** (019) from
partial to full coverage.

## C1 — Administrative emit (`Reject(35=3)`, `Logout(35=5)`, and the rest of the admin set)

For **every** engine-originated administrative frame, before the frame is stored or transmitted:

The contract is **callback before store/transmit**. For seqnum-assignment ordering, the new admin
sites adopt the **1137-Reject `assign`-then-`fire` shape** (`session.cpp:2113-2126`). (The 15 wired
sites have **mixed** assign-vs-callback ordering — the 1137-Reject is the assign-then-fire outlier;
the wired Logouts/Heartbeats fire first — and are unchanged. The choice is behaviourally immaterial for admin:
non-vetoable, and a throw → Disconnected regardless of seqnum-assignment order.)

```
assign_outbound()                       // (admin: seqnum assigned first, adopting the 1137-Reject shape)
if (!fire_to_admin_(frame)) {           // invokes Application::toAdmin if registered; false ⇔ threw
    record_state_transition_(Disconnected);
    co_return std::unexpected(app_callback_threw);
}
store_then_emit(seq, frame);
```

- **Pre**: the frame is fully built; an `Application` may or may not be registered.
- **Post (no app / non-throwing)**: `toAdmin` invoked exactly once (or skipped if no app); frame
  transmitted unchanged. `toAdmin` is **inspect-only** — it cannot suppress or mutate the frame.
- **Post (throwing)**: `app_callback_threw` surfaced; session `Disconnected`; frame **not**
  transmitted (the throw is caught inside `fire_to_admin_`'s `parse_and_dispatch_`, so the noexcept
  boundary is respected).
- **No-`Application`**: `fire_to_admin_` returns true at entry (`session.cpp:332`) — byte-for-byte
  no-op.

## C2 — Application-message emit (`BusinessMessageReject(35=j)`)

The `35=j` is an application message (outside the FIX admin set `A/0/1/2/3/4/5`). It is built inside
the `fromApp`-reject branch (`session.cpp:3232-3267`), and that whole branch **falls through** to the
inbound-seqnum durable persist `persist_inbound_advance_()` at `:3279`. **The veto must preserve that
fall-through** — it suppresses only the BMR's `assign_outbound` + `store_then_emit`, it does NOT
early-return. (`send_impl`'s `toApp` block early-returns on veto because it has no downstream persist;
the BMR site does — so "reuse `send_impl`'s semantics" means reuse the *callback + veto/throw outcome*,
NOT its control flow.) The `toApp` fires **before `assign_outbound`** (veto must not consume an
outbound seqnum):

```
if (bmr_r) {
    bool suppressed = false;
    if (engine_.application != nullptr) {
        auto cb_r = parse_and_dispatch_(*bmr_r, kInboundParseArena,
                                        [&](mv, sid){ return application->toApp(mv, sid); });
        if (!cb_r) {
            if (cb_r.error() == app_callback_threw) {
                co_await close(close_mode::terminal);     // throw → terminal close (persist moot)
                co_return std::unexpected(app_callback_threw);
            }
            suppressed = true;          // app_do_not_send (veto): drop the 35=j, but CONTINUE
        }
    }
    if (!suppressed) {
        assign_outbound();              // only when not vetoed — veto consumes NO outbound seqnum
        store_then_emit(bmr_seq, *bmr_r);   // (Disconnected on failure, as today)
    }
}
// FALL THROUGH (both not-vetoed and vetoed) to persist_inbound_advance_() at :3279
```

- **Pre**: a `fromApp` veto has just produced this engine-originated `35=j`; the **in-memory** inbound
  seqnum was advanced by `check_inbound`, but its **durable persist** is downstream at `:3279`.
- **Post (non-throwing, not vetoed)**: `toApp` invoked once; `35=j` transmitted; outbound seqnum
  consumed; inbound advance persisted at `:3279` (unchanged from today).
- **Post (veto, `app_do_not_send`)**: `35=j` **not** stored/transmitted; session **stays Active**;
  **no outbound seqnum consumed**; **inbound advance STILL persisted at `:3279`** (no early return) —
  otherwise durable < in-memory → restart reprocesses the message (under-persist silent loss, the
  inverted twin of [[feedback_unconditional_persist_at_multiexit_gate_breaks_lowerbound]]).
- **Post (throwing)**: `app_callback_threw`; session terminally closed (persist moot — terminal).
- **Routing rule**: `35=j` MUST NOT route through `toAdmin`.

## C3 — Exact-count observation invariant (the durable regression)

The admin exact-count equality holds **only with a registered, non-throwing `Application`**. The BMR
`toApp` equality holds **only on the non-veto path**; the veto path is carved out explicitly below.

```
// Registered, non-throwing Application:
toAdmin_calls(S)  ==  count(administrative frames on the wire in S)        // exact equality

// BMR (35=j), non-veto path:
toApp_calls_for_35j(S) == count(35=j frames on the wire in S)             // BMR counted separately

// BMR (35=j), veto path (app_do_not_send):
toApp_calls_for_35j(S) == 1  AND  wire_35j_count(S) == 0                  // fired once, not on wire
   AND  no outbound seqnum consumed
   AND  inbound durable seqnum advanced                                   // cross-ref INV-COV-5 / C2
```

The administrative count **excludes** `35=j`. This is **exact-count** equality (not "≥1", not
subset-presence): a future admin emit site added without `fire_to_admin_` breaks the equality, and a
double-fire breaks it too. See [[feedback_completeness_gate_exact_set_not_subset]]. The no-`Application`
helper caller (`:3215`, reachable only when `application == nullptr`) is witnessed as an FR-006
byte-identity no-op, not a callback count (see the quickstart cell `EmitSessionReject_NoAppUnknownType_NoOp`).

## C4 — Documentation obligations (FR-008)

- The stale in-source comment at `session.cpp:2096-2098` (033's warning to *avoid* the
  `fire_to_admin_`-less `emit_session_reject_` helper) MUST be inverted — the helper now fires
  `toAdmin`.
- `spec/behaviors-and-limitations.md` gains a row enumerating the coverage (which engine emits fire
  `toAdmin` vs `toApp`), since the prior `toAdmin` limitation rows never scoped coverage.
- The 019 FR-008 anchor records (spec/tasks) get a dated note: coverage extended to the full Reject
  family + Guard-3 Logout + BMR-via-`toApp` — no merged-history rewrite.
