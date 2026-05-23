# Data Model — 010-session-cfg-lifetime

**Branch**: `010-session-cfg-lifetime` | **Date**: 2026-05-23 | **Plan**: [plan.md](plan.md)

**No new entities.** This slice modifies three existing entities. The 005 binding entity model (`specs/005-session-establishment-fsm/data-model.md`) is **unchanged** — no field is removed, no relationship is severed; the edits below are ownership / observability / error-enum extensions that close the PR #82 Gate B waivers.

---

## E1 — `Session` (modified)

005 anchor: `specs/005-session-establishment-fsm/data-model.md` E1. 009 amended `next_test_request_id_` (per-session) and admin-emit gating; 010 amends ownership of `cfg_` + adds FSM visit observability.

| Field | Before (post-009) | After (010) | FR / D | Rationale |
|---|---|---|---|---|
| `cfg_` | `const SessionConfig& cfg_;` | `SessionConfig cfg_;` | FR-001 / FR-002 / D-1 | By-value member; ctor copies the caller's `SessionConfig`. UAF eliminated. Caller may drop or mutate their `SessionConfig` after the ctor returns; the Session's copy is independent. |
| `fsm_visit_history_` | (absent) | `std::array<fsm_state, 16> fsm_visit_history_{};` | FR-004 / D-2 | New private member. Ring buffer of FSM states visited; written synchronously in `record_state_transition_(new_state)` from every `fsm_state_ = X;` site. Bounded; no heap. |
| `fsm_visit_count_` | (absent) | `std::uint8_t fsm_visit_count_ = 0;` | FR-004 / D-2 | New private member. Count of transitions recorded; saturating at 255 (in practice ≤30 per session per D-2 risk note). |
| `record_state_transition_(fsm_state) noexcept` | (absent) | private method | FR-004 / D-2 | New private helper. Records the transition into the ring buffer, then performs the assignment to `fsm_state_`. Called from every `fsm_state_ = X;` site in `src/session/session.cpp` (~10 sites, line numbers in plan §Scale/Scope). |
| `fsm_visit_history() const noexcept` | (absent) | public accessor returning `std::span<const fsm_state>` | FR-004 / D-2 | New public read-only accessor. Returns a span over the first `min(fsm_visit_count_, 16)` entries. Used by `tests/session/fsm_matrix_witness_test.cpp` (FR-006) and `tests/session/cfg_lifetime_safety_test.cpp` indirectly. |

All other `Session` members and methods are **unchanged**. The 005-defined fields (`engine_`, `fsm_state_`, the seqnum-manager handle, transport handle, etc.) and methods (`open()`, `close()`, `send(...)`, `state()`, ...) keep their signatures and semantics.

**Invariants preserved**:
- I-1 / I-2 / I-3 / I-7 / I-9 / I-15 / I-17 from 005 (per-session strand, FSM legality, noexcept windows) — unchanged.
- New invariant **I-25 (010)**: every assignment to `fsm_state_` in `src/session/session.cpp` MUST go through `record_state_transition_()`. Grep-enforced at `/speckit-verify` (the search pattern is `fsm_state_ =` outside of the helper definition itself).

---

## E2 — `SessionConfig` (verified copyable)

005 anchor: `specs/005-session-establishment-fsm/data-model.md` E2. 009 added the `session_role` field. 010 makes no edit to `SessionConfig` itself — but adds a verification gate.

**Verification gate** (FR-001 D-1 hygiene check): the header `include/fixpp/session/session_config.hpp` MUST contain a top-level `static_assert(std::is_copy_constructible_v<SessionConfig>);` immediately after the class definition. The assert pins the by-value-membership contract: if a future contributor adds a non-copyable member (e.g. a `std::unique_ptr` to a session-scoped resource), the compile breaks and 010's by-value `Session::cfg_` decision is forced into review.

No field added or removed.

---

## E3 — `error` enum (extended)

005 anchor: `specs/005-session-establishment-fsm/data-model.md` E5 + `include/fixpp/core/error.hpp:14`. 009 surfaced no new variants. 010 adds **one variant** at slot 77.

| Variant | Slot | FR / D | C-ABI prefix group | Rationale |
|---|---|---|---|---|
| `session_invalid_state_for_send` | 77 | FR-005 / D-3 | `FIXPP_ERR_SESSION_REJECT` | `Session::send(...)` called while the FSM is not in `Active` (e.g. NotConnected, LogonSent, Disconnected). Replaces the 005-era reuse of `session_invalid_logon` at this site (semantic near-fit). No reject loop (I-5). |

Doc-comment text and exact placement pinned in `research.md` D-3. Insertion is immediately after `session_invalid_config = 76`.

**Invariant preserved**:
- `[const §X.4]` bounded `fixpp_error_t` with forwards-compat — yes, one new slot at the next free slot per the existing append convention.

---

## State transitions

**No FSM state set or transition rule is changed** by 010. The 005 `[FIX-SL §4.10]` state machine is binding; FR-006 (matrix per-cell witness) tests assert the EXISTING transitions, not new ones.

The visit-history ring buffer (FR-004 D-2) is an **observability** addition, not a state-machine addition.

---

## Relationships

- `Session` (E1) **owns** `cfg_: SessionConfig` (was: borrows). Lifetime of `cfg_` = lifetime of `Session`. Caller-side `SessionConfig` lifetime is independent.
- `Session::fsm_visit_history()` (E1 new public method) returns a view into `Session::fsm_visit_history_`. Lifetime of the returned span = lifetime of the `Session`. Tests must not retain the span across a `Session` destruction.
- `error::session_invalid_state_for_send` (E3 new variant) is returned from `Session::send(...)` when `fsm_state_ != fsm_state::Active`. Semantics: caller error; no FSM transition triggered; session remains in its current state.

---

## Cross-references

- `specs/005-session-establishment-fsm/data-model.md` — binding entity model (unchanged).
- `specs/009-session-fsm-finalize/data-model.md` — 009 amendments (unchanged by 010; 010 layers on top).
- `research.md` D-1, D-2, D-3 — pin the implementation specifics for each amendment above.
- `contracts/session_error_state_for_send.hpp` — the lone new shape oracle (the new enum variant + doc).
- `[[project_005_phase8_completeness_false_pass]]` — completeness-burn class context; the visit-history primitive is the structural antidote.
