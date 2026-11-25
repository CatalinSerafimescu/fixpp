# Implementation Plan: Logon guards before any answer, a time limit on every write, a configurable initiator handshake bound, and the msvc-asan job cap

**Branch**: `106-logon-guards-write-bounds` | **Date**: 2026-11-25 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `/specs/106-logon-guards-write-bounds/spec.md`, with its three Clarification
sessions of 2026-11-25 (owner rulings: the scope, #717's two parts, #714 option (b), C++ and TOML only; and from
`/speckit-clarify`: an immediate close at the write limit, every Logon refusal keeps NextNumIn, a handshake key in both
roles). **Base**: `origin/main` `9d81082a`, where the source was read. Decisions D1 to D11 are in
[research.md](research.md), the state in [data-model.md](data-model.md), the observable rules in
[contracts/logon-guards-write-bounds-106.md](contracts/logon-guards-write-bounds-106.md) (C-1 to C-9), and validation in
[quickstart.md](quickstart.md).

## Summary

Batches B129 (#724), B110 (#702, #703), B124 (#714) and B123 (#717), in one PR.

1. **B129 is done (D10).** Commits `febad771` and `aea0d5a5` give the `windows-msvc-asan` leg its own cap, with a
   whole-job re-derive recipe in the workflow comment. This PR's own Tier 2 runs are its acceptance.
2. **The identity gates first (#702; D1).** In the acceptor's NotConnected arm and the initiator's LogonSent arm,
   the gates run right after `interpret_logon`, before hydration and before every refusal that sends or changes
   sequence state:
   - the mTLS CompID gate, in both arms;
   - the credentials gate, on the acceptor.

   The validate-first Reject, which runs before `interpret_logon`, is sent only after an identity check (consuming
   when the Reject disconnects, N-13.2). It fails closed on unreadable credentials when a validator is installed, through the new
   `CompIdAuthorizationPolicy::has_logon_validator()`. A stay-in-arm Reject therefore does not consume the one-shot
   identity. The later gate calls, including 104's too-low branches' own, are deleted. A direct mTLS initiator
   started by `open()` has no peer identity, so the gate moves up in its arm; authorising at `open()` is not
   possible.
3. **NextNumIn consumed only on success (#703 and the class ruling; D2).** `SeqnumManager::classify_inbound`
   classifies without advancing. The advance is committed by `check_inbound` at one point per arm: on the acceptor
   before `LogonReceived`, for the reply's 789; on the initiator before `Active`, skipped when a 141=Y unit wrote
   absolute targets. Every refusal between the two points keeps NextNumIn. Research D2 tabulates them.
4. **The give-up never waits behind a write (#714a; D3, D4).** While the liveness loop is suspended in its own
   TestRequest or Heartbeat emit, the per-connection **connection bound** waits on its own timer to the give-up
   instant. At that instant it decides the give-up through the loop's own extracted decision, `decide_give_up_()`:
   - the ending claim (D11);
   - the slot-74 record;
   - the Logout owner;
   - 105's ending write machine, whose budget starts at the decision.

   The loop writes the Logout when its emit returns, or 105's limit closes the transport at the decision plus
   `logout_disconnect_timeout_ms`, which fails every blocked write.
   - An emit that has not returned is given up at L + 3T (ruling R1).
   - The 3T held cap applies with no TestRequest pending too, per the owner's T119b text.
5. **The opt-in write limit (#714b; D5).** `write_timeout_ms` (C++ and TOML, 0 = off, no C setter) limits every
   non-Logout live write, counted from its `async_write` call.
   - The same connection bound does the timing.
   - At expiry there is no Logout: the claim, a warn record, `Disconnected`, then the close.
   - The cut write returns the new `session_write_timeout` (136, C mapping `FIXPP_ERR_TRANSPORT_IO`), passed through
     `store_then_emit` unflattened.
5a. **The timers and the ending (rulings R2, R3; D3, D11).**
   - On `system_clock_source` the connection bound never calls `Clock::sleep_until`. Its wait is its own
     `asio::steady_timer`, so it allocates nothing per write or per emit, and it never resets another sleeper (#715
     stays out of scope).
   - On `mock_clock` it waits through the clock, which keeps the cells deterministic.
   - One claim per connection: one `Disconnected` per ending, a reason record only from a recording decider (FR-017).
6. **Handshake bounds (#717; D6).**
   - Initiator: TOML `[session.transport].tls_handshake_timeout_ms` feeds the factory's
     `Transport::Config::tls_handshake_timeout` (the C++ half already exists), with the minting loop's divergence
     test widened.
   - Acceptor: `EngineConfig::acceptor_tls_handshake_timeout` (default 1500 ms) and a root-scope TOML key replace the
     listener's literal.
   - A session-level cell proves the bounded attempt.
7. **Surfaces (D7).** C-ABI **1.19, BREAKING, behaviour only**. C++ layout of `Session`, `SessionConfig` and
   `EngineConfig`. Two member functions added and one error enumerator appended.

## Technical Context

**Language/Version**: C++23 (Clang 22 local and CI; GCC release sanity; MSVC Tier 2).

**Primary Dependencies**: standalone Asio (coroutines, `steady_timer`, `awaitable_operators`), OpenSSL (TLS
transport), toml++ (the loader), GoogleTest, Google Benchmark.

**Storage**: `MessageStore` (memory and file). D2's commit keeps the persist sites and their guards unchanged.

**Testing**: GoogleTest cells on the mock clock with `BlockingWriteTransport` (D2 to D5, D11). The 104 Logon rigs (D1,
D2). `system_clock_source` for the two shipping-clock cells (R2). Real TLS sockets for D6. Cells the base cannot compile
go in the seam file (D9). **Scope (owner, 2026-11-25):** targeted runs only, by label or binary on each lane, plus ONE
full `ctest` on `linux-clang-debug` (quickstart §5).

**Target Platform**: Linux (Tier 1), Windows MSVC (Tier 2: the new test targets built with `-k 0` before the first
labelled push).

**Project Type**: library (fixpp session layer, config loader, C ABI).

**Performance Goals**: no slowdown beyond +5 % on the paired `engine_send_bench` (a CI `paired` row), and locally on
`session_send_bench` and `on_inbound_frame_bench` (Article VIII §2).

**Constraints**:
- No allocation per write or per liveness emit, with the limit on or off, on `system_clock_source` (D3, D5; the
  mallocnesia cell).
- One `co_spawn` per connection, the connection bound (D3), and none per write or emit.
- No new call of `Clock::sleep_until` on the shipping clock beside another sleeper of the Session (#715).
- No new local held across an await in the Logon arms (104 T093).
- No new mutex shape, and no new `replay_gate` use (`[const §XI.3]`).

**Scale/Scope**: `src/session/session.cpp` (both Logon arms, the liveness loop, `live_write_serialized_`);
`seqnum_manager`; `compid_authorization_policy`; `engine.cpp` (one literal); `src/config` (three keys); the error
enum and C-ABI map; `version.h` and `session.h` comments; new `tests/session/test_106_*.cpp` and config cells.

## Constitution Check

*GATE: before Phase 0 research, and again after the Phase 1 design.*

### Pre-research (against the spec)

| Article | Requirement | Status |
|---|---|---|
| XVI §3 | `/clarify` before `/plan` (session FSM, security, error semantics, ABI) | PASS: three sessions recorded in spec.md |
| XII §9, XI §7, X §6 | the four controls (`/clarify`, `/analyze`, Gate A, user `/plan` sign-off) | OWED: `/analyze` after `/tasks`; Gate A on this bundle; the user's `/plan` sign-off, which the orchestrator records (the owner said they would pre-approve the plan; this bundle does not satisfy the control) |
| XVII §1 | Gate A triggers: the session FSM, concurrency (new coroutines), the security surface (identity gates), the C ABI (behaviour) | Gate A REQUIRED |
| X §7 | a breaking C-ABI change before the first release is declared | PLANNED: 1.19 BREAKING (D7); the predicate is `gh release list --exclude-drafts` |
| IV §1–2 | the C++ library is primary; no new C symbol (owner) | PASS |

### Post-design (against research, data-model and the contract)

| Article | Clause | Design's answer |
|---|---|---|
| IV | C ABI is the licence seam, not the primary API | No new C symbol, constant or type. The three knobs are C++/TOML only (F-7: no C TOML entry). Python binding: version pins only (D7 census). |
| VIII §2–3, §5 | +5 % paired budget; a bench in the same PR; zero allocation between parse and `fromApp`, which 2d extends to the heartbeat path | D1/D2 touch the establishment path only. D5 adds one branch per write (off). D3 allocates nothing per emit: one bound per connection (research D3). The mallocnesia cell binds it. The paired runs are owed (quickstart §6). |
| IX §1 | diff-scoped coverage 95/85, no uncovered error path | Each new error path has a cell in research D9 (its "has a cell" census): the commit refusal (`CommitRefused_Disconnected`); the bound's spawn failure at the loop and at a write; its wait failure; its teardown exits; the claim's race pairs; the TOML refusals, the root key's included. The verify record assesses the rest. |
| IX §2 | ASan, UBSan, TSan | targeted runs; TSan repeats the D3/D5 cells (quickstart §5) |
| X §1, §4 | the versioned contract; append-only error codes | `session_write_timeout = 136` appended; maps to the existing `FIXPP_ERR_TRANSPORT_IO`; no numeric reuse; occupancy unchanged |
| X §7 | BREAKING declared in `version.h`, the PR, B&L; in-repo consumers updated | D7: the `version.h` 1.19 entry, three `session.h` clauses, the MINOR-pin census, B-106-6 |
| XI §1–3 | coroutines; native cancellation; `async_mutex` or the one `replay_gate` exception | The connection bound waits on its own `steady_timer`, which the strand cancels to wake it (105's wake idiom). It takes no mutex and binds no cancellation slot, and makes no new gate use. The emit keeps its existing shared hold. |
| XI §4 | per-session strand | every new member is strand-confined; the join counters follow `liveness_counter_`'s shape |
| XII §5, §9 | explicit profiles; the security controls | D1 changes no TLS mechanism, only the order of the identity gates. FIX-TC 1S(c) (research D1). The four controls apply. |
| XIII §2 | async logging | two warn records through the session logger (one new), registered in `format_registry.cpp` |
| XIV §2 | pluggable interfaces ≤ 5 methods | no interface change: `TransportFactory` untouched (D6 rejects a per-session `make` argument) |
| XV §1 | no per-message heap allocation on the hot path | None per write or per emit on `system_clock_source` (the mallocnesia cell, known positives M5 and M8). One spawn per connection. |
| XV §9, §15 | no `std::mutex` in coroutines; no application-message drop | none added; D5 closes the session rather than dropping a frame (the disconnect-and-recover posture) |
| XV §16 | QuickFIX CFG or TOML | TOML keys only; the QuickFIX CFG translation is unaffected |
| XVI §6 | the orchestrator does not implement | this bundle is docs only; the implementer writes code and tests |
| XVII §2, §8 | Gate B on every PR; `/speckit-verify` after `/implement` | planned |
| XIX §5 | API pages regenerate | Doxygen of `session.h`, `session_config.hpp` and `engine_config.hpp` regenerate. No `docs/` page names `logout_disconnect_timeout_ms` today (re-derive: `git grep -n logout_disconnect_timeout_ms docs examples`), so no page is owed for the new keys. |

No violation. Complexity Tracking records the two structural additions anyway.

## Project Structure

### Documentation (this feature)

```text
specs/106-logon-guards-write-bounds/
├── spec.md
├── plan.md              # this file
├── research.md          # D1–D10, findings F-1–F-9, supersession, risks
├── data-model.md        # E-1–E-9
├── quickstart.md        # censuses, RED runs, mutants, sanitizers, benches
├── contracts/
│   └── logon-guards-write-bounds-106.md   # C-1–C-9
├── checklists/requirements.md
└── tasks.md             # /speckit-tasks (not created here)
```

### Source Code (repository root)

```text
include/fixpp/core/error.hpp                      # session_write_timeout = 136; error_message
include/fixpp/core/engine_config.hpp              # EngineConfig::acceptor_tls_handshake_timeout
include/fixpp/config/config_bundle.hpp            # EngineEstablishment::acceptor_tls_handshake_timeout (C-2)
include/fixpp/session/session.hpp                 # E-3, E-5 members; write_class; store_then_emit's argument; new private callees
include/fixpp/session/session_config.hpp          # SessionConfig::write_timeout_ms
include/fixpp/session/seqnum_manager.hpp          # classify_inbound
include/fixpp/session/compid_authorization_policy.hpp  # has_logon_validator
src/session/session.cpp                           # D1, D2 (both arms), D3/D4 (liveness loop, connection bound, decide_give_up_), D5 (live_write_serialized_, store_then_emit, Session::send), D11 (claim; the Disconnected census)
src/session/seqnum_manager.cpp                    # classify_inbound
src/session/compid_authorization_policy.cpp       # has_logon_validator
src/session/engine.cpp                            # listener bound from EngineConfig; the establishment expiry takes the claim
src/config/scalar_mappers.cpp                     # write_timeout_ms
src/config/toml_config_loader.cpp                 # kRecognized; the session-scope refusal of the acceptor key
src/config/selector_resolver.cpp                  # tls_handshake_timeout_ms → Transport::Config; divergence; the root acceptor key → EngineEstablishment
src/capi/error.cpp                                # translate session_write_timeout
src/log/format_registry.cpp                       # the write-timeout record
include/fix/c_api/version.h                       # MINOR 19 + 1.19 history (comment)
include/fix/c_api/session.h                       # 1.19 clauses (comments)
tests/session/test_106_identity_first.cpp         # IdentityFirst106, NextNumIn106 (all but CommitRefused_Disconnected)
tests/session/test_106_give_up.cpp                # GiveUp106
tests/session/test_106_seams.cpp                  # every cell naming a symbol the base lacks: WriteTimeout106, OneEnding106 (limit set), ConnectionBound106, ShippingClock106, both EngineHandshake106 cells, NextNumIn106.CommitRefused_Disconnected, TomlWriteTimeout106.Set
tests/session/test_106_one_ending.cpp             # OneEnding106 pairs that need no new symbol
tests/session/test_106_handshake.cpp              # HandshakeBound106 (real TLS)
tests/alloc_guard/test_106_connection_bound.cpp   # alloc_guard_106_connection_bound_mallocnesia (Release)
src/session/session_engine_access.hpp             # claim_ending (the establishment expiry)
tests/config/…                                    # TomlWriteTimeout106.{OutOfRange, WrongType}, TomlHandshake106 (the config suite's file)
tests/support/session_test_access.hpp             # the hooks: CommitRefused's freeze, connection_bound_spawn_fails_, connection_bound_wait_throws_
tests/capi/expected_error_map.csv                 # + session_write_timeout row
tests/session/CMakeLists.txt                      # the 106 binaries/labels
spec/behaviors-and-limitations.md (+ -closed.md)  # D8
```

**Structure Decision**: the existing single-library layout. The new cells go in new 106 files, as 104 and 105 did.
Only the cells that name no new symbol build against the base. Every other cell is in `test_106_seams.cpp` (ctest
`session_106_seam_tests`, labels `106` and `106_seam`). Base readings exclude it with `-LE 106_seam`, and each of its
cells shows RED on a named mutant at the head (research D9, 105's rule). Cells that change existing files (the census
of D9) stay where they are.

## Seams touched (Gate A root cause #3; re-derive each row from the source)

| New observable | Existing seam it crosses | Edit | Cell |
|---|---|---|---|
| the cut write's `session_write_timeout` | `live_write_serialized_` and `store_then_emit` flatten to `dispatch_aborted`; `Session::send`'s F9 block keys on it | pass through; census of `dispatch_aborted` branches (research D5) | `WriteTimeout106.<Class>` |
| one `Disconnected` per ending | unconditional `record_state_transition_(Disconnected)` sites | `record_disconnected_unless_already_()` at each reachable one (D11 census) | `OneEnding106.*` |
| the bound's waits | `Clock::sleep_until` (#715, `L-093-13`) | not crossed on `system_clock_source` (D3) | `ShippingClock106.*`, the mallocnesia cell |
| `drive_reconnect`'s return | `ReconnectFsm::drive_reconnect_attempt` returns `transport_reconnect_limit_exceeded` | none: the requirement follows the FSM (R4) | `HandshakeBound106.*` |
| the TOML handshake key | `asio_tls_transport_factory::cfg_` is private | none: observed through `make()` and `async_handshake` (D6) | `TomlHandshake106.*` |
| the root handshake key | `EngineEstablishment` has no field; hosts copy it field by field into `EngineConfig` | the new field; the copy-site census (D6) | `EngineHandshake106.Acceptor_ClosesAtTheKey` |
| the bound after a cancelled drain | `drive_reconnect`'s failed-drain return (`!drained`), which today only returns | the respawn of data-model E-3, after clearing `reconnect_drain_in_flight_` | `ConnectionBound106.DrainCancelled_RespawnsOnBlockedWrite` |
| new symbols in cells | the base build | the seam file (D9) | every [seam] cell |

## Phases (for /speckit-tasks; order by blocking, owner 2026-11-25)

0. **Setup.** The censuses of quickstart §1. The base worktree on `fixppbuild`. The RED-on-base harness for the 106
   binaries.
1. **P1 — B110 / #702 (security, first).** D1 in both arms. `has_logon_validator`. Delete the later gate calls.
   Cells: `IdentityFirst106.*` RED → GREEN; M1; the 104 and 099 suites unfiltered. FR-007 update (N-3; research D9's
   census): `LogonTooLow104.Precedence_MtlsAcceptorOffList`'s setup assertion becomes `EXPECT_EQ(r.next_in(), <the
   value read before the feed>)`, because the refusal precedes hydration (D1 item 4).
2. **P2 — #703 and the class ruling.** D2: `classify_inbound`, the two commit points. Cells: `NextNumIn106.*`; the
   census rows updated; M6.
3. **P3 — #714a (D3, D4, D11).**
   - Code: the claim and the `Disconnected` census; extract `decide_give_up_()`; the connection bound with its own
     timer; the no-pending held cap.
   - Cells: `GiveUp106.*` with M2's spurious-hit arms; `ConnectionBound106.*`; `ShippingClock106.*`;
     `OneEnding106.*` without the limit.
   - Guards: the 105 and 538 suites, by label.
4. **P4 — #714b (D5).**
   - Code: the knob and the TOML key; `write_class`; the bound's write arming; `session_write_timeout`, passed
     through `store_then_emit`, with its C mapping; the record.
   - Cells: `WriteTimeout106.*`; `OneEnding106.*` with the limit; `TomlWriteTimeout106.*`; the C-ABI map; the
     mallocnesia cell with the limit off and on.
   - Mutants: M3 to M5, M8, M9 with M9′, and M10.
5. **P5 — #717 (D6).** The TOML session key, divergence, and the `EngineConfig` field and root key. Cells:
   `TomlHandshake106.*`, `EngineHandshake106.*`, `HandshakeBound106.*`; M7, M7′ to M7‴.
6. **P6 — Surfaces and records.** The `version.h` 1.19 entry and the `session.h` clauses. The B&L delta (D8). The
   supersession comments (research table). The brain page `brain/components/session.md` gains a 106 section (why and
   what was rejected) in the PR before CI (the late-docs rule).
7. **P7 — Polish.** `/simplify`; the paired benches; targeted sanitizers; MSVC `-k 0` of the new targets;
   `/speckit-verify`, whose plan is targeted only, plus ONE full `ctest` on `linux-clang-debug` (owner, 2026-11-25).

B129 needs no task beyond recording the PR's asan leg outcome (SC-006).

## Complexity Tracking

| Addition | Why needed | Simpler alternative rejected because |
|---|---|---|
| One per-connection bound with its own timer, which decides the give-up and times the write limit (D3, D5) | the loop is suspended inside its own emit, and only a transport close ends a blocked write, so something else must act at the instant, with nothing allocated per write or emit and no shared timer slot | an in-loop race cancels `store_then_emit`; a spawn per emit or per write allocates on the heartbeat and send paths; `Clock::sleep_until` allocates per call on Engine sessions and resets the Session's one slot timer on direct drivers (#715); fixing #715 in the clock leaves both allocations and needs a sixth `Clock` virtual |
| One ending claim (D11) | five deciders could each record a reason | per-decider predicates wrote two reasons (Gate A C-3) |
| A non-consuming identity precheck (D1) | the stay-in-arm validate-first Reject must not consume the one-shot identity (F-2) | a consuming gate there fails the next Logon closed (FR-005) |

## Open questions for Gate A (only where the code cannot settle them; resolved, see the Resolutions and Gate A below)

- **Q1 — the credentials accessor.** Accept `CompIdAuthorizationPolicy::has_logon_validator()` for D1's fail-closed
  rule, over empty credentials, which the validator cannot tell from unreadable ones?
- **Q2 — the Text of a give-up with no TestRequest written** (a blocked Heartbeat; a hold with none pending at the 3T
  cap): Text(58) `TestRequest unanswered` without the TestReqID clause, or no Logout, as QuickFIX C++?
- **Q3 — refusals after a reset that already ran** (the acceptor's `reset_on_logon` reset, then a too-high or S3 refusal;
  an initiator's 789 refusal after a 141=Y unit, `UnaskedResetAck104.With789_TooHighAfterReset_Logout`): deferring the
  resets moves 024's, 093's and 104's positions, and moving S3 first breaks FR-005. Keep and disclose (`L-106-a`)?
- **Q4 — `write_timeout_ms`'s wrong type.** `logout_disconnect_timeout_ms`'s mapper ignores a non-integer silently.
  Refuse with `malformed_value` (`logon_timeout_ms`'s shape)?
- **Q5 — D3's 3T cap with none pending**, the owner's T119b text over 105's reading, which capped only with one pending.

**Resolutions (orchestrator, 2026-11-25).** The owner pre-approved the plan and delegated these calls. Gate A may reopen any of them.
- Q1: accept `has_logon_validator()`.
- Q2: the Logout carries Text(58) `TestRequest unanswered` without the TestReqID clause, so the ending stays one shape with 105's R-1.
- Q3: keep the resets where they are and disclose them as L-106-a. A configured `reset_on_logon` reset and a 141=Y reset unit run only for an identity the gates admit (D1), and QuickFIX C++ resets at the same place. The spec's FR-006 is narrowed to match.
- Q4: refuse a wrong-typed value with `malformed_value`.
- Q5: confirmed. Without the cap with none pending, a resend walk whose write blocks holds the liveness loop for ever (US3 scenario 3).

Spec findings applied with these resolutions: FR-006 / SC-002 / US2 scenario 4 narrowed for Q3; US1 scenarios 4 and 6
for a direct `open()` initiator (F-3); the F-9 edge case; Q4.

## Gate A

- Round 1 applied 2026-11-25: Codex P1=2 P2=5 P3=2; Opus post-judging P1=2 P2=11 P3=9; rewrite addresses root causes
  #1 (the clock that ships), #2 (one ending claim), #3 (the seams touched), #4 (the reconciliation after Q1–Q5).
  Reviews: `research/G19-fix-fpml-iso20022/research/reviews/codex_106-logon-guards-write-bounds_gate_a_review.md`,
  `research/G19-fix-fpml-iso20022/research/reviews/opus_106-logon-guards-write-bounds_gate_a_adversarial_review.md`.
- Round 2 applied 2026-11-26: Codex P1=1 P2=4 P3=0; Opus post-judging P1=1 P2=4 P3=10; rewrite addresses root causes
  #1 (the invariant re-derived from D11's table), #2 (deciders the code serialises), #3 (the censuses run), #4 (each
  new surface traced to its consumer). Reviews:
  `research/G19-fix-fpml-iso20022/research/reviews/codex_106-logon-guards-write-bounds_gate_a_2_review.md`,
  `research/G19-fix-fpml-iso20022/research/reviews/opus_106-logon-guards-write-bounds_gate_a_2_adversarial_review.md`.
- Loop 1 exhausted at round 3 (Codex P1=1 P2=1 P3=0; Opus P1=1 P2=3 P3=2). Step F (orchestrator, owner-delegated 2026-11-26): targeted edit applying round 3's exact text; loop 2 follows. Reviews: `research/G19-fix-fpml-iso20022/research/reviews/codex_106-logon-guards-write-bounds_gate_a_3_review.md`,
  `research/G19-fix-fpml-iso20022/research/reviews/opus_106-logon-guards-write-bounds_gate_a_3_adversarial_review.md`.

- Loop 2 round 1 applied 2026-11-26: Codex P1=2 P2=1 P3=1; Opus post-judging P1=1 P2=3 P3=6; rewrite addresses root causes
  #1 (the identity matrix derived per gate, from where each gate applies) and #2 (the drain cells scripted from
  `drain_for_reconnect_`'s step order). Reviews:
  `research/G19-fix-fpml-iso20022/research/reviews/codex_106-logon-guards-write-bounds_gate_a_4_review.md`,
  `research/G19-fix-fpml-iso20022/research/reviews/opus_106-logon-guards-write-bounds_gate_a_4_adversarial_review.md`.
  Every P3 is applied in the bundle; none is deferred to tasks.md.

### Round 1 — disagreements

None marked Disagree, so no Codex fix was refused. Three were applied as Opus corrected them, not as written:
- C-1: Codex's first option (preserve the last attempt's error in the reconnect FSM) is not taken (ruling R4).
- C-2: Codex's merged "1B/1S" key is not a coverage-index key; the two exact keys are listed.
- C-5: 544's counter is not a PMR counter; the binding witness is a mallocnesia cell in `tests/alloc_guard/`.

### Round 2 — disagreements

None marked Disagree. Applied otherwise than written:
- Codex C-1's first option (`close()` and the bound's failure exits as recording claimants) is not taken (R6). Codex
  C-5, a P3 after Opus: no C-ABI trace test, since the C path is the same Session.
- Opus C-2's recipe (`bundle.engine.clock`) misses `test_099_toml_live.cpp`'s `loaded->engine.clock`; D6's finds both.
  Opus C-3's rule would demand cells for by-design rows; those rows of D2's 106 column now read "as the base".
- Opus N-1: the disclosure is the new `L-106-f`, and the pre-Active cell's mutant is M10 (M9 alone never reaches the
  establishment expiry). N-5's row is initiator-only: the acceptor's superseded returns follow its commit.

### Loop 2 round 1 — disagreements

Two Codex fixes refused, as Opus judged:
- Codex C-2's oracle ("the hydrated manager equals the stored value") is not taken: with a peer 141=Y the acceptor
  withholds the inbound seed, so the manager reads 1 while the store holds N. FR-006 now defines "unchanged" against the
  value the arm classified the frame against.
- Codex C-3's counter-proposal (delete `WriteExpiryDuringDrain_NoRecord`; a seam calling `claim_ending_` directly) is not
  taken: a drain from LogonSent with a Logon-arm Reject's write blocked parks at step 3 with the bound alive, and that
  window tests the bound's exit term, which a seam probe would not.

Applied otherwise than written:
- Opus N-6: its text ("an arming that finds it not running spawns it again") never fires in that state, because later
  writes queue at `write_gate_` and the liveness loop starts only at Active. The respawn is placed at
  `drive_reconnect`'s failed-drain return (data-model E-3), with the cell `DrainCancelled_RespawnsOnBlockedWrite` and
  its mutant.
- Opus C-3's rig sets `logout_disconnect_timeout_ms` 1000 ms; every Drain cell sets 5000 ms, because step 3's
  wall-clock bound is a competing ending that would end the bound under the `_Timing` mutant and read it GREEN.
- C-1 and C-2 reach more surfaces than Opus's text: the paragraph after E-1's table and D1 item 2 state the credentials
  half per gate, and INV-106-1 names the acceptor's hydration.
