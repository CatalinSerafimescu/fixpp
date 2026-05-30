# Implementation Plan: Public Initiator/Acceptor Runtime Engine & Full T-041 Closure

**Branch**: `015-runtime-engine` | **Date**: 2026-05-30 | **Spec**: [`spec.md`](./spec.md)
**Input**: Feature specification from `specs/015-runtime-engine/spec.md`

**Pipeline state** (authority = `.specify/pipeline.md`, not this line): `/speckit-specify` (2026-05-30) → `/speckit-clarify` (2026-05-30, **3 Qs** — acceptor model static-default+optional-dynamic-hook; registry key = FIX SessionID tuple; lifecycle = injected executor + non-blocking start + idempotent stop; QFC/QFJ-grounded per `[[feedback_always_invoke_speckit_clarify]]`) → **`/speckit-plan` (this doc, step 3)** → next per `[const §XVII.1]` / `[pipeline.md step 4]` is **Phase-4 Gate A** on the 015 bundle. Then step 5 `/speckit-tasks` → 6 `/speckit-analyze` → 7 `/speckit-checklist` → 9 `/speckit-checklist-audit` (**MANDATORY**, blocks step 10; executor = checklist-auditor agent) → 10 `/speckit-implement` → 11 `/simplify` → 12 `/speckit-verify` → 14 Gate B → 19 MARK DONE.

## Summary

015 productionizes both FIX roles on top of 014's per-session live wiring and closes catalogue row **T-041**. It introduces one new public concrete type — a **runtime engine** in the existing **`session/`** module (NOT a new module — see Structure Decision), bound to a caller-supplied asio executor — that owns a `SessionID`-keyed session registry, drives an **initiator connect loop** (reusing 014's realized `ReconnectFsm::drive_reconnect_attempt`) and an **acceptor accept loop** (built on 012's listener surface), and runs a **continuous inbound read-pump** that feeds `Session::on_inbound_frame` on the session strand for every established session of either role. The live acceptor path supplies the real `handshake_result.peer_id` to the acceptor Logon gate via the already-shipped `live_peer_id_` member + `install_reconnected_transport` entry point (`session.hpp:552`/`:477`) — the structural mirror of 014's initiator-gate change (`session.cpp:1864`) applied to the still-seam-only acceptor gate (`session.cpp:1048`). That lets the test-only `logon_peer_identity_override` seam (`session_config.hpp`) be **removed** and **T-041** flip `implementing → done` for both roles. Bounded below the Phase-5 service wrapper.

## Technical Context

**Language/Version**: C++23 (per constitution; `std::expected`, coroutines, `std::pmr`)
**Primary Dependencies**: Asio standalone (no Boost flavour — `boost::asio` per existing tree), OpenSSL 3.x (via 011/012 TLS surface), `std::pmr`, fixpp codegen output
**Storage**: N/A (in-memory session + registry state; message store is 008, not touched)
**Testing**: ctest + Catch2 (unit/integration), libFuzzer (existing targets), Google Benchmark (existing), sanitizer matrix (ASan/UBSan/TSan)
**Target Platform**: Linux (WSL2 dev), gcc/clang; CI matrix is the gate
**Project Type**: single (C++ library — fixpp)
**Performance Goals**: no perf regression vs baseline; engine adds no per-frame allocation on the steady-state read-pump beyond the existing `Session` path
**Constraints**: caller-supplied executor, no engine-owned threads (clarify Q3); per-session strand (`[const §XI.4]`); **no `std::mutex` in awaitable headers** (`[const §XV.9]` — registry mutation sequenced on a strand, not a mutex); ASIO **total-cancellation** teardown for `stop()` (`[const §XI.2]`, `[[feedback_asio_cospawn_total_cancellation_default]]`); append-only error slots (`[const §X.4]`); fail-CLOSED authorization on BOTH the static and dynamic acceptor paths (`[const §XII]`)
**Scale/Scope**: ~13 FRs; new public type (engine) + optional dynamic-provider interface; brownfield reuse of 012/013/014 surfaces

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

*Articles validated against `/memory/constitution.md`:*

- **Article VI.5 (normative references per artifact)**: ✓ — spec §Anchors/Catalogue maps every binding FR to `[FIXS §4.4]` (T-041), `[FIX-SL §4.2.2]` (CompID), `[FIX-SL §4.3]` (connection establishment) and the signed-off 012/013/014 surfaces. research.md will consolidate.
- **Article VII.1/VII.3 (TDD; red-green)**: ✓ — tasks.md sequences failing tests before impl (accept-loop, read-pump, registry-duplicate, acceptor fail-CLOSED, seam-removal regression).
- **Article IX.1/IX.2 (coverage + sanitizer matrix)**: ✓ — full ASan/UBSan/TSan matrix is the Gate-B precondition (SC-005/SC-008); engine `stop()` teardown is the headline sanitizer target.
- **Article X.4 (append-only error slots)**: ⚠ **research item R5** — the acceptor unmatched-Logon rejection and the registry duplicate-registration rejection MAY need new slots (next free after 120 = 121+); resolve whether existing codes suffice before appending. No renumbering; retired slots stay holes.
- **Article XI.2 (ASIO native cancellation)**: ✓ — `stop()` cancels all connect/accept loops + in-flight handshakes via **total** cancellation (FR-011); the read-pump and loops must `enable_total_cancellation()` or hang silently (`[[feedback_asio_cospawn_total_cancellation_default]]`).
- **Article XI.4 (per-session strand)**: ✓ — read-pump dispatches `on_inbound_frame` on the session strand (FR-004); registry mutation sequenced on an engine strand, no cross-session shared mutable state.
- **Article XII / XII.3 (security; fail-CLOSED)**: ✓ — T-041 closes fail-CLOSED on the live acceptor path (FR-006/007); the optional dynamic path routes through the **identical** `authorize()` gate (no fail-OPEN — clarify Q1). Inherited extraction order + event/code shapes unchanged (FR-008).
- **Article XIV / XIV.2 (new public type + pluggable-interface cap ≤5)**: ⚠ **research item R1/R2** — the engine is a **new public concrete type**. Default placement = the existing **`session/`** module (its allowed edges `{core, dictionary, wire, transport, log, otel}` per `check_layers.py:29` already cover everything the engine needs; no new module key, no `check_layers.py` change, no arch amendment — only a new arch §4.4 public-type entry). A new `runtime/` module is the **rejected** alternative (would need a `check_layers.py` ALLOWED key + an architecture.md §2.2 amendment — Gate-A-heavy, `[[feedback_gate_b_check_layers_post_fixer]]`). R1 confirms the §4.4 entry. The **optional dynamic-session-provider hook**, IF built, is a new pluggable interface (1–2 methods, well under the §XIV.2 cap of 5) needing one-paragraph Gate-A justification (Article XIV §2). Per `[[karpathy-guidelines]]` Simplicity First, R2's default is to **defer the dynamic provider** — static matching alone closes T-041; build the dynamic hook only if the user/Gate A requires it in this slice.
- **Article XV.9 (no std::mutex in awaitable headers)**: ✓ — the registry is the risk surface; design uses strand-sequenced mutation, not a mutex in an awaitable-corpus header. The corpus ctest (`-L sync`, unfiltered) is the witness (`[[feedback_awaitable_header_mutex_include_edge]]`).
- **Article XVII.1 (Gate A blocks /tasks)**: ✓ — Gate A is the next pipeline step after this plan.
- **Article XVII.8 (/speckit-verify mandatory)**: ✓ — step 12 before Gate B.

**Result**: PASS with **two ⚠ research items (R1/R2 interface+layer placement, R5 error slots)** to resolve in Phase 0 — none are violations, they are design decisions Gate A will scrutinize. No Complexity Tracking entries required yet; revisit after research.

## Project Structure

### Documentation (this feature)

```
specs/015-runtime-engine/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── contracts/           # Phase 1 output
└── quickstart.md        # Phase 1 output
```

### Source Code (repository root)

```
include/fixpp/session/         # engine lives in the EXISTING session module (R1)
├── engine.hpp                 # NEW — runtime engine: ctor(executor), register(SessionConfig), start, stop, lookup(SessionID)
├── acceptor_session_provider.hpp  # NEW *iff* R2 keeps dynamic path — optional pluggable provider (else omitted)
├── session.hpp               # on_inbound_frame (read-pump target); acceptor gate (:1048); live_peer_id_ (:552, shipped by 014)
├── session_config.hpp        # logon_peer_identity_override seam — REMOVED (FR-009)
└── reconnect_fsm.hpp         # drive_reconnect_attempt (014) — reused by the initiator connect loop

include/fixpp/transport/      # consumed, unchanged
├── listener.hpp              # accept-loop substrate (012)
└── transport_factory.hpp     # consumed for handshake on accept

src/session/
├── engine.cpp                # NEW — connect-loop + accept-loop + read-pump + SessionID registry
└── session.cpp               # acceptor gate (:1048) live peer_id arm (mirror of initiator :1864), then seam removed (:1048/:1913)

tests/session/                # accept-loop, read-pump, registry/duplicate, lifecycle, acceptor fail-CLOSED, seam-removal regression
```

**Structure Decision**: Single C++ library (fixpp). 015 adds the public engine **inside the existing `session/` module** — NOT a new module: `session`'s allowed include edges (`check_layers.py:29`) already cover `transport`, and the engine is the session-orchestration concern that module owns. This avoids a `check_layers.py` ALLOWED-map change and an architecture.md §2.2 amendment; only a new arch §4.4 public-type entry is needed (R1). 015 **removes** the `session_config.hpp` seam and composes existing `transport/` (listener, transport_factory), `session/` (session, reconnect_fsm), and 013's authorization policy — no changes to their contracts beyond the acceptor-gate identity-source swap (mirror of 014's `session.cpp:1864`) and the seam removal.

## Complexity Tracking

> No new module (engine goes in existing `session/` — Structure Decision). Remaining candidate: the optional dynamic-provider interface (Article XIV). Default per R2 = **defer it** (static matching alone closes T-041; `[[karpathy-guidelines]]` Simplicity First). No violations to track unless Gate A/user mandates the dynamic provider in-slice.

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|--------------------------------------|
| *(none — pending Phase 0 R2 dynamic-provider decision)* | — | — |

## Notes

- The headline correctness target is **engine `stop()` teardown under the full sanitizer matrix** — clean cancellation of accept loops, connect loops, read-pumps, and in-flight handshakes with no UAF/leak (the 014 Gate-B UAF lesson + `[[feedback_gateb_full_sanitizer_before_signoff]]`).
- T-041 closure must prove the live acceptor path **and** that the seam is gone and no test depends on it (FR-009/SC-006). The existing binding-logic tests (on/off/absent) must be re-pointed to drive a live handshake identity — a symmetric-fix obligation per `[[feedback_half_restructure_symmetric_api]]` (014 did the initiator half; 015 does the acceptor half + removes the now-unused seam).

---

*Based on Constitution — see `/memory/constitution.md`*
