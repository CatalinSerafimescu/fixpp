# Implementation Plan: Outbound store-failure disposition — fail-closed on a persistent store

**Branch**: `059-outbound-store-fail-closed` | **Date**: 2026-07-03 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/059-outbound-store-fail-closed/spec.md`

## Summary

Make an outbound message-**retain** failure **fatal-when-persistent**, symmetric with the already-fatal durable **counter**-write path (`persist_inbound_advance_` / `persist_outbound_advance_`). Today `Session::store_then_emit` does `(void)store_r;` — it discards the value **returned** by `store_->store(...)` (the store returns its errors as `std::unexpected`; only *thrown* exceptions reach the existing `try/catch`), then transmits the frame anyway. On a durable store this turns one transient I/O fault into a permanent, silent retention freeze and a restart-time counterparty desync (S-P1-1), rooted in a disposition asymmetry (S-P2-1).

**Technical approach (single locus + reuse existing machinery):**
1. In `store_then_emit`, inspect the returned `store_r`. `store_cancelled` (cancellation-class/shutdown) is excluded first — today's absorb→proceed (FR-005). For a **genuine** retain failure with `store_is_persistent_`: reconcile then transition the session to `Disconnected` and `co_return std::unexpected(store_r.error())` — **before** the transmit step. On a volatile store, keep today's `(void)store_r;` logged-then-proceed (FR-003; L-008-2 stands).
2. **Reconcile-from-durable (FR-007 / US3, per the 2026-07-03 clarification):** a **targeted outbound-only reseed** — read the durable outbound counter (`store_->next_seqnum(outbound,false)`) and `seqnum_mgr_.set_next_outbound(durable_k)` (the existing 032-restore setter), best-effort, at disconnect time. This does **NOT** clear the shared `hydrated_` latch (which would bypass the `bilateral_strict` suppression and re-seed inbound — advisor-flagged blast radius); it touches only the outbound counter and is overridden by `reset_to_one()` under `bilateral_strict`/`reset_on_logon`.
3. The fatal disposition lives **inside** `store_then_emit`, so it fires regardless of whether a caller inspects `emit_r` — including the two best-effort `(void)co_await store_then_emit(...)` sites. The caller census (FR-006) confirms each site *tolerates* a mid-flow `Disconnected`, not that each site must re-check the return.

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
**Scale/Scope**: ~1 focused disposition change in `store_then_emit` (~15–25 LoC) + `hydrated_` clear; 1 new FileStore fault-injection seam; new test binaries for US1/US2/US3; doc amendments (L-008-2 / B&L / feature-catalogue / coverage-index).

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Gate | Relevance | Disposition |
|---|---|---|
| **Art. XV §15** (no silent app/session message drop; `disconnect-and-recover` is the sanctioned mode) | **Directly mandating.** The current swallow *is* a silent-loss-that-desyncs-the-seqnum-contract. Fail-closed → disconnect → `ResendRequest` on reconnect is exactly the constitutional `disconnect-and-recover` mode. | ✅ The fix brings the persistent-store send path **into** compliance. |
| **Art. XVII §1 Gate A** (session FSM / recovery / message-store contract; error semantics) | Triggered — this changes the store-failure disposition + adds a `Disconnected` transition + touches recovery (rehydrate). | ✅ **Gate A MANDATORY** — runs after this plan, before `/tasks`. |
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
