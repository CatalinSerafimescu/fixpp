# Phase 0 — Research: 010-session-cfg-lifetime

**Branch**: `010-session-cfg-lifetime` | **Date**: 2026-05-23 | **Plan**: [plan.md](plan.md)

This slice is a waiver-closure ride-along. It introduces **no new design space** — the binding design is 005 (with 009's drift closures merged). The /speckit-clarify pass resolved the only design-impactful question (FR-001 W-5 mechanism = Option A, by-value). The four entries below pin the remaining **plan-level micro-decisions** that /plan needs to commit to before /tasks generates an actionable backlog. None of them open a Gate A-triggering design surface.

---

## D-1 — By-value `SessionConfig cfg_;` copy cost + copyability

**Decision:** `SessionConfig` is trivially or near-trivially copyable; the ctor copy is acceptable. The `Session(...)` constructor parameter changes from `const SessionConfig&` (still acceptable; copy happens at ctor entry into the member) — keep the by-const-ref signature on the public ctor; the member is by-value.

**Rationale:**
- `SessionConfig` aggregates small POD-ish members per `include/fixpp/session/session_config.hpp`: `session_role` enum, `BeginString` short-string, heartbeat interval (`std::chrono::milliseconds`), `SenderCompID` / `TargetCompID` short strings, `executor_override` (`std::optional<executor_type>`), policy enums (send/recv reset, drain, persistence). No `std::function`, no large `std::vector`, no `std::string` longer than ~16 bytes (FIX comp IDs are ASCII, ≤16 chars by spec).
- Estimated copy size: ≤300 bytes per `SessionConfig`. A copy at ctor is ~50ns on x86_64.
- All members are copyable; the `std::optional<executor_type>` wraps an asio executor handle (an opaque pointer-sized type with cheap copy semantics).
- Reading sites in `src/session/session.cpp` (~40 grep hits for `cfg_.`) all access POD-ish fields; no change required beyond the declaration flip.

**Alternatives considered:**
- `std::shared_ptr<const SessionConfig>` (FR-001 Option B at /clarify) — rejected: introduces heap allocation per session, refcount per session ctor/dtor, and sharing semantics that aren't part of the 005 design. Adds bytes (atomic refcount) and complexity for zero present-day benefit. /clarify explicitly chose against.
- PMR-arena-tracked (FR-001 Option C) — rejected at /clarify; over-engineered for a refactor that's fundamentally "store the config inside Session."
- `[[clang::lifetimebound]]` (FR-001 Option D) — rejected at /clarify; doesn't actually fix the UAF, just narrows where it can occur, and only on Clang.

**Implementation impact:** ~1-line flip in `include/fixpp/session/session.hpp:281`; the ctor body change in `src/session/session.cpp:116` from `cfg_{cfg}` (reference-binding initializer) to `cfg_{cfg}` (copy-construction) — note: the initializer-list syntax is identical, only the declared member type changes. Read sites unchanged.

**Risk:** If `SessionConfig` later grows a non-copyable member (e.g. a `std::unique_ptr` to a session-scoped resource), this decision must be revisited. Today no such member exists. Flag in /speckit-tasks as a /speckit-verify hygiene check: confirm `static_assert(std::is_copy_constructible_v<SessionConfig>);` at the top of `session_config.hpp`.

---

## D-2 — F-04 LogonReceived observability seam mechanism

**Decision:** **`Session` exposes a public `fsm_visit_history() const noexcept` accessor returning `std::span<const fsm_state>` over a fixed-capacity (16-entry) `std::array<fsm_state, 16>` ring-buffer member.** Every assignment to `fsm_state_` in `src/session/session.cpp` (/speckit-analyze verified: **37 sites** spread across `Session::open`, the message-handler `switch` cascade, the close path, and the logout-timer handler) is routed through a new private `record_state_transition_(fsm_state) noexcept` helper that:
1. Writes `fsm_visit_history_[fsm_visit_count_ % 16] = new_state;`
2. Increments `fsm_visit_count_` (saturates at `std::numeric_limits<std::uint8_t>::max()` to avoid wrap-around for the read pattern; in practice the count is bounded by `[FIX-SL §4.10]` cells ≤ 30 per session lifetime).
3. Performs the actual `fsm_state_ = new_state;` assignment.

Tests assert that `LogonReceived` appears in `session.fsm_visit_history()` after the acceptor reply-Logon path runs, without needing the test to observe the FSM mid-transition. The synchronous-transient state is recorded *as it happens* — observable after.

**Rationale:**
- Always-on (no `#ifdef`-gated state), zero observation overhead in production (just one extra store + one extra increment per transition, ~1ns).
- No new sync primitive: the ring buffer is touched only on the per-session strand (the FSM transition site). Reads by tests happen after the operation's awaitable completes, with strand-completion as the happens-before edge.
- Fixed footprint: 17 bytes added to `Session` (16-byte array + 1-byte count, modulo alignment). No heap allocation. Satisfies `[const §VIII.5]`/`[const §XV.1]`.
- Generalizes to F-06 (FSM matrix per-cell witness): every cell needs a way to assert "this transition was taken." The visit history is the natural primitive for both F-04 (transient state observation) and F-06 (matrix coverage).
- The accessor returns `std::span<const fsm_state>` over the first `min(fsm_visit_count_, 16)` entries; the test asserts containment.

**Alternatives considered:**
- **`std::vector<fsm_state>`** populated by `push_back` — allocates on first push; risk of heap traffic on the transition path. Rejected.
- **`std::function<void(fsm_state)>` observer callback** — adds a `std::function` member (~32 bytes; potential SBO) + a branch per transition; ergonomic for tests but more state per Session. The ring buffer's API is simpler. Rejected on simplicity grounds.
- **`#ifdef FIXPP_TEST_HOOKS` compile-time gating** — discouraged (`[[feedback_subagent_phase_verification_two_traps]]` warns against compile-time test-only behavior diverging from production). Rejected.
- **Extending the existing META-attestation seam** (`tests/session/fsm_transition_matrix_test.cpp`) — that file is the EXTERNAL observer, reading `Session::state()` post-hoc. It can't see synchronous-transient states because they're already gone by the time the test reads. The seam mechanism IS the gap; extending it requires adding the ring buffer anyway. Same decision content; framed differently.

**Implementation impact:** ~9 lines added to `include/fixpp/session/session.hpp` (member + accessor); ~15 net lines in `src/session/session.cpp` (helper definition + replacing 10 direct assignments with helper calls).

**Risk:** Ring-buffer wraparound at 16 visits would mask a regression that overruns the buffer. Mitigation: the FIX FSM in `[FIX-SL §4.10]` admits at most ~6 transitions in a normal session lifecycle (open → LogonSent → Active → Logout → Disconnected = 4 visits) and ~10 in adversarial paths (refused-Logon retries, FSM-state-mismatch rejects). 16 is comfortably above the worst case. If the worst-case ever exceeds 16, the test would still see the last 16 — sufficient for any single-scenario witness. Flag as a /speckit-verify hygiene check: assert `fsm_visit_count_ < 16` at end of each FSM matrix witness test.

---

## D-3 — F-07/E1 dedicated error variant name + enum slot

**Decision:** **`error::session_invalid_state_for_send = 77`** in `include/fixpp/core/error.hpp`. Inserted immediately after `session_invalid_config = 76`. Maps to the `FIXPP_ERR_SESSION_REJECT` C-ABI prefix group (see D-3 mapping rationale below).

**Doc-comment template** (paste at the new variant site, following the existing pattern at lines 259-310):
```cpp
session_invalid_state_for_send = 77,       // FR-005 (010), [FIX-SL §4.5.4] — Session::send(...) called
                                            //   while the FSM is not in `Active` (e.g. NotConnected,
                                            //   LogonSent, Disconnected). Replaces the 005-era reuse
                                            //   of `session_invalid_logon` at this site (semantic
                                            //   near-fit; the caller's state mismatch is distinct
                                            //   from a Logon refusal). No reject loop (I-5).
                                            //   → FIXPP_ERR_SESSION_REJECT
```

**Rationale:**
- Slot 77 is the next free slot per `include/fixpp/core/error.hpp` (highest occupied session-class slot is `session_invalid_config = 76` at line 310; the enum is sparse and slot-pinned per `[[project_2e_design_doc_only_seqnum_handoff]]`).
- The name `session_invalid_state_for_send` is internally consistent with the 005 naming pattern (`session_invalid_logon`, `session_invalid_config`, `session_msg_type_invalid_for_state`): noun-led; "invalid_X" form; underscore-separated.
- C-ABI prefix-group mapping: this is a state-mismatch reject for an *outbound* send (caller's mistake), semantically analogous to `session_msg_type_invalid_for_state = 72` which already maps to `FIXPP_ERR_SESSION_REJECT`. Both surface to the integrator as "the session refused to send because of an FSM-state issue." Grouping them under the same C-ABI prefix is correct.

**Alternatives considered:**
- `session_send_invalid_state` (verb-led) — rejected; the 005 enum is noun-led ("X_invalid_Y" or "X_Y_failed").
- `session_send_not_active` — clearer but inconsistent with the "invalid" suffix convention; also too narrow (the variant covers Disconnected and any future non-Active terminal state).
- `session_state_mismatch_for_send` — too long; "invalid_state_for_send" is the natural shortening.
- Adding to `FIXPP_ERR_SESSION_LIFECYCLE` (the prefix group for `session_logout_timeout` etc.) — rejected; lifecycle errors are about session termination, not about a caller's misuse of a non-Active session.

**Implementation impact:** 6 lines added to `include/fixpp/core/error.hpp` (variant + doc comment); 1 line edited in `src/session/session.cpp` (the single `co_return std::unexpected(error::session_invalid_logon);` at line 1151 → `error::session_invalid_state_for_send` — /speckit-analyze verified exactly one site, not two as first-draft estimated); test-side FR-005 AC3 coverage is delivered by the new `session_send_invalid_state_test.cpp` (T013), since /speckit-analyze confirmed no existing test asserts against this specific variant at the send path (existing tests assert `EXPECT_FALSE(has_value())` only).

**Risk:** If any external C-ABI consumer mapped `session_invalid_logon` to a SESSION_REFUSAL handler and now sees a SESSION_REJECT, the rename is observably different. The pre-D-12 C-ABI prefix-group comments in `error.hpp` are *documentation only* — the variant→prefix mapping is implemented at a separate seam (deferred 2i). Today no external consumer depends on it (the C-ABI surface itself is deferred per `[const §X.2]` and 005 plan). When 2i lands and produces the actual mapping table, this slice's prefix-group annotation is the authoritative entry. **No external compatibility break today.**

---

## D-4 — FSM matrix cell count (N events for FR-006)

**Decision:** **N = the union of events in `[FIX-SL §4.10]` per the 005 `data-model.md` + the event alphabet defined in `include/fixpp/session/session_fsm.hpp:52-67`** — i.e. the inbound + outbound admin event types plus the lifecycle events (open / close / timer-fire) handled by the FSM transition function. Concretely, the enumeration comes from the existing `switch (fsm_state_)` cascade in `src/session/session.cpp` (state-arm) × the message-handler dispatch (event-arm). /speckit-analyze verified the event alphabet has **15 named events**: `open_initiator`, `inbound_logon_valid`, `inbound_logon_refused`, `inbound_heartbeat`, `inbound_test_request`, `inbound_reject`, `inbound_logout`, `inbound_out_of_scope_admin`, `seqnum_in_seq`, `seqnum_too_low`, `seqnum_too_high`, `invalid_msgtype`, `timer_tick`, `initiate_logout`, `close_terminal_or_fatal`. Raw matrix ≈ 6 × 15 = **90 cells**; minus the design-forbidden cells (events that the FSM ignores in certain states by spec, per 005 data-model) reduces to **~55-70 reachable cells**.

**Rationale:**
- Pinning the exact enumeration at /tasks-time avoids drift: the matrix witness file lists one `TEST` per cell, with the cell coords (state + event) in the test name (e.g. `TEST(FsmMatrixWitness, LogonSent_HeartbeatIn_IsRejected)`).
- The 005 `data-model.md` is the authority for "design-forbidden vs design-required transition." The 010 matrix witness mirrors the data-model's cells; it does not invent new ones.
- The visit-history accessor from D-2 is the assertion primitive: each cell test drives a state preparation, fires the event, then asserts the visit history matches the expected end-state (or asserts no transition for forbidden cells).

**Alternatives considered:**
- Enumerate ALL combinations (6 × ~all message types ≈ 6 × 30) — rejected; over-broad. The FSM only branches on session-admin events plus lifecycle events; application messages are out-of-band.
- Defer to /speckit-tasks without committing to a count here — rejected; the LoC estimate (~700-900 — updated post-/speckit-analyze to reflect the 15-event alphabet, not the first-draft 8-10) needs an anchored cell count.

**Implementation impact:** Phase 0 closes here; /speckit-tasks generates the per-cell task list by mining the 005 data-model + the existing FSM `switch` cascade.

**Risk:** If /speckit-analyze (the next phase) finds the data-model enumeration disagrees with the implementation's `switch` (i.e. the impl handles an event the data-model doesn't list, or vice versa), that is a 005-drift report and must be raised as a separate finding. This slice does not amend 005's design; it covers only what 005 declared. Cells the impl handles but 005 didn't declare go into a /speckit-analyze finding for a follow-up slice.

---

## Cross-references

- `[[project_005_phase8_completeness_false_pass]]` — completeness-PASS-as-hypothesis burn class; FR-006's per-cell witness file is the structural antidote (one assertion per cell, no inferential coverage).
- `[[feedback_simplify_pass_catches_9th_burn]]` — same; F-06 ride-along is the structural close.
- `[[feedback_codecov_patch_vs_lcov_da_brda_gate]]` + PR #73 precedent — W-1..W-4 carry-forward auto-revisit at /speckit-verify (FR-011); no dedicated tasks here.
- `[[project_release_interop_quickfix_fix8]]` — out of scope for 010; the per-release interop gate is a separate fresh-spec stream.
- `[[feedback_self_run_build_gate]]` — author the new test files, build, run ctest locally before each `/speckit-implement` phase return.
