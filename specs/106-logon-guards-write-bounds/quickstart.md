# Quickstart: validating 106

Run from the root of the tree that holds the branch (the main checkout's library submodule). One build or test process
at a time (the machine rule). **Testing scope (owner, 2026-11-25): targeted runs only**, by label or by binary on each
lane, **plus ONE full `ctest` on `linux-clang-debug`**. No full sanitizer, release or MSVC suite is run. Results go in the evidence file, never in a code comment. The rules are
[contracts/logon-guards-write-bounds-106.md](contracts/logon-guards-write-bounds-106.md), and the cells and mutants
are [research.md](research.md) D9.

## 1. Censuses (before the first code change)

Each census has a known positive that must appear. A census that reports nothing is not believed until its positive
shows.

- **Size pins** (research F-8). Run the seeded grep first; it must report `tests/dictionary/ref_shape_test.cpp`.
  Then the narrow grep for `Session`, `SessionConfig`, `EngineConfig` and `EngineEstablishment`.
- **Gate call sites** (D1): `git grep -n 'logon_identity_admitted_()\|logon_credentials_admitted_(' -- src/session`.
  On the base it shows the two arms' sites, both too-low branches and arm S. After D1, each arm keeps one consuming
  site, arm S keeps its own, and the precheck callee has its own.
- **Refusals after the advance** (D2): walk each Logon arm from its `check_inbound` call to `Active` and list every
  `co_return`. The S3 Reject of both arms must appear. Compare the list with research D2's table, and add any new
  row to the table and to the cells (round 2's result at `9d81082a` is in research D2).
- **Pinned consumed numbers** (D9): `git grep -nE 'next_in\(\)|next_inbound' -- tests/session/test_099_*.cpp tests/session/test_104_*.cpp`,
  read for assertions after a refusal ending.
- **Logout sites** (data-model E-6): `git grep -n 'build_logout(' -- src/session`, each followed to its
  `store_then_emit`. The R-1 leaf and `run_logout_phase1` must appear.
- **Bundle-to-`EngineConfig` copy sites** (D6, C-2): `git grep -nE '= *[a-z_]+(\.|->)engine\.clock' -- tests examples bindings bench`;
  known positives `tests/config/test_098_dialect_overlay.cpp` and `tests/config/test_099_toml_live.cpp`.
- **Last error enumerator** (D5): `git grep -n wire_undefined_tag -- include src tests tools bindings`.
- **`dispatch_aborted` branches** (D5): `git grep -n 'dispatch_aborted' -- src/session/session.cpp`, keeping the `==` and
  `!=` lines; the known positive is `Session::send`'s F9 block.
- **Live write sites** (D5): `git grep -n 'store_then_emit(\|live_write_serialized_(' -- src/session`; known positive
  `run_logout_phase1`.
- **Unconditional `Disconnected` writes** (D11): `git grep -n 'record_state_transition_(fsm_state::Disconnected)' --
  src/session/session.cpp`; known positives are the liveness loop's emit-failure branches and `Session::send`'s F9 block.
- **Ending claimants** (D11): `git grep -n 'logout_in_flight_ = true\|enter_ending_write_(' -- src/session/session.cpp`.
- **Sleepers** (D3, #715): `git grep -n 'sleep_until\|await_deadline' -- src/session/session.cpp`. After D3 no new hit
  may be the connection bound's shipping branch.
- **C-ABI minor pins** (D7): `git grep -nE 'MINOR 18|1\.18' -- include tests bindings tools ci`. Then
  `gh release list --exclude-drafts` at the PR head; an empty list keeps the MINOR bump.
- **session.h observers** (D7): `git grep -n 'amended in C-ABI 1.18' include/fix/c_api/session.h`.
- **Superseded records** (research, "Supersession"): each row's source is named in the comment that changes it.
  Check with `git grep -n '106 (' -- src include` at the end, against the table.

## 2. RED on the base

The base is `git merge-base HEAD origin/main`, as a scratch worktree on the build volume (owner rule: temporary
worktrees on `fixppbuild`, removed when done). Every new cell is written first and run there:

- **[RED]** cells must fail by an assertion for their stated reason (research D9, "RED evidence"), not by a crash or
  a timeout.
- **[guard: Mn]** cells are GREEN on the base by design. Each must fail under its named mutant (section 4).
- **[seam: Mn]** cells name a symbol the base lacks (`write_timeout_ms`, `session_write_timeout`,
  `EngineConfig::acceptor_tls_handshake_timeout`, `EngineEstablishment::acceptor_tls_handshake_timeout`, a new member
  or seam). The base build lacks the field, so RED is shown
  on the named mutant at the head, and recorded as such. They live in `tests/session/test_106_seams.cpp` (ctest
  `session_106_seam_tests`, labels `106` and `106_seam`). A base reading builds only `session_106_tests` and runs
  `-LE 106_seam`.

Build only the touched targets (targeted rule): the session tests binary that holds `test_106_*.cpp`, the config
tests, the transport tests and the C-ABI tests.

## 3. Cells, by phase (the plan's phase order)

| Phase | Cells | Binary / ctest label |
|---|---|---|
| P1 (D1) | `IdentityFirst106.*`; `LogonTooLow104.Precedence_*`; `UnaskedResetAck104.*`; the 099 W5 suites | session tests, labels `106`, `104`, `099` |
| P2 (D2) | `NextNumIn106.*`; the census rows of section 1 | session tests |
| P3 (D3, D4, D11) | `GiveUp106.*`; `ConnectionBound106.*`; `ShippingClock106.*`; `OneEnding106.*` (limit off); `LivenessTiming105.*`; `HeldTestRequestCap105.*`; `ReplayGate588.R588g*` and `R588h*`; `WriteLimit105.*` | session tests and the seam entry, labels `106`, `105`; the 538 binary |
| P4 (D5) | `WriteTimeout106.*`; `OneEnding106.*` (limit on); `TomlWriteTimeout106.*`; the C-ABI error-map test (`tests/capi/error_surface_test.cpp`); `alloc_guard_106_connection_bound_mallocnesia` | session seam entry, config, capi; the mallocnesia cell on `linux-clang-release` |
| P5 (D6) | `TomlHandshake106.*`; `EngineHandshake106.*`; `HandshakeBound106.*`; `AsioTlsTransportErrorPaths.HandshakeTimeoutMapsToTransportHandshakeTimeout`; `TlsClose105.*` | config, session, transport |

Example: `ctest --preset linux-clang-debug -L 106 --output-on-failure`, then the same with `-L 105` and `-L 104`.

Bounded waits use the mock clock with the budgets of research D9. The real-socket cells (P5) and the
`ShippingClock106` cell use wall-clock budgets derived there: bound + 2 s (the acceptor cell: EOF before 1.2 s, under its 1500 ms default); for `ShippingClock106`, W + 1 s against T = 2.4 s (HeartBtInt 2 s), with the write-timeout record asserted. Each spurious-hit arm is checked
before the competing deadline (10 s, `logon_timeout_ms` from accept).

## 4. Mutants (in a scratch copy, never in the PR's worktree)

| Mutant | Change | Must turn RED |
|---|---|---|
| M1 | the consuming mTLS gate before the stay-in-arm validate Reject | `IdentityFirst106.Admitted_StayInArmReject_NextLogonActive` |
| M1a | the precheck's credentials half skipped when the session is not mTLS | `IdentityFirst106.ValidateFirst_RefusedCredentials_NonMtls` |
| M1b | the consuming gate at D1 item 4 refuses an admitted identity | every `IdentityFirst106.Admitted_<Refusal>` cell (no answer) |
| M1c | `has_logon_validator()` returns true whatever is installed | `IdentityFirst106.Admitted_UnreadableNoValidator_ValidateFirstReject` (no Reject) |
| M2 | delete the loop's arming of the connection bound for its emit; and, separately, restore the cap's pending term | every `GiveUp106` cell except the guard; `GiveUp106.ResendHeldNoPending` |
| M3 | take `write_started_at_` before the `write_gate_` acquire | `WriteTimeout106.QueuedWrite_CountsFromItsOwnStart`, `ResendWalk_ProgressNotCut` |
| M3′ | delete the bound's write arming | every `WriteTimeout106.<Class>` cell |
| M3″ | the expiry does not set `write_cut_epoch_` | every `WriteTimeout106.<Class>` cell (the cut call returns `dispatch_aborted`) |
| M3‴ | `store_then_emit`'s pass-through deleted (`session_write_timeout` flattened to `dispatch_aborted`) | `WriteTimeout106.Send` (the cut `send` returns through `send_impl`'s `store_then_emit`) |
| M4 | one arm per `build_logout(` site (data-model E-6's census): that site passes `write_class::ordinary`, with M9 and M9′ (for `run_logout_phase1`, M9 and E-3's closing term deleted); alone it is masked, because every Logout is written under a claim or `closing`, which ends the bound first | the R-1 leaf: `WriteTimeout106.EndingLogout_Keeps105Limit`; `run_logout_phase1`: `WriteTimeout106.CloseLogout_KeepsGrace`; the other sites: plan.md "Deferred to tasks.md" |
| M5 | spawn a coroutine per write and per emit | `alloc_guard_106_connection_bound_mallocnesia`: the send half non-zero (spaced sends; back-to-back sends would not show it), the emit half's delta against the base positive |
| M8 | replace `connection_bound_wait_`'s shipping branch by `await_deadline(*effective_clock_, …)` | `ShippingClock106.DirectDriver_BoundBesideLiveness_TestRequestAtT` (the slot re-arm ends liveness); the mallocnesia cell (the per-call fallback allocates) |
| M9 | delete `claim_ending_`'s test (always true) | every `OneEnding106` pair whose second decider reaches `claim_ending_`, with M9′ where the second decider meets E-3's exit on a taken claim; pairs stopped earlier by a state test or an exit row are guards or take the mutant research D9 names for them |
| M9′ | delete E-3's exit on a taken claim | with M9: `OneEnding106` pairs whose second decider is the bound |
| M9″ | the establishment expiry's `claim_ending` always refused | `PreActive105.SilentPeer_StillClosedAtEstablishmentDeadlineWithEvent` (no event) |
| M10 | the pump races `on_inbound_frame` against the establishment deadline (alone, without M9) | `OneEnding106.Watchdog_PreActive_NoEstablishmentRecord` (W = 2 s, `logon_timeout_ms` 1 s: an establishment record and no write-timeout record) |
| M11 | the `write_timeout_ms` mapper recognises the key and drops its value | `TomlWriteTimeout106.Set` |
| M11′ | `SessionConfig::write_timeout_ms`'s default changed to 300 ms | `TomlWriteTimeout106.KeyAbsent_Off` |
| M12 | per joiner of data-model E-3's table: the drain's wake and its `reconnect_drain_in_flight_` term; the `Disconnected` edge's wake; the drain term with `claim_ending_`'s drain refusal; `drive_reconnect`'s respawn after a failed drain | `ConnectionBound106.Teardown_Drain_*`; `Teardown_ConnectionEnd_*`; `WriteExpiryDuringDrain_NoRecord` (LogonSent, a Logon-arm Reject's write blocked: research D9); `DrainCancelled_RespawnsOnBlockedWrite` |
| M13 | per site of research D11's `Disconnected` census that a cut write or a close returns through, one arm each: that site's `record_disconnected_unless_already_()` restored to the raw `record_state_transition_(fsm_state::Disconnected)` (the loop's TestRequest and Heartbeat tails after `store_then_emit`, `Session::send`'s F9 block, the Reject leaf's and each Logon-class write's failure tail) | the `WriteTimeout106.<Class>` cell whose cut returns through that site, and `GiveUp106.BehindBlockedSend` / `GiveUp106.RejectBlocked` for the F9 block and the Reject leaf: two `Disconnected` entries. A site no cell reaches is named unreachable in tasks.md |
| M14 | `decide_give_up_()`'s `refused` branch records `Disconnected` and returns without the close | `GiveUp106.RejectBlocked_EndingBoundSpawnFails_ClosedAtG` |
| M15 | `translate` maps `session_write_timeout` to `FIXPP_ERR_CANCELLED` | the error-map test of `tests/capi/error_surface_test.cpp` (the CSV row) |
| M6a | the acceptor arm commits the advance at the classification (the base's `check_inbound`) | every `NextNumIn106.Acceptor_*` cell |
| M6b | the initiator arm commits the advance at the classification | every `NextNumIn106.Initiator_*` cell |
| M7 | `tls_handshake_timeout_ms` read and dropped (the base's default `Transport::Config`) | `TomlHandshake106.SessionKey`, `SecondSessionDiverges` |
| M7′ | `EngineConfig::acceptor_tls_handshake_timeout` ignored (the literal back) | the C++ `EngineHandshake106` twin |
| M7‴ | the host's copy of `EngineEstablishment::acceptor_tls_handshake_timeout` deleted | `EngineHandshake106.Acceptor_ClosesAtTheKey` |
| M7″ | the factory's bound replaced by 60 s | `HandshakeBound106.DriveReconnect_OneAttempt` |
| M7⁗ | `EngineEstablishment::acceptor_tls_handshake_timeout`'s default changed (0 ms, then 2500 ms); separately, `EngineConfig::acceptor_tls_handshake_timeout`'s | `TomlHandshake106.AcceptorKeyAbsent_Default1500` |

A CMake mutant arm counts only when its reconfigure returned 0. Check the binary is fresh (`strings` for a
mutant-specific literal) before believing a GREEN.

## 5. Test scope, sanitizers and the one full run

- **Targeted only** (owner, 2026-11-25): the feature's labels and the touched binaries. That is `-L 106`, which takes
  in the seam entry; the 105, 104 and 099 session cells; the config, transport and C-ABI cells of section 3; and the
  538 binary.
- **Sanitizers:** the same set on `linux-clang-asan`, `linux-clang-tsan` and `linux-clang-ubsan`. D3 and D5 add one
  strand-confined coroutine per connection, so TSan runs the `GiveUp106`, `WriteTimeout106`, `OneEnding106` and
  `ConnectionBound106` cells 50 times (`--repeat until-fail:50`).
- **Release:** `alloc_guard_106_connection_bound_mallocnesia` and the `-L 544` registrations, on `linux-clang-release`.
- **One full `ctest`**, on `linux-clang-debug` only, as the last run before the labelled push (Article XVII §7's
  evidence).
- **MSVC:** a build of the new test targets before the first labelled push (`-k 0`; sandbox lock first).
- `/speckit-verify`'s plan follows this scope.

## 6. Benchmarks (Article VIII §2)

Locally, paired base-vs-candidate (105's T138 method: merge-base release tree, path padded to the candidate's length,
`.text` compared with `cmp`, A-B-A-B, min per tree, +5 % one-sided):

- `engine_send_bench` (also CI-paired);
- `session_send_bench`;
- `on_inbound_frame_bench`.

Record each in the evidence file. A breach needs the measurement, a rationale and an approval by someone other than
the author.

## 7. Close-out checks

- The parent repo's `.claude/scripts/check-comment-claims.py --root <tree> --base origin/main` (no results in
  comments).
- The line-citation gate over the bundle: `tools/check_line_citations.py --range origin/main..HEAD`.
- B&L:
  - `L-104-3` and `L-104-8` moved to the closed file;
  - `L-105-f` narrowed;
  - `L-093-12` amended (the liveness loop's store call among those that hold the joins; research D8);
  - `B-099-2` and `B-105-3` amended, after the census of research D8, whose known positive is `B-099-2`'s line;
  - `B-106-*` and `L-106-*` added in the LIVE file, section `## 106`.
