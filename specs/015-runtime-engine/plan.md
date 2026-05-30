# Implementation Plan: Public Initiator/Acceptor Runtime Engine & Full T-041 Closure

**Branch**: `015-runtime-engine` | **Date**: 2026-05-30 | **Spec**: [`spec.md`](./spec.md)
**Input**: Feature specification from `specs/015-runtime-engine/spec.md`

**Pipeline state** (authority = `.specify/pipeline.md`, not this line): `/speckit-specify` (2026-05-30) → `/speckit-clarify` (2026-05-30, **3 Qs** — acceptor model static-default+optional-dynamic-hook; registry key = FIX SessionID tuple; lifecycle = injected executor + non-blocking start + idempotent stop; QFC/QFJ-grounded per `[[feedback_always_invoke_speckit_clarify]]`) → **`/speckit-plan` (this doc, step 3)** → next per `[const §XVII.1]` / `[pipeline.md step 4]` is **Phase-4 Gate A** on the 015 bundle. Then step 5 `/speckit-tasks` → 6 `/speckit-analyze` → 7 `/speckit-checklist` → 9 `/speckit-checklist-audit` (**MANDATORY**, blocks step 10; executor = checklist-auditor agent) → 10 `/speckit-implement` → 11 `/simplify` → 12 `/speckit-verify` → 14 Gate B → 19 MARK DONE.

## Summary

015 productionizes both FIX roles on top of 014's per-session live wiring and closes catalogue row **T-041**. It introduces one new public concrete type — a **runtime engine** in the existing **`session/`** module (NOT a new module — see Structure Decision), bound to a caller-supplied asio executor — that owns a `SessionID`-keyed session registry, drives an **initiator connect loop** (reusing 014's realized `ReconnectFsm::drive_reconnect_attempt`) and an **acceptor accept loop** (built on 012's listener surface), and runs a **continuous inbound read-pump** (real `wire::Framer::feed(incoming, carry, out)` surface) that feeds `Session::on_inbound_frame` on the session strand for every established session of either role. Sessions are constructed via the **public, synchronous ctor `Session(const EngineConfig&, const SessionConfig&)`** (`session.hpp:95`) + the awaitable `open()` (`:114`) awaited inside each loop — **no `make_session` factory and no `Application&` exist** (the prior research baseline was fabricated; Gate A round 1). On the **acceptor** path the accept loop **runs the TLS handshake itself** (`Listener::async_accept` returns a TCP-only transport; `listener.hpp:45-53`), harvests `handshake_result.peer_id`, and attaches via a **new acceptor-specific primitive** (design-named `attach_accepted_transport`) that sets `live_peer_id_` (`session.hpp:552`) + rebinds outbound **without** an FSM transition — **NOT `install_reconnected_transport`** (`session.hpp:475-477`), which re-enters `LogonSent` (initiator-only). The live arm is added at the **single acceptor gate `session.cpp:1048`** (mirror of 014's initiator arm at `:1864`; `:1913` is the initiator seam arm, NOT a second acceptor gate — Gate A New-7), and the test-only `logon_peer_identity_override` seam (`session_config.hpp:229`) is **removed** from both `:1048` and `:1913`, flipping **T-041** `implementing → done` for both roles. An engine-level accept-scope cancellation domain bounds the pre-session window (FR-014) and `stop()` joins all loops before clearing the registry (no UAF). Bounded below the Phase-5 service wrapper.

## Technical Context

**Language/Version**: C++23 (per constitution; `std::expected`, coroutines, `std::pmr`)
**Primary Dependencies**: Asio standalone (no Boost flavour — `boost::asio` per existing tree), OpenSSL 3.x (via 011/012 TLS surface), `std::pmr`, fixpp codegen output
**Storage**: N/A (in-memory session + registry state; message store is 008, not touched)
**Testing**: ctest running **GoogleTest/GoogleMock** targets (`[const §VII.1]`; the shipped framework — the prior "Catch2" was an error, Gate A Codex-7), libFuzzer (existing targets), Google Benchmark (existing), sanitizer matrix (ASan/UBSan/TSan)
**Target Platform**: Linux (WSL2 dev), gcc/clang; CI matrix is the gate
**Project Type**: single (C++ library — fixpp)
**Performance Goals**: no perf regression vs baseline; engine adds no per-frame allocation on the steady-state read-pump beyond the existing `Session` path
**Constraints**: caller-supplied executor, no engine-owned threads (clarify Q3); per-session strand (`[const §XI.4]`); **no `std::mutex` in awaitable headers** (`[const §XV.9]` — registry mutation sequenced on a strand, not a mutex); ASIO **total-cancellation** teardown for `stop()` (`[const §XI.2]`, `[[feedback_asio_cospawn_total_cancellation_default]]`); append-only error slots (`[const §X.4]`); fail-CLOSED authorization on BOTH the static and dynamic acceptor paths (`[const §XII]`)
**Scale/Scope**: ~13 FRs; new public type (engine) + optional dynamic-provider interface; brownfield reuse of 012/013/014 surfaces

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

*Articles validated against `/memory/constitution.md`:*

- **Article VI.5 (normative references per artifact)**: ✓ (fixed at Gate A round 1, Codex-8) — spec now carries a dedicated **## Normative References** section listing the exact `[FIXS §4.4]`, `[FIX-SL §4.2.2]`, `[FIX-SL §4.3]`, `[FIX-SL §4.5.2]` entries that inform FR-001..FR-014, plus the inherited-from-013/014 binding semantics. (The prior "Anchors/Catalogue"-only form was an overclaim.)
- **Article VII.1/VII.3 (TDD; red-green; framework)**: ✓ — the C++ test framework is **GoogleTest/GoogleMock** run under ctest (`[const §VII.1]`; the prior "Catch2" line was corrected — Gate A Codex-7). tasks.md sequences failing tests before impl (accept-loop+handshake, read-pump on the real Framer, registry-duplicate, acceptor fail-CLOSED incl. delayed-identity, bounded-first-frame DoS, seam-removal regression).
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
├── engine.hpp                 # NEW — runtime engine: ctor(exec, EngineConfig), register_session(SessionConfig), start, stop, lookup(SessionId)
├── acceptor_session_provider.hpp  # NEW *iff* R2 keeps dynamic path — optional pluggable provider (else omitted)
├── session.hpp               # on_inbound_frame (read-pump target, :230); public ctor (:95) + open() (:114); live_peer_id_ (:552, shipped by 014); + NEW acceptor attach primitive
├── session_config.hpp        # logon_peer_identity_override seam (:229) — REMOVED (FR-009); only static_assert is is_copy_constructible_v (:260) — NO field-count guard
└── reconnect_fsm.hpp         # drive_reconnect_attempt (014) — reused by the initiator connect loop

include/fixpp/wire/           # consumed, unchanged — real read-pump framing surface
└── framer.hpp                # Framer::feed(incoming, carry, out) + pmr_carry_buffer (:131-136 / :28-68); wire_frame_too_large on over-capacity

include/fixpp/transport/      # consumed, unchanged
├── listener.hpp              # accept-loop substrate (012) — async_accept returns TCP-only Transport (TLS issued by the caller, :45-53)
└── transport_factory.hpp     # consumed for the acceptor-side async_handshake

src/session/
├── engine.cpp                # NEW — connect-loop + accept-loop (handshake + bounded first-frame read + attach) + read-pump + SessionId registry + accept-scope teardown
└── session.cpp               # NEW acceptor attach primitive (no LogonSent); live peer_id arm at the ACCEPTOR gate :1048 ONLY (mirror of initiator :1864); seam removed at BOTH :1048 and :1913 (initiator)

tests/session/                # named coverage for the Gate-A findings (P2-4):
├── engine_acceptor_test.cpp      # US1 — accept+handshake → resolve → live-identity authorize (SC-001/002)
├── engine_readpump_test.cpp      # SC-003 — inbound frames delivered in arrival order on the strand, real Framer surface
├── engine_lifecycle_test.cpp     # US3/SC-005/SC-008 — clean stop() join-before-clear, no leaks/UAF under the sanitizer presets
├── engine_firstframe_test.cpp    # SC-011/FR-014 — bounded first-frame read: handshake/Logon deadline + over-budget close + slot reclaim
├── engine_acceptor_failclosed_test.cpp  # SC-002 + delayed-identity regression (live_peer_id_ delayed → fail-CLOSED, Gate A New-1)
└── engine_seam_removal_test.cpp  # SC-006 — zero logon_peer_identity_override; binding-logic tests on the live path
```

**Structure Decision**: Single C++ library (fixpp). 015 adds the public engine **inside the existing `session/` module** — NOT a new module: `session`'s allowed include edges (`check_layers.py:29`) already cover `transport`, and the engine is the session-orchestration concern that module owns. This avoids a `check_layers.py` ALLOWED-map change and an architecture.md §2.2 amendment; only a new arch §4.4 public-type entry is needed (R1). 015 **removes** the `session_config.hpp` seam and composes existing `transport/` (listener, transport_factory), `wire/` (Framer), `session/` (session, reconnect_fsm), and 013's authorization policy — no changes to their contracts beyond (a) a **new acceptor attach primitive** on `Session` (the acceptor's first-attach seam, parallel to but distinct from `install_reconnected_transport` — the reconnect primitive re-enters `LogonSent`, wrong for the acceptor), (b) the acceptor-gate identity-source swap at `session.cpp:1048` (mirror of 014's initiator arm at `:1864`), and (c) the seam removal at both `:1048` and `:1913`.

## Complexity Tracking

> No new module (engine goes in existing `session/` — Structure Decision). Remaining candidate: the optional dynamic-provider interface (Article XIV). Default per R2 = **defer it** (static matching alone closes T-041; `[[karpathy-guidelines]]` Simplicity First). No violations to track unless Gate A/user mandates the dynamic provider in-slice.

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|--------------------------------------|
| *(none — pending Phase 0 R2 dynamic-provider decision)* | — | — |

## Notes

- The headline correctness target is **engine `stop()` teardown under the full sanitizer matrix** — clean cancellation of accept loops, connect loops, read-pumps, and in-flight handshakes with no UAF/leak (the 014 Gate-B UAF lesson + `[[feedback_gateb_full_sanitizer_before_signoff]]`).
- T-041 closure must prove the live acceptor path **and** that the seam is gone and no test depends on it (FR-009/SC-006). The existing binding-logic tests (on/off/absent) must be re-pointed to drive a live handshake identity — a symmetric-fix obligation per `[[feedback_half_restructure_symmetric_api]]` (014 did the initiator half; 015 does the acceptor half + removes the now-unused seam).

---

## Gate A

- Round 1 applied 2026-05-30: Codex P1=8 P2=2 P3=0; Opus post-judging P1=6 P2=4 P3=2; rewrite addresses root causes #1 (construction/attach model re-derived from shipped make_session/Application&/first-attach), #2 (pre-session accept-scope cancellation + DoS bound), #3 (real Framer surface + cite re-anchor). Reviews: research/reviews/codex_015-runtime-engine_gate_a_review.md, research/reviews/opus_015-runtime-engine_gate_a_adversarial_review.md.

### Round 1 — what the rewrite changed (mapped to the recovered review artifacts)

The Codex review's recovered text states **8 P1 / 2 P2 / 0 P3**; the Opus adversarial judge's **POST-JUDGING TALLY is P1=6 P2=4 P3=2** (the binding tally per `[[feedback_gate_a_structural_rec_and_judge_independence]]`). The rewrite applied **all** confirmed findings, collapsed into the five root causes:
- **Root cause #1 (acceptor construction/attach/identity — half-restructure)** — re-derived the accept path: `async_accept` returns a TCP-only transport (`listener.hpp:45-53`), the accept loop runs the TLS handshake + harvests `handshake_result.peer_id`; a **new acceptor attach primitive** sets `live_peer_id_` + rebinds outbound **without** the `LogonSent` transition (NOT `install_reconnected_transport`); the live arm goes at the **single** acceptor gate `session.cpp:1048` (`:1913` is the initiator seam arm, New-7); a **happens-before invariant** + delayed-identity fail-CLOSED regression is written down (New-1). Collapses Codex-1/2/4, New-1, New-7. (research.md R3/R4/R7; data-model E-2/E-4; realized-behavior C1/C3; spec FR-005 + Clarifications.)
- **Root cause #2 (fabricated shipped-reality baseline)** — deleted the `make_session(SessionConfig, Application&, executor)` row + private-ctor narrative (the real ctor is the public synchronous `Session(const EngineConfig&, const SessionConfig&)`, `session.hpp:95`); corrected the Framer surface to `feed(incoming, carry, out)` + `pending_bytes()` (no `feed()/next()`); corrected the seam guard to the real `is_copy_constructible_v` assert (no field-count `static_assert`); pinned the 118/119/120 + no-reusable-code error boundary. Collapses Codex-3/5/6, New-2 (baseline half). (research.md baseline table + R5/R8.)
- **Root cause #3 (plan self-attestations)** — added the spec **## Normative References** section (Codex-8); corrected the test framework to GoogleTest/GoogleMock (Codex-7).
- **Root cause #4 (engine lifetime/teardown)** — strict `assert(stopped())` destructor (no best-effort sync path, Codex-9); `stop()` join-before-clear (New-4); bounded pre-session first-frame read = FR-014 + SC-011 (Codex-10); `open()` awaited inside the loops, `lookup()` may return not-yet-open/null (New-3). (research.md R9; data-model E-7; realized-behavior C5.)
- **Root cause #5 (read-pump app-message sink)** — 015 scoped to admin/session-layer flow; no app-message user sink (New-2). (research.md R10; spec FR-013 + US2 scope note.)
- **P3s**: dropped the speculative `SessionId::qualifier` (New-5); pinned the error-slot boundary first-hand instead of "confirm at /tasks" (New-6).

### Round 1 — caveats / disagreements

- **No findings dropped or disagreed-with.** Every confirmed Codex + Opus finding was applied. I independently re-read the shipped source first-hand and CONFIRM the reviews **branch-local**: `session.hpp:95` is the public ctor (no `make_session`, no `Application&`), `on_inbound_frame` is `:230`, `install_reconnected_transport` is the two-arg initiator re-install (decl `:475-477`, doc `:455-464`, body `session.cpp:206`), `live_peer_id_` is `:552`, `transport_send_` is `:534`; `listener.hpp:45-53` shows `async_accept` returns a TCP-only transport (TLS issued by the caller); `framer.hpp:131-136` is `feed(incoming, carry, out)` + `pending_bytes()` (over-capacity → `wire_frame_too_large`, `error.hpp:60`); `error.hpp` confirms `session_compid_unauthorized = 117` / `session_invalid_argument = 119` / `session_seqnum_too_high = 120` with 121 the next free slot; `engine_config.hpp:106-148` carries no `Application`. The `.cpp` gate-site line numbers (acceptor gate `session.cpp:1048`, initiator seam `session.cpp:1913`, 014 initiator live arm ~`session.cpp:1864`) are **branch-local-verified** (round 2 confirmed `src/session/session.cpp` is the FULL file on this branch, not a stub) — no live-`main` re-confirmation needed before `/speckit-implement`.
- **Tally note** (per `[[feedback_gate_a_structural_rec_and_judge_independence]]`): the Opus closing recommendation framed this as a "re-plan from Phase 0," but the spec survived intact (only +Normative-References, +FR-014/SC-011, +Clarifications, US2 scope tighten) and the design fixes are all in-bundle research/data-model/contracts edits — so this was applied as a single in-bundle convergence rewrite (the recovered Codex summary in the rewrite brief, "P1=4/P2=7/P3=6", does not match either authoritative artifact; the brief's `make_session`/private-ctor/first-attach premises were themselves derived from the fabricated baseline and are superseded by the first-hand re-derivation above).

- Round 2 applied 2026-05-30: Codex P1=0 P2=2 P3=2 (all 12 round-1 findings CLOSED); Opus post-judging P1=0 P2=3 P3=2 effective after rejecting hallucinated New-A; rewrite fixes NEW-1 (data-model lazy-construction wording), NEW-2 (session_compid_unauthorized 118→117 — only that slot), NEW-3 (first-frame direct-delivery), NEW-4 (de-stub branch-local anchors). Reviews: research/reviews/codex_015-runtime-engine_gate_a_2_review.md, research/reviews/opus_015-runtime-engine_gate_a_2_adversarial_review.md.

- Round 3 verification 2026-05-30: Codex P1=0 P2=1 P3=1 → both residuals fixed (P2 qualifier-drift in spec.md:13/107/124 removed to match 3-field SessionId; P3 "(main)" provenance tags stripped). No rewrite-counter consumed (textual reconciliation, not a design rewrite). Bundle converged P1=0 P2=0. Reviews: research/reviews/codex_015-runtime-engine_gate_a_3_review.md.

### Round 2 — disagreements

- **Opus round-2 New-A (rename `on_inbound_frame` → `inject_frame`, + an invented `pump(...)` / `Session(SessionConfig, exec, Transport)` API) REJECTED — hallucination.** Orchestrator grep confirms the real inbound method is `on_inbound_frame(std::span<const std::byte>)` at `session.hpp:230` (the bundle already uses it correctly everywhere); `grep -c "inject_frame|pump("` in `session.hpp` + `session.cpp` = **0** — those methods DO NOT EXIST, and the bundle has zero `on_frame`/`inject_frame` tokens. The judge invented a clean-room Session API and "found" a mismatch that isn't there. No `on_inbound_frame` usage was changed. (This is why the effective post-judging P2 count is 3, not the judge's raw 3-incl-New-A: New-A is not a real finding. Per `[[feedback_gate_a_structural_rec_and_judge_independence]]`, the Opus judge's severities are not trusted raw — each is source-checked.)
- NEW-2 sub-correction: the round-2 judge's claim that "all four error rows shifted +1" was WRONG. Verified against `error.hpp`: `session_compid_unauthorized = 117` (`:616`) was the ONLY wrong value; `session_invalid_argument = 119` (`:627`), `session_seqnum_too_high = 120` (`:653`), and the new `session_unknown_acceptor_session = 121` are all correct and were left unchanged.

---

*Based on Constitution — see `/memory/constitution.md`*
