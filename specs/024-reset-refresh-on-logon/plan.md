# Implementation Plan: ResetOn{Logon,Logout,Disconnect} Sequence-Number Lifecycle Knobs (S-017, G3 slice 3)

**Branch**: `024-reset-refresh-on-logon` | **Date**: 2026-06-06 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/024-reset-refresh-on-logon/spec.md`

## Summary

Complete catalogue row **S-017** (flip `backlog → done`) by adding three additive per-session `SessionConfig` boolean knobs — `reset_on_logon`, `reset_on_logout`, `reset_on_disconnect` (default `false`; their naming mirrors the QuickFIX config keys `ResetOnLogon`/`ResetOnLogout`/`ResetOnDisconnect` so a future `008` `cfg_loader` mapping is 1:1, but this slice does NOT add those keys to the loader — C++ fields only) — that trigger a sequence-number reset to `{1,1}` at the corresponding session lifecycle event. **`RefreshOnLogon` (S-018) is descoped** to its own slice (Clarifications — fixpp's `SeqnumManager` is not store-seeded at `open()`, so a meaningful refresh needs hydrate-plumbing fixpp lacks).

The reset reuses the **existing `013` reset primitive** (`SeqnumManager::reset_to_one()` + `MessageStore::reset()`), already used by the `ResetSeqNumFlag(141)=Y` handshake. This feature adds new *triggers* onto it at three existing FSM transition points — it introduces no new reset path, no new error slot, no codegen, and no new wire field.

**Key design grounding (source sweep + reference engines, in [research.md](./research.md)):**

- **The reset knobs drive `141=Y` on the initiator via the OR-of-three predicate (the spec input's "independent of 141" premise was disproven by the reference sweep).** Both QuickFIX-cpp (`Session.cpp:677-685` `generateLogon`, `shouldSendReset()` `:1037`) and QuickFIX-J (`Session.java:2083-2090` `generateLogon`, `isResetNeeded()` `:919`) reset locally then, because **any** reset knob is on **and** seqnums are now `{1,1}`, set `ResetSeqNumFlag(141)=Y` on the outbound Logon. fixpp today emits `141=Y` only when `reset_seqnum_policy_field == bilateral_strict` (`session.cpp:523-526`); this feature extends the predicate to `send_reset_flag || ((reset_on_logon || reset_on_logout || reset_on_disconnect) && seqnums=={1,1})`, after the logon-time reset. The `reset_on_logout`/`reset_on_disconnect` arms matter on the **next** initiator Logon after a teardown that reset to `{1,1}` — omitting them desyncs vs QuickFIX. The existing `013` `reset_seqnum_policy_field` continues to govern **validation** of the peer's `141` echo — emission and echo-validation are orthogonal.
- **The reset is a durable two-op helper, disposition keyed on trigger CAUSE.** `reset_seqnums_to_one_durable(disposition)` couples `SeqnumManager::reset_to_one()` (`seqnum_manager.hpp:113`, live counters) **then** `MessageStore::reset()` (persist). The store-failure disposition is keyed on the **trigger cause**, NOT the trigger location: **knob-driven** Logon reset = **fatal** (block `Active`); the **013-only received-`141`** reset (knobs off) = **logged-then-proceed (I-07)**, UNCHANGED from today's `session.cpp:1589-1592` so all-off is byte/semantics-identical (FR-001); teardown = logged-then-proceed. The pair already exists and is invoked by the acceptor on a received `141=Y` (`session.cpp:1584`). On the acceptor Logon path the knob reset and the received-`141` reset are collapsed into **one** combined pre-validation decision (`need_logon_reset = reset_on_logon || peer_sent_reset`) → one `store_->reset()` I/O with the stricter applicable disposition. `FileStore::reset()` is NOT a value no-op (full atomic-rename + fdatasync + dir-fsync every call) → both the acceptor Logon overlap (single combined decision) AND teardown double-triggers (single-fire guard) collapse to one observable store reset.
- **The wiring sites are existing FSM transition points re-anchored to where fixpp actually converges** — no new states, no new transition matrix entries:
  - `ResetOnLogon` initiator → `Session::open()` initiator arm (`session.cpp:519-561`): `reset_seqnums_to_one_durable()` **before** `peek_outbound()` (`:522`), then pass the OR-of-three `141` predicate to `build_logon`.
  - `ResetOnLogon` acceptor → inbound-Logon handler: a single combined pre-validation decision `need_logon_reset = reset_on_logon || peer_sent_reset` → **one** `reset_seqnums_to_one_durable()` **BEFORE `check_inbound(seq)` (`session.cpp:1437`)** with the stricter applicable disposition (knob present → fatal; 013-only received-`141` knob off → I-07). In the handshake state a too-low/too-high sequence is fatal, so a reset after `check_inbound` lets a fresh peer Logon `34=1` disconnect when local next-expected is > 1. The existing `:1584` `141`-receipt reset is subsumed (no second store I/O for `reset_on_logon && inbound 141=Y`); the reply Logon mirrors `141=Y`.
  - `ResetOnDisconnect` → `Session::close()` convergence (`session.cpp:823-1025`), **before** the SeqnumManager async-mutex drain (`~:1002`): fires on ANY `close()` (graceful, terminal, abnormal read-pump EOF/error → `close(terminal)`).
  - `ResetOnLogout` → keyed on a dedicated **`logout_seen_`** flag, NOT `close_mode::graceful` alone and NOT `onLogout_fired_`. `onLogout_fired_` (`session.cpp:175-191`) fires on ANY `Active→!Active` (incl. abnormal terminal close / fatal), so it cannot distinguish a Logout from a raw drop — deriving `logout_seen_` from it would collapse `reset_on_logout` into `reset_on_disconnect`. A locally-initiated Logout traverses `close(graceful)` (`:880`); a **peer-initiated** Logout never calls `close()` — it transitions inline to `Disconnected` (`session.cpp:~2095-2174`) and teardown arrives via read-pump EOF → `close(terminal)` (`engine.cpp:506`). Set `logout_seen_` at exactly two Logout-specific sites: (a) the local graceful-Logout-sent path (`close(graceful)` `:880` / generateLogout), and (b) the inbound `35=5` receipt in `Active`/`LogonReceived`/`LogoutSent` (`~:2095`). Fire the reset in `close()` when (`(logout_seen_ && reset_on_logout) || reset_on_disconnect`); a single-fire guard collapses a logout+disconnect double-trigger to one store reset.
- **All knobs default `false` ⇒ pure no-op.** Every existing session (and the `013` `bilateral_strict` 141 flow) is byte/semantics-identical when the knobs are off: the only behavior delta is gated behind an opted-in knob.

**This is a bounded change at three existing transition sites + one additive header POD.** It reuses the `013` reset primitive, the existing `build_logon` `141`-flag parameter, the `SessionConfig` additive-field pattern (cf. `021` `redeliver_poss_dup`, `022` `allow_pos_dup`), the `005`/`009` FSM transitions, and the `018` live-interop fixture. No new module, no codegen, no new `error::core` slot, no new public C-ABI surface.

## Technical Context

**Language/Version**: C++23 (Clang; coroutines, `std::expected`) — [const §II]
**Primary Dependencies**: existing `session::Session::{open,close}` + the inbound-Logon handler, `SeqnumManager::reset_to_one()` (013 reset primitive), `MessageStore::reset()`, `session::build_logon` (141-flag param), `SessionConfig`, `core::{error,expected_t}` — no new third-party deps
**Storage**: `MessageStore` — the knob-driven `reset_to_one()` path persists via the existing `MessageStore::reset()` (durable counter rewind), same as the `013` handshake reset; no new persistent state or schema
**Testing**: GoogleTest + GoogleMock; sanitizers ASan/UBSan/TSan; coverage llvm-cov; mallocnesia no-heap gate (the reset path is on the session strand, not a hot send path, but the new knob branches must not alloc); live interop ctest cells (skip-without-counterparty) extending the 018 fixture — [const §VII, §IX]
**Target Platform**: Linux/Clang (Tier 1); Windows/MSVC (Tier 2)
**Project Type**: single C++ library (`fixpp`) + tests-only interop-harness extension (parent `phase-9-harness/`)
**Performance Goals**: N/A — the reset fires at most once per session lifecycle transition (logon/logout/disconnect), not on the message hot path; `reset_to_one()` + `MessageStore::reset()` are existing awaitables on the session strand
**Constraints**: `noexcept`/`expected_t` house style; no `std::mutex` in awaitable headers ([const §XV.9] — the logic lives in `session.cpp`; the only header touch is three additive `SessionConfig` POD bools, no new include into `session.hpp`'s awaitable closure); the reset must persist (durable) before the dialogue proceeds (FR-008); idempotent onto `{1,1}` (FR-009)
**Scale/Scope**: production wiring at the existing convergence points (`open()` initiator arm; inbound-Logon acceptor handler — reset **before** `check_inbound`; the peer-Logout-received transition + local graceful path — a `logout_seen` flag; `close()` teardown — reset **before** the seqnum-mutex drain, single-fire guard) reusing one existing primitive via the `reset_seqnums_to_one_durable()` helper + **3** additive `SessionConfig` bools + unit witnesses (each knob: reset-when-on / unchanged-when-off / idempotent-single-store-reset / initiator-emits-141-OR-of-three / acceptor-admits-34=1 / peer-initiated-logout / durable-failure-blocks-Active / both-roles) + live interop cells (QFJ + QFcpp, both roles). No store-hydrate (S-018 deferred), no FIXT/5.0SP2 (G4), no other G3 knobs.

## Constitution Check

*GATE: must pass before Phase 0 (passed) and re-checked after Phase 1 (re-confirmed — design adds no new violation).*

| Article | Gate | Status |
|---------|------|--------|
| **II** Language | C++23/Clang, no new deps | ✅ PASS |
| **VI** Spec coverage | **S-017** (`ResetOnLogon/Logout/Disconnect`, `feature-catalogue.md`) `backlog → done` — this slice delivers the three reset knobs wired onto the `013` primitive. **S-018** (`RefreshOnLogon`) stays `backlog` (descoped — Clarifications). Exact catalogue/coverage-index delta below (applied at **Polish**, per the 020/021/022 precedent). | ⚠ RESOLVED (S-017 → done; S-018 stays backlog; delta specified) |
| **VII** Testing/TDD | every behavior lands RED-first: `ResetOnLogon` reset-to-1 (initiator + acceptor) from seeded non-1 seqnums; off ⇒ unchanged; initiator emits `141=Y` via the OR-of-three predicate after reset (and NOT when knobs off + policy≠bilateral_strict); `reset_on_logout`/`reset_on_disconnect` drive `141=Y` on the next initiator Logon; acceptor admits a fresh `34=1` with local next-expected>1 (reset before `check_inbound`); acceptor idempotent with `141`-receipt; durable store-failure blocks `Active` (Logon path); `ResetOnLogout` reset on **both** local-graceful AND peer-initiated Logout; `ResetOnDisconnect` reset on abnormal drop; double-trigger → exactly one store reset; no-ResendRequest-below-reset; GoogleTest | ✅ planned |
| **VII.6** Interop | extends the live QFJ/QFcpp both-role matrix with TWO `ResetOnLogon` cells — fixpp **initiator** (C6.1: emits `141=Y`, live acceptor accepts, resync at 1) AND fixpp **acceptor** (C6.2: live QFcpp/QFJ initiator sends `141=Y` + fresh `34=1`, fixpp resets-before-validation and accepts) — satisfying FR-010/SC-005 both-roles *live* interop | ✅ planned |
| **VIII.5** Allocator | the reset path adds only branch + existing-awaitable calls (`reset_to_one()`, `MessageStore::reset()`); no new heap. The reset is NOT on the no-heap send hot path, but the new branches in `open()`/`close()` must not allocate — asserted via a mallocnesia/alloc-guard witness on the reset path (not merely "covered by existing discipline") | ✅ planned (witnessed) |
| **IX.1** Coverage | ≥95/85 on the three new knob branches: each knob's on-arm (reset fires + persists), off-arm (no reset), and the initiator `141`-emission extension are RED-first tested paths | ✅ planned |
| **IX.2** Sanitizers | ASan/UBSan/TSan on the lifecycle-transition changes + interop ctest (018 discipline) | ✅ planned |
| **X** ABI | no C-ABI surface or error-slot change; adding three public `SessionConfig` bool members (`include/fixpp/session/session_config.hpp`) changes C++ struct layout → a normal source rebuild is required; default-`false` preserves all existing wire behavior (the `141`-emission extension only fires under an opted-in knob) | ✅ N/A / additive |
| **XI.4** Threading | the resets run synchronously on the existing session strand inside `open()`/`close()`/the inbound-Logon handler (already strand-confined per 023); **no new concurrency surface**, no callback, no off-strand call | ✅ PASS |
| **XII.5** No-implicit-default | all three knobs default to `false` explicitly and documented (QuickFIX-compatible no-op); no silent default-on | ✅ PASS |
| **XIV.2** Pluggable ≤5 pure-virtual | no new pluggable interface; the knobs are plain `SessionConfig` fields | ✅ N/A |
| **XV.9** Banned (`std::mutex` in awaitable hdr) | logic lives in `session.cpp`; the only header touch is three additive `SessionConfig` POD bools — verify no new include drags a mutex into `session.hpp` (Tier-1 unfiltered / `-L sync` per the §XV.9 watch-item) | ✅ PASS (watch-item flagged for verify) |
| **XVI.3 / XVI.4** /clarify before /plan | session lifecycle + config trigger → `/speckit-clarify` Session 2026-06-06 (3 axes: `141` coupling, config-key naming, `RefreshOnLogon` descope), engine-grounded ✅ | ✅ PASS |
| **XVII.1** Gate A before /tasks | mandatory — runs after this plan, before `/speckit-tasks` | ⚠ Gate A PENDING |

**Result**: PASS to proceed. One production-behavior change (the initiator now emits `141=Y` when `reset_on_logon` is on, beyond today's `bilateral_strict`-only emission) is surfaced in Complexity Tracking for Gate-A scrutiny; it is gated behind an opted-in default-`false` knob, reuses the proven `013` reset primitive + `build_logon` `141`-flag param, and is grounded 1:1 against both live interop targets. No unjustified violations.

**Exact §VI delta (written before `/speckit-tasks`; applied at Polish):**
- `spec/feature-catalogue.md`:
  - **S-017** (`ResetOnLogon/Logout/Disconnect`) `backlog → done`, cite 024. Completion note: *"Three additive SessionConfig reset knobs wired onto the 013 reset_to_one() primitive at open() (initiator, emits 141=Y), the inbound-Logon handler (acceptor), and close() (Logout/disconnect teardown); default false = QuickFIX-compatible no-op."*
  - **S-018** (`RefreshOnLogon`) stays `backlog` with a gap-note: *"Descoped from 024 — fixpp's SeqnumManager is not store-seeded at open(); a meaningful RefreshOnLogon needs a store→manager hydrate-on-open path (008-boundary change). Deferred to its own slice."*
  - Append the new B-/L- entries (below).
- `spec/coverage-index.md`: flip **S-017** to `done` with a 024 reference; keep **S-018** + `NextExpectedMsgSeqNum(789)` + the remaining G3 config knobs (`CheckCompID`, `validateSequenceNumbers`, `MaxLatency`) in the still-deferred set. At Polish, assert this as an **exact-set** diff (the done-flip moves exactly S-017; the deferred set loses exactly S-017 and retains S-018 + 789 + the named knobs) — not a subset-presence check ([[feedback_completeness_gate_exact_set_not_subset]]).
- `spec/behaviors-and-limitations.md`: add **B-024-1** (the three reset knobs reset seqnums to 1 at their lifecycle event; default off; the initiator announces `ResetOnLogon` via `141=Y`; the reset is durable + idempotent) and **L-024-1** (`RefreshOnLogon` is NOT implemented — fixpp's seqnum manager is not store-seeded at open, so there is no construction-time store cache to refresh; operators needing external-store seqnum mutation must restart the session; tracked for a future store-hydrate slice).

## Project Structure

### Documentation (this feature)

```text
specs/024-reset-refresh-on-logon/
├── plan.md              # this file
├── research.md          # Phase 0 — engine-grounded decisions (D1..D6)
├── data-model.md        # Phase 1 — the three knobs + the reset-trigger disposition table
├── contracts/
│   └── reset-knobs.md   # the SessionConfig knob surface + per-knob trigger/precedence contract
├── quickstart.md        # Phase 1 — how to exercise (unit + idempotency + initiator-141 + live interop cells)
├── checklists/
│   └── requirements.md  # spec-quality checklist (from /speckit-specify)
└── tasks.md             # Phase 2 — /speckit-tasks (NOT created here)
```

### Source Code (repository root = library submodule)

```text
src/session/
└── session.cpp          # PRIMARY (behavior sites — re-anchored to fixpp's real convergence):
                         #  (1) open() initiator arm (~:519-561): reset_seqnums_to_one_durable()
                         #      BEFORE peek_outbound() when reset_on_logon; extend build_logon's
                         #      reset-flag from (policy==bilateral_strict) to
                         #      (… || ((reset_on_logon||reset_on_logout||reset_on_disconnect) && {1,1})).
                         #  (2) inbound-Logon handler: ONE reset_seqnums_to_one_durable() via the
                         #      combined need_logon_reset = reset_on_logon || peer_sent_reset, BEFORE
                         #      check_inbound(seq) (~:1437), stricter disposition; subsumes the :1584
                         #      141-receipt reset (no second store I/O); reply mirrors 141=Y.
                         #  (3) peer-Logout-received (~:2095) + local graceful path (~:880): set a
                         #      dedicated logout_seen_ flag (NOT onLogout_fired_ / :175-184, which
                         #      fires on ANY Active->!Active incl. abnormal drop).
                         #  (4) close() teardown (~:935-1025), BEFORE the seqnum-mutex drain (~:1002):
                         #      reset_seqnums_to_one_durable() when reset_on_disconnect (ANY close) or
                         #      (reset_on_logout && logout_seen); single-fire guard so double-trigger
                         #      does one store reset.
include/fixpp/session/
└── session_config.hpp   # PUBLIC header: +3 additive POD bools:
                         #   bool reset_on_logon = false;
                         #   bool reset_on_logout = false;
                         #   bool reset_on_disconnect = false;
                         #   (next to 013 reset_seqnum_policy_field; QuickFIX-key parity; default off)

tests/session/
└── test_reset_on_lifecycle.cpp   # NEW: reset-on-logon (init+acceptor, seeded non-1 → {1,1}) /
                                  #   off-unchanged / initiator-emits-141 (and not when off) /
                                  #   acceptor idempotent w/ 141-receipt / reset-on-logout (graceful) /
                                  #   reset-on-disconnect (abnormal drop) / double-trigger idempotent /
                                  #   no-ResendRequest-below-reset (RED-first)

tests/interop/                    # extend 018 fixture: ResetOnLogon cell (fixpp init emits 141=Y, resync at 1)
phase-9-harness/                  # parent: live cells (QFJ/QFcpp, both roles)
```

**Structure Decision**: Single-library change at the existing FSM convergence points in `src/session/session.cpp` — `open()` initiator arm, inbound-Logon acceptor handler (reset **before** `check_inbound`), the peer-Logout-received transition + local graceful path (a `logout_seen` flag), and `close()` teardown (reset **before** the seqnum-mutex drain, with a single-fire guard) — plus **three** additive public-header `SessionConfig` bools. All sites reuse the one existing `013` `reset_to_one()` + `MessageStore::reset()` pair via the `reset_seqnums_to_one_durable()` helper. No new modules, files-of-record, error slots, or public C surface. The interop witnesses extend the existing 018 fixture and the parent harness.

## Complexity Tracking

| Change | Why needed | Why it carries a real Gate B (not a chore) |
|--------|------------|-------------------------------------------|
| `open()` initiator arm now `reset_seqnums_to_one_durable()` at logon when `reset_on_logon`, and emits `ResetSeqNumFlag(141)=Y` via the OR-of-three predicate beyond today's `bilateral_strict`-only emission | FR-002/FR-007 — `ResetOnLogon` semantics + the engine-grounded `141` coupling | Changes the **outbound Logon wire content** on the production session-establishment path: the `141=Y` flag is now emitted under `(reset_on_logon || reset_on_logout || reset_on_disconnect) && {1,1}`, and the initiator's seqnums are reset mid-`open()` before the Logon is built. Hazards: (1) **ordering** — the reset MUST precede `peek_outbound()` (`:522`) or the Logon carries a stale seqnum; (2) **echo-validation interaction** — the existing `013` `reset_seqnum_policy_field` must still correctly validate the peer echo when emission was knob-driven, so `bilateral_lenient`/`unilateral` + a knob must not wedge; (3) the OR-of-three means a `reset_on_logout`/`reset_on_disconnect` session announces `141=Y` on its next Logon after a teardown left `{1,1}`. De-risked by RED-first witnesses: initiator emits `141=Y` + resets to 1 (seeded non-1); `reset_on_logout`/`reset_on_disconnect` each drive `141=Y` on the next Logon; off ⇒ no extra `141`; each policy × knob combination logs on cleanly. |
| Acceptor inbound-Logon now `reset_seqnums_to_one_durable()` **before** `check_inbound(seq)` when `reset_on_logon` | FR-002/FR-003 — acceptor `ResetOnLogon` ordering | The reset MUST run before sequence validation, NOT around the existing `:1584` `141`-receipt reset: in the handshake state a too-low/too-high sequence is fatal, so a fresh peer Logon `34=1` against a local next-expected > 1 would disconnect first. De-risked by a RED witness: acceptor local next-expected > 1, inbound `34=1`, `reset_on_logon=true` → no disconnect, no ResendRequest, reaches `Active`, seqnums `{1,1}`. |
| `close()` teardown now `reset_seqnums_to_one_durable()` when `reset_on_disconnect` (any close) or `reset_on_logout` (**logout seen, either direction** — incl. the peer-initiated path that never enters `close(graceful)`), with a single-fire guard | FR-004/FR-005/FR-009 — `ResetOnLogout`/`ResetOnDisconnect` semantics | Adds a **durable seqnum reset at teardown** on the production close path. Hazards: (1) the reset must fire on an **abnormal** transport drop (read-pump EOF/error → `close(terminal)`), not only graceful Logout; (2) `reset_on_logout` must fire for a **peer-initiated** Logout, which transitions inline to `Disconnected` (`:~2095-2174`) and never calls `close(graceful)` — so it is keyed on a `logout_seen` flag, not `close_mode::graceful`; (3) the reset must run **before** the seqnum-mutex drain (`~:1002`) or `reset_to_one()` hits a drained mutex and silently no-ops; (4) a single-fire guard collapses a logout+disconnect double-trigger to one `store_->reset()` (which is full I/O every call, not a no-op). De-risked by RED-first witnesses: reset after abnormal drop, reset after **local** graceful Logout, reset after **peer-initiated** Logout, double-trigger → exactly one store reset. |

No 4th-project / repository-pattern / speculative-abstraction violations. All rows are bounded triggers onto the existing `013` reset primitive (coupled via the `reset_seqnums_to_one_durable()` helper) at existing transition sites, gated behind default-`false` opt-in knobs; the wire-behavior delta (initiator `141=Y` via the OR-of-three predicate) is grounded 1:1 against both live interop targets and RED-witnessed per policy combination.

## Gate A

- PENDING — runs after this plan, before `/speckit-tasks` ([const §XVII.1]). Reviews will land at `research/reviews/{codex,opus}_024-reset-refresh-on-logon_gate_a_*`.
- Round 1 applied 2026-06-06: Codex P1=2 P2=4 P3=1; Opus post-judging P1=3 P2=4 P3=3; rewrite addresses root causes 1 (acceptor/teardown wiring: 3-site reset incl. peer-logout + reset-before-gap-validation), 2 (three-knob 141 predicate + durable-reset helper + single-fire idempotency guard), 3 (cfg-loader overclaim removed, both-role witnesses split, off-default wording, normative cite). Reviews: research/reviews/codex_024-reset-refresh-on-logon_gate_a_review.md, research/reviews/opus_024-reset-refresh-on-logon_gate_a_adversarial_review.md.

- Round 2 applied 2026-06-06: Codex P1=1 P2=3 P3=1; Opus post-judging P1=1 P2=3 P3=2; rewrite addresses root causes 1 (knob-vs-013 reset disposition split — all-off 013 received-141 stays I-07 logged-then-proceed + regression witness), 2 (logout_seen dedicated flag, NOT onLogout_fired_), 3 (acceptor Logon-path single combined need_logon_reset → one store reset), 4 (fixpp-acceptor live interop cell + C6.1 both-roles). Reviews: research/reviews/codex_024-reset-refresh-on-logon_gate_a_2_review.md, research/reviews/opus_024-reset-refresh-on-logon_gate_a_2_adversarial_review.md.

- Round 3 reviewed 2026-06-06 (P1=0): Codex P2=1 P3=1; Opus post-judging P1=0 P2=1 P3=1 — Opus verified the round-2 core fixes are present + source-grounded (013 I-07 path, dedicated `logout_seen_`, combined acceptor reset, C6.1/C6.2 both-role cells); the residual P2+P3 were **stale narrative text in spec.md only** (the least-swept file), zero design rework owed. Rewrite budget exhausted (2/2) → user-authorized **fix-forward pass** (matching the 022 round-3 precedent): rewrote the Clarifications Q1 acceptor clause (`spec.md:28`) from the superseded "invoked twice / shared and idempotent" model to the single-combined `need_logon_reset` decision (one durable store reset), and reframed the US2 AC3 parenthetical (`spec.md:63`) from "second reset is a no-op" to the single-fire-guard framing (durable store reset is not a value no-op; see FR-009). No binding clause (FR/contract/data-model) changed — they were already correct. **Gate A converged (P1=0, P2=0 after fix-forward).** Reviews: research/reviews/codex_024-reset-refresh-on-logon_gate_a_3_review.md, research/reviews/opus_024-reset-refresh-on-logon_gate_a_3_adversarial_review.md.

### Round 1 — disagreements

- None. Every confirmed/escalated finding in the Opus adversarial review was applied. Opus's **New-5** (the branch/feature name `024-reset-refresh-on-logon` carries "refresh" though `RefreshOnLogon` is descoped) was explicitly marked "P3, no action required beyond a one-line note; do **not** rename a live branch mid-Gate-A" — recorded here as that note: the branch name predates the descope; `RefreshOnLogon` (S-018) is NOT in this slice (see Clarifications Q3 / D6 / C7.1), and no rename is performed.

### Round 2 — disagreements

- None. All round-2 findings (Codex P1=1 P2=3 P3=1; Opus adversarial P1=1 P2=3 P3=2) were verified against the cited production source and applied in full.
