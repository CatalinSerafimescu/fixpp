# Implementation Plan: Thin End-to-End Python Binding (PY-001)

**Branch**: `053-python-thin-binding` | **Date**: 2026-06-26 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/053-python-thin-binding/spec.md`

## Summary

Expand the SWIG Python binding (`bindings/python/`) from its one-function skeleton to a thin, genuinely
end-to-end surface that drives a full loopback FIX round-trip **through the public C-ABI only**: load a
FIX 4.4 dictionary from XML, stand up two engines (acceptor + initiator), establish over a loopback TCP
endpoint, send one application message, receive it in a Python callback, and read one scalar field. This
is the **first real C-ABI consumer / `0→1` freeze validator**: it exercises exactly the public 052 surface
(`fixpp_dict_load_from_xml`, `fixpp_session_config_set_tcp_endpoint`, `fixpp_session_acceptor_bound_endpoint`)
where the C-ABI's own tests used internal test-only seams.

Technical approach: a **selective** SWIG interface (wrap only the ~14 functions the round-trip needs, with
explicit typemaps for every out-param and a hand-written C trampoline for the inbound callback that does
`PyGILState_Ensure/Release`), linking the **static** `fixpp_capi` archive (PIC) plus `-static-libstdc++
-static-libgcc` into `_fixpp.so`. The deliverable is a `pytest` end-to-end test (written **first**, RED)
that performs the round-trip and asserts the received field equals the sent value, replacing today's
import+version smoke test as the Tier-1 `python-bindings` gate. No `include/fix/c_api.h` change.

## Technical Context

**Language/Version**: SWIG 4.x interface + C/C++ trampoline (C++23 toolchain), CPython 3.12 reference interpreter; Python test code (pytest).
**Primary Dependencies**: SWIG ≥4.0, `Python3::Module` (Development.Module), the static `fixpp_capi` archive (049/050/051/052 C-ABI), the bundled `dictionaries/FIX44.xml`.
**Storage**: N/A (in-memory FIX session over loopback TCP).
**Testing**: pytest (`bindings/python/tests/`) — one end-to-end loopback round-trip; plus a local AddressSanitizer build of the extension for the SC-004 evidence.
**Target Platform**: Linux x86_64 (in-tree build via `-DFIXPP_BUILD_PYTHON=ON`, the existing Tier-1 `python-bindings` job). macOS/Windows wheels and pip packaging are PY-005 / deferred.
**Project Type**: Language binding (SWIG) over an existing C ABI — single project, additive consumer.
**Performance Goals**: None (correctness slice; not a perf-sensitive module — Article VIII N/A).
**Constraints**: No C++ type, exception, or symbol crosses the `extern "C"` boundary (the C-ABI thunks catch-all), which is what makes static-libstdc++ safe. Live-I/O steps MUST use bounded, test-failing deadlines (never an unbounded wait → CI hang). No `include/fix/c_api.h` modification; the `0→1` ABI freeze stays held.
**Scale/Scope**: ~14 wrapped C-ABI functions, one trampoline, ~3 hand-written typemaps, one pytest round-trip. One P1 user story.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Article | Gate | Verdict |
|---|---|---|
| **IV §3** Distribution — Python via SWIG over the C ABI, Linux x86_64 | SWIG `fixpp.i` over `fixpp_capi`; Linux in-tree build. | **PASS** |
| **VI §5** Normative References in the `/specify` artifact | Added to `spec.md`; PY-001 is an existing OFFICIAL row (no new coverage-index entry). | **PASS** |
| **VII §2/§3/§4** pytest, TDD mandatory, no untested code | E2E pytest authored **first** (RED), then binding to green; no library `src/` added without a test. | **PASS** (TDD ordering enforced in tasks) |
| **VIII** Perf budgets / bench-in-PR | No perf-sensitive module touched; no bench required. | **N/A** |
| **IX §1** Coverage ≥95/85 on touched modules | **No `include/`–`src/` library module is modified** (additive consumer); binding code under `bindings/` is outside the `include src` lcov scope and is exercised by the e2e. | **N/A** (stated, reviewer-checkable) |
| **IX §2** Sanitizers Tier-1 (ASan/UBSan/TSan) | The trampoline is the riskiest surface → SC-004 requires a local ASan e2e run; CI-sanitized Python deferred to PY-002 (documented). | **PASS w/ documented deferral** |
| **X §1/§5/§6** ABI Policy — C-ABI is a versioned contract; ABI changes trigger Gate A + `/plan` sign-off | **C-ABI consumed unchanged** (no `c_api.h` edit) → X§6 ABI-change controls not triggered by an ABI change. Gate A still runs on this design bundle per the pipeline; reentrancy contracts are honored by the consumer (no blocking call from inside the callback — FR-013a). | **PASS** |
| **XI §3** No `std::mutex` in awaitable headers | Trampoline is flat C/C++, includes no `asio::awaitable<...>`. | **N/A** |
| **XII §5** Transport security — no implicit default; `unset` rejected at `Session::open()` | The loopback round-trip sets an **explicit** `insecure_plain_tcp` profile via `fixpp_session_config_set_security` (FR-004a, mirroring the gold reference); no implicit default, the `unset` sentinel is never relied on. §1–§4 vacuously satisfied (no TLS context); §7 not engaged (no app-layer `EncryptMethod(98)`). | **PASS** (explicit plaintext profile) |
| **XV** Banned patterns | No global new/delete witness, no banned idioms; GIL/threading handled at the trampoline. | **PASS** |

No violations requiring Complexity Tracking.

## Project Structure

### Documentation (this feature)

```text
specs/053-python-thin-binding/
├── plan.md              # This file
├── research.md          # Phase 0 — decisions D-1..D-9
├── data-model.md        # Phase 1 — handle/proxy/trampoline entities
├── quickstart.md        # Phase 1 — the Python round-trip script
├── contracts/
│   ├── python-module-surface.md   # the thin fixpp.* surface PY-001 exposes
│   └── swig-typemap-contract.md    # per-param typemap + trampoline contract
└── checklists/
    └── requirements.md  # spec quality checklist (from /speckit-specify)
```

### Source Code (repository root = the library submodule)

```text
bindings/python/
├── fixpp.i              # EXPANDED: selective %include + %ignore, out-param typemaps,
│                        #   %inline C trampoline (PyGILState + SWIG_NewPointerObj msg proxy)
├── CMakeLists.txt       # EXPANDED: static-link fixpp_capi (PIC) + -static-libstdc++/-libgcc;
│                        #   ensure SWIG MODULE links the static archive
└── tests/
    ├── test_smoke.py        # KEPT (import + version) — no longer the only gate
    └── test_roundtrip.py    # NEW: the e2e loopback round-trip (the Tier-1 gate)

include/fix/c_api/        # UNCHANGED — consumed, not modified (freeze held)
dictionaries/FIX44.xml    # bundled dictionary the test loads (existing)
```

**Structure Decision**: Single-project additive binding. All new code lives under `bindings/python/`
(`fixpp.i`, `CMakeLists.txt`, `tests/test_roundtrip.py`). The C-ABI headers/sources and the engine are
untouched. The existing `-DFIXPP_BUILD_PYTHON=ON` CMake path and the Tier-1 `python-bindings` job are the
build/test vehicle.

## Complexity Tracking

> No Constitution Check violations — section intentionally empty.

## Phase Notes

- **Phase 0 (`research.md`)** resolves: two-engine necessity, static-stdlib link model, SWIG
  selective-wrap + typemap enumeration, the callback trampoline (GIL + `Py_INCREF` callable lifetime +
  non-owning borrowed-msg proxy), poll-with-deadline for the live-I/O waits, the first-establishment
  session config (mirror `capi_loopback_support.hpp`), the msg-type/field choice, and the SC-004
  sanitizer approach.
- **Phase 1** emits `data-model.md` (handle/proxy/trampoline state), `contracts/` (the Python surface +
  the typemap/trampoline contract), and `quickstart.md` (the round-trip script).
- Command stops after Phase 1 design. **Gate A runs next** (per the pipeline: `/plan` → Gate A → `/tasks`).

## Gate A

- Round 1 applied 2026-06-26: Codex P1=3 P2=3 P3=1; Opus post-judging P1=2 P2=4 P3=4; rewrite addresses root causes A (engine_create 4-arg + stale dict name + false-provenance claim), B (gold-reference establishment recipe: security/reset_on_logon/heartbeat + cert/key typemap), C (FR-013a supersedes 2m §6.5 provenance note), D (FR-014 reword + callable-release reconcile) + New-2 (%exception scoping). No c_api.h change; freeze held; FR-012 survives. Reviews: research/reviews/codex_053-python-thin-binding_gate_a_review.md, research/reviews/opus_053-python-thin-binding_gate_a_adversarial_review.md.
- Round 2 applied 2026-06-26: Codex 6/8 RESOLVED, 2 PARTIAL P2; Opus post-judging P1=0 P2=2 P3=1; rewrite completes the two sibling sweeps — D-4 wrap-table now lists set_security/set_reset_on_logon/set_heartbeat_seconds (+ SECURITY_INSECURE_PLAIN_TCP enum); FR-013/D-5/T-4/python-module-surface callable-release reworded to INCREF-load-bearing + held-until-interpreter-exit (DECREF/registry = PY-004), matching data-model E-4; FR-004a names reset_seqnum_policy as the 4th knob. No c_api.h change; freeze held. Reviews: research/reviews/codex_053-python-thin-binding_gate_a_2_review.md, research/reviews/opus_053-python-thin-binding_gate_a_2_adversarial_review.md.
