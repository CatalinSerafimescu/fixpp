<!-- SPECKIT START -->
**Active feature:** `007-threading-clock` — the application threading contract & `fixpp::core::Clock` plugin seam: per-session-strand default + `direct_executor` opt-out (attested); 4-pure-virtual `Clock` plugin + `system_clock_source` + pimpl'd `mock_clock`; `EngineConfig`/`SessionConfig` frozen engine-anchor+override split; `session_executor` wrapper-class accessor (round 3 RC#1); `session_local<T>` + `current_trace_context`; `cancellable_dispatch`; two-phase `Session::close`; minimal real `Session` skeleton (no FIX FSM — 005 extends); 9 threading error variants at slots 47–55 (second of 3 prerequisites `2f→2d→2e`; 21 named test seams; TSan mandatory). Owns NFR-015.

For technologies, project structure, build/test commands, and gate status, read the current plan: [`specs/007-threading-clock/plan.md`](specs/007-threading-clock/plan.md). Design anchor: `.specify/2d-threading.md` **v0.4** (Gate-A-converged round 3, post-cap line-edit pass; incl. Appendix C cross-doc entries from `2f` v1.5 / `2e` v0.4 sign-offs) — on conflict the design doc wins.

Prerequisite/consumed (merged, not modified): `006`/`2f` `async_mutex` (this feature ships the real `session_executor`/`cancellable_dispatch`/`Session::session_arena()` backing the declaration-only `async_lock_via_session_executor.hpp`); `003`/`2c` `dict::version_registry`; `001`/`002`/`004` `core`/`wire`. Downstream unblocked by this PR: `2e` (MessageStore), then the rest of the threading downstream (`2g`–`2m`). Cross-doc Appendix D / NFR-015 drop-ins applied by the **orchestrator at sign-off**, NOT by this feature (`research.md` D-12).

Companion artifacts in the same directory:
- [`research.md`](specs/007-threading-clock/research.md) — Phase 0 D-1..D-14 (D-4/D-5 = clarify scoping; D-7 = error slots 47–55; D-11 = VII.5/VII.6/VII.7/IX.5 N/A; D-12 = cross-doc edit tracking)
- [`data-model.md`](specs/007-threading-clock/data-model.md) — E1..E12 entities, invariants I-01..I-19, error slots 47–55
- [`contracts/`](specs/007-threading-clock/contracts/) — 11 shape-oracle headers: `clock` / `system_clock_source` / `mock_clock` / `engine_config` / `session_config` / `session_executor` / `session_local` / `trace_context` / `cancellable_dispatch` / `session` / `threading_errors`.hpp
- [`quickstart.md`](specs/007-threading-clock/quickstart.md) — build / test / TSan (mandatory) / ASan+UBSan / alloc-guard / fuzz / bench / coverage / `/speckit-verify` / `/gate-a` / `/gate-b`

**Deferred feature (005-session-establishment-fsm):** Gate A converged (round 1 complete); implementation blocked on the `2d`/`2e` prerequisites (`2f`/`006` is merged). Do NOT start 005 implementation until 007 Gate B merges and `2e` lands. `005` owns the FIX FSM that extends this feature's minimal `Session` skeleton + the FIX-TC corpus `tests/conformance/`.

Previous feature (merged): [`006-async-mutex`](specs/006-async-mutex/plan.md) — `fixpp::sync::async_mutex`; PR #73 on `main` (Gate B converged 2026-05-17). Earlier merged: [`004-wire-codec`](specs/004-wire-codec/plan.md) (PR #68, 2026-05-17), [`003-dictionary-codegen`](specs/003-dictionary-codegen/plan.md) (PR #67), [`002-dictionary-xml-loader`](specs/002-dictionary-xml-loader/plan.md) (PR #66), [`001-core-decimal`](specs/001-core-decimal/plan.md).
<!-- SPECKIT END -->
