# Data Model: RefreshOnLogon — per-logon re-hydrate (025)

Phase 1. Entities, the exact change-set, the force-trigger truth table, invariants, and the witness
matrix. Anchors verified against merged `session.cpp` (submodule main `0b9c8b8`).

## Entities

- **`refresh_on_logon`** — additive `SessionConfig` `bool`, explicit default `false`
  ([const §XII.5]). When `true` (and the policy is non-strict and the store is persistent), the
  session re-hydrates both seqnum counters from the store at each logon.
- **`ensure_hydrated_(bool apply_inbound_seed, bool force = false)`** — the existing 029 one-shot
  hydrate, extended with a `force` flag that bypasses the `hydrated_` early-return.
- **Persisted counters** — `store_->next_seqnum(direction, /*increment=*/false)` for inbound +
  outbound; the store-wins source the re-hydrate reads.
- **In-memory counters (manager)** — `SeqnumManager` inbound/outbound; the target
  `SeqnumManager::hydrate(in,out)` overwrites.
- **`reset_seqnum_policy_field`** — `bilateral_strict` (default) / `bilateral_lenient` /
  `unilateral`. The suppression discriminator (D-RoL-3).
- **`store_is_persistent_`** — the 029 discriminator (`MessageStoreFactory::yields_persistent_store()`
  captured at `open()`); a `false` value makes the re-hydrate a no-op (INV-H4 reused).

## Change-set (exhaustive)

| # | File | Change |
|---|------|--------|
| C1 | `include/fixpp/session/session_config.hpp` | `+ bool refresh_on_logon = false;` near `reset_on_logon` (`:247`), with a doc comment (QuickFIX `RefreshOnLogon`; store-wins; standby-only; no-op under `bilateral_strict`). |
| C2 | `include/fixpp/session/session.hpp` | `ensure_hydrated_` decl gains a defaulted `bool force = false` second param. |
| C3 | `src/session/session.cpp` `ensure_hydrated_` | The `hydrated_` early-return (`:564-566`) becomes `if (hydrated_ && !force) co_return ok;`. Nothing else changes. |
| C4 | `src/session/session.cpp` `emit_initiator_logon_` (`:658`) | The `ensure_hydrated_(!withhold_inbound)` call gains `, /*force=*/refresh_active_` where `refresh_active_ = cfg_.refresh_on_logon && cfg_.reset_seqnum_policy_field != reset_seqnum_policy::bilateral_strict`. |
| C5 | `src/session/session.cpp` acceptor `NotConnected` Logon (`:1738`) | The `ensure_hydrated_(!withhold_inbound)` call gains the same `, /*force=*/refresh_active_`. |
| C6 | `tests/session/test_refresh_on_logon.cpp` | New witness suite (W1–W8 below). |
| C7 | `tests/interop/happy/hp_fix44_restart_resume_test.cpp` | `+` standby re-hydrate live cell (skip-without-counterparty). |
| C8 | `spec/feature-catalogue.md` / `coverage-index.md` / `behaviors-and-limitations.md` | §VI delta (S-018 flip, coverage rows, L-024-1 retire, L-025-1 add) — Polish. |

> The `force` expression `cfg_.refresh_on_logon && policy != bilateral_strict` may be hoisted to a
> small local/helper at each call site for readability, but is NOT a new member unless an
> implementer prefers a private `refresh_active_()` accessor (either is fine; no behavior change).

## Force-trigger truth table

For a given logon event, the re-hydrate runs iff **all** of: `refresh_on_logon == true` AND
`policy != bilateral_strict` AND `store_is_persistent_ == true`. (The last is enforced inside
`ensure_hydrated_` at `:576`; the first two at the call site.)

| `refresh_on_logon` | policy | store | cold open (first logon) | 2nd+ logon / reconnect |
|--------------------|--------|-------|--------------------------|------------------------|
| `false` (default) | any | any | 029 one-shot hydrate (latched) | **no re-hydrate** (INV-H3) |
| `true` | `bilateral_strict` (default) | persistent | 029 one-shot hydrate (latched) | **no re-hydrate** (suppressed, D-RoL-3) |
| `true` | `lenient`/`unilateral` | persistent | 029 one-shot hydrate | **re-hydrate** (store-wins up/down) |
| `true` | `lenient`/`unilateral` | non-persistent | no-op (INV-H4) | no-op (INV-H4) |

> Cold open is **always** the 029 one-shot (`force=false` there is moot — `hydrated_` is still
> `false` on the first call, so the early-return is not taken regardless of `force`). The knob's
> only observable effect is on the **2nd+** logon, where it bypasses the latch. This is why the
> `bilateral_strict` cold-open seed is still the 029 path — the inherited L-029-3 gap (D-RoL-6,
> Gate A resolved: **defer**, NOT a 025 guarantee), where the 029 cold-open seed under
> `bilateral_strict` + a non-1 store can emit a `141=Y`+non-1 cold Logon. 025 never reaches it
> (refresh is gated to non-strict); the gap is a property of the policy, deferred to a 029/024
> follow-up.

## Invariants

- **INV-RoL-1 (default-off no-op)**: `refresh_on_logon == false` ⇒ every path is byte-identical to
  029 (the `force` arg is `false`, the `hydrated_` early-return is unchanged). (FR-004/FR-010)
- **INV-RoL-2 (non-persistent no-op)**: `store_is_persistent_ == false` ⇒ no store read on refresh,
  byte-identical to knob-off (reuses 029 INV-H4 at `:576`). (FR-005)
- **INV-RoL-3 (strict suppression — the 025 re-hydrate delta)**: `policy == bilateral_strict` ⇒ the
  per-logon **re-hydrate** (the 025 2nd+-logon delta) does not run (zero extra reads); the 2nd+ logon
  introduces no NEW malformed Logon attributable to the knob (establishment proceeds exactly as the
  knob-off `bilateral_strict` path). This invariant covers the **re-hydrate**, not the cold-open
  seed: the strict + non-1 **cold-open** path is the inherited **L-029-3** gap (D-RoL-6, deferred),
  NOT a 025 guarantee. (FR-008)
- **INV-RoL-4 (store-wins)**: when the re-hydrate runs, the manager counters equal the store's
  values (up or down); no advance-only clamp. (FR-003)
- **INV-RoL-5 (RC-1 preserved)**: a forced re-hydrate on an acceptor reset Logon (`34=1,141=Y`)
  still withholds the inbound seed (`apply_inbound_seed = !(peer_sent_reset || reset_on_logon)`),
  so the received-141 reset owns the post-state and the peer's `34=1` is accepted. (FR-009)
- **INV-RoL-6 (fatal on read failure)**: a refresh store-read/hydrate failure transitions to
  `Disconnected` with no partial seed (reuses the 029 disposition). (FR-006)
- **Carried 029 invariants**: INV-H1 (store ≤ manager lower bound — note: a store-wins refresh on an
  ACTIVE session can transiently violate the *manager's monotonicity*, which is the operator-accepted
  regression L-025-1, NOT an INV-H1 store-side violation); INV-H3 (cold one-shot) holds for the
  knob-off and `bilateral_strict` paths.

## Witness matrix

| ID | Witness | Asserts | FR/SC |
|----|---------|---------|-------|
| W1 | knob-on, lenient, store advanced **above** live → logon | both manager counters = store's higher values | FR-002/003, SC-001 |
| W2 | knob-on, lenient, store set **below** live → logon | both manager counters = store's lower values (store-wins DOWN) | FR-003, SC-002 |
| W3 | knob-**off**, 2nd logon/reconnect | no store re-read (call-count unchanged after cold) + counters retained + full regression green | FR-004/010, SC-003 |
| W4 | knob-on, **non-persistent** store → logon | zero store reads, byte-identical to knob-off | FR-005, SC-004 |
| W5a | knob-**on**, **bilateral_strict**, non-1 store → **2nd logon** | re-hydrate suppressed (zero EXTRA reads beyond the 029 cold one-shot); no NEW malformed Logon attributable to the knob (establishment == knob-off strict path) | FR-008, SC-005 |
| W5b | knob-**OFF**, **bilateral_strict**, non-1 store → **cold open** | the **L-029-3 inherited-gap witness** — documents/asserts the inherited 029 cold-open behaviour AS-IS; asserts ONLY what holds (e.g. that the cold seed runs). Does NOT assert the cold Logon is well-formed (it may carry `141=Y`+non-1). Clearly labeled "inherited 029 gap, NOT a 025 guarantee" — must not be mistaken for a correctness witness. | (L-029-3 gap; not a 025 FR/SC) |
| W6 | knob-on, lenient, **acceptor** non-1 inbound, **cold** logon (first connection — `hydrated_==false`), peer reset Logon `34=1,141=Y` | inbound seed withheld; peer `34=1` accepted; reaches Active. Witnesses RC-1 inbound-withhold under knob-on (force=true is inert on cold open but the store IS read and the withhold IS exercised). | FR-009, SC-006 |
| W7 | knob-on, lenient, refresh store-**read failure** (fault-injecting store) | `Disconnected`, no partial seed, no new error slot | FR-006, SC-007 |
| W8 | knob-on, lenient, **no-heap** under mallocnesia on the per-logon re-hydrate | zero allocations on the re-hydrate **apply step** (`SeqnumManager::hydrate()`), the same proxy 029 W8 uses | [const §VIII.5] |
| W9 (interop) | fixpp standby (lenient, knob-on) re-hydrates a primary-advanced store, logs on vs QFcpp/QFJ | resumes at the adopted counters, no fatal | [const §VII.6] |

> **FR-002 (per-logon re-hydrate) coverage scope:** FR-002 is witnessed for the **initiator** role
> only (W1/W2 warm-force via `drive_reconnect()`; W7 warm-force read-failure). The acceptor
> force-bypass (`/*force=*/refresh_active` at `session.cpp:1754`) covers the same-connection
> 2nd-logon edge, but that path is **not reachable through the current engine**: `engine.cpp:864`
> constructs a fresh `Session` per accepted connection (`hydrated_` is never reset), so every
> acceptor logon arrives on a fresh Session with `hydrated_==false` (force is then inert — the
> cold-open path runs unconditionally). A 2nd Logon received in `Active` state is dispatched to
> the dup-Logon-in-Active `Reject` arm, not back through the `NotConnected` Logon handler at
> `:1754`. The acceptor `force` wiring is therefore **dead-but-harmless**: it correctly handles
> the same-connection re-Logon edge if Session reuse across acceptor reconnect is ever introduced,
> but the warmed acceptor latch-bypass has no reachable test vehicle today. W6 witnesses the
> cold-acceptor RC-1 withhold (force=true inert, store IS read). See L-025-2 in
> `spec/behaviors-and-limitations.md`.
>
> W3's "no re-read" assertion must check the **store read call-count** directly (a counting test
> store), not a proxy — per [[feedback_witness_asserts_named_postcondition_not_proxy]]. **W5a**
> asserts the 025-delta suppression: zero EXTRA reads (the re-hydrate did not run) AND no NEW
> malformed Logon attributable to the knob. **W5b** is the L-029-3 **gap witness** for the knob-OFF
> strict + non-1 **cold open**: it documents the inherited 029 behaviour as-is and asserts ONLY what
> holds — it does NOT assert cold-open validity (the strict cold seed may emit `141=Y`+non-1) and
> must be clearly labeled so a future reader cannot mistake it for a correctness witness
> (per [[feedback_witness_asserts_named_postcondition_not_proxy]]). W2 (store-wins DOWN) is the
> witness that distinguishes store-wins from advance-only — it MUST move the counter below the live
> value.
>
> **FR-012** (catalogue S-018 flip + B&L L-024-1 retire / L-025-1 add) has no synthetic runtime
> witness by design: it is a doc-surface §VI delta discharged at **Polish** and verified by the
> mandatory **`/speckit-checklist-audit`** gate (the 027/028 precedent), not by a W-row over
> `feature-catalogue.md`/`coverage-index.md` bytes.
