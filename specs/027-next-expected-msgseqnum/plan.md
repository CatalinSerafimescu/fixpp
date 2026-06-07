# Implementation Plan: NextExpectedMsgSeqNum(789) fast session resume (G3 slice)

**Branch**: `027-next-expected-msgseqnum` | **Date**: 2026-06-07 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/027-next-expected-msgseqnum/spec.md`

## Summary

Add per-session `NextExpectedMsgSeqNum(789)` support so that, when both peers enable it, an at-logon sequence gap is recovered **within the Logon exchange** — each side advertises its next-expected-inbound in its Logon, and the counterparty proactively resends exactly the missing range, eliminating the `ResendRequest` round-trip. Default off ⇒ byte-identical no-op.

**This is a session/recovery-FSM change, not a light additive append. Gate A round 1 corrected the source model: fixpp has TWO structurally distinct inbound-Logon handlers** — acceptor `case fsm_state::NotConnected` (`session.cpp:1508`, fatal `check_inbound` @ `:1571`, reply emit @ `:1766`) and initiator `case fsm_state::LogonSent` (`:2755`, fatal `check_inbound` @ `:2842`) — **both fatal-on-too-high with no AwaitingResend on the Logon path.** The arm at `:1968-2009` is the *steady-state Active* too-high arm, OFF the reconnect-Logon path. The honor + behind-side recovery + suppression therefore live in BOTH Logon handlers (research D-0/D-7), and "reuse the existing resend walk" is an **extraction** of a 150-line inline coroutine block into `replay_outbound_range_` (research D-5), not a free call.

What is genuinely bounded:

1. **The field already exists in the dictionary; no codegen.** `dictionaries/FIX44.xml` defines tag **789** (`:6128`) and lists it (optional) inside the `Logon` message (`:284`). The **dictionary/codegen need not change**; inbound Logon processing here is the hand-written `interpret_logon` + `scan_frame_header` (NOT a generated validator), so the hand scan must add a `case 789:` and `interpret_logon` continues to tolerate the optional field (FR-010 holds).
2. **The builders are hand-written.** `build_logon` (`admin_messages.cpp:75`) already conditionally appends `ResetSeqNumFlag(141)` (`:150-155`) via a `bool reset_seqnum` param; 789 is the parallel conditional append. `build_logout` (called at `:2128`, `:2817`) supplies the X>N / invalid-789 error.

So the change is: **+1 additive `SessionConfig` bool; an additive 789 emit at the two Logon-build sites; an additive `case 789:` on the header scan; honor (read X / invalid-X / X</==/>N) in BOTH inbound-Logon handlers reusing the extracted `replay_outbound_range_` for X<N and `build_logout`+disconnect for X>N or invalid X; behind-side tolerance that gates the fatal `check_inbound` when the knob is on; and the `replay_outbound_range_` extraction itself.** No new wire field, no new error slot, no codegen, no C-ABI.

**Key design grounding (source sweep + reference engines, in [research.md](./research.md)):**

- **Single knob, emit+honor, default off** (Clarifications): matches QFcpp `m_sendNextExpectedMsgSeqNum` / QFJ `EnableNextExpectedMsgSeqNum`. One additive `SessionConfig` bool — no new header include (bool is primitive ⇒ §XV.9 is N/A here, unlike 026).
- **Advertised value — RESOLVED, plain `next_inbound_unsafe()` (NO `+1`)**: our Logon's 789 = next-expected-**inbound** = `seqnum_mgr_.next_inbound_unsafe()` at BOTH sites. The acceptor reply (`:1745`) is built AFTER `check_inbound` (`:1571`) which **advances `next_inbound_`**, so the plain read is already correct — fixpp does NOT need QFcpp's `+1` (QFcpp increments later). data-model E-OBO; RED-witnessed. Under 141 the value is cause-dependent (1 or 2 — data-model Reset table).
- **Comparison basis**: inbound `789=X` vs our next-outbound `N = seqnum_mgr_.peek_outbound()`: present-but-invalid X (parse→0) ⇒ Logout+disconnect FIRST (research D-10); else `X<N` ⇒ proactively resend `[X, N-1]` via `replay_outbound_range_`; `X==N` ⇒ in sync; `X>N` ⇒ `build_logout(...)` + disconnect (FR-005, QFcpp/QFJ parity). N (outbound) and the Active arm's `next_inbound_unsafe()` (inbound) are distinct counters (data-model I-NEX-11).
- **Behind-side recovery + suppression — both handlers, NO fallback** (Clarifications): when the knob is on AND the peer's Logon `MsgSeqNum = X_logon` is itself too-high, BOTH handlers must NOT take the fatal `check_inbound` path (`:1571` / `:2842`). **Held-Logon consume — formulation A (do-NOT-advance):** leave `next_inbound_` at X (no `set_next_inbound`), admit the peer's `[X, peer_N-1]` proactive resend **in-sequence via the existing Active in-sequence path** (it carries the counter to `peer_N`; the held Logon's seqnum is consumed by being inside that range), and emit NO at-logon `ResendRequest` (FR-004/FR-009). `set_next_inbound(X_logon+1)` is WRONG — it makes the resent frames too-low → fatal disconnect; post-recovery `next_inbound_ == peer_N` (data-model I-NEX-5/I-NEX-12, research D-7). The Active arm (`:1968-2009`) stays as recovery-of-last-resort for a lost proactive resend (research D-11). No automatic fallback — both ends must enable 789 (L-027-1).
- **Acceptor resend ordering (RC#4)**: for X<N the resend runs AFTER the acceptor reply Logon's `store_then_emit` succeeds (`:1766`), not at the parse point.
- **Default off ⇒ pure no-op**: no 789 emitted, inbound 789 ignored, the existing `ResendRequest` recovery (013) untouched, outbound Logon byte-identical (FR-006/SC-002).

## Technical Context

**Language/Version**: C++23 (Clang; asio awaitables, `std::expected`) — [const §II]
**Primary Dependencies**: `session::build_logon` (+ optional 789 append), `build_logout` (X>N / invalid-789 error), `SeqnumManager::{next_inbound_unsafe, peek_outbound, check_inbound}` (read-only reads — no new manager API), the `ResendRequest`-reply replay block (`session.cpp:2485-2635`) **extracted into `replay_outbound_range_`** (not free-reused), `scan_frame_header`/`FrameHeader` (+`case 789:`), `SessionConfig` (+1 bool). No new third-party deps, no new manager method, no store change.
**Storage**: read-only use of the existing message store via the existing replay walk; no schema/persistence change (orthogonal to the 025 hydrate-on-open work).
**Testing**: GoogleTest; ASan/UBSan/TSan; coverage llvm-cov; no-heap on the emit/resend send path (reuses no-heap builders); both-role unit witnesses; live interop ctest cell (skip-without-counterparty) vs a `EnableNextExpectedMsgSeqNum` peer. — [const §VII, §IX]
**Target Platform**: Linux/Clang Tier-1 (sanitizer matrix); the live cell runs against QFcpp/QFJ in the parent harness.
**Project Type**: single C++ library (`fixpp`) + tests + interop-harness extension.
**Performance Goals**: N/A — the happy path *removes* a wire round-trip; the emit is one extra field append; the resend reuses the existing walk. No new allocation, no new suspension on the default path.
**Constraints**: `noexcept`/`expected_t` on the send path; no new `std::mutex` in awaitable headers (§XV.9 — **N/A here**: the only header delta is a primitive `bool` in `session_config.hpp`, no new include); the default-off path MUST be byte-identical (FR-006); the proactive resend MUST reuse the existing replay walk (no second recovery implementation — [[feedback_half_restructure_symmetric_api]]).
**Scale/Scope**: +1 `SessionConfig` bool; `build_logon` gains an optional 789 (sig change + 2 emit sites: initiator `:601`, acceptor reply `:1745`); +789 capture on the `FrameHeader` struct (`:1152`) + a `case 789:` in `scan_frame_header` (`:1213`); the honor decision (invalid-X / X<N / ==N / >N) added to BOTH inbound-Logon handlers (acceptor `NotConnected` `:1508`, initiator `LogonSent` `:2755`) using the extracted `replay_outbound_range_` + `build_logout`; behind-side tolerance gating the fatal `check_inbound` (`:1571` / `:2842`) when the knob is on; the `replay_outbound_range_` extraction from `:2485-2635` (FSM transitions re-homed to callers); a secondary suppression note on the `:1968-2009` Active too-high arm. Unit witnesses (emit both roles incl. no-`+1`; honor X<N resend both roles; acceptor resend-after-reply ordering; X==N no-op; X>N + invalid-789 logout+disconnect; behind-side tolerance; bidirectional; lost-resend self-heal; default-off byte-identity + inbound-789-ignored; suppression; 3× reset cause table; walk single-impl; no-heap 789 entry) + live interop cells. No store/seqnum-manager API change; no FIXT/5.0 version-gating (G4).

## Constitution Check

*GATE: must pass before Phase 0 (passed) and re-checked after Phase 1 (re-confirmed).*

| Article | Gate | Status |
|---------|------|--------|
| **II** Language | C++23/Clang, no new deps | ✅ PASS |
| **VI** Spec coverage | Mark catalogue row **`S-031`** (`NextExpectedMsgSeqNum(789) in Logon`) `backlog → implementation-parity-4.4` (NOT blanket `done`): the row is versioned "5.0–5.0SP2, FIXT.1.1" and this slice delivers only FIX 4.4; the FIXT/5.0SP2 versions stay outstanding to G4. Normative refs: `[FIX-SL §4.4.1] Using NextExpectedMsgSeqNum(789)` (advertise/compare/proactive-resend/error) + `[FIX-SL §4.4] Message recovery` (the reused resend semantics + the off path) + `[FIX-SL §4.5.2] Sequence reset (ResetSeqNumFlag)` (cause-dependent post-reset advertised value, 024 interaction). Exact catalogue/coverage-index delta below (Polish). | ⚠ RESOLVED (delta specified) |
| **VII** Testing/TDD | RED-first: emit 789 both roles plain `next_inbound_unsafe()` no-`+1` (initiator `:601`, acceptor reply `:1745`); honor X<N → proactive resend of exactly `[X,N-1]` (PossDup app + GapFill admin) both roles, acceptor resend AFTER reply emit, no ResendRequest; X==N no resend; X>N + present-but-invalid 789 → Logout(text)+disconnect; behind-side tolerance (no fatal at `check_inbound` when knob on, `next_inbound_` left at X, final `next_inbound_ == peer_N`); bidirectional + lost-resend self-heal; default-off byte-identical Logon + inbound-789-ignored (013 recovery intact); 3× reset cause table; walk single-implementation + two-value-end (explicit-end-beyond-store ⇒ GapFill `NewSeqNo=21`, `EndSeqNo=0`-empty-store, BOTH callers) | ✅ planned |
| **VII.6** Interop | live both-role cell: fixpp+peer both `EnableNextExpectedMsgSeqNum` → gap recovered with no ResendRequest on the wire; vs QFcpp/QFJ | ✅ planned |
| **VIII.5** Allocator | emit reuses the no-heap `build_logon` (Writer + null_memory_resource); the proactive resend reuses the existing no-heap replay walk. No-heap witness on the emit + honor-resend path | ✅ planned (witnessed) |
| **IX.1** Coverage | ≥95/85 on the new emit append, the inbound-789 read, the X</==/> decision branches, and the suppression guard | ✅ planned |
| **IX.2** Sanitizers | ASan/UBSan/TSan on the session logon/recovery changes + interop ctest | ✅ planned |
| **X** ABI | no C-ABI/error-slot change. `build_logon` (internal `admin_messages.hpp` builder, NOT C-ABI) signature gains the optional 789 — source-level change to its 2 callers. +1 `SessionConfig` bool. Default-off preserves wire behaviour | ✅ source rebuild (internal sig change named) |
| **XI.4** Threading | runs on the existing session strand (logon handler + emit); no new concurrency surface, no callback | ✅ PASS |
| **XII.5** No-implicit-default | the knob defaults to **disabled** explicitly (FIX 4.x parity, opt-in), documented | ✅ PASS |
| **XIV.2** Pluggable ≤5 pure-virtual | no pluggable interface touched | ✅ N/A |
| **XV.9** Banned (`std::mutex` in awaitable hdr) | the only header delta is a primitive `bool` in `session_config.hpp` — no new include, no mutex risk | ✅ N/A |
| **XVI.3/4** /clarify before /plan | Session 2026-06-07 (3 asked: asymmetric/no-fallback, knob shape, too-high disposition) + reference sweep (version applicability, off-by-one, resend reuse, comparison basis) | ✅ PASS |
| **XVII.1** Gate A before /tasks | mandatory — runs after this plan, before `/speckit-tasks` | ⏳ PENDING (Gate A next) |

**Result**: PASS to proceed. The one wire-behaviour change (outbound Logon carries tag 789 when the knob is set; an inbound 789<N triggers a proactive resend; X>N triggers Logout+disconnect) is opt-in default-off, reuses the existing replay walk + Logout, the dictionary already permits 789 in the 4.4 Logon, and the behaviour is grounded against both live interop targets. No unjustified violations.

**Exact §VI delta (applied at Polish):**
- `spec/feature-catalogue.md`: mark **`S-031`** (`feature-catalogue.md:344`) status **`backlog → implementation-parity-4.4`** (NOT `done` — the row is versioned "5.0–5.0SP2, FIXT.1.1" and only FIX 4.4 ships here; FIXT/5.0SP2 stay outstanding to G4), cite `027-next-expected-msgseqnum` with a "FIX 4.4 parity; FIXT.1.1/5.0SP2 deferred to G4" gap-note; fill evidence_pr `(pending merge)` + Tests `tests/session/test_next_expected_msgseqnum.cpp` + the interop cell. Append B-027-1.
- `spec/coverage-index.md`: add the `§4.4.1` coverage entry for `S-031` with the same "4.4-parity, FIXT/5.0 outstanding" qualifier on the §4.4 / §4.4.1 / §4.7.1 rows (exact-set diff at Polish — [[feedback_completeness_gate_exact_set_not_subset]]).
- `spec/behaviors-and-limitations.md`: **B-027-1** (per-session NextExpectedMsgSeqNum(789): advertise next-expected-inbound in Logon; honor a peer's 789 with a proactive resend that eliminates the ResendRequest round-trip; X>N or present-but-invalid 789 ⇒ Logout+disconnect; default off byte-identical; FIX 4.4 only). **L-027-1** (789 is both-peers-required — NO automatic ResendRequest fallback at logon; if a peer doesn't support 789 the at-logon ResendRequest is suppressed and our own at-logon gap does not fast-recover; enable on both ends. Matches QFcpp/QFJ). **L-027-2** (lost proactive resend self-heals via the Active too-high arm on the next inbound frame; a never-recover hole would only arise if a future change also suppressed the Active arm).

## Project Structure

### Documentation (this feature)

```text
specs/027-next-expected-msgseqnum/
├── plan.md  ├── research.md  ├── data-model.md
├── contracts/next-expected-msgseqnum.md  ├── quickstart.md
├── checklists/requirements.md  └── tasks.md (Phase 2 — NOT created here)
```

### Source Code (repository root = library submodule)

```text
include/fixpp/session/
├── session_config.hpp     # +1 additive field: bool enable_next_expected_msg_seq_num = false;
│                          #   (primitive bool — no new include, §XV.9 N/A).
└── admin_messages.hpp     # build_logon: add optional 789 (e.g. std::optional<seqnum_t>
                           #   next_expected_seq, default nullopt) — append tag 789 after 141.
src/session/
├── admin_messages.cpp     # build_logon (:75): conditionally append 789 = *next_expected_seq
│                          #   (render_u32, w.append_raw(789,...)) after the 141 block.
└── session.cpp            # (1) emit_initiator_logon_ (:601 build_logon call): when knob on, pass
                           #     next_expected_seq = seqnum_mgr_.next_inbound_unsafe() (plain, no +1).
                           #  (2) acceptor reply Logon (:1745 build_logon call): when knob on, pass
                           #     next_expected_seq = seqnum_mgr_.next_inbound_unsafe() — already
                           #     post-check_inbound(:1571) increment, so NO +1 (data-model E-OBO).
                           #  (3) header scan: add `next_expected_msg_seq_num` to the FrameHeader
                           #     struct (:1152) + a `case 789:` to scan_frame_header's switch (:1213).
                           #  (4) ACCEPTOR inbound-Logon handler (NotConnected, :1508): when knob on
                           #     AND 789 present, X=parse_seqnum(789), N=peek_outbound():
                           #       invalid X (parse->0) -> build_logout(invalid)+disconnect (FIRST);
                           #       X>N -> build_logout("...too high...")+disconnect (FR-005);
                           #       too-high peer Logon MsgSeqNum -> behind-side tolerance (formulation A,
                           #         I-NEX-5): do NOT take the fatal :1571 check_inbound path; LEAVE
                           #         next_inbound_ at X (no set_next_inbound); admit the peer's
                           #         [X,peer_N-1] resend IN-SEQUENCE via the Active in-sequence path
                           #         (carries next_inbound_ to peer_N); emit no at-logon ResendRequest
                           #         (FR-004). NOT set_next_inbound(X_logon+1) (would fatal too-low);
                           #       X<N -> replay_outbound_range_(X, N-1, /*through_current=*/true)
                           #         AFTER the reply emit (:1766)
                           #         (RC#4 ordering); X==N -> no resend.
                           #  (5) INITIATOR inbound-Logon handler (LogonSent, :2755): symmetric honor
                           #     on the peer's Logon-ack 789; behind-side tolerance gates the fatal
                           #     :2842 check_inbound when knob on; X<N -> replay_outbound_range_ after
                           #     processing the ack (own Logon already sent @ :601).
                           #  (6) EXTRACT replay_outbound_range_(begin, requested_end,
                           #     end_is_through_current) from the inline :2485-2635 ResendRequest-reply
                           #     block (lambdas, gapfill_callback_threw, our_last clamp,
                           #     empty-store/per-slot/trailing-flush). PRESERVE the TWO-value end model:
                           #     helper owns eff_end clamp + GapFill NewSeqNo basis
                           #     (through_current?peek_outbound():requested_end+1) — NOT a single
                           #     end_inclusive (loses rr_end, regresses 013). ResendRequest caller passes
                           #     requested_end=rr_end, through_current=(rr_end==0); 789 caller passes
                           #     requested_end=N-1, through_current=true. Re-home the embedded
                           #     Disconnected transitions to the callers; both call it (single impl).
                           #  (7) Active too-high arm (:1968-2009): SECONDARY suppression note — stays
                           #     active as recovery-of-last-resort (research D-11); knob-off unchanged.
tests/session/
└── test_next_expected_msgseqnum.cpp (NEW; target session_next_expected_msgseqnum)
                           #   emit 789 both roles; honor X<N resend [X,N-1] (PossDup app + GapFill
                           #   admin) no ResendRequest; X==N no-op; X>N Logout(text)+disconnect;
                           #   default-off byte-identical Logon + inbound-789-ignored; suppression;
                           #   off-by-one acceptor reply; no-heap on emit/resend.
tests/interop/happy/
└── hp_fix44_next_expected_test.cpp (NEW)  # both-role live cell, skip-without-counterparty.
phase-9-harness/           # parent: live QFJ/QFcpp counterparty with EnableNextExpectedMsgSeqNum.
```

**Structure Decision**: A session-layer logon/recovery-FSM extension behind a default-off `SessionConfig` bool. Emit is an additive conditional field append in the hand-written `build_logon` at the two existing Logon-build sites (initiator + acceptor reply), plus a `case 789:` on the hand `scan_frame_header`. Honor is added to BOTH inbound-Logon handlers (acceptor `NotConnected` `:1508`, initiator `LogonSent` `:2755` — structurally distinct, research D-0): invalid-789 / X>N → `build_logout`+disconnect; X<N → the **extracted** `replay_outbound_range_` (factored from the inline `:2485-2635` walk, FSM transitions re-homed to callers); behind-side tolerance gating the fatal `check_inbound` (`:1571` / `:2842`) when the knob is on. The Active too-high arm (`:1968-2009`) is a secondary, recovery-of-last-resort site (kept active). No new module, no new error slot, no new seqnum-manager API, no codegen — the dictionary already permits 789 in the 4.4 Logon.

## Complexity Tracking

| Change | Why needed | Why it carries a real Gate B (not a chore) |
|--------|------------|-------------------------------------------|
| Honor inbound 789 in BOTH inbound-Logon handlers (acceptor `:1508`, initiator `:2755`): invalid-X / X</==/>N | FR-003/FR-005/FR-007/FR-008 — the core fast-resume + integrity error, symmetric over two structurally distinct handlers | Touches the logon/recovery FSM at two sites. Hazards: (1) the two handlers differ (fatal `check_inbound` @ `:1571` vs `:2842`; reply emit vs none) — a single-handler design (the original draft) doesn't work for the initiator; (2) **off-by-one** — RESOLVED to plain `next_inbound_unsafe()` no-`+1` (acceptor reply is post-`check_inbound`, E-OBO); wrong-by-one ⇒ wrong range / false X>N; (3) present-but-invalid 789 (parse→0) MUST Logout+disconnect BEFORE the X<N compare, else `[1,N-1]` full-history replay (remote-triggerable, D-10); (4) X>N MUST Logout(text)+disconnect (FR-005). RED witnesses: both-role X<N exact `[X,N-1]`; X==N noop; X>N + invalid-789 Logout+disconnect. |
| Behind-side recovery + suppression — both handlers (FR-004/FR-009) | the feature's purpose: the behind side must ACCEPT the peer's proactive resend, not just skip its ResendRequest | Suppressing the ResendRequest is necessary but NOT sufficient: each handler's own `check_inbound` (`:1571` / `:2842`) is fatal on the peer's too-high Logon MsgSeqNum BEFORE any resend is processed. When the knob is on the handler must NOT take the fatal path — it admits the fill and emits no at-logon ResendRequest. Get it wrong ⇒ the behind side fatally disconnects on the peer's Logon (opposite of the feature). The Active arm (`:1968-2009`) keeps emitting ResendRequest knob-off (013 intact) + stays as recovery-of-last-resort. RED: behind-side tolerance both roles; knob-off still ResendRequest; bidirectional no-double-recovery; lost-resend self-heal. |
| **Extract `replay_outbound_range_` from the inline `:2485-2635` walk** | FR-008 — reuse the resend semantics with exactly one implementation | This is NOT a free reuse: the walk is a 150-line inline coroutine block (two local lambdas, `gapfill_callback_threw`, `our_last` clamp, empty-store/per-slot/trailing-flush, 6+ embedded `record_state_transition_(Disconnected)` early-returns). Extracting it (FSM transitions re-homed to callers, error surfaced via `expected_t`) is the exact `[[feedback_half_restructure_symmetric_api]]` hazard the plan must honor, not just cite. **The signature MUST keep the walk's TWO-value end model — `(begin, requested_end, end_is_through_current)`, NOT a lossy single `end_inclusive`** (which loses `rr_end` and regresses the shipped 013 GapFill `NewSeqNo` for an explicit-end-beyond-store request). Get it wrong ⇒ two divergent walks, wrong-call-site Disconnected, or a corrupted 013 ResendRequest fill. RED: single-implementation invariant-count; two-value-end (explicit-end-beyond-store ⇒ `NewSeqNo=21`, `EndSeqNo=0`-empty-store) for BOTH callers; no-heap on the 789 entry specifically. |
| Emit 789 in `build_logon` at both sites + `case 789:` capture | FR-002/FR-007 — both roles advertise | Two emit sites (initiator `:601`, acceptor reply `:1745`); BOTH emit plain `next_inbound_unsafe()` (no `+1`); under 141 the value is cause-dependent (1 or 2 — Reset table). Header capture is a `case 789:` on the real `FrameHeader`/`scan_frame_header` (not a non-existent "LogonHeader"). Miss one / wrong value ⇒ asymmetric/incorrect advertise. RED: both Logons carry the correct 789; 3× reset cause table; default-off none (byte-identity). |
| `SessionConfig` +1 bool | FR-001 — the config surface | Additive primitive `bool` (default false). No new include (§XV.9 N/A). Low risk; included for completeness. |

No 4th-project / repository-pattern / speculative-abstraction violations. Every row extends the existing hand-written Logon builder + the two inbound-Logon handlers + the (now extracted) replay walk, behind a default-off knob; the wire delta is grounded against both live interop targets and RED-witnessed. The substantive risk concentration is the two-handler honor + behind-side recovery + the walk extraction — exactly what the RED witnesses + Gate A target.

## Gate A

- Round 1 applied 2026-06-07: Codex P1=4 P2=3 P3=1; Opus post-judging P1=9 P2=3 P3=4; rewrite re-derives honor/suppression against the real two-handler FSM (RC#1), scopes the replay_outbound_range_ extraction (RC#2), fixes off-by-one/141+789/invalid-789 data-model invariants (RC#3), acceptor resend ordering (RC#4), adds initiator honor path + behind-side recovery + bidirectional analysis, marks S-031 4.4-parity-partial. Reviews (PARENT-repo-relative — the review files are tracked at the parent, not the submodule which has no `research/reviews/`): `<parent>/research/G19-fix-fpml-iso20022/research/reviews/codex_027-next-expected-msgseqnum_gate_a_review.md`, `<parent>/research/G19-fix-fpml-iso20022/research/reviews/opus_027-next-expected-msgseqnum_gate_a_adversarial_review.md`.
- Round 2 applied 2026-06-07: Codex P1=2 P2=0 P3=1; Opus post-judging P1=3 P2=2 P3=2; rewrite fixes the behind-side counter model (RC#1 — leave next_inbound_ at X, peer's resend carries to peer_N; NOT Codex's wrong seq+1), preserves the replay_outbound_range_ two-value end model (RC#2), sweeps the stale +1 drift (spec.md:18), grounds D-12 on the counter model, fixes review-path + anchor nits. Reviews (PARENT-repo-relative): `<parent>/research/G19-fix-fpml-iso20022/research/reviews/codex_027-next-expected-msgseqnum_gate_a_2_review.md`, `<parent>/research/G19-fix-fpml-iso20022/research/reviews/opus_027-next-expected-msgseqnum_gate_a_2_adversarial_review.md`.
