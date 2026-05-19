<!-- SPECKIT START -->
**Active feature:** `006-async-mutex` — the awaitable mutex `fixpp::sync::async_mutex`: Lewis-Baker / cppcoro lock-free algorithm + mutex-owned residual FIFO (RC-A) + three-state `waiter_phase` machine + lazy `drain_latch_state` (RC-β) + `cancel_and_drain()` drain primitive + PMR-aware fallback via explicit `async_lock(mr)` + session-side helper declaration (RC#2 layering) + 4 `sync_*` error variants at slots 43–46 (fifth Phase 4 feature; 29 named test seams; TSan mandatory).

For technologies, project structure, build/test commands, and gate status, read the current plan: [`specs/006-async-mutex/plan.md`](specs/006-async-mutex/plan.md). Design anchor: `.specify/2f-async-mutex.md` **v1.5 + v1.6 errata E-1..E-4** (v1.5 Gate-A-converged; E-1/E-2/E-3/E-4 recorded post-sign-off at `/implement`, re-touching 006 Gate A scope) — on conflict the design doc (as amended by the v1.6 errata) wins.

Prerequisite design docs (consumed but not modified by this feature): `2d` (Threading/Clock; executor-compat surface; `[2d §4.8]` `session_executor`; `[2d §7.4]` completion contract), `2e` (MessageStore; `async_mutex` is the writer-mutex per `[2e §6.4]`; **2f sign-off is the named hard hand-off gate for 2e implementation per `[2e §3.1]`**). Cross-doc Appendix D drop-ins (§D.1/§D.2/§D.3) to `2d` are queued at 2f sign-off — NOT applied by this feature (see `research.md` D-12).

Companion artifacts in the same directory:
- [`research.md`](specs/006-async-mutex/research.md) — Phase 0 D-1..D-12 (D-11 = fuzz/abidiff/§VII.5 N/A rationale; D-12 = Appendix D cross-doc edit tracking)
- [`data-model.md`](specs/006-async-mutex/data-model.md) — E1..E7 entities (async_mutex / awaiter / guard / drain_latch_state / slot_allocator / completion_policy / session-side helper), memory-ordering invariant table I-01..I-31, error slots 43–46
- [`contracts/`](specs/006-async-mutex/contracts/) — 7 shape-oracle headers: `async_mutex` / `async_lock_guard` / `async_mutex_awaiter` / `drain_latch_state` / `completion_policy` / `async_lock_via_session_executor` / `sync_errors`.hpp
- [`quickstart.md`](specs/006-async-mutex/quickstart.md) — build / test / TSan (mandatory) / ASan+UBSan / ARM64 / bench / HALO spike / CI grep gate / coverage / `/speckit-verify` / `/gate-a` / `/gate-b`

**Deferred feature (005-session-establishment-fsm):** Gate A converged (round 1 complete); implementation blocked on 2f sign-off (async_mutex) and 2d/2e prerequisites. Do NOT start 005 implementation until 006 Gate B merges.

Previous feature (merged): [`004-wire-codec`](specs/004-wire-codec/plan.md) — `fixpp::wire` module: `Framer` / `Parser<Mode>` / `OffsetTable` / `Writer` / `Validator` + `View` flyweight base; PR #68 on `main` (Gate B converged 2026-05-17). Earlier merged: [`003-dictionary-codegen`](specs/003-dictionary-codegen/plan.md) (PR #67), [`002-dictionary-xml-loader`](specs/002-dictionary-xml-loader/plan.md) (PR #66), [`001-core-decimal`](specs/001-core-decimal/plan.md).
<!-- SPECKIT END -->
