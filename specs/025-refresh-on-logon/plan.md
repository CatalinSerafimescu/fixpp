# Implementation Plan: RefreshOnLogon — per-logon re-hydrate of seqnum counters from the store

**Branch**: `025-refresh-on-logon` | **Date**: 2026-06-09 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/025-refresh-on-logon/spec.md`

## Summary

Add an opt-in, default-off `refresh_on_logon` `SessionConfig` knob that makes a
persistent-store-backed session **re-read both persisted seqnum counters from the store at
each logon** (store-wins, can move up or down) — the QuickFIX `RefreshOnLogon` semantic for
hot-standby/backup topologies. The feature is a **thin latch-bypass on the merged 029 spine**:
029 already ships `ensure_hydrated_(apply_inbound_seed)` (reads both counters via
`SeqnumManager::hydrate(in,out)`, persistent-store discriminator `store_is_persistent_`,
fatal read-failure disposition, acceptor reset-Logon inbound-seed withhold), but as a **one-shot**
cold-open hydrate (`hydrated_` latch, INV-H3: never re-hydrates on reconnect). 025 adds a `force`
parameter that bypasses **only** the `hydrated_` early-return so the same machinery re-runs on
each logon, gated by the knob.

**Two design facts pin the whole slice (clarify-settled, reference-grounded):**

1. **Store-wins / unconditional** (D-RoL-1): the re-hydrate sets the manager to the store's
   values whether that moves them up or down — required so a standby follows a primary's
   reset-to-1 (DOWN). NOT advance-only/`max`. Mirrors QFcpp `Session::refresh()` (3 sites) and
   QFJ `refreshState()` (2 sites), both unconditional store reloads. Active-session reconnect can
   therefore regress past the INV-H1 lower-bound lag — an **operator responsibility** (enable on
   standby topologies only), documented L-025-1, exactly as QuickFIX.
2. **Suppressed under `bilateral_strict`** (D-RoL-3 / FR-008): `bilateral_strict` (the **default**
   policy) emits an unconditional `ResetSeqNumFlag(141=Y)` on the initiator Logon
   (`session.cpp:713-715`, arm A — a fixpp-024 construct with **no** analogue in QFcpp/QFJ/Fix8,
   all of which `{1,1}`-guard the reset flag via `shouldSendReset()`/`isResetNeeded()`/the
   reset-couples-to-`{1,1}` rule). A store-wins hydrate to a non-1 outbound under `bilateral_strict`
   would build a **malformed Logon** (`141=Y` + non-1 body, violating the FIX `141=Y` ⟹
   `MsgSeqNum=1` rule). So the re-hydrate fires **only under `bilateral_lenient`/`unilateral`** and
   is a no-op under `bilateral_strict`. `reset_on_logon` (024) needs no special-casing — the
   existing 029 ordering (outbound hydrate → durable reset → reset-flag) already yields body
   `34=1`. **Consequence:** since `bilateral_strict` is the default, enabling `refresh_on_logon`
   out of the box is a no-op until the operator selects a non-strict policy (L-025-1).

**Code shape (minimal — see [data-model.md](./data-model.md) §Change-set):** `+1` `SessionConfig`
bool; `+1` `force` parameter on `ensure_hydrated_` (default `false`; bypasses the `hydrated_`
early-return at `session.cpp:564-566`, nothing else); the two existing 029 call sites
(`emit_initiator_logon_` `:658`, acceptor `NotConnected` Logon `:1738`) pass
`force = cfg_.refresh_on_logon && cfg_.reset_seqnum_policy_field != reset_seqnum_policy::bilateral_strict`.
No change to the cold path (`force=false` ⇒ byte-identical 029), no new manager method, no new
store surface, no wire/error/codegen/C-ABI change.

**Key grounding (source sweep + reference engines, in [research.md](./research.md)):**

- **The re-hydrate reuses the entire 029 read/seed/withhold/fatal pipeline** (D-RoL-4): the only
  delta from cold-open is bypassing the `hydrated_` one-shot guard. The `hydrating_` re-entrancy
  guard, the `store_is_persistent_` skip (INV-H4 / non-persistent no-op, FR-005), the both-reads-
  before-mutate no-partial-seed rule, the `apply_inbound_seed` withhold (RC-1), and the fatal
  read-failure disposition (FR-006) are all reused as-is. The `hydrated_` latch is left set by the
  forced call — harmless, since `force` ignores it.
- **The two call sites are the existing 029 sites, re-entered each logon** (D-RoL-5):
  `emit_initiator_logon_()` runs on `open()` AND every `drive_reconnect()` (initiator logon event);
  the acceptor `NotConnected` Logon case runs on every received Logon (acceptor logon event). Both
  hydrate **before** their outbound counter is sampled into the Logon body (initiator `peek_outbound()`
  `:699`; acceptor reply `peek_outbound()` `:1951`). This matches QFJ's two sites (send +
  Logon-receipt) and is sufficient for store-wins parity; no third site is added.
- **Suppression gate placement** (D-RoL-3): the `policy != bilateral_strict` test lives at the
  **call sites** (in the `force` expression), NOT inside `ensure_hydrated_`. This keeps the 029
  cold path's internals untouched and means under `bilateral_strict` the refresh does **zero**
  extra store reads.
- **Acceptor received-141 still wins** (FR-009): unchanged from 029 — the acceptor call site already
  passes `apply_inbound_seed = !(peer_sent_reset || cfg_.reset_on_logon)` (`:1738`), so a forced
  re-hydrate on a peer reset Logon still withholds the inbound seed and the `:1925` received-141
  reset owns the post-state. The peer's `34=1` is accepted, not rejected too-low.
- **Inherited gap, Gate A RESOLVED → DEFER (L-029-3, NOT fixed here):** 029's **cold-open**
  hydrate seeds the outbound counter unconditionally regardless of policy, so a `bilateral_strict`
  initiator (`reset_on_logon==false`) restarting against a persistent non-1 store already emits a
  malformed cold-open Logon (`141=Y` + non-1 body) — untested in 029 (its hydrate tests all use
  `bilateral_lenient`). 025's knob, gated to non-strict, never makes this worse and is correct in
  isolation. **Gate A resolved to DEFER, not fold in:** the one-line `policy != bilateral_strict`
  guard on the outbound seed was REJECTED because (a) it breaks **FR-010**'s byte-identity on the
  knob-OFF strict cold-open path, (b) it is a half-fix of the broader `bilateral_strict`-reset-vs-`141`
  question (`:1795` handshake, out of scope), and (c) 025's call-site gate already prevents the
  feature from reaching the path. The inherited gap is a property of the **policy**, recorded as
  **L-029-3** — a deferred 029/024 follow-up (D-RoL-6). 025 stays a true thin slice.

## Technical Context

**Language/Version**: C++23 (Clang; asio awaitables, `std::expected`) — [const §II]
**Primary Dependencies**: `Session::ensure_hydrated_` (`session.cpp:561`), its two call sites
(`emit_initiator_logon_` `:658`; acceptor `NotConnected` inbound-Logon `:1738`), `SessionConfig`
(`session_config.hpp` — new bool near `reset_on_logon` `:247`), `reset_seqnum_policy_field`
(`:227`). No new third-party deps, no codegen, no wire field, no new error slot, no new manager or
store method.
**Storage**: the existing `MessageStore` (008/FileStore) — the re-hydrate issues the same two
`next_seqnum(dir, false)` reads 029's cold hydrate already does, now per-logon instead of once. No
store schema or interface change.
**Testing**: GoogleTest; ASan/UBSan/TSan; coverage llvm-cov. Witnesses: store-advanced-above /
store-set-below per-logon re-hydrate (store-wins up/down); default-off second-logon does-not-re-read
+ full regression byte-identity; non-persistent store no-op (zero reads) with knob on;
`bilateral_strict` suppression (zero re-hydrate reads + no NEW malformed Logon attributable to the
knob); acceptor received-141
still wins under refresh; refresh read-failure → fatal. Plus a live interop cell (skip-without-
counterparty): standby re-hydrate against a primary-advanced store. — [const §VII, §IX]
**Target Platform**: Linux/Clang Tier-1 (sanitizer matrix); the live cell runs against QFcpp/QFJ in
the parent harness.
**Project Type**: single C++ library (`fixpp`) + tests + interop-harness extension.
**Performance Goals**: when the knob is on under a non-strict policy, two extra counter-only store
reads per logon (no frame body, no new allocation); default-off path is byte-identical (the `force`
expression short-circuits, the cold one-shot is unchanged).
**Constraints**: `noexcept`/`expected_t` preserved; no new `std::mutex` in awaitable headers
([const §XV.9] — no new include; `ensure_hydrated_`/`hydrate` already exist); **default-off ⇒
byte-identical** (the `force` arg defaults `false`); **non-persistent store ⇒ byte-identical**
(INV-H4 reused); **`bilateral_strict` ⇒ no-op** (FR-008 call-site gate); store-wins (no
advance-only clamp); 029 INV-H1/H3 cold-path behavior unchanged for `force=false`.
**Scale/Scope**: +1 `SessionConfig` bool (`refresh_on_logon`, default `false`); +1 `force`
parameter on `ensure_hydrated_` (default `false`); the two existing call-site `force` expressions;
catalogue S-018 flip + coverage-index + B&L L-024-1 retire / L-025-1 add. New
`tests/session/test_refresh_on_logon.cpp` + a live interop cell. No FSM state, no new manager/store
method, no codegen/C-ABI/wire/error-slot change.

## Constitution Check

*GATE: must pass before Phase 0 (passed) and re-checked after Phase 1 (re-confirmed).*

| Article | Gate | Status |
|---------|------|--------|
| **II** Language | C++23/Clang, no new deps | ✅ PASS |
| **VI** Spec coverage | **Existing catalogue row `S-018`** (RefreshOnLogon, `[FIX-SL §4.3.12]`) flips `backlog`→`done` (no net-new row — S-018 already exists, descoped from 024). Add the `spec/coverage-index.md` S-018 entries (§4.3.12 + §4.4: `backlog`→`done`). Retire **L-024-1** (`behaviors-and-limitations.md:579` — "RefreshOnLogon NOT implemented") and add **L-025-1** (store-wins active-session-reconnect regression = operator responsibility / standby-only + the `bilateral_strict`-suppression no-op-under-default note). Normative refs in spec.md `## Normative References` (Article VI.5): QFcpp/QFJ `RefreshOnLogon`, Fix8 `recover_seqnums`, the FIX `141=Y`⟹`MsgSeqNum=1` rule, `[FIX-SL §4.3.12]`/`§4.4`. Exact delta below (Polish). | ⚠ RESOLVED (delta specified) |
| **VII** Testing/TDD | RED-first witnesses: (1) knob-on non-strict, store-advanced-above → re-hydrate UP; (2) store-set-below → re-hydrate DOWN (store-wins); (3) default-off second logon → NO re-read + counters retained + full regression byte-identity; (4) non-persistent store + knob-on → zero reads, no-op; (5) **W5a** `bilateral_strict` + knob-on + non-1 store → 2nd-logon re-hydrate suppressed (zero extra reads) + no NEW malformed Logon attributable to the knob (establishment == knob-off strict path); plus **W5b** the L-029-3 inherited-gap witness (knob-OFF strict cold open, asserts only what holds — NOT a 025 validity guarantee); (6) acceptor received-141 reset Logon under refresh → peer `34=1` accepted (RC-1 still holds); (7) refresh read-failure → fatal disconnect, no partial seed | ✅ planned |
| **VII.6** Interop | live cell (skip-without-counterparty): a fixpp standby (non-strict policy, `refresh_on_logon=on`) re-hydrates from a store a primary advanced, then logs on against a QFcpp/QFJ peer at the adopted counters | ✅ planned |
| **VIII.5** Allocator | the re-hydrate reads are counter-only (no frame body, no new container); reuses 029's non-allocating hydrate path; no-heap witness on the re-hydrate apply step (`SeqnumManager::hydrate()`) proxy, matching 029 (non-allocating ready-awaitable test store; full coroutine/store/reconnect path not witnessed) | ✅ planned (witnessed) |
| **IX.1** Coverage | ≥95/85 on the new `force` branch of `ensure_hydrated_` + the two call-site `force` expressions (incl. the `bilateral_strict` short-circuit) + the refresh read-failure branch | ✅ planned |
| **IX.2** Sanitizers | ASan/UBSan/TSan on the session logon-path changes + interop ctest | ✅ planned |
| **X** ABI | no C-ABI/error-slot/wire change. +1 `SessionConfig` bool (struct-layout change → source rebuild), +1 defaulted `force` param on a private method — source rebuild only, additive | ✅ source rebuild (additive) |
| **XI.4** Threading | the forced `ensure_hydrated_` runs on the existing session strand (emit/Logon-handler), same as the cold path; the `hydrated_`/`hydrating_` flags stay strand-confined; no new concurrency surface | ✅ PASS |
| **XII.5** No-implicit-default | `refresh_on_logon` has an EXPLICIT `= false` default (the byte-identical floor); the `force` param defaults `false` (the cold-path floor) | ✅ PASS |
| **XIV.2** Pluggable ≤5 pure-virtual | `MessageStore` stays at **4** pure-virtuals + `MessageStoreFactory` unchanged — the re-hydrate reuses the existing `next_seqnum(dir,false)` reads; no new store/factory surface | ✅ PASS (cap preserved) |
| **XV.9** Banned (`std::mutex` in awaitable hdr) | no new `#include`; `ensure_hydrated_`/`SeqnumManager::hydrate` already exist (029). The new `force` param adds no header edge | ✅ N/A (confirm at verify) |
| **XVI.3/4** /clarify before /plan | Session 2026-06-09 (1 asked: `bilateral_strict` composition → suppress, Option A) + reference sweep (QFcpp `shouldSendReset`/`refresh`, QFJ `isResetNeeded`/`refreshState` (2 sites), Fix8 `recover_seqnums`/`_reset_sequence_numbers`, FIX `141=Y`⟹`MsgSeqNum=1`) | ✅ PASS |
| **XVII.1** Gate A before /tasks | mandatory — runs after this plan, before `/speckit-tasks` | ⏳ PENDING (Gate A next) |

**Result**: PASS to proceed. The feature is a default-off, `bilateral_strict`-suppressed
latch-bypass on the 029 spine; the cold path and the non-strict/non-persistent floors are
byte-identical; no new store/manager/wire/error/codegen surface. One choice was **flagged for Gate A
and RESOLVED (round 1): DEFER** — the inherited 029 **cold-open** `bilateral_strict` malformed-Logon
gap is NOT closed inside 025 (fold-in rejected: breaks FR-010, half-fix, unnecessary given the
call-site gate); it is recorded as **L-029-3**, a deferred 029/024 follow-up.
The store-wins active-session-regression is an accepted operator responsibility (L-025-1),
matching QuickFIX. No unjustified violations.

**Exact §VI delta (applied at Polish):**
- `spec/feature-catalogue.md` **S-018**: `backlog`→`done`; set feature `025-refresh-on-logon`,
  evidence_pr `(pending merge)`, Tests `tests/session/test_refresh_on_logon.cpp` + the interop cell;
  update the gap-note to "shipped: per-logon store-wins re-hydrate on the 029 spine, suppressed
  under `bilateral_strict`".
- `spec/coverage-index.md` **§4.3.12** + **§4.4**: move S-018 from `backlog`/parked to `done` (025),
  source units (`ensure_hydrated_` `force` branch + the call-site gates) ↔
  `tests/session/test_refresh_on_logon.cpp` + interop cell.
- `spec/behaviors-and-limitations.md`: **retire L-024-1** (mark RefreshOnLogon IMPLEMENTED via 025,
  keep the ID with a "DISCHARGED by 025" status line); **add L-025-1** — (a) store-wins re-hydrate
  can regress an **active** session's counters past the INV-H1 lag on reconnect → enable
  `refresh_on_logon` **only on backup/standby topologies** (operator responsibility, same contract
  as QuickFIX); (b) `refresh_on_logon` is **suppressed under `bilateral_strict`** (the default), so
  it is a no-op until a non-strict reset-seqnum policy is selected. **Add L-029-3** as a deferred
  **inherited** limitation (Gate A resolved: DEFER, not folded in) — an **OPEN** gap: the 029
  cold-open `bilateral_strict` + non-1 store seed can emit a `141=Y`+non-1 cold Logon (a property of
  the policy, not the knob), routed to a 029/024 follow-up and **NOT closed by 025**.

## Project Structure

### Documentation (this feature)

```text
specs/025-refresh-on-logon/
├── plan.md              # This file
├── research.md          # Phase 0 — D-RoL-1..D-RoL-5 + reference sweep (QFcpp/QFJ/Fix8) + the 141 invariant
├── data-model.md        # Phase 1 — entities, change-set, the force-trigger truth table, invariants, witness matrix
├── quickstart.md        # Phase 1 — the RED witnesses
├── contracts/
│   └── refresh-knob.md   # Phase 1 — refresh_on_logon + force trigger + suppression + FR-008/009 contract
└── checklists/
    └── requirements.md   # spec quality checklist (done)
```

### Source Code (repository root)

```text
include/fixpp/session/
├── session_config.hpp   # +bool refresh_on_logon = false; (near reset_on_logon :247)
└── session.hpp          # ensure_hydrated_ decl gains a defaulted `bool force = false` param

src/session/
└── session.cpp          # ensure_hydrated_: bypass the `hydrated_` early-return when force;
                         #   emit_initiator_logon_ (:658) + acceptor NotConnected Logon (:1738):
                         #   pass force = cfg_.refresh_on_logon
                         #              && cfg_.reset_seqnum_policy_field != bilateral_strict
                         #   (cold-path force=false ⇒ byte-identical 029)

tests/session/
└── test_refresh_on_logon.cpp   # new witnesses (store-wins up/down, default no-op, non-persistent
                               #   no-op, bilateral_strict suppression, RC-1 under refresh, fatal)

tests/interop/happy/
└── hp_fix44_restart_resume_test.cpp   # +standby re-hydrate live interop cell (in-repo, skip-without-counterparty)
```

**Structure Decision**: single-library layout; all changes in `session/` (config + session). No
new module, no new store/manager method, no layer change (`tools/check_layers.py` unaffected —
session→store dependency already exists from 029).

## Complexity Tracking

*No constitution violations requiring justification.* The store-wins-can-regress property is an
accepted operator responsibility (L-025-1, QuickFIX-faithful), not a violation. The one flagged
choice (fold-in vs defer the 029 cold-open `bilateral_strict` gap) was a scope decision **resolved at
Gate A round 1: DEFER** (recorded as the inherited L-029-3 follow-up; fold-in rejected — see §VI /
D-RoL-6), not a violation.

## Gate A

- Round 1 applied 2026-06-09: Codex P1=2 P2=3 P3=1; Opus post-judging P1=1 P2=3 P3=5; rewrite addresses Root cause #1 (narrow the absolute FR-008/SC-005/FR-002/W5 claims to the 025 re-hydrate delta; DEFER the inherited 029 cold-open bilateral_strict gap as L-029-3) + Root cause #2 (Fix8 mechanism wording; record the QFJ-initiator-double-refresh 2-vs-3-site divergence, no third site) + 3 P3 contract/witness clauses. Reviews: research/reviews/codex_025-refresh-on-logon_gate_a_review.md, research/reviews/opus_025-refresh-on-logon_gate_a_adversarial_review.md.
- Round 2 applied 2026-06-09: Codex P2=1 P3=2; Opus post-judging P1=0 P2=1 P3=2 (root cause CLOSED round 1; residual stale-doc-drift only). Fixed the 3 enumerated residual over-claim sites (contracts C3.3, plan testing-summary, checklists note) — narrowed to "no NEW malformed Logon attributable to the knob". Reviews: research/reviews/codex_025-refresh-on-logon_gate_a_2_review.md, research/reviews/opus_025-refresh-on-logon_gate_a_2_adversarial_review.md.
- *(Note: the three pre-existing parked-025 review files in `research/reviews/` are from the SUPERSEDED outbound-only design and do NOT apply to this re-scoped knob.)*
