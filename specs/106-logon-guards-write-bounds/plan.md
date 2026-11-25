# Implementation Plan: Logon guards before any answer, a time limit on every write, a configurable initiator handshake bound, and the msvc-asan job cap

**Branch**: `106-logon-guards-write-bounds` | **Date**: 2026-11-25 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `/specs/106-logon-guards-write-bounds/spec.md`, with its three Clarification
sessions of 2026-11-25 (owner rulings: the scope, #717's two parts, #714 option (b), C++ and TOML only; and from
`/speckit-clarify`: an immediate close at the write limit, every Logon refusal keeps NextNumIn, a handshake key in both
roles). **Base**: `origin/main` `9d81082a`, where the source was read. Decisions D1 to D10 are in
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

   The validate-first Reject, which runs before `interpret_logon`, is sent only after a non-consuming identity check.
   That check fails closed on unreadable credentials when a validator is installed, through the new
   `CompIdAuthorizationPolicy::has_logon_validator()`. A stay-in-arm Reject therefore does not consume the one-shot
   identity. The later gate calls, including 104's too-low branches' own, are deleted. A direct mTLS initiator
   started by `open()` has no peer identity, so the gate moves up in its arm; authorising at `open()` is not
   possible.
3. **NextNumIn consumed only on success (#703 and the class ruling; D2).** `SeqnumManager::classify_inbound`
   classifies without advancing. The advance is committed by `check_inbound` at one point per arm: on the acceptor
   before `LogonReceived`, for the reply's 789; on the initiator before `Active`, skipped when a 141=Y unit wrote
   absolute targets. Every refusal between the two points keeps NextNumIn. Research D2 tabulates them.
4. **The give-up never waits behind a write (#714a; D3, D4).** While the liveness loop is suspended in its own
   TestRequest or Heartbeat emit, a per-emit bound sleeps to the would-be give-up instant. At that instant it decides
   the give-up through the loop's own extracted decision (the slot-74 record, the Logout owner, 105's ending write
   machine, whose budget starts at the decision). The loop writes the Logout when its emit returns, or 105's limit
   closes the transport at the decision plus `logout_disconnect_timeout_ms`, which fails every blocked write.
   - An unwritten TestRequest counts from `tr_issued_at`.
   - The 3T held cap applies with no TestRequest pending too, per the owner's T119b text.
5. **The opt-in write limit (#714b; D5).** `write_timeout_ms` (C++ and TOML, 0 = off, no C setter) limits every
   non-Logout live write, counted from its `async_write` call.
   - A per-connection watchdog does the timing, with nothing allocated per write.
   - At expiry there is no Logout: a warn record, `Disconnected`, then the close. The cut write returns the new
     `session_write_timeout` (136, C mapping `FIXPP_ERR_TRANSPORT_IO`).
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

**Testing**: GoogleTest cells on the mock clock with `BlockingWriteTransport` (D2 to D5). The 104 Logon rigs (D1, D2).
Real TLS sockets only for D6. Targeted sanitizer runs (quickstart §5).

**Target Platform**: Linux (Tier 1), Windows MSVC (Tier 2: the new test targets built with `-k 0` before the first
labelled push).

**Project Type**: library (fixpp session layer, config loader, C ABI).

**Performance Goals**: no slowdown beyond +5 % on the paired `engine_send_bench` (a CI `paired` row), and locally on
`session_send_bench` and `on_inbound_frame_bench` (Article VIII §2).

**Constraints**:
- No allocation per write with the limit on or off (D5).
- One `co_spawn` per liveness emit (D3) and one per connection with the limit on (D5).
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
| VIII §2–3, §5 | +5 % paired budget; a bench in the same PR; zero allocation between parse and `fromApp` | D1/D2 touch the establishment path only. D5 adds one branch per write (off). D3 runs at liveness cadence. The paired runs are owed (quickstart §6). No inbound per-message allocation is added. |
| IX §1 | diff-scoped coverage 95/85, no uncovered error path | Every new error path has a cell: the commit refusal, the bound's spawn failure, the watchdog's exits, the TOML refusals (research D9). The verify record assesses the rest. |
| IX §2 | ASan, UBSan, TSan | targeted runs; TSan repeats the D3/D5 cells (quickstart §5) |
| X §1, §4 | the versioned contract; append-only error codes | `session_write_timeout = 136` appended; maps to the existing `FIXPP_ERR_TRANSPORT_IO`; no numeric reuse; occupancy unchanged |
| X §7 | BREAKING declared in `version.h`, the PR, B&L; in-repo consumers updated | D7: the `version.h` 1.19 entry, three `session.h` clauses, the MINOR-pin census, B-106-6 |
| XI §1–3 | coroutines; native cancellation; `async_mutex` or the one `replay_gate` exception | The emit bound and the watchdog use the wake idiom of 103's and 105's bounds (a local `steady_timer` cancelled by the strand), with no mutex and no new gate use. The emit keeps its existing shared hold. |
| XI §4 | per-session strand | every new member is strand-confined; the join counters follow `liveness_counter_`'s shape |
| XII §5, §9 | explicit profiles; the security controls | D1 changes no TLS mechanism, only the order of the identity gates. FIX-TC 1S(c) (research D1). The four controls apply. |
| XIII §2 | async logging | two warn records through the session logger (one new), registered in `format_registry.cpp` |
| XIV §2 | pluggable interfaces ≤ 5 methods | no interface change: `TransportFactory` untouched (D6 rejects a per-session `make` argument) |
| XV §1 | no per-message heap allocation on the hot path | D5: none per write (allocation cell M5). D3: per liveness emit, not per message. 105 is the precedent for per-ending spawns. |
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
include/fixpp/session/session.hpp                 # E-3, E-5 members; write_class; store_then_emit's argument; new private callees
include/fixpp/session/session_config.hpp          # SessionConfig::write_timeout_ms
include/fixpp/session/seqnum_manager.hpp          # classify_inbound
include/fixpp/session/compid_authorization_policy.hpp  # has_logon_validator
src/session/session.cpp                           # D1, D2 (both arms), D3/D4 (liveness loop, emit bound, decide_give_up_), D5 (live_write_serialized_, watchdog)
src/session/seqnum_manager.cpp                    # classify_inbound
src/session/compid_authorization_policy.cpp       # has_logon_validator
src/session/engine.cpp                            # listener bound from EngineConfig
src/config/scalar_mappers.cpp                     # write_timeout_ms
src/config/toml_config_loader.cpp                 # kRecognized; the session-scope refusal of the acceptor key
src/config/selector_resolver.cpp                  # tls_handshake_timeout_ms → Transport::Config; divergence; the root acceptor key → bundle.engine
src/capi/error.cpp                                # translate session_write_timeout
src/log/format_registry.cpp                       # the write-timeout record
include/fix/c_api/version.h                       # MINOR 19 + 1.19 history (comment)
include/fix/c_api/session.h                       # 1.19 clauses (comments)
tests/session/test_106_identity_first.cpp         # IdentityFirst106, NextNumIn106
tests/session/test_106_give_up.cpp                # GiveUp106
tests/session/test_106_write_timeout.cpp          # WriteTimeout106
tests/session/test_106_handshake.cpp              # HandshakeBound106, EngineHandshake106 (real TLS)
tests/config/…                                    # TomlWriteTimeout106, TomlHandshake106 (the config suite's file)
tests/capi/expected_error_map.csv                 # + session_write_timeout row
tests/session/CMakeLists.txt                      # the 106 binaries/labels
spec/behaviors-and-limitations.md (+ -closed.md)  # D8
```

**Structure Decision**: the existing single-library layout. The new cells go in new 106 files, as 104 and 105 did, so
the RED-on-base runs build them against the base unchanged. Cells that change existing files (the census of D9) stay
where they are.

## Phases (for /speckit-tasks; order by blocking, owner 2026-11-25)

0. **Setup.** The censuses of quickstart §1. The base worktree on `fixppbuild`. The RED-on-base harness for the 106
   binaries.
1. **P1 — B110 / #702 (security, first).** D1 in both arms. `has_logon_validator`. Delete the later gate calls.
   Cells: `IdentityFirst106.*` RED → GREEN; M1; the 104 and 099 suites unfiltered.
2. **P2 — #703 and the class ruling.** D2: `classify_inbound`, the two commit points. Cells: `NextNumIn106.*`; the
   census rows updated; M6.
3. **P3 — #714a (D3, D4).** Extract `decide_give_up_()`; the emit bound; the no-pending held cap. Cells:
   `GiveUp106.*` with M2's spurious-hit arms; the 105 and 538 guards unfiltered.
4. **P4 — #714b (D5).** The knob and TOML key; `write_class`; the watchdog; `session_write_timeout` and its C
   mapping; the record. Cells: `WriteTimeout106.*` (the allocation cell with spaced writes), `TomlWriteTimeout106.*`,
   the C-ABI map; M3–M5.
5. **P5 — #717 (D6).** The TOML session key, divergence, and the `EngineConfig` field and root key. Cells:
   `TomlHandshake106.*`, `EngineHandshake106.*`, `HandshakeBound106.*`; M7.
6. **P6 — Surfaces and records.** The `version.h` 1.19 entry and the `session.h` clauses. The B&L delta (D8). The
   supersession comments (research table). The brain page `brain/components/session.md` gains a 106 section (why and
   what was rejected) in the PR before CI (the late-docs rule).
7. **P7 — Polish.** `/simplify`, the paired benches, targeted sanitizers, MSVC `-k 0` of the new targets,
   `/speckit-verify`.

B129 needs no task beyond recording the PR's asan leg outcome (SC-006).

## Complexity Tracking

| Addition | Why needed | Simpler alternative rejected because |
|---|---|---|
| A per-emit liveness bound that can decide the give-up (D3) | the loop is suspended inside its own emit, so something else must act at the instant | an in-loop race cancels `store_then_emit` (a number taken back, `Disconnected` before the slot-74 record); detached emits re-derive 103 D5′.4; a close-only watchdog drops R-1's Logout |
| A per-connection write watchdog (D5) | a time limit on a write that only a transport close can end, without a per-write allocation | a per-write spawn allocates on the 544-clean send path; `async_write || deadline` may never return; a transport-level timeout is per factory and not on `effective_clock_` |
| A non-consuming identity precheck (D1) | the stay-in-arm validate-first Reject must not consume the one-shot identity (F-2) | a consuming gate there fails the next Logon closed (FR-005) |

## Open questions for Gate A (only where the code cannot settle them)

- **Q1 — the credentials accessor.** D1's fail-closed rule for unreadable credentials needs
  `CompIdAuthorizationPolicy::has_logon_validator()`, a public C++ addition. The alternative passes empty credentials
  to the validator, which cannot tell absent from unreadable. Accept the accessor?
- **Q2 — the Text of a give-up with no TestRequest written.** Two cases: a blocked Heartbeat emit, and a hold with
  none pending at the 3T cap. Proposed default: Text(58) `TestRequest unanswered` without the TestReqID clause; the
  slot-74 record's `test_req_id` is empty. Alternatively, close with no Logout in those two cases, as QuickFIX C++
  does at a heartbeat timeout.
- **Q3 — refusals after a reset that already ran.** Two cases:
  - the acceptor's `reset_on_logon` reset, followed by a too-high or S3 refusal: NextNumIn 1 and the store reset;
  - on the initiator, the 789 refusal after an asked or unasked 141=Y unit, which keeps the unit's durable targets
    (104 pins it in `UnaskedResetAck104.With789_TooHighAfterReset_Logout`).

  Keeping NextNumIn in either needs the reset deferred past the refusal, which changes 024's, 093's and 104's
  positions; moving S3 first breaks FR-005 (research D2). QuickFIX C++ resets at the same place. Proposed: keep the
  resets where they are and disclose (`L-106-a`). Does the owner's class ruling ("every refusal") reach these
  cases?
- **Q4 — `write_timeout_ms`'s wrong-type case.** The spec says "refused with the loader's usual error, as
  `logout_disconnect_timeout_ms` is", but that key's mapper ignores a non-integer silently. Proposed: refuse it with
  `malformed_value` (`logon_timeout_ms`'s shape).
- **Q5 — D3's 3T cap with none pending.** It applies the owner's T119b text over 105's implementation reading, which
  capped only with one pending. Confirm.

**Resolutions (orchestrator, 2026-11-25).** The owner pre-approved the plan and delegated these calls. Gate A may reopen any of them.
- Q1: accept `has_logon_validator()`.
- Q2: the Logout carries Text(58) `TestRequest unanswered` without the TestReqID clause, so the ending stays one shape with 105's R-1.
- Q3: keep the resets where they are and disclose them as L-106-a. A configured `reset_on_logon` reset and a 141=Y reset unit run only for an identity the gates admit (D1), and QuickFIX C++ resets at the same place. The spec's FR-006 is narrowed to match.
- Q4: refuse a wrong-typed value with `malformed_value`.
- Q5: confirmed. Without the cap with none pending, a resend walk whose write blocks holds the liveness loop for ever (US3 scenario 3).

Spec findings for the orchestrator (applied to spec.md in the same commit as these resolutions):
- FR-006 / SC-002 / US2 scenario 4 ("every other refusal … NextNumIn unchanged") cannot hold after a reset that ran
  earlier in the arm (Q3). The acceptor's `reset_on_logon` reset precedes the too-high and S3 refusals.
- F-3: US1 scenarios 4 and 6 for a direct `open()` initiator. There, every identity is unauthorised, so scenario 6
  cannot arise.
- F-9: the "CompIDs cannot be read … already silent" edge case does not hold on a validating session.
- Q4 above.
