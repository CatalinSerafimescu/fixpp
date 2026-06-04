# Implementation Plan: Application Callback Layer (Phase-5, slice 1)

**Branch**: `019-app-callbacks` | **Date**: 2026-06-03 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/019-app-callbacks/spec.md`

## Summary

Add the public `fixpp::session::Application` callback interface — `onCreate`/`onLogon`/`onLogout`, `fromAdmin`/`fromApp`, `toAdmin`/`toApp` — plus a public any-thread origination entry point (`Engine::send(SessionId, payload) → asio::awaitable<expected_t<void>>`), wired into the existing `Engine`/`Session` production path. This is the **first Phase-5 slice** and the direct precondition for G2 business-message interop (`NewOrderSingle → ExecutionReport`, the [const §VII.6] v1.0-GA residual). Reject/veto are signalled by **return value** (`expected_t`), never exceptions (fixpp house style); callbacks run on the engine's `exec_` under single-thread executor confinement (015 E-5, INV-2) — serialization derives from that confinement, NOT a per-session strand (L-019-3); a throwing callback terminal-closes its session. Config-file parsing, store/log factories, and the C ABI are explicitly out of scope (later Phase-5 slices).

Technical approach (from [research.md](./research.md)): an abstract `Application` base with **all-default virtuals (0 pure-virtual)** registered via `EngineConfig::application` (`shared_ptr`, `nullptr` ⇒ no-op); inbound `fromAdmin`/`fromApp` invoked inline on `exec_` **after** the session-FSM accepts the frame; `toApp`/`toAdmin` invoked on the emit path; `Engine::send` returns `asio::awaitable<expected_t<void>>` — it posts onto the target session's `exec_` (holding a strong session keepalive over the post, 014 class) then runs `toApp` + the proven durable-before-transmit `Session::send` path, the await carrying the outcome and giving natural backpressure. Inbound app delivery requires a NEW shared admin/app classifier + an app-accept branch that suppresses the default `Reject(35=3)` when an `Application` is registered (research D8); reject ⇒ `Reject(35=3)` / a NEW stack-buffer `BusinessMessageReject(35=j)` builder; veto ⇒ message dropped + `error::app_do_not_send`; throw ⇒ terminal close.

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
| **VIII.5** Allocator | parse→`fromApp` zero-alloc (`const MessageView&`, no copy; direct invoke, no post-queue node) — research D1/D3; the **reject-emit** path is also zero-alloc — the new `BusinessMessageReject(35=j)` builder uses the same stack `std::array<std::byte,512>` discipline as `build_reject` | ✅ by design |
| **IX.1** Coverage | ≥95/85 on touched `session/` files; reject/veto/throw are *genuine* error paths ⇒ must be tested | ✅ planned |
| **IX.2** Sanitizers | ASan/UBSan/TSan on the new strand-dispatch + any-thread send + drain (SC-005) | ✅ planned |
| **X** ABI | C ABI explicitly **out of scope** ⇒ abidiff gate does not bind this slice | ✅ N/A |
| **XI.4** Threading | callbacks per-session-strand serialised, never on I/O thread — **this feature realises §XI.4** | ✅ PASS |
| **XI.7 / Appendix A** | threading + error-semantics feature ⇒ 4 controls: `/clarify` ✅, `/analyze` (step 6), **Codex Gate A** (mandatory), `/plan` sign-off | ⚠ Gate A + /analyze + sign-off PENDING |
| **XIV.2** Pluggable ≤5 pure-virtual | `Application` = 7 methods but **0 pure-virtual** (all default) ⇒ satisfies the cap by the letter; §2 justification in Complexity Tracking. The 7-method set is reconciled against `.specify/architecture.md` §4.4 (amended this round to add `onCreate`, completing the canonical reference-engine set) | ✅ PASS (with justification) |
| **XV.9** Banned (std::mutex in awaitable hdr) | `application.hpp` carries no mutex; reject/veto via return value, not exceptions ([const §XV] spirit) | ✅ PASS |
| **XV.15** No app-message drop | inbound delivered inline on the read-pump (natural backpressure), no post-queue to drop from — research D3; the any-thread **`Engine::send`** path returns `asio::awaitable<expected_t<void>>` — the caller's await *is* the outbound backpressure (no unbounded silent-drop queue), and a full/cancel/disconnect-mid-post surfaces as the awaited `error`, never a silent drop — research D6 | ✅ PASS |
| **XVI.3** /clarify before /plan | `/speckit-clarify` Session 2026-06-03 (5 axes) ✅ | ✅ PASS |

**Result**: PASS to proceed; the only outstanding *gates* are the mandatory downstream controls (Codex Gate A, `/analyze`, user `/plan` sign-off — [const §XI.7]/Appendix A), which run after this plan per the pipeline. No unjustified violations.

## Project Structure

### Documentation (this feature)

```text
specs/019-app-callbacks/
├── plan.md              # this file
├── research.md          # Phase 0 (D1–D8)
├── data-model.md        # Phase 1 (entities, firing order, INV-1..7)
├── quickstart.md        # Phase 1 (user-facing example)
├── contracts/
│   └── application-interface.md   # Phase 1 (the Application contract)
└── tasks.md             # Phase 2 (/speckit-tasks — NOT created here)
```

### Source Code (repository root)

```text
include/fixpp/session/
├── application.hpp      # NEW — the public Application interface (all-default virtuals)
├── engine.hpp           # EDIT — Engine::send(SessionId, payload) → asio::awaitable<expected_t<void>> decl ONLY
└── session.hpp          # EDIT — on-strand callback invocation points (reusable callback_dispatch_scope, T007)

include/fixpp/core/engine_config.hpp  # EDIT — EngineConfig::application field (shared_ptr<Application>{nullptr}); the field lives here, NOT in engine.hpp

src/session/
├── engine.cpp           # EDIT — onCreate after Session::open() succeeds, before first Logon processing/emission; Engine::send post→toApp→Session::send
└── session.cpp          # EDIT — fromAdmin/fromApp after FSM accept; toAdmin on admin emit;
                         #         onLogon/onLogout at FSM edges; throw→terminal-close wrapper

include/fixpp/core/error.hpp   # EDIT — app_do_not_send = 129, app_callback_threw = 130 (next free after 017's 122-128)
include/fixpp/session/admin_messages.hpp  # EDIT — NEW BusinessMessageReject(35=j) builder (absent today; stack-buffer, research D4)

tests/session/
├── test_application_inbound.cpp     # US1: fromApp/fromAdmin fire + reject mapping; classifier app-accept branch;
│                                    #      NAMED test: fromApp reject emits 35=j (NOT 35=3); FR-014 admin byte-identity
│                                    #      with an Application registered; INV-1/SC-006 nullptr zero-delta witness
├── test_application_outbound.cpp    # US2: Engine::send (awaited) + toApp veto + toAdmin;
│                                    #      FR-013 send-before-logon + unknown-SessionId error
├── test_application_lifecycle.cpp   # US3/INV-7: onCreate (post-open)/onLogon/onLogout exactly-once;
│                                    #      onLogout fires once per exit path: graceful / terminal / callback-threw
├── test_application_strand.cpp      # FR-010/FR-012: no-concurrency + drain (TSan/ASan);
│                                    #      Engine::send post keepalive — stop() races post, no UAF (TSan/ASan, 014 class);
│                                    #      re-entrant send from inside an on-strand callback (no deadlock)
└── test_application_throw.cpp       # FR-011: throwing callback → terminal close
tests/interop/                       # G2 enablement witness — opaque-payload app round-trip (NOS→ExecRpt is downstream)
```

**Structure Decision**: the `Application` interface lives in the existing **`session/`** module alongside `Engine`/`Session` (it is the engine's user-callback surface; no new module). Header placement and layer direction are cross-checked against `.specify/architecture.md` §4.4 at `/implement` per the `tools/check_layers.py` gate ([[feedback_gate_b_check_layers_post_fixer]]); §4.4 reserves `fixpp::session::Application` in the `session/` module, so the placement is ALLOWED with no map change. G2 interop cells (full `NewOrderSingle→ExecutionReport`) are a separate downstream feature; this slice delivers the surface + a minimal enabling witness.

## Complexity Tracking

> One justified deviation, surfaced for Codex Gate A.

| Item | Why needed | Why the simpler form is insufficient |
|------|-----------|--------------------------------------|
| `Application` totals **7 methods** vs [const §XIV.2] "interface surfaces are small (≤5 pure-virtual)" | It is the **canonical, irreducible FIX-engine callback set** (QuickFIX-C++/J + Fix8): 3 lifecycle edges (`onCreate`/`onLogon`/`onLogout`), 2 inbound hooks split admin-vs-app (distinct reject types — `Reject(35=3)` vs `BusinessMessageReject(35=j)`), 2 outbound hooks split admin-vs-app (distinct vetoability). Removing any drops a standard FIX semantic. | Collapsing inbound or outbound into one method would lose the admin/app reject-type and vetoability distinction; merging lifecycle hooks would lose the create-vs-logon-vs-logout edges users gate on. **Mitigation:** all 7 are **default (non-pure) virtuals**, so the *pure-virtual* count the §2 cap measures is **0** — the cap is satisfied by the letter; this row documents the total-surface size for transparency. |

## Normative References

Per `[const §VI.5]`: `[const §VIII.5]` (zero-alloc accept + reject paths), `[const §XI.4]` (per-session-strand serialization), `[const §XIV.2]` (interface cap — 7 methods / 0 pure-virtual), `[const §XV.9]` (no `std::mutex` in awaitable headers), `[const §XV.15]` (no app/session message drop — direct invoke + awaited `Engine::send` backpressure), `[const §XVII.1]` + Appendix A (Gate-A obligation), `[arch §4.4]` (`Application` placement in `session/`, amended to include `onCreate`), `[L-015-4]` (drain/keepalive), `[FIX-SL §4.5.4]` (`Reject(35=3)`), `[FIX50SP2] Infrastructure / Business Rejects` (catalogue row A-014; `BusinessMessageReject(35=j)`).

## Gate A

- Round 1 applied 2026-06-03: Codex P1=4 P2=8 P3=3; Opus post-judging P1=4 P2=6 P3=3; rewrite addresses root causes RC#1 (spec↔research reconciliation: D3 wiring, inspect-only FR-007/008, onCreate vs arch §4.4 + firing point), RC#2 (Engine::send → awaitable + keepalive + backpressure), RC#3 (admin/app classifier as explicit change + mandatory 35=j builder/test), RC#4 (cite sweep: slots 129/130, Normative References, L-019 wording, §X.6, arch path, parenthesis), RC#6 (onLogout/onCreate once-only). Reviews: research/G19-fix-fpml-iso20022/research/reviews/codex_019-app-callbacks_gate_a_review.md, research/G19-fix-fpml-iso20022/research/reviews/opus_019-app-callbacks_gate_a_adversarial_review.md.
- Round 2 applied 2026-06-04: Codex P1=0 P2=3 P3=2; Opus post-judging P1=0 P2=3 P3=2; rewrite (2/2, cap) addresses the RC#1 reconciliation tail (onCreate firing-point in plan/quickstart; "possibly stamped" residue) and the RC#4 cite tail (exact A-014 [FIX50SP2] / [FIX-SL §4.5.4] anchors for 35=j/35=3; awaited-result table wording; admin-builder enumeration). No P1; no Codex disagreements. Reviews: research/G19-fix-fpml-iso20022/research/reviews/codex_019-app-callbacks_gate_a_2_review.md, research/G19-fix-fpml-iso20022/research/reviews/opus_019-app-callbacks_gate_a_2_adversarial_review.md.
