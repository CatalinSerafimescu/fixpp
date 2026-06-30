# Implementation Plan: Python bindings ownership / lifetime layer (PY-004)

**Branch**: `055-python-lifetime-ownership` | **Date**: 2026-06-27 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/055-python-lifetime-ownership/spec.md`

## Summary

Build the `[2m §6.2]` pure-Python object-oriented **lifetime/ownership layer** on top of the flat SWIG substrate shipped by 053/054, so Python objects can never outlive the native handles they wrap. Pythonic wrapper classes (`Engine`, `Session`, `Message`, `Application`, `Dictionary`) each carry a `_dead` liveness sentinel checked before every C-ABI call; a parent's ordered `close()` walks its weak-referenced children and arms their sentinels; post-close / post-dispatch-window access raises `fixpp.ObjectLifetime` (1202) instead of a use-after-free. The existing hand-written receive trampoline (`%wrapper`) is extended to construct the inbound `Message` wrapper, arm its `_dead` at callback return (closes **L-053-1**), and set/clear a GIL-protected per-`Session` `_in_callback` marker so a blocking call from inside a callback raises `fixpp.CallbackReentrantClose` (1204) rather than deadlocking. `Engine` construction rejects sub-interpreters (`SubInterpreterRejected`, 1201). Handle-bearing wrappers refuse pickle. The registered callback becomes owned-by-`Session` and is released on close / re-registration (fixes the 053/054 hold-until-interpreter-exit leak).

**Approach is freeze-clean: no `include/fix/c_api.h` change.** All three binding error codes (1200–1204) already exist in `error.h` (lines 152–163); the full `Message` accessor surface and `fixpp_msg_clone` already exist (051); the liveness guard is a Python-side check *before* the C-ABI, backed by the 050/052 native tombstones. The `[2m §6.2]` model is delivered through the **existing trampoline**, not a new SWIG director — so the `0→1` C-ABI freeze holds outright.

## Technical Context

**Language/Version**: Python 3 (CPython, C-API; abi3 targeting deferred to PY-005) + C (SWIG `%wrapper` trampoline glue) + pure-Python OO layer. SWIG generates the flat substrate.
**Primary Dependencies**: SWIG (binding generator, already wired); CPython C-API (`PyGILState_*`, `PyObject_*`, `Py_INCREF/DECREF`, `weakref`); the existing `fixpp_capi` static archive (statically linked into `_fixpp.so`). Tests: pytest.
**Storage**: N/A.
**Testing**: pytest under `bindings/python/tests/`; Tier-1 `python-bindings` CI matrix (none / asan / tsan). ASan witnesses the no-UAF property (SC-001); a watchdog/timeout test witnesses the no-deadlock reentrancy property (SC-007).
**Target Platform**: Linux x86_64 for build/test (the binding logic is OS-agnostic; wheels/manylinux are PY-005).
**Project Type**: language-binding layer — a pure-Python OO layer + SWIG/C glue over a frozen C-ABI.
**Performance Goals**: Correctness-first. The liveness guard is a single Python attribute check (~tens of ns) per accessor — negligible. The `[2m §6.6]` latency ceilings are provisional and bench-gated under PY-005/post; not a v1.0 PY-004 acceptance gate.
**Constraints**: **No `include/fix/c_api.h` change** (`0→1` freeze held). GIL-protected wrapper state (no `threading.local`, no OS-thread-id assumptions — `[2m §1.3]` rule (4)). ASan/TSan clean. Build parallelism capped at `-j2`; sanitizer/verify presets run strictly one at a time (WSL2 OOM, per durable session cap).
**Scale/Scope**: ~5 Python wrapper classes (one new pure-Python module, e.g. `bindings/python/fixpp_oo.py` merged into the `fixpp` package surface) + targeted edits to the `%wrapper` trampoline and the register hand-wrapper in `fixpp.i` + ~6–8 new pytest files. No new C-ABI symbols.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-checked after Phase 1 design. (Real constitution = `.specify/constitution.md`; the skill's `.specify/memory/constitution.md` path is the stub — see memory `feedback_speckit_skill_points_at_stub_constitution`.)*

| Article | Gate | Status | Justification |
|---|---|---|---|
| **IV §3** | Python bindings ship via SWIG over the C-ABI | **PASS** | This feature is the pure-Python OO layer over the existing SWIG substrate; no change to the binding-over-C-ABI model. |
| **VII §2** | Python tests use pytest | **PASS** | All new tests are pytest under `bindings/python/tests/`. |
| **VIII** | Performance budgets / bench | **N/A (deferred)** | Liveness check is a Python flag read; `[2m §6.6]` ceilings are provisional, bench-gated post-v1.0. No hot-path C++ change. |
| **IX §1** | Coverage (≥90% line / ≥80% branch, touched files) | **PASS (Python-scoped)** | The lcov DA/BRDA gate scopes to C++; the C trampoline delta + pure-Python layer are exercised by the `python-bindings` matrix. New branches (liveness, close-flow, reentrancy, pickle) each get a discriminating pytest. |
| **IX §2/§3** | Sanitizers — Tier 1 ASan/UBSan/TSan | **PASS** | `python-bindings` none/asan/tsan legs run the full dir (extended in 054). SC-001 = ASan witnesses no UAF; SC-007 = watchdog witnesses no deadlock. (UBSan-leg carry-forward waiver per 053 D-9 / L-054-2 still applies — re-confirm at /speckit-verify.) |
| **X** | ABI policy — no breaking change; additive-only; `0→1` freeze | **PASS** | **No `c_api.h` change.** Codes 1200–1204 already in `error.h`; no new symbol, no signature change. The freeze holds. The deferred state/admin callback hooks (for the §4.5 6-method director) would be **additive** new symbols — not a freeze concern; explicitly out of scope (D-1). |
| **XI** | Concurrency — `async_mutex`, HALO | **N/A** | No new C++ coroutine/executor; the trampoline runs on the engine strand and only touches CPython under the GIL. |
| **XV §9** | No `std::mutex` in coroutine context | **N/A** | No C++ sync primitive added; reentrancy state is a GIL-protected Python attribute. |
| **XV §15** | No `drop-oldest` on app/session path | **N/A** | v1.0 keeps the §6.4 synchronous reacquire-and-call shape; no queue/handoff introduced. |
| **XVI §3** | `/clarify` MANDATORY before `/plan` (ABI/threading/error-semantics trigger) | **PASS** | `/speckit-clarify` ran 2026-06-27 (3 questions resolved). |
| **XVII** | Codex Gate A/B review gates | **PENDING** | Gate A runs after `/plan` (this triggers it — the feature touches threading + error semantics). Gate B before merge. |
| **XX** | Amendments — design-doc change requires Gate A review | **DEFERRED (carried as proposed text; Gate A reviews)** | The reentrancy decision (raise on send-from-callback too) amends **`[2m]`** (NOT the constitution). The amendment is **carried as proposed text in the bundle (research.md → "Proposed `[2m]` Article XX amendment") and deferred to `/implement`** — the live `.specify/2m-pybind.md` edit was **NOT** pre-applied (the round-1 in-place edit was reverted), matching the **043/051/054 precedent** (each deferred its live `[2m]`/constitution edit to `/implement`; the close-out `git add specs/<id>/` would not stage the design doc, so pre-applying creates a committed-bundle-vs-doc divergence). Complete site set is **6 substantive** — §1.3 rule (2), §6.5 send row, §6.5 close row, §9 seam #4, §6.7 1204 **table row**, **§6.7 1204 prose docstring (`:818-820`, the residual stale narrative)** — plus the editorial `Session.send` method docstring (`:455-460`). Gate A reviews the amendment, folded into this feature's Gate A. See research.md D-5 + the Proposed-amendment section. |

**Initial gate: PASS** (XVII pending = normal; XX flagged = a `[2m]` design-doc amendment folded into Gate A, not a violation). No constitution violations → Complexity Tracking empty.

## Project Structure

### Documentation (this feature)

```text
specs/055-python-lifetime-ownership/
├── plan.md              # This file
├── research.md          # Phase 0 — decisions D-1..D-12
├── data-model.md        # Phase 1 — entities E-1..E-8 (wrapper classes, sentinel, marker, ownership graph)
├── quickstart.md        # Phase 1 — OO usage + the lifetime/reentrancy/pickle behaviors
├── contracts/
│   └── python-lifetime-api.md   # Phase 1 — the OO API surface + close-flow + reentrancy + pickle contract
├── checklists/
│   └── requirements.md  # spec-quality checklist (from /speckit-specify)
└── tasks.md             # Phase 2 — /speckit-tasks (NOT created here)
```

### Source Code (repository root = the library submodule)

```text
bindings/python/
├── fixpp.i              # SWIG interface — EDIT: extend the %wrapper recv trampoline (construct Message
│                        #   wrapper, arm _dead on return, set/clear session._in_callback); change the
│                        #   register hand-wrapper so userdata carries the owning Session (callable
│                        #   DECREF on close/re-register); add the sub-interpreter check to engine_create.
├── fixpp_oo.py          # NEW pure-Python OO layer (Engine/Session/Message/Application/Dictionary
│                        #   wrappers, _dead sentinel, weakref child-tracking, ordered close-flow,
│                        #   context managers, pickle-ban, ObjectLifetime/CallbackReentrantClose raises).
│                        #   Re-exported from the `fixpp` package so `import fixpp; fixpp.Engine(...)` works.
├── CMakeLists.txt       # EDIT if a new .py needs install/packaging into the module (data file).
└── tests/
    ├── test_lifetime.py            # NEW — seams #3/#8: post-close/post-window → ObjectLifetime (ASan)
    ├── test_close_flow.py          # NEW — ordered close, idempotency, weakref child invalidation;
    │                               #   includes concurrent engine-entry witness: second thread attempts
    │                               #   engine.open_session(...) / engine.start() while engine.close() is
    │                               #   parked in native destroy, and must raise ObjectLifetime (1202)
    │                               #   without entering the C-ABI.
    ├── test_callback_lifetime.py   # NEW — callable owned-by-Session, DECREF on close/re-register (leak)
    ├── test_reentrancy.py          # NEW — send/close/engine_destroy from callback → CallbackReentrantClose (watchdog);
    │                               #   MUST include an io_threads>1 (e.g. io_threads=4) rotating-pool arm firing the
    │                               #   in-callback raise on a different OS thread (FR-016 / [2m §9 seam #4], NEW-P2b);
    │                               #   + Engine.close() all-sessions preflight: engine.close() from inside one session's
    │                               #   callback raises CallbackReentrantClose with NO sibling session closed (C-3 / Codex P1#2).
    │                               #   + Session.close() step-0 backstop: session.close() from inside its OWN callback raises
    │                               #   CallbackReentrantClose (1204) AND leaves state unmodified (_dead NOT armed, _application
    │                               #   NOT dropped, no native close) — proves step-0 precedes the NEW-P2a _dead-first arming (C-3 / Codex P1).
    ├── test_subinterpreter.py      # NEW — Engine from a sub-interpreter → SubInterpreterRejected
    ├── test_pickle_ban.py          # NEW — handle wrappers raise TypeError on pickle
    ├── test_context_manager.py     # NEW — with-block teardown + DeprecationWarning on GC-only teardown
    └── (existing) test_roundtrip.py / test_exceptions.py / test_gil_release_canary.py / ...  # MUST stay green
```

**Structure Decision**: A single new pure-Python module (`fixpp_oo.py`) hosts the OO layer; the flat SWIG substrate (`fixpp.i` → generated `fixpp.py` + `_fixpp.so`) is edited only in the trampoline + register hand-wrapper + engine_create. No new C-ABI, no new C++ TU. The OO classes are re-exported from the `fixpp` package so the public surface is `fixpp.Engine` / `fixpp.Session` / `fixpp.Message` / `fixpp.Application` / `fixpp.Dictionary` alongside the surviving flat functions.

## Complexity Tracking

> No constitution violations — section intentionally empty.

## Gate A

- Round 1 applied 2026-06-27: Codex P1=3 P2=2 P3=0; Opus post-judging P1=3 P2=4 P3=1; rewrite addresses root causes #1 (Article XX amendment DEFERRED — reverted live [2m], carried as proposed bundle text), #2 (userdata=Session callback-ownership), #3 (clarify-Q3→SC-003/US3), + Engine.close preflight, NEW-P2a (own _dead before native close), NEW-P2b (io_threads>1 test arm). Reviews: research/reviews/codex_055-python-lifetime-ownership_gate_a_review.md, research/reviews/opus_055-python-lifetime-ownership_gate_a_adversarial_review.md.
- Round 2 applied 2026-06-27: Codex P1=1 P2=1 P3=0; Opus post-judging P1=1 P2=1 P3=0; rewrite addresses RC#1 (Session.close step-0 _in_callback fail-fast + mirrors + test) and RC#2 (callback-ownership reframed to "binding-owned ref" incl. normative spec.md FR-011/SC-002/US2-test/AC1 + discriminating SC-002 witness). Reviews: research/reviews/codex_055-python-lifetime-ownership_gate_a_2_review.md, research/reviews/opus_055-python-lifetime-ownership_gate_a_2_adversarial_review.md.
- Round 3 escalation (Codex fixer) 2026-06-27: applied residual P2 — Engine.close() arms own _dead/_closing before the GIL-releasing child-close + engine_destroy (symmetric to NEW-P2a); SC-001 corrected to characterize both wrappers; data-model E-1 + research D-4 updated; concurrent-engine-entry test added. Review: research/reviews/opus_055-python-lifetime-ownership_gate_a_3_adversarial_review.md.
