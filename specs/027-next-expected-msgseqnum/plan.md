# Implementation Plan: NextExpectedMsgSeqNum(789) fast session resume (G3 slice)

**Branch**: `027-next-expected-msgseqnum` | **Date**: 2026-06-07 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/027-next-expected-msgseqnum/spec.md`

## Summary

Add per-session `NextExpectedMsgSeqNum(789)` support so that, when both peers enable it, an at-logon sequence gap is recovered **within the Logon exchange** — each side advertises its next-expected-inbound in its Logon, and the counterparty proactively resends exactly the missing range, eliminating the `ResendRequest` round-trip. Default off ⇒ byte-identical no-op. A **source sweep settled that this is far more bounded than it looks**:

1. **The field already exists in the dictionary.** `dictionaries/FIX44.xml` defines tag **789** (`:6128`) **and lists it (optional) inside the `Logon` message** (`:284`, `<field name='NextExpectedMsgSeqNum' required='N'/>`). ⇒ inbound 789 is already a *defined* Logon field (not rejected as unknown), and **no codegen/dictionary change is needed** (FR-010 holds).
2. **The resend machinery already exists.** The `ResendRequest`-reply walk (`session.cpp:2485+`) already replays stored application messages with `PossDupFlag(43)=Y`+`OrigSendingTime(122)` keeping their original `MsgSeqNum`, and collapses admin/absent runs into one `SeqReset`-`GapFill`. The 789 proactive resend **reuses this exact walk**, triggered by an inbound `789=X < N` instead of by a received `ResendRequest{BeginSeqNo=X, EndSeqNo=N-1}`.
3. **The builders are hand-written.** `build_logon` (`admin_messages.cpp:75`) already conditionally appends `ResetSeqNumFlag(141)` via a `bool reset_seqnum` param; 789 is the parallel conditional append. `build_logout` (used at `:1875/:2128/:2257`) supplies the X>N error.

So the change is: **+1 additive `SessionConfig` bool, an additive 789 emit at the two Logon-build sites, an additive 789 read on the inbound-Logon path, a comparison that reuses the existing resend walk (X<N) or the existing Logout+disconnect (X>N), and suppression of the redundant at-logon `ResendRequest` when the knob is on.** No new wire field, no new error slot, no codegen, no C-ABI.

**Key design grounding (source sweep + reference engines, in [research.md](./research.md)):**

- **Single knob, emit+honor, default off** (Clarifications): matches QFcpp `m_sendNextExpectedMsgSeqNum` / QFJ `EnableNextExpectedMsgSeqNum`. One additive `SessionConfig` bool — no new header include (bool is primitive ⇒ §XV.9 is N/A here, unlike 026).
- **Advertised value**: our Logon's 789 = next-expected-**inbound** = `seqnum_mgr_.next_inbound_unsafe()`. **Off-by-one**: the acceptor's *reply* Logon (`session.cpp:1745`) advertises the value as computed **before** the inbound Logon increments the target counter (QFcpp `generateLogon(aLogon)` uses `getExpectedTargetNum()+1`); the exact fixpp expression is pinned in data-model E-OBO against fixpp's own increment timing (RED-witnessed, not assumed).
- **Comparison basis**: inbound `789=X` vs our next-outbound `N = seqnum_mgr_.peek_outbound()`: `X<N` ⇒ proactively resend `[X, N-1]` (the reuse walk); `X==N` ⇒ in sync; `X>N` ⇒ `build_logout("NextExpectedMsgSeqNum too high, expecting N but received X")` + disconnect (FR-005, QFcpp/QFJ parity).
- **Both-peers-required, NO fallback** (Clarifications): when the knob is on, the at-logon too-high arm (`session.cpp:1964-1991`) **suppresses** its `ResendRequest` and relies on the peer's proactive resend (FR-004/FR-009). If the peer doesn't support 789, recovery of our own gap does not complete — a documented limitation (L-027-1), matching QFcpp/QFJ.
- **Default off ⇒ pure no-op**: no 789 emitted, inbound 789 ignored, the existing `ResendRequest` recovery (013) untouched, outbound Logon byte-identical (FR-006/SC-002).

## Technical Context

**Language/Version**: C++23 (Clang; asio awaitables, `std::expected`) — [const §II]
**Primary Dependencies**: `session::build_logon` (+ optional 789 append), `build_logout` (X>N error), `SeqnumManager::{next_inbound_unsafe, peek_outbound}` (read-only — no new manager API), the existing `ResendRequest`-reply replay walk (`session.cpp:2485+`, reused), `SessionConfig` (+1 bool). No new third-party deps, no new manager method, no store change.
**Storage**: read-only use of the existing message store via the existing replay walk; no schema/persistence change (orthogonal to the 025 hydrate-on-open work).
**Testing**: GoogleTest; ASan/UBSan/TSan; coverage llvm-cov; no-heap on the emit/resend send path (reuses no-heap builders); both-role unit witnesses; live interop ctest cell (skip-without-counterparty) vs a `EnableNextExpectedMsgSeqNum` peer. — [const §VII, §IX]
**Target Platform**: Linux/Clang Tier-1 (sanitizer matrix); the live cell runs against QFcpp/QFJ in the parent harness.
**Project Type**: single C++ library (`fixpp`) + tests + interop-harness extension.
**Performance Goals**: N/A — the happy path *removes* a wire round-trip; the emit is one extra field append; the resend reuses the existing walk. No new allocation, no new suspension on the default path.
**Constraints**: `noexcept`/`expected_t` on the send path; no new `std::mutex` in awaitable headers (§XV.9 — **N/A here**: the only header delta is a primitive `bool` in `session_config.hpp`, no new include); the default-off path MUST be byte-identical (FR-006); the proactive resend MUST reuse the existing replay walk (no second recovery implementation — [[feedback_half_restructure_symmetric_api]]).
**Scale/Scope**: +1 `SessionConfig` bool; `build_logon` gains an optional 789 (sig change + 2 emit sites: initiator `:601`, acceptor reply `:1745`); +789 read on the inbound-Logon header scan (`:1167` struct + `:1253`-style case); the honor decision (X<N / ==N / >N) in the inbound-Logon handler reusing the `:2485+` walk + `build_logout`; suppression guard on the `:1964-1991` too-high `ResendRequest` arm; unit witnesses (emit both roles; honor X<N resend; X==N no-op; X>N logout+disconnect; default-off byte-identity + inbound-789-ignored; ResendRequest suppression; off-by-one) + a live interop cell. No store/seqnum-manager API change; no FIXT/5.0 version-gating (G4).

## Constitution Check

*GATE: must pass before Phase 0 (passed) and re-checked after Phase 1 (re-confirmed).*

| Article | Gate | Status |
|---------|------|--------|
| **II** Language | C++23/Clang, no new deps | ✅ PASS |
| **VI** Spec coverage | Flip catalogue row **`S-031`** (`NextExpectedMsgSeqNum(789) in Logon`) `backlog → done`. Normative refs: `[FIX-SL §4.4.1] Using NextExpectedMsgSeqNum(789)` (the row's cited section — advertise/compare/proactive-resend/error) + `[FIX-SL §4.4] Message recovery` (the reused resend semantics + the off path) + `[FIX-SL §4.5.2] Sequence reset (ResetSeqNumFlag)` (post-reset advertised=1 interaction with 024). Exact catalogue/coverage-index delta below (Polish). | ⚠ RESOLVED (delta specified) |
| **VII** Testing/TDD | RED-first: emit 789 both roles (initiator `:601`, acceptor reply `:1745`); honor X<N → proactive resend of exactly `[X,N-1]` (PossDup app + GapFill admin), no ResendRequest; X==N no resend; X>N → Logout(text)+disconnect; default-off byte-identical Logon + inbound-789-ignored (013 recovery intact); off-by-one on acceptor reply | ✅ planned |
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
- `spec/feature-catalogue.md`: flip **`S-031`** (`feature-catalogue.md:344`) status **`backlog → done`**, cite `027-next-expected-msgseqnum`; fill evidence_pr `(pending merge)` + Tests `tests/session/test_next_expected_msgseqnum.cpp` + the interop cell. Append B-027-1.
- `spec/coverage-index.md`: add the `§4.4.1` coverage entry for `S-031` (exact-set diff at Polish — [[feedback_completeness_gate_exact_set_not_subset]]).
- `spec/behaviors-and-limitations.md`: **B-027-1** (per-session NextExpectedMsgSeqNum(789): advertise next-expected-inbound in Logon; honor a peer's 789 with a proactive resend that eliminates the ResendRequest round-trip; X>N ⇒ Logout+disconnect; default off byte-identical). **L-027-1** (789 is both-peers-required — NO automatic ResendRequest fallback; if a peer doesn't support 789 the at-logon ResendRequest is suppressed and our own gap does not recover; enable on both ends. Matches QFcpp/QFJ).

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
                           #     next_expected_seq = seqnum_mgr_.next_inbound_unsafe().
                           #  (2) acceptor reply Logon (:1745 build_logon call): when knob on, pass
                           #     next_expected_seq per data-model E-OBO (off-by-one vs increment timing).
                           #  (3) inbound-Logon header scan (:1167 LogonHeader struct + :1253-style
                           #     case): capture tag 789 -> next_expected_msg_seq_num view.
                           #  (4) inbound-Logon handler (after the seqnum/141 handling, ~:1683): when
                           #     knob on AND 789 present, compute X=parse, N=seqnum_mgr_.peek_outbound():
                           #       X<N  -> trigger the resend walk for [X, N-1] (reuse the :2485+
                           #               ResendRequest-reply replay logic — factor a helper if needed);
                           #       X==N -> no resend;
                           #       X>N  -> build_logout("NextExpectedMsgSeqNum too high, expecting N
                           #               but received X") + disconnect (FR-005).
                           #  (5) too-high at-logon arm (:1964-1991): when knob on, SUPPRESS the
                           #     ResendRequest(2) (rely on peer's 789-driven proactive resend; FR-004).
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

**Structure Decision**: A session-layer logon/recovery extension behind a default-off `SessionConfig` bool. Emit is an additive conditional field append in the hand-written `build_logon` at the two existing Logon-build sites (initiator + acceptor reply). Honor is a comparison in the inbound-Logon handler that **reuses the existing `ResendRequest`-reply replay walk** (`session.cpp:2485+`) for `X<N` and the existing `build_logout`+disconnect for `X>N`, plus a suppression guard on the existing too-high `ResendRequest` arm. No new module, no new error slot, no new seqnum-manager API, no codegen — the dictionary already permits 789 in the 4.4 Logon.

## Complexity Tracking

| Change | Why needed | Why it carries a real Gate B (not a chore) |
|--------|------------|-------------------------------------------|
| Honor inbound 789: X</==/> decision in the inbound-Logon handler, reusing the `:2485+` replay walk | FR-003/FR-005/FR-008 — the core fast-resume + integrity error | Touches the logon/recovery FSM. Hazards: (1) **off-by-one** in the comparison basis (X vs `peek_outbound()` N) and in the **advertised** value (acceptor reply, data-model E-OBO) — wrong by one ⇒ resend the wrong range or false X>N error; (2) the proactive resend MUST reuse the existing walk (replay app w/ PossDup keeping original seqnum, collapse admin to GapFill) — a second implementation would drift ([[feedback_half_restructure_symmetric_api]]); (3) X>N MUST Logout(text)+disconnect, not silently sync (FR-005). RED witnesses: X<N resends exactly `[X,N-1]` (PossDup app + GapFill admin); X==N resends nothing; X>N emits Logout w/ text then disconnects. |
| Suppress the at-logon `ResendRequest` when the knob is on (`:1964-1991`) + no double recovery | FR-004/FR-009 — both-peers-required, no double recovery | The too-high arm currently always emits `ResendRequest(2)`. When 789 is on, it MUST be suppressed (rely on peer's proactive resend); but ONLY when 789 is on — the default path MUST keep emitting it (013 recovery intact). Get the guard wrong ⇒ either double recovery (789 resend + ResendRequest) or a broken default path. RED witnesses: knob-on at-logon gap emits **no** ResendRequest; knob-off still emits ResendRequest (013 regression guard). |
| Emit 789 in `build_logon` at both Logon-build sites | FR-002/FR-007 — both roles advertise | Two emit sites (initiator `:601`, acceptor reply `:1745`); BOTH must emit when the knob is on, with the correct value (the acceptor reply carries the off-by-one). Miss one ⇒ asymmetric/incorrect advertise. RED witnesses: initiator Logon and acceptor reply Logon each carry 789 = the correct next-expected; default-off carries none (byte-identity). |
| `SessionConfig` +1 bool | FR-001 — the config surface | Additive primitive `bool` (default false). No new include (§XV.9 N/A). Low risk; included for completeness. |

No 4th-project / repository-pattern / speculative-abstraction violations. Every row extends the existing hand-written Logon builder + inbound-Logon handler + the existing replay walk, behind a default-off knob; the wire delta is grounded against both live interop targets and RED-witnessed. The single substantive risk concentration is the off-by-one / suppression correctness, which is exactly what the RED witnesses + Gate A target.

## Gate A

- PENDING — runs after this plan, before `/speckit-tasks` ([const §XVII.1]). Round log appended here on convergence.
