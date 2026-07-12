# Feature 071 — Decision Record: S-032 residual DEFERRED (not built)

**Status:** DEFERRED — decision record only, no engine code shipped.
**Date:** 2026-07-12.
**Feature:** S-032 residual — standalone initiator-driven mid-session `ResetSeqNumFlag(141=Y)` sequence-reset originator.
**Outcome:** After `/specify` → `/clarify` → `/plan` → Gate A (2 rounds), the on-demand mid-session reset originator is **deferred** — neither viable implementation path is proportionate to the niche value, and the common reset cases are already shipped. The spec/plan/research artifacts in this directory are retained as the **investigation record** (they cost both paths); they do **not** describe shipped behavior.

## What S-032 residual is

The one *proactive, on-demand, mid-session* half of `ResetSeqNumFlag(141)`: an application resetting a live initiator session's sequence numbers to 1 without waiting for reconnect. The *reactive* halves — received-141 (acceptor + initiator ack arms), connect-time `ResetOnLogon`, teardown resets — are **shipped** (024/030/032) and cover the common cases.

## Why deferred — both paths are substantial (the costing)

### Path A — in-band live-socket reset (send Logon(141=Y) on the open transport)
This is what a naïve reading of FIX-SL §4.4.2 suggests, and it is interop-valid against QuickFIX (verified: QuickFIX accepts a mid-session Logon(141=Y) and resets without disconnecting). But delivering it *robustly* on the shipped engine requires **six** pieces of custom concurrency machinery, surfaced one defect at a time across Gate A rounds 1–2:
1. a new FSM edge Active→LogonSent (no existing transition leaves Active for LogonSent);
2. a two-edge application-callback model (suppress `onLogout` on the benign reset edge, but **force** it on a failed-reset teardown — else a logged-on session dies silently; Gate A round-1 New-P1);
3. an ack-arm outbound-restore predicate change (the shipped `peek_outbound()==2` inference silently mis-restores when a validation-`Reject` intervenes in the window → duplicate seq 1; Gate A round-1 P1-2);
4. a liveness-loop generation guard (the reset's re-entry to Active double-spawns the heartbeat loop);
5. an inbound-tolerance branch (the shipped LogonSent handler disconnects on ANY non-Logon inbound — `session.cpp:3776` — so a routine peer heartbeat in the reset window drops the connection, violating the "no disconnect" headline);
6. an outbound-emit quiescence mechanism (a `send()`/heartbeat suspended at the transport write resumes *after* the reset and emits a stale-seq frame; the transition-before-await alone does not close this; Gate A round-2 P1).

Each exists precisely **because no reference engine originates an in-band reset** (see Path B) and the engine is not built for a live-socket re-logon. This is a real concurrency feature touching shipped emit paths, not the "reuse-first" composition originally scoped.

### Path B — reconnect-based reset (logout + reconnect + ResetOnLogon)
This is how **both** QuickFIX-cpp and fix8 actually implement 24-hour-connectivity reset (schedule → `generateLogout()` + `disconnect()` + `m_state.reset()` → reconnect with ResetOnLogon). It would sidestep every concurrency mechanism in Path A. **But it is not shipped in fixpp and is not cheap:**
- **Reconnect-after-drop is explicitly DEFERRED** (`src/session/engine.cpp:1004-1016`: *"multi-cycle reconnect-respin DEFERRED"*). `run_connect_loop` does one connect + read-pump-until-EOF, then returns; after an established session drops, `drive_reconnect()` is never re-invoked. The `reconnect_policy`/backoff machinery is **initial-connect retry only** (`reconnect_fsm.cpp:104-149`).
- **No public reconnect trigger**: `open()` is single-use (`session.cpp:936`), `Engine::start()` once-only (`engine.hpp:273`), `close()` only tears down; no per-session disconnect+reconnect method.
- **`reset_on_logon` is not reachable** on a live session: `cfg_` is a private by-value copy with no setter (`session.hpp:622`), so an app cannot toggle a one-shot reset even if it could reconnect.
- What IS shipped: **start-of-session** reset only (construct with `reset_on_logon=true` → the single connect Logon resets both sides to 1). Not mid-session, not on-demand.

So a "thin wrapper over shipped reconnect" would first have to **build reconnect-after-drop** (a separate, broadly-valuable deferred capability — a production initiator that never reconnects after a drop is itself a real gap, tracked with the session-recovery family / "catalogue row 400") plus expose the reset toggle plus add an engine-level reconnect trigger. Bigger than S-032 itself.

## Decision

**Defer S-032's on-demand mid-session originator.** Record both paths' costs here so the next visit does not re-derive them. The genuinely-useful adjacent investment, if prioritized, is **reconnect-after-drop / session-recovery** (Path B's prerequisite) — which is broadly valuable independent of S-032 and would make reconnect-based S-032 a small follow-on. In-band (Path A) should only be built if session-*preserving* reset (no transport churn, no in-flight loss) becomes a concrete requirement.

The shipped reset surface (connect-time `reset_on_logon` + received-141) remains the supported way to reset sequence numbers.

## Evidence / artifacts

- Gate A round 1 reviews: `research/reviews/codex_071-midsession-seqnum-reset_gate_a_review.md`, `research/reviews/opus_071-midsession-seqnum-reset_gate_a_adversarial_review.md`.
- Gate A round 2 review: `research/reviews/codex_071-midsession-seqnum-reset_gate_a_2_review.md`.
- Investigation bundle (retained, describes the *considered* in-band design, NOT shipped): `spec.md`, `plan.md`, `research.md`, `data-model.md`, `contracts/`, `quickstart.md`.
- Interop basis: QuickFIX-cpp `Session.cpp` (`nextLogon` accepts mid-session 141=Y); fix8 `session.cpp` (schedule → logout+reconnect). Both surveyed 2026-07-12.
