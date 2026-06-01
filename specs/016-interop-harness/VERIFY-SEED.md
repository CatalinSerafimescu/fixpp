<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
# 016-interop-harness — /speckit-verify seed (T033)

> Seeds the coverage / Article IX §1 assessment that `/speckit-verify` writes into
> `.specify/decisions/016-interop-harness-verify.md`. 016 is a **tests-only**
> feature with **one** production touch — the Option-1a T008 change. This note
> enumerates the touched lines + the per-file 95/85 (line/branch) assessment so
> verify computes the binding lcov DA/BRDA gate against the right surface
> (`[const §IX.1]`; the lcov DA/BRDA basis is authoritative, NOT this estimate —
> `[[feedback_coverage_gate_lcov_basis]]`).

## Production surface touched by 016 (Option 1a, T008 — FR-004/FR-028)

| File | Change | Covering tests |
|---|---|---|
| `include/fixpp/session/session_config.hpp` (~244) | new `std::optional<ReconnectPolicy> reconnect_policy` field (`nullopt` ⇒ `defaults_quickfix_compat`) + `#include <fixpp/transport/reconnect_policy.hpp>` (value-typed member needs complete type — `[const §XV.9]` checked) | declaration-only; exercised transitively below |
| `src/session/session.cpp` (~96–111) | `resolve_reconnect_policy(cfg, arena)` free fn (2 arms: `cfg.reconnect_policy.has_value()` → use it; else `defaults_quickfix_compat`) wired into the `reconnect_fsm_` ctor (was an empty `ReconnectPolicy{}`) | `tests/interop/happy/hp_down_peer_stop_watchdog_test.cpp` (T016 — finite policy, bounded stop); `tests/interop/happy/hp_fix44_disconnect_reconnect_noreset_test.cpp` (T014); `tests/session/` reconnect suite |
| `src/session/engine.cpp` (connect path) | bound/cancel the in-flight `Transport::async_connect` so `cancellation_type::total` promptly tears down a mid-connect initiator (`reset_cancellation_state(enable_total_cancellation())` + `bind_cancellation_slot`) | `hp_down_peer_stop_watchdog_test.cpp` (T016 — `stop_within` watchdog on a never-accepting peer) |

## Article IX §1 (95 line / 85 branch) assessment

- **`resolve_reconnect_policy`** — both arms reachable: the `has_value()` arm via a
  cfg that sets `reconnect_policy` (T014/T016 use `defaults_quickfix_compat(nullptr)`
  set explicitly); the `nullopt` default arm via any session leaving it unset. Verify
  MUST confirm both BRDA branches are taken; if only one is hit in the interop
  suite, the `tests/session/` reconnect tests cover the other — enumerate fresh
  per `[[feedback_coverage_profraw_staleness]]`.
- **engine.cpp connect-cancellation lines** — exercised by the down-peer watchdog
  (mid-connect total-cancel → bounded `stop()`); the established-session close path
  is separately covered by the 015 engine tests. A line that is only reachable with
  a live counterparty (the *successful* connect arm) is a justified zero-hit under
  `[const §IX.1]` (counterparty-required, parent-side) — record as a written
  justification in verify.md, not a coverage failure (`[[feedback_codecov_patch_vs_lcov_da_brda_gate]]`).

## Carried verify expectations

- **Sanitizer matrix (T029)** — DEFERRED to this verify run (AskUserQuestion
  2026-06-01): confirm every `interop-happy` + `interop-thorny` ctest target builds +
  runs clean under `linux-clang-asan`/`-ubsan` + `-tsan` (`-j2`, one preset at a
  time). A sanitizer-only failure is a real failure (FR-019/FR-020).
- The interop suite is otherwise standalone-green (14/14 on `linux-clang-debug`:
  6 happy [skip-with-reason, no counterparty] + 3 parity + 4 thorny + 1 support;
  + the `interop_cell_results_schema_check` python ctest).
