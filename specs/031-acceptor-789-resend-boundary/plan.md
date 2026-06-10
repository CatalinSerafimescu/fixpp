# Implementation Plan: Acceptor NextExpectedMsgSeqNum(789) Resend-Range Boundary Fix

**Branch**: `031-acceptor-789-resend-boundary` | **Date**: 2026-06-10 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/031-acceptor-789-resend-boundary/spec.md`

## Summary

Conformance bug fix for merged `027` (catalogue **S-031**, `NextExpectedMsgSeqNum(789)`),
found by the `027` SC-005 **live acceptor interop cell** vs QuickFIX-cpp v1.16.0 (the in-process
`027` unit tests miss it; parallels `030`). When fixpp is an **acceptor** with the `789` knob on,
`honor_peer_next_expected_` (`session.cpp:4509`) compares the peer initiator's advertised `789`
against `peek_outbound()` — but on the acceptor arm it is invoked **after** the reply Logon
consumed a sequence number (027 RC#4 ordering, `:2023-2030`), so that value is the **post-reply**
`N_post = N_pre + 1`. For the ordinary in-sync case the peer's initial Logon advertises `789 =
N_pre`, so `N_pre < N_post` fires a spurious `SequenceReset-GapFill` at the seq the reply Logon
already used → duplicate-`MsgSeqNum` → the peer rejects ("MsgSeqNum too low") and the session
fails to establish.

**Fix (technical approach, grounded in both reference engines — research.md R2/R3/R6)**:
parameterize `honor_peer_next_expected_` with a `next_outbound_ref` (the next-outbound the peer's
`789` is compared against), used for **all three** comparisons (too-high / behind / in-sync). The
**acceptor** call site captures the **pre-reply** outbound (`peek_outbound()` before the reply
emit) and passes it; the **initiator** call site passes the current `peek_outbound()` (unchanged —
its peer-reply `789 = target+1` already matches, and the `NE-*-init` cells pass both engines). The
resend **range** stays `[x789, peek_outbound()-1] = [X, N_pre]`, which already matches both engines
(research.md R4) — only the comparison threshold was wrong. Per the /clarify answer
(**comprehensive**), the too-high boundary is corrected too: a peer advertising exactly `N_pre+1`
in its initial Logon is now `X > N_pre` → Logout (today mis-classified in-sync).

## Technical Context

**Language/Version**: C++23 (Clang; asio awaitables, `std::expected`) — [const §II]
**Primary Dependencies**: `Session::honor_peer_next_expected_` (`src/session/session.cpp:4509`) —
signature gains `seqnum_t next_outbound_ref`; the three comparisons (`:4513` `==0`, `:4544` `> n`,
`:4587` `< n`, `:4597` `== n`) switch from the internally-read `peek_outbound()` to
`next_outbound_ref`; the resend range (`:4591` `replay_outbound_range_(x789, peek_outbound()-1,
end_is_through_current=true)`) is **unchanged**. Acceptor call site `:2027` captures
`const seqnum_t n_pre = seqnum_mgr_.peek_outbound();` before the reply Logon `store_then_emit`
(`:2015`) and passes `n_pre`. Initiator call site `:3322` passes `seqnum_mgr_.peek_outbound()`
(unchanged). `seqnum_mgr_.peek_outbound()` (existing), `replay_outbound_range_` (existing, 027),
`recent_events()` (existing, for the discriminating witness). No new deps, no codegen, no wire
field, no new error slot, no config knob, no FSM state.
**Storage**: none touched — this is an in-memory comparison-threshold correction; no `MessageStore`
schema/interface/call change, no persistence change. INV-H1 / 029 hydrate untouched.
**Testing**: GoogleTest; ASan/UBSan/TSan; coverage llvm-cov. New/amended acceptor witnesses
(research.md R8): **W1** in-sync ⇒ zero `SequenceReset`/`ResendRequest` + strict-monotonic
emitted seqnums (currently RED); **W2** genuine-gap ⇒ resend exactly `[X, N_pre]`, no
`ResendRequest` (non-regression); **W3** too-high boundary `X = N_pre+1` ⇒ Logout+disconnect;
**W5** invalid-789 ⇒ Logout (unchanged); **W4** initiator `NE-*-init` non-regression. Live `027`
SC-005 acceptor cell re-run vs QFcpp/QFJ, witness hardened past `drive_to_active`. — [const §VII, §IX]
**Target Platform**: Linux/Clang Tier-1 (sanitizer matrix); the live cell runs vs QFcpp/QFJ in the
parent `phase-9-harness`.
**Project Type**: single C++ library (`fixpp`) + tests + interop-harness extension.
**Performance Goals**: no hot-path change; one extra in-memory counter read/pass on the acceptor
Logon path (cold/rare); default knob-off + initiator paths untouched.
**Constraints**: `noexcept`/`expected_t` preserved; reuses existing `peek_outbound()` /
`replay_outbound_range_` (no new include into the `session.hpp` awaitable closure — [const §XV.9]
N/A, confirm at verify); initiator-role honor and the genuine-gap range **byte-identical**.
**Scale/Scope**: one signature param + one pre-reply capture (acceptor) + switch three comparisons
to the param (~10–14 effective LoC + comments); net-new/amended regression tests in
`tests/session/test_next_expected_msgseqnum.cpp`; a small blast-radius pin set (R7) confirmed at
/analyze. No FSM state, store, config, codegen, C-ABI, or wire change.

## Constitution Check

*GATE: must pass before Phase 0 (passed) and re-checked after Phase 1.*

| Article | Gate | Status |
|---------|------|--------|
| **II** Language | C++23/Clang, no new deps | ✅ PASS |
| **VI** Spec coverage | **No net-new catalogue row** — corrects behavior owned by `027` under existing row **S-031** (`NextExpectedMsgSeqNum`). Amend S-031 Notes to cite `031` as the acceptor-honor conformance correction (compare peer 789 vs pre-reply outbound; in-sync ⇒ no resend; too-high boundary at N_pre). `spec.md` carries **Normative References** (`[FIX-SL §4.4.1]`) per §VI.5. Exact §VI delta below (Polish). | ⚠ RESOLVED (delta specified) |
| **VII** Testing/TDD | RED-first: W1 (in-sync ⇒ no spurious resend + strict-monotonic seqnums, currently RED); W2 (genuine-gap range `[X,N_pre]`, non-regression); W3 (too-high `X=N_pre+1` ⇒ Logout); W5 (invalid-789 Logout unchanged); W4 (initiator non-regression). | ✅ planned |
| **VII.6** Interop | live acceptor cell `NE-*-acc`: peer 789-enabled initiator (no 141=Y) ⇒ session establishes, **zero** fixpp `SequenceReset`/`ResendRequest`, peer does not Logout-reject (SC-004, the true close-out) | ✅ planned |
| **VIII.5** Allocator | comparison-threshold correction; no new container/frame/allocation; existing no-heap witnesses on the 789 path stay green | ✅ PASS (no new alloc) |
| **IX.1** Coverage | ≥95/85 on the parameterized comparison branches (too-high / behind / in-sync) on BOTH call sites; the acceptor in-sync `X==N_pre` (no-resend) and `X=N_pre+1` (too-high) arms newly covered | ✅ planned |
| **IX.2** Sanitizers | ASan/UBSan/TSan on the acceptor 789-honor path + amended/new tests + interop ctest | ✅ planned |
| **X** ABI | no C-ABI/error-slot/wire change; internal honor signature gains a param (source rebuild only) | ✅ source rebuild (no surface change) |
| **XI.4** Threading | the capture + honor run on the existing session strand inside the inbound Logon handler; no new concurrency surface | ✅ PASS |
| **XII.5** No-implicit-default | **no new config flag** — gated only by the pre-existing `enable_next_expected_msg_seq_num` knob + `next_expected_present`; the threshold is derived from the existing outbound counter at the existing call sites | ✅ PASS (no new knob) |
| **XIV.2** Pluggable ≤5 pure-virtual | no interface touched (`MessageStore` untouched) | ✅ PASS |
| **XV.9** Banned (`std::mutex` in awaitable hdr) | no new include; `honor_peer_next_expected_` already in the awaitable corpus | ✅ N/A (confirm at verify) |
| **XVI.3/4** /clarify before /plan | Session 2026-06-10 — one user decision recorded (fix scope → comprehensive); the conformance threshold grounded by the QFcpp/QFJ source sweep (research.md R2/R3) | ✅ PASS |
| **XVII.1** Gate A before /tasks | mandatory — runs after this plan, before `/speckit-tasks` | ⏳ PENDING (Gate A next) |

**Result**: PASS to proceed. No new config/ABI/wire/interface/store surface; the genuine-gap range
and the initiator path are byte-identical (research.md R4/R5); only the acceptor comparison
threshold is corrected (and, per /clarify, the too-high boundary with it). One decision was flagged
for Gate A below.

### Flagged for Gate A

1. **Comparison reference — role-aware param vs blanket `peek_outbound()-1`.** The fix passes a
   `next_outbound_ref`: pre-reply outbound on the acceptor arm, current `peek_outbound()` on the
   initiator arm. A blanket `peek_outbound()-1` inside honor is **rejected** — it would break the
   initiator (its in-sync `X = N_post` would become `X > N_pre = N_post-1` → spurious Logout;
   research.md R5). Gate A to confirm the param shape and that the initiator arm stays byte-identical
   (the `NE-*-init` cells + unit pins are the witnesses).
2. **Too-high boundary scope (comprehensive).** Per /clarify, the too-high comparison also uses
   `next_outbound_ref`, so a peer advertising `X = N_pre+1` in its initial Logon flips
   in-sync→Logout. Gate A to confirm this matches both engines (QFcpp `:230` `> getExpectedSenderNum()`
   pre-reply; QFJ `:2255` `> actualNextNum` pre-reply — yes) and to enumerate any `027` X>N negative
   pin that sits at the shifted boundary (R7 item 2).
3. **Blast-radius pin set (R7).** Enumerate exactly at `/speckit-tasks`/`/analyze` which
   `test_next_expected_msgseqnum.cpp` pins assert the acceptor honor boundary and which (if any) must
   be amended vs are net-new. Hypothesis: the in-sync-at-`N_pre` boundary had **no** pin (the bug
   shipped because nothing hit it live); the genuine-gap range pin and the initiator pins are
   unchanged. Confirm — do not assume.

**Exact §VI delta (applied at Polish):**
- `spec/feature-catalogue.md`: amend the **S-031** row (`NextExpectedMsgSeqNum`) Notes to cite
  `031-acceptor-789-resend-boundary` as the acceptor-honor conformance correction (peer 789 compared
  vs the **pre-reply** outbound; in-sync `X==N_pre` ⇒ no resend; too-high boundary at `N_pre`; the
  genuine-gap range `[X, N_pre]` and the initiator honor unchanged). No new S-row.
- `spec/coverage-index.md`: map the parameterized comparison branches ↔
  `tests/session/test_next_expected_msgseqnum.cpp` (W1/W2/W3/W5) under the existing 027 entry.
- `spec/behaviors-and-limitations.md`: add **B-031-1** (acceptor 789 honor: the peer's advertised
  next-expected is compared against fixpp's pre-reply next-outbound; an in-sync peer triggers no
  resend and the session establishes with no duplicate-seq frame; reference-engine-conformant).
- **Obsolete-prose grep-sweep** (one exhaustive pass, [[feedback_verify_caught_design_pivot_stale_doc_bundle_drift]]):
  amend any 027 comment/doc that frames the acceptor honor as comparing against the post-reply
  outbound or that implies the in-sync case resends. Needles: `peek_outbound()` near the honor doc,
  `RC#4`/`AFTER reply store_then_emit` honor comments (`session.cpp:2023-2030`, `:4495-4512`), and any
  S-031 / B-0xx prose asserting the post-reply comparison.

## Project Structure

### Documentation (this feature)

```text
specs/031-acceptor-789-resend-boundary/
├── plan.md              # this file
├── spec.md              # /speckit-specify + /speckit-clarify output
├── research.md          # Phase 0 — oracle + endpoint resolution + role asymmetry
├── data-model.md        # Phase 1 — the comparison-reference value + honor states
├── quickstart.md        # Phase 1 — how to reproduce + validate
├── contracts/
│   └── honor-next-expected.md   # honor_peer_next_expected_ signature + semantics contract
├── checklists/
│   └── requirements.md  # spec-quality checklist (GREEN)
└── tasks.md             # Phase 2 — /speckit-tasks output (NOT created here)
```

### Source Code (repository root = library submodule)

```text
src/session/session.cpp        # honor_peer_next_expected_ (:4509) + acceptor (:2027) / initiator (:3322) call sites
include/fixpp/session/session.hpp   # honor_peer_next_expected_ declaration (new param)
tests/session/test_next_expected_msgseqnum.cpp   # W1/W2/W3/W5 + initiator non-regression
tests/interop/happy/hp_fix44_next_expected_test.cpp   # NE-*-acc witness hardening (live close-out)
```

**Structure Decision**: in-place fix on the existing `027` session 789-honor path; no new modules,
files-of-record only as listed. Tests extend the existing `027` unit + interop suites.

## Complexity Tracking

> No constitution violations. The single design choice (role-aware comparison reference vs blanket
> `-1`) is documented in research.md R5/R6 and flagged for Gate A; it does not introduce a new
> abstraction — it threads one existing value through an existing function via a parameter.

## Gate A

*(Runs after this plan, before `/speckit-tasks` — [const §XVII.1]. Record the convergence + sign-off here.)*

- Status: ⏳ PENDING
