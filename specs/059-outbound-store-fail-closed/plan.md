# Implementation Plan: Outbound store-failure disposition — fail-closed on a persistent store

**Branch**: `059-outbound-store-fail-closed` | **Date**: 2026-07-03 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/059-outbound-store-fail-closed/spec.md`

## Summary

Make an outbound message-**retain** failure **fatal-when-persistent**, symmetric with the already-fatal durable **counter**-write path (`persist_inbound_advance_` / `persist_outbound_advance_`). Today `Session::store_then_emit` does `(void)store_r;` — it discards the value **returned** by `store_->store(...)` (the store returns its errors as `std::unexpected`; only *thrown* exceptions reach the existing `try/catch`), then transmits the frame anyway. On a durable store this turns one transient I/O fault into a permanent, silent retention freeze and a restart-time counterparty desync (S-P1-1), rooted in a disposition asymmetry (S-P2-1).

**Technical approach (single locus + reuse existing machinery):**
1. In `store_then_emit`, inspect the returned `store_r`. `store_cancelled` (cancellation-class/shutdown) is excluded first — today's absorb→proceed (FR-005). For a **genuine** retain failure with `store_is_persistent_`: capture the error, best-effort reconcile, then `co_return std::unexpected(store_r.error())` — **before** the transmit step and with **no** internal `record_state_transition_`. This is the **same return shape** as the existing transport-write failure (`co_return dispatch_aborted`, `session.cpp:4820-4832`); the `Disconnected` transition is **caller-owned**, exactly as for a transport failure. On a volatile store, keep today's `(void)store_r;` logged-then-proceed (FR-003; L-008-2 stands).
2. **Reconcile-from-durable (FR-007 / US3, per the 2026-07-03 clarification):** a **targeted outbound-only reseed** — read the durable outbound counter (`store_->next_seqnum(outbound,false)`) and `seqnum_mgr_.set_next_outbound(durable_k)` (the existing 032-restore setter), best-effort, at disconnect time. This does **NOT** clear the shared `hydrated_` latch (which would bypass the `bilateral_strict` suppression and re-seed inbound — advisor-flagged blast radius); it touches only the outbound counter. US3 clean-recovery is scoped to **plain persistent** sessions; `reset_on_logon` overrides to 1 (durable reset gated on `cfg_.reset_on_logon`, `session.cpp:776-782`); `bilateral_strict` (the DEFAULT) runs **no** reset and the reconnect Logon is bounded by pre-existing L-029-3 (059 does not worsen it).
3. **Caller census (FR-006) — parity, not new design.** All 26 `store_then_emit` sites are classified against the pre-existing transport-failure return: 9 propagating-broad-guard (`if(!emit_r)→Disconnected`) fail closed unchanged; **1 propagating-narrow-guard** (`Session::send` `:4046`, gating on `== dispatch_aborted`) is broadened to also fail closed on the store-fatal class (keeping app-veto non-fatal) — the one genuinely-new-in-059 call-site edit; 16 swallow-and-continue sites exhibit their pre-existing transport-failure continuation (documented best-effort, not re-engineered). No new latch/chokepoint/enforcement machinery.

This is a disposition of an already-returned error along an existing internal error channel: **no new public API, no new wire behaviour, no new error code, no layout change** (A-4).

## Technical Context

**Language/Version**: C++20/23 (`asio::awaitable<T>` coroutines), Clang/GCC/MSVC.
**Primary Dependencies**: standalone Asio; GoogleTest/GoogleMock. No new dependency.
**Storage**: the feature is *about* the `MessageStore` contract — `MemoryStore` (volatile) and `FileStore` (durable, `pwrite`/`fdatasync` journal).
**Testing**: GoogleTest + GoogleMock; new fault-injection test seam on `FileStore` (mirrors the existing `FIXPP_TEST_HOOKS` seams `g_force_abort_after_reset_lambda` / `g_post_rename_reopen_fail_hook` in `file_store.cpp`).
**Target Platform**: Linux/Clang (Tier-1 gating incl. ASan/UBSan/TSan + coverage), Linux/GCC, Windows/MSVC (Tier-2 same-name `#ifdef` twins where the seam is platform-specific).
**Project Type**: library (session/store subsystem).
**Performance Goals**: zero hot-path regression. The happy path (successful retain) is **byte-identical** — the new branch is taken only on a store-failure return. No new allocation, no new lock. Store-write path already uses `async_mutex` (Article XI §5) — unchanged.
**Constraints**: no new public/C-ABI/wire/error surface; `sizeof` layout goldens unchanged (header/impl-body change only); retain-before-transmit ordering (I-3) preserved; existing `operation_aborted` cancellation handling (FR-005) preserved verbatim.
**Scale/Scope**: ~1 focused disposition change in `store_then_emit` (~15–25 LoC, reconcile + error-return, no internal transition) + a one-line guard broadening at `Session::send` (`:4046`); 1 new FileStore fault-injection seam; new test binaries for US1/US2/US3; doc amendments (L-008-2 / B&L / feature-catalogue / coverage-index).

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Gate | Relevance | Disposition |
|---|---|---|
| **Art. XV §15** (no silent app/session message drop; `disconnect-and-recover` is the sanctioned mode) | **Directly mandating.** The current swallow *is* a silent-loss-that-desyncs-the-seqnum-contract. Fail-closed → disconnect → `ResendRequest` on reconnect is exactly the constitutional `disconnect-and-recover` mode. | ✅ The fix brings the persistent-store send path **into** compliance. |
| **Art. XVII §1 Gate A** (session FSM / recovery / message-store contract; error semantics) | Triggered — this changes the store-failure disposition (a caller-owned `Disconnected` on the fatal error return) + a targeted outbound reconcile (recovery). | ✅ **Gate A MANDATORY** — runs after this plan, before `/tasks`. |
| **Art. XVI §3 + XI §7** (`/clarify` mandatory for error-semantics / session-FSM; four mandatory controls) | Triggered. | ✅ `/clarify` done (US3 disposition resolved 2026-07-03); `/analyze` scheduled; user `/plan` sign-off = Gate A. |
| **Art. VII** (TDD red-green; GoogleTest; conformance corpus) | New behaviour + witnesses. | ✅ Every US lands test-first; the SC-001 cascade witness is proven RED on `main` before the fix (see quickstart.md). Conformance corpus (TC-001..017) must stay green. |
| **Art. IX** (≥95% line / ≥85% branch on touched modules; ASan/UBSan/TSan Tier-1; clang-tidy/format/cppcheck/iwyu) | Touched modules: `src/session/session.cpp` (+ `session.hpp` if a helper is added), `src/session/file_store.cpp` (test seam). | ✅ Coverage-design gate at `/tasks`; the new fatal branch + both store-durability arms get discriminating (mutation-tested) witnesses; the volatile arm is exercised via a bounded MemoryStore at capacity. Sanitizer matrix at `/speckit-verify`. |
| **Art. VIII** (perf; no regression without a bench) | Happy path byte-identical; failure path is cold. | ✅ No bench needed — no hot-path change (state it in the verify doc). |
| **Art. X / ABI** | No C-ABI touch. | ✅ N/A — no `c_api.h` change, no abidiff delta. |
| **Art. XI §5** (store-write path always mutex) | Unchanged — we do not alter the store’s locking. | ✅ N/A. |

**No constitution violations.** Complexity Tracking table is empty.

## Project Structure

### Documentation (this feature)

```text
specs/059-outbound-store-fail-closed/
├── plan.md              # This file
├── spec.md              # Feature spec (+ Clarifications §)
├── research.md          # Phase 0 — design decisions D1–D6
├── data-model.md        # Phase 1 — disposition state + invariants INV-059-*
├── quickstart.md        # Phase 1 — RED-on-main → GREEN witness recipe
├── contracts/
│   └── store-then-emit-disposition.md   # internal error-channel contract (no public surface)
├── checklists/
│   └── requirements.md  # spec quality checklist (from /specify)
└── tasks.md             # /speckit-tasks output (NOT created here)
```

### Source Code (repository root = library submodule)

```text
include/fixpp/session/
└── session.hpp          # (maybe) declare a tiny helper; hydrated_ latch already present

src/session/
├── session.cpp          # store_then_emit: inspect store_r; fatal-when-persistent + set_next_outbound reconcile
└── file_store.cpp       # NEW FIXPP_TEST_HOOKS store()-pwrite fault-injection seam

include/fixpp/session/file_store.hpp
└── (FIXPP_TEST_HOOKS-gated declaration of the new seam arm/probe)

tests/session/           # (or existing test dir convention)
├── test_store_fail_closed_persistent.cpp   # US1: fail-closed + no-transmit + no-freeze (RED-on-main cascade)
├── test_store_fail_open_volatile.cpp       # US2: volatile unchanged (bounded MemoryStore at capacity)
└── test_store_fail_reconnect_reconcile.cpp # US3: reconcile-from-durable on reconnect (+ restart leg)
```

**Structure Decision**: Single-project library layout. The change is localised to `src/session/` with a test-only seam in `file_store.cpp`. Test file names/dirs are finalised at `/tasks` against the existing `tests/` convention (the repo uses same-name `#ifdef` cross-platform twins per `feedback_crossplatform_test_same_name_ifdef` where a seam is OS-specific).

## Complexity Tracking

> No Constitution Check violations — table intentionally empty.

## Gate A

- Round 1 applied 2026-07-03: Codex P1=2 P2=2 P3=0; Opus post-judging P1=2 P2=1 P3=3; rewrite addresses root causes RC#1 (caller census → transport-failure parity), RC#2 (bilateral_strict scoping → L-029-3-bounded), RC#3 (stale hydrated_ language). Reviews: research/reviews/codex_059-outbound-store-fail-closed_gate_a_review.md, research/reviews/opus_059-outbound-store-fail-closed_gate_a_adversarial_review.md.

### Round 1 — resolution notes

- **RC#1 (D3 / caller census).** Reframed on the load-bearing source fact that `store_then_emit` **already** fails closed on a transport-write failure via `co_return dispatch_aborted` with **no internal `record_state_transition_`** (`session.cpp:4820-4832`). The persistent-store-failure path mirrors that exact return shape (reconcile + `co_return unexpected(store_r.error())`, no internal transition). This dissolves NEW-P1 (no mid-handler synchronous `onLogout` since the callee no longer transitions) and makes the census a **parity check**. Real census of all **26** call sites (`9305e69`): 9 propagating-broad-guard (fail closed unchanged), **1** propagating-narrow-guard residual (`Session::send:4046`, resolved by broadening its `== dispatch_aborted` guard to the store-fatal class, keeping app-veto non-fatal — the single genuinely-new-in-059 call-site edit), 16 swallow-and-continue (pre-existing transport-failure dispositions, documented not re-engineered). **No new latch/chokepoint/enforcement machinery introduced.**
- **RC#2 (D4 / `bilateral_strict`).** Dropped the false "reset_to_one() overrides to 1 (harmless)": the durable reset runs **only** under `cfg_.reset_on_logon` (`:776-782`), NOT under `bilateral_strict` (which only forces `141=Y`, `:816-818`), and `bilateral_strict` is the production DEFAULT (`session_config.hpp:231`; `reset_on_logon=false` `:251`). US3 clean-recovery rescoped to plain persistent; `bilateral_strict` bounded by pre-existing, deferred L-029-3 (`behaviors-and-limitations.md:1252-1265`) — 059 neither introduces nor fixes it but does create a reconnect path that can reach it; W3 rebuilt as three honest variants (A plain=clean, B reset_on_logon=override-to-1, C bilateral_strict=L-029-3 regression-guard), acceptor-path reset entry noted.
- **RC#3 (stale `hydrated_` language).** Removed "marked for re-hydration" / "clearing the hydration latch" / "+ hydrated_ clear" / "override-to-1" from spec.md (Clarifications, A-3), plan.md (Scale/Scope), data-model, quickstart. The correct "hydrated_ is NOT touched" statements (D4) are retained.
- **NEW-P3s.** Reconcile error-code pre-emption fixed (capture `store_r.error()` before the reconcile read, D2/row-3a). Two hard-to-witness arms flagged for the `/tasks` coverage-design gate (quickstart). Stale HEAD ref corrected `f525446`→`9305e69` (research.md:3).

### Round 1 — disagreements

- None. All Codex and Opus findings were confirmed against source (HEAD `9305e69`) and applied.
