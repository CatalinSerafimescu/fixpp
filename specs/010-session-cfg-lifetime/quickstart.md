# Quickstart — 010-session-cfg-lifetime

**Branch**: `010-session-cfg-lifetime` | **Date**: 2026-05-23 | **Plan**: [plan.md](plan.md)

How to build, test, and ship this slice. All commands assume the working directory is the library submodule root:

```bash
cd research/G19-fix-fpml-iso20022/library
```

## Build presets

010 uses the same Tier-1 sanitizer matrix as 005 / 009. The build presets are unchanged.

```bash
# Debug (the default development preset; clangd + LSP point here)
cmake --preset linux-clang-debug && cmake --build build/linux-clang-debug -j

# Release
cmake --preset linux-clang-release && cmake --build build/linux-clang-release -j

# Sanitizers — ASan is critical for 010 FR-003 / SC-001
cmake --preset linux-clang-asan && cmake --build build/linux-clang-asan -j
cmake --preset linux-clang-ubsan && cmake --build build/linux-clang-ubsan -j
cmake --preset linux-clang-tsan && cmake --build build/linux-clang-tsan -j

# Coverage (required for /speckit-verify; SC-003 envelope rise computed here)
cmake --preset linux-clang-coverage && cmake --build build/linux-clang-coverage -j

# GCC release sanity (FR-003 also has to pass on GCC; the W-5 fix is purely
# C++ source-level and is compiler-agnostic)
cmake --preset linux-gcc-release && cmake --build build/linux-gcc-release -j
```

## Run the session test suite

```bash
ctest --preset linux-clang-debug --test-dir build/linux-clang-debug --output-on-failure -R '^session_'

# Sanitizer matrix (each one runs the full session_ set):
ctest --preset linux-clang-asan --test-dir build/linux-clang-asan --output-on-failure -R '^session_'
ctest --preset linux-clang-ubsan --test-dir build/linux-clang-ubsan --output-on-failure -R '^session_'
ctest --preset linux-clang-tsan --test-dir build/linux-clang-tsan --output-on-failure -R '^session_'
```

## The 010 acceptance gates

- **FR-003 / SC-001** — `session_coverage_adversarial` runs and passes under ASan with no `stack-use-after-scope` report:

  ```bash
  ctest --test-dir build/linux-clang-asan --output-on-failure -R '^session_coverage_adversarial$'
  ```

  After the W-5 fix (FR-001) lands, the `set_tests_properties(... DISABLED TRUE)` guard in `tests/session/CMakeLists.txt` is removed and this test runs again. ASan must report clean.

- **FR-006 / SC-002** — the new per-cell FSM matrix witness file passes 100% of cells:

  ```bash
  ctest --test-dir build/linux-clang-debug --output-on-failure -R '^session_fsm_matrix_witness$'
  ```

- **FR-007 / SC-006** — admin-builder distinct `SendingTime` test:

  ```bash
  ctest --test-dir build/linux-clang-debug --output-on-failure -R '^session_admin_builder_distinct_now$'
  ```

- **FR-008 / SC-005** — admin-emit mixed-success-mode test:

  ```bash
  ctest --test-dir build/linux-clang-debug --output-on-failure -R '^session_admin_emit_mixed_path$'
  ```

- **FR-009 / SC-007** — initiator transport-throw witness:

  ```bash
  ctest --test-dir build/linux-clang-debug --output-on-failure -R '^session_initiator_transport_throw$'
  ```

- **FR-001 / SC-001 (broader)** — cfg-lifetime safety test:

  ```bash
  ctest --test-dir build/linux-clang-asan --output-on-failure -R '^session_cfg_lifetime_safety$'
  ```

- **FR-004 / SC-002** — LogonReceived observability via the new visit-history accessor:

  ```bash
  ctest --test-dir build/linux-clang-debug --output-on-failure -R '^session_logon_received_observability$'
  ```

- **FR-005 / SC-004** — assertions on the new `session_invalid_state_for_send` error variant:

  ```bash
  ctest --test-dir build/linux-clang-debug --output-on-failure -R '^session_send_invalid_state$'
  ```

  Plus the full session suite as a regression check (any test that asserted the old `session_invalid_logon` at the send site would surface here):

  ```bash
  ctest --test-dir build/linux-clang-debug --output-on-failure -R '^session_'
  ```

## Build the codegen first, before reconfiguring

`[[project_codegen_emitter_staleness]]` — `fixpp-codegen` is the codegen emitter; non-debug build dirs compile stale `Reify.hpp` if you don't rebuild it. The 010 slice does not touch codegen, but the cross-preset workflow still requires it:

```bash
cmake --build build/linux-clang-debug --target fixpp-codegen -j
rm -rf build/linux-clang-debug/_codegen
cmake -S . -B build/linux-clang-debug   # reconfigure to pick up the emitter
```

## /speckit-verify

After `/speckit-implement` + `/simplify`, run `/speckit-verify` to produce `library/.specify/decisions/010-session-cfg-lifetime-verify.md`:

- Sanitizer matrix all green.
- Coverage envelope on `src/session/session.cpp` + `include/fixpp/session/session.hpp` (lcov DA/BRDA per `[const §IX.1]`) rises measurably vs the 009 baseline. W-1..W-4 carry-forwards (PR #82 Codecov DA/BRDA) auto-revisit here — expected to clear; otherwise re-waive with rationale per `[[feedback_codecov_patch_vs_lcov_da_brda_gate]]` + PR #73 precedent.
- ABI / static analysis / IWYU gates from `[const §IX.4]`.
- Completeness audit on **test bodies** (per `[[project_005_phase8_completeness_false_pass]]` + `[[feedback_simplify_pass_catches_9th_burn]]`) — every FR maps to a test that BINDS a runtime contract; SUCCEED-placeholders are a P1 finding.

Non-RED required for the Gate B label.

## Feature-completeness audit

Concurrent with `/speckit-verify`. Audit:
- Every `tasks.md` row is `[X]` or carries an explicit waiver rationale.
- Every spec FR- and SC- maps to a landed test AND a landed implementation **with test BODIES that bind the contract** (not file names or SUCCEED placeholders).
- `spec/feature-catalogue.md` rows touched by 010 are updated; matching `spec/coverage-index.md` entries exist.

Produces a `## Completeness` section inside `010-session-cfg-lifetime-verify.md` OR a sibling `010-session-cfg-lifetime-completeness.md`.

## /gate-b

Mandatory per `[const §XVII.2]`. Spawn:

```text
/gate-b 010-session-cfg-lifetime
```

Pre-flight checks (handled by the skill):
- `/speckit-verify` record is GREEN or YELLOW (not RED).
- Feature-completeness audit verdict is 100% or fully-waived.
- Gate A inheritance addendum is on disk at `library/.specify/decisions/010-session-cfg-lifetime-gatea.md` (synthesized at pre-flight if absent, citing 005 Gate A round 3 + research D-1/D-2/D-3).
- Author/reviewer independence preserved (each `codex:codex-rescue` agent spawn = fresh session).

Loop budget: 4 rounds (Sonnet 1/2, then Codex 1/2). Same shape as 009.

**Closing artifact**: at convergence, the slice PR description is amended via `bash .claude/scripts/gh-pr-meta.sh body-append <N> /tmp/pr<N>-gateb.md` with the Gate B outcome section; the W-5 row in `library/.specify/decisions/009-session-fsm-finalize-gateb.md` is annotated `CLOSED — see PR #<N>` (SC-009).

## Pipeline progress carry-forward

When this slice lands, mark in `phases/phase-4/session/`:
- Annotate `009-session-fsm-finalize.md` (the tracked Gate B audit) with a "010 follow-up" pointer.
- Create `phases/phase-4/session/010-session-cfg-lifetime.md` (tracked) with the Gate A inheritance + Gate B outcome + sign-off, mirroring the 009 pattern.

Update `spec/coverage-index.md`: add a line under the 005 / 009 session entries — "010-session-cfg-lifetime closed the W-5 + F-04 / F-05 / F-06 / F-07 + E1 / F-11 + RC#G mixed-path waivers on PR #82 Gate B."

Update `MEMORY.md` in the parent project memory (`/home/catalin/.claude/projects/-home-catalin-Work-Programming-Antreprenoriat/memory/`) to record the 010 lifecycle as CLOSED.
