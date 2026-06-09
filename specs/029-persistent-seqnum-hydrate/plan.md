# Implementation Plan: Persistent seqnum hydrate — resume both counters from the store across restart/reconnect

**Branch**: `029-persistent-seqnum-hydrate` | **Date**: 2026-06-09 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/029-persistent-seqnum-hydrate/spec.md`

## Summary

Make the persistent `MessageStore` the durable source of truth for **both**
sequence-number directions and resume the in-memory counters from it at **cold
open**. Two coupled mechanisms:

1. **Inbound counter persistence** (closes "T034"): after each accepted inbound message
   is **delivered/handled** (`fromApp`/`fromAdmin`), advance the durable inbound counter via a
   named `persist_inbound_advance_()` helper (`store_->next_seqnum(direction_t::inbound,
   /*increment=*/true)`) — counter-only, no inbound frame retention (fixpp never resends inbound),
   matching QFcpp's `incrNextTargetMsgSeqNum`. The helper is invoked at **every `check_inbound`-
   success site** per a disposition matrix (acceptor Logon, initiator Logon-ack, Heartbeat,
   TestRequest, ResendRequest, Logout, Reject, in-seq app, resend-fill app) — **not** a single tail
   write (the in-seq handler has ~10 early `co_return` exits; a tail-only persist would silently
   miss every admin frame, RC-2). No-persist at the no-advance arms (too-low/PossDup, validate-off
   deliver-without-advance, GapFill jump). Deliver-then-persist = at-least-once (**D-2**).
2. **Hydrate-on-open**: a **one-shot** `ensure_hydrated_()` reads both persisted
   counters (`next_seqnum(dir, false)`) and loads them into the `SeqnumManager` via a
   new production `hydrate(in, out)` — before the counters are first used, on cold
   start only, for both roles and both direct/engine-managed sessions (**D-1/D-6**).

**This is a store↔session seqnum-boundary slice (own Gate A) — the highest-risk area
(008 drew 5 P1s; the outbound-only 025 drew 3 P1s).** The *code* is small (a manager
setter, a one-shot helper + two call sites, a `persist_inbound_advance_()` helper invoked at the
matrixed persist sites, and the I-3 comment reconciliation); the *risk* is in the durability
invariants and the Logon-path gate ordering, which this plan pins explicitly. RefreshOnLogon
(025 / S-018, the per-logon **re**-hydrate knob) rides on this spine and is **out of scope**.

**Key design grounding (source sweep + reference engines, in [research.md](./research.md)):**

- **Always-on cold hydrate, one-shot, Logon-gate-aware** (D-1/D-6/D-9, RC-1): QFcpp `FileStore::populateCache()` hydrates from disk at construction and is read through on every access — the store *is* the source of truth, no enable-flag. fixpp mirrors this with a one-shot `ensure_hydrated_()` (guarded by `hydrated_`, latched **only after success** — D-9) at the **first** counter touch. Placed at the top of `emit_initiator_logon_()` (`:542`, before the 024 `reset_on_logon` block at `:558`) for initiators — the shared emit point that covers BOTH `open()` direct AND engine-managed first-connect via `drive_reconnect` ([[feedback_initiator_logon_wire_at_shared_emit_point]]) — and in the `NotConnected` inbound-Logon case for acceptors, **after** the `peer_sent_reset`/`reset_on_logon` header pre-scan (`:1585-1587`). **Crucially the Logon-path gate is NOT the steady-state gate** (`check_inbound` at `:1596`/`:2841` fatals on too-high with no ResendRequest arm unless 789 is on; the received-141 reset is at `:1760` — *after* `check_inbound`). So hydrate the **outbound** counter unconditionally but **withhold the inbound seed on a reset Logon** so a hydrated `next_inbound` cannot pre-empt a peer reset Logon into a too-low fatal at `:1615` (FR-010/INV-H5). **One-shot ⇒ reconnect does NOT re-hydrate** (re-hydrating a live session would regress the manager to the store's lower-bound value — the exact 025 Gate-A New-1 corruption); a *transient read failure* leaves `hydrated_` false so the next reconnect retries (D-9). Memory store is discriminated non-persistent at `open()` via the new `MessageStoreFactory::yields_persistent_store()` accessor (default `true`; `MemoryStoreFactory` → `false`) captured into `store_is_persistent_` (D-10 — a one-bit additive factory surface, NOT a 5th `MessageStore` pure-virtual) so it skips the read (FR-005 byte-identity).
- **Deliver-then-persist, counter-only** (D-2/D-4): persist the inbound advance **after** the in-seq `fromApp`/`fromAdmin` delivery returns, via `next_seqnum(inbound, true)` (counter only — no frame body, fixpp does not resend inbound). The in-memory `check_inbound` advance stays **before** delivery (unchanged — it is the gate); only the *durable* write moves after delivery. Crash mid-delivery ⇒ durable not advanced ⇒ restart re-delivers (PossDup dedup), never skips.
- **Lower-bound invariant** (D-5): the `MessageStore` interface (4 pure-virtuals: `store`/`retrieve`/`next_seqnum`/`reset`) has **no absolute counter set**, so a `+1` persist cannot mirror a `SequenceReset`-GapFill **jump** (`apply_inbound_sequence_reset` updates the manager but — verified — never persists to `store_`). We **do not** persist GapFill jumps. **INV-H1**: `persisted_next_inbound ≤ manager.next_inbound` always — the persisted counter is a monotonic lower bound, never ahead, so no inbound is ever skipped on restart; any residual gap is reconciled by the existing 013 ResendRequest on the post-restart Logon. Documented limitation **L-029-1** (bounded redundant resend after a restart-following-a-GapFill). Alternatives (a 5th pure-virtual `set_seqnum`, or a bounded catch-up loop) are rejected for KISS + cap headroom — **flagged for Gate A**.
- **Fatal-disconnect on inbound persist failure** (D-3): `next_seqnum(inbound,true)` failure → fatal, reusing the existing store-failure disposition (`record_state_transition_(Disconnected)`); reconnect re-hydrates the last durable value + 013 resyncs. Avoids the New-2 swallowed-failure desync class. **Asymmetry note**: the existing **outbound** store write stays I-07 logged-then-proceed (008/024 behavior, out of scope) — the residual New-2 outbound-desync-on-hydrate is documented **L-029-2**, not fixed here.
- **`SeqnumManager::hydrate(in,out)`** (D-7): the new production setter (FR-008) — mirrors `set_next_inbound` (acquire `async_mutex`, set both fields). The first production way to set the outbound counter (only `set_counters_for_test` existed).
- **I-3 reconciliation** (D-8): the unwired aspirational comment at `session.cpp:1517` ("store(inbound) BEFORE fromAdmin/fromApp") and any `[2e §7.6]`-derived prose asserting store-before-deliver are **corrected to deliver-then-persist** so shipped code and comments agree.

## Technical Context

**Language/Version**: C++23 (Clang; asio awaitables, `std::expected`) — [const §II]
**Primary Dependencies**: `Session::on_inbound_frame` in-seq delivery path (`session.cpp` Active/LogonReceived handler — `check_inbound` `:2253`, in-seq dispatch), `emit_initiator_logon_` (`:542`), `open()` (`:651`), the `NotConnected` inbound-Logon case (`:1524`), `SeqnumManager` (+`hydrate`), `MessageStore::next_seqnum` (existing, both directions). No new third-party deps, no codegen, no wire field, no new error slot.
**Storage**: the existing `MessageStore` (008/FileStore) — **read** both counters at cold open (`next_seqnum(dir,false)`); **write** the inbound counter after each in-seq delivery (`next_seqnum(inbound,true)`). No store schema change (FileStore already persists both counters in one record; the inbound counter was simply never advanced by the session). No new pure-virtual (cap stays 4/5).
**Testing**: GoogleTest; ASan/UBSan/TSan; coverage llvm-cov; restart-resume witnesses (both directions, both roles); deliver-then-persist crash-ordering witness; lower-bound (post-GapFill) witness; fatal-on-inbound-persist-failure witness (with a fault-injecting test store); default-no-op (memory/null store) byte-identity + full regression; one-shot-fires-exactly-once (both roles) witness; live interop cell (skip-without-counterparty) — restart a fixpp side mid-session vs a QFcpp/QFJ peer and resume. — [const §VII, §IX]
**Target Platform**: Linux/Clang Tier-1 (sanitizer matrix); the live cell runs against QFcpp/QFJ in the parent harness.
**Project Type**: single C++ library (`fixpp`) + tests + interop-harness extension.
**Performance Goals**: one extra durable counter write per accepted inbound message (counter-only, no frame body) — symmetric with the existing per-send outbound write; default path with no persistent store is unchanged. No new allocation on the hot path.
**Constraints**: `noexcept`/`expected_t` preserved; no new `std::mutex` in awaitable headers ([const §XV.9] — `hydrate` adds an awaitable method to `seqnum_manager.hpp`, which already includes `<asio/awaitable.hpp>` + `async_mutex.hpp`; no new include, so N/A — confirm via the unfiltered/`-L sync` verify step); **non-persistent store ⇒ byte-identical** (`store_is_persistent_==false` ⇒ `ensure_hydrated_` skips the read — memory store discriminated at `open()` from the new `MessageStoreFactory::yields_persistent_store()` accessor, default `true`/`MemoryStoreFactory`→`false`, a one-bit additive factory surface NOT a 5th `MessageStore` pure-virtual, D-10; FR-005); **INV-H1** persisted ≤ manager (lower bound); hydrate is **cold-open one-shot, latched-after-success** (never on reconnect; transient read-failure retried — D-9).
**Scale/Scope**: +1 `SeqnumManager::hydrate` method; +1 `ensure_hydrated_` one-shot helper + `hydrated_`/`hydrating_`/`store_is_persistent_` flags, 2 call sites (initiator emit, acceptor Logon — with the Logon-gate-aware inbound-seed split, RC-1); +1 `persist_inbound_advance_()` helper invoked at the matrixed persist sites on the inbound path (RC-2 — not a single tail call); +1 non-pure `MessageStoreFactory::yields_persistent_store()` accessor (default `true`; `MemoryStoreFactory`→`false`) captured into `store_is_persistent_` at `open()` (RC-A/New-A); the `session.cpp:1517` I-3 comment + `[2e §7.6]` prose reconciliation. New `tests/session/test_persistent_seqnum_hydrate.cpp` + a live interop cell. No FSM state added; no new store pure-virtual; no codegen/C-ABI/wire change.

## Constitution Check

*GATE: must pass before Phase 0 (passed) and re-checked after Phase 1 (re-confirmed).*

| Article | Gate | Status |
|---------|------|--------|
| **II** Language | C++23/Clang, no new deps | ✅ PASS |
| **VI** Spec coverage | **One net-new catalogue row** `S-042` (persistent inbound seqnum continuity / hydrate-on-open) `done` (FIX 4.4) **+ its `spec/coverage-index.md` entry** (Article VI.4); cross-link S-018 (025). The deferred RefreshOnLogon marker is **L-024-1** (`behaviors-and-limitations.md:579`), NOT L-025-1 (no such ID) — 029 is the spine that **unblocks** 025 but does **not** retire L-024-1 (RefreshOnLogon stays unimplemented until 025 ships); update L-024-1's prose to note the spine dependency is discharged. Normative refs live in spec.md `## Normative References` (Article VI.5): `[FIX-SL §4.1]` seqnums, `[FIX-SL §4.3.12]` synchronization-after-logon, `[FIX-SL §4.8.x]` ResendRequest/SequenceReset. New limitations **L-029-1** (post-GapFill restart redundant resend / knob-off Logon fatal) + **L-029-2** (outbound I-07 desync residual). Exact delta below (Polish). | ⚠ RESOLVED (delta specified) |
| **VII** Testing/TDD | RED-first: (1) restart resumes outbound `34=N`; (2) restart resumes inbound `N+1` **over an admin-inclusive stream** (persist matrix, RC-2); (3) deliver-then-persist crash ordering (durable not advanced until after callback); (4) post-GapFill restart stays ≤ true; recovery non-fatal **only with 789-on or peer-reset**, knob-off too-high Logon fatals (its own assertion, RC-1/New-1); (5) inbound persist failure → fatal disconnect, no partial state; (6) memory/null store byte-identical + full regression (non-persistent discriminator); (7) one-shot fires exactly once across both roles + NOT on reconnect + **hydrate happens-before first `check_inbound`** (New-4); (8) acceptor `141=Y` reset Logon over hydrated store is in-seq not too-low-fatal (RC-1 W9b); (9) hydrated initiator advertises `789=<hydrated>` with the knob on (New-3 W11); (10) `validate_sequence_numbers=false` + exact-match `35=4` → durable inbound `+1` (PERSIST), Reset-mode validate-off → no persist (RC-B W12); (11) custom persistent factory hydrates / custom non-persistent factory (`yields_persistent_store()==false`) skips (RC-A W13) | ✅ planned |
| **VII.6** Interop | live both-role cell: restart a fixpp initiator/acceptor mid-session vs a QFcpp/QFJ peer → resumes both counters, no fatal too-low/too-high, peer-ahead recovers via ResendRequest | ✅ planned |
| **VIII.5** Allocator | hydrate reads + the per-message counter write are counter-only (no frame body, no new container); no-heap witness on the in-seq persist path + the cold-open hydrate path (non-allocating ready-awaitable test store) | ✅ planned (witnessed) |
| **IX.1** Coverage | ≥95/85 on `ensure_hydrated_`, `SeqnumManager::hydrate`, the post-delivery inbound persist call + its fatal-failure branch | ✅ planned |
| **IX.2** Sanitizers | ASan/UBSan/TSan on the session inbound + open changes + interop ctest | ✅ planned |
| **X** ABI | no C-ABI/error-slot/wire change. +1 `SeqnumManager` method, +1 private flag/helper on `Session`, +1 non-pure `MessageStoreFactory::yields_persistent_store()` accessor (default `true`; `MemoryStoreFactory` override → `false`) captured into `store_is_persistent_` at `open()` (RC-A) — source rebuild only. **No new `MessageStore` pure-virtual (cap stays 4/5); the persistence accessor is on the FACTORY interface, not `MessageStore`** | ✅ source rebuild (additive) |
| **XI.4** Threading | `ensure_hydrated_` + the persist run on the existing session strand (open path / inbound handler); one-shot flag is strand-confined; no new concurrency surface | ✅ PASS |
| **XII.5** No-implicit-default | hydrate-on-open has no config flag (always-on when `store_` present, D-1); the no-store path is the explicit byte-identical floor | ✅ PASS (no new config) |
| **XIV.2** Pluggable ≤5 pure-virtual | `MessageStore` stays at **4** pure-virtuals — `hydrate`/persist reuse the existing `next_seqnum(dir, increment)`; **no 5th added** (the rejected `set_seqnum` alternative is the reason INV-H1 lower-bound is accepted). The persistence discriminator is a non-pure `MessageStoreFactory::yields_persistent_store()` accessor (default `true`; `MemoryStoreFactory`→`false`) on the **factory** interface — Article XIV.2's ≤5 cap governs `MessageStore`, which the factory accessor does not touch (cap stays 4) | ✅ PASS (cap preserved) |
| **XV.9** Banned (`std::mutex` in awaitable hdr) | `hydrate` adds an awaitable method to `seqnum_manager.hpp`; that header already includes `<asio/awaitable.hpp>` + `async_mutex.hpp` — no new include into the `session.hpp` closure | ✅ N/A (confirm at verify) |
| **XVI.3/4** /clarify before /plan | Session 2026-06-09 (3 asked: hydrate trigger, crash persist-vs-deliver ordering, inbound persist failure) + reference sweep (QFcpp `FileStore::populateCache`/`incrNextTargetMsgSeqNum`/`setNextTargetMsgSeqNum`, QFJ) | ✅ PASS |
| **XVII.1** Gate A before /tasks | mandatory — runs after this plan, before `/speckit-tasks` | ⏳ PENDING (Gate A next) |

**Result**: PASS to proceed. The hydrate is always-on-when-persistent but one-shot/cold,
so the no-store default is byte-identical; the inbound persist is additive and counter-only;
no new store pure-virtual (cap preserved); the durability invariants (INV-H1 lower-bound,
deliver-then-persist, fatal-on-inbound-failure) are pinned. Two design choices are
**explicitly flagged for Gate A**: (i) the INV-H1 lower-bound / no-GapFill-jump-persist
vs a 5th pure-virtual `set_seqnum`; (ii) the inbound-fatal / outbound-logged asymmetry
(L-029-2). No unjustified violations.

**Exact §VI delta (applied at Polish):**
- `spec/feature-catalogue.md`: add **`S-042`** (`Persistent inbound seqnum continuity — durable inbound counter + bidirectional hydrate-on-open; resume both directions across restart`), `done` (FIX 4.4), cite `029-persistent-seqnum-hydrate`, evidence_pr `(pending merge)`, Tests `tests/session/test_persistent_seqnum_hydrate.cpp` + the interop cell; cross-link `S-018`.
- `spec/coverage-index.md`: add the matching **S-042** coverage row (Article VI.4) — source units (`ensure_hydrated_`, `persist_inbound_advance_`, `SeqnumManager::hydrate`) ↔ `tests/session/test_persistent_seqnum_hydrate.cpp` + interop cell.
- `spec/behaviors-and-limitations.md`: add **L-029-1** (post-GapFill restart → bounded redundant ResendRequest when 789/reset available, else the peer Logon fatals on the Logon gate and recovers by reconnect; recovery-correct, at-least-once) and **L-029-2** (a swallowed I-07 **outbound** store-write failure in a prior run leaves the persisted outbound counter behind true; hydrate is only as fresh as the last successful outbound write — pre-existing 008/024 property, outbound→fatal deferred).
- Update **L-024-1** (`behaviors-and-limitations.md:579`) prose to note the store→manager hydrate-on-open dependency ("008-boundary") is **discharged by 029**; the limitation itself stays OPEN (RefreshOnLogon S-018 is still not implemented until 025). Do **not** edit a non-existent `L-025-1`.

## Project Structure

### Documentation (this feature)

```text
specs/029-persistent-seqnum-hydrate/
├── plan.md              # This file
├── research.md          # Phase 0 — D-1..D-8 decisions + reference sweep + INV-H1 derivation
├── data-model.md        # Phase 1 — entities, FSM placement, invariants, witness matrix
├── quickstart.md        # Phase 1 — the RED witnesses
├── contracts/
│   └── seqnum-hydrate.md # Phase 1 — hydrate() + inbound-persist + INV-H1 contract
└── checklists/
    └── requirements.md   # spec quality checklist (done)
```

### Source Code (repository root)

```text
include/fixpp/session/
├── seqnum_manager.hpp        # +hydrate(next_inbound, next_outbound) awaitable setter
├── message_store_factory.hpp # +yields_persistent_store() non-pure accessor (default true)
├── memory_store_factory.hpp  # override yields_persistent_store() → false
└── session.hpp               # +ensure_hydrated_()/persist_inbound_advance_() decls,
                              #   +hydrated_/hydrating_/store_is_persistent_ flags

src/session/
├── seqnum_manager.cpp        # hydrate() impl (mirrors set_next_inbound, both counters)
└── session.cpp               # ensure_hydrated_() impl (outbound hydrate + Logon-gate-aware
                              #   inbound seed; latch-after-success); call at
                              #   emit_initiator_logon_ top + NotConnected Logon (after the
                              #   reset pre-scan); persist_inbound_advance_() at the matrixed
                              #   persist sites on the in-seq path (incl. validate-off exact-match
                              #   35=4 PERSIST, RC-B); capture store_is_persistent_ at open() from
                              #   yields_persistent_store(); reconcile the :1517 I-3 comment

tests/session/
└── test_persistent_seqnum_hydrate.cpp   # new witnesses (both roles, both directions)

tests/interop/happy/
└── hp_fix44_restart_resume_test.cpp              # +restart-resume live interop cell (in-repo)

# parent harness
research/G19-fix-fpml-iso20022/phase-9-harness/   # +counterparty (QFcpp/QFJ) restart-resume config delta (cross-repo, not a fixpp source change)
```

**Structure Decision**: single-library layout; all changes in `session/` (manager +
session) plus the existing store interface used as-is. No new module, no new store
method, no layer change (`tools/check_layers.py` unaffected — session→store dependency
already exists).

## Complexity Tracking

*No constitution violations requiring justification.* The two flagged design choices
(INV-H1 lower-bound vs 5th pure-virtual; inbound-fatal/outbound-logged asymmetry) are
**simplicity-preserving** decisions (avoid a 5th pure-virtual; avoid re-opening the
008/024 outbound I-07 policy), documented as limitations L-029-1/L-029-2 and routed to
Gate A — not violations.

## Gate A

- Round 1 applied 2026-06-09: Codex P1=3 P2=5 P3=1; Opus post-judging P1=4 P2=7 P3=2; rewrite addresses root causes RC-1 (Logon-gate model / acceptor-141 reorder + narrowed recovery contract), RC-2 (site-keyed persist matrix), RC-3 (hydrated_ latch + memory-store discriminator), RC-4 (doc-accuracy sweep). Reviews: research/reviews/codex_029-persistent-seqnum-hydrate_gate_a_review.md, research/reviews/opus_029-persistent-seqnum-hydrate_gate_a_adversarial_review.md.
- RC-3 memory-store option chosen: **(b) discriminator** — a single `Session::store_is_persistent_` bool captured at `open()` (D-10), NOT a narrowed `store_==nullptr` promise and NOT a 5th `MessageStore` pure-virtual. Preserves the spec's "memory store ⇒ byte-identical / zero added reads" promise without ballooning the store interface.
- No new `/clarify`: the 3 clarifications (D-1/D-2/D-3) stand; round 1 is correct *application* of them against the real `session.cpp` Logon-path control flow. New resolved decisions recorded: research.md D-9 (latch-after-success), D-10 (non-persistent discriminator), and the RC-1 D-6 control-flow refinement.
- Round 2 applied 2026-06-09: Codex P1=1 P2=1 P3=0; Opus post-judging P1=1 P2=1 P3=2; rewrite addresses RC-A (real one-bit factory persistence accessor `MessageStoreFactory::yields_persistent_store()` default `true`/`MemoryStoreFactory`→`false` backing `store_is_persistent_`; was unbacked "factory metadata") + RC-B (three-way 35=4 persist-matrix split: validate-off exact-match GapFill PERSISTs) + New-A (capture-point pinned to the `if (cfg_.store_factory)` branch at `session.cpp:779`, once, before the first counter touch, both paths) + New-B (W12 validate-off `35=4` persist witness + W13 custom-store discriminator witness). Reviews: research/reviews/codex_029-persistent-seqnum-hydrate_gate_a_2_review.md, research/reviews/opus_029-persistent-seqnum-hydrate_gate_a_2_adversarial_review.md.
