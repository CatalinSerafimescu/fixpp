# Implementation Plan: Application Callback Layer (Phase-5, slice 1)

**Branch**: `019-app-callbacks` | **Date**: 2026-06-03 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/019-app-callbacks/spec.md`

## Summary

Add the public `fixpp::session::Application` callback interface — `onCreate`/`onLogon`/`onLogout`, `fromAdmin`/`fromApp`, `toAdmin`/`toApp` — plus a public any-thread origination entry point (`Engine::send(SessionId, payload)`), wired into the existing `Engine`/`Session` production path. This is the **first Phase-5 slice** and the direct precondition for G2 business-message interop (`NewOrderSingle → ExecutionReport`, the [const §VII.6] v1.0-GA residual). Reject/veto are signalled by **return value** (`expected_t`), never exceptions (fixpp house style); callbacks run **directly on the per-session strand**; a throwing callback terminal-closes its session. Config-file parsing, store/log factories, and the C ABI are explicitly out of scope (later Phase-5 slices).

Technical approach (from [research.md](./research.md)): an abstract `Application` base with **all-default virtuals (0 pure-virtual)** registered via `EngineConfig::application` (`shared_ptr`, `nullptr` ⇒ no-op); inbound `fromAdmin`/`fromApp` invoked inline on `exec_` **after** the session-FSM accepts the frame; `toApp`/`toAdmin` invoked on the emit path; `Engine::send` posts onto the target session's `exec_` then runs `toApp` + the proven durable-before-transmit `Session::send` path. Reject ⇒ `Reject(35=3)` / `BusinessMessageReject(35=j)`; veto ⇒ message dropped + `error::app_do_not_send`; throw ⇒ terminal close.

## Technical Context

**Language/Version**: C++23 (Clang; `std::expected`, coroutines) — [const §II]
**Primary Dependencies**: standalone ASIO (`asio::awaitable`, strands), `fixpp::sync::async_mutex`, existing `wire::MessageView`, `fixpp::core::{error,expected_t}` — no new third-party deps
**Storage**: N/A (reuses the existing `MessageStore` via `Session::send`; no new persistence)
**Testing**: GoogleTest + GoogleMock; sanitizers ASan/UBSan/TSan; coverage llvm-cov — [const §VII, §IX]
**Target Platform**: Linux/Clang (Tier 1); Windows/MSVC (Tier 2)
**Project Type**: single C++ library (`fixpp`)
**Performance Goals**: parse→`fromApp` zero-alloc on the hot path — [const §VIII.5]; no added strand re-schedule on the inbound path (direct invocation, research D3)
**Constraints**: per-session-strand serialisation, no concurrent callbacks ([const §XI.4]); no app/session message drop ([const §XV.15]); no `std::mutex` in awaitable headers ([const §XV.9]); return-value (non-throwing) callback contract
**Scale/Scope**: 1 new public interface (7 callbacks) + 1 config field + 1 engine entry point + reject/veto/throw wiring; bounded below the Phase-5 service wrapper

## Constitution Check

*GATE: must pass before Phase 0 (passed) and re-checked after Phase 1 (below).*

| Article | Gate | Status |
|---------|------|--------|
| **II** Language | C++23/Clang, no new deps | ✅ PASS |
| **VI** Spec coverage | new app-layer catalogue row (`APP-001`) at Polish | ⚠ TODO (catalogue step) |
| **VII** Testing/TDD | every callback site + reject/veto/throw lands red-first; GoogleTest | ✅ planned |
| **VII.6** Interop | this slice is the **precondition** for the v1.0 `Logon→NOS→ExecRpt→Logout` interop (G2) | ✅ enabling |
| **VIII.5** Allocator | parse→`fromApp` zero-alloc (`const MessageView&`, no copy; direct invoke, no post-queue node) — research D1/D3 | ✅ by design |
| **IX.1** Coverage | ≥95/85 on touched `session/` files; reject/veto/throw are *genuine* error paths ⇒ must be tested | ✅ planned |
| **IX.2** Sanitizers | ASan/UBSan/TSan on the new strand-dispatch + any-thread send + drain (SC-005) | ✅ planned |
| **X** ABI | C ABI explicitly **out of scope** ⇒ abidiff gate does not bind this slice | ✅ N/A |
| **XI.4** Threading | callbacks per-session-strand serialised, never on I/O thread — **this feature realises §XI.4** | ✅ PASS |
| **XI.7 / Appendix A** | threading + error-semantics feature ⇒ 4 controls: `/clarify` ✅, `/analyze` (step 6), **Codex Gate A** (mandatory), `/plan` sign-off | ⚠ Gate A + /analyze + sign-off PENDING |
| **XIV.2** Pluggable ≤5 pure-virtual | `Application` = 7 methods but **0 pure-virtual** (all default) ⇒ satisfies the cap by the letter; §2 justification provided for Gate A (see Complexity Tracking) | ✅ PASS (with justification) |
| **XV.9** Banned (std::mutex in awaitable hdr) | `application.hpp` carries no mutex; reject/veto via return value, not exceptions ([const §XV] spirit) | ✅ PASS |
| **XV.15** No app-message drop | inbound delivered inline on the read-pump (natural backpressure), no post-queue to drop from — research D3 | ✅ PASS |
| **XVI.3** /clarify before /plan | `/speckit-clarify` Session 2026-06-03 (5 axes) ✅ | ✅ PASS |

**Result**: PASS to proceed; the only outstanding *gates* are the mandatory downstream controls (Codex Gate A, `/analyze`, user `/plan` sign-off — [const §XI.7]/Appendix A), which run after this plan per the pipeline. No unjustified violations.

## Project Structure

### Documentation (this feature)

```text
specs/019-app-callbacks/
├── plan.md              # this file
├── research.md          # Phase 0 (D1–D8)
├── data-model.md        # Phase 1 (entities, firing order, INV-1..6)
├── quickstart.md        # Phase 1 (user-facing example)
├── contracts/
│   └── application-interface.md   # Phase 1 (the Application contract)
└── tasks.md             # Phase 2 (/speckit-tasks — NOT created here)
```

### Source Code (repository root)

```text
include/fixpp/session/
├── application.hpp      # NEW — the public Application interface (all-default virtuals)
├── engine.hpp           # EDIT — EngineConfig::application field; Engine::send(SessionId, payload)
└── session.hpp          # EDIT — on-strand callback invocation points (reuse dispatch_guard)

src/session/
├── engine.cpp           # EDIT — onCreate at construction; Engine::send post→toApp→Session::send
└── session.cpp          # EDIT — fromAdmin/fromApp after FSM accept; toAdmin on admin emit;
                         #         onLogon/onLogout at FSM edges; throw→terminal-close wrapper

include/fixpp/core/error.hpp   # EDIT — app_do_not_send, app_callback_threw (next free slots ≥122)
# (+ a BusinessMessageReject(35=j) builder — confirm/add, research D4)

tests/session/
├── test_application_inbound.cpp     # US1: fromApp/fromAdmin fire + reject mapping (TDD)
├── test_application_outbound.cpp    # US2: Engine::send + toApp veto + toAdmin
├── test_application_lifecycle.cpp   # US3: onCreate/onLogon/onLogout ordering
├── test_application_strand.cpp      # FR-010/FR-012: no-concurrency + drain (TSan/ASan)
└── test_application_throw.cpp       # FR-011: throwing callback → terminal close
tests/interop/                       # G2 enablement witness (NewOrderSingle→ExecutionReport)
```

**Structure Decision**: the `Application` interface lives in the existing **`session/`** module alongside `Engine`/`Session` (it is the engine's user-callback surface; no new module). Header placement and layer direction are cross-checked against `decisions/architecture.md` at `/implement` per the `tools/check_layers.py` gate ([[feedback_gate_b_check_layers_post_fixer]]). G2 interop cells (full `NewOrderSingle→ExecutionReport`) are a separate downstream feature; this slice delivers the surface + a minimal enabling witness.

## Complexity Tracking

> One justified deviation, surfaced for Codex Gate A.

| Item | Why needed | Why the simpler form is insufficient |
|------|-----------|--------------------------------------|
| `Application` totals **7 methods** vs [const §XIV.2] "interface surfaces are small (≤5 pure-virtual)" | It is the **canonical, irreducible FIX-engine callback set** (QuickFIX-C++/J + Fix8): 3 lifecycle edges (`onCreate`/`onLogon`/`onLogout`), 2 inbound hooks split admin-vs-app (distinct reject types — `Reject(35=3)` vs `BusinessMessageReject(35=j)`), 2 outbound hooks split admin-vs-app (distinct vetoability). Removing any drops a standard FIX semantic. | Collapsing inbound or outbound into one method would lose the admin/app reject-type and vetoability distinction; merging lifecycle hooks would lose the create-vs-logon-vs-logout edges users gate on. **Mitigation:** all 7 are **default (non-pure) virtuals**, so the *pure-virtual* count the §2 cap measures is **0** — the cap is satisfied by the letter; this row documents the total-surface size for transparency. |
