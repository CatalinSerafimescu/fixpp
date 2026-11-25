# Quickstart: validating 106

Run from the root of the tree that holds the branch (the main checkout's library submodule). One build or test process
at a time (the machine rule). Results go in the evidence file, never in a code comment. The rules are
[contracts/logon-guards-write-bounds-106.md](contracts/logon-guards-write-bounds-106.md), and the cells and mutants
are [research.md](research.md) D9.

## 1. Censuses (before the first code change)

Each census has a known positive that must appear. A census that reports nothing is not believed until its positive
shows.

- **Size pins** (research F-8). Run the seeded grep first; it must report `tests/dictionary/ref_shape_test.cpp`.
  Then the narrow grep for `Session`, `SessionConfig` and `EngineConfig`.
- **Gate call sites** (D1): `git grep -n 'logon_identity_admitted_()\|logon_credentials_admitted_(' -- src/session`.
  On the base it shows the two arms' sites, both too-low branches and arm S. After D1, each arm keeps one consuming
  site, arm S keeps its own, and the precheck callee has its own.
- **Refusals after the advance** (D2): walk each Logon arm from its `check_inbound` call to `Active` and list every
  `co_return`. The S3 Reject of both arms must appear. Compare the list with research D2's table, and add any new
  row to the table and to the cells.
- **Pinned consumed numbers** (D9): `git grep -nE 'next_in\(\)|next_inbound' -- tests/session/test_099_*.cpp tests/session/test_104_*.cpp`,
  read for assertions after a refusal ending.
- **Logout sites** (data-model E-6): `git grep -n 'build_logout(' -- src/session`, each followed to its
  `store_then_emit`. The R-1 leaf and `run_logout_phase1` must appear.
- **Last error enumerator** (D5): `git grep -n wire_undefined_tag -- include src tests tools bindings`.
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

Build only the touched targets (targeted rule): the session tests binary that holds `test_106_*.cpp`, the config
tests, the transport tests and the C-ABI tests.

## 3. Cells, by phase (the plan's phase order)

| Phase | Cells | Binary / ctest label |
|---|---|---|
| P1 (D1) | `IdentityFirst106.*`; `LogonTooLow104.Precedence_*`; `UnaskedResetAck104.*`; the 099 W5 suites | session tests, labels `106`, `104`, `099` |
| P2 (D2) | `NextNumIn106.*`; the census rows of section 1 | session tests |
| P3 (D3, D4) | `GiveUp106.*`; `LivenessTiming105.*`; `HeldTestRequestCap105.*`; `ReplayGate588.R588g*` and `R588h*`; `WriteLimit105.*` | session tests, labels `106`, `105`, the 538 binary unfiltered |
| P4 (D5) | `WriteTimeout106.*`; `TomlWriteTimeout106.*`; the C-ABI error-map test (`tests/capi/error_surface_test.cpp`) | session, config, capi |
| P5 (D6) | `TomlHandshake106.*`; `EngineHandshake106.*`; `HandshakeBound106.*`; `AsioTlsTransportErrorPaths.HandshakeTimeoutMapsToTransportHandshakeTimeout`; `TlsClose105.*` | config, session, transport |

Example: `ctest --preset linux-clang-debug -L 106 --output-on-failure`, then the same with `-L 105` and `-L 104`.

Bounded waits use the mock clock with the budgets of research D9. The real-socket cells (P5) use
wall-clock budgets derived there: bound + 2 s, with each spurious-hit arm checked before the competing deadline
(10 s, `logon_timeout_ms` from accept).

## 4. Mutants (in a scratch copy, never in the PR's worktree)

| Mutant | Change | Must turn RED |
|---|---|---|
| M1 | the consuming mTLS gate before the stay-in-arm validate Reject | `IdentityFirst106.Admitted_StayInArmReject_NextLogonActive` |
| M2 | delete `run_liveness_emit_bound_`'s spawn; and, separately, restore the cap's pending term | every `GiveUp106` cell except the guard; `GiveUp106.ResendHeldNoPending` |
| M3 | take `write_started_at_` before the `write_gate_` acquire | `WriteTimeout106.QueuedWrite_CountsFromItsOwnStart`, `ResendWalk_ProgressNotCut` |
| M4 | pass `write_class::ordinary` from every Logout site | `WriteTimeout106.EndingLogout_Keeps105Limit` |
| M5 | spawn a watchdog per write | `WriteTimeout106.On_NoAllocationPerWrite` (spaced sends; back-to-back sends would not show it) |
| M6 | commit the advance at the classification (the base's `check_inbound`) | every `NextNumIn106` cell |
| M7 | `tls_handshake_timeout_ms` read and dropped (the base's default `Transport::Config`) | `TomlHandshake106.SessionKey`, `SecondSessionDiverges` |

A CMake mutant arm counts only when its reconfigure returned 0. Check the binary is fresh (`strings` for a
mutant-specific literal) before believing a GREEN.

## 5. Sanitizers (targeted)

Only the feature's labels and the touched binaries: `-L 106` and the 105 and 104 session cells, on
`linux-clang-asan`, `linux-clang-tsan` and `linux-clang-ubsan`. D3 and D5 add strand-confined coroutines, so TSan
runs the `GiveUp106` and `WriteTimeout106` cells 50 times (`--repeat until-fail:50`). The MSVC build of the new test
targets runs before the first labelled push (`-k 0`; sandbox lock first).

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
  - `B-106-*` and `L-106-*` added in the LIVE file, section `## 106`.
